"""Umspannen – Lücken aus der Prüfung vom 31.10. (berichte/fraesbahnen-umspannen-pruefung-fable) geschlossen
(Sicherheitsprüfung Peter 04.11.): Seite 2 mit Spannmitteln «Seite 2», Gewinde und Passlöcher am Rand der Zone.
Prüfsteine wie test_umspannen_27_10: beide Spannungen zusammen tragen genau so viel ab wie ohne Spannmittel, der
feine Prüfer findet keinen Eilgang ins Material und kein Eintauchen ohne Mittenschnitt, in Spannung 1 kein
Fräsweg in einer Sperrzone, die Prüfung im Programm meldet nichts."""
import math
import os
import sys

import pytest

pytest.importorskip("shapely")
sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "werkzeuge"))
import eilgang_pruefen as ep  # noqa: E402
import test_umspannen_27_10 as T  # noqa: E402

from hirch_cam import engine  # noqa: E402
from hirch_cam.einstellungen import PASS_ID  # noqa: E402
from hirch_cam.ops import umspannen as U  # noqa: E402
from hirch_cam.post import tap_mm  # noqa: E402

UM = {"an": True, "abstand": 10}


def _nicht_in_zone(a, erg):
    from shapely.geometry import LineString, Point
    for p in erg["programme"]:
        if p["spannung"] != 1:
            continue
        z = U.zone(U.spannmittel(a, int(p["seite"]), 1), float(p["durchmesser"]) / 2, 10.0)
        if z is None:
            continue
        z = z.buffer(-0.01)
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


def _gleich_und_sicher(a, mit, ohne):
    assert mit["ok"] and ohne["ok"]
    H0 = T._feld(ohne)
    assert T._ohne_kanten(H0, T._feld(mit) - H0) == 0
    assert ep.pruefen(mit, werkzeuge=a["werkzeuge"]) == []
    assert not mit.get("pruefung"), mit.get("pruefung")
    _nicht_in_zone(a, mit)


def _zweiseitig(a, spann_seite=2):
    a["zweiseitig"] = {"an": True, "achse": "x", "passloecher": False}
    for b in a["bearbeitungen"]:
        b["auf_seite"] = 2
    for s in a["spannmittel"]:
        s["seite"] = spann_seite
    return a


def test_seite_2_mit_spannmitteln_seite_2(tmp_path):
    a = _zweiseitig(T._auftrag(tmp_path, "m", umspannen=UM))
    mit = engine.rechnen(a)
    ohne = engine.rechnen(_zweiseitig(T._auftrag(tmp_path, "o", spannmittel=False)))
    _gleich_und_sicher(a, mit, ohne)
    assert {(p["seite"], p["spannung"]) for p in mit["programme"]} == {(2, 1), (2, 2)}
    assert [u["seite"] for u in mit["umspannen"]] == [2]


def test_spannmittel_seite_1_wirken_nicht_auf_seite_2(tmp_path):
    a = _zweiseitig(T._auftrag(tmp_path, "m1", umspannen=UM), spann_seite=1)
    mit = engine.rechnen(a)
    assert mit["ok"] and {p["spannung"] for p in mit["programme"]} == {1}
    assert ep.pruefen(mit, werkzeuge=a["werkzeuge"]) == []


def _gewinde_passloch(a, cx):
    a["werkzeuge"]["G"] = {"typ": "gewinde", "durchmesser": 5, "zaehne": 1, "nummer": 3, "vorschub": 400,
                           "eintauchen": 200}
    a["werkzeuge"]["B"] = {"typ": "schaft", "durchmesser": 3, "nummer": 4, "vorschub": 1000, "eintauchen": 300,
                           "empfehlung": {"zustellung": 1.5}}
    a["bearbeitungen"] = [
        {"id": "kb", "art": "bohren", "werkzeug": "B", "tiefe": 6, "zustellung": 1.5, "methode": "helix",
         "objekte": [{"punkte": T._kreis(cx, 30, 3.3, 90), "geschlossen": True}]},
        {"id": "gw", "art": "gewinde", "werkzeug": "G", "tiefe": 5,
         "objekte": [{"punkte": T._kreis(cx, 30, 4, 90), "geschlossen": True}]},
        {"id": PASS_ID, "art": "bohren", "werkzeug": "C", "tiefe": 9, "zustellung": 3, "methode": "auto",
         "passloecher": True, "objekte": [{"punkte": T._kreis(cx, 70, 3, 72), "geschlossen": True}]}]
    a["spannmittel"].append({"punkte": T._rechteck(130, 60, 160, 80), "hoehe": 12, "seite": 1})
    return a


