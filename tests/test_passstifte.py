"""Zweiseitig: um die Passlöcher wird nie gefräst (Passstifte, Lochwand)."""
import json
import math
import re
from xml.sax.saxutils import quoteattr

import pytest

from hirch_cam import passstifte
from hirch_cam.model import Move, Toolpath


def _abstand_min(moves, kreise, start=(0.0, 0.0, 20.0)):
    """kleinster Abstand (Lochmitte – Bahn) aller Stücke unter der Oberfläche, je Kreis"""
    best = math.inf
    q = start
    for m in moves:
        b = (m.x, m.y, m.z)
        n = max(2, int(math.dist(q[:2], b[:2]) / 0.1) + 1)
        for i in range(n):
            t = i / (n - 1)
            p = [q[k] + (b[k] - q[k]) * t for k in range(3)]
            if p[2] < -1e-6:
                for x, y, r in kreise:
                    best = min(best, math.hypot(p[0] - x, p[1] - y) - r)
        q = b
    return best


def test_aussparen_hebt_ab_und_taucht_wieder_ein():
    tp = Toolpath(moves=[Move(True, 0, 50, 5), Move(False, 0, 50, -2, 300), Move(False, 100, 50, -2, 1000),
                         Move(True, 100, 50, 5)])
    kreise = [(50.0, 50.0, 7.0)]
    assert passstifte.aussparen(tp, kreise, 3.0, 300.0) == 1
    assert _abstand_min(tp.moves, kreise) >= -1e-6
    # Bahn beginnt und endet wie vorher, das Wiedereintauchen geht im Eintauchvorschub
    assert (tp.moves[0].x, tp.moves[-1].x) == (0, 100)
    ein = [m for m in tp.moves if not m.rapid and abs(m.x - 57) < 1e-6 and m.feed == 300.0]
    assert ein and ein[0].z == -2
    hoch = [m for m in tp.moves if m.rapid and m.z == 3.0]
    assert len(hoch) == 2 and abs(hoch[0].x - 43) < 1e-6 and abs(hoch[1].x - 57) < 1e-6


def test_eilgang_unter_der_oberflaeche_hebt_vor_dem_kreis_ab():
    """Eilgang in der geräumten Tasche (0,5 mm über dem Boden) Richtung Sperrkreis: schon am Start
    abheben, nicht erst am Kreis – dort steht das Material, um das herum gefräst wurde."""
    tp = Toolpath(moves=[Move(False, 80, 50, -1.5, 800), Move(True, 80, 50, -1.0), Move(True, 30, 50, -1.0),
                         Move(False, 30, 50, -1.5, 300), Move(False, 20, 50, -1.5, 800)])
    kreise = [(50.0, 50.0, 10.0)]
    assert passstifte.aussparen(tp, kreise, 3.0, 300.0) == 1
    unten_eil = [m for m in tp.moves if m.rapid and m.z < 0]
    assert [(m.x, m.z) for m in unten_eil] == [(80, -1.0)]          # nur das Abheben 0,5 mm
    hoch = [m for m in tp.moves if m.rapid and m.z == 3.0]
    assert (hoch[0].x, hoch[-1].x) == (80, 30)
    ab = [m for m in tp.moves if not m.rapid and m.x == 30 and m.z == -1.0]
    assert ab and ab[0].feed == 300.0                                 # wieder hinunter im Eintauchvorschub
    assert (tp.moves[-1].x, tp.moves[-1].z) == (20, -1.5)


def test_aussparen_ueber_der_oberflaeche_und_daneben_unveraendert():
    tp = Toolpath(moves=[Move(True, 0, 50, 5), Move(True, 100, 50, 5), Move(False, 100, 80, -2, 500)])
    vorher = [(m.x, m.y, m.z) for m in tp.moves]
    assert passstifte.aussparen(tp, [(50.0, 50.0, 7.0)], 3.0, 300.0) == 0
    assert [(m.x, m.y, m.z) for m in tp.moves] == vorher


