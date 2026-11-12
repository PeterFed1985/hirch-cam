# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Postprozessor aus einer Vorlage (JSON) – so kann jeder eigene anlegen.

Eine Vorlage beschreibt nur, was vor, zwischen und nach den Fräsbahnen
steht; die Fräsbahnen selbst schreibt Hirch CAM immer gleich (G0/G1, auf
Wunsch G2/G3, Koordinaten mit 3 Stellen, X/Y/Z nur wenn sie sich ändern).

    {
      "name": "WinCNC (mm, .tap)",
      "endung": ".tap",
      "werkzeugwechsel": true,      true: alle Werkzeuge in einer Datei, dazwischen «wechsel»
                                    false: eine Datei je Werkzeug
      "wechselposition": true,      Zeilen mit {wx}/{wy}/{wz} in «wechsel» ausgeben?
      "wechsel_x": 0, "wechsel_y": 0, "wechsel_z": null (= Sicherheitshöhe),
      "leerzeichen": false,         «G0 X1.000 Y2.000» statt «G0X1.000Y2.000»
      "kurzname": 8,                Dateinamen höchstens 8 Zeichen vor der Endung, nur A–Z 0–9 _
                                    (0 oder fehlt: Name wie gewählt, z. B. Schild_1_D3.tap)
      "grossbuchstaben": true,      Dateiname und Endung in GROSSBUCHSTABEN (SCHILD_1.TAP)
      "kopf":     [Zeilen am Dateianfang],
      "werkzeug": [Zeilen, bevor ein Werkzeug fräst],
      "wechsel":  [Zeilen zwischen zwei Werkzeugen (nur bei werkzeugwechsel)],
      "ende":     [Zeilen am Dateiende],

    Was die Steuerung kann (fehlt ein Eintrag, nimmt Hirch CAM das Vorsichtige):
      "boegen": true,               G2/G3 erlaubt (false: Rundungen immer als Geraden)
      "bohrzyklus": {               Bohrzyklen; fehlt der Eintrag, werden Bohrungen
        "entspanen": ["G98 G83 X{x} Y{y} Z{z} R{r} Q{q} F{f}"],   als Bewegungen geschrieben
        "einfach":   ["G98 G81 X{x} Y{y} Z{z} R{r} F{f}"],
        "verweilen": ["G98 G82 X{x} Y{y} Z{z} R{r} P{p} F{f}"],
        "ende":      ["G80"]
      },
      "kommentar": "({text})",      so steht ein Kommentar in der Datei (leer: keine)
      "zeilennummern": 0            > 0: N-Nummern in diesem Abstand (N10, N20, …)
    }

Platzhalter: {t} Werkzeugnummer, {s} Drehzahl, {werkzeug} Name, {d} Ø,
{sicher} Sicherheitshöhe (englisch zusätzlich: {tool} {safe} {file} {date}), {hx} {hy} Startpunkt, {wx} {wy} {wz}
Wechselposition, {datei} Dateiname, {datum} Datum. Bei «wechsel» steht {t}
für das nächste Werkzeug. Im Bohrzyklus: {x} {y} Lage, {z} tiefste Stelle,
{r} Rückzugsebene, {q} Zustellung je Hub, {p} Verweilzeit in s, {f} Vorschub,
{oben} Höhe davor und danach. Im Kommentar: {text}.

