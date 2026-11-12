#!/bin/sh
# Rechenkern-Update: nur der Python-Teil von Hirch CNC (hirch_cam samt
# CAM-Modul für Python 3.12) – ein paar MB statt des ganzen Programms.
#
#   fork/kern-paket.sh [ZIELORDNER]     Standard: ~/bau
#   → ZIELORDNER/hirch-cam-kern-VERSION.tar.xz
#
# «Fräsen → Hirch CNC aktualisieren …» packt es nach ~/.config/hirch-cam/kern
# (ohne Passwort). Es gilt nur, solange es neuer ist als das installierte
# Programm (Datei ZEIT = Zeitpunkt des Commits); ein neueres Programm-Update
# geht automatisch wieder vor.
set -e
REPO="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
ZIEL="${1:-$HOME/bau}"
VERSION="$(cd "$REPO" && git log -1 --date=format:%Y.%m.%d --format=%cd)-$(cd "$REPO" && git rev-parse --short HEAD)"
ZEIT="$(cd "$REPO" && git log -1 --format=%ct)"
TMP="$(mktemp -d)"
K="$TMP/hirch-cam-kern"
mkdir -p "$K"
cp -r "$REPO/hirch_cam" "$K/"
find "$K" -name "__pycache__" -type d -prune -exec rm -rf {} +
PY_SO="hirch_cam.cpython-312-x86_64-linux-gnu.so"
[ -f "$REPO/hirch_cam/$PY_SO" ] || sh "$REPO/cam_kern/bauen.sh" python3.12
for so in "$K"/hirch_cam/hirch_cam.*.so; do
  [ "$(basename "$so")" = "$PY_SO" ] || rm -f "$so"
done
[ -f "$K/hirch_cam/$PY_SO" ] || { echo "CAM-Modul fehlt: $PY_SO" >&2; exit 1; }
echo "$VERSION" > "$K/VERSION"
echo "$ZEIT" > "$K/ZEIT"
ARCHIV="$ZIEL/hirch-cam-kern-$VERSION.tar.xz"
tar -C "$TMP" -cf - hirch-cam-kern | xz -T0 -6 > "$ARCHIV"
rm -rf "$TMP"
ls -lh "$ARCHIV"
