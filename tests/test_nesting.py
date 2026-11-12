"""Vervielfältigen und Verschachteln (Nesting) und Platten (mehrere Seiten)."""
import json
import math

import pytest

from test_dokument import braucht_inkex

shapely = pytest.importorskip("shapely")
pytest.importorskip("scipy")

from shapely import affinity  # noqa: E402
from shapely.geometry import Point, Polygon, box  # noqa: E402

from hirch_cam import nesting  # noqa: E402


def _lagen_pruefen(formen, erg, breite, hoehe, abstand, rand):
    je_platte = {}
    for lg in erg["lagen"]:
        g = formen[lg["form"]]["voll"]
        g = affinity.translate(affinity.rotate(g, lg["winkel"], origin=(0, 0)), lg["dx"], lg["dy"])
        je_platte.setdefault(lg["platte"], []).append(g)
    for gs in je_platte.values():
        for i, g in enumerate(gs):
            x0, y0, x1, y1 = g.bounds
            assert x0 >= rand - 1e-6 and y0 >= rand - 1e-6
            assert x1 <= breite - rand + 1e-6 and y1 <= hoehe - rand + 1e-6
            for h in gs[:i]:
                assert g.distance(h) >= abstand - 1e-6
    return je_platte


def test_anordnen_ohne_ueberlappung_und_weitere_platten():
    L = Polygon([(0, 0), (120, 0), (120, 30), (30, 30), (30, 90), (0, 90)])
    formen = [{"voll": box(0, 0, 100, 60), "material": box(0, 0, 100, 60), "anzahl": 20},
              {"voll": L, "material": L, "anzahl": 8}]
    for drehen in ("keine", "90", "frei"):
        erg = nesting.anordnen(formen, 600, 400, abstand=8, rand=10, drehen=drehen)
        assert erg["nicht"] == 0 and len(erg["lagen"]) == 28
        pl = _lagen_pruefen(formen, erg, 600, 400, 8, 10)
        assert erg["platten"] == len(pl) >= 2
        if drehen == "keine":
            assert all(lg["winkel"] == 0 for lg in erg["lagen"])


def test_anordnen_kleine_teile_in_loecher():
    ring = Point(0, 0).buffer(60).difference(Point(0, 0).buffer(45))
    klein = Point(0, 0).buffer(10)
    formen = [{"voll": Point(0, 0).buffer(60), "material": ring, "anzahl": 1},
              {"voll": klein, "material": klein, "anzahl": 1}]
    # Platte gerade gross genug für den Ring: der kleine passt nur ins Loch
    erg = nesting.anordnen(formen, 132, 132, abstand=6, rand=4, drehen="keine", in_loechern=True)
    assert erg["platten"] == 1 and erg["nicht"] == 0
    erg2 = nesting.anordnen(formen, 132, 132, abstand=6, rand=4, drehen="keine", in_loechern=False)
    assert erg2["platten"] == 2


def test_zu_grosses_teil_bleibt():
    formen = [{"voll": box(0, 0, 500, 50), "material": box(0, 0, 500, 50), "anzahl": 1}]
    erg = nesting.anordnen(formen, 300, 200, abstand=5, rand=5)
    assert erg["nicht"] == 1 and not erg["lagen"]


def _svg(tmp_path, inhalt, zweiseitig=False, seiten=""):
    bs = [{"id": "k1", "art": "kontur", "tiefe": 18.2, "durch": True, "zustellung": 3, "farbe": "#0000ff"},
          {"id": "t1", "art": "tasche", "tiefe": 2, "zustellung": 1, "farbe": "#00a000"}]
    e = {"bearbeitungen": bs}
    if zweiseitig:
        e["zweiseitig"] = {"an": True, "achse": "x", "passloecher": True}
        bs.append({"id": "t2", "art": "tasche", "tiefe": 2, "zustellung": 1, "farbe": "#00c000",
                   "auf_seite": 2})
    attr = json.dumps(e).replace('"', "&quot;")
    p = tmp_path / "n.svg"
    p.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
        'xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
        'xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd" '
        'width="300mm" height="200mm" viewBox="0 0 300 200" hirch:materialdicke="18" '
        'hirch:einstellungen="%s"><sodipodi:namedview id="nv">%s</sodipodi:namedview>'
        '<g inkscape:groupmode="layer" id="ebene">%s</g></svg>' % (attr, seiten, inhalt), encoding="utf-8")
    return str(p)


