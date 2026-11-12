# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Umriss einer Fläche des 3D-Modells (Reiter «3D»: Klick auf das Modell, Flächen wählen).

STEP: die Fläche, auf die geklickt wurde, genau (Dreiecke der STEP-Fläche, in der Draufsicht
vereinigt). Mit der Höhe z des Klicks wird die Fläche genommen, die dem Punkt am nächsten liegt –
so lassen sich auch die Unterseite (Seite 2) und senkrechte Wände wählen; eine senkrechte Wand ist in
der Draufsicht nur eine Linie und bekommt einen schmalen Streifen (WAND_BREITE).
Netz (STL/OBJ) und Bild: von der Stelle aus alle zusammenhängenden Stellen der Ober- bzw. (Seite 2)
Unterseite, deren Neigung höchstens «winkel» Grad von der angeklickten abweicht.

Ergebnis: {"ok", "id", "seite", "name", "umriss": [{"punkte", "geschlossen"}] (Material-mm, Seite 1:
für die Hervorhebung in der 3D-Ansicht), "linien": dito, auf Seite 2 gewendet wie die Zeichnung
von Seite 2 (mit "seite": 2), "art": "step"|"netz", "flaechenart", "flaeche": mm², "z": [min, max],
"warnungen"}.

vereinigen(): mehrere Umrisse (Material-mm, Seite 1) einer Seite zu einer Fläche – der Bereich einer
3D-Bearbeitung («Gewählte Flächen als Bereich»), aneinandergrenzende Flächen ohne Naht.
"""
from __future__ import annotations

import math
from typing import Dict, List, Optional

import numpy as np

WAND_BREITE = 0.2      # mm: senkrechte Wand in der Draufsicht (vor dem Überlauf der Bearbeitung)


def _ringe(geom, tol: float) -> List[Dict]:
    aus = []
    for poly in getattr(geom, "geoms", [geom]):
        if poly.is_empty or poly.geom_type != "Polygon":
            continue
        for r in [poly.exterior] + list(poly.interiors):
            pts = [[round(float(x), 3), round(float(y), 3)] for x, y in r.simplify(tol).coords]
            if len(pts) >= 4:
                aus.append({"punkte": pts, "geschlossen": True})
    return aus


def _flaeche_aus_ringen(linien: List[Dict]):
    """Ringe (gerade Anzahl = Loch) zu einer shapely-Fläche."""
    from shapely.geometry import Polygon
    flaeche = None
    for l in linien or []:
        pts = [tuple(q[:2]) for q in l.get("punkte") or []]
        if len(pts) < 3:
            continue
        g = Polygon(pts)
        g = g if g.is_valid else g.buffer(0)
        if g.is_empty:
            continue
        flaeche = g if flaeche is None else flaeche.symmetric_difference(g)
    return flaeche


def _gewendet(linien: List[Dict], material, achse: str) -> List[Dict]:
    from .relief_zweiseitig import wenden_punkte
    aus = []
    for l in linien:
        pts = [list(q) for q in wenden_punkte([tuple(q[:2]) for q in l["punkte"]], material, achse)]
        aus.append({"punkte": [[round(x, 3), round(y, 3)] for x, y in pts], "geschlossen": True, "seite": 2})
    return aus


def _ergebnis(geom, tol, seite, material, achse, **mehr) -> Dict:
    umriss = _ringe(geom, tol)
    linien = _gewendet(umriss, material, achse) if seite == 2 and material is not None else umriss
    erg = {"ok": True, "seite": seite, "umriss": umriss, "linien": linien,
           "flaeche": round(float(geom.area), 2), "warnungen": []}
    erg.update(mehr)
    return erg


FLAECHENARTEN = {"ebene": "Ebene", "zylinder": "Zylinder", "kegel": "Kegel", "kugel": "Kugel",
                 "torus": "Torus", "bspline": "Freiform", "freiform": "Freiform"}


def flaeche(relief: Dict, dicke: float, x: float, y: float, winkel: float = 5.0, tol: float = 0.02,
            z: Optional[float] = None, seite: int = 1, material=None, achse: str = "x") -> Dict:
    """x, y, z: angeklickte Stelle (Material-mm, Seite 1; z = 0 Oberfläche, −dicke Unterseite);
    seite: 2 = von unten angeklickt (Unterseite). material (B, H[, D]) und achse: für die Linien
    auf Seite 2."""
    from shapely.geometry import LineString, Polygon
    from shapely.ops import unary_union
    from .relief3d import _abbildung, _modell_ausgerichtet, _raster, _flaechen_aus
    from .. import relief as rl
    seite = 2 if int(seite or 1) == 2 else 1
    m, a, tri, zmin, zmax = _modell_ausgerichtet(relief)
    abbilden, _v = _abbildung(relief, zmin, zmax, dicke)
    tri_m = abbilden(tri)
    # STEP: die genaue Fläche an der Stelle
    if m.art == "step" and m.flaechen and any("dreiecke" in f for f in m.flaechen):
        lage = rl.Lage(m, a)
        alle = abbilden(lage(m.dreiecke))
        k = _naechstes_dreieck(alle, x, y, z, seite)
        bester = None
        if k is not None:
            for nr, f in enumerate(m.flaechen):
                von, bis = f.get("dreiecke") or (0, 0)
                if int(von) <= k < int(bis):
                    bester = (nr, f, alle[int(von):int(bis)])
                    break
        if bester is not None:
            nr, f, t = bester
            polys = [Polygon(q[:, :2]) for q in t]
            flaechen = [p_.buffer(1e-4) for p_ in polys if p_.area > 1e-9]
            wand = not flaechen
            if wand:                        # senkrecht: in der Draufsicht nur Linien
                geom = unary_union([LineString(np.vstack([q[:, :2], q[:1, :2]])) for q in t]).buffer(
                    WAND_BREITE / 2, join_style=2)
            else:
                geom = unary_union(flaechen)
            art = str(f.get("art") or "")
            z0, z1 = float(t[..., 2].min()), float(t[..., 2].max())
            name = ("Wand" if wand else FLAECHENARTEN.get(art, "Fläche")) + \
                (" · Z %s" % _komma(z0) if abs(z1 - z0) < 0.01 else " · Z %s … %s" % (_komma(z0), _komma(z1)))
            return _ergebnis(geom, tol, seite, material, achse, art="step", flaechenart=art, id="step:%d" % nr,
                             name=name, wand=wand, z=[round(z0, 3), round(z1, 3)])
        return {"ok": True, "art": "step", "seite": seite, "linien": [], "umriss": [],
                "warnungen": ["An dieser Stelle ist keine Fläche des Modells."]}
    # Netz: zusammenhängend ähnlich geneigt (Ober- bzw. Unterseite)
    lo, hi = tri_m.reshape(-1, 3).min(0), tri_m.reshape(-1, 3).max(0)
    zelle = max(0.1, min(0.5, max(hi[0] - lo[0], hi[1] - lo[1]) / 800))
    bx = (lo[0] - 1, lo[1] - 1, hi[0] + 1, hi[1] + 1)
    oben, unten, zelle = _raster(np.ascontiguousarray(tri_m, np.float32), bx, zelle)
    feld = unten if seite == 2 else oben
    i, j = int(round((x - bx[0]) / zelle)), int(round((y - bx[1]) / zelle))
    if not (0 <= j < feld.shape[0] and 0 <= i < feld.shape[1]) or not np.isfinite(feld[j, i]):
        return {"ok": True, "art": "netz", "seite": seite, "linien": [], "umriss": [],
                "warnungen": ["An dieser Stelle ist kein Modell."]}
    zf = np.where(np.isfinite(feld), feld, np.nan).astype(np.float64)
    gy, gx = np.gradient(np.nan_to_num(zf, nan=float(np.nanmin(zf))), zelle)
    n = np.dstack([-gx, -gy, np.ones_like(gx)])
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    cos_g = math.cos(math.radians(max(0.5, min(89.0, winkel))))
    passt = np.isfinite(zf) & ((n * n[j, i]).sum(axis=2) >= cos_g)
    from scipy import ndimage
    marken, _anz = ndimage.label(passt)
    maske = marken == marken[j, i]
    geom = _flaechen_aus(maske.astype(np.float32), 0.5, bx[0], bx[1], zelle, max(tol, zelle * 0.5))
    zz = zf[maske]
    erste = int(np.flatnonzero(maske.ravel())[0])
    neigung = math.degrees(math.acos(max(-1.0, min(1.0, float(n[j, i][2])))))
    name = ("waagrecht" if neigung < 1 else "%s° geneigt" % _komma(neigung, 0)) + \
        " · Z %s … %s" % (_komma(float(zz.min())), _komma(float(zz.max())))
    return _ergebnis(geom, max(tol, zelle * 0.5), seite, material, achse, art="netz",
                     id="netz:%d:%d:%g" % (seite, erste, round(winkel, 1)), name=name,
                     z=[round(float(zz.min()), 3), round(float(zz.max()), 3)])


def _komma(v: float, n: int = 1) -> str:
    return ("%.*f" % (n, v)).replace(".", ",").replace("-", "−")


def _naechstes_dreieck(alle: np.ndarray, x: float, y: float, z: Optional[float], seite: int) -> Optional[int]:
    """Dreieck an der Stelle: ohne z das oberste (Seite 1) bzw. unterste (Seite 2) unter (x, y);
    mit z das, dem der Punkt (x, y, z) am nächsten liegt (auch senkrechte Wände)."""
    p0, p1, p2 = alle[:, 0], alle[:, 1], alle[:, 2]
    if z is not None:
        P = np.array([x, y, z], float)
        e1, e2 = p1 - p0, p2 - p0
        nrm = np.cross(e1, e2)
        laenge = np.linalg.norm(nrm, axis=1)
        gut = laenge > 1e-12
        nn = np.zeros_like(nrm)
        nn[gut] = nrm[gut] / laenge[gut, None]
        abst = ((P - p0) * nn).sum(axis=1)
        q = P - abst[:, None] * nn
        # baryzentrisch in 3D
        v2 = q - p0
        d00, d01, d11 = (e1 * e1).sum(1), (e1 * e2).sum(1), (e2 * e2).sum(1)
        d20, d21 = (v2 * e1).sum(1), (v2 * e2).sum(1)
        den = d00 * d11 - d01 * d01
        den = np.where(np.abs(den) > 1e-18, den, np.inf)
        v = (d11 * d20 - d01 * d21) / den
        w = (d00 * d21 - d01 * d20) / den
        u = 1 - v - w
        eps = 1e-6
        drin = gut & np.isfinite(den) & (u >= -eps) & (v >= -eps) & (w >= -eps)
        if drin.any():
            a_ = np.where(drin, np.abs(abst), np.inf)
            k = int(np.argmin(a_))
            if a_[k] < 0.5:
                return k
    d = (p1[:, 1] - p2[:, 1]) * (p0[:, 0] - p2[:, 0]) + (p2[:, 0] - p1[:, 0]) * (p0[:, 1] - p2[:, 1])
    d = np.where(np.abs(d) > 1e-12, d, np.inf)          # senkrechte Dreiecke: nie «drin»
    l0 = ((p1[:, 1] - p2[:, 1]) * (x - p2[:, 0]) + (p2[:, 0] - p1[:, 0]) * (y - p2[:, 1])) / d
    l1 = ((p2[:, 1] - p0[:, 1]) * (x - p2[:, 0]) + (p0[:, 0] - p2[:, 0]) * (y - p2[:, 1])) / d
    l2 = 1 - l0 - l1
    drin = np.isfinite(d) & (l0 >= -1e-9) & (l1 >= -1e-9) & (l2 >= -1e-9)
    if not drin.any():
        return None
    zs = np.where(drin, l0 * p0[:, 2] + l1 * p1[:, 2] + l2 * p2[:, 2], np.nan)
    return int(np.nanargmin(zs) if seite == 2 else np.nanargmax(zs))


def vereinigen(umrisse: List[List[Dict]], seite: int = 1, material=None, achse: str = "x",
               tol: float = 0.02) -> Dict:
    """Umrisse mehrerer Flächen (je eine Liste von Ringen, Material-mm Seite 1) zu einer Fläche.
    Aneinandergrenzende Flächen verschmelzen (kleine Fugen bis 0,05 mm werden geschlossen)."""
    from shapely.ops import unary_union
    teile = [g for g in (_flaeche_aus_ringen(u) for u in umrisse) if g is not None and not g.is_empty]
    if not teile:
        return {"ok": True, "linien": [], "umriss": [], "flaeche": 0.0, "warnungen": ["Keine Fläche gewählt."]}
    geom = unary_union([g.buffer(0.05, join_style=2) for g in teile]).buffer(-0.05, join_style=2)
    return _ergebnis(geom, tol, 2 if int(seite or 1) == 2 else 1, material, achse)
