import json
import math
import os

import pytest

shapely = pytest.importorskip("shapely")
from shapely.geometry import LineString, Point, Polygon  # noqa: E402

from hirch_cam import engine  # noqa: E402
from hirch_cam.model import Job  # noqa: E402
from hirch_cam.ops.bahn import flaeche, verschachtelung  # noqa: E402
from hirch_cam.ops.bohren import bohren  # noqa: E402
from hirch_cam.ops.kontur import kontur  # noqa: E402
from hirch_cam.ops.tasche import tasche  # noqa: E402
from hirch_cam.post import tap_mm  # noqa: E402

QUADRAT = [(0, 0), (100, 0), (100, 60), (0, 60), (0, 0)]
LOCH = [(40, 20), (60, 20), (60, 40), (40, 40), (40, 20)]


def _feed_ringe(tp, z):
    """Vorschubpunkte auf einer bestimmten Tiefe."""
    return [(m.x, m.y) for m in tp.moves if not m.rapid and abs(m.z - z) < 1e-9]


def _keine_tiefen_eilgaenge(tp, cz=3.0, start=(0, 0, 20)):
    p = start
    for m in tp.moves:
        if m.rapid and math.hypot(m.x - p[0], m.y - p[1]) > 1e-9:
            assert min(p[2], m.z) >= cz - 1e-9, (p, m)
        p = (m.x, m.y, m.z)


def test_verschachtelung():
    innen_teil = [(45, 25), (55, 25), (55, 35), (45, 35)]
    assert verschachtelung([QUADRAT[:-1], LOCH[:-1], innen_teil]) == [0, 1, 2]


def test_kontur_aussen_und_loch_mit_korrektur():
    tp, warn = kontur([(QUADRAT, True), (LOCH, True)], 6, 4, 2, 1500, 300)
    assert not warn
    pts = _feed_ringe(tp, -4)
    aussen = Polygon(QUADRAT)
    loch = Polygon(LOCH)
    abst_aussen = [aussen.exterior.distance(Point(p)) for p in pts if not loch.buffer(5).contains(Point(p))]
    abst_loch = [loch.exterior.distance(Point(p)) for p in pts if loch.buffer(5).contains(Point(p))]
    assert abst_aussen and abst_loch
    assert all(abs(a - 3) < 0.02 for a in abst_aussen)          # Radius 3 aussen
    assert all(abs(a - 3) < 0.02 for a in abst_loch)            # Radius 3 innen
    assert all(not aussen.contains(Point(p)) or loch.buffer(5).contains(Point(p)) for p in pts)
    assert all(loch.contains(Point(p)) for p in pts if loch.buffer(5).contains(Point(p)))
    _keine_tiefen_eilgaenge(tp)


def test_kontur_loch_zuerst_und_gleichlauf():
    tp, _ = kontur([(QUADRAT, True), (LOCH, True)], 6, 2, 2, 1500, 300)
    pts = _feed_ringe(tp, -2)
    loch_pts = [p for p in pts if Polygon(LOCH).contains(Point(p))]
    aussen_pts = [p for p in pts if not Polygon(LOCH).contains(Point(p))]
    assert pts.index(loch_pts[0]) < pts.index(aussen_pts[0])   # Loch vor Aussenkante
    assert flaeche(loch_pts) > 0      # innen gegen den Uhrzeigersinn (Gleichlauf)
    assert flaeche(aussen_pts) < 0    # aussen im Uhrzeigersinn


def test_kontur_haltestege_nur_beim_durchschneiden():
    tp, warn = kontur([(QUADRAT, True)], 6, 18.5, 6.2, 1500, 300, materialdicke=18,
                      stege_anzahl=4, stege_breite=8, stege_hoehe=2)
    zs = sorted({round(m.z, 3) for m in tp.moves if not m.rapid})
    assert -18.5 in zs and -16.5 in zs        # Endtiefe und Steg-Oberkante
    # Stegstücke: Länge der Bahn auf Stegtiefe im letzten Durchgang ~ 4 x 8 mm
    letzter = []
    p = None
    for m in tp.moves:
        if not m.rapid and p is not None and abs(m.z + 16.5) < 1e-9 and abs(p[2] + 16.5) < 1e-9:
            letzter.append(math.dist(p[:2], (m.x, m.y)))
        p = (m.x, m.y, m.z)
    assert sum(letzter) == pytest.approx(4 * 8, abs=0.5)
    tp2, warn2 = kontur([(QUADRAT, True)], 6, 5, 5, 1500, 300, materialdicke=18,
                        stege_anzahl=4)
    assert not warn2                           # kein Durchschnitt -> keine Stege, keine Meldung
    assert min(m.z for m in tp2.moves) == pytest.approx(-5)


def test_kontur_kleines_loch_wird_gemeldet():
    winz = [(10, 10), (12, 10), (12, 12), (10, 12), (10, 10)]
    tp, warn = kontur([(QUADRAT, True), (winz, True)], 6, 2, 2, 1500, 300)
    assert any("kleiner als der Fräser" in w for w in warn)


def test_tasche_mit_insel():
    insel = LOCH
    tp, warn = tasche([(QUADRAT, True), (insel, True)], 6, 3, 3, 1500, 300, bahnabstand_prozent=40)
    assert not warn
    pts = _feed_ringe(tp, -3)
    region = Polygon(QUADRAT[:-1], [insel[:-1]])
    erlaubt = region.buffer(-3 + 0.01)
    # Fräsermitte bleibt mindestens Radius vom Rand und von der Insel weg
    p = (0, 0, 20)
    for m in tp.moves:
        if not m.rapid and m.z < 0 and math.dist(p[:2], (m.x, m.y)) > 1e-9:
            assert erlaubt.contains(LineString([p[:2], (m.x, m.y)]))
        p = (m.x, m.y, m.z)
    # Abdeckung: der vom Fräser überstrichene Bereich deckt die Tasche fast ganz ab
    from shapely.ops import unary_union
    bahn = unary_union([LineString([a, c]).buffer(3) for a, c in zip(pts, pts[1:])])
    erreichbar = region.buffer(-3).buffer(3)
    assert erreichbar.difference(bahn).area < 0.01 * erreichbar.area
    _keine_tiefen_eilgaenge(tp)


def test_bohren_stufen():
    kreis = [(10 + 1.5 * math.cos(a / 8 * math.tau), 10 + 1.5 * math.sin(a / 8 * math.tau))
             for a in range(9)]
    tp, warn = bohren([(kreis, True)], 3, 6, 2, 200)
    assert not warn
    zs = [round(m.z, 3) for m in tp.moves]
    assert zs == [3.0, 0.5, -2.0, 0.5, -1.8, -4.0, 0.5, -3.8, -6.0, 3.0]
    assert all(abs(m.x - 10) < 1e-6 and abs(m.y - 10) < 1e-6 for m in tp.moves)


