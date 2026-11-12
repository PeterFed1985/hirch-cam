"""Adaptiv (Fräsbahnen 25.10.2026): was die adaptiven Bahnen nicht erreichen (schmale Stücke ohne Platz für die
Helix – am Impeller ein 3-mm-Ring am Rand von Stufe 3), räumt die Tasche in Runden nach; dazu der Vorschub-Zuschlag
für dünnere Späne."""
import math

import pytest
from shapely.geometry import Point, Polygon

from hirch_cam import cam
from hirch_cam.ops.tasche import span_zuschlag, tasche

braucht_cam = pytest.mark.skipif(not (cam.verfuegbar() and hasattr(cam._hc, "adaptiv")), reason="CAM-Modul fehlt")

QUADRAT = [(0, 0), (40, 0), (40, 40), (0, 40), (0, 0)]
SCHLITZ = [(50, 0), (54, 0), (54, 30), (50, 30), (50, 0)]      # 4 mm breit: keine Helix für Ø 3


def _gefraest(tp, poly, z, r):
    """Anteil der Fläche, die der Fräser auf Tiefe z überstreicht."""
    from shapely.geometry import LineString
    from shapely.ops import unary_union
    stuecke = []
    for a, b in zip(tp.moves, tp.moves[1:]):
        if not b.rapid and abs(a.z - z) < 1e-6 and abs(b.z - z) < 1e-6:
            stuecke.append(LineString([(a.x, a.y), (b.x, b.y)]).buffer(r, 8) if (a.x, a.y) != (b.x, b.y)
                           else Point(b.x, b.y).buffer(r, 8))
    return unary_union(stuecke).intersection(poly).area / poly.area


@braucht_cam
def test_schmales_stueck_wird_in_runden_nachgeraeumt():
    tp, warn = tasche([(QUADRAT, True), (SCHLITZ, True)], 3.0, 3.0, 3.0, 1000, 300, strategie="adaptiv",
                      adaptiv_last=15, start=(0, 0, 20))
    assert _gefraest(tp, Polygon(QUADRAT[:-1]), -3.0, 1.5) > 0.98
    assert _gefraest(tp, Polygon(SCHLITZ[:-1]), -3.0, 1.5) > 0.98, warn


@braucht_cam
def test_zuschlag_nur_auf_den_schnitten():
    tp, _w = tasche([(QUADRAT, True)], 6.0, 3.0, 3.0, 1000, 300, strategie="adaptiv", adaptiv_last=15,
                    start=(0, 0, 20))
    vorschuebe = {m.feed for m in tp.moves if not m.rapid and m.feed}
    assert round(1000 * span_zuschlag(0.15), 1) in vorschuebe
    assert max(vorschuebe) <= 1000 * 1.5 + 1e-6


def test_span_zuschlag():
    assert span_zuschlag(0.4) == 1.0                  # Bahnabstand der Runden: Schnittdaten wie sie sind
    assert span_zuschlag(0.15) == pytest.approx(1.372, abs=0.001)
    assert span_zuschlag(0.05) == 1.5                 # begrenzt
    assert span_zuschlag(0.5) == 1.0
