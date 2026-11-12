# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Schnittstelle für das Fräsen-Panel von Hirch CAM.

    python3 -m hirch_cam.dokument analyse ZEICHNUNG.svg
        Material, Einstellungen (ergänzt) und Objekte je Bearbeitung – schnell,
        ohne Werkzeugwege.

    python3 -m hirch_cam.dokument rechnen ZEICHNUNG.svg --ordner DIR [--name STAMM]
        Werkzeugwege rechnen und die .tap-Dateien nach DIR schreiben.

Ausgabe immer als JSON auf stdout.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import sys

from . import einstellungen as einst


def _abgleich_stand() -> dict:
    """Werkzeugkatalog mit einem Ordner abgleichen (Einstellungen › Maschine): an, Ordner, letzte Meldung."""
    try:
        from . import werkzeugabgleich
        e = werkzeugabgleich.einstellung()
        return {k: e[k] for k in ("an", "ordner", "zuletzt", "meldung", "anzahl", "fehler") if k in e}
    except Exception:
        return {}

def _laden(pfad):
    import inkex
    return inkex.load_svg(pfad).getroot()


def _abgleichen(e: dict) -> list:
    """Werkzeuge der Zeichnung mit «Meine Werkzeuge» abgleichen (siehe werkzeugdb.abgleichen)."""
    from . import werkzeugdb
    try:
        e["werkzeuge"], hinweise = werkzeugdb.abgleichen(e.get("werkzeuge") or {})
        return hinweise
    except Exception:            # nie das Öffnen oder Rechnen verhindern
        return []


def analyse(pfad: str, platte: int = 1) -> dict:
    from .svg_lesen import hinweise, lesen
    svg = _laden(pfad)
    e = einst.laden(svg.get(einst.ATTRIBUT))
    einst.vorgaben_ohne_nutzen_weg(e)          # unbenutzte, nie geänderte Vorgabe-Fräser nicht mitschleppen
    wz_hinweise = _abgleichen(e)
    info = lesen(svg, mit_linien=True, zweiseitig=einst.zweiseitig_an(e), platte=platte)
    if info.get("platte") == 0:
        info["dicke"] = float(einst.vorrichtung(e).get("dicke") or 19)
    einst.ergaenzen(e, {(el["art"], el.get("seite", 1)) for el in info["elemente"] if el["art"]})
    zu = einst.zuordnen(e, info["elemente"])
    ids = {}
    for bid, els in zu.items():
        ids[bid] = list(dict.fromkeys(el["id"] for el in els if el["id"]))
    anzahl = {bid: len(v) for bid, v in ids.items()}
    anzahl["aus"] = len(info["ids"].get("aus", []))
    zusatz = einst.zusaetzlich(e, info["elemente"])
    from .post import liste as post_liste
    from .farben import OHNE_OBJEKTE, schattierung
    from . import werkzeugdb
    db = werkzeugdb.laden()
    material = e.get("material", werkzeugdb.STANDARD_MATERIAL)
    from .engine import werkzeug_name

    def _wz_name(w):
        try:
            return werkzeug_name(w, True)
        except Exception:
            return ""
    pl = [list(p) for p in einst.passloecher(e, info["breite"], info["hoehe"])] \
        if einst.zweiseitig_an(e) and (e.get("zweiseitig") or {}).get("passloecher") and not einst.rund_an(e) \
        else []                                     # rund: zentriert die Schraube, keine Passlöcher
    pl_befunde = passloecher_befunde(e, info, pl)
    pl_warn = [b["index"] for b in pl_befunde]
    pl_hinweise = passloch_hinweise(pl_befunde)
    kennung = rechen_kennung(e, info, db, platte)
    return {"ok": True,
            "werkzeug_hinweise": wz_hinweise,
            "kennung": kennung,
            "werkzeug_namen": {k: _wz_name(w) for k, w in (e.get("werkzeuge") or {}).items()},
            # Schnittdaten der Zeichnung und der Fräser aus «Meine Werkzeuge» für ihren Werkstoff
            **werkzeugdb.wirksam_alle(e["werkzeuge"], material, db),
            "datenbank": db["werkzeuge"], "maschine": db["maschine"],
            "abgleich": _abgleich_stand(),
            "materialien": [[k, v["name"]] for k, v in werkzeugdb.materialien().items()],
            # auch die Bearbeitungen ohne Objekte (3D, Planen): «Duplizieren» gibt der Kopie einen eigenen Ton (B-23)
            "farbtoene": {art: [schattierung(art, n) for n in range(6)]
                          for art in list(einst.ARTEN) + list(OHNE_OBJEKTE)},
            "postprozessoren": post_liste(auch=e.get("postprozessor", "")),
            "material": {"breite": info["breite"], "hoehe": info["hoehe"],
                         "dicke": info["dicke"]},
            "einstellungen": e, "namen": einst.namen(e), "anzahl": anzahl, "ids": ids,
            "zusaetzlich": zusatz,
            "hinweise": wz_hinweise + hinweise(info) + pl_hinweise,
            "spannmittel": len(info.get("spannmittel", [])),
            "stege": len(info.get("stege", [])),
            "seiten": info.get("seiten", []),
            "platte": info.get("platte", 1), "platten": info.get("platten", 1),
            "vorrichtung": _vorrichtung_info(svg, e, info),
            "nullpunkte": nullpunkte(svg, e, info),
            "passloecher": pl,
            # Werkzeug der Passlöcher (gewählt oder passend vorgeschlagen – Dialog «Auftrag einrichten»)
            "pass_werkzeug": einst.pass_werkzeug(e, e.get("werkzeuge") or {}, float(info.get("dicke") or 0) + float(
                (e.get("zweiseitig") or {}).get("pass_extra") or 3.0)),
            "passloecher_warnung": pl_warn,
            "ablauf": _ablauf_vorab(e, zu, info),
            "gewinde_kreise": _gewinde_kreise(e, zu),
            "umrisse": _umrisse(e, info) if einst.zweiseitig_an(e) else {}}


def _gewinde_kreise(e, zu) -> dict:
    """Je Gewinde-Bearbeitung die gezeichneten Durchmesser (Nenn-Ø, für die Anleitung im Dialog)."""
    aus = {}
    for b in e.get("bearbeitungen") or []:
        if b.get("art") != "gewinde":
            continue
        ds = []
        for el in zu.get(b.get("id"), []):
            pts = [q for li in (el.get("linien") or []) for q in (li.get("punkte") or [])] or el.get("punkte") or []
            try:
                xs = [float(q[0]) for q in pts]
                ys = [float(q[1]) for q in pts]
            except (TypeError, ValueError, IndexError):
                continue
            if xs and ys:
                d = round(((max(xs) - min(xs)) + (max(ys) - min(ys))) / 2, 2)
                if d > 0 and d not in ds:
                    ds.append(d)
        aus[b["id"]] = sorted(ds)
    return aus


