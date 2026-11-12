"""Prüfung vor dem Fräsen."""
import pytest

pytest.importorskip("shapely")

from hirch_cam import engine  # noqa: E402
from hirch_cam.pruefung import pruefen  # noqa: E402


def _q(x, y, a, b=None):
    b = b or a
    return [(x, y), (x + a, y), (x + a, y + b), (x, y + b), (x, y)]


def _auftrag(tmp_path, bearbeitungen, werkzeuge=None, maschine=None):
    return {
        "werkzeuge": werkzeuge or {
            "C": {"typ": "schaft", "durchmesser": 6, "vorschub": 1500, "eintauchen": 300,
                  "schneidenlaenge": 20, "mittenschnitt": True},
            "P": {"typ": "schaft", "durchmesser": 22, "vorschub": 1500, "eintauchen": 300,
                  "schneidenlaenge": 6, "mittenschnitt": False}},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 18},
        "maschine": maschine or {"x": 600, "y": 900, "z": 150},
        "ausgabe": str(tmp_path / "p.tap"), "bearbeitungen": bearbeitungen}


def _texte(b):
    return [x["stufe"] + ": " + x["text"] for x in b]


def test_sauberer_auftrag_ohne_befunde(tmp_path):
    a = _auftrag(tmp_path, [
        {"id": "t", "art": "tasche", "name": "Tasche", "werkzeug": "C", "tiefe": 3, "zustellung": 3,
         "objekte": [{"punkte": _q(20, 20, 60), "geschlossen": True}]},
        {"id": "k", "art": "kontur", "name": "Kontur", "werkzeug": "C", "tiefe": 18.2, "zustellung": 3,
         "seite": "aussen", "stege_anzahl": 4, "objekte": [{"punkte": _q(10, 10, 80), "geschlossen": True}]}])
    erg = engine.rechnen(a)
    assert pruefen(a, erg) == []


def test_findet_die_typischen_probleme(tmp_path):
    a = _auftrag(tmp_path, [
        {"id": "t1", "art": "tasche", "name": "Tasche 1", "werkzeug": "C", "tiefe": 25, "zustellung": 25,
         "objekte": [{"punkte": _q(20, 20, 30), "geschlossen": True},
                     {"punkte": [(100, 100), (120, 100), (120, 120), (100, 120), (100, 100.3)], "geschlossen": False}]},
        {"id": "t2", "art": "tasche", "name": "Tasche 2", "werkzeug": "C", "tiefe": 2, "zustellung": 2,
         "objekte": [{"punkte": _q(50.5, 20, 30), "geschlossen": True}]},
        {"id": "p", "art": "tasche", "name": "Planen", "werkzeug": "P", "tiefe": 1, "zustellung": 1,
         "eintauchen_art": "senkrecht", "objekte": [{"punkte": _q(200, 200, 100), "geschlossen": True}]},
        {"id": "k", "art": "kontur", "name": "Kontur", "werkzeug": "C", "tiefe": 18.2, "zustellung": 3,
         "seite": "aussen", "stege_anzahl": 0,
         "objekte": [{"punkte": _q(650, 10, 20), "geschlossen": True},
                     {"punkte": _q(650, 10, 20), "geschlossen": True}]}])
    erg = engine.rechnen(a)
    t = "\n".join(_texte(pruefen(a, erg)))
    assert "fehler: Tasche 1: 25 mm tief, die Schneide ist aber nur 20 mm lang" in t
    assert "fehler: Tasche 1: 25 mm tief, das Material ist nur 18 mm dick" in t
    assert "offene Kontur (Lücke 0,30 mm)" in t
    assert "Wand zwischen Tasche 1 und Tasche 2 nur 0,50 mm dick" in t
    assert "fehler: Planen: Fräser ohne Mittenschnitt taucht" in t
    assert "Kontur: ganz durch, aber ohne Haltestege" in t
    assert "kleines Teil (20 × 20 mm)" in t
    assert "Linie liegt doppelt" in t
    assert "Verfahrweg der Maschine" in t
    assert t.index("fehler") < t.index("warnung")         # Fehler zuerst


def test_rampe_vermeidet_den_mittenschnitt_fehler(tmp_path):
    a = _auftrag(tmp_path, [
        {"id": "p", "art": "tasche", "name": "Planen", "werkzeug": "P", "tiefe": 1, "zustellung": 1,
         "rampe_winkel": 3, "objekte": [{"punkte": _q(200, 200, 100), "geschlossen": True}]}])
    erg = engine.rechnen(a)
    assert not any("Mittenschnitt" in x["text"] for x in pruefen(a, erg))
