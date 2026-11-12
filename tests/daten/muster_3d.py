"""3D-Teil der Musterdatei (planung 04.11., Peter 20:08) – ein eigenes kleines Modell, von der 3D-Sitzung selbst
erzeugt (keine fremden Modelle), darum frei verwendbar.

Der «Kiesel»: in der Draufsicht ein gerundetes Rechteck 36 × 30 mm, oben eine Kuppe (7 mm über der Trennlinie), unten
eine flachere Kuppe (5 mm), rundherum ein senkrechter Rand. Obenauf ein Absatz mit ebener Fläche und senkrechten
Wänden (12 × 8 mm, 9 mm hoch) – für «Böden und Wände». Zweiseitig mit Haut und zwei Haltestegen. Geschruppt wird
9 mm um das Modell (Rand 6 + Radius), damit es den 2D-Teilen daneben nicht in die Quere kommt.

Platz in der Musterdatei (200 × 150, Seite 1, y nach oben): linke untere Ecke bei (144, 38) – rechts unten, wo bisher
die Kontur innen lag (fraesbahnen 05.11.: bei (144, 30) kreuzte der untere Steg auf Seite 2 die Kontur aussen). Der
Fräsbereich reicht dann von x 135 bis 189 und y 29 bis 77, die Stege bis y 23 und 83 – innerhalb der Kontur aussen
(15 … 135) auf beiden Seiten.

Gebraucht von tests/daten/muster_alle_erzeugen.py (fraesbahnen):

    from muster_3d import teil
    t = teil(x0, y0, material=(200, 150, 20))
    t["defs"], t["bild"], t["stege"]          # SVG-Text: in <defs>, Bild des Modells, Haltestege (Seite 1)
    t["werkzeuge"], t["bearbeitungen"]         # in die Einstellungen übernehmen
"""
import base64
import json
import math
import os
import tempfile
from xml.sax.saxutils import quoteattr

import numpy as np

BREITE, HOEHE, ECKE = 36.0, 30.0, 9.0          # Draufsicht (mm)


def _umriss(n=144):
    """Gerundetes Rechteck um (0, 0), n Punkte gegen den Uhrzeigersinn, gleichmässig nach Länge."""
    b, h, r = BREITE / 2, HOEHE / 2, ECKE
    pts = []
    for cx, cy, a0 in ((b - r, -h + r, -90), (b - r, h - r, 0), (-b + r, h - r, 90), (-b + r, -h + r, 180)):
        pts += [(cx + r * math.cos(math.radians(a0 + 90 * i / 24)), cy + r * math.sin(math.radians(a0 + 90 * i / 24)))
                for i in range(25)]
    p = np.array(pts)
    s = np.concatenate([[0.0], np.cumsum(np.hypot(*np.diff(np.vstack([p, p[:1]]), axis=0).T))])
    t = np.linspace(0, s[-1], n, endpoint=False)
    q = np.vstack([p, p[:1]])
    return np.column_stack([np.interp(t, s, q[:, 0]), np.interp(t, s, q[:, 1])])


def _rund_rechteck(b, h, r, n=12):
    pts = []
    for cx, cy, a0 in ((b / 2 - r, -h / 2 + r, -90), (b / 2 - r, h / 2 - r, 0), (-b / 2 + r, h / 2 - r, 90),
                       (-b / 2 + r, -h / 2 + r, 180)):
        pts += [(cx + r * math.cos(math.radians(a0 + 90 * i / n)), cy + r * math.sin(math.radians(a0 + 90 * i / n)))
                for i in range(n + 1)]
    return np.array(pts)


def dreiecke(ringe=30) -> np.ndarray:
    """Geschlossener Körper (Dreiecke, mm, Trennlinie Z = 0)."""
    rand = _umriss()
    n = len(rand)
    s = np.linspace(0.0, 1.0, ringe + 1)[1:]

    def oben(f):
        return 2.0 + 5.0 * (1.0 - f * f)

    def unten(f):
        return -(2.0 + 3.0 * (1.0 - f * f))
    tri = []
    for z_von, vz in ((oben, 1), (unten, -1)):
        mitte = (0.0, 0.0, z_von(0.0))
        r0 = [(s[0] * x, s[0] * y, z_von(s[0])) for x, y in rand]
        for i in range(n):
            a, b = r0[i], r0[(i + 1) % n]
            tri.append((mitte, a, b) if vz > 0 else (mitte, b, a))
        for k in range(1, len(s)):
            ra = [(s[k - 1] * x, s[k - 1] * y, z_von(s[k - 1])) for x, y in rand]
            rb = [(s[k] * x, s[k] * y, z_von(s[k])) for x, y in rand]
            for i in range(n):
                j = (i + 1) % n
                q = (ra[i], rb[i], rb[j], ra[j])
                if vz > 0:
                    tri += [(q[0], q[1], q[2]), (q[0], q[2], q[3])]
                else:
                    tri += [(q[0], q[2], q[1]), (q[0], q[3], q[2])]
    for i in range(n):                                   # senkrechter Rand
        j = (i + 1) % n
        (x1, y1), (x2, y2) = rand[i], rand[j]
        a, b, c, d = (x1, y1, unten(1.0)), (x2, y2, unten(1.0)), (x2, y2, oben(1.0)), (x1, y1, oben(1.0))
        tri += [(a, b, c), (a, c, d)]
    # Absatz: gerundeter Quader 12 × 8 von Z 0 bis 9 (ragt aus der Kuppe, ebene Fläche, senkrechte Wände)
    ab = _rund_rechteck(12.0, 8.0, 2.0)
    m = len(ab)
    z0, z1 = 0.0, 9.0
    for i in range(m):
        j = (i + 1) % m
        (x1, y1), (x2, y2) = ab[i], ab[j]
        tri += [((0, 0, z1), (x1, y1, z1), (x2, y2, z1)), ((0, 0, z0), (x2, y2, z0), (x1, y1, z0)),
                ((x1, y1, z0), (x2, y2, z0), (x2, y2, z1)), ((x1, y1, z0), (x2, y2, z1), (x1, y1, z1))]
    return np.array(tri, np.float32)


