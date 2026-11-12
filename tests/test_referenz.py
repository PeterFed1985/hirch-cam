"""G-Code-Referenzen: feste Testzeichnungen → .tap, Zeile für Zeile verglichen.

Jede Änderung am Rechenkern, die Fräswege verändert, fällt hier auf. Ist die
Änderung gewollt, die Referenzen neu schreiben und den Unterschied prüfen:

    HIRCH_REFERENZ_NEU=1 python3 -m pytest tests/test_referenz.py
    git diff tests/referenz/erwartet
"""
import glob
import os
import shutil

import pytest

from test_dokument import braucht_inkex

HIER = os.path.join(os.path.dirname(__file__), "referenz")
ZEICHNUNGEN = sorted(glob.glob(os.path.join(HIER, "zeichnungen", "*.svg")))
NEU = os.environ.get("HIRCH_REFERENZ_NEU") == "1"


def _mit_kern(svg, kern, tmp_path):
    """Kopie der Zeichnung mit eingestelltem Rechenkern (CAM-Modul oder eigener)."""
    import html
    import json
    import re
    text = open(svg, encoding="utf-8").read()
    m = re.search(r'hirch:einstellungen="([^"]*)"', text)
    e = json.loads(html.unescape(m.group(1))) if m else {}
    e["rechenkern"] = kern
    attr = 'hirch:einstellungen="%s"' % html.escape(json.dumps(e, ensure_ascii=False), quote=True)
    text = text.replace(m.group(0), attr) if m else text.replace("<svg ", "<svg " + attr + " ", 1)
    pfad = tmp_path / os.path.basename(svg)
    pfad.write_text(text, encoding="utf-8")
    return str(pfad)


@braucht_inkex
@pytest.mark.parametrize("kern", ["auto", "intern"])
@pytest.mark.parametrize("svg", ZEICHNUNGEN, ids=[os.path.basename(z)[:-4] for z in ZEICHNUNGEN])
def test_gcode_wie_referenz(svg, kern, tmp_path, monkeypatch):
    from hirch_cam import cam, dokument
    monkeypatch.setenv("HIRCH_SPRACHE", "de")
    if kern == "auto" and not cam.verfuegbar():
        pytest.skip("CAM-Modul fehlt")
    name = os.path.basename(svg)[:-4]
    erg = dokument.rechnen(_mit_kern(svg, kern, tmp_path), str(tmp_path / "aus"), name)
    assert erg["ok"], erg.get("warnungen")
    ziel = os.path.join(HIER, "erwartet", "cam" if kern == "auto" else "intern", name)
    dateien = sorted(erg["dateien"])
    if NEU:
        shutil.rmtree(ziel, ignore_errors=True)
        os.makedirs(ziel)
        for d in dateien:
            shutil.copy(d, ziel)
        pytest.skip("Referenz neu geschrieben")
    if not os.path.isdir(ziel):
        pytest.skip("keine Referenz für diesen Rechenkern")
    erwartet = sorted(os.listdir(ziel))
    assert [os.path.basename(d) for d in dateien] == erwartet
    for d in dateien:
        ist = open(d, newline="").read().splitlines()
        soll = open(os.path.join(ziel, os.path.basename(d)), newline="").read().splitlines()
        for nr, (a, b) in enumerate(zip(ist, soll), 1):
            assert a == b, "%s Zeile %d: %r statt %r" % (os.path.basename(d), nr, a, b)
        assert len(ist) == len(soll), "%s: %d statt %d Zeilen" % (os.path.basename(d), len(ist), len(soll))
