# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Kollisionsprüfung: spielt die Fräswege auf einer Höhenkarte des Materials durch
(wie die 3D-Vorschau, nur ohne Bild) und meldet Eilgänge, bei denen der Fräser
mit seinem ganzen Körper Material trifft, das in diesem Moment noch steht.

Koordinaten: Materialkoordinaten der jeweiligen Seite (Nullpunkt unten links auf
der Oberfläche, Z nach oben). Das Material ist das Rechteck 0…breite × 0…hoehe
von 0 bis -dicke; daneben ist Luft.
"""
from __future__ import annotations

import math
from typing import Dict, List, Optional, Sequence, Tuple

ZELLEN = 2.0e6        # höchstens so viele Rasterzellen (Rechenzeit)
TOLERANZ = 0.03       # mm: so viel darf ein Eilgang Material «streifen» (Rundung des Rasters)


class Werkzeugform:
    """Höhe der Fräserunterseite in Abstand d von der Achse (relativ zur Spitze)."""

    def __init__(self, durchmesser: float, form: str = "flach", v_winkel: float = 60.0,
                 spitze: float = 0.0, eckradius: float = 0.0):
        self.r = max(0.01, float(durchmesser) / 2)
        self.form = form
        self.v_winkel = float(v_winkel)
        self.tan = math.tan(math.radians(max(1.0, min(179.0, float(v_winkel))) / 2))
        self.spitze = max(0.0, float(spitze)) / 2
        self.er = max(0.0, min(float(eckradius), self.r))

    def profil(self, d):
        import numpy as np
        d = np.asarray(d, dtype=float)
        if self.form == "v":
            return np.maximum(0.0, d - self.spitze) / self.tan
        if self.form == "konisch":            # Kugelspitze (Radius er), tangential in den Kegel
            rb = max(0.01, self.er)
            halb = math.atan(self.tan)
            dt, ht = rb * math.cos(halb), rb - rb * math.sin(halb)
            return np.where(d <= dt, rb - np.sqrt(np.maximum(0.0, rb * rb - np.minimum(d, rb) ** 2)),
                            ht + (d - dt) / self.tan)
        if self.er > 0:
            innen = self.r - self.er
            dd = np.clip(d - innen, 0.0, self.er)
            return np.where(d <= innen, 0.0, self.er - np.sqrt(np.maximum(0.0, self.er ** 2 - dd ** 2)))
        return np.zeros_like(d)

    def als_werkzeug(self) -> Dict:
        """Werkzeug für hirch_relief.abtragen (gleiches Profil)."""
        if self.form == "v":
            return {"typ": "v", "durchmesser": 2 * self.r, "v_winkel": self.v_winkel, "spitze": 2 * self.spitze}
        if self.form == "konisch":
            return {"typ": "konisch", "durchmesser": 2 * self.r, "v_winkel": self.v_winkel, "eckradius": self.er}
        if self.er > 0:
            return {"typ": "eckradius", "durchmesser": 2 * self.r, "eckradius": self.er}
        return {"typ": "schaft", "durchmesser": 2 * self.r}


def _kern():
    """Das C++-Modul hirch_relief (3D-Fräsung), wenn es gebaut ist – sonst None."""
    try:
        from . import hirch_relief
        return hirch_relief if hasattr(hirch_relief, "abtragen") else None
    except ImportError:
        return None


class Hoehenkarte:
    def __init__(self, breite: float, hoehe: float, dicke: float, zellen: float = ZELLEN):
        import numpy as np
        self.b, self.h, self.dicke = float(breite), float(hoehe), float(dicke)
        self.z = max(0.05, math.sqrt(max(1e-6, self.b * self.h) / zellen))
        self.nx = max(1, int(math.ceil(self.b / self.z)))
        self.ny = max(1, int(math.ceil(self.h / self.z)))
        self.k = np.zeros((self.ny, self.nx), dtype=np.float32)   # Oberkante je Zelle (0 = unberührt)

    def nur_kreis(self, mitte, radius):
        """Rundes Material: ausserhalb des Kreises ist kein Material (dort zählt die Zelle als durchgefräst)."""
        import numpy as np
        xs = (np.arange(self.nx) + 0.5) * self.z
        ys = (np.arange(self.ny) + 0.5) * self.z
        X, Y = np.meshgrid(xs, ys)
        aussen = np.hypot(X - float(mitte[0]), Y - float(mitte[1])) > float(radius) + 0.5 * self.z
        self.k[aussen] = -self.dicke

    def _fenster(self, a, b, r):
        x0, x1 = min(a[0], b[0]) - r, max(a[0], b[0]) + r
        y0, y1 = min(a[1], b[1]) - r, max(a[1], b[1]) + r
        i0 = max(0, int(math.floor(x0 / self.z)))
        i1 = min(self.nx, int(math.ceil(x1 / self.z)) + 1)
        j0 = max(0, int(math.floor(y0 / self.z)))
        j1 = min(self.ny, int(math.ceil(y1 / self.z)) + 1)
        return i0, i1, j0, j1

    def _abstand(self, a, b, i0, i1, j0, j1):
        """Abstand jeder Zellmitte im Fenster zur Strecke a–b und Parameter t (0…1)."""
        import numpy as np
        xs = (np.arange(i0, i1) + 0.5) * self.z
        ys = (np.arange(j0, j1) + 0.5) * self.z
        X, Y = np.meshgrid(xs, ys)
        dx, dy = b[0] - a[0], b[1] - a[1]
        L2 = dx * dx + dy * dy
        if L2 < 1e-12:
            t = np.zeros_like(X)
        else:
            t = np.clip(((X - a[0]) * dx + (Y - a[1]) * dy) / L2, 0.0, 1.0)
        px, py = a[0] + t * dx, a[1] + t * dy
        return np.hypot(X - px, Y - py), t

    @staticmethod
    def _z_entlang(a, b, t):
        """Höhe der Fräserspitze bei Parameter t; senkrechte Bewegung: die tiefste Stelle
        (der Fräser fährt dort durch alle Höhen)."""
        if math.hypot(b[0] - a[0], b[1] - a[1]) < 1e-6:
            return min(a[2], b[2]) + 0.0 * t
        return a[2] + t * (b[2] - a[2])

    def fraesen(self, a, b, wz: Werkzeugform):
        """Vorschub von a nach b: was der Fräser wegnimmt."""
        import numpy as np
        if min(a[2], b[2]) >= 0:
            return
        i0, i1, j0, j1 = self._fenster(a, b, wz.r)
        if i0 >= i1 or j0 >= j1:
            return
        d, t = self._abstand(a, b, i0, i1, j0, j1)
        zt = self._z_entlang(a, b, t)
        unten = np.where(d <= wz.r + 0.5 * self.z, zt + wz.profil(d), np.inf)
        L = math.hypot(b[0] - a[0], b[1] - a[1])
        if L > 1e-6 and abs(b[2] - a[2]) > 1e-9:
            # schräg: der Fräser liegt Richtung Abstieg tiefer als am nächsten Punkt (wie hirch_relief)
            xs = (np.arange(i0, i1) + 0.5) * self.z
            ys = (np.arange(j0, j1) + 0.5) * self.z
            X, Y = np.meshgrid(xs, ys)
            ux, uy = (b[0] - a[0]) / L, (b[1] - a[1]) / L
            tp = ((X - a[0]) * ux + (Y - a[1]) * uy) / L
            dp2 = np.maximum(0.0, (X - a[0]) ** 2 + (Y - a[1]) ** 2 - (tp * L) ** 2)
            m = (b[2] - a[2]) / L
            s = 1.0 if m < 0 else -1.0
            kand = [np.zeros_like(tp), np.ones_like(tp),
                    tp + s * np.sqrt(np.maximum(0.0, (wz.r + 0.5 * self.z) ** 2 - dp2)) / L]
            ri = wz.r - wz.er
            if ri > 0:
                kand.append(tp + s * np.sqrt(np.maximum(0.0, ri ** 2 - dp2)) / L)
            if wz.er > 0:
                R2 = wz.er ** 2 - np.maximum(0.0, np.sqrt(dp2) - ri) ** 2
                kand.append(tp + (-m * np.sqrt(np.maximum(0.0, R2)) / math.sqrt(1 + m * m)) / L)
                if ri > 0:                       # Eckradius: Torus – dazu Stichproben
                    h = np.sqrt(np.maximum(0.0, wz.r ** 2 - dp2)) / L
                    kand.extend(tp + s * h * q / 12.0 for q in range(1, 12))
            if wz.form == "v" and 1 / wz.tan > abs(m):
                kand.append(tp + (-m * np.sqrt(dp2) / math.sqrt(1 / wz.tan ** 2 - m * m)) / L)
            for tk in kand:
                tk = np.clip(tk, 0.0, 1.0)
                dk = np.hypot(X - (a[0] + tk * (b[0] - a[0])), Y - (a[1] + tk * (b[1] - a[1])))
                zk = a[2] + tk * (b[2] - a[2])
                unten = np.minimum(unten, np.where(dk <= wz.r + 0.5 * self.z + 1e-9, zk + wz.profil(dk), np.inf))
        fenster = self.k[j0:j1, i0:i1]
        np.minimum(fenster, np.maximum(unten, -self.dicke).astype(np.float32), out=fenster)

    def abtragen(self, start, ziele: Sequence[Tuple[float, float, float]], wz: Werkzeugform):
        """Vorschübe von start über alle ziele: was der Fräser wegnimmt. Mit dem C++-Modul
        (hirch_relief.abtragen) die ganze Folge auf einmal, sonst Bewegung für Bewegung."""
        if not ziele:
            return
        kern = _kern()
        if kern is None:
            pos = start
            for z in ziele:
                self.fraesen(pos, z, wz)
                pos = z
            return
        import numpy as np
        # Rasterpunkte der Karte sind die Zellmitten: (i + 0,5) · z
        kern.abtragen(self.k, 0.5 * self.z, 0.5 * self.z, self.z, np.asarray(ziele, dtype=np.float64),
                      wz.als_werkzeug(), [float(start[0]), float(start[1]), float(start[2])], -self.dicke)

    def trifft(self, a, b, wz: Werkzeugform, toleranz: float = TOLERANZ) -> Optional[Tuple[float, float, float]]:
        """Eilgang a→b: Stelle (x, y, wie tief im Material), wo der Fräser Material trifft."""
        import numpy as np
        if min(a[2], b[2]) >= -toleranz:
            return None
        i0, i1, j0, j1 = self._fenster(a, b, wz.r)
        if i0 >= i1 or j0 >= j1:
            return None
        d, t = self._abstand(a, b, i0, i1, j0, j1)
        zt = self._z_entlang(a, b, t)
        unten = zt + wz.profil(d)
        innen = d <= wz.r - 0.5 * self.z
        oben = self.k[j0:j1, i0:i1]
        # ganz durchgefräst (Oberkante = Unterseite): dort ist kein Material mehr
        ueber = np.where(innen & (oben > -self.dicke + 1e-3), oben - unten, -np.inf)
        k = int(np.argmax(ueber))
        tief = float(ueber.flat[k])
        if tief <= toleranz:
            return None
        jj, ii = divmod(k, i1 - i0)
        return ((i0 + ii + 0.5) * self.z, (j0 + jj + 0.5) * self.z, tief)


def _schraubenkopf(a, b, kopf, r: float, toleranz: float = TOLERANZ) -> Optional[Tuple[float, float, float]]:
    """Bewegung a→b gegen den Schraubenkopf (kopf: Mitte x, y, Radius, Oberkante): Stelle (x, y, wie tief
    unter der Oberkante des Kopfs), wo der Fräser ihn trifft, sonst None."""
    from .passstifte import _intervalle
    cx, cy, rk, oben = kopf
    if min(a[2], b[2]) >= oben - toleranz:
        return None
    best = None
    for t0, t1 in _intervalle(a, b, [(cx, cy, rk + r)]):
        for t in (t0, t1):
            z = a[2] + t * (b[2] - a[2])
            if oben - z > toleranz and (best is None or oben - z > best[2]):
                best = (a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]), oben - z)
    return best


def _gewinde_frei(karte: "Hoehenkarte", g: Dict) -> Optional[Dict]:
    """Bevor der Gewindefräser eintaucht: ist der Platz frei? Innengewinde – das Kernloch über die ganze
    Gewindetiefe (der Fräser taucht in der Mitte ein und fährt im Kernloch hoch); Aussengewinde – der Ring
    neben dem Zapfen. Liefert None oder {"ort", "fehlt" (mm zu wenig tief), "nichts" (dort ist noch gar nichts
    weggenommen)}."""
    import numpy as np
    cx, cy = (float(v) for v in g["mitte"])
    boden = float(g.get("oben") or 0) - float(g["tiefe"])          # so tief muss es frei sein
    xs = (np.arange(karte.nx) + 0.5) * karte.z
    ys = (np.arange(karte.ny) + 0.5) * karte.z
    i0 = max(0, int((cx - g["nenn"]) / karte.z) - 2)
    i1 = min(karte.nx, int((cx + g["nenn"]) / karte.z) + 3)
    j0 = max(0, int((cy - g["nenn"]) / karte.z) - 2)
    j1 = min(karte.ny, int((cy + g["nenn"]) / karte.z) + 3)
    if i0 >= i1 or j0 >= j1:
        return None
    X, Y = np.meshgrid(xs[i0:i1], ys[j0:j1])
    r = np.hypot(X - cx, Y - cy)
    rand = 0.15 + 0.5 * karte.z                                     # Raster und Bohrerspitze
    if g.get("innen", True):
        maske = r <= max(karte.z, float(g["kernloch"]) / 2 - rand)
    else:
        r_fraeser = float(g.get("fraeser") or 0)
        innen_r = float(g["nenn"]) / 2 + rand
        maske = (r >= innen_r) & (r <= max(innen_r, float(g["kernloch"]) / 2 + 2 * r_fraeser - rand))
    if not maske.any():
        return None
    oben = karte.k[j0:j1, i0:i1]
    ueber = np.where(maske, oben - boden, -np.inf)
    k = int(np.argmax(ueber))
    fehlt = float(ueber.flat[k])
    if fehlt <= 0.2:                                                # Toleranz: Raster, Spitze
        return None
    jj, ii = divmod(k, i1 - i0)
    nichts = bool(np.all(oben[maske] >= float(g.get("oben") or 0) - 1e-3))
    return {"ort": (float(X[jj, ii]), float(Y[jj, ii])), "fehlt": fehlt, "nichts": nichts}


_GEPRUEFT: Dict[str, List[Dict]] = {}


def pruefen(programme: Sequence[Dict], material: Dict, lesen, gewinde: Sequence[Dict] = ()) -> List[Dict]:
    """Wie _pruefen; das Ergebnis bleibt im Rechendienst gemerkt, solange Programme (Werkzeugdaten und
    Inhalt der Dateien), Material und Gewinde genau gleich sind (Fräsbahnen 25.10.: im Zwischenspeicher-
    Fall war die Prüfung der grösste Posten, Teller mit vielen Kurven ~0,8 s)."""
    import copy
    import hashlib
    import json
    try:
        h = hashlib.sha1()
        for p in programme:
            h.update(json.dumps({k: v for k, v in p.items() if k != "datei"}, sort_keys=True,
                                default=str).encode("utf-8"))
            with open(p["datei"], "rb") as fh:
                h.update(hashlib.sha1(fh.read()).digest())
        h.update(json.dumps([material, list(gewinde)], sort_keys=True, default=str).encode("utf-8"))
        schl = h.hexdigest()
    except (OSError, KeyError, TypeError, ValueError):
        return _pruefen(programme, material, lesen, gewinde)
    if schl not in _GEPRUEFT:
        if len(_GEPRUEFT) > 50:
            _GEPRUEFT.clear()
        _GEPRUEFT[schl] = _pruefen(programme, material, lesen, gewinde)
    return copy.deepcopy(_GEPRUEFT[schl])


def _pruefen(programme: Sequence[Dict], material: Dict, lesen, gewinde: Sequence[Dict] = ()) -> List[Dict]:
    """programme: [{datei, name, seite, durchmesser, form, v_winkel, spitze, eckradius}] in
    Fräsreihenfolge; material: {breite, hoehe, dicke, rund?}; lesen(datei) -> Job.
    rund (wie auftrag["rund"]): Material nur im Kreis – daneben trifft ein Eilgang nichts; mit Schraube
    ist der Kopf ein Hindernis (auch für Vorschübe, er wird nicht weggefräst).
    gewinde (engine-Ergebnis «gewinde»): vor dem Programm des Gewindefräsers muss das Kernloch (innen) bzw. der
    Ring um den Zapfen (aussen) frei sein; der Gewindefräser selbst trägt nur Gewindegänge ab (Hinterschneidung),
    in der Höhenkarte also nichts.
    Liefert [{"programm", "seite", "ort": (x, y), "tief", "art"}] – höchstens eine Stelle je Programm
    und Art («material», «schraube» oder «gewinde» mit «gewinde»: Name, «nichts»: kein Kernloch im Programm)."""
    karten: Dict[int, Hoehenkarte] = {}
    funde: List[Dict] = []
    ru = material.get("rund") or {}
    kopf = None
    if ru and ru.get("schraube"):
        rk = max(float(ru.get("kopf") or 0), float(ru.get("bohrung") or 0)) / 2
        if rk > 0:
            m_ = ru.get("mitte") or (0, 0)
            kopf = (float(m_[0]), float(m_[1]), rk, float(ru.get("ueberstand") or 0))
    for p in programme:
        seite = int(p.get("seite") or 1)
        if seite not in karten:
            karten[seite] = Hoehenkarte(material["breite"], material["hoehe"], material["dicke"])
            if ru and float(ru.get("radius") or 0) > 0:
                karten[seite].nur_kreis(ru.get("mitte") or (0, 0), float(ru["radius"]))
        karte = karten[seite]
        try:
            job = lesen(p["datei"])
        except (OSError, ValueError):
            continue
        wz = Werkzeugform(float(p.get("durchmesser") or 3), p.get("form3d") or p.get("form") or "flach",
                          float(p.get("v_winkel") or 60), float(p.get("spitze") or 0),
                          float(p.get("eckradius") or 0))
        ist_gewinde = p.get("typ") == "gewinde"
        if ist_gewinde:
            # nur die Gewinde dieses Programms (06.11.: mit dem Kernloch je Gewinde davor kommt ein Gewinde desselben
            # Fräsers in ein späteres Programm – sein Kernloch fehlte hier noch, «kein Kernloch» war ein Fehlalarm)
            hier = {str(a_.get("key") or "").split("@")[0].split("#")[0] for a_ in p.get("abschnitte") or []}
            for g in gewinde:
                if int(g.get("seite") or 1) != seite or g.get("werkzeug") != p.get("werkzeug"):
                    continue
                if hier and g.get("bearbeitung") and str(g["bearbeitung"]) not in hier:
                    continue
                f = _gewinde_frei(karte, g)
                if f is not None:
                    funde.append({"programm": p.get("name", ""), "seite": seite, "ort": f["ort"], "tief": f["fehlt"],
                                  "art": "gewinde", "gewinde": g.get("name", ""), "innen": g.get("innen", True),
                                  "nichts": f["nichts"], "bearbeitung": g.get("bearbeitung")})
        pos = (job.home[0], job.home[1], job.safe_z)
        gemeldet = False
        kopf_gemeldet = kopf is None
        lauf: List[Tuple[float, float, float]] = []     # Vorschübe seit dem letzten Eilgang
        lauf_start = pos
        for tp in job.toolpaths:
            for m in tp.moves:
                ziel = (m.x, m.y, m.z)
                if not kopf_gemeldet:
                    t = _schraubenkopf(pos, ziel, kopf, wz.r)
                    if t is not None:
                        funde.append({"programm": p.get("name", ""), "seite": seite, "ort": (t[0], t[1]),
                                      "tief": t[2], "art": "schraube"})
                        kopf_gemeldet = True
                if m.rapid:
                    if not ist_gewinde:
                        karte.abtragen(lauf_start, lauf, wz)    # erst abtragen, dann den Eilgang prüfen
                    lauf = []
                    if not gemeldet:
                        t = karte.trifft(pos, ziel, wz)
                        if t is not None:
                            funde.append({"programm": p.get("name", ""), "seite": seite,
                                          "ort": (t[0], t[1]), "tief": t[2], "art": "material"})
                            gemeldet = True
                else:
                    if not lauf:
                        lauf_start = pos
                    lauf.append(ziel)
                pos = ziel
        if not ist_gewinde:
            karte.abtragen(lauf_start, lauf, wz)
    return funde
