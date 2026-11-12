#!/usr/bin/env python3
"""G-Code zweier Programme vergleichen (z. B. Hirch CAM gegen ein anderes CAM-Programm), je Werkzeug-Abschnitt.

    python3 werkzeuge/gcode_vergleich.py A.tap [B.tap …] [--eilgang 3000] [--beschleunigung 0] [--json]

Liest .tap/.nc im üblichen G-Code (G0/G1/G2/G3, X Y Z I J F, Werkzeugwechsel «T…M6» oder «M06T…»)
und zählt je Abschnitt (Werkzeug):

- Fräsweg: Weg im Vorschub (G1/G2/G3, Bögen als Bogenlänge), davon im Material (Z < 0)
- Leerweg: Weg im Eilgang (G0), davon waagrecht
- Eintauchen: zusammenhängende Vorschübe nach unten, die unter der Oberfläche (Z < 0) enden. «von oben»:
  der Fräser kam aus der Luft oder im Eilgang (auch Bohren mit Entspanen, je Stufe); «im Material»: Zustellen
  auf die nächste Tiefe ohne Abheben. Je Art: senkrecht, Rampe (gerade mit XY), Helix (Bögen mit Z).
  Weg = Z-Weg unter der Oberfläche.
- Freifahren: Eilgänge nach oben (Anzahl)
- Zeit: Weg / Vorschub, Eilgang mit --eilgang (wie die Zeitschätzung von Hirch CNC: 3000 mm/min, ohne
  Beschleunigung). Mit --beschleunigung (mm/s²) zusätzlich je Bewegung Anfahren und Bremsen (Trapez,
  Halt an jedem Knick – obere Grenze, die Steuerung schleift Ecken).
- Zeilen, Bögen (G2/G3), Dateigrösse; tiefste Stelle, Vorschübe, Drehzahl.

Nur Standardbibliothek, ohne Hirch CNC. Peters Dateien gehören nicht ins Repository – das Skript liest
sie von dort, wo sie liegen.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
import sys

WORT = re.compile(r"([A-Z])\s*(-?\d*\.?\d+)")


def bogen(p, q, cx, cy, gegen_uhr):
    """Länge eines Bogens (mit Z-Anteil) von p nach q um (cx, cy)."""
    r = math.hypot(p[0] - cx, p[1] - cy)
    a0 = math.atan2(p[1] - cy, p[0] - cx)
    a1 = math.atan2(q[1] - cy, q[0] - cx)
    d = a1 - a0
    if gegen_uhr:
        while d <= 1e-12:
            d += 2 * math.pi
    else:
        while d >= -1e-12:
            d -= 2 * math.pi
    eben = abs(d) * r
    return math.hypot(eben, q[2] - p[2])


def _zeit_trapez(d, v, a):
    """Sekunden für d mm bei v mm/s und a mm/s² (aus dem Stand, zum Stand)."""
    if d <= 0 or v <= 0:
        return 0.0
    if a <= 0:
        return d / v
    s_acc = v * v / a                     # Anfahren + Bremsen
    if d >= s_acc:
        return d / v + v / a
    return 2 * math.sqrt(d / a)


class Abschnitt:
    def __init__(self, werkzeug):
        self.werkzeug = werkzeug
        self.drehzahl = 0
        self.zeilen = 0
        self.boegen = 0
        self.fraesweg = 0.0
        self.fraesweg_material = 0.0
        self.leerweg = 0.0
        self.leerweg_xy = 0.0
        self.eintauchen = {"senkrecht": 0, "rampe": 0, "helix": 0}      # von oben
        self.zustellen = {"senkrecht": 0, "rampe": 0, "helix": 0}       # im Material
        self.eintauch_weg = 0.0        # Z-Weg unter der Oberfläche beim Eintauchen von oben
        self.zustell_weg = 0.0
        self.freifahren = 0
        self.zeit_s = 0.0
        self.zeit_a_s = 0.0
        self.tiefste = 0.0
        self.vorschuebe = {}           # F → Weg
        self.xy = [math.inf, math.inf, -math.inf, -math.inf]

    def werte(self):
        return {
            "werkzeug": self.werkzeug, "drehzahl": self.drehzahl, "zeilen": self.zeilen, "boegen": self.boegen,
            "fraesweg": round(self.fraesweg), "fraesweg_material": round(self.fraesweg_material),
            "leerweg": round(self.leerweg), "leerweg_xy": round(self.leerweg_xy),
            "eintauchen": sum(self.eintauchen.values()), **{"eintauchen_" + k: v for k, v in self.eintauchen.items()},
            "eintauch_weg": round(self.eintauch_weg, 1),
            "zustellen": sum(self.zustellen.values()), **{"zustellen_" + k: v for k, v in self.zustellen.items()},
            "zustell_weg": round(self.zustell_weg, 1), "freifahren": self.freifahren,
            "zeit_min": round(self.zeit_s / 60, 2), "zeit_a_min": round(self.zeit_a_s / 60, 2),
            "tiefste": round(self.tiefste, 3),
            "vorschub_haupt": max(self.vorschuebe, key=self.vorschuebe.get) if self.vorschuebe else 0,
            "bereich": [round(v, 1) for v in self.xy] if self.xy[0] < math.inf else [],
        }


def lesen(pfad, eilgang=3000.0, beschleunigung=0.0, marken=None):
    """marken: {Zeilennummer (ab 1): Name} – dort beginnt zusätzlich ein eigener Abschnitt (z. B. je
    Bearbeitung, aus der Ablauf-Liste von Hirch CNC)."""
    with open(pfad, "rb") as fh:
        roh = fh.read()
    erg = auswerten(roh.decode("latin-1"), eilgang, beschleunigung, marken)
    erg.update({"datei": os.path.basename(pfad), "bytes": len(roh)})
    return erg


def auswerten(text, eilgang=3000.0, beschleunigung=0.0, marken=None):
    text = text.upper()
    marken = marken or {}
    abschnitte = []
    akt = Abschnitt("T?")
    abschnitte.append(akt)
    p = (0.0, 0.0, 20.0)
    g, f = 0, 0.0
    letzte_eil = True          # letzte Bewegung war Eilgang (Fräser kommt von oben)
    gruppe = None              # laufendes Eintauchen: [von_oben, Bogen?, XY-Weg, Z oben, Z unten, Abschnitt]

    def schliessen():
        nonlocal gruppe
        if gruppe and gruppe[4] < -1e-6:
            von_oben, mit_bogen, xy, z0, z1, ab = gruppe
            art = "helix" if mit_bogen else ("senkrecht" if xy < 1e-3 else "rampe")
            weg = max(0.0, min(z0, 0.0) - z1)
            if von_oben:
                ab.eintauchen[art] += 1
                ab.eintauch_weg += weg
            else:
                ab.zustellen[art] += 1
                ab.zustell_weg += weg
        gruppe = None
    for nr_zeile, zeile in enumerate(text.splitlines(), 1):
        if nr_zeile in marken:
            schliessen()
            if akt.fraesweg > 0 or akt.leerweg > 0:
                akt = Abschnitt(marken[nr_zeile])
                abschnitte.append(akt)
            else:
                akt.werkzeug = marken[nr_zeile]
        zeile = zeile.split("(")[0].split(";")[0].strip()
        if not zeile:
            continue
        m = re.search(r"T(\d+)\s*M0?6|M0?6\s*T(\d+)", zeile)
        if m:
            nr = m.group(1) or m.group(2)
            schliessen()
            if akt.zeilen <= 3 and akt.fraesweg == 0 and akt.leerweg == 0:
                akt.werkzeug = "T" + nr        # Kopf der Datei
            else:
                akt = Abschnitt("T" + nr)
                abschnitte.append(akt)
            akt.zeilen += 1
            continue
        akt.zeilen += 1
        woerter = WORT.findall(zeile)
        neu, ij = {}, {}
        for w, v in woerter:
            v = float(v)
            if w == "G":
                if int(v) in (0, 1, 2, 3):
                    g = int(v)
            elif w == "F":
                f = v
            elif w == "S":
                akt.drehzahl = int(v)
            elif w in "IJ":
                ij[w] = v
            elif w in "XYZ":
                neu[w] = v
        if not neu:
            continue
        q = (neu.get("X", p[0]), neu.get("Y", p[1]), neu.get("Z", p[2]))
        if g in (2, 3) and ij:
            akt.boegen += 1
            d = bogen(p, q, p[0] + ij.get("I", 0.0), p[1] + ij.get("J", 0.0), g == 3)
        else:
            d = math.dist(p, q)
        dxy = math.hypot(q[0] - p[0], q[1] - p[1])
        if g == 0:
            akt.leerweg += d
            akt.leerweg_xy += dxy
            if q[2] > p[2] + 1e-6:
                akt.freifahren += 1
            akt.zeit_s += d / (eilgang / 60)
            akt.zeit_a_s += _zeit_trapez(d, eilgang / 60, beschleunigung)
            schliessen()
            letzte_eil = True
        else:
            akt.fraesweg += d
            if min(p[2], q[2]) < -1e-6:
                akt.fraesweg_material += d
            akt.vorschuebe[f] = akt.vorschuebe.get(f, 0.0) + d
            v = f / 60 if f > 0 else 1.0
            akt.zeit_s += d / v
            akt.zeit_a_s += _zeit_trapez(d, v, beschleunigung)
            if q[2] < p[2] - 1e-6:                     # nach unten
                if gruppe is None:
                    gruppe = [letzte_eil or p[2] >= -1e-6, False, 0.0, p[2], q[2], akt]
                gruppe[1] = gruppe[1] or g in (2, 3)
                gruppe[2] += dxy
                gruppe[4] = q[2]
            elif abs(q[2] - p[2]) <= 1e-6 and gruppe is not None and gruppe[3] >= -1e-6 and q[2] >= -1e-6:
                pass                                   # waagrecht über der Oberfläche: Anfahren läuft weiter
            else:
                schliessen()
            # in der Luft (über der Oberfläche) zählt wie von oben
            letzte_eil = q[2] >= -1e-6 and (letzte_eil or p[2] >= -1e-6)
            if q[2] < -1e-6:
                b = akt.xy
                b[0], b[1], b[2], b[3] = min(b[0], q[0]), min(b[1], q[1]), max(b[2], q[0]), max(b[3], q[1])
        akt.tiefste = min(akt.tiefste, q[2])
        p = q
    schliessen()
    abschnitte = [a for a in abschnitte if a.fraesweg > 0 or a.leerweg > 0]
    return {"zeilen": len(text.splitlines()), "abschnitte": [a.werte() for a in abschnitte]}


SPALTEN = [("werkzeug", "WZ"), ("vorschub_haupt", "F"), ("tiefste", "Z min"), ("zeilen", "Zeilen"),
           ("boegen", "Bögen"), ("fraesweg", "Fräsweg"), ("fraesweg_material", "davon im Mat."),
           ("leerweg", "Leerweg"), ("leerweg_xy", "davon XY"), ("eintauchen", "Eintauchen"),
           ("eintauchen_senkrecht", "senkr."), ("eintauchen_rampe", "Rampe"), ("eintauchen_helix", "Helix"),
           ("zustellen", "Zustellen im Mat."), ("zustellen_senkrecht", "senkr."), ("zustellen_rampe", "Rampe"),
           ("zustellen_helix", "Helix"),
           ("freifahren", "Freifahren"), ("zeit_min", "Zeit min"), ("zeit_a_min", "mit Beschl.")]


def summe(abschnitte):
    s = {}
    for a in abschnitte:
        for k, v in a.items():
            if isinstance(v, (int, float)) and k not in ("drehzahl", "vorschub_haupt"):
                s[k] = s.get(k, 0) + v
    s["tiefste"] = min((a["tiefste"] for a in abschnitte), default=0)
    s["werkzeug"] = "Summe"
    s["vorschub_haupt"] = ""
    return s


def tabelle(erg, mit_beschl):
    sp = [x for x in SPALTEN if mit_beschl or x[0] != "zeit_a_min"]
    zeilen = ["| " + " | ".join(t for _, t in sp) + " |", "|" + "---|" * len(sp)]
    for a in erg["abschnitte"] + [summe(erg["abschnitte"])]:
        zeilen.append("| " + " | ".join(str(round(a.get(k), 2) if isinstance(a.get(k), float) else a.get(k, ""))
                                        for k, _ in sp) + " |")
    return "\n".join(zeilen)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("dateien", nargs="+")
    ap.add_argument("--eilgang", type=float, default=3000.0, help="mm/min (Hirch CNC rechnet mit 3000)")
    ap.add_argument("--beschleunigung", type=float, default=0.0, help="mm/s², 0 = ohne")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    alle = [lesen(d, a.eilgang, a.beschleunigung) for d in a.dateien]
    if a.json:
        json.dump(alle, sys.stdout, ensure_ascii=False, indent=1)
        print()
        return 0
    for erg in alle:
        print(f"### {erg['datei']} – {erg['bytes'] / 1024:.0f} KB, {erg['zeilen']} Zeilen\n")
        print(tabelle(erg, a.beschleunigung > 0))
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
