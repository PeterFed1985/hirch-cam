# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Umspannen (Peter 27.10., Befund B-5; Basis-Konzept claude/konzept-umspannen.md): liegen eingezeichnete
Spannmittel über Fräsbahnen, gibt es je Seite zwei Programme. **Spannung 1** fräst alles ausser einer Sperrzone um
die Spannmittel (Fläche + Fräserradius + Abstand), dann wird umgespannt, **Spannung 2** fräst nur, was in den Zonen
übrig blieb.

Die Bearbeitungen selbst rechnen unverändert; hier werden nur ihre fertigen Bahnen an der Zone zerschnitten:
- ausserhalb → Spannung 1. Vor der Zone hebt der Fräser ab, dahinter taucht er ein wie am Anfang eines Zuges
  (senkrecht im Eintauchvorschub, ohne Mittenschnitt mit Rampe). Die Höhen regelt danach `freifahren.ueber_bestand`
  über dem, was wirklich steht.
- innerhalb → Spannung 2, je Stück 1 mm länger auf der schon gefrästen Bahn (keine Naht). Dort ist in Spannung 1
  schon geräumt: der Fräser taucht daneben in der Luft ab und fährt seitlich ein.
- Bohrungen, Gewinde, Helix-Bohren, Passlöcher: ganz – berührt ein Loch die Zone, kommt es ganz in Spannung 2.