def test_sperrkreise_nur_zweiseitig():
    a = {"zweiseitig": {"an": True, "achse": "x", "passloecher": True, "pass_d": 6, "pass_rand": 15},
         "material_flaeche": [100, 90]}
    k = passstifte.sperrkreise(a, 12)
    assert [(x, y) for x, y, _r in k] == [(15.0, 45.0), (85.0, 45.0)]
    assert k[0][2] == pytest.approx(3 + 6 + passstifte.ABSTAND)
    assert passstifte.sperrkreise(dict(a, zweiseitig={"an": False}), 12) == []
    assert passstifte.sperrkreise(dict(a, zweiseitig=dict(a["zweiseitig"], passloecher=False)), 12) == []


def _zeichnung(tmp_path, bearbeitungen, objekte=""):
    e = {"eingerichtet": True, "material": "hartholz", "reihenfolge": "liste",
         "postprozessor": "linuxcnc_mm",
         "zweiseitig": {"an": True, "achse": "x", "passloecher": True, "pass_d": 6.0, "pass_rand": 15.0,
                        "pass_werkzeug": "C"},
         "werkzeuge": {"C": {"typ": "schaft", "durchmesser": 6.0, "schneiden": 2, "schneidenlaenge": 22,
                             "nummer": 1, "auto": True},
                       "G": {"typ": "schaft", "durchmesser": 12.0, "schneiden": 2, "schneidenlaenge": 30,
                             "nummer": 2, "auto": True}},
         "bearbeitungen": bearbeitungen}
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
           f'xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" width="100mm" height="90mm" '
           f'viewBox="0 0 100 90" hirch:materialdicke="18" hirch:einstellungen={quoteattr(json.dumps(e))}>'
           f'<g inkscape:groupmode="layer" id="ebene1">{objekte}</g></svg>')
    p = tmp_path / "zweiseitig.svg"
    p.write_text(svg, encoding="utf-8")
    return str(p)


def _gcode_moves(datei):
    x = y = 0.0
    z = 20.0
    aus = []
    with open(datei, encoding="utf-8") as f:
        zeilen = f.readlines()
    for zeile in zeilen:
        w = dict(re.findall(r"([XYZ])(-?\d+\.?\d*)", zeile.split("(")[0]))
        if not w:
            continue
        x, y, z = float(w.get("X", x)), float(w.get("Y", y)), float(w.get("Z", z))
        aus.append(Move(True, x, y, z))
    return aus


def test_planen_und_tasche_lassen_die_passstifte_stehen(tmp_path):
    from hirch_cam import dokument
    svg = _zeichnung(tmp_path, [
        {"id": "t1", "art": "tasche", "werkzeug": "G", "tiefe": 4, "zustellung": 2, "farbe": "#00a000"},
        {"id": "p2", "art": "planen", "werkzeug": "G", "tiefe": 0.5, "zustellung": 0.5, "auf_seite": 2,
         "farbe": "#9a7b4f"}],
        objekte='<rect x="5" y="30" width="40" height="30" style="fill:none;stroke:#00a000"/>')
    erg = dokument.rechnen(svg, str(tmp_path / "aus"), "teil")
    assert erg["ok"], erg
    # Lochrand + 1 mm frei, gemessen an der Fräsermitte: 3 + 6 + 1 = 10 mm
    pins = [(15.0, 45.0, 3.0 + 6.0 + passstifte.ABSTAND), (85.0, 45.0, 10.0)]
    import glob
    gepr = 0
    for datei in glob.glob(str(tmp_path / "aus" / ".teile" / "*D12*")):
        assert _abstand_min(_gcode_moves(datei), pins) >= -0.01, datei
        gepr += 1
    assert gepr == 2
    assert any("Passlöcher wird nicht gefräst" in w for w in erg["warnungen"]), erg["warnungen"]


def test_kontur_durch_ein_passloch_warnt(tmp_path):
    from hirch_cam import dokument
    svg = _zeichnung(tmp_path, [
        {"id": "k1", "art": "kontur", "werkzeug": "C", "tiefe": 18.5, "zustellung": 6, "seite": "aussen",
         "stege_modus": "auto", "stege_anzahl": 0, "farbe": "#0050ff"}],
        objekte='<rect x="12" y="20" width="76" height="50" style="fill:none;stroke:#0050ff"/>')
    erg = dokument.rechnen(svg, str(tmp_path / "aus"), "teil")
    assert erg["ok"], erg
    assert any("läuft durch ein Passloch" in w for w in erg["warnungen"]), erg["warnungen"]
