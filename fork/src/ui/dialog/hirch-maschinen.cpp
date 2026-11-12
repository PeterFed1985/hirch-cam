// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CAM: mehrere Maschinen (B-25, Konzept von Peter am 06.11.2026 freigegeben).
 * Oben in «Einstellungen › Maschine» je Maschine eine Karte: Name, Tisch, Spindel, Anzahl Fräser; die Maschine dieser
 * Zeichnung ist markiert. Ein Klick wählt die Maschine für die Zeichnung. Daten: hirch_cam/maschinen.py.
 */
#include "ui/dialog/hirch-intern.h"

#include <gtkmm/entry.h>
#include <gtkmm/radiobutton.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/modelbutton.h>
#include <gtkmm/popover.h>
#include <gtkmm/stack.h>
#include <glib/gstdio.h>

namespace Inkscape::UI::Dialog {

std::string HirchFraesen::Impl::maschine_name_fragen(std::string const &titel, std::string const &vorgabe,
                                                     std::string const &frage)
{
    auto dlg = dialog(titel);
    auto e = Gtk::make_managed<Gtk::Entry>();
    e->set_text(vorgabe);
    e->set_activates_default(true);
    e->set_width_chars(30);
    dlg->get_content_area()->add(*label(frage.empty() ? std::string(TR("Name der Maschine")) : frage));
    dlg->get_content_area()->add(*e);
    dlg->add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
    auto ok = dlg->add_button(TR("OK"), Gtk::RESPONSE_OK);
    klasse(*ok, "suggested-action");
    dlg->set_default_response(Gtk::RESPONSE_OK);
    dlg->show_all();
    auto antwort = dlg->run();
    std::string t = e->get_text().raw();
    return antwort == Gtk::RESPONSE_OK ? t : std::string();
}

void HirchFraesen::Impl::maschinen_karten(Gtk::Box *ziel)
{
    // Karten aus der zuletzt bekannten Liste zeigen, danach mit frischer Liste neu
    auto bauen = std::make_shared<std::function<void()>>();
    // der Abschnitt wird nach jeder Analyse neu gebaut: späte Antworten dürfen nicht in ein gelöschtes Feld schreiben
    auto lebt = std::make_shared<GtkWidget *>(GTK_WIDGET(ziel->gobj()));
    g_object_add_weak_pointer(G_OBJECT(*lebt), (gpointer *)lebt.get());
    *bauen = [this, ziel, bauen, lebt]() {
        if (!*lebt) return;
        UI::remove_all_children(*ziel);
        auto const &liste = hirch_maschinen();
        std::string mein = str(einst, "maschine_id");
        std::string aktiv;                        // die Maschine, die für diese Zeichnung gilt
        for (auto const &v : liste)
            if (auto o = v.if_object(); o && flag(*o, "aktiv", false)) aktiv = str(*o, "id");
        if (aktiv.empty() && !mein.empty()) aktiv = mein;
        auto hinweis = label(liste.size() > 1
                                 ? TR("Die Zeichnung merkt sich ihre Maschine. Jede Maschine hat ihre eigenen Fräser. "
                                      "Ein Klick auf eine Karte wählt die Maschine für diese Zeichnung.")
                                 : TR("Jede Maschine hat ihre eigenen Fräser. Eine zweite Maschine legst du als Kopie "
                                      "dieser an."),
                             "hirch-dim");
        klasse(*hinweis, "hirch-klein");
        hinweis->set_line_wrap(true);
        hinweis->set_max_width_chars(44);
        hinweis->set_xalign(0);
        ziel->add(*hinweis);
        for (auto const &v : liste) {
            auto o = v.if_object();
            if (!o) continue;
            std::string id = str(*o, "id");
            auto knopf = Gtk::make_managed<Gtk::Button>();
            knopf->set_relief(Gtk::RELIEF_NONE);
            klasse(*knopf, "hirch-wz-karte");
            klasse(*knopf, "hirch-wz-gewaehlt", id == aktiv);
            auto k = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);
            auto kopf = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
            auto n = label("");
            n->set_markup("<b>" + markup_escape(str(*o, "name")) + "</b>");
            n->set_hexpand(true);
            kopf->add(*n);
            if (id == aktiv) {
                auto b = label(TR("in dieser Zeichnung"), "hirch-badge");
                kopf->add(*b);
            }
            k->add(*kopf);
            auto werte = label(TRF("Tisch {} × {} mm · Spindel ≤ {} U/min · {} Fräser",
                                   {zahl(num(*o, "x", 0), hirch_zoll() ? 1 : 0), zahl(num(*o, "y", 0), hirch_zoll() ? 1 : 0),
                                    zahl(num(*o, "drehzahl_max", 0), 0), zahl(num(*o, "fraeser", 0), 0)}),
                               "hirch-dim");
            klasse(*werte, "hirch-klein");
            werte->set_xalign(0);
            k->add(*werte);
            knopf->add(*k);
            knopf->signal_clicked().connect([this, id, aktiv, bauen] {
                if (id == aktiv) return;
                maschine_wechseln(id, [bauen] { (*bauen)(); });
            });
            ziel->add(*knopf);
        }
        auto reihe = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        auto neu = Gtk::make_managed<Gtk::Button>(TR("+ Maschine hinzufügen"));
        neu->set_tooltip_text(TR("Als Kopie der Maschine dieser Zeichnung – mit ihren Werten und Fräsern"));
        auto umb = Gtk::make_managed<Gtk::Button>(TR("Umbenennen …"));
        auto weg = Gtk::make_managed<Gtk::Button>(TR("Entfernen"));
        weg->set_sensitive(liste.size() > 1);
        weg->set_tooltip_text(TR("Nimmt die Maschine dieser Zeichnung aus der Liste. Ihre Datei bleibt als Sicherung "
                                 "liegen. Die letzte Maschine bleibt immer."));
        reihe->add(*neu);
        reihe->add(*umb);
        reihe->add(*weg);
        ziel->add(*reihe);
        std::string akt_name;
        for (auto const &v : liste)
            if (auto o = v.if_object(); o && str(*o, "id") == aktiv) akt_name = str(*o, "name");
        neu->signal_clicked().connect([this, aktiv, akt_name, bauen] {
            auto name = maschine_name_fragen(TR("Maschine hinzufügen"), TRF("{} (Kopie)", {akt_name}));
            if (name.empty()) return;
            bj::object a;
            a["was"] = "neu";
            a["name"] = name;
            a["kopie_von"] = aktiv;
            python({"datenbank", "maschinen", bj::serialize(a)},
                   [bauen](bool, std::string const &, std::string const &) { (*bauen)(); });
        });
        umb->signal_clicked().connect([this, aktiv, akt_name, bauen] {
            auto name = maschine_name_fragen(TR("Maschine umbenennen"), akt_name);
            if (name.empty() || name == akt_name) return;
            bj::object a;
            a["was"] = "umbenennen";
            a["id"] = aktiv;
            a["name"] = name;
            python({"datenbank", "maschinen", bj::serialize(a)},
                   [bauen](bool, std::string const &, std::string const &) { (*bauen)(); });
        });
        weg->signal_clicked().connect([this, aktiv, akt_name, bauen] {
            auto dlg = dialog(TR("Maschine entfernen"));
            auto t = label(TRF("«{}» aus der Liste nehmen? Zeichnungen dieser Maschine nehmen danach die erste "
                               "Maschine der Liste. Die Datei mit Werten und Fräsern bleibt als Sicherung liegen.",
                               {akt_name}));
            t->set_line_wrap(true);
            t->set_max_width_chars(50);
            dlg->get_content_area()->add(*t);
            dlg->add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
            auto ok = dlg->add_button(TR("Entfernen"), Gtk::RESPONSE_OK);
            klasse(*ok, "destructive-action");
            dlg->show_all();
            if (dlg->run() != Gtk::RESPONSE_OK) return;
            bj::object a;
            a["was"] = "entfernen";
            a["id"] = aktiv;
            if (str(einst, "maschine_id") == aktiv) {
                einst.erase("maschine_id");
                schreiben(TR("Maschine entfernt"));
            }
            python({"datenbank", "maschinen", bj::serialize(a)},
                   [bauen](bool, std::string const &, std::string const &) { (*bauen)(); });
        });
        ziel->show_all();
    };
    (*bauen)();
    maschine_karten_neu = [bauen] { (*bauen)(); };       // bauen prüft selbst, ob die Karten noch da sind
    python({"datenbank", "werkstoffe"}, [](bool, std::string const &, std::string const &) {});
}

