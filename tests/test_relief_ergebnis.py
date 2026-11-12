"""Fräsergebnis im Reiter «3D» (ops/relief_ergebnis.py, hirch_relief.abtragen)."""
import json
import struct

import numpy as np
import pytest

hirch_relief = pytest.importorskip("hirch_cam.hirch_relief", reason="Modul hirch_relief nicht gebaut")
pytest.importorskip("inkex")

from hirch_cam.ops import relief_ergebnis as re_  # noqa: E402


def test_abtragen_rille_und_kugel():
    H = np.zeros((101, 101), np.float32)
    pts = np.array([[10, 50, 5], [10, 50, -2], [90, 50, -2], [90, 50, 5]], float)
    hirch_relief.abtragen(H, 0, 0, 1.0, pts, {"typ": "schaft", "durchmesser": 6}, [0, 0, 20], -10)
    assert H[50, 50] == pytest.approx(-2) and H[53, 50] == pytest.approx(-2) and H[54, 50] == 0
    H = np.zeros((101, 101), np.float32)
    hirch_relief.abtragen(H, 0, 0, 1.0, pts, {"typ": "kugel", "durchmesser": 6}, [0, 0, 20], -10)
    assert H[50, 50] == pytest.approx(-2)                 # Mitte der Kugel
    assert H[52, 50] == pytest.approx(-2 + 3 - np.sqrt(5), abs=1e-4)   # 2 mm daneben
    H = np.zeros((11, 11), np.float32)                    # nie tiefer als der Boden
    hirch_relief.abtragen(H, 0, 0, 1.0, np.array([[5, 5, -30.0]]), {"typ": "schaft", "durchmesser": 2},
                          [5, 5, 5], -10)
    assert H.min() == pytest.approx(-10)


def _lesen(datei):
    with open(datei, "rb") as fh:
        nx, ny, x0, y0, z = struct.unpack("<ii3f", fh.read(20))
        n = nx * ny
        oben = np.frombuffer(fh.read(4 * n), "<f4").reshape(ny, nx)
        unten = np.frombuffer(fh.read(4 * n), "<f4").reshape(ny, nx)
        ko = np.frombuffer(fh.read(n), np.uint8).reshape(ny, nx)
        ku = np.frombuffer(fh.read(n), np.uint8).reshape(ny, nx)
    return nx, ny, z, oben, unten, ko, ku


def test_kuppel_geschruppt_und_geschlichtet(tmp_path):
    if not hirch_relief.mit_ocl():
        pytest.skip("ohne OpenCAMLib gebaut")
    from hirch_cam import dokument
    from test_relief3d import zeichnung
    svg = zeichnung(tmp_path, [
        {"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 4, "aufmass": 1.0,
         "farbe": "#5e5c64"}])
    erg = dokument.rechnen(svg, str(tmp_path / "aus"), "kuppel")
    assert erg["ok"]
    nur_schruppen = re_.ergebnis(erg["programme"], (100, 90, 25), _reliefs(svg), {}, str(tmp_path / "e1"))
    assert nur_schruppen["anteil_rest"] > 20 and nur_schruppen["zu_tief"] == 0    # Aufmass bleibt stehen
    svg = zeichnung(tmp_path, [
        {"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 4, "aufmass": 1.0,
         "farbe": "#5e5c64"},
        {"id": "f1", "art": "relief_schlichten", "werkzeug": "K", "bahnabstand": 10, "farbe": "#9a96a0"}])
    erg = dokument.rechnen(svg, str(tmp_path / "aus"), "kuppel")
    fertig = re_.ergebnis(erg["programme"], (100, 90, 25), _reliefs(svg), {}, str(tmp_path / "e2"),
                          "fein")
    assert fertig["zu_tief"] == 0, fertig                  # nie ins Modell
    assert fertig["anteil_rest"] < 1.0, fertig             # geschlichtet: fast nichts bleibt stehen
    nx, ny, z, oben, unten, ko, ku = _lesen(fertig["datei"])
    assert (nx, ny) == (fertig["nx"], fertig["ny"]) and np.all(unten == -25)
    assert oben.min() < -8                                 # bis zum Fuss der Kuppel


