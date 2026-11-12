"""Reihenfolge (automatisch / wie in der Liste) und zweiseitige Aufträge."""
import json
import os

import pytest

from hirch_cam import ablauf, einstellungen as einst
from hirch_cam.post import tap_mm

from test_dokument import braucht_inkex


def _q(x, y, a):
    return [[x, y], [x + a, y], [x + a, y + a], [x, y + a], [x, y]]


def _auftrag(tmp_path, reihenfolge="auto", zweite_tasche_seite=1):
    return {
        "werkzeuge": {"A": {"typ": "schaft", "durchmesser": 3, "nummer": 1, "vorschub": 1000, "eintauchen": 300},
                      "B": {"typ": "schaft", "durchmesser": 6, "nummer": 1, "vorschub": 1500, "eintauchen": 400}},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 18},
        "postprozessor": "richauto_a11", "reihenfolge": reihenfolge,
        "maschine": {"werkzeugwechsel": "halt"},
        "ausgabe": str(tmp_path / "teil.tap"),
        "bearbeitungen": [
            {"id": "t1", "art": "tasche", "name": "Tasche 1", "werkzeug": "A", "tiefe": 2, "zustellung": 1,
             "objekte": [{"punkte": _q(10, 10, 20), "geschlossen": True}]},
            {"id": "k", "art": "kontur", "name": "Kontur", "werkzeug": "B", "tiefe": 4, "zustellung": 2,
             "seite": "aussen", "stege_anzahl": 0,
             "objekte": [{"punkte": _q(5, 5, 90), "geschlossen": True}]},
            {"id": "t2", "art": "tasche", "name": "Tasche 2", "werkzeug": "A", "tiefe": 1, "zustellung": 1,
             "auf_seite": zweite_tasche_seite,
             "objekte": [{"punkte": _q(50, 50, 20), "geschlossen": True}]},
        ]}


def _t_folge(datei):
    return [z for z in open(datei, newline="").read().split("\r\n") if z.startswith("T")]


def test_abschnitte_auto_und_liste():
    teile = [{"key": "a", "slot": "A", "art": "tasche", "seite": 1, "pos": 0},
             {"key": "k", "slot": "B", "art": "kontur", "seite": 1, "pos": 1},
             {"key": "b", "slot": "A", "art": "tasche", "seite": 1, "pos": 2}]
    auto = ablauf.abschnitte(teile, "auto")
    assert [(a["slot"], a["keys"]) for a in auto] == [("A", ["a", "b"]), ("B", ["k"])]
    liste = ablauf.abschnitte(teile, "liste")
    assert [(a["slot"], a["keys"]) for a in liste] == [("A", ["a"]), ("B", ["k"]), ("A", ["b"])]
    assert ablauf.werkzeugnummern(liste) == {"A": 1, "B": 2}


def test_zustellungen():
    z = ablauf.zustellungen("tasche", {"tiefe": 3, "zustellung": 1.5})
    assert (z["n"], z["je"]) == (2, 1.5)
    z = ablauf.zustellungen("kontur", {"tiefe": None, "zustellung": 6.1}, dicke=18)
    assert z["durch"] and z["n"] == 3 and z["tiefe"] == pytest.approx(18.2)


def test_engine_liste_werkzeug_kommt_wieder(tmp_path):
    from hirch_cam import engine
    auto = engine.rechnen(_auftrag(tmp_path, "auto"))
    assert _t_folge(auto["dateien"][0]) == ["T2M6"]                  # T1 steckt beim Start; Kontur zuletzt
    liste = engine.rechnen(_auftrag(tmp_path, "liste"))
    assert _t_folge(liste["dateien"][0]) == ["T2M6", "T1M6"]          # zurück zu T1
    seiten = liste["ablauf"]["seiten"]
    assert len(seiten) == 1
    ab = seiten[0]["abschnitte"]
    assert [(a["t"], a["wechsel"], [o["id"] for o in a["ops"]]) for a in ab] == \
        [(1, False, ["t1"]), (2, True, ["k"]), (1, True, ["t2"])]
    assert ab[0]["ops"][0]["n"] == 2 and ab[1]["ops"][0]["n"] == 2


