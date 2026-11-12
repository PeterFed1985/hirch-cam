"""Kollisionsprüfung und Fräsergebnis: schräge Bewegungen tragen so tief ab, wie der Fräser
wirklich kommt (nicht nur am nächsten Punkt der Strecke) – sonst meldet der Rückzug nach einem
Abstieg «Eilgang trifft Material», obwohl dort nichts mehr steht. Und das zweiseitige 3D-Schruppen
taucht beim Nachfahren des Bodens nicht mehr im Eilgang ins volle Material."""
import math

import numpy as np
import pytest

from hirch_cam import kollision
from hirch_cam.kollision import Hoehenkarte, Werkzeugform

FORMEN = [Werkzeugform(6, "flach"), Werkzeugform(6, "kugel", eckradius=3), Werkzeugform(6, "eckradius", eckradius=1),
          Werkzeugform(6, "v", 60)]


def _echt(karte, a, b, wz, n=600, rand=0.0):
    z = karte.z
    X, Y = np.meshgrid((np.arange(karte.nx) + .5) * z, (np.arange(karte.ny) + .5) * z)
    B = np.zeros_like(X)
    for t in np.linspace(0, 1, n):
        c = [a[i] + t * (b[i] - a[i]) for i in range(3)]
        dd = np.hypot(X - c[0], Y - c[1])
        B = np.minimum(B, np.where(dd <= wz.r + rand, c[2] + wz.profil(np.minimum(dd, wz.r)), 0))
    return B


@pytest.mark.parametrize("mit_kern", [False, True])
def test_schraeg_so_tief_wie_der_fraeser(monkeypatch, mit_kern):
    if mit_kern and kollision._kern() is None:
        pytest.skip("hirch_relief nicht gebaut")
    if not mit_kern:
        monkeypatch.setattr(kollision, "_kern", lambda: None)
    for wz in FORMEN:
        for a, b in [((5, 5, -2), (7, 5, -3)), ((6, 6, -1), (4.5, 7, -2.5)), ((5, 6, -3), (6.5, 4, -1.2))]:
            karte = Hoehenkarte(12, 12, 20, zellen=12 * 12 / 0.1 ** 2)
            karte.abtragen(a, [b], wz)
            # ohne C++-Modul trägt Python eine halbe Zelle weiter ab (wie bisher)
            rand = 0.0 if mit_kern else 0.5 * karte.z
            echt = _echt(karte, a, b, wz, rand=rand)
            # Zellen genau auf dem Rand des Fräsers nicht vergleichen (dort entscheidet die Rundung)
            z = karte.z
            X, Y = np.meshgrid((np.arange(karte.nx) + .5) * z, (np.arange(karte.ny) + .5) * z)
            d, _t = karte._abstand(a, b, 0, karte.nx, 0, karte.ny)
            innen = np.abs(d - (wz.r + rand)) > 0.02
            assert float((karte.k - echt)[innen].max()) < 0.03, (wz.form, a, b)      # nicht zu flach
            assert float((echt - karte.k)[innen].max()) < 0.01, (wz.form, a, b)      # nicht tiefer als echt


def test_rueckzug_nach_abstieg_kein_treffer():
    karte = Hoehenkarte(20, 20, 20)
    wz = Werkzeugform(6, "kugel", eckradius=3)
    a, b = (8.0, 10.0, -5.0), (10.0, 10.0, -6.0)
    karte.abtragen(a, [b], wz)
    assert karte.trifft(b, (10.0, 10.0, 3.0), wz) is None


def test_zweiseitig_kein_eilgang_ins_material(tmp_path):
    pytest.importorskip("hirch_cam.hirch_relief")
    pytest.importorskip("inkex")
    from hirch_cam import dokument
    import test_oberflaeche as t
    svg = t._3d_zeichnung(tmp_path)
    erg = dokument.rechnen(svg, str(tmp_path / "a"), "k", entwurf=True)
    assert erg["ok"], erg.get("warnungen")
    eilgang = [b["text"] for b in erg["pruefung"] if "Eilgang trifft Material" in b["text"]]
    assert not eilgang, eilgang
