"""Endprüfung 07.11. (Peter: «Du kannst alles so machen wie du denkst. Wenn es nicht betrifft, muss es dennoch
kommuniziert werden.»): die offenen Punkte der Fable-Zweitprüfung – Bögen, G91, Sammeldateien, Bohrzyklus mit
weitergeltender Tiefe – und die Hinweise, wo die Prüfung oder die Tasche etwas nicht kann."""
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "zufall"))
from zeichnung import WERKZEUGE, _kreis, _rechteck  # noqa: E402

from hirch_cam import dokument, engine, freigabe  # noqa: E402
from hirch_cam.einstellungen import STANDARD_BEARBEITUNG  # noqa: E402

FLACH6 = (3.0, True, 1, "flach", 60.0, 0.0, 0.0)


def _eil(erg):
    return [p for p in erg["punkte"] if "Eilgang/Eilgänge unterhalb" in p["text"] and p["stufe"] == "fehler"]


def _pruefen(tmp_path, text, fraeser=True):
    p = tmp_path / "a.tap"
    p.write_text(text, newline="")
    return freigabe.pruefen([str(p)], 10.0, 0.0, 20.0, fraeser={"a.tap": FLACH6} if fraeser else None)


def test_bogen_nicht_als_sehne():
    """Bogen G3 um (20,0) unten herum: als Sehne gelesen galt die Gerade y=0 als gefräst – der Eilgang zurück auf ihr
    fährt aber durch Material."""
    r = freigabe._lesen("G0X0Y0Z5\nG3X40Y0I20J0\n")
    assert min(b[2] for b in r["bewegungen"]) == pytest.approx(-20, abs=0.02)
    r = freigabe._lesen("G0X0Y0Z5\nG2X10Y10R-10\n")          # langer Bogen (R < 0)
    assert min(b[1] for b in r["bewegungen"]) == pytest.approx(-10, abs=0.02)


def test_eilgang_auf_der_sehne_ist_fehler(tmp_path):
    text = "G0Z5\nM3\nG0X0Y0\nG1Z-5F300\nG3X40Y0I20J0F500\nG0X0Y0\nG0Z5\nM30\n"
    assert _eil(_pruefen(tmp_path, text))
    # derselbe Weg im Bogen zurück: frei
    text = "G0Z5\nM3\nG0X0Y0\nG1Z-5F300\nG3X40Y0I20J0F500\nG2X0Y0I-20J0\nG0Z5\nM30\n"
    assert not _eil(_pruefen(tmp_path, text))


def test_g91_wird_gerechnet(tmp_path):
    """G91 (relativ) wurde übersprungen – ein Eilgang durch Material darin blieb ungesehen."""
    text = "G90G0X10Y10Z5\nM3\nG1Z-5F300\nG91\nG0X20\nG90\nG0Z5\nM30\n"
    assert _eil(_pruefen(tmp_path, text))
    text = "G90G0X10Y10Z5\nM3\nG1Z-5F300\nG91\nG1X20F500\nG0X-20\nG90\nG0Z5\nM30\n"
    assert not _eil(_pruefen(tmp_path, text))


def test_g91_ohne_start_wird_gemeldet(tmp_path):
    erg = _pruefen(tmp_path, "G91\nG0X5Y5\nG90\nG0X0Y0Z5\nM3\nG1Z-1F300\nG0Z5\nM30\n")
    assert any("relativen Koordinaten (G91)" in p["text"] and p["stufe"] == "warnung" for p in erg["punkte"])


def test_bogen_andere_ebene_wird_gemeldet(tmp_path):
    erg = _pruefen(tmp_path, "G0X0Y0Z5\nM3\nG18\nG2X10Z5I5K0F300\nG17\nG0Z5\nM30\n")
    assert any("anderen Ebene (G18/G19)" in p["text"] and p["stufe"] == "warnung" for p in erg["punkte"])


