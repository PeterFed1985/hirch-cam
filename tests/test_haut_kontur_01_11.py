"""B-15 (Peter 01.11.): nach dem 3D-Fräsen mit Haut (zweiseitig, «Haut statt durchfräsen») eine Kontur entlang des
Umrisses, die nur die Haut durchtrennt und Haltestege stehen lässt – «Tiefe gemessen ab: Haut» (`tiefe_ab: "haut"`).
Linse zweiseitig, Haut 0,5 mm mitten auf der Trennfläche (Z −10 von 20): von Seite 2 aus liegt die Haut zwischen
Z −9,75 und −10,25; die Kontur schneidet bis 0,3 mm darunter (−10,55), an den Stegen nicht.
Peter 02.11.: von der Materialoberfläche an in Zustellungen (2 mm) hinunter – das 3D-Fräsen macht am Umriss nicht
überall frei; die Stege (1,5 mm ab dem Boden) stehen unten in der Haut, nicht oben auf dem Block."""
import math
import os
import re
import sys

import pytest

pytest.importorskip("hirch_cam.hirch_relief", reason="hirch_relief nicht gebaut")
pytest.importorskip("inkex")
sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "werkzeuge"))
import eilgang_pruefen as ep  # noqa: E402
from test_relief3d import ZWEI, linsen_zeichnung  # noqa: E402

from hirch_cam import dokument, engine  # noqa: E402
from hirch_cam.ops import relief_projektion as rp  # noqa: E402

