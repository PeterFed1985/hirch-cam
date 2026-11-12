"""Zufallstests der 3D-Sicherheit (planung 06.11., Peter 07.11. 11:47): ein kleiner Lauf in der vollen Reihe und die
Funde als feste Fälle. Erzeuger und grosser Lauf: tests/zufall/modell_3d.py.

Funde vom 07.11. (Bericht berichte/3d-zufallstests-07-11):
- Fall 2/5: «Böden und Wände» zweiseitig brach die ganze Rechnung ab. inkex stellt numpy auf «invalid → Ausnahme»,
  und GEOS meldet beim runden Versetzen eines treppigen Umrisses ein «invalid value», obwohl das Ergebnis stimmt.
- Fall 16: GEOS bricht beim Versetzen einer gültigen Mehrfachfläche ab («unable to assign hole to a shell») –
  jetzt Teil für Teil (relief3d._puffer).
- Fall 37/43: «Haut zu dünn» war Messfehler an der messerscharfen Kante des Modells (Prüfung erst ab 1 mm).
- Fall 38: Eilgang des 3D-Schlichtens 1,25 mm durch Material, das das Schruppen stehen liess; Endprüfung schweigt.
  Behoben von fraesbahnen (22f2043, Raster des Sicherheitsnetzes).
- Eilgänge, die die Rechnung leicht ins Material legt (zweiseitig, «Böden und Wände», feine Fräser): die Endprüfung
  meldet sie jedes Mal. Der Test hier hält fest, dass sie nie schweigt."""
import os
import sys

import numpy as np
import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
pytest.importorskip("hirch_cam.hirch_relief", reason="hirch_relief nicht gebaut")
pytest.importorskip("inkex")
import modell_3d as Z  # noqa: E402

TREPPE = ("POLYGON ((17 16, 16 16, 16 18, 15 18, 15 20, 14 20, 14 30, 15 30, 15 32, 16 32, 16 34, 17 34, 17 35, 18 35, "
          "18 37, 20 37, 20 38, 21 38, 21 39, 23 39, 23 40, 25 40, 25 41, 35 41, 35 40, 37 40, 37 39, 39 39, 39 38, "
          "40 38, 40 37, 42 37, 42 35, 43 35, 43 34, 44 34, 44 32, 45 32, 45 30, 46 30, 46 20, 45 20, 45 18, 44 18, "
          "44 16, 43 16, 43 15, 42 15, 42 13, 40 13, 40 12, 39 12, 39 11, 37 11, 37 10, 35 10, 35 9, 25 9, 25 10, "
          "23 10, 23 11, 21 11, 21 12, 20 12, 20 13, 18 13, 18 15, 17 15, 17 16))")


def test_treppe_versetzen_bricht_nicht_ab():
    from shapely import wkt
    from hirch_cam.ops.relief3d import _bereich, ohne_nan_abbruch
    u = wkt.loads(TREPPE)
    with np.errstate(invalid="raise"):
        b = ohne_nan_abbruch(_bereich)(u, 12.0, (60.0, 50.0))
    assert b.is_valid and b.area > u.area


def _pruefen_ohne_schweigen(nr, ordner):
    e = Z.pruefen(nr, ordner)
    assert e["ok"], e["funde"]
    eil = [f for f in e["funde"] if f.startswith("Eilgang")]
    rest = [f for f in e["funde"] if not f.startswith("Eilgang")]
    assert rest == [], (nr, rest, e["besch"])
    if eil:                                    # gefunden – dann muss die Endprüfung es auch melden
        assert any("Eilgang" in t for t in e.get("endpruefung") or []), (nr, eil, e["besch"])


@pytest.mark.parametrize("nr", [2, 5, 16, 37, 38, 43])
def test_funde_vom_07_11(nr, tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "konfig"))
    _pruefen_ohne_schweigen(nr, str(tmp_path))


def test_kleiner_lauf(tmp_path, monkeypatch):
    """Sechs Fälle, etwa eine Minute."""
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "konfig"))
    for nr in range(100, 106):
        _pruefen_ohne_schweigen(nr, str(tmp_path / str(nr)))
