"""Zufallszeichnungen für die Sicherheitsprüfung der 2D-Bahnen (planung 06.11., Peter 17:43).

Die Musterdateien zeigen die Fälle, an die jemand gedacht hat – zufällige Zeichnungen finden die anderen. Jeder Fall
entsteht aus einer Zahl (Startwert) und lässt sich damit genau nachstellen. Gerechnet wird direkt mit engine.rechnen
(ohne SVG), geprüft mit den vorhandenen, unabhängigen Prüfern:

- werkzeuge/eilgang_pruefen.py: Eilgang durch stehendes Material, senkrechtes Eintauchen ohne Mittenschnitt;
- dokument.endpruefung (freigabe.py): Tiefe, Eilgänge unter der Oberfläche, Sicherheitshöhe;
- Umspannen: in Spannung 1 kein Fräsweg in einer Sperrzone;
- nirgends tiefer als die tiefste eingestellte Tiefe (+ Plantiefe).

    python3 tests/zufall/zeichnung.py 0 500          # Fälle 0 … 499, Funde nach stdout
    python3 tests/zufall/zeichnung.py 17              # nur Fall 17, ausführlich
"""
from __future__ import annotations

import math
import os
import random
import sys
import tempfile

HIER = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HIER, "..", ".."))
sys.path.insert(0, os.path.join(HIER, "..", "..", "werkzeuge"))

WERKZEUGE = {
    "A": {"typ": "v", "durchmesser": 12.0, "v_winkel": 60, "vorschub": 1000, "eintauchen": 300, "nummer": 1,
          "schneidenlaenge": 11.0, "mittenschnitt": True},
    "B": {"typ": "schaft", "durchmesser": 3.0, "vorschub": 1000, "eintauchen": 300, "nummer": 2,
          "schneidenlaenge": 12.0, "mittenschnitt": True},
    "C": {"typ": "schaft", "durchmesser": 6.0, "vorschub": 1000, "eintauchen": 300, "nummer": 3,
          "schneidenlaenge": 22.0, "mittenschnitt": True},
    "G": {"typ": "gewinde", "durchmesser": 5.0, "zaehne": 1, "steigung": 1.25, "vorschub": 400, "eintauchen": 200,
          "nummer": 4, "schneidenlaenge": 12.0},
}


# ------------------------------------------------------------------ Formen
def _rechteck(x0, y0, x1, y1):
    return [[x0, y0], [x1, y0], [x1, y1], [x0, y1], [x0, y0]]


def _kreis(cx, cy, r, n=48):
    return [[cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n)] for i in range(n + 1)]


def _polygon(rng, cx, cy, r):
    n = rng.randint(3, 9)
    w = sorted(rng.uniform(0, 2 * math.pi) for _ in range(n))
    pts = [[cx + r * rng.uniform(0.4, 1.0) * math.cos(a), cy + r * rng.uniform(0.4, 1.0) * math.sin(a)] for a in w]
    return pts + [pts[0]]


def _buchstabe(rng, x, y, h):
    """Ein einfacher «Text-Pfad»: Strichbuchstabe aus 2–4 Linien (offen)."""
    b = h * 0.6
    formen = [[[x, y], [x + b / 2, y + h], [x + b, y]], [[x, y], [x, y + h], [x + b, y + h]],
              [[x + b, y + h], [x, y + h], [x, y + h / 2], [x + b, y + h / 2], [x + b, y], [x, y]],
              [[x, y + h], [x + b, y]], [[x, y], [x + b, y + h], [x + b, y]]]
    return rng.choice(formen)


def _form(rng, kasten, klein=False):
    """Eine geschlossene Form in kasten (x0, y0, x1, y1): Rechteck, Kreis oder Polygon."""
    x0, y0, x1, y1 = kasten
    gr = min(x1 - x0, y1 - y0)
    r = rng.uniform(1.0, 3.0) if klein else rng.uniform(min(4.0, gr / 3), max(4.5, gr / 2.2))
    cx, cy = rng.uniform(x0 + r, max(x0 + r, x1 - r)), rng.uniform(y0 + r, max(y0 + r, y1 - r))
    art = rng.choice(["rechteck", "kreis", "polygon"])
    if art == "rechteck":
        w, h = r * rng.uniform(0.8, 2.0), r * rng.uniform(0.8, 2.0)
        return _rechteck(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2), (cx, cy, r)
    if art == "kreis":
        return _kreis(cx, cy, r), (cx, cy, r)
    return _polygon(rng, cx, cy, r), (cx, cy, r)


