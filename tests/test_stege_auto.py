"""Gerechnete Haltestege «auto» als Marken (Basis 26.10.2026): das Ergebnis von `rechnen` liefert je Kontur mit
stege_modus «auto» die Stegmitten (`bearbeitungen[id]["stege_auto"]`, Form wie beim Befehl `stege`). Setzt die
Basis daraus Marken und stellt auf «manuell», bleibt der G-Code gleich."""
import glob
import json
import os

import pytest

pytest.importorskip("inkex")

NS = ('xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
      'xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
      'xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"')

FORMEN = ('<rect id="a" x="10" y="10" width="100" height="10" style="fill:none;stroke:#1f5fd0"/>'
          '<rect id="b" x="10" y="35" width="60" height="40" style="fill:none;stroke:#1f5fd0"/>'
          '<circle id="c" cx="40" cy="55" r="10" style="fill:none;stroke:#1f5fd0"/>'
          '<path id="d" d="M 130 10 L 190 10 L 190 70 L 160 40 L 130 70 Z" style="fill:none;stroke:#1f5fd0"/>'
          '<rect id="t" x="125" y="5" width="70" height="70" style="fill:none;stroke:#00a000"/>')


def _kontur(modus, **mehr):
    k = {"id": "k1", "art": "kontur", "tiefe": 3, "zustellung": 1.5, "farbe": "#1f5fd0", "seite": "auto",
         "stege_modus": modus, "stege_anzahl": 3, "stege_breite": 5, "stege_hoehe": 1}
    k.update(mehr)
    return k


def _svg(pfad, e, marken="", formen=FORMEN, seiten="", breite=200, hoehe=80, dicke=3):
    attr = json.dumps(e).replace('"', "&quot;")
    pfad.write_text(
        f'<svg {NS} width="{breite}mm" height="{hoehe}mm" viewBox="0 0 {breite} {hoehe}" '
        f'hirch:materialdicke="{dicke}" hirch:einstellungen="{attr}">{seiten}'
        f'<g inkscape:groupmode="layer" id="l1">{formen}{marken}</g></svg>', encoding="utf-8")
    return str(pfad)


def _gcode(ordner):
    aus = {}
    for d in sorted(glob.glob(os.path.join(ordner, "*.tap"))):
        with open(d, newline="") as f:
            aus[os.path.basename(d)] = f.read()
    return aus


def _rundweg(tmp_path, e_auto, **svg_args):
    """auto rechnen → Marken aus stege_auto setzen, «manuell» → noch einmal rechnen. Liefert (alt, neu, stege)."""
    from hirch_cam import dokument, speicher
    speicher.SPEICHER.leeren()
    erg = dokument.rechnen(_svg(tmp_path / "auto.svg", e_auto, **svg_args), str(tmp_path / "a"))
    assert erg["ok"], erg.get("warnungen")
    alt = _gcode(tmp_path / "a")
    stege = {bid: st["stege_auto"] for bid, st in erg["bearbeitungen"].items() if st.get("stege_auto")}
    marken = "".join('<circle cx="%r" cy="%r" r="2.5" hirch:steg="%s" style="fill:#ffd400"/>'
                     % (m["svg"][0], m["svg"][1], bid) for bid, ms in stege.items() for m in ms)
    e_hand = json.loads(json.dumps(e_auto))
    for b in e_hand["bearbeitungen"]:
        if b["id"] in stege:
            b["stege_modus"] = "manuell"
    erg2 = dokument.rechnen(_svg(tmp_path / "hand.svg", e_hand, marken=marken, **svg_args), str(tmp_path / "h"))
    assert erg2["ok"], erg2.get("warnungen")
    assert not any("keiner Kontur nahe" in w for w in erg2["warnungen"])
    assert not any(st.get("stege_auto") for st in erg2["bearbeitungen"].values())     # nur bei «auto»
    _rundweg.bahnen = (erg["bahnen"], erg2["bahnen"])
    return alt, _gcode(tmp_path / "h"), stege


def _e(*bearbeitungen, **mehr):
    from hirch_cam import werkzeugdb as _wdb  # die Steuerung kommt aus der Maschine (27.10.)
    _wdb.maschine_setzen({"postprozessor": "vcarve_mm"})
    e = {"postprozessor": "vcarve_mm", "material": "hartholz", "bearbeitungen": list(bearbeitungen)}
    e.update(mehr)
    return e


TASCHE = {"id": "t1", "art": "tasche", "tiefe": 1, "zustellung": 1, "farbe": "#00a000"}


