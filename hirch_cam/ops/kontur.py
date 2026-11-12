# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Kontur (Blau): Ausschneiden mit Fräserradius-Korrektur.

Seite:
- "auto"   Teile ausschneiden: Aussenkanten werden aussen gefräst,
           Löcher innen (erkannt an der Verschachtelung der Linien).
- "aussen" / "innen" alle geschlossenen Linien auf diese Seite.
- "linie"  Fräsermitte auf der Linie.
Offene Linien: bei «aussen» rechts, bei «innen» links der Linie in Zeichenrichtung (Peter 11.11.: die
Oberfläche zeigt die Seite mit einem Pfeil); bei «auto» und «linie» auf der Linie.

Richtung (Spindel dreht rechts): Gleichlauf = aussen im Uhrzeigersinn,
innen gegen den Uhrzeigersinn; Gegenlauf umgekehrt.

Reihenfolge: zuerst die innersten Konturen (Löcher), zuletzt die
Aussenkanten – sonst ist das Teil schon lose, bevor die Löcher fertig sind.

Haltestege: Nur wenn die Kontur bis zur Materialdicke (oder tiefer) geht.
Die Stege werden gleichmässig über den Umfang verteilt oder – mit
`stege_punkte` – an von Hand gesetzten Stellen: jede Markierung gehört zur
nächstgelegenen Kontur (höchstens Fräserradius + STEG_FANG entfernt).
Auf dem Steg fährt das Werkzeug auf "Endtiefe + Steghöhe".
"""
from __future__ import annotations

import math
from typing import List, Optional, Sequence, Tuple

from .. import cam
from .bahn import (Bahnbauer, Punkt, flaeche, knochen, kurze_reihenfolge, offener_ring, punkt_in_ring,
                   eintauch_wahl, ring_ab, ring_ab_punkt, stellen_auf_ring, verschachtelung, zustellungen)

EPS = 1e-9
STEG_FANG = 5.0     # mm: so weit darf eine Steg-Markierung neben der Linie liegen


def _projektion(ring_geschlossen: List[Punkt], p: Punkt):
    """(Abstand, Weglänge) des nächsten Punkts auf dem Ring."""
    best = (math.inf, 0.0)
    s = 0.0
    for a, b in zip(ring_geschlossen, ring_geschlossen[1:]):
        dx, dy = b[0] - a[0], b[1] - a[1]
        seg2 = dx * dx + dy * dy
        seg = math.sqrt(seg2)
        t = 0.0 if seg2 <= EPS else max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / seg2))
        d = math.dist(p, (a[0] + dx * t, a[1] + dy * t))
        if d < best[0]:
            best = (d, s + seg * t)
        s += seg
    return best


def _punkt_bei(ring_geschlossen: List[Punkt], weg: float) -> Punkt:
    s = 0.0
    for a, b in zip(ring_geschlossen, ring_geschlossen[1:]):
        seg = math.dist(a, b)
        if s + seg >= weg and seg > EPS:
            t = (weg - s) / seg
            return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
        s += seg
    return ring_geschlossen[-1]


def _wie_gleichmaessig(ring: List[Punkt], punkte: Sequence[Punkt], breite: float):
    """Liegen die Markierungen so, wie die Kontur ohne Marken die Stege gleichmässig verteilt (Mitte jedes
    Abschnitts ab dem Anfang von `ring`)? Dann die Intervalle genau wie dort, sonst None. So rechnet eine alte
    Zeichnung, deren gerechnete Stege als Marken übernommen werden (Basis 26.10., «stege_auto»), bitgleich weiter.
    Spielraum: 2 % des Stegabstands, mindestens 0,02, höchstens 1 mm (gerundete Marken; Schlichtbahn mit Zugabe)."""
    n = len(punkte)
    zu = ring + [ring[0]]
    laenge = sum(math.dist(a, c) for a, c in zip(zu, zu[1:]))
    iv = _stege_intervalle(laenge, n, breite)
    if not iv:
        return None
    spiel = min(1.0, max(0.02, 0.02 * laenge / n))
    lagen = sorted(_projektion(zu, p)[1] for p in punkte)
    for lage, (v, b) in zip(lagen, iv):
        d = abs(lage - (v + b) / 2)
        if min(d, laenge - d) > spiel:
            return None
    return iv


def _stege_von_hand(ring: List[Punkt], punkte: Sequence[Punkt], breite: float, wie_auto=None):
    """Ring (offen) so drehen, dass er nicht auf einem Steg beginnt, und die
    Steg-Intervalle an den Markierungen liefern: (ring, intervalle).
    wie_auto: der Ring so gedreht, wie ihn die Kontur ohne Marken fräsen würde – liegen die Marken wie deren
    gleichmässige Verteilung, gilt genau diese (gleicher G-Code)."""
    if wie_auto is not None and punkte and breite > 0:
        iv = _wie_gleichmaessig(list(wie_auto), punkte, breite)
        if iv:
            return list(wie_auto), iv
    zu = ring + [ring[0]]
    laenge = sum(math.dist(a, c) for a, c in zip(zu, zu[1:]))
    if not punkte or breite <= 0 or laenge <= 2 * breite:
        return ring, []
    lagen = sorted(_projektion(zu, p)[1] for p in punkte)
    # Anfang in die Mitte der grössten Lücke zwischen zwei Stegen legen
    luecken = [((lagen[(k + 1) % len(lagen)] - lagen[k]) % laenge or laenge, lagen[k])
               for k in range(len(lagen))]
    weite, von = max(luecken)
    anfang = (von + weite / 2) % laenge
    p0 = _punkt_bei(zu, anfang)
    # Punkt einfügen und Ring dort beginnen lassen
    neu, s, eingefuegt = [], 0.0, False
    for a, c in zip(zu, zu[1:]):
        neu.append(a)
        seg = math.dist(a, c)
        if not eingefuegt and s - EPS <= anfang <= s + seg + EPS:
            neu.append(p0)
            j = len(neu) - 1
            eingefuegt = True
        s += seg
    if not eingefuegt:
        neu.append(p0)
        j = len(neu) - 1
    ring2 = [q for k, q in enumerate(neu[j:] + neu[:j])
             if k == 0 or math.dist(q, (neu[j:] + neu[:j])[k - 1]) > EPS]
    zu2 = ring2 + [ring2[0]]
    l2 = sum(math.dist(a, c) for a, c in zip(zu2, zu2[1:]))
    iv = []
    for p in punkte:
        m = _projektion(zu2, p)[1]
        iv.append((max(EPS, m - breite / 2), min(l2 - EPS, m + breite / 2)))
    iv.sort()
    zusammen = []
    for v, b in iv:                       # überlappende Stege vereinen
        if zusammen and v <= zusammen[-1][1]:
            zusammen[-1] = (zusammen[-1][0], max(zusammen[-1][1], b))
        else:
            zusammen.append((v, b))
    return ring2, zusammen


def _versetzen(ring: Sequence[Punkt], abstand: float, toleranz: float):
    """Ring um `abstand` versetzen (+ = nach aussen). Liefert Liste von Ringen
    (kann leer sein, wenn ein Loch kleiner als der Fräser ist)."""
    from shapely.geometry import Polygon

    poly = Polygon(ring)
    if not poly.is_valid:
        poly = poly.buffer(0)
    if abstand == 0:
        geom = poly
    elif cam.verfuegbar():
        geom = cam.puffer(poly, abstand, toleranz)
    else:
        # Auflösung so wählen, dass Rundungen genau genug sind
        r = abs(abstand)
        seg = max(8, int(math.ceil((math.pi / 2) / math.acos(max(-1, 1 - toleranz / r)))))
        geom = poly.buffer(abstand, resolution=min(seg, 256), join_style=1)
    if geom.is_empty:
        return []
    polys = list(getattr(geom, "geoms", [geom]))
    ringe = []
    for p in polys:
        if p.is_empty:
            continue
        ringe.append(offener_ring(list(p.exterior.coords)))
        for hole in p.interiors:
            ringe.append(offener_ring(list(hole.coords)))
    return ringe


def _orientieren(ring: List[Punkt], uhrzeiger: bool) -> List[Punkt]:
    ccw = flaeche(ring) > 0
    if ccw == uhrzeiger:
        ring = list(reversed(ring))
    return ring


def _abfall_normale(a: Punkt, c: Punkt, ring, aussen: bool):
    """Einheitsnormale des Stücks a→c, die zur Abfallseite zeigt (aussen:
    weg vom Ring, innen: in den Ring hinein)."""
    dx, dy = c[0] - a[0], c[1] - a[1]
    l = math.hypot(dx, dy) or 1.0
    nl = (-dy / l, dx / l)
    m = ((a[0] + c[0]) / 2 + nl[0] * 1e-3, (a[1] + c[1]) / 2 + nl[1] * 1e-3)
    links_innen = punkt_in_ring(m, ring)
    if aussen:
        return nl if not links_innen else (-nl[0], -nl[1])
    return nl if links_innen else (-nl[0], -nl[1])


def _anfahrboegen(zu: List[Punkt], ring, aussen: bool, radius: float, toleranz: float):
    """Viertelkreise auf der Abfallseite: Anfahren tangential in den Startpunkt,
    Abfahren tangential aus dem Endpunkt (wie FreeCAD LeadInOut, «Arc»).
    Liefert (Punkte Anfahren ab Startpunkt S, Punkte Abfahren ab zu[0])."""
    p0 = zu[0]
    t = (zu[1][0] - p0[0], zu[1][1] - p0[1])
    lt = math.hypot(*t) or 1.0
    t = (t[0] / lt, t[1] / lt)
    n = _abfall_normale(zu[0], zu[1], ring, aussen)
    te = (zu[-1][0] - zu[-2][0], zu[-1][1] - zu[-2][1])
    le = math.hypot(*te) or 1.0
    te = (te[0] / le, te[1] / le)
    ne = _abfall_normale(zu[-2], zu[-1], ring, aussen)
    if radius <= toleranz:
        schritte = 4
    else:
        schritte = max(4, math.ceil(math.pi / 2 / math.acos(max(-1.0, 1 - toleranz / radius))))
    c = (p0[0] + radius * n[0], p0[1] + radius * n[1])
    ein = []
    for i in range(schritte + 1):
        w = math.pi / 2 * i / schritte
        ein.append((c[0] + radius * (-t[0] * math.cos(w) - n[0] * math.sin(w)),
                    c[1] + radius * (-t[1] * math.cos(w) - n[1] * math.sin(w))))
    ce = (p0[0] + radius * ne[0], p0[1] + radius * ne[1])
    aus = []
    for i in range(1, schritte + 1):
        w = math.pi / 2 * i / schritte
        aus.append((ce[0] + radius * (-ne[0] * math.cos(w) + te[0] * math.sin(w)),
                    ce[1] + radius * (-ne[1] * math.cos(w) + te[1] * math.sin(w))))
    return ein, aus


def _start_auf_gerade(ring: List[Punkt]) -> List[Punkt]:
    """Ring in der Mitte des längsten Stücks beginnen lassen (dort ist Platz
    für den Anfahrbogen, an einer Ecke nicht)."""
    zu = ring + [ring[0]]
    k = max(range(len(ring)), key=lambda i: math.dist(zu[i], zu[i + 1]))
    a, c = zu[k], zu[k + 1]
    m = ((a[0] + c[0]) / 2, (a[1] + c[1]) / 2)
    return [m] + ring[k + 1:] + ring[:k + 1]


def _boegen_frei(ein, aus, ring, aussen: bool) -> bool:
    """Bleibt die Werkzeugmitte auf den Bögen im erlaubten Bereich (aussen:
    ausserhalb der Bahn, innen: innerhalb)?"""
    from shapely.geometry import Point, Polygon
    poly = Polygon(ring)
    if not poly.is_valid:
        poly = poly.buffer(0)
    for q in list(ein) + list(aus):
        d = poly.exterior.distance(Point(q))
        if d < 1e-3:
            continue
        drin = poly.contains(Point(q))
        if drin == aussen:
            return False
    return True


def _stege_intervalle(laenge: float, anzahl: int, breite: float):
    """Stege gleichmässig verteilt: Liste (von, bis) als Weglänge."""
    if anzahl <= 0 or breite <= 0 or laenge <= anzahl * breite * 2:
        return []
    schritt = laenge / anzahl
    return [(schritt * (k + 0.5) - breite / 2, schritt * (k + 0.5) + breite / 2)
            for k in range(anzahl)]


OFFEN_ZU_KURZ = "%d offene Linie(n) zu stark gekrümmt für den Fräser auf dieser Seite – ausgelassen."  # 11.11.


def _offen_seitlich(offen, abstand: float, toleranz: float, warnungen) -> list:
    """Offene Linien seitlich versetzt (Peter 11.11.): abstand > 0 links, < 0 rechts der Zeichenrichtung (Fräserradius
    + Aufmass). Aussen an Ecken gerundet (der Fräser fährt um die Ecke), innen gekürzt. Zerfällt der Versatz in
    Stücke (enge Kurve), jedes Stück für sich in der Reihenfolge entlang der Linie."""
    from shapely.geometry import LineString
    if abs(abstand) < 1e-9:
        return offen
    aus, weg = [], 0
    for l in offen:
        ls = LineString(l)
        if ls.length < 1e-9:
            continue
        v = ls.offset_curve(abstand, quad_segs=16, join_style="round")
        teile = [g for g in getattr(v, "geoms", [v]) if not g.is_empty and g.length > max(toleranz, 0.01)]
        if not teile:
            weg += 1
            continue
        teile.sort(key=lambda g: ls.project(LineString(g.coords).interpolate(0.5, normalized=True)))
        for g in teile:
            pts = [tuple(map(float, q)) for q in g.coords]
            # in Zeichenrichtung (offset_curve kann rechts herum umdrehen)
            if ls.project(LineString(pts).interpolate(0)) > ls.project(LineString(pts).interpolate(1, normalized=True)):
                pts.reverse()
            aus.append(pts)
    if weg:
        warnungen.append(OFFEN_ZU_KURZ % weg)
    return aus


def _mit_stegen(ring_geschlossen: List[Punkt], intervalle):
    """Punkte des Rings mit eingefügten Steg-Grenzen; jedes Teilstück
    bekommt ein Kennzeichen, ob es auf einem Steg liegt.
    Ergebnis: Liste (punkt, auf_steg_bis_zum_nächsten_punkt)."""
    grenzen = sorted({g for iv in intervalle for g in iv})
    pts = ring_geschlossen
    out = [(pts[0], None)]
    s = 0.0
    for a, b in zip(pts, pts[1:]):
        seg = math.dist(a, b)
        if seg <= EPS:
            continue
        for g in grenzen:
            if s + EPS < g < s + seg - EPS:
                t = (g - s) / seg
                out.append(((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t), None))
        out.append((b, None))
        s += seg
    # Kennzeichen setzen: Mitte jedes Teilstücks prüfen
    res = []
    s = 0.0
    for (p, _), (q, _) in zip(out, out[1:]):
        seg = math.dist(p, q)
        mitte = s + seg / 2
        steg = any(v <= mitte <= b for v, b in intervalle)
        res.append((p, q, steg))
        s += seg
    return res


def kontur(
    linien: Sequence[Tuple[Sequence[Punkt], bool]],
    durchmesser: float,
    tiefe: float,
    zustellung: float,
    vorschub: float,
    eintauchen: float,
    seite: str = "auto",
    gleichlauf: bool = True,
    materialdicke: Optional[float] = None,
    stege_anzahl: int = 0,
    stege_breite: float = 6.0,
    stege_hoehe: float = 1.5,
    stege_punkte: Optional[Sequence[Punkt]] = None,
    freifahrhoehe: float = 3.0,
    startabstand: float = 0.5,
    toleranz: float = 0.01,
    start=(0.0, 0.0, 20.0),
    name: str = "Kontur",
    aufmass: float = 0.0,
    rampe: float = 0.0,
    vollnut: float = 1.0,
    anfahren: str = "",
    anfahr_radius: float = 0.0,
    dogbone: bool = False,
    starttiefe: float = 0.0,
    vorher=None,
    nachbarn=None,
):
    """Liefert (Werkzeugweg, Warnungen).

    aufmass: + lässt mehr Material stehen (Teil grösser, Löcher kleiner),
    − nimmt mehr weg. Beim Schruppen kommt die Schlichtzugabe dazu.
    stege_punkte: Markierungen der von Hand gesetzten Stege (statt `stege_anzahl`).
    anfahren: «bogen» = im Viertelkreis auf der Abfallseite an- und abfahren
    (Radius anfahr_radius; Standard: halber Fräser-Ø), damit am Teil keine
    Eintauchmarke bleibt. Nicht bei «auf der Linie».
    rampe: Eintauchwinkel in Grad (0 = senkrecht). Geschlossene Konturen ohne
    Stege werden dann fortlaufend als Schraube gefräst, sonst je Ebene mit
    Rampe. vollnut: Vorschub-Faktor (die Kontur schneidet mit vollem Ø).
    starttiefe: hier beginnt der Fräsvorgang (darüber ist schon frei); zugestellt
    wird nur zwischen Start- und Endtiefe. vorher: [(Fläche, Tiefe)] früher
    ausgeräumt – für die Warnung, wenn über der Starttiefe noch Material steht.
    nachbarn: weitere geschlossene Linien in der Nähe (andere Konturen derselben Seite) – der Anfahrbogen hält von
    ihnen und von den eigenen Linien den Fräserradius + Aufmass Abstand, sonst senkrecht."""
    from .tasche import STARTTIEFE_NICHT_FREI, _mm
    r = durchmesser / 2
    warnungen: List[str] = []
    s0 = max(0.0, float(starttiefe or 0.0))
    if s0 >= float(tiefe) - 1e-6 and s0 > 0:
        warnungen.append("Beginnt bei Tiefe %s mm, die Kontur ist aber nur %s mm tief – nichts zu fräsen."
                         % (_mm(s0), _mm(tiefe)))
        return Bahnbauer(vorschub, eintauchen, freifahrhoehe, startabstand, start).weg(name), warnungen
    z0 = -s0                                    # ab hier steht Material
    tiefen = [z0 + z for z in zustellungen(float(tiefe) - s0, zustellung)]
    durch = materialdicke is not None and tiefe >= materialdicke - 1e-9
    steg_z = -tiefe + stege_hoehe

    geschlossen = [offener_ring(p) for p, g in linien if g and len(offener_ring(p)) >= 3]
    offen = [list(map(tuple, p)) for p, g in linien if not g and len(p) >= 2]
    if seite in ("aussen", "innen") and offen:
        offen = _offen_seitlich(offen, (r + aufmass) * (-1.0 if seite == "aussen" else 1.0), toleranz, warnungen)
    tiefe_n = verschachtelung(geschlossen)

    # (Verschachtelungstiefe, Ring, aussen?) bzw. offene Linien
    auftraege = []
    for ring, d in zip(geschlossen, tiefe_n):
        if seite == "linie":
            auftraege.append((d, ring, None))
            continue
        aussen = (d % 2 == 0) if seite == "auto" else (seite == "aussen")
        versetzt = _versetzen(ring, (r + aufmass) if aussen else -(r + aufmass), toleranz)
        if not versetzt:
            warnungen.append("Loch kleiner als der Fräser – ausgelassen.")
        for vr in versetzt:
            if dogbone:
                vr = knochen(vr, r, abfall_innen=not aussen)
            auftraege.append((d, vr, aussen))

    # Von Hand gesetzte Stege: jede Markierung zur nächsten Kontur
    von_hand = stege_punkte is not None
    steg_zu = {}
    if von_hand and durch:
        ringe = [i for i, a in enumerate(auftraege)]
        verwaist = 0
        for sp in stege_punkte:
            sp = (float(sp[0]), float(sp[1]))
            best = None
            for i in ringe:
                d = _projektion(auftraege[i][1] + [auftraege[i][1][0]], sp)[0]
                if best is None or d < best[0]:
                    best = (d, i)
            if best is not None and best[0] <= r + abs(aufmass) + STEG_FANG:
                steg_zu.setdefault(best[1], []).append(sp)
            else:
                verwaist += 1
        if verwaist:
            warnungen.append("1 Steg-Markierung liegt keiner Kontur nahe – nicht verwendet." if verwaist == 1 else
                             "%d Steg-Markierungen liegen keiner Kontur nahe – nicht verwendet." % verwaist)
        # Konturen ohne Steg meldet die Prüfung (mit Klick auf die Objekte)
    for i, a in enumerate(auftraege):
        auftraege[i] = (a[0], a[1], a[2], steg_zu.get(i, []))

    if s0 > 0 and (auftraege or offen):
        from shapely.geometry import LineString as _LS0
        from shapely.ops import unary_union as _uu
        fuss = _uu([_LS0(list(a[1]) + [a[1][0]]).buffer(r) for a in auftraege if len(a[1]) >= 2] +
                   [_LS0(l).buffer(r) for l in offen])
        frei = _uu([g for g, t in (vorher or []) if t >= s0 - 1e-6]) if vorher else None
        rest_ = fuss if frei is None or frei.is_empty else fuss.difference(frei.buffer(1e-3))
        if rest_.area > max(0.05, 0.005 * fuss.area):
            warnungen.append(STARTTIEFE_NICHT_FREI % (_mm(s0), _mm(z0 + startabstand)))

    # «Beginnt bei Tiefe»: über s0 ist nur frei, was frühere Bearbeitungen geräumt haben – der Anfahrbogen beginnt
    # neben der Kontur und darf dort nur im Eilgang hinunter, wenn auch da schon geräumt ist (Fräsbahnen 25.10.:
    # Impeller b7 «Im Bogen» tauchte sonst im Eilgang 19 mm tief ins Material)
    frei_s0 = None
    if s0 > 0:
        from shapely.ops import unary_union as _uu1
        frei_s0 = _uu1([g for g, t in (vorher or []) if t >= s0 - 1e-6]) if vorher else None

    # Anfahrbögen: nie näher als Fräserradius + Aufmass an eine gezeichnete Linie (Nachbarteil, Insel, Loch) –
    # die eigene Kontur hält schon die Bahn auf Abstand (_boegen_frei), Nachbarn nicht (Fräsbahnen 25.10.)
    _linien_geo = None

    def _weit_genug(punkte) -> bool:
        nonlocal _linien_geo
        from shapely.geometry import LineString as _LS2, MultiPoint as _MP2
        if _linien_geo is None:
            ringe_ = [list(g) + [g[0]] for g in geschlossen] + [list(map(tuple, n)) + [tuple(n[0])]
                                                              for n in (nachbarn or []) if len(n) >= 3]
            ringe_ += [l for l in offen]
            from shapely.ops import unary_union as _uu2
            _linien_geo = _uu2([_LS2(g) for g in ringe_ if len(g) >= 2])
        if _linien_geo.is_empty:
            return True
        return _linien_geo.distance(_MP2(list(punkte))) >= max(0.0, r + aufmass) - max(toleranz, 0.005) * 2

    def _start_frei(q) -> bool:
        if s0 <= 0:
            return True
        if frei_s0 is None or frei_s0.is_empty:
            return False
        from shapely.geometry import Point as _P1
        return frei_s0.buffer(1e-3).contains(_P1(q).buffer(r, 16))

    b = Bahnbauer(vorschub, eintauchen, freifahrhoehe, startabstand, start)
    stege_auto: List[Punkt] = []

    # Erst die Reihenfolge und die Eintauchstellen aller Ringe, dann die Bahnen (Peter 27.10.: bei mehreren
    # Objekten den kürzesten Weg über die Eintauchstellen – nicht beim einen links, beim nächsten rechts).
    # Tiefste Verschachtelung zuerst; innerhalb: kurze Reihenfolge über die Abstände der Ringe (nächster
    # Nachbar + 2-opt), dann je Ring die Eintauchstelle über alle Ringe zusammen (kürzester Leerweg, genau).
    from shapely.geometry import LineString as _LS, Point as _Pt
    plan = []
    letzte = None
    for stufe in sorted({a[0] for a in auftraege}, reverse=True):
        rest = [a for a in auftraege if a[0] == stufe]
        linien = [_LS(list(a[1]) + [a[1][0]]) if len(a[1]) >= 2 else _Pt(a[1][0]) for a in rest]
        von = letzte if letzte is not None else _Pt(b.pos[:2])
        folge = kurze_reihenfolge(len(rest), lambda i, j: linien[i].distance(linien[j]),
                                  lambda i: linien[i].distance(von), max_n=120)
        plan += [rest[k] for k in folge]
        if folge:
            letzte = linien[folge[-1]]
    rad_bogen = anfahr_radius or r
    stellen = []
    for _st, ring, aussen, _m in plan:
        if anfahren == "bogen" and aussen is not None:
            st = stellen_auf_ring(ring, 2.0, nur_gerade=2 * rad_bogen, rand=1.5 * rad_bogen)
        else:
            st = stellen_auf_ring(ring, 2.0)
        stellen.append(st or [tuple(ring[0])])
    wahl = eintauch_wahl([[(q, q) for q in st] for st in stellen], b.pos[:2])
    for k_plan, (_, ring, aussen, marken) in enumerate(plan):
        if True:
            ein_p = stellen[k_plan][wahl[k_plan]]
            if aussen is None:
                uhrzeiger = True
            else:
                uhrzeiger = aussen if gleichlauf else not aussen
            ring = _orientieren(ring, uhrzeiger)
            if von_hand and marken:
                wie_auto = ring_ab_punkt(ring, ein_p)
                ring, iv = _stege_von_hand(ring, marken, stege_breite, wie_auto=wie_auto)
                zu = ring + [ring[0]]
                laenge = sum(math.dist(a, c) for a, c in zip(zu, zu[1:]))
                if not iv:
                    warnungen.append("Form zu klein für Haltestege – ohne Stege gefräst.")
            else:
                ring = ring_ab_punkt(ring, ein_p)
                zu = ring + [ring[0]]
                laenge = sum(math.dist(a, c) for a, c in zip(zu, zu[1:]))
                iv = (_stege_intervalle(laenge, stege_anzahl, stege_breite)
                      if durch and not von_hand else [])
                if durch and not von_hand and stege_anzahl > 0 and not iv:
                    warnungen.append("Form zu klein für Haltestege – ohne Stege gefräst.")
            stuecke = _mit_stegen(zu, iv)
            boegen = None
            if anfahren == "bogen" and aussen is not None:
                if not (von_hand and marken):
                    # beginnt schon auf der gewählten Stelle auf einer Geraden (stellen_auf_ring)
                    zu = ring + [ring[0]]
                    laenge = sum(math.dist(a, c) for a, c in zip(zu, zu[1:]))
                    if not von_hand:
                        iv = _stege_intervalle(laenge, stege_anzahl, stege_breite) if durch else []
                    stuecke = _mit_stegen(zu, iv)
                rad = anfahr_radius or r
                material = False                        # Platz wäre da, aber neben der Kontur steht noch Material
                for _ in range(4):                      # zu gross? kleiner versuchen
                    e_, a_ = _anfahrboegen(zu, ring, aussen, rad, toleranz)
                    if _boegen_frei(e_, a_, ring, aussen) and _weit_genug(e_ + a_):
                        if _start_frei(e_[0]):
                            boegen = (e_, a_)
                            break
                        material = True
                    rad /= 2
                if boegen is None:
                    warnungen.append("Neben der Kontur steht noch Material – dort senkrecht angefahren." if material
                                     else "Kein Platz für den Anfahrbogen – dort senkrecht angefahren.")
            if durch and not von_hand and iv:
                # gerechnete Stegmitten (Basis 26.10.: als Marken übernehmen, «stege_auto»)
                stege_auto.extend(_punkt_bei(zu, (v + w) / 2) for v, w in iv)
            if boegen:
                ein, aus = boegen
                z_alt = z0
                for z in tiefen:
                    b.anfahren(*ein[0])
                    if rampe > 0:
                        b.rampe(z, ein[1:] + [q for _p, q, _s in stuecke], rampe, vollnut, z_von=z_alt)
                    else:
                        b.eintauchen(z, z_alt)
                    for q in ein[1:]:
                        b.fraesen(q[0], q[1], z, vollnut)
                    for p, q, steg in stuecke:
                        zz = max(z, steg_z) if steg else z
                        if abs(b.pos[2] - zz) > EPS:
                            b.eintauchen(zz, None)
                        b.fraesen(q[0], q[1], zz, vollnut)
                    if abs(b.pos[2] - z) > EPS:
                        b.eintauchen(z, None)
                    for q in aus:
                        b.fraesen(q[0], q[1], z, vollnut)
                    z_alt = z
                b.abheben()
                continue
            b.anfahren(*zu[0])
            schraube = (rampe > 0 and not iv and len(tiefen) >= 1
                        and laenge * math.tan(math.radians(rampe)) >= max(
                            [z0 - tiefen[0]] + [a - c for a, c in zip(tiefen, tiefen[1:])]) - EPS)
            if schraube:
                # fortlaufend: jede Runde sinkt linear um eine Zustellung
                z_alt = z0
                b.eintauchen(min(b.pos[2], z0), z0)
                for z in tiefen:
                    weg_s = 0.0
                    for p, q, _steg in stuecke:
                        weg_s += math.dist(p, q)
                        b.fraesen(q[0], q[1], z_alt + (z - z_alt) * weg_s / laenge, vollnut)
                    z_alt = z
                for p, q, _steg in stuecke:          # letzte Runde ganz auf Tiefe
                    b.fraesen(q[0], q[1], tiefen[-1], vollnut)
                b.abheben()
                continue
            z_alt = z0
            for z in tiefen:
                if rampe > 0:
                    # nur bis zum ersten Steg schräg (der Steg bleibt stehen)
                    frei = iv[0][0] if iv and z < steg_z else None
                    b.rampe(z, [q for _p, q, _s in stuecke], rampe, vollnut, z_von=z_alt,
                            max_laenge=frei)
                else:
                    b.eintauchen(z, z_alt)
                for p, q, steg in stuecke:
                    zz = max(z, steg_z) if steg else z
                    if abs(b.pos[2] - zz) > EPS:
                        b.eintauchen(zz, None)  # senkrecht auf/ab vom Steg (Nut ist frei)
                    b.fraesen(q[0], q[1], zz, vollnut)
                if abs(b.pos[2] - z) > EPS:
                    b.eintauchen(z, None)
                z_alt = z
            b.abheben()

    # Offene Linien: auf der Linie, hin und zurück
    while offen:
        k = min(range(len(offen)), key=lambda i: min(
            math.dist(b.pos[:2], offen[i][0]), math.dist(b.pos[:2], offen[i][-1])))
        l = offen.pop(k)
        if math.dist(b.pos[:2], l[-1]) < math.dist(b.pos[:2], l[0]):
            l = list(reversed(l))
        b.anfahren(*l[0])
        z_alt = z0
        for z in tiefen:
            if rampe > 0:
                b.rampe(z, l[1:], rampe, vollnut, z_von=z_alt)
            else:
                b.eintauchen(z, z_alt)
            for p in l[1:]:
                b.fraesen(p[0], p[1], None, vollnut)
            l = list(reversed(l))
            z_alt = z
        b.abheben()

    tp = b.weg(name)
    if durch:
        # Fräsbahn-Mitte der geschlossenen Konturen, die Haltestege bekommen können – für das
        # Verteilen der Marken (ops/stege.py, Entwurf B); ändert die Bahn nicht
        tp.steg_ringe = [list(a[1]) for a in auftraege if len(a[1]) >= 3]
        tp.steg_d = float(durchmesser)
        tp.stege_auto = [(float(x), float(y)) for x, y in stege_auto]
    return tp, warnungen
