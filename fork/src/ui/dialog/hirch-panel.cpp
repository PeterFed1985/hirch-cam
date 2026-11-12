// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Panel: Aufbau, Dokument, Rechendienst, Rechenanzeige (Teile: hirch-panel-*.cpp)
 */
#include "ui/dialog/hirch-intern.h"
#include "path/path-boolop.h"
#include "ui/tools/tool-base.h"
#include "ui/widget/canvas.h"
#include "helper/geom-pathstroke.h"
#include "ui/dialog/hirch-3d.h"
#include "inkscape-window.h"

#include <cstdio>
#include <glib/gstdio.h>

namespace Inkscape::UI::Dialog {

// Plätze, die werkzeug_platz eben angelegt hat und die noch nicht in der Zeichnung stehen (Befund 22.10.):
// kommt in der Zwischenzeit eine Analyse zurück, ersetzt sie «einst» – ohne diese Plätze. Die Bearbeitung
// im offenen Dialog zeigt dann auf einen Platz, den es nicht mehr gibt («Werkzeug D ist nicht eingerichtet»).
std::map<void const *, std::map<std::string, bj::object>> &offene_plaetze()
{
    static std::map<void const *, std::map<std::string, bj::object>> m;
    return m;
}

// Werkzeugfeld, dessen Platz fehlt: orange umrandet, solange der fehlende Eintrag gewählt ist.
// fehlt_id leer: Markierung weg. (Hier per Widget, die CSS-Datei steht in hirch-intern.h.)
void hirch_werkzeug_fehlt_markieren(Gtk::ComboBoxText &c, std::string const &fehlt_id)
{
    auto o = G_OBJECT(c.gobj());
    g_object_set_data_full(o, "hirch-fehlt", fehlt_id.empty() ? nullptr : g_strdup(fehlt_id.c_str()), g_free);
    auto setzen = [](Gtk::ComboBoxText &k) {
        auto oo = G_OBJECT(k.gobj());
        auto f = static_cast<char const *>(g_object_get_data(oo, "hirch-fehlt"));
        bool an = f && k.get_active_id().raw() == f;
        auto *prov = static_cast<GtkCssProvider *>(g_object_get_data(oo, "hirch-fehlt-css"));
        if (!prov) {
            prov = gtk_css_provider_new();
            gtk_css_provider_load_from_data(prov,
                "button { border: 2px solid #e8a33a; background-image: none; background-color: alpha(#e8a33a, 0.18); }",
                -1, nullptr);
            g_object_set_data_full(oo, "hirch-fehlt-css", prov, g_object_unref);
        }
        struct Z { GtkCssProvider *p; bool an; };
        Z z{prov, an};
        // der Knopf steckt in einer inneren Box des Felds: rekursiv suchen
        static void (*suchen)(GtkWidget *, gpointer) = [](GtkWidget *w, gpointer d) {
            auto z_ = static_cast<Z *>(d);
            if (GTK_IS_TOGGLE_BUTTON(w)) {
                auto ctx = gtk_widget_get_style_context(w);
                gtk_style_context_remove_provider(ctx, GTK_STYLE_PROVIDER(z_->p));
                if (z_->an) gtk_style_context_add_provider(ctx, GTK_STYLE_PROVIDER(z_->p), GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
            } else if (GTK_IS_CONTAINER(w)) {
                gtk_container_forall(GTK_CONTAINER(w), suchen, d);
            }
        };
        gtk_container_forall(GTK_CONTAINER(k.gobj()), suchen, &z);
    };
    if (!g_object_get_data(o, "hirch-fehlt-verbunden")) {
        g_object_set_data(o, "hirch-fehlt-verbunden", GINT_TO_POINTER(1));
        c.signal_changed().connect([&c, setzen] { setzen(c); });
    }
    setzen(c);
}

/// Werkzeug ist Pflicht (Befund 22.10.): «Übernehmen» (RESPONSE_OK) gesperrt, solange im Feld ein
/// fehlender Platz steht – sonst wäre unklar, welcher Fräser gemeint ist.
void hirch_werkzeug_pflicht(Gtk::Dialog &dlg, Gtk::ComboBoxText &c)
{
    auto pruefen = [&dlg, &c] {
        auto f = static_cast<char const *>(g_object_get_data(G_OBJECT(c.gobj()), "hirch-fehlt"));
        bool fehlt = f && c.get_active_id().raw() == f;
        if (auto ok = dlg.get_widget_for_response(Gtk::RESPONSE_OK)) ok->set_sensitive(!fehlt);
        return fehlt;
    };
    c.signal_changed().connect([pruefen] { pruefen(); });
    dlg.signal_response().connect([&dlg, pruefen](int r) {
        if (r == Gtk::RESPONSE_OK && pruefen()) g_signal_stop_emission_by_name(dlg.gobj(), "response");
    }, false);
    pruefen();
}


void HirchFraesen::Impl::darstellung_zeigen()
{
    if (dunkel_switch.get_active() != design_dunkel()) dunkel_switch.set_active(design_dunkel());
}

void HirchFraesen::Impl::leeren(std::string const &pfad, bool auch_ordner)
{
    if (GDir *dir = g_dir_open(pfad.c_str(), 0, nullptr)) {
        while (char const *n = g_dir_read_name(dir)) {
            auto p = Glib::build_filename(pfad, n);
            if (g_file_test(p.c_str(), G_FILE_TEST_IS_DIR)) {
                leeren(p, true);
            } else {
                ::g_remove(p.c_str()); // UTF-8-Pfad auch unter Windows
            }
        }
        g_dir_close(dir);
    }
    if (auch_ordner) g_rmdir(pfad.c_str());
}

void HirchFraesen::Impl::ordner_leeren(bool auch_ordner)
{ leeren(ordner, auch_ordner); }

void HirchFraesen::Impl::ort_zeigen(double x, double y, int seite)
{
    auto desk = panel.getDesktop();
    if (!desk) return;
    desktop_beobachten(desk);
    double hoehe = 0;
    if (auto m = unter(analyse, "material")) hoehe = num(*m, "hoehe", 0);
    double const px = MM_PX;
    double sx = 0, sy = 0;
    seite_ecke(seite, sx, sy);
    auto p = Geom::Point((sx + x) * px, (sy + hoehe - y) * px) * desk->doc2dt();
    desk->zoom_absolute(p, std::max(desk->current_zoom(), 4.0), false);
    Geom::PathVector pv;
    double r = 3.0 * px * desk->doc2dt().descrim();
    pv.push_back(Geom::Path(Geom::Circle(p, r)));
    marke = make_canvasitem<CanvasItemBpath>(desk->getCanvasSketch(), pv, false);
    marke->set_stroke(0xd9342bff);
    marke->set_stroke_width(2.5);
    marke->set_fill(0x0, SP_WIND_RULE_NONZERO);
    marke->set_pickable(false);
    marke_weg.disconnect();
    marke_weg = Glib::signal_timeout().connect([this] { marke.reset(); return false; }, 4000);
}

void HirchFraesen::Impl::umspannen_einschalten()
{
    bj::object uo;
    if (auto u = unter(einst, "umspannen")) uo = *u;
    uo["an"] = true;
    if (num(uo, "abstand", 0) < 10) uo["abstand"] = 10.0;   // wie in den Einstellungen
    einst["umspannen"] = uo;
    schreiben(TR("Umspannen"));
    rechnen_jetzt();
}

Gtk::Widget *HirchFraesen::Impl::befund_widget(bj::object const &o, Gtk::Label *l)
{
    // Fehler, die sich mit einem anderen Werkzeug lösen: gleich ein Knopf dazu (Peter, 21.10.: Passlöcher)
    if (str(o, "aktion") == "werkzeug" && !str(o, "bearbeitung").empty()) {
        bj::object ohne = o;
        ohne.erase("aktion");
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);
        box->add(*befund_widget(ohne, l));
        auto knopf = Gtk::make_managed<Gtk::Button>(TR("Werkzeug wählen …"));
        knopf->set_halign(Gtk::ALIGN_START);
        std::string bid = str(o, "bearbeitung");
        knopf->signal_clicked().connect([this, bid] {
            Glib::signal_idle().connect_once([this, bid] {
                if (bid == "pass") {             // Passlöcher: im Dialog «Auftrag einrichten»
                    material_dialog();
                    return;
                }
                if (auto bea = liste_von(einst, "bearbeitungen")) {
                    for (std::size_t i = 0; i < bea->size(); ++i) {
                        if (auto b = (*bea)[i].if_object(); b && str(*b, "id") == bid) {
                            bearbeitung_dialog((int)i);
                            return;
                        }
                    }
                }
            });
        });
        box->add(*knopf);
        return box;
    }
    // Spannmittel im Fräsweg, Umspannen noch aus: gleich einschalten (Peter 09.11.: «Dass man Umspannen aktivieren kann»)
    if (str(o, "aktion") == "umspannen") {
        bj::object ohne = o;
        ohne.erase("aktion");
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);
        box->add(*befund_widget(ohne, l));
        auto knopf = Gtk::make_managed<Gtk::Button>(TR("Umspannen einschalten"));
        knopf->set_halign(Gtk::ALIGN_START);
        klasse(*knopf, "suggested-action");
        knopf->set_tooltip_text(TR("Gleiche Einstellung wie unter «Auftrag einrichten › Umspannen»; dort lassen sich "
                                   "Abstand und Höhe ändern oder Umspannen wieder ausschalten."));
        knopf->signal_clicked().connect([this] {
            Glib::signal_idle().connect_once([this] { umspannen_einschalten(); });
        });
        box->add(*knopf);
        auto info = label(TR("Spannung 1 fräst alles ausser einem Rand um die Spannmittel. Nach dem Umspannen fräst "
                             "Spannung 2 den Rest."), "hirch-dim");
        klasse(*info, "hirch-klein");
        info->set_line_wrap(true);
        info->set_xalign(0);
        box->add(*info);
        return box;
    }
    // Restmaterial nach dem 3D-Schruppen: gleich ein zweites Schruppen mit dem kleineren Fräser (Peter 25.10.)
    if (str(o, "aktion") == "restmaterial" && !str(o, "bearbeitung").empty()) {
        bj::object ohne = o;
        ohne.erase("aktion");
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);
        box->add(*befund_widget(ohne, l));
        double d = num(o, "durchmesser", 3);
        auto knopf = Gtk::make_managed<Gtk::Button>(TRF("Restmaterial mit Ø {} anlegen", {zahl(d)}));
        knopf->set_halign(Gtk::ALIGN_START);
        klasse(*knopf, "suggested-action");
        std::string bid = str(o, "bearbeitung");
        knopf->signal_clicked().connect([this, bid, d] {
            Glib::signal_idle().connect_once([this, bid, d] { restmaterial_anlegen(bid, d); });
        });
        box->add(*knopf);
        return box;
    }
    auto ort = liste_von(o, "ort");
    bool hat_ort = ort && ort->size() == 2;
    std::vector<std::string> ids;
    if (auto ob = liste_von(o, "objekte")) {
        for (auto const &v : *ob) if (v.is_string()) ids.emplace_back(v.get_string());
    }
    if (!hat_ort && ids.empty()) return l;
    double x = hat_ort ? (*ort)[0].to_number<double>() : 0, y = hat_ort ? (*ort)[1].to_number<double>() : 0;
    int seite = (int)num(o, "seite", 1);
    auto btn = Gtk::make_managed<Gtk::Button>();
    btn->set_relief(Gtk::RELIEF_NONE);
    klasse(*btn, "hirch-befund");
    btn->add(*l);
    btn->set_tooltip_text(ids.empty() ? TR("Klick: Stelle in der Zeichnung zeigen")
                                      : TR("Klick: betroffene Objekte in der Zeichnung auswählen und zeigen"));
    btn->signal_clicked().connect([this, x, y, seite, hat_ort, ids] {
        if (zweiseitig() && seite != seite_aktiv) seite_waehlen(seite, false);
        bool gewaehlt = false;
        if (!ids.empty()) {
            auto d = doc();
            auto sel = panel.getSelection();
            std::vector<SPItem *> items;
            if (d && sel) {
                for (auto const &i : ids) {
                    if (auto item = cast<SPItem>(d->getObjectById(i))) items.push_back(item);
                }
                if (!items.empty()) {
                    sel->setList(items);
                    gewaehlt = true;
                }
            }
        }
        if (hat_ort) ort_zeigen(x, y, seite);
        else if (gewaehlt) {
            if (auto desk = panel.getDesktop()) desk->zoom_selection();
        }
    });
    return btn;
}

