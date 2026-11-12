# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Gewindefräsen (Türkis), wie FreeCAD «ThreadMilling».

Gezeichnet wird ein Kreis mit dem Nenndurchmesser (Ø 8 für M8) oder – Innengewinde – mit dem
Kernloch (Ø 6,8 für M8, wie man es bohrt; `aus_kreis`). Der
Gewindefräser fährt schraubenförmig (Helix) von unten nach oben, je Umdrehung
um die Steigung höher:

- Innengewinde: im vorgebohrten Kernloch, Fräserspitzen bis zum Nenn-Ø.
  Das Kernloch (Ø Nenn − 1.0825 · P) kann dieselbe Bearbeitung vorher mit
  einem Schaftfräser als Helix fräsen («Kernloch mit Werkzeug …»).
- Aussengewinde: um einen stehenden Zapfen herum, Fräserspitzen bis zum
  Kern-Ø (Nenn − 1.2269 · P).

Rechtsgewinde: von unten nach oben gegen den Uhrzeigersinn (Gleichlauf bei
rechtsdrehender Spindel), Linksgewinde im Uhrzeigersinn. An- und Abfahren im
Halbkreis, damit an der Flanke keine Marke bleibt. Mehrere Durchgänge teilen
die Gewindetiefe radial auf (hartes Material, kleiner Fräser).

