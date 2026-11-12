"""Schnittstelle des Fräsen-Panels: Einstellungen und Zeichnung → Auftrag."""
import json
import os
import sys

import pytest

from hirch_cam import einstellungen as einst

HIER = os.path.dirname(os.path.abspath(__file__))
SCHILD = os.path.join(HIER, "zeichnungen", "schild.svg")


def _inkex():
    try:
        import inkex  # noqa: F401
        return True
    except ImportError:
        pass
    for p in ("/opt/hirch-cam/share/inkscape/extensions",
              os.path.expanduser("~/bau/inkscape-src/share/extensions"),
              "/home/claude/bau/inkscape-src/share/extensions"):
        if os.path.isdir(os.path.join(p, "inkex")):
            sys.path.append(p)
            try:
                import inkex  # noqa: F401
                return True
            except ImportError:
                sys.path.remove(p)
    return False


braucht_inkex = pytest.mark.skipif(not _inkex(), reason="inkex nicht gefunden")


def test_laden_vorgaben():
    e = einst.laden(None)
    assert set(e["werkzeuge"]) == {"A", "B", "C"}
    assert e["bearbeitungen"] == []
    assert e["postprozessor"] == "richauto_a11"


def test_laden_ergaenzt_und_filtert():
    roh = {"werkzeuge": {"A": {"durchmesser": 8}},
           "bearbeitungen": [{"art": "tasche", "tiefe": 2}, {"art": "unsinn"},
                             {"art": "tasche", "tiefe": 9}],
           "hoehen": {"sicherheitshoehe": 25}}
    e = einst.laden(json.dumps(roh))
    assert e["werkzeuge"]["A"]["durchmesser"] == 8
    assert e["werkzeuge"]["A"]["typ"] == "v"            # Rest aus der Vorgabe
    # zwei Taschen: eigene IDs, eigene Farbtöne, die erste in der Grundfarbe
    assert [b["art"] for b in e["bearbeitungen"]] == ["tasche", "tasche"]
    assert [b["tiefe"] for b in e["bearbeitungen"]] == [2, 9]
    assert len({b["id"] for b in e["bearbeitungen"]}) == 2
    assert e["bearbeitungen"][0]["farbe"] == "#00a000"
    assert e["bearbeitungen"][1]["farbe"] != "#00a000"
    assert einst.namen(e) == {e["bearbeitungen"][0]["id"]: "Tasche 1",
                              e["bearbeitungen"][1]["id"]: "Tasche 2"}
    assert e["bearbeitungen"][0]["zustellung"] == 1.5
    assert e["hoehen"]["sicherheitshoehe"] == 25
    assert e["hoehen"]["freifahrhoehe"] == 3.0
    assert einst.laden("kein json")["bearbeitungen"] == []


def test_ergaenzen_kontur_bleibt_letzte():
    e = einst.laden(json.dumps({"bearbeitungen": [{"art": "kontur"}]}))
    einst.ergaenzen(e, {"gravur", "kontur", "vcarve"})
    assert [b["art"] for b in e["bearbeitungen"]] == ["vcarve", "gravur", "kontur"]


def _el(art, rgb, n=1):
    return {"id": "x", "art": art, "rgb": rgb, "linien": [{"punkte": [[0, 0], [n, 0]]}]}


def test_auftrag_reihenfolge_aktiv_und_durch():
    e = einst.laden(json.dumps({"bearbeitungen": [
        {"art": "kontur"}, {"art": "gravur", "aktiv": False}, {"art": "tasche"}]}))
    zu = einst.zuordnen(e, [_el("kontur", (0, 80, 255)), _el("gravur", (255, 140, 0)),
                            _el("tasche", (0, 160, 0))])
    a = einst.auftrag(e, zu, 18.0, "/tmp/x.tap")
    # ältere Zeichnung (automatische Reihenfolge): einmal sinnvoll geordnet, Kontur zuletzt
    assert [b["art"] for b in a["bearbeitungen"]] == ["tasche", "kontur"]   # Gravur ist aus
    assert a["bearbeitungen"][1]["tiefe"] == pytest.approx(18.2)            # ganz durch
    assert a["bearbeitungen"][1]["name"] == "Kontur (Blau)"
    assert a["hoehen"]["materialdicke"] == 18.0