def _grundplatte(svg, e, info) -> dict:
    """Rundes Material, beim Rechnen der Spannvorrichtung (Platte 0): wo das Teil auf ihr steht
    (Mitte in Koordinaten der Vorrichtung, Ø) – sonst None."""
    if info.get("platte") != 0 or not einst.rund_an(e) or not einst.vorrichtung_an(e):
        return None
    from .svg_lesen import seiten
    s1 = seiten(svg)[0]
    vs = info.get("vorrichtung_seite") or {}
    lx, ly = einst.vorrichtung_lage(e, (vs.get("breite", 0), vs.get("hoehe", 0)), (s1["breite"], s1["hoehe"]))
    return {"mitte": [lx + s1["breite"] / 2, ly + s1["hoehe"] / 2],
            "durchmesser": min(s1["breite"], s1["hoehe"])}


def nullpunkt_platte(svg, e, info, platte: int) -> dict:
    """Der eine Nullpunkt (Peter, 21.10.: «ein Nullpunkt», gehört zum Werkstück) in Materialkoordinaten der
    Platte (mm, von unten links): auf den Werkstück-Platten wie gewählt am Werkstück (rund: die Achse), auf der
    Spannvorrichtung (Platte 0) derselbe Maschinenpunkt = Lage des Werkstücks + Nullpunkt am Werkstück."""
    if platte != 0:
        return einst.nullpunkt(e, info["breite"], info["hoehe"])
    from .svg_lesen import seiten
    s1 = seiten(svg)[0]
    np_w = einst.nullpunkt(e, s1["breite"], s1["hoehe"])
    vs = info.get("vorrichtung_seite")
    if vs is None or not einst.vorrichtung_an(e):
        return np_w
    lx, ly = einst.vorrichtung_lage(e, (vs["breite"], vs["hoehe"]), (s1["breite"], s1["hoehe"]))
    return {"x": lx + np_w["x"], "y": ly + np_w["y"], "z_unten": np_w["z_unten"]}


def _nullpunkt_seiten(info, mit_kamera: bool = True) -> dict:
    """Provisorischer Nullpunkt (oranges Kreuz, Peter 29.10.) je Seite: {seite: {"x", "y"[, "kamera"]}} in mm der
    Seite (von unten links). Gilt nur für die Seite, auf der das Kreuz liegt; Z0 bleibt wie gewählt.
    Kamera-Versatz [dx, dy] (Fräser gegen Kamera, an der Maschine abgelesen): X0 Y0 wird mit der Kamera auf dem
    Kreuz gesetzt, der Fräser steht dann um den Versatz daneben – für den G-Code liegt der Nullpunkt darum bei
    Kreuz + Versatz (mit_kamera). Die Zeichenfläche zählt ab dem Kreuz (mit_kamera=False)."""
    if info.get("platte") == 0:
        return {}
    kam = info.get("nullmarken_kamera") or {}
    aus = {}
    for k, v in (info.get("nullmarken") or {}).items():
        dx, dy = (kam.get(k) or [0.0, 0.0]) if mit_kamera else (0.0, 0.0)
        aus[int(k)] = {"x": round(float(v[0]) + dx, 6), "y": round(float(v[1]) + dy, 6)}
        if k in kam:
            aus[int(k)]["kamera"] = [round(float(kam[k][0]), 6), round(float(kam[k][1]), 6)]
            aus[int(k)]["kreuz"] = [round(float(v[0]), 6), round(float(v[1]), 6)]
    return aus


def nullpunkte(svg, e, info) -> list:
    """Für die Zeichenfläche (Kreuz, Lineale, «Anordnen»): der Nullpunkt auf jeder Seite der Platte und auf der
    Vorrichtung, in Dokument-mm (von links oben, wie die Seiten). Dieselben Zahlen wie im G-Code.
    z: "oben" | "unten" – wo Z0 liegt (Oberseite oder Unterseite des Materials).
    seite_mm: [links, oben, breite, hoehe] – die Seite, auf der dieser Nullpunkt gilt (Dokument-mm)."""
    aus = []
    platte = info.get("platte", 1)
    z = "unten" if einst.nullpunkt(e, 1.0, 1.0)["z_unten"] else "oben"     # Z0 an der Ober- oder Unterseite
    werkstueck = info.get("seiten") or []
    if platte == 0:
        # beim Rechnen der Vorrichtung zeigen die Werkstück-Seiten (Platte 1) trotzdem ihren Nullpunkt
        from .svg_lesen import seiten
        werkstueck = seiten(svg)
    prov = _nullpunkt_seiten(info, mit_kamera=False) if platte != 0 else {}
    for nr, s in enumerate(werkstueck, 1):
        np_m = einst.nullpunkt(e, s["breite"], s["hoehe"])
        if nr in prov:
            np_m = dict(np_m, x=prov[nr]["x"], y=prov[nr]["y"])
        aus.append({"platte": platte or 1, "seite": nr, "x": round(s["links"] + np_m["x"], 6),
                    "y": round(s["oben"] + s["hoehe"] - np_m["y"], 6), "z": z,
                    "seite_mm": [s["links"], s["oben"], s["breite"], s["hoehe"]],
                    "provisorisch": nr in prov, "kamera": (prov.get(nr) or {}).get("kamera")})
    vs = info.get("vorrichtung_seite")
    if vs is not None and einst.vorrichtung_an(e):
        nv = nullpunkt_platte(svg, e, info, 0)
        aus.append({"platte": 0, "seite": 1, "x": round(vs["links"] + nv["x"], 6),
                    "y": round(vs["oben"] + vs["hoehe"] - nv["y"], 6), "z": z,
                    "seite_mm": [vs["links"], vs["oben"], vs["breite"], vs["hoehe"]]})
    return aus


def _vorrichtung_anwenden(svg, e, info, a, platte: int) -> None:
    """Spannvorrichtung: Platte 0 ist die Vorrichtung (eigene Dicke) mit demselben Maschinen-Nullpunkt wie
    das Werkstück (`nullpunkt_platte`); die Werkstück-Platten bekommen die Wände der Aufnahmen als Spannmittel."""
    vs = info.get("vorrichtung_seite")
    if vs is None or not einst.vorrichtung_an(e):
        return
    a["nullpunkt"] = nullpunkt_platte(svg, e, info, platte)
    if platte == 0:
        return
    # Lage dieser Platte auf der Vorrichtung – mit ihrer eigenen Grösse (Platten können verschieden gross sein)
    lx, ly = einst.vorrichtung_lage(e, (vs["breite"], vs["hoehe"]), (info["breite"], info["hoehe"]))
    v = einst.vorrichtung(e)
    try:
        a["spannmittel"] = list(a.get("spannmittel") or []) + _waende(svg, e, info, v, lx, ly)
    except Exception:
        pass


