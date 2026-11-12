"""Werkzeugdatenbank und Schnittdaten."""
import json

from hirch_cam import einstellungen, werkzeugdb


def test_beispiele_und_schnittdaten():
    db = werkzeugdb.laden("/gibt/es/nicht.json")
    namen = [w["name"] for w in db["werkzeuge"]]
    assert "Gravierstichel 36° / 0,2 mm" in namen and "Planfräser 22 mm" in namen
    s6 = next(w for w in db["werkzeuge"] if w["id"] == "s6")
    holz = werkzeugdb.schnittdaten(s6, "hartholz")
    alu = werkzeugdb.schnittdaten(s6, "aluminium")
    assert holz["drehzahl"] == 21500                      # vc 405 m/min bei Ø 6
    assert holz["vorschub"] >= 3000                       # Mittelwert der Herstellertabellen (Maschine bis 5000)
    assert alu["drehzahl"] < holz["drehzahl"] and alu["vorschub"] < holz["vorschub"]
    assert alu["zustellung"] < holz["zustellung"]
    assert werkzeugdb.schnittdaten(s6, "messing")["vorschub"] < alu["vorschub"]


def test_speichern_loeschen_maschine(tmp_path):
    datei = str(tmp_path / "w.json")
    w = werkzeugdb.speichern({"name": "Mein Fräser", "typ": "schaft", "durchmesser": 5,
                              "vorschub": 99}, datei)
    db = werkzeugdb.laden(datei)
    assert any(x["id"] == w["id"] and "vorschub" not in x for x in db["werkzeuge"])
    werkzeugdb.loeschen(w["id"], datei)
    werkzeugdb.maschine_setzen({"x": 610}, datei)
    db = werkzeugdb.laden(datei)
    assert all(x["id"] != w["id"] for x in db["werkzeuge"])
    assert db["maschine"]["x"] == 610 and db["maschine"]["y"] == 900


def test_alte_zeichnung_behaelt_vorschub():
    e = einstellungen.laden(json.dumps({"werkzeuge": {"A": {"typ": "schaft", "durchmesser": 6,
                                                            "vorschub": 777}}}))
    assert e["werkzeuge"]["A"]["auto"] is False
    w = werkzeugdb.wirksam(e["werkzeuge"], "aluminium")
    assert w["A"]["vorschub"] == 777
    neu = einstellungen.laden(None)
    w = werkzeugdb.wirksam(neu["werkzeuge"], "aluminium")
    assert w["C"]["vorschub"] == werkzeugdb.schnittdaten(neu["werkzeuge"]["C"], "aluminium")["vorschub"]


def test_hartschaum():
    """Hartschaum (Styrodur, XPS): grosser Zahnvorschub (sonst schmilzt er), Zustellung 1 × Ø
    (Herstellertabellen für Schaum: voller Zahnvorschub bis 1 × Ø)."""
    from hirch_cam import werkzeugdb
    assert werkzeugdb.MATERIALIEN["hartschaum"]["name"] == "Hartschaum (Styrodur, XPS)"
    sd = werkzeugdb.schnittdaten({"typ": "schaft", "durchmesser": 6, "schneiden": 2, "schneidenlaenge": 22},
                                 "hartschaum")
    holz = werkzeugdb.schnittdaten({"typ": "schaft", "durchmesser": 6, "schneiden": 2, "schneidenlaenge": 22},
                                   "weichholz")
    assert sd["fz"] > holz["fz"] and 0.15 <= sd["fz"] <= 0.2          # Ø 6 mm: 0,15–0,2 mm je Zahn
    assert sd["zustellung"] == 6.0                                    # 1 × Ø


