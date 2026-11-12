# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Prüfung vor dem Fräsen.

Läuft nach jeder Rechnung mit und liefert eine Liste von Befunden:
    {"stufe": "fehler" | "warnung", "text": …, "ort": [x, y] | None,
     "bearbeitung": ID | None}
«Fehler» sind Dinge, die Werkstück, Fräser oder Maschine gefährden; vor dem
Speichern muss man sie bestätigen. Koordinaten in mm (Nullpunkt unten links).
"""
from __future__ import annotations

import math
import os
from typing import Dict, List, Optional

WANDSTAERKE = 1.0        # mm: dünner wird gewarnt
KLEINES_TEIL = 30.0      # mm: ausgeschnittene Teile mit kleinerer Kante
BODEN_ZWEISEITIG = 2.0   # mm: weniger Material zwischen Taschen von Seite 1 und 2 wird gewarnt



def _z(v, stellen=None) -> str:
    """Zahl für Meldungen mit Dezimalkomma (wie die Oberfläche); englisch macht sprache.py den Punkt daraus."""
    t = f"{float(v):g}" if stellen is None else f"{float(v):.{stellen}f}"
    return t.replace(".", ",")

def _hausdorff(a, b) -> float:
    """Hausdorff-Abstand; bei entarteten Linien (doppelte Punkte) unendlich."""
    import warnings
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        try:
            v = a.hausdorff_distance(b)
        except Exception:
            return float("inf")
    return v if v == v else float("inf")


def _befund(stufe, text, ort=None, bid=None, seite=None, objekte=None):
    """objekte: IDs der betroffenen Objekte in der Zeichnung (Klick wählt sie aus)."""
    b = {"stufe": stufe, "text": text,
         "ort": [round(ort[0], 2), round(ort[1], 2)] if ort is not None else None,
         "bearbeitung": bid, "seite": seite}
    ids = [i for i in dict.fromkeys(objekte or []) if i]
    if ids:
        b["objekte"] = ids
    return b


def _flaechen(objekte):
    from shapely.geometry import Polygon
    from .ops.bahn import offener_ring
    aus = []
    for o in objekte:
        if o.get("geschlossen"):
            ring = offener_ring([tuple(p) for p in o["punkte"]])
            if len(ring) >= 3:
                p = Polygon(ring)
                if not p.is_valid:
                    # sich selbst schneidender Umriss: wie die Tasche (ops/tasche._flaechen) bereinigen –
                    # sonst bricht die ganze Prüfung ab (Impeller 25.10.: TopologyException, keine Kollisionsprüfung)
                    p = p.buffer(0)
                aus.append(p)
    return aus


def _eids(objekte):
    """Objekt-IDs in derselben Reihenfolge wie _flaechen(objekte)."""
    from .ops.bahn import offener_ring
    return [o.get("eid") for o in objekte
            if o.get("geschlossen") and len(offener_ring([tuple(p) for p in o["punkte"]])) >= 3]


def _teile_zu_nah(konturen) -> List[Dict]:
    """konturen: [(bid, kurz, Seite, Fräser-Ø, Aufmass, seite-Wahl, Objekte)]. Je Teil (äussere Kontur, «aussen»
    bzw. bei «auto» die äusserste Ebene) frisst der Fräser einen Ring von Ø + Aufmass rund herum. Liegt darin
    Material eines anderen Teils derselben Seite, wird es angeschnitten → Fehler mit Ort und beiden Objekten."""
    from shapely.geometry import Polygon
    teile = []            # (bid, kurz, Seite, Ø, Aufmass, Umriss, Material, eid)
    for bid, kurz, seite, d, aufmass, wahl, objekte in konturen:
        if wahl in ("innen", "linie") or d <= 0:
            continue
        fl = _flaechen(objekte)
        eids = _eids(objekte)
        for i, f in enumerate(fl):
            if not f.is_valid:
                f = f.buffer(0)
            if f.is_empty or f.geom_type != "Polygon":
                # Zufallstests 07.11.: eine Form, die sich selbst kreuzt, wird zu mehreren Flächen – vorher brach die
                # Prüfung hier ab (Absturz im Panel). Die grösste Fläche zählt.
                teile_f = [g for g in getattr(f, "geoms", []) if g.geom_type == "Polygon" and not g.is_empty]
                if not teile_f:
                    continue
                f = max(teile_f, key=lambda g: g.area)
            tief = sum(1 for j, g in enumerate(fl) if j != i and g.contains(f))
            if wahl == "auto" and tief % 2:
                continue                                   # Loch im Teil: die Kontur läuft innen
            loecher = [g for j, g in enumerate(fl) if j != i and f.contains(g)
                       and sum(1 for k, h in enumerate(fl) if k != j and h.contains(g)) == tief + 1]
            material = f
            for g in loecher:
                material = material.difference(g)
            teile.append((bid, kurz, seite, d, aufmass, Polygon(f.exterior), material, eids[i] if i < len(eids) else None))
    aus = []
    gemeldet = set()
    for a in teile:
        bid, kurz, seite, d, aufmass, umriss, _m, eid = a
        ring = umriss.buffer(max(0.0, aufmass) + d, join_style=1).difference(umriss.buffer(max(0.0, aufmass)))
        for b in teile:
            if b is a or b[2] != seite:
                continue
            schnitt = ring.intersection(b[6].buffer(-0.02))
            if schnitt.is_empty or schnitt.area < 0.01:
                continue
            paar = frozenset((id(a), id(b)))
            if paar in gemeldet:
                continue
            gemeldet.add(paar)
            abstand = umriss.distance(b[5])
            noetig = d + max(0.0, aufmass)
            aus.append(_befund("fehler", f"{kurz}: Teile zu nah beieinander – der Fräser (Ø {_z(d)} mm) schneidet in "
                               f"das andere Teil. Abstand {_z(abstand, 1)} mm, nötig mindestens {_z(noetig, 1)} mm.",
                               schnitt.representative_point().coords[0], bid, seite,
                               objekte=[e for e in (eid, b[7]) if e]))
    return aus


def pruefen(auftrag: Dict, erg: Dict, material: Optional[Dict] = None) -> List[Dict]:
    """auftrag: Auftrag an engine.rechnen; erg: dessen Ergebnis;
    material: {"breite", "hoehe", "dicke"}."""
    try:
        import shapely  # noqa: F401
    except ImportError:
        return []
    from shapely.geometry import LineString, Point
    from shapely.ops import unary_union

    befunde: List[Dict] = []
    werkzeuge = auftrag.get("werkzeuge", {})
    dicke = float(auftrag.get("hoehen", {}).get("materialdicke") or 18)
    maschine = auftrag.get("maschine") or {}
    namen = {b.get("id"): b.get("name", "") for b in auftrag.get("bearbeitungen", [])}
    statistik = erg.get("bearbeitungen", {}) or {}
    taschen = []          # (bid, Fläche, Tiefe)
    konturen = []         # (bid, kurz, Seite, Fräser-Ø, Aufmass, seite-Wahl, Objekte)

    # --- Reihenfolge: ausgeschnitten wird zuletzt (die Liste gilt so, wie sie ist)
    liste = [b for b in auftrag.get("bearbeitungen", []) if b.get("objekte") or b.get("art") == "planen"]
    for i, b in enumerate(liste):
        if b.get("art") != "kontur" or float(b.get("tiefe") or 0) < dicke - 1e-6:
            continue
        seite = int(b.get("auf_seite") or 1)
        spaeter = [x for x in liste[i + 1:] if int(x.get("auf_seite") or 1) == seite and x.get("art") != "kontur"]
        if spaeter:
            befunde.append(_befund("warnung", "%s schneidet ganz durch, bevor %s gefräst ist – das Teil kann "
                                   "sich lösen. «Ordnen» setzt die Kontur ans Ende."
                                   % (b.get("name", "").split(" (")[0], spaeter[0].get("name", "").split(" (")[0]),
                                   None, b.get("id"), seite))

    for b in auftrag.get("bearbeitungen", []):
        bid, art, name = b.get("id"), b.get("art"), b.get("name", "")
        kurz = name.split(" (")[0]
        w = werkzeuge.get(b.get("werkzeug"), {})
        objekte = b.get("objekte", [])
        tiefe = float(b.get("max_tiefe") if art == "vcarve" else b.get("tiefe") or 0)

        # --- Geometrie
        if art in ("tasche", "kontur", "vcarve"):
            for o in objekte:
                if o.get("geschlossen"):
                    continue
                pts = o["punkte"]
                luecke = math.dist(pts[0], pts[-1])
                if art == "kontur" and luecke > 0.5:
                    continue                    # offene Linie: wird auf der Linie gefräst
                text = (f"{kurz}: offene Kontur (Lücke {_z(luecke, 2)} mm) – wird nicht gefräst. "
                        "Mit dem Knotenwerkzeug die Enden verbinden.")
                befunde.append(_befund("warnung", text, pts[0], bid, objekte=[o.get("eid")]))
            flaechen = _flaechen(objekte)
            eid_von = {id(f): e for f, e in zip(flaechen, _eids(objekte))}
            for f in flaechen:
                if not f.is_valid:
                    from shapely.validation import explain_validity
                    ort = None
                    try:
                        teile = explain_validity(f).split("[")[1].split("]")[0].split()
                        ort = (float(teile[0]), float(teile[1]))
                    except (IndexError, ValueError):
                        ort = f.representative_point().coords[0]
                    befunde.append(_befund("warnung", f"{kurz}: Form schneidet sich selbst – "
                                           "das Ergebnis kann anders aussehen als gedacht.", ort, bid,
                                           objekte=[eid_von.get(id(f))]))
        # doppelte Linien
        gesehen = []
        for o in objekte:
            ln = LineString(o["punkte"]) if len(o["punkte"]) >= 2 else None
            if ln is None or ln.length < 0.5:
                continue
            for g in gesehen:
                if abs(g.length - ln.length) < 0.1 and _hausdorff(g, ln) < 0.05:
                    befunde.append(_befund("warnung", f"{kurz}: Linie liegt doppelt übereinander – "
                                           "wird zweimal gefräst.", ln.coords[0], bid))
                    break
            else:
                gesehen.append(ln)

        # --- Tiefe
        sl = float(w.get("schneidenlaenge") or 0)
        if sl > 0 and tiefe > sl + 1e-6:
            d_s, d_w = float(w.get("schaft") or 0), float(w.get("durchmesser") or 0)
            if d_s > d_w + 0.05 and w.get("typ") != "v":
                befunde.append(_befund("fehler", f"{kurz}: {_z(tiefe)} mm tief, die Schneide ist aber nur "
                                       f"{_z(sl)} mm lang – darüber ist der Schaft Ø {_z(d_s)} mm dicker als die "
                                       f"Schneide (Ø {_z(d_w)} mm) und streift die Wand.", None, bid))
                befunde[-1]["aktion"] = "werkzeug"
            else:
                befunde.append(_befund("fehler", f"{kurz}: {_z(tiefe)} mm tief, die Schneide ist aber nur "
                                       f"{_z(sl)} mm lang – der Schaft würde reiben.", None, bid))
                befunde[-1]["aktion"] = "werkzeug"
        if art != "kontur" and not b.get("passloecher") and not b.get("durch") and tiefe > dicke + 1e-6:
            befunde.append(_befund("fehler", f"{kurz}: {_z(tiefe)} mm tief, das Material ist nur "
                                   f"{_z(dicke)} mm dick.", None, bid))
        if art == "kontur" and tiefe > dicke + 1.0:
            befunde.append(_befund("warnung", f"{kurz}: schneidet {_z(tiefe - dicke, 1)} mm in die "
                                   "Opferplatte.", None, bid))
        empf = (w.get("empfehlung") or {})
        zust = float(b.get("zustellung") or 0)
        if art in ("kontur", "tasche") and empf.get("zustellung") and zust > 1.5 * empf["zustellung"]:
            befunde.append(_befund("warnung", f"{kurz}: Zustellung {_z(zust)} mm – für dieses Material "
                                   f"und Werkzeug sind etwa {_z(empf['zustellung'])} mm empfohlen "
                                   "(Vollnut).", None, bid))

        # --- Vorschub in der Zeitleiste schneller gezogen als empfohlen
        st_b = statistik.get(bid) or {}
        f_b = float(b.get("vorschub_faktor") or 100)
        if f_b > 100 and st_b.get("vorschub_empf") and st_b.get("vorschub", 0) > 1.2 * st_b["vorschub_empf"]:
            befunde.append(_befund("warnung", f"{kurz}: Vorschub {f_b:.0f} % ({st_b['vorschub']:.0f} mm/min) – "
                                   f"deutlich über den empfohlenen {st_b['vorschub_empf']:.0f} mm/min für "
                                   "Werkzeug und Material.", None, bid))

        # --- Teile beim Ausschneiden
        if art == "kontur" and tiefe >= dicke - 1e-6:
            flaechen = _flaechen(objekte)
            if flaechen and b.get("stege_modus") == "manuell":
                # Stege von Hand: welche Konturen haben keine Markierung in der Nähe?
                from .ops.kontur import STEG_FANG
                seite = int(b.get("auf_seite") or 1)
                st_seite = auftrag.get("stege_seite") or []
                st_zu = auftrag.get("stege_bearbeitung") or []
                punkte = [Point(p[0], p[1]) for i, p in enumerate(auftrag.get("stege") or [])
                          if (int(st_seite[i]) if i < len(st_seite) else 1) == seite
                          and not (i < len(st_zu) and st_zu[i] and str(st_zu[i]) != str(bid))]
                r = float(w.get("durchmesser") or 3) / 2
                fang = r + abs(float(b.get("aufmass") or 0)) + STEG_FANG
                ohne = [(f, e) for f, e in zip(flaechen, _eids(objekte))
                        if not any(f.exterior.distance(p) <= fang for p in punkte)]
                if ohne:
                    n = len(ohne)
                    befunde.append(_befund(
                        "warnung", (f"{kurz}: 1 Kontur ohne Haltesteg – Teile können sich lösen. Klick zeigt sie." if n == 1 else
                         f"{kurz}: {n} Konturen ohne Haltesteg – Teile können sich lösen. Klick zeigt sie."), ohne[0][0].exterior.coords[0], bid,
                        objekte=[e for _f, e in ohne]))
            elif flaechen and b.get("stege_modus") == "keine":
                pass        # bewusst ohne Stege (Peter, 22.10.): «kleines Teil – gut festhalten» bleibt
            elif flaechen and int(b.get("stege_anzahl") or 0) == 0:
                befunde.append(_befund("warnung", f"{kurz}: ganz durch, aber ohne Haltestege – "
                                       "Teile können sich lösen.", None, bid, objekte=_eids(objekte)))
            eid_teil = {id(f): e for f, e in zip(flaechen, _eids(objekte))}
            for f in flaechen:
                x0, y0, x1, y1 = f.bounds
                if min(x1 - x0, y1 - y0) < KLEINES_TEIL and f.area > 1:
                    befunde.append(_befund("warnung", f"{kurz}: kleines Teil ({x1 - x0:.0f} × "
                                           f"{y1 - y0:.0f} mm) – gut festhalten (Haltestege, Klebeband).",
                                           f.representative_point().coords[0], bid,
                                           objekte=[eid_teil.get(id(f))]))
        if art == "kontur" and objekte:
            konturen.append((bid, kurz, int(b.get("auf_seite") or 1), float(w.get("durchmesser") or 0),
                             float(b.get("aufmass") or 0), b.get("seite") or "auto", objekte))
        if art == "tasche":
            fl = unary_union(_flaechen(objekte)) if objekte else None
            if fl is not None and not fl.is_empty:
                taschen.append((bid, fl, tiefe, int(b.get("auf_seite") or 1)))

        # --- Fräser ohne Mittenschnitt
        st = statistik.get(bid, {})
        if w and not w.get("mittenschnitt", True) and st.get("senkrecht"):
            befunde.append(dict(_befund("fehler", f"{kurz}: Fräser ohne Mittenschnitt taucht "
                                        f"{st['senkrecht']}× senkrecht ein – Rampe einschalten oder "
                                        "anderen Fräser wählen.", None, bid), nur_speichern=True))

    # --- Teile zu nah beieinander: der Fräser einer Kontur schneidet in ein anderes Teil (Peter, 22.10.:
    # «Die zweite Fräsung würde das erste Teil kaputt machen»)
    befunde.extend(_teile_zu_nah(konturen))

    # --- dünne Wände zwischen Taschen
    for i in range(len(taschen)):
        for j in range(i + 1, len(taschen)):
            if taschen[i][3] != taschen[j][3]:
                continue
            a, b_ = taschen[i][1], taschen[j][1]
            d = a.distance(b_)
            if 1e-6 < d < WANDSTAERKE:
                from shapely.ops import nearest_points
                p, q = nearest_points(a, b_)
                befunde.append(_befund("warnung", f"Wand zwischen {namen[taschen[i][0]].split(' (')[0]} "
                                       f"und {namen[taschen[j][0]].split(' (')[0]} nur {_z(d, 2)} mm dick.",
                                       ((p.x + q.x) / 2, (p.y + q.y) / 2), None))

    # --- zweiseitig: Taschen von beiden Seiten am selben Ort – wie viel Boden bleibt?
    flaeche = auftrag.get("material_flaeche")
    if flaeche and any(t[3] == 2 for t in taschen):
        from shapely.affinity import affine_transform
        breite, hoehe = float(flaeche[0]), float(flaeche[1])
        um_y = (auftrag.get("zweiseitig") or {}).get("achse") == "y"
        # Seite 2 in Koordinaten von Seite 1: gespiegelt an der Wendeachse
        spiegel = [-1, 0, 0, 1, breite, 0] if um_y else [1, 0, 0, -1, 0, hoehe]
        for b1, f1, t1, s1 in taschen:
            if s1 != 1:
                continue
            for b2, f2, t2, s2 in taschen:
                if s2 != 2:
                    continue
                schnitt = f1.intersection(affine_transform(f2, spiegel))
                if schnitt.is_empty or schnitt.area < 0.01:
                    continue
                rest = dicke - t1 - t2
                ort = schnitt.representative_point().coords[0]
                n1, n2 = namen[b1].split(" (")[0], namen[b2].split(" (")[0]
                if rest <= 1e-6:
                    befunde.append(_befund("fehler", f"{n1} (Seite 1) und {n2} (Seite 2) treffen sich – "
                                           "dort entsteht ein Loch.", ort, None))
                elif rest < BODEN_ZWEISEITIG:
                    befunde.append(_befund("warnung", f"Zwischen {n1} (Seite 1) und {n2} (Seite 2) bleiben "
                                           f"nur {_z(rest, 1)} mm Material.", ort, None))

    # --- Fräswege: Kollision der Eilgänge mit dem Material, wie es in dem Moment steht
    from .post import tap_mm

    from . import engine as _engine
    gelesen: Dict[str, object] = {}

    def _lesen(datei):
        # Fräswege der eben gelaufenen Rechnung aus dem Speicher, sonst die Datei (einmal)
        job = gelesen.get(datei)
        merk = _engine.letzte_jobs.get(datei)
        if job is None and merk is not None:
            try:
                if os.path.getsize(datei) == merk[1]:      # Datei noch so, wie geschrieben
                    job = merk[0]
            except OSError:
                pass
        if job is None:
            with open(datei, newline="") as fh:
                job = gelesen[datei] = tap_mm.read(fh.read())
        return job
    xs, ys = [], []
    flaeche_m = auftrag.get("material_flaeche")
    if flaeche_m:
        try:
            from .kollision import pruefen as kollisionen
            for k in kollisionen(erg.get("programme", []),
                                 {"breite": float(flaeche_m[0]), "hoehe": float(flaeche_m[1]), "dicke": dicke,
                                  "rund": auftrag.get("rund")},
                                 _lesen, erg.get("gewinde") or ()):
                if k.get("art") == "gewinde":
                    if k.get("nichts"):
                        # vielleicht von Hand vorgebohrt – dann stimmt es; sonst bricht der Gewindefräser
                        text = (f"{k['gewinde']}: kein Kernloch im Programm – vorher bohren (Bearbeitung "
                                "«Bohren» oder beim Gewinde ein Werkzeug für das Kernloch wählen), sonst taucht der "
                                "Gewindefräser ins volle Material." if k.get("innen", True) else
                                f"{k['gewinde']}: um den Zapfen ist nichts freigefräst – der Gewindefräser taucht "
                                "neben dem Zapfen ins volle Material.")
                        # Peter 06.11.: nur beim Speichern melden (Endprüfung, dokument.rechnen)
                        befunde.append(dict(_befund("warnung", text, k["ort"], k.get("bearbeitung"), k["seite"]),
                                            nur_speichern=True))
                    else:
                        text = (f"{k['gewinde']}: Kernloch zu flach oder zu klein ({_z(k['tief'], 1)} mm fehlen) – "
                                "der Gewindefräser taucht ins Material." if k.get("innen", True) else
                                f"{k['gewinde']}: neben dem Zapfen ist es {_z(k['tief'], 1)} mm zu wenig tief "
                                "freigefräst – der Gewindefräser taucht ins Material.")
                        befunde.append(_befund("fehler", text, k["ort"], k.get("bearbeitung"), k["seite"]))
                    continue
                if k.get("art") == "schraube":
                    befunde.append(_befund("fehler", f"{k['programm']}: Fräser trifft den Schraubenkopf "
                                           f"({_z(k['tief'], 1)} mm tief) – Fräser und Schraube in Gefahr.",
                                           k["ort"], None, k["seite"]))
                    continue
                befunde.append(_befund("fehler", f"{k['programm']}: Eilgang trifft Material "
                                       f"({_z(k['tief'], 1)} mm tief) – Fräser oder Werkstück in Gefahr.",
                                       k["ort"], None, k["seite"]))
        except ImportError:
            pass
    for p in erg.get("programme", []):
        try:
            job = _lesen(p["datei"])
        except (OSError, ValueError):
            continue
        for tp in job.toolpaths:
            for m in tp.moves:
                xs.append(m.x)
                ys.append(m.y)
    # --- Spannmittel: schneidet ein Fräsweg (mit Fräserradius) hinein?
    spann = auftrag.get("spannmittel") or []
    if spann:
        try:
            from shapely.geometry import LineString, Point, Polygon
            flaechen = []
            seiten_sm = [int(sm.get("seite") or 1) for sm in spann]
            # Umspannen: Spannmittel gelten nur für ihre Spannung (ohne Umspannen: alle für das eine Programm)
            spannung_sm = [2 if int(sm.get("spannung") or 1) == 2 else 1 for sm in spann]
            mit_umspannen = bool((auftrag.get("umspannen") or {}).get("an"))
            for sm in spann:
                p_ = Polygon(sm["punkte"])
                flaechen.append((p_ if p_.is_valid else p_.buffer(0), float(sm.get("hoehe", 20))))
            for p in erg.get("programme", []):
                try:
                    job = _lesen(p["datei"])
                except (OSError, ValueError):
                    continue
                r = float(p.get("durchmesser") or 0) / 2
                seite_p = int(p.get("seite") or 1)
                spannung_p = int(p.get("spannung") or 1)
                vorher = (job.home[0], job.home[1], job.safe_z)
                gemeldet = set()
                for tp in job.toolpaths:
                    for m in tp.moves:
                        a, b = vorher, (m.x, m.y, m.z)
                        vorher = b
                        if math.dist(a[:2], b[:2]) < 1e-9 and abs(a[2] - b[2]) < 1e-9:
                            continue
                        weg = LineString([a[:2], b[:2]]) if math.dist(a[:2], b[:2]) > 1e-9 else None
                        for i, (f, h) in enumerate(flaechen):
                            if i in gemeldet or seiten_sm[i] != seite_p or min(a[2], b[2]) >= h:
                                continue
                            if mit_umspannen and spannung_sm[i] != spannung_p:
                                continue
                            ziel = f.buffer(r)
                            treffer = ziel.intersects(weg) if weg is not None else ziel.contains(Point(b[:2]))
                            if treffer:
                                gemeldet.add(i)
                                ort = f.representative_point()
                                befunde.append(_befund("fehler", f"{p['name']}: fährt in ein Spannmittel "
                                                       f"({'Eilgang' if m.rapid else 'Fräsweg'}).",
                                                       (ort.x, ort.y), None, seite_p))
                                # Peter 09.11.: gleich «Umspannen einschalten» anbieten (nur solange es aus ist)
                                if not mit_umspannen:
                                    befunde[-1]["aktion"] = "umspannen"
            sicher = float(auftrag.get("hoehen", {}).get("sicherheitshoehe") or 20)
            hoch = max(h for _f, h in flaechen)
            if hoch + 2 > sicher:
                befunde.append(_befund("warnung", f"Spannmittel ({_z(hoch)} mm) ragen bis an die Sicherheitshöhe "
                                       f"({_z(sicher)} mm) – Sicherheitshöhe erhöhen.", None, None))
        except ImportError:
            pass

    if xs and maschine:
        mx, my = float(maschine.get("x", 1e9)), float(maschine.get("y", 1e9))
        # Wo das Material auf dem Tisch liegt, weiss nur der Nullpunkt an der
        # Maschine – geprüft wird, ob die Fräswege überhaupt Platz haben
        bx, by = max(xs) - min(xs), max(ys) - min(ys)
        if bx > mx + 1e-6 or by > my + 1e-6:
            befunde.append(_befund("fehler", f"Fräsbahnen sind grösser als der Verfahrweg der Maschine "
                                   f"(X {_z(mx)}, Y {_z(my)} mm): {bx:.0f} × {by:.0f} mm.", None, None))
        mz = float(maschine.get("z", 1e9))
        sicher = float(auftrag.get("hoehen", {}).get("sicherheitshoehe") or 20)
        if dicke + sicher > mz:
            befunde.append(_befund("warnung", f"Material ({_z(dicke)} mm) und Sicherheitshöhe "
                                   f"({_z(sicher)} mm) sind höher als der Z-Weg ({_z(mz)} mm).", None, None))
    seiten = {b.get("id"): int(b.get("auf_seite") or 1) for b in auftrag.get("bearbeitungen", [])}
    for b in befunde:
        if b["seite"] is None:
            b["seite"] = seiten.get(b["bearbeitung"], 1)
    befunde.extend(restmaterial_befunde(auftrag, erg))
    befunde.extend(schneide_3d_befunde(auftrag, erg))
    befunde.sort(key=lambda b: b["stufe"] != "fehler")
    return befunde


SCHNEIDE_3D = ("{}: fräst bis {} mm unter der Oberfläche, die Schneide von Ø {} mm ist aber nur {} mm lang – an "
               "engen Stellen und an steilen Wänden reibt dort der Schaft.")


def _tiefste_je_bearbeitung(erg: Dict) -> Dict[str, float]:
    """Je Bearbeitung die tiefste Z ihrer Bahnen (Vorschau-Dateien, Abschnitte nach Startzeile)."""
    import re
    zahl = re.compile(r"Z(-?\d+(?:\.\d+)?)")
    aus: Dict[str, float] = {}
    for p in erg.get("programme") or []:
        try:
            with open(p["datei"], newline="") as fh:
                zeilen = fh.read().splitlines()
        except (OSError, KeyError, TypeError):
            continue
        ab = sorted(p.get("abschnitte") or [], key=lambda a: int(a.get("zeile") or 0))
        for i, a in enumerate(ab):
            von = max(0, int(a.get("zeile") or 1) - 1)
            bis = int(ab[i + 1].get("zeile") or len(zeilen) + 1) - 1 if i + 1 < len(ab) else len(zeilen)
            zs = [float(m) for z in zeilen[von:bis] for m in zahl.findall(z)]
            if not zs:
                continue
            bid = str(a.get("key") or "").split("@")[0].partition("#")[0]
            aus[bid] = min(aus.get(bid, 0.0), min(zs))
    return aus


def schneide_3d_befunde(auftrag: Dict, erg: Dict) -> List[Dict]:
    """Impeller 08.11.: Ø 3 mit 12 mm Schneide fräste «nur Restmaterial» 26 mm tief – keine Meldung, weil 3D-
    Bearbeitungen keine eingestellte Tiefe haben. Jetzt zählt die tiefste Stelle ihrer Bahnen."""
    aus = []
    werkzeuge = auftrag.get("werkzeuge", {})
    tiefst = None
    for b in auftrag.get("bearbeitungen", []):
        art = b.get("art") or ""
        if art not in ("relief_schruppen", "relief_schlichten", "boeden_waende"):
            continue
        w = werkzeuge.get(b.get("werkzeug"), {})
        sl = float(w.get("schneidenlaenge") or 0)
        if sl <= 0:
            continue
        if tiefst is None:
            tiefst = _tiefste_je_bearbeitung(erg)
        z = tiefst.get(str(b.get("id")))
        if z is None or -z <= sl + 0.05:
            continue
        bf = _befund("warnung", SCHNEIDE_3D.format(b.get("name", "").split(" (")[0], _z(-z, 1),
                                                    _z(float(w.get("durchmesser") or 0)), _z(sl)),
                     None, b.get("id"), int(b.get("auf_seite") or 1))
        bf["aktion"] = "werkzeug"
        aus.append(bf)
    return aus


REST_HINWEIS = "Restmaterial: Der Fräser Ø"


def restmaterial_befunde(auftrag: Dict, erg: Dict) -> List[Dict]:
    """Restmaterial nach dem 3D-Schruppen (relief3d._rest_hinweis, Peter 25.10.): als Hinweis mit Knopf
    «Restmaterial mit Ø … anlegen» (aktion «restmaterial», Panel)."""
    import re
    aus = []
    arten = {b.get("id"): b for b in auftrag.get("bearbeitungen", [])}
    for bid, texte in (erg.get("op_warnungen") or {}).items():
        b = arten.get(bid) or {}
        if b.get("art") != "relief_schruppen":
            continue
        for t in texte:
            if not t.startswith(REST_HINWEIS):
                continue
            m = re.search(r"Schaftfräser Ø ([0-9]+(?:,[0-9]+)?)", t)
            bf = _befund("warnung", "%s: %s" % (b.get("name", ""), t), None, bid,
                         int(b.get("auf_seite") or 1))
            if m:
                bf["aktion"] = "restmaterial"
                bf["durchmesser"] = float(m.group(1).replace(",", "."))
            aus.append(bf)
    return aus
