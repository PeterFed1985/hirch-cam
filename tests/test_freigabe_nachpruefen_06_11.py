"""Endprüfung (freigabe.py): ein echter Eilgang durch stehendes Material bleibt ein Fehler, auch mit der
Nachprüfung in der Form des Fräsers (06.11.) – die Nachprüfung nimmt nur Fehlalarme weg."""
import os

from hirch_cam import freigabe
from hirch_cam.model import Job, Move, Toolpath
from hirch_cam.post import tap_mm


def _datei(tmp_path, name, moves):
    p = tmp_path / name
    p.write_text(tap_mm.write(Job(toolpaths=[Toolpath(moves=moves)])), newline="")
    return str(p)


def test_echter_eilgang_bleibt(tmp_path):
    raeumen = [Move(True, 10, 10, 5), Move(False, 10, 10, -5, 300), Move(False, 30, 10, -5, 500), Move(True, 30, 10, 5)]
    a = _datei(tmp_path, "a.tap", raeumen)
    schlecht = _datei(tmp_path, "b.tap", [Move(True, 12, 10, 5), Move(False, 12, 10, -2, 300),
                                          Move(True, 12, 30, -2), Move(True, 12, 30, 5)])
    fr = {"a.tap": (3.0, True, 1, "flach", 60.0, 0.0, 0.0), "b.tap": (3.0, True, 1, "flach", 60.0, 0.0, 0.0)}
    erg = freigabe.pruefen([a, schlecht], 10.0, 0.0, 20.0, fraeser=fr)
    assert any("Eilgang/Eilgänge unterhalb" in p["text"] and p["stufe"] == "fehler" for p in erg["punkte"])


def test_eilgang_im_geraeumten_kein_fehler(tmp_path):
    raeumen = [Move(True, 10, 10, 5), Move(False, 10, 10, -5, 300), Move(False, 30, 10, -5, 500), Move(True, 30, 10, 5)]
    a = _datei(tmp_path, "a.tap", raeumen)
    gut = _datei(tmp_path, "b.tap", [Move(True, 12, 10, 5), Move(False, 12, 10, -2, 300),
                                     Move(True, 28, 10, -2), Move(True, 28, 10, 5)])
    fr = {"a.tap": (3.0, True, 1, "flach", 60.0, 0.0, 0.0), "b.tap": (3.0, True, 1, "flach", 60.0, 0.0, 0.0)}
    erg = freigabe.pruefen([a, gut], 10.0, 0.0, 20.0, fraeser=fr)
    assert not any("Eilgang/Eilgänge unterhalb" in p["text"] for p in erg["punkte"])
