// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Werkzeugleiste «Auswahl»: Neun-Punkte-Bezugsfeld und Drehwinkel (Teile von SelectToolbar)
 * Aus dem Inkscape-Patch hierher verschoben (29.10.2026, nur verschoben) – im Patch bleiben die Aufrufe.
 */
#include "select-toolbar.h"
#include <2geom/rect.h>
#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/box.h>
#include <gtkmm/enums.h>
#include <gtkmm/image.h>
#include <gtkmm/togglebutton.h>
#include "desktop.h"
#include "document-undo.h"
#include "document.h"
#include "message-stack.h"
#include "object/sp-item-transform.h"
#include "object/sp-namedview.h"
#include "page-manager.h"
#include "selection.h"
#include "ui/builder-utils.h"
#include "ui/icon-names.h"
#include "ui/widget/canvas.h" // Focus widget
#include "ui/widget/combo-tool-item.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/toolbar-menu-button.h"
#include "ui/widget/unit-tracker.h"
#include "widgets/widget-sizes.h"
#include "seltrans.h"
#include "ui/tools/select-tool.h"
#include "ui/dialog/hirch-sprache.h"

using Inkscape::DocumentUndo;

namespace Inkscape::UI::Toolbar {

// Hirch CNC: Neun-Punkte-Bezugsfeld. Zeigt und setzt den Bezugspunkt der Auswahl – derselbe
// wie ein einmal angeklickter Anfasser auf der Zeichenfläche: X/Y beziehen sich auf ihn, beim
// Ändern von Breite/Höhe bleibt er stehen.
void SelectToolbar::hirch_bezug_bauen()
{
    // Die Hirch-Knöpfe aus toolbar-select.ui tragen deutsche Tooltips; übersetzt wird mit dem Hirch-Katalog
    // (en.json), nicht mit gettext (Windows-Sitzung 28.10.: in der englischen Oberfläche standen sie deutsch)
    for (char const *id : {"hirch_links", "hirch_hmitte", "hirch_rechts", "hirch_oben", "hirch_vmitte", "hirch_unten",
                           "hirch_material"}) {
        if (auto w = _builder->get_object(id)) {
            if (auto b = dynamic_cast<Gtk::Widget *>(w.get())) {
                auto t = b->get_tooltip_text();
                if (!t.empty()) b->set_tooltip_text(Inkscape::UI::Dialog::TR(t.raw()));
            }
        }
    }
    auto &box = get_widget<Gtk::Box>(_builder, "hirch_bezug_box");
    _hirch_bezug = Gtk::make_managed<Gtk::DrawingArea>();
    _hirch_bezug->set_size_request(24, 24);
    _hirch_bezug->set_valign(Gtk::ALIGN_CENTER);
    _hirch_bezug->add_events(Gdk::BUTTON_PRESS_MASK);
    _hirch_bezug->set_tooltip_markup(std::string("<b>") + Inkscape::UI::Dialog::TR("Bezugspunkt") + "</b>\n" +
        Inkscape::UI::Dialog::TR("Worauf sich X und Y beziehen und welcher Punkt beim Ändern von Breite "
                                 "und Höhe stehen bleibt. Klick auf einen Punkt wählt ihn, ein zweiter "
                                 "Klick hebt ihn wieder auf. Auch auf der Zeichenfläche: einen Anfasser "
                                 "einmal anklicken."));
    box.add(*_hirch_bezug);

    // aktuelle Zelle (Bildschirm: Zeile 0 = oben) aus dem Bezugspunkt der Auswahl
    auto zelle = [this](int &sp, int &ze, bool &gesetzt) {
        sp = 0;
        ze = 0;
        gesetzt = false;
        if (!_desktop) return;
        auto sel = _desktop->getSelection();
        double ax = sel->has_anchor ? sel->anchor_x : 0.5, ay = sel->has_anchor ? sel->anchor_y : 0.5;
        gesetzt = sel->has_anchor;
        sp = std::clamp((int)std::lround(ax * 2), 0, 2);
        ze = std::clamp((int)std::lround((_desktop->yaxisdir() > 0 ? ay : 1.0 - ay) * 2), 0, 2);
    };
    _hirch_bezug->signal_draw().connect([this, zelle](Cairo::RefPtr<Cairo::Context> const &cr) {
        auto ctx = _hirch_bezug->get_style_context();
        auto fg = ctx->get_color(_hirch_bezug->get_state_flags());
        double w = _hirch_bezug->get_allocated_width(), h = _hirch_bezug->get_allocated_height();
        double r0 = 4, sx = (w - 2 * r0) / 2, sy = (h - 2 * r0) / 2;
        int sp, ze;
        bool gesetzt;
        zelle(sp, ze, gesetzt);
        bool aktiv = _desktop && !_desktop->getSelection()->isEmpty();
        cr->set_line_width(1);
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), aktiv ? 0.35 : 0.15);
        cr->rectangle(r0 + 0.5, r0 + 0.5, 2 * sx - 1, 2 * sy - 1);
        cr->stroke();
        for (int j = 0; j < 3; j++) {
            for (int i = 0; i < 3; i++) {
                double x = r0 + i * sx, y = r0 + j * sy;
                bool an = aktiv && i == sp && j == ze;
                if (an) {
                    cr->set_source_rgb(1.0, 0.55, 0.0);   // Orange wie der Fortschritt
                    cr->arc(x, y, 3.5, 0, 2 * M_PI);
                    cr->fill();
                } else {
                    cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), aktiv ? 0.75 : 0.3);
                    cr->arc(x, y, 1.6, 0, 2 * M_PI);
                    cr->fill();
                }
            }
        }
        return true;
    });
    _hirch_bezug->signal_button_press_event().connect([this, zelle](GdkEventButton *ev) {
        if (!_desktop || ev->button != 1 || _desktop->getSelection()->isEmpty()) return false;
        auto st = dynamic_cast<Inkscape::UI::Tools::SelectTool *>(_desktop->getTool());
        if (!st || !st->_seltrans) return false;
        double w = _hirch_bezug->get_allocated_width(), h = _hirch_bezug->get_allocated_height();
        int i = std::clamp((int)(ev->x / (w / 3)), 0, 2), j = std::clamp((int)(ev->y / (h / 3)), 0, 2);
        int sp, ze;
        bool gesetzt;
        zelle(sp, ze, gesetzt);
        if (gesetzt && i == sp && j == ze) {
            st->_seltrans->hirchBezugSetzen(-1, 0);   // zweiter Klick: aufheben
        } else {
            st->_seltrans->hirchBezugSetzen(i, j);
        }
        _hirch_bezug->queue_draw();
        return true;
    });
}