Sicherheit: in Spannung 1 steht in der Zone (und bis einen Fräserradius daneben) mehr Material, als die Bearbeitung
beim Rechnen annahm. Darum geht dort jeder waagrechte Eilgang unter der Freifahrhöhe hinauf; senkrecht hinunter
fängt das Sicherheitsnetz von `ueber_bestand`. In Spannung 2 steht überall höchstens so viel wie angenommen (alles
ausserhalb ist schon weg)."""
from __future__ import annotations

import math
from typing import Dict, List, Optional, Sequence, Tuple

from ..model import Move, Toolpath

EPS = 1e-6
UEBERLAPPUNG = 1.0          # mm: Stücke der Spannung 2 so weit auf der schon gefrästen Bahn beginnen/enden
ABSTAND = 10.0              # mm: Vorgabe Abstand zu den Spannmitteln (Peter 27.10.: mindestens 10)
GANZ = ("bohren", "gewinde", "passloecher")


def einstellung(auftrag: Dict) -> Optional[Dict]:
    """{"abstand", "hoehe2"} wenn Umspannen eingeschaltet ist, sonst None."""
    u = auftrag.get("umspannen") or {}
    if not u.get("an"):
        return None
    try:
        abstand = max(0.0, float(u.get("abstand") if u.get("abstand") not in (None, "") else ABSTAND))
    except (TypeError, ValueError):
        abstand = ABSTAND
    h2 = u.get("hoehe2")
    try:
        h2 = float(h2) if h2 not in (None, "") else None
    except (TypeError, ValueError):
        h2 = None
    return {"abstand": abstand, "hoehe2": h2}


def spannung_von(s: Dict) -> int:
    try:
        return 2 if int(s.get("spannung") or 1) == 2 else 1
    except (TypeError, ValueError):
        return 1


def spannmittel(auftrag: Dict, seite: int, spannung: int) -> List[Dict]:
    return [s for s in (auftrag.get("spannmittel") or [])
            if int(s.get("seite") or 1) == int(seite) and spannung_von(s) == spannung
            and len(s.get("punkte") or []) >= 3]


def zone(spann: Sequence[Dict], radius: float, abstand: float):
    """Sperrzone: Fläche der Spannmittel + Fräserradius + Abstand (eine shapely-Fläche) oder None."""
    from shapely.geometry import Polygon
    from shapely.ops import unary_union
    teile = []
    for s in spann:
        p = Polygon(s["punkte"])
        if not p.is_valid:
            p = p.buffer(0)
        if not p.is_empty:
            teile.append(p.buffer(float(radius) + float(abstand), quad_segs=16))
    if not teile:
        return None
    z = unary_union(teile)
    return None if z.is_empty else z


# --- Zerschneiden ----------------------------------------------------------------------------------------------

def _elemente(moves: Sequence[Move], zone, start) -> List[list]:
    """Bewegungen als Stücke [drin, rapid, (x0, y0, z0), (x1, y1, z1), feed], an der Zone geteilt."""
    import numpy as np
    import shapely
    from shapely.geometry import LineString, Point
    from shapely.prepared import prep
    pz = prep(zone)
    rand = zone.boundary
    aus: List[list] = []
    pos = tuple(map(float, start))
    if not moves:
        return aus
    a = np.array([pos] + [(m.x, m.y, m.z) for m in moves], float)
    p0, p1 = a[:-1], a[1:]
    gleich = np.hypot(p1[:, 0] - p0[:, 0], p1[:, 1] - p0[:, 1]) <= EPS
    segs = shapely.linestrings(np.stack([p0[:, :2], p1[:, :2]], axis=1))
    punkte = shapely.points(p1[:, :2])
    trifft = shapely.intersects(zone, np.where(gleich, punkte, segs))
    ganz = np.zeros(len(moves), bool)
    if trifft.any():
        idx = np.flatnonzero(trifft)
        ganz[idx] = shapely.contains(zone, np.where(gleich[idx], punkte[idx], segs[idx]))
    for i, m in enumerate(moves):
        q0, q1 = tuple(p0[i]), tuple(p1[i])
        if not trifft[i]:
            aus.append([False, m.rapid, q0, q1, m.feed])
            continue
        if gleich[i] or ganz[i]:
            aus.append([bool(ganz[i] or pz.contains(Point(q1[:2]))), m.rapid, q0, q1, m.feed])
            continue
        seg = LineString([q0[:2], q1[:2]])
        lang = seg.length
        ts = {0.0, 1.0}
        schnitt = seg.intersection(rand)
        for g in getattr(schnitt, "geoms", [schnitt]):
            if g.is_empty:
                continue
            for c in getattr(g, "coords", []):
                ts.add(min(1.0, max(0.0, seg.project(Point(c)) / lang)))
        ts = sorted(ts)
        for t0, t1 in zip(ts, ts[1:]):
            if t1 - t0 < 1e-9:
                continue
            tm = 0.5 * (t0 + t1)
            mitte = Point(q0[0] + (q1[0] - q0[0]) * tm, q0[1] + (q1[1] - q0[1]) * tm)
            u = tuple(q0[k] + (q1[k] - q0[k]) * t0 for k in range(3))
            v = tuple(q0[k] + (q1[k] - q0[k]) * t1 for k in range(3))
            aus.append([bool(pz.contains(mitte)), m.rapid, u, v, m.feed])
    return aus


def _laenge(e) -> float:
    return math.dist(e[2], e[3])


def _stueck_von(e, t0: float, t1: float):
    u = tuple(e[2][k] + (e[3][k] - e[2][k]) * t0 for k in range(3))
    v = tuple(e[2][k] + (e[3][k] - e[2][k]) * t1 for k in range(3))
    return [e[0], e[1], u, v, e[4]]


def _zurueck(el, i: int, weg: float) -> List[list]:
    """Die Fräsbahn vor el[i] (nur Vorschub ausserhalb), höchstens weg mm, in Fahrtrichtung."""
    aus, rest, k = [], weg, i - 1
    while k >= 0 and rest > EPS:
        e = el[k]
        if e[0] or e[1]:
            break
        L = _laenge(e)
        if L <= rest + EPS:
            aus.insert(0, e)
            rest -= L
        else:
            aus.insert(0, _stueck_von(e, 1.0 - rest / L, 1.0))
            rest = 0.0
        k -= 1
    return aus


def _vor(el, i: int, weg: float) -> List[list]:
    """Die Fräsbahn ab el[i] (nur Vorschub ausserhalb), höchstens weg mm."""
    aus, rest, k = [], weg, i
    while k < len(el) and rest > EPS:
        e = el[k]
        if e[0] or e[1]:
            break
        L = _laenge(e)
        if L <= rest + EPS:
            aus.append(e)
            rest -= L
        else:
            aus.append(_stueck_von(e, 0.0, rest / L))
            rest = 0.0
        k += 1
    return aus


class _Ausgabe:
    """Bewegungen zusammensetzen; merkt die Lage."""

    def __init__(self, start, cz: float, eintauchen: float, rampe: float = 0.0):
        self.moves: List[Move] = []
        self.pos = tuple(map(float, start))
        self.cz = float(cz)
        self.ein = float(eintauchen or 0) or None
        self.rampe = float(rampe or 0.0)
        self.senkrecht = 0

    def add(self, rapid, x, y, z, feed=None):
        if abs(x - self.pos[0]) <= EPS and abs(y - self.pos[1]) <= EPS and abs(z - self.pos[2]) <= EPS:
            return
        self.moves.append(Move(bool(rapid), float(x), float(y), float(z), None if rapid else feed))
        self.pos = (float(x), float(y), float(z))

    def abheben(self):
        if self.pos[2] < self.cz - EPS:
            self.add(True, self.pos[0], self.pos[1], self.cz)

    def hin(self, x, y):
        """Über dem Punkt auf die Freifahrhöhe (die Höhen senkt danach ueber_bestand, wo es frei ist)."""
        self.abheben()
        self.add(True, x, y, max(self.cz, self.pos[2]))

    def eintauchen(self, z, weg: Sequence[Tuple[float, float]] = (), feed=None):
        """Wie am Anfang eines Zuges: senkrecht im Eintauchvorschub; mit Rampe (Fräser ohne Mittenschnitt) schräg
        auf dem Weg hin und zurück, ab der Oberfläche."""
        if z >= self.pos[2] - EPS:
            return
        if self.rampe > 0 and weg:
            from .bahn import Bahnbauer
            b = Bahnbauer(feed or self.ein or 300.0, self.ein or 300.0, self.cz, 0.5, self.pos)
            b.rampe(z, list(weg), self.rampe, z_von=None)
            for m in b.moves:
                self.add(m.rapid, m.x, m.y, m.z, m.feed)
            return
        if z < 0:
            self.senkrecht += 1
        self.add(False, self.pos[0], self.pos[1], z, self.ein)


def _zug(elemente) -> List[Tuple[float, float]]:
    """Weg für die Rampe: die Vorschub-Stücke bis zum ersten Eilgang danach. Vorher liefen alle Vorschub-Punkte des
    Stücks hintereinander – auch über einen Eilgang hinweg; die Rampe fuhr dann auf der Geraden zwischen zwei Gravur-
    Linien durchs Material (Zufallstests 07.11., sichtbar mit der grösseren Zone)."""
    weg, angefangen = [], False
    for q in elemente:
        if q[1]:
            if angefangen:
                break
            continue
        angefangen = True
        weg.append(q[3][:2])
    return weg


def schnitt_flaeche(tp: Toolpath, radius: float):
    """Fläche, die die Vorschub-Bewegungen von tp unter der Oberfläche überstreichen (Fräsermitte + Radius), als
    shapely-Fläche oder None (Zufallstests 07.11.: was das Planen in Spannung 1 noch stehen lässt)."""
    from shapely.geometry import LineString, Point
    from shapely.ops import unary_union
    stuecke, lauf, pos = [], [], None
    for m in tp.moves:
        q = (float(m.x), float(m.y), float(m.z))
        if not m.rapid and pos is not None and min(pos[2], q[2]) < 0:
            if not lauf:
                lauf = [pos[:2]]
            lauf.append(q[:2])
        elif lauf:
            stuecke.append(lauf)
            lauf = []
        pos = q
    if lauf:
        stuecke.append(lauf)
    geo = [LineString(l_).buffer(radius, resolution=8) if len(set(l_)) > 1 else Point(l_[0]).buffer(radius, 8)
           for l_ in stuecke]
    return unary_union(geo) if geo else None


def teilen(tp: Toolpath, zone, cz: float, eintauchen: float, start, rampe: float = 0.0,
           radius: float = 3.0, ganz: bool = False) -> Tuple[Optional[Toolpath], Optional[Toolpath]]:
    """(Spannung 1, Spannung 2) – None, wo nichts bleibt. ganz: Löcher (Bohren, Gewinde, Passlöcher) nicht
    zerschneiden, sondern je Loch (Stück zwischen zwei Halten auf der Freifahrhöhe) ganz zuteilen."""
    if zone is None or not tp.moves:
        return tp, None
    if ganz or tp.zyklen:
        return _ganz_teilen(tp, zone, cz, start)
    el = _elemente(tp.moves, zone, start)
    if not any(e[0] for e in el):
        return tp, None
    if all(e[0] for e in el if math.dist(e[2][:2], e[3][:2]) > EPS or e[3][2] < cz - EPS):
        return None, tp
    nah = zone.buffer(float(radius), quad_segs=8)
    # Spannung 1
    a1 = _Ausgabe(start, cz, eintauchen, rampe)
    drin = False
    for i, e in enumerate(el):
        drin_e, rapid, u, v, feed = e
        if drin_e:
            if not drin:
                a1.abheben()
                drin = True
            continue
        if drin:
            if rapid:
                # Eilgänge hinter der Zone sind Übergänge, die mit dem Geräumten der Zone rechneten: auslassen – der
                # Fräser ist schon oben und taucht erst beim nächsten Fräsweg ein
                continue
            # dahinter wieder eintauchen wie am Anfang eines Zuges
            a1.hin(u[0], u[1])
            weg = _zug(_vor(el, i, 1e9))
            a1.eintauchen(u[2], weg, feed)
            drin = False
        a1.add(rapid, v[0], v[1], v[2], feed)
    a1.abheben()
    tp1 = Toolpath(name=tp.name, moves=_eilgaenge_heben(a1.moves, nah, cz, start, eintauchen=a1.ein),
                   senkrecht=int(getattr(tp, "senkrecht", 0)) + a1.senkrecht)
    # Spannung 2: jedes zusammenhängende Stück in der Zone, 1 mm auf der gefrästen Bahn davor und danach
    a2 = _Ausgabe(start, cz, eintauchen, rampe)
    i, n = 0, len(el)
    while i < n:
        if not el[i][0]:
            i += 1
            continue
        j = i
        while j < n and el[j][0]:
            j += 1
        stueck = _zurueck(el, i, UEBERLAPPUNG) + el[i:j] + _vor(el, j, UEBERLAPPUNG)
        s = stueck[0]
        a2.hin(s[2][0], s[2][1])
        if s[1]:                              # beginnt mit einem Eilgang (Übergang in der Zone): wie geplant
            if s[2][2] < a2.pos[2] - EPS:
                a2.eintauchen(s[2][2], _zug(stueck), s[4])
        else:
            # daneben in der Luft (in Spannung 1 geräumt) abtauchen und seitlich einfahren – ohne Überlappung davor
            # (Stück beginnt mitten im Zug) wie am Anfang eines Zuges
            a2.eintauchen(s[2][2], _zug(stueck), s[4])
        for q in stueck:
            if math.dist(a2.pos, q[2]) > 1e-4:
                a2.add(q[1], q[2][0], q[2][1], q[2][2], q[4])
            a2.add(q[1], q[3][0], q[3][1], q[3][2], q[4])
        a2.abheben()
        i = j
    tp2 = Toolpath(name=tp.name, moves=a2.moves, senkrecht=a2.senkrecht)
    return (tp1 if any(not m.rapid for m in tp1.moves) else None), (tp2 if tp2.moves else None)


def _eilgaenge_heben(moves: List[Move], nah, cz: float, start, sz: float = 0.5, eintauchen=None) -> List[Move]:
    """Spannung 1: waagrechte Eilgänge unter der Freifahrhöhe, die der Zone (+ Fräserradius) nahe kommen, über die
    Freifahrhöhe – dort steht noch, was erst Spannung 2 fräst. Hinunter im Eilgang nur bis auf den Startabstand, den
    Rest im Eintauchvorschub (ueber_bestand senkt den Eilgang, wo die Simulation es erlaubt). Geht es an derselben
    Stelle gleich wieder hinauf, gar nicht hinunter."""
    import shapely
    from shapely.geometry import LineString
    aus: List[Move] = []
    pos = tuple(map(float, start))
    n = len(moves)
    for i, m in enumerate(moves):
        if m.rapid and math.hypot(m.x - pos[0], m.y - pos[1]) > EPS and min(pos[2], m.z) < cz - EPS:
            if shapely.intersects(nah, LineString([pos[:2], (m.x, m.y)])):
                hz = max(cz, pos[2], m.z)
                if pos[2] < hz - EPS:
                    aus.append(Move(True, pos[0], pos[1], hz))
                aus.append(Move(True, m.x, m.y, hz))
                nach = moves[i + 1] if i + 1 < n else None
                gleich_hinauf = (nach is not None and nach.rapid and abs(nach.x - m.x) <= EPS
                                 and abs(nach.y - m.y) <= EPS and nach.z > m.z and nach.z >= 0.0)
                if m.z < hz - EPS and not gleich_hinauf:
                    zs = max(m.z, min(hz, float(sz)))
                    if zs < hz - EPS:
                        aus.append(Move(True, m.x, m.y, zs))
                    if m.z < zs - EPS:
                        aus.append(Move(False, m.x, m.y, m.z, eintauchen))
                    pos = (m.x, m.y, m.z)
                else:
                    pos = (m.x, m.y, hz)
                continue
        aus.append(m)
        pos = (m.x, m.y, m.z)
    return aus


def _ganz_teilen(tp: Toolpath, zone, cz: float, start):
    """Löcher je Stück zwischen zwei Halten auf der Freifahrhöhe: berührt eines die Zone, ganz in Spannung 2."""
    import shapely
    from shapely.geometry import LineString, Point
    from ..model import Bohrzyklus
    ms = tp.moves
    # Stücke: ein neues beginnt mit jedem waagrechten Eilgang über der Oberfläche (Weg zum nächsten Loch; nach dem
    # tiefen Freifahren liegt er oft nur 0,5 mm über ihr) – im Loch selbst gibt es keinen
    n = len(ms)
    grenzen = {0}
    for k in range(1, n):
        a_, b_ = ms[k - 1], ms[k]
        if b_.rapid and math.hypot(b_.x - a_.x, b_.y - a_.y) > EPS and min(a_.z, b_.z) >= -EPS:
            grenzen.add(k)
    grenzen = sorted(grenzen) + [n]
    zyklen = {(z.start, z.ende): z for z in tp.zyklen}
    teil1, teil2 = [], []
    for a, b in zip(grenzen, grenzen[1:]):
        stueck = ms[a:b]
        if not stueck:
            continue
        pts = [(m.x, m.y) for m in stueck if m.z < -EPS] or [(stueck[-1].x, stueck[-1].y)]
        geo = LineString(pts) if len(set(pts)) > 1 else Point(pts[0])
        (teil2 if shapely.intersects(zone, geo) else teil1).append((a, b))

    def bauen(liste):
        if not liste:
            return None
        neu: List[Move] = []
        zy = []
        for a, b in liste:
            erste = ms[a]
            if neu and (abs(neu[-1].x - erste.x) > EPS or abs(neu[-1].y - erste.y) > EPS):
                hz = max(cz, neu[-1].z)
                if neu[-1].z < hz - EPS:
                    neu.append(Move(True, neu[-1].x, neu[-1].y, hz))
                neu.append(Move(True, erste.x, erste.y, hz))
            versatz = len(neu) - a
            neu.extend(Move(m.rapid, m.x, m.y, m.z, m.feed) for m in ms[a:b])
            for (s0, e0), z in zyklen.items():
                if a <= s0 and e0 <= b:
                    zy.append(Bohrzyklus(s0 + versatz, e0 + versatz, z.zustellung, z.verweilen))
        return Toolpath(name=tp.name, moves=neu, senkrecht=0, zyklen=zy)
    t1, t2 = bauen(teil1), bauen(teil2)
    if t1 is not None and t2 is None:
        return tp, None
    if t1 is not None:
        t1.senkrecht = int(getattr(tp, "senkrecht", 0))
    else:
        t2.senkrecht = int(getattr(tp, "senkrecht", 0))
    return t1, t2


def hoehe2_anheben(tp: Toolpath, zone2, h2: float, start) -> int:
    """Spannung 2 ohne eingezeichnete zweite Lage (wo die Spannmittel jetzt liegen, weiss niemand): jeder waagrechte
    Eilgang, der nicht ganz in einer Zone liegt, mindestens auf h2 (höchstes Spannmittel + Abstand). Liefert die Zahl
    der angehobenen Eilgänge."""
    import shapely
    from shapely.geometry import LineString
    aus: List[Move] = []
    pos = tuple(map(float, start))
    n = 0
    for m in tp.moves:
        if m.rapid and math.hypot(m.x - pos[0], m.y - pos[1]) > EPS and min(pos[2], m.z) < h2 - EPS:
            seg = LineString([pos[:2], (m.x, m.y)])
            if zone2 is None or not shapely.contains(zone2, seg):
                if pos[2] < h2 - EPS:
                    aus.append(Move(True, pos[0], pos[1], h2))
                aus.append(Move(True, m.x, m.y, h2))
                if m.z < h2 - EPS:
                    aus.append(Move(True, m.x, m.y, m.z))
                n += 1
                pos = (m.x, m.y, m.z)
                continue
        aus.append(m)
        pos = (m.x, m.y, m.z)
    tp.moves = aus
    return n
