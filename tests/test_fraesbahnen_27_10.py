"""3D → Fräsbahnen 26.10. 22:01 (Fräsbahnen 27.10. nachts): 3D-Schruppen «Lücke für Lücke in Lagen», Sicherheitsnetz
für Geraden zwischen Bahnpunkten, Umlauf am Fuss an der Klippe."""
import math
import os
import sys

import numpy as np
import pytest

pytest.importorskip("hirch_cam.hirch_relief", reason="hirch_relief nicht gebaut")
pytest.importorskip("inkex")
sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "werkzeuge"))
import eilgang_pruefen as ep  # noqa: E402
import gcode_vergleich as gv  # noqa: E402
from test_messwerte_3d import MATERIAL, pruefmodell  # noqa: E402
from test_relief3d import mit_ocl, zeichnung  # noqa: E402


def _abschnitte(erg):
    s = {"leerweg": 0.0, "zeit_min": 0.0}
    for p in erg["programme"]:
        a = gv.summe(gv.auswerten(open(p["datei"], newline="").read())["abschnitte"])
        s["leerweg"] += a["leerweg"]
        s["zeit_min"] += a["zeit_min"]
    return s


def _rechnen(tmp_path, bea, name):
    from hirch_cam import dokument, speicher
    speicher.SPEICHER.leeren()
    ordner = tmp_path / name
    ordner.mkdir(exist_ok=True)
    svg = zeichnung(ordner, bea, material=MATERIAL, dreiecke=pruefmodell())
    erg = dokument.rechnen(svg, str(ordner / "aus"))
    assert erg["ok"], erg.get("warnungen")
    assert not [x for x in erg.get("pruefung", []) if "Eilgang" in x["text"]], erg.get("pruefung")
    assert ep.pruefen(erg, aussen_frei=(0, 0, MATERIAL[0], MATERIAL[1])) == []
    return erg, svg


def _nie_ins_modell(erg, svg, kleiner=0.02):
    """Genaue Prüfung je Programm: die Spitze liegt nirgends tiefer als der Drop-Cutter (Fräser 0,01 mm kleiner:
    an senkrechten Wänden springt die Abtastung, ein Hauch seitlich zählt nicht)."""
    from hirch_cam.ops import relief3d
    from hirch_cam.post import tap_mm
    from test_relief3d import auftrag_aus
    info, _a = auftrag_aus(svg)
    tri, _v = relief3d.modell_im_material(info["reliefs"][0], info["dicke"])
    for p in erg["programme"]:
        form = p.get("form") or "flach"
        d = float(p["durchmesser"]) - kleiner
        ab = relief3d._abtaster(tri, {"typ": "kugel" if form == "kugel" else "schaft", "durchmesser": d}, 0.0, -100.0)
        job = tap_mm.read(open(p["datei"], newline="").read())
        pos = (job.home[0], job.home[1], job.safe_z)
        pts = []
        for tp in job.toolpaths:
            for m in tp.moves:
                n = max(2, int(math.hypot(m.x - pos[0], m.y - pos[1]) / 0.05))
                t = np.linspace(0, 1, n + 1)[1:]
                pts.append(np.column_stack([pos[0] + (m.x - pos[0]) * t, pos[1] + (m.y - pos[1]) * t,
                                            pos[2] + (m.z - pos[2]) * t]))
                pos = (m.x, m.y, m.z)
        P = np.vstack(pts)
        tief = ab(P[:, :2]) - P[:, 2]
        assert float(tief.max()) <= 0.05, (p["name"], float(tief.max()), P[int(tief.argmax())])


S = {"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 3, "aufmass": 0.5}
BW = {"id": "bw", "art": "boeden_waende", "werkzeug": "C", "stufe": 6, "im_wechsel_mit": "s1"}
K = {"id": "f1", "art": "relief_schlichten", "werkzeug": "K", "bahnabstand": 15, "strategie": "konturen",
     "auslassen_fertig": True}


@mit_ocl
@pytest.mark.parametrize("lagen", [6.0, 4.0])
def test_schruppen_in_lagen_mit_boeden_und_waenden_im_wechsel(tmp_path, lagen):
    """Lagen innerhalb der Stufen von «Böden und Wände» (6 mm): 6 = Lage gleich Stufe, 4 = Lagen enden an der
    Stufengrenze. Sicher (Prüfung, feiner Prüfer, nie ins Modell) und nicht mehr Leerweg als Ebene für Ebene."""
    alt, _s = _rechnen(tmp_path, [dict(S, lagen=0.0), BW, K], "alt")
    neu, svg = _rechnen(tmp_path, [dict(S, lagen=lagen), BW, K], "neu")
    _nie_ins_modell(neu, svg)
    a, n = _abschnitte(alt), _abschnitte(neu)
    assert n["leerweg"] <= a["leerweg"] * 1.02 + 5, (a, n)
    # im Wechsel: die Stufen bleiben Stufen (ein Programm, Schruppen und Böden/Wände abwechselnd)
    keys = [x["key"] for p in neu["programme"] for x in p.get("abschnitte") or []]
    assert any("stufe2" in k for k in keys), keys


@mit_ocl
def test_schlichten_nie_ins_modell(tmp_path):
    """Umlauf am Fuss an der Klippe, Höhenlinien getrennt: die Spitze liegt nirgends unter dem Drop-Cutter."""
    for strategie in ("konturen", "kombiniert"):
        erg, svg = _rechnen(tmp_path, [S, BW, dict(K, strategie=strategie)], strategie)
        _nie_ins_modell(erg, svg)


def test_sehnen_sicher_ueber_wand_und_auf_kuppel():
    from hirch_cam.ops import relief3d

    def wand(xy):                       # Klippe: bis x 0,8 Boden −10, dahinter Wand bis 0
        return np.where(xy[:, 0] >= 0.8, 0.0, -10.0)
    # beide Enden auf dem Boden, die Gerade dazwischen streift die Wand (Ecke bei y 2)
    s = np.array([[0.0, 0.0, -10.0], [0.79, 2.0, -10.0], [0.0, 4.0, -10.0]])
    s_streift = np.array([[0.0, 0.0, -10.0], [0.7, 2.0, -10.0]])
    s_streift[1, 0] = 1.2                                           # Ende hinter der Kante: quer durch die Wand
    s_streift = np.array([[0.0, 0.0, -10.0], [0.0, 2.0, -10.0]])
    s_quer = np.array([[0.0, 0.0, -10.0], [1.5, 1.0, 0.0], [0.0, 2.0, -10.0]])
    for bahn in (s, s_streift, s_quer):
        out = relief3d._sehnen_sicher([bahn], wand)[0]
        for a, b in zip(out[:-1], out[1:]):
            for t in np.linspace(0, 1, 200):
                q = a + (b - a) * t
                assert q[2] >= wand(q[None, :2])[0] - 0.02, (bahn, out)

    def kuppel(xy):
        return -np.hypot(xy[:, 0], xy[:, 1]) ** 2 / 20.0
    x = np.arange(-6.0, 6.01, 2.0)                                  # 2 mm Abstand: Durchhang 0,05 mm
    s = np.column_stack([x, np.zeros_like(x), kuppel(np.column_stack([x, np.zeros_like(x)]))])
    out = relief3d._sehnen_sicher([s], kuppel)[0]
    assert len(out) > 2
    # auf der gewölbten Fläche nur verdichtet: knapp über der Fläche, nie darunter
    for a, b in zip(out[:-1], out[1:]):
        for t in np.linspace(0, 1, 20):
            q = a + (b - a) * t
            c = kuppel(q[None, :2])[0]
            assert c - 0.03 <= q[2] <= c + 0.03