# ------------------------------------------------------------------ Zeichnung
def auftrag(nr: int, ordner: str) -> dict:
    rng = random.Random(nr)
    B, H = rng.choice([60, 80, 100, 120]), rng.choice([50, 60, 80])
    D = rng.choice([6, 8, 10, 12, 18])
    zwei = rng.random() < 0.3
    werkzeuge = {k: dict(v) for k, v in WERKZEUGE.items()}
    if rng.random() < 0.25:                         # Fräser ohne Mittenschnitt (Rampe verlangt)
        werkzeuge[rng.choice("BC")]["mittenschnitt"] = False
    bea = []

    def kasten():
        x0, y0 = rng.uniform(5, B * 0.6), rng.uniform(5, H * 0.6)
        return (x0, y0, min(B - 5, x0 + rng.uniform(12, B * 0.5)), min(H - 5, y0 + rng.uniform(12, H * 0.5)))

    def obj(pts, zu=True):
        return {"punkte": [[round(x, 3), round(y, 3)] for x, y in pts], "geschlossen": zu}

    seite = lambda: (2 if zwei and rng.random() < 0.4 else 1)          # noqa: E731
    if rng.random() < 0.3:
        bea.append({"id": "pl", "art": "planen", "werkzeug": "C", "tiefe": rng.choice([0.3, 0.5, 1.0, 2.0]),
                    "zustellung": 0.5, "objekte": [obj(_rechteck(0, 0, B, H))], "auf_seite": seite()})
    for i in range(rng.randint(1, 5)):
        art = rng.choice(["tasche", "tasche", "kontur", "kontur", "bohren", "gewinde", "gravur", "vcarve"])
        bid, s = f"{art[:2]}{i}", seite()
        k = kasten()
        if art == "tasche":
            pts, (cx, cy, r) = _form(rng, k)
            ob = [obj(pts)]
            if rng.random() < 0.4:                                   # Insel
                ob.append(obj(_kreis(cx, cy, r * rng.uniform(0.15, 0.4))))
            t = round(rng.uniform(0.5, min(D - 0.5, 10)), 1)
            bea.append({"id": bid, "art": "tasche", "werkzeug": rng.choice("BC"), "tiefe": t,
                        "zustellung": round(rng.uniform(0.5, max(0.6, t)), 1),
                        "strategie": rng.choice(["runden", "adaptiv"]), "starttiefe": 0.0,
                        "aufmass": rng.choice([0.0, 0.0, 0.2, -0.1]), "objekte": ob, "auf_seite": s})
        elif art == "kontur":
            pts, _m = _form(rng, k, klein=rng.random() < 0.15)
            durch = rng.random() < 0.5
            st = rng.choice([0, 2, 4]) if durch else 0
            bea.append({"id": bid, "art": "kontur", "werkzeug": rng.choice("BC"),
                        "tiefe": None if durch else round(rng.uniform(0.5, D - 0.5), 1),
                        "zustellung": round(rng.uniform(1, 4), 1),
                        "seite": rng.choice(["innen", "aussen", "linie"]),
                        "stege_anzahl": st, "stege_modus": "auto" if st else "keine", "stege_breite": 5,
                        "stege_hoehe": 1.5, "anfahren": rng.choice(["", "bogen"]), "objekte": [obj(pts)],
                        "auf_seite": s})
        elif art == "bohren":
            ob = [obj(_kreis(rng.uniform(k[0], k[2]), rng.uniform(k[1], k[3]), rng.choice([1.5, 3.0, 4.0, 6.0])))
                  for _ in range(rng.randint(1, 4))]
            bea.append({"id": bid, "art": "bohren", "werkzeug": rng.choice("BC"),
                        "tiefe": round(rng.uniform(1, D), 1), "zustellung": 2.0,
                        "methode": rng.choice(["auto", "helix", "bohren"]), "objekte": ob, "auf_seite": s})
        elif art == "gewinde":
            if D < 8:
                continue
            bea.append({"id": bid, "art": "gewinde", "werkzeug": "G", "tiefe": round(rng.uniform(4, D - 1), 1),
                        "kernloch_werkzeug": "B", "innen": True,
                        "objekte": [obj(_kreis(rng.uniform(k[0] + 5, max(k[0] + 5, k[2] - 5)),
                                               rng.uniform(k[1] + 5, max(k[1] + 5, k[3] - 5)), 4))],
                        "auf_seite": s})
        elif art == "gravur":
            n_b = rng.randint(1, 4)
            x = rng.uniform(k[0], max(k[0], min(k[2], B - 5 - 6 * n_b)))      # Buchstaben im Material
            y = rng.uniform(k[1], max(k[1], min(k[3], H - 13)))
            linien = [obj(_buchstabe(rng, x + j * 6, y, 8), zu=False) for j in range(n_b)]
            if rng.random() < 0.5:
                linien.append(obj([[k[0], k[1]], [k[2], k[3]]], zu=False))
            bea.append({"id": bid, "art": "gravur", "werkzeug": rng.choice("AB"), "tiefe": rng.choice([0.3, 0.5, 1.0]),
                        "zustellung": 0.5, "objekte": linien, "auf_seite": s})
        else:
            pts, _m = _form(rng, k)
            bea.append({"id": bid, "art": "vcarve", "werkzeug": "A", "max_tiefe": rng.choice([2.0, 4.0, 6.0]),
                        "flach_werkzeug": rng.choice(["", "", "B"]), "objekte": [obj(pts)], "auf_seite": s})
    # wie einstellungen.laden/auftrag: Vorgaben je Art, Kontur ohne Tiefe = ganz durch (+ 0,2 mm)
    from hirch_cam.einstellungen import STANDARD_BEARBEITUNG
    for i, b in enumerate(bea):
        voll = dict(STANDARD_BEARBEITUNG.get(b["art"], {}), **b)
        if voll["art"] == "kontur" and not voll.get("tiefe"):
            voll["tiefe"], voll["durch"] = float(D) + 0.2, True
        voll.setdefault("name", "%s %d" % (voll["art"], i))
        bea[i] = voll
    a = {
        "werkzeuge": werkzeuge,
        "hoehen": {"freifahrhoehe": 3, "startabstand": 0.5, "sicherheitshoehe": 20, "materialdicke": D},
        "postprozessor": "vcarve_mm", "material_flaeche": [B, H], "spannmittel": [],
        "reihenfolge": rng.choice(["auto", "liste"]), "bearbeitungen": bea,
        "ausgabe": os.path.join(ordner, "z.tap"),
    }
    if zwei:
        a["zweiseitig"] = {"an": True, "achse": rng.choice(["x", "y"]), "passloecher": False}
    if rng.random() < 0.3:                                          # Umspannen mit Pratzen am Rand
        for _ in range(rng.randint(1, 2)):
            y = rng.uniform(5, H - 20)
            x0 = rng.choice([-10, B - 15])
            a["spannmittel"].append({"punkte": _rechteck(x0, y, x0 + 25, y + 15), "hoehe": rng.choice([6, 12]),
                                     "seite": 2 if zwei and rng.random() < 0.3 else 1})
        a["umspannen"] = {"an": True, "abstand": 10}
    return a


