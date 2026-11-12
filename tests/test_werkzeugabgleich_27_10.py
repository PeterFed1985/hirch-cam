"""Werkzeugkatalog zweier PCs über einen Ordner abgleichen (Peter 27.10.): je Werkzeug die neuere
Änderung, Gelöschtes bleibt gelöscht, der Zielordner bleibt auf jedem PC eigen."""
import json
import os
import time

import pytest

from hirch_cam import werkzeugabgleich, werkzeugdb


def _pc(monkeypatch, ordner):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(ordner))


def _ids(db):
    return [w["id"] for w in db["werkzeuge"]]


def test_zwei_pcs_ein_katalog(tmp_path, monkeypatch):
    a, b, teilen = tmp_path / "a", tmp_path / "b", tmp_path / "nextcloud"
    teilen.mkdir()
    # PC A: eigener Katalog mit Zielordner, dann Abgleich einschalten → Kopie im Ordner
    _pc(monkeypatch, a)
    werkzeugdb.maschine_setzen({"zielordner": "/home/anna/fraese", "vorschub_max": 4000})
    w = werkzeugdb.speichern({"name": "Bohrer 3,3", "typ": "bohrer", "durchmesser": 3.3})
    erg = werkzeugabgleich.setzen(True, str(teilen))
    assert erg["an"] and (teilen / "maschinen" / "m1.json").exists() and (teilen / "maschinen.json").exists()
    ids_a = _ids(werkzeugdb.laden())
    assert w["id"] in ids_a

    # PC B: noch kein Katalog – bekommt den von A, aber nicht dessen Zielordner
    _pc(monkeypatch, b)
    werkzeugabgleich.setzen(True, str(teilen))
    db_b = werkzeugdb.laden()
    assert _ids(db_b) == ids_a
    assert db_b["maschine"]["vorschub_max"] == 4000
    werkzeugdb.maschine_setzen({"zielordner": "C:/Fraese"})

    # B ändert den Bohrer, A legt gleichzeitig einen neuen Fräser an: beides bleibt
    time.sleep(0.01)
    werkzeugdb.speichern(dict(w, name="Bohrer 3,3 HSS"))
    _pc(monkeypatch, a)
    neu = werkzeugdb.speichern({"name": "Schaftfräser 5", "typ": "schaft", "durchmesser": 5.0})
    assert "_" in neu["id"]                         # eindeutige ID, solange abgeglichen wird
    db_a = werkzeugdb.laden()
    namen = {x["id"]: x.get("name") for x in db_a["werkzeuge"]}
    assert namen[w["id"]] == "Bohrer 3,3 HSS" and neu["id"] in namen
    assert db_a["maschine"]["zielordner"] == "/home/anna/fraese"

    # A löscht den Bohrer – auf B verschwindet er und kommt nicht zurück
    werkzeugdb.loeschen(w["id"])
    _pc(monkeypatch, b)
    db_b = werkzeugdb.laden()
    assert w["id"] not in _ids(db_b) and neu["id"] in _ids(db_b)
    assert db_b["maschine"]["zielordner"] == "C:/Fraese"
    _pc(monkeypatch, a)
    assert w["id"] not in _ids(werkzeugdb.laden())
    # vor dem Überschreiben gesichert
    assert any(f.startswith("werkzeuge-m1 ") for f in os.listdir(a / "hirch-cam" / "sicherungen"))


def test_unveraendert_uebernommen_aendert_die_zeit_nicht(tmp_path):
    datei = str(tmp_path / "w.json")
    w = werkzeugdb.speichern({"name": "V 60", "typ": "v", "durchmesser": 12, "v_winkel": 60}, datei)
    t = w["geaendert"]
    time.sleep(0.01)
    w2 = werkzeugdb.uebernehmen({"speichern": [dict(w)]}, datei)["gespeichert"][0]
    assert w2["geaendert"] == t


