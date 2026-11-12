"""Starttiefe («Beginnt bei Tiefe»), Bohrer als Werkzeug, Bohrzyklen und Fähigkeiten der Postprozessoren."""
import math

import pytest

pytest.importorskip("shapely")

from shapely.geometry import box  # noqa: E402

from hirch_cam import engine, post, werkzeugdb  # noqa: E402
from hirch_cam.model import Bohrzyklus, Job, Move, Toolpath  # noqa: E402
from hirch_cam.ops.bohren import bohren  # noqa: E402
from hirch_cam.ops.kontur import kontur  # noqa: E402
from hirch_cam.ops.tasche import tasche  # noqa: E402
from hirch_cam.post import tap_mm  # noqa: E402

QUADRAT = [([(0, 0), (40, 0), (40, 30), (0, 30), (0, 0)], True)]


@pytest.fixture(autouse=True)
def eigener_ordner(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))


def _ebenen(tp):
    """Tiefen, auf denen waagrecht gefräst wird."""
    aus = set()
    for a, b in zip(tp.moves, tp.moves[1:]):
        if not b.rapid and abs(a.z - b.z) < 1e-9 and math.hypot(b.x - a.x, b.y - a.y) > 1e-6 and b.z < 0:
            aus.add(round(b.z, 4))
    return sorted(aus)


# ---------------------------------------------------------------- Starttiefe
def test_tasche_ab_starttiefe_nur_dazwischen_zugestellt():
    tp, warn = tasche(QUADRAT, 6, 5.0, 1.5, 1000, 300, starttiefe=2.0)
    assert _ebenen(tp) == [-5.0, -3.5]            # 3 mm in 2 Schritten, nicht ab der Oberfläche
    # bis knapp über die Starttiefe im Eilgang
    assert any(m.rapid and abs(m.z - (-2.0 + 0.5)) < 1e-9 for m in tp.moves)
    assert all(m.rapid or m.z <= -1.5 + 1e-9 for m in tp.moves if m.z < 0.5)
    assert any(w.startswith("Beginnt bei Tiefe 2 mm: darüber wurde vorher nichts") for w in warn)


def test_tasche_starttiefe_ohne_warnung_wenn_vorher_geraeumt():
    vorher = [(box(-10, -10, 50, 40), 2.0)]
    _tp, warn = tasche(QUADRAT, 6, 5.0, 1.5, 1000, 300, starttiefe=2.0, ausgeraeumt=vorher)
    assert not any(w.startswith("Beginnt bei Tiefe") for w in warn)


def test_tasche_starttiefe_tiefer_als_tasche():
    tp, warn = tasche(QUADRAT, 6, 2.0, 1.0, 1000, 300, starttiefe=2.0)
    assert not tp.moves or all(m.z >= 0 for m in tp.moves)
    assert any("nichts zu fräsen" in w for w in warn)


def test_tasche_adaptiv_ab_starttiefe():
    from hirch_cam import cam
    if not (cam.verfuegbar() and hasattr(cam._hc, "adaptiv")):
        pytest.skip("CAM-Modul fehlt")
    tp, _w = tasche(QUADRAT, 6, 4.0, 1.0, 1000, 300, strategie="adaptiv", starttiefe=2.0)
    unten = [m.z for m in tp.moves if not m.rapid]
    assert min(unten) == pytest.approx(-4.0)
    assert {round(z, 3) for z in unten if z < -2.0 + 1e-9} >= {-3.0, -4.0}


def test_kontur_ab_starttiefe():
    tp, warn = kontur(QUADRAT, 6, 6.0, 2.0, 1000, 300, starttiefe=3.0)
    assert _ebenen(tp) == [-6.0, -4.5]
    assert any(m.rapid and abs(m.z - (-3.0 + 0.5)) < 1e-9 for m in tp.moves)
    assert any(w.startswith("Beginnt bei Tiefe 3 mm") for w in warn)
    _tp, warn2 = kontur(QUADRAT, 6, 6.0, 2.0, 1000, 300, starttiefe=3.0,
                        vorher=[(box(-20, -20, 60, 50), 3.0)])
    assert not any(w.startswith("Beginnt bei Tiefe") for w in warn2)


def test_ablauf_zeigt_start():
    from hirch_cam import ablauf
    z = ablauf.zustellungen("tasche", {"tiefe": 5, "zustellung": 1.5, "starttiefe": 2})
    assert z["start"] == 2 and z["n"] == 2 and z["je"] == pytest.approx(1.5)


# ---------------------------------------------------------------- Bohrer
def test_spitzenlaenge():
    assert werkzeugdb.spitzenlaenge({"durchmesser": 5, "spitzenwinkel": 118}) == pytest.approx(
        2.5 / math.tan(math.radians(59)))
    assert werkzeugdb.spitzenlaenge({"durchmesser": 6}) == pytest.approx(3 / math.tan(math.radians(59)))


