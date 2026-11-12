#!/bin/bash
# Übersetzt OpenCASCADE (OCCT, LGPL-2.1) schlank für den 3D-Kern (STEP lesen,
# vernetzen). Das fertige MSYS2-Paket zieht über TKService FFmpeg, FreeImage und
# OpenVR nach – die braucht Hirch CNC nicht. Diese Fassung braucht nur, was in
# der Inkscape-Umgebung ink168 schon ist (FreeType); ohne TBB, OCCT verteilt dann
# selbst auf die Kerne. Nur einmal nötig.
#
#   fork/windows/occt.sh [ARBEITSORDNER]     Standard: Ordner arbeit neben dem Repo
#
# Ergebnis: ARBEITSORDNER/occt (include/opencascade, lib/libTK*.dll.a, bin/libTK*.dll)
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
ARBEIT="${1:-$REPO/../arbeit}"
mkdir -p "$ARBEIT"
ARBEIT="$(cd "$ARBEIT" && pwd)"
TAG="V7_9_2"
QUELLE="$ARBEIT/occt-src"
BAU="$ARBEIT/occt-bau"
ZIEL="$ARBEIT/occt"

if [ ! -d "$QUELLE/.git" ]; then
  git -c core.autocrlf=false clone --depth 1 --branch "$TAG" https://github.com/Open-Cascade-SAS/OCCT "$QUELLE"
fi
cmake -S "$QUELLE" -B "$BAU" -G Ninja -DCMAKE_BUILD_TYPE=Release -DINSTALL_DIR="$ZIEL" \
      -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
      -DBUILD_LIBRARY_TYPE=Shared -DBUILD_DOC_Overview=OFF -DBUILD_SAMPLES_QT=OFF \
      -DBUILD_MODULE_Draw=OFF -DBUILD_MODULE_DETools=OFF \
      -DBUILD_MODULE_FoundationClasses=ON -DBUILD_MODULE_ModelingData=ON -DBUILD_MODULE_ModelingAlgorithms=ON \
      -DBUILD_MODULE_Visualization=ON -DBUILD_MODULE_ApplicationFramework=ON -DBUILD_MODULE_DataExchange=ON \
      -DUSE_FREETYPE=ON -DUSE_TBB=OFF -DUSE_OPENGL=OFF -DUSE_GLES2=OFF -DUSE_D3D=OFF \
      -DUSE_FFMPEG=OFF -DUSE_FREEIMAGE=OFF -DUSE_OPENVR=OFF -DUSE_TK=OFF -DUSE_VTK=OFF \
      -DUSE_RAPIDJSON=OFF -DUSE_DRACO=OFF -DUSE_TCL=OFF
ninja -C "$BAU"
ninja -C "$BAU" install
echo "Fertig: $ZIEL"
for d in "$ZIEL"/bin*/libTKService.dll "$ZIEL"/win64/gcc/bin*/libTKService.dll; do
  [ -f "$d" ] && { echo "$d braucht:"; objdump -p "$d" | sed -n 's/.*DLL Name: //p' | grep -v '^libTK'; }
done
true
