"""Unabhängige Prüfung der Eilgänge (Fräsbahnen 25.10.2026): fährt ein Eilgang durch Material, das nach den
Programmen davor (in der Reihenfolge des G-Codes, je Seite) noch steht?

Anders als kollision.pruefen (grobes Raster, Toleranz 0,03 mm) rechnet dies fein (Zelle 0,1 mm, Werkzeugform
wie simulation.Hoehenfeld) und meldet jede Stelle mit Tiefe. Nur für Tests und Messungen – nicht im Programm.

    python3 werkzeuge/eilgang_pruefen.py <ergebnis.json>   (oder aus Python: pruefen(erg))
"""
from __future__ import annotations

import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from hirch_cam.post import tap_mm  # noqa: E402
from hirch_cam.simulation import Hoehenfeld  # noqa: E402

TOLERANZ = 0.02     # mm


def _lesen(pfad):
    with open(pfad, newline="") as f:
        return tap_mm.read(f.read())


def pruefen(erg, zelle: float = 0.1, toleranz: float = TOLERANZ, aussen_frei=None, werkzeuge=None):
    """erg: Ergebnis von engine.rechnen. aussen_frei: (x0, y0, x1, y1) des Rohteils je Seite in Maschinen-
    koordinaten – ausserhalb ist Luft (sonst gilt alles als Material bis Z 0, also strenger).
    werkzeuge (Sicherheitsprüfung 04.11.): mit den Werkzeugen des Auftrags auch «eintauchen» – ein Fräser ohne
    Mittenschnitt («mittenschnitt»: False), der senkrecht im Vorschub in stehendes Material taucht (verlangt ist
    eine Rampe oder Helix).
    Liefert [{"programm", "seite", "ort": (x, y), "z", "tief", "art": "eilgang" | "eintauchen"}] – je Programm
    alle Stellen."""
    jobs = [(p, _lesen(p["datei"])) for p in erg.get("programme") or []]
    if not jobs:
        return []
    xs = [m.x for _p, j in jobs for tp in j.toolpaths for m in tp.moves]
    ys = [m.y for _p, j in jobs for tp in j.toolpaths for m in tp.moves]
    rmax = max(float(p.get("durchmesser") or 3) for p, _j in jobs) / 2
    box = (min(xs) - rmax - 2, min(ys) - rmax - 2, max(xs) + rmax + 2, max(ys) + rmax + 2)
    felder = {}
    funde = []
    for p, job in jobs:
        seite = int(p.get("seite") or 1)
        if seite not in felder:
            h = Hoehenfeld(*box, zelle)
            if aussen_frei is not None:
                X, Y = h.koordinaten()
                a = aussen_frei.get(seite) if isinstance(aussen_frei, dict) else aussen_frei
                if a is not None:
                    h.H[(X < a[0]) | (X > a[2]) | (Y < a[1]) | (Y > a[3])] = -1e6
            felder[seite] = h
        h = felder[seite]
        form = p.get("form3d") or p.get("form") or "flach"
        d = float(p.get("durchmesser") or 3)
        er = float(p.get("eckradius") or 0)
        if form == "kugel" and er <= 0:
            er = d / 2
        n, prof = h._stempel(form, d, float(p.get("v_winkel") or 60), float(p.get("spitze") or 0), er)
        # Eilgänge: flacher Fräser mit dem Radius − ¾ Zelle – das Raster liess sonst einen Eilgang, der genau neben
        # der stehenden Zugabe an der Wand fährt, die Wand streifen (Zufall 07.11., Fall 3621: 0,05 mm daneben)
        n_e, prof_e = (h._stempel(form, d - 1.5 * h.c, 60.0, 0.0, 0.0) if form == "flach" and d > 3 * h.c
                       else (n, prof))
        schritt = h.c / 2
        pos = (job.home[0], job.home[1], job.safe_z)
        w = (werkzeuge or {}).get(p.get("werkzeug")) or {}
        ohne_mitte = not w.get("mittenschnitt", True) and (w.get("typ") or "schaft") in ("schaft", "kugel", "eckradius",
                                                                                        "konisch")
        for tp in job.toolpaths:
            for m in tp.moves:
                q = (m.x, m.y, m.z)
                # senkrecht oder steiler als 30° (Helix mit winzigem Radius, steile Rampe)
                if ohne_mitte and not m.rapid and q[2] < pos[2] - 1e-6 and \
                        pos[2] - q[2] > math.dist(pos[:2], q[:2]) * math.tan(math.radians(30)):
                    # senkrecht hinunter: wie tief steht unter der Mitte (Ø/4) noch Material über dem Ziel?
                    m_ = max(1, n // 2)
                    tief = _eingriff(h, q[0], q[1], q[2], m_, prof[n - m_:n + m_ + 1, n - m_:n + m_ + 1])
                    if tief > max(toleranz, 0.1):
                        funde.append({"programm": p.get("name", ""), "seite": seite, "ort": q[:2], "z": q[2],
                                      "tief": tief, "art": "eintauchen"})
                # senkrecht hinauf aus dem Schnitt ist immer frei (wie in der Endprüfung)
                hinauf = m.rapid and q[2] > pos[2] and math.dist(pos[:2], q[:2]) < 1e-6
                if min(pos[2], q[2]) < 0 and not hinauf:
                    l = math.dist(pos, q)
                    k = max(1, int(math.ceil(l / schritt)))
                    for s in range(0, k + 1):
                        t = s / k
                        x, y, z = (pos[0] + (q[0] - pos[0]) * t, pos[1] + (q[1] - pos[1]) * t,
                                   pos[2] + (q[2] - pos[2]) * t)
                        if m.rapid:
                            tief = _eingriff(h, x, y, z, n_e, prof_e)
                            if tief > toleranz:
                                funde.append({"programm": p.get("name", ""), "seite": seite, "ort": (x, y),
                                              "z": z, "tief": tief, "art": "eilgang"})
                                break
                        else:
                            h._stempeln(x, y, z, n, prof)
                pos = q
    return funde


def _eingriff(h, x, y, z, n, prof) -> float:
    """Wie tief das Werkzeug an (x, y, z) im stehenden Material steckt (0 = frei)."""
    if z >= 0:
        return 0.0
    i = int(round((x - h.x0) / h.c))
    j = int(round((y - h.y0) / h.c))
    i0, i1 = max(0, i - n), min(h.nx, i + n + 1)
    j0, j1 = max(0, j - n), min(h.ny, j + n + 1)
    if i0 >= i1 or j0 >= j1:
        return 0.0
    sub = prof[j0 - (j - n):j1 - (j - n), i0 - (i - n):i1 - (i - n)] + z
    d = h.H[j0:j1, i0:i1] - sub
    return float(max(0.0, np.max(d)))


if __name__ == "__main__":
    import json
    with open(sys.argv[1], encoding="utf-8") as f:
        e = json.load(f)
    for x in pruefen(e):
        print(x)
