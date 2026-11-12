"""Umspannen (Basis → Fräsbahnen 27.10. 13:10, Befund B-5): liegen Spannmittel über Fräsbahnen, je Seite zwei
Programme – Spannung 1 fräst alles ausser den Sperrzonen, Spannung 2 den Rest. Prüfsteine: beide zusammen tragen
genau so viel ab wie ein Programm ohne Spannmittel, kein Eilgang ins Material (feiner Prüfer über beide), in
Spannung 1 nichts in den Zonen, ohne Umspannen G-Code unverändert."""
import math
import os
import sys

import numpy as np
import pytest

pytest.importorskip("shapely")
sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "werkzeuge"))
import eilgang_pruefen as ep  # noqa: E402

from hirch_cam import engine  # noqa: E402
from hirch_cam.ops import umspannen as U  # noqa: E402
from hirch_cam.post import tap_mm  # noqa: E402
from hirch_cam.simulation import Hoehenfeld  # noqa: E402

B, H = 150, 100


def _rechteck(x0, y0, x1, y1):
    return [[x0, y0], [x1, y0], [x1, y1], [x0, y1], [x0, y0]]


def _kreis(cx, cy, r, n=48):
    return [[cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n)] for i in range(n + 1)]


def _auftrag(tmp_path, name, umspannen=None, spannmittel=True, post="vcarve_mm", zweite=False):
    ordner = tmp_path / name
    ordner.mkdir(exist_ok=True)
    spann = [{"punkte": _rechteck(130, 20, 160, 40), "hoehe": 12, "seite": 1},
             {"punkte": _rechteck(-10, 60, 12, 80), "hoehe": 6, "seite": 1}] if spannmittel else []
    if zweite:
        spann.append({"punkte": _rechteck(60, -5, 80, 3), "hoehe": 15, "seite": 1, "spannung": 2})
    a = {
        "werkzeuge": {"C": {"typ": "schaft", "durchmesser": 6, "nummer": 1, "vorschub": 1000, "eintauchen": 300},
                      "D": {"typ": "schaft", "durchmesser": 3, "nummer": 2, "vorschub": 800, "eintauchen": 200,
                            "mittenschnitt": False}},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 8},
        "postprozessor": post, "material_flaeche": [B, H], "spannmittel": spann,
        "bearbeitungen": [
            {"id": "pl", "art": "planen", "werkzeug": "C", "tiefe": 0.5, "zustellung": 0.5,
             "objekte": [{"punkte": _rechteck(0, 0, B, H), "geschlossen": True}]},
            {"id": "g", "art": "gravur", "werkzeug": "D", "tiefe": 1, "zustellung": 0.5,
             "objekte": [{"punkte": [[20, 70], [140, 70]], "geschlossen": False},
                         {"punkte": [[20, 50], [140, 30]], "geschlossen": False}]},
            {"id": "t", "art": "tasche", "werkzeug": "D", "tiefe": 2, "zustellung": 1,
             "objekte": [{"punkte": _rechteck(110, 10, 145, 50), "geschlossen": True}]},
            {"id": "b", "art": "bohren", "werkzeug": "C", "tiefe": 4, "zustellung": 2, "methode": "helix",
             "objekte": [{"punkte": _kreis(15, 85, 5), "geschlossen": True},
                         {"punkte": _kreis(60, 85, 5), "geschlossen": True}]},
            {"id": "k", "art": "kontur", "werkzeug": "C", "tiefe": 8.2, "zustellung": 2, "seite": "aussen",
             "durch": True, "stege_anzahl": 4, "stege_breite": 6, "stege_hoehe": 1.5, "anfahren": "bogen",
             "objekte": [{"punkte": _rechteck(5, 5, 145, 95), "geschlossen": True}]}],
        "ausgabe": str(ordner / "teil.tap")}
    if umspannen is not None:
        a["umspannen"] = umspannen
    return a


def _feld(erg, nur=None):
    f = Hoehenfeld(-20, -20, B + 20, H + 20, 0.1)
    for p in erg["programme"]:
        if nur is not None and int(p.get("spannung") or 1) != nur:
            continue
        with open(p["datei"], newline="") as fh:
            job = tap_mm.read(fh.read())
        f.fraesen(job, p.get("form") or "flach", float(p["durchmesser"]), 60)
    return f.H


