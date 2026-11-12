# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Tief freifahren (Peter, 25.10.2026: «so tief wie möglich – beim V-Carve langt 0,5 mm»).

Die Fräsarten heben zwischen zwei Zügen auf die Freifahrhöhe (z. B. 3 mm), fahren hinüber und senken
wieder auf den Startabstand. Liegt auf dem Weg nichts über der Werkstückoberfläche (Z 0), genügt es, knapp
darüber zu fahren: höchstens 0,5 mm (oder der Startabstand, wenn er grösser ist). Auf der Freifahrhöhe
bleiben Wege, die über ein Hindernis führen: Passstifte, Schraubenkopf (rundes Material), Spannmittel
und Wände der Vorrichtung derselben Seite (je um Fräserradius + 1 mm vergrössert).

Nur für 2D-Bahnen: dort liegt nie Material über Z 0. Bahnen mit Bohrzyklen bleiben unverändert (die
Zyklen zeigen auf Bewegungen der Bahn).
"""
from __future__ import annotations

import math
from typing import List, Sequence, Tuple

from ..model import Toolpath

HOEHE = 0.5          # mm über der Oberfläche, wenn der Weg frei ist
RAND = 1.0           # mm zusätzlich um Hindernisse
EPS = 1e-6


def hoehe(startabstand: float, freifahrhoehe: float) -> float:
    """Höhe für freie Wege: 0,5 mm, nie unter dem Startabstand, nie über der Freifahrhöhe."""
    return min(float(freifahrhoehe), max(HOEHE, float(startabstand)))


def hindernisse(auftrag: dict, seite: int, fraeser_d: float):
    """(Kreise [(x, y, r)], Flächen) – was über der Oberfläche steht, vergrössert um Fräserradius + RAND."""
    from .. import passstifte
    kreise = [(x, y, r + RAND) for x, y, r in
              passstifte.sperrkreise(auftrag, fraeser_d) + passstifte.schraube(auftrag, fraeser_d)]
    flaechen = []
    spann = [s for s in (auftrag.get("spannmittel") or [])
             if int(s.get("seite") or 1) == int(seite) and len(s.get("punkte") or []) >= 3]
    if spann:
        try:
            from shapely.geometry import Polygon
            for s in spann:
                g = Polygon(s["punkte"]).buffer(fraeser_d / 2 + RAND)
                if not g.is_empty:
                    flaechen.append(g)
        except Exception:            # ohne shapely: Spannmittel als Kreis um ihre Punkte
            for s in spann:
                xs = [p[0] for p in s["punkte"]]
                ys = [p[1] for p in s["punkte"]]
                cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
                r = max(math.hypot(p[0] - cx, p[1] - cy) for p in s["punkte"])
                kreise.append((cx, cy, r + fraeser_d / 2 + RAND))
    return kreise, flaechen


def _strecke_trifft_kreis(a, b, k) -> bool:
    x, y, r = k
    dx, dy = b[0] - a[0], b[1] - a[1]
    l2 = dx * dx + dy * dy
    t = 0.0 if l2 < 1e-18 else max(0.0, min(1.0, ((x - a[0]) * dx + (y - a[1]) * dy) / l2))
    return math.hypot(a[0] + t * dx - x, a[1] + t * dy - y) < r


def _weg_frei(punkte: Sequence[Tuple[float, float]], kreise, flaechen) -> bool:
    for a, b in zip(punkte, punkte[1:]):
        if any(_strecke_trifft_kreis(a, b, k) for k in kreise):
            return False
    if flaechen and len(punkte) >= 1:
        from shapely.geometry import LineString, Point
        geo = LineString(punkte) if len(punkte) >= 2 else Point(punkte[0])
        if any(f.intersects(geo) for f in flaechen):
            return False
    return True


def tief(tp: Toolpath, freifahrhoehe: float, h: float, kreise=(), flaechen=(),
         start=(0.0, 0.0, 20.0)) -> int:
    """Hübe auf der Freifahrhöhe auf h senken, wo der Weg frei ist. Liefert die Zahl der gesenkten Hübe.

    Ein Hub: Eilgang senkrecht hinauf auf die Freifahrhöhe, Eilgänge waagrecht auf dieser Höhe, dann
    hinunter (Eilgang oder Vorschub). Beginnt der Hub über h (z. B. vom Start auf Sicherheitshöhe), bleibt er."""
    cz = float(freifahrhoehe)
    if tp.zyklen or h >= cz - EPS or not tp.moves:
        return 0
    ms = tp.moves
    gesenkt = 0
    raus = set()
    pos = tuple(start)
    i = 0
    n = len(ms)
    while i < n:
        m = ms[i]
        vor = pos if i == 0 else (ms[i - 1].x, ms[i - 1].y, ms[i - 1].z)
        senkrecht_hoch = (m.rapid and abs(m.z - cz) <= EPS and vor[2] < cz - EPS
                          and abs(m.x - vor[0]) <= EPS and abs(m.y - vor[1]) <= EPS)
        if not senkrecht_hoch or vor[2] > h + EPS:
            i += 1
            continue
        j = i + 1
        while j < n and ms[j].rapid and abs(ms[j].z - cz) <= EPS:
            j += 1
        # j: erste Bewegung, die die Freifahrhöhe verlässt (muss nach unten gehen)
        if j >= n or ms[j].z > cz - EPS:
            i = j
            continue
        weg = [(vor[0], vor[1])] + [(ms[k].x, ms[k].y) for k in range(i + 1, j)]
        if _weg_frei(weg, kreise, flaechen):
            for k in range(i, j):
                ms[k].z = h
            gesenkt += 1
            if abs(vor[2] - h) <= EPS:          # stand schon auf h: kein Hinauf mehr
                raus.add(i)
            # wird das Absenken danach zur Nullbewegung (Eilgang auf h), fällt es weg
            if ms[j].rapid and abs(ms[j].z - h) <= EPS:
                raus.add(j)
        i = j
    if raus:
        tp.moves = [m for k, m in enumerate(ms) if k not in raus]
    return gesenkt


# ---------------------------------------------------------------------------------------------------------------
# Übergänge über dem, was wirklich noch steht (3D → Fräsbahnen 25.10. 20:06, Punkt 1): eine Stelle für 2D und das
# 3D-Schlichten. Nach dem Ordnen der Programme (Fräsreihenfolge, je Seite) wird simuliert, was die Bahnen davor
# und die eigenen Züge bis hierher weggenommen haben (hirch_relief.abtragen, Raster 0,25 mm). Ein Übergang
# (senkrecht hinauf – waagrecht hinüber – hinunter – Eintauchen) fährt dann nur so hoch wie das höchste Material
# auf dem Weg + HOEHE und im Eilgang bis knapp über das Material am Ziel. Was nicht simuliert wird (Gewinde,
# Bohrzyklen, Planfräsen), zählt als stehend – das Material liegt dann eher höher (sicher). Über Hindernissen
# (Passstifte, Schraube, Spannmittel) bleibt der Übergang, wie er ist.

ZELLE = 0.25


def _vorsichtig(form: dict, zelle: float) -> dict:
    """Form zum Abtragen im Raster: der Rand um eine halbe Zellendiagonale kleiner. Sonst gilt eine Zelle schon als
    ganz gefräst, sobald ihre Mitte im Fräser liegt – ein schmaler Rest neben der Bahn (Säule, die der Schruppfräser
    stehen liess) verschwand im Raster, und der Übergang darüber wurde zu tief gesenkt (3D-Zufall 07.11., Fall 38:
    Kugel Ø 1,5 streifte im Eilgang 1,4 mm tief eine Säule). V- und Kegelfräser: wie bisher."""
    typ = str(form.get("typ") or "schaft")
    if typ not in ("schaft", "kugel", "eckradius", "gewinde", "bohrer"):
        return form
    d = float(form.get("durchmesser") or 0)
    rand = min(zelle * 0.71, d / 4)
    if d <= 0 or rand <= 0:
        return form
    neu = dict(form, durchmesser=d - 2 * rand)
    if float(form.get("eckradius") or 0) > 0:
        neu["eckradius"] = min(float(form["eckradius"]), (d - 2 * rand) / 2)
    return neu


class Bestand:
    """Höhenraster des Materials einer Seite (0 = Oberfläche, unberührt)."""

    def __init__(self, x0, y0, x1, y1, boden: float, zelle: float = ZELLE, material=None):
        """material: (Breite, Höhe) des Rohteils – ausserhalb ist Luft (3D «Über den Materialrand hinaus»,
        3D → Fräsbahnen 26.10. 19:05, Punkt 4); ohne: überall Material bis Z 0."""
        import numpy as np
        while (x1 - x0) * (y1 - y0) / zelle ** 2 > 6_000_000:
            zelle *= 1.25
        self.x0, self.y0, self.z, self.boden = float(x0), float(y0), zelle, float(boden)
        self.nx = int(math.ceil((x1 - x0) / zelle)) + 1
        self.ny = int(math.ceil((y1 - y0) / zelle)) + 1
        self.H = np.zeros((self.ny, self.nx), np.float32)
        self.modell = None          # Oberfläche des 3D-Modells je Rasterpunkt (−inf: kein Modell), siehe hoch()
        self.material = None
        if material and float(material[0]) > 0 and float(material[1]) > 0:
            self.material = (float(material[0]), float(material[1]))
            xs = self.x0 + np.arange(self.nx) * zelle
            ys = self.y0 + np.arange(self.ny) * zelle
            aussen = ~self._im_material(xs[None, :], ys[:, None])
            self.H[aussen] = self.boden

    def _im_material(self, x, y):
        """Zellmitte im Rohteil? Eine Zelle Spiel: am Rand gilt sie als Material (sicher)."""
        B, Hm = self.material
        e = self.z
        return (x >= -e) & (x <= B + e) & (y >= -e) & (y <= Hm + e)

    def abtragen(self, moves, form: dict, start) -> None:
        import numpy as np
        from .. import hirch_relief
        if not moves:
            return
        b3 = np.array([(m.x, m.y, m.z) for m in moves], np.float64).reshape(-1, 3)
        hirch_relief.abtragen(self.H, self.x0, self.y0, self.z, b3, _vorsichtig(form, self.z),
                              [float(v) for v in start], self.boden)

    def hoch(self, x, y, r: float, ohne_modell: bool = False) -> float:
        """Höchstes Material unter einem Fräser vom Radius r (+ 1,5 Zellen) an (x, y); ausserhalb des Rasters 0.
        ohne_modell: Stellen, an denen nur noch das 3D-Modell selbst steht (self.modell, Höhe der Oberfläche je
        Rasterpunkt), zählen nicht – davor schützt die Abtastung der Bahn selbst (siehe ueber_bestand)."""
        import numpy as np
        R = r + 1.5 * self.z
        k = int(math.ceil(R / self.z))
        i0, j0 = int(round((x - self.x0) / self.z)), int(round((y - self.y0) / self.z))
        oy, ox = np.mgrid[-k:k + 1, -k:k + 1]
        s = (ox * self.z) ** 2 + (oy * self.z) ** 2 <= R * R
        i, j = (i0 + ox)[s], (j0 + oy)[s]
        drin = (i >= 0) & (i < self.nx) & (j >= 0) & (j < self.ny)
        if ohne_modell and self.modell is not None and drin.any():
            hh = self.H[j[drin], i[drin]]
            rest = hh > self.modell[j[drin], i[drin]] + MODELL_SPIEL
            h = float(hh[rest].max()) if rest.any() else -1e9
        else:
            h = float(self.H[j[drin], i[drin]].max()) if drin.any() else -1e9
        if not drin.all():
            # ausserhalb des Rasters: unberührtes Material bis Z 0 – ausser ausserhalb des Rohteils (Luft)
            if self.material is None or self._im_material(self.x0 + i[~drin] * self.z, self.y0 + j[~drin] * self.z).any():
                h = max(h, 0.0)
            else:
                h = max(h, self.boden)
        return h

    def hoch_kern(self, x, y, r: float) -> float:
        """Höchstes Material unter dem Fräser selbst (Radius r − eine Zelle, ohne Zuschlag) – für die Prüfung, ob
        ein Eilgang wirklich ins Material fährt (nicht nur daneben)."""
        import numpy as np
        R = max(0.5 * r, r - self.z)
        k = int(math.ceil(R / self.z))
        i0, j0 = int(round((x - self.x0) / self.z)), int(round((y - self.y0) / self.z))
        oy, ox = np.mgrid[-k:k + 1, -k:k + 1]
        s = (ox * self.z) ** 2 + (oy * self.z) ** 2 <= R * R
        i, j = (i0 + ox)[s], (j0 + oy)[s]
        drin = (i >= 0) & (i < self.nx) & (j >= 0) & (j < self.ny)
        return float(self.H[j[drin], i[drin]].max()) if drin.any() else -1e9

    def auf_weg_kern(self, punkte, r: float) -> float:
        """Höchstes Material unter dem Fräser selbst (hoch_kern) entlang des Wegs."""
        h = -1e9
        for a, b in zip(punkte, punkte[1:]):
            n = max(1, int(math.ceil(math.dist(a, b) / (0.5 * self.z))))
            h = max(h, max(self.hoch_kern(a[0] + (b[0] - a[0]) * t / n, a[1] + (b[1] - a[1]) * t / n, r)
                           for t in range(n + 1)))
        return h

    def auf_weg(self, punkte, r: float) -> float:
        h = -1e9
        for a, b in zip(punkte, punkte[1:]):
            n = max(1, int(math.ceil(math.dist(a, b) / (0.5 * self.z))))
            h = max(h, max(self.hoch(a[0] + (b[0] - a[0]) * t / n, a[1] + (b[1] - a[1]) * t / n, r)
                           for t in range(n + 1)))
        return h


MODELL_SPIEL = 0.05   # mm: so viel über dem Modell zählt am Rasterpunkt noch als «nur das Modell»
SICHER_TOL = 0.1      # mm: so tief darf ein Eilgang hinunter laut Simulation ins Material reichen, ehe er stoppt


def ueber_bestand(tp, bestand: Bestand, form: dict, freifahrhoehe: float, startabstand: float, start,
                  hub_max: float, kreise=(), flaechen=(), anpassen: bool = True, eintauchen=None,
                  modell_frei: bool = False) -> int:
    """Übergänge von tp über dem Bestand (und trägt danach ab, was tp fräst). hub_max: nur Übergänge, die höchstens
    so hoch liegen (2D: die tief freigefahrenen; Übergänge über Hindernissen bleiben). anpassen=False: nur
    abtragen. Liefert die Zahl der geänderten Übergänge."""
    from ..model import Move
    ms = tp.moves
    r = float(form.get("durchmesser") or 6) / 2
    if not anpassen or tp.zyklen or len(ms) < 3:
        bestand.abtragen(ms, form, start)
        return 0
    frei = max(HOEHE, float(startabstand))
    neu, geaendert = [], 0
    pos = tuple(map(float, start))
    seit, seit_start = [], pos              # eigene Bewegungen seit dem letzten Abtragen
    i, n = 0, len(ms)
    while i < n:
        m = ms[i]
        hub = (m.rapid and abs(m.x - pos[0]) <= EPS and abs(m.y - pos[1]) <= EPS and m.z > pos[2] + EPS
               and pos[2] < 0 and m.z <= hub_max + EPS)
        if hub:
            j = i + 1
            while j < n and ms[j].rapid and abs(ms[j].z - m.z) <= EPS:
                j += 1
            quer = ms[i + 1:j]
            # hinunter: ein oder mehrere Eilgänge senkrecht (3D-Schruppen: erst auf den Startabstand, dann bis
            # knapp über die Ebene) – zählt als ein Weg hinunter bis zum tiefsten
            k, runter = j, None
            while k < n and ms[k].rapid and quer and abs(ms[k].x - quer[-1].x) <= EPS \
                    and abs(ms[k].y - quer[-1].y) <= EPS and ms[k].z < (runter.z if runter is not None else m.z):
                runter = ms[k]
                k += 1
            if not quer:
                # hinauf und an derselben Stelle wieder hinunter (3D-Schruppen zwischen zwei Stücken): nur so weit
                # hinauf, wie es danach weitergeht – hinauf aus dem Schnitt ist immer frei
                k = i + 1
                while k < n and ms[k].rapid and abs(ms[k].x - m.x) <= EPS and abs(ms[k].y - m.y) <= EPS \
                        and ms[k].z < ms[k - 1].z:
                    k += 1
                if k > i + 1 and k < n and not ms[k].rapid and ms[k - 1].z >= pos[2] - EPS:
                    z_hin = ms[k - 1].z
                    bestand.abtragen(seit, form, seit_start)
                    seit, seit_start = [], pos
                    if z_hin > pos[2] + EPS:
                        neu.append(Move(True, m.x, m.y, z_hin))
                    pos = (m.x, m.y, max(pos[2], z_hin))
                    seit_start = pos
                    geaendert += 1
                    i = k
                    continue
            k2 = k
            ein = ms[k2] if k2 < n and not ms[k2].rapid else None
            if quer and ein is not None:
                weg = [(pos[0], pos[1])] + [(q.x, q.y) for q in quer]
                if _weg_frei(weg, kreise, flaechen):
                    bestand.abtragen(seit, form, seit_start)       # was bis hierher gefräst ist
                    seit, seit_start = [], pos
                    b = weg[-1]
                    ueber = bestand.auf_weg(weg, r) + frei
                    unten = runter.z if runter is not None else m.z
                    # 3D-Schlichten (modell_frei): senkrecht hinunter auf einen Bahnpunkt stösst die Kugel nie ans
                    # Modell (die Höhe des Punkts kommt aus der Abtastung) – nur, was darüber noch steht, zählt.
                    # Sonst hielt jede Bahn neben einer Schaufel auf deren Oberkante an und tauchte 25 mm im
                    # Eintauchvorschub hinunter (Impeller «kombiniert»: 99 ×, Main-Sitzung 27.10.).
                    senk_ = abs(ein.x - b[0]) <= EPS and abs(ein.y - b[1]) <= EPS
                    am_ziel = bestand.hoch(b[0], b[1], r, ohne_modell=modell_frei and senk_) + float(startabstand)
                    senkrecht = abs(ein.x - b[0]) <= EPS and abs(ein.y - b[1]) <= EPS
                    ziel = max(am_ziel, ein.z if senkrecht else unten)
                    if modell_frei and senk_:
                        ziel = max(ziel, ein.z + float(startabstand))     # nie im Eilgang bis auf die Fläche
                    # Fable 06.11.: steht auf dem Weg quer mehr Material als der geplante Übergang hoch liegt (Schlichten
                    # nach einer V-Tasche, Insel in der Tasche), wird er angehoben – vorher wurde nur gesenkt
                    hoeher = bestand.auf_weg_kern(weg, r) > m.z - SICHER_TOL
                    if ueber < m.z - 1e-6 or ziel < unten - 1e-6 or hoeher:
                        hz = max(ueber, pos[2]) if hoeher else min(m.z, max(ueber, pos[2]))
                        neu.append(Move(True, pos[0], pos[1], hz))
                        for q in quer:
                            neu.append(Move(True, q.x, q.y, hz))
                        runter_z = min(ziel, unten)          # nie höher als der ursprüngliche Weg hinunter
                        # Sicherheitsnetz auch hier (gefunden 29.10.): steht unter dem Fräser am Ziel noch Material
                        # über runter_z, im Eilgang nur bis darüber (+ frei), den Rest im Eintauchvorschub.
                        # 3D-Schlichten senkrecht auf den Bahnpunkt: wie am_ziel nur Material über dem Modell
                        oben_ = (bestand.hoch(b[0], b[1], r, ohne_modell=True) if modell_frei and senk_
                                 else bestand.hoch_kern(b[0], b[1], r))
                        halt_ = min(hz, oben_ + frei) if oben_ > runter_z + SICHER_TOL else runter_z
                        if halt_ < hz - 1e-9:
                            neu.append(Move(True, b[0], b[1], halt_))
                        if runter_z < halt_ - 1e-9:
                            f_ = eintauchen or ein.feed or next((q.feed for q in ms[k2:] if not q.rapid and q.feed),
                                                                None)
                            neu.append(Move(False, b[0], b[1], runter_z, f_))
                        geaendert += 1
                    else:
                        neu += list(ms[i:k2])
                    neu_pos = neu[-1]
                    pos = (neu_pos.x, neu_pos.y, neu_pos.z)
                    seit_start = pos
                    i = k2
                    continue
        if (m.rapid and m.z < pos[2] - EPS and m.z < 0 and abs(m.x - pos[0]) <= EPS and abs(m.y - pos[1]) <= EPS):
            # Sicherheitsnetz (Fräsbahnen 26.10., Prüfung der 3D-Bahnen): ein Eilgang senkrecht hinunter, der laut
            # Simulation ins stehende Material fährt, stoppt darüber; den Rest im Vorschub. Gefunden am 3D-Schruppen
            # neben Passlöchern: das Aussparen der Passlöcher nimmt einen Teil einer Ebene weg, die Rampe der nächsten
            # Ebene tauchte trotzdem im Eilgang bis knapp über sie (2,5 mm ins Material).
            bestand.abtragen(seit, form, seit_start)
            seit, seit_start = [], pos
            oben = bestand.hoch_kern(m.x, m.y, r)
            if oben > m.z + SICHER_TOL:
                halt = min(pos[2], oben + frei)
                if halt > m.z + EPS:
                    if halt < pos[2] - EPS:
                        neu.append(Move(True, m.x, m.y, halt))
                    f = eintauchen or next((q.feed for q in ms[i + 1:] if not q.rapid and q.feed), None)
                    neu.append(Move(False, m.x, m.y, m.z, f))
                    pos = (m.x, m.y, m.z)
                    seit_start = pos
                    geaendert += 1
                    i += 1
                    continue
        if (m.rapid and not modell_frei and min(m.z, pos[2]) < 0
                and (abs(m.x - pos[0]) > EPS or abs(m.y - pos[1]) > EPS)):
            # Sicherheitsnetz für jeden waagrechten Eilgang unter der Oberfläche (Fable 06.11.: Schlicht-Durchgang
            # quer durch Material, das das Schruppen stehen liess): steckt der Fräser auf dem Weg im Material,
            # hinauf über den Bestand, quer, hinunter (das letzte Stück im Eintauchvorschub)
            bestand.abtragen(seit, form, seit_start)
            seit, seit_start = [], pos
            weg = [(pos[0], pos[1]), (m.x, m.y)]
            if bestand.auf_weg_kern(weg, r) > min(m.z, pos[2]) + SICHER_TOL:
                hz = max(bestand.auf_weg(weg, r) + frei, pos[2], m.z)
                if hz > pos[2] + EPS:
                    neu.append(Move(True, pos[0], pos[1], hz))
                neu.append(Move(True, m.x, m.y, hz))
                oben = bestand.hoch_kern(m.x, m.y, r)
                halt = max(m.z, min(hz, oben + frei))
                if halt < hz - EPS:
                    neu.append(Move(True, m.x, m.y, halt))
                if m.z < halt - EPS:
                    f = eintauchen or next((q.feed for q in ms[i + 1:] if not q.rapid and q.feed), None)
                    neu.append(Move(False, m.x, m.y, m.z, f))
                pos = (m.x, m.y, m.z)
                seit_start = pos
                geaendert += 1
                i += 1
                continue
        neu.append(m)
        seit.append(m)
        pos = (m.x, m.y, m.z)
        i += 1
    tp.moves = neu
    bestand.abtragen(seit, form, seit_start)
    return geaendert
