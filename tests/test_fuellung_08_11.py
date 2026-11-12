"""Peter 08.11.: Ein Objekt nur mit Füllung (kein Text) wird nie gefräst – z. B. ein roter Kreis aus der Formerstellung
wäre sonst ungewollt V-Carve geworden. Die Füllfarbe zählt nur noch bei Text."""
import inkex

from hirch_cam import svg_lesen

KOPF = ('<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" width="200mm" height="100mm" '
        'viewBox="0 0 200 100">')


def _lesen(innen):
    svg = inkex.load_svg(bytes(KOPF + innen + "</svg>", "utf-8")).getroot()
    return svg_lesen.lesen(svg)


def test_nur_fuellung_wird_nie_gefraest():
    info = _lesen('<circle id="k" cx="50" cy="50" r="20" style="fill:#e00000;stroke:none"/>'
                  '<rect id="r" x="100" y="20" width="40" height="40" style="fill:#00a000"/>')
    assert not info["objekte"]
    assert info["gefuellt"] == 2
    assert any("nur eine Füllung" in h for h in svg_lesen.hinweise(info))


def test_linie_bestimmt_die_fraesart_auch_mit_fuellung():
    info = _lesen('<circle id="k" cx="50" cy="50" r="20" style="fill:#00a000;stroke:#e00000"/>')
    assert list(info["objekte"]) == ["vcarve"]
    assert info["gefuellt"] == 0


def test_text_behaelt_die_fuellfarbe():
    """In Pfade umgewandelter Text (hirch:text) hat von Inkscape nur eine Füllung."""
    info = _lesen('<g hirch:text="1"><path id="t" d="M 10 10 L 60 10 L 60 40 L 10 40 Z" '
                  'style="fill:#e00000;stroke:none"/></g>')
    assert list(info["objekte"]) == ["vcarve"]


def test_gefuellte_gruppe_vererbt_keine_fraesart():
    info = _lesen('<g style="fill:#e00000"><path id="p" d="M 10 10 L 60 10 L 60 40 Z"/></g>')
    assert not info["objekte"]
