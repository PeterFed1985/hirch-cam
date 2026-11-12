"""Prüfen ohne Maschine (planung 04.11., Postprozessoren): jede mitgelieferte Vorlage schreibt die Musterdatei
`tests/daten/muster_alle.hir` (jede Bearbeitungsart, zweiseitig, Umspannen), und die Dateien werden so gelesen, wie
eine Steuerung sie liest:
- Kopf: Masseinheit mm (G21, bei WinPC-NC G71), bei Bögen die Ebene G17;
- Bögen G2/G3 nur, wo die Vorlage sie erlaubt; Bohrzyklen nur, wo die Vorlage sie kennt (WinPC-NC: nie G98, dort
  heisst das Unterprogramm);
- Spindel an, bevor geschnitten wird; Drehzahl, wo die Vorlage eine schreibt;
- Werkzeugwechsel: zwischen Spindel aus und wieder an immer ein Halt (M0, M6) – nie ein stiller Wechsel;
- sichere Höhe am Anfang (vor jeder Fahrt in X/Y) und am Ende, Programmende M30/M2;
- die Endprüfung (freigabe.py) ohne Fehler.
Ein Simulator (LinuxCNC, grbl-sim) läuft hier nicht – das steht im Bericht der Basis."""
import os
import re

import pytest

pytest.importorskip("shapely")

HIER = os.path.dirname(os.path.abspath(__file__))
MUSTER = os.path.join(HIER, "daten", "muster_alle.hir")
_WORT = re.compile(r"([A-Z])\s*([-+]?(?:\d+\.?\d*|\.\d+))")


def _vorlagen():
    import json
    ordner = os.path.join(HIER, "..", "hirch_cam", "post", "vorlagen")
    return {n[:-5]: json.load(open(os.path.join(ordner, n), encoding="utf-8"))
            for n in sorted(os.listdir(ordner)) if n.endswith(".json")}


VORLAGEN = _vorlagen()
SICHER = 20.0                    # Sicherheitshöhe der Musterdatei


@pytest.fixture(scope="module")
def ordner(tmp_path_factory):
    return tmp_path_factory.mktemp("post")


def _rechnen(ordner, schluessel, monkeypatch):
    from hirch_cam import dokument, hirch_relief, werkzeugdb
    if not hirch_relief.mit_ocl():
        pytest.skip("OpenCAMLib fehlt")
    monkeypatch.setenv("XDG_CONFIG_HOME", str(ordner / "konfig"))
    monkeypatch.setenv("APPDATA", str(ordner / "konfig"))
    monkeypatch.setenv("HIRCH_SPRACHE", "de")
    db = werkzeugdb.laden()
    db["maschine"]["postprozessor"] = schluessel
    werkzeugdb.schreiben(db)
    return dokument.rechnen(MUSTER, str(ordner / schluessel), "muster")


def _woerter(zeile, vorlage):
    z = re.sub(r"\([^)]*\)|\[[^\]]*\]", "", zeile).split(";")[0].strip().upper()     # Kommentare ( ) und [ ] (WinCNC)
    return [(b, float(v)) for b, v in _WORT.findall(z)]


def _code(woerter, b, *werte):
    return any(w == b and int(v) in werte and v == int(v) for w, v in woerter)


