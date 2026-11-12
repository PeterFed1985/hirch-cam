// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Ansicht «2D Zeichnung | 3D Ansicht» im Hauptfenster (Peter, 22.10.: Entwurf «Ansicht 2D | 3D»).
 *
 * Die 3D-Ansicht nimmt den Platz der Zeichenfläche ein; Panel rechts und Menüs bleiben. Das Widget selbst baut die
 * 3D-Sitzung und meldet es mit hirch_ansicht_3d_anmelden() an; bis dahin bleibt der Umschalter verborgen
 * (Test: HIRCH_TEST_ANSICHT3D=1 meldet einen Platzhalter an).
 * Aktion win.hirch-ansicht ('2d' | '3d'), Umschalten win.hirch-ansicht-wechseln (Strg+3).
 */
#include "ui/dialog/hirch-fraesen.h"
#include "ui/dialog/hirch-hilfen.h"

#include <cstdlib>
#include <map>

#include <giomm/simpleaction.h>
#include <glibmm/markup.h>
#include <gtkmm/box.h>
#include <gtkmm/eventbox.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/label.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/stylecontext.h>

#include "desktop.h"
#include "document.h"
#include "inkscape-application.h"
#include "inkscape-window.h"
#include "actions/actions-extra-data.h"
#include "page-manager.h"
#include "ui/widget/canvas-grid.h"
#include "ui/widget/desktop-widget.h"

