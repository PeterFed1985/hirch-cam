"""Peter 04.11.: Am Flügel ruckelte die Maschine im letzten Schritt des Schruppens («Boden nachfahren») entlang
Nasen- und Endleiste – X und Y starteten und stoppten ständig. Ursache: der Umriss des Modells kam aus dem Raster
und hatte an schrägen Kanten Treppen von einer Rasterzelle (1 mm gerade, 0,33 mm schräg, …), die Bahn fuhr sie
nach. Jetzt kommt der Umriss für die Bahnen genau aus den Dreiecken (relief3d.umriss_modell)."""
import math
import os
import re

import numpy as np
import pytest

HIER = os.path.dirname(os.path.abspath(__file__))

pytest.importorskip("hirch_cam.hirch_relief")


def _quader(b, h, d, winkel, mitte):
    c, s = math.cos(math.radians(winkel)), math.sin(math.radians(winkel))
    ecken = []
    for z in (0.0, -d):
        for x, y in ((-b / 2, -h / 2), (b / 2, -h / 2), (b / 2, h / 2), (-b / 2, h / 2)):
            ecken.append((mitte[0] + c * x - s * y, mitte[1] + s * x + c * y, z))
    p = np.array(ecken)
    f = [(0, 1, 2), (0, 2, 3), (4, 6, 5), (4, 7, 6), (0, 4, 5), (0, 5, 1), (1, 5, 6), (1, 6, 2),
         (2, 6, 7), (2, 7, 3), (3, 7, 4), (3, 4, 0)]
    return np.array([[p[i], p[j], p[k]] for i, j, k in f], np.float32)


def treppen(tap: str) -> int:
    """Grösste Zahl von Knicken (über 30°) zwischen zwei kurzen Stücken (unter 1,5 mm) in einem Zug –
    eine Treppe hat an jedem Absatz einen; Rampen beim Eintauchen in kleinen Ecken kommen auf ~50."""
    zuege, pos = [[]], [0.0, 0.0]
    for zeile in open(tap):
        m = dict((k, float(v)) for k, v in re.findall(r"([XY])(-?[\d.]+)", zeile))
        if zeile.startswith("G0"):
            zuege.append([])
            pos = [m.get("X", pos[0]), m.get("Y", pos[1])]
            continue
        if not m:
            continue
        n = [m.get("X", pos[0]), m.get("Y", pos[1])]
        if math.hypot(n[0] - pos[0], n[1] - pos[1]) > 0.005:
            zuege[-1].append((n[0] - pos[0], n[1] - pos[1]))
        pos = n
    am_meisten = 0
    for s in zuege:
        if len(s) > 1:                                     # Eintauchen auf der Stelle (Rampe in einer kleinen
            xy = np.cumsum(np.array(s), axis=0)            # Ecke) ist keine Treppe: nur Züge, die eine Strecke
            if np.ptp(xy[:, 0]) < 5 and np.ptp(xy[:, 1]) < 5:   # von mindestens 5 mm zurücklegen
                continue
        k = 0
        for p, q in zip(s, s[1:]):
            if math.hypot(*p) < 1.5 and math.hypot(*q) < 1.5:
                d = abs((math.atan2(q[1], q[0]) - math.atan2(p[1], p[0]) + math.pi) % (2 * math.pi) - math.pi)
                k += d > math.radians(30)
        am_meisten = max(am_meisten, k)
    return am_meisten


def test_umriss_modell_ohne_treppen():
    from hirch_cam.ops.relief3d import _raster, umriss_modell
    tri = _quader(80, 40, 10, 12, (60, 60))
    bx = (10.0, 10.0, 110.0, 110.0)
    oben, _u, zelle = _raster(tri, bx, 0.33)
    g = umriss_modell(tri, np.isfinite(oben), bx[0], bx[1], zelle, 0.01)
    assert g.geom_type == "Polygon"
    assert len(g.exterior.coords) <= 6                    # vier Ecken, keine Stufen
    assert g.area == pytest.approx(80 * 40, abs=0.5)


def _knicke(moves) -> int:
    zuege, pos = [[]], None
    for m in moves:
        if m.rapid:
            zuege.append([])
        elif pos is not None and math.hypot(m.x - pos[0], m.y - pos[1]) > 0.005:
            zuege[-1].append((m.x - pos[0], m.y - pos[1]))
        pos = (m.x, m.y)
    am_meisten = 0
    for s in zuege:
        k = sum(1 for p, q in zip(s, s[1:]) if math.hypot(*p) < 1.5 and math.hypot(*q) < 1.5 and abs(
            (math.atan2(q[1], q[0]) - math.atan2(p[1], p[0]) + math.pi) % (2 * math.pi) - math.pi) > math.radians(30))
        am_meisten = max(am_meisten, k)
    return am_meisten


def test_ebenen_um_schraeges_teil_ohne_treppen():
    """Schlichten «Waagrechte Flächen» um einen um 12° gedrehten Block: vorher ~50 Treppenknicke je Runde."""
    from hirch_cam.ops import relief3d as R
    sockel = _quader(100, 100, 10, 0, (60, 60))
    sockel[..., 2] -= 10
    tri = np.concatenate([sockel, _quader(40, 30, 10, 12, (60, 60))])
    w = {"typ": "schaft", "durchmesser": 6.0, "vorschub": 1000, "eintauchen": 300, "schneidenlaenge": 30}
    tp, _warn = R.schlichten(tri, (120, 120, 30), w, {"strategie": "ebenen", "bahnabstand": 40}, 5, 2,
                             (0, 0, 5), "t", 0.01)
    assert tp.moves
    assert _knicke(tp.moves) <= 10
    hoehen = {round(m.z, 2) for m in tp.moves if not m.rapid}
    assert hoehen <= {-10.0, 0.0}                         # nie die Wand hinauf, nie tiefer


def test_fluegel_boden_nachfahren_ohne_treppen(tmp_path, monkeypatch):
    """Der Flügel, so wie Peter ihn fräst: kein Zug mit mehr als 150 Treppenknicken (vorher je Seite ~1600)."""
    from hirch_cam import dokument
    monkeypatch.setenv("HIRCH_3D_SPEICHER", "aus")
    aus = tmp_path / "aus"
    e = dokument.rechnen(os.path.join(HIER, "daten", "fluegel_gefraest.hir"), str(aus), "m")
    assert e.get("ok"), e
    dateien = sorted(p for p in os.listdir(aus) if p.upper().endswith(".TAP"))
    assert dateien
    for f in dateien:
        assert treppen(str(aus / f)) < 150, f