Ein Gewindefräser mit mehreren Zähnen (Abstand = Steigung) schneidet auf
einmal mehrere Gänge; er braucht entsprechend weniger Umdrehungen.
"""
from __future__ import annotations

import math
from typing import Dict, List, Optional, Sequence, Tuple

from .bahn import Bahnbauer, Punkt, kurze_reihenfolge

# ISO-Regelgewinde: Nenndurchmesser → Steigung
REGELGEWINDE = {1.0: 0.25, 1.2: 0.25, 1.6: 0.35, 2.0: 0.4, 2.5: 0.45, 3.0: 0.5, 3.5: 0.6, 4.0: 0.7,
                5.0: 0.8, 6.0: 1.0, 8.0: 1.25, 10.0: 1.5, 12.0: 1.75, 14.0: 2.0, 16.0: 2.0,
                18.0: 2.5, 20.0: 2.5, 22.0: 2.5, 24.0: 3.0, 27.0: 3.0, 30.0: 3.5, 36.0: 4.0}


# Bohrer für das Kernloch der Regelgewinde (DIN 336 / DIN 13-1): so zeichnet man das Loch
KERNLOCH_BOHRER = {1.0: 0.75, 1.2: 0.95, 1.6: 1.25, 2.0: 1.6, 2.5: 2.05, 3.0: 2.5, 3.5: 2.9, 4.0: 3.3,
                   5.0: 4.2, 6.0: 5.0, 8.0: 6.8, 10.0: 8.5, 12.0: 10.2, 14.0: 12.0, 16.0: 14.0,
                   18.0: 15.5, 20.0: 17.5, 22.0: 19.5, 24.0: 21.0, 27.0: 24.0, 30.0: 26.5, 36.0: 32.0}
NENN_FANG = 0.15        # so weit darf der Kreis vom Nenn-Ø abweichen
KERN_FANG = 0.12        # … vom Kernloch (Bohrer oder D − 1,0825 P)


def aus_kreis(d: float, steigung: Optional[float] = None, innen: bool = True):
    """Gezeichneter Kreis → (Nenn-Ø, Steigung oder None, Kernloch-Ø oder None, Hinweis oder "").

    Innengewinde: der Kreis darf den Nenn-Ø (Ø 12 für M12, wie bisher) oder das Kernloch zeigen
    (Ø 10,2 für M12 – so bohrt man es, Peter 24.10.). Kernloch-Ø: das Loch bleibt so gross wie gezeichnet.
    Passt beides zu verschiedenen Gewinden (Ø 12 = M12 oder Kernloch M14), entscheidet eine eingetragene
    Steigung; sonst gilt wie bisher der Nenn-Ø, mit Hinweis. Aussengewinde: der Kreis ist der Zapfen = Nenn-Ø."""
    d = float(d)
    p_ein = float(steigung) if steigung else None
    nenn_k = [n for n in REGELGEWINDE if abs(n - d) <= NENN_FANG]
    if not innen:
        return d, p_ein or (regelsteigung(d) if nenn_k else None), None, ""

    def kern_passt(n, p):
        return (abs(d - kernloch(n, p)) <= KERN_FANG or abs(d - (n - p)) <= KERN_FANG or
                (abs(p - REGELGEWINDE[n]) < 1e-9 and abs(d - KERNLOCH_BOHRER[n]) <= KERN_FANG))

    if p_ein:
        kern_k = [n for n in REGELGEWINDE if kern_passt(n, p_ein)]
        nenn_p = [n for n in nenn_k if abs(REGELGEWINDE[n] - p_ein) < 1e-9]
        if kern_k and not nenn_k:
            return kern_k[0], p_ein, d, ""
        if kern_k and nenn_k and not nenn_p and any(abs(REGELGEWINDE[n] - p_ein) < 1e-9 for n in kern_k):
            n = next(n for n in kern_k if abs(REGELGEWINDE[n] - p_ein) < 1e-9)
            return n, p_ein, d, ""         # die Steigung passt nur zum Kernloch-Gewinde
        if kern_k and nenn_k and nenn_p and any(abs(REGELGEWINDE[n] - p_ein) < 1e-9 for n in kern_k):
            n2 = next(n for n in kern_k if abs(REGELGEWINDE[n] - p_ein) < 1e-9)
            return d, p_ein, None, ("Ø %s mm mit Steigung %s: als M%g gerechnet (Kreis = Nenn-Ø) – es könnte "
                                    "auch das Kernloch für M%g sein." % (_z(d), _z(p_ein), nenn_p[0], n2))
        return d, p_ein, None, ""
    kern_k = [n for n in REGELGEWINDE if kern_passt(n, REGELGEWINDE[n])]
    if nenn_k and kern_k and kern_k[0] not in nenn_k:
        n1, n2 = nenn_k[0], kern_k[0]
        if abs(REGELGEWINDE[n1] - REGELGEWINDE[n2]) > 1e-9:
            rat = "bei der Bearbeitung die Steigung %s mm eintragen" % _z(REGELGEWINDE[n2])
        else:                              # gleiche Steigung (M14/M16): nur der Kreis kann es sagen
            rat = "den Kreis mit dem Nenn-Ø %s mm zeichnen" % _z(n2)
        return d, REGELGEWINDE[n1], None, (
            "Ø %s mm: als M%g gerechnet (Kreis = Nenn-Ø). Ist es das Kernloch für M%g, %s."
            % (_z(d), n1, n2, rat))
    if nenn_k:
        return d, REGELGEWINDE[nenn_k[0]], None, ""
    if kern_k:
        return kern_k[0], REGELGEWINDE[kern_k[0]], d, ""
    return d, None, None, ""


def _z(v: float) -> str:
    return ("%g" % round(float(v), 3)).replace(".", ",")


def regelsteigung(nenn: float) -> Optional[float]:
    """Steigung des ISO-Regelgewindes, wenn der Durchmesser eines ist (± 0.15 mm)."""
    k = min(REGELGEWINDE, key=lambda d: abs(d - nenn))
    return REGELGEWINDE[k] if abs(k - nenn) <= 0.15 else None


def norm_name(nenn: float, steigung: float) -> str:
    k = min(REGELGEWINDE, key=lambda d: abs(d - nenn))
    if abs(k - nenn) <= 0.15:
        if abs(REGELGEWINDE[k] - steigung) < 1e-6:
            return "M%g" % k
        return "M%g×%g" % (k, steigung)
    return "Ø %g × %g" % (nenn, steigung)


def kernloch(nenn: float, steigung: float) -> float:
    """Kernloch-Ø für ein Innengewinde (ISO, D − 1.0825 P)."""
    return nenn - 1.0825 * steigung


def kern_aussen(nenn: float, steigung: float) -> float:
    """Kern-Ø eines Aussengewindes (ISO, d − 1.2269 P)."""
    return nenn - 1.2269 * steigung


def _schritte(radius: float, toleranz: float) -> int:
    if radius <= toleranz:
        return 16
    n = math.ceil(math.pi / math.acos(max(-1.0, 1.0 - toleranz / radius)))
    return max(24, min(720, n))


def gewinde(
    kreise: Sequence[Tuple[Punkt, float]],
    werkzeug: Dict,
    laenge: float,
    vorschub: float,
    eintauchen: float,
    steigung: Optional[float] = None,
    innen: bool = True,
    rechts: bool = True,
    durchgaenge: int = 1,
    passung: float = 0.0,
    freifahrhoehe: float = 3.0,
    startabstand: float = 0.5,
    toleranz: float = 0.01,
    start=(0.0, 0.0, 20.0),
    name: str = "Gewinde",
):
    """kreise: [((x, y), Ø des gezeichneten Kreises)] – Nenn-Ø oder Kernloch (aus_kreis). werkzeug: Gewindefräser (durchmesser = Ø über
    die Zahnspitzen, zaehne = Anzahl Gewindezähne, steigung = Zahnabstand bei
    mehreren Zähnen). Liefert (Werkzeugweg, Warnungen, Infos je Kreis)."""
    warnungen: List[str] = []
    infos: List[Dict] = []
    d_wz = float(werkzeug.get("durchmesser", 6))
    r_wz = d_wz / 2
    zaehne = max(1, int(werkzeug.get("zaehne") or 1))
    wz_steigung = float(werkzeug.get("steigung") or 0)
    b = Bahnbauer(vorschub, eintauchen, freifahrhoehe, startabstand, start)
    laenge = max(0.1, float(laenge))
    durchgaenge = max(1, int(durchgaenge))
    if werkzeug.get("typ") not in (None, "gewinde"):
        warnungen.append("Werkzeug ist kein Gewindefräser – gerechnet wie ein Einzahn-Gewindefräser.")

    rest = list(kreise)
    folge = kurze_reihenfolge(len(rest), lambda i, j: math.dist(rest[i][0], rest[j][0]),
                              lambda i: math.dist(b.pos[:2], rest[i][0]))
    for k in folge:
        (cx, cy), d_kreis = rest[k]
        nenn, p_auto, kern_gezeichnet, hinweis = aus_kreis(d_kreis, steigung, innen)
        if hinweis and hinweis not in warnungen:
            warnungen.append(hinweis)
        p = float(steigung) if steigung else (p_auto or 0)
        if p <= 0:
            warnungen.append("Ø %g mm ist kein Normgewinde – bitte die Steigung angeben." % d_kreis)
            continue
        if zaehne > 1 and wz_steigung > 0 and abs(wz_steigung - p) > 1e-3:
            warnungen.append("Der Gewindefräser hat %g mm Zahnabstand, das Gewinde %g mm Steigung – "
                             "mit mehreren Zähnen geht nur dieselbe Steigung." % (wz_steigung, p))
            continue
        if innen:
            # als Kernloch gezeichnet: das Loch so gross wie gezeichnet (Bohrer-Ø, z. B. 10,2 für M12)
            loch = kern_gezeichnet if kern_gezeichnet else kernloch(nenn, p)
            ziel = nenn / 2 + passung - r_wz              # Fräsermitte bei voller Tiefe
            tiefe_gang = 0.5413 * p                        # radiale Gewindetiefe innen
            if d_wz >= loch - 0.05:
                warnungen.append("Gewindefräser Ø %g passt nicht ins Kernloch Ø %.2f von %s."
                                 % (d_wz, loch, norm_name(nenn, p)))
                continue
        else:
            loch = kern_aussen(nenn, p)
            ziel = loch / 2 - passung + r_wz
            tiefe_gang = 0.6134 * p
        infos.append({"name": norm_name(nenn, p), "steigung": p, "kernloch": loch, "innen": innen,
                      "mitte": (cx, cy), "nenn": nenn, "gezeichnet": d_kreis})
        if ziel <= 0.05:
            warnungen.append("Gewindefräser zu gross für %s." % norm_name(nenn, p))
            continue
        # radiale Durchgänge: von wenig zu voller Tiefe
        radien = []
        for i in range(1, durchgaenge + 1):
            rest_tiefe = tiefe_gang * (durchgaenge - i) / durchgaenge
            radien.append(ziel - rest_tiefe if innen else ziel + rest_tiefe)
        # Umdrehungen: unterster Zahn von −laenge bis die oberen Zähne die Oberfläche erreichen
        abdeckung = (zaehne - 1) * p
        umdrehungen = max(1.0, (laenge - abdeckung) / p + 0.25)
        # Gegen den Uhrzeigersinn (+1) = Rechtsgewinde beim Hochfahren
        dreh = 1 if rechts else -1
        for R in radien:
            _helix(b, cx, cy, R, dreh, -laenge, umdrehungen, p, innen, r_wz, toleranz)
    return b.weg(name), warnungen, infos


def _helix(b: Bahnbauer, cx, cy, R, dreh, z0, umdrehungen, p, innen, r_wz, toleranz):
    """Eine Gewinde-Helix mit Halbkreis-An- und -Abfahrt."""
    n = _schritte(R, toleranz)
    # Startpunkt auf dem Kreis (Winkel 0), Tangente in Fahrrichtung
    t = (0.0, float(dreh))
    nach_innen = (-1.0, 0.0)
    abfall = nach_innen if innen else (1.0, 0.0)
    rl = (R / 2) if innen else max(r_wz, 0.5)          # Radius der An-/Abfahrt
    rl = max(0.05, min(rl, R / 2 if innen else rl))
    p0 = (cx + R, cy)
    mitte_ein = (p0[0] + abfall[0] * rl, p0[1] + abfall[1] * rl)
    # Anfahrt: Halbkreis von der Abfallseite tangential in den Startpunkt
    m = max(8, n // 4)
    ein = []
    for i in range(m + 1):
        w = math.pi * i / m
        # Vektor vom Bogenmittelpunkt: −abfall·cos … dreht so, dass bei w=π die Tangente t ist
        vx = abfall[0] * math.cos(w) - t[0] * math.sin(w)
        vy = abfall[1] * math.cos(w) - t[1] * math.sin(w)
        ein.append((mitte_ein[0] + rl * vx, mitte_ein[1] + rl * vy))
    # ein[0] = Mitte + rl·abfall (gegenüber), ein[-1] = Mitte − rl·abfall = p0
    b.anfahren(*ein[0])
    b.eintauchen(z0, None)                    # im Kernloch bzw. neben dem Zapfen: frei
    z_an = z0
    for q in ein[1:]:
        b.fraesen(q[0], q[1], z_an)
    schritte = max(1, int(round(umdrehungen * n)))
    for i in range(1, schritte + 1):
        w = dreh * 2 * math.pi * i / n
        b.fraesen(cx + R * math.cos(w), cy + R * math.sin(w), z0 + p * i / n)
    # Abfahrt: Halbkreis zurück auf die Abfallseite
    w_end = dreh * 2 * math.pi * schritte / n
    pe = (cx + R * math.cos(w_end), cy + R * math.sin(w_end))
    te = (-math.sin(w_end) * dreh, math.cos(w_end) * dreh)
    radial = (math.cos(w_end), math.sin(w_end))
    ab = (-radial[0], -radial[1]) if innen else radial
    me = (pe[0] + ab[0] * rl, pe[1] + ab[1] * rl)
    z_ende = b.pos[2]
    for i in range(1, m + 1):
        w = math.pi * i / m
        vx = -ab[0] * math.cos(w) + te[0] * math.sin(w)
        vy = -ab[1] * math.cos(w) + te[1] * math.sin(w)
        b.fraesen(me[0] + rl * vx, me[1] + rl * vy, z_ende)
    b.abheben()
