"""Flügel als fester Vergleichstest (planung 04.11. 20:05): der am 04.11. erfolgreich gefräste Stand
(`tests/daten/fluegel_gefraest.hir`, zweiseitig mit Haut, 3D und 2D) ist die Referenz. Jede Änderung an seinen Bahnen
fällt hier auf und wird bewusst nachgeführt – wie test_referenz.py:

    HIRCH_REFERENZ_NEU=1 python3 -m pytest tests/test_referenz_fluegel.py
    git diff --stat tests/referenz/fluegel

Die Programme liegen gepackt (.tap.gz). Unter Linux (dort ist die Referenz gerechnet) wird Zeile für Zeile verglichen;
Zahlen dürfen um 0,001 mm abweichen. Unter Windows rechnet das 3D-Schruppen leicht anders (Windows 05.11.: einzelne
Zeilen mehr oder weniger in Programm 2, 4 und 6, gleich bei jedem Lauf) – dort wird das gefräste Teil verglichen:
je Seite das Höhenfeld aller Programme (Zelle 0,5 mm), höchstens 0,05 mm Unterschied ausser an Kanten
(unter Linux zum Ausprobieren: HIRCH_REFERENZ_HOEHENFELD=1)."""
import gzip
import os
import re
import shutil
import sys

import pytest

pytest.importorskip("hirch_cam.hirch_relief", reason="hirch_relief nicht gebaut")
pytest.importorskip("inkex")

HIER = os.path.dirname(os.path.abspath(__file__))
FLUEGEL = os.path.join(HIER, "daten", "fluegel_gefraest.hir")
ZIEL = os.path.join(HIER, "referenz", "fluegel")
NEU = os.environ.get("HIRCH_REFERENZ_NEU") == "1"
ZAHL = re.compile(r"([A-Z])(-?\d+(?:\.\d+)?)")


def _gleich(a: str, b: str) -> bool:
    if a == b:
        return True
    za, zb = ZAHL.findall(a), ZAHL.findall(b)
    if len(za) != len(zb) or ZAHL.sub("#", a) != ZAHL.sub("#", b):
        return False
    return all(x[0] == y[0] and abs(float(x[1]) - float(y[1])) <= 0.0011 for x, y in zip(za, zb))


def test_fluegel_wie_referenz(tmp_path, monkeypatch):
    from hirch_cam import dokument, hirch_relief
    if not hirch_relief.mit_ocl():
        pytest.skip("OpenCAMLib fehlt")
    # nur die Werkzeuge und Einstellungen aus der Datei, nicht die des Rechners; 3D frisch rechnen
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "konfig"))
    monkeypatch.setenv("APPDATA", str(tmp_path / "konfig"))
    monkeypatch.setenv("HIRCH_3D_SPEICHER", "aus")
    monkeypatch.setenv("HIRCH_SPRACHE", "de")
    erg = dokument.rechnen(FLUEGEL, str(tmp_path / "aus"), "fluegel")
    assert erg["ok"], erg.get("warnungen")
    dateien = sorted(erg["dateien"])
    if NEU:
        shutil.rmtree(ZIEL, ignore_errors=True)
        os.makedirs(ZIEL)
        for d in dateien:
            with open(d, "rb") as q, gzip.GzipFile(os.path.join(ZIEL, os.path.basename(d) + ".gz"), "wb", mtime=0) as z:
                shutil.copyfileobj(q, z)
        pytest.skip("Referenz neu geschrieben")
    erwartet = sorted(n[:-3] for n in os.listdir(ZIEL) if n.endswith(".gz"))
    assert [os.path.basename(d) for d in dateien] == erwartet
    if not sys.platform.startswith("linux") or os.environ.get("HIRCH_REFERENZ_HOEHENFELD") == "1":
        _wie_gefraest(erg, tmp_path)
        return
    for d in dateien:
        ist = open(d, newline="").read().splitlines()
        with gzip.open(os.path.join(ZIEL, os.path.basename(d) + ".gz"), "rt", newline="") as z:
            soll = z.read().splitlines()
        for nr, (a, b) in enumerate(zip(ist, soll), 1):
            assert _gleich(a, b), "%s Zeile %d: %r statt %r" % (os.path.basename(d), nr, a, b)
        assert len(ist) == len(soll), "%s: %d statt %d Zeilen" % (os.path.basename(d), len(ist), len(soll))