def test_zuordnen_nach_farbton():
    e = einst.laden(json.dumps({"bearbeitungen": [
        {"art": "tasche", "tiefe": 2}, {"art": "tasche", "tiefe": 6}]}))
    f1, f2 = (e["bearbeitungen"][i]["farbe"] for i in (0, 1))
    from hirch_cam.farben import rgb_von
    zu = einst.zuordnen(e, [_el("tasche", rgb_von(f1)), _el("tasche", rgb_von(f2)),
                            _el("tasche", rgb_von(f2)), _el("tasche", (10, 170, 10))])
    b1, b2 = (e["bearbeitungen"][i]["id"] for i in (0, 1))
    assert len(zu[b1]) == 2 and len(zu[b2]) == 2         # leicht abweichendes Grün -> Tasche 1


@braucht_inkex
def test_analyse_schild():
    from hirch_cam import dokument
    erg = dokument.analyse(SCHILD)
    assert erg["ok"]
    assert erg["material"] == {"breite": 200.0, "hoehe": 100.0, "dicke": 18.0}
    bea = erg["einstellungen"]["bearbeitungen"]
    assert [b["art"] for b in bea] == ["vcarve", "gravur", "tasche", "kontur"]
    nach_art = {b["art"]: b["id"] for b in bea}
    assert erg["anzahl"][nach_art["vcarve"]] == 1 and erg["anzahl"][nach_art["kontur"]] == 1
    assert erg["ids"][nach_art["tasche"]] == ["ta1"]
    assert not any("anderer Farbe" in h for h in erg["hinweise"])   # Schwarz: noch ohne Bearbeitung
    assert erg["postprozessoren"][0][0] == "richauto_a11"


@braucht_inkex
def test_rechnen_schild_eigener_kern(tmp_path, monkeypatch):
    """Ganzer Ablauf mit eigenem Rechenkern: Dateien, Zeiten, Vorschau."""
    from hirch_cam import dokument
    svg = tmp_path / "schild.svg"
    text = open(SCHILD, encoding="utf-8").read().replace(
        'hirch:materialdicke="18"',
        'hirch:materialdicke="18" hirch:einstellungen="{&quot;rechenkern&quot;: &quot;intern&quot;, '
        '&quot;bearbeitungen&quot;: [{&quot;art&quot;: &quot;kontur&quot;, &quot;tiefe&quot;: 3}, '
        '{&quot;art&quot;: &quot;tasche&quot;}]}"')
    svg.write_text(text, encoding="utf-8")
    erg = dokument.rechnen(str(svg), str(tmp_path / "aus"), "teil")
    assert erg["ok"], erg
    # Standard RichAuto A11: je Werkzeug eine Datei mit kurzem Namen; dazu je Werkzeug ein
    # interner Teil für die Vorschau
    assert [os.path.basename(d) for d in erg["dateien"]] == ["teil_1.TAP", "teil_2.TAP", "teil_3.TAP"]
    namen = [os.path.basename(p["datei"]) for p in erg["programme"]]
    assert namen == ["teil_1_D3.tap", "teil_2_V60.tap", "teil_3_D6.tap"]
    assert not any("M6" in open(d, newline="").read() for d in erg["dateien"])
    assert len(erg["bearbeitungen"]) == 4
    assert erg["minuten"] > 0
    assert sum(b["minuten"] for b in erg["bearbeitungen"].values()) <= erg["minuten"] + 1e-6
    # Kontur ist nur 3 mm tief (eigene Einstellung)
    from hirch_cam.post import tap_mm
    job = tap_mm.read(open(erg["programme"][2]["datei"], newline="").read())
    assert min(m.z for tp in job.toolpaths for m in tp.moves) == pytest.approx(-3)


