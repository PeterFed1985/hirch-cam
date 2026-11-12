# Testteil Hartschaum (3D, zweiseitig)

Ein Teil, an dem sich alle 3D-Fräsarten von Hirch CNC in einem Durchgang prüfen lassen.
Die STEP-Datei liegt im Paket unter `hirch_cam/daten/testteil_hartschaum.step`; in Hirch CNC:

- **Neue Zeichnung** → Dialog «Neue Zeichnung einrichten» → Abschnitt «3D-Modell» → **Testteil laden**
  → «Los geht's». Material 220 × 220 × 49 mm (Rand 20 mm ringsum), «Zweiseitig» ist an.
- oder im Reiter **3D** einer Zeichnung ohne Modell: **Testteil laden …**

Neu erzeugen (OpenCASCADE, nur Linux):

    g++ -std=c++17 -I/usr/include/opencascade testteil_erzeugen.cpp -o testteil_erzeugen \
        -lTKSTEP -lTKXSBase -lTKBRep -lTKernel -lTKMath -lTKG3d -lTKG2d -lTKGeomBase \
        -lTKTopAlgo -lTKPrim -lTKBO -lTKFillet -lTKGeomAlgo -lTKShHealing -lTKBool
    ./testteil_erzeugen ../../hirch_cam/daten/testteil_hartschaum.step

## Masse

Platte 180 × 180 × 24 mm, Aussenecken R15; höchster Punkt 39 mm (oberste Treppenstufe).
Passt in eine 50-mm-Hartschaumplatte. Koordinaten unten ab der vorderen linken Ecke des Teils.

| | Merkmal | Lage | prüft |
|---|---|---|---|
| A | Kuppel Ø 70, 14 hoch | Mitte (50, 50) | Schlichten parallel, Höhenlinien, kombiniert; Fräsergebnis |
| B | Block 50 × 35 × 14, Oberkanten R3 | x 105–155, y 25–60 | senkrechte Wände, Kehlen nachfahren, Waagrechte Flächen |
| C | Treppe, 3 Stufen à 5 mm | x 105–165, y 110–165 | Waagrechte Flächen, Innenecken, Z-Ebenen-Schruppen |
| D | Rinne R2 (halbrund) | entlang X bei y 88, ab x 100 | Nur Restmaterial, Kehlen mit kleinem Kugelfräser |
| E | Tasche 40 × 25 × 10, Ecken R4 | x 20–60, y 115–140 | Bohrungen und Taschen erkennen (Tasche) |
| F | Durchgangsbohrung Ø 8 | (25, 92) | Bohrung erkennen, durch |
| G | Sackloch Ø 6, 8 tief | (75, 92) | Bohrung von oben |
| H | Mulde Ø 60, 6 tief (unten) | (50, 50) | Schlichten Seite 2 |
| I | Tasche 40 × 20 × 8 (unten) | x 110–150, y 32–52 | Tasche von unten (Seite 2) |
| J | Sackloch Ø 6, 10 tief (unten) | (155, 92) | Bohrung von unten (Seite 2) |

## Prüfliste

1. Testteil laden, «Los geht's»: Modell liegt mittig, «Zweiseitig» ist an und Seite 2 angelegt.
2. Reiter 3D › «Bohrungen und Taschen»: F und G auf Seite 1, J auf Seite 2; Taschen E (Seite 1) und
   I (Seite 2). Die Eckradien der Tasche E dürfen **nicht** als Bohrungen erscheinen.
3. Seite 1: 3D-Schruppen (Ebenen oder adaptiv), Schlichten kombiniert, Kehlen nachfahren,
   Waagrechte Flächen; Fräsergebnis ansehen (Rinne D bleibt mit Ø 6 stehen → Restmaterial Ø 3).
4. Seite 2: Schruppen und Schlichten der Mulde H, Tasche I, Sackloch J.
5. Zeitschätzung ansehen: Bahnabstand Schlichten etwa 20 % genügt für Hartschaum.
6. Probefräsen: Passlöcher, Seite 1, wenden um X, Seite 2; zum Schluss Haltestege lösen.
