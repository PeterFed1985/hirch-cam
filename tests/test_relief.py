"""3D-Fräsung: Modelle einlesen, Höhenkarten, Kennzahlen (Testflügel in tests/3d/)."""
import json
import os
import subprocess
import sys

import numpy as np
import pytest

pytest.importorskip("hirch_cam.hirch_relief", reason="Modul hirch_relief nicht gebaut "
                    "(relief_kern/bauen.sh)")

from hirch_cam import relief, relief_import  # noqa: E402

HIER = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HIER)
FLUEGEL_STL = os.path.join(HIER, "3d", "Fluegel_Test.stl")
FLUEGEL_STEP = os.path.join(HIER, "3d", "Fluegel_Test.step")
mit_step = pytest.mark.skipif(not relief_import.step_moeglich(), reason="ohne OpenCASCADE gebaut")


def quader(x0, y0, z0, x1, y1, z1):
    """Geschlossener Quader als 12 Dreiecke (aussen gegen den Uhrzeigersinn)."""
    p = np.array([[x0, y0, z0], [x1, y0, z0], [x1, y1, z0], [x0, y1, z0],
                  [x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1]], np.float32)
    f = [(0, 2, 1), (0, 3, 2), (4, 5, 6), (4, 6, 7), (0, 1, 5), (0, 5, 4),
         (1, 2, 6), (1, 6, 5), (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7)]
    return p[np.array(f)]


def stl_binaer(pfad, d):
    with open(pfad, "wb") as f:
        f.write(b"solid aber binaer".ljust(80, b" "))
        f.write(np.uint32(len(d)).tobytes())
        a = np.zeros(len(d), dtype=[("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")])
        a["v"] = d
        f.write(a.tobytes())


# --------------------------------------------------------------------------------------------
# Formate

def test_stl_binaer_und_text(tmp_path):
    d = quader(0, 0, -5, 40, 20, 0)
    b = tmp_path / "q.stl"
    stl_binaer(b, d)
    t = tmp_path / "q_text.stl"
    zeilen = ["solid q"]
    for tri in d:
        zeilen += ["facet normal 0 0 0", "outer loop"] + \
                  ["vertex %g %g %g" % tuple(p) for p in tri] + ["endloop", "endfacet"]
    t.write_text("\n".join(zeilen + ["endsolid q"]))
    for pfad in (b, t):
        m = relief_import.lesen(str(pfad))
        assert m.dreiecke.shape == (12, 3, 3)
        np.testing.assert_allclose(m.dreiecke, d)


def test_obj_mit_vierecken(tmp_path):
    p = tmp_path / "w.obj"
    p.write_text("# Würfel\nv 0 0 0\nv 10 0 0\nv 10 10 0\nv 0 10 0\n"
                 "v 0 0 10\nv 10 0 10\nv 10 10 10\nv 0 10 10\n"
                 "f 1 4 3 2\nf 5/1 6/2 7/3 8/4\nf 1//1 2//1 6//1 5//1\n"
                 "f 2 3 7 6\nf 3 4 8 7\nf -4 -8 -5 -1\n")
    m = relief_import.lesen(str(p))
    assert m.dreiecke.shape == (12, 3, 3)
    k = relief.kennzahlen(relief.hoehenkarten(m, zelle=0.5))
    assert k["masse"] == [10.0, 10.0, 10.0]
    assert k["volumen_cm3"] == pytest.approx(1.0, rel=0.12)   # Rasterpunkte auf dem Rand


def test_unbekanntes_format_und_fehlende_datei(tmp_path):
    (tmp_path / "a.xyz").write_text("x")
    with pytest.raises(relief_import.EinleseFehler):
        relief_import.lesen(str(tmp_path / "a.xyz"))
    with pytest.raises(relief_import.EinleseFehler):
        relief_import.lesen(str(tmp_path / "fehlt.stl"))


# --------------------------------------------------------------------------------------------
# Höhenkarten