def _waende(svg, e, info, v, lx, ly) -> list:
    """Wände der Taschen auf der Vorrichtung, soweit sie über das Werkstück ragen – als
    Spannmittel in Werkstück-Koordinaten (Ringe ohne Löcher)."""
    from shapely.geometry import LineString, Polygon
    from shapely.ops import split, unary_union
    from .svg_lesen import lesen
    vi = lesen(svg, zweiseitig=einst.zweiseitig_an(e), platte=0)
    zu = einst.zuordnen(e, vi["elemente"])
    nach = {b["id"]: b for b in e.get("bearbeitungen", [])}
    aus = []
    dicke_w = float(info["dicke"])
    for bid, els in zu.items():
        b = nach.get(bid) or {}
        if b.get("art") != "tasche" or not els:
            continue
        hoehe = float(b.get("tiefe") or 0) - dicke_w
        if hoehe <= 0.1:
            continue
        flaechen = []
        for el in els:
            for ln in el["linien"]:
                if ln["geschlossen"] and len(ln["punkte"]) >= 3:
                    pg = Polygon(ln["punkte"]).buffer(0)
                    if not pg.is_empty:
                        flaechen.append(pg)
        if not flaechen:
            continue
        tasche = unary_union(flaechen)
        band = tasche.buffer(6.0).difference(tasche)
        c = tasche.centroid
        gross = 1e5
        teile = [band]
        for linie in (LineString([(c.x, -gross), (c.x, gross)]), LineString([(-gross, c.y), (gross, c.y)])):
            neu = []
            for t in teile:
                try:
                    neu.extend(split(t, linie).geoms)
                except Exception:
                    neu.append(t)
            teile = neu
        for t in teile:
            for p in getattr(t, "geoms", [t]):
                if p.geom_type != "Polygon" or p.area < 0.5:
                    continue
                pts = [(x - lx, y - ly) for x, y in p.exterior.coords]
                aus.append({"punkte": pts, "hoehe": hoehe, "seite": 1})
    return aus


