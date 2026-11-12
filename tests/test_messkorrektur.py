"""Messkorrektur: Testteil in X und Y ausmessen, Fräser-Zugabe je Fräser, Weg der Achsen (Sammelauftrag 21.10.)."""
import json
import re
import xml.etree.ElementTree as ET

import pytest

from hirch_cam import engine, messkorrektur, werkzeugdb
from hirch_cam.post import tap_mm


def test_formeln_je_achse():
    # Fräser schneidet 0,05 grösser, Achsen stimmen: Quadrat 0,1 kleiner, Loch 0,1 grösser
    r = messkorrektur.rechnen(40, 20, 39.9, 39.9, 20.1, 20.1)
    assert r["s_x"] == pytest.approx(1) and r["s_y"] == pytest.approx(1)
    assert r["e"] == pytest.approx(0.05) and r["warnungen"] == []
    # Y fährt 0,2 % zu kurz, Fräser stimmt: alles in Y 0,2 % kleiner
    s = 0.998
    r = messkorrektur.rechnen(40, 20, 40, 40 * s, 20, 20 * s)
    assert r["s_x"] == pytest.approx(1) and r["s_y"] == pytest.approx(s)
    assert r["e_y"] == pytest.approx(0, abs=1e-9)
    assert "Schritte pro mm" in messkorrektur.achse_hinweis("Y", r["s_y"])
    # beides zusammen, und bisherige Korrektur wird eingerechnet
    r = messkorrektur.rechnen(100, 20, 100 * s - 0.1, 100 - 0.1, 20 * s + 0.1, 20 + 0.1, bisher=-0.05)
    assert r["s_x"] == pytest.approx(s) and r["e_x"] == pytest.approx(0.05)
    assert r["neu"] == pytest.approx(0.0)


def test_warnungen():
    assert "spiel" in messkorrektur.rechnen(40, 20, 39.9, 39.8, 20.1, 20.25)["warnungen"]
    assert "tippfehler" in messkorrektur.rechnen(40, 20, 39.9, 39.9, 21.1, 20.1)["warnungen"]
    assert "unrund" in messkorrektur.rechnen(40, 20, 39.9, 39.9, 20.1, 20.1, diagonal=20.2)["warnungen"]
    with pytest.raises(ValueError):
        messkorrektur.rechnen(40, 20, 0, 39.9, 20.1, 20.1)


def _auftrag(tmp_path, art, werkzeug, maschine=None):
    q = [[10, 10], [50, 10], [50, 40], [10, 40]]
    b = {"id": "k", "art": art, "name": art, "werkzeug": "A", "tiefe": 1.0, "zustellung": 1.0,
         "objekte": [{"punkte": q, "geschlossen": True}], "eintauchen_art": "senkrecht"}
    if art == "kontur":
        b.update(seite="aussen", stege_anzahl=0)
    w = dict({"typ": "schaft", "durchmesser": 4, "nummer": 1, "vorschub": 1000, "eintauchen": 300}, **werkzeug)
    return {"werkzeuge": {"A": w},
            "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 5},
            "maschine": maschine or {}, "postprozessor": "vcarve_mm",
            "ausgabe": str(tmp_path / "t.tap"), "bearbeitungen": [b]}


def _masse(erg):
    job = tap_mm.read(open(erg["programme"][0]["datei"], newline="").read())
    xs = [m.x for tp in job.toolpaths for m in tp.moves if not m.rapid and m.z < 0]
    return max(xs) - min(xs)


@pytest.mark.parametrize("art,richtung", [("kontur", 1), ("tasche", -1)])
def test_fraeser_zugabe_wirkt_wie_aufmass(tmp_path, art, richtung):
    a = _masse(engine.rechnen(_auftrag(tmp_path, art, {})))
    b = _masse(engine.rechnen(_auftrag(tmp_path, art, {"messkorrektur": 0.05})))
    # Fräser schneidet 0,05 grösser: Kontur aussen 0,05 weiter weg, Tasche 0,05 weniger weit
    assert b - a == pytest.approx(richtung * 0.1, abs=0.01)


