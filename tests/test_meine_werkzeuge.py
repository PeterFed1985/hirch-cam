"""«Meine Werkzeuge» als ein Katalog für alle Zeichnungen: Abgleich, Übernehmen, Export/Import."""
import json
import os

from hirch_cam import einstellungen as einst
from hirch_cam import werkzeugdb as wdb


def _db(tmp_path, werkzeuge, version=wdb.DB_VERSION, **mehr):
    p = tmp_path / "werkzeuge.json"
    p.write_text(json.dumps(dict({"version": version, "werkzeuge": werkzeuge, "maschine": {}}, **mehr)))
    return str(p)


def test_aenderung_kommt_beim_oeffnen_mit_hinweis(tmp_path):
    datei = _db(tmp_path, [wdb._w("s6", "Schaftfräser 6 mm", "schaft", 6, sl=25, schaft=6)])
    e = einst.laden(json.dumps({"werkzeuge": {"C": {"typ": "schaft", "durchmesser": 6.0, "schneidenlaenge": 22.0,
                                                    "db_id": "s6", "nummer": 3, "auto": True,
                                                    "name": "Schaftfräser 6 mm"}}}))
    neu, hinweise = wdb.abgleichen(e["werkzeuge"], wdb.laden(datei))
    assert neu["C"]["schneidenlaenge"] == 25.0 and neu["C"]["nummer"] == 3 and neu["C"]["db_id"] == "s6"
    assert len(hinweise) == 1 and "Schneidenlänge 22 mm → 25 mm" in hinweise[0]
    # zweites Öffnen: nichts mehr zu melden
    assert wdb.abgleichen(neu, wdb.laden(datei))[1] == []


def test_geloeschter_fraeser_bleibt_in_der_zeichnung(tmp_path):
    datei = _db(tmp_path, [wdb._w("s3", "Schaftfräser 3 mm", "schaft", 3, sl=12, schaft=3)])
    alt = {"T": {"typ": "kugel", "durchmesser": 2.0, "db_id": "w9", "name": "Kugelfräser 2 mm (alt)"}}
    neu, hinweise = wdb.abgleichen(alt, wdb.laden(datei))
    assert neu["T"]["nur_hier"] is True and neu["T"]["durchmesser"] == 2.0 and hinweise == []


def test_gleicher_fraeser_wird_verknuepft_und_vorgaben_kommen_aus_dem_katalog(tmp_path):
    datei = _db(tmp_path, [wdb._w("v60", "V-Fräser 60°", "v", 12, sl=11, schaft=6, v_winkel=60.0),
                           wdb._w("s4", "Schaftfräser 4 mm", "schaft", 4, sl=15, schaft=6)])
    e = einst.laden(None)                   # neue Zeichnung: Vorgaben A, B, C
    neu, hinweise = wdb.abgleichen(e["werkzeuge"], wdb.laden(datei))
    assert neu["A"]["db_id"] == "v60" and not neu["A"].get("nur_hier")
    # B (3 mm) und C (6 mm) gibt es nicht mehr: der nächste Schaftfräser
    assert neu["B"]["db_id"] == "s4" and neu["C"]["db_id"] == "s4" and hinweise == []
    # eigener, geänderter Fräser einer alten Zeichnung: nur in dieser Zeichnung
    eigen = {"A": dict(e["werkzeuge"]["A"], v_winkel=45)}
    assert wdb.abgleichen(eigen, wdb.laden(datei))[0]["A"]["nur_hier"] is True


def test_doppelte_werden_zusammengelegt_und_zeichnungen_folgen(tmp_path):
    v = wdb._w("v60", "V-Fräser 60°", "v", 12, sl=11, schaft=6, v_winkel=60.0)
    datei = _db(tmp_path, [v, dict(v, id="w1"), dict(v, id="w2"),
                           wdb._w("s3", "Schaftfräser 3 mm", "schaft", 3)], version=1)
    db = wdb.laden(datei)
    assert [w["id"] for w in db["werkzeuge"] if w["name"] == "V-Fräser 60°"] == ["v60"]
    assert db["ersetzt"] == {"w1": "v60", "w2": "v60"}
    wdb.schreiben(db, datei)
    neu, _ = wdb.abgleichen({"A": {"typ": "v", "durchmesser": 12.0, "v_winkel": 60, "db_id": "w2"}}, wdb.laden(datei))
    assert neu["A"]["db_id"] == "v60" and not neu["A"].get("nur_hier")
    assert wdb.neue_id(wdb.laden(datei)) == "w3"          # alte IDs werden nicht neu vergeben


def test_speichern_behaelt_schnittdaten_nach_modus(tmp_path):
    datei = _db(tmp_path, [])
    h = wdb.speichern({"name": "Hand", "typ": "schaft", "durchmesser": 4, "schnittdaten": "hand",
                       "drehzahl": 15000, "vorschub": 900, "eintauchen": 200, "nummer": 4, "db_id": "x"}, datei)
    assert (h["id"], h["drehzahl"], h["vorschub"], h["eintauchen"]) == ("x", 15000, 900, 200)
    assert "nummer" not in h and "db_id" not in h
    a = wdb.speichern({"name": "Auto", "typ": "schaft", "durchmesser": 4, "auto": True,
                       "drehzahl": 15000, "vorschub": 900}, datei)
    assert a["schnittdaten"] == "auto" and "drehzahl" not in a and "vorschub" not in a


