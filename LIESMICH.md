# Hirch CAM

*English: [README.md](README.md)*

Freies CAM-Programm für CNC-Fräsen: zeichnen, Fräsarten über die Linienfarbe festlegen,
im Fräsen-Panel einstellen, in 3D simulieren und als G-Code für die eigene Maschine speichern.

Das Programm ist ein umgebautes Inkscape 1.4.4 (eigener Name, eigenes
Symbol, Menü «Fräsen», fest angedocktes Fräsen-Panel, nur die Werkzeuge,
die man zum Fräsen braucht). Die Werkzeugwege rechnet das **CAM-Modul `hirch_cam`**:
Teile aus FreeCAD CAM (libarea mit Clipper für Versatz und
Taschen, die Voronoi-Mittellinien der V-Carve) als kleine Python-Erweiterung
(1,6 MB) – ohne FreeCAD, OpenCASCADE oder Qt. Die übrige Geometrie rechnet
Hirch CAM selbst in Python.

## Installieren

Pakete für Linux und Windows gibt es auf [cam.hirch.net](https://cam.hirch.net). Unter Linux
(Mint 22 / Ubuntu 24.04) mit dem Programm-Archiv `hirch-cam-…-linux-x86_64.tar.xz` in `~/Downloads`:

```bash
./installieren.sh
```

Das installiert die nötigen Pakete (fragt nach dem Passwort), entpackt das Programm nach
`/opt/hirch-cam` und legt «Hirch CAM» ins Anwendungsmenü. Kontrolle: `./pruefen.sh`

Hirch CAM hat ein eigenes Profil (`~/.config/hirch-cam/profil`), ein
installiertes Inkscape bleibt unberührt.

## Arbeiten

1. **Material** (Panel rechts, «Ändern» oder Menü *Fräsen › Material*):
   Breite, Höhe, Dicke. Nullpunkt unten links, Z0 = Oberfläche.
2. Zeichnen (Auswählen, Knoten, Text, Rechteck, Kreis, Stern/Polygon,
   Linie/Kurve, Messen) oder
   importieren (*Datei › Importieren*: DXF, PDF, EPS/PS, AI, HPGL, SVG,
   Bilder …). DXF und HPGL behalten ihren Nullpunkt: er liegt auf der
   linken unteren Ecke des Materials. Dann die Linien mit der Palette unten
   einfärben (Klick = Linie, Umschalt+Klick = Füllung) – die Farbe ist die
   Fräsart:

   | Farbe | Fräsart |
   |---|---|
   | Rot | V-Carve (Flächen mit dem V-Fräser, Tiefe aus der Breite) |
   | Orange | Gravur auf der Linie |
   | Grün | Tasche (ausräumen, innere Formen = Inseln) |
   | Blau | Kontur (ausschneiden oder auf eine Tiefe fräsen, Haltestege) |
   | Violett | Bohren |
   | Grau | nicht fräsen (Hilfslinien) |

   Text kann Text bleiben: Hirch CAM wandelt ihn für die Rechnung selbst in
   Pfade um. Einlinienschriften (Hershey, EMS, Relief SingleLine, KiCad Newstroke und eigene)
   stehen direkt im Textwerkzeug, mit Abständen, die die Breite der Rille berücksichtigen
   ([docs/EINLINIENSCHRIFTEN.md](docs/EINLINIENSCHRIFTEN.md)).
3. Im Panel **Bearbeitungen** erscheint jede Fräsart, sobald es Linien in
   ihrer Farbe gibt. Doppelklick öffnet die Einstellungen (Werkzeug, Tiefe,
   Zustellung, Haltestege …), Ziehen ändert die Reihenfolge, «an» schaltet
   eine Bearbeitung aus, ein Klick wählt ihre Objekte in der Zeichnung.
   **Mehrere Bearbeitungen derselben Fräsart** (z. B. zwei Taschen mit
   verschiedener Tiefe): Objekte auswählen, «+ Neu» › Fräsart – die
   Auswahl bekommt einen eigenen Farbton (dunkler/heller) und damit eine
   eigene Bearbeitung. Rechtsklick auf eine Bearbeitung: Auswahl zuweisen,
   Objekte auswählen, weitere Bearbeitung dieser Art, Entfernen.
   Die Konturtiefe ist frei einstellbar; «Ganz durch» folgt der
   Materialdicke. Werkzeuge verwalten: *Fräsen › Werkzeuge*
   (Werkzeugnummer für den G-Code, Standard 1).
4. Hirch CAM rechnet nach jeder Änderung im Hintergrund; die Zeiten stehen
   gleich in der Liste, unten die **Gesamtzeit**.
5. **Vorschau 3D** zeigt die Simulation genau der Programme, die
   gespeichert werden. **G-Code speichern** schreibt eine Datei pro
   Werkzeug (`schild_1_V60.tap`, `schild_2_D3.tap`, … – in dieser
   Reihenfolge fräsen; Ausschneiden kommt immer zuletzt).

Die Einstellungen werden mit der Zeichnung gespeichert (SVG).

## Aufbau

```
fork/                      Umbau von Inkscape 1.4.4
  patches/NN-*.patch       Änderungen an Inkscape je Thema (docs/INKSCAPE-AENDERUNGEN.md)
  inkscape/                ganze Inkscape-Dateien (Menüs, Werkzeugleisten, Tastenkürzel)
  src/ui/dialog/hirch-fraesen.*  das Fräsen-Panel (C++, GTK 3)
  src/ui/dialog/hirch-vorschau.* die 3D-Vorschau (OpenGL 3.3 über GtkGLArea)
  share/icons/             Programmsymbol, Panel-Symbol
  bauen.sh, paket.sh       übersetzen, Archiv für /opt/hirch-cam packen
  laufzeit-pakete.txt      Pakete, die das Programm unter Mint 22 braucht
installieren.sh            Installation beim Anwender
pruefen.sh                 Einrichtung prüfen
hirch_cam/                 Rechenkern (Python)
  dokument.py              Schnittstelle zum Panel: analyse / rechnen
  einstellungen.py         Einstellungen in der Zeichnung (hirch:einstellungen)
  svg_lesen.py             Linien und Farben aus der Zeichnung lesen
  engine.py                Auftrag -> Werkzeugwege -> .tap + Bericht
  cam.py                   Anschluss an das CAM-Modul hirch_cam
  ops/                     Fräsarten: gravur, kontur, tasche, bohren, vcarve
  post/                    Postprozessoren: Vorlagen für RichAuto A11, WinCNC, LinuxCNC/Mach3,
                           Grbl (post/vorlagen/*.json) – eigene: docs/POSTPROZESSOR.md
  einlinien.py, schriften/ Einlinienschriften
cam_kern/                  CAM-Modul hirch_cam (C++): libarea aus FreeCAD CAM,
                           V-Carve-Mittellinien; bauen.sh, LIZENZEN.md
inkscape/                  Profil, Palette, Vorlage, Erweiterungen
tests/                     Tests und Testdaten
docs/                      Aufbau, Postprozessoren, Schnittdaten, Einlinienschriften
```

## Selbst bauen

```bash
fork/bauen.sh ~/bau      # Inkscape holen, Umbau einspielen, übersetzen
sh cam_kern/bauen.sh python3.12   # CAM-Modul (macht paket.sh sonst selbst)
fork/paket.sh ~/bau      # -> ~/bau/hirch-cam-VERSION-linux-x86_64.tar.xz
```

Benötigte Entwicklerpakete stehen oben in `fork/bauen.sh`.

## Tests

```bash
python3 -m pytest -q
```

Der Postprozessor wird Byte für Byte gegen Referenzdateien geprüft.
Tests des CAM-Moduls laufen nur, wenn es gebaut ist (`cam_kern/bauen.sh`).

## Lizenz

GPL-3.0-or-later (wie Inkscape als Ganzes), Text in [`LICENSE`](LICENSE). Herkunft und Lizenz
aller übernommenen Teile, Daten und der Hinweis, dass der Code mit Claude geschrieben wurde:
[`HERKUNFT.md`](HERKUNFT.md) (englisch: [`NOTICE.md`](NOTICE.md)).
