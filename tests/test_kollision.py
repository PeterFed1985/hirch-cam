"""Kollisionsprüfung, Eilgang durch Ausgeräumtes, kurze Leerwege, Soll für Restmaterial."""
import math

import pytest

from hirch_cam.model import Job, Move, Toolpath

np = pytest.importorskip("numpy")


def _pruefen(moves, d=6.0, mat=(100, 60, 10)):
    from hirch_cam.kollision import pruefen
    job = Job(toolpaths=[Toolpath(moves=moves)], safe_z=20.0)
    prog = [{"datei": "x", "name": "T", "seite": 1, "durchmesser": d, "form": "flach"}]
    return pruefen(prog, {"breite": mat[0], "hoehe": mat[1], "dicke": mat[2]}, lambda _d: job)


def test_eilgang_durch_volles_material_wird_gemeldet():
    funde = _pruefen([Move(True, 20, 20, 1), Move(True, 20, 20, -2)])
    assert funde and funde[0]["tief"] > 1.5


def test_eilgang_in_ausgeraeumter_nut_ist_frei():
    m = [Move(True, 20, 20, 0.5), Move(False, 20, 20, -3, 300), Move(False, 60, 20, -3, 1000),
         Move(True, 60, 20, -2.5), Move(True, 20, 20, -2.5),       # zurück in der Nut, im Eilgang
         Move(True, 20, 20, 5)]
    assert _pruefen(m) == []


def test_eilgang_quer_ausserhalb_der_nut_wird_gemeldet():
    m = [Move(True, 20, 20, 0.5), Move(False, 20, 20, -3, 300), Move(False, 60, 20, -3, 1000),
         Move(True, 60, 40, -2.5)]
    assert _pruefen(m)


def test_tasche_faehrt_zwischen_ebenen_nicht_hoch_und_im_eilgang():
    from hirch_cam.ops.tasche import tasche
    ring = [(0, 0), (30, 0), (30, 20), (0, 20), (0, 0)]
    tp, _w = tasche([(ring, True)], 4, 6, 1, 1000, 300)
    zs = [m.z for m in tp.moves]
    # zwischen der ersten und der letzten Ebene nie über die Oberfläche
    erster = next(i for i, z in enumerate(zs) if z < -0.9)
    letzter = max(i for i, z in enumerate(zs) if z < -5.9)
    assert max(zs[erster:letzter]) <= 0.0
    # das Hinabfahren auf die vorige Ebene ist ein Eilgang
    eil_unten = [m for m in tp.moves if m.rapid and m.z < 0]
    assert len(eil_unten) >= 4
    # und trifft nirgends Material
    assert _pruefen(list(tp.moves), d=4, mat=(30, 20, 10)) == []


def test_bohren_kurze_leerwege():
    from hirch_cam.ops.bohren import bohren
    import random
    random.seed(3)
    kreise = []
    for _ in range(40):
        x, y = random.uniform(5, 295), random.uniform(5, 195)
        kreise.append(([(x + 1.5 * math.cos(a / 8 * math.pi), y + 1.5 * math.sin(a / 8 * math.pi))
                        for a in range(17)], True))
    tp, _w = bohren(kreise, 3, 2, 2, 300)
    pts = [(m.x, m.y) for m in tp.moves if m.rapid]
    weg = sum(math.dist(a, b) for a, b in zip(pts, pts[1:]))
    # nächster Nachbar allein
    rest = [((sum(p[0] for p in k[:-1]) / 16), (sum(p[1] for p in k[:-1]) / 16)) for k, _ in kreise]
    o, nn = (0.0, 0.0), 0.0
    while rest:
        k = min(range(len(rest)), key=lambda i: math.dist(o, rest[i]))
        nn += math.dist(o, rest[k])
        o = rest.pop(k)
    assert weg < nn * 0.97
