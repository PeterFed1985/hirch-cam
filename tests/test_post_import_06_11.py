"""Postprozessoren einzeln exportieren und importieren (Peter 06.11.: «Das kannst du so machen»)."""
import json

import pytest

from hirch_cam import post
from hirch_cam.post import verwalten


@pytest.fixture(autouse=True)
def eigener_ordner(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    monkeypatch.setenv("APPDATA", str(tmp_path / "config"))


def test_export_und_import_ohne_ueberschreiben(tmp_path):
    datei = str(tmp_path / "grbl.json")
    verwalten.exportieren("grbl_mm", datei)
    roh = json.load(open(datei, encoding="utf-8"))
    assert roh["art"] == verwalten.DATEI_ART and roh["name"].startswith("Grbl")
    assert "an_maschine_geprueft" not in roh
    k = verwalten.importieren(datei)
    a = post.alle()
    assert k != "grbl_mm" and a[k]["name"] == roh["name"] + " (importiert)"
    assert a[k]["kopf"] == a["grbl_mm"]["kopf"] and not a["grbl_mm"].get("geaendert")
    f = post.faehigkeiten(a[k])
    assert f["eigen"] and not f["geprueft"]
    # auch die mitgelieferte RichAuto wird als Kopie ungeprüft
    verwalten.exportieren("richauto_a11", str(tmp_path / "ra.json"))
    k2 = verwalten.importieren(str(tmp_path / "ra.json"))
    assert not post.faehigkeiten(post.alle()[k2])["geprueft"]
    assert post.faehigkeiten(post.alle()["richauto_a11"])["geprueft"]


def test_import_prueft_die_datei(tmp_path):
    def schreiben(d):
        p = tmp_path / "x.json"
        p.write_text(json.dumps(d), encoding="utf-8")
        return str(p)
    with pytest.raises(ValueError):
        verwalten.importieren(schreiben({"art": "hirch-werkzeuge", "name": "x"}))
    with pytest.raises(ValueError):
        verwalten.importieren(schreiben({"art": verwalten.DATEI_ART, "name": "x", "endung": "../../a"}))
    with pytest.raises(ValueError):
        verwalten.importieren(schreiben({"art": verwalten.DATEI_ART, "name": "", "kopf": []}))
    # unbekannte Felder und die Kennzeichnung kommen nicht mit; eine Vorlage ohne «art» geht
    k = verwalten.importieren(schreiben({"name": "Meine", "kopf": ["G21"], "ende": ["M30"],
                                         "an_maschine_geprueft": True, "befehl": "rm -rf"}))
    d = post.alle()[k]
    assert "befehl" not in d and "an_maschine_geprueft" not in d and d["kopf"] == ["G21"]
    assert not post.faehigkeiten(d)["geprueft"]


def test_englische_platzhalter():
    """planung 06.11.: {tool} {safe} {file} {date} zusätzlich zu den deutschen Namen."""
    from hirch_cam.model import Job
    from hirch_cam.post.vorlage import Vorlage
    v = Vorlage("x", {"name": "x", "kopf": ["({file} {date})", "({datei})"], "werkzeug": ["(T{t} {tool} {werkzeug})",
                                                                                         "G0 Z{safe}"],
                      "ende": ["M30"]})
    text = v.write(Job(tool_number=3, safe_z=20.0), datei="teil.nc", werkzeug={"name": "Fräser 6"})
    zeilen = text.splitlines()
    assert zeilen[0].startswith("(teil.nc ") and zeilen[1] == "(teil.nc)"
    assert zeilen[2] == "(T3 Fraeser 6 Fraeser 6)" and zeilen[3].startswith("G0 Z20")
