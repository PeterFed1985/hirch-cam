#!/bin/sh
# Prüft, ob alles für Hirch CNC eingerichtet ist, und nennt die Befehle
# zum Beheben. Läuft am Ende von installieren.sh, kann aber jederzeit
# einzeln aufgerufen werden:  ~/Projekte/hirch-cnc/inkscape/pruefen.sh
REPO="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
FEHLT=0

ok()   { printf '  \033[32m✓\033[0m %s\n' "$1"; }
fehlt() { printf '  \033[31m✗\033[0m %s\n      → %s\n' "$1" "$2"; FEHLT=$((FEHLT + 1)); }
hinweis() { printf '  \033[33m!\033[0m %s\n      → %s\n' "$1" "$2"; }

echo "Hirch CNC – Einrichtung prüfen"
echo

# Inkscape (gleiche Suche wie hirch-cam.sh)
INKSCAPE="${HIRCH_INKSCAPE:-}"
if [ -z "$INKSCAPE" ]; then
  for k in "$HOME/Programme/inkscape.AppImage" "$HOME"/AppImages/[Ii]nkscape*; do
    if [ -x "$k" ]; then INKSCAPE="$k"; break; fi
  done
fi
[ -n "$INKSCAPE" ] || INKSCAPE="$(command -v inkscape || true)"
if [ -n "$INKSCAPE" ]; then
  V="$("$INKSCAPE" --version 2>/dev/null | awk '/^Inkscape/ {print $2; exit}')"
  case "$V" in
    1.[4-9]*|[2-9].*) ok "Inkscape $V ($INKSCAPE)" ;;
    "") fehlt "Inkscape startet nicht ($INKSCAPE)" "Datei prüfen: chmod +x \"$INKSCAPE\"" ;;
    *) hinweis "Inkscape $V ist älter als 1.4" "AppImage 1.4 oder neuer nach ~/Programme/inkscape.AppImage" ;;
  esac
else
  fehlt "Inkscape nicht gefunden" "AppImage laden und speichern als ~/Programme/inkscape.AppImage (chmod +x)"
fi

# System-Python mit Shapely und NumPy
if command -v python3 >/dev/null; then
  ok "Python $(python3 -c 'import sys; print(sys.version.split()[0])')"
  if python3 -c "import shapely" 2>/dev/null; then
    ok "Shapely $(python3 -c 'import shapely; print(shapely.__version__)')"
  else
    fehlt "Shapely fehlt (für Kontur, Tasche, V-Carve)" "sudo apt install python3-shapely"
  fi
  if python3 -c "import numpy" 2>/dev/null; then
    ok "NumPy (für die Prüfsimulation)"
  else
    hinweis "NumPy fehlt (nur für Tests nötig)" "sudo apt install python3-numpy"
  fi
else
  fehlt "python3 nicht gefunden" "sudo apt install python3"
fi

# FreeCAD (nur für V-Carve)
FC="$(cd "$REPO" && python3 -c "
from hirch_cam.freecad_lauf import finde_freecadcmd
c = finde_freecadcmd()
print(' '.join(c) if c else '')" 2>/dev/null)"
if [ -n "$FC" ]; then
  ok "FreeCAD-Rechenkern: $FC"
else
  hinweis "FreeCAD (Rechenkern) fehlt – es rechnet der eigene Kern" \
          "$REPO/werkzeuge/freecad_holen.sh"
fi

# Browser für die 3D-Vorschau
if command -v xdg-open >/dev/null; then
  ok "xdg-open (öffnet die 3D-Vorschau im Browser)"
else
  hinweis "xdg-open fehlt – die Vorschau öffnet sich nicht von selbst" "sudo apt install xdg-utils"
fi

# Starter im Menü
if [ -f "${XDG_DATA_HOME:-$HOME/.local/share}/applications/hirch-cam.desktop" ]; then
  ok "Starter «Hirch CNC» im Anwendungsmenü"
else
  hinweis "Starter im Menü fehlt" "$REPO/inkscape/installieren.sh"
fi

echo
if [ "$FEHLT" -eq 0 ]; then
  echo "Alles bereit. Starten über das Menü «Hirch CNC» oder: $REPO/inkscape/hirch-cam.sh"
else
  echo "$FEHLT Punkt(e) fehlen noch – siehe oben."
fi
exit 0
