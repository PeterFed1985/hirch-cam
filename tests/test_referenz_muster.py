"""Musterdatei als fester Vergleichstest (planung 04.11., Peter 05.11. 23:25 «Die Datei passt»):
`tests/daten/muster_alle.hir` – jede 2D- und 3D-Bearbeitungsart, zweiseitig, Umspannen. Jede Änderung an ihren Bahnen
fällt hier auf und wird bewusst nachgeführt:

    HIRCH_REFERENZ_NEU=1 python3 -m pytest tests/test_referenz_muster.py
    git diff --stat tests/referenz/muster

Unter Linux Programme ohne 3D Zeile für Zeile (Zahlen ±0,001 mm). Programme mit 3D – und auf anderen Rechnern alle –
rechnen je nach Rechner leicht anders (OpenCAMLib; Bögen der Konturen, Windows 06.11.) – für sie und für jede Seite als
Ganzes zählt das gefräste Teil (Höhenfeld, 0,5-mm-Raster, 0,05 mm)."""
import gzip
import os
import shutil
import sys

import pytest

pytest.importorskip("hirch_cam.hirch_relief", reason="hirch_relief nicht gebaut")
pytest.importorskip("inkex")

from test_referenz_fluegel import _gleich, _job  # noqa: E402

HIER = os.path.dirname(os.path.abspath(__file__))
MUSTER = os.path.join(HIER, "daten", "muster_alle.hir")
ZIEL = os.path.join(HIER, "referenz", "muster")
NEU = os.environ.get("HIRCH_REFERENZ_NEU") == "1"
ARTEN_3D = ("s3", "f3", "bw")          # Schlüssel der 3D-Bearbeitungen in der Musterdatei (muster_3d.py)


def _ist_3d(p) -> bool:
    return any(a["key"].split("@")[0].split("#")[0].startswith(ARTEN_3D) for a in p["abschnitte"])


def test_muster_wie_referenz(tmp_path, monkeypatch):
    from hirch_cam import dokument, hirch_relief
    if not hirch_relief.mit_ocl():
        pytest.skip("OpenCAMLib fehlt")
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "konfig"))
    monkeypatch.setenv("APPDATA", str(tmp_path / "konfig"))
    monkeypatch.setenv("HIRCH_3D_SPEICHER", "aus")
    monkeypatch.setenv("HIRCH_SPRACHE", "de")
    erg = dokument.rechnen(MUSTER, str(tmp_path / "aus"), "muster")
    assert erg["ok"], erg.get("warnungen")
    # Endprüfung ohne Fehlalarm (Windows 05.11. 19:18: Kugelfräser und Spannung 2 meldeten Eilgänge im Material)
    assert erg["freigabe"]["stufe"] != "fehler", erg["freigabe"]["punkte"]
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
    assert len(erg["programme"]) == len(erg["dateien"])
    for p, d in zip(erg["programme"], erg["dateien"]):
        # andere Rechner (Windows 06.11.: ein Zwischenpunkt auf einem Bogen einer 2D-Kontur anders, gleiche Form): dort
        # nur über das gefräste Teil – wie test_referenz_fluegel. Unter Linux Zeile für Zeile.
        if _ist_3d(p) or not sys.platform.startswith("linux"):
            continue
        ist = open(d, newline="").read().splitlines()
        with gzip.open(os.path.join(ZIEL, os.path.basename(d) + ".gz"), "rt", newline="") as z:
            soll = z.read().splitlines()
        for nr, (a, b) in enumerate(zip(ist, soll), 1):
            assert _gleich(a, b), "%s Zeile %d: %r statt %r" % (os.path.basename(d), nr, a, b)
        assert len(ist) == len(soll), "%s: %d statt %d Zeilen" % (os.path.basename(d), len(ist), len(soll))
    _wie_gefraest(erg)


def _wie_gefraest(erg):
    """Je Seite das gefräste Teil (Höhenfeld) gleich wie aus der Referenz (wie test_referenz_fluegel)."""
    import numpy as np
    from scipy import ndimage
    from hirch_cam.simulation import Hoehenfeld
    import inkex
    from hirch_cam import einstellungen as E
    svg = inkex.load_svg(MUSTER).getroot()
    e = E.laden(svg.get(E.ATTRIBUT))
    dz = float(svg.get("{https://hirch.net/cnc}materialdicke") or 0) if (e.get("nullpunkt") or {}).get("z") == "unten" \
        else 0.0
    xs, jobs = [], {}
    for p, datei in zip(erg["programme"], erg["dateien"]):
        with open(datei, newline="") as fh:
            ist = _job(fh.read(), dz)
        with gzip.open(os.path.join(ZIEL, os.path.basename(datei) + ".gz"), "rt", newline="") as z:
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
        tiefer = ndimage.minimum_filter(H0, 3) - H1
        hoeher = H1 - ndimage.maximum_filter(H0, 3)
        d = np.maximum(tiefer, hoeher)
        anders = d > 0.05
        assert int(anders.sum()) == 0, "Seite %d: %d Zellen anders gefräst, bis %.2f mm" % (
            seite, int(anders.sum()), float(d.max()))
