"""Sicherheitsprüfung der 3D-Bahnen (planung 04.11. 20:05, Peter: Sicherheit vor Kürze – er fräst echte Teile).

Geprüft wird:
- **Flügel**, so wie Peter ihn am 04.11. gefräst hat (`tests/daten/fluegel_gefraest.hir`, zweiseitig mit Haut):
  kein Eilgang durch stehendes Material (feiner Prüfer), beide Seiten zusammen nirgends tiefer als das Modell
  (Übergang Seite 1 → 2 stimmt), und die 3D-Bearbeitungen allein lassen rundherum die Haut stehen.
- **Linse** zweiseitig mit Haut und zwei Haltestegen: Stege und Haut bleiben in voller Dicke stehen.
- **Linse** zweiseitig mit einer Pratze auf Seite 2 und «Umspannen»: beide Spannungen tragen zusammen gleich viel ab
  wie ohne Pratze, in Spannung 1 nichts in der Zone, kein Eilgang ins Material.
"""
import json
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
from test_relief3d import ZWEI, linsen_zeichnung, mit_ocl  # noqa: E402

HIER = os.path.dirname(os.path.abspath(__file__))
FLUEGEL = os.path.join(HIER, "daten", "fluegel_gefraest.hir")

S = {"art": "relief_schruppen", "werkzeug": "C", "zustellung": 3, "aufmass": 0.5}
F = {"art": "relief_schlichten", "werkzeug": "K", "bahnabstand": 20, "strategie": "konturen",
     "auslassen_fertig": True}


def _beidseits():
    return [dict(S, id="s1", auf_seite=1), dict(F, id="f1", auf_seite=1),
            dict(S, id="s2", auf_seite=2), dict(F, id="f2", auf_seite=2)]


def _datei_mit(quelle, ziel, aendern):
    """Kopie einer Zeichnung mit geänderten Einstellungen."""
    import inkex
    from hirch_cam import einstellungen as E
    doc = inkex.load_svg(quelle)
    svg = doc.getroot()
    e = E.laden(svg.get(E.ATTRIBUT))
    aendern(e)
    svg.set(E.ATTRIBUT, json.dumps(e, ensure_ascii=False))
    doc.write(str(ziel))
    return str(ziel)


def _flaechen(svg_pfad, erg, genau="fein"):
    """Oberseite (Seite 1) und Unterseite (Seite 2, gewendet) des gefrästen Teils im Raster von Seite 1, dazu das
    Modell, der Abstand zum Modell und die Einstellungen der Zweiseitigkeit."""
    import inkex
    from scipy import ndimage
    from hirch_cam import einstellungen as E
    from hirch_cam.ops import relief_ergebnis as RE
    from hirch_cam.svg_lesen import lesen
    svg = inkex.load_svg(svg_pfad).getroot()
    e = E.laden(svg.get(E.ATTRIBUT))
    info = lesen(svg, zweiseitig=True)
    B, H, D = info["breite"], info["hoehe"], info["dicke"]
    zelle, nx, ny = RE.raster(B, H, genau)
    pr = erg["programme"]
    oben = RE.abtragen_seite(pr, 1, 0.0, 0.0, zelle, nx, ny, D)
    achse = (e.get("zweiseitig") or {}).get("achse", "x")
    if achse == "y":
        h2 = RE.abtragen_seite(pr, 2, B - (nx - 1) * zelle, 0.0, zelle, nx, ny, D)[:, ::-1]
    else:
        h2 = RE.abtragen_seite(pr, 2, 0.0, H - (ny - 1) * zelle, zelle, nx, ny, D)[::-1, :]
    unten = -D - h2
    m_o, _m_u = RE.modelle_oben_unten(info["reliefs"], B, H, D, zelle, nx, ny, achse)
    modell = np.isfinite(m_o)
    abstand = ndimage.distance_transform_edt(~modell) * zelle
    return {"oben": oben, "unten": unten, "dicke": oben - unten, "modell": modell, "abstand": abstand,
            "zelle": zelle, "info": info}


def _haut(info):
    from hirch_cam.ops import relief_zweiseitig as zs
    return zs.haut(zs.einstellungen(info["reliefs"][0]["info"]))


