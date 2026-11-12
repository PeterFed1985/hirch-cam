#!/bin/sh
# Packt das übersetzte Hirch CNC samt Rechenkern in ein Archiv für
# /opt/hirch-cam (Linux Mint 22 / Ubuntu 24.04, x86_64).
#
#   fork/paket.sh [BAU-ORDNER]      Standard: ~/bau
#   → BAU-ORDNER/hirch-cam-<datum>-<commit>-linux-x86_64.tar.xz     Zwischenstand (Version = Datum-Commit)
#   HIRCH_RELEASE=1 fork/paket.sh …
#   → BAU-ORDNER/hirch-cam-<VERSION>-linux-x86_64.tar.xz            Release, z. B. 1.0.0-beta.1 aus der Datei VERSION
# Es gibt nur ein Programm, das öffentliche (Peter 09.11.2026 19:02). Es enthält nur, was im öffentlichen Repository
# steht (GPL). Peters Zusatz für fremde Formate (zusatz/) kommt nie ins Paket – er liegt in seinem Einstellungsordner.
set -e
REPO="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
BAU="${1:-$HOME/bau}"
WURZEL="$BAU/paket"
P="$WURZEL/opt/hirch-cam"
SH="$P/share/inkscape"
# Bau: Datum-Commit, steht immer in share/hirch-cam/BAU. Release (HIRCH_RELEASE=1): Version aus der Datei VERSION,
# z. B. «1.0.0-beta.1» (Peter 09.11.: «Hirch CAM 1.0 Beta»); sonst ist der Bau die Version (Zwischenstände)
BAU_ID="$(cd "$REPO" && git log -1 --date=format:%Y.%m.%d --format=%cd)-$(cd "$REPO" && git rev-parse --short HEAD)"
if [ -n "$HIRCH_RELEASE" ]; then
  VERSION="$(tr -d ' \r\n' < "$REPO/VERSION")"
else
  VERSION="$BAU_ID"
fi

rm -rf "$WURZEL"
DESTDIR="$WURZEL" ninja -C "$BAU/build" install >/dev/null
# Umzug Stufe 2 (04.11.2026): Programmordner /opt/hirch-cam – ein älterer Bauordner hat noch /opt/hirch-cnc als
# Präfix; das Programm findet seine Dateien relativ ($ORIGIN, share neben bin), darum genügt Verschieben
PRAEFIX="$(sed -n 's/^CMAKE_INSTALL_PREFIX:PATH=//p' "$BAU/build/CMakeCache.txt")"
[ -n "$PRAEFIX" ] && [ "$WURZEL$PRAEFIX" != "$P" ] && [ -d "$WURZEL$PRAEFIX" ] && mv "$WURZEL$PRAEFIX" "$P"

# Nicht gebraucht: Anleitungen (Hilfe-Menü gibt es nicht), Beispiele,
# Entwickler-Dateien, Inkview; Sprachen nur Deutsch
rm -rf "$SH/tutorials" "$SH/examples" "$P/include" "$P/share/man" "$P/share/bash-completion" \
       "$P/bin/inkview"
