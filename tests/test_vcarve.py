import math

import pytest

shapely = pytest.importorskip("shapely")
np = pytest.importorskip("numpy")
from shapely.geometry import Point, Polygon  # noqa: E402

from hirch_cam import engine  # noqa: E402
from hirch_cam.post import tap_mm  # noqa: E402

TAN30 = math.tan(math.radians(30))


@pytest.mark.parametrize("modul", [True, False], ids=["hirch_cam", "ohne-modul"])
def test_vcarve_gegen_idealform(tmp_path, monkeypatch, modul):
    from hirch_cam import cam
    if modul and not cam.verfuegbar():
        pytest.skip("CAM-Modul hirch_cam nicht gebaut")
    if not modul:
        monkeypatch.setattr(cam, "_hc", None)
    from shapely import vectorized

    from hirch_cam.simulation import Hoehenfeld

    ring_a = [(40 + 15 * math.cos(t / 64 * math.tau), 35 + 15 * math.sin(t / 64 * math.tau)) for t in range(64)]
    ring_i = [(40 + 8 * math.cos(t / 64 * math.tau), 35 + 8 * math.sin(t / 64 * math.tau)) for t in range(64)]
    L = [(70, 20), (76, 20), (76, 44), (90, 44), (90, 50), (70, 50)]
    breit = [(100, 20), (150, 20), (150, 50), (100, 50)]
    auftrag = {
        "werkzeug": {"nummer": 1, "durchmesser": 12, "v_winkel": 60, "drehzahl": 18000,
                     "vorschub": 1200, "eintauchen": 300},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 18},
        "vcarve": {"max_tiefe": 4, "flach_durchmesser": 3, },
        "toleranz": 0.01,
        "ausgabe": str(tmp_path / "vc.tap"),
        "objekte": {"vcarve": [{"punkte": r, "geschlossen": True} for r in (ring_a, ring_i, L, breit)]},
    }
    erg = engine.rechnen(auftrag)
    assert erg["ok"], erg
    assert len(erg["dateien"]) == 2
    region = Polygon(ring_a, [ring_i]).union(Polygon(L)).union(Polygon(breit))
    h = Hoehenfeld(0, 0, 170, 70, 0.1)
    # erst der Schaftfräser für den Flachbereich, dann der V-Fräser (er schlichtet die Kante)
    flach_datei, v_datei = erg["dateien"]
    assert "D3" in flach_datei and "V60" in v_datei
    h.fraesen(tap_mm.read(open(flach_datei, newline="").read()), "flach", 3)
    h.fraesen(tap_mm.read(open(v_datei, newline="").read()), "v", 12, 60)
    X, Y = h.koordinaten()
    innen = vectorized.contains(region, X, Y)
    nahe = vectorized.contains(region.buffer(0.15), X, Y)
    assert h.H[~nahe].min() > -1e-6                 # ausserhalb nichts verletzt
    d = shapely.distance(region.boundary, shapely.points(X[innen], Y[innen]))
    ideal = -np.minimum(d / TAN30, 4.0)
    abw = h.H[innen] - ideal
    kern = d > 0.15
    assert np.percentile(abw[kern], 99) < 0.1       # 99 % innerhalb 0.1 mm
    assert abw[kern].max() < 0.3
    assert abw[kern].min() > -0.15                  # nirgends deutlich zu tief


def test_mittellinien_rechteck():
    from hirch_cam.ops.mittellinie import mittellinien
    r = Polygon([(100, 20), (150, 20), (150, 50), (100, 50)])
    ketten = mittellinien(r, 0.15)
    assert len(ketten) == 5                      # 4 Diagonalen + Mittellinie
    enden = sorted({(round(k[i][0]), round(k[i][1])) for k in ketten for i in (0, -1)})
    assert (115, 35) in enden and (135, 35) in enden
    assert sum(1 for e in enden if e in {(100, 20), (150, 20), (100, 50), (150, 50)}) == 4


