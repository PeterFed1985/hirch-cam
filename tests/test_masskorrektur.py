"""Masskorrektur der Maschine: wirkt wie ein Aufmass an Konturen und Taschenwänden."""
import pytest

from hirch_cam import engine
from hirch_cam.post import tap_mm


def _auftrag(tmp_path, art, korr):
    q = [[10, 10], [50, 10], [50, 40], [10, 40]]
    b = {"id": "k", "art": art, "name": art, "werkzeug": "A", "tiefe": 1.0, "zustellung": 1.0,
         "objekte": [{"punkte": q, "geschlossen": True}], "eintauchen_art": "senkrecht"}
    if art == "kontur":
        b.update(seite="aussen", stege_anzahl=0)
    return {"werkzeuge": {"A": {"typ": "schaft", "durchmesser": 4, "nummer": 1, "vorschub": 1000,
                                "eintauchen": 300}},
            "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 5},
            "maschine": {"masskorrektur": korr}, "postprozessor": "vcarve_mm",
            "ausgabe": str(tmp_path / "t.tap"), "bearbeitungen": [b]}


def _breite(erg):
    job = tap_mm.read(open(erg["programme"][0]["datei"], newline="").read())
    xs = [m.x for tp in job.toolpaths for m in tp.moves if not m.rapid and m.z < 0]
    return max(xs) - min(xs)


@pytest.mark.parametrize("art,richtung", [("kontur", -1), ("tasche", 1)])
def test_korrektur(tmp_path, art, richtung):
    a = _breite(engine.rechnen(_auftrag(tmp_path, art, 0.0)))
    b = _breite(engine.rechnen(_auftrag(tmp_path, art, -0.05)))
    # Kontur aussen: Fräsermitte 0,05 mm näher am Teil (Teil kleiner); Tasche: Wand weiter aussen
    assert b - a == pytest.approx(richtung * 0.1, abs=0.01)