@braucht_inkex
def test_rechnen_nichts_zu_fraesen(tmp_path):
    from hirch_cam import dokument
    svg = tmp_path / "leer.svg"
    svg.write_text('<svg xmlns="http://www.w3.org/2000/svg" width="100mm" height="50mm" '
                   'viewBox="0 0 100 50"><path d="M0,0 L10,10" style="stroke:#000"/></svg>')
    erg = dokument.rechnen(str(svg), str(tmp_path / "aus"))
    assert not erg["ok"]
    assert "Nichts zu fräsen" in erg["warnungen"][0]


@braucht_inkex
def test_zwei_taschen_verschieden_tief(tmp_path):
    """Zwei Bearbeitungen derselben Fräsart: Ton der Linie entscheidet."""
    from hirch_cam import dokument
    from hirch_cam import cam
    from hirch_cam.post import tap_mm
    e = {"bearbeitungen": [
        {"id": "t1", "art": "tasche", "tiefe": 2, "zustellung": 2, "farbe": "#00a000"},
        {"id": "t2", "art": "tasche", "tiefe": 5, "zustellung": 5, "farbe": "#006300"}]}
    attr = json.dumps(e).replace('"', "&quot;")
    svg = tmp_path / "t.svg"
    svg.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
        'width="100mm" height="50mm" viewBox="0 0 100 50" hirch:materialdicke="18" '
        'hirch:einstellungen="%s">'
        '<rect id="a" x="10" y="10" width="30" height="30" style="fill:none;stroke:#00a000"/>'
        '<rect id="b" x="60" y="10" width="30" height="30" style="fill:none;stroke:#006300"/>'
        '</svg>' % attr, encoding="utf-8")
    an = dokument.analyse(str(svg))
    assert an["ids"] == {"t1": ["a"], "t2": ["b"]}
    assert an["namen"] == {"t1": "Tasche 1", "t2": "Tasche 2"}
    erg = dokument.rechnen(str(svg), str(tmp_path / "aus"))
    assert erg["ok"], erg
    assert set(erg["bearbeitungen"]) == {"t1", "t2"}
    if cam.verfuegbar():
        assert any("CAM-Modul" in z for z in erg["bericht"]), erg["bericht"]
    job = tap_mm.read(open(erg["programme"][0]["datei"], newline="").read())
    links = [m.z for tp in job.toolpaths for m in tp.moves if not m.rapid and m.x < 50]
    rechts = [m.z for tp in job.toolpaths for m in tp.moves if not m.rapid and m.x > 50]
    assert min(links) == pytest.approx(-2) and min(rechts) == pytest.approx(-5)


@braucht_inkex
def test_duplizierte_tasche_fraest_nochmals(tmp_path):
    """Duplikat (gleiche Farbe): dieselben Objekte, derselbe Weg nochmals – nicht
    «schon ausgeräumt»; anders gefärbt ist die Kopie leer."""
    from hirch_cam import dokument
    def svg_mit(farbe2):
        e = {"reihenfolge": "liste", "bearbeitungen": [
            {"id": "t1", "art": "tasche", "tiefe": 2, "zustellung": 2, "farbe": "#00a000", "gleichlauf": True},
            {"id": "t2", "art": "tasche", "tiefe": 2, "zustellung": 2, "farbe": farbe2, "gleichlauf": False}]}
        attr = json.dumps(e).replace('"', "&quot;")
        svg = tmp_path / ("d%s.svg" % farbe2[1:])
        svg.write_text(
            '<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
            'width="100mm" height="50mm" viewBox="0 0 100 50" hirch:materialdicke="18" '
            'hirch:einstellungen="%s">'
            '<rect id="a" x="10" y="10" width="30" height="30" style="fill:none;stroke:#00a000"/>'
            '</svg>' % attr, encoding="utf-8")
        return str(svg)
    s1 = svg_mit("#00a000")
    an = dokument.analyse(s1)
    assert an["ids"] == {"t1": ["a"], "t2": ["a"]}
    erg = dokument.rechnen(s1, str(tmp_path / "aus"))
    assert erg["ok"], erg
    m = {o["id"]: o["minuten"] for s in erg["ablauf"]["seiten"] for a in s["abschnitte"] for o in a["ops"]}
    assert m["t2"] == pytest.approx(m["t1"], rel=0.05) and m["t1"] > 0
    assert not any("schon ausgeräumt" in w for w in erg["warnungen"])
    an2 = dokument.analyse(svg_mit("#006300"))
    assert an2["ids"]["t2"] == []


