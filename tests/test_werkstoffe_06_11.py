"""Werkstoffe an der Maschine (Peter 06.11.: Schieber «langsamer – schneller», Rechnung in Klartext)."""
import pytest

from hirch_cam import werkzeugdb as W


@pytest.fixture(autouse=True)
def eigener_ordner(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    monkeypatch.setenv("APPDATA", str(tmp_path / "config"))


FR = {"typ": "schaft", "durchmesser": 6.0, "schneiden": 2, "schneidenlaenge": 22.0}


def test_prozent_wirkt_auf_vorschub_und_steht_in_der_rechnung():
    ma = dict(W.STANDARD_MASCHINE, drehzahl_max=19000)
    voll = W.schnittdaten(FR, "hartholz", ma)
    ma["werkstoffe"] = {"hartholz": {"vorschub": 85, "drehzahl": 100}}
    weniger = W.schnittdaten(FR, "hartholz", ma)
    assert weniger["drehzahl"] == voll["drehzahl"]
    assert abs(weniger["vorschub"] - voll["vorschub"] * 0.85) <= 10
    assert "× 85 %" in weniger["rechnung_text"] and "× 85 %" not in voll["rechnung_text"]
    assert weniger["prozent_vorschub"] == 85
    # andere Werkstoffe bleiben
    assert W.schnittdaten(FR, "mdf", ma)["vorschub"] == W.schnittdaten(FR, "mdf", dict(ma, werkstoffe={}))["vorschub"]


def test_drehzahl_prozent_nur_wenn_gerechnet_und_in_den_grenzen():
    ma = dict(W.STANDARD_MASCHINE, drehzahl_max=24000, drehzahl_min=6000,
              werkstoffe={"hartholz": {"vorschub": 100, "drehzahl": 50}})
    sd = W.schnittdaten(FR, "hartholz", ma)
    assert sd["drehzahl"] >= 6000
    von_hand = W.schnittdaten(FR, "hartholz", ma, drehzahl=18000)
    assert von_hand["drehzahl"] == 18000


def test_setzen_und_vorschau():
    W.werkstoff_setzen("hartholz", 85, 100)
    ws = {w["key"]: w for w in W.werkstoffe_vorschau()}
    assert ws["hartholz"]["vorschub"] == 85 and "85 %" in ws["hartholz"]["rechnung"]
    assert ws["mdf"]["vorschub"] == 100
    W.werkstoff_setzen("hartholz", 100, 100)            # zurück auf den Herstellerwert: Eintrag weg
    assert "hartholz" not in W.laden()["maschine"]["werkstoffe"]
    W.werkstoff_setzen("hartholz", 10, 999)             # Grenzen 50 … 150 %
    assert W.laden()["maschine"]["werkstoffe"]["hartholz"] == {"vorschub": 50, "drehzahl": 150}
    with pytest.raises(ValueError):
        W.werkstoff_setzen("gold", 90, 100)
    # «Übernehmen» in Maschine lässt die Werkstoffe stehen
    W.maschine_setzen({"x": 700})
    assert W.laden()["maschine"]["werkstoffe"]["hartholz"]["vorschub"] == 50


# ---- Details (Konzept 06.11., gebaut 07./08.11.): eigene Werkstoffe und Fachwerte, gelten für alle Maschinen

def test_eigener_werkstoff_und_fachwerte(tmp_path, monkeypatch):
    import json as _json
    from hirch_cam import werkzeugdb as wdb
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))
    monkeypatch.setenv("APPDATA", str(tmp_path))
    monkeypatch.delenv("HIRCH_MASCHINE", raising=False)
    key = wdb.werkstoff_eigen_neu("hartholz", "Arve")
    alle = wdb.materialien()
    assert alle[key]["name"] == "Arve" and alle[key]["vc"] == wdb.MATERIALIEN["hartholz"]["vc"]
    probe = {"typ": "schaft", "durchmesser": 6.0, "schneiden": 2}
    n_vorher = wdb.schnittdaten(probe, key)["drehzahl"]
    wdb.werkstoff_fachwerte(key, {"vc": 300, "unbekannt": 5})
    assert wdb.materialien()[key]["vc"] == 300
    assert wdb.schnittdaten(probe, key)["drehzahl"] < n_vorher
    # mitgelieferter Werkstoff: Fachwert ändern und zurück
    wdb.werkstoff_fachwerte("mdf", {"bahnabstand": 30})
    assert wdb.materialien()["mdf"]["bahnabstand"] == 30 and wdb.materialien()["mdf"]["geaendert"]
    wdb.werkstoff_herstellerwerte("mdf")
    assert wdb.materialien()["mdf"]["bahnabstand"] == wdb.MATERIALIEN["mdf"]["bahnabstand"]
    # Grenzen
    wdb.werkstoff_fachwerte(key, {"vc": 99999})
    assert wdb.materialien()[key]["vc"] == wdb.FACHWERTE["vc"][1]
    # Schieber je Maschine geht auch für eigene
    wdb.werkstoff_setzen(key, 80, 100)
    assert wdb.werkstoff_prozent(wdb.laden()["maschine"], key) == (0.8, 1.0)
    # Befehl, Löschen (nur eigene)
    wdb.main(["werkstoff_details", _json.dumps({"was": "loeschen", "key": "hartholz"})])
    wdb.main(["werkstoff_details", _json.dumps({"was": "loeschen", "key": key})])
    alle = wdb.materialien()
    assert "hartholz" in alle and key not in alle
