// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: 3D-Vorschau der Fräsprogramme, nativ mit OpenGL (GtkGLArea).
 *
 * Das Material ist ein Höhenbild in einer Tiefentextur auf der Grafikkarte.
 * Jede Fräsbewegung wird als Fläche gezeichnet, die der Fräser überstreicht;
 * der Fragment-Shader rechnet die Tiefe der Fräserspitze, der Tiefentest
 * behält das Minimum. Angezeigt wird direkt aus dieser Textur.
 * Läuft unter Linux und Windows gleich (kein Browser, kein WebKit).
 */
#ifndef SEEN_HIRCH_VORSCHAU_H
#define SEEN_HIRCH_VORSCHAU_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <gtkmm/window.h>
#include "ui/dialog/hirch-abtrag.h"

namespace Inkscape::UI::Dialog {

struct VorschauAbschnitt {
    int zeile = 1;              // ab dieser Zeile der Datei (1 = erste)
    std::string farbe;          // #rrggbb der Bearbeitung
    std::string name;           // Name der Bearbeitung
    std::string id;             // Schlüssel der Bearbeitung (engine «abschnitte» → key)
};

struct VorschauSpannmittel {
    std::vector<std::pair<double, double>> punkte;   // Umriss (mm, Material unten links)
    double hoehe = 15;                               // über der Materialoberfläche
    int seite = 1;                                   // zweiseitig: auf welcher Seite es spannt
    int spannung = 1;                                // Umspannen: 1 vorher, 2 nach dem Umspannen
};

/// Umspannen: Sperrzone um die Spannmittel einer Seite (aussen-Ring, mm wie die Spannmittel) – dort fräst erst
/// Spannung 2
struct VorschauZone {
    int seite = 1;
    std::vector<std::pair<double, double>> ring;
};

/// Schraube durch die Mitte (rundes Material): Kopf auf der Oberfläche
struct VorschauSchraube {
    double x = 0, y = 0, kopf = 0, ueberstand = 0;
};

struct VorschauProgramm {
    std::string datei;          // Pfad der .tap-Datei
    std::vector<VorschauAbschnitt> abschnitte;   // Farben je Bearbeitung (leer: Farbe je Werkzeug)
    std::string werkzeug;       // Name des Werkzeugs (Anzeige)
    std::string form = "flach"; // flach | v | kugel
    double durchmesser = 3, vwinkel = 60, spitze = 0, eckradius = 0;
    // 3D-Ansicht: Schneidenlänge, Schaft-Ø, Übergang, Ausspann- und Gesamtlänge (mm; 0 = unbekannt)
    double schneide = 0, schaft = 0, uebergang = 0, ausspann = 0, gesamt = 0;
    int schneiden = 2;          // Zahl der Schneiden (Spannuten in der Ansicht)
    char drall = 'R';           // R aufwärts (Rechtsdrall), L abwärts, K Kompression, G gerade (V-Fräser)
    int seite = 1;              // zweiseitig: 1 = Oberseite, 2 = nach dem Wenden
    int spannung = 1;           // Umspannen: 1 = vor, 2 = nach dem Umspannen
    int t = 0;                  // Werkzeugnummer in der Datei (0: nach Reihenfolge)
};

/// Was laut Zeichnung weg soll (Taschen): für «Restmaterial zeigen»
/// Gefrästes Gewinde (Formteil) – siehe hirch-abtrag.h
using VorschauGewinde = AbtragGewinde;

struct VorschauSoll {
    int seite = 1;
    double tiefe = 0;
    std::vector<std::vector<std::vector<double>>> flaechen;   // je Fläche: Ringe (x, y, x, y, …), erster aussen
};

struct VorschauDaten {
    double dicke = 18, breite = 0, hoehe = 0, sicherheit = 20;
    double null_x = 0, null_y = 0;   // Nullpunkt der Maschine (mm vom Material unten links)
    bool null_unten = false;         // Z0 auf der Unterseite statt auf der Oberfläche
    bool zweiseitig = false;         // Programme mit seite 2 fräsen das gewendete Werkstück
    int ausgaben = 0;                // so viele Dateien werden gespeichert (0: unbekannt)
    bool um_y = false;               // gewendet um die Y-Achse (sonst um X)
    double rund_r = 0;               // rundes Material: Radius (Mitte = Mitte des Materials), 0 = Platte
    std::vector<VorschauSpannmittel> spannmittel;
    std::vector<VorschauZone> zonen;     // Umspannen: Sperrzonen je Seite (leer: kein Umspannen)
    // Spannung 2 ohne eingezeichnete Spannmittel: Eilgänge ausserhalb der Zone mindestens auf «hoehe2» (eingetragen,
    // 0: keine), sonst höchstes Spannmittel der Seite + Abstand (wie engine._umspannen_hoehe2)
    double umspann_hoehe2 = 0, umspann_abstand = 10;   // hoehe2 <= 0: nicht eingetragen
    // Spannmutter und Spindel (Einstellungen › Maschine › «Spindel und Halterung», mm; 0 = nicht eingetragen) – die
    // 3D-Ansicht zeichnet sie über der Ausspannlänge (Peter 05.11.)
    double mutter_d = 0, mutter_h = 0, spindel_d = 0;
    std::vector<VorschauSchraube> schrauben;
    std::vector<VorschauSoll> soll;
    std::vector<VorschauGewinde> gewinde;
    std::vector<VorschauProgramm> programme;
    std::vector<std::string> bericht, warnungen;
    std::string meldung;        // statt einer Vorschau: diesen Text zeigen
};

/// Fräser als Dreiecke (x, y, z, nx, ny, nz je Ecke; Spitze bei 0, Achse +z) für beide 3D-Ansichten: Schneide
/// mit Spannuten im Drall, Übergang, Schaft bis zur Ausspannlänge, mit Gesamtlänge der Rest darüber. «drehung»
/// dreht ihn um seine Achse (rad; die Spindel dreht mit M3 im Uhrzeigersinn von oben, also abnehmend). Ältere
/// Ergebnisse ohne Längen (ausspann = 0): Schneide geschätzt, Schaft 25 mm, alles in einer Farbe.
struct FraeserNetz {
    std::vector<float> ecken;
    int n_schneide = 0;         // Ecken der Schneide (zuerst)
    int n_oben = 0;             // Ecken des Schafts über der Spannmutter (zuletzt, blass)
};
void fraeser_netz(VorschauProgramm const &P, double drehung, FraeserNetz &aus);

/// Pfeil in Fahrrichtung (dx, dy) neben dem Fräser (Radius r), flach auf der Höhe z, gleiche Eckenform
void fraeser_pfeil(double r, double z, double dx, double dy, std::vector<float> &aus);

class HirchVorschau : public Gtk::Window
{
public:
    HirchVorschau();
    ~HirchVorschau() override;

    /// Neue Programme zeigen (oder eine Meldung, wenn daten.meldung gesetzt ist)
    void zeigen(VorschauDaten const &daten);

    /// «Ab hier neu ausgeben»: Programm (1 = erstes) und Zeile der Datei an der Stelle des Reglers
    std::function<void(int programm, int zeile)> ab_hier;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace Inkscape::UI::Dialog

#endif // SEEN_HIRCH_VORSCHAU_H
