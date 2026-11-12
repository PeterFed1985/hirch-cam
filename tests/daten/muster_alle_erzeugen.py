"""Erzeugt tests/daten/muster_alle.hir – die Musterdatei mit jeder 2D-Bearbeitungsart (planung 04.11., Peter 20:08).

Von den Sitzungen selbst gezeichnet (keine fremden Formen, Schriften oder Modelle), darum frei verwendbar: Tests,
Website, später Veröffentlichung. Hartholz 200 × 150 × 20 mm, zweiseitig (wenden um X, Passlöcher in den Ecken),
Werkzeuge der Standardliste (A V 60°, B Ø 3, C Ø 6) und ein Gewindefräser.

Seite 1: Planen, Tasche in Runden mit Insel, Tasche adaptiv mit Insel, Bohren (Helix und Bohrzyklus), Gewinde M8
mit Kernloch, Gravur, V-Carve (spitz) und V-Carve mit Flachfräser (Mittellinie + Boden), Umspannen mit zwei
eingezeichneten Spannmitteln (Pratzen links und rechts); rechts unten der «Kiesel» der 3D-Sitzung
(tests/daten/muster_3d.py: 3D-Schruppen, Böden und Wände, 3D-Schlichten, beide Seiten, Haut und Haltestege).
Seite 2: Tasche, darin eine Kontur innen ganz durch mit Haltestegen, Kontur aussen ganz durch mit Haltestegen.

    python3 tests/daten/muster_alle_erzeugen.py      # schreibt tests/daten/muster_alle.hir
"""
import html
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))   # hirch_cam
from muster_3d import teil  # noqa: E402  (3D-Teil der 3D-Sitzung)

B, H, D = 200.0, 150.0, 20.0
S2 = B + 20.0                     # Seite 2: zweite Seite 20 mm rechts daneben, ausserhalb der Zeichenfläche
                                  # (wie «Seite 2 anlegen» im Programm: Zeichenfläche = Seite 1, Windows 05.11.)

FARBE = {"planen": "#808080", "tasche_r": "#00a000", "tasche_a": "#00c000", "tasche_2": "#008000",
         "kontur_i": "#0050ff", "kontur_a": "#0030c0", "bohren_h": "#8000c0", "bohren_z": "#a000e0",
         "gewinde": "#00aabe", "gravur": "#ff8c00", "vcarve": "#e00000", "vcarve_f": "#c00000"}

WERKZEUGE = {
    "A": {"typ": "v", "durchmesser": 12.0, "v_winkel": 60, "drehzahl": 18000, "vorschub": 1000, "eintauchen": 300,
          "nummer": 1, "name": "V-Fräser 60°", "schneidenlaenge": 11.0, "schneiden": 2, "mittenschnitt": True},
    "B": {"typ": "schaft", "durchmesser": 3.0, "drehzahl": 18000, "vorschub": 1000, "eintauchen": 300, "nummer": 2,
          "name": "Schaftfräser 3 mm", "schneidenlaenge": 12.0, "schneiden": 2, "mittenschnitt": True},
    "C": {"typ": "schaft", "durchmesser": 6.0, "drehzahl": 18000, "vorschub": 1000, "eintauchen": 300, "nummer": 3,
          "name": "Schaftfräser 6 mm", "schneidenlaenge": 22.0, "schneiden": 2, "mittenschnitt": True},
    "G": {"typ": "gewinde", "durchmesser": 5.0, "zaehne": 1, "steigung": 1.25, "drehzahl": 12000, "vorschub": 400,
          "eintauchen": 200, "nummer": 4, "name": "Gewindefräser M8", "schneidenlaenge": 12.0},
}

