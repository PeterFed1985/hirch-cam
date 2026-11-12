# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Zweiseitige 3D-Teile: Trennfläche, Haltestege und Seite 2 (gewendet).

Ein Modell, das von beiden Seiten gefräst wird, liegt im Material zwischen Ober- und
Unterseite. Seite 1 fräst von oben bis zur Trennfläche, Seite 2 (nach dem Wenden) von unten
bis knapp über die Trennfläche – um das Modell entsteht so ein durchgehender Spalt. Stehen
bleiben nur das Modell und die Haltestege, die es mit dem Rest des Materials verbinden.

Trennfläche: an der Trennlinie des Modells (Mitte zwischen Ober- und Unterseite am Umriss,
nach aussen fortgesetzt – darf schief sein) oder auf einer Ebene bei einer festen Höhe.

Haltestege: Rechtecke auf der Zeichnung (<svg:rect hirch:steg3d='{"relief", "z"}'>), Dicke
gemeinsam (Einstellung des Modells). Höhe der Stegmitte: automatisch die Trennfläche dort,
wo der Steg am Modell ansetzt, oder von Hand (z).

Für die Fräsbahnen kommt zum Modell ein «Zusatz»-Netz: Boden der Trennfläche um das Modell
und die Stege – Seite 1 ihre Oberseite, Seite 2 ihre Unterseite (gewendet). Schruppen und
Schlichten (ops/relief3d.py) behandeln es wie das Modell, nur ohne Aufmass.

