"""Vergleichstest: Jede Beispieldatei «G-Code (mm)» einlesen, neu schreiben und
Byte für Byte mit dem Original vergleichen."""
from pathlib import Path

import pytest

from hirch_cam.model import Job, Move, Toolpath
from hirch_cam.post import tap_mm

SAMPLES = sorted((Path(__file__).parent / "samples").glob("*.tap"))


@pytest.mark.parametrize("path", SAMPLES, ids=[p.name for p in SAMPLES])
def test_roundtrip_identisch(path):
    original = path.read_bytes().decode("ascii")
    assert tap_mm.write(tap_mm.read(original)) == original


def test_neuer_auftrag():
    job = Job(spindle_rpm=5000, toolpaths=[Toolpath(moves=[
        Move(True, 10, 10, 3),
        Move(True, 10, 10, 0.5),
        Move(False, 10, 10, -1, 300),
        Move(False, 20, 10, -1, 2000),
        Move(False, 20, 20, -1, 2000),
        Move(True, 20, 20, 3),
    ])])
    assert tap_mm.write(job).split("\r\n") == [
        "T1M6", "G17", "G0Z20.000", "G0X0.000Y0.000S5000M3",
        "G0X10.000Y10.000Z3.000", "G0Z0.500",
        "G1Z-1.000F300.0", "G1X20.000F2000.0", "Y20.000",
        "G0Z3.000", "G0Z20.000", "G0X0.000Y0.000", "M30", "",
    ]
