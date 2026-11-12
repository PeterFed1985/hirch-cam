# Hirch CNC – Aufbau

Kurz und aktuell: was wo liegt, wie gebaut, übersetzt und geprüft wird. Die
Entstehungsgeschichte mit allen Entscheidungen steht in `UEBERGABE.md`. Richtlinien für die Oberfläche (Aufbau, Farben, Texte, Mockups): `GESTALTUNG.md`.

## Teile

| Teil | Ort | Aufgabe |
|---|---|---|
| Oberfläche | Inkscape 1.4.4, umgebaut über `fork/patches/NN-*.patch`, `fork/inkscape/` und `fork/src/` (docs/INKSCAPE-AENDERUNGEN.md) | Zeichnen, Fräsen-Panel, Leiste, Menü «Fräsen» |
| Rechenkern | `hirch_cam/` (Python) | Zeichnung lesen, Werkzeugwege, Prüfung, G-Code |
| CAM-Modul | `cam_kern/` → `hirch_cam/hirch_cam.*.so` | Versatz, Taschen (Adaptive), V-Carve-Mittellinien aus FreeCAD CAM |
| 3D-Vorschau | `fork/src/ui/dialog/hirch-vorschau.cpp` | OpenGL-Simulation der Programme |
| 3D-Modul (Zweig `3d-fraesen`) | `relief_kern/` → `hirch_cam/hirch_relief.*.so` | Höhenkarten aus Dreiecksnetzen, Umrisse (Marching Squares), STEP lesen (OpenCASCADE), Drop-Cutter-Abtastung (OpenCAMLib), Abtrag auf einem Höhenraster (Simulation) |

Das Panel ruft den Rechenkern als eigenen Prozess: `python3 -m hirch_cam.dokument
BEFEHL …` (analyse, rechnen, vorschau, nesting, aufraeumen, crv, gravur, schrift,
datenbank, post). Ergebnis ist immer ein JSON auf stdout.

## Oberfläche (`fork/src/ui/dialog/`)

| Datei | Inhalt |
|---|---|
| `hirch-fraesen.h` | öffentliche Klassen: `HirchFraesen` (Panel), `HirchAnsicht` (Reiter Einstellungen), `HirchWerkzeuge` (alter Reiter, wird nicht mehr geöffnet), `HirchFarbleiste` |
| `hirch-intern.h` | gemeinsame Hilfen (JSON, Zahlen, Widgets, Skizzen, Fräsarten `ARTEN`) und die Deklaration von `HirchFraesen::Impl` |
| `hirch-panel.cpp` | Aufbau, Dokument beobachten, Analyse/Rechnen starten, Fräsbahnen, Ablauf-Liste, Ziehen, Ändern |
| `hirch-bearbeitung.cpp` | Bearbeitungsdialog aller Fräsarten, Vorschub, Live-Vorschau |
| `hirch-gravur.cpp` | Gravurtext, Schriften |
| `hirch-dialoge.cpp` | Postprozessoren, Werkzeuge, Auftrag einrichten, Spannmittel, Stege |
| `hirch-ausgabe.cpp` | G-Code speichern, USB-Stick, Vorlagen, Fassungen, Maschine, Wiederaufnahme, Laufzeit |
| `hirch-import.cpp` | Spannvorrichtung, Zeichnung aufräumen, Vervielfältigen/Verschachteln |
| `hirch-leiste.cpp` | schwebende Werkzeugleiste, Hover-Kärtchen (`hover_karte`) |
| `hirch-einstellungen.cpp` | Reiter «Einstellungen» (Darstellung, Rechnen, Import, Schriften, Maschine, Sprache) |
| `hirch-sprache.cpp/.h` | `TR()`, `TRF()`, Katalog |
| `hirch-fraeserbild.h` | Fräserbilder der Werkzeugwahl (flach, Variante B): aus Art, Schneiden, Drall, Wendeplatten, Spanbrecher gezeichnet |
| `hirch-aktionen.cpp` | Fenster-Aktionen (Menü «Fräsen», nach Themen; `hirch-neu`, `hirch-einstellungen` mit Ziel), Willkommen, Fassungen-Ordner |
| `hirch-messkorrektur.cpp` | Messkorrektur: Testteil in X und Y ausmessen (Rechnung: `hirch_cam/messkorrektur.py`) |
| `hirch-rille.cpp` | Rille der Gravuren auf der Zeichenfläche, Zuschlag der Einlinientexte |
| `hirch-fraesen.cpp` | Konstruktoren, öffentliche Methoden, Boost.JSON (einziges `src.hpp`) |
| `hirch-test.cpp` | Oberflächentest (nur mit `HIRCH_OBERFLAECHENTEST`) |
| `hirch-vorschau.cpp/.h` | 3D-Vorschau |
| `hirch-3d.cpp/.h` | 3D-Fräsung (Zweig `3d-fraesen`): Dialog «3D-Modell einlesen», Reiter «3D» (`Hirch3D`, OpenGL-Ansicht, Höhe, Ausrichtung), Dialog der Bearbeitungen 3D-Schruppen/3D-Schlichten (`relief_dialog`), «Spindel und Halterung» (`halterung_dialog`) |

