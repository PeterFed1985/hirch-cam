#!/bin/sh
# Prüft, ob Hirch CNC vollständig eingerichtet ist, und nennt die Befehle
# zum Beheben.   ./pruefen.sh
REPO="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
ZIEL=/opt/hirch-cam
FEHLT=0
ok()      { printf '  \033[32m✓\033[0m %s\n' "$1"; }
fehlt()   { printf '  \033[31m✗\033[0m %s\n      → %s\n' "$1" "$2"; FEHLT=$((FEHLT + 1)); }
hinweis() { printf '  \033[33m!\033[0m %s\n      → %s\n' "$1" "$2"; }

echo "Hirch CNC – Einrichtung prüfen"
echo

if [ -x "$ZIEL/bin/inkscape" ] && "$ZIEL/bin/inkscape" --version >/dev/null 2>&1; then
  ok "Programm $(cat "$ZIEL/VERSION" 2>/dev/null) in $ZIEL"
else
  fehlt "Programm fehlt oder startet nicht ($ZIEL)" "$REPO/installieren.sh"
fi

if python3 -c "import shapely, numpy, scipy" 2>/dev/null; then
  ok "Python-Rechenkern (Shapely, NumPy, SciPy)"
else
  fehlt "Python-Pakete fehlen" "sudo apt install python3-shapely python3-numpy python3-scipy"
fi
if PYTHONPATH="$ZIEL/share/inkscape/extensions" python3 -c "import inkex" 2>/dev/null; then
  ok "Zeichnung lesen (inkex)"
else
  fehlt "inkex startet nicht" "sudo apt install python3-lxml python3-cssselect python3-tinycss2"
fi

CAM="$(cd "$ZIEL/share/inkscape/hirch-cam" 2>/dev/null && python3 -c "
from hirch_cam import cam
print(cam.version() if cam.verfuegbar() else '')" 2>/dev/null)"
if [ -n "$CAM" ]; then
  ok "CAM-Modul hirch_cam $CAM (libarea/Voronoi aus FreeCAD CAM)"
else
  hinweis "CAM-Modul fehlt – es rechnet der eigene Kern" "$REPO/installieren.sh"
fi
if [ -d "${XDG_DATA_HOME:-$HOME/.local/share}/hirch-cam/freecad-cam" ]; then
  hinweis "Alter FreeCAD-CAM-Kern liegt noch da (400 MB, nicht mehr gebraucht)" \
          "rm -rf ${XDG_DATA_HOME:-$HOME/.local/share}/hirch-cam/freecad-cam"
fi

if [ -f "${XDG_DATA_HOME:-$HOME/.local/share}/applications/net.hirch.HirchCAM.desktop" ]; then
  ok "Eintrag «Hirch CNC» im Anwendungsmenü"
else
  hinweis "Eintrag im Anwendungsmenü fehlt" "$REPO/installieren.sh"
fi

echo
if [ "$FEHLT" -eq 0 ]; then
  echo "Alles bereit. Starten über das Menü «Hirch CNC» oder: $ZIEL/bin/hirch-cam"
else
  echo "$FEHLT Punkt(e) fehlen noch – siehe oben."
fi
