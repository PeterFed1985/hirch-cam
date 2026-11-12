"""CAM-Modul hirch_cam (libarea/Clipper und V-Carve-Mittellinien aus FreeCAD CAM)."""
import math

import pytest

from hirch_cam import cam

shapely = pytest.importorskip("shapely")
from shapely.geometry import Point, Polygon, box  # noqa: E402

pytestmark = pytest.mark.skipif(not cam.verfuegbar(), reason="CAM-Modul hirch_cam nicht gebaut")


def test_versatz_aussen_mit_runden_ecken():
    teile = cam.versatz([[(0, 0), (10, 0), (10, 10), (0, 10)]], 1.0, 0.005)
    assert len(teile) == 1 and teile[0][1] is False
    ring = Polygon(teile[0][0])
    assert ring.area == pytest.approx(100 + 40 + math.pi, abs=0.05)
    # jeder Punkt genau 1 mm vom Quadrat entfernt
    q = box(0, 0, 10, 10)
    for p in teile[0][0]:
        assert q.exterior.distance(Point(p)) == pytest.approx(1.0, abs=0.006)


def test_versatz_innen_mit_insel():
    teile = cam.versatz([[(0, 0), (10, 0), (10, 10), (0, 10)],
                         [(3, 3), (7, 3), (7, 7), (3, 7)]], -1.0, 0.005)
    aussen = [r for r, loch in teile if not loch]
    loecher = [r for r, loch in teile if loch]
    assert len(aussen) == 1 and len(loecher) == 1
    assert Polygon(aussen[0]).area == pytest.approx(64, abs=1e-6)
    assert Polygon(loecher[0]).area == pytest.approx(36 - 4 + math.pi, abs=0.05)


def test_puffer_verschwindet_bei_schmaler_flaeche():
    assert cam.puffer(box(0, 0, 10, 1), -0.6).is_empty
    g = cam.puffer(box(0, 0, 10, 4), -1.0)
    assert g.area == pytest.approx(8 * 2, abs=1e-6)


def test_tasche_aus_libarea():
    kurven = cam._hc.tasche([[[0, 0], [30, 0], [30, 20], [0, 20]]], 3.0, 0.0, 2.4, 0, 0.0, True, 0.01)
    assert kurven
    pts = [p for k in kurven for p in cam.linearisieren(k, 0.01)]
    # Fräsermitte bleibt mindestens den Radius vom Rand weg
    assert min(x for x, _ in pts) >= 3 - 1e-6 and max(x for x, _ in pts) <= 27 + 1e-6
    assert min(y for _, y in pts) >= 3 - 1e-6 and max(y for _, y in pts) <= 17 + 1e-6


def test_mittellinien_rechteck():
    zuege = cam.mittellinien(box(0, 0, 20, 6), 0.01)
    assert len(zuege) == 5
    mitte = [z for z in zuege if all(abs(p[1] - 3) < 1e-9 for p in z)]
    assert len(mitte) == 1
    xs = sorted(p[0] for p in mitte[0])
    assert xs[0] == pytest.approx(3) and xs[-1] == pytest.approx(17)
    assert all(p[2] == pytest.approx(3) for p in mitte[0])


def test_mittellinien_abstand_stimmt():
    ring = [(40 + 15 * math.cos(t / 64 * math.tau), 35 + 15 * math.sin(t / 64 * math.tau)) for t in range(64)]
    loch = [(40 + 8 * math.cos(t / 64 * math.tau), 35 + 8 * math.sin(t / 64 * math.tau)) for t in range(64)]
    form = Polygon(ring, [loch])
    for z in cam.mittellinien(form, 0.01):
        for x, y, d in z:
            assert form.boundary.distance(Point(x, y)) == pytest.approx(d, abs=0.01)


def test_mittellinien_abstand_auch_zwischen_den_punkten():
    """Zwischen zwei inneren Ecken (Steg eines «H») ist die Mittellinie eine Gerade, der Randabstand
    darauf aber eine Hyperbel. Früher lagen nur die Enden vor und dazwischen wurde linear
    interpoliert – an der Stelle fräste der V-Fräser bis 0,7 mm zu tief. Jetzt stimmt der
    Abstand auch in der Mitte jedes Teilstücks (so rechnet die V-Carve die Tiefe)."""
    h = Polygon([(0, 0), (6, 0), (6, 12), (20, 12), (20, 0), (26, 0), (26, 30), (20, 30), (20, 16),
                 (6, 16), (6, 30), (0, 30)])
    schlechteste = 0.0
    for z in cam.mittellinien(h, 0.01):
        for (x0, y0, d0), (x1, y1, d1) in zip(z, z[1:]):
            mitte = h.boundary.distance(Point((x0 + x1) / 2, (y0 + y1) / 2))
            schlechteste = max(schlechteste, (d0 + d1) / 2 - mitte)
    assert schlechteste < 0.02
