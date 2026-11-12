"""Zweiseitig: die andere Seite blass zeigen – auch Objekte ohne Bearbeitung (Peter, 22.10.: erst beide
Seiten zeichnen und prüfen, ob alles übereinanderliegt, dann die Bearbeitungen)."""
import json

from test_dokument import braucht_inkex


def _svg(tmp_path, zweiseitig=True):
    e = {"eingerichtet": True, "material": "hartholz",
         "zweiseitig": {"an": zweiseitig, "achse": "x", "passloecher": False}, "bearbeitungen": []}
    attr = json.dumps(e).replace('"', "&quot;")
    p = tmp_path / "g.svg"
    p.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" '
        'xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
        'xmlns:sodipodi="http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd" width="120mm" height="80mm" '
        f'viewBox="0 0 120 80" hirch:materialdicke="18" hirch:einstellungen="{attr}">'
        '<sodipodi:namedview id="nv"><inkscape:page id="p1" x="0" y="0" width="120" height="80"/>'
        '<inkscape:page id="p2" x="140" y="0" width="120" height="80"/></sodipodi:namedview>'
        '<g inkscape:groupmode="layer" id="l1">'
        '<rect id="r1" x="10" y="10" width="40" height="25" style="fill:none;stroke:#000000"/>'
        '<circle id="c2" cx="170" cy="40" r="10" style="fill:none;stroke:#000000"/></g></svg>',
        encoding="utf-8")
    return str(p)


@braucht_inkex
def test_umrisse_auch_ohne_bearbeitung(tmp_path):
    from hirch_cam import dokument
    a = dokument.analyse(_svg(tmp_path), 1)
    assert len(a["umrisse"]["1"]) == 1 and len(a["umrisse"]["2"]) == 1
    xs = a["umrisse"]["1"][0][0::2]
    assert min(xs) == 10 and max(xs) == 50            # in den Koordinaten von Seite 1
    xs2 = a["umrisse"]["2"][0][0::2]
    assert 19.9 <= min(xs2) and max(xs2) <= 40.1       # Kreis um 30 (170 − 140) auf Seite 2


@braucht_inkex
def test_einseitig_keine_umrisse(tmp_path):
    from hirch_cam import dokument
    assert dokument.analyse(_svg(tmp_path, False), 1)["umrisse"] == {}