def test_engine_zweiseitig_zwei_dateien(tmp_path):
    from hirch_cam import engine
    erg = engine.rechnen(_auftrag(tmp_path, "auto", zweite_tasche_seite=2))
    assert [os.path.basename(d) for d in erg["dateien"]] == ["teil_S1.TAP", "teil_S2.TAP"]
    assert _t_folge(erg["dateien"][0]) == ["T2M6"]                   # kein Halt am Start
    assert _t_folge(erg["dateien"][1]) == []                         # Seite 2: Werkzeug beim Wenden einsetzen
    seiten = erg["ablauf"]["seiten"]
    assert [s["seite"] for s in seiten] == [1, 2]
    assert seiten[1]["abschnitte"][0]["nach_wenden"] and seiten[1]["abschnitte"][0]["wechsel"]
    assert [p["seite"] for p in erg["programme"]] == [1, 1, 2]
    # Seite 2 nur die zweite Tasche: Koordinaten wie gezeichnet
    p2 = [p for p in erg["programme"] if p["seite"] == 2][0]
    job = tap_mm.read(open(p2["datei"], newline="").read())
    xs = [m.x for tp in job.toolpaths for m in tp.moves if not m.rapid]
    assert min(xs) > 49 and max(xs) < 71


def test_passloecher_im_auftrag():
    e = einst.laden(json.dumps({"zweiseitig": {"an": True, "achse": "x", "pass_rand": 12}}))
    a = einst.auftrag(e, {}, 18.0, "/tmp/x.tap", flaeche=(200, 100))
    pl = [b for b in a["bearbeitungen"] if b["id"] == einst.PASS_ID]
    assert len(pl) == 1 and pl[0]["tiefe"] == pytest.approx(21)
    mitten = [(round(sum(p[0] for p in o["punkte"][:-1]) / 72, 3), round(sum(p[1] for p in o["punkte"][:-1]) / 72, 3))
              for o in pl[0]["objekte"]]
    assert mitten == [(12, 50), (188, 50)]
    e["zweiseitig"]["achse"] = "y"
    assert einst.passloecher(e, 200, 100) == [(100, 12), (100, 88)]


def test_zuordnen_nach_seite():
    e = einst.laden(json.dumps({"zweiseitig": {"an": True}, "bearbeitungen": [
        {"art": "tasche", "id": "s1"}, {"art": "tasche", "id": "s2", "auf_seite": 2}]}))
    els = [{"id": "a", "art": "tasche", "rgb": (0, 160, 0), "seite": 1, "linien": []},
           {"id": "b", "art": "tasche", "rgb": (0, 160, 0), "seite": 2, "linien": []}]
    zu = einst.zuordnen(e, els)
    assert [x["id"] for x in zu["s1"]] == ["a"] and [x["id"] for x in zu["s2"]] == ["b"]
    # fehlt eine Bearbeitung für Seite 2, wird sie angelegt
    e2 = einst.laden(json.dumps({"zweiseitig": {"an": True}, "bearbeitungen": [{"art": "tasche"}]}))
    einst.ergaenzen(e2, {("tasche", 1), ("tasche", 2)})
    assert sorted(einst.auf_seite(e2, b) for b in e2["bearbeitungen"]) == [1, 2]


ZWEI_SEITEN = """<svg xmlns="http://www.w3.org/2000/svg" xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape"
 xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd" xmlns:hirch="https://hirch.net/cnc"
 width="200mm" height="100mm" viewBox="0 0 200 100" hirch:materialdicke="18" hirch:einstellungen='EINST'>
 <sodipodi:namedview id="nv"><inkscape:page x="0" y="0" width="200" height="100" id="p1"/>
  <inkscape:page x="220" y="0" width="200" height="100" id="p2"/></sodipodi:namedview>
 <path id="oben" d="M 20,20 L 60,20 L 60,60 L 20,60 Z" style="fill:none;stroke:#00a000"/>
 <path id="unten" d="M 330,30 L 370,30 L 370,70 L 330,70 Z" style="fill:none;stroke:#00a000"/>
</svg>"""