@mit_ocl
def test_fluegel_sicher(tmp_path, monkeypatch):
    from hirch_cam import dokument
    from hirch_cam.ops import relief_ergebnis as RE
    monkeypatch.setenv("HIRCH_3D_SPEICHER", "aus")
    erg = dokument.rechnen(FLUEGEL, str(tmp_path / "alles"), "f")
    assert erg["ok"]
    assert {int(p["seite"]) for p in erg["programme"]} == {1, 2}
    funde = ep.pruefen(erg)
    assert funde == [], [(f["programm"], [round(v, 2) for v in f["ort"]], round(f["tief"], 2)) for f in funde[:5]]
    assert not [x for x in erg.get("pruefung") or [] if "Eilgang" in x["text"]]
    # beide Seiten zusammen: nirgends tiefer als das Modell (Seite 2 sitzt richtig auf Seite 1)
    import inkex
    from hirch_cam import einstellungen as E
    from hirch_cam.svg_lesen import lesen
    svg = inkex.load_svg(FLUEGEL).getroot()
    e = E.laden(svg.get(E.ATTRIBUT))
    info = lesen(svg, zweiseitig=True)
    r = RE.ergebnis(erg["programme"], (info["breite"], info["hoehe"], info["dicke"]), info["reliefs"],
                    dict(e.get("zweiseitig") or {}), str(tmp_path / "erg"), "mittel")
    assert r["zu_tief"] == 0.0 and r["anteil_zu_tief"] == 0.0, r

    # nur die 3D-Bearbeitungen (die 2D-Taschen und das Durchtrennen schneiden absichtlich durch): die Haut bleibt
    def nur_3d(e):
        for b in e["bearbeitungen"]:
            if not b["art"].startswith("relief"):
                b["aktiv"] = False
        e["zweiseitig"]["passloecher"] = False
    svg3 = _datei_mit(FLUEGEL, tmp_path / "nur3d.hir", nur_3d)
    erg3 = dokument.rechnen(svg3, str(tmp_path / "nur3d"), "n")
    assert erg3["ok"]
    f = _flaechen(svg3, erg3)
    haut = _haut(f["info"])
    assert haut == pytest.approx(0.5)
    zone = (f["abstand"] > 2 * f["zelle"]) & (f["abstand"] < 40)
    assert zone.sum() > 1000
    assert float(f["dicke"][zone].min()) >= haut - 0.05, float(f["dicke"][zone].min())


@mit_ocl
def test_linse_stege_und_haut_bleiben(tmp_path, monkeypatch):
    from shapely.geometry import Polygon
    import shapely
    from hirch_cam import dokument, speicher
    monkeypatch.setenv("HIRCH_3D_SPEICHER", "aus")
    speicher.SPEICHER.leeren()
    svg, mat = linsen_zeichnung(tmp_path, bearbeitungen=_beidseits(), zweiseitig=ZWEI,
                                modell_zwei={"an": True, "haut_an": True, "haut": 0.5})
    erg = dokument.rechnen(svg, str(tmp_path / "aus"))
    assert erg["ok"], erg.get("warnungen")
    assert ep.pruefen(erg, aussen_frei=(0, 0, mat[0], mat[1])) == []
    f = _flaechen(svg, erg)
    z = f["zelle"]
    ny, nx = f["dicke"].shape
    X, Y = np.meshgrid(np.arange(nx) * z, np.arange(ny) * z)
    # Stege (Materialkoordinaten), um einen halben Millimeter eingerückt (Rand im Raster) und ohne das Modell
    stege = [Polygon(s["ecken"]).buffer(-0.5) for s in f["info"]["reliefs"][0]["stege"]]
    assert len(stege) == 2
    in_steg = np.zeros_like(f["modell"])
    for s in stege:
        in_steg |= shapely.contains_xy(s, X, Y)
    in_steg &= f["abstand"] > 2 * z
    assert in_steg.sum() > 100
    from hirch_cam.ops import relief_zweiseitig as zs
    steg_dicke = zs.einstellungen(f["info"]["reliefs"][0]["info"])["steg_dicke"]
    assert float(f["dicke"][in_steg].min()) >= steg_dicke - 0.05, float(f["dicke"][in_steg].min())
    # Haut rundherum (bis 12 mm vom Modell – so weit räumt das Schruppen um das Modell)
    haut = _haut(f["info"])
    assert haut == pytest.approx(0.5)
    zone = (f["abstand"] > 2 * z) & (f["abstand"] < 12) & ~in_steg
    assert float(f["dicke"][zone].min()) >= haut - 0.05, float(f["dicke"][zone].min())