def test_link_in_den_ordner_wird_eigene_datei(tmp_path, monkeypatch):
    """Bisher (20.10.) war werkzeuge.json ein Link in den Nextcloud-Ordner – mit dem Abgleich eine eigene Datei."""
    _pc(monkeypatch, tmp_path / "pc")
    teilen = tmp_path / "nc"
    teilen.mkdir()
    werkzeugdb.speichern({"name": "Kugel 3", "typ": "kugel", "durchmesser": 3}, str(teilen / "werkzeuge.json"))
    lokal = tmp_path / "pc" / "hirch-cam" / "werkzeuge.json"
    lokal.parent.mkdir(parents=True)
    try:
        os.symlink(teilen / "werkzeuge.json", lokal)
    except OSError:
        pytest.skip("Verknüpfungen braucht unter Windows Adminrechte oder den Entwicklermodus")
    werkzeugabgleich.setzen(True, str(teilen))
    assert not os.path.islink(lokal)
    m1 = tmp_path / "pc" / "hirch-cam" / "maschinen" / "m1.json"         # seit B-25: Maschine m1
    assert not os.path.islink(m1)
    assert json.load(open(m1, encoding="utf-8"))["werkzeuge"][-1]["name"] == "Kugel 3"


def test_ordner_fehlt_meldet_nur(tmp_path, monkeypatch):
    _pc(monkeypatch, tmp_path / "pc")
    erg = werkzeugabgleich.setzen(True, str(tmp_path / "gibts-nicht"))
    assert "nicht erreichbar" in erg["meldung"]
    assert werkzeugdb.laden()["werkzeuge"]               # Katalog bleibt nutzbar


def test_maschinen_zweier_pcs(tmp_path, monkeypatch):
    """B-25: Jede Maschine hat ihre eigene Datei; neue Maschinen eines PCs kommen auf den anderen, entfernte bleiben weg,
    die Fräser bleiben je Maschine getrennt."""
    from hirch_cam import maschinen
    a, b, teilen = tmp_path / "a", tmp_path / "b", tmp_path / "nc"
    teilen.mkdir()
    _pc(monkeypatch, a)
    werkzeugdb.speichern({"name": "Kugel 3", "typ": "kugel", "durchmesser": 3})
    werkzeugabgleich.setzen(True, str(teilen))
    klein = maschinen.neu("Kleine Fräse")
    monkeypatch.setenv("HIRCH_MASCHINE", klein)
    werkzeugdb.speichern({"name": "Schaft 1", "typ": "schaft", "durchmesser": 1})
    monkeypatch.delenv("HIRCH_MASCHINE")
    werkzeugabgleich.abgleichen()
    _pc(monkeypatch, b)
    werkzeugabgleich.setzen(True, str(teilen))
    assert [m["name"] for m in maschinen.index()["maschinen"]] == ["Meine Maschine", "Kleine Fräse"]
    monkeypatch.setenv("HIRCH_MASCHINE", klein)
    namen_klein = {w.get("name") for w in werkzeugdb.laden()["werkzeuge"]}
    monkeypatch.setenv("HIRCH_MASCHINE", "m1")
    namen_m1 = {w.get("name") for w in werkzeugdb.laden()["werkzeuge"]}
    assert "Schaft 1" in namen_klein and "Schaft 1" not in namen_m1
    monkeypatch.delenv("HIRCH_MASCHINE")
    # B entfernt die kleine Fräse – auf A ist sie danach auch weg
    time.sleep(0.01)
    maschinen.entfernen(klein)
    werkzeugabgleich.abgleichen()
    _pc(monkeypatch, a)
    werkzeugabgleich.abgleichen()
    assert maschinen.ids() == ["m1"]


def test_alter_katalog_im_ordner_wird_maschine_m1(tmp_path, monkeypatch):
    from hirch_cam import maschinen
    teilen = tmp_path / "nc"
    teilen.mkdir()
    werkzeugdb.speichern({"name": "Gravierstichel", "typ": "v", "durchmesser": 3, "v_winkel": 30},
                         str(teilen / "werkzeuge.json"))
    _pc(monkeypatch, tmp_path / "pc")
    werkzeugabgleich.setzen(True, str(teilen))
    assert (teilen / "maschinen" / "m1.json").exists() and not (teilen / "werkzeuge.json").exists()
    assert "Gravierstichel" in {w.get("name") for w in werkzeugdb.laden()["werkzeuge"]}
    assert maschinen.ids() == ["m1"]