@braucht_inkex
def test_dokument_zweiseitig(tmp_path):
    from hirch_cam import dokument
    e = {"zweiseitig": {"an": True, "passloecher": True, "pass_werkzeug": "C"}}
    pfad = tmp_path / "z.svg"
    pfad.write_text(ZWEI_SEITEN.replace("EINST", json.dumps(e)))
    a = dokument.analyse(str(pfad))
    assert [round(s["links"]) for s in a["seiten"]] == [0, 220]
    bea = a["einstellungen"]["bearbeitungen"]
    assert sorted(int(b.get("auf_seite") or 1) for b in bea) == [1, 2]   # je Seite eine Tasche
    vorab = a["ablauf"]
    assert [s["seite"] for s in vorab["seiten"]] == [1, 2]
    assert vorab["seiten"][0]["abschnitte"][0]["ops"][0]["id"] == "pass"
    erg = dokument.rechnen(str(pfad), str(tmp_path / "aus"), "z")
    assert erg["ok"], erg.get("warnungen")
    assert [os.path.basename(d) for d in erg["dateien"]] == ["z_1.TAP", "z_2.TAP", "z_3.TAP"]   # je Werkzeug eine Datei (RichAuto)
    p2 = [p for p in erg["programme"] if p["seite"] == 2][0]
    job = tap_mm.read(open(p2["datei"], newline="").read())
    xs = [m.x for tp in job.toolpaths for m in tp.moves if not m.rapid]
    ys = [m.y for tp in job.toolpaths for m in tp.moves if not m.rapid]
    # Seite 2 in ihren eigenen Koordinaten: x 110…150, y 30…70 (von unten)
    assert 109 < min(xs) and max(xs) < 151 and 29 < min(ys) and max(ys) < 71


def test_pruefung_taschen_von_beiden_seiten():
    from hirch_cam.pruefung import pruefen
    def tasche(bid, seite, tiefe, x, y):
        return {"id": bid, "art": "tasche", "name": bid, "werkzeug": "A", "tiefe": tiefe, "auf_seite": seite,
                "objekte": [{"punkte": _q(x, y, 20), "geschlossen": True}]}
    a = {"werkzeuge": {"A": {"durchmesser": 3}}, "hoehen": {"materialdicke": 18},
         "material_flaeche": [200, 100], "zweiseitig": {"an": True, "achse": "x"},
         # Seite 2 bei y 70…90 liegt gewendet (um X) über y 10…30 von Seite 1
         "bearbeitungen": [tasche("oben", 1, 10, 50, 10), tasche("unten", 2, 9, 50, 70)]}
    texte = [b["text"] for b in pruefen(a, {"programme": []})]
    assert any("treffen sich" in t for t in texte)
    a["bearbeitungen"][1]["tiefe"] = 7
    texte = [b["text"] for b in pruefen(a, {"programme": []})]
    assert any("nur 1,0 mm Material" in t for t in texte)


def test_tasche_mit_gravierstichel_raeumt_mit_der_spitze(tmp_path):
    from hirch_cam import engine
    a = _auftrag(tmp_path)
    a["werkzeuge"]["A"] = {"typ": "v", "durchmesser": 3.0, "v_winkel": 36, "spitze": 0.2, "nummer": 1,
                           "vorschub": 170, "eintauchen": 60}
    a["bearbeitungen"] = [a["bearbeitungen"][0]]
    a["bearbeitungen"][0]["objekte"] = [{"punkte": _q(10, 10, 4), "geschlossen": True}]
    a["bearbeitungen"][0]["bahnabstand"] = 30
    a["bearbeitungen"][0]["v_wand"] = "unten"
    erg = engine.rechnen(a)
    assert erg["ok"], erg
    assert engine.raeumbreite(a["werkzeuge"]["A"], 1, 1) == pytest.approx(0.2)
    assert any("nur die Spitze" in w for w in erg["warnungen"])
    # Rand: halbe Spitze vom Umriss weg (nicht 1,5 mm)
    p = erg["programme"][0]
    job = tap_mm.read(open(p["datei"], newline="").read())
    xs = [m.x for tp in job.toolpaths for m in tp.moves if not m.rapid and m.z < 0]
    assert min(xs) == pytest.approx(10.1, abs=0.02)


