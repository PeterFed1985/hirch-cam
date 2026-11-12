"""Nullpunkt: G-Code und Zeichenfläche müssen denselben Punkt meinen (Peter, 21.10.).

Für jede Kombination aus Nullpunkt (9 Punkte, mit und ohne Versatz), Z oben/unten, verschobener Ebene
(wie in Peters DXF-Import: transform="translate(0,-100)"), zweiseitig (Wenden um X und um Y), Spannvorrichtung
(mittig oder bei einer Lage) und rundem Material gilt:

* ein Loch an bekannter Stelle steht im G-Code genau bei Loch − Nullpunkt (X, Y) und in der richtigen Tiefe;
* `analyse["nullpunkte"]` (das Kreuz auf der Zeichenfläche, in Dokument-mm) liegt genau dort, wo der G-Code
  X0 Y0 hat: Loch in Dokument-mm − Nullpunkt in Dokument-mm = Koordinate im G-Code (Y umgekehrt).
"""
import json
import re

import pytest

B, H, D = 120.0, 80.0, 18.0          # Werkstück
LOCH = (30.0, 20.0)                   # Loch auf Seite 1 (mm, von unten links)
LOCH2 = (70.0, 50.0)                  # Loch auf Seite 2 (mm, von unten links, so wie gewendet gezeichnet)
TIEFE = 3.0
P2_X = 140.0                          # Seite 2 rechts daneben
VORR = (-260.0, -100.0, 220.0, 180.0)  # Seite der Vorrichtung (x, y, b, h) – links oben, negative Koordinaten
FARBE_BOHREN = "#8000c0"
FARBE_VORR = "#8000c1"


def _inkex():
    try:
        import inkex  # noqa: F401
        return True
    except ImportError:
        return False


pytestmark = pytest.mark.skipif(not _inkex(), reason="inkex nicht gefunden")

PUNKTE = ["ul", "um", "ur", "ml", "mm", "mr", "ol", "om", "or"]


def _zeichnung(tmp_path, xy="ul", dx=0.0, dy=0.0, z="oben", ebene_verschoben=False, zweiseitig=None,
               vorrichtung=None, rund=False, name="t"):
    b, h = (B, B) if rund else (B, H)
    bs = [{"id": "b1", "art": "bohren", "tiefe": TIEFE, "zustellung": TIEFE, "farbe": FARBE_BOHREN,
           "werkzeug": "B", "methode": "bohren"}]
    e = {"bearbeitungen": bs, "material": "hartholz",
         "nullpunkt": {"xy": xy, "dx": dx, "dy": dy, "z": z}, "eingerichtet": True}
    seiten = ['<inkscape:page id="p1" x="0" y="0" width="%g" height="%g"/>' % (b, h)]
    if zweiseitig:
        e["zweiseitig"] = {"an": True, "achse": zweiseitig, "passloecher": False}
        seiten.append('<inkscape:page id="p2" x="%g" y="0" width="%g" height="%g"/>' % (P2_X, b, h))
    if vorrichtung:
        e["vorrichtung"] = dict({"an": True, "dicke": 30.0}, **vorrichtung)
        e["bearbeitungen"].append({"id": "bv", "art": "bohren", "tiefe": TIEFE, "zustellung": TIEFE,
                                   "farbe": FARBE_VORR, "werkzeug": "B", "methode": "bohren"})
        seiten.append('<inkscape:page id="pv" x="%g" y="%g" width="%g" height="%g" hirch:vorrichtung="1"/>' % VORR)
    if rund:
        e["rund"] = {"an": True, "schraube": False, "grundplatte": False}
    verschiebung = 100.0 if ebene_verschoben else 0.0

    def kreis(x_doc, y_doc, farbe, i):
        return '<circle id="k%d" cx="%g" cy="%g" r="1" style="fill:none;stroke:%s"/>' % (
            i, x_doc, y_doc + verschiebung, farbe)
    teile = [kreis(LOCH[0], h - LOCH[1], FARBE_BOHREN, 1)]
    if zweiseitig:
        teile.append(kreis(P2_X + LOCH2[0], h - LOCH2[1], FARBE_BOHREN, 2))
    if vorrichtung:
        teile.append(kreis(VORR[0] + 25.0, VORR[1] + VORR[3] - 15.0, FARBE_VORR, 3))   # Vorrichtung: 25/15
    attr = json.dumps(e).replace('"', "&quot;")
    svg = tmp_path / (name + ".svg")
    svg.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
        'xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
        'xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd" '
        'width="%gmm" height="%gmm" viewBox="0 0 %g %g" hirch:materialdicke="%g" hirch:einstellungen="%s">'
        '<sodipodi:namedview id="nv">%s</sodipodi:namedview>'
        '<g inkscape:groupmode="layer" id="l1"%s>%s</g></svg>' % (
            b, h, b, h, D, attr, "".join(seiten),
            ' transform="translate(0,%g)"' % -verschiebung if ebene_verschoben else "", "".join(teile)),
        encoding="utf-8")
    return str(svg), (b, h)