Koordinaten: Material-mm (Nullpunkt unten links auf der Oberfläche, Z = 0 Oberfläche).
Seite 2: gewendet um die eingestellte Achse (x: y → Höhe − y; y: x → Breite − x),
Z → −Dicke − Z (die frühere Unterseite ist dann oben).
"""
from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Dict, List, Optional, Sequence, Tuple

import numpy as np

STANDARD = {"an": False, "trennung": "trennlinie", "z": 0.0, "steg_breite": 8.0,
            "steg_dicke": 3.0, "steg_laenge": 30.0, "ueberlappung": 0.5,
            "haut_an": False, "haut": 0.5,
            "ueber_trenn_auto": True, "ueber_trenn_1": 3.0, "ueber_trenn_2": 3.0}


def ueber_trennlinie(cfg: Dict, seite: int, auftrag: Optional[Dict] = None) -> float:
    """Wie weit Seite «seite» über die Trennfläche hinaus fräst (mm, ohne Haut; Peter 30.10.). Automatisch: der
    Radius des Schlichtfräsers dieser Seite (Kugel d/2, Eck- bzw. Spitzenradius) – sonst bliebe am Rand des
    Modells eine Kehle so hoch wie der Radius stehen. Von Hand: die Werte je Seite."""
    if haut(cfg) > 0:
        return 0.0
    if not cfg.get("ueber_trenn_auto", True):
        try:
            return max(0.0, float(cfg.get("ueber_trenn_%d" % seite) or 0.0))
        except (TypeError, ValueError):
            return 0.0
    r = 0.0
    werkzeuge = (auftrag or {}).get("werkzeuge") or {}
    for b in (auftrag or {}).get("bearbeitungen") or []:
        if b.get("art") != "relief_schlichten" or int(b.get("auf_seite") or 1) != seite:
            continue
        w = werkzeuge.get(b.get("werkzeug")) or {}
        typ = w.get("typ")
        if typ == "kugel":
            r = max(r, float(w.get("durchmesser") or 0) / 2)
        elif typ in ("eckradius", "konisch"):
            r = max(r, float(w.get("eckradius") or 0))
    return r


def haut(cfg: Dict) -> float:
    """Dicke der Haut, die rundherum an der Trennlinie stehen bleibt (0: der Spalt geht durch)."""
    return max(0.0, float(cfg.get("haut") or 0.0)) if cfg.get("haut_an") else 0.0


def einstellungen(info: Optional[Dict]) -> Dict:
    z = dict(STANDARD)
    z.update((info or {}).get("zweiseitig") or {})
    return z


@dataclass
class Lage:
    """Modell im Raster: Ober-/Unterseite, Umriss und Trennlinie (Höhe am Umriss)."""
    x0: float
    y0: float
    zelle: float
    oben: np.ndarray
    unten: np.ndarray
    maske: np.ndarray
    rand: np.ndarray            # Rasterpunkte am Umriss
    zr: np.ndarray              # Trennlinie (Mitte Ober-/Unterseite) an den Randpunkten, sonst nan
    naechster: Tuple[np.ndarray, np.ndarray]   # je Rasterpunkt der nächste Randpunkt (iy, ix)
    abstand: np.ndarray         # Abstand zum nächsten Randpunkt (mm)

    def xy(self, iy, ix):
        return self.x0 + np.asarray(ix) * self.zelle, self.y0 + np.asarray(iy) * self.zelle


def zelle_fuer(bx: Sequence[float], fein: float = 0.5, max_punkte: int = 2_500_000) -> float:
    b, h = bx[2] - bx[0], bx[3] - bx[1]
    z = fein
    while (b / z + 1) * (h / z + 1) > max_punkte:
        z *= 1.25
    return z


def analysieren(tri: np.ndarray, bx: Sequence[float], zelle: float) -> Optional[Lage]:
    from scipy import ndimage
    from .. import hirch_relief
    x0, y0, x1, y1 = bx
    nx, ny = int(math.ceil((x1 - x0) / zelle)) + 1, int(math.ceil((y1 - y0) / zelle)) + 1
    oben, unten, _n = hirch_relief.hoehenkarten(np.ascontiguousarray(tri, np.float32), x0, y0,
                                                zelle, nx, ny)
    maske = np.isfinite(oben) & np.isfinite(unten)
    if not maske.any():
        return None
    rand = maske & ~ndimage.binary_erosion(maske, border_value=0)
    mitte = np.where(rand, (oben + unten) / 2, np.nan).astype(np.float32)
    # glätten: Median der Randpunkte in der Umgebung (5 × 5) – Treppen und Ausreisser an
    # senkrechten Wänden fallen weg
    iy, ix = np.nonzero(rand)
    werte = np.full((25, len(iy)), np.nan, np.float32)
    k = 0
    for dy in range(-2, 3):
        for dx in range(-2, 3):
            jy, jx = np.clip(iy + dy, 0, ny - 1), np.clip(ix + dx, 0, nx - 1)
            werte[k] = mitte[jy, jx]
            k += 1
    zr = np.full_like(mitte, np.nan)
    zr[iy, ix] = np.nanmedian(werte, axis=0)
    dist, (ny_i, nx_i) = ndimage.distance_transform_edt(~rand, return_indices=True)
    return Lage(x0, y0, zelle, oben, unten, maske, rand, zr, (ny_i, nx_i),
                (dist * zelle).astype(np.float32))


def trennhoehe(lage: Lage, cfg: Dict) -> np.ndarray:
    """Höhe der Trennfläche an jedem Rasterpunkt."""
    from scipy import ndimage
    if cfg.get("trennung") == "ebene":
        return np.full(lage.maske.shape, float(cfg.get("z") or 0.0), np.float32)
    # nach aussen fortgesetzt und geglättet (~6 mm): Sprünge der Trennlinie (Kerben, Ecken)
    # würden sonst auf beiden Seiten etwas anders gerastert, und im Spalt bliebe ein Grat
    zp = lage.zr[lage.naechster].astype(np.float32)
    n = max(3, int(round(6.0 / lage.zelle)) | 1)
    return ndimage.uniform_filter(zp, size=n, mode="nearest")


def trennlinie_bereich(lage: Lage) -> Optional[List[float]]:
    z = lage.zr[lage.rand]
    z = z[np.isfinite(z)]
    if not len(z):
        return None
    return [round(float(z.min()), 2), round(float(z.max()), 2)]


def _zellen_in(lage: Lage, poly, nur=None):
    """Rasterpunkte (iy, ix) innerhalb des Polygons (wahlweise nur unter «nur»)."""
    import shapely
    x0, y0, x1, y1 = poly.bounds
    z = lage.zelle
    ny, nx = lage.maske.shape
    i0, i1 = max(0, int(math.floor((x0 - lage.x0) / z))), min(nx - 1, int(math.ceil((x1 - lage.x0) / z)))
    j0, j1 = max(0, int(math.floor((y0 - lage.y0) / z))), min(ny - 1, int(math.ceil((y1 - lage.y0) / z)))
    if i1 < i0 or j1 < j0:
        return np.zeros(0, int), np.zeros(0, int)
    jj, ii = np.mgrid[j0:j1 + 1, i0:i1 + 1]
    jj, ii = jj.ravel(), ii.ravel()
    if nur is not None:
        sel = nur[jj, ii]
        jj, ii = jj[sel], ii[sel]
    x, y = lage.xy(jj, ii)
    drin = shapely.contains_xy(poly, x, y)
    return jj[drin], ii[drin]


def steg_hoehe(lage: Lage, ecken: Sequence[Sequence[float]], cfg: Dict) -> float:
    """Automatische Höhe der Stegmitte: Trennfläche, wo der Steg am Modell ansetzt."""
    from shapely.geometry import Polygon
    if cfg.get("trennung") == "ebene":
        return round(float(cfg.get("z") or 0.0), 2)
    poly = Polygon([tuple(p) for p in ecken]).buffer(1.0)
    jj, ii = _zellen_in(lage, poly, lage.rand)
    if len(jj):
        return round(float(np.nanmedian(lage.zr[jj, ii])), 2)
    c = poly.centroid
    ny, nx = lage.maske.shape
    i = min(max(int(round((c.x - lage.x0) / lage.zelle)), 0), nx - 1)
    j = min(max(int(round((c.y - lage.y0) / lage.zelle)), 0), ny - 1)
    return round(float(lage.zr[lage.naechster[0][j, i], lage.naechster[1][j, i]]), 2)


def stege_mit_hoehe(lage: Lage, stege: List[Dict], cfg: Dict) -> List[Dict]:
    aus = []
    for s in stege:
        auto = steg_hoehe(lage, s["ecken"], cfg)
        z = s.get("z")
        aus.append(dict(s, z_auto=auto, z_mitte=float(z) if z is not None else auto))
    return aus


def zusatz(lage: Lage, cfg: Dict, stege: List[Dict], reichweite: float, dicke: float,
           seite: int) -> np.ndarray:
    """Höhenraster des Zusatzes: Boden der Trennfläche um das Modell (bis «reichweite» vom
    Umriss) und Stege. Seite 1: Oberseite; Seite 2: Unterseite (Materialkoordinaten)."""
    from scipy import ndimage
    from shapely.geometry import Polygon
    zp = trennhoehe(lage, cfg)
    bereich = ~ndimage.binary_erosion(lage.maske, border_value=0) & (lage.abstand <= reichweite)
    d = max(0.1, float(cfg.get("steg_dicke") or 3.0))
    # Ein Schaftfräser folgt einer schiefen Trennfläche nicht ganz (er sitzt auf der höchsten
    # Stelle unter sich). Damit sich die Schnitte trotzdem überall treffen: Seite 1 bis zur
    # tiefsten, Seite 2 bis zur höchsten Stelle der Trennfläche in der Umgebung (6 mm).
    n = max(3, int(round(12.0 / lage.zelle)) | 1)
    h = haut(cfg)
    if h > 0:
        # Haut statt durchfräsen: mitten auf der Trennfläche bleiben «h» mm stehen – jede Seite fräst die
        # halbe Dicke weniger tief (an schiefen Stellen eher dicker, nie dünner)
        if seite == 1:
            e = ndimage.maximum_filter(zp, size=n, mode="nearest") + h / 2
        else:
            e = ndimage.minimum_filter(zp, size=n, mode="nearest") - h / 2
    elif seite == 1:
        # «_ueber»: über die Trennfläche hinaus (ueber_trennlinie, von _modelle eingesetzt)
        e = ndimage.minimum_filter(zp, size=n, mode="nearest") - max(0.0, float(cfg.get("_ueber") or 0.0))
    else:
        e = ndimage.maximum_filter(zp, size=n, mode="nearest") + max(float(cfg.get("ueberlappung") or 0.0),
                                                                    float(cfg.get("_ueber") or 0.0), 0.0)
    for s in stege:
        poly = Polygon([tuple(p) for p in s["ecken"]])
        if not poly.is_valid or poly.area <= 0:
            continue
        jj, ii = _zellen_in(lage, poly, bereich)
        if not len(jj):
            continue
        zm = float(s["z_mitte"])
        if seite == 1:
            e[jj, ii] = np.maximum(e[jj, ii], zm + d / 2)
        else:
            e[jj, ii] = np.minimum(e[jj, ii], zm - d / 2)
    e = np.clip(e, -dicke, 0.0)
    e[~bereich] = np.nan
    return e.astype(np.float32)


def steg_koerper(stege: List[Dict], cfg: Dict, dicke: float, seite: int, ohne=None) -> np.ndarray:
    """Haltestege als genaue Körper mit senkrechten Wänden (Dreiecke, Materialkoordinaten) – zusätzlich zum Raster des
    Zusatzes. Im Raster (Zelle um 1 mm) waren die Seitenwände eine Rampe über eine Zelle, die Fräser schnitten bis 1 mm
    in den Steg hinein und liessen an seinen Kanten statt 3 mm nur 2 mm stehen (Sicherheitsprüfung 04.11.).
    Seite 1: vom Boden bis zur Oberkante des Stegs; Seite 2: von der Unterkante bis zur Oberfläche (danach gewendet wie
    der Zusatz). ohne: Umriss des Modells – über dem Modell gilt das Modell (wie im Raster)."""
    from shapely.geometry import Polygon
    from shapely.ops import triangulate
    d = max(0.1, float(cfg.get("steg_dicke") or 3.0))
    tri = []
    for s in stege:
        e = [tuple(map(float, p[:2])) for p in s.get("ecken") or []]
        if len(e) < 3 or s.get("z_mitte") is None:
            continue
        zm = float(s["z_mitte"])
        z0, z1 = (-dicke, zm + d / 2) if seite == 1 else (zm - d / 2, 0.0)
        z0, z1 = max(z0, -dicke), min(z1, 0.0)
        if z1 <= z0:
            continue
        g = Polygon(e)
        if not g.is_valid or g.area <= 0:
            continue
        if ohne is not None and not ohne.is_empty:
            g = g.difference(ohne)
        for poly in getattr(g, "geoms", [g]):
            if poly.geom_type != "Polygon" or poly.area < 0.01:
                continue
            poly = poly.segmentize(0.5)
            innen = poly.buffer(-1e-6)
            for t in triangulate(poly):                  # Deckel und Boden
                if not innen.contains(t.centroid):
                    continue
                a, b, c = list(t.exterior.coords)[:3]
                tri += [((*a, z1), (*b, z1), (*c, z1)), ((*a, z0), (*c, z0), (*b, z0))]
            for ring in [poly.exterior] + list(poly.interiors):   # senkrechte Wände
                q = list(ring.coords)
                for (x1, y1), (x2, y2) in zip(q, q[1:]):
                    tri += [((x1, y1, z0), (x2, y2, z0), (x2, y2, z1)), ((x1, y1, z0), (x2, y2, z1), (x1, y1, z1))]
    return np.array(tri, np.float32).reshape(-1, 3, 3)


def gitter(e: np.ndarray, x0: float, y0: float, zelle: float) -> np.ndarray:
    """Höhenraster als Dreiecke (nur Vierecke mit vier gültigen Ecken)."""
    ny, nx = e.shape
    ok = np.isfinite(e[:-1, :-1]) & np.isfinite(e[:-1, 1:]) & np.isfinite(e[1:, :-1]) & np.isfinite(e[1:, 1:])
    j, i = np.nonzero(ok)
    if not len(j):
        return np.zeros((0, 3, 3), np.float32)

    jx, jy = gitter_versatz(e.shape)

    def p(ii, jj):
        return np.stack([x0 + ii * zelle + jx[jj, ii], y0 + jj * zelle + jy[jj, ii], e[jj, ii]], axis=-1)
    a, b, c, d = p(i, j), p(i + 1, j), p(i + 1, j + 1), p(i, j + 1)
    return np.concatenate([np.stack([a, b, c], 1), np.stack([a, c, d], 1)]).astype(np.float32)


def gitter_versatz(form, groesse: float = 0.002):
    """Winziger, fester Versatz je Gitterpunkt (±2 µm). OpenCAMLib baut seinen Suchbaum über
    die Koordinaten der Dreiecke; liegen Tausende genau auf denselben Gitterlinien, entartet er
    und das Abtasten wird 20-mal langsamer. Gemeinsame Ecken bekommen denselben Versatz – das
    Netz bleibt geschlossen."""
    rng = np.random.default_rng(12345)
    return (rng.uniform(-groesse, groesse, form), rng.uniform(-groesse, groesse, form))


def wenden(tri: np.ndarray, material: Tuple[float, float, float], achse: str) -> np.ndarray:
    """In die Koordinaten von Seite 2 (nach dem Wenden, von oben gesehen)."""
    aus = np.array(tri, np.float32, copy=True)
    if achse == "y":
        aus[..., 0] = material[0] - aus[..., 0]
    else:
        aus[..., 1] = material[1] - aus[..., 1]
    aus[..., 2] = -material[2] - aus[..., 2]
    return np.ascontiguousarray(aus)


def wenden_punkte(pts, material, achse: str):
    if achse == "y":
        return [(material[0] - x, y) for x, y in pts]
    return [(x, material[1] - y) for x, y in pts]


def umriss(lage: Lage, tol: float = 0.05):
    """Umriss des Modells (shapely, Materialkoordinaten)."""
    from .relief3d import _flaechen_aus
    return _flaechen_aus(lage.maske.astype(np.float32), 0.5, lage.x0, lage.y0, lage.zelle, tol)


@dataclass
class Geometrie:
    modell: np.ndarray                 # Dreiecke des Modells (in den Koordinaten der Seite)
    zusatz: Optional[np.ndarray]       # Boden und Stege (dito), None: einseitig
    stege: List[Dict]                  # mit z_auto/z_mitte (Materialkoordinaten)
    umriss: object                     # Umriss des Modells (Materialkoordinaten, Seite 1)
    trennlinie: Optional[List[float]]
    haut: float = 0.0                  # Haut an der Trennlinie (mm, 0: Spalt geht durch)


def geometrie(tri: np.ndarray, stege: List[Dict], cfg: Dict, seite: int,
              material: Tuple[float, float, float], achse: str, reichweite: float) -> Geometrie:
    """Modell und Zusatz für eine Seite eines zweiseitigen Modells (tri in Materialkoordinaten
    von Seite 1)."""
    lo, hi = tri.reshape(-1, 3).min(0), tri.reshape(-1, 3).max(0)
    m = reichweite + 2
    bx = (max(-2.0, float(lo[0]) - m), max(-2.0, float(lo[1]) - m),
          min(material[0] + 2, float(hi[0]) + m), min(material[1] + 2, float(hi[1]) + m))
    lage = analysieren(tri, bx, zelle_fuer(bx, fein=1.0))
    if lage is None:
        return Geometrie(tri if seite == 1 else wenden(tri, material, achse), None, [], None, None)
    st = stege_mit_hoehe(lage, stege, cfg)
    e = zusatz(lage, cfg, st, reichweite, material[2], seite)
    extra = gitter(e, lage.x0, lage.y0, lage.zelle)
    from .relief3d import umriss_modell
    koerper = steg_koerper(st, cfg, material[2], seite,
                           umriss_modell(tri, lage.maske, lage.x0, lage.y0, lage.zelle, 0.02) if st else None)
    if len(koerper):
        extra = np.concatenate([extra, koerper]).astype(np.float32)
    mod = tri
    if seite == 2:
        mod = wenden(tri, material, achse)
        extra = wenden(extra, material, achse)
    return Geometrie(mod, extra, st, umriss(lage), trennlinie_bereich(lage), haut(cfg))


# --------------------------------------------------------------------------------------------
# Für den Reiter «3D»: Trennlinie, Höhen der Stege, Vorschlag für neue Stege

def stege_vorschlag(lage: Lage, anzahl: int, breite: float, laenge: float, innen: float = 5.0,
                    meiden: Sequence[Sequence[Sequence[float]]] = ()) -> List[Dict]:
    """Stege gleichmässig um den Umriss (mit vorhandenen Stegen «meiden»: in die grössten
    Lücken dazwischen): Mitte, Winkel (Grad, Breite entlang des Umrisses), Breite und Länge
    (quer zum Umriss, «innen» mm ins Modell hinein)."""
    from shapely.geometry import Polygon
    u = umriss(lage, tol=0.2)
    teile = [g for g in getattr(u, "geoms", [u]) if g.geom_type == "Polygon" and not g.is_empty]
    if not teile or anzahl <= 0:
        return []
    gross = max(teile, key=lambda g: g.area)
    ring = gross.exterior
    if not ring.is_ccw:
        ring = type(ring)(list(ring.coords)[::-1])
    L = ring.length
    besetzt = [Polygon([tuple(p) for p in e]) for e in meiden]
    # Anfang so, dass die Stege möglichst nicht in spitzen Ecken landen: Verschiebung, bei
    # der die Richtungsänderung an den Stegen am kleinsten ist
    def knick(s):
        a, b = ring.interpolate((s - 3) % L), ring.interpolate((s + 3) % L)
        m_ = ring.interpolate(s % L)
        v1 = np.array([m_.x - a.x, m_.y - a.y])
        v2 = np.array([b.x - m_.x, b.y - m_.y])
        n1, n2 = np.linalg.norm(v1), np.linalg.norm(v2)
        if n1 < 1e-9 or n2 < 1e-9:
            return 0.0
        return float(1 - v1 @ v2 / (n1 * n2))
    if besetzt:
        # neue Stege in die grössten Lücken zwischen den vorhandenen
        lagen = sorted(ring.project(q.centroid) for q in besetzt)
        orte = []
        for _ in range(anzahl):
            alle = sorted(lagen + orte)
            luecken = [((alle[(k + 1) % len(alle)] - alle[k]) % L or L, alle[k]) for k in range(len(alle))]
            g, a0 = max(luecken)
            orte.append((a0 + g / 2) % L)
    else:
        beste, beste_wert = 0.0, None
        for k in range(24):
            start = L / anzahl * k / 24
            w = sum(knick(start + L * (i + 0.5) / anzahl) for i in range(anzahl))
            if beste_wert is None or w < beste_wert - 1e-9:
                beste, beste_wert = start, w
        orte = [(beste + L * (i + 0.5) / anzahl) % L for i in range(anzahl)]
    aus = []
    for s in orte:
        p = ring.interpolate(s)
        a, b = ring.interpolate((s - 2) % L), ring.interpolate((s + 2) % L)
        t = np.array([b.x - a.x, b.y - a.y])
        n_t = np.linalg.norm(t)
        if n_t < 1e-9:
            continue
        t /= n_t
        n = np.array([t[1], -t[0]])          # aussen (Ring gegen den Uhrzeigersinn)
        mitte = np.array([p.x, p.y]) + n * (laenge / 2 - innen)
        winkel = math.degrees(math.atan2(t[1], t[0]))
        ecken = _rechteck(mitte, winkel, breite, laenge)
        aus.append({"mitte": [round(float(mitte[0]), 3), round(float(mitte[1]), 3)],
                    "winkel": round(winkel, 3), "breite": breite, "laenge": laenge,
                    "ecken": [[round(x, 3), round(y, 3)] for x, y in ecken]})
    return aus


def _rechteck(mitte, winkel: float, breite: float, laenge: float):
    """Ecken: Breite entlang «winkel», Länge quer dazu."""
    c, s = math.cos(math.radians(winkel)), math.sin(math.radians(winkel))
    t, n = np.array([c, s]), np.array([-s, c])
    m = np.asarray(mitte, float)
    return [tuple(m + t * bx + n * ly) for bx, ly in
            ((-breite / 2, -laenge / 2), (breite / 2, -laenge / 2), (breite / 2, laenge / 2),
             (-breite / 2, laenge / 2))]


def stege_info(relief: Dict, dicke: float, anzahl: int = 0, breite: Optional[float] = None,
               laenge: Optional[float] = None) -> Dict:
    """Für den Reiter «3D»: Trennlinie, automatische Höhen der vorhandenen Stege, auf Wunsch
    ein Vorschlag für «anzahl» neue Stege. relief wie aus relief_svg.lesen (info, daten, ecken,
    stege)."""
    from .relief3d import modell_im_material
    info = relief.get("info") or {}
    cfg = einstellungen(info)
    tri, v = modell_im_material(relief, dicke)
    lo, hi = tri.reshape(-1, 3).min(0), tri.reshape(-1, 3).max(0)
    rw = float(cfg.get("steg_laenge") or 30.0) + 5
    bx = (float(lo[0]) - rw, float(lo[1]) - rw, float(hi[0]) + rw, float(hi[1]) + rw)
    lage = analysieren(tri, bx, zelle_fuer(bx, max_punkte=1_500_000))
    erg = {"ok": True, "warnungen": [], "z_versatz": v["z_versatz"]}
    if lage is None:
        erg["trennlinie"] = None
        erg["stege"] = []
        return erg
    tl = trennlinie_bereich(lage)
    erg["trennlinie"] = tl
    erg["schief"] = bool(tl and tl[1] - tl[0] > 0.5)
    erg["stege"] = [{"id": s.get("id", ""), "z_auto": s["z_auto"]}
                    for s in stege_mit_hoehe(lage, relief.get("stege") or [], cfg)]
    if anzahl:
        b = float(breite or cfg.get("steg_breite") or 8.0)
        L = float(laenge or cfg.get("steg_laenge") or 30.0)
        vor = stege_vorschlag(lage, anzahl, b, L, meiden=[s["ecken"] for s in relief.get("stege") or []])
        for s in vor:
            s["z_auto"] = steg_hoehe(lage, s["ecken"], cfg)
        erg["vorschlag"] = vor
    return erg