def test_tasche_restmaterial_mit_raeumwerkzeug(tmp_path):
    """Grosser Fräser räumt zuerst, der kleine fräst nur noch die Ecken."""
    from hirch_cam import engine
    a = _auftrag(tmp_path)
    a["bearbeitungen"] = [a["bearbeitungen"][0]]
    t = a["bearbeitungen"][0]
    t["tiefe"], t["zustellung"] = 2, 1
    t["raeum_werkzeug"] = "B"                       # Ø6 räumt, Ø3 (A) fräst den Rest
    erg = engine.rechnen(a)
    assert erg["ok"], erg
    ab = erg["ablauf"]["seiten"][0]["abschnitte"]
    assert [(x["slot"], [o["teil"] for o in x["ops"]]) for x in ab] == [("B", ["raeumen"]), ("A", [""])]
    assert ab[0]["ops"][0]["extra"] == "Räumen" and ab[1]["ops"][0]["extra"] == "Restmaterial"
    assert ab[0]["ops"][0]["n"] == 1                # Zustellung = halber Ø (3 mm) → 1 Durchgang
    # der kleine fräst nur noch in den Ecken: alle Schnitte nahe einer Ecke
    p_a = [p for p in erg["programme"] if p["t"] == 2][0]
    job = tap_mm.read(open(p_a["datei"], newline="").read())
    pts = [(m.x, m.y) for tp in job.toolpaths for m in tp.moves if not m.rapid and m.z < -0.01]
    assert pts
    ecken = [(10, 10), (30, 10), (30, 30), (10, 30)]
    assert all(min(abs(x - ex) + abs(y - ey) for ex, ey in ecken) < 4.5 for x, y in pts)
    # vorab (Panel) zeigt dieselbe Folge
    from hirch_cam import ablauf as abl
    v = abl.vorab(a)
    assert [(x["slot"], [o["teil"] for o in x["ops"]]) for x in v["seiten"][0]["abschnitte"]] == \
        [("B", ["raeumen"]), ("A", [""])]


def test_raeumen_mit_eigenen_werten(tmp_path):
    """Der Räum-Durchgang hat eigenen Vorschub und Bahnabstand."""
    from hirch_cam import engine
    a = _auftrag(tmp_path)
    a["bearbeitungen"] = [a["bearbeitungen"][0]]
    t = a["bearbeitungen"][0]
    t.update({"tiefe": 2, "zustellung": 1, "raeum_werkzeug": "B", "raeum_vorschub_prozent": 50,
              "raeum_bahnabstand": 80, "raeum_eintauchen_art": "senkrecht"})
    erg = engine.rechnen(a)
    assert erg["ok"], erg
    p1 = [p for p in erg["programme"] if p["t"] == 1][0]
    text = open(p1["datei"], newline="").read()
    assert "F750" in text and "F1500" not in text     # 50 % von 1500 mm/min
    p2 = [p for p in erg["programme"] if p["t"] == 2][0]
    assert "F1000" in open(p2["datei"], newline="").read()   # das kleine Werkzeug bleibt bei 100 %