Neue Dateien unter `fork/src/` verlinkt `fork/bauen.sh` selbst in den
Inkscape-Quellbaum; eine neue `.cpp` muss zusätzlich in `src/ui/CMakeLists.txt`
(im Patch) stehen.

## Rechenkern (`hirch_cam/`)

- `dokument.py` – Befehle für das Panel; übersetzt die JSON-Ausgabe (`sprache.ausgabe`).
- `svg_lesen.py` – Zeichnung lesen (Farben → Fräsart, Platten = Seiten, Klone, Text als Pfad).
- `einstellungen.py` – Einstellungen in der Zeichnung (`hirch:einstellungen`), Objekte zuordnen
  (gleiche Fräsart, nächste Farbe, `hirch:auch`, Duplikate mit gleicher Farbe).
- `engine.py` – Auftrag rechnen: Reihenfolge (`ablauf.py`), je Bearbeitung die Werkzeugwege
  (`ops/`), Prüfung (`pruefung.py`, `kollision.py`), Postprozessor (`post/`; Vorlagen mit Fähigkeiten der Steuerung, siehe `docs/POSTPROZESSOR.md`).
- `nesting.py`, `aufraeumen.py`, `gravurtext.py`, `schriftimport.py`,
  `werkzeugdb.py` – je ein Befehl des Panels.
- `relief_import.py` (STL, OBJ, STEP, Graustufenbild → `Modell`), `relief.py` (Ausrichtung,
  Höhenkarten oben/unten, Kennzahlen, graues Schattenbild, Befehl `relief`) – 3D-Fräsung.
  `relief_svg.py` (eingebettete Modelle aus der Zeichnung lesen), `ops/relief3d.py`
  (`relief_schruppen`: Z-Ebenen mit Aufmass; `relief_schlichten`: parallele Bahnen, Höhe je Punkt
  aus OpenCAMLib), `ops/relief_zweiseitig.py` (zweiseitige Modelle: Trennfläche, Haltestege
  `hirch:steg3d`, Seite 2 gewendet; Befehl `relief stege` für den Reiter «3D»),
  `ops/relief_vektoren.py` (Umriss, Höhenlinie, STEP-Kanten; Befehl `relief vektoren`),
  `ops/relief_projektion.py` (Tiefe ab der Oberfläche des 3D-Modells, `tiefe_ab`),
  `ops/relief_halterung.py` (streifen Schaft, Spannmutter, Spindel oder Halterung das Material? –
  bricht die Rechnung ab, Ergebnis `kollisionen`), `ops/relief_ergebnis.py` (Fräsergebnis für den
  Reiter «3D»: Abtrag aller Programme, Vergleich mit dem Modell; Befehl `relief ergebnis`),
  `ops/relief_speicher.py` (Zwischenspeicher der 3D-Bahnen unter `~/.cache/hirch-cam/3d-bahnen`;
  mehrere neue 3D-Bearbeitungen gleichzeitig in Kindprozessen).
- `sprache.py` + `sprache/en.json` – Englisch.

## Wo steht was

Schnelle Karte für Änderungen (Stand 19.10.2026). Funktionen von `HirchFraesen::Impl` ohne Präfix.

