# Lizenz und Herkunft

Hirch CAM steht unter der **GNU General Public License, Version 3 oder später**
(GPL-3.0-or-later), Text in [`LICENSE`](LICENSE).

Copyright © 2026 Peter Federspiel (GitHub: PeterFed1985) und die Mitwirkenden von Hirch CAM.

Englisch: [`NOTICE.md`](NOTICE.md).

Das Programm ist ein Umbau von Inkscape. Inkscape steht überwiegend unter GPL-2.0-or-later,
einzelne Teile unter (L)GPL-3.0-or-later; ein vollständiges Inkscape-Programm fällt darum
unter GPL-3.0-or-later. Hirch CAM übernimmt diese Lizenz für das Ganze.

## Mit Claude geschrieben

Der Quelltext von Hirch CAM ist in Zusammenarbeit mit **Claude** (KI-Assistent von
Anthropic) entstanden: Claude hat den grössten Teil des Codes, der Tests und der
Dokumentation geschrieben, Peter hat Ziele, Abläufe und Entscheidungen vorgegeben, an seiner
Maschine geprüft und freigegeben.

## Übernommene Teile

| Teil | Ort im Repository / im Paket | Herkunft | Lizenz |
|---|---|---|---|
| Inkscape 1.4 (Zeichenprogramm, Oberfläche) | `fork/` (Umbau als Patch), Paket `/opt/hirch-cam` | https://inkscape.org | GPL-2.0-or-later, Ganzes GPL-3.0-or-later; Einzelheiten in `COPYING` und `LICENSES/` von Inkscape |
| libarea (Area, Curve, Pocket …) | `cam_kern/libarea/` | Dan Heeks, aus FreeCAD `src/Mod/CAM/libarea` (Tag 1.0.2) | BSD-3-Clause |
| Clipper 6.2 | `cam_kern/libarea/clipper.*` | Angus Johnson | BSL-1.0 |
| Adaptive (adaptives Ausräumen) | `cam_kern/libarea/Adaptive.*` | Kresimir Tusek, aus FreeCAD | LGPL-2.1-or-later |
| V-Carve-Mittellinien | `cam_kern/modul.cpp` | Nachbau von FreeCAD `App/Voronoi.cpp` und `Path/Op/Vcarve.py` (Markus Lampert) | LGPL-2.1-or-later |
| Boost.Polygon (Voronoi), pybind11 | nur Header, beim Bauen | Boost; pybind11 | BSL-1.0; BSD-3-Clause |
| Einlinienschriften Hershey Sans/Serif/Script | `hirch_cam/schriften/einlinien.json` | Dr. A. V. Hershey, U.S. National Bureau of Standards (1967) | Hershey-Lizenz (frei mit Danksagung) |
| Einlinienschriften EMS Readability/Tech/Allure | `hirch_cam/schriften/einlinien.json` | Evil Mad Scientist Laboratories (Hershey Text) | OFL-1.1 |
| 3D-Rechenkern `hirch_relief` (eigener Abtaster, Abtragen, Mittellinien, STEP lesen) | `relief_kern/modul.cpp` | eigene Arbeit (Zweig `3d-fraesen`); Mittellinien nach Zhang–Suen (1984) | wie Hirch CAM (GPL-3.0-or-later) |
| OpenCAMLib (AdaptiveWaterline, Kugel-/Kegel-/Torusfräser, Abtasten zum Vergleich) | gelinkt von `relief_kern/modul.cpp`, im Paket `hirch_relief.*.so` | Anders Wallin u. a., https://github.com/aewallin/opencamlib (Paket `libopencamlib-dev`) | LGPL-2.1-or-later |
| Open CASCADE Technology (STEP lesen, Flächen vernetzen) | gelinkt von `relief_kern/modul.cpp` | Open Cascade SAS, https://dev.opencascade.org (Pakete `libocct-*-dev`) | LGPL-2.1 mit OCCT-Ausnahme |

Einzelheiten: [`cam_kern/LIZENZEN.md`](cam_kern/LIZENZEN.md),
[`hirch_cam/schriften/LIZENZEN.md`](hirch_cam/schriften/LIZENZEN.md).

Benutzt, aber nicht im Repository (beim Bauen aus den Paketquellen der Distribution):
GTK 3 / gtkmm, Boost, Python 3 mit numpy und shapely, OpenGL; für die 3D-Fräsung zusätzlich
scipy (BSD-3-Clause), OpenCAMLib und Open CASCADE (siehe Tabelle).

## Daten

| Daten | Herkunft |
|---|---|
| `tests/3d/Fluegel_Test.step`, `tests/3d/Fluegel_Test.stl` | von Peter in FreeCAD konstruiert (16.10.2026) und für die Tests freigegeben (`tests/3d/LIESMICH.md`) |
| `hirch_cam/daten/testteil_hartschaum.step` (Testteil, im Paket) | eigene Arbeit: erzeugt mit `beispiele/testteil_3d/testteil_erzeugen.cpp` (Open CASCADE) |
| `tests/daten/merkmale.step` | eigene Arbeit: erzeugt mit `tests/daten/merkmale_erzeugen.cpp` |
| `tests/zeichnungen/`, `tests/referenz/`, übrige Testdaten | für die Tests geschrieben bzw. von Hirch CAM selbst erzeugt |
| Postprozessor-Vorlagen `hirch_cam/post/vorlagen/` | eigene Arbeit (nach den Handbüchern der Steuerungen) |

Nicht im Repository und nie hineinzunehmen: Schriften, Projekte und andere Daten aus fremden
CAM- und Laserprogrammen.

Inkscape und FreeCAD sind Marken ihrer Projekte. Hirch CAM ist ein eigenständiges Projekt und steht
in keiner Verbindung zu ihnen.
