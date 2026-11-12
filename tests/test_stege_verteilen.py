"""Haltestege als Marken (Entwurf B, 25.10.2026): gleichmässig verteilen, nicht in Ecken; Marken je Bearbeitung;
eine verschobene Marke rechnet nur ihre Kontur neu."""
import json
import math

import pytest

from hirch_cam.ops import stege

try:
    import inkex  # noqa: F401
    braucht_inkex = pytest.mark.skipif(False, reason="")
except ImportError:  # pragma: no cover
    braucht_inkex = pytest.mark.skip(reason="inkex fehlt")


def _lage(ring, p):
    """Weglänge des Punkts p auf dem geschlossenen Ring."""
    zu = list(ring) + [ring[0]]
    s, best = 0.0, (1e9, 0.0)
    for a, b in zip(zu, zu[1:]):
        l = math.dist(a, b)
        t = 0.0 if l == 0 else max(0.0, min(1.0, ((p[0] - a[0]) * (b[0] - a[0]) + (p[1] - a[1]) * (b[1] - a[1])) / l / l))
        q = (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
        if math.dist(p, q) < best[0]:
            best = (math.dist(p, q), s + l * t)
        s += l
    return best, s


def _ecken_abstand(ring, p):
    return min(math.dist(p, e) for e in ring)


def test_rechteck_wie_bisher_wenn_keine_ecke_im_weg():
    r = [(0, 0), (100, 0), (100, 10), (0, 10)]
    assert stege.verteilen(r, 4, 6, 6) == pytest.approx(stege.gleichmaessig_wie_bisher(r, 4, 6))


def test_steg_rueckt_aus_der_ecke_und_bleibt_gleichmaessig():
    r = [(0, 0), (100, 0), (100, 10), (0, 10)]
    alt = stege.gleichmaessig_wie_bisher(r, 3, 6)
    assert min(_ecken_abstand(r, p) for p in alt) < 1e-6          # bisher genau auf der Ecke (100, 10)
    neu = stege.verteilen(r, 3, 6, 6)
    assert len(neu) == 3
    for p in neu:
        (d, _s), _L = _lage(r, p)
        assert d < 1e-6                                           # auf der Fräsbahn
        assert _ecken_abstand(r, p) >= 3 + 6 - 1e-6               # halbe Breite + Fräser-Ø von jeder Ecke
    lagen = sorted(_lage(r, p)[0][1] for p in neu)
    L = 220.0
    luecken = [b - a for a, b in zip(lagen, lagen[1:])] + [L - lagen[-1] + lagen[0]]
    assert max(luecken) - min(luecken) < 0.2 * L / 3


def test_kreis_gleichmaessig_und_zu_klein_leer():
    c = [(20 * math.cos(2 * math.pi * i / 180), 20 * math.sin(2 * math.pi * i / 180)) for i in range(180)]
    p = stege.verteilen(c, 3, 6, 6)
    w = sorted(math.degrees(math.atan2(y, x)) % 360 for x, y in p)
    assert [b - a for a, b in zip(w, w[1:])] == pytest.approx([120, 120], abs=1.5)
    assert stege.verteilen([(0, 0), (10, 0), (10, 10), (0, 10)], 4, 6, 6) == []


def test_enger_bogen_wird_gemieden():
    """Rechteck mit einer engen Rundung (R 2) an einer Ecke und weiten Seiten: kein Steg in der Rundung."""
    bogen = [(78 + 2 * math.cos(w), 2 + 2 * math.sin(w))
             for w in (-math.pi / 2 + math.pi / 2 * i / 16 for i in range(17))]
    ring = [(0, 0)] + bogen + [(80, 40), (0, 40)]
    for n in (2, 3, 4, 5, 6):
        for p in stege.verteilen(ring, n, 6, 6):
            # Steg samt Fräser daneben (halbe Breite + Radius) klar neben dem Bogen
            assert min(math.dist(p, q) for q in bogen) >= 3 + 3 + 1, (n, p)


def _svg(tmp_path, e, marken=""):
    attr = json.dumps(e).replace('"', "&quot;")
    svg = tmp_path / "t.svg"
    svg.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
        'width="200mm" height="80mm" viewBox="0 0 200 80" hirch:materialdicke="3" '
        'hirch:einstellungen="%s">'
        '<rect id="a" x="10" y="10" width="100" height="10" style="fill:none;stroke:#1f5fd0"/>'
        '<rect id="t" x="130" y="10" width="30" height="30" style="fill:none;stroke:#00a000"/>'
        '%s</svg>' % (attr, marken), encoding="utf-8")
    return svg


