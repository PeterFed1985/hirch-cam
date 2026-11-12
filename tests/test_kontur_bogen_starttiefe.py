"""Kontur «im Bogen anfahren» mit «Beginnt bei Tiefe» (Fräsbahnen 25.10.2026, Impeller b7): der Bogen beginnt
neben der Kontur – dort im Eilgang hinunter nur, wenn auch da schon geräumt ist; sonst senkrecht auf der Kontur."""
from shapely.geometry import LineString, Point, Polygon

from hirch_cam.ops.kontur import kontur

QUADRAT = [(20, 20), (60, 20), (60, 50), (20, 50), (20, 20)]


def _eilgang_tief(tp, unter):
    return [b for a, b in zip(tp.moves, tp.moves[1:]) if b.rapid and b.z < unter]


def test_bogen_nur_wo_ueber_der_starttiefe_frei_ist():
    # früher geräumt: nur ein schmaler Streifen um die Linie (bis 6 mm tief), daneben steht noch Material
    streifen = LineString(QUADRAT).buffer(6.2)       # Fräser auf der Bahn ganz darin, kein Bogen
    tp, warn = kontur([(QUADRAT, True)], 6, 10, 2, 1000, 300, materialdicke=20, stege_anzahl=0,
                      anfahren="bogen", starttiefe=6, vorher=[(streifen, 6.0)], start=(0, 0, 20))
    assert "Neben der Kontur steht noch Material – dort senkrecht angefahren." in warn
    teil = Polygon(QUADRAT[:-1])
    for m in _eilgang_tief(tp, -0.1):
        assert streifen.contains(Point(m.x, m.y).buffer(3)), (m.x, m.y, m.z)   # nur über Geräumtem
    # ringsum geräumt (grosse Tasche aussen): dann im Bogen
    weit = Polygon(QUADRAT[:-1]).buffer(15)
    tp, warn = kontur([(QUADRAT, True)], 6, 10, 2, 1000, 300, materialdicke=20, stege_anzahl=0,
                      anfahren="bogen", starttiefe=6, vorher=[(weit, 6.0)], start=(0, 0, 20))
    assert not any("senkrecht angefahren" in w for w in warn)
    tauchen = [m for m in _eilgang_tief(tp, -0.1)]
    assert tauchen and all(teil.distance(Point(m.x, m.y)) > 3.5 for m in tauchen)


def test_kein_platz_eigener_hinweis():
    """Nachbar 4 mm neben der Kontur (Fräser Ø 6): kein Platz für den Bogen – der bisherige Hinweis."""
    nachbar = [(20, 54), (60, 54), (60, 70), (20, 70)]
    nachbar_l = [(64, 20), (80, 20), (80, 50), (64, 50)]
    unten = [(20, 4), (60, 4), (60, 16), (20, 16)]
    links = [(4, 20), (16, 20), (16, 50), (4, 50)]
    tp, warn = kontur([(QUADRAT, True)], 6, 10, 2, 1000, 300, materialdicke=20, stege_anzahl=0,
                      anfahren="bogen", start=(0, 0, 20), nachbarn=[nachbar, nachbar_l, unten, links])
    assert "Kein Platz für den Anfahrbogen – dort senkrecht angefahren." in warn
