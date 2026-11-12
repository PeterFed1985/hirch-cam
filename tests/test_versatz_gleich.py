"""CAM-Modul: der Versatz liefert bei gleicher Eingabe immer dasselbe (libarea ordnete die Kurven nach
Speicheradressen – Taschen am Impeller änderten sich von Lauf zu Lauf, 24.10.2026)."""
import pytest

from hirch_cam import cam


def _kreis(x, y, r, n=24):
    import math
    return [(x + r * math.cos(2 * math.pi * i / n), y + r * math.sin(2 * math.pi * i / n)) for i in range(n)]


@pytest.mark.skipif(not cam.verfuegbar(), reason="CAM-Modul fehlt")
def test_versatz_reproduzierbar():
    ringe = [[(0, 0), (100, 0), (100, 60), (0, 60)]] + [_kreis(12 + 15 * i, 20 + 20 * (i % 2), 4) for i in range(6)]
    erst = cam.versatz(ringe, -1.5, 0.01)
    assert len(erst) == 7
    for _ in range(30):
        _platz = [bytearray(1000 * (_ + 1)) for _ in range(3)]      # Speicher anders belegen
        assert cam.versatz(ringe, -1.5, 0.01) == erst
