# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Profil von Hirch CAM nach einem Update anpassen (läuft beim Start).

    python3 -m hirch_cam.profil PROFILORDNER [VERSION]

Inkscape schreibt beim ersten Start alle Vorgaben in preferences.xml; spätere
Änderungen an den Vorgaben kämen so nie an. Was sich an der Oberfläche
geändert hat, wird hier einmal nachgetragen (Stufe in .hirch-stufe). Alles
andere bleibt, wie der Anwender es eingestellt hat.
"""
from __future__ import annotations

import os
import sys
import xml.etree.ElementTree as ET

STUFE = 8

# Neue Formen starten schwarz (= ohne Bearbeitung), nicht in der zuletzt
# benutzten Fräsfarbe
NEUE_FORMEN = {("tools", "shapes", "rect"), ("tools", "shapes", "arc"), ("tools", "shapes", "star"),
               ("tools", "freehand", "pen"), ("tools", "freehand", "pencil"), ("tools", "text")}
SCHWARZ = "fill:none;stroke:#000000;stroke-width:0.5mm;"
EINRASTEN = {"snap-global-toggle": "1", "snap-path-intersection": "1", "snap-bbox-center": "1",
             "snap-bbox-edge-midpoint": "1", "snap-object-midpoint": "1", "snap-alignment": "1",
             "snap-distribution": "1", "simple-snap-alignment": "1", "snap-page-border": "1",
             "snap-grid": "0", "snap-grid-line": "0", "snap-path": "1", "snap-node-category": "1",
             "snap-bbox": "1"}
# Textwerkzeug: Einlinienschrift, nur Kontur (Einlinienschriften haben keine Fläche),
# 10 mm Schriftgrösse = 10 mm hohe Grossbuchstaben
TEXT_STIL = ("fill:none;stroke:#000000;stroke-width:0.5mm;stroke-linejoin:round;stroke-linecap:round;"
             "line-height:1.6;font-family:'Hershey Sans';font-style:normal;font-weight:normal;"
             "font-size:37.795276px;")

# Werkzeugleiste wie im Entwurf: nur diese Werkzeuge
WERKZEUGE = ["Select", "Node", "Text", "Rect", "Arc", "Star", "Pen", "Measure"]
ALLE = ["Select", "Node", "Booleans", "Rect", "Arc", "Star", "3DBox", "Spiral", "Pen", "Pencil",
        "Calligraphic", "Text", "Gradient", "Mesh", "Dropper", "PaintBucket", "Tweak", "Spray",
        "Eraser", "Connector", "LPETool", "Measure", "Zoom", "Pages"]


def _gruppe(eltern, gid):
    for c in eltern:                   # «group» oder «eventcontext»
        if c.get("id") == gid:
            return c
    return ET.SubElement(eltern, "group", {"id": gid})


def _stufe(ordner: str) -> int:
    try:
        with open(os.path.join(ordner, ".hirch-stufe"), encoding="utf-8") as fh:
            return int(fh.read().strip())
    except (OSError, ValueError):
        return 0


def pflegen(ordner: str, version: str = "") -> bool:
    """Gibt True zurück, wenn etwas angepasst wurde."""
    if _stufe(ordner) >= STUFE:
        return False
    alt = _stufe(ordner)
    pfad = os.path.join(ordner, "preferences.xml")
    if os.path.exists(pfad):
        baum = ET.parse(pfad)
        wurzel = baum.getroot()
        if alt < 4:
            # Werkzeugleiste wie im Entwurf. Ältere Profile haben die Gruppe
            # «toolbox» doppelt (Inkscape liest die erste): in allen setzen.
            leisten = [c for c in wurzel.findall("group") if c.get("id") == "toolbox"]
            for leiste in leisten or [_gruppe(wurzel, "toolbox")]:
                knopf = _gruppe(_gruppe(leiste, "tools"), "buttons")
                for t in ALLE:
                    if t in WERKZEUGE:
                        knopf.attrib.pop("show" + t, None)
                    else:
                        knopf.set("show" + t, "0")
        if alt < 2:
            fenster = _gruppe(wurzel, "window")
            for leiste in ("commands",):                   # Befehlsleiste aus (Einrastleiste: Stufe 7)
                _gruppe(fenster, leiste).set("state", "0")
        if alt < 5:
            for weg in NEUE_FORMEN:
                g = wurzel
                for teil in weg:
                    g = _gruppe(g, teil)
                g.set("usecurrent", "0")
                if weg[-1] != "text":
                    g.set("style", SCHWARZ)
        if alt < 7:
            # Einrasten (Materialkante, Pfade, Knoten, Schnitt- und Mittelpunkte,
            # Ausrichtungshilfen) mit sichtbarer Einrastleiste; Ausrichten am Material
            _gruppe(_gruppe(wurzel, "window"), "snaptoolbox").set("state", "1")
            einrasten = _gruppe(_gruppe(wurzel, "options"), "snapping")
            for k, v in EINRASTEN.items():
                einrasten.set(k, v)
            _gruppe(_gruppe(wurzel, "dialogs"), "align").set("objects-align-to", "page")
        if alt < 6:
            text = _gruppe(_gruppe(wurzel, "tools"), "text")
            text.set("usecurrent", "0")
            text.set("style", TEXT_STIL)
            _gruppe(_gruppe(wurzel, "options"), "font").set("unitType", "4")     # Schriftgrösse in mm
        if alt < 3:
            # Linienstärke beim Skalieren nicht mitskalieren (Fräslinien bleiben sichtbar)
            _gruppe(_gruppe(wurzel, "options"), "transform").set("stroke", "0")
        baum.write(pfad, encoding="UTF-8", xml_declaration=True)
    if alt < 8:
        # angedockte Dialoge neu: Fräsen-Panel und daneben der Reiter «Ansicht»
        try:
            os.unlink(os.path.join(ordner, "dialogs-state-ex.ini"))
        except OSError:
            pass
    os.makedirs(ordner, exist_ok=True)
    with open(os.path.join(ordner, ".hirch-stufe"), "w", encoding="utf-8") as fh:
        fh.write(str(STUFE))
    return True


def eine_spalte(ordner: str) -> bool:
    """Rechts gibt es nur eine Spalte (Peter 06.11.: Fenster erscheinen an Stelle von Fräsen; 09.11.: «zwei Spalten,
    das geht so nicht»). Ein älterer Fensterstand mit weiteren Spalten, mehreren Notebooks oder **schwebenden**
    Fenstern (Windows-Nacht a349d01: «Multiple dialogs», zweites Fräsen, Absturz beim Beenden) wird vor dem Start zu
    einer Spalte im Hauptfenster: alle Fenster einmal, Fräsen vorne. Gibt True zurück, wenn etwas geändert wurde."""
    import configparser
    pfad = os.path.join(ordner, "dialogs-state-ex.ini")
    if not os.path.exists(pfad):
        return False
    ini = configparser.RawConfigParser(strict=False, interpolation=None)
    ini.optionxform = str
    with open(pfad, encoding="utf-8") as fh:
        ini.read_file(fh)
    fenster = []
    i = 0
    while ini.has_section("Window%d" % i):
        fenster.append("Window%d" % i)
        i += 1
    if not fenster:
        return False

    def spalten(w):
        n = ini.getint(w, "ColumnCount", fallback=0)
        return ["%sColumn%d" % (w, k) for k in range(n) if ini.has_section("%sColumn%d" % (w, k))]

    def dialoge(c):
        out = []
        for k in range(ini.getint(c, "NotebookCount", fallback=0)):
            out += [d for d in ini.get(c, "Notebook%dDialogs" % k, fallback="").split(";") if d]
        return out

    schwebend = [w for w in fenster if ini.get(w, "Floating", fallback="false") == "true"]
    angedockt = [w for w in fenster if w not in schwebend]
    haupt = angedockt[0] if angedockt else None
    sp = spalten(haupt) if haupt else []
    sauber = (len(fenster) == 1 and haupt is not None and len(sp) == 1
              and ini.getint(sp[0], "NotebookCount", fallback=1) == 1
              and ini.get(sp[0], "BeforeCanvas", fallback="false") != "true")
    if sauber:
        return False
    # Ziel: die Spalte des Hauptfensters mit Fräsen (sonst die erste); ihre Höhe und Breite bleiben
    alle_sp = [c for w in angedockt for c in spalten(w)]
    ziel = next((c for c in alle_sp if "Fraesen" in dialoge(c)), alle_sp[0] if alle_sp else None)
    reihe = ([ziel] if ziel else []) + [c for c in alle_sp if c != ziel] + [c for w in schwebend for c in spalten(w)]
    alle = []
    for c in reihe:
        alle += [d for d in dialoge(c) if d not in alle]
    if "Fraesen" in alle:
        alle.remove("Fraesen")
    alle.insert(0, "Fraesen")
    hoehe = ini.get(ziel, "Notebook0Height", fallback="800") if ziel else "800"
    breite = ini.get(ziel, "ColumnWidth", fallback="480") if ziel else "480"
    for w in fenster:
        for c in spalten(w) + ["%sColumn%d" % (w, k) for k in range(20)]:
            if ini.has_section(c):
                ini.remove_section(c)
        ini.remove_section(w)
    ini.add_section("Window0")
    ini.set("Window0", "ColumnCount", "1")
    ini.set("Window0", "Floating", "false")
    ini.add_section("Window0Column0")
    for k, v in {"Notebook0Dialogs": ";".join(alle) + ";", "Notebook0Height": hoehe, "Notebook0ActiveTab": "0",
                 "ColumnWidth": breite, "NotebookCount": "1", "BeforeCanvas": "false"}.items():
        ini.set("Window0Column0", k, v)
    if not ini.has_section("Windows"):
        ini.add_section("Windows")
    ini.set("Windows", "Count", "1")
    with open(pfad, "w", encoding="utf-8") as fh:
        ini.write(fh, space_around_delimiters=False)
    return True


if __name__ == "__main__":
    try:
        # Umzug Stufe 2: zuerst den alten Einstellungsordner (hirch-cnc) kopieren – sonst legte pflegen() den neuen
        # Profilordner leer an
        from . import orte
        orte.umzug()
    except Exception as e:
        print("Hirch CAM: Einstellungen nicht umgezogen:", e, file=sys.stderr)
    try:
        pflegen(sys.argv[1])
    except Exception as e:  # Start nie verhindern
        print("Hirch CAM: Profil nicht angepasst:", e, file=sys.stderr)
    try:
        eine_spalte(sys.argv[1])
    except Exception as e:
        print("Hirch CAM: Fensterstand nicht angepasst:", e, file=sys.stderr)
    try:
        # Einlinienschriften fürs Textwerkzeug bereitstellen (nur Geänderte neu bauen)
        os.environ.setdefault("HIRCH_SCHRIFT_TTF", os.path.join(sys.argv[1], "fonts"))
        from . import schrift_ttf
        schrift_ttf.abgleichen()
    except Exception as e:
        print("Hirch CAM: Schriften nicht bereitgestellt:", e, file=sys.stderr)
