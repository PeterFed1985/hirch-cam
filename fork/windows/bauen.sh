#!/bin/bash
# Baut Hirch CNC für Windows 10/11 (64 Bit) – Gegenstück zu fork/bauen.sh und
# fork/paket.sh unter Linux. Läuft lokal in Inkscapes Bauumgebung ink168
# (MSYS2 UCRT64, der Stand, mit dem Inkscape 1.4.x gebaut wird):
#
#   C:\ink168\msys2_shell.cmd -defterm -no-start -ucrt64 -here -c "fork/windows/bauen.sh [ARBEITSORDNER]"
#
# Standard-Arbeitsordner: arbeit neben dem Repo. Ergebnis: ARBEITSORDNER/dist
# (Programmordner fürs Installationsprogramm, fork/windows/paket.ps1 macht den Rest).
# Der erste Lauf übersetzt Inkscape, OpenCASCADE und OpenCAMLib (Stunden), danach
# nur noch Geändertes (ccache).
set -euo pipefail
{ # alles in Klammern: bash liest das Skript ganz ein, Änderungen während des Laufs stören nicht
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
ARBEIT="${1:-$REPO/../arbeit}"
mkdir -p "$ARBEIT"
ARBEIT="$(cd "$ARBEIT" && pwd)"
QUELLE="$ARBEIT/inkscape-src"
BAU="$ARBEIT/build"
DIST="$ARBEIT/dist"
PAKETE="$ARBEIT/pakete"
TAG="INKSCAPE_1_4_4"
PYV="3.12"
SITE="/ucrt64/lib/python$PYV/site-packages"
# Bau: Datum-Commit (steht immer in share/hirch-cam/BAU). Release (HIRCH_RELEASE=1, wie fork/paket.sh): Version aus
# der Datei VERSION, z. B. «1.0.0-beta.1»; sonst ist der Bau die Version (Zwischenstände)
BAU_ID="$(cd "$REPO" && git log -1 --date=format:%Y.%m.%d --format=%cd)-$(cd "$REPO" && git rev-parse --short HEAD)"
if [ -n "$HIRCH_RELEASE" ]; then
  VERSION="$(tr -d ' \r\n' < "$REPO/VERSION")"
else
  VERSION="$BAU_ID"
fi
echo "Hirch CNC $VERSION für Windows"

# --- Zusatzpakete, fest auf den Stand der Umgebung (Python 3.12, Dezember 2025) ---
M=https://repo.msys2.org/mingw/ucrt64
mkdir -p "$PAKETE"
hole() {   # hole PAKET-VERSION → Pfad der Paketdatei
  local f="$PAKETE/mingw-w64-ucrt-x86_64-$1-any.pkg.tar.zst"
  [ -f "$f" ] || curl -sSfL -o "$f" "$M/$(basename "$f")"
  echo "$f"
}
zusatz=()
for p in geos-3.13.0-1 python-shapely-2.1.2-1 python-fonttools-4.61.1-1 python-olefile-0.47-2 \
         python-scipy-1.16.3-1; do
  zusatz+=("$(hole "$p")")
done
pacman -U --noconfirm --needed --nodeps --nodeps "${zusatz[@]}"
python -c "import pybind11" 2>/dev/null || python -m pip install --quiet --break-system-packages --no-deps pybind11==2.13.6
python -c "import shapely, fontTools, olefile, scipy, pybind11; print('shapely', shapely.__version__, 'scipy', scipy.__version__)"

# --- OpenCASCADE und OpenCAMLib für den 3D-Kern (einmal übersetzt) -----------
[ -f "$ARBEIT/occt/.fertig" ] || { bash "$REPO/fork/windows/occt.sh" "$ARBEIT" && touch "$ARBEIT/occt/.fertig"; }
[ -f "$ARBEIT/ocl/.fertig" ] || { bash "$REPO/fork/windows/opencamlib.sh" "$ARBEIT" && touch "$ARBEIT/ocl/.fertig"; }
OCCT_BIN="$ARBEIT/occt/win64/gcc/bin"
OCCT_LIB="$ARBEIT/occt/win64/gcc/lib"
OCCT_INC="$ARBEIT/occt/inc"
OCL_BIN="$ARBEIT/ocl/bin/opencamlib"

# --- Inkscape-Quellen mit den Hirch-CNC-Änderungen (wie fork/bauen.sh) -------
if [ ! -d "$QUELLE/.git" ]; then
  git -c core.autocrlf=false clone --depth 1 --branch "$TAG" --recurse-submodules --shallow-submodules \
      https://gitlab.com/inkscape/inkscape.git "$QUELLE"
fi
cd "$QUELLE"
# Hirch-CNC-Änderungen: Teil-Patches je Thema, po-de.patch, ganze Dateien aus fork/inkscape/ (fork/einspielen.sh)
if ! sh "$REPO/fork/einspielen.sh" "$QUELLE"; then
  # geänderte Patches aus main: Quellen zurücksetzen und neu einspielen (geänderte Dateien übersetzt ninja neu)
  echo "Patches haben sich geändert – Quellordner zurückgesetzt."
  git checkout -- . && git -C po checkout -- . || exit 1
  sh "$REPO/fork/einspielen.sh" "$QUELLE" || { echo "Patches passen nicht auf $TAG." >&2; exit 1; }
fi
# Neue Dateien: kopiert (Verknüpfungen gehen unter Windows nicht zuverlässig);
# cp -p lässt die Zeit, damit ninja nur wirklich Geändertes neu übersetzt
( cd "$REPO/fork/src" && find . -type f ) | while read -r f; do
  cmp -s "$REPO/fork/src/$f" "$QUELLE/src/$f" || cp -p "$REPO/fork/src/$f" "$QUELLE/src/$f"
done
# Programmsymbol aus den Hirch-Bildern – für Verknüpfungen, Installer, .hir-Dateien und als
# Symbol in inkscape.exe (Titelleiste, Taskleiste, Dialoge; sonst das Inkscape-Logo)
python - "$REPO/fork/share/icons/png" "$ARBEIT/hirch-cam.ico" <<'EOF'
import sys
from PIL import Image
ordner, ziel = sys.argv[1], sys.argv[2]
gross = Image.open(f"{ordner}/hirch-cam-256.png").convert("RGBA")
gross.save(ziel, sizes=[(n, n) for n in (16, 24, 32, 48, 256)],
           append_images=[Image.open(f"{ordner}/hirch-cam-{n}.png").convert("RGBA") for n in (16, 24, 32, 48)])
EOF
if ! cmp -s "$ARBEIT/hirch-cam.ico" "$QUELLE/share/branding/inkscape.ico"; then
  cp "$ARBEIT/hirch-cam.ico" "$QUELLE/share/branding/inkscape.ico"
  # ninja kennt das Symbol nicht als Abhängigkeit der Ressource: neu übersetzen lassen
  if [ -d "$BAU" ]; then find "$BAU" -name '*.rc.obj' -delete; fi
fi

# --- Übersetzen ---------------------------------------------------------------
mkdir -p "$BAU"
if [ ! -f "$BAU/build.ninja" ]; then
  cmake -S "$QUELLE" -B "$BAU" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$DIST" \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        -DBUILD_TESTING=OFF -DWITH_IMAGE_MAGICK=OFF -DWITH_GRAPHICS_MAGICK=OFF
fi
rm -rf "$DIST"
ninja -C "$BAU" install/strip

# --- Paket (wie fork/paket.sh) ------------------------------------------------
SH="$DIST/share/inkscape"
DSITE="$DIST/lib/python$PYV/site-packages"
# Nicht gebraucht: Anleitungen, Beispiele, Entwickler-Dateien, Inkview; Sprachen nur Deutsch
rm -rf "$SH/tutorials" "$SH/examples" "$DIST/include" "$DIST/share/man" "$DIST/share/bash-completion" \
       "$DIST/bin/inkview.exe" "$DIST/bin/inkview.com" "$DIST"/*.bat "$DIST/NEWS.md" "$DIST/README.md"
for d in "$DIST"/share/locale/*/; do
  case "$(basename "$d")" in de|de_*) ;; *) rm -rf "$d" ;; esac