def test_weg_der_achsen_im_g_code(tmp_path):
    ma = {"skalierung_an": 1.0, "skalierung_x": 1.0, "skalierung_y": 0.998}
    roh = engine.rechnen(_auftrag(tmp_path, "kontur", {}))
    roh_text = open(roh["dateien"][0]).read()
    erg = engine.rechnen(_auftrag(tmp_path, "kontur", {}, ma))
    text = open(erg["dateien"][0]).read()
    ys = lambda t: [float(v) for v in re.findall(r"Y(-?\d+\.\d+)", t)]
    xs = lambda t: [float(v) for v in re.findall(r"X(-?\d+\.\d+)", t)]
    assert max(xs(text)) == pytest.approx(max(xs(roh_text)), abs=1e-3)
    assert max(ys(text)) == pytest.approx(max(ys(roh_text)) / 0.998, abs=2e-3)
    assert any("Weg der Achsen ausgeglichen" in z for z in erg["bericht"])
    # ab Werk aus
    aus = engine.rechnen(_auftrag(tmp_path, "kontur", {}, dict(ma, skalierung_an=0.0)))
    assert max(ys(open(aus["dateien"][0]).read())) == pytest.approx(max(ys(roh_text)), abs=1e-3)


def test_speichern_beim_fraeser_und_maschine(tmp_path):
    datei = str(tmp_path / "w.json")
    w = werkzeugdb.speichern({"name": "Test 3", "typ": "schaft", "durchmesser": 3, "schneiden": 2}, datei)
    werkzeugdb.maschine_setzen({"masskorrektur": -0.05}, datei)
    messkorrektur.speichern({"id": w["id"], "messkorrektur": 0.0612, "info": {"datum": "2026-10-21", "quadrat": 40},
                             "s_x": 1.0, "s_y": 0.9985, "skalierung_an": False, "alte_null": True}, datei)
    db = werkzeugdb.laden(datei)
    neu = [x for x in db["werkzeuge"] if x["id"] == w["id"]][0]
    assert neu["messkorrektur"] == 0.0612 and neu["messkorrektur_info"]["quadrat"] == 40
    assert db["maschine"]["masskorrektur"] == 0.0
    assert db["maschine"]["skalierung_y"] == pytest.approx(0.9985) and db["maschine"]["skalierung_an"] == 0.0
    # zurücksetzen
    messkorrektur.speichern({"id": w["id"], "messkorrektur": 0.0}, datei)
    neu = [x for x in werkzeugdb.laden(datei)["werkzeuge"] if x["id"] == w["id"]][0]
    assert "messkorrektur" not in neu


def test_testteil_zeichnung():
    w = {"name": "Schaftfräser 3 mm", "typ": "schaft", "durchmesser": 3.0, "schneiden": 2, "db_id": "w7"}
    svg = messkorrektur.testteil_svg(100, 20, w, "hartholz")
    root = ET.fromstring(svg)
    e = json.loads(root.get("{https://hirch.net/cnc}einstellungen"))
    assert e["werkzeuge"]["A"]["db_id"] == "w7" and e["material"] == "hartholz"
    seiten = sorted(b["seite"] for b in e["bearbeitungen"])
    assert seiten == ["aussen", "innen"] and all(b["werkzeug"] == "A" for b in e["bearbeitungen"])
    assert root.get("width") == "200mm"
    farben = {b["seite"]: b["farbe"] for b in e["bearbeitungen"]}
    pfad = root.find(".//{http://www.w3.org/2000/svg}path")
    kreis = root.find(".//{http://www.w3.org/2000/svg}circle")
    assert farben["aussen"] in pfad.get("style") and farben["innen"] in kreis.get("style")
    assert float(kreis.get("r")) == 10
