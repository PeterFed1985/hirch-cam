#!/bin/sh
# Hirch CAM aktualisieren – als root über pkexec aufgerufen vom Menü
# «Fräsen → Hirch CAM aktualisieren …».
#
#   hirch-cam-aktualisieren ARCHIV.tar.xz
#   hirch-cam-aktualisieren --umzug        (einmal: /opt/hirch-cnc → /opt/hirch-cam)
#
# Entpackt das Archiv neben /opt/hirch-cam, installiert fehlende Pakete
# (ohne Internet geht es trotzdem weiter) und tauscht den Programmordner
# in einem Schritt aus. Ein laufendes Hirch CAM läuft mit den alten Dateien
# weiter, bis es neu gestartet wird.
#
# Umzug Stufe 2 (04.11.2026): Der Programmordner heisst /opt/hirch-cam (vorher /opt/hirch-cnc). Im Archiv heisst
# der Ordner für den Übergang noch «hirch-cnc» (ältere Versionen erwarten das) – genommen wird beides. Ein alter
# Ordner /opt/hirch-cnc wird nach der Installation entfernt; die Einstellungen liegen nicht dort.
set -e
ZIEL=/opt/hirch-cam
ALT=/opt/hirch-cnc

if [ "$1" = "--umzug" ]; then
  # eine ältere Version hat das neue Paket nach /opt/hirch-cnc gelegt: einmal verschieben
  [ -d "$ALT" ] && [ ! -L "$ALT" ] && [ -x "$ALT/bin/hirch-cam" ] || { echo "Nichts umzuziehen." >&2; exit 4; }
  [ -e "$ZIEL" ] && { echo "$ZIEL gibt es schon." >&2; exit 5; }
  mv "$ALT" "$ZIEL"
  cat "$ZIEL/VERSION"
  exit 0
fi

ARCHIV="$1"
[ -f "$ARCHIV" ] || { echo "Archiv nicht gefunden: $ARCHIV" >&2; exit 2; }
TMP="$(mktemp -d /opt/.hirch-cam-neu.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT
tar -C "$TMP" -xJf "$ARCHIV"
NEU=""
for d in hirch-cam hirch-cnc; do
  if [ -x "$TMP/$d/bin/hirch-cam" ] || [ -x "$TMP/$d/bin/hirch-cnc" ]; then NEU="$TMP/$d"; break; fi
done
[ -n "$NEU" ] || { echo "Das ist kein Hirch-CAM-Archiv." >&2; exit 3; }
for LISTE in "$NEU/share/hirch-cam/pakete.txt" "$NEU/share/hirch-cnc/pakete.txt"; do
  [ -f "$LISTE" ] && break
done
if [ -f "$LISTE" ] && command -v apt-get >/dev/null 2>&1; then
  FEHLT=""
  for p in $(grep -v '^#' "$LISTE"); do
    dpkg -s "$p" >/dev/null 2>&1 || FEHLT="$FEHLT $p"
  done
  if [ -n "$FEHLT" ]; then
    echo "Fehlende Pakete:$FEHLT"
    # shellcheck disable=SC2086
    apt-get install -y --no-install-recommends $FEHLT || echo "Pakete konnten nicht installiert werden." >&2
  fi
fi
chown -R root:root "$NEU"
# ein älteres Archiv (Programm noch unter dem alten Namen) bleibt am alten Ort
if [ ! -x "$NEU/bin/hirch-cam" ]; then ZIEL="$ALT"; fi
rm -rf "$ZIEL.alt"
[ -d "$ZIEL" ] && mv "$ZIEL" "$ZIEL.alt"
mv "$NEU" "$ZIEL"
rm -rf "$ZIEL.alt"
# Umzug Stufe 2: der alte Programmordner wird nicht mehr gebraucht
if [ "$ZIEL" != "$ALT" ] && [ -d "$ALT" ] && [ ! -L "$ALT" ]; then
  rm -rf "$ALT"
fi
cat "$ZIEL/VERSION"
