"""Schriften einlesen: Einlinien-TTF (Hin und zurück), Umriss-TTF, SVG-Schrift, Hershey."""
import math

import pytest

from hirch_cam import einlinien, schriftimport


def _ttf(pfad, glyphen, upm=1000):
    """Kleine TrueType-Schrift bauen: glyphen = {zeichen: (vorschub, [umriss, …])}."""
    fb_mod = pytest.importorskip("fontTools.fontBuilder")
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    namen = [".notdef"] + ["g%d" % i for i in range(len(glyphen))]
    fb = fb_mod.FontBuilder(upm, isTTF=True)
    fb.setupGlyphOrder(namen)
    fb.setupCharacterMap({ord(ch): "g%d" % i for i, ch in enumerate(glyphen)})
    gl = {".notdef": TTGlyphPen(None).glyph()}
    metrik = {".notdef": (500, 0)}
    for i, (ch, (adv, umrisse)) in enumerate(glyphen.items()):
        pen = TTGlyphPen(None)
        for u in umrisse:
            pen.moveTo(u[0])
            for p in u[1:]:
                pen.lineTo(p)
            pen.closePath()
        gl["g%d" % i] = pen.glyph()
        metrik["g%d" % i] = (adv, min((p[0] for u in umrisse for p in u), default=0))
    fb.setupGlyf(gl)
    fb.setupHorizontalMetrics(metrik)
    fb.setupHorizontalHeader(ascent=800, descent=-200)
    fb.setupNameTable({"familyName": "Test Einlinie", "styleName": "Regular"})
    fb.setupOS2(sCapHeight=700, sxHeight=500)
    fb.setupPost()
    fb.save(str(pfad))


@pytest.fixture
def bib(tmp_path, monkeypatch):
    monkeypatch.setenv("HIRCH_SCHRIFTEN", str(tmp_path / "bib"))
    einlinien.neu_laden()
    yield tmp_path
    einlinien.neu_laden()


def _segmente(striche):
    """Ungerichtete Strecken eines Zeichens (gerundet), zum Vergleichen."""
    aus = set()
    for s in striche:
        pts = [(round(s[i]), round(s[i + 1])) for i in range(0, len(s), 2)]
        for a, b in zip(pts, pts[1:]):
            aus.add(tuple(sorted((a, b))))
    return aus


def test_einlinien_ttf_hin_und_zurueck(bib):
    # «H» als ein einziger Umriss, der jeden Strich hin und zurück fährt;
    # der Rückweg hält in der Mitte, der Hinweg nicht
    h = [(100, 0), (100, 700), (100, 350), (500, 350), (500, 700), (500, 0), (500, 350), (100, 350)]
    # «O» als Achteck, einmal herum und wieder zurück
    acht = [(300 + 200 * math.cos(k * math.pi / 4), 350 + 300 * math.sin(k * math.pi / 4)) for k in range(8)]
    o = [(round(x), round(y)) for x, y in acht + [acht[0]] + acht[::-1][:-1]]
    _ttf(bib / "einlinie.ttf", {"H": (600, [h]), "O": (600, [o]), " ": (300, [])})
    s = schriftimport.einlesen(str(bib / "einlinie.ttf"))
    assert s["art"] == "einlinie" and s["cap"] == 700 and s["xh"] == 500
    assert s["name"] == "Test Einlinie"
    # H: drei Striche ohne doppelte Strecken
    assert _segmente(s["g"]["H"][1]) == {((100, 0), (100, 350)), ((100, 350), (100, 700)),
                                         ((100, 350), (500, 350)), ((500, 0), (500, 350)),
                                         ((500, 350), (500, 700))}
    laenge = sum(math.dist(*seg) for seg in _segmente(s["g"]["H"][1]))
    assert laenge == pytest.approx(700 + 700 + 400)
    # O: ein geschlossener Ring, jede Strecke nur einmal
    o_str = s["g"]["O"][1]
    assert len(o_str) == 1 and len(_segmente(o_str)) == 8
    assert o_str[0][:2] == o_str[0][-2:]


