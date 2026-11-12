"""3D-Fräsung Etappe 2: Schruppen und Schlichten (ops/relief3d.py, Kern hirch_relief)."""
import base64
import json
import math
import os
import subprocess
import sys
from xml.sax.saxutils import quoteattr

import numpy as np
import pytest

hirch_relief = pytest.importorskip("hirch_cam.hirch_relief", reason="Modul hirch_relief nicht "
                                   "gebaut (relief_kern/bauen.sh)")
pytest.importorskip("inkex")

from hirch_cam import relief, relief_import  # noqa: E402
from hirch_cam.ops import relief3d  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FLUEGEL_STEP = os.path.join(REPO, "tests", "3d", "Fluegel_Test.step")
mit_step = pytest.mark.skipif(not relief_import.step_moeglich(), reason="ohne OpenCASCADE gebaut")
mit_ocl = pytest.mark.skipif(not hirch_relief.mit_ocl(), reason="ohne OpenCAMLib gebaut")

KUGEL = {"typ": "kugel", "durchmesser": 6.0, "eckradius": 3.0, "laenge": 50}
SCHAFT = {"typ": "schaft", "durchmesser": 6.0, "laenge": 50}


def kuppel(R=40.0, h=12.0, halb=30.0, n=61, boden=-4.0):
    """Kugelkappe über einem Quadrat (Oberseite und Boden als Gitter), Mitte im Ursprung."""
    g = np.linspace(-halb, halb, n)
    X, Y = np.meshgrid(g, g)
    Z = np.maximum(0.0, np.sqrt(np.maximum(0.0, R * R - X * X - Y * Y)) - (R - h))
    P = np.dstack([X, Y, Z])
    B = np.dstack([X, Y, np.full_like(X, boden)])
    tri = []
    for i in range(n - 1):
        for j in range(n - 1):
            a, b, c, d = P[i, j], P[i, j + 1], P[i + 1, j + 1], P[i + 1, j]
            tri += [(a, b, c), (a, c, d)]
            a, b, c, d = B[i, j], B[i, j + 1], B[i + 1, j + 1], B[i + 1, j]
            tri += [(a, c, b), (a, d, c)]
    return np.array(tri, np.float32)


def kuppel_hoehe(x, y, R=40.0, h=12.0):
    return max(0.0, (R * R - x * x - y * y) ** 0.5 - (R - h)) if x * x + y * y < R * R else 0.0


def stl_schreiben(pfad, d):
    with open(pfad, "wb") as f:
        f.write(b"kuppel".ljust(80, b" "))
        f.write(np.uint32(len(d)).tobytes())
        a = np.zeros(len(d), dtype=[("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")])
        a["v"] = d
        f.write(a.tobytes())


def zeichnung(tmp_path, bearbeitungen, material=(100, 90, 25), mit_modell=True, dreiecke=None,
              lage=None, zweiseitig=None, zusatz_info=None, elemente="", modell_datei=None, rund=None):
    """Zeichnung wie hirch-3d.cpp sie schreibt: Bild mit hirch:relief, Daten in <defs>."""
    B, H, D = material
    lage = lage or {"art": "oben"}
    e = {"eingerichtet": True, "material": "hartschaum", "reihenfolge": "liste",
         "werkzeuge": {"C": dict(SCHAFT, nummer=1, auto=True),
                       "K": dict(KUGEL, nummer=2, auto=True)},
         "bearbeitungen": bearbeitungen}
    if zweiseitig:
        e["zweiseitig"] = zweiseitig
    if rund:
        e["rund"] = rund
    kopf = (f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
            f'xmlns:hirch="https://hirch.net/cnc" '
            f'xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" width="{B}mm" '
            f'height="{H}mm" viewBox="0 0 {B} {H}" hirch:materialdicke="{D}" '
            f'hirch:einstellungen={quoteattr(json.dumps(e))}>')
    if not mit_modell:
        svg = kopf + '<path d="M1,1 L2,2" style="stroke:#000"/></svg>'
    else:
        stl = tmp_path / "kuppel.stl"
        if modell_datei:
            stl = modell_datei
        else:
            stl_schreiben(stl, kuppel() if dreiecke is None else dreiecke)
        ordner = tmp_path / "einbetten"
        ordner.mkdir(exist_ok=True)
        erg = relief.einbetten(str(stl), str(ordner), material=list(material), lage=lage)
        daten = open(erg["dateien"]["daten"]).read()
        png = base64.b64encode(open(erg["dateien"]["bild"], "rb").read()).decode()
        bl = erg["bild_lage"]["mm"]
        info = {"daten": "rd1", "name": erg["dateien"]["name"], "art": erg["art"],
                "ausrichtung": erg["ausrichtung"], "lage": lage, "bild_lage": bl,
                "masse": erg["masse"]}
        info.update(zusatz_info or {})
        w, h = bl[2], bl[3]
        x, y = (B - w) / 2, (H - h) / 2
        svg = (kopf + f'<defs><hirch:reliefdaten id="rd1" hirch:name="{info["name"]}">{daten}'
               f'</hirch:reliefdaten></defs><g inkscape:groupmode="layer" id="ebene1">'
               f'<image id="relief1" x="{x}" y="{y}" width="{w}" height="{h}" '
               f'preserveAspectRatio="none" xlink:href="data:image/png;base64,{png}" '
               f'hirch:relief={quoteattr(json.dumps(info))}/>{elemente}</g></svg>')
    pfad = tmp_path / "kuppel.svg"
    pfad.write_text(svg, encoding="utf-8")
    return str(pfad)


def auftrag_aus(svg_pfad, zweiseitig=None):
    import inkex
    from hirch_cam import svg_lesen
    info = svg_lesen.lesen(inkex.load_svg(svg_pfad).getroot(), zweiseitig=bool(zweiseitig))
    return info, {"reliefs": info["reliefs"], "material_flaeche": [info["breite"], info["hoehe"]],
                  "hoehen": {"materialdicke": info["dicke"]}, "zweiseitig": zweiseitig or {}}


def punkte_der_bahn(tp, schritt=0.2, unter=0.5):
    """Alle Bewegungen unter z=unter, fein unterteilt: (xy, z)."""
    xy, zz, prev = [], [], None
    for m in tp.moves:
        q = np.array([m.x, m.y, m.z])
        if prev is not None and min(prev[2], q[2]) < unter:
            n = max(2, int(np.hypot(*(q - prev)[:2]) / schritt) + 1)
            p = prev + (q - prev) * np.linspace(0, 1, n)[:, None]
            xy.append(p[:, :2])
            zz.append(p[:, 2])
        prev = q
    return np.vstack(xy), np.concatenate(zz)


def abheben(tp):
    """Wie oft der Fräser senkrecht ausfährt."""
    n, vorher = 0, None
    for m in tp.moves:
        if vorher is not None and m.rapid and m.z > vorher.z + 1e-6 and \
                abs(m.x - vorher.x) < 1e-9 and abs(m.y - vorher.y) < 1e-9:
            n += 1
        vorher = m
    return n


# --------------------------------------------------------------------------------------------
def test_konturen_kreis():
    n, c = 101, 0.5
    g = (np.arange(n) - 50) * c
    X, Y = np.meshgrid(g, g)
    r = np.hypot(X, Y)
    raster = np.where(r < 20, 1.0, 0.0).astype(np.float32)
    raster[np.hypot(X - 5, Y) < 4] = 0.0                       # Loch
    ringe = hirch_relief.konturen(raster, 0.5, -25.0, -25.0, c)
    assert len(ringe) == 2

    def flaeche(p):
        p = np.asarray(p)
        return 0.5 * float(np.sum(p[:-1, 0] * p[1:, 1] - p[1:, 0] * p[:-1, 1]) +
                           p[-1, 0] * p[0, 1] - p[0, 0] * p[-1, 1])
    a = sorted((flaeche(p) for p in ringe), key=abs)
    assert a[1] == pytest.approx(np.pi * 20 ** 2, rel=0.02)    # aussen gegen den Uhrzeigersinn
    assert a[0] == pytest.approx(-np.pi * 4 ** 2, rel=0.1)     # Loch im Uhrzeigersinn


@mit_ocl
def test_abtasten_kugel_und_schaft():
    tri = kuppel()
    pts = np.array([[0.0, 0.0], [10.0, 5.0], [29.0, 0.0]])
    z = hirch_relief.abtasten(tri, KUGEL, pts, -50.0)
    assert z[0] == pytest.approx(12.0, abs=0.02)               # Scheitel
    # Kugel auf Kugelfläche: Mittelpunkt auf Radius R+3 → Spitze etwas über der Fläche darunter
    assert z[1] > kuppel_hoehe(10, 5)
    assert z[1] == pytest.approx((43 ** 2 - 125) ** 0.5 - 3 - 28, abs=0.05)
    zs = hirch_relief.abtasten(tri, SCHAFT, pts, -50.0)
    assert zs[2] == pytest.approx(kuppel_hoehe(26, 0), abs=0.05)   # Schaft trifft mit dem Rand


def test_vereinfachen_steile_wand():
    """An steilen Wänden zählt der Höhenfehler, nicht der (kleine) 3D-Abstand."""
    x = np.linspace(0, 0.2, 21)
    z = np.where(x < 0.1, 0.0, -6.0)
    z[10] = -0.2                                               # Knick mitten in der Wand
    z = np.minimum(z, -60 * (x - 0.1))
    pts = np.column_stack([x, np.zeros_like(x), z])
    aus = relief3d._vereinfachen(pts, 0.01)
    zi = np.interp(x, aus[:, 0], aus[:, 2])
    assert np.max(z - zi) <= 0.01 + 1e-9


def test_ohne_modell_warnung(tmp_path):
    from hirch_cam import dokument
    svg = zeichnung(tmp_path, [{"id": "s1", "art": "relief_schruppen", "werkzeug": "C"}],
                    mit_modell=False)
    erg = dokument.rechnen(svg, str(tmp_path / "aus"))
    assert not erg["ok"] or not erg["dateien"]
    assert any("Kein 3D-Modell" in w for w in erg["warnungen"]), erg["warnungen"]


@mit_ocl
def test_schruppen_und_schlichten_kuppel(tmp_path):
    svg = zeichnung(tmp_path, [])
    info, auftrag = auftrag_aus(svg)
    assert len(info["reliefs"]) == 1
    tri, _v = relief3d.modell_im_material(info["reliefs"][0], info["dicke"])
    assert tri[..., 2].max() == pytest.approx(0.0, abs=0.05)   # Lage «oben»: Scheitel auf Z0
    w = {"vorschub": 2000, "eintauchen": 500}
    # Aufmass überall (ohne «Böden auf Mass»: der Sockel der Kuppel ist eine flache Stelle)
    tpS, warnS = relief3d.bearbeiten("relief_schruppen", {"zustellung": 4, "aufmass": 0.5, "boden_fertig": False},
                                     dict(SCHAFT, **w), auftrag, 1, 3, 0.5, (0, 0, 20), "S", 0.01)
    tpK, warnK = relief3d.bearbeiten("relief_schlichten", {"bahnabstand": 15},
                                     dict(KUGEL, **w), auftrag, 1, 3, 0.5, (0, 0, 20), "K", 0.01)
    assert not warnS and not warnK, (warnS, warnK)
    assert len(tpS.moves) > 50 and len(tpK.moves) > 200
    # Schruppen: nie näher als das Aufmass ans Modell (Schaftfräser, Toleranz der Bahnen)
    xy, z = punkte_der_bahn(tpS)
    soll = hirch_relief.abtasten(tri, SCHAFT, xy, -100.0)
    assert np.min(z - soll) >= 0.5 - 0.05
    assert z.min() < -8                                        # bis zum Fuss der Kuppel
    # Schlichten: folgt der Oberfläche, ohne hineinzuschneiden
    xy, z = punkte_der_bahn(tpK)
    soll = hirch_relief.abtasten(tri, KUGEL, xy, -100.0)
    assert np.max(soll - z) <= 0.05
    innen = np.hypot(xy[:, 0] - 50, xy[:, 1] - 45) < 20
    assert np.max(np.abs(soll - z)[innen]) <= 0.05
    # am runden Rand von Bahn zu Bahn direkt weiter, nicht jedes Mal ausfahren und eintauchen
    assert abheben(tpK) <= 2, abheben(tpK)


