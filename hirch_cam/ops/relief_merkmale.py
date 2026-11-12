# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Bohrungen und Taschen aus einem STEP-Modell erkennen (Reiter «3D» › «Bohrungen und Taschen»).

STEP kennt die Flächen genau: eine Bohrung ist eine innere Zylinderfläche mit senkrechter Achse,
der Boden einer Tasche eine waagrechte, nach oben zeigende Ebene, die rundherum von höherem
Modell umgeben ist (eine Stufe am Rand ist offen und keine Tasche). Alles in Material-mm auf
Seite 1 (von oben zugänglich): Z = 0 Materialoberfläche.

Seite 2 (von unten zugänglich, nur mit «material»): Koordinaten nach dem Wenden wie die Zeichnung
von Seite 2, Z ab der Unterseite des Materials (dort Z = 0). Durchgangsbohrungen nur auf Seite 1.

Ergebnis: {"ok", "bohrungen": [{"x", "y", "d", "oben", "unten", "tiefe", "durch", "seite"}],
           "taschen": [{"punkte": [[x, y], …], "boden", "oben", "tiefe", "breite", "laenge",
                        "eckradius", "seite"}],
           "boeden_waende": {"boeden": [{"z", "flaeche"}], "waende", "wand_hoehe",
                             "rundungen": [{"radius", "anzahl", "z"}],
                             "duenn": {"anzahl", "dicke", "hoehe"} | None, "seite": 1} | None,
           "warnungen"}