def _vorrichtung_info(svg, e, info) -> dict:
    """Für das Panel: Seite der Vorrichtung, Lage und Umrisse des Werkstücks (Platte 1)."""
    from .svg_lesen import lesen, vorrichtung_seite
    vs = vorrichtung_seite(svg)
    if vs is None:
        return {}
    v = einst.vorrichtung(e)
    w = lesen(svg, zweiseitig=einst.zweiseitig_an(e), platte=1)
    umrisse = []
    for el in w["elemente"]:
        if el.get("seite", 1) != 1:
            continue
        for ln in el["linien"]:
            pts = ln["punkte"]
            if len(pts) < 2:
                continue
            schritt = max(1, len(pts) // 200)
            umrisse.append([round(c, 2) for q in pts[::schritt] + [pts[-1]] for c in q])
    from .svg_lesen import seiten
    s1 = seiten(svg)[0]
    return {"links": vs["links"], "oben": vs["oben"], "breite": vs["breite"], "hoehe": vs["hoehe"],
            "werkstueck_seite": {"links": s1["links"], "oben": s1["oben"]},
            "lage": list(einst.vorrichtung_lage(e, (vs["breite"], vs["hoehe"]), (s1["breite"], s1["hoehe"]))),
            "mittig": einst.vorrichtung_mittig(e), "werkstueck": [w["breite"], w["hoehe"]],
            "umrisse": umrisse[:400], "an": bool(v.get("an")), "vorhanden": bool(v.get("vorhanden"))}


def _umrisse(e, info, abstand: float = 0.3) -> dict:
    """Zweiseitig: Umrisse aller Objekte je Seite (vereinfacht, mm in den Koordinaten ihrer
    Seite), dazu die Passlöcher auf Seite 1. Das Panel zeigt sie blass und gespiegelt auf der
    anderen Seite – auch Objekte ohne Bearbeitung (Peter, 22.10.: erst beide Seiten zeichnen
    und prüfen, ob alles übereinanderliegt, dann die Bearbeitungen)."""
    import math
    aus = {"1": [], "2": []}
    for el in list(info["elemente"]) + list(info.get("ohne_art") or []):
        ziel = aus["2" if int(el.get("seite") or 1) == 2 else "1"]
        for lin in el["linien"]:
            pts = lin["punkte"]
            if len(pts) < 2:
                continue
            zug = [pts[0]]
            for q in pts[1:-1]:
                if math.dist(q, zug[-1]) >= abstand:
                    zug.append(q)
            zug.append(pts[-1])
            ziel.append([round(v, 2) for q in zug for v in q])
    z = dict(einst.STANDARD["zweiseitig"])
    z.update(e.get("zweiseitig") or {})
    if z.get("passloecher") and not einst.rund_an(e):
        r = float(z.get("pass_d") or 6.0) / 2
        for x, y in einst.passloecher(e, info["breite"], info["hoehe"]):
            aus["1"].append([round(v, 2) for i in range(37)
                             for v in (x + r * math.cos(2 * math.pi * i / 36), y + r * math.sin(2 * math.pi * i / 36))])
    return aus


def rechen_kennung(e, info, db, platte) -> str:
    """Fingerabdruck von allem, was das Rechnen braucht: Einstellungen, Werkzeuge und Maschine,
    Material, Seiten und nur die Objekte mit einer Bearbeitung (samt Stegen und Spannmitteln).
    Ändert sich nur, was nicht gefräst wird (neue schwarze Linie, Text ohne Fräsfarbe, ein solches
    Objekt verschoben), bleibt die Kennung gleich und das Panel rechnet nicht neu (Peter, 21.10.).
    3D-Modelle (Peter, 21.10.): Daten des Modells (als Prüfsumme), Lage, Einstellungen des Bilds
    (Stauchung, Ausrichtung …), Seite und Haltestege gehören dazu; 3D rechnet trotzdem nur auf
    Knopfdruck – die Kennung sagt nur, ob «Rechnen» nötig ist."""
    import hashlib
    reliefs = []
    for r in info.get("reliefs") or []:
        reliefs.append({"id": r.get("id"), "info": r.get("info"), "ecken": r.get("ecken"),
                        "seite": r.get("seite"), "stege": r.get("stege"),
                        "daten": hashlib.sha1(str(r.get("daten") or "").encode("utf-8")).hexdigest()})
    try:
        teile = {
            "e": e, "db": db, "platte": platte,
            "material": [info.get("breite"), info.get("hoehe"), info.get("dicke")],
            "seiten": info.get("seiten"), "platten": info.get("platten"),
            "elemente": [[el.get("id"), el.get("art"), el.get("auch"), el.get("seite"), el.get("rgb"),
                          [[li["geschlossen"], li["punkte"]] for li in el.get("linien") or []]]
                         for el in info.get("elemente") or []],
            "stege": [info.get("stege"), info.get("stege_seite")],
            # provisorischer Nullpunkt (Kreuz) und Kamera-Versatz: setzen/verschieben → G-Code neu (Windows 29.10.)
            "nullmarken": [info.get("nullmarken"), info.get("nullmarken_kamera")],
            "spannmittel": info.get("spannmittel"),
            "vorrichtung": info.get("vorrichtung_seite"),
            "post": _post_stand(),
            "reliefs": reliefs,
        }
        roh = json.dumps(teile, sort_keys=True, ensure_ascii=False, default=str)
    except (TypeError, ValueError):
        return ""
    return hashlib.sha1(roh.encode("utf-8")).hexdigest()


def _post_stand() -> list:
    """Eigene Postprozessoren (Datei und Änderungszeit): geändert → neu rechnen."""
    try:
        from .post import benutzer_ordner
        ordner = benutzer_ordner()
        return sorted((n, os.path.getmtime(os.path.join(ordner, n))) for n in os.listdir(ordner))
    except Exception:
        return []


PASS_ABSTAND = 3.0      # mm Rand zwischen Passloch und Teil bzw. Materialrand


def passloecher_pruefen(e, info, pl) -> list:
    """Indizes der Passlöcher, die zu nah an einem Teil (beide Seiten) oder am Materialrand liegen."""
    return [b["index"] for b in passloecher_befunde(e, info, pl)]


def _mm_text(v: float) -> str:
    return ("%.1f" % max(0.0, v)).rstrip("0").rstrip(".").replace(".", ",")


def passloch_hinweise(befunde: list) -> list:
    """Je Paar ein Satz mit der Nummer wie auf der Zeichnung («1» bzw. «1′»), das engste Loch des Paares zählt."""
    je_paar = {}
    for b in befunde:
        alt = je_paar.get(b["paar"])
        if alt is None or b["luecke"] < alt["luecke"]:
            je_paar[b["paar"]] = b
    aus = []
    for paar in sorted(je_paar, key=lambda k: (k == 0, k)):
        b = je_paar[paar]
        name = ("Passloch " + b["nummer"]) if b["nummer"] else "Ein Passloch"
        mind = _mm_text(PASS_ABSTAND)
        if b["grund"] == "rand":
            aus.append(("%s ragt über den Materialrand – Paar verschieben." % name) if b["luecke"] <= 0 else
                       "%s liegt %s mm vom Materialrand (mindestens %s mm) – Paar verschieben."
                       % (name, _mm_text(b["luecke"]), mind))
        else:
            aus.append(("%s überschneidet ein Teil – Paar verschieben." % name) if b["luecke"] <= 0 else
                       "%s liegt %s mm neben einem Teil (mindestens %s mm) – Paar verschieben."
                       % (name, _mm_text(b["luecke"]), mind))
    return aus


def passloecher_befunde(e, info, pl) -> list:
    """Passlöcher zu nah an einem Teil (beide Seiten) oder am Materialrand: [{"index", "nummer" («2» / «2′»),
    "paar" (Nummer des Paares, 0 = unbekannt), "grund": "teil"|"rand", "luecke": mm zwischen Lochrand und Teil
    bzw. Materialrand}] in der Reihenfolge von pl."""
    if not pl:
        return []
    z = dict(einst.STANDARD["zweiseitig"])
    z.update(e.get("zweiseitig") or {})
    r = float(z.get("pass_d") or 6.0) / 2
    B, H = info["breite"], info["hoehe"]
    um_y = z.get("achse") == "y"
    zuege = []
    for el in info.get("elemente") or []:
        if not el.get("art"):
            continue
        for li in el.get("linien") or []:
            pts = li["punkte"]
            if el.get("seite", 1) == 2:     # Seite 2 liegt nach dem Wenden gespiegelt über Seite 1
                pts = [(B - x, y) if um_y else (x, H - y) for x, y in pts]
            if pts:
                zuege.append(pts)

    def abstand(p, pts):
        best = math.inf
        for i in range(len(pts)):
            a = pts[i]
            b = pts[i + 1] if i + 1 < len(pts) else a
            dx, dy = b[0] - a[0], b[1] - a[1]
            l2 = dx * dx + dy * dy
            t = 0.0 if l2 <= 0 else max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2))
            best = min(best, math.hypot(p[0] - a[0] - t * dx, p[1] - a[1] - t * dy))
        return best
    grenze = r + PASS_ABSTAND
    aus = []
    for i, (x, y) in enumerate(pl):
        d_rand = min(x, y, B - x, H - y)
        d_teil = min((abstand((x, y), q) for q in zuege), default=math.inf)
        if min(d_rand, d_teil) < grenze - 1e-6:
            nummer = einst.pass_nummer(e, B, H, x, y)
            aus.append({"index": i, "nummer": nummer, "paar": int(nummer.rstrip("′") or 0),
                        "grund": "rand" if d_rand <= d_teil else "teil", "luecke": min(d_rand, d_teil) - r})
    return aus


def _ablauf_vorab(e, zu, info) -> dict:
    """Ablauf (Seiten › Werkzeuge › Bearbeitungen) schon vor dem Rechnen – ohne Zeiten."""
    from . import ablauf
    try:
        a = einst.auftrag(e, zu, info["dicke"], "", flaeche=(info["breite"], info["hoehe"]))
        aus = ablauf.vorab(a, zweiseitig=einst.zweiseitig_an(e))
        from .post import eine_datei
        aus["eine_datei"] = eine_datei(a)
        return aus
    except Exception:            # nie die Analyse verhindern
        return {"zweiseitig": einst.zweiseitig_an(e), "seiten": [], "vorab": True}


