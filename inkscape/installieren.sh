#!/bin/sh
# Legt einen Starter "Hirch CNC" im Anwendungsmenü an (nur für diesen Benutzer).
set -e
REPO="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
chmod +x "$REPO/inkscape/hirch-cam.sh"
ZIEL="${XDG_DATA_HOME:-$HOME/.local/share}/applications/hirch-cam.desktop"
mkdir -p "$(dirname "$ZIEL")"
cat > "$ZIEL" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Hirch CNC
Comment=Fräsaufträge zeichnen und als .tap ausgeben
Exec="$REPO/inkscape/hirch-cam.sh" %F
Icon=$REPO/assets/logo.svg
Terminal=false
Categories=Graphics;Engineering;
MimeType=image/svg+xml;
StartupWMClass=org.inkscape.Inkscape
DESKTOP
update-desktop-database "$(dirname "$ZIEL")" 2>/dev/null || true
echo "Starter angelegt: $ZIEL"
echo
"$REPO/inkscape/pruefen.sh"
