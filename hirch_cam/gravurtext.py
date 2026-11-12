# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Gravurtext für den Dialog im Panel: Text → Linien in mm (y nach oben,
Mitte des Textblocks bei 0,0), dazu die verfügbaren Schriften.

Aufruf:  python3 -m hirch_cam.dokument gravur '{"text": …, "schrift": …}'
         python3 -m hirch_cam.dokument schrift import|entfernen|tafel …
"""
from __future__ import annotations

import json
import sys
from typing import Dict, List

from . import einlinien, schriftimport

STANDARD = {"text": "Hirch CAM", "schrift": "Hershey Sans", "versalhoehe": 10.0, "zeilenabstand": 1.7,
            "ausrichtung": "mitte", "linien": 1, "linienabstand": 0.8, "optisch": True, "luft": 22,
            "breite": 0.5, "unterschneidung": False}


def schriftliste() -> List[Dict]:
    eigene = schriftimport.bibliothek()
    aus = []
    for name, s in einlinien.schriften().items():
        aus.append({"name": name, "art": s.get("art", "einlinie"), "eigen": name in eigene})
    return aus


def setzen(p: Dict) -> Dict:
    q = dict(STANDARD)
    q.update({k: v for k, v in p.items() if v is not None})
    namen = einlinien.schriftnamen()
    if q["schrift"] not in namen:
        q["schrift"] = namen[0]
    text = str(q["text"]).replace("\r", "")
    linien = einlinien.text_linien(
        text, q["schrift"], float(q["versalhoehe"]), float(q["zeilenabstand"]), bool(q["optisch"]),
        float(q["luft"]) / 100.0, float(q["breite"]), int(q["linien"]), float(q["linienabstand"]),
        q["ausrichtung"], bool(q["unterschneidung"])) if text.strip() else []
    glyphen = einlinien.schriften()[q["schrift"]]["g"]
    fehlend = "".join(sorted({c for c in text if c not in glyphen and c not in "\n\t"
                              and not any(e in glyphen for e in einlinien._ERSATZ.get(c, ""))}))
    xs = [x for l in linien for x, _y in l]
    ys = [y for l in linien for _x, y in l]
    if xs:
        # Bezugspunkt: Grundlinie der ersten Zeile, waagrecht je nach Ausrichtung
        # (links: linker Rand, rechts: rechter Rand, Mitte: Mitte). Beim Ändern
        # bleibt dieser Punkt stehen – wie in üblichen CAM-Programmen.
        x0, x1 = min(xs), max(xs)
        ax = {"links": x0, "rechts": x1}.get(q["ausrichtung"], (x0 + x1) / 2)
        linien = [[(x - ax, y) for x, y in l] for l in linien]
        b, h = x1 - x0, max(ys) - min(ys)
        box = [round(x0 - ax, 4), round(min(ys), 4), round(x1 - ax, 4), round(max(ys), 4)]
    else:
        b = h = 0.0
        box = [0.0, 0.0, 0.0, 0.0]
    flach = [[round(v, 4) for pt in l for v in pt] for l in linien]
    return {"ok": True, "schrift": q["schrift"], "linien": flach, "box": box, "breite": round(b, 3),
            "hoehe": round(h, 3), "fehlend": fehlend,
            "art": einlinien.schriften()[q["schrift"]].get("art", "einlinie")}


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    try:
        erg = setzen(json.loads(argv[0]) if argv else {})
    except Exception as ex:
        erg = {"ok": False, "fehler": str(ex), "linien": []}
    erg["schriften"] = schriftliste()
    sys.stdout.write(json.dumps(erg, ensure_ascii=False))
    return 0
