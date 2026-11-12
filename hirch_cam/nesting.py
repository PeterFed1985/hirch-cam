# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Vervielfältigen und Verschachteln (Nesting).

Teile (oberste Objekte bzw. Gruppen der Zeichnung) werden in der gewünschten Anzahl
möglichst eng auf die Platte (= Materialgrösse, erste Seite der Zeichnung) gelegt.
Was nicht passt, kommt auf weitere Platten: weitere Seiten (Pages) der Zeichnung,
bei zweiseitigen Aufträgen je Platte zwei Seiten (Oberseite, Unterseite).

Verfahren: Raster. Die Platte ist ein Gitter (Zellgrösse nach Plattengrösse und Anzahl
Teilformen); jedes Teil in jeder erlaubten Drehung ist eine Maske der Zellen, die es mit
halbem Abstand ringsum berührt (immer grosszügig gerundet, also nie zu eng). Wo ein
Teil hinpasst, zeigt die Korrelation der belegten Zellen mit der Maske (FFT); nach jedem
gelegten Teil wird sie nur um das Teil herum nachgeführt. Gelegt wird links unten
zuerst (bei hochkanten Platten unten links), die grossen Teile zuerst.

Koordinaten: mm, Y nach oben, Nullpunkt unten links der ersten Seite (wie svg_lesen).
"""
from __future__ import annotations

import json
import math
import sys
from typing import Dict, List, Optional, Sequence

ZELLEN_MAX = 400_000         # höchstens so viele Zellen je Platte
KARTEN_MAX = 40e6            # Zellen aller Masken-Karten zusammen (Speicher)
ZELLE_MIN = 0.5              # mm
SEITEN_ABSTAND = 20.0        # mm zwischen neuen Seiten
PLATTEN_MAX = 40
GEWICHT = 0.2                # Gewicht der zweiten Richtung beim Legen


# ----------------------------------------------------------------- Geometrie
def _affin(a: float, dx: float, dy: float):
    """3×3: erst um den Nullpunkt drehen (Grad), dann verschieben."""
    import numpy as np
    r = math.radians(a)
    c, s = math.cos(r), math.sin(r)
    return np.array([[c, -s, dx], [s, c, dy], [0.0, 0.0, 1.0]])


def _verschiebung(dx, dy):
    return _affin(0.0, dx, dy)


def _spiegel(achse: str, w: float, h: float):
    """Seite 1 ↔ Seite 2 (lokale Koordinaten der Seite): um die X-Achse (oben/unten)
    oder die Y-Achse (links/rechts) gewendet."""
    import numpy as np
    if achse == "y":
        return np.array([[-1.0, 0, w], [0, 1.0, 0], [0, 0, 1.0]])
    return np.array([[1.0, 0, 0], [0, -1.0, h], [0, 0, 1.0]])


def _shapely_affin(g, m):
    from shapely import affinity
    return affinity.affine_transform(g, [m[0][0], m[0][1], m[1][0], m[1][1], m[0][2], m[1][2]])


def _zellen_von(g, c: float, x0: float, y0: float, nx: int, ny: int):
    """Zellen (ny × nx, Ursprung x0/y0, Grösse c), die g berühren könnten:
    Zellmitte in g, vergrössert um die halbe Zelldiagonale."""
    import numpy as np
    import shapely
    if g.is_empty or nx <= 0 or ny <= 0:
        return np.zeros((max(ny, 0), max(nx, 0)), dtype=bool)
    gg = g.buffer(c * 0.7072, quad_segs=4)
    shapely.prepare(gg)
    xs = x0 + (np.arange(nx) + 0.5) * c
    ys = y0 + (np.arange(ny) + 0.5) * c
    X, Y = np.meshgrid(xs, ys)
    return shapely.contains_xy(gg, X, Y)


class Maske:
    """Ein Teil in einer Drehung, gerastert."""

    def __init__(self, geom, winkel: float, c: float, halb: float):
        from shapely import affinity
        g = affinity.rotate(geom, winkel, origin=(0, 0))
        gb = g.buffer(halb, quad_segs=4) if halb > 0 else g
        x0, y0, x1, y1 = gb.bounds
        self.winkel = winkel
        self.x0, self.y0 = x0, y0
        self.nx = max(1, int(math.ceil((x1 - x0) / c - 1e-9)))
        self.ny = max(1, int(math.ceil((y1 - y0) / c - 1e-9)))
        self.zellen = _zellen_von(gb, c, x0, y0, self.nx, self.ny)
        # Zellen am Rand der Maske könnten durch die Vergrösserung herausfallen
        self.zellen = self.zellen.astype(bool)
        self.gekehrt = self.zellen[::-1, ::-1].astype("float32")

    def gleich(self, o: "Maske") -> bool:
        return self.zellen.shape == o.zellen.shape and bool((self.zellen == o.zellen).all())


def _korrelation(belegt, maske: Maske):
    """Für jede Lage (iy, ix) der Maske: Anzahl belegter Zellen darunter."""
    import numpy as np
    from scipy.signal import fftconvolve
    ny, nx = belegt.shape
    if maske.ny > ny or maske.nx > nx:
        return np.ones((0, 0), dtype="float32")
    return fftconvolve(belegt.astype("float32"), maske.gekehrt, mode="valid").astype("float32")


def _winkel_liste(drehen: str, geom) -> List[float]:
    if drehen == "90":
        return [0.0, 90.0, 180.0, 270.0]
    if drehen == "frei":
        w = {0.0, 90.0, 180.0, 270.0, 45.0, 135.0, 225.0, 315.0}
        try:
            mrr = geom.minimum_rotated_rectangle
            pts = list(mrr.exterior.coords)
            beste = None
            for a, b in zip(pts, pts[1:]):
                L = math.dist(a, b)
                if beste is None or L > beste[0]:
                    beste = (L, math.degrees(math.atan2(b[1] - a[1], b[0] - a[0])))
            if beste:
                for k in range(4):
                    w.add(round((-beste[1] + 90 * k) % 360.0, 3))
        except Exception:
            pass
        return sorted(w)
    return [0.0]


def anordnen(formen: Sequence[Dict], breite: float, hoehe: float, *, abstand: float = 8.0,
             rand: float = 10.0, drehen: str = "90", in_loechern: bool = False,
             hindernisse=None, platten_max: int = PLATTEN_MAX, art: str = "eng",
             richtung: str = "auto", ecke: str = "ul", abstand_y: Optional[float] = None) -> Dict:
    """formen: [{"material": Geometrie mit Löchern, "voll": ohne Löcher, "anzahl": n,
    "kopie_ab": erste Kopiennummer}] in Platten-Koordinaten (mm). hindernisse(k) -> [Geometrie]
    je Platte k (0 = erste).
    art: "eng" (verschachteln) oder "raster" (Reihen und Spalten im Abstand abstand / abstand_y);
    richtung: "x" zuerst nach X (Reihen), "y" zuerst nach Y (Spalten), "auto" nach der längeren Seite;
    ecke: Startecke "ul", "ol", "ur", "or" (unten/oben, links/rechts).
    Liefert {"lagen": [{"form", "kopie", "platte", "winkel", "dx", "dy"}], "zelle",
    "nicht": Anzahl nicht gelegter Teile, "platten": benutzte Platten}.
    Eine Lage bewegt einen Punkt p der Form nach drehen(p, winkel) + (dx, dy)."""
    import numpy as np
    from scipy.signal import fftconvolve
    ax = max(0.0, float(abstand))
    ay = max(0.0, float(abstand if abstand_y is None else abstand_y))
    raster = art == "raster"
    halb = (min(ax, ay) if raster else ax) / 2
    typen = []
    for i, f in enumerate(formen):
        g = f["material"] if in_loechern else f["voll"]
        if g is None or g.is_empty or int(f.get("anzahl", 1)) <= 0:
            continue
        typen.append({"i": i, "g": g, "voll": f["voll"], "flaeche": f["voll"].area,
                      "rest": int(f.get("anzahl", 1)), "kopie": int(f.get("kopie_ab", 0)),
                      "winkel": _winkel_liste(drehen, f["voll"])})
    typen.sort(key=lambda t: -t["flaeche"])
    n_dreh = sum(len(t["winkel"]) for t in typen) or 1
    budget = min(ZELLEN_MAX, KARTEN_MAX / n_dreh)
    c = max(ZELLE_MIN, math.sqrt(max(1.0, breite * hoehe) / max(1000.0, budget)))
    nx, ny = int(breite // c), int(hoehe // c)
    if richtung not in ("x", "y"):
        richtung = "y" if breite >= hoehe else "x"
    links = ecke in ("ul", "ol")
    unten = ecke in ("ul", "ur")

    for t in typen:                 # Masken je Drehung (gleiche weglassen: Symmetrie)
        ms: List[Maske] = []
        for w in t["winkel"]:
            m = Maske(t["g"], w, c, halb)
            if not any(m.gleich(o) for o in ms):
                from shapely import affinity
                vb = affinity.rotate(t["voll"], w, origin=(0, 0)).bounds
                m.groesse = (vb[2] - vb[0], vb[3] - vb[1])
                ms.append(m)
        t["masken"] = ms

    # Plattenrand: Zellen, die näher als (Rand − halber Abstand) am Rand liegen
    rb = float(rand) - halb
    jx = np.arange(nx)
    iy = np.arange(ny)
    rand_x = (jx * c < rb - 1e-9) | ((jx + 1) * c > breite - rb + 1e-9)
    rand_y = (iy * c < rb - 1e-9) | ((iy + 1) * c > hoehe - rb + 1e-9)

    def wertung(frei, m):
        """Lage, die am nächsten bei der Startecke liegt (Füllrichtung stärker gewichtet)."""
        yy, xx = np.nonzero(frei)
        if not yy.size:
            return None
        dx = xx if links else (frei.shape[1] - 1 - xx)
        dy = yy if unten else (frei.shape[0] - 1 - yy)
        if richtung == "y":
            w_ = (dx + m.nx) + GEWICHT * (dy + m.ny)
        else:
            w_ = (dy + m.ny) + GEWICHT * (dx + m.nx)
        j = int(np.argmin(w_))
        return float(w_[j]), int(xx[j]), int(yy[j])

    lagen: List[Dict] = []
    k = 0
    while any(t["rest"] > 0 for t in typen) and k < platten_max:
        belegt = np.zeros((ny, nx), dtype=bool)
        if rb > 0:
            belegt[:, rand_x] = True
            belegt[rand_y, :] = True
        for h in (hindernisse(k) if hindernisse else []) or []:
            gh = h.buffer(halb, quad_segs=4) if halb > 0 else h
            belegt |= _zellen_von(gh, c, 0.0, 0.0, nx, ny)
        karten = {}
        for ti, t in enumerate(typen):
            if t["rest"] > 0:
                for mi, m in enumerate(t["masken"]):
                    karten[(ti, mi)] = _korrelation(belegt, m)

        def legen(t, mi, x, y):
            m = t["masken"][mi]
            belegt[y:y + m.ny, x:x + m.nx] |= m.zellen
            lagen.append({"form": t["i"], "kopie": t["kopie"], "platte": k, "winkel": m.winkel,
                          "dx": x * c - m.x0, "dy": y * c - m.y0})
            t["kopie"] += 1
            t["rest"] -= 1
            for (tj, mj), o in karten.items():      # Karten um das neue Teil herum nachführen
                if typen[tj]["rest"] <= 0 or o.size == 0:
                    continue
                mm = typen[tj]["masken"][mj]
                y0, y1 = max(0, y - mm.ny + 1), min(o.shape[0], y + m.ny)
                x0, x1 = max(0, x - mm.nx + 1), min(o.shape[1], x + m.nx)
                if y0 >= y1 or x0 >= x1:
                    continue
                teil = belegt[y0:y1 + mm.ny - 1, x0:x1 + mm.nx - 1].astype("float32")
                o[y0:y1, x0:x1] = fftconvolve(teil, mm.gekehrt, mode="valid")

        gelegt = 0
        for ti, t in enumerate(typen):
            while t["rest"] > 0:
                beste = None
                for mi, m in enumerate(t["masken"]):
                    o = karten.get((ti, mi))
                    if o is None or o.size == 0:
                        continue
                    if raster:
                        # die Drehung, die im Raster am meisten Teile fasst
                        r = wertung(o < 0.5, m)
                        if r is None:
                            continue
                        sx = m.groesse[0] + ax
                        sy = m.groesse[1] + ay
                        n_r = int((breite - 2 * rand + ax) // sx) * int((hoehe - 2 * rand + ay) // sy)
                        wert = (-n_r, r[0])
                        if beste is None or wert < beste[0]:
                            beste = (wert, mi, r[1], r[2])
                    else:
                        r = wertung(o < 0.5, m)
                        if r is None:
                            continue
                        if beste is None or r[0] < beste[0]:
                            beste = (r[0], mi, r[1], r[2])
                if beste is None:
                    break                       # passt auf dieser Platte nicht mehr
                _w, mi, x, y = beste
                m = t["masken"][mi]
                if not raster:
                    legen(t, mi, x, y)
                    gelegt += 1
                    continue
                # Raster ab der ersten Lage: Schritte = Grösse + Abstand, weg von der Startecke
                o = karten[(ti, mi)]
                sx = m.groesse[0] + ax
                sy = m.groesse[1] + ay
                fx = 1 if links else -1
                fy = 1 if unten else -1
                spalten = [x + fx * int(math.ceil(i * sx / c - 1e-9)) for i in range(0, nx + 1)]
                spalten = [v for v in spalten if 0 <= v < o.shape[1]]
                zeilen = [y + fy * int(math.ceil(j * sy / c - 1e-9)) for j in range(0, ny + 1)]
                zeilen = [v for v in zeilen if 0 <= v < o.shape[0]]
                folge = ([(xx, yy) for yy in zeilen for xx in spalten] if richtung == "x"
                         else [(xx, yy) for xx in spalten for yy in zeilen])
                for xx, yy in folge:
                    if t["rest"] <= 0:
                        break
                    if o[yy, xx] < 0.5:
                        legen(t, mi, xx, yy)
                        gelegt += 1
        if gelegt == 0 and k > 0:
            break                               # leere Platte und trotzdem nichts: zu gross
        k += 1
    return {"lagen": lagen, "zelle": c, "nicht": sum(t["rest"] for t in typen),
            "platten": (max(lg["platte"] for lg in lagen) + 1) if lagen else 0}


def zentrieren(lagen: List[Dict], formen: Sequence[Dict], breite: float, hoehe: float, rand: float,
               abstand: float, hindernisse=None) -> None:
    """Die Teile jeder Platte gemeinsam in die Mitte der nutzbaren Fläche schieben – nur,
    wenn sie dort keinem Hindernis zu nahe kommen."""
    from shapely import affinity
    from shapely.ops import unary_union
    je: Dict[int, List[Dict]] = {}
    for lg in lagen:
        je.setdefault(lg["platte"], []).append(lg)
    for k, ls in je.items():
        geo = [affinity.translate(affinity.rotate(formen[lg["form"]]["voll"], lg["winkel"], origin=(0, 0)),
                                  lg["dx"], lg["dy"]) for lg in ls]
        x0, y0, x1, y1 = unary_union(geo).bounds
        dx = (rand + (breite - 2 * rand - (x1 - x0)) / 2) - x0
        dy = (rand + (hoehe - 2 * rand - (y1 - y0)) / 2) - y0
        hs = (hindernisse(k) if hindernisse else []) or []
        if hs:
            alle = unary_union(hs)
            if any(affinity.translate(g, dx, dy).distance(alle) < abstand - 1e-6 for g in geo):
                continue
        for lg in ls:
            lg["dx"] += dx
            lg["dy"] += dy


# ----------------------------------------------------------------- Zeichnung
def _ist_ebene(el) -> bool:
    import inkex
    return isinstance(el, inkex.Layer) or (
        el.get("{http://www.inkscape.org/namespaces/inkscape}groupmode") == "layer")


def oberstes(el):
    """Oberstes Objekt (direkt in einer Ebene oder der Zeichnung)."""
    import inkex
    e = el
    while True:
        p = e.getparent()
        if p is None or isinstance(p, inkex.SvgDocumentElement) or _ist_ebene(p):
            return e
        e = p


def _oberste_objekte(svg) -> List:
    import inkex
    aus = []

    def gehen(wurzel):
        for ch in wurzel:
            if not isinstance(ch, inkex.ShapeElement):
                continue
            if _ist_ebene(ch):
                st = (ch.get("style") or "").replace(" ", "")
                if ch.get("display") == "none" or "display:none" in st:
                    continue
                gehen(ch)
            elif not isinstance(ch, inkex.Defs):
                aus.append(ch)
    gehen(svg)
    return aus


def _name(el) -> str:
    lab = el.get("{http://www.inkscape.org/namespaces/inkscape}label")
    return lab or el.get("id") or "Teil"


class _Objekt:
    """Ein oberstes Objekt: Geometrie in mm (global), Fräsfarben, Spannmittel, Steg."""

    def __init__(self, el, uu, hoehe_uu, e, dicke):
        from shapely.geometry import LineString, Polygon
        from shapely.ops import unary_union
        from . import einstellungen as einst
        from .farben import fraesart
        from .svg_lesen import auch, farbe, ist_steg, linien, spann_hoehe
        self.el = el
        self.id = el.get("id")
        self.spann = spann_hoehe(el) is not None
        self.steg = ist_steg(el)
        self.fraesen = False
        voll, loecher, offen = [], [], []
        kontur_durch = {b["id"] for b in e.get("bearbeitungen", [])
                        if b.get("art") == "kontur" and (b.get("durch") or
                                                         float(b.get("tiefe") or 0) >= dicke - 1e-6)}
        for sub in _elemente_mit(el):
            f = farbe(sub)
            art = fraesart(f) if f else None
            if art is not None and art != "aus" or auch(sub):
                self.fraesen = True
            try:
                st = linien(sub, uu, hoehe_uu, 0.05)
            except Exception:
                continue
            durch = False
            if art == "kontur":
                el_info = [{"art": art, "rgb": f, "auch": auch(sub), "seite": 1}]
                zu = einst.zuordnen(e, el_info) if e.get("bearbeitungen") else {}
                durch = any(bid in kontur_durch and els for bid, els in zu.items())
            for pts, zu_ in st:
                if zu_ and len(pts) >= 3:
                    try:
                        p = Polygon(pts).buffer(0)
                    except Exception:
                        continue
                    if p.is_empty:
                        continue
                    voll.append(p)
                    if durch:
                        loecher.append(p)
                elif len(pts) >= 2:
                    offen.append(LineString(pts).buffer(0.05, quad_segs=2))
        g = unary_union(voll + offen) if (voll or offen) else None
        if g is None or g.is_empty:
            bb = el.bounding_box(el.getparent().composed_transform() if el.getparent() is not None else None)
            if bb is not None and bb.width > 0 and bb.height > 0:
                from shapely.geometry import box
                g = box(bb.left / uu, (hoehe_uu - bb.bottom) / uu, bb.right / uu, (hoehe_uu - bb.top) / uu)
        self.voll = _ohne_loecher(g) if g is not None else None
        mat = self.voll
        if mat is not None and loecher:
            # durchgehende innere Konturen sind Löcher (nicht die äusserste)
            innen = []
            for p in loecher:
                pt = p.representative_point()
                huelle = [q for q in _aussen(self.voll) if q.contains(pt)]
                if huelle and p.area < 0.98 * huelle[0].area:
                    innen.append(p)
            if innen:
                mat = mat.difference(unary_union(innen))
        self.material = mat
        if self.voll is not None:
            self.voll = self.voll.simplify(0.05)
            self.material = self.material.simplify(0.05)

    def mitte(self):
        if self.voll is None or self.voll.is_empty:
            return None
        b = self.voll.bounds
        return ((b[0] + b[2]) / 2, (b[1] + b[3]) / 2)


def _elemente_mit(el):
    """el selbst und seine sichtbaren Formen (wie svg_lesen.elemente, aber mit el)."""
    import inkex
    from .svg_lesen import _sichtbar
    for sub in [el] + list(el.descendants()):
        if not isinstance(sub, inkex.ShapeElement):
            continue
        if isinstance(sub, (inkex.Group, inkex.TextElement, inkex.Tspan, inkex.Image, inkex.Use,
                            inkex.FlowRoot)):
            continue
        if not hasattr(sub, "path") or not _sichtbar(sub):
            continue
        yield sub


def _aussen(g):
    if g is None:
        return []
    if g.geom_type == "Polygon":
        return [g]
    return [p for p in getattr(g, "geoms", []) if p.geom_type == "Polygon"]


def _ohne_loecher(g):
    from shapely.geometry import Polygon
    from shapely.ops import unary_union
    if g is None:
        return None
    teile = [Polygon(p.exterior) for p in _aussen(g)]
    return unary_union(teile) if teile else g


class Zeichnung:
    def __init__(self, pfad: str):
        import inkex
        from . import einstellungen as einst
        from .svg_lesen import materialdicke, seiten
        self.svg = inkex.load_svg(pfad).getroot()
        from .svg_lesen import klone_aufloesen
        klone_aufloesen(self.svg)
        self.uu = self.svg.unittouu("1mm")
        self.hvb = self.svg.viewbox_height
        self.e = einst.laden(self.svg.get(einst.ATTRIBUT))
        self.zweiseitig = einst.zweiseitig_an(self.e)
        z = dict(einst.STANDARD["zweiseitig"])
        z.update(self.e.get("zweiseitig") or {})
        self.z = z
        self.dicke = materialdicke(self.svg)
        self.seiten = seiten(self.svg)
        self.pp = 2 if self.zweiseitig else 1
        p1 = self.seiten[0]
        self.breite, self.hoehe = p1["breite"], p1["hoehe"]
        self.objekte = []
        for el in _oberste_objekte(self.svg):
            try:
                o = _Objekt(el, self.uu, self.hvb, self.e, self.dicke)
            except Exception:
                continue
            if o.voll is None or o.voll.is_empty:
                continue
            o.seite = self.seite_von(o)
            self.objekte.append(o)

    def seite_von(self, o) -> int:
        """Index der Seite (0, 1, …), auf der die Mitte des Objekts liegt; -1: auf keiner."""
        m = o.mitte()
        if m is None:
            return -1
        for i, pg in enumerate(self.seiten):
            if pg["x0"] - 1e-6 <= m[0] <= pg["x0"] + pg["breite"] + 1e-6 and \
                    pg["y0"] - 1e-6 <= m[1] <= pg["y0"] + pg["hoehe"] + 1e-6:
                return i
        return -1

    def platten_vorhanden(self) -> int:
        return max(1, math.ceil(len(self.seiten) / self.pp))

    def seite(self, k: int, s: int = 0) -> Dict:
        """Seite s (0 = oben, 1 = unten) der Platte k – vorhanden oder neu (rechts angehängt)."""
        i = k * self.pp + s
        if i < len(self.seiten):
            return self.seiten[i]
        rechts = max(pg["links"] + pg["breite"] for pg in self.seiten)
        n = i - len(self.seiten)
        links = rechts + SEITEN_ABSTAND + n * (self.breite + SEITEN_ABSTAND)
        oben = self.seiten[0]["oben"]
        return {"links": links, "oben": oben, "breite": self.breite, "hoehe": self.hoehe,
                "x0": links, "y0": (self.hvb / self.uu) - oben - self.hoehe, "neu": True}

    def teile(self, ids: Optional[Sequence[str]] = None) -> List:
        """Die Teile: die gewählten obersten Objekte – sonst alle mit Fräsfarben auf der
        Oberseite der ersten Platte (ohne Spannmittel und Steg-Markierungen)."""
        if ids:
            gewaehlt = []
            for i in ids:
                el = self.svg.getElementById(i)
                if el is None:
                    continue
                top = oberstes(el)
                if top.get("id") not in gewaehlt:
                    gewaehlt.append(top.get("id"))
            return [o for o in self.objekte if o.id in gewaehlt and not o.spann and not o.steg]
        return [o for o in self.objekte if o.seite in (0, -1) and o.fraesen and not o.spann and not o.steg]

    def begleiter(self, teil, alle_teile) -> List:
        """Was mit dem Teil wandert: Steg-Markierungen darauf und (zweiseitig) die
        Objekte der Unterseite, die gewendet auf dem Teil liegen."""
        aus = []
        ids_teile = {t.id for t in alle_teile}
        k = max(0, teil.seite) // self.pp
        p_oben = self.seite(k, 0)
        for o in self.objekte:
            if o.id in ids_teile or o is teil or o.spann:
                continue
            m = o.mitte()
            if m is None:
                continue
            if o.steg and o.seite == teil.seite:
                from shapely.geometry import Point
                if teil.voll.buffer(1.0).contains(Point(m)):
                    aus.append(o)
            elif self.zweiseitig and o.seite == k * 2 + 1:
                p_unten = self.seite(k, 1)
                lok = (m[0] - p_unten["x0"], m[1] - p_unten["y0"])
                sp = _spiegel(self.z.get("achse"), self.breite, self.hoehe)
                x = sp[0][0] * lok[0] + sp[0][1] * lok[1] + sp[0][2] + p_oben["x0"]
                y = sp[1][0] * lok[0] + sp[1][1] * lok[1] + sp[1][2] + p_oben["y0"]
                from shapely.geometry import Point
                if teil.voll.buffer(0.5).contains(Point(x, y)):
                    aus.append(o)
        return aus

    def uu_matrix(self, m) -> List[float]:
        """Affine Abbildung in mm (Y oben) → SVG-Matrix in Benutzereinheiten (Y unten)."""
        import numpy as np
        S = np.array([[1 / self.uu, 0, 0], [0, -1 / self.uu, self.hvb / self.uu], [0, 0, 1.0]])
        T = np.linalg.inv(S) @ m @ S
        return [round(float(v), 9) for v in (T[0][0], T[1][0], T[0][1], T[1][1], T[0][2], T[1][2])]


def _pass_hindernisse(zg: Zeichnung) -> List:
    from shapely.geometry import Point
    from . import einstellungen as einst
    if not zg.zweiseitig or not zg.z.get("passloecher"):
        return []
    r = float(zg.z.get("pass_d") or 6.0) / 2 + float(zg.z.get("pass_extra") or 3.0)
    return [Point(x, y).buffer(r) for x, y in einst.passloecher(zg.e, zg.breite, zg.hoehe)]


def vorschlag(zg: Zeichnung) -> Dict:
    """Abstand und Rand vorschlagen: grösster Konturfräser + 3 mm, Rand 10 mm."""
    d = 0.0
    for b in zg.e.get("bearbeitungen", []):
        if b.get("art") == "kontur":
            w = (zg.e.get("werkzeuge") or {}).get(b.get("werkzeug") or "", {})
            try:
                d = max(d, float(w.get("durchmesser") or 0))
            except (TypeError, ValueError):
                pass
    return {"abstand": round((d or 6.0) + 3.0, 1), "rand": 10.0}


def teile_info(pfad: str, ids: Optional[List[str]] = None) -> Dict:
    zg = Zeichnung(pfad)
    tt = zg.teile(ids)
    v = vorschlag(zg)
    return {"ok": True, "teile": [{"id": t.id, "name": _name(t.el),
                                   "breite": round(t.voll.bounds[2] - t.voll.bounds[0], 1),
                                   "hoehe": round(t.voll.bounds[3] - t.voll.bounds[1], 1),
                                   "loecher": t.material is not None and not t.material.equals(t.voll)}
                                  for t in tt],
            "platte": {"breite": round(zg.breite, 1), "hoehe": round(zg.hoehe, 1)},
            "zweiseitig": zg.zweiseitig, "platten": zg.platten_vorhanden(),
            "abstand": v["abstand"], "rand": v["rand"],
            "auswahl": bool(ids)}


def rechnen(pfad: str, auftrag: Dict) -> Dict:
    """auftrag: {"teile": [{"id", "anzahl"}], "drehen", "abstand", "abstand_y", "rand", "in_loechern",
    "art" (eng|raster), "richtung" (auto|x|y), "ecke" (ul|ol|ur|or), "original_bleibt",
    "fuellen" (so viele wie auf eine Platte passen), "zentrieren"}.
    Liefert je Objekt, das wandert, die Kopien mit ihrer Matrix (SVG, Benutzereinheiten,
    auf die Dokumentkoordinaten anzuwenden) und die neuen Seiten."""
    zg = Zeichnung(pfad)
    anzahl = {str(t["id"]): int(t.get("anzahl", 1)) for t in auftrag.get("teile", [])}
    teile = [t for t in zg.teile(list(anzahl)) if anzahl.get(t.id, 0) > 0]
    if not teile:
        return {"ok": False, "warnungen": ["Keine Teile gefunden."]}
    ids_teile = {t.id for t in teile}
    p0 = zg.seite(0, 0)
    fuellen = bool(auftrag.get("fuellen", False))
    bleibt = bool(auftrag.get("original_bleibt", True))
    # Formen in Koordinaten der ersten Platte (Oberseite)
    from shapely import affinity
    formen = []
    originale = []                  # bleiben liegen: Hindernisse auf Platte 1
    for t in teile:
        n = 9999 if fuellen else anzahl[t.id]
        ab = 0
        voll = affinity.translate(t.voll, -p0["x0"], -p0["y0"])
        if bleibt and t.seite == 0:
            originale.append(voll)
            n -= 1
            ab = 1
        formen.append({"voll": voll, "material": affinity.translate(t.material, -p0["x0"], -p0["y0"]),
                       "anzahl": max(0, n), "kopie_ab": ab})
    pass_h = _pass_hindernisse(zg)
    achse = zg.z.get("achse") or "x"

    def hindernisse(k: int):
        aus = list(pass_h) + (list(originale) if k == 0 else [])
        for s in range(zg.pp):
            i = k * zg.pp + s
            if i >= len(zg.seiten):
                continue
            pg = zg.seiten[i]
            for o in zg.objekte:
                if o.seite != i or o.id in ids_teile:
                    continue
                g = affinity.translate(o.voll, -pg["x0"], -pg["y0"])
                if s == 1:
                    g = _shapely_affin(g, _spiegel(achse, zg.breite, zg.hoehe))
                aus.append(g)
        return aus

    # Begleiter (Stege, Unterseite) gehören zum Teil – nicht als Hindernis
    begleit = {t.id: zg.begleiter(t, teile) for t in teile}
    for bl in begleit.values():
        for o in bl:
            ids_teile.add(o.id)
    abstand = float(auftrag.get("abstand", 8.0))
    rand = float(auftrag.get("rand", 10.0))
    erg = anordnen(formen, zg.breite, zg.hoehe, abstand=abstand, rand=rand,
                   drehen=str(auftrag.get("drehen", "90")),
                   in_loechern=bool(auftrag.get("in_loechern", False)), hindernisse=hindernisse,
                   platten_max=1 if fuellen else PLATTEN_MAX, art=str(auftrag.get("art", "eng")),
                   richtung=str(auftrag.get("richtung", "auto")), ecke=str(auftrag.get("ecke", "ul")),
                   abstand_y=auftrag.get("abstand_y"))
    if auftrag.get("zentrieren"):
        zentrieren(erg["lagen"], formen, zg.breite, zg.hoehe, rand,
                   min(abstand, float(auftrag.get("abstand_y") or abstand)), hindernisse)
    if fuellen:
        erg["nicht"] = 0
    sp = _spiegel(achse, zg.breite, zg.hoehe)
    bewegen: List[Dict] = []
    for lg in erg["lagen"]:
        t = teile[lg["form"]]
        k = lg["platte"]
        L = _affin(lg["winkel"], lg["dx"], lg["dy"])
        po = zg.seite(k, 0)
        G1 = _verschiebung(po["x0"], po["y0"]) @ L @ _verschiebung(-p0["x0"], -p0["y0"])
        bewegen.append({"id": t.id, "kopie": lg["kopie"], "platte": k, "matrix": zg.uu_matrix(G1)})
        for o in begleit[t.id]:
            if o.steg and o.seite == t.seite:
                bewegen.append({"id": o.id, "kopie": lg["kopie"], "platte": k, "matrix": zg.uu_matrix(G1)})
            else:
                pu0 = zg.seite(0, 1)
                pu = zg.seite(k, 1)
                G2 = (_verschiebung(pu["x0"], pu["y0"]) @ sp @ L @ sp @
                      _verschiebung(-pu0["x0"], -pu0["y0"]))
                bewegen.append({"id": o.id, "kopie": lg["kopie"], "platte": k, "matrix": zg.uu_matrix(G2)})
    neue_seiten = []
    n_vorh = len(zg.seiten)
    for k in range(erg["platten"]):
        for s in range(zg.pp):
            pg = zg.seite(k, s)
            if pg.get("neu"):
                neue_seiten.append([pg["links"] * zg.uu, pg["oben"] * zg.uu, pg["breite"] * zg.uu,
                                    pg["hoehe"] * zg.uu])
    flaeche = sum(f["voll"].area * (f["anzahl"] + f["kopie_ab"]) for f in formen) if not fuellen else 0.0
    gelegt = len(erg["lagen"])
    bild = _bild(zg, formen, erg, originale, hindernisse)
    w = []
    if erg["nicht"]:
        w.append("%d Teil(e) passen auf keine Platte (zu gross für Platte, Rand und Abstand) – "
                 "sie bleiben, wo sie sind." % erg["nicht"])
    platten = max(1, erg["platten"])
    nutzung = 100.0 * (sum(formen[lg["form"]]["voll"].area for lg in erg["lagen"]) +
                       sum(g.area for g in originale)) / (platten * zg.breite * zg.hoehe)
    gesamt = gelegt + len(originale)
    return {"ok": gelegt > 0, "bewegen": bewegen, "neue_seiten": neue_seiten, "bild": bild,
            "teile_gesamt": gesamt,
            "seiten_vorher": n_vorh, "platten": erg["platten"], "gelegt": gelegt, "nicht": erg["nicht"],
            "nutzung": round(nutzung, 1), "zelle": round(erg["zelle"], 2), "warnungen": w,
            "flaeche": round(flaeche, 1)}


def _ringe(g, toleranz=0.4, punkte_max=160) -> List[List[float]]:
    g = g.simplify(toleranz)
    aus = []
    for p in _aussen(g):
        for ring in [p.exterior] + list(p.interiors):
            pts = list(ring.coords)
            if len(pts) > punkte_max:
                schritt = len(pts) / punkte_max
                pts = [pts[int(i * schritt)] for i in range(punkte_max)]
            aus.append([round(v, 1) for q in pts for v in q])
    return aus


def _bild(zg, formen, erg, originale, hindernisse) -> Dict:
    """Vorschau für den Dialog: Platten, Teile (Kopien, Originale), Hindernisse – mm je Platte."""
    from shapely import affinity
    n = max(1, erg["platten"], 1 if originale else 0)
    teile = []
    for lg in erg["lagen"][:600]:
        g = affinity.translate(affinity.rotate(formen[lg["form"]]["material"], lg["winkel"], origin=(0, 0)),
                               lg["dx"], lg["dy"])
        teile.append({"platte": lg["platte"], "art": "kopie", "ringe": _ringe(g)})
    for g in originale:
        teile.append({"platte": 0, "art": "original", "ringe": _ringe(g)})
    orig_ids = {id(g) for g in originale}
    for k in range(n):
        for h in (hindernisse(k) or []):
            if id(h) in orig_ids:
                continue
            teile.append({"platte": k, "art": "hindernis", "ringe": _ringe(h)})
    return {"platten": n, "breite": round(zg.breite, 1), "hoehe": round(zg.hoehe, 1), "teile": teile}


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("befehl", choices=["teile", "rechnen"])
    ap.add_argument("svg")
    ap.add_argument("--ids", default="")
    ap.add_argument("--auftrag", default="")
    a = ap.parse_args(argv)
    try:
        if a.befehl == "teile":
            erg = teile_info(a.svg, [i for i in a.ids.split(",") if i])
        else:
            with open(a.auftrag, encoding="utf-8") as f:
                erg = rechnen(a.svg, json.load(f))
    except Exception as ex:
        import traceback
        erg = {"ok": False, "warnungen": [f"Fehler: {ex}"], "traceback": traceback.format_exc()}
    sys.stdout.write(json.dumps(erg, ensure_ascii=False))
    sys.stdout.flush()
    return 0