def test_engine_gesamt(tmp_path):
    auftrag = {
        "werkzeug": {"nummer": 1, "durchmesser": 6, "drehzahl": 16000, "vorschub": 1500,
                     "eintauchen": 300},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20,
                   "materialdicke": 18},
        "gravur": {"tiefe": 0.5, "zustellung": 0.5},
        "kontur": {"tiefe": 18.3, "zustellung": 6.1, "seite": "auto", "gleichlauf": True,
                   "stege_anzahl": 3, "stege_breite": 6, "stege_hoehe": 2},
        "tasche": {"tiefe": 3, "zustellung": 1.5, "bahnabstand": 40},
        "bohren": {"tiefe": 5, "zustellung": 2.5},
        "ausgabe": str(tmp_path / "x.tap"),
        "objekte": {
            "kontur": [{"punkte": QUADRAT, "geschlossen": True}],
            "tasche": [{"punkte": LOCH, "geschlossen": True}],
            "gravur": [{"punkte": [[10, 50], [30, 50]], "geschlossen": False}],
            "bohren": [{"punkte": [[80, 10], [83, 10], [83, 13], [80, 13]], "geschlossen": True}],
        },
    }
    f = tmp_path / "a.json"
    f.write_text(json.dumps(auftrag))
    erg = engine.rechnen(json.loads(f.read_text()))
    assert erg["ok"], erg
    text = (tmp_path / "x.tap").read_bytes().decode("ascii")
    assert text.startswith("T1M6\r\nG17\r\nG0Z20.000\r\nG0X0.000Y0.000S16000M3\r\n")
    assert tap_mm.write(tap_mm.read(text)) == text
    job = tap_mm.read(text)
    # Reihenfolge: Bohren, Gravur, Tasche, Kontur -> 4 Werkzeugwege
    assert len(job.toolpaths) == 4
    assert min(m.z for m in job.toolpaths[-1].moves) == pytest.approx(-18.3)
    assert any(z.strip().startswith("Kontur (Blau): 1 Linie ·") for z in erg["bericht"])
    assert erg["warnungen"] == []


def test_engine_mehrere_werkzeuge(tmp_path):
    werkzeuge = {
        "A": {"typ": "schaft", "durchmesser": 3, "nummer": 1, "drehzahl": 18000,
              "vorschub": 1200, "eintauchen": 300},
        "B": {"typ": "schaft", "durchmesser": 6, "nummer": 2, "drehzahl": 16000,
              "vorschub": 1500, "eintauchen": 400},
    }
    auftrag = {
        "werkzeuge": werkzeuge,
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20,
                   "materialdicke": 18},
        "gravur": {"tiefe": 0.5, "zustellung": 0.5, "werkzeug": "A"},
        "kontur": {"tiefe": 18.3, "zustellung": 6.1, "werkzeug": "B", "stege_anzahl": 0},
        "bohren": {"tiefe": 5, "zustellung": 2.5, "werkzeug": "A"},
        "tasche": {"tiefe": 3, "zustellung": 1.5, "werkzeug": "B"},
        "ausgabe": str(tmp_path / "teil.tap"),
        "objekte": {
            "kontur": [{"punkte": QUADRAT, "geschlossen": True}],
            "tasche": [{"punkte": LOCH, "geschlossen": True}],
            "gravur": [{"punkte": [[10, 50], [30, 50]], "geschlossen": False}],
            "bohren": [{"punkte": [[80, 10], [83, 10], [83, 13], [80, 13]], "geschlossen": True}],
        },
    }
    erg = engine.rechnen(auftrag)
    assert erg["ok"], erg
    namen = [os.path.basename(d) for d in erg["dateien"]]
    # A (Bohren, Gravur) zuerst, B mit der Kontur zuletzt
    assert namen == ["teil_1_D3.tap", "teil_2_D6.tap"]
    a = tap_mm.read(open(erg["dateien"][0], newline="").read())
    b = tap_mm.read(open(erg["dateien"][1], newline="").read())
    assert (a.tool_number, a.spindle_rpm, len(a.toolpaths)) == (1, 18000, 2)
    assert (b.tool_number, b.spindle_rpm, len(b.toolpaths)) == (2, 16000, 2)
    assert min(m.z for m in b.toolpaths[-1].moves) == pytest.approx(-18.3)  # Kontur zuletzt
    assert [p["form"] for p in erg["programme"]] == ["flach", "flach"]
    assert any("2 Dateien" in z for z in erg["bericht"])


def test_engine_kontur_vor_anderem_werkzeug_zuletzt(tmp_path):
    # Kontur mit A, Gravur mit B: trotzdem B zuerst, weil Ausschneiden zuletzt kommt
    auftrag = {
        "werkzeuge": {"A": {"typ": "schaft", "durchmesser": 6, "vorschub": 1500, "eintauchen": 300},
                      "B": {"typ": "v", "durchmesser": 12, "v_winkel": 90, "vorschub": 1200,
                            "eintauchen": 300}},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20},
        "gravur": {"tiefe": 0.5, "zustellung": 0.5, "werkzeug": "B"},
        "kontur": {"tiefe": 3, "zustellung": 3, "werkzeug": "A"},
        "ausgabe": str(tmp_path / "x.tap"),
        "objekte": {"kontur": [{"punkte": QUADRAT, "geschlossen": True}],
                    "gravur": [{"punkte": [[10, 50], [30, 50]], "geschlossen": False}]},
    }
    erg = engine.rechnen(auftrag)
    assert [os.path.basename(d) for d in erg["dateien"]] == ["x_1_V90.tap", "x_2_D6.tap"]


def _tasche_auftrag(tmp_path, innen_tiefe, aussen_tiefe, innen_zuerst=False, werkzeuge=("B", "B")):
    aussen = [(10, 10), (70, 10), (70, 50), (10, 50), (10, 10)]
    innen = [(25, 20), (45, 20), (45, 40), (25, 40), (25, 20)]
    b_aussen = {"id": "t1", "art": "tasche", "name": "Tasche 1", "werkzeug": werkzeuge[0],
                "tiefe": aussen_tiefe, "zustellung": 1.5, "bahnabstand": 40,
                "objekte": [{"punkte": aussen, "geschlossen": True}]}
    b_innen = {"id": "t2", "art": "tasche", "name": "Tasche 2", "werkzeug": werkzeuge[1],
               "tiefe": innen_tiefe, "zustellung": 1.5, "bahnabstand": 40,
               "objekte": [{"punkte": innen, "geschlossen": True}]}
    return {
        "werkzeuge": {"B": {"typ": "schaft", "durchmesser": 6, "vorschub": 1500, "eintauchen": 300},
                      "C": {"typ": "schaft", "durchmesser": 3, "vorschub": 1500, "eintauchen": 300}},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 18},
        "ausgabe": str(tmp_path / "t.tap"),
        "bearbeitungen": [b_innen, b_aussen] if innen_zuerst else [b_aussen, b_innen],
    }


