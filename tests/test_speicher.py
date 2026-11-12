"""Zwischenspeicher je Bearbeitung (hirch_cam/speicher.py): nur Geändertes neu rechnen, G-Code bitgleich,
Abhängigkeiten (ausgeräumte Flächen) beachten, «nur prüfen» zeigt veraltete Bearbeitungen mit alten Bahnen."""
import copy

import pytest

from hirch_cam import engine, speicher


def _q(x, y, a):
    return [(x, y), (x + a, y), (x + a, y + a), (x, y + a)]


def _auftrag(tmp_path, name="teil"):
    return {
        "werkzeuge": {"A": {"typ": "schaft", "durchmesser": 3, "nummer": 1, "vorschub": 1000, "eintauchen": 300},
                      "B": {"typ": "schaft", "durchmesser": 6, "nummer": 2, "vorschub": 1500, "eintauchen": 400},
                      "C": {"typ": "schaft", "durchmesser": 2, "nummer": 3, "vorschub": 800, "eintauchen": 200}},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 18},
        "postprozessor": "richauto_a11", "reihenfolge": "liste",
        "maschine": {"werkzeugwechsel": "dateien"},
        "ausgabe": str(tmp_path / (name + ".tap")),
        "_dokument": "test-zeichnung",
        "bearbeitungen": [
            # t2 liegt in t1 und ist tiefer: sie weiss, was t1 schon ausgeräumt hat
            {"id": "t1", "art": "tasche", "name": "Tasche 1", "werkzeug": "B", "tiefe": 2, "zustellung": 1,
             "objekte": [{"punkte": _q(10, 10, 30), "geschlossen": True}]},
            {"id": "t2", "art": "tasche", "name": "Tasche 2", "werkzeug": "A", "tiefe": 4, "zustellung": 1,
             "objekte": [{"punkte": _q(15, 15, 20), "geschlossen": True}]},
            {"id": "g", "art": "gravur", "name": "Gravur", "werkzeug": "A", "tiefe": 0.5, "zustellung": 0.5,
             "objekte": [{"punkte": [(60, 10), (80, 30)], "geschlossen": False}]},
            {"id": "k", "art": "kontur", "name": "Kontur", "werkzeug": "B", "tiefe": 4, "zustellung": 2,
             "seite": "aussen", "stege_anzahl": 0,
             "objekte": [{"punkte": _q(5, 5, 90), "geschlossen": True}]},
        ]}


def _gcode(erg):
    return [open(d, newline="").read() for d in erg["dateien"]]


def _frisch(a, monkeypatch):
    """Dasselbe ohne Zwischenspeicher gerechnet."""
    monkeypatch.setenv("HIRCH_SPEICHER", "0")
    try:
        return _gcode(engine.rechnen(copy.deepcopy(a)))
    finally:
        monkeypatch.delenv("HIRCH_SPEICHER")


@pytest.fixture(autouse=True)
def _leer():
    speicher.SPEICHER.leeren()
    yield
    speicher.SPEICHER.leeren()


def test_unveraendert_nichts_neu_und_bitgleich(tmp_path, monkeypatch):
    a = _auftrag(tmp_path)
    erst = engine.rechnen(copy.deepcopy(a))
    g1 = _gcode(erst)
    assert sorted(erst["neu_gerechnet"]) == ["g", "k", "t1", "t2"]
    zweit = engine.rechnen(copy.deepcopy(a))
    assert zweit["neu_gerechnet"] == []
    assert _gcode(zweit) == g1 == _frisch(a, monkeypatch)
    assert zweit["bahnen"] == erst["bahnen"]
    assert zweit["bearbeitungen"] == erst["bearbeitungen"]
    assert zweit["veraltet"] == {"t1": False, "t2": False, "g": False, "k": False}


