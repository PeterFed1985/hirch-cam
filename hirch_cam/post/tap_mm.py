# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Postprozessor "G-Code (mm) (*.tap)" – das verbreitete Format «G-Code (mm)».

Format (abgeleitet aus Peters Beispieldateien, siehe tests/samples):

    T1M6
    G17
    G0Z20.000                      Sicherheitshöhe
    G0X0.000Y0.000S12000M3         Home + Spindel an
    ...Werkzeugwege...
    G0Z20.000
    G0X0.000Y0.000
    M30

Regeln:
- Koordinaten mit 3 Nachkommastellen, Vorschub mit 1, Drehzahl ganzzahlig.
- Bögen (G2/G3) nur auf Wunsch (koerper(…, boegen=True)), sonst Rundungen als kurze Geraden.
- X, Y, Z werden nur geschrieben, wenn sich der Wert ändert (modal).
  Verglichen wird der ungerundete Wert: Ändert sich Y nur in der 4. Stelle,
  steht es trotzdem in der Datei (z. B. "X127.041Y79.572" nach "...Y79.572").
- Erster Eilgang eines Werkzeugwegs: X, Y und Z werden immer geschrieben.
- "G0" steht vor jedem Eilgang.
- "G1" + F steht vor dem ersten Vorschub nach einem Eilgang und immer dann,
  wenn sich der Vorschub ändert; sonst nur die Koordinaten.
