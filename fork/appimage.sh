#!/bin/sh
# Hirch CAM als AppImage für Linux (Peter 08.11.2026: auf dem eigenen Notebook bauen, nicht in der Cloud).
# Eine Datei, ohne Installation und ohne Passwort. Gebaut auf Ubuntu 24.04 / Mint 22; läuft auf Systemen mit
# glibc 2.39 oder neuer (Ubuntu 24.04+, Mint 22+, Debian 13, Fedora 40+). glibc und OpenGL kommen vom System.
#
#   sh fork/appimage.sh [BAU-ORDNER]      Standard: ~/bau
#   → BAU-ORDNER/hirch-cam-VERSION-x86_64.AppImage   (HIRCH_RELEASE=1: Version aus der Datei VERSION, wie paket.sh)
#
# Voraussetzung: übersetzt mit fork/bauen.sh (wie für fork/paket.sh), dazu die Pakete aus fork/laufzeit-pakete.txt,
# python3-numpy python3-scipy python3-shapely python3-lxml python3-fonttools python3-pil python3-cssselect
# python3-tinycss2, libocct-*, libopencamlib. appimagetool wird beim ersten Mal nach BAU-ORDNER/werkzeuge geladen.
#
# Ablauf: 1. fork/paket.sh baut den Programmordner (wie /opt/hirch-cam), 2. der kommt nach AppDir/usr,
# 3. Bibliotheken (ldd), GTK-Teile, Python 3.12 mit den nötigen Paketen dazu, 4. AppRun setzt die Umgebung,
# 5. appimagetool packt. Prüfen: «./hirch-cam-….AppImage --selbsttest».
set -e
REPO="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
BAU="${1:-$HOME/bau}"
APPDIR="$BAU/AppDir"
U="$APPDIR/usr"
ARCH_LIB=/usr/lib/x86_64-linux-gnu
PY=python3.12

command -v "$PY" >/dev/null || { echo "$PY fehlt" >&2; exit 1; }
command -v patchelf >/dev/null || { echo "patchelf fehlt: sudo apt install patchelf" >&2; exit 1; }

# 1. Programmordner wie im Paket
sh "$REPO/fork/paket.sh" "$BAU" >/dev/null
VERSION="$(cat "$BAU/paket/opt/hirch-cam/VERSION")"

# 2. AppDir
rm -rf "$APPDIR"
mkdir -p "$U"
cp -a "$BAU/paket/opt/hirch-cam/." "$U/"
# Aktualisieren mit Passwort (pkexec, /opt) gibt es im AppImage nicht
rm -f "$U/bin/hirch-cam-aktualisieren" "$U/bin/hirch-cnc"

# 3a. Python 3.12 mit Standardbibliothek und den Paketen, die hirch_cam und die Inkscape-Erweiterungen brauchen
mkdir -p "$U/lib/python3/dist-packages"
cp -L "$(command -v $PY)" "$U/bin/$PY"
ln -sf "$PY" "$U/bin/python3"
tar -C /usr/lib -cf - --exclude="$PY/test" --exclude="$PY/idlelib" --exclude="$PY/tkinter" \
    --exclude="$PY/turtledemo" --exclude="$PY/ensurepip" --exclude="$PY/lib2to3" --exclude='__pycache__' \
    --exclude="$PY/config-*" "$PY" | tar -C "$U/lib" -xf -
for p in numpy scipy shapely lxml fontTools PIL cssselect tinycss2 webencodings; do
  [ -d "/usr/lib/python3/dist-packages/$p" ] || { echo "Python-Paket fehlt: $p" >&2; exit 1; }
  tar -C /usr/lib/python3/dist-packages -cf - --exclude='__pycache__' --exclude='tests' --exclude='test' "$p" |
    tar -C "$U/lib/python3/dist-packages" -xf -
done

# vorübersetzt: das AppImage ist schreibgeschützt, ohne .pyc dauert jeder Aufruf des Rechenkerns doppelt so lange
"$PY" -m compileall -q -j 0 --invalidation-mode unchecked-hash "$U/lib/$PY" "$U/lib/python3/dist-packages" \
    "$U/share/inkscape/hirch-cam" "$U/share/inkscape/extensions" >/dev/null 2>&1 || true

