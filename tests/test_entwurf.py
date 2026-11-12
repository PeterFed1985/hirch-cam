"""3D-Entwurfsmodus: Schlichten gröber und schneller; genau gerechnet bleibt alles wie bisher."""
import pytest

hirch_relief = pytest.importorskip("hirch_cam.hirch_relief", reason="Modul hirch_relief nicht gebaut")
pytest.importorskip("inkex")


def test_entwurf_groeber_und_markiert(tmp_path):
    if not hirch_relief.mit_ocl():
        pytest.skip("ohne OpenCAMLib gebaut")
    from hirch_cam import dokument
    from hirch_cam.post import tap_mm
    from test_relief3d import zeichnung
    svg = zeichnung(tmp_path, [
        {"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 4, "aufmass": 0.5, "farbe": "#5e5c64"},
        {"id": "f1", "art": "relief_schlichten", "werkzeug": "K", "bahnabstand": 10, "farbe": "#9a96a0"}])

    def bewegungen(erg):
        return [sum(len(tp.moves) for tp in tap_mm.read(open(p["datei"], newline="").read()).toolpaths)
                for p in erg["programme"]]
    genau = dokument.rechnen(svg, str(tmp_path / "g"), "k")
    entwurf = dokument.rechnen(svg, str(tmp_path / "e"), "k", entwurf=True)
    assert genau["ok"] and entwurf["ok"]
    assert genau["entwurf"] is False and entwurf["entwurf"] is True
    g, e = bewegungen(genau), bewegungen(entwurf)
    assert e[0] == g[0]                          # Schruppen unverändert
    assert e[1] < g[1] / 2                        # Schlichten deutlich gröber


def test_ohne_3d_kein_entwurf(tmp_path):
    from hirch_cam import dokument
    from test_relief3d import zeichnung
    svg = zeichnung(tmp_path, [{"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 4,
                                "aufmass": 0.5, "aktiv": False, "farbe": "#5e5c64"}], mit_modell=False)
    erg = dokument.rechnen(svg, str(tmp_path / "o"), "k", entwurf=True)
    assert not erg.get("entwurf")


def test_fortschritt_datei(tmp_path):
    """rechnen --fortschritt: die Datei nennt den letzten Schritt (die Prüfung)."""
    import contextlib
    import io
    import json
    from hirch_cam import dokument
    from test_relief3d import zeichnung
    svg = zeichnung(tmp_path, [{"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 4,
                                "aufmass": 0.5, "farbe": "#5e5c64"}])
    datei = tmp_path / "fortschritt.json"
    with contextlib.redirect_stdout(io.StringIO()):
        dokument.main(["rechnen", svg, "--ordner", str(tmp_path / "a"), "--fortschritt", str(datei)])
    assert json.loads(datei.read_text(encoding="utf-8"))["text"] == "Prüfung"
    from hirch_cam import fortschritt
    fortschritt.setzen(None)