@braucht_inkex
def test_eine_kontur_in_zwei_bearbeitungen(tmp_path):
    """hirch:auch: dieselbe Linie zusätzlich in einer zweiten Bearbeitung
    (erst mit V-Fräser fasen = Gravur, dann ausschneiden = Kontur)."""
    from hirch_cam import dokument
    e = {"bearbeitungen": [
        {"id": "g1", "art": "gravur", "tiefe": 1, "zustellung": 1, "farbe": "#ff8c00"},
        {"id": "k1", "art": "kontur", "tiefe": 6, "zustellung": 3, "farbe": "#1f5fd0",
         "stege_anzahl": 0}]}
    attr = json.dumps(e).replace('"', "&quot;")
    svg = tmp_path / "t.svg"
    svg.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
        'width="100mm" height="50mm" viewBox="0 0 100 50" hirch:materialdicke="18" '
        'hirch:einstellungen="%s">'
        '<rect id="a" x="10" y="10" width="30" height="30" hirch:auch="g1 weg" '
        'style="fill:none;stroke:#1f5fd0"/>'
        '<g hirch:auch="k1"><rect id="b" x="60" y="10" width="20" height="20" '
        'style="fill:none;stroke:#000000"/></g>'
        '</svg>' % attr, encoding="utf-8")
    an = dokument.analyse(str(svg))
    assert an["ids"] == {"g1": ["a"], "k1": ["a", "b"]}
    assert an["zusaetzlich"] == {"g1": 1, "k1": 1}
    assert not any("anderer Farbe" in h for h in an["hinweise"])
    erg = dokument.rechnen(str(svg), str(tmp_path / "aus"))
    assert erg["ok"], erg
    assert erg["bearbeitungen"]["g1"]["linien"] == 1
    assert erg["bearbeitungen"]["k1"]["linien"] == 2


@braucht_inkex
def test_im_panel_ausgeblendet_wird_trotzdem_gefraest(tmp_path):
    from hirch_cam import dokument
    svg = tmp_path / "v.svg"
    svg.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
        'width="100mm" height="50mm" viewBox="0 0 100 50">'
        '<rect id="a" x="10" y="10" width="30" height="30" hirch:versteckt="1" '
        'style="display:none;fill:none;stroke:#00a000"/>'
        '<rect id="b" x="60" y="10" width="30" height="30" style="display:none;fill:none;stroke:#00a000"/>'
        '</svg>', encoding="utf-8")
    an = dokument.analyse(str(svg))
    assert sum(an["ids"].values(), []) == ["a"]          # selbst ausgeblendet (b) zählt nicht


