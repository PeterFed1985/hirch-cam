"""Zinnteller als fester Vergleichstest (Peter 10.11.: «neue Datei, die V-Carve und Gravieren zeigt … darf ins
Repository»): `tests/daten/zinnteller_vcarve.hir`. Jede Änderung an seinen Bahnen fällt hier auf und wird bewusst
nachgeführt:

    HIRCH_REFERENZ_NEU=1 python3 -m pytest tests/test_referenz_zinnteller.py

Dazu eine Grenze für den Weg in der Luft (Leerfahrten, Peter 10.11.: «chaotisch») – wird die Reihenfolge wieder
schlechter, fällt das auf, auch wenn die Referenz neu geschrieben wird.
Die Texte der Datei (Schrift «Norm West 1L») wandelt die Oberfläche vor dem Rechnen in Pfade um – ohne Oberfläche
fehlen sie hier; ihre Reihenfolge prüft test_gravur_reihenfolge_text (drei Spalten in Einlinienschrift)."""
import gzip
import math
import os
import shutil
import sys

import pytest

pytest.importorskip("inkex")

HIER = os.path.dirname(os.path.abspath(__file__))
DATEI = os.path.join(HIER, "daten", "zinnteller_vcarve.hir")
ZIEL = os.path.join(HIER, "referenz", "zinnteller")
NEU = os.environ.get("HIRCH_REFERENZ_NEU") == "1"
LUFT_MAX_MM = 560.0       # V-Fräser-Programm: Weg in der Luft mit Anfahrt (vorher 604 mm, nach dem Ordnen 10.11. 520 mm)


def _luft(datei) -> float:
    from hirch_cam.post import tap_mm
    job = tap_mm.read(open(datei, newline="").read())
    pos, w = (job.home[0], job.home[1], job.safe_z), 0.0
    for tp in job.toolpaths:
        for m in tp.moves:
            if m.rapid:
                w += math.hypot(m.x - pos[0], m.y - pos[1])
            pos = (m.x, m.y, m.z)
    return w


def test_zinnteller_wie_referenz(tmp_path, monkeypatch):
    from hirch_cam import dokument
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "konfig"))
    monkeypatch.setenv("APPDATA", str(tmp_path / "konfig"))
    monkeypatch.setenv("HIRCH_SPRACHE", "de")
    erg = dokument.rechnen(DATEI, str(tmp_path / "aus"), "zinnteller")
    assert erg["ok"], erg.get("warnungen")
    assert erg["freigabe"]["stufe"] != "fehler", erg["freigabe"]["punkte"]
    vc = next(p for p in erg["programme"] if any(a["key"].startswith("b1") for a in p["abschnitte"]))
    assert _luft(vc["datei"]) < LUFT_MAX_MM, _luft(vc["datei"])
    dateien = sorted(erg["dateien"])
    if NEU:
        shutil.rmtree(ZIEL, ignore_errors=True)
        os.makedirs(ZIEL)
        for d in dateien:
            with open(d, "rb") as q, gzip.GzipFile(os.path.join(ZIEL, os.path.basename(d) + ".gz"), "wb", mtime=0) as z:
                shutil.copyfileobj(q, z)
        pytest.skip("Referenz neu geschrieben")
    from test_referenz_fluegel import _gleich
    erwartet = sorted(n[:-3] for n in os.listdir(ZIEL) if n.endswith(".gz"))
    assert [os.path.basename(d) for d in dateien] == erwartet
    if not sys.platform.startswith("linux"):
        return                     # andere Rechner: Bögen und Rundung leicht anders (wie Musterdatei)
    for d in dateien:
        ist = open(d, newline="").read().splitlines()
        with gzip.open(os.path.join(ZIEL, os.path.basename(d) + ".gz"), "rt", newline="") as z:
            soll = z.read().splitlines()
        for nr, (a, b) in enumerate(zip(ist, soll), 1):
            assert _gleich(a, b), "%s Zeile %d: %r statt %r" % (os.path.basename(d), nr, a, b)
        assert len(ist) == len(soll)


def test_gravur_reihenfolge_text():
    """Peter 10.11.: der Text graviert ein paar Zeilen, springt zur nächsten Spalte und wieder zurück. Drei Spalten
    mit je 10 Zeilen (Einlinienschrift): jede Spalte wird fertig graviert, bevor die nächste beginnt, und der Weg in
    der Luft ist kürzer als mit dem nächsten Nachbarn allein."""
    from hirch_cam import einlinien
    from hirch_cam.ops import gravur as G
    linien = []
    spalte_von = []
    for s, x0 in enumerate((0.0, 38.0, 76.0)):
        for z in range(10):
            text = "%d Nico Keller" % (1994 + s * 10 + z)          # etwa 28 mm breit, 10 mm Lücke
            for l in einlinien.text_linien(text, "Hershey Sans", 2.7, 1.6, True, 0.22, 0.5, 1, 0.8, "links", False):
                pts = [(x0 + x, 60.0 - 4.3 * z + y) for x, y in l]
                if len(pts) >= 2:
                    linien.append(G.Linie(pts, math.dist(pts[0], pts[-1]) < 1e-6))
                    spalte_von.append(s)

    def luft(folge):
        pos, w = (0.0, 0.0), 0.0
        for l in folge:
            w += math.dist(pos, l.punkte[0])
            pos = l.punkte[-1]
        return w
    neu = G._sortieren(list(linien), (0.0, 0.0), 1)
    breite = max(x for l in linien for x, _y in l.punkte if x < 38.0)
    assert breite < 34.0, breite
    # Spalten nicht verlassen und wieder betreten
    spalten = [int(l.punkte[0][0] // 38.0) for l in neu]
    wechsel = sum(1 for a, b in zip(spalten, spalten[1:]) if a != b)
    assert wechsel <= 3, wechsel                  # eine Spalte darf am Ende noch einmal drankommen (offener Weg)
    alt_gross = G.GROSS
    try:
        G.GROSS = 10 ** 9                 # nur der nächste Nachbar (wie vor dem 10.11.)
        alt = G._sortieren(list(linien), (0.0, 0.0), 1)
    finally:
        G.GROSS = alt_gross
    assert luft(neu) < luft(alt) * 0.95, (luft(neu), luft(alt))