def test_umriss_ttf_bleibt_umriss(bib):
    quadrat = [(100, 0), (100, 700), (500, 700), (500, 0)]
    _ttf(bib / "umriss.ttf", {"I": (600, [quadrat])})
    s = schriftimport.einlesen(str(bib / "umriss.ttf"), "Klotz")
    assert s["art"] == "umriss" and s["name"] == "Klotz"
    (ring,) = s["g"]["I"][1]
    assert ring[:2] == ring[-2:] and len(ring) == 10


def test_bibliothek_und_gravur(bib):
    h = [(100, 0), (100, 700), (100, 350), (500, 350), (500, 700), (500, 0), (500, 350), (100, 350)]
    _ttf(bib / "einlinie.ttf", {"H": (600, [h]), " ": (300, [])})
    erg = schriftimport.main(["import", str(bib / "einlinie.ttf"), "Norm Test 1L"])
    assert erg == 0
    assert "Norm Test 1L" in schriftimport.bibliothek()
    einlinien.neu_laden()
    assert "Norm Test 1L" in einlinien.schriftnamen()
    linien = einlinien.text_linien("HH", "Norm Test 1L", versalhoehe=7.0, optisch=False)
    ys = [p[1] for l in linien for p in l]
    assert max(ys) - min(ys) == pytest.approx(7.0)
    assert len(linien) == 6                               # 2 × 3 Striche (Balken gerade verbunden)
    assert schriftimport.entfernen("Norm Test 1L")
    assert schriftimport.bibliothek() == {}


def test_svg_schrift(bib):
    from test_dokument import _inkex
    if not _inkex():
        pytest.skip("inkex nicht gefunden")
    svg = bib / "s.svg"
    svg.write_text("""<svg xmlns="http://www.w3.org/2000/svg"><defs><font horiz-adv-x="400">
      <font-face font-family="Strich" units-per-em="1000" cap-height="700" x-height="500"/>
      <glyph unicode="L" horiz-adv-x="450" d="M 100 700 L 100 0 L 400 0"/>
      <glyph unicode="o" d="M 100 0 L 300 0 L 300 400 L 100 400 Z"/>
    </font></defs></svg>""")
    s = schriftimport.einlesen(str(svg))
    assert s["name"] == "Strich" and s["cap"] == 700
    assert s["g"]["L"][0] == 450 and s["g"]["o"][0] == 400
    assert _segmente(s["g"]["L"][1]) == {((100, 0), (100, 700)), ((100, 0), (400, 0))}


def test_hershey_jhf(bib):
    # Zeichen 32 (Leerzeichen) und 33 «!» im Hershey-Format
    jhf = bib / "mini.jhf"
    jhf.write_text("12345  1JZ\n12346  9MWRFRT RRYQZR[SZRY\n")
    s = schriftimport.einlesen(str(jhf))
    assert s["g"][" "][0] == 16
    striche = s["g"]["!"][1]
    assert len(striche) == 2
    # senkrechter Strich: von y = 9+12 = 21 nach 9-2 = 7 … Grundlinie bei 9
    assert striche[0][1] == 21 and striche[0][3] == 7


def test_unbekanntes_format(bib):
    (bib / "x.doc").write_text("nix")
    with pytest.raises(RuntimeError):
        schriftimport.einlesen(str(bib / "x.doc"))
    assert schriftimport.main(["import", str(bib / "x.doc")]) == 1