def test_quader_hoehenkarten_genau():
    m = relief_import.Modell("q", "stl", dreiecke=quader(10, 20, -8, 50, 45, 2))
    hk = relief.hoehenkarten(m, zelle=0.5)
    assert hk.oben.shape == (51, 81)
    assert np.nanmax(hk.oben) == pytest.approx(2) and np.nanmin(hk.unten) == pytest.approx(-8)
    # gemeinsame Kanten der Dreiecke: jeder Rasterpunkt gehört genau einer Fläche je Seite
    assert not hk.verdeckt.any()
    assert int(hk.maske.sum()) == 81 * 51 - 81 - 51 + 1     # Füllregel: obere/rechte Kante aussen


def test_hinterschnitt_erkannt():
    # zwei Platten übereinander: die obere Fläche der unteren ist von oben verdeckt
    d = np.concatenate([quader(0, 0, 0, 20, 20, 2), quader(5, 0, 6, 30, 20, 8)])
    hk = relief.hoehenkarten(relief_import.Modell("c", "stl", dreiecke=d), zelle=0.25)
    k = relief.kennzahlen(hk)
    assert k["hinterschnitt_mm2"] == pytest.approx(15 * 20, rel=0.05)


def test_ausrichtung_drehen_und_stauchen():
    m = relief_import.Modell("q", "stl", dreiecke=quader(0, 0, 0, 30, 10, 4))
    k = relief.kennzahlen(relief.hoehenkarten(m, {"drehen": [0, 0, 90], "z_faktor": 0.5},
                                              zelle=0.25))
    assert k["masse"] == [pytest.approx(10, abs=0.3), pytest.approx(30, abs=0.3), 2.0]
    # umdrehen (Unterseite nach oben): Z-Bereich gespiegelt
    k = relief.kennzahlen(relief.hoehenkarten(m, {"drehen": [180, 0, 0]}, zelle=0.25))
    assert k["z"] == [-4.0, 0.0]


# --------------------------------------------------------------------------------------------
# Testflügel

@pytest.fixture(scope="module")
def fluegel():
    return relief_import.lesen(FLUEGEL_STL)


def test_fluegel_stl(fluegel):
    assert len(fluegel.dreiecke) == 42678
    hk = relief.hoehenkarten(fluegel, zelle=0.25)
    k = relief.kennzahlen(hk)
    assert k["z"] == [pytest.approx(-13.63, abs=0.02), pytest.approx(26.63, abs=0.02)]
    assert k["volumen_cm3"] == pytest.approx(2865, rel=0.003)
    assert k["hinterschnitt_mm2"] == 0                      # zweiseitig vollständig fräsbar
    # Trennlinie schief: Nasenleiste sinkt zur Spitze auf etwa −7 mm
    tl = k["trennlinie"]
    assert tl[0] == pytest.approx(-7.0, abs=0.3) and tl[1] == pytest.approx(0.0, abs=0.3)
    # Servo-Schacht auf der Unterseite: Boden eben bei Z = 10 (X 170–220, Y 30–70)
    c = hk.zelle
    schacht = hk.unten[int(35 / c):int(65 / c), int(175 / c):int(215 / c)]
    np.testing.assert_allclose(schacht, 10.0, atol=1e-3)
    kanal = hk.unten[int(5 / c):int(25 / c), int(201 / c):int(205 / c)]
    np.testing.assert_allclose(kanal, 0.0, atol=1e-3)


def test_fluegel_info_befehl(tmp_path):
    png = tmp_path / "f.png"
    r = subprocess.run([sys.executable, "-m", "hirch_cam.dokument", "relief", FLUEGEL_STL,
                        "--bild", str(png), "--seite", "unten"],
                       cwd=REPO, capture_output=True, text=True, timeout=120)
    erg = json.loads(r.stdout)
    assert erg["ok"], erg
    assert erg["masse"] == [pytest.approx(521.7, abs=0.05), pytest.approx(501.0, abs=0.05),
                            pytest.approx(40.27, abs=0.05)]
    assert erg["dreiecke"] == 42678
    assert png.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"
    b, h = erg["bild_lage"]["mm"][2:]
    assert b == pytest.approx(521.7, abs=1.5) and h == pytest.approx(501.0, abs=1.5)