void HirchFraesen::Impl::maschine_waehlen(std::function<void()> danach)
{
    auto const liste = hirch_maschinen();
    if (liste.size() < 2) {
        if (danach) danach();
        return;
    }
    auto dlg = dialog(TR("Für welche Maschine?"));
    auto t = label(TR("Die Zeichnung merkt sich ihre Maschine: Werte, Steuerung und Fräser dieser Maschine gelten."),
                   "hirch-dim");
    t->set_line_wrap(true);
    t->set_max_width_chars(48);
    t->set_xalign(0);
    dlg->get_content_area()->add(*t);
    Gtk::RadioButton::Group gruppe;
    std::vector<std::pair<Gtk::RadioButton *, std::string>> knoepfe;
    for (auto const &v : liste) {
        auto o = v.if_object();
        if (!o) continue;
        auto k = Gtk::make_managed<Gtk::RadioButton>(gruppe);
        k->set_mode(false);
        klasse(*k, "hirch-wz-karte");
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
        auto n = label("");
        n->set_markup("<b>" + markup_escape(str(*o, "name")) + "</b>");
        box->add(*n);
        auto w = label(TRF("Tisch {} × {} mm · Spindel ≤ {} U/min · {} Fräser",
                           {zahl(num(*o, "x", 0), hirch_zoll() ? 1 : 0), zahl(num(*o, "y", 0), hirch_zoll() ? 1 : 0), zahl(num(*o, "drehzahl_max", 0), 0),
                            zahl(num(*o, "fraeser", 0), 0)}), "hirch-dim");
        klasse(*w, "hirch-klein");
        box->add(*w);
        k->add(*box);
        k->set_active(flag(*o, "aktiv", false));
        klasse(*k, "hirch-wz-gewaehlt", k->get_active());
        k->signal_toggled().connect([k] { klasse(*k, "hirch-wz-gewaehlt", k->get_active()); });
        dlg->get_content_area()->add(*k);
        knoepfe.emplace_back(k, str(*o, "id"));
    }
    auto ok = dlg->add_button(TR("Weiter"), Gtk::RESPONSE_OK);
    klasse(*ok, "suggested-action");
    dlg->set_default_response(Gtk::RESPONSE_OK);
    dlg->show_all();
    dlg->run();
    std::string id;
    for (auto const &[k, i] : knoepfe)
        if (k->get_active()) id = i;
    dlg.reset();
    if (!id.empty()) {
        einst["maschine_id"] = id;
        schreiben(TR("Maschine gewählt"));
        bj::object a;
        a["was"] = "zuletzt";
        a["id"] = id;
        python({"datenbank", "maschinen", bj::serialize(a)}, [](bool, std::string const &, std::string const &) {});
    }
    if (danach) danach();
}