done
mkdir -p "$DIST/share/locale/en/LC_MESSAGES"
msgfmt -o "$DIST/share/locale/en/LC_MESSAGES/inkscape.mo" "$REPO/fork/sprache-en.po"
sed -i 's/inkex.errormsg("\$PDMODE is ignored. A point is displayed as normal.")/pass  # Hirch CNC: kein Hinweisfenster/' \
    "$SH/extensions/dxf_input.py"
# Kleiner (Peter 28.10.): Fehlersucher gdb, Python-Zusätze ohne Nutzen (Editor, Installationshilfen, Tk),
# Bildformate AVIF/HEIC/JPEG XL mit ihren Video-Bibliotheken – für CNC-Zeichnungen nicht gebraucht
rm -rf "$DIST/bin/gdb.exe" "$DIST/bin/gdb_create_backtrace.bat" "$DIST/share/gdb" \
       "$DIST/lib/python$PYV"/{idlelib,ensurepip,pydoc_data,lib2to3,tkinter,turtledemo,test} \
       "$DIST/lib/python$PYV"/lib-dynload/_tkinter.*.pyd "$DIST/bin/tcl86.dll" "$DIST/bin/tk86.dll" \
       "$DIST/lib"/tcl8* "$DIST/lib"/tk8*
PIXBUF="$DIST/lib/gdk-pixbuf-2.0/2.10.0"
rm -f "$PIXBUF"/loaders/libpixbufloader-{avif,heif,jxl}.dll \
      "$DIST"/bin/{libheif,libaom,libSvtAv1Enc-3,libdav1d-7,libde265-0}.dll