def _ohne_kanten(H0, d, tol=0.05):
    """Zellen mit Unterschied, die nicht am Rand eines Schnitts liegen (dort kippt die Rasterung einer Zelle)."""
    from scipy import ndimage
    rand = (ndimage.maximum_filter(H0, 3) - ndimage.minimum_filter(H0, 3)) > tol
    return int(((np.abs(d) > tol) & ~rand).sum())


def _zeilen(erg):
    return {os.path.basename(d): open(d, newline="").read() for d in erg["dateien"]}


def test_aus_aendert_nichts(tmp_path):
    a = engine.rechnen(_auftrag(tmp_path, "a"))
    b = engine.rechnen(_auftrag(tmp_path, "b", umspannen={"an": False, "abstand": 10}))
    assert _zeilen(a) == _zeilen(b)
    assert "umspannen" not in b and all(p["spannung"] == 1 for p in b["programme"])


@pytest.mark.parametrize("post", ["vcarve_mm", "mach3_mm"])
def test_zwei_spannungen_tragen_genau_so_viel_ab(tmp_path, post):
    ohne = engine.rechnen(_auftrag(tmp_path, "ohne", spannmittel=False, post=post))
    a = _auftrag(tmp_path, "mit", umspannen={"an": True, "abstand": 10}, post=post)
    mit = engine.rechnen(a)
    assert ohne["ok"] and mit["ok"]
    sp = [int(p["spannung"]) for p in mit["programme"]]
    assert sp == sorted(sp) and set(sp) == {1, 2}            # erst alles von Spannung 1, dann Spannung 2
    namen = [os.path.basename(d) for d in mit["dateien"]]
    if post == "mach3_mm":
        assert namen == ["teil_spannung1.tap", "teil_spannung2.tap"]
    else:
        n1 = sp.count(1)                 # Spannung 1: Planen (Ø 6) zuerst, dann Ø 3, dann Ø 6 (04.11.)
        assert all("_spannung1_" in n for n in namen[:n1]) and all("_spannung2_" in n for n in namen[n1:])
    # zusammen derselbe Abtrag wie ohne Spannmittel
    H0 = _feld(ohne)
    assert _ohne_kanten(H0, _feld(mit) - H0) == 0
    # kein Eilgang ins Material, über beide Programme in der Reihenfolge des G-Codes
    assert ep.pruefen(mit) == []
    # Spannung 1: kein Fräsweg in einer Sperrzone (unter der Oberfläche)
    from shapely.geometry import LineString, Point
    for p in mit["programme"]:
        if p["spannung"] != 1:
            continue
        z = U.zone(U.spannmittel(a, 1, 1), float(p["durchmesser"]) / 2, 10.0).buffer(-0.01)
        with open(p["datei"], newline="") as fh:
            job = tap_mm.read(fh.read())
        pos = (job.home[0], job.home[1], job.safe_z)
        for tp in job.toolpaths:
            for m in tp.moves:
                if not m.rapid and min(pos[2], m.z) < 0:
                    g = LineString([pos[:2], (m.x, m.y)]) if math.dist(pos[:2], (m.x, m.y)) > 1e-9 \
                        else Point(m.x, m.y)
                    assert not g.intersects(z), (p["name"], pos, m)
                pos = (m.x, m.y, m.z)
    # Spannung 2 ohne eingezeichnete zweite Lage: waagrechte Eilgänge ausserhalb der Zonen mindestens auf
    # höchstem Spannmittel + Abstand (12 + 10)
    for p in mit["programme"]:
        if p["spannung"] != 2:
            continue
        z = U.zone(U.spannmittel(a, 1, 1), float(p["durchmesser"]) / 2, 10.0).buffer(1e-6)
        with open(p["datei"], newline="") as fh:
            job = tap_mm.read(fh.read())
        pos = (job.home[0], job.home[1], job.safe_z)
        for tp in job.toolpaths:
            for m in tp.moves:
                if m.rapid and math.dist(pos[:2], (m.x, m.y)) > 1e-6 and min(pos[2], m.z) < 22 - 1e-6:
                    assert z.contains(LineString([pos[:2], (m.x, m.y)])), (p["name"], pos, m)
                pos = (m.x, m.y, m.z)
    # Loch in der Zone ganz in Spannung 2, das andere ganz in Spannung 1
    keys = {p["spannung"]: [s["key"] for s in p["abschnitte"]] for p in mit["programme"] if p["name"].endswith("6 mm")}
    assert "b" in keys[1] and "b@2" in keys[2]
    # Ergebnis für die Oberfläche
    u = mit["umspannen"]
    assert [x["seite"] for x in u] == [1] and u[0]["zeit"] > 0 and u[0]["zonen_mm"]
    assert set(u[0]["bearbeitungen"]) >= {"t", "k", "b", "g"}


