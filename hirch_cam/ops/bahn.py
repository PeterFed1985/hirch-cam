# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Bausteine für Werkzeugwege: sicheres Anfahren, Eintauchen, Fräsen.

Alle Arten (Kontur, Tasche, Bohren …) bauen ihre Bewegungen hiermit,
damit die Regeln überall gleich sind (wie üblich):
- waagrechte Eilgänge nur auf der Freifahrhöhe
- vom Startabstand (z. B. 0.5 mm) aus mit Eintauchvorschub nach unten
- senkrechte Vorschubbewegungen mit Eintauchvorschub, alle anderen mit
  dem Bearbeitungsvorschub
"""
from __future__ import annotations

import math
from typing import List, Optional, Sequence, Tuple

from ..model import Move, Toolpath

EPS = 1e-9
Punkt = Tuple[float, float]


class Bahnbauer:
    def __init__(self, vorschub, eintauchen, freifahrhoehe=3.0, startabstand=0.5,
                 start=(0.0, 0.0, 20.0)):
        self.vorschub = float(vorschub)
        self.eintauchvorschub = float(eintauchen)
        self.cz = float(freifahrhoehe)
        self.sz = float(startabstand)
        self.pos = tuple(map(float, start))
        self.moves: List[Move] = []
        self.senkrecht = 0          # senkrechte Eintauchbewegungen ins Material

    # -- Grundbewegungen ---------------------------------------------------
    def _add(self, rapid, x, y, z, faktor=1.0):
        if (abs(x - self.pos[0]) <= EPS and abs(y - self.pos[1]) <= EPS
                and abs(z - self.pos[2]) <= EPS):
            return
        if rapid:
            feed = None
        elif math.hypot(x - self.pos[0], y - self.pos[1]) <= EPS:
            feed = self.eintauchvorschub
        else:
            feed = round(self.vorschub * faktor, 1)
        self.moves.append(Move(rapid, x, y, z, feed))
        self.pos = (x, y, z)

    def eilgang_z(self, z):
        self._add(True, self.pos[0], self.pos[1], z)

    def abheben(self):
        if self.pos[2] < self.cz - EPS:
            self.eilgang_z(self.cz)

    def anfahren(self, x, y):
        """Über den Punkt fahren und auf den Startabstand absenken."""
        if math.hypot(x - self.pos[0], y - self.pos[1]) > EPS:
            self.abheben()
            # von der Sicherheitshöhe aus schräg auf Freifahrhöhe, wie üblich
            self._add(True, x, y, self.cz)
        if self.pos[2] > self.sz + EPS:
            self.eilgang_z(self.sz)

    def fraesen(self, x, y, z=None, faktor=1.0):
        """Vorschub; faktor < 1 z. B. für Vollnut (Fräser schneidet ganz)."""
        self._add(False, x, y, self.pos[2] if z is None else z, faktor)

    def bis_frei(self, z_frei: float):
        """Durch bereits Ausgeräumtes (bis z_frei ist kein Material) im Eilgang bis
        knapp darüber (Startabstand), statt langsam durch die Luft zu tauchen."""
        ziel = z_frei + self.sz
        if ziel < self.pos[2] - EPS:
            self.eilgang_z(ziel)

    def eintauchen(self, z, material_ab: Optional[float] = 0.0):
        """Senkrecht nach unten. material_ab: ab dieser Höhe steht Material
        (zählt für die Prüfung «senkrecht eingetaucht»; darüber ist frei – dorthin
        im Eilgang); None = nicht zählen, ganz im Vorschub."""
        if material_ab is not None and z < self.pos[2] - EPS:
            self.bis_frei(max(material_ab, z))
        if material_ab is not None and z < material_ab - 0.05 and z < self.pos[2] - 0.05:
            self.senkrecht += 1
        self._add(False, self.pos[0], self.pos[1], z)

    def rampe(self, z_ziel, weg: Sequence[Punkt], winkel: float, faktor=1.0,
              z_von: Optional[float] = None, max_laenge: Optional[float] = None) -> bool:
        """Schräg eintauchen statt senkrecht: auf dem Anfang von «weg» hin und
        zurück, bis z_ziel erreicht ist; endet wieder am Startpunkt (= aktuelle
        Position). Bis z_von (bereits frei, z. B. vorherige Ebene) senkrecht.
        Liefert False, wenn der Weg zu kurz ist (dann senkrecht eingetaucht)."""
        x0, y0 = self.pos[0], self.pos[1]
        z_frei = min(self.pos[2], 0.0 if z_von is None else z_von)
        if z_frei < self.pos[2] - EPS:
            self.bis_frei(z_frei)                 # durch Luft / Freigeräumtes im Eilgang
            self.eintauchen(z_frei, None)         # die letzten Zehntel im Vorschub
        dz = self.pos[2] - z_ziel
        if dz <= EPS:
            return True
        tan_w = math.tan(math.radians(max(0.5, min(45.0, winkel))))
        noetig = dz / tan_w
        # Stück am Anfang des Wegs (ohne den Startpunkt doppelt)
        pts = [(x0, y0)]
        for q in weg:
            if math.dist(pts[-1], q) > EPS:
                pts.append(tuple(q))
        laenge, stueck = 0.0, [pts[0]]
        grenze = min(noetig / 2, max_laenge or noetig / 2)
        for a, b in zip(pts, pts[1:]):
            seg = math.dist(a, b)
            if laenge + seg >= grenze:
                t = (grenze - laenge) / seg if seg > EPS else 0
                stueck.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
                laenge = grenze
                break
            stueck.append(b)
            laenge += seg
        if laenge < 0.2:                          # kein Platz für eine Rampe
            self.eintauchen(z_ziel, z_frei)
            return False
        # so oft hin und zurück, bis die Neigung nicht steiler als «winkel» ist
        durchgaenge = max(1, math.ceil(noetig / (2 * laenge) - 1e-9))
        gesamt = 2 * laenge * durchgaenge
        z, gefahren = self.pos[2], 0.0
        hin = stueck
        zurueck = list(reversed(stueck))
        for _ in range(durchgaenge):
            for folge in (hin, zurueck):
                for a, b in zip(folge, folge[1:]):
                    gefahren += math.dist(a, b)
                    self._add(False, b[0], b[1], z - dz * gefahren / gesamt, faktor)
        return True

    def weg(self, name=""):
        self.abheben()
        return Toolpath(name=name, moves=list(self.moves), senkrecht=self.senkrecht)


class Zustellung(float):
    """Zustellung mit Art (Peter, 20.10.): der Wert ist die grösste Zustellung (für alles, was nur
    eine Zahl braucht), dazu wie die Tiefe aufgeteilt wird:
    ""        gleichmässig (Standard, wie üblich)
    "kleiner" gleichmässig, die letzte Zustellung kleiner («letzte» mm) – sauberer Boden
    "liste"   eigene Liste («liste» in mm, von oben nach unten; die letzte wiederholt sich)
    "eins"    in einem Durchgang auf die volle Tiefe"""

    def __new__(cls, wert, modus: str = "", letzte: float = 0.0, liste=None):
        obj = float.__new__(cls, wert)
        obj.modus = modus or ""
        obj.letzte = float(letzte or 0.0)
        obj.liste = [float(v) for v in (liste or []) if float(v) > 0]
        return obj

    def __reduce__(self):
        return (Zustellung, (float(self), self.modus, self.letzte, self.liste))


def zustellung_aus(p: dict, schluessel: str = "zustellung", tiefe: float = 0.0):
    """Zustellung einer Bearbeitung: Zahl oder Zustellung mit Art (zustell_modus)."""
    z = p.get(schluessel)
    modus = str(p.get("zustell_modus") or "")
    try:
        wert = float(z or 0.0)
    except (TypeError, ValueError):
        wert = 0.0
    if modus not in ("kleiner", "liste", "eins"):
        return z
    liste = []
    for v in p.get("zustell_liste") or []:
        try:
            if float(v) > 0:
                liste.append(float(v))
        except (TypeError, ValueError):
            pass
    if modus == "liste" and not liste:
        return z
    if modus == "liste":
        wert = max(liste)
    elif modus == "eins":
        wert = float(tiefe or wert or 0.0)
    return Zustellung(wert, modus, float(p.get("letzte_zustellung") or 0.0), liste)


def zustellungen(tiefe: float, zustellung: float) -> List[float]:
    """Schritte bis zur Endtiefe (negativ): gleich gross (wie üblich), oder nach der Art einer
    Zustellung (letzte kleiner, eigene Liste, ein Durchgang)."""
    if tiefe <= 0:
        raise ValueError("Tiefe muss grösser als 0 sein")
    modus = getattr(zustellung, "modus", "")
    if modus == "eins":
        return [-tiefe]
    if modus == "liste" and zustellung.liste:
        aus, summe, i = [], 0.0, 0
        while summe < tiefe - 1e-9 and i < 100000:
            summe = min(tiefe, summe + zustellung.liste[min(i, len(zustellung.liste) - 1)])
            aus.append(-summe)
            i += 1
        return aus
    if modus == "kleiner" and float(zustellung) > 0 and 0 < zustellung.letzte < tiefe - 1e-9:
        rest = tiefe - zustellung.letzte
        n = max(1, math.ceil(rest / float(zustellung) - 1e-9))
        return [-rest * k / n for k in range(1, n + 1)] + [-tiefe]
    if zustellung <= 0:
        zustellung = tiefe
    n = max(1, math.ceil(tiefe / zustellung - 1e-9))
    return [-tiefe * k / n for k in range(1, n + 1)]


def flaeche(ring: Sequence[Punkt]) -> float:
    """Vorzeichenbehaftete Fläche (positiv = gegen den Uhrzeigersinn)."""
    a = 0.0
    n = len(ring)
    for i in range(n):
        x1, y1 = ring[i]
        x2, y2 = ring[(i + 1) % n]
        a += x1 * y2 - x2 * y1
    return a / 2


def punkt_in_ring(p: Punkt, ring: Sequence[Punkt]) -> bool:
    x, y = p
    inside = False
    n = len(ring)
    for i in range(n):
        x1, y1 = ring[i]
        x2, y2 = ring[(i + 1) % n]
        if (y1 > y) != (y2 > y):
            xs = x1 + (y - y1) * (x2 - x1) / (y2 - y1)
            if xs > x:
                inside = not inside
    return inside


def offener_ring(punkte: Sequence[Punkt]) -> List[Punkt]:
    """Ring ohne doppelten Schlusspunkt."""
    pts = [tuple(map(float, p)) for p in punkte]
    if len(pts) > 1 and math.dist(pts[0], pts[-1]) <= 1e-6:
        pts = pts[:-1]
    return pts


def verschachtelung(ringe: Sequence[Sequence[Punkt]]) -> List[int]:
    """Tiefe jedes Rings: 0 = liegt in keinem anderen, 1 = in einem, …
    Gerade Tiefe = Aussenkante eines Teils, ungerade = Loch."""
    flaechen = [abs(flaeche(r)) for r in ringe]
    tiefe = []
    for i, r in enumerate(ringe):
        # Mittelpunkt einer Kante als Prüfpunkt (Ecken liegen oft auf anderen Ringen)
        p = ((r[0][0] + r[1][0]) / 2, (r[0][1] + r[1][1]) / 2) if len(r) > 1 else r[0]
        d = 0
        for j, s in enumerate(ringe):
            if j != i and flaechen[j] > flaechen[i] and punkt_in_ring(p, s):
                d += 1
        tiefe.append(d)
    return tiefe


def ring_ab(ring: Sequence[Punkt], start: Punkt) -> List[Punkt]:
    """Ring so drehen, dass er am Punkt beginnt, der `start` am nächsten liegt."""
    ring = list(ring)
    j = min(range(len(ring)), key=lambda k: math.dist(ring[k], start))
    return ring[j:] + ring[:j]


def knochen(ring: Sequence[Punkt], radius: float, abfall_innen: bool,
            min_drehung: float = 25.0) -> List[Punkt]:
    """Dogbone (wie FreeCAD DogboneII): an scharfen Ecken der Fräsbahn, in
    denen der runde Fräser die Ecke des Teils nicht erreicht, fährt er auf der
    Winkelhalbierenden hinein, bis er die Ecke berührt, und wieder zurück.
    abfall_innen: liegt der Abfall (die freigefräste Seite) innerhalb des
    Rings (Tasche, Loch) oder ausserhalb (Teil aussen herum)?"""
    pts = offener_ring(ring)
    n = len(pts)
    if n < 3 or radius <= 0:
        return list(pts)
    ccw = flaeche(pts) > 0
    links = ccw == abfall_innen           # Abfall links der Fahrtrichtung?
    aus: List[Punkt] = []
    for i in range(n):
        a, v, c = pts[i - 1], pts[i], pts[(i + 1) % n]
        tin = (v[0] - a[0], v[1] - a[1])
        tout = (c[0] - v[0], c[1] - v[1])
        li, lo = math.hypot(*tin), math.hypot(*tout)
        aus.append(v)
        if li < EPS or lo < EPS:
            continue
        tin = (tin[0] / li, tin[1] / li)
        tout = (tout[0] / lo, tout[1] / lo)
        drehung = math.degrees(math.atan2(tin[0] * tout[1] - tin[1] * tout[0],
                                          tin[0] * tout[0] + tin[1] * tout[1]))
        if (drehung if links else -drehung) < min_drehung:
            continue
        phi = math.radians(180.0 - abs(drehung))         # Öffnungswinkel der Ecke
        weg = radius / math.sin(phi / 2) - radius
        if weg < 1e-3:
            continue
        rx, ry = tin[0] - tout[0], tin[1] - tout[1]
        lr = math.hypot(rx, ry)
        if lr < EPS:
            continue
        aus.append((v[0] + rx / lr * weg, v[1] + ry / lr * weg))
        aus.append(v)
    return aus


def ring_ab_punkt(ring: Sequence[Punkt], p: Punkt) -> List[Punkt]:
    """Ring so drehen, dass er genau bei p beginnt; liegt p auf einer Kante (nicht auf einer Ecke), wird es dort
    eingefügt. p muss auf dem Ring liegen (sonst: nächste Ecke wie ring_ab)."""
    ring = list(ring)
    n = len(ring)
    if n < 2:
        return ring
    best, bk, bt = None, 0, 0.0
    for k in range(n):
        a, c = ring[k], ring[(k + 1) % n]
        vx, vy = c[0] - a[0], c[1] - a[1]
        ll = vx * vx + vy * vy
        t = 0.0 if ll < 1e-18 else max(0.0, min(1.0, ((p[0] - a[0]) * vx + (p[1] - a[1]) * vy) / ll))
        d = math.hypot(a[0] + vx * t - p[0], a[1] + vy * t - p[1])
        if best is None or d < best - 1e-12:
            best, bk, bt = d, k, t
    if best > 1e-6:
        return ring_ab(ring, p)
    a, c = ring[bk], ring[(bk + 1) % n]
    if math.dist(p, a) <= 1e-6:
        return ring[bk:] + ring[:bk]
    if math.dist(p, c) <= 1e-6:
        j = (bk + 1) % n
        return ring[j:] + ring[:j]
    q = (float(p[0]), float(p[1]))
    return [q] + ring[bk + 1:] + ring[:bk + 1]


def stellen_auf_ring(ring: Sequence[Punkt], abstand: float = 2.0, max_n: int = 160,
                     nur_gerade: float = 0.0, rand: float = 0.0) -> List[Punkt]:
    """Mögliche Eintauchstellen auf einem geschlossenen Ring: die Ecken und Punkte auf längeren Kanten (etwa alle
    `abstand` mm). nur_gerade > 0 (Anfahrbogen): nur auf Kanten, die mindestens so lang sind, und dort mindestens
    `rand` von den Ecken weg (der Bogen braucht eine Gerade) – fehlt so eine Kante, die Mitte der längsten."""
    ring = list(ring)
    n = len(ring)
    if n == 0:
        return []
    if n == 1:
        return [ring[0]]
    kanten = [(ring[k], ring[(k + 1) % n]) for k in range(n)]
    laengen = [math.dist(a, c) for a, c in kanten]
    aus: List[Punkt] = []
    if nur_gerade > 0:
        lmax = max(laengen)
        schwelle = min(nur_gerade, 0.9 * lmax)
        for (a, c), ll in zip(kanten, laengen):
            if ll < schwelle - 1e-9:
                continue
            r0 = min(rand, ll / 2)
            m = max(1, int((ll - 2 * r0) // max(abstand, 1e-3)))
            for i in range(m + 1):
                t = 0.5 if m == 0 or ll - 2 * r0 < 1e-9 else (r0 + (ll - 2 * r0) * i / m) / ll
                aus.append((a[0] + (c[0] - a[0]) * t, a[1] + (c[1] - a[1]) * t))
            aus.append(((a[0] + c[0]) / 2, (a[1] + c[1]) / 2))
        if not aus:
            k = max(range(n), key=lambda i: laengen[i])
            a, c = kanten[k]
            aus = [((a[0] + c[0]) / 2, (a[1] + c[1]) / 2)]
    else:
        for (a, c), ll in zip(kanten, laengen):
            aus.append(tuple(a))
            m = int(ll // max(abstand, 1e-3))
            for i in range(1, m):
                t = i / m
                aus.append((a[0] + (c[0] - a[0]) * t, a[1] + (c[1] - a[1]) * t))
    if len(aus) > max_n:
        schritt = len(aus) / max_n
        aus = [aus[int(i * schritt)] for i in range(max_n)]
    return aus


def eintauch_wahl(kandidaten: Sequence[Sequence[Tuple[Punkt, Punkt]]], start: Punkt) -> List[int]:
    """Für eine feste Reihenfolge von Stücken je Stück die Eintauchstelle so wählen, dass der ganze Leerweg am
    kürzesten wird (Peter 27.10.: nicht beim einen Teil links, beim nächsten rechts eintauchen, obwohl es näher
    ginge). kandidaten[i]: Liste (Eintauchpunkt, Endpunkt) – bei geschlossenen Ringen gleich, bei offenen Linien
    das andere Ende. Kürzester Weg durch die Stufen (dynamische Programmierung, genau). Gibt je Stück den Index
    in seiner Liste zurück."""
    import numpy as np
    if not kandidaten:
        return []
    ein0 = np.asarray([c[0] for c in kandidaten[0]], float)
    kosten = np.hypot(ein0[:, 0] - start[0], ein0[:, 1] - start[1])
    zurueck = []
    aus_vor = np.asarray([c[1] for c in kandidaten[0]], float)
    for i in range(1, len(kandidaten)):
        ein = np.asarray([c[0] for c in kandidaten[i]], float)
        d = np.hypot(aus_vor[:, None, 0] - ein[None, :, 0], aus_vor[:, None, 1] - ein[None, :, 1])
        ges = kosten[:, None] + d
        k = np.argmin(ges, axis=0)
        zurueck.append(k)
        kosten = ges[k, np.arange(len(ein))]
        aus_vor = np.asarray([c[1] for c in kandidaten[i]], float)
    wahl = [int(np.argmin(kosten))]
    for k in reversed(zurueck):
        wahl.append(int(k[wahl[-1]]))
    return list(reversed(wahl))


def kurze_reihenfolge(n: int, abstand, vom_start, max_n: int = 200) -> List[int]:
    """Reihenfolge von n Stücken (Löcher, Konturen, Flächen) mit möglichst kurzem
    Leerweg: nächster Nachbar, danach 2-opt (Teilstücke umdrehen, solange es kürzer
    wird). abstand(i, j): Leerweg zwischen zwei Stücken; vom_start(i): vom aktuellen
    Ort zum Stück. Ab max_n Stücken nur nächster Nachbar (Rechenzeit)."""
    if n <= 1:
        return list(range(n))
    s0 = [vom_start(i) for i in range(n)]
    if n > max_n:
        rest, folge, ort = set(range(n)), [], None
        while rest:
            k = min(rest, key=lambda i: s0[i] if ort is None else abstand(ort, i))
            rest.discard(k)
            folge.append(k)
            ort = k
        return folge
    d = [[0.0] * n for _ in range(n)]
    for i in range(n):
        for j in range(i + 1, n):
            d[i][j] = d[j][i] = abstand(i, j)
    # nächster Nachbar
    rest = set(range(n))
    k = min(rest, key=lambda i: s0[i])
    folge = [k]
    rest.discard(k)
    while rest:
        k = min(rest, key=lambda i: d[folge[-1]][i])
        folge.append(k)
        rest.discard(k)

    def laenge(f):
        return s0[f[0]] + sum(d[a][b] for a, b in zip(f, f[1:]))
    # 2-opt für einen offenen Weg mit festem Anfang (vom Start): Abschnitt i..j umdrehen
    # Begrenzt durch die Zahl der Vergleiche (nicht durch die Zeit): so kommt bei
    # gleicher Zeichnung immer derselbe G-Code heraus
    budget = 4_000_000
    besser = True
    while besser and budget > 0:
        besser = False
        for i in range(0, n - 1):
            vor = s0 if i == 0 else d[folge[i - 1]]
            budget -= n - i - 1
            for j in range(i + 1, n):
                a, b = folge[i], folge[j]
                alt = vor[a] + (d[b][folge[j + 1]] if j + 1 < n else 0.0)
                neu = vor[b] + (d[a][folge[j + 1]] if j + 1 < n else 0.0)
                if neu < alt - 1e-9:
                    folge[i:j + 1] = reversed(folge[i:j + 1])
                    besser = True
                    vor = s0 if i == 0 else d[folge[i - 1]]
    return folge


def kurze_reihenfolge_matrix(D, s0, max_runden: int = 60) -> List[int]:
    """Wie kurze_reihenfolge, für viele Stücke (Gravur eines ganzen Textes, Beispiel 10.11.: über 900 Linien):
    D Matrix der Leerwege (n × n, symmetrisch), s0 Leerweg vom Start. Nächster Nachbar, danach 2-opt mit numpy
    (je Anfang alle Enden auf einmal). Gleiche Eingabe → gleiche Reihenfolge."""
    import numpy as np
    D = np.asarray(D, float)
    s0 = np.asarray(s0, float)
    n = len(s0)
    if n <= 1:
        return list(range(n))
    frei = np.ones(n, bool)
    k = int(np.argmin(s0))
    folge = [k]
    frei[k] = False
    for _ in range(n - 1):
        d = np.where(frei, D[folge[-1]], np.inf)
        k = int(np.argmin(d))
        folge.append(k)
        frei[k] = False
    t = np.asarray(folge)
    for _ in range(max_runden):
        besser = False
        for i in range(n - 1):
            vor = s0[t[i]] if i == 0 else D[t[i - 1], t[i]]
            js = np.arange(i + 1, n)
            a, b = t[i], t[js]
            nach = np.append(t[js[:-1] + 1], -1)
            alt = vor + np.where(nach >= 0, D[b, np.maximum(nach, 0)], 0.0)
            neu_vor = s0[b] if i == 0 else D[t[i - 1], b]
            neu = neu_vor + np.where(nach >= 0, D[a, np.maximum(nach, 0)], 0.0)
            gewinn = alt - neu
            j = int(np.argmax(gewinn))
            if gewinn[j] > 1e-9:
                jj = js[j]
                t[i:jj + 1] = t[i:jj + 1][::-1]
                besser = True
        if not besser:
            break
    return [int(x) for x in t]


def vereinfachen(tp: Toolpath, toleranz: float, start=(0.0, 0.0, 20.0)) -> int:
    """Kurze Geraden zusammenfassen (Douglas-Peucker), solange der Weg höchstens «toleranz» mm
    abweicht – seitlich und in der Höhe getrennt gemessen. Nur innerhalb einer Folge von
    Vorschubbewegungen mit gleichem Vorschub; Eilgänge, Vorschubwechsel und Knicke bleiben.
    Ohne das gäbe Hirch CAM jeden Punkt der Zeichnung aus (Peter, Teller mit vielen Kurven 22.10.2026: 581 KB
    gegen 165 KB bei einem anderen CAM-Programm, Segmente im Median 0,017 mm) – die Steuerung bremst bei vielen
    Kleinstschritten. Werkzeugwege mit Bohrzyklen bleiben, wie sie sind. Gibt die Zahl der
    weggelassenen Bewegungen zurück."""
    import numpy as np
    if toleranz <= 0 or tp.zyklen or len(tp.moves) < 3:
        return 0
    neu: List[Move] = []
    lauf: List[Move] = []
    vor = tuple(map(float, start))     # Lage vor dem Lauf

    def abschliessen():
        nonlocal lauf
        if len(lauf) < 2:
            neu.extend(lauf)
        else:
            if len(lauf) < 48:
                # kurze Läufe (Rampen, Eintauchen): reines Python ist hier schneller als numpy
                pl = [vor] + [(m.x, m.y, m.z) for m in lauf]
                behalten = _behalten_klein(pl, toleranz)
                neu.extend(m for m, b in zip(lauf, behalten[1:]) if b)
                lauf = []
                return
            pts = np.array([vor] + [(m.x, m.y, m.z) for m in lauf], float)
            if float(np.ptp(pts[:, 2])) < 1e-9:
                # gleiche Höhe (der Normalfall): Douglas-Peucker von GEOS, viel schneller als hier in Python
                behalten = _behalten_2d(pts[:, :2], toleranz)
                if behalten is not None:
                    neu.extend(m for m, b in zip(lauf, behalten[1:]) if b)
                    lauf = []
                    return
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
                t = np.clip(Q[:, :2] @ d2 / L, 0, 1) if L > EPS else np.zeros(len(Q))
                abst = np.maximum(np.linalg.norm(Q[:, :2] - np.outer(t, d2), axis=1),
                                  np.abs(Q[:, 2] - t * d[2]))
                k = int(abst.argmax())
                if abst[k] > toleranz:
                    behalten[a + 1 + k] = True
                    stapel += [(a, a + 1 + k), (a + 1 + k, e)]
            neu.extend(m for m, b in zip(lauf, behalten[1:]) if b)
        lauf = []

    for m in tp.moves:
        if m.rapid or (lauf and m.feed != lauf[-1].feed):
            if lauf:
                abschliessen()
        if m.rapid:
            neu.append(m)
        else:
            if not lauf:
                vor = (neu[-1].x, neu[-1].y, neu[-1].z) if neu else vor
            lauf.append(m)
    if lauf:
        abschliessen()
    weg = len(tp.moves) - len(neu)
    tp.moves = neu
    return weg


def _behalten_2d(xy, toleranz: float):
    """Welche Punkte einer ebenen Linie Douglas-Peucker (GEOS, shapely) behält – als Maske; None, wenn
    shapely fehlt oder die Punkte sich nicht eindeutig zuordnen lassen."""
    import numpy as np
    try:
        from shapely.geometry import LineString
    except ImportError:
        return None
    if len(xy) < 3:
        return np.ones(len(xy), bool)
    try:
        einfach = np.asarray(LineString(xy).simplify(toleranz, preserve_topology=False).coords)
    except Exception:
        return None
    behalten = np.zeros(len(xy), bool)
    j = 0
    for i in range(len(xy)):
        if j < len(einfach) and abs(xy[i, 0] - einfach[j, 0]) < 1e-12 and abs(xy[i, 1] - einfach[j, 1]) < 1e-12:
            behalten[i] = True
            j += 1
    if j != len(einfach) or not behalten[0] or not behalten[-1]:
        return None
    return behalten


def _behalten_klein(pts, toleranz: float):
    """Douglas-Peucker für wenige Punkte (x, y, z): seitlich und in der Höhe getrennt gemessen."""
    n = len(pts)
    behalten = [False] * n
    behalten[0] = behalten[-1] = True
    stapel = [(0, n - 1)]
    while stapel:
        a, e = stapel.pop()
        if e <= a + 1:
            continue
        ax, ay, az = pts[a]
        dx, dy, dz = pts[e][0] - ax, pts[e][1] - ay, pts[e][2] - az
        L = dx * dx + dy * dy
        best, bi = -1.0, -1
        for i in range(a + 1, e):
            qx, qy, qz = pts[i][0] - ax, pts[i][1] - ay, pts[i][2] - az
            t = min(1.0, max(0.0, (qx * dx + qy * dy) / L)) if L > EPS else 0.0
            d = max(math.hypot(qx - t * dx, qy - t * dy), abs(qz - t * dz))
            if d > best:
                best, bi = d, i
        if best > toleranz:
            behalten[bi] = True
            stapel += [(a, bi), (bi, e)]
    return behalten

