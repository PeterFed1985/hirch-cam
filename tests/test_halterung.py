"""Spindel und Halterung: Kollisionsprüfung beim 3D-Fräsen (ops/relief_halterung.py)."""
import json
import math

import pytest

pytest.importorskip("numpy")

from hirch_cam.model import Move, Toolpath  # noqa: E402
from hirch_cam.ops import relief_halterung as rh  # noqa: E402

MASCHINE = {"mutter_hoehe": 18, "mutter_d": 25, "spindel_d": 65, "spindel_laenge": 120,
            "halter_b": 100, "halter_t": 80}
SCHAFT6 = {"typ": "schaft", "durchmesser": 6.0, "schneidenlaenge": 20.0, "schaft": 6.0}


def auftrag(maschine=MASCHINE, flaeche=(100, 100), dicke=60):
    return {"maschine": dict(maschine), "material_flaeche": list(flaeche),
            "hoehen": {"materialdicke": dicke, "sicherheitshoehe": 20}}


def loch(x=50, y=50, tiefe=40, start=5):
    """Senkrecht ins Material, dann eine kleine Runde auf der Tiefe."""
    return Toolpath("loch", [Move(True, x, y, start), Move(False, x, y, -tiefe, 300),
                             Move(False, x + 2, y, -tiefe, 800), Move(True, x + 2, y, start)])


def pruefen(tp, w, auf=None, geprueft=("b1",)):
    teile = [{"key": "b1", "slot": "C", "seite": 1, "tp": tp, "bid": "b1"}]
    folge = [{"seite": 1, "slot": "C", "keys": ["b1"]}]
    return rh.pruefen(teile, folge, {"C": w}, auf or auftrag(), geprueft)


def test_koerper_und_ausspannlaenge():
    k = {t["teil"]: t for t in rh.koerper(SCHAFT6, MASCHINE)}
    assert rh.ausspannlaenge(SCHAFT6) == 25.0                  # Schneidenlänge + 5 mm
    assert "schaft" not in k                                    # Schaft nicht dicker als die Schneide
    assert k["mutter"]["unten"] == 25 and k["mutter"]["r"] == 12.5
    assert k["spindel"]["unten"] == 43 and k["spindel"]["r"] == 32.5
    assert k["halterung"]["unten"] == 163 and (k["halterung"]["bx"], k["halterung"]["by"]) == (50, 40)
    k = {t["teil"]: t for t in rh.koerper(dict(SCHAFT6, ausspannlaenge=40), {})}
    assert list(k) == []                                        # ohne Masse: nichts zu prüfen
    kugel = {"typ": "kugel", "durchmesser": 3, "schaft": 6, "schneidenlaenge": 0, "ausspannlaenge": 30}
    ringe = [t for t in rh.koerper(kugel, {}) if t["teil"] == "schaft"]
    assert ringe[0]["unten"] == 1.5 and ringe[-1]["r"] == 3       # über dem Kugelprofil, Kegel bis zum Schaft


def test_uebergang_zum_schaft():
    """Kegel zwischen Schneide und Schaft: 20° zur Achse bei dickerem Schaft, 45° beim Planfräser;
    zählt zur Ausspannlänge und zur Kollisionsprüfung."""
    klein = {"typ": "schaft", "durchmesser": 1.0, "schaft": 3.0, "schneidenlaenge": 3.0}
    assert rh.uebergang(klein) == pytest.approx(1 / math.tan(math.radians(20)))
    assert rh.ausspannlaenge(klein) == pytest.approx(3 + 2.747 + 5, abs=0.01)
    plan = {"typ": "schaft", "durchmesser": 32.0, "schaft": 8.0, "schneidenlaenge": 6.0}
    assert rh.uebergang(plan) == pytest.approx(12.0)
    assert rh.ausspannlaenge(plan) == pytest.approx(23.0)           # statt 0 bzw. 11 mm
    assert rh.uebergang(dict(plan, uebergang=4)) == 4                # eingetragen
    assert rh.uebergang(SCHAFT6) == 0 and rh.uebergang({"typ": "v", "durchmesser": 3, "schaft": 6}) == 0
    ringe = [t for t in rh.koerper(dict(klein, ausspannlaenge=20), {}) if t["teil"] == "schaft"]
    assert len(ringe) == 6 and ringe[0]["unten"] == 3.0
    assert ringe[0]["r"] > 0.5 and ringe[-1]["r"] == pytest.approx(1.5)
    assert all(a["r"] < b["r"] and a["unten"] < b["unten"] for a, b in zip(ringe, ringe[1:]))


def test_spannmutter_streift_im_tiefen_loch():
    f = pruefen(loch(tiefe=40), SCHAFT6)
    assert f is not None and f["teil"] == "mutter"
    assert f["tief"] == pytest.approx(15.0, abs=0.1)            # Mutter bei −15, daneben steht Material
    assert "mindestens 40,0 mm ausspannen (jetzt 25,0 mm)" in f["text"]
    assert f["ort"] and f["zuege"]


def test_lang_genug_ausgespannt_keine_kollision():
    assert pruefen(loch(tiefe=40), dict(SCHAFT6, ausspannlaenge=41)) is None


def tasche(x0, x1, tiefe):
    tp = Toolpath("tasche", [Move(True, x0, x0, 5)])
    y = x0
    while y <= x1:
        tp.moves += [Move(False, x0, y, -tiefe, 800), Move(False, x1, y, -tiefe, 800)]
        y += 2
    return tp


