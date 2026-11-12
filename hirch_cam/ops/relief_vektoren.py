# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Vektoren aus einem 3D-Modell für die Zeichnung (Reiter «3D» › «Vektoren aus dem Modell»).

- Umriss: Rand des Modells in der Draufsicht (an der Trennlinie).
- Höhenlinie: wo die Oberseite des Modells eine Höhe Z schneidet.
- Kanten (nur STEP): exakte Kanten zwischen den Flächen, die an der Oberseite bzw. der
  Unterseite liegen – z. B. der Rand eines Schachts. Kanten der Unterseite kommen in den
  Koordinaten von Seite 2 (nach dem Wenden, wie die Zeichnung von Seite 2).

Alle Linien in Material-mm (Seite 1: Nullpunkt unten links; Seite 2: gewendet). Gefräst
werden sie wie jedes Objekt (einfärben); mit «Tiefe gemessen ab: 3D-Modell» folgt die
Bearbeitung der Oberfläche.
"""
from __future__ import annotations

import math
from typing import Dict, List, Optional

import numpy as np


def _ringe(raster, schwelle, x0, y0, zelle, tol) -> List[List[List[float]]]:
    from .. import hirch_relief
    aus = []
    for r in hirch_relief.konturen(np.ascontiguousarray(raster, np.float32), schwelle, x0, y0, zelle):
        if len(r) < 4:
            continue
        from shapely.geometry import LinearRing
        try:
            ring = LinearRing(r).simplify(tol)
        except Exception:
            continue
        pts = [[round(float(x), 3), round(float(y), 3)] for x, y in ring.coords]
        if len(pts) >= 4:
            aus.append(pts)
    return aus


def _umriss_genau(tri: np.ndarray, lage, tol: float, rand_zellen: int = 3) -> Optional[List[List[List[float]]]]:
    """Umriss in der Draufsicht genau aus den Dreiecken (Peter 30.10.: aus dem Raster entstanden an schrägen
    Kanten Treppen von einer Rasterzelle). Vereinigt werden nur die Dreiecke nahe am Rand – das Innere
    (Raster, «rand_zellen» Zellen eingerückt) liegt ganz unter ihnen und kommt als eine Fläche dazu."""
    try:
        import shapely
        from scipy import ndimage
        from shapely.geometry import LinearRing, Polygon
    except ImportError:
        return None
    xy = np.asarray(tri, np.float64)[:, :, :2]
    kreuz = 0.5 * ((xy[:, 1, 0] - xy[:, 0, 0]) * (xy[:, 2, 1] - xy[:, 0, 1])
                   - (xy[:, 2, 0] - xy[:, 0, 0]) * (xy[:, 1, 1] - xy[:, 0, 1]))
    # Ein geschlossener Körper wird von oben (und ebenso von unten) gesehen ganz von den Dreiecken einer
    # Richtung bedeckt – die halbe Menge genügt; offene Flächen (Bild als Gitter): die Seite mit mehr Fläche.
    # Senkrechte Flächen haben keine Draufsicht.
    auf, ab = kreuz > 1e-9, kreuz < -1e-9
    xy = xy[auf] if kreuz[auf].sum() >= -kreuz[ab].sum() else xy[ab]
    if not len(xy):
        return None
    innen = ndimage.binary_erosion(lage.maske, iterations=rand_zellen, border_value=0)
    nah = (~innen).astype(np.int32)
    ny, nx = nah.shape
    # Dreieck behalten, wenn in seinem Rechteck (Rasterzellen) eine Zelle nahe am Rand liegt
    sat = np.zeros((ny + 1, nx + 1), np.int64)
    sat[1:, 1:] = nah.cumsum(0).cumsum(1)
    z = lage.zelle
    i0 = np.clip(np.floor((xy[:, :, 0].min(1) - lage.x0) / z).astype(int), 0, nx - 1)
    i1 = np.clip(np.ceil((xy[:, :, 0].max(1) - lage.x0) / z).astype(int), 0, nx - 1)
    j0 = np.clip(np.floor((xy[:, :, 1].min(1) - lage.y0) / z).astype(int), 0, ny - 1)
    j1 = np.clip(np.ceil((xy[:, :, 1].max(1) - lage.y0) / z).astype(int), 0, ny - 1)
    n_nah = sat[j1 + 1, i1 + 1] - sat[j0, i1 + 1] - sat[j1 + 1, i0] + sat[j0, i0]
    xy = xy[n_nah > 0]
    teile = list(shapely.polygons(np.concatenate([xy, xy[:, :1]], 1)))
    if innen.any():
        from .relief3d import _flaechen_aus
        kern = _flaechen_aus(innen.astype(np.float32), 0.5, lage.x0, lage.y0, z, 0.0)
        if not kern.is_empty:
            teile.append(kern)
    try:
        u = shapely.union_all(teile)
    except Exception:
        try:
            u = shapely.union_all(teile, grid_size=0.001)     # langsamer, aber robust
        except Exception:
            return None
    aus = []
    for g in getattr(u, "geoms", [u]):
        if g.geom_type != "Polygon" or g.is_empty or g.area < 0.5:
            continue
        g = g.simplify(min(tol, 0.02))
        for ring in [g.exterior] + list(g.interiors):
            if Polygon(ring).area < 0.5:
                continue
            pts = [[round(float(x), 3), round(float(y), 3)] for x, y in LinearRing(ring).coords]
            if len(pts) >= 4:
                aus.append(pts)
    return aus or None


def _ketten(kanten: List[np.ndarray], tol: float = 0.02, alle=None) -> List[np.ndarray]:
    """Kantenstücke an gemeinsamen Endpunkten zu Linienzügen verbinden – nur wo genau zwei
    Stücke zusammenstossen (an Verzweigungen, z. B. wo eine Naht auf den Rand eines Schachts
    trifft, beginnt ein neuer Zug). alle: alle Kanten des Modells (für die Verzweigungen)."""
    offen = [np.asarray(k, float) for k in kanten if len(k) >= 2]

    def schl(p):
        return (round(p[0] / tol), round(p[1] / tol), round(p[2] / tol))
    grad: Dict = {}
    for k in (offen if alle is None else [np.asarray(q, float) for q in alle if len(q) >= 2]):
        for p in (k[0], k[-1]):
            grad[schl(p)] = grad.get(schl(p), 0) + 1

    def frei(p):
        return grad.get(schl(p), 0) == 2
    aus = []
    while offen:
        kette = offen.pop(0)
        weiter = True
        while weiter:
            weiter = False
            for i, k in enumerate(offen):
                if frei(kette[-1]) and np.linalg.norm(k[0] - kette[-1]) < tol:
                    kette = np.vstack([kette, k[1:]])
                elif frei(kette[-1]) and np.linalg.norm(k[-1] - kette[-1]) < tol:
                    kette = np.vstack([kette, k[::-1][1:]])
                elif frei(kette[0]) and np.linalg.norm(k[-1] - kette[0]) < tol:
                    kette = np.vstack([k[:-1], kette])
                elif frei(kette[0]) and np.linalg.norm(k[0] - kette[0]) < tol:
                    kette = np.vstack([k[::-1][:-1], kette])
                else:
                    continue
                offen.pop(i)
                weiter = True
                break
        aus.append(kette)
    return aus


def vektoren(relief: Dict, dicke: float, art: str, z: Optional[float] = None,
             material=None, achse: str = "x", tol: float = 0.05) -> Dict:
    """{"ok", "linien": [{"punkte": [[x, y], …], "geschlossen", "seite"}], "anzahl_oben",
    "anzahl_unten", "z": [min, max]} – art: umriss | hoehenlinie | kanten."""
    from . import relief_zweiseitig as zs
    from .relief3d import _abbildung, _modell_ausgerichtet
    from .. import relief as rl
    m, a, tri0, zmin, zmax = _modell_ausgerichtet(relief)
    abbilden, _v = _abbildung(relief, zmin, zmax, dicke)
    tri = np.ascontiguousarray(abbilden(tri0), np.float32)
    lo, hi = tri.reshape(-1, 3).min(0), tri.reshape(-1, 3).max(0)
    bx = (float(lo[0]) - 2, float(lo[1]) - 2, float(hi[0]) + 2, float(hi[1]) + 2)
    lage = zs.analysieren(tri, bx, zs.zelle_fuer(bx, fein=0.25, max_punkte=4_000_000))
    erg: Dict = {"ok": True, "warnungen": [], "linien": []}
    if lage is None:
        erg["warnungen"].append("Das Modell liegt ausserhalb der Zeichnung.")
        return erg
    if art == "umriss":
        ringe = _umriss_genau(tri, lage, tol)
        if ringe is None:       # zur Not wie früher aus dem Raster
            ringe = _ringe(lage.maske.astype(np.float32), 0.5, lage.x0, lage.y0, lage.zelle, tol)
        for r in ringe:
            erg["linien"].append({"punkte": r, "geschlossen": True, "seite": 1})
        # Peter 30.10.: zweiseitig auch auf Seite 2 (gewendet, wie die Kanten der Unterseite)
        if material is not None and zs.einstellungen(relief.get("info")).get("an"):
            for r in ringe:
                xy = zs.wenden_punkte([tuple(q) for q in r], material, achse)
                erg["linien"].append({"punkte": [[round(float(x), 3), round(float(y), 3)] for x, y in xy],
                                      "geschlossen": True, "seite": 2})
    elif art == "hoehenlinie":
        if z is None:
            erg["warnungen"].append("Keine Höhe angegeben.")
            return erg
        t = np.where(lage.maske, lage.oben, -1e9).astype(np.float32)
        for r in _ringe(t, float(z), lage.x0, lage.y0, lage.zelle, tol):
            erg["linien"].append({"punkte": r, "geschlossen": True, "seite": 1})
        if not erg["linien"]:
            erg["warnungen"].append("Auf dieser Höhe schneidet die Oberseite des Modells nichts.")
    elif art == "kanten":
        roh = rl.kanten(m, a)
        if not roh:
            erg["warnungen"].append("Kanten gibt es nur bei STEP-Dateien.")
            return erg
        from scipy import ndimage
        T = ndimage.maximum_filter(np.where(lage.maske, lage.oben, -np.inf), size=5)
        B = ndimage.minimum_filter(np.where(lage.maske, lage.unten, np.inf), size=5)
        ny, nx = lage.maske.shape
        oben, unten = 0, 0

        def seite_von(k):
            i = np.clip(np.round((k[:, 0] - lage.x0) / lage.zelle).astype(int), 0, nx - 1)
            j = np.clip(np.round((k[:, 1] - lage.y0) / lage.zelle).astype(int), 0, ny - 1)
            if np.all(lage.abstand[j, i] < 0.6):
                return 0            # liegt auf dem Umriss (Wurzel, Rand): dafür gibt es den Umriss
            if np.all(np.abs(k[:, 2] - T[j, i]) < 0.6):
                return 1
            if np.all(np.abs(k[:, 2] - B[j, i]) < 0.6):
                return 2
            return 0                # z. B. senkrechte Wand: dafür gibt es den Umriss
        # erst jede Kante einer Seite zuordnen, dann je Seite zu Linienzügen verbinden
        # Rand eines Schachts o. ä.: Kante zwischen der Haut und einer anderen Fläche (Ebene,
        # Zylinder); Naht: zwischen zwei gleichartigen Flächen. Verbunden wird nur innerhalb
        # derselben Seite und Art, nur wo genau zwei solche Stücke zusammenstossen.
        je_seite: Dict = {}
        for e in roh:
            k = abbilden(np.asarray(e["punkte"]))
            s_ = seite_von(k)
            if s_:
                art_k = "naht" if len(set(e.get("flaechen") or [])) == 1 else "kante"
                je_seite.setdefault((s_, art_k), []).append(k)
        from shapely.geometry import LineString
        gruppen = sorted(je_seite)
        for seite, art_k in gruppen:
            for k in _ketten(je_seite[(seite, art_k)]):
                geschlossen = len(k) > 2 and np.linalg.norm(k[0] - k[-1]) < 0.05
                xy = [tuple(p) for p in np.asarray(LineString(k[:, :2]).simplify(tol / 2).coords)]
                if seite == 2 and material is not None:
                    xy = zs.wenden_punkte(xy, material, achse)
                erg["linien"].append({"punkte": [[round(float(x), 3), round(float(y), 3)] for x, y in xy],
                                      "geschlossen": bool(geschlossen), "seite": seite,
                                      "z": [round(float(k[:, 2].min()), 2), round(float(k[:, 2].max()), 2)]})
                if seite == 1:
                    oben += 1
                else:
                    unten += 1
        erg["anzahl_oben"], erg["anzahl_unten"] = oben, unten
        if not erg["linien"]:
            erg["warnungen"].append("Keine Kanten auf der Ober- oder Unterseite gefunden.")
    else:
        erg["ok"] = False
        erg["warnungen"].append("Unbekannte Art: %s" % art)
    return erg
