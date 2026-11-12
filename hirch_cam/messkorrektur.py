# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Messkorrektur: ein gefrästes Testteil (Quadrat aussen, Loch innen) in X und Y ausmessen.

Sammelauftrag Peter, 21.10.2026, Punkt 3. Je Achse (Sollmasse Q und D, gemessen Qm und Dm):

    s = (Qm + Dm) / (Q + D)          Weg der Achse (1 = stimmt, < 1 = fährt zu kurz)
    e = (Dm − D · s) / 2             Fräser je Seite (> 0: schneidet grösser als angegeben)

Das Quadrat wird um 2e kleiner, das Loch um 2e grösser – eine falsche Skalierung macht beide im
selben Verhältnis grösser oder kleiner. Daraus die zwei Gleichungen oben.

Abgelegt wird e (Mittel aus X und Y) beim Fräser als «messkorrektur» (mm je Seite): Hirch CAM rechnet
an Konturen, Taschenwänden und gefrästen Löchern dieses Fräsers mit einem Aufmass e – als wäre er
Ø + 2e gross. s_X und s_Y sind ein Hinweis für die Steuerung (Schritte pro mm); wahlweise gleicht der
G-Code sie aus (Maschine: «skalierung_an», ab Werk aus).

War beim Fräsen des Testteils schon eine Korrektur wirksam (beim Fräser oder die alte für alle Fräser),
steckt sie im Messwert: neu = bisher wirksam + gemessen.
"""
from __future__ import annotations

import datetime
import json
import math
import os
import sys
from typing import Dict, List, Optional

GRENZE_SKALIERUNG = 0.01        # 1 %: grösser ist wohl ein Tippfehler
GRENZE_E = 0.3                  # mm je Seite
GRENZE_SPIEL = 0.10             # mm Unterschied Loch X/Y
GRENZE_RUND = 0.05              # mm Diagonale neben dem Mittel


def _zahl(v: float, n: int = 2) -> str:
    return ("%.*f" % (n, v)).replace(".", ",").replace("-", "−")


def rechnen(q: float, d: float, qx: float, qy: float, lx: float, ly: float,
            diagonal: Optional[float] = None, bisher: float = 0.0) -> Dict:
    """Skalierung und Fräser-Zugabe je Achse, Mittel, neue Korrektur und Warnungen."""
    if q <= 0 or d <= 0 or min(qx, qy, lx, ly) <= 0:
        raise ValueError("Sollmasse und Messwerte müssen grösser als 0 sein")
    sx = (qx + lx) / (q + d)
    sy = (qy + ly) / (q + d)
    ex = (lx - d * sx) / 2
    ey = (ly - d * sy) / 2
    e = (ex + ey) / 2
    warnungen: List[str] = []
    if abs(lx - ly) > GRENZE_SPIEL:
        warnungen.append("spiel")
    if abs(sx - 1) > GRENZE_SKALIERUNG or abs(sy - 1) > GRENZE_SKALIERUNG or abs(ex) > GRENZE_E or abs(ey) > GRENZE_E:
        warnungen.append("tippfehler")
    if diagonal and diagonal > 0 and abs(diagonal - (lx + ly) / 2) > GRENZE_RUND:
        warnungen.append("unrund")
    return {"s_x": sx, "s_y": sy, "e_x": ex, "e_y": ey, "e": e,
            "neu": max(-1.0, min(1.0, bisher + e)), "warnungen": warnungen}


def warnung_text(art: str, lx: float = 0, ly: float = 0, diagonal: float = 0) -> str:
    if art == "spiel":
        return ("Loch in X und Y %s mm verschieden – Spiel oder Umkehrspiel in einer Achse? Riemen, Spindelmutter "
                "und Lager prüfen, bevor du korrigierst." % _zahl(abs(lx - ly)))
    if art == "tippfehler":
        return "Unwahrscheinlich grosse Abweichung – wohl ein Tippfehler. Bitte die Messwerte prüfen."
    return ("Diagonal %s mm, X und Y im Mittel %s mm – das Loch ist nicht rund (Spiel oder der Fräser schwingt)."
            % (_zahl(diagonal), _zahl((lx + ly) / 2)))


def achse_hinweis(achse: str, s: float) -> str:
    """Hinweis für die Steuerung: Schritte pro mm × 1/s; Impulsäquivalent (mm je Schritt) × s."""
    prozent = abs(s - 1) * 100
    return ("Achse %s fährt %s %% %s: Schritte pro mm in der Steuerung × %s (Beispiel 400 → %s). "
            "RichAuto/NcStudio «Impulsäquivalent»: × %s." % (
                achse, _zahl(prozent), "zu kurz" if s < 1 else "zu weit", _zahl(1 / s, 4), _zahl(400 / s, 1),
                _zahl(s, 4)))


def speichern(auftrag: Dict, datei: Optional[str] = None) -> Dict:
    """Ergebnis ablegen. auftrag: {"id": Fräser in «Meine Werkzeuge» (oder leer), "messkorrektur": e_neu,
    "info": {...}, "s_x", "s_y", "skalierung_an": bool, "alte_null": bool}."""
    from . import werkzeugdb
    db = werkzeugdb.laden(datei)
    wid = auftrag.get("id") or ""
    gespeichert = None
    if wid:
        for w in db["werkzeuge"]:
            if w.get("id") == wid:
                w = dict(w)
                e = float(auftrag.get("messkorrektur") or 0.0)
                if abs(e) < 1e-9:
                    w.pop("messkorrektur", None)
                    w.pop("messkorrektur_info", None)
                else:
                    w["messkorrektur"] = round(e, 4)
                    w["messkorrektur_info"] = auftrag.get("info") or {}
                gespeichert = werkzeugdb.speichern(w, datei)
                break
    ma = {}
    if "skalierung_an" in auftrag:
        ma["skalierung_an"] = 1.0 if auftrag["skalierung_an"] else 0.0
        if auftrag.get("s_x"):
            ma["skalierung_x"] = round(float(auftrag["s_x"]), 6)
        if auftrag.get("s_y"):
            ma["skalierung_y"] = round(float(auftrag["s_y"]), 6)
    if auftrag.get("alte_null"):
        ma["masskorrektur"] = 0.0
    if ma:
        werkzeugdb.maschine_setzen(ma, datei)
    return {"ok": True, "werkzeug": gespeichert}


def testteil_svg(q: float, d: float, werkzeug: Dict, material: str = "hartholz", dicke: float = 18.0,
                 tiefe: float = 3.0) -> str:
    """Neue Zeichnung: Quadrat q (Kontur aussen) mit Loch Ø d (Kontur innen), beide mit dem Fräser, q/2 Rand."""
    from . import einstellungen as einst
    e = einst.laden(None)
    w = dict(werkzeug)
    w.setdefault("nummer", 1)
    e["werkzeuge"] = {"A": w}
    e["bearbeitungen"] = []
    e["material"] = material
    e["eingerichtet"] = True
    zust = round(min(tiefe, max(0.5, float(w.get("durchmesser") or 3) / 2)), 2)
    aussen = einst.neue_bearbeitung(e, "kontur")
    aussen.update(werkzeug="A", tiefe=tiefe, zustellung=zust, seite="aussen", titel="Testquadrat aussen",
                  stege_anzahl=0)
    e["bearbeitungen"].append(aussen)
    innen = einst.neue_bearbeitung(e, "kontur")
    innen.update(werkzeug="A", tiefe=tiefe, zustellung=zust, seite="innen", titel="Testloch innen", stege_anzahl=0)
    e["bearbeitungen"].append(innen)
    rand = max(15.0, q / 2)
    b = h = q + 2 * rand
    mx, my = b / 2, h / 2
    z = lambda v: ("%.4f" % v).rstrip("0").rstrip(".")
    quadrat = "M %s,%s H %s V %s H %s Z" % (z(mx - q / 2), z(my - q / 2), z(mx + q / 2), z(my + q / 2), z(mx - q / 2))
    attr = json.dumps(e, ensure_ascii=False).replace("&", "&amp;").replace('"', "&quot;").replace("<", "&lt;")
    return ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc"\n'
            '     xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape"\n'
            '     xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"\n'
            '     width="%smm" height="%smm" viewBox="0 0 %s %s" hirch:materialdicke="%s" hirch:einstellungen="%s">\n'
            '<title>Testteil Messkorrektur</title>\n'
            '<sodipodi:namedview id="namedview1" inkscape:document-units="mm">'
            '<inkscape:page x="0" y="0" width="%s" height="%s" id="page1"/></sodipodi:namedview>\n'
            '<g inkscape:groupmode="layer" id="ebene1" inkscape:label="Testteil">\n'
            '<path id="testquadrat" d="%s" style="fill:none;stroke:%s;stroke-width:0.3"/>\n'
            '<circle id="testloch" cx="%s" cy="%s" r="%s" style="fill:none;stroke:%s;stroke-width:0.3"/>\n'
            '</g>\n</svg>\n' % (z(b), z(h), z(b), z(h), z(dicke), attr, z(b), z(h),
                                quadrat, aussen["farbe"], z(mx), z(my), z(d / 2), innen["farbe"]))


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    befehl = argv[0] if argv else ""
    try:
        a = json.loads(argv[1]) if len(argv) > 1 else {}
        if befehl == "rechnen":
            erg = dict(rechnen(a["q"], a["d"], a["qx"], a["qy"], a["lx"], a["ly"], a.get("diagonal"),
                               a.get("bisher", 0.0)), ok=True)
        elif befehl == "speichern":
            erg = speichern(a)
        elif befehl == "testteil":
            from .orte import konfig
            ordner = os.path.join(konfig(), "testteile")
            os.makedirs(ordner, exist_ok=True)
            pfad = os.path.join(ordner, "Testteil %g x %g Loch %g.hir" % (a["q"], a["q"], a["d"]))
            with open(pfad, "w", encoding="utf-8") as f:
                f.write(testteil_svg(a["q"], a["d"], a["werkzeug"], a.get("material", "hartholz"),
                                     float(a.get("dicke") or 18), float(a.get("tiefe") or 3)))
            erg = {"ok": True, "datei": pfad}
        else:
            erg = {"ok": False, "fehler": "unbekannter Befehl"}
    except Exception as ex:                     # noqa: BLE001 – Meldung an den Dialog
        erg = {"ok": False, "fehler": str(ex)}
    sys.stdout.write(json.dumps(erg, ensure_ascii=False))
    return 0


def heute() -> str:
    return datetime.date.today().isoformat()


if __name__ == "__main__":
    sys.exit(main())
