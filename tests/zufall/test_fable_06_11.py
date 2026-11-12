"""Funde der zwei Fable-Zweitprüfungen vom 06.11. (Peter: Kontingent für Sicherheit) als feste Fälle.
Bericht: berichte/fraesbahnen-fable-zweitpruefung-06-11. Jeder Fall ist der kleinste, der den Fehler zeigte."""
import math
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "werkzeuge"))
import eilgang_pruefen as ep  # noqa: E402
from zeichnung import WERKZEUGE, _kreis, _rechteck  # noqa: E402

from hirch_cam import dokument, engine, freigabe  # noqa: E402
from hirch_cam.einstellungen import STANDARD_BEARBEITUNG  # noqa: E402
from hirch_cam.model import Job, Move, Toolpath  # noqa: E402
from hirch_cam.post import tap_mm  # noqa: E402

pytest.importorskip("hirch_cam.hirch_relief", reason="hirch_relief nicht gebaut")


def _auftrag(tmp_path, bea, B=100, H=60, D=10, werkzeuge=None):
    for i, b in enumerate(bea):
        voll = dict(STANDARD_BEARBEITUNG.get(b["art"], {}), **b)
        voll.setdefault("name", "%s %d" % (b["art"], i))
        bea[i] = voll
    return {"werkzeuge": werkzeuge or {k: dict(v) for k, v in WERKZEUGE.items()},
            "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": D},
            "postprozessor": "vcarve_mm", "material_flaeche": [B, H], "spannmittel": [], "reihenfolge": "auto",
            "bearbeitungen": bea, "ausgabe": str(tmp_path / "t.tap")}


def test_drei_gewinde_kernloch_immer_davor(tmp_path):
    """F1: ab dem 3. Gewinde kam ein Kernloch wieder nach dem Gewinde (Länge der Schleife fest)."""
    bea = [{"id": f"ge{i}", "art": "gewinde", "werkzeug": "G", "tiefe": 6, "kernloch_werkzeug": "B", "innen": True,
            "objekte": [{"punkte": _kreis(x, 30, 4), "geschlossen": True}]} for i, x in enumerate((20, 50, 80))]
    bea.append({"id": "ko", "art": "kontur", "werkzeug": "B", "tiefe": 10.2, "durch": True, "zustellung": 3,
                "seite": "aussen", "stege_anzahl": 0, "stege_modus": "keine",
                "objekte": [{"punkte": _rechteck(5, 5, 95, 55), "geschlossen": True}]})
    erg = engine.rechnen(_auftrag(tmp_path, bea))
    folge = [s["key"] for p in erg["programme"] for s in p["abschnitte"]]
    for i in range(3):
        assert folge.index(f"ge{i}#kernloch") < folge.index(f"ge{i}"), folge


def _datei(tmp_path, name, moves):
    p = tmp_path / name
    p.write_text(tap_mm.write(Job(toolpaths=[Toolpath(moves=moves)])), newline="")
    return str(p)


FLACH6 = (3.0, True, 1, "flach", 60.0, 0.0, 0.0)


def _eil(erg):
    return [p for p in erg["punkte"] if "Eilgang/Eilgänge unterhalb" in p["text"] and p["stufe"] == "fehler"]


def test_endpruefung_erster_punkt_im_material(tmp_path):
    """F2: der erste volle Punkt (Eilgang schon unter der Oberfläche) wurde in der Nachprüfung nie geprüft."""
    p = tmp_path / "a.tap"
    p.write_text("G0Z5\nG0X10Y10Z-5\nG1X20F500\nG0Z5\nM30\n", newline="")
    assert _eil(freigabe.pruefen([str(p)], 10.0, 0.0, 20.0, fraeser={"a.tap": FLACH6}))


