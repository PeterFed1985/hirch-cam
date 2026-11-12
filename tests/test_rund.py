"""Rundes Material (stehend, auf der Drehbank vorbereitet): Nullpunkt in der Mitte, Schraube
durch die Mitte, Planfräsen nur über dem Kreis, Kerbe zum Wenden, keine Passlöcher."""
import glob
import json
import math
import re
from xml.sax.saxutils import quoteattr

from hirch_cam import dokument, einstellungen
from hirch_cam.ops.planen import planen

D = 80.0          # Ø des Zylinders


def _zeichnung(tmp_path, bearbeitungen, objekte="", rund=None, zweiseitig=None, nullpunkt=None):
    from hirch_cam import werkzeugdb as _wdb  # die Steuerung kommt aus der Maschine (27.10.)
    _wdb.maschine_setzen({"postprozessor": "linuxcnc_mm"})
    e = {"eingerichtet": True, "material": "hartholz", "reihenfolge": "liste", "postprozessor": "linuxcnc_mm",
         "rund": dict({"an": True, "schraube": True, "bohrung": 6.5, "kopf": 12.0, "ueberstand": 0.0},
                      **(rund or {})),
         "werkzeuge": {"C": {"typ": "schaft", "durchmesser": 6.0, "schneiden": 2, "schneidenlaenge": 22,
                             "nummer": 1, "auto": True},
                       "G": {"typ": "schaft", "durchmesser": 12.0, "schneiden": 2, "schneidenlaenge": 30,
                             "nummer": 2, "auto": True}},
         "bearbeitungen": bearbeitungen}
    if zweiseitig:
        e["zweiseitig"] = zweiseitig
    if nullpunkt:
        e["nullpunkt"] = nullpunkt
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
           f'xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" width="{D}mm" height="{D}mm" '
           f'viewBox="0 0 {D} {D}" hirch:materialdicke="40" hirch:einstellungen={quoteattr(json.dumps(e))}>'
           f'<g inkscape:groupmode="layer" id="ebene1">{objekte}</g></svg>')
    p = tmp_path / "rund.svg"
    p.write_text(svg, encoding="utf-8")
    return str(p)


def _bewegungen(datei):
    x = y = 0.0
    z = 20.0
    aus = []
    with open(datei, encoding="utf-8") as f:
        for zeile in f:
            w = dict(re.findall(r"([XYZ])(-?\d+\.?\d*)", zeile.split("(")[0]))
            if not w:
                continue
            x, y, z = float(w.get("X", x)), float(w.get("Y", y)), float(w.get("Z", z))
            aus.append((x, y, z))
    return aus


def _abgetastet(punkte, schritt=0.2):
    for a, b in zip(punkte, punkte[1:]):
        n = max(2, int(math.dist(a[:2], b[:2]) / schritt) + 1)
        for i in range(n):
            t = i / (n - 1)
            yield tuple(a[k] + (b[k] - a[k]) * t for k in range(3))


def test_einstellungen_und_nullpunkt_in_der_mitte():
    e = einstellungen.laden(json.dumps({"rund": {"an": True}, "nullpunkt": {"xy": "ul", "dx": 5, "z": "unten"}}))
    assert einstellungen.rund_an(e)
    n = einstellungen.nullpunkt(e, D, D)
    assert (n["x"], n["y"], n["z_unten"]) == (40.0, 40.0, True)
    assert einstellungen.rund(einstellungen.laden(None))["an"] is False


def test_planen_nur_ueber_dem_kreis():
    tp, _w = planen((0, 0, D, D), 12, 0.5, 0.5, 2000, 500, kreis=(40.0, 40.0, 40.0))
    schnitt = [(m.x, m.y) for m in tp.moves if not m.rapid and m.z < 0]
    # jede Bahn deckt den Kreis (bis Fräserradius über den Rand) und bleibt in der Nähe
    assert max(math.hypot(x - 40, y - 40) for x, y in schnitt) < 40 + 6 + 7 + 2
    # Querfahrten liegen neben dem Kreis: die Mitte des Fräsers kommt dabei nie in den Kreis
    q = None
    for m in tp.moves:
        if q is not None and not m.rapid and m.z < 0 and abs(m.x - q[0]) < 1e-9 and abs(m.y - q[1]) > 1e-9:
            assert math.hypot(m.x - 40, m.y - 40) >= 40 + 6 - 1e-6
            assert math.hypot(q[0] - 40, q[1] - 40) >= 40 + 6 - 1e-6
        q = (m.x, m.y)
    # kürzer als das Rechteck
    tp_r, _ = planen((0, 0, D, D), 12, 0.5, 0.5, 2000, 500)
    lang = lambda t: sum(math.dist((a.x, a.y), (b.x, b.y)) for a, b in zip(t.moves, t.moves[1:]))
    assert lang(tp) < 0.97 * lang(tp_r)


