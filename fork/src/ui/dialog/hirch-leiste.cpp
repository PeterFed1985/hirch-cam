// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Hover-Kärtchen und schwebende Werkzeugleiste
 */
#include "ui/dialog/hirch-hilfen.h"
#include "ui/dialog/dialog-container.h"
#include "ui/dialog/dialog-data.h"
#include "ui/dialog/dialog-notebook.h"
#include "ui/dialog/hirch-3d.h"
#include <functional>
#include <gtkmm/treeview.h>

namespace Inkscape::UI::Dialog {

void hover_karte(Gtk::Widget &w, std::string const &markup)
{
    struct Karte {
        Gtk::Popover *pop = nullptr;
        sigc::connection timer;
    };
    auto k = std::make_shared<Karte>();
    k->pop = Gtk::make_managed<Gtk::Popover>(w);
    k->pop->set_modal(false);
    k->pop->set_position(Gtk::POS_LEFT);
    auto l = Gtk::make_managed<Gtk::Label>();
    l->set_markup(markup);
    l->set_line_wrap(true);
    l->set_max_width_chars(36);
    l->set_xalign(0);
    l->set_margin_top(8);
    l->set_margin_bottom(8);
    l->set_margin_start(10);
    l->set_margin_end(10);
    l->show();
    k->pop->add(*l);
    w.add_events(Gdk::ENTER_NOTIFY_MASK | Gdk::LEAVE_NOTIFY_MASK | Gdk::BUTTON_PRESS_MASK);
    w.signal_enter_notify_event().connect([k](GdkEventCrossing *e) {
        if (e->mode != GDK_CROSSING_NORMAL || k->pop->get_visible()) return false;
        k->timer.disconnect();
        k->timer = Glib::signal_timeout().connect([k] {
            k->pop->popup();
            return false;
        }, 450);
        return false;
    }, false);
    w.signal_leave_notify_event().connect([k, &w](GdkEventCrossing *e) {
        if (e->mode != GDK_CROSSING_NORMAL) return false;
        // Das Kärtchen legt beim Aufgehen ein Fenster über den Knopf: nur schliessen, wenn
        // die Maus den Knopf wirklich verlassen hat
        int x = -1, y = -1;
        Gdk::ModifierType mt;
        auto gw = w.get_window();
        auto disp = Gdk::Display::get_default();
        if (gw && disp && disp->get_default_seat()) {
            gw->get_device_position(disp->get_default_seat()->get_pointer(), x, y, mt);
            auto a = w.get_allocation();
            if (!w.get_has_window() && x >= a.get_x() && y >= a.get_y() && x < a.get_x() + a.get_width() &&
                y < a.get_y() + a.get_height())
                return false;
        }
        k->timer.disconnect();
        k->pop->popdown();
        return false;
    }, false);
    w.signal_button_press_event().connect([k](GdkEventButton *) {
        k->timer.disconnect();
        k->pop->popdown();
        return false;
    }, false);
    w.signal_unmap().connect([k] { k->timer.disconnect(); });
}

// ---------------------------------------------------------------------------
// Schwebende Werkzeugleiste rechts auf der Zeichenfläche: Ansicht, Werkzeuge, Fräsfarben
static std::set<HirchFarbleiste *> &farbleisten()
{
    static std::set<HirchFarbleiste *> s;
    return s;
}

/// Symbole der Leiste (24er-Raster, Strich in der Textfarbe)
static void leisten_symbol(Cr const &cr, std::string const &n)
{
    cr->set_line_width(2);
    cr->set_line_cap(Cairo::LINE_CAP_ROUND);
    cr->set_line_join(Cairo::LINE_JOIN_ROUND);
    auto rr = [&](double x, double y, double w, double h, double r) {
        cr->begin_new_sub_path();
        cr->arc(x + w - r, y + r, r, -M_PI / 2, 0);
        cr->arc(x + w - r, y + h - r, r, 0, M_PI / 2);
        cr->arc(x + r, y + h - r, r, M_PI / 2, M_PI);
        cr->arc(x + r, y + r, r, M_PI, 1.5 * M_PI);
        cr->close_path();
    };
    if (n == "zu") {
        cr->move_to(9, 6); cr->line_to(15, 12); cr->line_to(9, 18);
    } else if (n == "auf") {
        cr->move_to(15, 6); cr->line_to(9, 12); cr->line_to(15, 18);
    } else if (n == "bahnen") {
        cr->move_to(4, 18); cr->line_to(4, 7);
        cr->curve_to(4, 5.5, 5, 5, 6, 5); cr->line_to(9, 5);
        cr->curve_to(10, 5, 11, 5.5, 11, 7); cr->line_to(11, 17);
        cr->curve_to(11, 18.5, 12, 19, 13, 19); cr->line_to(16, 19);
        cr->curve_to(17, 19, 18, 18.5, 18, 17); cr->line_to(18, 5);
    } else if (n == "andere") {
        rr(3, 5, 12, 12, 1.5);
        cr->stroke();
        std::vector<double> d = {2, 2.5};
        cr->set_dash(d, 0);
        rr(9, 8, 12, 12, 1.5);
        cr->stroke();
        cr->unset_dash();
        return;
    } else if (n == "verschachteln") {
        rr(3, 3, 8, 11, 1); rr(14, 3, 7, 7, 1); rr(3, 17, 8, 4, 1);
        cr->begin_new_sub_path(); cr->arc(17.5, 17, 3.5, 0, 2 * M_PI);
    } else if (n == "stege") {
        cr->move_to(3, 15); cr->line_to(8, 15); cr->line_to(8, 12); cr->line_to(11, 12); cr->line_to(11, 15);
        cr->line_to(13, 15); cr->line_to(13, 12); cr->line_to(16, 12); cr->line_to(16, 15); cr->line_to(21, 15);
        cr->move_to(3, 19); cr->line_to(21, 19);
    } else if (n == "spannmittel") {
        cr->move_to(5, 4); cr->line_to(15, 4); cr->line_to(15, 9); cr->line_to(9, 9); cr->line_to(9, 15);
        cr->line_to(15, 15); cr->line_to(15, 20); cr->line_to(5, 20); cr->close_path();
        cr->move_to(15, 6.5); cr->line_to(19, 6.5); cr->move_to(15, 17.5); cr->line_to(19, 17.5);
        cr->move_to(19, 4); cr->line_to(19, 20);
    } else if (n == "gravur") {
        cr->move_to(4, 6); cr->line_to(4, 4); cr->line_to(16, 4); cr->line_to(16, 6);
        cr->move_to(10, 4); cr->line_to(10, 18); cr->move_to(7, 18); cr->line_to(13, 18);
        cr->move_to(18, 11); cr->line_to(21, 14); cr->line_to(17, 18); cr->line_to(14, 18); cr->line_to(14, 15);
        cr->close_path();
    } else if (n == "aufraeumen") {
        cr->move_to(4, 18); cr->curve_to(6, 12, 10, 9, 14, 8);
        cr->begin_new_sub_path(); cr->arc(16, 7, 2.2, 0, 2 * M_PI);
        cr->move_to(19, 13); cr->line_to(20, 15); cr->line_to(22, 16); cr->line_to(20, 17); cr->line_to(19, 19);
        cr->line_to(18, 17); cr->line_to(16, 16); cr->line_to(18, 15); cr->close_path();
    } else if (n == "nullpunkt") {
        cr->begin_new_sub_path(); cr->arc(12, 12, 6, 0, 2 * M_PI);
        cr->move_to(12, 2); cr->line_to(12, 7); cr->move_to(12, 17); cr->line_to(12, 22);
        cr->move_to(2, 12); cr->line_to(7, 12); cr->move_to(17, 12); cr->line_to(22, 12);
    }
    cr->stroke();
}

Gtk::Widget *symbol_bild(std::string const &name, int groesse)
{
    auto da = Gtk::make_managed<Gtk::DrawingArea>();
    da->set_size_request(groesse, groesse);
    da->signal_draw().connect([da, name, groesse](Cr const &cr) {
        auto f = da->get_style_context()->get_color(da->get_state_flags());
        cr->set_source_rgba(f.get_red(), f.get_green(), f.get_blue(), f.get_alpha());
        cr->scale(groesse / 24.0, groesse / 24.0);
        leisten_symbol(cr, name);
        return true;
    });
    return da;
}

/// Farbfeld einer Fräsart: voll (eine Bearbeitung), geteilt (mehrere), hohl (noch keine), schraffiert
Gtk::Widget *farbfeld_leiste(std::vector<std::string> const &farben, std::string const &leer_hex,
                                    bool schraffiert)
{
    auto da = Gtk::make_managed<Gtk::DrawingArea>();
    da->set_size_request(20, 20);
    da->set_halign(Gtk::ALIGN_CENTER);
    da->set_valign(Gtk::ALIGN_CENTER);
    da->signal_draw().connect([farben, leer_hex, schraffiert](Cr const &cr) {
        auto rr = [&](double x, double y, double w, double h, double r) {
            cr->begin_new_sub_path();
            cr->arc(x + w - r, y + r, r, -M_PI / 2, 0);
            cr->arc(x + w - r, y + h - r, r, 0, M_PI / 2);
            cr->arc(x + r, y + h - r, r, M_PI / 2, M_PI);
            cr->arc(x + r, y + r, r, M_PI, 1.5 * M_PI);
            cr->close_path();
        };
        double r, g, b;
        if (schraffiert) {
            rr(1, 1, 18, 18, 4);
            cr->save();
            cr->clip();
            cr->set_source_rgb(0.84, 0.83, 0.81);
            cr->paint();
            cr->set_source_rgb(0.60, 0.60, 0.58);
            cr->set_line_width(2);
            for (int i = -20; i < 40; i += 5) {
                cr->move_to(i, 20);
                cr->line_to(i + 20, 0);
            }
            cr->stroke();
            cr->restore();
            return true;
        }
        if (farben.empty()) {
            hex_rgb(leer_hex, r, g, b);
            rr(2.5, 2.5, 15, 15, 4);
            cr->set_source_rgb(r, g, b);
            cr->set_line_width(2.2);
            cr->stroke();
            return true;
        }
        rr(1, 1, 18, 18, 4);
        cr->save();
        cr->clip();
        int n = std::min<int>(3, farben.size());
        for (int i = 0; i < n; ++i) {
            hex_rgb(farben[i], r, g, b);
            cr->set_source_rgb(r, g, b);
            // schräg geteilt: Streifen von links oben nach rechts unten
            double a = 40.0 * i / n - 10, e = 40.0 * (i + 1) / n - 10;
            cr->move_to(a, 0);
            cr->line_to(e, 0);
            cr->line_to(e - 20, 20);
            cr->line_to(a - 20, 20);
            cr->close_path();
            cr->fill();
        }
        cr->restore();
        return true;
    });
    return da;
}

static void schein_zeichnen(SPDesktop *d, std::vector<CanvasItemPtr<CanvasItemBpath>> &schein);

struct HirchFarbleiste::Teile {
    Gtk::Box aussen{Gtk::ORIENTATION_HORIZONTAL, 8};   // Kärtchen «Rechnet» links, Leiste rechts
    Gtk::Box karte{Gtk::ORIENTATION_HORIZONTAL, 6};
    Gtk::Label uhr;
    Gtk::Button karte_x;
    Gtk::Button rechnen;              // «▶ Rechnen» – 3D-Zeichnung noch nicht gerechnet
    bool rechnet = false, wartet = false;
    Gtk::Box rahmen{Gtk::ORIENTATION_VERTICAL, 3};
    Gtk::Box rest{Gtk::ORIENTATION_VERTICAL, 3};
    Gtk::Box farben{Gtk::ORIENTATION_VERTICAL, 3};
    Gtk::ToggleButton bahnen, andere;
    Gtk::Box ansicht{Gtk::ORIENTATION_VERTICAL, 2};    // Umschalter 2D | 3D ganz oben (Peter, 22.10.)
    Gtk::ToggleButton k2d, k3d;
    Gtk::Separator ansicht_trenner{Gtk::ORIENTATION_HORIZONTAL};
    bool ist_3d = false;
    sigc::connection lage;
    // ausgewählte Pfade leuchten (Peter, 22.10.: in einer Gruppe sah man nur den Rahmen)
    std::vector<CanvasItemPtr<CanvasItemBpath>> schein;
    sigc::connection sel_anders, sel_geaendert;
    sigc::connection schein_bald;
    Inkscape::PrefObserver schein_beobachter;
    int x2d = -1, y2d = -1;           // 2D: Lücke rechts und oben zum Zeichenbereich – in 3D dieselbe
    bool zweite_seite = false;
    std::vector<Inkscape::PrefObserver> beobachter;
    sigc::connection blass;
    sigc::connection eltern_groesse;
    bool setzt = false;
    // Seitenleiste ohne Reiter (Peter 06.11.): Symbole für die seltenen Fenster in der rechten Spalte
    Gtk::Box spalte{Gtk::ORIENTATION_VERTICAL, 1};
    // kleine Fenster (Windows 07.11.: unter ~1000 px fehlten unten Symbole): Farben und seltene Fenster brechen in
    // eine zweite Spalte links um (Peter 07.11.); reicht auch das nicht, rollt die Leiste
    Gtk::ScrolledWindow roll;
    Gtk::Box spalten{Gtk::ORIENTATION_HORIZONTAL, 2};
    Gtk::Box zweite{Gtk::ORIENTATION_VERTICAL, 3};
    Gtk::Box grp_farben{Gtk::ORIENTATION_VERTICAL, 3}, grp_spalte{Gtk::ORIENTATION_VERTICAL, 1};
    Gtk::Separator trenn_farben{Gtk::ORIENTATION_HORIZONTAL}, trenn_spalte{Gtk::ORIENTATION_HORIZONTAL};
    std::vector<std::pair<std::string, Gtk::ToggleButton *>> spalte_knoepfe;
};

HirchFarbleiste::HirchFarbleiste()
    : _t(std::make_unique<Teile>())
{
    css_einrichten();
    farbleisten().insert(this);
    set_halign(Gtk::ALIGN_END);
    set_valign(Gtk::ALIGN_START);
    set_margin_top(12);
    set_margin_end(12);
    set_visible_window(false);
    add_events(Gdk::ENTER_NOTIFY_MASK | Gdk::LEAVE_NOTIFY_MASK);
    klasse(_t->rahmen, "hirch-leiste");
    // Kärtchen «Rechnet …» (Kreisel, Uhr, ×) links neben der Leiste – nur während gerechnet wird
    klasse(_t->karte, "hirch-rechenkarte");
    _t->karte.set_valign(Gtk::ALIGN_START);
    _t->karte.add(_t->uhr);
    _t->karte_x.set_label("×");
    _t->karte_x.set_relief(Gtk::RELIEF_NONE);
    _t->karte_x.set_tooltip_text(TR("Rechnen abbrechen"));
    _t->karte_x.signal_clicked().connect([this] {
        auto c = _desktop ? _desktop->getContainer() : nullptr;
        if (auto f = c ? dynamic_cast<HirchFraesen *>(c->get_dialog("Fraesen")) : nullptr) f->rechnenAbbrechen();
    });
    _t->karte.add(_t->karte_x);
    _t->karte.set_no_show_all(true);
    _t->aussen.add(_t->karte);
    _t->rechnen.set_label(TR("▶ Jetzt rechnen"));
    _t->rechnen.set_valign(Gtk::ALIGN_START);
    _t->rechnen.set_tooltip_text(TR("3D-Zeichnung: Fräsbahnen jetzt rechnen"));
    klasse(_t->rechnen, "hirch-rechenknopf");
    _t->rechnen.signal_clicked().connect([this] {
        auto c = _desktop ? _desktop->getContainer() : nullptr;
        if (auto f = c ? dynamic_cast<HirchFraesen *>(c->get_dialog("Fraesen")) : nullptr) f->jetztRechnen();
    });
    _t->rechnen.set_no_show_all(true);
    _t->aussen.add(_t->rechnen);
    _t->aussen.add(_t->rahmen);
    add(_t->aussen);
    // B-1 (26.10./30.10.): die Leiste liegt im Overlay in einem eigenen Fenster so gross wie Kärtchen + Knopf +
    // Leiste zusammen – ohne Eingabeform schluckte es auch die Klicks auf die Zeichnung unter der Lücke neben der
    // Leiste. Eingaben nur dort annehmen, wo wirklich etwas steht.
    signal_size_allocate().connect([this](Gtk::Allocation &) { eingabe_form(); }, true);
    // Fenster kleiner oder grösser: die Leiste so hoch wie die Zeichenfläche, der Rest rollt (Windows 07.11.)
    signal_hierarchy_changed().connect([this](Gtk::Widget *) {
        _t->eltern_groesse.disconnect();
        if (auto e = get_parent())
            _t->eltern_groesse = e->signal_size_allocate().connect([this](Gtk::Allocation &) {
                Glib::signal_idle().connect_once([this] { if (farbleisten().count(this)) hoehe_begrenzen(); });
            });
    });
    bauen();
    set_opacity(0.62);
    signal_enter_notify_event().connect([this](GdkEventCrossing *) {
        _t->blass.disconnect();
        set_opacity(1.0);
        return false;
    });
    signal_leave_notify_event().connect([this](GdkEventCrossing *) {
        _t->blass.disconnect();
        // erst ausblenden, wenn die Maus wirklich weg ist (Knöpfe melden beim Betreten
        // ein «Verlassen» der Leiste)
        _t->blass = Glib::signal_timeout().connect([this] {
            int x = -1, y = -1;
            Gdk::ModifierType mt;
            auto gw = get_window();
            auto disp = Gdk::Display::get_default();
            if (gw && disp && disp->get_default_seat()) {
                gw->get_device_position(disp->get_default_seat()->get_pointer(), x, y, mt);
                auto a = get_allocation();     // im Fenster der Zeichenfläche (die Leiste hat kein eigenes)
                if (x >= a.get_x() && y >= a.get_y() && x < a.get_x() + a.get_width() &&
                    y < a.get_y() + a.get_height())
                    return true;               // Maus noch auf der Leiste: weiter warten
            }
            if (!_t->rechnet) set_opacity(0.62);
            return false;
        }, 400);
        return false;
    });
    show_all();
}

HirchFarbleiste::~HirchFarbleiste()
{
    _t->eltern_groesse.disconnect();
    _t->sel_anders.disconnect();
    _t->sel_geaendert.disconnect();
    _t->schein_bald.disconnect();
    _t->schein.clear();
    _t->blass.disconnect();
    _t->lage.disconnect();
    farbleisten().erase(this);
}


// ---- Seitenleiste ohne Reiter (Peter 06.11.2026, Entwurf «b, Fenster in der Spalte») -------------------------
// Fräsen steht fest in der rechten Spalte. Die seltenen Fenster (Pfadeffekte, Ebenen, Ausrichten, Text,
// Einstellungen) öffnen sich in derselben Spalte an Stelle von Fräsen; oben führt «‹ Fräsen» zurück. Die Reiter
// der Spalte sind ausgeblendet – umgeschaltet wird mit den Symbolen der Leiste. Kein Eingriff in Inkscape: die
// Spalte ist Inkscapes DialogNotebook, nur ohne sichtbare Reiter.
struct SpaltenFenster { char const *typ, *titel, *icon; };
static std::vector<SpaltenFenster> const &spalten_fenster()
{
    static std::vector<SpaltenFenster> const f = {
        {"LivePathEffect", "Pfadeffekte", "dialog-path-effects"},
        {"Objects", "Ebenen und Objekte", "dialog-objects"},
        {"AlignDistribute", "Ausrichten und verteilen", "dialog-align-and-distribute"},
        {"Text", "Text und Schrift", "dialog-text-and-font"},
        {"HirchAnsicht", "Einstellungen", "preferences-system"},
        {"Hirch3D", "3D-Modell", "draw-cuboid"},          // nur mit einem Modell in der Zeichnung (3d 06.11.)
    };
    return f;
}

static DialogNotebook *spalte_notebook(SPDesktop *desk)
{
    auto c = desk ? desk->getContainer() : nullptr;
    auto f = c ? c->get_dialog("Fraesen") : nullptr;
    for (Gtk::Widget *w = f; w; w = w->get_parent())
        if (auto nb = dynamic_cast<DialogNotebook *>(w)) return nb;
    return nullptr;
}

static void spalte_zeigen(SPDesktop *desk, std::string const &typ);

/// Das DialogNotebook, in dem ein Fenster steckt (nullptr: schwebend)
static Gtk::Widget *spalte_nb_von(Gtk::Widget *d)
{
    for (Gtk::Widget *w = d ? d->get_parent() : nullptr; w; w = w->get_parent())
        if (dynamic_cast<DialogNotebook *>(w)) return w;
    return nullptr;
}

/// Fenster aus einer anderen Spalte in die Spalte von Fräsen holen; die leere Spalte schliessen
static void spalte_holen(DialogNotebook *nb, DialogBase *d)
{
    auto *von = dynamic_cast<DialogNotebook *>(spalte_nb_von(d));
    if (!nb || !von || von == nb) return;
    nb->move_page(*d);
    if (von->get_notebook()->get_n_pages() == 0) von->close_notebook_callback();
}

static std::string spalte_titel(std::string const &typ)
{
    for (auto const &f : spalten_fenster())
        if (typ == f.typ) return TR(f.titel);
    auto const &d = get_dialog_data();
    auto it = d.find(typ);
    if (it == d.end()) return typ;
    std::string l = it->second.label.raw();
    l.erase(std::remove(l.begin(), l.end(), '_'), l.end());
    return l;
}

/// Kopfzeile «‹ Fräsen   TITEL» über jedem Fenster ausser Fräsen (einmal je Fenster)
static void spalte_kopf(SPDesktop *desk, DialogBase *d)
{
    if (!d || d->get_type() == "Fraesen" || d->get_data("hirch-spalte-kopf")) return;
    d->set_data("hirch-spalte-kopf", (void *)1);
    auto kopf = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 10);
    kopf->set_margin_start(6);
    kopf->set_margin_end(10);
    kopf->set_margin_top(4);
    kopf->set_margin_bottom(4);
    auto zurueck = Gtk::make_managed<Gtk::Button>(TR("‹ Fräsen"));
    zurueck->set_relief(Gtk::RELIEF_NONE);
    zurueck->set_tooltip_text(TR("Zurück zu Fräsen (Escape)"));
    zurueck->signal_clicked().connect([desk] { spalte_zeigen(desk, "Fraesen"); });
    kopf->add(*zurueck);
    auto t = Gtk::make_managed<Gtk::Label>(Glib::ustring(spalte_titel(d->get_type().raw())).uppercase());
    t->set_xalign(0);
    t->set_hexpand(true);
    t->set_ellipsize(Pango::ELLIPSIZE_END);
    klasse(*t, "hirch-kopf");
    kopf->add(*t);
    d->pack_start(*kopf, false, false);
    d->reorder_child(*kopf, 0);
    kopf->show_all();
    // Escape im Fenster: zurück zu Fräsen. Inkscape fängt Escape schon am Fenster ab (DialogBase, Capture) –
    // dort fragt es zuerst hirch_dialog_escape (Windows 07.11.: Escape ging nicht)
    // Listen fangen Escape selbst ab (Suchfeld der Liste), bevor es beim Fenster ankommt (Windows-Nacht a349d01:
    // Sonderzeichen, Auswahl in der Schriftliste): dort zuerst zurück zu Fräsen. Manche Listen entstehen erst später
    // (Schriftauswahl füllt sich nach dem Zeigen): darum auch bei jedem Zeigen des Fensters nachsehen. In der
    // Schriftliste von Sonderzeichen ist die Tippsuche aus (unter Windows nahm sie Escape trotzdem, 5e98baa)
    {
        auto listen = std::make_shared<std::function<void(Gtk::Widget *)>>();
        bool sonderzeichen = d->get_type() == "Glyphs";
        *listen = [d, listen, sonderzeichen](Gtk::Widget *w) {
            if (auto tv = dynamic_cast<Gtk::TreeView *>(w); tv && !tv->get_data("hirch-escape")) {
                tv->set_data("hirch-escape", (void *)1);
                if (sonderzeichen) tv->set_enable_search(false);
                tv->signal_key_press_event().connect([d](GdkEventKey *e) {
                    if (e->keyval != GDK_KEY_Escape || !hirch_dialog_escape) return false;
                    return hirch_dialog_escape(*d);
                }, false);
            }
            if (auto k = dynamic_cast<Gtk::Container *>(w))
                for (auto *kind : k->get_children()) (*listen)(kind);
        };
        (*listen)(d);
        d->signal_map().connect([d, listen] { (*listen)(d); });
    }
    if (!hirch_dialog_escape) {
        hirch_dialog_escape = [](DialogBase &dlg) {
            if (!dlg.get_data("hirch-spalte-kopf")) return false;
            auto desk = dlg.getDesktop();
            if (!desk) return false;
            spalte_zeigen(desk, "Fraesen");
            return true;
        };
    }
    (void)desk;
}