def test_bohrer_in_der_datenbank_und_schnittdaten():
    db = werkzeugdb.laden()
    b5 = next(w for w in db["werkzeuge"] if w["id"] == "b5")
    assert b5["typ"] == "bohrer" and werkzeugdb.name({"typ": "bohrer", "durchmesser": 5}) == "Bohrer Ø 5 mm"
    sd = werkzeugdb.schnittdaten(b5, "hartholz")
    assert sd["eintauchen"] == sd["vorschub"]          # der Bohrer schneidet beim Eintauchen


def test_alte_datenbank_bekommt_die_bohrer(tmp_path):
    import json
    datei = tmp_path / "w.json"
    datei.write_text(json.dumps({"version": 1, "werkzeuge": [{"id": "x1", "name": "Mein Fräser", "typ": "schaft",
                                                              "durchmesser": 4}]}))
    db = werkzeugdb.laden(str(datei))
    ids = [w["id"] for w in db["werkzeuge"]]
    assert ids[0] == "x1" and {"b3", "b5", "b6"} <= set(ids)


def test_bohrer_keine_helix_und_meldet_unpassende_formen():
    gross = [([(10 + 6 * math.cos(a / 36 * math.pi), 10 + 6 * math.sin(a / 36 * math.pi)) for a in range(73)], True)]
    klein = [([(30 + 1 * math.cos(a / 36 * math.pi), 10 + 1 * math.sin(a / 36 * math.pi)) for a in range(73)], True)]
    tp, warn = bohren(gross + klein, 5, 4, 2, 300, methode="helix", bohrer=True)
    xy = {(round(m.x, 3), round(m.y, 3)) for m in tp.moves if not m.rapid}
    assert xy == {(10.0, 10.0), (30.0, 10.0)}            # nur in der Mitte, keine Schraube
    assert any("grösser als der Bohrer" in w for w in warn)
    assert any("kleiner als der Bohrer" in w for w in warn)
    assert len(tp.zyklen) == 2


def _auftrag_bohren(tmp_path, post_key, typ="bohrer", spitze=True):
    return {
        "werkzeuge": {"A": {"typ": typ, "durchmesser": 5, "nummer": 1, "vorschub": 400, "eintauchen": 400,
                            "drehzahl": 6000, "spitzenwinkel": 118},
                      "B": {"typ": "schaft", "durchmesser": 6, "nummer": 2, "vorschub": 1500, "eintauchen": 400,
                            "drehzahl": 16000}},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 18},
        "bohren": {"tiefe": 10, "zustellung": 4, "werkzeug": "A", "methode": "bohren", "spitze_dazu": spitze},
        "tasche": {"tiefe": 2, "zustellung": 1, "werkzeug": "A"},
        "postprozessor": post_key,
        "ausgabe": str(tmp_path / "teil.tap"),
        "objekte": {"bohren": [{"punkte": [[20 + 2.5 * math.cos(a / 18 * math.pi), 20 + 2.5 * math.sin(a / 18 * math.pi)]
                                           for a in range(37)], "geschlossen": True},
                               {"punkte": [[50 + 2.5 * math.cos(a / 18 * math.pi), 20 + 2.5 * math.sin(a / 18 * math.pi)]
                                           for a in range(37)], "geschlossen": True}],
                    "tasche": [{"punkte": [[60, 40], [90, 40], [90, 60], [60, 60]], "geschlossen": True}]},
    }


def test_engine_bohrer_spitze_dazu_und_nicht_fuer_tasche(tmp_path):
    erg = engine.rechnen(_auftrag_bohren(tmp_path, "linuxcnc_mm"))
    assert erg["ok"], erg
    alle = " ".join(erg["warnungen"])
    assert "ist ein Bohrer – er bohrt nur senkrecht" in alle          # Tasche mit Bohrer ausgelassen
    text = open(erg["dateien"][0], newline="").read()
    tiefe = 10 + werkzeugdb.spitzenlaenge({"durchmesser": 5, "spitzenwinkel": 118})
    assert f"Z-{tiefe:.3f}" in text
    # Vorschau und Prüfung kennen die Spitze (Kegel 118°)
    pr = next(p for p in erg["programme"] if p["werkzeug"] == "A")
    assert pr["form"] == "v" and pr["v_winkel"] == 118


# ---------------------------------------------------------------- Bohrzyklen
def test_linuxcnc_schreibt_bohrzyklen(tmp_path):
    erg = engine.rechnen(_auftrag_bohren(tmp_path, "linuxcnc_mm"))
    z = open(erg["dateien"][0], newline="").read().split("\r\n")
    zyklen = [l for l in z if l.startswith("G98 G83")]
    assert len(zyklen) == 2
    assert zyklen[0].startswith("G98 G83 X20.000 Y20.000 Z-11.502 R0.500 Q")
    i = z.index(zyklen[1])
    assert z[i + 1] == "G80"                        # nach der letzten Bohrung beendet
    assert z[i - 1] != "G80"                        # zwischen den Bohrungen nicht
    assert not any(l.startswith("G1 Z") for l in z[z.index(zyklen[0]):i + 1])
    assert any(l.startswith("(") and "Bohren" in l for l in z)       # Kommentar mit dem Namen


