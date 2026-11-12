"""Peter 27.10.: bei mehreren Objekten zuerst den kürzesten Weg über die Eintauchstellen finden, dann die Bahnen
rechnen – nicht beim einen Teil auf der einen Seite eintauchen und beim nächsten auf der gegenüberliegenden."""
import itertools
import math
import random

from hirch_cam.ops import bahn, gravur
from hirch_cam.ops import kontur as K


def _eintauchstellen(tp):
    p, aus, eil = (0.0, 0.0, 20.0), [], 0.0
    for m in tp.moves:
        if m.rapid:
            eil += math.hypot(m.x - p[0], m.y - p[1])
        elif p[2] > 0 >= m.z - 1e-9 and math.hypot(m.x - p[0], m.y - p[1]) < 0.01:
            aus.append((m.x, m.y))
        p = (m.x, m.y, m.z)
    return aus, eil


def _rechteck(x, y, w, h):
    return ([(x, y), (x + w, y), (x + w, y + h), (x, y + h)], True)


def test_eintauch_wahl_ist_der_kuerzeste_weg():
    rnd = random.Random(3)
    for _ in range(30):
        kand = [[((rnd.uniform(0, 50), rnd.uniform(0, 50)),) * 2 for _k in range(rnd.randint(1, 4))]
                for _i in range(4)]
        wahl = bahn.eintauch_wahl(kand, (0.0, 0.0))

        def laenge(w):
            pos, s = (0.0, 0.0), 0.0
            for c, j in zip(kand, w):
                s += math.dist(pos, c[j][0])
                pos = c[j][1]
            return s
        best = min(laenge(w) for w in itertools.product(*[range(len(c)) for c in kand]))
        assert laenge(wahl) <= best + 1e-9


def test_ring_ab_punkt_fuegt_ein():
    ring = [(0, 0), (10, 0), (10, 5), (0, 5)]
    assert bahn.ring_ab_punkt(ring, (4.0, 0.0)) == [(4.0, 0.0), (10, 0), (10, 5), (0, 5), (0, 0)]
    assert bahn.ring_ab_punkt(ring, (10, 5)) == [(10, 5), (0, 5), (0, 0), (10, 0)]


def test_kontur_im_bogen_taucht_auf_der_nahen_seite_ein():
    """Zwei Teile nebeneinander, Start links unten: beide Eintauchstellen liegen auf der Seite zum Vorgänger, nicht
    in der Mitte der längsten Kante gegenüber (wie bisher bei «Im Bogen»)."""
    teile = [_rechteck(10, 10, 40, 25), _rechteck(70, 10, 40, 25), _rechteck(10, 60, 100, 15)]
    for anfahren in ("", "bogen"):
        tp, _w = K.kontur(teile, 6, 5, 2.5, 1000, 300, seite="aussen", materialdicke=5, anfahren=anfahren,
                          start=(0, 0, 20))
        ein, eil = _eintauchstellen(tp)
        assert len(ein) == 3, ein
        assert ein[1][0] < 75, ein          # zweites Teil: auf der Seite zum ersten
        # bisher 134 mm (senkrecht, je die nächste Ecke) bzw. 154 mm (im Bogen, Mitte der längsten Kante)
        assert eil < (120 if anfahren else 105), (anfahren, eil)


def test_gravur_waehlt_die_enden_ueber_alle_linien():
    linien = [gravur.Linie([(0, 0), (0, 30)]), gravur.Linie([(5, 32), (5, 2)]), gravur.Linie([(10, 0), (10, 30)])]
    for tiefe, zust in ((1.0, 1.0), (1.0, 0.5)):
        tp = gravur.gravur(linien, tiefe, zust, 1000, 300, start=(0, 0))
        _ein, eil = _eintauchstellen(tp)
        # kürzester Weg: an den Enden der Reihe nach, jeweils ein paar mm hinüber
        assert eil < 20 + 1e-6, (tiefe, zust, eil)
