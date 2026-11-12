"""B-25 mehrere Maschinen (Konzept, Peter 06.11.): je Maschine Werte und Fräser getrennt; Umwandlung einmal mit
Sicherung; die Zeichnung wählt ihre Maschine über HIRCH_MASCHINE."""
import json
import os

import pytest

from hirch_cam import maschinen, vorgaben, werkzeugdb


@pytest.fixture
def konfig(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))
    monkeypatch.setenv("APPDATA", str(tmp_path))
    monkeypatch.delenv("HIRCH_MASCHINE", raising=False)
    return tmp_path / "hirch-cam"


def test_umwandlung_einmal_mit_sicherung(konfig):
    konfig.mkdir(parents=True)
    alt = {"version": 4, "werkzeuge": [{"id": "x1", "name": "Mein Fräser", "typ": "schaft", "durchmesser": 4.0}],
           "maschine": {"x": 700.0, "postprozessor": "richauto_a11"}, "vorgaben": {"gleichlauf": False}}
    (konfig / "werkzeuge.json").write_text(json.dumps(alt), encoding="utf-8")
    db = werkzeugdb.laden()
    assert db["maschine"]["x"] == 700.0 and db["maschine"]["postprozessor"] == "richauto_a11"
    assert [w["name"] for w in db["werkzeuge"]] == ["Mein Fräser"]
    assert (konfig / "werkzeuge-vor-maschinen.json").exists()          # Sicherung
    assert not (konfig / "werkzeuge.json").exists()                    # altes Format wird nicht mehr gelesen
    assert maschinen.index()["maschinen"] == [{"id": "m1", "name": "Meine Maschine"}]
    assert vorgaben.wirksam()["gleichlauf"] is False                   # «So fräse ich» gilt für alle Maschinen


def test_neue_maschine_ist_eine_kopie_mit_eigenen_fraesern(konfig, monkeypatch):
    werkzeugdb.maschine_setzen({"x": 800.0})
    klein = maschinen.neu("Kleine Fräse")
    monkeypatch.setenv("HIRCH_MASCHINE", klein)
    assert werkzeugdb.laden()["maschine"]["x"] == 800.0                # Kopie
    werkzeugdb.maschine_setzen({"x": 300.0})
    w = werkzeugdb.speichern({"name": "Nur klein", "typ": "schaft", "durchmesser": 2.0})
    monkeypatch.setenv("HIRCH_MASCHINE", "m1")
    db = werkzeugdb.laden()
    assert db["maschine"]["x"] == 800.0 and w["id"] not in {x["id"] for x in db["werkzeuge"]}


def test_umbenennen_entfernen_und_die_letzte_bleibt(konfig):
    klein = maschinen.neu("Kleine Fräse")
    maschinen.umbenennen(klein, "Büro")
    assert maschinen.name(klein) == "Büro"
    maschinen.zuletzt_setzen(klein)
    assert maschinen.aktiv_id() == klein
    maschinen.entfernen(klein)
    assert maschinen.ids() == ["m1"] and maschinen.aktiv_id() == "m1"
    assert any(f.startswith(klein + "-entfernt-") for f in os.listdir(konfig / "maschinen"))   # nur umbenannt
    maschinen.entfernen("m1")
    assert maschinen.ids() == ["m1"]                                    # die letzte bleibt


def test_unbekannte_maschine_der_zeichnung(konfig, monkeypatch):
    monkeypatch.setenv("HIRCH_MASCHINE", "m-gibts-nicht")
    assert maschinen.aktiv_id() == "m1"


def test_befehle_fuer_die_oberflaeche(konfig, capsys):
    werkzeugdb.main(["maschinen", json.dumps({"was": "neu", "name": "Zweite"})])
    aus = json.loads(capsys.readouterr().out)
    neu = aus["neu"]
    assert [m["name"] for m in aus["maschinen"]] == ["Meine Maschine", "Zweite"]
    assert aus["maschine_id"] == "m1"
    werkzeugdb.main(["maschinen", json.dumps({"was": "zuletzt", "id": neu})])
    aus = json.loads(capsys.readouterr().out)
    assert aus["maschine_id"] == neu and [m["aktiv"] for m in aus["maschinen"]] == [False, True]


def test_wechseln_vergleicht_die_fraeser(konfig, capsys):
    klein = maschinen.neu("Kleine Fräse")
    zeichnung = {"A": {"name": "V-Fräser 60°", "typ": "v", "durchmesser": 12.0, "v_winkel": 60},
                 "X": {"name": "Sonderfräser", "typ": "schaft", "durchmesser": 4.2, "schneiden": 1}}
    werkzeugdb.main(["maschinen", json.dumps({"was": "vergleich", "ziel": klein, "werkzeuge": zeichnung})])
    v = {e["slot"]: e for e in json.loads(capsys.readouterr().out)["vergleich"]}
    assert v["A"]["gefunden"] and not v["X"]["gefunden"]          # V 60 gehört zu den mitgelieferten
    werkzeugdb.main(["maschinen", json.dumps({"was": "wechseln", "ziel": klein, "werkzeuge": zeichnung,
                                              "uebernehmen": True})])
    aus = json.loads(capsys.readouterr().out)
    assert aus["maschine_id"] == klein and len(aus["uebernommen"]) == 1
    namen_klein = {w.get("name") for w in werkzeugdb.laden(maschinen.datei(klein))["werkzeuge"]}
    namen_m1 = {w.get("name") for w in werkzeugdb.laden(maschinen.datei("m1"))["werkzeuge"]}
    assert "Sonderfräser" in namen_klein and "Sonderfräser" not in namen_m1