/// Symbole der Leisten dieser Arbeitsfläche: das gezeigte Fenster ist gedrückt
static void spalte_knoepfe_stand(SPDesktop *desk);

/// Reiter ausblenden, Kopfzeilen setzen, beim Umschalten die Symbole nachführen (einmal je Spalte)
static void spalte_einrichten(SPDesktop *desk)
{
    auto nb = spalte_notebook(desk);
    if (!nb) return;
    auto *gn = nb->get_notebook();
    gn->set_show_tabs(false);
    for (auto *w : gn->get_children()) spalte_kopf(desk, dynamic_cast<DialogBase *>(w));
    if (gn->get_data("hirch-spalte")) return;
    gn->set_data("hirch-spalte", (void *)1);
    gn->signal_page_added().connect([desk, gn](Gtk::Widget *w, guint) {
        gn->set_show_tabs(false);
        spalte_kopf(desk, dynamic_cast<DialogBase *>(w));
    });
    gn->signal_switch_page().connect([desk](Gtk::Widget *, guint) {
        Glib::signal_idle().connect_once([desk] { spalte_knoepfe_stand(desk); });
    });
}

static void spalte_zeigen(SPDesktop *desk, std::string const &typ)
{
    auto c = desk ? desk->getContainer() : nullptr;
    if (!c) return;
    if (typ == "Hirch3D") {
        hirch_3d_fenster(desk);              // zeigt das gewählte Modell, sonst das erste
    } else if (!c->get_dialog(typ)) {
        c->new_dialog(typ);
    }
    spalte_einrichten(desk);
    auto d = c->get_dialog(typ);
    auto nb = spalte_notebook(desk);
    if (!d || !nb) return;
    auto *gn = nb->get_notebook();
    int n = gn->page_num(*d);
    if (n < 0 && spalte_nb_von(d)) {
        // in einer eigenen Spalte angedockt (z. B. aus einem älteren Fensterstand, Peter 09.11.: «zwei Spalten,
        // das geht so nicht»): in die eine Spalte rechts holen
        spalte_holen(nb, d);
        n = gn->page_num(*d);
    }
    if (n >= 0) gn->set_current_page(n);
    else d->blink();                     // schwebend
    spalte_knoepfe_stand(desk);
}

