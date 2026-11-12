# Gestaltung der Oberfläche – Richtlinien für Hirch CNC

Stand 17.10.2026 (3D-Sitzung, ergänzt von der Basis-Sitzung: Grundsätze 8–9, Sofort-Vorschau,
ältere Dialoge, Zusammenfassung). Abgeleitet aus dem, was gebaut ist und Peter gutgeheissen hat (Panel nach
Mockup C, Taschendialog «alles auf einer Seite», Spindel und Halterung «Vorschlag 2», Rechenanzeige,
Reiter «3D»). Gilt für beide Sitzungen (Basis und 3D) und für jede neue Oberfläche. Weicht etwas
davon ab, steht der Grund im Mockup oder in UEBERGABE*.md.

## 1. Grundsätze

1. **Zuerst ein Mockup, dann bauen.** Jede neue oder umgebaute Oberfläche kommt zuerst als Board in
   den Design-Canvas (claude.ai, Typ «Design»), Notiz daneben: Vorschlag, offene Fragen, später
   «Entschieden (Peter, Datum)». Gebaut wird erst, wenn Peter es sagt.
2. **Werkstatt-Sprache, keine Programm-Sprache.** Begriffe, die Peter an der Maschine benutzt:
   Zustellung, Bahnabstand, Aufmass, Eintauchen, Schruppen/Schlichten, Spannmutter. Keine internen
   Namen (`relief_schlichten`, `tiefe_ab`) in der Oberfläche.
3. **Sofort sichtbar, was eine Einstellung bewirkt.** Erklärung neben dem Feld, die sich mit dem Wert
   ändert («mm · darüber muss schon frei sein – bis dahin Eilgang»); Vorschau/Schnittbild im Dialog;
   Änderungen seit dem Öffnen zeigen (`aenderungen_zeigen`).
4. **Weniger zeigen statt sperren.** Was nicht passt, wird ausgeblendet (`zeile_zeigen`), nicht grau
   gemacht – ausser ein Knopf ist nur gerade nicht möglich (dann mit Tooltip, warum).
5. **Nichts geht verloren, nichts passiert heimlich.** Jede Änderung an der Zeichnung mit Rückgängig
   (`DocumentUndo::done` mit sprechendem Namen); reine Anzeige-Objekte (z. B. Unterseite auf Seite 2)
   ohne Rückgängig und gesperrt. Was gefräst wird, entscheidet nie ein Anzeige-Objekt.
6. **Sicher vor schnell.** Was der Maschine schaden kann, wird laut: rote Befunde, «Rechnung beendet –
   Kollision», Ausgabe eines Entwurfs nie ohne genaue Rechnung.
7. **Hell und dunkel gleich gut.** Peter arbeitet dunkel. Jede Oberfläche in beiden Designs prüfen.
8. **Für jede Maschine, nicht für eine.** Hirch CNC wird Open Source. Die Oberfläche nennt keine
   bestimmte Steuerung (kein «A11» in Texten); was eine Steuerung kann, steht in ihrer
   Postprozessor-Vorlage (`boegen`, `bohrzyklus`, `kommentar` …, siehe `POSTPROZESSOR.md`), und die
   Oberfläche richtet sich danach: kann sie etwas nicht, verschwindet die Wahl und ein Satz sagt
   warum (z. B. «Bögen» im Einrichten).
9. **Nur zeigen, was passt.** Auswahllisten bieten nur, was dort sinnvoll ist (Bohrer nur beim Bohren,
   beim Kernloch und bei den Passlöchern; Boden der V-Carve nur Schaftfräser). Wo ein Werkzeug fehlen
   kann, steht am Ende der Liste «Neues Werkzeug …» (öffnet den Werkzeugdialog mit passender Vorgabe,
   danach ist das neue gewählt).

## 2. Aufbau

### Panel «Fräsen» (rechts angedockt)
- Oben der **Rechenstreifen** (Zustände siehe 5.), darunter Material in einer Zeile, Seite 1 / Seite 2,
  Ablauf gruppiert nach Werkzeug (`.hirch-gruppe` mit Kopf `.hirch-gruppe-kopf`, T-Nummer als
  `.hirch-chip`), je Bearbeitung Schnittbild, Kurzhinweis, Warnungen direkt darunter.