@braucht_inkex
def test_haltestege_von_hand(tmp_path):
    """Steg-Markierungen (hirch:steg) setzen die Stege an diese Stellen –
    sie selbst werden nie gefräst."""
    from hirch_cam import dokument
    from hirch_cam.post import tap_mm
    from hirch_cam import werkzeugdb as _wdb  # die Steuerung kommt aus der Maschine (27.10.)
    _wdb.maschine_setzen({"postprozessor": "vcarve_mm"})
    e = {"postprozessor": "vcarve_mm", "bearbeitungen": [
        {"id": "k1", "art": "kontur", "tiefe": 3, "zustellung": 3, "farbe": "#1f5fd0",
         "seite": "aussen", "stege_modus": "manuell", "stege_anzahl": 4,
         "stege_breite": 5, "stege_hoehe": 1}]}
    attr = json.dumps(e).replace('"', "&quot;")
    svg = tmp_path / "t.svg"
    svg.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
        'width="100mm" height="50mm" viewBox="0 0 100 50" hirch:materialdicke="3" '
        'hirch:einstellungen="%s">'
        '<rect id="a" x="10" y="10" width="30" height="20" style="fill:none;stroke:#1f5fd0"/>'
        '<g><circle cx="25" cy="10" r="2.5" hirch:steg="1" style="fill:#ff8c1a"/>'
        '<circle cx="40" cy="20" r="2.5" hirch:steg="1" style="fill:#ff8c1a"/>'
        '<circle cx="90" cy="45" r="2.5" hirch:steg="1" style="fill:#ff8c1a"/></g>'
        '</svg>' % attr, encoding="utf-8")
    an = dokument.analyse(str(svg))
    assert an["stege"] == 3
    erg = dokument.rechnen(str(svg), str(tmp_path / "aus"))
    assert erg["ok"], erg
    assert erg["bearbeitungen"]["k1"]["linien"] == 1          # Markierungen nicht gefräst
    assert any("keiner Kontur nahe" in w for w in erg["warnungen"])
    job = tap_mm.read(open(erg["dateien"][0], newline="").read())
    ms = [m for tp in job.toolpaths for m in tp.moves if not m.rapid]
    z = sorted({round(m.z, 3) for m in ms})
    tiefst, steg = z[0], z[0] + 1
    laeufe, auf = [], False
    for a, b in zip(ms, ms[1:]):
        jetzt = abs(a.z - steg) < 1e-3 and abs(b.z - steg) < 1e-3
        if jetzt and not auf:
            laeufe.append(0.0)
        if jetzt:
            laeufe[-1] += ((b.x - a.x) ** 2 + (b.y - a.y) ** 2) ** 0.5
        auf = jetzt
    assert len(laeufe) == 2 and all(abs(l - 5) < 0.2 for l in laeufe), laeufe
    # automatisch: vier Stege
    e["bearbeitungen"][0]["stege_modus"] = "auto"
    svg.write_text(svg.read_text().replace(attr, json.dumps(e).replace('"', "&quot;")))
    erg = dokument.rechnen(str(svg), str(tmp_path / "aus2"))
    job = tap_mm.read(open(erg["dateien"][0], newline="").read())
    ms = [m for tp in job.toolpaths for m in tp.moves if not m.rapid]
    n = sum(1 for a, b in zip(ms, ms[1:]) if abs(b.z - steg) < 1e-3 and abs(a.z - steg) > 1e-3)
    assert n == 4


def test_materialdicke_mit_komma():
    """Ältere Versionen schrieben die Dicke in de/it mit Komma («1,75»)."""
    from lxml import etree
    from hirch_cam.svg_lesen import materialdicke
    from hirch_cam.einstellungen import DICKE
    el = etree.Element("svg")
    el.set(DICKE, "1,75")
    assert materialdicke(el) == 1.75
    el.set(DICKE, "1.75")
    assert materialdicke(el) == 1.75


@braucht_inkex
def test_vorschau_nur_eine_bearbeitung_mit_dialogwerten(tmp_path):
    """Vorschau im Dialog: rechnet nur diese Bearbeitung, mit den Werten aus dem
    Fenster (hier: Planfräsen X statt Y) – die Zeichnung bleibt unverändert."""
    from hirch_cam import dokument
    e = {"bearbeitungen": [
        {"id": "t1", "art": "tasche", "tiefe": 2, "zustellung": 2, "farbe": "#00a000"},
        {"id": "p1", "art": "planen", "tiefe": 0.5, "richtung": "x"}]}
    attr = json.dumps(e).replace('"', "&quot;")
    svg = tmp_path / "t.svg"
    svg.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
        'width="100mm" height="50mm" viewBox="0 0 100 50" hirch:materialdicke="18" '
        'hirch:einstellungen="%s">'
        '<rect id="a" x="10" y="10" width="30" height="30" style="fill:none;stroke:#00a000"/>'
        '</svg>' % attr, encoding="utf-8")

    def laufrichtung(r):
        zuege = r["bahnen"]["p1"]["zuege"]
        dx = dy = 0.0
        for z in zuege:
            for i in range(0, len(z) - 2, 2):
                dx += abs(z[i + 2] - z[i])
                dy += abs(z[i + 3] - z[i + 1])
        return "x" if dx > dy else "y"
    r = dokument.vorschau(str(svg), json.dumps(e), "p1", str(tmp_path / "v"))
    assert r["ok"] and set(r["bahnen"]) == {"p1"}
    assert laufrichtung(r) == "x"
    e["bearbeitungen"][1]["richtung"] = "y"
    r = dokument.vorschau(str(svg), json.dumps(e), "p1", str(tmp_path / "v"))
    assert laufrichtung(r) == "y"


