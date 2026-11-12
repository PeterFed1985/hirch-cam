"""Zufallstests der 3D-Sicherheit (planung 06.11., Peter 17:43 / 07.11. 11:47).

Ein Fall = ein Startwert. Daraus entsteht eine kleine Zeichnung mit einem zufälligen Höhenmodell (Hügel, Stufen,
steile Wände, Löcher), 3D-Schruppen, «Böden und Wände» und 3D-Schlichten mit zufälligen Werkzeugen, manchmal
zweiseitig (Achse x/y) mit Haut oder durchgefräst und Haltestegen. Geprüft wird mit den vorhandenen Prüfern:
- Eilgang durch stehendes Material (werkzeuge/eilgang_pruefen.py),
- nirgends tiefer als das Modell (relief_ergebnis «zu_tief»),
- Haut und Haltestege bleiben in voller Dicke stehen (zweiseitig),
- kein Absturz beim Rechnen.

Grosser Lauf nur auf Wunsch:

    PYTHONPATH=/root/bau/inkscape-src/share/extensions:. python3.12 tests/zufall/modell_3d.py 0 300
"""
import json
import os
import sys
import traceback

import numpy as np

HIER = os.path.dirname(os.path.abspath(__file__))
TESTS = os.path.dirname(HIER)
sys.path.insert(0, TESTS)
sys.path.insert(0, os.path.join(TESTS, "..", "werkzeuge"))

MAT = (90.0, 80.0, 14.0)
HALB = 15.0                     # Modell ±15 mm um die Mitte
N = 31                          # Gitterpunkte je Richtung (1 mm)


def _hoehen(rng, X, Y, zwei: bool):
    """Höhenfeld ≥ 0 aus zufälligen Bausteinen."""
    Z = np.zeros_like(X)
    for _ in range(rng.integers(1, 4)):
        art = rng.choice(["huegel", "stufe", "wand", "loch", "rippe"])
        cx, cy = rng.uniform(-8, 8, 2)
        if art == "huegel":
            s = rng.uniform(3, 9)
            Z += rng.uniform(2, 7) * np.exp(-((X - cx) ** 2 + (Y - cy) ** 2) / (2 * s * s))
        elif art == "stufe":
            a = rng.uniform(0, np.pi)
            d = (X - cx) * np.cos(a) + (Y - cy) * np.sin(a)
            Z += np.where(d > 0, rng.uniform(1, 4), 0.0)
        elif art == "wand":
            b, h = rng.uniform(1.5, 6), rng.uniform(1, 6)
            Z = np.maximum(Z, np.where((abs(X - cx) < b) & (abs(Y - cy) < rng.uniform(3, 12)), h, 0.0))
        elif art == "loch":
            r = rng.uniform(1.5, 5)
            Z = np.where((X - cx) ** 2 + (Y - cy) ** 2 < r * r, np.maximum(Z - rng.uniform(1, 5), 0.0), Z)
        else:
            a = rng.uniform(0, np.pi)
            d = abs((X - cx) * np.sin(a) - (Y - cy) * np.cos(a))
            Z += np.where(d < rng.uniform(0.6, 2.5), rng.uniform(1, 4), 0.0)
    if not zwei:
        Z += rng.uniform(0.5, 3)                    # Sockel, damit das Modell zusammenhängt
    return np.clip(Z, 0.0, MAT[2] / (2.3 if zwei else 1.3))


def dreiecke(rng, zwei: bool):
    g = np.linspace(-HALB, HALB, N)
    X, Y = np.meshgrid(g, g)
    Z = _hoehen(rng, X, Y, zwei)
    tri = []
    if not zwei:
        boden = -rng.uniform(1, 4)
        for i in range(N - 1):
            for j in range(N - 1):
                P = [np.array([X[a, b], Y[a, b], Z[a, b]]) for a, b in ((i, j), (i, j + 1), (i + 1, j + 1), (i + 1, j))]
                Q = [np.array([p[0], p[1], boden]) for p in P]
                tri += [(P[0], P[1], P[2]), (P[0], P[2], P[3]), (Q[0], Q[2], Q[1]), (Q[0], Q[3], Q[2])]
        # Seitenwände: ein geschlossener Körper wie jede echte STL-Datei (sonst sind die Schnitte offen)
        rand = [(0, j) for j in range(N)] + [(i, N - 1) for i in range(1, N)] + \
               [(N - 1, j) for j in range(N - 2, -1, -1)] + [(i, 0) for i in range(N - 2, 0, -1)] + [(0, 0)]
        for (a1, b1), (a2, b2) in zip(rand[:-1], rand[1:]):
            p1, p2 = np.array([X[a1, b1], Y[a1, b1], Z[a1, b1]]), np.array([X[a2, b2], Y[a2, b2], Z[a2, b2]])
            q1, q2 = np.array([p1[0], p1[1], boden]), np.array([p2[0], p2[1], boden])
            tri += [(p1, q1, q2), (p1, q2, p2)]
        return np.array(tri, np.float32)
    # zweiseitig: Linse aus oben +Z·f und unten −Z·u, rundum auf Z = 0 (Trennlinie)
    r2 = X * X + Y * Y
    rand = HALB - 1.0
    Zr = np.where(r2 < rand * rand, Z + 1.0, 0.0) * np.clip((rand * rand - r2) / 40.0, 0, 1)
    f, u = rng.uniform(0.6, 1.0), rng.uniform(0.3, 1.0)
    for i in range(N - 1):
        for j in range(N - 1):
            ecken = [(i, j), (i, j + 1), (i + 1, j + 1), (i + 1, j)]
            if not any(r2[a, b] < rand * rand for a, b in ecken):
                continue                     # aussen: Zr = 0, oben und unten treffen sich (geschlossen)
            P = [np.array([X[a, b], Y[a, b], f * Zr[a, b]]) for a, b in ecken]
            Q = [np.array([X[a, b], Y[a, b], -u * Zr[a, b]]) for a, b in ecken]
            tri += [(P[0], P[1], P[2]), (P[0], P[2], P[3]), (Q[0], Q[2], Q[1]), (Q[0], Q[3], Q[2])]
    return np.array(tri, np.float32)


