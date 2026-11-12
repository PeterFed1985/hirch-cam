# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Werkzeugdatenbank, Materialien und Maschine von Hirch CAM.

Je Maschine eine Datei (B-25, maschinen.py): ~/.config/hirch-cam/maschinen/<id>.json – Maschinenwerte und Fräser
dieser Maschine. pfad() ist die Datei der geltenden Maschine. Eine Zeichnung speichert eine Kopie der
Werkzeuge, die sie benutzt (hirch:einstellungen), damit sie auch ohne die
Datenbank funktioniert.

Schnittdaten werden aus dem Material gerechnet:
    Drehzahl  n = vc · 1000 / (π · D)        (begrenzt auf die Spindel)
    Vorschub  f = n · Schneiden · fz         (fz = Vorschub pro Zahn)
Die Werte sind vorsichtige Startwerte für eine Hobby-/Werkstattfräse. Sie
lassen sich je Werkzeug überschreiben (Schnittdaten «von Hand»).

    python3 -m hirch_cam.werkzeugdb liste
    python3 -m hirch_cam.werkzeugdb speichern '{"name": …}'
    python3 -m hirch_cam.werkzeugdb loeschen ID
    python3 -m hirch_cam.werkzeugdb maschine '{"x": 600, …}'
"""
from __future__ import annotations

import copy
import json
import math
import os
import sys
import time
from typing import Any, Dict, List, Optional, Tuple

# Fräserformen: schaft, v (V-Fräser; mit «spitze» > 0 abgeflacht/Gravierstichel),
# kugel (Vollradius), eckradius (Schaftfräser mit Eckradius), konisch (konischer Kugelfräser:
# Kugelspitze mit Radius «eckradius», Kegelwinkel «v_winkel» gesamt, «durchmesser» = grösster Ø
# der Schneide), bohrer (Spiralbohrer mit «spitzenwinkel», Standard 118°; bohrt nur senkrecht)
FORMEN = {
    "schaft": "Schaftfräser",
    "v": "V-Fräser",
    "kugel": "Kugelfräser",
    "eckradius": "Eckradiusfräser",
    "konisch": "Konischer Kugelfräser",
    "gewinde": "Gewindefräser",
    "bohrer": "Bohrer",
}
SPITZENWINKEL = 118.0          # Standardbohrer (HSS); Holzbohrer mit Zentrierspitze ≈ 90–118°


def spitzenlaenge(w: Dict) -> float:
    """Länge des Bohrerkegels: so viel tiefer muss die Spitze, damit der volle
    Durchmesser eine Tiefe erreicht."""
    d = float(w.get("durchmesser", 3) or 3)
    winkel = max(30.0, min(179.0, float(w.get("spitzenwinkel") or SPITZENWINKEL)))
    return d / 2 / math.tan(math.radians(winkel / 2))

MATERIALIEN: Dict[str, Dict] = {
    # Mittelwerte aus Herstellertabellen (Peter, 22.10.: «Mittelwert aus zu langsam und zu schnell, von Hand
    # korrigiere ich») – Quellen und Rechnung in docs/SCHNITTDATEN.md. Je Werkstoff:
    #   vc m/min; Zahnvorschub fz = k·Ø für Fräser mit 2 und mehr Schneiden, k1·Ø für Einschneider
    #   (begrenzt fz_min … fz_max bzw. fz_max1); ab 3 Schneiden × 0,8; Zustellung × Ø; Bahnabstand %;
    #   Rampe °; Vorschub bei Vollnut (Anteil). «quelle»: False = keine Herstellerangabe gefunden.
    "weichholz": {"name": "Weichholz", "vc": 430, "k": 0.026, "k1": 0.048, "fz_min": 0.05, "fz_max": 0.23,
                  "fz_max1": 0.36, "zustellung": 0.8, "bahnabstand": 45, "rampe": 5.0, "vollnut": 0.6},
    "hartholz": {"name": "Hartholz", "vc": 405, "k": 0.0205, "k1": 0.05, "fz_min": 0.02, "fz_max": 0.18,
                 "fz_max1": 0.36, "zustellung": 0.8, "bahnabstand": 40, "rampe": 4.0, "vollnut": 0.5},
    "mdf": {"name": "MDF", "vc": 405, "k": 0.025, "k1": 0.052, "fz_min": 0.04, "fz_max": 0.20,
            "fz_max1": 0.36, "zustellung": 0.8, "bahnabstand": 45, "rampe": 5.0, "vollnut": 0.6},
    "acryl": {"name": "Acryl (PMMA)", "vc": 340, "k": 0.028, "fz_min": 0.045, "fz_max": 0.19,
              "zustellung": 0.6, "bahnabstand": 40, "rampe": 3.0, "vollnut": 0.78},
    "pvc": {"name": "PVC", "vc": 340, "k": 0.025, "fz_min": 0.045, "fz_max": 0.185,
            "zustellung": 0.6, "bahnabstand": 40, "rampe": 3.0, "vollnut": 0.78},
    # Aluminium: Einschneider Ø 3 → fz 0,06 (Peter, Schild in Alu: 19 000 U/min, 1200 mm/min lief optimal).
    # Rampe 3° (Fräsbahnen 25.10.; vorher 2°, Peter: «sehr vorsichtig»): VHM-Fräser 1–5° je Reihe, in Alu
    # üblich 3° – docs/SCHNITTDATEN.md «Eintauchen schräg»
    "aluminium": {"name": "Aluminium", "vc": 330, "k": 0.0105, "k1": 0.02, "fz_min": 0.02, "fz_max": 0.10,
                  "fz_max1": 0.15, "zustellung": 0.5, "bahnabstand": 30, "rampe": 3.0, "vollnut": 0.86},
    "messing": {"name": "Messing", "vc": 255, "k": 0.0105, "fz_min": 0.012, "fz_max": 0.08,
                "zustellung": 0.33, "bahnabstand": 25, "rampe": 2.0, "vollnut": 0.86},
    # Hartschaum (PU/Sign Foam, XPS): Späne fliegen weit und laden sich elektrisch auf – absaugen
    "hartschaum": {"name": "Hartschaum (Styrodur, XPS)", "vc": 270, "k": 0.028, "fz_min": 0.08,
                   "fz_max": 0.21, "zustellung": 1.0, "bahnabstand": 50, "rampe": 10.0, "vollnut": 0.8},
    # Zinn: keine Herstellertabelle gefunden – bisherige Schätzung (weich und schmierend: wenig Zustellung,
    # scharfe Schneide, eher hohe Drehzahl mit kleinem Zahnvorschub)
    "zinn": {"name": "Zinn", "vc": 200, "k": 0.004, "fz_min": 0.003, "fz_max": 0.035,
             "zustellung": 0.2, "bahnabstand": 30, "rampe": 2.0, "vollnut": 0.4, "quelle": False},
}
# Fräserart: Faktor auf den Zahnvorschub gegenüber dem Schaftfräser (Mittel aus Herstellertabellen,
# docs/SCHNITTDATEN.md); Planfräser: fz wächst nicht mit dem Ø – ¼ des Werts eines Ø-12,7-Fräsers
FZ_FAKTOR = {"v": 0.55, "konisch": 0.35, "kugel": 0.9, "eckradius": 1.0, "gewinde": 0.45, "bohrer": 1.0}
# Peter 02.11.2026: V-Fräser und konische Fräser rechnen mit dem Ø, der bei der Zustellung wirklich schneidet
# (Spitze + 2 · Tiefe · tan(Winkel/2)), nicht mit dem oberen Ø – dann gilt der Zahnvorschub des Schaftfräsers
# (Faktor 1; die Faktoren oben waren der Ersatz dafür). Bericht berichte/basis-schnittdaten-stichel-planfraeser.md
FZ_FAKTOR_WIRKSAM = {"v": 1.0, "konisch": 1.0}
# Gravierstichel (V-Fräser mit Spitze bis STICHEL_SPITZE mm): eigene Werte je Werkstoff, weil die Zahnvorschub-
# Rechnung bei 0,2–0,5 mm Schneide überall an der Untergrenze landet. Vorschub mm/min (1 Schneide, Spindel so schnell
# wie die Maschine erlaubt), Eintauchen als Anteil des Vorschubs, Zustellung mm je Durchgang. Mitte der
# Herstellerangaben (Applitec/Alfatool, Makera, MAPAL, Amana, Haas – docs/SCHNITTDATEN.md), Alu nahe an Peters
# Erfahrung (280 / 50).
STICHEL_SPITZE = 0.5
STICHEL = {
    "aluminium": {"vorschub": 250, "eintauchen": 0.2, "zustellung": 0.1},
    "messing": {"vorschub": 200, "eintauchen": 0.2, "zustellung": 0.1},
    "zinn": {"vorschub": 150, "eintauchen": 0.2, "zustellung": 0.1},
    "hartholz": {"vorschub": 1200, "eintauchen": 0.4, "zustellung": 0.8},
    "weichholz": {"vorschub": 1400, "eintauchen": 0.45, "zustellung": 1.0},
    "mdf": {"vorschub": 1400, "eintauchen": 0.45, "zustellung": 1.0},
    "acryl": {"vorschub": 1000, "eintauchen": 0.4, "zustellung": 0.5},
    "pvc": {"vorschub": 1000, "eintauchen": 0.4, "zustellung": 0.5},
    "hartschaum": {"vorschub": 1500, "eintauchen": 0.5, "zustellung": 1.0},
}
# Planfräser (Wendeplatten, grosser Ø): Zahnvorschub fz = k · Ø bis Ø 25, Schnittgeschwindigkeit vc – Sorotec
# «Fräsparameter Planfräser» (Ø 20: Hartholz fz 0,16, Alu 0,108 …); Zustellung beim Planen höchstens so viel
PLAN = {
    "hartholz": {"vc": 250, "k": 0.008, "zustellung": 1.5},
    "weichholz": {"vc": 200, "k": 0.009, "zustellung": 2.0},
    "mdf": {"vc": 250, "k": 0.009, "zustellung": 1.5},
    "acryl": {"vc": 250, "k": 0.011, "zustellung": 1.0},
    "pvc": {"vc": 250, "k": 0.011, "zustellung": 1.0},
    "aluminium": {"vc": 350, "k": 0.0054, "zustellung": 0.5},
    "messing": {"vc": 300, "k": 0.0033, "zustellung": 0.3},
    "hartschaum": {"vc": 250, "k": 0.012, "zustellung": 3.0},
    "zinn": {"vc": 200, "k": 0.002, "zustellung": 0.2},
}


def ist_stichel(w: Dict) -> bool:
    """Gravierstichel: V-Fräser mit kleiner abgeflachter Spitze (0 < Spitze ≤ 0,5 mm)."""
    try:
        sp = float(w.get("spitze") or 0)
    except (TypeError, ValueError):
        sp = 0.0
    return w.get("typ") == "v" and 0 < sp <= STICHEL_SPITZE


def schneide_d(w: Dict, tiefe: float) -> float:
    """Ø, der bei dieser Tiefe schneidet: V-Fräser Spitze + 2·Tiefe·tan(Winkel/2), konisch Kugel-Ø + Kegel,
    sonst der Ø der Schneide. Nie grösser als der obere Ø."""
    d = max(0.1, float(w.get("durchmesser", 3) or 3))
    typ = w.get("typ", "schaft")
    if typ not in ("v", "konisch"):
        return d
    halb = math.radians(max(1.0, min(179.0, float(w.get("v_winkel") or (60 if typ == "v" else 10)))) / 2)
    unten = float(w.get("spitze") or 0) if typ == "v" else 2 * float(w.get("eckradius") or 0)
    return max(0.1, min(d, unten + 2 * max(0.0, tiefe) * math.tan(halb)))
BOHRER_VC = 0.4          # Bohrer: Schnittgeschwindigkeit gegenüber dem Fräser (Mittel HM 0,6 / HSS 0,25)
STANDARD_MATERIAL = "hartholz"

STANDARD_MASCHINE = {"x": 600.0, "y": 900.0, "z": 150.0,
                     "drehzahl_min": 6000, "drehzahl_max": 24000, "vorschub_max": 5000,
                     # Spindel und Halterung (Kollisionsprüfung beim 3D-Fräsen, mm; 0 = nicht
                     # eingetragen): Spannmutter Höhe unter der Spindel und grösster Ø, Spindel Ø
                     # und Länge bis zur Halterung, Halterung als Quader Breite (X) × Tiefe (Y)
                     "mutter_hoehe": 0.0, "mutter_d": 0.0, "spindel_d": 0.0, "spindel_laenge": 0.0,
                     "halter_b": 0.0, "halter_t": 0.0,
                     # Steuerung (Postprozessor) für neue Zeichnungen; leer = Vorgabe des Programms
                     "postprozessor": "",
                     # Werkzeugwechsel: "" = wie die Steuerung, "dateien" = eine Datei je Werkzeug,
                     # "halt" = alle in einer Datei mit Halt (T…M6)
                     "werkzeugwechsel": "",
                     # Speichern: Knopf «USB-Stick» zeigen, Prüfliste vor dem Speichern,
                     # Luftfräsen so viele mm über dem Werkstück
                     "usb": 1.0, "pruefliste": 1.0, "luft": 10.0,
                     # alte Masskorrektur je Seite für alle Fräser in mm (Teile zu gross → negativ, wie ein
                     # Aufmass); seit 21.10. misst die Messkorrektur je Fräser («messkorrektur» beim Werkzeug)
                     "masskorrektur": 0.0,
                     # Weg der Achsen aus der Messkorrektur (gemessen / soll); im G-Code ausgleichen nur,
                     # wenn «skalierung_an» (ab Werk aus – besser in der Steuerung einstellen)
                     "skalierung_an": 0.0, "skalierung_x": 1.0, "skalierung_y": 1.0,
                     # Ordner, in den «G-Code speichern» zuerst schaut (PC an der Fräse, Netzlaufwerk); leer = zuletzt benutzt
                     "zielordner": "",
                     # Werkstoffe an dieser Maschine (Peter 06.11.: Schieber «langsamer – schneller»):
                     # {werkstoff: {"vorschub": %, "drehzahl": %}}, 100 = Herstellerwert; fehlt = 100
                     "werkstoffe": {}}


def _w(id_, name, typ, d, schneiden=2, sl=None, schaft=None, mitte=True, **mehr):
    w = {"id": id_, "name": name, "typ": typ, "durchmesser": float(d), "schneiden": schneiden,
         "schneidenlaenge": float(sl if sl is not None else max(3.0, 3 * d)),
         "schaft": float(schaft if schaft is not None else (3 if d <= 3 else 6 if d <= 6 else 8)),
         "mittenschnitt": mitte, "v_winkel": 60.0, "spitze": 0.0, "eckradius": 0.0, "notiz": ""}
    w.update(mehr)
    return w


# Beispielwerkzeuge (Spannzangen 3, 6, 8 mm) – zum Anpassen gedacht
BEISPIELE: List[Dict] = [
    _w("s1", "Schaftfräser 1 mm", "schaft", 1, sl=3, schaft=3),
    _w("s2", "Schaftfräser 2 mm", "schaft", 2, sl=6, schaft=3),
    _w("s3", "Schaftfräser 3 mm", "schaft", 3, sl=12, schaft=3),
    _w("e3", "Einschneider 3 mm (Acryl, Alu)", "schaft", 3, schneiden=1, sl=12, schaft=3),
    _w("s4", "Schaftfräser 4 mm", "schaft", 4, sl=15, schaft=6),
    _w("s6", "Schaftfräser 6 mm", "schaft", 6, sl=22, schaft=6),
    _w("s8", "Schaftfräser 8 mm", "schaft", 8, schneiden=3, sl=25, schaft=8),
    _w("p22", "Planfräser 22 mm", "schaft", 22, sl=6, schaft=8, mitte=False, wendeplatten=True,
       notiz="Kein Mittenschnitt: nur mit Rampe oder Helix eintauchen"),
    _w("v60", "V-Fräser 60°", "v", 12, sl=11, schaft=6, v_winkel=60.0),
    _w("v90", "V-Fräser 90°", "v", 12, sl=7, schaft=6, v_winkel=90.0),
    _w("g36", "Gravierstichel 36° / 0,2 mm", "v", 3, schneiden=1, sl=4, schaft=3,
       v_winkel=36.0, spitze=0.2),
    _w("k3", "Kugelfräser 3 mm (R1,5)", "kugel", 3, sl=10, schaft=3, eckradius=1.5),
    _w("k6", "Kugelfräser 6 mm (R3)", "kugel", 6, sl=20, schaft=6, eckradius=3.0),
    _w("r6", "Eckradiusfräser 6 mm (R1)", "eckradius", 6, sl=20, schaft=6, eckradius=1.0),
    # konischer Kugelfräser: Ø = grösster Ø der Schneide (R0,25 + 15 mm Kegel mit 3,6°)
    _w("kk05", "Konischer Kugelfräser R0,25 · 3,6°", "konisch", 1.45, sl=15, schaft=3.175,
       eckradius=0.25, v_winkel=3.6),
    _w("kk1", "Konischer Kugelfräser R0,5 · 3,6°", "konisch", 1.95, sl=15, schaft=3.175,
       eckradius=0.5, v_winkel=3.6, notiz="Für feines 3D-Schlichten: kleine Spitze, steifer Kegel"),
    _w("b3", "Bohrer 3 mm (HSS, 118°)", "bohrer", 3, sl=33, schaft=3, spitzenwinkel=118.0),
    _w("b5", "Bohrer 5 mm (HSS, 118°)", "bohrer", 5, sl=52, schaft=5, spitzenwinkel=118.0),
    _w("b6", "Bohrer 6 mm (HSS, 118°)", "bohrer", 6, sl=57, schaft=6, spitzenwinkel=118.0),
    # seit Datenbank-Version 3: gängige Fräser, auch mit kleinerer Schneide als Schaft
    _w("s2l", "Schaftfräser 2 mm (Schaft 6)", "schaft", 2, sl=8, schaft=6),
    _w("s3l", "Schaftfräser 3 mm, lang (Schaft 6)", "schaft", 3, sl=12, schaft=6,
       notiz="Schaft dicker als die Schneide: tiefer als die Schneidenlänge streift der Schaft die Wand"),
    _w("d3", "Abwärtsspirale 3 mm (Downcut)", "schaft", 3, sl=12, schaft=3, drall="ab",
       notiz="Drückt die Späne nach unten: saubere Oberkante in Sperrholz und Furnier; Späne bleiben in der Nut"),
    _w("d6", "Abwärtsspirale 6 mm (Downcut)", "schaft", 6, sl=22, schaft=6, drall="ab",
       notiz="Drückt die Späne nach unten: saubere Oberkante in Sperrholz und Furnier; Späne bleiben in der Nut"),
    _w("c6", "Kompressionsfräser 6 mm", "schaft", 6, sl=22, schaft=6, drall="kompression",
       notiz="Unten aufwärts, oben abwärts: beide Kanten sauber beim Durchtrennen von Platten; "
             "erste Zustellung mindestens so tief wie der abwärts schneidende Teil"),
    _w("r6s", "Schruppfräser 6 mm", "schaft", 6, schneiden=3, sl=22, schaft=6, spanbrecher=True,
       notiz="Schruppverzahnung: viel Material schnell weg, raue Oberfläche – danach schlichten"),
    _w("e6", "Einschneider 6 mm (Acryl, Alu)", "schaft", 6, schneiden=1, sl=22, schaft=6),
    _w("k1", "Kugelfräser 1 mm (R0,5)", "kugel", 1, sl=3, schaft=3, eckradius=0.5),
    _w("k2", "Kugelfräser 2 mm (R1)", "kugel", 2, sl=6, schaft=3, eckradius=1.0),
    _w("k3l", "Kugelfräser 3 mm, lang (R1,5, Schaft 6)", "kugel", 3, sl=12, schaft=6, eckradius=1.5),
    _w("v30", "V-Fräser 30°", "v", 6, sl=11, schaft=6, v_winkel=30.0),
    _w("g20", "Gravierstichel 20° / 0,1 mm", "v", 3, schneiden=1, sl=4, schaft=3, v_winkel=20.0, spitze=0.1),
    _w("p32", "Planfräser 32 mm", "schaft", 32, sl=6, schaft=8, mitte=False, wendeplatten=True,
       notiz="Kein Mittenschnitt: nur mit Rampe oder Helix eintauchen"),
    _w("b25", "Bohrer 2,5 mm (HSS, 118°)", "bohrer", 2.5, sl=30, schaft=2.5, spitzenwinkel=118.0,
       notiz="Kernloch M3"),
    _w("b4", "Bohrer 4 mm (HSS, 118°)", "bohrer", 4, sl=43, schaft=4, spitzenwinkel=118.0),
    _w("b8", "Bohrer 8 mm (HSS, 118°)", "bohrer", 8, sl=75, schaft=8, spitzenwinkel=118.0),
]
NEU_SEIT_V3 = ("s2l", "s3l", "d3", "d6", "c6", "r6s", "e6", "k1", "k2", "k3l", "v30", "g20", "p32",
               "b25", "b4", "b8", "kk05", "kk1")
DB_VERSION = 4


def pfad() -> str:
    """Datei der geltenden Maschine (maschinen.py: Maschine der Zeichnung, sonst die zuletzt benutzte)."""
    from . import maschinen
    return maschinen.pfad_aktiv()


def _abgleich():
    """Mit dem Ordner abgleichen, falls eingeschaltet (werkzeugabgleich.py) – nie den Start verhindern."""
    try:
        from . import werkzeugabgleich
        werkzeugabgleich.abgleichen()
    except Exception:
        pass


# Startliste in Zoll (Peter 08.11.: Hirch CAM auch für die USA). Masse in mm (intern immer mm), Namen wie im Handel.
Z = 25.4
_AB = ("Drückt die Späne nach unten: saubere Oberkante in Sperrholz und Furnier; Späne bleiben in der Nut")
_KOMP = ("Unten aufwärts, oben abwärts: beide Kanten sauber beim Durchtrennen von Platten; "
         "erste Zustellung mindestens so tief wie der abwärts schneidende Teil")
_PLAN = "Kein Mittenschnitt: nur mit Rampe oder Helix eintauchen"


def _v_hoehe(d, winkel):
    """Schneidenlänge eines V-Fräsers: Höhe des Kegels bis zum vollen Ø."""
    return d / 2 / math.tan(math.radians(winkel / 2))


# Peter 10.11.: ebenbürtig der metrischen Liste (Hirch CAM wird auch in den USA beworben). Gängige Grössen im
# US-Handel, Schäfte 1/8" und 1/4" (Planfräser und 1/2"-Fräser 1/2"), Schneidenlängen nach typischen
# Herstellerangaben. Alle Masse genau aus Zoll gerechnet; Namen deutsch, englisch über en.json wie im Handel.
BEISPIELE_ZOLL: List[Dict] = [
    _w("zs132", 'Schaftfräser 1/32"', "schaft", Z / 32, sl=Z / 8, schaft=Z / 8),
    _w("zs116", 'Schaftfräser 1/16"', "schaft", Z / 16, sl=Z / 4, schaft=Z / 8),
    _w("zs18", 'Schaftfräser 1/8"', "schaft", Z / 8, sl=Z / 2, schaft=Z / 8),
    _w("zs14", 'Schaftfräser 1/4"', "schaft", Z / 4, sl=Z * 3 / 4, schaft=Z / 4),
    _w("zs12", 'Schaftfräser 1/2"', "schaft", Z / 2, sl=Z, schaft=Z / 2),
    _w("zd18", 'Abwärtsspirale 1/8" (Downcut)', "schaft", Z / 8, sl=Z / 2, schaft=Z / 8, drall="ab", notiz=_AB),
    _w("zd14", 'Abwärtsspirale 1/4" (Downcut)', "schaft", Z / 4, sl=Z * 3 / 4, schaft=Z / 4, drall="ab", notiz=_AB),
    _w("zc14", 'Kompressionsfräser 1/4"', "schaft", Z / 4, sl=Z * 7 / 8, schaft=Z / 4, drall="kompression",
       notiz=_KOMP),
    _w("ze18", 'Einschneider 1/8" (Acryl, Alu)', "schaft", Z / 8, schneiden=1, sl=Z / 2, schaft=Z / 8),
    _w("ze14", 'Einschneider 1/4" (Acryl, Alu)', "schaft", Z / 4, schneiden=1, sl=Z * 3 / 4, schaft=Z / 4),
    _w("zr14s", 'Schruppfräser 1/4"', "schaft", Z / 4, schneiden=3, sl=Z * 3 / 4, schaft=Z / 4, spanbrecher=True,
       notiz="Schruppverzahnung: viel Material schnell weg, raue Oberfläche – danach schlichten"),
    _w("zk116", 'Kugelfräser 1/16"', "kugel", Z / 16, sl=Z / 4, schaft=Z / 8, eckradius=Z / 32),
    _w("zk18", 'Kugelfräser 1/8"', "kugel", Z / 8, sl=Z / 2, schaft=Z / 8, eckradius=Z / 16),
    _w("zk14", 'Kugelfräser 1/4"', "kugel", Z / 4, sl=Z * 3 / 4, schaft=Z / 4, eckradius=Z / 8),
    _w("zr14", 'Eckradiusfräser 1/4" (R 1/32")', "eckradius", Z / 4, sl=Z * 3 / 4, schaft=Z / 4, eckradius=Z / 32),
    # konischer Kugelfräser: Spitze 1/32" (R 1/64"), Kegel 3,6° über 1" Schneide, wie kk05 der metrischen Liste
    _w("zkk", 'Konischer Kugelfräser 1/32" Spitze · 3,6°', "konisch", 2 * (Z / 64 + Z * math.tan(math.radians(1.8))),
       sl=Z, schaft=Z / 4, eckradius=Z / 64, v_winkel=3.6,
       notiz="Für feines 3D-Schlichten: kleine Spitze, steifer Kegel"),
    _w("zv30", 'V-Fräser 30° 1/4"', "v", Z / 4, sl=_v_hoehe(Z / 4, 30), schaft=Z / 4, v_winkel=30.0),
    _w("zv60k", 'V-Fräser 60° 1/4"', "v", Z / 4, sl=_v_hoehe(Z / 4, 60), schaft=Z / 4, v_winkel=60.0),
    _w("zv90k", 'V-Fräser 90° 1/4"', "v", Z / 4, sl=_v_hoehe(Z / 4, 90), schaft=Z / 4, v_winkel=90.0),
    _w("zv60", 'V-Fräser 60° 1/2"', "v", Z / 2, sl=_v_hoehe(Z / 2, 60), schaft=Z / 4, v_winkel=60.0),
    _w("zv90", 'V-Fräser 90° 1/2"', "v", Z / 2, sl=_v_hoehe(Z / 2, 90), schaft=Z / 4, v_winkel=90.0),
    _w("zg30", 'Gravierstichel 30° / 0,010"', "v", Z / 8, schneiden=1, sl=Z / 8, schaft=Z / 8, v_winkel=30.0,
       spitze=Z * 0.010),
    _w("zg20", 'Gravierstichel 20° / 0,005"', "v", Z / 8, schneiden=1, sl=Z / 8, schaft=Z / 8, v_winkel=20.0,
       spitze=Z * 0.005),
    _w("zp1", 'Planfräser 1"', "schaft", Z, sl=Z / 4, schaft=Z / 4, mitte=False, wendeplatten=True, notiz=_PLAN),
    _w("zp112", 'Planfräser 1-1/2"', "schaft", Z * 3 / 2, sl=Z / 4, schaft=Z / 2, mitte=False, wendeplatten=True,
       notiz=_PLAN),
    _w("zb18", 'Bohrer 1/8" (HSS, 118°)', "bohrer", Z / 8, sl=Z * 3 / 2, schaft=Z / 8, spitzenwinkel=118.0),
    _w("zb316", 'Bohrer 3/16" (HSS, 118°)', "bohrer", Z * 3 / 16, sl=Z * 2, schaft=Z * 3 / 16, spitzenwinkel=118.0),
    _w("zb14", 'Bohrer 1/4" (HSS, 118°)', "bohrer", Z / 4, sl=Z * 5 / 2, schaft=Z / 4, spitzenwinkel=118.0),
    _w("zb516", 'Bohrer 5/16" (HSS, 118°)', "bohrer", Z * 5 / 16, sl=Z * 3, schaft=Z * 5 / 16, spitzenwinkel=118.0),
]


def startliste(art: str, datei: Optional[str] = None) -> Dict:
    """Ersteinrichtung: Fräser der Maschine durch die Startliste ersetzen («mm» oder «zoll»)."""
    db = laden(datei)
    db["werkzeuge"] = copy.deepcopy(BEISPIELE_ZOLL if art == "zoll" else BEISPIELE)
    schreiben(db, datei)
    return db


def laden(datei: Optional[str] = None) -> Dict:
    if datei is None:
        _abgleich()
    datei = datei or pfad()
    db = {"version": DB_VERSION, "werkzeuge": copy.deepcopy(BEISPIELE), "maschine": dict(STANDARD_MASCHINE)}
    try:
        with open(datei, encoding="utf-8") as fh:
            roh = json.load(fh)
        if isinstance(roh.get("werkzeuge"), list):
            db["werkzeuge"] = [w for w in roh["werkzeuge"] if isinstance(w, dict) and w.get("id")]
        if isinstance(roh.get("maschine"), dict):
            db["maschine"].update(roh["maschine"])
        ver = int(roh.get("version") or 1)
        if ver < 2:
            # neu mitgeliefert seit Version 2: Bohrer (nur ergänzen, was fehlt)
            ids = {w["id"] for w in db["werkzeuge"]}
            db["werkzeuge"] += [copy.deepcopy(w) for w in BEISPIELE if w["typ"] == "bohrer" and w["id"] not in ids
                                and w["id"] not in NEU_SEIT_V3]
        if ver < 3:
            # seit Version 3: weitere gängige Fräser (nur ergänzen, was fehlt; eigene bleiben)
            ids = {w["id"] for w in db["werkzeuge"]}
            db["werkzeuge"] += [copy.deepcopy(w) for w in BEISPIELE if w["id"] in NEU_SEIT_V3 and w["id"] not in ids]
        if isinstance(roh.get("ersetzt"), dict):
            db["ersetzt"] = {str(k): str(v) for k, v in roh["ersetzt"].items()}
        # Abgleich zweier PCs (27.10.): Zeit des Löschens je Werkzeug, Zeit der Maschinenwerte
        if isinstance(roh.get("geloescht"), dict):
            db["geloescht"] = dict(roh["geloescht"])
        if roh.get("maschine_geaendert"):
            db["maschine_geaendert"] = roh["maschine_geaendert"]
        if ver < 4:
            # seit Version 4: gleiche Fräser (früher mehrmals gemerkt) nur noch einmal
            _doppelte_weg(db)
        db["version"] = DB_VERSION
    except (OSError, ValueError):
        pass
    return db


def _vergleich(w: Dict) -> Dict:
    """Inhalt eines Werkzeugs ohne ID und Verwaltungsfelder (zum Vergleichen)."""
    return {k: v for k, v in w.items()
            if k not in ("id", "db_id", "nummer", "auto", "nur_hier", "empfehlung", "geaendert")}


def _doppelte_weg(db: Dict):
    erst: Dict[str, str] = {}
    bleibt = []
    ersetzt = db.setdefault("ersetzt", {})
    for w in db["werkzeuge"]:
        k = json.dumps(_vergleich(w), sort_keys=True, ensure_ascii=False)
        if k in erst:
            ersetzt[w["id"]] = erst[k]
            continue
        erst[k] = w["id"]
        bleibt.append(w)
    db["werkzeuge"] = bleibt
    if not ersetzt:
        db.pop("ersetzt")


def schreiben(db: Dict, datei: Optional[str] = None):
    # Ist werkzeuge.json ein Link (z. B. in einen Nextcloud-Ordner, Peter 20.10.), wird die Zieldatei
    # geschrieben – os.replace auf den Link selbst würde ihn durch eine gewöhnliche Datei ersetzen.
    eigene_datei = datei is None
    datei = os.path.realpath(datei or pfad())
    os.makedirs(os.path.dirname(datei), exist_ok=True)
    tmp = datei + ".neu"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(db, fh, ensure_ascii=False, indent=1)
    os.replace(tmp, datei)
    if eigene_datei:
        _abgleich()


def speichern(werkzeug: Dict, datei: Optional[str] = None) -> Dict:
    """Werkzeug in die Datenbank (gleiche ID ersetzt, sonst neu)."""
    db = laden(datei)
    w = _fuer_db(werkzeug, db)
    _einsetzen(db, w)
    schreiben(db, datei)
    return w


def _fuer_db(werkzeug: Dict, db: Dict) -> Dict:
    """Werkzeug so, wie es in «Meine Werkzeuge» steht: Schnittdaten nach Modus
    (automatisch: keine; Drehzahl von Hand: die Drehzahl; alles von Hand: alle drei)."""
    modus = schnittmodus(werkzeug) if ("schnittdaten" in werkzeug or "auto" in werkzeug) else "auto"
    weg = {"nummer", "auto", "nur_hier", "empfehlung", "db_id", "rampe", "vollnut", "geaendert"}
    if modus != "hand":
        weg |= {"vorschub", "eintauchen"}
    if modus == "auto":
        weg.add("drehzahl")
    w = {k: v for k, v in werkzeug.items() if k not in weg}
    w["schnittdaten"] = modus
    je = {}
    for mat, e in (werkzeug.get("je_werkstoff") or {}).items():
        if isinstance(e, dict):
            m = e.get("schnittdaten") if e.get("schnittdaten") in SCHNITTMODI else "auto"
            felder = {"auto": (), "drehzahl": ("drehzahl",), "hand": ("drehzahl", "vorschub", "eintauchen")}[m]
            je[str(mat)] = dict({"schnittdaten": m}, **{f: e[f] for f in felder if f in e})
    if je:
        w["je_werkstoff"] = je
    else:
        w.pop("je_werkstoff", None)
    if not w.get("id"):
        w["id"] = werkzeug.get("db_id") or neue_id(db)
    return w


def neue_id(db: Dict) -> str:
    vorhanden = {x["id"] for x in db["werkzeuge"]} | set(db.get("ersetzt") or {})
    n = 1
    while "w%d" % n in vorhanden:
        n += 1
    try:
        from . import werkzeugabgleich
        if werkzeugabgleich.aktiv():
            # zwei PCs legen unabhängig neue Werkzeuge an: die ID darf sich nicht zufällig gleichen
            import secrets
            return "w%d_%s" % (n, secrets.token_hex(3))
    except Exception:
        pass
    return "w%d" % n


def _einsetzen(db: Dict, w: Dict):
    alt = [i for i, x in enumerate(db["werkzeuge"]) if x["id"] == w["id"]]
    # Abgleich zweier PCs: je Werkzeug gilt die neuere Änderung – die Zeit nur setzen, wenn sich wirklich
    # etwas geändert hat (der Werkzeugdialog übergibt beim «Übernehmen» auch Unveränderte)
    if alt and _vergleich(db["werkzeuge"][alt[0]]) == _vergleich(w):
        w["geaendert"] = db["werkzeuge"][alt[0]].get("geaendert", 0)
    else:
        w["geaendert"] = round(time.time(), 3)
    if not w["geaendert"]:
        w.pop("geaendert")
    (db.get("geloescht") or {}).pop(w["id"], None)
    if alt:
        db["werkzeuge"][alt[0]] = w                 # Reihenfolge bleibt
    else:
        db["werkzeuge"].append(w)


def loeschen(wid: str, datei: Optional[str] = None):
    db = laden(datei)
    db["werkzeuge"] = [x for x in db["werkzeuge"] if x["id"] != wid]
    db.setdefault("geloescht", {})[wid] = round(time.time(), 3)
    schreiben(db, datei)


def uebernehmen(aenderung: Dict, datei: Optional[str] = None) -> Dict:
    """Änderungen aus dem Werkzeugdialog in einem Schritt: {"speichern": [Werkzeuge], "loeschen": [IDs]}."""
    db = laden(datei)
    gespeichert = []
    for w in aenderung.get("speichern") or []:
        if isinstance(w, dict):
            w = _fuer_db(w, db)
            _einsetzen(db, w)
            gespeichert.append(w)
    weg = {str(i) for i in aenderung.get("loeschen") or []}
    db["werkzeuge"] = [x for x in db["werkzeuge"] if x["id"] not in weg]
    for i in weg:
        db.setdefault("geloescht", {})[i] = round(time.time(), 3)
    schreiben(db, datei)
    return {"gespeichert": gespeichert}


# ---------------------------------------------------------------- Werkzeuge einer Zeichnung abgleichen

# was zum Fräser gehört (verglichen und gemeldet); «nummer» gehört zur Zeichnung
_GEOMETRIE = ("typ", "durchmesser", "schneidenlaenge", "schneiden", "v_winkel", "spitze", "eckradius",
              "spitzenwinkel", "zaehne", "steigung", "mittenschnitt", "uebergang", "drall", "wendeplatten",
              "spanbrecher")
_FELDNAMEN = {"name": "Name", "typ": "Art", "durchmesser": "Durchmesser", "schneidenlaenge": "Schneidenlänge",
              "schneiden": "Schneiden", "v_winkel": "Winkel", "spitze": "Spitze", "eckradius": "Eckradius",
              "spitzenwinkel": "Spitzenwinkel", "zaehne": "Gewindezähne", "steigung": "Zahnabstand",
              "mittenschnitt": "Mittenschnitt", "uebergang": "Übergang zum Schaft",
              "drall": "Drall", "wendeplatten": "mit Wendeplatten", "spanbrecher": "Spanbrecher", "schaft": "Schaft-Ø", "schnittdaten": "Schnittdaten",
              "drehzahl": "Drehzahl", "vorschub": "Vorschub", "eintauchen": "Eintauchen"}
_MODUSNAMEN = {"auto": "automatisch", "drehzahl": "Drehzahl von Hand", "hand": "von Hand"}


def _gleich(a, b) -> bool:
    if isinstance(a, bool) or isinstance(b, bool):
        return bool(a) == bool(b)
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return abs(float(a) - float(b)) < 1e-6
    return a == b


def _geometrie_gleich(w: Dict, d: Dict) -> bool:
    typ = w.get("typ", "schaft")
    if typ != d.get("typ", "schaft"):
        return False
    for f in _GEOMETRIE[1:]:
        if f == "v_winkel" and typ != "v":
            continue
        if f in ("zaehne", "steigung") and typ != "gewinde":
            continue
        if f == "spitzenwinkel" and typ != "bohrer":
            continue
        if f in w and f in d and not _gleich(w[f], d[f]):
            return False
    if "schaft" in w and "schaft" in d and typ != "v" and not _gleich(w["schaft"], d["schaft"]):
        return False
    return True


def _db_modus(d: Dict) -> str:
    return d.get("schnittdaten") if d.get("schnittdaten") in SCHNITTMODI else "auto"


def _schnitt_gleich(w: Dict, d: Dict) -> bool:
    if (w.get("je_werkstoff") or {}) != (d.get("je_werkstoff") or {}):
        return False
    m = schnittmodus(w)
    if m != _db_modus(d):
        return False
    felder = {"auto": (), "drehzahl": ("drehzahl",), "hand": ("drehzahl", "vorschub", "eintauchen")}[m]
    return all(_gleich(w.get(f), d.get(f)) for f in felder)


def _als_platz(d: Dict, alt: Dict) -> Dict:
    """Fräser aus «Meine Werkzeuge» als Werkzeug der Zeichnung (Nummer bleibt die der Zeichnung)."""
    w = {k: v for k, v in alt.items() if k in ("nummer",)}
    for k, v in d.items():
        if k not in ("id", "notiz"):
            w[k] = copy.deepcopy(v)
    w["db_id"] = d["id"]
    modus = _db_modus(d)
    w["schnittdaten"] = modus
    w["auto"] = modus == "auto"
    for f in ("drehzahl", "vorschub", "eintauchen"):
        if f not in w and f in alt:
            w[f] = alt[f]                  # Anzeigewerte; bei «auto» rechnet wirksam() sie neu
    # Ausspannlänge (3D-Kollisionsprüfung): ältere Zeichnungen haben sie nur am Platz – behalten,
    # solange «Meine Werkzeuge» keine eigene kennt
    if not float(w.get("ausspannlaenge") or 0) and float(alt.get("ausspannlaenge") or 0):
        w["ausspannlaenge"] = alt["ausspannlaenge"]
    if not float(w.get("gesamtlaenge") or 0) and float(alt.get("gesamtlaenge") or 0):
        w["gesamtlaenge"] = alt["gesamtlaenge"]
    w.setdefault("nummer", 1)
    return w


def _wert_text(f, v) -> str:
    if f == "schnittdaten":
        return _MODUSNAMEN.get(v, str(v))
    if f == "drall":
        return {"auf": "aufwärts", "ab": "abwärts", "kompression": "Kompression"}.get(v, str(v))
    if isinstance(v, bool):
        return "ja" if v else "nein"
    if isinstance(v, (int, float)):
        t = ("%.2f" % float(v)).rstrip("0").rstrip(".").replace(".", ",")
        return t + {"durchmesser": " mm", "schneidenlaenge": " mm", "spitze": " mm", "eckradius": " mm",
                    "steigung": " mm", "schaft": " mm", "v_winkel": "°", "spitzenwinkel": "°",
                    "drehzahl": " U/min", "vorschub": " mm/min", "eintauchen": " mm/min"}.get(f, "")
    return str(v)


def _unterschiede(alt: Dict, neu: Dict) -> List[str]:
    aus = []
    modus = neu.get("schnittdaten", "auto")
    for f in ("name",) + _GEOMETRIE + ("schaft", "schnittdaten", "drehzahl", "vorschub", "eintauchen"):
        if f in ("drehzahl",) and modus == "auto":
            continue
        if f in ("vorschub", "eintauchen") and modus != "hand":
            continue
        if f == "schnittdaten" and schnittmodus(alt) == modus:
            continue
        if f not in neu or (f not in alt and f != "schnittdaten"):
            continue
        a = schnittmodus(alt) if f == "schnittdaten" else alt.get(f)
        if not _gleich(a, neu[f]):
            aus.append("%s %s → %s" % (_FELDNAMEN.get(f, f), _wert_text(f, a), _wert_text(f, neu[f])))
    ja, jn = alt.get("je_werkstoff") or {}, neu.get("je_werkstoff") or {}
    for mat in sorted(set(ja) | set(jn)):
        if ja.get(mat) != jn.get(mat):
            name_m = (materialien().get(mat) or {}).get("name", mat)
            aus.append("Schnittdaten für %s %s" % (name_m, "geändert" if mat in jn else "zurückgesetzt"))
    return aus


def abgleichen(werkzeuge: Dict[str, Dict], db: Optional[Dict] = None) -> Tuple[Dict[str, Dict], List[str]]:
    """Werkzeuge einer Zeichnung mit «Meine Werkzeuge» abgleichen.
    - mit db_id: die Werte aus «Meine Werkzeuge» gelten (Hinweis, was sich geändert hat);
      ist der Fräser dort gelöscht, behält die Zeichnung ihn mit nur_hier = True;
    - ohne db_id: genau gleicher Fräser in «Meine Werkzeuge» → verknüpft (ohne Hinweis);
      Vorgabe-Werkzeuge einer neuen Zeichnung → der passendste Fräser gleicher Art;
      sonst nur_hier = True (nur in dieser Zeichnung)."""
    from .einstellungen import STANDARD_WERKZEUGE
    db = db if db is not None else laden()
    nach_id = {d["id"]: d for d in db["werkzeuge"]}
    ersetzt = db.get("ersetzt") or {}
    aus: Dict[str, Dict] = {}
    hinweise: List[str] = []
    for k, w in werkzeuge.items():
        did = w.get("db_id")
        if did:
            n = 0
            while did in ersetzt and did not in nach_id and n < 20:
                did, n = ersetzt[did], n + 1
            d = nach_id.get(did)
            if d is None:
                aus[k] = dict(w, nur_hier=True)
                continue
            neu = _als_platz(d, w)
            unt = _unterschiede(w, neu)
            if unt:
                hinweise.append("Werkzeug «%s» (Platz %s) aus «Meine Werkzeuge» übernommen: %s"
                                % (name(neu), k, ", ".join(unt)))
            aus[k] = neu
            continue
        d = next((d for d in db["werkzeuge"] if _geometrie_gleich(w, d) and _schnitt_gleich(w, d)), None)
        if d is None and _vergleich(w) == _vergleich(STANDARD_WERKZEUGE.get(k, {})):
            # Vorgabe einer neuen Zeichnung: ein vorhandener Fräser gleicher Art, Ø am nächsten
            art = [d for d in db["werkzeuge"] if d.get("typ", "schaft") == w.get("typ")]
            if art:
                ziel = float(w.get("durchmesser", 3))
                d = min(art, key=lambda x: abs(float(x.get("durchmesser", 3)) - ziel))
        if d is not None:
            aus[k] = _als_platz(d, w)
        else:
            w = dict(w)
            w["nur_hier"] = True
            aus[k] = w
    return aus, hinweise


# ---------------------------------------------------------------- Übertragen (Export/Import)

DATEI_ART = "hirch-werkzeuge"


def _post_ordner() -> str:
    from .post import benutzer_ordner
    return benutzer_ordner()


def _eigene_post() -> Dict[str, Dict]:
    aus = {}
    o = _post_ordner()
    if os.path.isdir(o):
        for f in sorted(os.listdir(o)):
            if f.endswith(".json"):
                try:
                    with open(os.path.join(o, f), encoding="utf-8") as fh:
                        d = json.load(fh)
                except (OSError, ValueError):
                    continue
                if isinstance(d, dict) and not d.get("entfernt"):
                    aus[f[:-5]] = d
    return aus


def exportieren(ziel: str, datei: Optional[str] = None) -> Dict:
    """Fräser, Maschinenwerte und eigene Postprozessoren in eine Datei."""
    import datetime
    db = laden(datei)
    inhalt = {"art": DATEI_ART, "version": 1, "datum": datetime.date.today().isoformat(),
              "werkzeuge": db["werkzeuge"], "maschine": db["maschine"], "postprozessoren": _eigene_post()}
    tmp = ziel + ".neu"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(inhalt, fh, ensure_ascii=False, indent=1)
    os.replace(tmp, ziel)
    return {"datei": ziel, "werkzeuge": len(inhalt["werkzeuge"]),
            "postprozessoren": len(inhalt["postprozessoren"])}


def _import_lesen(quelle: str) -> Dict:
    with open(quelle, encoding="utf-8") as fh:
        roh = json.load(fh)
    if not isinstance(roh, dict) or roh.get("art") != DATEI_ART:
        raise ValueError("keine Werkzeugdatei von Hirch CAM")
    roh["werkzeuge"] = [w for w in roh.get("werkzeuge") or [] if isinstance(w, dict) and w.get("id")]
    return roh


def _gegenueber(roh: Dict, db: Dict) -> Tuple[List[Dict], int, List[Dict]]:
    """(neu, Anzahl gleich, Konflikte [{hier, datei}]) – gleich ist, was gleiche ID und gleichen Namen hat
    oder denselben Namen; verschieden nur, wenn die Werte abweichen."""
    neu, konflikte, gleich = [], [], 0
    hier_id = {w["id"]: w for w in db["werkzeuge"]}
    hier_name = {}
    for w in db["werkzeuge"]:
        hier_name.setdefault(name(w), w)
    for w in roh["werkzeuge"]:
        h = hier_id.get(w["id"])
        if h is not None and name(h) != name(w):
            h = None                               # gleiche ID, anderer Fräser (anderer Rechner)
        if h is None:
            h = hier_name.get(name(w))
        if h is None:
            neu.append(w)
        elif _vergleich(h) == _vergleich(w):
            gleich += 1
        else:
            konflikte.append({"hier": h, "datei": w})
    return neu, gleich, konflikte


def _kurz(w: Dict, gegen: Dict) -> str:
    unt = []
    for f in _GEOMETRIE + ("schaft", "schnittdaten", "drehzahl", "vorschub", "eintauchen", "notiz"):
        a, b = w.get(f), gegen.get(f)
        if f == "schnittdaten":
            a, b = _db_modus(w), _db_modus(gegen)
        if a is not None and not _gleich(a, b):
            unt.append("%s %s" % (_FELDNAMEN.get(f, f), _wert_text(f, a)) if f != "schnittdaten"
                       else _MODUSNAMEN.get(a, a))
    return ", ".join(unt[:3]) or "andere Werte"


def import_vorschau(quelle: str, datei: Optional[str] = None) -> Dict:
    roh = _import_lesen(quelle)
    db = laden(datei)
    neu, gleich, konflikte = _gegenueber(roh, db)
    ma = roh.get("maschine") if isinstance(roh.get("maschine"), dict) else {}
    ma_anders = any(k in ma and not _gleich(ma[k], db["maschine"].get(k)) for k in STANDARD_MASCHINE)
    post = []
    eigene = _eigene_post()
    for k, d in (roh.get("postprozessoren") or {}).items():
        if isinstance(d, dict):
            post.append({"schluessel": k, "name": d.get("name") or k,
                         "neu": k not in eigene, "gleich": eigene.get(k) == d})
    return {"datei": quelle, "datum": roh.get("datum", ""),
            "neu": [name(w) for w in neu], "gleich": gleich,
            "konflikte": [{"id": c["hier"]["id"], "name": name(c["hier"]),
                           "hier": _kurz(c["hier"], c["datei"]), "datei": _kurz(c["datei"], c["hier"])}
                          for c in konflikte],
            "maschine": bool(ma) and ma_anders, "postprozessoren": post,
            "anzahl": len(roh["werkzeuge"])}


def importieren(quelle: str, wahl: Optional[Dict] = None, maschine: bool = True, post=True,
                datei: Optional[str] = None) -> Dict:
    """Datei übernehmen: neue Fräser dazu, bei Konflikten nach wahl {id_hier: "hier"|"datei"}
    (Vorgabe «hier»), Maschine und Postprozessoren auf Wunsch (post: True/False oder Liste der Schlüssel).
    Gelöscht wird nichts;
    werkzeuge.json wird vorher als werkzeuge-vorher.json gesichert."""
    import shutil
    roh = _import_lesen(quelle)
    ziel = datei or pfad()
    if os.path.exists(ziel):
        # Sicherung wie bisher im Datenordner (bei mehreren Maschinen: die Datei der geltenden Maschine)
        from .orte import konfig
        sich = os.path.join(konfig() if datei is None else os.path.dirname(ziel), "werkzeuge-vorher.json")
        shutil.copyfile(ziel, sich)
    db = laden(datei)
    neu, _gleich_n, konflikte = _gegenueber(roh, db)
    wahl = wahl or {}
    for w in neu:
        w = copy.deepcopy(w)
        if w["id"] in {x["id"] for x in db["werkzeuge"]} or w["id"] in (db.get("ersetzt") or {}):
            w["id"] = neue_id(db)
        db["werkzeuge"].append(w)
    ersetzt = 0
    for c in konflikte:
        if wahl.get(c["hier"]["id"]) == "datei":
            w = copy.deepcopy(c["datei"])
            w["id"] = c["hier"]["id"]                  # Zeichnungen bleiben verknüpft
            _einsetzen(db, w)
            ersetzt += 1
    if maschine and isinstance(roh.get("maschine"), dict):
        for k in STANDARD_MASCHINE:
            if k in roh["maschine"]:
                db["maschine"][k] = roh["maschine"][k]
        db["maschine_geaendert"] = round(time.time(), 3)
    schreiben(db, datei)
    posts = 0
    if post:
        o = _post_ordner()
        for k, d in (roh.get("postprozessoren") or {}).items():
            if not isinstance(d, dict) or "/" in k or "\\" in k or k.startswith("."):
                continue
            if isinstance(post, (list, tuple, set)) and k not in post:
                continue
            os.makedirs(o, exist_ok=True)
            p = os.path.join(o, k + ".json")
            if os.path.exists(p):
                shutil.copyfile(p, p + ".vorher")
            with open(p, "w", encoding="utf-8") as fh:
                json.dump(d, fh, ensure_ascii=False, indent=1)
            posts += 1
    return {"neu": len(neu), "ersetzt": ersetzt, "postprozessoren": posts}


_TEXT_MASCHINE = ("postprozessor", "werkzeugwechsel", "zielordner")


# Fachwerte je Werkstoff, die man unter «Details» ändern kann (Peter 06.11., Konzept), mit Grenzen
FACHWERTE = {"vc": (20, 2000), "k": (0.001, 0.1), "k1": (0.001, 0.2), "fz_min": (0.001, 1.0), "fz_max": (0.005, 1.0),
             "fz_max1": (0.005, 1.0), "zustellung": (0.05, 3.0), "bahnabstand": (5, 95), "rampe": (0.5, 30.0),
             "vollnut": (0.1, 1.0)}
_MAT_CACHE: Dict[str, Any] = {}


def materialien() -> Dict[str, Dict]:
    """Werkstoffe, die gelten: die mitgelieferten (MATERIALIEN) mit geänderten Fachwerten und die eigenen Werkstoffe
    (Kopie eines mitgelieferten mit eigenem Namen). Gespeichert in maschinen.json («werkstoffe»), gilt für alle
    Maschinen; die Schieber «langsamer – schneller» bleiben je Maschine (werkstoff_prozent)."""
    from . import maschinen
    try:
        p = maschinen.index_pfad()
        stand = os.path.getmtime(p) if os.path.exists(p) else 0
        if _MAT_CACHE.get("pfad") == p and _MAT_CACHE.get("stand") == stand:
            return copy.deepcopy(_MAT_CACHE["werte"])
        eigene = maschinen.index().get("werkstoffe") or {}
    except Exception:
        return copy.deepcopy(MATERIALIEN)
    aus = copy.deepcopy(MATERIALIEN)
    for k, e in eigene.items():
        if not isinstance(e, dict):
            continue
        basis_key = k if k in MATERIALIEN else e.get("basis")
        if basis_key not in MATERIALIEN:
            continue
        m = copy.deepcopy(MATERIALIEN[basis_key])
        for f, v in (e.get("werte") or {}).items():
            if f in FACHWERTE:
                try:
                    lo, hi = FACHWERTE[f]
                    m[f] = min(max(float(v), lo), hi)
                except (TypeError, ValueError):
                    pass
        if k not in MATERIALIEN:
            m["name"] = str(e.get("name") or k)
            m["eigen"] = True
            m["basis"] = basis_key
        m["geaendert"] = bool(e.get("werte"))
        aus[k] = m
    _MAT_CACHE.update({"pfad": p, "stand": stand, "werte": copy.deepcopy(aus)})
    return aus


def _werkstoffe_aendern(aenderung):
    from . import maschinen
    idx = maschinen.index()
    w = dict(idx.get("werkstoffe") or {})
    erg = aenderung(w)
    idx["werkstoffe"] = w
    idx["werkstoffe_geaendert"] = round(time.time(), 3)     # Abgleich zweier PCs: die neuere gilt
    maschinen.index_schreiben(idx)
    _MAT_CACHE.clear()
    return erg


def werkstoff_eigen_neu(basis: str, name_: str) -> str:
    """Eigener Werkstoff als Kopie eines mitgelieferten (oder eines eigenen: dann dessen Grundlage und Werte)."""
    import uuid
    alle = materialien()
    if basis not in alle:
        raise ValueError("Unbekannter Werkstoff: %r" % basis)
    m = alle[basis]
    grund = m.get("basis") or basis
    werte = {f: m[f] for f in FACHWERTE if f in m and m[f] != MATERIALIEN[grund].get(f)}
    key = "eigen_" + uuid.uuid4().hex[:6]

    def a(w):
        w[key] = {"name": (name_ or "").strip() or (m["name"] + " (Kopie)"), "basis": grund, "werte": werte}
        return key
    return _werkstoffe_aendern(a)


def werkstoff_fachwerte(key: str, werte: Dict) -> None:
    alle = materialien()
    if key not in alle:
        raise ValueError("Unbekannter Werkstoff: %r" % key)
    grund = MATERIALIEN[alle[key].get("basis") or key]

    def a(w):
        e = dict(w.get(key) or {})
        neu = dict(e.get("werte") or {})
        for f, v in werte.items():
            if f not in FACHWERTE:
                continue
            lo, hi = FACHWERTE[f]
            v = min(max(float(v), lo), hi)
            if abs(v - float(grund.get(f, v))) < 1e-9:
                neu.pop(f, None)
            else:
                neu[f] = v
        e["werte"] = neu
        if key in MATERIALIEN and not neu:
            w.pop(key, None)
        else:
            w[key] = e
    _werkstoffe_aendern(a)


def werkstoff_herstellerwerte(key: str) -> None:
    """Fachwerte zurück auf die mitgelieferten (bei eigenen: die des Grundwerkstoffs)."""
    def a(w):
        if key in MATERIALIEN:
            w.pop(key, None)
        elif key in w:
            w[key] = dict(w[key], werte={})
    _werkstoffe_aendern(a)


def werkstoff_loeschen(key: str) -> None:
    if key in MATERIALIEN:
        return                                   # mitgelieferte bleiben
    _werkstoffe_aendern(lambda w: w.pop(key, None))


PROZENT_GRENZEN = (50, 150)        # Schieber der Werkstoffe: halb so schnell … anderthalbmal so schnell


def werkstoff_prozent(ma: Optional[Dict], material: str) -> Tuple[float, float]:
    """(Vorschub, Drehzahl) als Faktor für diesen Werkstoff an dieser Maschine; 1.0 = Herstellerwert."""
    e = ((ma or {}).get("werkstoffe") or {}).get(material) or {}
    lo, hi = PROZENT_GRENZEN
    def f(k):
        try:
            return min(max(float(e.get(k, 100)), lo), hi) / 100.0
        except (TypeError, ValueError):
            return 1.0
    return f("vorschub"), f("drehzahl")


def werkstoff_setzen(material: str, vorschub: float, drehzahl: float, datei: Optional[str] = None):
    if material not in materialien():
        raise ValueError("Unbekannter Werkstoff: %r" % material)
    lo, hi = PROZENT_GRENZEN
    db = laden(datei)
    ws = dict(db["maschine"].get("werkstoffe") or {})
    v, n = int(round(min(max(vorschub, lo), hi))), int(round(min(max(drehzahl, lo), hi)))
    if v == 100 and n == 100:
        ws.pop(material, None)
    else:
        ws[material] = {"vorschub": v, "drehzahl": n}
    db["maschine"]["werkstoffe"] = ws
    db["maschine_geaendert"] = round(time.time(), 3)
    schreiben(db, datei)


def werkstoffe_vorschau(datei: Optional[str] = None) -> List[Dict]:
    """Je Werkstoff: Prozente und das Ergebnis für einen Schaftfräser Ø 6 mit 2 Schneiden (Einstellungen)."""
    db = laden(datei)
    probe = {"typ": "schaft", "durchmesser": 6.0, "schneiden": 2, "schneidenlaenge": 22.0}
    aus = []
    for k, m in materialien().items():
        v, n = werkstoff_prozent(db["maschine"], k)
        sd = schnittdaten(probe, k, db["maschine"])
        aus.append({"key": k, "name": m["name"], "vorschub": int(round(v * 100)), "drehzahl": int(round(n * 100)),
                    "ergebnis_drehzahl": sd["drehzahl"], "ergebnis_vorschub": sd["vorschub"],
                    "rechnung": sd["rechnung_text"], "eigen": bool(m.get("eigen")),
                    "geaendert": bool(m.get("geaendert")),
                    "fachwerte": {f: m[f] for f in FACHWERTE if f in m}})
    return aus


def maschine_setzen(werte: Dict, datei: Optional[str] = None):
    db = laden(datei)
    vorher = dict(db["maschine"])
    for k in STANDARD_MASCHINE:
        if k == "werkstoffe":
            continue                         # eigener Befehl (werkstoff_setzen)
        if k in werte:
            db["maschine"][k] = str(werte[k] or "") if k in _TEXT_MASCHINE else float(werte[k])
    if db["maschine"] != vorher:
        db["maschine_geaendert"] = round(time.time(), 3)
    schreiben(db, datei)


def name(w: Dict) -> str:
    if w.get("name"):
        return w["name"]
    typ = w.get("typ", "schaft")
    if typ == "v":
        t = f"V-Fräser {float(w.get('v_winkel', 60)):g}°"
        if float(w.get("spitze") or 0) > 0:
            t += f" / {float(w['spitze']):g} mm"
        return t
    if typ == "bohrer":
        return f"Bohrer Ø {float(w.get('durchmesser', 3)):g} mm"
    return f"{FORMEN.get(typ, 'Fräser')} Ø {float(w.get('durchmesser', 3)):g} mm"


SCHNITTMODI = ("auto", "drehzahl", "hand")


def schnittmodus(w: Dict) -> str:
    """Woher die Schnittdaten eines Werkzeugs kommen:
    «auto»     Drehzahl und Vorschub aus Werkstoff und Werkzeug,
    «drehzahl» Drehzahl von Hand (an der Spindel eingestellt, z. B. wo es am wenigsten vibriert),
               Vorschub und Eintauchen daraus für den Werkstoff gerechnet,
    «hand»     alles von Hand.
    Ältere Zeichnungen kennen nur «auto»: true/false."""
    m = w.get("schnittdaten")
    if m in SCHNITTMODI:
        return m
    return "auto" if w.get("auto", False) else "hand"


def fuer_werkstoff(w: Dict, material: str) -> Dict:
    """Werkzeug mit den Schnittdaten, die für diesen Werkstoff gelten: eigener Eintrag in
    je_werkstoff[material] (Modus und Werte), sonst die allgemeinen Angaben des Werkzeugs."""
    e = (w.get("je_werkstoff") or {}).get(material)
    if not isinstance(e, dict):
        return w
    w = dict(w)
    m = e.get("schnittdaten") if e.get("schnittdaten") in SCHNITTMODI else "auto"
    w["schnittdaten"] = m
    w["auto"] = m == "auto"
    for f in ("drehzahl", "vorschub", "eintauchen"):
        if f in e:
            w[f] = e[f]
    return w


def schnittdaten(w: Dict, material: str = STANDARD_MATERIAL, maschine: Optional[Dict] = None,
                 drehzahl: Optional[float] = None) -> Dict:
    """Drehzahl, Vorschub, Eintauchen und Richtwerte für ein Werkzeug im Material.
    drehzahl: an der Spindel eingestellte Drehzahl – dann wird nur der Vorschub dazu gerechnet
    (Vorschub = Drehzahl × Schneiden × Zahnvorschub des Werkstoffs).
    «erklaerung»: wie gerechnet wurde (Zeile im Werkzeugfenster, Peter 02.11.)."""
    alle = materialien()
    mat_key = material if material in alle else STANDARD_MATERIAL
    m = alle[mat_key]
    ma = dict(STANDARD_MASCHINE, **(maschine or {}))
    d_oben = max(0.1, float(w.get("durchmesser", 3)))
    typ = w.get("typ", "schaft")
    z = max(1, int(w.get("schneiden") or 2))
    runden = (lambda v, s: max(s, round(v / s) * s))
    name_ = str(w.get("name") or "").lower()
    plan = typ == "schaft" and (bool(w["wendeplatten"]) if "wendeplatten" in w
                                else "planfr" in name_ or "wendeplatt" in name_)   # wie fraeser_merkmale
    sl = float(w.get("schneidenlaenge") or 99)
    n_max, n_min = float(ma["drehzahl_max"]), float(ma["drehzahl_min"])

    pv, pn = werkstoff_prozent(ma, mat_key)     # Schieber des Werkstoffs an dieser Maschine

    def zahl(v, nk=0):
        # Tausender mit schmalem Abstand (U+202F) wie überall in der Oberfläche
        t = f"{v:,.{nk}f}".replace(",", "\u2009").replace(".", ",")
        return t.replace("\u2009", "\u202f")

    def ausgabe(n, f_ideal, anteil, zust, fz, d_wirk, erkl, rechnung):
        f_roh = f_ideal
        f_ideal = f_ideal * pv
        f = min(f_ideal, ma["vorschub_max"])
        # Rechnung in Klartext (Peter 06.11.): «19 000 U/min × 2 × 0,12 mm = 4560 mm/min × 85 % = 3880 mm/min»
        # Zoll: jede Grösse selbst umgerechnet – die Oberfläche läse «× 2 × 0,12 mm» sonst als Kette (2 → 0,079 Zoll)
        from . import einheit
        if einheit.zoll():
            def lg(v):
                return einheit.zahl(v, 3)

            def vs(v):
                return einheit.zahl(v, 0, groesse="vorschub")
        else:
            def lg(v):
                return zahl(v, 3).rstrip('0').rstrip(',') + " mm"

            def vs(v):
                return zahl(v) + " mm/min"
        txt = f"{zahl(runden(n, 100))} U/min × {z} × {lg(fz)} = {vs(runden(f_roh, 10))}"
        if abs(pv - 1) > 1e-9:
            txt += f" × {int(round(pv * 100))} % = {vs(runden(f_ideal, 10))}"
        if f_ideal > ma["vorschub_max"] + 1e-6:
            txt += f" → Maschine höchstens {vs(ma['vorschub_max'])}"
        return {"drehzahl": int(runden(n, 100)), "vorschub": int(runden(f, 10)),
                "eintauchen": int(runden(f * anteil, 10)),
                "zustellung": round(min(zust, sl), 2),
                "bahnabstand": m["bahnabstand"], "rampe": m["rampe"], "vollnut": m["vollnut"],
                "fz": round(fz, 4),
                # für «Drehzahl von Hand» im Dialog: dieselbe Rechnung wie hier (ungerundet)
                "fz_genau": fz,
                "eintauch_anteil": anteil,
                # Maschine zu langsam für diese Drehzahl: der Span wird dünner als ideal
                "vorschub_begrenzt": f_ideal > ma["vorschub_max"] + 1e-6,
                "vorschub_ideal": int(runden(f_ideal, 10)),
                "vorschub_max": float(ma["vorschub_max"]),
                "d_wirksam": round(d_wirk, 3),
                "erklaerung": erkl,
                # wie gerechnet: «stichel», «plan» oder «fraeser» (Zeile im Werkzeugfenster)
                "rechnung": rechnung,
                "rechnung_text": txt, "prozent_vorschub": int(round(pv * 100)),
                "prozent_drehzahl": int(round(pn * 100)),
                "vc": round(math.pi * d_wirk * n / 1000.0, 1)}

    # ---- Gravierstichel: eigene Werte je Werkstoff (Herstellerangaben), Spindel so schnell wie erlaubt
    if ist_stichel(w):
        st = STICHEL.get(mat_key, STICHEL["hartholz"])
        n = float(drehzahl) if drehzahl and drehzahl > 0 else min(max(n_max * pn, n_min), n_max)
        # Tabelle gilt für 1 Schneide bei der höchsten Drehzahl der Maschine (sonst im Verhältnis)
        f_ideal = st["vorschub"] * (1.0 + 0.5 * (z - 1)) * min(1.0, n / max(n_max, 1.0))
        d_w = schneide_d(w, st["zustellung"])
        fz = f_ideal / max(n * z, 1.0)
        erkl = (f"Gravierstichel · schneidet etwa {d_w:.2f} mm breit (Spitze {float(w.get('spitze') or 0):g} mm) · "
                f"{m['name']} · Herstellerwerte für Stichel").replace(".", ",")
        return ausgabe(n, f_ideal, st["eintauchen"], st["zustellung"], fz, d_w, erkl, "stichel")

    # ---- Planfräser: Zahnvorschub nach Ø (bis Ø 25), eigene Schnittgeschwindigkeit, kleine Zustellung
    if plan:
        pl = PLAN.get(mat_key, PLAN["hartholz"])
        if drehzahl and drehzahl > 0:
            n = float(drehzahl)
        else:
            n = min(max(pl["vc"] * 1000.0 / (math.pi * d_oben) * pn, n_min), n_max)
        fz = pl["k"] * min(d_oben, 25.0) * (0.8 if z >= 3 else 1.0)
        f_ideal = n * z * fz
        anteil = (1.0 / z + 0.5) / 2
        erkl = (f"Planfräser · Ø {d_oben:g} mm, {z} Schneiden · {m['name']} · Herstellerwerte für Planfräser"
                ).replace(".", ",")
        return ausgabe(n, f_ideal, anteil, pl["zustellung"], fz, d_oben, erkl, "plan")

    # ---- übrige Fräser: Zahnvorschub nach dem Ø, der schneidet
    vc = m["vc"]
    if typ == "bohrer":
        vc *= BOHRER_VC          # Bohrer: kleinere Schnittgeschwindigkeit als ein Fräser
    zust = m["zustellung"] * d_oben
    if typ in ("v", "konisch"):
        zust = min(zust, 2.0)
    elif typ == "bohrer":
        zust = max(0.5, m["zustellung"]) * d_oben   # Entspanen etwa alle 0,5–1 × Ø
    d = schneide_d(w, min(zust, sl))
    if drehzahl and drehzahl > 0:
        n = float(drehzahl)                    # so dreht die Spindel wirklich
    else:
        n = vc * 1000.0 / (math.pi * d) * pn
        n = min(max(n, n_min), n_max)

    def fz_bei(dd):
        if z == 1 and "k1" in m:
            return min(max(m["k1"] * dd, m["fz_min"]), m.get("fz_max1", m["fz_max"]))
        f_ = min(max(m["k"] * dd, m["fz_min"]), m["fz_max"])
        return f_ * 0.8 if z >= 3 else f_
    fz = fz_bei(d) * FZ_FAKTOR_WIRKSAM.get(typ, FZ_FAKTOR.get(typ, 1.0))
    f_ideal = n * z * fz
    # der Bohrer schneidet beim Eintauchen: dort gilt sein ganzer Vorschub
    # Eintauchen: Mittel aus Amana (Vorschub ÷ Schneiden) und Harvey (½): 1 Schneide 75 %, 2: 50 %, 3: 42 %
    anteil = 1.0 if typ == "bohrer" else (1.0 / z + 0.5) / 2
    art = FORMEN.get(typ, "Fräser")
    if typ in ("v", "konisch"):
        erkl = f"{art} · schneidet bei {round(min(zust, sl), 1):g} mm Tiefe etwa {d:.2f} mm breit · {m['name']}"
    else:
        erkl = f"{art} · Ø {d:g} mm, {z} Schneide{'n' if z != 1 else ''} · {m['name']}"
    erkl = (erkl + " · Mittelwert der Herstellertabellen").replace(".", ",")
    return ausgabe(n, f_ideal, anteil, zust, fz, d, erkl, "fraeser")


def wirksam(werkzeuge: Dict[str, Dict], material: str, maschine: Optional[Dict] = None) -> Dict[str, Dict]:
    """Werkzeuge einer Zeichnung mit den Schnittdaten, die gelten: bei «auto»
    aus dem Material gerechnet, bei «drehzahl» der Vorschub zur eingetragenen Drehzahl,
    sonst die eingetragenen."""
    ma = dict(STANDARD_MASCHINE, **(maschine or {}))
    n_max, n_min = float(ma["drehzahl_max"]), float(ma["drehzahl_min"])
    aus = {}
    for k, w in werkzeuge.items():
        w = dict(fuer_werkstoff(w, material))
        modus = schnittmodus(w)
        # Höchst- und Mindestdrehzahl der Maschine gelten für alle Fräser, auch für Drehzahlen von Hand
        # (Peter, 21.10.: Spindel vibriert über 19 000 – Maschine auf 19 000 begrenzt)
        n_eingetragen = float(w.get("drehzahl") or 0)
        n_grenze = min(max(n_eingetragen, n_min), n_max) if n_eingetragen > 0 else 0.0
        if modus in ("drehzahl", "hand") and n_eingetragen > 0 and abs(n_grenze - n_eingetragen) > 0.5:
            w["drehzahl_eingetragen"] = int(round(n_eingetragen))
            w["drehzahl"] = int(round(n_grenze))
            if modus == "hand":             # gleicher Span je Schneide: Vorschub im selben Verhältnis
                f = n_grenze / n_eingetragen
                for x in ("vorschub", "eintauchen"):
                    if w.get(x):
                        w[x] = int(max(10, round(float(w[x]) * f / 10) * 10))
        n_hand = float(w.get("drehzahl") or 0) if modus == "drehzahl" else 0.0
        sd = schnittdaten(w, material, maschine, drehzahl=n_hand or None)
        if modus == "auto":
            for f in ("drehzahl", "vorschub", "eintauchen"):
                w[f] = sd[f]
        elif modus == "drehzahl" and n_hand > 0:
            for f in ("vorschub", "eintauchen"):
                w[f] = sd[f]
        w["schnittdaten"] = modus
        w.setdefault("rampe", sd["rampe"])
        w["vollnut"] = sd["vollnut"]
        w["empfehlung"] = sd
        aus[k] = w
    return aus


def wirksam_alle(werkzeuge: Dict[str, Dict], material: str, db: Optional[Dict] = None) -> Dict[str, Dict]:
    """Schnittdaten, die gelten: für die Werkzeuge der Zeichnung und für «Meine Werkzeuge»."""
    db = db or laden()
    return {"werkzeuge_wirksam": wirksam(werkzeuge or {}, material, db["maschine"]),
            "datenbank_wirksam": wirksam({w["id"]: dict(w, schnittdaten=_db_modus(w)) for w in db["werkzeuge"]},
                                         material, db["maschine"])}


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    befehl = argv[0] if argv else "liste"
    if befehl == "speichern":
        erg = {"ok": True, "werkzeug": speichern(json.loads(argv[1]))}
    elif befehl == "loeschen":
        loeschen(argv[1])
        erg = {"ok": True}
    elif befehl == "uebernehmen":
        erg = dict(uebernehmen(json.loads(argv[1])), ok=True)
    elif befehl == "export":
        erg = dict(exportieren(argv[1]), ok=True)
    elif befehl == "import_vorschau":
        erg = {"ok": True, "vorschau": import_vorschau(argv[1])}
    elif befehl == "import":
        opt = json.loads(argv[2]) if len(argv) > 2 else {}
        pp = opt.get("postprozessoren", True)
        erg = dict(importieren(argv[1], opt.get("wahl"), bool(opt.get("maschine", True)),
                               pp if isinstance(pp, list) else bool(pp)), ok=True)
    elif befehl == "abgleich":                # Einstellungen › Maschine: Katalog mit einem Ordner abgleichen
        from . import werkzeugabgleich
        a = json.loads(argv[1])
        erg = {"ok": True, "abgleich": werkzeugabgleich.setzen(bool(a.get("an")), str(a.get("ordner") or ""))}
    elif befehl == "maschine":
        maschine_setzen(json.loads(argv[1]))
        erg = {"ok": True}
    elif befehl == "werkstoff":                # Einstellungen › Werkstoffe: Schieber
        a = json.loads(argv[1])
        werkstoff_setzen(a["key"], float(a.get("vorschub", 100)), float(a.get("drehzahl", 100)))
        erg = {"ok": True, "werkstoffe": werkstoffe_vorschau()}
    elif befehl == "startliste":                # Ersteinrichtung: Fräser metrisch oder in Zoll
        a = json.loads(argv[1])
        startliste("zoll" if a.get("art") == "zoll" else "mm")
        erg = {"ok": True}
    elif befehl == "werkstoffe":
        erg = {"ok": True, "werkstoffe": werkstoffe_vorschau()}
    elif befehl == "werkstoff_details":        # Einstellungen › Werkstoffe › Details
        a = json.loads(argv[1])
        was, key = a.get("was"), str(a.get("key") or "")
        neu_key = None
        if was == "neu":
            neu_key = werkstoff_eigen_neu(key, str(a.get("name") or ""))
        elif was == "werte":
            werkstoff_fachwerte(key, a.get("werte") or {})
        elif was == "zurueck":
            werkstoff_herstellerwerte(key)
        elif was == "loeschen":
            werkstoff_loeschen(key)
        erg = {"ok": True, "werkstoffe": werkstoffe_vorschau(), "neu": neu_key}
    elif befehl == "maschinen":                # Einstellungen › Maschinen (B-25)
        from . import maschinen
        a = json.loads(argv[1]) if len(argv) > 1 else {}
        was = a.get("was")
        if was == "neu":
            erg = {"ok": True, "neu": maschinen.neu(str(a.get("name") or ""), a.get("kopie_von"))}
        elif was == "umbenennen":
            maschinen.umbenennen(str(a["id"]), str(a.get("name") or ""))
            erg = {"ok": True}
        elif was == "entfernen":
            maschinen.entfernen(str(a["id"]))
            erg = {"ok": True}
        elif was == "zuletzt":
            maschinen.zuletzt_setzen(str(a["id"]))
            erg = {"ok": True}
        elif was == "vergleich":
            erg = {"ok": True, "vergleich": maschinen.vergleich(str(a["ziel"]), a.get("werkzeuge") or {})}
        elif was == "wechseln":
            erg = {"ok": True, "uebernommen": maschinen.wechseln(str(a["ziel"]), a.get("werkzeuge") or {},
                                                                 bool(a.get("uebernehmen")))}
        else:
            erg = {"ok": True}
    elif befehl == "vorgabe":                  # Einstellungen › Fräsen: «So fräse ich»
        from . import vorgaben
        a = json.loads(argv[1])
        if a.get("alle_zurueck"):
            vorgaben.zuruecksetzen()
        elif isinstance(a.get("werte"), dict):       # Bearbeitung › «Als Vorgabe merken»: mehrere auf einmal
            vorgaben.mehrere_setzen(a["werte"])
        else:
            vorgaben.setzen(str(a["key"]), a.get("wert"))
        erg = {"ok": True}
    elif befehl == "wirksam":                  # Werkzeug-Dialog nach «Übernehmen»
        a = json.loads(argv[1])
        erg = dict(wirksam_alle(a.get("werkzeuge") or {}, a.get("material") or STANDARD_MATERIAL), ok=True)
    else:
        erg = {"ok": True}
    db = laden()
    if "abgleich" not in erg:
        from . import werkzeugabgleich
        erg["abgleich"] = werkzeugabgleich.einstellung()
    from . import maschinen, vorgaben
    erg["vorgaben"] = vorgaben.fuer_oberflaeche()
    erg["maschinen"] = maschinen.liste()
    erg["maschine_id"] = maschinen.aktiv_id()
    erg.update({"werkzeuge": db["werkzeuge"], "maschine": db["maschine"],
                "materialien": [[k, v["name"]] for k, v in materialien().items()]})
    sys.stdout.write(json.dumps(erg, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
