"""Tasche: zerfallen die Versatzrunden in Teilstücke, wird jedes Teilstück von innen nach aussen fertig
gefräst (Fräsbahnen 25.10.2026) – nicht Stufe für Stufe hin und her. Abtrag unverändert."""
import numpy as np

from hirch_cam.model import Job
from hirch_cam.ops.tasche import _innen_nach_aussen, tasche
from hirch_cam.simulation import Hoehenfeld

# Hantel: zwei Quadrate 20 × 20, verbunden durch einen Hals 6 mm breit
HANTEL = [(0, 0), (20, 0), (20, 7), (30, 7), (30, 0), (50, 0), (50, 20), (30, 20), (30, 13), (20, 13),
          (20, 20), (0, 20)]


def _huebe(tp):
    return sum(1 for a, b in zip(tp.moves, tp.moves[1:]) if b.rapid and b.z > a.z + 1e-6)


def test_teilstuecke_nacheinander():
    tp, _w = tasche([(HANTEL, True)], 3.0, 1.0, 1.0, 1000, 300, bahnabstand_prozent=40)
    # eine Ebene: höchstens einmal zwischen den Hälften abheben (+ am Ende). Vorher: 1 Hub, aber auf
    # jeder Stufe einmal hin und her quer durch den noch nicht geräumten Hals
    assert _huebe(tp) <= 2
    # Teilstücke: erst alle Runden einer Hälfte, dann die der anderen, zuletzt die um beide
    from shapely.geometry import Polygon
    g = Polygon(HANTEL)
    stufen = [s for s in (g.buffer(-1.5 - 1.2 * k) for k in range(8)) if not s.is_empty]
    art = []
    for _k, t in _innen_nach_aussen(stufen):
        x0, _y0, x1, _y1 = t.bounds
        a = "L" if x1 < 25 else "R" if x0 > 25 else "B"
        if not art or art[-1] != a:
            art.append(a)
    assert art in (["L", "R", "B"], ["R", "L", "B"])


def test_reihenfolge_innen_vor_aussen_und_vollstaendig():
    from shapely.geometry import Polygon
    g = Polygon(HANTEL)
    stufen = [g.buffer(-1.5 - 1.2 * k) for k in range(8)]
    stufen = [s for s in stufen if not s.is_empty]
    folge = _innen_nach_aussen(stufen)
    alle = [(k, t) for k, s in enumerate(stufen) for t in getattr(s, "geoms", [s])]
    assert len(folge) == len(alle)
    # jedes Polygon kommt erst, wenn alle in ihm liegenden schon da waren
    for i, (k, t) in enumerate(folge):
        for k2, t2 in folge[i + 1:]:
            assert not (k2 > k and t.contains(t2.representative_point()))


def test_abtrag_wie_ohne_teilstuecke():
    """Höhenfeld: alles innerhalb der Hantel (ausser den Ecken, die der Fräser nicht erreicht) ist
    auf Tiefe, nichts ausserhalb ist angeschnitten."""
    tp, _w = tasche([(HANTEL, True)], 3.0, 2.0, 1.0, 1000, 300, bahnabstand_prozent=40)
    h = Hoehenfeld(-5, -5, 55, 25, 0.1)
    h.fraesen(Job(toolpaths=[tp]), "flach", 3.0)
    X, Y = h.koordinaten()
    from shapely import contains_xy
    from shapely.geometry import Polygon
    g = Polygon(HANTEL)
    innen = contains_xy(g.buffer(-1.6).buffer(1.5), X, Y)
    aussen = ~contains_xy(g.buffer(0.05), X, Y)
    assert np.all(h.H[innen] < -1.99)
    assert np.all(h.H[aussen] > -1e-6)
