# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Spindel und Halterung: streift beim 3D-Fräsen etwas anderes als die Schneide das Material?

Bei tiefen 3D-Teilen taucht der Fräser so weit ein, dass der Schaft über der Schneide, die
Spannmutter, die Spindel oder ihre Halterung das stehende Material berühren können. Die Masse
stehen in den Maschineneinstellungen (Einstellungen › Maschine › «Spindel und Halterung …»),
die Ausspannlänge je Werkzeug im Werkzeugdialog.

Aufbau über der Fräserspitze (alles achsensymmetrisch zur Spindel, die Halterung ein Quader):

    Schaft      Ø Schaft            von der Schneidenlänge bis zur Ausspannlänge A
    Spannmutter Ø C                 von A bis A + B
    Spindel     Ø D                 von A + B bis A + B + E
    Halterung   Breite × Tiefe      ab A + B + E

Geprüft wird wie in kollision.py auf einer Höhenkarte des Materials, die alle Bahnen der Seite
in Fräsreihenfolge abträgt: was in diesem Moment noch steht, darf keiner dieser Körper
berühren. Geprüft werden die Bahnen von 3D-Schruppen und 3D-Schlichten und die Bahnen, deren
Tiefe ab der Oberfläche des 3D-Modells gemessen wird. Ein nicht eingetragenes Mass (0) lässt
den Körper weg.