for d in "$P"/share/locale/*/; do
  case "$(basename "$d")" in de|de_*) ;; *) rm -rf "$d" ;; esac
done
# Englisch: nur die Inkscape-Texte, die den Programmnamen tragen (sonst gilt der englische Quelltext)
mkdir -p "$P/share/locale/en/LC_MESSAGES"
msgfmt -o "$P/share/locale/en/LC_MESSAGES/inkscape.mo" "$REPO/fork/sprache-en.po"

# DXF-Import: harmlosen Hinweis zu $PDMODE nicht als Fehlerfenster zeigen
sed -i 's/inkex.errormsg("\$PDMODE is ignored. A point is displayed as normal.")/pass  # Hirch CNC: kein Hinweisfenster/' \
    "$SH/extensions/dxf_input.py"

# Aktualisieren aus dem Menü (pkexec) und die Liste der nötigen Pakete
cp "$REPO/fork/aktualisieren.sh" "$P/bin/hirch-cam-aktualisieren"
chmod 755 "$P/bin/hirch-cam-aktualisieren"
mkdir -p "$P/share/hirch-cam"
{ grep -v '^#' "$REPO/fork/laufzeit-pakete.txt"
  printf '%s\n' python3-shapely python3-fonttools python3-olefile python3-scipy python3-numpy python3-lxml \
         python3-cssselect python3-tinycss2 xz-utils \
         python3-pil libocct-data-exchange-7.6t64 libocct-foundation-7.6t64 \
         libocct-modeling-algorithms-7.6t64 libocct-modeling-data-7.6t64 libopencamlib-2023.01; } > "$P/share/hirch-cam/pakete.txt"

# Rechenkern (Python) und Logo
mkdir -p "$SH/hirch-cam"
cp -r "$REPO/hirch_cam" "$REPO/assets" "$SH/hirch-cam/"
find "$SH/hirch-cam" -name "__pycache__" -type d -prune -exec rm -rf {} +
# Nur der öffentliche Stand: alles aus hirch_cam/, was nicht ins öffentliche Repository kommt, und prüfen
if [ -f "$REPO/werkzeuge/oeffentlich-weg.txt" ]; then
  grep -v '^#' "$REPO/werkzeuge/oeffentlich-weg.txt" | grep '^hirch_cam/' | while read -r f; do
    rm -rf "${SH:?}/hirch-cam/$f"
  done
fi
# ein Zusatz (Paket hirch_zusatz) gehört nie ins Programm, nur in den Einstellungsordner
if [ -n "$(find "$SH/hirch-cam" -name 'hirch_zusatz' -o -name 'fremdschriften.py' | head -n 1)" ]; then
  echo "Paket enthält einen Zusatz (hirch_zusatz) – so nicht packen" >&2
  exit 1
fi
# (im öffentlichen Repository fehlen diese Werkzeuge – dort gibt es auch nichts zu entfernen)
if [ -f "$REPO/werkzeuge/oeffentlich_pruefen.py" ]; then
  python3 "$REPO/werkzeuge/oeffentlich_sprache.py" "$SH/hirch-cam/hirch_cam/sprache/en.json"
  python3 "$REPO/werkzeuge/oeffentlich_pruefen.py" "$SH/hirch-cam/hirch_cam" >/dev/null || {
    echo "Paket: verbotene Wörter in hirch_cam (werkzeuge/oeffentlich_pruefen.py)" >&2; exit 1; }
fi
# Einlinienschriften fürs Textwerkzeug schon hier bauen (sonst wartet der erste Start)
PYTHONPATH="$SH/hirch-cam" python3 -m hirch_cam.schrift_ttf --vorrat "$SH/hirch-cam/hirch_cam/schriften/ttf" >/dev/null
# CAM-Modul hirch_cam nur für das Python von Mint 22 (3.12) mitgeben
PY_SO="hirch_cam.cpython-312-x86_64-linux-gnu.so"
[ -f "$REPO/hirch_cam/$PY_SO" ] || sh "$REPO/cam_kern/bauen.sh" python3.12
for so in "$SH"/hirch-cam/hirch_cam/hirch_cam.*.so; do
  [ "$(basename "$so")" = "$PY_SO" ] || rm -f "$so"
done
[ -f "$SH/hirch-cam/hirch_cam/$PY_SO" ] || { echo "CAM-Modul fehlt: $PY_SO" >&2; exit 1; }
# 3D-Modul nur mit STEP und OpenCAMLib packen (ohne die Entwicklerpakete baut relief_kern/bauen.sh still eine
# Fassung ohne 3D-Bahnen – 11.11.2026 nach einem Neustart der Bauumgebung so passiert)
PYTHONPATH="$SH/hirch-cam" python3.12 -c "from hirch_cam import hirch_relief as h; assert h.mit_step() and h.mit_ocl()" \
  2>/dev/null || { echo "3D-Modul ohne STEP oder OpenCAMLib – relief_kern/bauen.sh mit den Entwicklerpaketen neu bauen" >&2; exit 1; }

# Lizenzen: Hirch CNC (GPL) und die Herkunft der Rechenkerne (FreeCAD-Teile, OpenCAMLib, OpenCASCADE)
mkdir -p "$P/share/hirch-cam/lizenzen"
cp "$REPO/LICENSE" "$P/share/hirch-cam/lizenzen/LICENSE"
cp "$REPO/cam_kern/LIZENZEN.md" "$P/share/hirch-cam/lizenzen/cam_kern.md"
cp "$REPO/relief_kern/LIZENZEN.md" "$P/share/hirch-cam/lizenzen/relief_kern.md"
cp "$REPO/assets/willkommen/QUELLEN.md" "$P/share/hirch-cam/lizenzen/willkommen-bilder.md"

# Erweiterung Einlinienschrift (im Menü «Fräsen»)
mkdir -p "$SH/extensions/hirch"
cp "$REPO/inkscape/extensions/hirch_einlinien.py" "$SH/extensions/hirch/"
sed 's|<effects-menu>|<effects-menu hidden="true">|' \
    "$REPO/inkscape/extensions/hirch_einlinien.inx" > "$SH/extensions/hirch/hirch_einlinien.inx"

# Palette und Vorlage für neue Zeichnungen (300 × 200 mm)
cp "$REPO/inkscape/profile/palettes/hirch-cam.gpl" "$SH/palettes/"
for t in "$SH"/templates/default*.svg; do
  cp "$REPO/inkscape/profile/templates/default.svg" "$t"
done

# Symbole
for thema in "$SH"/icons/*/; do
  [ -d "$thema/scalable/actions" ] && cp "$REPO/fork/share/icons/hirch-fraesen.svg" "$thema/scalable/actions/"
  [ -d "$thema/symbolic/actions" ] && cp "$REPO/fork/share/icons/hirch-fraesen-symbolic.svg" "$thema/symbolic/actions/"
  [ -d "$thema/scalable/actions" ] && cp "$REPO/fork/share/icons/hirch-werkzeuge.svg" "$thema/scalable/actions/"
  [ -d "$thema/symbolic/actions" ] && cp "$REPO/fork/share/icons/hirch-werkzeuge-symbolic.svg" "$thema/symbolic/actions/"
  for s in hirch-drehen-links hirch-drehen-rechts; do       # Drehen 90° (nicht wie Rückgängig)
    [ -d "$thema/scalable/actions" ] && cp "$REPO/fork/share/icons/$s.svg" "$thema/scalable/actions/"
    [ -d "$thema/symbolic/actions" ] && cp "$REPO/fork/share/icons/$s-symbolic.svg" "$thema/symbolic/actions/"
  done
