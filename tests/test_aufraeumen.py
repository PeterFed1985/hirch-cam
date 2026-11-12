"""Zeichnung aufräumen (Import): verbinden, schliessen, doppelte Linien."""
import pytest

from test_dokument import braucht_inkex

pytest.importorskip("shapely")


def _svg(tmp_path, inhalt):
    p = tmp_path / "a.svg"
    p.write_text('<svg xmlns="http://www.w3.org/2000/svg" width="300mm" height="200mm" '
                 'viewBox="0 0 300 200"><g id="ebene">%s</g></svg>' % inhalt, encoding="utf-8")
    return str(p)


@braucht_inkex
def test_stuecke_verbinden_und_schliessen(tmp_path):
    from hirch_cam import aufraeumen
    st = 'style="fill:none;stroke:#000000"'
    inhalt = (
        # Rechteck aus vier Linien (wie DXF), eine Linie umgedreht, Lücke 0,5 mm in der Ecke
        f'<path id="l1" d="M 10 10 L 60 10" {st}/>'
        f'<path id="l2" d="M 60 40 L 60 10" {st}/>'
        f'<path id="l3" d="M 60 40 L 10 40" {st}/>'
        f'<path id="l4" d="M 10 40 L 10 10.5" {st}/>'
        # Bogen + Linie, exakt anschliessend
        f'<path id="b1" d="M 100 10 C 120 10 120 30 100 30" {st}/>'
        f'<path id="b2" d="M 100 30 L 100 10" {st}/>'
        # andere Farbe: nicht mit dem Rechteck verbinden
        '<path id="rot" d="M 60 10 L 90 10" style="fill:none;stroke:#ff0000"/>'
        # offen mit grosser Lücke (3 mm): verbinden ja, schliessen nein
        f'<path id="u1" d="M 150 10 L 200 10 L 200 50" {st}/>'
        f'<path id="u2" d="M 200 50 L 150 50 L 150 13" {st}/>')
    erg = aufraeumen.rechnen(_svg(tmp_path, inhalt))
    z = erg["zahlen"]
    assert z["zuege"] == 3 and z["verbunden"] == 8
    assert z["geschlossen"] == 2 and z["luecken"] == [0.5]
    assert z["offen"] == 1
    assert sorted(erg["loeschen"]) == sorted(["l1", "l2", "l3", "l4", "b1", "b2", "u1", "u2"])
    ds = [n["d"] for n in erg["neu"]]
    assert sum(1 for d in ds if d.rstrip().endswith("Z")) == 2
    assert any(" C " in d for d in ds)              # Bogen bleibt Kurve
    assert "rot" not in erg["loeschen"]


@braucht_inkex
def test_doppelte_linien(tmp_path):
    from hirch_cam import aufraeumen
    inhalt = (
        # wie aus Illustrator: dasselbe Rechteck als Füllung und als Kontur
        '<rect id="f" x="10" y="10" width="40" height="30" style="fill:#000000;stroke:none"/>'
        '<path id="k" d="M 50 40 L 10 40 L 10 10 L 50 10 Z" style="fill:none;stroke:#000000"/>'
        # dieselbe Linie zweimal, einmal umgedreht
        '<path id="a" d="M 100 10 L 150 10" style="fill:none;stroke:#000000"/>'
        '<path id="b" d="M 150 10 L 100 10" style="fill:none;stroke:#000000"/>'
        # teilweise übereinander (10 mm gemeinsam)
        '<path id="t1" d="M 100 80 L 140 80" style="fill:none;stroke:#000000"/>'
        '<path id="t2" d="M 130 80 L 170 80" style="fill:none;stroke:#00ff00"/>')
    erg = aufraeumen.rechnen(_svg(tmp_path, inhalt))
    z = erg["zahlen"]
    assert z["doppelt"] == 2
    assert "f" in erg["loeschen"] and "k" not in erg["loeschen"]     # Kontur bleibt
    assert ("a" in erg["loeschen"]) != ("b" in erg["loeschen"])
    assert z["teilweise"] == 1
    assert any("übereinander" in s["text"] for s in erg["stellen"])


@braucht_inkex
def test_sauber_bleibt_unveraendert(tmp_path):
    from hirch_cam import aufraeumen
    inhalt = ('<rect id="r" x="10" y="10" width="40" height="30" style="fill:none;stroke:#0000ff"/>'
              '<circle id="c" cx="100" cy="50" r="20" style="fill:none;stroke:#0000ff"/>')
    erg = aufraeumen.rechnen(_svg(tmp_path, inhalt))
    assert not erg["geaendert"] and not erg["bericht"]


@braucht_inkex
def test_dxf_in_zoll(tmp_path):
    from hirch_cam import aufraeumen
    inhalt = '<path id="l" d="M 10 10 L 14 10 L 14 13 L 10 13 Z" style="fill:none;stroke:#000"/>'
    erg = aufraeumen.rechnen(_svg(tmp_path, inhalt), endung="dxf")
    assert erg["einheit"] and erg["einheit"]["faktor"] == pytest.approx(25.4)
