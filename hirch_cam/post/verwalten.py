# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Postprozessoren verwalten (für den Dialog «Fräsen → Postprozessoren …»).

    python3 -m hirch_cam.dokument post liste
    python3 -m hirch_cam.dokument post speichern '{"schluessel": "…", "name": …}'
    python3 -m hirch_cam.dokument post entfernen SCHLÜSSEL
    python3 -m hirch_cam.dokument post zuruecksetzen SCHLÜSSEL
    python3 -m hirch_cam.dokument post beispiel '{…Vorlage…}'

Antwort immer JSON auf stdout: {"ok", "postprozessoren": [...], …}.
"""
from __future__ import annotations

import json
import os
import re
import sys
import unicodedata

from . import alle, benutzer_ordner
from .vorlage import FELDER, Vorlage

WERTE = ("name", "beschreibung", "endung", "werkzeugwechsel", "wechselposition", "wechsel_x", "wechsel_y",
         "wechsel_z", "leerzeichen", "boegen", "bohrzyklus", "kommentar", "zeilennummern", "kurzname",
         "grossbuchstaben") + FELDER


def _schluessel(name: str, vorhanden) -> str:
    s = unicodedata.normalize("NFKD", name).encode("ascii", "ignore").decode().lower()
    s = re.sub(r"[^a-z0-9]+", "_", s).strip("_") or "postprozessor"
    k, n = s, 2
    while k in vorhanden:
        k = "%s_%d" % (s, n)
        n += 1
    return k


def _schreiben(schluessel: str, daten) -> None:
    o = benutzer_ordner()
    os.makedirs(o, exist_ok=True)
    tmp = os.path.join(o, schluessel + ".json.neu")
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(daten, fh, ensure_ascii=False, indent=2)
    os.replace(tmp, os.path.join(o, schluessel + ".json"))


def speichern(daten) -> str:
    a = alle()
    k = (daten.get("schluessel") or "").strip()
    if not k:
        k = _schluessel(daten.get("name") or "Postprozessor", a)
    if not re.fullmatch(r"[A-Za-z0-9_\-]+", k):
        raise ValueError("Ungültiger Schlüssel: %r" % k)
    # Was der Dialog nicht kennt (z. B. Bohrzyklen), bleibt von der bisherigen Vorlage
    bisher = {f: v for f, v in (a.get(k) or {}).items() if f in WERTE}
    neu = dict(bisher, **{f: daten[f] for f in WERTE if f in daten})
    for f in FELDER:
        if isinstance(neu.get(f), str):
            neu[f] = [z for z in neu[f].splitlines()]
    _schreiben(k, neu)
    return k


def entfernen(k: str) -> None:
    a = alle()
    if k not in a:
        return
    pfad = os.path.join(benutzer_ordner(), k + ".json")
    if a[k].get("mitgeliefert"):
        _schreiben(k, {"entfernt": True})       # mitgeliefert: nur ausblenden
    elif os.path.exists(pfad):
        os.unlink(pfad)


def zuruecksetzen(k: str) -> None:
    pfad = os.path.join(benutzer_ordner(), k + ".json")
    if k in alle() and alle()[k].get("mitgeliefert") and os.path.exists(pfad):
        os.unlink(pfad)


def beispiel(daten) -> str:
    """Kleines Beispielprogramm mit zwei Werkzeugen (gekürzt)."""
    from ..model import Bohrzyklus, Job, Move, Toolpath
    d = dict(daten)
    for f in FELDER:
        if isinstance(d.get(f), str):
            d[f] = d[f].splitlines()
    v = Vorlage(d.get("schluessel") or "beispiel", d)
    jobs = []
    for nr, (s, dm) in enumerate(((18000, 3.0), (16000, 6.0)), 1):
        tp = Toolpath(moves=[Move(True, 10, 10, 3), Move(True, 10, 10, 0.5), Move(False, 10, 10, -1, 300),
                             Move(False, 40, 10, -1, 1000), Move(False, 40, 30, -1, 1000), Move(True, 40, 30, 3)])
        tp.name = "Kontur" if nr == 1 else "Tasche"
        tps = [tp]
        if nr == 1:                         # dazu eine Bohrung (zeigt den Bohrzyklus)
            tps.append(Toolpath(name="Bohren", moves=[
                Move(True, 25, 20, 3), Move(True, 25, 20, 0.5), Move(False, 25, 20, -2, 300),
                Move(True, 25, 20, 0.5), Move(True, 25, 20, -1.8), Move(False, 25, 20, -4, 300),
                Move(True, 25, 20, 3)], zyklen=[Bohrzyklus(1, 7, 2.0)]))
        jobs.append(Job(tool_number=nr, spindle_rpm=s, safe_z=20.0, toolpaths=tps))
    infos = [{"name": "Schaftfräser Ø 3 mm", "durchmesser": 3.0}, {"name": "Schaftfräser Ø 6 mm", "durchmesser": 6.0}]
    if v.eine_datei:
        return v.write_mehrere(jobs, datei="beispiel" + v.endung, werkzeuge=infos).replace("\r\n", "\n")
    teile = []
    for i, job in enumerate(jobs):
        name = "beispiel_%d%s" % (i + 1, v.endung)
        teile.append("――― Datei %s ―――\n" % name + v.write(job, datei=name, werkzeug=infos[i]).replace("\r\n", "\n"))
    return "\n".join(teile)


DATEI_ART = "hirch-postprozessor"
_ENDUNG = re.compile(r"\.?[A-Za-z0-9]{1,8}")


def exportieren(k: str, ziel: str) -> str:
    """Einen Postprozessor in eine eigene Datei (zum Weitergeben, Peter 06.11.). Die Kennzeichnung «an der Maschine
    geprüft» kommt nicht mit – sie gilt nur für die mitgelieferte, unveränderte Vorlage."""
    a = alle()
    if k not in a:
        raise ValueError("Unbekannter Postprozessor: %r" % k)
    d = a[k]
    inhalt = {"art": DATEI_ART, "version": 1, "schluessel": k}
    inhalt.update({f: d[f] for f in WERTE if f in d})
    tmp = ziel + ".neu"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(inhalt, fh, ensure_ascii=False, indent=2)
    os.replace(tmp, ziel)
    return ziel


def _pruefen(roh) -> dict:
    """Nur bekannte Felder mit dem richtigen Typ – eine fremde Datei darf nichts anderes ins Programm bringen."""
    if not isinstance(roh, dict):
        raise ValueError("keine Postprozessor-Datei von Hirch CAM")
    # eigene Exportdatei, oder eine Vorlage wie im Programmordner (ohne «art», mit Zeilen für Kopf usw.)
    vorlage = "art" not in roh and any(isinstance(roh.get(f), list) for f in FELDER)
    if roh.get("art") != DATEI_ART and not vorlage:
        raise ValueError("keine Postprozessor-Datei von Hirch CAM")

    def text(v, n=2000):
        if not isinstance(v, str) or len(v) > n:
            raise ValueError("ungültiger Eintrag")
        return v

    def zeilen(v):
        if isinstance(v, str):
            v = v.splitlines()
        if not isinstance(v, list) or len(v) > 60:
            raise ValueError("ungültige Zeilen")
        return [text(z, 200) for z in v]

    d = {}
    for f, v in roh.items():
        if f not in WERTE:
            continue                                   # z. B. «an_maschine_geprueft»: kommt nie mit
        if f in FELDER:
            d[f] = zeilen(v)
        elif f in ("werkzeugwechsel", "wechselposition", "leerzeichen", "boegen", "grossbuchstaben"):
            d[f] = bool(v)
        elif f in ("wechsel_x", "wechsel_y", "wechsel_z"):
            d[f] = None if v is None and f == "wechsel_z" else float(v or 0)
        elif f in ("zeilennummern", "kurzname"):
            d[f] = max(0, min(100, int(v or 0)))
        elif f == "bohrzyklus":
            if v:
                if not isinstance(v, dict):
                    raise ValueError("ungültiger Bohrzyklus")
                d[f] = {k: zeilen(z) for k, z in v.items() if k in ("entspanen", "einfach", "verweilen", "ende")}
        elif f == "endung":
            if not _ENDUNG.fullmatch(text(v, 10)):
                raise ValueError("ungültige Dateiendung: %r" % v)
            d[f] = v
        else:
            d[f] = text(v, 80 if f == "name" else 2000)
    if not (d.get("name") or "").strip():
        raise ValueError("Der Postprozessor hat keinen Namen")
    return d


def importieren(quelle: str) -> str:
    """Postprozessor aus einer Datei als eigenen anlegen – nie einen vorhandenen überschreiben (gleicher Name:
    «… (importiert)»). Gilt immer als ungeprüft."""
    if os.path.getsize(quelle) > 200_000:
        raise ValueError("Die Datei ist zu gross für einen Postprozessor")
    with open(quelle, encoding="utf-8") as fh:
        d = _pruefen(json.load(fh))
    a = alle()
    namen = {str(v.get("name") or "").strip().lower() for v in a.values()}
    while d["name"].strip().lower() in namen:
        d["name"] = d["name"].strip() + " (importiert)"
    k = _schluessel(d["name"], a)
    _schreiben(k, d)
    return k


def _liste():
    aus = []
    for k, d in alle().items():
        e = {f: d.get(f) for f in WERTE}
        e.update(schluessel=k, mitgeliefert=bool(d.get("mitgeliefert")), geaendert=bool(d.get("geaendert")),
                 entfernt=bool(d.get("entfernt")))
        aus.append(e)
    return aus


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    befehl = argv[0] if argv else "liste"
    erg = {"ok": True}
    try:
        if befehl == "speichern":
            erg["schluessel"] = speichern(json.loads(argv[1]))
        elif befehl == "entfernen":
            entfernen(argv[1])
        elif befehl == "zuruecksetzen":
            zuruecksetzen(argv[1])
        elif befehl == "beispiel":
            erg["text"] = beispiel(json.loads(argv[1]))
        elif befehl == "exportieren":
            erg["datei"] = exportieren(argv[1], argv[2])
        elif befehl == "importieren":
            erg["schluessel"] = importieren(argv[1])
    except Exception as ex:          # lesbar an den Dialog
        erg = {"ok": False, "fehler": str(ex)}
    erg["postprozessoren"] = _liste()
    sys.stdout.write(json.dumps(erg, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
