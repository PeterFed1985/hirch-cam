"""Zustellungen mit Art (Peter, 20.10.): letzte kleiner, eigene Liste, ein Durchgang;
Schlichten mit eigener Fräsrichtung; Gewinde nur mit Gewindefräser."""
import copy
import json

import pytest

from hirch_cam import ablauf, engine
from hirch_cam.ops.bahn import Zustellung, zustellung_aus, zustellungen


def test_arten():
    assert zustellungen(1.5, 0.5) == pytest.approx([-0.5, -1.0, -1.5])
    assert zustellungen(1.5, Zustellung(0.6, "kleiner", 0.3)) == pytest.approx([-0.6, -1.2, -1.5])
    assert zustellungen(1.5, Zustellung(0.7, "liste", 0, [0.7, 0.5, 0.3])) == pytest.approx([-0.7, -1.2, -1.5])
    # die letzte wiederholt sich, die Tiefe wird nie überschritten
    assert zustellungen(2.0, Zustellung(0.7, "liste", 0, [0.7, 0.5])) == pytest.approx([-0.7, -1.2, -1.7, -2.0])
    assert zustellungen(1.5, Zustellung(1.5, "eins")) == [-1.5]
    # letzte grösser als die Tiefe: gleichmässig
    assert zustellungen(0.2, Zustellung(0.6, "kleiner", 0.3)) == pytest.approx([-0.2])


def test_zustellung_bleibt_zahl():
    z = zustellung_aus({"zustellung": 0.6, "zustell_modus": "kleiner", "letzte_zustellung": 0.3})
    assert float(z) == pytest.approx(0.6) and min(z, 1.0) == pytest.approx(0.6)
    assert copy.deepcopy(z).modus == "kleiner"
    assert json.dumps({"z": z}) == '{"z": 0.6}'
    assert zustellung_aus({"zustellung": 0.6}) == 0.6                 # ohne Art: unverändert
    assert zustellung_aus({"zustellung": 1, "zustell_modus": "liste"}) == 1   # leere Liste: wie bisher


def test_ablauf_zeigt_stufen():
    z = ablauf.zustellungen("tasche", {"tiefe": 1.5, "zustellung": 0.6, "zustell_modus": "kleiner",
                                       "letzte_zustellung": 0.3})
    assert z["n"] == 3 and z["stufen"] == pytest.approx([0.6, 0.6, 0.3]) and not z["gleich"]


def _auftrag(tmp_path, **b):
    q = [[10, 10], [60, 10], [60, 40], [10, 40]]
    bea = dict({"id": "t", "art": "tasche", "name": "Tasche", "werkzeug": "A", "tiefe": 1.5,
                "zustellung": 0.5, "objekte": [{"punkte": q, "geschlossen": True}]}, **b)
    return {"werkzeuge": {"A": {"typ": "schaft", "durchmesser": 3, "nummer": 1, "vorschub": 1000,
                                "eintauchen": 300},
                          "G": {"typ": "gewinde", "durchmesser": 4, "nummer": 2, "vorschub": 300,
                                "eintauchen": 100}},
            "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 10},
            "postprozessor": "vcarve_mm", "ausgabe": str(tmp_path / "t.tap"), "bearbeitungen": [bea]}


def _ebenen(erg, teil=""):
    from hirch_cam.post import tap_mm
    zs = set()
    for p in erg["programme"]:
        job = tap_mm.read(open(p["datei"], newline="").read())
        for tp in job.toolpaths:
            if teil and teil not in tp.name:
                continue
            for m in tp.moves:
                if not m.rapid and m.z < -1e-6:
                    zs.add(round(m.z, 3))
    return sorted(zs, reverse=True)


def test_tasche_letzte_kleiner(tmp_path):
    erg = engine.rechnen(_auftrag(tmp_path, zustellung=0.6, zustell_modus="kleiner", letzte_zustellung=0.3,
                                  eintauchen_art="senkrecht"))
    assert erg["ok"], erg
    assert {-0.6, -1.2, -1.5} <= set(_ebenen(erg))


def test_schlichten_eigene_richtung(tmp_path):
    """Schruppen im Gleichlauf, Schlichten im Gegenlauf: die Schlichtbahn läuft andersherum."""
    def umlauf(erg):
        from hirch_cam.post import tap_mm
        job = tap_mm.read(open(erg["programme"][-1]["datei"], newline="").read())
        pts = [(m.x, m.y) for m in job.toolpaths[-1].moves if not m.rapid and abs(m.z + 1.5) < 1e-6]  # Schlichten
        assert pts
        return sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(pts, pts[1:]))
    ua = umlauf(engine.rechnen(_auftrag(tmp_path, schlichten=True, schlicht_zugabe=0.3, gleichlauf=True)))
    ub = umlauf(engine.rechnen(_auftrag(tmp_path, schlichten=True, schlicht_zugabe=0.3, gleichlauf=True,
                                        schlicht_gleichlauf=False)))
    assert ua * ub < 0


def test_gewinde_ohne_gewindefraeser_ausgelassen(tmp_path):
    import math
    kreis = [[30 + 3 * math.cos(i / 36 * 2 * math.pi), 20 + 3 * math.sin(i / 36 * 2 * math.pi)] for i in range(37)]
    a = _auftrag(tmp_path)
    a["bearbeitungen"].append({"id": "g", "art": "gewinde", "name": "Gewinde", "werkzeug": "A", "tiefe": 6,
                               "objekte": [{"punkte": kreis, "geschlossen": True}]})
    erg = engine.rechnen(a)
    assert "g" not in erg["bearbeitungen"]
    assert any("kein Gewindefräser" in t for t in erg["op_warnungen"]["g"])
    a["bearbeitungen"][1]["werkzeug"] = "G"
    erg = engine.rechnen(a)
    assert erg["bearbeitungen"]["g"]["minuten"] > 0