std::set<std::string> HirchFraesen::Impl::quittiert_lesen()
{
    std::set<std::string> aus;
    auto d = doc();
    if (!d) return aus;
    auto a = d->getReprRoot()->attribute(QUITTIERT);
    if (!a) return aus;
    boost::system::error_code ec;
    auto v = bj::parse(a, ec);
    if (!ec && v.is_array())
        for (auto const &x : v.as_array()) if (x.is_string()) aus.insert(std::string(x.get_string()));
    return aus;
}

void HirchFraesen::Impl::quittieren(std::string const &text, bool an)
{
    auto d = doc();
    if (!d) return;
    auto q = quittiert_lesen();
    if (an) q.insert(text); else q.erase(text);
    bj::array arr;
    for (auto const &t : q) arr.emplace_back(t);
    d->getReprRoot()->setAttribute(QUITTIERT, q.empty() ? nullptr : bj::serialize(arr).c_str());
    DocumentUndo::done(d, an ? TR("Hinweis abhaken") : TR("Hinweis wieder öffnen"), "");
    neu_zeigen.disconnect();
    neu_zeigen = Glib::signal_idle().connect([this] { anzeigen(); return false; });
}

Gtk::Widget *HirchFraesen::Impl::hinweis_widget(std::string const &text, bj::object const *o, std::set<std::string> const &q,
                                int max_zeichen)
{
    bool erledigt = q.count(text) > 0;
    auto zeile = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 4);
    auto haken = Gtk::make_managed<Gtk::CheckButton>();
    haken->set_active(erledigt);
    haken->set_valign(Gtk::ALIGN_START);
    haken->set_tooltip_text(erledigt ? TR("Gelesen – Häkchen entfernen, um den Hinweis wieder offen zu zeigen")
                                     : TR("Abhaken: gelesen und verstanden"));
    zeile->add(*haken);
    auto l = label((erledigt ? "✓ " : "⚠ ") + text, erledigt ? "hirch-ok" : "hirch-warn");
    haken->signal_toggled().connect([this, text, haken, l] {
        bool an = haken->get_active();
        // sofort umfärben (auch im offenen Dialog), dann merken
        l->set_text((an ? "✓ " : "⚠ ") + text);
        auto sc = l->get_style_context();
        sc->remove_class(an ? "hirch-warn" : "hirch-ok");
        sc->add_class(an ? "hirch-ok" : "hirch-warn");
        quittieren(text, an);
    });
    l->set_line_wrap(true);
    if (max_zeichen) l->set_max_width_chars(max_zeichen);
    klasse(*l, "hirch-klein");
    auto inhalt = o ? befund_widget(*o, l) : static_cast<Gtk::Widget *>(l);
    inhalt->set_hexpand(true);
    zeile->add(*inhalt);
    return zeile;
}

Gtk::Window *HirchFraesen::Impl::fenster()
{ return dynamic_cast<Gtk::Window *>(panel.get_toplevel()); }

void HirchFraesen::Impl::meldung(std::string const &titel, std::string const &text, Gtk::MessageType typ)
{
    Gtk::MessageDialog m(titel, false, typ, Gtk::BUTTONS_NONE, true);
    m.set_title("Hirch CAM");
    if (auto f = fenster()) m.set_transient_for(*f);
    m.set_secondary_text(text);
    m.add_button(TR("Schliessen"), Gtk::RESPONSE_CLOSE);   // GESTALTUNG: kein «OK»
    m.run();
}

void hirch_nullpunkt_feld_auffrischen(SPDesktop *desktop);   // hirch-ansicht.cpp

bool HirchFraesen::Impl::geister_an()
{ return Inkscape::Preferences::get()->getBool("/hirch/andere_seite", true); }