def _apply(m, x, y):
    a, b, c, d, e, f = m
    return a * x + c * y + e, b * x + d * y + f


@braucht_inkex
def test_rechnen_legt_seiten_an_und_bewegt(tmp_path):
    svg = _svg(tmp_path, '<g id="A"><rect id="ra" x="10" y="10" width="80" height="50" '
                         'style="fill:none;stroke:#0000ff"/></g>')
    info = nesting.teile_info(svg)
    assert [t["id"] for t in info["teile"]] == ["A"]
    erg = nesting.rechnen(svg, {"teile": [{"id": "A", "anzahl": 14}], "drehen": "keine",
                                "abstand": 9, "rand": 10, "original_bleibt": False})
    assert erg["ok"] and erg["gelegt"] == 14 and erg["platten"] == 2
    assert len(erg["neue_seiten"]) == 1
    x, y, w, h = erg["neue_seiten"][0]
    assert x >= 300 and w == pytest.approx(300) and h == pytest.approx(200)
    for b in erg["bewegen"]:
        x0, y0 = _apply(b["matrix"], 10, 10)
        x1, y1 = _apply(b["matrix"], 90, 60)
        links = 0 if b["platte"] == 0 else x
        assert links + 10 - 1e-6 <= min(x0, x1) and max(x0, x1) <= links + 290 + 1e-6
        assert 10 - 1e-6 <= min(y0, y1) and max(y0, y1) <= 190 + 1e-6
    assert sorted(b["kopie"] for b in erg["bewegen"]) == list(range(14))


@braucht_inkex
def test_zweiseitig_unterseite_wandert_gespiegelt_mit(tmp_path):
    seiten = ('<inkscape:page id="p1" x="0" y="0" width="300" height="200"/>'
              '<inkscape:page id="p2" x="320" y="0" width="300" height="200"/>')
    # Teil oben links auf Seite 1; Tasche der Unterseite liegt gewendet (um X) auf Seite 2
    inhalt = ('<rect id="A" x="10" y="10" width="80" height="50" style="fill:none;stroke:#0000ff"/>'
              '<rect id="U" x="340" y="150" width="20" height="20" style="fill:none;stroke:#00c000"/>')
    svg = _svg(tmp_path, inhalt, zweiseitig=True, seiten=seiten)
    erg = nesting.rechnen(svg, {"teile": [{"id": "A", "anzahl": 3}], "drehen": "90", "abstand": 9,
                                "rand": 10, "original_bleibt": False})
    assert erg["ok"] and erg["gelegt"] == 3
    teil = [b for b in erg["bewegen"] if b["id"] == "A"]
    unten = [b for b in erg["bewegen"] if b["id"] == "U"]
    assert len(unten) == 3
    for bt, bu in zip(teil, unten):
        # Mitte der Tasche unten (Seite 2, Benutzereinheiten = mm, Y unten)
        ux, uy = _apply(bu["matrix"], 350, 160)
        # dieselbe Stelle oben: auf Seite 1 gespiegelt (um X: y → 200 − y)
        ox, oy = ux - 320, 200 - uy
        # Vorher: (350,160) unten ↔ oben (30, 40) – innerhalb des Teils (10…90, 10…60)
        tx, ty = _apply(bt["matrix"], 30, 40)
        assert (ox, oy) == pytest.approx((tx, ty), abs=1e-6)
    # Passlöcher (rand 15, Mitte Höhe) bleiben frei
    for b in teil:
        pts = [_apply(b["matrix"], x, y) for x, y in ((10, 10), (90, 10), (90, 60), (10, 60))]
        g = Polygon(pts)
        for px, py in ((15, 100), (285, 100)):
            assert g.distance(Point(px, py)) > 3


