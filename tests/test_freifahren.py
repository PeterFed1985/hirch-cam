"""Tief freifahren (Peter 25.10.2026): zwischen zwei Zügen nur knapp über die Oberfläche, über Hindernissen
auf der Freifahrhöhe; Abtrag unverändert. Dazu: Prüfung bricht bei sich selbst schneidendem Umriss nicht ab."""
from hirch_cam import engine
from hirch_cam.model import Bohrzyklus, Move, Toolpath
from hirch_cam.ops import freifahren


def _hub(x0, x1, cz=3.0):
    """Zug bei x0 auf −1, abheben, hinüber nach x1, runter auf 0,5, eintauchen."""
    return [Move(True, x0, 0, cz), Move(True, x0, 0, 0.5), Move(False, x0, 0, -1, 300),
            Move(False, x0 + 5, 0, -1, 1000), Move(True, x0 + 5, 0, cz), Move(True, x1, 0, cz),
            Move(True, x1, 0, 0.5), Move(False, x1, 0, -1, 300)]


def test_freier_weg_wird_gesenkt():
    tp = Toolpath(moves=_hub(0, 30))
    assert freifahren.tief(tp, 3.0, 0.5, start=(0, 0, 20)) == 1
    zs = [m.z for m in tp.moves]
    assert max(zs[1:]) == 0.5                      # nach dem ersten Anfahren nie mehr über 0,5
    assert len(tp.moves) == 7                      # «runter auf 0,5» fällt weg (Nullbewegung)


def test_hindernis_auf_dem_weg_bleibt_oben():
    tp = Toolpath(moves=_hub(0, 30))
    assert freifahren.tief(tp, 3.0, 0.5, kreise=[(18, 0, 3)], start=(0, 0, 20)) == 0
    assert any(m.z == 3.0 for m in tp.moves[2:])
    tp = Toolpath(moves=_hub(0, 30))               # Hindernis neben dem Weg: frei
    assert freifahren.tief(tp, 3.0, 0.5, kreise=[(18, 10, 3)], start=(0, 0, 20)) == 1


def test_bohrzyklen_bleiben_unveraendert():
    tp = Toolpath(moves=_hub(0, 30), zyklen=[Bohrzyklus(0, 3)])
    vorher = [(m.x, m.y, m.z) for m in tp.moves]
    assert freifahren.tief(tp, 3.0, 0.5) == 0
    assert [(m.x, m.y, m.z) for m in tp.moves] == vorher


def test_hoehe():
    assert freifahren.hoehe(0.2, 3.0) == 0.5
    assert freifahren.hoehe(0.8, 3.0) == 0.8
    assert freifahren.hoehe(0.5, 0.3) == 0.3


def _auftrag(tmp_path, **mehr):
    q = lambda x, y, a: [(x, y), (x + a, y), (x + a, y + a), (x, y + a)]
    a = {"werkzeuge": {"A": {"typ": "v", "durchmesser": 6, "v_winkel": 60, "nummer": 1, "vorschub": 1000,
                             "eintauchen": 300}},
         "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 10},
         "postprozessor": "richauto_a11", "ausgabe": str(tmp_path / "t.tap"),
         "bearbeitungen": [{"id": "g", "art": "gravur", "name": "Gravur", "werkzeug": "A", "tiefe": 0.5,
                            "zustellung": 0.5,
                            "objekte": [{"punkte": q(10, 10, 5), "geschlossen": True},
                                        {"punkte": q(40, 10, 5), "geschlossen": True}]}]}
    a.update(mehr)
    return a


def _zs(erg):
    from hirch_cam.post import tap_mm
    job = tap_mm.read(open(erg["programme"][0]["datei"], newline="").read())
    return [m for tp in job.toolpaths for m in tp.moves]


def test_engine_gravur_faehrt_tief_und_spannmittel_hebt(tmp_path):
    ms = _zs(engine.rechnen(_auftrag(tmp_path)))
    eil_xy = [m for a, m in zip(ms, ms[1:]) if m.rapid and (m.x, m.y) != (a.x, a.y) and m.z < 2.9]
    assert eil_xy and all(abs(m.z - 0.5) < 1e-9 for m in eil_xy)
    # Spannmittel zwischen den beiden Quadraten: der Weg dorthin bleibt auf der Freifahrhöhe
    spann = [{"punkte": [(25, 0), (30, 0), (30, 30), (25, 30)], "seite": 1, "hoehe": 2}]
    ms = _zs(engine.rechnen(_auftrag(tmp_path, spannmittel=spann)))
    ueber = [m for a, m in zip(ms, ms[1:]) if m.rapid and a.rapid and abs(a.z - m.z) < 1e-9
             and min(a.x, m.x) < 25 and max(a.x, m.x) > 30]
    assert ueber and all(m.z >= 3.0 - 1e-9 for m in ueber)


def test_pruefung_mit_selbstschneidendem_umriss():
    from hirch_cam import pruefung
    acht = [{"punkte": [(0, 0), (10, 10), (10, 0), (0, 10)], "geschlossen": True}]   # Achterschleife
    fl = pruefung._flaechen(acht)
    assert len(fl) == 1 and fl[0].is_valid


