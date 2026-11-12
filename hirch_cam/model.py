# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Datenmodell für einen Fräsauftrag.

Ein Auftrag (Job) besteht aus einem Werkzeug und mehreren Werkzeugwegen
(Toolpaths). Jeder Werkzeugweg ist eine Folge von Bewegungen mit
absoluten Koordinaten in mm. Wie die Bewegungen als G-Code geschrieben
werden (welche Wörter weggelassen werden usw.), entscheidet allein der
Postprozessor.

Bohrungen stehen zweimal darin: als gewöhnliche Bewegungen (die versteht
jede Steuerung, Vorschau und Simulation rechnen damit) und zusätzlich als
Bohrzyklus (Toolpath.zyklen: welcher Abschnitt der Bewegungen eine Bohrung
ist). Ein Postprozessor, dessen Steuerung Bohrzyklen kennt (G81/G83), schreibt
dafür eine Zeile; die anderen schreiben die Bewegungen.
"""
from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class Move:
    rapid: bool          # True = Eilgang (G0), False = Vorschub (G1)
    x: float
    y: float
    z: float
    feed: float | None = None   # mm/min, nur bei Vorschub


@dataclass
class Bohrzyklus:
    """Die Bewegungen moves[start:ende] eines Werkzeugwegs sind eine Bohrung:
    von der Rückzugsebene (erste Bewegung, Eilgang) in Stufen von «zustellung» mm
    bis zur tiefsten Stelle, dazwischen zum Entspanen auf die Rückzugsebene,
    zum Schluss im Eilgang zurück auf die Höhe davor (letzte Bewegung).
    Lage, Tiefe und Höhen liest der Postprozessor aus den Bewegungen selbst –
    so stimmt der Zyklus auch nach dem Verschieben auf einen anderen Nullpunkt."""
    start: int
    ende: int
    zustellung: float = 0.0          # 0 = in einem Zug (G81)
    verweilen: float = 0.0           # Sekunden unten (G82); 0 = nicht


@dataclass
class Toolpath:
    name: str = ""
    moves: list[Move] = field(default_factory=list)
    senkrecht: int = 0       # senkrecht ins Material getaucht (für die Prüfung)
    zyklen: list[Bohrzyklus] = field(default_factory=list)


@dataclass
class Job:
    tool_number: int = 1
    spindle_rpm: int = 12000
    safe_z: float = 20.0                 # Sicherheitshöhe am Anfang/Ende
    home: tuple[float, float] = (0.0, 0.0)
    toolpaths: list[Toolpath] = field(default_factory=list)