void HirchFraesen::Impl::maschine_wechseln(std::string const &ziel, std::function<void()> danach)
{
    std::string ziel_name = ziel;
    for (auto const &v : hirch_maschinen())
        if (auto o = v.if_object(); o && str(*o, "id") == ziel) ziel_name = str(*o, "name");
    bj::object wz;
    if (auto w = unter(einst, "werkzeuge")) wz = *w;
    bj::object a;
    a["was"] = "vergleich";
    a["ziel"] = ziel;
    a["werkzeuge"] = wz;
    python({"datenbank", "maschinen", bj::serialize(a)},
           [this, ziel, ziel_name, wz, danach](bool ok, std::string const &o, std::string const &) {
        bj::object r;
        if (!ok || !json_lesen(o, r)) return;
        auto v = liste_von(r, "vergleich");
        int fehlt = 0;
        auto dlg = dialog(TRF("Maschine wechseln: {}", {ziel_name}));
        auto t = label(TR("Die Zeichnung rechnet danach mit den Werten und der Steuerung dieser Maschine. "
                          "Ihre Fräser behält sie."), "hirch-dim");
        t->set_line_wrap(true);
        t->set_max_width_chars(52);
        t->set_xalign(0);
        dlg->get_content_area()->add(*t);
        auto g = formular();
        int zeile = 0;
        if (v) {
            for (auto const &e : *v) {
                auto eo = e.if_object();
                if (!eo) continue;
                auto n = label(str(*eo, "slot") + "  " + str(*eo, "name"));
                n->set_hexpand(true);
                g->attach(*n, 0, zeile);
                bool da = !str(*eo, "gefunden").empty();
                if (!da) ++fehlt;
                auto st = label(da ? std::string("✓ ") + TR("vorhanden") : TRF("fehlt bei «{}»", {ziel_name}),
                                da ? "hirch-ok" : "hirch-warntext");
                g->attach(*st, 1, zeile);
                ++zeile;
            }
        }
        dlg->get_content_area()->add(*g);
        auto ueb = Gtk::make_managed<Gtk::CheckButton>(TRF("Fehlende Fräser zu «{}» übernehmen", {ziel_name}));
        ueb->set_active(true);
        if (fehlt > 0) dlg->get_content_area()->add(*ueb);
        dlg->add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
        auto okk = dlg->add_button(TR("Wechseln"), Gtk::RESPONSE_OK);
        klasse(*okk, "suggested-action");
        dlg->set_default_response(Gtk::RESPONSE_OK);
        dlg->show_all();
        bool uebernehmen = false;
        int antwort = dlg->run();
        uebernehmen = fehlt > 0 && ueb->get_active();
        dlg.reset();
        if (antwort != Gtk::RESPONSE_OK) return;
        einst["maschine_id"] = ziel;
        schreiben(TR("Maschine gewechselt"));          // ändert die Zeichnung: es wird neu gerechnet und geprüft
        bj::object b;
        b["was"] = "wechseln";
        b["ziel"] = ziel;
        b["werkzeuge"] = wz;
        b["uebernehmen"] = uebernehmen;
        python({"datenbank", "maschinen", bj::serialize(b)},
               [danach](bool, std::string const &, std::string const &) { if (danach) danach(); });
    });
}

