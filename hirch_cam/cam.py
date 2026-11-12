# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Das CAM-Modul von Hirch CAM (hirch_cam) aus Python benutzen.

hirch_cam ist aus dem FreeCAD-CAM-Modul gebaut (libarea mit Clipper für
Versatz und Taschen, Mittellinien der V-Carve wie FreeCAD), aber ohne
FreeCAD selbst: eine kleine Python-Erweiterung (cam_kern/, bauen.sh).
Die übrige Geometrie (Ringe, Löcher, Reihenfolge, Haltestege, Tiefen)
rechnet Hirch CAM selbst. Fehlt das Modul, gibt verfuegbar() False zurück und
die Fräsarten nehmen ihren eigenen Weg (Shapely/SciPy).
"""
from __future__ import annotations

import math
from typing import Dict, List, Sequence, Tuple

try:
    from . import hirch_cam as _hc        # type: ignore
except ImportError:                       # Modul nicht gebaut / anderes Python
    _hc = None

Punkt = Tuple[float, float]


def verfuegbar() -> bool:
    return _hc is not None


def version() -> str:
    return getattr(_hc, "VERSION", "") if _hc else ""


def _flaeche(ring: Sequence[Punkt]) -> float:
    s = 0.0
    for (x0, y0), (x1, y1) in zip(ring, list(ring[1:]) + [ring[0]]):
        s += x0 * y1 - x1 * y0
    return s / 2.0


def linearisieren(kurve, toleranz: float) -> List[Punkt]:
    """Kurve von hirch_cam (Strecken und Bögen) als Punktfolge."""
    if not kurve:
        return []
    pts = [(kurve[0][1], kurve[0][2])]
    for typ, x, y, cx, cy in kurve[1:]:
        x0, y0 = pts[-1]
        if typ == 0:
            pts.append((x, y))
            continue
        r = math.hypot(x0 - cx, y0 - cy)
        a0 = math.atan2(y0 - cy, x0 - cx)
        a1 = math.atan2(y - cy, x - cx)
        if typ > 0:                      # gegen den Uhrzeiger
            while a1 <= a0 + 1e-12:
                a1 += 2 * math.pi
        else:
            while a1 >= a0 - 1e-12:
                a1 -= 2 * math.pi
        if r < 1e-9:
            pts.append((x, y))
            continue
        dphi = 2 * math.acos(max(-1.0, min(1.0, 1 - toleranz / r))) if toleranz < r else math.pi / 4
        n = max(1, int(math.ceil(abs(a1 - a0) / max(dphi, 1e-3))))
        for i in range(1, n):
            a = a0 + (a1 - a0) * i / n
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
        pts.append((x, y))
    return pts


def _offen(pts: List[Punkt]) -> List[Punkt]:
    if len(pts) > 1 and math.dist(pts[0], pts[-1]) < 1e-9:
        return pts[:-1]
    return pts


def versatz(ringe: Sequence[Sequence[Punkt]], abstand: float,
            toleranz: float = 0.01) -> List[Tuple[List[Punkt], bool]]:
    """Fläche (Ringe, gerade-ungerade) um «abstand» versetzen, + = nach aussen.
    Liefert [(Ring, ist_loch)], Aussenringe gegen den Uhrzeiger, Löcher im Uhrzeigersinn."""
    eingabe = [[list(p) for p in r] for r in ringe if len(r) >= 3]
    if not eingabe:
        return []
    aus = []
    for k in _hc.versatz(eingabe, float(abstand), float(toleranz)):
        pts = _offen(linearisieren(k, toleranz))
        if len(pts) >= 3 and abs(_flaeche(pts)) > 1e-9:
            aus.append((pts, _flaeche(pts) < 0))
    return aus


# ------------------------------------------------------------ V-Carve
def _ketten(kanten) -> List[List[Tuple[float, float, float]]]:
    """Kanten zu Zügen verbinden – wie FreeCAD Vcarve._collectVoronoiWires."""
    an: Dict[int, List[int]] = {}
    for i, (v0, v1, _pts) in enumerate(kanten):
        an.setdefault(v0, []).append(i)
        an.setdefault(v1, []).append(i)
    knoten = [v for v, ks in an.items() if len(ks) == 1]
    knoten += [v for v, ks in an.items() if len(ks) > 2]
    if not knoten and an:
        knoten = [next(iter(an))]
    benutzt = set()
    zuege = []

    def weiter(v):
        for i in an.get(v, []):
            if i not in benutzt:
                return i
        return None

    while True:
        start = None
        for v in knoten:
            if weiter(v) is not None:
                start = v
                break
        if start is None:
            # nur noch geschlossene Schleifen übrig
            rest = [i for i in range(len(kanten)) if i not in benutzt]
            if not rest:
                break
            start = kanten[rest[0]][0]
        zug: List[Tuple[float, float, float]] = []
        v = start
        while True:
            i = weiter(v)
            if i is None:
                break
            benutzt.add(i)
            v0, v1, pts = kanten[i]
            folge = list(pts) if v0 == v else list(reversed(pts))
            zug.extend(folge if not zug else folge[1:])
            v = v1 if v0 == v else v0
            if len(an[v]) > 2:      # an Verzweigungen endet ein Zug (FreeCAD: knots)
                break
        if len(zug) >= 2:
            zuege.append([tuple(p) for p in zug])
    return zuege


def mittellinien(region, toleranz: float = 0.01) -> List[List[Tuple[float, float, float]]]:
    """Mittellinien einer Shapely-Fläche als Züge [(x, y, Abstand zum Rand)],
    je Teilfläche einzeln (wie FreeCAD: je Face)."""
    aus = []
    for poly in getattr(region, "geoms", [region]):
        if poly.is_empty:
            continue
        ringe = [list(poly.exterior.coords)[:-1]] + [list(i.coords)[:-1] for i in poly.interiors]
        ringe = [[list(p) for p in r] for r in ringe if len(r) >= 3]
        kanten = _hc.mittellinien(ringe, float(toleranz), 10.0, 0.01)
        aus.extend(_ketten(kanten))
    return aus


def puffer(geom, abstand: float, toleranz: float = 0.01):
    """Wie Shapely buffer(abstand, join_style=round), aber gerechnet mit
    libarea/Clipper aus FreeCAD CAM. Nimmt und liefert eine Shapely-Fläche."""
    from shapely.geometry import MultiPolygon, Polygon
    from shapely.ops import unary_union

    ringe = []
    for p in getattr(geom, "geoms", [geom]):
        if p.is_empty or p.geom_type != "Polygon":
            continue
        ringe.append(list(p.exterior.coords)[:-1])
        ringe.extend(list(i.coords)[:-1] for i in p.interiors)
    if not ringe:
        return Polygon()
    teile = versatz(ringe, abstand, toleranz)
    aussen = [Polygon(r) for r, loch in teile if not loch]
    loecher = [Polygon(r) for r, loch in teile if loch]
    polys = []
    for a in aussen:
        innen = [l for l in loecher if a.contains(l.representative_point())]
        polys.append(Polygon(a.exterior.coords, [l.exterior.coords for l in innen]))
    if not polys:
        return Polygon()
    erg = polys[0] if len(polys) == 1 else MultiPolygon(polys)
    if not erg.is_valid:
        erg = unary_union([p.buffer(0) for p in polys])
    return erg


def adaptiv(flaeche, werkzeug_d: float, helix_d: float, bahnabstand: float,
            toleranz: float = 0.05, zugabe: float = 0.0, schlichtbahn: bool = True):
    """Adaptives Ausräumen einer Shapely-Fläche (FreeCAD CAM Adaptive).
    Liefert [(helix_mitte, startpunkt, [(art, [(x, y), …]), …])];
    art: 0 schneiden, 1 Übergang frei, 2 Übergang durch Material, 3 frei auf
    der vorigen Ebene."""
    ringe = []
    for p in getattr(flaeche, "geoms", [flaeche]):
        if p.is_empty or p.geom_type != "Polygon":
            continue
        ringe.append([list(c) for c in list(p.exterior.coords)[:-1]])
        ringe.extend([list(c) for c in list(i.coords)[:-1]] for i in p.interiors)
    if not ringe:
        return []
    x0, y0, x1, y1 = flaeche.bounds
    rand = 2 * werkzeug_d
    roh = [[[x0 - rand, y0 - rand], [x1 + rand, y0 - rand], [x1 + rand, y1 + rand],
            [x0 - rand, y1 + rand]]]
    aus = []
    for hc, sp, wege, _rt in _hc.adaptiv(ringe, roh, float(werkzeug_d), float(helix_d),
                                        float(bahnabstand), float(toleranz), float(zugabe),
                                        bool(schlichtbahn)):
        aus.append((tuple(hc), tuple(sp), [(int(art), [tuple(q) for q in pts]) for art, pts in wege]))
    return aus