@braucht_inkex
def test_kontur_ohne_steg_nennt_die_objekte(tmp_path):
    """Stege von Hand: die Prüfung nennt die Konturen ohne Steg mit ihrer ID
    (das Panel wählt sie per Klick aus)."""
    from hirch_cam import dokument
    e = {"bearbeitungen": [
        {"id": "k1", "art": "kontur", "tiefe": 3, "zustellung": 3, "farbe": "#1f5fd0",
         "seite": "aussen", "stege_modus": "manuell", "stege_anzahl": 4,
         "stege_breite": 5, "stege_hoehe": 1}]}
    attr = json.dumps(e).replace('"', "&quot;")
    svg = tmp_path / "t.svg"
    svg.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
        'width="100mm" height="50mm" viewBox="0 0 100 50" hirch:materialdicke="3" '
        'hirch:einstellungen="%s">'
        '<rect id="a" x="10" y="10" width="30" height="20" style="fill:none;stroke:#1f5fd0"/>'
        '<rect id="b" x="55" y="10" width="20" height="20" style="fill:none;stroke:#1f5fd0"/>'
        '<rect id="c" x="80" y="10" width="15" height="20" style="fill:none;stroke:#1f5fd0"/>'
        '<circle cx="25" cy="10" r="2.5" hirch:steg="1" style="fill:#ff8c1a"/>'
        '</svg>' % attr, encoding="utf-8")
    erg = dokument.rechnen(str(svg), str(tmp_path / "aus"))
    assert erg["ok"], erg
    b = [x for x in erg["pruefung"] if "ohne Haltesteg" in x["text"]]
    assert len(b) == 1 and b[0]["objekte"] == ["b", "c"], erg["pruefung"]
    assert "2 Konturen" in b[0]["text"]
    assert not any("ohne Haltesteg" in w for w in erg["bearbeitungen"]["k1"].get("warnungen", []))


@braucht_inkex
def test_vorschub_faktor_nur_fraesvorschub(tmp_path):
    """Faktor aus der Zeitleiste: Fräsvorschübe × Faktor, Eintauchen bleibt; Teilzeit im
    Vorschub wird gemeldet; zu schnell → Hinweis."""
    from hirch_cam import dokument
    from hirch_cam.post import tap_mm

    def rechnen(faktor):
        b = {"id": "t1", "art": "tasche", "tiefe": 2, "zustellung": 1, "farbe": "#00a000"}
        if faktor:
            b["vorschub_faktor"] = faktor
        attr = json.dumps({"bearbeitungen": [b]}).replace('"', "&quot;")
        svg = tmp_path / "t.svg"
        svg.write_text(
            '<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
            'width="100mm" height="50mm" viewBox="0 0 100 50" hirch:materialdicke="18" '
            'hirch:einstellungen="%s">'
            '<rect id="a" x="10" y="10" width="30" height="20" style="fill:none;stroke:#00a000"/>'
            '</svg>' % attr, encoding="utf-8")
        erg = dokument.rechnen(str(svg), str(tmp_path / ("aus%s" % faktor)))
        job = tap_mm.read(open(erg["programme"][0]["datei"], newline="").read())
        feeds = sorted({m.feed for tp in job.toolpaths for m in tp.moves if not m.rapid and m.feed})
        return erg, feeds
    e1, f1 = rechnen(None)
    e2, f2 = rechnen(150)
    assert e2["minuten"] < e1["minuten"]
    assert max(f2) == pytest.approx(min(max(f1) * 1.5, 5000), rel=0.01)
    assert min(f2) == pytest.approx(min(f1), rel=0.01)          # Eintauchen unverändert
    assert e1["teil_vorschub"]["t1"] > 0
    assert e2["bearbeitungen"]["t1"]["faktor"] == 150
    e3, _f = rechnen(400)
    assert any("Vorschub 400 %" in x["text"] for x in e3["pruefung"])


