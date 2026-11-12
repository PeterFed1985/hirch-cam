# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""3D-Fräsen auf 3D-Modellen der Zeichnung (STL, OBJ, STEP, Graustufenbild).

Schruppen («relief_schruppen»): Schaftfräser, Z-Ebene für Ebene. Auf jeder Ebene wird alles
ausgeräumt, wo das Modell (plus Aufmass) tiefer liegt – in Runden wie die Tasche, innerhalb
eines Bereichs um das Modell. Die Höhen kommen aus einem Höhenraster des Modells im Material,
die Umrisse je Ebene aus Marching Squares (hirch_relief.konturen).

Schlichten («relief_schlichten»): meist Kugelfräser, parallele Bahnen (entlang X, Y oder
beides). Die Höhe an jedem Punkt rechnet der eigene Drop-Cutter (hirch_relief.SchnellAbtaster;
OpenCAMLib nur noch für Bohrer und mit HIRCH_3D_OCL=1)
direkt auf den Dreiecken – ohne Rasterfehler. Gerade Stücke werden zusammengefasst
(Toleranz), damit die Dateien nicht unnötig lang werden.

Koordinaten: Material-mm wie überall (Nullpunkt unten links auf der Oberfläche, Z nach oben,
Z = 0 Oberfläche, −Dicke Unterseite).
"""
from __future__ import annotations

import math
import os
from typing import Dict, List, Optional, Sequence, Tuple

import numpy as np

from .bahn import Bahnbauer


def _gueltig(g, raster: float = 0.0):
    """Fläche gültig machen (sonst bricht GEOS beim Versetzen oder Abziehen ab); nur die Flächenteile. raster > 0:
    zusätzlich auf ein feines Raster legen (fast deckungsgleiche Kanten nach einer Differenz)."""
    if g is None or g.is_empty:
        return g
    if raster > 0:
        from shapely import set_precision
        g = set_precision(g, raster)
    if g.is_valid:
        return g
    from shapely import make_valid
    from shapely.ops import unary_union
    g = make_valid(g)
    if g.geom_type == "GeometryCollection":
        g = unary_union([x for x in g.geoms if x.geom_type in ("Polygon", "MultiPolygon")])
    return g


def _puffer(g, d: float, **kw):
    """g.buffer(d) – GEOS 3.12 bricht bei manchen gültigen Mehrfachflächen mit «unable to assign hole to a shell»
    ab (Zufallstest 3D 07.11., Fall 16); dann jedes Teil für sich versetzen und vereinigen."""
    try:
        return g.buffer(d, **kw)
    except Exception:
        from shapely.ops import unary_union
        teile = list(getattr(g, "geoms", [g]))
        return unary_union([t.buffer(d, **kw) for t in teile])


def ohne_nan_abbruch(f):
    """inkex stellt beim Import numpy auf «invalid → Ausnahme» (np.seterr). GEOS meldet beim runden Versetzen
    treppiger Umrisse ein «invalid value», obwohl das Ergebnis stimmt – im Programm brach dann die ganze
    Rechnung ab (Zufallstest 3D 07.11., Fall 2). In den 3D-Bearbeitungen gilt darum numpys Vorgabe."""
    import functools

    @functools.wraps(f)
    def innen(*a, **k):
        with np.errstate(invalid="ignore"):
            return f(*a, **k)
    return innen

ARTEN = ("relief_schruppen", "relief_schlichten")


# --------------------------------------------------------------------------------------------
# Modell ins Material legen

def _gitter_dreiecke(hk) -> np.ndarray:
    """Höhenraster (Graustufenbild) als Dreiecke der Oberseite."""
    T = hk.oben
    ny, nx = T.shape
    j, i = np.mgrid[0:ny - 1, 0:nx - 1]
    ok = (np.isfinite(T[:-1, :-1]) & np.isfinite(T[:-1, 1:]) & np.isfinite(T[1:, :-1])
          & np.isfinite(T[1:, 1:]))
    i, j = i[ok], j[ok]

    from .relief_zweiseitig import gitter_versatz
    jx, jy = gitter_versatz(T.shape)            # sonst entartet der Suchbaum von OpenCAMLib

    def p(ii, jj):
        return np.stack([hk.x0 + ii * hk.zelle + jx[jj, ii], hk.y0 + jj * hk.zelle + jy[jj, ii],
                         T[jj, ii]], axis=-1)
    a, b, c, d = p(i, j), p(i + 1, j), p(i + 1, j + 1), p(i, j + 1)
    return np.concatenate([np.stack([a, b, c], 1), np.stack([a, c, d], 1)]).astype(np.float32)


def _abbildung(relief: Dict, zmin: float, zmax: float, dicke: float):
    """(Abbildung ausgerichtete Modellkoordinaten → Material, Kennzahlen der Lage)."""
    from .. import relief as rl
    info = relief.get("info") or {}
    v = rl.versatz_aus(zmin, zmax, info.get("lage"), dicke)
    bl = list(info.get("bild_lage") or [0, 0, 1, 1]) + [0, 0, 1, 1]
    mx0, my0, mw, mh = float(bl[0]), float(bl[1]), float(bl[2]) or 1.0, float(bl[3]) or 1.0
    p00, p10, p01 = (np.asarray(relief["ecken"][k], float) for k in range(3))

    def abbilden(pts: np.ndarray) -> np.ndarray:
        pts = np.asarray(pts)
        u = (pts[..., 0].astype(np.float64) - mx0) / mw
        w = (pts[..., 1].astype(np.float64) - my0) / mh
        xy = p00 + u[..., None] * (p10 - p00) + (1.0 - w)[..., None] * (p01 - p00)
        aus = np.empty(pts.shape, np.float64)
        aus[..., 0] = xy[..., 0]
        aus[..., 1] = xy[..., 1]
        aus[..., 2] = pts[..., 2] + v["z_versatz"]
        return aus
    return abbilden, v


BILD_PUNKTE = 2_000_000      # Graustufenbild als Modell: so viele Höhenpunkte höchstens


def _modell_ausgerichtet(relief: Dict):
    """(Modell, Ausrichtung, Dreiecke in ausgerichteten Koordinaten, zmin, zmax)."""
    from .. import relief as rl
    info = relief.get("info") or {}
    m = rl.modell_aus_text(relief.get("daten") or "", str(info.get("name") or "modell.stl"))
    a = rl.ausrichtung(info.get("ausrichtung"))
    if m.ist_bild:
        hk = rl.hoehenkarten(m, a)
        # nicht feiner als nötig: höchstens BILD_PUNKTE (Peter 31.10.: feiner – vorher 750 000; gemessen:
        # Abtasten gleich schnell, Speicher je Rechnung rund 1 GB statt 0,4 GB)
        ny, nx = hk.oben.shape
        if nx * ny > BILD_PUNKTE:
            hk = rl.hoehenkarten(m, a, zelle=hk.zelle * math.sqrt(nx * ny / BILD_PUNKTE))
        tri = _gitter_dreiecke(hk)
        return m, a, tri, 0.0, float(np.nanmax(hk.oben))
    tri = rl.ausgerichtet(m, a)
    return m, a, tri, float(tri[..., 2].min()), float(tri[..., 2].max())


def modell_im_material(relief: Dict, dicke: float) -> Tuple[np.ndarray, Dict]:
    """Dreiecke des Modells in Materialkoordinaten und Kennzahlen (z_versatz …)."""
    _m, _a, tri, zmin, zmax = _modell_ausgerichtet(relief)
    abbilden, v = _abbildung(relief, zmin, zmax, dicke)
    return np.ascontiguousarray(abbilden(tri), np.float32), v


def _modelle(auftrag: Dict, p: Dict, seite: int, material: Tuple[float, float, float],
             reichweite: float, warnungen: List[str]):
    """(Dreiecke des Modells, Zusatz oder None, zweiseitige Geometrien) für eine Seite.
    Einseitig: die Modelle auf dieser Seite der Zeichnung. Zweiseitig (Modell mit
    «Von beiden Seiten fräsen» auf Seite 1, Auftrag zweiseitig): Seite 1 bis zur Trennfläche,
    Seite 2 gewendet von unten – dazu der Zusatz (Boden um das Modell und Haltestege)."""
    from . import relief_zweiseitig as zs
    gewollt = str(p.get("relief") or "")
    zw = auftrag.get("zweiseitig") or {}
    zwei_an = bool(zw.get("an"))
    achse = "y" if zw.get("achse") == "y" else "x"
    dicke = material[2]
    einseitig, zweiseitig = [], []
    for r in auftrag.get("reliefs") or []:
        if gewollt and r.get("id") != gewollt:
            continue
        cfg = zs.einstellungen(r.get("info"))
        zwei = bool(cfg.get("an")) and int(r.get("seite") or 1) == 1
        if zwei and not zwei_an:
            if seite == 1:
                warnungen.append("Das 3D-Modell soll von beiden Seiten gefräst werden, aber «Zweiseitig» "
                                 "ist ausgeschaltet – es wird nur von oben gefräst.")
            zwei = False
        if not zwei and int(r.get("seite") or 1) != seite:
            continue
        if not r.get("daten"):
            warnungen.append("Die Modelldaten fehlen in der Zeichnung – bitte das Modell neu einlesen.")
            continue
        tri, v = modell_im_material(r, dicke)
        if v["raus_unten"] > 0.005 and not zwei:
            warnungen.append("Unten fehlen %s mm Material – dort wird das Modell abgeschnitten."
                             % _komma(v["raus_unten"]))
        if zwei and (v["raus_unten"] > 0.005 or v["raus_oben"] > 0.005):
            warnungen.append("Das Modell ragt aus dem Material (oben %s mm, unten %s mm) – dort wird "
                             "es abgeschnitten." % (_komma(v["raus_oben"]), _komma(v["raus_unten"])))
        (zweiseitig if zwei else einseitig).append((r, tri, cfg))
    modelle, zusaetze, geos = [], [], []
    for _r, tri, _cfg in einseitig:
        modelle.append(tri)
    if zweiseitig:
        tri = np.concatenate([t for _r, t, _c in zweiseitig])
        stege = []
        for r, _t, _c in zweiseitig:
            for st in r.get("stege") or []:
                e = st["ecken"]
                if int(st.get("seite") or 1) == 2:          # auf Seite 2 gezeichnet
                    e = zs.wenden_punkte(e, material, achse)
                stege.append(dict(st, ecken=e))
        cfg = dict(zweiseitig[0][2], _ueber=zs.ueber_trennlinie(zweiseitig[0][2], seite, auftrag))
        if cfg.get("trennung") == "ebene":
            # Peter 03.11. (Fluegel_Fehler.hir): «auf einer Ebene» ohne Höhe = Z 0, die Oberfläche – Seite 1 fräste
            # nur noch das Modell. Liegt die Ebene nicht im Modell, sagen
            zt = float(cfg.get("z") or 0.0)
            z_hi, z_lo = float(tri[..., 2].max()), float(tri[..., 2].min())
            if zt >= z_hi - 0.5 or zt <= z_lo + 0.5:
                warnungen.append("Die Seiten treffen sich auf einer Ebene bei Z %s mm – das liegt nicht im Modell "
                                 "(Z %s … %s mm). Dann fräst eine Seite neben dem Modell nichts. Im Fenster «3D-Modell» "
                                 "«Seiten treffen sich» auf «an der Trennlinie des Modells» stellen oder die Höhe "
                                 "der Ebene anpassen." % (_komma(zt), _komma(z_lo), _komma(z_hi)))
        g = zs.geometrie(tri, stege, cfg, seite, material, achse, reichweite)
        modelle.append(g.modell)
        if g.zusatz is not None and len(g.zusatz):
            zusaetze.append(g.zusatz)
        geos.append(g)
    if not modelle:
        return None, None, geos
    tri = np.ascontiguousarray(np.concatenate(modelle), np.float32)
    extra = np.ascontiguousarray(np.concatenate(zusaetze), np.float32) if zusaetze else None
    return tri, extra, geos


def _zweiseitig_pruefen(geos, auftrag: Dict, seite: int, spalt: float, material, warnungen,
                        fraeser_r: float = 0.0):
    """Hinweise: keine Stege, Steg zu kurz, Passloch im Fräsbereich. spalt: Breite des Fräsbereichs um
    das Modell; fraeser_r: was der Fräser darüber hinaus schneidet (Schlichten: Mitte bis spalt)."""
    from shapely.geometry import Point, Polygon
    for g in geos:
        if g.umriss is None or g.umriss.is_empty:
            continue
        band = g.umriss.buffer(spalt, resolution=8)
        if seite == 2 and not g.stege and not g.haut:
            warnungen.append("Keine Haltestege – das Teil löst sich beim Fräsen von Seite 2 aus dem "
                             "Material. Stege im Fenster «3D-Modell» setzen.")
        for i, st in enumerate(g.stege):
            poly = Polygon([tuple(q) for q in st["ecken"]])
            if poly.is_valid and poly.difference(band).area < 0.5:
                warnungen.append("Haltesteg %d reicht nicht über den Fräsbereich hinaus (%s mm um das "
                                 "Modell) – das Teil hätte dort keinen Halt. Steg länger ziehen."
                                 % (i + 1, _komma(spalt)))
            if poly.is_valid and not poly.intersects(g.umriss):
                warnungen.append("Haltesteg %d berührt das Modell nicht." % (i + 1))
        zw = dict(auftrag.get("zweiseitig") or {})
        if seite == 1 and zw.get("passloecher", True):
            from ..einstellungen import passloecher
            r = float(zw.get("pass_d") or 6.0) / 2
            # um die Passlöcher wird nicht gefräst (passstifte.py: Lochradius + Fräserradius + 1 mm)
            schnitt = g.umriss.buffer(spalt + fraeser_r, resolution=8)
            for x, y in passloecher({"zweiseitig": zw}, material[0], material[1]):
                if Point(x, y).buffer(r + 1.0).intersects(schnitt):
                    warnungen.append("Ein Passloch liegt im Fräsbereich um das 3D-Modell – dort wird nicht "
                                     "gefräst, es bleibt eine Insel stehen. Modell verschieben oder den "
                                     "Abstand der Passlöcher ändern.")
                    break


def _komma(v: float) -> str:
    return ("%.2f" % v).replace(".", ",")


# --------------------------------------------------------------------------------------------
# Raster und Umrisse

def _raster(tri: np.ndarray, bereich: Tuple[float, float, float, float], zelle: float,
            max_punkte: int = 8_000_000):
    from .. import hirch_relief
    x0, y0, x1, y1 = bereich
    while (x1 - x0) / zelle * (y1 - y0) / zelle > max_punkte:
        zelle *= 1.25
    nx, ny = int(math.ceil((x1 - x0) / zelle)) + 1, int(math.ceil((y1 - y0) / zelle)) + 1
    oben, unten, _anz = hirch_relief.hoehenkarten(tri, x0, y0, zelle, nx, ny)
    return oben, unten, zelle


def _flaechen_aus(raster: np.ndarray, schwelle: float, x0: float, y0: float, zelle: float, tol: float):
    """Fläche (shapely) aller Stellen mit raster > schwelle."""
    from shapely.geometry import Polygon
    from shapely.ops import unary_union
    from .. import hirch_relief
    from .tasche import _flaechen
    ringe = [r for r in hirch_relief.konturen(np.ascontiguousarray(raster, np.float32), schwelle,
                                              x0, y0, zelle) if len(r) >= 3]
    if not ringe:
        return Polygon()
    g = _flaechen([[tuple(q) for q in r] for r in ringe])
    if g is None:
        return Polygon()
    g = g.buffer(0).simplify(max(tol, zelle * 0.25))
    return unary_union([g])


def umriss_modell(tri: np.ndarray, maske: np.ndarray, x0: float, y0: float, zelle: float, tol: float):
    """Umriss des Modells in der Draufsicht (shapely) genau aus den Dreiecken (Peter 04.11.: der Umriss aus
    dem Raster hatte an schrägen Kanten Treppen von einer Rasterzelle – die Bahn «Boden nachfahren» fuhr sie
    nach, die Maschine ruckelte). Ohne shapely 2 / scipy oder bei einem Fehler: wie bisher aus dem Raster."""
    from types import SimpleNamespace
    from shapely.geometry import Polygon
    from shapely.ops import unary_union
    try:
        from .relief_vektoren import _umriss_genau
        ringe = _umriss_genau(tri, SimpleNamespace(maske=maske, x0=x0, y0=y0, zelle=zelle), tol)
    except Exception:
        ringe = None
    if ringe:
        from .tasche import _flaechen
        g = _flaechen([[tuple(q) for q in r] for r in ringe])
        if g is not None and not g.is_empty:
            g = unary_union([g.buffer(0)])
            # Plausibel? Die Fläche weicht vom Raster höchstens um einen Saum von einer Zelle ab
            ras = _flaechen_aus(maske.astype(np.float32), 0.5, x0, y0, zelle, tol)
            if ras.is_empty or abs(g.area - ras.area) <= ras.length * zelle * 1.5 + 1.0:
                return g
    return _flaechen_aus(maske.astype(np.float32), 0.5, x0, y0, zelle, tol)


def _versetzt(geom, d: float, tol: float):
    from .. import cam
    if cam.verfuegbar():
        return cam.puffer(geom, d, tol)
    return geom.buffer(d, resolution=16, join_style=1)


BEREICH_FLAECHEN = {"flaechen": "innen", "ohne_flaechen": "aussen"}


def begrenzung(p: Dict):
    """Bereich aus den Linien der Bearbeitung («Bereich»): (Fläche, «innen»|«aussen») oder None.
    Die Linien gehören über ihre Farbe oder hirch:auch zur 3D-Bearbeitung (einstellungen.auftrag →
    «bereich_objekte»); der Überlauf greift über die Linie hinaus, damit am Übergang keine Kante
    stehen bleibt. «flaechen» / «ohne_flaechen» (Peter, 22.10.): nur die Linien aus «Gewählte
    Flächen als Bereich» (hirch:flaechen, Reiter «3D»), sonst wie «innen» / «aussen»; dort zählen
    umgekehrt nur die übrigen Linien."""
    art = p.get("bereich") or "alles"
    if art not in ("innen", "aussen") and art not in BEREICH_FLAECHEN:
        return None
    nur_flaechen = art in BEREICH_FLAECHEN
    art = BEREICH_FLAECHEN.get(art, art)
    from shapely.geometry import Polygon
    from shapely.ops import unary_union
    flaechen = []
    for o in p.get("bereich_objekte") or []:
        if bool(o.get("flaechen")) != nur_flaechen:
            continue
        pts = [tuple(q[:2]) for q in o.get("punkte") or []]
        if len(pts) >= 3 and o.get("geschlossen", True):
            g = Polygon(pts)
            g = g if g.is_valid else g.buffer(0)
            if not g.is_empty and g.area > 1e-6:
                flaechen.append(g)
    if not flaechen:
        return None
    ueber = max(0.0, float(p.get("bereich_ueberlauf") if p.get("bereich_ueberlauf") not in (None, "") else 1.0))
    # Linien in Linien (Loch in einer Fläche) wie bei der Tasche: gerade Anzahl = draussen
    flaeche = flaechen[0]
    for g in flaechen[1:]:
        flaeche = flaeche.symmetric_difference(g)
    if art == "innen":
        return flaeche.buffer(ueber, resolution=8), art
    return (flaeche.buffer(-ueber, resolution=8) if ueber > 0 else flaeche), art


def _begrenzen(bereich, p: Dict, warnungen: List[str]):
    """Fräsbereich auf die Linien der Bearbeitung beschränken (siehe begrenzung)."""
    b = begrenzung(p)
    if b is None:
        art = p.get("bereich") or "alles"
        if art in ("innen", "aussen"):
            warnungen.append("Bereich «%s» gewählt, aber keine geschlossenen Linien zugeordnet – das ganze "
                             "Modell wird bearbeitet." % ("nur innerhalb der Linien" if art == "innen"
                                                          else "ausser innerhalb der Linien"))
        elif art in BEREICH_FLAECHEN:
            warnungen.append("Bereich «%s» gewählt, aber keine Flächen zugeordnet – das ganze Modell wird "
                             "bearbeitet. In der 3D-Ansicht mit Rechtsklick aufs Modell Flächen wählen und «Gewählte Flächen als Bereich» "
                             "drücken." % ("nur gewählte Flächen" if art == "flaechen" else "ausser gewählten Flächen"))
        return bereich
    flaeche, art = b
    neu = bereich.intersection(flaeche) if art == "innen" else bereich.difference(flaeche)
    if neu.is_empty:
        warnungen.append("Die Linien des Bereichs treffen das 3D-Modell nicht – nichts zu fräsen.")
    return neu


def _werkzeug_form(w: Dict) -> Dict:
    return {"typ": w.get("typ") or "schaft", "durchmesser": float(w.get("durchmesser") or 6),
            "eckradius": float(w.get("eckradius") or 0), "v_winkel": float(w.get("v_winkel") or 60),
            "spitze": float(w.get("spitze") or 0)}


def _huelle(alle, w: Dict, boden: float, drin, gx, gy, xs, bx, zelle: float, material) -> np.ndarray:
    """Was der Fräser w bestenfalls erreicht, wenn seine Mitte jede Zelle in «drin» besucht: an jeder
    Zelle abgetastet, Zeile um Zeile abgetragen, je zusammenhängendes Stück. Ausserhalb 0 (Oberfläche)."""
    from .. import hirch_relief
    ny, nx = drin.shape
    E = np.zeros((ny, nx), np.float32)
    if not drin.any():
        return E
    abtasten = _abtaster(alle, w, 0.0, boden)
    z = np.full((ny, nx), np.nan)
    z[drin] = abtasten(np.column_stack([gx[drin], gy[drin]]))
    form = _werkzeug_form(w)
    for j in range(ny):
        zeile = drin[j]
        if not zeile.any():
            continue
        kanten = np.flatnonzero(np.diff(np.concatenate([[0], zeile.astype(np.int8), [0]])))
        for a, e in zip(kanten[::2], kanten[1::2]):
            pts = np.column_stack([xs[a:e], gy[j, a:e], z[j, a:e]])
            if len(pts) == 1:
                pts = np.vstack([pts, pts])
            hirch_relief.abtragen(E, bx[0], bx[1], zelle, np.ascontiguousarray(pts),
                                  form, [pts[0, 0], pts[0, 1], 50.0], -float(material[2]))
    return E


def _restflaeche(tri: np.ndarray, extra: Optional[np.ndarray], material, bx, vorher, aufmass: float,
                 schwelle: float, w: Dict, tol: float, bereich=None, randstreifen: float = 0.0):
    """Wo nach den vorherigen Bahnen (vorher: [(Werkzeugweg, Werkzeug)]) noch mehr als «schwelle»
    über dem steht, was dieser Fräser (w) erreichen kann (Modell + Aufmass, Zusatz ohne Aufmass,
    daneben die tiefste Stelle – Kehlen, in die er nicht passt, zählen nicht) – als Fläche, um den
    Radius dieses Fräsers erweitert.

    Restmaterial ist nur, was die vorherigen Fräser wegen ihrer Form nicht erreichen konnten (enge
    Rinnen, Kehlen) oder wo sie gar nicht hinkamen – nicht die Rillen zwischen ihren Bahnen: dafür
    zählt je vorheriger Bearbeitung ihre Hülle (Fräser überall in ihrem Gebiet, ohne Bahnabstand)
    plus das Aufmass, das sie stehen liess."""
    from shapely.geometry import Polygon
    from scipy import ndimage
    from .. import hirch_relief
    d = float(w["durchmesser"])
    zelle = max(0.1, min(0.5, d / 8))
    oben, _u, zelle = _raster(tri, bx, zelle, max_punkte=1_500_000)
    ziel = np.where(np.isfinite(oben), oben + aufmass, np.nan)
    if extra is not None and len(extra):
        xr, _xu, _z = _raster(extra, bx, zelle, max_punkte=10 ** 12)
        ziel = np.where(np.isfinite(xr), np.fmax(np.nan_to_num(ziel, nan=-1e9), xr), ziel)
    auf_modell = np.isfinite(ziel)          # nur auf Modell und Zusatz zählt, was stehen bleibt
    if not auf_modell.any():
        return Polygon()
    boden = max(float(np.nanmin(ziel)), -float(material[2]))
    ziel = np.where(auf_modell, ziel, boden).astype(np.float32)
    ny, nx = ziel.shape
    alle = tri if extra is None or not len(extra) else np.concatenate([tri, extra])
    xs = bx[0] + np.arange(nx) * zelle
    gx, gy = np.meshgrid(xs, bx[1] + np.arange(ny) * zelle)
    H = np.zeros((ny, nx), np.float32)          # tatsächlich stehen geblieben (mit Rillen)
    huelle = np.full((ny, nx), np.inf, np.float32)   # bestenfalls stehen geblieben (ohne Rillen)
    for tp, wv in vorher or []:
        pts = np.array([(m.x, m.y, m.z) for m in tp.moves], np.float64).reshape(-1, 3)
        if not len(pts):
            continue
        Hi = np.zeros((ny, nx), np.float32)
        hirch_relief.abtragen(Hi, bx[0], bx[1], zelle, pts, _werkzeug_form(wv),
                              [pts[0, 0], pts[0, 1], 50.0], -float(material[2]))
        H = np.minimum(H, Hi)
        # Gebiet dieser Bearbeitung: wo ihr Fräser Material nahm, Lücken zwischen den Bahnen geschlossen
        gebiet = Hi < -1e-3
        if not gebiet.any():
            continue
        if (wv.get("typ") or "schaft") == "schaft":
            # Schaftfräser (Schruppen, Böden und Wände auf Mass): keine Rillen zwischen den Bahnen, die man
            # übersehen müsste – es zählt, was tatsächlich steht (Peter 25.10., Impeller: die Hülle schloss
            # die engen Lücken zwischen den Schaufeln, der Ø 3 fand dort kaum Restmaterial)
            huelle = np.minimum(huelle, Hi)
            continue
        dv = float(wv.get("durchmesser") or 6)
        n_zu, n_ab =max(1, int(np.ceil(dv / zelle))), int(dv / 2 / zelle) - 1
        rand = n_zu + 2                        # am Rand des Rasters weitermachen, nicht abfressen
        g = np.pad(gebiet, rand, mode="edge")
        g = ndimage.binary_closing(g, iterations=n_zu)
        gebiet = g[rand:-rand, rand:-rand]
        # Fussabdruck → wo die Mitte des Fräsers war (um den Radius nach innen)
        mitte = ndimage.binary_erosion(g, iterations=n_ab)[rand:-rand, rand:-rand] if n_ab >= 1 else gebiet
        Ei = _huelle(alle, wv, boden, mitte if mitte.any() else gebiet, gx, gy, xs, bx, zelle, material)
        # Aufmass, das diese Bearbeitung stehen liess (Schruppen): kleinster typischer Abstand über der Hülle
        abst = (Hi - Ei)[gebiet & auf_modell & (Hi < -1e-3)]
        auf_i = float(np.percentile(abst, 10)) if abst.size > 20 else 0.0
        huelle = np.where(gebiet, np.minimum(huelle, Ei + max(0.0, auf_i)), huelle)
    huelle = np.where(np.isfinite(huelle), huelle, H)      # wo keine hinkam: was tatsächlich steht
    if bereich is not None:
        # um einen Bahnabstand nach innen: am Rand des Bereichs liegt die äusserste Bahn bis zu einem
        # Bahnabstand vor der Grenze – der schmale Streifen dahinter ist kein Restmaterial
        import shapely
        geb = _puffer(bereich, -randstreifen) if randstreifen > 0 else bereich
        shapely.prepare(geb)
        drin = shapely.contains_xy(geb, gx, gy)
    else:
        drin = np.ones((ny, nx), bool)
    E = np.fmax(_huelle(alle, w, boden, drin, gx, gy, xs, bx, zelle, material), ziel)
    # massgebend ist die Hülle; was tatsächlich steht, muss auch darüber liegen (Rillen zählen dort mit)
    rest = ((huelle - E > schwelle) & (H - E > 0.5 * schwelle) & auf_modell).astype(np.float32)
    if not rest.any():
        return Polygon()
    fl = _flaechen_aus(rest, 0.5, bx[0], bx[1], zelle, tol)
    return fl.buffer(d / 2 + zelle, resolution=8)


def _rest_begrenzen(bereich, p: Dict, tri, extra, material, bx, w: Dict, tol: float, warnungen: List[str]):
    """«Nur Restmaterial»: den Bereich auf die Stellen beschränken, an denen die vorherigen
    3D-Bearbeitungen dieser Seite Material stehen liessen."""
    if not p.get("rest"):
        return bereich
    vorher = p.get("_vorher") or []
    if not vorher:
        warnungen.append("«Nur Restmaterial»: vorher gibt es keine 3D-Bearbeitung auf dieser Seite – "
                         "es wird alles bearbeitet.")
        return bereich
    aufmass = float(p.get("aufmass") or 0.0)
    schwelle = max(0.02, float(p.get("rest_schwelle") or 0.1))
    schritt = float(w["durchmesser"]) * max(1.0, float(p.get("bahnabstand") or 10)) / 100.0
    fl = _restflaeche(tri, extra, material, bx, vorher, aufmass, schwelle, w, tol, bereich, schritt)
    if fl.is_empty:
        warnungen.append("Kein Restmaterial: die vorherigen Bearbeitungen haben überall bis auf %s mm "
                         "gefräst – nichts zu tun." % _komma(schwelle))
        return fl
    return bereich.intersection(fl)


def ueber_rand(p: Dict) -> float:
    """«Über den Materialrand hinaus» (mm): so weit darf die Fräsermitte ausserhalb des Blocks fahren
    (Peter 26.10.: Impeller so gross wie der Block, gespannt mit Schrauben in den Ecken). 0 = bis zur Kante."""
    try:
        return max(0.0, min(50.0, float(p.get("ueber_rand") or 0.0)))
    except (TypeError, ValueError):
        return 0.0


def _material_huelle(material, u: float):
    """Raster-Grenzen: Material, um u über den Rand hinaus"""
    return (-u, -u, material[0] + u, material[1] + u)


def _bx(lo, hi, ext: float, material, u: float = 0.0):
    """Rasterbereich um das Modell (lo, hi: Ecken), im Material bzw. bis u darüber hinaus"""
    h = _material_huelle(material, u)
    return (max(h[0], lo[0] - ext), max(h[1], lo[1] - ext), min(h[2], hi[0] + ext), min(h[3], hi[1] + ext))


def _bereich(umriss, rand: float, material: Tuple[float, float], kreis=None, ueber: float = 0.0):
    """Fräsbereich: Modell + Rand, im Material (bzw. bis «ueber» mm über den Rand hinaus) – bei rundem
    Material (kreis: Mitte x, y, Radius) nur im Kreis (die Ecken der Seite sind Luft)."""
    from shapely.geometry import Point, box
    b = umriss.buffer(rand, resolution=16, join_style=1)
    if kreis:
        return b.intersection(Point(kreis[0], kreis[1]).buffer(kreis[2] + ueber, resolution=64))
    return b.intersection(box(-ueber, -ueber, material[0] + ueber, material[1] + ueber))


# --------------------------------------------------------------------------------------------
# Schruppen

def _flache_hoehen(oben: np.ndarray, maske: np.ndarray, zelle: float, d: float) -> List[float]:
    """Höhen der waagrechten Flächen im Raster des Modells (oben): Zellen mit Neigung unter ~0,1°, die mit allen
    Nachbarn im Modell liegen, nach Höhe gruppiert (0,05 mm); eine Fläche zählt ab der Grösse eines Fräsers.
    Bis 30.10. galt ~1°: auf sanft gewölbten Flächen (Flügel, B-8) zählte dann jedes 0,05-mm-Band nahe dem Scheitel
    als «waagrecht», und das Schruppen bekam dort eine Ebene je 0,05–0,25 mm – die Zustellung schien immer kleiner
    zu werden. Echte Böden aus dem CAD sind im Raster genau eben (Neigung 0)."""
    if not maske.any():
        return []
    h = np.where(maske, oben, 0.0).astype(np.float64)
    innen = maske.copy()
    innen[1:, :] &= maske[:-1, :]
    innen[:-1, :] &= maske[1:, :]
    innen[:, 1:] &= maske[:, :-1]
    innen[:, :-1] &= maske[:, 1:]
    gy, gx = np.gradient(h, zelle)
    eben = innen & (np.hypot(gx, gy) < 0.002)
    if not eben.any():
        return []
    werte = h[eben]
    schluessel = np.round(werte / 0.05).astype(np.int64)
    k, zahl = np.unique(schluessel, return_counts=True)
    min_zellen = max(4, int(math.ceil((d * d) / (zelle * zelle))))
    aus = []
    for kk, n in zip(k, zahl):
        if n >= min_zellen:
            aus.append(float(np.median(werte[schluessel == kk])))
    return aus


class _Schnitte:
    """Genaue waagrechte Schnitte des Modells: welche Fläche (von oben gesehen) liegt höher als z? Aus den nach
    oben zeigenden Dreiecken, an der Ebene abgeschnitten und vereinigt – ohne Raster, für Böden und Wände auf
    Mass. Gemerkt je Höhe."""

    def __init__(self, tri: np.ndarray):
        t = np.asarray(tri, np.float64)
        n = np.cross(t[:, 1] - t[:, 0], t[:, 2] - t[:, 0])
        nz = n[:, 2] / np.maximum(np.linalg.norm(n, axis=1), 1e-12)
        self.t = t[nz > 1e-4]
        self.zmax = self.t[:, :, 2].max(1) if len(self.t) else np.zeros(0)
        self.zmin = self.t[:, :, 2].min(1) if len(self.t) else np.zeros(0)
        self.merk = {}

    def __call__(self, z: float):
        import shapely
        from shapely.geometry import Polygon
        k = round(float(z), 4)
        if k in self.merk:
            return self.merk[k]
        ganz = self.t[self.zmin >= z]
        teil = self.t[(self.zmax > z + 1e-9) & (self.zmin < z)]
        polys = [shapely.polygons(ganz[:, :, :2])] if len(ganz) else []
        if len(teil):
            # an der Ebene abschneiden (ohne Schleife): eine Ecke darüber → Dreieck, zwei → Viereck
            ob = teil[:, :, 2] >= z
            n_ob = ob.sum(1)

            def schnitt(a, c):
                q = ((z - a[:, 2]) / (c[:, 2] - a[:, 2]))[:, None]
                return (a + q * (c - a))[:, :2]
            for anz in (1, 2):
                t = teil[n_ob == anz]
                if not len(t):
                    continue
                o = ob[n_ob == anz]
                # Ecken so drehen, dass die einzelne Ecke (darüber bzw. darunter) vorne steht
                einzeln = o if anz == 1 else ~o
                kk = np.argmax(einzeln, axis=1)
                idx = np.stack([kk, (kk + 1) % 3, (kk + 2) % 3], 1)
                t = np.take_along_axis(t, idx[:, :, None], axis=1)
                a, b2, c2 = t[:, 0], t[:, 1], t[:, 2]
                if anz == 1:
                    ring = np.stack([a[:, :2], schnitt(a, b2), schnitt(a, c2)], 1)
                else:
                    ring = np.stack([schnitt(a, b2), b2[:, :2], c2[:, :2], schnitt(a, c2)], 1)
                polys.append(shapely.polygons(ring))
        if not polys:
            g = Polygon()
        else:
            alle = np.concatenate([np.atleast_1d(x) for x in polys])
            alle = alle[~shapely.is_empty(alle)]
            if not len(alle):
                g = Polygon()
            else:
                # ohne Raster (grid_size) etwa 6 × schneller; nur wenn die Vereinigung scheitert, mit
                try:
                    g = shapely.union_all(shapely.make_valid(alle))
                except shapely.errors.GEOSException:
                    g = shapely.union_all(shapely.make_valid(alle), grid_size=1e-5)
            g = g.buffer(0)
        self.merk[k] = g
        return g


class _Bestand:
    """Wie hoch das Material nach den bisherigen Ebenen noch steht (Raster, Z0 = Oberkante). Für jeden
    Eilgang nach unten: er endet über dem höchsten Material unter dem Fräser, nicht über einer Annahme
    («eine Zustellung über dem Boden», «die Ebene darüber ist frei»). Peter 25.10. (Impeller, Bericht der
    Fräsbahnen-Sitzung): neben den Modellwänden lassen die Ebenen einen schmalen Streifen stehen (Aufmass
    + eine Rasterzelle); «Boden nachfahren» tauchte im Eilgang 2 mm hinein."""

    def __init__(self, bx, zelle: float, r: float, material):
        self.x0, self.y0, self.x1, self.y1 = bx
        self.g = float(zelle)
        self.r = float(r)
        self.mat = material
        self.nx = max(1, int(math.ceil((bx[2] - bx[0]) / self.g)))
        self.ny = max(1, int(math.ceil((bx[3] - bx[1]) / self.g)))
        self.top = np.zeros((self.ny, self.nx), np.float32)
        self.xs = bx[0] + (np.arange(self.nx) + 0.5) * self.g
        self.ys = bx[1] + (np.arange(self.ny) + 0.5) * self.g
        # über den Materialrand hinaus (ueber_rand): dort ist Luft
        aussen = ((self.xs[None, :] < 0) | (self.xs[None, :] > material[0]) |
                  (self.ys[:, None] < 0) | (self.ys[:, None] > material[1]))
        if aussen.any():
            self.top[aussen] = -float(material[2]) - 1.0

    def _fenster(self, x0, y0, x1, y1):
        i0 = max(0, int(math.floor((x0 - self.x0) / self.g)))
        i1 = min(self.nx, int(math.ceil((x1 - self.x0) / self.g)) + 1)
        j0 = max(0, int(math.floor((y0 - self.y0) / self.g)))
        j1 = min(self.ny, int(math.ceil((y1 - self.y0) / self.g)) + 1)
        return i0, i1, j0, j1

    def geraeumt(self, geom, z: float):
        """Der Fräser hat die Fläche «geom» (was er überstrichen hat) bis auf z geräumt."""
        import shapely
        if geom is None or geom.is_empty:
            return
        i0, i1, j0, j1 = self._fenster(*geom.bounds)
        if i0 >= i1 or j0 >= j1:
            return
        X, Y = np.meshgrid(self.xs[i0:i1], self.ys[j0:j1])
        shapely.prepare(geom)                      # viele Punkte: vorbereitet ein Vielfaches schneller
        m = shapely.contains_xy(geom, X, Y)
        sub = self.top[j0:j1, i0:i1]
        sub[m] = np.minimum(sub[m], z)

    def hoehe(self, x: float, y: float, rand: float = 0.0) -> float:
        """Höchstes Material unter dem Fräser bei (x, y): Mitten der Rasterzellen im Radius (+ rand)."""
        R = self.r + rand
        i0, i1, j0, j1 = self._fenster(x - R, y - R, x + R, y + R)
        h = -1e9
        if i0 < i1 and j0 < j1:
            X, Y = np.meshgrid(self.xs[i0:i1], self.ys[j0:j1])
            m = (X - x) ** 2 + (Y - y) ** 2 <= R * R
            if m.any():
                h = float(self.top[j0:j1, i0:i1][m].max())
        # über den Rand des Rasters hinaus: dort steht das Material noch ganz (ausser ausserhalb des Rohteils)
        if ((x - R < self.x0 and self.x0 > 1e-6) or (y - R < self.y0 and self.y0 > 1e-6) or
                (x + R > self.x1 and self.x1 < self.mat[0] - 1e-6) or (y + R > self.y1 and self.y1 < self.mat[1] - 1e-6)):
            h = max(h, 0.0)
        return h if h > -1e8 else 0.0


def schruppen(tri: np.ndarray, material: Tuple[float, float, float], w: Dict, p: Dict,
              freifahrhoehe: float, startabstand: float, start, name: str, tol: float,
              extra: Optional[np.ndarray] = None):
    """extra: Zusatz (Boden der Trennfläche, Haltestege) – bleibt stehen wie das Modell, aber
    ohne Aufmass; der Fräsbereich richtet sich nur nach dem Modell."""
    from shapely.geometry import LineString, Point
    from .tasche import _ringe_von
    from .bahn import flaeche, kurze_reihenfolge
    from shapely.ops import unary_union
    warnungen: List[str] = []
    d = float(w["durchmesser"])
    r = d / 2
    zust = float(p.get("zustellung") or 0) or d / 2
    aufmass = max(0.0, float(p.get("aufmass") if p.get("aufmass") is not None else 0.5))
    schritt = d * max(5.0, min(90.0, float(p.get("bahnabstand") or 40))) / 100.0
    rand = float(p.get("rand")) if p.get("rand") not in (None, "") else d
    winkel = float(p.get("rampe_winkel") or w.get("rampe") or 3.0)
    # adaptiv: je Ebene mit gleichmässiger Last (FreeCAD Adaptive, wie beim Räumen der Tasche)
    adaptiv = bool(p.get("adaptiv"))
    last = max(5.0, min(90.0, float(p.get("adaptiv_last") or 15.0)))
    vollnut = float(w.get("vollnut") or 1.0)
    b = Bahnbauer(w["vorschub"], w["eintauchen"], freifahrhoehe, startabstand, start)
    lo, hi = tri.reshape(-1, 3).min(0), tri.reshape(-1, 3).max(0)
    ext = rand + d + 1
    u = ueber_rand(p)
    bx = _bx(lo, hi, ext, material, u)
    if bx[2] <= bx[0] or bx[3] <= bx[1]:
        warnungen.append("Das 3D-Modell liegt ausserhalb des Materials.")
        return b.weg(name), warnungen
    zelle = max(0.1, min(1.0, d / 24))
    oben, _unten, zelle = _raster(tri, bx, zelle)
    maske = np.isfinite(oben)
    if not maske.any():
        warnungen.append("Das 3D-Modell liegt ausserhalb des Materials.")
        return b.weg(name), warnungen
    umriss = umriss_modell(tri, maske, bx[0], bx[1], zelle, tol)
    bereich = _begrenzen(_bereich(umriss, rand + r, material[:2], p.get("_kreis"), u), p, warnungen)
    if not bereich.is_empty:
        bereich = _rest_begrenzen(bereich, p, tri, extra, material, bx, w, tol, warnungen)
    if bereich.is_empty:
        return b.weg(name), warnungen
    z_unten = float(np.nanmin(oben))
    x_raster = None
    if extra is not None and len(extra):
        x_raster, _xu, _z = _raster(extra, bx, zelle, max_punkte=10 ** 12)
        if np.isfinite(x_raster).any():
            z_unten = min(z_unten, float(np.nanmin(x_raster)))
        x_raster = np.where(np.isfinite(x_raster), x_raster, -1e9).astype(np.float32)
    z_unten = max(z_unten, -float(material[2]))
    if z_unten >= -1e-6:
        warnungen.append("Das Modell liegt ganz über der Materialoberfläche – nichts zu schruppen.")
        return b.weg(name), warnungen
    k_raster = np.where(maske, oben + aufmass, -1e9).astype(np.float32)
    # flache Stellen des Modells: je eine Ebene auf Höhe + Aufmass (Peter 25.10., Impeller: auf der Nabe blieben
    # 3 mm, auf dem Boden 1 mm stehen, weil die nächste Zustellebene ins Aufmass fiel) – dann bleibt dort nur das
    # Aufmass. Böden und senkrechte Wände auf Mass: eigene Bearbeitung «Böden und Wände» (ops/boeden_waende.py)
    from .boeden_waende import schrupp_ebenen
    ebenen = schrupp_ebenen(oben, maske, zelle, d, zust, aufmass, z_unten)
    z_alt = 0.0
    ebenen_gefraest = 0

    def frei_auf(z):
        halten = _flaechen_aus(k_raster, z, bx[0], bx[1], zelle, tol)
        if aufmass > 0 and not halten.is_empty:
            # plus eine Rasterzelle: an senkrechten Wänden liegt der Umriss aus dem Raster
            # bis zu einer Zelle daneben
            halten = halten.buffer(aufmass + zelle, resolution=8)
        if x_raster is not None:
            zusatz = _flaechen_aus(x_raster, z, bx[0], bx[1], zelle, tol)
            if not zusatz.is_empty:
                halten = zusatz if halten.is_empty else halten.union(zusatz)
        return bereich.difference(halten) if not halten.is_empty else bereich
    freis = [frei_auf(z) for z in ebenen]
    bestand = _Bestand(bx, zelle, r, material)
    mitten = {}                                # Ebene → wo die Fräsermitte gefahren ist
    adaptiv_erg = {}
    if adaptiv:
        # alle Ebenen gleichzeitig rechnen (Adaptive gibt die Python-Sperre frei); Genauigkeit
        # nach dem Aufmass – das Schlichten nimmt den Rest
        from concurrent.futures import ThreadPoolExecutor
        import os
        a_tol = max(0.05, min(0.25, aufmass / 2))
        with ThreadPoolExecutor(max_workers=max(1, os.cpu_count() or 1)) as pool:
            jobs = {k: pool.submit(_adaptiv_rechnen, f, d, last, a_tol) for k, f in enumerate(freis)
                    if not f.is_empty}
            for k, job in jobs.items():
                try:
                    adaptiv_erg[k] = job.result()
                except Exception as ex:
                    warnungen.append("Adaptives Schruppen auf Z %s nicht möglich (%s) – diese Ebene in Runden."
                                     % (_komma(ebenen[k]), ex))
    def teil_fraesen(teil, z, z_alt, sofort=True):
        """Ein Teilstück einer Ebene in Runden von innen nach aussen (Gleichlauf: aussen gegen den Uhrzeigersinn)."""
        erlaubt = teil.buffer(1e-3)
        stufen = [teil]
        n = 1
        while n < 2000:
            # innere Runden: shapely genügt (die Grenze zum Modell kommt genau aus
            # «mitte»); cam.puffer bräuchte hier bei grossen Abständen fast 1 s je Runde
            g = teil.buffer(-schritt * n, resolution=16, join_style=1)
            if g.is_empty:
                break
            stufen.append(g)
            n += 1
        ringe = []
        for g in reversed(stufen):
            for t in getattr(g, "geoms", [g]):
                if t.is_empty or t.geom_type != "Polygon":
                    continue
                for ring, loch in _ringe_von(t):
                    if (flaeche(ring) > 0) != (not loch):
                        ring = list(reversed(ring))
                    ringe.append(ring)
        erster = True
        for ring in ringe:
            if len(ring) < 3:
                continue
            k0 = min(range(len(ring)), key=lambda i: math.dist(ring[i], b.pos[:2]))
            ring = list(ring[k0:]) + list(ring[:k0])
            p0 = ring[0]
            if not erster and abs(b.pos[2] - z) < 1e-9 and erlaubt.contains(LineString([b.pos[:2], p0])):
                b.fraesen(p0[0], p0[1])
            else:
                b.abheben()
                b.anfahren(*p0)
                weg = ring[1:] + [ring[0]]
                # bis über das höchste Material unter dem Fräser frei, mindestens bis zur Ebene darüber
                b.rampe(z, weg, winkel, z_von=max(z_alt, bestand.hoehe(p0[0], p0[1], rand=bestand.g)), max_laenge=4 * d)
            erster = False
            for q in ring[1:] + [ring[0]]:
                b.fraesen(q[0], q[1])
        b.abheben()
        # geräumt: was der Fräser hier überstrichen hat (Mitten + Radius) – bei Lagen gleich nach dem Teilstück,
        # damit die Rampen der nächsten Stücke (auch tiefer in derselben Lücke) über dem richtigen Bestand beginnen
        if sofort:
            bestand.geraeumt(teil.buffer(r, resolution=16), z)

    def teile_von(k_e):
        mitte = _versetzt(freis[k_e], -r, tol)
        if mitte.is_empty:
            return []
        return [t for t in getattr(mitte, "geoms", [mitte]) if t.geom_type == "Polygon" and t.area > 1e-4]

    # «Lücke für Lücke in Lagen von … mm» (Peter 26.10.): die Ebenen in Lagen zusammengefasst; innerhalb einer Lage
    # jedes Teilstück (Lücke zwischen Schaufeln, Insel) über alle Ebenen der Lage fertig, dann das nächste. Eine Lage
    # endet spätestens an der Stufengrenze von «Böden und Wände» im Wechsel (p["_stufe"], von engine). Adaptiv:
    # Ebene für Ebene wie bisher.
    lagen = max(0.0, float(p.get("lagen") or 0))
    stufe_bw = max(0.0, float(p.get("_stufe") or 0))
    if lagen > 0 and not adaptiv:
        def lage_von(z):
            k = int(math.floor(-z / lagen - 1e-6))
            ks = int(math.floor(-z / stufe_bw - 1e-6)) if stufe_bw > 0 else 0
            return (ks, k)
        gruppen = []
        for k_e, z in enumerate(ebenen):
            if gruppen and lage_von(ebenen[gruppen[-1][0]]) == lage_von(z):
                gruppen[-1].append(k_e)
            else:
                gruppen.append([k_e])
        for gruppe in gruppen:
            # Teilstücke je Ebene; jedes Stück einer tieferen Ebene gehört zu dem der Ebene darüber, in dem es liegt
            # (tiefer ist das Freie nie grösser: das Modell wird nach unten nur breiter)
            stuecke = {k_e: teile_von(k_e) for k_e in gruppe}
            kinder = {}
            for a, c in zip(gruppe, gruppe[1:]):
                for j, t in enumerate(stuecke[c]):
                    oben_t = stuecke[a]
                    if not oben_t:
                        continue
                    i = max(range(len(oben_t)), key=lambda i: oben_t[i].intersection(t).area)
                    if oben_t[i].intersection(t).area <= 1e-9:
                        continue                           # liegt in keinem Stück darüber: als eigene Säule
                    kinder.setdefault((a, i), []).append((c, j))
            hat_eltern = {kj for v in kinder.values() for kj in v}
            wurzeln = [(k_e, j) for k_e in gruppe for j in range(len(stuecke[k_e])) if (k_e, j) not in hat_eltern]

            def saeule(k_e, j):
                z = ebenen[k_e]
                teil_fraesen(stuecke[k_e][j], z, ebenen[k_e - 1] if k_e > 0 else 0.0)
                mitten.setdefault(z, []).append(stuecke[k_e][j])
                ks = kinder.get((k_e, j), [])
                folge_k = kurze_reihenfolge(len(ks), lambda u, v: stuecke[ks[u][0]][ks[u][1]].distance(
                    stuecke[ks[v][0]][ks[v][1]]), lambda u: stuecke[ks[u][0]][ks[u][1]].distance(Point(b.pos[:2])),
                    max_n=120)
                for u in folge_k:
                    saeule(*ks[u])
            # Säulen in kurzer Reihenfolge, aber die oberste Ebene der Lage zuerst (Stücke, die erst tiefer
            # beginnen, z. B. auf einer flachen Ebene, kommen nach ihrer Nachbarschaft)
            wurzeln.sort(key=lambda kj: gruppe.index(kj[0]))
            rest_w = list(wurzeln)
            while rest_w:
                ebene0 = rest_w[0][0]
                diese = [kj for kj in rest_w if kj[0] == ebene0]
                folge_w = kurze_reihenfolge(len(diese), lambda u, v: stuecke[diese[u][0]][diese[u][1]].distance(
                    stuecke[diese[v][0]][diese[v][1]]), lambda u: stuecke[diese[u][0]][diese[u][1]].distance(
                    Point(b.pos[:2])), max_n=120)
                for u in folge_w:
                    saeule(*diese[u])
                rest_w = [kj for kj in rest_w if kj not in diese]
            ebenen_gefraest += sum(1 for k_e in gruppe if stuecke[k_e])
        mitten = {z: unary_union(v) for z, v in mitten.items()}
    else:
      for k_e, z in enumerate(ebenen):
        frei = freis[k_e]
        # adaptiv – findet Adaptive keinen Platz zum Eintauchen (schmale Ringe), wie bisher in Runden
        if adaptiv_erg.get(k_e):
            _adaptiv_ausgeben(b, adaptiv_erg[k_e], d, z, z_alt, winkel, vollnut, bestand)
            # geräumt: wo der Fräser in «frei» ganz hinpasst
            offen = _versetzt(frei, -r, tol)
            if not offen.is_empty:
                bestand.geraeumt(offen.buffer(r, resolution=16), z)
                mitten[z] = offen
            ebenen_gefraest += 1
            z_alt = z
            continue
        teile = teile_von(k_e)
        if not teile:
            z_alt = z
            continue
        folge = kurze_reihenfolge(len(teile), lambda i, j: teile[i].distance(teile[j]),
                                  lambda i: teile[i].distance(Point(b.pos[:2])), max_n=120)
        for k_t in folge:
            teil_fraesen(teile[k_t], z, z_alt, sofort=False)
        # geräumt: was der Fräser auf dieser Ebene überstrichen hat (Mitten + Radius)
        mitten[z] = unary_union(teile)
        bestand.geraeumt(mitten[z].buffer(r, resolution=16), z)
        ebenen_gefraest += 1
        z_alt = z
    if ebenen_gefraest == 0:
        warnungen.append("Der Fräser passt nirgends zwischen Modell und Rand – nichts geschruppt.")
    elif extra is not None and len(extra):
        _boden_nachfahren(b, tri, extra, w, aufmass, bereich, umriss, schritt, zust, z_unten, tol,
                          warnungen, bestand)
    if ebenen_gefraest:
        hinweis = _rest_hinweis(bestand, oben, maske, x_raster, bereich, aufmass, d, freis, ebenen, mitten, r, tol)
        if hinweis:
            warnungen.append(hinweis)
    return b.weg(name), warnungen


def _zahl(v: float) -> str:
    """Zahl ohne überflüssige Nullen, mit Komma (6 · 2,5 · 23,2)."""
    t = ("%.2f" % v).rstrip("0").rstrip(".")
    return t.replace(".", ",")


def _rest_hinweis(bestand, oben, maske, x_raster, bereich, aufmass, d, freis, ebenen, mitten, r, tol):
    """Wo der Fräser nicht hinpasst (frei wäre, aber nicht erreicht – nicht die Treppen über geneigten Flächen,
    die nimmt das Schlichten): wie viel bleibt dort stehen, und welcher kleinere Fräser käme weiter
    (Peter 25.10.: Restmaterial vorschlagen)."""
    import shapely
    from shapely.ops import polylabel, unary_union
    ny, nx = bestand.top.shape
    m = np.where(maske, oben, -1e9)
    if x_raster is not None:
        m = np.maximum(m, x_raster)
    m = m[:ny, :nx]
    if m.shape != bestand.top.shape:
        return None
    breite = 0.0
    offene = []
    paare = []                       # (nicht erreicht, freie Fläche derselben Ebene)
    for z, f in zip(ebenen, freis):
        if f.is_empty:
            continue
        erreicht = mitten.get(z)
        offen = f if erreicht is None else f.difference(erreicht.buffer(r, resolution=16))
        for t in getattr(offen, "geoms", [offen]):
            if t.geom_type != "Polygon" or t.area < 1.0:
                continue
            # schmale Säume (Rundung am Rand des Erreichten) zählen nicht, ebenso einzelne kleine Zwickel in
            # Innenecken (z. B. am Haltesteg, seit das Schruppen zweiseitig weiter hinaus räumt: je 1–4 mm²)
            if t.buffer(-0.2).is_empty or t.area < d * d / 4:
                continue
            offene.append(t)
            paare.append((t, f))
            try:
                pol = polylabel(t, tolerance=0.05)
                breite = max(breite, 2 * t.boundary.distance(pol))
            except Exception:
                pass
    if not offene:
        return None
    gebiet = unary_union(offene)
    X, Y = np.meshgrid(bestand.xs, bestand.ys)
    drin = shapely.contains_xy(gebiet, X, Y) & (m > -1e8)
    rest = np.where(drin, bestand.top - (m + aufmass), 0.0)
    viel = rest > 1.0
    flaeche = float(viel.sum()) * bestand.g ** 2
    if flaeche < max(4.0, d * d * 0.5):
        return None
    hoch = float(np.percentile(rest[viel], 95))
    # Impeller 08.11.: vorgeschlagen wurde der grösste Fräser, der in die breiteste Stelle passt (Ø 5) – in die
    # engen Kanäle passte er nicht, «nur Restmaterial» mit Ø 5 liess fast alles stehen. Jetzt zählt, wie viel des
    # Rests er erreicht: der grösste, der mindestens 90 % erreicht, sonst der, der am meisten erreicht.
    # (Die freien Flächen sind schon um das Aufmass kleiner – ein Fräser Ø dd erreicht, was seine Mitte in
    # «frei − dd/2» überstreicht.)
    flaeche_rest = gebiet.area
    erreicht_von = {}
    for dd in (5.0, 4.0, 3.0, 2.0, 1.5, 1.0):
        if dd >= d - 1e-6 or dd > breite - 0.05 or flaeche_rest <= 0:
            continue
        teil = 0.0
        offen_dd = {}
        for t, f_ in paare:
            if id(f_) not in offen_dd:           # was der Fräser Ø dd auf dieser Ebene überstreichen kann
                innen = f_.buffer(-dd / 2, resolution=8)
                offen_dd[id(f_)] = None if innen.is_empty else innen.buffer(dd / 2, resolution=8)
            if offen_dd[id(f_)] is not None:
                teil += offen_dd[id(f_)].intersection(t).area
        erreicht_von[dd] = min(1.0, teil / max(1e-9, sum(t.area for t in offene)))
    vorschlag = next((dd for dd in sorted(erreicht_von, reverse=True) if erreicht_von[dd] >= 0.9), None)
    if vorschlag is None and erreicht_von:
        vorschlag = max(erreicht_von, key=lambda dd: (erreicht_von[dd], dd))
        if erreicht_von[vorschlag] < 0.05:
            vorschlag = None
    text = ("Restmaterial: Der Fräser Ø %s passt nicht überall hin – auf %s mm² bleiben bis %s mm über dem "
            "Aufmass stehen." % (_zahl(d), _zahl(round(flaeche)), _zahl(round(hoch, 1))))
    if vorschlag:
        text += (" Vorschlag: danach ein zweites 3D-Schruppen mit einem Schaftfräser Ø %s und «nur Restmaterial»."
                 % _zahl(vorschlag))
        if erreicht_von[vorschlag] < 0.9:
            text += REST_TEIL % round(100 * erreicht_von[vorschlag])
        # ein grösserer, der schon das meiste erreicht (schneller, kürzere Schneide reicht eher): dazu sagen
        groesser = [dd for dd in erreicht_von if dd > vorschlag and erreicht_von[dd] >= 0.5]
        if groesser:
            dd = max(groesser)
            text += REST_GROESSER % (_zahl(dd), round(100 * erreicht_von[dd]))
    return text


REST_TEIL = " Er erreicht etwa %d %% davon."   # Impeller 08.11.
REST_GROESSER = " Ein Ø %s erreicht etwa %d %%."


def _boden_nachfahren(b, tri, extra, w, aufmass, bereich, umriss, schritt, zust, z_unten, tol,
                      warnungen, bestand=None):
    """Zweiseitig: die Ebenen lassen über der (schiefen) Trennfläche und den Stegen bis zu
    einer Zustellung Material stehen. Hier fährt der Fräser in Runden um das Modell genau auf
    dem Boden – so treffen sich die Schnitte von Seite 1 und Seite 2 und der Spalt um das
    Modell geht durch. Höhe je Punkt aus OpenCAMLib: Modell mit Aufmass, Boden und Stege ohne."""
    from shapely.geometry import LineString
    from .. import hirch_relief
    if not hirch_relief.mit_ocl():
        warnungen.append("Ohne OpenCAMLib bleibt über der Trennfläche bis zu einer Zustellung Material "
                         "stehen – bitte Hirch CAM aktualisieren.")
        return
    d = float(w["durchmesser"])
    # Modell ohne Aufmass (Peter 02.11.: sauber bis auf die Haut): der Schaftfräser liegt mit dem Boden auf
    # der Haut und berührt das Modell höchstens mit der Kante – neben dem Modell bleibt keine Kehle stehen
    ab_m = _abtaster(tri, w, 0.0, z_unten)
    ab_x = _abtaster(extra, w, 0.0, z_unten)

    def abtasten(pts):
        return np.maximum(ab_m(pts), ab_x(pts))
    # aussen bleibt der Fräser einen Radius innerhalb des Fräsbereichs (dort steht das Material noch in
    # voller Höhe – die Ebenen haben es nicht geräumt); innen zum Modell hebt ihn OpenCAMLib
    frei = _versetzt(bereich, -d / 2, tol)
    if frei.is_empty:
        return
    from shapely.geometry import Point
    innen_sicher = _puffer(bereich, -(d / 2 + 1.0))
    frei = frei.difference(umriss.buffer(0.01, resolution=8))
    if frei.is_empty:
        return
    abtast = max(0.1, min(0.5, d / 12))
    ringe = []
    k = 0
    while k < 2000:
        g = frei if k == 0 else _versetzt(frei, -schritt * k, tol)
        if g.is_empty:
            break
        for t in getattr(g, "geoms", [g]):
            if t.geom_type != "Polygon" or t.is_empty:
                continue
            for r in [t.exterior] + list(t.interiors):
                if r.length > 4 * abtast:
                    ringe.append(r)
        k += 1
    stuecke = []
    for r in ringe:
        q = np.asarray(LineString(r.coords).segmentize(abtast).coords)[:, :2]
        stuecke.append(q)
    if not stuecke:
        return
    alle = np.concatenate(stuecke)
    z = abtasten(alle)
    roh, i = [], 0
    for q in stuecke:
        roh.append(np.column_stack([q, z[i:i + len(q)]]))
        i += len(q)
    roh = _verfeinern(roh, abtasten, max(0.05, abtast / 2))
    for pf in (_vereinfachen(s, tol) for s in roh):
        b.abheben()
        b.anfahren(float(pf[0][0]), float(pf[0][1]))
        z0 = float(pf[0][2])
        # im Eilgang nur bis über das Material, das die Ebenen hier stehen liessen (neben Modellwänden ein
        # Streifen bis weit über dem Boden), den Rest im Vorschub
        z_frei = max(z0, bestand.hoehe(float(pf[0][0]), float(pf[0][1]), rand=bestand.g)) if bestand is not None else z0 + zust
        # am äusseren Rand des Bereichs (dort steht das Rohteil in voller Höhe, in Ecken auch schmale Keile,
        # die das Raster nicht sicher sieht): ganz im Vorschub eintauchen
        if not innen_sicher.contains(Point(float(pf[0][0]), float(pf[0][1]))):
            z_frei = max(z_frei, 0.0)
        b.bis_frei(z_frei)
        b.eintauchen(z0, None)
        for q in pf[1:]:
            b.fraesen(float(q[0]), float(q[1]), float(q[2]))
    b.abheben()


# --------------------------------------------------------------------------------------------
# Schlichten

def _vereinfachen(pts: np.ndarray, tol: float) -> np.ndarray:
    """Douglas-Peucker: Punkte weglassen, solange die Linie höchstens tol abweicht – seitlich
    und in der Höhe getrennt gemessen (an steilen Wänden wäre der 3D-Abstand viel kleiner als
    der Höhenfehler, der Fräser würde ins Modell schneiden)."""
    if len(pts) < 3:
        return pts
    behalten = np.zeros(len(pts), bool)
    behalten[[0, -1]] = True
    stapel = [(0, len(pts) - 1)]
    while stapel:
        a, e = stapel.pop()
        if e <= a + 1:
            continue
        A, d = pts[a], pts[e] - pts[a]
        d2 = d[:2]
        L = float(d2 @ d2)
        Q = pts[a + 1:e] - A
        t = np.clip(Q[:, :2] @ d2 / L, 0, 1) if L > 0 else np.zeros(len(Q))
        seitlich = np.linalg.norm(Q[:, :2] - np.outer(t, d2), axis=1)
        abst = np.maximum(seitlich, np.abs(Q[:, 2] - t * d[2]))
        k = int(abst.argmax())
        if abst[k] > tol:
            behalten[a + 1 + k] = True
            stapel += [(a, a + 1 + k), (a + 1 + k, e)]
    return pts[behalten]


def _verfeinern(stuecke: List[np.ndarray], abtasten, stufe: float, kuerzest: float = 0.01,
                runden: int = 10) -> List[np.ndarray]:
    """Abtastpunkte (x, y, z) dort verdichten, wo die Höhe zwischen zwei Punkten stark springt
    (Kanten, steile Wände): sonst schneidet die Gerade dazwischen die Kante an. Neue Punkte in
    der Mitte, bis der Sprung höchstens «stufe» ist oder die Punkte «kuerzest» auseinander liegen."""
    for _ in range(runden):
        neu_xy, wo = [], []
        for k, s in enumerate(stuecke):
            if len(s) < 2:
                continue
            dz = np.abs(np.diff(s[:, 2]))
            dl = np.hypot(np.diff(s[:, 0]), np.diff(s[:, 1]))
            idx = np.nonzero((dz > stufe) & (dl > kuerzest))[0]
            if len(idx):
                neu_xy.append((s[idx, :2] + s[idx + 1, :2]) / 2)
                wo.append((k, idx))
        if not neu_xy:
            break
        z = abtasten(np.concatenate(neu_xy))
        i = 0
        for (k, idx), xy in zip(wo, neu_xy):
            pts = np.column_stack([xy, z[i:i + len(xy)]])
            i += len(xy)
            stuecke[k] = np.insert(stuecke[k], idx + 1, pts, axis=0)
    return stuecke


def _schaft(w: Dict):
    """Schaft, der dicker ist als die Schneide (z. B. 3 mm Schneide an 6 mm Schaft): (Ø, Höhe seiner
    Unterkante über der Spitze) – sonst None. Wie die Kollisionsprüfung (relief_halterung.koerper)."""
    from .relief_halterung import koerper
    ringe = [k for k in koerper(w, None) if k["teil"] == "schaft"]
    if not ringe:
        return None
    # der Übergang ist eine Folge von Ringen (Kegel): für die Bahn sicher als voller Schaft ab seiner
    # Unterkante – wie vor dem Kegel
    return 2 * max(float(k["r"]) for k in ringe), min(float(k["unten"]) for k in ringe)


def _abtaster(tri, w: Dict, aufmass: float, z_unten: float):
    from .. import hirch_relief
    typ = w.get("typ") or "schaft"
    wz = {"typ": typ, "durchmesser": float(w["durchmesser"]) + 2 * aufmass,
          "eckradius": max(0.0, float(w.get("eckradius") or (float(w["durchmesser"]) / 2 if typ == "kugel" else 0))
                           + aufmass), "v_winkel": float(w.get("v_winkel") or 60), "spitze": float(w.get("spitze") or 0),
          "laenge": float(w.get("schneidenlaenge") or w.get("laenge") or 50)}

    # Suchbaum einmal bauen, für alle Runden (Bahnen, Verfeinern) wiederverwenden
    # eigener Abtaster (genau, ~10–100× schneller als OpenCAMLib); HIRCH_3D_OCL=1: OpenCAMLib
    import os
    if hasattr(hirch_relief, "SchnellAbtaster") and not os.environ.get("HIRCH_3D_OCL"):
        ab = hirch_relief.SchnellAbtaster(np.ascontiguousarray(tri, np.float32), wz)
    else:
        ab = hirch_relief.Abtaster(tri, wz) if hasattr(hirch_relief, "Abtaster") else None

    # dicker Schaft über der Schneide: als zweiter (flacher) Zylinder mitabgetastet – die Spitze
    # bleibt so hoch, dass auch der Schaft das Modell nicht berührt
    sch = _schaft(w)
    ab_schaft = None
    if sch is not None and hasattr(hirch_relief, "SchnellAbtaster"):
        ab_schaft = hirch_relief.SchnellAbtaster(np.ascontiguousarray(tri, np.float32),
                                                 {"typ": "schaft", "durchmesser": sch[0] + 2 * aufmass,
                                                  "eckradius": 0.0, "laenge": 1000.0})

    def abtasten(pts: np.ndarray) -> np.ndarray:
        # Fräser um das Aufmass vergrössert: seine Spitze liegt um das Aufmass tiefer als die
        # des echten Fräsers mit derselben Mitte – also das Aufmass dazuzählen (beim Schaft-
        # fräser gibt das den Abstand nach oben, die Vergrösserung den seitlichen)
        if len(pts) == 0:
            return np.zeros(0)
        q = np.ascontiguousarray(pts, np.float64)
        z = ab.abtasten(q, z_unten - aufmass) if ab is not None else \
            hirch_relief.abtasten(tri, wz, q, z_unten - aufmass)
        if ab_schaft is not None:
            z = np.maximum(z, ab_schaft.abtasten(q, z_unten - aufmass - sch[1]) - sch[1])
        return z + aufmass
    return abtasten


def _wasserlinien(tri, w: Dict, aufmass: float, abtast: float):
    """Genaue Höhenlinien mit OpenCAMLib (AdaptiveWaterline): Funktion Höhe der Spitze → Züge
    (M, 2) der Fräsermitte; None, wenn das Rechenmodul sie nicht kennt."""
    from .. import hirch_relief
    if not hasattr(hirch_relief, "Wasserlinie") or _schaft(w) is not None:
        return None          # dicker Schaft: OpenCAMLib kennt ihn nicht – Höhenlinien aus dem Raster
    typ = w.get("typ") or "schaft"
    wz = {"typ": typ, "durchmesser": float(w["durchmesser"]) + 2 * aufmass,
          "eckradius": max(0.0, float(w.get("eckradius") or (float(w["durchmesser"]) / 2 if typ == "kugel" else 0))
                           + aufmass), "v_winkel": float(w.get("v_winkel") or 60), "spitze": float(w.get("spitze") or 0),
          "laenge": float(w.get("schneidenlaenge") or w.get("laenge") or 50)}
    wl = hirch_relief.Wasserlinie(np.ascontiguousarray(tri, np.float32), wz)

    def linien(z: float):
        # Fräser um das Aufmass vergrössert: seine Spitze liegt um das Aufmass tiefer
        return [np.asarray(q, np.float64) for q in wl.linien(z - aufmass, max(0.05, abtast), max(0.01, abtast / 10))]
    return linien


def schlichten(tri: np.ndarray, material: Tuple[float, float, float], w: Dict, p: Dict,
               freifahrhoehe: float, startabstand: float, start, name: str, tol: float,
               extra: Optional[np.ndarray] = None):
    from shapely.geometry import LineString
    from .. import hirch_relief
    warnungen: List[str] = []
    b = Bahnbauer(w["vorschub"], w["eintauchen"], freifahrhoehe, startabstand, start)
    if not hirch_relief.mit_ocl():
        warnungen.append("3D-Schlichten braucht OpenCAMLib (Paket libopencamlib-2023.01) – bitte "
                         "Hirch CAM aktualisieren.")
        return b.weg(name), warnungen
    d = float(w["durchmesser"])
    r = d / 2
    if (w.get("typ") or "schaft") not in ("kugel", "eckradius", "konisch") and p.get("strategie") != "ebenen":
        warnungen.append("Zum Schlichten eignet sich ein Kugelfräser (Vollradius) am besten.")
    schritt = d * max(1.0, min(80.0, float(p.get("bahnabstand") or 10))) / 100.0
    aufmass = float(p.get("aufmass") or 0.0)
    if aufmass < 0:
        # negatives Aufmass (Peter 31.10.): gerechnet wie ein um |a| kleinerer Fräser, die Spitze |a| tiefer –
        # das ganze Modell wird so viel kleiner. Höchstens 1 mm und weniger als der Radius.
        grenze = min(1.0, 0.9 * (d / 2))
        if aufmass < -grenze:
            warnungen.append("Negatives Aufmass auf −%s mm begrenzt (höchstens 1 mm und weniger als der Radius)."
                             % _komma(grenze))
            aufmass = -grenze
        warnungen.append("Negatives Aufmass: das Modell wird überall %s mm kleiner gefräst." % _komma(-aufmass))
    rand = float(p.get("rand")) if p.get("rand") not in (None, "") else r + 1.0
    abtast = float(p.get("abtastung") or 0) or max(0.1, min(0.5, d / 12))
    richtung = p.get("richtung") or "x"
    lo, hi = tri.reshape(-1, 3).min(0), tri.reshape(-1, 3).max(0)
    ext = rand + 1
    u = ueber_rand(p)
    bx = _bx(lo, hi, ext, material, u)
    if bx[2] <= bx[0] or bx[3] <= bx[1]:
        warnungen.append("Das 3D-Modell liegt ausserhalb des Materials.")
        return b.weg(name), warnungen
    oben, _u, zelle = _raster(tri, bx, 0.5)
    maske = np.isfinite(oben)
    if not maske.any():
        warnungen.append("Das 3D-Modell liegt ausserhalb des Materials.")
        return b.weg(name), warnungen
    umriss = umriss_modell(tri, maske, bx[0], bx[1], zelle, tol)
    bereich = _begrenzen(_bereich(umriss, rand, material[:2], p.get("_kreis"), u), p, warnungen)
    if not bereich.is_empty:
        bereich = _rest_begrenzen(bereich, p, tri, extra, material, bx, w, tol, warnungen)
    if bereich.is_empty:
        return b.weg(name), warnungen
    if p.get("auslassen_fertig"):
        # Peter 25.10.: was «Böden und Wände» mit dem Schaftfräser schon auf Mass gefräst hat (waagrechte Flächen,
        # senkrechte Wände), fährt der Kugelfräser nicht noch einmal – es bleiben geneigte und gekrümmte Stellen,
        # z. B. die Rundungen (Kehlen R3) am Fuss von Schaufeln und Nabe
        bereich = bereich.intersection(_geneigt(tri, bx, r, tol))
        if bereich.is_empty:
            warnungen.append("Nichts mehr zu schlichten: das Modell hat nur waagrechte Flächen und senkrechte "
                             "Wände – die macht «Böden und Wände» auf Mass.")
            return b.weg(name), warnungen
    z_unten = float(np.nanmin(oben))
    if extra is not None and len(extra):
        z_unten = min(z_unten, float(extra[..., 2].min()))
        tri = np.ascontiguousarray(np.concatenate([tri, extra]), np.float32)
    z_unten = max(z_unten, -float(material[2]))
    abtasten = _abtaster(tri, w, aufmass, z_unten)
    strategie = p.get("strategie") or "parallel"
    if strategie not in ("parallel", "hoehenlinien", "kombiniert", "ebenen", "kehlen", "konturen"):
        strategie = "parallel"
    grenz = math.radians(max(10.0, min(80.0, float(p.get("grenzwinkel") or 40.0))))
    stufe = max(0.05, abtast * 0.5)
    pfade: List[np.ndarray] = []
    fuss: List[np.ndarray] = []
    if p.get("auslassen_fertig") and p.get("fuss_umlauf") is not False:
        # Kehlen am Fuss (Peter 25.10.: R3 um Schaufeln und Nabe «wie eine Kontur» abfahren): je waagrechter
        # Fläche ein Umlauf, Spitze auf der Fläche – die Kugel liegt dann in der Rundung; ist ihr Radius so gross
        # wie der der Rundung, ist diese mit einem Umlauf fertig. Danach nur noch, was der Umlauf nicht trifft.
        fuss = _fuss_umlaeufe(tri, bx, bereich, abtasten, abtast, z_unten, d, aufmass, stufe, tol)
        if fuss:
            from shapely.ops import unary_union as _uu
            # nur, was die Umläufe wirklich fertig schlichten (simuliert), nehmen die übrigen Bahnen aus
            getroffen = _fuss_fertig(fuss, tri, bx, w, aufmass, tol)
            # Zufallstest 3D 07.11., Fall 16: ungültige Fläche → GEOS «unable to assign hole to a shell»
            bereich = _gueltig(_gueltig(bereich).difference(_gueltig(getroffen)))
            if bereich.is_empty or _puffer(bereich, -0.3).is_empty:
                pfade = list(fuss)
                # «Konturen je Form» und «Kehlen»: die Kehlen auch dann, wenn ausser den Umläufen kaum Fläche bleibt
                # (3D → Fräsbahnen 26.10. 19:05, Punkt 5)
                if not bereich.is_empty and (strategie == "kehlen" or
                                             (strategie == "konturen" and p.get("kehlen_nachfahren", True))):
                    pfade += _kehlen(bereich, abtasten, d, schritt, abtast, z_unten, stufe, tol, pfade[-1][-1],
                                     dict(p, kehlen_bahnen=0) if strategie == "konturen" else p, warnungen)
                _ausgeben(b, pfade, abtasten, _uu([getroffen, bereich]), schritt, d, abtast, stufe, tol)
                return b.weg(name), warnungen
    bereich_parallel = bereich
    if strategie == "konturen":
        # Peter 26.10.: jede Form für sich von oben nach unten als Schraubenlinie, flache Stellen entlang ihrer
        # Kontur (Kuppel als Spirale), zuletzt die Kehlen in einem Zug (Bleistiftbahnen)
        cl = _cl_karte(bereich, abtasten, abtast, z_unten)
        steil, flach = _steil_flach(cl, bereich, grenz, schritt)
        dz = schritt * math.sin(grenz)
        pfade = _entlang_kontur(flach, abtasten, schritt, abtast, stufe, tol, start) + \
            _konturen_je_form(cl, steil, dz, abtasten, abtast, stufe, tol, start,
                              ohne_senkrecht=bool(p.get("auslassen_fertig")))
        if p.get("kehlen_nachfahren", True):
            pfade += _kehlen(bereich, abtasten, d, schritt, abtast, z_unten, stufe, tol,
                             pfade[-1][-1] if pfade else start, dict(p, kehlen_bahnen=0), [])
        bereich_parallel = None
    elif strategie == "kehlen":
        pfade = _kehlen(bereich, abtasten, d, schritt, abtast, z_unten, stufe, tol, start, p, warnungen)
        bereich_parallel = None
    elif strategie in ("hoehenlinien", "kombiniert", "ebenen"):
        # «genau»: Höhenlinien von OpenCAMLib statt aus der Karte (die Karte braucht es dann nur
        # noch für steil/flach und die waagrechten Flächen)
        genau = _wasserlinien(tri, w, aufmass, abtast) if p.get("genau") and strategie != "ebenen" else None
        if p.get("genau") and strategie != "ebenen" and _schaft(w) is not None:
            warnungen.append("«Höhenlinien genau rechnen» geht mit einem Schaft, der dicker ist als die "
                             "Schneide, nicht – die Höhenlinien kommen aus dem Raster.")
        cl = None if genau is not None and strategie == "hoehenlinien" else \
            _cl_karte(bereich, abtasten, abtast, z_unten)
        if strategie == "ebenen":
            pfade = _ebenen(cl, umriss, bereich, w, abtasten, schritt, aufmass, tol, warnungen)
            bereich_parallel = None
        else:
            steil = None
            if strategie == "kombiniert":
                steil, flach = _steil_flach(cl, bereich, grenz, schritt)
                bereich_parallel = flach
                dz = schritt * math.sin(grenz)
            else:
                bereich_parallel = None
                dz = schritt
            hoehen = (float(tri[..., 2].max()) + aufmass, z_unten) if cl is None else None
            pfade = _hoehenlinien(cl, bereich if steil is None else steil, dz, abtasten, stufe, tol, start,
                                  genau, hoehen)
    if bereich_parallel is not None and not bereich_parallel.is_empty:
        pfade = _inseln_reihenfolge(
            _parallel_pfade(bereich_parallel, richtung, schritt, abtast, abtasten, stufe, tol),
            bereich_parallel, start) + pfade
    if fuss:
        pfade = list(fuss) + pfade
        from shapely.ops import unary_union as _uu
        bereich = _uu([bereich] + [LineString(f[:, :2]).buffer(r, resolution=8) for f in fuss if len(f) > 1])
    if not pfade:
        warnungen.append("Kein Bereich zum Schlichten gefunden.")
        return b.weg(name), warnungen
    _ausgeben(b, pfade, abtasten, bereich, schritt, d, abtast, stufe, tol)
    return b.weg(name), warnungen


def _auf_hoehe(xy: np.ndarray, L: float, abtasten, zelle: float, zz: Optional[np.ndarray] = None) -> np.ndarray:
    """Punkte einer Höhenlinie, die beim genauen Abtasten höher liegen als L (die Linie aus dem Raster schneidet an
    einer Wand die Klippe zwischen zwei Zellen – die Kugel säße dort auf der Wand), quer zur Linie bis 1,5 Zellen
    verschieben, wo der Fräser wieder auf L steht. Sonst klettert die Bahn an jeder Wand hinauf und wieder hinunter
    (Impeller: 49 m senkrechter Vorschub). Gibt xy zurück."""
    if len(xy) < 2:
        return xy
    z = abtasten(xy) if zz is None else zz
    schlecht = np.flatnonzero(z > L + 0.05)
    if not len(schlecht):
        return xy
    tg = np.gradient(xy, axis=0)
    tg /= np.maximum(np.linalg.norm(tg, axis=1, keepdims=True), 1e-12)
    nrm = np.column_stack([-tg[:, 1], tg[:, 0]])[schlecht]
    stufen = np.array([s_ for k in range(1, 7) for s_ in (k, -k)], np.float64) * zelle * 0.25
    kand = xy[schlecht][:, None, :] + nrm[:, None, :] * stufen[None, :, None]
    zk = abtasten(kand.reshape(-1, 2)).reshape(len(schlecht), len(stufen))
    ok = zk <= L + 0.05
    aus = xy.copy()
    for q, i in enumerate(schlecht):
        w = np.flatnonzero(ok[q])
        if len(w):
            aus[i] = kand[q, w[0]]
    return aus


def _an_die_klippe(xy: np.ndarray, L: float, abtasten, zelle: float, eps: float = 0.03,
                   zurueck: float = 0.05) -> np.ndarray:
    """Punkte eines Umlaufs am Fuss quer zur Linie so weit zur Wand schieben, wie die Fräserspitze noch auf L bleibt
    (bis auf 0,01 mm, Halbieren). Die Höhenlinie aus der CL-Karte liegt an einer Rundung, die so gross ist wie der
    Fräser, zwischen zwei Zellen vor der Klippe – bis eine Zelle neben der Stelle, an der die Kugel in der Rundung
    liegt. So liegt sie in der Rundung und schlichtet sie ganz, auf einer Höhe, ohne an der Klippe auf und ab zu
    fahren (Fräsbahnen 26.10. nachts). Punkte, die nirgends auf L stehen, bleiben (werden danach getrennt).
    zurueck: so weit wieder von der Klippe weg – sonst schneidet die Gerade zwischen zwei Punkten (und nach dem
    Vereinfachen um die Toleranz) an einer gekrümmten Wand in die Klippe."""
    if len(xy) < 2:
        return xy
    tg = np.gradient(xy, axis=0)
    tg /= np.maximum(np.linalg.norm(tg, axis=1, keepdims=True), 1e-12)
    nrm = np.column_stack([-tg[:, 1], tg[:, 0]])
    # Seite der Wand: wo die Abtastung höher liegt
    s0 = zelle
    za = abtasten(xy + nrm * s0)
    zb = abtasten(xy - nrm * s0)
    nrm = np.where((za >= zb)[:, None], nrm, -nrm)
    ts = np.arange(-6, 7, dtype=np.float64) * zelle * 0.25          # −1,5 … +1,5 Zellen
    kand = xy[:, None, :] + nrm[:, None, :] * ts[None, :, None]
    ok = abtasten(kand.reshape(-1, 2)).reshape(len(xy), len(ts)) <= L + eps
    aus = xy.copy()
    lo = np.full(len(xy), np.nan)
    hi = np.full(len(xy), np.nan)
    mitte = len(ts) // 2
    for i in range(len(xy)):
        o = ok[i]
        if not o.any():
            continue
        # nächster ok-Punkt zur Linie, dann zur Wand hin, solange ok
        k = min(np.flatnonzero(o), key=lambda q: (abs(q - mitte), -q))
        while k + 1 < len(ts) and o[k + 1]:
            k += 1
        lo[i] = ts[k]
        hi[i] = ts[k + 1] if k + 1 < len(ts) else np.nan
    weiter = np.isfinite(lo) & np.isfinite(hi)
    for _ in range(6):
        if not weiter.any():
            break
        tm = (lo + hi) / 2
        q = np.flatnonzero(weiter)
        zz = abtasten(xy[q] + nrm[q] * tm[q, None])
        gut = zz <= L + eps
        lo[q[gut]] = tm[q[gut]]
        hi[q[~gut]] = tm[q[~gut]]
    fertig = np.isfinite(lo)
    # nur zur Wand hin zurücknehmen, nie hinter den Ausgangspunkt (wo der schon auf L stand)
    t = np.where(np.isfinite(hi), lo - zurueck, lo)
    t = np.where(lo > 0, np.maximum(t, np.minimum(lo, 0.0)), t)
    aus[fertig] = xy[fertig] + nrm[fertig] * t[fertig, None]
    return aus


def _fuss_umlaeufe(tri, bx, bereich, abtasten, abtast: float, z_unten: float, d: float, aufmass: float,
                   stufe: float, tol: float) -> List[np.ndarray]:
    """Je waagrechter Fläche des Modells (unter der höchsten) die Höhenlinie der Fräserspitze knapp über ihr:
    die Mitte fährt dort, wo die Kugel auf der Fläche liegt und die Rundung am Fuss der Wände berührt."""
    from shapely.geometry import LineString
    from .. import hirch_relief
    if bereich is None or bereich.is_empty:
        return []
    oben, _u, zr = _raster(tri, bx, max(0.2, min(0.5, d / 12)))
    maske = np.isfinite(oben)
    if not maske.any():
        return []
    top = float(np.nanmax(oben))
    hs = [h for h in _flache_hoehen(oben, maske, zr, d) if h < top - 0.05]
    if not hs:
        return []
    Z, x0, y0, zelle = _cl_karte(bereich, abtasten, abtast, z_unten)
    innen = _puffer(bereich, 1e-6)
    # Nur, wo die Kugel wirklich am Fuss liegt (Höhe der Fläche ± FUSS_SPIEL). Wo sie es nicht kann (Engstellen,
    # Spitzen dünner Schaufeln, Kanten der Fläche), folgte der Umlauf bisher der Fläche senkrecht hinauf und wieder
    # hinunter – Impeller 26.10.: 25 von 32 min des Schlichtens waren senkrechte Hübe im Vorschub (3D → Fräsbahnen
    # 19:05). Dort wird der Umlauf jetzt getrennt; was er nicht fertig schlichtet, machen die übrigen Bahnen
    # (schlichten: der Bereich wird nur um das verkleinert, was die Stücke wirklich treffen – _fuss_fertig).
    pfade, hoehe_von = [], []
    for h in sorted(hs, reverse=True):
        L = h + aufmass + 0.02
        for ring in hirch_relief.konturen(Z, L, x0, y0, zelle):
            if len(ring) < 3:
                continue
            linie = LineString(np.vstack([ring, ring[:1]]))
            schnitt = linie.intersection(innen)
            for g in (getattr(schnitt, "geoms", [schnitt]) if not schnitt.is_empty else []):
                if g.geom_type != "LineString" or g.length < max(2 * zelle, 0.5):
                    continue
                xy = np.asarray(g.coords, np.float64)
                seg = np.hypot(*np.diff(xy, axis=0).T)
                t = np.concatenate([[0.0], np.cumsum(seg)])
                # dicht (Viertel-Zelle): an Engstellen liegen Stücke am Fuss und Stellen, wo die Kugel nicht hinunterkommt,
                # dicht beieinander – grob abgetastet fielen die Stücke am Fuss mit weg
                n = max(2, int(math.ceil(t[-1] / (max(zelle, 0.25) / 4))) + 1)
                tt = np.linspace(0.0, t[-1], n)
                xy = np.column_stack([np.interp(tt, t, xy[:, 0]), np.interp(tt, t, xy[:, 1])])
                # An die Klippe (zur Wand hin, solange die Spitze auf L bleibt): so liegt die Kugel in der Rundung am
                # Fuss und schlichtet sie auf einer Höhe ganz – auch ihren steilen Teil, ohne an der Wand auf und ab zu
                # fahren (3D 26.10.: sonst blieb dort ein Band; Fräsbahnen 26.10. nachts).
                xy = _an_die_klippe(xy, L, abtasten, zelle, zurueck=max(0.05, tol + 0.03))
                cl = abtasten(xy)
                z = np.maximum(cl, h + aufmass)
                # Getrennt, wo die Kugel nicht am Fuss liegt: höher (Engstelle, Schaufelspitze) oder tiefer – dort
                # schwebt sie neben einer Wand über einem tieferen Boden (die Höhenlinie der Karte liegt auf jeder
                # Klippe, die über L hinausreicht; die Gerade zwischen zwei solchen Punkten schnitt in die Wand).
                for st in _am_fuss(np.column_stack([xy, z]), h + aufmass, cl=cl):
                    pfade.append(st)
                    hoehe_von.append(h + aufmass)
    if not pfade:
        return []
    pfade = _verfeinern(pfade, abtasten, stufe)
    aus, aus_h = [], []
    for s_, h_ in zip(pfade, hoehe_von):
        for st in _am_fuss(s_, h_):
            if float(np.hypot(*np.diff(st[:, :2], axis=0).T).sum()) >= max(2 * zelle, 0.5):
                aus.append(_vereinfachen(st, tol))
                aus_h.append(h_)
    return _stuecke_ordnen(aus, aus_h)


def _stuecke_ordnen(stuecke: List[np.ndarray], hoehen: List[float]) -> List[np.ndarray]:
    """Stücke der Umläufe am Fuss je Höhe (oben zuerst) in kurzer Reihenfolge, die Fahrtrichtung bleibt
    (Gleichlauf). Bisher in der Folge der Höhenlinie: getrennt an jeder Schaufel sprang der Fräser zwischen
    Lücken auf beiden Seiten des Teils hin und her (3D → Fräsbahnen 27.10. 10:33, Punkt 2; Impeller: 60–77 mm
    quer über die Schaufeln, je über Z 0,5)."""
    from .bahn import kurze_reihenfolge
    if len(stuecke) < 3:
        return stuecke
    aus = []
    pos = None
    for h in sorted(set(hoehen), reverse=True):
        idx = [i for i, hh in enumerate(hoehen) if hh == h]
        a = [stuecke[i][0][:2] for i in idx]
        e = [stuecke[i][-1][:2] for i in idx]

        def d(i, j):                      # Weg von Ende i zu Anfang j und umgekehrt, gemittelt (für das 2-opt)
            return 0.5 * (math.dist(e[i], a[j]) + math.dist(e[j], a[i]))
        folge = kurze_reihenfolge(len(idx), d, (lambda i: 0.0) if pos is None else (lambda i: math.dist(pos, a[i])),
                                  max_n=150)
        aus += [stuecke[idx[k]] for k in folge]
        pos = aus[-1][-1][:2]
    return aus


def _fuss_fertig(fuss: List[np.ndarray], tri, bx, w: Dict, aufmass: float, tol: float):
    """Wo die Umläufe am Fuss das Modell wirklich fertig schlichten (Fräser simuliert, höchstens 0,1 mm über Modell
    + Aufmass) – nur das nehmen die übrigen Bahnen aus. Vorher: ein Band von 0,9 × Radius um jeden Umlauf; wo die
    Kugel am Fuss die Rundung nicht erreicht (Rundung grösser als der Fräser, Wand näher als der Radius), blieb dort
    stehen, was sonst niemand fräste (Fräsbahnen 26.10.)."""
    from .. import hirch_relief
    from shapely.geometry import Polygon
    zelle = 0.25
    oben, _u, zelle = _raster(tri, bx, zelle)
    H = np.full(oben.shape, 1e3, np.float32)
    form = _werkzeug_form(w)
    for f in fuss:
        if len(f) < 2:
            continue
        b3 = np.ascontiguousarray(f[:, :3], np.float64)
        hirch_relief.abtragen(H, float(bx[0]), float(bx[1]), zelle, b3, form, [b3[0, 0], b3[0, 1], b3[0, 2]], -1e3)
    # An steilen Stellen steht in einer Rasterzelle die höchste Stelle; tiefer in derselben Zelle kann noch Material
    # stehen, das der Umlauf nicht erreicht. Darum gegen die tiefste Höhe der Zelle und ihrer Nachbarn prüfen –
    # sonst gilt der steile Teil der Rundung als fertig und keine andere Bahn schlichtet ihn (3D 26.10.: Band am
    # Schaufelfuss, Impeller > 0,5 mm 1,6 → 2,8 %).
    o = np.where(np.isfinite(oben), oben, np.inf)
    tief = o.copy()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            tief = np.minimum(tief, np.roll(np.roll(o, dy, 0), dx, 1))
    fertig = np.isfinite(oben) & np.isfinite(tief) & (H <= tief + aufmass + 0.1)
    if not fertig.any():
        return Polygon()
    return _flaechen_aus(fertig.astype(np.float32), 0.5, bx[0], bx[1], zelle, tol)


FUSS_SPIEL = 0.2      # mm: so weit darf die Spitze beim Umlauf am Fuss über (oder unter) der Fläche liegen


def _am_fuss(pts: np.ndarray, h: float, hoch: float = FUSS_SPIEL, cl: Optional[np.ndarray] = None,
             unten: float = FUSS_SPIEL) -> List[np.ndarray]:
    """Teilstücke eines Umlaufs, auf denen die Fräserspitze von h − FUSS_SPIEL bis h + hoch liegt. cl: die
    Abtastung selbst (ohne «mindestens h») – dann fallen auch Punkte weg, an denen die Kugel über einem tieferen
    Boden schwebt."""
    dz = (pts[:, 2] if cl is None else np.asarray(cl)) - h
    gut = (dz >= -unten) & (dz <= max(FUSS_SPIEL, hoch))
    aus, i = [], 0
    while i < len(pts):
        if not gut[i]:
            i += 1
            continue
        j = i
        while j + 1 < len(pts) and gut[j + 1]:
            j += 1
        if j > i:
            aus.append(pts[i:j + 1])
        i = j + 1
    return aus



def _geneigt(tri, bx, r: float, tol: float):
    """Fläche der geneigten und gekrümmten Stellen des Modells (weder waagrecht noch senkrechte Wand), um den
    Radius des Fräsers erweitert – dort muss der Kugelfräser hin."""
    from scipy import ndimage
    zelle = 0.2
    oben, _u, zelle = _raster(tri, bx, zelle, max_punkte=4_000_000)
    m = np.isfinite(oben)
    h = np.where(m, oben, np.nan)
    gy, gx = np.gradient(np.where(m, oben, 0.0), zelle)
    neig = np.hypot(gx, gy)
    # Sprung zur Nachbarzelle: senkrechte Wand (an der Kante einer Rundung steigt es pro Zelle viel weniger)
    sprung = np.zeros_like(neig)
    for ax in (0, 1):
        dd = np.abs(np.diff(np.nan_to_num(h, nan=-1e3), axis=ax))
        if ax == 0:
            sprung[1:, :] = np.maximum(sprung[1:, :], dd)
            sprung[:-1, :] = np.maximum(sprung[:-1, :], dd)
        else:
            sprung[:, 1:] = np.maximum(sprung[:, 1:], dd)
            sprung[:, :-1] = np.maximum(sprung[:, :-1], dd)
    geneigt = m & (neig > 0.02) & (sprung < 10 * zelle)
    geneigt = ndimage.binary_opening(geneigt, iterations=1)
    if not geneigt.any():
        from shapely.geometry import Polygon
        return Polygon()
    fl = _flaechen_aus(geneigt.astype(np.float32), 0.5, bx[0], bx[1], zelle, tol)
    return fl.buffer(r + zelle, resolution=8)


def _adaptiv_rechnen(frei, d: float, last: float, tol: float):
    """Adaptive Bahnen für eine Ebene (cam.adaptiv = FreeCAD Adaptive): der Fräser ist nie voll
    umschlungen, die Last bleibt gleich (last % vom Ø). Leer, wenn kein Platz zum Eintauchen ist."""
    from .. import cam
    return cam.adaptiv(frei, d, d * 0.75, last / 100.0, tol, 0.0, True)


def _adaptiv_ausgeben(b, ergebnis, d: float, z: float, z_alt: float, winkel: float, vollnut: float,
                      bestand=None):
    """Eintauchen mit Helix von der Ebene darüber (die Fläche schrumpft nach unten – darüber ist
    schon frei); Übergänge im Freien knapp angehoben, durch Material abgehoben."""
    from .tasche import _helix
    hub = min(0.5, (z_alt - z) / 2) if z_alt > z else 0.5
    hr = d * 0.75 / 2
    def frei_bis(x, y, rand=0.0):              # höchstes Material unter dem Fräser, mindestens die Ebene darüber
        return max(z_alt, bestand.hoehe(x, y, rand)) if bestand is not None else z_alt
    for mitte, startpunkt, wege in ergebnis:
        b.abheben()
        b.anfahren(mitte[0] + hr, mitte[1])
        zf = frei_bis(mitte[0], mitte[1], hr)
        b.bis_frei(zf)
        b.eintauchen(zf, None)
        _helix(b, mitte, hr, zf, z, winkel, vollnut)
        b.fraesen(startpunkt[0], startpunkt[1], z, vollnut)
        for art, pts in wege:
            if not pts:
                continue
            if art == 0:                           # schneiden
                for q in pts:
                    b.fraesen(q[0], q[1], z)
            elif art in (1, 3):                    # Übergang im Freien, leicht angehoben
                oben = z + hub if art == 1 else max(z_alt, z + hub)
                b.fraesen(b.pos[0], b.pos[1], oben)
                for q in pts:
                    b.fraesen(q[0], q[1], oben)
                b.fraesen(pts[-1][0], pts[-1][1], z)
            else:                                  # durch Material: abheben
                b.abheben()
                b.anfahren(*pts[-1])
                zf = frei_bis(pts[-1][0], pts[-1][1])
                b.bis_frei(zf)
                b.eintauchen(zf, None)
                b.eintauchen(z, None)
    b.abheben()


def _parallel_pfade(bereich, richtung: str, schritt: float, abtast: float, abtasten, stufe: float,
                    tol: float) -> List[np.ndarray]:
    """Parallele Bahnen (entlang X, Y oder kreuzweise) im Bereich, im Zickzack, auf der Oberfläche."""
    from shapely.geometry import LineString
    gx0, gy0, gx1, gy1 = bereich.bounds

    def bahnen(entlang_x: bool):
        """Stücke je Bahn: [[(x0,y0),(x1,y1)], …] innerhalb des Bereichs, im Zickzack."""
        q0, q1 = (gy0, gy1) if entlang_x else (gx0, gx1)
        n = max(1, int(math.ceil((q1 - q0) / schritt)))
        zeilen = []
        for k in range(n + 1):
            q = min(q1, q0 + k * schritt)
            linie = LineString([(gx0 - 1, q), (gx1 + 1, q)] if entlang_x else [(q, gy0 - 1), (q, gy1 + 1)])
            schnitt = bereich.intersection(linie)
            stuecke = [g for g in getattr(schnitt, "geoms", [schnitt])
                       if g.geom_type == "LineString" and g.length > 1e-6]
            stuecke.sort(key=lambda g: g.coords[0][0] if entlang_x else g.coords[0][1])
            folge = [list(g.coords) for g in stuecke]
            for f in folge:
                f.sort(key=lambda c: c[0] if entlang_x else c[1])
            if k % 2:
                folge = [list(reversed(f)) for f in reversed(folge)]
            zeilen.append(folge)
        return zeilen

    richtungen = [True, False] if richtung == "kreuz" else [richtung != "y"]
    stuecke_alle: List[np.ndarray] = []      # (n, 2) Abtastpunkte je Stück
    for entlang_x in richtungen:
        for zeile in bahnen(entlang_x):
            for (a, e) in [(f[0], f[-1]) for f in zeile]:
                laenge = math.dist(a, e)
                n = max(2, int(math.ceil(laenge / abtast)) + 1)
                t = np.linspace(0.0, 1.0, n)
                stuecke_alle.append(np.column_stack([a[0] + (e[0] - a[0]) * t, a[1] + (e[1] - a[1]) * t]))
    if not stuecke_alle:
        return []
    alle = np.concatenate(stuecke_alle)
    z = abtasten(alle)
    roh = []
    i = 0
    for s in stuecke_alle:
        roh.append(np.column_stack([s, z[i:i + len(s)]]))
        i += len(s)
    roh = _verfeinern(roh, abtasten, stufe)
    return [_vereinfachen(s, tol) for s in roh]


def _cl_karte(bereich, abtasten, abtast: float, z_unten: float):
    """Höhe der Fräserspitze (Drop-Cutter) auf einem Raster über dem Bereich, 3 Zellen Rand:
    (Z (ny, nx), x0, y0, zelle). Höhenlinien dieser Karte sind die Bahnen der Fräsermitte."""
    zelle = max(0.1, min(0.5, abtast))
    gx0, gy0, gx1, gy1 = bereich.bounds
    while (gx1 - gx0) / zelle * (gy1 - gy0) / zelle > 6_000_000:
        zelle *= 1.25
    x0, y0 = gx0 - 3 * zelle, gy0 - 3 * zelle
    nx = int(math.ceil((gx1 - gx0) / zelle)) + 7
    ny = int(math.ceil((gy1 - gy0) / zelle)) + 7
    gx, gy = np.meshgrid(x0 + np.arange(nx) * zelle, y0 + np.arange(ny) * zelle)
    z = abtasten(np.column_stack([gx.ravel(), gy.ravel()])).reshape(ny, nx)
    return np.ascontiguousarray(z, np.float32), x0, y0, zelle


def _steil_flach(cl, bereich, grenz: float, schritt: float):
    """Bereich teilen: steil (Neigung ≥ grenz, für Höhenlinien) und flach (für parallele Bahnen);
    beide um einen Bahnabstand überlappend, damit an der Grenze nichts stehen bleibt."""
    Z, x0, y0, zelle = cl
    gy, gx = np.gradient(Z.astype(np.float64), zelle)
    neigung = np.arctan(np.hypot(gx, gy))
    steil_r = (neigung >= grenz).astype(np.float32)
    steil = _flaechen_aus(steil_r, 0.5, x0, y0, zelle, zelle * 0.5)
    # winzige Inseln (einzelne Zellen) weglassen
    steil = steil.buffer(-zelle, resolution=4).buffer(zelle, resolution=4)
    if steil.is_empty:
        return steil, bereich
    flach = bereich.difference(steil.buffer(-schritt / 2, resolution=8))
    return bereich.intersection(steil.buffer(schritt / 2, resolution=8)), flach


def _hoehenlinien(cl, bereich, dz: float, abtasten, stufe: float, tol: float, start=None, genau=None,
                  hoehen=None) -> List[np.ndarray]:
    """Höhenlinien (Waterline): die Fräsermitte umrundet das Modell auf festen Höhen, von oben nach
    unten im Abstand dz. Nur innerhalb des Bereichs; die Höhe jedes Punkts ist mindestens die der
    Abtastung (nie ins Modell)."""
    from shapely.geometry import LineString
    from .. import hirch_relief
    if bereich is None or bereich.is_empty:
        return []
    if cl is not None:
        Z, x0, y0, zelle = cl
        zmax, zmin = float(np.nanmax(Z)), float(np.nanmin(Z))
    else:                                    # nur mit «genau»: Höhen aus dem Modell
        zmax, zmin = hoehen
        zelle = 0.25
    if zmax - zmin < 1e-3:
        return []
    pfade: List[np.ndarray] = []
    stufe_von: List[int] = []
    innen = _puffer(bereich, 1e-6)
    stufen = int(math.floor((zmax - zmin) / dz))
    for k in range(1, stufen + 1):
        L = zmax - k * dz
        if L <= zmin + 1e-3:
            break
        ringe = genau(L) if genau is not None else hirch_relief.konturen(Z, L, x0, y0, zelle)
        for ring in ringe:
            if len(ring) < 3:
                continue
            linie = LineString(np.vstack([ring, ring[:1]]))
            schnitt = linie.intersection(innen)
            teile = getattr(schnitt, "geoms", [schnitt]) if not schnitt.is_empty else []
            for g in teile:
                if g.geom_type != "LineString" or g.length < max(2 * zelle, 0.3):
                    continue
                xy = np.asarray(g.coords, np.float64)
                # gleichmässig nachtasten: Punkte im Abstand ~ zelle
                seg = np.hypot(*np.diff(xy, axis=0).T)
                t = np.concatenate([[0.0], np.cumsum(seg)])
                n = max(2, int(math.ceil(t[-1] / max(zelle, 0.25))) + 1)
                tt = np.linspace(0.0, t[-1], n)
                xy = np.column_stack([np.interp(tt, t, xy[:, 0]), np.interp(tt, t, xy[:, 1])])
                # An die Klippe schieben (die Linie aus der Karte liegt an Wänden bis eine Zelle daneben: die Kugel
                # berührte die Wand dort nicht) und trennen, wo die Spitze nicht auf L bleiben kann (Engstellen,
                # Kanten): dort kletterte die Höhenlinie bisher senkrecht hinauf und wieder hinunter – «kombiniert»
                # am Impeller 40 von 64 min (3D 26.10. 22:01). Neben Wänden schwebt sie (die Kugel schneidet die Wand
                # mit der Seite) – das bleibt.
                xy = _an_die_klippe(xy, L, abtasten, zelle, zurueck=max(0.05, tol + 0.03))
                cl = abtasten(xy)
                z = np.maximum(cl, L)
                gut = cl <= L + FUSS_SPIEL
                i = 0
                while i < len(xy):
                    if not gut[i]:
                        i += 1
                        continue
                    j = i
                    while j + 1 < len(xy) and gut[j + 1]:
                        j += 1
                    if j > i:
                        pfade.append(np.column_stack([xy[i:j + 1], z[i:j + 1]]))
                        stufe_von.append(k)
                    i = j + 1
    pfade = _verfeinern(pfade, abtasten, stufe) if pfade else []
    # was das Verdichten an Klippen eingefügt hat und höher liegt als die Höhe der Linie: dort trennen
    neu_p, neu_k = [], []
    for s_, k_ in zip(pfade, stufe_von):
        for st in _am_fuss(s_, zmax - k_ * dz, hoch=FUSS_SPIEL, unten=1e9):
            if len(st) >= 2:
                neu_p.append(st)
                neu_k.append(k_)
    pfade, stufe_von = neu_p, neu_k
    pfade = [_vereinfachen(s, tol) for s in pfade]
    return _hoehen_reihenfolge(pfade, stufe_von, start)


def _gleichmaessig(ring: np.ndarray, n: int) -> np.ndarray:
    """Geschlossenen Ring (ohne Wiederholung des ersten Punkts) mit n Punkten gleichmässig nach der Länge belegen"""
    xy = np.vstack([ring, ring[:1]])
    seg = np.hypot(*np.diff(xy, axis=0).T)
    t = np.concatenate([[0.0], np.cumsum(seg)])
    if t[-1] <= 1e-9:
        return np.repeat(ring[:1], n, axis=0)
    tt = np.linspace(0.0, t[-1], n, endpoint=False)
    return np.column_stack([np.interp(tt, t, xy[:, 0]), np.interp(tt, t, xy[:, 1])])


def _flaeche_vz(xy: np.ndarray) -> float:
    """Fläche mit Vorzeichen (> 0: gegen den Uhrzeigersinn)"""
    x, y = xy[:, 0], xy[:, 1]
    return 0.5 * float(np.dot(x, np.roll(y, -1)) - np.dot(y, np.roll(x, -1)))


def _ab_naechstem(ring: np.ndarray, pos) -> np.ndarray:
    i = int(np.argmin(np.hypot(ring[:, 0] - pos[0], ring[:, 1] - pos[1])))
    return np.vstack([ring[i:], ring[:i]])


def _helix(ringe: List[np.ndarray], hoehen: List[float], abtasten, abtast: float) -> np.ndarray:
    """Ringe einer Form (oben nach unten, gleiche Richtung) zu einer Schraubenlinie: von Ring k zu Ring k+1
    in einer Umrundung, Lage und Höhe dabei gleitend übergeblendet (Peter 26.10.: keine Ansatzstelle, kein
    senkrechtes Zustellen); am Ende eine volle Runde auf der untersten Höhe. Nie tiefer als die Abtastung."""
    stuecke = []
    pos = None
    vor = None
    for k, ring in enumerate(ringe):
        n = max(24, int(math.ceil(LineString_laenge(ring) / max(abtast, 0.05))))
        r = _gleichmaessig(ring if pos is None else _ab_naechstem(ring, pos), n)
        if vor is not None:
            import shapely
            from shapely.geometry import LinearRing
            a = _gleichmaessig(vor, n)                       # vor beginnt am Ende der letzten Runde
            # Zuordnung über den nächsten Punkt des tieferen Rings (nicht über die Länge – sonst schneidet die
            # Überblendung an Stellen, wo die Ringe verschieden lang sind, quer durch die Form)
            ziel = LinearRing(ring)
            pa = shapely.points(a)
            b = shapely.get_coordinates(shapely.line_interpolate_point(ziel, shapely.line_locate_point(ziel, pa)))
            t = np.linspace(0.0, 1.0, n, endpoint=False)[:, None]
            xy = a * (1 - t) + b * t
            z = hoehen[k - 1] + (hoehen[k] - hoehen[k - 1]) * t[:, 0]
            # wo die Überblendung doch ans Modell stösst (Abtastung höher als die Schraubenlinie), auf dem tieferen
            # Ring fahren – nie über die Form hinweg
            za = abtasten(xy)
            weg = za > z + 0.05
            if weg.any():
                xy[weg] = b[weg]
            stuecke.append(np.column_stack([xy, z]))
            r = _gleichmaessig(_ab_naechstem(ring, b[-1]), n)
        vor = r
        pos = r[0]
    # Schluss: eine volle Runde unten
    unten = np.vstack([vor, vor[:1]])
    stuecke.append(np.column_stack([unten, np.full(len(unten), hoehen[-1])]))
    p = np.vstack(stuecke)
    p[:, 2] = np.maximum(p[:, 2], abtasten(p[:, :2]))
    return p


def LineString_laenge(ring: np.ndarray) -> float:
    xy = np.vstack([ring, ring[:1]])
    return float(np.hypot(*np.diff(xy, axis=0).T).sum())


def _konturen_je_form(cl, steil, dz: float, abtasten, abtast: float, stufe: float, tol: float,
                      start, ohne_senkrecht: bool = False) -> List[np.ndarray]:
    """Konturen je Form (Peter 26.10.): jede Form (Schaufel, Nabe, Taschenwand) für sich von oben nach unten,
    die Höhenlinien zu einer Schraubenlinie verbunden; geschlossen nur, wo die Höhenlinie ganz im steilen
    Bereich liegt – sonst wie bisher Stücke je Höhe. Gleichlauf: um Erhebungen im Uhrzeigersinn, in Taschen
    dagegen (Spindel M3)."""
    from shapely.geometry import LineString, Polygon
    from .. import hirch_relief
    if steil is None or steil.is_empty:
        return []
    Z, x0, y0, zelle = cl
    zmax, zmin = float(np.nanmax(Z)), float(np.nanmin(Z))
    if zmax - zmin < 1e-3:
        return []
    innen = steil.buffer(1e-6)
    stufen = int(math.floor((zmax - zmin) / dz))
    # «Was schon auf Mass ist, auslassen»: senkrechte Wände hat «Böden und Wände» schon. Die Höhenlinie aus dem Raster
    # liegt dort auf der Klippe zwischen zwei Zellen; an die richtige Stelle geschoben (_auf_hoehe) schwebt der Fräser
    # neben der Wand über dem Boden – solche Stellen fallen weg. Rundungen und geneigte Flächen berührt er auf der Höhe.
    ketten: List[dict] = []          # offen: {"ringe", "hoehen", "poly", "aussen"}
    fertige: List[dict] = []
    offen_stuecke: List[np.ndarray] = []
    stufe_von: List[int] = []

    def hoehe_in(poly) -> float:
        q = poly.representative_point()
        i = int(round((q.x - x0) / zelle))
        j = int(round((q.y - y0) / zelle))
        i = min(max(i, 0), Z.shape[1] - 1)
        j = min(max(j, 0), Z.shape[0] - 1)
        return float(Z[j, i])

    for k in range(1, stufen + 1):
        L = zmax - k * dz
        if L <= zmin + 1e-3:
            break
        neue: List[dict] = []
        for ring in hirch_relief.konturen(Z, L, x0, y0, zelle):
            if len(ring) < 3:
                continue
            ring = np.asarray(ring, np.float64)
            linie = LineString(np.vstack([ring, ring[:1]]))
            if linie.length < max(4 * zelle, 1.0):
                continue
            ring = _auf_hoehe(ring, L, abtasten, zelle)
            if ohne_senkrecht:
                za = abtasten(ring)
                kl = za < L - 0.3                     # schwebt: senkrechte Wand, schon auf Mass
                if kl.mean() > 0.6:
                    continue
                if kl.any():
                    # nur die Stücke, wo der Fräser das Modell auf dieser Höhe berührt – als offene Stücke
                    gut = ~kl
                    zyk = np.concatenate([gut, gut[:1]])
                    pts = np.vstack([ring, ring[:1]])
                    beginn = None
                    for q in range(len(zyk)):
                        if zyk[q] and beginn is None:
                            beginn = q
                        if (not zyk[q] or q == len(zyk) - 1) and beginn is not None:
                            ende = q if zyk[q] else q - 1
                            if ende - beginn >= 2:
                                g = LineString(pts[beginn:ende + 1]).intersection(innen)
                                for gg in (getattr(g, "geoms", [g]) if not g.is_empty else []):
                                    if gg.geom_type == "LineString" and gg.length >= max(2 * zelle, 0.5):
                                        xy = _dicht(np.asarray(gg.coords, np.float64), max(zelle, 0.25))
                                        offen_stuecke.append(np.column_stack([xy, np.maximum(abtasten(xy), L)]))
                                        stufe_von.append(k)
                            beginn = None
                    continue
            linie = LineString(np.vstack([ring, ring[:1]]))
            if innen.contains(linie):
                poly = Polygon(ring)
                if not poly.is_valid:
                    poly = poly.buffer(0)
                if poly.is_empty:
                    continue
                aussen = hoehe_in(poly) > L          # Erhebung: Material innen
                neue.append({"ring": ring, "poly": poly, "aussen": aussen})
                continue
            schnitt = linie.intersection(innen)
            for g in (getattr(schnitt, "geoms", [schnitt]) if not schnitt.is_empty else []):
                if g.geom_type != "LineString" or g.length < max(2 * zelle, 0.3):
                    continue
                xy = _dicht(np.asarray(g.coords, np.float64), max(zelle, 0.25))
                offen_stuecke.append(np.column_stack([xy, np.maximum(abtasten(xy), L)]))
                stufe_von.append(k)
        # an Ketten der Höhe darüber hängen: eins zu eins, die Ringe umschliessen einander und liegen nah
        frei = list(range(len(ketten)))
        weiter: List[dict] = []
        genommen = set()
        for nr in neue:
            treffer = []
            for ci in frei:
                c = ketten[ci]
                if c["aussen"] != nr["aussen"]:
                    continue
                a, bpol = c["poly"], nr["poly"]
                klein, gross = (a, bpol) if a.area <= bpol.area else (bpol, a)
                if klein.area <= 0 or klein.intersection(gross).area < 0.9 * klein.area:
                    continue
                if a.exterior.hausdorff_distance(bpol.exterior) > max(4 * dz, 2 * zelle, 1.0) + abs(
                        math.sqrt(a.area) - math.sqrt(bpol.area)):
                    continue
                treffer.append(ci)
            if len(treffer) == 1 and treffer[0] not in genommen and \
                    sum(1 for m in neue if m is not nr and _passt(ketten[treffer[0]], m)) == 0:
                c = ketten[treffer[0]]
                genommen.add(treffer[0])
                c["ringe"].append(nr["ring"])
                c["hoehen"].append(L)
                c["poly"] = nr["poly"]
                weiter.append(c)
            else:
                weiter.append({"ringe": [nr["ring"]], "hoehen": [L], "poly": nr["poly"], "aussen": nr["aussen"],
                               "stufe": k})
        for ci, c in enumerate(ketten):
            if ci not in genommen:
                fertige.append(c)
        ketten = weiter
    fertige.extend(ketten)
    pfade: List[np.ndarray] = []
    for c in fertige:
        # Gleichlauf: Erhebung im Uhrzeigersinn (von oben), Tasche gegen den Uhrzeigersinn
        ringe = []
        for r in c["ringe"]:
            ccw = _flaeche_vz(r) > 0
            if ccw == c["aussen"]:
                r = r[::-1]
            ringe.append(r)
        pfade.append(_helix(ringe, c["hoehen"], abtasten, abtast))
        stufe_von.append(c.get("stufe", 1))
    fest = set(range(len(offen_stuecke), len(offen_stuecke) + len(pfade)))
    pfade = offen_stuecke + pfade
    if not pfade:
        return []
    pfade = _verfeinern(pfade, abtasten, stufe)
    pfade = [_vereinfachen(p_, tol) for p_ in pfade]
    return _hoehen_reihenfolge(pfade, stufe_von, start, fest)


def _passt(kette: dict, nr: dict) -> bool:
    """Gehört der neue Ring auch zu dieser Kette (dann teilt sich die Form – keine Schraubenlinie weiter)?"""
    if kette["aussen"] != nr["aussen"]:
        return False
    a, b = kette["poly"], nr["poly"]
    klein, gross = (a, b) if a.area <= b.area else (b, a)
    return klein.area > 0 and klein.intersection(gross).area >= 0.9 * klein.area


def _entlang_kontur(flach, abtasten, schritt: float, abtast: float, stufe: float, tol: float,
                    start) -> List[np.ndarray]:
    """Flache und leicht gewölbte Stellen entlang ihrer Kontur (Peter 26.10.): Versatzringe der Fläche von
    aussen nach innen im Bahnabstand, die Höhe aus der Abtastung; ist es je Schritt nur ein Ring (z. B. eine
    Kuppel), werden die Ringe zu einer Spirale verbunden – ohne Abheben."""
    if flach is None or flach.is_empty:
        return []
    pfade: List[np.ndarray] = []
    stufe_von: List[int] = []
    spiralen = set()
    for teil in getattr(flach, "geoms", [flach]):
        if teil.area < schritt * schritt:
            continue
        ringe_je = []
        f = teil
        k = 0
        while not f.is_empty and k < 2000:
            rr = []
            for poly in getattr(f, "geoms", [f]):
                for r in [poly.exterior] + list(poly.interiors):
                    xy = np.asarray(r.coords, np.float64)[:-1]
                    if len(xy) >= 3 and LineString_laenge(xy) > 2 * abtast:
                        rr.append(xy)
            if rr:
                ringe_je.append(rr)
            f = f.buffer(-schritt, resolution=8)
            k += 1
        einzeln = all(len(rr) == 1 for rr in ringe_je)
        if einzeln and len(ringe_je) > 1:
            # Spirale: Ring um Ring übergeblendet, gleiche Richtung
            ringe = []
            for (r,) in ringe_je:
                ringe.append(r if _flaeche_vz(r) > 0 else r[::-1])
            dummy = [0.0] * len(ringe)
            sp = _helix(ringe, dummy, abtasten, abtast)
            sp[:, 2] = abtasten(sp[:, :2])
            pfade.append(sp)
            stufe_von.append(0)
            spiralen.add(len(pfade) - 1)
        else:
            for k, rr in enumerate(ringe_je):
                for r in rr:
                    xy = _dicht(np.vstack([r, r[:1]]), max(abtast, 0.1))
                    pfade.append(np.column_stack([xy, abtasten(xy)]))
                    stufe_von.append(k)
    if not pfade:
        return []
    pfade = _verfeinern(pfade, abtasten, stufe)
    pfade = [_vereinfachen(p_, tol) for p_ in pfade]
    return _hoehen_reihenfolge(pfade, stufe_von, start, spiralen)


def _hoehen_reihenfolge(pfade: List[np.ndarray], stufe_von: List[int], start, fest=None) -> List[np.ndarray]:
    """Höhenlinien in kurzer Reihenfolge: ein Stück ist frei, wenn alle höheren Stücke, die in
    seinem Umriss liegen, schon gefräst sind (erst oben, dann darunter); unter den freien das
    nächste – so wird ein Hügel von oben bis unten fertig, bevor es zum nächsten geht. Offene Stücke
    dürfen rückwärts gefahren werden."""
    n = len(pfade)
    if n < 2 or n > 4000:
        return pfade
    bb = np.array([[p[:, 0].min(), p[:, 1].min(), p[:, 0].max(), p[:, 1].max()] for p in pfade])
    st = np.asarray(stufe_von)
    # über[i]: höhere Stücke, deren Rechteck in dem von i liegt
    ueber = []
    for i in range(n):
        drin = ((st < st[i]) & (bb[:, 0] >= bb[i, 0] - 0.01) & (bb[:, 1] >= bb[i, 1] - 0.01) &
                (bb[:, 2] <= bb[i, 2] + 0.01) & (bb[:, 3] <= bb[i, 3] + 0.01))
        ueber.append(set(np.flatnonzero(drin).tolist()))
    fertig = np.zeros(n, bool)
    offen_ueber = [len(u) for u in ueber]
    unter = [[] for _ in range(n)]
    for i, u in enumerate(ueber):
        for j in u:
            unter[j].append(i)
    pos = np.asarray(start[:2] if start is not None else (0.0, 0.0), float)
    aus = []
    for _ in range(n):
        frei = np.flatnonzero(~fertig & (np.asarray(offen_ueber) == 0))
        if not len(frei):
            frei = np.flatnonzero(~fertig)
        best, best_d, umkehren = -1, math.inf, False
        for i in frei:
            p = pfade[i]
            zu = math.dist(p[0][:2], p[-1][:2]) < 1e-6
            if zu:
                d = float(np.min(np.hypot(p[:, 0] - pos[0], p[:, 1] - pos[1])))
                r = False
            elif fest is not None and int(i) in fest:          # Schraubenlinie: nur von oben nach unten
                d, r = math.dist(p[0][:2], pos), False
            else:
                d0, d1 = math.dist(p[0][:2], pos), math.dist(p[-1][:2], pos)
                d, r = (d1, True) if d1 < d0 else (d0, False)
            if d < best_d:
                best, best_d, umkehren = int(i), d, r
        p = pfade[best][::-1] if umkehren else pfade[best]
        aus.append(p)
        fertig[best] = True
        for k in unter[best]:
            offen_ueber[k] -= 1
        pos = np.asarray(p[-1][:2], float) if math.dist(p[0][:2], p[-1][:2]) > 1e-6 else \
            np.asarray(p[int(np.argmin(np.hypot(p[:, 0] - pos[0], p[:, 1] - pos[1])))][:2], float)
    return aus


def _inseln_reihenfolge(pfade: List[np.ndarray], bereich, start) -> List[np.ndarray]:
    """Parallele Bahnen bei mehreren getrennten Flächen (z. B. Restmaterial): jede Fläche für sich
    im Zickzack fertig, die Flächen in kurzer Reihenfolge (sonst springt jede Zeile von Insel zu Insel)."""
    teile = list(getattr(bereich, "geoms", [bereich]))
    if len(teile) < 2 or len(pfade) < 2:
        return pfade
    from shapely.geometry import Point
    gruppen: List[List[np.ndarray]] = [[] for _ in teile]
    rest = []
    for p in pfade:
        m = p[len(p) // 2]
        q = Point(float(m[0]), float(m[1]))
        i = min(range(len(teile)), key=lambda k: teile[k].distance(q))
        gruppen[i].append(p)
    gruppen = [g for g in gruppen if g]
    pos = np.asarray(start[:2] if start is not None else (0.0, 0.0), float)
    aus: List[np.ndarray] = []
    while gruppen:
        best, best_d, umkehren = 0, math.inf, False
        for i, g in enumerate(gruppen):
            d0, d1 = math.dist(g[0][0][:2], pos), math.dist(g[-1][-1][:2], pos)
            d, r = (d1, True) if d1 < d0 else (d0, False)
            if d < best_d:
                best, best_d, umkehren = i, d, r
        g = gruppen.pop(best)
        if umkehren:
            g = [p[::-1] for p in reversed(g)]
        aus.extend(g)
        pos = np.asarray(aus[-1][-1][:2], float)
    return aus + rest


def _kehlen(bereich, abtasten, d: float, schritt: float, abtast: float, z_unten: float, stufe: float,
            tol: float, start, p: Dict, warnungen: List[str]) -> List[np.ndarray]:
    """Kehlen nachfahren (Pencil): wo der Fräser das Modell an zwei Stellen zugleich berührt (Kehlen,
    Innenecken, Fuss von Wänden), hat die Höhe der Fräserspitze einen Knick nach oben (Tal). Diese
    Stellen auf einem feinen Raster finden, zur Mittellinie verdünnen und genau entlangfahren;
    mit «kehlen_bahnen» > 0 zusätzlich so viele Bahnen beidseits im Bahnabstand."""
    from shapely.geometry import LineString
    from .. import hirch_relief
    zelle = max(0.04, min(0.2, d / 30))
    gx0, gy0, gx1, gy1 = bereich.bounds
    while (gx1 - gx0) / zelle * (gy1 - gy0) / zelle > 8_000_000:
        zelle *= 1.25
    # um einen Bruchteil einer Zelle verschoben: liegt der Bereich auf ganzen mm (genauer Umriss eines Quaders),
    # fielen Rasterpunkte sonst genau auf «Fräser berührt die Kante» (zwei gleich gute Höhen)
    x0, y0 = gx0 - 2 * zelle + 0.317 * zelle, gy0 - 2 * zelle + 0.317 * zelle
    nx = int(math.ceil((gx1 - gx0) / zelle)) + 5
    ny = int(math.ceil((gy1 - gy0) / zelle)) + 5
    gx, gy = np.meshgrid(x0 + np.arange(nx) * zelle, y0 + np.arange(ny) * zelle)
    Z = abtasten(np.column_stack([gx.ravel(), gy.ravel()])).reshape(ny, nx)
    # Knick: Änderung der Steigung über eine Zelle, in vier Richtungen, nur Täler (positiv)
    knick = math.tan(math.radians(max(2.0, min(80.0, float(p.get("kehlen_winkel") or 15.0)))))
    tal = np.zeros(Z.shape, bool)
    for (di, dj) in ((1, 0), (0, 1), (1, 1), (1, -1)):
        h = zelle * math.hypot(di, dj)
        a = np.roll(np.roll(Z, dj, 0), di, 1)
        b = np.roll(np.roll(Z, -dj, 0), -di, 1)
        tal |= (a + b - 2 * Z) / h > knick
    tal[:2, :] = tal[-2:, :] = False
    tal[:, :2] = tal[:, -2:] = False
    # nur im Bereich
    import shapely
    jj_, ii_ = np.nonzero(tal)
    if len(jj_):
        geb = _puffer(bereich, zelle)
        shapely.prepare(geb)
        tal[jj_, ii_] = shapely.contains_xy(geb, gx[jj_, ii_], gy[jj_, ii_])
    if not tal.any():
        warnungen.append("Keine Kehlen gefunden – der Fräser passt überall hinein (oder der Knickwinkel ist zu gross).")
        return []
    from scipy import ndimage
    tal = ndimage.binary_closing(tal, iterations=1) | tal
    linien = hirch_relief.mittellinien(tal.astype(np.uint8), x0, y0, zelle, max(3, int(1.0 / zelle)))
    n_beid = max(0, int(p.get("kehlen_bahnen") or 0))
    stuecke = []
    for l in linien:
        if len(l) < 2:
            continue
        ls = LineString(l).simplify(zelle * 0.7)
        if ls.length < max(1.0, d / 2):
            continue
        teile = [ls]
        for k in range(1, n_beid + 1):
            for seite in (1, -1):
                o = ls.offset_curve(seite * k * schritt, join_style=1)
                teile.extend(g for g in getattr(o, "geoms", [o]) if g.length > 0.5)
        for g in teile:
            stuecke.append(_dicht(np.asarray(g.coords, np.float64), abtast))
    if not stuecke:
        warnungen.append("Keine Kehlen gefunden – der Fräser passt überall hinein (oder der Knickwinkel ist zu gross).")
        return []
    k_fenster = max(2, int(math.ceil(min(d / 2, 1.5) / zelle)))

    def ins_tal(xy):
        """Jeden Punkt quer zur Bahn (±k Zellen) in die tiefste Stelle schieben, bei gleicher Tiefe
        die nächste – so liegt die Bahn genau im Tal, auch an Ecken; entlang des Tals bleibt er."""
        if len(xy) < 2:
            return xy
        tg = np.gradient(xy, axis=0)
        tg /= np.maximum(np.linalg.norm(tg, axis=1, keepdims=True), 1e-12)
        nrm = np.column_stack([-tg[:, 1], tg[:, 0]])
        o = np.arange(-k_fenster, k_fenster + 1) * zelle
        qx = xy[:, 0:1] + nrm[:, 0:1] * o[None, :]
        qy = xy[:, 1:2] + nrm[:, 1:2] * o[None, :]
        ii = np.clip(np.rint((qx - x0) / zelle).astype(int), 0, nx - 1)
        jj = np.clip(np.rint((qy - y0) / zelle).astype(int), 0, ny - 1)
        w = Z[jj, ii] + np.abs(o)[None, :] * 1e-6
        k = np.argmin(w, axis=1)
        r_ = np.arange(len(xy))
        # die Rasterzelle selbst (dort ist die Höhe bekannt), nicht der Punkt dazwischen
        return np.column_stack([x0 + ii[r_, k] * zelle, y0 + jj[r_, k] * zelle])
    if n_beid == 0:
        stuecke = [ins_tal(s_) for s_ in stuecke]
    stuecke = _hoehen_reihenfolge(stuecke, [0] * len(stuecke), start)
    # nahe Enden durch das Tal verbinden (an Ecken bricht die Mittellinie oft ab)
    zusammen: List[np.ndarray] = [stuecke[0]]
    for s_ in stuecke[1:]:
        a, e = zusammen[-1][-1], s_[0]
        if math.dist(a, e) <= max(d, 2 * zelle):
            bruecke = _dicht(np.array([a, e]), zelle)
            zusammen[-1] = np.vstack([zusammen[-1], (ins_tal(bruecke) if n_beid == 0 else bruecke)[1:-1], s_])
        else:
            zusammen.append(s_)
    def glatt(xy):
        # Treppen der Rasterzellen glätten (gleitender Mittelwert über 5 Punkte, Enden bleiben)
        if len(xy) < 7 or n_beid:
            return xy
        k = np.ones(5) / 5
        aus = xy.copy()
        for c in (0, 1):
            aus[2:-2, c] = np.convolve(xy[:, c], k, mode="valid")
        return aus
    pfade = []
    for xy in zusammen:
        g = glatt(xy)
        z = abtasten(g)
        if g is not xy:
            # das Glätten schneidet Ecken ab – um eine Aussenecke (Turm) kommt der Fräser so näher als den Radius
            # an die Kante und stiege auf das Modell (04.11.: Ring um den Turm kletterte 5 mm hoch): dort die
            # Punkte im Tal behalten
            z0 = abtasten(xy)
            hoch = z > z0 + 0.02
            if hoch.any():
                g = np.where(hoch[:, None], xy, g)
                z = np.where(hoch, z0, z)
        pfade.append(np.column_stack([g, z]))
    pfade = _verfeinern(pfade, abtasten, stufe)
    return [_vereinfachen(s_, tol) for s_ in pfade]


def _dicht(xy: np.ndarray, abstand: float) -> np.ndarray:
    """Linienzug gleichmässig mit Punkten im Abstand ≈ abstand belegen (Enden bleiben)."""
    seg = np.hypot(*np.diff(xy, axis=0).T)
    t = np.concatenate([[0.0], np.cumsum(seg)])
    n = max(2, int(math.ceil(t[-1] / max(abstand, 0.02))) + 1)
    tt = np.linspace(0.0, t[-1], n)
    return np.column_stack([np.interp(tt, t, xy[:, 0]), np.interp(tt, t, xy[:, 1])])


def _ebenen(cl, umriss, bereich, w: Dict, abtasten, schritt: float, aufmass: float, tol: float,
            warnungen: List[str]) -> List[np.ndarray]:
    """Waagrechte Flächen des Modells (Böden, Absätze) mit dem Schaftfräser ausräumen: je Fläche
    die Stellen, an denen der Fräser genau auf ihrer Höhe steht, in Ringen von aussen nach innen."""
    Z, x0, y0, zelle = cl
    if (w.get("typ") or "schaft") not in ("schaft", "eckradius"):
        warnungen.append("Waagrechte Flächen: dafür eignet sich ein Schaftfräser (flach) am besten.")
    # Höhen der waagrechten Flächen: Häufungen in der Karte, wo sie flach ist
    gy, gx = np.gradient(Z.astype(np.float64), zelle)
    flach = np.hypot(gx, gy) < 0.02
    werte = np.round(Z[flach] / 0.01) * 0.01
    if not len(werte):
        warnungen.append("Keine waagrechten Flächen gefunden.")
        return []
    hoehen, anz = np.unique(werte, return_counts=True)
    min_zellen = max(4, int((schritt * schritt) / (zelle * zelle)))
    pfade: List[np.ndarray] = []
    auf_modell = bereich.intersection(umriss)      # der Boden um das Modell gehört nicht dazu
    for h, n in sorted(zip(hoehen, anz), key=lambda t: -t[0]):
        if n < min_zellen:
            continue
        auf = (np.abs(Z - h) < 0.015).astype(np.float32)
        fl = _flaechen_aus(auf, 0.5, x0, y0, zelle, zelle * 0.5).intersection(auf_modell)
        fl = fl.buffer(-zelle, resolution=8)               # nicht an den Rand der Fläche, der steigt
        # Treppen der Rasterzellen am Rand glätten (Peter 04.11.: Maschine ruckelte an schrägen Kanten); die Höhe
        # kommt danach aus der Abtastung (nie tiefer als die Fläche, nie ins Modell)
        fl = fl.simplify(zelle).buffer(0)
        if fl.is_empty:
            continue
        ring_fl = fl
        while not ring_fl.is_empty:
            for poly in getattr(ring_fl, "geoms", [ring_fl]):
                for r in [poly.exterior] + list(poly.interiors):
                    xy = np.asarray(r.coords, np.float64)
                    if len(xy) < 3:
                        continue
                    z = np.maximum(abtasten(xy), h)
                    pfade.append(_vereinfachen(np.column_stack([xy, z]), tol))
            ring_fl = ring_fl.buffer(-schritt, resolution=8)
    if not pfade:
        warnungen.append("Keine waagrechten Flächen gefunden.")
    return pfade


def _sehnen_sicher(stuecke: List[np.ndarray], abtasten, abstand: float = 0.05, spiel: float = 0.02,
                   klippe: float = 0.3) -> List[np.ndarray]:
    """Jede Gerade zwischen zwei Bahnpunkten fein nachtasten (alle «abstand» mm, mindestens in der Mitte). Läge die
    Fräserspitze irgendwo tiefer als die Abtastung erlaubt (mehr als «spiel»): bei wenig (gewölbte Fläche) den
    schlimmsten Punkt mit seiner Abtasthöhe einfügen und neu prüfen; bei mehr als «klippe» (die Gerade schneidet in
    eine Wand) senkrecht hinauf, auf der höchsten Abtasthöhe hinüber, senkrecht hinunter. _verfeinern verdichtet nur,
    wo die Höhe zwischen zwei Punkten springt – liegen beide Enden auf derselben Höhe (Höhenlinie oder Umlauf neben
    einer Wand), schnitt die Gerade an Ecken und Klippen in die Wand (Fräsbahnen 26.10. nachts). Nie tiefer."""
    aus = []
    for s in stuecke:
        s = np.asarray(s, np.float64)
        for _runde in range(6):
            if len(s) < 2:
                break
            d = np.diff(s, axis=0)
            l = np.hypot(d[:, 0], d[:, 1])
            n = np.maximum(2, np.ceil(l / abstand).astype(int))
            seg = np.repeat(np.arange(len(d)), n - 1)
            t = np.concatenate([np.arange(1, k) / k for k in n])
            q = s[seg] + d[seg] * t[:, None]
            cl = abtasten(q[:, :2])
            ueber = cl - q[:, 2]
            if not (ueber > spiel).any():
                break
            schlimm = np.full(len(d), -np.inf)
            np.maximum.at(schlimm, seg, ueber)
            hoechst = np.full(len(d), -np.inf)
            np.maximum.at(hoechst, seg, cl)
            # schlimmster Punkt je Strecke
            wo = np.full(len(d), -1)
            reihen = np.flatnonzero(ueber > spiel)
            for i in reihen:
                g = seg[i]
                if wo[g] < 0 or ueber[i] > ueber[wo[g]]:
                    wo[g] = i
            teile = [s[:1]]
            for i in range(len(d)):
                if schlimm[i] > klippe or (schlimm[i] > spiel and _runde == 5):
                    zh = max(hoechst[i] + 0.01, s[i, 2], s[i + 1, 2])
                    teile.append(np.array([[s[i, 0], s[i, 1], zh], [s[i + 1, 0], s[i + 1, 1], zh]]))
                elif schlimm[i] > spiel:
                    k = wo[i]
                    teile.append(np.array([[q[k, 0], q[k, 1], cl[k]]]))
                teile.append(s[i + 1:i + 2])
            s = np.vstack(teile)
        aus.append(s)
    return aus

def _ausgeben(b, pfade: List[np.ndarray], abtasten, bereich, schritt: float, d: float, abtast: float,
              stufe: float, tol: float):
    """Pfade nacheinander fahren. Übergänge: nahe Stücke direkt verbinden statt abheben. Die Gerade
    zwischen zwei Bahnenden liegt an einem gewölbten Rand knapp ausserhalb des Bereichs, an
    schrägen Rändern sind die Enden etwas weiter auseinander – beides erlaubt, solange der Übergang
    nahe am Bereich bleibt (dort ist geschruppt; die Höhe kommt auch hier aus OpenCAMLib, also nie
    ins Modell)."""
    from shapely.geometry import LineString
    r = d / 2
    # geschlossene Ringe (Höhenlinien, Ebenen) dort beginnen, wo der vorherige Pfad endet
    for k in range(1, len(pfade)):
        pf = pfade[k]
        if len(pf) > 3 and math.dist(pf[0][:2], pf[-1][:2]) < 1e-6:
            ende = pfade[k - 1][-1][:2]
            i = int(np.argmin(np.hypot(pf[:-1, 0] - ende[0], pf[:-1, 1] - ende[1])))
            if i:
                pfade[k] = np.vstack([pf[i:-1], pf[:i + 1]])
    erlaubt = _puffer(bereich, min(2.0, r / 2) + 1e-3, resolution=8)
    max_lang = max(2.5 * schritt, 1.5 * d)
    verbindungen = []
    for k in range(1, len(pfade)):
        a, e = pfade[k - 1][-1], pfade[k][0]
        lang = math.dist(a[:2], e[:2])
        if lang <= max_lang + 1e-9 and erlaubt.contains(LineString([a[:2], e[:2]])):
            n = max(2, int(math.ceil(lang / abtast)) + 1)
            t = np.linspace(0.0, 1.0, n)
            verbindungen.append((k, np.column_stack([a[0] + (e[0] - a[0]) * t, a[1] + (e[1] - a[1]) * t])))
    vz = abtasten(np.concatenate([v for _k, v in verbindungen])) if verbindungen else np.zeros(0)
    verb_roh = []
    i = 0
    behalten = []
    for k, v in verbindungen:
        # nie tiefer als die beiden Enden verlangen (Höhenlinien liegen über der Abtastung)
        zv = vz[i:i + len(v)]
        i += len(v)
        # nicht über eine Wand hinauf und wieder hinunter (Impeller: 57 m Fräsweg, davon 49 m senkrecht über
        # die Schaufeln) – dort abheben, die Übergänge liegen über dem Geräumten (tief_uebergaenge)
        enden = max(pfade[k - 1][-1][2], pfade[k][0][2])
        if len(zv) and float(np.max(zv)) > enden + max(1.0, schritt):
            continue
        verb_roh.append(np.column_stack([v, zv]))
        behalten.append((k, v))
    verbindungen = behalten
    verb_roh = _verfeinern(verb_roh, abtasten, stufe)
    verb: Dict[int, np.ndarray] = {k: _vereinfachen(v, tol) for (k, _v), v in zip(verbindungen, verb_roh)}
    # Sicherheitsnetz: keine Gerade schneidet ins Modell (auch nicht zwischen zwei Punkten auf gleicher Höhe)
    pfade = _sehnen_sicher(pfade, abtasten)
    if verb:
        ks = list(verb)
        for k, v in zip(ks, _sehnen_sicher([verb[k] for k in ks], abtasten)):
            verb[k] = v
    for k, pf in enumerate(pfade):
        if k in verb:
            for q in verb[k][1:-1]:
                b.fraesen(float(q[0]), float(q[1]), float(q[2]))
            b.fraesen(float(pf[0][0]), float(pf[0][1]), float(pf[0][2]))
        else:
            b.abheben()
            b.anfahren(float(pf[0][0]), float(pf[0][1]))
            b.eintauchen(float(pf[0][2]), None)
        for q in pf[1:]:
            b.fraesen(float(q[0]), float(q[1]), float(q[2]))


def tief_uebergaenge(tp, vorher, material, d: float, freifahrhoehe: float, startabstand: float, start) -> int:
    """Übergänge des 3D-Schlichtens tiefer (Impeller 25.10.: 32 min, davon 3 min Schneiden – jeder der ~1000
    Züge tauchte von 0,5 mm über der Oberfläche im Eintauchvorschub bis −25 ein). Mit dem, was nach den
    Bearbeitungen davor tatsächlich steht (vorher: [(Werkzeugweg, Werkzeug)] derselben Seite, simuliert):
    abheben nur bis über das höchste Material auf dem Weg, im Eilgang hinüber und bis knapp über das Material
    am Ziel, erst dort im Vorschub. Das Schlichten selbst nimmt nur das Aufmass – ohne es mitzurechnen steht
    das Material eher höher (sicher). Liefert die Zahl der geänderten Übergänge."""
    from .. import hirch_relief
    from ..model import Move
    ms = tp.moves
    if not vorher or len(ms) < 3:
        return 0
    pts = np.array([(m.x, m.y) for m in ms], np.float64)
    r = d / 2
    x0, y0 = pts.min(0) - r - 2
    x1, y1 = pts.max(0) + r + 2
    zelle = 0.25
    while (x1 - x0) * (y1 - y0) / zelle ** 2 > 6_000_000:
        zelle *= 1.25
    nx, ny = int(math.ceil((x1 - x0) / zelle)) + 1, int(math.ceil((y1 - y0) / zelle)) + 1
    H = np.zeros((ny, nx), np.float32)
    for tv, wv in vorher:
        b3 = np.array([(m.x, m.y, m.z) for m in tv.moves], np.float64).reshape(-1, 3)
        if len(b3):
            hirch_relief.abtragen(H, float(x0), float(y0), zelle, b3, _werkzeug_form(wv),
                                  [b3[0, 0], b3[0, 1], 50.0], -float(material[2]))
    R = r + 1.5 * zelle
    kx = int(math.ceil(R / zelle))
    oy, ox = np.mgrid[-kx:kx + 1, -kx:kx + 1]
    scheibe = (ox * zelle) ** 2 + (oy * zelle) ** 2 <= R * R
    oy, ox = oy[scheibe], ox[scheibe]

    def hoch(x, y):
        i = int(round((x - x0) / zelle)) + ox
        j = int(round((y - y0) / zelle)) + oy
        drin = (i >= 0) & (i < nx) & (j >= 0) & (j < ny)
        h = float(H[j[drin], i[drin]].max()) if drin.any() else 0.0
        return h if drin.all() else max(h, 0.0)

    def auf_weg(a, b):
        n = max(1, int(math.ceil(math.dist(a, b) / (0.5 * zelle))))
        return max(hoch(a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n) for k in range(n + 1))

    cz = float(freifahrhoehe)
    neu, geaendert, i = [], 0, 0
    pos = tuple(map(float, start))
    while i < len(ms):
        m = ms[i]
        # Muster des Bahnbauers: Z hinauf auf die Freifahrhöhe – quer – (Z auf den Startabstand) – Eintauchen
        if (m.rapid and abs(m.x - pos[0]) < 1e-9 and abs(m.y - pos[1]) < 1e-9 and m.z >= cz - 1e-6 > pos[2]
                and i + 1 < len(ms)):
            j = i + 1
            quer = ms[j] if ms[j].rapid and abs(ms[j].z - m.z) < 1e-9 else None
            if quer is not None:
                k = j + 1
                runter = ms[k] if k < len(ms) and ms[k].rapid and abs(ms[k].x - quer.x) < 1e-9 \
                    and abs(ms[k].y - quer.y) < 1e-9 else None
                k2 = k + 1 if runter is not None else k
                ein = ms[k2] if k2 < len(ms) and not ms[k2].rapid and abs(ms[k2].x - quer.x) < 1e-9 \
                    and abs(ms[k2].y - quer.y) < 1e-9 else None
                if ein is not None:
                    a, b = (pos[0], pos[1]), (quer.x, quer.y)
                    ueber = auf_weg(a, b) + startabstand
                    ziel_frei = max(ein.z, hoch(b[0], b[1])) + startabstand
                    if ueber < cz - 1e-6:
                        if ueber > pos[2] + 1e-9:
                            neu.append(Move(True, pos[0], pos[1], ueber))
                        hz = max(ueber, pos[2])
                        neu.append(Move(True, b[0], b[1], hz))
                        if ziel_frei < hz - 1e-9:
                            neu.append(Move(True, b[0], b[1], ziel_frei))
                        neu.append(ein)
                        pos = (ein.x, ein.y, ein.z)
                        geaendert += 1
                        i = k2 + 1
                        continue
                    elif ziel_frei < (runter.z if runter is not None else quer.z) - 1e-9:
                        # nur unten: im Eilgang bis knapp über das Material am Ziel
                        neu += [m, quer, Move(True, b[0], b[1], ziel_frei), ein]
                        pos = (ein.x, ein.y, ein.z)
                        geaendert += 1
                        i = k2 + 1
                        continue
        neu.append(m)
        pos = (m.x, m.y, m.z)
        i += 1
    tp.moves = neu
    return geaendert


# --------------------------------------------------------------------------------------------

ENTWURF = 3.0   # Entwurfsmodus: Abtastung und Bahnabstand beim Schlichten × 3


def _zweiseitig_da(auftrag: Dict) -> bool:
    """Ein Modell wird von beiden Seiten gefräst (Auftrag zweiseitig und Modell «Von beiden Seiten»)."""
    from . import relief_zweiseitig as zs
    if not (auftrag.get("zweiseitig") or {}).get("an"):
        return False
    return any(zs.einstellungen(r.get("info")).get("an") and int(r.get("seite") or 1) == 1
               for r in auftrag.get("reliefs") or [])


@ohne_nan_abbruch
def bearbeiten(art: str, p: Dict, w: Dict, auftrag: Dict, seite: int, freifahrhoehe: float,
               startabstand: float, start, name: str, tol: float, vorher=None):
    """Einstieg für engine.rechnen: (Werkzeugweg, Warnungen). vorher: [(Werkzeugweg, Werkzeug)] der
    früheren 3D-Bearbeitungen dieser Seite (für «Nur Restmaterial»)."""
    warnungen: List[str] = []
    if vorher is not None:
        p = dict(p, _vorher=vorher)
    ru = auftrag.get("rund") or {}
    if ru.get("radius"):
        # rundes Material: Mitte bleibt beim Wenden (um X oder Y) in der Mitte
        p = dict(p, _kreis=(float(ru["mitte"][0]), float(ru["mitte"][1]), float(ru["radius"])))
    flaeche = auftrag.get("material_flaeche") or [300.0, 200.0]
    dicke = float((auftrag.get("hoehen") or {}).get("materialdicke") or 18.0)
    material = (float(flaeche[0]), float(flaeche[1]), dicke)
    d = float(w["durchmesser"])
    rand = float(p.get("rand")) if p.get("rand") not in (None, "") else (d if art == "relief_schruppen" else d / 2 + 1)
    if p.get("rand") in (None, "") and _zweiseitig_da(auftrag):
        # Peter 02.11. (Skizze V1/V2): zweiseitig verlässt der Schlichtfräser das Modell ganz und fährt eine
        # Fräserbreite auf die Haut bzw. Trennfläche hinaus – sonst bleibt neben der Kante eine Wand stehen.
        # Das Schruppen räumt noch breiter bis auf die Haut (Peter 16:44: «auch ausserhalb des Flügels bis
        # auf die Haut»): Mitte bis 2 Ø + 2 mm, die Schnittkante damit 2,5 Ø + 2 mm vom Umriss
        rand = 2 * d + 2.0 if art == "relief_schruppen" else d + 1.0
        p = dict(p, rand=rand)
    # so weit reicht der Fräsbereich höchstens: Rand + Fräser + Reserve
    reichweite = rand + d + 2
    tri, extra, geos = _modelle(auftrag, p, seite, material, reichweite, warnungen)
    if tri is None:
        if not geos and not any("Modelldaten" in t for t in warnungen):
            if not (seite == 2 and any(int(r.get("seite") or 1) == 1 for r in auftrag.get("reliefs") or [])):
                warnungen.append("Kein 3D-Modell in der Zeichnung – zuerst «Fräsen › 3D-Modell einlesen …».")
            else:
                warnungen.append("Auf Seite 2 gibt es nichts zu fräsen: das 3D-Modell wird nur von oben "
                                 "gefräst (im Fenster «3D-Modell» «Von beiden Seiten fräsen» einschalten).")
        return Bahnbauer(w["vorschub"], w["eintauchen"], freifahrhoehe, startabstand, start).weg(name), warnungen
    if auftrag.get("entwurf"):
        # Entwurfsmodus: schneller und gröber (zum Einstellen); gespeichert wird immer genau gerechnet
        # (Schruppen bleibt: ein gröberes Raster macht es nicht schneller)
        if art == "relief_schlichten":
            p = dict(p)
            p["abtastung"] = (float(p.get("abtastung") or 0) or max(0.1, min(0.5, d / 12))) * ENTWURF
            p["bahnabstand"] = min(80.0, float(p.get("bahnabstand") or 10) * ENTWURF)
    if geos:
        # Schruppen räumt bis Modell + Rand + Radius (Schnittkante), Schlichten führt die Mitte bis Rand
        spalt = rand + d / 2 if art == "relief_schruppen" else rand
        _zweiseitig_pruefen(geos, auftrag, seite, spalt, material, warnungen,
                            0.0 if art == "relief_schruppen" else d / 2)
    if art == "relief_schruppen":
        tp, w2 = schruppen(tri, material, w, p, freifahrhoehe, startabstand, start, name, tol, extra)
    else:
        tp, w2 = schlichten(tri, material, w, p, freifahrhoehe, startabstand, start, name, tol, extra)
    return tp, warnungen + w2