| Thema | Oberfläche (C++) | Rechenkern (Python) |
|---|---|---|
| Rechnen anstossen | `hirch-panel.cpp`: `spaeter` → `auffrischen` (Analyse) → `rechnen`; `rechnen_jetzt` | `dokument.py` `analyse`/`rechnen`, `engine.rechnen` |
| Ablauf-Liste, eine Zeile | `hirch-panel.cpp`: `anzeigen`, `op_zeile` (Schnittbild `schnitt_zeichnen`, Auge, Haken) | – |
| Ziehen im Ablauf | `hirch-panel.cpp`: `ziehbar`, `ziehen_starten`/`_bewegen`/`_beenden`, Zustand `zg` | – |
| «+ Neu» | `hirch-panel.cpp`: `neu` → `neu_dialog` (Dialog zuerst, gerechnet nach «Übernehmen») | – |
| Auswahl zuweisen | `hirch-panel.cpp`: `zuweisen`, Symbol in `op_zeile`, `zuweisen_auffrischen` (bei Auswahl), Rechtsklick `kontextmenue` | `einstellungen.zuordnen` |
| Bearbeitungsdialoge | `hirch-bearbeitung.cpp`: `bearbeitung_dialog` (alle), `tasche_dialog`; 3D: `hirch-3d.cpp` `relief_dialog`; Sofort-Vorschau `live_rechnen` | `dokument.vorschau` |
| Werkzeugwahl in Bearbeitungen | `hirch-panel.cpp`: `werkzeug_combo`, `werkzeug_combo_alle`; Klick aufs Feld `werkzeug_aufklappen` → `werkzeuge_dialog(…, WzWahl)`; «passend» `wz_passend` (je `wz_kontext`), Kartengruppe `fraeser_gruppe` (`hirch-intern.h`) | – |
| Fräserbilder (Karten) | `hirch-fraeserbild.h` (`bauen`, `zeichnen`, SVG-Pfadleser für Cairo); Merkmale aus dem Fräser: `fraeser_merkmale` (`hirch-intern.h`) | Felder `drall`, `wendeplatten`, `spanbrecher` (`werkzeugdb.py`) |
| Werkzeugdialog (wählen und verwalten, «Meine Werkzeuge») | `hirch-dialoge.cpp`: `werkzeuge_dialog` – links Suche, Art-Karten (`arten_fuellen`), Zeilen (`liste_fuellen`), rechts Felder, Prüfhinweise `pruef_hinweis`, Laufzeit «Neu eingesetzt»; Plätze ohne Bearbeitung fallen beim Übernehmen weg (`aufraeumen`, aus Ablauf/Menü) | `werkzeugdb.py` |
| Welche Inkscape-Dialoge es gibt (Panel-Menü) | Patch: `src/ui/dialog/dialog-data.cpp` (Liste), `dialog-container.cpp` `dialog_factory` (gesperrte Typen) | – |
| Werkzeug-Kopfzeile im Ablauf (T1 …) | `hirch-panel.cpp`: `gruppe(chip, titel, rechts, slot)` – Schild, Fräserbild, Stift öffnet `werkzeuge_dialog(slot)` | `ablauf.py` (`slot`) |
| Werkzeugskizze, Übergang, Ausspannlänge | `hirch-intern.h`: `werkzeug_skizze`, `werkzeug_uebergang`, `werkzeug_ausspann` | `ops/relief_halterung.py`: `uebergang`, `ausspannlaenge`, `koerper` |
| Fräsbahnen auf der Zeichnung (2D) | `hirch-panel.cpp`: `bahnen_zeichnen` | `engine._bahnen_2d` |
| V-Carve | – | `ops/vcarve.py`, `cam.mittellinien` (C++: `cam_kern/modul.cpp`) |
| Auftrag einrichten (Material, Nullpunkt, Ausgabe) | `hirch-dialoge.cpp`: `material_dialog` | `einstellungen.py` |
| Maschine, Steuerung (Postprozessor) | `hirch-ausgabe.cpp`: `maschine_abschnitt` (gilt für alle Zeichnungen) | `einstellungen.laden` (Maschine vor Zeichnung), `post/` |
| G-Code speichern, USB-Stick | `hirch-ausgabe.cpp`: `speichern`, `auf_stick`, `pruefliste_ok`, `gespeichert_dialog`, `luftfraesen`, Kurznamen `kurz_stamm`/`ziel_vorschlag` | `engine` (`luft`, `kurzname`), `post/` (`kurzname`, `grossbuchstaben`, `eine_datei`) |
| Zustellungen mit Art | `hirch-intern.h`: `ZustellArt`, `zustell_stufen`; `hirch-bearbeitung.cpp`: `zustell_art_zeilen` | `ops/bahn.py`: `Zustellung`, `zustellung_aus`, `zustellungen` |
| Werkzeugleiste oben | Patch: `desktop-widget.cpp` (Rückgängig/Wiederholen), `toolbar-select.ui` + `select-toolbar.cpp` (Menü «Auswahl», Drehen-Symbole) | – |
| Englische Texte | `TR`/`TRF` (`hirch-sprache.cpp`) | `sprache/en.json` (Test `test_katalog.py`) |
| Oberflächentest | `hirch-test.cpp` (Schritte `schritt(…)`) | `tests/test_oberflaeche.py` |
| 3D: Reiter «3D» (Modell, Fräsergebnis, Zweiseitig, Stege, Vektoren) | `hirch-3d.cpp`: `Hirch3D::Impl`, 3D-Ansicht `ansicht` (Ziehen dreht, Rad zoomt, Doppelklick zurück); Daten in `Szene3D` (eine je Fenster, `szenen()`), jede `Ansicht` zeigt sie mit eigener Kamera | `relief.py` (Modell, Lage), `ops/relief_zweiseitig.py`, `ops/relief_vektoren.py` |
| 3D: Modell einlesen, Anfangsdialog «3D-Modell»/«Testteil laden», Nullpunkt im Block | `hirch-3d.cpp`: `Einlesen`, `einlesenDatei`, `hirch_3d_masse`, `nullpunkt_block`; `hirch-dialoge.cpp`: `material_dialog` (`waehlen_fertig`, `modell_rechnen`) | `relief_import.py` (STL/OBJ/STEP/Bild), Testteil `hirch_cam/daten/` |
| 3D: Bearbeitungen anlegen, Dialog 3D-Schruppen/-Schlichten | `hirch-3d.cpp`: `relief_anlegen`, `relief_dialog` | `ops/relief3d.py`: `bearbeiten` → `schruppen`/`schlichten` |
| 3D: Abtasten, Schaft, Restmaterial, rundes Material | – | `relief3d._abtaster` (C++ `relief_kern` `SchnellAbtaster`), `_schaft`, `_restflaeche`, `_bereich` (Kreis) |
| 3D: Bohrungen und Taschen aus STEP | `hirch-3d.cpp` (Dialog «Bohrungen und Taschen»), `bearbeitungenAnlegen` | `ops/relief_merkmale.py` |
| 3D: Fräsergebnis, Klick ins Ergebnis, Karte «zuletzt gefräst von» | `hirch-3d.cpp`: `rechnung_neu`, `treffen` | `ops/relief_ergebnis.py` (`abtragen_seite`, `MINDESTTIEFE`) |
| 3D-Ansicht im Hauptfenster (Umschalter 2D \| 3D: Platz im Fenster Basis) | `hirch-3d.cpp`: `hirch_ansicht_3d_neu(SPDesktop*)` (`AnsichtFenster`, dieselbe `Szene3D` wie der Reiter; gefrästes Teil aus `HirchAbtrag`: `teil_bewegungen`, `teil_bauen`, `gewinde_bauen`; gezeichnet von `TeilGL` (CDLOD-Kacheln direkt aus den Höhentexturen); Abspielleiste `ablauf_bauen`/`stelle_rechnen`; Daten je Fenster `teile()` aus `kollisionen_zeichnen` ← `vorschau_daten()` in hirch-ausgabe.cpp) | – |
| 3D: Flächen wählen (Klick/Umschalt+Klick, blau, Seite 2), «Gewählte Flächen als Bereich» | `hirch-3d.cpp`: `flaechen_bauen`, `flaeche_holen`, `flaechen_vereinigen`, `Ansicht::hervorheben`; `HirchFraesen::bereichSetzen` | `ops/relief_flaeche.py` (`flaeche`, `vereinigen`), `relief3d.begrenzung` («flaechen»/«ohne_flaechen»), `svg_lesen` (`hirch:flaechen`) |
| 3D: Spindel/Halterung, Kollision | `hirch-3d.cpp`: `halterung_dialog`, `kollision_zeigen` | `ops/relief_halterung.py` (`koerper`, `pruefen`) |
| 3D: Zwischenspeicher der Bahnen | – | `ops/relief_speicher.py` (`AUFTRAG_TEILE`, `schluessel`) |
| 3D-Rechenkern (C++) | – | `relief_kern/modul.cpp` (Abtaster, Abtragen, Mittellinien, STEP, OpenCAMLib), bauen mit `relief_kern/bauen.sh` |