def _ebenen_von(moves):
    """Tiefen, auf denen waagrecht gefräst wird (ohne Rampen)."""
    aus, z_alt = set(), None
    for m in moves:
        if not m.rapid and z_alt is not None and abs(m.z - z_alt) < 1e-9 and m.z < 0:
            aus.add(round(m.z, 3))
        z_alt = m.z
    return aus


def _boden(erg):
    """Simuliertes Ergebnis: Tiefe in der Mitte der inneren und der äusseren Tasche."""
    from hirch_cam.post import tap_mm
    from hirch_cam.simulation import Hoehenfeld
    h = Hoehenfeld(0, 0, 80, 60, 0.25)
    for d in erg["dateien"]:
        h.fraesen(tap_mm.read(open(d, newline="").read()), "flach", 6)
    X, Y = h.koordinaten()
    innen = (X > 28) & (X < 42) & (Y > 23) & (Y < 37)
    aussen = (X > 13) & (X < 67) & (Y > 13) & (Y < 47) & ~((X > 24) & (X < 46) & (Y > 19) & (Y < 41))
    return h.H[innen], h.H[aussen]


def test_tasche_in_tasche_ueberspringt_ausgeraeumtes(tmp_path):
    from hirch_cam import engine
    erg = engine.rechnen(_tasche_auftrag(tmp_path, innen_tiefe=5, aussen_tiefe=2))
    assert erg["ok"], erg
    from hirch_cam.post import tap_mm
    job = tap_mm.read(open(erg["dateien"][0], newline="").read())
    zweite = [m for m in job.toolpaths[-1].moves if not m.rapid and m.z < 0]
    # innere Tasche fräst erst unterhalb der äusseren (2 mm): 3.5 und 5
    assert _ebenen_von(job.toolpaths[-1].moves) == {-3.5, -5.0}
    assert all(m.z < -2 + 1e-6 for m in zweite)          # auch die Rampen beginnen unten
    innen, aussen = _boden(erg)
    assert innen.max() == pytest.approx(-5) and innen.min() == pytest.approx(-5)
    assert aussen.max() == pytest.approx(-2) and aussen.min() == pytest.approx(-2)
    # Eilgang im ausgeräumten Bereich bis knapp über den Boden
    assert any(m.rapid and m.z == pytest.approx(-1.5) for m in job.toolpaths[-1].moves)


def test_flache_tasche_in_tiefer_wird_ausgelassen(tmp_path):
    from hirch_cam import engine
    erg = engine.rechnen(_tasche_auftrag(tmp_path, innen_tiefe=2, aussen_tiefe=5))
    assert erg["ok"], erg
    assert any("schon ausgeräumt" in w for w in erg["warnungen"]), erg["warnungen"]
    assert erg["bearbeitungen"].get("t2", {}).get("weg", 0) == 0


def test_tiefere_tasche_zuerst_dann_aussen_spart_den_bereich(tmp_path):
    from hirch_cam import engine
    erg = engine.rechnen(_tasche_auftrag(tmp_path, innen_tiefe=5, aussen_tiefe=2, innen_zuerst=True))
    assert erg["ok"], erg
    from hirch_cam.post import tap_mm
    job = tap_mm.read(open(erg["dateien"][0], newline="").read())
    aussen = [m for m in job.toolpaths[-1].moves if not m.rapid and m.z < 0]
    # die äussere Tasche fährt nicht mehr durch den inneren (schon tiefen) Bereich
    assert not any(28 < m.x < 42 and 23 < m.y < 37 for m in aussen)
    innen, aussen = _boden(erg)
    assert innen.max() == pytest.approx(-5) and aussen.max() == pytest.approx(-2)
    assert aussen.min() == pytest.approx(-2)


def _auftrag_einfach(tmp_path, art, **werte):
    quadrat = [(10, 10), (50, 10), (50, 40), (10, 40), (10, 10)]
    b = {"id": "x1", "art": art, "name": art, "werkzeug": "C", "tiefe": 3, "zustellung": 3,
         "bahnabstand": 40, "seite": "aussen", "stege_anzahl": 0,
         "objekte": [{"punkte": quadrat, "geschlossen": True}]}
    b.update(werte)
    return {
        "werkzeuge": {"C": {"typ": "schaft", "durchmesser": 6, "vorschub": 1500, "eintauchen": 300},
                      "D": {"typ": "schaft", "durchmesser": 3, "vorschub": 900, "eintauchen": 200}},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 18},
        "ausgabe": str(tmp_path / "t.tap"), "bearbeitungen": [b]}


def _xy_grenzen(erg, schluessel):
    pts = [(z[i], z[i + 1]) for z in erg["bahnen"][schluessel]["zuege"] for i in range(0, len(z), 2)]
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return min(xs), max(xs), min(ys), max(ys)


def test_kontur_aufmass(tmp_path):
    erg = engine.rechnen(_auftrag_einfach(tmp_path, "kontur", aufmass=0.2))
    x0, x1, y0, y1 = _xy_grenzen(erg, "x1")
    assert x0 == pytest.approx(10 - 3.2, abs=0.02) and x1 == pytest.approx(50 + 3.2, abs=0.02)
    erg = engine.rechnen(_auftrag_einfach(tmp_path, "kontur", aufmass=-0.1))
    x0, *_ = _xy_grenzen(erg, "x1")
    assert x0 == pytest.approx(10 - 2.9, abs=0.02)


def test_tasche_aufmass_und_gegenlauf(tmp_path):
    erg = engine.rechnen(_auftrag_einfach(tmp_path, "tasche", aufmass=0.5, gleichlauf=False))
    x0, x1, y0, y1 = _xy_grenzen(erg, "x1")
    assert x0 == pytest.approx(10 + 3.5, abs=0.02) and y1 == pytest.approx(40 - 3.5, abs=0.02)
    # Gegenlauf: Randbahn im Uhrzeigersinn (letzte Bahn = äusserste)
    rand = erg["bahnen"]["x1"]["zuege"][-1]
    pts = [(rand[i], rand[i + 1]) for i in range(0, len(rand), 2)]
    assert flaeche(pts[:-1]) < 0


