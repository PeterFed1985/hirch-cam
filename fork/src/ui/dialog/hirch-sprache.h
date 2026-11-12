// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Übersetzung der Hirch-Texte (Deutsch → Englisch).
 *
 * Die Texte stehen deutsch im Quelltext; TR() liefert sie in der eingestellten Sprache.
 * Der Katalog liegt bei den Python-Dateien (hirch_cam/sprache/en.json) und gilt auch für
 * die Meldungen des Rechenkerns. Muster mit {} passen auf Texte mit Zahlen oder Namen.
 */
#ifndef SEEN_HIRCH_SPRACHE_H
#define SEEN_HIRCH_SPRACHE_H

#include <string>
#include <vector>

namespace Inkscape::UI::Dialog {

/// Englisch eingestellt (Einstellungen › Sprache, sonst nach der Systemsprache)?
bool hirch_englisch();
/// "de" oder "en" – für den Rechenkern (Umgebungsvariable HIRCH_SPRACHE)
char const *hirch_sprache();
/// Text in der eingestellten Sprache (feste Texte; der Zeiger bleibt gültig)
char const *TR(char const *de);
/// Auch zusammengesetzte Texte (Muster mit {})
std::string TR(std::string const &de);
/// Ganzer Satz mit Platzhaltern «{}»: übersetzt, dann der Reihe nach gefüllt
std::string TRF(char const *muster, std::vector<std::string> const &werte);
/// Steht der Text (deutsch) im Katalog? – für den Oberflächentest: so ein Text in der
/// englischen Oberfläche wurde nicht übersetzt
bool hirch_im_katalog(std::string const &text);

// ---------------------------------------------------------------------------
// Einheit (Peter 08.11.: mm und Zoll für den Release). Intern ist alles mm – in Dateien, Einstellungen, im Rechenkern
// und im G-Code (G21). Nur Anzeige und Eingabe wandeln um, und zwar nur hier: Zahlenfelder über einheit_feld(),
// Texte über einheit_text() (TR und TRF tun das selbst). Eine Prüfung (tests/test_einheit*.py) sucht nach «mm», die
// an dieser Stelle vorbeigehen.
// ---------------------------------------------------------------------------
enum class Groesse { LAENGE, VORSCHUB, FLAECHE, SCHNITT };   // SCHNITT: Schnittgeschwindigkeit m/min ↔ ft/min (SFM)
/// Zoll eingestellt (Einstellungen › Programm › Einheit, Ersteinrichtung)?
bool hirch_zoll();
/// "mm" oder "in" – für den Rechenkern (Umgebungsvariable HIRCH_EINHEIT)
char const *hirch_einheit();
/// Einheit zum Anzeigen: «mm» / «in», «mm/min» / «in/min», «mm²» / «in²»
std::string einheit_name(Groesse g = Groesse::LAENGE);
/// mm (intern) → Anzeige und zurück
double einheit_zeigen(double mm, Groesse g = Groesse::LAENGE);
double einheit_lesen(double anzeige, Groesse g = Groesse::LAENGE);
/// «12,5 mm» bzw. «0.492 in» – stellen gilt für mm, Zoll bekommt passend mehr
std::string einheit_zahl(double mm, int stellen = 2, Groesse g = Groesse::LAENGE, bool mit_einheit = true);
/// Kurze Masse (Ø, R) in Zoll: «1/4 Zoll» (Bruch, wenn der Wert einer ist), sonst «0,236 Zoll». Bei mm: mm_text.
std::string einheit_mass(double mm, std::string const &mm_text, bool bruch = true);
/// Zoll als Bruch wie im Handel: «1/4», «1-1/2», «3/8» (Nenner bis 32, ab 1/32); leer, wenn kein Bruch passt
std::string zoll_bruch(double zoll);
/// Lange Wege (Fräsweg): «12,3 m» bzw. «40,4 ft»
std::string einheit_weg(double mm);
/// Texte mit «… mm», «mm/min», «mm²»: bei Zoll umgerechnet (Zahl und Einheit). Bei mm unverändert.
std::string einheit_text(std::string const &text);
/// Nur Zahlen mit Einheit umrechnen («6 mm»), ein einzelnes «mm» bleibt (für fremde Texte, z. B. von Inkscape)
std::string einheit_text_zahlen(std::string const &text);
/// Zahlenfeld zeigt und nimmt Zoll; Wert (get_value/set_value) bleibt mm. Bei mm: nichts.
/// Gibt den Namen der Einheit für die Beschriftung zurück.
std::string einheit_feld(void *spinbutton, Groesse g = Groesse::LAENGE);
/// Ist die Beschriftung eines Feldes eine Längen- oder Vorschub-Einheit? (für spin_zeile)
bool einheit_erkennen(std::string const &beschriftung, Groesse &g);
/// Die Beschriftung mit der Einheit neben einem Zahlenfeld (gleiche Zeile im Grid oder nächstes Kind der Box), sonst
/// nullptr. Gilt für die automatische Umrechnung und für den Oberflächentest.
void *einheit_nachbar(void *spinbutton);
/// Zahlenfeld ohne Längen-Einheit, auch wenn «mm» daneben steht (z. B. Verhältnis k = mm je mm Ø)
void einheit_nie(void *spinbutton);

} // namespace Inkscape::UI::Dialog

#endif
