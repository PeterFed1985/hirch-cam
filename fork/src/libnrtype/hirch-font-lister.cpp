// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Einlinienschriften in der Schriftliste (Teile von FontLister).
 * Aus dem Inkscape-Patch hierher verschoben (29.10.2026, nur verschoben) – im Patch bleiben die Aufrufe.
 */
#include "font-lister.h"
#include <fstream>
#include <map>
#include <set>
#include <cairomm/context.h>
#include <cairomm/surface.h>
#include <pango/pangocairo.h>
#include <gdkmm/general.h>
#include "io/resource.h"
#include <glibmm/markup.h>
#include <glibmm/regex.h>
#include <gtkmm/cellrenderertext.h>
#include <gtkmm/settings.h>
#include <libnrtype/font-instance.h>
#include <string>
#include "font-factory.h"
#include "display/cairo-utils.h"
#include <2geom/transforms.h>
#include <cmath>
#include "desktop.h"
#include "desktop-style.h"
#include "document.h"
#include "inkscape.h"
#include "preferences.h"
#include "object/sp-object.h"
#include "object/sp-root.h"
#include "object/sp-anchor.h"
#include "object/sp-text.h"
#include "object/sp-tspan.h"
#include "object/sp-textpath.h"
#include "object/sp-tref.h"
#include "object/sp-flowtext.h"
#include "object/sp-flowdiv.h"
#include "util/font-collections.h"
#include "util/recently-used-fonts.h"
#include "util/document-fonts.h"
#include "xml/repr.h"

namespace Inkscape {

namespace {
std::set<Glib::ustring> &hirch_einlinien()
{
    static std::set<Glib::ustring> namen;
    return namen;
}
bool &hirch_einlinien_gelesen()
{
    static bool gelesen = false;
    return gelesen;
}
std::map<Glib::ustring, Glib::RefPtr<Gdk::Pixbuf>> &hirch_bilder()
{
    static std::map<Glib::ustring, Glib::RefPtr<Gdk::Pixbuf>> bilder;
    return bilder;
}
} // namespace

bool FontLister::ist_einlinie(Glib::ustring const &family)
{
    if (!hirch_einlinien_gelesen()) {
        hirch_einlinien_gelesen() = true;
        auto pfad = Inkscape::IO::Resource::profile_path("fonts/.hirch-einlinien.txt");
        std::ifstream f(pfad);
        std::string z;
        while (std::getline(f, z)) {
            if (!z.empty()) hirch_einlinien().insert(z);
        }
    }
    return hirch_einlinien().count(family) > 0;
}

Glib::RefPtr<Gdk::Pixbuf> FontLister::einlinie_bild(Glib::ustring const &family, Glib::ustring const &text,
                                                   bool dunkel, int hoehe)
{
    auto schluessel = family + "\n" + text + (dunkel ? "\nd" : "\nh") + std::to_string(hoehe);
    auto &bilder = hirch_bilder();
    if (auto f = bilder.find(schluessel); f != bilder.end()) return f->second;
    int breite = 420;
    auto flaeche = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, breite, hoehe);
    auto cr = Cairo::Context::create(flaeche);
    // Umrisse aus Inkscapes eigener Schrift (wie auf der Zeichenfläche). Nicht über die Standard-Schriftliste von
    // Pango: unter Windows kennt sie die mitgebrachten Einlinienschriften nicht und zeigt eine Ersatzschrift
    // (Windows 355999b, Peter 10.11.: «Bitte fixe auch die kleine Vorschau»).
    auto font = FontFactory::get().FaceFromFontSpecification(family.c_str());
    double const s = hoehe * 0.62;              // Schriftgrösse in Bildpunkten (1 em)
    double const grund = hoehe * 0.78;          // Grundlinie
    double x = 2;
    if (font) {
        for (auto it = text.begin(); it != text.end() && x < breite; ++it) {
            int const gid = font->MapUnicodeChar(*it);
            if (gid <= 0) continue;
            if (auto pv = font->PathVector(gid); pv && !pv->empty()) {
                feed_pathvector_to_cairo(cr->cobj(), *pv * (Geom::Scale(s, -s) * Geom::Translate(x, grund)));
            }
            x += font->Advance(gid, false) * s;
        }
    }
    if (dunkel) cr->set_source_rgba(0.85, 0.85, 0.85, 0.75);
    else cr->set_source_rgba(0.35, 0.35, 0.35, 0.85);
    cr->set_line_width(1.0);
    cr->set_line_join(Cairo::LINE_JOIN_ROUND);
    cr->set_line_cap(Cairo::LINE_CAP_ROUND);
    cr->stroke();
    PangoRectangle logisch{0, 0, (int)std::ceil(x), hoehe};
    int b = std::min(breite, std::max(1, logisch.width + 6));
    auto bild = Gdk::Pixbuf::create(flaeche, 0, 0, b, hoehe);
    bilder[schluessel] = bild;
    return bild;
}

void FontLister::schriften_neu_laden()
{
    FontFactory::get().refreshConfig();
    pango_family_map = FontFactory::get().GetUIFamilies();
    hirch_einlinien_gelesen() = false;
    hirch_einlinien().clear();
    hirch_bilder().clear();
    init_font_families(-1);
    new_fonts_signal.emit();
}

} // namespace Inkscape