## Sprache

Quelltext deutsch. `TR("…")` (C++) und `sprache.tr()` / `sprache.ausgabe()` (Python)
übersetzen über `hirch_cam/sprache/en.json`. Sätze mit Namen oder Zahlen als Ganzes:
`TRF("Auf «{}» speichern", {name})`. Muster mit `{}` passen auch auf fertige Texte des
Rechenkerns. Sprache: Einstellungen › Sprache (`/hirch/sprache`, setzt auch `/ui/language`
für die Inkscape-Menüs; deren deutsche Texte in `po/de.po`). Daten aus der Zeichnung werden
nie übersetzt. Test `tests/test_katalog.py` meldet Texte ohne Eintrag.

## Bauen und liefern

```bash
fork/bauen.sh                         # Inkscape holen, Patch, Links, cmake (einmal)
cd ~/bau/build && ninja               # übersetzen
cam_kern/bauen.sh python3.12          # CAM-Modul (nur nach Änderungen in cam_kern/)
relief_kern/bauen.sh python3.12       # 3D-Modul (STEP nur mit libocct-*-dev, OpenCAMLib mit libopencamlib-dev)
fork/paket.sh ~/bau                   # Archiv für /opt/hirch-cam
```

Patch neu schreiben nach Änderungen im Inkscape-Baum:
`sh ~/hirch-cam/fork/patch-schreiben.sh ~/bau/inkscape-src` (schreibt die Teil-Patches und holt die ganzen Dateien zurück).
`po/` (Übersetzungen) ist ein Git-Untermodul und steht darum in einem eigenen Patch:
`cd ~/bau/inkscape-src/po && git diff > ~/hirch-cam/fork/patches/po-de.patch` (Hirch-Menüs:
englische msgids in `menus.ui`, deutsch in `po/de.po`).

