# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Gravur: Das Werkzeug fährt auf der Linie (Mitte des Fräsers auf der
Linie), in gleich grossen Zustellungen bis zur Endtiefe.

Ablauf pro Linie (wie üblich):
    Eilgang auf Freifahrhöhe über den Startpunkt, Eilgang auf Startabstand
    (z. B. 0.5 mm), dann je Zustellung senkrecht eintauchen und die Linie
    abfahren. Geschlossene Linien beginnen jede Runde am selben Punkt,
    offene Linien werden abwechselnd hin und zurück gefahren. Danach
    Eilgang auf Freifahrhöhe.

Die Linien werden so sortiert, dass die Leerfahrten kurz bleiben.
"""
from __future__ import annotations

import math
from typing import List, Sequence, Tuple

from ..model import Move, Toolpath

Punkt = Tuple[float, float]
EPS = 1e-6


class Linie:
    def __init__(self, punkte: Sequence[Punkt], geschlossen: bool | None = None):
        pts = [tuple(map(float, p)) for p in punkte]
        # doppelte Punkte direkt hintereinander entfernen
        clean: List[Punkt] = []
        for p in pts:
            if not clean or math.dist(p, clean[-1]) > EPS:
                clean.append(p)
        if geschlossen is None:
            geschlossen = len(clean) > 2 and math.dist(clean[0], clean[-1]) <= EPS
        if geschlossen and math.dist(clean[0], clean[-1]) > EPS:
            clean.append(clean[0])
        self.punkte = clean
        self.geschlossen = geschlossen


def zustellungen(tiefe: float, zustellung: float) -> List[float]:
    """Gleich grosse Schritte bis zur Endtiefe (negativ), wie üblich."""
    if tiefe <= 0:
        raise ValueError("Tiefe muss grösser als 0 sein")
    if zustellung <= 0:
        zustellung = tiefe
    n = max(1, math.ceil(tiefe / zustellung - 1e-9))
    return [-tiefe * k / n for k in range(1, n + 1)]


GROSS = 40          # ab so vielen Linien: Reihenfolge mit 2-opt (Beispiel mit vielen Textlinien, 10.11.)


def _sortieren(linien: List[Linie], start: Punkt, n_tiefen: int = 1) -> List[Linie]:
    """Nächster-Nachbar: immer die Linie, deren Anfang (oder bei offenen
    Linien auch das Ende, bei geschlossenen jeder Punkt) am nächsten liegt.
    Viele Linien (Text): danach 2-opt über den kürzesten Abstand zwischen den Linien – der nächste Nachbar allein
    sprang im Text zwischen Zeilen und Spalten hin und her (Beispiel mit vielen Textlinien, Peter 10.11.)."""
    offen = [l for l in linien if len(l.punkte) >= 2]
    if len(offen) >= GROSS:
        return _eintauchstellen(_kurz(offen, start), start, n_tiefen)
    ergebnis = []
    pos = start
    while offen:
        best = None
        for i, l in enumerate(offen):
            if l.geschlossen:
                for j, p in enumerate(l.punkte[:-1]):
                    d = math.dist(pos, p)
                    if best is None or d < best[0]:
                        best = (d, i, "dreh", j)
            else:
                for art, p in (("vor", l.punkte[0]), ("rueck", l.punkte[-1])):
                    d = math.dist(pos, p)
                    if best is None or d < best[0]:
                        best = (d, i, art, 0)
        _, i, art, j = best
        l = offen.pop(i)
        if art == "dreh" and j:
            ring = l.punkte[:-1]
            ring = ring[j:] + ring[:j]
            l = Linie(ring + [ring[0]], True)
        elif art == "rueck":
            l = Linie(list(reversed(l.punkte)), False)
        ergebnis.append(l)
        pos = l.punkte[-1]
    return _eintauchstellen(ergebnis, start, n_tiefen)


def _kurz(linien: List[Linie], start: Punkt) -> List[Linie]:
    """Reihenfolge vieler Linien: nächster Nachbar, danach 2-opt mit Richtung – ein umgedrehter Abschnitt fährt
    jede offene Linie darin rückwärts; gezählt wird der Weg vom Ende der einen zum Anfang der nächsten (Beispiel mit vielen Textlinien
    10.11.). Geschlossene Linien: Ein- und Austritt an der Ecke, die beim nächsten Nachbarn am nächsten lag. Die
    Eintauchstellen wählt danach _eintauchstellen über alle Linien zusammen."""
    import numpy as np
    n = len(linien)
    A = np.empty((n, 2))
    E = np.empty((n, 2))
    zu = np.zeros(n, bool)
    for k, l in enumerate(linien):
        A[k], E[k], zu[k] = l.punkte[0], l.punkte[-1], l.geschlossen
    # nächster Nachbar (offene Linien in beiden Richtungen, geschlossene an jeder Ecke)
    frei = np.ones(n, bool)
    pos = np.asarray(start, float)
    folge, sa, se = [], [], []
    for _ in range(n):
        da = np.hypot(*(A - pos).T)
        de = np.where(zu, np.inf, np.hypot(*(E - pos).T))
        d = np.where(frei, np.minimum(da, de), np.inf)
        dz = {}
        for k in np.flatnonzero(frei & zu):
            if da[k] - 1e-9 <= d.min() + 50:          # nur nahe geschlossene genau ansehen
                pts = np.asarray(linien[k].punkte[:-1], float)
                dd = np.hypot(*(pts - pos).T)
                dz[k] = (dd.min(), int(dd.argmin()))
                d[k] = dz[k][0]
        k = int(np.argmin(d))
        frei[k] = False
        if zu[k]:
            q = linien[k].punkte[dz[k][1]] if k in dz else linien[k].punkte[0]
            a = e = np.asarray(q, float)
        elif de[k] < da[k]:
            a, e = E[k], A[k]
        else:
            a, e = A[k], E[k]
        folge.append(k)
        sa.append(a)
        se.append(e)
        pos = e
    t = np.asarray(folge)
    S, Z = np.asarray(sa, float), np.asarray(se, float)
    st = np.asarray(start, float)
    for _runde in range(60):
        besser = False
        for i in range(n - 1):
            vor = st if i == 0 else Z[i - 1]
            js = np.arange(i + 1, n)
            nach_ok = js + 1 < n
            nach = S[np.minimum(js + 1, n - 1)]
            alt = np.hypot(*(S[i] - vor)) + np.where(nach_ok, np.hypot(*(Z[js] - nach).T), 0.0)
            neu = np.hypot(*(Z[js] - vor).T) + np.where(nach_ok, np.hypot(*(S[i] - nach).T), 0.0)
            g = alt - neu
            j = int(np.argmax(g))
            if g[j] > 1e-9:
                jj = int(js[j])
                t[i:jj + 1] = t[i:jj + 1][::-1].copy()
                S[i:jj + 1], Z[i:jj + 1] = Z[i:jj + 1][::-1].copy(), S[i:jj + 1][::-1].copy()
                besser = True
        if not besser:
            break
    return [linien[int(k)] for k in t]


def _eintauchstellen(folge: List[Linie], start: Punkt, n_tiefen: int) -> List[Linie]:
    """In der gefundenen Reihenfolge je Linie die Eintauchstelle über alle Linien zusammen wählen (Peter 27.10.:
    kürzester Weg über die Eintauchstellen, nicht nur zur nächsten): geschlossene Linien an jeder Ecke, offene an
    einem der Enden – bei gerader Zahl Zustellungen endet eine offene Linie dort, wo sie begann."""
    from .bahn import eintauch_wahl
    if len(folge) < 2:
        return folge
    kand = []
    for l in folge:
        p = l.punkte
        if l.geschlossen:
            kand.append([(q, q) for q in p[:-1]])
        else:
            gerade = n_tiefen % 2 == 0
            kand.append([(p[0], p[0] if gerade else p[-1]), (p[-1], p[-1] if gerade else p[0])])
    wahl = eintauch_wahl(kand, start)
    aus = []
    for l, j in zip(folge, wahl):
        if l.geschlossen:
            ring = l.punkte[:-1]
            ring = ring[j:] + ring[:j]
            aus.append(Linie(ring + [ring[0]], True))
        else:
            aus.append(l if j == 0 else Linie(list(reversed(l.punkte)), False))
    return aus


def gravur(
    linien: Sequence[Linie],
    tiefe: float,
    zustellung: float,
    vorschub: float,
    eintauchen: float,
    freifahrhoehe: float = 3.0,
    startabstand: float = 0.5,
    start: Punkt = (0.0, 0.0),
    name: str = "Gravur",
    rampe_winkel: float = 0.0,
) -> Toolpath:
    """rampe_winkel > 0 (Fräser ohne Mittenschnitt, Sicherheitsprüfung 04.11.): je Ebene schräg auf der Linie
    eintauchen statt senkrecht (ops/bahn.Bahnbauer.rampe)."""
    tiefen = zustellungen(tiefe, zustellung)
    moves: List[Move] = []
    cz = freifahrhoehe
    for l in _sortieren(list(linien), start, len(tiefen)):
        pts = l.punkte
        x0, y0 = pts[0]
        if moves:
            moves.append(Move(True, moves[-1].x, moves[-1].y, cz))
        moves.append(Move(True, x0, y0, cz))
        moves.append(Move(True, x0, y0, startabstand))
        weg = pts
        z_vor = 0.0
        for z in tiefen:
            x, y = weg[0]
            if rampe_winkel > 0 and len(weg) > 1:
                from .bahn import Bahnbauer
                q = moves[-1]
                bb = Bahnbauer(vorschub, eintauchen, cz, startabstand, (q.x, q.y, q.z))
                bb.rampe(z, weg[1:], rampe_winkel, z_von=z_vor)
                moves.extend(bb.moves)
            else:
                moves.append(Move(False, x, y, z, eintauchen))
            z_vor = z
            for x, y in weg[1:]:
                moves.append(Move(False, x, y, z, vorschub))
            if not l.geschlossen:
                weg = list(reversed(weg))
    if moves:
        moves.append(Move(True, moves[-1].x, moves[-1].y, cz))
    return Toolpath(name=name, moves=moves)


def bahnlaenge(tp: Toolpath, start=(0.0, 0.0, 20.0)):
    """Summe der Vorschub- und Eilgangwege in mm und geschätzte Zeit in
    Minuten (Eilgang mit 3000 mm/min angenommen)."""
    p = start
    vor = eil = minuten = 0.0
    for m in tp.moves:
        d = math.dist(p, (m.x, m.y, m.z))
        if m.rapid:
            eil += d
            minuten += d / 3000.0
        else:
            vor += d
            minuten += d / m.feed
        p = (m.x, m.y, m.z)
    return vor, eil, minuten
