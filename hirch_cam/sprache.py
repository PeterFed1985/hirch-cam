# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Sprache: deutsche Texte im Quelltext, englischer Katalog in sprache/en.json.

Das Panel setzt HIRCH_SPRACHE (de/en). Übersetzt wird nur die Ausgabe an das
Panel (JSON): Meldungen, Warnungen, Namen, die Hirch CAM selbst erzeugt.
Daten aus der Zeichnung (Einstellungen, Werkzeugdatenbank, Kennungen) bleiben
unverändert.
"""
import json
import os
import re

_KATALOG = None

# Teilbäume der Ausgabe, die Daten sind und nie übersetzt werden
NICHT = {"einstellungen", "datenbank", "datenbank_wirksam", "werkzeuge_wirksam", "werkzeuge", "farbtoene",
         "postprozessoren", "ids", "bahnen", "datei", "dateien", "svg", "pfad", "ordner", "traceback",
         "id", "key", "art", "typ", "farbe", "hex", "seite", "material", "postprozessor", "xml",
         "punkte", "matrix", "bild", "gcode", "programm_datei", "notiz"}

_ZAHL = re.compile(r"-?\d+,\d+")


def sprache():
    s = (os.environ.get("HIRCH_SPRACHE") or "").strip().lower()
    if s in ("de", "en"):
        return s
    for k in ("LANGUAGE", "LC_ALL", "LANG"):
        v = os.environ.get(k)
        if v:
            return "de" if v.lower().startswith("de") else "en"
    return "de"


def englisch():
    return sprache() == "en"


def _katalog():
    global _KATALOG
    if _KATALOG is not None:
        return _KATALOG
    fest, muster = {}, []
    try:
        pfad = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sprache", "en.json")
        with open(pfad, encoding="utf-8") as f:
            daten = json.load(f)
        for de, en in daten.items():
            if not isinstance(en, str) or not en:
                continue
            if "{}" in de:
                td, te = de.split("{}"), en.split("{}")
                if len(td) == len(te):
                    rx = re.compile("^" + "(.*?)".join(re.escape(t) for t in td) + "$", re.S)
                    muster.append((sum(len(t) for t in td), rx, te))
            else:
                fest[de] = en
    except (OSError, ValueError):
        pass
    muster.sort(key=lambda m: -m[0])
    _KATALOG = (fest, muster)
    return _KATALOG


def tr(de):
    """Deutschen Text übersetzen (nur wenn Englisch eingestellt ist)."""
    if not englisch() or not isinstance(de, str) or not de:
        return de
    return _tr(de)


def _tr(de):
    fest, muster = _katalog()
    if de in fest:
        return fest[de]
    kern = de.strip(" ")
    if kern != de:                       # Einrückung bleibt, übersetzt wird der Text
        if not kern:
            return de
        i = de.index(kern)
        return de[:i] + _tr(kern) + de[i + len(kern):]
    for _l, rx, te in muster:
        m = rx.match(de)
        if not m:
            continue
        aus = te[0]
        for i, w in enumerate(m.groups()):
            if _ZAHL.fullmatch(w):
                w = w.replace(",", ".")
            aus += _tr(w) + te[i + 1]
        return aus
    # zusammengesetzt: Zeilen einzeln, sonst «Kopf: Rest», «A – B», «A · B»
    if "\n" in de:
        return "\n".join(_tr(t) for t in de.split("\n"))
    for trenner in (": ", " – ", " · "):
        if trenner in de:
            kopf, rest = de.split(trenner, 1)
            nk, nr = _tr(kopf), _tr(rest)
            if (nk, nr) != (kopf, rest):
                return nk + trenner + nr
    return de


# zusätzlich bei Befehlen, die Texte und Namen aus Dateien des Benutzers liefern
NICHT_DATEN = {"name", "text", "texte", "schrift", "objekte", "d", "style", "kern", "zeichen", "linien",
               "kurven", "vektoren", "ebenen", "stichworte", "teile", "quelle"}


def ausgabe(obj, befehl=None):
    """Ausgabe an das Panel übersetzen und bei Zoll die Masse in Texten umrechnen; Daten-Teilbäume bleiben."""
    from . import einheit
    en, zoll = englisch(), einheit.zoll()
    if not en and not zoll:
        return obj
    nicht = NICHT | NICHT_DATEN if befehl in ("gravur", "schrift", "nesting", "datenbank",
                                               "post") else NICHT
    return _ausgabe(obj, nicht, en, zoll)


def _ausgabe(o, nicht=NICHT, en=True, zoll=False):
    if isinstance(o, str):
        if en:
            o = _tr(o)
        if zoll:
            from .einheit import text
            o = text(o)
        return o
    if isinstance(o, list):
        return [_ausgabe(v, nicht, en, zoll) for v in o]
    if isinstance(o, dict):
        return {k: (v if k in nicht else _ausgabe(v, nicht, en, zoll)) for k, v in o.items()}
    return o
