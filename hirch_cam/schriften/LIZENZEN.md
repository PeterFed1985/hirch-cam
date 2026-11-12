# Einlinienschriften

`einlinien.json` enthält Einlinienschriften (Mittellinien, nicht Umrisse). Gesammelt mit
`werkzeuge/einlinienschriften.py` (Quellen und Vorgehen: `docs/EINLINIENSCHRIFTEN.md`).

| Schrift | Herkunft | Lizenz |
|---|---|---|
| Hershey Sans, Sans Medium, Serif, Serif Italic, Serif Bold, Serif Bold Italic, Script, Script Medium, Gothic English | Dr. A. V. Hershey, U.S. National Bureau of Standards (1967); SVG-Fassung Windell H. Oskay, Evil Mad Scientist (Inkscape `share/extensions/svg_fonts`) | Hershey-Lizenz, frei mit Danksagung (unten) |
| Hershey Gothic German, Hershey Gothic Italian | wie oben, Daten aus `hershey-fonts` (Kamal Mostafa, `.jhf`); Umlaute von Hirch CNC aus Buchstabe und Punkten zusammengesetzt | Hershey-Lizenz |
| EMS Allure, Elfin, Felix, Nixish, Nixish Italic, Osmotron, Readability, Readability Italic, Tech | Sheldon B. Michaels, Windell H. Oskay (Evil Mad Scientist Laboratories), abgeleitet von Allura (Rob Leuschke), Mountains of Christmas (Crystal Kluge), Felipa (Fontstage), Nixie One (Jovanny Lemonad), Orbitron (Matt McInerney), Source Sans Pro (Paul D. Hunt, Adobe), Architects Daughter (Kimberly Geswein) | SIL Open Font License 1.1 (`OFL-EMS.txt`); EMS Elfin laut Google auch Apache 2.0 |
| Relief SingleLine | The Relief SingleLine Project Authors – Élisa Garzelli, Noëlie Dayma, François Chastanet, isdaT Toulouse (github.com/isdat-type/Relief-SingleLine) | SIL Open Font License 1.1 (`OFL-Relief-SingleLine.txt`) |
| KiCad Newstroke | Vladimir Uryvaev und die KiCad-Entwickler (`common/newstroke_font.cpp`); nur Latein, Griechisch und Zeichen, ohne CJK | GNU GPL 2 oder später (wie Hirch CNC unter GPL 3) |

Aufbau je Schrift: `cap` (Versalhöhe), `xh` (x-Höhe), `lic`, `g` = Zeichen ->
`[Vorschub, [[x, y, x, y, …], …]]` in Schrifteinheiten, y nach oben.

Im Textwerkzeug erscheinen die Schriften als TTF (`hirch_cam/schrift_ttf.py`) mit **optischem
Buchstabenabstand** von Hirch CNC statt der Breiten und Paare der Vorlage. Das sind abgeleitete
Werke: Sie stehen unter derselben Lizenz wie die Vorlage und tragen ihren Namen; die Vorlagen
reservieren keine Schriftnamen.

## Danksagung (Bedingung der Hershey-Lizenz)

Die Hershey-Lizenz erlaubt jede Verwendung, auch kommerziell, wenn diese Danksagung mit den Schriftdaten
weitergegeben wird und die Daten nicht in das Format des U.S. NTIS gebracht werden:

- The Hershey Fonts were originally created by Dr. A. V. Hershey while working at the
  U. S. National Bureau of Standards.
- The format of the font data in this distribution was originally created by
  James Hurt, Cognition, Inc.

## SIL Open Font License

Die EMS-Schriften und Relief SingleLine dürfen frei verwendet, verändert und weitergegeben werden,
auch eingebettet; verkauft werden dürfen sie nur zusammen mit Software. Abgeleitete Schriften bleiben
unter der OFL. Die Lizenztexte liegen hier bei (`OFL-EMS.txt`, `OFL-Relief-SingleLine.txt`).

## Herstellerschriften

Schriften, die nur mit der Software ihres Herstellers verwendet werden dürfen, liegen nie im Repository – nur in
`~/.config/hirch-cam/schriften` des jeweiligen Rechners (Import über «Schriften importieren …»).
