# 3D-Modul hirch_relief – Herkunft und Lizenzen

`relief_kern/modul.cpp` ist eigene Arbeit von Hirch CNC (Höhenraster, Abtrag-Simulation, Anbindung). Für das
3D-Fräsen nutzt es zwei freie Bibliotheken, die **nicht** in Hirch CNC hineinkopiert, sondern dynamisch gelinkt
werden:

| Bibliothek | wofür | Lizenz | Quelle |
|---|---|---|---|
| OpenCAMLib 2023.01 (`libocl`) | Drop-Cutter: wie tief der Fräser an jedem Punkt sinken darf (3D-Bahnen) | LGPL-2.1+ | https://github.com/aewallin/opencamlib |
| Open CASCADE Technology 7.6 (`TKSTEP`, `TKernel`, `TKMesh` …) | STEP-Dateien lesen und vernetzen | LGPL-2.1 mit OpenCASCADE-Ausnahme | https://dev.opencascade.org |
| pybind11 | Anbindung an Python (nur Header) | BSD-Stil | https://github.com/pybind/pybind11 |

**Linux:** Die Bibliotheken kommen als Pakete der Distribution (`libopencamlib-2023.01`, `libocct-*-7.6t64`, siehe
`share/hirch-cam/pakete.txt`); deren Lizenztexte liegen dort unter `/usr/share/doc/<Paket>/copyright`.
**Windows:** Die DLLs werden mitgeliefert – dann gehören ihre Lizenztexte (LGPL-2.1, OpenCASCADE-Ausnahme) ins Paket.

Die LGPL erlaubt, die Bibliotheken gegen eine eigene Fassung auszutauschen: Sie sind getrennte Dateien, das Modul
lässt sich mit `relief_kern/bauen.sh` neu bauen.
