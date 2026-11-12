"""mm und Zoll (Peter 08.11.2026: «für den Release fertig»). Intern bleibt alles mm; umgerechnet werden nur Texte an
die Oberfläche (hirch_cam/einheit.py, gleiche Regeln wie einheit_text in hirch-sprache.cpp)."""
import pytest

from hirch_cam import einheit, sprache


@pytest.fixture
def zoll(monkeypatch):
    monkeypatch.setenv("HIRCH_EINHEIT", "in")
    monkeypatch.setenv("HIRCH_SPRACHE", "de")


def test_ohne_zoll_bleibt_alles(monkeypatch):
    monkeypatch.delenv("HIRCH_EINHEIT", raising=False)
    t = "Tiefe 6 mm, Vorschub 1 200 mm/min, Fläche 50 mm², Ø 6"
    assert einheit.text(t) == t
    assert sprache.ausgabe({"hinweise": [t]}) == {"hinweise": [t]} or sprache.englisch()


def test_laengen_vorschub_flaeche(zoll):
    assert einheit.text("Tiefe 6 mm") == "Tiefe 0,236 Zoll"
    assert einheit.text("Tisch 600 × 900 mm") == "Tisch 23,622 × 35,433 Zoll"
    assert einheit.text("Vorschub 1 200 mm/min") == "Vorschub 47,2 Zoll/min"
    assert einheit.text("auf 1369 mm² bleiben") == "auf 2,12 Zoll² bleiben"
    assert einheit.text("Zugabe 0,25 mm") == "Zugabe 0,01 Zoll"


def test_durchmesser_ohne_einheit(zoll):
    assert einheit.text("Der Fräser Ø 6 passt nicht") == "Der Fräser Ø 0,236 Zoll passt nicht"
    assert einheit.text("Schaftfräser Ø 6 mm") == "Schaftfräser Ø 0,236 Zoll"
    assert einheit.text("V-Fräser 60°") == "V-Fräser 60°"                # Winkel bleibt


def test_woerter_mit_mm_bleiben(zoll):
    for t in ("Programm läuft", "Kommentar", "Summe"):
        assert einheit.text(t) == t


def test_englisch_in(monkeypatch):
    monkeypatch.setenv("HIRCH_EINHEIT", "in")
    monkeypatch.setenv("HIRCH_SPRACHE", "en")
    assert einheit.text("Depth 6 mm") == "Depth 0.236 in"
    assert einheit.text("4 × Ø 6 in the corners") == "4 × Ø 0.236 in in the corners"


def test_ausgabe_rechnet_texte_aber_keine_daten(zoll):
    aus = sprache.ausgabe({"hinweise": ["Tiefe 6 mm"], "einstellungen": {"text": "6 mm"}, "tiefe": 6.0})
    assert aus["hinweise"] == ["Tiefe 0,236 Zoll"]
    assert aus["einstellungen"] == {"text": "6 mm"}          # Daten der Zeichnung bleiben
    assert aus["tiefe"] == 6.0                                # Zahlen bleiben mm


def test_zahl_und_rueckweg(zoll):
    assert einheit.zahl(25.4) == "1 Zoll"
    assert einheit.lesen(einheit.zeigen(12.7)) == pytest.approx(12.7)
    assert einheit.zahl(3000, 0, groesse="vorschub") == "118,1 Zoll/min"


def test_startliste_zoll(tmp_path, monkeypatch, capsys):
    """Ersteinrichtung: Startliste in Zoll – Masse in mm, Namen wie im Handel."""
    import json
    from hirch_cam import werkzeugdb
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))
    monkeypatch.setenv("APPDATA", str(tmp_path))
    werkzeugdb.main(["startliste", json.dumps({"art": "zoll"})])
    capsys.readouterr()
    ws = werkzeugdb.laden()["werkzeuge"]
    viertel = next(w for w in ws if w["name"] == 'Schaftfräser 1/4"')
    assert viertel["durchmesser"] == pytest.approx(6.35)
    assert all("mm" not in w["name"] for w in ws)
    werkzeugdb.main(["startliste", json.dumps({"art": "mm"})])
    assert any(w["name"] == "Schaftfräser 6 mm" for w in werkzeugdb.laden()["werkzeuge"])