void HirchFraesen::Impl::maschine_werte_nachfuehren(bj::object const &r)
{
    // Windows 08.11. (Fund 1): nach Wechsel oder Entfernen zeigten die Felder in «Einstellungen › Maschine» noch die
    // Werte der alten Maschine, «Übernehmen» hätte sie in die neue geschrieben. Jede Antwort von «datenbank» bringt
    // die Werte der gültigen Maschine mit: damit nachführen und die Seite neu bauen, wenn die Maschine eine andere ist.
    auto m = unter(r, "maschine");
    std::string mid = str(r, "maschine_id");
    if (!m || mid.empty()) return;
    bool andere = mid != maschine_werte_id;
    maschine_werte_id = mid;
    if (hat_analyse) {
        if (auto am = unter(analyse, "maschine")) {
            for (auto const &kv : *m) (*am)[kv.key()] = kv.value();
        } else {
            analyse["maschine"] = *m;
        }
    }
    if (andere && maschine_ziel) {
        Glib::signal_idle().connect_once([this] {
            if (maschine_ziel)
                if (auto *box = dynamic_cast<Gtk::Box *>(Glib::wrap(maschine_ziel))) maschine_abschnitt(box);
        });
    } else if (maschine_karten_neu) {
        Glib::signal_idle().connect_once([this] { if (maschine_karten_neu) maschine_karten_neu(); });
    }
}