def test_kontur_schruppen_schlichten_mit_anderem_werkzeug(tmp_path):
    erg = engine.rechnen(_auftrag_einfach(tmp_path, "kontur", schlichten=True, schlicht_zugabe=0.5,
                                          schlicht_werkzeug="D", tiefe=6, zustellung=2))
    assert erg["ok"], erg
    assert len(erg["dateien"]) == 2                     # Werkzeugwechsel
    x0, *_ = _xy_grenzen(erg, "x1")
    assert x0 == pytest.approx(10 - 3.5, abs=0.02)      # Schruppen: 0.5 Zugabe
    x0, *_ = _xy_grenzen(erg, "x1#schlicht")
    assert x0 == pytest.approx(10 - 1.5, abs=0.02)      # Schlichten mit Ø 3 auf Mass
    from hirch_cam.post import tap_mm
    job = tap_mm.read(open(erg["dateien"][1], newline="").read())
    assert _ebenen_von([m for tp in job.toolpaths for m in tp.moves]) == {-6.0}   # ein Durchgang


def test_tasche_schlichten_bleibt_am_rand(tmp_path):
    erg = engine.rechnen(_auftrag_einfach(tmp_path, "tasche", schlichten=True, schlicht_zugabe=0.3))
    assert len(erg["dateien"]) == 1
    x0, *_ = _xy_grenzen(erg, "x1")
    assert x0 == pytest.approx(10 + 3.3, abs=0.02)
    s = erg["bahnen"]["x1#schlicht"]
    assert len(s["zuege"]) == 1 and s["durchmesser"] == 6
    x0, x1, y0, y1 = _xy_grenzen(erg, "x1#schlicht")
    assert (x0, x1, y0, y1) == pytest.approx((13, 47, 13, 37), abs=0.02)
    # Simulation: Wand genau auf Mass
    from hirch_cam.post import tap_mm
    from hirch_cam.simulation import Hoehenfeld
    h = Hoehenfeld(0, 0, 60, 50, 0.1)
    h.fraesen(tap_mm.read(open(erg["dateien"][0], newline="").read()), "flach", 6)
    X, Y = h.koordinaten()
    from shapely import vectorized
    from shapely.geometry import box
    erreichbar = box(10, 10, 50, 40).buffer(-3).buffer(3).buffer(-0.15)
    innen = vectorized.contains(erreichbar, X, Y)
    assert h.H[innen].max() == pytest.approx(-3)
    assert h.H[(X < 9.9) | (X > 50.1)].min() > -1e-6


def _steigungen(moves):
    """Grösste Neigung (Grad) der Vorschubbewegungen, die waagrecht und
    nach unten zugleich fahren, und Anzahl senkrechter Stösse ins Material."""
    gross, senkrecht, p, tiefste = 0.0, 0, None, 0.0
    for m in moves:
        if p is not None and not m.rapid and m.z < p[2] - 1e-9:
            xy = math.dist(p[:2], (m.x, m.y))
            if xy < 1e-9:
                if m.z < tiefste - 1e-6:        # tiefer als je zuvor: ins Material
                    senkrecht += 1
            else:
                gross = max(gross, math.degrees(math.atan2(p[2] - m.z, xy)))
        p = (m.x, m.y, m.z)
        tiefste = min(tiefste, m.z)
    return gross, senkrecht


def test_tasche_taucht_mit_rampe_ein_und_raeumt_gleich_aus(tmp_path):
    from hirch_cam.post import tap_mm
    from hirch_cam.simulation import Hoehenfeld
    from shapely import vectorized
    from shapely.geometry import box
    a = _auftrag_einfach(tmp_path, "tasche", tiefe=4, zustellung=2, rampe_winkel=5)
    erg = engine.rechnen(a)
    job = tap_mm.read(open(erg["dateien"][0], newline="").read())
    moves = [m for tp in job.toolpaths for m in tp.moves]
    gross, senkrecht = _steigungen(moves)
    assert senkrecht == 0 and gross <= 5.0 + 0.05
    h = Hoehenfeld(0, 0, 60, 50, 0.1)
    h.fraesen(job, "flach", 6)
    X, Y = h.koordinaten()
    innen = vectorized.contains(box(10, 10, 50, 40).buffer(-3).buffer(3).buffer(-0.15), X, Y)
    assert h.H[innen].max() == pytest.approx(-4) and h.H[innen].min() == pytest.approx(-4)
    assert h.H[~vectorized.contains(box(10, 10, 50, 40).buffer(0.05), X, Y)].min() > -1e-6


def test_tasche_innerste_runde_und_uebergaenge_langsamer(tmp_path):
    erg = engine.rechnen(_auftrag_einfach(tmp_path, "tasche", tiefe=2, zustellung=2, vollnut=50))
    from hirch_cam.post import tap_mm
    job = tap_mm.read(open(erg["dateien"][0], newline="").read())
    feeds = {m.feed for tp in job.toolpaths for m in tp.moves if not m.rapid and m.feed}
    assert 750.0 in feeds and 1500.0 in feeds


def test_kontur_ohne_stege_fortlaufend_als_schraube(tmp_path):
    erg = engine.rechnen(_auftrag_einfach(tmp_path, "kontur", tiefe=6, zustellung=2,
                                          rampe_winkel=3))
    from hirch_cam.post import tap_mm
    job = tap_mm.read(open(erg["dateien"][0], newline="").read())
    moves = [m for tp in job.toolpaths for m in tp.moves]
    gross, senkrecht = _steigungen(moves)
    assert senkrecht == 0 and gross < 3.0
    assert _ebenen_von(moves) == {-6.0}                  # nur die letzte Runde waagrecht


def test_senkrecht_wenn_gewuenscht(tmp_path):
    erg = engine.rechnen(_auftrag_einfach(tmp_path, "tasche", tiefe=2, zustellung=2,
                                          eintauchen_art="senkrecht"))
    from hirch_cam.post import tap_mm
    job = tap_mm.read(open(erg["dateien"][0], newline="").read())
    assert _steigungen([m for tp in job.toolpaths for m in tp.moves])[1] >= 1