def test_startliste_zoll_ebenbuertig():
    """Peter 10.11.: Die Startliste in Zoll ist der metrischen ebenbürtig – jede Art, gleiche Felder, alle Masse
    genau aus Zoll gerechnet (1/4" = 6,35 mm), Namen englisch wie im Handel über en.json."""
    import json
    import math
    import os
    from hirch_cam import werkzeugdb
    zoll, mm = werkzeugdb.BEISPIELE_ZOLL, werkzeugdb.BEISPIELE
    assert len(zoll) >= 25
    assert {w["typ"] for w in mm} <= {w["typ"] for w in zoll}
    assert {w.get("drall") for w in mm} <= {w.get("drall") for w in zoll}
    felder = set().union(*(w.keys() for w in mm))
    for w in zoll:
        assert set(w) <= felder, w["id"]
        assert set(w) >= set(werkzeugdb._w("x", "x", "schaft", 1)), w["id"]
    assert len({w["id"] for w in zoll}) == len(zoll)
    assert not {w["id"] for w in zoll} & {w["id"] for w in mm}

    def bruch(mm_wert):                       # ganze 1/64" (bzw. 1/1000" bei der Spitze)
        return abs(mm_wert / 25.4 * 64 - round(mm_wert / 25.4 * 64)) < 1e-9
    for w in zoll:
        if w["typ"] != "konisch":
            assert bruch(w["durchmesser"]), w["id"]
        assert bruch(w["schaft"]), w["id"]
        if w["typ"] in ("kugel", "eckradius", "konisch"):
            assert bruch(w["eckradius"]), w["id"]
    assert next(w for w in zoll if w["id"] == "zs14")["durchmesser"] == 6.35
    assert next(w for w in zoll if w["id"] == "zg30")["spitze"] == 25.4 * 0.010
    kk = next(w for w in zoll if w["id"] == "zkk")
    assert kk["durchmesser"] == 2 * (25.4 / 64 + 25.4 * math.tan(math.radians(1.8)))
    en = json.load(open(os.path.join(os.path.dirname(werkzeugdb.__file__), "sprache", "en.json"), encoding="utf-8"))
    for w in zoll:
        assert w["name"] in en and "mm" not in en[w["name"]], w["name"]
        assert not w["notiz"] or w["notiz"] in en, w["notiz"]


def test_durchmesser_als_bruch(zoll):
    """Fräser in Zoll heissen im Handel 1/4", 1/8" … – nur nach «Ø» und nur, wenn der Wert ein Bruch ist."""
    assert einheit.text("Schaftfräser Ø 6,35 mm") == "Schaftfräser Ø 1/4 Zoll"
    assert einheit.text("Ø 3,18 mm") == "Ø 1/8 Zoll"
    assert einheit.text("Ø 38,1 mm") == "Ø 1-1/2 Zoll"
    assert einheit.text("Der Fräser Ø 12,7 passt") == "Der Fräser Ø 1/2 Zoll passt"
    assert einheit.text("Ø 6 mm") == "Ø 0,236 Zoll"            # metrisch bleibt Dezimalzahl
    assert einheit.text("Ø 25 mm") == "Ø 0,984 Zoll"
    assert einheit.text("Ø 0,8 mm") == "Ø 0,031 Zoll"           # kein Bruch (0,0315 ist nicht 1/32)
    assert einheit.text("Tiefe 6,35 mm") == "Tiefe 0,25 Zoll"   # nur nach Ø
    assert einheit.text("Ø 6,35 × 20 mm") == "Ø 0,25 × 0,787 Zoll"


def test_durchmesser_nicht_doppelt(zoll):
    for t in ("Ø 0,236 Zoll", "Ø 1/4 Zoll", "Ø 1-1/4 Zoll"):
        assert einheit.text(t) == t
    assert einheit.text(einheit.text("Ø 6")) == "Ø 0,236 Zoll"


def test_bruch():
    assert [einheit.bruch(x) for x in (0.25, 0.125, 1.0, 1.25, 0.0625, 0.03125, 0.03, 0.236, 0.015625)] == \
        ["1/4", "1/8", "1", "1-1/4", "1/16", "1/32", "", "", ""]


def test_spanne_mit_auslassung(zoll):
    """Windows-Abnahme 09.11.: «Bereich X -4 … 204 mm» – beide Zahlen umrechnen."""
    assert einheit.text("Bereich X -25,4 … 254 mm, Y 0 … 50,8 mm") == "Bereich X -1 … 10 Zoll, Y 0 … 2 Zoll"


def test_rechnung_text_in_zoll(monkeypatch):
    """Windows-Abnahme 09.11.: «× 2 × 0,12 mm» darf nicht als Kette gelesen werden (Schneiden bleiben 2)."""
    from hirch_cam import werkzeugdb
    monkeypatch.setenv("HIRCH_SPRACHE", "de")
    w = {"id": "x", "name": "Schaftfräser 6 mm", "typ": "schaft", "durchmesser": 6, "schneiden": 2}
    monkeypatch.delenv("HIRCH_EINHEIT", raising=False)
    mm = werkzeugdb.schnittdaten(w, "mdf")["rechnung_text"]
    assert " U/min × 2 × " in mm and "mm/min" in mm
    monkeypatch.setenv("HIRCH_EINHEIT", "in")
    z = werkzeugdb.schnittdaten(w, "mdf")["rechnung_text"]
    assert " U/min × 2 × " in z and "mm" not in z and "Zoll/min" in z, z
