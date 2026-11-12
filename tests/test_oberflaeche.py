"""Oberflächentest: Hirch CNC unter Xvfb starten, alle Fenster und Dialoge öffnen lassen
(hirch-test.cpp) und den Bericht prüfen – auf Deutsch und auf Englisch.

Braucht ein installiertes Hirch CNC (/opt/hirch-cam oder HIRCH_CAM_BIN) und Xvfb.
Dauert etwa eine Minute je Sprache. Überspringen: HIRCH_OHNE_OBERFLAECHE=1.
"""
import json
import os
import re
import shutil
import subprocess
import time

import pytest

PROGRAMM = os.environ.get("HIRCH_CAM_BIN", "/opt/hirch-cam/bin/hirch-cam")
HIER = os.path.dirname(os.path.abspath(__file__))
ZEICHNUNG = os.path.join(HIER, "referenz", "zeichnungen", "taschen.svg")
ERLAUBT = os.path.join(HIER, "oberflaeche_erlaubt.txt")

braucht_oberflaeche = pytest.mark.skipif(
    os.environ.get("HIRCH_OHNE_OBERFLAECHE") == "1" or not os.path.exists(PROGRAMM)
    or not shutil.which("Xvfb"), reason="kein installiertes Hirch CNC oder kein Xvfb")

# Ohne Dialog sind diese Menüpunkte zulässig (z. B. «nichts ausgewählt» als Hinweis in der Statuszeile)
OHNE_FENSTER_OK = {"Hauptfenster", "hirch-aufraeumen", "hirch-stege", "hirch-spannmittel"}


def _erlaubt():
    if not os.path.exists(ERLAUBT):
        return []
    with open(ERLAUBT, encoding="utf-8") as f:
        return [re.compile(z.strip()) for z in f if z.strip() and not z.startswith("#")]


