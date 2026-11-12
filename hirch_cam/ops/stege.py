# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Haltestege gleichmässig verteilen (Peter 25.10.2026, Entwurf B: Haltestege sind immer gelbe Marken).

Die Oberfläche setzt die Marken; dieses Modul schlägt vor, wo sie hinkommen: je Kontur `anzahl` Punkte auf
der Fräsbahn-Mitte, möglichst gleich weit voneinander, aber **nicht in Ecken und engen Bögen** – dort hält
ein Steg schlecht, lässt sich schlecht versäubern, und der Fräser fährt in der Ecke ohnehin langsamer.

Die Kontur rechnet aus den Marken (ops/kontur.py, `stege_punkte`): jede Marke gehört zur nächsten Kontur
und wird auf die Fräsbahn projiziert. Die Punkte von hier liegen genau auf der Fräsbahn-Mitte.

Vorgehen je Kontur (Länge L, Abstand D = L / anzahl):
1. Für Stellen im Abstand von ~0,25 mm die Richtungsänderung der Bahn im «Fussabdruck» des Stegs
   (Stegbreite + Fräser-Ø, ein Fräserradius vor und nach dem Steg) – gut ist eine Stelle, an der sich die
   Bahn dort um höchstens KNICK Grad dreht (gerade Stücke, weite Bögen).
2. Die Lage des ganzen Satzes (Phase) so wählen, dass die Stege mit möglichst wenig Verschieben auf gute
   Stellen fallen; jeder Steg darf höchstens 35 % von D aus seiner gleichmässigen Lage rücken.
3. Hat eine Kontur gar keine gute Stelle (Kreis, kleines Teil), zählen die geradesten Stellen als gut – bei
   einem Kreis heisst das: gleichmässig.