@pytest.mark.parametrize("insel", [False, True])
def test_tasche_adaptiv_raeumt_aus_ohne_zu_verletzen(tmp_path, insel):
    from hirch_cam import cam
    if not cam.verfuegbar():
        pytest.skip("CAM-Modul fehlt")
    from hirch_cam.post import tap_mm
    from hirch_cam.simulation import Hoehenfeld
    from shapely import vectorized
    from shapely.geometry import Polygon, box
    a = _auftrag_einfach(tmp_path, "tasche", tiefe=6, zustellung=3, strategie="adaptiv",
                         adaptiv_last=20)
    form = box(10, 10, 50, 40)
    if insel:
        loch = [(25, 20), (35, 20), (35, 30), (25, 30), (25, 20)]
        a["bearbeitungen"][0]["objekte"].append({"punkte": loch, "geschlossen": True})
        form = Polygon(form.exterior.coords, [loch])
    erg = engine.rechnen(a)
    assert erg["ok"], erg
    job = tap_mm.read(open(erg["dateien"][0], newline="").read())
    moves = [m for tp in job.toolpaths for m in tp.moves]
    assert _steigungen(moves)[1] == 0                     # nur Helix, kein Stoss
    h = Hoehenfeld(0, 0, 60, 50, 0.1)
    h.fraesen(job, "flach", 6)
    X, Y = h.koordinaten()
    innen = vectorized.contains(form.buffer(-3).buffer(3).buffer(-0.2), X, Y)
    import numpy as np
    assert np.percentile(h.H[innen], 99.5) == pytest.approx(-6, abs=0.01)
    draussen = ~vectorized.contains(form.buffer(0.05), X, Y)
    assert h.H[draussen].min() > -1e-6                   # Rand und Insel unverletzt
    # keine Eilgänge im Material (seit 25.10. dürfen Übergänge über Geräumtem tiefer liegen – geprüft gegen das,
    # was wirklich noch steht: werkzeuge/eilgang_pruefen.py)
    import sys as _sys
    _sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "werkzeuge"))
    import eilgang_pruefen
    assert eilgang_pruefen.pruefen(erg, aussen_frei=(0, 0, 60, 50)) == []


def _vorschuebe(datei):
    from hirch_cam.post import tap_mm
    job = tap_mm.read(open(datei, newline="").read())
    return {round(m.feed) for tp in job.toolpaths for m in tp.moves
            if m.feed and not m.rapid}


def test_vorschub_je_bearbeitung(tmp_path):
    """«Vorschub dieser Bearbeitung» und «Vorschub beim Schlichten» skalieren
    nur den Bahnvorschub, nicht das Eintauchen."""
    erg = engine.rechnen(_auftrag_einfach(tmp_path, "kontur", eintauchen_art="senkrecht"))
    normal = _vorschuebe(erg["dateien"][0])
    assert 1500 in normal and 300 in normal
    erg = engine.rechnen(_auftrag_einfach(tmp_path, "kontur", eintauchen_art="senkrecht",
                                          vorschub_prozent=60))
    langsam = _vorschuebe(erg["dateien"][0])
    assert 900 in langsam and 1500 not in langsam and 300 in langsam
    erg = engine.rechnen(_auftrag_einfach(tmp_path, "kontur", eintauchen_art="senkrecht",
                                          schlichten=True, schlicht_werkzeug="D",
                                          schlicht_vorschub=50, tiefe=6, zustellung=2))
    assert 450 in _vorschuebe(erg["dateien"][1])          # 900 × 50 %
    assert 1500 in _vorschuebe(erg["dateien"][0])


def test_eigener_name_der_bearbeitung():
    from hirch_cam import einstellungen
    e = einstellungen.laden(None)
    e["bearbeitungen"].append(einstellungen.neue_bearbeitung(e, "tasche"))
    e["bearbeitungen"][0]["titel"] = "  Deckel  "
    assert einstellungen.namen(e)[e["bearbeitungen"][0]["id"]] == "Deckel"
    e["bearbeitungen"][0]["titel"] = ""
    assert einstellungen.namen(e)[e["bearbeitungen"][0]["id"]] != ""


def _auftrag_richauto(tmp_path, **mehr):
    QUADRAT_ = [[0, 0], [100, 0], [100, 60], [0, 60]]
    a = {
        "werkzeuge": {"A": {"typ": "schaft", "durchmesser": 3, "nummer": 1, "vorschub": 1000,
                            "eintauchen": 300},
                      "B": {"typ": "schaft", "durchmesser": 6, "nummer": 1, "vorschub": 1500,
                            "eintauchen": 400}},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20,
                   "materialdicke": 18},
        "gravur": {"tiefe": 0.5, "zustellung": 0.5, "werkzeug": "A"},
        "kontur": {"tiefe": 18.3, "zustellung": 6.1, "werkzeug": "B", "stege_anzahl": 0},
        "postprozessor": "richauto_a11",
        "ausgabe": str(tmp_path / "teil.tap"),
        "objekte": {
            "kontur": [{"punkte": QUADRAT_, "geschlossen": True}],
            "gravur": [{"punkte": [[10, 50], [30, 50]], "geschlossen": False}],
        },
    }
    a.update(mehr)
    return a


def test_engine_richauto_datei_je_werkzeug(tmp_path):
    """RichAuto A11 (Vorgabe seit 20.10.): je Werkzeug eine Datei mit kurzem Namen, Endung .TAP –
    M6 fährt am Regler zum Maschinen-Nullpunkt (Peter)."""
    erg = engine.rechnen(_auftrag_richauto(tmp_path))
    assert erg["ok"], erg
    assert [os.path.basename(d) for d in erg["dateien"]] == ["teil_1.TAP", "teil_2.TAP"]
    assert erg["kurzname"] == 8
    for d in erg["dateien"]:
        text = open(d, newline="").read()
        assert "M6" not in text and text.endswith("M5\r\nM30\r\n")


def test_engine_luftfraesen(tmp_path):
    """Luftfräsen: dieselben Bahnen, alles um den Abstand höher."""
    import re
    (tmp_path / "e").mkdir()
    (tmp_path / "l").mkdir()
    echt = engine.rechnen(_auftrag_richauto(tmp_path / "e"))
    luft = engine.rechnen(_auftrag_richauto(tmp_path / "l", luft=10))

    def zwerte(d):
        return [float(z) for z in re.findall(r"Z(-?[0-9.]+)", open(d, newline="").read())]
    for a, b in zip(echt["dateien"], luft["dateien"]):
        za, zb = zwerte(a), zwerte(b)
        assert zb == pytest.approx([z + 28.3 for z in za])    # 18,3 mm tiefste Stelle + 10 mm
    assert min(min(zwerte(b)) for b in luft["dateien"]) == pytest.approx(10)
    assert any("Luftfräsen" in z for z in luft["bericht"])


