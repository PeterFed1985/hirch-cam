# Schnittdaten: woher die automatischen Werte kommen

Stand 22.10.2026. Auftrag Peter: «Für jede Art Fräser die Herstellerangabe heraussuchen und einen
Mittelwert aus zu langsam und zu schnell berechnen. Hirch CNC rechnet mit diesem Mittelwert; stimmt er
nicht, ändere ich die Vorgaben von Hand.» Umgesetzt in `hirch_cam/werkzeugdb.py` (`MATERIALIEN`,
`FZ_FAKTOR`, `schnittdaten`).

## Rechnung

1. **Drehzahl** n = vc · 1000 / (π · Ø), begrenzt auf die Spindel (Maschine 6000–24 000). Ist die Drehzahl
   am Werkzeug von Hand eingestellt, gilt sie.
2. **Zahnvorschub** fz (mm je Schneide):
   - 2 und mehr Schneiden: fz = k · Ø, begrenzt fz_min … fz_max; ab 3 Schneiden × 0,8
   - 1 Schneide (wo die Hersteller eigene Werte haben): fz = k1 · Ø, begrenzt fz_min … fz_max1
   - Fräserart: × Faktor (unten); Planfräser: höchstens ¼ des Werts eines Ø-12,7-Fräsers
3. **Vorschub** = n · Schneiden · fz, höchstens der Höchstvorschub der Maschine.
4. **Vollnut** (Kontur, Nut): Vorschub × Vollnut-Faktor des Werkstoffs.
5. **Eintauchen** = Vorschub × (1/Schneiden + ½) / 2 – Mittel aus Amana (Vorschub ÷ Schneiden) und
   Harvey (½): 1 Schneide 75 %, 2 Schneiden 50 %, 3 Schneiden 42 %. Bohrer: voller Vorschub.
6. **Zustellung** = Faktor × Ø (höchstens die Schneidenlänge).

Der Mittelwert ist je Ø die Mitte zwischen kleinstem und grösstem Herstellerwert, gemittelt über die
Quellen; k ist die Gerade durch den Nullpunkt, die am besten passt. Die Tabellen gelten für eine
Zustellung bis 1 × Ø (Amana, Onsrud: 2 × Ø −25 %, 3 × Ø −50 %).

## Werkstoffe

| Werkstoff | vc m/min | k (2+ Schneiden) | k1 (1 Schneide) | fz_min … fz_max (fz_max1) | Zustellung × Ø | Vollnut |
|---|---|---|---|---|---|---|
| Weichholz | 430 | 0,026 | 0,048 | 0,05 … 0,23 (0,36) | 0,8 | 0,6 ¹ |
| Hartholz | 405 | 0,0205 | 0,05 | 0,02 … 0,18 (0,36) | 0,8 | 0,5 ¹ |
| MDF (neu) | 405 | 0,025 | 0,052 | 0,04 … 0,20 (0,36) | 0,8 | 0,6 ¹ |
| Acryl | 340 | 0,028 | – (gleich) | 0,045 … 0,19 | 0,6 | 0,78 |
| PVC | 340 | 0,025 | – | 0,045 … 0,185 | 0,6 | 0,78 |
| Aluminium | 330 | 0,0105 | 0,02 | 0,02 … 0,10 (0,15) | 0,5 | 0,86 |
| Messing | 255 | 0,0105 | – | 0,012 … 0,08 | 0,33 | 0,86 |
| Hartschaum | 270 | 0,028 | – | 0,08 … 0,21 | 1,0 | 0,8 ¹ |
| Zinn | 200 | 0,004 | – | 0,003 … 0,035 | 0,2 | 0,4 ² |

¹ Kein Hersteller nennt einen Vollnut-Abschlag für Holz und Schaum (die Tabellen gelten für 1 × Ø) – der
bisherige Wert bleibt. ² Für Zinn keine Herstellertabelle gefunden – bisherige Schätzung.

