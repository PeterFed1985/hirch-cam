# Änderungen an Inkscape 1.4.4

Hirch CNC baut auf Inkscape 1.4.4 (Tag `INKSCAPE_1_4_4`). Was an Inkscape selbst geändert ist, steht an drei
Stellen; `fork/einspielen.sh <Quellordner>` spielt alles ein (Linux `fork/bauen.sh`, Windows `fork/windows/bauen.sh`).
Nach einer Änderung im Inkscape-Baum: `sh fork/patch-schreiben.sh <Quellordner>` schreibt die Teil-Patches neu (jede
geänderte Datei gehört zu genau einem Thema; neue Dateien dort eintragen).

Beim Wechsel auf eine neue Inkscape-Fassung: je Thema prüfen, ob es noch nötig ist (Spalte «warum»), die ganzen
Dateien unter `fork/inkscape/` gegen die neuen Inkscape-Dateien vergleichen und von Hand nachführen.

## 1. Eigene Dateien (`fork/src/`, im Baum verlinkt bzw. kopiert)

Die eigentliche Logik: Panel, Dialoge, Leiste, 3D, Sprache … (`fork/src/ui/dialog/hirch-*`), dazu die Teile von
Inkscape-Klassen, die Hirch ergänzt (seit 29.10.2026 aus dem Patch hierher verschoben, im Patch bleiben die Aufrufe):
`ui/toolbar/hirch-select-toolbar.cpp` (Bezugspunkt, Drehwinkel), `ui/toolbar/hirch-arc-toolbar.cpp` (Radius,
Kreis anlegen), `libnrtype/hirch-font-lister.cpp` (Einlinienschriften in der Schriftliste).

**Zentrale Liste «erlaubt»** (`ui/dialog/hirch-erlaubt.{h,cpp}`, 07.11.2026): welche Inkscape-Fenster, Werkzeuge,
Pfadeffekte, Dateitypen beim Speichern und Seiten der Erweiterten Einstellungen erreichbar sind. Die Patches fragen
nur diese Liste (Fenster-Fabrik und Menü ▾, `tool_switch` und Werkzeugleiste, Liste der Pfadeffekte, Speichern unter
Linux und Windows, `InkscapePreferences::AddPage`). Was eine neue Inkscape-Fassung dazubringt, ist zuerst gesperrt.

## 2. Ganze Dateien (`fork/inkscape/`, über die Inkscape-Datei kopiert)

| Datei | was und warum |
|---|---|
| `share/ui/menus.ui` | Menüs auf das Fräsen zugeschnitten, Menü «Fräsen» – Inkscapes Menü ist für Grafik gebaut |
| `share/ui/toolbar-select.ui` | Auswahl-Leiste: Ausrichten, Bezugspunkt, Material – weniger, aber fräsgerechte Knöpfe |
| `share/ui/toolbar-tool.ui` | Werkzeugleiste links: nur Werkzeuge, die beim Fräsen helfen, dazu Pfadeffekte |
| `share/keys/inkscape.xml` | Tastenkürzel passend zu den Menüs (auch der Schreibfehler bei Strg+Alt+M behoben); Satz heisst «Hirch CAM»; ohne Tasten für Drucken, Vorlagen, Füllung, Muster, Bitmap-Kopie, Version, Palette |
| `share/ui/default-dialog-state.ini` | beim ersten Start offene Reiter: Fräsen, Einstellungen |
| `share/ui/dialog-crash.glade` | Absturzmeldung nennt Hirch CNC |

## 3. Teil-Patches (`fork/patches/NN-*.patch`, je Thema)

