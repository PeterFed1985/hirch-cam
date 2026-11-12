#!/usr/bin/env python3
"""Inkscape-Erweiterung "Hirch CNC › Fräsen".

Liest alle Linien der Zeichnung (oder nur die Auswahl), ordnet sie über
ihre Farbe einer Fräsart zu und übergibt sie dem Rechenkern
(hirch_cam.engine). Der Rechenkern läuft mit dem System-Python (dort ist
Shapely installiert), nicht mit dem Python 3.8 aus dem Inkscape-AppImage.

Koordinaten: Nullpunkt = linke untere Ecke der Seite, Y nach oben,
Z0 = Materialoberfläche.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

_HIER = os.path.dirname(os.path.realpath(__file__))
_REPO = os.environ.get("HIRCH_CAM_REPO") or os.path.dirname(os.path.dirname(_HIER))
if _REPO not in sys.path:
    sys.path.insert(0, _REPO)

import inkex  # noqa: E402
from inkex import bezier  # noqa: E402

from hirch_cam.farben import FRAESARTEN, fraesart  # noqa: E402


def _farbe(elem):
    """Linienfarbe, sonst Füllfarbe, als (r, g, b) – oder None."""
    style = elem.specified_style()
    for key in ("stroke", "fill"):
        val = style.get(key)
        if val and val not in ("none", "transparent") and not str(val).startswith("url("):
            try:
                c = inkex.Color(val).to_rgb()
                return (int(c.red), int(c.green), int(c.blue))
            except Exception:
                continue
    return None


def system_python():
    """Python des Systems finden (nicht das aus dem AppImage)."""
    kandidaten = [os.environ.get("HIRCH_PYTHON"), "/usr/bin/python3", "/usr/local/bin/python3"]
    for k in kandidaten:
        if k and os.path.isfile(k) and os.access(k, os.X_OK):
            return k
    appdir = os.environ.get("APPDIR", "")
    for d in os.environ.get("PATH", "").split(os.pathsep):
        if appdir and d.startswith(appdir):
            continue
        k = shutil.which("python3", path=d)
        if k:
            return k
    return None


def _sauberes_env():
    """Umgebung ohne die AppImage-Einstellungen, damit das System-Python läuft."""
    env = dict(os.environ)
    for k in ("PYTHONHOME", "PYTHONPATH", "LD_LIBRARY_PATH", "PYTHONNOUSERSITE",
              "GDK_PIXBUF_MODULE_FILE", "GI_TYPELIB_PATH", "GIO_MODULE_DIR"):
        env.pop(k, None)
    env["PYTHONPATH"] = _REPO
    env["PYTHONIOENCODING"] = "utf-8"
    return env


def materialdicke(svg, standard=18.0):
    """Materialdicke aus der Zeichnung (gesetzt mit «Material einrichten»)."""
    try:
        return float(svg.get("{https://hirch.net/cnc}materialdicke"))
    except (TypeError, ValueError):
        return standard


class Fraesen(inkex.EffectExtension):
    def add_arguments(self, pars):
        a = pars.add_argument
        a("--reiter")
        for x, typ, d in (("A", "v", 12.0), ("B", "schaft", 3.0), ("C", "schaft", 6.0)):
            a("--w%s_typ" % x, default=typ)
            a("--w%s_durchmesser" % x, type=float, default=d)
            a("--w%s_winkel" % x, type=int, default=60)
            a("--w%s_drehzahl" % x, type=int, default=18000)
            a("--w%s_vorschub" % x, type=int, default=1000)
            a("--w%s_eintauchen" % x, type=int, default=300)
            a("--w%s_nummer" % x, type=int, default=1)
        a("--vcarve_werkzeug", default="A")
        a("--vcarve_max_tiefe", type=float, default=5.0)
        a("--vcarve_flach_werkzeug", default="B")
        a("--gravur_werkzeug", default="A")
        a("--gravur_tiefe", type=float, default=0.5)
        a("--gravur_zustellung", type=float, default=0.5)
        a("--tasche_werkzeug", default="B")
        a("--tasche_tiefe", type=float, default=3.0)
        a("--tasche_zustellung", type=float, default=1.5)
        a("--tasche_bahnabstand", type=int, default=40)
        a("--kontur_werkzeug", default="C")
        a("--kontur_tiefe", type=float, default=6.0)
        a("--kontur_zustellung", type=float, default=2.0)
        a("--kontur_seite", default="auto")
        a("--kontur_richtung", default="gleichlauf")
        a("--stege_anzahl", type=int, default=4)
        a("--stege_breite", type=float, default=6.0)
        a("--stege_hoehe", type=float, default=1.5)
        a("--bohren_werkzeug", default="B")
        a("--bohren_tiefe", type=float, default=5.0)
        a("--bohren_zustellung", type=float, default=2.5)
        a("--freifahrhoehe", type=float, default=3.0)
        a("--startabstand", type=float, default=0.5)
        a("--sicherheitshoehe", type=float, default=20.0)
        a("--postprozessor", default="vcarve_mm")
        a("--datei", default="")
        a("--toleranz", type=float, default=0.01)
        a("--vorschau", type=inkex.Boolean, default=True)

    def _werkzeuge(self):
        o = vars(self.options)
        w = {}
        for x in "ABC":
            w[x] = {"typ": o["w%s_typ" % x], "durchmesser": o["w%s_durchmesser" % x],
                    "v_winkel": o["w%s_winkel" % x], "drehzahl": o["w%s_drehzahl" % x],
                    "vorschub": o["w%s_vorschub" % x], "eintauchen": o["w%s_eintauchen" % x],
                    "nummer": o["w%s_nummer" % x]}
        return w

    # -- Geometrie --------------------------------------------------------
    def _linien(self, elem, uu_pro_mm, hoehe_uu):
        """Pfad eines Elements als Polylinien in mm (Maschinenkoordinaten).
        Liefert Liste von (punkte, geschlossen)."""
        path = elem.path.to_absolute().transform(elem.composed_transform())
        ergebnis = []
        # Teilpfade einzeln, um "geschlossen" (Z) sicher zu erkennen
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
        tol_uu = self.options.toleranz * uu_pro_mm
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

    def _elemente(self):
        quelle = list(self.svg.selection.values()) if self.svg.selection else [self.svg]
        for wurzel in quelle:
            for el in wurzel.descendants():
                if not isinstance(el, inkex.ShapeElement):
                    continue
                if isinstance(el, (inkex.Group, inkex.TextElement, inkex.Tspan,
                                   inkex.Image, inkex.Use)):
                    continue
                if not hasattr(el, "path"):
                    continue
                if el.specified_style().get("display") == "none":
                    continue
                yield el

    def _zieldatei(self):
        if self.options.datei:
            return os.path.expanduser(self.options.datei)
        doc = self.document_path()
        if doc:
            return os.path.splitext(doc)[0] + ".tap"
        return os.path.expanduser("~/hirch-cam.tap")

    # -- Hauptteil --------------------------------------------------------
    def effect(self):
        o = self.options
        uu_pro_mm = self.svg.unittouu("1mm")
        hoehe_uu = self.svg.viewbox_height
        self._dicke = materialdicke(self.svg)
        self._seite = (self.svg.viewbox_width / uu_pro_mm, hoehe_uu / uu_pro_mm)

        objekte = {k: [] for k in FRAESARTEN}
        unbekannt = 0
        for el in self._elemente():
            farbe = _farbe(el)
            art = fraesart(farbe) if farbe else None
            if art is None:
                unbekannt += 1
                continue
            if art == "aus":
                continue
            for pts, zu in self._linien(el, uu_pro_mm, hoehe_uu):
                objekte[art].append({"punkte": pts, "geschlossen": zu})
        texte = sum(1 for el in (self.svg.selection.values() if self.svg.selection
                                 else self.svg.descendants())
                    if isinstance(el, inkex.TextElement))

        auftrag = {
            "werkzeuge": self._werkzeuge(),
            "hoehen": {"freifahrhoehe": o.freifahrhoehe, "startabstand": o.startabstand,
                       "sicherheitshoehe": o.sicherheitshoehe,
                       "materialdicke": self._dicke},
            "gravur": {"werkzeug": o.gravur_werkzeug, "tiefe": o.gravur_tiefe,
                       "zustellung": o.gravur_zustellung},
            "kontur": {"werkzeug": o.kontur_werkzeug, "tiefe": o.kontur_tiefe,
                       "zustellung": o.kontur_zustellung, "seite": o.kontur_seite,
                       "gleichlauf": o.kontur_richtung == "gleichlauf",
                       "stege_anzahl": o.stege_anzahl, "stege_breite": o.stege_breite,
                       "stege_hoehe": o.stege_hoehe},
            "tasche": {"werkzeug": o.tasche_werkzeug, "tiefe": o.tasche_tiefe,
                       "zustellung": o.tasche_zustellung, "bahnabstand": o.tasche_bahnabstand},
            "bohren": {"werkzeug": o.bohren_werkzeug, "tiefe": o.bohren_tiefe,
                       "zustellung": o.bohren_zustellung},
            "vcarve": {"werkzeug": o.vcarve_werkzeug, "max_tiefe": o.vcarve_max_tiefe,
                       "flach_werkzeug": o.vcarve_flach_werkzeug},
            "toleranz": o.toleranz,
            "postprozessor": o.postprozessor,
            "ausgabe": self._zieldatei(),
            "objekte": {k: v for k, v in objekte.items() if v and k != "aus"},
        }

        bericht, warnungen = [], []
        breite_mm, hoehe_mm = self._seite
        tol = 0.05
        draussen = sum(
            1 for liste in objekte.values() for ob in liste
            if any(x < -tol or y < -tol or x > breite_mm + tol or y > hoehe_mm + tol
                   for x, y in ob["punkte"]))
        if draussen:
            warnungen.append("{} {} ganz oder teilweise ausserhalb des Materials (Seite) – "
                             "bitte prüfen.".format(draussen,
                                                    "Linie liegt" if draussen == 1 else "Linien liegen"))
        if unbekannt:
            warnungen.append("{} {} in anderer Farbe ausgelassen (es zählt nur die Palette "
                             "«Hirch CNC Fräsfarben»).".format(
                                 unbekannt, "Objekt" if unbekannt == 1 else "Objekte"))
        if texte:
            warnungen.append("{} Text-{} ausgelassen: zuerst Pfad › Objekt in Pfad "
                             "umwandeln.".format(texte, "Objekt" if texte == 1 else "Objekte"))
        if not auftrag["objekte"]:
            warnungen.insert(0, "Keine Linien in den Fräsfarben gefunden – keine Datei "
                                "geschrieben.")
            self.msg("\n".join(warnungen))
            return
        ergebnis = self._rechnen(auftrag)
        bericht = ergebnis.get("bericht", [])
        warnungen = ergebnis.get("warnungen", []) + warnungen
        vorschau = bool(ergebnis.get("ok")) and o.vorschau
        if vorschau:
            self._vorschau(ergebnis.get("programme", []), auftrag, bericht, warnungen)
        # Bei Erfolg ohne Warnungen schweigt Inkscape: der Bericht steht in der
        # Vorschau. Sonst (Warnung, Fehler, keine Vorschau) eine Meldung zeigen.
        if warnungen or not vorschau:
            teile = []
            if warnungen:
                teile += ["Bitte beachten:"] + ["• " + w for w in warnungen] + [""]
            teile += bericht
            self.msg("\n".join(teile).strip())

    def _rechnen(self, auftrag):
        py = system_python()
        if not py:
            return {"ok": False, "bericht": ["System-Python nicht gefunden (/usr/bin/python3)."]}
        if os.environ.get("HIRCH_AUFTRAG_KOPIE"):  # für Tests: Auftrag mitschreiben
            with open(os.environ["HIRCH_AUFTRAG_KOPIE"], "w", encoding="utf-8") as fh:
                json.dump(auftrag, fh)
        fd, pfad = tempfile.mkstemp(prefix="hirch-auftrag-", suffix=".json")
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            json.dump(auftrag, fh)
        try:
            r = subprocess.run([py, "-m", "hirch_cam.engine", pfad], cwd=_REPO,
                               env=_sauberes_env(), capture_output=True, timeout=600)
        finally:
            os.unlink(pfad)
        try:
            return json.loads(r.stdout.decode("utf-8"))
        except Exception:
            fehler = (r.stderr or b"").decode("utf-8", "replace")[-1500:]
            return {"ok": False, "bericht": ["Rechenkern ist abgebrochen:", fehler]}

    def _vorschau(self, programme, auftrag, bericht, warnungen):
        """3D-Vorschau im Browser öffnen, mit Bericht (alle Dateien nacheinander)."""
        py = system_python()
        if not py or not programme:
            return
        fd, info = tempfile.mkstemp(prefix="hirch-bericht-", suffix=".json")
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            json.dump({"bericht": bericht, "warnungen": warnungen}, fh, ensure_ascii=False)
        args = [py, "-m", "hirch_cam.vorschau"]
        for p in programme:
            args += ["--programm", "%s|%s|%s|%s|%s" % (
                p["datei"], p["form"], p["durchmesser"], p["v_winkel"], p["name"])]
        args += ["--dicke", str(auftrag["hoehen"]["materialdicke"]),
                 "--breite", str(self._seite[0]), "--hoehe", str(self._seite[1]),
                 "--bericht", info, "--oeffnen"]
        try:
            proc = subprocess.Popen(args, cwd=_REPO, env=_sauberes_env(), start_new_session=True,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            # Die Vorschau läuft eigenständig weiter; nicht auf sie warten
            # (sonst meldet Python beim Beenden eine ResourceWarning).
            proc.returncode = 0
        except Exception:
            pass


if __name__ == "__main__":
    Fraesen().run()
