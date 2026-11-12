"""werkzeuge/gcode_vergleich.py: Messwerte an einem kleinen, von Hand geschriebenen Programm."""
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "werkzeuge"))
import gcode_vergleich as gv  # noqa: E402

PROGRAMM = """T1M6
G17
G0Z20.000
G0X0.000Y0.000S12000M3
G0X10.000Y0.000Z3.000
G0Z0.500
G1Z-1.000F300.0
G1X20.000F1200.0
G0Z3.000
G0X20.000Y10.000
G0Z0.500
G1X25.000Z-1.000F300.0
G1X20.000F1200.0
G3X10.000Y10.000I-5.000J0.000Z-2.000
G0Z3.000
M5
T2M6
G0X0.000Y0.000Z3.000
G0Z0.500
G1Z-2.000F200.0
G0Z-1.500
G1Z-4.000
G0Z20.000
M30
"""


def test_werte_je_werkzeug(tmp_path):
    p = tmp_path / "a.tap"
    p.write_text(PROGRAMM.replace("\n", "\r\n"), newline="")
    erg = gv.lesen(str(p))
    a1, a2 = erg["abschnitte"]
    assert (a1["werkzeug"], a2["werkzeug"]) == ("T1", "T2")
    assert a1["eintauchen_senkrecht"] == 1 and a1["eintauchen_rampe"] == 1
    assert a1["zustellen_helix"] == 1                      # Bogen mit Z im Material
    assert a1["boegen"] == 1 and a1["freifahren"] == 2    # Z20 → Z3 am Anfang geht nach unten
    halbkreis = gv.bogen((20, 10, -1), (10, 10, -2), 15, 10, True)
    assert halbkreis == pytest.approx(((5 * 3.14159265) ** 2 + 1) ** 0.5, rel=1e-6)
    rampe = (5 ** 2 + 1.5 ** 2) ** 0.5
    assert a1["fraesweg"] == round(1.5 + 10 + rampe + 5 + halbkreis)
    # Bohren mit Entspanen: zwei Stufen von oben
    assert a2["eintauchen_senkrecht"] == 2 and a2["tiefste"] == -4.0
    assert erg["bytes"] == len(PROGRAMM.replace("\n", "\r\n").encode())


def test_marken_teilen_abschnitte(tmp_path):
    p = tmp_path / "a.tap"
    p.write_text(PROGRAMM)
    erg = gv.lesen(str(p), marken={9: "zweite Bahn"})
    assert [a["werkzeug"] for a in erg["abschnitte"]] == ["T1", "zweite Bahn", "T2"]