def test_drehzahl_von_hand_vorschub_gerechnet(tmp_path):
    """Drehzahl an der Spindel von Hand (wo es am wenigsten vibriert): Hirch CNC rechnet den
    Vorschub dazu – Drehzahl × Schneiden × Zahnvorschub des Werkstoffs."""
    from hirch_cam import werkzeugdb
    w = {"typ": "schaft", "durchmesser": 6, "schneiden": 2, "schneidenlaenge": 22,
         "schnittdaten": "drehzahl", "drehzahl": 18000, "vorschub": 111, "eintauchen": 11}
    fz = werkzeugdb.schnittdaten(w, "hartholz")["fz"]
    erg = werkzeugdb.wirksam({"A": w}, "hartholz")["A"]
    assert erg["drehzahl"] == 18000                                   # bleibt, wie eingetragen
    assert abs(erg["vorschub"] - 18000 * 2 * fz) <= 10
    assert 0 < erg["eintauchen"] < erg["vorschub"]
    assert erg["schnittdaten"] == "drehzahl"
    # andere Drehzahl → anderer Vorschub, gleicher Span je Schneide
    w2 = dict(w, drehzahl=12000)
    erg2 = werkzeugdb.wirksam({"A": w2}, "hartholz")["A"]
    assert abs(erg2["vorschub"] / 12000 - erg["vorschub"] / 18000) < 0.002
    # Maschine zu langsam: Vorschub begrenzt, gemeldet
    langsam = werkzeugdb.wirksam({"A": w}, "hartholz", {"vorschub_max": 1000})["A"]
    assert langsam["vorschub"] == 1000 and langsam["empfehlung"]["vorschub_begrenzt"]
    # automatisch: die Drehzahl kommt aus dem Werkstoff; von Hand: alles bleibt
    auto = werkzeugdb.wirksam({"A": dict(w, schnittdaten="auto")}, "hartholz")["A"]
    assert auto["drehzahl"] != 18000 or auto["vorschub"] != erg["vorschub"]
    hand = werkzeugdb.wirksam({"A": dict(w, schnittdaten="hand")}, "hartholz")["A"]
    assert (hand["drehzahl"], hand["vorschub"], hand["eintauchen"]) == (18000, 111, 11)
    # alte Zeichnungen: nur «auto»
    assert werkzeugdb.schnittmodus({"auto": True}) == "auto"
    assert werkzeugdb.schnittmodus({"auto": False}) == "hand"
    # «Meine Werkzeuge»: die Drehzahl von Hand wird mitgespeichert
    datei = str(tmp_path / "w.json")
    gesp = werkzeugdb.speichern(dict(w, name="Test 6"), datei)
    assert gesp.get("drehzahl") == 18000 and gesp.get("schnittdaten") == "drehzahl"
    gesp = werkzeugdb.speichern(dict(w, name="Test 6b", schnittdaten="auto"), datei)
    assert "drehzahl" not in gesp


def test_drehzahl_von_hand_warnung_maschine_zu_langsam(tmp_path, monkeypatch):
    """Die Maschine schafft den passenden Vorschub nicht: Warnung mit dem Rat, die Drehzahl zu senken."""
    from hirch_cam import engine, werkzeugdb
    monkeypatch.setenv("HIRCH_KONFIG", str(tmp_path))
    roh = {"A": {"typ": "schaft", "durchmesser": 6, "schneiden": 2, "schneidenlaenge": 22,
                 "nummer": 1, "schnittdaten": "drehzahl", "drehzahl": 18000}}
    auftrag = {
        "material": "hartholz",
        "maschine": {"vorschub_max": 1000},
        "werkzeuge": werkzeugdb.wirksam(roh, "hartholz", {"vorschub_max": 1000}),
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 18},
        "tasche": {"tiefe": 2, "zustellung": 1, "werkzeug": "A"},
        "postprozessor": "linuxcnc_mm",
        "ausgabe": str(tmp_path / "teil.ngc"),
        "objekte": {"tasche": [{"punkte": [[0, 0], [30, 0], [30, 20], [0, 20]], "geschlossen": True}]},
    }
    erg = engine.rechnen(auftrag)
    assert erg["ok"]
    assert any("18 000 U/min wären" in t and "Drehzahl etwas senken" in t for t in erg["warnungen"])
    code = open(erg["datei"], encoding="utf-8").read()
    assert "S18000" in code
    auftrag["maschine"] = {"vorschub_max": 20000}
    auftrag["werkzeuge"] = werkzeugdb.wirksam(roh, "hartholz", auftrag["maschine"])
    erg = engine.rechnen(auftrag)
    assert not any("U/min wären" in t for t in erg["warnungen"])


