// SPDX-License-Identifier: GPL-2.0-or-later
// Hirch CAM: zentrale Liste, was von Inkscape erreichbar bleibt (Peter 07.11., Windows-Bericht «Inkscape-Werkzeuge»).
// Was hier nicht erlaubt ist, lässt sich auf keinem Weg öffnen: nicht über das Menü ▾ der Fenster, nicht über eine
// Taste, nicht über die Erweiterten Einstellungen. Neues aus späteren Inkscape-Versionen ist damit zuerst gesperrt.
// Die Listen stehen in hirch-erlaubt.cpp.
#ifndef HIRCH_ERLAUBT_H
#define HIRCH_ERLAUBT_H

#include <string_view>

namespace HirchErlaubt {

bool fenster(std::string_view name);         // Fenster (DialogContainer, Menü ▾)
bool werkzeug(std::string_view name);        // Werkzeug wählen (win.tool-switch)
bool werkzeug_knopf(std::string_view name);  // Knopf in der Werkzeugleiste links
bool pfadeffekt(int typ);                    // Liste «Pfadeffekt hinzufügen» (LivePathEffect::EffectType)
bool speichern(std::string_view id);         // Dateityp beim Speichern (Kennung der Ausgabe)
bool einstellungsseite(int id);              // Seite der Erweiterten Einstellungen (PREFS_PAGE_…)

} // namespace HirchErlaubt

#endif
