# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Passstifte schützen (zweiseitig).

Zweiseitige Teile werden über zwei Passlöcher auf der Wendeachse ausgerichtet:
Seite 1 bohrt sie durch das Werkstück bis ins Opferbrett, vor Seite 2 kommen
Passstifte hinein und das gewendete Werkstück darauf. Darum darf um die Löcher
auf keiner Seite gefräst werden:

- auf Seite 2 stecken die Stifte darin – ein Fräser darüber träfe den Stahl;
- auf Seite 1 würde die Lochwand weggefräst, das Teil sässe nach dem Wenden
  nicht mehr genau.

Jede Bahn, die unter der Oberfläche in den Sperrkreis um ein Passloch käme
(Lochradius + Fräserradius + Abstand), hebt davor auf die Freifahrhöhe ab, fährt
darüber und taucht danach im Eintauchvorschub wieder ein. Was im Kreis liegt,
bleibt stehen. Die Länge der Stifte spielt so keine Rolle.
"""
from __future__ import annotations

import math
from typing import Iterable, List, Optional, Sequence, Tuple

from .model import Move, Toolpath

ABSTAND = 1.0          # mm zwischen Fräserrand und Lochrand
_EPS = 1e-9


def sperrkreise(auftrag: dict, fraeser_d: float, abstand: float = ABSTAND) -> List[Tuple[float, float, float]]:
    """(x, y, r) je Passloch, r = Lochradius + Fräserradius + Abstand; leer, wenn
    der Auftrag nicht zweiseitig ist oder keine Passlöcher hat."""
    zw = dict(auftrag.get("zweiseitig") or {})
    if not zw.get("an") or not zw.get("passloecher", True):
        return []
    flaeche = auftrag.get("material_flaeche")
    if not flaeche:
        return []
    from .einstellungen import passloecher
    r = float(zw.get("pass_d") or 6.0) / 2 + max(0.0, float(fraeser_d)) / 2 + abstand
    return [(float(x), float(y), r) for x, y in passloecher({"zweiseitig": zw}, flaeche[0], flaeche[1])]


def schraube(auftrag: dict, fraeser_d: float, abstand: float = ABSTAND) -> List[Tuple[float, float, float]]:
    """Rundes Material, mit einer Schraube durch die Mitte befestigt: Sperrkreis um Kopf bzw.
    Scheibe (Kopf-Ø / 2 + Fräserradius + Abstand); leer ohne rundes Material oder Schraube."""
    ru = auftrag.get("rund") or {}
    if not ru or not ru.get("schraube"):
        return []
    kopf = max(float(ru.get("kopf") or 0), float(ru.get("bohrung") or 0))
    if kopf <= 0:
        return []
    cx, cy = (float(v) for v in ru.get("mitte") or (0, 0))
    return [(cx, cy, kopf / 2 + max(0.0, float(fraeser_d)) / 2 + abstand)]


def _innen(px, py, kreise) -> bool:
    return any((px - x) ** 2 + (py - y) ** 2 < r * r - 1e-9 for x, y, r in kreise)


def _intervalle(a, b, kreise) -> List[Tuple[float, float]]:
    """t-Abschnitte von a→b (XY), die in einem Kreis liegen, zusammengefasst."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    L2 = dx * dx + dy * dy
    aus = []
    if L2 < _EPS:
        if _innen(a[0], a[1], kreise):
            aus.append((0.0, 1.0))
        return aus
    for x, y, r in kreise:
        fx, fy = a[0] - x, a[1] - y
        B = 2 * (fx * dx + fy * dy)
        C = fx * fx + fy * fy - r * r
        disk = B * B - 4 * L2 * C
        if disk <= 0:
            continue
        s = math.sqrt(disk)
        t0, t1 = (-B - s) / (2 * L2), (-B + s) / (2 * L2)
        t0, t1 = max(0.0, t0), min(1.0, t1)
        if t1 - t0 > 1e-9:
            aus.append((t0, t1))
    aus.sort()
    zus: List[List[float]] = []
    for t0, t1 in aus:
        if zus and t0 <= zus[-1][1] + 1e-12:
            zus[-1][1] = max(zus[-1][1], t1)
        else:
            zus.append([t0, t1])
    return [(a_, b_) for a_, b_ in zus]


