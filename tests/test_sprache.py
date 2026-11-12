"""Deutsch/Englisch: Katalog, Muster, Ausgabe an das Panel."""
import json
import os

from hirch_cam import sprache


def _en(monkeypatch):
    monkeypatch.setenv("HIRCH_SPRACHE", "en")


def test_deutsch_bleibt(monkeypatch):
    monkeypatch.setenv("HIRCH_SPRACHE", "de")
    assert sprache.tr("Nichts zu fräsen") == "Nichts zu fräsen"
    assert sprache.ausgabe({"warnungen": ["Fehler: x"]}) == {"warnungen": ["Fehler: x"]}


def test_muster_und_zahlen(monkeypatch):
    _en(monkeypatch)
    assert sprache.tr("Steg") == "Tab"
    assert sprache.tr("Lücke 0,5 mm geschlossen") == "Gap of 0.5 mm closed"
    assert sprache.tr("   Tasche (Grün)") == "   Pocket (Green)"
    assert sprache.tr("Kontur (Blau): Fehler: kaputt") .startswith("Profile (Blue): ")


def test_daten_bleiben(monkeypatch):
    _en(monkeypatch)
    aus = sprache.ausgabe({"namen": {"b1": "Tasche"}, "einstellungen": {"bearbeitungen": [{"name": "Tasche"}]},
                           "datenbank": [{"name": "Schaftfräser 3 mm"}], "ok": True})
    assert aus["namen"]["b1"] == "Pocket"
    assert aus["einstellungen"]["bearbeitungen"][0]["name"] == "Tasche"
    assert aus["datenbank"][0]["name"] == "Schaftfräser 3 mm"


def test_katalog_platzhalter_stimmen():
    pfad = os.path.join(os.path.dirname(sprache.__file__), "sprache", "en.json")
    with open(pfad, encoding="utf-8") as f:
        kat = json.load(f)
    assert len(kat) > 1000
    for de, en in kat.items():
        assert de.count("{}") == en.count("{}"), de
