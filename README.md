# Hirch CAM

**Free CAM software for CNC routers.** You draw your part and give each line a colour. The colour sets how it is cut.
Hirch CAM then shows the cut as a 3D simulation and writes the G-code for your machine.

Hirch CAM is built on [Inkscape](https://inkscape.org) 1.4.4. It keeps Inkscape's drawing tools and adds a docked
milling panel, its own menus and a 3D preview. The toolpath engine is written for Hirch CAM. Its 2D core uses parts
of [FreeCAD](https://www.freecad.org) CAM (libarea, Clipper, Adaptive and the V-carve centre lines).

- **2D:** profile cuts with tabs, pockets with islands, V-carving, engraving on the line, drilling, single-line fonts,
  nesting and two-sided jobs with dowel holes.
- **3D:** STEP, STL and OBJ models; roughing by contours or parallel passes; finishing with parallel passes,
  waterlines, a combination of both, flat areas or pencil passes; cutting from two sides.
- **Machines:** your own machines and tools, feeds and speeds by material, and post-processors for RichAuto,
  WinCNC, LinuxCNC/Mach3 and Grbl. You can write your own post-processor as a small JSON file
  ([docs/POSTPROZESSOR.md](docs/POSTPROZESSOR.md)).
- **Units:** mm or inches. Internally, and in the G-code (G21), everything stays in mm.
- **Languages:** English and German.

| Colour | Operation |
|---|---|
| Red | V-carve (area cut with a V-bit; the depth follows the width) |
| Orange | Engrave on the line |
| Green | Pocket (inner shapes become islands) |
| Blue | Profile (cut out or to a depth, with tabs) |
| Violet | Drill |
| Grey | Not cut (construction lines) |

## Download

Packages for Linux and Windows are on [cam.hirch.net](https://cam.hirch.net). The first start asks for your
machine, its controller and your units, and then sets up a starter tool list.

## Build it yourself

### Linux (Ubuntu 24.04 / Linux Mint 22)

```bash
fork/bauen.sh ~/build                 # fetch Inkscape 1.4.4 (dcaf3e7), apply the changes, compile
sh cam_kern/bauen.sh python3.12       # 2D toolpath module
sh relief_kern/bauen.sh python3.12    # 3D toolpath module
fork/paket.sh ~/build                 # -> ~/build/hirch-cam-VERSION-linux-x86_64.tar.xz
```

The developer packages you need are listed at the top of `fork/bauen.sh`.

### Windows

Hirch CAM is built in MSYS2 (UCRT64): see `fork/windows/`.

### Tests

```bash
python3 -m pytest -q tests/ --ignore=tests/test_oberflaeche.py   # toolpaths, post-processors, files
python3 -m pytest -q tests/test_oberflaeche.py                    # the user interface (needs Xvfb)
```

## Repository layout

```
fork/          the changes to Inkscape 1.4.4: patches by topic, whole files (menus, toolbars), own sources
               (fork/src/ui/dialog/hirch-*), build and packaging scripts
hirch_cam/     toolpath engine (Python): reading the drawing, operations, post-processors, fonts, tool database
cam_kern/      2D module (C++): libarea from FreeCAD CAM, V-carve centre lines
relief_kern/   3D module (C++): sampling, roughing and finishing, STEP import (Open CASCADE, OpenCAMLib)
inkscape/      profile, palette, templates and extensions for the milling panel
tests/         tests and test data
docs/          architecture, post-processors, cutting data, single-line fonts
```

The source code, comments and most documents are in German ([LIESMICH.md](LIESMICH.md) is this page in German). The user interface is in English and German.

## Contributing

Bug reports are welcome. Use *Milling › Send Feedback …* in the program or open a GitHub issue. See
[CONTRIBUTING.md](CONTRIBUTING.md).

## Licence

Hirch CAM is licensed under the **GNU General Public License, version 3 or later** (GPL-3.0-or-later), like Inkscape
as a whole. See [LICENSE](LICENSE). The origin and licence of every part taken from other projects is listed in
[NOTICE.md](NOTICE.md).

Large parts of the code, the tests and the documentation were written together with **Claude** (Anthropic). Peter set
the goals, checked everything on his own machine and made the decisions.

Hirch CAM is an independent project. It is not affiliated with or endorsed by Inkscape or FreeCAD.
