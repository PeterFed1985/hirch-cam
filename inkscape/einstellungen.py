#!/usr/bin/env python3
"""Passt die Inkscape-Einstellungen des Hirch-CNC-Profils an.

Aufruf (macht hirch-cam.sh bei jedem Start):
    python3 einstellungen.py PREFERENCES.XML INKSCAPE-VERSION [--erststart]

Immer:       Willkommensbildschirm aus (auch nach einem Inkscape-Update,
             darum wird die angezeigte Version auf die aktuelle gesetzt).
--erststart: zusätzlich die Palette "Hirch CNC Fräsfarben" einstellen.
Alle anderen Einstellungen bleiben unverändert.
"""
import sys
import xml.etree.ElementTree as ET

PALETTE = "Hirch CNC Fräsfarben"


def gruppe(eltern, gid):
    for c in eltern.findall("group"):
        if c.get("id") == gid:
            return c
    return ET.SubElement(eltern, "group", {"id": gid})


def main(argv):
    pfad, version = argv[0], argv[1]
    erststart = "--erststart" in argv
    baum = ET.parse(pfad)
    wurzel = baum.getroot()
    geaendert = False

    boot = gruppe(gruppe(wurzel, "options"), "boot")
    shown = gruppe(boot, "shown")
    if boot.get("enabled") != "0" or shown.get("ver") != version:
        boot.set("enabled", "0")
        shown.set("ver", version)
        geaendert = True

    if erststart:
        gruppe(gruppe(wurzel, "embedded"), "swatches").set("palette", PALETTE)
        gruppe(gruppe(wurzel, "dialogs"), "swatches").set("palette", PALETTE)
        geaendert = True

    if geaendert:
        baum.write(pfad, encoding="UTF-8", xml_declaration=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except Exception as e:  # Start von Inkscape nie verhindern
        print("Hirch CNC: Einstellungen nicht angepasst:", e, file=sys.stderr)
        sys.exit(0)
