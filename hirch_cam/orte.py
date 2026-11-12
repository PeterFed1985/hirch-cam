# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Wo Hirch CAM seine Einstellungen ablegt.

Linux: ~/.config/hirch-cam (bzw. $XDG_CONFIG_HOME/hirch-cam)
Windows: %APPDATA%\\hirch-cam

Umzug Stufe 2 (Peter 04.11.2026): vorher hiess der Ordner «hirch-cnc». Beim ersten Start der neuen Version wird er
einmal nach «hirch-cam» kopiert (umzug()); der alte bleibt als Sicherung stehen, ein bestehender neuer wird nie
überschrieben.
"""
from __future__ import annotations

import os
import shutil

ALT = "hirch-cnc"
_umgezogen = False


def _basen():
    """Grundordner, in denen Hirch CAM einen eigenen Ordner hat (Python: konfig; Windows-Programm: auch LOCALAPPDATA)."""
    if os.name == "nt":
        aus = [os.environ.get("APPDATA") or os.path.join(os.path.expanduser("~"), "AppData", "Roaming")]
        if os.environ.get("LOCALAPPDATA"):
            aus.append(os.environ["LOCALAPPDATA"])
        return aus
    return [os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")]


def umzug() -> list:
    """Alten Einstellungsordner einmal kopieren (Maschine, Werkzeuge, Postprozessoren, Vorlagen, Profil …).
    Nicht mitkopiert: «kern» (Rechenkern-Updates des alten Pakets passen nicht mehr). Gibt die kopierten Ziele."""
    global _umgezogen
    if _umgezogen:
        return []
    _umgezogen = True
    aus = []
    for basis in _basen():
        alt, neu = os.path.join(basis, ALT), os.path.join(basis, "hirch-cam")
        if os.path.isdir(alt) and not os.path.lexists(neu):
            try:
                shutil.copytree(alt, neu, symlinks=True, ignore=shutil.ignore_patterns("kern", "kern.alt"))
                aus.append(neu)
            except OSError:
                pass
    return aus


def konfig() -> str:
    umzug()
    if os.name == "nt":
        basis = os.environ.get("APPDATA") or os.path.join(os.path.expanduser("~"), "AppData", "Roaming")
    else:
        basis = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")
    return os.path.join(basis, "hirch-cam")