- Zeiten nur je Bearbeitung; Gesamtzeit und Zeitstrahl unten. Ausgabe-Knöpfe ganz unten,
  «G-Code speichern» als Hauptknopf (`suggested-action`).

### Dialoge (Vorlage: Taschen- und Konturdialog)
- **Kopfzeile:** Symbol, Titel (`.hirch-seitentitel`), Unterzeile (`.hirch-dim`) mit dem Zusammenhang
  («Einstellungen › Maschine · gilt für alle Zeichnungen»).
- **Alles auf einer Seite:** ein Rollbereich mit Abschnitten; oben **Sprung-Chips** (`.hirch-sprung`)
  zu den Abschnitten und eine Suche. Keine Notebook-Reiter in Dialogen.
- **Abschnitt** = Titel + Karte (`.hirch-karte2`); in der Karte Überschrift in Grossbuchstaben
  (`.hirch-kopf`, z. B. «TIEFE», «RAND») und ein Raster:
  `Beschriftung (200 px) | Feld | Erklärung (.hirch-dim .hirch-klein, live)`.
- **Rechte Spalte** (wo sinnvoll): grauer Kasten mit Vorschau/Schnitt und darunter die Prüfung.
- **Knöpfe unten, in dieser Reihenfolge:** «Abbrechen» · [«Entfernen»] · [«Auswahl zuweisen»] ·
  «Übernehmen» (`suggested-action`). Kein «OK».
- Grösse: so, dass auf 1600 × 1000 nichts gerollt werden muss, was man zum Verstehen braucht.
- **Sofort-Vorschau** (rechte Spalte): Schnitt durch die Rille mit den Ebenen der Zustellungen; zeigt
  die wirkliche Werkzeugform (V-Winkel, Kugel, Bohrerspitze mit «+ Spitze») und eine Starttiefe als
  graue Linie «ab …». Rechnet nichts, folgt jeder Änderung; genau gerechnet wird erst mit
  «Genau rechnen» oder «Übernehmen».

### Ältere Dialoge (Bestand)
Einrichten (Werkstück, Nullpunkt, Ausgabe, Zweiseitig), Werkzeuge, Postprozessoren, Begrüssung,
Einstellungen › Maschine stammen von vor diesen Richtlinien. Sie bleiben, bis Peter einen Umbau
wünscht; dann im neuen Aufbau. Bis dahin gilt für sie:
- **Werkzeuge:** links die Liste der Zeichnung mit Skizze je Werkzeug, rechts Skizze gross und nur die
  Felder, die zur Art passen (Spitzenwinkel nur beim Bohrer, Winkel/Spitze nur beim V-Fräser …).
  Knöpfe «Abbrechen» · «Übernehmen».
- **Postprozessoren:** ein Editor für Vorlagen-Dateien – darum Reiter für die Zeilenblöcke
  (Dateianfang … Bohrzyklus, Beispiel) als begründete Ausnahme von «keine Notebook-Reiter», und
  «Speichern» · «Schliessen» statt «Übernehmen», weil jede Vorlage für alle Zeichnungen gilt. Der Reiter
  «Beispiel» zeigt sofort die erzeugte Datei.
- **Einrichten:** zwei Spalten mit Abschnitten; abhängige Felder werden eingerückt und ausgeblendet,
  solange das Häkchen darüber aus ist.

### Zusammenfassung zum Fotografieren
Bewusst anders als der Rest: hoch und schmal wie ein Handybildschirm, grosse Schrift, starker Kontrast,
Umschalter hell/dunkel für die Werkstatt. Reihenfolge wie an der Maschine: Material und Nullpunkt,
Gesamtzeit, Werkzeug beim Start, dann je Halt «HALT n – WERKZEUG WECHSELN» (orange, zum Abhaken)
bzw. bei einer Datei je Werkzeug «DATEI n – …» mit Dateiname; Drehzahl gross (sie wird von Hand
eingestellt).

### Reiter «3D» und andere Dock-Reiter
- Gleiche Bausteine wie Dialoge, aber schmal (`set_max_width_chars(44)` für Fliesstext).
- Abschnittstitel als `abschnitt(...)`, Formulare mit `formular()` / `spin_zeile()`.
- Hinweise oben: Fehler `.hirch-fehler`, Warnung `.hirch-warn`, neutraler Hinweis `.hirch-karte`.

