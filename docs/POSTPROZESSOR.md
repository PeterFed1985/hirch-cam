# Eigenen Postprozessor schreiben

Hirch CNC rechnet die Werkzeugwege unabhängig von der Steuerung. Erst der **Postprozessor**
schreibt sie als Datei für eine bestimmte Maschine. Jeder Postprozessor ist eine kleine
JSON-Vorlage, kein Programmcode: Was die Steuerung kann und was sie nicht kann, steht in der
Vorlage, und Hirch CNC richtet sich danach.

Mitgeliefert (`hirch_cam/post/vorlagen/`):

| Schlüssel | Steuerung | Besonderheiten |
|---|---|---|
| `richauto_a11` | RichAuto DSP A11 (Handregler) | eine Datei je Werkzeug (seit 20.10.; `M6` fährt am Regler zum Maschinen-Nullpunkt), kurze Namen `SCHILD_1.TAP`, keine Drehzahl, keine Kommentare, keine Bohrzyklen |
| `wincnc_mm` | WinCNC | Kommentare in `[ ]`, `G22` = Millimeter, Wechsel-Makro über `T…` |
| `linuxcnc_mm` | LinuxCNC, Mach3/Mach4 (RS274/NGC) | Bohrzyklen `G83`/`G81`/`G82`, Kommentare `( )`, `T… M6` |
| `grbl_mm` | Grbl 1.1, grblHAL | keine Bohrzyklen, kein `M6`: beim Wechsel `M0` (Halt) an der Wechselstelle |
| `mach3_mm` | Mach3, Mach4 (.tap) | Bohrzyklen, `T… M6` |
| `uccnc_mm` | UCCNC (UC100/UC300/UC400ETH/AXBB) | Bohrzyklen, `T… M6` |
| `masso_mm` | MASSO G3 | `G81`/`G83`, `T… M6` (misst mit Sensor) |
| `carvera` | Makera Carvera / Carvera Air | `M6 T…` holt das Werkzeug aus dem Magazin (T1…T6) |
| `ncstudio_mm` | Weihong NcStudio | eine Datei je Werkzeug |
| `centroid_mm` | Centroid Acorn / CNC12 | Bohrzyklen, `T… M6` |
| `fanuc_mm` | Fanuc, Haas, Syntec | `%`, `O0001`, N-Nummern, `G43 H…`, `G28` |

Die Vorlagen ab `mach3_mm` sind nach den Handbüchern zusammengestellt und an keiner echten
Maschine geprüft (steht in ihrer Beschreibung) – vor dem ersten Einsatz Luftfräsen.

Weitere Felder: `"kurzname": 8` (höchstens so viele Zeichen vor der Endung, nur A–Z 0–9 _) und
`"grossbuchstaben": true` (Name und Endung gross) für Steuerungen mit kurzer Dateianzeige. Unter
**Einstellungen › Maschine › Werkzeugwechsel** lässt sich «eine Datei je Werkzeug» oder «alles in
einer Datei» unabhängig von der Vorlage erzwingen.

Eigene Vorlagen legt man am einfachsten im Programm an: **Fräsen › Postprozessoren …** →
«Kopie anlegen». Sie liegen dann in `~/.config/hirch-cam/postprozessoren/<schlüssel>.json`.
Eine eigene Vorlage mit demselben Schlüssel wie eine mitgelieferte ersetzt diese.

Welche Steuerung neue Zeichnungen bekommen, steht unter **Einstellungen › Maschine ›
Steuerung für neue Zeichnungen**; jede Zeichnung kann unter «Auftrag einrichten › Ausgabe»
eine andere wählen.

## Aufbau einer Vorlage

```json
{
  "name": "Meine Steuerung (mm, .nc)",
  "beschreibung": "Hinweise für den Bediener (frei)",
  "endung": ".nc",
  "werkzeugwechsel": true,
  "wechselposition": true,
  "wechsel_x": 0, "wechsel_y": 0, "wechsel_z": null,
  "leerzeichen": true,

  "boegen": true,
  "kommentar": "({text})",
  "zeilennummern": 0,
  "bohrzyklus": {
    "entspanen": ["G98 G83 X{x} Y{y} Z{z} R{r} Q{q} F{f}"],
    "einfach":   ["G98 G81 X{x} Y{y} Z{z} R{r} F{f}"],
    "verweilen": ["G98 G82 X{x} Y{y} Z{z} R{r} P{p} F{f}"],
    "ende":      ["G80"]
  },

  "kopf":     ["({datei} - Hirch CNC {datum})", "G21 G90 G94 G17"],
  "werkzeug": ["T{t} M6 ({werkzeug})", "S{s} M3", "G0 Z{sicher}"],
  "wechsel":  ["M5", "G0 Z{sicher}"],
  "ende":     ["M5", "G0 Z{sicher}", "G0 X{hx} Y{hy}", "M30"]
}
```

### Ablauf einer Datei

```
kopf
werkzeug                      ← erstes Werkzeug
  … Fräsbahnen …
wechsel                       ← nur bei "werkzeugwechsel": true
werkzeug                      ← nächstes Werkzeug
  … Fräsbahnen …
ende
```

Bei `"werkzeugwechsel": false` bekommt jedes Werkzeug eine eigene Datei (`kopf`, `werkzeug`,
Bahnen, `ende`). Die Zusammenfassung zum Fotografieren nennt dann statt «Halt» die nächste Datei.

Die Fräsbahnen schreibt Hirch CNC immer selbst: `G0`/`G1`, Koordinaten mit drei Stellen,
X/Y/Z nur wenn sie sich ändern, Vorschub nur wenn er sich ändert.