def test_nur_geaenderte_und_abhaengige_neu(tmp_path, monkeypatch):
    a = _auftrag(tmp_path)
    engine.rechnen(copy.deepcopy(a))
    # Gravur verschoben: nur sie
    a["bearbeitungen"][2]["objekte"][0]["punkte"] = [(62, 10), (82, 30)]
    erg = engine.rechnen(copy.deepcopy(a))
    assert erg["neu_gerechnet"] == ["g"]
    assert _gcode(erg) == _frisch(a, monkeypatch)
    # Tasche 1 grösser: auch Tasche 2 (ausgeräumt) und die Kontur (liest Ausgeräumtes) neu, Gravur nicht
    a["bearbeitungen"][0]["objekte"][0]["punkte"] = _q(10, 10, 32)
    erg = engine.rechnen(copy.deepcopy(a))
    assert sorted(erg["neu_gerechnet"]) == ["k", "t1", "t2"]
    assert _gcode(erg) == _frisch(a, monkeypatch)
    # Werkzeug C (von keiner Bearbeitung benutzt) geändert: nichts neu
    a["werkzeuge"]["C"]["vorschub"] = 900
    assert engine.rechnen(copy.deepcopy(a))["neu_gerechnet"] == []
    # Werkzeug A (Tasche 2, Gravur) geändert: genau diese zwei
    a["werkzeuge"]["A"]["vorschub"] = 1100
    erg = engine.rechnen(copy.deepcopy(a))
    assert sorted(erg["neu_gerechnet"]) == ["g", "t2"]
    assert _gcode(erg) == _frisch(a, monkeypatch)
    # Ausgabe, Nullpunkt, Postprozessor zählen erst danach: nichts neu, G-Code wie frisch
    a["nullpunkt"] = {"x": 50, "y": 50}
    a["postprozessor"] = "vcarve_mm"
    erg = engine.rechnen(copy.deepcopy(a))
    assert erg["neu_gerechnet"] == []
    assert _gcode(erg) == _frisch(a, monkeypatch)


def test_umsortieren_unabhaengige_nicht_neu(tmp_path, monkeypatch):
    a = _auftrag(tmp_path)
    engine.rechnen(copy.deepcopy(a))
    b = a["bearbeitungen"]
    a["bearbeitungen"] = [b[2], b[0], b[1], b[3]]       # Gravur zuerst: liest nichts Ausgeräumtes
    erg = engine.rechnen(copy.deepcopy(a))
    assert erg["neu_gerechnet"] == []
    assert _gcode(erg) == _frisch(a, monkeypatch)
    # Tasche 2 vor Tasche 1: beide lesen jetzt anderes Ausgeräumtes → neu; die Gravur nicht
    a["bearbeitungen"] = [b[2], b[1], b[0], b[3]]
    erg = engine.rechnen(copy.deepcopy(a))
    assert {"t1", "t2"} <= set(erg["neu_gerechnet"]) and "g" not in erg["neu_gerechnet"]
    assert _gcode(erg) == _frisch(a, monkeypatch)


def test_nur_pruefen_zeigt_veraltete_mit_alten_bahnen(tmp_path):
    a = _auftrag(tmp_path)
    erst = engine.rechnen(copy.deepcopy(a))
    a["bearbeitungen"][0]["objekte"][0]["punkte"] = _q(12, 10, 30)   # Tasche 1 verschoben
    p = copy.deepcopy(a)
    p["nur_pruefen"] = True
    erg = engine.rechnen(p)
    assert erg["neu_gerechnet"] == []
    # Tasche 1 geändert; Tasche 2 und Kontur hängen von ihr ab; die Gravur nicht
    assert erg["veraltet"] == {"t1": True, "t2": True, "g": False, "k": True}
    assert erg["bearbeitungen"]["t1"]["veraltet"] is True
    assert erg["bahnen"]["t1"] == erst["bahnen"]["t1"]                # alte Bahnen bleiben stehen
    assert _gcode(erg) == _gcode(erst)
    # richtig rechnen (Speichern/USB): danach alles aktuell
    neu = engine.rechnen(copy.deepcopy(a))
    assert sorted(neu["neu_gerechnet"]) == ["k", "t1", "t2"]
    p = copy.deepcopy(a)
    p["nur_pruefen"] = True
    erg = engine.rechnen(p)
    assert not any(erg["veraltet"].values())
    assert _gcode(erg) == _gcode(neu)


def test_nur_pruefen_nie_gerechnet(tmp_path):
    a = _auftrag(tmp_path)
    a["nur_pruefen"] = True
    erg = engine.rechnen(a)
    assert erg["ok"] is False and erg["neu_gerechnet"] == []
    assert erg["veraltet"] == {"t1": True, "t2": True, "g": True, "k": True}


def test_speicher_begrenzt():
    s = speicher.Speicher(hoechstens=2)
    for i in range(3):
        s.ablegen("d", f"b{i}", 1, speicher.Eintrag(f"k{i}"))
    assert len(s) == 2 and s.holen("k0") is None and s.letzter("d", "b0", 1) is None
    assert s.letzter("d", "b2", 1).schluessel == "k2"