def _job(text, dz=0.0):
    """Ausgabedatei (nach Postprozessor) als Job: alle Bewegungen mit Koordinaten (freigabe.lesen); dz: so viel tiefer
    (Nullpunkt unten: die Oberfläche liegt bei der Materialdicke, das Höhenfeld rechnet mit 0)."""
    from hirch_cam import freigabe
    from hirch_cam.model import Job, Move, Toolpath
    ms = [Move(art == "G0", x, y, z - dz, None if art == "G0" else 1000.0)
          for art, x, y, z, *_r in freigabe.lesen(text)["bewegungen"]
          if art in ("G0", "G1") and None not in (x, y, z)]
    return Job(safe_z=max([m.z for m in ms] + [20.0]), toolpaths=[Toolpath(name="", moves=ms)])


def _wie_gefraest(erg, tmp_path):
    """Andere Rechner: je Seite das gefräste Teil (Höhenfeld) gleich wie aus der Referenz."""
    import numpy as np
    from scipy import ndimage
    from hirch_cam.simulation import Hoehenfeld
    import inkex
    from hirch_cam import einstellungen as E
    svg = inkex.load_svg(FLUEGEL).getroot()
    e = E.laden(svg.get(E.ATTRIBUT))
    dz = float(svg.get("{https://hirch.net/cnc}materialdicke") or 0) if (e.get("nullpunkt") or {}).get("z") == "unten" \
        else 0.0
    xs = []
    jobs = {}
    assert len(erg["programme"]) == len(erg["dateien"])
    for p, datei in zip(erg["programme"], erg["dateien"]):        # gleiche Reihenfolge (eine Datei je Programm)
        name = os.path.basename(datei)
        with open(datei, newline="") as fh:
            ist = _job(fh.read(), dz)
        with gzip.open(os.path.join(ZIEL, name + ".gz"), "rt", newline="") as z:
            soll = _job(z.read(), dz)
        jobs.setdefault(int(p.get("seite") or 1), []).append((p, ist, soll))
        xs += [(m.x, m.y) for j in (ist, soll) for tp in j.toolpaths for m in tp.moves]
    x0, y0 = min(x for x, _y in xs) - 5, min(y for _x, y in xs) - 5
    x1, y1 = max(x for x, _y in xs) + 5, max(y for _x, y in xs) + 5
    for seite, liste in jobs.items():
        felder = []
        for k in (1, 2):
            f = Hoehenfeld(x0, y0, x1, y1, 0.5)
            for p, ist, soll in liste:
                form = p.get("form3d") or p.get("form") or "flach"
                d = float(p["durchmesser"])
                f.fraesen(ist if k == 1 else soll, form, d, float(p.get("v_winkel") or 60),
                          eckradius=float(p.get("eckradius") or (d / 2 if form == "kugel" else 0)))
            felder.append(f.H)
        H0, H1 = felder[1], felder[0]
        # anders: tiefer als die Referenz in allen Nachbarzellen oder höher als in allen (eine halbe Zelle seitlicher
        # Versatz ist erlaubt – sonst zählte am schrägen Flügel jede Zelle)
        tiefer = ndimage.minimum_filter(H0, 3) - H1
        hoeher = H1 - ndimage.maximum_filter(H0, 3)
        d = np.maximum(tiefer, hoeher)
        anders = d > 0.05
        assert int(anders.sum()) == 0, "Seite %d: %d Zellen anders gefräst, bis %.2f mm" % (
            seite, int(anders.sum()), float(d.max()))
