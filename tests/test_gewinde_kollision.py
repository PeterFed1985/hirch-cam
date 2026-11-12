"""Gewindefräser: Kollisionsprüfung (Peter, 22.10.). Der Gewindefräser taucht in der Mitte des Kernlochs (bzw.
neben dem Zapfen) auf volle Tiefe und schneidet nur die Gänge – vorher muss dort frei sein. In der Höhenkarte
trägt er selbst nichts ab (die Gänge sind Hinterschneidungen)."""
import math

import pytest

from hirch_cam import engine
from hirch_cam.post import tap_mm

np = pytest.importorskip("numpy")

KREIS = [(20 + 4 * math.cos(a / 90 * 2 * math.pi), 20 + 4 * math.sin(a / 90 * 2 * math.pi)) for a in range(91)]
KERN = [(20 + 3.3 * math.cos(a / 90 * 2 * math.pi), 20 + 3.3 * math.sin(a / 90 * 2 * math.pi)) for a in range(91)]


def _rechnen(tmp_path, kernloch_werkzeug="", bohren_tiefe=None, tiefe=10):
    bs = []
    if bohren_tiefe is not None:
        bs.append({"id": "b", "art": "bohren", "werkzeug": "B", "tiefe": bohren_tiefe, "zustellung": 1.5,
                   "methode": "helix", "objekte": [{"punkte": [list(p) for p in KERN], "geschlossen": True}]})
    bs.append({"id": "g", "art": "gewinde", "werkzeug": "G", "tiefe": tiefe, "kernloch_werkzeug": kernloch_werkzeug,
               "objekte": [{"punkte": [list(p) for p in KREIS], "geschlossen": True}]})
    auftrag = {
        "werkzeuge": {"G": {"typ": "gewinde", "durchmesser": 5, "zaehne": 1, "nummer": 2, "vorschub": 400,
                            "eintauchen": 200},
                      "B": {"typ": "schaft", "durchmesser": 3, "nummer": 1, "vorschub": 1000, "eintauchen": 300,
                            "empfehlung": {"zustellung": 1.5}}},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 15},
        "postprozessor": "vcarve_mm", "reihenfolge": "liste",
        "bearbeitungen": bs,
        "ausgabe": str(tmp_path / "g.tap"),
    }
    erg = engine.rechnen(auftrag)
    assert erg["ok"], erg
    return erg


def _funde(erg):
    from hirch_cam.kollision import pruefen

    def lesen(datei):
        with open(datei, newline="") as fh:
            return tap_mm.read(fh.read())
    return [f for f in pruefen(erg["programme"], {"breite": 40, "hoehe": 40, "dicke": 15}, lesen, erg["gewinde"])
            if f["art"] == "gewinde"]


def test_mit_kernloch_im_selben_gewinde_frei(tmp_path):
    erg = _rechnen(tmp_path, kernloch_werkzeug="B")
    g = erg["gewinde"]
    assert len(g) == 1 and g[0]["name"] == "M8" and g[0]["innen"]
    assert g[0]["mitte"] == pytest.approx([20, 20], abs=1e-3) and g[0]["kernloch"] == pytest.approx(6.6475, abs=0.01)
    assert [p["typ"] for p in erg["programme"]] == ["schaft", "gewinde"]      # Kernloch vor dem Gewinde
    assert _funde(erg) == []


def test_ohne_kernloch_warnung(tmp_path):
    erg = _rechnen(tmp_path)
    f = _funde(erg)
    assert len(f) == 1 and f[0]["nichts"] and f[0]["gewinde"] == "M8"
    assert f[0]["tief"] == pytest.approx(10, abs=0.3)


def test_kernloch_zu_flach_fehler(tmp_path):
    erg = _rechnen(tmp_path, bohren_tiefe=5)
    f = _funde(erg)
    assert len(f) == 1 and not f[0]["nichts"]
    assert f[0]["tief"] == pytest.approx(5, abs=0.3)
    # tief genug gebohrt: frei
    assert _funde(_rechnen(tmp_path, bohren_tiefe=10.5)) == []


def test_pruefung_texte():
    import inspect
    from hirch_cam import pruefung
    q = inspect.getsource(pruefung)
    assert "kein Kernloch im Programm" in q and "Kernloch zu flach oder zu klein" in q
