"""Testsatz «Messwerte» für 3D (3D → Fräsbahnen 25.10.2026, Punkt 3): ein kleines Modell mit Boden, dünner Wand,
Rundung am Fuss und engem Spalt; 3D-Schruppen + «Böden und Wände» im Wechsel + 3D-Schlichten «kombiniert,
Fertiges auslassen». Werte in tests/messwerte.json (Schlüssel «3d_…»): Zeit, Leerweg, Fräsweg, Eintauchen,
senkrechter Vorschub (dort steckt die Zeit des Schlichtens) und der Anteil der Modellfläche, auf dem mehr als
0,5 mm stehen bleibt. Dazu: kein Eilgang durch Material (werkzeuge/eilgang_pruefen.py).

Rot, wenn etwas schlechter wird (Zeit/Leerweg/senkrecht > 1 %, öfter eingetaucht, Rest > 0,3 Prozentpunkte mehr)
oder das Modell verletzt wird; neue Werte bewusst mit HIRCH_MESSWERTE_NEU=1.
"""
import json
import math
import os
import sys

import numpy as np
import pytest

hirch_relief = pytest.importorskip("hirch_cam.hirch_relief", reason="hirch_relief nicht gebaut")
pytest.importorskip("inkex")

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "werkzeuge"))
import gcode_vergleich as gv  # noqa: E402
import eilgang_pruefen as ep  # noqa: E402
from test_messwerte import DATEI, NEU, _erwartet  # noqa: E402
from test_relief3d import mit_ocl, zeichnung  # noqa: E402

MATERIAL = (90, 60, 20)