def test_referenzfahrt_kein_fehlalarm(tmp_path):
    """Fanuc «G91 G28 Z0»: danach steht Z ganz oben – nicht auf der alten Höhe."""
    text = "G0X0Y0Z5\nM3\nG1Z-5F300\nG91G28Z0\nG90\nG0X30Y30\nG0Z5\nM30\n"
    assert not _eil(_pruefen(tmp_path, text))


def test_bohrzyklus_tiefe_gilt_weiter(tmp_path):
    """G81 … Z−10, dann «X40Y10»: auch das zweite Loch ist 10 mm tief (vorher galt es als nicht gebohrt)."""
    text = ("G0Z5\nM3\nG0X10Y10\nG81X10Y10Z-10R1F200\nX40Y10\nG80\nG0X40Y10\nG0Z-9\nG0Z5\nM30\n")
    assert not _eil(_pruefen(tmp_path, text, fraeser=False))
    r = freigabe._lesen(text)
    assert [b[3] for b in r["bewegungen"] if b[0] == "zyklus"] == [-10, -10]


def _auftrag(tmp_path, bea, post, werkzeuge=None, B=100, H=60):
    for i, b in enumerate(bea):
        voll = dict(STANDARD_BEARBEITUNG.get(b["art"], {}), **b)
        voll.setdefault("name", "%s %d" % (b["art"], i))
        bea[i] = voll
    return {"werkzeuge": werkzeuge or {k: dict(v) for k, v in WERKZEUGE.items()},
            "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 10},
            "postprozessor": post, "material_flaeche": [B, H], "spannmittel": [], "reihenfolge": "auto",
            "bearbeitungen": bea, "ausgabe": str(tmp_path / ("t_" + post + ".nc"))}


@pytest.mark.parametrize("post", ["grbl_mm", "mach3_mm", "fanuc_mm"])
def test_sammeldatei_je_werkzeug(tmp_path, post):
    """Fable B, Fund G: Planen + Tasche + Kontur in einer Datei – ohne Fräserform blieb ein Fehlalarm. Jetzt prüft die
    Endprüfung jedes Werkzeug mit seiner Form; ein echter Eilgang im Material bleibt ein Fehler."""
    bea = [{"id": "pl", "art": "planen", "werkzeug": "C", "tiefe": 1.0,
            "objekte": [{"punkte": _rechteck(0, 0, 100, 60), "geschlossen": True}]},
           {"id": "t", "art": "tasche", "werkzeug": "C", "tiefe": 4,
            "objekte": [{"punkte": _rechteck(20, 10, 50, 40), "geschlossen": True}]},
           {"id": "k", "art": "kontur", "werkzeug": "B", "tiefe": 3, "seite": "innen",
            "objekte": [{"punkte": _kreis(75, 30, 8), "geschlossen": True}]}]
    a = _auftrag(tmp_path, bea, post)
    erg = engine.rechnen(a)
    assert len(erg["dateien"]) == 1 and len(erg["programme"]) == 2
    assert [len(t) for t in erg["datei_teile"]] == [2]
    fr = dokument.endpruefung(a, erg)
    assert fr["stufe"] != "fehler", fr["punkte"]
    # im Teil des zweiten Werkzeugs ein Eilgang quer durch das Material (unter dem Planen, neben allem Gefrästen)
    d = erg["dateien"][0]
    zeilen = open(d, newline="").read().splitlines(True)
    ab = erg["datei_teile"][0][1][0]
    i = next(k for k in range(ab, len(zeilen)) if "G1" in zeilen[k] and "Z" in zeilen[k])
    nl = "\r\n" if zeilen[i].endswith("\r\n") else "\n"
    zeilen[i + 1:i + 1] = ["G0 X5 Y55 Z-2" + nl, "G0 X95 Y55 Z-2" + nl]
    open(d, "w", newline="").write("".join(zeilen))
    fr = dokument.endpruefung(a, erg)
    assert any("Eilgang/Eilgänge unterhalb" in q["text"] and q["stufe"] == "fehler" for q in fr["punkte"]), fr