/// Seltene Fenster, die in einer eigenen Spalte angedockt sind, in die Spalte von Fräsen holen (beim Start)
static void spalte_sammeln(SPDesktop *desk)
{
    // ALLE angedockten Fenster dieser Arbeitsfläche, nicht nur die mit Symbol (Windows-Abnahme ea0907c: Verlauf,
    // Glyphen, Nachzeichnen, Suchen, Transformieren landeten in einer zweiten Spalte)
    auto c = desk ? desk->getContainer() : nullptr;
    auto nb = spalte_notebook(desk);
    if (!c || !nb) return;
    std::vector<DialogNotebook *> andere;
    std::function<void(Gtk::Widget *)> suchen = [&](Gtk::Widget *w) {
        if (auto n = dynamic_cast<DialogNotebook *>(w)) {
            if (n != nb) andere.push_back(n);
            return;                                  // in einem Notebook stecken keine weiteren
        }
        if (auto k = dynamic_cast<Gtk::Container *>(w))
            for (auto *kind : k->get_children()) suchen(kind);
    };
    suchen(c);
    for (auto *n : andere) {
        std::vector<DialogBase *> seiten;
        for (auto *w : n->get_notebook()->get_children())
            if (auto d = dynamic_cast<DialogBase *>(w)) seiten.push_back(d);
        for (auto *d : seiten) spalte_holen(nb, d);
        // leer (auch von Anfang an, Windows-Nacht a349d01: nur «Fraesen;» gespeichert → leere zweite Spalte)
        if (seiten.empty() && n->get_notebook()->get_n_pages() == 0) n->close_notebook_callback();
    }
}

