# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""3D-Modelle einlesen: STL, OBJ, STEP und Graustufenbilder.

Ergebnis ist immer ein `Modell`: entweder ein Dreiecksnetz (STL, OBJ, STEP) oder ein
Höhenbild (Graustufenbild, hell = hoch). STEP liefert zusätzlich die Kanten mit den Arten der
angrenzenden Flächen (exakt aus dem Volumenkörper) – daraus entstehen später Vektoren, z. B.
der Rand einer Tasche für die 3D-Fase.
"""
from __future__ import annotations

import os
import struct
from dataclasses import dataclass, field
from typing import Dict, List, Optional

import numpy as np

ENDUNGEN_NETZ = (".stl", ".obj", ".step", ".stp")
ENDUNGEN_BILD = (".png", ".jpg", ".jpeg", ".tif", ".tiff", ".bmp")


class EinleseFehler(Exception):
    pass


@dataclass
class Modell:
    quelle: str
    art: str                                   # stl, obj, step, bild
    dreiecke: Optional[np.ndarray] = None      # (N, 3, 3) float32, mm
    kanten: List[Dict] = field(default_factory=list)    # STEP: {"punkte": (M, 3), "flaechen": [...]}
    flaechen: List[Dict] = field(default_factory=list)  # STEP: {"art", "flaeche"}
    bild: Optional[np.ndarray] = None          # Höhen 0…1, float32, Zeile 0 = oberer Bildrand
    bild_bits: int = 0
    hinweise: List[str] = field(default_factory=list)

    @property
    def ist_bild(self) -> bool:
        return self.bild is not None


def lesen(pfad: str, abweichung: float = 0.02) -> Modell:
    """Liest eine 3D-Datei oder ein Graustufenbild. `abweichung`: Vernetzungsgenauigkeit
    für STEP (mm)."""
    if not os.path.isfile(pfad):
        raise EinleseFehler(f"Datei nicht gefunden: {pfad}")
    endung = os.path.splitext(pfad)[1].lower()
    if endung == ".stl":
        m = Modell(pfad, "stl", dreiecke=stl_lesen(pfad))
    elif endung == ".obj":
        m = Modell(pfad, "obj", dreiecke=obj_lesen(pfad))
    elif endung in (".step", ".stp"):
        m = step_lesen(pfad, abweichung)
    elif endung in ENDUNGEN_BILD:
        hoehe, bits = bild_lesen(pfad)
        m = Modell(pfad, "bild", bild=hoehe, bild_bits=bits)
        if bits <= 8:
            m.hinweise.append("8-Bit-Bild: nur 256 Höhenstufen – bei tiefen Reliefs glätten "
                              "oder ein 16-Bit-PNG verwenden.")
    else:
        raise EinleseFehler(f"Unbekanntes Format: {endung or pfad}")
    if m.dreiecke is not None:
        roh = np.ascontiguousarray(m.dreiecke, dtype=np.float32)
        ok = _gueltig(roh)
        m.dreiecke = roh[ok]
        if m.flaechen:
            # STEP: die Flächen verweisen auf ihre Dreiecke [von, bis) – nach dem Bereinigen neu zählen
            neu = np.concatenate([[0], np.cumsum(ok)])
            for f in m.flaechen:
                if "dreiecke" in f:
                    von, bis = (int(v) for v in f["dreiecke"])
                    f["dreiecke"] = [int(neu[von]), int(neu[bis])]
        if len(m.dreiecke) == 0:
            raise EinleseFehler("Die Datei enthält keine Flächen.")
    return m


# --------------------------------------------------------------------------------------------
# STL

def stl_lesen(pfad: str) -> np.ndarray:
    with open(pfad, "rb") as f:
        daten = f.read()
    if len(daten) >= 84:
        n = struct.unpack_from("<I", daten, 80)[0]
        if 84 + 50 * n == len(daten):                    # binär (auch wenn der Kopf «solid» sagt)
            a = np.frombuffer(daten, dtype=np.dtype([("n", "<f4", 3), ("v", "<f4", (3, 3)),
                                                     ("a", "<u2")]), count=n, offset=84)
            return np.ascontiguousarray(a["v"], dtype=np.float32)
    text = daten.decode("ascii", errors="replace")
    if not text.lstrip().lower().startswith("solid"):
        raise EinleseFehler("Keine gültige STL-Datei.")
    werte = []
    for zeile in text.splitlines():
        t = zeile.split()
        if len(t) == 4 and t[0].lower() == "vertex":
            werte.append((float(t[1]), float(t[2]), float(t[3])))
    if len(werte) % 3:
        raise EinleseFehler("STL-Datei unvollständig.")
    return np.asarray(werte, dtype=np.float32).reshape(-1, 3, 3)


# --------------------------------------------------------------------------------------------
# OBJ (nur Geometrie: v und f; Vielecke werden als Fächer zerlegt)

def obj_lesen(pfad: str) -> np.ndarray:
    punkte: List[tuple] = []
    tri: List[tuple] = []
    with open(pfad, encoding="utf-8", errors="replace") as f:
        for zeile in f:
            if zeile.startswith("v "):
                t = zeile.split()
                punkte.append((float(t[1]), float(t[2]), float(t[3])))
            elif zeile.startswith("f "):
                idx = []
                for teil in zeile.split()[1:]:
                    i = int(teil.split("/")[0])
                    idx.append(i - 1 if i > 0 else len(punkte) + i)
                for k in range(1, len(idx) - 1):
                    tri.append((idx[0], idx[k], idx[k + 1]))
    if not punkte or not tri:
        raise EinleseFehler("OBJ-Datei ohne Flächen.")
    p = np.asarray(punkte, dtype=np.float32)
    return p[np.asarray(tri, dtype=np.int64)]


# --------------------------------------------------------------------------------------------
# STEP (über das Modul hirch_relief mit OpenCASCADE)

def step_moeglich() -> bool:
    try:
        from . import hirch_relief
    except ImportError:
        return False
    return bool(hirch_relief.mit_step())


def step_lesen(pfad: str, abweichung: float = 0.02) -> Modell:
    if not step_moeglich():
        raise EinleseFehler("STEP-Dateien brauchen OpenCASCADE (Paket libocct-data-exchange). "
                            "Bitte als STL ausgeben oder Hirch CAM aktualisieren.")
    from . import hirch_relief
    try:
        erg = hirch_relief.step_lesen(pfad, float(abweichung))
    except RuntimeError as ex:
        raise EinleseFehler(str(ex)) from None
    return Modell(pfad, "step", dreiecke=np.asarray(erg["dreiecke"], dtype=np.float32),
                  kanten=[{"punkte": np.asarray(k["punkte"]), "flaechen": list(k["flaechen"])}
                          for k in erg["kanten"]],
                  flaechen=list(erg["flaechen"]))


# --------------------------------------------------------------------------------------------
# Graustufenbild: hell = hoch, 8 oder 16 Bit

def bild_lesen(pfad: str):
    try:
        from PIL import Image
    except ImportError:
        raise EinleseFehler("Bilder einlesen braucht das Paket python3-pil.") from None
    with Image.open(pfad) as im:
        modus = im.mode
        if modus in ("I;16", "I;16B", "I;16L", "I"):
            a = np.asarray(im, dtype=np.float64)
            bits = 16
            voll = 65535.0 if modus != "I" or a.max() > 255 else 255.0
        else:
            if modus in ("RGBA", "LA", "PA") or (modus == "P" and "transparency" in im.info):
                im = im.convert("RGBA")
                alpha = np.asarray(im)[..., 3]
                hell = np.asarray(im.convert("L"), dtype=np.float64)
                hell[alpha == 0] = 0.0                 # durchsichtig = tiefste Stelle
                a = hell
            else:
                a = np.asarray(im.convert("L"), dtype=np.float64)
            bits, voll = 8, 255.0
    return np.clip(a / voll, 0.0, 1.0).astype(np.float32), bits


# --------------------------------------------------------------------------------------------

def _gueltig(d: np.ndarray) -> np.ndarray:
    """Welche Dreiecke gültige Zahlen und eine Fläche haben."""
    ok = np.isfinite(d).all(axis=(1, 2))
    kreuz = np.cross(d[:, 1] - d[:, 0], d[:, 2] - d[:, 0])
    ok &= np.einsum("ij,ij->i", kreuz, kreuz) > 1e-18
    return ok


def _bereinigen(d: np.ndarray) -> np.ndarray:
    """Entfernt Dreiecke mit ungültigen Zahlen oder ohne Fläche."""
    d = np.ascontiguousarray(d, dtype=np.float32)
    return d[_gueltig(d)]
