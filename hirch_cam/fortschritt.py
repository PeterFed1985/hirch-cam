# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Fortschritt einer Rechnung für die Oberfläche (Rechenanzeige im Panel, Reiter «3D», Leiste).

Das Panel ruft «rechnen --fortschritt DATEI» auf und liest die Datei alle 250 ms. Hier steht
jeweils der aktuelle Schritt als JSON: {"text": …, "nr": n, "von": m} (nr/von nur bei den
Bearbeitungen). Ohne Datei tut melden() nichts.
"""
from __future__ import annotations

import json
import os
from typing import Optional

_datei: Optional[str] = None


def setzen(pfad: Optional[str]):
    global _datei
    _datei = pfad or None


def melden(text: str, nr: Optional[int] = None, von: Optional[int] = None):
    if not _datei:
        return
    daten = {"text": text}
    if nr is not None and von is not None:
        daten.update(nr=int(nr), von=int(von))
    try:
        tmp = _datei + ".neu"
        with open(tmp, "w", encoding="utf-8") as fh:
            json.dump(daten, fh, ensure_ascii=False)
        os.replace(tmp, _datei)
    except OSError:
        pass