### Menü «Fräsen»
- Führt **alles**, was zum Fräsen gehört, nach Themen: Auftrag und Material, Werkzeuge, Bearbeitungen,
  Vorschau und Rechnen, Ausgabe, Messkorrektur, Einstellungen (Sammelauftrag 21.10.). Seitenleiste und
  Leiste bleiben der schnelle Zugriff.
- Jede neue Funktion bekommt einen Menüeintrag. Menü, Panel und Leiste rufen **dieselbe Aktion bzw.
  Funktion** auf (`win.hirch-…` in `hirch-aktionen.cpp`) – nie eine zweite Umsetzung.
- **Gleicher Name überall** (Menü, Knopf, Tooltip, Fenstertitel). «Einstellungen ›» springt zum
  Abschnitt im Reiter «Einstellungen» (`HirchAnsicht::abschnittZeigen`).
- Nur in der Seitenleiste bleibt, was zu einer Zeile gehört (Auge, Häkchen, Ziehen, Rechtsklick einer
  Bearbeitung, Platte/Seite wählen, Vorschub-Leiste).

### Auf der Zeichenfläche
- Schwebende Leiste rechts oben (`.hirch-leiste`, blass 0,62 bis zum Überfahren); Kärtchen und Knöpfe
  links daneben (`.hirch-rechenkarte`, `.hirch-rechenknopf`).
- **Keine Animation über Zeichenfläche oder 3D-Ansicht** (Kreisel, Puls): sie zeichnen alles darunter
  laufend neu. Dort springt eine Uhr (◴◷◶◵) einmal pro Sekunde, Text nur bei Änderung setzen.

## 3. Farben

- **Grundsatz:** Farben aus dem Design (`@theme_bg_color`, `@theme_selected_bg_color`, `currentColor`
  mit `alpha()`), damit hell und dunkel stimmen. Feste Farben nur für Bedeutungen:

| Bedeutung | Farbe | Klasse / Ort |
|---|---|---|
| Fehler, Gefahr, Kollision | `#d9342b` (Hintergrund 20 %) | `.hirch-fehler`, rote Markierung |
| Warnung, Achtung | `#e8a33a` / `#e5a50a` | `.hirch-warn`, `.hirch-abgebrochen` |
| Fertig, in Ordnung | `#26a269` / `#2e8b3e` | `.hirch-fertig`, `.hirch-ok` |
| Läuft, Auswahl, Hauptaktion | Auswahlfarbe des Designs (`#3584e4`) | `.hirch-rechnet`, `suggested-action` |
| Geändert | `#e66100` (13 %) | `.hirch-geaendert` |
| Schild «ENTWURF» | `#ffe6b3` auf `#6b4a00` | `.hirch-schild` |
| Zweiseitig / Wenden | `#9141ac` gestrichelt | `.hirch-wenden` |

- **Fräsfarben** (Linienfarbe = Fräsart, `ARTEN` in `hirch-intern.h`, `farben.py`): V-Carve Rot
  `#e00000`, Gravur Orange `#ff8c00`, Tasche Grün `#00a000`, Kontur Blau `#0050ff`, Bohren Violett
  `#8000c0`, Gewinde Türkis `#00aabe`, Planfräsen `#9a7b4f`, 3D-Schruppen `#5e5c64`, 3D-Schlichten
  `#9a96a0`. Diese Farben nie für etwas anderes verwenden; weitere Bearbeitungen derselben Art
  bekommen Abstufungen (`freier_ton`).
- **3D-Ansicht:** Modell grau schattiert, Werkstück blau durchsichtig, Haltestege gelb `#ffd21a`,
  Fräsergebnis Holz; orange = bleibt stehen, rot = zu tief / ragt heraus. Legende unter der Ansicht.

## 4. Texte

- **Deutsch (Schweiz):** «ss» statt «ß» (grösser, gemäss), Anführungszeichen «…», Dezimalkomma
  (0,5 mm), Einheiten mit Leerzeichen (6 mm, 2000 mm/min, 18 000 U/min, Ø 6 mm, 90°).
- **Knöpfe:** Verb oder Ziel, kurz («Rechnen», «Übernehmen», «Alle entfernen»). «…» am Ende, wenn ein
  weiteres Fenster aufgeht («Spindel und Halterung …», «Werkzeuge …»).