Eine Anleitung steht in docs/POSTPROZESSOR.md.
"""
from __future__ import annotations

import datetime
import re
from typing import Dict, List

from ..model import Job
from .tap_mm import NL, _c, koerper

FELDER = ("kopf", "werkzeug", "wechsel", "ende")
_LEER = re.compile(r"(?<=[0-9.])(?=[A-Z])")


_ASCII = {"ä": "ae", "ö": "oe", "ü": "ue", "Ä": "Ae", "Ö": "Oe", "Ü": "Ue", "ß": "ss", "Ø": "D", "ø": "d",
          "°": "Grad", "–": "-", "—": "-", "×": "x", "·": "-"}


def ascii_text(t: str) -> str:
    """Für ältere Steuerungen: nur ASCII in Kommentaren (Werkzeug- und Dateinamen)."""
    import unicodedata
    t = "".join(_ASCII.get(c, c) for c in str(t))
    return unicodedata.normalize("NFKD", t).encode("ascii", "ignore").decode()


def _ohne_klammern(t: str) -> str:
    """Namen stehen oft in Kommentaren – ( ) [ ] darin würden ihn vorzeitig beenden."""
    return t.replace("(", "").replace(")", "").replace("[", "").replace("]", "")


class Vorlage:
    def __init__(self, schluessel: str, daten: Dict):
        self.schluessel = schluessel
        self.d = daten
        self.name = daten.get("name") or schluessel
        self.endung = daten.get("endung") or ".tap"
        if not self.endung.startswith("."):
            self.endung = "." + self.endung
        self.eine_datei = bool(daten.get("werkzeugwechsel", True))
        # Steuerungen mit kurzen Dateinamen: höchstens so viele Zeichen vor der Endung, nur A–Z 0–9 _
        self.kurzname = int(daten.get("kurzname") or 0)
        self.grossbuchstaben = bool(daten.get("grossbuchstaben", False))
        if self.grossbuchstaben:
            self.endung = self.endung.upper()

    # -- Platzhalter ---------------------------------------------------------
    def _werte(self, job: Job, datei: str, werkzeug: Dict) -> Dict[str, str]:
        d = self.d
        wz = d.get("wechsel_z")
        werte = {
            "t": str(job.tool_number), "s": str(int(job.spindle_rpm)),
            "werkzeug": _ohne_klammern(ascii_text(werkzeug.get("name", ""))),
            "d": "%g" % float(werkzeug.get("durchmesser", 0) or 0),
            "sicher": _c(job.safe_z), "hx": _c(job.home[0]), "hy": _c(job.home[1]),
            "wx": _c(float(d.get("wechsel_x") or 0)), "wy": _c(float(d.get("wechsel_y") or 0)),
            "wz": _c(job.safe_z if wz is None else float(wz)),
            "datei": _ohne_klammern(ascii_text(datei)), "datum": datetime.date.today().strftime("%d.%m.%Y"),
        }
        # englische Namen zusätzlich (planung 06.11.: ein englischer Nutzer versteht {werkzeug} nicht)
        for de, en in (("werkzeug", "tool"), ("sicher", "safe"), ("datei", "file"), ("datum", "date")):
            werte[en] = werte[de]
        return werte

    def _zeilen(self, feld: str, werte: Dict[str, str]) -> List[str]:
        aus = []
        for z in self.d.get(feld) or []:
            if feld == "wechsel" and not self.d.get("wechselposition", True) and re.search(r"\{w[xyz]\}", z):
                continue
            try:
                aus.append(z.format(**werte))
            except (KeyError, IndexError, ValueError):
                aus.append(z)          # unbekannter Platzhalter: Zeile wie geschrieben
        return aus

    @property
    def kann_boegen(self) -> bool:
        return bool(self.d.get("boegen", True))

    @property
    def kann_bohrzyklen(self) -> bool:
        z = self.d.get("bohrzyklus") or {}
        return isinstance(z, dict) and bool(z.get("entspanen") or z.get("einfach"))

    def kommentar(self, text: str) -> List[str]:
        muster = self.d.get("kommentar") or ""
        if not muster or not text:
            return []
        # Klammern im Text würden den Kommentar beenden
        sauber = _ohne_klammern(ascii_text(text))
        try:
            return [muster.format(text=sauber)]
        except (KeyError, IndexError, ValueError):
            return []

    def _bahnen(self, job: Job, boegen: bool) -> List[str]:
        anf: List[int] = []
        zeilen = koerper(job, anfaenge=anf, boegen=boegen and self.kann_boegen,
                         zyklen=self.d.get("bohrzyklus") if self.kann_bohrzyklen else None)
        if self.d.get("kommentar"):
            # vor jedem Werkzeugweg sein Name (Bearbeitung)
            for i in reversed(range(len(anf))):
                zeilen[anf[i]:anf[i]] = self.kommentar(job.toolpaths[i].name)
        if self.d.get("leerzeichen"):
            zeilen = [_LEER.sub(" ", z) if not self._ist_kommentar(z) else z for z in zeilen]
        return zeilen

    def _ist_kommentar(self, z: str) -> bool:
        m = self.d.get("kommentar") or ""
        return bool(m) and z.startswith(m.split("{")[0])

    def _nummerieren(self, out: List[str]) -> List[str]:
        schritt = int(self.d.get("zeilennummern") or 0)
        if schritt <= 0:
            return out
        aus, n = [], 0
        for z in out:
            if not z.strip() or self._ist_kommentar(z) or z.strip() == "%" or re.match(r"^O\d", z.strip()):
                aus.append(z)
                continue
            n += schritt
            aus.append(f"N{n}{' ' if self.d.get('leerzeichen') else ''}{z}")
        return aus

    # -- Schreiben -----------------------------------------------------------
    def write_mehrere(self, jobs: List[Job], boegen: bool = False, datei: str = "",
                      werkzeuge: List[Dict] | None = None, grenzen: List[int] | None = None) -> str:
        """grenzen: wird je Werkzeug mit der Zeile (ab 0) gefüllt, ab der sein Teil beginnt (Endprüfung: welcher Fräser
        welche Zeilen fräst)."""
        werkzeuge = werkzeuge or [{} for _ in jobs]
        out: List[str] = []
        for i, job in enumerate(jobs):
            if grenzen is not None:
                grenzen.append(sum(1 + z.count("\n") for z in out))
            w = self._werte(job, datei, werkzeuge[i])
            if i == 0:
                out += self._zeilen("kopf", w)
            else:
                # Wechsel: Sicherheitshöhe des vorigen Werkzeugs, Nummer des nächsten
                w_vor = dict(w, sicher=_c(jobs[i - 1].safe_z))
                out += self._zeilen("wechsel", w_vor)
            out += self._zeilen("werkzeug", w)
            out += self._bahnen(job, boegen)
        out += self._zeilen("ende", self._werte(jobs[-1], datei, werkzeuge[-1]))
        return NL.join(self._nummerieren(out)) + NL

    def write(self, job: Job, boegen: bool = False, datei: str = "", werkzeug: Dict | None = None) -> str:
        return self.write_mehrere([job], boegen, datei, [werkzeug or {}])
