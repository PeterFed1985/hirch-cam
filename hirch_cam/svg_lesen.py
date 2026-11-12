# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Peter Federspiel
"""Linien einer SVG-Zeichnung lesen und über die Farbe den Fräsarten zuordnen.

Braucht inkex (liegt Hirch CAM bei, share/inkscape/extensions).
Koordinaten: Nullpunkt = linke untere Ecke der Seite, Y nach oben, in mm.
"""
from __future__ import annotations

from collections import OrderedDict
from typing import Dict, List, Optional

import inkex
from inkex import bezier

from .farben import FRAESARTEN, fraesart
from .einstellungen import AUCH, DICKE, FLAECHEN, NS, NULLKAMERA, NULLMARKE, PASSLOCH, SPANN, STEG, TEXT, VERSTECKT, VORRICHTUNG


def _ist_textelement(el) -> bool:
    """Text von Inkscape (auch ein Teil davon) oder in Pfade umgewandelter Text."""
    e = el
    while e is not None:
        if isinstance(e, (inkex.TextElement, inkex.FlowRoot)) or e.get(TEXT):
            return True
        e = e.getparent()
    return False


def nur_fuellung(elem) -> bool:
    """Ein Objekt ohne Linie, aber mit Füllung in einer Farbe (kein Text) – wird nie gefräst."""
    style = elem.specified_style()
    s = style.get("stroke")
    if s and s not in ("none", "transparent") and not str(s).startswith("url("):
        return False
    f = style.get("fill")
    return bool(f) and f not in ("none", "transparent") and not str(f).startswith("url(") and \
        not _ist_textelement(elem)


def farbe(elem):
    """Linienfarbe als (r, g, b) – oder None. Die Füllfarbe zählt nur bei Text: In Hirch gibt es keine Füllungen,
    ein gefülltes Objekt (z. B. aus der Formerstellung) darf nie von allein eine Fräsart bekommen (Peter 08.11.)."""
    style = elem.specified_style()
    for key in ("stroke", "fill") if _ist_textelement(elem) else ("stroke",):
        val = style.get(key)
        if val and val not in ("none", "transparent") and not str(val).startswith("url("):
            try:
                c = inkex.Color(val).to_rgb()
                return (int(c.red), int(c.green), int(c.blue))
            except Exception:
                continue
    return None


def materialdicke(svg, standard=18.0) -> float:
    try:
        # auch «1,75» (ältere Versionen schrieben in de/it mit Komma)
        return float(str(svg.get(DICKE)).strip().replace(",", "."))
    except (TypeError, ValueError):
        return standard


_UNSICHTBAR = {"defs", "marker", "pattern", "symbol", "clipPath", "mask", "metadata",
               "namedview"}


def _sichtbar(el) -> bool:
    """Ausgeblendete Objekte und Ebenen (display:none) zählen nicht, ebenso
    Vorlagen in <defs> (Marker, Muster, Symbole, Masken …)."""
    e = el
    while e is not None:
        tag = e.tag.split("}")[-1] if isinstance(e.tag, str) else ""
        if tag in _UNSICHTBAR:
            return False
        if e.get(VERSTECKT) is None:     # im Panel ausgeblendet: wird trotzdem gefräst
            if e.get("display") == "none":
                return False
            st = e.get("style")
            if st and "display:none" in st.replace(" ", ""):
                return False
        e = e.getparent()
    return True


def elemente(svg, auswahl: Optional[List] = None):
    quelle = auswahl if auswahl else [svg]
    for wurzel in quelle:
        for el in wurzel.descendants():
            if not isinstance(el, inkex.ShapeElement):
                continue
            if isinstance(el, (inkex.Group, inkex.TextElement, inkex.Tspan,
                               inkex.Image, inkex.Use, inkex.FlowRoot)):
                continue
            if not hasattr(el, "path"):
                continue
            if not _sichtbar(el):
                continue
            yield el


_LINIEN_SPEICHER: "OrderedDict[tuple, List]" = OrderedDict()
_LINIEN_HOECHSTENS = 20000


