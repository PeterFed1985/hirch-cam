#!/bin/bash
# Übersetzt OpenCAMLib (C++-Bibliothek, LGPL-2.1) für Windows – gibt es für MSYS2
# nicht fertig. Gleiche Fassung wie unter Linux (libopencamlib-2023.01).
# Läuft in der Inkscape-Umgebung ink168 (MSYS2 UCRT64), nur einmal nötig.
#
#   fork/windows/opencamlib.sh [ARBEITSORDNER]     Standard: Ordner arbeit neben dem Repo
#
# Ergebnis: ARBEITSORDNER/ocl (include/opencamlib, lib/libocl.dll.a, bin/libocl.dll)
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
ARBEIT="${1:-$REPO/../arbeit}"
mkdir -p "$ARBEIT"
ARBEIT="$(cd "$ARBEIT" && pwd)"
TAG="2023.01.11"
QUELLE="$ARBEIT/opencamlib-src"
BAU="$ARBEIT/opencamlib-bau"
ZIEL="$ARBEIT/ocl"

if [ ! -d "$QUELLE/.git" ]; then
  git -c core.autocrlf=false clone --depth 1 --branch "$TAG" https://github.com/aewallin/opencamlib "$QUELLE"
fi
cmake -S "$QUELLE" -B "$BAU" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$ZIEL" \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DBUILD_CXX_LIB=ON -DBUILD_PY_LIB=OFF -DBUILD_NODEJS_LIB=OFF -DBUILD_DOC=OFF -DUSE_OPENMP=ON \
      -DVERSION_STRING="$TAG"
ninja -C "$BAU"
ninja -C "$BAU" install
echo "Fertig: $ZIEL"
find "$ZIEL" -name 'libocl*' -o -name 'batchdropcutter.hpp'