@mit_step
def test_fluegel_step():
    m = relief_import.lesen(FLUEGEL_STEP)
    arten = [f["art"] for f in m.flaechen]
    assert len(arten) == 15
    assert arten.count("zylinder") == 3                     # drei gerundete Schachtecken
    k = relief.kennzahlen(relief.hoehenkarten(m, zelle=0.25))
    assert k["volumen_cm3"] == pytest.approx(2865, rel=0.003)   # passt zur STL
    # Rand von Schacht und Kanal: Kanten zwischen Profilfläche und Wand, exakt mit Z
    rand = [kk["punkte"] for kk in relief.kanten(m, relief.ausrichtung())
            if "bspline" in kk["flaechen"] and kk["flaechen"].count("bspline") == 1
            and kk["punkte"][:, 1].max() <= 70.01 and 169.99 <= kk["punkte"][:, 0].min()
            and kk["punkte"][:, 0].max() <= 220.01]
    assert len(rand) == 11
    z = np.concatenate([p[:, 2] for p in rand])
    assert z.min() == pytest.approx(-12.90, abs=0.03) and z.max() == pytest.approx(-10.42, abs=0.03)


# --------------------------------------------------------------------------------------------
# Graustufenbild

def test_graustufen_16bit(tmp_path):
    Image = pytest.importorskip("PIL.Image")
    rampe = np.tile(np.linspace(0, 65535, 200).astype(np.uint16), (100, 1))
    p = tmp_path / "r16.png"
    Image.fromarray(rampe, mode="I;16").save(p)
    m = relief_import.lesen(str(p))
    assert m.bild_bits == 16 and not m.hinweise
    hk = relief.hoehenkarten(m, {"breite": 100.0, "hoehe": 8.0})
    assert hk.zelle == pytest.approx(0.5)                   # ein Bildpunkt = 0,5 mm
    assert np.nanmax(hk.oben) == pytest.approx(8.0, abs=0.05)
    assert np.nanmin(hk.oben) == pytest.approx(0.0, abs=0.05)
    assert np.all(np.diff(hk.oben[50]) >= -1e-5)            # links tief, rechts hoch


def test_graustufen_8bit_invertiert_und_hinweis(tmp_path):
    Image = pytest.importorskip("PIL.Image")
    a = np.zeros((40, 40), np.uint8)
    a[10:30, 10:30] = 255
    p = tmp_path / "q8.png"
    Image.fromarray(a, mode="L").save(p)
    m = relief_import.lesen(str(p))
    assert m.bild_bits == 8 and m.hinweise
    hk = relief.hoehenkarten(m, {"breite": 40.0, "hoehe": 3.0, "invertieren": True})
    assert hk.oben[20, 20] == pytest.approx(0.0) and hk.oben[2, 2] == pytest.approx(3.0)


def test_png_schreiben(tmp_path):
    Image = pytest.importorskip("PIL.Image")
    bild = np.dstack([np.arange(12, dtype=np.uint8).reshape(3, 4) * 20,
                      np.full((3, 4), 255, np.uint8)])
    relief.png_schreiben(str(tmp_path / "x.png"), bild)
    with Image.open(tmp_path / "x.png") as im:
        assert im.mode == "LA" and im.size == (4, 3)
        np.testing.assert_array_equal(np.asarray(im), bild)


def test_graustufen_masse_genau(tmp_path):
    Image = pytest.importorskip("PIL.Image")
    p = tmp_path / "k.png"
    Image.fromarray(np.full((30, 50), 128, np.uint8)).save(p)
    m = relief_import.lesen(str(p))
    for zelle in (None, 0.7):                    # Bildraster und anderes Raster
        k = relief.kennzahlen(relief.hoehenkarten(m, {"breite": 100.0}, zelle=zelle))
        assert k["masse"][0] == pytest.approx(100.0, abs=0.7)
        assert k["masse"][1] == pytest.approx(60.0, abs=0.7)