def rechnen(pfad: str, ordner: str, name: str = "programm", ab: dict = None, platte: int = 1,
            entwurf: bool = False, luft: float = 0.0, nur_pruefen: bool = False) -> dict:
    """entwurf: 3D-Bahnen gröber und schneller (Entwurfsmodus, nie zum Speichern).
    luft: Luftfräsen – alles so hoch, dass die tiefste Stelle so viele mm über dem Werkstück bleibt.
    nur_pruefen: nichts neu rechnen (Zwischenspeicher, speicher.py) – geänderte Bearbeitungen behalten
    ihre alten Bahnen und stehen im Ergebnis unter «veraltet»; nie zum Speichern."""
    from . import engine
    from .svg_lesen import hinweise, lesen
    svg = _laden(pfad)
    e = einst.laden(svg.get(einst.ATTRIBUT))
    wz_hinweise = _abgleichen(e)
    info = lesen(svg, toleranz=float(e.get("toleranz", 0.01)), zweiseitig=einst.zweiseitig_an(e),
                 platte=platte)
    if info.get("platte") == 0:
        info["dicke"] = float(einst.vorrichtung(e).get("dicke") or 19)
    einst.ergaenzen(e, {(el["art"], el.get("seite", 1)) for el in info["elemente"] if el["art"]})
    os.makedirs(ordner, exist_ok=True)
    for alt in os.listdir(ordner):              # alte Ergebnisse weg
        if alt.endswith((".tap", ".nc", ".gcode", ".html")):
            os.unlink(os.path.join(ordner, alt))
    teile = os.path.join(ordner, ".teile")
    if os.path.isdir(teile):
        for alt in os.listdir(teile):
            os.unlink(os.path.join(teile, alt))
    a = einst.auftrag(e, einst.zuordnen(e, info["elemente"]), info["dicke"],
                      os.path.join(ordner, name + ".tap"), flaeche=(info["breite"], info["hoehe"]),
                      grundplatte=_grundplatte(svg, e, info))
    a["nullpunkt"] = einst.nullpunkt(e, info["breite"], info["hoehe"])
    a["nullpunkt_seiten"] = _nullpunkt_seiten(info)
    a["material_flaeche"] = [info["breite"], info["hoehe"]]
    a["spannmittel"] = info.get("spannmittel", [])
    a["stege"] = info.get("stege", [])
    a["stege_seite"] = info.get("stege_seite", [])
    a["stege_bearbeitung"] = info.get("stege_bearbeitung", [])
    a["reliefs"] = info.get("reliefs", [])           # 3D-Modelle (ops/relief3d.py)
    a["_dokument"] = f"{os.path.abspath(pfad)}#{platte}"   # Zwischenspeicher: alte Stände je Zeichnung
    if nur_pruefen:
        a["nur_pruefen"] = True
    a["entwurf"] = bool(entwurf) and not ab and any(
        b.get("art") in ("relief_schruppen", "relief_schlichten") for b in a["bearbeitungen"])
    _vorrichtung_anwenden(svg, e, info, a, int(info.get("platte", 1)))
    if ab:
        a["ab"] = ab                            # Wiederaufnahme nach einem Abbruch
    if luft:
        a["luft"] = float(luft)
        a["entwurf"] = False
    if not a["bearbeitungen"]:
        return {"ok": False, "dateien": [], "bericht": [], "bearbeitungen": {},
                "warnungen": ["Nichts zu fräsen: keine eingeschalteten Bearbeitungen mit "
                              "Objekten in den Fräsfarben."] + hinweise(info)}
    erg = engine.rechnen(a)
    for st_ in (erg.get("bearbeitungen") or {}).values():      # gerechnete Stege «auto» → Marken (Basis 26.10.)
        if isinstance(st_, dict) and st_.get("stege_auto"):
            _marken_ins_svg(svg, info, einst.zweiseitig_an(e), st_["stege_auto"])
    for u_ in erg.get("umspannen") or []:                     # Sperrzonen der Spannung 2 für die Zeichenfläche
        u_["zonen"] = []
        for ring in u_.get("zonen_mm") or []:
            pts = [{"x": x, "y": y, "seite": u_["seite"]} for x, y in ring]
            _marken_ins_svg(svg, info, einst.zweiseitig_an(e), pts)
            u_["zonen"].append([p_["svg"] for p_ in pts])
    erg["entwurf"] = a["entwurf"]                    # Panel: vor dem Speichern genau nachrechnen
    erg["warnungen"] = wz_hinweise + erg.get("warnungen", []) + hinweise(info)
    if "warnungen_allgemein" in erg:
        erg["warnungen_allgemein"] = wz_hinweise + erg["warnungen_allgemein"] + hinweise(info)
    if erg.get("ok") and not ab:
        from . import fortschritt
        from .pruefung import pruefen
        from .sprache import tr
        fortschritt.melden(tr("Prüfung"))
        try:
            erg["pruefung"] = pruefen(a, erg)
            # Restmaterial steht als Hinweis mit Knopf in der Prüfung – nicht noch einmal in den Warnungen
            doppelt = {x["text"] for x in erg["pruefung"] if x.get("aktion") == "restmaterial"}
            for k_ in ("warnungen", "warnungen_allgemein"):
                if k_ in erg:
                    erg[k_] = [w_ for w_ in erg[k_] if w_ not in doppelt]
        except Exception as ex:  # Prüfung darf das Rechnen nie verhindern
            erg["pruefung"] = [{"stufe": "warnung", "text": f"Prüfung fehlgeschlagen: {ex}",
                                "ort": None, "bearbeitung": None}]
        if erg.get("halter_befund"):                # Spindel und Halterung (B-10): Fehler, aber die Dateien sind da
            erg["pruefung"].insert(0, erg["halter_befund"])
        # Sicherheitsprüfung (Peter, planung 05.11.): ohne Sicherheitsnetz nicht unbemerkt fräsen
        if erg.get("sicherheitsnetz_aus") == "modul":
            erg["pruefung"].insert(0, {"stufe": "fehler", "ort": None, "bearbeitung": None, "text": tr(
                "Die Prüfung der Leerfahrten über dem stehenden Material lief nicht (Modul fehlt) – "
                "bitte die Installation prüfen, bevor du fräst.")})
        if erg.get("hoehe2_geschaetzt") is not None:
            h2 = ("%.2f" % erg["hoehe2_geschaetzt"]).rstrip("0").rstrip(".").replace(".", ",")
            erg["pruefung"].append({"stufe": "warnung", "ort": None, "bearbeitung": None, "text": tr(
                "Spannung 2: Wo die Spannmittel nach dem Umspannen liegen, ist nicht eingezeichnet – "
                "Leerfahrten ausserhalb der Zonen auf {} mm.".replace("{}", h2))})
    if erg.get("ok"):
        erg["freigabe"] = endpruefung(a, erg)
        nur_beim_speichern(erg)
    # Der Arbeitsordner ist intern – gespeichert wird später über das Panel
    erg["bericht"] = [z for z in erg.get("bericht", []) if not z.startswith("Ordner:")]
    return erg


def nur_beim_speichern(erg: dict) -> None:
    """Peter 06.11.: «kein Kernloch im Programm» und «Fräser ohne Mittenschnitt taucht ein» nur beim Speichern – aus
    der Prüfung im Panel (Befunde mit «nur_speichern») in die Endprüfung. Das Eintauchen meldet die Endprüfung selbst
    schon (engine._eintauchen_ohne_mitte); fehlt es dort, kommt der Satz der Prüfung mit (nichts geht verloren)."""
    nur = [x for x in erg.get("pruefung") or [] if x.get("nur_speichern")]
    if not nur or not erg.get("freigabe"):
        return
    erg["pruefung"] = [x for x in erg["pruefung"] if not x.get("nur_speichern")]
    fr = erg["freigabe"]
    gemeldet = {str(f.get("key") or "").split("@")[0].split("#")[0] for f in erg.get("ohne_mittenschnitt") or []}
    for x in nur:
        if x["stufe"] == "fehler" and x.get("bearbeitung") in gemeldet:
            continue                          # dieselbe Bearbeitung meldet die Endprüfung schon (Fable 06.11.: nur sie)
        fr.setdefault("punkte", []).append({"stufe": x["stufe"], "text": x["text"]})
        if x["stufe"] == "fehler" or (x["stufe"] == "warnung" and fr.get("stufe") == "ok"):
            fr["stufe"] = x["stufe"]


