"""Mehrere Teile mit zu kleinem Abstand: der Fräser der einen Kontur schneidet in das andere Teil (Peter, 22.10.:
«Die zweite Fräsung würde das erste Teil kaputt machen») → Fehler in der Prüfung."""
import pytest

pytest.importorskip("shapely")

from hirch_cam.pruefung import _teile_zu_nah  # noqa: E402


def _rechteck(x, y, w, h, eid):
    return {"geschlossen": True, "punkte": [[x, y], [x + w, y], [x + w, y + h], [x, y + h], [x, y]], "eid": eid}


def _pruefen(objekte, d=6.0, aufmass=0.0, wahl="auto"):
    return _teile_zu_nah([("k", "Kontur 1", 1, d, aufmass, wahl, objekte)])


def test_zu_nah_ist_ein_fehler_mit_beiden_teilen():
    # wie in Peters Bild: kleines Schild über dem langen, 4 mm Abstand bei Fräser Ø 6
    f = _pruefen([_rechteck(0, 40, 70, 30, "klein"), _rechteck(0, 0, 120, 36, "lang")])
    assert len(f) == 1 and f[0]["stufe"] == "fehler"
    assert "4,0 mm" in f[0]["text"] and "6,0 mm" in f[0]["text"]
    assert set(f[0]["objekte"]) == {"klein", "lang"}
    assert 30 <= f[0]["ort"][1] <= 36          # dort, wo das andere Teil angeschnitten wird


def test_genug_abstand_kein_befund():
    assert _pruefen([_rechteck(0, 43, 70, 30, "a"), _rechteck(0, 0, 120, 36, "b")]) == []


def test_aufmass_braucht_mehr_abstand():
    assert _pruefen([_rechteck(0, 42.5, 70, 30, "a"), _rechteck(0, 0, 120, 36, "b")]) == []
    assert _pruefen([_rechteck(0, 42.5, 70, 30, "a"), _rechteck(0, 0, 120, 36, "b")], aufmass=1.0)


def test_teil_im_loch_eines_anderen():
    aussen = _rechteck(0, 0, 100, 100, "rahmen")
    loch = _rechteck(20, 20, 60, 60, "loch")
    # Teil im Loch mit 10 mm Luft: gut; mit 3 mm Luft: der Fräser schneidet in den Rahmen
    assert _pruefen([aussen, loch, _rechteck(30, 30, 40, 40, "innen")]) == []
    f = _pruefen([aussen, loch, _rechteck(23, 30, 40, 40, "innen")])
    assert f and set(f[0]["objekte"]) == {"innen", "rahmen"}


def test_andere_seite_zaehlt_nicht():
    f = _teile_zu_nah([("k1", "Kontur 1", 1, 6.0, 0.0, "auto", [_rechteck(0, 40, 70, 30, "a")]),
                       ("k2", "Kontur 2", 2, 6.0, 0.0, "auto", [_rechteck(0, 0, 120, 36, "b")])])
    assert f == []
