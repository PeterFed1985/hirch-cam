// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: das Fräsen-Panel (Material, Bearbeitungen, Ausgabe).
 *
 * Die Zeichnung bestimmt über die Linienfarbe, was gefräst wird. Das Panel
 * zeigt die Bearbeitungen, lässt sie einstellen und ordnen, rechnet im
 * Hintergrund (Python-Rechenkern hirch_cam mit dem CAM-Modul hirch_cam) und speichert
 * den G-Code. Die Einstellungen stehen in der Zeichnung (hirch:einstellungen).
 */
#ifndef SEEN_HIRCH_FRAESEN_H
#define SEEN_HIRCH_FRAESEN_H

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <gtkmm/eventbox.h>

#include <gtkmm/box.h>
#include <gtkmm/window.h>

#include "ui/dialog/dialog-base.h"

class InkscapeWindow;
class SPDesktop;
class SPItem;
namespace Geom { class Affine; }
namespace Gtk { class Box; class Widget; }
class SPDesktopWidget;

namespace Inkscape::UI::Dialog {

class HirchFraesen : public DialogBase
{
public:
    HirchFraesen();
    ~HirchFraesen() override;

    void materialBearbeiten();
    void werkzeugeBearbeiten();
    /// erster Start: Ersteinrichtung zeigen, wenn nötig – true, wenn sie gezeigt wurde
    bool ersteinrichtungWennNoetig();
    void vorschauZeigen();
    void gcodeSpeichern();
    void bahnenZeigen(bool an);
    void gravurText();
    void aktualisieren();
    void spannmittel();
    void stege();
    void aufraeumen(std::string const &endung, bool nach_import);
    void verschachteln();
    void zusatzOeffnen(std::string const &datei);
    bool zweiteSeite();
    void maschineFuellen(Gtk::Box *ziel);
    void werkstoffeFuellen(Gtk::Box *ziel);
    void vorgabenFuellen(Gtk::Box *ziel);
    /// Einlinienschrift importieren (Einstellungen › Schriften); info bekommt Zwischenstand und Ergebnis
    void schriftImportieren(Gtk::Window &eltern, std::function<void(std::string const &)> info);
    void zeichentafel(Gtk::Window &eltern);
    void alsVorlage();
    void gespiegeltSpeichern();
    void ausVorlage();
    void fassungen();
    void postprozessoren();
    void darstellungZeigen();
    // Menü «Fräsen» (Sammelauftrag 21.10., Punkt 2): dieselben Funktionen wie die Knöpfe im Panel
    void ordnen();
    void zusammenfassungZeigen();
    void aufStickSpeichern();
    void luftfraesenSpeichern();
    void werkzeugeExportieren();
    void werkzeugeImportieren();
    void messkorrektur();
    /// Drehpunkt für «Anordnen»: Nullpunkt (Dokument-px) der Seite, auf der (x, y) liegt
    bool nullpunktFuer(double x, double y, double &nx, double &ny);
    /// 3D: ein Modell von beiden Seiten fräsen – schaltet «Zweiseitig» ein (Seite 2, Wendeachse)
    /// und legt die 3D-Bearbeitungen für Seite 2 an (hirch-3d.cpp)
    void reliefZweiseitig(bool an, std::string const &achse);
    /// 3D: beim Einlesen 3D-Schruppen und 3D-Schlichten mit passenden Werkzeugen anlegen (nur wenn
    /// es noch keine 3D-Bearbeitung gibt); fertig bekommt den Hinweistext (hirch-3d.cpp)
    void reliefBearbeitungen(double tiefe, std::function<void(std::string const &)> fertig);
    /// 3D: Bearbeitungen für erkannte Bohrungen und Taschen anlegen (hirch-3d.cpp). json: Liste von
    /// {"art": "bohren"|"tasche", "tiefe", "starttiefe", "titel"}; Rückgabe: Farbe je Eintrag (für
    /// die Linien, die dazugehören), vor der ersten Kontur eingefügt, ein Schritt Rückgängig.
    std::vector<std::string> bearbeitungenAnlegen(std::string const &json);
    /// 3D: Bereich einer Bearbeitung setzen («alles», «innen», «aussen», «flaechen», «ohne_flaechen»;
    /// Reiter «3D» › «Gewählte Flächen als Bereich», hirch-3d.cpp); ein Schritt Rückgängig
    void bereichSetzen(std::string const &id, std::string const &bereich);
    // für den Oberflächentest (hirch-test.cpp)
    int anzahlBearbeitungen();
    void bearbeitungOeffnen(int index);
    /// wie «+ Neu» › Fräsart (färbt die Auswahl, öffnet den Dialog, rechnet erst nach «Übernehmen»)
    void neuAnlegen(std::string const &art);
    /// Dialog «Neue Zeichnung einrichten» (wie beim Start einer neuen Zeichnung)
    /// modell: gleich dieses 3D-Modell vorwählen
    void anfangsdialog(std::string const &modell = {});
    /// Dialog «Spindel und Halterung» (Einstellungen › Maschine; hirch-3d.cpp)
    void halterung();
    /// Für den Reiter «3D»: letzte Rechnung als JSON {svg, ok, programme, kollisionen} (leer = keine)
    std::string rechnungFuer3D();
    /// Laufende Rechnung abbrechen (Knopf in Panel, Leiste, Reiter «3D»)
    void rechnenAbbrechen();
    /// Neu rechnen, auch ohne Änderung an der Zeichnung (z. B. Entwurfsmodus umgeschaltet)
    void neuRechnen();
    /// «Rechnen» (3D-Zeichnungen rechnen auf Knopfdruck): jetzt rechnen
    void jetztRechnen();
    bool wartetAufKnopf();
    /// 3D-Ansicht, Rechtsklick auf die Zeitleiste (B-30): «Ab hier neu ausgeben». ab: {"programm": n, "zeile": z}
    /// (Zeile der Vorschau-Datei, nur mit genau gerechnetem Ergebnis) oder {"key": Bearbeitung}; wo: Text für die Meldung
    void abHierAusgeben(std::string const &ab_json, std::string const &wo);
    /// Zeigt das letzte Ergebnis den 3D-Entwurf (gröber; die Zeilen passen nicht zur genauen Ausgabe)?
    bool ergebnisEntwurf();
    bool beschaeftigt();
    /// Inhalt des Reiters «Werkzeuge» (Werkzeuge der Zeichnung) in ziel aufbauen
    void werkzeugListe(Gtk::Box *ziel);
    /// Knöpfe der beschrifteten Fräsfarben (Statusleiste) in ziel aufbauen
    void farbleisteFuellen(Gtk::Box *ziel);

private:
    void desktopReplaced() override;
    void documentReplaced() override;
    void selectionChanged(Inkscape::Selection *selection) override;