def test_uebernehmen_speichert_und_loescht_in_einem_schritt(tmp_path):
    datei = _db(tmp_path, [wdb._w("s3", "S3", "schaft", 3), wdb._w("s6", "S6", "schaft", 6)])
    r = wdb.uebernehmen({"speichern": [dict(wdb._w("s3", "S3 neu", "schaft", 3)), {"id": "w5", "name": "Neu",
                                                                                   "typ": "kugel", "durchmesser": 2}],
                         "loeschen": ["s6"]}, datei)
    assert [w["id"] for w in r["gespeichert"]] == ["s3", "w5"]
    db = wdb.laden(datei)
    assert [w["id"] for w in db["werkzeuge"]] == ["s3", "w5"] and db["werkzeuge"][0]["name"] == "S3 neu"


def test_export_und_import(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "konfig"))
    os.makedirs(wdb._post_ordner())
    with open(os.path.join(wdb._post_ordner(), "meine_a11.json"), "w") as fh:
        json.dump({"name": "Meine A11"}, fh)
    hier = [wdb._w("s6", "Schaftfräser 6 mm", "schaft", 6, sl=22), wdb._w("w1", "Mein Kugel", "kugel", 2)]
    db = {"version": wdb.DB_VERSION, "werkzeuge": hier, "maschine": dict(wdb.STANDARD_MASCHINE, x=800.0)}
    wdb.schreiben(db)
    ziel = str(tmp_path / "hirch-werkzeuge.json")
    r = wdb.exportieren(ziel)
    assert r["werkzeuge"] == 2 and r["postprozessoren"] == 1

    # anderer Rechner: s6 mit anderer Schneide, w1 ist dort ein anderer Fräser, Maschine anders
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "andere"))
    wdb.schreiben({"version": wdb.DB_VERSION, "werkzeuge": [wdb._w("s6", "Schaftfräser 6 mm", "schaft", 6, sl=25),
                                                           wdb._w("w1", "V 45", "v", 6, v_winkel=45.0)],
                   "maschine": dict(wdb.STANDARD_MASCHINE)})
    v = wdb.import_vorschau(ziel)
    assert v["neu"] == ["Mein Kugel"] and v["gleich"] == 0 and v["maschine"] is True
    assert [k["id"] for k in v["konflikte"]] == ["s6"] and "22" in v["konflikte"][0]["datei"]
    assert v["postprozessoren"][0]["neu"] is True
    r = wdb.importieren(ziel, {"s6": "datei"}, maschine=True, post=True)
    assert r == {"neu": 1, "ersetzt": 1, "postprozessoren": 1}
    db2 = wdb.laden()
    namen = {w["id"]: w["name"] for w in db2["werkzeuge"]}
    assert namen["w1"] == "V 45" and "Mein Kugel" in namen.values()     # nichts überschrieben, neue ID
    assert next(w for w in db2["werkzeuge"] if w["id"] == "s6")["schneidenlaenge"] == 22.0
    assert db2["maschine"]["x"] == 800.0
    assert os.path.exists(os.path.join(str(tmp_path / "andere"), "hirch-cam", "werkzeuge-vorher.json"))
    assert os.path.exists(os.path.join(wdb._post_ordner(), "meine_a11.json"))
    # zweiter Import: alles gleich
    assert wdb.import_vorschau(ziel)["gleich"] == 2


def test_schnittdaten_je_werkstoff(tmp_path):
    """Eigene Schnittdaten gelten nur für ihren Werkstoff; andere bleiben bei den allgemeinen Angaben."""
    datei = _db(tmp_path, [])
    w = wdb.speichern({"id": "s3", "name": "S3", "typ": "schaft", "durchmesser": 3, "schneiden": 2,
                       "schnittdaten": "auto", "drehzahl": 1, "vorschub": 2,
                       "je_werkstoff": {"hartholz": {"schnittdaten": "drehzahl", "drehzahl": 16000, "vorschub": 9},
                                        "acryl": {"schnittdaten": "hand", "drehzahl": 12000, "vorschub": 700,
                                                  "eintauchen": 150}}}, datei)
    assert w["je_werkstoff"]["hartholz"] == {"schnittdaten": "drehzahl", "drehzahl": 16000}   # Vorschub gerechnet
    assert "drehzahl" not in w                                                                  # allgemein: auto
    holz = wdb.wirksam({"B": w}, "hartholz")["B"]
    acryl = wdb.wirksam({"B": w}, "acryl")["B"]
    weich = wdb.wirksam({"B": w}, "weichholz")["B"]
    assert holz["schnittdaten"] == "drehzahl" and holz["drehzahl"] == 16000
    assert holz["vorschub"] == wdb.schnittdaten(w, "hartholz", drehzahl=16000)["vorschub"]
    assert (acryl["schnittdaten"], acryl["drehzahl"], acryl["vorschub"], acryl["eintauchen"]) == ("hand", 12000, 700, 150)
    assert weich["schnittdaten"] == "auto" and weich["drehzahl"] == wdb.schnittdaten(w, "weichholz")["drehzahl"]
    # Zeichnung mit dem Fräser: Änderung für einen Werkstoff wird beim Öffnen gemeldet
    alt = {"B": dict({k: v for k, v in w.items() if k != "id"}, db_id="s3", nummer=1)}
    w2 = dict(w)
    w2["je_werkstoff"] = {"hartholz": w["je_werkstoff"]["hartholz"]}
    wdb.speichern(w2, datei)
    neu, hinweise = wdb.abgleichen(alt, wdb.laden(datei))
    assert "acryl" not in neu["B"]["je_werkstoff"]
    assert any("Schnittdaten für Acryl (PMMA) zurückgesetzt" in h for h in hinweise), hinweise
