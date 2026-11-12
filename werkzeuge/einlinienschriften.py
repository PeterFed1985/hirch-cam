#!/usr/bin/env python3
"""Freie Einlinienschriften für Hirch CNC sammeln → hirch_cam/schriften/einlinien.json.

Holt jede Schrift aus ihrer Quelle, liest sie mit hirch_cam.schriftimport ein, ergänzt fehlende
Umlaute und schreibt sie in einlinien.json (Herkunft und Lizenz je Schrift im Feld «lic»).
Nur Schriften mit eindeutig freier Lizenz; Herstellerschriften anderer Programme nie (siehe LIZENZEN.md).

Aufruf (aus dem Repository):
    python3 werkzeuge/einlinienschriften.py QUELLORDNER

QUELLORDNER enthält (so heruntergeladen, Stand siehe docs/EINLINIENSCHRIFTEN.md):
    svg_fonts/            share/extensions/svg_fonts aus dem Inkscape-Quellbaum (EMS, Hershey)
    Relief-SingleLine/    git clone https://github.com/isdat-type/Relief-SingleLine
    hershey-fonts/        git clone https://github.com/kamalmostafa/hershey-fonts
    newstroke_font.cpp    common/newstroke_font.cpp aus https://github.com/KiCad/kicad-source-mirror
"""
from __future__ import annotations

import json
import os
import re
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HIER))

from hirch_cam import schriftimport  # noqa: E402

ZIEL = os.path.join(os.path.dirname(HIER), "hirch_cam", "schriften", "einlinien.json")

OFL = "SIL Open Font License 1.1"
HERSHEY = "Hershey-Lizenz (frei mit Danksagung)"

# (Name in Hirch CNC, Datei relativ zum Quellordner, Lizenz)
SVG = [
    ("EMS Allure", "svg_fonts/EMSAllure.svg", OFL),
    ("EMS Elfin", "svg_fonts/EMSElfin.svg", OFL),
    ("EMS Felix", "svg_fonts/EMSFelix.svg", OFL),
    ("EMS Nixish", "svg_fonts/EMSNixish.svg", OFL),
    ("EMS Nixish Italic", "svg_fonts/EMSNixishItalic.svg", OFL),
    ("EMS Osmotron", "svg_fonts/EMSOsmotron.svg", OFL),
    ("EMS Readability", "svg_fonts/EMSReadability.svg", OFL),
    ("EMS Readability Italic", "svg_fonts/EMSReadabilityItalic.svg", OFL),
    ("EMS Tech", "svg_fonts/EMSTech.svg", OFL),
    ("Hershey Sans", "svg_fonts/HersheySans1.svg", HERSHEY),
    ("Hershey Sans Medium", "svg_fonts/HersheySansMed.svg", HERSHEY),
    ("Hershey Serif", "svg_fonts/HersheySerifMed.svg", HERSHEY),
    ("Hershey Serif Italic", "svg_fonts/HersheySerifMedItalic.svg", HERSHEY),
    ("Hershey Serif Bold", "svg_fonts/HersheySerifBold.svg", HERSHEY),
    ("Hershey Serif Bold Italic", "svg_fonts/HersheySerifBoldItalic.svg", HERSHEY),
    ("Hershey Script", "svg_fonts/HersheyScript1.svg", HERSHEY),
    ("Hershey Script Medium", "svg_fonts/HersheyScriptMed.svg", HERSHEY),
    ("Hershey Gothic English", "svg_fonts/HersheyGothEnglish.svg", HERSHEY),
    ("Relief SingleLine", "Relief-SingleLine/fonts/open_svg/ReliefSingleLineSVG-Regular.svg",
     OFL + " (Relief SingleLine Project Authors, isdaT Toulouse)"),
]
JHF = [
    ("Hershey Gothic German", "hershey-fonts/hershey-fonts/gothicger.jhf", HERSHEY),
    ("Hershey Gothic Italian", "hershey-fonts/hershey-fonts/gothicita.jhf", HERSHEY),
]
NEWSTROKE = ("KiCad Newstroke", "newstroke_font.cpp",
             "GPL-2.0-or-later (KiCad, Vladimir Uryvaev)")

# Zeichen, die aus Grundbuchstabe + zwei Punkten zusammengesetzt werden, wenn sie fehlen
UMLAUTE = {"Ä": "A", "Ö": "O", "Ü": "U", "ä": "a", "ö": "o", "ü": "u", "ë": "e", "ï": "i", "ÿ": "y"}


def _bbox(striche):
    xs = [s[i] for s in striche for i in range(0, len(s), 2)]
    ys = [s[i] for s in striche for i in range(1, len(s), 2)]
    return (min(xs), min(ys), max(xs), max(ys)) if xs else None


