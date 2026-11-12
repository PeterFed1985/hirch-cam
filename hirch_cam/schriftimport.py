# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Schriften für die Einlinien-Gravur einlesen und in der eigenen
Schriftbibliothek ablegen (~/.config/hirch-cam/schriften).

Unterstützt:
- TrueType/OpenType (.ttf, .otf): Einlinienschriften speichern ihre Striche
  als «hin und zurück» gefahrene geschlossene Umrisse (TrueType kennt keine
  offenen Linien). Solche Umrisse werden zu einfachen Linien
  zurückgeführt. Echte Umrissschriften bleiben Umrisse (Gravur der Kontur).
- SVG-Schriften (<font><glyph d="…">), z. B. die «Hershey Text»-Schriften
  von Inkscape oder die EMS-Schriften.
- Hershey-Schriften (.jhf).

Format der Bibliothek (wie schriften/einlinien.json, je Datei eine Schrift):
{"name", "cap", "xh", "lic", "quelle", "art": "einlinie"|"umriss",
 "g": {zeichen: [vorschub, [[x, y, x, y, …], …]]}}  – Schrifteinheiten, y nach oben.
"""
from __future__ import annotations

import json
import math
import os
import re
import sys
import unicodedata
from typing import Dict, List, Optional, Tuple

Punkt = Tuple[float, float]


# ---------------------------------------------------------------------------
# Bibliothek
# ---------------------------------------------------------------------------
def ordner() -> str:
    o = os.environ.get("HIRCH_SCHRIFTEN")
    if not o:
        from .orte import konfig
        o = os.path.join(konfig(), "schriften")
    return o


def _slug(name: str) -> str:
    s = unicodedata.normalize("NFKD", name).encode("ascii", "ignore").decode()
    s = re.sub(r"[^A-Za-z0-9]+", "-", s).strip("-").lower()
    return s or "schrift"


def bibliothek() -> Dict[str, Dict]:
    """Alle eigenen Schriften: {Name: Schrift}."""
    aus: Dict[str, Dict] = {}
    o = ordner()
    if not os.path.isdir(o):
        return aus
    for f in sorted(os.listdir(o)):
        if not f.endswith(".json"):
            continue
        try:
            with open(os.path.join(o, f), encoding="utf-8") as fh:
                s = json.load(fh)
            if isinstance(s, dict) and s.get("name") and isinstance(s.get("g"), dict):
                aus[s["name"]] = s
        except (OSError, ValueError):
            continue
    return aus


def ablegen(schrift: Dict) -> str:
    o = ordner()
    os.makedirs(o, exist_ok=True)
    pfad = os.path.join(o, _slug(schrift["name"]) + ".json")
    tmp = pfad + ".neu"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(schrift, fh, ensure_ascii=False, separators=(",", ":"))
    os.replace(tmp, pfad)
    return pfad


def entfernen(name: str) -> bool:
    for n, s in bibliothek().items():
        if n == name:
            os.remove(os.path.join(ordner(), _slug(n) + ".json"))
            return True
    return False


# ---------------------------------------------------------------------------
# Geometrie
# ---------------------------------------------------------------------------
def _bezier(p0, ps, schritte):
    """Bézier (quadratisch oder kubisch) in gleichen t-Schritten – vorwärts
    und rückwärts ergeben dieselben Punkte (wichtig für die Rückführung)."""
    out = []
    for i in range(1, schritte + 1):
        t = i / schritte
        if len(ps) == 2:
            (x1, y1), (x2, y2) = ps
            a, b, c = (1 - t) ** 2, 2 * t * (1 - t), t * t
            out.append((a * p0[0] + b * x1 + c * x2, a * p0[1] + b * y1 + c * y2))
        else:
            (x1, y1), (x2, y2), (x3, y3) = ps
            a, b, c, d = (1 - t) ** 3, 3 * t * (1 - t) ** 2, 3 * t * t * (1 - t), t ** 3
            out.append((a * p0[0] + b * x1 + c * x2 + d * x3, a * p0[1] + b * y1 + c * y2 + d * y3))
    return out


def _schritte(p0, ps, fein):
    pts = [p0] + list(ps)
    lang = sum(math.dist(pts[i], pts[i + 1]) for i in range(len(pts) - 1))
    return max(2, min(64, int(math.ceil(lang / fein))))


def _flaeche(ring: List[Punkt]) -> float:
    return 0.5 * sum(ring[i][0] * ring[i - 1][1] - ring[i - 1][0] * ring[i][1] for i in range(len(ring)))


class _Fang:
    """Punkt → Schlüssel; Punkte näher als «fang» bekommen denselben Schlüssel
    (auch über Rastergrenzen hinweg, anders als einfaches Runden)."""

    def __init__(self, fang: float):
        self.fang = fang
        self.zellen: Dict = {}

    def __call__(self, p):
        cx, cy = int(math.floor(p[0] / self.fang)), int(math.floor(p[1] / self.fang))
        for dx in (0, -1, 1):
            for dy in (0, -1, 1):
                for q, k in self.zellen.get((cx + dx, cy + dy), ()):
                    if abs(q[0] - p[0]) <= self.fang and abs(q[1] - p[1]) <= self.fang:
                        return k
        k = (cx, cy, len(self.zellen.get((cx, cy), ())))
        self.zellen.setdefault((cx, cy), []).append((p, k))
        return k


def zurueckfuehren(umrisse: List[Tuple[List[Punkt], bool]], fang: float,
                   anteil: float = 0.9) -> Tuple[List[List[Punkt]], bool]:
    """Umrisse, die einen Strich hin und zurück fahren, zu offenen Linien
    machen. Liefert (Linien, einlinig). Ist ein Umriss kein Hin-und-zurück
    (echte Fläche), bleibt er geschlossen und einlinig = False."""
    key = _Fang(fang)

    linien: List[List[Punkt]] = []
    alle_einlinig = True
    for pts, zu in umrisse:
        if len(pts) < 2:
            continue
        if not zu:
            linien.append(list(pts))
            continue
        ring = list(pts)
        if math.dist(ring[0], ring[-1]) <= fang:
            ring = ring[:-1]
        n = len(ring)
        if n < 2:
            continue
        # Ein Hin-und-zurück umschliesst keine Fläche: echte Umrisse gleich erkennen
        umfang = sum(math.dist(ring[i], ring[(i + 1) % n]) for i in range(n))
        if abs(_flaeche(ring)) > umfang * fang * 5:
            alle_einlinig = False
            linien.append(ring + [ring[0]])
            continue
        ring = _teilen(ring, fang)
        n = len(ring)
        kanten = {}
        laenge = gepaart = 0.0
        for i in range(n):
            a, b = ring[i], ring[(i + 1) % n]
            ka, kb = key(a), key(b)
            if ka == kb:
                continue
            kanten.setdefault((ka, kb), []).append((a, b))
        for (ka, kb), liste in kanten.items():
            l = sum(math.dist(a, b) for a, b in liste)
            laenge += l
            if (kb, ka) in kanten:
                gepaart += l
        if laenge <= 0:
            continue
        if gepaart / laenge < anteil:
            alle_einlinig = False
            linien.append(ring + [ring[0]])
            continue
        # Ungerichteter Graph aus den Kanten, dann in Linienzüge zerlegen
        nachbarn: Dict = {}
        ort: Dict = {}
        for (ka, kb), liste in kanten.items():
            a, b = liste[0]
            ort.setdefault(ka, a)
            ort.setdefault(kb, b)
            nachbarn.setdefault(ka, set()).add(kb)
            nachbarn.setdefault(kb, set()).add(ka)
        linien.extend(_zuege(nachbarn, ort))
    return linien, alle_einlinig


def _teilen(ring: List[Punkt], fang: float) -> List[Punkt]:
    """Strecken an Punkten des Rings teilen, die auf ihnen liegen: so passen
    Hin- und Rückweg auch zusammen, wenn sie verschieden viele Stützpunkte
    haben (hinauf in einem Zug, zurück mit Halt in der Mitte)."""
    n = len(ring)
    if n > 2000:
        return ring
    out: List[Punkt] = []
    for i in range(n):
        a, b = ring[i], ring[(i + 1) % n]
        out.append(a)
        dx, dy = b[0] - a[0], b[1] - a[1]
        l2 = dx * dx + dy * dy
        if l2 <= 4 * fang * fang:
            continue
        x0, x1 = min(a[0], b[0]) - fang, max(a[0], b[0]) + fang
        y0, y1 = min(a[1], b[1]) - fang, max(a[1], b[1]) + fang
        innen = []
        for p in ring:
            if not (x0 <= p[0] <= x1 and y0 <= p[1] <= y1):
                continue
            t = ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2
            if t <= 1e-6 or t >= 1 - 1e-6:
                continue
            qx, qy = a[0] + t * dx, a[1] + t * dy
            if (p[0] - qx) ** 2 + (p[1] - qy) ** 2 <= fang * fang and \
                    math.dist(p, a) > fang and math.dist(p, b) > fang:
                innen.append((t, p))
        for _t, p in sorted(set(innen)):
            out.append(p)
    return out


def _zuege(nachbarn: Dict, ort: Dict) -> List[List[Punkt]]:
    """Graph in möglichst lange Linienzüge zerlegen (an Verzweigungen enden)."""
    benutzt = set()
    out = []

    def kante(a, b):
        return (a, b) if a <= b else (b, a)

    def gehen(start, nxt):
        zug = [start, nxt]
        benutzt.add(kante(start, nxt))
        vor, jetzt = start, nxt
        while len(nachbarn[jetzt]) == 2:
            weiter = [q for q in nachbarn[jetzt] if q != vor and kante(jetzt, q) not in benutzt]
            if not weiter:
                break
            vor, jetzt = jetzt, weiter[0]
            benutzt.add(kante(vor, jetzt))
            zug.append(jetzt)
        return zug

    # zuerst von Enden und Verzweigungen aus, Enden zuerst
    knoten = sorted(nachbarn, key=lambda k: (len(nachbarn[k]) != 1, len(nachbarn[k]) == 2))
    for k in knoten:
        if len(nachbarn[k]) == 2:
            continue
        for q in sorted(nachbarn[k]):
            if kante(k, q) not in benutzt:
                out.append(gehen(k, q))
    # übrig: geschlossene Ringe ohne Verzweigung (z. B. «O»)
    for k in knoten:
        for q in sorted(nachbarn[k]):
            if kante(k, q) not in benutzt:
                z = gehen(k, q)
                if z[-1] != k and k in nachbarn[z[-1]]:
                    z.append(k)
                    benutzt.add(kante(z[-2], k))
                out.append(z)
    return _gerade_weiter([[ort[k] for k in z] for z in out])


def _gerade_weiter(zuege: List[List[Punkt]], max_knick: float = 50.0) -> List[List[Punkt]]:
    """An Verzweigungen die Züge verbinden, die fast gerade weiterlaufen
    (beim «H» die beiden Hälften eines Balkens): weniger Absetzen beim Fräsen."""
    zuege = [list(z) for z in zuege if len(z) >= 2]

    def key(p):
        return (round(p[0], 3), round(p[1], 3))

    def richtung(z, am_ende):
        a, b = (z[-2], z[-1]) if am_ende else (z[1], z[0])
        return math.atan2(b[1] - a[1], b[0] - a[0])      # Richtung zum Knoten hin

    while True:
        enden: Dict = {}
        for i, z in enumerate(zuege):
            if key(z[0]) == key(z[-1]):
                continue                                  # geschlossener Ring bleibt
            enden.setdefault(key(z[0]), []).append((i, False))
            enden.setdefault(key(z[-1]), []).append((i, True))
        bestes = None
        for knoten, liste in enden.items():
            for x in range(len(liste)):
                for y in range(x + 1, len(liste)):
                    (i, ei), (j, ej) = liste[x], liste[y]
                    if i == j:
                        continue
                    # gerade weiter: Richtung in den Knoten hinein = Gegenrichtung des anderen
                    d = abs((richtung(zuege[i], ei) - richtung(zuege[j], ej) + math.pi) % (2 * math.pi) - math.pi)
                    knick = math.degrees(math.pi - d)
                    if knick <= max_knick and (bestes is None or knick < bestes[0]):
                        bestes = (knick, i, ei, j, ej)
        if bestes is None:
            return zuege
        _k, i, ei, j, ej = bestes
        a = zuege[i] if ei else zuege[i][::-1]            # endet im Knoten
        b = zuege[j][::-1] if ej else zuege[j]            # beginnt im Knoten
        neu = a + b[1:]
        zuege = [z for n, z in enumerate(zuege) if n not in (i, j)] + [neu]


def verbinden(linien: List[List[Punkt]], fang: float) -> List[List[Punkt]]:
    """Einzelne Strecken (z. B. aus DXF: jede Linie für sich) zu
    zusammenhängenden Linienzügen verbinden; doppelte Strecken fallen weg."""
    key = _Fang(fang)
    nachbarn: Dict = {}
    ort: Dict = {}
    for l in linien:
        for a, b in zip(l, l[1:]):
            ka, kb = key(a), key(b)
            if ka == kb:
                continue
            ort.setdefault(ka, a)
            ort.setdefault(kb, b)
            nachbarn.setdefault(ka, set()).add(kb)
            nachbarn.setdefault(kb, set()).add(ka)
    return _zuege(nachbarn, ort)


def _flach(linien: List[List[Punkt]]) -> List[List[float]]:
    out = []
    for l in linien:
        f: List[float] = []
        for x, y in l:
            f.extend((round(x, 2), round(y, 2)))
        if len(f) >= 4:
            out.append(f)
    return out


# Zeichen, die eine Gravurschrift haben soll (der Rest wird mitgenommen, wenn vorhanden)
WICHTIG = ("".join(chr(c) for c in range(32, 127)) + "ÄÖÜäöüßÀÁÂÈÉÊËÌÍÎÏÒÓÔÙÚÛàáâèéêëìíîïòóôùúûçÇñÑ"
           "°±²³µ×÷€£§©®ØøÆæŒœ–—„“”‚‘’«»…·⌀")


def _behalten(cp: int) -> bool:
    ch = chr(cp)
    if ch in WICHTIG:
        return True
    if cp < 32 or 0x7F <= cp < 0xA0:
        return False
    return (cp < 0x0250 or 0x2010 <= cp < 0x2070 or 0x20A0 <= cp < 0x20C0 or 0x2190 <= cp < 0x2200) \
        and unicodedata.category(ch)[0] in "LNPS"


# ---------------------------------------------------------------------------
# TrueType / OpenType
# ---------------------------------------------------------------------------
def _ttf(pfad: str, name: Optional[str]) -> Dict:
    try:
        from fontTools.ttLib import TTFont
        from fontTools.pens.basePen import BasePen
    except ImportError:
        raise RuntimeError("Für TrueType/OpenType wird fontTools gebraucht: "
                           "«sudo apt install python3-fonttools»")

    font = TTFont(pfad, fontNumber=0, lazy=True)
    upm = float(font["head"].unitsPerEm)
    fein = upm / 250.0          # Kurven: Schritt ~0,4 % der Geviertgrösse
    fang = upm / 1000.0
    gs = font.getGlyphSet()
    cmap = font.getBestCmap() or {}

    class Stift(BasePen):
        def __init__(self):
            super().__init__(gs)
            self.umrisse: List[Tuple[List[Punkt], bool]] = []
            self.akt: List[Punkt] = []

        def _moveTo(self, p):
            self.akt = [tuple(p)]

        def _lineTo(self, p):
            self.akt.append(tuple(p))

        def _qCurveToOne(self, p1, p2):
            p0 = self.akt[-1]
            self.akt.extend(_bezier(p0, [p1, p2], _schritte(p0, [p1, p2], fein)))

        def _curveToOne(self, p1, p2, p3):
            p0 = self.akt[-1]
            self.akt.extend(_bezier(p0, [p1, p2, p3], _schritte(p0, [p1, p2, p3], fein)))

        def _closePath(self):
            if len(self.akt) >= 2:
                self.umrisse.append((self.akt, True))
            self.akt = []

        def _endPath(self):
            if len(self.akt) >= 2:
                self.umrisse.append((self.akt, False))
            self.akt = []

    g: Dict[str, list] = {}
    einlinig = 0
    flaechig = 0
    for cp, gname in sorted(cmap.items()):
        if not _behalten(cp) or gname not in gs:
            continue
        glyph = gs[gname]
        stift = Stift()
        glyph.draw(stift)
        linien, ok = zurueckfuehren(stift.umrisse, fang)
        if stift.umrisse:
            if ok:
                einlinig += 1
            else:
                flaechig += 1
        g[chr(cp)] = [round(float(glyph.width), 2), _flach(linien)]

    def hoehe(ch, attr):
        # gemessen am Zeichen (H, x), sonst Angabe der Schrift
        ys = [s[i] for s in g.get(ch, [0, []])[1] for i in range(1, len(s), 2)]
        if ys and max(ys) > 0:
            return max(ys)
        os2 = font["OS/2"] if "OS/2" in font else None
        wert = getattr(os2, attr, 0) if os2 is not None else 0
        return float(wert) if wert and wert > 0 else upm * (0.7 if ch == "H" else 0.5)

    familie = name
    if not familie:
        n = font["name"]
        familie = n.getDebugName(16) or n.getDebugName(1) or os.path.splitext(os.path.basename(pfad))[0]
        stil = n.getDebugName(17) or n.getDebugName(2) or ""
        if stil and stil.lower() not in ("regular", "normal", "book", "roman", "standard"):
            familie += " " + stil
    lic = ""
    try:
        lic = font["name"].getDebugName(0) or ""
    except Exception:
        pass
    return {"name": familie, "cap": round(hoehe("H", "sCapHeight"), 2), "xh": round(hoehe("x", "sxHeight"), 2),
            "lic": lic, "quelle": os.path.basename(pfad),
            "art": "einlinie" if einlinig >= flaechig else "umriss", "g": g}


# ---------------------------------------------------------------------------
# SVG-Schrift
# ---------------------------------------------------------------------------
def _svg(pfad: str, name: Optional[str]) -> Dict:
    from lxml import etree
    import inkex
    from inkex import bezier

    baum = etree.parse(pfad)
    font = next((e for e in baum.iter() if isinstance(e.tag, str) and e.tag.endswith("}font")), None)
    if font is None:
        raise RuntimeError("Die SVG-Datei enthält keine Schrift (<font>).")
    standard_adv = float(font.get("horiz-adv-x") or 500)
    face = next((e for e in font if isinstance(e.tag, str) and e.tag.endswith("font-face")), None)
    upm = float(face.get("units-per-em") or 1000) if face is not None else 1000.0
    familie = name or (face.get("font-family") if face is not None else None) or \
        os.path.splitext(os.path.basename(pfad))[0]
    fang = upm / 1000.0
    g: Dict[str, list] = {}
    for gl in font:
        if not (isinstance(gl.tag, str) and gl.tag.endswith("}glyph")):
            continue
        u = gl.get("unicode")
        if not u or len(u) != 1:
            continue
        adv = float(gl.get("horiz-adv-x") or standard_adv)
        d = gl.get("d") or ""
        umrisse = []
        if d.strip():
            csp = inkex.Path(d).to_absolute().to_superpath()
            bezier.cspsubdiv(csp, upm / 500.0)
            text = inkex.Path(d).to_absolute()
            zu_liste = []
            for seg in text:
                if seg.letter in "Mm":
                    zu_liste.append(False)
                elif seg.letter in "Zz" and zu_liste:
                    zu_liste[-1] = True
            for i, sub in enumerate(csp):
                pts = [(p[1][0], p[1][1]) for p in sub]
                umrisse.append((pts, zu_liste[i] if i < len(zu_liste) else False))
        linien, _ok = zurueckfuehren(umrisse, fang)
        g[u] = [adv, _flach(linien)]

    def hoehe(attr, ch, faktor):
        ys = [s[i] for s in g.get(ch, [0, []])[1] for i in range(1, len(s), 2)]
        if ys and max(ys) > 0:
            return max(ys)
        if face is not None and face.get(attr):
            return float(face.get(attr))
        return upm * faktor

    return {"name": familie, "cap": hoehe("cap-height", "H", 0.7), "xh": hoehe("x-height", "x", 0.5),
            "lic": "", "quelle": os.path.basename(pfad), "art": "einlinie", "g": g}


# ---------------------------------------------------------------------------
# Hershey (.jhf)
# ---------------------------------------------------------------------------
def _jhf(pfad: str, name: Optional[str]) -> Dict:
    with open(pfad, encoding="latin-1") as fh:
        roh = fh.read().replace("\r", "")
    # Einträge können über mehrere Zeilen gehen: nach Länge zusammensetzen
    eintraege = []
    zeilen = roh.split("\n")
    i = 0
    while i < len(zeilen):
        z = zeilen[i]
        if len(z) < 8 or not z[5:8].strip().isdigit():
            i += 1
            continue
        n = int(z[5:8])
        daten = z[8:]
        while len(daten) < 2 * n and i + 1 < len(zeilen):
            i += 1
            daten += zeilen[i]
        eintraege.append(daten[:2 * n])
        i += 1
    g: Dict[str, list] = {}
    for idx, daten in enumerate(eintraege):
        cp = 32 + idx
        if cp > 126:
            break
        links = ord(daten[0]) - ord("R")
        rechts = ord(daten[1]) - ord("R")
        striche, akt = [], []
        for k in range(2, len(daten) - 1, 2):
            if daten[k:k + 2] == " R":
                if len(akt) >= 2:
                    striche.append(akt)
                akt = []
                continue
            x = ord(daten[k]) - ord("R") - links
            y = -(ord(daten[k + 1]) - ord("R"))
            akt.append((float(x), float(y) + 9.0))     # Grundlinie bei Hershey y = 9
        if len(akt) >= 2:
            striche.append(akt)
        g[chr(cp)] = [float(rechts - links), _flach(striche)]
    ys = [s[i] for s in g.get("H", [0, []])[1] for i in range(1, len(s), 2)]
    xs = [s[i] for s in g.get("x", [0, []])[1] for i in range(1, len(s), 2)]
    return {"name": name or os.path.splitext(os.path.basename(pfad))[0], "cap": max(ys) if ys else 21.0,
            "xh": max(xs) if xs else 14.0, "lic": "Hershey (frei)", "quelle": os.path.basename(pfad),
            "art": "einlinie", "g": g}



def _ohne_doppelte(linien: List[List[Punkt]], fang: float) -> List[List[Punkt]]:
    """Offene Linienzüge: an Stützpunkten teilen, doppelt gefahrene Strecken
    nur einmal behalten, wieder zu Zügen verbinden."""
    alle = [p for l in linien for p in l]
    segmente = []
    for l in linien:
        for a, b in zip(l, l[1:]):
            dx, dy = b[0] - a[0], b[1] - a[1]
            l2 = dx * dx + dy * dy
            if l2 <= fang * fang:
                continue
            innen = []
            if len(alle) < 3000:
                x0, x1 = min(a[0], b[0]) - fang, max(a[0], b[0]) + fang
                y0, y1 = min(a[1], b[1]) - fang, max(a[1], b[1]) + fang
                for p in alle:
                    if x0 <= p[0] <= x1 and y0 <= p[1] <= y1:
                        t = ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2
                        if 1e-6 < t < 1 - 1e-6:
                            qx, qy = a[0] + t * dx, a[1] + t * dy
                            if (p[0] - qx) ** 2 + (p[1] - qy) ** 2 <= fang * fang and \
                                    math.dist(p, a) > fang and math.dist(p, b) > fang:
                                innen.append((t, p))
            kette = [a] + [p for _t, p in sorted(set(innen))] + [b]
            segmente.append(kette)
    return verbinden(segmente, fang)


# ---------------------------------------------------------------------------
# Zeichentafel: Schrift aus einem Export lernen (z. B. aus einem anderen CAM-Programm als DXF/SVG)
# ---------------------------------------------------------------------------
TAFEL = [
    "A B C D E F G H I J K L M",
    "N O P Q R S T U V W X Y Z",
    "a b c d e f g h i j k l m",
    "n o p q r s t u v w x y z",
    "0 1 2 3 4 5 6 7 8 9 & @",
    "Ä Ö Ü ä ö ü ß é è à ç €",
    ". , : ; ! ? - + = / ( ) %",
    "\" ' * # _ < > [ ] ° $ ø Ø",
]
# Jede Zeile beginnt mit einem «H» als Mass: Grundlinie und Versalhöhe
BEZUG = "H"


def tafel_text() -> str:
    return "\n".join(BEZUG + " " + z for z in TAFEL)


def _gruppieren(intervalle: List[Tuple[float, float]], anzahl: int) -> Optional[List[List[int]]]:
    """Indizes nach überlappenden Intervallen zusammenfassen, dann die
    nächstliegenden Gruppen vereinen, bis es genau «anzahl» sind."""
    if not intervalle:
        return None
    ordnung = sorted(range(len(intervalle)), key=lambda i: intervalle[i][0])
    gruppen: List[List[int]] = []
    grenzen: List[List[float]] = []
    for i in ordnung:
        a, b = intervalle[i]
        if gruppen and a <= grenzen[-1][1]:
            gruppen[-1].append(i)
            grenzen[-1][1] = max(grenzen[-1][1], b)
        else:
            gruppen.append([i])
            grenzen.append([a, b])
    if len(gruppen) < anzahl:
        return None
    while len(gruppen) > anzahl:
        k = min(range(len(gruppen) - 1), key=lambda j: grenzen[j + 1][0] - grenzen[j][1])
        gruppen[k] += gruppen.pop(k + 1)
        grenzen[k][1] = max(grenzen[k][1], grenzen.pop(k + 1)[1])
    return gruppen


def _tafel_linien(pfad: str) -> List[List[Punkt]]:
    """Alle Linien einer SVG- oder DXF-Datei (y nach oben)."""
    import inkex
    from inkex import bezier
    if pfad.lower().endswith(".dxf"):
        import subprocess
        skript = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(inkex.__file__))), "dxf_input.py")
        r = subprocess.run([sys.executable, skript, pfad], capture_output=True, timeout=120)
        if r.returncode != 0 or not r.stdout:
            raise RuntimeError("DXF konnte nicht gelesen werden: " + r.stderr.decode(errors="replace")[-300:])
        svg = inkex.load_svg(r.stdout).getroot()
    else:
        svg = inkex.load_svg(pfad).getroot()
    linien = []
    for el in svg.descendants():
        if not isinstance(el, inkex.ShapeElement) or isinstance(el, (inkex.Group, inkex.TextElement)):
            continue
        if not hasattr(el, "path"):
            continue
        if any(isinstance(a.tag, str) and a.tag.split("}")[-1] in ("defs", "marker", "pattern", "symbol",
                                                                  "clipPath", "mask")
               for a in el.iterancestors()):
            continue
        try:
            path = el.path.to_absolute().transform(el.composed_transform())
        except Exception:
            continue
        csp = path.to_superpath()
        bezier.cspsubdiv(csp, 0.02)
        for sub in csp:
            pts = [(p[1][0], -p[1][1]) for p in sub]
            if len(pts) >= 2:
                linien.append(pts)
    return linien


def tafel_lernen(pfad: str, name: str) -> Dict:
    linien = _tafel_linien(pfad)
    if not linien:
        raise RuntimeError("In der Datei sind keine Linien.")
    zeilen_soll = [[BEZUG] + z.split(" ") for z in TAFEL]
    ys = [(min(p[1] for p in l), max(p[1] for p in l)) for l in linien]
    # Zeilen: von oben nach unten
    zg = _gruppieren([(-b, -a) for a, b in ys], len(zeilen_soll))
    if zg is None:
        raise RuntimeError("Die Zeichentafel hat weniger als %d Zeilen – bitte genau die Vorlage "
                           "verwenden (eine Zeile je Vorlagenzeile, Zeilenabstand mindestens doppelte "
                           "Schrifthöhe)." % len(zeilen_soll))
    zg.sort(key=lambda g: -max(ys[i][1] for i in g))
    roh = {}
    hoehen: List[float] = []
    for zeile, idx in zip(zeilen_soll, zg):
        xs = [(min(p[0] for p in linien[i]), max(p[0] for p in linien[i])) for i in idx]
        cg = _gruppieren(xs, len(zeile))
        if cg is None:
            raise RuntimeError("In der Zeile «%s» sind Zeichen zusammengewachsen oder fehlen – "
                               "bitte die Zeichen mit Leerschlag und die Zeilen mit mindestens doppelter "
                               "Schrifthöhe Abstand schreiben." % " ".join(zeile))
        cg.sort(key=lambda g: min(xs[i][0] for i in g))
        teile = [[linien[idx[i]] for i in g] for g in cg]
        bezug = teile[0]
        grundlinie = min(p[1] for l in bezug for p in l)
        hoehen.append(max(p[1] for l in bezug for p in l) - grundlinie)
        for ch, st in zip(zeile[1:], teile[1:]):
            roh[ch] = [[(x, y - grundlinie) for x, y in l] for l in st]
    # Grösse: Versalhöhe aus den Grossbuchstaben ohne Überhang
    hoehen.sort()
    cap = hoehen[len(hoehen) // 2]
    xh = sorted(max(p[1] for l in roh[c] for p in l) for c in "xzvwnmu" if c in roh)
    k = 700.0 / cap                           # Einheiten: Versalhöhe = 700
    rand = 0.08 * 700
    g: Dict[str, list] = {" ": [0.32 * 700, []]}
    for ch, st in roh.items():
        x0 = min(p[0] for l in st for p in l)
        x1 = max(p[0] for l in st for p in l)
        striche = [[((x - x0) * k + rand, y * k) for x, y in l] for l in st]
        linien_neu = verbinden(striche, 0.5)
        g[ch] = [round((x1 - x0) * k + 2 * rand, 2), _flach(linien_neu)]
    return {"name": name, "cap": 700.0, "xh": round(xh[len(xh) // 2] * k, 2) if xh else 480.0,
            "lic": "eigene Zeichentafel", "quelle": os.path.basename(pfad), "art": "einlinie", "g": g}


# ---------------------------------------------------------------------------
def _ist_svg_schrift(pfad: str) -> bool:
    with open(pfad, "rb") as fh:
        return b"<font" in fh.read()


def _zusatz_leser() -> Dict:
    """Weitere Schriftformate aus einem Zusatz im Einstellungsordner (hirch_cam/zusatz.py); ohne Zusatz keine."""
    from . import zusatz
    m = zusatz.modul("fremdschriften")
    return getattr(m, "LESER", {}) if m else {}


def einlesen(pfad: str, name: Optional[str] = None) -> Dict:
    endung = os.path.splitext(pfad)[1].lower()
    if endung in (".ttf", ".otf", ".ttc"):
        s = _ttf(pfad, name)
    elif endung == ".svg" and _ist_svg_schrift(pfad):
        s = _svg(pfad, name)
    elif endung in (".svg", ".dxf"):
        s = tafel_lernen(pfad, name or os.path.splitext(os.path.basename(pfad))[0])
    elif endung == ".jhf":
        s = _jhf(pfad, name)
    elif endung in _zusatz_leser():
        s = _zusatz_leser()[endung](pfad, name)
    else:
        raise RuntimeError("Unbekanntes Schriftformat «%s» – möglich sind .ttf, .otf, .svg, .jhf "
                           "und eine Zeichentafel als .dxf oder .svg." % endung)
    if not any(v[1] for v in s["g"].values()):
        raise RuntimeError("In der Datei wurden keine Zeichen gefunden.")
    return s


def zusammenfassung(s: Dict) -> Dict:
    fehlend = [c for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789ÄÖÜäöü"
               if c not in s["g"]]
    return {"name": s["name"], "zeichen": len(s["g"]), "art": s.get("art", "einlinie"),
            "fehlend": "".join(fehlend), "quelle": s.get("quelle", "")}


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    befehl = argv[0] if argv else "liste"
    try:
        if befehl == "import":
            s = einlesen(argv[1], argv[2] if len(argv) > 2 and argv[2] else None)
            ablegen(s)
            erg = {"ok": True, "schrift": zusammenfassung(s)}
            try:                                  # gleich auch fürs Textwerkzeug
                from . import schrift_ttf
                erg["ttf"] = schrift_ttf.abgleichen([s["name"]])
            except Exception as e:
                erg["ttf_fehler"] = str(e)
        elif befehl == "tafel":
            erg = {"ok": True, "tafel": tafel_text()}
        elif befehl == "entfernen":
            erg = {"ok": entfernen(argv[1])}
            try:
                from . import schrift_ttf
                schrift_ttf.entfernen(argv[1])
            except Exception:
                pass
        else:
            erg = {"ok": True}
    except Exception as e:  # Meldung fürs Panel
        erg = {"ok": False, "fehler": str(e)}
    erg["eigene"] = [zusammenfassung(s) for s in bibliothek().values()]
    sys.stdout.write(json.dumps(erg, ensure_ascii=False))
    return 0 if erg.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
