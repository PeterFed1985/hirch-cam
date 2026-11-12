"""Gravur, V-Carve und Fase auf einer tieferen Stufe (Peter, 23.10.2026)."""
from hirch_cam.engine import _ab_stufe
from hirch_cam.model import Move, Toolpath


def test_ab_stufe_senkt_alles_unter_der_freifahrhoehe():
    tp = Toolpath(moves=[Move(True, 0, 0, 3.0), Move(True, 0, 0, 0.5), Move(False, 0, 0, -0.5, 100),
                         Move(False, 5, 0, -0.5, 500), Move(True, 5, 0, 3.0)])
    _ab_stufe(tp, 10.0, 3.0)
    assert [m.z for m in tp.moves] == [3.0, -9.5, -10.5, -10.5, 3.0]


def test_ohne_starttiefe_unveraendert():
    tp = Toolpath(moves=[Move(False, 0, 0, -0.5, 100)])
    _ab_stufe(tp, 0.0, 3.0)
    assert tp.moves[0].z == -0.5
