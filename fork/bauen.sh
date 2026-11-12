#!/bin/sh
# Baut das Programm Hirch CNC (umgebautes Inkscape 1.4.4) aus den Quellen.
#
#   fork/bauen.sh [BAU-ORDNER]        Standard: ~/bau
#
# Schritte: Inkscape 1.4.4 holen, die Hirch-CNC-Änderungen einspielen
# (fork/einspielen.sh: Teil-Patches, ganze Dateien aus fork/inkscape/; dazu die neuen Dateien in fork/src),
# übersetzen (Präfix /opt/hirch-cam). Danach fork/paket.sh für das Paket.
#
# Einmalig nötig (Ubuntu 24.04 / Linux Mint 22):
#   sudo apt build-dep inkscape   (deb-src in den Paketquellen aktivieren)
#   sudo apt install ninja-build cmake ccache \
#        libgtksourceview-4-dev libgspell-1-dev libpoppler-private-dev \
#        libboost-all-dev libepoxy-dev libdouble-conversion-dev \
#        python3-dev python3-pybind11
#   (die letzten beiden für das CAM-Modul, cam_kern/bauen.sh)
set -e
REPO="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
BAU="${1:-$HOME/bau}"
QUELLE="$BAU/inkscape-src"
ZIEL="$BAU/build"
TAG="INKSCAPE_1_4_4"

mkdir -p "$BAU"
if [ ! -d "$QUELLE/.git" ]; then
  git clone --depth 1 --branch "$TAG" --recurse-submodules --shallow-submodules \
      https://gitlab.com/inkscape/inkscape.git "$QUELLE"
fi

cd "$QUELLE"
# Hirch-CNC-Änderungen: Teil-Patches je Thema, po-de.patch, ganze Dateien aus fork/inkscape/ (fork/einspielen.sh)
sh "$REPO/fork/einspielen.sh" "$QUELLE" || exit 1
# Neue Dateien (verlinkt, damit Änderungen im Repo sofort gelten)
( cd "$REPO/fork/src" && find . -type f ) | while read -r f; do
  ln -sf "$REPO/fork/src/$f" "$QUELLE/src/$f"
done

mkdir -p "$ZIEL"
cd "$ZIEL"
if [ ! -f build.ninja ]; then
  cmake "$QUELLE" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/hirch-cam \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        -DBUILD_TESTING=OFF -DWITH_IMAGE_MAGICK=OFF -DWITH_GRAPHICS_MAGICK=OFF
fi
ninja
echo "Fertig übersetzt. Paket bauen mit: $REPO/fork/paket.sh $BAU"
