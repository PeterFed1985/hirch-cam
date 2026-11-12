"""Endprüfung der fertigen G-Code-Dateien (freigabe.py)."""
from hirch_cam import freigabe


def _datei(tmp_path, text, name="t.tap"):
    p = tmp_path / name
    p.write_text(text.replace("\n", "\r\n"))
    return str(p)


GUT = """G17
G0Z20.000
G0X0.000Y0.000M3
G0X10.000Y10.000Z3.000
G0Z0.500
G1Z-1.000F300.0
G1X20.000F1000.0
G0Z-0.500
G0X12.000
G1Z-1.000
G0Z20.000
M5
M30
"""


def test_gutes_programm(tmp_path):
    f = freigabe.pruefen([_datei(tmp_path, GUT)], 5.0, 0.0, 20.0, {"x": 600, "y": 900, "z": 150}, soll_max=1.0)
    assert f["stufe"] == "ok", f
    assert any("Tiefste Stelle 1 mm" in p["text"] for p in f["punkte"])


def test_eilgang_ins_material(tmp_path):
    schlecht = GUT.replace("G0X12.000", "G0X40.000")         # waagrecht unter der Oberfläche ins Volle
    f = freigabe.pruefen([_datei(tmp_path, schlecht)], 5.0, 0.0, 20.0)
    assert f["stufe"] == "fehler" and any("Eilg" in p["text"] for p in f["punkte"])


def test_zu_tief_und_ohne_spindel(tmp_path):
    t = GUT.replace("G1Z-1.000F300.0", "G1Z-3.000F300.0").replace("G0X0.000Y0.000M3", "G0X0.000Y0.000")
    f = freigabe.pruefen([_datei(tmp_path, t)], 5.0, 0.0, 20.0, soll_max=1.0)
    texte = " ".join(p["text"] for p in f["punkte"])
    assert f["stufe"] == "fehler" and "tiefer als jede Bearbeitung" in texte and "Spindel" in texte


def test_verfahrbereich(tmp_path):
    f = freigabe.pruefen([_datei(tmp_path, GUT.replace("G1X20.000", "G1X900.000"))], 5.0, 0.0, 20.0,
                         {"x": 600, "y": 900, "z": 150})
    assert f["stufe"] == "fehler" and any("Verfahrbereich X" in p["text"] for p in f["punkte"])


def test_luftfraesen(tmp_path):
    luft = "\n".join(l.replace("Z-1.000", "Z9.000").replace("Z-0.500", "Z9.500").replace("Z0.500", "Z10.500")
                     for l in GUT.splitlines())
    f = freigabe.pruefen([_datei(tmp_path, luft)], 5.0, 0.0, 20.0, luft=10.0)
    assert f["stufe"] == "fehler"                               # 9 mm < 10 mm Abstand
    f = freigabe.pruefen([_datei(tmp_path, luft)], 5.0, 0.0, 20.0, luft=9.0)
    assert f["stufe"] == "ok"