void HirchFraesen::Impl::bauen()
{
    klasse(inhalt, "hirch-panel");
    scroll.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
    scroll.set_vexpand(true);
    scroll.add(inhalt);
    // Allgemeine Darstellung steht im eigenen Reiter «Ansicht» (HirchAnsicht) daneben
    UI::pack_start(panel, seite_fraesen, true, true);
    rechen_balken_bauen();
    // die Leiste «Neu rechnen» steht unter der Liste der Bearbeitungen, über der Ausgabe mit der
    // Zeitleiste (Peter, 19.10.) – eingefügt nach dem Abschnitt «Ablauf»
    seite_fraesen.pack_start(scroll, true, true);
    // Karte, die beim Ziehen an der Maus hängt: eigenes kleines Fenster (wie ein Drag-Symbol)
    schwebe_fenster.set_type_hint(Gdk::WINDOW_TYPE_HINT_DND);
    schwebe_fenster.set_app_paintable(true);
    if (auto sc = schwebe_fenster.get_screen()) {
        if (auto v = sc->get_rgba_visual()) gtk_widget_set_visual(GTK_WIDGET(schwebe_fenster.gobj()), v->gobj());
    }
    schwebe_fenster.add(schwebe);
    schwebe.show();
    schwebe.signal_draw().connect([this](Cairo::RefPtr<Cairo::Context> const &cr) { return schwebe_malen(cr); });
    geister_beobachter = Inkscape::Preferences::get()->createObserver("/hirch/andere_seite",
                                                                      [this] {
        nullpunkt_zeichnen();
        // sofort neu zeichnen (Peter, 22.10.: über Remote Desktop verschwand die Rückseite erst beim nächsten
        // Ereignis auf der Zeichenfläche)
        if (auto desk = panel.getDesktop()) {
            if (auto c = desk->getCanvas()) {
                c->redraw_all();
                c->queue_draw();
            }
        }
    });

    // --- Material: eine Zeile
    auto mat = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 10);
    klasse(*mat, "hirch-karte");
    auto mtext = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 1);
    mtext->set_hexpand(true);
    mat_groesse.set_xalign(0);
    klasse(mat_groesse, "hirch-titel");
    klasse(mat_groesse, "hirch-mono");
    mat_zeile.set_xalign(0);
    mat_zeile.set_line_wrap(true);
    klasse(mat_zeile, "hirch-dim");
    klasse(mat_zeile, "hirch-klein");
    mtext->add(mat_groesse);
    mtext->add(mat_zeile);
    mat->add(*mtext);
    auto aendern = Gtk::make_managed<Gtk::Button>(TR("Einrichten …"));
    aendern->set_valign(Gtk::ALIGN_CENTER);
    aendern->set_tooltip_text(TR("Auftrag einrichten: Material, Nullpunkt, Höhen und zweiseitige Bearbeitung"));
    aendern->signal_clicked().connect([this] { material_dialog(); });
    mat->add(*aendern);
    // Peter, 23.10.: die Karte nimmt oben Höhe weg – eingerichtet wird über Fräsen › Auftrag einrichten
    mat->set_no_show_all(true);
    mat->hide();
    inhalt.add(*mat);

    // --- Seite 1 / Seite 2 (nur zweiseitig)
    klasse(seiten_box, "linked");
    seiten_box.set_homogeneous(true);
    for (int i = 0; i < 2; ++i) {
        seite_btn[i].set_label(TR("Seite ") + std::to_string(i + 1));
        seite_btn[i].set_size_request(-1, 34);
        seite_btn[i].set_tooltip_text(i == 0 ? TR("Bearbeitungen der Oberseite (erste Seite der Zeichnung)")
                                             : TR("Bearbeitungen nach dem Wenden (zweite Seite der Zeichnung)"));
        seite_btn[i].signal_clicked().connect([this, i] {
            if (baut_ui) return;
            if (platte == 0) platte_waehlen(1, false);      // von der Vorrichtung zurück aufs Werkstück
            seite_waehlen(i + 1, true);
        });
        seiten_box.add(seite_btn[i]);
    }
    seiten_box.set_no_show_all(true);
    inhalt.add(seiten_box);

    // --- Knopfzeile unter den Reitern (Peter, 24.10.: Reiterzeile in Ruhe lassen, Knöpfe knapp darunter):
    // Zeichenfläche hinzufügen/entfernen, Vorrichtung, Platten blättern. Ersetzt die Zeile «Platte ‹ ›».
    {
        platten_box.set_spacing(4);
        klasse(platten_box, "hirch-zf-zeile");
        zf_plus.set_label(TR("+ Zeichenfläche"));
        zf_plus.set_tooltip_text(TR("Rückseite (zweiseitig), Spannvorrichtung oder eine weitere Platte hinzufügen"));
        zf_rueck.set_label(TR("Rückseite (zweiseitig) …"));
        zf_vorr.set_label(TR("Spannvorrichtung …"));
        zf_platte.set_label(TR("Neue Platte"));
        zf_rueck.signal_activate().connect([this] {
            einrichten_vorwahl = "zweiseitig";
            Glib::signal_idle().connect_once([this] { material_dialog(); });
        });
        zf_vorr.signal_activate().connect([this] {
            einrichten_vorwahl = "vorrichtung";
            Glib::signal_idle().connect_once([this] { material_dialog(); });
        });
        zf_platte.signal_activate().connect([this] {
            Glib::signal_idle().connect_once([this] { zeichenflaeche_neu_platte(); });
        });
        zf_menu.append(zf_rueck);
        zf_menu.append(zf_vorr);
        zf_menu.append(zf_platte);
        zf_menu.show_all();
        zf_plus.set_popup(zf_menu);
        zf_plus.show();
        platten_box.add(zf_plus);
        zf_weg.set_label(TR("Entfernen"));
        zf_weg.set_tooltip_text(TR("Die gewählte Zeichenfläche entfernen – die Objekte darauf bleiben in der "
                                "Zeichnung liegen und werden nicht mehr gefräst"));
        zf_weg.signal_clicked().connect([this] {
            Glib::signal_idle().connect_once([this] { zeichenflaeche_entfernen(); });
        });
        zf_weg.show();
        platten_box.add(zf_weg);
        zf_groesse.set_label(TR("Grösse …"));
        zf_groesse.set_tooltip_text(TR("Breite und Höhe der gewählten Platte ändern – jede Platte kann eine eigene "
                                    "Grösse haben"));
        zf_groesse.signal_clicked().connect([this] {
            Glib::signal_idle().connect_once([this] { platte_groesse_dialog(); });
        });
        zf_groesse.set_no_show_all(true);
        platten_box.add(zf_groesse);
        maschine_knopf.set_tooltip_text(TR("Maschine dieser Zeichnung – ein Klick zeigt die anderen"));
        maschine_knopf.set_no_show_all(true);
        UI::pack_end(platten_box, maschine_knopf);
        zf_vor.set_label(TR("Vorrichtung"));
        zf_vor.set_tooltip_text(TR("Die Spannvorrichtung rechnen und zeigen (zuerst fräsen)"));
        zf_vor.signal_clicked().connect([this] {
            if (baut_ui) return;
            platte_waehlen(zf_vor.get_active() ? 0 : 1, true);
        });
        zf_vor.set_no_show_all(true);
        platten_box.add(zf_vor);
        umriss_btn.set_label(TR("Umriss → Vorrichtung"));
        umriss_btn.set_tooltip_text(TR("Die ausgewählten Objekte des Werkstücks grau auf die Vorrichtung kopieren – "
                                    "dort, wo das Werkstück liegt. Dann mit Pfad › Versatz das Spiel dazugeben "
                                    "und als Tasche einfärben."));
        umriss_btn.signal_clicked().connect([this] { Glib::signal_idle().connect_once([this] { umriss_kopieren(); }); });
        umriss_btn.set_no_show_all(true);
        platten_box.add(umriss_btn);
        auto luft = Gtk::make_managed<Gtk::Label>();
        luft->set_hexpand(true);
        luft->show();
        platten_box.add(*luft);
        platte_text.set_xalign(1);
        klasse(platte_text, "hirch-dim");
        platte_text.set_no_show_all(true);
        platten_box.add(platte_text);
        platte_zurueck.set_image_from_icon_name("go-previous-symbolic", Gtk::ICON_SIZE_BUTTON);
        platte_vor.set_image_from_icon_name("go-next-symbolic", Gtk::ICON_SIZE_BUTTON);
        platte_zurueck.set_tooltip_text(TR("Vorige Platte rechnen und zeigen"));
        platte_vor.set_tooltip_text(TR("Nächste Platte rechnen und zeigen"));
        platte_zurueck.signal_clicked().connect([this] { platte_waehlen(std::max(1, platte - 1), true); });
        platte_vor.signal_clicked().connect([this] { platte_waehlen(platte + 1, true); });
        platte_zurueck.set_no_show_all(true);
        platte_vor.set_no_show_all(true);
        platten_box.add(platte_zurueck);
        platten_box.add(platte_vor);
        platten_box.set_tooltip_text(TR("Jede Seite der Zeichnung ist eine Zeichenfläche: Platten (zweiseitig je zwei "
                                     "Seiten) und die Spannvorrichtung. Gerechnet und gespeichert wird die gewählte – "
                                     "die Datei bekommt «_Vorrichtung», «_Platte2» usw. im Namen. Ein Klick in eine "
                                     "Zeichenfläche wählt sie."));
        platten_box.set_no_show_all(true);
        inhalt.add(platten_box);
        vor_hinweis.set_text(TR("Spannvorrichtung: zuerst dieses Programm fräsen. Danach die Vorrichtung gespannt "
                             "lassen, das Werkstück einlegen und Z auf der Werkstückoberfläche antasten – XY bleibt."));
        vor_hinweis.set_line_wrap(true);
        vor_hinweis.set_xalign(0);
        klasse(vor_hinweis, "hirch-dim");
        klasse(vor_hinweis, "hirch-klein");
        vor_hinweis.set_no_show_all(true);
        inhalt.add(vor_hinweis);
    }

    // --- Bearbeitungen
    auto bea = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
    auto bkopf = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
    ablauf_titel.set_text(TR("ABLAUF"));
    ablauf_titel.set_xalign(0);
    klasse(ablauf_titel, "hirch-kopf");
    ablauf_titel.set_hexpand(true);
    bkopf->add(ablauf_titel);
    neu_btn.set_label(TR("+ Neu"));
    neu_btn.set_tooltip_text(TR("Bearbeitung hinzufügen – ausgewählte Objekte bekommen ihre Farbe"));
    neu_btn.set_popup(neu_menu);
    neu_menu_bauen();
    // Vorgaben «So fräse ich» gleich beim Start holen (gelten für die erste neue Bearbeitung)
    Glib::signal_timeout().connect_once([this] {
        python({"datenbank", "werkstoffe"}, [](bool, std::string const &, std::string const &) {});
    }, 1500);
    auto ordnen_btn = Gtk::make_managed<Gtk::Button>(TR("Ordnen"));
    ordnen_btn->set_tooltip_text(TR("Ablauf einmal sinnvoll ordnen: Planfräsen zuerst, Kontur (Ausschneiden) "
                                 "zuletzt, gleiche Werkzeuge beieinander. Sonst gilt genau die Reihenfolge "
                                 "der Liste."));
    ordnen_btn->signal_clicked().connect([this] {
        Glib::signal_idle().connect_once([this] { ordnen(); });
    });
    bkopf->add(*ordnen_btn);
    bkopf->add(neu_btn);
    bea->add(*bkopf);

    // Wenden (vor Seite 2)
    klasse(wenden_box, "hirch-wenden");
    auto wicon = Gtk::make_managed<Gtk::Image>();
    wicon->set_from_icon_name("object-flip-vertical", Gtk::ICON_SIZE_LARGE_TOOLBAR);
    wenden_box.add(*wicon);
    wenden_text.set_xalign(0);
    wenden_text.set_line_wrap(true);
    wenden_text.set_use_markup(true);
    wenden_box.add(wenden_text);
    wenden_box.set_no_show_all(true);
    bea->add(wenden_box);
    bea->add(liste);
    liste_leer.set_line_wrap(true);
    liste_leer.set_xalign(0);
    klasse(liste_leer, "hirch-dim");
    liste_leer.set_no_show_all(true);       // nur liste_aufbauen zeigt es (nicht ein show_all des Reiters)
    liste_leer.set_text(TR("Noch nichts zu fräsen. Linien in einer Fräsfarbe einfärben "
                        "(Leiste rechts auf der Zeichenfläche) – Rot V-Carve, Orange Gravur, Grün Tasche, "
                        "Blau Kontur, Violett Bohren, Türkis Gewinde."));
    bea->add(liste_leer);
    tipp.set_text(TR("Doppelklick öffnet eine Bearbeitung · Ziehen ändert die Reihenfolge in der Liste"));
    tipp.set_xalign(0);
    klasse(tipp, "hirch-dim");
    klasse(tipp, "hirch-klein");
    tipp.set_line_wrap(true);
    bea->add(tipp);
    bea->add(warnbox);
    inhalt.add(*bea);
    inhalt.add(rechen_balken);

    // --- Ausgabe
    auto aus = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
    aus->add(*label(TR("AUSGABE"), "hirch-kopf"));
    // keine Wahl der Steuerung hier: sie steht in «Einstellungen › Maschine» (Peter, 19.10.)

    auto zbox = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    auto zl = label(TR("Gesamtzeit"));
    zl->set_hexpand(true);
    zbox->add(*zl);
    zbox->add(spinner);
    gesamtzeit.set_text("–");
    klasse(gesamtzeit, "hirch-gross");
    zbox->add(gesamtzeit);
    aus->add(*zbox);
    strahl.set_size_request(-1, 18);
    // Erklärung steht beim Darüberfahren in der Zeile darunter (ein Tooltip würde sie verdecken)
    strahl.signal_draw().connect([this](Cr const &cr) { strahl_zeichnen(cr); return true; });
    strahl.add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::POINTER_MOTION_MASK |
                      Gdk::LEAVE_NOTIFY_MASK);
    strahl.signal_button_press_event().connect([this](GdkEventButton *e) { return strahl_druck(e); });
    strahl.signal_motion_notify_event().connect([this](GdkEventMotion *e) { return strahl_bewegung(e); });
    strahl.signal_button_release_event().connect([this](GdkEventButton *e) { return strahl_los(e); });
    strahl.signal_leave_notify_event().connect([this](GdkEventCrossing *) {
        if (zieh < 0) {
            strahl_info.set_text("");
            if (auto w = strahl.get_window()) w->set_cursor();
        }
        return false;
    });
    aus->add(strahl);
    strahl_info.set_xalign(0);
    klasse(strahl_info, "hirch-klein");
    aus->add(strahl_info);
    status.set_xalign(0);
    status.set_line_wrap(true);
    status.set_selectable(true);
    klasse(status, "hirch-dim");
    klasse(status, "hirch-klein");
    aus->add(status);

    auto knoepfe = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    knoepfe->set_homogeneous(true);
    vorschau_btn.set_label(TR("Vorschau 3D …"));
    vorschau_btn.set_tooltip_text(TR("Fräsen simulieren – zeigt genau die Programme, die gespeichert werden"));
    // Peter 03.11.: vorerst die 3D-Ansicht im Hauptfenster statt des eigenen Fensters (hirch-vorschau.cpp bleibt)
    vorschau_btn.signal_clicked().connect([this] {
        auto desk = panel.getDesktop();
        auto win = desk ? desk->getInkscapeWindow() : nullptr;
        if (win && win->lookup_action("hirch-ansicht"))
            win->activate_action("hirch-ansicht", Glib::Variant<Glib::ustring>::create("3d"));
        else
            vorschau();
    });
    speichern_btn.set_label(TR("G-Code speichern"));
    speichern_btn.set_tooltip_text(TR("Programm für die Fräse speichern – vorher kommt die Prüfliste"));
    klasse(speichern_btn, "suggested-action");
    speichern_btn.signal_clicked().connect([this] { speichern(); });
    // B-31 (Peter 04.11.): Knopf «Vorschau 3D …» weg – die grosse 3D-Ansicht ersetzt ihn (Umschalter «2D | 3D»)
    {
        // «G-Code speichern» mit ▾: Luftfräsen (Peter, 20.10.)
        auto paar = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
        klasse(*paar, "linked");
        speichern_btn.set_hexpand(true);
        paar->pack_start(speichern_btn, true, true);
        klasse(speichern_mehr, "suggested-action");
        speichern_mehr.set_tooltip_text(TR("Luftfräsen"));
        auto luft = Gtk::make_managed<Gtk::MenuItem>(TR("Luftfräsen speichern …"));
        luft->set_tooltip_text(TR("Dieselben Bahnen, aber so hoch, dass nichts geschnitten wird – zum Prüfen "
                                  "von Nullpunkt, Spannung und Weg"));
        luft->signal_activate().connect([this] { luftfraesen(false); });
        speichern_menu.append(*luft);
        luft_stick_item = Gtk::make_managed<Gtk::MenuItem>(TR("Luftfräsen auf den USB-Stick"));
        luft_stick_item->signal_activate().connect([this] { luftfraesen(true); });
        speichern_menu.append(*luft_stick_item);
        speichern_menu.show_all();
        speichern_mehr.set_popup(speichern_menu);
        paar->pack_start(speichern_mehr, false, false);
        knoepfe->add(*paar);
    }
    aus->add(*knoepfe);
    stick_btn.set_no_show_all(true);
    stick_btn.signal_clicked().connect([this] { auf_stick(); });
    zusammen_btn.set_label(TR("Zusammenfassung …"));
    zusammen_btn.set_tooltip_text(TR("Ein Blatt zum Fotografieren: Material, Nullpunkt, Werkzeuge mit Drehzahl "
                                     "und jeder Halt zum Werkzeugwechsel"));
    zusammen_btn.signal_clicked().connect([this] { zusammenfassung(); });
    {
        auto reihe = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        reihe->set_homogeneous(true);
        reihe->add(zusammen_btn);
        reihe->add(stick_btn);
        aus->add(*reihe);
    }
    {
        auto vm = Gio::VolumeMonitor::get();
        vm->signal_mount_added().connect([this](Glib::RefPtr<Gio::Mount> const &) { stick_knopf_auffrischen(); });
        vm->signal_mount_removed().connect([this](Glib::RefPtr<Gio::Mount> const &) { stick_knopf_auffrischen(); });
        stick_knopf_auffrischen();
    }
    // Ausgabe bleibt unten stehen, auch wenn die Liste lang wird
    auto unten = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 0);
    unten->add(*Gtk::make_managed<Gtk::Separator>());
    aus->set_margin_start(10);
    aus->set_margin_end(10);
    aus->set_margin_top(10);
    aus->set_margin_bottom(10);
    unten->add(*aus);
    seite_fraesen.pack_end(*unten, false, false);

    anzeigen();
}