def test_endpruefung_eilgang_in_der_wand(tmp_path):
    """F5: Nut y=0 mit Ø 6, dann Eilgang auf Z −5 entlang y=2,5 – 2,5 mm in der Wand; die breite Toleranz für
    flache Fräser liess ihn durch. Jetzt entscheidet die Nachprüfung in der Form des Fräsers."""
    a = _datei(tmp_path, "a.tap", [Move(True, 0, 0, 5), Move(False, 0, 0, -5, 300), Move(False, 40, 0, -5, 500),
                                   Move(True, 40, 0, 5)])
    b = _datei(tmp_path, "b.tap", [Move(True, 5, 2.5, 5), Move(True, 5, 2.5, -5), Move(True, 35, 2.5, -5),
                                   Move(True, 35, 2.5, 5)])
    erg = freigabe.pruefen([a, b], 10.0, 0.0, 20.0, fraeser={"a.tap": FLACH6, "b.tap": FLACH6})
    assert _eil(erg)
    # entlang der Nut selbst: frei
    c = _datei(tmp_path, "c.tap", [Move(True, 5, 0, 5), Move(True, 5, 0, -5), Move(True, 35, 0, -5),
                                   Move(True, 35, 0, 5)])
    assert not _eil(freigabe.pruefen([a, c], 10.0, 0.0, 20.0, fraeser={"a.tap": FLACH6, "c.tap": FLACH6}))


def test_endpruefung_rampe_und_bohrzyklus(tmp_path):
    """Rampe (0,0,0)→(50,0,−10): galt überall als −10 tief; Bohrzyklus: der Weg zum Loch galt als gebohrt."""
    a = _datei(tmp_path, "a.tap", [Move(True, 0, 0, 5), Move(True, 0, 0, 0.5), Move(False, 0, 0, 0, 300),
                                   Move(False, 50, 0, -10, 500), Move(True, 50, 0, -9.5), Move(True, 0, 0, -9.5),
                                   Move(True, 0, 0, 5)])
    assert _eil(freigabe.pruefen([a], 12.0, 0.0, 20.0))
    p = tmp_path / "z.tap"
    p.write_text("G0Z5\nG0X10Y10\nG81X10Y10Z-10R1F200\nX40Y10\nG80\nG0Z-9\nG0X10Y10\nG0Z5\nM30\n", newline="")
    assert _eil(freigabe.pruefen([str(p)], 12.0, 0.0, 20.0))


def test_enge_helix_ohne_mittenschnitt(tmp_path):
    """F3: Loch Ø 6,2 mit Ø 6 ohne Mittenschnitt als Helix (Radius 0,1 mm) – in der Mitte bleibt ein Zapfen."""
    w = {k: dict(v) for k, v in WERKZEUGE.items()}
    w["C"]["mittenschnitt"] = False
    bea = [{"id": "bo", "art": "bohren", "werkzeug": "C", "tiefe": 4, "zustellung": 2, "methode": "helix",
            "objekte": [{"punkte": _kreis(30, 30, 3.1, 72), "geschlossen": True}]}]
    erg = engine.rechnen(_auftrag(tmp_path, bea, werkzeuge=w))
    assert erg.get("ohne_mittenschnitt")


def test_plantiefe_nur_fuer_verschobene(tmp_path):
    """F6: mit Planen zählt die Plantiefe nur dort zur erlaubten Tiefe, wo die Bahn verschoben wird – eine Kontur
    ganz durch, die 2 mm zu tief fräst, bleibt ein Fehler."""
    a = _auftrag(tmp_path, [{"id": "pl", "art": "planen", "werkzeug": "C", "tiefe": 2.0,
                             "objekte": [{"punkte": _rechteck(0, 0, 100, 60), "geschlossen": True}]},
                            {"id": "ko", "art": "kontur", "werkzeug": "C", "tiefe": 10.2, "durch": True,
                             "objekte": [{"punkte": _rechteck(10, 10, 90, 50), "geschlossen": True}]}])
    erg = engine.rechnen(a)
    assert dokument.endpruefung(a, erg)["stufe"] != "fehler"
    import re
    d = erg["dateien"][0]
    text = re.sub(r"Z-10\.20*(?![0-9])", "Z-12.2", open(d, newline="").read())
    open(d, "w", newline="").write(text)
    assert any("tiefer als jede Bearbeitung" in q["text"] for q in dokument.endpruefung(a, erg)["punkte"])


