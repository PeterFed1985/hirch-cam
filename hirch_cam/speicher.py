# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Zwischenspeicher je Bearbeitung (2D): eine Bearbeitung wird nur neu gerechnet, wenn sich ihre
Eingaben geändert haben (Peter, 23./24.10.2026: «erst rechnen, wenn ich es sage»).

Der Rechendienst (dienst.py) läuft dauerhaft, darum genügt ein Speicher im Prozess. Je Bearbeitung
merkt sich engine.rechnen, was ihr Rechenteil (die Fräsart samt Vorbereitung in `ablegen`) erzeugt
hat – die fertigen Bahnen, Warnungen und was sie für die folgenden Bearbeitungen hinterlässt
(ausgeräumte Flächen, Schnittfugen, Gewinde). Beim nächsten Rechnen wird das abgespielt statt
gerechnet; Reihenfolge, Werkzeugeinsätze, Verbindungswege, Zeiten und G-Code entstehen immer neu.

Schlüssel: alles, was der Rechenteil liest – die Bearbeitung selbst (Art, Werte, Objekte, Seite,
Name), die Werkzeuge, die sie benutzt (auch Schlicht-, Räum-, Kernloch- und Flachwerkzeug), der
Auftrag ohne das, was erst danach zählt (Ausgabe, Nullpunkt, Postprozessor, Reihenfolge …), und die
Abhängigkeiten: was frühere Taschen und Konturen schon ausgeräumt bzw. geschnitten haben.

3D (relief_*) hat einen eigenen Speicher (ops/relief_speicher.py) und läuft hier nicht durch.

