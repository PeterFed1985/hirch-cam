"""SVG lesen und Bögen suchen merken sich ihre Ergebnisse im Rechendienst (Fräsbahnen 25.10.2026).
Zweimal rechnen gibt denselben G-Code; gemerkte Linien sind dieselben wie frisch gerechnete."""
import glob
import os

from test_dokument import braucht_inkex

HIER = os.path.join(os.path.dirname(__file__), "referenz", "zeichnungen")


@braucht_inkex
def test_zweimal_rechnen_gleich(tmp_path, monkeypatch):
    from hirch_cam import dokument, speicher, svg_lesen
    from hirch_cam.post import boegen
    monkeypatch.setenv("HIRCH_SPRACHE", "de")
    for svg in sorted(glob.glob(os.path.join(HIER, "*.svg"))):
        svg_lesen._LINIEN_SPEICHER.clear()
        boegen._GEMERKT.clear()
        speicher.SPEICHER.leeren()
        texte = []
        for lauf in range(2):
            erg = dokument.rechnen(svg, str(tmp_path / f"{lauf}"), "teil")
            assert erg["ok"], (svg, erg.get("warnungen"))
            texte.append([open(d, newline="").read() for d in sorted(erg["dateien"])])
        assert texte[0] == texte[1], os.path.basename(svg)
        assert svg_lesen._LINIEN_SPEICHER, "nichts gemerkt"


@braucht_inkex
def test_gemerkte_linien_wie_frisch():
    """Je Dokument in derselben Reihenfolge lesen: frisch, mit Merken (leer) und aus dem Gemerkten
    gibt dieselben Punkte (auch mit der Eigenheit von inkex beim ersten composed_transform)."""
    from hirch_cam import dokument, svg_lesen

    def alle(svg_pfad, f):
        svg = dokument._laden(svg_pfad)
        svg_lesen.klone_aufloesen(svg)
        uu, h = svg.unittouu("1mm"), svg.viewbox_height
        return [f(el, uu, h, t) for el in svg_lesen.elemente(svg) for t in (0.05, 0.01)]

    for svg_pfad in sorted(glob.glob(os.path.join(HIER, "*.svg"))):
        svg_lesen._LINIEN_SPEICHER.clear()
        frisch = alle(svg_pfad, svg_lesen._linien)
        assert frisch
        assert alle(svg_pfad, svg_lesen.linien) == frisch          # Speicher leer: rechnen und merken
        assert alle(svg_pfad, svg_lesen.linien) == frisch          # aus dem Speicher


def test_kollisionspruefung_gemerkt_und_neu_bei_aenderung(tmp_path):
    from hirch_cam import kollision
    from hirch_cam.model import Job, Move, Toolpath
    from hirch_cam.post import tap_mm

    def schreiben(z):
        job = Job(toolpaths=[Toolpath(moves=[Move(True, 20, 20, 1), Move(True, 20, 20, z)])], safe_z=20.0)
        (tmp_path / "p.tap").write_text(tap_mm.write(job), newline="")

    def pruefen():
        prog = [{"datei": str(tmp_path / "p.tap"), "name": "T", "seite": 1, "durchmesser": 6.0, "form": "flach"}]
        return kollision.pruefen(prog, {"breite": 100, "hoehe": 60, "dicke": 10},
                                 lambda d: tap_mm.read(open(d, newline="").read()))
    kollision._GEPRUEFT.clear()
    schreiben(-2.0)
    erst = pruefen()
    assert erst and erst[0]["tief"] > 1.5          # Eilgang ins volle Material
    erst[0]["tief"] = 0                             # Kopie: der gemerkte Wert bleibt
    assert pruefen()[0]["tief"] > 1.5 and len(kollision._GEPRUEFT) == 1
    schreiben(0.5)                                  # Datei geändert: neu prüfen
    assert pruefen() == [] and len(kollision._GEPRUEFT) == 2