def test_gaengige_fraeser_und_datenbank_version_3(tmp_path):
    import json as _json
    from hirch_cam import werkzeugdb
    ids = {w["id"] for w in werkzeugdb.BEISPIELE}
    assert set(werkzeugdb.NEU_SEIT_V3) <= ids and len(ids) == len(werkzeugdb.BEISPIELE)
    s3l = next(w for w in werkzeugdb.BEISPIELE if w["id"] == "s3l")
    assert (s3l["durchmesser"], s3l["schaft"]) == (3.0, 6.0)
    # alte Datenbank (Version 2, eigene Werkzeuge): neue kommen dazu, eigene bleiben
    datei = tmp_path / "w.json"
    datei.write_text(_json.dumps({"version": 2, "werkzeuge": [{"id": "w1", "name": "Mein Fräser", "typ": "schaft",
                                                              "durchmesser": 4}], "maschine": {}}))
    db = werkzeugdb.laden(str(datei))
    neu = {w["id"] for w in db["werkzeuge"]}
    assert "w1" in neu and set(werkzeugdb.NEU_SEIT_V3) <= neu and db["version"] == werkzeugdb.DB_VERSION
    # Version 1: Bohrer kommen wie bisher, dazu die neuen
    datei.write_text(_json.dumps({"werkzeuge": [{"id": "w1", "typ": "schaft", "durchmesser": 4}]}))
    assert {"b3", "b5", "b6", "b25", "s3l"} <= {w["id"] for w in werkzeugdb.laden(str(datei))["werkzeuge"]}
    # Version 3: nichts mehr ergänzen (gelöschte Beispiele bleiben gelöscht)
    datei.write_text(_json.dumps({"version": 3, "werkzeuge": [{"id": "w1", "typ": "schaft", "durchmesser": 4}]}))
    assert [w["id"] for w in werkzeugdb.laden(str(datei))["werkzeuge"]] == ["w1"]


def test_schaft_dicker_als_schneide_meldet():
    from hirch_cam.pruefung import pruefen
    w = {"typ": "schaft", "durchmesser": 3.0, "schaft": 6.0, "schneidenlaenge": 12.0}
    auftrag = {"werkzeuge": {"A": w}, "hoehen": {"materialdicke": 30},
               "bearbeitungen": [{"id": "t1", "art": "tasche", "werkzeug": "A", "tiefe": 15, "zustellung": 3,
                                  "name": "Tasche", "objekte": [{"punkte": [[0, 0], [20, 0], [20, 20], [0, 20], [0, 0]],
                                                                 "geschlossen": True}]}]}
    befunde = pruefen(auftrag, {"statistik": {}})
    assert any("Schaft Ø 6 mm dicker als die Schneide" in b["text"] for b in befunde), befunde


def test_katalog_merkmale_fuer_die_fraeserbilder():
    """Die Karten der Werkzeugwahl zeichnen Drall, Wendeplatten und Spanbrecher (Peter, 19.10.)."""
    from hirch_cam import werkzeugdb
    k = {w["id"]: w for w in werkzeugdb.BEISPIELE}
    assert k["p22"]["wendeplatten"] and k["p32"]["wendeplatten"]
    assert k["d3"]["drall"] == k["d6"]["drall"] == "ab"
    assert k["c6"]["drall"] == "kompression"
    assert k["r6s"]["spanbrecher"]
    assert "drall" in werkzeugdb._GEOMETRIE and werkzeugdb._wert_text("drall", "ab") == "abwärts"


def test_schreiben_folgt_einem_link(tmp_path):
    """werkzeuge.json als Link in einen synchronisierten Ordner: der Link bleibt (Peter, 20.10.)."""
    import os
    from hirch_cam import werkzeugdb
    ziel = tmp_path / "nextcloud" / "werkzeuge.json"
    ziel.parent.mkdir()
    link = tmp_path / "konfig" / "werkzeuge.json"
    link.parent.mkdir()
    werkzeugdb.schreiben(werkzeugdb.laden(str(ziel)), str(ziel))
    try:
        os.symlink(ziel, link)
    except OSError:
        import pytest
        pytest.skip("Verknüpfungen braucht unter Windows Adminrechte oder den Entwicklermodus")
    db = werkzeugdb.laden(str(link))
    db["werkzeuge"] = db["werkzeuge"][:1]
    werkzeugdb.schreiben(db, str(link))
    assert os.path.islink(link)
    assert len(werkzeugdb.laden(str(ziel))["werkzeuge"]) == 1


