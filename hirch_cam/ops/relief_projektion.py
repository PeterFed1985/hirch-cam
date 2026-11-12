# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Bahnen auf die Oberfläche eines 3D-Modells projizieren («Tiefe gemessen ab: 3D-Modell»).

Eine Gravur, ein V-Carve, eine Kontur (auch Fase), Tasche oder Bohrung wird wie gewohnt
gerechnet – Tiefen ab Z = 0. Danach wird jede Bahn um die Höhe der Modell-Oberseite an ihrer
Stelle verschoben: an jeder Stelle gleich tief unter der Fläche. Lange Stücke werden dafür in
kurze geteilt (0,5 mm), damit die Bahn der gewölbten Fläche folgt.

Bewegungen in der Luft (über der Oberfläche, bis zum Startabstand) folgen der höchsten Stelle
unter dem ganzen Fräser – so streift er beim Anfahren keine Flanke. Bewegungen auf der
Sicherheitshöhe bleiben, wie sie sind.

Ausserhalb des Modells wird ab der Materialoberfläche gemessen (mit Hinweis).
Seite 2: die Unterseite des Modells (gewendet), wie beim 3D-Fräsen.
"""
from __future__ import annotations

import math
from typing import List, Optional

import numpy as np

ARTEN = ("gravur", "vcarve", "kontur", "tasche", "bohren")


class Flaeche:
    """Höhe der Modell-Oberseite (Koordinaten der Seite), bilinear aus einem Raster."""

    def __init__(self, tri: np.ndarray, bx, durchmesser: float, zelle: float = 0.25):
        from scipy import ndimage
        from .. import hirch_relief
        x0, y0, x1, y1 = bx
        while (x1 - x0) / zelle * (y1 - y0) / zelle > 6_000_000:
            zelle *= 1.25
        nx, ny = int(math.ceil((x1 - x0) / zelle)) + 2, int(math.ceil((y1 - y0) / zelle)) + 2
        oben, _u, _n = hirch_relief.hoehenkarten(np.ascontiguousarray(tri, np.float32), x0, y0, zelle, nx, ny)
        self.x0, self.y0, self.zelle = x0, y0, zelle
        self.gilt = np.isfinite(oben)
        # zum Interpolieren ausserhalb mit dem nächsten Wert des Modells füllen (sonst mischt
        # sich am Rand die Materialoberfläche hinein); ausserhalb gilt danach wieder 0
        if self.gilt.any() and not self.gilt.all():
            _d, (iy, ix) = ndimage.distance_transform_edt(~self.gilt, return_indices=True)
            self.h = oben[iy, ix].astype(np.float64)
        else:
            self.h = np.where(self.gilt, oben, 0.0).astype(np.float64)
        # höchste Stelle unter dem ganzen Fräser (für Bewegungen in der Luft)
        k = max(1, int(math.ceil(durchmesser / 2 / zelle)))
        yy, xx = np.mgrid[-k:k + 1, -k:k + 1]
        fuss = (xx * xx + yy * yy) <= k * k
        self.h_max = ndimage.maximum_filter(np.where(self.gilt, self.h, 0.0), footprint=fuss, mode="nearest")
        self.nahe = ndimage.binary_dilation(self.gilt, structure=fuss) if self.gilt.any() else self.gilt

    def _hol(self, feld, x, y):
        fx = (np.asarray(x) - self.x0) / self.zelle
        fy = (np.asarray(y) - self.y0) / self.zelle
        ny, nx = feld.shape
        i = np.clip(np.floor(fx).astype(int), 0, nx - 2)
        j = np.clip(np.floor(fy).astype(int), 0, ny - 2)
        tx, ty = np.clip(fx - i, 0, 1), np.clip(fy - j, 0, 1)
        return ((feld[j, i] * (1 - tx) + feld[j, i + 1] * tx) * (1 - ty) +
                (feld[j + 1, i] * (1 - tx) + feld[j + 1, i + 1] * tx) * ty)

    def hoehe(self, x, y):
        return np.where(self.auf_modell(x, y), self._hol(self.h, x, y), 0.0)

    def hoehe_max(self, x, y):
        # in der Luft: nie tiefer als die Materialoberfläche ausserhalb des Modells
        h = self._hol(self.h_max, x, y)
        return np.where(self.nahe_modell(x, y), h, np.maximum(h, 0.0))

    def nahe_modell(self, x, y):
        return self._maske(self.nahe, x, y)

    def auf_modell(self, x, y):
        return self._maske(self.gilt, x, y)

    def _maske(self, maske, x, y):
        fx = np.round((np.asarray(x) - self.x0) / self.zelle).astype(int)
        fy = np.round((np.asarray(y) - self.y0) / self.zelle).astype(int)
        ny, nx = maske.shape
        ok = (fx >= 0) & (fy >= 0) & (fx < nx) & (fy < ny)
        aus = np.zeros(fx.shape, bool)
        aus[ok] = maske[fy[ok], fx[ok]]
        return aus


def projizieren(tp, auftrag, seite: int, freifahrhoehe: float, startabstand: float,
                durchmesser: float, schritt: float = 0.5) -> List[str]:
    """Verschiebt die Bewegungen von tp (an Ort und Stelle). Rückgabe: Hinweise."""
    from ..model import Move
    from .relief3d import _modelle
    warnungen: List[str] = []
    if not tp.moves:
        return warnungen
    flaeche = auftrag.get("material_flaeche") or [300.0, 200.0]
    dicke = float((auftrag.get("hoehen") or {}).get("materialdicke") or 18.0)
    material = (float(flaeche[0]), float(flaeche[1]), dicke)
    w: List[str] = []
    tri, _extra, _g = _modelle(auftrag, {}, seite, material, 0.0, w)
    if tri is None:
        warnungen.append("Tiefe ab 3D-Modell: auf dieser Seite liegt kein 3D-Modell – gemessen ab der "
                         "Materialoberfläche.")
        return warnungen
    xs = np.array([m.x for m in tp.moves])
    ys = np.array([m.y for m in tp.moves])
    r = durchmesser / 2 + 1
    bx = (float(xs.min()) - r, float(ys.min()) - r, float(xs.max()) + r, float(ys.max()) + r)
    f = Flaeche(tri, bx, durchmesser)
    luft = startabstand + 1e-6               # bis hier: nahe der Oberfläche → mitverschieben
    neu = []
    ausserhalb = 0
    prev = None
    for m in tp.moves:
        if m.z > luft:                        # Sicherheitshöhe: bleibt
            neu.append(Move(m.rapid, m.x, m.y, m.z, m.feed))
            prev = m
            continue
        pts = [(m.x, m.y, m.z)]
        if prev is not None and prev.z <= luft:
            L = math.hypot(m.x - prev.x, m.y - prev.y)
            n = int(math.ceil(L / schritt))
            if n > 1:
                t = np.linspace(0, 1, n + 1)[1:]
                pts = list(zip(prev.x + (m.x - prev.x) * t, prev.y + (m.y - prev.y) * t,
                               prev.z + (m.z - prev.z) * t))
        px = np.array([q[0] for q in pts])
        py = np.array([q[1] for q in pts])
        pz = np.array([q[2] for q in pts])
        schneidet = pz <= 1e-6
        h = np.where(schneidet, f.hoehe(px, py), f.hoehe_max(px, py))
        if schneidet.any():
            ausserhalb += int((~f.auf_modell(px[schneidet], py[schneidet])).sum())
        for x, y, z, hh in zip(px, py, pz, h):
            neu.append(Move(m.rapid, float(x), float(y), float(z + hh), m.feed))
        prev = m
    tp.moves = neu
    if ausserhalb:
        warnungen.append("Ein Teil der Bahnen liegt ausserhalb des 3D-Modells – dort ist die Tiefe ab der "
                         "Materialoberfläche gemessen.")
    return warnungen


# ------------------------------------------------------------------ «Tiefe gemessen ab: Haut» (B-15, Peter 01.11.)
HAUT_ZUGABE = 0.3      # mm: so viel tiefer als die Haut dick ist – sicher durch, darunter ist Luft (andere Seite)
HAUT_FEHLT = ("Tiefe ab Haut: hier gibt es keine Haut (zweiseitiges 3D-Modell mit «Haut statt durchfräsen») – "
              "die Kontur ist ausgelassen.")
HAUT_AUSSERHALB = ("Die Kontur liegt teilweise ausserhalb der Haut um das 3D-Modell – dort steht volles Material. "
                   "Linie näher an den Umriss legen (Vektoren aus dem Modell › Umriss).")


class _Haut:
    """Die Haut eines zweiseitigen 3D-Modells (relief_zweiseitig: «Haut statt durchfräsen»), von einer Seite aus:
    Höhe ihrer Fläche auf dieser Seite (wo das 3D-Fräsen dieser Seite aufhört, Koordinaten der Seite) und ihre Dicke
    (bis zur Fläche der anderen Seite)."""

    def __init__(self, auftrag, seite: int, bx, durchmesser: float, warnungen: List[str]):
        from .relief3d import _modelle
        from . import relief_zweiseitig as zs
        flaeche = auftrag.get("material_flaeche") or [300.0, 200.0]
        dicke = float((auftrag.get("hoehen") or {}).get("materialdicke") or 18.0)
        self.material = (float(flaeche[0]), float(flaeche[1]), dicke)
        zw = auftrag.get("zweiseitig") or {}
        self.achse = "y" if zw.get("achse") == "y" else "x"
        self.seite = int(seite)
        self.ok = False
        reich = durchmesser + 4.0
        w: List[str] = []
        _tri, extra, geos = _modelle(auftrag, {}, self.seite, self.material, reich, w)
        if extra is None or not geos or not any(g.haut > 0 for g in geos):
            warnungen.append(HAUT_FEHLT)
            return
        self.hier = Flaeche(extra, bx, durchmesser)
        andere = 2 if self.seite == 1 else 1
        _t2, extra2, _g2 = _modelle(auftrag, {}, andere, self.material, reich, w)
        if extra2 is None:
            warnungen.append(HAUT_FEHLT)
            return
        ecken = zs.wenden_punkte([(bx[0], bx[1]), (bx[2], bx[3])], self.material, self.achse)
        bx2 = (min(e[0] for e in ecken), min(e[1] for e in ecken), max(e[0] for e in ecken),
               max(e[1] for e in ecken))
        self.dort = Flaeche(extra2, bx2, durchmesser)
        self.ok = True

    def _dort(self, x, y):
        from . import relief_zweiseitig as zs
        pts = zs.wenden_punkte(list(zip(np.atleast_1d(x), np.atleast_1d(y))), self.material, self.achse)
        return np.array([p[0] for p in pts]), np.array([p[1] for p in pts])

    def oben(self, x, y):
        """Fläche der Haut auf dieser Seite (Z der Seite)."""
        return self.hier._hol(self.hier.h, x, y)

    def dicke(self, x, y):
        x2, y2 = self._dort(x, y)
        z_dort = self.dort._hol(self.dort.h, x2, y2)            # Fläche der anderen Seite, ihre Koordinaten
        return self.oben(x, y) - (-self.material[2] - z_dort)   # … in Koordinaten dieser Seite

    def auf_haut(self, x, y):
        x2, y2 = self._dort(x, y)
        return self.hier.auf_modell(x, y) & self.dort.auf_modell(x2, y2)


_HAUT_SPEICHER: "OrderedDict" = None


def _haut_holen(auftrag, seite: int, bx, durchmesser: float, warnungen: List[str]) -> "_Haut":
    """_Haut, im Prozess gemerkt (B-28: ein Ein/Aus einer Tasche davor rechnete die ganze Haut zweimal neu).
    Schlüssel: Modelle, Zweiseitig, Material, Seite, genau derselbe Bereich, Fräser – das Raster der Haut hängt am
    Bereich; nur ein gleicher Bereich gibt dieselbe Bahn (ein auf 20 mm erweiterter wich bis 0,04 mm ab, Basis 05.11.)."""
    global _HAUT_SPEICHER
    import hashlib
    import json
    from collections import OrderedDict
    if _HAUT_SPEICHER is None:
        _HAUT_SPEICHER = OrderedDict()
    bx = tuple(float(v) for v in bx)
    teile = {k: auftrag.get(k) for k in ("reliefs", "zweiseitig", "material_flaeche")}
    teile["dicke"] = (auftrag.get("hoehen") or {}).get("materialdicke")
    schl = (hashlib.sha1(json.dumps(teile, sort_keys=True, default=str).encode()).hexdigest(), int(seite), bx,
            round(float(durchmesser), 4))
    if schl in _HAUT_SPEICHER:
        h, w = _HAUT_SPEICHER[schl]
        _HAUT_SPEICHER.move_to_end(schl)
        warnungen.extend(w)
        return h
    w: List[str] = []
    h = _Haut(auftrag, seite, bx, durchmesser, w)
    warnungen.extend(w)
    _HAUT_SPEICHER[schl] = (h, list(w))
    while len(_HAUT_SPEICHER) > 4:
        _HAUT_SPEICHER.popitem(last=False)
    return h


def haut_tiefe(auftrag, seite: int, punkte, durchmesser: float, warnungen: List[str], zugabe: float = HAUT_ZUGABE,
               aus: Optional[dict] = None) -> Optional[float]:
    """Tiefe für eine Kontur «ab Haut»: grösste Dicke der Haut an den Punkten der Fräsbahn (Mitte des Fräsers) +
    zugabe. None: keine Haut (oder die Bahn liegt ganz ausserhalb). aus["oben"]: tiefste Fläche der Haut (Z)."""
    if not punkte:
        return None
    xs, ys = np.array([p[0] for p in punkte]), np.array([p[1] for p in punkte])
    r = durchmesser / 2 + 2
    h = _haut_holen(auftrag, seite, (xs.min() - r, ys.min() - r, xs.max() + r, ys.max() + r), durchmesser, warnungen)
    if not h.ok:
        return None
    auf = h.auf_haut(xs, ys)
    if not auf.all():
        warnungen.append(HAUT_AUSSERHALB)
    if not auf.any():
        return None
    if aus is not None:
        aus["oben"] = float(np.min(h.oben(xs[auf], ys[auf])))
    return round(float(np.max(h.dicke(xs[auf], ys[auf]))) + float(zugabe), 3)


def auf_haut(tp, auftrag, seite: int, startabstand: float, durchmesser: float, schritt: float = 0.5,
             ueber: float = 0.0, zustellung: Optional[float] = None, freifahrhoehe: Optional[float] = None) -> List[str]:
    """Wie projizieren, aber auf die Fläche der Haut dieser Seite (Kontur «Tiefe ab Haut»): die Bahn ist ab Z = 0
    gerechnet und wird an jeder Stelle um die Höhe der Haut (+ «ueber») verschoben – sie folgt einer schiefen
    Trennfläche. Mit «zustellung» (Peter 02.11.): darüber dieselbe Ebene im Abstand der Zustellung, von der
    Materialoberfläche an hinunter – auch wo das 3D-Fräsen am Umriss nicht ganz frei gemacht hat."""
    from ..model import Move
    warnungen: List[str] = []
    if not tp.moves:
        return warnungen
    xs = np.array([m.x for m in tp.moves])
    ys = np.array([m.y for m in tp.moves])
    r = durchmesser / 2 + 2
    h = _haut_holen(auftrag, seite, (xs.min() - r, ys.min() - r, xs.max() + r, ys.max() + r), durchmesser, warnungen)
    if not h.ok:
        tp.moves = []
        return warnungen
    f = h.hier
    luft = startabstand + 1e-6
    neu, prev = [], None
    for m in tp.moves:
        if m.z > luft:
            neu.append(Move(m.rapid, m.x, m.y, m.z, m.feed))
            prev = m
            continue
        pts = [(m.x, m.y, m.z)]
        if prev is not None and prev.z <= luft:
            L = math.hypot(m.x - prev.x, m.y - prev.y)
            n = int(math.ceil(L / schritt))
            if n > 1:
                t = np.linspace(0, 1, n + 1)[1:]
                pts = list(zip(prev.x + (m.x - prev.x) * t, prev.y + (m.y - prev.y) * t,
                               prev.z + (m.z - prev.z) * t))
        px = np.array([q[0] for q in pts])
        py = np.array([q[1] for q in pts])
        pz = np.array([q[2] for q in pts])
        # schneiden: auf der Haut; in der Luft: über der höchsten Stelle unter dem ganzen Fräser
        hh = np.where(pz <= 1e-6, f._hol(f.h, px, py), f._hol(f.h_max, px, py)) + float(ueber)
        for x, y, z, d in zip(px, py, pz, hh):
            neu.append((Move(m.rapid, float(x), float(y), float(z + d), m.feed), float(d)))
        prev = m
    if not zustellung or freifahrhoehe is None:
        tp.moves = [m if isinstance(m, Move) else m[0] for m in neu]
        return warnungen
    tp.moves = _haut_ebenen(neu, float(zustellung), float(startabstand), float(freifahrhoehe), float(ueber))
    return warnungen


def _haut_ebenen(neu, zust: float, sz: float, cz: float, ueber: float):
    """Ebenen über der untersten (auf der Haut) im Abstand «zust», von oben nach unten. neu: Bewegungen in der Luft
    (Move, unverändert) und verschobene (Move, Verschiebung d). Eilgänge einer Ebene nie tiefer als der Startabstand
    über dem, was die Ebene darüber schon frei gemacht hat (oben: über der Materialoberfläche)."""
    from ..model import Move
    zust = max(0.05, zust)
    schnitt = [q[0].z for q in neu if isinstance(q, tuple) and not q[0].rapid]
    if not schnitt:
        return [q if isinstance(q, Move) else q[0] for q in neu]
    tiefste = min(schnitt)
    k_max = max(0, int(math.ceil(-tiefste / zust - 1e-6)) - 1)
    aus: List = []
    for k in range(k_max, -1, -1):
        ebene, schneidet = [], False
        for q in neu:
            if isinstance(q, Move):
                ebene.append(Move(q.rapid, q.x, q.y, q.z, q.feed))
                continue
            m, d = q
            z = m.z + k * zust
            if m.rapid:
                # frei bis zum Boden der Ebene darüber; zur Sicherheit von der Fläche der Haut (d − ueber) aus
                # gerechnet, der Boden liegt noch tiefer
                frei = min(0.0, d - ueber + (k + 1) * zust) if k < k_max else 0.0
                z = max(z, frei + sz)
            elif z < -1e-6:
                schneidet = True
            ebene.append(Move(m.rapid, m.x, m.y, z, m.feed))
        if not schneidet:
            continue
        if aus:
            aus.append(Move(True, aus[-1].x, aus[-1].y, cz))
            aus.append(Move(True, ebene[0].x, ebene[0].y, cz))
        aus += ebene
    return aus


# ---------------------------------------------- «Tiefe gemessen ab: bis auf das 3D-Modell» (Peter 01.11., Servoschacht)
BIS_KEIN_MODELL = ("Bis auf das 3D-Modell: auf dieser Seite liegt kein 3D-Modell – die Bearbeitung ist ausgelassen.")
BIS_SEITLICH = 0.01    # mm: beim Aufsetzen ist der Fräser je Seite so viel schmaler – an einer senkrechten Wand, an
                       # der die gezeichnete Linie liegt, hängt er sonst an der Kante und fährt nicht bis zum Boden


def bis_modell(tp, auftrag, seite: int, werkzeug, zustellung: float, tiefe_max: float, versatz: float,
               aufmass: float, startabstand: float, freifahrhoehe: float, schritt: float = 0.5,
               start: float = 0.0) -> List[str]:
    """Tasche/Kontur «bis auf das 3D-Modell» (Peter 01.11.): tp ist EINE Ebene der 2D-Bahn (bei −zustellung, Eintauchen
    von 0 an). Jede Stelle wird mit dem ganzen Fräser (alle Formen, ops/relief3d._abtaster) auf das Modell dieser Seite
    gesetzt – mit Aufmass (auch negativ) und «versatz» (+ tiefer, − weniger tief als die Fläche), nie tiefer als
    tiefe_max. Darüber dieselbe Ebene im Abstand «zustellung», von oben nach unten, bis die oberste über dem Material
    liegt. Ebenen, die ganz in der Luft lägen, fallen weg; ebenso Ebenen, die nirgends tiefer als «start» schneiden
    (Peter 03.11.: «Beginnt bei Tiefe», wenn darüber schon gefräst/geschlichtet ist). Rückgabe: Hinweise."""
    from ..model import Move
    from .relief3d import _abtaster, _modelle
    warnungen: List[str] = []
    if not tp.moves:
        return warnungen
    flaeche = auftrag.get("material_flaeche") or [300.0, 200.0]
    dicke = float((auftrag.get("hoehen") or {}).get("materialdicke") or 18.0)
    material = (float(flaeche[0]), float(flaeche[1]), dicke)
    w_: List[str] = []
    tri, extra, _g = _modelle(auftrag, {}, seite, material, 0.0, w_)
    if tri is None:
        warnungen.append(BIS_KEIN_MODELL)
        tp.moves = []
        return warnungen
    alle = np.ascontiguousarray(np.concatenate([tri] + ([extra] if extra is not None and len(extra) else [])),
                                np.float32)
    zust = max(0.05, float(zustellung))
    grenze = -abs(float(tiefe_max))
    luft = startabstand + 1e-6
    # dichte Punkte der einen Ebene: (rapid, x, y, z_rel, feed) – z_rel = z + zust (0 auf der Ebene), in der Luft None
    pts, prev = [], None
    for m in tp.moves:
        if m.z > luft:
            pts.append((m.rapid, m.x, m.y, None, m.z, m.feed))
            prev = m
            continue
        if prev is not None and prev.z <= luft:
            L = math.hypot(m.x - prev.x, m.y - prev.y)
            n = max(1, int(math.ceil(L / schritt)))
            for t in np.linspace(0, 1, n + 1)[1:]:
                pts.append((m.rapid, prev.x + (m.x - prev.x) * t, prev.y + (m.y - prev.y) * t,
                            prev.z + (m.z - prev.z) * t + zust, None, m.feed))
        else:
            pts.append((m.rapid, m.x, m.y, m.z + zust, None, m.feed))
        prev = m
    unten = [i for i, q in enumerate(pts) if q[3] is not None]
    if not unten:
        return warnungen
    w = dict(werkzeug)
    w["durchmesser"] = max(0.1, float(w.get("durchmesser") or 6) - 2 * BIS_SEITLICH)
    if (w.get("typ") or "") == "kugel":
        w["eckradius"] = w["durchmesser"] / 2
    abtasten = _abtaster(alle, w, float(aufmass or 0.0), grenze - float(aufmass or 0.0) - 1.0)
    xy = np.array([[pts[i][1], pts[i][2]] for i in unten])
    zb = np.asarray(abtasten(xy), float) - float(versatz or 0.0)
    zb = np.maximum(zb, grenze)
    basis = dict(zip(unten, zb))
    schnitt = [basis[i] for i in unten if abs(pts[i][3]) < 1e-6]
    tiefste = min(schnitt) if schnitt else float(zb.min())
    if tiefste >= -1e-6:
        warnungen.append("Bis auf das 3D-Modell: das Modell liegt hier über der Materialoberfläche – nichts zu fräsen.")
        tp.moves = []
        return warnungen
    k_max = max(0, int(math.ceil(-tiefste / zust - 1e-6)) - 1)
    neu = []
    for k in range(k_max, -1, -1):
        ebene = [Move(True, pts[0][1], pts[0][2], freifahrhoehe)] if neu else []
        schneidet = False
        for i, (rapid, x, y, zr, z_alt, feed) in enumerate(pts):
            if zr is None:
                ebene.append(Move(rapid, x, y, z_alt, feed))
                continue
            z = basis[i] + k * zust + zr
            if abs(zr) < 1e-6 and z < -max(0.0, float(start or 0.0)) - 1e-6:
                schneidet = True
            ebene.append(Move(rapid, float(x), float(y), float(z), feed))
        if schneidet:
            if neu:
                neu.append(Move(True, neu[-1].x, neu[-1].y, freifahrhoehe))
            neu += ebene
    tp.moves = neu
    if float(zb.min()) <= grenze + 1e-6:
        warnungen.append("Bis auf das 3D-Modell: an manchen Stellen begrenzt die eingestellte Tiefe – dort bleibt "
                         "Material über dem Modell stehen.")
    return warnungen