# dieselben Lader aus loaders.cache streichen (Einträge sind durch Leerzeilen getrennt)
awk -v RS= -v ORS='\n\n' '!/libpixbufloader-(avif|heif|jxl)\.dll/' "$PIXBUF/loaders.cache" > "$PIXBUF/loaders.neu"
mv "$PIXBUF/loaders.neu" "$PIXBUF/loaders.cache"

# Python-Pakete, die Inkscape selbst nicht mitbringt
for p in shapely shapely-*.dist-info shapely.libs fontTools fonttools-*.dist-info olefile olefile-*.dist-info \
         scipy scipy-*.dist-info; do
  for q in $SITE/$p; do
    [ -e "$q" ] && cp -r "$q" "$DSITE/"
  done
done
cp /ucrt64/bin/libgeos.dll /ucrt64/bin/libgeos_c.dll "$DIST/bin/"
find "$DSITE" -name "tests" -type d -prune -exec rm -rf {} +

# Rechenkern (Python) und Logo
mkdir -p "$SH/hirch-cam"
cp -r "$REPO/hirch_cam" "$REPO/assets" "$SH/hirch-cam/"
find "$SH/hirch-cam" -name "__pycache__" -type d -prune -exec rm -rf {} +
rm -f "$SH"/hirch-cam/hirch_cam/hirch_cam.*.so "$SH"/hirch-cam/hirch_cam/hirch_relief.*.so

# CAM-Modul hirch_cam und 3D-Kern hirch_relief für das Python im Paket
PYINC="$(python -c 'import sysconfig; print(sysconfig.get_paths()["include"])')"
PYBIND="$(python -c 'import pybind11; print(pybind11.get_include())')"
EXT="$(python -c 'import sysconfig; print(sysconfig.get_config_var("EXT_SUFFIX"))')"
QUELLEN="$REPO/cam_kern/modul.cpp"
for f in Adaptive Arc Area AreaClipper AreaOrderer AreaPocket Circle Curve clipper; do
  QUELLEN="$QUELLEN $REPO/cam_kern/libarea/$f.cpp"