@pytest.mark.parametrize("fall", ["senkrecht", "bogen", "rampe", "schlichten"])
def test_marken_aus_stege_auto_gleicher_gcode(tmp_path, fall):
    mehr = {"senkrecht": {"anfahren": ""}, "bogen": {"anfahren": "bogen"},
            "rampe": {"anfahren": "", "rampe_winkel": 3},
            "schlichten": {"anfahren": "bogen", "schlichten": True, "schlicht_zugabe": 0.3}}[fall]
    alt, neu, stege = _rundweg(tmp_path, _e(TASCHE, _kontur("auto", **mehr)))
    # vier Ringe (a, b, das Loch c in b, d) – je 3 Stege
    assert len(stege["k1"]) == 3 * 4, stege
    assert all(set(m) == {"x", "y", "seite", "svg"} and m["seite"] == 1 for m in stege["k1"])
    if fall != "schlichten":
        assert alt and alt == neu
    else:
        # Schruppen gleich; das Schlichten legt bei «auto» eigene Stege (Anfang auf seiner Bahn, teils an
        # anderen Stellen als das Schruppen) – mit Marken liegen sie dort, wo das Schruppen sie hat
        b_alt, b_neu = _rundweg.bahnen
        assert b_alt["k1"] == b_neu["k1"] and b_alt["t1"] == b_neu["t1"]


def test_ohne_stege_kein_feld(tmp_path):
    from hirch_cam import dokument
    for k in (_kontur("keine"), _kontur("auto", tiefe=2)):           # «keine» bzw. nicht durch
        erg = dokument.rechnen(_svg(tmp_path / "x.svg", _e(k)), str(tmp_path / "x"))
        assert erg["ok"] and "stege_auto" not in erg["bearbeitungen"]["k1"]


def test_zweiseitig_stege_auto_auf_seite_2(tmp_path):
    """Seite 1 fräst 4 von 6 mm, Seite 2 trennt heraus: die Stege gehören zu Seite 2, Marken auf deren Seite."""
    e = _e({"id": "k1", "art": "kontur", "tiefe": 4, "zustellung": 2, "farbe": "#1f5fd0", "auf_seite": 1,
            "seite": "aussen", "stege_modus": "auto", "stege_anzahl": 3, "stege_breite": 5, "stege_hoehe": 1},
           {"id": "k2", "art": "kontur", "tiefe": 2.5, "zustellung": 2.5, "farbe": "#1f5fd0", "auf_seite": 2,
            "seite": "aussen", "stege_modus": "auto", "stege_anzahl": 3, "stege_breite": 5, "stege_hoehe": 1},
           eingerichtet=True, zweiseitig={"an": True, "achse": "x", "passloecher": False})
    seiten = ('<sodipodi:namedview id="nv"><inkscape:page id="p1" x="0" y="0" width="120" height="80"/>'
              '<inkscape:page id="p2" x="140" y="0" width="120" height="80"/></sodipodi:namedview>')
    formen = ('<rect id="r1" x="10" y="10" width="80" height="20" style="fill:none;stroke:#1f5fd0"/>'
              '<rect id="r2" x="150" y="50" width="80" height="20" style="fill:none;stroke:#1f5fd0"/>')
    alt, neu, stege = _rundweg(tmp_path, e, formen=formen, seiten=seiten, breite=120, dicke=6)
    assert "k1" not in stege and len(stege["k2"]) == 3
    for m in stege["k2"]:
        assert m["seite"] == 2
        x, y = m["svg"]
        assert 140 + 10 - 3.5 < x < 140 + 90 + 3.5 and 50 - 3.5 < y < 70 + 3.5, m
    assert alt == neu


def test_hand_marken_bleiben_wie_bisher(tmp_path):
    """Marken, die nicht gleichmässig liegen, rechnen wie bisher (Anfang in der grössten Lücke)."""
    from hirch_cam.ops import kontur as k
    ring = [(0.0, 0.0), (100.0, 0.0), (100.0, 10.0), (0.0, 10.0)]
    marken = [(20.0, 0.0), (60.0, 0.0), (100.0, 5.0)]
    alt = k._stege_von_hand(ring, marken, 5.0)
    assert k._stege_von_hand(ring, marken, 5.0, wie_auto=ring) == alt
    # gleichmässig ab dem Anfang (Abschnitt 220/3, Mitte bei 36,67 / 110 / 183,33) → genau wie «auto»
    gl = [(36.6667, 0.0), (100.0, 10.0), (26.6667, 10.0)]
    r2, iv = k._stege_von_hand(ring, gl, 5.0, wie_auto=ring)
    assert r2 == ring and iv == k._stege_intervalle(220.0, 3, 5.0)