def test_zweite_lage_eingezeichnet(tmp_path):
    """Mit eingezeichneter zweiter Lage gilt sie als Hindernis in Spannung 2 (Eilgänge darüber), die Höhenregel
    «höchstes Spannmittel + Abstand» entfällt; liegt sie, wo Spannung 2 fräst, kommt ein Hinweis."""
    a = _auftrag(tmp_path, "zw", umspannen={"an": True, "abstand": 10}, zweite=True)
    mit = engine.rechnen(a)
    assert mit["ok"] and ep.pruefen(mit) == []
    assert engine.UMSPANNEN_IN_ZONE not in mit["warnungen"]
    a["spannmittel"].append({"punkte": _rechteck(125, 25, 135, 30), "hoehe": 15, "seite": 1, "spannung": 2})
    a["ausgabe"] = str(tmp_path / "zw" / "b.tap")
    assert engine.UMSPANNEN_IN_ZONE in engine.rechnen(a)["warnungen"]


def test_kontur_ohne_stege_frei_erst_nach_spannung_2(tmp_path):
    a = _auftrag(tmp_path, "k", umspannen={"an": True, "abstand": 10})
    k = next(b for b in a["bearbeitungen"] if b["id"] == "k")
    k.update(stege_anzahl=0, stege_modus="keine")
    erg = engine.rechnen(a)
    assert engine.UMSPANNEN_FREI in erg["op_warnungen"]["k"]
    # in der Ablaufliste nur in der Zeile «Spannung 2» (Basis 27.10. 15:15), nicht bei Spannung 1
    zeilen = [o for se in erg["ablauf"]["seiten"] for ab in se["abschnitte"] for o in ab["ops"] if o["id"] == "k"]
    assert {o["key"].endswith("@2") for o in zeilen} == {True, False}
    for o in zeilen:
        assert (engine.UMSPANNEN_FREI in o["warnungen"]) == o["key"].endswith("@2"), o["key"]


def test_teilen_ueberlappt_einen_millimeter():
    """Ein Zug quer durch die Zone: Spannung 1 hebt davor ab und taucht dahinter ein, Spannung 2 beginnt und endet
    1 mm auf dem schon Gefrästen."""
    from shapely.geometry import Point
    from hirch_cam.model import Move, Toolpath
    zone = Point(50, 0).buffer(10)
    tp = Toolpath(moves=[Move(True, 0, 0, 3), Move(True, 0, 0, 0.5), Move(False, 0, 0, -2, 300),
                         Move(False, 100, 0, -2, 1000), Move(True, 100, 0, 3)])
    t1, t2 = U.teilen(tp, zone, 3.0, 300, (0, 0, 20))
    x1 = [round(m.x, 3) for m in t1.moves if not m.rapid and abs(m.z + 2) < 1e-9]
    assert 40.0 in x1 and 60.0 in x1 and not any(40.01 < x < 59.99 for x in x1)
    fr = [m for m in t2.moves if not m.rapid]
    assert min(m.x for m in fr) == pytest.approx(39.0) and max(m.x for m in fr) == pytest.approx(61.0)


def test_bohrzyklen_je_loch(tmp_path):
    """Bohren mit Bohrzyklus: das Loch in der Zone ganz in Spannung 2, das andere bleibt in Spannung 1."""
    def auftrag(name, spann):
        a = _auftrag(tmp_path, name, umspannen={"an": True, "abstand": 10}, spannmittel=spann)
        a["bearbeitungen"] = [{"id": "b", "art": "bohren", "werkzeug": "C", "tiefe": 4, "zustellung": 2,
                               "methode": "bohren",
                               "objekte": [{"punkte": _kreis(15, 85, 3.0), "geschlossen": True},
                                           {"punkte": _kreis(60, 85, 3.0), "geschlossen": True},
                                           {"punkte": _kreis(100, 60, 3.0), "geschlossen": True}]}]
        return a
    ohne = engine.rechnen(auftrag("ohne", False))
    mit = engine.rechnen(auftrag("mit", True))
    assert [p["spannung"] for p in mit["programme"]] == [1, 2]
    H0 = _feld(ohne)
    assert _ohne_kanten(H0, _feld(mit) - H0) == 0
    assert ep.pruefen(mit) == []
    unten = []
    for p in mit["programme"]:
        with open(p["datei"], newline="") as fh:
            job = tap_mm.read(fh.read())
        unten.append(sorted({(round(m.x), round(m.y)) for tp in job.toolpaths for m in tp.moves if m.z < -3.9}))
    assert unten == [[(60, 85), (100, 60)], [(15, 85)]]


