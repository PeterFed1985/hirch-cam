"""«So fräse ich» (Peter 07.11.): Vorgaben für neue Bearbeitungen – je Wert eine Stelle, nur für neue."""
import json

import pytest

from hirch_cam import einstellungen as einst
from hirch_cam import maschinen, vorgaben, werkzeugdb


@pytest.fixture
def db_datei(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "konfig"))
    monkeypatch.setenv("APPDATA", str(tmp_path / "konfig"))
    return None


def test_standard_ist_der_wert_der_bearbeitung():
    vorgaben._pruefen_standard()


def test_ohne_vorgaben_bleibt_alles_wie_bisher(db_datei):
    e = einst.laden(None)
    einst.ergaenzen(e, {"tasche", "kontur"})
    t = next(b for b in e["bearbeitungen"] if b["art"] == "tasche")
    assert t["bahnabstand"] == 40 and t["strategie"] == "adaptiv" and t["gleichlauf"] is True


def test_eine_vorgabe_gilt_fuer_alle_fraesarten(db_datei):
    vorgaben.setzen("gleichlauf", False)
    e = einst.laden(None)
    einst.ergaenzen(e, {"tasche", "kontur", "bohren"})
    assert all(b["gleichlauf"] is False for b in e["bearbeitungen"])


def test_grenzen(db_datei):
    w = vorgaben.setzen("taschen_bahnabstand", 90)
    assert w["taschen_bahnabstand"] == 60                       # fraesbahnen: 5–60 %
    w = vorgaben.setzen("schlicht3d_bahnabstand", 1)
    assert w["schlicht3d_bahnabstand"] == 3                     # 3d: 3–40 %
    with pytest.raises(ValueError):
        vorgaben.setzen("taschen_strategie", "spirale")
    with pytest.raises(ValueError):
        vorgaben.setzen("tiefe", 3)                             # Tiefe gehört zur Zeichnung: keine Vorgabe


def test_gleich_standard_wird_nicht_gespeichert(db_datei):
    vorgaben.setzen("taschen_bahnabstand", 30)
    assert maschinen.index().get("vorgaben") == {"taschen_bahnabstand": 30}
    vorgaben.setzen("taschen_bahnabstand", 40)
    assert maschinen.index().get("vorgaben") == {}


def test_gespeicherte_bearbeitungen_bleiben(db_datei):
    """Vorgaben wirken nie beim Laden einer Zeichnung – auch nicht auf Felder, die dort fehlen."""
    vorgaben.setzen("taschen_bahnabstand", 25)
    vorgaben.setzen("schlichten", True)
    roh = {"bearbeitungen": [{"id": "t1", "art": "tasche", "farbe": "#00a000", "tiefe": 3.0}]}
    e = einst.laden(json.dumps(roh))
    t = e["bearbeitungen"][0]
    assert t["bahnabstand"] == 40 and t["schlichten"] is False


def test_3d_und_zusammen_gehoerende_felder(db_datei):
    """3D-Bearbeitungen legt die Oberfläche an (ohne gefärbte Linien) – sie nimmt dafür vorgaben.anwenden."""
    vorgaben.setzen("schrupp3d_bahnabstand", 50)
    vorgaben.setzen("schlicht3d_strategie", "konturen")
    nach_art = {a: vorgaben.anwenden(dict(einst.STANDARD_BEARBEITUNG[a]), a)
                for a in ("relief_schruppen", "boeden_waende", "relief_schlichten")}
    assert nach_art["relief_schruppen"]["bahnabstand"] == 50
    assert nach_art["boeden_waende"]["bahnabstand"] == 50
    assert nach_art["relief_schlichten"]["strategie"] == "konturen"
    assert nach_art["relief_schlichten"]["bahnabstand"] == 10


def test_befehl_fuer_die_oberflaeche(db_datei, capsys):
    werkzeugdb.main(["vorgabe", json.dumps({"key": "stege_anzahl", "wert": 6})])
    aus = json.loads(capsys.readouterr().out)
    wert = {v["key"]: v["wert"] for v in aus["vorgaben"]["liste"]}
    assert wert["stege_anzahl"] == 6
    werkzeugdb.main(["vorgabe", json.dumps({"alle_zurueck": True})])
    aus = json.loads(capsys.readouterr().out)
    assert {v["key"]: v["wert"] for v in aus["vorgaben"]["liste"]}["stege_anzahl"] == 4


def test_als_vorgabe_merken_mehrere(db_datei, capsys):
    werkzeugdb.main(["vorgabe", json.dumps({"werte": {"taschen_bahnabstand": 30, "gleichlauf": False,
                                                      "tiefe": 5, "taschen_strategie": "spirale"}})])
    aus = json.loads(capsys.readouterr().out)
    wert = {v["key"]: v["wert"] for v in aus["vorgaben"]["liste"]}
    assert wert["taschen_bahnabstand"] == 30 and wert["gleichlauf"] is False
    assert wert["taschen_strategie"] == "adaptiv"                # ungültig: übersprungen
