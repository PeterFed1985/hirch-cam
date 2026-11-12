# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Vorgaben für neue Bearbeitungen – «So fräse ich» (Einstellungen › Fräsen, Peter 07.11.2026).

Je Wert eine Stelle: Eine Vorgabe gilt für alle Fräsarten, bei denen sie vorkommt (z. B. die Fräsrichtung für Tasche,
Kontur und Bohren). Sie wirkt nur auf **neue** Bearbeitungen; bestehende bleiben, wie sie sind.

Gespeichert wird in der Liste der Maschinen (`maschinen.json`, Schlüssel «vorgaben»; gilt für alle Maschinen), nur was
vom Standard abweicht.
Was zur Zeichnung, zum Modell oder zum Fräser gehört (Tiefe, Werkzeug, Zustellung, Lage des Modells …), ist bewusst
keine Vorgabe (Listen von fraesbahnen und 3d, 07.11.).
"""
from __future__ import annotations

import copy
import time
from typing import Any, Dict, List, Optional

from .einstellungen import STANDARD_BEARBEITUNG

# schluessel: (gruppe, felder je Fräsart, art, Wahl/Grenzen, Standard)
#   art: "wahl" (Werte), "bool", "zahl" (min, max), "zahl_leer" (min, max; None = automatisch / Wert am Fräser)
LISTE: List[Dict[str, Any]] = [
    # ---- 2D, vorne
    {"key": "eintauchen_art", "gruppe": "2d", "felder": {"tasche": "eintauchen_art", "kontur": "eintauchen_art"},
     "art": "wahl", "werte": ["senkrecht", "rampe"], "standard": "rampe"},
    {"key": "rampe_winkel", "gruppe": "2d", "felder": {"tasche": "rampe_winkel", "kontur": "rampe_winkel"},
     "art": "zahl_leer", "grenzen": (0.5, 45.0), "standard": None},
    {"key": "gleichlauf", "gruppe": "2d", "felder": {"tasche": "gleichlauf", "kontur": "gleichlauf", "bohren": "gleichlauf"},
     "art": "bool", "standard": True},
    {"key": "taschen_strategie", "gruppe": "2d", "felder": {"tasche": "strategie"},
     "art": "wahl", "werte": ["adaptiv", "runden"], "standard": "adaptiv"},
    # fraesbahnen 07.11.: 5–60 %, Vorschlag 40 %
    {"key": "taschen_bahnabstand", "gruppe": "2d", "felder": {"tasche": "bahnabstand"},
     "art": "zahl", "grenzen": (5, 60), "standard": 40},
    {"key": "stege_modus", "gruppe": "2d", "felder": {"kontur": "stege_modus"},
     "art": "wahl", "werte": ["auto", "keine"], "standard": "auto"},
    {"key": "stege_anzahl", "gruppe": "2d", "felder": {"kontur": "stege_anzahl"},
     "art": "zahl", "grenzen": (1, 20), "standard": 4},
    {"key": "stege_breite", "gruppe": "2d", "felder": {"kontur": "stege_breite"},
     "art": "zahl", "grenzen": (1.0, 50.0), "standard": 6.0},
    {"key": "stege_hoehe", "gruppe": "2d", "felder": {"kontur": "stege_hoehe"},
     "art": "zahl", "grenzen": (0.2, 20.0), "standard": 1.5},
    {"key": "schlichten", "gruppe": "2d", "felder": {"tasche": "schlichten", "kontur": "schlichten"},
     "art": "bool", "standard": False},
    {"key": "schlicht_zugabe", "gruppe": "2d", "felder": {"tasche": "schlicht_zugabe", "kontur": "schlicht_zugabe"},
     "art": "zahl", "grenzen": (0.05, 5.0), "standard": 0.3},
    {"key": "schlicht_zustellung", "gruppe": "2d",
     "felder": {"tasche": "schlicht_zustellung", "kontur": "schlicht_zustellung"},
     "art": "zahl_leer", "grenzen": (0.1, 100.0), "standard": None},
    # ---- 2D, Details
    {"key": "anfahren", "gruppe": "2d_details", "felder": {"kontur": "anfahren"},
     "art": "wahl", "werte": ["bogen", ""], "standard": "bogen"},
    {"key": "adaptiv_wechsel", "gruppe": "2d_details", "felder": {"tasche": "adaptiv_wechsel"},
     "art": "bool", "standard": True},
    {"key": "planen_bahnabstand", "gruppe": "2d_details", "felder": {"planen": "bahnabstand"},
     "art": "zahl", "grenzen": (10, 95), "standard": 60},
    {"key": "planen_richtung", "gruppe": "2d_details", "felder": {"planen": "richtung"},
     "art": "wahl", "werte": ["x", "y"], "standard": "x"},
    {"key": "bohren_methode", "gruppe": "2d_details", "felder": {"bohren": "methode"},
     "art": "wahl", "werte": ["auto", "bohren", "helix"], "standard": "auto"},
    {"key": "bohren_spitze_dazu", "gruppe": "2d_details", "felder": {"bohren": "spitze_dazu"},
     "art": "bool", "standard": True},
    {"key": "dogbone", "gruppe": "2d_details", "felder": {"tasche": "dogbone", "kontur": "dogbone"},
     "art": "bool", "standard": False},
    {"key": "v_wand", "gruppe": "2d_details", "felder": {"tasche": "v_wand"},
     "art": "wahl", "werte": ["oben", "unten"], "standard": "oben"},
    # ---- 3D, vorne (Liste von 3d, 07.11.; Peter 18:33 «wie vorgeschlagen»)
    {"key": "schlicht3d_strategie", "gruppe": "3d", "felder": {"relief_schlichten": "strategie"},
     "art": "wahl", "werte": ["parallel", "hoehenlinien", "kombiniert", "ebenen", "kehlen", "konturen"],
     "standard": "parallel"},
    {"key": "schlicht3d_richtung", "gruppe": "3d", "felder": {"relief_schlichten": "richtung"},
     "art": "wahl", "werte": ["x", "y"], "standard": "x"},
    {"key": "schlicht3d_grenzwinkel", "gruppe": "3d", "felder": {"relief_schlichten": "grenzwinkel"},
     "art": "zahl", "grenzen": (5, 85), "standard": 40},
    # 3d 07.11.: 3–40 %, darüber wird es bei der Kugel sichtbar rau
    {"key": "schlicht3d_bahnabstand", "gruppe": "3d", "felder": {"relief_schlichten": "bahnabstand"},
     "art": "zahl", "grenzen": (3, 40), "standard": 10},
    {"key": "schrupp3d_bahnabstand", "gruppe": "3d",
     "felder": {"relief_schruppen": "bahnabstand", "boeden_waende": "bahnabstand"},
     "art": "zahl", "grenzen": (5, 90), "standard": 40},
    {"key": "schrupp3d_aufmass", "gruppe": "3d", "felder": {"relief_schruppen": "aufmass"},
     "art": "zahl", "grenzen": (0.05, 5.0), "standard": 0.5},
    {"key": "schrupp3d_adaptiv", "gruppe": "3d", "felder": {"relief_schruppen": "adaptiv"},
     "art": "bool", "standard": False},
    {"key": "schrupp3d_adaptiv_last", "gruppe": "3d", "felder": {"relief_schruppen": "adaptiv_last"},
     "art": "zahl", "grenzen": (5, 50), "standard": 15},
    {"key": "auslassen_fertig", "gruppe": "3d", "felder": {"relief_schlichten": "auslassen_fertig"},
     "art": "bool", "standard": False},
    {"key": "fuss_umlauf", "gruppe": "3d", "felder": {"relief_schlichten": "fuss_umlauf"},
     "art": "bool", "standard": True},
    {"key": "kehlen_nachfahren", "gruppe": "3d", "felder": {"relief_schlichten": "kehlen_nachfahren"},
     "art": "bool", "standard": True},
    # ---- 3D, Details
    {"key": "schrupp3d_lagen", "gruppe": "3d_details", "felder": {"relief_schruppen": "lagen"},
     "art": "zahl", "grenzen": (0.0, 100.0), "standard": 6.0},
    {"key": "kehlen_winkel", "gruppe": "3d_details", "felder": {"relief_schlichten": "kehlen_winkel"},
     "art": "zahl", "grenzen": (1, 90), "standard": 15},
    {"key": "kehlen_bahnen", "gruppe": "3d_details", "felder": {"relief_schlichten": "kehlen_bahnen"},
     "art": "zahl", "grenzen": (0, 10), "standard": 0},
    {"key": "schlicht3d_genau", "gruppe": "3d_details", "felder": {"relief_schlichten": "genau"},
     "art": "bool", "standard": False},
    {"key": "schlicht3d_abtastung", "gruppe": "3d_details", "felder": {"relief_schlichten": "abtastung"},
     "art": "zahl_leer", "grenzen": (0.02, 2.0), "standard": None},
    {"key": "bw_boeden", "gruppe": "3d_details", "felder": {"boeden_waende": "boeden"},
     "art": "bool", "standard": True},
    {"key": "bw_waende", "gruppe": "3d_details", "felder": {"boeden_waende": "waende"},
     "art": "bool", "standard": True},
    {"key": "bw_stufe", "gruppe": "3d_details", "felder": {"boeden_waende": "stufe"},
     "art": "zahl", "grenzen": (0.5, 100.0), "standard": 10.0},
    {"key": "rest_schwelle", "gruppe": "3d_details",
     "felder": {"relief_schruppen": "rest_schwelle", "relief_schlichten": "rest_schwelle"},
     "art": "zahl", "grenzen": (0.01, 5.0), "standard": 0.1},
]
NACH_KEY = {e["key"]: e for e in LISTE}


def _pruefen_standard():
    """Der Standard jeder Vorgabe ist der Wert aus STANDARD_BEARBEITUNG (wo es ihn gibt) – sonst gäbe es zwei Stellen."""
    for e in LISTE:
        for art, feld in e["felder"].items():
            if feld in STANDARD_BEARBEITUNG.get(art, {}):
                assert STANDARD_BEARBEITUNG[art][feld] == e["standard"], (e["key"], art)


def gueltig(key: str, wert: Any) -> Any:
    """Wert prüfen und in die Grenzen legen. ValueError bei Unbekanntem."""
    e = NACH_KEY.get(key)
    if e is None:
        raise ValueError("Unbekannte Vorgabe: %r" % key)
    art = e["art"]
    if art == "wahl":
        if wert not in e["werte"]:
            raise ValueError("Ungültiger Wert für %s: %r" % (key, wert))
        return wert
    if art == "bool":
        if not isinstance(wert, bool):
            raise ValueError("Ja/Nein erwartet für %s" % key)
        return wert
    if art == "zahl_leer" and wert in (None, ""):
        return None
    try:
        z = float(wert)
    except (TypeError, ValueError):
        raise ValueError("Zahl erwartet für %s" % key)
    lo, hi = e["grenzen"]
    z = min(max(z, lo), hi)
    return int(round(z)) if isinstance(e["standard"], int) and not isinstance(e["standard"], bool) else z


def _index() -> Dict:
    from . import maschinen
    return maschinen.index()


def gespeichert(idx: Optional[Dict] = None) -> Dict[str, Any]:
    """Abweichungen vom Standard, wie sie gespeichert sind (Ungültiges fällt weg)."""
    idx = _index() if idx is None else idx
    aus = {}
    for k, v in (idx.get("vorgaben") or {}).items():
        try:
            aus[k] = gueltig(k, v)
        except ValueError:
            continue
    return aus


def wirksam(idx: Optional[Dict] = None) -> Dict[str, Any]:
    """Jede Vorgabe mit ihrem geltenden Wert."""
    w = {e["key"]: copy.deepcopy(e["standard"]) for e in LISTE}
    w.update(gespeichert(idx))
    return w


def anwenden(b: Dict, art: str, idx: Optional[Dict] = None) -> Dict:
    """Die gespeicherten Vorgaben auf eine neue Bearbeitung der Fräsart art legen."""
    for k, v in gespeichert(idx).items():
        feld = NACH_KEY[k]["felder"].get(art)
        if feld:
            b[feld] = copy.deepcopy(v)
    return b


def _speichern(aenderung) -> Dict[str, Any]:
    from . import maschinen
    idx = maschinen.index()
    v = dict(idx.get("vorgaben") or {})
    aenderung(v)
    idx["vorgaben"] = v
    idx["vorgaben_geaendert"] = round(time.time(), 3)        # Abgleich zweier PCs: die neuere gilt
    maschinen.index_schreiben(idx)
    return wirksam(idx)


def setzen(key: str, wert: Any) -> Dict[str, Any]:
    """Eine Vorgabe setzen; gleich dem Standard → Eintrag weg. Gibt die wirksamen Vorgaben zurück."""
    wert = gueltig(key, wert)

    def a(v):
        if wert == NACH_KEY[key]["standard"]:
            v.pop(key, None)
        else:
            v[key] = wert
    return _speichern(a)


def mehrere_setzen(werte: Dict[str, Any]) -> Dict[str, Any]:
    """Mehrere Vorgaben in einem Schritt. Ungültiges wird übersprungen."""
    def a(v):
        for key, wert in werte.items():
            try:
                wert = gueltig(key, wert)
            except ValueError:
                continue
            if wert == NACH_KEY[key]["standard"]:
                v.pop(key, None)
            else:
                v[key] = wert
    return _speichern(a)


def zuruecksetzen() -> Dict[str, Any]:
    return _speichern(lambda v: v.clear())


def fuer_oberflaeche(idx: Optional[Dict] = None) -> Dict:
    """Für Einstellungen › Fräsen und für «Vorgabe / geändert» in der Bearbeitung: Liste, Werte, Felder je Art."""
    w = wirksam(idx)
    return {"liste": [{"key": e["key"], "gruppe": e["gruppe"], "art": e["art"], "werte": e.get("werte"),
                       "grenzen": list(e["grenzen"]) if "grenzen" in e else None, "standard": e["standard"],
                       "wert": w[e["key"]], "felder": e["felder"]} for e in LISTE]}
