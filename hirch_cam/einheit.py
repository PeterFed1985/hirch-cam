# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Einheit der Anzeige: mm oder Zoll (Peter 08.11.2026: «Inch und Millimeter für den Release»).

Intern ist alles mm – Dateien, Einstellungen, Werkzeuge, Rechenkern, G-Code (G21). Die Oberfläche setzt HIRCH_EINHEIT
(mm/in). Umgerechnet werden nur Texte an die Oberfläche (sprache.ausgabe ruft text() auf): jede Zahl vor «mm»,
«mm/min», «mm²» – auch «600 × 900 mm». Zahlen in Datenfeldern bleiben mm; die zeigt die Oberfläche selbst um.
Gegenstück in C++: einheit_text() in hirch-sprache.cpp (gleiche Regeln).
"""
import os
import re

ZOLL_MM = 25.4

_Z = r"-?\d+(?:[ \u202f]\d{3})*(?:[.,]\d+)?(?!\d)"
_RE = re.compile(r"((?:" + _Z + r")(?:\s*(?:×|x|…)\s*" + _Z + r")*)?(\s?)(mm/min|mm²|mm)(?![A-Za-zÄÖÜäöüß])")
_ZAHL = re.compile(_Z)
# «Ø 6» ohne Einheit (Fräser): auch eine Länge in mm
# (nicht, wenn schon eine Einheit oder ein Bruch folgt: «Ø 0,236 Zoll», «Ø 1/4 in» bleiben)
_DURCHMESSER = re.compile(r"Ø\s?(" + _Z + r")(?![.,/-]?\d)(?!\s?(?:mm|°|%|Zoll|in(?!\s*[A-Za-z])|\"|×))")


def zoll() -> bool:
    return (os.environ.get("HIRCH_EINHEIT") or "").strip().lower() == "in"


def name(groesse: str = "laenge") -> str:
    """Deutsch «Zoll» (sonst verwechselbar mit dem Wort «in»), englisch «in»."""
    z = zoll()
    if not z:
        return {"vorschub": "mm/min", "flaeche": "mm²"}.get(groesse, "mm")
    from .sprache import englisch
    e = "in" if englisch() else "Zoll"
    return {"vorschub": e + "/min", "flaeche": e + "²"}.get(groesse, e)


def _faktor(groesse: str) -> float:
    return ZOLL_MM * ZOLL_MM if groesse == "flaeche" else ZOLL_MM


def _stellen(stellen_mm: int, groesse: str) -> int:
    if groesse == "vorschub":
        return max(1, stellen_mm)
    if groesse == "flaeche":
        return max(2, stellen_mm)
    return min(max(stellen_mm + 1, 3), 4)


def _zahl_text(v: float, stellen: int, komma: bool) -> str:
    s = ("%.*f" % (stellen, v))
    if "." in s:
        s = s.rstrip("0").rstrip(".")
    if s == "-0":
        s = "0"
    return s.replace(".", ",") if komma else s


def bruch(zoll_wert: float) -> str:
    """Zoll als Bruch wie im Handel: «1/4», «1-1/2» (Nenner bis 32, ab 1/32, Abweichung bis 0,006 mm); sonst ''.
    Gegenstück: zoll_bruch() in hirch-sprache.cpp."""
    if not (1 / 32 - 1e-9 <= zoll_wert <= 12):   # ab 1/32 (kleinster Fräser der Liste, Windows 10.11.)
        return ""
    r = round(zoll_wert * 32)
    if abs(zoll_wert - r / 32) * ZOLL_MM > 0.006:
        return ""
    ganz, rest, nenner = r // 32, r % 32, 32
    while rest and rest % 2 == 0:
        rest //= 2
        nenner //= 2
    if not rest:
        return str(ganz)
    return (f"{ganz}-" if ganz else "") + f"{rest}/{nenner}"


def zeigen(mm: float, groesse: str = "laenge") -> float:
    return mm / _faktor(groesse) if zoll() else mm


def lesen(anzeige: float, groesse: str = "laenge") -> float:
    return anzeige * _faktor(groesse) if zoll() else anzeige


def zahl(mm: float, stellen: int = 2, groesse: str = "laenge", komma=None) -> str:
    """«12,5 mm» bzw. «0,492 in»."""
    if komma is None:
        from .sprache import englisch
        komma = not englisch()
    z = zoll()
    v = mm / _faktor(groesse) if z else mm
    return _zahl_text(v, _stellen(stellen, groesse) if z else stellen, komma) + " " + name(groesse)


def _wert(roh: str):
    w = roh.replace(" ", "").replace("\u202f", "").replace(",", ".")
    try:
        return float(w), (len(w.split(".")[1]) if "." in w else 0)
    except ValueError:
        return None, 0


def text(t: str) -> str:
    """Zahlen mit «mm», «mm/min», «mm²» und «Ø 6» (Fräser, ohne Einheit) in Zoll umrechnen (nur bei
    HIRCH_EINHEIT=in). Text, der schon umgerechnet ist, bleibt gleich (es steht dann kein «mm» mehr darin)."""
    if not zoll() or not isinstance(t, str) or ("mm" not in t and "Ø" not in t):
        return t
    from .sprache import englisch
    komma = not englisch()

    def zahl_neu(roh: str, groesse: str, durchmesser: bool = False) -> str:
        v, stellen = _wert(roh)
        if v is None:
            return roh
        b = bruch(v / ZOLL_MM) if durchmesser and groesse == "laenge" else ""
        return b or _zahl_text(v / _faktor(groesse), _stellen(stellen, groesse), komma)

    # zuerst «Ø 6» ohne Einheit (danach steht dort «in»/«Zoll», nie wieder «mm»)
    if "Ø" in t:
        t = _DURCHMESSER.sub(lambda m: m.group(0)[:m.start(1) - m.start(0)] + zahl_neu(m.group(1), "laenge", True) + " "
                             + name(), t)
    if "mm" not in t:
        return t
    quelle = t

    def ersetzen(m):
        start = m.start()
        if m.group(1) is None and start > 0 and quelle[start - 1].isalpha():
            return m.group(0)                       # «mm» in einem Wort, z. B. «Programm»
        einheit = m.group(3)
        groesse = "vorschub" if einheit == "mm/min" else ("flaeche" if einheit == "mm²" else "laenge")
        roh = m.group(1) or ""
        # «Ø 6,35 mm» → «Ø 1/4 Zoll» (eine Zahl direkt nach Ø)
        dm = quelle[:start].rstrip().endswith("Ø") and len(_ZAHL.findall(roh)) == 1
        kette = _ZAHL.sub(lambda z: zahl_neu(z.group(0), groesse, dm), roh)
        return kette + m.group(2) + name(groesse)

    return _RE.sub(ersetzen, t)

