# Licence and origin

Hirch CAM is licensed under the **GNU General Public License, version 3 or later** (GPL-3.0-or-later). The full text
is in [`LICENSE`](LICENSE).

Copyright © 2026 Peter Federspiel (GitHub: PeterFed1985) and the Hirch CAM contributors.

Hirch CAM is a modified version of Inkscape. Most of Inkscape is licensed under GPL-2.0-or-later, and some parts
under (L)GPL-3.0-or-later. So a complete Inkscape program falls under GPL-3.0-or-later, and Hirch CAM uses that
licence for the whole.

## Written with Claude

The source code of Hirch CAM was written together with **Claude**, the AI assistant from Anthropic. Claude wrote most
of the code, the tests and the documentation. Peter set the goals, the workflows and the decisions. He checked
everything on his own machine and approved it.

## Parts from other projects

| Part | Where | Origin | Licence |
|---|---|---|---|
| Inkscape 1.4.4 (drawing program, user interface) | `fork/` (changes as patches); in the package | https://inkscape.org, commit dcaf3e7 | GPL-2.0-or-later, the whole GPL-3.0-or-later; details in Inkscape's `COPYING` and `LICENSES/` |
| libarea (Area, Curve, Pocket …) | `cam_kern/libarea/` | Dan Heeks, from FreeCAD `src/Mod/CAM/libarea` (tag 1.0.2) | BSD-3-Clause |
| Clipper 6.2 | `cam_kern/libarea/clipper.*` | Angus Johnson | BSL-1.0 |
| Adaptive (adaptive clearing) | `cam_kern/libarea/Adaptive.*` | Kresimir Tusek, from FreeCAD | LGPL-2.1-or-later |
| V-carve centre lines | `cam_kern/modul.cpp` | rewritten after FreeCAD `App/Voronoi.cpp` and `Path/Op/Vcarve.py` (Markus Lampert) | LGPL-2.1-or-later |
| Boost.Polygon (Voronoi), pybind11 | headers only, at build time | Boost; pybind11 | BSL-1.0; BSD-3-Clause |
| Single-line fonts Hershey Sans/Serif/Script | `hirch_cam/schriften/einlinien.json` | Dr. A. V. Hershey, U.S. National Bureau of Standards (1967) | Hershey licence (free with acknowledgement) |
| Single-line fonts EMS Readability/Tech/Allure | `hirch_cam/schriften/einlinien.json` | Evil Mad Scientist Laboratories (Hershey Text) | OFL-1.1 |
| Other single-line fonts (Relief SingleLine, KiCad Newstroke) | `hirch_cam/schriften/` | see [`hirch_cam/schriften/LIZENZEN.md`](hirch_cam/schriften/LIZENZEN.md) | OFL-1.1; GPL-2.0-or-later |
| 3D module `hirch_relief` (sampling, material removal, centre lines, STEP import) | `relief_kern/modul.cpp` | own work; centre lines after Zhang–Suen (1984) | GPL-3.0-or-later |
| OpenCAMLib (AdaptiveWaterline, ball/bull/cone cutters) | linked by `relief_kern/modul.cpp` | Anders Wallin and others, https://github.com/aewallin/opencamlib | LGPL-2.1-or-later |
| Open CASCADE Technology (reading STEP, meshing surfaces) | linked by `relief_kern/modul.cpp` | Open Cascade SAS, https://dev.opencascade.org | LGPL-2.1 with the OCCT exception |
| Inkscape and FreeCAD logos (thanks in the welcome window only) | `assets/willkommen/` | see `assets/willkommen/QUELLEN.md` | trademarks of their projects |

Details: [`cam_kern/LIZENZEN.md`](cam_kern/LIZENZEN.md), [`relief_kern/LIZENZEN.md`](relief_kern/LIZENZEN.md),
[`hirch_cam/schriften/LIZENZEN.md`](hirch_cam/schriften/LIZENZEN.md).

Used, but not in the repository (taken from the distribution, or bundled in the Windows package): GTK 3 and gtkmm
(LGPL-2.1-or-later), Boost (BSL-1.0), Python 3 (PSF licence) with numpy, scipy and shapely (BSD), lxml, fonttools,
Pillow, OpenGL. The Windows package carries their licence texts in `lizenzen/`.

## Data

| Data | Origin |
|---|---|
| `tests/3d/Fluegel_Test.step`, `tests/3d/Fluegel_Test.stl`, `tests/daten/fluegel*.hir`, impeller | designed by Peter in FreeCAD and released for the tests |
| `hirch_cam/daten/testteil_hartschaum.step` (test part, in the package) | own work: made with `beispiele/testteil_3d/testteil_erzeugen.cpp` (Open CASCADE) |
| `tests/daten/merkmale.step` | own work: made with `tests/daten/merkmale_erzeugen.cpp` |
| `tests/zeichnungen/`, `tests/referenz/`, other test data | written for the tests or produced by Hirch CAM itself |
| Post-processor templates `hirch_cam/post/vorlagen/` | own work, after the controller manuals |

Inkscape and FreeCAD are trademarks of their projects. Hirch CAM is an independent project and is not affiliated
with or endorsed by them.