def test_zeichentafel_lernen(bib):
    """Zeichentafel mit einer bekannten Schrift schreiben, als SVG ablegen und
    daraus wieder lernen: die Zeichen müssen dieselbe Form haben."""
    from test_dokument import _inkex
    if not _inkex():
        pytest.skip("inkex nicht gefunden")
    linien = einlinien.text_linien(schriftimport.tafel_text(), "Hershey Sans", versalhoehe=10.0,
                                   zeilenabstand=1.8, optisch=False, ausrichtung="links")
    d = " ".join("M " + " L ".join("%.4f,%.4f" % (x, 150 - y) for x, y in l) for l in linien)
    (bib / "tafel.svg").write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" width="300mm" height="200mm" viewBox="0 0 300 200">'
        '<path d="%s" style="fill:none;stroke:#000"/></svg>' % d)
    s = schriftimport.einlesen(str(bib / "tafel.svg"), "Gelernt")
    assert s["name"] == "Gelernt" and s["cap"] == 700
    orig = einlinien.schriften()["Hershey Sans"]
    k = 700.0 / orig["cap"]

    def form(striche, f):
        pts = [(st[i] * f, st[i + 1] * f) for st in striche for i in range(0, len(st), 2)]
        x0 = min(p[0] for p in pts)
        segs = set()
        for st in striche:
            q = [(round((st[i] * f - x0) / 3), round(st[i + 1] * f / 3)) for i in range(0, len(st), 2)]
            segs |= {tuple(sorted(ab)) for ab in zip(q, q[1:]) if ab[0] != ab[1]}
        return segs

    for ch in "AHgjpqyiö0%&@\"'=,.Ø$_":
        a = form(orig["g"][ch][1], k)
        b = form(s["g"][ch][1], 1.0)
        assert len(a ^ b) <= len(a) * 0.1, ch
    assert s["g"]["y"][1] and min(st[i] for st in s["g"]["y"][1] for i in range(1, len(st), 2)) < -100


def test_zeichentafel_aus_dxf(bib):
    """Wie aus einem anderen CAM-Programm exportiert: jede Strecke eine eigene DXF-Linie."""
    from test_dokument import _inkex
    if not _inkex():
        pytest.skip("inkex nicht gefunden")
    import os
    import subprocess
    import sys
    import inkex
    linien = einlinien.text_linien(schriftimport.tafel_text(), "Hershey Sans", versalhoehe=8.0,
                                   zeilenabstand=2.0, optisch=False, ausrichtung="links")
    d = " ".join("M " + " L ".join("%.4f,%.4f" % (x, 150 - y) for x, y in l) for l in linien)
    (bib / "t.svg").write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" width="300mm" height="200mm" viewBox="0 0 300 200">'
        '<path d="%s" style="fill:none;stroke:#000"/></svg>' % d)
    ext = os.path.dirname(os.path.dirname(os.path.abspath(inkex.__file__)))
    r = subprocess.run([sys.executable, os.path.join(ext, "dxf_outlines.py"), str(bib / "t.svg")],
                       capture_output=True, cwd=ext)
    (bib / "t.dxf").write_bytes(r.stdout)
    s = schriftimport.einlesen(str(bib / "t.dxf"), "Aus DXF")
    assert len(s["g"]["O"][1]) == 1                      # Einzelstrecken wieder verbunden
    assert s["g"]["O"][1][0][:2] == s["g"]["O"][1][0][-2:]
    assert len(s["g"]["A"][1]) <= 3


def test_gravurtext_fuer_den_dialog(bib):
    from hirch_cam import gravurtext
    r = gravurtext.setzen({"text": "HÄ\nx", "versalhoehe": 10, "schrift": "Gibt es nicht"})
    assert r["ok"] and r["schrift"] == einlinien.schriftnamen()[0]
    xs = [s[i] for s in r["linien"] for i in range(0, len(s), 2)]
    ys = [s[i] for s in r["linien"] for i in range(1, len(s), 2)]
    assert min(xs) == pytest.approx(-max(xs), abs=1e-3)       # Mitte bei 0
    assert r["box"][0] == pytest.approx(min(xs)) and r["box"][3] == pytest.approx(max(ys))
    links = gravurtext.setzen({"text": "HÄ\nx", "versalhoehe": 10, "ausrichtung": "links"})
    rechts = gravurtext.setzen({"text": "HÄ\nx", "versalhoehe": 10, "ausrichtung": "rechts"})
    assert links["box"][0] == pytest.approx(0) and rechts["box"][2] == pytest.approx(0)
    assert links["box"][3] > 9.9                              # Grundlinie bei 0: «H» bis 10 mm
    assert r["breite"] == pytest.approx(max(xs) - min(xs), abs=1e-2)
    leer = gravurtext.setzen({"text": "  "})
    assert leer["linien"] == [] and leer["breite"] == 0
    namen = [s["name"] for s in gravurtext.schriftliste()]
    assert "Hershey Sans" in namen