| Patch | Dateien | was und warum |
|---|---|---|
| `01-bau.patch` | `src/ui/CMakeLists.txt`, `src/libnrtype/CMakeLists.txt`, `share/keys/CMakeLists.txt`, `share/CMakeLists.txt` | die eigenen Dateien aus `fork/src/` mitübersetzen; nur der eigene Tastensatz wird eingerichtet (keine Sätze mit Firmennamen); kein Thema «Minwaita-Inkscape» |
| `02-start.patch` | `inkscape-main.cpp`, `inkscape.cpp`, `inkscape-application.cpp`, `inkscape-window.cpp`, `preferences-skeleton.h`, `ui/desktop/menubar.cpp`, `ui/dialog/dialog-container.cpp`, `ui/dialog/dialog-data.cpp`, `ui/dialog/dialog-base.{h,cpp}`, `ui/dialog/dialog-notebook.cpp` | eigener Programmname, Profil und Fensterklasse; Windows: Grafiktest/Mesa, Sprache deutsch; Hirch-Aktionen und -Reiter anmelden; Filter-/Erweiterungen-Menü und nicht gebrauchte Dialoge weg; Fräsfarben als Palette; Escape in einem Fenster der Seitenleiste führt zurück zu Fräsen (`hirch_dialog_escape`, 07.11.); Fenster nur aus der Liste «erlaubt» |
| `03-oberflaeche.patch` | `ui/contextmenu.cpp`, `ui/dialog/color-item.cpp`, `ui/dialog/livepatheffect-editor.cpp`, `ui/dialog/inkscape-preferences.cpp`, `ui/dialog/svg-preview.cpp`, `ui/dialog/transformation.cpp`, `ui/dialog/tile.cpp`, `ui/shortcuts.cpp`, `ui/widget/canvas-grid.*`, `ui/widget/desktop-widget.*`, `ui/widget/page-selector.cpp`, `ui/widget/status-bar.*` | Kontextmenü fürs Fräsen (provisorischer Nullpunkt); Farbfeld färbt die Linie (= Fräsart); Lage ab Werkstück-Nullpunkt; schwebende Leiste und 3D-Ansicht über der Zeichenfläche; Rückgängig immer vorn; ohne Inkscape-Logo; Pfadeffekte und Seiten der Erweiterten Einstellungen nur aus der Liste «erlaubt», ohne Sprachwahl und XML-Farbschema; ein GTK-Thema, das es nicht mehr gibt (Minwaita), wird zum Systemthema |
| `04-werkzeugleisten.patch` | `ui/toolbar/select-toolbar.*`, `ui/toolbar/arc-toolbar.*`, `ui/toolbar/text-toolbar.*`, `ui/tools/select-tool.cpp`, `ui/toolbar/tool-toolbar.cpp`, `ui/toolbar/lpe-toolbar.cpp`, `ui/tools/booleans-tool.cpp`, `actions/actions-tools.cpp`, `ui/widget/font-selector.*`, `libnrtype/font-lister.*` | Aufrufe der Hirch-Teile (Bezugspunkt, Drehwinkel, Radius, Einlinienschriften), Nullpunkt-Feld, Mitte als Vorgabe; Werkzeuge nur aus der Liste «erlaubt» |
| `05-zeichnen.patch` | `seltrans.*`, `selection.h`, `selection-chemistry.cpp`, `actions/actions-object-align.cpp`, `page-manager.*`, `object/sp-page.cpp`, `document.h`, `document-undo.*`, `live_effects/parameter/point.cpp`, `display/…` | Koordinaten, Lineale, Pfadeffekte ab dem Werkstück-Nullpunkt; Bezugspunkt Mitte; Drehwinkel je Objekt merken; Ausrichten am zuletzt gewählten; Linienbreite auf der Zeichenfläche begrenzen; Undo-Schritte zusammenfassen |
| `06-datei.patch` | `extension/extension.h`, `extension/internal/svg.cpp`, `extension/system.cpp`, `file.cpp`, `xml/repr-util.cpp`, `ui/dialog/filedialog*` | eigene Endung `.hir`; datierte Fassungen beim Speichern; DXF/AI/PDF nach dem Import aufräumen; Hirch-Attribute erhalten; Dateitypen beim Speichern nur aus der Liste «erlaubt», «Inkscape-SVG» heisst «SVG mit Hirch-Angaben»; unter Windows Öffnen/Importieren im Fenster von Windows (Speichern bleibt Win32, `desktopintegration` = 1) |

Dazu `po-de.patch` im Untermodul `po/`: deutsche Texte («Hirch CNC», «ss», Guillemets).

**Noch im Patch, nicht verschoben** (Stand 29.10.2026): der Windows-Teil von `inkscape-main.cpp` (Grafiktest, Mesa,
Profil – unter Linux nicht übersetzbar, verschieben nur zusammen mit der Windows-Sitzung) und kleinere Eingriffe
mitten in Inkscape-Funktionen, die sich nicht als eigene Funktion herauslösen lassen.