BEARBEITUNGEN = [
    {"id": "pl", "art": "planen", "name": "Planen", "werkzeug": "C", "tiefe": 0.5, "zustellung": 0.5},
    {"id": "bz", "art": "bohren", "name": "Bohren Zyklus", "werkzeug": "B", "tiefe": 6, "zustellung": 2,
     "methode": "bohren", "farbe": FARBE["bohren_z"]},
    {"id": "bh", "art": "bohren", "name": "Bohren Helix", "werkzeug": "C", "tiefe": 8, "zustellung": 2,
     "methode": "helix", "farbe": FARBE["bohren_h"]},
    {"id": "gw", "art": "gewinde", "name": "Gewinde M8", "werkzeug": "G", "tiefe": 10, "kernloch_werkzeug": "B",
     "innen": True, "farbe": FARBE["gewinde"]},
    {"id": "gr", "art": "gravur", "name": "Gravur", "werkzeug": "A", "tiefe": 0.5, "zustellung": 0.5,
     "farbe": FARBE["gravur"]},
    {"id": "vc", "art": "vcarve", "name": "V-Carve", "werkzeug": "A", "max_tiefe": 8.0, "farbe": FARBE["vcarve"]},
    {"id": "vf", "art": "vcarve", "name": "V-Carve mit Flachfräser", "werkzeug": "A", "max_tiefe": 2.5,
     "flach_werkzeug": "B", "farbe": FARBE["vcarve_f"]},
    {"id": "tr", "art": "tasche", "name": "Tasche Runden", "werkzeug": "C", "tiefe": 5, "zustellung": 2.5,
     "strategie": "runden", "farbe": FARBE["tasche_r"]},
    {"id": "ta", "art": "tasche", "name": "Tasche adaptiv", "werkzeug": "C", "tiefe": 6, "zustellung": 6,
     "strategie": "adaptiv", "farbe": FARBE["tasche_a"]},
    {"id": "ki", "art": "kontur", "name": "Kontur innen", "werkzeug": "C", "tiefe": None, "zustellung": 3,
     "seite": "innen", "stege_anzahl": 3, "stege_breite": 4, "stege_hoehe": 1.5, "auf_seite": 2,
     "farbe": FARBE["kontur_i"]},
    {"id": "t2", "art": "tasche", "name": "Tasche Seite 2", "werkzeug": "C", "tiefe": 4, "zustellung": 2,
     "strategie": "adaptiv", "auf_seite": 2, "farbe": FARBE["tasche_2"]},
    {"id": "ka", "art": "kontur", "name": "Kontur aussen", "werkzeug": "C", "tiefe": None, "zustellung": 4,
     "seite": "aussen", "stege_anzahl": 4, "stege_breite": 8, "stege_hoehe": 2, "anfahren": "bogen",
     "auf_seite": 2, "farbe": FARBE["kontur_a"]},
]

EINSTELLUNGEN = {
    "eingerichtet": True, "werkzeuge": WERKZEUGE, "bearbeitungen": BEARBEITUNGEN,
    "material": "hartholz", "reihenfolge": "auto",
    "zweiseitig": {"an": True, "achse": "x", "passloecher": True, "pass_d": 6.0, "pass_rand": 8.0,
                   "pass_werkzeug": "C", "pass_extra": 1.5, "pass_lage": "ecken"},
    "umspannen": {"an": True, "abstand": 10},
}


def _pfad(punkte, zu=True):
    d = "M " + " L ".join("%.3f,%.3f" % p for p in punkte)
    return d + (" Z" if zu else "")


def _kreis(cx, cy, r, n=72):
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]


def _stern(cx, cy, r1, r2, n=5):
    return [(cx + (r1 if i % 2 == 0 else r2) * math.sin(math.pi * i / n),
             cy - (r1 if i % 2 == 0 else r2) * math.cos(math.pi * i / n)) for i in range(2 * n)]


def _rund_rechteck(x0, y0, x1, y1, r, n=12):
    pts = []
    for cx, cy, a0 in ((x1 - r, y0 + r, -90), (x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180)):
        pts += [(cx + r * math.cos(math.radians(a0 + 90 * i / n)), cy + r * math.sin(math.radians(a0 + 90 * i / n)))
                for i in range(n + 1)]
    return pts


