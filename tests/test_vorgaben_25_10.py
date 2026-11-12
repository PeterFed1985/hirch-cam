"""Peters Entscheide 25.10.2026: «Im Bogen anfahren» ist Vorgabe der Kontur (alte Konturen bleiben senkrecht),
der Bogen hält Abstand zu Nachbarteilen; weite Taschen melden «adaptiv_vorschlag»."""
import json
import math

from shapely.geometry import LineString, Point, Polygon

from hirch_cam import einstellungen as einst
from hirch_cam import engine
from hirch_cam.ops.kontur import kontur
from hirch_cam.ops.tasche import adaptiv_lohnt


def test_bogen_vorgabe_neu_und_alte_bleiben_senkrecht():
    e = einst.laden(None)
    assert einst.neue_bearbeitung(e, "kontur")["anfahren"] == "bogen"
    alt = einst.laden(json.dumps({"bearbeitungen": [{"id": "k1", "art": "kontur", "tiefe": 3}]}))
    assert alt["bearbeitungen"][0]["anfahren"] == ""
    gewaehlt = einst.laden(json.dumps({"bearbeitungen": [{"id": "k1", "art": "kontur", "anfahren": "bogen"}]}))
    assert gewaehlt["bearbeitungen"][0]["anfahren"] == "bogen"


def _bogen_zuege(tp, teil):
    """Fräsbewegungen im Material, die nicht auf der Kontur selbst liegen (Anfahr-/Abfahrbogen)."""
    return [m for m in tp.moves if not m.rapid and m.z < 0 and teil.exterior.distance(Point(m.x, m.y)) > 3.05]


def test_bogen_haelt_abstand_zum_nachbarteil():
    """Schmales Teil zwischen zwei Nachbarn je 8 mm daneben: die Bahn passt (5 mm Abstand), ein voller Bogen
    (bis 6 mm neben der Bahn) schnitte den Nachbarn an – ohne Nachbarn-Prüfung 2 mm statt ≥ 3 mm Abstand."""
    from shapely.ops import unary_union
    a = [(20, 0), (30, 0), (30, 40), (20, 40), (20, 0)]
    l, r = [(0, 0), (12, 0), (12, 40), (0, 40)], [(38, 0), (50, 0), (50, 40), (38, 40)]
    n = unary_union([Polygon(l), Polygon(r)])
    tp, _w = kontur([(a, True)], 6, 6, 3, 1000, 300, materialdicke=6, stege_anzahl=0, anfahren="bogen",
                    nachbarn=[l, r], start=(0, 0, 20))
    assert min(n.distance(Point(m.x, m.y)) for m in tp.moves if not m.rapid and m.z < 0) >= 3 - 0.03
    assert _bogen_zuege(tp, Polygon(a[:-1]))                 # mit kleinerem Bogen, nicht senkrecht
    ohne, _w = kontur([(a, True)], 6, 6, 3, 1000, 300, materialdicke=6, stege_anzahl=0, anfahren="bogen",
                      start=(0, 0, 20))
    assert min(n.distance(Point(m.x, m.y)) for m in ohne.moves if not m.rapid and m.z < 0) < 3 - 0.5


def test_engine_gibt_nachbarn_der_anderen_kontur():
    q = lambda x, y: [(x, y), (x + 30, y), (x + 30, y + 20), (x, y + 20)]
    a = {"werkzeuge": {"A": {"typ": "schaft", "durchmesser": 6, "nummer": 1, "vorschub": 1000, "eintauchen": 300}},
         "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 6},
         "postprozessor": "richauto_a11", "ausgabe": "/dev/null",
         "bearbeitungen": [
             {"id": "k1", "art": "kontur", "name": "K1", "werkzeug": "A", "tiefe": 6, "zustellung": 3,
              "seite": "aussen", "anfahren": "bogen", "stege_modus": "keine",
              "objekte": [{"punkte": q(10, 10), "geschlossen": True}]},
             {"id": "k2", "art": "kontur", "name": "K2", "werkzeug": "A", "tiefe": 6, "zustellung": 3,
              "seite": "aussen", "anfahren": "bogen", "stege_modus": "keine",
              "objekte": [{"punkte": q(44, 10), "geschlossen": True}]}]}
    bb = engine._bearbeitungen(a, engine._werkzeug_setup(a)[1])
    k1 = next(b for b in bb if b["id"] == "k1")
    assert engine._kontur_nachbarn(bb, k1) == [[tuple(p) for p in q(44, 10)]]


def test_adaptiv_vorschlag_nur_fuer_weite_taschen():
    weit = [([(0, 0), (150, 0), (150, 100), (0, 100)], True)]
    eng = [([(0, 0), (150, 0), (150, 20), (0, 20)], True)]
    assert adaptiv_lohnt(weit, 6) and not adaptiv_lohnt(eng, 6)


def test_engine_feld_adaptiv_vorschlag(tmp_path):
    a = {"werkzeuge": {"A": {"typ": "schaft", "durchmesser": 6, "nummer": 1, "vorschub": 1000, "eintauchen": 300}},
         "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 10},
         "postprozessor": "richauto_a11", "ausgabe": str(tmp_path / "t.tap"),
         "bearbeitungen": [
             {"id": "t1", "art": "tasche", "name": "T1", "werkzeug": "A", "tiefe": 2, "zustellung": 2,
              "objekte": [{"punkte": [(0, 0), (60, 0), (60, 40), (0, 40)], "geschlossen": True}]},
             {"id": "t2", "art": "tasche", "name": "T2", "werkzeug": "A", "tiefe": 2, "zustellung": 2,
              "objekte": [{"punkte": [(80, 0), (95, 0), (95, 40), (80, 40)], "geschlossen": True}]}]}
    st = engine.rechnen(a)["bearbeitungen"]
    assert st["t1"]["adaptiv_vorschlag"] is True and st["t2"]["adaptiv_vorschlag"] is False