## Prüfen

```bash
python3 -m pytest -q                  # alle Tests (inkex aus /opt/hirch-cam wird gefunden)
HIRCH_REFERENZ_NEU=1 python3 -m pytest tests/test_referenz.py   # G-Code-Referenzen neu
```

`tests/test_oberflaeche.py` startet das installierte Hirch CNC (`/opt/hirch-cam`, sonst
`HIRCH_CAM_BIN`) unter Xvfb auf Deutsch und Englisch. Mit `HIRCH_OBERFLAECHENTEST=bericht.json`
öffnet das Programm selbst alle Dialoge des Menüs «Fräsen», jede Bearbeitung und die Reiter,
liest alle Texte, schliesst wieder und schreibt einen Bericht (`hirch-test.cpp`). Der Test
prüft: kein Absturz, jeder Dialog geht auf, und in der englischen Oberfläche steht kein
deutscher Text (Ausnahmen: `tests/oberflaeche_erlaubt.txt`, z. B. Werkzeugnamen aus der
Datenbank). Etwa 80 s; überspringen mit `HIRCH_OHNE_OBERFLAECHE=1`. Also: nach dem Bauen
erst installieren (bzw. die Bibliothek nach `/opt/hirch-cam/lib/inkscape/` kopieren).

`tests/test_referenz.py` rechnet die Zeichnungen in `tests/referenz/zeichnungen/` mit beiden
Rechenkernen und vergleicht jede Zeile mit `tests/referenz/erwartet/`. Ändert sich dort etwas,
ist entweder ein Fehler passiert oder die Änderung gewollt – dann Referenzen neu schreiben und
den Unterschied mit `git diff` ansehen. Der G-Code ist reproduzierbar (Adaptive startet seinen
Zufallsgenerator je Tasche neu, die Wegsortierung hat eine feste Rechenmenge).

## Windows

Die Windows-Variante (Bau über GitHub Actions, Installer, Windows-7-Anpassungen) ist am
15.10.2026 aus dem Repo genommen worden: weiterentwickelt wird nur Linux. Der letzte Stand
liegt in der Git-Geschichte (`git log -- fork/windows`, Commit vor `Windows-Variante entfernt`).
Die `#ifdef _WIN32`-Stellen im Quelltext sind geblieben – sie stören unter Linux nicht und
erleichtern einen späteren Windows-Bau.