def _soll_null(xy, dx, dy, b, h):
    fy = {"u": 0.0, "m": 0.5, "o": 1.0}[xy[0]]
    fx = {"l": 0.0, "m": 0.5, "r": 1.0}[xy[1]]
    return fx * b + dx, fy * h + dy


def _loecher(datei):
    """(X, Y, tiefstes Z) jeder Bohrung im G-Code."""
    text = open(datei, newline="").read()
    x = y = None
    aus = {}
    for z in text.splitlines():
        mx, my, mz = re.search(r"X(-?[\d.]+)", z), re.search(r"Y(-?[\d.]+)", z), re.search(r"Z(-?[\d.]+)", z)
        if mx:
            x = float(mx.group(1))
        if my:
            y = float(my.group(1))
        if mz and x is not None and y is not None and not mx and not my:
            k = (round(x, 3), round(y, 3))
            aus[k] = min(aus.get(k, 1e9), float(mz.group(1)))
    return aus


def _datei(erg):
    assert len(erg["dateien"]) == 1, erg["dateien"]
    return erg["dateien"][0]


def _pruefen(erg_loecher, soll_xy, soll_z):
    treffer = [k for k in erg_loecher if abs(k[0] - soll_xy[0]) < 5e-3 and abs(k[1] - soll_xy[1]) < 5e-3]
    assert treffer, "Loch nicht bei %s – im G-Code: %s" % (soll_xy, sorted(erg_loecher))
    if soll_z is not None:
        assert erg_loecher[treffer[0]] == pytest.approx(soll_z, abs=1e-3)


@pytest.mark.parametrize("xy", PUNKTE)
@pytest.mark.parametrize("versatz", [(0.0, 0.0), (5.0, -3.0)])
@pytest.mark.parametrize("z", ["oben", "unten"])
@pytest.mark.parametrize("ebene", [False, True])
def test_seite_1(tmp_path, xy, versatz, z, ebene):
    from hirch_cam import dokument
    svg, (b, h) = _zeichnung(tmp_path, xy, versatz[0], versatz[1], z, ebene)
    nx, ny = _soll_null(xy, versatz[0], versatz[1], b, h)
    erg = dokument.rechnen(svg, str(tmp_path / "aus"), "t")
    assert erg["ok"], erg.get("warnungen")
    soll_z = -TIEFE if z == "oben" else D - TIEFE
    _pruefen(_loecher(_datei(erg)), (LOCH[0] - nx, LOCH[1] - ny), soll_z)
    # Kreuz auf der Zeichenfläche (Dokument-mm, y nach unten) = X0 Y0 im G-Code
    an = dokument.analyse(svg, 1)
    kreuz = [n for n in an["nullpunkte"] if n["platte"] == 1 and n["seite"] == 1][0]
    loch_doc = (LOCH[0], h - LOCH[1])
    assert (loch_doc[0] - kreuz["x"], kreuz["y"] - loch_doc[1]) == pytest.approx((LOCH[0] - nx, LOCH[1] - ny))
    assert kreuz["z"] == z                                   # Z0 oben/unten für 3D und Anzeige


@pytest.mark.parametrize("xy", ["ul", "mm", "or"])
@pytest.mark.parametrize("achse", ["x", "y"])
def test_seite_2_gleicher_maschinenpunkt(tmp_path, xy, achse):
    """Gewendet wird um die Passstifte; X und Y werden nicht neu angetastet – Seite 2 nutzt denselben
    Maschinenpunkt in der Ansicht nach dem Wenden."""
    from hirch_cam import dokument
    svg, (b, h) = _zeichnung(tmp_path, xy, zweiseitig=achse)
    nx, ny = _soll_null(xy, 0, 0, b, h)
    erg = dokument.rechnen(svg, str(tmp_path / "aus"), "t")
    assert erg["ok"], erg.get("warnungen")
    alle = {}
    for d in erg["dateien"]:
        alle.update(_loecher(d))
    _pruefen(alle, (LOCH2[0] - nx, LOCH2[1] - ny), None)    # Tiefe: eigene Bearbeitung der Seite 2
    an = dokument.analyse(svg, 1)
    kreuz = [n for n in an["nullpunkte"] if n["platte"] == 1 and n["seite"] == 2][0]
    loch_doc = (P2_X + LOCH2[0], h - LOCH2[1])
    assert (loch_doc[0] - kreuz["x"], kreuz["y"] - loch_doc[1]) == pytest.approx((LOCH2[0] - nx, LOCH2[1] - ny))