def _quader(x0, y0, x1, y1, z0, z1):
    p = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
         (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    f = [(4, 5, 6), (4, 6, 7), (0, 2, 1), (0, 3, 2), (0, 1, 5), (0, 5, 4), (1, 2, 6), (1, 6, 5),
         (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7)]
    return [[p[a], p[b], p[c]] for a, b, c in f]


def _rundung(x_wand, y0, y1, z0, r, n=16):
    """Hohlkehle R r links an einer Wand bei x_wand (Fuss auf z0): Prisma aus dem Viertelkreis, geschlossen."""
    bogen = [(x_wand - r + r * math.sin(math.pi / 2 * k / n), z0 + r - r * math.cos(math.pi / 2 * k / n))
             for k in range(n + 1)]
    ecke = (x_wand, z0)
    t = []
    for (xa, za), (xb, zb) in zip(bogen, bogen[1:]):
        t += [[(xa, y0, za), (xb, y0, zb), (xb, y1, zb)], [(xa, y0, za), (xb, y1, zb), (xa, y1, za)]]  # Kehle
        t += [[(ecke[0], y0, ecke[1]), (xb, y0, zb), (xa, y0, za)]]                                 # Stirn vorn
        t += [[(ecke[0], y1, ecke[1]), (xa, y1, za), (xb, y1, zb)]]                                 # Stirn hinten
    xa = x_wand - r
    t += [[(xa, y0, z0), (x_wand, y1, z0), (xa, y1, z0)], [(xa, y0, z0), (x_wand, y0, z0), (x_wand, y1, z0)]]
    t += [[(x_wand, y0, z0), (x_wand, y0, z0 + r), (x_wand, y1, z0 + r)],
          [(x_wand, y0, z0), (x_wand, y1, z0 + r), (x_wand, y1, z0)]]
    return t


def pruefmodell():
    """Sockel 70 × 40 × 4; Block A (x 12…28, 12 hoch) mit Hohlkehle R 3 am linken Fuss; dünne Wand 1,5 mm
    (x 32…33,5, 12 hoch); Spalt 4 mm; Block B (x 37,5…58) nur 8 hoch (zweiter Boden)."""
    t = _quader(0, 0, 70, 40, 0, 4)
    t += _quader(12, 8, 28, 32, 4, 16)
    t += _rundung(12, 8, 32, 4, 3.0)
    t += _quader(32, 8, 33.5, 32, 4, 16)
    t += _quader(37.5, 8, 58, 32, 4, 12)
    return np.array(t, float)


AUFGABEN = {
    "3d_schruppen_bw_schlichten": [
        {"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 3, "aufmass": 0.5},
        {"id": "bw", "art": "boeden_waende", "werkzeug": "C", "stufe": 6, "im_wechsel_mit": "s1"},
        {"id": "f1", "art": "relief_schlichten", "werkzeug": "K", "bahnabstand": 15, "strategie": "kombiniert",
         "auslassen_fertig": True}],
}


def _messen(name, tmp_path):
    from hirch_cam import dokument, speicher
    from hirch_cam.ops import relief3d
    from hirch_cam.post import tap_mm
    from hirch_cam.simulation import Hoehenfeld
    from test_relief3d import auftrag_aus
    speicher.SPEICHER.leeren()
    svg = zeichnung(tmp_path, AUFGABEN[name], material=MATERIAL, dreiecke=pruefmodell())
    erg = dokument.rechnen(svg, str(tmp_path / "aus"))
    assert erg["ok"], erg.get("warnungen")
    werte = {"zeit_min": 0.0, "leerweg": 0.0, "fraesweg": 0.0, "eintauchen": 0, "senkrecht_mm": 0.0}
    feld = None
    info, _a = auftrag_aus(svg)
    tri, _v = relief3d.modell_im_material(info["reliefs"][0], info["dicke"])
    for p in erg["programme"]:
        with open(p["datei"], newline="") as f:
            text = f.read()
        s = gv.summe(gv.auswerten(text)["abschnitte"])
        werte["zeit_min"] += s["zeit_min"]
        werte["leerweg"] += s["leerweg"]
        werte["fraesweg"] += s["fraesweg"]
        werte["eintauchen"] += s["eintauchen"] + s.get("zustellen", 0)
        job = tap_mm.read(text)
        prev = (job.home[0], job.home[1], job.safe_z)
        for tp in job.toolpaths:
            for m in tp.moves:
                dz, dxy = abs(m.z - prev[2]), math.hypot(m.x - prev[0], m.y - prev[1])
                if not m.rapid and dz > dxy:
                    werte["senkrecht_mm"] += dz
                prev = (m.x, m.y, m.z)
        if feld is None:
            feld = Hoehenfeld(0, 0, MATERIAL[0], MATERIAL[1], 0.2)
        form = p.get("form") or "flach"
        feld.fraesen(job, form, float(p["durchmesser"]), float(p.get("v_winkel") or 60),
                     eckradius=float(p.get("eckradius") or (float(p["durchmesser"]) / 2 if form == "kugel" else 0)))
    oben, _u, z_ = relief3d._raster(tri, (0, 0, MATERIAL[0], MATERIAL[1]), 0.2)
    ny, nx = min(oben.shape[0], feld.ny), min(oben.shape[1], feld.nx)
    o, h = oben[:ny, :nx], feld.H[:ny, :nx]
    # nur, wo das Modell im Raster eben genug ist (an senkrechten Wänden liegt eine Zelle halb im Modell)
    from scipy import ndimage
    o0 = np.where(np.isfinite(o), o, -1e3)
    spanne = ndimage.maximum_filter(o0, size=3) - ndimage.minimum_filter(o0, size=3)
    modell = np.isfinite(o) & (spanne < 0.6)
    ueber = (h - o)[modell]
    werte["rest_ueber_05_prozent"] = round(100.0 * float((ueber > 0.5).mean()), 2)
    # zu tief: genau gegen den Drop-Cutter je Programm (Fräser 0,02 mm kleiner – an senkrechten Wänden springt die
    # Abtastung). Das Raster 0,2 mm zählte an Stirnseiten einer Rundung Zellen halb im Modell (Fräsbahnen 27.10.:
    # 0,35 mm «verletzt», genau gerechnet 0,01 mm, nachdem «Böden und Wände» den Boden daneben fertig fräst)
    tief = 0.0
    for p in erg["programme"]:
        form = p.get("form") or "flach"
        ab = relief3d._abtaster(tri, {"typ": "kugel" if form == "kugel" else "schaft",
                                      "durchmesser": float(p["durchmesser"]) - 0.02}, 0.0, -100.0)
        with open(p["datei"], newline="") as f:
            job = tap_mm.read(f.read())
        pos = (job.home[0], job.home[1], job.safe_z)
        for tp in job.toolpaths:
            for m in tp.moves:
                n = max(2, int(math.hypot(m.x - pos[0], m.y - pos[1]) / 0.05))
                t = np.linspace(0, 1, n + 1)[1:]
                P = np.column_stack([pos[0] + (m.x - pos[0]) * t, pos[1] + (m.y - pos[1]) * t,
                                     pos[2] + (m.z - pos[2]) * t])
                tief = max(tief, float((ab(P[:, :2]) - P[:, 2]).max()))
                pos = (m.x, m.y, m.z)
    werte["verletzt_mm"] = round(tief, 3)
    werte["eilgang_durch_material"] = len(ep.pruefen(erg, aussen_frei=(0, 0, MATERIAL[0], MATERIAL[1])))
    return {k: (round(v, 3) if isinstance(v, float) else v) for k, v in werte.items()}


@mit_ocl
@pytest.mark.parametrize("name", sorted(AUFGABEN))
def test_messwerte_3d(name, tmp_path):
    jetzt = _messen(name, tmp_path)
    assert jetzt["eilgang_durch_material"] == 0, jetzt
    assert jetzt["verletzt_mm"] <= 0.1, jetzt               # Raster 0,2 mm: Kanten zählen nicht genau
    alt = _erwartet()
    if NEU or name not in alt:
        alt[name] = jetzt
        with open(DATEI, "w", encoding="utf-8") as f:
            json.dump(alt, f, ensure_ascii=False, indent=1, sort_keys=True)
            f.write("\n")
        if not NEU:
            pytest.fail(f"{name}: keine Messwerte – neu geschrieben, bitte prüfen: {jetzt}")
        return
    soll = alt[name]
    fehler = []
    for k in ("zeit_min", "leerweg", "senkrecht_mm"):
        if jetzt[k] > soll[k] * 1.01 + 0.01:
            fehler.append(f"{k} {jetzt[k]} statt {soll[k]}")
    if jetzt["eintauchen"] > soll["eintauchen"]:
        fehler.append(f"{jetzt['eintauchen']}× eingetaucht statt {soll['eintauchen']}×")
    if jetzt["rest_ueber_05_prozent"] > soll["rest_ueber_05_prozent"] + 0.3:
        fehler.append(f"Rest > 0,5 mm auf {jetzt['rest_ueber_05_prozent']} % statt {soll['rest_ueber_05_prozent']} %")
    assert not fehler, f"{name}: " + "; ".join(fehler)