Beispiele (Mittelwerte):
- Ø 3, 1 Schneide, Aluminium, 19 000 U/min: fz 0,06 → **1140 mm/min**, Vollnut 980, Eintauchen 860,
  Zustellung 1,5 mm. Zum Vergleich ein Schild von Hand: 1200 / Vollnut 800, Zustellung 0,6 (lief optimal).
  Stepcraft empfiehlt für wenig steife Portalmaschinen in NE-Metallen Nuten nur 0,2–0,3 × Ø tief.
- Ø 6, 2 Schneiden, Hartholz: 21 500 U/min, fz 0,12 → 5000 mm/min (Maschinengrenze).


### Eintauchen schräg (Rampe, Helix)

Der Winkel ist ein Richtwert je Werkstoff (`MATERIALIEN[…]["rampe"]`, Grad); der Dialog übernimmt ihn mit
«Richtwerte übernehmen», eine eigene Eingabe bei der Bearbeitung geht vor. Dieselbe Neigung gilt für die
Helix (adaptives Ausräumen, Helix-Bohren). Zu kurze Wege: die Rampe pendelt auf höchstens 4 × Ø hin und zurück,
ohne Platz (unter 0,2 mm) taucht der Fräser senkrecht ein (zählt für die Prüfung).

| Werkstoff | Rampe | Grund |
|---|---|---|
| Weichholz, MDF | 5° | weich, Spiralfräser schneiden gut über die Stirn |
| Hartholz | 4° | |
| Acryl, PVC | 3° | Wärme: nicht zu steil |
| **Aluminium** | **3°** (bis 24.10.: 2°) | VHM-Schaftfräser je Reihe 1–2,5° bzw. 3–5° (IMCO «Helical Ramp»), in der Praxis für Alu meist 3°, offene Dreischneider 3–7° (eMastercam-Forum «Ramp angles for aluminium») |
| Messing | 2° | härter, kurze Späne |
| Zinn | 2° | weich und schmierend; meist V-Fräser (tauchen senkrecht ein) |
| Hartschaum | 10° | kaum Schnittkraft |

Quellen: IMCO Carbide Tool «Helical Ramp» (cms.imcousa.com/wp-content/uploads/2021/01/Helical-Ramp-PDF.pdf),
eMastercam-Forum «ramp angles for aluminium» (emastercam.com/forums/topic/26301). Die grossen
Wendeplatten-Fräser (z. B. Mitsubishi AXD4000) erlauben in Alu viel steiler (bis 20°) – nicht vergleichbar.

### Adaptives Ausräumen: Zuschlag für dünnere Späne

Adaptiv schneidet mit schmaler Schnittbreite ae (Vorgabe 20 %, für volle Zustellung eher 10–15 % des Ø). Bei ae
unter dem halben Ø wird der Span dünner als fz (radiale Spanausdünnung): h = fz · √(1 − (1 − 2·ae/D)²). Damit der
Span so dick bleibt wie bei den Runden (Bahnabstand der Tasche, für den die Schnittdaten gelten), steigt der
Vorschub auf den Schnittbahnen um √(1 − (1 − 2·b)²) / √(1 − (1 − 2·ae/D)²), b = Bahnabstand/100, **höchstens × 1,5**
(Fräsbahnen 25.10.2026, `ops/tasche.span_zuschlag`). Bahnabstand 40 %: ae 20 % → × 1,23, 15 % → × 1,37,
10 % → × 1,5. Eintauchen (Helix) und Übergänge bleiben beim Grundvorschub; der Höchstvorschub der Maschine gilt.
Hersteller rechnen gleich (Harvey Tool «Chip Thinning», Sandvik «average chip thickness»).

## Fräserarten (Faktor auf fz gegenüber dem Schaftfräser)

