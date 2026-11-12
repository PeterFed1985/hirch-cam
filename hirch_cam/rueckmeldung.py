# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Rückmeldung an den Annahme-Dienst senden (Fenster «Hilfe › Rückmeldung senden …»).

Nur Python-Standardbibliothek. Gesendet wird nur auf Knopfdruck; ohne Verbindung kommt eine klare Meldung
(RueckmeldungFehler), und das Fenster behält die Eingaben.

Zum Testen auf dem iMac:
    python3 -m hirch_cam.rueckmeldung --adresse http://localhost:8088 --art fehler --text "Probe"

Für das Fenster im Programm (fork/src/ui/dialog/hirch-rueckmeldung.cpp):
    python3 -m hirch_cam.rueckmeldung --info     → {"adresse", "system", "datenschutz_de", "datenschutz_en"}
    python3 -m hirch_cam.rueckmeldung --json     liest {"art","text","email","bild","zeichnung","version","sprache"}
                                                 von stdin → {"nummer"} oder {"fehler", "text"}

Ablauf: Aufgabe holen (/api/v1/aufgabe) → Zahl suchen, bis sha256("<aufgabe>:<zahl>") mit `schwierigkeit`
Null-Bits beginnt (Schutz gegen Roboter, etwa 1 s) → Meldung als multipart/form-data senden (/api/v1/meldung).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import ssl
import sys
import time
import urllib.error
import urllib.request
import uuid

# Adresse des Dienstes (Peter 08.11.2026). Zum Testen anders: HIRCH_FEEDBACK_ADRESSE.
STANDARD_ADRESSE = "https://feedback.cam.hirch.net"
# Datenschutzerklärung auf der Website (Domain cam.hirch.net, Peter 08.11.2026). Leer: kein Link.
DATENSCHUTZ = {"de": "https://cam.hirch.net/de/datenschutz/", "en": "https://cam.hirch.net/privacy/"}
ZEITLIMIT = 30
MAX_SCHWIERIGKEIT = 26  # mehr verlangt ein echter Dienst nie – schützt vor endlosem Rechnen


class RueckmeldungFehler(Exception):
    """grund: keine_adresse | keine_verbindung | nicht_erreichbar | zu_viele | zu_gross | ungueltig | aufgabe |
    abgelehnt | server"""

    def __init__(self, grund: str, text: str):
        super().__init__(text)
        self.grund = grund
        self.text = text


def adresse() -> str:
    return (os.environ.get("HIRCH_FEEDBACK_ADRESSE") or STANDARD_ADRESSE).rstrip("/")


def betriebssystem() -> str:
    """Kurz und sichtbar im Fenster, z. B. «Linux Mint 22.3» oder «Windows 11»."""
    if sys.platform.startswith("linux"):
        try:
            with open("/etc/os-release", encoding="utf-8") as f:
                werte = dict(z.rstrip("\n").split("=", 1) for z in f if "=" in z)
            name = werte.get("NAME", "Linux").strip('"')
            ver = werte.get("VERSION_ID", "").strip('"')
            return f"{name} {ver}".strip()[:120]
        except OSError:
            return "Linux"
    if sys.platform == "win32":
        ver = platform.version()
        try:
            haupt = "11" if int(ver.split(".")[2]) >= 22000 else platform.release()
        except (IndexError, ValueError):
            haupt = platform.release()
        return f"Windows {haupt}"
    if sys.platform == "darwin":
        return f"macOS {platform.mac_ver()[0]}".strip()
    return platform.system()[:120]


def _null_bits(d: bytes) -> int:
    n = 0
    for b in d:
        if b == 0:
            n += 8
            continue
        return n + (8 - b.bit_length())
    return n


def aufgabe_loesen(aufgabe: str, schwierigkeit: int) -> str:
    if not 0 <= schwierigkeit <= MAX_SCHWIERIGKEIT:
        raise RueckmeldungFehler("server", "Der Server verlangt eine ungültige Prüfung.")
    zahl = 0
    praefix = f"{aufgabe}:".encode()
    while True:
        if _null_bits(hashlib.sha256(praefix + str(zahl).encode()).digest()) >= schwierigkeit:
            return str(zahl)
        zahl += 1