done
for f in Construction Finite kurve Matrix offset; do
  QUELLEN="$QUELLEN $REPO/cam_kern/libarea/kurve/$f.cpp"
done
# shellcheck disable=SC2086
g++ -O2 -std=c++17 -shared -w -I"$PYINC" -I"$PYBIND" -I"$REPO/cam_kern/libarea" $QUELLEN \
    -o "$SH/hirch-cam/hirch_cam/hirch_cam$EXT" -L/ucrt64/lib -lpython$PYV
strip --strip-unneeded "$SH/hirch-cam/hirch_cam/hirch_cam$EXT"
echo "CAM-Modul: hirch_cam$EXT"
# 3D-Kern wie relief_kern/bauen.sh, mit STEP (OpenCASCADE 7.9: TKDESTEP statt TKSTEP) und OpenCAMLib
g++ -O2 -std=c++17 -shared -fvisibility=hidden -w \
    -I"$PYINC" -I"$PYBIND" -DMIT_OCCT -I"$OCCT_INC" -DMIT_OCL -fopenmp -I"$ARBEIT/ocl/include" \
    "$REPO/relief_kern/modul.cpp" -o "$SH/hirch-cam/hirch_cam/hirch_relief$EXT" \
    -L"$OCCT_LIB" -lTKDESTEP -lTKDE -lTKXSBase -lTKBRep -lTKernel -lTKMath -lTKG3d -lTKG2d -lTKGeomBase \
    -lTKTopAlgo -lTKMesh -lTKGeomAlgo -lTKShHealing -L"$ARBEIT/ocl/lib/opencamlib" -locl -L/ucrt64/lib -lpython$PYV
strip --strip-unneeded "$SH/hirch-cam/hirch_cam/hirch_relief$EXT"
echo "3D-Kern: hirch_relief$EXT"

# Fehlende DLLs ins bin\ (OpenCASCADE, OpenCAMLib, OpenMP, SciPy …): alles, was die
# Module laden und weder Windows noch das Paket schon hat
echo "DLLs für die Module:"
mapfile -t MODULE < <(find "$SH/hirch-cam/hirch_cam" "$DSITE/shapely" "$DSITE/scipy" -name '*.pyd' -exec cygpath -w {} +)
python "$REPO/fork/windows/dll_kette.py" "$(cygpath -w "$DIST/bin")" \
    "$(cygpath -w "$OCCT_BIN");$(cygpath -w "$OCL_BIN");$(cygpath -w /ucrt64/bin)" "${MODULE[@]}"
if [ -f "$DIST/bin/libTKService.dll" ] && objdump -p "$DIST/bin/libTKService.dll" | sed -n 's/.*DLL Name: //p' \
     | grep -qiE 'avcodec|freeimage|openvr'; then
  echo "OpenCASCADE zieht FFmpeg/FreeImage/OpenVR nach – occt.sh prüfen" >&2
  exit 1
fi

find "$DSITE" -name "__pycache__" -type d -prune -exec rm -rf {} +
"$DIST/bin/python.exe" -m compileall -qq "$DSITE/shapely" "$DSITE/fontTools" "$DSITE/olefile" "$DSITE/scipy" \
    "$SH/hirch-cam/hirch_cam" || true
# Einlinienschriften fürs Textwerkzeug schon hier bauen (sonst wartet der erste Start)
PYTHONPATH="$SH/hirch-cam" "$DIST/bin/python.exe" -m hirch_cam.schrift_ttf --vorrat "$SH/hirch-cam/hirch_cam/schriften/ttf" >/dev/null

