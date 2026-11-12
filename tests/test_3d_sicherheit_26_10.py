"""Unabhängige Sicherheitsprüfung der neuen 3D-Bahnen (3D → Fräsbahnen 26.10.2026, 19:05, Punkt 3): «Konturen je Form»
und «Über den Materialrand hinaus» – zweiseitig mit Haltestegen/Haut, Planfräsen davor, Passlöcher an (Schrauben in
den Ecken: dort darf nie gefräst werden), «liste»-Reihenfolge, Restmaterial dazwischen.

Geprüft wird je Fall: (1) kein Eilgang durch stehendes Material (feiner Prüfer, werkzeuge/eilgang_pruefen.py),
(2) die Kollisionsprüfung meldet keinen Eilgang, (3) um die Passlöcher fährt ausser dem Bohren der Passlöcher kein
Programm unter der Freifahrhöhe (Sperrkreis = Loch + Fräserradius + 1 mm), (4) nirgends tiefer als das Modell (Höhenfeld 0,25 mm gegen das Modell, 0,1 mm Spiel; ausser an steilen
Rasterkanten, am Blockrand und in den Passlöchern)."""
import json
import math
import os
import sys

import numpy as np
import pytest

pytest.importorskip("hirch_cam.hirch_relief", reason="hirch_relief nicht gebaut")
pytest.importorskip("inkex")
sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "werkzeuge"))
import eilgang_pruefen as ep  # noqa: E402
from test_relief3d import ZWEI, auftrag_aus, linsen_zeichnung, mit_ocl, zeichnung  # noqa: E402

