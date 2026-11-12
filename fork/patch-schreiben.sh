#!/bin/sh
# Die Teil-Patches neu schreiben, nachdem im Inkscape-Quellbaum etwas geändert wurde (nur Linux-Basis).
#   sh fork/patch-schreiben.sh <Inkscape-Quellordner>
# Jede geänderte Inkscape-Datei gehört zu genau einem Thema (Liste unten). Ganze Dateien unter fork/inkscape/
# werden aus dem Baum zurückkopiert. Eine geänderte Datei ohne Thema: Abbruch – erst unten eintragen.
set -e
QUELLE="$1"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
P="$REPO/fork/patches"
cd "$QUELLE"

thema() {
  case "$1" in
    src/ui/CMakeLists.txt|src/libnrtype/CMakeLists.txt|share/keys/CMakeLists.txt|share/CMakeLists.txt) echo 01-bau ;;
    src/inkscape-main.cpp|src/inkscape.cpp|src/inkscape-application.cpp|src/inkscape-window.cpp|\
    src/preferences-skeleton.h|src/ui/desktop/menubar.cpp|src/ui/dialog/dialog-data.cpp|\
    src/ui/dialog/dialog-container.cpp|src/ui/dialog/dialog-base.*|src/ui/dialog/dialog-notebook.cpp) echo 02-start ;;
    src/ui/widget/desktop-widget.*|src/ui/widget/canvas-grid.*|src/ui/widget/status-bar.*|\
    src/ui/widget/page-selector.cpp|src/ui/contextmenu.cpp|src/ui/dialog/color-item.cpp|\
    src/ui/dialog/transformation.cpp|src/ui/dialog/tile.cpp|src/ui/shortcuts.cpp|src/ui/dialog/livepatheffect-editor.*|src/ui/dialog/inkscape-preferences.cpp|src/ui/dialog/svg-preview.cpp|\
    src/live_effects/lpe-offset.cpp|src/live_effects/lpe-copy_rotate.cpp|src/live_effects/lpe-patternalongpath.cpp)
      echo 03-oberflaeche ;;
    src/ui/toolbar/*|src/actions/actions-tools.cpp|src/ui/tools/booleans-tool.cpp|src/ui/widget/font-selector.*|src/ui/dialog/glyphs.cpp|src/ui/widget/combo-box-entry-tool-item.*|src/libnrtype/font-lister.*|src/ui/tools/select-tool.cpp)
      echo 04-werkzeugleisten ;;
    src/display/*|src/seltrans.*|src/selection.*|src/selection-chemistry.cpp|src/page-manager.*|\
    src/object/sp-page.cpp|src/object/sp-namedview.cpp|src/document.h|src/document-undo.*|src/live_effects/parameter/point.cpp|\
    src/actions/actions-object-align.cpp) echo 05-zeichnen ;;
    src/extension/*|src/file.cpp|src/xml/repr-util.cpp|src/ui/dialog/filedialog*|src/helper/choose-file.cpp) echo 06-datei ;;
    *) echo "" ;;
  esac
}

# ganze Dateien zurückholen
( cd "$REPO/fork/inkscape" && find . -type f ) | while read -r f; do
  cmp -s "$QUELLE/$f" "$REPO/fork/inkscape/$f" || cp "$QUELLE/$f" "$REPO/fork/inkscape/$f"
done
ganz=$(cd "$REPO/fork/inkscape" && find . -type f | sed 's|^\./||')

fehlt=0
rm -f "$P"/[0-9][0-9]-*.liste
for f in $(git diff --name-only -- . ':!po' ':!src/3rdparty/2geom'); do
  case " $(echo $ganz) " in *" $f "*) continue ;; esac
  t=$(thema "$f")
  if [ -z "$t" ]; then
    echo "Kein Thema für $f – in fork/patch-schreiben.sh eintragen." >&2
    fehlt=1
  else
    echo "$f" >> "$P/$t.liste"
  fi
done
[ $fehlt = 0 ] || exit 1
rm -f "$P"/[0-9][0-9]-*.patch
for l in "$P"/[0-9][0-9]-*.liste; do
  [ -f "$l" ] || continue
  t=$(basename "$l" .liste)
  git diff -- $(cat "$l") > "$P/$t.patch"
  rm -f "$l"
  echo "$t.patch: $(grep -c '^diff --git' "$P/$t.patch") Dateien"
done
