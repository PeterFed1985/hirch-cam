# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Zeichnung aufräumen – vor allem nach dem Import von DXF, AI, PDF oder EPS.

* doppelte Linien: Stücke, die genau übereinander liegen (auch in umgekehrter
  Richtung), fallen weg – ein Objekt, das nur aus doppelten Stücken besteht, ganz
* verbinden: offene Stücke gleicher Farbe, deren Enden sich treffen, werden zu einem
  Pfad zusammengesetzt (DXF: Linien und Bögen einzeln)
* schliessen: ein Pfad, dessen Ende fast auf dem Anfang liegt, wird geschlossen –
  bis zur Fangweite still, bis zur Schliessweite mit Meldung
* melden: Linien, die nur teilweise übereinander liegen; fast geschlossene Pfade mit
  grösserer Lücke; eine DXF-Zeichnung, die nach Zoll aussieht

Gerechnet wird in Benutzereinheiten des Dokuments (Y nach unten); das Panel setzt
die Änderungen in einem Schritt (rückgängig machbar) um.
"""
from __future__ import annotations

import json
import math
import sys
from typing import Dict, List, Optional, Sequence

FANG = 0.05            # mm: Enden, die so nahe liegen, gehören zusammen
SCHLIESSEN = 1.0       # mm: grössere Lücken werden nicht geschlossen, nur gemeldet
DOPPELT = 0.05         # mm: so genau müssen doppelte Linien übereinander liegen
TEILWEISE = 2.0        # mm: so lang muss eine teilweise doppelte Strecke sein


class Stueck:
    """Ein Unterpfad eines Objekts: Kurve (CubicSuperPath-Knoten) und Polylinie."""

    def __init__(self, el, nr, knoten, zu, poly):
        self.el = el
        self.nr = nr
        self.knoten = knoten          # [[h_in, p, h_out], …] in Benutzereinheiten
        self.zu = zu
        self.poly = poly              # [(x, y)] in Benutzereinheiten
        self.weg = False              # doppelt: fällt weg

    @property
    def anfang(self):
        return self.knoten[0][1]

    @property
    def ende(self):
        return self.knoten[-1][1]

    def laenge(self):
        return sum(math.dist(a, b) for a, b in zip(self.poly, self.poly[1:]))


def _umgedreht(knoten):
    return [[k[2], k[1], k[0]] for k in reversed(knoten)]


def _pfad_d(teile) -> str:
    """teile: [(knoten, zu)] → SVG-Pfaddaten (absolut)."""
    aus = []
    for knoten, zu in teile:
        if not knoten:
            continue
        p0 = knoten[0][1]
        aus.append("M %.8g %.8g" % (p0[0], p0[1]))
        folge = list(zip(knoten, knoten[1:]))
        if zu:
            folge.append((knoten[-1], knoten[0]))
        for a, b in folge:
            if _gerade(a, b):
                aus.append("L %.8g %.8g" % (b[1][0], b[1][1]))
            else:
                aus.append("C %.8g %.8g %.8g %.8g %.8g %.8g" % (a[2][0], a[2][1], b[0][0], b[0][1],
                                                                  b[1][0], b[1][1]))
        if zu:
            aus.append("Z")
    return " ".join(aus)


def _gerade(a, b) -> bool:
    return (math.dist(a[2], a[1]) < 1e-9) and (math.dist(b[0], b[1]) < 1e-9)


def _stil_schluessel(el) -> str:
    st = el.specified_style()
    s = st.get("stroke")
    if s and s not in ("none",):
        return "s:" + str(s).lower()
    return "f:" + str(st.get("fill") or "").lower()


def _fraesart(el):
    from .farben import fraesart
    from .svg_lesen import farbe
    f = farbe(el)
    return fraesart(f) if f else None


class Aufraeumer:
    def __init__(self, svg, ids: Optional[Sequence[str]] = None, fang: float = FANG,
                 schliessen: float = SCHLIESSEN):
        import inkex
        from inkex import bezier
        from .svg_lesen import _sichtbar, ist_steg, spann_hoehe
        self.svg = svg
        self.uu = svg.unittouu("1mm")
        self.fang = fang * self.uu
        self.schliessen = schliessen * self.uu
        self.doppelt = DOPPELT * self.uu
        wurzeln = [svg.getElementById(i) for i in (ids or [])]
        wurzeln = [w for w in wurzeln if w is not None] or [svg]
        self.elemente = []
        gesehen = set()
        for w in wurzeln:
            for el in [w] + list(w.descendants()):
                if id(el) in gesehen or not isinstance(el, inkex.ShapeElement):
                    continue
                gesehen.add(id(el))
                if isinstance(el, (inkex.Group, inkex.TextElement, inkex.Tspan, inkex.Image, inkex.Use,
                                   inkex.FlowRoot)) or not hasattr(el, "path"):
                    continue
                if not _sichtbar(el) or ist_steg(el) or spann_hoehe(el) is not None:
                    continue
                self.elemente.append(el)
        self.stuecke: List[Stueck] = []
        self.je_el: Dict[int, List[Stueck]] = {}
        tol = 0.02 * self.uu
        for el in self.elemente:
            try:
                pfad = el.path.to_absolute().transform(el.composed_transform())
            except Exception:
                continue
            teile, akt = [], None
            for seg in pfad:
                if seg.letter in "Mm":
                    akt = inkex.Path([seg])
                    teile.append([akt, False])
                elif akt is not None:
                    akt.append(seg)
                    if seg.letter in "Zz":
                        teile[-1][1] = True
            liste = []
            for nr, (teil, zu) in enumerate(teile):
                csp = teil.to_superpath()
                for sub in csp:
                    if len(sub) < 2:
                        continue
                    knoten = [[list(k[0]), list(k[1]), list(k[2])] for k in sub]
                    if zu and math.dist(knoten[0][1], knoten[-1][1]) < 1e-9:
                        # geschlossen: letzter Knoten = erster
                        knoten[0][0] = knoten[-1][0]
                        knoten = knoten[:-1]
                    elif not zu and len(knoten) > 2 and math.dist(knoten[0][1], knoten[-1][1]) < 1e-9:
                        zu = True
                        knoten[0][0] = knoten[-1][0]
                        knoten = knoten[:-1]
                    if len(knoten) < 2 and not zu:
                        continue
                    flach = [[list(k[0]), list(k[1]), list(k[2])] for k in knoten]
                    if zu:
                        flach = flach + [[list(knoten[0][0]), list(knoten[0][1]), list(knoten[0][2])]]
                    cs = [flach]
                    bezier.cspsubdiv(cs, tol)
                    poly = [(p[1][0], p[1][1]) for p in cs[0]]
                    s = Stueck(el, nr, knoten, zu, poly)
                    liste.append(s)
                    self.stuecke.append(s)
            self.je_el[id(el)] = liste
        self.stellen: List[Dict] = []
        self.zahlen = {"doppelt": 0, "doppelt_objekte": 0, "verbunden": 0, "zuege": 0, "geschlossen": 0,
                       "luecken": [], "offen": 0, "teilweise": 0, "art_konflikt": 0}

    # ------------------------------------------------------------------ doppelt
    def doppelte(self):
        import shapely
        from shapely import STRtree
        from shapely.geometry import LineString
        geo = []
        for s in self.stuecke:
            pts = s.poly if len(s.poly) >= 2 else s.poly * 2
            geo.append(LineString(pts))
        baum = STRtree(geo)
        for i, s in enumerate(self.stuecke):
            if s.weg:
                continue
            for j in baum.query(geo[i].buffer(self.doppelt)):
                j = int(j)
                if j <= i:
                    continue
                t = self.stuecke[j]
                if t.weg or t.el is s.el and t.nr == s.nr:
                    continue
                a, b = geo[i], geo[j]
                if abs(a.length - b.length) > 4 * self.doppelt + 0.01 * max(a.length, b.length):
                    continue
                if shapely.hausdorff_distance(a, b, densify=0.25) > self.doppelt:
                    continue
                art_s, art_t = _fraesart(s.el), _fraesart(t.el)
                if art_s and art_t and art_s != art_t:
                    self.zahlen["art_konflikt"] += 1
                    self._stelle(a, "Gleiche Linie in zwei Fräsarten (%s und %s) – bitte prüfen" % (art_s, art_t))
                    continue
                # behalten: das Objekt mit Kontur (Strich), sonst das untere
                weg = t
                if self._hat_strich(t.el) and not self._hat_strich(s.el):
                    weg = s
                weg.weg = True
                self.zahlen["doppelt"] += 1
                if weg is s:
                    break

    @staticmethod
    def _hat_strich(el):
        st = el.specified_style().get("stroke")
        return bool(st) and st != "none"

    # ------------------------------------------------------------------ verbinden
    def verbinden(self):
        """Ketten offener Stücke je Farbe. Liefert [(stuecke_mit_richtung, zu, luecke)]."""
        offen = [s for s in self.stuecke if not s.weg and not s.zu]
        gruppen: Dict[str, List[Stueck]] = {}
        for s in offen:
            gruppen.setdefault(_stil_schluessel(s.el), []).append(s)
        ketten = []
        zelle = max(self.fang * 2, 1e-6)
        for liste in gruppen.values():
            raster: Dict[tuple, List] = {}

            def key(p):
                return (int(math.floor(p[0] / zelle)), int(math.floor(p[1] / zelle)))

            for s in liste:
                for ende, p in ((0, s.anfang), (1, s.ende)):
                    raster.setdefault(key(p), []).append((s, ende))
            benutzt = set()

            def naechstes(p):
                kx, ky = key(p)
                beste = None
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        for s, ende in raster.get((kx + dx, ky + dy), []):
                            if id(s) in benutzt:
                                continue
                            q = s.anfang if ende == 0 else s.ende
                            d = math.dist(p, q)
                            if d <= self.fang and (beste is None or d < beste[0]):
                                beste = (d, s, ende)
                return beste

            for s in liste:
                if id(s) in benutzt:
                    continue
                benutzt.add(id(s))
                kette = [(s, False)]
                while True:                       # vorwärts
                    letzt, umg = kette[-1]
                    p = letzt.anfang if umg else letzt.ende
                    b = naechstes(p)
                    if b is None:
                        break
                    _d, t, ende = b
                    benutzt.add(id(t))
                    kette.append((t, ende == 1))
                while True:                       # rückwärts
                    erst, umg = kette[0]
                    p = erst.ende if umg else erst.anfang
                    b = naechstes(p)
                    if b is None:
                        break
                    _d, t, ende = b
                    benutzt.add(id(t))
                    kette.insert(0, (t, ende == 0))
                ketten.append(kette)
        return ketten

    def _knoten_der_kette(self, kette):
        knoten = []
        for s, umg in kette:
            k = [list(map(list, x)) for x in (_umgedreht(s.knoten) if umg else s.knoten)]
            if knoten:
                # Stossstelle: ein Knoten (Mitte), Griffe beider Seiten behalten
                a, b = knoten[-1], k[0]
                m = [(a[1][0] + b[1][0]) / 2, (a[1][1] + b[1][1]) / 2]
                da = [m[0] - a[1][0], m[1] - a[1][1]]
                db = [m[0] - b[1][0], m[1] - b[1][1]]
                knoten[-1] = [[a[0][0] + da[0], a[0][1] + da[1]], m, [b[2][0] + db[0], b[2][1] + db[1]]]
                knoten.extend(k[1:])
            else:
                knoten.extend(k)
        return knoten

    # ------------------------------------------------------------------ teilweise doppelt
    def teilweise(self):
        from shapely import STRtree
        from shapely.geometry import LineString
        aktiv = [s for s in self.stuecke if not s.weg and len(s.poly) >= 2]
        geo = [LineString(s.poly + ([s.poly[0]] if s.zu else [])) for s in aktiv]
        baum = STRtree(geo)
        grenze = TEILWEISE * self.uu
        n = 0
        for i, s in enumerate(aktiv):
            puffer = geo[i].buffer(self.doppelt * 1.5)
            for j in baum.query(puffer):
                j = int(j)
                if j <= i or aktiv[j].el is s.el:
                    continue
                gemeinsam = geo[j].intersection(puffer)
                if gemeinsam.is_empty or gemeinsam.length < grenze:
                    continue
                n += 1
                if n <= 30:
                    self._stelle(gemeinsam, "Linien liegen auf %s mm übereinander" % _z(gemeinsam.length / self.uu, 1))
        self.zahlen["teilweise"] = n

    def _stelle(self, geom, text):
        try:
            p = geom.interpolate(0.5, normalized=True) if hasattr(geom, "interpolate") else geom.centroid
        except Exception:
            p = geom.centroid
        self.stellen.append({"x": round(p.x, 4), "y": round(p.y, 4), "text": text})

    # ------------------------------------------------------------------ alles
    def rechnen(self) -> Dict:
        from shapely.geometry import Point
        self.doppelte()
        ketten = self.verbinden()
        geaendert_el = set()
        verbraucht = set()                      # Stücke, die in einer neuen Kette stecken
        neue_teile: Dict[int, List] = {}        # Anker-Objekt -> [(knoten, zu)]
        for kette in ketten:
            knoten = self._knoten_der_kette(kette)
            luecke = math.dist(knoten[0][1], knoten[-1][1])
            laenge = sum(st.laenge() for st, _u in kette)
            mitte = Point((knoten[0][1][0] + knoten[-1][1][0]) / 2, (knoten[0][1][1] + knoten[-1][1][1]) / 2)
            zu = False
            if luecke <= self.schliessen and laenge > 3 * max(luecke, 1e-9) and laenge > 2 * self.fang:
                zu = True
                if luecke > self.fang:
                    self.zahlen["luecken"].append(round(luecke / self.uu, 2))
                    self._stelle(mitte, "Lücke %s mm geschlossen" % _z(luecke / self.uu))
                if luecke <= self.fang:
                    # letzter Knoten = erster: zusammenlegen
                    a, b = knoten[-1], knoten[0]
                    m = [(a[1][0] + b[1][0]) / 2, (a[1][1] + b[1][1]) / 2]
                    knoten[0] = [[a[0][0] + m[0] - a[1][0], a[0][1] + m[1] - a[1][1]], m,
                                 [b[2][0] + m[0] - b[1][0], b[2][1] + m[1] - b[1][1]]]
                    knoten = knoten[:-1]
                # sonst: gerade Linie über die Lücke (Z)
                self.zahlen["geschlossen"] += 1
            elif luecke <= 10 * self.uu and laenge > 10 * luecke:
                self.zahlen["offen"] += 1
                self._stelle(mitte, "Offen: Lücke %s mm – nicht geschlossen" % _z(luecke / self.uu))
            if len(kette) > 1 or zu:
                if len(kette) > 1:
                    self.zahlen["verbunden"] += len(kette)
                    self.zahlen["zuege"] += 1
                for st, _u in kette:
                    geaendert_el.add(id(st.el))
                    verbraucht.add(id(st))
                neue_teile.setdefault(id(kette[0][0].el), []).append((knoten, zu))
        for st in self.stuecke:                 # doppelte fallen weg: Objekt ändert sich
            if st.weg:
                geaendert_el.add(id(st.el))
        neu: List[Dict] = []
        loeschen: List[str] = []
        ganz_weg = 0
        for el in self.elemente:
            if id(el) not in geaendert_el or not el.get("id"):
                continue
            eigene = self.je_el.get(id(el), [])
            rest = [(st.knoten, st.zu) for st in eigene if not st.weg and id(st) not in verbraucht]
            teile = neue_teile.get(id(el), []) + rest
            loeschen.append(el.get("id"))
            if teile:
                neu.append({"nach": el.get("id"), "d": _pfad_d(teile), "style": _stil_text(el)})
            elif eigene and all(st.weg for st in eigene):
                ganz_weg += 1
        self.zahlen["doppelt_objekte"] = ganz_weg
        self.teilweise()
        return {"neu": neu, "loeschen": loeschen}


def _z(v: float, stellen: int = 2) -> str:
    return ("%.*f" % (stellen, v)).replace(".", ",")


def _stil_text(el) -> str:
    """Stil des Objekts samt geerbter Farbe (die neue Linie liegt evtl. anderswo)."""
    st = el.specified_style()
    teile = []
    for k in ("fill", "fill-opacity", "stroke", "stroke-width", "stroke-opacity", "stroke-dasharray",
              "stroke-linecap", "stroke-linejoin", "opacity"):
        v = st.get(k)
        if v is not None and str(v) != "":
            teile.append("%s:%s" % (k, v))
    if "fill" not in st:
        teile.append("fill:none")
    return ";".join(teile)


def rechnen(pfad: str, ids: Optional[List[str]] = None, endung: str = "", fang: float = FANG,
            schliessen: float = SCHLIESSEN) -> Dict:
    import inkex
    svg = inkex.load_svg(pfad).getroot()
    a = Aufraeumer(svg, ids, fang=fang, schliessen=schliessen)
    ops = a.rechnen()
    z = a.zahlen
    bericht = []
    if z["doppelt"]:
        bericht.append("%d doppelte Linie(n) entfernt%s" % (
            z["doppelt"], " (davon %d ganze Objekte)" % z["doppelt_objekte"] if z["doppelt_objekte"] else ""))
    if z["zuege"]:
        bericht.append("%d Stücke zu %d Pfad(en) verbunden" % (z["verbunden"], z["zuege"]))
    if z["geschlossen"]:
        t = "%d Pfad(e) geschlossen" % z["geschlossen"]
        if z["luecken"]:
            t += " · Lücken %s mm" % ", ".join(("%.2f" % v).replace(".", ",") for v in sorted(z["luecken"])[:6])
        bericht.append(t)
    if z["offen"]:
        bericht.append("%d Pfad(e) mit grösserer Lücke offen gelassen – bitte prüfen" % z["offen"])
    if z["teilweise"]:
        bericht.append("%d Stelle(n), an denen Linien teilweise übereinander liegen" % z["teilweise"])
    if z["art_konflikt"]:
        bericht.append("%d Linie(n) doppelt in verschiedenen Fräsarten – nicht entfernt" % z["art_konflikt"])
    # DXF in Zoll?
    einheit = None
    if endung.lower() in ("dxf", "plt", "hpgl") and a.stuecke:
        xs = [p[0] for s in a.stuecke for p in s.poly]
        ys = [p[1] for s in a.stuecke for p in s.poly]
        gross = max(max(xs) - min(xs), max(ys) - min(ys)) / a.uu
        material = max(svg.viewbox_width, svg.viewbox_height) / a.uu
        if 0 < gross < 20 and material >= 100:
            einheit = {"faktor": 25.4, "text": "Die Zeichnung ist nur %s mm gross – war sie in Zoll gezeichnet?"
                       % _z(gross, 1)}
        elif gross > 25.4 * max(material, 1) * 0.8:
            einheit = {"faktor": 1 / 25.4, "text": "Die Zeichnung ist %.0f mm gross – viel grösser als das "
                       "Material. War sie in Zoll und wurde als mm gelesen?" % gross}
    return {"ok": True, "neu": ops["neu"], "loeschen": ops["loeschen"], "bericht": bericht,
            "stellen": a.stellen[:40], "einheit": einheit, "zahlen": z,
            "geaendert": bool(ops["neu"] or ops["loeschen"])}


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("svg")
    ap.add_argument("--ids", default="")
    ap.add_argument("--endung", default="")
    ap.add_argument("--fang", type=float, default=FANG)
    ap.add_argument("--schliessen", type=float, default=SCHLIESSEN)
    a = ap.parse_args(argv)
    try:
        erg = rechnen(a.svg, [i for i in a.ids.split(",") if i], a.endung, a.fang, a.schliessen)
    except Exception as ex:
        import traceback
        erg = {"ok": False, "warnungen": ["Fehler: %s" % ex], "traceback": traceback.format_exc()}
    sys.stdout.write(json.dumps(erg, ensure_ascii=False))
    sys.stdout.flush()
    return 0
