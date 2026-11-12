"""Provisorischer Nullpunkt (Peter, 29.10.2026): ein oranges Kreuz in der Zeichnung (`hirch:nullpunkt`) ist für
seine Seite X0 Y0 – z. B. an einem im Vorjahr gravierten Namen. Es wird nie gefräst, gilt nur auf seiner Seite,
und das Kreuz auf der Zeichenfläche (`analyse["nullpunkte"]`) liegt am selben Punkt wie X0 Y0 im G-Code."""
import pytest

from test_nullpunkt_matrix import (LOCH, LOCH2, P2_X, TIEFE, _datei, _inkex, _loecher, _pruefen, _soll_null,
                                   _zeichnung)

pytestmark = pytest.mark.skipif(not _inkex(), reason="inkex nicht gefunden")

KREUZ = (52.4, 31.8)        # mm von unten links der Seite


def _mit_kreuz(svg, h, x0=0.0, wo=KREUZ):
    """Kreuz wie es die Oberfläche setzt: Gruppe mit hirch:nullpunkt, Kreis und zwei Linien um den Punkt."""
    x, y = x0 + wo[0], h - wo[1]
    kreuz = ('<g hirch:nullpunkt="provisorisch" id="np1">'
             '<circle cx="%g" cy="%g" r="2" style="fill:none;stroke:#e66100"/>'
             '<path d="M %g %g H %g M %g %g V %g" style="fill:none;stroke:#e66100"/></g>' % (
                 x, y, x - 4, y, x + 4, x, y - 4, y + 4))
    text = open(svg, encoding="utf-8").read().replace("</g></svg>", kreuz + "</g></svg>")
    open(svg, "w", encoding="utf-8").write(text)


def test_seite_1_kreuz_ist_x0_y0(tmp_path):
    from hirch_cam import dokument
    svg, (b, h) = _zeichnung(tmp_path, "ul", 5.0, -3.0)          # Versatz gilt dann nicht
    _mit_kreuz(svg, h)
    erg = dokument.rechnen(svg, str(tmp_path / "aus"), "t")
    assert erg["ok"], erg.get("warnungen")
    _pruefen(_loecher(_datei(erg)), (LOCH[0] - KREUZ[0], LOCH[1] - KREUZ[1]), -TIEFE)
    assert erg["nullpunkt_provisorisch"]["1"] == pytest.approx({"x": KREUZ[0], "y": KREUZ[1]})
    an = dokument.analyse(svg, 1)
    kreuz = [n for n in an["nullpunkte"] if n["platte"] == 1 and n["seite"] == 1][0]
    assert kreuz["provisorisch"]
    assert (kreuz["x"], kreuz["y"]) == pytest.approx((KREUZ[0], h - KREUZ[1]))


def test_kreuz_wird_nie_gefraest(tmp_path):
    from hirch_cam import svg_lesen
    import inkex
    svg, (b, h) = _zeichnung(tmp_path, "ul")
    _mit_kreuz(svg, h)
    info = svg_lesen.lesen(inkex.load_svg(svg).getroot())
    assert len(info["elemente"]) == 1 and info["unbekannt"] == 0
    assert info["nullmarken"][1] == pytest.approx(list(KREUZ))


def test_zweiseitig_nur_seite_2(tmp_path):
    """Kreuz auf Seite 2: Seite 2 rechnet ab dem Kreuz, Seite 1 weiter ab dem gewählten Nullpunkt."""
    from hirch_cam import dokument
    svg, (b, h) = _zeichnung(tmp_path, "ul", zweiseitig="x")
    _mit_kreuz(svg, h, x0=P2_X)
    erg = dokument.rechnen(svg, str(tmp_path / "aus"), "t")
    assert erg["ok"], erg.get("warnungen")
    nx, ny = _soll_null("ul", 0, 0, b, h)
    alle = {}
    for d in erg["dateien"]:
        alle.update(_loecher(d))
    _pruefen(alle, (LOCH[0] - nx, LOCH[1] - ny), -TIEFE)
    _pruefen(alle, (LOCH2[0] - KREUZ[0], LOCH2[1] - KREUZ[1]), None)
    an = dokument.analyse(svg, 1)
    k = {n["seite"]: n for n in an["nullpunkte"] if n["platte"] == 1}
    assert not k[1]["provisorisch"] and k[2]["provisorisch"]
    assert (k[2]["x"], k[2]["y"]) == pytest.approx((P2_X + KREUZ[0], h - KREUZ[1]))


def test_kamera_versatz(tmp_path):
    """Peter 29.10.: Kamera am Fräskopf. Gefräster Punkt bei X0 Y0, Kamera darüber → Maschine zeigt X 100 / Y 100
    (Kamera-Versatz). X0 Y0 wird dann mit der Kamera auf dem Kreuz gesetzt; der Fräser steht um den Versatz daneben.
    G-Code: Nullpunkt = Kreuz + Versatz. Zeichenfläche und Lineale zählen weiter ab dem Kreuz."""
    from hirch_cam import dokument
    svg, (b, h) = _zeichnung(tmp_path, "ul")
    _mit_kreuz(svg, h)
    text = open(svg, encoding="utf-8").read().replace('hirch:nullpunkt="provisorisch"',
                                                      'hirch:nullpunkt="provisorisch" hirch:kamera="100,100"')
    open(svg, "w", encoding="utf-8").write(text)
    erg = dokument.rechnen(svg, str(tmp_path / "aus"), "t")
    assert erg["ok"], erg.get("warnungen")
    # Fräser bei Maschine (gx, gy) = Kamera bei (gx - 100, gy - 100) – über dem Loch: gx - 100 = LOCH - KREUZ
    _pruefen(_loecher(_datei(erg)), (LOCH[0] - KREUZ[0] - 100, LOCH[1] - KREUZ[1] - 100), -TIEFE)
    assert any("Kamera-Versatz X 100,00 / Y 100,00" in w for w in erg["warnungen"])
    an = dokument.analyse(svg, 1)
    kreuz = [n for n in an["nullpunkte"] if n["platte"] == 1 and n["seite"] == 1][0]
    assert (kreuz["x"], kreuz["y"]) == pytest.approx((KREUZ[0], h - KREUZ[1]))
    assert kreuz["kamera"] == pytest.approx([100, 100])


def test_beschriftung_zaehlt_nicht(tmp_path):
    """Die Beschriftung «Kamera-Versatz …» am Kreuz (hirch:nullteil) bestimmt die Mitte nicht."""
    from hirch_cam import svg_lesen
    import inkex
    svg, (b, h) = _zeichnung(tmp_path, "ul")
    _mit_kreuz(svg, h)
    x, y = KREUZ[0], h - KREUZ[1]
    text = open(svg, encoding="utf-8").read().replace(
        'id="np1">', 'id="np1"><path hirch:nullteil="kamera" d="M %g %g h 30 v 3 h -30 z" style="fill:#c64600"/>' % (x + 7, y - 3))
    open(svg, "w", encoding="utf-8").write(text)
    info = svg_lesen.lesen(inkex.load_svg(svg).getroot())
    assert info["nullmarken"][1] == pytest.approx(list(KREUZ))
