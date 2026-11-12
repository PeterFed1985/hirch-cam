"""«Tiefe gemessen ab: bis auf das 3D-Modell» (Peter 01.11., Servoschacht im Flügel): Tasche/Kontur als EINE 2D-Ebene,
mit dem ganzen Fräser (Schaft, Kugel, V, konisch) auf das Modell gesetzt, Ebenen darüber im Abstand der Zustellung.
Mit Aufmass (auch negativ) und «tiefe_ab_mm» (+ tiefer, − weniger tief). Prüfstein: nirgends tiefer als der Fräser
(0,02 mm kleiner, wie test_messwerte_3d) auf dem Modell + Aufmass − Versatz erlaubt; kein Eilgang ins Material;
die Tiefe der Bearbeitung ist die Grenze. Zeichnung: tests/daten/fluegel_fertig.hir (Peters Flügel, Schacht «tasche1»
auf Seite 2)."""
import json
import math
import os
import re
import sys

import numpy as np
import pytest

pytest.importorskip("hirch_cam.hirch_relief", reason="hirch_relief nicht gebaut")
inkex = pytest.importorskip("inkex")
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "werkzeuge"))
import eilgang_pruefen as ep  # noqa: E402

from hirch_cam import dokument, einstellungen as einst, svg_lesen  # noqa: E402
from hirch_cam.ops import relief_projektion as rp  # noqa: E402
from hirch_cam.ops.relief3d import _abtaster, _modelle  # noqa: E402

FLUEGEL = os.path.join(os.path.dirname(__file__), "daten", "fluegel_fertig.hir")
FARBE = {"tasche": "#00a000", "kontur": "#0050ff"}


def _rechnen(tmp_path, slot, art, tiefe=40.0, versatz=0.0, aufmass=0.0, strategie="runden", start=0.0):
    from lxml import etree
    svg = dokument._laden(FLUEGEL)
    e = einst.laden(svg.get(einst.ATTRIBUT))
    for b in e["bearbeitungen"]:
        b["aktiv"] = False
    e["bearbeitungen"].append({"id": "t9", "art": art, "name": "Servoschacht", "werkzeug": slot, "farbe": FARBE[art],
                               "aktiv": True, "auf_seite": 2, "tiefe": tiefe, "zustellung": 4.0,
                               "tiefe_ab": "bis_modell", "tiefe_ab_mm": versatz, "aufmass": aufmass,
                               "strategie": strategie, "seite": "innen", "starttiefe": start})
    e["zweiseitig"]["passloecher"] = False
    svg.set(einst.ATTRIBUT, json.dumps(e, ensure_ascii=False))
    for el in svg.iter("{http://www.w3.org/2000/svg}path"):
        if el.get("id") == "tasche1":
            el.set("style", "fill:none;stroke:%s;stroke-width:0.5" % FARBE[art])
        elif el.get("id") == "tasche2":
            el.set("style", "fill:none;stroke:#000000;stroke-width:0.5")
            el.set("{https://hirch.net/cnc}auch", "")
    pfad = str(tmp_path / f"f_{slot}_{art}_{tiefe}_{start}.hir")
    with open(pfad, "wb") as f:
        f.write(etree.tostring(svg.getroottree()))
    return pfad, e, dokument.rechnen(pfad, str(tmp_path / f"aus_{slot}_{art}_{tiefe}_{start}"))


def _zu_tief(pfad, e, erg, slot, versatz, aufmass):
    """Grösste Stelle, an der ein Vorschub tiefer liegt als der Fräser (0,02 kleiner) auf dem Modell erlaubt."""
    prog = next(p for p in erg["programme"] if any(a["key"].startswith("t9") for a in p["abschnitte"]))
    x = y = 0.0
    z, eil, P = 50.0, True, []
    for zeile in open(prog["datei"]).read().upper().splitlines():
        w = dict(re.findall(r"([GXYZ])(-?[0-9.]+)", zeile))
        if "G" in w:
            eil = float(w["G"]) == 0
        nx, ny, nz = float(w.get("X", x)), float(w.get("Y", y)), float(w.get("Z", z))
        if not eil and nz < 0:
            n = max(1, int(math.hypot(nx - x, ny - y) / 0.5))
            for t in np.linspace(0, 1, n + 1)[1:]:
                P.append((x + (nx - x) * t, y + (ny - y) * t, z + (nz - z) * t))
        x, y, z = nx, ny, nz
    P = np.array(P)
    info = svg_lesen.lesen(inkex.load_svg(pfad).getroot(), zweiseitig=True)
    a = {"reliefs": info["reliefs"], "zweiseitig": e["zweiseitig"], "material_flaeche": [info["breite"], info["hoehe"]],
         "hoehen": {"materialdicke": info["dicke"]}}
    tri, extra, _g = _modelle(a, {}, 2, (info["breite"], info["hoehe"], info["dicke"]), 0.0, [])
    alle = np.concatenate([tri] + ([extra] if extra is not None else []))
    w = dict(e["werkzeuge"][slot])
    w["durchmesser"] = float(w["durchmesser"]) - 0.02
    if w.get("typ") == "kugel":
        w["eckradius"] = w["durchmesser"] / 2
    ab = _abtaster(alle, w, aufmass, -80)
    # G-Code-Koordinaten → Material (Nullpunkt der Seite): über die Mitte des Schachts (Linie x 391…404, y 27,6…111,6)
    off = np.array([397.47 - (P[:, 0].min() + P[:, 0].max()) / 2, 69.6 - (P[:, 1].min() + P[:, 1].max()) / 2])
    zm = ab(np.column_stack([P[:, 0] + off[0], P[:, 1] + off[1]])) - versatz
    return float((zm - P[:, 2]).max()), float(P[:, 2].min())


