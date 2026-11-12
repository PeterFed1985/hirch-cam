"""Peter 30.10. abends: Umriss genau (ohne Treppen) und auch auf Seite 2, über die Trennlinie hinaus fräsen,
3D-Bahnen orange und blau."""
import math
import os

import numpy as np
import pytest

from hirch_cam.farben import alte_relief_farbe, schattierung
from hirch_cam.ops import relief_zweiseitig as zs

HIER = os.path.dirname(os.path.abspath(__file__))


def _quader(b, h, d, winkel, mitte):
    """Geschlossener Quader (12 Dreiecke), um Z gedreht, Oberkante Z = 0."""
    c, s = math.cos(math.radians(winkel)), math.sin(math.radians(winkel))
    ecken = []
    for z in (0.0, -d):
        for x, y in ((-b / 2, -h / 2), (b / 2, -h / 2), (b / 2, h / 2), (-b / 2, h / 2)):
            ecken.append((mitte[0] + c * x - s * y, mitte[1] + s * x + c * y, z))
    p = np.array(ecken)
    f = [(0, 1, 2), (0, 2, 3), (4, 6, 5), (4, 7, 6), (0, 4, 5), (0, 5, 1), (1, 5, 6), (1, 6, 2),
         (2, 6, 7), (2, 7, 3), (3, 7, 4), (3, 4, 0)]
    return np.array([[p[i], p[j], p[k]] for i, j, k in f], np.float32)


def test_umriss_ohne_treppen():
    from shapely.geometry import Polygon
    from hirch_cam.ops.relief_vektoren import _umriss_genau
    tri = _quader(80, 40, 10, 12, (60, 60))
    bx = (10.0, 10.0, 110.0, 110.0)
    lage = zs.analysieren(tri, bx, 0.5)
    ringe = _umriss_genau(tri, lage, 0.05)
    assert ringe and len(ringe) == 1
    assert len(ringe[0]) <= 6, ringe            # vier Ecken (+ Schluss), keine Stufen der 0,5-mm-Zellen
    assert Polygon(ringe[0]).area == pytest.approx(80 * 40, abs=0.5)


def test_ueber_trennlinie_automatisch_und_von_hand():
    auftrag = {"werkzeuge": {"K": {"typ": "kugel", "durchmesser": 6.0}, "N": {"typ": "konisch", "eckradius": 1.5},
                             "C": {"typ": "schaft", "durchmesser": 6.0}},
               "bearbeitungen": [{"art": "relief_schruppen", "werkzeug": "C", "auf_seite": 1},
                                 {"art": "relief_schlichten", "werkzeug": "K", "auf_seite": 1},
                                 {"art": "relief_schlichten", "werkzeug": "N", "auf_seite": 2}]}
    cfg = zs.einstellungen({"zweiseitig": {"an": True}})
    assert zs.ueber_trennlinie(cfg, 1, auftrag) == pytest.approx(3.0)      # Kugel Ø 6
    assert zs.ueber_trennlinie(cfg, 2, auftrag) == pytest.approx(1.5)      # Spitzenradius
    hand = dict(cfg, ueber_trenn_auto=False, ueber_trenn_1=2.0, ueber_trenn_2=4.5)
    assert zs.ueber_trennlinie(hand, 1, auftrag) == 2.0
    assert zs.ueber_trennlinie(hand, 2, auftrag) == 4.5
    haut = dict(cfg, haut_an=True, haut=1.0)
    assert zs.ueber_trennlinie(haut, 1, auftrag) == 0.0                     # mit Haut nie


def test_zusatz_seite1_tiefer_seite2_hoeher():
    tri = _quader(40, 40, 20, 0, (50, 50))
    tri[..., 2] -= 10.0                       # Modell von −10 bis −30, Trennlinie bei −20
    lage = zs.analysieren(tri, (20.0, 20.0, 80.0, 80.0), 0.5)
    cfg = zs.einstellungen({"zweiseitig": {"an": True}})
    for seite, vz in ((1, -1), (2, 1)):
        ohne = zs.zusatz(lage, dict(cfg, _ueber=0.0), [], 8.0, 50.0, seite)
        mit = zs.zusatz(lage, dict(cfg, _ueber=3.0), [], 8.0, 50.0, seite)
        d = (mit - ohne)[np.isfinite(mit)]
        if seite == 1:
            assert np.allclose(d, -3.0, atol=1e-4)
        else:                                  # Seite 2: statt 0,5 mm Überlappung 3 mm
            assert np.allclose(d, 2.5, atol=1e-4)