def linien(elem, uu_pro_mm, hoehe_uu, toleranz) -> List:
    """Pfad eines Elements als Polylinien in mm: Liste von (punkte, geschlossen).

    Das Auflösen der Kurven (cspsubdiv) ist der teure Teil beim Lesen. Das Ergebnis hängt nur vom
    Pfad, der Transformation, der Seitenhöhe und der Toleranz ab – im Rechendienst bleibt es darum
    gemerkt (Fräsbahnen 25.10.: dieselbe Zeichnung wird je Rechnung zweimal gelesen, Platte 1 und
    Vorrichtung, und nach jeder Änderung wieder)."""
    # composed_transform() genau einmal je Aufruf wie bisher: inkex 1.4 liefert beim ersten Aufruf je
    # Dokument einen um ~1e-5 anderen Wert (Wurzel-Transformation) – ein zusätzlicher Aufruf für den
    # Schlüssel würde die Punkte verschieben und der G-Code wäre nicht mehr bitgleich
    tr = elem.composed_transform()
    try:
        # volle Genauigkeit: str() von Pfad und Transformation rundet auf 6 Stellen
        schl = (tuple((s.letter, tuple(s.args)) for s in elem.path),
                tuple(v for zeile in tr.matrix for v in zeile),
                float(uu_pro_mm), float(hoehe_uu), float(toleranz))
    except Exception:                 # ohne Schlüssel: einfach rechnen
        return _linien(elem, uu_pro_mm, hoehe_uu, toleranz, tr)
    alt = _LINIEN_SPEICHER.get(schl)
    if alt is None:
        alt = _linien(elem, uu_pro_mm, hoehe_uu, toleranz, tr)
        _LINIEN_SPEICHER[schl] = alt
        if len(_LINIEN_SPEICHER) > _LINIEN_HOECHSTENS:
            _LINIEN_SPEICHER.popitem(last=False)
    else:
        _LINIEN_SPEICHER.move_to_end(schl)
    return [(list(pts), zu) for pts, zu in alt]


def _linien(elem, uu_pro_mm, hoehe_uu, toleranz, tr=None) -> List:
    path = elem.path.to_absolute().transform(elem.composed_transform() if tr is None else tr)
    teile = []
    aktuell = None
    for seg in path:
        if seg.letter in "Mm":
            aktuell = inkex.Path([seg])
            teile.append([aktuell, False])
        elif aktuell is not None:
            aktuell.append(seg)
            if seg.letter in "Zz":
                teile[-1][1] = True
    ergebnis = []
    tol_uu = toleranz * uu_pro_mm
    for teil, zu in teile:
        csp = teil.to_superpath()
        bezier.cspsubdiv(csp, tol_uu)
        for sub in csp:
            pts = [(p[1][0] / uu_pro_mm, (hoehe_uu - p[1][1]) / uu_pro_mm) for p in sub]
            if len(pts) >= 2:
                geschlossen = zu or (len(pts) > 2 and abs(pts[0][0] - pts[-1][0]) < 1e-6
                                     and abs(pts[0][1] - pts[-1][1]) < 1e-6)
                ergebnis.append((pts, geschlossen))
    return ergebnis


def _ist_text(el) -> bool:
    """In Pfade umgewandelter Text (vom Panel markiert, auch die Gruppe darüber)."""
    while el is not None:
        if el.get(TEXT):
            return True
        el = el.getparent()
    return False


def einlinig(stuecke, fang: float = 0.002):
    """Text in einer Einlinienschrift: die Schrift speichert jeden Strich als
    Umriss hin und zurück. Solche Umrisse werden wieder einfache Linien, damit
    jeder Strich nur einmal gefräst wird. Normale Schriften bleiben Umrisse."""
    from .schriftimport import zurueckfuehren
    aus = [(pts, zu) for pts, zu in stuecke if not zu]
    for pts, zu in stuecke:
        if not zu:
            continue
        linien, _ok = zurueckfuehren([(pts, True)], fang)
        for l in linien:
            geschlossen = len(l) > 2 and abs(l[0][0] - l[-1][0]) < 1e-9 and abs(l[0][1] - l[-1][1]) < 1e-9
            aus.append((l, geschlossen))
    return aus


def _umlauf(ring, x, y) -> int:
    """Umlaufzahl des Rings um den Punkt (x, y)."""
    n = 0
    for i in range(len(ring)):
        x0, y0 = ring[i]
        x1, y1 = ring[(i + 1) % len(ring)]
        if y0 <= y:
            if y1 > y and (x1 - x0) * (y - y0) - (x - x0) * (y1 - y0) > 0:
                n += 1
        elif y1 <= y and (x1 - x0) * (y - y0) - (x - x0) * (y1 - y0) < 0:
            n -= 1
    return n


