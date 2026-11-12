"""Kollisionsprüfung mit rundem Material in der (quadratischen) Vorrichtung: neben dem Kreis ist Luft,
der Schraubenkopf in der Mitte ist ein Hindernis (Peter, 21.10.)."""
import pytest

from hirch_cam.model import Job, Move, Toolpath

np = pytest.importorskip("numpy")

RUND = {"mitte": [50.0, 50.0], "radius": 40.0, "schraube": False}


def _pruefen(moves, rund, d=6.0):
    from hirch_cam.kollision import pruefen
    job = Job(toolpaths=[Toolpath(moves=moves)], safe_z=20.0)
    prog = [{"datei": "x", "name": "T", "seite": 1, "durchmesser": d, "form": "flach"}]
    return pruefen(prog, {"breite": 100, "hoehe": 100, "dicke": 20, "rund": rund}, lambda _d: job)


def test_eilgang_neben_dem_kreis_trifft_nichts():
    # Ecke der quadratischen Fläche: beim runden Teil ist dort kein Material
    m = [Move(True, 5, 5, 5), Move(True, 5, 5, -5), Move(True, 95, 5, -5)]
    assert _pruefen(m, RUND) == []
    # ohne rund: dasselbe ist ein Treffer
    assert _pruefen(m, None)


def test_eilgang_in_den_kreis_wird_gemeldet():
    m = [Move(True, 5, 5, -5), Move(True, 50, 20, -5)]
    funde = _pruefen(m, RUND)
    assert funde and funde[0]["art"] == "material"
    x, y = funde[0]["ort"]
    assert (x - 50) ** 2 + (y - 50) ** 2 <= (40 + 3 + 1) ** 2


def test_schraubenkopf_ist_ein_hindernis():
    ru = dict(RUND, schraube=True, kopf=12.0, bohrung=6.5, ueberstand=3.0)
    # Eilgang über die Mitte knapp über dem Material, aber unter der Oberkante des Kopfs
    m = [Move(True, 10, 50, 1.0), Move(True, 90, 50, 1.0)]
    funde = _pruefen(m, ru)
    assert [f["art"] for f in funde] == ["schraube"]
    assert abs(funde[0]["tief"] - 2.0) < 1e-6
    # hoch genug: frei
    assert _pruefen([Move(True, 10, 50, 3.5), Move(True, 90, 50, 3.5)], ru) == []
    # vorbei: frei
    assert _pruefen([Move(True, 10, 70, 1.0), Move(True, 90, 70, 1.0)], ru) == []


def test_vorschub_in_den_schraubenkopf_wird_gemeldet():
    ru = dict(RUND, schraube=True, kopf=12.0, bohrung=6.5, ueberstand=0.0)
    m = [Move(True, 30, 50, 1.0), Move(False, 30, 50, -2, 300), Move(False, 50, 50, -2, 1000)]
    funde = _pruefen(m, ru)
    assert any(f["art"] == "schraube" for f in funde)


def test_pruefung_meldet_schraubenkopf():
    from hirch_cam import pruefung
    import inspect
    assert "Schraubenkopf" in inspect.getsource(pruefung)
