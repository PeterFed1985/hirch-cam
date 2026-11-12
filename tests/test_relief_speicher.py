"""Schneller rechnen: Zwischenspeicher und gleichzeitiges Rechnen der 3D-Bearbeitungen
(ops/relief_speicher.py). Der G-Code bleibt Byte für Byte gleich."""
import hashlib

import pytest

hirch_relief = pytest.importorskip("hirch_cam.hirch_relief", reason="Modul hirch_relief nicht gebaut")
pytest.importorskip("inkex")

OPS = [{"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 4, "aufmass": 0.5, "farbe": "#5e5c64"},
       {"id": "f1", "art": "relief_schlichten", "werkzeug": "K", "bahnabstand": 10, "farbe": "#9a96a0"}]


def _md5(erg):
    return [hashlib.md5(open(d, "rb").read()).hexdigest() for d in erg["dateien"]] + \
           [hashlib.md5(open(p["datei"], "rb").read()).hexdigest() for p in erg["programme"]]


def _zaehlen(monkeypatch):
    from hirch_cam.ops import relief3d
    gerechnet = []
    orig = relief3d.bearbeiten

    def zaehlen(art, *a, **k):
        gerechnet.append(art)
        return orig(art, *a, **k)
    monkeypatch.setattr(relief3d, "bearbeiten", zaehlen)
    return gerechnet


def test_packen_genau():
    from hirch_cam.model import Move, Toolpath
    from hirch_cam.ops import relief_speicher as rs
    tp = Toolpath(name="x", senkrecht=3)
    tp.moves = [Move(True, 0.1, 1 / 3, 5.0), Move(False, 2 ** 0.5, -1e-12, -3.25, 1234.5)]
    tp2, warn = rs.auspacken(rs.packen(tp, ["w"]))
    assert tp2 == tp and warn == ["w"]
    assert type(tp2.moves[0].rapid) is bool and tp2.moves[0].feed is None


def test_schluessel(tmp_path):
    import base64
    import gzip
    from hirch_cam.ops import relief_speicher as rs
    roh = b"solid x\nendsolid x\n"
    a = base64.b64encode(gzip.compress(roh, mtime=1)).decode()
    b = base64.b64encode(gzip.compress(roh, mtime=2)).decode()
    auftrag = {"reliefs": [{"id": "r", "daten": a}], "hoehen": {"materialdicke": 20}, "objekte": {"x": 1}}
    args = ("relief_schruppen", {"zustellung": 4}, {"durchmesser": 6}, auftrag, 1, 5, 2, (0, 0, 20), "S", 0.01)
    k = rs.schluessel(*args)
    # gleiches Modell neu eingebettet, andere 2D-Objekte, Entwurf (nur Schlichten): gleicher Schlüssel
    auftrag2 = dict(auftrag, reliefs=[{"id": "r", "daten": b}], objekte={}, entwurf=True)
    assert rs.schluessel(*(args[:3] + (auftrag2,) + args[4:])) == k
    # andere Zustellung: anderer Schlüssel
    assert rs.schluessel(args[0], {"zustellung": 3}, *args[2:]) != k
    assert rs.schluessel("relief_schlichten", *args[1:3], auftrag2, *args[4:]) != \
        rs.schluessel("relief_schlichten", *args[1:3], auftrag, *args[4:])


def test_zweites_mal_aus_dem_speicher(tmp_path, monkeypatch):
    if not hirch_relief.mit_ocl():
        pytest.skip("ohne OpenCAMLib gebaut")
    from hirch_cam import dokument
    from test_relief3d import zeichnung
    monkeypatch.setenv("HIRCH_3D_SPEICHER", str(tmp_path / "speicher"))
    svg = zeichnung(tmp_path, OPS)
    monkeypatch.setenv("HIRCH_3D_SPEICHER", "aus")
    frisch = dokument.rechnen(svg, str(tmp_path / "a"), "k")
    monkeypatch.setenv("HIRCH_3D_SPEICHER", str(tmp_path / "speicher"))
    erst = dokument.rechnen(svg, str(tmp_path / "b"), "k")         # gleichzeitig gerechnet
    gerechnet = _zaehlen(monkeypatch)
    zweit = dokument.rechnen(svg, str(tmp_path / "c"), "k")
    assert frisch["ok"] and erst["ok"] and zweit["ok"]
    assert gerechnet == []                                         # nichts neu gerechnet
    assert _md5(frisch) == _md5(erst) == _md5(zweit)
    assert frisch["pruefung"] == zweit["pruefung"]
    # Entwurf: nur das Schlichten neu, das Schruppen kommt aus dem Speicher
    dokument.rechnen(svg, str(tmp_path / "d"), "k", entwurf=True)
    assert gerechnet == ["relief_schlichten"]


def test_kindprozess(tmp_path, monkeypatch):
    """Das Kind (python -m hirch_cam.ops.relief_speicher) rechnet dasselbe wie hier."""
    import pickle
    import subprocess
    import sys
    from hirch_cam.ops import relief_speicher as rs
    from hirch_cam.ops import relief3d
    from test_relief3d import SCHAFT, auftrag_aus, zeichnung
    _info, auftrag = auftrag_aus(zeichnung(tmp_path, []))
    p, w = {"zustellung": 4, "aufmass": 0.5}, dict(SCHAFT, vorschub=2000, eintauchen=500)
    args = ("relief_schruppen", p, w, auftrag, 1, 5.0, 2.0, (0.0, 0.0, 20.0), "3D-Schruppen", 0.01)
    with open(tmp_path / "ein", "wb") as fh:
        pickle.dump(args, fh)
    import os
    paket = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(rs.__file__))))
    subprocess.run([sys.executable, "-m", "hirch_cam.ops.relief_speicher", str(tmp_path / "ein"),
                    str(tmp_path / "aus")], check=True, cwd=str(tmp_path), env=dict(os.environ, PYTHONPATH=paket))
    d = rs._lesen(str(tmp_path / "aus"))
    tp, _w = rs.auspacken(d)
    tp0, _w0 = relief3d.bearbeiten(*args)
    assert tp.moves == tp0.moves