# Zone der Spannmittel bei x 130 (Abstand 10): Ø 3 ab x 118,5, Gewindefräser Ø 5 ab 117,5, Ø 6 ab 117 –
# die Stellen, an denen Kernloch, Gewinde und Passloch nacheinander in die Zone kommen
@pytest.mark.parametrize("cx", [112.0, 116.0, 116.3, 116.8, 117.2, 118.0])
def test_gewinde_und_passloch_am_zonenrand(tmp_path, cx):
    a = _gewinde_passloch(T._auftrag(tmp_path, "m", umspannen=UM), cx)
    mit = engine.rechnen(a)
    o = _gewinde_passloch(T._auftrag(tmp_path, "o", spannmittel=False), cx)
    o["spannmittel"] = []
    _gleich_und_sicher(a, mit, engine.rechnen(o))
    # Reihenfolge im G-Code: das Gewinde erst, wenn das Kernloch ganz gebohrt ist
    folge = [s["key"].split("@")[0] for p in mit["programme"] for s in p["abschnitte"]]
    assert max(i for i, k in enumerate(folge) if k == "kb") < min(i for i, k in enumerate(folge) if k == "gw")
    # Passloch ganz in einer Spannung (nie halb gebohrt umspannen)
    assert sum(1 for k in folge if k == PASS_ID) == 1


def test_spannung_2_ohne_zweite_lage_nie_unter_der_sicherheitshoehe(tmp_path):
    """_umspannen_hoehe2: ohne eingezeichnete zweite Lage schätzte Hirch CNC die Höhe der Leerfahrten aus den
    Spannmitteln von Spannung 1 (+ Abstand). Spannt Peter danach höher, fuhren sie darunter. Jetzt nie tiefer als
    die Sicherheitshöhe (20 mm), über die jedes Programm am Anfang fährt."""
    from shapely.geometry import LineString
    a = T._auftrag(tmp_path, "h", umspannen=UM)
    for s in a["spannmittel"]:
        s["hoehe"] = 3                                   # geschätzt wären 3 + 10 = 13 mm
    erg = engine.rechnen(a)
    assert erg["ok"] and ep.pruefen(erg, werkzeuge=a["werkzeuge"]) == []
    # 05.11. (planung 1058): die geschätzte Höhe steht im Ergebnis – die Prüfung nennt sie als Hinweis
    assert erg["hoehe2_geschaetzt"] == 20
    n = 0
    for p in erg["programme"]:
        if p["spannung"] != 2:
            continue
        z = U.zone(U.spannmittel(a, 1, 1), float(p["durchmesser"]) / 2, 10.0).buffer(1e-6)
        with open(p["datei"], newline="") as fh:
            job = tap_mm.read(fh.read())
        pos = (job.home[0], job.home[1], job.safe_z)
        for tp in job.toolpaths:
            for m in tp.moves:
                if m.rapid and math.dist(pos[:2], (m.x, m.y)) > 1e-6 and \
                        not z.contains(LineString([pos[:2], (m.x, m.y)])):
                    assert min(pos[2], m.z) >= 20 - 1e-6, (p["name"], pos, m)
                    n += 1
                pos = (m.x, m.y, m.z)
    assert n
    # mit eingegebener Höhe gilt sie (Peter weiss, wie hoch er spannt)
    a2 = T._auftrag(tmp_path, "h2", umspannen=dict(UM, hoehe2=15))
    erg2 = engine.rechnen(a2)
    assert erg2["ok"] and "hoehe2_geschaetzt" not in erg2
