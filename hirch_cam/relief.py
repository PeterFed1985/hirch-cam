# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""3D-Modell ausrichten, Höhenkarten rechnen, Kennzahlen und Schattenbild.

Höhenkarten: das Modell von oben gesehen (Oberseite, grösste Höhe je Stelle) und von unten
(Unterseite, kleinste Höhe). Seite 1 einer zweiseitigen Fräsung fräst die Oberseite, Seite 2
die Unterseite. Ein Graustufenbild hat nur eine Oberseite; die Unterseite ist seine Grundfläche.

Koordinaten nach dem Ausrichten: die linke untere Ecke des Modells (Draufsicht) liegt bei
X = Y = 0; Z bleibt wie im Modell (nur gedreht und skaliert), die Lage im Material bestimmt
erst das Platzieren.

Befehl für das Panel:
    python3 -m hirch_cam.dokument relief DATEI [--zelle MM] [--ausrichtung JSON]
                                              [--bild PNG] [--seite oben|unten]
"""
from __future__ import annotations

import argparse
import json
import math
import struct
import sys
import zlib
from dataclasses import dataclass
from typing import Dict, List, Optional, Sequence

import numpy as np

from . import relief_import
from .relief_import import EinleseFehler, Modell

# Rasterweiten, aus denen «automatisch» die feinste mit höchstens MAX_ZELLEN Punkten wählt
RASTER = (0.05, 0.1, 0.2, 0.25, 0.5, 1.0, 2.0)
MAX_ZELLEN = 25_000_000

STANDARD_AUSRICHTUNG = {
    "oben": "+Z",                # welche Achse des Modells nach oben zeigt (+Z, −Z, +Y, −Y, +X, −X)
    "neigung": [0.0, 0.0],       # Grad um X, dann um Y (nach «oben»)
    "drehen_z": 0.0,             # Grad in der Ebene (z. B. 90, damit es ins Material passt)
    "drehen": [0.0, 0.0, 0.0],   # frei: Grad um X, Y, Z (vor allem anderen)
    "skalierung": 1.0,           # gleichmässig
    "z_faktor": 1.0,             # Stauchung in Z
    # nur Graustufenbild:
    "breite": 100.0,             # mm (Bildbreite)
    "hoehe": 5.0,                # mm (weiss)
    "invertieren": False,        # dunkel = hoch
    "glaetten": 0.0,             # mm (Gauss)
}


@dataclass
class Hoehenkarten:
    x0: float
    y0: float
    zelle: float
    oben: np.ndarray        # (ny, nx) float32, NaN ausserhalb
    unten: np.ndarray
    verdeckt: np.ndarray    # (ny, nx) bool: von oben verdeckte Stellen (Hinterschnitt)

    @property
    def maske(self) -> np.ndarray:
        return np.isfinite(self.oben)

    @property
    def form(self):
        return self.oben.shape


def ausrichtung(werte: Optional[Dict] = None) -> Dict:
    a = dict(STANDARD_AUSRICHTUNG)
    a.update({k: v for k, v in (werte or {}).items() if v is not None})
    return a


def _drehmatrix(grad: Sequence[float]) -> np.ndarray:
    rx, ry, rz = (math.radians(float(g)) for g in (list(grad) + [0, 0, 0])[:3])
    cx, sx, cy, sy, cz, sz = math.cos(rx), math.sin(rx), math.cos(ry), math.sin(ry), \
        math.cos(rz), math.sin(rz)
    X = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    Y = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    Z = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return Z @ Y @ X


# Drehung, die die gewählte Achse nach +Z bringt
OBEN = {"+Z": (0, 0, 0), "-Z": (180, 0, 0), "+Y": (90, 0, 0), "-Y": (-90, 0, 0),
        "+X": (0, -90, 0), "-X": (0, 90, 0)}


def gesamt_drehung(a: Dict) -> np.ndarray:
    oben = str(a.get("oben") or "+Z").replace("−", "-").upper()
    n = list(a.get("neigung") or [0, 0]) + [0, 0]
    return (_drehmatrix([0, 0, float(a.get("drehen_z") or 0)])
            @ _drehmatrix([float(n[0]), float(n[1]), 0])
            @ _drehmatrix(OBEN.get(oben, OBEN["+Z"]))
            @ _drehmatrix(a.get("drehen") or [0, 0, 0]))


class Lage:
    """Abbildung Modell → ausgerichtete Koordinaten (für Netz und Kanten gleich)."""

    def __init__(self, modell: Modell, a: Dict):
        self.m = gesamt_drehung(a) * float(a["skalierung"])
        self.m[2] *= float(a["z_faktor"])
        p = modell.dreiecke.reshape(-1, 3).astype(np.float64) @ self.m.T
        self.versatz = np.array([-p[:, 0].min(), -p[:, 1].min(), 0.0])

    def __call__(self, punkte: np.ndarray) -> np.ndarray:
        s = punkte.shape
        return (punkte.reshape(-1, 3).astype(np.float64) @ self.m.T + self.versatz).reshape(s)


def ausgerichtet(modell: Modell, a: Dict) -> np.ndarray:
    """Dreiecke nach der Ausrichtung (float32). Eine Spiegelung (negativer Faktor) kehrt den
    Umlaufsinn um; das stört die Höhenkarten nicht."""
    return Lage(modell, a)(modell.dreiecke).astype(np.float32)


def kanten(modell: Modell, a: Dict) -> List[Dict]:
    """STEP-Kanten in ausgerichteten Koordinaten (Punkte mit Z, Arten der beiden Flächen)."""
    if not modell.kanten:
        return []
    lage = Lage(modell, a)
    return [{"punkte": lage(k["punkte"]), "flaechen": k["flaechen"]} for k in modell.kanten]


def zelle_waehlen(breite: float, tiefe: float, wunsch: Optional[float] = None) -> float:
    if wunsch:
        return float(wunsch)
    for z in RASTER:
        if (breite / z + 2) * (tiefe / z + 2) <= MAX_ZELLEN:
            return z
    return RASTER[-1]


def hoehenkarten(modell: Modell, a: Optional[Dict] = None,
                 zelle: Optional[float] = None) -> Hoehenkarten:
    a = ausrichtung(a)
    if modell.ist_bild:
        return _bild_karten(modell, a, zelle)
    from . import hirch_relief
    d = ausgerichtet(modell, a)
    hi = d.reshape(-1, 3).max(axis=0)
    z = zelle_waehlen(float(hi[0]), float(hi[1]), zelle)
    nx, ny = int(math.ceil(hi[0] / z)) + 1, int(math.ceil(hi[1] / z)) + 1
    oben, unten, anzahl = hirch_relief.hoehenkarten(d, 0.0, 0.0, z, nx, ny)
    return Hoehenkarten(0.0, 0.0, z, oben, unten, _verdeckt(anzahl >= 2))


def _verdeckt(roh: np.ndarray) -> np.ndarray:
    """Einzelne Rasterpunkte auf Ecken des Netzes sind kein Hinterschnitt."""
    if not roh.any():
        return roh
    from scipy import ndimage
    return ndimage.binary_opening(roh, structure=np.ones((2, 2), bool))


def _bild_karten(modell: Modell, a: Dict, zelle: Optional[float]) -> Hoehenkarten:
    h = modell.bild[::-1].astype(np.float64)              # Zeile 0 = unten (Y = 0)
    if a.get("invertieren"):
        h = 1.0 - h
    ny0, nx0 = h.shape
    breite = float(a["breite"]) * float(a["skalierung"])
    px = breite / nx0                                       # mm je Bildpunkt
    tiefe = px * ny0
    z = zelle or max(zelle_waehlen(breite, tiefe), px)      # nicht feiner als das Bild
    # Rasterpunkte in Zellmitten: (i + ½)·z, damit das Bild genau «breite» breit ist
    nx, ny = max(1, int(round(breite / z))), max(1, int(round(tiefe / z)))
    if abs(z - px) > 1e-9 or (nx, ny) != (nx0, ny0):        # auf das Raster bilinear abtasten
        from scipy import ndimage
        yy, xx = np.mgrid[0:ny, 0:nx].astype(np.float64)
        # Bildpunkt k deckt [k·px, (k+1)·px), Mitte bei (k+½)·px
        h = ndimage.map_coordinates(h, [np.clip((yy + 0.5) * z / px - 0.5, 0, ny0 - 1),
                                        np.clip((xx + 0.5) * z / px - 0.5, 0, nx0 - 1)],
                                    order=1, mode="nearest")
    if float(a.get("glaetten") or 0) > 0:
        from scipy import ndimage
        h = ndimage.gaussian_filter(h, float(a["glaetten"]) / z, mode="nearest")
    oben = (h * float(a["hoehe"]) * float(a["z_faktor"])).astype(np.float32)
    # in der Ebene nur in Vierteldrehungen (gegen den Uhrzeigersinn wie bei Netzen);
    # Seite oben und Neigung gelten nur für Netze
    k = int(round(float(a.get("drehen_z") or 0) / 90.0)) % 4
    if k:
        oben = np.ascontiguousarray(np.rot90(oben, -k))     # Zeile = Y nach oben, Spalte = X
    unten = np.zeros_like(oben)
    return Hoehenkarten(z / 2, z / 2, z, oben, unten, np.zeros(oben.shape, bool))


# --------------------------------------------------------------------------------------------
# Kennzahlen

def kennzahlen(hk: Hoehenkarten) -> Dict:
    m = hk.maske
    if not m.any():
        return {"masse": [0.0, 0.0, 0.0], "volumen_cm3": 0.0}
    j, i = np.nonzero(m)
    c = hk.zelle
    zmin, zmax = float(np.nanmin(hk.unten)), float(np.nanmax(hk.oben))
    dicke = np.where(m, hk.oben - hk.unten, 0.0)
    erg = {
        "masse": [round(float((i.max() - i.min() + 1) * c), 2),        # je Rasterpunkt eine Zelle
                  round(float((j.max() - j.min() + 1) * c), 2),
                  round(zmax - zmin, 2)],
        "z": [round(zmin, 2), round(zmax, 2)],
        "zelle": c,
        "raster": [int(hk.form[1]), int(hk.form[0])],
        "volumen_cm3": round(float(dicke.sum()) * c * c / 1000.0, 1),
        "flaeche_cm2": round(float(m.sum()) * c * c / 100.0, 1),
        "hinterschnitt_mm2": round(float(hk.verdeckt.sum()) * c * c, 1),
        "trennlinie": trennlinie(hk),
    }
    return erg


def trennlinie(hk: Hoehenkarten, max_dicke: Optional[float] = None):
    """Z-Bereich der Trennlinie: Randpunkte des Umrisses, an denen Ober- und Unterseite
    zusammenlaufen (dünner als `max_dicke`). Senkrechte Randflächen zählen nicht."""
    from scipy import ndimage
    m = hk.maske
    rand = m & ~ndimage.binary_erosion(m)
    grenze = max_dicke if max_dicke is not None else max(0.5, 5 * hk.zelle)
    dicke = np.where(rand, hk.oben - hk.unten, np.inf)
    # auch die Nachbarn müssen dünn sein: ein Rasterpunkt, der eine senkrechte Randfläche
    # gerade noch streift, ist sonst «dünn», obwohl das Teil dort dick ist
    from scipy import ndimage as nd
    nachbarn = nd.maximum_filter(np.where(m, hk.oben - hk.unten, 0.0), size=3)
    sel = rand & (dicke <= grenze) & (nachbarn <= max(3.0, 15 * hk.zelle))
    if sel.sum() < 3:
        return None
    zm = ((hk.oben + hk.unten) / 2)[sel]
    return [round(float(zm.min()), 2), round(float(zm.max()), 2)]


# --------------------------------------------------------------------------------------------
# Schattenbild (grau, durchsichtig ausserhalb) für die Zeichnung

def schattierung(hoehe: np.ndarray, zelle: float, azimut: float = 315.0,
                 hoehenwinkel: float = 45.0) -> np.ndarray:
    """Helligkeit 0…1 je Rasterpunkt (Licht von links oben)."""
    h = np.where(np.isfinite(hoehe), hoehe, np.nanmin(hoehe) if np.isfinite(hoehe).any() else 0)
    gy, gx = np.gradient(h.astype(np.float64), zelle)
    az, al = math.radians(azimut), math.radians(hoehenwinkel)   # Azimut ab +Y im Uhrzeigersinn
    lx, ly, lz = math.cos(al) * math.sin(az), math.cos(al) * math.cos(az), math.sin(al)
    n = np.sqrt(gx * gx + gy * gy + 1.0)
    return np.clip((-gx * lx - gy * ly + lz) / n, 0.0, 1.0)


def schattenbild(hk: Hoehenkarten, pfad: str, seite: str = "oben", max_pixel: int = 2000,
                 ueberhoehung: float = 3.0, achse: str = "x") -> Dict:
    """Schreibt ein graues PNG mit Alphakanal. Seite «unten» ist von unten gesehen (in X
    gespiegelt). Seite «unterseite»: die Unterseite in derselben Lage wie «oben» – die Zeichnung
    spiegelt das Bild auf Seite 2 (Wenden um «achse»); das Licht kommt danach von links oben.
    Rückgabe: Pixelgrösse und Ausdehnung in mm."""
    if seite == "unten":
        h = -hk.unten[:, ::-1]
        m = hk.maske[:, ::-1]
    elif seite == "unterseite":
        h, m = -hk.unten, hk.maske
    else:
        h, m = hk.oben, hk.maske
    k = max(1, int(math.ceil(max(h.shape) / max_pixel)))
    h, m = h[::k, ::k], m[::k, ::k]
    # Schattierung (überhöht, damit flache Reliefs lesbar sind) und etwas Höhe: hoch = heller
    if seite == "unterseite":            # im gewendeten Bild beleuchten, dann zurück
        sp = (lambda a: a[::-1, :]) if achse != "y" else (lambda a: a[:, ::-1])
        hell = sp(schattierung(sp(np.where(m, h, np.nan)) * ueberhoehung, hk.zelle * k))
    else:
        hell = schattierung(h * ueberhoehung, hk.zelle * k)
    if m.any():
        lo, hi = float(np.nanmin(h[m])), float(np.nanmax(h[m]))
        stufe = np.clip((np.nan_to_num(h, nan=lo) - lo) / max(hi - lo, 1e-9), 0, 1)
        hell = 0.7 * hell + 0.3 * stufe
    grau = (40 + 210 * hell).astype(np.uint8)
    alpha = np.where(m, 255, 0).astype(np.uint8)
    png_schreiben(pfad, np.dstack([grau, alpha])[::-1])     # Zeile 0 = oben im Bild
    ny, nx = h.shape
    p = hk.zelle * k                     # ein Bildpunkt deckt die Fläche um seinen Rasterpunkt
    return {"pixel": [nx, ny], "mm": [round(hk.x0 - p / 2, 4), round(hk.y0 - p / 2, 4),
                                      round(nx * p, 4), round(ny * p, 4)]}


def png_schreiben(pfad: str, bild: np.ndarray):
    """PNG ohne Zusatzpakete: Graustufen (2D) oder Graustufen + Alpha (H, B, 2), 8 Bit."""
    bild = np.ascontiguousarray(bild, dtype=np.uint8)
    farbtyp = 0 if bild.ndim == 2 else 4
    h, b = bild.shape[:2]
    roh = np.zeros((h, 1 + bild[0].size), np.uint8)
    roh[:, 1:] = bild.reshape(h, -1)

    def block(typ, daten):
        return struct.pack(">I", len(daten)) + typ + daten + \
            struct.pack(">I", zlib.crc32(typ + daten) & 0xFFFFFFFF)
    with open(pfad, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(block(b"IHDR", struct.pack(">IIBBBBB", b, h, 8, farbtyp, 0, 0, 0)))
        f.write(block(b"IDAT", zlib.compress(roh.tobytes(), 6)))
        f.write(block(b"IEND", b""))


# --------------------------------------------------------------------------------------------
# Befehl

def info(pfad: str, a: Optional[Dict] = None, zelle: Optional[float] = None,
         bild: Optional[str] = None, seite: str = "oben") -> Dict:
    modell = relief_import.lesen(pfad)
    hk = hoehenkarten(modell, a, zelle)
    erg = {"ok": True, "datei": pfad, "art": modell.art, "warnungen": [],
           "hinweise": list(modell.hinweise)}
    erg.update(kennzahlen(hk))
    if modell.dreiecke is not None:
        erg["dreiecke"] = int(len(modell.dreiecke))
        p = ausgerichtet(modell, ausrichtung(a)).reshape(-1, 3)     # Masse genau aus dem Netz
        erg["masse"] = [round(float(v), 2) for v in p.max(axis=0) - p.min(axis=0)]
    if modell.ist_bild:
        erg["bild_bits"] = modell.bild_bits
        erg["bildpunkte"] = [int(modell.bild.shape[1]), int(modell.bild.shape[0])]
    if modell.art == "step":
        erg["flaechen"] = len(modell.flaechen)
        erg["kanten"] = len(modell.kanten)
    if erg.get("hinterschnitt_mm2", 0) > 0:
        erg["hinweise"].append("Teile des Modells sind von oben verdeckt (Hinterschnitt) – sie "
                               "bleiben beim Fräsen stehen.")
    if bild:
        erg["bild"] = bild
        erg["bild_lage"] = schattenbild(hk, bild, seite)
    return erg


# --------------------------------------------------------------------------------------------
# In der Zeichnung gespeichertes Modell (Etappe «Einlesen und Platzieren»)
#
# Die Originaldatei steht gzip-komprimiert und base64-kodiert in <hirch:reliefdaten> in den
# <defs> der Zeichnung; das sichtbare Objekt ist ein <svg:image> mit dem Schattenbild und
# hirch:relief = {"name", "art", "ausrichtung", "lage", …}. Die Oberfläche gibt die Daten als
# Datei (--daten) an diese Befehle.

def daten_kodieren(pfad: str) -> str:
    import base64
    import gzip
    with open(pfad, "rb") as f:
        return base64.b64encode(gzip.compress(f.read(), 6)).decode("ascii")


def _cache_ordner() -> str:
    import os
    basis = os.environ.get("XDG_CACHE_HOME") or os.path.join(os.path.expanduser("~"), ".cache")
    ordner = os.path.join(basis, "hirch-cam", "relief")
    os.makedirs(ordner, exist_ok=True)
    return ordner


def modell_aus_daten(daten_pfad: str, name: str) -> Modell:
    """Liest ein gespeichertes Modell (Datei mit dem base64-Text). Gelesene Netze und Bilder
    kommen in einen Zwischenspeicher, damit Änderungen an der Ausrichtung schnell gehen."""
    with open(daten_pfad, "rb") as f:
        return modell_aus_text(f.read(), name)


def modell_aus_text(text, name: str) -> Modell:
    """Wie modell_aus_daten, aber mit dem base64-Text selbst (z. B. aus der Zeichnung)."""
    import base64
    import gzip
    import hashlib
    import os
    import tempfile
    if isinstance(text, str):
        text = text.encode("ascii", "ignore")
    text = b"".join(text.split())
    schluessel = hashlib.sha1(text).hexdigest()
    endung = os.path.splitext(name)[1].lower() or ".stl"
    cache = os.path.join(_cache_ordner(), schluessel + ".npz")
    if os.path.isfile(cache):
        try:
            z = np.load(cache, allow_pickle=False)
            art = str(z["art"])
            if art == "bild":
                return Modell(name, art, bild=z["bild"], bild_bits=int(z["bits"]))
            m = Modell(name, art, dreiecke=z["dreiecke"])
            if "kanten_pkt" in z.files:          # STEP: Kanten mit Z (Vektoren aus dem Modell)
                import json
                idx, pkt = z["kanten_idx"], z["kanten_pkt"]
                fl = json.loads(str(z["kanten_fl"]))
                m.kanten = [{"punkte": pkt[idx[i]:idx[i + 1]], "flaechen": fl[i]}
                            for i in range(len(idx) - 1)]
                if "flaechen2" in z.files:       # STEP: Lage der Flächen (Bohrungen, Taschen)
                    m.flaechen = json.loads(str(z["flaechen2"]))
                elif art == "step":
                    raise ValueError("alter Zwischenspeicher ohne Flächen")
            elif art == "step":
                raise ValueError("alter Zwischenspeicher ohne Kanten")
            return m
        except Exception:
            pass
    roh = gzip.decompress(base64.b64decode(text))
    with tempfile.TemporaryDirectory() as tmp:
        datei = os.path.join(tmp, "modell" + endung)
        with open(datei, "wb") as f:
            f.write(roh)
        m = relief_import.lesen(datei)
    m.quelle = name
    try:
        if m.ist_bild:
            np.savez(cache, art="bild", bild=m.bild, bits=m.bild_bits)
        elif m.kanten:
            import json
            pkt = [np.asarray(k["punkte"], np.float64).reshape(-1, 3) for k in m.kanten]
            idx = np.cumsum([0] + [len(q) for q in pkt])
            np.savez(cache, art=m.art, dreiecke=m.dreiecke, kanten_pkt=np.concatenate(pkt),
                     kanten_idx=idx, kanten_fl=json.dumps([list(k["flaechen"]) for k in m.kanten]),
                     flaechen2=json.dumps(m.flaechen, default=float))   # «2»: Dreiecke nach dem Bereinigen
        else:
            np.savez(cache, art=m.art, dreiecke=m.dreiecke)
    except OSError:
        pass
    return m


def z_versatz(hk: Hoehenkarten, lage: Optional[Dict], dicke: float) -> Dict:
    """Wo das Modell in Z liegt (Z = 0: Materialoberfläche, −dicke: Unterseite)."""
    return versatz_aus(float(np.nanmin(hk.unten)), float(np.nanmax(hk.oben)), lage, dicke)


def versatz_aus(zmin: float, zmax: float, lage: Optional[Dict], dicke: float) -> Dict:
    """Wie z_versatz, aus tiefster und höchster Stelle des Modells."""
    lage = lage or {}
    art = lage.get("art") or "mittig"
    if art == "oben":
        off = -zmax
    elif art == "tiefe":
        off = -float(lage.get("tiefe") or 0.0) - zmax
    else:
        off = -dicke / 2.0 - (zmin + zmax) / 2.0
    return {"z_versatz": round(off, 4), "rand_oben": round(-(zmax + off), 3),
            "rand_unten": round(dicke + zmin + off, 3),
            "raus_oben": round(max(0.0, zmax + off), 3),
            "raus_unten": round(max(0.0, -dicke - (zmin + off)), 3)}


def netz_schreiben(hk: Hoehenkarten, pfad: str):
    """Grobes Höhenraster für die 3D-Ansicht: int32 nx, ny; float32 zelle, x0, y0;
    dann Oberseite und Unterseite (je ny·nx float32, NaN ausserhalb)."""
    ny, nx = hk.form
    with open(pfad, "wb") as f:
        f.write(np.array([nx, ny], "<i4").tobytes())
        f.write(np.array([hk.zelle, hk.x0, hk.y0], "<f4").tobytes())
        f.write(np.ascontiguousarray(hk.oben, "<f4").tobytes())
        f.write(np.ascontiguousarray(hk.unten, "<f4").tobytes())


def _passt(b, h, mb, mh, rand):
    return b <= mb - 2 * rand + 1e-6 and h <= mh - 2 * rand + 1e-6


def ansicht(modell: Modell, ordner: str, a: Optional[Dict] = None, lage: Optional[Dict] = None,
            dicke: float = 18.0, max_netz: int = 360, achse: str = "x") -> Dict:
    """Schattenbild (ordner/bild.png), bei Netzen auch die Unterseite für Seite 2 der Zeichnung
    (ordner/bild_unten.png, gleiche Lage wie bild.png), Netz für die 3D-Ansicht (ordner/netz.bin)
    und Kennzahlen."""
    import os
    a = ausrichtung(a)
    hk = hoehenkarten(modell, a)
    erg = {"ok": True, "art": modell.art, "warnungen": [], "hinweise": list(modell.hinweise),
           "ausrichtung": a}
    erg.update(kennzahlen(hk))
    if modell.dreiecke is not None:
        p = ausgerichtet(modell, a).reshape(-1, 3)
        erg["masse"] = [round(float(v), 2) for v in p.max(axis=0) - p.min(axis=0)]
    erg.update(z_versatz(hk, lage, dicke))
    dateien = erg["dateien"] = {"bild": os.path.join(ordner, "bild.png"),
                                "netz": os.path.join(ordner, "netz.bin")}
    erg["bild_lage"] = schattenbild(hk, dateien["bild"], "oben", max_pixel=3200)
    if not modell.ist_bild:              # ein Graustufenbild hat keine eigene Unterseite
        dateien["bild_unten"] = os.path.join(ordner, "bild_unten.png")
        schattenbild(hk, dateien["bild_unten"], "unterseite", max_pixel=3200, achse=achse)
    # grobes Netz: höchstens max_netz Punkte auf der langen Seite
    b, h = erg["masse"][0], erg["masse"][1]
    zg = max(hk.zelle, max(b, h) / max_netz)
    grob = hk if abs(zg - hk.zelle) < 1e-9 else hoehenkarten(modell, a, zelle=zg)
    netz_schreiben(grob, dateien["netz"])
    erg["netz_raster"] = [int(grob.form[1]), int(grob.form[0]), grob.zelle]
    if erg.get("hinterschnitt_mm2", 0) > 0:
        erg["hinweise"].append("Teile des Modells sind von oben verdeckt (Hinterschnitt) – sie "
                               "bleiben beim Fräsen stehen.")
    if erg["raus_oben"] > 0.005:
        erg["warnungen"].append("Das Modell ragt %s mm aus dem Material." % _komma(erg["raus_oben"]))
    if erg["raus_unten"] > 0.005:
        erg["warnungen"].append("Unten fehlen %s mm Material – dort wird das Modell abgeschnitten."
                                % _komma(erg["raus_unten"]))
    return erg


def _komma(v: float) -> str:
    return ("%.2f" % v).replace(".", ",")


def einbetten(pfad: str, ordner: str, a: Optional[Dict] = None, lage: Optional[Dict] = None,
              material: Optional[Sequence[float]] = None, rand: float = 10.0) -> Dict:
    """Liest eine Datei für die Zeichnung: Daten (ordner/daten.txt), Schattenbild, Netz,
    Kennzahlen. Mit `material` (Breite, Höhe, Dicke): passt das Modell nur um 90° gedreht
    hinein, wird es gedreht (drehen_z = 90)."""
    import os
    modell = relief_import.lesen(pfad)
    a = ausrichtung(a)
    dicke = float(material[2]) if material else 18.0
    gedreht = False
    if material:
        mb, mh = float(material[0]), float(material[1])
        p = ausgerichtet(modell, a).reshape(-1, 3) if modell.dreiecke is not None else None
        if p is not None:
            b, h = (p.max(axis=0) - p.min(axis=0))[:2]
        else:
            b = float(a["breite"]) * float(a["skalierung"])
            h = b * modell.bild.shape[0] / modell.bild.shape[1]
        if not _passt(b, h, mb, mh, rand) and _passt(h, b, mb, mh, rand):
            a["drehen_z"] = (float(a.get("drehen_z") or 0) + 90.0) % 360
            gedreht = True
    erg = ansicht(modell, ordner, a, lage, dicke)
    d = erg["dateien"]
    d["original"] = os.path.abspath(pfad)
    d["name"] = os.path.basename(pfad)
    d["daten"] = os.path.join(ordner, "daten.txt")
    with open(d["daten"], "w", encoding="ascii") as f:
        f.write(daten_kodieren(pfad))
    erg["daten_bytes"] = os.path.getsize(d["daten"])
    erg["original_zeit"] = int(os.path.getmtime(pfad))
    erg["gedreht"] = gedreht
    if gedreht:
        erg["hinweise"].append("Um 90° gedreht, damit es ins Material passt.")
    if modell.dreiecke is not None:
        erg["dreiecke"] = int(len(modell.dreiecke))
    if modell.ist_bild:
        erg["bild_bits"] = modell.bild_bits
        erg["bildpunkte"] = [int(modell.bild.shape[1]), int(modell.bild.shape[0])]
    if modell.art == "step":
        erg["flaechen"] = len(modell.flaechen)
        erg["kanten"] = len(modell.kanten)
    if material:
        b, h = erg["masse"][0], erg["masse"][1]
        if not _passt(b, h, float(material[0]), float(material[1]), 0):
            erg["warnungen"].append("Das Modell (%s × %s mm) ist grösser als das Material." %
                                    (_komma(b), _komma(h)))
    return erg


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv and argv[0] in ("einbetten", "ansicht"):
        return _main_neu(argv)
    if argv and argv[0] == "stege":
        return _main_stege(argv[1:])
    if argv and argv[0] == "vektoren":
        return _main_vektoren(argv[1:])
    if argv and argv[0] == "merkmale":              # Bohrungen und Taschen (STEP)
        return _main_merkmale(argv[1:])
    if argv and argv[0] == "flaeche":               # Umriss der angeklickten Fläche
        return _main_flaeche(argv[1:])
    if argv and argv[0] == "ergebnis":              # Fräsergebnis für den Reiter «3D»
        from .ops import relief_ergebnis
        return relief_ergebnis.main(argv[1:])
    return _main_info(argv)


def _main_neu(argv) -> int:
    ap = argparse.ArgumentParser(prog="relief")
    ap.add_argument("befehl", choices=["einbetten", "ansicht"])
    ap.add_argument("datei", nargs="?", help="einbetten: die 3D-Datei oder das Bild")
    ap.add_argument("--daten", help="ansicht: Datei mit den gespeicherten Daten")
    ap.add_argument("--name", help="ansicht: Dateiname des Originals (für das Format)")
    ap.add_argument("--ordner", required=True)
    ap.add_argument("--ausrichtung")
    ap.add_argument("--lage")
    ap.add_argument("--material", help="Breite,Höhe,Dicke in mm")
    ap.add_argument("--dicke", type=float)
    ap.add_argument("--achse", choices=["x", "y"], default="x", help="Wenden um (Licht der Unterseite)")
    a = ap.parse_args(argv)
    try:
        ausr = json.loads(a.ausrichtung) if a.ausrichtung else None
        lage = json.loads(a.lage) if a.lage else None
        mat = [float(v) for v in a.material.split(",")] if a.material else None
        if a.befehl == "einbetten":
            erg = einbetten(a.datei, a.ordner, ausr, lage, mat)
        else:
            modell = modell_aus_daten(a.daten, a.name or "modell.stl")
            dicke = a.dicke if a.dicke is not None else (mat[2] if mat else 18.0)
            erg = ansicht(modell, a.ordner, ausr, lage, dicke, achse=a.achse)
    except EinleseFehler as ex:
        erg = {"ok": False, "warnungen": [str(ex)]}
    except Exception as ex:
        import traceback
        erg = {"ok": False, "warnungen": [f"Fehler: {ex}"], "traceback": traceback.format_exc()}
    sys.stdout.write(json.dumps(erg, ensure_ascii=False))
    sys.stdout.flush()
    return 0


def _main_stege(argv) -> int:
    """relief stege --daten DATEI --info JSON --ecken JSON --dicke D [--stege JSON]
    [--anzahl N --breite B --laenge L]: Trennlinie und Höhen der Haltestege (Reiter «3D»);
    mit --anzahl ein Vorschlag für neue Stege. Koordinaten: Material-mm."""
    ap = argparse.ArgumentParser(prog="relief stege")
    ap.add_argument("--daten", required=True)
    ap.add_argument("--info", required=True)
    ap.add_argument("--ecken", required=True, help="Bildecken oben links, oben rechts, unten links")
    ap.add_argument("--dicke", type=float, required=True)
    ap.add_argument("--stege", default="[]")
    ap.add_argument("--anzahl", type=int, default=0)
    ap.add_argument("--breite", type=float)
    ap.add_argument("--laenge", type=float)
    a = ap.parse_args(argv)
    try:
        from .ops.relief_zweiseitig import stege_info
        with open(a.daten, encoding="ascii") as f:
            text = f.read()
        relief = {"info": json.loads(a.info), "daten": text,
                  "ecken": [tuple(p) for p in json.loads(a.ecken)][:3],
                  "stege": [{"id": s.get("id", ""), "ecken": [tuple(p) for p in s["ecken"]],
                             "z": s.get("z")} for s in json.loads(a.stege)]}
        erg = stege_info(relief, a.dicke, a.anzahl, a.breite, a.laenge)
    except Exception as ex:
        import traceback
        erg = {"ok": False, "warnungen": [f"Fehler: {ex}"], "traceback": traceback.format_exc()}
    sys.stdout.write(json.dumps(erg, ensure_ascii=False))
    sys.stdout.flush()
    return 0


def _main_vektoren(argv) -> int:
    """relief vektoren --daten DATEI --info JSON --ecken JSON --dicke D --art umriss|hoehenlinie|
    kanten [--z Z] [--material B,H --achse x|y]: Linien aus dem Modell (Material-mm; Kanten der
    Unterseite in den Koordinaten von Seite 2)."""
    ap = argparse.ArgumentParser(prog="relief vektoren")
    ap.add_argument("--daten", required=True)
    ap.add_argument("--info", required=True)
    ap.add_argument("--ecken", required=True)
    ap.add_argument("--dicke", type=float, required=True)
    ap.add_argument("--art", required=True, choices=["umriss", "hoehenlinie", "kanten"])
    ap.add_argument("--z", type=float)
    ap.add_argument("--material", help="Breite,Höhe in mm (für Seite 2)")
    ap.add_argument("--achse", default="x")
    a = ap.parse_args(argv)
    try:
        from .ops.relief_vektoren import vektoren
        with open(a.daten, encoding="ascii") as f:
            text = f.read()
        relief = {"info": json.loads(a.info), "daten": text,
                  "ecken": [tuple(p) for p in json.loads(a.ecken)][:3]}
        mat = None
        if a.material:
            b, h = (float(v) for v in a.material.split(",")[:2])
            mat = (b, h, a.dicke)
        erg = vektoren(relief, a.dicke, a.art, a.z, mat, a.achse)
    except Exception as ex:
        import traceback
        erg = {"ok": False, "warnungen": [f"Fehler: {ex}"], "traceback": traceback.format_exc()}
    sys.stdout.write(json.dumps(erg, ensure_ascii=False))
    sys.stdout.flush()
    return 0


def _main_merkmale(argv) -> int:
    """relief merkmale --daten DATEI --info JSON --ecken JSON --dicke D: Bohrungen und Taschen
    eines STEP-Modells (Material-mm, Seite 1), siehe ops/relief_merkmale.py."""
    ap = argparse.ArgumentParser(prog="relief merkmale")
    ap.add_argument("--daten", required=True)
    ap.add_argument("--info", required=True)
    ap.add_argument("--ecken", required=True)
    ap.add_argument("--dicke", type=float, required=True)
    ap.add_argument("--material", help="Breite,Höhe in mm (für Seite 2)")
    ap.add_argument("--achse", default="x")
    a = ap.parse_args(argv)
    try:
        from .ops.relief_merkmale import merkmale
        with open(a.daten, encoding="ascii") as f:
            text = f.read()
        relief = {"info": json.loads(a.info), "daten": text,
                  "ecken": [tuple(p) for p in json.loads(a.ecken)][:3]}
        mat = None
        if a.material:
            b_, h_ = (float(v) for v in a.material.split(",")[:2])
            mat = (b_, h_, a.dicke)
        erg = merkmale(relief, a.dicke, material=mat, achse=a.achse)
        bw = erg.get("boeden_waende")
        if bw and bw.get("rundungen"):
            # Kugelfräser für die Rundungen am Fuss aus «Meine Werkzeuge»: Radius wie die Rundung oder kleiner
            from . import werkzeugdb
            r_ = max(x["radius"] for x in bw["rundungen"])
            kugeln = [w for w in werkzeugdb.laden().get("werkzeuge", [])
                      if w.get("typ") == "kugel" and 0 < float(w.get("durchmesser") or 0) / 2 <= r_ + 0.01]
            if kugeln:
                k_ = max(kugeln, key=lambda w: (float(w["durchmesser"]), float(w.get("schneidenlaenge") or 0)))
                bw["kugel"] = dict({k: v for k, v in k_.items() if k not in ("id", "notiz")}, db_id=k_.get("id"),
                                   auto=True)
    except Exception as ex:
        import traceback
        erg = {"ok": False, "warnungen": [f"Fehler: {ex}"], "traceback": traceback.format_exc()}
    sys.stdout.write(json.dumps(erg, ensure_ascii=False))
    sys.stdout.flush()
    return 0


def _main_flaeche(argv) -> int:
    """relief flaeche --daten DATEI --info JSON --ecken JSON --dicke D --x X --y Y [--z Z] [--winkel W]
    [--seite 1|2 --material B,H --achse x|y]: Umriss der Fläche des Modells an der Stelle
    (Material-mm), siehe ops/relief_flaeche.py.
    relief flaeche --vereinigen DATEI [--seite --material --achse]: Umrisse mehrerer Flächen zu einem
    Bereich (DATEI: JSON, Liste von Umrissen, je eine Liste von Ringen)."""
    ap = argparse.ArgumentParser(prog="relief flaeche")
    ap.add_argument("--vereinigen")
    for k in ("--daten", "--info", "--ecken"):
        ap.add_argument(k)
    for k in ("--dicke", "--x", "--y", "--z"):
        ap.add_argument(k, type=float)
    ap.add_argument("--winkel", type=float, default=5.0)
    ap.add_argument("--seite", type=int, default=1)
    ap.add_argument("--material", help="Breite,Höhe in mm (für die Linien auf Seite 2)")
    ap.add_argument("--achse", default="x")
    a = ap.parse_args(argv)
    try:
        from .ops.relief_flaeche import flaeche, vereinigen
        mat = tuple(float(v) for v in a.material.split(",")[:2]) if a.material else None
        if a.vereinigen is not None:
            with open(a.vereinigen, encoding="utf-8") as f:
                erg = vereinigen(json.load(f), a.seite, mat, a.achse)
        else:
            with open(a.daten, encoding="ascii") as f:
                text = f.read()
            relief = {"info": json.loads(a.info), "daten": text,
                      "ecken": [tuple(p) for p in json.loads(a.ecken)][:3]}
            erg = flaeche(relief, a.dicke, a.x, a.y, a.winkel, z=a.z, seite=a.seite, material=mat, achse=a.achse)
    except Exception as ex:
        import traceback
        erg = {"ok": False, "warnungen": [f"Fehler: {ex}"], "traceback": traceback.format_exc()}
    sys.stdout.write(json.dumps(erg, ensure_ascii=False))
    sys.stdout.flush()
    return 0


def _main_info(argv) -> int:
    ap = argparse.ArgumentParser(prog="relief")
    ap.add_argument("datei")
    ap.add_argument("--zelle", type=float)
    ap.add_argument("--ausrichtung", help="JSON, siehe STANDARD_AUSRICHTUNG")
    ap.add_argument("--bild", help="Schattenbild (PNG) hierhin schreiben")
    ap.add_argument("--seite", choices=["oben", "unten"], default="oben")
    a = ap.parse_args(argv)
    try:
        erg = info(a.datei, json.loads(a.ausrichtung) if a.ausrichtung else None, a.zelle,
                   a.bild, a.seite)
    except EinleseFehler as ex:
        erg = {"ok": False, "warnungen": [str(ex)]}
    except Exception as ex:
        import traceback
        erg = {"ok": False, "warnungen": [f"Fehler: {ex}"], "traceback": traceback.format_exc()}
    sys.stdout.write(json.dumps(erg, ensure_ascii=False))
    sys.stdout.flush()
    return 0