"""
from __future__ import annotations

import math
from typing import Dict, List, Sequence, Tuple

Punkt = Tuple[float, float]

KNICK = 20.0          # Grad: so viel darf sich die Bahn unter einem Steg drehen
SCHUB = 0.35          # Anteil des Abstands, um den ein Steg rücken darf
EPS = 1e-9


def _laengen(ring: Sequence[Punkt]):
    zu = list(ring) + [ring[0]]
    s = [0.0]
    for a, b in zip(zu, zu[1:]):
        s.append(s[-1] + math.dist(a, b))
    return zu, s


def _knicke(ring: Sequence[Punkt]) -> List[float]:
    """Richtungsänderung an jedem Punkt des geschlossenen Rings (Bogenmass, ≥ 0)."""
    n = len(ring)
    aus = []
    for i in range(n):
        a, b, c = ring[i - 1], ring[i], ring[(i + 1) % n]
        w1 = math.atan2(b[1] - a[1], b[0] - a[0])
        w2 = math.atan2(c[1] - b[1], c[0] - b[0])
        d = abs((w2 - w1 + math.pi) % (2 * math.pi) - math.pi)
        aus.append(d if math.dist(a, b) > EPS and math.dist(b, c) > EPS else 0.0)
    return aus


def _punkt_bei(zu: Sequence[Punkt], s: Sequence[float], weg: float) -> Punkt:
    import bisect
    L = s[-1]
    weg = weg % L if L > 0 else 0.0
    i = max(0, min(len(zu) - 2, bisect.bisect_right(s, weg) - 1))
    seg = s[i + 1] - s[i]
    t = 0.0 if seg <= EPS else (weg - s[i]) / seg
    a, b = zu[i], zu[i + 1]
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


def verteilen(ring: Sequence[Punkt], anzahl: int, breite: float, werkzeug_d: float,
              knick: float = KNICK) -> List[Punkt]:
    """`anzahl` Stegmitten auf dem geschlossenen Ring (Fräsbahn-Mitte), nicht in Ecken/engen Bögen.

    Leer, wenn die Kontur zu kurz ist (wie bisher: Umfang ≤ 2 × anzahl × Breite)."""
    ring = [(float(p[0]), float(p[1])) for p in ring]
    if len(ring) > 1 and math.dist(ring[0], ring[-1]) <= EPS:
        ring = ring[:-1]
    if anzahl <= 0 or breite <= 0 or len(ring) < 3:
        return []
    zu, s = _laengen(ring)
    L = s[-1]
    if L <= anzahl * breite * 2:
        return []
    D = L / anzahl
    halb = breite / 2 + max(0.0, werkzeug_d)            # Steg + je ein Fräser-Ø davor und danach
    halb = min(halb, 0.45 * D)
    # Knicke an den Punkten, als Weglänge (zweimal hintereinander für den Umlauf)
    kn = _knicke(ring)
    lage = s[:-1]
    import bisect
    pos2 = lage + [x + L for x in lage] + [x + 2 * L for x in lage]
    kn3 = kn * 3
    summe = [0.0]
    for k in kn3:
        summe.append(summe[-1] + k)

    def drehung(weg: float) -> float:
        a = weg + L - halb
        b = weg + L + halb
        i = bisect.bisect_left(pos2, a)
        j = bisect.bisect_right(pos2, b)
        return summe[j] - summe[i]

    schritt = max(0.05, min(0.25, L / 4000))
    n = max(8, int(math.ceil(L / schritt)))
    schritt = L / n
    dreh = [drehung(i * schritt) for i in range(n)]
    grenze = math.radians(knick)
    gut = [d <= grenze + 1e-9 for d in dreh]
    if not any(gut):                                     # keine gute Stelle: die geradesten
        m = min(dreh)
        gut = [d <= m * 1.15 + 1e-6 for d in dreh]
    # Abstand (in Schritten) zur nächsten guten Stelle, vorwärts und rückwärts (im Umlauf)
    unend = 10 ** 9
    vor = [unend] * n
    nach = [unend] * n
    letzte = None
    for durchgang in range(2):
        for i in range(n):
            if gut[i]:
                letzte = i + durchgang * n
            if letzte is not None:
                vor[i] = min(vor[i], i + durchgang * n - letzte)
    letzte = None
    for durchgang in range(2):
        for i in range(n - 1, -1, -1):
            if gut[i]:
                letzte = i - durchgang * n
            if letzte is not None:
                nach[i] = min(nach[i], letzte - (i - durchgang * n))
    max_schub = SCHUB * D / schritt

    def wohin(i: int):
        """(Kosten in Schritten, Ziel-Index) für die gleichmässige Lage i."""
        i %= n
        a, b = vor[i], nach[i]
        if min(a, b) > max_schub:
            return 3 * max_schub + 100 * dreh[i], i
        return (a, i - a) if a <= b else (b, i + b)

    # Phase: in Schritten über einen Abstand D; bei Gleichstand die alte Lage (Mitte der Abschnitte)
    m_phase = max(1, int(round(D / schritt)))
    stufen = min(m_phase, 400)
    best = None
    for q in range(stufen):
        phase = (0.5 * D + q * D / stufen) % D
        kosten = 0.0
        for k in range(anzahl):
            kosten += wohin(int(round((phase + k * D) / schritt)))[0]
        kosten += 1e-6 * min(q, stufen - q)               # Gleichstand: nah an der alten Lage
        if best is None or kosten < best[0] - 1e-12:
            best = (kosten, phase)
    phase = best[1]
    punkte = []
    for k in range(anzahl):
        _c, ziel = wohin(int(round((phase + k * D) / schritt)))
        punkte.append(_punkt_bei(zu, s, (ziel % n) * schritt))
    return punkte


def gleichmaessig_wie_bisher(ring: Sequence[Punkt], anzahl: int, breite: float) -> List[Punkt]:
    """Die Stegmitten, wie sie die Kontur bisher ohne Marken setzte (Mitte jedes Abschnitts ab dem
    Anfang des Rings) – für den Vergleich und alte Zeichnungen."""
    ring = [(float(p[0]), float(p[1])) for p in ring]
    zu, s = _laengen(ring)
    L = s[-1]
    if anzahl <= 0 or breite <= 0 or L <= anzahl * breite * 2:
        return []
    D = L / anzahl
    return [_punkt_bei(zu, s, D * (k + 0.5)) for k in range(anzahl)]


def vorschlag(erg: Dict, bid: str, anzahl: int, breite: float) -> List[Dict]:
    """Marken für eine Kontur-Bearbeitung aus dem Ergebnis von engine.rechnen («steg_ringe»):
    [{"x", "y", "seite"}] in den Koordinaten der Seite (wie auftrag["stege"])."""
    aus = []
    for e in (erg.get("steg_ringe") or {}).get(bid) or []:
        for ring in e["ringe"]:
            for x, y in verteilen(ring, anzahl, breite, float(e.get("d") or 0)):
                aus.append({"x": round(x, 4), "y": round(y, 4), "seite": int(e.get("seite") or 1)})
    return aus