def _fraeser_je_datei(erg: dict) -> dict:
    """Je Datei (Name): (Radius, flach, Seite) für die Endprüfung. Fräser nur, wenn je Datei ein Programm (sonst 0:
    nur die Fräsermitte); die Seite aus «datei_seiten» – auch wenn eine Datei alle Werkzeuge einer Seite enthält."""
    dateien, programme = erg.get("dateien") or [], erg.get("programme") or []
    seiten = erg.get("datei_seiten") or []
    if len(seiten) != len(dateien):
        seiten = [None] * len(dateien)
    je = len(dateien) == len(programme)
    teile = erg.get("datei_teile") or []
    if len(teile) != len(dateien):
        teile = [[] for _ in dateien]

    def fraeser(pr, seite):
        # dazu die Form für die Nachprüfung der Eilgänge (freigabe._eilgaenge_nachpruefen, 06.11.)
        return (float(pr.get("durchmesser") or 0.0) / 2, pr.get("form") == "flach", seite,
                pr.get("form3d") or pr.get("form") or "", float(pr.get("v_winkel") or 60),
                float(pr.get("spitze") or 0), float(pr.get("eckradius") or 0))
    aus = {}
    for i, d in enumerate(dateien):
        if not je and teile[i] and all(0 <= k < len(programme) for _z, k in teile[i]):
            # Sammeldatei (Fable 07.11.): je Werkzeug seine Form, ab der Zeile, wo es beginnt
            aus[os.path.basename(str(d))] = {"teile": [(z, fraeser(programme[k], seiten[i]),
                                                        "T%s" % programme[k].get("t", k + 1))
                                                       for z, k in teile[i]]}
            continue
        aus[os.path.basename(str(d))] = fraeser(programme[i] if je else {}, seiten[i])
    return aus


def endpruefung(a: dict, erg: dict) -> dict:
    """Endprüfung der fertigen Dateien (freigabe.py): Tiefe, Verfahrbereich, Eilgänge, Sicherheitshöhe."""
    from . import freigabe
    try:
        h = a.get("hoehen") or {}
        dicke = float(h.get("materialdicke") or 0.0)
        np_ = a.get("nullpunkt") or {}
        oben = dicke if np_.get("z_unten") else 0.0
        # schon frei (Beginnt bei Tiefe, vorgefräst): dort darf der Eilgang so tief
        frei = max([float(b.get("starttiefe") or 0.0) for b in a.get("bearbeitungen") or []] + [0.0])
        # tiefste eingestellte Stelle: tiefer darf das Programm nirgends fräsen
        soll, pass_tief, mit_3d = 0.0, 0.0, False
        werkzeuge = a.get("werkzeuge") or {}
        bea = a.get("bearbeitungen") or []
        # Plantiefe je Seite: die anderen Bahnen der Seite liegen um sie tiefer (Zufallstests 06.11.) – ausser dem
        # Planen selbst, Passlöchern und Konturen ganz durch (sie werden nicht verschoben, Fable 06.11.)
        plan = {}
        for b in bea:
            if b.get("art") == "planen":
                s_ = int(b.get("auf_seite") or 1)
                plan[s_] = max(plan.get(s_, 0.0), float(b.get("tiefe") or 0.0))
        for b in bea:
            art = b.get("art") or ""
            if art.startswith("relief") or b.get("tiefe_ab") in ("modell", "haut", "bis_modell"):
                mit_3d = True
                continue
            t = b.get("max_tiefe") if art == "vcarve" else b.get("tiefe")
            t = dicke + 0.2 if t in (None, "") else float(t)
            if art in ("gravur", "vcarve"):
                t += float(b.get("starttiefe") or 0.0)        # Tiefe ab der Stufe (engine._ab_stufe, Fable 06.11.)
            if art == "kontur" and b.get("schlicht_tiefe") not in (None, ""):
                try:
                    t = max(t, float(b["schlicht_tiefe"]))  # B-2: Schlichten tiefer als das Schruppen (Fable 06.11.)
                except (TypeError, ValueError):
                    pass
            if art == "gewinde":
                t += 4.0                               # Kernloch: + eine Steigung
            if art == "bohren":
                w = werkzeuge.get(b.get("werkzeug") or "") or {}
                if w.get("typ") == "bohrer":
                    t += float(w.get("durchmesser") or 3.0)      # Spitze dazu (grosszügig)
            durch_ = art == "kontur" and (b.get("durch") or t >= dicke - 1e-6)
            if art != "planen" and b.get("id") != einst.PASS_ID and not durch_:
                t += plan.get(int(b.get("auf_seite") or 1), 0.0)
            if b.get("id") == einst.PASS_ID:
                pass_tief = max(pass_tief, t)
            soll = max(soll, t)
        fr = freigabe.pruefen(erg.get("dateien") or [], dicke, oben - frei if frei else oben,
                              float(h.get("sicherheitshoehe") or 20.0), a.get("maschine"),
                              luft=float(a.get("luft") or 0.0), oben_material=oben,
                              soll_max=None if mit_3d or not soll else soll, pass_tief=pass_tief,
                              fraeser=_fraeser_je_datei(erg))
        # Fräser ohne Mittenschnitt taucht senkrecht ins Material (engine._eintauchen_ohne_mitte; Peter 06.11.)
        from .engine import OHNE_MITTE
        je: dict = {}
        for f in erg.get("ohne_mittenschnitt") or []:
            je[f["name"]] = je.get(f["name"], 0) + 1
        for name_, n_ in je.items():
            fr.setdefault("punkte", []).append({"stufe": "fehler", "text": OHNE_MITTE.format(name_, n_)})
            fr["stufe"] = "fehler"
        return fr
    except Exception as ex:          # die Endprüfung darf nie das Rechnen verhindern
        return {"stufe": "warnung", "punkte": [{"stufe": "warnung", "text": f"Endprüfung fehlgeschlagen: {ex}"}],
                "dateien": len(erg.get("dateien") or [])}