| Art | Faktor | Herstellerspanne | Hinweis |
|---|---|---|---|
| V-Fräser | 1,0 auf den Ø, der schneidet | – | seit 02.11.2026: Ø = Spitze + 2 · Zustellung · tan(Winkel/2) |
| Gravierstichel (V, Spitze 0 < s ≤ 0,5 mm) | eigene Tabelle | siehe unten | Spindel so schnell wie die Maschine erlaubt |
| Kugelfräser | 0,9 | 0,65–1,0 | |
| Konischer Kugelfräser | 1,0 auf den Ø, der schneidet | 0,2–0,5 auf den oberen Ø | seit 02.11.2026: Kugel-Ø + Kegel bei der Zustellung |
| Eckradiusfräser | 1,0 | 1,0 | |
| Planfräser (Wendeplatten) | eigene Tabelle | siehe unten | fz = k · Ø bis Ø 25, kleine Zustellung |
| Gewindefräser | 0,45 | 0,33–1,0 | |
| Bohrer | 1,0, vc × 0,4 | HM: vc × 0,6, f ≈ 0,05·Ø mm/U in Alu; HSS: vc × 0,25 | Mittel HM/HSS; Holz: keine Angabe |

Der Schaft-Ø spielt für die Schnittdaten nie eine Rolle, nur der Ø der Schneide (Fräser mit dickem Schaft und
dünner Schneide, Planfräser 8 mm Schaft / 22 mm Schneide).

### Gravierstichel (Peter 02.11.2026: 15°/0,2 mm in Alu war 660 / 500, richtig etwa 280 / 50)

1 Schneide, höchste Drehzahl der Maschine; mehr Schneiden: Vorschub × (1 + 0,5 · (Schneiden − 1));
tiefere Drehzahl: Vorschub im Verhältnis.

| Werkstoff | Vorschub mm/min | Eintauchen | Zustellung mm |
|---|---|---|---|
| Aluminium | 250 | 20 % (50) | 0,1 |
| Messing | 200 | 20 % (40) | 0,1 |
| Zinn | 150 | 20 % | 0,1 |
| Hartholz | 1200 | 40 % | 0,8 |
| Weichholz, MDF | 1400 | 45 % | 1,0 |
| Acryl, PVC | 1000 | 40 % | 0,5 |
| Hartschaum | 1500 | 50 % | 1,0 |

Quellen: Applitec MICRO Line, Alfatool «bulini», Makera Single Flute Engraving Bit (Metall und Holz), MAPAL
OptiMill-Graver, Haas Engraving Cutters, Amana Spektra 15/30/45° und 30/60° Engraving – jeweils Mitte der Angaben
bei 0,1–0,3 mm Spitze, umgerechnet auf ≤ 19 000 U/min.

### Planfräser

fz = k · min(Ø, 25), vc eigene Werte, Drehzahl = vc · 1000 / (π · Ø), begrenzt auf die Maschine.

| Werkstoff | vc m/min | k (fz/Ø) | Zustellung mm |
|---|---|---|---|
| Hartholz | 250 | 0,008 | 1,5 |
| Weichholz | 200 | 0,009 | 2,0 |
| MDF | 250 | 0,009 | 1,5 |
| Acryl, PVC | 250 | 0,011 | 1,0 |
| Aluminium | 350 | 0,0054 | 0,5 |
| Messing | 300 | 0,0033 | 0,3 |
| Hartschaum | 250 | 0,012 | 3,0 |
| Zinn | 200 | 0,002 | 0,2 |

Beispiel Ø 22, 2 Schneiden, Hartholz, Maschine 6000–19 000 U/min: 6000 U/min, etwa 2100 mm/min.
Quellen: Sorotec «Fräsparameter Planfräser», Amana Carbide Tipped Spoilboard.

## Quellen

Holz, MDF, Schaum: Onsrud «Soft Wood / Hard Wood / MDF Cutting Data» (woodweb.com/knowledge_base_images/zp/
Chipload_*.pdf); Amana Spiral Plunge (DLC, 46240, Spektra, 2/3 Flute), Low Helix, Ball Nose, V-Groove,
Engraving, Spektra 3D (amanatool.com, toolstoday.com); Stepcraft «Fräsparameter» (SC_Fraesparameter.pdf);
Mekanika «CNC Feeds & Speeds Explained»; Coastal/Onsrud «Tooling News 5»; RAMPF RAKU TOOL SB-0301/0470.

