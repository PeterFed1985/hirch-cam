"""Kontur auf einer offenen Linie (Peter 11.11.: «Aussen oder innen sollte die Seiten der Linie fräsen»; Entscheid:
Aussen = rechts, Innen = links in Zeichenrichtung, die Oberfläche zeigt die Seite mit einem Pfeil)."""
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "zufall"))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "werkzeuge"))

from hirch_cam.ops.kontur import kontur  # noqa: E402

WINKEL = [([(0, 0), (20, 0), (20, 20)], False)]          # nach rechts, dann nach oben (Linkskurve)


def _bahn(seite, linien=WINKEL, d=6.0, aufmass=0.0):
    tp, warn = kontur(linien, d, 2.0, 2.0, 1000, 300, seite, True, 10.0, 0, 5, 1.5, aufmass=aufmass)
    return [(m.x, m.y) for m in tp.moves if not m.rapid], warn


def test_aussen_rechts_innen_links():
    aussen, _ = _bahn("aussen")
    innen, _ = _bahn("innen")
    linie, _ = _bahn("linie")
    auto, _ = _bahn("auto")
    assert min(y for _x, y in aussen) == pytest.approx(-3.0) and max(x for x, _y in aussen) == pytest.approx(23.0)
    assert min(y for _x, y in innen) == pytest.approx(3.0) and max(x for x, _y in innen) == pytest.approx(17.0)
    assert min(y for _x, y in linie) == pytest.approx(0.0) and auto == linie
    # aussen um die Ecke im Bogen (der Fräser bleibt 3 mm von der Ecke), innen gekürzt
    import math
    assert min(math.dist(p, (20, 0)) for p in aussen) == pytest.approx(3.0, abs=0.01)
    assert (17.0, 3.0) in [(round(x, 3), round(y, 3)) for x, y in innen]


def test_aufmass_und_richtung():
    """Aufmass schiebt weiter weg; eine umgekehrt gezeichnete Linie tauscht die Seite (darum der Pfeil)."""
    aussen, _ = _bahn("aussen", aufmass=0.5)
    assert min(y for _x, y in aussen) == pytest.approx(-3.5)
    rueck = [([(20, 20), (20, 0), (0, 0)], False)]
    aussen_r, _ = _bahn("aussen", rueck)
    assert min(y for _x, y in aussen_r) == pytest.approx(3.0) and max(x for x, _y in aussen_r) == pytest.approx(17.0)


def test_zu_enge_kurve_wird_gemeldet():
    """Ein kleiner Haken innen: für Ø 20 bleibt auf der Innenseite nichts – Hinweis statt stiller Lücke."""
    haken = [([(0, 0), (4, 0), (4, 2), (0, 2)], False)]
    bahn, warn = _bahn("innen", haken, d=20.0)
    assert not bahn and any("zu stark gekrümmt" in t for t in warn), (bahn, warn)


def test_zufaellige_linien_nie_naeher_als_der_radius():
    """200 zufällige offene Linien, beide Seiten: die Fräsbahn kommt der gezeichneten Linie nirgends näher als den
    Fräserradius (sonst fräste sie in die Linie hinein)."""
    import random
    from shapely.geometry import LineString, Point
    rng = random.Random(11)
    for _ in range(100):
        pts = [(rng.uniform(0, 60), rng.uniform(0, 60)) for _ in range(rng.randint(2, 7))]
        d = rng.choice([2.0, 3.0, 6.0, 8.0, 12.0])
        for seite in ("aussen", "innen"):
            tp, _w = kontur([(pts, False)], d, 2.0, 2.0, 1000, 300, seite, True, 10.0, 0, 5, 1.5)
            ls, pos = LineString(pts), None
            for m in tp.moves:
                q = (m.x, m.y)
                if not m.rapid and pos is not None:
                    g = LineString([pos, q]) if pos != q else Point(q)
                    assert g.distance(ls) >= d / 2 - 0.02, (pts, d, seite, pos, q)
                pos = q


def test_ganzer_auftrag_sicher(tmp_path):
    """Durch die ganze Rechnung: Endprüfung ohne Fehler, kein Eilgang im Material."""
    import eilgang_pruefen as ep
    from zeichnung import WERKZEUGE
    from hirch_cam import dokument, engine
    from hirch_cam.einstellungen import STANDARD_BEARBEITUNG
    pytest.importorskip("hirch_cam.hirch_relief")
    for seite in ("aussen", "innen"):
        b = dict(STANDARD_BEARBEITUNG["kontur"], id="k", art="kontur", name="Kontur", werkzeug="C", tiefe=4.0,
                 zustellung=2.0, seite=seite, objekte=[{"punkte": [(10, 10), (60, 10), (60, 40), (30, 50)],
                                                        "geschlossen": False}])
        a = {"werkzeuge": {k: dict(v) for k, v in WERKZEUGE.items()},
             "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 10},
             "postprozessor": "vcarve_mm", "material_flaeche": [80, 60], "spannmittel": [], "reihenfolge": "auto",
             "bearbeitungen": [b], "ausgabe": str(tmp_path / ("k_%s.tap" % seite))}
        erg = engine.rechnen(a)
        assert erg["ok"], erg.get("warnungen")
        assert dokument.endpruefung(a, erg)["stufe"] != "fehler"
        assert ep.pruefen(erg, werkzeuge=a["werkzeuge"], aussen_frei=(0, 0, 80, 60)) == []
