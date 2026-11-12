# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Rechenkern als Dienst: bleibt laufen, damit nicht jede Rechnung Python neu starten und alle
Module (shapely, inkex, CAM-Modul …) neu laden muss (Peter, 20.10.: «Berechnen dauert ein paar
Sekunden»). Das Panel schickt je Auftrag eine Zeile JSON auf stdin:

    {"args": ["rechnen", "zeichnung.svg", …], "cwd": "/tmp/hirch-cam-…", "env": {"HIRCH_SPRACHE": "de"}}

und bekommt eine Zeile JSON zurück: {"rc": 0, "out": "<Ausgabe von dokument>", "err": "…"}.
Die Antwort geht über einen eigenen Kanal (Kopie von stdout); was sonst auf stdout landet
(Hilfsprozesse, print), geht nach stderr und stört die Antworten nicht.
"""
from __future__ import annotations

import io
import json
import os
import sys
import traceback


def _vorladen():
    """Was jede Rechnung braucht, schon beim Start laden."""
    try:
        from . import dokument, engine  # noqa: F401
        from .ops import bohren, gravur, kontur, tasche, vcarve  # noqa: F401
        import shapely.geometry  # noqa: F401
    except Exception:          # fehlt etwas, merkt es die erste Rechnung und meldet es lesbar
        pass


def bearbeiten(anfrage: dict) -> dict:
    from . import dokument
    cwd = anfrage.get("cwd")
    if cwd and os.path.isdir(cwd):
        os.chdir(cwd)
    for k, v in (anfrage.get("env") or {}).items():
        os.environ[str(k)] = str(v)
    out, err = io.StringIO(), io.StringIO()
    alt = sys.stdout, sys.stderr
    sys.stdout, sys.stderr = out, err
    rc = 0
    try:
        rc = dokument.main(list(anfrage.get("args") or []))
    except SystemExit as e:
        rc = e.code if isinstance(e.code, int) else (0 if e.code is None else 1)
    except BaseException:
        err.write(traceback.format_exc())
        rc = 1
    finally:
        sys.stdout, sys.stderr = alt
    return {"rc": int(rc or 0), "out": out.getvalue(), "err": err.getvalue()}


def main() -> int:
    kanal = os.fdopen(os.dup(1), "w", encoding="utf-8", newline="\n")
    os.dup2(2, 1)                      # alles andere auf stdout → stderr
    sys.stdout = os.fdopen(1, "w", encoding="utf-8", closefd=False)
    _vorladen()
    kanal.write(json.dumps({"bereit": True}) + "\n")
    kanal.flush()
    for zeile in sys.stdin:
        zeile = zeile.strip()
        if not zeile:
            continue
        try:
            antwort = bearbeiten(json.loads(zeile))
        except Exception:
            antwort = {"rc": 1, "out": "", "err": traceback.format_exc()}
        kanal.write(json.dumps(antwort, ensure_ascii=False) + "\n")
        kanal.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