# --------------------------------------------------------------------------------------------
# In der Zeichnung speichern (einbetten) und Ansicht aus den gespeicherten Daten

def test_seite_oben_und_neigung():
    m = relief_import.Modell("q", "stl", dreiecke=quader(0, 0, 0, 30, 10, 4))
    for oben, dz in (("+Z", 4), ("-Z", 4), ("+Y", 10), ("-X", 30)):
        k = relief.kennzahlen(relief.hoehenkarten(m, {"oben": oben}, zelle=0.25))
        assert k["masse"][2] == pytest.approx(dz, abs=0.01), oben


def test_einbetten_und_ansicht(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CACHE_HOME", str(tmp_path / "cache"))
    a = tmp_path / "a"
    a.mkdir()
    # passt nur gedreht: 521,7 × 501 in 530 × 600 (Rand 10 mm)
    erg = relief.einbetten(FLUEGEL_STL, str(a), material=[530, 600, 50])
    assert erg["gedreht"] and erg["ausrichtung"]["drehen_z"] == 90
    assert erg["masse"][:2] == [pytest.approx(501.0, abs=0.05), pytest.approx(521.7, abs=0.05)]
    assert erg["rand_oben"] == pytest.approx(4.88, abs=0.02)            # mittig in 50 mm
    assert erg["raus_oben"] == 0 and erg["raus_unten"] == 0
    d = erg["dateien"]
    assert os.path.getsize(d["daten"]) < os.path.getsize(FLUEGEL_STL)
    # Netz für die 3D-Ansicht
    roh = open(d["netz"], "rb").read()
    nx, ny = np.frombuffer(roh[:8], "<i4")
    zelle = float(np.frombuffer(roh[8:12], "<f4")[0])
    assert max(nx, ny) <= 362 and len(roh) == 20 + 2 * 4 * nx * ny
    assert [nx, ny, zelle] == pytest.approx(erg["netz_raster"])
    # aus den gespeicherten Daten, zweimal (zweites Mal aus dem Zwischenspeicher)
    b = tmp_path / "b"
    b.mkdir()
    for _ in range(2):
        m = relief.modell_aus_daten(d["daten"], "Fluegel_Test.stl")
        e2 = relief.ansicht(m, str(b), erg["ausrichtung"], {"art": "oben"}, dicke=30)
        assert e2["masse"] == erg["masse"]
        assert e2["raus_oben"] == 0 and e2["raus_unten"] == pytest.approx(10.27, abs=0.02)
        assert e2["warnungen"]
    assert len(os.listdir(tmp_path / "cache" / "hirch-cam" / "relief")) == 1


def test_einbetten_befehl_bild(tmp_path, monkeypatch):
    Image = pytest.importorskip("PIL.Image")
    monkeypatch.setenv("XDG_CACHE_HOME", str(tmp_path / "cache"))
    p = tmp_path / "r.png"
    Image.fromarray(np.tile(np.linspace(0, 255, 60).astype(np.uint8), (30, 1))).save(p)
    r = subprocess.run([sys.executable, "-m", "hirch_cam.dokument", "relief", "einbetten", str(p),
                        "--ordner", str(tmp_path), "--material", "300,200,18",
                        "--ausrichtung", json.dumps({"breite": 120, "hoehe": 4}),
                        "--lage", json.dumps({"art": "oben"})],
                       cwd=REPO, capture_output=True, text=True, timeout=120,
                       env=dict(os.environ, XDG_CACHE_HOME=str(tmp_path / "cache")))
    erg = json.loads(r.stdout)
    assert erg["ok"], erg
    assert erg["art"] == "bild" and erg["bild_bits"] == 8
    assert erg["masse"][:2] == [pytest.approx(120, abs=2.1), pytest.approx(60, abs=2.1)]
    assert erg["z_versatz"] == pytest.approx(-4.0, abs=0.1)
    m = relief.modell_aus_daten(erg["dateien"]["daten"], "r.png")
    assert m.ist_bild and m.bild.shape == (30, 60)