void HirchFraesen::Impl::maschine_knopf_auffrischen()
{
    auto const &liste = hirch_maschinen();
    if (liste.size() < 2) {
        maschine_knopf.hide();
        return;
    }
    // die Maschine der Zeichnung; ohne Eintrag die, die Python als gültig meldet
    std::string aktiv = str(einst, "maschine_id"), aktiv_name;
    bool da = false;
    for (auto const &v : liste)
        if (auto o = v.if_object(); o && str(*o, "id") == aktiv) da = true;
    if (!da) aktiv.clear();
    for (auto const &v : liste)
        if (auto o = v.if_object(); o && (aktiv.empty() ? flag(*o, "aktiv", false) : str(*o, "id") == aktiv)) {
            aktiv = str(*o, "id");
            aktiv_name = str(*o, "name");
        }
    auto innen = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 4);
    auto l = label(aktiv_name);
    l->set_ellipsize(Pango::ELLIPSIZE_END);
    l->set_max_width_chars(18);
    innen->add(*l);
    innen->add(*Gtk::make_managed<Gtk::Image>("pan-down-symbolic", Gtk::ICON_SIZE_BUTTON));
    if (maschine_knopf.get_child()) maschine_knopf.remove();
    maschine_knopf.add(*innen);
    auto pop = Gtk::make_managed<Gtk::Popover>(maschine_knopf);
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
    box->set_margin_top(6);
    box->set_margin_bottom(6);
    box->set_margin_start(6);
    box->set_margin_end(6);
    for (auto const &v : liste) {
        auto o = v.if_object();
        if (!o) continue;
        std::string id = str(*o, "id");
        auto k = Gtk::make_managed<Gtk::ModelButton>();
        k->property_text() = (id == aktiv ? "✓ " : "    ") + str(*o, "name");
        k->signal_clicked().connect([this, id, aktiv] {
            if (id != aktiv)
                Glib::signal_idle().connect_once([this, id] { maschine_wechseln(id, [this] { maschine_knopf_auffrischen(); }); });
        });
        box->add(*k);
    }
    box->show_all();
    pop->add(*box);
    maschine_knopf.set_popover(*pop);
    innen->show_all();
    maschine_knopf.show();          // no_show_all: show_all() wäre hier wirkungslos
}

} // namespace Inkscape::UI::Dialog

