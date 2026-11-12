# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Planfräsen (wie FreeCAD «MillFace», Muster Zickzack): die ganze
Materialoberfläche um eine Tiefe abfräsen – z. B. eine Alu-Platte, die nicht
ganz eben ist, oder ein Holzrohling auf Mass.

Der Fräser fährt in parallelen Bahnen über das Material und auf beiden
Seiten über den Rand hinaus (Überstand, Standard: Fräserradius + 1 mm), so
dass er immer neben dem Material eintaucht und die Kanten sauber werden.
Die erste und letzte Bahn liegen so, dass der Fräser die Ränder überdeckt.
"""
from __future__ import annotations

import math
from typing import List, Optional, Tuple

from .bahn import Bahnbauer, zustellungen


def bahnen(laenge_quer: float, durchmesser: float, bahnabstand_prozent: float) -> List[float]:
    """Lage der Bahnen quer zur Fahrrichtung (0 … laenge_quer)."""
    r = durchmesser / 2
    s = durchmesser * max(5.0, min(95.0, bahnabstand_prozent)) / 100.0
    erst = r - 0.1 * durchmesser
    letzt = laenge_quer - r + 0.1 * durchmesser
    if letzt <= erst:
        return [laenge_quer / 2]
    n = max(2, math.ceil((letzt - erst) / s - 1e-9) + 1)
    return [erst + (letzt - erst) * k / (n - 1) for k in range(n)]


def planen(
    rechteck: Tuple[float, float, float, float],
    durchmesser: float,
    tiefe: float,
    zustellung: float,
    vorschub: float,
    eintauchen: float,
    bahnabstand_prozent: float = 60.0,
    richtung: str = "x",
    ueberstand: Optional[float] = None,
    freifahrhoehe: float = 3.0,
    startabstand: float = 0.5,
    start=(0.0, 0.0, 20.0),
    name: str = "Planfräsen",
    kreis: Optional[Tuple[float, float, float]] = None,
):
    """rechteck: (x0, y0, x1, y1) der Materialoberfläche. Liefert (Werkzeugweg, Warnungen).
    kreis: (cx, cy, r) bei rundem Material – jede Bahn reicht nur über die Sehne des Kreises
    (plus Überstand), zwischen den Bahnen geht es in der Luft neben dem Kreis zurück."""
    x0, y0, x1, y1 = rechteck
    r = durchmesser / 2
    ue = (r + 1.0) if ueberstand is None else max(0.0, float(ueberstand))
    warnungen: List[str] = []
    if tiefe <= 0:
        return Bahnbauer(vorschub, eintauchen, freifahrhoehe, startabstand, start).weg(name), warnungen
    entlang_x = richtung != "y"
    if entlang_x:
        a0, a1, q0, q1 = x0 - ue, x1 + ue, y0, y1
    else:
        a0, a1, q0, q1 = y0 - ue, y1 + ue, x0, x1
    quer = [q0 + q for q in bahnen(q1 - q0, durchmesser, bahnabstand_prozent)]

    def p(a, q):
        return (a, q) if entlang_x else (q, a)

    b = Bahnbauer(vorschub, eintauchen, freifahrhoehe, startabstand, start)
    if kreis is not None:
        cx, cy, R = kreis
        mq = cy if entlang_x else cx
        ma = cx if entlang_x else cy
        reihen = []
        for q in quer:
            # halbe Sehne dort, wo der Fräser den Kreis noch berührt (Bahn um r breiter)
            dq = max(0.0, abs(q - mq) - r)
            if dq < R:
                reihen.append((q, math.sqrt(R * R - dq * dq) + ue))
        for z in zustellungen(tiefe, zustellung):
            hin = True
            for i, (q, h) in enumerate(reihen):
                # Ende so weit draussen, dass die Querfahrt zur nächsten Bahn den Kreis nicht schneidet
                h_ende = max(h, reihen[i + 1][1]) if i + 1 < len(reihen) else h
                if i == 0:
                    b.anfahren(*p(ma - h if hin else ma + h, q))
                    b.eintauchen(z, None)         # neben dem Kreis: Luft
                else:
                    b.fraesen(*p(ma - h_vor if hin else ma + h_vor, q))   # Querfahrt neben dem Kreis
                b.fraesen(*p(ma + h_ende if hin else ma - h_ende, q))
                h_vor = h_ende
                hin = not hin
            b.abheben()
        if ue < r:
            warnungen.append("Überstand kleiner als der Fräserradius – an den Rändern bleibt ein Rest stehen.")
        return b.weg(name), warnungen
    for z in zustellungen(tiefe, zustellung):
        b.anfahren(*p(a0, quer[0]))
        b.eintauchen(z, None)                 # neben dem Material: Luft
        hin = True
        for i, q in enumerate(quer):
            ende = a1 if hin else a0
            if i:
                b.fraesen(*p(a0 if hin else a1, q))   # Querfahrt ausserhalb des Materials
            b.fraesen(*p(ende, q))
            hin = not hin
        b.abheben()
    if ue < r:
        warnungen.append("Überstand kleiner als der Fräserradius – an den Rändern bleibt ein Rest stehen.")
    return b.weg(name), warnungen
