// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CAM: Fenster «Hilfe › Rückmeldung senden …» (Sitzung rueckmeldungen, Entwurf 2 freigegeben 04.11.2026).
 *
 * Braucht nur GTK und hirch-sprache.h – die Anbindung an Inkscape (Menü, Zeichnung schreiben) steht in
 * hirch-aktionen.cpp. Gesendet wird über Python (hirch_cam/rueckmeldung.py --json), nur auf Knopfdruck.
 */
#ifndef SEEN_HIRCH_RUECKMELDUNG_H
#define SEEN_HIRCH_RUECKMELDUNG_H

#include <functional>
#include <string>

namespace Gtk { class Window; }

namespace Inkscape::UI::Dialog {

struct RueckmeldungKontext {
    std::string python;          ///< hirch_python()
    std::string pythonpath;      ///< Ordner mit hirch_cam
    std::string version;         ///< Inhalt von VERSION, z. B. «2026.11.04-ffe424e»
    std::string sprache;         ///< "de" oder "en" (hirch_sprache())
    std::string zeichnung_name;  ///< Name der offenen Zeichnung; leer = keine offen
    /// schreibt den jetzigen Stand der Zeichnung in `pfad` (nur wenn der Nutzer das Häkchen setzt)
    std::function<bool(std::string const &pfad)> zeichnung_schreiben;
};

/// Öffnet das Fenster (eines je Programm). `eltern` ist das Hirch-Fenster – davon kommt das Bildschirmfoto.
void rueckmeldung_zeigen(Gtk::Window *eltern, RueckmeldungKontext k);

} // namespace Inkscape::UI::Dialog

#endif