def test_tasche_mit_stichel_schraege_wand_oben_auf_der_linie(tmp_path):
    """Brennstempel: die Flanke trifft oben genau die Linie, je tiefer, desto weiter innen;
    der grosse Fräser bleibt unten hinter der schrägen Wand."""
    import math
    from hirch_cam import engine
    a = _auftrag(tmp_path)
    a["werkzeuge"]["A"] = {"typ": "v", "durchmesser": 3.0, "v_winkel": 36, "spitze": 0.2, "nummer": 1,
                           "vorschub": 170, "eintauchen": 60}
    a["bearbeitungen"] = [a["bearbeitungen"][0]]
    t = a["bearbeitungen"][0]
    t.update({"tiefe": 2, "zustellung": 1, "bahnabstand": 30, "raeum_werkzeug": "B"})
    erg = engine.rechnen(a)
    assert erg["ok"], erg
    tan = math.tan(math.radians(18))
    p_v = [p for p in erg["programme"] if p["t"] == 2][0]
    job = tap_mm.read(open(p_v["datei"], newline="").read())
    for tiefe in (1, 2):
        xs = [m.x for tp in job.toolpaths for m in tp.moves if not m.rapid and abs(m.z + tiefe) < 1e-6]
        # Mitte der Spitze: Spitzenradius + Tiefe × tan(18°) vom Rand (x = 10)
        assert min(xs) == pytest.approx(10 + 0.1 + tiefe * tan, abs=0.02)
    p_g = [p for p in erg["programme"] if p["t"] == 1][0]
    job = tap_mm.read(open(p_g["datei"], newline="").read())
    xs = [m.x for tp in job.toolpaths for m in tp.moves if not m.rapid and m.z < 0]
    assert min(xs) == pytest.approx(10 + 3 + 2 * tan, abs=0.02)      # Ø 6 hinter der Wand unten
    assert any("oben genau auf der Linie" in w for w in erg["warnungen"])


def test_schraege_wand_randrunde_auch_unten_an_den_kanten(tmp_path):
    """Nach dem Räumen fährt der Stichel auf jeder Ebene die Randrunde – auch ganz
    unten mitten an einer Kante (nicht nur in den Ecken)."""
    import math
    from hirch_cam import engine
    a = _auftrag(tmp_path)
    a["werkzeuge"]["A"] = {"typ": "v", "durchmesser": 3.0, "v_winkel": 36, "spitze": 0.2, "nummer": 1,
                           "vorschub": 170, "eintauchen": 60}
    a["bearbeitungen"] = [a["bearbeitungen"][0]]
    a["bearbeitungen"][0].update({"tiefe": 1, "zustellung": 0.6, "bahnabstand": 30, "raeum_werkzeug": "B"})
    erg = engine.rechnen(a)
    p_v = [p for p in erg["programme"] if p["t"] == 2][0]
    job = tap_mm.read(open(p_v["datei"], newline="").read())
    x_rand = 10 + 0.1 + 1 * math.tan(math.radians(18))
    # gefräste Strecke auf voller Tiefe entlang der linken Kante (x fest)
    laenge = 0.0
    for tp in job.toolpaths:
        for m0, m1 in zip(tp.moves, tp.moves[1:]):
            if (not m1.rapid and abs(m0.z + 1) < 1e-6 and abs(m1.z + 1) < 1e-6
                    and abs(m0.x - x_rand) < 0.02 and abs(m1.x - x_rand) < 0.02):
                laenge += abs(m1.y - m0.y)
    assert laenge > 15


def test_raeumwerkzeug_muss_groesser_sein(tmp_path):
    """Sicherung: ein Räumwerkzeug, das nicht grösser ist, wird nicht verwendet."""
    from hirch_cam import engine, ablauf as abl
    a = _auftrag(tmp_path)
    a["bearbeitungen"] = [a["bearbeitungen"][0]]
    a["bearbeitungen"][0].update({"werkzeug": "B", "raeum_werkzeug": "A"})   # Ø3 «räumt» vor Ø6
    erg = engine.rechnen(a)
    assert erg["ok"], erg
    assert [p["t"] for p in erg["programme"]] == [1]
    assert any("nicht grösser" in w for w in erg["warnungen"])
    v = abl.vorab(a)
    assert [x["slot"] for x in v["seiten"][0]["abschnitte"]] == ["B"]