def umlaute_ergaenzen(s: dict) -> list:
    """Fehlende Umlaute aus Grundbuchstabe und zwei Punkten (Punkt der Schrift oder kleiner Strich)."""
    g = s["g"]
    cap, xh = float(s["cap"]), float(s["xh"])
    punkt = None
    if "." in g and g["."][1]:
        b = _bbox(g["."][1])
        if b and (b[2] - b[0]) < 0.25 * cap and (b[3] - b[1]) < 0.25 * cap:
            cx, cy = (b[0] + b[2]) / 2, (b[1] + b[3]) / 2
            punkt = [[v - (cx if i % 2 == 0 else cy) for i, v in enumerate(st)] for st in g["."][1]]
    if punkt is None:                                   # kurzer senkrechter Strich als Punkt
        h = 0.04 * cap
        punkt = [[0.0, -h, 0.0, h]]
    neu = []
    for ch, basis in UMLAUTE.items():
        if ch in g or basis not in g or not g[basis][1]:
            continue
        adv, striche = g[basis]
        b = _bbox(striche)
        oben = (cap if basis.isupper() else xh) + 0.17 * cap
        mitte = (b[0] + b[2]) / 2
        abstand = max(0.12 * cap, min(0.22 * cap, (b[2] - b[0]) * 0.35))
        dazu = []
        for dx in (-abstand / 2, abstand / 2):
            for st in punkt:
                dazu.append([round(v + (mitte + dx if i % 2 == 0 else oben), 2) for i, v in enumerate(st)])
        g[ch] = [adv, [list(st) for st in striche] + dazu]
        neu.append(ch)
    return neu


def newstroke(pfad: str, name: str) -> dict:
    """KiCad-Newstroke (Hershey-Kodierung, Eintrag i = Zeichen U+0020 + i); nur Latein, Griechisch
    und gängige Zeichen (ohne CJK)."""
    with open(pfad, encoding="utf-8") as fh:
        text = fh.read()
    start = text.index("newstroke_font[] =")
    ende = text.index("};", start)
    eintraege = []
    for zeile in text[start:ende].splitlines():
        zeile = re.sub(r"/\*.*?\*/", "", zeile)
        teile = re.findall(r'"((?:[^"\\]|\\.)*)"', zeile)
        if teile:
            eintraege.append("".join(t.replace('\\"', '"').replace("\\\\", "\\") for t in teile))
    g = {}
    for i, e in enumerate(eintraege):
        cp = 0x20 + i
        if not (cp <= 0x017F or 0x0391 <= cp <= 0x03C9 or 0x2010 <= cp <= 0x205F or 0x20A0 <= cp <= 0x20CF
                or 0x2100 <= cp <= 0x214F or 0x2190 <= cp <= 0x21FF or 0x2200 <= cp <= 0x22FF
                or 0x2300 <= cp <= 0x23FF or 0x25A0 <= cp <= 0x25FF):
            continue          # Latein, Griechisch, Satzzeichen, Währung, Pfeile, Mathe, ⌀ – ohne CJK
        ch = chr(cp)
        if len(e) < 2:
            continue
        links, rechts = ord(e[0]) - ord("R"), ord(e[1]) - ord("R")
        striche, akt = [], []
        daten = e[2:]
        for j in range(0, len(daten) - 1, 2):
            a, b = daten[j], daten[j + 1]
            if a == " " and b == "R":
                if len(akt) >= 4:
                    striche.append(akt)
                akt = []
                continue
            x, y = ord(a) - ord("R") - links, -(ord(b) - ord("R"))
            akt += [float(x), float(y)]
        if len(akt) >= 4:
            striche.append(akt)
        g[ch] = [float(rechts - links), striche]
    # Grundlinie: Unterkante von «H» auf 0
    b = _bbox(g["H"][1])
    for ch, (adv, striche) in g.items():
        g[ch] = [adv, [[v - b[1] if i % 2 else v for i, v in enumerate(st)] for st in striche]]
    xb = _bbox(g["x"][1])
    return {"name": name, "cap": b[3] - b[1], "xh": xb[3] - xb[1], "art": "einlinie", "g": g}


def _runden(s: dict) -> dict:
    s["g"] = {ch: [round(float(adv), 2), [[round(float(v), 2) for v in st] for st in striche]]
              for ch, (adv, striche) in s["g"].items()}
    s["cap"], s["xh"] = round(float(s["cap"]), 2), round(float(s["xh"]), 2)
    return s


def main(quelle: str) -> None:
    alle = {}
    for name, datei, lic in SVG + JHF:
        s = schriftimport.einlesen(os.path.join(quelle, datei), name)
        alle[name] = (s, lic)
    s = newstroke(os.path.join(quelle, NEWSTROKE[1]), NEWSTROKE[0])
    alle[NEWSTROKE[0]] = (s, NEWSTROKE[2])
    aus = {}
    for name, (s, lic) in alle.items():
        neu = umlaute_ergaenzen(s)
        eintrag = {"cap": s["cap"], "xh": s["xh"], "lic": lic, "g": s["g"]}
        aus[name] = _runden(eintrag)
        print("%-28s %4d Zeichen%s" % (name, len(s["g"]), ("  ergänzt: " + "".join(neu)) if neu else ""))
    with open(ZIEL, "w", encoding="utf-8") as fh:
        json.dump(aus, fh, ensure_ascii=False, separators=(",", ":"))
    print("→", ZIEL, os.path.getsize(ZIEL) // 1024, "KB")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