def _anfrage(url: str, daten: bytes | None = None, kopf: dict | None = None) -> dict:
    req = urllib.request.Request(url, data=daten, headers={"User-Agent": "Hirch-CAM", **(kopf or {})})
    try:
        with urllib.request.urlopen(req, timeout=ZEITLIMIT, context=ssl.create_default_context()) as r:
            return json.loads(r.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        try:
            antwort = json.loads(e.read().decode("utf-8"))
        except (ValueError, OSError):
            antwort = {}
        if e.code in (502, 503, 504) and not antwort.get("fehler"):
            raise RueckmeldungFehler(
                "nicht_erreichbar",
                "Nicht gesendet – der Server ist gerade nicht erreichbar. Deine Eingaben bleiben stehen. "
                "Bitte später nochmals senden.") from e
        grund = antwort.get("fehler") or ("zu_viele" if e.code == 429 else "zu_gross" if e.code == 413 else "server")
        raise RueckmeldungFehler(grund, antwort.get("text") or f"Der Server antwortet mit Fehler {e.code}.") from e
    except (urllib.error.URLError, OSError, TimeoutError) as e:
        raise RueckmeldungFehler(
            "keine_verbindung",
            "Nicht gesendet – keine Verbindung zum Server. Deine Eingaben bleiben stehen. "
            "Prüfe die Internetverbindung und drücke nochmals «Senden».") from e
    except ValueError as e:
        raise RueckmeldungFehler("server", "Der Server hat unerwartet geantwortet.") from e


def _multipart(felder: dict, dateien: dict) -> tuple[bytes, str]:
    grenze = uuid.uuid4().hex
    teile = []
    for k, v in felder.items():
        teile.append(f'--{grenze}\r\nContent-Disposition: form-data; name="{k}"\r\n\r\n'.encode() +
                     str(v).encode("utf-8") + b"\r\n")
    for k, (name, inhalt, typ) in dateien.items():
        teile.append(f'--{grenze}\r\nContent-Disposition: form-data; name="{k}"; filename="{name}"\r\n'
                     f"Content-Type: {typ}\r\n\r\n".encode() + inhalt + b"\r\n")
    teile.append(f"--{grenze}--\r\n".encode())
    return b"".join(teile), f"multipart/form-data; boundary={grenze}"


def senden(art: str, text: str, *, email: str = "", bild: str | None = None, zeichnung: str | None = None,
           version: str = "", system: str | None = None, sprache: str = "de", ziel: str | None = None,
           website: bool = False) -> str:
    """Sendet die Meldung und gibt die Nummer zurück (z. B. «R-0012»). Fehler → RueckmeldungFehler.

    bild: Pfad zu PNG/JPG (Bildschirmfoto oder eigenes Bild) oder None. zeichnung: Pfad zur .hir oder None –
    nur wenn der Nutzer das Häkchen gesetzt hat. website: Einwilligung «Darf ohne Namen auf der Website erscheinen»."""
    basis = (ziel or adresse()).rstrip("/")
    if not basis:
        raise RueckmeldungFehler("keine_adresse", "Für Rückmeldungen ist noch keine Adresse eingerichtet.")
    dateien = {}
    if bild:
        with open(bild, "rb") as f:
            inhalt = f.read()
        typ = "image/png" if inhalt.startswith(b"\x89PNG") else "image/jpeg"
        dateien["bild"] = ("bild.png" if typ == "image/png" else "bild.jpg", inhalt, typ)
    if zeichnung:
        with open(zeichnung, "rb") as f:
            dateien["zeichnung"] = ("zeichnung.hir", f.read(), "application/octet-stream")

    a = _anfrage(f"{basis}/api/v1/aufgabe")
    loesung = aufgabe_loesen(str(a.get("aufgabe", "")), int(a.get("schwierigkeit", 0)))
    felder = {"art": art, "text": text, "email": email, "version": version,
              "system": betriebssystem() if system is None else system, "sprache": sprache,
              "aufgabe": a.get("aufgabe", ""), "loesung": loesung, "website": "ja" if website else ""}
    daten, typ = _multipart(felder, dateien)
    antwort = _anfrage(f"{basis}/api/v1/meldung", daten, {"Content-Type": typ})
    nummer = antwort.get("nummer")
    if not nummer:
        raise RueckmeldungFehler("server", "Der Server hat keine Nummer zurückgegeben.")
    return nummer


def _ausgabe(d: dict) -> None:
    sys.stdout.write(json.dumps(d, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Rückmeldung senden (Test).")
    ap.add_argument("--adresse", default=None)
    ap.add_argument("--info", action="store_true", help="Angaben fürs Fenster als JSON")
    ap.add_argument("--json", action="store_true", help="Meldung als JSON von stdin, Antwort als JSON")
    ap.add_argument("--art", default="fehler", choices=["fehler", "wunsch", "frage"])
    ap.add_argument("--text")
    ap.add_argument("--email", default="")
    ap.add_argument("--bild")
    ap.add_argument("--zeichnung")
    ap.add_argument("--version", default="test")
    ap.add_argument("--website", action="store_true", help="Einwilligung: darf ohne Namen auf der Website erscheinen")
    ns = ap.parse_args(argv)
    if ns.info:
        _ausgabe({"adresse": ns.adresse or adresse(), "system": betriebssystem(),
                  "datenschutz_de": DATENSCHUTZ["de"], "datenschutz_en": DATENSCHUTZ["en"]})
        return 0
    if ns.json:
        try:
            d = json.loads(sys.stdin.read() or "{}")
            nummer = senden(str(d.get("art", "")), str(d.get("text", "")), email=str(d.get("email") or ""),
                            bild=d.get("bild") or None, zeichnung=d.get("zeichnung") or None,
                            version=str(d.get("version", "")), sprache=str(d.get("sprache", "de")),
                            ziel=ns.adresse, website=d.get("website") is True)
        except RueckmeldungFehler as e:
            _ausgabe({"fehler": e.grund, "text": e.text})
            return 0
        except (OSError, ValueError) as e:
            _ausgabe({"fehler": "datei", "text": f"Datei nicht lesbar: {e}"})
            return 0
        _ausgabe({"nummer": nummer})
        return 0
    if not ns.text:
        ap.error("--text fehlt")
    t = time.time()
    try:
        nummer = senden(ns.art, ns.text, email=ns.email, bild=ns.bild, zeichnung=ns.zeichnung,
                        version=ns.version, ziel=ns.adresse, website=ns.website)
    except RueckmeldungFehler as e:
        print(f"Fehler ({e.grund}): {e.text}", file=sys.stderr)
        return 1
    print(f"Angekommen: {nummer} ({time.time() - t:.1f} s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