@pytest.mark.parametrize("slot,art,versatz,aufmass", [
    ("C", "tasche", 0.0, 0.0),        # Schaft Ø 6
    ("F", "tasche", 0.0, 0.0),        # Kugel R3
    ("A", "tasche", 0.0, 0.0),        # V 90°
    ("N", "tasche", 0.0, 0.0),        # konisch
    ("C", "tasche", 1.0, 0.0),        # 1 mm tiefer als das Modell
    ("C", "tasche", -2.0, 0.0),       # 2 mm weniger tief
    ("C", "tasche", 0.0, -0.3),       # negatives Aufmass
    ("F", "kontur", 0.0, 0.5),
    ("A", "kontur", 0.0, 0.0),
])
def test_nie_tiefer_als_das_modell_erlaubt(tmp_path, slot, art, versatz, aufmass):
    pfad, e, erg = _rechnen(tmp_path, slot, art, versatz=versatz, aufmass=aufmass)
    assert erg["ok"] and ep.pruefen(erg) == []
    zu_tief, tiefster = _zu_tief(pfad, e, erg, slot, versatz, aufmass)
    assert zu_tief <= 0.015, zu_tief
    assert tiefster < -30 - versatz + aufmass       # bis in den Schacht (Boden um Z −32,3)


def test_tiefe_ist_die_grenze(tmp_path):
    _pfad, _e, erg = _rechnen(tmp_path, "C", "tasche", tiefe=25.0)
    prog = next(p for p in erg["programme"] if any(a["key"].startswith("t9") for a in p["abschnitte"]))
    zs = [float(z) for z in re.findall(r"Z(-?[0-9.]+)", open(prog["datei"]).read())]
    assert min(zs) == pytest.approx(-25.0, abs=1e-3)
    assert any(t.startswith("Bis auf das 3D-Modell: an manchen Stellen begrenzt") for t in erg["op_warnungen"]["t9"])


def test_ohne_modell_ausgelassen():
    from hirch_cam.model import Move, Toolpath
    tp = Toolpath()
    tp.moves = [Move(True, 0, 0, 3), Move(False, 0, 0, -2, 300), Move(False, 10, 0, -2, 900)]
    w = rp.bis_modell(tp, {"reliefs": [], "material_flaeche": [100, 100], "hoehen": {"materialdicke": 20}}, 1,
                      {"typ": "schaft", "durchmesser": 6}, 2.0, 20.0, 0.0, 0.0, 0.5, 3.0)
    assert w == [rp.BIS_KEIN_MODELL] and tp.moves == []


def _z_vorschub(erg):
    prog = next(p for p in erg["programme"] if any(a["key"].startswith("t9") for a in p["abschnitte"]))
    return [float(z) for z in re.findall(r"Z(-?[0-9.]+)", open(prog["datei"]).read())]


def test_tiefe_0_heisst_bis_aufs_modell(tmp_path):
    """Peter 03.11.: mit Tiefe 0 tauchte der Fräser gar nicht ein – 0 heisst jetzt «ohne Grenze»."""
    _p, _e, erg = _rechnen(tmp_path, "C", "tasche", tiefe=0.0)
    assert erg["ok"] and min(_z_vorschub(erg)) < -30


def test_beginnt_bei_tiefe_laesst_obere_ebenen_aus(tmp_path):
    """Peter 03.11.: «Beginnt bei Tiefe», wenn darüber schon geschlichtet ist – die oberen Ebenen fallen weg."""
    _p, _e, ganz = _rechnen(tmp_path, "C", "tasche")
    _p, _e, ab20 = _rechnen(tmp_path, "C", "tasche", start=20.0)
    zg, z20 = _z_vorschub(ganz), _z_vorschub(ab20)
    assert min(z20) == pytest.approx(min(zg), abs=1e-3)       # gleich tief
    assert len(z20) < 0.8 * len(zg)                            # weniger Ebenen
