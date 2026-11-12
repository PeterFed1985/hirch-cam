"""Kurze Geraden vor der Ausgabe zusammenfassen (Peter, 22.10.2026: Teller mit vielen Kurven 581 KB gegen 165 KB bei einem anderen CAM-Programm)."""
import math

from hirch_cam.model import Move, Toolpath
from hirch_cam.ops.bahn import vereinfachen


def _abstand(p, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    L = dx * dx + dy * dy
    t = 0.0 if L == 0 else max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / L))
    return math.hypot(p[0] - a[0] - t * dx, p[1] - a[1] - t * dy)


def test_dichter_kreis_wird_schlank_und_bleibt_genau():
    r, n = 20.0, 4000                       # Punkte alle ~0,03 mm
    pts = [(r * math.cos(2 * math.pi * i / n), r * math.sin(2 * math.pi * i / n)) for i in range(n + 1)]
    tp = Toolpath(moves=[Move(True, pts[0][0], pts[0][1], 3.0), Move(False, pts[0][0], pts[0][1], -0.5, 100)]
                  + [Move(False, x, y, -0.5, 800) for x, y in pts[1:]] + [Move(True, pts[-1][0], pts[-1][1], 3.0)])
    weg = vereinfachen(tp, 0.01)
    assert weg > 3500
    assert tp.moves[0].rapid and tp.moves[-1].rapid
    assert tp.moves[1].feed == 100 and tp.moves[1].z == -0.5       # Eintauchen bleibt
    bahn = [(m.x, m.y) for m in tp.moves[1:-1]]
    assert bahn[-1] == pts[-1]
    for p in pts[::7]:                                                # jeder alte Punkt liegt nah an der neuen Bahn
        assert min(_abstand(p, a, b) for a, b in zip(bahn, bahn[1:])) <= 0.0101


def test_ecken_tiefe_und_vorschub_bleiben():
    tp = Toolpath(moves=[Move(False, 0, 0, -1, 500), Move(False, 5, 0, -1, 500), Move(False, 10, 0, -1, 500),
                         Move(False, 10, 10, -1, 500), Move(False, 10, 10, -2, 200), Move(False, 0, 10, -2, 500)])
    vereinfachen(tp, 0.01, start=(0, 0, 0))
    assert [(m.x, m.y, m.z, m.feed) for m in tp.moves] == [
        (0, 0, -1, 500), (10, 0, -1, 500), (10, 10, -1, 500), (10, 10, -2, 200), (0, 10, -2, 500)]
