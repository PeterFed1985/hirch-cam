"""Gewinde: der Kreis darf das Kernloch zeigen (Ø 10,2 für M12, Peter 24.10.2026) – Nenn-Ø geht weiter."""
import pytest

from hirch_cam.ops.gewinde import aus_kreis, gewinde

GF = {"typ": "gewinde", "durchmesser": 4.8, "zaehne": 1, "vorschub": 600, "eintauchen": 200}


@pytest.mark.parametrize("d, p, nenn, steig, kern", [
    (12.0, None, 12.0, 1.75, None),      # Nenn-Ø wie bisher
    (10.2, None, 12.0, 1.75, 10.2),      # Kernloch-Bohrer M12
    (6.8, None, 8.0, 1.25, 6.8),
    (3.3, None, 4.0, 0.7, 3.3),
    (8.0, None, 8.0, 1.25, None),
    (10.4, 1.5, 12.0, 1.5, 10.4),        # Feingewinde M12×1,5 als Kernloch
    (12.0, 1.5, 12.0, 1.5, None),
    (7.3, None, 7.3, None, None),        # weder noch: wie bisher «kein Normgewinde»
])
def test_aus_kreis(d, p, nenn, steig, kern):
    n, s, k, hinweis = aus_kreis(d, p)
    assert (n, s, k) == (nenn, steig, kern)
    assert hinweis == "" or d in (12.0,)


def test_mehrdeutig_melden_und_steigung_entscheidet():
    n, s, k, hinweis = aus_kreis(5.0)                   # M5 oder Kernloch M6
    assert (n, s, k) == (5.0, 0.8, None) and "M6" in hinweis and "Steigung 1 mm" in hinweis
    assert aus_kreis(5.0, 1.0)[:3] == (6.0, 1.0, 5.0)    # Steigung 1: M6
    assert aus_kreis(5.0, 0.8)[:3] == (5.0, 0.8, None)   # Steigung 0,8: M5
    n, s, k, hinweis = aus_kreis(14.0)                  # M14 oder Kernloch M16 – gleiche Steigung
    assert n == 14.0 and "Nenn-Ø 16 mm zeichnen" in hinweis


def test_aussengewinde_kreis_ist_der_zapfen():
    assert aus_kreis(10.2, None, innen=False)[0] == 10.2


def test_kernloch_kreis_gibt_dieselbe_gewindebahn():
    tp_n, w_n, i_n = gewinde([((0.0, 0.0), 12.0)], GF, 10.0, 600, 200)
    tp_k, w_k, i_k = gewinde([((0.0, 0.0), 10.2)], GF, 10.0, 600, 200)
    assert [(m.x, m.y, m.z) for m in tp_n.moves] == [(m.x, m.y, m.z) for m in tp_k.moves]
    assert i_n[0]["name"] == i_k[0]["name"] == "M12"
    assert i_k[0]["nenn"] == 12.0 and i_k[0]["kernloch"] == pytest.approx(10.2)
    assert i_n[0]["kernloch"] == pytest.approx(12 - 1.0825 * 1.75)
    assert not any("kein Normgewinde" in t for t in w_k)


def test_engine_meldet_nenn_fuer_3d(tmp_path):
    from hirch_cam import engine
    import math
    kreis = [(5.1 * math.cos(2 * math.pi * i / 72) + 30, 5.1 * math.sin(2 * math.pi * i / 72) + 30)
             for i in range(72)]
    a = {"werkzeuge": {"A": dict(GF, nummer=1)},
         "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 18},
         "postprozessor": "richauto_a11", "ausgabe": str(tmp_path / "g.tap"),
         "bearbeitungen": [{"id": "g", "art": "gewinde", "name": "Gewinde", "werkzeug": "A", "tiefe": 10,
                            "objekte": [{"punkte": kreis, "geschlossen": True}]}]}
    erg = engine.rechnen(a)
    assert erg["ok"]
    g = erg["gewinde"][0]
    assert g["nenn"] == 12.0 and g["kernloch"] == pytest.approx(10.2, abs=0.02) and g["name"] == "M12"