def test_wiederaufnahme_ab_bearbeitung_und_ab_zeile(tmp_path):
    """«Ab hier neu ausgeben»: ab einer Bearbeitung nur noch diese und die folgenden; ab einer
    Zeile beginnt das Programm über die Sicherheitshöhe am Punkt davor, ohne Prüfung."""
    from hirch_cam import dokument
    from hirch_cam.post import tap_mm
    bs = [{"id": "t1", "art": "tasche", "tiefe": 2, "zustellung": 1, "farbe": "#00a000"},
          {"id": "k1", "art": "kontur", "tiefe": 3, "zustellung": 1.5, "farbe": "#0000ff",
           "stege": False}]
    attr = json.dumps({"bearbeitungen": bs}).replace('"', "&quot;")
    svg = tmp_path / "w.svg"
    svg.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
        'width="100mm" height="50mm" viewBox="0 0 100 50" hirch:materialdicke="18" '
        'hirch:einstellungen="%s">'
        '<rect id="a" x="10" y="10" width="30" height="20" style="fill:none;stroke:#00a000"/>'
        '<rect id="b" x="55" y="10" width="30" height="20" style="fill:none;stroke:#0000ff"/>'
        '</svg>' % attr, encoding="utf-8")
    voll = dokument.rechnen(str(svg), str(tmp_path / "voll"))
    assert voll["ok"]
    keys = [a["key"] for p in voll["programme"] for a in p["abschnitte"]]
    assert "t1" in keys and "k1" in keys
    nur_k = dokument.rechnen(str(svg), str(tmp_path / "k"), ab={"key": "k1"})
    assert nur_k["ok"] and "pruefung" not in nur_k
    assert [a["key"] for p in nur_k["programme"] for a in p["abschnitte"]] == keys[keys.index("k1"):]
    assert nur_k["minuten"] < voll["minuten"]
    # ab Zeile 40 des ersten Programms
    teil = open(voll["programme"][0]["datei"], newline="").read().splitlines()
    z = dokument.rechnen(str(svg), str(tmp_path / "z"), ab={"programm": 1, "zeile": 40})
    assert z["ok"] and z["minuten"] < voll["minuten"]
    job = tap_mm.read(open(z["programme"][0]["datei"], newline="").read())
    m = job.toolpaths[0].moves
    assert m[0].rapid and m[0].z == pytest.approx(job.safe_z)
    assert all(x.z >= -1e-6 for x in m[:2])
    assert not dokument.rechnen(str(svg), str(tmp_path / "x"), ab={"key": "gibtsnicht"})["ok"]
    assert len(teil) > 40


def test_klone_werden_gefraest(tmp_path):
    """Klone (<use>) – auch eines Klons – werden wie gezeichnete Objekte gefräst und
    erscheinen im Panel unter der ID des Klons."""
    if not _inkex():
        pytest.skip("inkex nicht gefunden")
    import inkex
    from hirch_cam import svg_lesen
    p = tmp_path / "k.svg"
    p.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
        'width="100mm" height="50mm" viewBox="0 0 100 50">'
        '<g id="orig" transform="translate(5,0)"><rect id="r" x="5" y="5" width="10" height="10" '
        'style="fill:none;stroke:#0000ff"/></g>'
        '<use id="klon1" xlink:href="#orig" x="20" y="0"/>'
        '<use id="klon2" xlink:href="#klon1" transform="translate(0,20)"/>'
        '</svg>', encoding="utf-8")
    svg = inkex.load_svg(str(p)).getroot()
    info = svg_lesen.lesen(svg)
    ids = [e["id"] for e in info["elemente"]]
    assert ids == ["r", "klon1", "klon2"]
    xs = sorted(min(x for x, _y in e["linien"][0]["punkte"]) for e in info["elemente"])
    assert xs == pytest.approx([10, 30, 30])
    ys = sorted(min(y for _x, y in e["linien"][0]["punkte"]) for e in info["elemente"])
    # Y nach oben: Rechteck 5…15 von oben → 35…45; der zweite Klon 20 tiefer → 15…25
    assert ys == pytest.approx([15, 35, 35])


