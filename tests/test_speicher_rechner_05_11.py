"""B-26 Weg C (Peter 05.11.): gerechnete 2D-Bahnen liegen auch auf dem Rechner (speicher.py, HIRCH_2D_SPEICHER).
Nach einem Neustart (Speicher im Prozess leer) wird nichts neu gerechnet, was sich nicht geändert hat – mit genau
demselben G-Code. Sicherheit: ein anderer Programmstand, eine geänderte Bearbeitung, ein anderes Werkzeug oder eine
kaputte Datei → neu gerechnet. Gespeichert werden nur Bahnen, nie G-Code."""
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(__file__))
import test_umspannen_27_10 as T  # noqa: E402

from hirch_cam import engine, speicher  # noqa: E402


@pytest.fixture
def ordner(tmp_path, monkeypatch):
    o = tmp_path / "2d"
    monkeypatch.setenv("HIRCH_2D_SPEICHER", str(o))
    speicher.SPEICHER.leeren()
    yield o
    speicher.SPEICHER.leeren()


def _rechnen(tmp_path, name, aendern=None):
    a = T._auftrag(tmp_path, name, umspannen={"an": True, "abstand": 10})
    a["_dokument"] = "muster.hir#1"
    if aendern:
        aendern(a)
    erg = engine.rechnen(a)
    assert erg["ok"]
    return erg


def _gcode(erg):
    return [open(d, newline="").read() for d in sorted(erg["dateien"])]


def test_nach_neustart_nichts_neu_und_derselbe_gcode(tmp_path, ordner):
    erst = _rechnen(tmp_path, "a")
    assert set(erst["neu_gerechnet"]) == {"pl", "g", "t", "b", "k"}
    assert len(list(ordner.glob("*.bahn2d"))) == 5
    speicher.SPEICHER.leeren()                       # wie ein Neustart des Programms
    zweit = _rechnen(tmp_path, "b")
    assert zweit["neu_gerechnet"] == []
    assert _gcode(zweit) == _gcode(erst)


def test_geaendert_wird_neu_gerechnet(tmp_path, ordner):
    _rechnen(tmp_path, "a")
    speicher.SPEICHER.leeren()

    def tiefer(a):
        next(b for b in a["bearbeitungen"] if b["id"] == "g")["tiefe"] = 1.5
    assert _rechnen(tmp_path, "b", tiefer)["neu_gerechnet"] == ["g"]
    speicher.SPEICHER.leeren()

    def werkzeug(a):
        a["werkzeuge"]["D"]["vorschub"] = 700
    assert set(_rechnen(tmp_path, "c", werkzeug)["neu_gerechnet"]) == {"g", "t"}


def test_anderer_programmstand_gilt_nicht(tmp_path, ordner, monkeypatch):
    _rechnen(tmp_path, "a")
    speicher.SPEICHER.leeren()
    monkeypatch.setattr(speicher, "_programm", "anderes Paket")
    assert set(_rechnen(tmp_path, "b")["neu_gerechnet"]) == {"pl", "g", "t", "b", "k"}


def test_kaputte_datei_gilt_nicht(tmp_path, ordner):
    erst = _rechnen(tmp_path, "a")
    speicher.SPEICHER.leeren()
    for f in ordner.glob("*.bahn2d"):
        f.write_bytes(f.read_bytes()[:50])           # abgebrochen geschrieben
    zweit = _rechnen(tmp_path, "b")
    assert set(zweit["neu_gerechnet"]) == {"pl", "g", "t", "b", "k"}
    assert _gcode(zweit) == _gcode(erst)


def test_aus(tmp_path, monkeypatch):
    monkeypatch.setenv("HIRCH_2D_SPEICHER", "aus")
    speicher.SPEICHER.leeren()
    _rechnen(tmp_path, "a")
    assert speicher.ordner() is None
