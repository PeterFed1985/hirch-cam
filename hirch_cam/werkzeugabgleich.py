# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Werkzeugkatalog mit einem Ordner abgleichen (Peter 27.10.): zwei PCs, ein Katalog.

Jeder PC hat seine eigene werkzeuge.json im Datenordner von Hirch CAM (orte.konfig()). In
«Einstellungen › Maschine» lässt sich ein Ordner wählen (z. B. ein Nextcloud-Ordner); dort liegt
eine Kopie. Beim Laden und nach jedem Speichern werden beide zusammengeführt:

- je Werkzeug gilt die neuere Änderung (Feld «geaendert», Sekunden); gelöschte Werkzeuge merken
  sich die Zeit des Löschens («geloescht»), damit sie nicht vom anderen PC zurückkommen;
- die Maschinenwerte gelten als Ganzes nach «maschine_geaendert», ausser dem, was nur auf diesen
  PC gehört (NUR_HIER: der Zielordner ist ein Pfad dieses Rechners);
- ohne Zeitangabe (ältere Dateien) gilt die neuere Datei; beim ersten Abgleich der Katalog im Ordner,
  was nur hier steht, kommt dazu;
- eigene Postprozessoren (Unterordner «postprozessoren»): je Datei die neuere.

Vor jedem Überschreiben wird gesichert (Unterordner «sicherungen», die letzten 20).