    struct Impl;
    std::unique_ptr<Impl> _impl;
};

/// Reiter «Ansicht» neben «Fräsen»: dunkle Darstellung, feine Linien, Fräsbahnen, andere Seite
class HirchAnsicht : public DialogBase
{
public:
    HirchAnsicht();
    ~HirchAnsicht() override;
    /// Menü «Fräsen › Einstellungen»: zum Abschnitt (Überschrift wie «MASCHINE») blättern
    void abschnittZeigen(std::string const &kopf);
    /// Menü: dieselben Knöpfe wie im Abschnitt «Schriften»
    void schriftImportieren();
    void zeichentafel();

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

/// Beschriftete Fräsfarben links in der Statusleiste: je Bearbeitung ein Knopf in ihrem
/// Farbton (ohne Bearbeitung: die Fräsart), Klick färbt die Auswahl
class HirchFarbleiste : public Gtk::EventBox
{
public:
    HirchFarbleiste();
    ~HirchFarbleiste() override;
    void setDesktop(SPDesktop *d);
    void auffrischen();
    SPDesktop *desktop() const { return _desktop; }
    /// alle Leisten dieser Arbeitsfläche neu aufbauen
    static void alle_auffrischen(SPDesktop *d);
    /// Rechenanzeige: Kärtchen mit Kreisel, Uhr und × neben der Leiste (laeuft = false: weg)
    static void rechenstand(SPDesktop *d, bool laeuft, std::string const &uhr);
    /// 3D-Zeichnung noch nicht gerechnet: Knopf «▶ Rechnen» neben der Leiste (an = false: weg)
    static void wartet(SPDesktop *d, bool an);
    /// Symbole der Seitenleiste: das Fenster, das gerade in der rechten Spalte steht, ist gedrückt
    void spalte_stand();
    /// so hoch wie die Zeichenfläche; was nicht passt, rollt
    void hoehe_begrenzen();

private:
    SPDesktop *_desktop = nullptr;
    struct Teile;
    std::unique_ptr<Teile> _t;
    void bauen();
    void zustand();
    void eingabe_form();
};

/// Reiter «Werkzeuge» neben «Fräsen»: alle Werkzeuge der Zeichnung mit Bild,
/// Schnittdaten und wo sie benutzt werden; bearbeiten, neu, entfernen
class HirchWerkzeuge : public DialogBase
{
public:
    HirchWerkzeuge();
    ~HirchWerkzeuge() override;
    void auffrischen();

private:
    void documentReplaced() override;
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace Inkscape::UI::Dialog

/// Nach dem Speichern einer Zeichnung: datierte Kopie in «.versionen/<Name>/» (die letzten 20)
void hirch_version_sichern(std::string const &pfad);
/// Hirch CNC: nach dem Import von DXF, AI, PDF … aufräumen (file.cpp)
void hirch_nach_import(std::string const &pfad);
/// Hirch CNC: nach dem Öffnen von AI, PDF, EPS, Illustrator-SVG nur die Linien behalten (inkscape-application.cpp)
class SPDocument;
void hirch_nach_oeffnen(std::string const &pfad, SPDocument *doc);
/// Oberflächentest: Ballast entfernen wie nach dem Import aus Illustrator (Bericht zurück)
std::vector<std::string> hirch_ballast_probe(std::string const &ein, std::string const &aus);
/// Datei in einem Zusatzformat (Liste im Zusatz im Einstellungsordner, siehe hirch_cam/zusatz.py): umwandeln und
/// in einem neuen Fenster öffnen. true: übernommen (Öffnen/Importieren von Inkscape nicht weiter versuchen).
bool hirch_zusatz_oeffnen(std::string const &pfad);
/// Fenster in der einen Spalte rechts zeigen (an Stelle von Fräsen; holt es aus einer anderen Spalte)
void hirch_spalte_zeigen(SPDesktop *desk, std::string const &typ);

/// Pfadeffekt-Punktfelder (live_effects/parameter/point.cpp, z. B. «Ursprung» bei rotierten Kopien):
/// Umrechnung Objektkoordinaten → angezeigte Werte in mm vom Werkstück-Nullpunkt, Y nach oben.
/// false: keine Fräszeichnung (Panel zu, noch keine Analyse) – dann zeigt Inkscape wie gewohnt.
bool hirch_punkt_anzeige(SPDesktop *desktop, SPItem const *item, Geom::Affine &item_zu_anzeige);

namespace Inkscape::UI::Dialog {
/// Ansicht «2D Zeichnung | 3D Ansicht» im Hauptfenster (hirch-ansicht.cpp). Die 3D-Sitzung meldet ihr Widget an;
/// es wird je Fenster einmal gebaut und nimmt in 3D den Platz der Zeichenfläche ein.
using HirchAnsicht3DFabrik = std::function<Gtk::Widget *(SPDesktop *)>;
void hirch_ansicht_3d_anmelden(HirchAnsicht3DFabrik fabrik);
bool hirch_ansicht_3d_da();
Gtk::Widget *hirch_ansicht_zeile(SPDesktopWidget *dtw);      // Zeile mit dem Umschalter (desktop-widget.cpp)
void hirch_ansicht_3d_setzen(SPDesktopWidget *dtw, bool an);
bool hirch_ansicht_3d_ist(SPDesktopWidget *dtw);
void hirch_ansicht_aktionen(InkscapeWindow *win);            // win.hirch-ansicht, win.hirch-ansicht-wechseln
/// Panel → 3D-Ansicht: Bearbeitung in der Zeitleiste anspringen und wählen. Schlüssel wie engine «abschnitte» key
/// (Bearbeitungs-ID, bei Teilen «ID#teil», Passlöcher «pass»). Die 3D-Sitzung meldet die Funktion an.
using HirchAnsicht3DSprung = std::function<void(SPDesktop *, std::string const &schluessel)>;
void hirch_ansicht_3d_sprung_anmelden(HirchAnsicht3DSprung sprung);
void hirch_ansicht_3d_springen(SPDesktop *desktop, std::string const &schluessel);
} // namespace Inkscape::UI::Dialog

/// Aktionen win.hirch-* (Menü «Fräsen»)
void add_actions_hirch(InkscapeWindow *win);

#endif // SEEN_HIRCH_FRAESEN_H
