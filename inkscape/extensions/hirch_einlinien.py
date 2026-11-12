#!/usr/bin/env python3
"""Inkscape-Erweiterung "Hirch CNC › Einlinienschrift".

Setzt Text als Einlinienschrift (Mittellinien) in die Zeichnung, orange
eingefärbt (= Gravur). Die Strichbreite entspricht der Breite der
gefrästen Rille. Platziert wird in der Mitte der Auswahl oder, ohne
Auswahl, in der Mitte der Seite.
"""
import math
import os
import sys

_HIER = os.path.dirname(os.path.realpath(__file__))
_REPO = os.environ.get("HIRCH_CAM_REPO") or os.path.dirname(os.path.dirname(_HIER))
if _REPO not in sys.path:
    sys.path.insert(0, _REPO)

import inkex  # noqa: E402

from hirch_cam.einlinien import text_linien  # noqa: E402

ORANGE = "#ff8c00"


class Einlinien(inkex.EffectExtension):
    def add_arguments(self, pars):
        a = pars.add_argument
        a("--reiter")
        a("--text", default="Hirch CNC")
        a("--schrift", default="Hershey Sans")
        a("--versalhoehe", type=float, default=20.0)
        a("--ausrichtung", default="mitte")
        a("--zeilenabstand", type=float, default=1.7)
        a("--linien", type=int, default=1)
        a("--linienabstand", type=float, default=0.8)
        a("--optisch", default="optisch")
        a("--luft", type=int, default=22)
        a("--fraeser", default="v")
        a("--winkel", type=float, default=60.0)
        a("--tiefe", type=float, default=0.5)
        a("--durchmesser", type=float, default=1.0)

    def effect(self):
        o = self.options
        text = o.text.replace("\\n", "\n").strip("\n")
        if not text.strip():
            self.msg("Kein Text eingegeben.")
            return
        if o.fraeser == "v":
            breite = 2 * o.tiefe * math.tan(math.radians(o.winkel / 2))
        else:
            breite = o.durchmesser
        linien = text_linien(
            text, o.schrift, o.versalhoehe, o.zeilenabstand, o.optisch == "optisch",
            o.luft / 100.0, breite, o.linien, o.linienabstand, o.ausrichtung)
        if not linien:
            self.msg("Der Text enthält keine darstellbaren Zeichen.")
            return

        uu = self.svg.unittouu("1mm")
        xs = [p[0] for l in linien for p in l]
        ys = [p[1] for l in linien for p in l]
        mx, my = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2  # mm, y nach oben

        # Ziel: Mitte der Auswahl oder der Seite (in Benutzereinheiten, y nach unten)
        if self.svg.selection:
            bb = self.svg.selection.bounding_box()
            zx, zy = bb.center_x, bb.center_y
        else:
            zx, zy = self.svg.viewbox_width / 2, self.svg.viewbox_height / 2

        d = []
        for l in linien:
            pts = [(zx + (x - mx) * uu, zy - (y - my) * uu) for x, y in l]
            d.append("M " + " L ".join("%.4f,%.4f" % p for p in pts))
        gruppe = inkex.Group()
        kurz = text.replace("\n", " / ")
        gruppe.label = "Einlinienschrift: " + (kurz[:40] + "…" if len(kurz) > 40 else kurz)
        pfad = inkex.PathElement()
        pfad.set("d", " ".join(d))
        pfad.style = inkex.Style({
            "fill": "none", "stroke": ORANGE, "stroke-width": "%.4f" % (breite * uu),
            "stroke-linecap": "round", "stroke-linejoin": "round"})
        pfad.label = "Gravur"
        gruppe.append(pfad)
        ebene = self.svg.get_current_layer()
        # Koordinaten oben sind Dokument-Koordinaten; eine verschobene Ebene
        # (z. B. nach «Material einrichten») herausrechnen
        gruppe.transform = -ebene.composed_transform()
        ebene.append(gruppe)


if __name__ == "__main__":
    Einlinien().run()
