# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Ablauf: in welcher Reihenfolge, mit welchem Werkzeug und auf welcher Seite gefräst wird.

Ein Abschnitt ist eine ununterbrochene Folge von Werkzeugwegen mit demselben
Werkzeug auf derselben Seite. Zwischen zwei Abschnitten hält die Maschine zum
Werkzeugwechsel; zwischen Seite 1 und Seite 2 wird das Werkstück gewendet.

Reihenfolge:
- «auto»: je Seite ein Abschnitt pro Werkzeug (möglichst wenige Wechsel);
  Planfräsen zuerst, das Werkzeug der Kontur (Ausschneiden) zuletzt, sonst
  in der Reihenfolge der Liste.
- «liste»: genau in der Reihenfolge der Liste; ein Werkzeug kann später
  wiederkommen (dann gibt es einen weiteren Wechsel).

Rechenkern (engine) und Panel (dokument.analyse) benutzen dieselben Regeln.
"""
from __future__ import annotations

import math
from typing import Dict, List, Optional

MODI = ("auto", "liste")


def _stufe(arten) -> int:
    return 2 if "kontur" in arten else (0 if "planen" in arten else 1)


def abschnitte(teile: List[Dict], modus: str = "auto") -> List[Dict]:
    """teile: [{"key", "slot", "art", "seite", "pos"}] – pos = Stelle in der Liste.
    Liefert [{"seite", "slot", "keys": [...]}] in Fräsreihenfolge."""
    aus: List[Dict] = []
    # Umspannen (Fräsbahnen 27.10.): je Seite erst alles von Spannung 1, dann Spannung 2 (ohne Umspannen nur 1)
    for seite, sp in sorted({(int(t.get("seite") or 1), int(t.get("spannung") or 1)) for t in teile}):
        hier = [t for t in teile if int(t.get("seite") or 1) == seite and int(t.get("spannung") or 1) == sp]
        extra = {"spannung": sp} if sp != 1 or any(int(t.get("spannung") or 1) == 2 for t in teile) else {}
        # Planfräsen immer zuerst (Sicherheitsprüfung 04.11.): alle anderen Bahnen der Seite sind um die Plantiefe
        # tiefer gelegt und setzen die geplante Oberfläche voraus. Teilte es sein Werkzeug mit einer Kontur, kam
        # es sonst mit ihr zuletzt – Eintauchen und erste Zustellung gingen in die noch nicht geplante Schicht.
        plan = [t for t in hier if t["art"] == "planen"]
        n0 = len(aus)
        if plan and len(plan) < len(hier):
            for s in dict.fromkeys(t["slot"] for t in sorted(plan, key=lambda t: t["pos"])):
                aus.append(dict({"seite": seite, "slot": s,
                                 "keys": [t["key"] for t in sorted(plan, key=lambda t: t["pos"]) if t["slot"] == s]},
                                **extra))
            hier = [t for t in hier if t["art"] != "planen"]
        if modus == "liste":
            neu = True
            for t in sorted(hier, key=lambda t: t["pos"]):
                if not neu and aus[-1]["slot"] == t["slot"]:
                    aus[-1]["keys"].append(t["key"])
                else:
                    aus.append(dict({"seite": seite, "slot": t["slot"], "keys": [t["key"]]}, **extra))
                neu = False
        else:
            gruppen: Dict[str, List[Dict]] = {}
            for t in hier:
                gruppen.setdefault(t["slot"], []).append(t)
            folge = sorted(gruppen, key=lambda s: (_stufe([t["art"] for t in gruppen[s]]),
                                                   min(t["pos"] for t in gruppen[s])))
            for s in folge:
                aus.append(dict({"seite": seite, "slot": s,
                                 "keys": [t["key"] for t in sorted(gruppen[s], key=lambda t: t["pos"])]}, **extra))
        # Kernloch vor dem Gewinde (Zufallstests 06.11.): bohrt das Werkzeug fürs Kernloch auch eine Kontur, kam es mit
        # ihr zuletzt – der Gewindefräser tauchte vorher ins volle Material. Das Kernloch kommt dann in einen eigenen
        # Abschnitt direkt vor den Abschnitt seines Gewindes.
        slot_von = {t["key"]: t["slot"] for t in hier}
        gi = n0
        while gi < len(aus):              # Länge jedes Mal neu (Fable 06.11.: ab dem 3. Gewinde blieb ein Kernloch
            for g in list(aus[gi]["keys"]):  # dahinter, als die Länge vor der Schleife feststand)
                if "#" in g:
                    continue
                basis_, at_, sp_ = g.partition("@")              # Umspannen: «ge1@2» → «ge1#kernloch@2»
                kl = basis_ + "#kernloch" + (at_ + sp_)
                ki = next((i for i in range(n0, len(aus)) if kl in aus[i]["keys"]), None)
                if ki is None or ki < gi:
                    continue
                aus[ki]["keys"].remove(kl)
                aus.insert(gi, dict({"seite": seite, "slot": slot_von.get(kl, aus[ki]["slot"]), "keys": [kl]}, **extra))
                break                     # das Gewinde steht jetzt eins weiter hinten: dort weiter prüfen
            gi += 1
        aus[n0:] = [a_ for a_ in aus[n0:] if a_["keys"]]
        # Planfräsen vorgezogen: folgt dasselbe Werkzeug gleich danach, bleibt es ein Abschnitt (kein Wechsel)
        i = n0
        while i + 1 < len(aus):
            if aus[i]["slot"] == aus[i + 1]["slot"]:
                aus[i]["keys"] += aus.pop(i + 1)["keys"]
            else:
                i += 1
    return aus


def rechenfolge(bearbeitungen: List[Dict], modus: str = "auto") -> List[Dict]:
    """Bearbeitungen in der Reihenfolge, in der sie gerechnet werden (eine
    Tasche weiss so, was vorher schon ausgeräumt ist)."""
    def rang(bb):
        seite = int(bb.get("auf_seite") or 1)
        i = bearbeitungen.index(bb)
        if bb.get("art") == "planen":
            return (seite, -1, i)                 # Planfräsen zuerst (wie abschnitte)
        if modus == "liste":
            return (seite, 0, i)
        gleiche = [x for x in bearbeitungen
                   if x["werkzeug"] == bb["werkzeug"] and int(x.get("auf_seite") or 1) == seite and x["objekte"]]
        return (seite, _stufe([x["art"] for x in gleiche]),
                min([bearbeitungen.index(x) for x in gleiche] or [i]))
    return sorted(bearbeitungen, key=rang)


def werkzeugnummern(folge: List[Dict]) -> Dict[str, int]:
    """T-Nummer je Werkzeugplatz: in der Reihenfolge des ersten Einsatzes
    (über beide Seiten gleich, ein wiederkehrendes Werkzeug behält seine Nummer)."""
    nr: Dict[str, int] = {}
    for a in folge:
        if a["slot"] not in nr:
            nr[a["slot"]] = len(nr) + 1
    return nr


def zustellungen(art: str, p: Dict, teil: str = "", dicke: Optional[float] = None) -> Dict:
    """Tiefe, Anzahl Zustellungen und Tiefe je Zustellung (für die Anzeige)."""
    def zahl(v, d=0.0):
        try:
            return float(v)
        except (TypeError, ValueError):
            return d
    if art == "vcarve":
        t = zahl(p.get("max_tiefe"), 5.0)
        if p.get("zustell_modus") and t > 0:
            return _mit_art(p, t, 0.0, {"tiefe": t, "durch": False})
        z = zahl(p.get("zustellung"), 0.0)
        n = max(1, math.ceil(t / z - 1e-9)) if 0 < z < t else 1
        return {"tiefe": t, "n": n, "je": t / n, "durch": False}
    if art == "gewinde":
        t = zahl(p.get("tiefe"), 8.0)
        n = max(1, int(zahl(p.get("durchgaenge"), 1)))
        return {"tiefe": t, "n": n, "je": t, "durch": False}
    tiefe = zahl(p.get("tiefe"), 0.0)
    durch = bool(p.get("durch"))
    if art == "kontur" and p.get("tiefe_ab") == "haut" and p.get("_haut_tiefe") and not teil:
        # B-15: Tiefe rechnet Hirch CNC (von der Oberfläche bis durch die Haut, tiefste Stelle)
        t = zahl(p.get("_haut_tiefe"), 0.0)
        z = zahl(p.get("zustellung"), 0.0) or t
        n = max(1, math.ceil(t / z - 1e-9))
        return {"tiefe": t, "n": n, "je": t / n, "durch": True}
    if not tiefe and art == "kontur" and dicke:
        tiefe, durch = float(dicke) + 0.2, True
    if p.get("fase") and art == "kontur":
        return {"tiefe": zahl(p.get("fase_breite"), 0.5), "n": 1, "je": zahl(p.get("fase_breite"), 0.5),
                "durch": False}
    if teil == "raeumen":
        zust = zahl(p.get("raeum_zustellung"), 0.0) or zahl(p.get("_raeum_d"), 0.0) / 2
        zust = min(zust, tiefe) if tiefe > 0 else zust
    else:
        zust = zahl(p.get("schlicht_zustellung") if teil == "schlicht" else p.get("zustellung"), 0.0)
    if teil == "schlicht" and not zust:
        zust = tiefe
    start = zahl(p.get("starttiefe"), 0.0) if art in ("tasche", "kontur") else 0.0
    start = max(0.0, start) if 0 < start < tiefe else 0.0
    weg = tiefe - start
    if teil == "schlicht" and zust >= tiefe - 1e-9:
        zust = weg                     # «Schlichten in einem Durchgang» ab der Starttiefe
    if zust <= 0:
        zust = weg or 1.0
    if not teil and p.get("zustell_modus") and weg > 0:
        return _mit_art(p, weg, start, {"tiefe": tiefe, "durch": durch, "start": start})
    n = max(1, int(math.ceil(weg / zust - 1e-6))) if weg > 0 else 1
    if teil == "schlicht" and art == "kontur" and zahl(p.get("schlicht_tiefe"), 0.0) > 0:
        # B-2: eigene Tiefe des Schlichtens (engine: bis zur Tiefe wie bisher, darunter mit der Zustellung des
        # Schruppens – höchstens der Schlicht-Zustellung)
        t_s = zahl(p.get("schlicht_tiefe"), 0.0)
        if t_s < tiefe - 1e-9:
            weg_s = max(0.0, t_s - start)
            n = max(1, int(math.ceil(weg_s / min(zust, weg_s or 1.0) - 1e-6))) if weg_s > 0 else 1
        elif t_s > tiefe + 1e-9:
            z_t = zahl(p.get("zustellung"), 0.0) or (t_s - tiefe)
            if zahl(p.get("schlicht_zustellung"), 0.0):
                z_t = min(z_t, zahl(p.get("schlicht_zustellung"), 0.0))
            n += max(1, int(math.ceil((t_s - max(start, tiefe)) / z_t - 1e-6)))
        durch = bool(dicke) and t_s >= float(dicke) - 1e-9
        weg = t_s - start
        tiefe = t_s
    return {"tiefe": tiefe, "n": n, "je": (weg / n if n else weg), "durch": durch, "start": start}


def _mit_art(p: Dict, weg: float, start: float, aus: Dict) -> Dict:
    """Zustellungen mit Art (letzte kleiner, eigene Liste, ein Durchgang): Anzahl, grösste und
    die einzelnen Tiefen je Zustellung («stufen», von oben nach unten)."""
    from .ops.bahn import zustellung_aus, zustellungen as stufen_von
    z = zustellung_aus(p, tiefe=weg)
    try:
        tiefen = [-t for t in stufen_von(weg, z if z else weg)]
    except (TypeError, ValueError):
        tiefen = [weg]
    stufen = [round(b - a, 4) for a, b in zip([0.0] + tiefen[:-1], tiefen)]
    aus.update(n=len(stufen), je=max(stufen), stufen=stufen,
               gleich=max(stufen) - min(stufen) < 1e-6)
    return aus


def zusatz(art: str, p: Dict, teil: str = "") -> str:
    """Ein kurzer Hinweis je Bearbeitung (Stege, Schlichten …) – höchstens einer."""
    if teil == "flach":
        return "Flachbereich"
    if teil == "schlicht":
        return "Schlichten"
    if teil == "kernloch":
        return "Kernloch"
    if teil == "raeumen":
        return "Räumen"
    if teil.startswith("stufe"):                 # Böden und Wände im Wechsel mit dem 3D-Schruppen
        return "Stufe " + teil[5:]
    if art == "tasche" and p.get("raeum_werkzeug") and p.get("raeum_werkzeug") != p.get("werkzeug"):
        return "Restmaterial"
    if art == "kontur":
        if p.get("fase"):
            return "Fase"
        n = int(p.get("stege_anzahl") or 0)
        if p.get("durch") and p.get("stege_modus") == "keine":
            return "ohne Haltestege"
        # «manuell»: die Stege sind gelbe Marken, gleichmässig verteilt mit dieser Anzahl (Entwurf B)
        if p.get("durch") and n:
            return f"{n} Haltesteg" if n == 1 else f"{n} Haltestege"
    if art == "tasche" and p.get("strategie") == "adaptiv":
        return "adaptiv"
    if art == "bohren" and p.get("methode") == "helix":
        return "Helix"
    if art == "gewinde":
        return "innen" if p.get("innen", True) else "aussen"
    if p.get("passloecher"):
        return p.get("passloch_text") or ""
    return ""


def spindel(w: Dict, vorschuebe: List[float], material: Optional[str], maschine: Optional[Dict] = None) -> Dict:
    """Drehzahl zum Einstellen (von Hand) und Prüfung des Zahnvorschubs.
    fz = Vorschub / (Drehzahl · Schneiden); zu klein: der Fräser reibt statt zu schneiden."""
    from .werkzeugdb import materialien
    n = float(w.get("drehzahl") or 0)
    aus = {"drehzahl": int(round(n / 100.0) * 100) if n > 0 else 0}
    m = materialien().get(material or "")
    f = min([v for v in vorschuebe if v > 0] or [float(w.get("vorschub") or 0)])
    z = max(1, int(w.get("schneiden") or 2))
    if not m or n <= 0 or f <= 0:
        return aus
    fz = f / (n * z)
    grenze = m["fz_min"] * (0.6 if w.get("typ") == "v" else 1.0)
    ziel = float((w.get("empfehlung") or {}).get("fz") or 0) or 2 * grenze
    aus["fz"] = round(fz, 4)
    if fz < grenze * 0.999:
        aus["reibt"] = True
        n_min = float((maschine or {}).get("drehzahl_min") or 0)
        aus["drehzahl_ziel"] = int(max(n_min, 100, round(f / (z * ziel) / 100.0) * 100))
        aus["vorschub_ziel"] = int(round(n * z * ziel / 10.0) * 10)
    return aus


def info(folge, nach_key, nach_id, werkzeug_namen, nummer, teil_minuten=None, abschnitt_zeit=None,
         seiten_zeit=None, op_warnungen=None, dicke=None, zweiseitig=False, werkzeuge=None,
         material=None, maschine=None, nur_spannung2=()) -> Dict:
    """Ablauf für das Panel: Seiten › Abschnitte (Werkzeug) › Bearbeitungen.
    nach_key: Schlüssel -> {bid, teil}; nach_id: Bearbeitung -> {art, name, farbe, p}.
    nur_spannung2: Hinweise, die nur in der Zeile des Teils «Spannung 2» (Schlüssel …@2) stehen."""
    from .farben import FRAESARTEN, hex_farbe
    teil_minuten = teil_minuten or {}
    seiten_zeit = seiten_zeit or {}
    op_warnungen = op_warnungen or {}
    seiten: List[Dict] = []
    vorher_slot = None
    for i, ab in enumerate(folge):
        seite = ab["seite"]
        erster = not seiten or seiten[-1]["seite"] != seite
        if erster:
            seiten.append({"seite": seite, "minuten": seiten_zeit.get(seite, 0.0), "abschnitte": []})
        ops = []
        for k in ab["keys"]:
            t = nach_key[k]
            bb = nach_id.get(t["bid"], {})
            art = bb.get("art", "")
            p = dict(bb.get("p") or {})
            z = zustellungen(art, p, t["teil"], dicke)
            farbe = bb.get("farbe") or (hex_farbe(FRAESARTEN[art][0]) if art in FRAESARTEN else "")
            ops.append(dict(z, id=t["bid"], key=k, teil=t["teil"], art=art,
                            name=(bb.get("name") or "").split(" (")[0], farbe=farbe,
                            minuten=teil_minuten.get(k, 0.0), extra=zusatz(art, p, t["teil"]),
                            warnungen=[] if t["teil"] else [
                                w_ for w_ in op_warnungen.get(t["bid"], [])
                                if w_ not in nur_spannung2 or k.endswith("@2")]))
        # Halt zum Wechseln: anderes Werkzeug als im Abschnitt davor (auch über das Wenden hinweg)
        wechsel = vorher_slot is not None and ab["slot"] != vorher_slot
        abschnitt = {
            "slot": ab["slot"], "t": nummer[ab["slot"]], "werkzeug": werkzeug_namen.get(ab["slot"], ab["slot"]),
            "wechsel": wechsel, "minuten": (abschnitt_zeit[i] if abschnitt_zeit else 0.0), "ops": ops,
            "nach_wenden": erster and seite == 2}
        if "spannung" in ab:
            # Umspannen: Spannung des Abschnitts; der erste von Spannung 2 einer Seite kommt nach dem Umspannen
            abschnitt["spannung"] = ab["spannung"]
            abschnitt["nach_umspannen"] = ab["spannung"] == 2 and (
                i == 0 or folge[i - 1].get("spannung") != 2 or folge[i - 1]["seite"] != seite)
        w = (werkzeuge or {}).get(ab["slot"])
        if w:
            vs = []
            for k in ab["keys"]:
                p = (nach_id.get(nach_key[k]["bid"], {}).get("p") or {})
                if not nach_key[k]["teil"] and p.get("vorschub_mm"):
                    vs.append(float(p["vorschub_mm"]))
            abschnitt.update(spindel(w, vs, material, maschine))
        seiten[-1]["abschnitte"].append(abschnitt)
        vorher_slot = ab["slot"]
    return {"zweiseitig": zweiseitig, "seiten": seiten}


def vorab(auftrag: Dict, zweiseitig: bool = False) -> Dict:
    """Ablauf schon vor dem Rechnen (für das Panel, ohne Zeiten): dieselben Regeln
    wie im Rechenkern, die Teile (Flachbereich, Schlichten, Kernloch) aus den
    Einstellungen."""
    from .engine import _bearbeitungen, werkzeug_name
    werkzeuge = auftrag.get("werkzeuge") or {}
    bbs = [b for b in _bearbeitungen(auftrag, {}) if b["objekte"]]
    modus = auftrag.get("reihenfolge") if auftrag.get("reihenfolge") in MODI else "auto"
    teile: List[Dict] = []
    pos = 0

    def dazu(bb, slot, teil=""):
        nonlocal pos
        if slot and slot in werkzeuge:
            teile.append({"key": bb["id"] + ("#" + teil if teil else ""), "slot": slot, "art": bb["art"],
                          "seite": bb["auf_seite"], "pos": pos, "bid": bb["id"], "teil": teil})
        pos += 1
    for bb in bbs:
        p = bb["p"]
        if bb["art"] == "gewinde" and p.get("innen", True) and p.get("kernloch_werkzeug"):
            dazu(bb, p["kernloch_werkzeug"], "kernloch")
        r_slot = p.get("raeum_werkzeug") if bb["art"] == "tasche" else ""
        if r_slot and r_slot != bb["werkzeug"] and r_slot in werkzeuge and bb["werkzeug"] in werkzeuge:
            # nur ein grösseres Räumwerkzeug wird verwendet (wie im Rechenkern)
            from .engine import raeumbreite
            t_ = float(p.get("tiefe") or 1)
            gross = dict(werkzeuge[r_slot], durchmesser=werkzeuge[r_slot].get("durchmesser", 3))
            klein = dict(werkzeuge[bb["werkzeug"]], durchmesser=werkzeuge[bb["werkzeug"]].get("durchmesser", 3))
            if raeumbreite(gross, t_, t_) <= raeumbreite(klein, t_, float(p.get("zustellung") or t_)) + 1e-6:
                r_slot = p["raeum_werkzeug"] = ""
        if r_slot and r_slot != bb["werkzeug"]:
            p["_raeum_d"] = (werkzeuge.get(r_slot) or {}).get("durchmesser", 0)
            dazu(bb, r_slot, "raeumen")
        if bb["art"] == "vcarve" and bb.get("flach"):
            dazu(bb, bb["flach"], "flach")      # Flachbereich zuerst
        dazu(bb, bb["werkzeug"])
        if bb["art"] in ("tasche", "kontur") and p.get("schlichten") and not p.get("fase"):
            dazu(bb, p.get("schlicht_werkzeug") or bb["werkzeug"], "schlicht")
    folge = abschnitte(teile, modus)
    nach_key = {t["key"]: t for t in teile}
    nach_id = {b["id"]: b for b in bbs}
    dicke = (auftrag.get("hoehen") or {}).get("materialdicke")
    aus = info(folge, nach_key, nach_id, {k: werkzeug_name(dict(v, durchmesser=v.get("durchmesser", 3),
                                                                 v_winkel=v.get("v_winkel", 60)), True)
                                          for k, v in werkzeuge.items()},
               werkzeugnummern(folge), dicke=dicke, zweiseitig=zweiseitig, werkzeuge=werkzeuge,
               material=auftrag.get("material"), maschine=auftrag.get("maschine"))
    aus["vorab"] = True
    return aus
