"""Werkzeug-Dialog, «Drehzahl von Hand – Vorschub passend gerechnet» (Sammelauftrag 21.10., Punkt 1).

Der Dialog rechnet den Vorschub zur Drehzahl selbst (hirch-dialoge.cpp, von_drehzahl) und holt nach
«Übernehmen» die Schnittdaten neu («datenbank wirksam»). Beides muss genau das ergeben, was beim
Erzeugen des G-Codes gilt (werkzeugdb.wirksam): Vorschau, gespeichertes Werkzeug und G-Code zeigen
dieselbe Drehzahl und denselben Vorschub.
"""
import io
import json
import math
import re
from contextlib import redirect_stdout

import pytest

from hirch_cam import engine, werkzeugdb


def _zehner(v):
    """wie im Dialog: std::max(10, std::nearbyint(v / 10) * 10)"""
    return max(10.0, round(v / 10) * 10)


def _wie_dialog(empfehlung, n, z):
    """Nachbau von von_drehzahl() im Werkzeug-Dialog"""
    fz = empfehlung.get("fz_genau", empfehlung["fz"])
    f = min(n * max(1, z) * fz, empfehlung["vorschub_max"])
    return _zehner(f), _zehner(f * empfehlung["eintauch_anteil"])


WERKZEUGE = [
    {"typ": "schaft", "durchmesser": 3, "schneiden": 2, "schneidenlaenge": 12},
    {"typ": "schaft", "durchmesser": 6, "schneiden": 2, "schneidenlaenge": 22},
    {"typ": "schaft", "durchmesser": 3, "schneiden": 1, "schneidenlaenge": 12},
    {"typ": "v", "durchmesser": 12, "v_winkel": 60, "schneiden": 2},
    {"typ": "kugel", "durchmesser": 4, "schneiden": 2, "schneidenlaenge": 10},
    {"typ": "bohrer", "durchmesser": 5, "schneiden": 2, "schneidenlaenge": 40},
]


@pytest.mark.parametrize("material", list(werkzeugdb.MATERIALIEN))
@pytest.mark.parametrize("n", [15000, 16000, 18000, 12345, 9000])
def test_dialog_rechnet_wie_der_g_code(material, n):
    for w in WERKZEUGE:
        w = dict(w, schnittdaten="drehzahl", drehzahl=n)
        erg = werkzeugdb.wirksam({"A": w}, material)["A"]
        vs, ei = _wie_dialog(erg["empfehlung"], n, w["schneiden"])
        assert erg["drehzahl"] == n
        assert (erg["vorschub"], erg["eintauchen"]) == (vs, ei), (w["typ"], w["durchmesser"])


def test_nach_uebernehmen_vorschau_gleich_gespeichert_und_g_code(tmp_path, monkeypatch):
    monkeypatch.setenv("HIRCH_KONFIG", str(tmp_path))
    # vorher automatisch (24 000 U/min), dann Drehzahl von Hand 15 000 übernommen
    w = {"typ": "schaft", "durchmesser": 3, "schneiden": 2, "schneidenlaenge": 12, "nummer": 1,
         "schnittdaten": "auto"}
    vorher = werkzeugdb.wirksam({"B": w}, "hartholz")["B"]
    w_neu = dict(w, schnittdaten="drehzahl", drehzahl=15000)
    # was der Dialog vor dem Übernehmen zeigt: Empfehlung der letzten Rechnung (noch mit den alten Werten)
    vs_dialog, ei_dialog = _wie_dialog(vorher["empfehlung"], 15000, 2)
    # «Übernehmen»: Dialog holt die Schnittdaten neu
    aus = io.StringIO()
    with redirect_stdout(aus):
        werkzeugdb.main(["wirksam", json.dumps({"werkzeuge": {"B": w_neu}, "material": "hartholz"})])
    r = json.loads(aus.getvalue())
    assert r["ok"]
    neu = r["werkzeuge_wirksam"]["B"]
    assert neu["drehzahl"] == 15000 != vorher["drehzahl"]
    assert (neu["vorschub"], neu["eintauchen"]) == (vs_dialog, ei_dialog)
    assert "datenbank_wirksam" in r and isinstance(r["werkzeuge"], list)
    # gespeichert in «Meine Werkzeuge» und wieder gelesen: dieselben Werte
    gesp = werkzeugdb.speichern(dict(w_neu, name="Test 3"), str(tmp_path / "w.json"))
    wieder = werkzeugdb.wirksam({"B": dict(gesp)}, "hartholz")["B"]
    assert (wieder["drehzahl"], wieder["vorschub"]) == (15000, vs_dialog)
    # G-Code: S und F wie in der Vorschau
    auftrag = {
        "material": "hartholz",
        "werkzeuge": {"B": neu},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 18},
        "tasche": {"tiefe": 2, "zustellung": 1, "werkzeug": "B"},
        "postprozessor": "linuxcnc_mm",
        "ausgabe": str(tmp_path / "teil.ngc"),
        "objekte": {"tasche": [{"punkte": [[0, 0], [30, 0], [30, 20], [0, 20]], "geschlossen": True}]},
    }
    erg = engine.rechnen(auftrag)
    assert erg["ok"]
    code = open(erg["datei"], encoding="utf-8").read()
    assert "S15000" in code
    f = {float(x) for x in re.findall(r"F(\d+(?:\.\d+)?)", code)}
    assert any(math.isclose(x, vs_dialog) for x in f), (vs_dialog, sorted(f))