# 3b. GTK-Teile: Bildlader, Eingabemethoden, Schemas, Symbole
mkdir -p "$U/lib/gdk-pixbuf-2.0/2.10.0" "$U/lib/gtk-3.0/3.0.0" "$U/share/glib-2.0/schemas" "$U/share/icons"
cp -a "$ARCH_LIB/gdk-pixbuf-2.0/2.10.0/loaders" "$U/lib/gdk-pixbuf-2.0/2.10.0/"
rm -f "$U"/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.a
cp -L "$ARCH_LIB/gdk-pixbuf-2.0/gdk-pixbuf-query-loaders" "$U/bin/"
cp -a "$ARCH_LIB/gtk-3.0/3.0.0/immodules" "$U/lib/gtk-3.0/3.0.0/" 2>/dev/null || true
cp /usr/share/glib-2.0/schemas/*.xml "$U/share/glib-2.0/schemas/" 2>/dev/null || true
cp "$U"/share/inkscape/*.gschema.xml "$U/share/glib-2.0/schemas/" 2>/dev/null || true
glib-compile-schemas "$U/share/glib-2.0/schemas"
for t in Adwaita hicolor; do
  [ -d "/usr/share/icons/$t" ] && cp -a "/usr/share/icons/$t" "$U/share/icons/" 2>/dev/null || true
done

# 3c. Bibliotheken: alles, was die Programme und Module laden (ldd), ausser glibc, OpenGL und
# Teilen, die zum Grafiksystem des Rechners passen müssen (wie die Ausschlussliste von AppImage)
NICHT='^(ld-linux|libc|libm|libdl|libpthread|librt|libresolv|libutil|libnsl|libanl|libmvec|libGL|libGLX|libEGL|libGLdispatch|libOpenGL|libGLESv2|libdrm|libgbm|libxcb|libX11|libX11-xcb|libasound|libfontconfig|libfreetype|libharfbuzz|libgcc_s|libstdc\+\+|libudev|libwayland-client|libwayland-cursor|libwayland-egl)\.so'
LISTE="$BAU/appimage-libs.txt"
: > "$LISTE"
find "$U" -type f \( -name '*.so' -o -name '*.so.*' -o -perm -u+x \) | while read -r f; do
  file -b "$f" | grep -q '^ELF' || continue
  LD_LIBRARY_PATH="$U/lib:$U/lib/inkscape" ldd "$f" 2>/dev/null | awk '/=> \//{print $3}' >> "$LISTE"
done
mkdir -p "$U/lib"
sort -u "$LISTE" | while read -r l; do
  case "$l" in "$APPDIR"/*) continue ;; esac
  n="$(basename "$l")"
  echo "$n" | grep -Eq "$NICHT" && continue
  [ -e "$U/lib/$n" ] || cp -L "$l" "$U/lib/$n"
done
# zweite Runde: Abhängigkeiten der eben kopierten Bibliotheken
for runde in 1 2; do
  for f in "$U"/lib/*.so*; do
    LD_LIBRARY_PATH="$U/lib" ldd "$f" 2>/dev/null | awk '/=> \//{print $3}'
  done | sort -u | while read -r l; do
    case "$l" in "$APPDIR"/*) continue ;; esac
    n="$(basename "$l")"
    echo "$n" | grep -Eq "$NICHT" && continue
    [ -e "$U/lib/$n" ] || cp -L "$l" "$U/lib/$n"
  done
done
# Inkscape und die Module finden die Bibliotheken neben sich
patchelf --set-rpath '$ORIGIN/../lib:$ORIGIN/../lib/inkscape' "$U/bin/inkscape"
patchelf --set-rpath '$ORIGIN/../lib:$ORIGIN' "$U/lib/inkscape/libinkscape_base.so"

# 4. Start
cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
# Hirch CAM (AppImage): Umgebung setzen und starten
HIER="$(dirname "$(readlink -f "$0")")"
U="$HIER/usr"
export HIRCH_APPIMAGE="${APPIMAGE:-$HIER}"
export INKSCAPE_PROFILE_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/hirch-cam/profil"
export HIRCH_CAM_REPO="$U/share/inkscape/hirch-cam"
export XDG_DATA_DIRS="$U/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export GSETTINGS_SCHEMA_DIR="$U/share/glib-2.0/schemas"
export GTK_PATH="$U/lib/gtk-3.0"
export GTK_IM_MODULE_FILE="$U/lib/gtk-3.0/3.0.0/immodules.cache"
export PATH="$U/bin:$PATH"
# Python im AppImage, nie das des Systems (dort fehlen die Rechenkerne)
export PYTHONHOME="$U"
export PYTHONPATH="$HIRCH_CAM_REPO:$U/lib/python3/dist-packages:$U/share/inkscape/extensions"
export PYTHONNOUSERSITE=1
export PYTHONDONTWRITEBYTECODE=1
export HIRCH_PYTHON="$U/bin/python3.12"
export LD_LIBRARY_PATH="$U/lib:$U/lib/inkscape${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Bildlader: die Liste enthält feste Pfade, darum je Start neu (schnell)
C="${XDG_CACHE_HOME:-$HOME/.cache}/hirch-cam"
mkdir -p "$C"
export GDK_PIXBUF_MODULEDIR="$U/lib/gdk-pixbuf-2.0/2.10.0/loaders"
export GDK_PIXBUF_MODULE_FILE="$C/loaders.cache"
"$U/bin/gdk-pixbuf-query-loaders" > "$C/loaders.cache" 2>/dev/null
[ -d "$U/lib/gtk-3.0/3.0.0/immodules" ] && \
  "$U/bin/gtk-query-immodules-3.0" "$U"/lib/gtk-3.0/3.0.0/immodules/*.so > "$C/immodules.cache" 2>/dev/null && \
  export GTK_IM_MODULE_FILE="$C/immodules.cache"

if [ "$1" = "--selbsttest" ]; then
  # Rechenkerne laden, Musterzeichnung rechnen (ohne Fenster)
  exec "$HIRCH_PYTHON" -c '
import sys, hirch_cam, hirch_cam.hirch_cam, numpy, scipy, shapely, lxml
try:
    import hirch_cam.hirch_relief
    r = "3D ok"
except ImportError as e:
    r = "3D fehlt: %s" % e
print("Hirch CAM", open(sys.argv[1]).read().strip(), "– Python", sys.version.split()[0], "– 2D ok –", r)
' "$U/VERSION"
fi

# vorgegebene Dateien ins Profil übernehmen (einmal je Version)
"$HIRCH_PYTHON" -m hirch_cam.profil "$INKSCAPE_PROFILE_DIR" "$(cat "$U/VERSION" 2>/dev/null)" >/dev/null 2>&1 || true

# Dateityp .hir und Startmenü für diesen Benutzer anmelden (zeigt auf diese AppImage-Datei)
if [ -n "$APPIMAGE" ]; then
  D="${XDG_DATA_HOME:-$HOME/.local/share}"
  mkdir -p "$D/applications" "$D/mime/packages" "$D/icons/hicolor/scalable/apps"
  NEU="$C/hirch-cam.desktop"
  sed "s|^Exec=.*|Exec=\"$APPIMAGE\" %F|" "$U/share/applications/net.hirch.HirchCAM.desktop" > "$NEU"
  if ! cmp -s "$NEU" "$D/applications/net.hirch.HirchCAM.desktop" 2>/dev/null; then
    cp "$NEU" "$D/applications/net.hirch.HirchCAM.desktop"
    cp "$U/share/mime/packages/hirch-cam.xml" "$D/mime/packages/" 2>/dev/null
    cp "$U/share/icons/hicolor/scalable/apps/hirch-cam.svg" "$D/icons/hicolor/scalable/apps/" 2>/dev/null
    ( command -v update-mime-database >/dev/null && update-mime-database "$D/mime"
      command -v update-desktop-database >/dev/null && update-desktop-database -q "$D/applications"
      command -v xdg-mime >/dev/null && xdg-mime default net.hirch.HirchCAM.desktop application/x-hirch-cam
    ) >/dev/null 2>&1 &
  fi
fi
exec "$U/bin/inkscape" "$@"
EOF
chmod +x "$APPDIR/AppRun"
cp -L "$ARCH_LIB/libgtk-3-0t64/gtk-query-immodules-3.0" "$U/bin/" 2>/dev/null || \
  cp -L "$(command -v gtk-query-immodules-3.0)" "$U/bin/" 2>/dev/null || true
cp "$U/share/applications/net.hirch.HirchCAM.desktop" "$APPDIR/hirch-cam.desktop"
sed -i 's|^Exec=.*|Exec=hirch-cam %F|' "$APPDIR/hirch-cam.desktop"
cp "$REPO/fork/share/icons/hirch-cam.svg" "$APPDIR/hirch-cam.svg"
ln -sf hirch-cam.svg "$APPDIR/.DirIcon"

# 5. Packen (HIRCH_NUR_APPDIR=1: nur den Ordner bauen und ihn direkt prüfen, ohne appimagetool)
if [ -n "$HIRCH_NUR_APPDIR" ]; then
  "$APPDIR/AppRun" --selbsttest
  exit 0
fi
WZ="$BAU/werkzeuge"
TOOL="$WZ/appimagetool-x86_64.AppImage"
if [ ! -x "$TOOL" ]; then
  mkdir -p "$WZ"
  curl -fsSL -o "$TOOL" https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
  chmod +x "$TOOL"
fi
ZIEL="$BAU/hirch-cam-$VERSION-x86_64.AppImage"
rm -f "$ZIEL"
ARCH=x86_64 "$TOOL" --appimage-extract-and-run --no-appstream "$APPDIR" "$ZIEL" >/dev/null
ls -lh "$ZIEL"
"$ZIEL" --appimage-extract-and-run --selbsttest