@braucht_inkex
def test_platten_werden_einzeln_gelesen(tmp_path):
    import inkex
    from hirch_cam import svg_lesen
    seiten = ('<inkscape:page id="p1" x="0" y="0" width="300" height="200"/>'
              '<inkscape:page id="p2" x="320" y="0" width="300" height="200"/>')
    inhalt = ('<rect id="a" x="10" y="10" width="80" height="50" style="fill:none;stroke:#0000ff"/>'
              '<rect id="b" x="330" y="10" width="40" height="40" style="fill:none;stroke:#0000ff"/>'
              '<rect id="c" x="400" y="100" width="40" height="40" style="fill:none;stroke:#0000ff"/>')
    svg = inkex.load_svg(_svg(tmp_path, inhalt, seiten=seiten)).getroot()
    assert svg_lesen.platten(svg) == 2
    i1 = svg_lesen.lesen(svg, platte=1)
    i2 = svg_lesen.lesen(svg, platte=2)
    assert [e["id"] for e in i1["elemente"]] == ["a"]
    assert [e["id"] for e in i2["elemente"]] == ["b", "c"]
    assert i2["platten"] == 2 and i2["draussen"] == 0
    xs = [x for e in i2["elemente"] for ln in e["linien"] for x, _y in ln["punkte"]]
    assert min(xs) == pytest.approx(10) and max(xs) == pytest.approx(120)


@braucht_inkex
def test_original_bleibt_liegen(tmp_path):
    svg = _svg(tmp_path, '<g id="A"><rect id="ra" x="100" y="70" width="80" height="50" '
                         'style="fill:none;stroke:#0000ff"/></g>')
    erg = nesting.rechnen(svg, {"teile": [{"id": "A", "anzahl": 5}], "drehen": "keine",
                                "abstand": 9, "rand": 10})
    assert erg["gelegt"] == 4 and erg["teile_gesamt"] == 5
    assert sorted(b["kopie"] for b in erg["bewegen"]) == [1, 2, 3, 4]
    orig = Polygon([(100, 70), (180, 70), (180, 120), (100, 120)])
    for b in erg["bewegen"]:
        pts = [_apply(b["matrix"], x, y) for x, y in ((100, 70), (180, 70), (180, 120), (100, 120))]
        assert Polygon(pts).distance(orig) >= 9 - 1e-6
    assert any(t["art"] == "original" for t in erg["bild"]["teile"])


def test_raster_reihen_und_spalten():
    r = box(0, 0, 50, 30)
    formen = [{"voll": r, "material": r, "anzahl": 12}]
    erg = nesting.anordnen(formen, 300, 200, abstand=10, abstand_y=5, rand=10, drehen="keine",
                           art="raster", richtung="x", ecke="ol")
    assert erg["nicht"] == 0
    xs = sorted({round(lg["dx"], 3) for lg in erg["lagen"]})
    ys = sorted({round(lg["dy"], 3) for lg in erg["lagen"]})
    assert len(xs) == 4 and len(ys) == 3                 # 4 je Reihe, 3 Reihen
    assert all(b - a >= 60 - 1e-6 for a, b in zip(xs, xs[1:]))
    assert all(b - a >= 35 - 1e-6 for a, b in zip(ys, ys[1:]))
    assert max(ys) + 30 > 150                            # oben angefangen
    _lagen_pruefen(formen, erg, 300, 200, 5, 10)


def test_platte_fuellen():
    r = box(0, 0, 50, 30)
    erg = nesting.anordnen([{"voll": r, "material": r, "anzahl": 9999}], 300, 200, abstand=10,
                           rand=10, drehen="keine", art="raster", platten_max=1)
    assert erg["platten"] == 1 and len(erg["lagen"]) == 4 * 4