@mit_ocl
def test_linse_zweiseitig_mit_pratze_auf_seite_2(tmp_path, monkeypatch):
    """Spannmittel «Seite 2» über dem Schruppbereich neben der Linse, Umspannen an: in Spannung 1 bleibt die Zone
    frei, Spannung 2 holt sie nach – zusammen gleich viel wie ohne Pratze."""
    from shapely.geometry import LineString, Point
    from hirch_cam import dokument, engine, speicher
    from hirch_cam.ops import umspannen as U
    from hirch_cam.post import tap_mm
    from hirch_cam.simulation import Hoehenfeld
    monkeypatch.setenv("HIRCH_3D_SPEICHER", "aus")
    echt = engine.rechnen
    pratze = {"punkte": [(42.0, 70.0), (58.0, 70.0), (58.0, 82.0), (42.0, 82.0)], "hoehe": 12, "seite": 2}

    def rechnen(name, mit):
        def mit_pratze(a, *args, **kw):
            if mit:
                a["spannmittel"] = list(a.get("spannmittel") or []) + [dict(pratze)]
                a["umspannen"] = {"an": True, "abstand": 10}
            return echt(a, *args, **kw)
        monkeypatch.setattr(engine, "rechnen", mit_pratze)
        speicher.SPEICHER.leeren()
        t = tmp_path / name
        t.mkdir()
        svg, mat = linsen_zeichnung(t, stege=False, bearbeitungen=_beidseits(), zweiseitig=ZWEI)
        erg = dokument.rechnen(svg, str(t / "aus"))
        monkeypatch.setattr(engine, "rechnen", echt)
        assert erg["ok"], erg.get("warnungen")
        return erg, mat

    ohne, mat = rechnen("ohne", False)
    mit, _mat = rechnen("mit", True)
    assert {(int(p["seite"]), int(p["spannung"])) for p in mit["programme"]} == {(1, 1), (2, 1), (2, 2)}

    def feld(erg, seite):
        f = Hoehenfeld(-10, -10, mat[0] + 10, mat[1] + 10, 0.1)
        for p in erg["programme"]:
            if int(p["seite"]) != seite:
                continue
            with open(p["datei"], newline="") as fh:
                job = tap_mm.read(fh.read())
            form = p.get("form") or "flach"
            d = float(p["durchmesser"])
            f.fraesen(job, form, d, 60, eckradius=d / 2 if form == "kugel" else 0)
        return f.H

    from scipy import ndimage
    for seite in (1, 2):
        H0, H1 = feld(ohne, seite), feld(mit, seite)
        rand = (ndimage.maximum_filter(H0, 3) - ndimage.minimum_filter(H0, 3)) > 0.05
        assert int(((np.abs(H1 - H0) > 0.05) & ~rand).sum()) == 0, seite
    assert ep.pruefen(mit, aussen_frei=(0, 0, mat[0], mat[1])) == []
    # 05.11.: der Hinweis «Spannung 2: … auf {} mm» (geschätzte Höhe) ist gewollt
    assert not [x for x in mit.get("pruefung") or [] if not x["text"].startswith("Spannung 2:") and ("Spannmittel" in x["text"] or "Eilgang" in x["text"])]
    # Spannung 1 auf Seite 2: kein Fräsweg unter Z 0 in der Zone der Pratze
    a = {"spannmittel": [pratze]}
    for p in mit["programme"]:
        if int(p["seite"]) != 2 or int(p["spannung"]) != 1:
            continue
        zone = U.zone(U.spannmittel(a, 2, 1), float(p["durchmesser"]) / 2, 10.0).buffer(-0.01)
        with open(p["datei"], newline="") as fh:
            job = tap_mm.read(fh.read())
        pos = (job.home[0], job.home[1], job.safe_z)
        for tp in job.toolpaths:
            for m in tp.moves:
                if not m.rapid and min(pos[2], m.z) < 0:
                    g = LineString([pos[:2], (m.x, m.y)]) if math.dist(pos[:2], (m.x, m.y)) > 1e-9 \
                        else Point(m.x, m.y)
                    assert not g.intersects(zone), (p["name"], pos, (m.x, m.y, m.z))
                pos = (m.x, m.y, m.z)


