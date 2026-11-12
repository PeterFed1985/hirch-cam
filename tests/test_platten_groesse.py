"""Platten mit eigener Grösse (Basis 26.10.2026, `hirch:platte-groesse`, Seite unten bündig mit Platte 1):
Platte N rechnet genau wie eine Zeichnung, die nur aus dieser Platte besteht – Nullpunkt, «ausserhalb des
Materials», Haltestege, zweiseitig gespiegelt um die eigene Breite."""
import glob
import json
import os

import pytest

pytest.importorskip("inkex")

NS = ('xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
      'xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
      'xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd"')


def _formen(dx, dy, aussen=False, spiegel=None):
    """Teil mit Loch und Tasche, um (dx, dy) verschoben (Dokument-Einheiten = mm, y nach unten).
    spiegel: Höhe der Seite – für Seite 2 (an X gespiegelt: y → Höhe − y)."""
    def y(v, h=0):
        return (spiegel - v - h if spiegel else v) + dy
    f = (f'<rect id="k{dx}" x="{10 + dx}" y="{y(10, 35)}" width="70" height="35" style="fill:none;stroke:#1f5fd0"/>'
         f'<rect id="c{dx}" x="{22 + dx}" y="{y(19, 16)}" width="16" height="16" style="fill:none;stroke:#1f5fd0"/>'
         f'<rect id="t{dx}" x="{50 + dx}" y="{y(18, 18)}" width="22" height="18" style="fill:none;stroke:#00a000"/>'
         f'<circle id="b{dx}" cx="{95 + dx}" cy="{y(15)}" r="2" style="fill:none;stroke:#ff0000"/>')
    if aussen:
        f += f'<rect id="x{dx}" x="{110 + dx}" y="{y(40, 10)}" width="30" height="10" style="fill:none;stroke:#1f5fd0"/>'
    return f


def _e(zweiseitig=False, nullpunkt=None):
    k = {"id": "k1", "art": "kontur", "tiefe": 6, "zustellung": 3, "farbe": "#1f5fd0", "seite": "auto",
         "stege_modus": "auto", "stege_anzahl": 3, "stege_breite": 5, "stege_hoehe": 1}
    from hirch_cam import werkzeugdb as _wdb  # die Steuerung kommt aus der Maschine (27.10.)
    _wdb.maschine_setzen({"postprozessor": "vcarve_mm"})
    e = {"postprozessor": "vcarve_mm", "material": "hartholz", "eingerichtet": True, "bearbeitungen": [
        {"id": "t1", "art": "tasche", "tiefe": 2, "zustellung": 1, "farbe": "#00a000"},
        {"id": "d1", "art": "bohren", "tiefe": 3, "farbe": "#ff0000"}, k]}
    if zweiseitig:
        k["auf_seite"] = 2
        k["tiefe"] = 3.5
        e["bearbeitungen"].append({"id": "g1", "art": "kontur", "tiefe": 3, "zustellung": 3, "farbe": "#1f5fd0",
                                   "auf_seite": 1, "seite": "auto", "stege_modus": "keine"})
        e["zweiseitig"] = {"an": True, "achse": "x", "passloecher": True}
    if nullpunkt:
        e["nullpunkt"] = nullpunkt
    return e


def _svg(pfad, e, seiten, formen, vb):
    attr = json.dumps(e).replace('"', "&quot;")
    pages = "".join(f'<inkscape:page id="p{i}" x="{x}" y="{y}" width="{w}" height="{h}"'
                    + (f' hirch:platte-groesse="{w}x{h}"' if gross else "") + "/>"
                    for i, (x, y, w, h, gross) in enumerate(seiten))
    pfad.write_text(
        f'<svg {NS} width="{vb[0]}mm" height="{vb[1]}mm" viewBox="0 0 {vb[0]} {vb[1]}" hirch:materialdicke="6" '
        f'hirch:einstellungen="{attr}"><sodipodi:namedview id="nv">{pages}</sodipodi:namedview>'
        f'<g inkscape:groupmode="layer" id="l1">{formen}</g></svg>', encoding="utf-8")
    return str(pfad)


def _gcode(ordner):
    return {os.path.basename(d): open(d, newline="").read() for d in sorted(glob.glob(os.path.join(ordner, "*.tap")))}


def _rechnen(svg, ordner, platte):
    from hirch_cam import dokument, speicher
    speicher.SPEICHER.leeren()
    erg = dokument.rechnen(svg, ordner, platte=platte)
    assert erg["ok"], erg.get("warnungen")
    return erg


@pytest.mark.parametrize("nullpunkt", [None, {"xy": "om", "dx": 2, "dy": -3}])
def test_platte2_eigene_groesse_wie_allein(tmp_path, nullpunkt):
    """Platte 1 200 × 100, Platte 2 130 × 60 rechts daneben, unten bündig (y = 100 − 60)."""
    e = _e(nullpunkt=nullpunkt)
    zwei = _svg(tmp_path / "zwei.svg", e, [(0, 0, 200, 100, False), (220, 40, 130, 60, True)],
                _formen(0, 0) + _formen(220, 40), (350, 100))
    allein = _svg(tmp_path / "allein.svg", e, [(0, 0, 130, 60, False)], _formen(0, 0), (130, 60))
    a = _rechnen(zwei, str(tmp_path / "a"), 2)
    b = _rechnen(allein, str(tmp_path / "b"), 1)
    assert _gcode(tmp_path / "a") == _gcode(tmp_path / "b")
    assert a["bearbeitungen"]["k1"]["stege_auto"][0]["svg"][0] > 220      # Marken auf Platte 2
    for m_a, m_b in zip(a["bearbeitungen"]["k1"]["stege_auto"], b["bearbeitungen"]["k1"]["stege_auto"]):
        assert (m_a["x"], m_a["y"]) == (m_b["x"], m_b["y"])
        assert m_a["svg"] == pytest.approx([m_b["svg"][0] + 220, m_b["svg"][1] + 40])