@mit_ocl
def test_rechnen_ganzer_ablauf(tmp_path):
    from hirch_cam import dokument
    from hirch_cam.post import tap_mm
    svg = zeichnung(tmp_path, [
        {"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 4,
         "aufmass": 0.5, "farbe": "#5e5c64"},
        {"id": "f1", "art": "relief_schlichten", "werkzeug": "K", "bahnabstand": 20,
         "farbe": "#9a96a0"}])
    erg = dokument.rechnen(svg, str(tmp_path / "aus"), "kuppel")
    assert erg["ok"], erg
    assert len(erg["programme"]) == 2
    assert len(erg["bearbeitungen"]) == 2
    jobs = [tap_mm.read(open(p["datei"], newline="").read()) for p in erg["programme"]]
    zmin = [min(m.z for tp in j.toolpaths for m in tp.moves) for j in jobs]
    assert -14 < zmin[0] < -8 and -14 < zmin[1] < -8


# --------------------------------------------------------------------------------------------
# Zweiseitig: Trennfläche, Haltestege, Seite 2

def linse(R=40.0, h=6.0, halb=30.0, n=61):
    """Geschlossene Linse: Kugelkappe oben und unten, am Rand messerscharf (Trennlinie Z = 0)."""
    g = np.linspace(-halb, halb, n)
    X, Y = np.meshgrid(g, g)
    r2 = X * X + Y * Y
    a2 = R * R - (R - h) ** 2                       # Radius der Kappe zum Quadrat
    Z = np.where(r2 < a2, np.sqrt(np.maximum(0.0, R * R - r2)) - (R - h), 0.0)
    tri = []
    for i in range(n - 1):
        for j in range(n - 1):
            ecken = [(i, j), (i, j + 1), (i + 1, j + 1), (i + 1, j)]
            if not all(r2[a, b] < a2 for a, b in ecken):
                continue
            P = [np.array([X[a, b], Y[a, b], Z[a, b]]) for a, b in ecken]
            Q = [np.array([X[a, b], Y[a, b], -Z[a, b]]) for a, b in ecken]
            tri += [(P[0], P[1], P[2]), (P[0], P[2], P[3]), (Q[0], Q[2], Q[1]), (Q[0], Q[3], Q[2])]
    return np.array(tri, np.float32)


ZWEI = {"an": True, "achse": "x", "passloecher": False}


def steg_rect(sid, mitte_mm, breite, laenge, winkel, H, z=None):
    """Haltesteg wie hirch-3d.cpp ihn setzt (Materialkoordinaten → Zeichnung, y nach unten)."""
    w = json.dumps({"relief": "relief1", "z": z})
    return (f'<rect id="{sid}" x="{-breite / 2}" y="{-laenge / 2}" width="{breite}" height="{laenge}" '
            f'transform="translate({mitte_mm[0]},{H - mitte_mm[1]}) rotate({-winkel})" '
            f'style="fill:#ffd21a;fill-opacity:0.6" hirch:steg3d={quoteattr(w)}/>')


def linsen_zeichnung(tmp_path, stege=True, zweiseitig=ZWEI, bearbeitungen=(), elemente="", modell_zwei=None):
    mat = (100, 90, 20)
    el = ""
    if stege:
        # zwei Stege links und rechts (Linse: Mitte 50/45, Radius ~21 mm)
        el = (steg_rect("st1", (50 - 21.4 - 10, 45), 8, 30, 90, mat[1]) +
              steg_rect("st2", (50 + 21.4 + 10, 45), 8, 30, 90, mat[1]))
    el += elemente
    return zeichnung(tmp_path, list(bearbeitungen), material=mat, dreiecke=linse(), lage={"art": "mittig"},
                     zweiseitig=zweiseitig, zusatz_info={"zweiseitig": modell_zwei or {"an": True}},
                     elemente=el), mat


def test_stege_lesen_und_nicht_fraesen(tmp_path):
    svg, mat = linsen_zeichnung(tmp_path)
    info, _a = auftrag_aus(svg, ZWEI)
    r = info["reliefs"][0]
    assert [s["id"] for s in r["stege"]] == ["st1", "st2"]
    e = np.array(r["stege"][0]["ecken"])
    assert e[:, 0].min() == pytest.approx(50 - 21.4 - 25, abs=0.01)     # Länge quer zum Umriss
    assert e[:, 1].max() - e[:, 1].min() == pytest.approx(8, abs=0.01)  # Breite entlang
    assert not info["elemente"] and not info["stege"]   # weder Fräsobjekt noch 2D-Steg


def test_stege_info_und_vorschlag(tmp_path):
    from hirch_cam.ops import relief_zweiseitig as zs
    svg, mat = linsen_zeichnung(tmp_path)
    info, _a = auftrag_aus(svg, ZWEI)
    r = info["reliefs"][0]
    erg = zs.stege_info(r, mat[2], anzahl=3, breite=8, laenge=30)
    assert erg["trennlinie"][0] == pytest.approx(-10, abs=0.3)      # mittig: Z −10
    assert not erg["schief"]
    assert [s["z_auto"] for s in erg["stege"]] == pytest.approx([-10, -10], abs=0.3)
    # Vorschlag ohne die vorhandenen Stege (die würden gemieden)
    erg = zs.stege_info(dict(r, stege=[]), mat[2], anzahl=3, breite=8, laenge=30)
    assert len(erg["vorschlag"]) == 3
    for v in erg["vorschlag"]:
        m = np.array(v["mitte"])
        # ragt 5 mm ins Modell, Mitte also 10 mm ausserhalb des Umrisses (Radius ~20…21)
        assert np.hypot(*(m - [50, 45])) == pytest.approx(20.5 + 10, abs=1.5)


def test_stege_befehl(tmp_path):
    svg, mat = linsen_zeichnung(tmp_path)
    info, _a = auftrag_aus(svg, ZWEI)
    r = info["reliefs"][0]
    daten = tmp_path / "daten.txt"
    daten.write_text(r["daten"])
    erg = subprocess.run([sys.executable, "-m", "hirch_cam.dokument", "relief", "stege", "--daten",
                          str(daten), "--info", json.dumps(r["info"]), "--ecken",
                          json.dumps([list(p) for p in r["ecken"]]), "--dicke", "20", "--stege",
                          json.dumps([{"id": s["id"], "ecken": [list(p) for p in s["ecken"]],
                                       "z": None} for s in r["stege"]]), "--anzahl", "2"],
                         capture_output=True, text=True, cwd=REPO)
    aus = json.loads(erg.stdout)
    assert aus["ok"], aus
    assert len(aus["stege"]) == 2 and len(aus["vorschlag"]) == 2


@mit_ocl
def test_zweiseitig_spalt_durch_und_stege_bleiben(tmp_path):
    from hirch_cam.model import Job
    from hirch_cam.simulation import Hoehenfeld
    from hirch_cam.ops import relief_zweiseitig as zs
    import shapely
    from shapely.geometry import Polygon
    svg, mat = linsen_zeichnung(tmp_path)
    info, auftrag = auftrag_aus(svg, ZWEI)
    B, H, D = mat
    w = {"vorschub": 2000, "eintauchen": 500}
    tri, _v = relief3d.modell_im_material(info["reliefs"][0], D)
    c = 0.25
    felder = {}
    for seite in (1, 2):
        tpS, wS = relief3d.bearbeiten("relief_schruppen", {"zustellung": 4, "aufmass": 0.5},
                                      dict(SCHAFT, **w), auftrag, seite, 3, 0.5, (0, 0, 20), "S", 0.01)
        tpK, wK = relief3d.bearbeiten("relief_schlichten", {"bahnabstand": 15},
                                      dict(KUGEL, **w), auftrag, seite, 3, 0.5, (0, 0, 20), "K", 0.01)
        assert not wS and not wK, (wS, wK)
        # Schlichten: nie ins Modell (gewendet auf Seite 2)
        mod = tri if seite == 1 else zs.wenden(tri, mat, "x")
        xy, z = punkte_der_bahn(tpK, unter=-0.5)
        assert np.max(hirch_relief.abtasten(mod, KUGEL, xy, -100.0) - z) <= 0.05
        xy, z = punkte_der_bahn(tpS, unter=-0.5)
        # Schruppen hält das Aufmass – nur die letzte Runde auf der Trennfläche fährt seit 02.11. ohne Aufmass
        # (Peter: sauber bis auf die Haut, keine Kehle neben dem Modell); auch sie nie ins Modell
        luft = z - hirch_relief.abtasten(mod, SCHAFT, xy, -100.0)
        assert np.min(luft) >= -0.05
        assert np.mean(luft >= 0.45) > 0.8
        hf = Hoehenfeld(0, 0, B, H, zelle=c)
        hf.fraesen(Job(toolpaths=[tpS], safe_z=20, home=(0, 0)), form="flach", durchmesser=6.0, eckradius=0)
        hf.fraesen(Job(toolpaths=[tpK], safe_z=20, home=(0, 0)), form="kugel", durchmesser=6.0, eckradius=3.0)
        felder[seite] = hf.H
    oben = felder[1]
    unten = -D - felder[2][::-1, :]                   # gewendet um X
    ny, nx = min(oben.shape[0], unten.shape[0]), min(oben.shape[1], unten.shape[1])
    dick = (oben[:ny, :nx] - unten[:ny, :nx])
    XX, YY = np.meshgrid(np.arange(nx) * c, np.arange(ny) * c)
    r = np.hypot(XX - 50, YY - 45)
    stege = np.zeros_like(r, bool)
    for s in info["reliefs"][0]["stege"]:
        stege |= shapely.contains_xy(Polygon(s["ecken"]).buffer(3.5), XX, YY)
    spalt = (r > 21.4 + 3.5) & (r < 21.4 + 7.5) & ~stege
    assert spalt.sum() > 500
    assert np.max(dick[spalt]) <= 0.01                 # rundum durch
    for s in info["reliefs"][0]["stege"]:
        im = shapely.contains_xy(Polygon(s["ecken"]).buffer(-1.0), XX, YY) & (r > 21.4 + 3.5) & (r < 21.4 + 7.5)
        assert np.median(dick[im]) == pytest.approx(3.0, abs=0.05)      # Steg 3 mm, um Z −10
        assert np.median(oben[:ny, :nx][im]) == pytest.approx(-8.5, abs=0.3)


@mit_ocl
def test_zweiseitig_hinweise(tmp_path):
    w = dict(SCHAFT, vorschub=2000, eintauchen=500)
    svg, mat = linsen_zeichnung(tmp_path, stege=False)
    _i, auftrag = auftrag_aus(svg, ZWEI)
    _tp, warn = relief3d.bearbeiten("relief_schruppen", {"zustellung": 4}, w, auftrag, 2, 3, 0.5,
                                    (0, 0, 20), "S", 0.01)
    assert any("Keine Haltestege" in t for t in warn), warn
    # Zweiseitig ausgeschaltet: nur von oben, mit Hinweis; Seite 2 hat nichts
    _i, auftrag = auftrag_aus(svg, None)
    _tp, warn = relief3d.bearbeiten("relief_schruppen", {"zustellung": 4}, w, auftrag, 1, 3, 0.5,
                                    (0, 0, 20), "S", 0.01)
    assert any("«Zweiseitig» ist ausgeschaltet" in t for t in warn), warn
    # Passloch im Spalt
    tmp2 = tmp_path / "b"
    tmp2.mkdir()
    zw = dict(ZWEI, passloecher=True, pass_rand=26.0)
    svg2, _m = linsen_zeichnung(tmp2, zweiseitig=zw)
    _i, auftrag = auftrag_aus(svg2, zw)
    _tp, warn = relief3d.bearbeiten("relief_schruppen", {"zustellung": 4}, w, auftrag, 1, 3, 0.5,
                                    (0, 0, 20), "S", 0.01)
    assert any("Passloch" in t for t in warn), warn