Kunststoffe: Onsrud «Hard Plastic»; Amana Plastic O-Flute v2, Spektra Plastic O-Flute v19, Double O-Flute v6,
Acrylic O-Flute; LEUCO «Kunststoffe bearbeiten»; Garant 209320/209322 (Hoffmann); Stepcraft; Mekanika PMMA-
und HDPE-Guide; Trinseo «Plexiglas Fabrication Manual»; Westlake Royal «Cutting Routing and Milling PVC».

Metalle: Amana Aluminum O-Flute, ZrN Ball Nose, Square End Mill, Soft Cutting Aluminum; Onsrud (zitiert von
CNC Router Store); Stepcraft; Mekanika; Harvey 9245xx und Engraving Cutters; Datron Katalog; Horn.

Fräserarten: Amana 90° Engraving, AMS-159 V-Groove, Spoilboard, Corner Radius; Harvey Engraving (Pointed,
Tipped Off), Ball/Corner Radius Aluminum, Plastics Ball/Square, Tapered Ball, Single Form Thread Mill,
Miniature Drills; Dapra «Feed/Speed Diameter Compensation»; RedLine Threadmill/Endmill; Gühring 6515–5768;
Garant HSS N 114150; Eylert Eylhandbuch.

Links (Auswahl):
- https://www.woodweb.com/knowledge_base_images/zp/Chipload_Softwood.pdf, …_Hardwood.pdf, …_MDF.pdf
- https://www.woodweb.com/images_forums_public/cnc/Hard_Plastic.pdf
- https://www.amanatool.com/pub/media/productattachments/Solid-Carbide-Spiral-Plunge-Solid-Wood-DLC-Coated-Speed-Chart.pdf
- https://es.global.toolstoday.com/content/ProductFile/Attachments/Solid-Carbide-Spektra-Spiral-Plunge-2-3-Flute-n.pdf
- https://www.amanatool.com/pub/media/productattachments/Plastic-O-Flute-Speed-Chart-v2.pdf
- https://es.global.toolstoday.com/content/ProductFile/Attachments/Aluminum-O-Flute-Speed-Chart.pdf
- https://www.amanatool.com/pub/media/productattachments/Carbide-Tipped-Spoilboard-2-3-4-Wing-Speed-Chart-v4.pdf
- https://oldjoomla.stepcraft-systems.com/images/SC-Service/SC_Fraesparameter.pdf
- https://www.mekanika.io/blog/learn-1/feeds-speeds-explained-12
- https://www.leuco.com/DE/CH/media/DOK_MBR_LEUCO-Kunststoffbearbeitung_DL.pdf
- https://ecomm.productivity.com/customer/docs/skudocs/HAT2_Speeds_Feeds_924531.pdf (Harvey Alu)
- https://harveyperformance.widen.net/content/arbteiznw1/original/SF_772900.pdf (Harvey Gewindefräser)
- https://harveyperformance.widen.net/content/tddg3aq0we/pdf/SF_BAF0000.pdf (Harvey Bohrer)
- https://dapra.com/sbn/feed-speed-dia-compensation
- https://www.applitec-tools.com/wp-content/uploads/2023/07/APPLITEC_catalogue-2020-2022_parametres_coupe_MICRO_Line.pdf
- https://www.vemas.it/pdf/products/alfatool/bulini_alfatool.pdf
- https://global.makera.com/products/single-flute-engraving-bit-for-metal-1-8-shank
- https://shop.mapal.com/medias/sys_master/documents/h4f/h2e/10167194189854/OptiMill-Graver_DE/OptiMill-Graver-DE.pdf
- https://cdn.shopify.com/s/files/1/1558/5651/files/Spektra-15-30-45-Degree-Engraving-Speed-Chart.pdf
- https://webseite.sorotec.de/download/fraesparameter/schnittwerte_planfraeser.pdf
- https://cdn.shopify.com/s/files/1/1558/5651/files/Carbide-Tipped-Spoilboard-3-4-Wing-Speed-Chart.pdf
