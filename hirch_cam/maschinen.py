# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Mehrere Maschinen (B-25, Konzept von Peter am 06.11.2026 freigegeben).

Je Maschine eine eigene Datei im bisherigen Format (Maschinenwerte und Fräser), damit die Fräser getrennt bleiben
(Peter 04.11.: «Änderungen an den Werkzeugen einer Maschine wirken nicht auf andere»):

    <konfig>/maschinen.json            Liste (id, Name), zuletzt benutzte Maschine, Vorgaben «So fräse ich»
    <konfig>/maschinen/<id>.json       Maschinenwerte und Fräser dieser Maschine (Format von werkzeugdb)

Welche Maschine gilt: HIRCH_MASCHINE (die Oberfläche setzt die Maschine der Zeichnung), sonst die zuletzt benutzte,
sonst die erste.

Umwandlung einmal (wissen/code-pflege, Peter 06.11.): Ist noch keine Liste da, wird die bisherige werkzeuge.json die
erste Maschine «Meine Maschine» (id «m1»). Vorher wird sie gesichert (werkzeuge-vor-maschinen.json). Danach liest
Hirch CAM das alte Format nicht mehr.
"""
from __future__ import annotations

import json
import os
import shutil
import time
import uuid
from typing import Dict, List, Optional

INDEX = "maschinen.json"
ORDNER = "maschinen"
ALT = "werkzeuge.json"
ERSTE_ID = "m1"


def _konfig() -> str:
    from .orte import konfig
    return konfig()


def index_pfad() -> str:
    return os.path.join(_konfig(), INDEX)


def datei(mid: str) -> str:
    return os.path.join(_konfig(), ORDNER, "%s.json" % mid)


def _lesen(p: str) -> Optional[Dict]:
    try:
        with open(p, encoding="utf-8") as fh:
            d = json.load(fh)
        return d if isinstance(d, dict) else None
    except (OSError, ValueError):
        return None


def _schreiben(p: str, d: Dict):
    os.makedirs(os.path.dirname(p), exist_ok=True)
    tmp = p + ".neu"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(d, fh, ensure_ascii=False, indent=1)
    os.replace(tmp, p)


def _umwandeln() -> Dict:
    """Erste Liste anlegen; eine vorhandene werkzeuge.json wird Maschine m1 (mit Sicherung)."""
    k = _konfig()
    alt = os.path.join(k, ALT)
    from .sprache import englisch
    idx = {"version": 1, "maschinen": [{"id": ERSTE_ID, "name": "My machine" if englisch() else "Meine Maschine"}],
           "zuletzt": ERSTE_ID,
           "vorgaben": {}}
    if os.path.exists(alt):
        roh = _lesen(os.path.realpath(alt))
        shutil.copyfile(os.path.realpath(alt), os.path.join(k, "werkzeuge-vor-maschinen.json"))
        if roh is not None:
            v = roh.pop("vorgaben", None)              # «So fräse ich» gilt für alle Maschinen
            if isinstance(v, dict):
                idx["vorgaben"] = v
            _schreiben(datei(ERSTE_ID), roh)
        else:
            shutil.copyfile(os.path.realpath(alt), datei(ERSTE_ID))
        os.remove(alt)
    _schreiben(index_pfad(), idx)
    return idx


def index() -> Dict:
    idx = _lesen(index_pfad())
    if idx is None or not isinstance(idx.get("maschinen"), list) or not idx["maschinen"]:
        idx = _umwandeln()
    idx["maschinen"] = [m for m in idx["maschinen"] if isinstance(m, dict) and m.get("id")] or \
        [{"id": ERSTE_ID, "name": "Meine Maschine"}]
    return idx


def index_schreiben(idx: Dict):
    _schreiben(index_pfad(), idx)


def ids() -> List[str]:
    return [m["id"] for m in index()["maschinen"]]


def aktiv_id(idx: Optional[Dict] = None) -> str:
    idx = idx or index()
    alle = [m["id"] for m in idx["maschinen"]]
    for k in (os.environ.get("HIRCH_MASCHINE"), idx.get("zuletzt")):
        if k and k in alle:
            return k
    return alle[0]


def pfad_aktiv() -> str:
    return datei(aktiv_id())


def name(mid: str, idx: Optional[Dict] = None) -> str:
    for m in (idx or index())["maschinen"]:
        if m["id"] == mid:
            return str(m.get("name") or mid)
    return mid


def _neue_id(idx: Dict) -> str:
    da = {m["id"] for m in idx["maschinen"]}
    while True:
        k = "m" + uuid.uuid4().hex[:6]       # zufällig: zwei PCs legen nie dieselbe id an
        if k not in da:
            return k


def neu(name_: str, kopie_von: Optional[str] = None) -> str:
    """Neue Maschine als Kopie einer anderen (Werte und Fräser). Gibt die id zurück."""
    idx = index()
    q = kopie_von if kopie_von in [m["id"] for m in idx["maschinen"]] else aktiv_id(idx)
    mid = _neue_id(idx)
    roh = _lesen(datei(q)) or {}
    roh.pop("geloescht", None)                     # Löschspuren gehören zur Quelle
    roh["maschine_geaendert"] = round(time.time(), 3)
    _schreiben(datei(mid), roh)
    idx["maschinen"].append({"id": mid, "name": (name_ or "").strip() or "Neue Maschine"})
    index_schreiben(idx)
    return mid


def umbenennen(mid: str, name_: str):
    idx = index()
    for m in idx["maschinen"]:
        if m["id"] == mid and (name_ or "").strip():
            m["name"] = name_.strip()
            m["geaendert"] = round(time.time(), 3)
    index_schreiben(idx)


def entfernen(mid: str):
    """Maschine aus der Liste nehmen. Die letzte bleibt. Die Datei wird nur umbenannt (…-entfernt-<Zeit>.json)."""
    idx = index()
    if len(idx["maschinen"]) <= 1 or mid not in [m["id"] for m in idx["maschinen"]]:
        return
    idx["maschinen"] = [m for m in idx["maschinen"] if m["id"] != mid]
    idx.setdefault("entfernt", {})[mid] = round(time.time(), 3)
    if idx.get("zuletzt") == mid:
        idx["zuletzt"] = idx["maschinen"][0]["id"]
    index_schreiben(idx)
    p = datei(mid)
    if os.path.exists(p):
        os.replace(p, p[:-5] + time.strftime("-entfernt-%Y%m%d-%H%M%S.json"))


def zuletzt_setzen(mid: str):
    idx = index()
    if mid in [m["id"] for m in idx["maschinen"]] and idx.get("zuletzt") != mid:
        idx["zuletzt"] = mid
        index_schreiben(idx)


def liste() -> List[Dict]:
    """Für «Einstellungen › Maschinen»: je Maschine Name, Steuerung und die wichtigsten Werte."""
    from . import werkzeugdb
    idx = index()
    akt = aktiv_id(idx)
    aus = []
    for m in idx["maschinen"]:
        db = werkzeugdb.laden(datei(m["id"]))
        ma = db["maschine"]
        aus.append({"id": m["id"], "name": m.get("name") or m["id"], "aktiv": m["id"] == akt,
                    "x": ma.get("x"), "y": ma.get("y"), "drehzahl_max": ma.get("drehzahl_max"),
                    "postprozessor": ma.get("postprozessor"), "fraeser": len(db["werkzeuge"])})
    return aus


def _passend(w: Dict, liste: List[Dict]) -> Optional[Dict]:
    """Der gleiche Fräser in einer anderen Liste: gleiche Form und Masse; bei mehreren der mit gleichem Namen."""
    from . import werkzeugdb
    treffer = [d for d in liste if isinstance(d, dict) and werkzeugdb._geometrie_gleich(w, d)]
    return next((d for d in treffer if d.get("name") == w.get("name")), treffer[0] if treffer else None)


def vergleich(ziel: str, werkzeuge: Dict[str, Dict]) -> List[Dict]:
    """Maschine wechseln (Konzept B-25): je Fräser der Zeichnung den gleichen bei der Zielmaschine suchen."""
    from . import werkzeugdb
    db = werkzeugdb.laden(datei(ziel))
    aus = []
    for slot in sorted(werkzeuge or {}):
        w = werkzeuge[slot] or {}
        d = _passend(w, db["werkzeuge"])
        aus.append({"slot": slot, "name": werkzeugdb.name(w), "gefunden": werkzeugdb.name(d) if d else ""})
    return aus


def wechseln(ziel: str, werkzeuge: Dict[str, Dict], uebernehmen: bool) -> List[str]:
    """Zielmaschine wird die zuletzt benutzte; auf Wunsch kommen fehlende Fräser der Zeichnung zu ihr.
    Gibt die Namen der übernommenen Fräser zurück."""
    from . import werkzeugdb
    neu = []
    if uebernehmen:
        db = werkzeugdb.laden(datei(ziel))
        for slot in sorted(werkzeuge or {}):
            w = dict(werkzeuge[slot] or {})
            if not w or _passend(w, db["werkzeuge"]):
                continue
            for k in ("id", "db_id", "nummer", "empfehlung", "geaendert", "auto", "nur_hier"):
                w.pop(k, None)
            gespeichert = werkzeugdb.speichern(w, datei(ziel))
            db["werkzeuge"].append(gespeichert)
            neu.append(werkzeugdb.name(gespeichert))
    zuletzt_setzen(ziel)
    return neu
