# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Tasche (Grün): Fläche ausräumen.

Die Fläche ergibt sich aus den grünen geschlossenen Linien; innen liegende
Linien sind Inseln, die stehen bleiben (gerade/ungerade Verschachtelung).
Ausgeräumt wird mit parallelen Versatzbahnen von innen nach aussen; die
letzte Bahn ist die Randbahn (sauberer Rand). Zwischen zwei Bahnen fährt
das Werkzeug auf Tiefe weiter, wenn die Verbindung im bereits
ausgeräumten Bereich liegt, sonst hebt es ab.
"""
from __future__ import annotations

import math
from typing import List, Optional, Sequence, Tuple

from .. import cam
from .bahn import (Bahnbauer, Punkt, flaeche, knochen, kurze_reihenfolge, offener_ring, ring_ab,
                   verschachtelung, zustellungen)


ADAPTIV_RUNDEN = ("Wo adaptiv nicht hinkommt, wird in Runden geräumt – dort mit %s mm Zustellung statt der "
                  "ganzen Tiefe.")


def _flaechen(ringe: List[List[Punkt]]):
    """Polygone mit Löchern aus verschachtelten Ringen (gerade-ungerade)."""
    from shapely.geometry import Polygon
    from shapely.ops import unary_union

    tiefe = verschachtelung(ringe)
    polys = []
    for i, (ring, d) in enumerate(zip(ringe, tiefe)):
        if d % 2:
            continue
        loecher = []
        for j, (s, e) in enumerate(zip(ringe, tiefe)):
            if e == d + 1:
                p = Polygon(s)
                if Polygon(ring).buffer(1e-6).contains(p.representative_point()):
                    loecher.append(s)
        poly = Polygon(ring, loecher)
        if not poly.is_valid:
            poly = poly.buffer(0)
        polys.append(poly)
    return unary_union(polys) if polys else None


def _ringe_von(geom):
    """(Ring, ist_loch) aller Ränder einer (Multi-)Fläche."""
    out = []
    for p in getattr(geom, "geoms", [geom]):
        if p.is_empty or p.geom_type != "Polygon":
            continue
        out.append((offener_ring(list(p.exterior.coords)), False))
        for h in p.interiors:
            out.append((offener_ring(list(h.coords)), True))
    return out


SCHMALER_TEIL = "%d Form(en) schmaler als der Fräser – ausgelassen."      # Fable 07.11.
SCHMALER_SCHLICHT = "%d Form(en) schmaler als der Schruppfräser – dort fräst nur das Schlichtwerkzeug."
STARTTIEFE_ZU_TIEF = "Beginnt bei Tiefe %s mm, die Tasche ist aber nur %s mm tief – nichts zu fräsen."
STARTTIEFE_NICHT_FREI = ("Beginnt bei Tiefe %s mm: darüber wurde vorher nichts ausgeräumt. Die Maschine fährt im "
                         "Eilgang bis %s mm – nur so einstellen, wenn dort kein Material mehr steht.")


def _mm(v) -> str:
    return ("%g" % round(float(v), 3)).replace(".", ",")


def _ab_starttiefe(flaeche_geom, fuss, starttiefe, vorher, durchmesser, startabstand, warnungen):
    """Starttiefe als «bis hierher schon frei»: ein Eintrag für «vorher» (die ganze
    Fläche ist bis zur Starttiefe ausgeräumt). fuss: was der Fräser überstreicht.
    Warnt, wenn keine frühere Bearbeitung diesen Bereich bis zur Starttiefe geräumt hat."""
    if starttiefe <= 0:
        return []
    from shapely.ops import unary_union
    frei = unary_union([g for g, t in vorher if t >= starttiefe - 1e-6]) if vorher else None
    offen = fuss if frei is None or frei.is_empty else fuss.difference(frei.buffer(1e-3))
    if offen.area > max(0.05, 0.005 * fuss.area):
        warnungen.append(STARTTIEFE_NICHT_FREI % (_mm(starttiefe), _mm(-starttiefe + startabstand)))
    return [(flaeche_geom.buffer(durchmesser + 1.0), float(starttiefe))]


def _innen_nach_aussen(stufen):
    """Reihenfolge der Runden: [(Stufe k, Polygon)], innen vor aussen.

    stufen[0] ist die Randstufe, stufen[k + 1] liegt in stufen[k]. Zerfällt eine Stufe in mehrere
    Teilstücke (zwischen Inseln, Schaufeln …), wird jedes Teilstück von innen nach aussen fertig
    gefräst, bevor die Runde aussen herum folgt – nicht Stufe für Stufe über alle Teilstücke
    springen (Impeller 25.10.: 50 statt 27 Hübe je Ebene). Jede Runde hat innen dieselben schon
    gefrästen Runden wie vorher; geändert sind nur die Übergänge. Geschwister: das nächstgelegene
    zuerst."""
    from shapely.geometry import Point
    teile = [[t for t in getattr(s, "geoms", [s]) if not t.is_empty and t.geom_type == "Polygon"]
             for s in stufen]
    kinder = {}
    for k in range(len(teile) - 1):
        oben = teile[k]
        for j, t in enumerate(teile[k + 1]):
            pt = t.representative_point()
            i = next((i for i, o in enumerate(oben) if o.contains(pt)), None)
            if i is None:                         # Rundung: das nächste Teilstück aussen
                i = min(range(len(oben)), key=lambda i_: oben[i_].distance(pt)) if oben else None
            if i is not None:
                kinder.setdefault((k, i), []).append((k + 1, j))
    folge = []
    ort = [None]

    def naechstes(offen):
        if ort[0] is None or len(offen) == 1:
            return offen[0]
        return min(offen, key=lambda kj: teile[kj[0]][kj[1]].distance(ort[0]))

    def ablegen(kj):
        folge.append((kj[0], teile[kj[0]][kj[1]]))
        c = teile[kj[0]][kj[1]].exterior.coords[0]
        ort[0] = Point(c[0], c[1])

    # ohne Rekursion (tiefe Taschen haben hunderte Stufen): Stapel aus (Knoten, offene Kinder)
    wurzeln = [(0, i) for i in range(len(teile[0]))] if teile else []
    while wurzeln:
        w = naechstes(wurzeln)
        wurzeln.remove(w)
        stapel = [(w, list(kinder.get(w, [])))]
        while stapel:
            knoten, offen = stapel[-1]
            if offen:
                kind = naechstes(offen)
                offen.remove(kind)
                stapel.append((kind, list(kinder.get(kind, []))))
            else:
                stapel.pop()
                ablegen(knoten)
    return folge


def _ebenen(tiefe, zustellung, grenzen):
    """Zustelltiefen (negativ): gleich grosse Schritte, dazu Halt an den Tiefen
    früherer Taschen (grenzen), damit dort nichts doppelt gefräst wird."""
    punkte = sorted({0.0, float(tiefe)} | {float(g) for g in grenzen if 1e-6 < g < tiefe - 1e-6})
    ebenen = []
    for a, b in zip(punkte, punkte[1:]):
        for z in zustellungen(b - a, zustellung):
            ebenen.append(-(a - z))
    return ebenen


def tasche(
    linien: Sequence[Tuple[Sequence[Punkt], bool]],
    durchmesser: float,
    tiefe: float,
    zustellung: float,
    vorschub: float,
    eintauchen: float,
    bahnabstand_prozent: float = 40.0,
    gleichlauf: bool = True,
    freifahrhoehe: float = 3.0,
    startabstand: float = 0.5,
    toleranz: float = 0.01,
    start=(0.0, 0.0, 20.0),
    name: str = "Tasche",
    ausgeraeumt=None,
    aufmass: float = 0.0,
    nur_rand: bool = False,
    rand_breite: float = 0.0,
    rampe: float = 0.0,
    vollnut: float = 1.0,
    strategie: str = "runden",
    adaptiv_last: float = 20.0,
    dogbone: bool = False,
    wand_tan: float = 0.0,
    starttiefe: float = 0.0,
    runden_zustellung: Optional[float] = None,
    wechselnd: bool = False,
    weit_faktor: float = 0.0,
    _einzeln: bool = False,
):
    """ausgeraeumt: Liste [(Fläche, Tiefe)] von Taschen, die vorher gefräst
    werden. Dort fehlt das Material bis zu dieser Tiefe schon: diese Ebenen
    werden übersprungen. Die eigene Tasche wird danach angehängt.

    aufmass: + lässt am Rand (und an Inseln) Material stehen, − nimmt mehr weg.
    nur_rand: nur die Randbahn fahren (Schlichten nach dem Schruppen); rand_breite: so breit muss sie räumen
    (die Schlicht-Zugabe) – ist sie breiter als der Fräser, mehrere Runden.
    rampe: Eintauchwinkel in Grad (0 = senkrecht). vollnut: Vorschub-Faktor
    für Stücke, auf denen der Fräser ganz schneidet (innerste Runde, Übergänge).
    strategie: «runden» (Versatz nach innen) oder «adaptiv» (gleichbleibende
    Schnittbreite, adaptiv_last in % vom Ø; Eintauchen mit Helix).
    wand_tan: schräge Wand (V-Fräser, tan des halben Spitzenwinkels): je Ebene
    rückt der Rand um Tiefe × wand_tan nach innen – die Flanke trifft oben genau
    die gezeichnete Linie, unten liegt die Wand Tiefe × wand_tan weiter innen.
    starttiefe: hier beginnt der Fräsvorgang (mm unter der Oberfläche): darüber
    steht kein Material mehr (vorbearbeitet, frühere Bearbeitung). Zugestellt wird
    nur zwischen Start- und Endtiefe, bis knapp darüber im Eilgang.
    runden_zustellung: bei «adaptiv» – was in Runden geräumt wird (Rückfall, Reste, in die keine Helix passt),
    höchstens so tief zugestellt (die Zustellung von adaptiv ist meist die ganze Schneidenlänge; in Runden
    schneidet der Fräser aber in den innersten Runden mit vollem Ø).
    wechselnd: bei «adaptiv» Nachbar-Schwünge abwechselnd im Gleich- und Gegenlauf (Peter 31.10.: nur zum Räumen,
    das Schlichten beseitigt die Spuren) – die Rückwege zwischen den Schwüngen entfallen, wo es geprüft frei ist.
    weit_faktor: bei «adaptiv» nur die weiten Teile adaptiv räumen – was nach Öffnen mit weit_faktor × Ø übrig bleibt;
    schmale Teile (enge Lücken) in Runden, dort ist adaptiv langsamer (0 = alles adaptiv). > 0: die Tasche wird
    dreimal gerechnet – ganz adaptiv, weite Teile adaptiv + schmale in Runden, ganz in Runden – und die schnellste
    genommen (Peter 01.11.: je Form ist eine andere am besten; jede für sich ist geprüft). < 0: ganz in Runden."""
    args = dict(locals())
    if strategie == "adaptiv" and not nur_rand and weit_faktor and weit_faktor > 0 and not _einzeln:
        return _schnellste(args)
    from shapely.geometry import LineString, Point
    from shapely.ops import unary_union

    warnungen: List[str] = []
    ringe = [offener_ring(p) for p, g in linien if g and len(offener_ring(p)) >= 3]
    offene = sum(1 for p, g in linien if not g)
    if offene:
        warnungen.append(f"{offene} offene Linien ausgelassen (braucht geschlossene Formen).")
    flaeche_geom = _flaechen(ringe)
    b = Bahnbauer(vorschub, eintauchen, freifahrhoehe, startabstand, start)
    if flaeche_geom is None or flaeche_geom.is_empty:
        return b.weg(name), warnungen
    starttiefe = max(0.0, float(starttiefe or 0.0))
    if starttiefe >= float(tiefe) - 1e-6 and starttiefe > 0:
        warnungen.append(STARTTIEFE_ZU_TIEF % (_mm(starttiefe), _mm(tiefe)))
        return b.weg(name), warnungen

    r = durchmesser / 2
    schritt = durchmesser * max(5.0, min(95.0, bahnabstand_prozent)) / 100.0
    res = max(8, int(math.ceil((math.pi / 2) / math.acos(max(-1, 1 - toleranz / max(r, toleranz))))))
    res = min(res, 128)

    def versetzt(geom, d):
        if cam.verfuegbar():
            return cam.puffer(geom, -d, toleranz)
        return geom.buffer(-d, resolution=res, join_style=1)

    def bahnen_fuer(geom, zone=None, am=None):
        """Versatzstufen (Rand −r, dann je −schritt) und die Bahnen daraus.
        zone: Bereich der Fräsermitte schon vorgegeben (Restmaterial) – die
        Stufen gehen dann von ihr aus."""
        am = aufmass if am is None else am
        stufen = []
        g = zone if zone is not None else versetzt(geom, r + am)
        k = 0
        letzte = None
        while not g.is_empty and k < 10000:
            stufen.append(g.simplify(toleranz / 2))
            letzte = g
            # Schlichten: so viele Runden, dass die ganze Schlicht-Zugabe weg ist (Fable 06.11.: Zugabe 1,0 mit Ø 0,8
            # liess eine 0,2-mm-Wand in voller Höhe stehen)
            if nur_rand and 2 * r + schritt * k >= float(rand_breite or 0.0) - 1e-6:
                break
            k += 1
            # jede Stufe direkt aus der Fläche rechnen (sonst wachsen die Punkte an)
            g = (versetzt(zone, schritt * k) if zone is not None
                 else versetzt(geom, r + am + schritt * k))
        # Bahnabstand über dem Radius (Fable 06.11.: 80 % mit Ø 8 liess in der Mitte einen Klotz in voller Höhe stehen):
        # fällt die nächste Stufe weg, die innerste Runde deckt aber nicht alles (mehr als r von ihrem Rand entfernt),
        # noch eine Stufe so weit innen, wie es geht – bis die Mitte gedeckt ist
        while letzte is not None and not nur_rand and k < 10000:
            rest = versetzt(letzte, r * 0.98)
            if rest.is_empty or rest.area < 1e-4:
                break
            lo, hi = 0.0, schritt
            for _ in range(14):                       # grösster Versatz, bei dem noch etwas bleibt
                mitte_ = (lo + hi) / 2
                if versetzt(letzte, mitte_).is_empty:
                    hi = mitte_
                else:
                    lo = mitte_
            if lo <= toleranz:
                break
            g = versetzt(letzte, min(lo * 0.97, r * 0.98))
            if g.is_empty:
                break
            stufen.append(g.simplify(toleranz / 2))
            letzte = g
            k += 1
        if not stufen:
            return None
        # Bahnen von innen nach aussen. Gleichlauf (Spindel rechts): am Rand
        # gegen den Uhrzeigersinn, um Inseln herum im Uhrzeigersinn.
        bahnen: List[Tuple[List[Punkt], bool]] = []
        for k, teil in _innen_nach_aussen(stufen):
            innen = stufen[k + 1] if k + 1 < len(stufen) else None
            # innerste Runde: nichts ist vorher frei – der Fräser schneidet ganz
            voll = innen is None or innen.is_empty or not innen.intersects(teil)
            for ring, loch in _ringe_von(teil):
                soll_ccw = gleichlauf != loch
                if (flaeche(ring) > 0) != soll_ccw:
                    ring = list(reversed(ring))
                if dogbone and k == 0:
                    # Randbahn: Ecken der Tasche freifräsen (Inseln: aussen herum)
                    ring = knochen(ring, r, abfall_innen=not loch)
                bahnen.append((ring, voll))
        # hier darf die Fräsermitte auf Tiefe fahren, ohne Rand oder Inseln zu verletzen
        return bahnen, stufen[0].buffer(1e-3)

    wollte_adaptiv = strategie == "adaptiv" and not nur_rand
    if strategie == "adaptiv" and not nur_rand and wand_tan > 0:
        warnungen.append("Schräge Wand gibt es nur beim Ausräumen in Runden – in Runden geräumt.")
        strategie = "runden"
    if strategie == "adaptiv" and not nur_rand:
        if dogbone:
            warnungen.append("Dogbone gibt es nur beim Ausräumen in Runden (oder mit Schlichten).")
        if cam.verfuegbar() and hasattr(cam._hc, "adaptiv"):
            vor_adaptiv = list(ausgeraeumt or [])          # _adaptiv trägt die eigene Fläche ein
            abgedeckt = _adaptiv(flaeche_geom, b, durchmesser, tiefe, zustellung, adaptiv_last,
                                 aufmass, rampe, vollnut, toleranz, startabstand, ausgeraeumt,
                                 name, warnungen, res, starttiefe,
                                 span_bezug=max(5.0, min(95.0, bahnabstand_prozent)) / 100.0,
                                 wechselnd=wechselnd, nur=_weit(flaeche_geom, durchmesser, weit_faktor, res))
            # Was adaptiv nicht erreicht (schmale Bänder, in die keine Helix passt – Impeller Stufe 3: ein
            # 3-mm-Ring am Rand), in Runden nachräumen; nur dort, wo noch Material steht
            if not abgedeckt.is_empty:
                # Rest ist nur, was der Fräser erreichen kann: nicht das Aufmass an den Wänden (bleibt mit
                # Absicht stehen), nicht Innenecken, in die er nicht passt
                erreichbar = versetzt(flaeche_geom, r + aufmass)
                erreichbar = erreichbar.buffer(r, resolution=res) if not erreichbar.is_empty else erreichbar
                abgedeckt = abgedeckt.union(flaeche_geom.difference(erreichbar))
            adaptiv_rest = [(abgedeckt, float(tiefe))] if not abgedeckt.is_empty else None
        else:
            warnungen.append("Adaptives Ausräumen braucht das CAM-Modul – in Runden geräumt.")
            adaptiv_rest = None
    else:
        adaptiv_rest = None

    # früher ausgeräumtes Material, das diese Tasche berührt
    vorher = [(g, float(t)) for g, t in (list(ausgeraeumt or []) if adaptiv_rest is None
                                         else vor_adaptiv + adaptiv_rest)
              if not g.is_empty and g.intersects(flaeche_geom)]
    ganz = bahnen_fuer(flaeche_geom)
    if ganz is None:
        if adaptiv_rest is None:
            warnungen.append("Fläche schmaler als der Fräser – ausgelassen.")
        return b.weg(name), warnungen
    # Fable 07.11.: passt der Fräser in eine von mehreren Formen gar nicht hinein, blieb sie ohne Hinweis stehen
    zu_schmal = sum(1 for g_ in getattr(flaeche_geom, "geoms", [flaeche_geom])
                    if not g_.is_empty and g_.area > 1e-6 and versetzt(g_, r + aufmass).is_empty)
    if zu_schmal:
        warnungen.append(SCHMALER_TEIL % zu_schmal)
    if adaptiv_rest is None:
        vorher += _ab_starttiefe(flaeche_geom, ganz[1].buffer(r, resolution=res), starttiefe, vorher,
                                 durchmesser, startabstand, warnungen)
    else:
        vorher += _ab_starttiefe(flaeche_geom, ganz[1].buffer(r, resolution=res), starttiefe, vorher,
                                 durchmesser, startabstand, [])     # schon gemeldet

    gefraest = 0
    # Fräsbahnen 31.10. (Denk-Hilfe Fable, im Code bestätigt): adaptiv mit voller Schneidenlänge als Zustellung –
    # was davon in Runden geräumt wird, lief bisher mit derselben Zustellung, also als Vollnut über die ganze Tiefe
    zust_r = zustellung                       # unverändert weitergeben (kann eine eigene Zustellungs-Art tragen)
    if wollte_adaptiv and runden_zustellung and 0 < float(runden_zustellung) < float(zustellung or tiefe) - 1e-6:
        zust_r = float(runden_zustellung)
    geraeumt_je = []                       # schräge Wand: (Fläche, Tiefe) je Ebene
    # Getrennte Flächen nacheinander, jede über alle Ebenen fertig (nicht Ebene
    # für Ebene über alle Flächen springen): nächstgelegene Fläche zuerst
    teile = [t for t in getattr(flaeche_geom, "geoms", [flaeche_geom]) if not t.is_empty]
    # kurze Leerwege: Reihenfolge der Flächen vorab (nächster Nachbar + 2-opt)
    folge_t = kurze_reihenfolge(len(teile), lambda i, j: teile[i].distance(teile[j]),
                                lambda i: teile[i].distance(Point(b.pos[:2])), max_n=120)
    for k_t in folge_t:
        teil = teile[k_t]
        vorher_t = [(g, t) for g, t in vorher if g.intersects(teil)]
        cache = {}
        z_alt = 0.0
        erlaubt_alt = None                    # wo der Fräser auf der vorigen Ebene frei ist
        for ebene, z in enumerate(_ebenen(tiefe, zust_r, [t for _g, t in vorher_t])):
            schon = tuple(i for i, (_g, t) in enumerate(vorher_t) if t >= -z - 1e-6)
            am_z = aufmass + (-z) * wand_tan if wand_tan > 0 else aufmass
            wz = round(am_z - aufmass, 6)
            if ((), wz) not in cache:
                cache[((), wz)] = bahnen_fuer(teil, None, am_z)
            ganz_z = cache[((), wz)]
            if ganz_z is None:
                continue                      # auf dieser Tiefe zu schmal für den Fräser
            if wand_tan > 0 and ausgeraeumt is not None:
                geraeumt_je.append((versetzt(teil, r + am_z).buffer(r, resolution=res, join_style=1), -z))
            if (schon, wz) not in cache:
                rest = teil.difference(unary_union([vorher_t[i][0] for i in schon]))
                # Haarfeine Reste (Rechenungenauigkeit entlang der Wände) weglassen. Die
                # Fräsermitte fährt überall, wo sie darf (Abstand zum Rand) und der Fräser
                # den Rest noch berührt: so erreicht er auch schmale Zwickel (Ecken, die ein
                # grösserer Fräser stehen liess) bis zur Wand, ohne Runden im Leeren.
                eps = max(toleranz, 0.005) * 2
                if adaptiv_rest is not None:
                    eps = max(eps, 0.06)          # Ungenauigkeit der adaptiven Bahnen (Toleranz × 5)
                rest = rest.buffer(-eps, resolution=8).buffer(eps, resolution=8)
                if adaptiv_rest is not None and not rest.is_empty:
                    # Krümel und Streifen unter 0,4 mm mittlerer Breite (Rundung der Flächen, Anfang und Ende der
                    # Schlichtbahn) zählen nicht – adaptiv hat sie im Rahmen der Toleranz gefräst
                    rest = unary_union([t for t in getattr(rest, "geoms", [rest])
                                        if t.area >= 0.3 and 2 * t.area / max(t.length, 1e-9) >= 0.4])
                zone = None
                if not rest.is_empty and rest.area > 1e-6:
                    zone = ganz_z[1].buffer(-1e-3).intersection(rest.buffer(0.95 * r, resolution=res))
                    zone = unary_union([t for t in getattr(zone, "geoms", [zone])
                                        if t.geom_type == "Polygon" and t.area > 1e-6])
                if wand_tan > 0:
                    # Schräge Wand: die Randrunde jeder Ebene immer fahren – ihre Flanke
                    # schneidet die Wand bis zur Oberfläche, auch wo der Boden darunter
                    # schon geräumt ist (sonst bliebe zwischen den Ebenen eine Stufe)
                    rand = ganz_z[1].buffer(-1e-3)
                    band = rand.difference(rand.buffer(-max(0.5 * schritt, 0.01), resolution=res))
                    zone = band if zone is None or zone.is_empty else unary_union([zone, band])
                cache[(schon, wz)] = (bahnen_fuer(teil, zone, am_z) if zone is not None and not zone.is_empty
                                      else None)
            auf = cache[(schon, wz)]
            if auf is None:
                continue                      # Material hier schon weg
            bahnen, erlaubt = auf
            gefraest += 1
            for nr, (ring, voll) in enumerate(bahnen):
                if nr == 0 and len(ring) > 2:
                    # Start je Ebene versetzt: Übergänge liegen nicht übereinander
                    ring = list(ring)
                    k = int(len(ring) * ((ebene * 0.381966) % 1.0))
                    ring = ring[k:] + ring[:k]
                else:
                    ring = ring_ab(ring, b.pos[:2])
                p0 = ring[0]
                f_ring = vollnut if voll else 1.0
                if abs(b.pos[2] - z) < 1e-9 and erlaubt.contains(LineString([b.pos[:2], p0])):
                    b.fraesen(p0[0], p0[1], None, vollnut)   # Übergang: Fräser schneidet ganz
                else:
                    unten = (nr == 0 and erlaubt_alt is not None and b.pos[2] < -1e-9
                             and abs(b.pos[2] - z_alt) < 1e-9
                             and erlaubt_alt.buffer(1e-6).contains(LineString([b.pos[:2], p0])))
                    if unten:
                        # neue Ebene: nicht hochfahren – auf der vorigen Ebene (alles frei)
                        # knapp angehoben im Eilgang zum Start, dann weiter hinunter
                        b.eilgang_z(min(0.0, z_alt + startabstand))
                        b._add(True, p0[0], p0[1], b.pos[2])
                    else:
                        b.abheben()
                        b.anfahren(*p0)
                    # über schon ausgeräumtem Boden im Eilgang bis knapp darüber
                    fuss = Point(p0).buffer(r, 16)
                    boden = max([t for g, t in vorher_t if g.buffer(1e-3).contains(fuss)] + [0.0])
                    if boden > 0 and -boden + startabstand > z:
                        b.eilgang_z(-boden + startabstand)
                    frei = min(z_alt, -boden)
                    if rampe > 0:
                        # Rampe auf dieser Runde; ist sie zu kurz, weiter auf den
                        # folgenden Runden (solange der Weg in der Tasche bleibt)
                        weg = ring[1:] + [ring[0]]
                        for folge, _v in bahnen[nr + 1:]:
                            q = min(folge, key=lambda t: math.dist(t, weg[-1]))
                            if not erlaubt.contains(LineString([weg[-1], q])):
                                break
                            k = folge.index(q)
                            weg += folge[k:] + folge[:k] + [q]
                            if sum(math.dist(a, c) for a, c in zip(weg, weg[1:])) > 4 * durchmesser:
                                break
                        b.rampe(z, weg, rampe, vollnut, z_von=frei, max_laenge=4 * durchmesser)
                    else:
                        b.eintauchen(z, frei)
                for p in ring[1:] + [ring[0]]:
                    b.fraesen(p[0], p[1], None, f_ring)
            erlaubt_alt = erlaubt
            z_alt = z
        b.abheben()
    if gefraest and zust_r is not zustellung:
        warnungen.append(ADAPTIV_RUNDEN % _mm(zust_r))
    if adaptiv_rest is not None:
        return b.weg(name), warnungen                # Ausgeräumtes hat _adaptiv schon eingetragen
    if vorher and gefraest == 0:
        warnungen.append("Material ist hier schon ausgeräumt (tiefere Tasche vorher) – nichts zu fräsen.")
    if ausgeraeumt is not None and wand_tan > 0:
        ausgeraeumt.extend(geraeumt_je)
    elif ausgeraeumt is not None:
        # was diese Tasche ausräumt: alles, was der Fräser erreicht
        ausgeraeumt.append((versetzt(flaeche_geom, r + aufmass).buffer(r, resolution=res,
                                                                      join_style=1),
                            float(tiefe)))
    return b.weg(name), warnungen


def _helix(b: Bahnbauer, mitte, radius, z_von, z_bis, winkel, faktor):
    """Schraubenförmig von z_von auf z_bis um «mitte» (Startpunkt rechts davon)."""
    tan_w = math.tan(math.radians(max(0.5, min(45.0, winkel or 3.0))))
    umfang = 2 * math.pi * radius
    runden = max(1.0, (z_von - z_bis) / (umfang * tan_w))
    n = max(12, int(math.ceil(runden * 32)))
    gesamt = runden * 2 * math.pi
    for k in range(1, n + 1):
        t = k / n
        w = t * gesamt
        b.fraesen(mitte[0] + radius * math.cos(w), mitte[1] + radius * math.sin(w),
                  z_von + (z_bis - z_von) * t, faktor)
    for k in range(1, 33):                       # eine Runde ganz unten
        w = gesamt + k / 32 * 2 * math.pi
        b.fraesen(mitte[0] + radius * math.cos(w), mitte[1] + radius * math.sin(w), z_bis, faktor)


def _zeit(tp, start) -> float:
    """Minuten wie die Zeitschätzung (Eilgang 3000 mm/min, Vorschub laut Bahn)."""
    pos, t = tuple(map(float, start)), 0.0
    for m in tp.moves:
        d = math.dist(pos, (m.x, m.y, m.z))
        t += d / 3000.0 if m.rapid else (d / m.feed if m.feed else 0.0)
        pos = (m.x, m.y, m.z)
    return t


def _schnellste(args):
    """Adaptiv: ganz adaptiv / weite Teile adaptiv + schmale in Runden / ganz in Runden – die schnellste. Jede Variante
    rechnet mit einer Kopie von «ausgeraeumt»; was die gewählte einträgt, kommt danach in die echte Liste."""
    ausgeraeumt = args.get("ausgeraeumt")
    faktor = float(args["weit_faktor"])
    wahl = None
    for wf in (0.0, faktor, -1.0):
        if wf > 0:
            ringe = [offener_ring(p) for p, g in args["linien"] if g and len(offener_ring(p)) >= 3]
            f = _flaechen(ringe)
            if f is None or f.is_empty or _weit(f, args["durchmesser"], wf, 16) is None:
                continue                      # keine schmalen Teile: wie ganz adaptiv
        kopie = None if ausgeraeumt is None else list(ausgeraeumt)
        tp, warn = tasche(**dict(args, ausgeraeumt=kopie, weit_faktor=wf, _einzeln=True))
        t = _zeit(tp, args["start"])
        if wahl is None or t < wahl[0] - 1e-9:
            wahl = (t, tp, warn, kopie)
    t, tp, warn, kopie = wahl
    if ausgeraeumt is not None:
        ausgeraeumt.extend(kopie[len(ausgeraeumt):])
    return tp, warn


def _weit(flaeche, durchmesser, faktor, res):
    """Weite Teile einer Fläche (Peter 01.11.: adaptiv nur dort, schmale Teile in Runden): morphologisch geöffnet mit
    einem Kreis faktor × Ø – nur, was einen solchen Kreis aufnimmt, und alles, was er dabei überstreicht. None: alles
    adaptiv (faktor 0 oder kaum schmale Teile); leer: alles in Runden."""
    if faktor and faktor < 0:
        from shapely.geometry import Point
        return Point(0, 0).buffer(0)          # ganz in Runden
    if not faktor:
        return None
    h = faktor * durchmesser / 2
    w = flaeche.buffer(-h, resolution=res)
    w = w.buffer(h, resolution=res).intersection(flaeche) if not w.is_empty else w
    if w.is_empty:
        return w                       # alles schmal: ganz in Runden
    if w.area >= 0.98 * flaeche.area:
        return None
    return w


def _wechsel(mitte, startpunkt, wege, r, helix_d, bahnabstand):
    """Peter 31.10. (Gleich- und Gegenlauf im Wechsel; Denk-Hilfe Fable, Python statt C++): aus den Wegen des
    Adaptiven Ketten bilden (zusammenhängende Schnitte zwischen zwei Übergängen im Freien) und jede zweite rückwärts
    fahren. Statt Rückweg (Ende → Anfang der nächsten Kette) dann: Ende → Ende der nächsten (als Schnitt im
    Vollnut-Vorschub), diese rückwärts, danach Anfang → Anfang der übernächsten (Übergang im Freien).
    Umgekehrt wird nur, wenn beide neuen Wege im schon Geräumten liegen – zusammen mit dem, was die umgekehrte Kette
    selbst wegnimmt (Ende → Ende) bzw. mit beiden Ketten (Anfang → Anfang); sonst bleibt es wie bisher. Die Bahn jeder
    Kette ist dieselbe, also auch die weggenommene Fläche. Der erste Bahnabstand einer umgekehrten Kette läuft im
    Vollnut-Vorschub (dort setzt der Fräser an der Wand an). Liefert neue Wege; Art 4 = Schnitt im Vollnut-Vorschub."""
    import math
    from shapely.geometry import LineString, Point
    # Ketten und Übergänge
    teile, pos, kette = [], tuple(startpunkt), [tuple(startpunkt)]
    for art, pts in wege:
        if not pts:
            continue
        if art == 0:
            kette += [tuple(q) for q in pts]
        else:
            teile.append(("k", kette))
            teile.append((art, [tuple(q) for q in pts]))
            kette = [tuple(pts[-1])]
        pos = tuple(pts[-1])
    teile.append(("k", kette))

    def band(pts):
        return (LineString(pts) if len(pts) >= 2 else Point(pts[0])).buffer(r, resolution=8)

    def frei(weg, *dazu):
        g = band(weg).difference(geraeumt)
        for d_ in dazu:
            if g.is_empty:
                break
            g = g.difference(d_)
        return g.is_empty or g.area < 1e-3

    geraeumt = Point(mitte).buffer(helix_d / 2 + r, resolution=16)
    neu, i, n = [], 0, len(teile)
    aus = []
    while i < n:
        typ, pts = teile[i]
        if typ != "k":
            aus.append((typ, pts))
            i += 1
            continue
        aus.append((0, pts[1:]))                      # Kette vorwärts (beginnt dort, wo der Fräser steht)
        b_k = band(pts)
        # nächste Kette umkehren?  teile: k_i, L_i, k_j, L_j, k_m …
        if i + 2 < n and teile[i + 1][0] in (1, 3) and teile[i + 2][0] == "k":
            kj = teile[i + 2][1]
            weiter = i + 4 < n and teile[i + 3][0] in (1, 3) and teile[i + 4][0] == "k"
            schluss = i + 3 >= n
            if len(kj) >= 2 and (weiter or schluss):
                b_j = band(kj)
                g2 = geraeumt.union(b_k)
                ok = True
                geraeumt_alt, geraeumt = geraeumt, g2
                if not frei([pts[-1], kj[-1]], b_j):
                    ok = False
                if ok and weiter and not frei([kj[0], teile[i + 4][1][0]], b_j):
                    ok = False
                geraeumt = geraeumt_alt
                if ok:
                    rueck = list(reversed(kj))
                    # erster Bahnabstand im Vollnut-Vorschub
                    erst, rest, l = [rueck[0]], [], 0.0
                    for a_, b_ in zip(rueck, rueck[1:]):
                        if l < bahnabstand:
                            erst.append(b_)
                        else:
                            rest.append(b_)
                        l += math.dist(a_, b_)
                    aus.append((4, [kj[-1]]))             # Ende → Ende
                    aus.append((4, erst[1:]))
                    if rest:
                        aus.append((0, rest))
                    geraeumt = geraeumt.union(b_k).union(b_j)
                    if weiter:
                        aus.append((1, [teile[i + 4][1][0]]))   # Anfang → Anfang der übernächsten
                    i += 4
                    continue
        geraeumt = geraeumt.union(b_k)
        i += 1
    neu = [(a, p) for a, p in aus if p]
    return neu


def _adaptiv(flaeche_geom, b, durchmesser, tiefe, zustellung, last, aufmass, rampe, vollnut,
             toleranz, startabstand, ausgeraeumt, name, warnungen, res, starttiefe=0.0, span_bezug=0.4,
             wechselnd=False, nur=None):
    """Tasche adaptiv ausräumen: dieselben 2D-Bahnen auf jeder Ebene, Eintauchen
    mit Helix (FreeCAD Adaptive); Übergänge im Freien knapp angehoben.

    Schneiden mit dem Zuschlag für dünnere Späne (span_zuschlag; die Schnittdaten gelten für den Bahnabstand
    span_bezug der Runden). Liefert die Fläche, die der Fräser dabei
    überstreicht (die Tasche räumt danach in Runden nach, was fehlt; leer: nichts gefräst)."""
    from shapely.geometry import LineString, Point
    from shapely.ops import unary_union
    r = durchmesser / 2
    helix_d = durchmesser * 0.75
    anteil = max(0.05, min(0.9, last / 100.0))
    # nur: weite Teile (_weit) – adaptiv dort, der Rest in Runden (die Tasche räumt nach, was nicht überstrichen ist)
    ergebnis = cam.adaptiv(flaeche_geom if nur is None or nur.is_empty else nur, durchmesser, helix_d, anteil,
                           max(0.01, toleranz * 5), aufmass, True) if (nur is None or not nur.is_empty) else []
    if not ergebnis:
        return Point(0, 0).buffer(0)                 # leer: alles in Runden
    zuschlag = span_zuschlag(anteil, span_bezug)        # Bezug: Bahnabstand der Runden dieser Tasche
    fahrt = ergebnis
    if wechselnd:
        fahrt = [(m_, s_, _wechsel(m_, s_, w_, r, helix_d, max(0.3, anteil * durchmesser)))
                 for m_, s_, w_ in ergebnis]
    abgedeckt = []
    vorher = [(g, float(t)) for g, t in (ausgeraeumt or []) if not g.is_empty and g.intersects(flaeche_geom)]
    vorher += _ab_starttiefe(flaeche_geom, cam.puffer(flaeche_geom, -(r + aufmass), toleranz).buffer(r),
                             starttiefe, vorher, durchmesser, startabstand, warnungen)
    z_alt = 0.0
    ebenen = _ebenen(tiefe, zustellung, [t for _g, t in vorher])
    for z in ebenen:
        hub = min(0.5, (z_alt - z) / 2) if z_alt > z else 0.5
        for mitte, startpunkt, wege in fahrt:
            boden = max([t for g, t in vorher if g.buffer(1e-3).contains(
                Point(mitte).buffer(r, 16))] + [0.0])
            if boden >= -z - 1e-6:
                continue                       # hier ist schon alles weg
            hr = helix_d / 2
            b.anfahren(mitte[0] + hr, mitte[1])
            frei = min(z_alt, -boden)
            b.bis_frei(frei)                    # durch Ausgeräumtes im Eilgang
            b.eintauchen(frei, None)
            _helix(b, mitte, hr, frei, z, rampe, vollnut)
            b.fraesen(startpunkt[0], startpunkt[1], z, vollnut)
            for art, pts in wege:
                if not pts:
                    continue
                if art == 0:                    # schneiden
                    for q in pts:
                        b.fraesen(q[0], q[1], z, zuschlag)
                elif art == 4:                  # schneiden im Vollnut-Vorschub (Gleich-/Gegenlauf im Wechsel)
                    for q in pts:
                        b.fraesen(q[0], q[1], z, vollnut)
                elif art in (1, 3):             # Übergang im Freien, leicht angehoben
                    # Fräsbahnen 31.10. (Denk-Hilfe Fable): in engen Lücken waren 62 % der Zeit diese Übergänge –
                    # hinauf und hinunter im Eintauchvorschub, quer im Grundvorschub. Jetzt: hinauf im Eilgang
                    # (aus dem Schnitt nach oben ist immer frei – die Bahn ist auf jeder Ebene dieselbe), quer mit
                    # dem Span-Zuschlag, hinunter schräg auf dem letzten Stück (über Geräumtem dieser Ebene).
                    oben = z + hub if art == 1 else max(z_alt, z + hub)
                    if oben > b.pos[2] + 1e-9:
                        b.eilgang_z(oben)
                    for q in pts[:-1]:
                        b.fraesen(q[0], q[1], oben, zuschlag)
                    b.fraesen(pts[-1][0], pts[-1][1], z, zuschlag)
                else:                           # durch Material: abheben
                    b.abheben()
                    b.anfahren(*pts[-1])
                    b.bis_frei(z_alt)
                    b.eintauchen(z_alt, None)
                    b.eintauchen(z, None)
            b.abheben()
        z_alt = z
    if ausgeraeumt is not None:
        ausgeraeumt.append((cam.puffer(flaeche_geom, -(r + aufmass), toleranz).buffer(r, resolution=res,
                                                                                    join_style=1),
                            float(tiefe)))
    # überstrichen (auf jeder Ebene dieselben Bahnen): Helix, Schnitte; Übergänge liegen höher
    for mitte, startpunkt, wege in ergebnis:
        abgedeckt.append(Point(mitte).buffer(helix_d / 2 + r, resolution=16))
        zug = [startpunkt]
        for art, pts in wege:
            if art == 0 and pts:
                zug += list(pts)
            else:
                if len(zug) >= 2:
                    abgedeckt.append(LineString(zug).buffer(r, resolution=16))
                zug = [pts[-1]] if pts else []
        if len(zug) >= 2:
            abgedeckt.append(LineString(zug).buffer(r, resolution=16))
    return unary_union(abgedeckt)


def adaptiv_lohnt(linien, durchmesser: float, faktor: float = 5.0) -> bool:
    """Weite Tasche (Peter 25.10.: adaptiv vorschlagen): irgendwo passt ein Kreis von faktor × Ø hinein – dort
    räumt adaptiv mit voller Zustellung deutlich schneller als Runden (150 × 100 × 12, Ø 6: 20,5 statt 49 min).
    In engen Lücken ist adaptiv langsamer."""
    ringe = [offener_ring(p) for p, g in linien if g and len(offener_ring(p)) >= 3]
    f = _flaechen(ringe)
    if f is None or f.is_empty or durchmesser <= 0:
        return False
    return not f.buffer(-faktor * durchmesser / 2, resolution=8).is_empty


def span_zuschlag(anteil: float, bezug: float = 0.4, hoechstens: float = 1.5) -> float:
    """Vorschub-Faktor für dünnere Späne bei schmalem Eingriff (radiale Spanausdünnung): bei der Schnittbreite
    anteil × Ø ist der Span so dick wie bei bezug × Ø (Bahnabstand der Runden, für den die Schnittdaten gelten),
    wenn der Vorschub um √(1 − (1 − 2·bezug)²) / √(1 − (1 − 2·anteil)²) steigt. Höchstens «hoechstens»."""
    def dicke(a):
        a = max(0.01, min(0.5, a))
        return math.sqrt(1 - (1 - 2 * a) ** 2)
    return round(max(1.0, min(hoechstens, dicke(bezug) / dicke(anteil))), 3)
