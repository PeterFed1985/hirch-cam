"""Endprüfung (freigabe.py) über mehrere Dateien derselben Seite – Windows 05.11. 19:18 (muster_alle): Schlichten mit
dem Kugelfräser und Spannung 2 fahren im Eilgang dort, wo eine frühere Datei schon ausgeräumt hat. Das ist frei.
Echte Eilgänge ins stehende Material werden weiter gemeldet."""
from hirch_cam import freigabe

KOPF = "G90\nG0 Z20\nM3 S18000\n"
SCHRUPPEN = KOPF + "G0 X10 Y10\nG1 Z-5 F300\nG1 X60\nG1 Y40\nG1 X10\nG1 Y10\nG0 Z20\nM30\n"
# fährt auf Z -5 im Eilgang von (10,10) nach (60,40): nur frei, wenn dort schon so tief gefräst wurde
EILGANG = KOPF + "G0 X10 Y10\nG1 Z-5 F300\nG0 X60 Y10\nG0 X60 Y40\nG1 X55 Y40\nG0 Z20\nM30\n"


def _schreiben(tmp_path, name, text):
    p = tmp_path / name
    p.write_text(text)
    return str(p)


def _eil(erg):
    return [q["text"] for q in erg["punkte"] if "Eilg" in q["text"] and q["stufe"] == "fehler"]


def test_fruehere_datei_derselben_seite_macht_frei(tmp_path):
    a = _schreiben(tmp_path, "a.TAP", SCHRUPPEN)
    b = _schreiben(tmp_path, "b.TAP", EILGANG)
    fr = {"a.TAP": (3.0, True, 1), "b.TAP": (3.0, False, 1)}
    assert _eil(freigabe.pruefen([a, b], 20, 0, 20, fraeser=fr)) == []
    # allein (oder ohne bekannte Seite) bleibt der Eilgang ein Fehler
    assert _eil(freigabe.pruefen([b], 20, 0, 20, fraeser=fr))
    assert _eil(freigabe.pruefen([a, b], 20, 0, 20, fraeser={k: v[:2] for k, v in fr.items()}))


def test_andere_seite_oder_spaetere_datei_macht_nicht_frei(tmp_path):
    a = _schreiben(tmp_path, "a.TAP", SCHRUPPEN)
    b = _schreiben(tmp_path, "b.TAP", EILGANG)
    # Seite 2 ist umgedreht: was Seite 1 ausgeräumt hat, zählt dort nicht
    assert _eil(freigabe.pruefen([a, b], 20, 0, 20, fraeser={"a.TAP": (3.0, True, 1), "b.TAP": (3.0, False, 2)}))
    # erst danach geschruppt: der Eilgang fährt noch ins Material
    assert _eil(freigabe.pruefen([b, a], 20, 0, 20, fraeser={"a.TAP": (3.0, True, 1), "b.TAP": (3.0, False, 1)}))