def test_vcarve_gravierstichel_mit_flacher_spitze(tmp_path):
    """36° mit 0,2 mm Spitze: auf der Mittellinie (Randabstand − 0,1) / tan 18°
    tief, an den Flanken nie tiefer als Randabstand / tan 18° (kein Überschnitt)."""
    from shapely import vectorized

    from hirch_cam.simulation import Hoehenfeld
    tan18 = math.tan(math.radians(18))
    tri = [(10, 10), (30, 10), (20, 14)]
    auftrag = {
        "werkzeuge": {"G": {"typ": "v", "durchmesser": 3, "v_winkel": 36, "spitze": 0.2,
                            "vorschub": 600, "eintauchen": 200}},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 18},
        "ausgabe": str(tmp_path / "g.tap"),
        "bearbeitungen": [{"id": "v1", "art": "vcarve", "name": "V", "werkzeug": "G", "max_tiefe": 3,
                           "objekte": [{"punkte": tri, "geschlossen": True}]}],
    }
    erg = engine.rechnen(auftrag)
    assert erg["ok"], erg
    region = Polygon(tri)
    h = Hoehenfeld(5, 5, 35, 20, 0.02)
    h.fraesen(tap_mm.read(open(erg["dateien"][0], newline="").read()), "v", 3, 36, spitze=0.2)
    X, Y = h.koordinaten()
    innen = vectorized.contains(region, X, Y)
    d = shapely.distance(region.boundary, shapely.points(X[innen], Y[innen]))
    flanke = -np.minimum(d / tan18, 3.0)
    assert np.percentile(h.H[innen] - flanke, 1) > -0.08          # nirgends überschnitten
    assert h.H[~vectorized.contains(region.buffer(0.03), X, Y)].min() > -1e-6
    assert h.H.min() == pytest.approx(-3.0, abs=0.01)
    # an der schmalen Spitze ist die Rille um den Spitzenradius flacher
    assert np.percentile((h.H[innen] - flanke)[d < 0.4], 99) > 0.05


def _schnitt(tp, winkel, spitze):
    """Fläche, die ein V-Fräser mit den Bahnen oben am Material wegnimmt."""
    from shapely.geometry import Point
    from shapely.ops import unary_union
    t = math.tan(math.radians(winkel / 2))
    geo, vor = [], None
    for m in tp.moves:
        if vor is not None and m.z < 0 and vor.z < 0:
            n = max(1, int(math.dist((vor.x, vor.y), (m.x, m.y)) / 0.02))
            for i in range(n + 1):
                f = i / n
                z = vor.z + (m.z - vor.z) * f
                geo.append(Point(vor.x + (m.x - vor.x) * f, vor.y + (m.y - vor.y) * f).buffer(spitze / 2 - z * t, 8))
        vor = m
    return unary_union(geo)


def test_vcarve_zustellung_und_aufdicken():
    from hirch_cam.ops.vcarve import vcarve
    w = {"v_winkel": 36, "durchmesser": 3, "spitze": 0.2, "vorschub": 600, "eintauchen": 200}
    streifen = [(0, 0), (20, 0), (20, 2), (0, 2)]
    # Zustellung 0,2 bei 0,6 mm: drei Ebenen, zuletzt die volle Tiefe; Form unverändert
    tp, warn, _ = vcarve([(streifen, True)], w, {"max_tiefe": 0.6, "zustellung": 0.2})
    tiefen = sorted({round(m.z, 3) for m in tp.moves if m.z < 0})
    assert tiefen == [-0.6, -0.4, -0.2]
    eins, _w, _ = vcarve([(streifen, True)], w, {"max_tiefe": 0.6})
    a, b = _schnitt(tp, 36, 0.2), _schnitt(eins, 36, 0.2)
    assert a.symmetric_difference(b).area < 0.01
    assert abs(a.area - 40) < 0.2
    # die Randbahn (Schlichtgang an der Kante) kommt zuletzt
    letzte = [m for m in tp.moves if m.z < 0][-1]
    assert abs(letzte.z + 0.6) < 1e-6
    # Aufdicken: ringsum 0,1 mm grösser bzw. 0,2 mm kleiner
    for dick, soll in ((0.1, (-0.1, -0.1, 20.1, 2.1)), (-0.2, (0.2, 0.2, 19.8, 1.8))):
        tp, warn, _ = vcarve([(streifen, True)], w, {"max_tiefe": 5, "aufdicken": dick})
        gr = _schnitt(tp, 36, 0.2).bounds
        assert all(abs(g - s) < 0.02 for g, s in zip(gr, soll)), (dick, gr)
    # zu stark verdünnt: nichts bleibt, Warnung
    tp, warn, _ = vcarve([(streifen, True)], w, {"max_tiefe": 5, "aufdicken": -1.5})
    assert not tp.moves and warn


