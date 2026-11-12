"""werkzeuge/eilgang_pruefen.py: findet einen Eilgang durch stehendes Material, nicht einen durch Geräumtes."""
import os
import sys

from hirch_cam.model import Job, Move, Toolpath
from hirch_cam.post import tap_mm

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "werkzeuge"))
import eilgang_pruefen as ep  # noqa: E402


def _datei(tmp_path, name, moves):
    p = tmp_path / name
    p.write_text(tap_mm.write(Job(toolpaths=[Toolpath(moves=moves)])), newline="")
    return str(p)


def test_eilgang_durch_material_und_durch_geraeumtes(tmp_path):
    raeumen = [Move(True, 10, 10, 5)] + [Move(False, x, 10, -5, 500) for x in (10, 30)]
    a = _datei(tmp_path, "a.tap", raeumen)
    gut = _datei(tmp_path, "b.tap", [Move(True, 12, 10, 5), Move(True, 12, 10, -2), Move(True, 28, 10, -2),
                                     Move(True, 28, 10, 5)])
    schlecht = _datei(tmp_path, "c.tap", [Move(True, 12, 10, 5), Move(True, 12, 10, -2), Move(True, 12, 30, -2),
                                          Move(True, 12, 30, 5)])
    prog = lambda f: {"datei": f, "seite": 1, "durchmesser": 6, "form": "flach", "name": f[-5:]}
    assert ep.pruefen({"programme": [prog(a), prog(gut)]}) == []
    f = ep.pruefen({"programme": [prog(a), prog(schlecht)]})
    assert f and f[0]["programm"] == "c.tap" and f[0]["tief"] > 1.5
    # ausserhalb des Rohteils ist Luft
    draussen = _datei(tmp_path, "d.tap", [Move(True, 12, 40, 5), Move(True, 12, 40, -2), Move(True, 30, 40, -2)])
    assert ep.pruefen({"programme": [prog(a), prog(draussen)]}, aussen_frei=(0, 0, 40, 30)) == []
    assert ep.pruefen({"programme": [prog(a), prog(draussen)]})


def test_eintauchen_ohne_mittenschnitt(tmp_path):
    """Sicherheitsprüfung 04.11.: ein Fräser ohne Mittenschnitt, der senkrecht im Vorschub in stehendes Material
    taucht, wird gemeldet; derselbe mit Rampe, oder in schon Geräumtes, nicht."""
    senkrecht = _datei(tmp_path, "s.tap", [Move(True, 10, 10, 5), Move(False, 10, 10, -3, 200),
                                           Move(False, 30, 10, -3, 500)])
    rampe = _datei(tmp_path, "r.tap", [Move(True, 10, 10, 5), Move(False, 10, 10, 0, 200),
                                       Move(False, 30, 10, -3, 200), Move(False, 10, 10, -3, 500)])
    prog = lambda f: {"datei": f, "seite": 1, "durchmesser": 6, "form": "flach", "name": f[-5:], "werkzeug": "E"}
    ohne = {"E": {"typ": "schaft", "durchmesser": 6, "mittenschnitt": False}}
    f = ep.pruefen({"programme": [prog(senkrecht)]}, werkzeuge=ohne)
    assert [x["art"] for x in f] == ["eintauchen"] and f[0]["tief"] > 2.5
    assert ep.pruefen({"programme": [prog(senkrecht)]}, werkzeuge={"E": {"typ": "schaft"}}) == []
    assert ep.pruefen({"programme": [prog(rampe)]}, werkzeuge=ohne) == []
    nochmal = _datei(tmp_path, "n.tap", [Move(True, 20, 10, 5), Move(False, 20, 10, -3, 200)])
    assert ep.pruefen({"programme": [prog(rampe), prog(nochmal)]}, werkzeuge=ohne) == []