namespace Inkscape::UI::Dialog {
bool hirch_hat_nullmarke(SPDocument *doc);   // hirch-aktionen.cpp

namespace {

HirchAnsicht3DFabrik &fabrik()
{
    static HirchAnsicht3DFabrik f;
    return f;
}

/// je Fenster: Zeile oben, Texte darin, das 3D-Widget (einmal gebaut) und ob 3D an ist
struct AnsichtZustand {
    Gtk::Box *zeile = nullptr;
    Gtk::Label *hinweis = nullptr, *nullpunkt = nullptr;
    Gtk::Label *feld = nullptr;                 // «Nullpunkt: …» in der Werkzeugleiste (Auswahl)
    Gtk::ToggleButton *k2d = nullptr, *k3d = nullptr;
    Gtk::Widget *widget = nullptr;
    bool an = false;
    sigc::connection seite;
};

std::map<SPDesktopWidget *, AnsichtZustand> &zustaende()
{
    static std::map<SPDesktopWidget *, AnsichtZustand> m;
    return m;
}

bool provisorisch(SPDesktopWidget *dtw)
{
    auto desktop = dtw ? dtw->get_desktop() : nullptr;
    return desktop && hirch_hat_nullmarke(desktop->getDocument());
}

/// «Nullpunkt: unten links · mm» aus den Einstellungen der Zeichnung (rund: die Mitte)
std::string nullpunkt_text(SPDesktopWidget *dtw)
{
    if (provisorisch(dtw)) return std::string(TR("Nullpunkt: provisorisch (oranges Kreuz)")) + " · " + einheit_name();
    std::string xy = "ul";
    bool rund = false;
    if (auto desktop = dtw->get_desktop()) {
        if (auto doc = desktop->getDocument(); doc && doc->getReprRoot()) {
            if (auto t = doc->getReprRoot()->attribute("hirch:einstellungen")) {
                try {
                    auto wert_ = bj::parse(t);                     // der Wert muss leben, solange o zeigt
                    if (auto o = wert_.if_object()) {
                        if (auto n = unter(*o, "nullpunkt")) xy = str(*n, "xy", "ul");
                        rund = rund_einst(*o) != nullptr;
                    }
                } catch (...) {
                }
            }
        }
    }
    return TR("Nullpunkt: ") + (rund ? TR("Mitte (rund)") : nullpunkt_name(xy)) + " · " + einheit_name();
}

/// für die Werkzeugleiste: ohne « · mm» (die Einheit steht gleich daneben)
std::string nullpunkt_feld_text(SPDesktopWidget *dtw)
{
    auto t = nullpunkt_text(dtw);
    auto ende = " · " + einheit_name();
    if (auto p = t.rfind(ende); p != std::string::npos && p + ende.size() == t.size()) t.erase(p);
    return t;
}

/// Feld in der Werkzeugleiste: provisorischer Nullpunkt orange und fett, damit man ihn nicht übersieht
void feld_setzen(Gtk::Label *l, SPDesktopWidget *dtw)
{
    if (!l) return;
    auto t = nullpunkt_feld_text(dtw);
    if (provisorisch(dtw)) {
        l->set_markup("<span foreground='#c64600' weight='bold'>" + Glib::Markup::escape_text(t) + "</span>");
        l->set_tooltip_text(TR("X und Y zählen ab dem orangen Kreuz – dort an der Maschine X0 Y0 antasten. "
                               "Rechtsklick auf das Kreuz: Position eingeben oder entfernen."));
    } else {
        l->set_text(t);
        l->set_tooltip_text(TR("X und Y zählen ab diesem Punkt des Werkstücks (Auftrag einrichten › Nullpunkt)"));
    }
}

/// Zeichenwerkzeuge links: in 3D nur «Auswählen» (gezeichnet wird in 2D)
void werkzeuge_grau(Gtk::Widget *w, bool grau)
{
    if (!w) return;
    if (GTK_IS_ACTIONABLE(w->gobj())) {
        auto name = gtk_actionable_get_action_name(GTK_ACTIONABLE(w->gobj()));
        if (name && std::string(name) == "win.tool-switch") {
            auto ziel = gtk_actionable_get_action_target_value(GTK_ACTIONABLE(w->gobj()));
            bool auswahl = ziel && g_variant_is_of_type(ziel, G_VARIANT_TYPE_STRING) &&
                           std::string(g_variant_get_string(ziel, nullptr)) == "Select";
            w->set_sensitive(!grau || auswahl);
            return;
        }
    }
    if (auto c = dynamic_cast<Gtk::Container *>(w)) {
        for (auto k : c->get_children()) werkzeuge_grau(k, grau);
    }
}

} // namespace

void hirch_ansicht_3d_anmelden(HirchAnsicht3DFabrik f)
{
    fabrik() = std::move(f);
    for (auto &[dtw, z] : zustaende()) {
        if (z.zeile) z.zeile->set_visible(false);       // Umschalter steht in der schwebenden Leiste
        auto win = dtw->get_desktop() ? dtw->get_desktop()->getInkscapeWindow() : nullptr;
        for (char const *n : {"hirch-ansicht", "hirch-ansicht-wechseln"}) {
            if (!win) continue;
            if (auto a = Glib::RefPtr<Gio::SimpleAction>::cast_dynamic(win->lookup_action(n)))
                a->set_enabled(hirch_ansicht_3d_da());
        }
    }
}

static HirchAnsicht3DSprung &sprung_fn()
{
    static HirchAnsicht3DSprung f;
    return f;
}

void hirch_ansicht_3d_sprung_anmelden(HirchAnsicht3DSprung sprung) { sprung_fn() = std::move(sprung); }

void hirch_ansicht_3d_springen(SPDesktop *desktop, std::string const &schluessel)
{
    if (sprung_fn() && desktop) sprung_fn()(desktop, schluessel);
}

bool hirch_ansicht_3d_da()
{
    return static_cast<bool>(fabrik()) || std::getenv("HIRCH_TEST_ANSICHT3D");
}

static Gtk::Widget *bauen(SPDesktop *desktop)
{
    if (fabrik()) {
        // deckend: das Widget liegt über der Zeichenfläche (Overlay) – ohne eigenen Hintergrund schiene die
        // Zeichnung durch
        auto w = fabrik()(desktop);
        if (!w) return nullptr;
        auto grund = Gtk::make_managed<Gtk::EventBox>();
        grund->set_name("HirchAnsicht3D");
        grund->set_visible_window(true);
        grund->add(*w);
        auto css = Gtk::CssProvider::create();
        css->load_from_data("#HirchAnsicht3D { background-color: @theme_bg_color; }");
        grund->get_style_context()->add_provider(css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        return grund;
    }
    // Test-Platzhalter (HIRCH_TEST_ANSICHT3D): eine dunkle Fläche mit Beschriftung
    auto l = Gtk::make_managed<Gtk::Label>(TR("3D-Ansicht (Platzhalter für den Test)"));
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL);
    box->set_name("HirchAnsicht3DTest");
    box->pack_start(*l, true, true);
    auto css = Gtk::CssProvider::create();
    css->load_from_data("#HirchAnsicht3DTest { background: #5e6470; color: #f6f5f4; }");
    box->get_style_context()->add_provider(css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    return box;
}

Gtk::Widget *hirch_ansicht_zeile(SPDesktopWidget *dtw)
{
    auto &z = zustaende()[dtw];
    auto zeile = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 14);
    zeile->set_name("HirchAnsichtZeile");
    zeile->set_margin_start(8);
    zeile->set_margin_end(10);
    zeile->set_margin_top(4);
    zeile->set_margin_bottom(4);
    // Umschalter: zwei verbundene Knöpfe (wie «Ansehen von oben | unten» der 3D-Ansicht)
    auto gruppe = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
    gruppe->get_style_context()->add_class("linked");
    // Umschaltknöpfe mit Ziel der Aktion (aktiv = Zustand der Aktion); keine Radio-Gruppe – die schaltet beim
    // Abgleich hin und her
    auto k2d = Gtk::make_managed<Gtk::ToggleButton>(TR("2D Zeichnung"));
    auto k3d = Gtk::make_managed<Gtk::ToggleButton>(TR("3D Ansicht"));
    for (auto [k, ziel] : {std::pair{k2d, "2d"}, std::pair{k3d, "3d"}}) {
        k->set_focus_on_click(false);
        gtk_actionable_set_action_name(GTK_ACTIONABLE(k->gobj()), "win.hirch-ansicht");
        gtk_actionable_set_action_target_value(GTK_ACTIONABLE(k->gobj()), g_variant_new_string(ziel));
        gruppe->pack_start(*k, false, false);
    }
    k2d->set_tooltip_text(TR("Zeichnen und bearbeiten (Strg+3 schaltet um)"));
    k3d->set_tooltip_text(TR("Das fertige Teil mit allen Bearbeitungen beider Seiten ansehen (Strg+3 schaltet um)"));
    zeile->pack_start(*gruppe, false, false);
    auto hinweis = Gtk::make_managed<Gtk::Label>(TR("Zeichnen und bearbeiten"));
    hinweis->get_style_context()->add_class("dim-label");
    zeile->pack_start(*hinweis, false, false);
    auto np = Gtk::make_managed<Gtk::Label>("");
    np->get_style_context()->add_class("dim-label");
    zeile->pack_end(*np, false, false);
    zeile->show_all();
    zeile->set_no_show_all(true);
    // Peter, 22.10.: die Zeile kostet eine ganze Bildschirmzeile – der Umschalter steht jetzt oben in der
    // schwebenden Leiste (hirch-leiste.cpp), der Nullpunkt neben der Einheit (hirch_nullpunkt_feld)
    zeile->set_visible(false);
    z.zeile = zeile;
    z.hinweis = hinweis;
    z.nullpunkt = np;
    z.k2d = k2d;
    z.k3d = k3d;
    zeile->signal_unrealize().connect([dtw] {
        zustaende()[dtw].seite.disconnect();
    });
    // Fenster zu: Zustand weg (der Zeiger dtw könnte später für ein neues Fenster wiederkommen)
    zeile->add_destroy_notify_callback(dtw, [](void *d) -> void * {
        auto &m = zustaende();
        if (auto it = m.find(static_cast<SPDesktopWidget *>(d)); it != m.end()) {
            it->second.seite.disconnect();
            m.erase(it);
        }
        return nullptr;
    });
    return zeile;
}

/// Texte der Zeile auffrischen (Nullpunkt kann sich mit den Einstellungen ändern)
static void zeile_auffrischen(SPDesktopWidget *dtw)
{
    auto it = zustaende().find(dtw);
    if (it == zustaende().end()) return;
    auto &z = it->second;
    if (z.nullpunkt) z.nullpunkt->set_text(nullpunkt_text(dtw));
    feld_setzen(z.feld, dtw);
    if (z.hinweis) z.hinweis->set_text(z.an ? TR("Ansehen und wählen – gezeichnet wird in 2D")
                                            : TR("Zeichnen und bearbeiten"));
    // Nullpunkt folgt der Analyse (Seite gewählt / Nullpunkt geändert)
    if (!z.seite.connected()) {
        if (auto desktop = dtw->get_desktop()) {
            z.seite = desktop->getDocument()->getPageManager().connectPageSelected([dtw](SPPage *) {
                auto it2 = zustaende().find(dtw);
                if (it2 == zustaende().end()) return;
                if (it2->second.nullpunkt) it2->second.nullpunkt->set_text(nullpunkt_text(dtw));
                feld_setzen(it2->second.feld, dtw);
            });
        }
    }
}

Gtk::Widget *hirch_nullpunkt_feld(SPDesktop *desktop)
{
    auto dtw = desktop ? desktop->getDesktopWidget() : nullptr;
    if (!dtw) return nullptr;
    auto l = Gtk::make_managed<Gtk::Label>("");
    l->get_style_context()->add_class("dim-label");
    l->set_tooltip_text(TR("X und Y zählen ab diesem Punkt des Werkstücks (Auftrag einrichten › Nullpunkt)"));
    l->show();
    auto &z = zustaende()[dtw];
    z.feld = l;
    l->add_destroy_notify_callback(dtw, [](void *d) -> void * {
        auto it = zustaende().find(static_cast<SPDesktopWidget *>(d));
        if (it != zustaende().end()) it->second.feld = nullptr;
        return nullptr;
    });
    feld_setzen(l, dtw);
    Glib::signal_idle().connect_once([dtw] { zeile_auffrischen(dtw); });
    return l;
}

/// «Nullpunkt: …» neu beschriften – nach jeder Analyse (Peter 22.10.: nach «Einrichten › Mitte» stand
/// noch «unten links» da, Lineale und X/Y zählten aber schon ab der Mitte)
void hirch_nullpunkt_feld_auffrischen(SPDesktop *desktop)
{
    if (auto dtw = desktop ? desktop->getDesktopWidget() : nullptr) zeile_auffrischen(dtw);
}

bool hirch_ansicht_3d_ist(SPDesktopWidget *dtw)
{
    auto it = zustaende().find(dtw);
    return it != zustaende().end() && it->second.an;
}

void hirch_ansicht_3d_setzen(SPDesktopWidget *dtw, bool an)
{
    if (!dtw) return;
    auto &z = zustaende()[dtw];
    if (an && !hirch_ansicht_3d_da()) an = false;
    auto cg = dtw->get_canvas_grid();
    if (an && !z.widget && cg) {
        z.widget = bauen(dtw->get_desktop());
        if (z.widget) {
            z.widget->set_hexpand(true);
            z.widget->set_vexpand(true);
            z.widget->set_halign(Gtk::ALIGN_FILL);
            z.widget->set_valign(Gtk::ALIGN_FILL);
        }
    }
    z.an = an && z.widget;
    if (cg) cg->hirch_3d(z.widget, z.an);
    werkzeuge_grau(dtw->get_tool_toolbox(), z.an);
    if (z.an) {
        if (auto win = dtw->get_desktop() ? dtw->get_desktop()->getInkscapeWindow() : nullptr)
            win->activate_action("tool-switch", Glib::Variant<Glib::ustring>::create("Select"));
    } else if (auto desktop = dtw->get_desktop()) {
        if (auto canvas = cg ? cg->GetCanvas() : nullptr) canvas->grab_focus();
        (void)desktop;
    }
    zeile_auffrischen(dtw);
    // schwebende Leiste: in 3D nur der Umschalter
    if (auto desktop = dtw->get_desktop()) HirchFarbleiste::alle_auffrischen(desktop);
}

void hirch_ansicht_aktionen(InkscapeWindow *win)
{
    win->add_action_radio_string("hirch-ansicht", [win](Glib::ustring const &v) {
        auto dtw = win->get_desktop_widget();
        hirch_ansicht_3d_setzen(dtw, v == "3d");
        if (auto a = Glib::RefPtr<Gio::SimpleAction>::cast_dynamic(win->lookup_action("hirch-ansicht")))
            a->change_state(Glib::ustring(hirch_ansicht_3d_ist(dtw) ? "3d" : "2d"));
    }, "2d");
    win->add_action("hirch-ansicht-wechseln", [win] {
        auto dtw = win->get_desktop_widget();
        bool neu = !hirch_ansicht_3d_ist(dtw);
        win->activate_action("hirch-ansicht", Glib::Variant<Glib::ustring>::create(neu ? "3d" : "2d"));
    });
    // Strg+3: direkt setzen – die Tastenkürzel-Datei wird gelesen, bevor es Fenster-Aktionen gibt
    if (auto app = InkscapeApplication::instance()) {
        app->get_action_extra_data().add_data({
            {"win.hirch-ansicht-wechseln", Glib::Markup::escape_text(TR("2D Zeichnung | 3D Ansicht")), "Hirch CAM",
             Glib::Markup::escape_text(TR("Zwischen Zeichnung und 3D-Ansicht umschalten"))},
        });
        if (auto g = app->gtk_app(); g && g->get_accels_for_action("win.hirch-ansicht-wechseln").empty())
            g->set_accels_for_action("win.hirch-ansicht-wechseln", {"<Primary>3", "<Primary>KP_3"});
    }
    if (!hirch_ansicht_3d_da()) {
        for (char const *n : {"hirch-ansicht", "hirch-ansicht-wechseln"}) {
            if (auto a = Glib::RefPtr<Gio::SimpleAction>::cast_dynamic(win->lookup_action(n))) a->set_enabled(false);
        }
    }
    if (auto dtw = win->get_desktop_widget()) zeile_auffrischen(dtw);
}

} // namespace Inkscape::UI::Dialog