def test_langloch_ohne_faecher_an_den_rundungen():
    """Langloch 4 mm breit (mit Flachbereich bei 3 mm Tiefe), Enden aus kurzen Segmenten
    wie aus einer DXF: früher lief von der Mitte zu jedem Punkt der Rundung ein Ast, jeder mit
    eigenem Hub. Was die Randbahn um den Flachbereich ohnehin fräst, fällt jetzt weg."""
    from hirch_cam.ops.vcarve import vcarve
    pts = [(28, 10), (52, 10)]
    pts += [(52 + 2 * math.sin(math.radians(a)), 12 - 2 * math.cos(math.radians(a))) for a in range(2, 180, 2)]
    pts += [(52, 14), (28, 14)]
    pts += [(28 - 2 * math.sin(math.radians(a)), 12 + 2 * math.cos(math.radians(a))) for a in range(2, 180, 2)]
    w = {"v_winkel": 60, "durchmesser": 12, "spitze": 0, "vorschub": 1000, "eintauchen": 300}
    tp, _w, _x = vcarve([(pts, True)], w, {"max_tiefe": 3})
    huebe = sum(1 for a, b in zip(tp.moves, tp.moves[1:]) if a.z >= 0 > b.z)
    assert huebe <= 3
    # nichts tiefer als erlaubt und nichts ausserhalb
    form = Polygon(pts)
    tan_h = math.tan(math.radians(30))
    for m in tp.moves:
        if m.z < 0:
            assert -m.z <= form.boundary.distance(Point(m.x, m.y)) / tan_h + 0.02


def test_rueckweg_ueber_gefraeste_zuege_statt_abheben():
    """Ein «E» aus 1 mm breiten Strichen: Die Mittellinie ist ein Baum mit vielen Enden
    (Ecken). Früher hob der Fräser nach jedem Ende ab (7 Hübe); jetzt fährt er auf den
    schon gefrästen Zügen zurück und hebt nur am Schluss ab – ohne irgendwo tiefer zu
    gehen, als die Form erlaubt."""
    from hirch_cam.ops.vcarve import vcarve
    E = [(0, 0), (6, 0), (6, 1), (1, 1), (1, 4.5), (5, 4.5), (5, 5.5), (1, 5.5), (1, 9), (6, 9),
         (6, 10), (0, 10)]
    w = {"v_winkel": 60, "durchmesser": 12, "spitze": 0, "vorschub": 1000, "eintauchen": 300}
    tp, _w, _x = vcarve([(E, True)], w, {"max_tiefe": 3})
    abheben = sum(1 for a, b in zip(tp.moves, tp.moves[1:]) if a.z <= 0.01 and b.z > 0.1)
    assert abheben == 1
    form = Polygon(E)
    tan_h = math.tan(math.radians(30))
    for a, b in zip(tp.moves, tp.moves[1:]):
        for t in (0.0, 0.25, 0.5, 0.75, 1.0):
            x, y, z = (a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t)
            if z < 0:
                assert -z <= form.boundary.distance(Point(x, y)) / tan_h + 0.02


def test_2d_vorschau_zeigt_auch_die_aeste_in_die_ecken():
    """Ein Ast in eine Ecke endet an der Oberfläche (Z = 0). Die 2D-Vorschau hat diesen
    letzten Schnitt früher weggelassen; einzelne Restpunkte erschienen dann als Kreise."""
    from hirch_cam.ops.vcarve import vcarve
    T = [(-1.1, -.3), (-.2, -.3), (-.2, -2), (.2, -2), (.2, -.3), (1.1, -.3), (1.1, .3), (.2, .3),
         (.2, 1.5), (-.2, 1.5), (-.2, .3), (-1.1, .3)]
    w = {"v_winkel": 15, "durchmesser": 3, "spitze": 0.2, "vorschub": 1000, "eintauchen": 300}
    tp, _w, _x = vcarve([(T, True)], w, {"max_tiefe": 0.6})
    zuege = engine._bahnen_2d(tp)
    assert not [z for z in zuege if len(z) == 2]
    # die Anzeige dünnt die Züge aus (0,03 mm): jeder Schnitt muss nahe an einer gezeichneten Linie liegen
    from shapely.geometry import LineString, Point
    linien = [LineString(list(zip(z[0::2], z[1::2]))) for z in zuege if len(z) >= 4]
    for a, b in zip(tp.moves, tp.moves[1:]):
        if b.rapid or (a.z >= 0 and b.z >= 0) or math.hypot(b.x - a.x, b.y - a.y) < 1e-6:
            continue
        for q in ((a.x, a.y), (b.x, b.y), ((a.x + b.x) / 2, (a.y + b.y) / 2)):
            assert min(l.distance(Point(q)) for l in linien) <= 0.031