def test_tasche_form_zu_schmal_wird_gemeldet(tmp_path):
    """Fable 06.11.: Ø 10 in einer Tasche 7 × 7 neben einer grossen – die kleine fiel ohne Hinweis weg."""
    w = {"A": {"typ": "schaft", "durchmesser": 10.0, "nummer": 1, "vorschub": 1000, "eintauchen": 300},
         "B": {"typ": "schaft", "durchmesser": 3.0, "nummer": 2, "vorschub": 800, "eintauchen": 300}}

    def rechnen(schlichten):
        bea = [{"id": "t", "art": "tasche", "werkzeug": "A", "tiefe": 4, "schlichten": schlichten,
                "schlicht_werkzeug": "B", "name": "Tasche",
                "objekte": [{"punkte": _rechteck(10, 10, 40, 40), "geschlossen": True},
                            {"punkte": _rechteck(50, 10, 57, 17), "geschlossen": True}]}]
        return engine.rechnen(_auftrag(tmp_path, bea, "vcarve_mm", werkzeuge=w, B=70, H=50))["warnungen"]
    assert "Tasche: 1 Form(en) schmaler als der Fräser – ausgelassen." in rechnen(False)
    assert rechnen(True) == ["Tasche: 1 Form(en) schmaler als der Schruppfräser – dort fräst nur das "
                             "Schlichtwerkzeug."]


def test_umspannen_rampe_bleibt_auf_ihrem_zug():
    """Spannung 2, Fräser ohne Mittenschnitt: die Rampe lief über alle Vorschub-Punkte des Stücks – auch über einen
    Eilgang hinweg – und fräste so die Gerade zwischen zwei Gravur-Linien (Zufallstests 07.11.)."""
    from shapely.geometry import box
    from hirch_cam.model import Move, Toolpath
    from hirch_cam.ops import umspannen as U
    # Linie 1 von x=30 nach x=0 (fährt mitten im Zug in die Zone), Linie 2 von x=0 nach x=30
    tp = Toolpath(moves=[Move(True, 30, 0, 3), Move(False, 30, 0, -1, 200), Move(False, 0, 0, -1, 500),
                         Move(True, 0, 0, 3), Move(True, 0, 20, 3), Move(False, 0, 20, -1, 200),
                         Move(False, 30, 20, -1, 500), Move(True, 30, 20, 3)])
    zone = box(-50, -50, 3, 50)          # erster Zug in der Zone nur 4 mm – kürzer als die Rampe
    _tp1, tp2 = U.teilen(tp, zone, 3.0, 200, (0, 0, 20), rampe=3.0, radius=1.5)
    pos = (0, 0, 20)
    for m in tp2.moves:
        if not m.rapid and min(pos[2], m.z) < 0:
            # kein Vorschub unter der Oberfläche zwischen den Linien y=0 und y=20
            assert abs(m.y - pos[1]) < 1e-6 and m.y in (0, 20), (pos, m)
        pos = (m.x, m.y, m.z)


def test_bestand_sieht_schmalen_rest():
    """3D-Zufall 07.11., Fall 38: zwischen zwei Bahnen einer Kugel Ø 1,5 im Abstand 1,6 mm bleibt ein Grat stehen. Im
    0,25-mm-Raster galt er als gefräst (jede Zelle, deren Mitte im Fräser lag) – der Übergang darüber wurde zu tief
    gesenkt. Jetzt bleibt er im Raster sichtbar."""
    pytest.importorskip("hirch_cam.hirch_relief")
    from hirch_cam.model import Move
    from hirch_cam.ops import freifahren as FF
    b = FF.Bestand(-5, -5, 25, 10, -20.0)
    form = {"typ": "kugel", "durchmesser": 1.5, "eckradius": 0.75, "v_winkel": 60, "spitze": 0}
    for y in (0.0, 1.6):
        b.abtragen([Move(True, 0, y, 1), Move(False, 0, y, -3, 300), Move(False, 20, y, -3, 500),
                    Move(True, 20, y, 1)], form, (0, y, 1))
    assert b.hoch(10, 0.8, 0.75) > -1.0
