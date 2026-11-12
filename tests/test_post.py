"""Postprozessoren als Vorlagen: WinCNC, eigene anlegen/ändern/entfernen, Werkzeugwechsel-Einstellungen."""
import json
import os

import pytest

pytest.importorskip("shapely")

from hirch_cam import engine  # noqa: E402
from hirch_cam import post  # noqa: E402
from hirch_cam.post import verwalten  # noqa: E402


@pytest.fixture(autouse=True)
def eigener_ordner(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))


def _auftrag(tmp_path, key):
    return {
        "werkzeuge": {"A": {"typ": "schaft", "durchmesser": 3, "nummer": 1, "vorschub": 1000, "eintauchen": 300,
                            "drehzahl": 18000},
                      "B": {"typ": "schaft", "durchmesser": 6, "nummer": 1, "vorschub": 1500, "eintauchen": 400,
                            "drehzahl": 16000}},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 18},
        "gravur": {"tiefe": 0.5, "zustellung": 0.5, "werkzeug": "A"},
        "kontur": {"tiefe": 18.3, "zustellung": 6.1, "werkzeug": "B", "stege_anzahl": 0},
        "postprozessor": key,
        "ausgabe": str(tmp_path / "teil.tap"),
        "objekte": {"kontur": [{"punkte": [[0, 0], [100, 0], [100, 60], [0, 60]], "geschlossen": True}],
                    "gravur": [{"punkte": [[10, 50], [30, 50]], "geschlossen": False}]},
    }


def test_wincnc_eine_datei(tmp_path):
    erg = engine.rechnen(_auftrag(tmp_path, "wincnc_mm"))
    assert erg["ok"], erg
    z = open(erg["dateien"][0], newline="").read().split("\r\n")
    assert z[1:3] == ["G90", "G22 [Millimeter]"]
    assert z[3].startswith("T1 [Schaftfraeser") and z[4] == "S18000" and z[5] == "M3"
    i = z.index("T2 [Schaftfraeser D 6 mm]")
    assert z[i - 3:i] == ["M5", "G0 Z20.000", "G0 X0.000 Y0.000"]
    assert z[i + 1] == "S16000"
    assert "G1 Z-0.500 F300.0" in z
    assert z[-4:] == ["M5", "G53 Z0", "G0 X0.000 Y0.000", ""]


def test_eigener_postprozessor_wechselposition_und_einzeldateien(tmp_path):
    d = dict(post.alle()["richauto_a11"])
    d.update(name="Meine Fräse", werkzeugwechsel=True, wechsel_x=150, wechsel_y=-20, wechsel_z=60)
    d["wechsel"] = ["G0Z{wz}", "G0X{wx}Y{wy}", "M5", "T{t}M6"]
    d.pop("schluessel", None)
    k = verwalten.speichern(d)
    assert k == "meine_frase" or k.startswith("meine")
    assert [k, "Meine Fräse"] in [e[:2] for e in post.liste()]
    erg = engine.rechnen(_auftrag(tmp_path, k))
    text = open(erg["dateien"][0], newline="").read()
    assert "G0Z60.000\r\nG0X150.000Y-20.000\r\nM5\r\nT2M6\r\n" in text
    # Wechselposition aus: die Zeilen mit {wx}/{wy}/{wz} fallen weg
    d2 = dict(post.alle()[k], schluessel=k, wechselposition=False)
    verwalten.speichern(d2)
    text = open(engine.rechnen(_auftrag(tmp_path, k))["dateien"][0], newline="").read()
    assert "X150.000" not in text and "M5\r\nT2M6" in text
    # ohne Werkzeugwechsel: eine Datei je Werkzeug
    verwalten.speichern(dict(post.alle()[k], schluessel=k, werkzeugwechsel=False))
    erg = engine.rechnen(_auftrag(tmp_path, k))
    assert len(erg["dateien"]) == 2
    assert all("T2M6" not in open(f, newline="").read() for f in erg["dateien"][:1])
    # entfernen: eigener verschwindet, mitgelieferter wird nur ausgeblendet
    verwalten.entfernen(k)
    assert k not in post.alle()
    verwalten.entfernen("wincnc_mm")
    assert "wincnc_mm" not in [a[0] for a in post.liste()]
    assert post.alle()["wincnc_mm"]["entfernt"]
    verwalten.zuruecksetzen("wincnc_mm")
    assert "wincnc_mm" in [a[0] for a in post.liste()]


def test_alte_vcarve_ausgabe_ausgeblendet_aber_nutzbar(tmp_path):
    assert "vcarve_mm" not in [a[0] for a in post.liste()]
    assert "vcarve_mm" in [a[0] for a in post.liste(auch="vcarve_mm")]
    erg = engine.rechnen(_auftrag(tmp_path, "vcarve_mm"))
    assert len(erg["dateien"]) == 2



def test_unbekannter_postprozessor_faellt_zurueck(tmp_path):
    erg = engine.rechnen(_auftrag(tmp_path, "geloescht_xy"))
    assert erg["ok"], erg
    assert any("geloescht_xy" in w for w in erg["warnungen"])
    assert open(erg["dateien"][0]).read().startswith("G17")


@pytest.mark.parametrize("schluessel", ["mach3_mm", "uccnc_mm", "masso_mm", "carvera", "ncstudio_mm",
                                        "centroid_mm", "fanuc_mm"])
def test_neue_vorlagen(tmp_path, schluessel):
    """Jede mitgelieferte Vorlage schreibt ein vollständiges Programm (Kopf, Bahnen, Ende)."""
    d = post.alle()[schluessel]
    assert d["name"] and "geprüft" in d["beschreibung"]
    erg = engine.rechnen(_auftrag(tmp_path, schluessel))
    assert erg["ok"], erg
    for f in erg["dateien"]:
        text = open(f, newline="").read()
        assert "M30" in text and ("G1" in text or "G01" in text)
        assert text.count("M3") >= 1
    if d.get("werkzeugwechsel"):
        assert len(erg["dateien"]) == 1
    else:
        assert len(erg["dateien"]) == 2
    if d.get("zeilennummern"):
        zeilen = open(erg["dateien"][0], newline="").read().splitlines()
        assert zeilen[1].startswith("O0001") and zeilen[2].startswith("N")
