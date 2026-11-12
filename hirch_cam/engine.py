# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Rechenkern: Auftrag (JSON) -> .tap-Datei + Bericht.

Aufruf (macht die Inkscape-Erweiterung):
    python3 -m hirch_cam.engine auftrag.json

Ausgabe auf stdout: JSON {"ok": bool, "datei": str, "bericht": [str, …]}

Aufbau des Auftrags (alle Masse in mm, Vorschübe in mm/min):
{
  "werkzeug": {"nummer", "durchmesser", "drehzahl", "vorschub", "eintauchen", "v_winkel"},
  "hoehen":   {"freifahrhoehe", "startabstand", "sicherheitshoehe", "materialdicke"},
  "gravur":   {"tiefe", "zustellung"},
  "kontur":   {"tiefe", "zustellung", "seite", "gleichlauf",
               "stege_anzahl", "stege_breite", "stege_hoehe", "stege_modus"},
  "tasche":   {"tiefe", "zustellung", "bahnabstand", "gleichlauf"},
  "bohren":   {"tiefe", "zustellung", "methode": bohren|helix|auto, "gleichlauf", "aufmass"},
  "vcarve":   {"max_tiefe", "zustellung" (0 = ein Durchgang), "aufdicken" (mm je Seite)},
  "toleranz": 0.01,
  "ausgabe":  "/pfad/datei.tap",
  "objekte":  {"gravur": [{"punkte": [[x, y], …], "geschlossen": bool}, …], …}
}
Reihenfolge im Programm: Bohren, Gravur, V-Carve, Tasche, Kontur
(Ausschneiden immer zuletzt).
"""
from __future__ import annotations

import json
import math as _math
import os
import re as _re
import sys
from typing import Dict, List, Tuple

from .sprache import tr
from .model import Job, Toolpath
from .einstellungen import KERBE_ID, PASS_ID
from .ops.gravur import Linie, bahnlaenge, gravur

REIHENFOLGE = ["planen", "bohren", "gewinde", "gravur", "vcarve", "tasche", "kontur",
               "relief_schruppen", "boeden_waende", "relief_schlichten"]
NAMEN = {"planen": "Planfräsen (ganzes Material)", "bohren": "Bohren (Violett)", "gewinde": "Gewinde (Türkis)", "gravur": "Gravur (Orange)", "vcarve": "V-Carve (Rot)",
         "tasche": "Tasche (Grün)", "kontur": "Kontur (Blau)",
         "relief_schruppen": "3D-Schruppen (3D-Modell)", "relief_schlichten": "3D-Schlichten (3D-Modell)",
         "boeden_waende": "Böden und Wände (3D-Modell)"}


class _Teile(dict):
    """Namen der Teile einer Bearbeitung («#flach» …); «#stufe2» … : Stufe von «Böden und Wände» und ihrem
    3D-Schruppen (im Wechsel)."""

    def __missing__(self, k):
        return " Stufe " + k[5:] if str(k).startswith("stufe") else ""

    def get(self, k, standard=None):
        return self[k] if k in self or str(k).startswith("stufe") else standard


def _fraeser_laengen(w: Dict) -> Dict:
    """Form des Werkzeugs für die 3D-Ansicht: Längen (mm) – Schneide, Schaft-Ø, Übergang, wirksame Ausspannlänge
    (ohne Angabe Schneide + Übergang + 5), Gesamtlänge (0 = nicht angegeben) – und für die Spannuten die Zahl der
    Schneiden und den Drall (R aufwärts, L abwärts, K Kompression, G gerade)."""
    from .ops import relief_halterung as rh

    def z(k):
        try:
            return max(0.0, float(w.get(k) or 0.0))
        except (TypeError, ValueError):
            return 0.0
    typ = w.get("typ") or "schaft"
    # Drall wie im Fräserdialog (fraeser_merkmale): eingetragen, sonst aus dem Namen; V-Fräser gerade genutet
    drall = str(w.get("drall") or "")
    name = str(w.get("name") or "").lower()
    if not drall:
        if "abwärts" in name or "downcut" in name:
            drall = "ab"
        elif "kompression" in name or "compression" in name:
            drall = "kompression"
    zeichen = {"ab": "L", "kompression": "K"}.get(drall, "G" if typ == "v" else "R")
    try:
        schneiden = int(w.get("schneiden") or 0)
    except (TypeError, ValueError):
        schneiden = 0
    if schneiden <= 0:
        schneiden = 1 if typ == "v" and z("spitze") > 0 else 2
    return {"schneidenlaenge": z("schneidenlaenge"), "schaft": z("schaft"),
            "uebergang": round(rh.uebergang(w), 3), "ausspannlaenge": round(rh.ausspannlaenge(w), 3),
            "gesamtlaenge": z("gesamtlaenge"), "schneiden": schneiden, "drall": zeichen}


def _wechsel_paare(bearbeitungen: List[Dict]) -> Dict[str, str]:
    """«Böden und Wände» im Wechsel mit seinem 3D-Schruppen (Peter 25.10.): nur mit demselben Fräser auf derselben
    Seite, und das Schruppen steht davor. {id Böden und Wände: id Schruppen}."""
    nach_id = {b["id"]: b for b in bearbeitungen}
    aus = {}
    for i, bb in enumerate(bearbeitungen):
        if bb["art"] != "boeden_waende":
            continue
        s = nach_id.get(str(bb["p"].get("im_wechsel_mit") or ""))
        if (s and s["art"] == "relief_schruppen" and s["auf_seite"] == bb["auf_seite"]
                and s["werkzeug"] == bb["werkzeug"] and bearbeitungen.index(s) < i and not s["p"].get("rest")
                and s["objekte"] and bb["objekte"]):
            aus[bb["id"]] = s["id"]
    return aus


def _schruppen_vor(bearbeitungen: List[Dict], bb: Dict) -> Dict:
    """Das 3D-Schruppen, nach dem «Böden und Wände» fräst: das im Wechsel, sonst das letzte davor auf derselben
    Seite (ohne «nur Restmaterial»)."""
    s = next((x for x in bearbeitungen if x["id"] == str(bb["p"].get("im_wechsel_mit") or "")
              and x["art"] == "relief_schruppen" and x["auf_seite"] == bb["auf_seite"]), None)
    if s is not None:
        return s
    davor = [x for x in bearbeitungen[:bearbeitungen.index(bb)]
             if x["art"] == "relief_schruppen" and x["auf_seite"] == bb["auf_seite"] and not x["p"].get("rest")]
    return davor[-1] if davor else None


def _anzahl(n, einzahl, mehrzahl):
    return f"{n} {einzahl if n == 1 else mehrzahl}"


EILGANG = 3000.0  # mm/min, angenommen (gleich wie in der 3D-Vorschau)
_PZ_WORT = _re.compile(r"([GXYZFIJ])(-?\d*\.?\d+)")

# Fräswege der zuletzt gelaufenen Rechnung (Vorschau-Datei → Job, Materialkoordinaten): die
# Prüfung nimmt sie von hier, statt die .teile-Dateien wieder einzulesen (pruefung.py)
letzte_jobs: Dict[str, tuple] = {}      # Datei -> (Job, Grösse der Datei)


def programmzeit(text: str) -> float:
    """Geschätzte Laufzeit eines ganzen .tap-Programms in Minuten – zählt wie
    die 3D-Vorschau jede Zeile mit Bewegung (inkl. Anfang und Ende)."""
    import math
    import re
    from .post.boegen import bogenlaenge
    p = (0.0, 0.0, 20.0)
    rapid, f, t, g = True, 0.0, 0.0, 0
    finde = _PZ_WORT.findall
    for zeile in text.upper().splitlines():
        neu = {}
        ij = {}
        for w, v in finde(zeile):
            v = float(v)
            if w == "G":
                g = int(v)
                rapid = g == 0
            elif w == "F":
                f = v
            elif w in "IJ":
                ij[w] = v
            else:
                neu[w] = v
        if neu:
            q = (neu.get("X", p[0]), neu.get("Y", p[1]), neu.get("Z", p[2]))
            if g in (2, 3) and ij:
                d = bogenlaenge(p, q, p[0] + ij.get("I", 0.0), p[1] + ij.get("J", 0.0), g == 3)
            else:
                d = math.dist(p, q)
            t += d / EILGANG if rapid else (d / f if f > 0 else 0.0)
            p = q
    return t


def _zeit(minuten):
    if minuten < 1:
        return f"ca. {max(1, round(minuten * 60))} s"
    return f"ca. {minuten:.1f} min"


def _linien(objekte):
    return [([tuple(p) for p in o["punkte"]], bool(o.get("geschlossen"))) for o in objekte]


def _kontur_nachbarn(bearbeitungen: List[Dict], bb: Dict) -> List:
    """Geschlossene Linien der anderen Konturen derselben Seite – der Anfahrbogen hält Abstand zu ihnen
    (Fräsbahnen 25.10.: «Im Bogen» als Vorgabe, Teile dicht nebeneinander)."""
    aus = []
    for x in bearbeitungen:
        if x is bb or x.get("id") == bb.get("id") or x["art"] != "kontur" or x["auf_seite"] != bb["auf_seite"]:
            continue
        for o in x.get("objekte") or []:
            if o.get("geschlossen") and len(o.get("punkte") or []) >= 3:
                aus.append([tuple(q) for q in o["punkte"]])
    return aus


REIHENFOLGE_ALLE = ["planen", "bohren", "gewinde", "gravur", "vcarve_flach", "vcarve", "tasche", "kontur"]
SLOTS = ["A", "B", "C"]


def _werkzeug_setup(auftrag: Dict):
    """Werkzeuge und Zuordnung Fräsart -> Werkzeugplatz.

    Neu: "werkzeuge": {"A": {...}, "B": {...}, "C": {...}} und je Fräsart
    "werkzeug": "A". Alt (ein Werkzeug): "werkzeug": {...} für alles, der
    Flachbereich der V-Carve über vcarve.flach_durchmesser."""
    zuordnung = {}
    if "werkzeuge" in auftrag:
        werkzeuge = {k: dict(v) for k, v in auftrag["werkzeuge"].items()}
        for art in REIHENFOLGE:
            zuordnung[art] = auftrag.get(art, {}).get("werkzeug", "A")
        flach = auftrag.get("vcarve", {}).get("flach_werkzeug") or ""
        if flach:
            zuordnung["vcarve_flach"] = flach
    else:
        w = dict(auftrag["werkzeug"])
        w.setdefault("typ", "v" if (auftrag.get("objekte") or {}).get("vcarve") else "schaft")
        werkzeuge = {"A": w}
        for art in REIHENFOLGE:
            zuordnung[art] = "A"
        fd = float(auftrag.get("vcarve", {}).get("flach_durchmesser", 0) or 0)
        if fd > 0:
            werkzeuge["Flach"] = dict(w, typ="schaft", durchmesser=fd)
            zuordnung["vcarve_flach"] = "Flach"
    for k, w in werkzeuge.items():
        w.setdefault("durchmesser", 3.0)
        w.setdefault("v_winkel", 60.0)
        w.setdefault("nummer", 1)
        w.setdefault("drehzahl", 12000)
    return werkzeuge, zuordnung


def als_2d(w: Dict) -> Dict:
    """Konischer Kugelfräser in 2D-Bearbeitungen (Gravur, V-Carve, Kontur …): wie ein V-Fräser mit
    abgeflachter Spitze Ø 2·Kugelradius (3D-Bearbeitungen rechnen mit der echten Form)."""
    if not w or w.get("typ") != "konisch":
        return w
    return dict(w, typ="v", spitze=2 * float(w.get("eckradius") or 0.25), v_winkel=float(w.get("v_winkel") or 10))


def _zahl_np(v) -> str:
    """mm mit zwei Stellen und Komma (Hinweise; die Oberfläche übersetzt das Komma)."""
    return f"{float(v):.2f}".replace(".", ",")


def _ganz(v) -> str:
    """Ganze Zahl wie in der Oberfläche (C++ ganz()): Tausender mit Abstand, «14 300»."""
    n = int(round(float(v or 0)))
    return f"{n:,}".replace(",", " ")


def werkzeug_name(w: Dict, anzeige: bool = False) -> str:
    """Name für G-Code-Kopf und Bericht (Zahlen mit Punkt, wie bisher – der G-Code bleibt gleich) bzw. mit
    anzeige=True für die Oberfläche: Dezimalkomma, «Kugelfräser» wie überall (Windows-Sitzung 28.10.)."""
    def z(v: float) -> str:
        t = f"{float(v):g}"
        return t.replace(".", ",") if anzeige else t
    d = float(w["durchmesser"])
    if w.get("typ") == "konisch":
        return (f"Konischer Kugelfräser R{float(w.get('eckradius') or 0):g} · {float(w.get('v_winkel') or 0):g}° "
                f"Ø {d:g} mm").replace(".", ",")
    if w.get("typ") == "v":
        spitze = float(w.get("spitze") or 0)
        extra = f" / Spitze {spitze:g} mm".replace(".", ",") if spitze > 0 else ""
        return f"V-Fräser {z(w['v_winkel'])}°{extra} Ø {z(d)} mm"
    if w.get("typ") == "kugel":
        return f"Kugelfräser Ø {z(d)} mm" if anzeige else f"Vollradiusfräser Ø {d:g} mm"
    if w.get("typ") == "eckradius":
        return f"Eckradiusfräser Ø {z(d)} mm R{z(w.get('eckradius') or 0)}"
    if w.get("typ") == "gewinde":
        zn = int(w.get("zaehne") or 1)
        return f"Gewindefräser Ø {z(d)} mm" + (f" · {zn} Zähne" if zn > 1 else "")
    if w.get("typ") == "bohrer":
        return f"Bohrer Ø {z(d)} mm"
    return f"Schaftfräser Ø {z(d)} mm"


def _kurz(w: Dict) -> str:
    """Kurzname für Dateinamen (nur ASCII)."""
    if w.get("typ") == "v":
        return f"V{float(w['v_winkel']):g}"
    if w.get("typ") == "kugel":
        return f"K{float(w['durchmesser']):g}"
    if w.get("typ") == "konisch":
        return f"KK{2 * float(w.get('eckradius') or 0):g}"
    if w.get("typ") == "bohrer":
        return f"B{float(w['durchmesser']):g}"
    return f"D{float(w['durchmesser']):g}"




ABSTAND_SPANNMITTEL = 2.0     # mm Luft über und neben Spannmitteln


def _ueber_spannmittel(toolpaths, spann, radius: float, start) -> int:
    """Eilgänge, die (samt Fräserradius) über ein Spannmittel führen und dabei
    tiefer als dessen Oberkante + Abstand liegen, über das Spannmittel heben:
    hoch, hinüber, wieder runter. Gibt die Anzahl angehobener Eilgänge zurück.
    Die Werkzeugwege werden an Ort und Stelle geändert."""
    try:
        from shapely.geometry import LineString, Polygon
    except ImportError:
        return 0
    flaechen = []
    for s in spann:
        try:
            p = Polygon(s["punkte"])
            if not p.is_valid:
                p = p.buffer(0)
            flaechen.append((p.buffer(radius + ABSTAND_SPANNMITTEL), float(s.get("hoehe", 20))))
        except Exception:
            continue
    if not flaechen:
        return 0
    from .model import Move
    n = 0
    pos = tuple(start)
    for tp in toolpaths:
        neu = []
        for m in tp.moves:
            if m.rapid and (abs(m.x - pos[0]) > 1e-9 or abs(m.y - pos[1]) > 1e-9):
                weg = LineString([pos[:2], (m.x, m.y)])
                hoch = max((h for f, h in flaechen if f.intersects(weg)), default=None)
                if hoch is not None:
                    z = hoch + ABSTAND_SPANNMITTEL
                    if min(pos[2], m.z) < z:
                        z = max(z, pos[2])
                        neu.append(Move(True, pos[0], pos[1], z, None))
                        neu.append(Move(True, m.x, m.y, z, None))
                        n += 1
            neu.append(m)
            pos = (m.x, m.y, m.z)
        tp.moves = neu
    return n


def _verschoben(job: Job, ox: float, oy: float, oz: float, sx: float = 1.0, sy: float = 1.0) -> Job:
    """Job für die Ausgabe: Koordinaten vom Material (unten links, Z0 oben)
    auf den gewählten Nullpunkt umrechnen. Start/Ende bleiben bei X0 Y0.
    sx, sy: Weg der Achsen aus der Messkorrektur – die Koordinaten werden durch sie geteilt
    (fährt Y 0,15 % zu kurz, steht im G-Code 0,15 % mehr)."""
    if abs(ox) < 1e-12 and abs(oy) < 1e-12 and abs(oz) < 1e-12 and sx == 1.0 and sy == 1.0:
        return job
    from .model import Move
    neu = []
    for tp in job.toolpaths:
        t = Toolpath(name=tp.name)
        for m in tp.moves:
            t.moves.append(Move(m.rapid, (m.x - ox) / sx, (m.y - oy) / sy, m.z + oz, m.feed))
        for k, v in vars(tp).items():
            if k not in ("name", "moves"):
                setattr(t, k, v)
        neu.append(t)
    return Job(tool_number=job.tool_number, spindle_rpm=job.spindle_rpm, safe_z=job.safe_z + oz,
               toolpaths=neu, home=job.home)


def _farbe_art():
    from .farben import FRAESARTEN, hex_farbe
    return {art: hex_farbe(v[0]) for art, v in FRAESARTEN.items()}


FARBE_ART = _farbe_art()


def _bearbeitungen(auftrag: Dict, zuordnung: Dict) -> List[Dict]:
    """Bearbeitungen in Fräsreihenfolge: [{id, art, name, werkzeug, flach, p, objekte}].

    Neu: "bearbeitungen": [{"id", "art", "name", "werkzeug", …Werte…, "objekte"}]
    (mehrere je Fräsart möglich). Alt: je Fräsart ein Abschnitt + "objekte"
    nach Fräsart, Reihenfolge aus "reihenfolge"."""
    liste = []
    if "bearbeitungen" in auftrag:
        for i, b in enumerate(auftrag["bearbeitungen"]):
            art = b.get("art")
            if art not in REIHENFOLGE:
                continue
            p = {k: v for k, v in b.items() if k not in ("objekte", "id", "art", "name", "anzeigefarbe",
                                                         "auf_seite")}
            if p.get("zustell_modus") and "zustellung" in p:      # letzte kleiner, Liste, ein Durchgang
                from .ops.bahn import zustellung_aus
                p["zustellung"] = zustellung_aus(p, tiefe=float(p.get("max_tiefe") if art == "vcarve"
                                                                else p.get("tiefe") or 0.0))
            liste.append({"id": str(b.get("id") or f"b{i + 1}"), "art": art,
                          "name": b.get("name") or NAMEN[art], "farbe": b.get("anzeigefarbe") or "",
                          "werkzeug": b.get("werkzeug", "A"),
                          "flach": b.get("flach_werkzeug") or None,
                          "auf_seite": int(b.get("auf_seite") or 1),
                          "p": p, "objekte": b.get("objekte") or []})
        return liste
    reihenfolge = [a for a in (auftrag.get("reihenfolge") or []) if a in REIHENFOLGE]
    reihenfolge += [a for a in REIHENFOLGE if a not in reihenfolge]
    objekte = auftrag.get("objekte", {})
    for art in reihenfolge:
        if not objekte.get(art):
            continue
        liste.append({"id": art, "art": art, "name": NAMEN[art],
                      "werkzeug": zuordnung.get(art, "A"),
                      "flach": zuordnung.get("vcarve_flach") if art == "vcarve" else None,
                      "auf_seite": 1, "p": dict(auftrag.get(art, {})), "objekte": objekte[art]})
    return liste


def _stege_der_seite(auftrag: Dict, seite: int, bid: str = "") -> List:
    """Haltesteg-Marken auf dieser Seite (ohne Seitenangabe: alle). Entwurf B (25.10.): eine Marke kann zu einer
    Bearbeitung gehören (`stege_bearbeitung`, Id; leer = zu jeder Kontur) – dann nur zu dieser."""
    stege = auftrag.get("stege") or []
    seiten = auftrag.get("stege_seite")
    zu = auftrag.get("stege_bearbeitung") or []
    aus = []
    for i, s in enumerate(stege):
        if seiten and len(seiten) == len(stege) and int(seiten[i] or 1) != seite:
            continue
        if bid and i < len(zu) and zu[i] and str(zu[i]) != str(bid):
            continue
        aus.append(s)
    return aus


def raeumbreite(w: Dict, tiefe: float, zustellung: float) -> float:
    """Breite, mit der ein Werkzeug den Boden einer Tasche ausräumt: Schaftfräser
    sein Durchmesser, V-Fräser / Gravierstichel nur die Spitze (ohne Spitze: die
    Breite auf einer Zustellung). Bahnabstand und Rand der Tasche gehen davon aus."""
    if w.get("typ") != "v":
        return float(w["durchmesser"])
    spitze = float(w.get("spitze") or 0.0)
    if spitze > 0:
        return spitze
    import math
    t = max(0.01, min(tiefe, zustellung or tiefe))
    return max(0.05, min(float(w["durchmesser"]), 2 * t * math.tan(math.radians(float(w.get("v_winkel", 60)) / 2))))


SCHLICHT_FLACHER = ("Das Schlichten geht nur bis %s mm, das Schruppen bis %s mm – darunter bleibt die "
                    "Schlicht-Zugabe (%s mm) an der Wand stehen.")
STEGE_NIEDRIGER = ("Die Haltestege sind nur %s mm hoch (statt %s mm) – so viel Material lässt das Schruppen unten "
                   "stehen.")


def _mm1(x) -> str:
    return f"{float(x):.2f}".rstrip("0").rstrip(".").replace(".", ",")


# Adaptiv nur in den weiten Teilen einer Tasche (Peter 01.11.): was einen Kreis von ADAPTIV_WEIT × Ø aufnimmt; schmale
# Teile (enge Lücken) in Runden – dort ist adaptiv langsamer (Impeller Stufe 3 Ø 3: 11,3 statt 5,2 min)
ADAPTIV_WEIT = 4.0


def _schlicht_tiefe(p: Dict) -> float:
    """B-2: Endtiefe des Schlicht-Durchgangs einer Kontur – «schlicht_tiefe» (mm, positiv), fehlt sie, die Tiefe
    der Bearbeitung (wie bisher)."""
    v = p.get("schlicht_tiefe")
    try:
        v = float(v) if v not in (None, "") else 0.0
    except (TypeError, ValueError):
        v = 0.0
    return v if v > 0 else float(p["tiefe"])


def _schlicht_gleichlauf(p: Dict) -> bool:
    """Fräsrichtung beim Schlichten: eigene (schlicht_gleichlauf) oder wie die Bearbeitung –
    z. B. Schruppen im Gleichlauf, Schlichten im Gegenlauf (Peter, 20.10.)."""
    v = p.get("schlicht_gleichlauf")
    return bool(p.get("gleichlauf", True)) if v is None or v == "" else bool(v)


def _vorschub_faktor(prozent) -> float:
    """Vorschub-Faktor (Prozent, in der Zeitleiste gezogen) – 20 … 400 %."""
    try:
        f = float(prozent) / 100.0 if prozent not in (None, "") else 1.0
    except (TypeError, ValueError):
        return 1.0
    return max(0.2, min(4.0, f)) if f > 0 else 1.0


def _mit_vorschub(w: Dict, prozent, mm=None) -> Dict:
    """Werkzeug mit dem Vorschub einer einzelnen Bearbeitung: «mm» in mm/min
    (so wird er im Dialog eingegeben) oder – ältere Zeichnungen – Prozent vom
    Werkzeug; das Werkzeug selbst bleibt unverändert."""
    try:
        if mm not in (None, "") and float(mm) > 0:
            w = dict(w)
            w["vorschub"] = round(float(mm), 1)
            return w
    except (TypeError, ValueError):
        pass
    try:
        f = float(prozent) / 100.0 if prozent not in (None, "") else 1.0
    except (TypeError, ValueError):
        f = 1.0
    if abs(f - 1.0) < 1e-9 or f <= 0:
        return w
    w = dict(w)
    w["vorschub"] = round(float(w["vorschub"]) * f, 1)
    return w


def _bahnen_2d(tp, raster: float = 0.001) -> List[List[float]]:
    """Fräsbahnen von oben gesehen: Züge [x0, y0, x1, y1, …] in mm (nur
    Vorschub, Eilgänge trennen die Züge). Ein Schnitt, der genau an der Oberfläche
    endet (V-Carve: Ast in eine Ecke, Z geht auf 0), gehört noch zum Zug."""
    zuege: List[List[float]] = []
    aktuell: List[float] = []
    pos = None
    tief = False                           # die letzte Position lag im Material

    def anhaengen(x, y):
        nonlocal aktuell
        x, y = round(x / raster) * raster, round(y / raster) * raster
        if len(aktuell) < 2 or abs(aktuell[-2] - x) > 1e-9 or abs(aktuell[-1] - y) > 1e-9:
            aktuell += [x, y]

    for m in tp.moves:
        if m.rapid or m.z >= 0:
            if tief and not m.rapid and aktuell:
                anhaengen(m.x, m.y)        # Schnitt aus dem Material bis an die Oberfläche
            if len(aktuell) >= 2:          # 2 Werte = nur gebohrt (ein Punkt)
                zuege.append(aktuell)
            aktuell = []
            tief = False
        else:
            if not aktuell and pos is not None:
                anhaengen(pos[0], pos[1])
            anhaengen(m.x, m.y)
            tief = True
        pos = (m.x, m.y)
    if len(aktuell) >= 2:
        zuege.append(aktuell)
    # Jeder Zug nur einmal: dieselbe Bahn in mehreren Zustellungen liegt von oben gesehen genau
    # übereinander (Peter, 23.10.: Impeller 68 000 Punkte auf der Zeichenfläche, meist doppelt)
    aus, gesehen = [], set()
    for z in zuege:
        z = [round(v, 3) for v in z]
        k = tuple(round(v, 2) for v in z)
        if k in gesehen:
            continue
        gesehen.add(k)
        aus.append(_anzeige_vereinfacht(z))
    return aus


def _anzeige_vereinfacht(z: List[float], tol: float = 0.03) -> List[float]:
    """Zug für die Anzeige auf der Zeichenfläche ausdünnen (Douglas-Peucker, 0,03 mm – kleiner als ein
    Bildpunkt bei starkem Zoom): weniger Punkte, die bei jedem Neuzeichnen gemalt werden müssen."""
    n = len(z) // 2
    if n < 4:
        return z
    import numpy as np
    pts = np.asarray(z, float).reshape(n, 2)
    from .ops.bahn import _behalten_2d
    keep = _behalten_2d(pts, tol)
    if keep is not None:
        return [round(float(v), 3) for v in pts[keep].ravel()]
    keep = np.zeros(n, bool)
    keep[[0, -1]] = True
    stapel = [(0, n - 1)]
    while stapel:
        a, e = stapel.pop()
        if e <= a + 1:
            continue
        d = pts[e] - pts[a]
        L = float(d @ d)
        Q = pts[a + 1:e] - pts[a]
        if L > 1e-18:
            t = np.clip(Q @ d / L, 0, 1)
            abst = np.linalg.norm(Q - np.outer(t, d), axis=1)
        else:
            abst = np.linalg.norm(Q, axis=1)
        k = int(abst.argmax())
        if abst[k] > tol:
            keep[a + 1 + k] = True
            stapel += [(a, a + 1 + k), (a + 1 + k, e)]
    return [round(float(v), 3) for v in pts[keep].ravel()]


def _neben_rund(tp: Toolpath, auftrag: Dict, d_w: float) -> bool:
    """Ob eine Bahn unter der Oberfläche ganz neben dem runden Material liegt (der Fräser
    berührt den Kreis dort nicht mehr)."""
    ru = auftrag.get("rund") or {}
    if not ru:
        return False
    cx, cy = (float(v) for v in ru.get("mitte") or (0, 0))
    grenze = float(ru.get("radius") or 0) + d_w / 2 + 0.5
    return any(not m.rapid and m.z < -1e-6 and _math.hypot(m.x - cx, m.y - cy) > grenze for m in tp.moves)


def _ab_stufe(tp, starttiefe: float, freifahrhoehe: float) -> None:
    """Gravur und V-Carve auf einer tieferen Stufe (Peter, 23.10.2026): alle Bewegungen unterhalb der
    Freifahrhöhe um «Beginnt bei Tiefe» absenken – auch das Anfahren auf den Startabstand, der dann knapp
    über der Stufe liegt (wie bei Kontur und Tasche). Die Freifahrhöhe bleibt."""
    if not tp or starttiefe <= 1e-9:
        return
    for m in tp.moves:
        if m.z < freifahrhoehe - 1e-6:
            m.z -= starttiefe


def _durch_mit_gegenseite(auftrag: Dict, linien, tiefe: float, dicke, seite: int, schnitte_je: Dict) -> bool:
    """Zweiseitig: trennt diese Kontur das Teil zusammen mit einem Schnitt der anderen Seite heraus?
    (Peter, 23.10.: Haltestege waren bei Seite 2 ausgegraut, obwohl erst beide Seiten zusammen
    durchschneiden.) Schnitte der anderen Seite: [(Fläche, Tiefe)] in deren Koordinaten, gespiegelt an
    der Wendeachse (wie pruefung.py)."""
    if not dicke or tiefe >= float(dicke) - 1e-9:
        return False
    andere = schnitte_je.get(3 - seite) or []
    flaeche = auftrag.get("material_flaeche")
    if not andere or not flaeche:
        return False
    try:
        from shapely.affinity import affine_transform
        from shapely.geometry import LineString
        from shapely.ops import unary_union
    except ImportError:
        return False
    breite, hoehe = float(flaeche[0]), float(flaeche[1])
    um_y = (auftrag.get("zweiseitig") or {}).get("achse") == "y"
    spiegel = [-1, 0, 0, 1, breite, 0] if um_y else [1, 0, 0, -1, 0, hoehe]
    teile = [LineString(list(pts) + ([pts[0]] if zu and len(pts) > 2 else [])) for pts, zu in linien if len(pts) >= 2]
    if not teile:
        return False
    weg = unary_union(teile)
    for g, t in andere:
        if tiefe + float(t) < float(dicke) - 0.01 or g is None or g.is_empty:
            continue
        gg = affine_transform(g, spiegel)
        if gg.buffer(0.05).covers(weg) or weg.intersection(gg.buffer(0.05)).length > 0.5 * weg.length:
            return True
    return False


def _modell_raster(auftrag, seite: int, bestand, dicke: float, plan_tiefe: float):
    """Oberfläche der 3D-Modelle einer Seite auf den Rasterpunkten des Bestands (−inf ohne Modell), um die
    Plantiefe verschoben wie die Bahnen. Zweiseitig zählen Boden und Haltestege mit (das 3D-Schlichten tastet sie
    ebenso ab); der Boden reicht hier weniger weit als beim Schlichten – darüber hinaus zählt er als stehendes
    Material (sicher). None, wenn es nicht geht (dann wie bisher)."""
    try:
        import numpy as np
        from .ops import relief3d
        from . import hirch_relief
        mf = auftrag.get("material_flaeche") or (0, 0)
        tri, extra, _g = relief3d._modelle(auftrag, {}, seite, (float(mf[0]), float(mf[1]), dicke), 0.0, [])
        if tri is None:
            return None
        if extra is not None and len(extra):
            tri = np.concatenate([tri, extra])
        oben, _u, _a = hirch_relief.hoehenkarten(np.ascontiguousarray(tri, np.float32), float(bestand.x0),
                                                  float(bestand.y0), float(bestand.z), int(bestand.nx), int(bestand.ny))
        m = np.where(np.isfinite(oben), oben - plan_tiefe, -np.inf).astype(np.float32)
        return m if m.shape == bestand.H.shape else None
    except Exception:
        return None


def _ueber_bestand(folge, nach_key, werkzeuge, bearbeitungen, auftrag, cz: float, sz: float, home,
                   plan=None) -> int:
    """Übergänge über dem, was nach den Programmen davor (Fräsreihenfolge, je Seite) wirklich steht
    (ops/freifahren.ueber_bestand; 3D → Fräsbahnen 25.10. 20:06). Ersetzt relief3d.tief_uebergaenge im
    3D-Schlichten und gilt auch für 2D. Was sich nicht sicher simulieren lässt (Gewinde, Bohrzyklen), zählt als
    stehend; Planfräsen trägt nur ab (es regelt seine Übergänge selbst)."""
    from . import speicher as _speicher
    from .ops import freifahren
    from .ops.relief3d import _werkzeug_form
    try:
        from . import hirch_relief
        if not hasattr(hirch_relief, "abtragen"):
            return -1
    except ImportError:
        return -1                      # Modul fehlt: das Sicherheitsnetz lief nicht (Ergebnis: «sicherheitsnetz_aus»)
    arten = {b["id"]: b["art"] for b in bearbeitungen}
    dicke = float((auftrag.get("hoehen") or {}).get("materialdicke") or 18.0)
    h_frei = freifahren.hoehe(sz, cz)
    # Ausserhalb des Rohteils ist Luft – aber nur, wo Peter es gesagt hat: eine 3D-Bearbeitung der Seite fährt «über
    # den Materialrand hinaus» (Block so gross wie das Modell). Sonst kann das echte Rohteil grösser sein als die
    # Zeichenfläche; dann zählt es dort weiter als Material bis Z 0 (sicher).
    from .ops.relief3d import ueber_rand
    rand_luft = {b["auf_seite"] for b in bearbeitungen if b.get("objekte") and ueber_rand(b["p"]) > 0}
    geaendert = 0
    for seite in sorted({a["seite"] for a in folge}):
        keys = [k for a in folge if a["seite"] == seite for k in a["keys"]]
        pts = [(m.x, m.y) for k in keys for m in nach_key[k]["tp"].moves]
        if not pts:
            continue
        # Seite unverändert (gleiche Bahnen, Werkzeuge, Einstellungen): das Ergebnis vom letzten Mal nehmen – eine
        # Änderung auf Seite 2 simuliert Seite 1 nicht noch einmal (Peter 30.10., B-7)
        kennung = _ueber_kennung(seite, keys, nach_key, werkzeuge, arten, auftrag, dicke, h_frei, cz, sz, home,
                                 seite in rand_luft, float((plan or {}).get(seite, 0.0)))
        gemerkt = _UEBER_MERK.get(kennung)
        if gemerkt is not None:
            n_alt, bahnen_alt = gemerkt
            for k in keys:
                if k in bahnen_alt:
                    nach_key[k]["tp"] = _speicher.bahn_kopie(bahnen_alt[k])
            geaendert += n_alt
            continue
        n_vorher = geaendert
        rmax = max(float(werkzeuge[nach_key[k]["slot"]].get("durchmesser") or 6) for k in keys
                   if nach_key[k]["slot"] in werkzeuge) / 2 if keys else 3.0
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        bestand = freifahren.Bestand(min(xs) - rmax - 2, min(ys) - rmax - 2, max(xs) + rmax + 2,
                                     max(ys) + rmax + 2, -dicke - 1.0,
                                     material=auftrag.get("material_flaeche") if seite in rand_luft else None)
        # 3D-Schlichten: Oberfläche des Modells je Rasterpunkt – senkrecht hinunter auf einen Bahnpunkt zählt nur, was
        # darüber noch steht (Main-Sitzung 27.10.: «kombiniert» tauchte 99 × ab Z 0,5 durch Geräumtes).
        if any(arten.get(nach_key[k].get("bid"), nach_key[k].get("art")) == "relief_schlichten" for k in keys):
            bestand.modell = _modell_raster(auftrag, seite, bestand, dicke, float((plan or {}).get(seite, 0.0)))
        hind = {}
        for k in keys:
            t = nach_key[k]
            w = werkzeuge.get(t["slot"])
            art = arten.get(t.get("bid"), t.get("art"))
            if w is None or art == "gewinde" or w.get("typ") == "gewinde" or t["tp"].zyklen:
                continue                                     # zählt als stehend (sicher)
            form = _werkzeug_form(w)
            # 3D-Schruppen: Abheben zwischen Teilstücken und Ebenen nur so hoch wie nötig (3D → Fräsbahnen 26.10.
            # 19:05, Punkt 2 – am Impeller 8,5 m Eilgang in Z); hinunter bleibt höchstens wie vom Schruppen geplant
            anpassen = art != "planen"
            hub_max = cz if art in ("relief_schlichten", "relief_schruppen") else h_frei
            d_w = float(w.get("durchmesser") or 0)
            if d_w not in hind:
                hind[d_w] = freifahren.hindernisse(auftrag, seite, d_w)
            kr, fl = hind[d_w]
            tp = _speicher.bahn_kopie(t["tp"]) if anpassen else t["tp"]
            n = freifahren.ueber_bestand(tp, bestand, form, cz, sz, home, hub_max, kr, fl, anpassen=anpassen,
                                         eintauchen=float(w.get("eintauchen") or 0) or None,
                                         modell_frei=art == "relief_schlichten" and bestand.modell is not None)
            if n:
                t["tp"] = tp
                geaendert += n
        _UEBER_MERK[kennung] = (geaendert - n_vorher, {k: _speicher.bahn_kopie(nach_key[k]["tp"]) for k in keys})
        while len(_UEBER_MERK) > 8:
            _UEBER_MERK.pop(next(iter(_UEBER_MERK)))
    return geaendert


_UEBER_MERK: Dict[str, Tuple] = {}     # Kennung einer Seite → (geänderte Übergänge, Bahnen danach), im Dienst


OHNE_MITTE = ("{}: hat keinen Mittenschnitt, taucht aber an {} Stelle(n) senkrecht ins Material ein – dort eine Rampe "
              "oder Helix wählen oder einen Fräser mit Mittenschnitt nehmen.")


OHNE_MITTE_TYPEN = ("schaft", "kugel", "eckradius", "konisch")
OHNE_MITTE_STEIL = 30.0       # Grad: steiler als das zählt für einen Fräser ohne Mittenschnitt als Eintauchen
OHNE_MITTE_TAN = _math.tan(_math.radians(OHNE_MITTE_STEIL))


def _ohne_mitte_lauf(lauf, h_anfang: float, r: float, key, slot, funde) -> None:
    """Ein Stück hinunter laufender Vorschübe (Helix, Rampe), dessen Mitte die ganze Zeit nahe beisammen bleibt
    (Abstand zur Mitte des Stücks < OHNE_MITTE_KERN · r) und dabei ins Material taucht: für einen Fräser ohne
    Mittenschnitt ein Eintauchen (Fable 06.11.: Helix mit 0,1 mm Radius, Loch Ø 6,2 mit Ø 6 – in der Mitte bleibt ein
    Zapfen, auf den der Fräser drückt). Die Simulation kennt den toten Kern nicht, darum über die Form der Bahn."""
    if len(lauf) < 3:
        return
    mx = sum(p[0] for p in lauf) / len(lauf)
    my = sum(p[1] for p in lauf) / len(lauf)
    weit = max(_math.hypot(p[0] - mx, p[1] - my) for p in lauf)
    z_ende = lauf[-1][2]
    if weit < OHNE_MITTE_KERN * r and h_anfang > z_ende + 0.1:
        funde.append({"key": key, "werkzeug": slot, "x": mx, "y": my, "tief": h_anfang - z_ende})


OHNE_MITTE_KERN = 0.25        # · Radius: so nahe bleibt die Mitte einer Helix, die für den toten Kern ein Eintauchen ist


def _eintauchen_ohne_mitte(folge, nach_key, werkzeuge, auftrag, dicke: float) -> List[Dict]:
    """Zufallstests 06.11. (Peter: «Prüfung + Hinweis»): ein Fräser ohne Mittenschnitt («mittenschnitt»: False), der
    senkrecht im Vorschub in stehendes Material taucht (Bohrzyklen stehen als Bewegungen in der Bahn). Simuliert je Seite in
    Fräsreihenfolge (ops/freifahren.Bestand, ausserhalb des Rohteils Luft). Liefert je Stelle {key, werkzeug, x, y,
    tief}; leer, wenn kein solcher Fräser vorkommt oder das Modul fehlt."""
    ohne = {s for s, w in werkzeuge.items() if not w.get("mittenschnitt", True)
            and (w.get("typ") or "schaft") in OHNE_MITTE_TYPEN}
    if not ohne or not any(a["slot"] in ohne for a in folge):
        return []
    try:
        from . import hirch_relief
        if not hasattr(hirch_relief, "abtragen"):
            return []
    except ImportError:
        return []
    from .ops import freifahren
    from .ops.relief3d import _werkzeug_form
    funde: List[Dict] = []
    mat = auftrag.get("material_flaeche")
    sicher = float(((auftrag.get("hoehen") or {}).get("sicherheitshoehe")) or 20.0)
    for seite in sorted({a["seite"] for a in folge}):
        abs_ = [a for a in folge if a["seite"] == seite]
        pts = [(m.x, m.y) for a in abs_ for k in a["keys"] for m in nach_key[k]["tp"].moves]
        if not pts:
            continue
        rmax = max(float((werkzeuge.get(a["slot"]) or {}).get("durchmesser") or 6) for a in abs_) / 2
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        best = freifahren.Bestand(min(xs) - rmax - 2, min(ys) - rmax - 2, max(xs) + rmax + 2, max(ys) + rmax + 2,
                                  -dicke - 1.0, material=mat)
        for a in abs_:
            w = werkzeuge.get(a["slot"]) or {}
            if w.get("typ") == "gewinde":
                continue                                 # schneidet nur die Gänge (trägt im Raster nichts ab)
            form = _werkzeug_form(w)
            r = float(w.get("durchmesser") or 6) / 2
            pruefe = a["slot"] in ohne
            for k in a["keys"]:
                tp = nach_key[k]["tp"]
                pos = (0.0, 0.0, sicher)
                seit, seit_start = [], pos
                lauf, lauf_h = [], None           # hinunter laufende Vorschübe (Helix): Punkte, Material am Anfang
                for m in tp.moves:
                    runter = pruefe and not m.rapid and m.z < pos[2] - 1e-6
                    if runter and not lauf:
                        best.abtragen(seit, form, seit_start)
                        seit, seit_start = [], pos
                        lauf, lauf_h = [pos], best.hoch_kern(pos[0], pos[1], r / 2)
                    if runter:
                        lauf.append((m.x, m.y, m.z))
                    elif lauf:
                        _ohne_mitte_lauf(lauf, lauf_h, r, k, a["slot"], funde)
                        lauf = []
                    # senkrecht – oder steiler als OHNE_MITTE_STEIL (Fable 06.11.: steile Rampe)
                    if runter and (pos[2] - m.z) > _math.hypot(m.x - pos[0], m.y - pos[1]) * OHNE_MITTE_TAN:
                        best.abtragen(seit, form, seit_start)
                        seit, seit_start = [], pos
                        h = best.hoch_kern(m.x, m.y, r / 2)
                        if h > m.z + 0.1:
                            funde.append({"key": k, "werkzeug": a["slot"], "x": m.x, "y": m.y, "tief": h - m.z})
                    seit.append(m)
                    pos = (m.x, m.y, m.z)
                if lauf:
                    _ohne_mitte_lauf(lauf, lauf_h, r, k, a["slot"], funde)
                best.abtragen(seit, form, seit_start)
    return funde


def _ueber_kennung(seite, keys, nach_key, werkzeuge, arten, auftrag, dicke, h_frei, cz, sz, home, luft, plan_z) -> str:
    """Kennung von allem, was _ueber_bestand für eine Seite liest: Bahnen in Fräsreihenfolge, Werkzeuge, Höhen,
    der Auftrag (Spannmittel, Passlöcher, Modelle) ohne die Liste der Bearbeitungen und die Ausgabe."""
    import hashlib
    import numpy as _np
    h = hashlib.sha256()
    h.update(json.dumps([seite, dicke, h_frei, cz, sz, list(home), luft, plan_z], default=str).encode())
    for k in keys:
        t = nach_key[k]
        tp = t["tp"]
        h.update(json.dumps([k, t.get("slot"), arten.get(t.get("bid"), t.get("art")), bool(tp.zyklen),
                             werkzeuge.get(t.get("slot"))], sort_keys=True, default=str).encode())
        h.update(_np.array([(m.rapid, m.x, m.y, m.z, m.feed if m.feed is not None else -1.0) for m in tp.moves],
                           _np.float64).tobytes())
    # ohne Bearbeitungen, Ausgabe und die Angaben zum Lauf («_dokument»: Pfad der Zeichnung, «nur_pruefen»)
    h.update(json.dumps({k_: v_ for k_, v_ in auftrag.items()
                         if k_ not in ("bearbeitungen", "ausgabe", "nur_pruefen") and not k_.startswith("_")},
                        sort_keys=True, default=str).encode())
    return h.hexdigest()


UMSPANNEN_FREI = "Das Teil ist nach Spannung 2 frei – Haltestege setzen oder das Teil von Hand halten."
UMSPANNEN_VIEL = "Viel Arbeit bleibt für Spannung 2 – Spannmittel anders setzen?"
UMSPANNEN_IN_ZONE = ("Ein Spannmittel der Spannung 2 liegt dort, wo Spannung 2 fräst – an eine Stelle legen, die "
                     "in Spannung 1 schon fertig ist.")


def _umspannen_teilen(teile, auftrag, werkzeuge, cz: float, home, teil_bericht, teil_minuten, op_warnungen,
                      bearbeitungen):
    """Umspannen (ops/umspannen.py): jedes Teil an der Sperrzone seines Werkzeugs zerschneiden – ausserhalb bleibt
    es (Spannung 1), innerhalb kommt ein neues Teil «<key>@2» (Spannung 2) dazu. Ändert «teile» an Ort und Stelle.
    Liefert None (aus oder nichts zu teilen) oder {"abstand", "hoehe2", "seiten": [...], "zonen": {...}}."""
    from .ops import umspannen as ums
    e = ums.einstellung(auftrag)
    if e is None:
        return None
    try:
        import shapely  # noqa: F401
    except ImportError:
        return None
    nach_id = {b["id"]: b for b in bearbeitungen}
    zonen: Dict = {}
    neu: List[Dict] = []
    in2: Dict[int, set] = {}
    zonen_2: Dict[int, List] = {}

    def zone_fuer(seite, r, spann1):
        k_z = (seite, round(r, 4))
        if k_z not in zonen:
            zonen[k_z] = ums.zone(spann1, r, e["abstand"])
        return zonen[k_z]

    def zerlegen(t, w, z):
        r = float(w.get("durchmesser") or 0) / 2
        bb = nach_id.get(t.get("bid"), {})
        ganz = t["art"] in ums.GANZ or t.get("bid") == PASS_ID or bool(t["tp"].zyklen)
        rampe = 0.0 if w.get("mittenschnitt", True) else max(2.0, float(bb.get("p", {}).get("rampe_winkel") or 3.0))
        return ums.teilen(t["tp"], z, cz, float(w.get("eintauchen") or 0), home, rampe=rampe, radius=r, ganz=ganz)

    # Zufallstests 07.11.: das Planen in Spannung 1 lässt zwischen seinen Bahnen am Rand der Sperrzone schmale
    # Streifen stehen (es plant sie erst in Spannung 2). Ein Loch knapp ausserhalb der Zone tauchte mit dem Rand im
    # Eilgang 0,15 mm breit hinein. Darum gilt für die anderen Bearbeitungen der Seite auch als gesperrt, was ihr
    # Fräser von solchen Streifen berührt: alles, was das Planen insgesamt fräst, in Spannung 1 aber noch nicht.
    plan_teile: Dict[int, tuple] = {}
    offen_plan: Dict[int, object] = {}
    for i_t, t in enumerate(teile):
        w = werkzeuge.get(t["slot"])
        if t["art"] != "planen" or not w:
            continue
        spann1 = ums.spannmittel(auftrag, t["seite"], 1)
        if not spann1:
            continue
        r_p = float(w.get("durchmesser") or 0) / 2
        tp1_p, tp2_p = zerlegen(t, w, zone_fuer(t["seite"], r_p, spann1))
        plan_teile[i_t] = (tp1_p, tp2_p)
        if tp2_p is not None:
            ganz_p = ums.schnitt_flaeche(t["tp"], r_p)
            fertig = ums.schnitt_flaeche(tp1_p, r_p) if tp1_p is not None else None
            rest = ganz_p if fertig is None or ganz_p is None else ganz_p.difference(fertig)
            if rest is not None and not rest.is_empty:
                alt_ = offen_plan.get(t["seite"])
                offen_plan[t["seite"]] = rest if alt_ is None else alt_.union(rest)
    for i_t, t in enumerate(teile):
        w = werkzeuge.get(t["slot"])
        spann1 = ums.spannmittel(auftrag, t["seite"], 1) if w else []
        if not spann1:
            neu.append(t)
            continue
        r = float(w.get("durchmesser") or 0) / 2
        z = zone_fuer(t["seite"], r, spann1)
        bb = nach_id.get(t.get("bid"), {})
        if i_t in plan_teile:
            tp1, tp2 = plan_teile[i_t]
        else:
            if offen_plan.get(t["seite"]) is not None and z is not None:
                z = z.union(offen_plan[t["seite"]].buffer(r + 0.05))
            tp1, tp2 = zerlegen(t, w, z)
        if tp2 is None:
            neu.append(t)
            continue
        k2 = t["key"] + "@2"
        _v2, _e2, min2 = bahnlaenge(tp2, home)
        teil_bericht[k2] = teil_bericht.get(t["key"], t["key"]) + " – Spannung 2"
        teil_minuten[k2] = min2
        if tp1 is not None:
            _v1, _e1, min1 = bahnlaenge(tp1, home)
            teil_minuten[t["key"]] = min1
            neu.append(dict(t, tp=tp1, spannung=1))
        neu.append(dict(t, key=k2, tp=tp2, spannung=2))
        in2.setdefault(t["seite"], set()).add(t.get("bid"))
        zonen_2.setdefault(t["seite"], []).append(z)
        # Hinweise: durchgehende Kontur – frei erst nach Spannung 2, viel Arbeit für Spannung 2
        p = bb.get("p") or {}
        if t["art"] == "kontur" and p.get("durch"):
            ziel = op_warnungen.setdefault(t["bid"], [])
            if (p.get("stege_modus") == "keine" or not (p.get("stege_anzahl") or p.get("stege_modus") == "manuell")) \
                    and UMSPANNEN_FREI not in ziel:
                ziel.append(UMSPANNEN_FREI)
            l1 = sum(_math.dist(*q) for q in _fraeswege(tp1)) if tp1 is not None else 0.0
            l2 = sum(_math.dist(*q) for q in _fraeswege(tp2))
            if l1 + l2 > 0 and l2 > (l1 + l2) / 3 and UMSPANNEN_VIEL not in ziel:
                ziel.append(UMSPANNEN_VIEL)
    if not in2:
        return None
    teile[:] = neu
    from shapely.ops import unary_union
    seiten = []
    hinweise: List[str] = []
    for seite in sorted(in2):
        z_ = unary_union(zonen_2[seite])
        polys = [g for g in getattr(z_, "geoms", [z_]) if g.geom_type == "Polygon"]
        # zweite Lage eingezeichnet: liegt eines ihrer Spannmittel dort, wo Spannung 2 fräst?
        for s2 in ums.spannmittel(auftrag, seite, 2):
            from shapely.geometry import Polygon
            g2 = Polygon(s2["punkte"]).buffer(0)
            if g2.intersects(z_) and UMSPANNEN_IN_ZONE not in hinweise:
                hinweise.append(UMSPANNEN_IN_ZONE)
        seiten.append({"seite": seite, "zonen_mm": [[[round(x, 3), round(y, 3)] for x, y in g.exterior.coords]
                                                    for g in polys],
                       "bearbeitungen": [b["id"] for b in bearbeitungen if b["id"] in in2[seite]], "zeit": 0.0})
    return {"abstand": e["abstand"], "hoehe2": e["hoehe2"], "seiten": seiten, "zonen": zonen, "hinweise": hinweise}


def _fraeswege(tp):
    if tp is None:
        return []
    aus, pos = [], None
    for m in tp.moves:
        q = (m.x, m.y, m.z)
        if pos is not None and not m.rapid:
            aus.append((pos, q))
        pos = q
    return aus


def _umspannen_hoehe2(umspann, folge, nach_key, werkzeuge, home) -> float | None:
    """Spannung 2 ohne eingezeichnete zweite Lage: waagrechte Eilgänge ausserhalb der Sperrzonen mindestens auf
    höchstes Spannmittel + Abstand (oder die eingegebene Spannhöhe «hoehe2»). Mit zweiter Lage: deren Spannmittel
    sind Hindernis wie sonst (Ausgabe: _ueber_spannmittel).
    Rückgabe: die geschätzte Höhe (keine zweite Lage, keine Spannhöhe eingegeben) für den Hinweis, sonst None."""
    from .ops import umspannen as ums
    geschaetzt = None
    for ab in folge:
        if int(ab.get("spannung") or 1) != 2:
            continue
        seite = ab["seite"]
        auftrag_sp = umspann.get("_auftrag")
        zweite = ums.spannmittel(auftrag_sp, seite, 2) if auftrag_sp else []
        if zweite:
            continue
        w = werkzeuge.get(ab["slot"]) or {}
        r = float(w.get("durchmesser") or 0) / 2
        z2 = umspann["zonen"].get((seite, round(r, 4)))
        h2 = umspann["hoehe2"]
        if h2 is None:
            alle = [float(s.get("hoehe") or 20) for s in (auftrag_sp.get("spannmittel") or [])
                    if int(s.get("seite") or 1) == seite] if auftrag_sp else []
            h2 = (max(alle) if alle else 0.0) + umspann["abstand"]
            # Sicherheitsprüfung 04.11.: wo und wie hoch nach dem Umspannen gespannt wird, weiss niemand – nie
            # tiefer als die Sicherheitshöhe (über die fährt jedes Programm am Anfang über den ganzen Tisch)
            h2 = max(h2, float(home[2]))
            geschaetzt = max(h2, geschaetzt or h2)
        pos = home                                  # je Werkzeugeinsatz von der Sicherheitshöhe
        for k in ab["keys"]:
            tp = nach_key[k]["tp"]
            ums.hoehe2_anheben(tp, z2, h2, pos)
            if tp.moves:
                pos = (tp.moves[-1].x, tp.moves[-1].y, tp.moves[-1].z)
    return geschaetzt


def rechnen(auftrag: Dict) -> Dict:
    import os

    h = auftrag["hoehen"]
    tol = float(auftrag.get("toleranz", 0.01))
    cz, sz = float(h["freifahrhoehe"]), float(h["startabstand"])
    rund_hinweis = []
    ru = auftrag.get("rund") or {}
    if ru.get("schraube") and float(ru.get("ueberstand") or 0) + 2.0 > cz:
        # der Schraubenkopf steht über: zwischen den Bahnen höher fahren, sonst streift der Fräser ihn
        cz = float(ru["ueberstand"]) + 2.0
        rund_hinweis.append(f"Freifahrhöhe auf {cz:g} mm angehoben – der Schraubenkopf steht "
                            f"{float(ru['ueberstand']):g} mm über.")
    # Masskorrektur der Maschine je Seite (Peter, 20.10.: Teile ~0,1 mm zu gross → −0,05): wirkt wie ein
    # zusätzliches Aufmass an Konturen, Taschenwänden und gefrästen Löchern; negativ = Teile kleiner, Löcher grösser
    masskorrektur = max(-1.0, min(1.0, float((auftrag.get("maschine") or {}).get("masskorrektur") or 0.0)))

    def mess(wz) -> float:
        """Messkorrektur des Fräsers je Seite (Testteil ausgemessen): > 0 = er schneidet grösser."""
        return max(-1.0, min(1.0, float((wz or {}).get("messkorrektur") or 0.0)))
    safe = float(h.get("sicherheitshoehe", 20.0))
    safe = max(safe, cz + 5.0)
    home = (0.0, 0.0, safe)
    objekte = auftrag.get("objekte", {})
    werkzeuge, zuordnung = _werkzeug_setup(auftrag)
    warnungen: List[str] = list(rund_hinweis)
    op_warnungen: Dict[str, List[str]] = {}   # Bearbeitung -> Warnungen (ohne Namen davor)
    op_zeilen: set = set()
    teil_bericht: Dict[str, str] = {}    # Schlüssel -> Text
    teile: List[Dict] = []               # [{key, slot, art, seite, pos, tp}] in Rechenfolge
    teil_minuten: Dict[str, float] = {}
    teil_vorschub: Dict[str, float] = {}     # davon im Fräsvorschub (min)
    statistik: Dict[str, Dict] = {}      # Bearbeitung -> Fräsweg, Zeit, Linien
    steg_ringe: Dict[str, List] = {}     # Kontur -> [{seite, d, ringe}] (Haltestege verteilen)
    stege_auto: Dict[str, List] = {}     # Kontur «auto» -> gerechnete Stegmitten [{x, y, seite}] (Basis 26.10.)
    bahnen: Dict[str, List] = {}         # Schlüssel -> Fräsbahnen (2D) für die Zeichnung
    bearbeitungen = _bearbeitungen(auftrag, zuordnung)
    # Reihenfolge (im Panel per Ziehen festgelegt); Flachbereich direkt vor seiner V-Carve
    # (erst grob ausräumen, dann schlichtet der V-Fräser Flanken und Kante)
    ordnung = []
    wechsel = _wechsel_paare(bearbeitungen)
    # 3D-Schruppen «in Lagen» im Wechsel mit «Böden und Wände»: eine Lage endet spätestens an der Stufengrenze
    # (die Stufen werden danach aus dem Werkzeugweg geschnitten – boeden_waende.aufteilen)
    for bw_id, s_id in wechsel.items():
        s_bb = next(x for x in bearbeitungen if x["id"] == s_id)
        bw_bb = next(x for x in bearbeitungen if x["id"] == bw_id)
        if float(s_bb["p"].get("lagen") or 0) > 0 and bw_bb["werkzeug"] in werkzeuge:
            from .ops import boeden_waende as _bw0
            s_bb["p"] = dict(s_bb["p"], _stufe=_bw0.stufe_mm(bw_bb["p"], werkzeuge[bw_bb["werkzeug"]]))
    for bb in bearbeitungen:
        if bb["id"] in wechsel:
            continue                     # steht Stufe um Stufe hinter seinem Schruppen (unten)
        if bb["art"] == "gewinde":
            ordnung.append(bb["id"] + "#kernloch")
        if bb["art"] == "tasche":
            ordnung.append(bb["id"] + "#raeumen")
        if bb["art"] == "vcarve":
            ordnung.append(bb["id"] + "#flach")
        ordnung.append(bb["id"])
        if bb["art"] in ("tasche", "kontur"):
            ordnung.append(bb["id"] + "#schlicht")
        for bw_id, s_id in wechsel.items():
            if s_id == bb["id"]:
                dicke_ = float((auftrag.get("hoehen") or {}).get("materialdicke") or 18.0)
                n_st = int(dicke_ / 1.0) + 3
                ordnung.append(bw_id)
                for k in range(2, n_st + 1):
                    ordnung += [f"{s_id}#stufe{k}", f"{bw_id}#stufe{k}"]

    from . import cam
    kern_info = (f"CAM-Modul {cam.version()} (aus FreeCAD CAM)" if cam.verfuegbar()
                 else "eigener Rechenkern (CAM-Modul fehlt)")
    _vdatei = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "VERSION")
    if os.path.basename(os.path.dirname(_vdatei)) == "kern" and os.path.exists(_vdatei):
        with open(_vdatei, encoding="utf-8") as fh:
            kern_info += " · Rechenkern-Update " + fh.read().strip()

    from . import speicher as _speicher
    aufnahme = None                      # Zwischenspeicher: was die laufende Bearbeitung erzeugt

    def warnen(bb, texte):
        if aufnahme is not None and texte:
            aufnahme.ereignisse.append(("warnen", list(texte)))
        for t in texte:
            zeile = f"{bb['name']}: {t}"
            if zeile not in warnungen:
                warnungen.append(zeile)
                op_warnungen.setdefault(bb["id"], []).append(t)
                op_zeilen.add(zeile)

    TEILE = _Teile({"flach": " Flachbereich", "schlicht": " Schlichten", "kernloch": " Kernloch",
                    "raeumen": " Räumen"})

    # Planfräsen nimmt die Oberfläche ab: alle anderen Tiefen zählen ab der neuen
    # Oberfläche (ganz durchgehende Konturen bleiben, sie reichen schon bis unten)
    plan_tiefen = {}
    gewinde_liste: List[Dict] = []      # je Gewinde: Lage, Masse, Seite – für die Prüfung und die 3D-Ansicht
    for x in bearbeitungen:
        if x["art"] == "planen" and x["objekte"]:
            plan_tiefen[x["auf_seite"]] = max(plan_tiefen.get(x["auf_seite"], 0.0), float(x["p"].get("tiefe") or 0))

    def ablegen(bb, slot, tp, anzahl=None, flach=False, teil=None, basis=None):
        """basis: Bahn kommt fertig vorbereitet aus dem Zwischenspeicher (speicher.py) – dann nur
        noch eintragen; der Wert ist der Fräsvorschub vor dem Vorschub-Faktor."""
        if not tp.moves:
            return
        if getattr(tp, "stege_auto", None) and not teil and bb["p"].get("stege_modus") not in ("manuell", "keine"):
            stege_auto.setdefault(bb["id"], []).extend(
                {"x": x, "y": y, "seite": int(bb["auf_seite"])} for x, y in tp.stege_auto)
        if getattr(tp, "steg_ringe", None) and not teil:
            # Fräsbahn-Mitte der Konturen mit Haltestegen (ops/stege.py: Marken verteilen)
            steg_ringe.setdefault(bb["id"], []).append({"seite": bb["auf_seite"], "d": getattr(tp, "steg_d", 0.0),
                                                       "ringe": tp.steg_ringe})
        aus_speicher = basis is not None
        if not aus_speicher:
            # Kurze Geraden zusammenfassen (innerhalb der Toleranz); 3D und «Tiefe ab Modell»
            # vereinfachen selbst bzw. brauchen die dichten Punkte für die Projektion
            if not bb["art"].startswith("relief_") and bb["p"].get("tiefe_ab") not in ("modell", "haut", "bis_modell"):
                from .ops.bahn import vereinfachen
                vereinfachen(tp, max(0.002, tol), home)
            # Planfräsen derselben Seite: was hier bei 0 liegt, liegt nachher plan_vorab tiefer – die
            # stehengelassenen Inseln (Schraube, Passstifte) reichen aber bis zur alten Oberfläche
            plan_vorab = plan_tiefen.get(bb["auf_seite"], 0.0) if bb["art"] != "planen" else 0.0
            # Rundes Material: um die Schraube in der Mitte nie fräsen
            if slot in werkzeuge and auftrag.get("rund"):
                from . import passstifte
                d_w = float(werkzeuge[slot].get("durchmesser") or 0)
                kreise = passstifte.schraube(auftrag, d_w)
                ein = float(werkzeuge[slot].get("eintauchen") or 0) or None
                kopf_oben = plan_vorab + float(auftrag["rund"].get("ueberstand") or 0) + 0.5
                if kreise and passstifte.aussparen(tp, kreise, cz, ein, home, oberflaeche=kopf_oben):
                    warnen(bb, ["Um die Schraube in der Mitte wird nicht gefräst – dort bleibt Material stehen."])
                if bb["art"] in ("tasche", "gravur", "vcarve", "bohren", "gewinde") and bb.get("id") != KERBE_ID \
                        and _neben_rund(tp, auftrag, d_w):
                    warnen(bb, ["Liegt teilweise neben dem runden Material – dort fräst der Fräser Luft."])
            # Zweiseitig: um die Passlöcher nie fräsen (Seite 2: Stifte darin, Seite 1: Lochwand)
            if bb.get("id") != PASS_ID and slot in werkzeuge:
                from . import passstifte
                kreise = passstifte.sperrkreise(auftrag, float(werkzeuge[slot].get("durchmesser") or 0))
                if kreise:
                    durch = bb["art"] == "kontur" and passstifte.kontur_durch_stift(tp, kreise)
                    ein = float(werkzeuge[slot].get("eintauchen") or 0) or None
                    if passstifte.aussparen(tp, kreise, cz, ein, home, oberflaeche=plan_vorab):
                        if durch:
                            warnen(bb, ["Die Kontur läuft durch ein Passloch – dort bleibt Material stehen "
                                        "und das Teil hängt noch fest. Teil verschieben oder die Passlöcher "
                                        "anders setzen (Auftrag einrichten › Zweiseitig)."])
                        elif not bb["art"].startswith("relief_"):
                            # 3D meldet es genauer (relief3d: «… es bleibt eine Insel stehen. Modell verschieben …»)
                            warnen(bb, ["Um die Passlöcher wird nicht gefräst (Passstifte) – dort bleibt "
                                        "Material stehen."])
            plan_tiefe = plan_tiefen.get(bb["auf_seite"], 0.0)
            # nicht verschoben: Planfräsen selbst, ganz durchgehende Konturen und die Passlöcher – sie gehen durch die
            # ganze Platte (der Grund bleibt, wo er ist) und werden meist vor dem Planfräsen gebohrt: verschoben lag
            # die Rückzugsebene des Bohrzyklus unter der noch nicht geplanten Oberfläche (Eilgang 0,5 mm ins Material,
            # Fräsbahnen 26.10., Prüfung der 3D-Bahnen)
            durch_ = bb["p"].get("durch")
            if bb["art"] == "kontur" and teil == "schlicht" and bb["p"].get("schlicht_tiefe") not in (None, "", 0):
                # B-2: das Schlichten hat eine eigene Tiefe – geht es durch die ganze Platte, wird es nicht verschoben
                durch_ = _schlicht_tiefe(bb["p"]) >= float((auftrag.get("hoehen") or {}).get("materialdicke")
                                                           or 1e9) - 1e-6
            if plan_tiefe > 0 and bb["art"] != "planen" and not (bb["art"] == "kontur" and durch_) \
                    and bb.get("id") != PASS_ID and bb["p"].get("tiefe_ab") not in ("haut", "bis_modell"):
                for m in tp.moves:
                    # Was auf der Freifahrhöhe liegt, bleibt, wo es ist: über den Inseln, die das Planfräsen stehen
                    # lässt (Passstifte, Schrauben in den Ecken, Schraube in der Mitte), stünde der Fräser sonst um die
                    # Plantiefe tiefer (Fräsbahnen 26.10.); wo der Weg frei ist, senken ihn die Übergänge danach
                    if m.z < cz - 1e-6:
                        m.z -= plan_tiefe
            # «Tiefe gemessen ab: 3D-Modell»: die Bahn folgt der Oberfläche des Modells
            if bb["p"].get("tiefe_ab") == "modell":
                from .ops import relief_projektion
                if bb["art"] in relief_projektion.ARTEN:
                    d_w = float((werkzeuge.get(slot) or {}).get("durchmesser") or 6.0)
                    warnen(bb, relief_projektion.projizieren(tp, auftrag, bb["auf_seite"], cz, sz, d_w))
            # «Tiefe gemessen ab: Haut» (B-15): die Kontur folgt der Haut des zweiseitigen 3D-Modells
            elif bb["p"].get("tiefe_ab") == "haut" and bb["art"] == "kontur":
                from .ops import relief_projektion
                d_w = float((werkzeuge.get(slot) or {}).get("durchmesser") or 6.0)
                warnen(bb, relief_projektion.auf_haut(
                    tp, auftrag, bb["auf_seite"], sz, d_w, ueber=float(bb["p"].get("_haut_ueber") or 0.0),
                    zustellung=float(bb["p"].get("zustellung") or 0.0) or None, freifahrhoehe=cz))
            # «bis auf das 3D-Modell» (Peter 01.11.): eine Ebene → auf das Modell gesetzt, Ebenen darüber
            elif bb["p"].get("tiefe_ab") == "bis_modell" and bb["art"] in ("tasche", "kontur"):
                from .ops import relief_projektion
                q_ = bb["p"]
                t_max = q_.get("tiefe")
                # Peter 03.11.: «Tiefe» 0 (oder leer) heisst «bis aufs Modell, wie tief es auch ist» – nicht «gar nicht»
                t_max = float(t_max) if t_max not in (None, "") and float(t_max) > 0 \
                    else float(h.get("materialdicke") or 18.0) + 0.2
                warnen(bb, relief_projektion.bis_modell(
                    tp, auftrag, bb["auf_seite"], werkzeuge.get(slot) or {},
                    float(q_.get("zustellung") or 0.0) or 3.0, t_max, float(q_.get("tiefe_ab_mm") or 0.0),
                    float(q_.get("aufmass") or 0.0), sz, cz,
                    start=float(q_.get("starttiefe") or 0.0)))
            # Tief freifahren (Peter, 25.10.): zwischen zwei Zügen nur knapp über die Oberfläche, wo auf dem
            # Weg nichts steht (Passstifte, Schraube, Spannmittel bleiben auf der Freifahrhöhe). Nur 2D.
            # Planfräsen nicht: das Rohteil kann vorher über Z 0 stehen
            elif not bb["art"].startswith("relief_") and bb["art"] != "planen" and slot in werkzeuge:
                from .ops import freifahren
                d_w = float(werkzeuge[slot].get("durchmesser") or 0)
                kr, fl = freifahren.hindernisse(auftrag, bb["auf_seite"], d_w)
                freifahren.tief(tp, cz, freifahren.hoehe(sz, cz), kr, fl, home)
        teil = "flach" if flach else teil
        # Vorschub-Faktor der Bearbeitung (in der Zeitleiste gezogen): nur Fräsvorschübe,
        # nicht Eilgang und senkreches Eintauchen; nie über den Höchstvorschub der Maschine
        faktor = _vorschub_faktor(bb["p"].get("vorschub_faktor"))
        vmax = float((auftrag.get("maschine") or {}).get("vorschub_max") or 0)
        if not aus_speicher:
            basis = max([m.feed or 0.0 for m in tp.moves if not m.rapid] + [0.0])
            if abs(faktor - 1.0) > 1e-9 or vmax > 0:
                begrenzt = False
                q = home
                for m in tp.moves:
                    if not m.rapid and m.feed and _math.hypot(m.x - q[0], m.y - q[1]) > 1e-9:
                        f = m.feed * faktor
                        if vmax > 0 and f > vmax:
                            f, begrenzt = vmax, True
                        m.feed = round(f, 1)
                    q = (m.x, m.y, m.z)
                if begrenzt and abs(faktor - 1.0) > 1e-9:
                    text = f"Vorschub auf den Höchstvorschub der Maschine begrenzt ({vmax:g} mm/min)."
                    op_warnungen.setdefault(bb["id"], []).append(text)
                    if aufnahme is not None:
                        aufnahme.ereignisse.append(("opwarn", text))
            if aufnahme is not None:
                aufnahme.ereignisse.append(("ablegen", slot, _speicher.bahn_kopie(tp), anzahl, flach, teil, basis))
        vor, _eil, minuten = bahnlaenge(tp, home)
        # Anteil der Zeit, die im Fräsvorschub vergeht (für die Schätzung beim Ziehen)
        vm, q = 0.0, home
        for m in tp.moves:
            if not m.rapid and m.feed and _math.hypot(m.x - q[0], m.y - q[1]) > 1e-9:
                vm += _math.dist(q, (m.x, m.y, m.z)) / m.feed
            q = (m.x, m.y, m.z)
        key = bb["id"] + ("#" + teil if teil else "")
        name = (bb["name"].split(" (")[0] + TEILE[teil]) if teil else bb["name"]
        d_bahn = float(werkzeuge[slot]["durchmesser"]) if slot in werkzeuge else 0.0
        if bb["art"] == "tasche" and slot in werkzeuge and werkzeuge[slot].get("typ") == "v" \
                and float(werkzeuge[slot].get("spitze") or 0) > 0:
            d_bahn = float(werkzeuge[slot]["spitze"])     # am Boden räumt nur die Spitze
        if slot in werkzeuge and werkzeuge[slot].get("typ") == "v" and bb["art"] in ("vcarve", "gravur"):
            # V-Fräser: die Breite hängt von der Tiefe ab – in der Vorschau nur die Spitze
            import math as _m
            t_ = float(bb["p"].get("tiefe") or 0) if bb["art"] == "gravur" else 0.0
            d_bahn = float(werkzeuge[slot].get("spitze") or 0) + 2 * t_ * _m.tan(
                _m.radians(float(werkzeuge[slot].get("v_winkel", 60)) / 2))
        bahnen[key] = {"durchmesser": d_bahn, "zuege": _bahnen_2d(tp), "seite": bb["auf_seite"]}
        menge = f"{tr(_anzahl(anzahl, 'Linie', 'Linien'))} · " if anzahl else ""
        teil_bericht[key] = f"{name}: {menge}Fräsweg {vor:.0f} mm · {_zeit(minuten)}"
        teile.append({"key": key, "slot": slot, "art": bb["art"], "seite": bb["auf_seite"],
                      "pos": ordnung.index(key), "tp": tp, "bid": bb["id"], "teil": teil or ""})
        teil_minuten[key] = teil_minuten.get(key, 0.0) + minuten
        teil_vorschub[key] = teil_vorschub.get(key, 0.0) + vm
        st = statistik.setdefault(bb["id"], {"weg": 0.0, "minuten": 0.0, "linien": 0,
                                             "senkrecht": 0})
        st["senkrecht"] += getattr(tp, "senkrecht", 0)
        if not teil or (teil.startswith("stufe") and "vorschub_basis" not in st):
            # Fräsvorschub vor/nach dem Faktor und Empfehlung für Werkzeug und Material
            st["vorschub_basis"] = round(basis, 1)
            st["vorschub"] = round(min(basis * faktor, vmax) if vmax > 0 else basis * faktor, 1)
            st["vorschub_empf"] = float(werkzeuge.get(slot, {}).get("vorschub") or 0)
            st["faktor"] = round(faktor * 100)
            # Zeitleiste (Peter 27.10.): woraus der gefahrene Vorschub entsteht – Werkzeug, Bearbeitung,
            # Faktor der Zeitleiste, Höchstvorschub der Maschine
            pb = bb["p"]
            try:
                st["vorschub_bearbeitung_mm"] = round(float(pb.get("vorschub_mm") or 0), 1)
            except (TypeError, ValueError):
                st["vorschub_bearbeitung_mm"] = 0.0
            try:
                st["vorschub_bearbeitung_prozent"] = round(float(pb.get("vorschub_prozent") or 100))
            except (TypeError, ValueError):
                st["vorschub_bearbeitung_prozent"] = 100
            st["vorschub_max"] = vmax
            st["vorschub_begrenzt"] = bool(vmax > 0 and basis * faktor > vmax + 1e-6)
        st["weg"] += vor
        st["minuten"] += minuten
        st["linien"] += anzahl or 0

    # In der Reihenfolge rechnen, in der gefräst wird (Seite für Seite, je nach
    # Einstellung Werkzeug für Werkzeug oder wie in der Liste): so weiss eine
    # Tasche, was vorher schon ausgeräumt ist.
    from . import ablauf as abl
    modus = auftrag.get("reihenfolge") if auftrag.get("reihenfolge") in abl.MODI else "auto"
    ablauf = abl.rechenfolge(bearbeitungen, modus)
    ausgeraeumt_je: Dict[int, List] = {}   # Seite -> [(Fläche, Tiefe)] der Taschen bisher
    konturen_je: Dict[int, List] = {}      # Seite -> [(Schnittfuge, Tiefe)] der Konturen bisher (zweiseitig)
    herkunft_je: Dict[int, List] = {}      # Seite -> je Eintrag: welche Objekte ihn ausgeräumt haben
    offen = None

    def nachtragen():
        # was die letzte Bearbeitung ausgeräumt hat, gilt für alle folgenden –
        # ausser für Duplikate (dieselben Objekte): die fräsen denselben Weg nochmals
        nonlocal offen, aufnahme
        if aufnahme is not None:
            # Zwischenspeicher: was diese Bearbeitung für die folgenden hinterlässt, mit ablegen
            e_, a_ = aufnahme, aufnahme_bb
            aufnahme = None
            if offen:
                e_.ausgeraeumt = list(offen[2][offen[3]:])
            e_.konturen = list(konturen_je.get(a_["auf_seite"], [])[aufnahme_n[0]:])
            e_.gewinde = [dict(g) for g in gewinde_liste[aufnahme_n[1]:]]
            e_.p_neu = {k: v for k, v in a_["p"].items() if k not in aufnahme_p or aufnahme_p[k] is not v}
            _speicher.SPEICHER.ablegen(dok_kennung, a_["id"], a_["auf_seite"], e_)
        if offen:
            voll, herkunft, eigen, n0, zwilling = offen
            neu = eigen[n0:]
            voll.extend(neu)
            herkunft.extend([zwilling] * len(neu))
        offen = None

    # Zwischenspeicher je Bearbeitung (speicher.py): unveränderte Bearbeitungen werden abgespielt
    # statt gerechnet. «nur_pruefen» (Panel, Peter 24.10.: erst auf Wunsch rechnen): nichts rechnen –
    # geänderte Bearbeitungen behalten ihre alten Bahnen und gelten als veraltet.
    nur_pruefen = bool(auftrag.get("nur_pruefen"))
    speicher_an = not _speicher.aus()
    dok_kennung = str(auftrag.get("_dokument") or "")
    k_basis = _speicher.auftrag_teil(
        auftrag, _cz=cz, _sz=sz, _home=home, _plan=plan_tiefen,
        _andere=bool([x for x in bearbeitungen if x["art"] != "planen"])) if speicher_an else ""
    k_reliefs = None
    aufnahme_bb, aufnahme_p, aufnahme_n = None, {}, (0, 0)
    veraltet: Dict[str, bool] = {}       # Bearbeitung → veraltet (alte Bahnen) oder aktuell
    neu_gerechnet: List[str] = []
    unsicher: set = set()                # Seiten, deren Ausgeräumtes von veralteten Bearbeitungen stammt

    def abspielen(bb, e) -> None:
        bb["p"].update(e.p_neu)
        for ev in e.ereignisse:
            if ev[0] == "warnen":
                warnen(bb, ev[1])
            elif ev[0] == "opwarn":
                op_warnungen.setdefault(bb["id"], []).append(ev[1])
            else:
                _, sl, tp_, anz, fl, tl, bs = ev
                ablegen(bb, sl, _speicher.bahn_kopie(tp_), anz, fl, tl, basis=bs)
        offen[2].extend(e.ausgeraeumt)
        if e.konturen:
            konturen_je.setdefault(bb["auf_seite"], []).extend(e.konturen)
        gewinde_liste.extend(dict(g) for g in e.gewinde)

    from . import fortschritt
    mit_objekten = [x for x in ablauf if x["objekte"]]
    vorab_3d: List[bool] = []           # 3D-Bearbeitungen schon vorab (gleichzeitig) gerechnet?
    for bb in ablauf:
        if bb["objekte"]:
            fortschritt.melden(bb["name"].split(" (")[0], mit_objekten.index(bb) + 1, len(mit_objekten))
        nachtragen()
        art = bb["art"]
        liste = bb["objekte"]
        if not liste:
            continue
        slot = bb["werkzeug"]
        if slot not in werkzeuge:
            warnen(bb, [f"Werkzeug {slot} ist nicht eingerichtet – ausgelassen."])
            continue
        p = bb["p"]
        zwilling = (art, tuple(sorted(str(o.get("eid") or o.get("id") or id(o)) for o in liste)))
        voll = ausgeraeumt_je.setdefault(bb["auf_seite"], [])
        herkunft = herkunft_je.setdefault(bb["auf_seite"], [])
        ausgeraeumt = [a for a, k in zip(voll, herkunft) if k != zwilling]
        offen = (voll, herkunft, ausgeraeumt, len(ausgeraeumt), zwilling)
        if speicher_an and not art.startswith("relief_"):
            zustand = {}
            liest = set()
            if art in ("tasche", "kontur"):
                zustand["aus"] = _speicher.flaechen(ausgeraeumt)
                liest.add(bb["auf_seite"])
            if art == "kontur":
                # Haltesteg-Marken stehen nicht im Schlüssel des Auftrags (sonst rechnete jede verschobene Marke
                # alles neu), sondern nur bei der Kontur, zu der sie gehören
                zustand["stege"] = (_stege_der_seite(auftrag, bb["auf_seite"], bb["id"])
                                    if p.get("stege_modus") == "manuell" else None)
                if p.get("anfahren") == "bogen":          # der Anfahrbogen hält Abstand zu den Nachbarn
                    zustand["nachbarn"] = _kontur_nachbarn(bearbeitungen, bb)
                zustand["schnitte"] = {s_: _speicher.flaechen(ausgeraeumt_je.get(s_, []))
                                       + _speicher.flaechen(konturen_je.get(s_, [])) for s_ in (1, 2)}
                liest |= {1, 2}
            if art == "boeden_waende":
                # aus dem Modell, nach dem 3D-Schruppen davor (wo es hinkam)
                vor_ = _schruppen_vor(bearbeitungen, bb)
                zustand["vor"] = [vor_["p"], werkzeuge.get(vor_["werkzeug"])] if vor_ else None
                zustand["wechsel"] = bb["id"] in wechsel
                if k_reliefs is None:
                    k_reliefs = _speicher.reliefs_teil(auftrag)
                zustand["reliefs"] = k_reliefs
            if p.get("tiefe_ab") in ("modell", "haut", "bis_modell"):
                if k_reliefs is None:
                    k_reliefs = _speicher.reliefs_teil(auftrag)
                zustand["reliefs"] = k_reliefs
            schl = _speicher.schluessel(k_basis, bb, werkzeuge, zustand)
            eintrag = _speicher.SPEICHER.holen(schl)
            alt_ = eintrag is None or bool(liest & unsicher)
            if eintrag is None and nur_pruefen:
                eintrag = _speicher.SPEICHER.letzter(dok_kennung, bb["id"], bb["auf_seite"])
            if nur_pruefen and alt_:
                veraltet[bb["id"]] = True
                if art in ("tasche", "kontur"):
                    unsicher.add(bb["auf_seite"])
            if eintrag is not None:
                veraltet.setdefault(bb["id"], False)
                abspielen(bb, eintrag)
                continue
            if nur_pruefen:              # nie gerechnet: keine Bahnen, veraltet
                continue
            aufnahme = _speicher.Eintrag(schl)
            aufnahme_bb, aufnahme_p = bb, dict(p)
            aufnahme_n = (len(konturen_je.get(bb["auf_seite"], [])), len(gewinde_liste))
        if not nur_pruefen or art.startswith("relief_"):
            veraltet.setdefault(bb["id"], False)
            if bb["id"] not in neu_gerechnet:
                neu_gerechnet.append(bb["id"])
        w = _mit_vorschub(werkzeuge[slot], p.get("vorschub_prozent"), p.get("vorschub_mm"))
        if not art.startswith("relief_"):
            w = als_2d(w)
        d = float(w["durchmesser"])
        linien = _linien(liste)
        warn: List[str] = []
        if art in ("tasche", "kontur") and p.get("tiefe_ab") == "bis_modell":
            # Peter 01.11. (Servoschacht im Flügel): EINE Ebene der 2D-Bahn rechnen; ablegen setzt sie auf das 3D-Modell
            # und legt die Ebenen darüber (relief_projektion.bis_modell)
            z_ = float(p.get("zustellung") or 0.0) or 3.0
            p = dict(p, tiefe=z_, zustellung=z_, starttiefe=0.0, stege_modus="keine", stege_anzahl=0, fase=False,
                     schlicht_zustellung=None, raeum_zustellung=0)
        # Schruppen + Schlichten: Schruppen lässt die Zugabe stehen, ein eigener
        # Durchgang (auch mit anderem Werkzeug) nimmt sie am Rand weg
        zugabe, sw, s_slot, s_zust = 0.0, None, slot, p.get("zustellung")
        # «Beginnt bei Tiefe»: darüber ist schon frei (Tasche, Kontur; nicht bei der Fase)
        starttiefe = (max(0.0, float(p.get("starttiefe") or 0.0))
                      if art in ("tasche", "kontur", "gravur", "vcarve") else 0.0)
        if art in ("tasche", "kontur") and p.get("schlichten"):
            s_slot = p.get("schlicht_werkzeug") or slot
            sw = als_2d(werkzeuge.get(s_slot))
            if sw is not None:
                sw = _mit_vorschub(sw, p.get("schlicht_vorschub"), p.get("schlicht_vorschub_mm"))
            if sw is None:
                warnen(bb, [f"Schlichtwerkzeug {s_slot} ist nicht eingerichtet – nur geschruppt."])
            else:
                zugabe = max(0.0, float(p.get("schlicht_zugabe") or 0.0))
                s_zust = float(p.get("schlicht_zustellung") or p["tiefe"])
        def eintauch_werte(wz, vor=""):
            """Rampenwinkel (0 = senkrecht) und Vollnut-Faktor für ein Werkzeug.
            vor: Vorsilbe eigener Werte eines Durchgangs («raeum_»); fehlen sie,
            gelten die der Bearbeitung."""
            def w_(k, d=None):
                v = p.get(vor + k) if vor else None
                return v if v not in (None, "") else p.get(k, d)
            winkel = 0.0
            if w_("eintauchen_art", "rampe") == "rampe" and wz.get("typ") != "v":
                winkel = float(w_("rampe_winkel") or wz.get("rampe") or 3.0)
            faktor = w_("vollnut")
            faktor = float(faktor) / 100.0 if faktor else float(wz.get("vollnut", 1.0))
            mm = p.get(vor + "vollnut_mm")  # Vollnut-Vorschub in mm/min (Dialog), je Durchgang eigen
            try:
                if mm not in (None, "") and float(mm) > 0 and float(wz.get("vorschub") or 0) > 0:
                    faktor = float(mm) / float(wz["vorschub"])
            except (TypeError, ValueError):
                pass
            return winkel, max(0.1, min(1.0, faktor))

        if w.get("typ") == "bohrer" and art != "bohren":
            warnen(bb, [f"Werkzeug {slot} ist ein Bohrer – er bohrt nur senkrecht. Für diese Bearbeitung "
                        "einen Fräser wählen; ausgelassen."])
            continue
        if sw is not None and sw.get("typ") == "bohrer":
            warnen(bb, [f"Schlichtwerkzeug {s_slot} ist ein Bohrer – nur geschruppt."])
            sw, zugabe = None, 0.0
        try:
            if art == "gravur":
                # Fräser ohne Mittenschnitt: schräg eintauchen (Sicherheitsprüfung 04.11.: tauchte senkrecht)
                tp = gravur([Linie(pts, g) for pts, g in linien], p["tiefe"], p["zustellung"],
                            w["vorschub"], w["eintauchen"], cz, sz, start=home[:2],
                            name=bb["name"], rampe_winkel=0.0 if w.get("mittenschnitt", True) else
                            max(2.0, eintauch_werte(w)[0] or 3.0))
                _ab_stufe(tp, starttiefe, cz)
            elif art == "kontur":
                from .ops.kontur import kontur

                def wirksam(wz):
                    """V-Fräser: Breite auf Schnitttiefe (wie üblich), sonst Durchmesser."""
                    dm = float(wz["durchmesser"])
                    if wz.get("typ") == "v":
                        import math
                        breite = float(wz.get("spitze") or 0) + 2 * float(p["tiefe"]) * math.tan(
                            math.radians(float(wz.get("v_winkel", 60)) / 2))
                        return max(0.01, min(dm, breite))
                    return dm

                # zweiseitig: trennt erst der Schnitt der anderen Seite das Teil heraus, gelten die
                # Haltestege trotzdem – sie sitzen unten in diesem Schnitt
                mdicke = h.get("materialdicke")
                schnitte = {s_: list(ausgeraeumt_je.get(s_, [])) + list(konturen_je.get(s_, [])) for s_ in (1, 2)}
                if p.get("tiefe_ab") != "haut" and _durch_mit_gegenseite(auftrag, linien, float(p["tiefe"]), mdicke,
                                                                         bb["auf_seite"], schnitte):
                    mdicke = float(p["tiefe"])

                def kontur_mit(wz, zugabe, zust, schlichten=False, tiefe_=None, start_=None, voll=False):
                    """tiefe_/start_: eigene End- und Starttiefe (B-2: Schlichten tiefer als das Schruppen);
                    voll: der Durchgang schneidet in volles Material (Vollnut-Vorschub auch beim Schlichten)."""
                    winkel, faktor = eintauch_werte(wz)
                    return kontur(
                        linien, wirksam(wz), p["tiefe"] if tiefe_ is None else tiefe_, zust,
                        wz["vorschub"], wz["eintauchen"],
                        seite=p.get("seite", "auto"),
                        gleichlauf=_schlicht_gleichlauf(p) if schlichten else p.get("gleichlauf", True),
                        materialdicke=mdicke,
                        stege_anzahl=0 if p.get("stege_modus") == "keine" else int(p.get("stege_anzahl", 0)),
                        stege_punkte=_stege_der_seite(auftrag, bb["auf_seite"], bb["id"])
                        if p.get("stege_modus") == "manuell" else None,
                        stege_breite=p.get("stege_breite", 6.0),
                        stege_hoehe=p.get("stege_hoehe", 1.5),
                        freifahrhoehe=cz, startabstand=sz, toleranz=tol, start=home,
                        aufmass=aufmass + zugabe + (mess(wz) if p.get("seite", "auto") != "linie" else 0.0),
                        rampe=winkel,
                        vollnut=1.0 if schlichten and not voll else faktor,
                        anfahren=p.get("anfahren", ""), anfahr_radius=float(p.get("anfahr_radius") or 0.0),
                        dogbone=bool(p.get("dogbone", False)),
                        starttiefe=starttiefe if start_ is None else start_, vorher=ausgeraeumt,
                        nachbarn=_kontur_nachbarn(bearbeitungen, bb) if p.get("anfahren") == "bogen" else None)
                aufmass = float(p.get("aufmass") or 0.0)
                if p.get("seite", "auto") != "linie" and not p.get("fase"):
                    aufmass += masskorrektur          # Maschine: Teile kamen zu gross/zu klein
                if p.get("fase"):
                    # Kante brechen (wie FreeCAD Deburr): V-Fräser so weit neben der Kante und
                    # so tief, dass oben genau «fase_breite» weggenommen wird; die Spitze
                    # reicht «fase_extra» unter die Fase, damit sie sauber ausläuft.
                    import math
                    if w.get("typ") != "v":
                        warnen(bb, [f"Fase braucht einen V-Fräser – Werkzeug {slot} ist keiner."])
                        continue
                    halb = math.radians(float(w.get("v_winkel", 90)) / 2)
                    breite = max(0.05, float(p.get("fase_breite") or 0.5))
                    extra = max(0.0, float(p.get("fase_extra", 0.2)))
                    tiefe_f = breite / math.tan(halb) + extra
                    abstand = float(w.get("spitze") or 0) / 2 + extra * math.tan(halb)
                    winkel, _faktor = eintauch_werte(w)
                    # «Beginnt bei Tiefe»: die Kante liegt so tief (Stufe), die Fase bricht sie dort
                    tp, warn = kontur(
                        linien, 2 * abstand, starttiefe + tiefe_f, min(float(p["zustellung"]), tiefe_f),
                        w["vorschub"], w["eintauchen"],
                        seite=p.get("seite", "auto"),
                        gleichlauf=p.get("gleichlauf", True),
                        materialdicke=None, freifahrhoehe=cz, startabstand=sz, toleranz=tol,
                        start=home, aufmass=aufmass, rampe=winkel,
                        anfahren=p.get("anfahren", ""),
                        anfahr_radius=float(p.get("anfahr_radius") or 0.0), starttiefe=starttiefe,
                        nachbarn=_kontur_nachbarn(bearbeitungen, bb) if p.get("anfahren") == "bogen" else None)
                    if p.get("seite") == "linie":
                        warn.append("Fase «auf der Linie»: die Kante wird beidseitig gebrochen.")
                    if 2 * (breite + abstand) > float(w["durchmesser"]) + 1e-6:
                        warn.append("Fase breiter als der V-Fräser – er schneidet mit dem Schaft.")
                    sw = None
                else:
                    if p.get("tiefe_ab") == "haut":
                        # B-15 (Peter 01.11.): nach dem 3D-Fräsen mit Haut die Haut entlang des Umrisses durchtrennen.
                        # Tiefe = dickste Stelle der Haut entlang der Bahn (erst ohne Tiefe gerechnet) + Zugabe, ab
                        # der Haut gemessen (ablegen: relief_projektion.auf_haut); ein Durchgang; gilt als
                        # durchgehend – die Haltestege bleiben in der Haut stehen.
                        from .ops import relief_projektion
                        starttiefe, sw, zugabe, mdicke = 0.0, None, 0.0, 1.0
                        p = dict(p, starttiefe=0.0, schlichten=False, fase=False)
                        tp0, _w0 = kontur_mit(w, 0.0, 1.0, tiefe_=1.0)
                        w_h, h_aus = [], {}
                        t_h = relief_projektion.haut_tiefe(
                            auftrag, bb["auf_seite"], [(m.x, m.y) for m in tp0.moves if not m.rapid and m.z < -1e-6],
                            float(w["durchmesser"]), w_h,
                            relief_projektion.HAUT_ZUGABE + float(p.get("tiefe_ab_mm") or 0.0), h_aus)
                        warnen(bb, w_h)
                        if t_h is None:
                            continue
                        # Peter 02.11.: von der Materialoberfläche an in Zustellungen hinunter bis durch die Haut,
                        # die Haltestege (Höhe ab dem Boden) bleiben unten in der Haut stehen. Die eine Ebene ist
                        # mindestens so tief, dass die Stege ganz hineinpassen; ablegen setzt ihren Boden auf
                        # Haut − t_h und legt die Ebenen darüber
                        mit_stegen = p.get("stege_modus") != "keine" and bool(
                            p.get("stege_anzahl") or p.get("stege_modus") == "manuell")
                        t_e = max(t_h, float(p.get("stege_hoehe") or 0.0) + 0.5) if mit_stegen else t_h
                        bb["p"]["_haut_ueber"] = round(t_e - t_h, 4)
                        bb["p"]["_haut_tiefe"] = round(max(t_h, t_h - h_aus.get("oben", 0.0)), 3)
                        p = dict(p, tiefe=t_e, zustellung=t_e)
                        mdicke = t_e
                    tp, warn = kontur_mit(w, zugabe, p["zustellung"])
                    # Schnittfuge merken: eine Kontur der anderen Seite kann hier hineintreffen
                    try:
                        from shapely.geometry import LineString
                        from shapely.ops import unary_union
                        fuge = unary_union([LineString(list(pts) + ([pts[0]] if zu and len(pts) > 2 else []))
                                            for pts, zu in linien if len(pts) >= 2]).buffer(wirksam(w) / 2 + 0.05)
                        konturen_je.setdefault(bb["auf_seite"], []).append((fuge, float(p["tiefe"])))
                    except Exception:
                        pass
                if w.get("typ") == "v" and not p.get("fase"):
                    warn.append("mit V-Fräser ausgeschnitten – die Kante wird schräg (Fase).")
                if sw is not None:
                    t_r = float(p["tiefe"])
                    t_s = _schlicht_tiefe(p)
                    if t_s < t_r - 1e-6:
                        # B-2: weniger tief als das Schruppen – darunter bleibt die Schlicht-Zugabe stehen
                        tp_s, _w = kontur_mit(sw, 0.0, min(s_zust, t_s), schlichten=True, tiefe_=t_s)
                        if zugabe > 0:
                            warn.append(SCHLICHT_FLACHER % (_mm1(t_s), _mm1(t_r), _mm1(zugabe)))
                    else:
                        tp_s, _w = kontur_mit(sw, 0.0, s_zust, schlichten=True)
                    if t_s > t_r + 1e-6:
                        # B-2: tiefer als das Schruppen (Peter: das Schruppen lässt in Z Material stehen, damit das
                        # Werkstück verbunden bleibt; das Schlichten fräst auf die Endtiefe). Erst wie bisher bis zur
                        # Tiefe des Schruppens (die Wand mit der Zugabe ist dann weg), danach darunter in volles
                        # Material: Zustellung höchstens wie beim Schruppen, Vollnut-Vorschub des Schlichtwerkzeugs.
                        z_t = float(p.get("zustellung") or 0.0) or (t_s - t_r)
                        if p.get("schlicht_zustellung"):
                            z_t = min(z_t, float(p["schlicht_zustellung"]))
                        tp_t, w_t = kontur_mit(sw, 0.0, z_t, schlichten=True, tiefe_=t_s,
                                               start_=max(starttiefe, t_r), voll=True)
                        tp_s.moves = list(tp_s.moves) + list(tp_t.moves)
                        for a_ in ("steg_ringe", "steg_d", "stege_auto"):
                            if hasattr(tp_t, a_):
                                setattr(tp_s, a_, getattr(tp_t, a_))
                        warn.extend(x for x in w_t if x not in warn)
                        hst = float(p.get("stege_hoehe", 1.5) or 0.0)
                        if getattr(tp_t, "steg_ringe", None) and p.get("stege_modus") != "keine" \
                                and hst > t_s - t_r + 1e-6:
                            warn.append(STEGE_NIEDRIGER % (_mm1(t_s - t_r), _mm1(hst)))
                        try:
                            from shapely.geometry import LineString
                            from shapely.ops import unary_union
                            fuge = unary_union([LineString(list(pts) + ([pts[0]] if zu and len(pts) > 2 else []))
                                                for pts, zu in linien if len(pts) >= 2]).buffer(wirksam(sw) / 2 + 0.05)
                            konturen_je.setdefault(bb["auf_seite"], []).append((fuge, t_s))
                        except Exception:
                            pass
                    ablegen(bb, s_slot, tp_s, teil="schlicht")
            elif art == "tasche":
                from .ops.tasche import tasche
                aufmass = float(p.get("aufmass") or 0.0) + masskorrektur
                winkel, faktor = eintauch_werte(w)
                vorher = list(ausgeraeumt)
                n_vorher = len(vorher)
                d_r = raeumbreite(w, float(p["tiefe"]), float(p["zustellung"] or p["tiefe"]))
                # Restmaterial: ein grösserer Fräser räumt die Fläche zuerst, das
                # Werkzeug der Tasche fräst danach nur, wo er nicht hinkommt
                r_slot = p.get("raeum_werkzeug") or ""
                geraeumt = False
                # V-Fräser: schräge Wand, oben genau auf der Linie («oben», Standard) oder
                # unten auf der Linie und oben breiter («unten», wie bisher)
                import math
                wand_tan = 0.0
                # «bis auf das 3D-Modell»: keine schräge Wand – das Aufsetzen auf das Modell rechnet mit der Form
                if w.get("typ") == "v" and p.get("v_wand", "oben") != "unten" and p.get("tiefe_ab") != "bis_modell":
                    wand_tan = math.tan(math.radians(float(w.get("v_winkel", 60)) / 2))
                wand_innen = float(p["tiefe"]) * wand_tan     # so weit liegt die Wand unten innen
                if r_slot and r_slot != slot:
                    gw = als_2d(werkzeuge.get(r_slot))
                    if gw is not None and gw.get("typ") == "bohrer":
                        warn.append(f"Räumwerkzeug {r_slot} ist ein Bohrer – ohne Vorräumen.")
                        gw, r_slot = None, ""
                    if gw is None:
                        if r_slot:
                            warn.append(f"Räumwerkzeug {r_slot} ist nicht eingerichtet – ohne Vorräumen.")
                    else:
                        gw = _mit_vorschub(gw, p.get("raeum_vorschub_prozent") or p.get("vorschub_prozent"),
                                           p.get("raeum_vorschub_mm"))
                        p["_raeum_d"] = float(gw["durchmesser"])   # für die Anzeige der Zustellungen
                        tiefe_t = float(p["tiefe"])
                        g_zust = float(p.get("raeum_zustellung") or 0.0)
                        if g_zust <= 0:
                            g_zust = float(gw["durchmesser"]) / 2
                        g_zust = min(tiefe_t, g_zust)
                        d_g = raeumbreite(gw, tiefe_t, g_zust)
                        if d_g <= d_r + 1e-6:
                            # Sicherung: ein Räumwerkzeug, das nicht grösser ist, bringt nichts
                            warn.append(f"Räumwerkzeug {r_slot} ist nicht grösser als das Werkzeug der Tasche "
                                        "– ohne Vorräumen gerechnet.")
                            p["raeum_werkzeug"] = ""      # Anzeige: kein «Restmaterial»
                        else:
                            g_winkel, g_faktor = eintauch_werte(gw, "raeum_")
                            tp_g, warn_g = tasche(
                                linien, d_g, tiefe_t, g_zust, gw["vorschub"], gw["eintauchen"],
                                bahnabstand_prozent=p.get("raeum_bahnabstand") or p.get("bahnabstand", 40.0),
                                gleichlauf=p.get("gleichlauf", True),
                                freifahrhoehe=cz, startabstand=sz, toleranz=tol, start=home,
                                ausgeraeumt=ausgeraeumt, aufmass=aufmass + zugabe + wand_innen + mess(gw),
                                rampe=g_winkel, vollnut=g_faktor,
                                strategie=p.get("raeum_strategie") or p.get("strategie", "runden"),
                                adaptiv_last=float(p.get("raeum_adaptiv_last") or p.get("adaptiv_last") or 20),
                                wechselnd=p.get("adaptiv_wechsel") is not False, weit_faktor=ADAPTIV_WEIT,
                                runden_zustellung=float((gw.get("empfehlung") or {}).get("zustellung") or 0)
                                or d_g / 4,
                                starttiefe=starttiefe)
                            # eine Form, in die das grosse Räumwerkzeug nicht passt, räumt das Taschenwerkzeug
                            from .ops.tasche import SCHMALER_TEIL
                            warn += ["Räumen: " + t for t in warn_g
                                     if not t.endswith(SCHMALER_TEIL.split("%d")[1])]
                            ablegen(bb, r_slot, tp_g, teil="raeumen")
                            geraeumt = True
                if w.get("typ") == "v":
                    if wand_tan > 0:
                        warn.append(("Tasche mit V-Fräser: schräge Wand – oben genau auf der Linie, unten %s mm "
                                     "weiter innen. Den Boden räumt nur die Spitze (Ø %s mm).")
                                    % (("%.2f" % wand_innen).replace(".", ","), ("%g" % d_r).replace(".", ",")))
                    else:
                        warn.append("Tasche mit V-Fräser: den Boden räumt nur die Spitze (Ø %s mm) aus – unten "
                                    "auf der Linie, oben wird die Tasche breiter (schräge Wand)."
                                    % ("%g" % d_r).replace(".", ","))
                tp, warn_t = tasche(
                    linien, d_r, p["tiefe"], p["zustellung"], w["vorschub"], w["eintauchen"],
                    bahnabstand_prozent=p.get("bahnabstand", 40.0),
                    gleichlauf=p.get("gleichlauf", True),
                    freifahrhoehe=cz, startabstand=sz, toleranz=tol, start=home,
                    ausgeraeumt=ausgeraeumt, aufmass=aufmass + zugabe + mess(w),
                    rampe=winkel, vollnut=faktor,
                    # Restmaterial nach dem Räumen: in Runden (adaptiv lohnt sich für Zwickel nicht)
                    # V-Fräser: schräge Wand gibt es nur in Runden – seit adaptiv die Vorgabe ist (01.11.) ohne Hinweis
                    strategie="runden" if geraeumt or (w.get("typ") == "v" and wand_tan > 0)
                    else p.get("strategie", "runden"),
                    adaptiv_last=float(p.get("adaptiv_last") or 20),
                    dogbone=bool(p.get("dogbone", False)) and sw is None,
                    wand_tan=wand_tan, starttiefe=starttiefe,
                    # adaptiv: was in Runden geräumt wird, nicht mit der ganzen Schneidenlänge zustellen
                    runden_zustellung=float((w.get("empfehlung") or {}).get("zustellung") or 0) or d_r / 4,
                    # Peter 31.10.: Gleich- und Gegenlauf im Wechsel (nur Räumen); Peter 01.11.: Standard für adaptiv
                    # («kein Grund mehr für das alte adaptive Räumen») – nur ein ausdrückliches false schaltet ab
                    wechselnd=p.get("adaptiv_wechsel") is not False, weit_faktor=ADAPTIV_WEIT)
                if geraeumt:
                    warn_t = [("Das Räumwerkzeug hat schon alles ausgeräumt – kein Restmaterial."
                               if t.startswith("Material ist hier schon ausgeräumt") else t) for t in warn_t
                              if not t.startswith("Beginnt bei Tiefe")]     # steht schon beim Räumen
                    warn = [t[len("Räumen: "):] if t.startswith("Räumen: Beginnt bei Tiefe") else t for t in warn]
                warn += warn_t
                # Peter 25.10.: weite Taschen – im Dialog «adaptiv» vorschlagen (Feld adaptiv_vorschlag)
                if p.get("strategie", "runden") != "adaptiv" and not geraeumt and w.get("typ") != "v":
                    from .ops.tasche import adaptiv_lohnt
                    p["_adaptiv_vorschlag"] = adaptiv_lohnt(linien, float(w["durchmesser"]))
                else:
                    p["_adaptiv_vorschlag"] = False
                if sw is not None:
                    tp_s, _w = tasche(
                        linien, raeumbreite(sw, float(p["tiefe"]), float(s_zust or p["tiefe"])), p["tiefe"], s_zust,
                        sw["vorschub"], sw["eintauchen"],
                        gleichlauf=_schlicht_gleichlauf(p),
                        freifahrhoehe=cz, startabstand=sz, toleranz=tol, start=home,
                        ausgeraeumt=vorher, aufmass=aufmass + mess(sw), nur_rand=True, rand_breite=zugabe,
                        rampe=eintauch_werte(sw)[0], vollnut=1.0,
                        dogbone=bool(p.get("dogbone", False)), starttiefe=starttiefe)
                    ausgeraeumt.extend(vorher[n_vorher:])
                    ablegen(bb, s_slot, tp_s, teil="schlicht")
                    # Fable 07.11.: Formen, in die der Schruppfräser nicht passt – das Schlichten räumt sie allein; nur
                    # wo auch der Schlichtfräser nicht hineinpasst, bleiben sie stehen
                    from .ops.tasche import SCHMALER_SCHLICHT, SCHMALER_TEIL
                    ende_ = SCHMALER_TEIL.split("%d")[1]

                    def _schmal(ws):
                        n_ = 0
                        for t in ws:
                            kopf_ = t.split(" ", 1)[0]
                            if t.endswith(ende_) and kopf_.isdigit():
                                n_ += int(kopf_)
                        return n_
                    n_r, n_s = _schmal(warn), _schmal(_w)
                    if n_r or n_s:
                        warn = [t for t in warn if not (t.endswith(ende_) and t.split(" ", 1)[0].isdigit())]
                        if n_r > n_s:
                            warn.append(SCHMALER_SCHLICHT % (n_r - n_s))
                        if n_s:
                            warn.append(SCHMALER_TEIL % n_s)
            elif art == "planen":
                from .ops.planen import planen
                pts = [q for o in bb["objekte"] for q in o["punkte"]]
                rechteck = (min(q[0] for q in pts), min(q[1] for q in pts),
                            max(q[0] for q in pts), max(q[1] for q in pts))
                ue = p.get("ueberstand")
                kreis = None
                if ru:
                    kreis = (float(ru["mitte"][0]), float(ru["mitte"][1]), float(ru["radius"]))
                tp, warn = planen(rechteck, d, float(p["tiefe"]), float(p["zustellung"]),
                                  w["vorschub"], w["eintauchen"],
                                  bahnabstand_prozent=float(p.get("bahnabstand", 60)),
                                  richtung=p.get("richtung", "x"),
                                  ueberstand=None if ue is None else float(ue),
                                  freifahrhoehe=cz, startabstand=sz, start=home, kreis=kreis)
                if len([x for x in bearbeitungen if x["art"] != "planen"]):
                    warn.append("Die anderen Bearbeitungen fräsen ab der neuen Oberfläche (%g mm tiefer)."
                                % float(p["tiefe"]))
            elif art == "gewinde":
                import math
                from .ops.gewinde import gewinde
                from .ops.bohren import bohren
                if w.get("typ") != "gewinde" and "werkzeuge" in auftrag:
                    # Peter, 20.10.: keine Zeit mit einem Werkzeug, das kein Gewinde schneiden kann
                    warnen(bb, [f"Werkzeug {slot} ist kein Gewindefräser – Gewinde ausgelassen. Einen "
                                "Gewindefräser wählen (Werkzeuge … › Art «Gewindefräser»)."])
                    continue
                kreise = []
                for pts, _g in linien:
                    xs = [q[0] for q in pts]
                    ys = [q[1] for q in pts]
                    mitte = ((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2)
                    if not any(math.dist(mitte, k[0]) < 1e-3 for k in kreise):
                        kreise.append((mitte, ((max(xs) - min(xs)) + (max(ys) - min(ys))) / 2))
                innen = p.get("innen", True)
                tp, warn, infos = gewinde(
                    kreise, w, float(p["tiefe"]), w["vorschub"], w["eintauchen"],
                    steigung=float(p["steigung"]) if p.get("steigung") else None,
                    innen=innen, rechts=p.get("rechts", True),
                    durchgaenge=int(p.get("durchgaenge") or 1), passung=float(p.get("passung") or 0),
                    freifahrhoehe=cz, startabstand=sz, toleranz=tol, start=home)
                if w.get("typ") != "gewinde":
                    warn.append(f"Werkzeug {slot} ist kein Gewindefräser – bitte einen einrichten "
                                "(Werkzeuge … › Art «Gewindefräser»).")
                for i in infos:
                    # Nenn-Ø des Gewindes (auch wenn der Kreis das Kernloch zeigt) – 3D-Ansicht, Prüfung
                    nenn = i.get("nenn") or next((k[1] for k in kreise if math.dist(k[0], i["mitte"]) < 1e-6), 0.0)
                    gewinde_liste.append({
                        "bearbeitung": bb["id"], "werkzeug": slot, "seite": int(bb["auf_seite"]),
                        "mitte": [round(i["mitte"][0], 6), round(i["mitte"][1], 6)], "nenn": nenn,
                        "steigung": i["steigung"], "kernloch": round(i["kernloch"], 4), "innen": i["innen"],
                        "rechts": bool(p.get("rechts", True)), "name": i["name"],
                        # Oberkante (nach Planfräsen tiefer) und Tiefe des Gewindes, Material-Z
                        "oben": -plan_tiefen.get(bb["auf_seite"], 0.0), "tiefe": float(p["tiefe"]),
                        "fraeser": float(w.get("durchmesser") or 0)})
                namen_ = sorted({i["name"] for i in infos})
                if namen_:
                    warn.append(("Innengewinde " if innen else "Aussengewinde ") + ", ".join(namen_) +
                                (" – Kernloch " + ", ".join("Ø %.2f" % i["kernloch"] for i in infos[:3])
                                 if innen else ""))
                k_slot = p.get("kernloch_werkzeug") or ""
                if innen and k_slot and infos:
                    kw = als_2d(werkzeuge.get(k_slot))
                    if kw is None:
                        warn.append(f"Werkzeug {k_slot} für das Kernloch ist nicht eingerichtet.")
                    else:
                        loecher = []
                        for info in infos:
                            mitte = info["mitte"]
                            r_k = info["kernloch"] / 2
                            loecher.append(([(mitte[0] + r_k * math.cos(2 * math.pi * i / 72),
                                              mitte[1] + r_k * math.sin(2 * math.pi * i / 72))
                                             for i in range(73)], True))
                        zust_k = float((kw.get("empfehlung") or {}).get("zustellung") or 1.0)
                        tiefe_k = float(p["tiefe"]) + max(i["steigung"] for i in infos)
                        k_bohrer = kw.get("typ") == "bohrer"
                        if k_bohrer:
                            from .werkzeugdb import spitzenlaenge
                            tiefe_k += spitzenlaenge(kw)
                        tp_k, warn_k = bohren(loecher, float(kw["durchmesser"]), tiefe_k,
                                              zust_k, kw["eintauchen"], cz, sz, start=home,
                                              methode="bohren" if k_bohrer else "helix",
                                              vorschub=kw["vorschub"], toleranz=tol, bohrer=k_bohrer)
                        warn += warn_k
                        ablegen(bb, k_slot, tp_k, teil="kernloch")
            elif art == "bohren":
                from .ops.bohren import bohren
                ist_bohrer = w.get("typ") == "bohrer"
                tiefe_b = float(p["tiefe"])
                if ist_bohrer and p.get("spitze_dazu", True):
                    # voller Durchmesser bis zur Tiefe: die Spitze reicht so viel weiter
                    from .werkzeugdb import spitzenlaenge
                    tiefe_b += spitzenlaenge(w)
                if ist_bohrer and p.get("methode", "bohren") == "helix":
                    warn.append(f"Werkzeug {slot} ist ein Bohrer – er bohrt nur in der Mitte (keine Helix).")
                tp, warn_b = bohren(linien, d, tiefe_b, p["zustellung"], w["eintauchen"],
                                    cz, sz, start=home, methode=p.get("methode", "bohren"),
                                    vorschub=w["vorschub"], gleichlauf=p.get("gleichlauf", True),
                                    toleranz=tol, aufmass=float(p.get("aufmass") or 0.0) + masskorrektur + mess(w),
                                    bohrer=ist_bohrer)
                warn += warn_b
            elif art == "vcarve":
                from .ops.vcarve import vcarve
                if w.get("typ") != "v" and "werkzeuge" in auftrag:
                    warnen(bb, [f"Werkzeug {slot} ist kein V-Fräser – ausgelassen."])
                    continue
                fslot = bb.get("flach")
                fw = als_2d(werkzeuge.get(fslot)) if fslot else None
                if fw is not None and fw.get("typ") == "v":
                    warn.append(f"Werkzeug {fslot} für den Flachbereich ist ein V-Fräser – "
                                "dafür braucht es einen Schaftfräser.")
                    fw = None
                tp, w2, extra = vcarve(linien, w, p, cz, sz, tol, start=home,
                                       flach_werkzeug=fw)
                _ab_stufe(tp, starttiefe, cz)
                if extra:
                    _ab_stufe(extra.get("weg"), starttiefe, cz)
                warn = list(w2) + warn
                if extra:
                    ablegen(bb, fslot, extra["weg"], flach=True)
            elif art in ("relief_schruppen", "relief_schlichten"):
                # Zwischenspeicher: unveränderte 3D-Bearbeitungen nicht neu rechnen; beim ersten
                # Mal alle noch fehlenden gleichzeitig (ops/relief_speicher.py)
                from .ops import relief_speicher
                if nur_pruefen:
                    # nichts rechnen: nur abspielen, was der 3D-Speicher schon hat
                    a3 = (art, p, w, auftrag, bb["auf_seite"], cz, sz, home, bb["name"], tol)
                    if p.get("rest") or not relief_speicher.vorhanden(relief_speicher.schluessel(*a3)):
                        veraltet[bb["id"]] = True
                        neu_gerechnet.remove(bb["id"])
                        continue
                if not vorab_3d and not nur_pruefen:
                    vorab_3d.append(True)
                    aufgaben = []
                    for x in ablauf[ablauf.index(bb):]:
                        # «Nur Restmaterial» braucht die Bahnen davor: nicht vorab, sondern der Reihe nach
                        if x["art"] in relief_speicher.ARTEN and x["objekte"] and x["werkzeug"] in werkzeuge \
                                and not x["p"].get("rest"):
                            px = x["p"]
                            aufgaben.append((x["art"], px, _mit_vorschub(werkzeuge[x["werkzeug"]],
                                             px.get("vorschub_prozent"), px.get("vorschub_mm")),
                                             auftrag, x["auf_seite"], cz, sz, home, x["name"], tol))
                    neu_3d = [a for a in aufgaben if not relief_speicher.vorhanden(relief_speicher.schluessel(*a))]
                    if len(neu_3d) >= 2 and (os.cpu_count() or 1) >= 2:
                        fortschritt.melden(" · ".join(dict.fromkeys(
                            a[8].split(" (")[0] for a in neu_3d)) + " (" + tr("gleichzeitig") + ")")
                        relief_speicher.vorab(neu_3d)
                vorher = [(t["tp"], werkzeuge[t["slot"]]) for t in teile
                          if t["art"] in relief_speicher.ARTEN and t["seite"] == bb["auf_seite"]
                          and t["slot"] in werkzeuge] if p.get("rest") else None
                tp, warn = relief_speicher.bearbeiten(art, p, w, auftrag, bb["auf_seite"], cz, sz, home,
                                                      bb["name"], tol, vorher=vorher)
                # Impeller 08.11.: folgt auf derselben Seite schon ein 3D-Schruppen «nur Restmaterial», ist der
                # Vorschlag dafür erledigt – die Meldung steht dann bei jenem (was er noch stehen lässt)
                if art == "relief_schruppen" and not p.get("rest") and any(
                        x["art"] == "relief_schruppen" and x["p"].get("rest") and x["auf_seite"] == bb["auf_seite"]
                        for x in ablauf[ablauf.index(bb) + 1:]):
                    warn = [t for t in warn if not str(t).startswith("Restmaterial:")]
                # Übergänge des Schlichtens über dem, was die Bearbeitungen davor stehen liessen: nach dem Ordnen
                # der Programme für alle Bearbeitungen (ops/freifahren.ueber_bestand, unten)
                if bb["id"] in wechsel.values():
                    # im Wechsel mit «Böden und Wände»: Stufe für Stufe ablegen (Reihenfolge: ordnung)
                    from .ops import boeden_waende as _bw
                    bw_bb = next(x for x in bearbeitungen if wechsel.get(x["id"]) == bb["id"])
                    st_ = _bw.stufe_mm(bw_bb["p"], werkzeuge[bw_bb["werkzeug"]])
                    warnen(bb, warn)
                    for k_, t_ in _bw.aufteilen(tp, st_, cz):
                        ablegen(bb, slot, t_, len(liste), teil=("stufe%d" % (k_ + 1)) if k_ else None)
                    continue
            elif art == "boeden_waende":
                from .ops import boeden_waende as _bw
                vor = _schruppen_vor(bearbeitungen, bb)
                vor_w = werkzeuge.get(vor["werkzeug"]) if vor is not None else None
                stufen_, warn_b = _bw.bearbeiten(p, w, auftrag, bb["auf_seite"], cz, sz, home, bb["name"], tol,
                                                 vor_w, vor["p"] if vor_w is not None else None)
                warn += warn_b
                s_id = str(p.get("im_wechsel_mit") or "")
                if s_id and bb["id"] not in wechsel:
                    s_bb = next((x for x in bearbeitungen if x["id"] == s_id), None)
                    if s_bb is None or s_bb["art"] != "relief_schruppen":
                        warn.append("Das 3D-Schruppen für «im Wechsel» ist nicht (mehr) eingeschaltet – "
                                    "Böden und Wände kommen nach dem ganzen Schruppen.")
                    elif s_bb["auf_seite"] != bb["auf_seite"]:
                        warn.append("Im Wechsel geht nur mit einem 3D-Schruppen derselben Seite.")
                    elif s_bb["werkzeug"] != bb["werkzeug"]:
                        n_ = len(stufen_)
                        warn.append("Nicht im Wechsel: anderer Fräser als beim 3D-Schruppen (das wären %d "
                                    "Werkzeugwechsel) – Böden und Wände kommen nach dem ganzen Schruppen."
                                    % max(2, 2 * n_ - 1))
                    else:
                        warn.append("Nicht im Wechsel: «Böden und Wände» steht vor seinem 3D-Schruppen – in der "
                                    "Liste dahinter ziehen.")
                warnen(bb, warn)
                if bb["id"] in wechsel:
                    for k_, t_ in stufen_:
                        ablegen(bb, slot, t_, None, teil=("stufe%d" % (k_ + 1)) if k_ else None)
                elif stufen_:
                    from .model import Toolpath as _TP
                    ablegen(bb, slot, _TP(name=bb["name"], moves=[m for _k, t_ in stufen_ for m in t_.moves],
                                          senkrecht=sum(t_.senkrecht for _k, t_ in stufen_)), None)
                continue
            else:
                continue
        except ImportError as e:
            if "shapely" in str(e):
                warnen(bb, ["Shapely fehlt – einmalig installieren mit "
                            "«sudo apt install python3-shapely». Ausgelassen."])
                continue
            raise
        warnen(bb, warn)
        ablegen(bb, slot, tp, len(liste))
    nachtragen()

    # 3D-Schlichten nach einem Schruppen, das Restmaterial stehen liess (Peter 25.10., Impeller): der Kugelfräser
    # griffe dort voll ein – Hinweis, solange kein «nur Restmaterial»-Schruppen dazwischen liegt
    import re as _re2
    for i_, bb in enumerate(bearbeitungen):
        if bb["art"] != "relief_schlichten" or not bb["objekte"] or bb["id"] not in statistik:
            continue
        davor = [x for x in bearbeitungen[:i_] if x["art"] == "relief_schruppen" and x["auf_seite"] == bb["auf_seite"]]
        rest_t = None
        for x in davor:
            if x["p"].get("rest"):
                rest_t = None                      # danach kam ein Restmaterial-Schruppen
                continue
            for t_ in op_warnungen.get(x["id"], []):
                if t_.startswith("Restmaterial: Der Fräser"):
                    rest_t = t_
        if rest_t:
            m_ = _re2.search(r"bis ([0-9]+(?:,[0-9]+)?) mm über", rest_t)
            v_ = _re2.search(r"Schaftfräser Ø ([0-9]+(?:,[0-9]+)?)", rest_t)
            # zwei ganze Sätze (ein eingesetztes « mit Ø …» blieb in der englischen Oberfläche deutsch)
            if v_:
                warnen(bb, ["Vorher steht noch Restmaterial (bis %s mm über dem Aufmass) – dort griffe der Fräser "
                            "voll ein. Zuerst ein 3D-Schruppen «nur Restmaterial» mit Ø %s."
                            % (m_.group(1) if m_ else "?", v_.group(1))])
            else:
                warnen(bb, ["Vorher steht noch Restmaterial (bis %s mm über dem Aufmass) – dort griffe der Fräser "
                            "voll ein. Zuerst ein 3D-Schruppen «nur Restmaterial»." % (m_.group(1) if m_ else "?")])

    for bb in bearbeitungen:
        if bb["art"] == "tasche" and bb["id"] in statistik:
            statistik[bb["id"]]["adaptiv_vorschlag"] = bool(bb["p"].get("_adaptiv_vorschlag"))
    for bid_, st_ in stege_auto.items():
        if bid_ in statistik:
            statistik[bid_]["stege_auto"] = st_
    for bid_, v_ in veraltet.items():
        if bid_ in statistik:
            statistik[bid_]["veraltet"] = v_
    if not teile:
        warnungen.insert(0, "Nichts zu fräsen – keine Datei geschrieben.")
        return {"ok": False, "datei": "", "dateien": [], "programme": [], "bericht": [],
                "warnungen": warnungen, "veraltet": veraltet, "neu_gerechnet": neu_gerechnet}

    # Umspannen (Peter 27.10., B-5): Bahnen an den Sperrzonen um die Spannmittel zerschneiden – Spannung 1 fräst alles
    # ausserhalb, Spannung 2 den Rest (ops/umspannen.py). Aus: nichts geändert.
    umspann = _umspannen_teilen(teile, auftrag, werkzeuge, cz, home, teil_bericht, teil_minuten, op_warnungen,
                                bearbeitungen)
    if umspann:
        umspann["_auftrag"] = auftrag
        warnungen.extend(x for x in umspann["hinweise"] if x not in warnungen)
    # Abschnitte: ununterbrochen dasselbe Werkzeug auf derselben Seite (und in derselben Spannung)
    folge = abl.abschnitte(teile, modus)
    nummer = abl.werkzeugnummern(folge)
    nach_key = {t["key"]: t for t in teile}
    # Sicherheitsprüfung 04.11.: läuft das Sicherheitsnetz nicht (Modul fehlt, abgeschaltet), steht es im Ergebnis
    netz_aus = ""
    if os.environ.get("HIRCH_UEBER_BESTAND", "1") == "0":
        netz_aus = "abgeschaltet"
    elif not auftrag.get("luft") and not nur_pruefen:
        if _ueber_bestand(folge, nach_key, werkzeuge, bearbeitungen, auftrag, cz, sz, home, plan_tiefen) < 0:
            netz_aus = "modul"
    ohne_mitte = [] if auftrag.get("luft") or nur_pruefen else \
        _eintauchen_ohne_mitte(folge, nach_key, werkzeuge, auftrag, float(h.get("materialdicke") or 18.0))
    h2_geschaetzt = None
    if umspann:
        h2_geschaetzt = _umspannen_hoehe2(umspann, folge, nach_key, werkzeuge, home)
    # Spindel und Halterung (3D): streift Schaft, Mutter, Spindel oder Halterung das Material, wird die Stelle
    # markiert und als Fehler der Prüfung gemeldet (ops/relief_halterung.py). Bis 30.10. brach das die Rechnung ab;
    # Peter (B-10): trotzdem fräsen können – die Dateien entstehen, «G-Code speichern» fragt wie bei jedem Fehler
    # «Trotzdem speichern?», der Hinweis bleibt an der Bearbeitung sichtbar.
    from .ops import relief_halterung
    koll = relief_halterung.pruefen(teile, folge, werkzeuge, auftrag, [
        x["id"] for x in bearbeitungen
        if x["art"] in relief_halterung.ARTEN_3D or x["p"].get("tiefe_ab") in ("modell", "haut", "bis_modell")])
    halter_befund = None
    if koll:
        name = next((x["name"] for x in bearbeitungen if x["id"] == koll["bearbeitung"]), "")
        op_warnungen.setdefault(koll["bearbeitung"], []).append(koll["text"])
        halter_befund = {"stufe": "fehler", "text": f"Kollision – {name}: {koll['text']}", "ort": koll["ort"],
                         "bearbeitung": koll["bearbeitung"], "seite": koll["seite"]}
    zweiseitig = any(a["seite"] == 2 for a in folge) or bool((auftrag.get("zweiseitig") or {}).get("an"))

    fortschritt.melden(tr("G-Code schreiben"))
    from .post import postprozessor
    from .post import tap_mm
    post_key = auftrag.get("postprozessor", "vcarve_mm")  # roher Auftrag ohne Angabe: wie bisher
    try:
        post = postprozessor(post_key)
    except KeyError:                                   # gelöscht oder auf anderem Rechner angelegt
        from .post import STANDARD
        post = postprozessor(STANDARD)
        warnungen.append(f"Postprozessor «{post_key}» gibt es nicht (mehr) – Ausgabe für «{post.name}». "
                         "Unter «Auftrag einrichten → Ausgabe» einen anderen wählen.")
    post_name, endung, eine_datei = post.name, post.endung, post.eine_datei
    if str((auftrag.get("maschine") or {}).get("werkzeugwechsel") or "") in ("dateien", "halt"):
        from .post import eine_datei as _eine
        eine_datei = _eine(auftrag)                   # Maschinen-Einstellung geht vor
    # Kurze Dateinamen (z. B. RichAuto: 8 Zeichen, GROSS): SCHILD_1.TAP statt Schild_1_D3.tap
    kurz = int(getattr(post, "kurzname", 0) or 0)
    if getattr(post, "grossbuchstaben", False):
        endung = endung.upper()
    boegen = bool(auftrag.get("boegen", False))       # G2/G3 statt kurzer Geraden
    ziel = auftrag["ausgabe"]
    stamm = os.path.splitext(ziel)[0]
    ziel = stamm + endung
    mehrere = len(folge) > 1
    # Je Abschnitt ein Teil im Format «G-Code (mm)» für Vorschau und Prüfung (intern,
    # in Materialkoordinaten der jeweiligen Seite: Nullpunkt unten links auf der
    # Oberfläche). Gespeichert werden die Ausgabedateien – verschoben auf den
    # gewählten Nullpunkt.
    teile_ordner = os.path.join(os.path.dirname(os.path.abspath(ziel)), ".teile")
    os.makedirs(teile_ordner, exist_ok=True)
    letzte_jobs.clear()
    np_ = auftrag.get("nullpunkt") or {}
    ox, oy = float(np_.get("x") or 0.0), float(np_.get("y") or 0.0)
    prov_np = auftrag.get("nullpunkt_seiten") or {}
    for s_, p_ in sorted(prov_np.items(), key=lambda kv: int(kv[0])):
        if p_.get("kamera"):
            kx, ky = p_["kreuz"]
            dx, dy = p_["kamera"]
            warnungen.append(f"Seite {s_}: provisorischer Nullpunkt mit Kamera – die Kamera auf das orange Kreuz "
                             f"fahren ({_zahl_np(kx)} / {_zahl_np(ky)} mm von der Ecke unten links) und dort X0 Y0 "
                             f"setzen; der Kamera-Versatz X {_zahl_np(dx)} / Y {_zahl_np(dy)} mm ist im G-Code "
                             "eingerechnet.")
            continue
        warnungen.append(f"Seite {s_}: provisorischer Nullpunkt – X0 Y0 am orangen Kreuz antasten "
                         f"({_zahl_np(p_['x'])} / {_zahl_np(p_['y'])} mm von der Ecke unten links), "
                         "nicht an der Materialecke.")
    oz = float(h.get("materialdicke") or 0.0) if np_.get("z_unten") else 0.0
    # Luftfräsen: alles so weit höher, dass auch die tiefste Stelle «luft» mm über dem Werkstück bleibt
    luft = float(auftrag.get("luft") or 0.0)
    luft_hub = 0.0
    if luft:
        tiefste = min((m.z for t in teile for m in t["tp"].moves), default=0.0)
        luft_hub = luft + max(0.0, -tiefste)
        oz += luft_hub
    # Messkorrektur: Weg der Achsen im G-Code ausgleichen (nur auf Wunsch; besser in der Steuerung)
    ma_ = auftrag.get("maschine") or {}
    ska_x = ska_y = 1.0
    if float(ma_.get("skalierung_an") or 0) >= 0.5:
        ska_x = min(1.02, max(0.98, float(ma_.get("skalierung_x") or 1.0)))
        ska_y = min(1.02, max(0.98, float(ma_.get("skalierung_y") or 1.0)))
    ausgaben = []
    spann_alle = [x for x in (auftrag.get("spannmittel") or []) if len(x.get("punkte") or []) >= 3]
    bericht: List[str] = [f"Ordner: {os.path.dirname(os.path.abspath(ziel))}",
                          f"Steuerung: {post_name} · Rechenkern: {kern_info}"]
    programme = []
    gesamt = 0.0
    nach_id = {x["id"]: x for x in bearbeitungen}
    seiten_jobs: Dict[int, List] = {}
    seiten_infos: Dict[int, List] = {}
    seiten_zeit: Dict[int, float] = {}
    abschnitt_zeit: List[float] = []
    # Wiederaufnahme nach einem Abbruch: erst ab einer Bearbeitung (key) oder ab einer Zeile
    # eines Werkzeugeinsatzes (programm = Nummer in der Folge, zeile = Zeile der Vorschau-Datei)
    ab_hier = auftrag.get("ab") or None
    ab_aktiv = bool(ab_hier)
    genutzt: List[Dict] = []
    for nr, ab in enumerate(folge, 1):
        slot, seite = ab["slot"], ab["seite"]
        w = werkzeuge[slot]
        spann = [x for x in spann_alle if int(x.get("seite") or 1) == seite]
        spannung = int(ab.get("spannung") or 1)
        if umspann:
            # Spannung 1: nur ihre Spannmittel; Spannung 2: nur die eingezeichnete zweite Lage (ohne sie regelt
            # _umspannen_hoehe2 die Höhe der Eilgänge)
            from .ops import umspannen as _ums
            spann = [x for x in spann if _ums.spannung_von(x) == spannung]
        tps = [nach_key[k]["tp"] for k in ab["keys"]]
        if ab_aktiv:
            if "key" in ab_hier:
                if ab_hier["key"] not in ab["keys"]:
                    continue
                k0 = ab["keys"].index(ab_hier["key"])
                tps = tps[k0:]
                ab = dict(ab, keys=ab["keys"][k0:])
            else:
                if nr < int(ab_hier.get("programm") or 1):
                    continue
                tps = _ab_zeile(tps, int(ab_hier.get("zeile") or 1), safe, w)
                ab = dict(ab, keys=ab["keys"][len(ab["keys"]) - len(tps):])
            ab_aktiv = False
            if not tps:
                continue
        genutzt.append(ab)
        # Eine Datei: Werkzeuge nach erstem Einsatz durchnummerieren (T1, T2, …) –
        # die Steuerung hält nur an, wenn sich die Nummer ändert; ein Werkzeug, das
        # wiederkommt, behält seine Nummer.
        job = Job(tool_number=nummer[slot] if eine_datei else int(w.get("nummer", 1)),
                  spindle_rpm=int(w.get("drehzahl", 12000)), safe_z=safe, toolpaths=tps)
        if spann:
            gehoben = _ueber_spannmittel(job.toolpaths, spann, float(w["durchmesser"]) / 2,
                                         (job.home[0], job.home[1], safe))
            if gehoben:
                warnungen.append(f"{werkzeug_name(w, True)}: {gehoben} Eilgang/Eilgänge über Spannmittel angehoben.")
        # ab hier ändert sich der Job nicht mehr: G-Code-Zeilen nur einmal erzeugen
        # (Vorschau-Datei, Ausgabe und Zeilenanfänge teilen sie)
        tap_mm.merken(job)
        # provisorischer Nullpunkt (Kreuz in der Zeichnung) gilt nur für seine Seite
        np_s = prov_np.get(seite) or prov_np.get(str(seite))
        np_x, np_y = (float(np_s["x"]), float(np_s["y"])) if np_s else (ox, oy)
        verschoben = _verschoben(job, np_x, np_y, oz, ska_x, ska_y)
        seiten_jobs.setdefault((seite, spannung), []).append(verschoben)
        info_w = {"name": werkzeug_name(w), "durchmesser": w["durchmesser"]}
        seiten_infos.setdefault((seite, spannung), []).append(info_w)
        s_teil = f"s{seite}_" if zweiseitig else ""
        datei = os.path.join(teile_ordner, f"{os.path.basename(stamm)}_{s_teil}{nr}_{_kurz(w)}.tap")
        vorschau_text = tap_mm.write(job)
        with open(datei, "w", newline="") as fh:
            fh.write(vorschau_text)
        letzte_jobs[datei] = (job, os.path.getsize(datei))
        if eine_datei:
            text = vorschau_text if not boegen else tap_mm.write(job, boegen=True)
        else:
            s_name = f"{tr('_seite')}{seite}" if zweiseitig else ""
            if umspann:
                s_name += f"_spannung{spannung}"
            if kurz:
                aus = f"{stamm}_{nr}{endung}" if mehrere else stamm + endung
            else:
                aus = f"{stamm}{s_name}_{nr}_{_kurz(w)}{endung}" if mehrere else ziel
            text = post.write(verschoben, boegen=boegen, datei=os.path.basename(aus), werkzeug=info_w)
            with open(aus, "w", newline="") as fh:
                fh.write(text)
            ausgaben.append(aus)
        # Abschnitte für die Vorschau (Farbe der Bearbeitung): Startzeile je
        # Werkzeugweg in der Datei für die Vorschau (immer Format «G-Code (mm)», 4 Kopfzeilen)
        anfaenge: List[int] = []
        tap_mm.koerper(job, anfaenge)
        stuecke = []
        for k, a in zip(ab["keys"], anfaenge):
            bid, _, teil = k.split("@")[0].partition("#")
            x = nach_id.get(bid, {})
            name = (x.get("name") or "").split(" (")[0] + (TEILE.get(teil, "") if teil else "")
            stuecke.append({"zeile": 5 + a, "key": k, "farbe": x.get("farbe") or FARBE_ART.get(x.get("art"), ""),
                            "name": name})
        platz = f"Werkzeug {slot}" if slot in SLOTS else "Werkzeug"
        name_im_bericht = (f"T{job.tool_number}" if eine_datei else os.path.basename(ausgaben[-1]))
        minuten = programmzeit(text)
        kopf = (f"{nr}. " if mehrere else "") + (f"Seite {seite} · " if zweiseitig else "") + (
            f"{name_im_bericht} – {platz}: {werkzeug_name(w)} · "
            f"{len(text.splitlines())} Zeilen · {_zeit(minuten)}")
        bericht.append(kopf)
        bericht.extend("   " + teil_bericht[k] for k in ab["keys"])
        gesamt += minuten
        seiten_zeit[seite] = seiten_zeit.get(seite, 0.0) + minuten
        abschnitt_zeit.append(minuten)
        # negatives Aufmass beim 3D-Schlichten (Peter 31.10.): so viel tiefer gilt im Fräsergebnis nicht als «zu tief»
        aufm = [float(nach_id.get(k.split("@")[0].partition("#")[0], {}).get("aufmass") or 0.0) for k in ab["keys"]
                if str(nach_id.get(k.split("@")[0].partition("#")[0], {}).get("art", "")).startswith("relief_")]
        programme.append({"datei": datei, "werkzeug": slot, "name": werkzeug_name(w, True), "abschnitte": stuecke,
                          **({"aufmass_min": min(aufm)} if aufm and min(aufm) < 0 else {}),
                          "typ": w.get("typ") or "",
                          "minuten": round(minuten, 3),
                          "seite": seite, "t": nummer[slot], "spannung": spannung,
                          # Vorschau: konischer Kugelfräser als V-Fräser mit flacher Spitze Ø 2·R;
                          # «form3d» hat die genaue Form (Fräsergebnis 3D, Kollisionsprüfung)
                          "form": {"v": "v", "kugel": "kugel", "eckradius": "kugel", "bohrer": "v",
                                   "konisch": "v"}.get(w.get("typ"), "flach"),
                          "form3d": "konisch" if w.get("typ") == "konisch" else None,
                          "spitze": float(w.get("spitze") or 0) if w.get("typ") == "v"
                          else 2 * float(w.get("eckradius") or 0.25) if w.get("typ") == "konisch" else 0.0,
                          "eckradius": (float(w.get("durchmesser", 3)) / 2 if w.get("typ") == "kugel"
                                        else float(w.get("eckradius") or 0))
                          if w.get("typ") in ("kugel", "eckradius", "konisch") else 0.0,
                          "durchmesser": float(w["durchmesser"]),
                          # 3D-Ansicht: Schneide eingefärbt, Schaft bis zur Spannmutter (3D, 26.10.)
                          **_fraeser_laengen(w),
                          # Bohrer: Kegel mit dem Spitzenwinkel, darüber der volle Ø
                          "v_winkel": float(w.get("spitzenwinkel") or 118.0) if w.get("typ") == "bohrer"
                          else float(w.get("v_winkel", 60.0))})
    if ab_hier and not genutzt:
        return {"ok": False, "dateien": [], "programme": [], "bericht": [], "bearbeitungen": statistik,
                "warnungen": ["Wiederaufnahme: die gewählte Stelle wurde nicht gefunden."]}
    datei_seiten: List[int] = []           # je Datei ihre Seite (Endprüfung: was davor auf der Seite ausgeräumt ist)
    datei_teile: List[list] = []           # Sammeldatei: je Datei [(erste Zeile, Programm-Nr.)] je Werkzeug
    if eine_datei:
        dateien = []
        for seite_sp in sorted(seiten_jobs):
            seite, sp_ = seite_sp
            aus = (f"{stamm}_S{seite}{endung}" if kurz else f"{stamm}_seite{seite}{endung}") if zweiseitig else ziel
            if umspann:
                # zwei Dateien je Seite (Peter 27.10.): …_spannung1 / …_spannung2 (kurz: …_U1 / …_U2)
                aus = os.path.splitext(aus)[0] + (f"U{sp_}" if kurz else f"_spannung{sp_}") + endung
            grenzen: List[int] = []
            with open(aus, "w", newline="") as fh:
                fh.write(post.write_mehrere(seiten_jobs[seite_sp], boegen=boegen, datei=os.path.basename(aus),
                                            werkzeuge=seiten_infos[seite_sp], grenzen=grenzen))
            dateien.append(aus)
            datei_seiten.append(int(seite))
            # Endprüfung je Werkzeug (Fable 07.11.): ab welcher Zeile welches Programm
            nrn = [i_ for i_, p_ in enumerate(programme) if (p_.get("seite"), p_.get("spannung")) == seite_sp]
            datei_teile.append(list(zip(grenzen, nrn)) if len(nrn) == len(grenzen) else [])
        if zweiseitig and umspann:
            bericht.insert(2, "Je Seite zwei Dateien (Spannung 1 und 2): " + ", dann ".join(
                f"«{os.path.basename(d_)}»" for d_ in dateien) + " – vor Seite 2 das Werkstück wenden, auf die "
                "Passstifte legen und Z neu antasten.")
        elif zweiseitig:
            bericht.insert(2, f"Zwei Dateien: zuerst «{os.path.basename(dateien[0])}», dann das Werkstück "
                              "wenden, auf die Passstifte legen, Z neu antasten und "
                              f"«{os.path.basename(dateien[-1])}» fräsen.")
        elif umspann:
            bericht.insert(2, f"Zwei Dateien: «{os.path.basename(dateien[0])}» (Spannung 1) und "
                              f"«{os.path.basename(dateien[-1])}» (Spannung 2) – bei jedem Halt Werkzeug wechseln, "
                              "Nullpunkt Z neu antasten, dann weiterlaufen lassen.")
        elif mehrere:
            bericht.insert(2, f"Eine Datei «{os.path.basename(ziel)}» mit {len(programme)} Werkzeugeinsätzen – "
                              "bei jedem Halt Werkzeug wechseln, Nullpunkt Z neu antasten, "
                              "dann weiterlaufen lassen.")
    else:
        dateien = ausgaben
        if len(programme) == len(dateien):
            datei_seiten = [int(p_.get("seite") or 1) for p_ in programme]
        if mehrere:
            bericht.insert(2, f"{len(programme)} Dateien – in dieser Reihenfolge fräsen, "
                              "dazwischen das Werkzeug wechseln (Nullpunkt Z neu antasten)"
                              + (", vor Seite 2 das Werkstück wenden." if zweiseitig else "."))

    dicke = float(h.get("materialdicke") or 0) or None
    ablauf_info = abl.info(genutzt, nach_key, nach_id, {k: werkzeug_name(v, True) for k, v in werkzeuge.items()},
                           nummer, teil_minuten, abschnitt_zeit, seiten_zeit, op_warnungen, dicke, zweiseitig,
                           werkzeuge=werkzeuge, material=auftrag.get("material"),
                           maschine=auftrag.get("maschine"),
                           # «nach Spannung 2 frei» gilt dem Teil, das Spannung 2 fräst (Basis 27.10. 15:15)
                           nur_spannung2=(UMSPANNEN_FREI,) if umspann else ())
    # Was die Steuerung kann – für die Zusammenfassung (Halt in der Datei oder nächste Datei laden)
    ablauf_info["eine_datei"] = bool(eine_datei)
    ablauf_info["steuerung"] = post_name
    if not eine_datei:
        flach = [ab for so in ablauf_info["seiten"] for ab in so["abschnitte"]]
        for ab, aus in zip(flach, ausgaben):
            ab["datei"] = os.path.basename(aus)
    # Zahnvorschub zu klein: Hinweis mit Drehzahl zum Einstellen
    for so in ablauf_info["seiten"]:
        for ab in so["abschnitte"]:
            if ab.get("reibt"):
                text = (f"T{ab['t']} ({ab['werkzeug']}): nur {('%.3f' % ab['fz']).replace('.', ',')} mm je Schneide und Umdrehung – "
                        f"der Fräser reibt statt zu schneiden. Spindel auf ca. {_ganz(ab['drehzahl_ziel'])} U/min "
                        f"stellen oder Vorschub auf {_ganz(ab['vorschub_ziel'])} mm/min erhöhen.")
                if text not in warnungen:
                    warnungen.append(text)
    # Drehzahl von Hand: schafft die Maschine den passenden Vorschub nicht, wird der Span dünner
    for so in ablauf_info["seiten"]:
        for ab in so["abschnitte"]:
            w = werkzeuge.get(ab["slot"]) or {}
            e = w.get("empfehlung") or {}
            if w.get("schnittdaten") == "drehzahl" and e.get("vorschub_begrenzt"):
                text = (f"T{ab['t']} ({ab['werkzeug']}): bei {_ganz(w.get('drehzahl'))} U/min wären "
                        f"{_ganz(e['vorschub_ideal'])} mm/min richtig – die Maschine fährt höchstens {_ganz(e['vorschub'])} mm/min, "
                        "der Span wird dünner. Wenn die Spindel ruhig bleibt: Drehzahl etwas senken.")
                if text not in warnungen:
                    warnungen.append(text)
    # Drehzahl von Hand über der Höchstdrehzahl der Maschine (oder unter der Mindestdrehzahl): begrenzt
    for so in ablauf_info["seiten"]:
        for ab in so["abschnitte"]:
            w = werkzeuge.get(ab["slot"]) or {}
            if w.get("drehzahl_eingetragen"):
                text = (f"T{ab['t']} ({ab['werkzeug']}): eingetragen sind {_ganz(w['drehzahl_eingetragen'])} U/min, die Maschine "
                        f"ist auf {_ganz(w.get('drehzahl'))} U/min eingestellt – es gilt "
                        f"{_ganz(w.get('drehzahl'))} U/min"
                        + (", der Vorschub ist im selben Verhältnis kleiner." if w.get("schnittdaten") == "hand" else "."))
                if text not in warnungen:
                    warnungen.append(text)
    if ska_x != 1.0 or ska_y != 1.0:
        bericht.insert(2, "Weg der Achsen ausgeglichen (Messkorrektur): X × %s, Y × %s." % (
            ("%.4f" % (1 / ska_x)).replace(".", ","), ("%.4f" % (1 / ska_y)).replace(".", ",")))
    if luft:
        bericht.insert(2, f"Luftfräsen: alles {luft_hub:g} mm höher – die tiefste Stelle bleibt "
                          f"{luft:g} mm über dem Werkstück, es wird nichts geschnitten.")
    if umspann:
        bericht.insert(2, "Umspannen: zuerst die Dateien «Spannung 1» fräsen, dann – ohne das Teil zu verschieben – erst "
                          "die neuen Spannmittel setzen, dann die alten lösen, und «Spannung 2» fräsen.")
        for u_ in umspann["seiten"]:
            u_["zeit"] = round(sum(p_["minuten"] for p_ in programme
                                   if p_["seite"] == u_["seite"] and p_.get("spannung") == 2), 3)
    return {"ok": True, "datei": dateien[0], "kurzname": kurz, "luft": luft,
            **({"umspannen": umspann["seiten"]} if umspann else {}),
            "dateien": dateien, "datei_seiten": datei_seiten, "datei_teile": datei_teile,
            "programme": programme, "gewinde": gewinde_liste,
            "bericht": bericht, "warnungen": warnungen, "op_warnungen": op_warnungen,
            "warnungen_allgemein": [w for w in warnungen if w not in op_zeilen],
            "spannmittel": spann_alle,
            "soll": _soll(bearbeitungen, werkzeuge),
            "teil_vorschub": {k: round(v, 4) for k, v in teil_vorschub.items()},
            "bearbeitungen": statistik, "minuten": gesamt, "bahnen": bahnen, "ablauf": ablauf_info,
            "nullpunkt_provisorisch": {str(k): v for k, v in prov_np.items()},
            # Zwischenspeicher: je Bearbeitung veraltet (alte Bahnen gezeigt) oder aktuell; was neu gerechnet wurde
            "veraltet": veraltet, "neu_gerechnet": neu_gerechnet,
            **({"sicherheitsnetz_aus": netz_aus} if netz_aus else {}),
            # Fräser ohne Mittenschnitt taucht senkrecht ins Material (Endprüfung: Fehler, Zufallstests 06.11.)
            **({"ohne_mittenschnitt": [dict(f, name=werkzeug_name(werkzeuge[f["werkzeug"]], True))
                                       for f in ohne_mitte]} if ohne_mitte else {}),
            **({"hoehe2_geschaetzt": round(h2_geschaetzt, 2)} if h2_geschaetzt is not None else {}),
            # Spindel und Halterung: Kollision (3D-Ansicht: roter Klotz) und Befund für die Prüfung (dokument.py)
            **({"kollisionen": [koll], "halter_befund": halter_befund} if koll else {}),
            # nur auf Anfrage (dokument.stege_verteilen): Fräsbahn-Mitte der Konturen mit Haltestegen
            **({"steg_ringe": steg_ringe} if auftrag.get("_steg_ringe") else {})}


def _ab_zeile(tps, zeile: int, safe: float, w: Dict):
    """Werkzeugwege ab einer Zeile der Vorschau-Datei (4 Kopfzeilen, erste Bewegung in Zeile 5):
    was davor liegt, ist schon gefräst. Angefahren wird über die Sicherheitshöhe, dann im
    Eintauchvorschub hinunter auf den Punkt, an dem es weitergeht (dort ist frei)."""
    from .post import tap_mm
    from .model import Job, Move, Toolpath
    zeilen: List = []
    tap_mm.koerper(Job(toolpaths=tps, safe_z=safe), zeilen=zeilen)
    rumpf = max(0, zeile - 5)
    ziel = next(((t, i) for t, i, n in zeilen if n > rumpf), None)
    if ziel is None:
        return []
    t0, i0 = ziel
    neu = [Toolpath(name=tp.name, moves=list(tp.moves)) for tp in tps[t0:]]
    erster = neu[0]
    vorher = erster.moves[i0 - 1] if i0 > 0 else None
    rest = erster.moves[i0:]
    if vorher is not None and vorher.z < 0:
        tauch = float(w.get("eintauchen") or 300)
        anfahrt = [Move(True, vorher.x, vorher.y, safe), Move(True, vorher.x, vorher.y, 0.5),
                   Move(False, vorher.x, vorher.y, vorher.z, tauch)]
    elif vorher is not None:
        anfahrt = [Move(True, vorher.x, vorher.y, safe), Move(True, vorher.x, vorher.y, vorher.z)]
    else:
        anfahrt = []
    erster.moves = anfahrt + rest
    return neu


def _soll(bearbeitungen, werkzeuge) -> List[Dict]:
    """Was laut Zeichnung weg soll (für «Restmaterial zeigen» in der 3D-Vorschau):
    Taschen mit senkrechter Wand – Fläche (ohne Aufmass, ausser es wird geschlichtet)
    und Tiefe, in Materialkoordinaten der Seite."""
    aus: List[Dict] = []
    try:
        from .ops.tasche import _flaechen
        from .ops.bahn import offener_ring
    except ImportError:
        return aus
    for bb in bearbeitungen:
        if bb.get("art") != "tasche" or not bb.get("objekte"):
            continue
        p = dict(bb.get("p") or {}, objekte=bb["objekte"], id=bb.get("id"), auf_seite=bb.get("auf_seite"),
                 werkzeug=bb.get("werkzeug") or (bb.get("p") or {}).get("werkzeug"))
        w = als_2d(werkzeuge.get(p.get("werkzeug"), {}))
        if w.get("typ") == "v" and p.get("v_wand", "oben") != "unten":
            continue                              # schräge Wand ist gewollt
        ringe = [offener_ring([tuple(q) for q in o["punkte"]]) for o in p["objekte"] if o.get("geschlossen")]
        ringe = [r for r in ringe if len(r) >= 3]
        if not ringe:
            continue
        try:
            g = _flaechen(ringe)
            am = float(p.get("aufmass") or 0)
            if am > 0 and not p.get("schlichten"):
                g = g.buffer(-am, join_style=2)
        except Exception:
            continue
        if g is None or g.is_empty:
            continue
        polys = []
        for poly in getattr(g, "geoms", [g]):
            if poly.geom_type != "Polygon" or poly.area < 0.01:
                continue
            polys.append([[round(v, 3) for q in poly.exterior.coords for v in q]] +
                         [[round(v, 3) for q in r.coords for v in q] for r in poly.interiors])
        if polys:
            aus.append({"id": p.get("id"), "seite": int(p.get("auf_seite") or 1),
                        "tiefe": float(p.get("tiefe") or 0), "flaechen": polys})
    return aus


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    with open(argv[0], encoding="utf-8") as fh:
        auftrag = json.load(fh)
    try:
        ergebnis = rechnen(auftrag)
    except Exception as e:  # Fehler lesbar an Inkscape zurückgeben
        import traceback
        ergebnis = {"ok": False, "datei": "", "bericht": [],
                    "warnungen": [f"Fehler beim Rechnen: {e}"],
                    "traceback": traceback.format_exc()}
    print(json.dumps(ergebnis, ensure_ascii=False))
    return 0 if ergebnis["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