- **Hinweise sagen, was zu tun ist:** Ursache + Ausweg in einem Satz («Werkzeug C ist ein Bohrer – er
  bohrt nur senkrecht. Für diese Bearbeitung einen Fräser wählen; ausgelassen.»).
- **Zustände als kurze Titel:** «Rechnet … 0:12», «✓ Fertig in 0:14», «■ Abgebrochen nach …»,
  «◷ Noch nicht gerechnet».
- **Tooltips** erklären das Warum, nicht das Offensichtliche.
- **Übersetzung:** jeder sichtbare Text über `TR()`/`TRF()` (C++) bzw. `tr()` (Python) und in
  `hirch_cam/sprache/en.json` (sortiert). Ganze Sätze mit `{}` statt zusammengesetzter Stücke; keine
  allgemeinen Muster wie `"{} s"` oder `"{} mit {}"` (sie treffen fremde Texte). `tests/test_katalog.py`
  und der englische Oberflächentest müssen grün sein.

## 5. Zustände beim Rechnen (überall gleich)

| Zustand | Panel-Streifen | Reiter «3D» (Fräsergebnis) | neben der Leiste |
|---|---|---|---|
| Rechnet | blau, Puls, «Rechnet … Uhr», Schritt, «Abbrechen» | abgedunkelt, Pille mit Uhr | Kärtchen ◴ Uhr × |
| Fertig | grün «✓ Fertig in …» | Ergebnis | – |
| Abgebrochen | gelb «■ Abgebrochen … – noch kein neues Programm», «Neu rechnen» | – | – |
| Wartet (3D, «Automatisch rechnen» aus) | grau «◷ Noch nicht gerechnet», blau «Rechnen» | Pille mit «Rechnen» | «▶ Rechnen» |
| Wartet aufs Werkzeug | grau «◷ Text wird bearbeitet – gerechnet wird danach» bzw. «◷ Geändert – gerechnet wird, wenn du fertig bist», «Jetzt rechnen» | – | – |

Während gerechnet wird oder ein alter Stand gezeigt wird, ist die Liste blass (0,45).

**Wann gerechnet wird** (Peter, 21.10.): nur wenn sich Gefrästes geändert hat (Kennung der Analyse). Mit
Text-, Knoten-, Zeichen- und Formwerkzeugen wartet es bis zum Wechsel zum Auswahlwerkzeug; mit dem
Auswahlwerkzeug 1,5 s nach der letzten Änderung; Einstellungen im Panel sofort. Speichern, USB-Stick,
Vorschau 3D und Zusammenfassung rechnen selbst, wenn das Ergebnis veraltet ist.

## 6. Mockups im Design-Canvas

- Ein Board je Thema, Name wie die Funktion («Haut», «Knopfdruck»); Titel sagt, was gezeigt wird.
- Aussehen wie das Programm (GNOME/Adwaita): Schrift Ubuntu, Fläche `#F6F5F4`, Rahmen `#C0BFBC`,
  Hintergrund `#DEDDDA`, Text `#2E3436`, gedämpft `#77767B`, Blau `#3584E4` / `#1A5FB4`; GTK-Felder mit
  − / +, verbundene Knöpfe für Auswahlen. Keine Web-Optik, die es im Programm nicht gibt.
- Zustände umschaltbar (Play-Modus), damit Peter jeden Fall sieht.
- Notiz daneben: blau = Vorschlag, orange = entschieden.

## 7. Prüfliste vor dem Commit einer Oberfläche

- [ ] Mockup freigegeben; Abweichungen begründet.
- [ ] Hell und dunkel angesehen (Bildschirmfoto aus dem Oberflächentest, `HIRCH_TEST_BILD…`, oder
      unter Xvfb mit xdotool).
- [ ] Keine Annahme über eine bestimmte Steuerung; Fähigkeiten aus der Postprozessor-Vorlage.
- [ ] Englische Oberfläche ohne deutsche Reste; neue Texte in `en.json`.
- [ ] Felder mit Dezimalkomma eingebbar (`punkt_und_komma`).
- [ ] Rückgängig funktioniert, und der Name im Menü «Bearbeiten» stimmt.
- [ ] Keine Animation über Zeichenfläche/3D-Ansicht; nichts flackert beim Rechnen.
- [ ] Gemeinsame Dateien vorher in `docs/ABSPRACHEN.md` eingetragen.
