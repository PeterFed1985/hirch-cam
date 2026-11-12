# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Einlinienschrift mit optischem Buchstabenabstand.

Übernommen aus dem Prototyp "Frässchrift Studio". Der optische Abstand
berücksichtigt die Fräserbreite: Buchstaben werden so zusammengeschoben,
dass die gefrästen Rillen überall etwa gleich viel "Luft" haben – statt
fester Zeichenbreiten wie in üblichen CAM-Programmen.

Reines Python (läuft auch mit dem Python 3.8 aus dem Inkscape-AppImage).
Koordinaten im Ergebnis: mm, y nach oben, Grundlinie der ersten Zeile bei 0.
"""
from __future__ import annotations

import json
import math
import os
import unicodedata
from typing import Dict, List, Tuple

_DATEI = os.path.join(os.path.dirname(os.path.abspath(__file__)), "schriften", "einlinien.json")
_CACHE: Dict = {}

Linie = List[Tuple[float, float]]
NB = 64  # Höhenbänder für das Profil


def schriften() -> Dict:
    """Mitgelieferte Schriften und die eigenen aus der Schriftbibliothek
    (~/.config/hirch-cam/schriften, siehe schriftimport)."""
    if not _CACHE:
        with open(_DATEI, encoding="utf-8") as fh:
            _CACHE.update(json.load(fh))
        from . import schriftimport
        for name, s in schriftimport.bibliothek().items():
            _CACHE.setdefault(name, s)
    return _CACHE


def neu_laden():
    _CACHE.clear()


def schriftnamen() -> List[str]:
    return list(schriften())


_ERSATZ = {"–": "-", "—": "-", "‐": "-", "−": "-", "„": "\"", "“": "\"", "”": "\"", "«": "<", "»": ">",
           "‚": ",", "‘": "'", "’": "'", "´": "'", "`": "'", "…": ".", "⌀": "ØøO", "Ø": "O", "ø": "o",
           "×": "x", "·": ".", "\u00a0": " "}


class _Schrift:
    def __init__(self, name):
        f = schriften()[name]
        self.cap = f["cap"]
        self.xh = f["xh"]
        self.lizenz = f["lic"]
        self._g = f["g"]
        self.kern = f.get("kern") or {}

    def zeichen(self, ch):
        g = self._g.get(ch)
        if not g:
            # ähnliches Zeichen, wenn die Schrift es nicht hat
            for ersatz in _ERSATZ.get(ch, ""):
                g = self._g.get(ersatz)
                if g:
                    break
        g = g or self._g.get("?")
        if not g:
            return 500.0, []
        return float(g[0]), g[1]


def versatz(p: Linie, d: float) -> Linie:
    """Polylinie seitlich um d versetzen (für 2- und 3-Linien-Schrift)."""
    n = len(p)
    if n < 2 or d == 0:
        return list(p)
    zu = n > 2 and math.dist(p[0], p[-1]) < 1e-6
    out = []
    for i in range(n):
        ip = (n - 2 if i == 0 else i - 1) if zu else max(0, i - 1)
        inx = (1 if i == n - 1 else i + 1) if zu else min(n - 1, i + 1)
        tx = ty = 0.0
        if ip != i:
            dx, dy = p[i][0] - p[ip][0], p[i][1] - p[ip][1]
            L = math.hypot(dx, dy) or 1
            tx += dx / L
            ty += dy / L
        if inx != i:
            dx, dy = p[inx][0] - p[i][0], p[inx][1] - p[i][1]
            L = math.hypot(dx, dy) or 1
            tx += dx / L
            ty += dy / L
        L = math.hypot(tx, ty) or 1
        nx, ny = -ty / L, tx / L
        k = 1.0
        if ip != i and inx != i:  # Gehrung begrenzen
            dx, dy = p[inx][0] - p[i][0], p[inx][1] - p[i][1]
            L2 = math.hypot(dx, dy) or 1
            c = nx * (-dy / L2) + ny * (dx / L2)
            k = 1 / max(0.5, c)
        out.append((p[i][0] + nx * d * k, p[i][1] + ny * d * k))
    return out


class _Profil:
    """Linker und rechter Rand eines Zeichens je Höhenband (inkl. Fräserradius)."""

    def __init__(self, striche, cap, r):
        self.y0 = -0.4 * cap
        y1 = 1.3 * cap
        self.bh = (y1 - self.y0) / NB
        self.L = [math.inf] * NB
        self.R = [-math.inf] * NB
        schritt = cap / 60
        for s in striche:
            for i in range(0, len(s) - 2, 2):
                ax, ay, bx, by = s[i], s[i + 1], s[i + 2], s[i + 3]
                n = max(1, int(math.ceil(math.hypot(bx - ax, by - ay) / schritt)))
                for k in range(n + 1):
                    self._add(ax + (bx - ax) * k / n, ay + (by - ay) * k / n, r)
        fin_l = [v for v in self.L if math.isfinite(v)]
        fin_r = [v for v in self.R if math.isfinite(v)]
        self.leer = not fin_l
        self.minL = min(fin_l) if fin_l else 0.0
        self.maxR = max(fin_r) if fin_r else 0.0
        self.iy0 = int(math.floor((0 - self.y0) / self.bh))
        self.iy1 = int(math.ceil((cap - self.y0) / self.bh))

    def _add(self, x, y, r):
        b0 = int(math.floor((y - r - self.y0) / self.bh))
        b1 = int(math.floor((y + r - self.y0) / self.bh))
        for b in range(max(0, b0), min(NB - 1, b1) + 1):
            yc = self.y0 + (b + 0.5) * self.bh
            dy = min(abs(yc - y), r)
            w = math.sqrt(max(0.0, r * r - dy * dy))
            if x - w < self.L[b]:
                self.L[b] = x - w
            if x + w > self.R[b]:
                self.R[b] = x + w


def _paar_abstand(A: _Profil, B: _Profil, T, tiefe, min_luecke):
    summe, n, noetig = 0.0, 0, -math.inf
    for b in range(A.iy0, A.iy1):
        ar = max(A.R[b], A.maxR - tiefe) if math.isfinite(A.R[b]) else A.maxR - tiefe
        bl = min(B.L[b], B.minL + tiefe) if math.isfinite(B.L[b]) else B.minL + tiefe
        summe += bl - ar
        n += 1
    for b in range(NB):
        if math.isfinite(A.R[b]) and math.isfinite(B.L[b]):
            noetig = max(noetig, min_luecke - (B.L[b] - A.R[b]))
    return max(T - summe / max(1, n), noetig)


# Bezugswerte für Schriften im Textwerkzeug (TTF): Rille 0,5 mm bei 10 mm Versalhöhe, Luft 22 %
# – wie die Vorgaben im Gravurtext. Die Abstände wachsen mit der Schriftgrösse mit.
RILLE_ANTEIL = 0.05
LUFT_ANTEIL = 0.22


def _abstaende(profile: Dict, T, tiefe, min_luecke):
    """Wie _paar_abstand, aber für ein Zeichen gegen viele auf einmal (numpy; sonst langsam bei
    Schriften mit vielen Zeichen). Liefert f(a, liste) -> Abstände."""
    try:
        import numpy as np
    except ImportError:                                # ohne numpy: einzeln rechnen
        return lambda a, liste: [_paar_abstand(profile[a], profile[b], T, tiefe, min_luecke) for b in liste]
    namen = list(profile)
    idx = {c: i for i, c in enumerate(namen)}
    L = np.array([profile[c].L for c in namen])
    R = np.array([profile[c].R for c in namen])
    minL = np.array([profile[c].minL for c in namen])

    def f(a, liste):
        A = profile[a]
        j = np.array([idx[b] for b in liste])
        bands = np.arange(A.iy0, A.iy1)
        ar = np.maximum(np.array(A.R)[bands], A.maxR - tiefe)                 # (nb,)
        bl = np.minimum(L[j][:, bands], (minL[j] + tiefe)[:, None])           # (n, nb)
        mittel = (bl - ar[None, :]).sum(axis=1) / max(1, len(bands))
        aR = np.array(A.R)
        beide = np.isfinite(L[j]) & np.isfinite(aR)[None, :]
        luecke = np.where(beide, min_luecke - (L[j] - aR[None, :]), -np.inf)
        noetig = luecke.max(axis=1)
        return list(np.maximum(T - mittel, noetig))
    return f


def _paar_zeichen(c: str) -> bool:
    """Zeichen mit Paar-Unterschneidung: ASCII, Latin-1, typografische Satzzeichen und €."""
    o = ord(c)
    return 0x21 <= o <= 0x7E or 0xA1 <= o <= 0xFF or c in "–—‘’‚“”„…€‹›"


def optische_metrik(schrift: Dict, rille: float = RILLE_ANTEIL, luft: float = LUFT_ANTEIL):
    """Optischer Abstand als feste Breiten plus Paar-Unterschneidung (Einheiten der Schrift).

    Liefert (breiten, paare): breiten[ch] = Vorschub, paare[ch1 + ch2] = Korrektur, so dass
    Vorschub(ch1) + Korrektur genau den optischen Abstand ergibt, den der Gravurtext rechnet.
    Das Leerzeichen bekommt den Wortabstand; Paare mit dem Leerzeichen setzen die Ränder."""
    cap = float(schrift["cap"])
    r = rille * cap / 2
    T = luft * cap
    tiefe, min_luecke = 0.2 * cap, 0.35 * T
    profile = {}
    for ch, (vorschub, striche) in schrift["g"].items():
        if len(ch) != 1 or not ch.strip():
            continue
        P = _Profil(striche, cap, r)
        if not P.leer:
            profile[ch] = P
    breiten, paare = {}, {}
    zeichen = sorted(profile)
    # Paare nur unter den gängigen Zeichen (sonst wächst die Tabelle quadratisch); seltene Zeichen
    # bekommen nur ihren Vorschub
    paar_zeichen = [c for c in zeichen if _paar_zeichen(c)]
    bezug = [c for c in "HnoOlx" if c in profile] or zeichen[:6]
    abstand = _abstaende(profile, T, tiefe, min_luecke)
    for a in zeichen:
        A = profile[a]
        werte = sorted(abstand(a, bezug))
        adv = max(0.0, werte[len(werte) // 2])        # Vorschub: Abstand zu typischen Nachbarn
        if unicodedata.category(a).startswith("M"):   # Akzent zum Kombinieren: wie in der Vorlage
            breiten[a] = max(0.0, float(schrift["g"][a][0]))
            continue
        breiten[a] = adv
        if not _paar_zeichen(a):
            continue
        for b, d in zip(paar_zeichen, abstand(a, paar_zeichen)):
            korr = d - adv
            if abs(korr) > 0.004 * cap:
                paare[a + b] = korr
        paare[a + " "] = A.maxR + T / 2 - adv           # Ende eines Worts
    for ch, (vorschub, _s) in schrift["g"].items():
        if len(ch) == 1 and not ch.strip():
            breiten[ch] = min(max(float(vorschub), 0.25 * cap), 0.42 * cap)
    for b in paar_zeichen:
        paare[" " + b] = T / 2 - profile[b].minL         # Anfang eines Worts
    return breiten, paare


def _zeile(text, F: _Schrift, optisch, T, r, unterschneidung=True):
    platziert = []
    vorher = None
    x = 0.0
    leer = 0.0
    tiefe, min_luecke = 0.2 * F.cap, 0.35 * T
    vorher_ch = None
    for ch in text:
        adv, striche = F.zeichen(ch)
        if not ch.strip():
            vorher_ch = None
            if not optisch:
                x += adv
            else:
                leer += min(max(adv, 0.25 * F.cap), 0.42 * F.cap)   # Wortabstand
            continue
        if not optisch:
            if vorher_ch is not None and unterschneidung:
                x += F.kern.get(vorher_ch + ch, 0.0)
            platziert.append((striche, x))
            x += adv
            vorher_ch = ch
            continue
        P = _Profil(striche, F.cap, r)
        if P.leer:
            continue
        if vorher is None:
            ox = -P.minL
        elif leer:
            ox = vorher[1] + vorher[0].maxR + leer + T - P.minL
        else:
            ox = vorher[1] + _paar_abstand(vorher[0], P, T, tiefe, min_luecke)
        leer = 0.0
        platziert.append((striche, ox))
        vorher = (P, ox)
    return platziert


def text_linien(text: str, schrift: str = "Hershey Sans", versalhoehe: float = 20.0,
                zeilenabstand: float = 1.7, optisch: bool = True, luft: float = 0.22,
                schnittbreite: float = 1.0, linien: int = 1, linienabstand: float = 0.8,
                ausrichtung: str = "mitte", unterschneidung: bool = True) -> List[Linie]:
    """Text als Liste von Polylinien in mm (y nach oben).

    luft: Abstand zwischen Buchstaben als Anteil der Versalhöhe (optisch).
    schnittbreite: Breite der gefrästen Rille in mm (für den optischen Abstand).
    linien: 1, 2 oder 3 parallele Linien je Strich (Abstand linienabstand).
    ausrichtung: "links", "mitte" oder "rechts" je Zeile.
    """
    F = _Schrift(schrift)
    k = versalhoehe / F.cap
    if linien == 2:
        offs = [-linienabstand / 2, linienabstand / 2]
        spann = linienabstand
    elif linien == 3:
        offs = [-linienabstand, 0.0, linienabstand]
        spann = 2 * linienabstand
    else:
        offs, spann = [0.0], 0.0
    r_einh = ((schnittbreite + spann) / 2) / k
    T = luft * F.cap
    ergebnis: List[Linie] = []
    zeilen = text.split("\n")
    breiten = []
    zeilen_linien = []
    for li, zeile in enumerate(zeilen):
        platziert = _zeile(zeile, F, optisch, T, r_einh if optisch else 0.0, unterschneidung)
        by = -li * zeilenabstand * versalhoehe
        polys = []
        for striche, x in platziert:
            for s in striche:
                p = [((s[i] + x) * k, s[i + 1] * k + by) for i in range(0, len(s), 2)]
                for d in offs:
                    polys.append(p if d == 0 else versatz(p, d))
        xs = [q[0] for p in polys for q in p]
        breiten.append((min(xs), max(xs)) if xs else (0.0, 0.0))
        zeilen_linien.append(polys)
    gesamt = max((b[1] - b[0] for b in breiten), default=0.0)
    for (x0, x1), polys in zip(breiten, zeilen_linien):
        if ausrichtung == "links":
            dx = -x0
        elif ausrichtung == "rechts":
            dx = gesamt - x1
        else:
            dx = (gesamt - (x1 - x0)) / 2 - x0
        for p in polys:
            ergebnis.append([(x + dx, y) for x, y in p])
    return ergebnis


def lizenz(schrift: str) -> str:
    return _Schrift(schrift).lizenz
