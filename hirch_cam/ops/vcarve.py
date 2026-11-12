# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""V-Carve (Rot): Flächen mit dem V-Fräser entlang der Mittellinie
ausfräsen. Die Mittellinien rechnet das CAM-Modul hirch_cam (Voronoi wie FreeCAD CAM
Vcarve), die Fahrwege werden hier sicher gemacht (Eilgänge nur auf
Freifahrhöhe).

Flachbereich (wie üblich): Wo eine Fläche breiter ist, als der V-Fräser
bei der maximalen Tiefe schneidet (2 · Tiefe · tan(Winkel/2)),
- fährt der V-Fräser zusätzlich eine Randbahn auf maximaler Tiefe, damit
  die Flanken bis an die gezeichnete Kontur reichen, und
- der flache Boden wird mit einem Schaftfräser ausgeräumt. Das ist ein
  anderes Werkzeug, darum eine eigene Datei ("…_flach.tap").

Reihenfolge je Buchstabe: zuerst den Boden ausräumen (was der Schaftfräser
nicht erreicht), dann die V-Flanken entlang der Mittellinien, zuletzt die
Randbahn als Schlichtgang – so bleibt die fertige Kante unberührt.

param: max_tiefe, zustellung (0 = in einem Durchgang), aufdicken (mm je
Seite, negativ = dünner).
"""
from __future__ import annotations

import math
from typing import List

from ..model import Toolpath
from .. import cam
from .bahn import Bahnbauer, flaeche, offener_ring, ring_ab, zustellungen
from .tasche import _flaechen, tasche



RILLE = 0.1  # höchste Rille (mm), die der V-Fräser im Flachbereich stehen lässt
RUECKWEG = 15.0  # so weit (mm) fährt der Fräser höchstens über schon gefräste Züge zurück
HUB_MM = 15.0    # ein Hub (abheben, Eilgang, eintauchen) kostet etwa so viel wie 15 mm fräsen


def _abstaende(region, xs, ys):
    """Abstand der Punkte zum Rand der Fläche (Shapely 2 schnell, 1.8 langsam)."""
    try:
        import shapely
        return list(shapely.distance(region.boundary, shapely.points(xs, ys)))
    except (ImportError, AttributeError, TypeError):
        from shapely.geometry import Point
        rand = region.boundary
        return [rand.distance(Point(x, y)) for x, y in zip(xs, ys)]


def _rest_ausraeumen(b, flach, rest, tiefe, bahnabstand, toleranz=0.01):
    """Restflächen im Flachbereich mit dem V-Fräser auf Tiefe ausräumen:
    Versatzbahnen der Flachfläche, beschnitten auf den Rest."""
    from shapely.geometry import LineString

    erlaubt = flach.buffer(1e-3)
    zone = rest.buffer(bahnabstand)
    stuecke = []
    k = 0
    geom = flach
    while not geom.is_empty and k < 5000:
        for p in getattr(geom, "geoms", [geom]):
            for ring in [p.exterior] + list(p.interiors):
                teil = LineString(ring.coords).intersection(zone)
                for g in getattr(teil, "geoms", [teil]):
                    if g.geom_type == "LineString" and g.length > bahnabstand:
                        stuecke.append([tuple(c) for c in g.coords])
        k += 1
        # jede Bahn direkt aus der Ausgangsform (sonst wachsen die Punkte an)
        geom = flach.buffer(-bahnabstand * k, join_style=1).simplify(toleranz / 2)
    while stuecke:
        i = min(range(len(stuecke)), key=lambda j: min(
            math.dist(b.pos[:2], stuecke[j][0]), math.dist(b.pos[:2], stuecke[j][-1])))
        st = stuecke.pop(i)
        if math.dist(b.pos[:2], st[-1]) < math.dist(b.pos[:2], st[0]):
            st = list(reversed(st))
        if abs(b.pos[2] + tiefe) < 1e-9 and erlaubt.contains(LineString([b.pos[:2], st[0]])):
            b.fraesen(*st[0])
        else:
            b.anfahren(*st[0])
            b.eintauchen(-tiefe)
        for q in st[1:]:
            b.fraesen(*q)
    b.abheben()


def _knicke(zug, *grenzen):
    """Zug [(x, y, d)] als (xs, ys, ds); wo der Randabstand eine der Grenzen
    kreuzt (maximale Tiefe erreicht, Rand der flachen Spitze), einen Punkt
    einfügen – sonst würde zwischen den Punkten zu tief interpoliert."""
    grenzen = [g for g in grenzen if g > 0]
    xs, ys, ds = [zug[0][0]], [zug[0][1]], [zug[0][2]]
    for (x0, y0, d0), (x1, y1, d1) in zip(zug, zug[1:]):
        ts = sorted((g - d0) / (d1 - d0) for g in grenzen if (d0 - g) * (d1 - g) < 0)
        for t in ts:
            xs.append(x0 + (x1 - x0) * t)
            ys.append(y0 + (y1 - y0) * t)
            ds.append(d0 + (d1 - d0) * t)
        xs.append(x1)
        ys.append(y1)
        ds.append(d1)
    return xs, ys, ds


def _aeste_kappen(ketten, w, schwelle):
    """Mittellinien als Graph: Seitenäste, die kaum Material wegnehmen, weglassen
    und Ketten an Knoten mit nur noch zwei Enden verbinden.

    Ein Ast (Kette mit einem freien Ende) lohnt sich nur, wenn der Fräser auf ihm
    weiter reicht als schon am Abzweig: Reichweite = Abstand zum Abzweig +
    Schneidradius dort (höchstens w, der Radius auf maximaler Tiefe). Ist der
    Gewinn kleiner als «schwelle», bleibt nur ein unsichtbarer Rest stehen –
    sonst führe der Fräser für jede Zacke im Umriss hoch und wieder hinunter."""
    def schluessel(x, y):
        return (round(x, 4), round(y, 4))

    ketten = [list(zip(*k)) for k in ketten if len(k[0]) >= 2]   # [(x, y, d), …]
    aenderung = True
    while aenderung:
        aenderung = False
        grad = {}
        for k in ketten:
            for p in (k[0], k[-1]):
                grad[schluessel(p[0], p[1])] = grad.get(schluessel(p[0], p[1]), 0) + 1
        bleiben = []
        for k in ketten:
            a_frei = grad[schluessel(k[0][0], k[0][1])] == 1
            e_frei = grad[schluessel(k[-1][0], k[-1][1])] == 1
            if a_frei != e_frei:
                knoten = k[-1] if a_frei else k[0]
                if grad[schluessel(knoten[0], knoten[1])] >= 3:
                    r_j = min(knoten[2], w)
                    gewinn = max(math.dist(p[:2], knoten[:2]) + min(p[2], w) for p in k) - r_j
                    if gewinn < schwelle:
                        aenderung = True
                        continue
            bleiben.append(k)
        ketten = bleiben
        # Ketten an Knoten mit genau zwei Enden zu einer verbinden
        verbunden = True
        while verbunden:
            verbunden = False
            enden = {}
            for i, k in enumerate(ketten):
                for seite, p in ((0, k[0]), (1, k[-1])):
                    enden.setdefault(schluessel(p[0], p[1]), []).append((i, seite))
            for knoten, liste in enden.items():
                if len(liste) != 2 or liste[0][0] == liste[1][0]:
                    continue
                (i, si), (j, sj) = liste
                ki = ketten[i] if si == 1 else list(reversed(ketten[i]))      # endet am Knoten
                kj = ketten[j] if sj == 0 else list(reversed(ketten[j]))      # beginnt am Knoten
                neu = ki + kj[1:]
                ketten = [k for n, k in enumerate(ketten) if n not in (i, j)] + [neu]
                verbunden = aenderung = True
                break
    return [([p[0] for p in k], [p[1] for p in k], [p[2] for p in k]) for k in ketten]


def _von_randbahn_gedeckt(ketten, flach, w, toleranz):
    """Von den Ketten die Stücke weglassen, deren Arbeit schon die Randbahn um den
    Flachbereich macht.

    Die Randbahn fährt auf maximaler Tiefe am Rand des Flachbereichs (Abstand w zur Kontur).
    Ihr Kegel erreicht einen Punkt p mit Randabstand d < w, wenn p höchstens w − d vom
    Flachbereich entfernt ist – dann ist p dort schon so tief wie verlangt. Solche Stücke
    am Anfang und Ende einer Kette fallen weg; Stücke auf dem Boden (d >= w) bleiben, sie räumen die Mitte zwischen den
    Ausräumbahnen. Typisch: die Fächer von der Mitte eines Langlochs zu jedem Punkt der
    diskretisierten Rundung – übrig bleiben kurze Stummel am Knoten, die der Fräser ohne
    Abheben fährt."""
    if flach is None or flach.is_empty:
        return ketten
    aus = []
    for xs, ys, ds in ketten:
        try:
            import shapely
            ab = list(shapely.distance(flach, shapely.points(xs, ys)))
        except (ImportError, AttributeError, TypeError):      # Shapely 1.8
            from shapely.geometry import Point
            ab = [flach.distance(Point(x, y)) for x, y in zip(xs, ys)]
        noetig = [d >= w - 1e-6 or a > w - d + toleranz for a, d in zip(ab, ds)]
        # nur vorne und hinten kürzen: ein gedecktes Stück mittendrin fährt der Fräser einfach
        # durch (sonst gäbe es dort einen Hub mehr)
        i0 = next((i for i, n in enumerate(noetig) if n), None)
        if i0 is None:
            continue
        i1 = len(noetig) - 1 - next(i for i, n in enumerate(reversed(noetig)) if n)
        if i1 - i0 >= 1:
            aus.append((xs[i0:i1 + 1], ys[i0:i1 + 1], ds[i0:i1 + 1]))
    return aus


def _mittellinien_weg(region, tan_h, max_tiefe, werkzeug, freifahrhoehe, startabstand,
                      toleranz, start, name, rt=0.0, ebenen=None, flach=None):
    """V-Carve-Weg aus den Mittellinien. rt: Radius der flachen Spitze
    (Gravierstichel); die Tiefe ist dann (Randabstand − rt) / tan.
    flach: Flachbereich dieser Fläche (bekommt eine Randbahn) oder None."""
    from shapely.geometry import LineString

    from .mittellinie import mittellinien, vereinfachen3d

    # Abtastabstand: fein genug für die Genauigkeit, nicht zu fein für grosse Flächen
    umfang = region.boundary.length
    abstand = min(0.15, max(0.03, umfang / 60000.0))
    erlaubt = region.buffer(-1e-6)
    if cam.verfuegbar():
        # Mittellinien wie FreeCAD (Voronoi aus boost::polygon), Randabstand exakt
        ketten = [_knicke(k, max_tiefe * tan_h + rt, rt) for k in cam.mittellinien(region, toleranz)]
    else:
        ketten = []
        for k in mittellinien(region, abstand):
            xs = [p[0] for p in k]
            ys = [p[1] for p in k]
            ketten.append((xs, ys, _abstaende(region, xs, ys)))

    # Seitenäste ohne Nutzen weg (Gewinn unter 0,03 mm), Ketten verbinden
    ketten = _aeste_kappen(ketten, max_tiefe * tan_h + rt, max(0.03, 3 * toleranz))
    # was die Randbahn um den Flachbereich ohnehin fräst, nicht noch einmal (Fächer an Rundungen)
    ketten = _von_randbahn_gedeckt(ketten, flach, max_tiefe * tan_h + rt, max(0.01, toleranz))
    zuege = []
    for xs, ys, ds in ketten:
        pts = [(x, y, -min(max(0.0, d - rt) / tan_h, max_tiefe)) for x, y, d in zip(xs, ys, ds)]
        pts = vereinfachen3d(pts, max(0.002, toleranz / 2))
        if len(pts) >= 2:
            zuege.append(pts)
    # Zustellung: dieselben Züge Ebene für Ebene, jeweils auf die Ebene begrenzt.
    # Ab der zweiten Ebene nur Züge, die tiefer gehen als die vorige.
    ebenen = sorted(ebenen or [max_tiefe])
    alle = []
    vorige = 0.0
    for e in ebenen:
        for z in zuege:
            if min(p[2] for p in z) < -vorige - 1e-6:
                alle.append((e, [(x, y, max(zz, -e)) for x, y, zz in z]))
        vorige = e
    if not alle:
        alle = [(max_tiefe, z) for z in zuege]

    b = Bahnbauer(werkzeug["vorschub"], werkzeug["eintauchen"], freifahrhoehe, startabstand,
                  tuple(start))
    # Zusammenhängende Flächen (Buchstaben) ganz fertig fräsen, bevor es zur
    # nächsten geht – sonst bliebe z. B. ein Ast des «T» bis ganz am Schluss liegen
    from shapely.geometry import Point
    teile = list(getattr(region, "geoms", [region]))

    def teil_von(z):
        p = Point(z[len(z) // 2][:2])
        return min(range(len(teile)), key=lambda i: teile[i].distance(p))
    gruppen = {}
    for e, z in alle:
        gruppen.setdefault((e, teil_von(z)), []).append(z)
    # erst Ebene für Ebene, innerhalb einer Ebene Fläche für Fläche
    offen_gruppen = [gruppen[k] for k in sorted(gruppen)]
    zuege = []
    # Schon gefräste Züge als Graph (je Fläche und Ebene): dorthin fährt der Fräser auf ihnen
    # zurück (G1 auf derselben Tiefe, nimmt nichts Neues weg) statt abzuheben – wie FreeCAD
    # «generateVirtualBackTrackEdges», aber über beliebig viele gefräste Stücke, bis RUECKWEG mm
    nachbarn = {}
    hoehe = {}

    def knoten(x, y):
        return (round(x, 4), round(y, 4))

    def eintragen(punkte):
        for (xa, ya, za), (xb, yb, zb) in zip(punkte, punkte[1:]):
            ka, kb = knoten(xa, ya), knoten(xb, yb)
            if ka == kb:
                continue
            l = math.hypot(xb - xa, yb - ya)
            nachbarn.setdefault(ka, {})[kb] = min(l, nachbarn.get(ka, {}).get(kb, l))
            nachbarn.setdefault(kb, {})[ka] = min(l, nachbarn.get(kb, {}).get(ka, l))
            hoehe.setdefault(ka, za)
            hoehe.setdefault(kb, zb)
            hoehe[ka] = min(hoehe[ka], za)
            hoehe[kb] = min(hoehe[kb], zb)

    def rueckwege(von):
        """kürzeste Wege auf gefrästen Zügen ab «von» (Dijkstra, bis RUECKWEG mm)"""
        import heapq
        dist, vor = {von: 0.0}, {}
        heap = [(0.0, von)]
        while heap:
            d, k = heapq.heappop(heap)
            if d > dist.get(k, 1e18) or d > RUECKWEG:
                continue
            for n, l in nachbarn.get(k, {}).items():
                nd = d + l
                if nd < dist.get(n, 1e18) and nd <= RUECKWEG:
                    dist[n], vor[n] = nd, k
                    heapq.heappush(heap, (nd, n))
        return dist, vor

    while offen_gruppen or zuege:
        if not zuege:
            # nächste Fläche: die mit dem nächstgelegenen Zug-Ende
            zuege = offen_gruppen.pop(0)
            nachbarn.clear()
            hoehe.clear()
        # nächster Zug, wahlweise rückwärts: erreichbar über schon gefräste Züge zählt die
        # Weglänge, sonst die Luftlinie plus die Kosten eines Hubs
        dist, vor = ({}, {})
        # (auch an der Spitze einer Ecke, wo der Zug auf Tiefe 0 endet)
        if b.pos[2] <= 1e-9 and knoten(*b.pos[:2]) in nachbarn:
            dist, vor = rueckwege(knoten(*b.pos[:2]))
        best = None
        for i, z in enumerate(zuege):
            for rueck in (False, True):
                p0 = z[-1] if rueck else z[0]
                k0 = knoten(p0[0], p0[1])
                d = dist[k0] if k0 in dist else math.dist(b.pos[:2], p0[:2]) + HUB_MM
                if best is None or d < best[0]:
                    best = (d, i, rueck)
        _d, i, rueck = best
        z = zuege.pop(i)
        if rueck:
            z = list(reversed(z))
        x0, y0, z0 = z[0]
        k0 = knoten(x0, y0)
        anschluss = (b.pos[2] <= 1e-9 and math.dist(b.pos[:2], (x0, y0)) < 1e-6)
        if not anschluss and k0 in dist and dist[k0] > 0:
            # Rückweg über gefräste Züge (jeder Punkt auf der Tiefe, auf der er gefräst wurde)
            weg = [k0]
            while weg[-1] in vor:
                weg.append(vor[weg[-1]])
            for k in reversed(weg[:-1]):
                b.fraesen(k[0], k[1], hoehe[k])
            b.fraesen(x0, y0, z0)
            anschluss = True
        if (not anschluss and b.pos[2] < 0
                and math.dist(b.pos[:2], (x0, y0)) < 2 * (max_tiefe * tan_h + rt)
                and erlaubt.contains(LineString([b.pos[:2], (x0, y0)]))):
            # kurzer Übergang im Inneren: nur wenn der V-Fräser dabei nirgends
            # über den Rand hinaus schneidet (Tiefe <= Randabstand / tan)
            n = 12
            xs = [b.pos[0] + (x0 - b.pos[0]) * t / n for t in range(n + 1)]
            ys = [b.pos[1] + (y0 - b.pos[1]) * t / n for t in range(n + 1)]
            zs = [b.pos[2] + (z0 - b.pos[2]) * t / n for t in range(n + 1)]
            ds = _abstaende(region, xs, ys)
            if all(-zz <= max(0.0, d - rt) / tan_h + 1e-3 for zz, d in zip(zs, ds)):
                b.fraesen(x0, y0, z0)
                anschluss = True
        if not anschluss:
            b.anfahren(x0, y0)
            b.eintauchen(z0)
        for x, y, zz in z[1:]:
            b.fraesen(x, y, zz)
        eintragen(z)
    tp = b.weg(name)
    ende = b.pos
    return tp, ende


def stuecke_ordnen(moves, start, cz: float, radius: float, teil_von=None, mehrere_ebenen: bool = True):
    """Stücke (Vorschub zwischen zwei Eilgängen quer) mit kurzem Leerweg ordnen. Ein Stück darf nur vor ein früheres,
    wenn sich ihre gefrästen Bereiche (Bahn + radius) nicht berühren – dort bleibt die Reihenfolge wie gerechnet.
    Nächster Nachbar unter dieser Bedingung, danach Stücke einzeln umsetzen, solange der Weg kürzer wird. Übergänge
    neu: hinauf auf die Freifahrhöhe, quer, hinunter wie vorher; wo zwei Stücke wie vorher aufeinander folgen, der
    alte Übergang (kurzer Hub)."""
    from shapely import STRtree
    from shapely.geometry import LineString, Point
    from ..model import Move
    EPS_ = 1e-6
    # zerlegen: Übergänge (Eilgänge mit Weg quer) und Stücke
    stuecke, uebergaenge, teil = [], [], []
    pos = tuple(map(float, start))
    lauf, stueck = [], []
    lauf_ab = pos                                   # wo der Eilgang-Lauf begann
    for i_m, m in enumerate(moves):
        if m.rapid:
            if not lauf:
                lauf_ab = pos
            lauf.append(m)
        else:
            quer = bool(lauf) and any(math.hypot(q.x - lauf_ab[0], q.y - lauf_ab[1]) > EPS_ for q in lauf)
            if quer or not stuecke and not stueck:
                teil.append(teil_von[i_m] if teil_von is not None and i_m < len(teil_von) else -1)
                if stueck:
                    stuecke.append(stueck)
                uebergaenge.append(lauf if lauf else [Move(True, pos[0], pos[1], pos[2])])
                stueck = []
            else:
                stueck.extend(lauf)
            lauf = []
            stueck.append(m)
        pos = (m.x, m.y, m.z)
    if stueck:
        stuecke.append(stueck)
    ende = lauf                                     # Eilgänge nach dem letzten Stück (abheben)
    n = len(stuecke)
    if n < 3 or len(uebergaenge) != n:
        return moves
    # Anfahrpunkt (Ende des Übergangs davor) und Endpunkt je Stück
    an = [(u[-1].x, u[-1].y, u[-1].z) for u in uebergaenge]
    ab = [(s[-1].x, s[-1].y, s[-1].z) for s in stuecke]

    def flaeche_von(k):
        pts = [an[k][:2]] + [(q.x, q.y) for q in stuecke[k]]
        pts = [p for i, p in enumerate(pts) if i == 0 or math.dist(p, pts[i - 1]) > 1e-9]
        g = LineString(pts) if len(pts) > 1 else Point(pts[0])
        return g.buffer(radius, quad_segs=4)
    geo = [flaeche_von(k) for k in range(n)]
    baum = STRtree(geo)
    vor = [set() for _ in range(n)]                 # vor[j]: Stücke, die vor j kommen müssen
    for j in range(n):
        for i in baum.query(geo[j], predicate="intersects"):
            i = int(i)
            if i < j and (mehrere_ebenen or teil[i] != teil[j] or teil[i] < 0):
                vor[j].add(i)
    nach = [set() for _ in range(n)]
    for j in range(n):
        for i in vor[j]:
            nach[i].add(j)

    def leer(a, k):
        return math.dist(a[:2], an[k][:2])
    # nächster Nachbar unter der Bedingung
    fertig, folge, ort = set(), [], tuple(map(float, start))
    while len(folge) < n:
        frei = [k for k in range(n) if k not in fertig and vor[k] <= fertig]
        k = min(frei, key=lambda k: (leer(ort, k), k))
        folge.append(k)
        fertig.add(k)
        ort = ab[k]

    def weg(f):
        w_, o = 0.0, tuple(map(float, start))
        for k in f:
            w_ += leer(o, k)
            o = ab[k]
        return w_
    st_ = tuple(map(float, start))

    def d(a, b):                                    # Leerweg von Stück a (None: Start) zu Stück b (None: Ende)
        if b is None:
            return 0.0
        return leer(st_ if a is None else ab[a], b)
    # Ketten von 1 bis 5 Stücken umsetzen (Or-opt), nur wo die Bedingung hält – Änderung des Wegs direkt gerechnet
    for _runde in range(30):
        besser = False
        for L in (1, 2, 3, 4, 5):
            p = 0
            while p + L <= n:
                kette = folge[p:p + L]
                vor_k = folge[p - 1] if p else None
                nach_k = folge[p + L] if p + L < n else None
                weg_raus = d(vor_k, kette[0]) + d(kette[-1], nach_k) - d(vor_k, nach_k)
                rest = folge[:p] + folge[p + L:]
                lage = {k: i_ for i_, k in enumerate(rest)}
                in_kette = set(kette)
                # wo die Kette hin darf: nach allen ihren Vorgängern, vor allen ihren Nachfolgern
                frueh = max([lage[v] + 1 for k in kette for v in vor[k] if v not in in_kette] + [0])
                spaet = min([lage[w] for k in kette for w in nach[k] if w not in in_kette] + [len(rest)])
                best = None
                for q in range(frueh, spaet + 1):
                    if q == p:
                        continue
                    a_ = rest[q - 1] if q else None
                    b_ = rest[q] if q < len(rest) else None
                    rein = d(a_, kette[0]) + d(kette[-1], b_) - d(a_, b_)
                    g = weg_raus - rein
                    if g > 1e-6 and (best is None or g > best[0]):
                        best = (g, q)
                if best:
                    q = best[1]
                    folge = rest[:q] + kette + rest[q:]
                    besser = True
                p += 1
        if not besser:
            break
    if weg(folge) >= weg(list(range(n))) - 1e-6:
        return moves
    # zusammensetzen
    aus = []
    o = tuple(map(float, start))
    letzt = None
    for k in folge:
        if letzt is not None and k == letzt + 1:
            aus.extend(uebergaenge[k])              # wie vorher
        else:
            x, y, z = an[k]
            if math.hypot(x - o[0], y - o[1]) > EPS_:
                if o[2] < cz - EPS_:
                    aus.append(Move(True, o[0], o[1], cz))
                aus.append(Move(True, x, y, max(cz, min(o[2], cz)) if o[2] >= cz else cz))
            if z < aus[-1].z - EPS_ if aus else z < o[2] - EPS_:
                aus.append(Move(True, x, y, z))
        aus.extend(stuecke[k])
        o = ab[k]
        letzt = k
    if o[2] < cz - EPS_:
        aus.append(Move(True, o[0], o[1], cz))
    elif ende:
        aus.extend(ende)
    return aus


def vcarve(linien, werkzeug, param, freifahrhoehe=3.0, startabstand=0.5, toleranz=0.01,
           start=(0.0, 0.0, 20.0), name="V-Carve", flach_werkzeug=None):
    """flach_werkzeug: Schaftfräser für den Flachbereich (dict mit durchmesser,
    vorschub, eintauchen) oder None."""
    warnungen: List[str] = []
    ringe = [offener_ring(p) for p, g in linien if g and len(offener_ring(p)) >= 3]
    offen = sum(1 for p, g in linien if not g)
    if offen:
        warnungen.append(f"{offen} offene Linien ausgelassen (braucht geschlossene Formen).")
    region = _flaechen(ringe)
    leer = Toolpath(name=name, moves=[])
    if region is None or region.is_empty:
        return leer, warnungen, None
    # Winzige Zacken im Umriss (gerundete Koordinaten, umgewandelte Schriften)
    # glätten: jede Zacke gäbe in der Mittellinie einen Seitenast bis an den Rand,
    # und der Fräser führe jedes Mal hoch und wieder hinunter.
    glatt = region.simplify(max(toleranz, 0.01), preserve_topology=True)
    if not glatt.is_empty and abs(glatt.area - region.area) < 0.01 * region.area:
        region = glatt
    # Aufdicken: jede Form ringsum um «aufdicken» mm grösser (negativ: dünner),
    # Ecken bleiben spitz (Gehrung, begrenzt)
    dick = float(param.get("aufdicken") or 0.0)
    if abs(dick) > 1e-6:
        neu = region.buffer(dick, join_style=2, mitre_limit=3.0)
        if neu.is_empty:
            warnungen.append("Aufdicken %s mm: die Formen verschwinden ganz – ausgelassen."
                             % ("%g" % dick).replace(".", ","))
            return leer, warnungen, None
        region = neu

    winkel = float(werkzeug.get("v_winkel", 60.0))
    durchm = float(werkzeug.get("durchmesser", 12.0))
    tan_h = math.tan(math.radians(winkel / 2))
    max_tiefe = float(param.get("max_tiefe", 5.0))
    # Tiefer als die Schneide reicht, geht nicht
    rt = max(0.0, float(werkzeug.get("spitze") or 0.0)) / 2   # flache Spitze (Gravierstichel)
    max_tiefe = min(max_tiefe, max(0.01, durchm / 2 - rt) / tan_h)

    extra = None
    w = max_tiefe * tan_h + rt  # Radius, den der V-Fräser auf maximaler Tiefe schneidet
    flach = region.buffer(-w, join_style=1)
    fd = float(flach_werkzeug["durchmesser"]) if flach_werkzeug else 0.0

    # Zustellung: gleich grosse Ebenen bis zur maximalen Tiefe (positiv)
    zust = param.get("zustellung") or 0.0
    if getattr(zust, "modus", ""):                     # Zustellung mit Art (letzte kleiner, Liste, eins)
        ebenen = [-z for z in zustellungen(max_tiefe, zust)]
    else:
        zust = float(zust)
        ebenen = [-z for z in zustellungen(max_tiefe, zust if 0 < zust < max_tiefe else max_tiefe)]

    # Fläche für Fläche (Buchstabe für Buchstabe) ganz fertig – dann zur
    # nächstgelegenen Fläche. Je Fläche: 1) Boden ausräumen, 2) V-Flanken
    # entlang der Mittellinien, 3) Randbahn zuletzt als Schlichtgang.
    from shapely.geometry import Point
    teile = list(getattr(region, "geoms", [region]))
    tp = Toolpath(name=name, moves=[])
    teil_von: List[int] = []           # je Bewegung: 0 Boden, 1 Flanken, 2 Randbahn (Reihenfolge der Stücke)
    pos = tuple(start)
    while teile:
        k = min(range(len(teile)), key=lambda i: teile[i].distance(Point(pos[:2])))
        teil = teile.pop(k)
        flach_t = teil.buffer(-w, join_style=1)
        # 1) Was der Schaftfräser nicht erreicht (zu schmal, Ecken) oder ohne
        #    Schaftfräser der ganze Boden: mit dem V-Fräser in engen Bahnen,
        #    Rillen <= RILLE mm, Ebene für Ebene
        if not flach_t.is_empty:
            if fd > 0:
                rest = flach_t.difference(flach_t.buffer(-fd / 2, join_style=1).buffer(fd / 2))
            else:
                rest = flach_t
            if not rest.is_empty and rest.area > 1e-3:
                b = Bahnbauer(werkzeug["vorschub"], werkzeug["eintauchen"], freifahrhoehe,
                              startabstand, pos)
                for e in ebenen:
                    _rest_ausraeumen(b, flach_t, rest, e, 2 * RILLE * tan_h + 2 * rt, toleranz)
                tp.moves.extend(b.weg().moves)
                teil_von += [0] * (len(tp.moves) - len(teil_von))
                pos = b.pos
        # 2) V-Flanken entlang der Mittellinien
        tp_t, ende = _mittellinien_weg(teil, tan_h, max_tiefe, werkzeug, freifahrhoehe,
                                       startabstand, toleranz, pos, name, rt, ebenen, flach_t)
        tp.moves.extend(tp_t.moves)
        teil_von += [1] * (len(tp.moves) - len(teil_von))
        pos = ende
        if flach_t.is_empty:
            continue
        # 3) Randbahn auf maximaler Tiefe zuletzt (Gleichlauf): Schlichtgang an der Kante
        b = Bahnbauer(werkzeug["vorschub"], werkzeug["eintauchen"], freifahrhoehe,
                      startabstand, pos)
        for p in getattr(flach_t, "geoms", [flach_t]):
            for ring, loch in [(offener_ring(list(p.exterior.coords)), False)] + [
                    (offener_ring(list(h.coords)), True) for h in p.interiors]:
                if (flaeche(ring) > 0) != (not loch):
                    ring = list(reversed(ring))
                ring = ring_ab(ring, b.pos[:2])
                b.anfahren(*ring[0])
                for e in ebenen:
                    b.eintauchen(-e)
                    for q in ring[1:] + [ring[0]]:
                        b.fraesen(*q)
                b.abheben()
        tp.moves.extend(b.weg().moves)
        teil_von += [2] * (len(tp.moves) - len(teil_von))
        pos = b.pos
    # Beispiel 10.11. (Peter: «Ganz ordnen»): die Stücke aller Flächen in kurzer Reihenfolge – Stücke, deren
    # gefräste Bereiche sich berühren, behalten ihre Reihenfolge (Boden, Flanken, Randbahn, Ebene für Ebene)
    tp.moves = stuecke_ordnen(tp.moves, tuple(start), freifahrhoehe, w + 0.05, teil_von, len(ebenen) > 1)
    if not flach.is_empty:
        # 2) Flacher Boden mit Schaftfräser: eigene Datei
        if fd > 0:
            ringe_flach = []
            for p in getattr(flach, "geoms", [flach]):
                ringe_flach.append((list(p.exterior.coords), True))
                ringe_flach.extend((list(h.coords), True) for h in p.interiors)
            tp2, w2 = tasche(ringe_flach, fd, max_tiefe, min(max_tiefe, fd),
                             flach_werkzeug["vorschub"], flach_werkzeug["eintauchen"],
                             bahnabstand_prozent=40,
                             freifahrhoehe=freifahrhoehe, startabstand=startabstand,
                             toleranz=toleranz, name="V-Carve Flachbereich")
            if tp2.moves:
                extra = {"weg": tp2, "durchmesser": fd}
            else:
                warnungen.append(f"Flachbereich zu schmal für den Schaftfräser Ø {fd:g} mm – mit dem V-Fräser ausgeräumt.")
        elif not flach.buffer(-1.0).is_empty and flach.area > 4.0:
            # Hinweis nur, wenn sich ein Schaftfräser lohnt: Boden breiter als 2 mm
            # und eine nennenswerte Fläche (ein 0,6 mm breiter Boden ist mit dem
            # V-Fräser schnell gemacht)
            warnungen.append(
                "Breiter Boden auf %s mm Tiefe (%.0f mm²) – mit dem V-Fräser in engen Bahnen ausgeräumt "
                "(langsam). Schneller: Schaftfräser für den Flachbereich angeben."
                % (("%.2f" % max_tiefe).replace(".", ","), flach.area))
    return tp, warnungen, extra
