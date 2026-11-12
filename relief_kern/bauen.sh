#!/bin/sh
# Baut das Modul hirch_relief (Rechenkern der 3D-Fräsung) als Python-Erweiterung.
#
#   relief_kern/bauen.sh [PYTHON …]     Standard: python3
#
# Braucht: g++, pybind11 (python3-pybind11), Python-Header (python3-dev).
# STEP-Dateien nur, wenn OpenCASCADE da ist (libocct-data-exchange-dev,
# libocct-modeling-algorithms-dev); sonst ohne STEP. 3D-Bahnen nur mit OpenCAMLib
# (libopencamlib-dev, LGPL-2.1, wie in FreeCAD CAM).
# Ergebnis: hirch_cam/hirch_relief.cpython-…so
set -e
HIER="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
ZIEL="$HIER/../hirch_cam"
OCC=/usr/include/opencascade
if [ -f "$OCC/STEPControl_Reader.hxx" ]; then
  EXTRA="-DMIT_OCCT -I$OCC"
  LIBS="-lTKSTEP -lTKXSBase -lTKBRep -lTKernel -lTKMath -lTKG3d -lTKG2d -lTKGeomBase \
        -lTKTopAlgo -lTKMesh -lTKGeomAlgo -lTKShHealing"
  echo "mit STEP (OpenCASCADE)"
else
  EXTRA=""; LIBS=""
  echo "ohne STEP (OpenCASCADE fehlt)"
fi
# 3D-Bahnen (Drop-Cutter) mit OpenCAMLib (libopencamlib-dev), sonst ohne
OCL=/usr/include/opencamlib
if [ -f "$OCL/batchdropcutter.hpp" ]; then
  EXTRA="$EXTRA -DMIT_OCL -fopenmp"
  LIBS="$LIBS -L/usr/lib/x86_64-linux-gnu/opencamlib -locl -Wl,-rpath,/usr/lib/x86_64-linux-gnu/opencamlib"
  echo "mit OpenCAMLib"
else
  echo "ohne OpenCAMLib (libopencamlib-dev fehlt)"
fi
[ $# -eq 0 ] && set -- python3
for PY in "$@"; do
  INC="$("$PY" -c 'import sysconfig; print(sysconfig.get_paths()["include"])')"
  SUFFIX="$("$PY" -c 'import sysconfig; print(sysconfig.get_config_var("EXT_SUFFIX"))')"
  echo "Baue hirch_relief$SUFFIX …"
  # shellcheck disable=SC2086
  g++ -O2 -std=c++17 -shared -fPIC -fvisibility=hidden -Wall -Wno-deprecated-declarations \
      -I"$INC" $EXTRA "$HIER/modul.cpp" -o "$ZIEL/hirch_relief$SUFFIX" $LIBS
  strip --strip-unneeded "$ZIEL/hirch_relief$SUFFIX"
  ls -l "$ZIEL/hirch_relief$SUFFIX"
done
