"""Testsatz «Messwerte» (Fräsbahnen 25.10.2026): je Fräsart eine kleine, selbst gezeichnete Aufgabe mit festen
Werten. Gemessen wird wie in werkzeuge/gcode_vergleich.py: Zeit (Eilgang 3000 mm/min), Leerweg (G0), Fräsweg, wie
oft eingetaucht wird. Die Werte von heute stehen in tests/messwerte.json.

Der Test schlägt fehl, wenn eine Änderung etwas **schlechter** macht (Zeit oder Leerweg > 1 % mehr, öfter
eingetaucht) – oder den Abtrag ändert (Fräsweg ± 2 %, tiefste Stelle). Wird etwas besser, meldet er es und bleibt
grün; die neuen Werte schreibt man bewusst mit

    HIRCH_MESSWERTE_NEU=1 python3 -m pytest tests/test_messwerte.py

und nennt im Eintrag, was sich warum verbessert hat.
"""
import json
import math
import os
import sys

import pytest

from hirch_cam import engine

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "werkzeuge"))
import gcode_vergleich as gv  # noqa: E402

DATEI = os.path.join(os.path.dirname(__file__), "messwerte.json")
NEU = os.environ.get("HIRCH_MESSWERTE_NEU") == "1"

SCHAFT6 = {"typ": "schaft", "durchmesser": 6, "nummer": 1, "vorschub": 1200, "eintauchen": 300, "drehzahl": 18000}
SCHAFT3 = {"typ": "schaft", "durchmesser": 3, "nummer": 2, "vorschub": 900, "eintauchen": 250, "drehzahl": 20000}
V60 = {"typ": "v", "durchmesser": 12, "v_winkel": 60, "nummer": 3, "vorschub": 1000, "eintauchen": 300,
       "drehzahl": 18000}
BOHRER5 = {"typ": "bohrer", "durchmesser": 5, "nummer": 4, "vorschub": 300, "eintauchen": 200, "drehzahl": 6000}
GF = {"typ": "gewinde", "durchmesser": 4.8, "zaehne": 1, "nummer": 5, "vorschub": 600, "eintauchen": 200,
      "drehzahl": 15000}


def _kreis(cx, cy, r, n=72):
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]


def _rechteck(x, y, b, h):
    return [(x, y), (x + b, y), (x + b, y + h), (x, y + h)]


def _zu(*ringe):
    return [{"punkte": r, "geschlossen": True} for r in ringe]


# Buchstabe «E» und ein Bogen als offene Linien (Gravur), drei Kreise und ein Dreieck (V-Carve)
_E = [[(10, 10), (10, 30), (22, 30)], [(10, 20), (20, 20)], [(10, 10), (22, 10)]]
_BOGEN = [(30 + 10 * math.cos(math.pi * i / 24), 20 + 10 * math.sin(math.pi * i / 24)) for i in range(25)]

AUFGABEN = {
    "gravur": ("A", V60, {"art": "gravur", "tiefe": 0.5, "zustellung": 0.5},
               [{"punkte": l, "geschlossen": False} for l in _E + [_BOGEN]]),
    "vcarve": ("A", V60, {"art": "vcarve", "tiefe": 3.0},
               _zu(_kreis(15, 15, 6), _kreis(35, 15, 4), [(50, 8), (66, 8), (58, 24)])),
    "tasche_runden": ("A", SCHAFT6, {"art": "tasche", "tiefe": 6, "zustellung": 2, "bahnabstand": 40},
                      _zu(_rechteck(10, 10, 60, 40), _rechteck(30, 22, 20, 16))),
    "tasche_teilstuecke": ("A", SCHAFT6, {"art": "tasche", "tiefe": 4, "zustellung": 2, "bahnabstand": 40},
                           # Hantel: zwei Kammern, verbunden durch einen Hals
                           _zu([(10, 10), (40, 10), (40, 22), (60, 22), (60, 10), (90, 10), (90, 50), (60, 50),
                                (60, 38), (40, 38), (40, 50), (10, 50)])),
    "tasche_adaptiv": ("A", SCHAFT6, {"art": "tasche", "tiefe": 12, "zustellung": 12, "bahnabstand": 40,
                                      "strategie": "adaptiv", "adaptiv_last": 15},
                       _zu(_rechteck(10, 10, 60, 40))),
    "kontur_stege": ("A", SCHAFT6, {"art": "kontur", "tiefe": 10, "zustellung": 2.5, "seite": "auto",
                                    "stege_modus": "auto", "stege_anzahl": 3, "stege_breite": 6,
                                    "stege_hoehe": 1.5},
                     _zu(_rechteck(10, 10, 80, 40), _kreis(30, 30, 6), _rechteck(55, 22, 20, 16))),
    "kontur_bogen": ("A", SCHAFT6, {"art": "kontur", "tiefe": 6, "zustellung": 2, "seite": "aussen",
                                    "anfahren": "bogen", "stege_modus": "keine"},
                     _zu(_rechteck(10, 10, 40, 30), _rechteck(60, 10, 30, 30))),
    "bohren": ("A", BOHRER5, {"art": "bohren", "tiefe": 8, "zustellung": 2.5},
               _zu(*[_kreis(15 + 15 * i, 15 + 10 * (i % 2), 2.5, 24) for i in range(5)])),
    "bohren_helix": ("A", SCHAFT3, {"art": "bohren", "tiefe": 8, "zustellung": 2, "methode": "helix"},
                     _zu(_kreis(20, 20, 4), _kreis(40, 20, 3.5), _kreis(60, 25, 5))),
    "gewinde": ("A", GF, {"art": "gewinde", "tiefe": 10},
                _zu(_kreis(20, 20, 5.1), _kreis(45, 20, 5.1))),
    "planen": ("A", SCHAFT6, {"art": "planen", "tiefe": 0.5, "zustellung": 0.5, "bahnabstand": 60,
                              "richtung": "x"},
               _zu(_rechteck(0, 0, 80, 50))),
}