def vorschau(pfad: str, einst_text: str, bid: str, ordner: str, platte: int = 1) -> dict:
    """Nur eine Bearbeitung rechnen – mit den Werten aus dem offenen Dialog
    (einst_text: Einstellungen als JSON). Für die Vorschau im Dialog."""
    from . import engine
    from .svg_lesen import lesen
    svg = _laden(pfad)
    e = einst.laden(einst_text)
    _abgleichen(e)
    info = lesen(svg, toleranz=float(e.get("toleranz", 0.01)), zweiseitig=einst.zweiseitig_an(e),
                 platte=platte)
    if info.get("platte") == 0:
        info["dicke"] = float(einst.vorrichtung(e).get("dicke") or 19)
    einst.ergaenzen(e, {(el["art"], el.get("seite", 1)) for el in info["elemente"] if el["art"]})
    ziel = [b for b in e["bearbeitungen"] if b.get("id") == bid]
    if not ziel:
        return {"ok": False, "bahnen": {}, "warnungen": ["Bearbeitung nicht gefunden."]}
    for b in e["bearbeitungen"]:
        b["aktiv"] = b.get("id") == bid
    ziel[0]["aktiv"] = True
    os.makedirs(ordner, exist_ok=True)
    a = einst.auftrag(e, einst.zuordnen(e, info["elemente"]), info["dicke"],
                      os.path.join(ordner, "vorschau.tap"), flaeche=(info["breite"], info["hoehe"]),
                      grundplatte=_grundplatte(svg, e, info))
    a["nullpunkt"] = einst.nullpunkt(e, info["breite"], info["hoehe"])
    a["nullpunkt_seiten"] = _nullpunkt_seiten(info)
    a["material_flaeche"] = [info["breite"], info["hoehe"]]
    a["spannmittel"] = info.get("spannmittel", [])
    a["stege"] = info.get("stege", [])
    a["stege_seite"] = info.get("stege_seite", [])
    a["stege_bearbeitung"] = info.get("stege_bearbeitung", [])
    a["reliefs"] = info.get("reliefs", [])           # 3D-Modelle (ops/relief3d.py)
    _vorrichtung_anwenden(svg, e, info, a, int(info.get("platte", 1)))
    if not a["bearbeitungen"]:
        return {"ok": True, "bahnen": {}, "bearbeitungen": {}, "warnungen": []}
    erg = engine.rechnen(a)
    bahnen = {k: v for k, v in (erg.get("bahnen") or {}).items() if k == bid or k.startswith(bid + "#")}
    st = {k: v for k, v in (erg.get("bearbeitungen") or {}).items() if k == bid}
    return {"ok": bool(erg.get("ok", True)), "bahnen": bahnen, "bearbeitungen": st,
            "warnungen": list(erg.get("warnungen") or [])}


def stege_verteilen(pfad: str, einst_text: str, bid: str, ordner: str, platte: int = 1,
                    anzahl: int = None, breite: float = None) -> dict:
    """Haltestege einer Kontur gleichmässig verteilen (Entwurf B, ops/stege.py): Vorschlag für die Marken.

    Rechnet wie «rechnen» (mit dem Zwischenspeicher meist schnell), mit den Werten aus dem offenen Dialog
    (einst_text), damit zweiseitig erkannt wird, welche Seite das Teil heraustrennt. anzahl/breite: sonst aus
    der Bearbeitung (stege_anzahl, stege_breite). Ergebnis:
    {"ok", "stege": [{"x", "y", "seite", "svg": [X, Y]}], "warnungen"} – x/y in mm wie die gelesenen Marken
    (Seite, Nullpunkt unten links), svg in Benutzereinheiten des Dokuments (ohne Transformation einer Ebene)."""
    from . import engine
    from .ops import stege as _stege
    from .svg_lesen import lesen
    svg = _laden(pfad)
    e = einst.laden(einst_text) if einst_text else einst.laden(svg.get(einst.ATTRIBUT))
    _abgleichen(e)
    zweiseitig = einst.zweiseitig_an(e)
    info = lesen(svg, toleranz=float(e.get("toleranz", 0.01)), zweiseitig=zweiseitig, platte=platte)
    einst.ergaenzen(e, {(el["art"], el.get("seite", 1)) for el in info["elemente"] if el["art"]})
    ziel = [b for b in e["bearbeitungen"] if b.get("id") == bid]
    if not ziel:
        return {"ok": False, "stege": [], "warnungen": ["Bearbeitung nicht gefunden."]}
    zb = ziel[0]
    if zb.get("art") != "kontur":
        return {"ok": False, "stege": [], "warnungen": ["Haltestege gibt es nur bei der Kontur."]}
    zb["aktiv"] = True
    n = int(anzahl if anzahl is not None else (zb.get("stege_anzahl") or 4))
    br = float(breite if breite is not None else (zb.get("stege_breite") or 6.0))
    os.makedirs(ordner, exist_ok=True)
    a = einst.auftrag(e, einst.zuordnen(e, info["elemente"]), info["dicke"],
                      os.path.join(ordner, "stege.tap"), flaeche=(info["breite"], info["hoehe"]),
                      grundplatte=_grundplatte(svg, e, info))
    a["nullpunkt"] = einst.nullpunkt(e, info["breite"], info["hoehe"])
    a["nullpunkt_seiten"] = _nullpunkt_seiten(info)
    a["material_flaeche"] = [info["breite"], info["hoehe"]]
    a["spannmittel"] = info.get("spannmittel", [])
    a["stege"] = info.get("stege", [])
    a["stege_seite"] = info.get("stege_seite", [])
    a["stege_bearbeitung"] = info.get("stege_bearbeitung", [])
    a["reliefs"] = info.get("reliefs", [])
    a["_dokument"] = f"{os.path.abspath(pfad)}#{platte}"
    a["_steg_ringe"] = True
    _vorrichtung_anwenden(svg, e, info, a, int(info.get("platte", 1)))
    erg = engine.rechnen(a)
    marken = _stege.vorschlag(erg, bid, n, br)
    warnungen = []
    if not (erg.get("steg_ringe") or {}).get(bid):
        warnungen.append("Die Kontur geht nicht ganz durch – sie braucht keine Haltestege.")
    elif not marken:
        warnungen.append("Form zu klein für Haltestege.")
    _marken_ins_svg(svg, info, zweiseitig, marken)
    return {"ok": True, "stege": marken, "warnungen": warnungen}


def _marken_ins_svg(svg, info: dict, zweiseitig: bool, marken: list) -> None:
    """Haltesteg-Marken [{x, y, seite}] (mm, Koordinaten der Seite) bekommen "svg": [X, Y] in Benutzereinheiten
    des Dokuments (wie svg_lesen.linien, nur umgekehrt; ohne Transformation einer Ebene)."""
    from .svg_lesen import seiten
    uu = svg.unittouu("1mm")
    hoehe_uu = svg.viewbox_height
    alle = seiten(svg)
    pp = 2 if zweiseitig else 1
    for m in marken:
        m["x"], m["y"] = round(float(m["x"]), 4), round(float(m["y"]), 4)
        dx = dy = 0.0
        if len(alle) > 1:
            gi = min(len(alle) - 1, (int(info.get("platte") or 1) - 1) * pp + int(m.get("seite") or 1) - 1)
            dx, dy = alle[gi]["x0"] - alle[0]["x0"], alle[gi]["y0"] - alle[0]["y0"]
        m["svg"] = [round((m["x"] + dx) * uu, 4), round(hoehe_uu - (m["y"] + dy) * uu, 4)]


