"""Gegenprüfung der tiefen Übergänge beim 3D-Schlichten (relief3d.tief_uebergaenge, 3D → Fräsbahnen 25.10.2026,
Punkt 2): mit dem feinen Eilgang-Prüfer (werkzeuge/eilgang_pruefen.py) und der Kollisionsprüfung – kein Eilgang
durch Material. Gefunden und behoben: mit Planfräsen davor lagen die Übergänge um die Plantiefe zu tief."""
import os
import sys

import pytest

pytest.importorskip("hirch_cam.hirch_relief", reason="hirch_relief nicht gebaut")
pytest.importorskip("inkex")
sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "werkzeuge"))
import eilgang_pruefen as ep  # noqa: E402
from test_relief3d import ZWEI, linsen_zeichnung, mit_ocl, zeichnung  # noqa: E402

S = {"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 3, "aufmass": 0.5}
F = {"id": "f1", "art": "relief_schlichten", "werkzeug": "K", "bahnabstand": 20, "strategie": "kombiniert",
     "auslassen_fertig": True}
P = {"id": "p1", "art": "planen", "werkzeug": "C", "tiefe": 1.5, "zustellung": 1.5, "bahnabstand": 60,
     "richtung": "x"}


def _pruefen(erg, b, h):
    assert erg["ok"], erg.get("warnungen")
    assert not [x for x in erg.get("pruefung", []) if "Eilgang" in x["text"]], erg.get("pruefung")
    assert ep.pruefen(erg, aussen_frei=(0, 0, b, h)) == []


@mit_ocl
@pytest.mark.parametrize("bea", [[P, S, F], [F, S], [S, F]], ids=["planen_davor", "schlichten_zuerst", "normal"])
def test_kein_eilgang_durch_material(tmp_path, bea):
    from hirch_cam import dokument, speicher
    speicher.SPEICHER.leeren()
    svg = zeichnung(tmp_path, bea)
    _pruefen(dokument.rechnen(svg, str(tmp_path / "aus")), 100, 90)


@mit_ocl
def test_zweiseitig_mit_stegen_und_planen(tmp_path):
    from hirch_cam import dokument, speicher
    speicher.SPEICHER.leeren()
    bea = [dict(P, auf_seite=2), dict(S, auf_seite=1), dict(F, auf_seite=1), dict(S, id="s3", auf_seite=2),
           dict(F, id="f3", auf_seite=2)]
    svg, mat = linsen_zeichnung(tmp_path, bearbeitungen=bea, zweiseitig=ZWEI)
    _pruefen(dokument.rechnen(svg, str(tmp_path / "aus")), mat[0], mat[1])
