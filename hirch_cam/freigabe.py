# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Endprüfung der fertigen G-Code-Dateien (Peter, 20.10.: «klares Freigabe-Ergebnis»).

Liest jede Ausgabedatei so, wie sie auf die Maschine geht (nach Postprozessor und Nullpunkt),
und prüft vier Dinge:

- Tiefe: die tiefste Stelle liegt nicht tiefer als das Material (+ etwas Opferplatte beim
  Ausschneiden);
- Verfahrbereich: was das Programm in X, Y und Z überstreicht, passt in die Verfahrwege der
  Maschine;
- Eilgänge: kein G0 unterhalb der Materialoberfläche, ausser senkrecht nach oben;
- Sicherheitshöhe: das Programm beginnt und endet auf Sicherheitshöhe, die Spindel läuft, bevor
  geschnitten wird, und jede Datei endet mit M30 (bzw. M2).

Ergebnis: {"stufe": "ok" | "warnung" | "fehler", "punkte": [{"stufe", "text"}], "dateien": n}
"""
from __future__ import annotations

import os
import re
from typing import Dict, List

_WORT = re.compile(r"([A-Z])\s*([-+]?(?:\d+\.?\d*|\.\d+))")
OPFER = 0.6          # mm: so tief darf ein Ausschnitt in die Opferplatte (Kontur «ganz durch» + 0,2)
ZELLE = 0.5          # mm: Raster für «hier ist schon ausgeräumt» (Eilgänge in Taschen)
RAND = 2             # Zellen: so nah muss schon gefräst sein
EIL_GRENZE_BEWEGUNGEN = 400000      # mehr Bewegungen (grosse 3D-Programme): Eilgänge nicht geprüft – wird gemeldet

# Peter 07.11.: was die Endprüfung nicht genau prüfen kann, sagt sie – auch wenn es die eigene Maschine nicht betrifft
NICHT_GEPRUEFT_BOGEN = "{}: {} Bogen/Bögen in einer anderen Ebene (G18/G19) – dort nur als Gerade geprüft."
NICHT_GEPRUEFT_RELATIV = ("{}: {} Bewegung(en) mit relativen Koordinaten (G91) ohne bekannten Startpunkt – "
                          "dort nicht geprüft.")
NICHT_GEPRUEFT_GROSS = "{}: sehr grosses Programm ({} Bewegungen) – Eilgänge nicht geprüft, nur Tiefe und Bereich."


def _zeilen(text: str):
    """(Zeilennummer ab 0, Zeile ohne Kommentar) – die Nummer zählt jede Zeile der Datei (Sammeldatei: wo welches
    Werkzeug beginnt)."""
    for i, z in enumerate(text.splitlines()):
        z = re.sub(r"\([^)]*\)", "", z)          # Kommentare (…)
        z = z.split(";")[0].strip().upper()
        if z and z != "%":
            yield i, z


BOGEN_TOL = 0.01     # mm: so weit darf ein Stück eines gelesenen Bogens vom Bogen abweichen


def _bogen(x, y, z, nx, ny, nz, w, gegen_uhr, mitte_absolut):
    """Bogen G2/G3 in der XY-Ebene als kurze Geraden: Liste der Punkte nach dem Start (ohne ihn), oder None, wenn er
    sich nicht lesen lässt (dann gilt die Sehne). Vorher lasen Endprüfung und Nachprüfung nur die Sehne – ein Eilgang
    durch das Innere des Bogens fiel nicht auf."""
    import math
    if "I" in w or "J" in w:
        cx = (w.get("I", 0.0) if mitte_absolut else x + w.get("I", 0.0))
        cy = (w.get("J", 0.0) if mitte_absolut else y + w.get("J", 0.0))
    elif "R" in w:
        rr = w["R"]
        dx, dy = nx - x, ny - y
        s = math.hypot(dx, dy)
        if s < 1e-9 or abs(rr) < s / 2 - 1e-6:
            return None
        h = math.sqrt(max(0.0, rr * rr - s * s / 4))
        # Mitte links der Sehne für G3 mit R > 0 und für G2 mit R < 0 (kurzer bzw. langer Bogen)
        links = gegen_uhr == (rr > 0)
        sg = 1.0 if links else -1.0
        cx, cy = x + dx / 2 - sg * dy / s * h, y + dy / 2 + sg * dx / s * h
    else:
        return None
    r0, r1 = math.hypot(x - cx, y - cy), math.hypot(nx - cx, ny - cy)
    if r0 < 1e-6:
        return None
    a0, a1 = math.atan2(y - cy, x - cx), math.atan2(ny - cy, nx - cx)
    zwei_pi = 2 * math.pi
    lauf = (a1 - a0) % zwei_pi if gegen_uhr else (a0 - a1) % zwei_pi
    if lauf < 1e-9 and ("I" in w or "J" in w):
        lauf = zwei_pi                         # Vollkreis: Start = Ende
    if lauf < 1e-9:
        return None
    rm = max(r0, r1)
    schritt = 2 * math.acos(max(-1.0, 1 - BOGEN_TOL / rm)) if rm > BOGEN_TOL else math.pi / 4
    n = max(1, min(3600, int(math.ceil(lauf / max(schritt, 1e-4)))))
    aus = []
    for k in range(1, n + 1):
        t = k / n
        a = a0 + (lauf * t if gegen_uhr else -lauf * t)
        rad = r0 + (r1 - r0) * t
        aus.append((cx + rad * math.cos(a), cy + rad * math.sin(a),
                    z + (nz - z) * t if z is not None and nz is not None else nz))
    aus[-1] = (nx, ny, nz)
    return aus


_GELESEN: Dict = {}       # Kennung des Textes → Ergebnis von lesen (im Dienst; B-7: Seite 1 nicht jedes Mal neu lesen)


def lesen(text: str) -> Dict:
    """Bewegungen eines Programms: Liste von (art, x, y, z) mit art G0/G1/zyklus; dazu Merkmale. Gleicher Text:
    das Ergebnis vom letzten Mal (es wird nur gelesen, nie verändert)."""
    import hashlib
    k = hashlib.sha256(text.encode("utf-8", "replace")).hexdigest()
    if k not in _GELESEN:
        _GELESEN[k] = _lesen(text)
        while len(_GELESEN) > 16:
            _GELESEN.pop(next(iter(_GELESEN)))
    return _GELESEN[k]


def _lesen(text: str) -> Dict:
    x = y = z = None
    modus = 0
    absolut = True
    mitte_absolut = False           # G90.1: Bogenmitte I/J absolut (sonst G91.1, relativ zum Start)
    ebene = 17
    zyklus = False
    zyk_z = zyk_r = None            # Bohrzyklus: Z und R gelten weiter (Zeilen nur mit X/Y)
    zurueck_r = False               # G99: nach dem Loch auf R statt auf die Anfangshöhe
    spindel = False
    vor_spindel = 0                 # Schnitte, bevor die Spindel läuft
    bewegungen = []
    zeilen = []                     # je Bewegung ihre Zeile in der Datei
    ende = False
    nicht_geprueft = {}             # was sich nicht genau lesen liess → Anzahl (Peter 07.11.: immer sagen)
    for nr, zeile in _zeilen(text):
        w = {}
        gs = []
        ms = []
        for b, v in _WORT.findall(zeile):
            if b == "G":
                gs.append(round(float(v), 1))
            elif b == "M":
                ms.append(int(float(v)))
            else:
                w[b] = float(v)
        for m in ms:
            if m in (3, 4):
                spindel = True
            elif m == 5:
                spindel = False
            elif m in (2, 30):
                ende = True
        neue = None
        for g in gs:
            if g in (0, 1, 2, 3):
                modus, zyklus = int(g), False
                neue = modus
            elif g in (81, 82, 83, 73, 85, 86, 89):
                zyklus = True
                neue = "zyklus"
            elif g == 80:
                zyklus = False
            elif g == 90:
                absolut = True
            elif g == 91:
                absolut = False
            elif g == 90.1:
                mitte_absolut = True
            elif g == 91.1:
                mitte_absolut = False
            elif g in (17, 18, 19):
                ebene = int(g)
            elif g == 98:
                zurueck_r = False
            elif g == 99:
                zurueck_r = True
            elif g in (28, 53, 30):
                neue = "maschine"          # Referenzfahrt: in Maschinenkoordinaten, nicht prüfen
        if neue == "maschine":
            # danach steht die Achse irgendwo in Maschinenkoordinaten (G28/G30: ganz oben) – unbekannt, bis eine
            # Zeile sie wieder setzt
            alle_achsen = 53 not in gs and not any(k in w for k in "XYZ")     # «G28» allein: alle Achsen
            x = None if alle_achsen or "X" in w else x
            y = None if alle_achsen or "Y" in w else y
            z = None if alle_achsen or "Z" in w else z
            continue
        if not any(k in w for k in "XYZ"):
            continue

        def neu(k, alt):
            if k not in w:
                return alt
            if absolut:
                return w[k]
            if alt is None:
                # G91 ohne bekannten Start: wo der Fräser steht, weiss die Prüfung nicht
                nicht_geprueft["relativ"] = nicht_geprueft.get("relativ", 0) + 1
                return None
            return alt + w[k]
        nx, ny = neu("X", x), neu("Y", y)
        if zyklus:
            # Bohrzyklus: Z ist die tiefste Stelle, R die Rückzugsebene; beide gelten weiter. G91: R ab der
            # Anfangshöhe, Z ab R
            if "R" in w:
                zyk_r = w["R"] if absolut else (None if z is None else z + w["R"])
            if "Z" in w:
                zyk_z = w["Z"] if absolut else (None if zyk_r is None else zyk_r + w["Z"])
                if zyk_z is None:
                    nicht_geprueft["relativ"] = nicht_geprueft.get("relativ", 0) + 1
            bewegungen.append(("zyklus", nx, ny, zyk_z, x, y, z))
            zeilen.append(nr)
            if not spindel:
                vor_spindel += 1
            x, y = nx, ny
            if zurueck_r and zyk_r is not None:
                z = zyk_r
            continue
        nz = neu("Z", z)
        art = "G0" if modus == 0 else "G1"
        if art == "G1" and not spindel:
            vor_spindel += 1
        stuecke = None
        if modus in (2, 3) and x is not None and y is not None and nx is not None and ny is not None:
            if ebene == 17:
                stuecke = _bogen(x, y, z, nx, ny, nz, w, modus == 3, mitte_absolut)
            if stuecke is None:
                # andere Ebene (G18/G19) oder nicht lesbar: nur als Gerade geprüft
                nicht_geprueft["bogen"] = nicht_geprueft.get("bogen", 0) + 1
        if stuecke:
            px, py, pz = x, y, z
            for qx, qy, qz in stuecke:
                bewegungen.append(("G1", qx, qy, qz, px, py, pz))
                zeilen.append(nr)
                px, py, pz = qx, qy, qz
        else:
            bewegungen.append((art, nx, ny, nz, x, y, z))
            zeilen.append(nr)
        x, y, z = nx, ny, nz
    return {"bewegungen": bewegungen, "zeilen": zeilen, "ende": ende, "vor_spindel": vor_spindel,
            "nicht_geprueft": nicht_geprueft}


FEIN = 0.1           # mm: Raster der Nachprüfung (Fräserform)
FEIN_TOL = 0.02      # mm: so tief darf ein Eilgang laut Nachprüfung im Material stecken


def _eilgaenge_nachpruefen(dateien_gelesen, punkte, oben: float) -> None:
    """Zufallstests 06.11.: das grobe Raster (Fräsermitte, 0,5 mm) meldete Eilgänge «unterhalb der Oberfläche», wo
    der Fräser längst alles geräumt hatte – Peter sah «Fehler» ohne Grund. Hat eine Datei solche Eilgänge und kennen
    sie und alle Dateien davor auf derselben Seite ihren Fräser (Form, Radius), wird mit der Form im 0,1-mm-Raster
    nachgerechnet (alle Dateien der Seite in Fräsfolge). Steckt dann kein
    Eilgang im Material, fällt die Meldung weg. Sonst bleibt sie (nie eine echte verlieren)."""
    import math
    bekannt = ("flach", "kugel", "v", "konisch")
    zu_pruefen = [i for i, d in enumerate(dateien_gelesen)
                  if (d[5] or d[7]) and d[3][0] in bekannt and d[2] > 0 and d[4] is not None]
    if not zu_pruefen:
        return
    try:
        import numpy as np
        from .simulation import Hoehenfeld
    except Exception:
        return
    weg, neu = [], []
    for i in zu_pruefen:
        name, bw, r_f, flach, seite, n_eil, stelle, n_zweifel = dateien_gelesen[i]
        davor = [d for d in dateien_gelesen[:i + 1] if d[4] == seite]
        if any(d[3][0] not in bekannt or d[2] <= 0 for d in davor):
            continue                          # eine Form unbekannt (mehrere Fräser in einer Datei …): grob bleibt
        xs = [b[1] for d in davor for b in d[1] if b[1] is not None]
        ys = [b[2] for d in davor for b in d[1] if b[2] is not None]
        if not xs or not ys:
            continue
        rmax = max(d[2] for d in davor)
        h = Hoehenfeld(min(xs) - rmax - 2, min(ys) - rmax - 2, max(xs) + rmax + 2, max(ys) + rmax + 2, FEIN)
        echt = 0
        for j, d in enumerate(davor):
            form, vw, spitze, er = d[3]
            if form == "kugel" and er <= 0:
                er = d[2]
            n, prof = h._stempel(form, 2 * d[2], vw, spitze, er)
            eigene = j == len(davor) - 1
            pos = None
            for art, x, y, z, xv, yv, zv in d[1]:
                if x is None or y is None or z is None:
                    continue
                q = (x, y, z - oben)
                if pos is None:
                    # erster voller Punkt im Eilgang schon unter der Oberfläche (Fable 06.11.: wurde nie geprüft)
                    if eigene and art == "G0" and q[2] < 0 and _im_material(h, q[0], q[1], q[2], n, prof) > FEIN_TOL:
                        echt += 1
                    pos = q
                    continue
                if min(pos[2], q[2]) < 0:
                    k = max(1, int(math.ceil(math.dist(pos, q) / (FEIN / 2))))
                    for s_ in range(k + 1):
                        t = s_ / k
                        px, py, pz = (pos[0] + (q[0] - pos[0]) * t, pos[1] + (q[1] - pos[1]) * t,
                                      pos[2] + (q[2] - pos[2]) * t)
                        if art == "zyklus" and s_ < k:
                            continue                  # Bohrzyklus: nur das Loch, nicht der Weg dorthin
                        if art == "G0":
                            if eigene and pz < 0 and _im_material(h, px, py, pz, n, prof) > FEIN_TOL:
                                echt += 1
                                break
                        else:
                            h._stempeln(px, py, pz, n, prof)
                pos = q
        if not echt and n_eil:
            weg.append(stelle)
        elif echt and not n_eil:
            # nur dank der breiten Toleranz «frei» – die Nachprüfung findet den Fräser im Material (Fable 06.11.: Eilgang
            # 2,5 mm in der Wand neben der Nut)
            neu.append({"stufe": "fehler", "text": f"{name}: {echt} Eilgang/Eilgänge unterhalb der Materialoberfläche."})
    for stelle in sorted(weg, reverse=True):
        del punkte[stelle]
    punkte.extend(neu)


def _im_material(h, x, y, z, n, prof) -> float:
    i = int(round((x - h.x0) / h.c))
    j = int(round((y - h.y0) / h.c))
    i0, i1 = max(0, i - n), min(h.nx, i + n + 1)
    j0, j1 = max(0, j - n), min(h.ny, j + n + 1)
    if i0 >= i1 or j0 >= j1:
        return 0.0
    sub = prof[j0 - (j - n):j1 - (j - n), i0 - (i - n):i1 - (i - n)] + z
    import numpy as np
    return float(max(0.0, np.max(h.H[j0:j1, i0:i1] - sub)))


def pruefen(dateien: List[str], dicke: float, oben: float, sicher: float, maschine: Dict | None = None,
            durch_erlaubt: bool = True, luft: float = 0.0, oben_material: float | None = None,
            soll_max: float | None = None, pass_tief: float = 0.0, fraeser: Dict | None = None) -> Dict:
    """dicke: Materialdicke; oben: Z der Materialoberfläche in Ausgabekoordinaten (0, oder die Dicke bei
    Nullpunkt unten); luft: Luftfräsen-Abstand (alles liegt dann so viel höher); sicher: Sicherheitshöhe.
    fraeser: je Dateiname (radius mm, flach[, seite]) – ein flacher Fräser räumt bis fast an seinen Rand gleich tief aus,
    darum gilt dort ein Eilgang so weit neben der Fräsermitte als frei (05.11.: falsche Funde beim Flügel)."""
    punkte: List[Dict] = []
    ma = maschine or {}
    eil_grenze = oben                      # Eilgänge nicht tiefer (schon freie Bereiche abgezogen)
    oben = oben if oben_material is None else oben_material

    def p(stufe, text):
        punkte.append({"stufe": stufe, "text": text})

    def zahl(v):
        return ("%.2f" % v).rstrip("0").rstrip(".").replace(".", ",")

    alle_x, alle_y, alle_z = [], [], []
    tiefste = None
    frei_seiten: Dict = {}                    # Seite → was die Dateien davor ausgeräumt haben (Reihenfolge = Fräsfolge)
    dateien_gelesen: List = []                # (Name, Bewegungen, Radius, Form, Seite, Eilgänge, Stelle in «punkte»)
    for pfad in dateien:
        name = os.path.basename(pfad)
        try:
            with open(pfad, encoding="utf-8", errors="replace", newline="") as fh:
                prog = lesen(fh.read())
        except OSError as e:
            p("fehler", f"{name}: nicht lesbar ({e}).")
            continue
        bw = prog["bewegungen"]
        if not bw:
            p("fehler", f"{name}: keine Bewegungen.")
            continue
        if not prog["ende"]:
            p("warnung", f"{name}: endet nicht mit M30 oder M2.")
        if prog["vor_spindel"]:
            p("fehler", f"{name}: {prog['vor_spindel']} Schnitt(e), bevor die Spindel läuft (M3).")
        # Sammeldatei (Grbl, Mach3 …: alle Werkzeuge in einer Datei, Fable 07.11.): je Werkzeug ein eigener Teil mit seiner
        # Fräserform – sonst kannte die Nachprüfung die Form nicht, und es blieb ein Fehlalarm
        f_alle = (fraeser or {}).get(name)
        if isinstance(f_alle, dict) and f_alle.get("teile"):
            zl_ = prog.get("zeilen") or []
            starts = [int(t[0]) for t in f_alle["teile"]] + [1 << 62]
            teile = []
            for k_, (start, f_k, wort) in enumerate(f_alle["teile"]):
                bw_k = [b_ for b_, n_ in zip(bw, zl_) if starts[k_] <= n_ < starts[k_ + 1]]
                if bw_k:
                    teile.append((f"{name} {wort}".strip(), bw_k, tuple(f_k or ())))
            if not teile:
                teile = [(name, bw, ())]
        else:
            teile = [(name, bw, tuple(f_alle or ()))]
        for k_ng, n_ng in sorted((prog.get("nicht_geprueft") or {}).items()):
            p("warnung", (NICHT_GEPRUEFT_BOGEN if k_ng == "bogen" else NICHT_GEPRUEFT_RELATIV).format(name, n_ng))
        if len(bw) > EIL_GRENZE_BEWEGUNGEN:
            p("warnung", NICHT_GEPRUEFT_GROSS.format(name, len(bw)))
        name_datei = name
        for name, bw_t, f_ in teile:
            eil_tief = 0
            eil_zweifel = 0                       # nur mit der breiten Toleranz flacher Fräser frei (Fable 06.11.)
            rand = RAND
            r_f, flach, seite = (f_ + (0.0, False, None)[len(f_):])[:3]
            if flach and r_f > ZELLE:
                rand = max(RAND, int((r_f - 0.5) / ZELLE))     # 0,5 mm Abstand zum Rand des Fräsers
            frei: Dict = {}                       # Zelle → tiefste schon gefräste Z (Eilgänge im Freien sind erlaubt)
            # 06.11. (Windows, muster_alle): was eine frühere Datei derselben Seite ausgeräumt hat, bleibt frei – das
            # Schlichten und Spannung 2 fahren dort, wo das Schruppen schon war. Ohne bekannte Seite: nur die eigene Datei.
            vorher = frei_seiten.setdefault(seite, {}) if seite is not None else {}
            pruef_eil = len(bw) <= EIL_GRENZE_BEWEGUNGEN    # sehr grosse 3D-Programme: nur Tiefe und Bereich

            def zellen(xa, ya, xb, yb):
                import math
                n = max(1, int(math.hypot(xb - xa, yb - ya) / ZELLE) + 1)
                for i in range(n + 1):
                    t = i / n
                    yield int(round((xa + (xb - xa) * t) / ZELLE)), int(round((ya + (yb - ya) * t) / ZELLE))

            def frei_bei(cx, cy, z, rand_eigen):
                # frei, wenn in der Nähe schon mindestens so tief gefräst wurde (in dieser oder einer früheren Datei)
                for quelle, rr in ((frei, rand_eigen), (vorher, min(RAND, rand_eigen))):
                    for dx in range(-rr, rr + 1):
                        for dy in range(-rr, rr + 1):
                            v = quelle.get((cx + dx, cy + dy))
                            if v is not None and v <= z + 0.01:
                                return True
                return False
            for art, x, y, z, xv, yv, zv in bw_t:
                if x is not None:
                    alle_x.append(x)
                if y is not None:
                    alle_y.append(y)
                if z is not None:
                    alle_z.append(z)
                    if art != "G0" and (tiefste is None or z < tiefste):
                        tiefste = z
                if not pruef_eil or x is None or y is None or z is None:
                    continue
                xa = x if xv is None else xv
                ya = y if yv is None else yv
                if art == "zyklus":
                    # Bohrzyklus: nur das Loch selbst ist gebohrt, nicht der Weg dorthin (Fable 06.11.)
                    c = (int(round(x / ZELLE)), int(round(y / ZELLE)))
                    if frei.get(c, 1e9) > z:
                        frei[c] = z
                elif art != "G0":
                    # je Zelle die Höhe an dieser Stelle der Strecke (Fable 06.11.: eine Rampe galt überall als so tief
                    # wie ihr Ende)
                    za = zv if zv is not None else z
                    zl = list(zellen(xa, ya, x, y))
                    for i_, c in enumerate(zl):
                        tz = za + (z - za) * (i_ / max(1, len(zl) - 1))
                        if frei.get(c, 1e9) > tz:
                            frei[c] = tz
                elif z < eil_grenze - 0.05:
                    # senkrecht nach oben ist erlaubt; waagrecht nur, wo schon so tief ausgeräumt ist
                    senkrecht_hoch = zv is not None and z >= zv - 1e-6 and x == xv and y == yv
                    if not senkrecht_hoch:
                        zl = list(zellen(xa, ya, x, y))
                        if not all(frei_bei(cx, cy, z, 0) for cx, cy in zl):
                            # nicht genau auf einer schon gefrästen Bahn: nur mit Toleranz frei – die Nachprüfung in der
                            # Form des Fräsers entscheidet (Fable 06.11.: neben der Nut, V-Flanke)
                            if all(frei_bei(cx, cy, z, rand) for cx, cy in zl):
                                eil_zweifel += 1          # nur dank der Fräserbreite frei: die Nachprüfung entscheidet
                            else:
                                eil_tief += 1
            if seite is not None:
                for c, v in frei.items():
                    if vorher.get(c, 1e9) > v:
                        vorher[c] = v
            form_ = tuple(f_[3:7]) if len(f_) >= 7 else (("flach" if flach else ""), 60.0, 0.0, 0.0)
            if eil_tief:
                p("fehler", f"{name}: {eil_tief} Eilgang/Eilgänge unterhalb der Materialoberfläche.")
            dateien_gelesen.append((name, bw_t, r_f, form_, seite, eil_tief, len(punkte) - (1 if eil_tief else 0),
                                    eil_zweifel))
        name = name_datei
        erste_z = next((b[3] for b in bw if b[3] is not None), None)
        if erste_z is not None and erste_z < min(sicher, oben + 2.0) - 0.05 and bw[0][0] == "G0":
            p("warnung", f"{name}: beginnt auf Z {zahl(erste_z)} – unter der Sicherheitshöhe ({zahl(sicher)}).")
        letzte_z = next((b[3] for b in reversed(bw) if b[3] is not None), None)
        if letzte_z is not None and letzte_z < oben - 0.05:
            p("fehler", f"{name}: endet im Material (Z {zahl(letzte_z)}).")
    _eilgaenge_nachpruefen(dateien_gelesen, punkte, eil_grenze)
    if tiefste is not None:
        tief = oben - tiefste                 # mm unter der Oberfläche
        if luft:
            if tiefste < oben + luft - 0.05:
                p("fehler", f"Luftfräsen: tiefste Stelle nur {zahl(tiefste - oben)} mm über dem Werkstück.")
            else:
                p("ok", f"Luftfräsen: tiefste Stelle {zahl(tiefste - oben)} mm über dem Werkstück.")
        elif soll_max is not None and tief > soll_max + 0.05:
            p("fehler", f"Tiefste Stelle {zahl(tief)} mm unter der Oberfläche – tiefer als jede Bearbeitung "
                        f"eingestellt ist ({zahl(soll_max)} mm).")
        elif dicke and tief > dicke + (OPFER if durch_erlaubt else 0.0) + 1e-6:
            p("ok" if pass_tief and tief <= pass_tief + 0.05 else "warnung",
              f"Tiefste Stelle {zahl(tief)} mm – {zahl(tief - dicke)} mm in die Opferplatte"
              + (" (Passlöcher)." if pass_tief and tief <= pass_tief + 0.05 else
                 f" (Material {zahl(dicke)} mm). Ist die Opferplatte dick genug?"))
        elif dicke and tief > dicke + 1e-6:
            p("ok", f"Tiefste Stelle {zahl(tief)} mm: {zahl(tief - dicke)} mm in die Opferplatte (Ausschneiden).")
        else:
            p("ok", f"Tiefste Stelle {zahl(tief)} mm unter der Oberfläche" +
                    (f" (Material {zahl(dicke)} mm)." if dicke else "."))
    for achse, werte, k in (("X", alle_x, "x"), ("Y", alle_y, "y"), ("Z", alle_z, "z")):
        if not werte:
            continue
        spanne = max(werte) - min(werte)
        weg = float(ma.get(k) or 0)
        if weg and spanne > weg + 1e-6:
            p("fehler", f"Verfahrbereich {achse}: das Programm braucht {zahl(spanne)} mm, die Maschine hat "
                        f"{zahl(weg)} mm.")
    if alle_x and alle_y:
        p("ok", f"Bereich X {zahl(min(alle_x))} … {zahl(max(alle_x))} mm, Y {zahl(min(alle_y))} … "
                f"{zahl(max(alle_y))} mm (ab dem Nullpunkt).")
    if not any(q["stufe"] != "ok" and "Eilg" in q["text"] for q in punkte) and alle_z:
        p("ok", "Eilgänge nur über dem Werkstück.")
    stufe = "fehler" if any(q["stufe"] == "fehler" for q in punkte) else \
        "warnung" if any(q["stufe"] == "warnung" for q in punkte) else "ok"
    return {"stufe": stufe, "punkte": punkte, "dateien": len(dateien)}