SPDocument *HirchFraesen::Impl::doc()
{ return panel.getDocument(); }

void HirchFraesen::Impl::beobachten(XML::Node *wurzel)
{
    if (beobachtet) {
        beobachtet->removeObserver(beobachter);
        beobachtet->removeSubtreeObserver(auch_beobachter);
    }
    beobachtet = wurzel;
    if (beobachtet) {
        beobachtet->addObserver(beobachter);
        beobachtet->addSubtreeObserver(auch_beobachter);
    }
}

void HirchFraesen::Impl::dokument_neu()
{
    doc_mod.disconnect();
    doc_tot.disconnect();
    beobachten(doc() ? doc()->getReprRoot() : nullptr);
    if (auto d = doc()) {
        // Dokument wird geschlossen: Beobachter nicht mehr abmelden
        doc_tot = d->connectDestroy([this] { beobachtet = nullptr; });
    }
    letzte_xml.clear();
    hat_analyse = false;
    platte = platten = 1;
    ergebnis_aktuell = false;
    ergebnis = {};
    zeit_cache.clear();
    seiten_rest.clear();
    platten_stand.clear();
    bahnen_weg();
    rillen.clear();
    if (auto d = doc()) {
        doc_mod = d->connectModified([this](unsigned) { aenderung(); });
        // Werkzeug gewechselt: wartet eine Änderung, jetzt rechnen
        werkzeug_wechsel.disconnect();
        if (auto desk = panel.getDesktop()) {
            werkzeug_wechsel = desk->connectEventContextChanged([this](SPDesktop *, Inkscape::UI::Tools::ToolBase *) {
                if (pausiert && !werkzeug_pausiert()) {
                    pausiert = false;
                    spaeter(50);
                }
                rillen_spaeter();
            });
        }
        leinwand_verbinden();
        // geöffnete DXF/AI/PDF-Datei: einmal aufräumen
        if (auto fn = d->getDocumentFilename(); fn && !import_geprueft.count(d) &&
            Inkscape::Preferences::get()->getBool("/hirch/import_aufraeumen", true)) {
            auto e = import_endung(fn);
            if (!e.empty()) {
                import_geprueft.insert(d);
                Glib::signal_timeout().connect_once([this, e] {
                    if (auto sel = panel.getSelection()) sel->clear();
                    aufraeumen(e, true);
                }, 600);
            }
        }
        seite_gewaehlt.disconnect();
        seite_gewaehlt = d->getPageManager().connectPageSelected([this](SPPage *pg) {
            // nur die Id merken: beim Entfernen einer Zeichenfläche ist die Seite im Leerlauf schon weg
            if (!pg || !pg->getId()) return;
            std::string id = pg->getId();
            Glib::signal_idle().connect_once([this, id] {
                if (auto dd = doc())
                    if (auto p = cast<SPPage>(dd->getObjectById(id))) seite_gewaehlt_von(p);
            });
        });
        spaeter(50);
        // neu geöffnet: ganze Seite (= Material) zeigen. Das Fenster bekommt
        // seine endgültige Grösse oft erst etwas später (Maximieren beim
        // Start): in den ersten Sekunden bei jeder Grössenänderung neu einpassen.
        einpassen_bis = g_get_monotonic_time() + 4 * G_USEC_PER_SEC;
        canvas_groesse.disconnect();
        if (auto desk = panel.getDesktop(); desk && desk->getCanvas()) {
            canvas_groesse = desk->getCanvas()->signal_size_allocate().connect([this](Gtk::Allocation &) {
                if (g_get_monotonic_time() < einpassen_bis) seite_zeigen(120);
            });
        }
        seite_zeigen(300);
    }
    anzeigen();
}

bool HirchFraesen::Impl::platte_groesse_von(Inkscape::XML::Node *r, double &w_uu, double &h_uu, double uu_px)
{
    auto a = r ? r->attribute("hirch:platte-groesse") : nullptr;
    if (!a) return false;
    double bm = 0, hm = 0;
    std::string t(a);
    for (auto &c : t) if (c == 'x' || c == 'X') c = ' ';
    std::istringstream is(t);
    is.imbue(std::locale::classic());
    if (!(is >> bm >> hm) || bm < 1 || hm < 1) return false;
    if (uu_px <= 0) uu_px = MM_PX;
    w_uu = bm * MM_PX / uu_px;
    h_uu = hm * MM_PX / uu_px;
    return true;
}

void HirchFraesen::Impl::material_groesse(SPDocument *d, double b_mm, double h_mm)
{
    auto root = d->getReprRoot();
    double const px_mm = MM_PX;
    double uu_px = d->getDocumentScale()[Geom::X];        // px je Benutzereinheit
    if (uu_px <= 0) uu_px = px_mm;
    double bw = b_mm * px_mm / uu_px, bh = h_mm * px_mm / uu_px;  // in Benutzereinheiten
    auto z = [](double v) {
        std::ostringstream o;
        o.imbue(std::locale::classic());
        o.precision(10);
        o << v;
        return o.str();
    };
    // nur setzen, was abweicht (sonst gilt die Zeichnung als geändert)
    auto setzen = [](Inkscape::XML::Node *n, char const *k, std::string const &v) {
        auto alt = n->attribute(k);
        if (!alt || v != alt) n->setAttribute(k, v);
    };
    setzen(root, "width", z(b_mm) + "mm");
    setzen(root, "height", z(h_mm) + "mm");
    setzen(root, "viewBox", "0 0 " + z(bw) + " " + z(bh));
    // Grösse der Seiten: Platte 1 (und alle ohne eigene Grösse) wie das Material; Platten mit eigener
    // Grösse (hirch:platte-groesse="B×H" in mm, Peter 26.10.) unten bündig mit Platte 1 – so bleibt
    // der Nullpunkt unten links jeder Platte, wenn sich Platte 1 in der Höhe ändert
    auto ps = platten_seiten(d);
    for (int i = 0; i < (int)ps.size(); ++i) {
        auto r = ps[i]->getRepr();
        double w = bw, h = bh;
        if (i > 0) platte_groesse_von(r, w, h, uu_px);
        if (i == 0) {
            setzen(r, "x", "0");
            setzen(r, "y", "0");
        } else {
            setzen(r, "y", z(bh - h));
        }
        setzen(r, "width", z(w));
        setzen(r, "height", z(h));
    }
    d->ensureUpToDate();
    {
        // weitere Seiten (Seite 2 beim Wenden, weitere Platten) samt Objekten in einer
        // Reihe rechts neben Seite 1 halten
        double luecke = 20.0 * px_mm / uu_px;
        double soll = 0;
        for (int i = 0; i < (int)ps.size(); ++i) {
            auto pg = ps[i];
            if (i > 0) {
                double ist = pg->getRect().left();
                if (std::abs(soll - ist) > 1e-6) pg->movePage(Geom::Translate((soll - ist) * uu_px, 0), true);
            }
            soll += pg->getRect().width() + luecke;
        }
        if (auto vp = vorrichtung_seite(d)) {      // Vorrichtung links vor Seite 1
            double soll = -(vp->getRect().width() + luecke), ist = vp->getRect().left();
            if (std::abs(soll - ist) > 1e-6) vp->movePage(Geom::Translate((soll - ist) * uu_px, 0), true);
        }
    }
    d->ensureUpToDate();
}

