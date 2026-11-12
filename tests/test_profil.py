"""Profilpflege nach einem Update."""
from hirch_cam import profil


def test_oberflaeche_wie_entwurf(tmp_path):
    (tmp_path / "preferences.xml").write_text(
        '<?xml version="1.0"?><inkscape><group id="window"><group id="commands" state="1"/>'
        '</group><group id="toolbox"><group id="tools" small="1">'
        '<group id="buttons" show3DBox="0" showText="0"/>'
        '</group></group><group id="options" yaxisdown="0"/></inkscape>', encoding="utf-8")
    (tmp_path / "dialogs-state-ex.ini").write_text("[Windows]\n")
    assert profil.pflegen(str(tmp_path))
    text = (tmp_path / "preferences.xml").read_text(encoding="utf-8")
    assert 'showSpiral="0"' in text and 'showZoom="0"' in text and 'showPencil="0"' in text
    assert "showText" not in text and "showRect" not in text           # Entwurfs-Werkzeuge sichtbar
    assert '<group id="commands" state="0"' in text and 'id="snaptoolbox" state="1"' in text
    assert 'yaxisdown="0"' in text and 'small="1"' in text              # Rest bleibt
    assert '<group id="transform" stroke="0"' in text                  # Linienstärke fest
    assert not (tmp_path / "dialogs-state-ex.ini").exists()
    assert not profil.pflegen(str(tmp_path))                            # nur einmal


def test_von_stufe_2_nur_linienstaerke(tmp_path):
    (tmp_path / ".hirch-stufe").write_text("2")
    (tmp_path / "preferences.xml").write_text(
        '<?xml version="1.0"?><inkscape><group id="toolbox"><group id="tools">'
        '<group id="buttons" showSpiral="1"/></group></group>'
        '<group id="options"><group id="transform" stroke="1" pattern="1"/></group></inkscape>',
        encoding="utf-8")
    (tmp_path / "dialogs-state-ex.ini").write_text("[Windows]\n")
    assert profil.pflegen(str(tmp_path))
    text = (tmp_path / "preferences.xml").read_text(encoding="utf-8")
    assert 'stroke="0"' in text and 'pattern="1"' in text
    assert 'showSpiral="0"' in text                                     # Werkzeuge (Stufe 4)
    assert not (tmp_path / "dialogs-state-ex.ini").exists()            # Stufe 8: Reiter «Ansicht» neu


def test_ohne_profil(tmp_path):
    assert profil.pflegen(str(tmp_path / "neu"))                       # erster Start: nichts zu tun


def test_doppelte_werkzeugleiste(tmp_path):
    """So sah Peters Profil aus: zwei Gruppen «toolbox», die erste ohne die
    ausgeblendeten Werkzeuge – Inkscape zeigte darum wieder alle."""
    (tmp_path / ".hirch-stufe").write_text("3")
    (tmp_path / "preferences.xml").write_text(
        '<?xml version="1.0"?><inkscape>'
        '<group id="toolbox" small="1"><group id="tools"><group id="buttons" showLPETool="0"/></group></group>'
        '<group id="toolbox"><group id="tools"><group id="buttons" showDropper="0"/></group></group>'
        '</inkscape>', encoding="utf-8")
    assert profil.pflegen(str(tmp_path))
    import xml.etree.ElementTree as ET
    leisten = [g for g in ET.parse(tmp_path / "preferences.xml").getroot() if g.get("id") == "toolbox"]
    for leiste in leisten:
        knopf = leiste.find("group").find("group")
        assert knopf.get("showDropper") == "0" and knopf.get("showPaintBucket") == "0"
        assert knopf.get("showText") is None


def test_neue_formen_schwarz(tmp_path):
    (tmp_path / ".hirch-stufe").write_text("4")
    (tmp_path / "preferences.xml").write_text(
        '<?xml version="1.0"?><inkscape><group id="tools"><group id="shapes">'
        '<eventcontext id="rect" style="fill:none;stroke:#00a000" usecurrent="1"/></group>'
        '<eventcontext id="text" usecurrent="1" style="fill:black"/></group></inkscape>', encoding="utf-8")
    assert profil.pflegen(str(tmp_path))
    text = (tmp_path / "preferences.xml").read_text(encoding="utf-8")
    assert 'id="rect" style="fill:none;stroke:#000000;stroke-width:0.5mm;" usecurrent="0"' in text
    assert "font-family:'Hershey Sans'" in text and 'id="text" usecurrent="0"' in text
    assert 'id="arc"' in text and 'id="pen"' in text
    assert 'unitType="4"' in text                       # Schriftgrösse in mm


def test_einrasten_und_ausrichten(tmp_path):
    (tmp_path / ".hirch-stufe").write_text("6")
    (tmp_path / "preferences.xml").write_text(
        '<?xml version="1.0"?><inkscape><group id="window"><group id="snaptoolbox" state="0"/></group>'
        '</inkscape>', encoding="utf-8")
    assert profil.pflegen(str(tmp_path))
    text = (tmp_path / "preferences.xml").read_text(encoding="utf-8")
    assert '<group id="snaptoolbox" state="1"' in text
    assert 'snap-path-intersection="1"' in text and 'snap-alignment="1"' in text
    assert 'objects-align-to="page"' in text
    assert (tmp_path / ".hirch-stufe").read_text() == str(profil.STUFE)
