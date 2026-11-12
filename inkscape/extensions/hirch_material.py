#!/usr/bin/env python3
"""Inkscape-Erweiterung "Hirch CNC › Material einrichten".

Setzt die Seitengrösse auf die Materialgrösse (1 Einheit = 1 mm) und
speichert die Materialdicke in der Zeichnung (Attribut hirch:materialdicke).
"""
import inkex

NS = "https://hirch.net/cnc"
inkex.addNS  # noqa: B018  (sicherstellen, dass inkex geladen ist)


def materialdicke(svg, standard=18.0):
    """Materialdicke aus der Zeichnung lesen (mm)."""
    wert = svg.get("{%s}materialdicke" % NS)
    try:
        return float(wert) if wert is not None else standard
    except ValueError:
        return standard


class Material(inkex.EffectExtension):
    def add_arguments(self, pars):
        pars.add_argument("--breite", type=float, default=300.0)
        pars.add_argument("--hoehe", type=float, default=200.0)
        pars.add_argument("--dicke", type=float, default=18.0)
        pars.add_argument("--verschieben", type=inkex.Boolean, default=True)

    def effect(self):
        o = self.options
        svg = self.svg
        uu = svg.unittouu("1mm")
        alt_b, alt_h = svg.viewbox_width / uu, svg.viewbox_height / uu
        # Inhalt in mm-Einheiten bringen, falls die Zeichnung andere Einheiten hatte
        if abs(uu - 1.0) > 1e-9:
            for kind in list(svg):
                if isinstance(kind, inkex.ShapeElement):
                    kind.transform = inkex.Transform(scale=1 / uu) @ kind.transform
        svg.set("width", "%gmm" % o.breite)
        svg.set("height", "%gmm" % o.hoehe)
        svg.set("viewBox", "0 0 %g %g" % (o.breite, o.hoehe))
        svg.set("{%s}materialdicke" % NS, "%g" % o.dicke)
        nv = svg.namedview
        nv.set(inkex.addNS("document-units", "inkscape"), "mm")
        nv.set("units", "mm")
        for page in nv.findall(inkex.addNS("page", "inkscape")):
            page.set("x", "0")
            page.set("y", "0")
            page.set("width", "%g" % o.breite)
            page.set("height", "%g" % o.hoehe)
            break
        if o.verschieben:
            dx = (o.breite - alt_b) / 2
            dy = (o.hoehe - alt_h) / 2
            if abs(dx) > 1e-9 or abs(dy) > 1e-9:
                for kind in list(svg):
                    if isinstance(kind, inkex.ShapeElement) and not isinstance(kind, inkex.NamedView):
                        kind.transform = inkex.Transform(translate=(dx, dy)) @ kind.transform


if __name__ == "__main__":
    Material().run()
