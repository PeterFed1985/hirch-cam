# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Böden und Wände («boeden_waende», Peter 25.10.): waagrechte Flächen und senkrechte Wände des 3D-Modells
mit dem Schaftfräser genau auf Mass – aus den genauen Schnitten des Modells (STEP: die Flächen der
Konstruktion), mit den Wegen der 2D-Tasche (Runden von innen nach aussen) und der Kontur (Umlauf im Abstand
des Radius).

Die Bearbeitung gehört zu einem 3D-Schruppen derselben Seite («im_wechsel_mit», sonst das letzte davor): sie
fräst nur, wo das Schruppen hinkam (sonst wäre es ein Vollschnitt in einer Lücke), und taucht nur dort im
Eilgang ein, wo es geräumt hat. Stufenweise: je «stufe» mm (von Hand, Vorgabe 10) erst die Ebenen des
Schruppens, dann die Wände dieser Stufe – dünne Wände werden oben fertig, solange unten noch Material sie
hält. Mit demselben Fräser verschränkt die Engine die beiden Stufe um Stufe (engine.py).

Ergebnis: [(Stufe, Werkzeugweg)] – Stufe 0 = die oberste (0 … −stufe mm).
"""
from __future__ import annotations

import math
from typing import Dict, List, Optional, Tuple

import numpy as np

from .bahn import Bahnbauer

ART = "boeden_waende"
STUFE = 10.0


def stufe_mm(p: Dict, w: Dict) -> float:
    """Stufenhöhe: von Hand (Vorgabe 10 mm), höchstens die Schneidenlänge − 0,5 mm."""
    schneide = float(w.get("schneidenlaenge") or w.get("laenge") or 50)
    s = float(p.get("stufe") if p.get("stufe") not in (None, "") else STUFE)
    if s <= 0 or s > schneide - 0.5:
        s = max(1.0, schneide - 0.5)
    return s


def stufe_von(z: float, stufe: float) -> int:
    """Stufe einer Ebene: 0 für 0 … −stufe (einschliesslich), 1 für darunter …"""
    return max(0, int(math.floor(-z / stufe - 1e-6)))


def aufteilen(tp, stufe: float, freifahrhoehe: float):
    """Werkzeugweg des 3D-Schruppens in Stufen: [(Stufe, Werkzeugweg)]. Geteilt wird nach dem Abheben vor dem
    ersten Schnitt unter die Stufengrenze – jedes Stück beginnt auf der Freifahrhöhe."""
    from ..model import Toolpath
    moves = tp.moves
    aus: List[Tuple[int, list]] = []
    k_akt, anfang = 0, 0
    oben = freifahrhoehe - 1e-6
    for i, m in enumerate(moves):
        if m.rapid:
            continue
        k = stufe_von(m.z, stufe) if m.z < -1e-6 else 0
        if k <= k_akt:
            continue
        # das letzte Abheben auf die Freifahrhöhe davor (Z-Eilgang von unten herauf)
        j = i - 1
        while j > anfang and not (moves[j].rapid and moves[j].z >= oben and moves[j - 1].z < oben):
            j -= 1
        if j <= anfang:
            k_akt = k                  # kein Abheben dazwischen: nicht teilen (nie mitten im Material)
            continue
        aus.append((k_akt, moves[anfang:j + 1]))
        anfang, k_akt = j + 1, k
    aus.append((k_akt, moves[anfang:]))
    return [(k, Toolpath(name=tp.name, moves=list(ms))) for k, ms in aus if ms]


def schrupp_ebenen(oben, maske, zelle: float, d: float, zust: float, aufmass: float, z_unten: float) -> List[float]:
    """Ebenen des 3D-Schruppens (wie relief3d.schruppen): je Zustellung, die unterste, und auf jeder flachen
    Stelle Höhe + Aufmass."""
    from .relief3d import _flache_hoehen
    ebenen = []
    z = -zust
    while z > z_unten + 1e-6:
        ebenen.append(z)
        z -= zust
    ebenen.append(z_unten)
    for h in _flache_hoehen(oben, maske, zelle, d):
        zf = h + aufmass
        if not (z_unten - 1e-6 <= zf < -1e-6):
            continue
        for e in [e for e in ebenen if abs(zf - e) <= 0.05]:
            ebenen.remove(e)
        ebenen.append(zf)
    return sorted(ebenen, reverse=True)


def rechnen(tri: np.ndarray, material: Tuple[float, float, float], w: Dict, p: Dict, freifahrhoehe: float,
            startabstand: float, start, name: str, tol: float, extra: Optional[np.ndarray],
            vor_w: Dict, vor_p: Dict):
    """vor_w, vor_p: Werkzeug und Werte des 3D-Schruppens davor (wo es hinkam, was es stehen liess)."""
    from shapely.geometry import LineString, Point
    from shapely.ops import linemerge, nearest_points, unary_union
    from .bahn import flaeche, kurze_reihenfolge
    from .tasche import _innen_nach_aussen, _ringe_von
    from .relief3d import (_Bestand, _Schnitte, _begrenzen, _bereich, _flache_hoehen, _flaechen_aus, _raster,
                           _versetzt, _zahl, _bx, ueber_rand, umriss_modell)
    warnungen: List[str] = []
    d = float(w["durchmesser"])
    r = d / 2
    aufmass = max(0.0, float(p.get("aufmass") or 0.0))
    boeden = p.get("boeden") is not False
    waende = p.get("waende") is not False
    schritt = d * max(5.0, min(90.0, float(p.get("bahnabstand") or 40))) / 100.0
    stufe = stufe_mm(p, w)
    # das Schruppen davor
    d_s = float(vor_w.get("durchmesser") or d)
    r_s = d_s / 2
    a_s = max(0.0, float(vor_p.get("aufmass") if vor_p.get("aufmass") is not None else 0.5))
    zust_s = float(vor_p.get("zustellung") or 0) or d_s / 2
    rand_s = float(vor_p.get("rand")) if vor_p.get("rand") not in (None, "") else d_s
    if aufmass > a_s - 1e-6 and a_s > 0:
        warnungen.append("Das Aufmass (%s mm) ist nicht kleiner als beim 3D-Schruppen (%s mm) – es bleibt "
                         "alles stehen." % (_zahl(aufmass), _zahl(a_s)))
        return [], warnungen
    lo, hi = tri.reshape(-1, 3).min(0), tri.reshape(-1, 3).max(0)
    ext = rand_s + d_s + 1
    u = ueber_rand(vor_p)
    bx = _bx(lo, hi, ext, material, u)
    if bx[2] <= bx[0] or bx[3] <= bx[1]:
        warnungen.append("Das 3D-Modell liegt ausserhalb des Materials.")
        return [], warnungen
    zelle = max(0.1, min(1.0, d_s / 24))
    oben, _u, zelle = _raster(tri, bx, zelle)
    maske = np.isfinite(oben)
    if not maske.any():
        warnungen.append("Das 3D-Modell liegt ausserhalb des Materials.")
        return [], warnungen
    umriss = umriss_modell(tri, maske, bx[0], bx[1], zelle, tol)
    bereich = _begrenzen(_bereich(umriss, rand_s + r_s, material[:2], vor_p.get("_kreis"), u), vor_p, [])
    if bereich.is_empty:
        return [], warnungen
    z_unten = float(np.nanmin(oben))
    x_raster = None
    if extra is not None and len(extra):
        x_raster, _xu, _z = _raster(extra, bx, zelle, max_punkte=10 ** 12)
        if np.isfinite(x_raster).any():
            z_unten = min(z_unten, float(np.nanmin(x_raster)))
        x_raster = np.where(np.isfinite(x_raster), x_raster, -1e9).astype(np.float32)
    z_unten = max(z_unten, -float(material[2]))
    if z_unten >= -1e-6:
        warnungen.append("Das Modell liegt ganz über der Materialoberfläche – nichts zu fräsen.")
        return [], warnungen
    ebenen = schrupp_ebenen(oben, maske, zelle, d_s, zust_s, a_s, z_unten)
    schnitte = _Schnitte(tri)

    def zusatz_auf(z):
        if x_raster is None:
            return None
        g = _flaechen_aus(x_raster, z, bx[0], bx[1], zelle, tol)
        return None if g.is_empty else g

    def halt_auf(z):
        S = schnitte(z)
        zs = zusatz_auf(z)
        return S if zs is None else (zs if S.is_empty else S.union(zs))

    innen_s = _versetzt(bereich, -r_s, tol)
    innen = _versetzt(bereich, -r, tol)
    merk_g = {}

    def geraeumt_s(z, sicher: bool = True):
        """Was das Schruppen auf der Ebene z bis z geräumt hat (Mitten + Radius). sicher (Bestand der Eilgänge und
        wo «Böden und Wände» fährt; sonst streiften noch Eilgänge neben den Stegen, Fall 7): das Schruppen hält das Aufmass auch nach unten – auf z steht es über allem, was bis z − Aufmass
        reicht. Nur der Schnitt auf z liess Böden knapp darunter (und einen Boden genau auf z) als geräumt gelten,
        der Eilgang hinunter streifte dann das Aufmass (Zufallstest 3D 07.11.)."""
        k = (round(z, 4), sicher)
        if k not in merk_g:
            h = halt_auf(z)
            if sicher:
                h2 = halt_auf(z - a_s + 0.01)        # ein Boden genau Aufmass tiefer bleibt erlaubt
                h = h2 if h.is_empty else (h if h2.is_empty else h.union(h2))
            frei = bereich if h.is_empty else bereich.difference(h.buffer(a_s + zelle, resolution=8))
            mitte = _versetzt(frei, -r_s, tol) if not frei.is_empty else frei
            merk_g[k] = mitte.buffer(r_s, resolution=16) if not mitte.is_empty else mitte
        return merk_g[k]

    def erlaubt(z):
        """Wo die Fräsermitte auf z fahren darf: der Fräser liegt ganz im Geräumten (+ dem Streifen, den das
        Schruppen neben den Wänden stehen liess)."""
        g = geraeumt_s(z)
        if g.is_empty:
            return g
        return g.buffer(a_s + zelle + 0.1, resolution=8).buffer(-r, resolution=8)

    bestand = _Bestand(bx, zelle, r, material)
    schon = set()                                  # Ebenen des Schruppens, die im Bestand stehen

    def schruppen_bis(k):
        for e in ebenen:
            if stufe_von(e, stufe) <= k and round(e, 4) not in schon:
                schon.add(round(e, 4))
                g = geraeumt_s(e, sicher=True)
                if not g.is_empty:
                    # 0,1 mm Spiel: die flachen Ebenen des Schruppens kommen aus dessen eigenem Raster und liegen
                    # bis einige Hundertstel höher als hier gerechnet (Zufallstest 3D 07.11., Fall 48)
                    bestand.geraeumt(g, e + 0.1)

    weg_von = a_s + 2 * zelle + tol + 0.3

    def eintauchen(b, xy, z):
        z_frei = max(z, bestand.hoehe(xy[0], xy[1], rand=bestand.g))
        b.bis_frei(z_frei)
        b.eintauchen(z, None)

    def hoch_auf_weg(p0, p1):
        """Höchstes Material unter dem Fräser auf dem Weg p0 → p1 (alle halbe Rasterzelle, eine Zelle Rand)."""
        n = max(1, int(math.ceil(math.dist(p0, p1) / max(0.5 * bestand.g, 0.05))))
        return max(bestand.hoehe(p0[0] + (p1[0] - p0[0]) * i / n, p0[1] + (p1[1] - p0[1]) * i / n,
                                 rand=bestand.g) for i in range(n + 1))

    def hinfahren(b, ziel, z):
        """Zum nächsten Zug (Fräsbahnen 25.10.: Leerwege): liegt der Weg im schon Geräumten, auf Tiefe im Vorschub
        hinüber; sonst nur so hoch wie das höchste Material auf dem Weg + Startabstand (nie höher als die
        Freifahrhöhe), hinüber und über dem Ziel eintauchen."""
        x0, y0, z0 = b.pos
        if math.hypot(ziel[0] - x0, ziel[1] - y0) < 1e-6:
            eintauchen(b, ziel, z)
            return
        h = hoch_auf_weg((x0, y0), ziel)
        if abs(z0 - z) < 1e-9 and h <= z + 1e-6:
            b.fraesen(ziel[0], ziel[1])
            return
        ueber = h + b.sz
        if ueber >= b.cz - 1e-6 or z0 > b.cz + 1e-6:
            b.abheben()
            b.anfahren(*ziel)
        else:
            if z0 < ueber:
                b.eilgang_z(ueber)
            b._add(True, ziel[0], ziel[1], max(ueber, min(z0, b.cz)))
        eintauchen(b, ziel, z)

    def wand_umlaeufe(b, z):
        """Senkrechte Wände auf z: Fräsermitte im Abstand r (+ Aufmass) zum genauen Schnitt; nur wo der Schnitt
        0,5 mm tiefer nicht breiter ist (geneigte Flächen, Rundungen am Fuss bleiben fürs Schlichten)."""
        S = schnitte(z)
        if S.is_empty or innen.is_empty:
            return 0
        halt = halt_auf(z)
        bahn = innen.difference(halt.buffer(r + aufmass + 0.01, resolution=32))
        if bahn.is_empty:
            return 0
        erl = erlaubt(z)
        if erl.is_empty:
            return 0
        wand_nah = S.buffer(r + aufmass + 0.05, resolution=16)
        tiefer = schnitte(z - 0.5)
        if not tiefer.is_empty:
            wand_nah = wand_nah.difference(tiefer.buffer(r + aufmass - 0.05, resolution=16))
            if wand_nah.is_empty:
                return 0
        zuege = []                                   # (Anfahrpunkt, Punkte), erst sammeln, dann kurz ordnen
        for t in getattr(bahn, "geoms", [bahn]):
            if t.geom_type != "Polygon" or t.is_empty:
                continue
            for ring, loch in _ringe_von(t):
                if len(ring) < 3:
                    continue
                if (flaeche(ring) > 0) != (not loch):
                    ring = list(reversed(ring))
                linie = LineString(list(ring) + [ring[0]])
                stuecke = linie.intersection(erl.buffer(1e-3)).intersection(wand_nah)
                if stuecke.is_empty:
                    continue
                stuecke = [g for g in getattr(stuecke, "geoms", [stuecke]) if g.geom_type == "LineString"]
                if len(stuecke) > 1:
                    m = linemerge(stuecke)
                    stuecke = [g for g in getattr(m, "geoms", [m])]
                for g in stuecke:
                    if g.length < 0.5:
                        continue
                    pts = list(g.coords)
                    p0 = pts[0]
                    # anfahren von der freien Seite (weg von der Wand), dort eintauchen
                    a = nearest_points(S, Point(p0))[0]
                    vx, vy = p0[0] - a.x, p0[1] - a.y
                    ln = math.hypot(vx, vy)
                    start_p = (p0[0] + vx / ln * weg_von, p0[1] + vy / ln * weg_von) if ln > 1e-6 else p0
                    if not erl.contains(Point(start_p)):
                        start_p = p0
                    zuege.append((start_p, pts))
        folge = kurze_reihenfolge(len(zuege), lambda i, j: math.dist(zuege[i][1][-1], zuege[j][0]),
                                  lambda i: math.dist(b.pos[:2], zuege[i][0]), max_n=150)
        for i in folge:
            start_p, pts = zuege[i]
            hinfahren(b, start_p, z)
            if math.dist(start_p, pts[0]) > 1e-9:
                b.fraesen(pts[0][0], pts[0][1])
            for q in pts[1:]:
                b.fraesen(q[0], q[1])
            bestand.geraeumt(LineString([start_p] + pts).buffer(r, resolution=16), z)
        return len(zuege)

    def boden(b, h):
        """Boden auf der Höhe h genau (+ Aufmass): Runden von innen nach aussen wie die Tasche; die letzte
        Runde läuft den Wänden entlang."""
        z = h + aufmass
        F = schnitte(h - 0.02)
        hoeher = schnitte(h + 0.02)
        if not hoeher.is_empty:
            F = F.difference(hoeher)
        if F.is_empty or F.area < 1.0:
            return 0
        halt = halt_auf(h + 0.01)
        C = innen if halt.is_empty else innen.difference(halt.buffer(r + aufmass + 0.01, resolution=32))
        # nur, wo das Schruppen auf seiner Ebene über diesem Boden hinkam – auf einer Ebene, die mindestens das
        # Aufmass über dem Boden liegt: eine Ebene genau auf Bodenhöhe (Impeller: Nabe −12 bei Zustellung 3) liess
        # über dem Boden das Aufmass stehen und kam darum gar nicht hin; der Boden blieb 0,5 mm zu hoch, und
        # «Fertiges auslassen» hielt ihn für fertig (3D → Fräsbahnen 27.10. 10:33, Punkt 3: Ring auf der Nabe)
        e_s = min([e for e in ebenen if e >= h + a_s - 1e-6] or [h + a_s])
        erl = erlaubt(e_s)
        C = C.intersection(F.buffer(r, resolution=16)).intersection(erl.buffer(1e-3))
        teile = [t for t in getattr(C, "geoms", [C]) if t.geom_type == "Polygon" and t.area > 1e-3]
        if not teile:
            return 0
        folge = kurze_reihenfolge(len(teile), lambda i, j: teile[i].distance(teile[j]),
                                  lambda i: teile[i].distance(Point(b.pos[:2])), max_n=120)
        for k_t in folge:
            teil = teile[k_t]
            stufen = [teil]
            n = 1
            while n < 2000:
                g = teil.buffer(-schritt * n, resolution=16, join_style=1)
                if g.is_empty:
                    break
                stufen.append(g)
                n += 1
            # Runden je Teilstück von innen nach aussen (wie die Tasche, Fräsbahnen fee89f9)
            ringe = []
            for _k, t in _innen_nach_aussen(stufen):
                for ring, loch in _ringe_von(t):
                    if (flaeche(ring) > 0) != (not loch):
                        ring = list(reversed(ring))
                    ringe.append(ring)
            drin = teil.buffer(1e-3)
            erster = True
            for ring in ringe:
                if len(ring) < 3:
                    continue
                k0 = min(range(len(ring)), key=lambda i: math.dist(ring[i], b.pos[:2]))
                ring = list(ring[k0:]) + list(ring[:k0])
                p0 = ring[0]
                if not erster and abs(b.pos[2] - z) < 1e-9 and drin.contains(LineString([b.pos[:2], p0])):
                    b.fraesen(p0[0], p0[1])
                else:
                    hinfahren(b, p0, z)
                erster = False
                for q in ring[1:] + [ring[0]]:
                    b.fraesen(q[0], q[1])
            bestand.geraeumt(teil.buffer(r, resolution=16), z)
        return len(teile)

    flach = sorted({round(h, 4) for h in _flache_hoehen(oben, maske, zelle, d_s)
                    if z_unten - 1e-6 <= h < -1e-6}, reverse=True) if boeden else []
    n_stufen = stufe_von(min(ebenen), stufe) + 1
    aus = []
    n_waende = n_boeden = 0
    # Wände bis hinunter zur Rundung am Fuss (Impeller: zwischen der letzten Ebene des Schruppens über der
    # Rundung und ihrem oberen Rand blieb das Aufmass – das Schlichten fuhr dafür an jeder Schaufel auf und ab):
    # je Boden die tiefste Höhe, auf der die Wände noch senkrecht sind
    fuss_ebenen = []
    if waende:
        for h in sorted({round(x, 4) for x in _flache_hoehen(oben, maske, zelle, d_s)
                         if z_unten - 1e-6 <= x < -1e-6}):
            for t in np.arange(0.5, 10.01, 0.25):
                z = h + float(t)
                if z >= -1e-6:
                    break
                S = schnitte(z)
                if S.is_empty:
                    break
                breiter = schnitte(z - 0.5).difference(S).area / max(S.length, 1e-6)
                if breiter < 0.03:
                    if t > 0.5:
                        fuss_ebenen.append(z)
                    break
    for k in range(n_stufen):
        schruppen_bis(k)
        b = Bahnbauer(w["vorschub"], w["eintauchen"], freifahrhoehe, startabstand, start)
        hier = sorted({round(e, 4) for e in ebenen + fuss_ebenen if stufe_von(e, stufe) == k}, reverse=True)
        if waende and hier:
            # gleicher Schnitt auf mehreren Ebenen (senkrechte Wand): ein Umlauf auf der untersten
            gruppen, alt = [], None
            for z in hier:
                sz = schnitte(z)
                if alt is not None and gruppen and sz.symmetric_difference(alt).area <= 0.02 * max(sz.length, 1.0):
                    gruppen[-1] = z
                else:
                    gruppen.append(z)
                alt = sz
            for z in gruppen:
                n_waende += wand_umlaeufe(b, z)
        for h in flach:
            if stufe_von(h, stufe) == k:
                n_boeden += boden(b, h)
        tp = b.weg(name)
        if tp.moves:
            aus.append((k, tp))
    if not aus:
        warnungen.append("Keine waagrechten Böden und senkrechten Wände gefunden, die das 3D-Schruppen erreicht.")
        return [], warnungen
    return aus, warnungen


def _bearbeiten_ohne_abbruch(f):
    from .relief3d import ohne_nan_abbruch
    return ohne_nan_abbruch(f)


@_bearbeiten_ohne_abbruch
def bearbeiten(p: Dict, w: Dict, auftrag: Dict, seite: int, freifahrhoehe: float, startabstand: float,
               start, name: str, tol: float, vor_w: Optional[Dict], vor_p: Optional[Dict]):
    """Einstieg für engine.rechnen: ([(Stufe, Werkzeugweg)], Warnungen)."""
    from .relief3d import _modelle
    warnungen: List[str] = []
    if vor_w is None or vor_p is None:
        return [], ["«Böden und Wände» braucht ein 3D-Schruppen davor auf derselben Seite – es fräst nur, "
                    "wo das Schruppen hinkam."]
    ru = auftrag.get("rund") or {}
    vor_p = dict(vor_p)
    if ru.get("radius"):
        vor_p["_kreis"] = (float(ru["mitte"][0]), float(ru["mitte"][1]), float(ru["radius"]))
    if (w.get("typ") or "schaft") not in ("schaft", "eckradius"):
        warnungen.append("Für Böden und Wände auf Mass braucht es einen Schaftfräser (flach).")
    fl = auftrag.get("material_flaeche") or [300.0, 200.0]
    dicke = float((auftrag.get("hoehen") or {}).get("materialdicke") or 18.0)
    material = (float(fl[0]), float(fl[1]), dicke)
    d_s = float(vor_w.get("durchmesser") or 6)
    rand_s = float(vor_p.get("rand")) if vor_p.get("rand") not in (None, "") else d_s
    # Hinweise zum Modell meldet schon das 3D-Schruppen
    tri, extra, _geos = _modelle(auftrag, vor_p, seite, material, rand_s + d_s + 2, [])
    if tri is None:
        return [], warnungen + ["Kein 3D-Modell auf dieser Seite – zuerst «Fräsen › 3D-Modell einlesen …»."]
    aus, w2 = rechnen(tri, material, w, p, freifahrhoehe, startabstand, start, name, tol, extra, vor_w, vor_p)
    return aus, warnungen + w2