def test_engine_richauto_eine_datei(tmp_path):
    """RichAuto A11 mit Maschine «alle in einer Datei»: Halt bei T2M6, keine Drehzahl."""
    QUADRAT_ = [[0, 0], [100, 0], [100, 60], [0, 60]]
    auftrag = {
        "werkzeuge": {"A": {"typ": "schaft", "durchmesser": 3, "nummer": 1, "vorschub": 1000,
                            "eintauchen": 300},
                      "B": {"typ": "schaft", "durchmesser": 6, "nummer": 1, "vorschub": 1500,
                            "eintauchen": 400}},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20,
                   "materialdicke": 18},
        "gravur": {"tiefe": 0.5, "zustellung": 0.5, "werkzeug": "A"},
        "kontur": {"tiefe": 18.3, "zustellung": 6.1, "werkzeug": "B", "stege_anzahl": 0},
        "postprozessor": "richauto_a11",
        "maschine": {"werkzeugwechsel": "halt"},
        "ausgabe": str(tmp_path / "teil.tap"),
        "objekte": {
            "kontur": [{"punkte": QUADRAT_, "geschlossen": True}],
            "gravur": [{"punkte": [[10, 50], [30, 50]], "geschlossen": False}],
        },
    }
    erg = engine.rechnen(auftrag)
    assert erg["ok"], erg
    assert [os.path.basename(d) for d in erg["dateien"]] == ["teil.TAP"]
    text = open(erg["dateien"][0], newline="").read()
    zeilen = text.split("\r\n")
    assert text.endswith("M5\r\nM30\r\n")
    assert zeilen[:3] == ["G17", "G0Z20.000", "G0X0.000Y0.000M3"]   # erstes Werkzeug steckt schon
    assert "T1M6" not in zeilen
    i = zeilen.index("T2M6")
    assert zeilen[i - 3:i + 3] == ["G0Z20.000", "M5", "G0X0.000Y0.000", "T2M6",
                                   "G0Z20.000", "G0X0.000Y0.000M3"]
    assert "S" not in text
    # Vorschau/Prüfung bekommen je Werkzeug einen Teil
    assert len(erg["programme"]) == 2
    b = tap_mm.read(open(erg["programme"][1]["datei"], newline="").read())
    assert min(m.z for m in b.toolpaths[-1].moves) == pytest.approx(-18.3)
    assert any("Eine Datei" in z for z in erg["bericht"])


def test_engine_nullpunkt_mitte_und_unterseite(tmp_path):
    """Ausgabe auf den gewählten Nullpunkt verschoben; Vorschau-Teile bleiben in Materialkoordinaten."""
    auftrag = {
        "werkzeuge": {"A": {"typ": "schaft", "durchmesser": 3, "nummer": 1, "vorschub": 1000,
                            "eintauchen": 300}},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20,
                   "materialdicke": 18},
        "gravur": {"tiefe": 0.5, "zustellung": 0.5, "werkzeug": "A"},
        "postprozessor": "vcarve_mm",
        "nullpunkt": {"x": 50.0, "y": 25.0, "z_unten": True},
        "ausgabe": str(tmp_path / "teil.tap"),
        "objekte": {"gravur": [{"punkte": [[10, 10], [90, 10]], "geschlossen": False}]},
    }
    erg = engine.rechnen(auftrag)
    assert erg["ok"], erg
    aus = tap_mm.read(open(erg["dateien"][0], newline="").read())
    teil = tap_mm.read(open(erg["programme"][0]["datei"], newline="").read())
    xs = [m.x for tp in aus.toolpaths for m in tp.moves]
    zs = [m.z for tp in aus.toolpaths for m in tp.moves]
    assert min(xs) == pytest.approx(-40) and max(xs) == pytest.approx(40)
    assert min(zs) == pytest.approx(17.5)              # 18 mm Material, 0,5 mm tief
    assert aus.safe_z == pytest.approx(38)
    assert min(m.x for tp in teil.toolpaths for m in tp.moves) == pytest.approx(10)


def test_eilgang_ueber_spannmittel_angehoben(tmp_path):
    """Zwei Gravuren links und rechts einer Pratze: der Eilgang dazwischen geht über die Pratze."""
    auftrag = {
        "werkzeuge": {"A": {"typ": "schaft", "durchmesser": 3, "nummer": 1, "vorschub": 1000,
                            "eintauchen": 300}},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 30,
                   "materialdicke": 18},
        "gravur": {"tiefe": 0.5, "zustellung": 0.5, "werkzeug": "A"},
        "postprozessor": "vcarve_mm",
        "spannmittel": [{"punkte": [[45, 0], [55, 0], [55, 60], [45, 60]], "hoehe": 15}],
        "ausgabe": str(tmp_path / "teil.tap"),
        "objekte": {"gravur": [{"punkte": [[10, 10], [10, 50]], "geschlossen": False},
                               {"punkte": [[90, 50], [90, 10]], "geschlossen": False}]},
    }
    erg = engine.rechnen(auftrag)
    assert erg["ok"], erg
    job = tap_mm.read(open(erg["dateien"][0], newline="").read())
    ms = [m for tp in job.toolpaths for m in tp.moves]
    quer = [(a, b) for a, b in zip(ms, ms[1:]) if b.rapid and min(a.x, b.x) < 45 and max(a.x, b.x) > 55]
    assert quer and all(min(a.z, b.z) >= 17 - 1e-6 for a, b in quer)    # 15 mm Pratze + 2 mm Luft
    assert any("Spannmittel" in w for w in erg["warnungen"])
    from hirch_cam.pruefung import pruefen
    assert not [b for b in pruefen(auftrag, erg) if "Spannmittel" in b["text"]]
    # eine Gravur quer durch die Pratze ist ein Fehler
    auftrag["objekte"]["gravur"].append({"punkte": [[30, 30], [70, 30]], "geschlossen": False})
    erg = engine.rechnen(auftrag)
    befunde = [b for b in pruefen(auftrag, erg) if "Spannmittel" in b["text"]]
    assert befunde and befunde[0]["stufe"] == "fehler"


def test_bohren_helix_auf_durchmesser():
    """Kreis Ø10 mit 3-mm-Fräser: Schrauben bis auf Ø10, ohne Zapfen in der Mitte."""
    import math
    from hirch_cam.ops.bohren import bohren
    kreis = [(10 + 5 * math.cos(a / 60 * 2 * math.pi), 10 + 5 * math.sin(a / 60 * 2 * math.pi)) for a in range(61)]
    klein = [(30 + 1.4 * math.cos(a / 20 * 2 * math.pi), 10 + 1.4 * math.sin(a / 20 * 2 * math.pi))
             for a in range(21)]
    tp, warn = bohren([(kreis, True), (klein, True)], 3, 4, 1, 300, methode="auto", vorschub=1000)
    assert not warn
    unten = [m for m in tp.moves if not m.rapid and abs(m.z + 4) < 1e-6 and m.x < 20]
    r = [math.dist((m.x, m.y), (10, 10)) for m in unten]
    assert max(r) == pytest.approx(3.5, abs=0.02)          # Wand: Lochradius − Fräserradius
    assert min(r) < 1.5                                      # innen ausgeräumt
    # das kleine Loch (Ø2.8 < Fräser+0.5) wird gebohrt: senkrecht in der Mitte
    assert any(abs(m.x - 30) < 1e-6 and abs(m.y - 10) < 1e-6 and abs(m.z + 4) < 1e-6 for m in tp.moves)
    nur_bohren, warn = bohren([(kreis, True)], 3, 4, 1, 300, methode="bohren")
    assert warn and "Helix" in warn[0]