void HirchFraesen::Impl::platte_groesse_dialog()
{
    // Platte 1 ist das Material (Materialdialog); weitere Platten bekommen eine eigene Grösse (Peter, 26.10.)
    auto d = doc();
    if (!d || platte == 0) return;
    if (platte <= 1) {
        material_dialog();
        return;
    }
    auto ps = platten_seiten(d);
    int pp = zweiseitig() ? 2 : 1;
    std::vector<SPPage *> meine;
    for (int k = 0; k < pp; ++k) {
        int i = (platte - 1) * pp + k;
        if (i >= 0 && i < (int)ps.size()) meine.push_back(ps[i]);
    }
    if (meine.empty()) return;
    double uu_px = d->getDocumentScale()[Geom::X];
    if (uu_px <= 0) uu_px = MM_PX;
    double b1 = d->getWidth().value("mm"), h1 = d->getHeight().value("mm");
    double w = 0, h = 0;
    bool eigen = platte_groesse_von(meine[0]->getRepr(), w, h, uu_px);
    double b0 = eigen ? w * uu_px / MM_PX : b1, h0 = eigen ? h * uu_px / MM_PX : h1;

    auto dlg = dialog(TRF("Grösse von Platte {}", {std::to_string(platte)}));
    auto box = dlg->get_content_area();
    auto g = formular();
    int z = 0;
    auto wie = Gtk::make_managed<Gtk::CheckButton>(TRF("Wie Platte 1 ({} × {} mm)", {zahl_text(b1), zahl_text(h1)}));
    wie->set_active(!eigen);
    g->attach(*wie, 0, z++, 3);
    auto sb = spin_zeile(*g, z++, TR("Breite"), b0, 10, 5000, 1, 1, "mm");
    auto sh = spin_zeile(*g, z++, TR("Höhe"), h0, 10, 5000, 1, 1, "mm");
    auto an = [wie, sb, sh] {
        sb->set_sensitive(!wie->get_active());
        sh->set_sensitive(!wie->get_active());
    };
    wie->signal_toggled().connect(an);
    an();
    box->add(*g);
    auto info = label(TR("Der Nullpunkt bleibt unten links der Platte; die Objekte darauf bleiben liegen. "
                         "Dicke und Werkstoff gelten für alle Platten."), "hirch-dim");
    info->set_line_wrap(true);
    info->set_max_width_chars(48);
    klasse(*info, "hirch-klein");
    box->add(*info);
    dlg->add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
    klasse(*dlg->add_button(TR("Übernehmen"), Gtk::RESPONSE_OK), "suggested-action");
    dlg->set_default_response(Gtk::RESPONSE_OK);
    dlg->show_all();
    if (dlg->run() != Gtk::RESPONSE_OK) return;
    bool gleich = wie->get_active() || (std::abs(wert(sb) - b1) < 1e-6 && std::abs(wert(sh) - h1) < 1e-6);
    std::string wert_neu = zahl_text(wert(sb)) + "x" + zahl_text(wert(sh));
    for (auto pg : meine) {
        if (gleich) pg->getRepr()->removeAttribute("hirch:platte-groesse");
        else pg->getRepr()->setAttribute("hirch:platte-groesse", wert_neu);
    }
    material_groesse(d, b1, h1);
    DocumentUndo::done(d, TR("Plattengrösse ändern"), "");
    platte_waehlen(platte, true);
}

void HirchFraesen::Impl::seite_zeigen(int ms)
{
    seite_timer.disconnect();
    seite_timer = Glib::signal_timeout().connect([this] {
        if (auto desk = panel.getDesktop()) {
            if (auto w = desk->getInkscapeWindow()) w->activate_action("canvas-zoom-page");
        }
        return false;
    }, ms);
}

std::string HirchFraesen::Impl::modus_kennung()
{ return entwurf_an() && !genau_noetig ? "|entwurf" : "|genau"; }

bool HirchFraesen::Impl::auto_rechnen()
{ return Inkscape::Preferences::get()->getBool("/hirch/auto_rechnen", true); }

bool HirchFraesen::Impl::werkzeug_pausiert(bool *text)
{
    auto desk = panel.getDesktop();
    auto t = desk ? desk->getTool() : nullptr;
    if (!t) return false;
    auto const &pfad = t->getPrefsPath();
    if (text) *text = pfad == "/tools/text";
    // Auswahl, Zoom und Messen ändern nichts oder sind ein fertiger Schritt; alle Zeichen- und
    // Bearbeitungswerkzeuge (Text, Knoten, Stift, Formen …) arbeiten in vielen kleinen Schritten
    return pfad != "/tools/select" && pfad != "/tools/zoom" && pfad != "/tools/measure";
}

void HirchFraesen::Impl::aenderung()
{
    rillen_spaeter();
    // 3D (B-14): das Bild der Unterseite auf Seite 2 folgt dem Modell – auch ohne offenen Reiter «3D»
    if (!seite2_bald.connected())
        seite2_bald = Glib::signal_idle().connect([this] {
            hirch3d::seite2_nachfuehren(doc());
            return false;
        });
    bool schnell = g_get_monotonic_time() < schnell_bis;     // eben eine Einstellung im Panel geändert
    bool text = false;
    if (!schnell && werkzeug_pausiert(&text)) {
        // gerechnet wird beim Wechsel zum Auswahlwerkzeug
        bool neu = !pausiert || pause_text != text;
        pausiert = true;
        pause_text = text;
        timer.disconnect();
        if (neu) anzeigen_status();
        return;
    }
    spaeter(schnell ? 50 : 1500);    // Auswahlwerkzeug: nach einer kurzen Ruhe (mehrmals schieben = einmal)
}

void HirchFraesen::Impl::spaeter(int ms)
{
    timer.disconnect();
    timer = Glib::signal_timeout().connect([this] { auffrischen(); return false; }, ms);
}

void HirchFraesen::Impl::abbrechen()
{
    rechen_gen = 0;
    geparkt = nullptr;
    for (auto l : laeufe) {
        l->abgebrochen = true;
        g_cancellable_cancel(l->cancel);
        g_subprocess_force_exit(l->proc);
    }
    laeufe.clear();
    // Dienst: Wartendes verwerfen; rechnet er gerade, beenden (wird beim nächsten Auftrag neu gestartet)
    dienst_warte.clear();
    if (dienst && dienst->laufend) {
        dienst->beenden();
        dienst.reset();
    }
}

bool HirchFraesen::Impl::dienst_starten()
{
    auto launcher = g_subprocess_launcher_new(GSubprocessFlags(G_SUBPROCESS_FLAGS_STDIN_PIPE |
                                                               G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                                               G_SUBPROCESS_FLAGS_STDERR_SILENCE));
    std::string pfad = hirch_repo() + G_SEARCHPATH_SEPARATOR_S +
                       IO::Resource::get_path_string(IO::Resource::SYSTEM, IO::Resource::EXTENSIONS);
    g_subprocess_launcher_setenv(launcher, "PYTHONPATH", pfad.c_str(), TRUE);
    g_subprocess_launcher_setenv(launcher, "PYTHONIOENCODING", "utf-8", TRUE);
    g_subprocess_launcher_setenv(launcher, "PYTHONUTF8", "1", TRUE);
    g_subprocess_launcher_setenv(launcher, "HIRCH_SPRACHE", hirch_sprache(), TRUE);
    g_subprocess_launcher_setenv(launcher, "HIRCH_EINHEIT", hirch_einheit(), TRUE);
    g_subprocess_launcher_unsetenv(launcher, "PYTHONHOME");
    g_subprocess_launcher_set_cwd(launcher, ordner.c_str());
    auto py = hirch_python();
    char const *argv[] = {py.c_str(), "-m", "hirch_cam.dienst", nullptr};
    GError *fehler = nullptr;
    GSubprocess *proc = g_subprocess_launcher_spawnv(launcher, argv, &fehler);
    g_object_unref(launcher);
    if (!proc) {
        if (fehler) g_error_free(fehler);
        return false;
    }
    auto d = std::make_shared<DienstProzess>();
    d->proc = proc;
    d->ein = g_subprocess_get_stdin_pipe(proc);
    d->aus = g_data_input_stream_new(g_subprocess_get_stdout_pipe(proc));
    g_data_input_stream_set_newline_type(d->aus, G_DATA_STREAM_NEWLINE_TYPE_LF);
    g_buffered_input_stream_set_buffer_size(G_BUFFERED_INPUT_STREAM(d->aus), 1 << 20);
    d->cancel = g_cancellable_new();
    std::weak_ptr<DienstProzess> schwach = d;
    std::weak_ptr<int> lebt = panel_lebt;
    d->weiter = [this, schwach, lebt] {
        if (auto x = schwach.lock(); x && x == dienst)
            Glib::signal_idle().connect_once([this, lebt] {
                if (lebt.lock()) dienst_weiter();         // nur, wenn es das Panel noch gibt
            });
    };
    d->gestorben = [this, schwach](std::unique_ptr<DienstAuftrag> a) {
        auto x = schwach.lock();
        if (!x || x != dienst) return;
        dienst.reset();
        if (a && a->fertig) {               // Notweg: als eigener Prozess wie früher
            dienst_meiden = true;
            python(a->args, a->fertig);
            dienst_meiden = false;
        }
        std::weak_ptr<int> lebt = panel_lebt;
        Glib::signal_idle().connect_once([this, lebt] {
            if (lebt.lock()) dienst_weiter();
        });
    };
    dienst = d;
    dienst_lesen(d);                        // «bereit» abwarten
    return true;
}

void HirchFraesen::Impl::dienst_weiter()
{
    if (dienst_warte.empty()) return;
    if (dienst && (dienst->laufend || !dienst->bereit)) return;    // läuft noch / startet noch
    if (!dienst) {
        if (!dienst_starten()) {
            // ohne Dienst: wie früher je Auftrag ein eigener Prozess
            auto warte = std::move(dienst_warte);
            dienst_warte.clear();
            dienst_meiden = true;
            for (auto &a : warte) python(a.args, a.fertig);
            dienst_meiden = false;
            return;
        }
        return;                             // nach «bereit» geht es weiter
    }
    auto d = dienst;
    d->laufend = std::make_unique<DienstAuftrag>(std::move(dienst_warte.front()));
    dienst_warte.pop_front();
    std::string z = d->laufend->zeile + "\n";
    GError *fehler = nullptr;
    gsize geschrieben = 0;
    if (!g_output_stream_write_all(d->ein, z.data(), z.size(), &geschrieben, nullptr, &fehler) ||
        !g_output_stream_flush(d->ein, nullptr, &fehler)) {
        if (fehler) g_error_free(fehler);
        auto a = std::move(d->laufend);
        d->beenden();
        dienst.reset();
        if (a && a->fertig) {
            dienst_meiden = true;
            python(a->args, a->fertig);
            dienst_meiden = false;
        }
        return;
    }
    dienst_lesen(d);
}