def fall(nr: int, ordner: str):
    """Zeichnung des Falls nr: (svg, Beschreibung)."""
    from pathlib import Path
    from test_relief3d import steg_rect, zeichnung
    import test_relief3d as T
    rng = np.random.default_rng(1000 + nr)
    zwei = rng.random() < 0.45
    d_s = float(rng.choice([3.0, 4.0, 6.0, 8.0]))
    d_k = float(rng.choice([1.5, 2.0, 3.0, 4.0, 6.0]))
    kugel = rng.random() < 0.8
    alt = (T.SCHAFT, T.KUGEL)
    T.SCHAFT = {"typ": "schaft", "durchmesser": d_s, "laenge": 50, "mittenschnitt": True}
    T.KUGEL = ({"typ": "kugel", "durchmesser": d_k, "eckradius": d_k / 2, "laenge": 50, "mittenschnitt": True}
               if kugel else {"typ": "schaft", "durchmesser": d_k, "laenge": 50, "mittenschnitt": True})
    strat = str(rng.choice(["parallel", "hoehenlinien", "kombiniert", "ebenen", "kehlen", "konturen"]))
    S = {"art": "relief_schruppen", "werkzeug": "C", "zustellung": float(rng.choice([1.0, 2.0, 3.0, 5.0])),
         "aufmass": float(rng.choice([0.0, 0.3, 0.5, 1.0]))}
    F = {"art": "relief_schlichten", "werkzeug": "K", "bahnabstand": float(rng.choice([15, 25, 40])),
         "strategie": strat, "auslassen_fertig": bool(rng.random() < 0.5)}
    seiten = [1, 2] if zwei else [1]
    bea = []
    for s in seiten:
        bea.append(dict(S, id="s%d" % s, auf_seite=s))
        if rng.random() < 0.3:
            bea.append({"art": "boeden_waende", "werkzeug": "C", "id": "b%d" % s, "auf_seite": s})
        bea.append(dict(F, id="f%d" % s, auf_seite=s))
    p = Path(ordner)
    p.mkdir(parents=True, exist_ok=True)
    tri = dreiecke(rng, zwei)
    besch = {"nr": nr, "zwei": zwei, "schaft": d_s, "fein": T.KUGEL["typ"] + " %g" % d_k, "strategie": strat,
             "zustellung": S["zustellung"], "aufmass": S["aufmass"], "bw": any(b["art"] == "boeden_waende" for b in bea)}
    if not zwei:
        try:
            svg = zeichnung(p, bea, material=MAT, dreiecke=tri, lage={"art": "oben"})
        finally:
            T.SCHAFT, T.KUGEL = alt
        return svg, besch
    achse = str(rng.choice(["x", "y"]))
    haut = bool(rng.random() < 0.5)
    mz = {"an": True, "haut_an": haut, "haut": float(rng.choice([0.4, 0.8, 1.5])) if haut else 0.5,
          "steg_dicke": float(rng.choice([2.0, 3.0, 4.0])), "steg_breite": 8.0}
    el = ""
    n_st = int(rng.integers(0 if haut else 2, 4))
    for k in range(n_st):
        w = rng.uniform(0, 2 * np.pi)
        r = HALB - 1 + 17               # vom Modell bis über den Fräsbereich hinaus
        m = (MAT[0] / 2 + r * np.cos(w), MAT[1] / 2 + r * np.sin(w))
        el += steg_rect("st%d" % k, m, 8, 36, np.degrees(w) + 90, MAT[1])
    besch.update(achse=achse, haut=mz["haut"] if haut else None, stege=n_st, steg_dicke=mz["steg_dicke"])
    try:
        svg = zeichnung(p, bea, material=MAT, dreiecke=tri, lage={"art": "mittig"},
                        zweiseitig={"an": True, "achse": achse, "passloecher": False},
                        zusatz_info={"zweiseitig": mz}, elemente=el)
    finally:
        T.SCHAFT, T.KUGEL = alt
    return svg, besch


