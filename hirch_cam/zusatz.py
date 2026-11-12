# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Zusätze aus dem Einstellungsordner laden (<Einstellungen>/zusatz/hirch_zusatz/).

Ein Zusatz ist ein Python-Paket «hirch_zusatz», das jemand selbst in seinen Einstellungsordner legt. Er bleibt bei
Updates erhalten. Hirch CAM fragt nur zwei Stellen an:
- «hirch_zusatz.formate»: weitere Dateiformate für Datei › Öffnen/Importieren (Endungen in «formate.txt»,
  Aufruf main([pfad]) – schreibt JSON wie beim Umwandeln einer Zeichnung);
- «hirch_zusatz.fremdschriften»: LESER = {".endung": funktion(pfad, name)} für «Schrift importieren».
Ohne Zusatz gibt es beides nicht. HIRCH_ZUSATZ_ORDNER setzt einen anderen Ordner (Tests).
"""
import importlib
import os
import sys


def ordner() -> str:
    eigen = os.environ.get("HIRCH_ZUSATZ_ORDNER")
    if eigen:
        return eigen
    from . import orte
    return os.path.join(orte.konfig(), "zusatz")


def modul(name: str):
    """hirch_zusatz.<name> laden, None wenn es keinen Zusatz gibt."""
    o = ordner()
    if not os.path.isfile(os.path.join(o, "hirch_zusatz", "__init__.py")):
        return None
    if o not in sys.path:
        sys.path.append(o)
    try:
        return importlib.import_module("hirch_zusatz." + name)
    except ImportError:
        return None