def _e(modus="manuell"):
    from hirch_cam import werkzeugdb as _wdb  # die Steuerung kommt aus der Maschine (27.10.)
    _wdb.maschine_setzen({"postprozessor": "vcarve_mm"})
    return {"postprozessor": "vcarve_mm", "werkzeuge": None, "bearbeitungen": [
        {"id": "t1", "art": "tasche", "tiefe": 1, "zustellung": 1, "farbe": "#00a000"},
        {"id": "k1", "art": "kontur", "tiefe": 3, "zustellung": 3, "farbe": "#1f5fd0",
         "seite": "aussen", "stege_modus": modus, "stege_anzahl": 3,
         "stege_breite": 5, "stege_hoehe": 1}]}


def _steg_stellen(erg):
    from hirch_cam.post import tap_mm
    ms = []
    for d in erg["dateien"]:
        with open(d, newline="") as f:
            text = f.read()
        if "Y" in text and "-3.000" in text:                     # das Programm der Kontur
            ms = [m for tp in tap_mm.read(text).toolpaths for m in tp.moves if not m.rapid]
    z = sorted({round(m.z, 3) for m in ms})
    steg = z[0] + 1
    return sum(1 for a, b in zip(ms, ms[1:]) if abs(b.z - steg) < 1e-3 and abs(a.z - steg) > 1e-3)


@braucht_inkex
def test_verteilen_marken_setzen_rechnen(tmp_path):
    from hirch_cam import dokument
    e = _e()
    e.pop("werkzeuge")
    svg = _svg(tmp_path, e)
    v = dokument.stege_verteilen(str(svg), json.dumps(e), "k1", str(tmp_path / "s"))
    assert v["ok"] and len(v["stege"]) == 3, v
    assert all(m["seite"] == 1 for m in v["stege"])
    # die Marken an die gelieferten Stellen (Dokument-Koordinaten) setzen, der Bearbeitung zugeordnet
    marken = "".join('<circle cx="%g" cy="%g" r="2.5" hirch:steg="k1" style="fill:#ffd400"/>' % tuple(m["svg"])
                     for m in v["stege"])
    svg = _svg(tmp_path, e, marken)
    erg = dokument.rechnen(str(svg), str(tmp_path / "aus"))
    assert erg["ok"], erg
    assert _steg_stellen(erg) == 3
    assert not any("keiner Kontur nahe" in w for w in erg["warnungen"])
    assert "steg_ringe" not in erg                                  # nur auf Anfrage
    # Marke einer anderen Bearbeitung zählt hier nicht (und wird hier nicht als verwaist gemeldet)
    fremd = '<circle cx="150" cy="70" r="2.5" hirch:steg="k9" style="fill:#ffd400"/>'
    svg = _svg(tmp_path, e, marken + fremd)
    erg = dokument.rechnen(str(svg), str(tmp_path / "aus"))
    assert _steg_stellen(erg) == 3
    assert not any("keiner Kontur nahe" in w for w in erg["warnungen"])


