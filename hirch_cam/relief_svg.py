# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""3D-Modelle aus der Zeichnung lesen (für das Fräsen).

Ein 3D-Modell steht als <svg:image hirch:relief="{…}"> in der Zeichnung (Schattenbild der
Draufsicht), die Originaldatei als <hirch:reliefdaten> in den <defs> (siehe hirch-3d.h).
Gelesen werden die Einstellungen, die Daten und die Lage des Bildes in Materialkoordinaten
(mm, Nullpunkt unten links der Seite, y nach oben) – daraus legt `ops/relief3d.py` das Modell
ins Material.
"""
from __future__ import annotations

import json
from typing import Callable, Dict, List

from .einstellungen import NS

RELIEF = "{%s}relief" % NS
DATEN = "{%s}reliefdaten" % NS
STEG3D = "{%s}steg3d" % NS            # Haltesteg eines zweiseitigen Modells (svg:rect)
SVG_IMAGE = "{http://www.w3.org/2000/svg}image"


def ist_steg3d(el) -> bool:
    return hasattr(el, "get") and el.get(STEG3D) is not None


def _zahl(el, k, d=0.0) -> float:
    try:
        return float(str(el.get(k)).replace(",", "."))
    except (TypeError, ValueError):
        return d


def _sichtbar(el) -> bool:
    e = el
    while e is not None and hasattr(e, "get"):
        stil = (e.get("style") or "").replace(" ", "")
        if "display:none" in stil or e.get("display") == "none":
            return False
        e = e.getparent()
    return True


def lesen(svg, uu: float, hoehe_uu: float, zuordnen_seite: Callable) -> List[Dict]:
    """[{id, info, ecken, seite, daten, stege}] – ecken: Materialkoordinaten der Bildecken oben
    links, oben rechts, unten links (wie das Bild in der Zeichnung liegt). zuordnen_seite: wie
    in svg_lesen.lesen (Seite und Verschiebung bei mehreren Seiten). stege: Haltestege des
    Modells [{id, ecken (4 Punkte), z (None = automatisch)}]."""
    daten: Dict[str, str] = {}
    for el in svg.iter(DATEN):
        if el.get("id"):
            daten[el.get("id")] = "".join(el.itertext())
    aus = []
    for el in svg.iter(SVG_IMAGE):
        roh = el.get(RELIEF)
        if not roh or not _sichtbar(el):
            continue
        try:
            info = json.loads(roh)
        except ValueError:
            continue
        x, y, w, h = _zahl(el, "x"), _zahl(el, "y"), _zahl(el, "width"), _zahl(el, "height")
        try:
            t = el.composed_transform()
        except Exception:
            continue

        def mm(px, py):
            q = t.apply_to_point((px, py))
            return (q[0] / uu, (hoehe_uu - q[1]) / uu)
        ecken = [mm(x, y), mm(x + w, y), mm(x, y + h), mm(x + w, y + h)]
        nr, stuecke = zuordnen_seite([(ecken, True)])
        if nr is None:
            continue
        ecken = [tuple(p) for p in stuecke[0][0]]
        aus.append({"id": el.get("id") or "", "info": info, "ecken": ecken[:3], "seite": nr,
                    "daten": daten.get(str(info.get("daten") or ""), ""), "stege": []})
    _stege_lesen(svg, uu, hoehe_uu, zuordnen_seite, aus)
    return aus


def _stege_lesen(svg, uu, hoehe_uu, zuordnen_seite, reliefs: List[Dict]) -> None:
    """Haltestege (Rechtecke mit hirch:steg3d) dem Modell zuordnen: nach der id im Attribut,
    sonst dem Modell auf derselben Seite, dessen Mitte am nächsten liegt."""
    if not reliefs:
        return
    for el in svg.iter():
        roh = el.get(STEG3D) if hasattr(el, "get") else None
        if roh is None or not _sichtbar(el):
            continue
        try:
            werte = json.loads(roh) if roh.strip() else {}
        except ValueError:
            werte = {}
        x, y, w, h = _zahl(el, "x"), _zahl(el, "y"), _zahl(el, "width"), _zahl(el, "height")
        if w <= 0 or h <= 0:
            continue
        try:
            t = el.composed_transform()
        except Exception:
            continue

        def mm(px, py):
            q = t.apply_to_point((px, py))
            return (q[0] / uu, (hoehe_uu - q[1]) / uu)
        ecken = [mm(x, y), mm(x + w, y), mm(x + w, y + h), mm(x, y + h)]
        nr, stuecke = zuordnen_seite([(ecken, True)])
        if nr is None:
            continue
        ecken = [tuple(p) for p in stuecke[0][0]]
        z = werte.get("z")
        try:
            z = None if z is None else float(z)
        except (TypeError, ValueError):
            z = None
        ziel = [r for r in reliefs if r["id"] and r["id"] == werte.get("relief")]
        if not ziel:
            cx = sum(p[0] for p in ecken) / 4
            cy = sum(p[1] for p in ecken) / 4
            gleich = [r for r in reliefs if r["seite"] == nr] or reliefs

            def abstand(r):
                e = r["ecken"]
                mx = (e[1][0] + e[2][0]) / 2
                my = (e[1][1] + e[2][1]) / 2
                return (mx - cx) ** 2 + (my - cy) ** 2
            ziel = [min(gleich, key=abstand)]
        ziel[0]["stege"].append({"id": el.get("id") or "", "ecken": ecken, "z": z, "seite": nr})