@pytest.mark.parametrize("xy", ["ul", "mm", "or"])
@pytest.mark.parametrize("vorr", [{"mittig": True}, {"mittig": False, "lage": [10.0, 10.0]}])
def test_vorrichtung_ein_nullpunkt(tmp_path, xy, vorr):
    """Ein Nullpunkt am Werkstück; die Vorrichtung (Platte 0) läuft mit demselben Maschinenpunkt."""
    from hirch_cam import dokument
    svg, (b, h) = _zeichnung(tmp_path, xy, vorrichtung=vorr)
    nx, ny = _soll_null(xy, 0, 0, b, h)
    lx, ly = ((VORR[2] - b) / 2, (VORR[3] - h) / 2) if vorr["mittig"] else tuple(vorr["lage"])
    w = dokument.rechnen(svg, str(tmp_path / "w"), "t", platte=1)
    assert w["ok"], w.get("warnungen")
    _pruefen(_loecher(_datei(w)), (LOCH[0] - nx, LOCH[1] - ny), -TIEFE)
    v = dokument.rechnen(svg, str(tmp_path / "v"), "t", platte=0)
    assert v["ok"], v.get("warnungen")
    _pruefen(_loecher(_datei(v)), (25.0 - (lx + nx), 15.0 - (ly + ny)), -TIEFE)
    # Kreuze: auf dem Werkstück und auf der Vorrichtung derselbe Maschinenpunkt
    an = dokument.analyse(svg, 1)
    k = {n["platte"]: n for n in an["nullpunkte"]}
    assert (k[1]["x"], k[1]["y"]) == pytest.approx((nx, h - ny))
    assert (k[0]["x"], k[0]["y"]) == pytest.approx((VORR[0] + lx + nx, VORR[1] + VORR[3] - (ly + ny)))
    assert an["vorrichtung"]["lage"] == pytest.approx([lx, ly])
    # Seite je Eintrag (Lineale/Werkzeugleiste wählen danach) und beim Rechnen der Vorrichtung dieselben Punkte
    assert k[1]["seite_mm"] == pytest.approx([0.0, 0.0, b, h])
    assert k[0]["seite_mm"] == pytest.approx(list(VORR))
    an0 = dokument.analyse(svg, 0)
    assert sorted((n["platte"], n["x"], n["y"]) for n in an0["nullpunkte"]) == \
        sorted((n["platte"], n["x"], n["y"]) for n in an["nullpunkte"])


@pytest.mark.parametrize("vorrichtung", [None, {"mittig": True}, {"mittig": False, "lage": [20.0, 30.0]}])
def test_rund_immer_die_achse(tmp_path, vorrichtung):
    from hirch_cam import dokument
    svg, (b, h) = _zeichnung(tmp_path, "ul", 7.0, 7.0, rund=True, vorrichtung=vorrichtung)
    erg = dokument.rechnen(svg, str(tmp_path / "w"), "t", platte=1)
    assert erg["ok"], erg.get("warnungen")
    _pruefen(_loecher(_datei(erg)), (LOCH[0] - b / 2, LOCH[1] - h / 2), -TIEFE)
    an = dokument.analyse(svg, 1)
    k = {n["platte"]: n for n in an["nullpunkte"]}
    assert (k[1]["x"], k[1]["y"]) == pytest.approx((b / 2, h / 2))
    assert an["passloecher"] == []
    if vorrichtung:
        lx, ly = ((VORR[2] - b) / 2, (VORR[3] - h) / 2) if vorrichtung["mittig"] else tuple(vorrichtung["lage"])
        assert (k[0]["x"], k[0]["y"]) == pytest.approx((VORR[0] + lx + b / 2, VORR[1] + VORR[3] - (ly + h / 2)))