# Erweiterung Einlinienschrift (im Menü «Fräsen»)
mkdir -p "$SH/extensions/hirch"
cp "$REPO/inkscape/extensions/hirch_einlinien.py" "$SH/extensions/hirch/"
sed 's|<effects-menu>|<effects-menu hidden="true">|' \
    "$REPO/inkscape/extensions/hirch_einlinien.inx" > "$SH/extensions/hirch/hirch_einlinien.inx"

# Palette und Vorlage für neue Zeichnungen (300 × 200 mm)
cp "$REPO/inkscape/profile/palettes/hirch-cam.gpl" "$SH/palettes/"
for t in "$SH"/templates/default*.svg; do
  cp "$REPO/inkscape/profile/templates/default.svg" "$t"
done

# Symbole
for thema in "$SH"/icons/*/; do
  for s in hirch-fraesen hirch-werkzeuge hirch-drehen-links hirch-drehen-rechts; do
    [ -d "$thema/scalable/actions" ] && cp "$REPO/fork/share/icons/$s.svg" "$thema/scalable/actions/"
    [ -d "$thema/symbolic/actions" ] && cp "$REPO/fork/share/icons/$s-symbolic.svg" "$thema/symbolic/actions/"
  done
done
mkdir -p "$SH/icons/hicolor/scalable/apps" "$DIST/share/icons/hicolor/scalable/apps"
cp "$REPO/fork/share/icons/hirch-cam.svg" "$SH/icons/hicolor/scalable/apps/"
cp "$REPO/fork/share/icons/hirch-cam.svg" "$DIST/share/icons/hicolor/scalable/apps/"
# Inkscape-Logo überall durch das Hirch-Logo ersetzen (Fenster-, Dialog- und Programmsymbole)
find "$DIST/share" \( -name 'org.inkscape.Inkscape.svg' -o -name 'inkscape-logo.svg' \) -not -path '*/extensions/*' \
  -exec cp "$REPO/fork/share/icons/hirch-cam.svg" {} \;
find "$DIST/share" \( -name 'org.inkscape.Inkscape-symbolic.svg' -o -name 'inkscape-logo-symbolic.svg' \) -not -path '*/extensions/*' \
  -exec cp "$REPO/fork/share/icons/hirch-cam-symbolic.svg" {} \;
# wie fork/paket.sh (Peter 28.10.): auch die Markenbilder ohne Inkscape-Logo
for f in "$SH"/branding/*.svg; do
  [ -f "$f" ] && cp "$REPO/fork/share/icons/hirch-cam.svg" "$f"
done
for n in 16 22 24 32 48 256; do
  for d in "$DIST/share/icons/hicolor/${n}x${n}/apps" "$SH/icons/hicolor/${n}x${n}/apps"; do
    [ -d "$d" ] || continue
    cp "$REPO/fork/share/icons/png/hirch-cam-$n.png" "$d/hirch-cam.png"
    [ -f "$d/org.inkscape.Inkscape.png" ] && cp "$REPO/fork/share/icons/png/hirch-cam-$n.png" "$d/org.inkscape.Inkscape.png"
  done
done
cp "$ARBEIT/hirch-cam.ico" "$DIST/hirch-cam.ico"

# Software-Darstellung (Mesa llvmpipe, installiert 150 MB, im Installer 27 MB) für Rechner ohne
# OpenGL 3.3 (z. B. ohne Grafiktreiber, Fernzugriff) – nur die nötigen DLLs nach bin\mesa
# (inkscape-main.cpp lädt sie bei Bedarf), nicht installiert
mkdir -p "$ARBEIT/mesa" "$DIST/bin/mesa"
for p in mesa-25.3.3-1 llvm-libs-21.1.8-1 'spirv-tools-3~1.4.335.0-1'; do
  tar --zstd -xf "$(hole "$p")" -C "$ARBEIT/mesa" ucrt64/bin
done
for d in opengl32.dll libgallium_wgl.dll libLLVM-21.dll libSPIRV-Tools.dll; do
  cp "$ARBEIT/mesa/ucrt64/bin/$d" "$DIST/bin/mesa/"
done
# Lizenzen wie fork/paket.sh (Hirch CNC, Herkunft der Rechenkerne, Bilder im Begrüssungsfenster) – dazu die Texte
# der DLLs, die unter Windows mitgeliefert werden statt aus der Distribution zu kommen (Basis 30.10.):
# OpenCAMLib (LGPL-2.1+), OpenCASCADE (LGPL-2.1 mit OCCT-Ausnahme), Mesa, LLVM, SPIRV-Tools
L="$DIST/share/hirch-cam/lizenzen"
mkdir -p "$L/opencamlib" "$L/opencascade"
cp "$REPO/LICENSE" "$L/LICENSE"
cp "$REPO/cam_kern/LIZENZEN.md" "$L/cam_kern.md"
cp "$REPO/relief_kern/LIZENZEN.md" "$L/relief_kern.md"
cp "$REPO/assets/willkommen/QUELLEN.md" "$L/willkommen-bilder.md"
cp "$ARBEIT/opencamlib-src/COPYING" "$L/opencamlib/COPYING"
cp "$ARBEIT/occt-src/LICENSE_LGPL_21.txt" "$ARBEIT/occt-src/OCCT_LGPL_EXCEPTION.txt" "$L/opencascade/"
for p in mesa-25.3.3-1 llvm-libs-21.1.8-1 'spirv-tools-3~1.4.335.0-1'; do
  tar --zstd -xf "$(hole "$p")" -C "$ARBEIT/mesa" ucrt64/share/licenses
done
cp -r "$ARBEIT/mesa/ucrt64/share/licenses/"{mesa,llvm-libs,spirv-tools} "$L/"
for d in libsystre-0.dll libtre-5.dll; do
  [ -f "$DIST/bin/$d" ] || cp "/ucrt64/bin/$d" "$DIST/bin/"
done
# Weiche: GTK holt opengl32 sonst schon beim Start aus System32, und Mesa käme zu spät
# («Es ist keine GL-Implementierung verfügbar»). Siehe hirchgl.c.
gcc -O2 -shared -o "$DIST/bin/hirchgl.dll" "$REPO/fork/windows/hirchgl.c" "$REPO/fork/windows/hirchgl.def" -Wl,--kill-at
strip "$DIST/bin/hirchgl.dll"
python "$REPO/fork/windows/gl_weiche.py" "$(cygpath -w "$DIST/bin/libgdk-3-0.dll")"
# Prüfprogramm: kann die Grafikkarte OpenGL 3.3?
gcc -O2 -mwindows "$REPO/fork/windows/gltest.c" -o "$DIST/bin/hirch-gltest.exe" -lgdi32
strip "$DIST/bin/hirch-gltest.exe"

echo "$VERSION" > "$DIST/VERSION"
mkdir -p "$DIST/share/hirch-cam" && echo "$BAU_ID" > "$DIST/share/hirch-cam/BAU"
echo "$VERSION" > "$ARBEIT/VERSION"
mkdir -p "$DIST/share/hirch-cam"
(cd "$REPO" && git log -1 --format=%ct) > "$DIST/share/hirch-cam/ZEIT"

# Kurztest: startet das mitgelieferte Python mit beiden Rechenkernen?
cd "$ARBEIT"   # nicht im Repo, sonst findet Python dessen hirch_cam
PYTHONPATH="$SH/hirch-cam" "$DIST/bin/python.exe" -c \
  "import hirch_cam.engine, hirch_cam.cam as c, hirch_cam.hirch_relief, shapely, scipy, numpy, lxml; print('Rechenkern ok, CAM-Modul:', c.verfuegbar())"
"$DIST/bin/inkscape.com" --version
echo "Fertig: $DIST"
exit 0
}