def _messen(name, tmp_path):
    slot, w, p, objekte = AUFGABEN[name]
    a = {"werkzeuge": {slot: dict(w)}, "material": "aluminium",
         "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 10},
         "postprozessor": "richauto_a11", "ausgabe": str(tmp_path / (name + ".tap")),
         "material_flaeche": [100, 60],
         "bearbeitungen": [dict(p, id="b", name=name, werkzeug=slot, objekte=objekte)]}
    erg = engine.rechnen(a)
    assert erg["ok"], erg.get("warnungen")
    werte = {"zeit_min": 0.0, "leerweg": 0.0, "fraesweg": 0.0, "eintauchen": 0, "tiefste": 0.0}
    for d in erg["dateien"]:
        with open(d, newline="") as f:
            s = gv.summe(gv.auswerten(f.read())["abschnitte"])
        werte["zeit_min"] += s["zeit_min"]
        werte["leerweg"] += s["leerweg"]
        werte["fraesweg"] += s["fraesweg"]
        werte["eintauchen"] += s["eintauchen"] + s.get("zustellen", 0)
        werte["tiefste"] = min(werte["tiefste"], s["tiefste"])
    return {k: (round(v, 3) if isinstance(v, float) else v) for k, v in werte.items()}


def _erwartet():
    try:
        with open(DATEI, encoding="utf-8") as f:
            return json.load(f)
    except FileNotFoundError:
        return {}


@pytest.mark.parametrize("name", sorted(AUFGABEN))
def test_messwerte(name, tmp_path):
    jetzt = _messen(name, tmp_path)
    alt = _erwartet()
    if NEU or name not in alt:
        alt[name] = jetzt
        with open(DATEI, "w", encoding="utf-8") as f:
            json.dump(alt, f, ensure_ascii=False, indent=1, sort_keys=True)
            f.write("\n")
        if not NEU:
            pytest.fail(f"{name}: keine Messwerte – neu geschrieben, bitte prüfen: {jetzt}")
        return
    soll = alt[name]
    fehler = []
    if jetzt["zeit_min"] > soll["zeit_min"] * 1.01 + 0.005:
        fehler.append(f"Zeit {jetzt['zeit_min']} statt {soll['zeit_min']} min")
    if jetzt["leerweg"] > soll["leerweg"] * 1.01 + 1:
        fehler.append(f"Leerweg {jetzt['leerweg']} statt {soll['leerweg']} mm")
    if jetzt["eintauchen"] > soll["eintauchen"]:
        fehler.append(f"{jetzt['eintauchen']}× eingetaucht statt {soll['eintauchen']}×")
    if abs(jetzt["fraesweg"] - soll["fraesweg"]) > 0.02 * soll["fraesweg"] + 1:
        fehler.append(f"Fräsweg {jetzt['fraesweg']} statt {soll['fraesweg']} mm (Abtrag geändert?)")
    if abs(jetzt["tiefste"] - soll["tiefste"]) > 1e-3:
        fehler.append(f"tiefste Stelle {jetzt['tiefste']} statt {soll['tiefste']}")
    assert not fehler, f"{name}: " + "; ".join(fehler)
    besser = [k for k in ("zeit_min", "leerweg", "eintauchen") if jetzt[k] < soll[k] * 0.99]
    if besser:
        print(f"{name}: besser ({', '.join(f'{k} {soll[k]} → {jetzt[k]}' for k in besser)}) – "
              "HIRCH_MESSWERTE_NEU=1 schreibt die neuen Werte")