def test_kerning_bei_festen_breiten(bib):
    h = [(100, 0), (100, 700), (100, 350), (500, 350), (500, 700), (500, 0), (500, 350), (100, 350)]
    _ttf(bib / "e.ttf", {"H": (600, [h]), " ": (300, [])})
    s = schriftimport.einlesen(str(bib / "e.ttf"), "Kern Test")
    s["kern"] = {"HH": -200.0}
    schriftimport.ablegen(s)
    einlinien.neu_laden()
    linien = einlinien.text_linien("HH", "Kern Test", versalhoehe=7.0, optisch=False, ausrichtung="links")
    xs = sorted({round(p[0], 3) for l in linien for p in l})
    assert xs == pytest.approx([0, 4, 8], abs=1e-3)      # ohne Kerning wäre das 2. «H» bei 6..10


def test_text_in_einlinienschrift_wird_einmal_gefraest(bib):
    """In Pfade umgewandelter Text (hirch:text): Hin-und-zurück-Umrisse → Linien,
    Umrisse normaler Schriften bleiben geschlossen."""
    from test_dokument import _inkex
    if not _inkex():
        pytest.skip("inkex nicht gefunden")
    import inkex
    from hirch_cam import svg_lesen
    # «L» hin und zurück, dazu ein echtes Quadrat (Umrissschrift)
    svg = inkex.load_svg(
        b'<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
        b'width="100mm" height="100mm" viewBox="0 0 100 100">'
        b'<g hirch:text="1"><path style="fill:none;stroke:#ff8c00" '
        b'd="M 10,10 L 10,50 L 10.0000004,10.0000001 L 30,10 Z M 50,50 L 60,50 L 60,60 L 50,60 Z"/></g>'
        b'</svg>').getroot()
    info = svg_lesen.lesen(svg)
    (el,) = info["elemente"]
    offen = [l for l in el["linien"] if not l["geschlossen"]]
    zu = [l for l in el["linien"] if l["geschlossen"]]
    assert len(zu) == 1 and len(offen) >= 1
    laenge = sum(math.dist(a, b) for l in offen for a, b in zip(l["punkte"], l["punkte"][1:]))
    assert laenge == pytest.approx(40 + 20, abs=0.01)         # jeder Strich nur einmal


def test_ttf_fuer_das_textwerkzeug(bib, monkeypatch):
    pytest.importorskip("fontTools.feaLib")
    from fontTools.ttLib import TTFont
    from hirch_cam import schrift_ttf
    monkeypatch.setenv("HIRCH_SCHRIFT_TTF", str(bib / "fonts"))
    s = dict(einlinien.schriften()["Hershey Sans"], name="Hershey Sans", kern={"AV": -40})
    datei = schrift_ttf.bauen(s, str(bib / "fonts" / "t.ttf"))
    f = TTFont(datei)
    assert f["name"].getDebugName(1) == "Hershey Sans"
    assert f["OS/2"].sCapHeight == 1000 and "GPOS" in f
    # «H»: jeder Umriss fährt hin und zurück → Fläche null
    from fontTools.pens.areaPen import AreaPen
    pen = AreaPen(f.getGlyphSet())
    f.getGlyphSet()[f.getBestCmap()[ord("H")]].draw(pen)
    assert abs(pen.value) < 1e-6
    neu = schrift_ttf.abgleichen()
    assert any(d.endswith("hirch-hershey-sans.ttf") for d in neu)
    assert schrift_ttf.abgleichen() == []                   # unverändert: nichts neu bauen