def wie_gefuellt(stuecke):
    """Umrisse so auflösen, wie Inkscape sie füllt (Regel «nonzero»): überlappende
    und sich selbst kreuzende Umrisse einer Schrift werden zu sauberen Flächen.
    Offene Linien bleiben, wie sie sind. Ohne Kreuzungen bleibt alles unverändert."""
    try:
        from shapely.geometry import LineString, MultiLineString
        from shapely.ops import polygonize, unary_union
    except ImportError:
        return stuecke
    ringe = []
    for pts, zu in stuecke:
        if zu and len(pts) >= 3:
            r = list(pts)
            if abs(r[0][0] - r[-1][0]) < 1e-9 and abs(r[0][1] - r[-1][1]) < 1e-9:
                r = r[:-1]
            if len(r) >= 3:
                ringe.append(r)
    if not ringe:
        return stuecke
    try:
        if MultiLineString([r + [r[0]] for r in ringe]).is_simple:
            return stuecke
        netz = unary_union([LineString(r + [r[0]]) for r in ringe])
        flaechen = []
        for f in polygonize(netz):
            if f.area < 1e-9:
                continue
            q = f.representative_point()
            if sum(_umlauf(r, q.x, q.y) for r in ringe) != 0:
                flaechen.append(f)
        ganz = unary_union(flaechen) if flaechen else None
    except Exception:
        return stuecke
    if ganz is None or ganz.is_empty:
        return stuecke
    aus = [(pts, zu) for pts, zu in stuecke if not (zu and len(pts) >= 3)]
    for poly in getattr(ganz, "geoms", [ganz]):
        if poly.geom_type != "Polygon":
            continue
        for ring in [poly.exterior] + list(poly.interiors):
            pts = [(float(x), float(y)) for x, y in ring.coords]
            aus.append((pts, True))
    return aus


KLON = "data-hirch-klon"


def klone_aufloesen(svg, tiefe: int = 8) -> int:
    """Klone (<use>, auch gekachelte Klone) durch Kopien des Originals ersetzen, damit
    sie wie gezeichnete Objekte gefräst werden. Die Kopie behält die ID des Klons
    (das Panel wählt so den Klon aus). Liefert die Anzahl aufgelöster Klone."""
    n = 0
    for _ in range(tiefe):
        klone = [u for u in svg.iter() if isinstance(u, inkex.Use)]
        if not klone:
            break
        for u in klone:
            try:
                ziel = u.href
            except Exception:
                ziel = None
            if ziel is None:
                u.getparent().remove(u)
                continue
            kopie = ziel.copy()
            if isinstance(kopie, inkex.Symbol):
                g = inkex.Group(**kopie.attrib)
                g.extend(list(kopie))
                kopie = g
            x = u.to_dimensionless(u.get("x", 0) or 0)
            y = u.to_dimensionless(u.get("y", 0) or 0)
            kopie.transform = u.transform @ inkex.Transform(translate=(x, y)) @ kopie.transform
            kopie.style = u.style + kopie.style
            alt = u.get("id")
            for k, v in u.attrib.items():          # hirch:auch, hirch:versteckt … des Klons
                if k.startswith("{https://hirch.net/cnc}") or k == "display":
                    kopie.set(k, v)
            u.replace_with(kopie)
            kopie.set_random_ids()
            if alt:
                kopie.set("id", alt)
            kopie.set(KLON, "1")
            n += 1
    return n


def objekt_id(el) -> Optional[str]:
    """ID, unter der ein Objekt im Panel erscheint: bei Text, den das Panel
    für die Rechnung in Pfade umgewandelt hat, die ID des Texts; bei einem
    aufgelösten Klon die ID des Klons."""
    e, text, klon = el, None, None
    while e is not None:
        if text is None and e.get(TEXT) is not None:
            text = e
        if e.get(KLON) is not None:
            klon = e                            # der äusserste Klon zählt
        e = e.getparent()
    return (klon if klon is not None else text if text is not None else el).get("id")


def auch(el) -> List[str]:
    """Weitere Bearbeitungen (hirch:auch) des Objekts und seiner Gruppen."""
    ids: List[str] = []
    e = el
    while e is not None:
        for bid in (e.get(AUCH) or "").split():
            if bid not in ids:
                ids.append(bid)
        e = e.getparent()
    return ids