- Zeilenende CRLF, auch nach der letzten Zeile.
"""
from __future__ import annotations

import re

from ..model import Job, Move, Toolpath

NL = "\r\n"
EPS = 1e-9


def _c(v: float) -> str:
    s = f"{v:.3f}"
    return "0.000" if s == "-0.000" else s


def _f(v: float) -> str:
    return f"{v:.1f}"


def write(job: Job, boegen: bool = False) -> str:
    out: list[str] = []
    hx, hy = job.home
    out.append(f"T{job.tool_number}M6")
    out.append("G17")
    out.append(f"G0Z{_c(job.safe_z)}")
    out.append(f"G0X{_c(hx)}Y{_c(hy)}S{job.spindle_rpm}M3")
    out += koerper(job, boegen=boegen)
    out.append(f"G0Z{_c(job.safe_z)}")
    out.append(f"G0X{_c(hx)}Y{_c(hy)}")
    out.append("M30")
    return NL.join(out) + NL


def zyklus_werte(ms, z, pos) -> dict | None:
    """Werte eines Bohrzyklus aus seinen Bewegungen ms[z.start:z.ende] – oder None,
    wenn sie nicht (mehr) wie eine Bohrung aussehen (dann schreibt der
    Postprozessor die Bewegungen). pos: Stelle davor (X, Y, Z)."""
    if not (0 <= z.start < z.ende <= len(ms)):
        return None
    teil = ms[z.start:z.ende]
    x, y = pos["X"], pos["Y"]
    if any(abs(m.x - x) > 1e-6 or abs(m.y - y) > 1e-6 for m in teil):
        return None
    erste, letzte = teil[0], teil[-1]
    oben = pos["Z"]
    if not erste.rapid or not letzte.rapid or abs(letzte.z - oben) > 1e-6 or erste.z > oben + 1e-6:
        return None
    vorschuebe = {m.feed for m in teil if not m.rapid}
    if len(vorschuebe) != 1 or None in vorschuebe:
        return None
    unten = min(m.z for m in teil)
    if unten >= erste.z - 1e-6:
        return None
    return {"x": _c(x), "y": _c(y), "z": _c(unten), "r": _c(erste.z), "oben": _c(oben),
            "q": _c(z.zustellung) if z.zustellung > 0 else _c(erste.z - unten),
            "p": "%g" % float(z.verweilen or 0), "f": _f(vorschuebe.pop()), "_unten": unten}


def merken(job: Job) -> Job:
    """Schaltet für diesen Job eine Merkliste ein: koerper() rechnet die Zeilen dann nur einmal
    (je Bögen ja/nein und Bohrzyklen) und gibt sie beim nächsten Aufruf wieder. Nur, wenn sich der
    Job danach nicht mehr ändert (engine.rechnen: Vorschau-Datei, Ausgabe und Zeilenanfänge)."""
    job._koerper_merk = {}
    return job


def koerper(job: Job, anfaenge: list | None = None, boegen: bool = False,
            toleranz: float = 0.005, zeilen: list | None = None, zyklen: dict | None = None) -> list[str]:
    """Die Werkzeugwege eines Jobs als G-Code-Zeilen (ohne Kopf und Schluss).
    Beginnt an job.home auf Sicherheitshöhe. anfaenge (falls gegeben) bekommt
    je Werkzeugweg den Index seiner ersten Zeile. boegen: Folgen kurzer
    Geraden auf einem Kreis als G2/G3 (I, J relativ zum Bogenanfang).
    zyklen: Muster für Bohrzyklen (aus der Vorlage): {"entspanen": [Zeilen, z. B.
    G83 …], "einfach": [G81 …], "verweilen": [G82 …], "ende": [G80]}. Ohne Muster
    (oder wenn eine Bohrung nicht passt) werden die Bewegungen geschrieben."""
    merk = getattr(job, "_koerper_merk", None)
    if merk is not None and zeilen is None:
        import json
        k = (bool(boegen), toleranz, json.dumps(zyklen, sort_keys=True) if zyklen else "")
        if k not in merk:
            a: list = []
            merk[k] = (_koerper(job, a, boegen, toleranz, None, zyklen), a)
        out, a = merk[k]
        if anfaenge is not None:
            anfaenge.extend(a)
        return list(out)
    return _koerper(job, anfaenge, boegen, toleranz, zeilen, zyklen)


def _koerper(job: Job, anfaenge, boegen, toleranz, zeilen, zyklen) -> list[str]:
    out: list[str] = []
    ap = out.append
    hx, hy = job.home
    px, py, pz = hx, hy, job.safe_z
    last_feed: str | None = None
    letzter_wert = None                           # Vorschub, zu dem last_feed gehört
    modus = 0                                     # zuletzt geschriebenes G0/G1/G2/G3
    zyklen = zyklen if zyklen and (zyklen.get("entspanen") or zyklen.get("einfach")) else None
    im_zyklus = False                             # Bohrzyklus noch aktiv (modal) – vor anderem G80

    def zyklus_ende():
        nonlocal im_zyklus
        if im_zyklus:
            out.extend(zyklen.get("ende") or ["G80"])
            im_zyklus = False

    def zyklus_schreiben(z, werte) -> None:
        nonlocal im_zyklus, modus, last_feed, letzter_wert
        if z.verweilen > 0 and zyklen.get("verweilen"):
            muster = zyklen["verweilen"]
        elif z.zustellung > 0 and zyklen.get("entspanen"):
            muster = zyklen["entspanen"]
        else:
            muster = zyklen.get("einfach") or zyklen["entspanen"]
        for zeile in muster:
            try:
                out.append(zeile.format(**{k: v for k, v in werte.items() if not k.startswith("_")}))
            except (KeyError, IndexError, ValueError):
                out.append(zeile)
        im_zyklus = True
        modus = -1                                 # nach G80 wieder G0/G1 schreiben
        last_feed = letzter_wert = None

    def gerade(m: Move, first: bool):
        # Wörter nur bei Änderung (ungerundet verglichen); erster Eilgang eines Wegs: X, Y und Z
        nonlocal px, py, pz, last_feed, letzter_wert, modus
        x, y, z = m.x, m.y, m.z
        if m.rapid:
            if first:
                words = "X" + _c(x) + "Y" + _c(y) + "Z" + _c(z)
            else:
                words = ((("X" + _c(x)) if abs(x - px) > EPS else "")
                         + (("Y" + _c(y)) if abs(y - py) > EPS else "")
                         + (("Z" + _c(z)) if abs(z - pz) > EPS else ""))
            ap("G0" + words)
            modus = 0
        else:
            words = ((("X" + _c(x)) if abs(x - px) > EPS else "")
                     + (("Y" + _c(y)) if abs(y - py) > EPS else "")
                     + (("Z" + _c(z)) if abs(z - pz) > EPS else ""))
            fv = m.feed
            if fv is None:
                raise ValueError("Vorschubbewegung ohne Vorschubwert")
            if fv == letzter_wert:
                f = last_feed
            else:
                f = _f(fv)
            if modus != 1 or f != last_feed:
                ap("G1" + words + "F" + f)
                last_feed, letzter_wert = f, fv
            elif words:
                ap(words)
            modus = 1
        px, py, pz = x, y, z

    def bogen(ziel, mx, my, ccw, feed):
        nonlocal px, py, pz, last_feed, letzter_wert, modus
        f = _f(feed)
        g = 3 if ccw else 2
        words = f"X{_c(ziel[0])}Y{_c(ziel[1])}"
        if abs(ziel[2] - pz) > EPS:
            words += f"Z{_c(ziel[2])}"
        words += f"I{_c(mx - px)}J{_c(my - py)}"
        zeile = "G%d" % g + words                # G2/G3 in jeder Bogenzeile (sicher für jede Steuerung)
        if f != last_feed or modus == 0:
            zeile += "F" + f
            last_feed, letzter_wert = f, feed
        ap(zeile)
        modus = g
        px, py, pz = ziel[0], ziel[1], ziel[2]

    for t_nr, tp in enumerate(job.toolpaths):
        if anfaenge is not None:
            anfaenge.append(len(out))
        ms = tp.moves
        starts = {z.start: z for z in (getattr(tp, "zyklen", None) or [])} if zyklen else {}
        if not boegen and zeilen is None and not starts and not im_zyklus:
            first = True                          # schneller Weg: nur Geraden
            for m in ms:
                gerade(m, first)
                first = False
            continue
        first = True
        i = 0
        while i < len(ms):
            m = ms[i]
            z = starts.get(i)
            werte = zyklus_werte(ms, z, {"X": px, "Y": py, "Z": pz}) if z is not None else None
            if werte is not None:
                zyklus_schreiben(z, werte)
                px, py, pz = ms[z.ende - 1].x, ms[z.ende - 1].y, ms[z.ende - 1].z
                if zeilen is not None:
                    for k in range(i, z.ende):
                        zeilen.append((t_nr, k, len(out)))
                first = False
                i = z.ende
                continue
            if im_zyklus:
                # zur nächsten Bohrung: der Zyklus fährt selbst auf der Rückzugshöhe hinüber
                nxt = starts.get(i + 1)
                if (nxt is not None and m.rapid and abs(m.z - pz) <= 1e-6
                        and zyklus_werte(ms, nxt, {"X": m.x, "Y": m.y, "Z": m.z}) is not None):
                    px, py, pz = m.x, m.y, m.z
                    i += 1
                    continue
                zyklus_ende()
            if not boegen or m.rapid:
                gerade(m, first)
                if zeilen is not None:          # (Werkzeugweg, Bewegung, Zeilen danach) – Wiederaufnahme
                    zeilen.append((t_nr, i, len(out)))
                first = False
                i += 1
                continue
            # Folge von Vorschüben mit gleichem Vorschub
            j = i
            while j + 1 < len(ms) and not ms[j + 1].rapid and ms[j + 1].feed == m.feed:
                j += 1
            pts = [(px, py, pz)] + [(q.x, q.y, q.z) for q in ms[i:j + 1]]
            if len(pts) < 5:
                for q in ms[i:j + 1]:
                    gerade(q, False)
            else:
                from .boegen import zerlegen
                for stueck in zerlegen(pts, toleranz):
                    if stueck[0] == "linie":
                        gerade(ms[i + stueck[2] - 1], False)     # pts[q] = ms[i + q − 1]
                    else:
                        _a, ziel, _q, mx, my, ccw = stueck
                        bogen(ziel, mx, my, ccw, m.feed)
            first = False
            i = j + 1
        zyklus_ende()
    return out


_WORD = re.compile(r"([A-Z])(-?\d+(?:\.\d+)?)")


def read(text: str) -> Job:
    """Liest eine .tap-Datei in diesem Format wieder ein (für Tests und
    die spätere 3D-Vorschau bestehender .tap-Dateien)."""
    lines = [l.strip() for l in text.splitlines() if l.strip()]
    job = Job()
    m = re.fullmatch(r"T(\d+)M6", lines[0])
    if not m or lines[1] != "G17":
        raise ValueError("Kopfzeilen nicht im erwarteten Format")
    job.tool_number = int(m.group(1))
    job.safe_z = float(lines[2][3:])
    head = dict(_WORD.findall(lines[3]))
    job.home = (float(head["X"]), float(head["Y"]))
    job.spindle_rpm = int(float(head["S"]))
    if lines[-1] != "M30":
        raise ValueError("Datei endet nicht mit M30")
    body = lines[4:-3]  # ohne Schlusszeilen G0Z<safe>, G0X..Y.., M30

    pos = {"X": job.home[0], "Y": job.home[1], "Z": job.safe_z}
    feed: float | None = None
    rapid = True
    g = 0
    tp: Toolpath | None = None
    for line in body:
        w = dict(_WORD.findall(line))
        if "G" in w:
            g = int(float(w["G"]))
            rapid = g == 0
        if "F" in w:
            feed = float(w["F"])
        if g in (2, 3) and tp is not None and ("I" in w or "J" in w):
            # Bogen in Geraden auflösen
            from .boegen import bogen_punkte
            start = (pos["X"], pos["Y"], pos["Z"])
            ziel = tuple(float(w[a]) if a in w else pos[a] for a in "XYZ")
            mx = start[0] + float(w.get("I", 0))
            my = start[1] + float(w.get("J", 0))
            for q in bogen_punkte(start, ziel, mx, my, g == 3, 0.002):
                tp.moves.append(Move(False, q[0], q[1], q[2], feed))
            pos = {"X": ziel[0], "Y": ziel[1], "Z": ziel[2]}
            continue
        # Neuer Werkzeugweg: Eilgang mit X, Y und Z in einer Zeile
        if rapid and all(a in w for a in "XYZ"):
            tp = Toolpath(name=f"Weg {len(job.toolpaths) + 1}")
            job.toolpaths.append(tp)
        if tp is None:
            raise ValueError(f"Bewegung vor dem ersten Werkzeugweg: {line}")
        for a in "XYZ":
            if a in w:
                v = float(w[a])
                # Gleicher gerundeter Wert, aber trotzdem geschrieben: Der
                # ungerundete Wert war leicht anders. Winzig verschieben,
                # damit das Wort beim Neuschreiben erhalten bleibt.
                pos[a] = v + 1e-7 if v == pos[a] else v
        tp.moves.append(Move(rapid, pos["X"], pos["Y"], pos["Z"], None if rapid else feed))
    return job