static std::string spalte_aktiv(SPDesktop *desk)
{
    auto nb = spalte_notebook(desk);
    if (!nb) return "";
    auto *gn = nb->get_notebook();
    if (auto d = dynamic_cast<DialogBase *>(gn->get_nth_page(gn->get_current_page()))) return d->get_type().raw();
    return "";
}

void HirchFarbleiste::bauen()
{
    auto prefs = Inkscape::Preferences::get();
    auto aktion = [this](char const *name) {
        return [this, name] {
            if (!_desktop) return;
            if (auto w = _desktop->getInkscapeWindow()) w->activate_action(name);
        };
    };
    auto knopf = [](Gtk::Button &b, std::string const &symbol, std::string const &titel, std::string const &text) {
        b.set_relief(Gtk::RELIEF_NONE);
        b.set_size_request(34, 34);
        b.set_halign(Gtk::ALIGN_CENTER);
        auto bild = symbol_bild(symbol);
        bild->set_halign(Gtk::ALIGN_CENTER);
        bild->set_valign(Gtk::ALIGN_CENTER);
        b.add(*bild);
        hover_karte(b, "<b>" + markup_escape(titel) + "</b>\n" + markup_escape(text));
    };
    // Umschalter 2D | 3D (Peter, 22.10.: statt einer eigenen Zeile über der Zeichenfläche): zwei Knöpfe mit
    // Ziel der Aktion win.hirch-ansicht – aktiv = Zustand der Aktion; in 3D bleibt nur er sichtbar
    {
        auto css = Gtk::CssProvider::create();
        css->load_from_data("button { min-width: 34px; min-height: 26px; padding: 0; font-weight: bold; }"
                            "button:checked { background-image: none; background-color: #1c71d8; color: #ffffff; }");
        for (auto [k, ziel, text, tipp] :
             {std::tuple{&_t->k2d, "2d", "2D", TR("2D-Zeichnung – zeichnen und bearbeiten (Strg+3 schaltet um)")},
              std::tuple{&_t->k3d, "3d", "3D",
                         TR("3D-Ansicht – das fertige Teil mit allen Bearbeitungen beider Seiten ansehen; gezeichnet "
                            "wird in 2D (Strg+3 schaltet um)")}}) {
            k->set_label(text);
            k->set_relief(Gtk::RELIEF_NONE);
            k->set_focus_on_click(false);
            k->set_halign(Gtk::ALIGN_CENTER);
            k->get_style_context()->add_provider(css, GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
            k->set_tooltip_text(tipp);
            gtk_actionable_set_action_name(GTK_ACTIONABLE(k->gobj()), "win.hirch-ansicht");
            gtk_actionable_set_action_target_value(GTK_ACTIONABLE(k->gobj()), g_variant_new_string(ziel));
            _t->ansicht.add(*k);
        }
        _t->ansicht_trenner.set_margin_top(4);
        _t->ansicht_trenner.set_margin_start(6);
        _t->ansicht_trenner.set_margin_end(6);
        _t->ansicht.add(_t->ansicht_trenner);
        _t->ansicht.set_no_show_all(true);
        _t->rahmen.add(_t->ansicht);
    }
    _t->roll.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
    _t->roll.set_propagate_natural_height(true);
    _t->roll.set_propagate_natural_width(true);
    _t->roll.set_overlay_scrolling(true);
    _t->spalten.add(_t->zweite);
    _t->spalten.add(_t->rest);
    _t->zweite.set_no_show_all(true);
    _t->roll.add(_t->spalten);
    _t->rahmen.add(_t->roll);

    // Ansicht
    knopf(_t->bahnen, "bahnen", TR("Fräsbahnen zeigen"), TR("Mittellinie des Fräsers in der Farbe der Bearbeitung ein- oder ausblenden."));
    knopf(_t->andere, "andere", TR("Andere Seite blass zeigen"),
          TR("Zweiseitig und Vorrichtung: die Umrisse der anderen Seite blass über der Zeichnung."));
    _t->bahnen.signal_toggled().connect([this] {
        if (_t->setzt || !_desktop) return;
        bool an = Inkscape::Preferences::get()->getBool("/hirch/fraesbahnen", true);
        if (_t->bahnen.get_active() == an) return;
        if (auto w = _desktop->getInkscapeWindow()) w->activate_action("hirch-bahnen");
    });
    _t->andere.signal_toggled().connect([this] {
        if (_t->setzt) return;
        Inkscape::Preferences::get()->setBool("/hirch/andere_seite", _t->andere.get_active());
    });
    _t->rest.add(_t->bahnen);
    _t->rest.add(_t->andere);
    auto trenner = [this] {
        auto s = Gtk::make_managed<Gtk::Separator>(Gtk::ORIENTATION_HORIZONTAL);
        s->set_margin_top(4);
        s->set_margin_bottom(4);
        s->set_margin_start(6);
        s->set_margin_end(6);
        _t->rest.add(*s);
    };
    trenner();
    struct W { char const *sym, *titel, *text, *aktion; };
    for (auto w : {W{"verschachteln", TR("Vervielfältigen und verschachteln"),
                     TR("Die ausgewählten Teile in der gewünschten Anzahl eng auf die Platte legen. Nichts ausgewählt: "
                     "alle Teile der ersten Platte."), "hirch-verschachteln"},
                   W{"stege", TR("Haltestege setzen"), TR("Haltestege an Konturen und an zweiseitigen 3D-Modellen setzen oder verschieben."),
                     "hirch-stege"},
                   W{"spannmittel", TR("Spannmittel einzeichnen"), TR("Spanner, Schrauben und Anschläge einzeichnen – die Prüfung hält "
                     "den Fräser davon fern."), "hirch-spannmittel"},
                   W{"aufraeumen", TR("Zeichnung aufräumen"),
                     TR("Doppelte Linien entfernen, einzelne Stücke verbinden, fast geschlossene Pfade schliessen. "
                     "Auswahl – sonst die ganze Zeichnung."), "hirch-aufraeumen"},
                   W{"nullpunkt", TR("Auftrag einrichten"), TR("Material, Nullpunkt und Höhen einrichten."),
                     "hirch-material"}}) {
        auto b = Gtk::make_managed<Gtk::Button>();
        knopf(*b, w.sym, w.titel, w.text);
        b->signal_clicked().connect(aktion(w.aktion));
        _t->rest.add(*b);
    }
    for (auto *tr : {&_t->trenn_farben, &_t->trenn_spalte}) {
        tr->set_margin_top(4);
        tr->set_margin_bottom(4);
        tr->set_margin_start(6);
        tr->set_margin_end(6);
    }
    _t->grp_farben.add(_t->trenn_farben);
    _t->grp_farben.add(_t->farben);
    _t->rest.add(_t->grp_farben);
    // Seltene Fenster: öffnen sich in der rechten Spalte an Stelle von Fräsen (Peter 06.11.)
    {
        _t->grp_spalte.add(_t->trenn_spalte);
        for (auto const &f : spalten_fenster()) {
            auto b = Gtk::make_managed<Gtk::ToggleButton>();
            b->set_relief(Gtk::RELIEF_NONE);
            b->set_focus_on_click(false);
            b->set_size_request(30, 28);             // etwas kleiner: die Leiste muss auf kleine Bildschirme passen
            b->set_halign(Gtk::ALIGN_CENTER);
            auto bild = Gtk::make_managed<Gtk::Image>();
            bild->set_from_icon_name(f.icon, Gtk::ICON_SIZE_SMALL_TOOLBAR);
            b->add(*bild);
            hover_karte(*b, "<b>" + markup_escape(TR(f.titel)) + "</b>\n" +
                                markup_escape(TR("Klick: in der Spalte rechts statt Fräsen – nochmals: zurück")));
            std::string typ = f.typ;
            b->signal_toggled().connect([this, b, typ] {
                if (_t->setzt || !_desktop) return;
                bool war = spalte_aktiv(_desktop) == typ;
                spalte_zeigen(_desktop, war ? "Fraesen" : typ);
                (void)b;
            });
            _t->spalte_knoepfe.emplace_back(typ, b);
            _t->spalte.add(*b);
        }
        _t->grp_spalte.add(_t->spalte);
        _t->rest.add(_t->grp_spalte);
        // die Spalte ohne Reiter, sobald Fräsen angedockt ist
        // (Fräsen kommt beim Start erst später dazu: bis 60 s nachsehen; beim ersten Mal Fräsen zeigen)
        auto versuche = std::make_shared<int>(0);
        Glib::signal_timeout().connect([this, versuche] {
            if (!farbleisten().count(this) || !_desktop || ++*versuche > 120) return false;
            auto nb = spalte_notebook(_desktop);
            if (!nb) return true;
            bool neu = !nb->get_notebook()->get_data("hirch-spalte");
            spalte_sammeln(_desktop);
            // Inkscape legt beim Start manche Notebooks erst später an: nach 2 und 8 s noch einmal sammeln
            for (int ms : {2000, 8000}) {
                Glib::signal_timeout().connect_once([this] {
                    if (farbleisten().count(this) && _desktop) spalte_sammeln(_desktop);
                }, ms);
            }
            spalte_einrichten(_desktop);
            if (neu) spalte_zeigen(_desktop, "Fraesen");
            spalte_stand();
            return false;
        }, 500);
    }
    for (char const *p : {"/hirch/fraesbahnen", "/hirch/andere_seite"})
        _t->beobachter.push_back(prefs->createObserver(p, [this] { zustand(); }));
    zustand();
}

void HirchFarbleiste::zustand()
{
    spalte_stand();                          // Symbol «3D-Modell» nur mit einem Modell
    // Umschalter 2D | 3D: nur, wenn es die 3D-Ansicht gibt; in 3D nur er (gezeichnet wird in 2D)
    _t->ist_3d = _desktop && _desktop->getDesktopWidget() && hirch_ansicht_3d_ist(_desktop->getDesktopWidget());
    bool da = hirch_ansicht_3d_da();
    _t->ansicht.set_visible(da);
    if (da) {
        _t->k2d.show();
        _t->k3d.show();
        _t->ansicht_trenner.set_visible(!_t->ist_3d);
    }
    _t->rest.set_visible(!_t->ist_3d);
    _t->roll.set_visible(!_t->ist_3d);
    schein_zeichnen(_desktop, _t->schein);
    // In 3D fehlen Lineale und Bildlaufleisten – die Leiste soll trotzdem nicht springen: Abstand zum rechten
    // und oberen Fensterrand in 2D merken, in 3D mit den Rändern nachbilden
    _t->lage.disconnect();
    _t->lage = Glib::signal_timeout().connect([this] {
        auto grid = get_toplevel();      // Lücke zum Fensterrand: hängt nicht von der Fenstergrösse ab
        int x = 0, y = 0;
        if (!grid || !get_realized() || !translate_coordinates(*grid, 0, 0, x, y)) return false;
        int rechts = grid->get_allocated_width() - (x + get_allocated_width());   // Lücke rechts
        hoehe_begrenzen();
        if (!_t->ist_3d) {
            if (get_margin_top() == 12 && get_margin_end() == 12) {
                _t->x2d = rechts;
                _t->y2d = y;
            } else {
                set_margin_top(12);
                set_margin_end(12);
            }
        } else if (_t->x2d >= 0) {
            int mt = std::max(0, get_margin_top() + _t->y2d - y), me = std::max(0, get_margin_end() + _t->x2d - rechts);
            if (mt != get_margin_top()) set_margin_top(mt);
            if (me != get_margin_end()) set_margin_end(me);
        }
        return false;
    }, 80);
    auto prefs = Inkscape::Preferences::get();
    _t->setzt = true;
    _t->bahnen.set_active(prefs->getBool("/hirch/fraesbahnen", true));
    _t->andere.set_active(prefs->getBool("/hirch/andere_seite", true));
    _t->setzt = false;
    // «Andere Seite» nur, wenn es eine zweite Seite gibt (zweiseitig, Vorrichtung)
    // immer sichtbar, ohne zweite Seite nur grau: ein erst später eingeblendeter Knopf bekam
    // keine Klicks (Peter 22.10.: «Andere Seite blass» in der Leiste wirkte nie)
    _t->andere.set_sensitive(_t->zweite_seite);
}

/// Schein unter den Pfaden der Auswahl: jeder Pfad – auch in Gruppen und Texten – bekommt einen weichen blauen
/// Rand darunter (zwei breite, durchscheinende Striche auf der Ebene der Seiten, also unter der Zeichnung). Nur
/// Anzeige; ab 2000 Pfaden nur noch der Rahmen (Tempo). Einstellung «/hirch/auswahl_leuchten».
static void schein_zeichnen(SPDesktop *d, std::vector<CanvasItemPtr<CanvasItemBpath>> &schein)
{
    schein.clear();
    if (!d || !Inkscape::Preferences::get()->getBool("/hirch/auswahl_leuchten", true)) return;
    if (d->getDesktopWidget() && hirch_ansicht_3d_ist(d->getDesktopWidget())) return;
    auto sel = d->getSelection();
    if (!sel || sel->isEmpty()) return;
    Geom::PathVector pv;
    std::size_t n = 0;
    constexpr std::size_t HOECHSTENS = 2000;
    std::function<void(SPItem *)> sammeln = [&](SPItem *it) {
        if (!it || n > HOECHSTENS) return;
        if (auto g = cast<SPGroup>(it)) {
            for (auto &c : g->children) sammeln(cast<SPItem>(&c));
            return;
        }
        Geom::Affine m = it->i2doc_affine() * d->doc2dt();
        Geom::PathVector teil;
        if (auto sh = cast<SPShape>(it); sh && sh->curve()) teil = sh->curve()->get_pathvector();
        else if (auto t = cast<SPText>(it)) teil = t->getNormalizedBpath().get_pathvector();
        for (auto const &p : teil) {
            pv.push_back(p * m);
            if (++n > HOECHSTENS) return;
        }
    };
    for (auto it : sel->items()) sammeln(it);
    if (pv.empty() || n > HOECHSTENS) return;
    // B-13 (Peter 30.10.): hinter der Zeichnung der kräftige Schein wie bisher; darüber ein schwacher, damit er
    // auch über gefüllten Flächen und dem Graustufenbild eines 3D-Modells zu sehen ist – schwach, damit die Farbe
    // der Linie (= Fräsart) erkennbar bleibt
    for (auto [gruppe, breite, alpha] : {std::tuple{d->getCanvasPagesBg(), 10.0, 0x30u},
                                         std::tuple{d->getCanvasPagesBg(), 5.0, 0x55u},
                                         std::tuple{d->getCanvasSketch(), 10.0, 0x28u}}) {
        auto b = make_canvasitem<CanvasItemBpath>(gruppe, pv, false);
        b->set_stroke(0x3584e400u | alpha);
        b->set_stroke_width(breite);
        b->set_fill(0x0, SP_WIND_RULE_NONZERO);
        b->set_pickable(false);
        schein.push_back(std::move(b));
    }
}

void HirchFarbleiste::setDesktop(SPDesktop *d)
{
    _desktop = d;
    _t->sel_anders.disconnect();
    _t->sel_geaendert.disconnect();
    _t->schein.clear();
    if (d && d->getSelection()) {
        // beim Ziehen kommt «geändert» sehr oft: höchstens alle 40 ms neu
        auto bald = [this] {
            if (_t->schein_bald.connected()) return;
            _t->schein_bald = Glib::signal_timeout().connect([this] {
                schein_zeichnen(_desktop, _t->schein);
                return false;
            }, 40);
        };
        _t->sel_anders = d->getSelection()->connectChanged([bald](Inkscape::Selection *) { bald(); });
        _t->sel_geaendert = d->getSelection()->connectModified([bald](Inkscape::Selection *, unsigned) { bald(); });
    }
    if (!_t->schein_beobachter)
        _t->schein_beobachter = Inkscape::Preferences::get()->createObserver("/hirch/auswahl_leuchten", [this] {
            schein_zeichnen(_desktop, _t->schein);
        });
    Glib::signal_idle().connect_once([this] {
        if (farbleisten().count(this)) auffrischen();
    });
}

void HirchFarbleiste::alle_auffrischen(SPDesktop *d)
{
    for (auto f : farbleisten()) if (f->desktop() == d) f->auffrischen();
}

void HirchFarbleiste::hoehe_begrenzen()
{
    auto eltern = get_parent();
    if (!eltern) return;
    int frei = eltern->get_allocated_height() - get_margin_top() - 16 -
               (_t->ansicht.get_visible() ? _t->ansicht.get_allocated_height() + 2 : 0);
    // Höhen ohne Umbruch: was fest in der ersten Spalte bleibt, dazu Farben und seltene Fenster
    auto hoehe = [](Gtk::Widget &w) { int mn = 0, nat = 0; w.get_preferred_height(mn, nat); return nat; };
    auto in_rest = [this](Gtk::Widget &g) { return g.get_parent() == &_t->rest; };
    int h_fa = hoehe(_t->grp_farben), h_sp = hoehe(_t->grp_spalte);
    int h_fest = hoehe(_t->rest) - (in_rest(_t->grp_farben) ? h_fa + 3 : 0) - (in_rest(_t->grp_spalte) ? h_sp + 3 : 0);
    bool fa_rechts = h_fest + h_fa + h_sp + 6 <= frei || h_fest + h_fa + 3 <= frei;   // Farben bleiben, wenn möglich
    bool sp_rechts = h_fest + h_fa + h_sp + 6 <= frei;
    auto setzen = [this](Gtk::Box &g, bool rechts) {
        Gtk::Box &ziel = rechts ? _t->rest : _t->zweite;
        if (g.get_parent() == &ziel) return false;
        if (auto p = g.get_parent()) static_cast<Gtk::Container *>(p)->remove(g);
        ziel.add(g);
        return true;
    };
    bool anders = setzen(_t->grp_farben, fa_rechts);
    anders |= setzen(_t->grp_spalte, sp_rechts);
    if (anders) {
        // Reihenfolge in der zweiten Spalte: Farben vor den seltenen Fenstern
        if (_t->grp_farben.get_parent() == &_t->zweite) _t->zweite.reorder_child(_t->grp_farben, 0);
        // oben in einer Spalte kein Trennstrich
        _t->trenn_farben.set_visible(fa_rechts);
        _t->trenn_spalte.set_visible(sp_rechts || !fa_rechts);
        bool zwei = !fa_rechts || !sp_rechts;
        _t->zweite.set_visible(zwei);
        _t->grp_farben.show();
        _t->grp_spalte.show();
    }
    int max_h = std::max(120, frei);
    if (_t->roll.get_max_content_height() != max_h) _t->roll.set_max_content_height(max_h);
}

void HirchFarbleiste::spalte_stand()
{
    auto aktiv = _desktop ? spalte_aktiv(_desktop) : std::string();
    bool modell = _desktop && hirch_3d_hat_modell(_desktop->getDocument());
    _t->setzt = true;
    for (auto &[typ, b] : _t->spalte_knoepfe) {
        b->set_active(typ == aktiv);
        if (typ == "Hirch3D") {
            b->set_no_show_all(true);
            if (auto bild = b->get_child()) bild->show();      // no_show_all: das Bild selbst zeigen
            b->set_visible(modell || typ == aktiv);
        }
    }
    _t->setzt = false;
}

static void spalte_knoepfe_stand(SPDesktop *desk)
{
    for (auto f : farbleisten()) if (f->desktop() == desk) f->spalte_stand();
}

/// Eingabeform des Overlay-Fensters: nur die sichtbaren Teile (Kärtchen, Rechnen-Knopf, Leiste) nehmen Klicks
void HirchFarbleiste::eingabe_form()
{
    auto fenster = get_parent_window();          // das Fenster, das Gtk::Overlay für dieses Kind anlegt
    if (!fenster || !get_parent()) return;
    auto region = Cairo::Region::create();
    for (Gtk::Widget *w : std::initializer_list<Gtk::Widget *>{&_t->karte, &_t->rechnen, &_t->rahmen}) {
        if (!w->get_visible() || !w->get_child_visible()) continue;
        auto a = w->get_allocation();            // ohne eigenes Fenster: im selben Fenster wie die Leiste
        if (a.get_width() <= 1 || a.get_height() <= 1) continue;
        region->do_union(Cairo::RectangleInt{a.get_x(), a.get_y(), a.get_width(), a.get_height()});
    }
    fenster->input_shape_combine_region(region, 0, 0);
}

void HirchFarbleiste::rechenstand(SPDesktop *d, bool laeuft, std::string const &uhr)
{
    for (auto f : farbleisten()) {
        if (f->desktop() != d) continue;
        auto &t = *f->_t;
        if (laeuft) {
            // Uhr statt Kreisel: ein Kreisel über der Zeichenfläche zeichnet sie samt allen Fräsbahnen
            // dauernd neu – die Uhr springt nur einmal pro Sekunde
            static char const *const ZEIGER[] = {"◴", "◷", "◶", "◵"};
            int sek = 0;
            if (auto p = uhr.find(':'); p != std::string::npos) sek = std::atoi(uhr.c_str() + p + 1);
            auto text = std::string(ZEIGER[sek % 4]) + " " + uhr;
            if (t.uhr.get_text() != text) t.uhr.set_text(text);
            if (!t.rechnet) {
                // no_show_all: show_all() würde das Kärtchen überspringen – Teile einzeln zeigen
                t.uhr.show();
                t.karte_x.show_all();
                t.karte.show();
                f->set_opacity(1.0);            // gut sichtbar, solange gerechnet wird
            }
        } else if (t.rechnet) {
            t.karte.hide();
            f->set_opacity(0.62);
        }
        t.rechnet = laeuft;
    }
}

void HirchFarbleiste::wartet(SPDesktop *d, bool an)
{
    for (auto f : farbleisten()) {
        if (f->desktop() != d) continue;
        auto &t = *f->_t;
        if (an == t.wartet) continue;
        t.wartet = an;
        if (an) {
            t.rechnen.show_all();
            t.rechnen.show();
        } else {
            t.rechnen.hide();
        }
    }
}

void HirchFarbleiste::auffrischen()
{
    auto c = _desktop ? _desktop->getContainer() : nullptr;
    _t->zweite_seite = false;
    if (auto f = c ? dynamic_cast<HirchFraesen *>(c->get_dialog("Fraesen")) : nullptr) {
        f->farbleisteFuellen(&_t->farben);
        _t->zweite_seite = f->zweiteSeite();
    } else {
        // ohne Fräs-Panel: nur die Fräsarten
        UI::remove_all_children(_t->farben);
        auto desk = _desktop;
        for (auto const &a : ARTEN) {
            if (ohne_objekte(a.key)) continue;
            std::string hex = a.hex;
            auto b = Gtk::make_managed<Gtk::Button>();
            b->set_relief(Gtk::RELIEF_NONE);
            b->set_size_request(34, 30);
            b->set_halign(Gtk::ALIGN_CENTER);
            b->add(*farbfeld_leiste({}, hex));
            hover_karte(*b, "<b>" + markup_escape(TR(a.name)) + TR("</b>\nKlick: Auswahl so einfärben"));
            b->signal_clicked().connect([desk, hex] {
                if (!desk || desk->getSelection()->isEmpty()) return;
                SPCSSAttr *css = sp_repr_css_attr_new();
                sp_repr_css_set_property(css, "stroke", hex.c_str());
                sp_repr_css_set_property(css, "fill", "none");
                sp_desktop_set_style(desk, css);
                sp_repr_css_attr_unref(css);
                DocumentUndo::done(desk->getDocument(), TR("Fräsfarbe"), "");
            });
            _t->farben.add(*b);
        }
    }
    _t->farben.show_all();
    zustand();
}

// ---------------------------------------------------------------------------
// Reiter «Werkzeuge»: Inhalt kommt vom Fräs-Panel derselben Arbeitsfläche

} // namespace Inkscape::UI::Dialog

void hirch_spalte_zeigen(SPDesktop *desk, std::string const &typ)
{
    Inkscape::UI::Dialog::spalte_zeigen(desk, typ);
}
