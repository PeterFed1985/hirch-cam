# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Bohren (Violett): an der Mitte jeder violetten Form bohren.

Gebohrt wird in Stufen (Spanbruch): je Zustellung eintauchen, auf den
Startabstand zurückziehen, im Eilgang bis 0.2 mm über die letzte Tiefe
zurück und weiter.

Helix (wie FreeCAD «Helix»): Formen, die grösser sind als der Fräser,
werden schraubenförmig auf ihren Durchmesser gefräst – je Umdrehung um die
Zustellung tiefer, unten eine volle Runde für einen sauberen Grund. Ist das
Loch mehr als doppelt so gross wie der Fräser, folgen von innen nach aussen
weitere Schrauben (Bahnabstand 40 % vom Ø), damit kein Zapfen stehen bleibt.
Methode «auto»: Helix für grössere Formen, sonst bohren.

Jede Bohrung wird zusätzlich als Bohrzyklus vermerkt (model.Bohrzyklus): Kennt
die Steuerung G81/G83, schreibt der Postprozessor dafür eine Zeile.

Bohrer (Werkzeugtyp «bohrer»): nur bohren, nie Helix; die Spitze (Kegel mit dem
Spitzenwinkel) kommt auf Wunsch zur Tiefe dazu, damit der volle Durchmesser
die Tiefe erreicht (Durchgangslöcher, Passlöcher).
"""
from __future__ import annotations

import math
from typing import List, Sequence, Tuple

from ..model import Bohrzyklus
from .bahn import kurze_reihenfolge, Bahnbauer, Punkt, zustellungen

RUECKZUG_ABSTAND = 0.2


def bohren(
    linien: Sequence[Tuple[Sequence[Punkt], bool]],
    durchmesser: float,
    tiefe: float,
    zustellung: float,
    eintauchen: float,
    freifahrhoehe: float = 3.0,
    startabstand: float = 0.5,
    start=(0.0, 0.0, 20.0),
    name: str = "Bohren",
    methode: str = "bohren",
    vorschub: float | None = None,
    gleichlauf: bool = True,
    toleranz: float = 0.01,
    aufmass: float = 0.0,
    bohrer: bool = False,
):
    """methode: «bohren» (Mitte), «helix» (auf Durchmesser fräsen) oder «auto».
    zustellung gilt bei der Helix je Umdrehung; vorschub ist der Fräsvorschub
    der Helix (Standard: Eintauchvorschub). bohrer: das Werkzeug ist ein Bohrer –
    nur in der Mitte bohren; Formen, die nicht zum Bohrer passen, werden gemeldet."""
    warnungen: List[str] = []
    if bohrer:
        methode = "bohren"
    zu_klein = 0
    punkte = []          # (x, y, Lochdurchmesser oder 0 = bohren)
    zu_gross = 0
    unrund = 0
    for pts, _g in linien:
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        mx, my = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
        bx, by = max(xs) - min(xs), max(ys) - min(ys)
        groesse = max(bx, by)
        loch = 0.0
        if bohrer and groesse < durchmesser - 0.05 and groesse > 0.05:
            zu_klein += 1
        if groesse > durchmesser + 0.5:
            if methode in ("helix", "auto"):
                loch = (bx + by) / 2 - 2 * aufmass
                if abs(bx - by) > 0.05 * groesse:
                    unrund += 1
            else:
                zu_gross += 1
        elif methode == "helix" and groesse > durchmesser + 0.05:
            loch = (bx + by) / 2 - 2 * aufmass
        if not any(math.dist((mx, my), p[:2]) < 1e-3 for p in punkte):
            punkte.append((mx, my, loch))
    if zu_gross and bohrer:
        warnungen.append(
            f"{zu_gross} Formen grösser als der Bohrer (Ø {durchmesser:g} mm) – nur in der Mitte gebohrt. "
            "Für grössere Löcher einen grösseren Bohrer oder einen Fräser mit «Helix» wählen.")
    elif zu_gross:
        warnungen.append(
            f"{zu_gross} Formen grösser als der Fräser (Ø {durchmesser:g} mm) – nur in der Mitte "
            "gebohrt. Für das ganze Loch «Helix» wählen.")
    if zu_klein:
        warnungen.append(f"{zu_klein} Formen kleiner als der Bohrer (Ø {durchmesser:g} mm) – "
                         "das Loch wird grösser als gezeichnet.")
    if unrund:
        warnungen.append(f"{unrund} Formen sind nicht rund – Helix auf dem mittleren Durchmesser. "
                         "Eckige Löcher besser als Tasche zeichnen.")

    b = Bahnbauer(vorschub or eintauchen, eintauchen, freifahrhoehe, startabstand, start)
    tiefen = zustellungen(tiefe, zustellung)
    zyklen: List[Bohrzyklus] = []
    # kurze Leerwege: Reihenfolge der Löcher vorab (nächster Nachbar + 2-opt)
    folge = kurze_reihenfolge(len(punkte), lambda i, j: math.dist(punkte[i][:2], punkte[j][:2]),
                              lambda i: math.dist(b.pos[:2], punkte[i][:2]))
    for k in folge:
        x, y, loch = punkte[k]
        if loch > durchmesser + 1e-6:
            _helix(b, x, y, loch, durchmesser, tiefe, zustellung, gleichlauf, toleranz)
            continue
        # über dem Loch auf Freifahrhöhe – ab hier ist es ein Bohrzyklus
        b.abheben()
        b._add(True, x, y, b.cz)               # wie anfahren(): auf Freifahrhöhe hinüber
        i0 = len(b.moves)
        b.eilgang_z(startabstand)
        vorher = 0.0
        for i, z in enumerate(tiefen):
            if i:
                b.eilgang_z(vorher + RUECKZUG_ABSTAND)
            b.eintauchen(z)
            vorher = z
            if i < len(tiefen) - 1:
                b.eilgang_z(startabstand)
        b.abheben()
        zyklen.append(Bohrzyklus(i0, len(b.moves), tiefe / len(tiefen) if len(tiefen) > 1 else 0.0))
    weg = b.weg(name)
    weg.zyklen = zyklen
    return weg, warnungen


def _kreis_punkte(radius: float, toleranz: float) -> int:
    """Teilung je Umdrehung, damit die Sehnen höchstens «toleranz» abweichen."""
    if radius <= toleranz:
        return 8
    n = math.ceil(math.pi / math.acos(max(-1.0, 1.0 - toleranz / radius)))
    return max(16, min(720, n))


def _helix(b: Bahnbauer, x, y, loch, d, tiefe, steigung, gleichlauf, toleranz):
    """Loch «loch» (Ø) schraubenförmig fräsen; innen anfangen, wenn nötig
    mehrere Schrauben nach aussen."""
    r = d / 2
    aussen = loch / 2 - r
    if aussen <= 1e-6:
        return
    schritt = 0.4 * d
    radien = [aussen]
    while radien[-1] - schritt > r * 0.2:      # innen bliebe sonst ein Zapfen stehen
        radien.append(radien[-1] - schritt)
    radien.reverse()
    richtung = 1 if gleichlauf else -1        # innen: Gleichlauf = gegen den Uhrzeigersinn
    steigung = max(0.05, float(steigung))
    for R in radien:
        n = _kreis_punkte(R, toleranz)
        # nie schneller nach unten als mit dem Eintauchvorschub (Peter 25.10.: kleine Löcher wurden fast
        # senkrecht – Loch 6,2 mit Ø 6 sank mit ~2600 mm/min): Steigung je Umdrehung höchstens
        # Umfang × Eintauch- / Fräsvorschub. Die Zeit hängt dann nur von Tiefe und Vorschüben ab.
        s_r = steigung
        if b.vorschub > 0 and b.eintauchvorschub > 0:
            s_r = max(0.01, min(steigung, 2 * math.pi * R * b.eintauchvorschub / b.vorschub))
        b.anfahren(x + R, y)
        b.eintauchen(0.0, None)                  # bis zur Oberfläche: Luft
        umdrehungen = max(1, math.ceil(tiefe / s_r - 1e-9))
        schritte = umdrehungen * n
        for i in range(1, schritte + 1):
            w = richtung * 2 * math.pi * i / n
            b.fraesen(x + R * math.cos(w), y + R * math.sin(w), -tiefe * i / schritte)
        for i in range(1, n + 1):                # unten eine volle Runde
            w = richtung * 2 * math.pi * i / n
            b.fraesen(x + R * math.cos(w), y + R * math.sin(w), -tiefe)
        # zur Mitte weg von der Wand, dann hoch
        b.fraesen(x + R - min(R, 0.5), y, -tiefe)
        b.abheben()