def test_helix_bohren_sinkt_nie_schneller_als_eintauchen():
    import math
    from hirch_cam.ops.bohren import bohren
    for loch in (6.2, 6.6, 10.2):
        kreis = [(loch / 2 * math.cos(2 * math.pi * i / 72) + 20, loch / 2 * math.sin(2 * math.pi * i / 72) + 20)
                 for i in range(73)]
        tp, _w = bohren([(kreis, True)], 6.0, 10.0, 1.5, 300, methode="helix", vorschub=1100)
        p = None
        for m in tp.moves:
            if p is not None and not m.rapid and m.feed and m.z < p.z - 1e-9:
                l = math.dist((p.x, p.y, p.z), (m.x, m.y, m.z))
                assert (p.z - m.z) / (l / m.feed) <= 300 * 1.02, loch
            p = m
        assert min(m.z for m in tp.moves) <= -10 + 1e-6


def test_bestand_ausserhalb_des_rohteils_luft():
    """3D «Über den Materialrand hinaus» (3D → Fräsbahnen 26.10. 19:05, Punkt 4): ausserhalb des Rohteils ist Luft –
    aber nur, wenn das Rohteil angegeben ist; sonst zählt dort Material bis Z 0 (das echte Rohteil kann grösser
    sein als die Zeichenfläche)."""
    import pytest
    pytest.importorskip("hirch_cam.hirch_relief")
    mit = freifahren.Bestand(-10, -10, 70, 50, -21.0, material=(60, 40))
    ohne = freifahren.Bestand(-10, -10, 70, 50, -21.0)
    assert mit.hoch(30, 20, 3) == 0.0 and ohne.hoch(30, 20, 3) == 0.0          # im Rohteil: unberührt
    assert mit.hoch(-6, 20, 3) == -21.0                                         # daneben: Luft
    assert ohne.hoch(-6, 20, 3) == 0.0
    assert mit.hoch(-2, 20, 3) == 0.0                                           # Fräser reicht noch hinein
    assert mit.hoch(-40, 20, 3) == -21.0 and ohne.hoch(-40, 20, 3) == 0.0       # ausserhalb des Rasters
    assert mit.hoch(30, 60, 3) == -21.0



def test_bestand_hoch_ohne_modell():
    """3D-Schlichten (Main-Sitzung 27.10.): senkrecht hinunter auf einen Bahnpunkt zählt neben einer Schaufel nur, was
    über dem Modell noch steht – die Schaufel selbst schützt die Abtastung der Bahn."""
    import numpy as np
    import pytest
    pytest.importorskip("hirch_cam.hirch_relief")
    b = freifahren.Bestand(0, 0, 20, 20, -26.0)
    b.H[:] = -25.0                                   # geräumt bis auf den Boden …
    b.H[:, 40:44] = 0.0                              # … ausser einer Schaufel (x 10 … 10,75), Oberkante Z 0
    b.modell = np.full(b.H.shape, -25.0, np.float32)
    b.modell[:, 40:44] = 0.0
    x = 10 - 3.05                                    # Kugel R 3 neben der Schaufel
    assert b.hoch(x, 10, 3.0) == 0.0                 # mit Modell: Oberkante der Schaufel
    assert b.hoch(x, 10, 3.0, ohne_modell=True) < -24.9
    b.H[:, 38:40] = -10.0                            # neben der Schaufel steht noch Material (kein Modell)
    assert b.hoch(x, 10, 3.0, ohne_modell=True) == -10.0


def test_hinunter_nach_dem_querweg_nie_im_eilgang_ins_material():
    """Gefunden 29.10. (Denk-Hilfe Fable, nachgestellt): nach Abheben und Querweg fuhr der Weg hinunter am Ziel im
    Eilgang bis auf die geplante Tiefe – auch wenn dort laut Simulation noch Material stand; das Sicherheitsnetz sah
    diesen Eilgang nicht. Jetzt: Eilgang nur bis über das stehende Material, der Rest im Eintauchvorschub."""
    import pytest
    pytest.importorskip("hirch_cam.hirch_relief")
    from hirch_cam.model import Move, Toolpath
    b = freifahren.Bestand(0, 0, 40, 20, -20.0)
    b.H[:] = -20.0
    b.H[:, int(25 / freifahren.ZELLE):int(35 / freifahren.ZELLE)] = -5.0     # Insel steht bis Z −5
    tp = Toolpath()
    tp.moves = [Move(False, 5, 10, -20, 500), Move(True, 5, 10, 5), Move(True, 30, 10, 5), Move(True, 30, 10, -15),
                Move(False, 32, 10, -15, 500)]
    freifahren.ueber_bestand(tp, b, {"durchmesser": 6}, 5.0, 0.5, (5, 10, -20), 5.0, eintauchen=300)
    pos = (5, 10, -20)
    for m in tp.moves:
        if m.rapid and m.z < pos[2]:
            assert m.z >= -5.0, (m.x, m.y, m.z)          # nie im Eilgang unter die Oberkante der Insel
        pos = (m.x, m.y, m.z)
    assert (tp.moves[-1].x, tp.moves[-1].z) == (32, -15)
    assert any(not m.rapid and m.feed == 300 and abs(m.z + 15) < 1e-9 for m in tp.moves)
