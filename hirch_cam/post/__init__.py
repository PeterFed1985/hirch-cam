# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Postprozessoren: ein Werkzeugweg-Auftrag (Job) -> Text für die Steuerung.

Die meisten Postprozessoren sind Vorlagen (JSON, siehe vorlage.py):
- mitgeliefert in hirch_cam/post/vorlagen/<schlüssel>.json,
- eigene und geänderte in ~/.config/hirch-cam/postprozessoren/<schlüssel>.json
  (gleicher Schlüssel = ersetzt die mitgelieferte; {"entfernt": true} blendet
  eine mitgelieferte aus).
Verwalten: «Fräsen → Postprozessoren …» oder python3 -m hirch_cam.post.verwalten.

«vcarve_mm» (die bisherige Ausgabe «G-Code (mm)», eine Datei je Werkzeug) ist fest eingebaut,
aber ausgeblendet – ältere Zeichnungen und die Tests rechnen weiter damit.
"""
from __future__ import annotations

import json
import os
from typing import Dict, List

STANDARD = "richauto_a11"
_HIER = os.path.dirname(os.path.abspath(__file__))
VORLAGEN = os.path.join(_HIER, "vorlagen")

# Fest eingebaut (Modul mit write(job)): Schlüssel -> (Anzeigename, Modul, Endung)
EINGEBAUT = {
    "vcarve_mm": ("G-Code (mm), .tap – eine Datei pro Werkzeug (alt)", "hirch_cam.post.tap_mm", ".tap"),
}


def benutzer_ordner() -> str:
    from ..orte import konfig
    return os.path.join(konfig(), "postprozessoren")


def _lesen(pfad: str) -> Dict:
    try:
        with open(pfad, encoding="utf-8") as fh:
            d = json.load(fh)
        return d if isinstance(d, dict) else {}
    except (OSError, ValueError):
        return {}


def _mitgeliefert() -> Dict[str, Dict]:
    aus = {}
    if os.path.isdir(VORLAGEN):
        for f in sorted(os.listdir(VORLAGEN)):
            if f.endswith(".json"):
                aus[f[:-5]] = _lesen(os.path.join(VORLAGEN, f))
    return aus


def _eigene() -> Dict[str, Dict]:
    aus = {}
    o = benutzer_ordner()
    if os.path.isdir(o):
        for f in sorted(os.listdir(o)):
            if f.endswith(".json"):
                aus[f[:-5]] = _lesen(os.path.join(o, f))
    return aus


def alle() -> Dict[str, Dict]:
    """Alle Vorlagen (mitgeliefert, durch eigene ersetzt), mit Zusatzfeldern
    «mitgeliefert», «geaendert», «entfernt»."""
    mit = _mitgeliefert()
    eig = _eigene()
    aus: Dict[str, Dict] = {}
    for k, d in mit.items():
        e = eig.get(k)
        if e and e.get("entfernt"):
            aus[k] = dict(d, mitgeliefert=True, geaendert=False, entfernt=True)
        elif e:
            aus[k] = dict(e, mitgeliefert=True, geaendert=True, entfernt=False)
        else:
            aus[k] = dict(d, mitgeliefert=True, geaendert=False, entfernt=False)
    for k, e in eig.items():
        if k not in mit and not e.get("entfernt"):
            aus[k] = dict(e, mitgeliefert=False, geaendert=False, entfernt=False)
    return aus


def faehigkeiten(d: Dict) -> Dict:
    """Was die Steuerung einer Vorlage kann (für die Oberfläche)."""
    z = d.get("bohrzyklus") or {}
    return {"boegen": bool(d.get("boegen", True)),
            "bohrzyklen": isinstance(z, dict) and bool(z.get("entspanen") or z.get("einfach")),
            "eine_datei": bool(d.get("werkzeugwechsel", True)),
            "endung": d.get("endung") or ".tap",
            # an der Maschine geprüft (Peter 06.11.): nur mitgeliefert und unverändert – eine eigene oder geänderte
            # Vorlage gilt immer als ungeprüft
            "geprueft": bool(d.get("an_maschine_geprueft")) and bool(d.get("mitgeliefert")) and not d.get("geaendert"),
            "eigen": not d.get("mitgeliefert") or bool(d.get("geaendert"))}


def liste(auch: str = "") -> List[List]:
    """[[Schlüssel, Name, Fähigkeiten]] der wählbaren Postprozessoren; «auch» (der in
    der Zeichnung gewählte) erscheint immer, auch wenn er ausgeblendet ist."""
    aus = [[k, d.get("name") or k, faehigkeiten(d)] for k, d in alle().items() if not d.get("entfernt")]
    aus.sort(key=lambda e: (e[0] != STANDARD, str(e[1]).lower()))     # die Vorgabe zuerst
    if auch and auch not in [a[0] for a in aus]:
        if auch in EINGEBAUT:
            aus.append([auch, EINGEBAUT[auch][0], {"boegen": True, "bohrzyklen": False, "eine_datei": False,
                                                   "endung": EINGEBAUT[auch][2]}])
        elif auch in alle():
            aus.append([auch, (alle()[auch].get("name") or auch) + " (entfernt)", faehigkeiten(alle()[auch])])
    if not aus:                                    # nie ganz ohne Ausgabe
        d = _mitgeliefert().get(STANDARD, {})
        aus.append([STANDARD, d.get("name", STANDARD), faehigkeiten(d)])
    return aus


class _Modul:
    """Fest eingebauter Postprozessor im selben Gewand wie eine Vorlage."""

    def __init__(self, schluessel):
        from importlib import import_module
        self.schluessel = schluessel
        self.name, mod, self.endung = EINGEBAUT[schluessel]
        self._m = import_module(mod)
        self.eine_datei = bool(getattr(self._m, "EINE_DATEI", False))

    def write(self, job, boegen=False, datei="", werkzeug=None):
        return self._m.write(job, boegen=boegen)

    def write_mehrere(self, jobs, boegen=False, datei="", werkzeuge=None, grenzen=None):
        return self._m.write_mehrere(jobs, boegen=boegen)      # ohne Grenzen: Endprüfung wie bisher grob


def postprozessor(schluessel: str = STANDARD):
    """Objekt mit name, endung, eine_datei, write(job, …), write_mehrere(jobs, …)."""
    if schluessel in EINGEBAUT:
        return _Modul(schluessel)
    from .vorlage import Vorlage
    a = alle()
    if schluessel not in a:
        raise KeyError(f"Unbekannter Postprozessor: {schluessel}")
    return Vorlage(schluessel, a[schluessel])


def eine_datei(auftrag: Dict) -> bool:
    """Alle Werkzeuge in einer Datei (Halt bei T…M6) oder eine Datei je Werkzeug – nach der
    Steuerung, die Maschinen-Einstellung «Werkzeugwechsel» geht vor."""
    w = str((auftrag.get("maschine") or {}).get("werkzeugwechsel") or "")
    if w in ("dateien", "halt"):
        return w == "halt"
    try:
        return bool(postprozessor(auftrag.get("postprozessor") or STANDARD).eine_datei)
    except KeyError:
        return bool(postprozessor(STANDARD).eine_datei)


POSTPROZESSOREN = {e[0]: (e[1], None, None) for e in liste()}
