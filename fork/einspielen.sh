#!/bin/sh
# Hirch-CNC-Änderungen in einen Inkscape-Quellbaum einspielen (Linux: fork/bauen.sh, Windows: fork/windows/bauen.sh).
#   sh fork/einspielen.sh <Inkscape-Quellordner>
# 1. die Teil-Patches fork/patches/NN-*.patch der Reihe nach (je Thema einer; Übersicht in docs/INKSCAPE-AENDERUNGEN.md),
# 2. po-de.patch im Untermodul po/ (deutsche Menütexte),
# 3. ganze Dateien aus fork/inkscape/ über die gleichnamigen in Inkscape (Menüs, Werkzeugleisten, Tastenkürzel).
# Schon Eingespieltes wird erkannt; passt ein Patch nicht, bricht es ab (Quellordner mit 'git checkout .' zurücksetzen).
# Die eigenen Quellen fork/src/ verlinkt bzw. kopiert jedes Bauskript selbst.
set -e
QUELLE="$1"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
[ -d "$QUELLE/.git" ] || { echo "Kein Inkscape-Quellordner: $QUELLE" >&2; exit 1; }
cd "$QUELLE"
for p in "$REPO"/fork/patches/[0-9][0-9]-*.patch; do
  [ -f "$p" ] || continue
  n=$(basename "$p")
  if git apply --check "$p" 2>/dev/null; then
    git apply "$p"
    echo "$n eingespielt."
  elif git apply --reverse --check "$p" 2>/dev/null; then
    echo "$n ist schon eingespielt."
  else
    echo "$n passt nicht – Quellordner mit 'git checkout .' zurücksetzen." >&2
    exit 1
  fi
done
if [ -f "$REPO/fork/patches/po-de.patch" ]; then
  if (cd po && git apply --check "$REPO/fork/patches/po-de.patch" 2>/dev/null); then
    (cd po && git apply "$REPO/fork/patches/po-de.patch") && echo "Deutsche Menütexte eingespielt."
  elif ! (cd po && git apply --reverse --check "$REPO/fork/patches/po-de.patch" 2>/dev/null); then
    echo "po-de.patch passt nicht – po/ mit 'git -C po checkout .' zurücksetzen." >&2
    exit 1
  fi
fi
# Ganze Dateien (kopiert, nicht verlinkt – unter Windows gehen Verknüpfungen nicht zuverlässig);
# cmp: unveränderte nicht anfassen, damit ninja nichts neu übersetzt
( cd "$REPO/fork/inkscape" && find . -type f ) | while read -r f; do
  cmp -s "$REPO/fork/inkscape/$f" "$QUELLE/$f" || cp "$REPO/fork/inkscape/$f" "$QUELLE/$f"
done
echo "Ganze Dateien aus fork/inkscape/ übernommen."
