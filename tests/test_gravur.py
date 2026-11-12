import pytest

from hirch_cam.farben import fraesart
from hirch_cam.model import Job
from hirch_cam.ops.gravur import Linie, gravur, zustellungen
from hirch_cam.post import tap_mm


def test_farben():
    assert fraesart((255, 140, 0)) == "gravur"
    assert fraesart((250, 150, 10)) == "gravur"     # leicht abweichend
    assert fraesart((224, 0, 0)) == "vcarve"
    assert fraesart((255, 0, 0)) == "vcarve"
    assert fraesart((0, 0, 255)) == "kontur"
    assert fraesart((0, 0, 0)) is None               # Schwarz wird nie gefräst
    assert fraesart((255, 255, 255)) is None


def test_zustellungen_wie_vcarve():
    assert zustellungen(2.5, 1.25) == [-1.25, -2.5]
    assert zustellungen(5, 2) == pytest.approx([-5 / 3, -10 / 3, -5])
    assert zustellungen(0.5, 0.5) == [-0.5]


def test_geschlossenes_rechteck_zwei_durchgaenge():
    rect = Linie([(7.85, 56.95), (20.95, 56.95), (20.95, 80.05), (7.85, 80.05), (7.85, 56.95)])
    assert rect.geschlossen
    tp = gravur([rect], 2.5, 1.25, 2000, 300, freifahrhoehe=3)
    job = Job(spindle_rpm=5000, toolpaths=[tp])
    lines = tap_mm.write(job).split("\r\n")
    # Gleiche Struktur wie Peters Profile.tap
    assert lines[4:17] == [
        "G0X7.850Y56.950Z3.000", "G0Z0.500",
        "G1Z-1.250F300.0", "G1X20.950F2000.0", "Y80.050", "X7.850", "Y56.950",
        "G1Z-2.500F300.0", "G1X20.950F2000.0", "Y80.050", "X7.850", "Y56.950",
        "G0Z3.000",
    ]


def test_offene_linie_hin_und_zurueck():
    l = Linie([(0, 0), (10, 0)])
    assert not l.geschlossen
    tp = gravur([l], 1, 0.5, 1000, 100, start=(0, 0))
    feed = [(m.x, m.z) for m in tp.moves if not m.rapid]
    assert feed == [(0, -0.5), (10, -0.5), (10, -1.0), (0, -1.0)]


def test_reihenfolge_kurze_leerwege():
    weit = Linie([(100, 0), (110, 0)])
    nah = Linie([(5, 0), (1, 0)])         # wird umgedreht: startet bei 1
    tp = gravur([weit, nah], 0.5, 0.5, 1000, 100, start=(0, 0))
    erste = next(m for m in tp.moves if not m.rapid)
    assert (erste.x, erste.y) == (1, 0)


def test_rundlauf_durch_postprozessor():
    kreis = Linie([(10 + 5 * c, 10 + 5 * s) for c, s in
                   [(1, 0), (0, 1), (-1, 0), (0, -1), (1, 0)]])
    tp = gravur([kreis, Linie([(30, 30), (40, 35)])], 1, 0.4, 600, 100)
    text = tap_mm.write(Job(toolpaths=[tp]))
    assert tap_mm.write(tap_mm.read(text)) == text
    # Eilgänge waagrecht nur auf Freifahrhöhe
    p = (0, 0, 20)
    for m in tp.moves:
        if m.rapid and (m.x, m.y) != p[:2]:
            assert min(p[2], m.z) >= 3
        p = (m.x, m.y, m.z)