def aussparen(tp: Toolpath, kreise: Sequence[Tuple[float, float, float]], freifahr: float,
              eintauchen: Optional[float], start: Tuple[float, float, float] = (0.0, 0.0, 20.0),
              oberflaeche: float = 0.0) -> int:
    """Bahnen um die Sperrkreise herumführen (abheben, darüber, wieder eintauchen).
    Gibt zurück, wie oft abgehoben wurde (0 = Bahn unverändert)."""
    if not kreise or not tp.moves:
        return 0
    neu: List[Move] = []
    q = start                      # wo das Werkzeug laut Originalbahn steht
    oben = False                   # abgehoben (auf Freifahrhöhe über dem Sperrkreis)
    lifts = 0
    pos = start                    # wo das Werkzeug wirklich steht

    def stueck(m: Move, a, b):
        """erlaubtes Teilstück a→b der Bewegung m ausgeben"""
        nonlocal oben, pos
        if oben:
            if math.dist((a[0], a[1]), (pos[0], pos[1])) > 1e-9:
                neu.append(Move(True, a[0], a[1], max(freifahr, pos[2])))
            neu.append(Move(False, a[0], a[1], a[2], eintauchen or m.feed))
            oben = False
        if math.dist(b, pos) > 1e-9 or not neu:
            neu.append(Move(m.rapid, b[0], b[1], b[2], m.feed))
        pos = b

    for m in tp.moves:
        b = (m.x, m.y, m.z)
        ivs = [(t0, t1) for t0, t1 in _intervalle(q, b, kreise)
               if min(q[2] + (b[2] - q[2]) * t0, q[2] + (b[2] - q[2]) * t1) < oberflaeche - 1e-6]
        if not ivs:
            if oben:
                stueck(m, q, b)
            else:
                neu.append(Move(m.rapid, m.x, m.y, m.z, m.feed))
                pos = b
            q = b
            continue
        if m.rapid:
            # Eilgang unter der Oberfläche, der in einen Sperrkreis käme: gleich hier abheben (nicht erst
            # am Kreis – dort steht das Material, um das herum gefräst wurde), oben hinüber, am Ziel
            # ausserhalb im Eintauchvorschub wieder hinunter
            if not oben:
                h = max(freifahr, pos[2])
                neu.append(Move(True, pos[0], pos[1], h))
                pos = (pos[0], pos[1], h)
                oben = True
                lifts += 1
            if math.dist((b[0], b[1]), (pos[0], pos[1])) > 1e-9:
                neu.append(Move(True, b[0], b[1], pos[2]))
                pos = (b[0], b[1], pos[2])
            if not _innen(b[0], b[1], kreise):
                if b[2] < pos[2] - 1e-9:
                    neu.append(Move(False, b[0], b[1], b[2], eintauchen or m.feed))
                pos = b
                oben = False
            q = b
            continue
        punkt = lambda t: (q[0] + (b[0] - q[0]) * t, q[1] + (b[1] - q[1]) * t, q[2] + (b[2] - q[2]) * t)
        t = 0.0
        for t0, t1 in ivs:
            if t0 > t + 1e-12:
                stueck(m, punkt(t), punkt(t0))
            if not oben:
                e = punkt(t0)
                if math.dist(e, pos) > 1e-9:
                    neu.append(Move(m.rapid, e[0], e[1], e[2], m.feed))
                h = max(freifahr, e[2])
                neu.append(Move(True, e[0], e[1], h))
                pos = (e[0], e[1], h)
                oben = True
                lifts += 1
            x = punkt(t1)
            if math.dist((x[0], x[1]), (pos[0], pos[1])) > 1e-9:
                neu.append(Move(True, x[0], x[1], pos[2]))
                pos = (x[0], x[1], pos[2])
            t = t1
        if t < 1.0 - 1e-12:
            stueck(m, punkt(t), b)
        q = b
    if lifts:
        tp.moves = neu
        tp.zyklen = []             # Abschnitte stimmen nicht mehr: Bohrungen als Bewegungen
    return lifts


def kontur_durch_stift(tp: Toolpath, kreise: Iterable[Tuple[float, float, float]]) -> bool:
    """Ob eine Bahn einen Sperrkreis kreuzt (dann bleibt dort Material stehen)."""
    kreise = list(kreise)
    q = None
    for m in tp.moves:
        b = (m.x, m.y, m.z)
        if q is not None and min(q[2], b[2]) < 0 and _intervalle(q, b, kreise):
            return True
        q = b
    return False
