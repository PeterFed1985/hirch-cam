"""B-2 (Peter 26.10., Schnittstelle Basis 30.10.): 2D-Schlichten einer Kontur mit eigener Tiefe `schlicht_tiefe` –
das Schruppen lässt in Z Material stehen, damit das Werkstück verbunden bleibt; das Schlichten fräst auf die
Endtiefe. Prüfsteine: ohne den Wert G-Code unverändert; tiefer: derselbe Abtrag wie Schruppen und Schlichten auf die
Endtiefe, kein Eilgang ins Material, Haltestege nur so hoch wie die stehen gelassene Haut (Hinweis); flacher: Hinweis."""
import math
import os
import re
import sys

import numpy as np
import pytest

pytest.importorskip("shapely")
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "werkzeuge"))
import eilgang_pruefen as ep  # noqa: E402

from hirch_cam import engine  # noqa: E402
from hirch_cam.post import tap_mm  # noqa: E402
from hirch_cam.simulation import Hoehenfeld  # noqa: E402

B, H, DICKE = 100, 70, 10.0


def _rechteck(x0, y0, x1, y1):
    return [[x0, y0], [x1, y0], [x1, y1], [x0, y1], [x0, y0]]


def _auftrag(tmp_path, name, **k):
    ordner = tmp_path / name
    ordner.mkdir(exist_ok=True)
    kontur = {"id": "k", "art": "kontur", "werkzeug": "C", "tiefe": 9.0, "zustellung": 3, "seite": "aussen",
              "schlichten": True, "schlicht_zugabe": 0.3, "schlicht_werkzeug": "D", "stege_modus": "keine",
              "objekte": [{"punkte": _rechteck(20, 15, 80, 55), "geschlossen": True}]}
    kontur.update(k)
    return {
        "werkzeuge": {"C": {"typ": "schaft", "durchmesser": 6, "nummer": 1, "vorschub": 1000, "eintauchen": 300},
                      "D": {"typ": "schaft", "durchmesser": 4, "nummer": 2, "vorschub": 900, "eintauchen": 250,
                            "vollnut": 0.6}},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": DICKE},
        "postprozessor": "vcarve_mm", "material_flaeche": [B, H],
        "bearbeitungen": [kontur], "ausgabe": str(ordner / "teil.tap")}


def _texte(erg):
    return {os.path.basename(d): open(d, newline="").read() for d in erg["dateien"]}


def _tiefste(erg, d):
    p = next(p for p in erg["programme"] if abs(float(p["durchmesser"]) - d) < 1e-9)
    zs = [float(z) for z in re.findall(r"Z(-?[0-9.]+)", open(p["datei"]).read())]
    return min(zs)


def _feld(erg):
    f = Hoehenfeld(-10, -10, B + 10, H + 10, 0.1)
    for p in erg["programme"]:
        with open(p["datei"], newline="") as fh:
            job = tap_mm.read(fh.read())
        f.fraesen(job, p.get("form") or "flach", float(p["durchmesser"]), 60)
    return f.H


def _ohne_kanten(H0, d, tol=0.05):
    from scipy import ndimage
    rand = (ndimage.maximum_filter(H0, 3) - ndimage.minimum_filter(H0, 3)) > tol
    return int(((np.abs(d) > tol) & ~rand).sum())


def test_ohne_eigene_tiefe_unveraendert(tmp_path):
    a = _texte(engine.rechnen(_auftrag(tmp_path, "a")))
    assert a == _texte(engine.rechnen(_auftrag(tmp_path, "b", schlicht_tiefe=None)))
    assert a == _texte(engine.rechnen(_auftrag(tmp_path, "c", schlicht_tiefe=9.0)))   # gleich wie die Tiefe