def _starten(tmp_path, sprache, zeichnung=ZEICHNUNG, einheit="mm", **extra):
    anzeige = ":%d" % (90 + os.getpid() % 9)
    xvfb = subprocess.Popen(["Xvfb", anzeige, "-screen", "0", "1600x1000x24"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.0)
    bericht = tmp_path / ("bericht_%s.json" % sprache)
    svg = tmp_path / ("start_" + os.path.basename(zeichnung))
    shutil.copy(zeichnung, svg)
    env = dict(os.environ, DISPLAY=anzeige, HOME=str(tmp_path / "home"), LANGUAGE=sprache,
               LANG="%s.UTF-8" % ("de_CH" if sprache == "de" else "en_US"), LC_ALL="",
               HIRCH_OBERFLAECHENTEST=str(bericht), HIRCH_EINHEIT=einheit, **extra)
    os.makedirs(env["HOME"], exist_ok=True)
    try:
        # eigene D-Bus-Sitzung: läuft auf dem Rechner schon ein Hirch CNC (Einzelprogramm), gäbe der Test
        # die Zeichnung sonst an dieses weiter und beendete sich gleich
        befehl = [PROGRAMM, str(svg)]
        if shutil.which("dbus-run-session"):
            befehl = ["dbus-run-session", "--"] + befehl
        p = subprocess.run(befehl, env=env, timeout=240,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        fehler = p.stderr.decode("utf-8", "replace")
    except subprocess.TimeoutExpired:
        fehler = "Zeitüberschreitung"
    finally:
        xvfb.terminate()
        xvfb.wait()
    assert bericht.exists(), "kein Bericht – abgestürzt?\n" + fehler[-3000:]
    with open(bericht, encoding="utf-8") as f:
        return json.load(f), fehler


def _3d_zeichnung(tmp_path):
    """Zeichnung mit eingebettetem, zweiseitigem 3D-Modell (Linse mit zwei Haltestegen),
    3D-Schruppen und 3D-Schlichten auf beiden Seiten."""
    pytest.importorskip("hirch_cam.hirch_relief")
    import test_relief3d as t3
    svg, _mat = t3.linsen_zeichnung(tmp_path, bearbeitungen=[
        {"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 4, "aufmass": 0.5,
         "rand": None, "farbe": "#5e5c64"},
        {"id": "f1", "art": "relief_schlichten", "werkzeug": "K", "bahnabstand": 20,
         "richtung": "kreuz", "farbe": "#9a96a0"},
        {"id": "s2", "art": "relief_schruppen", "werkzeug": "C", "auf_seite": 2, "farbe": "#5e5c64"},
        {"id": "f2", "art": "relief_schlichten", "werkzeug": "K", "auf_seite": 2, "farbe": "#9a96a0"},
        {"id": "g1", "art": "gravur", "werkzeug": "K", "tiefe": 0.5, "tiefe_ab": "modell",
         "farbe": "#ff8c00"}],
        elemente='<path d="M 35,45 L 65,45" style="fill:none;stroke:#ff8c00;stroke-width:0.5"/>')
    return svg


@braucht_oberflaeche
@pytest.mark.parametrize("sprache,art", [("de", "2d"), ("en", "2d"), ("de", "3d"), ("en", "3d")])
def test_alle_fenster_oeffnen(tmp_path, sprache, art):
    b, fehler = _starten(tmp_path, sprache, _3d_zeichnung(tmp_path) if art == "3d" else ZEICHNUNG)
    assert b["sprache"] == sprache
    assert b["version_proben"] == {"anzeige beta": "1.0 Beta 1", "anzeige fertig": "1.0",
                                   "anzeige peter": "2026.11.09-af4e82c", "beta1<beta2": -1, "beta2<1.0.0": -1,
                                   "1.0.1>1.0.0": 1, "datum<datum": -1, "peter gleich": 0, "gemischt": 2}
    namen = [s["name"] for s in b["schritte"]]
    assert "Hauptfenster" in namen and any(n.startswith("Bearbeitung ") for n in namen), namen
    schlecht = [s for s in b["schritte"] if not s["ok"]]
    assert not schlecht, schlecht
    leer = [s["name"] for s in b["schritte"]
            if not s["fenster"] and s["name"].startswith(("hirch-", "Bearbeitung "))
            and s["name"] not in OHNE_FENSTER_OK]
    assert not leer, "ohne Fenster: %s" % leer
    assert "Segmentation fault" not in fehler and "terminate called" not in fehler
    if sprache == "en":
        erlaubt = _erlaubt()
        deutsch = [d for d in b["deutsch"] if not any(r.search(d["text"]) for r in erlaubt)]
        assert not deutsch, "Deutsch in der englischen Oberfläche:\n" + "\n".join(
            "  %s: %r" % (d["wo"], d["text"]) for d in deutsch)


@braucht_oberflaeche
@pytest.mark.parametrize("sprache,art", [("de", "2d"), ("en", "2d"), ("en", "3d")])
def test_zoll_kein_mm_sichtbar(tmp_path, sprache, art):
    """Peter 08.11.: mm und Zoll für den Release. In Zoll steht nirgends mehr «mm», und jedes Zahlenfeld neben
    «Zoll»/«in» rechnet um (sonst stünde ein mm-Wert als Zoll da)."""
    b, fehler = _starten(tmp_path, sprache, _3d_zeichnung(tmp_path) if art == "3d" else ZEICHNUNG, einheit="in")
    assert b.get("einheit") == "in"
    z = "in" if sprache == "en" else "Zoll"
    k = (lambda t: t.replace(",", ".")) if sprache == "en" else (lambda t: t)
    assert b["einheit_proben"] == {
        "Ø 6,35 mm": "Ø 1/4 " + z, "Ø 6 mm": "Ø " + k("0,236 ") + z, "Tiefe 6,35 mm": "Tiefe " + k("0,25 ") + z,
        "Ø 38,1 mm": "Ø 1-1/2 " + z, "600 × 900 mm": k("23,622 × 35,433 ") + z, "mass 3.175": "1/8 " + z, "mass 0.79375": "1/32 " + z,
        "X -25,4 … 254 mm": "X -1 … 10 " + z}
    assert "Segmentation fault" not in fehler and "terminate called" not in fehler
    assert not b["ohne_umrechnung"], "Zahlenfeld neben «Zoll» ohne Umrechnung:\n" + "\n".join(
        "  %s: %r" % (d["wo"], d["text"]) for d in b["ohne_umrechnung"])
    erlaubt = [re.compile(z.strip()) for z in open(os.path.join(HIER, "zoll_erlaubt.txt"), encoding="utf-8")
               if z.strip() and not z.startswith("#")]
    metrisch = [d for d in b["metrisch"] if not any(r.search(d["text"]) for r in erlaubt)]
    assert not metrisch, "«mm» in der Zoll-Oberfläche:\n" + "\n".join(
        "  %s: %r" % (d["wo"], d["text"]) for d in metrisch)


@braucht_oberflaeche
def test_import_nur_linien(tmp_path):
    """Peter 10.11.: Aus Illustrator, PDF und AI kommen nur die Linien – schwarz, ohne Füllung, ohne Fräsart.
    Bilder, Texte (zu Linien), Beschneidungen, Masken, Farbprofil und Daten von Illustrator fallen weg."""
    from lxml import etree
    ein = tmp_path / "import_ballast.svg"
    shutil.copy(os.path.join(HIER, "daten", "import_ballast.svg"), ein)
    b, fehler = _starten(tmp_path, "de", HIRCH_TEST_BALLAST=str(ein))
    bericht = " | ".join(b.get("ballast", []))
    for t in ("Nur die Linien übernommen", "1 Text(e) in Linien", "1 Bild(er) entfernt", "Beschneidung",
              "1 verborgene(s)", "Farbprofil entfernt", "Daten anderer Programme"):
        assert t in bericht, bericht
    r = etree.parse(str(ein) + ".aus.svg").getroot()
    namen = {etree.QName(e).localname for e in r.iter() if isinstance(e.tag, str)}
    for weg in ("image", "text", "use", "rect", "circle", "switch", "foreignObject", "style", "color-profile",
                "clipPath", "mask", "pattern", "linearGradient", "pgf", "xmpmeta"):
        assert weg not in namen, weg
    pfade = [e for e in r.iter("{http://www.w3.org/2000/svg}path")]
    assert len(pfade) == 7          # Rechteck, Kreis, Linie, Muster-Fläche, Text, grünes Quadrat und sein Klon
    assert not [e for e in r.iter("{http://www.w3.org/2000/svg}defs") if e.getparent() is not r]
    for e in pfade:
        assert e.get("style", "").startswith("fill:none;stroke:#000000;stroke-width:"), e.get("style")
        assert not e.get("clip-path") and not e.get("mask") and not e.get("class")
    leer = [g for g in r.iter("{http://www.w3.org/2000/svg}g") if not len(g)]
    assert not leer
    assert not any(k.startswith("{http://ns.adobe.com") for e in r.iter() if isinstance(e.tag, str) for k in e.attrib)
    assert "Segmentation fault" not in fehler
