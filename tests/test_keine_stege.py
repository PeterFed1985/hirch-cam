"""Kontur «Keine Haltestege» (Peter, 22.10.): bewusst ohne Stege – keine Stege in der Bahn, keine Warnung
«ohne Haltesteg», auch wenn noch eine Anzahl eingetragen ist."""
import pytest

pytest.importorskip("shapely")

from hirch_cam import engine  # noqa: E402
from hirch_cam.ablauf import zusatz  # noqa: E402
from hirch_cam.pruefung import pruefen  # noqa: E402


def _auftrag(tmp_path, modus):
    q = [(10, 10), (90, 10), (90, 90), (10, 90), (10, 10)]
    return {
        "werkzeuge": {"C": {"typ": "schaft", "durchmesser": 6, "vorschub": 1500, "eintauchen": 300,
                            "schneidenlaenge": 20, "mittenschnitt": True}},
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": 18},
        "maschine": {"x": 600, "y": 900, "z": 150},
        "ausgabe": str(tmp_path / f"{modus}.tap"),
        "bearbeitungen": [{"id": "k", "art": "kontur", "name": "Kontur", "werkzeug": "C", "tiefe": 18.2,
                           "zustellung": 18.2, "durch": True, "seite": "aussen", "stege_modus": modus,
                           "stege_anzahl": 4, "stege_breite": 6, "stege_hoehe": 1.5,
                           "objekte": [{"punkte": q, "geschlossen": True}]}]}


def _tiefste_hebungen(erg):
    """Wie oft die Bahn auf der letzten Tiefe wieder hochgeht (Stege)."""
    import re
    z_alt, hoch = None, 0
    for zeile in open(erg["dateien"][0], encoding="utf-8"):
        m = re.search(r"Z(-?\d+\.?\d*)", zeile.split("(")[0])
        if not m:
            continue
        z = float(m.group(1))
        if z_alt is not None and z_alt < -18 and -18 < z < 0:
            hoch += 1
        z_alt = z
    return hoch


def test_keine_stege(tmp_path):
    a = _auftrag(tmp_path, "keine")
    erg = engine.rechnen(a)
    assert erg["ok"]
    assert _tiefste_hebungen(erg) == 0
    assert not any("Haltesteg" in b["text"] for b in pruefen(a, erg))
    assert zusatz("kontur", a["bearbeitungen"][0]) == "ohne Haltestege"


def test_gegenprobe_automatisch_hat_stege(tmp_path):
    erg = engine.rechnen(_auftrag(tmp_path, "auto"))
    assert _tiefste_hebungen(erg) >= 4