def pruefen(nr: int, ordner: str) -> dict:
    """Fall rechnen und prüfen: {"ok", "funde": [Text], "besch", "endpruefung": [Text]}."""
    os.environ["HIRCH_3D_SPEICHER"] = "aus"
    os.environ.setdefault("XDG_CONFIG_HOME", os.path.join(ordner, "konfig"))
    funde = []
    besch = {"nr": nr}
    try:
        from hirch_cam import dokument
        import eilgang_pruefen as ep
        svg, besch = fall(nr, ordner)
        erg = dokument.rechnen(svg, os.path.join(ordner, "aus"), "z")
        if not erg.get("ok"):
            return {"ok": True, "funde": [], "besch": besch, "nicht_gerechnet": erg.get("warnungen")}
        e = ep.pruefen(erg, aussen_frei=(0, 0, MAT[0], MAT[1]))
        if e:
            funde.append("Eilgang durch Material: %d Stellen, tiefste %.2f mm (Programm %s)"
                         % (len(e), max(f["tief"] for f in e), e[0]["programm"]))
        funde += _flaechen_pruefen(svg, erg, besch)
        endp = [x.get("text") for x in erg.get("pruefung") or []]
    except Exception:
        return {"ok": False, "funde": ["Absturz: " + traceback.format_exc(limit=3)], "besch": besch}
    return {"ok": True, "funde": funde, "besch": besch, "endpruefung": endp}


def _flaechen_pruefen(svg, erg, besch):
    import shapely
    from shapely.geometry import Polygon
    from test_3d_sicherheit_04_11 import _flaechen
    f = _flaechen(svg, erg, "mittel")
    z = f["zelle"]
    out = []
    m = f["modell"]
    import inkex
    from hirch_cam import einstellungen as E
    from hirch_cam.ops import relief_ergebnis as RE
    e = E.laden(inkex.load_svg(svg).getroot().get(E.ATTRIBUT))
    i = f["info"]
    r = RE.ergebnis(erg["programme"], (i["breite"], i["hoehe"], i["dicke"]), i["reliefs"],
                    dict(e.get("zweiseitig") or {}), os.path.join(os.path.dirname(svg), "erg"), "mittel")
    # Raster «mittel»: an steilen Flanken weichen einzelne Zellen ein, zwei Zehntel ab (Messfehler, nicht Bahn)
    if r["zu_tief"] > 0.3 or r["anteil_zu_tief"] > 0.01:
        out.append("zu tief ins Modell: %.2f mm (%.2f %% der Fläche)" % (r["zu_tief"], r["anteil_zu_tief"]))
    if not besch.get("zwei"):
        return out
    ny, nx = f["dicke"].shape
    X, Y = np.meshgrid(np.arange(nx) * z, np.arange(ny) * z)
    in_steg = np.zeros_like(m)
    for s in f["info"]["reliefs"][0].get("stege") or []:
        in_steg |= shapely.contains_xy(Polygon(s["ecken"]).buffer(-0.6), X, Y)
    in_steg &= f["abstand"] > 2 * z
    if in_steg.any():
        d = float(f["dicke"][in_steg].min())
        if d < besch["steg_dicke"] - 0.1:
            out.append("Haltesteg zu dünn: %.2f statt %.2f mm" % (d, besch["steg_dicke"]))
    if besch.get("haut"):
        # ab 1 mm vom Modell: an der messerscharfen Kante der Linse ist das Modell selbst dünner als die Haut
        zone = (f["abstand"] > max(2 * z, 1.0)) & (f["abstand"] < 4) & ~in_steg
        if zone.any():
            d = float(f["dicke"][zone].min())
            if d < besch["haut"] - 0.1:
                out.append("Haut zu dünn: %.2f statt %.2f mm" % (d, besch["haut"]))
    return out


if __name__ == "__main__":
    import tempfile
    import time
    von, bis = int(sys.argv[1]), int(sys.argv[2])
    log = sys.argv[3] if len(sys.argv) > 3 else None
    t0 = time.time()
    n_funde = 0
    for nr in range(von, bis):
        with tempfile.TemporaryDirectory() as d:
            e = pruefen(nr, d)
        if e["funde"] or not e["ok"]:
            n_funde += 1
        zeile = json.dumps(e, ensure_ascii=False, default=str)
        if e["funde"]:
            print(zeile, flush=True)
        if log:
            with open(log, "a") as f:
                f.write(zeile + "\n")
    print("Fälle %d, mit Fund %d, %.0f s" % (bis - von, n_funde, time.time() - t0), flush=True)
