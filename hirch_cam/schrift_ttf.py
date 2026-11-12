# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Einlinienschriften als TrueType-Schrift fürs Textwerkzeug.

TrueType kennt nur geschlossene Umrisse. Jeder Strich wird darum hin und
zurück gefahren (Fläche null): mit Kontur statt Füllung gezeichnet sieht man
genau die Linie. Der Rechenkern führt solche Umrisse beim Fräsen wieder auf
einfache Linien zurück (svg_lesen, hirch:text).

Die Schriften liegen im Schriftordner des Hirch-CAM-Profils
($INKSCAPE_PROFILE_DIR/fonts), den Hirch CAM beim Start einliest.
"""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import sys
import unicodedata
from typing import Dict, List, Optional

EINHEITEN = 1000          # units per em
VERSAL = 1000             # Versalhöhe = Schriftgrösse (10 mm Schrift = 10 mm hohe Grossbuchstaben)
STAND = "4"               # 4: optischer Abstand (21.10.)               # bei Änderungen am Aufbau erhöhen → Schriften neu bauen


def ordner() -> str:
    o = os.environ.get("HIRCH_SCHRIFT_TTF")
    if o:
        return o
    profil = os.environ.get("INKSCAPE_PROFILE_DIR")
    if not profil:
        from .orte import konfig
        profil = os.path.join(konfig(), "profil")
    return os.path.join(profil, "fonts")


def _dateiname(name: str) -> str:
    s = unicodedata.normalize("NFKD", name).encode("ascii", "ignore").decode()
    s = "".join(c if c.isalnum() else "-" for c in s).strip("-").lower() or "schrift"
    return "hirch-" + s + ".ttf"


def _kennung(schrift: Dict) -> str:
    roh = json.dumps([STAND, schrift.get("name"), schrift.get("cap"), schrift.get("g"), schrift.get("kern")],
                     sort_keys=True, ensure_ascii=False)
    return hashlib.sha1(roh.encode("utf-8")).hexdigest()[:16]


def _kern_tabelle(font, paare: Dict) -> None:
    """GPOS mit Feature «kern» direkt bauen (schneller als über den Feature-Text bei ~30 000 Paaren)."""
    from fontTools.otlLib.builder import buildLookup, buildPairPosGlyphs, buildValue
    from fontTools.ttLib import newTable
    from fontTools.ttLib.tables import otTables as ot
    werte = {(a, b): (buildValue({"XAdvance": w}), None) for (a, b), w in paare.items()}
    lookup = buildLookup(buildPairPosGlyphs(werte, font.getReverseGlyphMap()))
    gpos = ot.GPOS()
    gpos.Version = 0x00010000
    gpos.LookupList = ot.LookupList()
    gpos.LookupList.Lookup = [lookup]
    gpos.LookupList.LookupCount = 1
    rec = ot.FeatureRecord()
    rec.FeatureTag = "kern"
    rec.Feature = ot.Feature()
    rec.Feature.FeatureParams = None
    rec.Feature.LookupListIndex = [0]
    rec.Feature.LookupCount = 1
    gpos.FeatureList = ot.FeatureList()
    gpos.FeatureList.FeatureRecord = [rec]
    gpos.FeatureList.FeatureCount = 1
    gpos.ScriptList = ot.ScriptList()
    gpos.ScriptList.ScriptRecord = []
    for tag in ("DFLT", "latn", "grek"):
        sr = ot.ScriptRecord()
        sr.ScriptTag = tag
        sr.Script = ot.Script()
        sr.Script.DefaultLangSys = ot.LangSys()
        sr.Script.DefaultLangSys.LookupOrder = None
        sr.Script.DefaultLangSys.ReqFeatureIndex = 0xFFFF
        sr.Script.DefaultLangSys.FeatureIndex = [0]
        sr.Script.DefaultLangSys.FeatureCount = 1
        sr.Script.LangSysRecord = []
        sr.Script.LangSysCount = 0
        gpos.ScriptList.ScriptRecord.append(sr)
    gpos.ScriptList.ScriptCount = len(gpos.ScriptList.ScriptRecord)
    t = newTable("GPOS")
    t.table = gpos
    font["GPOS"] = t


def bauen(schrift: Dict, ziel: str) -> str:
    """Schrift (Format wie einlinien.json) als TTF nach «ziel» schreiben."""
    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.ttGlyphPen import TTGlyphPen

    k = VERSAL / float(schrift.get("cap") or 1.0)
    name = schrift["name"]
    # Optischer Abstand (mit der Rillenbreite gerechnet) statt der Breiten und Paare der Vorlage
    from .einlinien import optische_metrik
    opt_breite, opt_paare = optische_metrik(schrift)
    glyphen = {".notdef": None}
    cmap = {}
    breiten = {}
    tief = 0
    hoch = VERSAL
    for ch, (vorschub, striche) in sorted(schrift["g"].items()):
        if len(ch) != 1:
            continue
        cp = ord(ch)
        gname = "uni%04X" % cp if cp > 0xFFFF or cp >= 32 else None
        if not gname:
            continue
        konturen = []
        for s in striche:
            pts = [(int(round(s[i] * k)), int(round(s[i + 1] * k))) for i in range(0, len(s) - 1, 2)]
            # doppelte Nachbarpunkte weg
            p2 = [pts[0]]
            for p in pts[1:]:
                if p != p2[-1]:
                    p2.append(p)
            if len(p2) < 2:
                continue
            if p2[0] == p2[-1] and len(p2) > 2:
                p2 = p2[:-1]
                ring = p2 + [p2[0]] + p2[::-1][:-1]   # einmal herum und zurück
            else:
                ring = p2 + p2[-2:0:-1]                # hin und zurück
            konturen.append(ring)
            for _x, y in ring:
                tief = min(tief, y)
                hoch = max(hoch, y)
        glyphen[gname] = konturen
        cmap[cp] = gname
        breiten[gname] = int(round(opt_breite.get(ch, vorschub) * k))
    reihenfolge = list(glyphen)
    fb = FontBuilder(EINHEITEN, isTTF=True)
    fb.setupGlyphOrder(reihenfolge)
    fb.setupCharacterMap(cmap)
    glyf = {}
    metrik = {}
    for g in reihenfolge:
        pen = TTGlyphPen(None)
        xmin = 0
        if glyphen[g]:
            xmin = min(x for ring in glyphen[g] for x, _y in ring)
            for ring in glyphen[g]:
                pen.moveTo(ring[0])
                for p in ring[1:]:
                    pen.lineTo(p)
                pen.closePath()
        glyf[g] = pen.glyph()
        metrik[g] = (breiten.get(g, EINHEITEN // 2), xmin)
    fb.setupGlyf(glyf)
    fb.setupHorizontalMetrics(metrik)
    aufstieg = max(int(hoch * 1.05), int(VERSAL * 1.3))
    abstieg = min(int(tief * 1.05), -int(VERSAL * 0.35))
    fb.setupHorizontalHeader(ascent=aufstieg, descent=abstieg)
    fb.setupNameTable({"familyName": name, "styleName": "Regular",
                       "uniqueFontIdentifier": "HirchCAM:" + name,
                       "fullName": name, "psName": "".join(c for c in name if c.isalnum()) or "Hirch",
                       "version": "Version 1.0 hirch-" + _kennung(schrift),
                       "description": "Einlinienschrift (Hirch CAM) – Striche hin und zurück, mit Kontur zeichnen"})
    xh = int(round(float(schrift.get("xh") or VERSAL / k * 0.7) * k))
    fb.setupOS2(sTypoAscender=aufstieg, sTypoDescender=abstieg, sTypoLineGap=0,
                usWinAscent=aufstieg, usWinDescent=-abstieg, sCapHeight=VERSAL, sxHeight=xh,
                achVendID="HRCH", fsType=0)
    fb.setupPost()
    # Unterschneidung: die optischen Paare (ersetzen das Kerning der Vorlage)
    paare = {}
    for paar, wert in opt_paare.items():
        a, b = cmap.get(ord(paar[0])), cmap.get(ord(paar[1]))
        w = int(round(float(wert) * k))
        if a and b and w:
            paare[(a, b)] = w
    if paare:
        _kern_tabelle(fb.font, paare)
    os.makedirs(os.path.dirname(ziel) or ".", exist_ok=True)
    tmp = ziel + ".neu"
    fb.save(tmp)
    os.replace(tmp, ziel)
    return ziel


# vom Paket vorgebaute Schriften (fork/paket.sh: python3 -m hirch_cam.schrift_ttf --vorrat)
VORRAT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "schriften", "ttf")


def vorrat_bauen(ziel: str = VORRAT) -> List[str]:
    """Alle mitgelieferten Einlinienschriften als TTF in den Vorrat (für das Paket)."""
    from . import einlinien
    import json as _json
    with open(einlinien._DATEI, encoding="utf-8") as fh:
        mit = _json.load(fh)
    os.makedirs(ziel, exist_ok=True)
    liste = {}
    for name, s in mit.items():
        s = dict(s, name=name)
        datei = _dateiname(name)
        bauen(s, os.path.join(ziel, datei))
        liste[datei] = _kennung(s)
    with open(os.path.join(ziel, "vorrat.json"), "w", encoding="utf-8") as fh:
        _json.dump(liste, fh, ensure_ascii=False, indent=0)
    return sorted(liste)


def abgleichen(nur: Optional[List[str]] = None) -> List[str]:
    """Alle Einlinienschriften (mitgelieferte und eigene) als TTF bereitstellen;
    nur neu bauen, was sich geändert hat. Liefert die geschriebenen Dateien."""
    from . import einlinien
    einlinien.neu_laden()
    o = ordner()
    os.makedirs(o, exist_ok=True)
    vermerk_pfad = os.path.join(o, ".hirch-schriften.json")
    try:
        with open(vermerk_pfad, encoding="utf-8") as fh:
            vermerk = json.load(fh)
    except (OSError, ValueError):
        vermerk = {}
    vorrat_ordner = os.environ.get("HIRCH_SCHRIFT_VORRAT") or VORRAT
    try:
        with open(os.path.join(vorrat_ordner, "vorrat.json"), encoding="utf-8") as fh:
            vorrat = json.load(fh)
    except (OSError, ValueError):
        vorrat = {}
    neu = []
    for name, s in einlinien.schriften().items():
        if nur and name not in nur:
            continue
        if s.get("art") == "umriss":
            continue                     # Umrissschriften sind normale Schriften
        s = dict(s, name=name)
        datei = os.path.join(o, _dateiname(name))
        kenn = _kennung(s)
        if vermerk.get(datei) == kenn and os.path.exists(datei):
            continue
        try:
            if vorrat.get(os.path.basename(datei)) == kenn:
                # im Paket schon gebaut: nur kopieren (der erste Start wartet nicht)
                tmp = datei + ".neu"
                shutil.copyfile(os.path.join(vorrat_ordner, os.path.basename(datei)), tmp)
                os.replace(tmp, datei)
            else:
                bauen(s, datei)
        except Exception as e:  # eine kaputte Schrift soll die anderen nicht aufhalten
            print("Hirch CAM: Schrift «%s» nicht gebaut: %s" % (name, e), file=sys.stderr)
            continue
        vermerk[datei] = kenn
        neu.append(datei)
    with open(vermerk_pfad, "w", encoding="utf-8") as fh:
        json.dump(vermerk, fh, ensure_ascii=False, indent=0)
    # Liste der Einlinienschriften für die Schriftauswahl (eigener Abschnitt, Vorschau als Striche)
    namen = sorted(n for n, s in einlinien.schriften().items() if s.get("art") != "umriss")
    try:
        with open(os.path.join(o, ".hirch-einlinien.txt"), "w", encoding="utf-8") as fh:
            fh.write("\n".join(namen) + "\n")
    except OSError:
        pass
    return neu


def entfernen(name: str) -> None:
    datei = os.path.join(ordner(), _dateiname(name))
    try:
        os.remove(datei)
    except OSError:
        pass


if __name__ == "__main__":
    if sys.argv[1:2] == ["--vorrat"]:
        for d in vorrat_bauen(*sys.argv[2:3]):
            print(d)
    else:
        for d in abgleichen():
            print(d)
