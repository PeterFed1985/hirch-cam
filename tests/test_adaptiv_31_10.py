"""Adaptives Ausräumen (Fräsbahnen 31.10., Denk-Hilfe Fable, nachgeprüft):
1. Was adaptiv in Runden räumt (Rückfall, wenn keine Helix passt; Reste), wird nicht mehr mit der ganzen Tiefe als
   Zustellung gefräst, sondern mit «runden_zustellung» – mit Hinweis.
2. Übergänge im Freien: hinauf im Eilgang, quer mit dem Span-Zuschlag, hinunter schräg – nie mehr im
   Eintauchvorschub senkrecht auf und ab. In engen Lücken deutlich schneller, Abtrag gleich."""
import pytest

from hirch_cam import cam
from hirch_cam.engine import bahnlaenge
from hirch_cam.ops import tasche as T

pytestmark = pytest.mark.skipif(not (cam.verfuegbar() and hasattr(cam._hc, "adaptiv")),
                                reason="CAM-Modul fehlt")


def _r(w, h):
    return ([(0, 0), (w, 0), (w, h), (0, h)], True)


def test_rueckfall_in_runden_mit_kleiner_zustellung():
    # 60 × 5 mit Ø 3: keine Helix passt – adaptiv fällt ganz auf Runden zurück
    tp, warn = T.tasche([_r(60, 5)], 3, 3, 3, 900, 250, strategie="adaptiv", adaptiv_last=15,
                        runden_zustellung=0.75, start=(0, 0, 20))
    ebenen = sorted({round(m.z, 3) for m in tp.moves if not m.rapid and m.z < 0})
    assert ebenen == [-3.0, -2.25, -1.5, -0.75]
    assert T.ADAPTIV_RUNDEN % "0,75" in warn
    # ohne den Wert wie bisher (eine Ebene)
    tp0, warn0 = T.tasche([_r(60, 5)], 3, 3, 3, 900, 250, strategie="adaptiv", adaptiv_last=15, start=(0, 0, 20))
    assert sorted({round(m.z, 3) for m in tp0.moves if not m.rapid and m.z < 0}) == [-3.0]
    assert not any(w.startswith("Wo adaptiv nicht hinkommt") for w in warn0)


def test_uebergaenge_ohne_senkrechte_huebe_im_eintauchvorschub():
    tp, _w = T.tasche([_r(60, 8)], 3, 3, 3, 900, 250, strategie="adaptiv", adaptiv_last=15, start=(0, 0, 20))
    pos, huebe = (0, 0, 20), 0
    for m in tp.moves:
        senkrecht = abs(m.x - pos[0]) < 1e-6 and abs(m.y - pos[1]) < 1e-6
        if senkrecht and not m.rapid and m.z > pos[2] + 1e-6:
            huebe += 1                                   # im Vorschub senkrecht hinauf
        pos = (m.x, m.y, m.z)
    assert huebe == 0
    # vorher 2,67 min (Zeitschätzung), jetzt um 1,8
    assert bahnlaenge(tp, (0, 0, 20))[2] < 2.0


def test_gleich_und_gegenlauf_im_wechsel():
    """Peter 31.10.: Nachbar-Schwünge abwechselnd im Gleich- und Gegenlauf – die Rückwege entfallen. Schneller,
    dieselbe Bahn je Schwung (gleiche Punkte); Anfang der umgekehrten Schwünge im Vollnut-Vorschub."""
    lin = [_r(60, 8)]
    kw = dict(strategie="adaptiv", adaptiv_last=15, start=(0, 0, 20), vollnut=0.5)
    tp0, _ = T.tasche(lin, 3, 3, 3, 900, 250, **kw)
    tp1, _ = T.tasche(lin, 3, 3, 3, 900, 250, wechselnd=True, **kw)
    t0, t1 = bahnlaenge(tp0, (0, 0, 20))[2], bahnlaenge(tp1, (0, 0, 20))[2]
    assert t1 < 0.8 * t0, (t0, t1)                       # gemessen 1,78 → 1,25 min
    # alle Schnittpunkte von vorher kommen wieder vor (die Bahn ist dieselbe, nur die Richtung wechselt)
    p0 = {(round(m.x, 3), round(m.y, 3)) for m in tp0.moves if not m.rapid and abs(m.z + 3) < 1e-6}
    p1 = {(round(m.x, 3), round(m.y, 3)) for m in tp1.moves if not m.rapid and abs(m.z + 3) < 1e-6}
    assert p0 <= p1
    # im Eilgang nur senkrecht (hinauf aus dem Schnitt, Helix-Anfahrt) – kein schräger Eilgang unter der Oberfläche
    pos = (0, 0, 20)
    for m in tp1.moves:
        if m.rapid and min(m.z, pos[2]) < 0:
            assert abs(m.x - pos[0]) < 1e-6 and abs(m.y - pos[1]) < 1e-6, (pos, m)
        pos = (m.x, m.y, m.z)
    # Schnitte im Vollnut-Vorschub (900 × 0,5) auf der Endtiefe: Ansatz der umgekehrten Schwünge
    assert sum(1 for m in tp1.moves if not m.rapid and m.feed == 450.0 and abs(m.z + 3) < 1e-6) > \
        sum(1 for m in tp0.moves if not m.rapid and m.feed == 450.0 and abs(m.z + 3) < 1e-6)


def test_wahl_je_tasche_nimmt_die_schnellste():
    """Peter 01.11.: adaptiv nur, wo es schneller ist – ganz adaptiv, weite Teile adaptiv + schmale in Runden oder
    ganz in Runden; Hirch CNC rechnet alle drei und nimmt die schnellste. Kamm: breiter Rücken, enge Finger."""
    kamm = [(10, 10), (110, 10), (110, 40)]
    for i in range(7):
        kamm += [(100 - 12 * i, 40), (100 - 12 * i, 70), (94 - 12 * i, 70), (94 - 12 * i, 40)]
    kamm += [(10, 40)]
    lin = [(kamm, True)]
    kw = dict(strategie="adaptiv", adaptiv_last=15, start=(0, 0, 20), wechselnd=True, runden_zustellung=1.5)
    zeiten = {}
    for wf in (0.0, 4.0, -1.0):
        tp, _ = T.tasche(lin, 3, 6, 6, 1000, 300, weit_faktor=wf, _einzeln=True, **kw)
        zeiten[wf] = T._zeit(tp, (0, 0, 20))
    aus = []
    tp, _ = T.tasche(lin, 3, 6, 6, 1000, 300, weit_faktor=4.0, ausgeraeumt=aus, **kw)
    assert T._zeit(tp, (0, 0, 20)) == pytest.approx(min(zeiten.values()))
    assert len({round(v, 3) for v in zeiten.values()}) == 3   # drei verschiedene Varianten
    assert aus                                        # das Ausgeräumte der gewählten Variante ist eingetragen