def test_vcarve_schmaler_boden_ohne_hinweis(tmp_path):
    """Ein schmaler Boden (unter 2 mm) bringt keinen Hinweis auf einen Schaftfräser,
    eine breite Fläche schon."""
    from hirch_cam import engine
    a = _auftrag(tmp_path)
    a["werkzeuge"]["A"] = {"typ": "v", "durchmesser": 6.0, "v_winkel": 60, "nummer": 1,
                           "vorschub": 1000, "eintauchen": 300}
    def lauf(breite):
        a["bearbeitungen"] = [{"id": "v", "art": "vcarve", "name": "V", "werkzeug": "A", "max_tiefe": 0.5,
                               "objekte": [{"punkte": [[10, 10], [40, 10], [40, 10 + breite], [10, 10 + breite],
                                                       [10, 10]], "geschlossen": True}]}]
        erg = engine.rechnen(a)
        assert erg["ok"], erg
        return [w for w in erg["warnungen"] if "Schaftfräser" in w]
    assert lauf(1.2) == []          # Boden ca. 0,6 mm breit
    assert lauf(8) != []            # Boden ca. 7 mm breit


def test_vcarve_zackiger_umriss_ohne_staendiges_abheben():
    """Winzige Zacken im Umriss (gerundete Koordinaten) dürfen keine Seitenäste
    geben, für die der Fräser jedes Mal hoch und wieder hinunter fährt."""
    import numpy as np
    pytest.importorskip("matplotlib")
    from matplotlib.textpath import TextPath
    from matplotlib.font_manager import FontProperties
    from hirch_cam.ops.vcarve import vcarve
    tp_s = TextPath((0, 0), "S", size=20, prop=FontProperties(family="DejaVu Sans"))
    linien, cur = [], []
    for bez, code in tp_s.iter_bezier():
        if code == 1:
            if len(cur) >= 4:
                linien.append(cur)
            cur = [tuple(bez.control_points[0])]
            continue
        for t in np.linspace(0, 1, (1 if code == 2 else 40) + 1)[1:]:
            cur.append(tuple(bez(t)) if callable(bez) else tuple(bez.point_at_t(t)))
    if len(cur) >= 4:
        linien.append(cur)
    rau = 0.005
    linien = [([(x + rau * ((i % 2) * 2 - 1), y + rau * (((i // 2) % 2) * 2 - 1)) for i, (x, y) in enumerate(p)]
               + [(p[0][0] - rau, p[0][1] - rau)], True) for p in linien]
    w = {"typ": "v", "durchmesser": 6, "v_winkel": 60, "vorschub": 1000, "eintauchen": 300}
    tp, _w, _e = vcarve(linien, w, {"max_tiefe": 5})
    abheben = sum(1 for a, b in zip(tp.moves, tp.moves[1:]) if a.z < 0 < b.z)
    assert abheben <= 3


def test_vcarve_buchstabe_fuer_buchstabe():
    """V-Carve fräst jede zusammenhängende Fläche fertig, bevor es weitergeht."""
    from hirch_cam.ops.vcarve import vcarve
    from shapely.geometry import Point, Polygon
    # «T» aus zwei Balken, daneben zwei Quadrate
    t = [(0, 20), (14, 20), (14, 17), (8.5, 17), (8.5, 0), (5.5, 0), (5.5, 17), (0, 17), (0, 20)]
    q1 = [(20, 0), (26, 0), (26, 6), (20, 6), (20, 0)]
    q2 = [(-12, 14), (-6, 14), (-6, 20), (-12, 20), (-12, 14)]
    w = {"typ": "v", "durchmesser": 6, "v_winkel": 60, "vorschub": 1000, "eintauchen": 300}
    tp, _w, _e = vcarve([(t, True), (q1, True), (q2, True)], w, {"max_tiefe": 1.2})
    flaechen = [Polygon(t), Polygon(q1), Polygon(q2)]
    folge = []
    for m in tp.moves:
        if m.rapid or m.z >= 0:
            continue
        i = min(range(3), key=lambda k: flaechen[k].distance(Point(m.x, m.y)))
        if not folge or folge[-1] != i:
            folge.append(i)
    assert sorted(folge) == [0, 1, 2]          # jede Fläche genau einmal an der Reihe


def test_tasche_flaeche_fuer_flaeche_ueber_alle_ebenen():
    """Getrennte Flächen einer Tasche: jede ganz fertig (alle Ebenen), dann die nächste."""
    from hirch_cam.ops.tasche import tasche
    a = [(0, 0), (10, 0), (10, 10), (0, 10), (0, 0)]
    b = [(30, 0), (40, 0), (40, 10), (30, 10), (30, 0)]
    tp, _w = tasche([(a, True), (b, True)], 3, 3, 1, 1000, 300)
    folge = []
    for m in tp.moves:
        if not m.rapid and m.z < 0:
            i = 0 if m.x < 20 else 1
            if not folge or folge[-1] != i:
                folge.append(i)
    assert folge == [0, 1]


def test_vorschub_in_mm_pro_minute():
    """Vorschub der Bearbeitung direkt in mm/min (statt Prozent vom Werkzeug)."""
    from hirch_cam import engine
    import tempfile, pathlib
    a = _auftrag(pathlib.Path(tempfile.mkdtemp()))
    a["bearbeitungen"] = [a["bearbeitungen"][0]]
    a["bearbeitungen"][0]["vorschub_mm"] = 640
    erg = engine.rechnen(a)
    text = open(erg["programme"][0]["datei"], newline="").read()
    assert "F640" in text and "F1000" not in text


def test_sinnvoll_ordnen_und_alte_zeichnung():
    """Keine automatische Sortierung mehr: alte Zeichnungen werden einmal geordnet,
    danach gilt die Liste."""
    e = einst.laden(json.dumps({"bearbeitungen": [
        {"art": "kontur", "werkzeug": "C"}, {"art": "tasche", "werkzeug": "B"},
        {"art": "gravur", "werkzeug": "A"}, {"art": "planen", "werkzeug": "C"},
        {"art": "bohren", "werkzeug": "B"}]}))
    assert [b["art"] for b in e["bearbeitungen"]] == ["planen", "tasche", "bohren", "gravur", "kontur"]
    assert e["reihenfolge"] == "liste"
    # in der neuen Form gespeichert: die Liste bleibt, wie sie ist
    e2 = einst.laden(json.dumps({"reihenfolge": "liste", "bearbeitungen": [
        {"art": "kontur"}, {"art": "tasche"}]}))
    assert [b["art"] for b in e2["bearbeitungen"]] == ["kontur", "tasche"]


def test_schrift_wie_gefuellt():
    """Schriften mit überlappenden/sich kreuzenden Umrissen: Fläche wie in
    Inkscape (nonzero) – nichts fällt weg, keine Selbstkreuzung mehr."""
    pytest.importorskip("shapely")
    from shapely.geometry import Polygon
    from hirch_cam.svg_lesen import wie_gefuellt
    # zwei überlappende Teile desselben Buchstabens (gleiche Drehrichtung)
    a = [(0, 0), (10, 0), (10, 10), (0, 10), (0, 0)]
    b = [(5, 5), (15, 5), (15, 15), (5, 15), (5, 5)]
    aus = wie_gefuellt([(a, True), (b, True)])
    assert len(aus) == 1
    assert Polygon(aus[0][0]).area == pytest.approx(175)
    # «o»: Loch gegen die Drehrichtung bleibt Loch, unverändert
    loch = [(3, 3), (3, 7), (7, 7), (7, 3), (3, 3)]
    assert wie_gefuellt([(a, True), (loch, True)]) == [(a, True), (loch, True)]
    # Ende eines Bogens, das über den eigenen Umriss hinausläuft (Schleife)
    schleife = [(0, 0), (10, 0), (10, 6), (12, 8), (14, 6), (8, 4), (0, 4), (0, 0)]
    aus = wie_gefuellt([(schleife, True)])
    flaeche = sum(Polygon(p).area for p, _ in aus)
    assert all(Polygon(p).is_valid for p, _ in aus)
    assert flaeche >= Polygon(schleife).buffer(0).area - 1e-6
    # offene Linien bleiben
    assert wie_gefuellt([([(0, 0), (5, 5)], False)]) == [([(0, 0), (5, 5)], False)]