Seit B-25 (mehrere Maschinen, 07.11.) gilt das je Maschine: maschinen/<id>.json hier und im Ordner. Die Liste
maschinen.json wird ebenso zusammengeführt (Maschinen beider PCs; entfernte bleiben entfernt; Namen und die Vorgaben
«So fräse ich» nach der neueren Änderung; die zuletzt benutzte Maschine gehört zu diesem PC). Liegt im Ordner noch ein
Katalog im alten Format (werkzeuge.json), gilt er beim ersten Abgleich als Maschine m1.
"""
from __future__ import annotations

import copy
import json
import os
import shutil
import time
from typing import Dict, Optional, Tuple

NUR_HIER = ("zielordner",)
DATEI = "werkzeuge.json"
SICHERUNGEN = 20


def _konfig() -> str:
    from .orte import konfig
    return konfig()


def _einstellung_pfad() -> str:
    return os.path.join(_konfig(), "abgleich.json")


def einstellung() -> Dict:
    try:
        with open(_einstellung_pfad(), encoding="utf-8") as fh:
            e = json.load(fh)
        if isinstance(e, dict):
            return e
    except (OSError, ValueError):
        pass
    return {}


def _einstellung_schreiben(e: Dict):
    os.makedirs(_konfig(), exist_ok=True)
    tmp = _einstellung_pfad() + ".neu"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(e, fh, ensure_ascii=False, indent=1)
    os.replace(tmp, _einstellung_pfad())


def aktiv() -> bool:
    e = einstellung()
    return bool(e.get("an")) and bool(e.get("ordner"))


def _roh(pfad: str) -> Optional[Dict]:
    try:
        with open(pfad, encoding="utf-8") as fh:
            d = json.load(fh)
        return d if isinstance(d, dict) else None
    except (OSError, ValueError):
        return None


def _schluessel(d: Optional[Dict]) -> str:
    return json.dumps(d or {}, sort_keys=True, ensure_ascii=False)


def _zeit(w: Dict) -> float:
    try:
        return float(w.get("geaendert") or 0)
    except (TypeError, ValueError):
        return 0.0


def zusammenfuehren(hier: Dict, dort: Dict, hier_gilt: bool) -> Tuple[Dict, int]:
    """Beide Kataloge zusammenführen. hier_gilt: bei gleicher Zeit (oder ohne Zeit) gilt «hier».
    Gibt (Ergebnis, Anzahl Werkzeuge, die von «dort» kamen oder dort gelöscht wurden) zurück."""
    weg: Dict[str, float] = {}
    for q in (hier, dort):
        for k, v in (q.get("geloescht") or {}).items():
            try:
                weg[str(k)] = max(weg.get(str(k), 0.0), float(v))
            except (TypeError, ValueError):
                pass
    a = {w["id"]: w for w in hier.get("werkzeuge") or [] if isinstance(w, dict) and w.get("id")}
    b = {w["id"]: w for w in dort.get("werkzeuge") or [] if isinstance(w, dict) and w.get("id")}
    reihe = list(a) + [i for i in b if i not in a]
    liste, von_dort = [], 0
    for i in reihe:
        wa, wb = a.get(i), b.get(i)
        if wa is not None and wb is not None:
            ta, tb = _zeit(wa), _zeit(wb)
            w = wa if (ta > tb or (ta == tb and hier_gilt)) else wb
        else:
            w = wa if wa is not None else wb
        if i in weg and weg[i] >= _zeit(w):
            if wa is not None:
                von_dort += 1
            continue
        if w is not wa and _schluessel(w) != _schluessel(wa):
            von_dort += 1
        liste.append(copy.deepcopy(w))
    erg = copy.deepcopy(hier)
    erg["werkzeuge"] = liste
    # nur die neuesten 500 Löschungen behalten
    if weg:
        erg["geloescht"] = dict(sorted(weg.items(), key=lambda kv: -kv[1])[:500])
    ersetzt = dict(dort.get("ersetzt") or {})
    ersetzt.update(hier.get("ersetzt") or {})
    if ersetzt:
        erg["ersetzt"] = ersetzt
    ma, mb = dict(hier.get("maschine") or {}), dict(dort.get("maschine") or {})
    ta, tb = float(hier.get("maschine_geaendert") or 0), float(dort.get("maschine_geaendert") or 0)
    if mb and (tb > ta or (tb == ta and not hier_gilt)):
        m = mb
        for k in NUR_HIER:
            if k in ma:
                m[k] = ma[k]
            else:
                m.pop(k, None)
        erg["maschine"] = m
        if tb:
            erg["maschine_geaendert"] = tb
    erg["version"] = max(int(hier.get("version") or 1), int(dort.get("version") or 1))
    return erg, von_dort


def _sichern(quelle: str, name: str):
    if not os.path.exists(quelle):
        return
    o = os.path.join(_konfig(), "sicherungen")
    os.makedirs(o, exist_ok=True)
    stempel = time.strftime("%Y-%m-%d %H-%M-%S")
    shutil.copyfile(quelle, os.path.join(o, "%s %s.json" % (name, stempel)))
    alle = sorted(f for f in os.listdir(o) if f.startswith(name + " ") and f.endswith(".json"))
    for f in alle[:-SICHERUNGEN]:
        try:
            os.remove(os.path.join(o, f))
        except OSError:
            pass


def _schreiben(pfad: str, d: Dict):
    os.makedirs(os.path.dirname(pfad), exist_ok=True)
    tmp = pfad + ".neu"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(d, fh, ensure_ascii=False, indent=1)
    os.replace(tmp, pfad)


def _link_aufloesen(lokal: str):
    """War werkzeuge.json ein Link (früher in den Nextcloud-Ordner): durch eine eigene Datei ersetzen."""
    if os.path.islink(lokal):
        inhalt = _roh(lokal)
        os.remove(lokal)
        if inhalt is not None:
            _schreiben(lokal, inhalt)


def _postprozessoren(ordner: str):
    hier = os.path.join(_konfig(), "postprozessoren")
    dort = os.path.join(ordner, "postprozessoren")
    namen = set()
    for o in (hier, dort):
        if os.path.isdir(o):
            namen |= {f for f in os.listdir(o) if f.endswith(".json") and not f.startswith(".")}
    for f in namen:
        a, b = os.path.join(hier, f), os.path.join(dort, f)
        ta = os.path.getmtime(a) if os.path.exists(a) else -1
        tb = os.path.getmtime(b) if os.path.exists(b) else -1
        if abs(ta - tb) < 1:
            continue
        q, z = (a, b) if ta > tb else (b, a)
        os.makedirs(os.path.dirname(z), exist_ok=True)
        shutil.copy2(q, z)


def index_zusammenfuehren(hier: Dict, dort: Dict, hier_gilt: bool) -> Dict:
    """Liste der Maschinen beider PCs zusammenführen (maschinen.json)."""
    weg: Dict[str, float] = {}
    for q in (hier, dort):
        for k, v in (q.get("entfernt") or {}).items():
            try:
                weg[str(k)] = max(weg.get(str(k), 0.0), float(v))
            except (TypeError, ValueError):
                pass
    a = {m["id"]: m for m in hier.get("maschinen") or [] if isinstance(m, dict) and m.get("id")}
    b = {m["id"]: m for m in dort.get("maschinen") or [] if isinstance(m, dict) and m.get("id")}
    liste = []
    for i in list(a) + [i for i in b if i not in a]:
        ma, mb = a.get(i), b.get(i)
        if ma is not None and mb is not None:
            ta, tb = _zeit(ma), _zeit(mb)
            m = ma if (ta > tb or (ta == tb and hier_gilt)) else mb
        else:
            m = ma if ma is not None else mb
        if i in weg and weg[i] >= _zeit(m):
            continue
        liste.append(copy.deepcopy(m))
    erg = copy.deepcopy(hier)
    erg["maschinen"] = liste or copy.deepcopy(hier.get("maschinen") or [])
    if weg:
        erg["entfernt"] = weg
    for feld in ("vorgaben", "werkstoffe"):           # «So fräse ich», eigene Werkstoffe und Fachwerte
        va, vb = float(hier.get(feld + "_geaendert") or 0), float(dort.get(feld + "_geaendert") or 0)
        if vb > va or (vb == va and not hier_gilt and feld in dort):
            erg[feld] = copy.deepcopy(dort.get(feld) or {})
            if vb:
                erg[feld + "_geaendert"] = vb
    if erg.get("zuletzt") not in [m["id"] for m in erg["maschinen"]] and erg["maschinen"]:
        erg["zuletzt"] = erg["maschinen"][0]["id"]
    return erg


def _paar(lokal: str, kopie: str, staende: Dict, name: str, zusammen, sicher: str, erzwingen: bool = False) -> int:
    """Eine Datei hier und im Ordner abgleichen. Gibt die Zahl der übernommenen Werkzeuge zurück."""
    if os.path.islink(lokal) or (os.path.exists(kopie) and os.path.exists(lokal)
                                 and os.path.samefile(lokal, kopie)):
        _link_aufloesen(lokal)
    stand = [os.path.getmtime(p) if os.path.exists(p) else 0 for p in (lokal, kopie)]
    alt = staende.get(name)
    if alt == stand and not erzwingen:
        return 0
    hier, dort = _roh(lokal), _roh(kopie)
    n = 0
    if dort is None and hier is None:
        pass
    elif dort is None:
        _schreiben(kopie, hier)
    elif hier is None:
        _schreiben(lokal, dort)
        n = len(dort.get("werkzeuge") or [])
    else:
        # erster Abgleich: die Datei im Ordner gilt (was nur hier ist, kommt dazu); sonst die neuere Datei
        hier_gilt = bool(alt) and stand[0] >= stand[1]
        erg = zusammen(hier, dort, hier_gilt)
        if isinstance(erg, tuple):
            erg, n = erg
        if _schluessel(erg) != _schluessel(hier):
            _sichern(lokal, sicher)
            _schreiben(lokal, erg)
        if _schluessel(erg) != _schluessel(dort):
            _sichern(kopie, sicher + "-ordner")
            _schreiben(kopie, copy.deepcopy(erg))
    staende[name] = [os.path.getmtime(p) if os.path.exists(p) else 0 for p in (lokal, kopie)]
    return n


def abgleichen(erzwingen: bool = False) -> Dict:
    """Maschinen und ihre Fräser dieses PCs mit den Kopien im Ordner abgleichen. Nichts zu tun (aus, oder seit dem
    letzten Abgleich hat sich keine Datei geändert): sofort zurück."""
    from . import maschinen
    e = einstellung()
    if not (e.get("an") and e.get("ordner")):
        return {"an": False}
    ordner = str(e["ordner"])
    if not os.path.isdir(ordner):
        e["meldung"] = "Ordner nicht erreichbar"
        return dict(e)
    staende = dict(e.get("staende") or {})
    try:
        maschinen.index()                         # hier: einmal umwandeln (werkzeuge.json → maschinen/m1.json)
        # im Ordner: alter Katalog → Maschine m1
        alt_dort = os.path.join(ordner, DATEI)
        if os.path.exists(alt_dort) and not os.path.exists(os.path.join(ordner, maschinen.INDEX)):
            ziel = os.path.join(ordner, maschinen.ORDNER, maschinen.ERSTE_ID + ".json")
            if not os.path.exists(ziel):
                roh = _roh(alt_dort) or {}
                roh.pop("vorgaben", None)
                _schreiben(ziel, roh)
            os.replace(alt_dort, os.path.join(ordner, "werkzeuge-vor-maschinen.json"))
        uebernommen = _paar(maschinen.index_pfad(), os.path.join(ordner, maschinen.INDEX), staende, maschinen.INDEX,
                            index_zusammenfuehren, "maschinen", erzwingen)
        for mid in maschinen.ids():
            rel = "%s/%s.json" % (maschinen.ORDNER, mid)
            uebernommen += _paar(maschinen.datei(mid), os.path.join(ordner, maschinen.ORDNER, mid + ".json"),
                                 staende, rel, zusammenfuehren, "werkzeuge-" + mid, erzwingen)
        _postprozessoren(ordner)
        e["staende"] = staende
        e.pop("stand", None)
        e["zuletzt"] = time.time()
        e["meldung"] = "übernommen" if uebernommen else "abgeglichen"
        e["anzahl"] = uebernommen
        e.pop("fehler", None)
    except OSError as fehler:
        e["meldung"] = "nicht möglich"
        e["fehler"] = str(fehler)
    _einstellung_schreiben(e)
    return dict(e)


def setzen(an: bool, ordner: str) -> Dict:
    e = einstellung()
    neu = bool(an) and bool(ordner)
    if str(e.get("ordner") or "") != str(ordner or ""):
        e.pop("stand", None)                  # anderer Ordner: wie ein erster Abgleich
        e.pop("staende", None)
    e["an"] = neu
    e["ordner"] = str(ordner or "")
    if not neu:
        e.pop("stand", None)
        e.pop("staende", None)
        e["meldung"] = ""
    _einstellung_schreiben(e)
    return abgleichen(erzwingen=True) if neu else dict(e)