BEA = [{"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 4, "aufmass": 0.5, "auf_seite": 1},
       {"id": "s2", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 4, "aufmass": 0.5, "auf_seite": 2}]


def _rechnen(tmp_path, monkeypatch, haut=True, stege=3, mehr=0.0, zust=2, marken=None, zweimal=None):
    svg, _mat = linsen_zeichnung(tmp_path, stege=False, bearbeitungen=BEA,
                                 modell_zwei={"an": True, "haut_an": haut, "haut": 0.5})
    orig = engine.rechnen
    kreis = [[50 + 21.6 * math.cos(2 * math.pi * i / 90), 45 + 21.6 * math.sin(2 * math.pi * i / 90)]
             for i in range(91)]

    def mit_kontur(a):
        # eigener Fräser (Ø 4): die Kontur kommt in ein eigenes Programm
        a["werkzeuge"]["D"] = {"typ": "schaft", "durchmesser": 4.0, "laenge": 50, "nummer": 3, "vorschub": 1200,
                               "eintauchen": 300}
        a["bearbeitungen"].append({"id": "k", "art": "kontur", "werkzeug": "D", "tiefe": 5, "zustellung": zust,
                                   "seite": "aussen", "tiefe_ab": "haut", "stege_anzahl": stege,
                                   "stege_breite": 6, "stege_hoehe": 1.5, "auf_seite": 2, "aktiv": True, "tiefe_ab_mm": mehr,
                                   "objekte": [{"punkte": kreis, "geschlossen": True}]})
        if marken:
            a["bearbeitungen"][-1]["stege_modus"] = "manuell"
            a["stege"] = marken
            a["stege_seite"] = [2] * len(marken)
        return orig(a)
    monkeypatch.setattr(engine, "rechnen", mit_kontur)
    erg = dokument.rechnen(svg, str(tmp_path / "aus"))
    if zweimal is not None:          # dieselbe Zeichnung noch einmal, vorher zweimal(erg)
        zweimal(erg)
        return erg, dokument.rechnen(svg, str(tmp_path / "aus2"))
    return erg


def _kontur_z(erg):
    """Z der Vorschübe auf dem Ring der Konturbahn (Mitte 23,6 mm vom Zentrum) im Programm der Kontur (Ø 4)."""
    p = next(p for p in erg["programme"] if int(p["seite"]) == 2 and abs(float(p["durchmesser"]) - 4) < 1e-6)
    x = y = 0.0
    z, eil, aus = 20.0, True, []
    for l in open(p["datei"]).read().upper().splitlines():
        w = dict(re.findall(r"([GXYZF])(-?[0-9.]+)", l))
        if "G" in w:
            eil = float(w["G"]) == 0
        x, y, z = float(w.get("X", x)), float(w.get("Y", y)), float(w.get("Z", z))
        if not eil and abs(math.hypot(x - 50, y - 45) - 23.6) < 0.3:
            aus.append(z)
    return aus


def test_kontur_von_oben_durch_die_haut_mit_stegen_unten(tmp_path, monkeypatch):
    erg = _rechnen(tmp_path, monkeypatch)
    assert erg["ok"] and ep.pruefen(erg) == []
    assert not erg["op_warnungen"].get("k"), erg["op_warnungen"].get("k")
    zs = _kontur_z(erg)
    unten = -9.75 - 0.5 - rp.HAUT_ZUGABE
    assert min(zs) == pytest.approx(unten, abs=0.02)                 # durch die Haut, 0,3 darunter
    # Ebenen alle 2 mm von unten bis knapp unter die Oberfläche (−0,55 … −10,55)
    for k in range(6):
        assert any(abs(z - (unten + 2 * k)) < 0.02 for z in zs), k
    # Stege unten: auf der untersten Ebene hebt die Kontur auf 1,5 mm über den Boden – über die Haut (−9,75)
    assert any(abs(z - (unten + 1.5)) < 0.02 for z in zs)
    # Anzeige: Tiefe von der Oberfläche, 6 Zustellungen (Peter: es standen 26, gefräst wurde eine)
    op = next(o for s in erg["ablauf"]["seiten"] for a in s["abschnitte"] for o in a["ops"] if o["id"] == "k")
    assert op["tiefe"] == pytest.approx(10.55, abs=0.02) and op["n"] == 6


def test_ohne_stege_ganz_durch(tmp_path, monkeypatch):
    """Ohne Stege: je Ebene als Schraube, bis 0,3 mm unter die Haut."""
    erg = _rechnen(tmp_path, monkeypatch, stege=0)
    assert ep.pruefen(erg) == []
    zs = _kontur_z(erg)
    assert zs and min(zs) == pytest.approx(-10.55, abs=0.02)
    assert any(-1.0 < z < 0 for z in zs)                             # oben angefangen


def test_tiefer_als_die_haut(tmp_path, monkeypatch):
    """«Tiefe gemessen ab» mit Wert: so viel tiefer (oder weniger tief) als 0,3 mm unter der Haut."""
    zs = _kontur_z(_rechnen(tmp_path, monkeypatch, stege=0, mehr=0.7))
    assert min(zs) == pytest.approx(-11.25, abs=0.02)


def test_ohne_haut_ausgelassen_mit_hinweis(tmp_path, monkeypatch):
    erg = _rechnen(tmp_path, monkeypatch, haut=False)
    assert rp.HAUT_FEHLT in erg["op_warnungen"].get("k", [])
    assert "k" not in erg["bearbeitungen"]


def test_stege_von_hand_und_grosse_zustellung(tmp_path, monkeypatch):
    """Peter 02.11.: Stege von Hand gesetzt; Zustellung 20 mm (dort ist schon gefräst) – eine Ebene von oben bis
    durch die Haut, die Stege unten an den gesetzten Stellen."""
    marken = [[50 + 23.6, 45], [50 - 23.6, 45]]
    erg = _rechnen(tmp_path, monkeypatch, zust=20, marken=marken)
    assert erg["ok"] and ep.pruefen(erg) == []
    zs = _kontur_z(erg)
    assert min(zs) == pytest.approx(-10.55, abs=0.02)
    assert not any(-8.0 < z < -0.5 for z in zs)                       # keine Ebene dazwischen
    op = next(o for s in erg["ablauf"]["seiten"] for a in s["abschnitte"] for o in a["ops"] if o["id"] == "k")
    assert op["n"] == 1
    # Stege an den Marken: dort 1,5 mm über dem Boden
    p = next(p for p in erg["programme"] if int(p["seite"]) == 2 and abs(float(p["durchmesser"]) - 4) < 1e-6)
    x = y = 0.0
    z, eil, steg = 20.0, True, []
    for l in open(p["datei"]).read().upper().splitlines():
        w = dict(re.findall(r"([GXYZF])(-?[0-9.]+)", l))
        if "G" in w:
            eil = float(w["G"]) == 0
        x, y, z = float(w.get("X", x)), float(w.get("Y", y)), float(w.get("Z", z))
        if not eil and abs(z - (-10.55 + 1.5)) < 0.02:
            steg.append((x, y))
    assert steg
    for m in marken:
        assert min(math.dist(m, q) for q in steg) < 4, m


def test_haut_nur_einmal_gerechnet(tmp_path, monkeypatch):
    """B-28 (Peter 04.11.): ein Ein/Aus davor rechnet die Kontur neu – die Haut (das Teure daran) wird dabei
    nicht noch einmal gebaut, sondern im Prozess wiederverwendet; die Bahn bleibt genau gleich."""
    monkeypatch.setattr(rp, "_HAUT_SPEICHER", None)
    n = []
    orig = rp._Haut

    class Zaehler(orig):
        def __init__(self, *a, **k):
            n.append(1)
            super().__init__(*a, **k)
    monkeypatch.setattr(rp, "_Haut", Zaehler)
    from hirch_cam import speicher
    stand = {}

    def dazwischen(erg):
        stand["n"] = len(n)
        speicher.SPEICHER.leeren()               # wie nach einem Ein/Aus davor: die Kontur rechnet neu …
    erg, erg2 = _rechnen(tmp_path, monkeypatch, zweimal=dazwischen)
    assert erg["ok"] and erg2["ok"] and 1 <= stand["n"] <= 2
    assert len(n) == stand["n"]                  # … die Haut nicht
    assert _kontur_z(erg2) == _kontur_z(erg)     # und die Bahn ist genau dieselbe