def spann_hoehe(el) -> Optional[float]:
    """Höhe eines Spannmittels über dem Material (mm) – oder None, wenn das
    Objekt (bzw. seine Gruppe) kein Spannmittel ist."""
    e = el
    while e is not None:
        v = e.get(SPANN) if hasattr(e, "get") else None
        if v is not None:
            try:
                return max(0.0, float(str(v).replace(",", ".")))
            except ValueError:
                return 20.0
        e = e.getparent()
    return None


def spann_spannung(el) -> int:
    """Umspannen (Fräsbahnen 27.10.): «hirch:spannung="2"» am Spannmittel (oder seiner Gruppe) = zweite Lage, gilt
    nur für Spannung 2. Sonst 1."""
    from .einstellungen import NS
    e = el
    while e is not None:
        v = e.get("{%s}spannung" % NS) if hasattr(e, "get") else None
        if v is not None:
            return 2 if str(v).strip() == "2" else 1
        e = e.getparent()
    return 1


def ist_passloch(el) -> bool:
    """Passloch-Kreis aus der Ebene «Passlöcher» (Zeichnung der Löcher, nicht zum Fräsen)."""
    e = el
    while e is not None:
        if hasattr(e, "get") and e.get(PASSLOCH) is not None:
            return True
        e = e.getparent()
    return False


def ist_nullmarke(el) -> bool:
    """Kreuz des provisorischen Nullpunkts (oder in seiner Gruppe)? Wird nie gefräst."""
    e = el
    while e is not None:
        if hasattr(e, "get") and e.get(NULLMARKE) is not None:
            return True
        e = e.getparent()
    return False


def nullmarke_kamera(el) -> Optional[List[float]]:
    """Kamera-Versatz des Kreuzes (hirch:kamera="dx,dy" an der Gruppe) oder None."""
    e = el
    while e is not None:
        if hasattr(e, "get") and e.get(NULLMARKE) is not None:
            t = str(e.get(NULLKAMERA) or "").strip()
            try:
                dx, dy = (float(v.replace(",", ".")) for v in t.split(";" if ";" in t else ",")) if t else (0, 0)
            except ValueError:
                return None
            return [dx, dy] if t else None
        e = e.getparent()
    return None


def ist_steg(el) -> bool:
    """Von Hand gesetzte Haltesteg-Markierung (oder in einer solchen Gruppe)?"""
    e = el
    while e is not None:
        if hasattr(e, "get") and e.get(STEG) is not None:
            return True
        e = e.getparent()
    return False


def _steg_bearbeitung(el) -> str:
    """Zu welcher Bearbeitung eine Steg-Marke gehört: Wert von hirch:steg (an ihr oder ihrer Gruppe), wenn er
    nicht «1» ist – sonst "" (gilt für jede Kontur in der Nähe, wie bisher)."""
    e = el
    while e is not None:
        if hasattr(e, "get") and e.get(STEG) is not None:
            v = str(e.get(STEG) or "").strip()
            return "" if v in ("", "1", "true") else v
        e = e.getparent()
    return ""


def _seite_mm(svg, pg) -> Optional[Dict]:
    uu = svg.unittouu("1mm")
    hoehe_uu = svg.viewbox_height
    try:
        x, y, w, h = float(pg.x), float(pg.y), float(pg.width), float(pg.height)
    except (TypeError, ValueError):
        return None
    return {"x0": x / uu, "y0": (hoehe_uu - y - h) / uu, "links": x / uu, "oben": y / uu,
            "breite": w / uu, "hoehe": h / uu}


def vorrichtung_seite(svg) -> Optional[Dict]:
    """Die Seite der Spannvorrichtung (mm wie seiten()) – oder None."""
    try:
        pages = svg.namedview.get_pages()
    except Exception:
        return None
    for pg in pages:
        if pg.get(VORRICHTUNG) is not None:
            return _seite_mm(svg, pg)
    return None


