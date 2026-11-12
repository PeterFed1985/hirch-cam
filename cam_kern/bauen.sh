#!/bin/sh
# Baut das CAM-Modul hirch_cam (libarea aus FreeCAD CAM + V-Carve-Mittellinien)
# als Python-Erweiterung.
#
#   cam_kern/bauen.sh [PYTHON …]     Standard: python3
#
# Braucht: g++, pybind11 (python3-pybind11), Boost-Header (libboost-dev),
# Python-Header (python3-dev). Ergebnis: hirch_cam/hirch_cam.cpython-…so
# Die C++-Laufzeit ist fest eingebaut, aber versteckt (--exclude-libs), sonst
# stört sie die von Shapely/GEOS (Absturz beim Formatieren von Zahlen).
set -e
HIER="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
ZIEL="$HIER/../hirch_cam"
QUELLEN="$HIER/modul.cpp"
for f in Adaptive Arc Area AreaClipper AreaOrderer AreaPocket Circle Curve clipper; do
  QUELLEN="$QUELLEN $HIER/libarea/$f.cpp"
done
for f in Construction Finite kurve Matrix offset; do
  QUELLEN="$QUELLEN $HIER/libarea/kurve/$f.cpp"
done
[ $# -eq 0 ] && set -- python3
for PY in "$@"; do
  INC="$("$PY" -c 'import sysconfig; print(sysconfig.get_paths()["include"])')"
  SUFFIX="$("$PY" -c 'import sysconfig; print(sysconfig.get_config_var("EXT_SUFFIX"))')"
  echo "Baue hirch_cam$SUFFIX …"
  # shellcheck disable=SC2086
  g++ -O2 -std=c++17 -shared -fPIC -fvisibility=hidden -w \
      -I/usr/include -I"$INC" -I"$HIER/libarea" $QUELLEN \
      -o "$ZIEL/hirch_cam$SUFFIX" -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL -Wl,--version-script="$HIER/export.map"
  strip --strip-unneeded "$ZIEL/hirch_cam$SUFFIX"
  ls -l "$ZIEL/hirch_cam$SUFFIX"
done
