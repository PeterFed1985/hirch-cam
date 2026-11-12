# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Fräsart über die Linienfarbe.

Die Grundfarben entsprechen der Palette inkscape/profile/palettes/hirch-cam.gpl.
Entscheidend ist der Farbton: dunklere und hellere Töne derselben Farbe
gehören zur selben Fräsart. So kann eine Fräsart mehrere Bearbeitungen haben
(z. B. zwei Taschen mit verschiedener Tiefe) – jede bekommt einen eigenen Ton
(siehe schattierung()). Schwarz, Weiss und blasse Farben werden nicht gefräst,
mittleres Grau heisst ausdrücklich «nicht fräsen».
"""
from __future__ import annotations

import colorsys
from typing import Optional, Tuple

FRAESARTEN = {
    "vcarve": ((224, 0, 0), "V-Carve", "Rot"),
    "gravur": ((255, 140, 0), "Gravur", "Orange"),
    "tasche": ((0, 160, 0), "Tasche", "Grün"),
    "kontur": ((0, 80, 255), "Kontur", "Blau"),
    "bohren": ((128, 0, 192), "Bohren", "Violett"),
    "gewinde": ((0, 170, 190), "Gewinde", "Türkis"),
    "aus": ((128, 128, 128), "Nicht fräsen", "Grau"),
}

# Bearbeitungen ohne gezeichnete Objekte (keine Farbe in der Zeichnung)
OHNE_OBJEKTE = {"planen": ("Planfräsen", "ganzes Material"),
                # 3D-Fräsung: fräst die 3D-Modelle der Zeichnung (ops/relief3d.py)
                "relief_schruppen": ("3D-Schruppen", "3D-Modell"),
                "relief_schlichten": ("3D-Schlichten", "3D-Modell"),
                "boeden_waende": ("Böden und Wände", "3D-Modell")}
# Peter 30.10.: 3D-Bahnen orange und blau – grau verschwanden sie auf dem Graustufenbild des Modells.
# Die Farbe zeigt nur an (3D-Bearbeitungen bekommen Linien über «hirch:auch», nie über die Farbe).
_OHNE_FARBE = {"planen": (154, 123, 79),      # nur Anzeige im Panel
               "relief_schruppen": (230, 97, 0), "relief_schlichten": (28, 113, 216),
               "boeden_waende": (46, 194, 126)}
# bis 30.10.: Grautöne – gespeicherte Bearbeitungen mit diesen Tönen bekommen die neuen (alte_relief_farbe)
_ALT_GRAU = {"relief_schruppen": (94, 92, 100), "relief_schlichten": (154, 150, 160)}


def art_name(art: str) -> str:
    """Name der Fräsart in der eingestellten Sprache (Anzeige, nie gespeichert)."""
    from .sprache import tr
    return tr(FRAESARTEN[art][1] if art in FRAESARTEN else OHNE_OBJEKTE[art][0])


def art_zusatz(art: str) -> str:
    from .sprache import tr
    return tr(FRAESARTEN[art][2] if art in FRAESARTEN else OHNE_OBJEKTE[art][1])


# Farbton (Grad) und erlaubte Abweichung je Fräsart
_TOENE = {"vcarve": (0.0, 14.0), "gravur": (33.0, 14.0), "tasche": (120.0, 35.0),
          "kontur": (221.0, 25.0), "bohren": (280.0, 25.0), "gewinde": (186.0, 18.0)}

# Abstufungen für weitere Bearbeitungen derselben Fräsart:
# (Helligkeit, Anteil Weiss)
_STUFEN = [(1.0, 0.0), (0.62, 0.0), (1.0, 0.42), (0.42, 0.0), (0.8, 0.25), (1.0, 0.58)]


def _hsv(rgb):
    r, g, b = (c / 255.0 for c in rgb)
    h, s, v = colorsys.rgb_to_hsv(r, g, b)
    return h * 360.0, s, v


def fraesart(rgb: Tuple[int, int, int]) -> Optional[str]:
    """Schlüssel der Fräsart oder None bei unbekannter Farbe.
    Schwarz gilt als unbekannt, damit nichts versehentlich gefräst wird."""
    h, s, v = _hsv(rgb)
    if v < 0.18:
        return None                                  # schwarz / fast schwarz
    if s < 0.22:
        return "aus" if 0.3 <= v <= 0.8 else None    # Grau = nicht fräsen; Weiss = unbekannt
    for art, (ton, breite) in _TOENE.items():
        d = abs((h - ton + 180.0) % 360.0 - 180.0)
        if d <= breite:
            return art
    return None


def hex_farbe(rgb) -> str:
    return "#%02x%02x%02x" % tuple(int(round(c)) for c in rgb)


def rgb_von(hex_text: str) -> Optional[Tuple[int, int, int]]:
    t = (hex_text or "").strip().lstrip("#")
    if len(t) == 3:
        t = "".join(c * 2 for c in t)
    try:
        return (int(t[0:2], 16), int(t[2:4], 16), int(t[4:6], 16))
    except (ValueError, IndexError):
        return None


def schattierung(art: str, n: int) -> str:
    """Farbe der n-ten Bearbeitung einer Fräsart (0 = Grundfarbe)."""
    basis = FRAESARTEN[art][0] if art in FRAESARTEN else _OHNE_FARBE[art]
    hell, weiss = _STUFEN[n % len(_STUFEN)]
    rgb = [c * hell for c in basis]
    rgb = [c + (255 - c) * weiss for c in rgb]
    return hex_farbe(rgb)


def abstand(a, b) -> float:
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


def alte_relief_farbe(art: str, farbe: str) -> Optional[str]:
    """Neuer Ton für eine 3D-Bearbeitung, die noch den alten Grauton von vor dem 30.10. trägt (sonst None)."""
    basis = _ALT_GRAU.get(art)
    if not basis or not farbe:
        return None
    for n, (hell, weiss) in enumerate(_STUFEN):
        rgb = [c * hell for c in basis]
        rgb = [c + (255 - c) * weiss for c in rgb]
        if hex_farbe(rgb).lower() == farbe.strip().lower():
            return schattierung(art, n)
    return None