namespace Inkscape::UI::Dialog {

// Zeit des Programmstarts (beim Laden der Bibliothek gesetzt)
static gint64 const programm_start = g_get_real_time() / G_USEC_PER_SEC;

bool HirchFraesen::Impl::ersteinrichtung_noetig()
{
    // Peter 08.11. (Entwurf basis-ersteinrichtung.png): nur beim allerersten Start – noch keine Maschinen und kein
    // altes werkzeuge.json, und die Ersteinrichtung lief noch nie
    auto prefs = Inkscape::Preferences::get();
    if (prefs->getBool("/hirch/ersteinrichtung", false)) return false;
#ifdef _WIN32
    char const *basis = g_getenv("APPDATA");
    std::string ordner = Glib::build_filename(basis ? basis : g_get_user_config_dir(), "hirch-cam");
#else
    std::string ordner = Glib::build_filename(g_get_user_config_dir(), "hirch-cam");
#endif
    // maschinen.json legt Hirch CAM beim ersten Lesen selbst an – auch schon in diesem Start (Analyse): neu ist sie,
    // wenn sie nach dem Programmstart entstand und nicht aus einer alten werkzeuge.json umgewandelt wurde
    auto alt = [&](char const *d) { return Glib::file_test(Glib::build_filename(ordner, d), Glib::FILE_TEST_EXISTS); };
    bool schon = alt("werkzeuge.json") || alt("werkzeuge-vor-maschinen.json");
    if (!schon && alt("maschinen.json")) {
        GStatBuf st;
        if (g_stat(Glib::build_filename(ordner, "maschinen.json").c_str(), &st) == 0) schon = st.st_mtime < programm_start - 2;
    }
    if (schon) prefs->setBool("/hirch/ersteinrichtung", true);       // schon eingerichtet (z. B. ältere Version)
    return !schon;
}

void HirchFraesen::Impl::ersteinrichtung()
{
    auto prefs = Inkscape::Preferences::get();
    auto dlg = dialog(hirch_englisch() ? "Welcome to Hirch CAM" : "Willkommen bei Hirch CAM");
    dlg->set_default_size(560, -1);
    auto inhalt = dlg->get_content_area();
    inhalt->set_spacing(8);
    // Schritte oben
    auto schritte = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
    schritte->set_homogeneous(true);
    std::vector<Gtk::Label *> marken;
    for (char const *t : {"1 Sprache", "2 Maschine", "3 Fräser", "4 Fertig"}) {
        auto l = label(TR(t));
        klasse(*l, "hirch-schritt");
        marken.push_back(l);
        schritte->add(*l);
    }
    inhalt->add(*schritte);
    auto stapel = Gtk::make_managed<Gtk::Stack>();
    stapel->set_transition_type(Gtk::STACK_TRANSITION_TYPE_NONE);
    inhalt->add(*stapel);
    auto gruppe = [](std::vector<std::pair<std::string, std::string>> const &w, std::string const &an) {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
        klasse(*box, "linked");
        Gtk::RadioButton::Group g;
        auto knoepfe = std::make_shared<std::vector<std::pair<Gtk::RadioButton *, std::string>>>();
        for (auto const &[id, text] : w) {
            auto k = Gtk::make_managed<Gtk::RadioButton>(g, text);
            k->set_mode(false);
            k->set_active(id == an);
            box->add(*k);
            knoepfe->emplace_back(k, id);
        }
        auto wert = [knoepfe] {
            for (auto const &[k, id] : *knoepfe)
                if (k->get_active()) return id;
            return std::string();
        };
        return std::make_pair(box, std::function<std::string()>(wert));
    };
    auto zeile = [](Gtk::Box *ziel, std::string const &text, Gtk::Widget &w) {
        auto z = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 12);
        auto l = label(text);
        l->set_hexpand(true);
        l->set_xalign(0);
        z->add(*l);
        z->add(w);
        ziel->add(*z);
    };
    auto hinweis = [](Gtk::Box *ziel, std::string const &text) {
        auto l = label(text, "hirch-dim");
        klasse(*l, "hirch-klein");
        l->set_line_wrap(true);
        l->set_max_width_chars(60);
        l->set_xalign(0);
        ziel->add(*l);
    };
    // 1 Sprache (zweisprachig beschriftet: die Wahl gilt erst nach dem Neustart)
    auto s1 = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
    auto [sp_box, sprache] = gruppe({{"de", "Deutsch"}, {"en", "English"}}, hirch_englisch() ? "en" : "de");
    zeile(s1, "Sprache / Language", *sp_box);
    auto [ds_box, design] = gruppe({{"hell", TR("Hell")}, {"dunkel", TR("Dunkel")}}, design_dunkel() ? "dunkel" : "hell");
    zeile(s1, TR("Darstellung"), *ds_box);
    stapel->add(*s1, "1");
    // 2 Maschine
    auto s2 = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
    auto kopf = [](Gtk::Box *z, char const *t) {
        auto l = label(TR(t), "hirch-kopf");
        l->set_xalign(0);
        z->add(*l);
    };
    kopf(s2, "EINHEIT");
    // ohne TR: die Texte nennen beide Einheiten
    auto [eh_box, einheit] = gruppe({{"mm", "mm"}, {"in", hirch_englisch() ? "Inches (in)" : "Zoll (in)"}},
                                    hirch_zoll() ? "in" : "mm");
    zeile(s2, TR("Masse anzeigen und eingeben in"), *eh_box);
    hinweis(s2, TR("Hirch CAM rechnet immer in Millimetern. Zoll ändert nur, was du siehst und eingibst. Später: "
                   "Einstellungen › Programm."));
    kopf(s2, "MASCHINE");
    struct Vorlage { char const *id, *name; double x, y; };
    static Vorlage const VORLAGEN[] = {{"klein", "Kleine Portalfräse", 600, 900}, {"gross", "Grosse Portalfräse", 1300, 2500},
                                       {"eigen", "Eigene Werte", 0, 0}};
    std::vector<std::pair<std::string, std::string>> vl;
    for (auto const &v : VORLAGEN)
        vl.emplace_back(v.id, TR(v.name));
    auto [vl_box, vorlage] = gruppe(vl, "klein");
    s2->add(*vl_box);
    auto g = formular();
    auto mx = spin_zeile(*g, 0, TR("Verfahrweg X"), 600, 10, 5000, 10, 0, "mm");
    auto my = spin_zeile(*g, 1, TR("Verfahrweg Y"), 900, 10, 5000, 10, 0, "mm");
    auto mn = spin_zeile(*g, 2, TR("Drehzahl höchstens"), 24000, 1000, 60000, 500, 0, TR("U/min"));
    s2->add(*g);
    for (auto *k : vl_box->get_children())
        if (auto rb = dynamic_cast<Gtk::RadioButton *>(k))
            rb->signal_toggled().connect([vorlage, mx, my] {
                auto v = vorlage();
                for (auto const &x : VORLAGEN)
                    if (v == x.id && x.x > 0) {
                        mx->set_value(x.x);
                        my->set_value(x.y);
                    }
            });
    auto name = Gtk::make_managed<Gtk::Entry>();
    name->set_text(TR("Meine Maschine"));
    zeile(s2, TR("Name"), *name);
    auto steuerung = Gtk::make_managed<Gtk::ComboBoxText>();
    for (auto const &[id, t] : std::vector<std::pair<char const *, char const *>>{
             {"", "RichAuto A11 (Vorgabe)"}, {"grbl_mm", "Grbl / grblHAL / FluidNC"}, {"mach3_mm", "Mach3 / Mach4"},
             {"uccnc_mm", "UCCNC"}, {"linuxcnc_mm", "LinuxCNC"}, {"masso_mm", "MASSO G3"}, {"carvera", "Makera Carvera"},
             {"centroid_mm", "Centroid Acorn / CNC12"}, {"edingcnc_mm", "EdingCNC"}, {"planetcnc_mm", "PlanetCNC TNG"},
             {"wincnc_mm", "WinCNC"}, {"winpcnc_mm", "WinPC-NC"}, {"kinetic_nc_mm", "KinetiC-NC"},
             {"ncstudio_mm", "Weihong NcStudio"}, {"fanuc_mm", "Fanuc / Haas / Syntec"}})
        steuerung->append(id, TR(t));
    steuerung->set_active_id("");
    zeile(s2, TR("Steuerung"), *steuerung);
    hinweis(s2, TR("Die Steuerung bestimmt den G-Code. Ungeprüfte Steuerungen: den ersten Lauf in der Luft fahren."));
    stapel->add(*s2, "2");
    // 3 Fräser
    auto s3 = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
    auto [fr_box, fraeser] = gruppe({{"mm", TR("Metrisch (Ø 3, 6, V 60° …)")}, {"zoll", TR("Zoll (1/8\", 1/4\", V 60° 1/2\" …)")}},
                                    hirch_zoll() ? "zoll" : "mm");
    zeile(s3, TR("Startliste der Fräser"), *fr_box);
    hinweis(s3, TR("Gespeichert wird in Millimetern. Eigene Fräser kommen über «Fräser …» dazu."));
    stapel->add(*s3, "3");
    // 4 Fertig
    auto s4 = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
    auto fertig_text = label("", "");
    fertig_text->set_line_wrap(true);
    fertig_text->set_max_width_chars(60);
    fertig_text->set_xalign(0);
    s4->add(*fertig_text);
    stapel->add(*s4, "4");
    // Knöpfe
    auto zurueck = dlg->add_button(TR("Zurück"), 1);
    dlg->add_button(TR("Später"), Gtk::RESPONSE_CANCEL);
    auto weiter = dlg->add_button(TR("Weiter"), 2);
    klasse(*weiter, "suggested-action");
    auto schritt = std::make_shared<int>(1);
    auto zeigen = [=] {
        stapel->set_visible_child(std::to_string(*schritt));
        for (int i = 0; i < 4; ++i) {
            klasse(*marken[i], "hirch-schritt-an", i + 1 == *schritt);
            klasse(*marken[i], "hirch-schritt-fertig", i + 1 < *schritt);
        }
        zurueck->set_sensitive(*schritt > 1);
        // Fräser: Startliste passend zur gewählten Einheit (Zoll → Zoll-Liste)
        if (*schritt == 3) {
            auto k = fr_box->get_children();
            if (k.size() == 2)
                if (auto rb = dynamic_cast<Gtk::RadioButton *>(k[einheit() == "in" ? 1 : 0])) rb->set_active(true);
        }
        weiter->set_label(*schritt == 4 ? TR("Los geht's") : TR("Weiter"));
        if (*schritt == 4) {
            bool neu = sprache() != (hirch_englisch() ? "en" : "de") || (einheit() == "in") != hirch_zoll();
            fertig_text->set_text(neu ? TR("Fertig. Sprache oder Einheit gelten nach einem Neustart von Hirch CAM – "
                                           "bitte danach einmal schliessen und neu öffnen.")
                                      : TR("Fertig. Alles lässt sich später in den Einstellungen ändern."));
        }
    };
    dlg->show_all();
    zeigen();
    bool gespeichert = false;
    for (;;) {
        int a = dlg->run();
        if (a == 1 && *schritt > 1) { --*schritt; zeigen(); continue; }
        if (a == 2 && *schritt < 4) { ++*schritt; zeigen(); continue; }
        if (a == 2) gespeichert = true;
        break;
    }
    prefs->setBool("/hirch/ersteinrichtung", true);
    if (!gespeichert) {
        dlg->hide();
        return;
    }
    // Sprache, Einheit, Darstellung
    prefs->setString("/hirch/sprache", sprache());
    prefs->setString("/ui/language", sprache());
    prefs->setString("/hirch/einheit", einheit());
    design_setzen(design() == "dunkel", fenster());
    prefs->save();
    // Maschine (Werte in mm – die Felder rechnen selbst um) und Fräser
    bj::object m;
    m["x"] = wert(mx);
    m["y"] = wert(my);
    m["drehzahl_max"] = wert(mn);
    m["postprozessor"] = steuerung->get_active_id().raw();
    std::string aus, fehler;
    python_jetzt({"datenbank", "maschine", bj::serialize(m)}, aus, fehler);
    bj::object r;
    if (python_jetzt({"datenbank", "werkstoffe"}, aus, fehler) && json_lesen(aus, r)) {
        bj::object u;
        u["was"] = "umbenennen";
        u["id"] = str(r, "maschine_id", "m1");
        u["name"] = name->get_text().raw();
        python_jetzt({"datenbank", "maschinen", bj::serialize(u)}, aus, fehler);
    }
    bj::object f;
    f["art"] = fraeser();
    python_jetzt({"datenbank", "startliste", bj::serialize(f)}, aus, fehler);
    dlg->hide();
    letzte_xml.clear();
    spaeter(10);
}

} // namespace Inkscape::UI::Dialog