def seiten(svg) -> List[Dict]:
    """Seiten (Pages) der Zeichnung in mm, im Koordinatensystem von Seite 1
    (x nach rechts, y nach oben, Nullpunkt unten links von Seite 1): je
    {"x0", "y0"} = linke untere Ecke, dazu "links", "oben" = linke obere Ecke
    in Dokument-mm (für das Panel). Ohne die Seite der Spannvorrichtung."""
    uu = svg.unittouu("1mm")
    hoehe_uu = svg.viewbox_height
    aus = []
    try:
        pages = [pg for pg in svg.namedview.get_pages() if pg.get(VORRICHTUNG) is None]
    except Exception:
        pages = []
    for pg in pages:
        try:
            x, y, w, h = float(pg.x), float(pg.y), float(pg.width), float(pg.height)
        except (TypeError, ValueError):
            continue
        aus.append({"x0": x / uu, "y0": (hoehe_uu - y - h) / uu, "links": x / uu, "oben": y / uu,
                    "breite": w / uu, "hoehe": h / uu})
    if not aus:
        aus.append({"x0": 0.0, "y0": 0.0, "links": 0.0, "oben": 0.0,
                    "breite": svg.viewbox_width / uu, "hoehe": hoehe_uu / uu})
    return aus


def _seite_von(stuecke, pages, sonst=1) -> int:
    """Auf welcher Seite (1, 2, …) liegt ein Objekt? Nach der Mitte seiner Umrisse.
    Auf keiner: sonst."""
    pts = [p for pts, _zu in stuecke for p in pts]
    if not pts or len(pages) < 2:
        return 1
    cx = (min(p[0] for p in pts) + max(p[0] for p in pts)) / 2
    cy = (min(p[1] for p in pts) + max(p[1] for p in pts)) / 2
    for i, pg in enumerate(pages):
        if pg["x0"] - 1e-6 <= cx <= pg["x0"] + pg["breite"] + 1e-6 and \
                pg["y0"] - 1e-6 <= cy <= pg["y0"] + pg["hoehe"] + 1e-6:
            return i + 1
    return sonst


def _verschieben(stuecke, dx, dy):
    if not dx and not dy:
        return stuecke
    return [([(x - dx, y - dy) for x, y in pts], zu) for pts, zu in stuecke]