def _reliefs(svg):
    import inkex
    from hirch_cam import svg_lesen
    return svg_lesen.lesen(inkex.load_svg(svg).getroot())["reliefs"]


def test_befehl(tmp_path):
    from hirch_cam import dokument
    from test_relief3d import zeichnung
    svg = zeichnung(tmp_path, [])
    prog = tmp_path / "programme.json"
    prog.write_text("[]")
    import io
    import contextlib
    aus = io.StringIO()
    with contextlib.redirect_stdout(aus):
        dokument.main(["relief", "ergebnis", svg, "--programme", str(prog), "--ordner", str(tmp_path / "o"),
                       "--genauigkeit", "grob"])
    erg = json.loads(aus.getvalue())
    assert erg["ok"] and erg["zu_tief"] == 0 and erg["anteil_rest"] > 0   # nichts gefräst: alles bleibt


def _karte(datei):
    with open(datei, "rb") as fh:
        nx, ny, x0, y0, z = struct.unpack("<ii3f", fh.read(20))
        n = nx * ny
        fh.read(4 * n * 2 + n * 2 + 4 * n)
        ko = np.frombuffer(fh.read(2 * n), "<u2").reshape(ny, nx)
        ku = np.frombuffer(fh.read(2 * n), "<u2").reshape(ny, nx)
    return ko, ku


def test_karte_zuletzt_tiefer_gemacht_von(tmp_path):
    """«Färben nach: Bearbeitung» (Peter, 22.10.): je Stelle die Bearbeitung der tiefsten Stelle, erst
    ab 0,02 mm unter der Oberfläche (wie hirch-abtrag). Schruppen mit Aufmass, dann Schlichten: auf
    der Kuppel das Schlichten, daneben (vom Schlichten nicht tiefer) das Schruppen; unten nichts."""
    if not hirch_relief.mit_ocl():
        pytest.skip("ohne OpenCAMLib gebaut")
    from hirch_cam import dokument
    from test_relief3d import zeichnung
    svg = zeichnung(tmp_path, [
        {"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 4, "aufmass": 1.0,
         "farbe": "#5e5c64"},
        {"id": "f1", "art": "relief_schlichten", "werkzeug": "K", "bahnabstand": 10, "farbe": "#9a96a0"}])
    erg = dokument.rechnen(svg, str(tmp_path / "aus"), "kuppel")
    assert erg["ok"]
    e = re_.ergebnis(erg["programme"], (100, 90, 25), _reliefs(svg), {}, str(tmp_path / "e"))
    ids = [k["id"] for k in e["karte"]]
    assert ids == ["s1", "f1"], e["karte"]
    assert all(k["farbe"] for k in e["karte"])
    ko, ku = _karte(e["datei"])
    werte = set(np.unique(ko).tolist())
    assert {1, 2} <= werte, werte                  # beide Bearbeitungen sichtbar
    # auf der Flanke der Kuppel (15 mm neben der Mitte; die Spitze bleibt Materialoberfläche): geschlichtet
    z = e["zelle"]
    flanke = ko[int(round(45 / z)), int(round(65 / z))]
    assert flanke == 2, flanke
    assert not ku.any()                            # Unterseite unberührt
    assert re_.MINDESTTIEFE == pytest.approx(0.02)


def test_weg_anfaenge_wie_tap_lesen(tmp_path):
    """Die Zuordnung Werkzeugweg → Bearbeitung zählt die Wege nach derselben Regel wie tap_mm.read."""
    from hirch_cam.post import tap_mm
    text = "\n".join(["T1M6", "G17", "G0Z20.000", "G0X0.000Y0.000S12000M3",
                      "G0X1.000Y1.000Z5.000", "G1Z-1.000F500", "G1X5.000",
                      "G0Z5.000", "G0X10.000Y1.000Z5.000", "G1Z-1.000", "G1X15.000",
                      "G0Z20.000", "G0X0.000Y0.000", "M30"])
    job = tap_mm.read(text)
    assert len(re_._weg_anfaenge(text)) == len(job.toolpaths) == 2
    p = {"abschnitte": [{"zeile": 5, "key": "a#1"}, {"zeile": 9, "key": "b"}]}
    assert re_._weg_schluessel(p, text, 2) == ["a", "b"]
