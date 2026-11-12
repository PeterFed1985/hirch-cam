// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: 3D-Fräsung – 3D-Modelle einlesen, auf der Zeichnung platzieren und im
 * Reiter «3D» ansehen und einstellen.
 *
 * In der Zeichnung steht ein 3D-Modell als <svg:image> (graues Schattenbild der Draufsicht)
 * mit dem Attribut hirch:relief (Name, Art, Ausrichtung, Lage im Material, …). Die
 * Originaldatei (STL, OBJ, STEP oder Graustufenbild) steht gzip-komprimiert und base64-kodiert
 * in einem <hirch:reliefdaten> in den <defs>; die Zeichnung braucht die Datei danach nicht mehr.
 * Gerechnet wird im Python-Rechenkern (hirch_cam/relief.py, Befehl «relief»).
 */
#ifndef SEEN_HIRCH_3D_H
#define SEEN_HIRCH_3D_H

#include <array>
#include <functional>
#include <memory>
#include <string>

#include "ui/dialog/dialog-base.h"

class InkscapeWindow;
class SPDocument;
namespace Gtk { class Widget; }
class SPDesktop;

namespace Inkscape::UI::Dialog {

/// Reiter «3D» neben «Fräsen»: 3D-Ansicht des gewählten Modells im Werkstück, Höhe im
/// Material, Ausrichtung; zweiseitig: Trennfläche und Haltestege (Rechtecke auf der Zeichnung
/// mit hirch:steg3d = {"relief": id, "z": Höhe der Stegmitte oder null = automatisch})
class Hirch3D : public DialogBase
{
public:
    Hirch3D();
    ~Hirch3D() override;
    /// Dialog «3D-Modell einlesen» öffnen
    void einlesen();
    /// Datei ohne Dialog einlesen (Standard-Ausrichtung, mittig in die Mitte des Materials);
    /// zweiseitig: danach «Von beiden Seiten fräsen» einschalten (Anfangsdialog)
    void einlesenDatei(std::string const &pfad, bool zweiseitig = false);
    /// Stelle, an der Spindel oder Halterung streift (JSON-Liste «kollisionen» der Rechnung; leer = keine)
    void kollisionZeigen(std::string const &json);
    /// Neue Rechnung im Panel (JSON {svg, ok, programme, kollisionen}): Markierung und Fräsergebnis
    void rechnungNeu(std::string const &json);
    /// Ansicht «Fräsergebnis» (an) oder «Modell» (aus)
    void fraesergebnisZeigen(bool an);
    /// Rechenanzeige vom Panel: läuft eine Rechnung? (Text und Uhr über der 3D-Ansicht im Fräsergebnis)
    void rechenstand(bool laeuft, std::string const &text, std::string const &uhr);
    /// 3D-Zeichnung noch nicht gerechnet: über der 3D-Ansicht text und «Rechnen» (an = false: weg)
    void wartet(bool an, std::string const &text);
    /// Für den Oberflächentest: rechnet der Reiter noch?
    bool beschaeftigt() const;
    /// Fenster «3D-Modell» in der rechten Spalte nach vorne holen (zeigt das gewählte Modell, sonst das erste)
    void zeigen();
    /// «Haltestege setzen» aus der Leiste: zum Abschnitt Haltestege des gewählten Modells rollen
    void stegeZeigen();
    /// Für den Oberflächentest: Klick ins Fräsergebnis bei (fx, fy) – Anteile der Ansicht (0…1);
    /// rest: danach «Restmaterial hier nachfräsen» drücken, wenn der Knopf da ist (true: gedrückt)
    bool klickTesten(double fx, double fy, bool rest);
    /// Für den Oberflächentest: Klick auf das Modell (Ansicht «Modell») bei (fx, fy), mit Umschalt
    void modellKlickTesten(double fx, double fy, bool umschalt = false);
    /// Für den Oberflächentest: Anzahl gewählter Flächen, «Gewählte Flächen als Bereich» drücken
    int flaechenGewaehlt() const;
    void flaechenAlsBereichTesten();

private:
    /// nach dem Einlesen: 3D-Schruppen und 3D-Schlichten mit Werkzeugen anlegen (Panel «Fräsen»)
    void bearbeitungen_anlegen(double modell_hoehe, double dicke, std::function<void()> danach);
    void documentReplaced() override;
    void selectionChanged(Inkscape::Selection *selection) override;
    void selectionModified(Inkscape::Selection *selection, guint flags) override;

    struct Impl;
    std::unique_ptr<Impl> _impl;
};

/// Nullpunkt am Block (Anfangsdialog «Neue Zeichnung einrichten», auch für 2D): Werkstück
/// schräg von vorne links, 9 Punkte oben (Z0 Materialoberfläche), 5 sichtbare unten (Z0
/// Werktisch), Umschalter Oberfläche/Werktisch. xy wie einst.nullpunkt.xy: erstes Zeichen
/// u/m/o (vorne/Mitte/hinten), zweites l/m/r.
struct NullpunktWahl {
    std::string xy = "ul";
    bool unten = false;
    double b = 300, h = 200, d = 18, dx = 0, dy = 0;
    bool rund = false;                            // Zylinder stehend (Ø = b): nur Mitte oben/unten
    std::array<double, 4> modell{0, 0, 0, 0};   // Modell als Rechteck (x, y, Breite, Höhe; 0 = keines)
    std::function<void()> neu_zeichnen;           // vom Element gesetzt
};
Gtk::Widget *nullpunkt_block(std::shared_ptr<NullpunktWahl> wahl);

/// Masse eines 3D-Modells (Datei), z. B. für den Anfangsdialog. fertig(ok, b, h, d, bild, fehler)
/// läuft im Hauptprogramm, sobald der Rechenkern fertig ist.
void hirch_3d_masse(std::string const &pfad,
                    std::function<void(bool, double, double, double, bool, std::string const &)> fertig);

/// Liegt in der Zeichnung ein 3D-Modell? (Auswahl «Tiefe gemessen ab» in den Bearbeitungen)
bool hirch_3d_hat_modell(SPDocument *doc);
namespace hirch3d {
/// B-14: Bilder der Unterseite (Seite 2) ihrem Modell nachführen (hirch-3d-grund.cpp)
void seite2_nachfuehren(SPDocument *doc);
}

/// 3D-Ansicht für das Hauptfenster (Umschalter «2D Zeichnung | 3D Ansicht», Platz im Fenster: Basis):
/// zeigt dieselbe Szene wie der Reiter «3D» dieses Fensters (Modell, Fräsergebnis, Stege, gewählte
/// Flächen) mit eigener Kamera; Klicks wie dort. Solange der Reiter fehlt: ein Hinweis.
Gtk::Widget *hirch_ansicht_3d_neu(SPDesktop *desktop);

} // namespace Inkscape::UI::Dialog

/// Fenster «3D-Modell» öffnen und nach vorne holen (Doppelklick aufs Modell, Menü, Symbol der Seitenleiste)
void hirch_3d_fenster(SPDesktop *desk);

/// Menü «Fräsen › 3D-Modell einlesen …»
void hirch_3d_einlesen(InkscapeWindow *win);

#endif // SEEN_HIRCH_3D_H