Bei einer Kollision bricht engine.rechnen ab: Peters Entscheid (16.10.2026) – warnen, die Stelle
markieren und keinen G-Code schreiben.
"""
from __future__ import annotations

import math
from typing import Dict, Iterable, List, Optional, Sequence

ARTEN_3D = ("relief_schruppen", "relief_schlichten", "boeden_waende")
TOLERANZ = 0.05        # mm: so nahe darf ein Körper dem Material kommen (Rundung des Rasters)
ZUGABE_AUSSPANNEN = 5.0  # mm über Schneide und Übergang, wenn keine Ausspannlänge eingetragen ist
# Übergang Schneide → Schaft (Kegel), wenn nichts eingetragen ist: Winkel zur Achse
UEBERGANG_SCHMAL = 20.0  # Schneide schmaler als der Schaft (z. B. 1 mm auf 3-mm-Schaft)
UEBERGANG_BREIT = 45.0   # Schneide breiter als der Schaft (Planfräser)
UEBERGANG_STUFE = 0.5    # mm: der Kegel wird für die Prüfung in Ringe dieser Höhe zerlegt
SCHRITT_QUADER = 2.0   # mm: Abstand der Prüfpunkte für die Halterung entlang einer Bewegung
MARKE_PUNKTE = 12000   # höchstens so viele Punkte des streifenden Bahnstücks für die Zeichnung

TEIL_NAMEN = {"schaft": "der Schaft", "mutter": "die Spannmutter", "spindel": "die Spindel",
              "halterung": "die Halterung"}


def _z(werte: Dict, k: str) -> float:
    try:
        return max(0.0, float(werte.get(k) or 0.0))
    except (TypeError, ValueError):
        return 0.0


def uebergang(w: Dict) -> float:
    """Höhe des Kegels zwischen Schneide und Schaft (mm). Eingetragen oder – bei 0 – aus den
    Durchmessern: 20° zur Achse, wenn der Schaft dicker ist, 45°, wenn die Schneide breiter ist.
    V-Fräser und Bohrer: keiner (dort ist der Ø schon der Schaft)."""
    u = _z(w, "uebergang")
    if u > 0:
        return u
    if w.get("typ") in ("v", "bohrer"):
        return 0.0
    d, s = _z(w, "durchmesser"), _z(w, "schaft")
    if s <= 0 or abs(s - d) < 0.01:
        return 0.0
    winkel = UEBERGANG_SCHMAL if s > d else UEBERGANG_BREIT
    return abs(s - d) / 2 / math.tan(math.radians(winkel))


def ausspannlaenge(w: Dict) -> float:
    """Wie weit der Fräser aus der Spannmutter ragt (Spitze bis Mutter); ohne Angabe
    Schneidenlänge + Übergang + 5 mm."""
    a = _z(w, "ausspannlaenge")
    return a if a > 0 else _z(w, "schneidenlaenge") + uebergang(w) + ZUGABE_AUSSPANNEN


def koerper(w: Dict, maschine: Optional[Dict]) -> List[Dict]:
    """Körper über der Fräserspitze: [{teil, form kreis|quader, r | bx, by, unten}];
    «unten» = Höhe der Unterkante über der Spitze."""
    ma = maschine or {}
    a = ausspannlaenge(w)
    sl = _z(w, "schneidenlaenge")
    aus: List[Dict] = []
    # Schaft: nur wenn er dicker ist als die Schneide (sonst bleibt er in der eigenen Spur),
    # und erst über dem Schneidenprofil (Kugel: Radius, V-Fräser: Höhe der Spitze)
    d_w = _z(w, "durchmesser")
    schaft = _z(w, "schaft")
    r_w = d_w / 2
    typ = w.get("typ")
    if typ == "kugel":
        profil = r_w
    elif typ == "eckradius":
        profil = min(_z(w, "eckradius"), r_w)
    elif typ == "v":
        tan = math.tan(math.radians(max(1.0, min(179.0, float(w.get("v_winkel") or 60))) / 2))
        profil = max(0.0, r_w - _z(w, "spitze") / 2) / tan
    elif typ == "konisch":                   # Kugelspitze, dann Kegel bis zum grössten Ø
        halb = math.radians(max(1.0, min(179.0, float(w.get("v_winkel") or 10))) / 2)
        rb = min(max(_z(w, "eckradius"), 0.01), r_w)
        dt = rb * math.cos(halb)
        profil = rb - rb * math.sin(halb) + max(0.0, r_w - dt) / math.tan(halb)
    else:
        profil = 0.0
    unten_schaft = max(sl, profil)
    if schaft > d_w + 0.01 and a > unten_schaft:
        # Kegel als Ringe: jeder mit dem Radius an seiner Oberkante (lieber etwas zu dick), der
        # letzte ist der volle Schaft
        ue = uebergang(w)
        n = max(1, min(8, math.ceil(ue / UEBERGANG_STUFE - 1e-9))) if ue > 0 else 1
        for k in range(n):
            unten = unten_schaft + k * ue / n
            if a > unten:
                aus.append({"teil": "schaft", "form": "kreis", "r": r_w + (k + 1) / n * (schaft / 2 - r_w),
                            "unten": unten})
    b, c = _z(ma, "mutter_hoehe"), _z(ma, "mutter_d")
    if b > 0 and c > 0:
        aus.append({"teil": "mutter", "form": "kreis", "r": c / 2, "unten": a})
    d, e = _z(ma, "spindel_d"), _z(ma, "spindel_laenge")
    if d > 0:
        aus.append({"teil": "spindel", "form": "kreis", "r": d / 2, "unten": a + b})
    hb, ht = _z(ma, "halter_b"), _z(ma, "halter_t")
    if hb > 0 and ht > 0 and e > 0:
        aus.append({"teil": "halterung", "form": "quader", "bx": hb / 2, "by": ht / 2, "unten": a + b + e})
    return aus


def _zahl(v: float, stellen: int = 1) -> str:
    return (f"%.{stellen}f" % v).replace(".", ",")


def _aufrunden(v: float) -> float:
    return math.ceil(v * 2 - 1e-9) / 2


class _Fund:
    def __init__(self, key, bid, slot, seite, teil, w):
        self.key, self.bid, self.slot, self.seite, self.teil, self.w = key, bid, slot, seite, teil, w
        self.tief, self.ort, self.z, self.unten = 0.0, (0.0, 0.0), 0.0, 0.0
        self.zuege: List[List[float]] = []
        self.punkte = 0

    def stueck(self, a, b):
        self.punkte += 2
        if self.punkte > MARKE_PUNKTE:          # genug, um die Stelle zu sehen
            return
        z = self.zuege[-1] if self.zuege else None
        if z and abs(z[-2] - a[0]) < 1e-6 and abs(z[-1] - a[1]) < 1e-6:
            z.extend([round(b[0], 3), round(b[1], 3)])
        else:
            self.zuege.append([round(a[0], 3), round(a[1], 3), round(b[0], 3), round(b[1], 3)])


def _pruefe_bewegung(karte, a, b, teile: Sequence[Dict]):
    """Tiefster Eingriff eines Körpers auf der Bewegung a→b: (teil, tief, x, y, z_spitze, unten)
    oder None (unten: Unterkante des Körpers über der Spitze)."""
    import numpy as np
    best = None
    zmin = min(a[2], b[2])
    for t in teile:
        if zmin + t["unten"] >= -TOLERANZ:         # Unterkante über der Materialoberfläche
            continue
        if t["form"] == "kreis":
            r = t["r"]
            i0, i1, j0, j1 = karte._fenster(a, b, r)
            if i0 >= i1 or j0 >= j1:
                continue
            d, s = karte._abstand(a, b, i0, i1, j0, j1)
            zs = karte._z_entlang(a, b, s)
            innen = d <= r - 0.5 * karte.z
        else:
            # Quader: Prüfpunkte entlang der Bewegung
            n = max(1, int(math.ceil(math.hypot(b[0] - a[0], b[1] - a[1]) / SCHRITT_QUADER)))
            for k in range(n + 1):
                f = k / n
                p = (a[0] + f * (b[0] - a[0]), a[1] + f * (b[1] - a[1]), a[2] + f * (b[2] - a[2]))
                if p[2] + t["unten"] >= -TOLERANZ:
                    continue
                r = max(t["bx"], t["by"])
                i0, i1, j0, j1 = karte._fenster(p, p, r)
                if i0 >= i1 or j0 >= j1:
                    continue
                xs = (np.arange(i0, i1) + 0.5) * karte.z
                ys = (np.arange(j0, j1) + 0.5) * karte.z
                X, Y = np.meshgrid(xs, ys)
                drin = (np.abs(X - p[0]) <= t["bx"]) & (np.abs(Y - p[1]) <= t["by"])
                oben = karte.k[j0:j1, i0:i1]
                ueber = np.where(drin & (oben > -karte.dicke + 1e-3), oben - (p[2] + t["unten"]), -np.inf)
                q = int(np.argmax(ueber))
                tief = float(ueber.flat[q])
                if tief > TOLERANZ and (best is None or tief > best[1]):
                    jj, ii = divmod(q, i1 - i0)
                    best = (t["teil"], tief, (i0 + ii + 0.5) * karte.z, (j0 + jj + 0.5) * karte.z, p[2],
                            t["unten"])
            continue
        oben = karte.k[j0:j1, i0:i1]
        unten = zs + t["unten"]
        ueber = np.where(innen & (oben > -karte.dicke + 1e-3), oben - unten, -np.inf)
        q = int(np.argmax(ueber))
        tief = float(ueber.flat[q])
        if tief > TOLERANZ and (best is None or tief > best[1]):
            jj, ii = divmod(q, i1 - i0)
            zq = float(np.asarray(zs).flat[q]) if np.ndim(zs) else float(zs)
            best = (t["teil"], tief, (i0 + ii + 0.5) * karte.z, (j0 + jj + 0.5) * karte.z, zq, t["unten"])
    return best


def pruefen(teile: Sequence[Dict], folge: Sequence[Dict], werkzeuge: Dict[str, Dict],
            auftrag: Dict, geprueft: Iterable[str]) -> Optional[Dict]:
    """teile: wie in engine.rechnen ({key, slot, seite, tp, bid}); folge: Abschnitte in
    Fräsreihenfolge (ablauf.abschnitte); geprueft: IDs der Bearbeitungen, deren Bahnen
    geprüft werden. Liefert die erste Kollision (mit dem ganzen betroffenen Bahnstück) oder None."""
    geprueft = set(geprueft)
    flaeche = auftrag.get("material_flaeche")
    dicke = float((auftrag.get("hoehen") or {}).get("materialdicke") or 0)
    if not geprueft or not flaeche or dicke <= 0:
        return None
    maschine = auftrag.get("maschine") or {}
    koerper_je = {s: koerper(w, maschine) for s, w in werkzeuge.items()}
    # Nur rechnen, wenn ein geprüfter Weg überhaupt so tief kommt, dass ein Körper unter die
    # Materialoberfläche reicht (sonst kann nichts streifen)
    noetig = False
    for t in teile:
        if t["bid"] not in geprueft or not koerper_je.get(t["slot"]):
            continue
        tiefst = min((m.z for m in t["tp"].moves), default=0.0)
        if tiefst + min(k["unten"] for k in koerper_je[t["slot"]]) < -TOLERANZ:
            noetig = True
            break
    if not noetig:
        return None

    from ..kollision import Hoehenkarte, Werkzeugform
    nach_key: Dict[str, List[Dict]] = {}
    for t in teile:
        nach_key.setdefault(t["key"], []).append(t)
    karten: Dict[int, object] = {}
    h = auftrag.get("hoehen") or {}
    pos0 = (0.0, 0.0, float(h.get("sicherheitshoehe", 20.0)))
    for ab in folge:
        seite = int(ab.get("seite") or 1)
        if seite not in karten:
            karten[seite] = Hoehenkarte(float(flaeche[0]), float(flaeche[1]), dicke)
        karte = karten[seite]
        w = werkzeuge.get(ab["slot"]) or {}
        wz = Werkzeugform(float(w.get("durchmesser") or 3),
                          {"v": "v", "kugel": "kugel", "eckradius": "kugel", "konisch": "konisch"}.get(
                              w.get("typ"), "flach"),
                          float(w.get("v_winkel") or 60), float(w.get("spitze") or 0),
                          float(w.get("durchmesser", 3)) / 2 if w.get("typ") == "kugel"
                          else float(w.get("eckradius") or 0))
        kp = koerper_je.get(ab["slot"]) or []
        tiefste = -min((k["unten"] for k in kp), default=1e9)
        for key in ab["keys"]:
            for t in nach_key.get(key, []):
                pruef = t["bid"] in geprueft and kp
                fund: Optional[_Fund] = None
                pos = pos0
                lauf: List = []                      # Vorschübe, noch nicht abgetragen
                lauf_start = pos
                for m in t["tp"].moves:
                    ziel = (m.x, m.y, m.z)
                    noetig = pruef and min(pos[2], ziel[2]) < tiefste - TOLERANZ
                    if not m.rapid:
                        if not lauf:
                            lauf_start = pos
                        lauf.append(ziel)
                    # erst abtragen (am Stück, C++), dann prüfen: was die Schneide auf dieser
                    # Bewegung selbst wegnimmt, liegt unter allen Körpern darüber
                    if noetig or m.rapid:
                        karte.abtragen(lauf_start, lauf, wz)
                        lauf = []
                    if noetig:
                        r = _pruefe_bewegung(karte, pos, ziel, kp)
                        if r is not None:
                            if fund is None:
                                fund = _Fund(key, t["bid"], ab["slot"], seite, r[0], w)
                            fund.stueck(pos, ziel)
                            if r[1] > fund.tief:
                                fund.tief, fund.ort, fund.z, fund.teil = r[1], (r[2], r[3]), r[4], r[0]
                                fund.unten = r[5]
                    pos = ziel
                karte.abtragen(lauf_start, lauf, wz)
                if fund is not None:
                    return _bericht(fund)
    return None


def _bericht(f: _Fund) -> Dict:
    w = f.w
    a = ausspannlaenge(w)
    wo = (f"bei X {_zahl(f.ort[0])} · Y {_zahl(f.ort[1])}, Fräserspitze Z {_zahl(f.z)}")
    if f.teil == "schaft":
        sl = _z(w, "schneidenlaenge")
        rat = (f"Ein Fräser mit mindestens {_zahl(_aufrunden(sl + f.tief))} mm Schneidenlänge "
               f"(jetzt {_zahl(sl)} mm) oder ein dünnerer Schaft.")
    else:
        rat = (f"Werkzeug {f.slot} mindestens {_zahl(_aufrunden(a + f.tief))} mm ausspannen "
               f"(jetzt {_zahl(a)} mm).")
    text = (f"{TEIL_NAMEN[f.teil][0].upper()}{TEIL_NAMEN[f.teil][1:]} streift das Material bis "
            f"{_zahl(f.tief)} mm tief, {wo}. {rat}")
    return {"key": f.key, "bearbeitung": f.bid, "werkzeug": f.slot, "seite": f.seite, "teil": f.teil,
            "tief": round(f.tief, 2), "ort": [round(f.ort[0], 2), round(f.ort[1], 2)], "z": round(f.z, 2),
            "ausspannlaenge": a, "text": text, "zuege": f.zuege,
            # Höhe (Z) der Stelle, an der der Körper das Material streift: von seiner Unterkante
            # bis zur Oberkante des Materials dort – für die 3D-Ansicht
            "hoehe": [round(f.z + f.unten, 2), round(f.z + f.unten + f.tief, 2)]}