def main(argv=None):
    """Einstieg für das Panel; die JSON-Ausgabe kommt in der eingestellten Sprache."""
    from . import sprache
    if not sprache.englisch():
        return _main(argv)
    import io
    puffer, alt = io.StringIO(), sys.stdout
    sys.stdout = puffer
    try:
        r = _main(argv)
    finally:
        sys.stdout = alt
    text = puffer.getvalue()
    try:
        befehl = (list(sys.argv[1:] if argv is None else argv) or [None])[0]
        text = json.dumps(sprache.ausgabe(json.loads(text), befehl), ensure_ascii=False)
    except ValueError:
        pass
    sys.stdout.write(text)
    sys.stdout.flush()
    return r


def _main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv and argv[0] == "datenbank":             # Werkzeugdatenbank (Panel)
        from . import werkzeugdb
        try:
            return werkzeugdb.main(argv[1:])
        except Exception as ex:
            sys.stdout.write(json.dumps({"ok": False, "warnungen": [f"Fehler: {ex}"]},
                                        ensure_ascii=False))
            return 0
    if argv and argv[0] == "messkorrektur":         # Testteil ausmessen (Dialog)
        from . import messkorrektur
        return messkorrektur.main(argv[1:])
    if argv and argv[0] == "post":                  # Postprozessoren verwalten (Dialog)
        from .post import verwalten
        return verwalten.main(argv[1:])
    if argv and argv[0] == "gravur":                # Gravurtext-Dialog (Panel)
        from . import gravurtext
        return gravurtext.main(argv[1:])
    if argv and argv[0] == "zusatz":                # Zusatzformat öffnen (Zusatz im Einstellungsordner)
        from . import zusatz
        zusatzformate = zusatz.modul("formate")
        if zusatzformate is None:
            sys.stdout.write(json.dumps({"ok": False, "fehler": "Dieses Format lässt sich nicht öffnen."},
                                        ensure_ascii=False))
            return 1
        zusatzformate.main(argv[1:])
        return 0
    if argv and argv[0] == "schrift":               # Schriften importieren (Panel)
        from . import schriftimport
        schriftimport.main(argv[1:])
        return 0
    if argv and argv[0] == "aufraeumen":            # Zeichnung aufräumen (Import)
        from . import aufraeumen
        return aufraeumen.main(argv[1:])
    if argv and argv[0] == "nesting":               # Vervielfältigen und Verschachteln
        from . import nesting
        return nesting.main(argv[1:])
    if argv and argv[0] == "relief":                # 3D-Modell einlesen (3D-Fräsung)
        from . import relief
        return relief.main(argv[1:])
    if argv and argv[0] == "stege":                 # Haltestege gleichmässig verteilen (Kontur-Dialog, Entwurf B)
        sp = argparse.ArgumentParser()
        sp.add_argument("befehl")
        sp.add_argument("svg")
        sp.add_argument("--einst")
        sp.add_argument("--bearbeitung", required=True)
        sp.add_argument("--ordner", required=True)
        sp.add_argument("--platte", type=int, default=1)
        sp.add_argument("--anzahl", type=int)
        sp.add_argument("--breite", type=float)
        v = sp.parse_args(argv)
        try:
            text = None
            if v.einst:
                with open(v.einst, encoding="utf-8") as f:
                    text = f.read()
            erg = stege_verteilen(v.svg, text, v.bearbeitung, v.ordner, v.platte, v.anzahl, v.breite)
        except Exception as ex:
            import traceback
            erg = {"ok": False, "stege": [], "warnungen": [f"Fehler: {ex}"], "traceback": traceback.format_exc()}
        sys.stdout.write(json.dumps(erg, ensure_ascii=False))
        sys.stdout.flush()
        return 0
    if argv and argv[0] == "vorschau":              # eine Bearbeitung für den Dialog
        vp = argparse.ArgumentParser()
        vp.add_argument("befehl")
        vp.add_argument("svg")
        vp.add_argument("--einst", required=True)
        vp.add_argument("--bearbeitung", required=True)
        vp.add_argument("--ordner", required=True)
        vp.add_argument("--platte", type=int, default=1)
        v = vp.parse_args(argv)
        try:
            with open(v.einst, encoding="utf-8") as f:
                erg = vorschau(v.svg, f.read(), v.bearbeitung, v.ordner, v.platte)
        except Exception as ex:
            import traceback
            erg = {"ok": False, "warnungen": [f"Fehler: {ex}"], "traceback": traceback.format_exc()}
        sys.stdout.write(json.dumps(erg, ensure_ascii=False))
        sys.stdout.flush()
        return 0
    ap.add_argument("befehl", choices=["analyse", "rechnen"])
    ap.add_argument("svg")
    ap.add_argument("--ordner")
    ap.add_argument("--name", default="programm")
    ap.add_argument("--platte", type=int, default=1)
    ap.add_argument("--ab", help='Wiederaufnahme: {"key": …} oder {"programm": n, "zeile": z}')
    ap.add_argument("--luft", type=float, default=0.0, help="Luftfräsen: Abstand über dem Werkstück (mm)")
    ap.add_argument("--entwurf", action="store_true", help="3D-Bahnen gröber und schneller (Entwurfsmodus)")
    ap.add_argument("--nur-pruefen", action="store_true",
                    help="nichts neu rechnen: geänderte Bearbeitungen behalten ihre alten Bahnen (veraltet)")
    ap.add_argument("--fortschritt", help="Datei, in die der aktuelle Schritt geschrieben wird (Rechenanzeige)")
    a = ap.parse_args(argv)
    from . import fortschritt
    fortschritt.setzen(a.fortschritt)
    try:
        if a.befehl == "analyse":
            erg = analyse(a.svg, a.platte)
        else:
            erg = rechnen(a.svg, a.ordner or os.path.dirname(os.path.abspath(a.svg)), a.name,
                          json.loads(a.ab) if a.ab else None, a.platte, a.entwurf, a.luft, a.nur_pruefen)
    except Exception as ex:  # lesbar an das Panel zurückgeben
        import traceback
        erg = {"ok": False, "warnungen": [f"Fehler: {ex}"], "traceback": traceback.format_exc()}
    sys.stdout.write(json.dumps(erg, ensure_ascii=False))
    sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