Böden und Wände (Peter 25.10., für «Böden und Wände» und den Vorschlag beim Erkennen): waagrechte Flächen
von oben, die senkrechten Wände darüber, Rundungen am Fuss der Wände (Radius aus den genauen Schnitten: wie
weit der Fuss über die Wand hinausreicht und wie hoch er ist) und dünne Wände (höher als 4 × ihre Dicke).
"""
from __future__ import annotations

import math
from typing import Dict, List

import numpy as np


def _ausgerichtet(relief: Dict):
    from .. import relief as rl
    info = relief.get("info") or {}
    m = rl.modell_aus_text(relief.get("daten") or "", str(info.get("name") or "modell.step"))
    a = rl.ausrichtung(info.get("ausrichtung"))
    return m, a, rl.Lage(m, a) if not m.ist_bild else None


def merkmale(relief: Dict, dicke: float, tol: float = 0.05, material=None, achse: str = "x") -> Dict:
    from shapely.geometry import Point, Polygon
    from shapely.ops import unary_union
    from .relief3d import _abbildung
    m, a, lage = _ausgerichtet(relief)
    if m.art != "step" or not m.flaechen or lage is None:
        return {"ok": True, "bohrungen": [], "taschen": [],
                "warnungen": ["Bohrungen und Taschen erkennt Hirch CAM nur in STEP-Dateien – dieses Modell "
                              "ist ein Netz (STL/OBJ) oder ein Bild."]}
    if not any("dreiecke" in f for f in m.flaechen):
        return {"ok": False, "bohrungen": [], "taschen": [],
                "warnungen": ["Das Modell wurde mit einer älteren Version eingelesen – bitte neu einlesen."]}
    tri_alle = lage(m.dreiecke)
    zmin, zmax = float(tri_alle[..., 2].min()), float(tri_alle[..., 2].max())
    abbilden, _v = _abbildung(relief, zmin, zmax, dicke)
    R = lage.m                                    # Drehung · Skalierung (ohne Versatz)
    massstab = float(np.linalg.norm(R @ np.array([1.0, 0.0, 0.0])))

    def richtung(v):
        q = R @ np.asarray(v, float)
        n = np.linalg.norm(q)
        return q / n if n > 0 else q

    def dreiecke(f):
        von, bis = f.get("dreiecke") or (0, 0)
        return tri_alle[int(von):int(bis)]

    # Draufsicht des Modells (Oberseite) für «ist rundherum höher?»
    from .relief3d import _raster
    lo, hi = tri_alle.reshape(-1, 3).min(0), tri_alle.reshape(-1, 3).max(0)
    zelle = max(0.1, min(0.5, max(hi[0] - lo[0], hi[1] - lo[1]) / 600))
    bx = (lo[0] - 2, lo[1] - 2, hi[0] + 2, hi[1] + 2)
    oben, unten, zelle = _raster(np.ascontiguousarray(tri_alle, np.float32), bx, zelle)
    unten = np.where(np.isfinite(unten), unten, np.nan)

    def hoehe(x, y, karte=None):
        karte = oben if karte is None else karte
        i = int(round((x - bx[0]) / zelle))
        j = int(round((y - bx[1]) / zelle))
        if 0 <= j < karte.shape[0] and 0 <= i < karte.shape[1]:
            return float(karte[j, i])
        return float("nan")

    from .relief_zweiseitig import wenden_punkte

    def auf_seite2(pts_m):
        """Material-Koordinaten von Seite 1 (x, y, z) → Seite 2 (gewendet, Z ab der Unterseite)."""
        xy = wenden_punkte([(float(q[0]), float(q[1])) for q in pts_m], material, achse)
        return np.array([[x, y, -float(dicke) - float(q[2])] for (x, y), q in zip(xy, pts_m)])

    # --- Bohrungen
    roh = []
    for f in m.flaechen:
        if f.get("art") != "zylinder" or "achse" not in f or "normale" not in f:
            continue
        ax = richtung(f["achse"])
        if abs(ax[2]) < 0.999:
            continue
        o = lage(np.asarray(f["achse_punkt"], float))
        p = lage(np.asarray(f["punkt"], float))
        n = richtung(f["normale"])
        radial = p - o - ax * np.dot(p - o, ax)
        if np.dot(n, radial) >= 0:                # Normale zeigt nach aussen: Zapfen, keine Bohrung
            continue
        t = dreiecke(f)
        if not len(t):
            continue
        roh.append((float(o[0]), float(o[1]), float(f["radius"]) * massstab,
                    float(t[..., 2].max()), float(t[..., 2].min()), float(f.get("flaeche") or 0) * massstab ** 2))
    gruppen: List[List] = []
    for x, y, r, zo, zu, fl_ in roh:              # Halbzylinder derselben Bohrung zusammen
        for g in gruppen:
            if math.hypot(g[0] - x, g[1] - y) < tol and abs(g[2] - r) < tol:
                g[3], g[4] = max(g[3], zo), min(g[4], zu)
                g[5] += fl_
                break
        else:
            gruppen.append([x, y, r, zo, zu, fl_])
    bohrungen = []
    loecher = []
    for x, y, r, zo, zu, fl_ in gruppen:
        # ganz rund? (Eckradien von Taschen sind nur Viertel-Zylinder)
        if fl_ < 0.85 * 2 * math.pi * r * max(zo - zu, 1e-9):
            continue
        # zugänglich von oben: rund um das Loch liegt die Oberseite auf Höhe des oberen Lochrands;
        # von unten (Seite 2): die Unterseite auf Höhe des unteren Rands
        ring = [(x + (r + 1.0) * math.cos(t), y + (r + 1.0) * math.sin(t))
                for t in np.linspace(0, 2 * math.pi, 12, endpoint=False)]
        rand_o = [h for h in (hoehe(a, b) for a, b in ring) if np.isfinite(h)]
        rand_u = [h for h in (hoehe(a, b, unten) for a, b in ring) if np.isfinite(h)]
        von_oben = not rand_o or zo >= max(rand_o) - 0.1
        von_unten = bool(rand_u) and zu <= min(rand_u) + 0.1
        durch = zu <= zmin + 0.05 and von_oben
        mx = abbilden(np.array([[x, y, zo], [x, y, zu], [x + r, y, zu]]))
        d = 2 * float(np.hypot(*(mx[2, :2] - mx[1, :2])))
        if von_oben:
            bohrungen.append({"x": round(float(mx[0, 0]), 3), "y": round(float(mx[0, 1]), 3),
                              "d": round(d, 3), "oben": round(float(mx[0, 2]), 3),
                              "unten": round(float(mx[1, 2]), 3),
                              "tiefe": round(float(mx[0, 2] - mx[1, 2]), 3), "durch": bool(durch), "seite": 1})
        elif von_unten and material is not None:
            m2 = auf_seite2(mx[:2])               # [Rand oben (Seite 1) → Boden, Rand unten → Öffnung]
            bohrungen.append({"x": round(float(m2[1, 0]), 3), "y": round(float(m2[1, 1]), 3),
                              "d": round(d, 3), "oben": round(float(m2[1, 2]), 3),
                              "unten": round(float(m2[0, 2]), 3),
                              "tiefe": round(float(m2[1, 2] - m2[0, 2]), 3), "durch": False, "seite": 2})
        else:
            continue
        loecher.append(Point(x, y).buffer(r + tol, resolution=24))
    bohrungen.sort(key=lambda b: (b["seite"], b["d"], b["x"], b["y"]))

    # --- Taschen: waagrechte Böden, rundherum höher (Seite 2: Decken nach unten, rundherum tiefer)
    taschen = []
    for f in m.flaechen:
        if f.get("art") != "ebene" or "normale" not in f:
            continue
        n = richtung(f["normale"])
        if n[2] >= 0.999:
            seite = 1
        elif n[2] <= -0.999 and material is not None:
            seite = 2
        else:
            continue
        t = dreiecke(f)
        if not len(t):
            continue
        z = float(t[..., 2].mean())
        if (seite == 1 and z >= zmax - 0.05) or (seite == 2 and z <= zmin + 0.05):
            continue
        fl = unary_union([Polygon(q[:, :2]).buffer(1e-4) for q in t if Polygon(q[:, :2]).area > 1e-9])
        if fl.is_empty or fl.area < 1.0:
            continue
        if any(fl.difference(l_).area < 0.05 * fl.area for l_ in loecher):
            continue                              # Boden eines Sacklochs
        aussen_fl = fl.buffer(max(0.5, 2 * zelle), resolution=8)
        karte = oben if seite == 1 else unten
        hoehen = []
        for teil in getattr(aussen_fl, "geoms", [aussen_fl]):   # zerfällt die Fläche, jeder Teil
            rand = teil.exterior
            n_proben = max(24, int(rand.length / max(zelle, 0.5)))
            hoehen += [hoehe(q.x, q.y, karte) for q in
                       (rand.interpolate(k / n_proben, normalized=True) for k in range(n_proben))]
        hoehen = np.array(hoehen)
        aussen = np.isfinite(hoehen) & ((hoehen > z + 0.5) if seite == 1 else (hoehen < z - 0.5))
        if aussen.mean() < 0.9:                   # offen zur Seite: Stufe, keine Tasche
            continue
        z_rand = float(np.median(hoehen[aussen]))
        umriss = fl.simplify(tol)
        poly = umriss if umriss.geom_type == "Polygon" else max(umriss.geoms, key=lambda g: g.area)
        ring = np.array(poly.exterior.coords)
        pm = abbilden(np.column_stack([ring, np.full(len(ring), z)]))
        rm = abbilden(np.array([[ring[0, 0], ring[0, 1], z_rand]]))
        if seite == 2:
            pm, rm = auf_seite2(pm), auf_seite2(rm)
        oz = float(rm[0, 2])
        mrr = poly.minimum_rotated_rectangle
        seiten = sorted({round(math.dist(a_, b_), 3) for a_, b_ in zip(list(mrr.exterior.coords)[:-1],
                                                                      list(mrr.exterior.coords)[1:])})
        # Eckradius: Fläche des umschreibenden Rechtecks minus Fläche = (4 − π)·r²
        fehl = max(0.0, mrr.area - poly.area)
        eck = math.sqrt(fehl / (4 - math.pi)) * massstab if fehl > 0.01 else 0.0
        taschen.append({"punkte": [[round(float(p_[0]), 3), round(float(p_[1]), 3)] for p_ in pm],
                        "boden": round(float(pm[0, 2]), 3), "oben": round(oz, 3),
                        "tiefe": round(float(oz - pm[0, 2]), 3),
                        "laenge": round(seiten[-1] * massstab, 2), "breite": round(seiten[0] * massstab, 2),
                        "eckradius": round(eck, 2), "seite": seite})
    taschen.sort(key=lambda t_: (t_["seite"], -t_["boden"], t_["punkte"][0]))
    try:
        bw = boeden_waende(abbilden(tri_alle))
    except Exception:                             # Erkennen darf nie am Zusatz scheitern
        bw = None
    w = []
    if not bohrungen and not taschen and not (bw and bw["boeden"]):
        w.append("Keine Bohrungen und Taschen gefunden." if material is not None
                 else "Keine Bohrungen und Taschen von oben gefunden.")
    return {"ok": True, "bohrungen": bohrungen, "taschen": taschen, "boeden_waende": bw, "warnungen": w}


def boeden_waende(tri_m: np.ndarray) -> Dict:
    """Böden, Wände, Rundungen am Fuss und dünne Wände von oben (Seite 1); tri_m in Material-mm."""
    from shapely.ops import polylabel
    from .relief3d import _Schnitte, _flache_hoehen, _raster
    tri_m = np.ascontiguousarray(tri_m, np.float32)
    lo, hi = tri_m.reshape(-1, 3).min(0), tri_m.reshape(-1, 3).max(0)
    zelle = max(0.1, min(0.5, max(hi[0] - lo[0], hi[1] - lo[1]) / 600))
    oben, _u, zelle = _raster(tri_m, (lo[0] - 1, lo[1] - 1, hi[0] + 1, hi[1] + 1), zelle)
    maske = np.isfinite(oben)
    S = _Schnitte(tri_m)
    ztop = float(hi[2])
    boeden = []
    for h in sorted(_flache_hoehen(oben, maske, zelle, 2.0), reverse=True):
        if h >= ztop - 0.05:
            continue
        F = S(h - 0.02).difference(S(h + 0.02))
        if F.area >= 4.0:
            boeden.append({"z": round(h, 2), "flaeche": round(float(F.area))})
    rundungen, waende, wand_hoehe, duenn = [], 0, 0.0, None
    duenne = []

    def teile(g):
        return [t for t in getattr(g, "geoms", [g]) if t.geom_type == "Polygon" and t.area > 0.5]

    for b in boeden:
        h = b["z"]
        if S(h + 0.02).is_empty:
            continue
        # Fuss: wie weit reicht der Schnitt über dem Boden über die Wand hinaus? Wand = Schnitt, der sich nach oben
        # nicht mehr ändert. Rundung mit Radius R: auf der Höhe t steht der Fuss f = R − √(2Rt − t²) vor der Wand,
        # also R = f + t + √(2 f t)
        hoehen = [t for t in (0.25, 0.5, 1, 1.5, 2, 2.5, 3, 4, 5, 6, 8, 10) if h + t < ztop - 0.05]
        flaechen = [(t, S(h + t).area) for t in hoehen]
        t_wand = None
        for i in range(1, len(flaechen)):
            if abs(flaechen[i][1] - flaechen[i - 1][1]) < 0.002 * max(flaechen[i][1], 1.0):
                t_wand = flaechen[i - 1][0]
                break
        z_wand = h + (t_wand if t_wand is not None else 0.25)
        if t_wand is not None and t_wand > 0.25:
            W = S(z_wand)
            # Länge nur der Wände mit Fuss (nicht der Wände ohne Rundung, nicht der Ränder des Modells)
            fuss_band = S(h + flaechen[0][0]).difference(W)
            L = max(W.boundary.intersection(fuss_band.buffer(0.05, resolution=4)).length, 1e-6)
            schaetz = []
            for t, a_t in flaechen:
                if t >= t_wand:
                    break
                f = (a_t - W.area) / L
                if f > 0.02:
                    schaetz.append(f + t + math.sqrt(2 * f * t))
            if schaetz:
                r = round(float(np.median(schaetz)) * 2) / 2      # auf 0,5 mm
                if r >= 0.5:
                    n = len([t for t in teile(S(h + 0.25)) if t.area > 2.0])
                    rundungen.append({"radius": r, "anzahl": max(1, n), "z": h})
        # Wände über dem Boden: Ränder des Schnitts in der Mitte zwischen Fuss und Oberkante
        z_mitte = (z_wand + min(ztop, h + 50)) / 2
        M = S(z_mitte)
        ring_n = sum(1 + len(t.interiors) for t in teile(M) if t.area > 2.0)
        if ring_n:
            waende = max(waende, ring_n)
            wand_hoehe = max(wand_hoehe, round(ztop - h, 1))
        hier = []
        for t in teile(M):
            try:
                pol = polylabel(t, tolerance=0.05)
                dicke = 2 * t.exterior.distance(pol)
            except Exception:
                continue
            hoehe = ztop - h
            if dicke > 0.1 and hoehe > 4 * dicke:
                hier.append((dicke, hoehe))
        # dieselben Wände stehen auf mehreren Böden: zählt der tiefste (dort sind sie am höchsten)
        if hier and (not duenne or hier[0][1] > duenne[0][1]):
            duenne = hier
    if duenne:
        dk = min(x[0] for x in duenne)
        duenn = {"anzahl": len(duenne), "dicke": round(dk, 1), "hoehe": round(max(x[1] for x in duenne), 1)}
    if not boeden:
        return None
    return {"boeden": boeden, "waende": waende, "wand_hoehe": wand_hoehe, "rundungen": rundungen,
            "duenn": duenn, "seite": 1}