// Hirch CNC (Peter, 23.10.): Drehwinkel der Auswahl, absolut (hirch:drehung, siehe ObjectSet::applyAffine).
// Eintippen dreht um den Bezugspunkt (ohne gewählten Punkt die Mitte).
void SelectToolbar::hirch_winkel_bauen()
{
    auto &box = get_widget<Gtk::Box>(_builder, "h_box");
    auto l = Gtk::make_managed<Gtk::Label>("∠");
    l->set_margin_start(10);
    l->set_tooltip_text(Inkscape::UI::Dialog::TR("Drehwinkel"));
    auto adj = Gtk::Adjustment::create(0.0, -360.0, 360.0, 1.0, 15.0, 0.0);
    _hirch_winkel = Gtk::make_managed<Gtk::SpinButton>(adj, 1.0, 1);
    _hirch_winkel->set_width_chars(6);
    _hirch_winkel->set_tooltip_markup(std::string("<b>") + Inkscape::UI::Dialog::TR("Drehwinkel") + "</b>\n" +
        Inkscape::UI::Dialog::TR("In Grad, positiv gegen den Uhrzeiger. Hirch CAM merkt sich die Drehung je "
                                 "Objekt, auch nach dem Speichern. Eintippen dreht um den Bezugspunkt."));
    auto grad = Gtk::make_managed<Gtk::Label>("°");
    box.add(*l);
    box.add(*_hirch_winkel);
    box.add(*grad);
    l->show();
    _hirch_winkel->show();
    grad->show();
    adj->signal_value_changed().connect([this] {
        if (_hirch_winkel_setzt || !_desktop) return;
        auto sel = _desktop->getSelection();
        if (sel->isEmpty()) return;
        auto bbox = sel->preferredBounds();
        if (!bbox) return;
        double ist = 0;
        if (auto it = sel->singleItem(); it && it->getRepr()) {
            if (auto a = it->getRepr()->attribute("hirch:drehung")) ist = g_ascii_strtod(a, nullptr);
        } else if (auto first = *sel->items().begin(); first && first->getRepr()) {
            if (auto a = first->getRepr()->attribute("hirch:drehung")) ist = g_ascii_strtod(a, nullptr);
        }
        double delta = _hirch_winkel->get_value() - ist;
        if (std::abs(delta) < 1e-9) return;
        auto punkt = bbox->min() + bbox->dimensions() * Geom::Scale(sel->anchor_x, sel->anchor_y);
        // Bildschirm: positiv gegen den Uhrzeiger
        sel->rotateRelative(punkt, delta * -_desktop->yaxisdir());     // Desktop-Koordinaten
        Inkscape::DocumentUndo::done(_desktop->getDocument(), Inkscape::UI::Dialog::TR("Drehen"),
                                     INKSCAPE_ICON("object-rotate-left"));
    });
}

void SelectToolbar::hirch_winkel_zeigen()
{
    if (!_hirch_winkel || !_desktop) return;
    auto sel = _desktop->getSelection();
    _hirch_winkel->set_sensitive(!sel->isEmpty());
    double w = 0;
    if (!sel->isEmpty()) {
        auto first = *sel->items().begin();
        if (first && first->getRepr()) {
            if (auto a = first->getRepr()->attribute("hirch:drehung")) w = g_ascii_strtod(a, nullptr);
        }
    }
    _hirch_winkel_setzt = true;
    _hirch_winkel->set_value(w);
    _hirch_winkel_setzt = false;
}

} // namespace Inkscape::UI::Toolbar
