// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Auftrag einrichten / Neue Zeichnung einrichten (material_dialog)
 */
#include "ui/dialog/hirch-intern.h"
#include "ui/dialog/hirch-dateiwahl.h"
#include "ui/dialog/hirch-3d.h"

namespace Inkscape::UI::Dialog {

void HirchFraesen::Impl::material_dialog(bool beim_start, std::string const &modell)
{
    auto d = doc();
    if (!d) return;
    double breite = 300, hoehe = 200, dicke = 18;
    if (auto m = unter(analyse, "material")) {
        breite = num(*m, "breite", breite);
        hoehe = num(*m, "hoehe", hoehe);
        dicke = num(*m, "dicke", dicke);
    }
    bj::object vor;
    if (auto v = unter(analyse, "vorrichtung")) vor = *v;
    if (platte == 0) {                    // Vorrichtung gewählt: das Werkstück einrichten
        if (auto ws = liste_von(vor, "werkstueck"); ws && ws->size() >= 2) {
            breite = (*ws)[0].to_number<double>();
            hoehe = (*ws)[1].to_number<double>();
        }
        if (auto a = d->getReprRoot()->attribute("hirch:materialdicke")) {
            // mit Punkt gespeichert; std::stod läse unter deutscher Spracheinstellung nur bis zum Punkt
            char *ende = nullptr;
            double v = g_ascii_strtod(a, &ende);
            if (ende && ende != a) dicke = v;
        }
    }
    bj::object h;
    if (auto hh = unter(einst, "hoehen")) h = *hh;
    bj::object np;
    if (auto n = unter(einst, "nullpunkt")) np = *n;

    auto dlg = dialog(beim_start ? TR("Neue Zeichnung einrichten") : TR("Auftrag einrichten"));
    auto box = dlg->get_content_area();
    if (beim_start) {
        auto l = label(TR("Zuerst Material und Nullpunkt festlegen – so, wie das Werkstück auf der Maschine "
                       "liegt. Alles lässt sich später über «Einrichten …» im Panel anpassen."), "hirch-dim");
        l->set_line_wrap(true);
        l->set_max_width_chars(70);
        box->add(*l);
    }
    auto spalten = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 24);
    // Nie höher als der Bildschirm (Windows-Sitzung 28.10.: 1145 bzw. 1118 Bildpunkte – «Los geht's» lag unter
    // dem Rand auf 1080er-Bildschirmen): der Inhalt rollt, wenn er nicht passt, die Knöpfe bleiben sichtbar
    {
        int hoehe_max = 900;
        if (auto anzeige = Gdk::Display::get_default()) {
            Glib::RefPtr<Gdk::Monitor> mon;
            if (auto f = dlg->get_transient_for(); f && f->get_window()) mon = anzeige->get_monitor_at_window(f->get_window());
            if (!mon) mon = anzeige->get_primary_monitor();
            if (!mon && anzeige->get_n_monitors() > 0) mon = anzeige->get_monitor(0);
            if (mon) {
                Gdk::Rectangle r;
                mon->get_workarea(r);
                hoehe_max = std::max(400, r.get_height() - 190);   // Titel, Text oben, Knöpfe unten
            }
        }
        auto rolle = Gtk::make_managed<Gtk::ScrolledWindow>();
        rolle->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
        rolle->set_propagate_natural_height(true);
        rolle->set_propagate_natural_width(true);
        rolle->set_max_content_height(hoehe_max);
        rolle->add(*spalten);
        box->add(*rolle);
    }
    // Beim Start: zuletzt bearbeitete Dateien direkt öffnen
    auto gewaehlte_datei = std::make_shared<std::string>();
    int const OEFFNEN = 100;
    if (beim_start) {
        struct Zuletzt { std::string pfad; long zeit; };
        std::vector<Zuletzt> zuletzt;
        for (auto const &it : Gtk::RecentManager::get_default()->get_items()) {
            if (!it || !it->is_local() || !it->exists()) continue;
            std::string pfad = Glib::filename_from_uri(it->get_uri());
            auto klein = Glib::ustring(pfad).lowercase().raw();
            auto endet = [&](char const *e) {
                std::size_t n = std::strlen(e);
                return klein.size() >= n && klein.compare(klein.size() - n, n, e) == 0;
            };
            if (!endet(".hir") && !endet(".hirch") && !endet(".svg") && !endet(".svgz")) continue;
            zuletzt.push_back({pfad, (long)it->get_modified()});
        }
        std::sort(zuletzt.begin(), zuletzt.end(), [](Zuletzt const &a, Zuletzt const &b) { return a.zeit > b.zeit; });
        if (zuletzt.size() > 12) zuletzt.resize(12);
        // Peter 28.10.: Liste immer zeigen (mit «Datei öffnen …»); Rechtsklick: Ordner öffnen, aus der Liste
        // entfernen, Datei löschen (in den Papierkorb) – damit die Liste übersichtlich bleibt
        {
            auto spalte = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
            spalte->add(*abschnitt(TR("Zuletzt bearbeitet")));
            auto lb = Gtk::make_managed<Gtk::ListBox>();
            lb->set_activate_on_single_click(true);
            // Entf = «Aus der Liste entfernen» (die Datei bleibt; Löschen nur über das Menü mit Rückfrage)
            lb->signal_key_press_event().connect([lb](GdkEventKey *e) {
                if (e->keyval != GDK_KEY_Delete && e->keyval != GDK_KEY_KP_Delete) return false;
                auto row = lb->get_selected_row();
                if (!row) return false;
                try {
                    Gtk::RecentManager::get_default()->remove_item(Glib::filename_to_uri(row->get_name().raw()));
                } catch (...) {
                }
                lb->remove(*row);
                return true;
            }, false);
            auto leer_hinweis = label(TR("Noch keine Zeichnungen."), "hirch-dim");
            leer_hinweis->set_margin_top(12);
            leer_hinweis->set_margin_bottom(12);
            lb->set_placeholder(*leer_hinweis);
            leer_hinweis->show();
            for (auto const &z : zuletzt) {
                auto row = Gtk::make_managed<Gtk::ListBoxRow>();
                auto rb = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 1);
                rb->set_margin_start(8);
                rb->set_margin_end(8);
                rb->set_margin_top(5);
                rb->set_margin_bottom(5);
                auto n = label(Glib::path_get_basename(z.pfad), "hirch-titel");
                n->set_ellipsize(Pango::ELLIPSIZE_MIDDLE);
                n->set_max_width_chars(28);
                n->set_xalign(0);
                rb->add(*n);
                auto ordner = Glib::path_get_dirname(z.pfad);
                auto heim = Glib::get_home_dir();
                if (ordner.rfind(heim, 0) == 0) ordner = "~" + ordner.substr(heim.size());
                auto o = label(ordner, "hirch-dim");
                klasse(*o, "hirch-klein");
                o->set_ellipsize(Pango::ELLIPSIZE_START);
                o->set_max_width_chars(30);
                o->set_xalign(0);
                rb->add(*o);
                auto eb = Gtk::make_managed<Gtk::EventBox>();
                eb->add(*rb);
                row->add(*eb);
                row->set_name(z.pfad);
                row->set_tooltip_text(z.pfad + "\n" + TR("Rechtsklick: Ordner öffnen, aus der Liste entfernen, löschen · Entf: aus der Liste entfernen"));
                lb->add(*row);
                eb->signal_button_press_event().connect([lb, row, dlg_ = dlg.get()](GdkEventButton *e) {
                    if (e->type != GDK_BUTTON_PRESS || e->button != 3) return false;
                    lb->select_row(*row);
                    std::string pfad = row->get_name().raw();
                    auto menue = Gtk::make_managed<Gtk::Menu>();
                    auto eintrag = [menue](std::string const &text, std::function<void()> was) {
                        auto mi = Gtk::make_managed<Gtk::MenuItem>(text);
                        mi->signal_activate().connect([was] { Glib::signal_idle().connect_once(was); });
                        menue->append(*mi);
                    };
                    auto aus_liste = [lb, row, pfad] {
                        try {
                            Gtk::RecentManager::get_default()->remove_item(Glib::filename_to_uri(pfad));
                        } catch (...) {
                        }
                        lb->remove(*row);
                    };
                    eintrag(TR("Ordner öffnen"), [pfad] {
                        try {
                            ordner_zeigen(Glib::path_get_dirname(pfad));
                        } catch (...) {
                        }
                    });
                    menue->append(*Gtk::make_managed<Gtk::SeparatorMenuItem>());
                    eintrag(TR("Aus der Liste entfernen"), aus_liste);
                    eintrag(TR("Datei löschen …"), [dlg_, pfad, aus_liste] {
                        Gtk::MessageDialog m(*dlg_, TRF("«{}» löschen?", {Glib::path_get_basename(pfad)}), false,
                                             Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
                        m.set_title("Hirch CAM");
                        m.set_secondary_text(TR("Die Datei kommt in den Papierkorb."));
                        m.add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
                        klasse(*m.add_button(TR("Löschen"), Gtk::RESPONSE_OK), "destructive-action");
                        if (m.run() != Gtk::RESPONSE_OK) return;
                        m.hide();
                        try {
                            Gio::File::create_for_path(pfad)->trash();
                        } catch (Glib::Error const &f) {
                            Gtk::MessageDialog fehler(*dlg_, TR("Die Datei liess sich nicht löschen."), false,
                                                      Gtk::MESSAGE_ERROR, Gtk::BUTTONS_NONE, true);
                            fehler.set_title("Hirch CAM");
                            fehler.set_secondary_text(f.what().raw());
                            fehler.add_button(TR("Schliessen"), Gtk::RESPONSE_CLOSE);
                            fehler.run();
                            return;
                        }
                        aus_liste();
                    });
                    menue->show_all();
                    menue->attach_to_widget(*lb);
                    menue->popup_at_pointer(reinterpret_cast<GdkEvent *>(e));
                    return true;
                });
            }
            lb->signal_row_activated().connect([dlg_ = dlg.get(), gewaehlte_datei, OEFFNEN](Gtk::ListBoxRow *row) {
                if (!row) return;
                *gewaehlte_datei = row->get_name().raw();
                dlg_->response(OEFFNEN);
            });
            auto sw = Gtk::make_managed<Gtk::ScrolledWindow>();
            sw->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
            sw->set_min_content_height(240);
            sw->set_vexpand(true);
            sw->add(*lb);
            klasse(*sw, "hirch-karte");
            spalte->add(*sw);
            auto oeffnen = Gtk::make_managed<Gtk::Button>(TR("Datei öffnen …"));
            oeffnen->set_halign(Gtk::ALIGN_START);
            oeffnen->signal_clicked().connect([dlg_ = dlg.get(), gewaehlte_datei, OEFFNEN] {
                HirchDateiwahl dateiwahl(dlg_, TR("Zeichnung öffnen"), Gtk::FILE_CHOOSER_ACTION_OPEN, TR("Abbrechen"),
                                    TR("Öffnen"));
                auto &fc = dateiwahl.fc();
                auto zeichnungen = Gtk::FileFilter::create();
                zeichnungen->set_name(TR("Zeichnungen (.hir, .svg)"));
                for (char const *e : {"hir", "hirch", "svg", "svgz"}) {
                    zeichnungen->add_pattern(std::string("*.") + e);
                    zeichnungen->add_pattern("*." + Glib::ustring(e).uppercase().raw());
                }
                fc.add_filter(zeichnungen);
                auto zuletzt_ordner = Inkscape::Preferences::get()->getString("/hirch/oeffnen/ordner");
                if (!zuletzt_ordner.empty()) fc.set_current_folder(zuletzt_ordner);
                if (!dateiwahl.run()) return;
                auto pfad = fc.get_filename();
                dateiwahl.hide();
                if (pfad.empty()) return;
                Inkscape::Preferences::get()->setString("/hirch/oeffnen/ordner", Glib::path_get_dirname(pfad));
                *gewaehlte_datei = pfad;
                dlg_->response(OEFFNEN);
            });
            spalte->add(*oeffnen);
            auto h = label(TR("Klick öffnet die Datei statt der neuen Zeichnung."), "hirch-dim");
            klasse(*h, "hirch-klein");
            h->set_xalign(0);
            spalte->add(*h);
            spalten->add(*spalte);
            spalten->add(*Gtk::make_managed<Gtk::Separator>(Gtk::ORIENTATION_VERTICAL));
        }
    }
    auto links = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
    auto rechts = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
    spalten->add(*links);
    spalten->add(*rechts);