def test_schlichten_nach_v_tasche_kein_eilgang_im_material(tmp_path):
    """Fable B, Fund A: Tasche mit V-Fräser, Schlichten mit Ø 3 – die Übergänge zwischen den Schlicht-Ebenen fuhren
    quer durch Material, das die schrägen V-Wände stehen liessen. Das Sicherheitsnetz hebt sie jetzt an."""
    w = {"V": {"typ": "v", "durchmesser": 32.0, "v_winkel": 90, "spitze": 0.0, "nummer": 1, "vorschub": 1000,
               "eintauchen": 300},
         "B": {"typ": "schaft", "durchmesser": 3.0, "nummer": 2, "vorschub": 800, "eintauchen": 300}}
    bea = [{"id": "t", "art": "tasche", "werkzeug": "V", "tiefe": 9.0, "zustellung": 1.0, "strategie": "runden",
            "bahnabstand": 40, "v_wand": "oben", "schlichten": True, "schlicht_werkzeug": "B", "schlicht_zugabe": 0.3,
            "schlicht_zustellung": 1.0, "objekte": [{"punkte": _rechteck(15, 12, 45, 28), "geschlossen": True}]}]
    a = _auftrag(tmp_path, bea, B=60, H=40, werkzeuge=w)
    erg = engine.rechnen(a)
    assert ep.pruefen(erg, werkzeuge=w, aussen_frei=(0, 0, 60, 40)) == []


def _rest(erg, kasten, tiefe, rand):
    from hirch_cam.simulation import Hoehenfeld
    x0, y0, x1, y1 = kasten
    f = Hoehenfeld(x0 - 5, y0 - 5, x1 + 5, y1 + 5, 0.05)
    for p in erg["programme"]:
        f.fraesen(tap_mm.read(open(p["datei"], newline="").read()), "flach", float(p["durchmesser"]), 60)
    X, Y = f.koordinaten()
    innen = (X > x0 + rand) & (X < x1 - rand) & (Y > y0 + rand) & (Y < y1 - rand)
    return float((f.H[innen] + tiefe).max())


def test_schlicht_zugabe_breiter_als_der_fraeser(tmp_path):
    """Fable B, Fund B: Ø 0,8 mit Zugabe 1,0 liess eine 0,2-mm-Wand in voller Höhe stehen."""
    w = {"B": {"typ": "schaft", "durchmesser": 0.8, "nummer": 1, "vorschub": 800, "eintauchen": 300}}
    bea = [{"id": "t", "art": "tasche", "werkzeug": "B", "tiefe": 4.0, "zustellung": 0.5, "strategie": "runden",
            "bahnabstand": 60, "schlichten": True, "schlicht_werkzeug": "", "schlicht_zugabe": 1.0,
            "schlicht_zustellung": 4.0, "objekte": [{"punkte": _rechteck(20, 10, 40, 25), "geschlossen": True}]}]
    erg = engine.rechnen(_auftrag(tmp_path, bea, B=60, H=40, werkzeuge=w))
    assert _rest(erg, (20, 10, 40, 25), 4.0, 0.45) < 0.1


def test_runden_mit_grossem_bahnabstand_ohne_insel(tmp_path):
    """Fable B, Fund C: Ø 8, Bahnabstand 80 % – der letzte Ring fiel weg, in der Mitte blieb ein Klotz."""
    w = {"A": {"typ": "schaft", "durchmesser": 8.0, "nummer": 1, "vorschub": 1000, "eintauchen": 300}}
    bea = [{"id": "t", "art": "tasche", "werkzeug": "A", "tiefe": 5.0, "zustellung": 5.0, "strategie": "runden",
            "bahnabstand": 80, "objekte": [{"punkte": _rechteck(10, 10, 34.4, 29.9), "geschlossen": True}]}]
    erg = engine.rechnen(_auftrag(tmp_path, bea, B=50, H=40, werkzeuge=w))
    assert _rest(erg, (10, 10, 34.4, 29.9), 5.0, 4.6) < 0.1
