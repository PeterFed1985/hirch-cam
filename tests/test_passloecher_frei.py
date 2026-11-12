"""Passlöcher frei platziert: je Loch sein Partner gespiegelt, Loch auf der Achse einzeln,
Passloch-Kreise in der Zeichnung werden nie gefräst."""
import json

import inkex

from hirch_cam import einstellungen as einst
from hirch_cam import svg_lesen


def _e(achse, punkte):
    return einst.laden(json.dumps({"zweiseitig": {"an": True, "achse": achse, "pass_lage": "frei",
                                                  "pass_punkte": punkte}}))


def test_frei_partner_gespiegelt():
    pts = einst.passloecher(_e("x", [[20, 15], [150, 30]]), 200, 100)
    assert pts == [(20, 15), (20, 85), (150, 30), (150, 70)]
    pts = einst.passloecher(_e("y", [[20, 15]]), 200, 100)
    assert pts == [(20, 15), (180, 15)]


def test_frei_auf_der_achse_einzeln():
    pts = einst.passloecher(_e("x", [[30, 50], [170, 50]]), 200, 100)
    assert pts == [(30, 50), (170, 50)]


def test_frei_ohne_punkte_wie_achse():
    assert einst.passloecher(_e("x", []), 200, 100) == [(15.0, 50.0), (185.0, 50.0)]


def test_frei_gewendet_trifft_stifte():
    for achse in ("x", "y"):
        pts = einst.passloecher(_e(achse, [[12, 9], [77, 40], [100, 50]]), 200, 100)
        gewendet = {(x, 100 - y) if achse == "x" else (200 - x, y) for x, y in pts}
        assert gewendet == set(pts)


def test_passloch_kreis_wird_nicht_gelesen():
    svg = inkex.load_svg(
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
        'width="200mm" height="100mm" viewBox="0 0 200 100">'
        '<g id="ebene-passloecher"><circle hirch:passloch="1" hirch:pass_rolle="a" cx="20" cy="85" r="4" '
        'style="fill:#26a269;stroke:#26a269"/></g>'
        '<rect x="50" y="20" width="40" height="30" style="fill:none;stroke:#0000ff"/></svg>').getroot()
    erg = svg_lesen.lesen(svg)
    assert len(erg["elemente"]) == 1 and erg["unbekannt"] == 0


def test_pruefung_rand_und_teil():
    from hirch_cam import dokument
    e = _e("x", [[5, 50], [100, 30], [160, 20]])
    info = {"breite": 200, "hoehe": 100,
            "elemente": [{"art": "kontur", "seite": 1, "linien": [{"punkte": [(90, 20), (110, 20)]}]},
                         {"art": "kontur", "seite": 2, "linien": [{"punkte": [(160, 85), (170, 85)]}]}]}
    pl = einst.passloecher(e, 200, 100)
    # (5,50): Rand; (100,30) 10 mm von der Linie: ok bei r=3+3; (160,20) unter dem gespiegelten Teil von Seite 2
    warn = dokument.passloecher_pruefen(e, info, pl)
    assert [pl[i] for i in warn] == [(5, 50), (160, 20)]


def test_hinweis_nennt_das_paar():
    """Der Hinweis nennt die Nummer wie auf der Zeichnung (Peter 26.10.): «1» und sein Partner «1′»."""
    from hirch_cam import dokument
    e = _e("x", [[5, 50], [100, 30], [160, 20]])
    info = {"breite": 200, "hoehe": 100,
            "elemente": [{"art": "kontur", "seite": 1, "linien": [{"punkte": [(90, 20), (110, 20)]}]},
                         {"art": "kontur", "seite": 2, "linien": [{"punkte": [(160, 85), (170, 85)]}]},
                         {"art": "kontur", "seite": 1, "linien": [{"punkte": [(95, 70), (105, 70)]}]}]}
    pl = einst.passloecher(e, 200, 100)
    b = dokument.passloecher_befunde(e, info, pl)
    assert [(x["nummer"], x["grund"]) for x in b] == [("1", "rand"), ("2′", "teil"), ("3", "teil")]
    assert dokument.passloch_hinweise(b) == [
        "Passloch 1 liegt 2 mm vom Materialrand (mindestens 3 mm) – Paar verschieben.",
        "Passloch 2′ überschneidet ein Teil – Paar verschieben.",
        "Passloch 3 liegt 2 mm neben einem Teil (mindestens 3 mm) – Paar verschieben."]


def test_nummern_wie_das_panel():
    """Reihenfolge der Paare wie pass_primaer im Panel: Ecken je Achse, Standard auf der Achse."""
    B, H = 200, 100
    e = {"zweiseitig": {"an": True, "achse": "x", "pass_lage": "ecken", "pass_rand": 15}}
    assert [einst.pass_nummer(e, B, H, x, y) for x, y in einst.passloecher(e, B, H)] == ["1", "2", "1′", "2′"]
    e["zweiseitig"]["achse"] = "y"
    assert [einst.pass_nummer(e, B, H, x, y) for x, y in einst.passloecher(e, B, H)] == ["1", "1′", "2", "2′"]
    e = {"zweiseitig": {"an": True, "achse": "x", "pass_rand": 15}}
    assert [einst.pass_nummer(e, B, H, x, y) for x, y in einst.passloecher(e, B, H)] == ["1", "2"]