def test_hoechstdrehzahl_gilt_fuer_alle_fraeser(tmp_path, monkeypatch):
    """Peter, 21.10.: Maschine auf 19 000 U/min begrenzt – gilt auch für Drehzahlen von Hand."""
    monkeypatch.setenv("HIRCH_KONFIG", str(tmp_path))
    ma = {"drehzahl_max": 19000, "drehzahl_min": 6000}
    basis = {"typ": "schaft", "durchmesser": 3, "schneiden": 2, "schneidenlaenge": 12, "nummer": 1,
             "drehzahl": 24000, "vorschub": 1300, "eintauchen": 390}
    auto = werkzeugdb.wirksam({"A": dict(basis, schnittdaten="auto")}, "hartholz", ma)["A"]
    assert auto["drehzahl"] <= 19000
    dreh = werkzeugdb.wirksam({"A": dict(basis, schnittdaten="drehzahl")}, "hartholz", ma)["A"]
    assert dreh["drehzahl"] == 19000 and dreh["drehzahl_eingetragen"] == 24000
    assert (dreh["vorschub"], dreh["eintauchen"]) == _wie_dialog(dreh["empfehlung"], 19000, 2)
    hand = werkzeugdb.wirksam({"A": dict(basis, schnittdaten="hand")}, "hartholz", ma)["A"]
    assert hand["drehzahl"] == 19000
    assert hand["vorschub"] == _zehner(1300 * 19000 / 24000)          # gleicher Span je Schneide
    assert hand["eintauchen"] == _zehner(390 * 19000 / 24000)
    # unter der Grenze bleibt alles, wie eingetragen
    ok = werkzeugdb.wirksam({"A": dict(basis, schnittdaten="hand", drehzahl=18000)}, "hartholz", ma)["A"]
    assert (ok["drehzahl"], ok["vorschub"]) == (18000, 1300) and "drehzahl_eingetragen" not in ok
    # G-Code: S19000 und ein Hinweis
    auftrag = {
        "material": "hartholz", "maschine": ma,
        "werkzeuge": {"A": hand},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 18},
        "tasche": {"tiefe": 2, "zustellung": 1, "werkzeug": "A"},
        "postprozessor": "linuxcnc_mm",
        "ausgabe": str(tmp_path / "teil.ngc"),
        "objekte": {"tasche": [{"punkte": [[0, 0], [30, 0], [30, 20], [0, 20]], "geschlossen": True}]},
    }
    erg = engine.rechnen(auftrag)
    code = open(erg["datei"], encoding="utf-8").read()
    assert "S19000" in code and "S24000" not in code
    assert any("eingetragen sind 24 000 U/min" in t for t in erg["warnungen"])