@mit_ocl
def test_zweiseitig_ganzer_ablauf(tmp_path):
    from hirch_cam import dokument
    svg, mat = linsen_zeichnung(tmp_path, bearbeitungen=[
        {"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 4, "farbe": "#5e5c64"},
        {"id": "f1", "art": "relief_schlichten", "werkzeug": "K", "bahnabstand": 20, "farbe": "#9a96a0"},
        {"id": "s2", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 4, "auf_seite": 2,
         "farbe": "#5e5c64"},
        {"id": "f2", "art": "relief_schlichten", "werkzeug": "K", "bahnabstand": 20, "auf_seite": 2,
         "farbe": "#9a96a0"}])
    erg = dokument.rechnen(svg, str(tmp_path / "aus"), "linse")
    assert erg["ok"], erg
    assert sorted(p.get("seite") for p in erg["programme"]) == [1, 1, 2, 2]
    assert not [w for w in erg["warnungen"] if "Steg" in w or "Kein" in w], erg["warnungen"]


# --------------------------------------------------------------------------------------------
# Vektoren aus dem Modell

def relief_aus(tmp_path, pfad, material, lage=None):
    """Modell wie in der Zeichnung: eingebettet, Bild mittig im Material."""
    B, H, D = material
    ordner = tmp_path / "vek"
    ordner.mkdir(exist_ok=True)
    erg = relief.einbetten(str(pfad), str(ordner), material=list(material), lage=lage or {"art": "mittig"})
    bl = erg["bild_lage"]["mm"]
    w, h = bl[2], bl[3]
    x, y = (B - w) / 2, (H - h) / 2              # Zeichnung: y nach unten
    info = {"name": erg["dateien"]["name"], "art": erg["art"], "ausrichtung": erg["ausrichtung"],
            "lage": lage or {"art": "mittig"}, "bild_lage": bl}
    return {"info": info, "daten": open(erg["dateien"]["daten"]).read(),
            "ecken": [(x, H - y), (x + w, H - y), (x, H - y - h)]}


def test_vektoren_umriss_und_hoehenlinie(tmp_path):
    from hirch_cam.ops import relief_vektoren as rv
    stl = tmp_path / "kuppel.stl"
    stl_schreiben(stl, kuppel())
    mat = (100, 90, 25)
    r = relief_aus(tmp_path, stl, mat, {"art": "oben"})
    erg = rv.vektoren(r, mat[2], "umriss")
    assert len(erg["linien"]) == 1 and erg["linien"][0]["geschlossen"]
    p = np.array(erg["linien"][0]["punkte"])
    assert p[:, 0].min() == pytest.approx(50 - 30, abs=0.4)      # Quadrat 60 × 60 um die Mitte
    erg = rv.vektoren(r, mat[2], "hoehenlinie", z=-6.0)
    p = np.array(erg["linien"][0]["punkte"])
    radius = np.hypot(p[:, 0] - 50, p[:, 1] - 45)
    assert radius == pytest.approx(np.full(len(p), (1600 - 34 ** 2) ** 0.5), abs=0.3)
    assert rv.vektoren(r, mat[2], "kanten")["warnungen"]          # nur STEP


@mit_step
def test_vektoren_kanten_servo_schacht(tmp_path):
    from hirch_cam.ops import relief_vektoren as rv
    mat = (560, 600, 50)
    r = relief_aus(tmp_path, FLUEGEL_STEP, mat)
    erg = rv.vektoren(r, mat[2], "kanten", material=mat)
    assert erg["anzahl_oben"] == 0 and erg["anzahl_unten"] == 1
    linie = erg["linien"][0]
    assert linie["seite"] == 2
    zv = relief.versatz_aus(-13.63, 26.63, {"art": "mittig"}, 50)["z_versatz"]
    assert linie["z"] == pytest.approx([-12.9 + zv, -10.4 + zv], abs=0.1)   # Rand Schacht + Kanal


# --------------------------------------------------------------------------------------------
# Projektion: Tiefe ab der Oberfläche des 3D-Modells

def test_gravur_auf_dem_modell(tmp_path):
    from hirch_cam import dokument
    from hirch_cam.post import tap_mm
    linie = '<path d="M 20,45 L 80,45" style="fill:none;stroke:#ff8c00;stroke-width:0.5"/>'
    svg = zeichnung(tmp_path, [{"id": "g1", "art": "gravur", "werkzeug": "K", "tiefe": 0.5,
                                "zustellung": 0.5, "tiefe_ab": "modell", "farbe": "#ff8c00"}],
                    elemente=linie)
    erg = dokument.rechnen(svg, str(tmp_path / "aus"), "gravur")
    assert erg["ok"], erg
    # die Linie reicht links und rechts über das Modell hinaus (Kuppel ±30 mm um 50)
    assert any("ausserhalb des 3D-Modells" in w for w in erg["warnungen"]), erg["warnungen"]
    job = tap_mm.read(open(erg["programme"][0]["datei"], newline="").read())
    schnitt = [(m.x, m.y, m.z) for tp in job.toolpaths for m in tp.moves if not m.rapid and m.z < 0]
    assert len(schnitt) > 50                             # in kurze Stücke geteilt
    for x, y, z in schnitt:
        if 23 < x < 77:            # über der Kappe (am Kappenrand weicht das 1-mm-Netz ab)
            soll = kuppel_hoehe(x - 50, y - 45) - 12.0 - 0.5
            assert z == pytest.approx(soll, abs=0.08), (x, z, soll)
    assert min(z for _x, _y, z in schnitt) < -11            # am Fuss der Kuppel


def test_zweiseitig_haut_statt_durch(tmp_path):
    """«Haut stehen lassen»: rundherum bleibt mitten auf der Trennfläche (Z −10) die eingestellte Dicke
    stehen; ohne Stege keine Warnung."""
    from hirch_cam.model import Job
    from hirch_cam.simulation import Hoehenfeld
    svg, mat = linsen_zeichnung(tmp_path, stege=False, modell_zwei={"an": True, "haut_an": True, "haut": 0.6})
    info, auftrag = auftrag_aus(svg, ZWEI)
    B, H, D = mat
    w = {"vorschub": 2000, "eintauchen": 500}
    c = 0.25
    felder = {}
    for seite in (1, 2):
        tpS, wS = relief3d.bearbeiten("relief_schruppen", {"zustellung": 4, "aufmass": 0.5},
                                      dict(SCHAFT, **w), auftrag, seite, 3, 0.5, (0, 0, 20), "S", 0.01)
        tpK, wK = relief3d.bearbeiten("relief_schlichten", {"bahnabstand": 15},
                                      dict(KUGEL, **w), auftrag, seite, 3, 0.5, (0, 0, 20), "K", 0.01)
        assert not any("Haltestege" in t for t in wS + wK), (wS, wK)
        hf = Hoehenfeld(0, 0, B, H, zelle=c)
        hf.fraesen(Job(toolpaths=[tpS], safe_z=20, home=(0, 0)), form="flach", durchmesser=6.0, eckradius=0)
        hf.fraesen(Job(toolpaths=[tpK], safe_z=20, home=(0, 0)), form="kugel", durchmesser=6.0, eckradius=3.0)
        felder[seite] = hf.H
    oben = felder[1]
    unten = -D - felder[2][::-1, :]
    ny, nx = min(oben.shape[0], unten.shape[0]), min(oben.shape[1], unten.shape[1])
    dick = oben[:ny, :nx] - unten[:ny, :nx]
    XX, YY = np.meshgrid(np.arange(nx) * c, np.arange(ny) * c)
    r = np.hypot(XX - 50, YY - 45)
    spalt = (r > 21.4 + 3.5) & (r < 21.4 + 7.5)
    assert spalt.sum() > 500
    assert np.min(dick[spalt]) >= 0.55                 # nirgends durch
    assert np.median(dick[spalt]) == pytest.approx(0.6, abs=0.05)
    mitte = (oben[:ny, :nx] + unten[:ny, :nx]) / 2
    assert np.median(mitte[spalt]) == pytest.approx(-10.0, abs=0.1)


def test_unterseite_fuer_seite2(tmp_path):
    """«ansicht» schreibt bei Netzen auch die Unterseite (gleiche Lage wie das Bild der Oberseite)
    für Seite 2 der Zeichnung."""
    from hirch_cam import relief, relief_import
    stl = tmp_path / "linse.stl"
    stl_schreiben(str(stl), linse())
    m = relief_import.lesen(str(stl))
    erg = relief.ansicht(m, str(tmp_path), None, {"art": "mittig"}, 20.0, achse="y")
    assert os.path.getsize(erg["dateien"]["bild_unten"]) > 1000
    with open(erg["dateien"]["bild_unten"], "rb") as fh:
        assert fh.read(8) == b"\x89PNG\r\n\x1a\n"


def test_bereich_innen_und_aussen(tmp_path):
    """«Bereich»: 3D-Schlichten nur innerhalb bzw. ausserhalb der zugeordneten Linien (+ Überlauf)."""
    svg = zeichnung(tmp_path, [])
    _info, auftrag = auftrag_aus(svg)
    w = {"vorschub": 2000, "eintauchen": 500}
    links = [{"punkte": [[0, 0], [50, 0], [50, 90], [0, 90], [0, 0]], "geschlossen": True}]
    def xs(p):
        tp, warn = relief3d.bearbeiten("relief_schlichten", dict({"bahnabstand": 15}, **p), dict(KUGEL, **w),
                                       auftrag, 1, 3, 0.5, (0, 0, 20), "K", 0.01)
        return np.array([m.x for m in tp.moves if not m.rapid and m.z < 0.0]), warn
    alle, _w = xs({})
    innen, w1 = xs({"bereich": "innen", "bereich_objekte": links, "bereich_ueberlauf": 1.0})
    aussen, w2 = xs({"bereich": "aussen", "bereich_objekte": links, "bereich_ueberlauf": 1.0})
    assert not w1 and not w2, (w1, w2)
    assert alle.min() < 40 and alle.max() > 60
    assert innen.max() <= 51.0 + 0.01 and len(innen) > 0
    assert aussen.min() >= 49.0 - 0.01 and len(aussen) > 0
    # ohne Linien: Hinweis, ganzes Modell
    ohne, w3 = xs({"bereich": "innen"})
    assert any("keine geschlossenen" in t for t in w3) and ohne.max() > 60


def test_nur_restmaterial(tmp_path):
    """«Nur Restmaterial»: ein grosser Kugelfräser lässt in der Kehle am Fuss der Kuppel Material stehen –
    der kleine fährt nur dort; nach dem kleinen selbst gibt es nichts mehr zu tun."""
    svg = zeichnung(tmp_path, [])
    _info, auftrag = auftrag_aus(svg)
    w = {"vorschub": 2000, "eintauchen": 500}
    gross = dict(KUGEL, durchmesser=12.0, eckradius=6.0, **w)
    grob, _ = relief3d.bearbeiten("relief_schlichten", {"bahnabstand": 8}, gross, auftrag, 1, 3, 0.5,
                                  (0, 0, 20), "G", 0.01)
    fein = dict(KUGEL, **w)
    def bahn(vorher, p=None):
        tp, warn = relief3d.bearbeiten("relief_schlichten", dict({"bahnabstand": 15, "rest": True,
                                                                  "rest_schwelle": 0.2}, **(p or {})),
                                       fein, auftrag, 1, 3, 0.5, (0, 0, 20), "R", 0.01, vorher=vorher)
        return sum(1 for m in tp.moves if not m.rapid), warn
    voll, _ = relief3d.bearbeiten("relief_schlichten", {"bahnabstand": 15}, fein, auftrag, 1, 3, 0.5,
                                  (0, 0, 20), "V", 0.01)
    n_voll = sum(1 for m in voll.moves if not m.rapid)
    n_rest, w1 = bahn([(grob, gross)])
    assert 0 < n_rest < n_voll * 0.9, (n_rest, n_voll, w1)
    n0, w2 = bahn([(voll, fein)], {"bahnabstand": 15})
    assert n0 == 0 and any("Kein Restmaterial" in t for t in w2), (n0, w2)
    n_ohne, w3 = bahn([])
    assert any("keine 3D-Bearbeitung" in t for t in w3)


def test_restmaterial_ohne_rillen(tmp_path):
    """Rillen zwischen den Bahnen des vorherigen Fräsers (grosser Bahnabstand) sind kein Restmaterial –
    der kleinere Fräser fährt nur, wo der grosse wegen seiner Form nicht hinkam (Peter, 18.10.2026:
    Ø 6 mit Rillen, danach Ø 5 «nur Restmaterial» fuhr die ganze Oberfläche ab)."""
    svg = zeichnung(tmp_path, [])
    _info, auftrag = auftrag_aus(svg)
    w = {"vorschub": 2000, "eintauchen": 500}
    k6 = dict(KUGEL, **w)
    k5 = dict(KUGEL, durchmesser=5.0, eckradius=2.5, **w)
    grob, _ = relief3d.bearbeiten("relief_schlichten", {"bahnabstand": 40}, k6, auftrag, 1, 3, 0.5,
                                  (0, 0, 20), "G", 0.01)
    voll, _ = relief3d.bearbeiten("relief_schlichten", {"bahnabstand": 15}, k5, auftrag, 1, 3, 0.5,
                                  (0, 0, 20), "V", 0.01)
    rest, warn = relief3d.bearbeiten("relief_schlichten", {"bahnabstand": 15, "rest": True, "rest_schwelle": 0.1},
                                     k5, auftrag, 1, 3, 0.5, (0, 0, 20), "R", 0.01, vorher=[(grob, k6)])
    n = lambda tp: sum(1 for m in tp.moves if not m.rapid)
    assert n(rest) < 0.4 * n(voll), (n(rest), n(voll), warn)


def test_rundes_material_nur_im_kreis(tmp_path):
    """Rundes Material (Zylinder Ø 100, stehend): 3D-Schruppen und -Schlichten bleiben im Kreis – die
    Ecken der Seite sind Luft."""
    svg = zeichnung(tmp_path, [], material=(100, 100, 25), rund={"an": True, "schraube": False})
    _info, auftrag = auftrag_aus(svg)
    # wie einstellungen.auftrag es für rundes Material einträgt
    auftrag["rund"] = {"mitte": [50.0, 50.0], "radius": 50.0, "schraube": False}
    w = {"vorschub": 2000, "eintauchen": 500}
    for art, wz, p in (("relief_schruppen", SCHAFT, {"rand": 12}), ("relief_schlichten", KUGEL, {"rand": 10})):
        tp, warn = relief3d.bearbeiten(art, p, dict(wz, **w), auftrag, 1, 3, 0.5, (0, 0, 20), "R", 0.01)
        xy = np.array([(m.x, m.y) for m in tp.moves if not m.rapid and m.z < -0.01])
        assert len(xy) > 50, (art, warn)
        r = np.hypot(xy[:, 0] - 50, xy[:, 1] - 50)
        # Schruppen: Schnittkante im Kreis (Mitte um den Radius innen); Schlichten: Mitte im Kreis
        grenze = 50 - (3 if art == "relief_schruppen" else 0) + 0.05
        assert r.max() <= grenze, (art, r.max())
        # ohne Kreis ginge der Rand in die Ecken (Modell 60 × 60, Rand 10–12 mm)
        assert np.hypot(np.abs(xy[:, 0] - 50).max(), np.abs(xy[:, 1] - 50).max()) > 40


def test_schaft_dicker_als_schneide(tmp_path):
    """Kugelfräser Ø 3 mit 1,5 mm Schneide an einem Ø-6-Schaft: an den steilen Flanken der Kuppel hält
    die Bahn den Schaft frei (die Spitze bleibt dort höher); ohne Schaft würde er streifen."""
    svg = zeichnung(tmp_path, [])
    info, auftrag = auftrag_aus(svg)
    tri, _v = relief3d.modell_im_material(info["reliefs"][0], info["dicke"])
    w = {"vorschub": 2000, "eintauchen": 500}
    fein = dict(KUGEL, durchmesser=3.0, eckradius=1.5, schneidenlaenge=1.5, **w)
    flach6 = {"typ": "schaft", "durchmesser": 6.0, "laenge": 1000.0}

    def streift(tp):
        xy, z = punkte_der_bahn(tp)
        sch = hirch_relief.abtasten(tri, flach6, xy, -100.0)       # Unterkante des Schafts
        return float(np.max(sch - (z + 1.5)))                      # > 0: Schaft im Modell

    ohne, _ = relief3d.bearbeiten("relief_schlichten", {"bahnabstand": 60}, fein, auftrag, 1, 3, 0.5,
                                  (0, 0, 20), "O", 0.01)
    mit, warn = relief3d.bearbeiten("relief_schlichten", {"bahnabstand": 60}, dict(fein, schaft=6.0), auftrag,
                                    1, 3, 0.5, (0, 0, 20), "M", 0.01)
    assert streift(ohne) > 0.5, streift(ohne)
    assert streift(mit) <= 0.05, (streift(mit), warn)
    # «genau» geht mit dickem Schaft nicht – Hinweis, Höhenlinien aus dem Raster
    _tp, w2 = relief3d.bearbeiten("relief_schlichten", {"bahnabstand": 80, "strategie": "hoehenlinien",
                                                         "genau": True}, dict(fein, schaft=6.0), auftrag,
                                  1, 3, 0.5, (0, 0, 20), "H", 0.01)
    assert any("genau rechnen" in t for t in w2), w2


@mit_ocl
def test_bereich_und_rest_ganzer_ablauf(tmp_path):
    """Über die Zeichnung: ein schwarzer Kreis mit «zusätzlich hier fräsen» (hirch:auch) begrenzt das
    Schlichten; ein zweites Schlichten «nur Restmaterial» mit demselben Fräser und denselben Linien hat
    nichts mehr zu tun."""
    from hirch_cam import dokument
    from hirch_cam.post import tap_mm
    kreis = ('<path d="M 40,45 A 10,10 0 1 0 60,45 A 10,10 0 1 0 40,45 Z" '
             'style="fill:none;stroke:#000000;stroke-width:0.3" hirch:auch="f1 f2"/>')
    svg = zeichnung(tmp_path, [
        {"id": "f1", "art": "relief_schlichten", "werkzeug": "K", "bahnabstand": 20, "farbe": "#9a96a0",
         "bereich": "innen", "bereich_ueberlauf": 1.0},
        {"id": "f2", "art": "relief_schlichten", "werkzeug": "K", "bahnabstand": 20, "farbe": "#9a96a0",
         "rest": True, "rest_schwelle": 0.2, "bereich": "innen"}], elemente=kreis)
    erg = dokument.rechnen(svg, str(tmp_path / "aus"), "kuppel")
    assert erg["ok"], erg
    jobs = [tap_mm.read(open(p["datei"], newline="").read()) for p in erg["programme"]]
    moves = [m for j in jobs for tp in j.toolpaths for m in tp.moves if not m.rapid and m.z < 0]
    xs = np.array([m.x for m in moves])
    assert len(xs) > 20 and xs.min() >= 39.0 - 0.05 and xs.max() <= 61.0 + 0.05, (xs.min(), xs.max())
    # f2 rechnet gegen f1: im Kreis ist nichts übrig
    assert any("Kein Restmaterial" in t for t in erg["warnungen"]), erg["warnungen"]


def stufenquader():
    """Unterbau 60 × 60 × 8 mit einem Turm 20 × 20 × 8 in der Mitte (zwei waagrechte Flächen)."""
    def quader(x0, y0, x1, y1, z0, z1):
        p = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
             (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
        f = [(4, 5, 6), (4, 6, 7), (0, 2, 1), (0, 3, 2), (0, 1, 5), (0, 5, 4), (1, 2, 6), (1, 6, 5),
             (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7)]
        return [[p[a], p[b], p[c]] for a, b, c in f]
    return np.array(quader(0, 0, 60, 60, 0, 8) + quader(20, 20, 40, 40, 8, 16), float)


@mit_ocl
def test_hoehenlinien_und_kombiniert(tmp_path):
    """Höhenlinien: konstante Höhen, nie ins Modell; kombiniert: oben flach parallel, am Rand der
    Kuppel (steil) Höhenlinien."""
    svg = zeichnung(tmp_path, [])
    info, auftrag = auftrag_aus(svg)
    tri, _v = relief3d.modell_im_material(info["reliefs"][0], info["dicke"])
    w = {"vorschub": 2000, "eintauchen": 500}
    def rechne(p):
        tp, warn = relief3d.bearbeiten("relief_schlichten", dict({"bahnabstand": 20}, **p), dict(KUGEL, **w),
                                       auftrag, 1, 3, 0.5, (0, 0, 20), "K", 0.01)
        assert not warn, warn
        xy, z = punkte_der_bahn(tp)
        soll = hirch_relief.abtasten(tri, KUGEL, xy, -100.0)
        assert np.max(soll - z) <= 0.05, np.max(soll - z)
        return tp, xy, z
    tp, xy, z = rechne({"strategie": "hoehenlinien"})
    hoehen = np.unique(np.round([m.z for m in tp.moves if not m.rapid and m.z < -0.5], 1))
    assert len(hoehen) > 8
    tpk, xyk, zk = rechne({"strategie": "kombiniert", "grenzwinkel": 30})
    tpp, _xy, _z = rechne({})
    assert 0 < len(tpk.moves) and len(tpk.moves) != len(tpp.moves)


@mit_ocl
def test_waagrechte_flaechen(tmp_path):
    """«Waagrechte Flächen» mit dem Schaftfräser: Ringe genau auf den beiden Ebenen des Stufenquaders,
    nicht auf dem Boden daneben."""
    svg = zeichnung(tmp_path, [], dreiecke=stufenquader())
    info, auftrag = auftrag_aus(svg)
    w = {"vorschub": 2000, "eintauchen": 500}
    tp, warn = relief3d.bearbeiten("relief_schlichten", {"bahnabstand": 40, "strategie": "ebenen"},
                                   dict(SCHAFT, **w), auftrag, 1, 3, 0.5, (0, 0, 20), "C", 0.01)
    assert not warn, warn
    zs = np.round([m.z for m in tp.moves if not m.rapid], 2)
    tri, _v = relief3d.modell_im_material(info["reliefs"][0], info["dicke"])
    oben = tri[..., 2].max()
    assert set(np.unique(zs)) <= {round(oben, 2), round(oben - 8, 2)} | set(np.unique(zs[zs > oben - 8.01]))
    assert np.isclose(zs, oben, atol=0.02).sum() > 10 and np.isclose(zs, oben - 8, atol=0.02).sum() > 10


def test_bohrungen_und_taschen_aus_step(tmp_path):
    """STEP-Platte 80 × 60 × 20 (tests/daten/merkmale.step): Durchgang Ø 8, Sackloch Ø 6 (8 tief),
    Tasche 20 × 14 × 6; die offene Stufe am Rand ist keine Tasche."""
    from hirch_cam import hirch_relief as hr
    if not hasattr(hr, "step_lesen"):
        pytest.skip("ohne OpenCASCADE gebaut")
    from hirch_cam.ops.relief_merkmale import merkmale
    step = os.path.join(os.path.dirname(__file__), "daten", "merkmale.step")
    svg = zeichnung(tmp_path, [], material=(100, 80, 25), modell_datei=step)
    info, _auftrag = auftrag_aus(svg)
    erg = merkmale(info["reliefs"][0], info["dicke"])
    assert erg["ok"], erg
    b = erg["bohrungen"]
    assert [round(x["d"], 2) for x in b] == [6.0, 8.0], b
    sack, durch = b
    assert not sack["durch"] and sack["tiefe"] == pytest.approx(8.0, abs=0.05)
    assert durch["durch"] and durch["tiefe"] == pytest.approx(20.0, abs=0.05)
    # Lage in der Zeichnung: Modell mittig im Material (100 × 80) → Versatz (10, 10)
    assert (durch["x"], durch["y"]) == (pytest.approx(25.0, abs=0.05), pytest.approx(25.0, abs=0.05))
    assert len(erg["taschen"]) == 1, erg["taschen"]
    t = erg["taschen"][0]
    assert t["tiefe"] == pytest.approx(6.0, abs=0.05)
    assert (t["laenge"], t["breite"]) == (pytest.approx(20.0, abs=0.05), pytest.approx(14.0, abs=0.05))


def test_bohrungen_und_taschen_gewendet(tmp_path):
    """Dasselbe Modell mit «oben = −Z»: von oben ist nur die Durchgangsbohrung zugänglich; Sackloch
    und Tasche liegen unten (Seite 2)."""
    from hirch_cam import hirch_relief as hr
    if not hasattr(hr, "step_lesen"):
        pytest.skip("ohne OpenCASCADE gebaut")
    from hirch_cam import relief as rl
    from hirch_cam.ops.relief_merkmale import merkmale
    step = os.path.join(os.path.dirname(__file__), "daten", "merkmale.step")
    ausr = dict(rl.ausrichtung(None), oben="-Z")
    svg = zeichnung(tmp_path, [], material=(100, 80, 25), modell_datei=step,
                    zusatz_info={"ausrichtung": ausr})
    info, _auftrag = auftrag_aus(svg)
    erg = merkmale(info["reliefs"][0], info["dicke"])
    assert [(round(b["d"], 2), b["durch"]) for b in erg["bohrungen"]] == [(8.0, True)], erg["bohrungen"]
    assert erg["taschen"] == []
    # mit Material (Seite 2): Sackloch und Tasche kommen auf Seite 2, gewendet um X
    erg = merkmale(info["reliefs"][0], info["dicke"], material=(100.0, 80.0, info["dicke"]), achse="x")
    b1 = [b for b in erg["bohrungen"] if b["seite"] == 1]
    b2 = [b for b in erg["bohrungen"] if b["seite"] == 2]
    assert [round(b["d"], 2) for b in b1] == [8.0] and [round(b["d"], 2) for b in b2] == [6.0], erg["bohrungen"]
    assert b2[0]["tiefe"] == pytest.approx(8.0, abs=0.05) and not b2[0]["durch"]
    # Seite 2: Öffnung auf der Unterseite des Modells, Z ab der Unterseite des Materials; das Sackloch
    # (Modell 15/45, gedreht «oben = −Z» → 15/15, im Material +10/+10 → 25/25) gewendet um X → 25/55
    assert (b2[0]["x"], b2[0]["y"]) == (pytest.approx(25.0, abs=0.05), pytest.approx(55.0, abs=0.05))
    assert b2[0]["oben"] <= 0.0 + 1e-6 and b2[0]["oben"] - b2[0]["unten"] == pytest.approx(8.0, abs=0.05)
    t2 = [t for t in erg["taschen"] if t["seite"] == 2]
    assert len(t2) == 1 and t2[0]["tiefe"] == pytest.approx(6.0, abs=0.05), erg["taschen"]
    assert (t2[0]["laenge"], t2[0]["breite"]) == (pytest.approx(20.0, abs=0.05), pytest.approx(14.0, abs=0.05))
    assert all(0 <= p_[1] <= 80 for p_ in t2[0]["punkte"])


def test_flaeche_anklicken(tmp_path):
    """Klick auf das Modell: STEP – genau die Fläche (Boden der Tasche 20 × 14); Netz – die
    zusammenhängende, ähnlich geneigte Stelle (oben auf der Kuppel ein kleiner Fleck, nicht alles)."""
    from hirch_cam import hirch_relief as hr
    from hirch_cam.ops.relief_flaeche import flaeche
    if hasattr(hr, "step_lesen"):
        step = os.path.join(os.path.dirname(__file__), "daten", "merkmale.step")
        svg = zeichnung(tmp_path, [], material=(100, 80, 25), modell_datei=step)
        info, _a = auftrag_aus(svg)
        erg = flaeche(info["reliefs"][0], info["dicke"], 60.0, 37.0)
        assert erg["art"] == "step" and erg["flaeche"] == pytest.approx(280.0, abs=1.0), erg
        assert len(erg["linien"]) == 1
    (tmp_path / "netz").mkdir()
    svg = zeichnung(tmp_path / "netz", [])
    info, _a = auftrag_aus(svg)
    erg = flaeche(info["reliefs"][0], info["dicke"], 50.0, 45.0, winkel=10)
    assert erg["art"] == "netz" and erg["linien"], erg
    assert 10 < erg["flaeche"] < 1500, erg["flaeche"]


KONISCH = {"typ": "konisch", "durchmesser": 3.0, "eckradius": 0.5, "v_winkel": 10.0, "laenge": 50}


@mit_ocl
def test_konischer_kugelfraeser(tmp_path):
    """Konischer Kugelfräser: OCL-Abtastung stimmt mit dem Profil (Kugel, dann Kegel) überein,
    Schlichten schneidet nie ins Modell, die Simulation trägt mit derselben Form ab."""
    import math
    tri = np.ascontiguousarray(stufenquader(), np.float32)
    rb, halb = 0.5, math.radians(5.0)
    dt, ht = rb * math.cos(halb), rb - rb * math.sin(halb)

    def profil(d):
        return rb - math.sqrt(max(0.0, rb * rb - d * d)) if d <= dt else ht + (d - dt) / math.tan(halb)
    dxs = [0.0, 0.3, 0.8, 1.2]
    z = hirch_relief.abtasten(tri, KONISCH, np.array([[40 + dx, 30.0] for dx in dxs]), -50.0)
    for dx, zz in zip(dxs, z):
        assert zz == pytest.approx(max(8.0, 16.0 - profil(dx)), abs=1e-3), (dx, zz)
    H = np.zeros((41, 41), np.float32)
    hirch_relief.abtragen(H, -2.0, -2.0, 0.1, np.array([[0.0, 0.0, -3.0], [0.0, 0.0, -3.0]]), KONISCH,
                          [0.0, 0.0, 50.0], -100.0)
    for k in (0, 4, 10, 15):
        assert H[20, 20 + k] == pytest.approx(min(0.0, -3.0 + profil(k * 0.1)), abs=1e-3)
    # Schlichten der Kuppel: nie tiefer als die Abtastung
    svg = zeichnung(tmp_path, [])
    info, auftrag = auftrag_aus(svg)
    tri_k, _v = relief3d.modell_im_material(info["reliefs"][0], info["dicke"])
    w = dict(KONISCH, vorschub=2000, eintauchen=500)
    tp, warn = relief3d.bearbeiten("relief_schlichten", {"bahnabstand": 40}, w, auftrag, 1, 3, 0.5,
                                   (0, 0, 20), "KK", 0.01)
    assert not warn, warn
    xy, zb = punkte_der_bahn(tp)
    soll = hirch_relief.abtasten(tri_k, KONISCH, xy, -100.0)
    assert np.max(soll - zb) <= 0.05


def test_konisch_in_2d_wie_v_fraeser():
    """In 2D-Bearbeitungen gilt der konische Kugelfräser als V-Fräser mit flacher Spitze Ø 2·R."""
    from hirch_cam.engine import als_2d, werkzeug_name
    w = als_2d(dict(KONISCH))
    assert w["typ"] == "v" and w["spitze"] == pytest.approx(1.0) and w["v_winkel"] == 10.0
    assert "Konischer Kugelfräser" in werkzeug_name(dict(KONISCH))


@mit_ocl
def test_hoehenlinien_huegel_nacheinander(tmp_path):
    """Zwei Hügel: die Höhenlinien fräsen einen Hügel von oben bis unten, dann den anderen – nicht
    auf jeder Höhe hin und her."""
    a = kuppel(R=14.0, h=8.0, halb=12.0, n=41)
    b = a.copy()
    b[..., 0] += 30.0
    svg = zeichnung(tmp_path, [], material=(110, 70, 25), dreiecke=np.concatenate([a, b]))
    _info, auftrag = auftrag_aus(svg)
    w = {"vorschub": 2000, "eintauchen": 500}
    tp, warn = relief3d.bearbeiten("relief_schlichten", {"bahnabstand": 25, "strategie": "hoehenlinien"},
                                   dict(KUGEL, **w), auftrag, 1, 3, 0.5, (0, 0, 20), "K", 0.01)
    assert not warn, warn
    xs = [m.x for m in tp.moves if not m.rapid and m.z < -0.5]
    mitte = (min(xs) + max(xs)) / 2
    seiten = [x > mitte for x in xs]
    wechsel = sum(1 for p, q in zip(seiten, seiten[1:]) if p != q)
    assert wechsel <= 3, wechsel


@mit_ocl
def test_hoehenlinien_genau(tmp_path):
    """«Genau»: Höhenlinien von OpenCAMLib (AdaptiveWaterline). Auf der Kuppel liegt die Fräsermitte
    auf dem Kreis, den die Kugel dort berührt (Kugelfräser R3: Abstand zur Kugelmitte R + 3)."""
    if not hasattr(hirch_relief, "Wasserlinie"):
        pytest.skip("Rechenmodul ohne Wasserlinie")
    import math
    svg = zeichnung(tmp_path, [])
    info, auftrag = auftrag_aus(svg)
    tri, _v = relief3d.modell_im_material(info["reliefs"][0], info["dicke"])
    w = {"vorschub": 2000, "eintauchen": 500}
    tp, warn = relief3d.bearbeiten("relief_schlichten", {"bahnabstand": 30, "strategie": "hoehenlinien",
                                                          "genau": True},
                                   dict(KUGEL, **w), auftrag, 1, 3, 0.5, (0, 0, 20), "K", 0.01)
    assert not warn, warn
    xy, z = punkte_der_bahn(tp)
    soll = hirch_relief.abtasten(tri, KUGEL, xy, -100.0)
    assert np.max(soll - z) <= 0.05
    # Kuppel: Kugel R40, Scheitel auf Z0 (Höhe 12) – Mitte (50, 45), Kugelmitte bei Z −40
    cx, cy = (float(tri[..., 0].min()) + float(tri[..., 0].max())) / 2, \
        (float(tri[..., 1].min()) + float(tri[..., 1].max())) / 2
    ztop = float(tri[..., 2].max())
    feld = [(m.x, m.y, m.z) for m in tp.moves if not m.rapid and ztop - 8 < m.z < ztop - 1]
    assert len(feld) > 50
    for x, y, zz in feld[::7]:
        mz = zz + 3.0                            # Mitte der Fräserkugel
        r = math.hypot(x - cx, y - cy)
        assert math.hypot(r, mz - (ztop - 40.0)) == pytest.approx(43.0, abs=0.05)


@mit_ocl
@pytest.mark.parametrize("w", [
    {"typ": "kugel", "durchmesser": 6.0, "eckradius": 3.0},
    {"typ": "schaft", "durchmesser": 6.0},
    {"typ": "eckradius", "durchmesser": 6.0, "eckradius": 1.0},
    {"typ": "v", "durchmesser": 6.0, "v_winkel": 60.0},
    {"typ": "v", "durchmesser": 6.0, "v_winkel": 90.0, "spitze": 0.5},
    {"typ": "konisch", "durchmesser": 3.0, "eckradius": 0.5, "v_winkel": 10.0},
])
def test_schnellabtaster_wie_opencamlib(w):
    """Der eigene Drop-Cutter gibt dieselben Höhen wie OpenCAMLib (Kuppel und Stufenquader)."""
    if not hasattr(hirch_relief, "SchnellAbtaster"):
        pytest.skip("Rechenmodul ohne SchnellAbtaster")
    for tri in (kuppel(), stufenquader()):
        tri = np.ascontiguousarray(tri, np.float32)
        lo, hi = tri.reshape(-1, 3).min(0), tri.reshape(-1, 3).max(0)
        rng = np.random.default_rng(3)
        pts = np.column_stack([rng.uniform(lo[0] - 2, hi[0] + 2, 3000), rng.uniform(lo[1] - 2, hi[1] + 2, 3000)])
        z_ocl = hirch_relief.Abtaster(tri, w).abtasten(pts, float(lo[2]) - 1)
        z = hirch_relief.SchnellAbtaster(tri, w).abtasten(pts, float(lo[2]) - 1)
        assert np.max(np.abs(z - z_ocl)) < 1e-6


@mit_ocl
def test_kehlen_nachfahren(tmp_path):
    """Stufenquader mit Kugelfräser R3: die Kehle am Fuss des Turms (Innenecke Turm/Unterbau) wird
    als Ring im Abstand R um den Turm gefahren, auf Höhe des Unterbaus; die Aussenkanten (konvex)
    nicht."""
    svg = zeichnung(tmp_path, [], dreiecke=stufenquader())
    info, auftrag = auftrag_aus(svg)
    tri, _v = relief3d.modell_im_material(info["reliefs"][0], info["dicke"])
    w = {"vorschub": 2000, "eintauchen": 500}
    tp, warn = relief3d.bearbeiten("relief_schlichten", {"bahnabstand": 20, "strategie": "kehlen"},
                                   dict(KUGEL, **w), auftrag, 1, 3, 0.5, (0, 0, 20), "K", 0.01)
    assert not warn, warn
    lo, hi = tri.reshape(-1, 3).min(0), tri.reshape(-1, 3).max(0)
    cx, cy = (lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2
    oben = float(hi[2])
    pts = [(m.x, m.y, m.z) for m in tp.moves if not m.rapid and m.z < oben - 1]
    assert len(pts) > 20
    for x, y, z in pts:
        # Abstand zum Turm (20 × 20 um die Mitte): Rechteck-Abstand ≈ 3 (Kugelradius)
        dx, dy = max(abs(x - cx) - 10, 0.0), max(abs(y - cy) - 10, 0.0)
        assert math.hypot(dx, dy) == pytest.approx(3.0, abs=0.3), (x, y)
        assert z == pytest.approx(oben - 8, abs=0.05)
    # rundherum: alle vier Seiten
    assert min(x for x, _y, _z in pts) < cx - 12 and max(x for x, _y, _z in pts) > cx + 12
    assert min(y for _x, y, _z in pts) < cy - 12 and max(y for _x, y, _z in pts) > cy + 12


@mit_ocl
def test_kehlen_mit_bahnen_beidseits(tmp_path):
    """«Bahnen beidseits»: zusätzliche Bahnen neben der Kehle, nie ins Modell."""
    svg = zeichnung(tmp_path, [], dreiecke=stufenquader())
    info, auftrag = auftrag_aus(svg)
    tri, _v = relief3d.modell_im_material(info["reliefs"][0], info["dicke"])
    w = dict(KUGEL, vorschub=2000, eintauchen=500)
    tp0, _ = relief3d.bearbeiten("relief_schlichten", {"bahnabstand": 20, "strategie": "kehlen"}, w,
                                 auftrag, 1, 3, 0.5, (0, 0, 20), "K", 0.01)
    tp2, warn = relief3d.bearbeiten("relief_schlichten", {"bahnabstand": 20, "strategie": "kehlen",
                                                          "kehlen_bahnen": 2}, w, auftrag, 1, 3, 0.5,
                                    (0, 0, 20), "K", 0.01)
    assert not warn, warn
    assert len(tp2.moves) > 2 * len(tp0.moves)
    xy, z = punkte_der_bahn(tp2)
    soll = hirch_relief.abtasten(tri, KUGEL, xy, -100.0)
    assert np.max(soll - z) <= 0.05


@mit_ocl
def test_schruppen_adaptiv(tmp_path):
    """Adaptives 3D-Schruppen: Ebene für Ebene mit FreeCAD Adaptive, nie näher als das Aufmass ans
    Modell, bis zum Fuss der Kuppel."""
    svg = zeichnung(tmp_path, [])
    info, auftrag = auftrag_aus(svg)
    tri, _v = relief3d.modell_im_material(info["reliefs"][0], info["dicke"])
    w = dict(SCHAFT, vorschub=2000, eintauchen=500)
    tp, warn = relief3d.bearbeiten("relief_schruppen", {"zustellung": 4, "aufmass": 0.5, "adaptiv": True,
                                                        "adaptiv_last": 15, "boden_fertig": False},
                                   w, auftrag, 1, 3, 0.5, (0, 0, 20), "S", 0.01)
    assert not warn, warn
    assert len(tp.moves) > 200
    xy, z = punkte_der_bahn(tp)
    soll = hirch_relief.abtasten(tri, SCHAFT, xy, -100.0)
    assert np.min(z - soll) >= 0.5 - 0.05
    assert z.min() < -8


@mit_step
def test_testteil_hartschaum(tmp_path):
    """Mitgeliefertes Testteil (hirch_cam/daten/testteil_hartschaum.step, 180 × 180 × 39, mittig in
    200 × 200 × 50): Bohrungen und Taschen beider Seiten; Eckradien der Taschen sind keine Bohrungen."""
    from hirch_cam.ops.relief_merkmale import merkmale
    step = os.path.join(os.path.dirname(relief.__file__), "daten", "testteil_hartschaum.step")
    svg = zeichnung(tmp_path, [], material=(200, 200, 50), modell_datei=step)
    info, _auftrag = auftrag_aus(svg)
    tri, _v = relief3d.modell_im_material(info["reliefs"][0], info["dicke"])
    lo, hi = tri.reshape(-1, 3).min(0), tri.reshape(-1, 3).max(0)
    assert hi[:2] - lo[:2] == pytest.approx([180, 180], abs=0.05) and hi[2] - lo[2] == pytest.approx(39, abs=0.05)
    erg = merkmale(info["reliefs"][0], info["dicke"], material=(200.0, 200.0, info["dicke"]), achse="x")
    b = sorted((x["seite"], round(x["d"], 1), x["durch"], round(x["tiefe"], 1)) for x in erg["bohrungen"])
    assert b[0][:3] == (1, 6.0, False) and b[0][3] == 8.0, b
    assert b[1][:3] == (1, 8.0, True), b
    assert b[2] == (2, 6.0, False, 10.0), b
    assert len(b) == 3, b
    t = sorted((x["seite"], round(x["laenge"]), round(x["breite"]), round(x["tiefe"])) for x in erg["taschen"])
    assert t == [(1, 40, 25, 10), (2, 40, 20, 8)], erg["taschen"]


def test_flaechen_waehlen_seite2_wand_vereinigen(tmp_path):
    """Flächen wählen (Peter, 22.10.): mit der Höhe des Klicks die nächste Fläche – oben, unten
    (Seite 2, gewendet in die Zeichnung) oder eine senkrechte Wand (schmaler Streifen); mehrere
    Umrisse vereinigt ohne Naht; Netz mit einstellbarem Winkel."""
    from hirch_cam import hirch_relief as hr
    from hirch_cam.ops.relief_flaeche import flaeche, vereinigen, WAND_BREITE
    if hasattr(hr, "step_lesen"):
        step = os.path.join(os.path.dirname(__file__), "daten", "merkmale.step")
        svg = zeichnung(tmp_path, [], material=(100, 80, 25), modell_datei=step)
        info, _a = auftrag_aus(svg)
        r = info["reliefs"][0]
        oben = flaeche(r, info["dicke"], 60.0, 37.0)
        assert oben["seite"] == 1 and oben["id"].startswith("step:") and oben["name"]
        z_unten = oben["z"][0]
        # gleiche Stelle von unten: die Unterseite des Modells, Linien gewendet um X (y → 80 − y)
        unten = flaeche(r, info["dicke"], 30.0, 20.0, seite=2, material=(100, 80), achse="x")
        assert unten["seite"] == 2 and unten["umriss"], unten
        assert all(l.get("seite") == 2 for l in unten["linien"])
        p_um, p_li = unten["umriss"][0]["punkte"][0], unten["linien"][0]["punkte"][0]
        assert p_li[0] == pytest.approx(p_um[0]) and p_li[1] == pytest.approx(80 - p_um[1])
        assert unten["z"][1] < z_unten + 1e-6 or unten["id"] != oben["id"]
        # Klick mit Höhe auf den Boden der Tasche: dieselbe Fläche wie ohne Höhe
        mit_z = flaeche(r, info["dicke"], 60.0, 37.0, z=oben["z"][0])
        assert mit_z["id"] == oben["id"]
        # Vereinigen: zweimal dieselbe Fläche → eine Fläche gleicher Grösse
        v = vereinigen([oben["umriss"], oben["umriss"]])
        assert v["flaeche"] == pytest.approx(oben["flaeche"], rel=0.02) and len(v["linien"]) == 1
        assert WAND_BREITE > 0
    (tmp_path / "netz").mkdir()
    svg = zeichnung(tmp_path / "netz", [])
    info, _a = auftrag_aus(svg)
    r = info["reliefs"][0]
    klein = flaeche(r, info["dicke"], 50.0, 45.0, winkel=5)
    gross = flaeche(r, info["dicke"], 50.0, 45.0, winkel=40)
    assert klein["art"] == "netz" and gross["flaeche"] > klein["flaeche"], (klein["flaeche"], gross["flaeche"])
    assert klein["id"] != gross["id"]


def test_vereinigen_aneinander():
    """Zwei aneinandergrenzende Quadrate (gemeinsame Kante) werden ein Rechteck ohne Naht."""
    from hirch_cam.ops.relief_flaeche import vereinigen
    a = [{"punkte": [[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]], "geschlossen": True}]
    b = [{"punkte": [[10, 0], [20, 0], [20, 10], [10, 10], [10, 0]], "geschlossen": True}]
    v = vereinigen([a, b])
    assert len(v["linien"]) == 1 and v["flaeche"] == pytest.approx(200.0, abs=0.5)
    # Seite 2: gewendet um Y (x → B − x)
    v2 = vereinigen([a], seite=2, material=(100, 50), achse="y")
    xs = sorted({q[0] for q in v2["linien"][0]["punkte"]})
    assert xs[0] == pytest.approx(90.0, abs=0.06) and xs[-1] == pytest.approx(100.0, abs=0.06)


def test_bereich_nur_gewaehlte_flaechen():
    """Bereich «flaechen» / «ohne_flaechen» nimmt nur die Linien aus «Gewählte Flächen als Bereich»,
    «innen» / «aussen» nur die übrigen."""
    from hirch_cam.ops.relief3d import begrenzung
    fl = {"punkte": [[0, 0], [10, 0], [10, 10], [0, 10], [0, 0]], "geschlossen": True, "flaechen": True}
    li = {"punkte": [[50, 50], [60, 50], [60, 60], [50, 60], [50, 50]], "geschlossen": True}
    g, art = begrenzung({"bereich": "flaechen", "bereich_ueberlauf": 0, "bereich_objekte": [fl, li]})
    assert art == "innen" and g.bounds == pytest.approx((0, 0, 10, 10))
    g, art = begrenzung({"bereich": "ohne_flaechen", "bereich_ueberlauf": 0, "bereich_objekte": [fl, li]})
    assert art == "aussen" and g.bounds == pytest.approx((0, 0, 10, 10))
    g, art = begrenzung({"bereich": "innen", "bereich_ueberlauf": 0, "bereich_objekte": [fl, li]})
    assert g.bounds == pytest.approx((50, 50, 60, 60))
    assert begrenzung({"bereich": "flaechen", "bereich_objekte": [li]}) is None


def test_bestand_eilgang_nur_ueber_material():
    """Peter 25.10. (Impeller): «Boden nachfahren» tauchte im Eilgang 2 mm in einen Streifen neben der
    Modellwand. Das Schruppen führt mit, wie hoch das Material noch steht; ein Eilgang nach unten endet
    über dem höchsten Material unter dem Fräser."""
    from shapely.geometry import Point, box
    b = relief3d._Bestand((0.0, 0.0, 40.0, 40.0), 0.25, 3.0, (40.0, 40.0, 30.0))
    assert b.hoehe(20, 20) == 0.0                       # noch nichts geräumt
    b.geraeumt(box(0, 0, 40, 40).difference(box(24, 0, 40, 40)), -10.0)   # links bis −10
    b.geraeumt(box(0, 0, 20, 40), -20.0)                # ganz links bis −20
    assert b.hoehe(10, 20) == pytest.approx(-20.0)
    assert b.hoehe(18, 20) == pytest.approx(-10.0)      # Fräser reicht über x = 20 hinaus
    assert b.hoehe(22.5, 20) == pytest.approx(0.0)      # reicht über x = 24: dort steht alles
    assert b.hoehe(10, 20, rand=12) == pytest.approx(0.0)
    # am Rand des Rohteils ist kein Material mehr – am Rand des Rasters innen schon
    b2 = relief3d._Bestand((5.0, 5.0, 40.0, 40.0), 0.25, 3.0, (40.0, 40.0, 30.0))
    b2.geraeumt(Point(20, 20).buffer(30), -5.0)
    assert b2.hoehe(39, 20) == pytest.approx(-5.0)
    assert b2.hoehe(6, 20) == pytest.approx(0.0)


def test_boeden_und_waende_auf_mass_stufenweise(tmp_path):
    """Peter 25.10. (Impeller): «Böden und Wände» fräst waagrechte Flächen und senkrechte Wände auf Mass,
    Stufe für Stufe nach dem 3D-Schruppen – nie ins Modell und nur, wo das Schruppen hinkam."""
    from hirch_cam.ops import boeden_waende as bw
    svg = zeichnung(tmp_path, [], dreiecke=stufenquader())
    info, auftrag = auftrag_aus(svg)
    tri, _v = relief3d.modell_im_material(info["reliefs"][0], info["dicke"])
    oben = float(tri[..., 2].max())
    w = dict(SCHAFT, vorschub=2000, eintauchen=500, schneidenlaenge=20)
    ps = {"zustellung": 3, "aufmass": 0.5}
    tp_s, warn = relief3d.bearbeiten("relief_schruppen", ps, w, auftrag, 1, 3, 0.5, (0, 0, 20), "S", 0.01)
    assert not any("Restmaterial" in x for x in warn), warn
    stufen, warn = bw.bearbeiten({"stufe": 4}, w, auftrag, 1, 3, 0.5, (0, 0, 20), "B", 0.01, w, ps)
    assert stufen and not warn, warn
    assert [k for k, _t in stufen] == sorted(k for k, _t in stufen)
    from hirch_cam.model import Toolpath
    tp = Toolpath(moves=[m for _k, t in stufen for m in t.moves])
    xy, z = punkte_der_bahn(tp)
    soll = hirch_relief.abtasten(tri, SCHAFT, xy, -100.0)
    assert np.min(z - soll) >= -0.02                       # nie ins Modell
    # Absatz des Unterbaus (8 mm unter dem Turm) genau auf Mass, nicht Aufmass darüber
    absatz = oben - 8
    auf_absatz = np.isclose(z, absatz, atol=0.02) & np.isclose(soll, absatz, atol=0.02)
    assert auf_absatz.sum() > 10
    # Turm (x, y 20…40 in Modellkoordinaten): Fräsermitte genau einen Radius neben der Wand
    lo = tri.reshape(-1, 3)[:, :2].min(0)
    tx0, ty0 = lo[0] + 20, lo[1] + 20
    dx = np.maximum(np.maximum(tx0 - xy[:, 0], xy[:, 0] - (tx0 + 20)), 0)
    dy = np.maximum(np.maximum(ty0 - xy[:, 1], xy[:, 1] - (ty0 + 20)), 0)
    abst = np.hypot(dx, dy)
    an_wand = (np.abs(abst - 3.0) < 0.03) & (z < oben - 0.5) & (z > absatz + 0.1)
    assert an_wand.sum() > 20
    # Leerwege (Fräsbahnen 25.10.): zwischen Zügen nicht jedes Mal auf die Freifahrhöhe – über Geräumtem tiefer
    alle = [m for _k, t in stufen for m in t.moves]
    quer = [(a_, b_) for a_, b_ in zip(alle, alle[1:]) if b_.rapid and math.hypot(b_.x - a_.x, b_.y - a_.y) > 1e-6]
    assert any(b_.z < 3 - 1e-6 for _a, b_ in quer), "kein tiefer Quergang"
    # Stufe 0 hat die Wand bis −4, die nächste geht tiefer
    k0 = Toolpath(moves=list(stufen[0][1].moves))
    _xy0, z0 = punkte_der_bahn(k0)
    assert stufen[0][0] == 0 and z0.min() >= oben - 4.01 - 1e-6
    # das Schruppen lässt sich an denselben Grenzen teilen: jedes Stück beginnt auf der Freifahrhöhe
    teile = bw.aufteilen(tp_s, 4, 3)
    assert len(teile) >= 2 and [k for k, _t in teile] == sorted({k for k, _t in teile})
    assert sum(len(t.moves) for _k, t in teile) == len(tp_s.moves)
    for k, t in teile[1:]:
        assert t.moves[0].rapid and t.moves[0].z >= 3 - 1e-6
        tief = [m.z for m in t.moves if not m.rapid]
        assert min(tief) < -(k * 4) + 1e-6


@mit_ocl
def test_schlichten_laesst_fertiges_aus(tmp_path):
    """«Was schon auf Mass ist, auslassen»: ein Modell nur aus waagrechten Flächen und senkrechten Wänden
    braucht kein Schlichten mehr; die Kuppel (gekrümmt) wird geschlichtet, mit einem Umlauf am Fuss."""
    w = dict(KUGEL, vorschub=2000, eintauchen=500)
    svg = zeichnung(tmp_path, [], dreiecke=stufenquader())
    info, auftrag = auftrag_aus(svg)
    tp, warn = relief3d.bearbeiten("relief_schlichten", {"bahnabstand": 20, "strategie": "kombiniert",
                                                         "auslassen_fertig": True},
                                   w, auftrag, 1, 3, 0.5, (0, 0, 20), "K", 0.01)
    assert any("Nichts mehr zu schlichten" in x for x in warn), warn
    assert not [m for m in tp.moves if not m.rapid and m.z < -0.5]
    (tmp_path / "k").mkdir()
    svg = zeichnung(tmp_path / "k", [])
    info, auftrag = auftrag_aus(svg)
    tri, _v = relief3d.modell_im_material(info["reliefs"][0], info["dicke"])
    tp, warn = relief3d.bearbeiten("relief_schlichten", {"bahnabstand": 20, "strategie": "kombiniert",
                                                         "auslassen_fertig": True},
                                   w, auftrag, 1, 3, 0.5, (0, 0, 20), "K", 0.01)
    assert not warn, warn
    xy, z = punkte_der_bahn(tp)
    soll = hirch_relief.abtasten(tri, KUGEL, xy, -100.0)
    assert np.max(soll - z) <= 0.05                       # nie ins Modell
    sockel = float(np.nanmin(tri[..., 2]))
    assert len(xy) > 100


def test_boeden_und_waende_im_wechsel_mit_schruppen(tmp_path):
    """Gleicher Fräser: das 3D-Schruppen und «Böden und Wände» wechseln Stufe um Stufe ab – ein Programm,
    kein Werkzeugwechsel; mit einem anderen Fräser nacheinander und ein Hinweis."""
    from hirch_cam import dokument
    bea = [{"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 3, "aufmass": 0.5},
           {"id": "bw", "art": "boeden_waende", "werkzeug": "C", "stufe": 4, "im_wechsel_mit": "s1"}]
    svg = zeichnung(tmp_path, bea, dreiecke=stufenquader())
    erg = dokument.rechnen(svg, str(tmp_path / "aus"))
    assert erg["ok"], erg["warnungen"]
    assert len(erg["programme"]) == 1
    keys = [s["key"] for s in erg["programme"][0]["abschnitte"]]
    assert keys[:4] == ["s1", "bw", "s1#stufe2", "bw#stufe2"], keys
    assert erg["bearbeitungen"]["bw"]["minuten"] > 0
    assert not [w for w in erg["warnungen"] if w.startswith("Böden und Wände")], erg["warnungen"]
    # anderer Fräser: nicht im Wechsel
    (tmp_path / "k").mkdir()
    bea[1] = dict(bea[1], werkzeug="K")
    svg = zeichnung(tmp_path / "k", bea, dreiecke=stufenquader())
    erg = dokument.rechnen(svg, str(tmp_path / "k" / "aus"))
    assert any("Nicht im Wechsel: anderer Fräser" in w for w in erg["warnungen"]), erg["warnungen"]


def test_merkmale_boden_wand_rundung():
    """Erkennen (Peter 25.10.): Boden, senkrechte Wand und die Rundung am Fuss (R 2) aus den genauen Schnitten;
    eine dünne Wand (1,5 mm, 10 mm hoch) wird gemeldet."""
    from hirch_cam.ops.relief_merkmale import boeden_waende

    def streifen(xs, zs, y0, y1):
        t = []
        for i in range(len(xs) - 1):
            a, b = (xs[i], y0, zs[i]), (xs[i + 1], y0, zs[i + 1])
            c, d = (xs[i + 1], y1, zs[i + 1]), (xs[i], y1, zs[i])
            t += [[a, b, c], [a, c, d]]
        return t
    R = 2.0
    xf = [20 + R * math.sin(w) for w in np.linspace(0, math.pi / 2, 24)]
    zf = [-8 - R * math.cos(w) for w in np.linspace(0, math.pi / 2, 24)]
    xs = [0, 10] + xf + [22.001, 28, 28.001, 34, 34.001, 35.5, 35.501, 40]
    zs = [-10, -10] + zf + [0, 0, -10, -10, 0, 0, -10, -10]
    tri = np.array(streifen(xs, zs, 0, 20), float)
    erg = boeden_waende(tri)
    assert erg is not None
    assert [b["z"] for b in erg["boeden"]] == [-10.0]
    assert erg["rundungen"] and erg["rundungen"][0]["radius"] == pytest.approx(2.0)
    assert erg["waende"] >= 2
    assert erg["duenn"] and erg["duenn"]["dicke"] == pytest.approx(1.5, abs=0.1)


def test_restmaterial_befund_mit_knopf():
    """Restmaterial nach dem 3D-Schruppen: in der Prüfung mit «aktion» (Knopf «Restmaterial mit Ø 3 anlegen»)."""
    from hirch_cam.pruefung import restmaterial_befunde
    a = {"bearbeitungen": [{"id": "s1", "art": "relief_schruppen", "name": "3D-Schruppen 1 (3D-Modell)"}]}
    t = ("Restmaterial: Der Fräser Ø 6 passt nicht überall hin – auf 1369 mm² bleiben bis 26 mm über dem Aufmass "
         "stehen. Vorschlag: danach ein zweites 3D-Schruppen mit einem Schaftfräser Ø 2,5 und «nur Restmaterial».")
    b = restmaterial_befunde(a, {"op_warnungen": {"s1": [t, "anderes"]}})
    assert len(b) == 1 and b[0]["aktion"] == "restmaterial" and b[0]["durchmesser"] == 2.5
    assert b[0]["text"] == "3D-Schruppen 1 (3D-Modell): " + t and b[0]["bearbeitung"] == "s1"


def _zwei_bloecke():
    """Sockel 60 × 40 × 4, darauf zwei Blöcke 12 hoch mit 4 mm Spalt (Ø 6 passt nicht hinein)."""
    def quader(x0, y0, x1, y1, z0, z1):
        p = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
             (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
        f = [(4, 5, 6), (4, 6, 7), (0, 2, 1), (0, 3, 2), (0, 1, 5), (0, 5, 4), (1, 2, 6), (1, 6, 5),
             (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7)]
        return [[p[a], p[b], p[c]] for a, b, c in f]
    return np.array(quader(0, 0, 60, 40, 0, 4) + quader(10, 10, 26, 30, 4, 16) + quader(30, 10, 46, 30, 4, 16),
                    float)


def test_restmaterial_vor_dem_schlichten(tmp_path):
    """Lässt das 3D-Schruppen Restmaterial stehen, warnt das 3D-Schlichten danach (der Kugelfräser griffe voll
    ein) – mit einem 3D-Schruppen «nur Restmaterial» dazwischen nicht mehr; der Hinweis steht in der Prüfung mit
    dem Knopf (aktion «restmaterial»)."""
    from hirch_cam import dokument
    bea = [{"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 4, "aufmass": 0.5},
           {"id": "f1", "art": "relief_schlichten", "werkzeug": "K", "bahnabstand": 40}]
    svg = zeichnung(tmp_path, bea, dreiecke=_zwei_bloecke(), material=(100, 70, 20))
    erg = dokument.rechnen(svg, str(tmp_path / "aus"))
    assert any(w.startswith("Vorher steht noch Restmaterial") for w in erg["op_warnungen"].get("f1", [])), erg
    knopf = [b for b in erg["pruefung"] if b.get("aktion") == "restmaterial"]
    assert knopf and knopf[0]["bearbeitung"] == "s1" and knopf[0]["durchmesser"] <= 4
    assert not [w for w in erg["warnungen"] if "Restmaterial: Der Fräser" in w]     # nur einmal (Prüfung)
    (tmp_path / "r").mkdir()
    bea.insert(1, {"id": "s2", "art": "relief_schruppen", "werkzeug": "K", "zustellung": 4, "aufmass": 0.5,
                   "rest": True})
    svg = zeichnung(tmp_path / "r", bea, dreiecke=_zwei_bloecke(), material=(100, 70, 20))
    erg = dokument.rechnen(svg, str(tmp_path / "r" / "aus"))
    assert not any(w.startswith("Vorher steht noch") for w in erg["op_warnungen"].get("f1", [])), erg["op_warnungen"]


def test_schlichten_uebergaenge_ueber_geraeumtem():
    """3D-Schlichten: Übergänge (abheben, quer, eintauchen) nur so hoch wie das Material, das nach den
    Bearbeitungen davor tatsächlich steht – über Geräumtem im Eilgang tief, über einer stehenden Insel darüber."""
    from hirch_cam.model import Move, Toolpath
    vorher_moves = [Move(True, 2, 2, 5)]
    for k, y in enumerate(np.arange(2, 49, 2.0)):
        xs = (2, 48) if k % 2 == 0 else (48, 2)
        vorher_moves += [Move(False, xs[0], y, -10, 500), Move(False, xs[1], y, -10, 500)]
    # Insel x 18…22 bleibt stehen: dort nicht räumen
    vorher = [(Toolpath(moves=[m for m in vorher_moves]), {"typ": "schaft", "durchmesser": 6})]
    insel = Toolpath(moves=[Move(True, 2, 2, 5)] + [Move(False, x, y, -10, 500) for y in np.arange(2, 49, 2.0)
                                                     for x in (2, 14)] +
                     [Move(True, 14, 48, 5)] + [Move(False, x, y, -10, 500) for y in np.arange(2, 49, 2.0)
                                                for x in (26, 48)])
    def schlicht():
        return Toolpath(moves=[Move(True, 10, 10, 20), Move(True, 10, 10, 0.5), Move(False, 10, 10, -9.5, 300),
                               Move(False, 12, 10, -9.5, 1000),
                               Move(True, 12, 10, 3), Move(True, 40, 30, 3), Move(True, 40, 30, 0.5),
                               Move(False, 40, 30, -9.5, 300), Move(False, 42, 30, -9.5, 1000)])
    tp = schlicht()
    assert relief3d.tief_uebergaenge(tp, vorher, (60, 60, 20), 6.0, 3.0, 0.5, (0, 0, 20)) == 1
    quer = [m for m in tp.moves if m.rapid and abs(m.x - 40) < 1e-9]
    assert quer and max(m.z for m in quer) < -9                      # tief hinüber, kein Eintauchen ab 0,5
    tp = schlicht()
    relief3d.tief_uebergaenge(tp, [(insel, {"typ": "schaft", "durchmesser": 6})], (60, 60, 20), 6.0, 3.0, 0.5,
                              (0, 0, 20))
    quer = [m for m in tp.moves if m.rapid and abs(m.x - 40) < 1e-9]
    assert max(m.z for m in quer) == pytest.approx(0.5)               # über die Insel (Oberfläche) + Abstand
    assert min(m.z for m in quer) <= -9 + 1e-9                         # am Ziel im Eilgang hinunter


def test_modell_auf_platte_zwei_mit_eigener_groesse(tmp_path):
    """3D-Modell auf Platte 2 (eigene Grösse 140 × 70, unten bündig neben Platte 1): Lesen, Rechnen und das
    Fräsergebnis des Reiters «3D» arbeiten in den Koordinaten und mit der Grösse dieser Platte (3D, 26.10.)."""
    import inkex
    import re
    from hirch_cam import dokument, svg_lesen
    from hirch_cam.ops import relief_ergebnis
    bea = [{"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 6, "aufmass": 0.5}]
    pfad = zeichnung(tmp_path, bea, material=(100, 90, 25))
    svg = open(pfad, encoding="utf-8").read()
    m = re.search(r'<image id="relief1" x="([\d.e+-]+)" y="([\d.e+-]+)" width="([\d.e+-]+)" height="([\d.e+-]+)"', svg)
    x, y, w, h = (float(v) for v in m.groups())
    dx, dy = 120 + 10 - x, 20 + 10 - y                   # Platte 2 links oben bei (120, 20): Bild bei (10, 10)
    svg = svg.replace('<image id="relief1"', f'<g id="verschoben" transform="translate({dx},{dy})"><image id="relief1"')
    svg = svg.replace('</g></svg>', '</g></g></svg>')
    svg = svg.replace('<defs>', '<sodipodi:namedview id="nv"><inkscape:page id="p1" x="0" y="0" width="100" '
                      'height="90"/><inkscape:page id="p2" x="120" y="20" width="140" height="70" '
                      'hirch:platte-groesse="140×70"/></sodipodi:namedview><defs>', 1)
    svg = svg.replace('xmlns:inkscape=', 'xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd" '
                      'xmlns:inkscape=', 1)
    open(pfad, "w", encoding="utf-8").write(svg)
    root = inkex.load_svg(pfad).getroot()
    assert svg_lesen.lesen(root, platte=1)["reliefs"] == []
    i2 = svg_lesen.lesen(root, platte=2)
    assert (i2["breite"], i2["hoehe"]) == pytest.approx((140, 70))
    r = i2["reliefs"][0]
    assert r["ecken"][0] == pytest.approx((10, 60), abs=1e-6)            # oben links, mm ab unten links
    assert r["ecken"][1] == pytest.approx((10 + w, 60), abs=1e-6)
    erg = dokument.rechnen(pfad, str(tmp_path / "aus2"), platte=2)
    assert erg["ok"], erg.get("warnungen")
    text = open(erg["datei"], newline="").read()
    xs = [x for x in (float(v) for v in re.findall(r"X(-?[\d.]+)", text)) if abs(x) > 1e-9]   # ohne X0 (Park)
    ys = [y for y in (float(v) for v in re.findall(r"Y(-?[\d.]+)", text)) if abs(y) > 1e-9]
    assert min(xs) > 10 - 8 and max(xs) < 10 + w + 8                     # um das Modell auf Platte 2
    assert min(ys) > 60 - h - 8 and max(ys) < 60 + 8
    # Fräsergebnis wie der Reiter «3D» es holt: mit --platte 2 Grösse und Modell dieser Platte
    prog = tmp_path / "programme.json"
    prog.write_text(json.dumps(erg["programme"]), encoding="utf-8")
    import io
    import contextlib
    aus = io.StringIO()
    with contextlib.redirect_stdout(aus):
        relief_ergebnis.main([pfad, "--programme", str(prog), "--ordner", str(tmp_path / "e"),
                              "--genauigkeit", "grob", "--platte", "2"])
    e = json.loads(aus.getvalue())
    assert e["ok"], e
    breite = (e["nx"] - 1) * e["zelle"]
    hoehe = (e["ny"] - 1) * e["zelle"]
    assert 140 - 1e-6 <= breite < 140 + e["zelle"] + 1e-6 and 70 - 1e-6 <= hoehe < 70 + e["zelle"] + 1e-6
    assert e["anteil_zu_tief"] < 1                                       # das Modell liegt, wo gefräst wurde


def _block_voll(b=60.0, h=40.0, hoch=10.0):
    """Modell so gross wie der Block (Peter 26.10., Impeller 100 × 100): Sockel 4 hoch über die ganze Fläche,
    darauf eine Rippe bis an beide Ränder."""
    def quader(x0, y0, x1, y1, z0, z1):
        p = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
             (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
        f = [(4, 5, 6), (4, 6, 7), (0, 2, 1), (0, 3, 2), (0, 1, 5), (0, 5, 4), (1, 2, 6), (1, 6, 5),
             (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7)]
        return [[p[a], p[b_], p[c]] for a, b_, c in f]
    return np.array(quader(0, 0, b, h, 0, 4) + quader(0, 15, b, 25, 4, hoch), float)


def test_ueber_den_materialrand_hinaus(tmp_path):
    """«Über den Materialrand hinaus» (Peter 26.10.): ist das Modell so gross wie der Block, fährt die Fräsermitte
    ohne die Einstellung höchstens bis zur Kante; mit 4 mm bis 4 mm hinaus – sonst bliebe am Rand stehen."""
    import re
    from hirch_cam import dokument
    def xs_von(ueber):
        d = tmp_path / ("u%g" % ueber)
        d.mkdir()
        bea = [{"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 3, "aufmass": 0.5,
                "ueber_rand": ueber},
               {"id": "f1", "art": "relief_schlichten", "werkzeug": "K", "bahnabstand": 30, "ueber_rand": ueber}]
        svg = zeichnung(d, bea, dreiecke=_block_voll(), material=(60, 40, 20))
        erg = dokument.rechnen(svg, str(d / "aus"))
        assert erg["ok"], erg.get("warnungen")
        werte = {}
        for prog in erg["programme"]:
            text = open(prog["datei"], newline="").read()
            werte[prog["werkzeug"]] = [float(v) for v in re.findall(r"X(-?[\d.]+)", text)]
        return werte
    ohne, mit = xs_von(0), xs_von(4)
    for wz in ("C", "K"):
        assert max(ohne[wz]) <= 60 + 1e-6 and min(x for x in ohne[wz] if x != 0) >= -1e-6
        # Schruppen: der Fräser räumt bis 4 mm über die Kante (Mitte also einen Radius davor), Schlichten: die Mitte
        assert max(mit[wz]) > 60 + 0.5 and min(mit[wz]) < -0.5, wz
        assert max(mit[wz]) <= 64 + 1e-6 and min(mit[wz]) >= -4 - 1e-6


def test_konturen_je_form_schraubenlinie(tmp_path):
    """Strategie «Konturen je Form» (Peter 26.10.): die Kuppel wird als zusammenhängende Schraubenlinie geschlichtet –
    stetig fallend, ohne senkrechtes Zustellen; schneller als «kombiniert», nirgends zu tief."""
    import contextlib
    import io
    from hirch_cam import dokument
    from hirch_cam.ops import relief_ergebnis
    zeiten = {}
    for st in ("kombiniert", "konturen"):
        d = tmp_path / st
        d.mkdir()
        bea = [{"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 4, "aufmass": 0.5},
               {"id": "f1", "art": "relief_schlichten", "werkzeug": "K", "bahnabstand": 10, "strategie": st}]
        svg = zeichnung(d, bea, material=(100, 90, 25))
        erg = dokument.rechnen(svg, str(d / "aus"))
        assert erg["ok"]
        zeiten[st] = erg["bearbeitungen"]["f1"]["minuten"]
        pr = d / "p.json"
        pr.write_text(json.dumps(erg["programme"]), encoding="utf-8")
        aus = io.StringIO()
        with contextlib.redirect_stdout(aus):
            relief_ergebnis.main([svg, "--programme", str(pr), "--ordner", str(d / "e"), "--genauigkeit", "mittel"])
        e = json.loads(aus.getvalue())
        assert e["anteil_zu_tief"] < 0.05 and e["anteil_rest"] < 0.5, (st, e)
    assert zeiten["konturen"] < 0.8 * zeiten["kombiniert"], zeiten


def _kanaele():
    """Sockel 90 × 40 × 4, darauf Blöcke 12 hoch: zwei lange Kanäle 4,8 mm breit, dazu eine kurze Lücke 6,8 mm
    (Impeller 08.11.: enge Kanäle zwischen den Schaufeln, eine breitere Stelle)."""
    def quader(x0, y0, x1, y1, z0, z1):
        p = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
             (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
        f = [(4, 5, 6), (4, 6, 7), (0, 2, 1), (0, 3, 2), (0, 1, 5), (0, 5, 4), (1, 2, 6), (1, 6, 5),
             (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7)]
        return [[p[a], p[b], p[c]] for a, b, c in f]
    return np.array(quader(0, 0, 90, 40, 0, 4) + quader(5, 5, 25, 35, 4, 16) + quader(29.8, 5, 50, 35, 4, 16)
                    + quader(54.8, 5, 75, 35, 4, 16) + quader(81.8, 5, 86, 12, 4, 16), float)


def test_restmaterial_vorschlag_nach_flaeche(tmp_path):
    """Impeller 08.11.: vorgeschlagen wurde der grösste Fräser, der in die breiteste Lücke passt – in die engen
    Kanäle passte er nicht («nur Restmaterial» liess fast alles stehen). Jetzt der, der den Rest erreicht."""
    from hirch_cam import dokument
    bea = [{"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 4, "aufmass": 0.5}]
    svg = zeichnung(tmp_path, bea, dreiecke=_kanaele(), material=(90, 40, 20))
    erg = dokument.rechnen(svg, str(tmp_path / "aus"))
    knopf = [b for b in erg["pruefung"] if b.get("aktion") == "restmaterial"]
    assert knopf and knopf[0]["durchmesser"] <= 3, knopf


def test_restmaterial_folgt_und_schneide_zu_kurz(tmp_path, monkeypatch):
    """Impeller 08.11.: folgt ein 3D-Schruppen «nur Restmaterial», meldet das erste Schruppen seinen Rest nicht mehr
    (der Vorschlag ist erledigt). Und ein Fräser, der tiefer fräst als seine Schneide lang ist, bekommt eine
    Warnung – auch im 3D (vorher nur bei 2D-Bearbeitungen mit eingestellter Tiefe)."""
    import test_relief3d as T
    from hirch_cam import dokument
    monkeypatch.setattr(T, "KUGEL", dict(T.SCHAFT, durchmesser=3.0, schneidenlaenge=6.0))
    bea = [{"id": "s1", "art": "relief_schruppen", "werkzeug": "C", "zustellung": 4, "aufmass": 0.5},
           {"id": "s2", "art": "relief_schruppen", "werkzeug": "K", "zustellung": 4, "aufmass": 0.5, "rest": True}]
    svg = zeichnung(tmp_path, bea, dreiecke=_kanaele(), material=(90, 40, 20))
    erg = dokument.rechnen(svg, str(tmp_path / "aus"))
    assert not [b for b in erg["pruefung"] if b.get("aktion") == "restmaterial" and b["bearbeitung"] == "s1"]
    kurz = [b for b in erg["pruefung"] if "Schneide von Ø 3 mm ist aber nur 6 mm lang" in b["text"]]
    assert kurz and kurz[0]["bearbeitung"] == "s2" and kurz[0]["stufe"] == "warnung", erg["pruefung"]
