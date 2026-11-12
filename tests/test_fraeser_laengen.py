"""Längen des Fräsers in den Programmen (3D-Ansicht: Schneide eingefärbt, Schaft bis zur Spannmutter, Peter 26.10.)."""
import json
import pathlib

import pytest

REF = pathlib.Path(__file__).parent / "referenz" / "zeichnungen" / "taschen.svg"


def test_programme_tragen_die_laengen_des_fraesers(tmp_path):
    inkex = pytest.importorskip("inkex")
    from hirch_cam import dokument, einstellungen as e
    doc = inkex.load_svg(str(REF))
    root = doc.getroot()
    d = json.loads(root.get(e.ATTRIBUT))
    d["werkzeuge"]["B"].update(schaft=6, gesamtlaenge=50)          # Ø 3 mit Schaft 6
    d["werkzeuge"]["C"].update(ausspannlaenge=30)
    root.set(e.ATTRIBUT, json.dumps(d))
    svg = tmp_path / "t.svg"
    doc.write(str(svg))
    erg = dokument.rechnen(str(svg), str(tmp_path / "aus"))
    assert erg["ok"]
    je = {p["werkzeug"]: p for p in erg["programme"]}
    b, c = je["B"], je["C"]
    assert b["schneidenlaenge"] == 12 and b["schaft"] == 6 and b["gesamtlaenge"] == 50
    assert b["uebergang"] == pytest.approx(1.5 / 0.36397, abs=0.01)  # 20° zur Achse von Ø 3 auf Ø 6
    assert b["ausspannlaenge"] == pytest.approx(12 + b["uebergang"] + 5, abs=0.01)   # ohne Angabe
    assert c["ausspannlaenge"] == 30 and c["gesamtlaenge"] == 0 and c["uebergang"] == 0
    assert b["schneiden"] >= 1 and b["drall"] in "RLKG"


def test_spannuten_drall_und_schneiden():
    """Spannuten der Ansicht: Drall wie im Fräserdialog (eingetragen, sonst aus dem Namen), V-Fräser gerade."""
    from hirch_cam.engine import _fraeser_laengen
    def f(**w):
        return (lambda r: (r["schneiden"], r["drall"]))(_fraeser_laengen(dict({"typ": "schaft", "durchmesser": 6}, **w)))
    assert f() == (2, "R")
    assert f(drall="ab", schneiden=3) == (3, "L")
    assert f(drall="kompression") == (2, "K")
    assert f(name="Abwärtsspirale 3 mm (Downcut)") == (2, "L")
    assert f(typ="v", spitze=0.2) == (1, "G")