# ------------------------------------------------------------------ Prüfen
def _soll_tiefe(a) -> float:
    """Tiefste erlaubte Stelle (positiv): grösste eingestellte Tiefe + Plantiefe (wie dokument.endpruefung)."""
    D = float(a["hoehen"]["materialdicke"])
    soll, plan = 0.0, 0.0
    for b in a["bearbeitungen"]:
        t = b.get("max_tiefe") if b["art"] == "vcarve" else b.get("tiefe")
        t = D + 0.2 if t in (None, "") else float(t)
        if b["art"] == "planen":
            plan = max(plan, t)
        if b["art"] == "gewinde":
            t += 4.0
        soll = max(soll, t)
    return soll + plan


def pruefen(nr: int, ordner: str = None) -> dict:
    """Rechnet Fall nr und liefert {"funde": [...], "ok": bool, "sekunden": …}. Ein Fund ist ein Text."""
    import time
    import eilgang_pruefen as ep
    from hirch_cam import dokument, engine
    from hirch_cam.ops import umspannen as U
    from hirch_cam.post import tap_mm
    ordner = ordner or tempfile.mkdtemp(prefix="zufall-")
    a = auftrag(nr, ordner)
    t0 = time.time()
    funde = []
    try:
        erg = engine.rechnen(a)
    except Exception as ex:                                          # ein Absturz ist auch ein Fund
        return {"funde": [f"Absturz: {type(ex).__name__}: {ex}"], "ok": False, "sekunden": time.time() - t0}
    if not erg.get("ok"):
        return {"funde": [], "ok": False, "sekunden": time.time() - t0, "warnungen": erg.get("warnungen")}
    B, H = a["material_flaeche"]
    # ausserhalb des Materials ist Luft (Planen fährt mit Absicht darüber hinaus)
    for f in ep.pruefen(erg, werkzeuge=a["werkzeuge"], aussen_frei={1: (0, 0, B, H), 2: (0, 0, B, H)}):
        funde.append("%s: %s bei %.2f/%.2f Z %.3f, %.3f mm im Material" % (f["programm"], f["art"], f["ort"][0],
                                                                          f["ort"][1], f["z"], f["tief"]))
    fr = dokument.endpruefung(a, erg)
    gemeldet = any("keinen Mittenschnitt" in p["text"] for p in fr.get("punkte") or [])
    # Bohren ohne Mittenschnitt meldet die Prüfung im Panel; beim Speichern wandert die Meldung in die Endprüfung
    # (dokument.nur_beim_speichern) – das zählt auch (Fall 14861, 10.11.)
    from hirch_cam import pruefung as _pr
    gemeldet = gemeldet or any(x.get("nur_speichern") and x.get("stufe") == "fehler" and "ohne Mittenschnitt" in x["text"]
                               for x in _pr.pruefen(a, erg))
    for p in fr.get("punkte") or []:
        if p.get("stufe") == "fehler" and "Schneide" not in p["text"] and "Verfahrweg" not in p["text"] \
                and "keinen Mittenschnitt" not in p["text"]:
            funde.append("Endprüfung: " + p["text"])
    # senkrechtes Eintauchen ohne Mittenschnitt ist kein Fund, wenn die Endprüfung es meldet (Peter 06.11.:
    # «Prüfung + Hinweis»); meldet sie es nicht, ist es einer – und umgekehrt ein Fehlalarm der Endprüfung
    eint = [f for f in funde if ": eintauchen bei " in f]
    if gemeldet and eint:
        funde = [f for f in funde if f not in eint]
    # meldet nur die Endprüfung etwas (enge Helix: der feine Prüfer kennt den toten Kern nicht), ist das kein Fund
    # die Prüfung im Programm: ein Gewinde ohne Kernloch davor (06.11.: Kernloch kam nach dem Gewinde)
    from hirch_cam import pruefung
    for x in pruefung.pruefen(a, erg):
        if "kein Kernloch" in x["text"] and any(b.get("kernloch_werkzeug") for b in a["bearbeitungen"]
                                                  if b["art"] == "gewinde"):
            funde.append("Prüfung: " + x["text"][:120])
    soll = _soll_tiefe(a)
    for p in erg["programme"]:
        with open(p["datei"], newline="") as fh:
            job = tap_mm.read(fh.read())
        zmin = min([m.z for tp in job.toolpaths for m in tp.moves] + [0.0])
        if zmin < -soll - 0.011:
            funde.append("%s: tiefer als eingestellt: Z %.3f, erlaubt %.3f" % (p["name"], zmin, -soll))
        if a.get("umspannen") and int(p.get("spannung") or 1) == 1:
            z = U.zone(U.spannmittel(a, int(p["seite"]), 1), float(p["durchmesser"]) / 2, 10.0)
            if z is not None:
                from shapely.geometry import LineString, Point
                z = z.buffer(-0.01)
                pos = (job.home[0], job.home[1], job.safe_z)
                for tp in job.toolpaths:
                    for m in tp.moves:
                        if not m.rapid and min(pos[2], m.z) < 0:
                            g = LineString([pos[:2], (m.x, m.y)]) if math.dist(pos[:2], (m.x, m.y)) > 1e-9 \
                                else Point(m.x, m.y)
                            if g.intersects(z):
                                funde.append("%s: Spannung 1 fräst in der Sperrzone bei %.2f/%.2f" % (
                                    p["name"], m.x, m.y))
                                break
                        pos = (m.x, m.y, m.z)
    return {"funde": funde, "ok": True, "sekunden": time.time() - t0}


if __name__ == "__main__":
    os.environ.setdefault("HIRCH_OHNE_OBERFLAECHE", "1")
    os.environ.setdefault("HIRCH_2D_SPEICHER", "aus")
    von = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    bis = int(sys.argv[2]) if len(sys.argv) > 2 else von + 1
    n_ok = n_funde = 0
    zeit = 0.0
    for nr in range(von, bis):
        try:
            e = pruefen(nr)
        except Exception as ex:                       # 07.11.: ein Absturz der Prüfungen ist ein Fund, kein Abbruch
            e = {"funde": [f"Absturz: {type(ex).__name__}: {ex}"], "ok": False, "sekunden": 0.0}
        zeit += e["sekunden"]
        n_ok += e["ok"]
        if e["funde"]:
            n_funde += 1
            print("FALL %d: %s" % (nr, " | ".join(e["funde"][:4])), flush=True)
        elif bis - von == 1:
            print("Fall %d: ok=%s, keine Funde, %.1f s %s" % (nr, e["ok"], e["sekunden"], e.get("warnungen") or ""))
    print("ENDE %d Fälle, %d gerechnet, %d mit Funden, %.0f s" % (bis - von, n_ok, n_funde, zeit), flush=True)