def elemente():
    el = []

    def p(farbe, pts, zu=True):
        el.append('<path d="%s" style="fill:none;stroke:%s;stroke-width:0.4"/>' % (_pfad(pts, zu), farbe))

    # Seite 1 (Ränder frei für Passlöcher bei 8 mm und die Pratzen links/rechts in der Mitte)
    p(FARBE["tasche_r"], [(20, 18), (80, 18), (80, 62), (20, 62)])                    # Tasche Runden …
    p(FARBE["tasche_r"], _kreis(50, 40, 8))                                           # … mit Insel
    p(FARBE["tasche_a"], [(95, 18), (180, 18), (180, 62), (95, 62)])                  # Tasche adaptiv …
    p(FARBE["tasche_a"], [(125, 32), (150, 32), (150, 48), (125, 48)])                # … mit Insel
    for x in (18, 45, 60):                              # Bohrzyklus Ø 3 (das linke liegt in der Zone der Pratze)
        p(FARBE["bohren_z"], _kreis(x, 80, 1.5, 36))
    for x in (95, 110):                                                               # Helix Ø 10
        p(FARBE["bohren_h"], _kreis(x, 80, 5))
    p(FARBE["gewinde"], _kreis(130, 80, 4))                                           # M8
    welle = [(20 + i, 132 + 4 * math.sin(i / 6.0)) for i in range(0, 121, 2)]       # Gravur
    p(FARBE["gravur"], welle, zu=False)
    p(FARBE["vcarve"], _stern(45, 107, 15, 6))                                        # V-Carve spitz
    p(FARBE["vcarve_f"], [(80, 92), (130, 92), (130, 122), (80, 122)])                # V-Carve flach: Ring
    p(FARBE["vcarve_f"], [(92, 100), (118, 100), (118, 114), (92, 114)])

    # Seite 2 (eigene Seite, Koordinaten + S2)
    def q(farbe, pts, zu=True):
        p(farbe, [(x + S2, y) for x, y in pts], zu)
    q(FARBE["tasche_2"], _rund_rechteck(60, 50, 140, 100, 10))
    q(FARBE["kontur_i"], _rund_rechteck(78, 62, 108, 88, 5))                          # Kontur innen (Durchbruch)
    q(FARBE["kontur_a"], _rund_rechteck(15, 15, 185, 135, 12))

    # Pratzen (Spannmittel, 12 mm hoch) links und rechts in der Mitte von Seite 1 – Umspannen
    for x0 in (-15, 190):
        el.append('<rect x="%g" y="65" width="25" height="20" hirch:spannmittel="12" '
                  'style="fill:#888888;fill-opacity:0.5;stroke:#444444;stroke-width:0.3"/>' % x0)
    return el


KIESEL = (144.0, 38.0)            # (3d 05.11.: Stege innerhalb der Kontur aussen) linke untere Ecke des 3D-Teils (Materialkoordinaten Seite 1, y nach oben)


def zeichnung() -> str:
    t = teil(KIESEL[0], KIESEL[1], (B, H, D))
    e = json.loads(json.dumps(EINSTELLUNGEN))
    e["werkzeuge"].update(t["werkzeuge"])
    e["bearbeitungen"] += t["bearbeitungen"]
    einst = html.escape(json.dumps(e, ensure_ascii=False), quote=True)
    seiten = ('<inkscape:page id="p1" x="0" y="0" width="%g" height="%g"/>'
              '<inkscape:page id="p2" x="%g" y="0" width="%g" height="%g"/>' % (B, H, S2, B, H))
    return ('<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
            'xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
            'xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd" '
            'xmlns:xlink="http://www.w3.org/1999/xlink" '
            'width="%gmm" height="%gmm" viewBox="0 0 %g %g" hirch:materialdicke="%g" hirch:einstellungen="%s">\n'
            '<sodipodi:namedview id="nv" inkscape:document-units="mm">%s</sodipodi:namedview>\n'
            '<defs>%s</defs>\n'
            '<g inkscape:groupmode="layer" inkscape:label="Muster">\n%s\n%s\n%s\n</g></svg>\n'
            % (B, H, B, H, D, einst, seiten, t["defs"], "\n".join(elemente()), t["bild"], t["stege"]))


if __name__ == "__main__":
    ziel = os.path.join(os.path.dirname(os.path.abspath(__file__)), "muster_alle.hir")
    with open(ziel, "w", encoding="utf-8") as f:
        f.write(zeichnung())
    print(ziel)