@braucht_inkex
def test_marke_verschieben_rechnet_nur_die_kontur(tmp_path):
    from hirch_cam import dokument, speicher
    speicher.SPEICHER.leeren()
    e = _e()
    e.pop("werkzeuge")
    m1 = '<circle cx="40" cy="10" r="2.5" hirch:steg="1" style="fill:#ffd400"/>'
    erg = dokument.rechnen(str(_svg(tmp_path, e, m1)), str(tmp_path / "a1"))
    assert set(erg["neu_gerechnet"]) == {"t1", "k1"}
    m2 = '<circle cx="60" cy="10" r="2.5" hirch:steg="1" style="fill:#ffd400"/>'
    erg = dokument.rechnen(str(_svg(tmp_path, e, m2)), str(tmp_path / "a2"))
    assert erg["neu_gerechnet"] == ["k1"]


@braucht_inkex
def test_alte_zeichnung_ohne_marken_wie_bisher(tmp_path):
    """«auto» ohne Marken (alte Zeichnungen): Stege wie bisher gleichmässig verteilt."""
    from hirch_cam import dokument
    e = _e("auto")
    e.pop("werkzeuge")
    erg = dokument.rechnen(str(_svg(tmp_path, e)), str(tmp_path / "aus"))
    assert erg["ok"] and _steg_stellen(erg) == 3


@braucht_inkex
def test_zweiseitig_stege_auf_der_seite_die_heraustrennt(tmp_path):
    """Seite 1 fräst 4 von 6 mm, Seite 2 die restlichen 2 mm an derselben Stelle (gespiegelt): die Marken
    gehören zu Seite 2 und liegen auf deren Zeichenseite (rechts, x + 140)."""
    from hirch_cam import dokument
    from hirch_cam import werkzeugdb as _wdb  # die Steuerung kommt aus der Maschine (27.10.)
    _wdb.maschine_setzen({"postprozessor": "vcarve_mm"})
    e = {"eingerichtet": True, "material": "hartholz", "postprozessor": "vcarve_mm",
         "zweiseitig": {"an": True, "achse": "x", "passloecher": False}, "bearbeitungen": [
             {"id": "k1", "art": "kontur", "tiefe": 4, "zustellung": 4, "farbe": "#1f5fd0", "auf_seite": 1,
              "seite": "aussen", "stege_modus": "manuell", "stege_anzahl": 3, "stege_breite": 5,
              "stege_hoehe": 1},
             {"id": "k2", "art": "kontur", "tiefe": 2.5, "zustellung": 2.5, "farbe": "#1f5fd0", "auf_seite": 2,
              "seite": "aussen", "stege_modus": "manuell", "stege_anzahl": 3, "stege_breite": 5,
              "stege_hoehe": 1}]}
    attr = json.dumps(e).replace('"', "&quot;")
    p = tmp_path / "z.svg"
    # Seite 2 ist an X gespiegelt: Rechteck y 10..30 auf Seite 1 liegt auf Seite 2 bei y 50..70
    p.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
        'xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
        'xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd" width="120mm" height="80mm" '
        f'viewBox="0 0 120 80" hirch:materialdicke="6" hirch:einstellungen="{attr}">'
        '<sodipodi:namedview id="nv"><inkscape:page id="p1" x="0" y="0" width="120" height="80"/>'
        '<inkscape:page id="p2" x="140" y="0" width="120" height="80"/></sodipodi:namedview>'
        '<g inkscape:groupmode="layer" id="l1">'
        '<rect id="r1" x="10" y="10" width="80" height="20" style="fill:none;stroke:#1f5fd0"/>'
        '<rect id="r2" x="150" y="50" width="80" height="20" style="fill:none;stroke:#1f5fd0"/></g></svg>',
        encoding="utf-8")
    v1 = dokument.stege_verteilen(str(p), None, "k1", str(tmp_path / "s1"))
    assert v1["stege"] == [] and v1["warnungen"]                    # Seite 1 trennt nicht heraus
    v2 = dokument.stege_verteilen(str(p), None, "k2", str(tmp_path / "s2"))
    assert len(v2["stege"]) == 3 and all(m["seite"] == 2 for m in v2["stege"]), v2
    for m in v2["stege"]:
        x, y = m["svg"]
        assert 140 + 10 - 3.5 < x < 140 + 90 + 3.5 and 50 - 3.5 < y < 70 + 3.5, m