void HirchFraesen::Impl::python(std::vector<std::string> args,
                std::function<void(bool, std::string const &, std::string const &)> fertig, bool abbrechbar)
{
    // B-25: die Maschine der Zeichnung (leer = die zuletzt benutzte). Auch im Prozess setzen: andere Aufrufe
    // (3D einlesen, Import) erben sie so.
    g_setenv("HIRCH_MASCHINE", str(einst, "maschine_id").c_str(), TRUE);
    // «So fräse ich»: jede Antwort von «datenbank» bringt die Vorgaben mit – für neue Bearbeitungen merken
    if (!args.empty() && args[0] == "datenbank") {
        // Windows 08.11.: Datenbank-Aufträge nie abbrechen. Eine Neuberechnung bricht abbrechbare Läufe ab – danach
        // blieb z. B. die Werkstoffliste leer und der Maschinenknopf zeigte nach einem Wechsel den alten Namen.
        abbrechbar = false;
        fertig = [this, f = std::move(fertig)](bool ok, std::string const &o, std::string const &e) {
            bj::object r;
            if (ok && json_lesen(o, r)) {
                if (auto v = unter(r, "vorgaben")) hirch_vorgaben() = *v;
                if (auto m = liste_von(r, "maschinen")) hirch_maschinen() = *m;
                maschine_werte_nachfuehren(r);
                maschine_knopf_auffrischen();
            }
            if (f) f(ok, o, e);
        };
    }
    auto launcher = g_subprocess_launcher_new(
        GSubprocessFlags(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE));
    std::string pfad = hirch_repo() + G_SEARCHPATH_SEPARATOR_S +
                       IO::Resource::get_path_string(IO::Resource::SYSTEM, IO::Resource::EXTENSIONS);
    g_subprocess_launcher_setenv(launcher, "PYTHONPATH", pfad.c_str(), TRUE);
    g_subprocess_launcher_setenv(launcher, "PYTHONIOENCODING", "utf-8", TRUE);
    g_subprocess_launcher_setenv(launcher, "PYTHONUTF8", "1", TRUE);   // Windows: Dateien als UTF-8
    g_subprocess_launcher_setenv(launcher, "HIRCH_SPRACHE", hirch_sprache(), TRUE);
    g_subprocess_launcher_setenv(launcher, "HIRCH_EINHEIT", hirch_einheit(), TRUE);
    g_subprocess_launcher_unsetenv(launcher, "PYTHONHOME");
    g_subprocess_launcher_set_cwd(launcher, ordner.c_str());
    // Rechnungen über den Dienst (bleibt geladen); HIRCH_OHNE_DIENST=1: wie früher je Auftrag ein Prozess
    if (!args.empty() && (args[0] == "analyse" || args[0] == "rechnen" || args[0] == "vorschau") &&
        !dienst_meiden && !g_getenv("HIRCH_OHNE_DIENST")) {
        bj::object a;
        bj::array liste;
        for (auto const &x : args) liste.push_back(bj::value(x));
        a["args"] = liste;
        a["cwd"] = ordner;
        bj::object env;
        env["HIRCH_SPRACHE"] = hirch_sprache();
        env["HIRCH_EINHEIT"] = hirch_einheit();
        env["HIRCH_MASCHINE"] = str(einst, "maschine_id");       // immer setzen: der Dienst bleibt laufen
        a["env"] = env;
        dienst_warte.push_back({bj::serialize(a), args, std::move(fertig)});
        dienst_weiter();
        return;
    }
    args.insert(args.begin(), {hirch_python(), "-m", "hirch_cam.dokument"});
    hirch_leere_argumente(args);
    std::vector<char const *> argv;
    for (auto const &a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    GError *fehler = nullptr;
    GSubprocess *proc = g_subprocess_launcher_spawnv(launcher, argv.data(), &fehler);
    g_object_unref(launcher);
    if (!proc) {
        std::string e = fehler ? fehler->message : "unbekannt";
        if (fehler) g_error_free(fehler);
        fertig(false, "", TR("Python nicht gestartet: ") + e);
        return;
    }
    auto lauf = new Lauf;
    lauf->proc = proc;
    lauf->cancel = g_cancellable_new();
    std::weak_ptr<int> lebt = panel_lebt;
    lauf->fertig = [this, lauf, fertig, lebt](bool ok, std::string const &o, std::string const &e) {
        if (lebt.expired()) return;            // Panel schon zu (nur bei nicht abbrechbaren Läufen möglich)
        laeufe.erase(lauf);
        fertig(ok, o, e);
    };
    if (abbrechbar) laeufe.insert(lauf);       // sonst läuft er auch weiter, wenn eine Änderung neu rechnet
    g_subprocess_communicate_utf8_async(proc, nullptr, lauf->cancel, lauf_fertig, lauf);
}

bool HirchFraesen::Impl::json_lesen(std::string const &text, bj::object &ziel)
{
    boost::system::error_code ec;
    auto v = bj::parse(text, ec);
    if (ec || !v.is_object()) return false;
    ziel = std::move(v.get_object());
    return true;
}

Glib::ustring HirchFraesen::Impl::text_als_pfade(SPDocument *d)
{
    auto kopie = d->copy();
    kopie->ensureUpToDate();
    std::vector<SPItem *> texte;
    std::function<void(SPObject *)> sammeln = [&](SPObject *o) {
        for (auto &c : o->children) {
            if (is<SPText>(&c) || is<SPFlowtext>(&c)) {
                // Beschriftung am provisorischen Nullpunkt bleibt Text (wird nie gefräst, zählt nicht zur Mitte)
                if (c.getRepr() && c.getRepr()->attribute("hirch:nullteil")) continue;
                if (auto it = cast<SPItem>(&c)) texte.push_back(it);
            } else {
                sammeln(&c);
            }
        }
    };
    sammeln(kopie->getRoot());
    if (!texte.empty()) {
        // weitere Bearbeitungen (hirch:auch) gehen beim Umwandeln verloren:
        // merken, die ID bleibt gleich
        std::map<std::string, std::string> auch;
        // im Panel ausgeblendete Texte (Auge, hirch:versteckt) werden trotzdem gefräst: ein ausgeblendeter
        // Text hat kein Layout und würde zu einem leeren Pfad – für das Umwandeln kurz einblenden (Peter, 20.10.)
        std::set<std::string> versteckt;
        for (auto t : texte) {
            auto id = t->getAttribute("id");
            auto a = t->getAttribute("hirch:auch");
            if (id && a) auch[id] = a;
            if (id && t->getAttribute("hirch:versteckt")) {
                versteckt.insert(id);
                t->setHidden(false);
            }
        }
        if (!versteckt.empty()) kopie->ensureUpToDate();
        ObjectSet set(kopie.get());
        set.setList(texte);
        set.toCurves(true);
        // markieren: so zeigt das Panel den Text als ein Objekt
        for (auto item : set.items()) {
            item->setAttribute("hirch:text", "1");
            auto id = item->getAttribute("id");
            if (id && auch.count(id)) item->setAttribute("hirch:auch", auch[id]);
            if (id && versteckt.count(id)) {
                item->setHidden(true);
                item->setAttribute("hirch:versteckt", "1");
            }
        }
        kopie->ensureUpToDate();
    }
    return sp_repr_save_buf(kopie->getReprDoc());
}

bool HirchFraesen::Impl::hat_text(Glib::ustring const &xml)
{
    return xml.find("<text") != Glib::ustring::npos || xml.find("<svg:text") != Glib::ustring::npos ||
           xml.find("flowRoot") != Glib::ustring::npos;
}

void HirchFraesen::Impl::auffrischen()
{
    auto d = doc();
    if (!d) return;
    if (pausiert) {
        pausiert = false;
        anzeigen_status();
    }
    // gezogene Passlöcher zuerst übernehmen (Partner folgen, Einstellungen «frei»)
    passloecher_abgleichen(false);
    auto xml = sp_repr_save_buf(d->getReprDoc());
    {
        // abgehakte Hinweise ändern nichts am Fräsen: nicht neu rechnen
        auto raw = xml.raw();
        // die Kreise der Passlöcher auch nicht: ihre Lage steht in den Einstellungen
        if (auto pi = raw.find("id=\"ebene-passloecher\""); pi != std::string::npos) {
            auto anf = raw.rfind("<", pi);
            auto ende = raw.find("</", pi);           // Kreise schliessen selbst: das ist die Ebene
            if (ende != std::string::npos) ende = raw.find(">", ende);
            auto leer = raw.find("/>", pi);
            auto zu = raw.find(">", pi);
            if (anf != std::string::npos && zu != std::string::npos) {
                if (leer != std::string::npos && leer + 1 == zu) raw.erase(anf, zu + 1 - anf);
                else if (ende != std::string::npos) raw.erase(anf, ende + 1 - anf);
            }
        }
        auto p = raw.find(" hirch:quittiert=\"");
        if (p != std::string::npos) {
            auto e = raw.find('"', p + 18);
            if (e != std::string::npos) raw.erase(p, e - p + 1);
        }
        Glib::ustring vergleich(raw);
        // nur das Auge umgeschaltet: gefräst wird gleich, die Rechnung bleibt gültig (Peter, 20.10.)
        if (nur_sichtbar_geaendert) {
            nur_sichtbar_geaendert = false;
            letzte_xml = vergleich;
            vorlaeufig_an = false;
            anzeigen();
            return;
        }
        erzwungen = erzwungen || letzte_xml.empty();     // jemand will ausdrücklich neu rechnen
        if (vergleich == letzte_xml) {
            if (vorlaeufig_an && !rechnet) {      // nichts geändert: wieder das fertige Ergebnis
                vorlaeufig_an = false;
                anzeigen();
            }
            return;
        }
        letzte_xml = vergleich;
    }
    // Zeichnung mit 3D-Modell: gerechnet wird erst auf Knopfdruck («Rechnen»)
    bool hat_3d = xml.raw().find("hirch:relief=\"") != std::string::npos;
    unsigned gen = ++generation;
    // Läuft eine Rechnung, erst nachsehen, ob die Änderung sie überhaupt betrifft (eigene Analyse
    // neben dem Dienst); nur dann abbrechen. Sonst rechnet sie weiter (Peter, 21.10.).
    bool rechnung_weiter = rechen_gen != 0 && !kennung_laufend.empty() && !erzwungen;
    if (!rechnung_weiter) abbrechen();
    bool erzw = erzwungen;
    erzwungen = false;
    auto svg = Glib::build_filename(ordner, "zeichnung.svg");
    try {
        Glib::file_set_contents(svg, (hat_text(xml) ? text_als_pfade(d) : xml).raw());
    } catch (Glib::Error const &e) {
        fehlertext = TR("Arbeitsordner nicht beschreibbar: ") + e.what().raw();
        anzeigen();
        return;
    }
    if (!rechnet) dauer_vor_analyse = rechen_dauer;
    ergebnis_aktuell = false;
    rechnet = true;
    rechnen_abgebrochen = false;
    std::remove(fortschritt_datei().c_str());     // alter Fortschritt weg
    fehlertext.clear();
    // «Rechnet …» erst zeigen, wenn das Lesen länger dauert (meist ist es gleich fertig)
    status_spaeter.disconnect();
    status_spaeter = Glib::signal_timeout().connect([this, gen] {
        if (gen == generation && rechnet) anzeigen_status();
        return false;
    }, 400);
    if (rechnung_weiter) dienst_meiden = true;       // Dienst rechnet gerade: Analyse als eigener Prozess
    python({"analyse", svg, "--platte", std::to_string(platte)}, [this, gen, svg, hat_3d, erzw](bool ok, std::string const &o, std::string const &e) {
        if (gen != generation) return;
        bj::object a;
        if (!ok || !json_lesen(o, a)) {
            if (rechen_gen != 0) {                 // die laufende Rechnung gilt nicht mehr
                abbrechen();
                rechen_gen = 0;
            }
            rechnet = false;
            fehlertext = TR("Rechenkern nicht bereit: ") + (e.empty() ? o : e).substr(0, 600);
            anzeigen();
            return;
        }
        if (!flag(a, "ok", false)) {
            rechnet = false;
            fehlertext = warnungen_text(a);
            anzeigen();
            return;
        }
        analyse = std::move(a);
        // Läuft gerade eine Änderung (vorläufiger Ablauf), bleibt die Liste bis zum
        // fertigen Ergebnis so stehen – sonst springen die Zeilen zweimal
        bool war_vorlaeufig = vorlaeufig_an;
        platten = std::max(1, (int)num(analyse, "platten", 1));
        platte = std::clamp((int)num(analyse, "platte", platte), platte_min(), platten);
        if (auto e2 = unter(analyse, "einstellungen")) {
            einst = *e2;
            auto &offen = offene_plaetze()[this];
            for (auto it = offen.begin(); it != offen.end();) {
                if (!unter(einst, "werkzeuge")) einst["werkzeuge"] = bj::object();
                auto &ws = einst["werkzeuge"].as_object();
                auto da = ws.if_contains(it->first);
                if (da && da->is_object() && str(da->as_object(), "db_id") == str(it->second, "db_id")) {
                    it = offen.erase(it);          // steht jetzt in der Zeichnung
                    continue;
                }
                if (!da) ws[it->first] = it->second;
                ++it;
            }
        }
        hat_analyse = true;
        if (maschine_ziel && maschine_ohne_analyse)          // Einstellungen › Maschine jetzt mit den echten Werten
            if (auto *box = dynamic_cast<Gtk::Box *>(Glib::wrap(maschine_ziel))) maschine_abschnitt(box);
        if (war_vorlaeufig) vorlaeufig_bauen();
        anzeigen();
        nullpunkt_zeichnen();
        passloecher_abgleichen(true);
        rillen_spaeter();
        einrichten_beim_start();
        rueckfrage_pruefen();
        if (neu_offen) {
            // «+ Neu»: erst rechnen, wenn der Dialog mit «Übernehmen» zu ist
            neu_wartet = true;
            neu_gen = gen;
            neu_svg = svg;
            rechnet = false;
            anzeigen_status();
            if (!neu_id.empty() && gen > neu_ab) Glib::signal_idle().connect_once([this] { neu_dialog(); });
            return;
        }
        bool gewollt = rechnen_gewollt;
        // Nur neu rechnen, wenn sich etwas Gefrästes geändert hat (Kennung der Analyse, mit dem Modus
        // Entwurf/genau): eine neue schwarze Linie, Text ohne Fräsfarbe oder ein solches Objekt
        // verschoben ändern nichts. Gilt auch für 3D – dort entscheidet sie nur, ob «Rechnen» nötig ist.
        auto kenn = str(analyse, "kennung", "");
        auto kenn_voll = kenn.empty() ? kenn : kenn + modus_kennung();
        bool frei = !erzw && !gewollt && !kenn.empty() && !speichern_wartet && !stick_wartet;
        // eben aus «auto» übernommene Haltestege: gleicher G-Code (Fräsbahnen 26.10.), die Rechnung gilt weiter
        if (marken_uebernommen && rechen_gen == 0 && !ergebnis.empty()) kennung_fertig = kenn_voll;
        marken_uebernommen = false;
        if (frei && rechen_gen != 0 && kenn_voll == kennung_laufend) {
            rechen_gen = gen;                    // die laufende Rechnung gilt weiter
            status_spaeter.disconnect();
            if (geparkt) {                       // … und ist inzwischen schon fertig
                auto g = std::move(geparkt);
                geparkt = nullptr;
                g();
            }
            return;
        }
        if (frei && rechen_gen == 0 && kenn_voll == kennung_fertig && !ergebnis.empty() &&
            !(genau_noetig && flag(ergebnis, "entwurf", false))) {
            status_spaeter.disconnect();
            rechnet = false;
            ergebnis_aktuell = true;
            weg_rahmen.clear();
            wartet_knopf = false;
            vorlaeufig_an = false;
            anzeigen();
            // «Fertig in …» zeigt die Dauer der Rechnung, nicht die Lesezeit der Analyse
            rechen_dauer = bahnen_wieder ? dauer_wieder : dauer_vor_analyse;
            anzeigen_status();
            if (bahnen_wieder) {
                bahnen_wieder = false;
                bahnen_zeichnen();
            }
            return;
        }
        bool warten_aus = !auto_rechnen() && !kenn.empty();
        if ((hat_3d || warten_aus) && !gewollt && !speichern_wartet && !stick_wartet) {
            // 3D (und «Automatisch rechnen» aus): warten auf «Rechnen» (Speichern und USB-Stick rechnen selbst)
            if (rechen_gen != 0) abbrechen();
            wartet_knopf = true;
            knopf_gen = gen;
            knopf_svg = svg;
            rechnet = false;
            anzeigen_status();
            return;
        }
        rechnen_gewollt = false;
        wartet_knopf = false;
        if (rechen_gen != 0) abbrechen();
        rechnen(gen, svg);
    });
    dienst_meiden = false;
}

void HirchFraesen::Impl::rechnen(unsigned gen, std::string const &svg)
{
    rechen_gen = gen;
    rechnet = true;
    bahnen_wieder = false;
    anzeigen_status();
    stamm_rechnung = vorgeschlagener_name();
    std::vector<std::string> args{"rechnen", svg, "--ordner", ausgabe_ordner(), "--name",
                                  stamm_rechnung, "--platte", std::to_string(platte)};
    // 3D-Entwurfsmodus: gröber und schneller – ausser vor dem Speichern (genau_noetig)
    if (entwurf_an() && !genau_noetig) args.push_back("--entwurf");
    args.insert(args.end(), {"--fortschritt", fortschritt_datei()});
    kennung_laufend = str(analyse, "kennung", "");
    if (!kennung_laufend.empty()) kennung_laufend += modus_kennung();
    geparkt = nullptr;
    auto ende = std::make_shared<std::function<void(bool, std::string const &, std::string const &)>>(
           [this](bool ok, std::string const &o, std::string const &e) {
               rechen_gen = 0;
               rechnet = false;
               bj::object r;
               if (!ok || !json_lesen(o, r)) {
                   kennung_fertig.clear();
                   fehlertext = TR("Rechnen fehlgeschlagen: ") + (e.empty() ? o : e).substr(0, 600);
                   ergebnis = {};
                   ergebnis_aktuell = false;
                   bahnen_weg();
                   speichern_wartet = false;
                   anzeigen();
                   return;
               }
               ergebnis = std::move(r);
               weg_rahmen.clear();
               kennung_fertig = kennung_laufend;
               vorlaeufig_an = false;
               zeiten_merken();
               ergebnis_aktuell = true;
               fehlertext.clear();
               anzeigen();
               bahnen_zeichnen();
               if (!flag(ergebnis, "entwurf", false)) Glib::signal_idle().connect_once([this] { stege_auto_uebernehmen(); });
               if (vorschau_fenster && vorschau_fenster->get_visible()) vorschau_laden();
               if (!flag(ergebnis, "entwurf", false)) genau_noetig = false;
               if (speichern_wartet) {
                   speichern_wartet = false;
                   speichern();
               }
               if (stick_wartet) {
                   stick_wartet = false;
                   auf_stick();
               }
               if (nach_rechnen) {
                   auto f = std::move(nach_rechnen);
                   nach_rechnen = nullptr;
                   f();
               }
           });
    python(args, [this, ende](bool ok, std::string const &o, std::string const &e) {
        if (rechen_gen == 0) return;                 // abgebrochen
        if (rechen_gen != generation) {
            // eine neue Analyse läuft noch: entscheidet sie «gilt weiter», wird das Ergebnis übernommen
            geparkt = [ende, ok, o, e] { (*ende)(ok, o, e); };
            return;
        }
        (*ende)(ok, o, e);
    });
}

bool HirchFraesen::Impl::bahnen_an()
{ return Inkscape::Preferences::get()->getBool("/hirch/fraesbahnen", true); }

void HirchFraesen::Impl::bahnen_setzen(bool an)
{
    Inkscape::Preferences::get()->setBool("/hirch/fraesbahnen", an);
    if (bahnen_check.get_active() != an) bahnen_check.set_active(an);
    if (auto desk = panel.getDesktop()) {
        if (auto win = desk->getInkscapeWindow()) {
            auto a = Glib::RefPtr<Gio::SimpleAction>::cast_dynamic(win->lookup_action("hirch-bahnen"));
            if (a) a->change_state(an);
        }
    }
    bahnen_zeichnen();
    rillen_zeichnen();
}

void HirchFraesen::Impl::desktop_beobachten(SPDesktop *desk)
{
    if (desk == bahn_desktop) return;
    bahn_desktop_tot.disconnect();
    bahn_desktop = desk;
    bahn_desktop_tot = desk->connectDestroy([this](SPDesktop *) {
        for (auto &i : bahn_items) (void)i.release();
        bahn_items.clear();
        for (auto &i : geister) (void)i.release();
        geister.clear();
        for (auto *i : {&np_kreis, &np_x, &np_y, &np2_kreis, &np2_x, &np2_y}) (void)i->release();
        (void)marke.release();
        bahn_desktop = nullptr;
    });
}

// ---------------------------------------------------------------------------
// Rechenanzeige: oben im Panel ein Streifen mit Kreisel, Uhr, woran gerade gerechnet wird und
// «Abbrechen»; danach «Fertig in …» bzw. «Abgebrochen …». Der Takt (250 ms) meldet den Stand
// auch an das Kärtchen neben der schwebenden Leiste und an den Reiter «3D».

namespace {
std::string uhr_text(double s)
{
    int t = std::max(0, (int)std::floor(s));
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%02d", t / 60, t % 60);
    return buf;
}
} // namespace

void HirchFraesen::Impl::rechen_balken_bauen()
{
    klasse(rechen_puls, "hirch-rechen-puls");
    rechen_puls.set_pulse_step(0.08);
    rechen_puls.set_no_show_all(true);
    rechen_balken.add(rechen_puls);
    auto zeile = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 10);
    zeile->set_margin_start(12);
    zeile->set_margin_end(10);
    zeile->set_margin_top(8);
    zeile->set_margin_bottom(8);
    rechen_kreisel.set_no_show_all(true);
    zeile->add(rechen_kreisel);
    auto texte = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 1);
    texte->set_hexpand(true);
    rechen_titel.set_xalign(0);
    rechen_titel.set_line_wrap(true);
    klasse(rechen_titel, "hirch-rechen-titel");
    texte->add(rechen_titel);
    rechen_schritt.set_xalign(0);
    rechen_schritt.set_ellipsize(Pango::ELLIPSIZE_END);
    rechen_schritt.set_no_show_all(true);
    klasse(rechen_schritt, "hirch-klein");
    texte->add(rechen_schritt);
    zeile->add(*texte);
    rechen_schild.set_text(TR("ENTWURF"));
    rechen_schild.set_valign(Gtk::ALIGN_CENTER);
    rechen_schild.set_tooltip_text(TR("3D-Bahnen im Entwurf gerechnet – gespeichert wird immer genau"));
    rechen_schild.set_no_show_all(true);
    klasse(rechen_schild, "hirch-schild");
    zeile->add(rechen_schild);
    // Entwurf (3d 08.11., Peter «Alles heute»: aus dem Reiter «3D» hierher): nur, wenn die Zeichnung ein 3D-Modell hat
    rechen_entwurf.set_label(TR("Entwurf"));
    rechen_entwurf.set_valign(Gtk::ALIGN_CENTER);
    rechen_entwurf.set_tooltip_text(TR("Zum Einstellen: 3D-Schlichten mit dreifachem Bahnabstand und dreifacher "
                                       "Abtastung. Gespeichert und auf den USB-Stick geschrieben wird immer genau "
                                       "gerechnet."));
    rechen_entwurf.set_no_show_all(true);
    rechen_entwurf.set_active(entwurf_an());
    rechen_entwurf.signal_toggled().connect([this] {
        if (rechen_entwurf_setzt) return;
        Inkscape::Preferences::get()->setBool("/hirch/3d/entwurf", rechen_entwurf.get_active());
        // gleich neu rechnen wie «Jetzt rechnen» (3D-Zeichnungen warten sonst auf den Knopf; Windows 08.11.)
        if (!rechnet) rechnen_jetzt();
    });
    zeile->add(rechen_entwurf);
    rechen_knopf.set_valign(Gtk::ALIGN_CENTER);
    rechen_knopf.set_no_show_all(true);
    rechen_knopf.signal_clicked().connect([this] {
        if (rechnet) rechnen_abbrechen();
        else rechnen_jetzt();             // «Rechnen» (3D) oder «Neu rechnen» nach einem Abbruch
    });
    zeile->add(rechen_knopf);
    rechen_balken.add(*zeile);
    rechen_balken.set_no_show_all(true);
}