def test_alte_graue_3d_farben_werden_bunt():
    assert schattierung("relief_schruppen", 0) == "#e66100"
    assert schattierung("relief_schlichten", 0) == "#1c71d8"
    assert alte_relief_farbe("relief_schruppen", "#5e5c64") == "#e66100"
    assert alte_relief_farbe("relief_schlichten", "#9A96A0") == "#1c71d8"
    assert alte_relief_farbe("relief_schlichten", "#5e5c64") is None       # anderer Ton bleibt
    assert alte_relief_farbe("tasche", "#5e5c64") is None


def test_einstellungen_nehmen_neue_farbe():
    import json
    from hirch_cam import einstellungen
    e = einstellungen.laden(json.dumps({"bearbeitungen": [{"id": "b1", "art": "relief_schlichten",
                                                           "farbe": "#9a96a0"}]}))
    assert next(b for b in e["bearbeitungen"] if b["id"] == "b1")["farbe"] == "#1c71d8"


pytest.importorskip("hirch_cam.hirch_relief")


def test_fluegel_umriss_genau_und_auf_seite_2(tmp_path, monkeypatch):
    from shapely.geometry import Polygon
    from hirch_cam import dokument, engine
    from hirch_cam.ops.relief_vektoren import vektoren
    gefangen = {}

    def fangen(a, *args, **kw):
        gefangen["a"] = a
        return {"ok": False, "warnungen": []}
    monkeypatch.setattr(engine, "rechnen", fangen)
    dokument.rechnen(os.path.join(HIER, "daten", "fluegel.hir"), str(tmp_path / "aus"), "m")
    r = gefangen["a"]["reliefs"][0]
    e = vektoren(r, 50.0, "umriss", material=(560.0, 560.0, 50.0), achse="y")
    seiten = sorted(l["seite"] for l in e["linien"])
    assert seiten == [1, 2]
    for l in e["linien"]:
        assert len(l["punkte"]) <= 12, len(l["punkte"])      # gerade Kanten statt Treppen
        assert Polygon(l["punkte"]).area == pytest.approx(130409, abs=20)
    s1 = next(l for l in e["linien"] if l["seite"] == 1)
    s2 = next(l for l in e["linien"] if l["seite"] == 2)
    assert s2["punkte"][0][0] == pytest.approx(560.0 - s1["punkte"][0][0], abs=1e-3)   # um Y gewendet


def _rinne(breite=6.0, tiefe=5.0):
    """Platte mit einer Rinne (Breite × Tiefe) entlang Y, Oberkante Z = 0 – als Dreiecke (nur die Oberseite)."""
    import numpy as np
    b = breite / 2
    pts = [(-20, 0), (-b, 0), (-b, -tiefe), (b, -tiefe), (b, 0), (20, 0)]
    tri = []
    for (x1, z1), (x2, z2) in zip(pts, pts[1:]):
        a, c, e, f = (x1, -20, z1), (x2, -20, z2), (x2, 20, z2), (x1, 20, z1)
        tri += [[a, c, e], [a, e, f]]
    return np.array(tri, np.float32)


def test_negatives_aufmass_kugel_kommt_in_die_rinne():
    import numpy as np
    from hirch_cam.ops.relief3d import _abtaster
    tri = _rinne(5.9, 5.0)                                # Tasche knapp kleiner als der Fräser
    w = {"typ": "kugel", "durchmesser": 6.0, "schneidenlaenge": 20}
    q = np.array([[0.0, 0.0], [-10.0, 0.0]])
    z0 = _abtaster(tri, w, 0.0, -30.0)(q)
    z1 = _abtaster(tri, w, -0.2, -30.0)(q)
    assert z0[1] == pytest.approx(0.0, abs=0.02)          # auf der Platte: aufs Mass
    assert z1[1] == pytest.approx(-0.2, abs=0.02)         # überall 0,2 mm tiefer
    assert z0[0] > -5.0 + 0.5                             # Ø 6 in 5,9 mm: hängt an den Kanten
    assert z1[0] == pytest.approx(-5.2, abs=0.05)         # mit −0,2: ganz hinunter (0,2 unter den Boden)