def test_alu_einschneider_wie_schild():
    """Peter, 22.10.: Ø 3, 1 Schneide, 19 000 U/min in Alu lief mit 1200 / Vollnut 800 mm/min optimal.
    Die Mittelwerte der Herstellertabellen (docs/SCHNITTDATEN.md) liegen dort: 1140 / Vollnut × 0,86."""
    w = {"typ": "schaft", "durchmesser": 3, "schneiden": 1}
    s = werkzeugdb.schnittdaten(w, "aluminium", drehzahl=19000)
    assert 1000 <= s["vorschub"] <= 1300
    assert 900 <= s["vorschub"] * s["vollnut"] <= 1050
    assert s["zustellung"] == 1.5                         # 0,5 × Ø (Mittel der Tabellen)


def test_einschneider_und_fraeserarten():
    """Einschneider haben laut Herstellern einen grösseren Zahnvorschub als Zweischneider; V-Fräser,
    Kugel-, Gewindefräser mit eigenem Faktor; Planfräser: fz wächst nicht mit dem Ø."""
    e = werkzeugdb.schnittdaten({"typ": "schaft", "durchmesser": 6, "schneiden": 1}, "hartholz")
    z = werkzeugdb.schnittdaten({"typ": "schaft", "durchmesser": 6, "schneiden": 2}, "hartholz")
    d = werkzeugdb.schnittdaten({"typ": "schaft", "durchmesser": 6, "schneiden": 3}, "hartholz")
    assert e["fz"] > 2 * z["fz"] and abs(d["fz"] - 0.8 * z["fz"]) < 1e-3
    for typ, f in (("kugel", 0.9), ("gewinde", 0.45), ("eckradius", 1.0)):
        x = werkzeugdb.schnittdaten({"typ": typ, "durchmesser": 6, "schneiden": 2}, "hartholz")
        assert abs(x["fz"] - f * z["fz"]) < 1e-3, typ
    # V-Fräser (02.11.): Zahnvorschub nach dem Ø, der bei der Zustellung schneidet, kein Faktor mehr
    v = werkzeugdb.schnittdaten({"typ": "v", "durchmesser": 6, "schneiden": 2, "v_winkel": 90}, "hartholz")
    assert v["d_wirksam"] < 6 and v["rechnung"] == "fraeser"
    assert z["eintauchen"] == round(z["vorschub"] * 0.5 / 10) * 10      # 2 Schneiden: 50 %


def test_gravierstichel_eigene_werte():
    """Peter 02.11.: 15°-Stichel mit 0,2 mm Spitze in Alu – vorher 660 / 500, richtig etwa 280 / 50."""
    st = {"typ": "v", "durchmesser": 3.175, "v_winkel": 15, "spitze": 0.2, "schneiden": 1}
    a = werkzeugdb.schnittdaten(st, "aluminium", {"drehzahl_max": 19000})
    assert a["rechnung"] == "stichel" and a["drehzahl"] == 19000
    assert 200 <= a["vorschub"] <= 300 and 40 <= a["eintauchen"] <= 60 and a["zustellung"] <= 0.15
    h = werkzeugdb.schnittdaten(st, "hartholz", {"drehzahl_max": 19000})
    assert h["vorschub"] > 4 * a["vorschub"]
    # V-Fräser ohne Spitze bleibt bei der normalen Rechnung
    v = werkzeugdb.schnittdaten(dict(st, spitze=0), "aluminium", {"drehzahl_max": 19000})
    assert v["rechnung"] == "fraeser"


def test_planfraeser_herstellerwerte():
    """Planfräser Ø 22 (8 mm Schaft), 2 Schneiden, Hartholz: etwa 1900–2200 mm/min, Zustellung höchstens 2 mm;
    der Schaft-Ø spielt keine Rolle."""
    p = werkzeugdb.schnittdaten({"typ": "schaft", "durchmesser": 22, "schneiden": 2, "wendeplatten": True,
                                 "schaft": 8}, "hartholz", {"drehzahl_max": 19000, "drehzahl_min": 6000})
    assert p["rechnung"] == "plan" and 1800 <= p["vorschub"] <= 2300 and p["zustellung"] <= 2
    al = werkzeugdb.schnittdaten({"typ": "schaft", "durchmesser": 22, "schneiden": 2, "wendeplatten": True},
                                 "aluminium", {"drehzahl_max": 19000, "drehzahl_min": 6000})
    assert al["zustellung"] <= 0.5 and al["vorschub"] < p["vorschub"]