def test_schlichten_tiefer_als_das_schruppen(tmp_path):
    erg = engine.rechnen(_auftrag(tmp_path, "t", schlicht_tiefe=DICKE + 0.2))
    assert erg["ok"] and ep.pruefen(erg) == []
    assert _tiefste(erg, 6) == pytest.approx(-9.0)            # Schruppen: lässt 1 mm stehen
    assert _tiefste(erg, 4) == pytest.approx(-10.2)           # Schlichten: ganz durch
    # das Teil ist dasselbe wie mit Schruppen auf die Endtiefe + Schlichten wie bisher (auf der Abfallseite fehlt
    # unten nur, was der breitere Schrupp-Fräser mehr weggenommen hätte) …
    ref = engine.rechnen(_auftrag(tmp_path, "r", tiefe=DICKE + 0.2))
    H0, H1 = _feld(ref), _feld(erg)
    teil = np.zeros(H0.shape, bool)
    teil[int((15 - 1 + 10) / 0.1):int((55 + 1 + 10) / 0.1), int((20 - 1 + 10) / 0.1):int((80 + 1 + 10) / 0.1)] = True
    assert _ohne_kanten(H0, np.where(teil, H1 - H0, 0.0)) == 0
    # … und ringsum ganz durch (Mitte der Schlicht-Fuge Ø 4)
    fuge = [H1[int((y + 10) / 0.1), int((x + 10) / 0.1)] for x in np.arange(20, 80, 0.5) for y in (13.0, 57.0)]
    assert max(fuge) == pytest.approx(-10.2, abs=0.05)
    assert H1.min() >= -10.2 - 1e-3
    # in der Ablaufliste: Schlichten bis zur eigenen Tiefe, ganz durch
    ops = [o for s in erg["ablauf"]["seiten"] for ab in s["abschnitte"] for o in ab["ops"]]
    sch = next(o for o in ops if o["teil"] == "schlicht")
    assert sch["tiefe"] == pytest.approx(10.2) and sch["durch"]
    # unter der Tiefe des Schruppens nie tiefer zugestellt als beim Schruppen (3 mm)
    p = next(p for p in erg["programme"] if abs(float(p["durchmesser"]) - 4) < 1e-9)
    ebenen = sorted({round(float(z), 3) for z in re.findall(r"Z(-[0-9.]+)", open(p["datei"]).read())
                     if float(z) < -9.0 - 1e-6})
    assert ebenen and ebenen[0] == pytest.approx(-10.2)
    assert all(b - a <= 3.0 + 1e-6 for a, b in zip(ebenen, ebenen[1:] + [-9.0]))


def test_haltestege_so_hoch_wie_die_haut(tmp_path):
    erg = engine.rechnen(_auftrag(tmp_path, "s", schlicht_tiefe=DICKE + 0.2, stege_modus="auto", stege_anzahl=4,
                                  stege_breite=6, stege_hoehe=1.5))
    assert erg["ok"] and ep.pruefen(erg) == []
    w = erg["op_warnungen"].get("k", [])
    assert engine.STEGE_NIEDRIGER % ("1,2", "1,5") in w, w
    # die Stege stehen (im Schnitt bleibt Material bis zur Tiefe des Schruppens)
    Hf = _feld(erg)
    f = Hoehenfeld(-10, -10, B + 10, H + 10, 0.1)
    ring = []
    for x in np.arange(20, 80, 0.5):
        for y in (15 - 2.0, 55 + 2.0):                        # Mitte der Schnittfuge (Ø 4, aussen)
            i, j = int((x + 10) / 0.1), int((y + 10) / 0.1)
            ring.append(Hf[j, i])
    assert min(ring) == pytest.approx(-10.2, abs=0.05)        # durch …
    assert max(ring) > -9.3                                   # … ausser an den Stegen
    del f


def test_schlichten_flacher_als_das_schruppen(tmp_path):
    erg = engine.rechnen(_auftrag(tmp_path, "f", schlicht_tiefe=5.0))
    assert erg["ok"] and ep.pruefen(erg) == []
    assert _tiefste(erg, 4) == pytest.approx(-5.0)
    w = erg["op_warnungen"].get("k", [])
    assert engine.SCHLICHT_FLACHER % ("5", "9", "0,3") in w, w
    assert math.isclose(_tiefste(erg, 6), -9.0)