done
mkdir -p "$SH/icons/hicolor/scalable/apps" "$P/share/icons/hicolor/scalable/apps"
cp "$REPO/fork/share/icons/hirch-cam.svg" "$SH/icons/hicolor/scalable/apps/"
cp "$REPO/fork/share/icons/hirch-cam.svg" "$P/share/icons/hicolor/scalable/apps/"

# Inkscape-Logo überall durch das Hirch-Logo ersetzen (Fenster-, Dialog- und Programmsymbole)
for f in $(find "$P/share" \( -name 'org.inkscape.Inkscape.svg' -o -name 'inkscape-logo.svg' \) -not -path '*/extensions/*'); do
  cp "$REPO/fork/share/icons/hirch-cam.svg" "$f"
done
for f in $(find "$P/share" \( -name 'org.inkscape.Inkscape-symbolic.svg' -o -name 'inkscape-logo-symbolic.svg' \) -not -path '*/extensions/*'); do
  cp "$REPO/fork/share/icons/hirch-cam-symbolic.svg" "$f"
done
# Peter 28.10.: auch die Markenbilder (Startbild, Absturzmeldung u. a. lesen sie) ohne Inkscape-Logo
for f in "$SH"/branding/*.svg; do
  [ -f "$f" ] && cp "$REPO/fork/share/icons/hirch-cam.svg" "$f"
done
for n in 16 22 24 32 48 256; do
  for d in "$P/share/icons/hicolor/${n}x${n}/apps" "$SH/icons/hicolor/${n}x${n}/apps"; do
    [ -d "$d" ] || continue
    cp "$REPO/fork/share/icons/png/hirch-cam-$n.png" "$d/hirch-cam.png"
    [ -f "$d/org.inkscape.Inkscape.png" ] && cp "$REPO/fork/share/icons/png/hirch-cam-$n.png" "$d/org.inkscape.Inkscape.png"
  done
done

# Starter
cat > "$P/bin/hirch-cam" <<'EOF'
#!/bin/sh
# Hirch CAM starten (eigenes Profil in ~/.config/hirch-cam/profil)
HIER="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
# Umzug Stufe 2: hat eine ältere Version das Paket noch nach /opt/hirch-cnc gelegt, einmal nach /opt/hirch-cam
# umziehen (fragt nach dem Passwort); abgelehnt: weiter vom alten Ort
if [ "$HIER" = /opt/hirch-cnc ] && [ ! -e /opt/hirch-cam ] && command -v pkexec >/dev/null 2>&1; then
  pkexec "$HIER/bin/hirch-cam-aktualisieren" --umzug && [ -x /opt/hirch-cam/bin/hirch-cam ] && \
    exec /opt/hirch-cam/bin/hirch-cam "$@"
fi
export INKSCAPE_PROFILE_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/hirch-cam/profil"
export HIRCH_CAM_REPO="$HIER/share/inkscape/hirch-cam"
export XDG_DATA_DIRS="$HIER/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
# nach einem Update geänderte Vorgaben ins Profil übernehmen (einmal je Version)
PYTHONPATH="$HIRCH_CAM_REPO" python3 -m hirch_cam.profil "$INKSCAPE_PROFILE_DIR" \
    "$(cat "$HIER/VERSION" 2>/dev/null)" 2>/dev/null || true
# Dateiendung .hir (27.10.): Dateityp, Symbol, Vorschaubild und Zuordnung für diesen Benutzer anmelden –
# auch nach einem Update aus dem Menü, das installieren.sh nicht noch einmal ausführt
D="${XDG_DATA_HOME:-$HOME/.local/share}"
# Umzug Stufe 2: alten Menüeintrag dieses Benutzers entfernen (der alte Dateityp bleibt als Alias angemeldet)
if [ -f "$D/applications/net.hirch.HirchCNC.desktop" ]; then
  rm -f "$D/applications/net.hirch.HirchCNC.desktop" "$D/mime/packages/hirch-cnc.xml" "$D/thumbnailers/hirch-cnc.thumbnailer"
  rm -f "$D/mime/packages/hirch-cam.xml"     # neu anmelden lassen
fi
if ! cmp -s "$HIER/share/mime/packages/hirch-cam.xml" "$D/mime/packages/hirch-cam.xml" 2>/dev/null; then
  mkdir -p "$D/mime/packages" "$D/thumbnailers" "$D/applications" "$D/icons/hicolor/scalable/apps"
  cp "$HIER/share/mime/packages/hirch-cam.xml" "$D/mime/packages/" 2>/dev/null
  cp "$HIER/share/thumbnailers/hirch-cam.thumbnailer" "$D/thumbnailers/" 2>/dev/null
  cp "$HIER/share/applications/net.hirch.HirchCAM.desktop" "$D/applications/" 2>/dev/null
  cp "$HIER/share/icons/hicolor/scalable/apps/hirch-cam.svg" "$D/icons/hicolor/scalable/apps/" 2>/dev/null
  ( command -v update-mime-database >/dev/null && update-mime-database "$D/mime"
    command -v update-desktop-database >/dev/null && update-desktop-database -q "$D/applications"
    command -v xdg-mime >/dev/null && xdg-mime default net.hirch.HirchCAM.desktop application/x-hirch-cam \
      && xdg-mime default net.hirch.HirchCAM.desktop application/x-hirch-cnc
  ) >/dev/null 2>&1 &
fi
# Symbol: hat ein anderes Programm in diesem Ordner einen Symbol-Zwischenspeicher angelegt, der älter ist als unser
# Symbol, findet die Taskleiste «hirch-cam» nicht und zeigt ein Zahnrad (Peter 12.11.) – Zwischenspeicher auffrischen
H="$D/icons/hicolor"
if [ -f "$H/icon-theme.cache" ] && [ "$H/scalable/apps/hirch-cam.svg" -nt "$H/icon-theme.cache" ] &&
   command -v gtk-update-icon-cache >/dev/null; then
  gtk-update-icon-cache -q -f -t "$H" >/dev/null 2>&1 &
fi
exec "$HIER/bin/inkscape" "$@"
EOF
chmod +x "$P/bin/hirch-cam"
# Übergang: ältere «Aktualisieren» prüfen bin/hirch-cnc im Archiv, alte Menüeinträge starten ihn
ln -s hirch-cam "$P/bin/hirch-cnc"

# Desktop-Eintrag (installieren.sh legt ihn ins Anwendungsmenü)
rm -f "$P"/share/applications/*.desktop "$P"/share/metainfo/*.xml
mkdir -p "$P/share/applications"
cat > "$P/share/applications/net.hirch.HirchCAM.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Hirch CAM
GenericName=CNC-Fräsprogramme
Comment=Zeichnen und G-Code für die CNC-Fräse erstellen
Exec=/opt/hirch-cam/bin/hirch-cam %F
Icon=hirch-cam
Terminal=false
StartupNotify=true
StartupWMClass=hirch-cam
Categories=Graphics;Engineering;
MimeType=application/x-hirch-cam;application/x-hirch-cnc;image/svg+xml;
Keywords=CNC;Fräse;G-Code;V-Carve;Gravur;
EOF

# Dateityp .hir (auch .hirch der ersten Fassung): Inhalt ist SVG (darum Unterart von image/svg+xml), eigenes Symbol, Vorschaubild wie SVG
mkdir -p "$P/share/mime/packages" "$P/share/thumbnailers"
cat > "$P/share/mime/packages/hirch-cam.xml" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <mime-type type="application/x-hirch-cam">
    <alias type="application/x-hirch-cnc"/>
    <comment>Hirch CAM drawing</comment>
    <comment xml:lang="de">Hirch-CAM-Zeichnung</comment>
    <sub-class-of type="image/svg+xml"/>
    <icon name="hirch-cam"/>
    <glob pattern="*.hir"/>
    <glob pattern="*.hirch"/>
  </mime-type>
</mime-info>
EOF
cat > "$P/share/thumbnailers/hirch-cam.thumbnailer" <<'EOF'
[Thumbnailer Entry]
TryExec=gdk-pixbuf-thumbnailer
Exec=gdk-pixbuf-thumbnailer -s %s %u %o
MimeType=application/x-hirch-cam;
EOF

echo "$VERSION" > "$P/VERSION"
echo "$BAU_ID" > "$P/share/hirch-cam/BAU"
# Zeitpunkt des Commits: ein Rechenkern-Update gilt nur, wenn es neuer ist
(cd "$REPO" && git log -1 --format=%ct) > "$P/share/hirch-cam/ZEIT"
# HIRCH_PAKET_ZUSATZ: z. B. «-3d» für Pakete vom Zweig 3d-fraesen
# Name seit 03.11.2026 «Hirch CAM» (Peter). Im Archiv heisst der Ordner für den Übergang noch «hirch-cnc»:
# ältere «Aktualisieren» erwarten ihn; die neuen (aktualisieren.sh, installieren.sh) nehmen beide Namen
ARCHIV="$BAU/hirch-cam-$VERSION${HIRCH_PAKET_ZUSATZ:-}-linux-x86_64.tar.xz"
tar -C "$WURZEL/opt" --transform 's,^hirch-cam,hirch-cnc,S' -cf - hirch-cam | xz -T0 -6 > "$ARCHIV"
ls -lh "$ARCHIV"