S = {"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 3, "aufmass": 0.5}
R = {"id": "s2", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 3, "aufmass": 0.3, "rest": True}
F = {"id": "f1", "art": "relief_schlichten", "werkzeug": "K", "bahnabstand": 25, "strategie": "konturen",
     "auslassen_fertig": True}
P = {"id": "p1", "art": "planen", "werkzeug": "C", "tiefe": 1.0, "zustellung": 1.0, "bahnabstand": 60,
     "richtung": "x"}
ECKEN = {"an": True, "achse": "x", "passloecher": True, "pass_lage": "ecken", "pass_rand": 6.0, "pass_d": 6.0}


def _welle(b=60.0, h=40.0, n=61):
    """Modell so gross wie der Block: gewellte Oberfläche (4–9 mm hoch) mit einer flachen Insel (Kuppe) in der Mitte,
    Boden auf 0 – überall geneigt, damit das Schlichten mit «Fertiges auslassen» etwas zu tun hat."""
    xs, ys = np.linspace(0, b, n), np.linspace(0, h, n)
    X, Y = np.meshgrid(xs, ys)
    Z = 6.5 + 2.5 * np.sin(2 * np.pi * X / 30) * np.cos(2 * np.pi * Y / 25)
    Z = np.where((X - b / 2) ** 2 + (Y - h / 2) ** 2 < 36, 12.0, Z)
    P = np.dstack([X, Y, Z])
    B = np.dstack([X, Y, np.zeros_like(X)])
    tri = []
    for i in range(n - 1):
        for j in range(n - 1):
            a, b_, c, d = P[i, j], P[i, j + 1], P[i + 1, j + 1], P[i + 1, j]
            tri += [(a, b_, c), (a, c, d)]
            a, b_, c, d = B[i, j], B[i, j + 1], B[i + 1, j + 1], B[i + 1, j]
            tri += [(a, c, b_), (a, d, c)]
    return np.array(tri, np.float32)


def _mit(b, **k):
    x = dict(b)
    x.update(k)
    return x


def _pruefen(erg, b, h, auftrag_zw=None):
    from hirch_cam.post import tap_mm
    assert erg["ok"], erg.get("warnungen")
    assert not [x for x in erg.get("pruefung", []) if "Eilgang" in x["text"]], erg.get("pruefung")
    funde = ep.pruefen(erg, aussen_frei=(0, 0, b, h))
    assert funde == [], [(f["programm"], [round(v, 2) for v in f["ort"]], round(f["z"], 2), round(f["tief"], 2))
                         for f in funde[:5]]
    if not auftrag_zw:
        return
    # Passlöcher: Schrauben/Stifte stehen dort – nur das Bohren der Passlöcher selbst darf hinein
    from hirch_cam.einstellungen import passloecher
    pl = passloecher({"zweiseitig": auftrag_zw}, b, h)
    cz = 3.0
    for p in erg["programme"]:
        if any("pass" in a["key"].lower() for a in p.get("abschnitte") or []):
            continue
        job = tap_mm.read(open(p["datei"], newline="").read())
        rr = float(auftrag_zw.get("pass_d") or 6) / 2 + float(p["durchmesser"]) / 2 + 1.0 - 0.05
        for tp in job.toolpaths:
            pos = (job.home[0], job.home[1], job.safe_z)
            for m in tp.moves:
                if min(pos[2], m.z) < cz - 1e-6:
                    for t in np.linspace(0, 1, 9):
                        x, y = pos[0] + (m.x - pos[0]) * t, pos[1] + (m.y - pos[1]) * t
                        for px, py in pl:
                            # Seite 2 ist gespiegelt – die Passlöcher liegen auf der Achse spiegelbildlich (gleiche Menge)
                            assert math.hypot(x - px, y - py) >= rr, (p["name"], (round(x, 2), round(y, 2)),
                                                                      round(min(pos[2], m.z), 2), (px, py))
                pos = (m.x, m.y, m.z)


def _nicht_zu_tief(svg, erg, b, h, zelle=0.25, grenze=0.1, auftrag_zw=None, plan=0.0):
    """Seite 1: nirgends tiefer als das Modell (Höhenfeld; steile Rasterkanten zählen nicht). plan: Planfräsen davor –
    alle Tiefen zählen dann ab der neuen Oberfläche, das Modell liegt so viel tiefer."""
    from scipy import ndimage
    from hirch_cam.ops import relief3d
    from hirch_cam.post import tap_mm
    from hirch_cam.simulation import Hoehenfeld
    info, _a = auftrag_aus(svg)
    rel = [r for r in info["reliefs"] if int(r.get("seite") or 1) == 1] or info["reliefs"]
    tri, _v = relief3d.modell_im_material(rel[0], info["dicke"])
    feld = Hoehenfeld(0, 0, b, h, zelle)
    for p in erg["programme"]:
        if int(p.get("seite") or 1) != 1:
            continue
        job = tap_mm.read(open(p["datei"], newline="").read())
        form = p.get("form") or "flach"
        d = float(p["durchmesser"])
        feld.fraesen(job, form, d, float(p.get("v_winkel") or 60),
                     eckradius=float(p.get("eckradius") or (d / 2 if form == "kugel" else 0)))
    oben, _u, _z = relief3d._raster(tri, (0, 0, b, h), zelle)
    ny, nx = min(oben.shape[0], feld.ny), min(oben.shape[1], feld.nx)
    o, hh = oben[:ny, :nx], feld.H[:ny, :nx]
    o0 = np.where(np.isfinite(o), o, -1e3)
    sp = ndimage.maximum_filter(o0, size=3) - ndimage.minimum_filter(o0, size=3)
    m = np.isfinite(o) & (sp < 0.6)
    m[:2, :] = m[-2:, :] = False                     # Rand des Blocks: die Seitenflächen liegen im Raster halb drin
    m[:, :2] = m[:, -2:] = False
    if auftrag_zw:
        from hirch_cam.einstellungen import passloecher
        X, Y = np.meshgrid((np.arange(nx) + 0.5) * zelle, (np.arange(ny) + 0.5) * zelle)
        for px, py in passloecher({"zweiseitig": auftrag_zw}, b, h):
            m &= np.hypot(X - px, Y - py) > float(auftrag_zw.get("pass_d") or 6) / 2 + 0.5
    tief = (o - plan - hh)[m]
    assert float(tief.max()) <= grenze, float(tief.max())


@mit_ocl
@pytest.mark.parametrize("fall", ["konturen", "konturen_planen", "kombiniert_liste_rest", "kehlen"])
def test_ueber_rand_mit_passloechern_in_den_ecken(tmp_path, fall):
    """Modell so gross wie der Block (60 × 40), «Über den Materialrand hinaus» 4 mm, Passlöcher in den Ecken."""
    from hirch_cam import dokument, speicher
    speicher.SPEICHER.leeren()
    u = {"ueber_rand": 4}
    bea = {"konturen": [_mit(S, **u), _mit(F, **u)],
           "konturen_planen": [P, _mit(S, **u), _mit(F, **u)],
           "kombiniert_liste_rest": [_mit(S, **u), _mit(R, **u), _mit(F, strategie="kombiniert", **u)],
           "kehlen": [_mit(S, **u), _mit(F, strategie="kehlen", **u)]}[fall]
    svg = zeichnung(tmp_path, bea, dreiecke=_welle(), material=(60, 40, 20), zweiseitig=ECKEN)
    erg = dokument.rechnen(svg, str(tmp_path / "aus"))
    _pruefen(erg, 60, 40, ECKEN)
    _nicht_zu_tief(svg, erg, 60, 40, auftrag_zw=ECKEN, plan=P["tiefe"] if fall == "konturen_planen" else 0.0)
    assert any(int(p.get("seite") or 1) == 1 and p.get("form") == "kugel" for p in erg["programme"])


@mit_ocl
@pytest.mark.parametrize("strategie", ["konturen", "kehlen"])
def test_zweiseitig_stege_haut_planen(tmp_path, strategie):
    """Linse zweiseitig mit Haltestegen und Haut, Planfräsen auf Seite 2, Schlichten «Konturen je Form»/«Kehlen»."""
    from hirch_cam import dokument, speicher
    speicher.SPEICHER.leeren()
    f = _mit(F, strategie=strategie)
    bea = [_mit(P, auf_seite=2), _mit(S, auf_seite=1), _mit(f, auf_seite=1), _mit(S, id="s3", auf_seite=2),
           _mit(f, id="f3", auf_seite=2)]
    svg, mat = linsen_zeichnung(tmp_path, bearbeitungen=bea, zweiseitig=ZWEI)
    erg = dokument.rechnen(svg, str(tmp_path / "aus"))
    _pruefen(erg, mat[0], mat[1])
    _nicht_zu_tief(svg, erg, mat[0], mat[1])         # Planen nur auf Seite 2