### Felder

| Feld | Bedeutung |
|---|---|
| `endung` | Dateiendung (`.tap`, `.nc`, `.ngc`, `.gcode` …) |
| `werkzeugwechsel` | `true`: alle Werkzeuge in einer Datei, dazwischen `wechsel`; `false`: eine Datei je Werkzeug |
| `wechselposition` | `false`: Zeilen mit `{wx}`/`{wy}`/`{wz}` in `wechsel` fallen weg |
| `wechsel_x/_y/_z` | Wechselstelle (Werkstück-Koordinaten); `wechsel_z: null` = Sicherheitshöhe |
| `leerzeichen` | `G0 X1.000 Y2.000` statt `G0X1.000Y2.000` |
| `boegen` | Steuerung kann `G2`/`G3`. `false`: Rundungen immer als kurze Geraden; die Wahl «Bögen» im Auftrag verschwindet |
| `kommentar` | Muster für Kommentare, `{text}` = Name der Bearbeitung; vor jeder Bearbeitung steht ihr Name. Leer = keine Kommentare |
| `zeilennummern` | `> 0`: `N10`, `N20` … (Abstand); Kommentare und `%` ohne Nummer |
| `bohrzyklus` | fehlt der Eintrag (oder `null`), stehen Bohrungen als einzelne Bewegungen in der Datei |
| `kopf`, `werkzeug`, `wechsel`, `ende` | Zeilen, mit Platzhaltern |

### Platzhalter

| In | Platzhalter |
|---|---|
| allen Zeilen | `{t}` Werkzeugnummer · `{s}` Drehzahl · `{werkzeug}` Name · `{d}` Ø · `{sicher}` Sicherheitshöhe · `{hx}` `{hy}` Startpunkt · `{wx}` `{wy}` `{wz}` Wechselstelle · `{datei}` Dateiname · `{datum}` Datum · englisch gleichwertig: `{tool}` `{safe}` `{file}` `{date}` |
| `wechsel` | `{t}` ist das **nächste** Werkzeug, `{sicher}` die Sicherheitshöhe des vorigen |
| `bohrzyklus` | `{x}` `{y}` Lage · `{z}` tiefste Stelle · `{r}` Rückzugsebene (Startabstand über der Oberfläche) · `{q}` Zustellung je Hub · `{p}` Verweilzeit in s · `{f}` Vorschub · `{oben}` Höhe davor und danach |
| `kommentar` | `{text}` |

Namen in `{werkzeug}`, `{datei}` und `{text}` werden auf ASCII gebracht (ä → ae, Ø → D) und
von Klammern befreit, damit ein Kommentar nicht vorzeitig endet.

## Was eine Steuerung nicht kann

Die Grundregel: **Hirch CNC nimmt das Vorsichtige, wenn ein Eintrag fehlt.**

- **Keine Bohrzyklen** (Grbl, RichAuto A11): `bohrzyklus` weglassen. Jede Bohrung steht dann als
  Bewegungen in der Datei – mit Entspanen (hoch auf die Rückzugsebene, im Eilgang zurück bis
  0,2 mm über die letzte Tiefe). Das versteht jede Steuerung.
- **Kein `M6`** (Grbl): den Wechsel selbst beschreiben – Spindel aus, an die Wechselstelle,
  `M0` (Halt). Siehe `grbl_mm.json`.
- **Kein Halt mit `M0`**: dann `"werkzeugwechsel": false` – eine Datei je Werkzeug, gewechselt
  wird zwischen den Dateien.
- **Halt nur mit eigenem Befehl** (RichAuto A11 hält bei `T…M6`, wenn «Read T» eingestellt ist):
  den Befehl in `wechsel` schreiben.
- **Keine Bögen**: `"boegen": false`.
- **Keine Kommentare**: `"kommentar": ""` – auch in `kopf` keine Kommentarzeilen schreiben.
- **Drehzahl von Hand** (Spindel ohne Ansteuerung): kein `S{s}`; die Zusammenfassung zeigt die
  Drehzahl trotzdem, zum Einstellen am Drehknopf.

Bohrzyklen und Bewegungen fräsen genau gleich: Hirch CNC liest Lage, Tiefe und Rückzugsebene
für den Zyklus aus denselben Bewegungen, die es sonst schreiben würde. Passt eine Bohrung nicht
in einen Zyklus, stehen für sie die Bewegungen in der Datei.

## Ausprobieren

1. Im Dialog «Postprozessoren» zeigt der Reiter **Beispiel** sofort, wie eine kleine Datei mit
   zwei Werkzeugen und einer Bohrung aussieht.
2. Erste Datei immer **in der Luft** fahren: Z-Nullpunkt 20–30 mm über dem Werkstück setzen.
   Auf Bögen (falls eingeschaltet), Bohrzyklen und den Werkzeugwechsel achten.
3. Für Bögen: einen Kreis zeichnen, als Gravur 0,1 mm tief mit «Bögen» ausgeben und in der
   Luft fahren – so sieht man, ob die Steuerung `G2`/`G3` richtig herum fährt.

## Eine Vorlage beisteuern

Funktioniert eine Vorlage für eine Steuerung, die noch fehlt: die JSON-Datei nach
`hirch_cam/post/vorlagen/` legen, in die Tabelle oben eintragen und einen Test in
`tests/test_starttiefe_bohrer.py` oder `tests/test_post.py` ergänzen (z. B. «enthält `M0` beim
Wechsel», «keine `G83`»). In `beschreibung` steht, wie die Steuerung eingestellt sein muss.