void HirchFraesen::Impl::rechnen_abbrechen()
{
    if (!rechnet) return;
    abbrechen();
    ++generation;                         // späte Antworten gelten nicht mehr
    timer.disconnect();
    rechnet = false;
    rechnen_abgebrochen = true;
    speichern_wartet = stick_wartet = genau_noetig = false;
    status.set_text("");
    anzeigen();
    anzeigen_status();
}

void HirchFraesen::Impl::rechen_melden(bool laeuft, std::string const &uhr)
{
    auto desk = panel.getDesktop();
    if (!desk) return;
    HirchFarbleiste::rechenstand(desk, laeuft, uhr);
    if (auto c = desk->getContainer())
        if (auto d3 = dynamic_cast<Hirch3D *>(c->get_dialog("Hirch3D")))
            d3->rechenstand(laeuft, TR("Wartet auf die Rechnung …"), uhr);
}

bool HirchFraesen::Impl::rechen_tick()
{
    if (!rechnet) return false;
    auto uhr = uhr_text((g_get_monotonic_time() - rechen_start) / 1e6);
    rechen_titel.set_text(TRF("Rechnet … {}", {uhr}));
    std::string schritt = TR("Zeichnung lesen …");
    try {
        auto text = Glib::file_get_contents(fortschritt_datei());
        boost::system::error_code ec;
        auto v = bj::parse(text, ec);
        if (!ec && v.is_object()) {
            auto const &o = v.as_object();
            schritt = str(o, "text");
            if (o.contains("nr") && o.contains("von"))
                schritt = TRF("{} ({} von {})", {schritt, ganz(num(o, "nr", 1)), ganz(num(o, "von", 1))});
        }
    } catch (...) {
    }
    rechen_schritt.set_text(schritt);
    rechen_puls.pulse();
    rechen_melden(true, uhr);
    return true;
}

