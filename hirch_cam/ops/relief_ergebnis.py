# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Fräsergebnis für den Reiter «3D»: was nach allen Programmen vom Material übrig bleibt.

Die Programme der letzten Rechnung (.teile, Materialkoordinaten der jeweiligen Seite) werden auf
einem Höhenraster abgetragen (hirch_relief.abtragen, C++). Zweiseitig: Seite 2 wird gewendet und
bildet die Unterseite. Verglichen mit den 3D-Modellen der Zeichnung bekommt jeder Rasterpunkt
eine Klasse:

    0  wie gewollt (oder ausserhalb des Modells)
    1  Material bleibt stehen – mehr als REST mm über dem Modell
    2  zu tief – mehr als ZU_TIEF mm ins Modell hinein

An steilen Wänden trifft ein Rasterpunkt mal die obere, mal die untere Kante; verglichen wird
darum mit der tiefsten bzw. höchsten Stelle des Modells in der Nachbarschaft (3 × 3 Punkte).

Ausgabe (Binärdatei für die Ansicht): int32 nx, ny; float32 x0, y0, zelle; float32 oben[ny·nx],
unten[ny·nx]; uint8 klasse_oben[ny·nx], klasse_unten[ny·nx]; float32 modell_oben[ny·nx] (NaN neben
dem Modell; für den Klick ins Fräsergebnis); uint16 karte_oben[ny·nx], karte_unten[ny·nx] (Bearbeitung
der tiefsten Stelle: Index in «karte» + 1, 0 = unberührt oder höchstens MINDESTTIEFE tief – für
«Färben nach: Bearbeitung», dieselbe Regel wie hirch-abtrag beim vollen Rechnen).
Rasterpunkt (i, j) liegt bei
(x0 + i·zelle, y0 + j·zelle) in Koordinaten von Seite 1 (Nullpunkt unten links auf der Oberfläche).
"""
from __future__ import annotations

import json
import math
import os
import struct
import time
from typing import Dict, List, Optional

import numpy as np

REST = 0.5          # mm über dem Modell: «bleibt stehen»
ZU_TIEF = 0.1       # mm ins Modell: «zu tief»
GENAUIGKEIT = {     # höchstens so viele Rasterpunkte je Seite, feinstes Raster
    "grob": (250_000, 0.5),
    "mittel": (800_000, 0.25),
    "fein": (2_500_000, 0.1),
}


def raster(breite: float, hoehe: float, genauigkeit: str = "mittel"):
    """Rasterweite und Anzahl Punkte für das Material (auf 0,05 mm gerundet)."""
    n_max, feinst = GENAUIGKEIT.get(genauigkeit, GENAUIGKEIT["mittel"])
    z = max(feinst, math.sqrt(max(1.0, breite * hoehe) / n_max))
    z = math.ceil(z / 0.05 - 1e-9) * 0.05
    return z, int(math.ceil(breite / z)) + 1, int(math.ceil(hoehe / z)) + 1


def _werkzeug(p: Dict) -> Dict:
    form = p.get("form3d") or p.get("form") or "flach"
    d = float(p.get("durchmesser") or 3)
    if form == "v":
        return {"typ": "v", "durchmesser": d, "v_winkel": float(p.get("v_winkel") or 60),
                "spitze": float(p.get("spitze") or 0)}
    if form == "kugel":
        return {"typ": "eckradius", "durchmesser": d, "eckradius": float(p.get("eckradius") or d / 2)}
    if form == "konisch":
        return {"typ": "konisch", "durchmesser": d, "eckradius": float(p.get("eckradius") or 0.25),
                "v_winkel": float(p.get("v_winkel") or 10)}
    return {"typ": "schaft", "durchmesser": d}


MINDESTTIEFE = 0.02    # mm: erst so tief unter der Oberfläche bekommt eine Stelle eine Bearbeitung (Karte)

_WORT = None


def _weg_anfaenge(text: str) -> List[int]:
    """Zeilennummer (ab 1, wie «zeile» der Abschnitte) des ersten Satzes jedes Werkzeugwegs – nach
    derselben Regel wie tap_mm.read (Eilgang mit X, Y und Z beginnt einen neuen Weg)."""
    import re
    global _WORT
    if _WORT is None:
        _WORT = re.compile(r"([A-Z])(-?\d+(?:\.\d+)?)")
    zeilen = [z.strip() for z in text.splitlines() if z.strip()]
    anfaenge: List[int] = []
    g = 0
    for k, z in enumerate(zeilen[4:-3]):
        w = dict(_WORT.findall(z))
        if "G" in w:
            g = int(float(w["G"]))
        if g == 0 and all(a in w for a in "XYZ"):
            anfaenge.append(k + 5)
    return anfaenge


def _weg_schluessel(p: Dict, text: str, anzahl: int) -> List[Optional[str]]:
    """Bearbeitung (id) je Werkzeugweg des Programms, aus den Abschnitten (Startzeile je Abschnitt)."""
    ab = sorted((a for a in p.get("abschnitte") or [] if a.get("key")), key=lambda a: int(a.get("zeile") or 0))
    if not ab:
        return [None] * anzahl
    anfaenge = _weg_anfaenge(text)
    aus: List[Optional[str]] = []
    for k in range(anzahl):
        z = anfaenge[k] if k < len(anfaenge) else 10 ** 9
        key = ab[0]["key"]
        for a in ab:
            if int(a.get("zeile") or 0) <= z:
                key = a["key"]
        aus.append(str(key).partition("#")[0])
    return aus


def abtragen_seite(programme: List[Dict], seite: int, x0: float, y0: float, zelle: float,
                   nx: int, ny: int, dicke: float, karte: Optional[np.ndarray] = None,
                   liste: Optional[List[Dict]] = None) -> np.ndarray:
    """Höhenraster einer Seite nach allen ihren Programmen (0 = Oberfläche, −dicke = durch).
    karte (ny, nx, uint16, optional): je Stelle die Bearbeitung der tiefsten Stelle (die sie als erste
    so tief gefräst hat; Index in liste + 1), 0 = unberührt oder höchstens MINDESTTIEFE unter der
    Oberfläche – dieselbe Regel wie hirch-abtrag beim vollen Rechnen (Basis, 22.10.); liste wird
    dabei ergänzt ({id, farbe, name} in der Reihenfolge, in der die Bearbeitungen zuerst fräsen)."""
    from .. import hirch_relief
    from ..post import tap_mm
    H = np.zeros((ny, nx), np.float32)
    for p in programme:
        if int(p.get("seite") or 1) != seite:
            continue
        try:
            with open(p["datei"], newline="") as fh:
                text = fh.read()
            job = tap_mm.read(text)
        except (OSError, ValueError, KeyError):
            continue
        start = [float(job.home[0]), float(job.home[1]), float(job.safe_z)]
        wz = _werkzeug(p)
        if karte is None:
            pts = np.array([(m.x, m.y, m.z) for tp in job.toolpaths for m in tp.moves], np.float64).reshape(-1, 3)
            if len(pts):
                hirch_relief.abtragen(H, x0, y0, zelle, pts, wz, start, -dicke)
            continue
        # je Bearbeitung (aufeinanderfolgende Wege mit derselben id) einzeln, Karte nachführen
        schluessel = _weg_schluessel(p, text, len(job.toolpaths))
        farben = {str(a.get("key", "")).partition("#")[0]: a for a in p.get("abschnitte") or []}
        gruppen: List[List] = []
        for tp, key in zip(job.toolpaths, schluessel):
            if gruppen and gruppen[-1][0] == key:
                gruppen[-1][1].append(tp)
            else:
                gruppen.append([key, [tp]])
        for key, wege in gruppen:
            pts = np.array([(m.x, m.y, m.z) for tp in wege for m in tp.moves], np.float64).reshape(-1, 3)
            if not len(pts):
                continue
            vorher = H.copy()
            hirch_relief.abtragen(H, x0, y0, zelle, pts, wz, start, -dicke)
            start = [float(v) for v in pts[-1]]
            bid = key or str(p.get("name") or "")
            nr = next((i for i, e in enumerate(liste) if e["id"] == bid), None)
            if nr is None:
                a = farben.get(key or "", {})
                liste.append({"id": bid, "farbe": a.get("farbe") or "", "name": a.get("name") or bid})
                nr = len(liste) - 1
            karte[H < vorher - 1e-6] = nr + 1
    if karte is not None:
        karte[H > -MINDESTTIEFE] = 0
    return H


def _nachbarn(a: np.ndarray, art: str) -> np.ndarray:
    from scipy import ndimage
    if art == "min":
        # am Umriss des Modells (Nachbar ohne Modell) fällt die Wand beliebig tief ab: dort nie «zu tief»
        # (eine Rasterzelle auf der Wand misst sonst senkrecht, was waagrecht nur Hundertstel sind)
        return ndimage.minimum_filter(np.where(np.isfinite(a), a, -np.inf), size=3, mode="nearest")
    return ndimage.maximum_filter(np.where(np.isfinite(a), a, -np.inf), size=3, mode="nearest")


def modelle_oben_unten(reliefs: List[Dict], breite: float, hoehe: float, dicke: float, zelle: float,
                       nx: int, ny: int, achse: str):
    """Oberseite und (gefräste) Unterseite der Modelle im Raster von Seite 1 (NaN = kein Modell).
    Einseitige Modelle von Seite 1 haben keine gefräste Unterseite; Modelle auf Seite 2 zeigen
    ihre Oberseite gewendet als Unterseite."""
    from .. import hirch_relief
    from . import relief3d, relief_zweiseitig as zs
    oben = np.full((ny, nx), np.nan, np.float32)
    unten = np.full((ny, nx), np.nan, np.float32)
    for r in reliefs or []:
        try:
            tri, _v = relief3d.modell_im_material(r, dicke)
        except Exception:                    # Modell nicht lesbar: ohne Vergleich
            continue
        seite = int(r.get("seite") or 1)
        if seite == 2:                        # in Koordinaten von Seite 1 wenden
            tri = tri.copy()
            if achse == "y":
                tri[..., 0] = breite - tri[..., 0]
            else:
                tri[..., 1] = hoehe - tri[..., 1]
            tri[..., 2] = -dicke - tri[..., 2]
        o, u, _n = hirch_relief.hoehenkarten(np.ascontiguousarray(tri, np.float32), 0.0, 0.0, zelle, nx, ny)
        if seite == 2:
            unten = np.fmin(unten, u)          # die gefräste Seite des Modells zeigt nach unten
            continue
        oben = np.fmax(oben, o)
        if bool(zs.einstellungen(r.get("info")).get("an")):
            unten = np.fmin(unten, u)
    return oben, unten


def ergebnis(programme: List[Dict], material, reliefs: List[Dict], zweiseitig: Optional[Dict], ordner: str,
             genauigkeit: str = "mittel") -> Dict:
    t0 = time.time()
    breite, hoehe, dicke = (float(v) for v in material)
    zelle, nx, ny = raster(breite, hoehe, genauigkeit)
    zw = zweiseitig or {}
    achse = "y" if zw.get("achse") == "y" else "x"
    seiten = {int(p.get("seite") or 1) for p in programme}
    karte_liste: List[Dict] = []
    karte_oben = np.zeros((ny, nx), np.uint16)
    karte_unten = np.zeros((ny, nx), np.uint16)
    oben = abtragen_seite(programme, 1, 0.0, 0.0, zelle, nx, ny, dicke, karte_oben, karte_liste) if 1 in seiten \
        else np.zeros((ny, nx), np.float32)
    if 2 in seiten:
        # Raster von Seite 2 so legen, dass gewendet dieselben Punkte herauskommen
        k2 = np.zeros((ny, nx), np.uint16)
        if achse == "y":
            h2 = abtragen_seite(programme, 2, breite - (nx - 1) * zelle, 0.0, zelle, nx, ny, dicke, k2,
                                karte_liste)[:, ::-1]
            karte_unten = np.ascontiguousarray(k2[:, ::-1])
        else:
            h2 = abtragen_seite(programme, 2, 0.0, hoehe - (ny - 1) * zelle, zelle, nx, ny, dicke, k2,
                                karte_liste)[::-1, :]
            karte_unten = np.ascontiguousarray(k2[::-1, :])
        unten = (-dicke - h2).astype(np.float32)
    else:
        unten = np.full((ny, nx), -dicke, np.float32)
    # ausserhalb des Materials (das Raster reicht bis zu einer Zelle darüber hinaus)
    xs = np.arange(nx) * zelle
    ys = np.arange(ny) * zelle
    draussen = (xs[None, :] > breite + 1e-6) | (ys[:, None] > hoehe + 1e-6)
    oben[draussen] = unten[draussen] = -dicke

    m_oben, m_unten = modelle_oben_unten(reliefs, breite, hoehe, dicke, zelle, nx, ny, achse)
    k_oben = np.zeros((ny, nx), np.uint8)
    k_unten = np.zeros((ny, nx), np.uint8)
    hinweise: List[str] = []
    zu_tief = rest = 0.0
    durch = oben <= unten + 0.01
    # negatives Aufmass (Peter 31.10.): so viel tiefer ist gewollt
    def _neg(s):
        return max([0.0] + [-float(p.get("aufmass_min") or 0.0) for p in programme if int(p.get("seite") or 1) == s])
    tol1, tol2 = ZU_TIEF + _neg(1), ZU_TIEF + _neg(2)
    if np.isfinite(m_oben).any():
        drin = np.isfinite(m_oben) & ~durch
        lo, hi = _nachbarn(m_oben, "min"), _nachbarn(m_oben, "max")
        tief = drin & (oben < lo - tol1)
        viel = drin & (oben > hi + REST)
        k_oben[viel] = 1
        k_oben[tief] = 2
        if tief.any():
            zu_tief = max(zu_tief, float(np.max(lo[tief] - oben[tief])))
        if viel.any():
            rest = max(rest, float(np.max(oben[viel] - hi[viel])))
    if np.isfinite(m_unten).any() and 2 in seiten:
        drin = np.isfinite(m_unten) & ~durch
        lo, hi = _nachbarn(m_unten, "min"), _nachbarn(m_unten, "max")
        tief = drin & (unten > hi + tol2)
        viel = drin & (unten < lo - REST)
        k_unten[viel] = 1
        k_unten[tief] = 2
        if tief.any():
            zu_tief = max(zu_tief, float(np.max(unten[tief] - hi[tief])))
        if viel.any():
            rest = max(rest, float(np.max(lo[viel] - unten[viel])))
    if not np.isfinite(m_oben).any() and not np.isfinite(m_unten).any():
        hinweise.append("Kein 3D-Modell zum Vergleichen – gezeigt wird nur, was gefräst wird.")

    os.makedirs(ordner, exist_ok=True)
    datei = os.path.join(ordner, "ergebnis.bin")
    with open(datei, "wb") as fh:
        fh.write(struct.pack("<ii3f", nx, ny, 0.0, 0.0, zelle))
        fh.write(np.ascontiguousarray(oben, "<f4").tobytes())
        fh.write(np.ascontiguousarray(unten, "<f4").tobytes())
        fh.write(np.ascontiguousarray(k_oben).tobytes())
        fh.write(np.ascontiguousarray(k_unten).tobytes())
        fh.write(np.ascontiguousarray(m_oben, "<f4").tobytes())
        fh.write(np.ascontiguousarray(karte_oben, "<u2").tobytes())
        fh.write(np.ascontiguousarray(karte_unten, "<u2").tobytes())
    return {"ok": True, "datei": datei, "nx": nx, "ny": ny, "zelle": round(zelle, 3), "karte": karte_liste,
            "zu_tief": round(zu_tief, 2), "rest": round(rest, 2),
            "anteil_zu_tief": round(float(((k_oben == 2) | (k_unten == 2)).mean()) * 100, 2),
            "anteil_rest": round(float(((k_oben == 1) | (k_unten == 1)).mean()) * 100, 2),
            "seiten": sorted(seiten), "sekunden": round(time.time() - t0, 2), "hinweise": hinweise}


def main(argv) -> int:
    """relief ergebnis SVG --programme DATEI.json --ordner O [--genauigkeit grob|mittel|fein] [--platte N]"""
    import argparse
    import sys
    ap = argparse.ArgumentParser(prog="relief ergebnis")
    ap.add_argument("svg")
    ap.add_argument("--programme", required=True, help="JSON-Datei: Liste der Programme der letzten Rechnung")
    ap.add_argument("--ordner", required=True)
    ap.add_argument("--genauigkeit", default="mittel", choices=sorted(GENAUIGKEIT))
    ap.add_argument("--platte", type=int, default=1, help="Platte (ab 2: eigene Grösse und Modelle)")
    a = ap.parse_args(argv)
    try:
        import inkex
        from .. import einstellungen as einst
        from ..svg_lesen import lesen
        svg = inkex.load_svg(a.svg).getroot()
        e = einst.laden(svg.get(einst.ATTRIBUT))
        info = lesen(svg, zweiseitig=einst.zweiseitig_an(e), platte=a.platte)
        with open(a.programme, encoding="utf-8") as fh:
            programme = json.load(fh)
        zw = dict(e.get("zweiseitig") or {})
        erg = ergebnis(programme, (info["breite"], info["hoehe"], info["dicke"]), info.get("reliefs", []),
                       zw, a.ordner, a.genauigkeit)
    except Exception as ex:                      # lesbar an die Oberfläche
        erg = {"ok": False, "warnungen": [f"Fräsergebnis: {ex}"]}
    from ..sprache import ausgabe
    sys.stdout.write(json.dumps(ausgabe(erg), ensure_ascii=False))
    return 0
