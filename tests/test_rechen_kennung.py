"""Neu rechnen nur, wenn sich etwas Gefrästes ändert (Peter, 21.10.)."""
import pytest

from hirch_cam import dokument

KOPF = ('<svg xmlns="http://www.w3.org/2000/svg" xmlns:hirch="https://hirch.net/cnc" width="200mm" '
        'height="100mm" viewBox="0 0 200 100" hirch:materialdicke="18">')
BLAU = '<rect id="k" x="20" y="20" width="40" height="30" style="fill:none;stroke:#0000ff"/>'


def _kennung(tmp_path, inhalt):
    p = tmp_path / "z.svg"
    p.write_text(KOPF + inhalt + "</svg>", encoding="utf-8")
    return dokument.analyse(str(p))["kennung"]


def test_schwarzes_objekt_aendert_nichts(tmp_path):
    k0 = _kennung(tmp_path, BLAU)
    assert k0
    schwarz = '<rect id="s" x="100" y="20" width="30" height="30" style="fill:none;stroke:#000000"/>'
    assert _kennung(tmp_path, BLAU + schwarz) == k0
    verschoben = schwarz.replace('x="100"', 'x="140"')
    assert _kennung(tmp_path, BLAU + verschoben) == k0


def test_gefraestes_objekt_verschoben(tmp_path):
    k0 = _kennung(tmp_path, BLAU)
    assert _kennung(tmp_path, BLAU.replace('x="20"', 'x="25"')) != k0
    assert _kennung(tmp_path, BLAU.replace("#0000ff", "#ff0000")) != k0


def test_3d_kennung_ohne_modell_aenderung_gleich():
    from hirch_cam import dokument as d
    info = {"breite": 100, "hoehe": 90, "dicke": 25, "elemente": [],
            "reliefs": [{"id": "m1", "info": {"daten": "d1", "stauchung": 1}, "ecken": [(0, 90), (60, 90), (0, 30)],
                         "seite": 1, "stege": [], "daten": "AAAA"}]}
    k0 = d.rechen_kennung({}, info, {}, 1)
    assert k0
    info2 = dict(info, reliefs=[dict(info["reliefs"][0], ecken=[(1, 90), (61, 90), (1, 30)])])
    assert d.rechen_kennung({}, info2, {}, 1) != k0
    info3 = dict(info, reliefs=[dict(info["reliefs"][0], daten="BBBB")])
    assert d.rechen_kennung({}, info3, {}, 1) != k0
