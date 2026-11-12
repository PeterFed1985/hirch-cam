# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Mittellinien (Medial Axis) einer Fläche – Grundlage für V-Carve.

Ohne FreeCAD: Der Rand wird dicht abgetastet, aus den Punkten ein
Voronoi-Diagramm gerechnet (SciPy/Qhull). Die Voronoi-Kanten im Inneren,
deren zwei erzeugende Randpunkte auf "verschiedenen Seiten" liegen, bilden
die Mittellinie. Kanten zwischen benachbarten Randpunkten (Rauschen der
Abtastung) werden verworfen.

Ergebnis: Polylinien (Liste von (x, y)), zusammengesetzt aus den Kanten,
von Verzweigung zu Verzweigung.
"""
from __future__ import annotations

import math
from collections import defaultdict
from typing import Dict, List, Tuple

Punkt = Tuple[float, float]


def _abtasten(region, abstand: float):
    """Randpunkte im Abstand `abstand`. Liefert (punkte, ring_id, bogenpos)."""
    pts, ring_ids, pos = [], [], []
    rid = 0
    for p in getattr(region, "geoms", [region]):
        for ring in [p.exterior] + list(p.interiors):
            laenge = ring.length
            n = max(8, int(math.ceil(laenge / abstand)))
            for k in range(n):
                s = laenge * k / n
                q = ring.interpolate(s)
                pts.append((q.x, q.y))
                ring_ids.append(rid)
                pos.append(s)
            rid += 1
    laengen = {}
    rid = 0
    for p in getattr(region, "geoms", [region]):
        for ring in [p.exterior] + list(p.interiors):
            laengen[rid] = ring.length
            rid += 1
    return pts, ring_ids, pos, laengen


def mittellinien(region, abstand: float = 0.15, min_winkel: float = 25.0) -> List[List[Punkt]]:
    """Mittellinien der Fläche (Shapely Polygon/MultiPolygon)."""
    import numpy as np
    from scipy.spatial import Voronoi
    from shapely import vectorized

    pts, ring_ids, pos, laengen = _abtasten(region, abstand)
    if len(pts) < 4:
        return []
    P = np.asarray(pts)
    vor = Voronoi(P)
    V = vor.vertices
    innen = vectorized.contains(region, V[:, 0], V[:, 1]) if len(V) else np.zeros(0, bool)
    cos_min = math.cos(math.radians(min_winkel))  # Winkel am Voronoi-Punkt

    kanten = []
    for (i, j), (a, b) in zip(vor.ridge_points, vor.ridge_vertices):
        if a < 0 or b < 0 or not (innen[a] and innen[b]):
            continue
        # Nachbarn auf demselben Rand erzeugen nur Rauschen
        if ring_ids[i] == ring_ids[j]:
            d = abs(pos[i] - pos[j])
            d = min(d, laengen[ring_ids[i]] - d)
            if d < 2.5 * abstand:
                continue
        # Winkel zwischen den Richtungen zu den beiden Randpunkten: auf der
        # echten Mittellinie gross, bei Abtast-Rauschen klein
        ok = True
        for v in (a, b):
            u1 = P[i] - V[v]
            u2 = P[j] - V[v]
            n1, n2 = np.hypot(*u1), np.hypot(*u2)
            if n1 < 1e-12 or n2 < 1e-12:
                continue
            c = float(u1 @ u2) / (n1 * n2)
            if c > cos_min:  # Winkel kleiner als min_winkel
                ok = False
                break
        if ok:
            kanten.append((a, b))
    return _ketten(kanten, V)


def _ketten(kanten, V) -> List[List[Punkt]]:
    """Kanten zu Polylinien zusammensetzen (Ende an Knoten mit Grad != 2)."""
    nachbarn: Dict[int, List[int]] = defaultdict(list)
    for a, b in kanten:
        if a == b:
            continue
        nachbarn[a].append(b)
        nachbarn[b].append(a)
    besucht = set()

    def kante(u, v):
        return (u, v) if u < v else (v, u)

    ketten = []
    knoten = [n for n, nb in nachbarn.items() if len(nb) != 2]
    for start in knoten:
        for nb in nachbarn[start]:
            if kante(start, nb) in besucht:
                continue
            kette = [start, nb]
            besucht.add(kante(start, nb))
            vor, cur = start, nb
            while len(nachbarn[cur]) == 2:
                nxt = nachbarn[cur][0] if nachbarn[cur][0] != vor else nachbarn[cur][1]
                if kante(cur, nxt) in besucht:
                    break
                besucht.add(kante(cur, nxt))
                kette.append(nxt)
                vor, cur = cur, nxt
            ketten.append(kette)
    # Reine Ringe (alle Knoten Grad 2), z. B. Mitte eines Kreisrings
    for a, nb in nachbarn.items():
        for b in nb:
            if kante(a, b) in besucht:
                continue
            kette = [a, b]
            besucht.add(kante(a, b))
            vor, cur = a, b
            while cur != a:
                nxt = nachbarn[cur][0] if nachbarn[cur][0] != vor else nachbarn[cur][1]
                if kante(cur, nxt) in besucht:
                    break
                besucht.add(kante(cur, nxt))
                kette.append(nxt)
                vor, cur = cur, nxt
            ketten.append(kette)
    return [[(float(V[k][0]), float(V[k][1])) for k in kette] for kette in ketten]


def vereinfachen3d(pts: List[Tuple[float, float, float]], tol: float) -> List[Tuple[float, float, float]]:
    """Douglas-Peucker in 3D (erhält Knicke in der Tiefe)."""
    if len(pts) < 3:
        return list(pts)
    keep = [False] * len(pts)
    keep[0] = keep[-1] = True
    stapel = [(0, len(pts) - 1)]
    while stapel:
        a, b = stapel.pop()
        ax, ay, az = pts[a]
        bx, by, bz = pts[b]
        dx, dy, dz = bx - ax, by - ay, bz - az
        L2 = dx * dx + dy * dy + dz * dz
        best, bi = -1.0, -1
        for i in range(a + 1, b):
            px, py, pz = pts[i]
            if L2 < 1e-18:
                d = math.dist(pts[i], pts[a])
            else:
                t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy + (pz - az) * dz) / L2))
                d = math.dist((px, py, pz), (ax + dx * t, ay + dy * t, az + dz * t))
            if d > best:
                best, bi = d, i
        if best > tol:
            keep[bi] = True
            stapel.append((a, bi))
            stapel.append((bi, b))
    return [p for p, k in zip(pts, keep) if k]