def test_schraube_in_der_mitte_bleibt_frei(tmp_path):
    svg = _zeichnung(tmp_path, [
        {"id": "t1", "art": "tasche", "werkzeug": "C", "tiefe": 5, "zustellung": 2.5, "farbe": "#00a000"},
        {"id": "p1", "art": "planen", "werkzeug": "G", "tiefe": 0.5, "zustellung": 0.5, "farbe": "#9a7b4f"}],
        objekte='<rect x="20" y="20" width="40" height="40" style="fill:none;stroke:#00a000"/>',
        rund={"kerbe": True})
    erg = dokument.rechnen(svg, str(tmp_path / "aus"), "teil")
    assert erg["ok"], erg
    assert any("Schraube in der Mitte" in w for w in erg["warnungen"]), erg["warnungen"]
    gcode = [f for f in glob.glob(str(tmp_path / "aus" / ".teile" / "*")) if f.endswith(".ngc")] or \
        glob.glob(str(tmp_path / "aus" / "*.ngc"))
    assert gcode
    for datei in gcode:
        pkt = _bewegungen(datei)
        # Nullpunkt in der Mitte: Koordinaten um 0 herum
        assert min(p[0] for p in pkt if p[2] < 0) < 0 < max(p[0] for p in pkt if p[2] < 0)
        for x, y, z in _abgetastet(pkt):
            if z < -1e-6:
                r_w = 6.0 if "D12" in datei else 3.0
                assert math.hypot(x, y) >= 6.0 + r_w + 1.0 - 0.01, (datei, x, y, z)
    # Kerbe zum Wenden bei X+ am Rand
    assert any("Kerbe" in str(b) for b in erg.get("bearbeitungen", {}).values()) or \
        any("Kerbe" in z for z in erg["bericht"]), erg["bericht"]


def test_ueberstehender_kopf_hebt_die_freifahrhoehe(tmp_path):
    svg = _zeichnung(tmp_path, [
        {"id": "t1", "art": "tasche", "werkzeug": "C", "tiefe": 2, "zustellung": 2, "farbe": "#00a000"}],
        objekte='<rect x="10" y="10" width="20" height="20" style="fill:none;stroke:#00a000"/>'
                '<rect x="50" y="50" width="20" height="20" style="fill:none;stroke:#00a000"/>',
        rund={"ueberstand": 4.0})
    erg = dokument.rechnen(svg, str(tmp_path / "aus"), "teil")
    assert erg["ok"], erg
    assert any("Freifahrhöhe auf 6 mm" in w for w in erg["warnungen"]), erg["warnungen"]
    datei = glob.glob(str(tmp_path / "aus" / "*.ngc"))[0]
    pkt = _bewegungen(datei)
    # zwischen den beiden Taschen über der Mitte nie tiefer als der Kopf + 2 mm
    for x, y, z in _abgetastet(pkt):
        if math.hypot(x, y) < 6.0 + 3.0 + 1.0:
            assert z >= 6.0 - 1e-6, (x, y, z)


def test_zweiseitig_rund_ohne_passloecher(tmp_path):
    svg = _zeichnung(tmp_path, [
        {"id": "t1", "art": "tasche", "werkzeug": "C", "tiefe": 3, "zustellung": 3, "farbe": "#00a000"},
        {"id": "t2", "art": "tasche", "werkzeug": "C", "tiefe": 3, "zustellung": 3, "auf_seite": 2,
         "farbe": "#006300"}],
        objekte='<rect x="15" y="15" width="12" height="12" style="fill:none;stroke:#00a000"/>',
        zweiseitig={"an": True, "achse": "x", "passloecher": True})
    erg = dokument.rechnen(svg, str(tmp_path / "aus"), "teil")
    assert erg["ok"], erg
    assert not any("Passl" in z for z in erg["bericht"]), erg["bericht"]


def test_neben_dem_kreis_warnt(tmp_path):
    svg = _zeichnung(tmp_path, [
        {"id": "t1", "art": "tasche", "werkzeug": "C", "tiefe": 2, "zustellung": 2, "farbe": "#00a000"}],
        objekte='<rect x="0" y="0" width="12" height="12" style="fill:none;stroke:#00a000"/>')
    erg = dokument.rechnen(svg, str(tmp_path / "aus"), "teil")
    assert any("neben dem runden Material" in w for w in erg["warnungen"]), erg["warnungen"]