def test_kontur_anfahren_im_bogen_ohne_marke_am_teil():
    """Anfahren im Bogen: Werkzeugmitte bleibt immer mindestens den Radius vom Teil weg,
    getaucht wird neben der Kontur."""
    for loch in (LOCH, [(40, 20), (52, 20), (52, 32), (40, 32), (40, 20)]):
        teil = Polygon(QUADRAT[:-1], [loch[:-1]])
        tp, warn = kontur([(QUADRAT, True), (loch, True)], 6, 4, 2, 1000, 300, materialdicke=4,
                          stege_anzahl=0, anfahren="bogen", start=(0, 0, 20))
        schnitte = [m for m in tp.moves if not m.rapid and m.z < 0]
        assert all(teil.distance(Point(m.x, m.y)) > 3 - 0.02 for m in schnitte)
        # eingetaucht wird nicht auf der Kontur selbst (Abstand > Radius)
        tauchen = [b for a, b in zip(tp.moves, tp.moves[1:])
                   if not b.rapid and b.z < a.z - 1e-6 and abs(a.x - b.x) < 1e-9 and abs(a.y - b.y) < 1e-9
                   and b.z < -0.01]
        assert tauchen and all(teil.distance(Point(m.x, m.y)) > 3 - 0.02 for m in tauchen)
        if loch is LOCH:                    # genug Platz: ganz neben der Kontur getaucht
            assert all(teil.distance(Point(m.x, m.y)) > 3.5 for m in tauchen)


def test_dogbone_ecken_der_tasche_und_des_lochs_frei():
    """Mit Dogbone erreicht der Fräser jede Ecke der Tasche bzw. des Lochs (Abstand = Radius)."""
    tasche_ = [(10, 10), (40, 10), (40, 30), (10, 30), (10, 10)]
    tp, _w = tasche([(tasche_, True)], 6, 2, 2, 1000, 300, dogbone=True)
    for ecke in [(10, 10), (40, 10), (40, 30), (10, 30)]:
        d = min(math.dist((m.x, m.y), ecke) for m in tp.moves if not m.rapid and m.z < 0)
        assert d == pytest.approx(3, abs=0.02)
    ohne, _w = tasche([(tasche_, True)], 6, 2, 2, 1000, 300)
    assert min(math.dist((m.x, m.y), (10, 10)) for m in ohne.moves if not m.rapid and m.z < 0) > 4
    # Kontur: Loch im Teil, Innenecken des Lochs; die Aussenecken des Teils bleiben rund
    tp, _w = kontur([(QUADRAT, True), (LOCH, True)], 6, 4, 4, 1000, 300, materialdicke=4,
                    stege_anzahl=0, dogbone=True)
    teil = Polygon(QUADRAT[:-1], [LOCH[:-1]])
    pts = [(m.x, m.y) for m in tp.moves if not m.rapid and m.z < 0]
    # die Knochen schneiden nur in den Ecken etwas in die Wände (das ist ihr Zweck)
    assert all(teil.distance(Point(p)) > 3 - 0.02 or min(math.dist(p, e) for e in LOCH) < 3.2 for p in pts)
    for ecke in [(40, 20), (60, 20), (60, 40), (40, 40)]:
        assert min(math.dist(p, ecke) for p in pts) == pytest.approx(3, abs=0.02)


def test_fase_mit_v_fraeser(tmp_path):
    """Fase 1 mm mit 90°-V-Fräser: Spitze 0.2 mm unter der Fase, Bahn 0.2 mm neben der Kante."""
    auftrag = {
        "werkzeuge": {"A": {"typ": "v", "durchmesser": 12, "v_winkel": 90, "nummer": 1, "vorschub": 1000,
                            "eintauchen": 300}},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 10},
        "postprozessor": "vcarve_mm",
        "bearbeitungen": [{"id": "f", "art": "kontur", "werkzeug": "A", "tiefe": 5, "zustellung": 5,
                           "seite": "aussen", "fase": True, "fase_breite": 1.0,
                           "objekte": [{"punkte": [list(p) for p in QUADRAT], "geschlossen": True}]}],
        "ausgabe": str(tmp_path / "f.tap"),
    }
    erg = engine.rechnen(auftrag)
    assert erg["ok"], erg
    job = tap_mm.read(open(erg["dateien"][0], newline="").read())
    ms = [m for tp in job.toolpaths for m in tp.moves if not m.rapid]
    assert min(m.z for m in ms) == pytest.approx(-1.2, abs=1e-6)
    teil = Polygon(QUADRAT[:-1])
    unten = [m for m in ms if abs(m.z + 1.2) < 1e-6]
    assert all(teil.exterior.distance(Point(m.x, m.y)) == pytest.approx(0.2, abs=0.02) for m in unten)


def test_planfraesen_deckt_alles_und_verschiebt_die_anderen(tmp_path):
    from hirch_cam.ops.planen import planen
    tp, warn = planen((0, 0, 100, 60), 20, 1.0, 0.5, 2000, 500, bahnabstand_prozent=60)
    assert not warn
    unten = [m for m in tp.moves if not m.rapid and abs(m.z + 1.0) < 1e-9]
    # jede Stelle der Oberfläche liegt unter dem Fräser (Bahnen mit Radius 10)
    from shapely.geometry import LineString
    from shapely.ops import unary_union
    zuege = unary_union([LineString([(a.x, a.y), (b.x, b.y)]).buffer(10)
                         for a, b in zip(unten, unten[1:])])
    assert zuege.contains(Polygon([(0, 0), (100, 0), (100, 60), (0, 60)]))
    # eingetaucht wird neben dem Material
    tauchen = [m for a, m in zip(tp.moves, tp.moves[1:]) if not m.rapid and m.z < a.z and m.z < 0]
    assert all(m.x < -10 or m.x > 110 for m in tauchen)
    # im Auftrag: Gravur 0.5 tief zählt ab der geplanten Oberfläche (−1 −0.5)
    auftrag = {
        "werkzeuge": {"C": {"typ": "schaft", "durchmesser": 20, "nummer": 1, "vorschub": 2000, "eintauchen": 500},
                      "A": {"typ": "v", "durchmesser": 6, "v_winkel": 60, "nummer": 2, "vorschub": 1000,
                            "eintauchen": 300}},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 10},
        "postprozessor": "vcarve_mm",
        "bearbeitungen": [
            {"id": "p", "art": "planen", "werkzeug": "C", "tiefe": 1.0, "zustellung": 1.0,
             "objekte": [{"punkte": [[0, 0], [100, 0], [100, 60], [0, 60], [0, 0]], "geschlossen": True}]},
            {"id": "g", "art": "gravur", "werkzeug": "A", "tiefe": 0.5, "zustellung": 0.5,
             "objekte": [{"punkte": [[10, 10], [50, 10]], "geschlossen": False}]}],
        "ausgabe": str(tmp_path / "p.tap"),
    }
    erg = engine.rechnen(auftrag)
    assert erg["ok"], erg
    tiefsten = sorted(min(m.z for m in tap_mm.read(open(f, newline="").read()).toolpaths[0].moves)
                      for f in erg["dateien"])
    assert tiefsten == [pytest.approx(-1.5), pytest.approx(-1.0)]