def test_spannvorrichtung_eigene_seite(tmp_path):
    """Vorrichtung: eigene Seite (Platte 0) mit eigener Dicke; ein Nullpunkt am Werkstück (Peter, 21.10.) –
    die Vorrichtung läuft mit demselben Maschinenpunkt (um die Lage verschoben); die Wände der Aufnahme
    werden Spannmittel."""
    if not _inkex():
        pytest.skip("inkex nicht gefunden")
    from hirch_cam import dokument
    bs = [{"id": "t1", "art": "tasche", "tiefe": 2, "zustellung": 1, "farbe": "#00a000"},
          {"id": "tv", "art": "tasche", "tiefe": 25, "zustellung": 5, "farbe": "#00e000"}]
    e = {"bearbeitungen": bs, "vorrichtung": {"an": True, "dicke": 30, "lage": [50, 40], "mittig": False}}
    attr = json.dumps(e).replace('"', "&quot;")
    svg = tmp_path / "v.svg"
    # Werkstück 100 × 50 × 18 auf Seite 1; Vorrichtung 200 × 130 links daneben (x −220)
    svg.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
        'xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
        'xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd" '
        'width="100mm" height="50mm" viewBox="0 0 100 50" hirch:materialdicke="18" '
        'hirch:einstellungen="%s"><sodipodi:namedview id="nv">'
        '<inkscape:page id="p1" x="0" y="0" width="100" height="50"/>'
        '<inkscape:page id="pv" x="-220" y="0" width="200" height="130" hirch:vorrichtung="1"/>'
        '</sodipodi:namedview>'
        '<rect id="w" x="10" y="10" width="30" height="20" style="fill:none;stroke:#00a000"/>'
        # Aufnahme: Tasche in der Vorrichtung, genau unter dem Werkstück (Lage 50/40, 100 × 50)
        '<rect id="a" x="-170" y="40" width="100" height="50" style="fill:none;stroke:#00e000"/>'
        '</svg>' % attr, encoding="utf-8")
    an = dokument.analyse(str(svg), 0)
    assert an["material"]["breite"] == pytest.approx(200) and an["material"]["dicke"] == pytest.approx(30)
    assert an["anzahl"].get("tv") == 1 and an["anzahl"].get("t1", 0) == 0
    assert an["vorrichtung"]["lage"] == [50, 40] and an["vorrichtung"]["umrisse"]
    v = dokument.rechnen(str(svg), str(tmp_path / "v"), "teil", platte=0)
    assert v["ok"] and set(v["bearbeitungen"]) == {"tv"}
    w = dokument.rechnen(str(svg), str(tmp_path / "w"), "teil", platte=1)
    assert w["ok"] and set(w["bearbeitungen"]) == {"t1"}
    # Wände (25 − 18 = 7 mm über dem Werkstück) als Spannmittel
    assert w["spannmittel"] and all(s["hoehe"] == pytest.approx(7) for s in w["spannmittel"])
    # ein Nullpunkt: unten links am Werkstück. Werkstück-G-Code unverschoben, die Vorrichtung um −Lage
    import re

    def xs_von(erg):
        text = open(erg["datei"], newline="").read()
        return [x for x in (float(v) for v in re.findall(r"X(-?[\d.]+)", text)) if abs(x) > 1e-9]
    xw = xs_von(w)
    assert min(xw) > 10 - 1e-6 and max(xw) < 40 + 1e-6
    xv = xs_von(v)                                       # Aufnahme in der Vorrichtung bei x 50 … 150
    assert min(xv) > 0 - 1e-6 and max(xv) < 100 + 1e-6
    # die Zeichenfläche bekommt dieselben Punkte (Dokument-mm): Werkstück unten links, Vorrichtung bei Lage
    an1 = dokument.analyse(str(svg), 1)
    np_ = {n["platte"]: n for n in an1["nullpunkte"]}
    assert (np_[1]["x"], np_[1]["y"]) == pytest.approx((0, 50))
    assert (np_[0]["x"], np_[0]["y"]) == pytest.approx((-220 + 50, 130 - 40))