def test_grundplatte_auf_der_vorrichtung(tmp_path):
    """Spannvorrichtung + rundes Material: Ring Ø + 0,2, Mittelbohrung ganz durch und Marke bei X+,
    X0 Y0 in der Mitte des Rings; das Teil selbst behält die Mitte als Nullpunkt."""
    import pytest
    bs = [{"id": "g1", "art": "gravur", "tiefe": 0.5, "zustellung": 0.5, "farbe": "#ff8000", "werkzeug": "C"}]
    from hirch_cam import werkzeugdb as _wdb  # die Steuerung kommt aus der Maschine (27.10.)
    _wdb.maschine_setzen({"postprozessor": "linuxcnc_mm"})
    e = {"eingerichtet": True, "material": "hartholz", "postprozessor": "linuxcnc_mm",
         "rund": {"an": True, "schraube": True, "bohrung": 6.5, "kopf": 12.0, "kerbe": True},
         "vorrichtung": {"an": True, "dicke": 19, "lage": [60, 25]},
         "werkzeuge": {"B": {"typ": "schaft", "durchmesser": 3.0, "schneiden": 2, "schneidenlaenge": 12,
                             "nummer": 1, "auto": True},
                       "C": {"typ": "schaft", "durchmesser": 6.0, "schneiden": 2, "schneidenlaenge": 22,
                             "nummer": 2, "auto": True}},
         "bearbeitungen": bs}
    svg = tmp_path / "rv.svg"
    svg.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
        'xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
        'xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd" '
        f'width="{D}mm" height="{D}mm" viewBox="0 0 {D} {D}" hirch:materialdicke="40" '
        f'hirch:einstellungen={quoteattr(json.dumps(e))}><sodipodi:namedview id="nv">'
        f'<inkscape:page id="p1" x="0" y="0" width="{D}" height="{D}"/>'
        '<inkscape:page id="pv" x="-220" y="0" width="200" height="130" hirch:vorrichtung="1"/>'
        '</sodipodi:namedview>'
        '<path id="l" d="M 20,40 L 60,40" style="fill:none;stroke:#ff8000"/></svg>', encoding="utf-8")
    v = dokument.rechnen(str(svg), str(tmp_path / "v"), "platte", platte=0)
    assert v["ok"], v.get("warnungen")
    ids = set(v["bearbeitungen"])
    assert {einstellungen.GRUND_RING_ID, einstellungen.GRUND_BOHRUNG_ID, einstellungen.GRUND_MARKE_ID} <= ids
    assert "g1" not in ids and einstellungen.KERBE_ID not in ids
    pts = _bewegungen(v["datei"])
    xy = [(x, y) for x, y, z in pts if z < 0]
    r = max(math.hypot(x, y) for x, y in xy)
    # Marke reicht bis R + 8 (+ Fräserradius); der Ring selbst bleibt im Ø + 0,2
    assert r < D / 2 + 0.1 + 8 + 2 and min(math.hypot(x, y) for x, y in xy) < 2.0
    ring = [math.hypot(x, y) for x, y in xy if abs(y) > 5]
    assert max(ring) == pytest.approx(D / 2 + 0.1 - 3.0, abs=0.05)     # Bahn des 6-mm-Fräsers
    zs = [z for x, y, z in pts]
    # der 3-mm-Fräser hat 12 mm Schneide: so tief es geht, der Rest von Hand (steht im Namen)
    assert min(zs) == pytest.approx(-11.5, abs=0.01)
    assert any("Rest von Hand" in z for z in v["bericht"])
    assert not [p for p in v.get("pruefung") or [] if p["stufe"] == "fehler"], v.get("pruefung")
    # mit einem langen Fräser ganz durch die 19 mm
    text = svg.read_text(encoding="utf-8")
    assert text.count('"schneidenlaenge": 12') == 1
    svg.write_text(text.replace('"schneidenlaenge": 12', '"schneidenlaenge": 25'), encoding="utf-8")
    v2 = dokument.rechnen(str(svg), str(tmp_path / "v2"), "platte", platte=0)
    assert min(z for x, y, z in _bewegungen(v2["datei"])) == pytest.approx(-19.2, abs=0.01)
    assert not [p for p in v2.get("pruefung") or [] if p["stufe"] == "fehler"], v2.get("pruefung")
    # das Teil: Nullpunkt in seiner Mitte, wie ohne Vorrichtung
    w = dokument.rechnen(str(svg), str(tmp_path / "w"), "teil", platte=1)
    assert w["ok"] and "g1" in w["bearbeitungen"]
    xs = [x for x, y, z in _bewegungen(w["datei"]) if z < 0]
    assert min(xs) == pytest.approx(-20, abs=0.01)
    assert max(xs) == pytest.approx(D / 2 + 1.5 + 1.0, abs=0.01)          # Kerbe am Teil bei X+