def _stl(pfad, d):
    with open(pfad, "wb") as f:
        f.write(b"Hirch CAM Muster Kiesel".ljust(80, b" "))
        f.write(np.uint32(len(d)).tobytes())
        a = np.zeros(len(d), dtype=[("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")])
        a["v"] = d
        f.write(a.tobytes())


def _steg(sid, mitte, breite, laenge, winkel, H):
    w = json.dumps({"relief": "kiesel", "z": None})
    return (f'<rect id="{sid}" x="{-breite / 2}" y="{-laenge / 2}" width="{breite}" height="{laenge}" '
            f'transform="translate({mitte[0]:.3f},{H - mitte[1]:.3f}) rotate({-winkel})" '
            f'style="fill:#ffd21a;fill-opacity:0.6" hirch:steg3d={quoteattr(w)}/>')


FARBE_S, FARBE_F, FARBE_B = "#e66100", "#1c71d8", "#2ec27e"

WERKZEUGE = {"K": {"typ": "kugel", "durchmesser": 6.0, "drehzahl": 18000, "vorschub": 1500, "eintauchen": 400,
                   "nummer": 5, "name": "Kugelfräser 6 mm", "schneidenlaenge": 20.0, "schneiden": 2,
                   "mittenschnitt": True}}

BEARBEITUNGEN = [
    {"id": "s3a", "art": "relief_schruppen", "name": "3D-Schruppen Seite 1", "werkzeug": "C", "zustellung": 3,
     "aufmass": 0.5, "rand": 6, "auf_seite": 1, "farbe": FARBE_S},
    {"id": "bw", "art": "boeden_waende", "name": "Böden und Wände", "werkzeug": "C", "stufe": 6,
     "im_wechsel_mit": "s3a", "auf_seite": 1, "farbe": FARBE_B},
    {"id": "f3a", "art": "relief_schlichten", "name": "3D-Schlichten Seite 1", "werkzeug": "K", "bahnabstand": 12,
     "strategie": "konturen", "auslassen_fertig": True, "auf_seite": 1, "farbe": FARBE_F},
    {"id": "s3b", "art": "relief_schruppen", "name": "3D-Schruppen Seite 2", "werkzeug": "C", "zustellung": 3,
     "aufmass": 0.5, "rand": 6, "auf_seite": 2, "farbe": FARBE_S},
    {"id": "f3b", "art": "relief_schlichten", "name": "3D-Schlichten Seite 2", "werkzeug": "K", "bahnabstand": 12,
     "strategie": "parallel", "richtung": "x", "auf_seite": 2, "farbe": FARBE_F},
]


def teil(x0: float, y0: float, material=(200.0, 150.0, 20.0)) -> dict:
    """Der Kiesel mit seiner linken unteren Ecke bei (x0, y0) mm (Materialkoordinaten von Seite 1, y nach oben)."""
    from hirch_cam import relief
    B, H, D = material
    with tempfile.TemporaryDirectory() as t:
        stl = os.path.join(t, "kiesel.stl")
        _stl(stl, dreiecke())
        lage = {"art": "mittig"}
        erg = relief.einbetten(stl, t, material=list(material), lage=lage)
        daten = open(erg["dateien"]["daten"]).read()
        png = base64.b64encode(open(erg["dateien"]["bild"], "rb").read()).decode()
    bl = erg["bild_lage"]["mm"]
    info = {"daten": "kieseldaten", "name": "kiesel.stl", "art": erg["art"], "ausrichtung": erg["ausrichtung"],
            "lage": lage, "bild_lage": bl, "masse": erg["masse"],
            "zweiseitig": {"an": True, "haut_an": True, "haut": 0.5}}
    w, h = bl[2], bl[3]
    x, y = x0, H - y0 - h                               # Zeichnung: y nach unten
    mx, my = x0 + w / 2, y0 + h / 2
    return {
        "defs": f'<hirch:reliefdaten id="kieseldaten" hirch:name="kiesel.stl">{daten}</hirch:reliefdaten>',
        "bild": (f'<image id="kiesel" x="{x:.3f}" y="{y:.3f}" width="{w}" height="{h}" preserveAspectRatio="none" '
                 f'xlink:href="data:image/png;base64,{png}" hirch:relief={quoteattr(json.dumps(info))}/>'),
        # zwei Haltestege oben und unten, 8 mm breit, 20 mm lang (5 mm ins Modell, 15 mm hinaus – über den
        # Fräsbereich von 9 mm hinaus)
        "stege": (_steg("kiesel_steg1", (mx, my + HOEHE / 2 + 5), 8, 20, 0, H) +
                  _steg("kiesel_steg2", (mx, my - HOEHE / 2 - 5), 8, 20, 0, H)),
        "werkzeuge": WERKZEUGE, "bearbeitungen": BEARBEITUNGEN, "groesse": (w, h),
    }