    // ---- 3D-Modell (nur beim Start; hirch-3d.cpp liest es nach «Los geht's» ein)
    auto modell_pfad = std::make_shared<std::string>();
    Gtk::Box *modell_box = nullptr;
    Gtk::Label *modell_name = nullptr, *modell_info = nullptr;
    Gtk::SpinButton *modell_rand = nullptr;
    Gtk::Button *modell_btn = nullptr, *testteil_btn = nullptr;
    auto modell_masse = std::make_shared<std::array<double, 4>>();   // b, h, d, bild (1/0)
    auto modell_gedreht = std::make_shared<bool>(false);
    if (beim_start) {
        links->add(*abschnitt(TR("3D-Modell (nicht nötig)")));
        auto mz = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        modell_btn = Gtk::make_managed<Gtk::Button>(TR("3D-Modell einlesen …"));
        mz->add(*modell_btn);
        if (!hirch_testteil().empty()) {
            testteil_btn = Gtk::make_managed<Gtk::Button>(TR("Testteil laden"));
            testteil_btn->set_tooltip_text(TR("Testteil für das Probefräsen in Hartschaum (180 × 180 × 39 mm, beide "
                                              "Seiten): Kuppel, Block, Treppe, Rinne, Taschen und Bohrungen."));
            mz->add(*testteil_btn);
        }
        modell_name = label(TR("STL, OBJ, STEP oder Graustufenbild – füllt das Material selbst aus."), "hirch-dim");
        modell_name->set_line_wrap(true);
        modell_name->set_max_width_chars(28);
        modell_name->set_xalign(0);
        klasse(*modell_name, "hirch-klein");
        mz->add(*modell_name);
        links->add(*mz);
        modell_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);
        auto gm = formular();
        modell_rand = spin_zeile(*gm, 0, TR("Rand ringsum"), 20, 0, 500, 1, 1, "mm");
        modell_box->add(*gm);
        modell_info = label("", "hirch-dim");
        modell_info->set_line_wrap(true);
        modell_info->set_max_width_chars(40);
        modell_info->set_xalign(0);
        klasse(*modell_info, "hirch-klein");
        modell_box->add(*modell_info);
        modell_box->set_no_show_all(true);
        links->add(*modell_box);
    }

    // ---- Werkstück
    links->add(*abschnitt(TR("Werkstück")));
    auto g = formular();
    auto sb = spin_zeile(*g, 0, TR("Breite (X)"), breite, 1, 5000, 1, 1, "mm");
    auto sh = spin_zeile(*g, 1, TR("Höhe (Y)"), hoehe, 1, 5000, 1, 1, "mm");
    auto sd = spin_zeile(*g, 2, TR("Dicke (Z)"), dicke, 0.1, 300, 0.5, 2, "mm");
    auto mwahl = Gtk::make_managed<Gtk::ComboBoxText>();
    if (auto l = liste_von(analyse, "materialien")) {
        for (auto const &v : *l) {
            if (auto a = v.if_array(); a && a->size() == 2) {
                mwahl->append(std::string((*a)[0].get_string()), std::string((*a)[1].get_string()));
            }
        }
    }
    mwahl->set_active_id(str(einst, "material", "hartholz"));
    g->attach(*label(TR("Werkstoff")), 0, 3);
    g->attach(*mwahl, 1, 3, 2);
    links->add(*g);
    auto minfo = label(TR("Der Werkstoff bestimmt Drehzahl und Vorschub der Werkzeuge mit automatischen "
                       "Schnittdaten."), "hirch-dim");
    minfo->set_line_wrap(true);
    minfo->set_max_width_chars(40);
    klasse(*minfo, "hirch-klein");
    links->add(*minfo);
    // rund (Zylinder stehend): Ø, Schraube in der Mitte, Kerbe; die XY-Wahl verschwindet (immer Mitte)
    auto xy_teile = std::make_shared<std::vector<Gtk::Widget *>>();
    auto np_wahl = std::make_shared<std::shared_ptr<NullpunktWahl>>();   // gesetzt beim Nullpunkt
    auto rundw = rund_abschnitt(*links, einst, sb, sh, [xy_teile, np_wahl](bool r) {
        for (auto w : *xy_teile) w->set_visible(!r);
        if (auto w = *np_wahl) {
            // rund: X0 Y0 immer in der Achse – im Block nur Mitte oben/unten
            w->rund = r;
            if (r) {
                w->xy = "mm";
                w->dx = w->dy = 0;
            }
            if (w->neu_zeichnen) w->neu_zeichnen();
        }
    });

    // ---- Höhen
    links->add(*abschnitt(TR("Höhen über dem Material")));
    auto g2 = formular();
    auto ss = spin_zeile(*g2, 0, TR("Sicherheitshöhe (Start/Ende)"), num(h, "sicherheitshoehe", 20), 1, 200, 1, 1, "mm");
    auto sf = spin_zeile(*g2, 1, TR("Freifahrhöhe (zwischen Wegen)"), num(h, "freifahrhoehe", 3), 0.5, 100, 0.5, 1, "mm");
    auto sa = spin_zeile(*g2, 2, TR("Eintauchen ab (über Material)"), num(h, "startabstand", 0.5), 0, 20, 0.1, 2, "mm");
    links->add(*g2);

    // ---- Nullpunkt am Block (hirch-3d.cpp): oben Z0 Materialoberfläche, unten Z0 Werktisch
    rechts->add(*abschnitt(TR("Nullpunkt")));
    auto hinweis = label(TR("Punkt anklicken – dort wird an der Maschine X0 Y0 Z0 gesetzt. Punkte oben: Z0 auf der "
                            "Materialoberfläche, Punkte unten: Z0 auf dem Werktisch."), "hirch-dim");
    klasse(*hinweis, "hirch-klein");
    hinweis->set_line_wrap(true);
    hinweis->set_max_width_chars(48);
    hinweis->set_xalign(0);
    rechts->add(*hinweis);
    auto wahl = std::make_shared<NullpunktWahl>();
    wahl->xy = str(np, "xy", "ul");
    if (wahl->xy.size() != 2) wahl->xy = "ul";
    wahl->unten = str(np, "z", "oben") == "unten";
    wahl->b = breite;
    wahl->h = hoehe;
    wahl->d = dicke;
    wahl->dx = num(np, "dx", 0);
    wahl->dy = num(np, "dy", 0);
    *np_wahl = wahl;
    if (rundw->ist_rund()) {
        wahl->rund = true;
        wahl->xy = "mm";
        wahl->dx = wahl->dy = 0;
    }
    rechts->add(*nullpunkt_block(wahl));
    auto g3 = formular();
    auto vx = spin_zeile(*g3, 0, TR("Versatz X"), num(np, "dx", 0), -5000, 5000, 1, 2, "mm");
    auto vy = spin_zeile(*g3, 1, TR("Versatz Y"), num(np, "dy", 0), -5000, 5000, 1, 2, "mm");
    rechts->add(*g3);
    *xy_teile = {g3};
    auto vinfo = label(TR("Versatz: Nullpunkt frei verschieben, gemessen vom gewählten Punkt "
                       "(z. B. Anschlag 10 mm neben der Ecke)."), "hirch-dim");
    vinfo->set_line_wrap(true);
    vinfo->set_max_width_chars(44);
    klasse(*vinfo, "hirch-klein");
    rechts->add(*vinfo);
    xy_teile->push_back(vinfo);
    // Spannvorrichtung an: der Nullpunkt sitzt auf der Vorrichtung (Peter, 21.10.: «0-Punkt verschoben»)
    auto np_vorr = label(TR("Mit Spannvorrichtung: derselbe Nullpunkt am Werkstück. Auf der Seite der Vorrichtung "
                            "zeigt das Kreuz diesen Punkt – dort antasten, Vorrichtung fräsen, Werkstück einlegen und "
                            "ohne neu anzutasten weiterfräsen."), "hirch-dim");
    np_vorr->set_line_wrap(true);
    np_vorr->set_max_width_chars(44);
    np_vorr->set_xalign(0);
    klasse(*np_vorr, "hirch-klein");
    np_vorr->set_no_show_all(true);
    rechts->add(*np_vorr);

    // ---- Ausgabe
    rechts->add(*abschnitt(TR("Ausgabe")));
    auto steuer = Gtk::make_managed<Gtk::ComboBoxText>();
    if (auto p = liste_von(analyse, "postprozessoren")) {
        for (auto const &e : *p) {
            auto const *paar = e.if_array();
            if (paar && paar->size() >= 2 && (*paar)[0].is_string() && (*paar)[1].is_string()) {
                steuer->append(std::string((*paar)[0].get_string()), std::string((*paar)[1].get_string()));
            }
        }
    }
    steuer->set_active_id(str(einst, "postprozessor", "richauto_a11"));
    steuer->set_hexpand(true);
    // Die Steuerung wird nur noch in «Einstellungen › Maschine» gewählt (Peter, 19.10.); die Liste bleibt
    // unsichtbar für die Frage, ob sie Bögen kann
    steuer->set_no_show_all(true);
    auto steuer_zeile = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
    steuer_zeile->add(*steuer);
    auto steuer_bea = Gtk::make_managed<Gtk::Button>(TR("Postprozessoren …"));
    steuer_bea->set_tooltip_text(TR("Postprozessoren anlegen, ändern oder entfernen – z. B. wo und ob das "
                                 "Werkzeug gewechselt wird"));
    steuer_zeile->add(*steuer_bea);
    rechts->add(*steuer_zeile);
    auto einricht_offen = std::make_shared<bool>(true);
    steuer_bea->signal_clicked().connect([this, steuer, einricht_offen] {
        postprozessoren_dialog(steuer->get_active_id().raw(),
                               [steuer, einricht_offen](std::vector<std::pair<std::string, std::string>> const &l) {
                                   if (!*einricht_offen) return;
                                   auto aktiv = steuer->get_active_id();
                                   steuer->remove_all();
                                   for (auto const &[k, n] : l) steuer->append(k, n);
                                   steuer->set_active_id(aktiv);
                                   if (steuer->get_active_row_number() < 0 && !l.empty()) steuer->set_active(0);
                               });
    });
    auto boegen = Gtk::make_managed<Gtk::CheckButton>(TR("Rundungen als Bögen ausgeben (G2/G3)"));
    boegen->set_active(flag(einst, "boegen", false));
    boegen->set_tooltip_text(TR("Kreise und Rundungen als echte Bögen statt vieler kurzer Geraden: viel kürzere "
                             "Dateien und glattere Rundungen. Vorher einmal mit «boegen_lufttest.tap» prüfen, "
                             "ob die Steuerung G2/G3 richtig fährt."));
    rechts->add(*boegen);
    // Kann die gewählte Steuerung keine Bögen, gibt es die Wahl nicht (ausblenden statt ausgrauen)
    auto ohne_boegen = label(TR("Diese Steuerung kann keine Bögen – Rundungen werden als kurze Geraden geschrieben."),
                             "hirch-dim");
    ohne_boegen->set_line_wrap(true);
    ohne_boegen->set_max_width_chars(44);
    klasse(*ohne_boegen, "hirch-klein");
    ohne_boegen->set_no_show_all(true);
    rechts->add(*ohne_boegen);
    auto kann = std::make_shared<std::map<std::string, bool>>();
    if (auto p = liste_von(analyse, "postprozessoren")) {
        for (auto const &e : *p) {
            auto const *a = e.if_array();
            if (a && a->size() >= 3 && (*a)[0].is_string() && (*a)[2].is_object())
                (*kann)[std::string((*a)[0].get_string())] = flag((*a)[2].as_object(), "boegen", true);
        }
    }
    auto boegen_pruefen = [steuer, boegen, ohne_boegen, kann] {
        auto it = kann->find(steuer->get_active_id().raw());
        bool k = it == kann->end() || it->second;
        boegen->set_no_show_all(!k);
        if (k) boegen->show(); else boegen->hide();
        if (k) ohne_boegen->hide(); else ohne_boegen->show();
    };
    steuer->signal_changed().connect(boegen_pruefen);
    boegen_pruefen();

    // ---- Zweiseitig
    bj::object zw;
    if (auto z = unter(einst, "zweiseitig")) zw = *z;
    rechts->add(*abschnitt(TR("Zweiseitig")));
    auto z_an = Gtk::make_managed<Gtk::CheckButton>(TR("Werkstück von beiden Seiten fräsen"));
    z_an->set_active(flag(zw, "an", false) || einrichten_vorwahl == "zweiseitig");
    z_an->set_tooltip_text(TR("Seite 2 ist die zweite Seite der Zeichnung (wird angelegt). Dort so zeichnen, "
                           "wie man das Werkstück nach dem Wenden von oben sieht. Je Seite eine Datei."));
    rechts->add(*z_an);
    auto zwg = formular();
    zwg->set_margin_start(24);
    auto achse = Gtk::make_managed<Gtk::ComboBoxText>();
    achse->append("x", TR("um die X-Achse (oben ↔ unten)"));
    achse->append("y", TR("um die Y-Achse (links ↔ rechts)"));
    achse->set_active_id(str(zw, "achse", "x") == "y" ? "y" : "x");
    zwg->attach(*label(TR("Wenden")), 0, 0);
    zwg->attach(*achse, 1, 0, 2);
    auto pl_an = Gtk::make_managed<Gtk::CheckButton>(TR("Passlöcher bohren (für Passstifte)"));
    pl_an->set_active(flag(zw, "passloecher", true));
    pl_an->set_tooltip_text(TR("Löcher durch das Werkstück bis ins Opferbrett (auf der Wendeachse oder in den Ecken). "
                            "Nach dem Wenden Stifte hineinstecken – das Werkstück liegt wieder genau."));
    zwg->attach(*pl_an, 0, 1, 3);
    auto pl_d = spin_zeile(*zwg, 2, TR("Ø Passloch"), num(zw, "pass_d", 6), 2, 30, 0.5, 1, "mm");
    auto pl_rand = spin_zeile(*zwg, 3, TR("Abstand vom Rand"), num(zw, "pass_rand", 15), 2, 500, 1, 1, "mm");
    auto pl_extra = spin_zeile(*zwg, 4, TR("Tiefer als das Material"), num(zw, "pass_extra", 3), 0, 20, 0.5, 1, "mm");
    auto pl_wz = Gtk::make_managed<Gtk::ComboBoxText>();
    pl_wz->set_tooltip_text(TR("Alle Werkzeuge: die dieser Zeichnung, «Meine Werkzeuge» und ein neues. Ein Bohrer "
                            "bohrt die Löcher, ein Fräser fräst sie als Helix."));
    wz_kontext.clear();
    // ohne eigene Wahl: der Vorschlag des Rechenkerns (Bohrer im Ø des Lochs, sonst ein passender Fräser)
    werkzeug_combo_alle(*pl_wz, str(zw, "pass_werkzeug").empty() ? str(analyse, "pass_werkzeug", "C")
                                                                 : str(zw, "pass_werkzeug"));
    zwg->attach(*label(TR("Werkzeug")), 0, 5);
    zwg->attach(*pl_wz, 1, 5, 2);
    // Lage: zwei Löcher auf der Wendeachse oder vier in den Ecken (spiegelbildlich, Peter 20.10.)
    auto pl_lage = Gtk::make_managed<Gtk::ComboBoxText>();
    pl_lage->append("achse", TR("zwei auf der Wendeachse"));
    pl_lage->append("ecken", TR("vier in den Ecken"));
    pl_lage->append("frei", TR("frei (in der Zeichnung gezogen)"));
    {
        auto l = str(zw, "pass_lage", "achse");
        pl_lage->set_active_id(l == "ecken" || l == "frei" ? l : "achse");
    }
    pl_lage->set_tooltip_text(TR("In den Ecken: je zwei Löcher liegen spiegelbildlich zur Wendeachse – nach dem Wenden "
                                 "trifft jedes Loch wieder einen Stift. Der Abstand vom Rand gilt in beide Richtungen. "
                                 "Frei: die Passlöcher in der Zeichnung (Ebene «Passlöcher») mit der Maus ziehen – "
                                 "der Partner folgt gespiegelt. Ziehen schaltet von selbst auf «frei»."));
    zwg->attach(*label(TR("Lage")), 0, 6);
    zwg->attach(*pl_lage, 1, 6, 2);
    // «+ Lochpaar»: ein weiteres Paar (frei), erscheint in der Zeichnung und lässt sich dort ziehen
    auto pl_mehr = std::make_shared<int>(0);
    auto pl_plus = Gtk::make_managed<Gtk::Button>(TR("+ Lochpaar"));
    pl_plus->set_tooltip_text(TR("Ein weiteres Lochpaar anlegen – danach in der Zeichnung an die gewünschte Stelle ziehen."));
    pl_plus->set_halign(Gtk::ALIGN_START);
    auto pl_plus_info = label("", "hirch-dim");
    klasse(*pl_plus_info, "hirch-klein");
    pl_plus->signal_clicked().connect([pl_mehr, pl_lage, pl_plus_info] {
        ++*pl_mehr;
        pl_lage->set_active_id("frei");
        pl_plus_info->set_text(*pl_mehr == 1 ? std::string(TR("1 Lochpaar kommt dazu"))
                                             : std::to_string(*pl_mehr) + TR(" Lochpaare kommen dazu"));
    });
    zwg->attach(*pl_plus, 1, 7);
    zwg->attach(*pl_plus_info, 2, 7);
    // «Neues Werkzeug …»: Werkzeugdialog mit einem neuen Bohrer (Ø des Passlochs), danach gewählt
    auto pl_alt = std::make_shared<std::string>(pl_wz->get_active_id().raw());
    pl_wz->signal_changed().connect([this, pl_wz, pl_alt, pl_d] {
        auto id = pl_wz->get_active_id().raw();
        if (id != "neu") {
            if (!id.empty()) *pl_alt = id;
            return;
        }
        Glib::signal_idle().connect_once([this, pl_wz, pl_alt, pl_d] {
            std::set<std::string> vorher;
            if (auto ws = unter(einst, "werkzeuge")) for (auto const &kv : *ws) vorher.insert(std::string(kv.key()));
            neu_durchmesser = pl_d->get_value();       // gleich mit dem Ø des Passlochs
            werkzeuge_dialog({}, TR("Bohrer"));
            std::string neu_key;
            if (auto ws = unter(einst, "werkzeuge")) {
                for (auto const &kv : *ws) {
                    if (!vorher.count(std::string(kv.key()))) neu_key = std::string(kv.key());
                }
            }
            werkzeug_combo_alle(*pl_wz, neu_key.empty() ? *pl_alt : neu_key);
        });
    });
    rechts->add(*zwg);
    zwg->set_no_show_all(true);
    auto z_abh = [=] {
        zwg->set_no_show_all(!z_an->get_active());
        if (z_an->get_active()) zwg->show_all();
        else zwg->hide();
        for (auto w_ : {(Gtk::Widget *)pl_d, (Gtk::Widget *)pl_rand, (Gtk::Widget *)pl_extra, (Gtk::Widget *)pl_wz,
                        (Gtk::Widget *)pl_lage, (Gtk::Widget *)pl_plus})
            w_->set_sensitive(pl_an->get_active());
    };
    z_an->signal_toggled().connect(z_abh);
    pl_an->signal_toggled().connect(z_abh);
    z_abh();

    // ---- Spannvorrichtung
    bj::object vo;
    if (auto v = unter(einst, "vorrichtung")) vo = *v;
    rechts->add(*abschnitt(TR("Spannvorrichtung")));
    auto v_an = Gtk::make_managed<Gtk::CheckButton>(TR("Zuerst eine Spannvorrichtung fräsen (eigene Seite)"));
    v_an->set_active(flag(vo, "an", false) || einrichten_vorwahl == "vorrichtung");
    einrichten_vorwahl.clear();
    v_an->set_tooltip_text(TR("Eine Seite «Vorrichtung» links vor Seite 1: dort die Aufnahme zeichnen (das Werkstück "
                           "liegt durchscheinend darüber). Sie wird zuerst aus eigenem Material gefräst, danach "
                           "das Werkstück mit demselben XY-Nullpunkt."));
    rechts->add(*v_an);
    auto vg = formular();
    vg->set_margin_start(24);
    double vlx = 50, vly = 50;
    if (auto l = liste_von(vo, "lage"); l && l->size() >= 2) {
        vlx = (*l)[0].to_number<double>();
        vly = (*l)[1].to_number<double>();
    }
    auto v_d = spin_zeile(*vg, 0, TR("Dicke der Vorrichtung"), num(vo, "dicke", 19), 1, 300, 0.5, 1, "mm");
    // Peter, 23.10.: wie viel grösser als das Werkstück (je Seite) – wieder einstellbar
    double rand_alt = num(vo, "rand", -1);
    if (rand_alt < 0) {
        rand_alt = 50;                         // bisher fest: Werkstück + 100 mm
        if (auto vs = d ? vorrichtung_seite(d) : nullptr; vs && breite > 0) {
            double uu_px = d->getDocumentScale()[Geom::X];
            double k = MM_PX / (uu_px > 0 ? uu_px : MM_PX);       // Benutzereinheiten je mm
            rand_alt = std::max(0.0, (vs->getRect().width() / k - breite) / 2);
        }
    }
    auto v_rand = spin_zeile(*vg, 5, TR("Rand um das Werkstück"), rand_alt, 0, 1000, 5, 1, TR("mm je Seite"));
    v_rand->set_tooltip_text(TR("So viel grösser als das Werkstück wird die Vorrichtung auf jeder Seite."));
    // ein Nullpunkt am Werkstück (Peter, 21.10.): ab Werk liegt das Werkstück mittig in der Vorrichtung
    auto v_mitte = Gtk::make_managed<Gtk::CheckButton>(TR("Werkstück mittig in der Vorrichtung"));
    {
        bool m = true;
        if (auto av = unter(analyse, "vorrichtung"); av && av->if_contains("mittig")) m = flag(*av, "mittig", true);
        else if (vo.if_contains("mittig")) m = flag(vo, "mittig", true);
        else m = std::abs(vlx - 50) < 1e-9 && std::abs(vly - 50) < 1e-9;
        v_mitte->set_active(m);
    }
    vg->attach(*v_mitte, 0, 1, 3);
    auto v_x = spin_zeile(*vg, 2, TR("Werkstück liegt bei X"), vlx, -2000, 5000, 1, 1, "mm");
    auto v_y = spin_zeile(*vg, 3, TR("Werkstück liegt bei Y"), vly, -2000, 5000, 1, 1, "mm");
    auto v_vh = Gtk::make_managed<Gtk::CheckButton>(TR("Vorrichtung schon vorhanden – nicht mehr ausgeben"));
    v_vh->set_active(flag(vo, "vorhanden", false));
    vg->attach(*v_vh, 0, 4, 3);
    rechts->add(*vg);
    vg->set_no_show_all(true);
    auto v_abh = [=] {
        vg->set_no_show_all(!v_an->get_active());
        if (v_an->get_active()) vg->show_all();
        else vg->hide();
        np_vorr->set_visible(v_an->get_active());
        if (v_an->get_active()) {
            zeile_zeigen(v_x, !v_mitte->get_active());
            zeile_zeigen(v_y, !v_mitte->get_active());
        }
    };
    v_an->signal_toggled().connect(v_abh);
    v_mitte->signal_toggled().connect(v_abh);
    v_abh();

    // ---- Umspannen (Peter 27.10., Entwurf «Hirch CNC – Umspannen» A): ein Häkchen für die ganze Zeichnung,
    // ob eine Seite zwei Programme braucht, entscheidet der Rechenteil je Seite
    auto ums = umspannen_abschnitt(*rechts, einst, d);


    auto modell_lage = [=] {
        if ((*modell_masse)[0] <= 0) return;
        double mb = (*modell_masse)[0], mh = (*modell_masse)[1];
        if (*modell_gedreht) std::swap(mb, mh);
        wahl->modell = {(wert(sb) - mb) / 2, (wert(sh) - mh) / 2, mb, mh};
    };
    for (auto s_ : {sb, sh, sd, vx, vy}) {
        s_->signal_value_changed().connect([=] {
            wahl->b = wert(sb);
            wahl->h = wert(sh);
            wahl->d = wert(sd);
            wahl->dx = wert(vx);
            wahl->dy = wert(vy);
            modell_lage();
            if (wahl->neu_zeichnen) wahl->neu_zeichnen();
        });
    }
    // 3D-Modell wählen: Material aus den Massen + Rand (in X zu knapp für die Maschine: um 90°
    // gedreht), «Zweiseitig» bei Netzen an
    auto modell_rechnen = [=] {
        if (modell_pfad->empty() || (*modell_masse)[0] <= 0) return;
        double r = wert(modell_rand);
        double mb = (*modell_masse)[0], mh = (*modell_masse)[1], md = (*modell_masse)[2];
        bool bild_ = (*modell_masse)[3] > 0;
        double mx = 600, my = 900;
        if (auto ma = unter(analyse, "maschine")) {
            mx = num(*ma, "x", mx);
            my = num(*ma, "y", my);
        }
        bool rund_ = rundw->ist_rund();
        bool drehen = !rund_ && mb + 2 * r > mx && mh + 2 * r <= mx && mb + 2 * r <= my;
        if (drehen) std::swap(mb, mh);
        *modell_gedreht = drehen;
        if (rund_) {
            // rund: das Modell (mittig) muss mit dem Rand in den Kreis passen
            rundw->durchmesser->set_value(std::ceil((std::hypot(mb, mh) + 2 * r) * 2) / 2);
        } else {
            sb->set_value(std::ceil((mb + 2 * r) * 2) / 2);
            sh->set_value(std::ceil((mh + 2 * r) * 2) / 2);
        }
        if (!bild_) sd->set_value(std::ceil((md + 2 * std::min(r, 5.0)) * 2) / 2);
        else if (wert(sd) < md + 1) sd->set_value(std::ceil((md + 3) * 2) / 2);
        z_an->set_active(!bild_);
        std::string t = TR("Material und Dicke aus dem Modell übernommen");
        if (!bild_) t += std::string(", ") + TR("«Zweiseitig» eingeschaltet");
        if (drehen) t += std::string(" · ") + TR("um 90° gedreht, weil es in X sonst nicht auf die Maschine passt");
        modell_info->set_text(t + ".");
        modell_lage();
        if (wahl->neu_zeichnen) wahl->neu_zeichnen();
    };
    std::function<void(std::string const &)> waehlen_fertig = [=](std::string const &pfad) {
        modell_btn->set_sensitive(false);
        modell_name->set_text(TR("Wird gelesen …"));
        hirch_3d_masse(pfad, [=](bool ok, double b_, double h_, double d_, bool bild_, std::string const &f) {
            if (!*einricht_offen) return;            // Dialog schon zu
            modell_btn->set_sensitive(true);
            if (!ok) {
                modell_name->set_text(f.empty() ? TR("Die Datei liess sich nicht lesen.") : f);
                return;
            }
            *modell_pfad = pfad;
            *modell_masse = {b_, h_, d_, bild_ ? 1.0 : 0.0};
            if (pfad == hirch_testteil()) mwahl->set_active_id("hartschaum");   // Testteil: Hartschaum
            modell_btn->set_label(TR("Anderes …"));
            modell_name->set_text(Glib::path_get_basename(pfad) + "\n" + zahl(b_) + " × " + zahl(h_) + " × " +
                                  zahl(d_, 2) + " mm");
            modell_box->set_no_show_all(false);
            modell_box->show_all();
            modell_rechnen();
        });
    };
    if (modell_btn) {
        modell_rand->signal_value_changed().connect([=] {
            if ((*modell_masse)[0] > 0) modell_rechnen();
        });
        rundw->rund->signal_toggled().connect([=] {
            if ((*modell_masse)[0] > 0) modell_rechnen();
        });
        modell_btn->signal_clicked().connect([=, dlg_ = dlg.get()] {
            // Oberflächentest: Datei ohne Auswahlfenster
            if (auto t = g_getenv("HIRCH_TEST_START_MODELL")) {
                waehlen_fertig(t);
                return;
            }
            HirchDateiwahl dateiwahl(dlg_, TR("3D-Modell einlesen"), Gtk::FILE_CHOOSER_ACTION_OPEN, TR("Abbrechen"),
                                TR("Öffnen"));
            auto &fc = dateiwahl.fc();
            auto alle = Gtk::FileFilter::create();
            alle->set_name(TR("3D-Modelle und Graustufenbilder"));
            for (char const *e : {"stl", "obj", "step", "stp", "png", "jpg", "jpeg", "tif", "tiff", "bmp"}) {
                alle->add_pattern(std::string("*.") + e);
                alle->add_pattern("*." + Glib::ustring(e).uppercase().raw());
            }
            fc.add_filter(alle);
            auto zuletzt = Inkscape::Preferences::get()->getString("/hirch/3d/ordner");
            if (!zuletzt.empty()) fc.set_current_folder(zuletzt);
            if (!dateiwahl.run()) return;
            auto pfad = fc.get_filename();
            dateiwahl.hide();
            Inkscape::Preferences::get()->setString("/hirch/3d/ordner", Glib::path_get_dirname(pfad));
            waehlen_fertig(pfad);
        });
        if (testteil_btn) testteil_btn->signal_clicked().connect([=] { waehlen_fertig(hirch_testteil()); });
        if (!modell.empty()) Glib::signal_idle().connect_once([=] { if (*einricht_offen) waehlen_fertig(modell); });
    }

    dlg->add_button(beim_start ? TR("Später") : TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
    klasse(*dlg->add_button(beim_start ? TR("Los geht's") : TR("Übernehmen"), Gtk::RESPONSE_OK), "suggested-action");
    dlg->set_default_response(Gtk::RESPONSE_OK);
    dlg->show_all();
    int einricht_antwort = dlg->run();
    *einricht_offen = false;
    if (einricht_antwort == OEFFNEN && !gewaehlte_datei->empty()) {
        dlg->hide();
        std::string pfad = *gewaehlte_datei;
        Glib::signal_idle().connect_once([pfad] {
            auto app = InkscapeApplication::instance();
            if (!app) return;
            auto datei = Gio::File::create_for_path(pfad);
            bool abgebrochen = false;
            if (auto d = app->document_open(datei, &abgebrochen)) {
                app->create_window(d, true);   // ersetzt die leere neue Zeichnung
                Gtk::RecentManager::get_default()->add_item(datei->get_uri());
            }
        });
        return;
    }
    if (einricht_antwort != Gtk::RESPONSE_OK) return;

    bj::object np_neu;
    np_neu["xy"] = wahl->xy;
    np_neu["dx"] = wert(vx);
    np_neu["dy"] = wert(vy);
    np_neu["z"] = wahl->unten ? "unten" : "oben";
    if (rundw->ist_rund()) {
        np_neu["xy"] = "mm";              // rund: X0 Y0 in der Achse
        np_neu["dx"] = 0.0;
        np_neu["dy"] = 0.0;
    }
    einst["nullpunkt"] = np_neu;
    rundw->speichern(einst);
    einst["eingerichtet"] = true;
    einst["boegen"] = boegen->get_active();
    einst["reihenfolge"] = "liste";
    zw["an"] = z_an->get_active();
    zw["achse"] = achse->get_active_id().raw();
    zw["passloecher"] = pl_an->get_active();
    zw["pass_d"] = wert(pl_d);
    zw["pass_rand"] = wert(pl_rand);
    zw["pass_extra"] = wert(pl_extra);
    zw["pass_lage"] = pl_lage->get_active_id().raw();
    if (*pl_mehr > 0) {
        // weitere Paare: zu den bisherigen Löchern (so wie sie jetzt liegen) dazu
        // Grundlage: die Löcher, wie sie bisher lagen (gespeicherte Lage, neue Masse)
        bj::object alt_z;
        if (auto z0 = unter(einst, "zweiseitig")) alt_z = *z0;
        bj::object tmp = zw;
        tmp["pass_lage"] = str(alt_z, "pass_lage", "achse");
        double B = wert(sb), H = wert(sh), rd = wert(pl_rand), dd = wert(pl_d);
        bool um_y = str(zw, "achse", "x") == "y";
        einst["zweiseitig"] = tmp;
        auto prim = pass_primaer(B, H);
        einst["zweiseitig"] = alt_z;
        zw["pass_lage"] = "frei";
        for (int k = 0; k < *pl_mehr; ++k) {
            Geom::Point p = um_y ? Geom::Point(rd, H / 2) : Geom::Point(B / 2, rd);
            for (int schritt = 0; schritt < 200; ++schritt) {
                bool frei_ = std::none_of(prim.begin(), prim.end(),
                                          [&](Geom::Point q) { return Geom::distance(p, q) < dd + 6; });
                if (frei_) break;
                if (um_y) p[Geom::Y] += dd + 6;
                else p[Geom::X] += dd + 6;
            }
            prim.push_back(p);
        }
        bj::array pp;
        for (auto const &p : prim) {
            bj::array a_;
            a_.push_back(std::round(p.x() * 100) / 100);
            a_.push_back(std::round(p.y() * 100) / 100);
            pp.push_back(a_);
        }
        zw["pass_punkte"] = pp;
    }
    if (auto pw = werkzeug_platz(pl_wz->get_active_id().raw()); !pw.empty() && pw != "neu") zw["pass_werkzeug"] = pw;
    bool zwei_neu = z_an->get_active() && !zweiseitig();
    // Zweiseitig aus: auch die 3D-Modelle nur noch von oben (sonst stünde im Reiter 3D weiter «beide Seiten»)
    if (!z_an->get_active() && zweiseitig()) reliefs_einseitig(d);
    einst["zweiseitig"] = zw;
    vo["an"] = v_an->get_active();
    vo["dicke"] = wert(v_d);
    bj::array vl;
    vl.push_back(wert(v_x));
    vl.push_back(wert(v_y));
    vo["lage"] = vl;
    vo["mittig"] = v_mitte->get_active();
    vo["vorhanden"] = v_vh->get_active();
    vo["rand"] = wert(v_rand);
    einst["vorrichtung"] = vo;
    ums->speichern(einst);
    auto root = d->getReprRoot();
    root->setAttribute("hirch:materialdicke", attr_zahl(wert(sd)));
    // immer abgleichen: richtet auch Zeichnungen, deren Seite nicht zum Material passt
    material_groesse(d, wert(sb), wert(sh));
    if (z_an->get_active()) seite2_anlegen(d);
    if (v_an->get_active()) {
        vorrichtung_anlegen(d, wert(sb), wert(sh), wert(v_rand));
    } else if (auto vs = vorrichtung_seite(d)) {
        // Vorrichtung ausgeschaltet: ihre Seite entfernen, die Zeichnung darauf bleibt liegen (Peter, 23.10.)
        d->getPageManager().deletePage(vs, false);
        d->ensureUpToDate();
    }
    seite_zeigen(150);
    if (std::abs(wert(sb) - breite) > 1e-6 || std::abs(wert(sh) - hoehe) > 1e-6) {
        using Util::Quantity;
        // Inkscape hält beim Vergrössern die obere Kante fest. Der Nullpunkt
        // der Maschine ist aber unten links: alles um die Höhenänderung nach
        // unten schieben, damit die Teile ihre Maschinen-Koordinaten behalten.
        double dh = Quantity::convert(wert(sh) - hoehe, "mm", "px");
        if (std::abs(dh) > 1e-9) {
            for (auto &kind : d->getRoot()->children) {
                auto item = cast<SPItem>(&kind);
                auto eltern = cast<SPItem>(kind.parent);
                if (!item || !eltern) continue;
                item->set_item_transform(item->i2doc_affine() * Geom::Translate(0, dh) *
                                         eltern->i2doc_affine().inverse());
                item->doWriteTransform(item->transform, nullptr, false);
            }
        }
        seite_zeigen(150);
    }
    h["sicherheitshoehe"] = wert(ss);
    h["freifahrhoehe"] = wert(sf);
    h["startabstand"] = wert(sa);
    einst["hoehen"] = h;
    if (!mwahl->get_active_id().empty()) einst["material"] = mwahl->get_active_id().raw();
    schreiben(TR("Auftrag einrichten"));
    nullpunkt_zeichnen();
    if (zwei_neu) Glib::signal_idle().connect_once([this] { seite_waehlen(1, false); });
    // Neue Zeichnung eingerichtet, noch nichts gezeichnet: nicht als geändert zählen (Windows-Nacht a349d01: Strg+W
    // fragte nach dem Speichern einer leeren Zeichnung). Das 3D-Modell unten ist dagegen eine Änderung.
    if (beim_start && modell_pfad->empty()) {
        Glib::signal_timeout().connect_once([this] {
            if (auto d = doc()) d->setModifiedSinceSave(false);
        }, 300);
    }
    // 3D-Modell aus dem Anfangsdialog: jetzt einlesen (Reiter «3D»), zweiseitig wie gewählt
    if (!modell_pfad->empty()) {
        std::string pfad = *modell_pfad;
        bool zwei = z_an->get_active() && (*modell_masse)[3] <= 0;
        Glib::signal_idle().connect_once([this, pfad, zwei] {
            auto desk = panel.getDesktop();
            auto c = desk ? desk->getContainer() : nullptr;
            if (!c) return;
            c->new_dialog("Hirch3D");
            if (auto d3 = dynamic_cast<Hirch3D *>(c->get_dialog("Hirch3D"))) d3->einlesenDatei(pfad, zwei);
        });
    }
}

} // namespace Inkscape::UI::Dialog
