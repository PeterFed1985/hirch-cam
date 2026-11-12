#!/bin/sh
# Hirch CAM installieren – Linux Mint 22 / Ubuntu 24.04 (x86_64).
#
#   ./installieren.sh                 Archiv suchen (Repo-Ordner, ~/Downloads)
#   ./installieren.sh ARCHIV.tar.xz   bestimmtes Archiv installieren
#
# Das Archiv (hirch-cam-…-linux-x86_64.tar.xz) gibt es unter
# https://github.com/PeterFed1985/hirch-cnc/releases – im Browser laden,
# es darf im Ordner Downloads liegen bleiben.
#
# Was passiert:
#   1. Pakete installieren, die Hirch CAM braucht (apt, fragt nach dem Passwort)
#   2. Programm nach /opt/hirch-cam entpacken
#   3. Eintrag «Hirch CAM» im Anwendungsmenü
#   4. Aufräumen: FreeCAD aus früheren Versionen wird nicht mehr gebraucht
#      (das CAM-Modul hirch_cam ist jetzt im Programm enthalten)
set -e
REPO="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
ZIEL=/opt/hirch-cam

archiv_suchen() {
  # seit 03.11.2026 hirch-cam-…, ältere Archive hirch-cnc-…
  for k in "$REPO"/hirch-cam-*-linux-x86_64.tar.xz "$HOME"/Downloads/hirch-cam-*-linux-x86_64.tar.xz \
           "$HOME"/Projekte/hirch_CAM/hirch-cam-*-linux-x86_64.tar.xz "$HOME"/bau/hirch-cam-*-linux-x86_64.tar.xz \
           "$REPO"/hirch-cnc-*-linux-x86_64.tar.xz "$HOME"/Downloads/hirch-cnc-*-linux-x86_64.tar.xz \
           "$HOME"/Projekte/hirch_CAM/hirch-cnc-*-linux-x86_64.tar.xz "$HOME"/bau/hirch-cnc-*-linux-x86_64.tar.xz; do
    [ -f "$k" ] && echo "$k"
  done | xargs -r ls -t 2>/dev/null | head -n 1
}

ARCHIV="${1:-$(archiv_suchen)}"
if [ -z "$ARCHIV" ] || [ ! -f "$ARCHIV" ]; then
  echo "Kein Programm-Archiv gefunden."
  echo "Bitte hirch-cam-…-linux-x86_64.tar.xz von"
  echo "  https://cam.hirch.net"
  echo "herunterladen (landet in ~/Downloads) und diesen Befehl erneut starten."
  exit 1
fi
echo "Archiv: $ARCHIV"
echo

echo "1/4  Pakete installieren …"
PAKETE="$(grep -v '^#' "$REPO/fork/laufzeit-pakete.txt" | tr '\n' ' ')"
# shellcheck disable=SC2086
sudo apt-get install -y --no-install-recommends $PAKETE \
     python3-shapely python3-fonttools python3-olefile python3-scipy python3-numpy python3-lxml python3-cssselect python3-tinycss2 \
     xz-utils

echo "2/4  Programm nach $ZIEL entpacken …"
TMP="$(mktemp -d)"
tar -C "$TMP" -xJf "$ARCHIV"
sudo rm -rf "$ZIEL.alt"
[ -d "$ZIEL" ] && sudo mv "$ZIEL" "$ZIEL.alt"
# im Archiv heisst der Ordner «hirch-cnc» (Übergang) oder «hirch-cam»
if [ -d "$TMP/hirch-cam" ]; then sudo mv "$TMP/hirch-cam" "$ZIEL"; else sudo mv "$TMP/hirch-cnc" "$ZIEL"; fi
sudo chown -R root:root "$ZIEL"
rmdir "$TMP"
sudo rm -rf "$ZIEL.alt"
# Umzug Stufe 2 (04.11.2026): der alte Programmordner wird nicht mehr gebraucht (Einstellungen liegen nicht dort)
[ -d /opt/hirch-cnc ] && [ ! -L /opt/hirch-cnc ] && sudo rm -rf /opt/hirch-cnc

echo "3/4  Eintrag im Anwendungsmenü …"
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
ICONS="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/scalable/apps"
mkdir -p "$APPS" "$ICONS"
cp "$ZIEL/share/applications/net.hirch.HirchCAM.desktop" "$APPS/"
cp "$ZIEL/share/icons/hicolor/scalable/apps/hirch-cam.svg" "$ICONS/"
rm -f "$APPS/hirch-cnc.desktop" "$APPS/net.hirch.HirchCNC.desktop"   # frühere Starter (AppImage, vor dem Umzug)
command -v update-desktop-database >/dev/null && update-desktop-database -q "$APPS" || true
command -v gtk-update-icon-cache >/dev/null && \
  gtk-update-icon-cache -q -t "${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor" 2>/dev/null || true

echo "4/4  CAM-Modul prüfen …"
DATEN="${XDG_DATA_HOME:-$HOME/.local/share}/hirch-cnc"   # Reste der frühen FreeCAD-Fassung (alter Name)
for alt in "$DATEN/freecad-cam" "$DATEN/freecad"; do
  if [ -d "$alt" ]; then
    rm -rf "$alt"
    echo "     $alt entfernt (FreeCAD wird nicht mehr gebraucht)."
  fi
done
( cd "$ZIEL/share/inkscape/hirch-cam" && python3 -c "
from hirch_cam import cam
print('     CAM-Modul', cam.version()) if cam.verfuegbar() else print('     CAM-Modul fehlt – es rechnet der eigene Kern.')" ) || true

echo
"$ZIEL/bin/inkscape" --version 2>/dev/null | head -n 1 | sed 's/^Inkscape/Grundlage: Inkscape/' || true
echo "Hirch CAM $(cat "$ZIEL/VERSION") ist installiert."
echo "Starten über das Anwendungsmenü «Hirch CAM» oder mit: $ZIEL/bin/hirch-cam"