void HirchFraesen::Impl::rechenanzeige()
{
    bool r = rechnet;
    if (r && !war_rechnet) {
        rechen_start = g_get_monotonic_time();
        rechen_schritt.set_text(TR("Zeichnung lesen …"));    // nicht den Schritt der letzten Rechnung
        rechen_takt.disconnect();
        rechen_takt = Glib::signal_timeout().connect([this] { return rechen_tick(); }, 250);
    }
    if (!r && war_rechnet) {
        rechen_dauer = (g_get_monotonic_time() - rechen_start) / 1e6;
        rechen_takt.disconnect();
        rechen_melden(false, "");
    }
    war_rechnet = r;
    auto c = rechen_balken.get_style_context();
    for (char const *k : {"hirch-rechnet", "hirch-fertig", "hirch-abgebrochen", "hirch-wartet"}) c->remove_class(k);
    rechen_knopf.get_style_context()->remove_class("suggested-action");
    if (r) wartet_knopf = false;
    bool fertig = ergebnis_aktuell && flag(ergebnis, "ok", false);
    bool zeigen = true;
    if (r) {
        c->add_class("hirch-rechnet");
        rechen_titel.set_text(TRF("Rechnet … {}", {uhr_text((g_get_monotonic_time() - rechen_start) / 1e6)}));
        rechen_schritt.show();
        rechen_kreisel.show();
        rechen_kreisel.start();
        rechen_puls.show();
        rechen_schild.set_visible(entwurf_an() && !genau_noetig);
        rechen_knopf.set_label(TR("Abbrechen"));
        rechen_knopf.show();
    } else {
        rechen_kreisel.stop();
        rechen_kreisel.hide();
        rechen_puls.hide();
        rechen_schritt.hide();
        if (pausiert && !wartet_knopf) {
            c->add_class("hirch-wartet");
            rechen_titel.set_text(pause_text ? TR("◷ Text wird bearbeitet – gerechnet wird danach")
                                             : TR("◷ Geändert – gerechnet wird, wenn du fertig bist"));
            rechen_schritt.set_text(TR("beim Wechsel zum Auswahlwerkzeug"));
            rechen_schritt.show();
            rechen_schild.hide();
            rechen_knopf.set_label(TR("Jetzt rechnen"));
            rechen_knopf.show();
        } else if (wartet_knopf) {
            c->add_class("hirch-wartet");
            bool alt = !ergebnis.empty();
            rechen_titel.set_text(alt ? TR("◷ Geändert – noch nicht gerechnet") : TR("◷ Noch nicht gerechnet"));
            rechen_schritt.set_text(TR("Speichern und USB-Stick rechnen selbst"));
            rechen_schritt.show();
            rechen_schild.set_visible(entwurf_an());
            rechen_knopf.set_label(TR("Jetzt rechnen"));
            rechen_knopf.get_style_context()->add_class("suggested-action");
            rechen_knopf.show();
        } else if (rechnen_abgebrochen) {
            c->add_class("hirch-abgebrochen");
            rechen_titel.set_text(TRF("■ Abgebrochen nach {} – noch kein neues Programm", {uhr_text(rechen_dauer)}));
            rechen_schild.hide();
            rechen_knopf.set_label(TR("Jetzt rechnen"));
            rechen_knopf.show();
        } else if (fertig) {
            c->add_class("hirch-fertig");
            rechen_titel.set_text(rechen_dauer < 1.0 ? std::string(TR("✓ Fertig"))
                                                     : TRF("✓ Fertig in {}", {uhr_text(rechen_dauer)}));
            rechen_schild.set_visible(flag(ergebnis, "entwurf", false));
            rechen_knopf.hide();
        } else {
            zeigen = false;
        }
    }
    if (platte_leer && !r) zeigen = false;
    rechen_entwurf_setzt = true;
    rechen_entwurf.set_active(entwurf_an());
    rechen_entwurf_setzt = false;
    rechen_entwurf.set_visible(zeigen && doc() && hirch_3d_hat_modell(doc()));
    rechen_balken.set_visible(zeigen);
    if (zeigen) rechen_balken.show_all_children();
    // die Liste zeigt während des Rechnens (und nach einem Abbruch) den alten Stand: blass
    liste.set_opacity(r || rechnen_abgebrochen || wartet_knopf || pausiert ? 0.45 : 1.0);
    wartet_melden();
}

void HirchFraesen::Impl::wartet_melden()
{
    bool an = wartet_knopf && !rechnet && !platte_leer;
    if (an == war_wartet) return;
    war_wartet = an;
    auto desk = panel.getDesktop();
    if (!desk) return;
    HirchFarbleiste::wartet(desk, an);
    if (auto c = desk->getContainer())
        if (auto d3 = dynamic_cast<Hirch3D *>(c->get_dialog("Hirch3D")))
            d3->wartet(an, ergebnis.empty() ? TR("Noch nicht gerechnet") : TR("Geändert – noch nicht gerechnet"));
}

void HirchFraesen::Impl::rechnen_jetzt()
{
    if (rechnet) return;
    pausiert = false;
    rechnen_abgebrochen = false;
    if (wartet_knopf && knopf_gen == generation && !knopf_svg.empty() && hat_analyse) {
        wartet_knopf = false;
        rechnen(knopf_gen, knopf_svg);       // die eben analysierte Zeichnung
        return;
    }
    wartet_knopf = false;
    rechnen_gewollt = true;
    letzte_xml.clear();
    spaeter(0);
}

// «+ Neu» mit Überschriften (Peter 06.11.: «Überschriften passen») und oben der zuletzt gewählten Art
void HirchFraesen::Impl::neu_menu_bauen()
{
    for (auto *c : neu_menu.get_children()) neu_menu.remove(*c);
    auto eintrag = [this](Art const &a) {
        auto item = Gtk::make_managed<Gtk::MenuItem>();
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        box->add(*farbfeld(&a, 12));
        box->add(*label(std::string(TR(a.name)) + " (" + TR(a.farbe) + ")"));
        item->add(*box);
        std::string key = a.key;
        item->signal_activate().connect([this, key] {
            Inkscape::Preferences::get()->setString("/hirch/neu_zuletzt", key);
            neu(key);
            Glib::signal_idle().connect_once([this] { neu_menu_bauen(); });
        });
        neu_menu.append(*item);
    };
    auto ueberschrift = [this](char const *t, bool trenner) {
        if (trenner) neu_menu.append(*Gtk::make_managed<Gtk::SeparatorMenuItem>());
        auto item = Gtk::make_managed<Gtk::MenuItem>();
        auto l = label(Glib::ustring(TR(t)).uppercase(), "hirch-kopf");
        l->set_xalign(0);
        item->add(*l);
        item->set_sensitive(false);
        neu_menu.append(*item);
    };
    auto zuletzt = Inkscape::Preferences::get()->getString("/hirch/neu_zuletzt");
    bool oben = false;
    if (auto a = art_info(zuletzt.raw())) {
        ueberschrift("Zuletzt", false);
        eintrag(*a);
        oben = true;
    }
    struct Gruppe { char const *titel; std::vector<char const *> arten; };
    for (auto const &g : {Gruppe{"Ausschneiden und Ausräumen", {"kontur", "tasche"}},
                          Gruppe{"Gravieren", {"gravur", "vcarve"}},
                          Gruppe{"Bohren", {"bohren", "gewinde"}},
                          Gruppe{"Fläche", {"planen"}},
                          Gruppe{"3D", {"relief_schruppen", "relief_schlichten", "boeden_waende"}}}) {
        ueberschrift(g.titel, oben);
        oben = true;
        for (auto k : g.arten)
            if (auto a = art_info(k)) eintrag(*a);
    }
    neu_menu.show_all();
}

} // namespace Inkscape::UI::Dialog
