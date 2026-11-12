"""Sicherheitsprüfung aller 2D-Bahnen (Peter 04.11. über planung: «Sicherheit vor Kürze»).

Über alle Referenzzeichnungen (beide Rechenkerne) und Peters Impeller als 2D-Teil (tests/daten/impeller_2d.hir):
der feine, unabhängige Prüfer (werkzeuge/eilgang_pruefen.py) findet keinen Eilgang durch stehendes Material und
kein senkrechtes Eintauchen eines Fräsers ohne Mittenschnitt ins Material."""
import glob
import os
import sys

import pytest

from test_dokument import braucht_inkex
from test_referenz import _mit_kern

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "werkzeuge"))
import eilgang_pruefen as ep  # noqa: E402

HIER = os.path.dirname(__file__)
ZEICHNUNGEN = sorted(glob.glob(os.path.join(HIER, "referenz", "zeichnungen", "*.svg")))
IMPELLER = os.path.join(HIER, "daten", "impeller_2d.hir")


def _pruefen(pfad, tmp_path):
    from hirch_cam import dokument, einstellungen as einst
    erg = dokument.rechnen(pfad, str(tmp_path / "aus"))
    assert erg["ok"], erg.get("warnungen")
    e = einst.laden(dokument._laden(pfad).get(einst.ATTRIBUT))
    return ep.pruefen(erg, werkzeuge=e.get("werkzeuge"))


@braucht_inkex
@pytest.mark.parametrize("kern", ["auto", "intern"])
@pytest.mark.parametrize("svg", ZEICHNUNGEN, ids=[os.path.basename(z)[:-4] for z in ZEICHNUNGEN])
def test_referenzen_sicher(svg, kern, tmp_path, monkeypatch):
    from hirch_cam import cam
    monkeypatch.setenv("HIRCH_SPRACHE", "de")
    if kern == "auto" and not cam.verfuegbar():
        pytest.skip("CAM-Modul fehlt")
    assert _pruefen(_mit_kern(svg, kern, tmp_path), tmp_path) == []


@braucht_inkex
def test_impeller_2d_sicher(tmp_path, monkeypatch):
    monkeypatch.setenv("HIRCH_SPRACHE", "de")
    assert _pruefen(IMPELLER, tmp_path) == []


def test_planen_immer_zuerst_und_kein_senkrechtes_eintauchen(tmp_path):
    """Gefunden am 04.11.: teilte das Planfräsen sein Werkzeug mit einer Kontur, kam es mit ihr zuletzt – die
    Gravur und die Tasche (Ø 3 ohne Mittenschnitt) davor tauchten senkrecht in die noch nicht geplante Schicht,
    die Gravur ohne Mittenschnitt ohnehin immer senkrecht. Jetzt: Planen zuerst, Gravur mit Rampe."""
    import test_umspannen_27_10 as T
    from hirch_cam import engine
    a = T._auftrag(tmp_path, "p", spannmittel=False)
    a["bearbeitungen"][0].update(tiefe=2, zustellung=1)
    erg = engine.rechnen(a)
    assert erg["ok"]
    assert [[s["key"] for s in p["abschnitte"]] for p in erg["programme"]] == [["pl"], ["g", "t"], ["b", "k"]]
    assert ep.pruefen(erg, werkzeuge=a["werkzeuge"]) == []
    # Reihenfolge «wie in der Liste»: auch dann zuerst
    a["reihenfolge"] = "liste"
    a["bearbeitungen"].append(a["bearbeitungen"].pop(0))
    a["ausgabe"] = str(tmp_path / "p" / "l.tap")
    erg = engine.rechnen(a)
    assert erg["programme"][0]["abschnitte"][0]["key"] == "pl"
    assert ep.pruefen(erg, werkzeuge=a["werkzeuge"]) == []


def test_sicherheitsnetz_aus_steht_im_ergebnis(tmp_path, monkeypatch):
    """Fehlt das Modul für das Sicherheitsnetz (Übergänge über dem stehenden Material), fiel es still weg – jetzt
    steht es im Ergebnis («sicherheitsnetz_aus»), damit die Oberfläche es zeigen kann."""
    import test_umspannen_27_10 as T
    from hirch_cam import engine
    hr = pytest.importorskip("hirch_cam.hirch_relief")
    erg = engine.rechnen(T._auftrag(tmp_path, "n1", spannmittel=False))
    assert "sicherheitsnetz_aus" not in erg
    monkeypatch.delattr(hr, "abtragen")
    erg = engine.rechnen(T._auftrag(tmp_path, "n2", spannmittel=False))
    assert erg["ok"] and erg["sicherheitsnetz_aus"] == "modul"
    monkeypatch.setenv("HIRCH_UEBER_BESTAND", "0")
    assert engine.rechnen(T._auftrag(tmp_path, "n3", spannmittel=False))["sicherheitsnetz_aus"] == "abgeschaltet"


@braucht_inkex
def test_muster_alle_sicher(tmp_path, monkeypatch):
    """Musterdatei mit jeder 2D- und 3D-Bearbeitungsart (tests/daten/muster_alle.hir, planung 04.11.; 3D-Teil von
    der 3D-Sitzung): rechnet ohne Fehler der Prüfung, jede Bearbeitung kommt vor, Umspannen teilt, der feine Prüfer
    findet nichts."""
    from hirch_cam import dokument, einstellungen as einst
    monkeypatch.setenv("HIRCH_SPRACHE", "de")
    pfad = os.path.join(HIER, "daten", "muster_alle.hir")
    erg = dokument.rechnen(pfad, str(tmp_path / "aus"))
    fehler = [x for x in erg.get("pruefung") or [] if x.get("stufe") == "fehler"]
    assert erg["ok"] and not fehler, fehler
    e = einst.laden(dokument._laden(pfad).get(einst.ATTRIBUT))
    assert {b["id"] for b in e["bearbeitungen"]} | {"pass"} <= set(erg["bearbeitungen"])
    assert {p["spannung"] for p in erg["programme"]} == {1, 2}
    assert ep.pruefen(erg, werkzeuge=e["werkzeuge"]) == []