def test_a11_schreibt_bohrungen_als_bewegungen(tmp_path):
    erg = engine.rechnen(_auftrag_bohren(tmp_path, "richauto_a11"))
    text = open(erg["dateien"][0], newline="").read()
    assert "G83" not in text and "G81" not in text and "G80" not in text
    assert "G1Z-11.502F400.0" in text.replace(" ", "")


def test_grbl_haelt_beim_wechsel_mit_m0(tmp_path):
    a = _auftrag_bohren(tmp_path, "grbl_mm")
    a["kontur"] = {"tiefe": 3, "zustellung": 3, "werkzeug": "B", "stege_anzahl": 0}
    a["objekte"]["kontur"] = [{"punkte": [[0, 0], [100, 0], [100, 70], [0, 70]], "geschlossen": True}]
    erg = engine.rechnen(a)
    z = open(erg["dateien"][0], newline="").read().split("\r\n")
    assert "M0" in z and not any("M6" in l for l in z) and "G83" not in "".join(z)
    i = z.index("M0")
    assert z[i - 1].startswith("(Werkzeug T2 einsetzen")


def test_zyklus_faellt_auf_bewegungen_zurueck_wenn_er_nicht_passt():
    ms = [Move(True, 5, 5, 3), Move(True, 5, 5, 0.5), Move(False, 5, 5, -2, 200), Move(True, 6, 5, 3)]
    job = Job(toolpaths=[Toolpath(moves=ms, zyklen=[Bohrzyklus(1, 4)])], safe_z=20)
    z = tap_mm.koerper(job, zyklen={"einfach": ["G81 X{x} Y{y} Z{z} R{r} F{f}"]})
    assert not any(l.startswith("G81") for l in z)             # letzte Bewegung woanders: keine Bohrung
    ms[3] = Move(True, 5, 5, 3)
    z = tap_mm.koerper(job, zyklen={"einfach": ["G81 X{x} Y{y} Z{z} R{r} F{f}"]})
    assert "G81 X5.000 Y5.000 Z-2.000 R0.500 F200.0" in z and z[-1] == "G80"


def test_faehigkeiten_kommentar_zeilennummern_und_ohne_boegen():
    v = post.postprozessor("linuxcnc_mm")
    v.d = dict(v.d, zeilennummern=10, boegen=False)
    pts = [Move(True, 10, 0, 3), Move(False, 10, 0, -1, 300)] + [
        Move(False, 10 * math.cos(a / 18 * math.pi), 10 * math.sin(a / 18 * math.pi), -1, 1000) for a in range(1, 37)]
    job = Job(toolpaths=[Toolpath(name="Kreis (innen)", moves=pts)], safe_z=20)
    text = v.write(job, boegen=True, datei="t.ngc")
    z = text.split("\r\n")
    assert "(Kreis innen)" in z                    # Klammern im Namen entfernt
    assert not any(" G2 " in l or " G3 " in l for l in z)
    nummern = [l for l in z if l.startswith("N")]
    assert nummern[0].startswith("N10 ") and nummern[1].startswith("N20 ")
    assert z[0] == "%"                             # Prozentzeichen ohne Nummer


def test_liste_mit_faehigkeiten():
    l = post.liste()
    assert l[0][0] == post.STANDARD
    f = {e[0]: e[2] for e in l}
    assert f["linuxcnc_mm"]["bohrzyklen"] and not f["richauto_a11"]["bohrzyklen"]
    assert f["grbl_mm"]["endung"] == ".nc"


def test_speichern_behaelt_unbekannte_felder():
    from hirch_cam.post import verwalten
    d = {k: v for k, v in post.alle()["linuxcnc_mm"].items() if k in ("name", "kopf", "endung")}
    d["schluessel"] = "linuxcnc_mm"
    verwalten.speichern(d)                           # der Dialog schickt nicht alles mit
    assert post.alle()["linuxcnc_mm"]["bohrzyklus"]["entspanen"]


def test_maschine_waehlt_die_steuerung():
    """Die Steuerung kommt aus «Einstellungen › Maschine» – auch für ältere Zeichnungen mit eigenem
    Eintrag (Peter, 19.10.). Ohne Wahl dort gilt die Vorgabe – ein Eintrag der Zeichnung nie mehr (27.10.)."""
    from hirch_cam import einstellungen
    assert einstellungen.laden('{"postprozessor": "wincnc_mm"}')["postprozessor"] == "richauto_a11"
    werkzeugdb.maschine_setzen({"postprozessor": "grbl_mm"})
    assert einstellungen.laden(None)["postprozessor"] == "grbl_mm"
    assert einstellungen.laden('{"postprozessor": "wincnc_mm"}')["postprozessor"] == "grbl_mm"
