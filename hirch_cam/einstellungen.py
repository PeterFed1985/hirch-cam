# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Einstellungen einer Zeichnung: Werkzeuge, Bearbeitungen, Höhen, Steuerung.

Sie stehen als JSON im Wurzelelement der SVG-Datei (Attribut
hirch:einstellungen), damit sie mit der Zeichnung gespeichert werden. Das
Fräsen-Panel liest und schreibt sie; fehlende Werte kommen von hier.

Eine Fräsart kann mehrere Bearbeitungen haben (z. B. zwei Taschen mit
verschiedener Tiefe). Jede Bearbeitung hat eine eigene Farbe – einen Ton der
Grundfarbe ihrer Fräsart. Ein Objekt gehört zu der Bearbeitung seiner
Fräsart, deren Farbe seiner Linienfarbe am nächsten ist. Zusätzlich kann
ein Objekt weiteren Bearbeitungen zugewiesen sein (Attribut hirch:auch,
z. B. dieselbe Kontur erst mit dem V-Fräser fasen, dann ausschneiden).
"""
from __future__ import annotations

import copy
import json
from typing import Dict, List, Optional

from .farben import FRAESARTEN, OHNE_OBJEKTE, abstand, art_name, art_zusatz, alte_relief_farbe, rgb_von, schattierung

NS = "https://hirch.net/cnc"
ATTRIBUT = "{%s}einstellungen" % NS
DICKE = "{%s}materialdicke" % NS
TEXT = "{%s}text" % NS        # markiert in Pfade umgewandelten Text (Panel)
VERSTECKT = "{%s}versteckt" % NS    # im Panel ausgeblendet, wird aber gefräst
AUCH = "{%s}auch" % NS        # weitere Bearbeitungen eines Objekts (IDs, Leerzeichen)
FLAECHEN = "{%s}flaechen" % NS   # Linie aus «Gewählte Flächen als Bereich» (Reiter «3D»), JSON
STEG = "{%s}steg" % NS      # von Hand gesetzter Haltesteg (Markierung, wird nie gefräst)
NULLMARKE = "{%s}nullpunkt" % NS   # provisorischer Nullpunkt (Kreuz, wird nie gefräst; Peter 29.10.)
NULLKAMERA = "{%s}kamera" % NS     # am Kreuz: Kamera-Versatz «dx,dy» mm (Fräser gegen Kamera, Peter 29.10.)
SPANN = "{%s}spannmittel" % NS
PASSLOCH = "{%s}passloch" % NS   # Passloch-Kreis der Ebene «Passlöcher» (Zeichnung, wird nicht gelesen)
VORRICHTUNG = "{%s}vorrichtung" % NS   # Seite (Page) der Spannvorrichtung    # Spannmittel (Pratze, Zwinge …): Höhe über dem Material in mm

ARTEN = ["vcarve", "gravur", "bohren", "gewinde", "tasche", "kontur"]   # Vorgabe-Reihenfolge

_FELDER = {"schneiden": 2, "schneidenlaenge": 20.0, "mittenschnitt": True, "spitze": 0.0,
           "eckradius": 0.0}
STANDARD_WERKZEUGE = {
    "A": dict(_FELDER, typ="v", durchmesser=12.0, v_winkel=60, drehzahl=18000, vorschub=1000,
              eintauchen=300, nummer=1, name="V-Fräser 60°", schneidenlaenge=11.0, auto=True),
    "B": dict(_FELDER, typ="schaft", durchmesser=3.0, v_winkel=60, drehzahl=18000, vorschub=1000,
              eintauchen=300, nummer=1, name="Schaftfräser 3 mm", schneidenlaenge=12.0, auto=True),
    "C": dict(_FELDER, typ="schaft", durchmesser=6.0, v_winkel=60, drehzahl=18000, vorschub=1000,
              eintauchen=300, nummer=1, name="Schaftfräser 6 mm", schneidenlaenge=22.0, auto=True),
}

def vorgaben_ohne_nutzen_weg(e: Dict) -> List[str]:
    """Vorgabe-Werkzeuge einer neuen Zeichnung (A, B, C), die keine Bearbeitung benutzt und die nie geändert
    wurden, aus der Zeichnung nehmen (Peter 27.10.: eine reine V-Carve-Zeichnung zeigte noch Schaftfräser 3 und
    6 mm «nur in dieser Zeichnung»). Nur, wenn es schon Bearbeitungen gibt – eine leere neue Zeichnung behält
    ihre Vorgaben für «+ Neu». Gibt die entfernten Plätze zurück."""
    ws = e.get("werkzeuge")
    bea = e.get("bearbeitungen") or []
    if not isinstance(ws, dict) or not bea:
        return []
    benutzt = set()
    for b in bea:
        for k, v in (b or {}).items():
            if "werkzeug" in k and isinstance(v, str) and v:
                benutzt.add(v)
    pw = (e.get("zweiseitig") or {}).get("pass_werkzeug")
    if isinstance(pw, str) and pw:
        benutzt.add(pw)
    weg = []
    # was den Fräser ausmacht – Name, Verwaltungs- und spätere Zusatzfelder zählen nicht
    felder = ("typ", "durchmesser", "schneiden", "schneidenlaenge", "v_winkel", "eckradius", "spitze",
              "mittenschnitt", "drehzahl", "vorschub", "eintauchen")
    for k in list(ws):
        w = ws[k] or {}
        std = STANDARD_WERKZEUGE.get(k)
        if k in benutzt or std is None or w.get("db_id"):
            continue
        if all(w.get(f) == std.get(f) for f in felder):
            del ws[k]
            weg.append(k)
    return weg


STANDARD_BEARBEITUNG = {
    "vcarve": {"werkzeug": "A", "max_tiefe": 5.0, "zustellung": 0.0, "aufdicken": 0.0, "flach_werkzeug": "",
               "starttiefe": 0.0},
    "gravur": {"werkzeug": "A", "tiefe": 0.5, "zustellung": 0.5, "starttiefe": 0.0},
    # Peter 01.11.: adaptiv ist die Vorgabe für neue Taschen (schnellste von adaptiv / weit adaptiv + Runden / Runden)
    "tasche": {"werkzeug": "B", "tiefe": 3.0, "zustellung": 1.5, "bahnabstand": 40, "strategie": "adaptiv",
               "gleichlauf": True, "aufmass": 0.0, "schlichten": False, "schlicht_zugabe": 0.3,
               "schlicht_werkzeug": "", "schlicht_zustellung": None,
               "raeum_werkzeug": "", "raeum_zustellung": 0, "raeum_bahnabstand": None,
               "raeum_strategie": "", "raeum_eintauchen_art": "", "raeum_rampe_winkel": None,
               "raeum_vollnut": None, "raeum_vorschub_prozent": None, "v_wand": "oben", "starttiefe": 0.0},
    "kontur": {"werkzeug": "C", "tiefe": None, "zustellung": 2.0, "seite": "auto",
               "gleichlauf": True, "stege_modus": "auto", "stege_anzahl": 4, "stege_breite": 6.0, "stege_hoehe": 1.5,
               "aufmass": 0.0, "schlichten": False, "schlicht_zugabe": 0.3,
               "schlicht_werkzeug": "", "schlicht_zustellung": None, "starttiefe": 0.0,
               # Peter 25.10.: im Bogen von der Abfallseite anfahren (keine Marke an der Wand); Kontur fällt auf
               # senkrecht zurück, wo kein Platz ist. Gespeicherte Konturen ohne Wert bleiben senkrecht (laden)
               "anfahren": "bogen"},
    "bohren": {"werkzeug": "B", "tiefe": 5.0, "zustellung": 2.5, "methode": "auto", "gleichlauf": True,
               "spitze_dazu": True},          # Bohrer: voller Ø bis zur Tiefe (Spitze reicht weiter)
    # Gewinde: Kreis mit Nenn-Ø; tiefe = Gewindelänge, steigung None = ISO-Regelgewinde
    "gewinde": {"werkzeug": "B", "tiefe": 8.0, "steigung": None, "innen": True, "rechts": True,
                "durchgaenge": 1, "passung": 0.0, "kernloch_werkzeug": ""},
    # Planfräsen: ganze Oberfläche, ohne gezeichnete Objekte
    "planen": {"werkzeug": "C", "tiefe": 0.5, "zustellung": 0.5, "bahnabstand": 60, "richtung": "x",
               "ueberstand": None},
    # 3D-Fräsung (ops/relief3d.py): fräst die 3D-Modelle der Zeichnung; relief = ID eines
    # Modells ("" = alle auf dieser Seite); rand: Bereich um das Modell (None = automatisch);
    # bereich: "alles" | "innen" | "aussen" – zugeordnete geschlossene Linien begrenzen, wo
    # gefräst wird (bereich_ueberlauf mm darüber hinaus); rest: nur, wo die vorherigen 3D-
    # Bearbeitungen dieser Seite mehr als rest_schwelle mm stehen liessen; strategie (Schlichten):
    # parallel | hoehenlinien | kombiniert (steil ab grenzwinkel °) | ebenen (waagrechte Flächen);
    # genau: Höhenlinien mit OpenCAMLib (AdaptiveWaterline) statt aus dem Raster; kehlen: nur Kehlen
    # nachfahren (Knick ab kehlen_winkel °, kehlen_bahnen zusätzliche Bahnen je Seite);
    # Schruppen adaptiv (FreeCAD Adaptive je Ebene, adaptiv_last % vom Ø seitlich)
    "relief_schruppen": {"werkzeug": "C", "relief": "", "zustellung": 3.0, "aufmass": 0.5,
                         "bahnabstand": 40, "rand": None, "bereich": "alles", "bereich_ueberlauf": 1.0,
                         "rest": False, "rest_schwelle": 0.1, "adaptiv": False, "adaptiv_last": 15,
                         # Peter 26.10.: Lücke für Lücke in Lagen von … mm (0 = Ebene für Ebene über alles)
                         "lagen": 6.0},
    # Böden und Wände (ops/boeden_waende.py): waagrechte Flächen und senkrechte Wände des Modells auf Mass,
    # nach dem 3D-Schruppen davor; stufe: mm je Stufe (von Hand); im_wechsel_mit: ID des 3D-Schruppens, mit dem
    # es Stufe um Stufe abwechselt (gleicher Fräser) – "" = nach dem ganzen Schruppen
    "boeden_waende": {"werkzeug": "C", "boeden": True, "waende": True, "aufmass": 0.0, "bahnabstand": 40,
                      "stufe": 10.0, "im_wechsel_mit": ""},
    "relief_schlichten": {"werkzeug": "C", "relief": "", "bahnabstand": 10, "richtung": "x",
                          "aufmass": 0.0, "rand": None, "abtastung": None, "bereich": "alles",
                          "bereich_ueberlauf": 1.0, "rest": False, "rest_schwelle": 0.1,
                          "strategie": "parallel", "grenzwinkel": 40, "genau": False,
                          "kehlen_winkel": 15, "kehlen_bahnen": 0},
}

STANDARD = {
    "version": 2,
    "werkzeuge": STANDARD_WERKZEUGE,
    "bearbeitungen": [],
    "hoehen": {"freifahrhoehe": 3.0, "startabstand": 0.5, "sicherheitshoehe": 20.0},
    "postprozessor": "richauto_a11",
    "boegen": False,           # Rundungen als G2/G3 statt kurzer Geraden
    "material": "hartholz",
    # Nullpunkt: xy = Bezugspunkt am Material (u/m/o × l/m/r, z. B. "ul" = unten
    # links, "mm" = Mitte), dazu ein freier Versatz dx/dy; z = "oben"
    # (Materialoberfläche) oder "unten" (Maschinentisch / Unterseite)
    "nullpunkt": {"xy": "ul", "dx": 0.0, "dy": 0.0, "z": "oben"},
    "rechenkern": "auto",
    "toleranz": 0.01,
    # Reihenfolge: "auto" = je Werkzeug ein Block (wenige Wechsel), "liste" = wie in der Liste
    "reihenfolge": "liste",
    # Zweiseitig: Seite 2 ist die zweite Seite (Page) der Zeichnung, gezeichnet so, wie
    # man sie nach dem Wenden von oben sieht. achse "x": um die X-Achse wenden (oben/unten
    # tauschen), "y": um die Y-Achse (links/rechts). Passlöcher auf der Wendeachse.
    "zweiseitig": {"an": False, "achse": "x", "passloecher": True, "pass_d": 6.0, "pass_rand": 15.0,
                   "pass_werkzeug": "", "pass_extra": 3.0, "pass_lage": "achse"},
    # Spannvorrichtung: eigene Seite «Vorrichtung», wird zuerst aus eigenem Material gefräst; das
    # Werkstück liegt dann bei lage (mm, linke untere Ecke auf der Vorrichtung). Gemeinsamer XY-Nullpunkt
    # (der der Vorrichtung). vorhanden: schon gefräst – nicht mehr ausgeben.
    # Spannvorrichtung: das Werkstück liegt mittig (ab Werk) oder bei «lage» (linke untere Ecke, mm)
    "vorrichtung": {"an": False, "dicke": 19.0, "lage": [50.0, 50.0], "vorhanden": False},
    # Rundes Material (auf der Drehbank vorbereitet, stehend): die Seite ist ein Quadrat Ø × Ø,
    # das Werkstück der Kreis darin; X0 Y0 immer in der Mitte. Befestigt mit einer Schraube durch
    # die Mittelbohrung – um Kopf/Scheibe wird nie gefräst. kerbe: kleine Kerbe am Rand bei X+
    # (zum Wenden, wenn das Teil nicht rundum gleich ist).
    # grundplatte: auf der Seite der Spannvorrichtung automatisch Mittelbohrung (Schraube) und ein
    # flacher Ring Ø + 0,2 mm, ring_tiefe tief, in dem das Teil mittig steht (Nullpunkt = Achse).
    "rund": {"an": False, "schraube": True, "bohrung": 6.5, "kopf": 12.0, "ueberstand": 0.0,
             "kerbe": False, "grundplatte": True, "ring_tiefe": 1.0},
}

PASS_ID = "pass"                 # erzeugte Bearbeitung «Passlöcher»
PASS_FARBE = "#9141ac"


def zweiseitig_an(e: Dict) -> bool:
    return bool((e.get("zweiseitig") or {}).get("an"))


def auf_seite(e: Dict, b: Dict) -> int:
    """Seite (1 oder 2), auf der eine Bearbeitung fräst; einseitig immer 1."""
    if not zweiseitig_an(e):
        return 1
    try:
        return 2 if int(b.get("auf_seite") or 1) == 2 else 1
    except (TypeError, ValueError):
        return 1


def passloecher(e: Dict, breite: float, hoehe: float) -> List[tuple]:
    """Mittelpunkte der Passlöcher (Seite-1-Koordinaten). Sie liegen auf der
    Wendeachse – nach dem Wenden wieder genau an derselben Stelle."""
    z = dict(STANDARD["zweiseitig"])
    z.update(e.get("zweiseitig") or {})
    rand = float(z.get("pass_rand") or 15.0)
    if z.get("pass_lage") == "frei" and z.get("pass_punkte"):
        # frei platziert (Peter, 21.10.): je Loch sein Partner gespiegelt an der Wendeachse;
        # ein Loch auf der Achse ist sein eigener Partner
        aus = []
        for p in z.get("pass_punkte") or []:
            try:
                x, y = float(p[0]), float(p[1])
            except (TypeError, ValueError, IndexError):
                continue
            aus.append((x, y))
            q = pass_partner(z, breite, hoehe, x, y)
            if abs(q[0] - x) > 1e-6 or abs(q[1] - y) > 1e-6:
                aus.append(q)
        if aus:
            return aus
    if z.get("pass_lage") == "ecken":
        # vier Löcher in den Ecken (Peter, 20.10.): je zwei liegen spiegelbildlich zur Wendeachse –
        # nach dem Wenden trifft jedes Loch wieder einen Stift
        return [(rand, rand), (breite - rand, rand), (rand, hoehe - rand), (breite - rand, hoehe - rand)]
    if z.get("achse") == "y":
        return [(breite / 2, rand), (breite / 2, hoehe - rand)]
    return [(rand, hoehe / 2), (breite - rand, hoehe / 2)]


def pass_paare(e: Dict, breite: float, hoehe: float) -> List[tuple]:
    """Die Primärlöcher der Passloch-Paare in der Reihenfolge, in der das Panel sie nummeriert («1» und sein
    Partner «1′», hirch-dialoge.cpp pass_primaer): Paar n = n-tes Primärloch, Partner = Spiegelbild an der
    Wendeachse (auf der Achse: es selbst)."""
    z = dict(STANDARD["zweiseitig"])
    z.update(e.get("zweiseitig") or {})
    rand = float(z.get("pass_rand") or 15.0)
    um_y = z.get("achse") == "y"
    if z.get("pass_lage") == "frei" and z.get("pass_punkte"):
        aus = []
        for p in z.get("pass_punkte") or []:
            try:
                aus.append((float(p[0]), float(p[1])))
            except (TypeError, ValueError, IndexError):
                continue
        if aus:
            return aus
    if z.get("pass_lage") == "ecken":
        return [(rand, rand), (rand, hoehe - rand)] if um_y else [(rand, rand), (breite - rand, rand)]
    if um_y:
        return [(breite / 2, rand), (breite / 2, hoehe - rand)]
    return [(rand, hoehe / 2), (breite - rand, hoehe / 2)]


def pass_nummer(e: Dict, breite: float, hoehe: float, x: float, y: float) -> str:
    """Bezeichnung eines Passlochs wie auf der Zeichnung: «2» (Primärloch) oder «2′» (sein Partner); "" unbekannt."""
    z = dict(STANDARD["zweiseitig"])
    z.update(e.get("zweiseitig") or {})
    for n, (px, py) in enumerate(pass_paare(e, breite, hoehe), 1):
        if abs(px - x) < 1e-3 and abs(py - y) < 1e-3:
            return str(n)
        qx, qy = pass_partner(z, breite, hoehe, px, py)
        if abs(qx - x) < 1e-3 and abs(qy - y) < 1e-3:
            return "%d′" % n
    return ""


def pass_partner(z: Dict, breite: float, hoehe: float, x: float, y: float) -> tuple:
    """Spiegelbild eines Passlochs an der Wendeachse (Seite-1-Koordinaten)."""
    if (z or {}).get("achse") == "y":
        return (breite - x, y)
    return (x, hoehe - y)


def _neue_id(vorhanden) -> str:
    n = 1
    while "b%d" % n in vorhanden:
        n += 1
    return "b%d" % n


# Fräsarten, die eine bestimmte Werkzeugart brauchen (sonst lässt der Rechenkern sie aus)
_WERKZEUGART = {"gewinde": ("gewinde",), "vcarve": ("v",)}


def _werkzeug_fuer(e: Dict, art: str, vorgabe: str) -> str:
    """Werkzeug für eine neue Bearbeitung (Peter, 22.10.: Gewinde auf Seite 2 bekam den Schaftfräser B und wurde
    ausgelassen): wie eine vorhandene Bearbeitung derselben Fräsart (z. B. auf der anderen Seite), sonst ein
    eingerichtetes Werkzeug der nötigen Art, sonst die Vorgabe."""
    wz = e.get("werkzeuge") or {}
    for b in e.get("bearbeitungen") or []:
        if b.get("art") == art and b.get("werkzeug") in wz:
            return b["werkzeug"]
    typen = _WERKZEUGART.get(art)
    if typen and (wz.get(vorgabe) or {}).get("typ") not in typen:
        for slot in sorted(wz):
            if (wz[slot] or {}).get("typ") in typen:
                return slot
    return vorgabe


def neue_bearbeitung(e: Dict, art: str) -> Dict:
    """Bearbeitung mit Vorgabewerten, eigener ID, freiem Farbton und passendem Werkzeug."""
    ids = {b.get("id") for b in e["bearbeitungen"]}
    belegt = {(b.get("farbe") or "").lower() for b in e["bearbeitungen"] if b["art"] == art}
    n = 0
    while schattierung(art, n) in belegt and n < 60:
        n += 1
    b = {"id": _neue_id(ids), "art": art, "aktiv": True, "farbe": schattierung(art, n)}
    b.update(copy.deepcopy(STANDARD_BEARBEITUNG[art]))
    if "werkzeug" in b:
        b["werkzeug"] = _werkzeug_fuer(e, art, b["werkzeug"])
    return b


def laden(text: Optional[str]) -> Dict:
    """JSON aus der Zeichnung lesen und mit den Vorgaben ergänzen."""
    roh = {}
    if text:
        try:
            roh = json.loads(text)
        except ValueError:
            roh = {}
    if not isinstance(roh, dict):
        roh = {}
    e = copy.deepcopy(STANDARD)
    if isinstance(roh.get("werkzeuge"), dict) and roh["werkzeuge"]:
        e["werkzeuge"] = {}
        for k, w in roh["werkzeuge"].items():
            basis = copy.deepcopy(STANDARD_WERKZEUGE.get(k, STANDARD_WERKZEUGE["B"]))
            basis.update(w or {})
            if "auto" not in (w or {}):
                basis["auto"] = False      # ältere Zeichnung: eingetragene Werte gelten
            if "name" not in (w or {}):
                basis.pop("name", None)
            e["werkzeuge"][str(k)] = basis
    if isinstance(roh.get("hoehen"), dict):
        e["hoehen"].update(roh["hoehen"])
    for k in ("rechenkern", "toleranz", "material", "eingerichtet", "boegen", "reihenfolge"):
        if k in roh:
            e[k] = roh[k]
    if isinstance(roh.get("umspannen"), dict):         # Umspannen (Fräsbahnen 27.10.): {"an", "abstand", "hoehe2"}
        e["umspannen"] = dict(roh["umspannen"])
    # Die Steuerung kommt nur aus «Einstellungen › Maschine» (Peter, 19.10.: kein Feld mehr im Panel;
    # 27.10.: ein alter Eintrag in der Zeichnung gilt nicht mehr – sonst führe jede Zeichnung still ihre
    # eigene Steuerung). Leer heisst: die Vorgabe des Programms.
    try:
        from . import werkzeugdb
        e["postprozessor"] = werkzeugdb.laden()["maschine"].get("postprozessor") or STANDARD["postprozessor"]
    except Exception:
        e["postprozessor"] = STANDARD["postprozessor"]
    if isinstance(roh.get("zweiseitig"), dict):
        e["zweiseitig"].update(roh["zweiseitig"])
    if isinstance(roh.get("vorrichtung"), dict):
        e["vorrichtung"].update(roh["vorrichtung"])
    if isinstance(roh.get("rund"), dict):
        e["rund"].update(roh["rund"])
    if isinstance(roh.get("nullpunkt"), dict):
        e["nullpunkt"].update(roh["nullpunkt"])
    for b in roh.get("bearbeitungen") or []:
        if not isinstance(b, dict) or b.get("art") not in STANDARD_BEARBEITUNG:
            continue
        neu = neue_bearbeitung(e, b["art"])
        # Ältere Zeichnungen (eine Bearbeitung je Fräsart, ohne ID und Farbe)
        # behalten so ihre Werte; ID und Farbe kommen dazu.
        werte = {k: v for k, v in b.items() if k not in ("id", "farbe") or v}
        if b["art"] == "kontur" and "anfahren" not in b:
            werte["anfahren"] = ""        # gespeichert, bevor «Im Bogen» die Vorgabe wurde: G-Code bleibt gleich
        if b["art"] == "tasche" and "strategie" not in b:
            werte["strategie"] = "runden"  # gespeichert, bevor adaptiv die Vorgabe wurde: G-Code bleibt gleich
        if b["art"] == "relief_schruppen" and "lagen" not in b:
            werte["lagen"] = 0.0          # gespeichert, bevor es Lagen gab: G-Code bleibt gleich
        if werte.get("farbe") and alte_relief_farbe(b["art"], str(werte["farbe"])):
            werte["farbe"] = alte_relief_farbe(b["art"], str(werte["farbe"]))   # 3D: grau → orange/blau
        if werte.get("id") in {x["id"] for x in e["bearbeitungen"]}:
            werte.pop("id")
        if werte.get("farbe") and rgb_von(werte["farbe"]) is None:
            werte.pop("farbe")
        neu.update(werte)
        neu["farbe"] = neu["farbe"].lower()
        e["bearbeitungen"].append(neu)
    # Früher sortierte Hirch CNC automatisch («auto»): solche Zeichnungen einmal
    # sinnvoll ordnen – ab dann gilt die Reihenfolge der Liste
    if roh.get("reihenfolge", "auto") != "liste":
        e["bearbeitungen"] = sinnvoll_ordnen(e["bearbeitungen"])
    e["reihenfolge"] = "liste"
    return e


def sinnvoll_ordnen(bearbeitungen: List[Dict]) -> List[Dict]:
    """Ablauf einmal sinnvoll ordnen: je Seite Planfräsen zuerst, Kontur
    (Ausschneiden) zuletzt, dazwischen gleiche Werkzeuge beieinander – sonst
    bleibt die Reihenfolge, wie sie ist."""
    def stufe(b):
        return 0 if b.get("art") == "planen" else (2 if b.get("art") == "kontur" else 1)

    def seite(b):
        try:
            return int(b.get("auf_seite") or 1)
        except (TypeError, ValueError):
            return 1
    erstes = {}
    for i, b in enumerate(bearbeitungen):
        erstes.setdefault((seite(b), stufe(b), b.get("werkzeug")), i)
    return [b for _i, b in sorted(enumerate(bearbeitungen), key=lambda ib: (
        seite(ib[1]), stufe(ib[1]), erstes[(seite(ib[1]), stufe(ib[1]), ib[1].get("werkzeug"))], ib[0]))]


def _vorgaben_anwenden(b: Dict, art: str):
    """«So fräse ich» (vorgaben.py): nur für neue Bearbeitungen, nie beim Laden gespeicherter."""
    from . import vorgaben
    try:
        vorgaben.anwenden(b, art)
    except Exception:          # die Datenbank ist nicht lesbar: Standard gilt
        pass


def ergaenzen(e: Dict, vorhandene_arten) -> Dict:
    """Bearbeitungen für Fräsarten anlegen, die in der Zeichnung vorkommen
    (eingefärbte Linien), aber noch keine Bearbeitung haben. vorhandene_arten:
    Fräsarten oder (Fräsart, Seite) – zweiseitig bekommt jede Seite ihre eigene."""
    paare = {(x, 1) if isinstance(x, str) else (x[0], int(x[1] or 1)) for x in vorhandene_arten}
    if not zweiseitig_an(e):
        paare = {(a, 1) for a, _s in paare}
    da = {(b["art"], auf_seite(e, b)) for b in e["bearbeitungen"]}
    for art, seite in sorted(paare, key=lambda p: (p[1], ARTEN.index(p[0]) if p[0] in ARTEN else 99)):
        if art in ARTEN and (art, seite) not in da:
            neu = neue_bearbeitung(e, art)
            _vorgaben_anwenden(neu, art)
            if seite == 2:
                neu["auf_seite"] = 2
            da.add((art, seite))
            liste = e["bearbeitungen"]
            pos = len(liste)
            if art != "kontur":           # Kontur (Ausschneiden) ans Ende, sonst davor
                for i, b in enumerate(liste):
                    if b["art"] == "kontur":
                        pos = i
                        break
            liste.insert(pos, neu)
    return e


def namen(e: Dict) -> Dict[str, str]:
    """Anzeigename je Bearbeitung: «Tasche», bei mehreren «Tasche 1», «Tasche 2» …"""
    zaehler: Dict[str, int] = {}
    for b in e["bearbeitungen"]:
        zaehler[b["art"]] = zaehler.get(b["art"], 0) + 1
    nr: Dict[str, int] = {}
    aus = {}
    for b in e["bearbeitungen"]:
        art = b["art"]
        nr[art] = nr.get(art, 0) + 1
        name = art_name(art)
        if zaehler[art] > 1:
            name += " %d" % nr[art]
        titel = str(b.get("titel") or "").strip()
        aus[b["id"]] = titel or name
    return aus


def zuordnen(e: Dict, elemente: List[Dict]) -> Dict[str, List[Dict]]:
    """Objekte den Bearbeitungen zuordnen: gleiche Fräsart, nächste Farbe,
    dazu die Bearbeitungen aus hirch:auch.
    elemente: [{"art", "rgb", "auch", …}] → {Bearbeitungs-ID: [Element, …]}"""
    je_art: Dict[tuple, List[Dict]] = {}
    for b in e["bearbeitungen"]:
        je_art.setdefault((b["art"], auf_seite(e, b)), []).append(b)
    aus: Dict[str, List[Dict]] = {b["id"]: [] for b in e["bearbeitungen"]}
    zwei = zweiseitig_an(e)
    for el in elemente:
        kandidaten = je_art.get((el.get("art"), int(el.get("seite") or 1) if zwei else 1))
        if kandidaten:
            beste = min(kandidaten,
                        key=lambda b: abstand(rgb_von(b["farbe"]) or (0, 0, 0), el["rgb"]))
            # duplizierte Bearbeitungen (gleiche Farbe) fräsen dieselben Objekte
            ton = rgb_von(beste["farbe"])
            for b in kandidaten:
                if b is beste or (ton is not None and rgb_von(b["farbe"]) == ton):
                    aus[b["id"]].append(el)
        for bid in el.get("auch", ()):
            if bid in aus and not any(x is el for x in aus[bid]):
                aus[bid].append(el)
    return aus


def zusaetzlich(e: Dict, elemente: List[Dict]) -> Dict[str, int]:
    """Anzahl Objekte je Bearbeitung, die nur über hirch:auch dazugehören."""
    zu = zuordnen(e, [dict(el, auch=[]) for el in elemente])
    farbe = {bid: {el["id"] for el in els} for bid, els in zu.items()}
    n: Dict[str, int] = {}
    for el in elemente:
        for bid in el.get("auch", ()):
            if bid in farbe and el["id"] not in farbe[bid]:
                n[bid] = n.get(bid, 0) + 1
    return n


NULLPUNKT_NAMEN = {"ul": "unten links", "um": "unten Mitte", "ur": "unten rechts",
                   "ml": "Mitte links", "mm": "Mitte", "mr": "Mitte rechts",
                   "ol": "oben links", "om": "oben Mitte", "or": "oben rechts"}


def rund(e: Dict) -> Dict:
    """Einstellungen für rundes Material (mit Vorgaben)."""
    r = dict(STANDARD["rund"])
    r.update(e.get("rund") or {})
    return r


def rund_an(e: Dict) -> bool:
    return bool(rund(e).get("an"))


def nullpunkt(e: Dict, breite: float, hoehe: float) -> Dict:
    """Nullpunkt in Materialkoordinaten (mm, von unten links): x, y und ob Z0
    auf der Unterseite liegt. Rundes Material: immer die Mitte (Achse)."""
    n = dict(STANDARD["nullpunkt"])
    n.update(e.get("nullpunkt") or {})
    if rund_an(e):
        n.update(xy="mm", dx=0.0, dy=0.0)
    xy = str(n.get("xy") or "ul")
    fy = {"u": 0.0, "m": 0.5, "o": 1.0}.get(xy[:1], 0.0)
    fx = {"l": 0.0, "m": 0.5, "r": 1.0}.get(xy[1:2], 0.0)
    return {"x": fx * breite + float(n.get("dx") or 0), "y": fy * hoehe + float(n.get("dy") or 0),
            "z_unten": n.get("z") == "unten"}


def auftrag(e: Dict, zuordnung: Dict[str, List[Dict]], dicke: float, ausgabe: str,
            flaeche: Optional[tuple] = None, grundplatte: Optional[Dict] = None) -> Dict:
    """Auftrag für hirch_cam.engine aus Einstellungen und zugeordneten Objekten.
    flaeche: (Breite, Höhe) des Materials – für Bearbeitungen ohne Objekte (Planfräsen).
    grundplatte: beim Rechnen der Spannvorrichtung (Platte 0) mit rundem Material
    {"mitte": [x, y], "durchmesser": D} – Lage des Teils auf der Vorrichtung."""
    from . import werkzeugdb
    name = namen(e)
    maschine = werkzeugdb.laden()["maschine"]
    a = {
        "werkzeuge": werkzeugdb.wirksam(copy.deepcopy(e["werkzeuge"]),
                                        e.get("material", werkzeugdb.STANDARD_MATERIAL), maschine),
        "material": e.get("material", werkzeugdb.STANDARD_MATERIAL),
        "maschine": maschine,
        "hoehen": dict(e["hoehen"], materialdicke=dicke),
        "toleranz": float(e.get("toleranz", 0.01)),
        "postprozessor": e.get("postprozessor", "richauto_a11"),
        "boegen": bool(e.get("boegen", False)),
        "rechenkern": e.get("rechenkern", "auto"),
        "reihenfolge": "liste",          # es gilt die Reihenfolge der Liste
        "zweiseitig": dict(e.get("zweiseitig") or {}),
        "ausgabe": ausgabe,
        **({"umspannen": dict(e["umspannen"])} if isinstance(e.get("umspannen"), dict) else {}),
        "bearbeitungen": [],
    }
    z = dict(STANDARD["zweiseitig"])
    z.update(e.get("zweiseitig") or {})
    if rund_an(e):
        # rund: die Schraube durch die Mitte hält das Teil mittig – keine Passlöcher
        z["passloecher"] = False
        a["zweiseitig"]["passloecher"] = False
        if grundplatte is not None:
            if rund(e).get("grundplatte", True):
                a["bearbeitungen"] += _grundplatte(e, a, grundplatte, dicke)
        elif flaeche:
            ru = rund(e)
            d_ = min(float(flaeche[0]), float(flaeche[1]))
            a["rund"] = {"mitte": [float(flaeche[0]) / 2, float(flaeche[1]) / 2], "radius": d_ / 2,
                         "schraube": bool(ru.get("schraube", True)),
                         "bohrung": float(ru.get("bohrung") or 0), "kopf": float(ru.get("kopf") or 0),
                         "ueberstand": max(0.0, float(ru.get("ueberstand") or 0))}
            if ru.get("kerbe"):
                a["bearbeitungen"].append(_kerbe(e, a, flaeche))
    if zweiseitig_an(e) and z.get("passloecher") and flaeche:
        # Passlöcher zuerst auf Seite 1: durch das Werkstück bis ins Opferbrett
        import math
        r = float(z.get("pass_d") or 6.0) / 2
        kreise = [{"punkte": [[x + r * math.cos(2 * math.pi * i / 72), y + r * math.sin(2 * math.pi * i / 72)]
                              for i in range(73)], "geschlossen": True}
                  for x, y in passloecher(e, flaeche[0], flaeche[1])]
        slot = pass_werkzeug(e, a["werkzeuge"], float(dicke) + float(z.get("pass_extra") or 3.0))
        a["bearbeitungen"].append({
            "id": PASS_ID, "art": "bohren", "werkzeug": slot, "auf_seite": 1,
            "tiefe": float(dicke) + float(z.get("pass_extra") or 3.0), "zustellung": 3.0,
            "methode": "auto", "gleichlauf": True, "passloecher": True,
            "passloch_text": ("%d × Ø %g mm frei platziert" if z.get("pass_lage") == "frei" and z.get("pass_punkte")
                              else "%d × Ø %g mm in den Ecken" if z.get("pass_lage") == "ecken"
                              else "%d × Ø %g mm auf der Wendeachse") % (len(kreise), 2 * r),
            "name": "Passlöcher (Bohren)", "anzeigefarbe": PASS_FARBE, "objekte": kreise})
    for b in e["bearbeitungen"]:
        if not b.get("aktiv", True):
            continue
        # eid: Objekt in der Zeichnung (für Hinweise, die es zeigen)
        objekte = [dict(lin, eid=el.get("id"), **({"flaechen": True} if el.get("flaechen") else {}))
                   for el in zuordnung.get(b["id"], []) for lin in el["linien"]]
        bereich_objekte = objekte if b["art"].startswith("relief_") else []   # 3D: Linien = Bereich
        if b["art"] in OHNE_OBJEKTE and flaeche:
            w_, h_ = flaeche
            objekte = [{"punkte": [[0, 0], [w_, 0], [w_, h_], [0, h_], [0, 0]], "geschlossen": True}]
        if not objekte:
            continue
        p = {k: v for k, v in b.items() if k not in ("aktiv", "farbe")}
        p["auf_seite"] = auf_seite(e, b)
        if b["art"] == "kontur" and not p.get("tiefe"):
            p["tiefe"] = float(dicke) + 0.2       # ganz durch (Opferplatte)
            p["durch"] = True
        p["name"] = "%s (%s)" % (name[b["id"]], art_zusatz(b["art"]))
        p["anzeigefarbe"] = b.get("farbe") or ""      # nur für die Vorschau
        p["objekte"] = objekte
        if bereich_objekte:
            p["bereich_objekte"] = bereich_objekte
        a["bearbeitungen"].append(p)
    return a


KERBE_ID = "kerbe"


def _kerbe(e: Dict, a: Dict, flaeche) -> Dict:
    """Kerbe am Rand bei X+ (rundes Material): eine kurze Nut quer über den Rand, 2 mm tief –
    zeigt nach dem Wenden, wie das Teil gedreht war. Fräst mit dem kleinsten Fräser."""
    cx, cy = float(flaeche[0]) / 2, float(flaeche[1]) / 2
    R = min(float(flaeche[0]), float(flaeche[1])) / 2
    ws = a["werkzeuge"]
    slot = min(ws, key=lambda k: (ws[k].get("typ") not in ("v", "schaft", "kugel"),
                                  float(ws[k].get("durchmesser") or 99))) if ws else "C"
    r_w = float((ws.get(slot) or {}).get("durchmesser") or 3) / 2
    return {"id": KERBE_ID, "art": "gravur", "werkzeug": slot, "auf_seite": 1, "tiefe": 2.0,
            "zustellung": 2.0, "name": "Kerbe zum Wenden (Gravur)", "anzeigefarbe": "#ff8c00",
            "objekte": [{"punkte": [[cx + R - 4.0, cy], [cx + R + r_w + 1.0, cy]], "geschlossen": False}]}


GRUND_BOHRUNG_ID, GRUND_RING_ID, GRUND_MARKE_ID = "grund_bohrung", "grund_ring", "grund_marke"


def _grundplatte(e: Dict, a: Dict, gp: Dict, dicke: float) -> List[Dict]:
    """Grundplatte für rundes Material (auf der Spannvorrichtung): flacher Ring Ø + 0,2 mm, in dem
    das Teil mittig steht, Mittelbohrung für die Schraube ganz durch, Marke bei X+ (zur Kerbe)."""
    import math
    ru = rund(e)
    cx, cy = (float(v) for v in gp["mitte"][:2])
    R = float(gp["durchmesser"]) / 2 + 0.1
    ws = a["werkzeuge"]

    def kreis(r, n=144):
        return {"punkte": [[cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n)]
                           for i in range(n + 1)], "geschlossen": True}

    def d_(k):
        return float(ws[k].get("durchmesser") or 3)

    fraeser = [k for k in ws if ws[k].get("typ") in ("schaft", "kugel", "eckradius")]
    aus = []
    tiefe = max(0.2, float(ru.get("ring_tiefe") or 1.0))
    passend = [k for k in fraeser if d_(k) <= R]
    if passend:
        slot = max(passend, key=d_)
        aus.append({"id": GRUND_RING_ID, "art": "tasche", "werkzeug": slot, "auf_seite": 1, "tiefe": tiefe,
                    "zustellung": tiefe, "bahnabstand": 40, "gleichlauf": True, "aufmass": 0.0,
                    "name": "Grundplatte: Ring Ø %g mm (Tasche)" % round(2 * R, 1),
                    "anzeigefarbe": "#3584e4", "objekte": [kreis(R)]})
    b = float(ru.get("bohrung") or 0)
    if ru.get("schraube", True) and b > 0:
        durch = float(dicke) + 0.2

        def sl(k):
            return float(ws[k].get("schneidenlaenge") or 0)
        bohrer = [k for k in ws if ws[k].get("typ") == "bohrer" and abs(d_(k) - b) <= 0.3]
        kleiner = [k for k in fraeser if d_(k) <= 0.9 * b]
        # zuerst, was ganz durch kommt (Schneide lang genug), sonst die längste Schneide – so tief es geht
        lang_b = [k for k in bohrer if sl(k) >= durch]
        lang_f = [k for k in kleiner if sl(k) >= durch]
        wahl = (lang_b[:1] or ([max(lang_f, key=d_)] if lang_f else [])
                or ([max(bohrer or kleiner, key=lambda k: (sl(k), d_(k)))] if bohrer or kleiner else []))
        if wahl:
            slot = wahl[0]
            tiefe_b = durch if sl(slot) <= 0 or sl(slot) >= durch else max(1.0, sl(slot) - 0.5)
            name = "Grundplatte: Mittelbohrung Ø %g mm (Bohren)" % b
            if tiefe_b < durch:
                name = ("Grundplatte: Mittelbohrung Ø %g mm, %g mm tief – Rest von Hand durchbohren (Bohren)"
                        % (b, round(tiefe_b, 1)))
            aus.append({"id": GRUND_BOHRUNG_ID, "art": "bohren", "werkzeug": slot, "auf_seite": 1,
                        "tiefe": tiefe_b, "zustellung": 2.0, "methode": "auto", "gleichlauf": True,
                        "spitze_dazu": True, "durch": tiefe_b >= durch, "name": name,
                        "anzeigefarbe": "#1a5fb4", "objekte": [kreis(b / 2, 72)]})
    if ru.get("kerbe") and ws:
        slot = min(ws, key=lambda k: (ws[k].get("typ") not in ("v", "schaft", "kugel"), d_(k)))
        aus.append({"id": GRUND_MARKE_ID, "art": "gravur", "werkzeug": slot, "auf_seite": 1, "tiefe": 0.5,
                    "zustellung": 0.5, "name": "Grundplatte: Marke X+ (Gravur)", "anzeigefarbe": "#ff8c00",
                    "objekte": [{"punkte": [[cx + R + 1.5, cy], [cx + R + 8.0, cy]], "geschlossen": False}]})
    return aus


def vorrichtung(e: Dict) -> Dict:
    """Einstellungen der Spannvorrichtung (mit Vorgaben)."""
    v = dict(STANDARD["vorrichtung"])
    v.update(e.get("vorrichtung") or {})
    return v


def pass_werkzeug(e: Dict, werkzeuge: Dict, tiefe: float) -> str:
    """Werkzeug der Passlöcher: das gewählte; ohne Wahl (Peter, 21.10.: «gar nicht dazu gekommen, ein anderes
    Werkzeug auszusuchen») ein Bohrer im Ø des Lochs, sonst ein Fräser, der tief genug schneidet und ins Loch
    passt, sonst der erste."""
    z = dict(STANDARD["zweiseitig"])
    z.update(e.get("zweiseitig") or {})
    gewaehlt = z.get("pass_werkzeug") or ""
    if gewaehlt in werkzeuge:
        return gewaehlt
    d = float(z.get("pass_d") or 6.0)

    def lang(w):
        sl = float(w.get("schneidenlaenge") or 0)
        return sl <= 0 or sl + 1e-6 >= tiefe

    bohrer = [k for k, w in werkzeuge.items() if w.get("typ") == "bohrer" and abs(float(w.get("durchmesser") or 0) - d) < 0.05]
    for k in sorted(bohrer, key=lambda k: not lang(werkzeuge[k])):
        return k
    fraeser = [k for k, w in werkzeuge.items() if w.get("typ") in ("schaft", "kugel", "eckradius")
               and float(w.get("durchmesser") or 0) <= d + 1e-6 and lang(w)]
    if fraeser:
        return max(fraeser, key=lambda k: float(werkzeuge[k].get("durchmesser") or 0))
    return "C" if "C" in werkzeuge else next(iter(werkzeuge), "C")


def vorrichtung_mittig(e: Dict) -> bool:
    """Liegt das Werkstück mittig in der Vorrichtung? Ältere Zeichnungen ohne Angabe: nur, wenn die Lage
    noch die Vorgabe 50/50 ist (eine selbst eingetragene Lage gilt weiter)."""
    m = (e.get("vorrichtung") or {}).get("mittig")
    if m is None:
        return [float(x) for x in (vorrichtung(e).get("lage") or [50, 50])[:2]] == [50.0, 50.0]
    return bool(m)


def vorrichtung_lage(e: Dict, vorrichtung_mm, werkstueck_mm) -> tuple:
    """Linke untere Ecke des Werkstücks auf der Vorrichtung (mm). «mittig» (ab Werk): in ihrer Mitte."""
    v = vorrichtung(e)
    if vorrichtung_mittig(e):
        return ((float(vorrichtung_mm[0]) - float(werkstueck_mm[0])) / 2,
                (float(vorrichtung_mm[1]) - float(werkstueck_mm[1])) / 2)
    lx, ly = (float(x) for x in (v.get("lage") or [0, 0])[:2])
    return lx, ly


def vorrichtung_an(e: Dict) -> bool:
    return bool(vorrichtung(e).get("an"))