def platten(svg, zweiseitig=False) -> int:
    """Anzahl Platten: je Seite (Page) eine, zweiseitig je zwei Seiten (oben, unten)."""
    n = len(seiten(svg))
    return max(1, (n + 1) // 2 if zweiseitig else n)


def lesen(svg, toleranz=0.01, auswahl=None, mit_linien=True, zweiseitig=False, platte=1) -> Dict:
    """Alles, was zum Fräsen gebraucht wird, aus der Zeichnung holen.
    Mehrere Seiten (Pages): jede Seite ist eine Platte (z. B. nach dem Verschachteln);
    gelesen wird nur, was auf der Platte «platte» liegt, in deren Koordinaten.
    zweiseitig: je Platte zwei Seiten – Objekte auf der zweiten gehören zu Seite 2 und
    werden in deren Koordinaten gelesen (Nullpunkt unten links der Seite)."""
    klone_aufloesen(svg)
    uu = svg.unittouu("1mm")
    hoehe_uu = svg.viewbox_height
    breite, hoehe = svg.viewbox_width / uu, hoehe_uu / uu
    alle = seiten(svg)
    vs = vorrichtung_seite(svg)
    pp = 2 if zweiseitig else 1
    n_platten = max(1, (len(alle) + pp - 1) // pp)
    vor = vs is not None and platte == 0
    platte = 0 if vor else min(max(1, int(platte or 1)), n_platten)
    pages = [vs] if vor else (alle[(platte - 1) * pp:platte * pp] or alle[:1])
    p1 = alle[0]
    if len(alle) > 1 or vor:
        breite, hoehe = pages[0]["breite"], pages[0]["hoehe"]

    def auf_vorrichtung(stuecke) -> bool:
        pts = [q for ps, _zu in stuecke for q in ps]
        if vs is None or not pts:
            return False
        cx = (min(q[0] for q in pts) + max(q[0] for q in pts)) / 2
        cy = (min(q[1] for q in pts) + max(q[1] for q in pts)) / 2
        return (vs["x0"] - 1e-6 <= cx <= vs["x0"] + vs["breite"] + 1e-6 and
                vs["y0"] - 1e-6 <= cy <= vs["y0"] + vs["hoehe"] + 1e-6)

    def zuordnen_seite(stuecke):
        """(Seite, Linien in den Koordinaten dieser Seite) – None: auf einer anderen Platte"""
        if vs is not None:
            if auf_vorrichtung(stuecke) != vor:
                return None, None
            if vor:
                return 1, _verschieben(stuecke, vs["x0"] - p1["x0"], vs["y0"] - p1["y0"])
        if len(alle) < 2:
            return 1, stuecke
        gi = _seite_von(stuecke, alle, -1) - 1
        if gi < 0:                      # auf keiner Seite: zur ersten (Hinweis «ausserhalb»)
            gi = 0
        if gi // pp != platte - 1:
            return None, None
        nr = gi % pp + 1
        pg = alle[gi]
        return nr, _verschieben(stuecke, pg["x0"] - p1["x0"], pg["y0"] - p1["y0"])
    objekte: Dict[str, List] = {k: [] for k in FRAESARTEN}
    ids: Dict[str, List[str]] = {k: [] for k in FRAESARTEN}
    unbekannt = 0
    gefuellt = 0                        # nur Füllung, keine Linie: wird nie gefräst (Hinweis)
    draussen = 0
    liste: List[Dict] = []          # je Objekt: id, art, rgb, linien
    ohne_art: List[Dict] = []       # zweiseitig: Objekte ohne Bearbeitung (nur für die blasse andere Seite)
    spannmittel: List[Dict] = []    # [{punkte, hoehe}] – werden nie gefräst
    stege: List[List[float]] = []   # Mittelpunkte der Steg-Markierungen
    stege_seite: List[int] = []
    stege_bearbeitung: List[str] = []   # Entwurf B: Id der Bearbeitung (hirch:steg="<id>"), "" = jede Kontur
    ids_seite: Dict[str, int] = {}  # Objekt-ID -> Seite
    from .relief_svg import ist_steg3d
    nullmarken_kamera: Dict[int, List] = {}  # Seite -> [dx, dy]: Kamera-Versatz am Kreuz
    nullmarken: Dict[int, List] = {}  # Seite -> [x, y]: provisorischer Nullpunkt (Mitte des Kreuzes, mm der Seite)
    for el in elemente(svg, auswahl):
        if ist_steg3d(el) or ist_passloch(el):   # Haltesteg eines 3D-Modells / Passloch-Kreis
            continue
        if ist_nullmarke(el):
            if any(hasattr(e, "get") and e.get("{%s}nullteil" % NS) is not None for e in [el] + list(el.ancestors())):
                continue                    # Beschriftung am Kreuz – nur der Kreis bestimmt die Mitte
            nr, st = zuordnen_seite(linien(el, uu, hoehe_uu, 0.01))
            pts = [p for stueck, _zu in st or [] for p in stueck]
            if nr is not None and pts:
                xs, ys = [p[0] for p in pts], [p[1] for p in pts]
                nullmarken[nr] = [(min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2]
                k = nullmarke_kamera(el)
                if k:
                    nullmarken_kamera[nr] = k
                else:
                    nullmarken_kamera.pop(nr, None)
            continue
        if ist_steg(el):
            nr, st = zuordnen_seite(linien(el, uu, hoehe_uu, 0.05))
            if nr is None:
                continue
            pts = [p for stueck, _zu in st for p in stueck]
            if pts:
                xs, ys = [p[0] for p in pts], [p[1] for p in pts]
                stege.append([(min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2])
                stege_seite.append(nr)
                stege_bearbeitung.append(_steg_bearbeitung(el))
            continue
        h_spann = spann_hoehe(el)
        if h_spann is not None:
            nr, st = zuordnen_seite(linien(el, uu, hoehe_uu, max(toleranz, 0.05)))
            for pts, _zu in st or []:
                if len(pts) >= 3:
                    spannmittel.append({"punkte": pts, "hoehe": h_spann, "seite": nr,
                                        "spannung": spann_spannung(el)})
            continue
        f = farbe(el)
        art = fraesart(f) if f else None
        weitere = auch(el)
        if art is None and not weitere:
            # Schwarz = neu gezeichnet, noch ohne Bearbeitung: kein Hinweis
            if f is not None and max(f) >= 46:
                unbekannt += 1
            elif f is None and nur_fuellung(el):
                gefuellt += 1
            # zweiseitig: auch ohne Bearbeitung blass auf der anderen Seite zeigen (Peter, 22.10.)
            if zweiseitig and mit_linien and len(ohne_art) < 2000:
                nr, st = zuordnen_seite(linien(el, uu, hoehe_uu, max(toleranz, 0.05)))
                if nr is not None:
                    ohne_art.append({"seite": nr, "linien": [{"punkte": pts} for pts, _zu in st]})
            continue
        oid = objekt_id(el)
        nr, stuecke = zuordnen_seite(linien(el, uu, hoehe_uu, toleranz))
        if nr is None:
            continue                    # auf einer anderen Platte
        if art is not None and oid and oid not in ids[art]:
            ids[art].append(oid)
        if (art == "aus" and not weitere) or not mit_linien:
            continue
        if oid:
            ids_seite[oid] = nr
        if _ist_text(el):
            stuecke = wie_gefuellt(einlinig(stuecke))
        liste.append({"id": oid, "art": None if art == "aus" else art, "rgb": f or (0, 0, 0),
                      "auch": weitere, "seite": nr, "flaechen": el.get(FLAECHEN) is not None,
                      "linien": [{"punkte": pts, "geschlossen": zu} for pts, zu in stuecke]})
        tol = 0.05
        if any(x < -tol or y < -tol or x > breite + tol or y > hoehe + tol
               for pts, _zu in stuecke for x, y in pts):
            draussen += 1
        if art not in (None, "aus"):
            for pts, zu in stuecke:
                objekte[art].append({"punkte": pts, "geschlossen": zu})
    from . import relief_svg                        # 3D-Modelle (hirch:relief)
    reliefs = relief_svg.lesen(svg, uu, hoehe_uu, zuordnen_seite) if not auswahl else []
    texte = sum(1 for el in (auswahl or [svg])
                for t in ([el] + list(el.descendants()))
                if isinstance(t, (inkex.TextElement, inkex.FlowRoot)) and not ist_nullmarke(t))
    return {"breite": breite, "hoehe": hoehe, "dicke": materialdicke(svg),
            "objekte": {k: v for k, v in objekte.items() if v and k != "aus"},
            "ids": {k: v for k, v in ids.items() if v},
            "elemente": liste,
            "unbekannt": unbekannt, "gefuellt": gefuellt, "texte": texte, "draussen": draussen,
            "spannmittel": spannmittel, "stege": stege, "stege_seite": stege_seite,
            "stege_bearbeitung": stege_bearbeitung,
            "nullmarken": nullmarken, "nullmarken_kamera": nullmarken_kamera,
            "seiten": pages, "ids_seite": ids_seite, "platte": platte, "platten": n_platten,
            "vorrichtung_seite": vs, "reliefs": reliefs, "ohne_art": ohne_art,
            # zweiseitig: Seite 2 wird um die Grösse von Seite 1 gespiegelt – beide müssen gleich gross sein
            "seiten_ungleich": (len(pages) == 2 and (abs(pages[0]["breite"] - pages[1]["breite"]) > 0.01 or
                                                     abs(pages[0]["hoehe"] - pages[1]["hoehe"]) > 0.01))}


def _zahl(v: float) -> str:
    return ("%.1f" % v).rstrip("0").rstrip(".").replace(".", ",")


def hinweise(info: Dict) -> List[str]:
    w = []
    if info.get("seiten_ungleich"):
        s1, s2 = info["seiten"][0], info["seiten"][1]
        w.append("Seite 2 dieser Platte ist anders gross als Seite 1 ({} × {} statt {} × {} mm) – beim Wenden "
                 "passt sie so nicht. Beide Seiten gleich gross machen.".format(
                     _zahl(s2["breite"]), _zahl(s2["hoehe"]), _zahl(s1["breite"]), _zahl(s1["hoehe"])))
    n = info["draussen"]
    if n:
        w.append("{} ganz oder teilweise ausserhalb des Materials – bitte prüfen.".format(
            "1 Objekt liegt" if n == 1 else "%d Objekte liegen" % n))
    n = info["unbekannt"]
    if n:
        w.append("{} in anderer Farbe – wird nicht gefräst (nur die Fräsfarben zählen).".format(
            "1 Objekt" if n == 1 else "%d Objekte" % n))
    n = info.get("gefuellt", 0)
    if n:
        w.append("{} nur eine Füllung und keine Linie – wird nicht gefräst. Linienfarbe wählen.".format(
            "1 Objekt hat" if n == 1 else "%d Objekte haben" % n))
    n = info["texte"]
    if n:
        w.append("{} – zuerst Pfad › Objekt in Pfad umwandeln.".format(
            "1 Text wird nicht gefräst" if n == 1 else "%d Texte werden nicht gefräst" % n))
    return w