def test_3d_schruppen_boeden_schlichten(tmp_path):
    """3D: Schruppen + «Böden und Wände» im Wechsel + Schlichten «Konturen je Form», Pratze über einer Ecke des
    Modells – beide Spannungen zusammen wie ohne, kein Eilgang ins Material."""
    pytest.importorskip("hirch_cam.hirch_relief", reason="hirch_relief nicht gebaut")
    pytest.importorskip("inkex")
    import html
    import json
    import re
    from test_relief3d import zeichnung
    from hirch_cam import dokument, hirch_relief, speicher
    if not hirch_relief.mit_ocl():
        pytest.skip("OpenCAMLib fehlt")
    from test_messwerte_3d import MATERIAL, pruefmodell
    b_, h_, _d = MATERIAL
    bea = [{"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 3, "aufmass": 0.5},
           {"id": "bw", "art": "boeden_waende", "werkzeug": "C", "stufe": 6, "im_wechsel_mit": "s1"},
           {"id": "f1", "art": "relief_schlichten", "werkzeug": "K", "bahnabstand": 15, "strategie": "konturen",
            "auslassen_fertig": True}]

    def rechnen(name, um):
        t = tmp_path / name
        t.mkdir()
        el = (f'<rect x="-5" y="{h_ - 18}" width="20" height="23" hirch:spannmittel="10" style="fill:#888"/>'
              if um else "")
        svg = zeichnung(t, bea, material=MATERIAL, dreiecke=pruefmodell(), elemente=el)
        s = open(svg, encoding="utf-8").read()
        m = re.search(r'hirch:einstellungen=("([^"]*)"|\'([^\']*)\')', s)
        e = json.loads(html.unescape(m.group(2) if m.group(2) is not None else m.group(3)))
        if um:
            e["umspannen"] = {"an": True, "abstand": 10}
        s = s.replace(m.group(0), 'hirch:einstellungen="%s"' % html.escape(json.dumps(e), quote=True))
        open(svg, "w", encoding="utf-8").write(s)
        speicher.SPEICHER.leeren()
        return dokument.rechnen(svg, str(t / "aus"))

    def feld(erg):
        f = Hoehenfeld(-10, -10, b_ + 10, h_ + 10, 0.1)
        for p in erg["programme"]:
            with open(p["datei"], newline="") as fh:
                job = tap_mm.read(fh.read())
            form = p.get("form") or "flach"
            d = float(p["durchmesser"])
            f.fraesen(job, form, d, 60, eckradius=d / 2 if form == "kugel" else 0)
        return f.H
    ohne, mit = rechnen("ohne", False), rechnen("mit", True)
    assert ohne["ok"] and mit["ok"]
    assert {p["spannung"] for p in mit["programme"]} == {1, 2}
    H0 = feld(ohne)
    assert _ohne_kanten(H0, feld(mit) - H0) == 0
    assert ep.pruefen(mit, aussen_frei=(0, 0, b_, h_)) == []
    # 05.11.: der Hinweis «Spannung 2: … auf {} mm» (geschätzte Höhe) ist gewollt
    assert not [x for x in mit.get("pruefung", []) if not x["text"].startswith("Spannung 2:") and ("Spannmittel" in x["text"] or "Eilgang" in x["text"])]
    assert mit["umspannen"][0]["zonen"]                      # in SVG-Einheiten für die Zeichenfläche


def test_spannmittel_im_weg_bietet_umspannen_an(tmp_path):
    """Peter 09.11.: fährt ein Weg in ein Spannmittel, bietet die Meldung gleich «Umspannen einschalten» an –
    aber nur, solange Umspannen aus ist."""
    from hirch_cam import pruefung
    a = _auftrag(tmp_path, "aus")
    befunde = pruefung.pruefen(a, engine.rechnen(a))
    treffer = [x for x in befunde if "fährt in ein Spannmittel" in x["text"]]
    assert treffer and all(x.get("aktion") == "umspannen" for x in treffer)
    a = _auftrag(tmp_path, "an", umspannen={"an": True, "abstand": 10})
    assert not [x for x in pruefung.pruefen(a, engine.rechnen(a)) if x.get("aktion") == "umspannen"]