def test_platte2_ausserhalb_nach_eigener_groesse(tmp_path):
    """Ein Teil ragt über Platte 2 (130 breit) hinaus, läge aber in Platte 1 (200) – Hinweis «ausserhalb»."""
    zwei = _svg(tmp_path / "zwei.svg", _e(), [(0, 0, 200, 100, False), (220, 40, 130, 60, True)],
                _formen(0, 0) + _formen(220, 40, aussen=True), (360, 100))
    erg = _rechnen(zwei, str(tmp_path / "a"), 2)
    assert any("ausserhalb des Materials" in w for w in erg["warnungen"]), erg["warnungen"]
    erg1 = _rechnen(zwei, str(tmp_path / "b"), 1)
    assert not any("ausserhalb des Materials" in w for w in erg1["warnungen"])


def test_stege_verteilen_auf_platte2(tmp_path):
    from hirch_cam import dokument
    e = _e()
    zwei = _svg(tmp_path / "zwei.svg", e, [(0, 0, 200, 100, False), (220, 40, 130, 60, True)],
                _formen(0, 0) + _formen(220, 40), (350, 100))
    allein = _svg(tmp_path / "allein.svg", e, [(0, 0, 130, 60, False)], _formen(0, 0), (130, 60))
    v2 = dokument.stege_verteilen(zwei, None, "k1", str(tmp_path / "s2"), platte=2)
    v1 = dokument.stege_verteilen(allein, None, "k1", str(tmp_path / "s1"), platte=1)
    assert v2["ok"] and len(v2["stege"]) == len(v1["stege"]) >= 3
    for a, b in zip(v2["stege"], v1["stege"]):
        assert (a["x"], a["y"]) == (b["x"], b["y"])
        assert a["svg"] == pytest.approx([b["svg"][0] + 220, b["svg"][1] + 40])


def test_zweiseitig_platte2_eigene_groesse_wie_allein(tmp_path):
    """Zweiseitig: Platte 1 = Seiten 1/2 (200 × 100), Platte 2 = Seiten 3/4 (130 × 60, unten bündig). Seite 2 der
    Platte 2 ist um deren eigene Breite gespiegelt (Passlöcher, Haltestege der heraustrennenden Seite)."""
    e = _e(zweiseitig=True)
    zwei = _svg(tmp_path / "zwei.svg", e,
                [(0, 0, 200, 100, False), (220, 0, 200, 100, False), (440, 40, 130, 60, True),
                 (590, 40, 130, 60, True)],
                _formen(0, 0) + _formen(220, 0, spiegel=100) + _formen(440, 40) + _formen(590, 40, spiegel=60), (720, 100))
    allein = _svg(tmp_path / "allein.svg", e, [(0, 0, 130, 60, False), (150, 0, 130, 60, False)],
                  _formen(0, 0) + _formen(150, 0, spiegel=60), (280, 60))
    a = _rechnen(zwei, str(tmp_path / "a"), 2)
    b = _rechnen(allein, str(tmp_path / "b"), 1)
    ga, gb = _gcode(tmp_path / "a"), _gcode(tmp_path / "b")
    assert ga and ga == gb
    st_a, st_b = a["bearbeitungen"]["k1"]["stege_auto"], b["bearbeitungen"]["k1"]["stege_auto"]
    assert st_a and all(m["seite"] == 2 for m in st_a)
    for m_a, m_b in zip(st_a, st_b):
        assert m_a["svg"] == pytest.approx([m_b["svg"][0] + 440, m_b["svg"][1] + 40])


def test_zweiseitig_seiten_ungleich_gross_hinweis(tmp_path):
    """Nur Seite 3 bekam die eigene Grösse, Seite 4 nicht: Seite 2 wird um Seite 1 gespiegelt – Hinweis."""
    e = _e(zweiseitig=True)
    zwei = _svg(tmp_path / "zwei.svg", e,
                [(0, 0, 200, 100, False), (220, 0, 200, 100, False), (440, 40, 130, 60, True),
                 (590, 0, 200, 100, False)],
                _formen(0, 0) + _formen(220, 0, spiegel=100) + _formen(440, 40) + _formen(590, 0, spiegel=100),
                (790, 100))
    erg = _rechnen(zwei, str(tmp_path / "a"), 2)
    assert any("Seite 2 dieser Platte ist anders gross als Seite 1 (200 × 100 statt 130 × 60 mm)" in w
               for w in erg["warnungen"]), erg["warnungen"]
    erg1 = _rechnen(zwei, str(tmp_path / "b"), 1)
    assert not any("anders gross" in w for w in erg1["warnungen"])