def kiesel_zeichnung(ordner, B=200.0, H=150.0, D=20.0):
    """Nur der 3D-Teil der Musterdatei (tests/daten/muster_3d.py) auf dem Material der Musterdatei."""
    import html
    sys.path.insert(0, os.path.join(HIER, "daten"))
    from muster_3d import teil
    t = teil(144, 38, (B, H, D))
    w = {"C": {"typ": "schaft", "durchmesser": 6.0, "drehzahl": 18000, "vorschub": 1000, "eintauchen": 300,
               "nummer": 3, "name": "Schaftfräser 6 mm", "schneidenlaenge": 22.0, "schneiden": 2,
               "mittenschnitt": True}}
    w.update(t["werkzeuge"])
    e = {"eingerichtet": True, "werkzeuge": w, "bearbeitungen": t["bearbeitungen"], "material": "Hartholz",
         "reihenfolge": "auto",
         "zweiseitig": {"an": True, "achse": "x", "passloecher": True, "pass_d": 6.0, "pass_rand": 8.0,
                        "pass_werkzeug": "C", "pass_extra": 1.5, "pass_lage": "ecken"}}
    s2 = B + 20
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
           'xmlns:hirch="https://hirch.net/cnc" xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
           'xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd" width="%gmm" height="%gmm" '
           'viewBox="0 0 %g %g" hirch:materialdicke="%g" hirch:einstellungen="%s"><sodipodi:namedview id="nv" '
           'inkscape:document-units="mm"><inkscape:page id="p1" x="0" y="0" width="%g" height="%g"/>'
           '<inkscape:page id="p2" x="%g" y="0" width="%g" height="%g"/></sodipodi:namedview><defs>%s</defs>'
           '<g inkscape:groupmode="layer" inkscape:label="Muster">%s%s</g></svg>'
           % (s2 + B, H, s2 + B, H, D, html.escape(json.dumps(e, ensure_ascii=False), quote=True), B, H, s2, B, H,
              t["defs"], t["bild"], t["stege"]))
    pfad = os.path.join(str(ordner), "kiesel.hir")
    with open(pfad, "w", encoding="utf-8") as f:
        f.write(svg)
    return pfad, (B, H, D)


@mit_ocl
def test_kiesel_der_musterdatei_sicher(tmp_path, monkeypatch):
    """Der 3D-Teil der Musterdatei: jede 3D-Art (Schruppen, «Böden und Wände» im Wechsel, Schlichten beider Seiten),
    Haut und Haltestege – keine Warnung, kein Eilgang ins Material, Haut und Stege bleiben stehen."""
    from shapely.geometry import Polygon
    import shapely
    from hirch_cam import dokument
    monkeypatch.setenv("HIRCH_3D_SPEICHER", "aus")
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "konfig"))
    svg, mat = kiesel_zeichnung(tmp_path)
    erg = dokument.rechnen(svg, str(tmp_path / "aus"), "k")
    assert erg["ok"], erg.get("warnungen")
    assert not erg.get("pruefung"), erg.get("pruefung")
    arten = {a["key"].split("#")[0] for p in erg["programme"] for a in p["abschnitte"]}
    assert {"s3a", "bw", "f3a", "s3b", "f3b"} <= arten
    assert ep.pruefen(erg, aussen_frei=(0, 0, mat[0], mat[1])) == []
    f = _flaechen(svg, erg)
    z = f["zelle"]
    ny, nx = f["dicke"].shape
    X, Y = np.meshgrid(np.arange(nx) * z, np.arange(ny) * z)
    in_steg = np.zeros_like(f["modell"])
    for s in f["info"]["reliefs"][0]["stege"]:
        in_steg |= shapely.contains_xy(Polygon(s["ecken"]).buffer(-0.5), X, Y)
    in_steg &= f["abstand"] > 2 * z
    assert in_steg.sum() > 100
    assert float(f["dicke"][in_steg].min()) >= 3.0 - 0.05, float(f["dicke"][in_steg].min())
    zone = (f["abstand"] > 2 * z) & (f["abstand"] < 8) & ~in_steg
    assert float(f["dicke"][zone].min()) >= 0.5 - 0.05, float(f["dicke"][zone].min())