@pytest.mark.parametrize("schluessel", sorted(VORLAGEN))
def test_vorlage_mit_der_musterdatei(ordner, schluessel, monkeypatch):
    v = VORLAGEN[schluessel]
    assert isinstance(v.get("an_maschine_geprueft"), bool), "Kennzeichnung fehlt"
    assert v["an_maschine_geprueft"] == (schluessel == "richauto_a11")      # heute nur Peters Maschine
    erg = _rechnen(ordner, schluessel, monkeypatch)
    assert erg["ok"], erg.get("warnungen")
    assert erg["freigabe"]["stufe"] != "fehler", erg["freigabe"]["punkte"]
    boegen_da = False
    for datei in erg["dateien"]:
        name = os.path.basename(datei)
        with open(datei, newline="") as fh:
            zeilen = fh.read().splitlines()
        if "%" in zeilen:                         # WinPC-NC: was vor «%» steht, ist Kopf (Kommentar)
            zeilen = zeilen[zeilen.index("%") + 1:]
        mm = ebene = spindel = False
        xy_vorher = False
        erstes_z = letztes_z = None
        halt_seit_aus = True                      # zwischen M5 und M3 ein Halt?
        aus = False
        drehzahl = None
        modus = 0
        letzter_code = ""
        for nr, zeile in enumerate(zeilen, 1):
            w = _woerter(zeile, v)
            if not w:
                continue
            letzter_code = zeile.strip()
            if _code(w, "G", 21) and schluessel != "wincnc_mm" or (schluessel == "winpcnc_mm" and _code(w, "G", 71)) \
                    or (schluessel == "wincnc_mm" and _code(w, "G", 22)):          # WinCNC: G21 = cm, G22 = mm
                mm = True
            if _code(w, "G", 53, 28):
                # Maschinenkoordinaten bzw. Referenzpunkt (WinCNC «G53 Z0», Fanuc «G91 G28 Z0»): Z ganz oben
                if any(b == "Z" for b, _ in w):
                    letztes_z = float("inf")
                continue
            if _code(w, "G", 20, 70):
                pytest.fail("%s Zeile %d: Zoll (%s)" % (name, nr, zeile))
            if _code(w, "G", 17):
                ebene = True
            if _code(w, "G", 2, 3):
                boegen_da = True
                assert v.get("boegen", True), "%s Zeile %d: Bogen, die Vorlage erlaubt keine" % (name, nr)
                assert ebene, "%s Zeile %d: Bogen ohne G17 davor" % (name, nr)
                assert any(b in "IJ" for b, _ in w), "%s Zeile %d: Bogen ohne I/J" % (name, nr)
            if _code(w, "G", 81, 82, 83):
                assert v.get("bohrzyklus"), "%s Zeile %d: Bohrzyklus, die Vorlage kennt keinen" % (name, nr)
            if schluessel == "winpcnc_mm":
                assert not _code(w, "G", 98), "%s Zeile %d: G98 ist bei WinPC-NC ein Unterprogramm" % (name, nr)
            for b, val in w:
                if b == "S":
                    drehzahl = val
            if _code(w, "M", 0, 6) or (schluessel == "wincnc_mm" and any(b == "T" for b, _ in w)):
                halt_seit_aus = True                  # WinCNC: das T-Wort ruft das Wechsel-Makro
            if _code(w, "M", 5):
                aus, halt_seit_aus = True, False
            if _code(w, "M", 3):
                if aus:
                    assert halt_seit_aus, "%s Zeile %d: Werkzeugwechsel ohne Halt (M0/M6)" % (name, nr)
                spindel, aus = True, False
            hat_xy = any(b in "XY" for b, _ in w) and not _code(w, "G", 81, 82, 83)
            z = next((val for b, val in w if b == "Z"), None)
            if hat_xy and erstes_z is None:
                xy_vorher = True
            if z is not None:
                if erstes_z is None:
                    erstes_z = z
                letztes_z = z
            for g in (0, 1, 2, 3):
                if _code(w, "G", g):
                    modus = g
            if modus and any(b in "XYZ" for b, _ in w):
                assert spindel, "%s Zeile %d: geschnitten, bevor die Spindel läuft" % (name, nr)
        # RichAuto A11 (an Peters Maschine geprüft) kennt nur mm und bekommt kein G21
        assert mm or v["an_maschine_geprueft"], "%s: Masseinheit mm fehlt im Kopf" % name
        assert not xy_vorher, "%s: fährt in X/Y, bevor es auf sicherer Höhe ist" % name
        assert erstes_z is not None and erstes_z >= SICHER - 1e-6, "%s: beginnt auf Z %s" % (name, erstes_z)
        assert letztes_z >= SICHER - 1e-6, "%s: endet nicht auf sicherer Höhe (Z %s)" % (name, letztes_z)
        if any(re.search(r"\bM0?(30|2)\b", z) for z in v.get("ende") or []):      # WinCNC braucht kein M30
            assert re.match(r"^(N\d+\s*)?M0?(30|2)\b", letzter_code), "%s: endet mit %r" % (name, letzter_code)
        if any("{s}" in z for f in ("werkzeug", "kopf") for z in v.get(f) or []):
            assert drehzahl and drehzahl > 0, "%s: keine Drehzahl" % name
    if not v.get("boegen", True):
        assert not boegen_da
