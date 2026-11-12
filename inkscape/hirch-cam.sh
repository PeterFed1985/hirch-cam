#!/bin/sh
# Startet Inkscape als "Hirch CNC" mit eigenem Profil.
# Das normale Inkscape-Profil bleibt unberührt.
set -e
REPO="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"

# Inkscape suchen: Umgebungsvariable, fester Ort, Gear-Lever-Ordner, System
INKSCAPE="${HIRCH_INKSCAPE:-}"
if [ -z "$INKSCAPE" ]; then
  for k in "$HOME/Programme/inkscape.AppImage" "$HOME"/AppImages/[Ii]nkscape*; do
    if [ -x "$k" ]; then INKSCAPE="$k"; break; fi
  done
fi
[ -n "$INKSCAPE" ] || INKSCAPE="$(command -v inkscape || true)"
if [ -z "$INKSCAPE" ]; then
  echo "Hirch CNC: Inkscape nicht gefunden (erwartet: ~/Programme/inkscape.AppImage)" >&2
  exit 1
fi

PROFIL="${XDG_CONFIG_HOME:-$HOME/.config}/hirch-cam/inkscape"
mkdir -p "$PROFIL/extensions" "$PROFIL/palettes" "$PROFIL/templates"
# Verknüpfungen ins Repo: Änderungen im Repo wirken sofort
ln -sfn "$REPO/inkscape/extensions" "$PROFIL/extensions/hirch-cam"
ln -sfn "$REPO/inkscape/profile/palettes/hirch-cam.gpl" "$PROFIL/palettes/hirch-cam.gpl"
ln -sfn "$REPO/inkscape/profile/templates/default.svg" "$PROFIL/templates/default.svg"
export INKSCAPE_PROFILE_DIR="$PROFIL"

# Einstellungen: Beim ersten Start legt Inkscape die vollständige Datei selbst
# an (ohne Fenster). einstellungen.py setzt dann die Fräsfarben-Palette und
# schaltet bei jedem Start den Willkommensbildschirm ab (auch nach Updates).
# Eine unvollständige Datei aus einer frühen Version wird ersetzt.
PREFS="$PROFIL/preferences.xml"
if [ -f "$PREFS" ] && grep -q "Startwerte für das Hirch-CNC-Profil" "$PREFS"; then
  rm -f "$PREFS"
fi
ERST=""
if [ ! -f "$PREFS" ]; then
  "$INKSCAPE" --action-list >/dev/null 2>&1 || true
  ERST="--erststart"
fi
VERSION="$("$INKSCAPE" --version 2>/dev/null | awk '/^Inkscape/ {print $2; exit}')"
if [ -f "$PREFS" ] && command -v python3 >/dev/null; then
  python3 "$REPO/inkscape/einstellungen.py" "$PREFS" "${VERSION:-0}" $ERST || true
fi

export HIRCH_CAM_REPO="$REPO"
exec "$INKSCAPE" "$@"