def test_spindel_in_der_tasche():
    """Tasche 40 × 40 mm, 30 tief, darin ein Loch: die Mutter hat Platz, die Spindel (Ø 65) nicht."""
    w = dict(SCHAFT6, ausspannlaenge=56)
    teile = [{"key": "t", "slot": "C", "seite": 1, "tp": tasche(30, 70, 30), "bid": "t"},
             {"key": "b1", "slot": "C", "seite": 1, "tp": loch(tiefe=80), "bid": "b1"}]
    folge = [{"seite": 1, "slot": "C", "keys": ["t", "b1"]}]
    f = rh.pruefen(teile, folge, {"C": w}, auftrag(dicke=200), ["b1"])
    assert f["teil"] == "spindel"
    assert f["tief"] == pytest.approx(80 - 56 - 18, abs=0.1)
    assert "die Spindel" in f["text"] or "Die Spindel" in f["text"]


def test_dicker_schaft():
    w = {"typ": "schaft", "durchmesser": 6.0, "schneidenlaenge": 10.0, "schaft": 8.0, "ausspannlaenge": 40}
    f = pruefen(loch(tiefe=15), w, auftrag({}))
    assert f["teil"] == "schaft" and f["tief"] == pytest.approx(5.0, abs=0.1)
    assert "mindestens 15,0 mm Schneidenlänge" in f["text"]


def test_nur_gepruefte_bearbeitungen():
    assert pruefen(loch(tiefe=40), SCHAFT6, geprueft=()) is None
    assert pruefen(loch(tiefe=40), SCHAFT6, geprueft=("andere",)) is None


def test_ausgeraeumtes_zaehlt_nicht():
    """Erst eine breite Tasche, dann tief in der Mitte: die Mutter hat Platz."""
    raeumen = Toolpath("raeumen", [Move(True, 20, 20, 5)])
    y = 20.0
    while y <= 80:
        raeumen.moves += [Move(False, 20, y, -20, 800), Move(False, 80, y, -20, 800)]
        y += 2
    teile = [{"key": "r", "slot": "C", "seite": 1, "tp": raeumen, "bid": "r"},
             {"key": "b1", "slot": "C", "seite": 1, "tp": loch(tiefe=40), "bid": "b1"}]
    folge = [{"seite": 1, "slot": "C", "keys": ["r", "b1"]}]
    w = dict(SCHAFT6, ausspannlaenge=22)                         # Mutter bei −18: in der Tasche
    assert rh.pruefen(teile, folge, {"C": w}, auftrag(), ["b1"]) is None
    folge = [{"seite": 1, "slot": "C", "keys": ["b1", "r"]}]     # andere Reihenfolge: streift
    assert rh.pruefen(teile, folge, {"C": w}, auftrag(), ["b1"])["teil"] == "mutter"


hirch_relief = pytest.importorskip("hirch_cam.hirch_relief", reason="Modul hirch_relief nicht gebaut")


def test_kollision_markiert_und_rechnet_weiter(tmp_path, monkeypatch):
    pytest.importorskip("inkex")
    if not hirch_relief.mit_ocl():
        pytest.skip("ohne OpenCAMLib gebaut")
    from hirch_cam import dokument, werkzeugdb
    from test_relief3d import zeichnung
    konfig = tmp_path / "konfig"
    monkeypatch.setenv("XDG_CONFIG_HOME", str(konfig))
    werkzeugdb.maschine_setzen(MASCHINE)
    svg = zeichnung(tmp_path, [{"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 4,
                                "aufmass": 0.5, "farbe": "#5e5c64"}])
    # Fräser C nur 6 mm ausgespannt: die Mutter (Ø 25) kommt an der Kuppel ins Material
    text = open(svg, encoding="utf-8").read()
    e_alt = text.split("hirch:einstellungen=")[1]
    import html
    q = e_alt[0]
    roh = e_alt[1:e_alt.index(q, 1)]
    e = json.loads(html.unescape(roh))
    e["werkzeuge"]["C"]["ausspannlaenge"] = 6
    from xml.sax.saxutils import quoteattr
    text = text.replace(q + roh + q, quoteattr(json.dumps(e)))
    open(svg, "w", encoding="utf-8").write(text)
    erg = dokument.rechnen(svg, str(tmp_path / "aus"), "kuppel")
    # Peter 30.10. (B-10): trotzdem fräsen können – die Dateien entstehen, die Kollision ist ein Fehler der Prüfung
    # («G-Code speichern» fragt «Trotzdem speichern?») und steht an der Bearbeitung
    assert erg["ok"] and erg["dateien"]
    assert erg["pruefung"][0]["stufe"] == "fehler"
    assert erg["pruefung"][0]["text"].startswith("Kollision – ")
    assert "Spannmutter" in erg["pruefung"][0]["text"]
    k = erg["kollisionen"][0]
    assert k["teil"] == "mutter" and k["zuege"] and k["ort"]
    assert any("Spannmutter" in t for t in erg["op_warnungen"][k["bearbeitung"]])
    # lang genug ausgespannt: rechnet wie immer
    e["werkzeuge"]["C"]["ausspannlaenge"] = 40
    text = open(svg, encoding="utf-8").read()
    alt = text.split("hirch:einstellungen=")[1]
    q = alt[0]
    roh = alt[1:alt.index(q, 1)]
    open(svg, "w", encoding="utf-8").write(text.replace(q + roh + q, quoteattr(json.dumps(e))))
    erg = dokument.rechnen(svg, str(tmp_path / "aus2"), "kuppel")
    assert erg["ok"], erg.get("pruefung")