def test_gewinde_m8_innen_mit_kernloch(tmp_path):
    """M8 innen, 10 mm lang, Einzahn-Gewindefräser Ø 5: Helix mit 1.25 mm je Umdrehung,
    Zahnspitzen bis Ø 8; vorher Kernloch Ø 6.65 mit dem 3-mm-Schaftfräser."""
    import math
    from hirch_cam.ops.gewinde import gewinde, regelsteigung
    assert regelsteigung(8) == 1.25 and regelsteigung(7.5) is None
    kreis = [(20 + 4 * math.cos(a / 90 * 2 * math.pi), 20 + 4 * math.sin(a / 90 * 2 * math.pi)) for a in range(91)]
    auftrag = {
        "werkzeuge": {"G": {"typ": "gewinde", "durchmesser": 5, "zaehne": 1, "nummer": 2, "vorschub": 400,
                            "eintauchen": 200},
                      "B": {"typ": "schaft", "durchmesser": 3, "nummer": 1, "vorschub": 1000, "eintauchen": 300,
                            "empfehlung": {"zustellung": 1.5}}},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 15},
        "postprozessor": "vcarve_mm",
        "bearbeitungen": [{"id": "g", "art": "gewinde", "werkzeug": "G", "tiefe": 10, "kernloch_werkzeug": "B",
                           "objekte": [{"punkte": [list(p) for p in kreis], "geschlossen": True}]}],
        "ausgabe": str(tmp_path / "g.tap"),
    }
    erg = engine.rechnen(auftrag)
    assert erg["ok"], erg
    assert any("M8" in w and "6.6" in w for w in erg["warnungen"])
    jobs = [tap_mm.read(open(f, newline="").read()) for f in erg["dateien"]]
    assert len(jobs) == 2
    kern = [m for m in jobs[0].toolpaths[0].moves if not m.rapid]
    gew = [m for m in jobs[1].toolpaths[0].moves if not m.rapid]
    # Kernloch zuerst: Fräserrand bis Ø 6.65
    assert max(math.dist((m.x, m.y), (20, 20)) for m in kern) == pytest.approx(6.6475 / 2 - 1.5, abs=0.02)
    # Gewinde: Fräsermitte auf Radius 4 − 2.5 = 1.5, von −10 nach oben, 1.25 je Umdrehung
    auf_kreis = [m for m in gew if abs(math.dist((m.x, m.y), (20, 20)) - 1.5) < 0.01]
    assert min(m.z for m in auf_kreis) == pytest.approx(-10, abs=1e-6)
    assert max(m.z for m in auf_kreis) == pytest.approx(-10 + (10 / 1.25 + 0.25) * 1.25, abs=0.05)
    # nie ausserhalb des Gewinde-Nenndurchmessers
    assert max(math.dist((m.x, m.y), (20, 20)) + 2.5 for m in gew) <= 4 + 1e-6


def test_boegen_g2_g3_gleiche_bahn(tmp_path):
    """Mit Bögen: viel kürzere Datei, eingelesen dieselbe Bahn (± 0.01 mm); Ecken bleiben Ecken."""
    kreis = [(50 + 20 * math.cos(a / 200 * 2 * math.pi), 30 + 20 * math.sin(a / 200 * 2 * math.pi))
             for a in range(201)]
    sechseck = [(20 + 8 * math.cos(a / 6 * 2 * math.pi), 30 + 8 * math.sin(a / 6 * 2 * math.pi)) for a in range(7)]
    texte = {}
    for boegen in (False, True):
        auftrag = {
            "werkzeuge": {"C": {"typ": "schaft", "durchmesser": 6, "nummer": 1, "vorschub": 1000, "eintauchen": 300}},
            "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 6},
            "postprozessor": "richauto_a11", "boegen": boegen,
            "bearbeitungen": [{"id": "k", "art": "kontur", "werkzeug": "C", "tiefe": 3, "zustellung": 1.5,
                               "seite": "aussen", "anfahren": "bogen",
                               "objekte": [{"punkte": [list(p) for p in kreis], "geschlossen": True},
                                           {"punkte": [list(p) for p in sechseck], "geschlossen": True}]}],
            "ausgabe": str(tmp_path / ("b%d.tap" % boegen)),
        }
        erg = engine.rechnen(auftrag)
        assert erg["ok"], erg
        texte[boegen] = open(erg["dateien"][0], newline="").read()
    assert "G2" in texte[True] or "G3" in texte[True]
    assert "G2" not in texte[False] and "G3" not in texte[False]
    assert len(texte[True].splitlines()) < len(texte[False].splitlines()) / 3
    # Bahn vergleichen: jeder Punkt der Geraden-Datei liegt nahe der Bogen-Datei
    from hirch_cam.post.boegen import bogen_punkte
    def punkte(text):
        pos, g, aus = (0.0, 0.0, 20.0), 0, []
        import re
        for z in text.splitlines():
            w = dict(re.findall(r"([GXYZIJ])(-?\d+(?:\.\d+)?)", z))
            if "G" in w:
                g = int(float(w["G"]))
            if not any(a in w for a in "XYZ"):
                continue
            ziel = tuple(float(w[a]) if a in w else pos["XYZ".index(a)] for a in "XYZ")
            if g in (2, 3) and ("I" in w or "J" in w):
                # fein aufgelöst: sonst zählt die Sehne der Messung (0,01) selbst zur Abweichung
                aus += bogen_punkte(pos, ziel, pos[0] + float(w.get("I", 0)), pos[1] + float(w.get("J", 0)), g == 3,
                                    toleranz=0.001)
            else:
                aus.append(ziel)
            pos = ziel
        return aus
    a, b = punkte(texte[False]), punkte(texte[True])
    unten_b = [p for p in b if p[2] < -2.9]
    unten_a = [p for p in a if p[2] < -2.9]
    from shapely.geometry import MultiPoint, LineString
    lb = LineString([p[:2] for p in unten_b])
    assert max(lb.distance(Point(p[:2])) for p in unten_a) < 0.01
    # das Sechseck bleibt eckig: seine Ecken (Bahn mit Radius-Rundung) sind keine Vollkreise
    assert "G1" in texte[True]
