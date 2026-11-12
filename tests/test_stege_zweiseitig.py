"""Zweiseitig: Haltestege auch, wenn erst beide Seiten zusammen durchschneiden (Peter, 23.10.2026)."""
from shapely.geometry import Polygon

from hirch_cam.engine import _durch_mit_gegenseite

AUFTRAG = {"material_flaeche": [100, 100], "zweiseitig": {"an": True, "achse": "x"}}
QUADRAT = [((20, 20), (80, 20), (80, 80), (20, 80)), True]


def _fuge_seite1(y_versatz=0.0):
    # Schnittfuge einer Kontur auf Seite 1, an der Wendeachse X gespiegelt = dieselbe Lage auf Seite 2
    aussen = Polygon([(17, 17 + y_versatz), (83, 17 + y_versatz), (83, 83 + y_versatz), (17, 83 + y_versatz)])
    innen = Polygon([(23, 23 + y_versatz), (77, 23 + y_versatz), (77, 77 + y_versatz), (23, 77 + y_versatz)])
    return aussen.difference(innen)


def test_beide_seiten_zusammen_durch():
    schnitte = {1: [(_fuge_seite1(), 25.0)]}
    assert _durch_mit_gegenseite(AUFTRAG, [QUADRAT], 5.0, 30.0, 2, schnitte)


def test_zu_wenig_tief():
    schnitte = {1: [(_fuge_seite1(), 20.0)]}
    assert not _durch_mit_gegenseite(AUFTRAG, [QUADRAT], 5.0, 30.0, 2, schnitte)


def test_anderer_ort():
    schnitte = {1: [(_fuge_seite1(y_versatz=-15.0), 25.0)]}    # gespiegelt liegt die Fuge 15 mm daneben
    assert not _durch_mit_gegenseite(AUFTRAG, [QUADRAT], 5.0, 30.0, 2, schnitte)


def test_selbst_durch_braucht_es_nicht():
    assert not _durch_mit_gegenseite(AUFTRAG, [QUADRAT], 30.0, 30.0, 2, {1: [(_fuge_seite1(), 25.0)]})
