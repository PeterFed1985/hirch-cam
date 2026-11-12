"""Zufallstests der 2D-Sicherheit (planung 06.11., Peter 17:43): ein kleiner Lauf in der vollen Reihe (150 Fälle,
etwa 1 min) und die verkleinerten Funde als feste Fälle. Grosser Lauf nur auf Wunsch:

    python3 tests/zufall/zeichnung.py 0 2000

Funde vom 06.11. (Bericht berichte/fraesbahnen-zufallstests-06-11):
- Fall 4/20: Endprüfung meldete «tiefer als eingestellt», wenn plangefräst wird (Plantiefe nicht mitgezählt).
- Fall 21/28/172: Endprüfung meldete Eilgänge «unterhalb der Oberfläche», wo der Fräser längst geräumt hatte
  (grobes Raster der Fräsermitte) – jetzt mit der Form des Fräsers nachgeprüft.
- Fall 6/9/16: Fräser ohne Mittenschnitt taucht beim Bohren senkrecht ein – jetzt meldet die Endprüfung es als
  Fehler (Peter: «Prüfung + Hinweis»).
- Fall 1848: das Kernloch eines Gewindes kam nach dem Gewinde (sein Werkzeug fräste auch eine Kontur) – jetzt immer
  davor; die Prüfung «kein Kernloch» schaut nur noch auf die Gewinde des eigenen Programms. Fall 720/1029: dasselbe
  in Spannung 2 (Umspannen).

Funde vom 07.11. (Fälle 3000–5199, Bericht berichte/fraesbahnen-endpruefung-luecken-07-11):
- Fall 3808/4096: Umspannen mit Planen – ein Loch knapp ausserhalb der Sperrzone tauchte mit dem Fräserrand im
  Eilgang in einen Streifen, den das Planen erst in Spannung 2 fräst. Jetzt kommt es in Spannung 2.
- Fall 3621/3967/4689: Scheinfunde des Feinprüfers (Raster an der Zugabe, senkrecht hinauf) – der Prüfer ist genauer.
- Fall 6861: eine Form, die sich selbst kreuzt, brachte die Prüfung im Panel zum Absturz (pruefung._teile_zu_nah).
- Fall 14861 (10.11.): Bohren ohne Mittenschnitt in Spannung 2 – die Meldung kommt aus der Prüfung im Panel und
  wandert beim Speichern in die Endprüfung; der Erzeuger zählt sie jetzt mit (kein Fehler im Programm)."""
import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zeichnung as Z  # noqa: E402

pytest.importorskip("hirch_cam.hirch_relief", reason="hirch_relief nicht gebaut")


@pytest.mark.parametrize("nr", [4, 6, 9, 16, 20, 21, 28, 172, 720, 1029, 1848, 3621, 3808, 3967, 4096,
                                4689, 6861, 14861])
def test_funde_vom_06_11(nr, tmp_path):
    e = Z.pruefen(nr, str(tmp_path))
    assert e["ok"] and e["funde"] == []


def test_mittenschnitt_wird_gemeldet(tmp_path):
    from hirch_cam import dokument, engine
    from hirch_cam.engine import OHNE_MITTE
    a = Z.auftrag(6, str(tmp_path))
    erg = engine.rechnen(a)
    assert erg["ohne_mittenschnitt"]
    fr = dokument.endpruefung(a, erg)
    assert fr["stufe"] == "fehler"
    assert any(p["text"] == OHNE_MITTE.format("Schaftfräser Ø 6 mm", len(erg["ohne_mittenschnitt"]))
               for p in fr["punkte"]), fr["punkte"]
    # mit Mittenschnitt: keine Meldung
    a = Z.auftrag(6, str(tmp_path))
    a["werkzeuge"]["C"]["mittenschnitt"] = True
    assert "ohne_mittenschnitt" not in engine.rechnen(a)


def test_kleiner_lauf(tmp_path):
    funde = {}
    for nr in range(150):
        e = Z.pruefen(nr, str(tmp_path / str(nr)))
        if e["funde"]:
            funde[nr] = e["funde"]
    assert funde == {}


def test_kernloch_vor_dem_gewinde(tmp_path):
    from hirch_cam import engine
    a = Z.auftrag(1848, str(tmp_path))
    a["reihenfolge"] = "auto"
    erg = engine.rechnen(a)
    folge = [s["key"] for p in erg["programme"] for s in p["abschnitte"]]
    for g in [b["id"] for b in a["bearbeitungen"] if b["art"] == "gewinde"]:
        assert folge.index(g + "#kernloch") < folge.index(g), folge


def test_nur_beim_speichern(tmp_path):
    """Peter 06.11.: «kein Kernloch» und «ohne Mittenschnitt» nicht im Panel, nur in der Endprüfung beim Speichern."""
    from hirch_cam import dokument, engine, pruefung
    a = Z.auftrag(6, str(tmp_path))                      # Bohren mit Fräser ohne Mittenschnitt
    a["bearbeitungen"].append({"id": "gx", "art": "gewinde", "werkzeug": "G", "tiefe": 6, "innen": True,
                               "name": "M8", "objekte": [{"punkte": Z._kreis(30, 30, 4), "geschlossen": True}]})
    erg = engine.rechnen(a)
    erg["pruefung"] = pruefung.pruefen(a, erg)
    assert any(x.get("nur_speichern") for x in erg["pruefung"])
    erg["freigabe"] = dokument.endpruefung(a, erg)
    dokument.nur_beim_speichern(erg)
    assert not any(x.get("nur_speichern") for x in erg["pruefung"])
    texte = [p["text"] for p in erg["freigabe"]["punkte"]]
    assert any("kein Kernloch im Programm" in t for t in texte)
    assert sum("Mittenschnitt" in t for t in texte) == 1           # einmal, nicht doppelt
    assert erg["freigabe"]["stufe"] == "fehler"
