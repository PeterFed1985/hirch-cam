# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Bögen erkennen: Folgen kurzer Geraden, die auf einem Kreis liegen, als
G2/G3-Bogen ausgeben (wie die «arc fitting» in FreeCAD CAM).

Rechnerisch bleibt alles Geraden (Prüfung, Vorschau, Zeiten) – nur der
Postprozessor fasst beim Schreiben zusammen. Ein Bogen wird nur genommen,
wenn jeder Punkt und jede Sehnenmitte höchstens «toleranz» vom Kreis
abweicht, die Drehrichtung gleich bleibt und Z gleichmässig mit dem Winkel
läuft (Schraube: Helix-Bohren, Gewinde). Höchstens ein Halbkreis je Bogen,
damit jede Steuerung ihn eindeutig liest.
"""
from __future__ import annotations

import math
from collections import OrderedDict
from typing import List, Optional, Sequence, Tuple

P3 = Tuple[float, float, float]

MIN_PUNKTE = 4           # mindestens so viele Geraden für einen Bogen
MAX_RADIUS = 2000.0
MAX_WINKEL = math.pi     # je Bogen höchstens 180°
SEHNE = 0.025            # so weit dürfen die Geraden (Sehnen) vom Bogen abweichen: die
                         # Rundungen sind beim Rechnen auf ~0.01 mm in Geraden aufgelöst,
                         # echte Ecken (Sechseck usw.) weichen viel mehr ab


def _kreis(a, b, c) -> Optional[Tuple[float, float, float]]:
    """Mittelpunkt und Radius des Kreises durch drei Punkte (x, y)."""
    ax, ay = a[0], a[1]
    bx, by = b[0], b[1]
    cx, cy = c[0], c[1]
    d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by))
    if abs(d) < 1e-12:
        return None
    a2, b2, c2 = ax * ax + ay * ay, bx * bx + by * by, cx * cx + cy * cy
    ux = (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d
    uy = (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d
    return ux, uy, math.hypot(ax - ux, ay - uy)


def _passt(pts: Sequence[P3], i: int, j: int, toleranz: float):
    """Liegen pts[i..j] auf einem Bogen? Liefert (mx, my, r, gegen_uhrzeiger) oder None."""
    k = _kreis(pts[i], pts[(i + j) // 2], pts[j])
    if k is None:
        return None
    mx, my, r = k
    if r > MAX_RADIUS or r < 0.01:
        return None
    # Drehrichtung und Gesamtwinkel
    w0 = math.atan2(pts[i][1] - my, pts[i][0] - mx)
    summe = 0.0
    richtung = 0
    vor = w0
    for q in range(i + 1, j + 1):
        w = math.atan2(pts[q][1] - my, pts[q][0] - mx)
        dw = (w - vor + math.pi) % (2 * math.pi) - math.pi
        if abs(dw) < 1e-12:
            return None
        s = 1 if dw > 0 else -1
        if richtung and s != richtung:
            return None
        richtung = s
        summe += dw
        vor = w
        if abs(summe) > MAX_WINKEL + 1e-9:
            return None
        if dw * s > math.pi / 3:          # zu grobe Teilung: lieber Geraden
            return None
    # Abweichung der Punkte und der Sehnenmitten
    for q in range(i, j + 1):
        if abs(math.hypot(pts[q][0] - mx, pts[q][1] - my) - r) > toleranz:
            return None
    for q in range(i, j):
        sx = (pts[q][0] + pts[q + 1][0]) / 2
        sy = (pts[q][1] + pts[q + 1][1]) / 2
        if abs(math.hypot(sx - mx, sy - my) - r) > max(toleranz, SEHNE):
            return None
    # Z muss linear mit dem Winkel laufen (eben oder Schraube)
    z0, z1 = pts[i][2], pts[j][2]
    winkel = 0.0
    vor = w0
    for q in range(i + 1, j + 1):
        w = math.atan2(pts[q][1] - my, pts[q][0] - mx)
        winkel += abs((w - vor + math.pi) % (2 * math.pi) - math.pi)
        vor = w
        soll = z0 + (z1 - z0) * winkel / abs(summe)
        if abs(pts[q][2] - soll) > toleranz:
            return None
    return mx, my, r, richtung > 0


_GEMERKT: "OrderedDict[tuple, list]" = OrderedDict()
_PUNKTE_HOECHSTENS = 2_000_000          # alle gemerkten Folgen zusammen (3D-Bahnen sind gross)
_punkte = [0]


def zerlegen(pts: Sequence[P3], toleranz: float = 0.005):
    """Punkte (Start + Ziele) in Stücke teilen. Liefert Liste von
    ("linie", ziel, j) bzw. ("bogen", ziel, j, mx, my, gegen_uhrzeiger);
    j = Index des Zielpunkts in pts.

    Gemerkt nach den genauen Punkten (Rechendienst, Fräsbahnen 25.10.): kommen die Bahnen aus dem
    Zwischenspeicher, sind die Punkte dieselben und das Suchen der Bögen entfällt."""
    if len(pts) > _PUNKTE_HOECHSTENS // 10:
        return _zerlegen(pts, toleranz)
    schl = (tuple(tuple(p) for p in pts), float(toleranz))
    alt = _GEMERKT.get(schl)
    if alt is None:
        alt = _zerlegen(pts, toleranz)
        _GEMERKT[schl] = alt
        _punkte[0] += len(pts)
        while _punkte[0] > _PUNKTE_HOECHSTENS and _GEMERKT:
            k, _v = _GEMERKT.popitem(last=False)
            _punkte[0] -= len(k[0])
    else:
        _GEMERKT.move_to_end(schl)
    return list(alt)


def _zerlegen(pts: Sequence[P3], toleranz: float):
    aus = []
    i = 0
    n = len(pts)
    while i < n - 1:
        best = None
        # erst verdoppeln, dann eingrenzen (die Prüfung kostet je Bogen O(Länge))
        schritt = MIN_PUNKTE
        gut, schlecht = None, None
        while i + schritt < n:
            f = _passt(pts, i, i + schritt, toleranz)
            if f is None:
                schlecht = i + schritt
                break
            gut = (i + schritt, f)
            schritt *= 2
        if gut is None and schlecht is None and n - 1 - i >= MIN_PUNKTE:
            f = _passt(pts, i, n - 1, toleranz)
            if f is not None:
                gut = (n - 1, f)
        if gut is not None:
            lo = gut[0]
            hi = schlecht if schlecht is not None else n
            if schlecht is None and lo < n - 1:
                f = _passt(pts, i, n - 1, toleranz)
                if f is not None:
                    gut, lo, hi = (n - 1, f), n - 1, n
                else:
                    hi = n - 1
            while hi - lo > 1:
                mitte = (lo + hi) // 2
                f = _passt(pts, i, mitte, toleranz)
                if f is None:
                    hi = mitte
                else:
                    gut, lo = (mitte, f), mitte
            best = gut
        if best is None:
            aus.append(("linie", pts[i + 1], i + 1))
            i += 1
            continue
        j, (mx, my, _r, ccw) = best
        aus.append(("bogen", pts[j], j, mx, my, ccw))
        i = j
    return aus


def bogen_punkte(start: P3, ziel: P3, mx: float, my: float, ccw: bool, toleranz: float = 0.01) -> List[P3]:
    """Bogen wieder in Geraden auflösen (zum Einlesen von G2/G3)."""
    r = math.hypot(start[0] - mx, start[1] - my)
    w0 = math.atan2(start[1] - my, start[0] - mx)
    w1 = math.atan2(ziel[1] - my, ziel[0] - mx)
    if ccw:
        dw = (w1 - w0) % (2 * math.pi)
    else:
        dw = -((w0 - w1) % (2 * math.pi))
    if abs(dw) < 1e-9 and math.dist(start[:2], ziel[:2]) < 1e-9:
        dw = 2 * math.pi if ccw else -2 * math.pi        # Vollkreis
    if r > toleranz:
        schritt = 2 * math.acos(max(-1.0, 1 - toleranz / r))
    else:
        schritt = math.pi / 4
    k = max(1, math.ceil(abs(dw) / max(schritt, 1e-3)))
    aus = []
    for q in range(1, k + 1):
        w = w0 + dw * q / k
        aus.append((mx + r * math.cos(w), my + r * math.sin(w), start[2] + (ziel[2] - start[2]) * q / k))
    return aus


def bogenlaenge(start: P3, ziel: P3, mx: float, my: float, ccw: bool) -> float:
    r = math.hypot(start[0] - mx, start[1] - my)
    w0 = math.atan2(start[1] - my, start[0] - mx)
    w1 = math.atan2(ziel[1] - my, ziel[0] - mx)
    dw = (w1 - w0) % (2 * math.pi) if ccw else (w0 - w1) % (2 * math.pi)
    if dw < 1e-9:
        dw = 2 * math.pi
    return math.hypot(r * dw, ziel[2] - start[2])
