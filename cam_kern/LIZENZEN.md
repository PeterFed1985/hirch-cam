# CAM-Modul hirch_cam – Herkunft und Lizenzen

Das CAM-Modul ist aus dem CAM-Arbeitsbereich von FreeCAD gebaut
(https://github.com/FreeCAD/FreeCAD, Tag `1.0.2`, `src/Mod/CAM`), aber
ohne FreeCAD selbst, ohne OpenCASCADE und ohne Qt.

| Teil | Herkunft | Lizenz |
|---|---|---|
| `libarea/` (Area, Curve, Arc, Circle, AreaClipper, AreaPocket, AreaOrderer) | Dan Heeks, aus FreeCAD `src/Mod/CAM/libarea` | BSD (3-Klausel), siehe Kopf der Dateien |
| `libarea/clipper.*` | Angus Johnson, Clipper 6.2 | Boost Software License 1.0 |
| `libarea/kurve/` | Geometrie-Bibliothek aus libarea (Dan Heeks / ursprünglich «kurve») | wie libarea (BSD) |
| `libarea/Adaptive.*` | Kresimir Tusek, aus FreeCAD | LGPL-2.1+ (adaptives Ausräumen) |
| `modul.cpp`, Mittellinien | Nachbau von FreeCAD `App/Voronoi.cpp` und `Path/Op/Vcarve.py` (Markus Lampert) auf Boost.Polygon | LGPL-2.1+ |
| Boost.Polygon (Voronoi) | Boost, nur Header | Boost Software License 1.0 |
| pybind11 | nur Header | BSD-Stil |

Die Python-Teile von Hirch CNC (Ringe, Löcher, Reihenfolge, Haltestege,
Tiefen, Postprozessor) sind eigene Arbeit.

Aktualisieren: Dateien aus einem neueren FreeCAD-Tag nach `libarea/`
kopieren (ohne `PythonStuff.*`, `pyarea.cpp`, `test.py`), dann
`cam_kern/bauen.sh python3.12 python3` und die Tests laufen lassen.
