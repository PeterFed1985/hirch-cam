"""Peters Flügel (tests/daten/fluegel.hir – Freigabe Peter 30.10.: dieses Modell darf als Testobjekt ins Repository).

B-8: 3D-Schruppen – die Zustellung wurde mit der Tiefe immer kleiner. Ursache: _flache_hoehen zählte auf der
sanft gewölbten Unterseite jedes 0,05-mm-Band nahe dem Scheitel als «waagrecht» (Neigung unter ~1°) und setzte dort
je eine Ebene (Seite 2: 17 Ebenen zwischen −4,3 und −6,5 mm). Jetzt zählen nur wirklich ebene Flächen.
"""
import os

import numpy as np
import pytest

HIER = os.path.dirname(os.path.abspath(__file__))
FLUEGEL = os.path.join(HIER, "daten", "fluegel.hir")

pytest.importorskip("hirch_cam.hirch_relief")


def _auftrag(tmp_path, monkeypatch):
    from hirch_cam import dokument, engine
    gefangen = {}

    def fangen(a, *args, **kw):
        gefangen["a"] = a
        return {"ok": False, "warnungen": []}
    monkeypatch.setattr(engine, "rechnen", fangen)
    dokument.rechnen(FLUEGEL, str(tmp_path / "aus"), "m")
    return gefangen["a"]


def _ebenen(a, bid):
    """Ebenen des 3D-Schruppens bid, wie relief3d.schruppen sie wählt (ohne die Bahnen zu rechnen)."""
    from hirch_cam.ops import relief3d as R
    from hirch_cam.ops.boeden_waende import schrupp_ebenen
    b = next(x for x in a["bearbeitungen"] if x["id"] == bid)
    w = a["werkzeuge"][b["werkzeug"]]
    flaeche = a.get("material_flaeche")
    material = (float(flaeche[0]), float(flaeche[1]), float(a["hoehen"]["materialdicke"]))
    d = float(w["durchmesser"])
    warn = []
    tri, extra, _g = R._modelle(a, b, b["auf_seite"], material, d + d + 2, warn)
    lo, hi = tri.reshape(-1, 3).min(0), tri.reshape(-1, 3).max(0)
    bx = R._bx(lo, hi, d + d + 1, material, R.ueber_rand(b))
    oben, _u, zelle = R._raster(tri, bx, max(0.1, min(1.0, d / 24)))
    maske = np.isfinite(oben)
    z_unten = float(np.nanmin(oben))
    if extra is not None and len(extra):
        xr, _xu, _z = R._raster(extra, bx, zelle, max_punkte=10 ** 12)
        if np.isfinite(xr).any():
            z_unten = min(z_unten, float(np.nanmin(xr)))
    z_unten = max(z_unten, -material[2])
    zust = float(b.get("zustellung") or d / 2)
    aufmass = float(b.get("aufmass") if b.get("aufmass") is not None else 0.5)
    return schrupp_ebenen(oben, maske, zelle, d, zust, aufmass, z_unten), zust


@pytest.mark.parametrize("bid", ["b1", "b3"])
def test_schruppen_zustellung_bleibt(tmp_path, monkeypatch, bid):
    a = _auftrag(tmp_path, monkeypatch)
    ebenen, zust = _ebenen(a, bid)
    abstaende = [o - u for o, u in zip([0.0] + ebenen[:-1], ebenen)]
    eng = [round(x, 2) for x in abstaende if x < zust / 2]
    # höchstens zwei engere Schritte: an einem echten Boden (Höhe + Aufmass) und ganz unten
    assert len(eng) <= 2, (ebenen, eng)


def test_zweiseitig_schlichten_verlaesst_das_modell(tmp_path, monkeypatch):
    """Peter 02.11. (Skizze V1): zweiseitig fährt der Schlichtfräser eine Fräserbreite über den Umriss hinaus."""
    from hirch_cam.ops import relief3d as R
    a = _auftrag(tmp_path, monkeypatch)
    assert R._zweiseitig_da(a)
    gesehen = {}

    def fangen(tri, material, w, p, *args, **kw):
        gesehen["rand"] = p.get("rand")
        from hirch_cam.ops.bahn import Bahnbauer
        return Bahnbauer(1000, 300, 3, 0.5, (0, 0, 20)).weg("x"), []
    monkeypatch.setattr(R, "schlichten", fangen)
    b = next(x for x in a["bearbeitungen"] if x["id"] == "b2")
    w = a["werkzeuge"][b["werkzeug"]]
    R.bearbeiten("relief_schlichten", dict(b, rand=None), w, a, 1, 3.0, 0.5, (0, 0, 20), "b2", 0.01)
    assert gesehen["rand"] == pytest.approx(float(w["durchmesser"]) + 1.0)


def test_gefraester_fluegel_liest_sich(tmp_path, monkeypatch):
    """Peters Flügel, so wie er gefräst wird (tests/daten/fluegel_gefraest.hir, Peter 03.11.): zweiseitig mit Haut,
    3D-Schruppen/-Schlichten je Seite, Taschen und Durchtrennen auf Seite 2. Zum Testen in allen Sitzungen."""
    from hirch_cam import dokument, engine
    gefangen = {}

    def fangen(a, *args, **kw):
        gefangen["a"] = a
        return {"ok": False, "warnungen": []}
    monkeypatch.setattr(engine, "rechnen", fangen)
    dokument.rechnen(os.path.join(HIER, "daten", "fluegel_gefraest.hir"), str(tmp_path / "aus"), "m")
    a = gefangen["a"]
    arten = sorted(b["art"] for b in a["bearbeitungen"] if b.get("id") != "passloecher")
    assert arten.count("relief_schruppen") == 2 and arten.count("relief_schlichten") == 2
    assert arten.count("tasche") >= 4 and "kontur" in arten
    from hirch_cam.ops import relief3d as R, relief_zweiseitig as zs
    assert R._zweiseitig_da(a)
    cfg = zs.einstellungen(a["reliefs"][0]["info"])
    assert cfg.get("haut_an") and cfg.get("trennung") == "trennlinie"


def test_impeller_2d_liest_sich(tmp_path, monkeypatch):
    """Peters Impeller als 2D-Teil (tests/daten/impeller_2d.hir, Peter 03.11.): Beispiel für alle Sitzungen –
    zweiseitig, Taschen, Konturen, Gewinde, Gravur und V-Carve auf Seite 2."""
    from hirch_cam import dokument, engine
    gefangen = {}

    def fangen(a, *args, **kw):
        gefangen["a"] = a
        return {"ok": False, "warnungen": []}
    monkeypatch.setattr(engine, "rechnen", fangen)
    dokument.rechnen(os.path.join(HIER, "daten", "impeller_2d.hir"), str(tmp_path / "aus"), "m")
    arten = {b["art"] for b in gefangen["a"]["bearbeitungen"]}
    assert {"tasche", "kontur", "gewinde"} <= arten