Auf dem Rechner (B-26 Weg C, Peter 05.11.): jeder Eintrag liegt zusätzlich als Datei unter
~/.cache/hirch-cam/2d-bahnen (HIRCH_2D_SPEICHER: anderer Ordner, «aus»: nie). Nach dem Neustart des Programms wird
nur neu gerechnet, was sich geändert hat. Im Schlüssel steckt der Programmstand (Quelltext der Rechenteile und die
Module) – nach einem Update wird einmal alles neu gerechnet. Eine Datei, die sich nicht lesen lässt oder nicht zum
Schlüssel passt, gilt nicht. Gespeichert werden nur die Bahnen je Bearbeitung; der G-Code entsteht immer frisch.
"""
from __future__ import annotations

import hashlib
import json
import os
import pickle
from collections import OrderedDict
from typing import Dict, List, Optional

from .model import Bohrzyklus, Move, Toolpath

STAND = 3                # bei Änderungen an der Form der Einträge erhöhen
HOECHSTENS = 400         # Einträge im Speicher (älteste fallen heraus)
DATEIEN_HOECHSTENS = 5000             # auf dem Rechner (älteste, zuletzt gebraucht, fallen heraus)
DATEIEN_BYTES = 300 * 1024 * 1024

# Was im Auftrag erst nach dem Rechnen der Bahnen zählt (oder je Bearbeitung eigens im Schlüssel steht)
_NICHT_IM_SCHLUESSEL = {"bearbeitungen", "objekte", "werkzeuge", "werkzeug", "ausgabe", "reihenfolge",
                        "ab", "luft", "entwurf", "postprozessor", "boegen", "nullpunkt", "nullpunkt_seiten",
                        "reliefs", "nur_pruefen", "_dokument", "_steg_ringe",
                        "stege", "stege_seite", "stege_bearbeitung"}
# «spannmittel» gehört seit dem tiefen Freifahren (ops/freifahren.py) dazu: über Spannmitteln bleibt die
# Freifahrhöhe. Die Haltesteg-Marken («stege» …) stehen im Zustand der Kontur, zu der sie gehören (engine.py).


def _json(x) -> str:
    return json.dumps(x, sort_keys=True, ensure_ascii=False, default=str, separators=(",", ":"))


def _sha(text: str) -> str:
    return hashlib.sha1(text.encode("utf-8")).hexdigest()


def auftrag_teil(auftrag: Dict, **dazu) -> str:
    """Prüfsumme des Auftrags ohne die Bearbeitungen (einmal je Rechnung)."""
    teile = {k: v for k, v in auftrag.items() if k not in _NICHT_IM_SCHLUESSEL}
    teile["_werkzeuge_da"] = "werkzeuge" in auftrag
    teile["_sprache"] = os.environ.get("HIRCH_SPRACHE", "")
    teile["_einheit"] = os.environ.get("HIRCH_EINHEIT", "")
    teile["_stand"] = STAND
    teile["_programm"] = programmstand()
    teile.update(dazu)
    return _sha(_json(teile))


_programm: Optional[str] = None


def programmstand() -> str:
    """Prüfsumme des Quelltexts aller Rechenteile (hirch_cam/*.py, ops/, post/) und der Module (.so/.pyd: Name,
    Grösse, Zeit). Ein anderes Paket gibt andere Schlüssel – gespeicherte Bahnen alter Stände gelten nicht."""
    global _programm
    if _programm is None:
        h = hashlib.sha256()
        paket = os.path.dirname(os.path.abspath(__file__))
        for unter in ("", "ops", "post"):
            d = os.path.join(paket, unter)
            try:
                namen = sorted(os.listdir(d))
            except OSError:
                continue
            for n in namen:
                pf = os.path.join(d, n)
                if n.endswith(".py"):
                    try:
                        with open(pf, "rb") as fh:
                            h.update(unter.encode() + b"/" + n.encode() + b"\0" + fh.read())
                    except OSError:
                        pass
                elif n.endswith((".so", ".pyd")):
                    try:
                        st = os.stat(pf)
                        h.update(f"{unter}/{n}:{st.st_size}:{int(st.st_mtime)}".encode())
                    except OSError:
                        pass
        _programm = h.hexdigest()
    return _programm


def ordner() -> Optional[str]:
    o = os.environ.get("HIRCH_2D_SPEICHER")
    if o == "aus":
        return None
    if not o:
        basis = os.environ.get("XDG_CACHE_HOME") or os.path.join(os.path.expanduser("~"), ".cache")
        o = os.path.join(basis, "hirch-cam", "2d-bahnen")
    return o


def _datei_lesen(schl: str) -> Optional["Eintrag"]:
    o = ordner()
    if not o:
        return None
    pfad = os.path.join(o, schl + ".bahn2d")
    if not os.path.exists(pfad):
        return None
    try:
        with open(pfad, "rb") as fh:
            d = pickle.load(fh)
        if not isinstance(d, dict) or d.get("stand") != STAND or d.get("schluessel") != schl:
            return None
        e = Eintrag(schl)
        for k in ("ereignisse", "p_neu", "ausgeraeumt", "konturen", "gewinde"):
            setattr(e, k, d[k])
        os.utime(pfad)                       # zuletzt gebraucht (fürs Aufräumen)
        return e
    except Exception:                        # angebrochen, andere Version …: gilt nicht, neu rechnen
        return None


def _datei_schreiben(e: "Eintrag") -> None:
    o = ordner()
    if not o:
        return
    try:
        os.makedirs(o, exist_ok=True)
        d = {"stand": STAND, "schluessel": e.schluessel,
             **{k: getattr(e, k) for k in ("ereignisse", "p_neu", "ausgeraeumt", "konturen", "gewinde")}}
        pfad = os.path.join(o, e.schluessel + ".bahn2d")
        tmp = f"{pfad}.{os.getpid()}.neu"
        with open(tmp, "wb") as fh:
            pickle.dump(d, fh, protocol=pickle.HIGHEST_PROTOCOL)
        os.replace(tmp, pfad)
        _aufraeumen(o)
    except Exception:                        # nicht schreibbar, nicht einpackbar: nur im Prozess gemerkt
        try:
            os.unlink(tmp)
        except Exception:
            pass


def _aufraeumen(o: str) -> None:
    dateien = []
    for n in os.listdir(o):
        if n.endswith(".bahn2d"):
            try:
                st = os.stat(os.path.join(o, n))
                dateien.append((st.st_mtime, st.st_size, n))
            except OSError:
                pass
    dateien.sort(reverse=True)
    summe = 0
    for i, (_t, groesse, n) in enumerate(dateien):
        summe += groesse
        if i >= DATEIEN_HOECHSTENS or summe > DATEIEN_BYTES:
            try:
                os.unlink(os.path.join(o, n))
            except OSError:
                pass


def reliefs_teil(auftrag: Dict) -> str:
    """Für «Tiefe ab Modell»: die 3D-Modelle gehören in den Schlüssel."""
    return _sha(_json(auftrag.get("reliefs") or []))


def flaechen(liste) -> List:
    """[(Fläche, Tiefe)] → vergleichbar (WKB der Fläche als Prüfsumme)."""
    aus = []
    for g, t in liste or []:
        try:
            wkb = hashlib.sha1(g.wkb).hexdigest() if g is not None else ""
        except Exception:          # keine shapely-Fläche: als Text
            wkb = _sha(str(g))
        aus.append([wkb, t])
    return aus


def schluessel(basis: str, bb: Dict, werkzeuge: Dict, zustand: Dict) -> str:
    p = bb.get("p") or {}
    slots = sorted({s for s in (bb.get("werkzeug"), bb.get("flach"), p.get("schlicht_werkzeug"),
                                p.get("raeum_werkzeug"), p.get("kernloch_werkzeug")) if s})
    return _sha(_json({"basis": basis, "bb": bb, "werkzeuge": {s: werkzeuge.get(s) for s in slots},
                       "zustand": zustand}))


def bahn_kopie(tp: Toolpath) -> Toolpath:
    """Tiefe Kopie einer Bahn (schneller als copy.deepcopy)."""
    neu = Toolpath(name=tp.name, moves=[Move(m.rapid, m.x, m.y, m.z, m.feed) for m in tp.moves],
                   senkrecht=tp.senkrecht,
                   zyklen=[Bohrzyklus(z.start, z.ende, z.zustellung, z.verweilen) for z in tp.zyklen])
    for k, v in vars(tp).items():                 # was eine Fräsart sonst noch anhängt
        if k not in ("name", "moves", "senkrecht", "zyklen"):
            setattr(neu, k, v)
    return neu


class Eintrag:
    """Was der Rechenteil einer Bearbeitung erzeugt hat, in der Reihenfolge des Entstehens.
    ereignisse: ("warnen", [Texte]) | ("opwarn", Text) | ("ablegen", slot, Bahn, anzahl, flach, teil, basis)"""
    __slots__ = ("schluessel", "ereignisse", "p_neu", "ausgeraeumt", "konturen", "gewinde")

    def __init__(self, schluessel: str):
        self.schluessel = schluessel
        self.ereignisse: List = []
        self.p_neu: Dict = {}            # Werte, die der Rechenteil in p setzt (Anzeige)
        self.ausgeraeumt: List = []      # [(Fläche, Tiefe)] für die folgenden Bearbeitungen
        self.konturen: List = []         # [(Schnittfuge, Tiefe)]
        self.gewinde: List = []

    def bahnen(self) -> List[Toolpath]:
        return [e[2] for e in self.ereignisse if e[0] == "ablegen"]


class Speicher:
    def __init__(self, hoechstens: int = HOECHSTENS):
        self.hoechstens = hoechstens
        self._eintraege: "OrderedDict[str, Eintrag]" = OrderedDict()
        self._letzter: Dict = {}         # (Dokument, Bearbeitung, Seite) → Schlüssel des letzten Eintrags

    def holen(self, schl: str) -> Optional[Eintrag]:
        e = self._eintraege.get(schl)
        if e is not None:
            self._eintraege.move_to_end(schl)
            return e
        e = _datei_lesen(schl)                # B-26: vom Rechner (nach einem Neustart)
        if e is not None:
            self._eintraege[schl] = e
            while len(self._eintraege) > self.hoechstens:
                alt, _ = self._eintraege.popitem(last=False)
                for k in [k for k, v in self._letzter.items() if v == alt]:
                    del self._letzter[k]
        return e

    def letzter(self, dok: str, bid: str, seite: int) -> Optional[Eintrag]:
        """Der zuletzt gerechnete Stand einer Bearbeitung (veraltet, für «nur prüfen»)."""
        schl = self._letzter.get((dok, bid, seite))
        return self._eintraege.get(schl) if schl else None

    def ablegen(self, dok: str, bid: str, seite: int, e: Eintrag) -> None:
        self._eintraege[e.schluessel] = e
        self._eintraege.move_to_end(e.schluessel)
        self._letzter[(dok, bid, seite)] = e.schluessel
        _datei_schreiben(e)
        while len(self._eintraege) > self.hoechstens:
            alt, _ = self._eintraege.popitem(last=False)
            for k in [k for k, v in self._letzter.items() if v == alt]:
                del self._letzter[k]

    def leeren(self) -> None:
        self._eintraege.clear()
        self._letzter.clear()

    def __len__(self) -> int:
        return len(self._eintraege)


SPEICHER = Speicher()


def aus() -> bool:
    """HIRCH_SPEICHER=0 schaltet den Speicher ab (Fehlersuche)."""
    return os.environ.get("HIRCH_SPEICHER", "1") == "0"
