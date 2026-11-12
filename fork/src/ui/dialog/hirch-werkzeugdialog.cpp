// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Werkzeugdialog, Werkzeugliste, Farbleiste
 */
#include "ui/dialog/hirch-intern.h"
#include "ui/dialog/hirch-3d.h"

namespace Inkscape::UI::Dialog {

void HirchFraesen::Impl::werkzeuge_dialog(std::string const &start_key, std::string const &neu_text, WzWahl *wahl,
                                         bool aufraeumen)
{
    if (!unter(einst, "werkzeuge")) einst["werkzeuge"] = bj::object();
    // «Meine Werkzeuge» ist der eine Katalog für alle Zeichnungen. In «arbeit» stehen die Werkzeuge
    // der Zeichnung (Plätze A, B, …; mit db_id verknüpft oder nur_hier) und dahinter die übrigen
    // Fräser aus «Meine Werkzeuge» als «db:<id>». Alles erst bei «Übernehmen» zurück.
    bj::object arbeit;
    std::map<std::string, bj::object> db_alt;          // id → wie in «Meine Werkzeuge»
    std::set<std::string> bearbeitet;                  // Einträge, an denen etwas geändert wurde
    std::set<std::string> db_weg;                      // aus «Meine Werkzeuge» löschen
    // auch nach «Übernehmen» bei offenem Fenster (Peter, 20.10.): neu aus Zeichnung und Katalog
    auto arbeit_aufbauen = [&] {
        arbeit = *unter(einst, "werkzeuge");
        db_alt.clear();
        bearbeitet.clear();
        db_weg.clear();
        std::set<std::string> verknuepft;
        for (auto const &kv : arbeit) {
            if (auto w = kv.value().if_object(); w && !flag(*w, "nur_hier", false)) {
                if (auto d = str(*w, "db_id"); !d.empty()) verknuepft.insert(d);
            }
        }
        if (auto l = liste_von(analyse, "datenbank")) {
            for (auto const &v : *l) {
                auto o = v.if_object();
                if (!o || str(*o, "id").empty()) continue;
                auto id = str(*o, "id");
                db_alt[id] = *o;
                if (verknuepft.count(id)) continue;
                bj::object w = *o;
                w.erase("id");
                schnittdaten_uebernehmen(w);
                arbeit["db:" + id] = std::move(w);
            }
        }
    };
    arbeit_aufbauen();
    // Änderungen seit dem letzten «Übernehmen»: der Knopf ist nur dann aktiv
    bool veraendert = false;
    Gtk::Widget *ok_knopf = nullptr;
    Gtk::Button *ueb_knopf = nullptr;          // Wählen: «Übernehmen» lässt das Fenster offen (Peter, 22.10.)
    Gtk::Label *aend_hinweis = nullptr;
    auto aendern = [&](bool an) {
        veraendert = an;
        if (ok_knopf && !wahl) ok_knopf->set_sensitive(an);
        if (wahl) {
            if (ueb_knopf) ueb_knopf->set_sensitive(an);
            if (auto b = dynamic_cast<Gtk::Button *>(ok_knopf))
                b->set_label(an ? TR("Übernehmen und wählen") : TR("Wählen"));
            if (aend_hinweis) aend_hinweis->set_text(an ? TR("● Geändert – noch nicht übernommen") : "");
        }
    };
    auto ist_db = [](std::string const &k) { return k.rfind("db:", 0) == 0; };
    auto benutzt = werkzeug_benutzung();
    int neu_zaehler = 0;
    auto neue_id = [&neu_zaehler] {
        return "w" + std::to_string((long long)(g_get_real_time() / 1000)) + "_" + std::to_string(++neu_zaehler);
    };

    // Ein Dialog für Wählen und Verwalten (Peter, 20.10.): links die Arten als Karten und die Fräser der
    // Art als Zeilen, rechts alle Angaben des gewählten Fräsers (direkt änderbar, gilt erst mit
    // «Übernehmen» bzw. «Wählen»).
    auto dlg = dialog(wahl ? TR("Werkzeug wählen") : TR("Werkzeuge"));
    dlg->set_resizable(true);
    auto box = dlg->get_content_area();
    auto haupt = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 18);
    box->add(*haupt);

    // ---------------- links: Suche, Arten (Karten), Fräser der Art (Zeilen)
    auto links = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
    links->set_size_request(780, -1);
    auto such = Gtk::make_managed<Gtk::SearchEntry>();
    such->set_placeholder_text(TR("Fräser suchen – Name, Ø, Winkel …"));
    links->add(*such);
    // Arten als Spalte links neben der Liste (Peter, 20.10.: kein waagrechtes Scrollen)
    auto arten_liste = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 10);
    arten_liste->set_vexpand(true);
    auto arten_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
    auto arten_sw = Gtk::make_managed<Gtk::ScrolledWindow>();
    arten_sw->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
    arten_sw->set_propagate_natural_width(true);
    arten_sw->set_size_request(170, -1);
    arten_sw->add(*arten_box);
    arten_liste->add(*arten_sw);
    auto liste_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
    liste_box->set_hexpand(true);
    arten_liste->add(*liste_box);
    links->add(*arten_liste);
    auto spalten = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    auto sp_name = label("", "hirch-kopf");
    sp_name->set_xalign(0);
    sp_name->set_hexpand(true);
    sp_name->set_margin_start(36);
    auto sp_masse = label(TR("MASSE"), "hirch-kopf");
    sp_masse->set_xalign(0);
    sp_masse->set_size_request(115, -1);
    auto sp_daten = label(TR("SCHNITTDATEN"), "hirch-kopf");
    sp_daten->set_xalign(0);
    sp_daten->set_size_request(130, -1);
    auto sp_platz = label(TR("PLATZ"), "hirch-kopf");
    sp_platz->set_size_request(44, -1);
    for (auto l_ : {sp_name, sp_masse, sp_daten, sp_platz}) spalten->add(*l_);
    liste_box->add(*spalten);
    auto wl = Gtk::make_managed<Gtk::ListBox>();
    wl->set_activate_on_single_click(false);
    auto wscroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    wscroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
    wscroll->add(*wl);
    wscroll->set_vexpand(true);
    wscroll->set_min_content_height(420);
    klasse(*wscroll, "hirch-karte");
    liste_box->add(*wscroll);
    auto hilfe = label(wahl ? TR("Klick zeigt rechts die Angaben · Doppelklick oder Enter wählt · blasse Arten passen "
                                 "weniger, bleiben aber wählbar")
                            : TR("Schild = in dieser Zeichnung benutzt, orange = nur in dieser Zeichnung · "
                                 "Änderungen gelten für alle Zeichnungen"), "hirch-dim");
    klasse(*hilfe, "hirch-klein");
    hilfe->set_xalign(0);
    hilfe->set_line_wrap(true);
    links->add(*hilfe);
    auto lk = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
    auto neu_knopf = Gtk::make_managed<Gtk::MenuButton>();
    neu_knopf->set_label(TR("+ Neuer Fräser"));
    neu_knopf->set_tooltip_text(TR("Neuen Fräser in «Meine Werkzeuge» anlegen – Art wählen"));
    auto neu_menue = Gtk::make_managed<Gtk::Menu>();
    neu_knopf->set_popup(*neu_menue);
    auto kopie = Gtk::make_managed<Gtk::Button>(TR("Kopie"));
    kopie->set_tooltip_text(TR("Gewählten Fräser kopieren (z. B. gleicher Fräser, anderer Durchmesser)"));
    auto minus = Gtk::make_managed<Gtk::Button>(TR("Löschen …"));
    klasse(*minus, "hirch-loeschen");
    lk->add(*neu_knopf);
    lk->add(*kopie);
    lk->add(*minus);
    Gtk::Button *keiner = nullptr;
    if (wahl && (wahl->mit_keinem || !wahl->keiner_text.empty())) {
        keiner = Gtk::make_managed<Gtk::Button>(wahl->mit_keinem ? TR("keiner – V-Fräser räumt aus") : wahl->keiner_text);
        if (wahl->mit_keinem) keiner->set_tooltip_text(TR("Ohne Fräser für den flachen Boden: der V-Fräser räumt alles aus"));
        keiner->set_halign(Gtk::ALIGN_END);
        keiner->set_hexpand(true);
        lk->add(*keiner);
    }
    links->add(*lk);
    haupt->add(*links);

    // ---------------- rechts: Skizze und die Werte, die zur Art passen
    auto rechts = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
    rechts->set_size_request(400, -1);
    auto oben = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 14);
    auto skizze = Gtk::make_managed<Gtk::DrawingArea>();
    skizze->set_size_request(150, 190);
    klasse(*skizze, "hirch-karte");
    oben->add(*skizze);
    auto kopf = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
    kopf->set_hexpand(true);
    kopf->add(*label(TR("Art")));
    auto typ = Gtk::make_managed<Gtk::ComboBoxText>();
    typ->append("v", TR("Gravierstichel / V-Fräser"));
    typ->append("schaft", TR("Schaftfräser"));
    typ->append("kugel", TR("Kugelfräser (Vollradius)"));
    typ->append("eckradius", TR("Eckradiusfräser"));
    typ->append("konisch", TR("Konischer Kugelfräser"));
    typ->append("gewinde", TR("Gewindefräser"));
    typ->append("bohrer", TR("Bohrer (Spiralbohrer)"));
    kopf->add(*typ);
    kopf->add(*label("Name"));
    auto name = Gtk::make_managed<Gtk::Entry>();
    name->set_tooltip_text(TR("Leer lassen: der Name ergibt sich aus Art und Massen"));
    kopf->add(*name);
    auto zusammenfassung = label("", "hirch-dim");
    zusammenfassung->set_line_wrap(true);
    zusammenfassung->set_max_width_chars(40);
    klasse(*zusammenfassung, "hirch-klein");
    kopf->add(*zusammenfassung);
    // Laufzeit des echten Fräsers (über alle Zeichnungen, gezählt beim Speichern des G-Codes)
    auto lz_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
    auto lz_l = label("", "hirch-dim");
    klasse(*lz_l, "hirch-klein");
    lz_box->add(*lz_l);
    auto lz_neu = Gtk::make_managed<Gtk::Button>(TR("Neu eingesetzt"));
    lz_neu->set_relief(Gtk::RELIEF_NONE);
    klasse(*lz_neu, "hirch-klein");
    lz_neu->set_tooltip_text(TR("Zähler auf 0 setzen – z. B. nach dem Schärfen oder mit einem neuen Fräser"));
    lz_neu->set_no_show_all(true);
    lz_box->add(*lz_neu);
    kopf->add(*lz_box);
    std::string lz_schluessel;
    oben->add(*kopf);
    rechts->add(*oben);

    rechts->add(*abschnitt(TR("Masse")));
    auto grid = formular();
    int zr = 0;
    int z_dm = zr; auto dm = spin_zeile(*grid, zr++, TR("Durchmesser"), 6, 0.1, 50, 0.1, 2, "mm");
    // Messkorrektur (Sammelauftrag 21.10., Punkt 3): so breit schneidet der Fräser wirklich
    int z_mk = zr; auto mk = spin_zeile(*grid, zr++, TR("Schneidet wie"), 6, 0.1, 52, 0.01, 2, "");
    mk->set_tooltip_text(TR("So breit schneidet der Fräser wirklich (Testteil ausgemessen). Gilt für Konturen, "
                            "Taschenwände und gefräste Löcher mit diesem Fräser, in allen Zeichnungen. Gleich wie der "
                            "Durchmesser = keine Korrektur."));
    auto mk_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
    mk_box->add(*label(einheit_feld(mk)));        // Länge: bei Zoll in Zoll
    auto mk_mess = Gtk::make_managed<Gtk::Button>(TR("Ausmessen …"));
    mk_mess->set_tooltip_text(TR("Testteil (Quadrat und Loch) in X und Y messen – Hirch CAM rechnet daraus, wie breit "
                                 "der Fräser schneidet"));
    mk_box->add(*mk_mess);
    grid->attach(*mk_box, 2, z_mk);
    double mk_e = 0;               // je Seite; > 0: schneidet grösser als angegeben
    bj::object mk_info;
    int z_vw = zr; auto vw = spin_zeile(*grid, zr++, TR("Winkel"), 60, 1, 170, 0.5, 1, "°");
    int z_sp = zr; auto sp = spin_zeile(*grid, zr++, TR("Spitze (flach)"), 0, 0, 5, 0.05, 2, "mm");
    int z_er = zr; auto er = spin_zeile(*grid, zr++, TR("Eckradius"), 1, 0, 25, 0.1, 2, "mm");
    int z_gz = zr; auto gz = spin_zeile(*grid, zr++, TR("Gewindezähne"), 1, 1, 30, 1, 0, TR("(1 = Einzahn)"));
    int z_gs = zr; auto gs = spin_zeile(*grid, zr++, TR("Zahnabstand (Steigung)"), 1.0, 0.1, 6, 0.05, 2, "mm");
    int z_bw = zr; auto bw = spin_zeile(*grid, zr++, TR("Spitzenwinkel"), 118, 60, 180, 1, 0, TR("° (Standardbohrer 118°)"));
    // Schaft-Ø: auch dicker als die Schneide (z. B. 3 mm Schneide auf 6 mm Schaft)
    int z_sd = zr; auto sd_ = spin_zeile(*grid, zr++, TR("Schaft-Ø"), 6, 0.5, 25, 0.5, 2, "mm");
    sd_->set_tooltip_text(TR("Durchmesser des Schafts (Spannzange). Ist er dicker als die Schneide, darf nicht "
                             "tiefer als die Schneidenlänge gefräst werden – sonst streift der Schaft die Wand."));
    auto sl = spin_zeile(*grid, zr++, TR("Schneidenlänge"), 20, 0.5, 200, 0.5, 1, "mm");
    // 3D: wie weit der Fräser aus der Spannmutter ragt (Kollisionsprüfung, hirch-3d.cpp)
    int z_al = zr; auto al = spin_zeile(*grid, zr++, TR("Ausspannlänge"), 0, 0, 300, 0.5, 1, "mm");
    al->set_tooltip_text(TR("Spitze bis Spannmutter – für die Prüfung von Schaft, Spannmutter, Spindel und "
                            "Halterung. 0 eintippen: wieder automatisch (Schneide + Übergang + 5 mm)."));
    bool al_auto = true;          // nichts eingetragen: der gerechnete Wert steht blass im Feld
    // ganze Länge des Werkzeugs (Peter 26.10.: Werkzeugkasten/Schubladen per Skript); rechnet nicht mit
    int z_gl = zr; auto gl = spin_zeile(*grid, zr++, TR("Gesamtlänge"), 0, 0, 400, 0.5, 1, "mm");
    gl->set_tooltip_text(TR("Ganze Länge des Werkzeugs, von der Spitze bis zum Ende des Schafts. Steht in der "
                            "Werkzeugdatei (z. B. für Werkzeugkästen und Beschriftungen); gefräst wird damit "
                            "nicht. 0 = nicht angegeben."));
    rechts->add(*grid);
    // Angaben, die nicht zusammenpassen: ein Satz in Orange unter den Massen
    auto pruef_hinweis = label("", "hirch-warntext");
    pruef_hinweis->set_line_wrap(true);
    pruef_hinweis->set_max_width_chars(48);
    pruef_hinweis->set_xalign(0);
    pruef_hinweis->set_no_show_all(true);
    rechts->add(*pruef_hinweis);

    // Schnittdaten: meist automatisch aus dem Material
    // Schnittdaten gelten je Werkstoff: hier für den der Zeichnung
    std::string const mat = str(einst, "material", "hartholz");
    auto werkstoff_von = [this](std::string const &k) -> std::string {
        if (auto l = liste_von(analyse, "materialien")) {
            for (auto const &e : *l) {
                auto p = e.if_array();
                if (p && p->size() >= 2 && (*p)[0].is_string() && (*p)[0].get_string() == k && (*p)[1].is_string())
                    return std::string((*p)[1].get_string());
            }
        }
        return k;
    };
    rechts->add(*abschnitt(TRF("Schnittdaten für {}", {werkstoff_von(mat)})));
    auto smodus = Gtk::make_managed<Gtk::ComboBoxText>();
    smodus->append("auto", TR("Automatisch aus Werkstoff und Fräser"));
    smodus->append("drehzahl", TR("Drehzahl von Hand – Vorschub passend gerechnet"));
    smodus->append("hand", TR("Alles von Hand"));
    smodus->set_tooltip_text(TR("Automatisch: Drehzahl, Vorschub und Eintauchen kommen aus Werkstoff und Fräser. "
                             "Drehzahl von Hand: die Drehzahl, die an der Spindel eingestellt ist (z. B. wo es am "
                             "wenigsten vibriert) – Vorschub und Eintauchen rechnet Hirch CAM dazu, damit jede "
                             "Schneide den richtigen Span für den Werkstoff abnimmt."));
    rechts->add(*smodus);
    auto grid2 = formular();
    int z2 = 0;
    // Höchst- und Mindestdrehzahl der Maschine gelten auch für Drehzahlen von Hand (Peter, 21.10.)
    double const n_max = [&] { auto m = unter(analyse, "maschine"); return m ? num(*m, "drehzahl_max", 24000) : 24000.0; }();
    double const n_min = [&] { auto m = unter(analyse, "maschine"); return m ? num(*m, "drehzahl_min", 6000) : 6000.0; }();
    auto dz = spin_zeile(*grid2, z2++, TR("Drehzahl"), std::min(18000.0, n_max), std::min(n_min, n_max), n_max, 500, 0,
                         TR("U/min"));
    dz->set_tooltip_text(TRF("Die Maschine dreht {} bis {} U/min (Einstellungen › Maschine)", {ganz(n_min), ganz(n_max)}));
    auto vs = spin_zeile(*grid2, z2++, TR("Vorschub"), 1000, 10, 20000, 50, 0, "mm/min");
    auto ei = spin_zeile(*grid2, z2++, TR("Eintauchen"), 300, 10, 10000, 10, 0, "mm/min");
    rechts->add(*grid2);
    auto sd_hinweis = label("", "hirch-dim");
    sd_hinweis->set_line_wrap(true);
    sd_hinweis->set_max_width_chars(48);
    sd_hinweis->set_xalign(0);
    klasse(*sd_hinweis, "hirch-klein");
    rechts->add(*sd_hinweis);
    // eigene Schnittdaten dieses Fräsers je Werkstoff (einzeln zurücksetzbar)
    auto je_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
    rechts->add(*je_box);

    // selten gebraucht
    auto mehr = Gtk::make_managed<Gtk::Expander>(TR("Weitere Angaben"));
    auto grid3 = formular();
    int z3 = 0;
    auto sn = spin_zeile(*grid3, z3++, TR("Schneiden"), 2, 1, 8, 1, 0, "");
    auto mitte = Gtk::make_managed<Gtk::CheckButton>(TR("Mittenschnitt (darf senkrecht eintauchen)"));
    grid3->attach(*mitte, 0, z3++, 3);
    // Übergang Schneide → Schaft (Kegel), 0 = automatisch aus den Durchmessern
    int z_ue = z3; auto ue = spin_zeile(*grid3, z3++, TR("Übergang zum Schaft"), 0, 0, 100, 0.5, 1, "mm");
    ue->set_tooltip_text(TR("Höhe des Kegels zwischen Schneide und Schaft – zählt zur Ausspannlänge und zur "
                            "Kollisionsprüfung. 0 eintippen: wieder automatisch (20° zur Achse bei dickerem "
                            "Schaft, 45° bei breiterer Schneide)."));
    bool ue_auto = true;
    // Drall, Wendeplatten, Spanbrecher: für das Bild in der Werkzeugwahl (Karten)
    int z_dr = z3;
    auto dr_l = label(TR("Drall"));
    dr_l->set_xalign(0);
    grid3->attach(*dr_l, 0, z3);
    auto drall = Gtk::make_managed<Gtk::ComboBoxText>();
    drall->append("auf", TR("aufwärts (Rechtsdrall)"));
    drall->append("ab", TR("abwärts (Linksdrall, Downcut)"));
    drall->append("kompression", TR("Kompression (unten auf-, oben abwärts)"));
    drall->set_tooltip_text(TR("Wohin die Wendel die Späne fördert – sieht man im Bild der Werkzeugwahl"));
    grid3->attach(*drall, 1, z3++, 2);
    int z_wp = z3;
    auto wendeplatten = Gtk::make_managed<Gtk::CheckButton>(TR("mit Wendeplatten (z. B. Planfräser)"));
    grid3->attach(*wendeplatten, 0, z3++, 3);
    int z_sb = z3;
    auto spanbrecher = Gtk::make_managed<Gtk::CheckButton>(TR("Spanbrecher (Schruppfräser)"));
    grid3->attach(*spanbrecher, 0, z3++, 3);
    auto nr = spin_zeile(*grid3, z3++, TR("Werkzeugnummer (T)"), 1, 1, 99, 1, 0, "");
    auto nrh = label(TR("Nur, wenn jedes Werkzeug eine eigene Datei bekommt: steht dann als T… in der Datei. "
                     "Bei Steuerungen mit allen Werkzeugen in einer Datei werden sie der Reihe nach nummeriert."),
                     "hirch-dim");
    nrh->set_line_wrap(true);
    nrh->set_max_width_chars(44);
    klasse(*nrh, "hirch-klein");
    grid3->attach(*nrh, 0, z3++, 3);
    mehr->add(*grid3);
    rechts->add(*mehr);

    // «In dieser Zeichnung»: Platz, wer ihn benutzt; bei nur_hier zurück in den Katalog oder ersetzen
    auto hier_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
    klasse(*hier_box, "hirch-karte2");
    auto hier_titel = label(TR("In dieser Zeichnung"), "hirch-titel");
    hier_titel->set_xalign(0);
    hier_box->add(*hier_titel);
    auto hier_text = label("");
    hier_text->set_line_wrap(true);
    hier_text->set_max_width_chars(48);
    hier_text->set_xalign(0);
    hier_box->add(*hier_text);
    auto hier_knoepfe = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    auto wieder = Gtk::make_managed<Gtk::Button>(TR("Wieder zu «Meine Werkzeuge»"));
    wieder->set_tooltip_text(TR("Diesen Fräser wieder für alle Zeichnungen in «Meine Werkzeuge» aufnehmen"));
    auto anderer = Gtk::make_managed<Gtk::MenuButton>();
    anderer->set_label(TR("Anderen wählen ▾"));
    anderer->set_tooltip_text(TR("Diesen Platz mit einem Fräser aus «Meine Werkzeuge» belegen – die Bearbeitungen "
                                 "behalten ihren Platz"));
    auto anderer_menue = Gtk::make_managed<Gtk::Menu>();
    anderer->set_popup(*anderer_menue);
    hier_knoepfe->add(*wieder);
    hier_knoepfe->add(*anderer);
    hier_box->add(*hier_knoepfe);
    hier_box->set_no_show_all(true);
    rechts->add(*hier_box);
    // rechts rollbar: der Dialog bleibt auch auf kleinen Bildschirmen ganz sichtbar
    auto rscroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    rscroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
    rscroll->set_propagate_natural_width(true);
    rscroll->set_min_content_height(680);
    rscroll->set_vexpand(true);
    rechts->set_margin_end(10);
    rscroll->add(*rechts);
    haupt->add(*rscroll);

    auto zeile_zeigen = [](Gtk::Grid *g, int z, bool an) {
        for (int c = 0; c < 3; ++c) {
            if (auto w = g->get_child_at(c, z)) {
                if (an) w->show(); else w->hide();
            }
        }
    };
    std::string aktuell;
    std::string art_gewaehlt;       // Kartengruppe links (fraeser_gruppe)
    bool lade = false;
    std::function<void()> liste_fuellen;
    auto sichtbar_setzen = [=] {
        auto t = typ->get_active_id();
        zeile_zeigen(grid, z_vw, t == "v" || t == "konisch");
        zeile_zeigen(grid, z_sp, t == "v");
        zeile_zeigen(grid, z_er, t == "eckradius" || t == "konisch");
        // konischer Kugelfräser: Kugelspitze, Kegelwinkel, grösster Ø der Schneide
        if (auto l = dynamic_cast<Gtk::Label *>(grid->get_child_at(0, z_er)))
            l->set_text(t == "konisch" ? TR("Radius der Kugelspitze") : TR("Eckradius"));
        if (auto l = dynamic_cast<Gtk::Label *>(grid->get_child_at(0, z_vw)))
            l->set_text(t == "konisch" ? TR("Kegelwinkel (gesamt)") : TR("Winkel"));
        zeile_zeigen(grid, z_gz, t == "gewinde");
        zeile_zeigen(grid, z_gs, t == "gewinde" && gz->get_value() > 1.5);
        zeile_zeigen(grid, z_bw, t == "bohrer");
        zeile_zeigen(grid, z_sd, t != "v");          // beim V-Fräser ist der Ø schon der Schaft
        zeile_zeigen(grid, z_mk, t != "v" && t != "bohrer" && t != "gewinde" && t != "konisch");
        zeile_zeigen(grid3, z_ue, t != "v" && t != "bohrer" && std::abs(sd_->get_value() - dm->get_value()) > 0.01);
        zeile_zeigen(grid3, z_dr, t == "schaft" || t == "kugel" || t == "eckradius");
        zeile_zeigen(grid3, z_wp, t == "schaft");
        zeile_zeigen(grid3, z_sb, t == "schaft");
        if (auto l = dynamic_cast<Gtk::Label *>(grid->get_child_at(0, z_dm))) {
            l->set_text(t == "v" ? TR("Schaftdurchmesser") : t == "konisch" ? TR("Grösster Ø der Schneide")
                                                                            : TR("Durchmesser"));
        }
        auto m = smodus->get_active_id();
        dz->set_sensitive(m != "auto");
        vs->set_sensitive(m == "hand");
        ei->set_sensitive(m == "hand");
    };
    auto lesen = [&] {
        if (aktuell.empty() || lade) return;
        auto it = arbeit.find(aktuell);
        if (it == arbeit.end()) return;
        lade = true;
        for (auto s : {dm, vw, sp, er, gz, gs, bw, sn, sl, al, ue, dz, vs, ei, nr})
            spin_lesen(s);   // nur getippter Text (sonst 3,175 → 3,17)
        lade = false;
        auto &w = it->value().as_object();
        {
            // mitgelieferte Namen stehen deutsch in der Datei und übersetzt im Feld: unverändert → deutsch lassen
            auto alt = str(w, "name");
            auto neu = name->get_text().raw();
            if (alt.empty() || neu != TR(alt.c_str())) w["name"] = neu;
        }
        w["typ"] = typ->get_active_id().raw();
        w["durchmesser"] = wert(dm);
        if (std::abs(mk_e) < 1e-6) {
            w.erase("messkorrektur");
            w.erase("messkorrektur_info");
        } else {
            w["messkorrektur"] = std::round(mk_e * 1e4) / 1e4;
            if (!mk_info.empty()) w["messkorrektur_info"] = mk_info;
        }
        w["v_winkel"] = wert(vw);
        w["spitze"] = wert(sp);
        w["eckradius"] = typ->get_active_id() == "kugel" ? wert(dm) / 2 : wert(er);
        w["zaehne"] = (int)wert(gz);
        w["steigung"] = wert(gs);
        w["spitzenwinkel"] = wert(bw);
        w["schneiden"] = (int)wert(sn);
        w["schneidenlaenge"] = wert(sl);
        spin_lesen(sd_);
        w["schaft"] = typ->get_active_id() == "v" ? wert(dm) : wert(sd_);
        // automatisch (0) oder eingetragen; der Wert im Feld zeigt dann den gerechneten
        w["uebergang"] = ue_auto ? 0.0 : wert(ue);
        w["drall"] = drall->get_active_id().empty() ? std::string("auf") : drall->get_active_id().raw();
        w["wendeplatten"] = wendeplatten->get_active();
        w["spanbrecher"] = spanbrecher->get_active();
        w["ausspannlaenge"] = al_auto ? 0.0 : wert(al);
        w["gesamtlaenge"] = wert(gl);
        w["mittenschnitt"] = mitte->get_active();
        w["nummer"] = (int)wert(nr);
        // Schnittdaten: nur für den Werkstoff der Zeichnung, und nur wenn sie von dem abweichen, was bisher galt
        {
            auto eff = fuer_werkstoff(w, mat);
            auto m_alt = schnittmodus(eff), m = smodus->get_active_id().raw();
            bool anders = m != m_alt || (m != "auto" && std::abs(num(eff, "drehzahl", 0) - wert(dz)) > 0.5) ||
                          (m == "hand" && (std::abs(num(eff, "vorschub", 0) - wert(vs)) > 0.5 ||
                                           std::abs(num(eff, "eintauchen", 0) - wert(ei)) > 0.5));
            auto je = w.if_contains("je_werkstoff");
            bool hat = je && je->is_object() && je->as_object().contains(mat);
            if (anders || hat) {
                bj::object e;
                e["schnittdaten"] = m;
                if (m != "auto") e["drehzahl"] = (int)wert(dz);
                if (m == "hand") {
                    e["vorschub"] = (int)wert(vs);
                    e["eintauchen"] = (int)wert(ei);
                }
                if (!je || !je->is_object()) w["je_werkstoff"] = bj::object();
                w["je_werkstoff"].as_object()[mat] = e;
            }
        }
    };
    auto wirksam = [&](std::string const &key) -> bj::object const * {
        auto it = arbeit.find(key);
        if (it == arbeit.end()) return nullptr;
        auto const &w = it->value().as_object();
        // geänderte Einträge: die Rechnung kennt ihre Werte noch nicht – bei «automatisch» gelten trotzdem die
        // gerechneten (die gespeicherten Drehzahl und Vorschub sind dort nur alte Reste)
        auto m_ = schnittmodus(fuer_werkstoff(w, mat));
        if (m_ == "auto" || (m_ != "hand" && !bearbeitet.count(key))) {
            bool db_ = key.rfind("db:", 0) == 0;
            if (auto ww = unter(analyse, db_ ? "datenbank_wirksam" : "werkzeuge_wirksam")) {
                if (auto x = unter(*ww, (db_ ? key.substr(3) : key).c_str())) return x;
            }
        }
        return &w;
    };
    // Empfehlung der letzten Rechnung (Span je Schneide, Maschinenvorschub) – auch für geänderte Einträge:
    // sie hängt nur von Werkstoff, Fräserart und Ø ab, nicht von Drehzahl oder Vorschub
    auto empfehlung_von = [&](std::string const &key) -> bj::object const * {
        bool db_ = key.rfind("db:", 0) == 0;
        if (auto ww = unter(analyse, db_ ? "datenbank_wirksam" : "werkzeuge_wirksam")) {
            if (auto x = unter(*ww, (db_ ? key.substr(3) : key).c_str())) return unter(*x, "empfehlung");
        }
        return nullptr;
    };
    // Peter 02.11.: wie die Schnittdaten gerechnet sind (Gravierstichel, Planfräser, Breite die schneidet)
    auto erklaerung = [&](std::string const &key) -> std::string {
        auto e = empfehlung_von(key);
        if (!e) return {};
        auto r = str(*e, "rechnung");
        double dw = num(*e, "d_wirksam", 0), zu = num(*e, "zustellung", 0);
        auto it = arbeit.find(key);
        std::string typ = it != arbeit.end() ? str(it->value().as_object(), "typ") : std::string();
        std::string t;
        if (r == "stichel")
            t = TRF("Gravierstichel: schneidet etwa {} mm breit (Spitze {} mm) · Herstellerwerte für Stichel",
                    {zahl(dw, 2), zahl(it != arbeit.end() ? num(it->value().as_object(), "spitze", 0) : 0, 2)});
        else if (r == "plan")
            t = TR("Planfräser: Herstellerwerte für Planfräser");
        else if (typ == "v" || typ == "konisch")
            t = TRF("Schneidet bei {} mm Tiefe etwa {} mm breit · Mittelwert der Herstellertabellen",
                    {zahl(zu, 1), zahl(dw, 2)});
        else if (!r.empty())
            t = TR("Mittelwert der Herstellertabellen");
        if (t.empty()) return t;
        // Rechnung in Klartext mit dem Schieber des Werkstoffs (Peter 06.11.)
        if (auto rt = str(*e, "rechnung_text"); !rt.empty()) t += "\n" + rechnung_uebersetzen(rt);
        return t + "\n" + TRF("→ {} U/min, Vorschub {}, Eintauchen {} mm/min, Zustellung {} mm",
                              {ganz(num(*e, "drehzahl", 0)), ganz(num(*e, "vorschub", 0)),
                               ganz(num(*e, "eintauchen", 0)), zahl(zu, 2)});
    };
    // wie werkzeugdb.schnittdaten: auf 10 gerundet, bei genau ,5 zur geraden Zahl (Python round)
    auto zehner = [](double v) { return std::max(10.0, std::nearbyint(v / 10) * 10); };
    // Drehzahl von Hand: Vorschub = Drehzahl × Schneiden × Span je Schneide, höchstens der
    // Maschinenvorschub; dieselbe Rechnung wie beim Erzeugen des G-Codes (werkzeugdb.wirksam)
    struct VonDrehzahl { double vorschub = 0, eintauchen = 0, ideal = 0, fz = 0, vmax = 0; };
    auto von_drehzahl = [&](std::string const &key, double n, double z) -> std::optional<VonDrehzahl> {
        auto e = empfehlung_von(key);
        double fz = e ? num(*e, "fz_genau", num(*e, "fz", 0)) : 0;
        if (fz <= 0 || n <= 0) return {};
        VonDrehzahl r;
        r.fz = fz;
        r.vmax = num(*e, "vorschub_max", 5000);
        r.ideal = n * std::max(1.0, z) * fz * num(*e, "prozent_vorschub", 100) / 100.0;   // Schieber des Werkstoffs
        double f = std::min(r.ideal, r.vmax);
        double anteil = num(*e, "eintauch_anteil", num(*e, "vorschub", 0) > 0
                                                       ? num(*e, "eintauchen", 0) / num(*e, "vorschub", 1) : 0.3);
        r.vorschub = zehner(f);
        r.eintauchen = zehner(f * anteil);
        return r;
    };
    // Drehzahl und Vorschub, wie sie für den Werkstoff dieser Zeichnung gelten (Liste «Schnittdaten»)
    auto schnitt_anzeige = [&](std::string const &key) -> std::pair<double, double> {
        auto it = arbeit.find(key);
        if (it == arbeit.end()) return {0, 0};
        auto const &w = it->value().as_object();
        auto we = fuer_werkstoff(w, mat);
        auto m = schnittmodus(we);
        if (m == "hand") {          // wie werkzeugdb.wirksam: begrenzt, Vorschub im selben Verhältnis
            double n = num(we, "drehzahl", 18000), ng = std::clamp(n, std::min(n_min, n_max), n_max);
            double f = num(we, "vorschub", 1000);
            if (n > 0 && std::abs(ng - n) > 0.5) f = zehner(f * ng / n);
            return {ng, f};
        }
        auto const *q = wirksam(key);
        if (m == "drehzahl") {
            double n = num(we, "drehzahl", 0);
            if (n > 0) n = std::clamp(n, std::min(n_min, n_max), n_max);
            if (auto r = von_drehzahl(key, n, num(w, "schneiden", 2))) return {n, r->vorschub};
            return {n > 0 ? n : num(*q, "drehzahl", 18000), num(*q, "vorschub", 1000)};
        }
        return {num(*q, "drehzahl", 18000), num(*q, "vorschub", 1000)};
    };
    auto drehzahl_rechnen = [&] {
        if (smodus->get_active_id() != "drehzahl") return;
        double n = dz->get_value(), z = std::max(1.0, sn->get_value());
        auto r = von_drehzahl(aktuell, n, z);
        if (!r) {
            sd_hinweis->set_text(TR("Vorschub wird beim Übernehmen aus Werkstoff und Fräser gerechnet."));
            return;
        }
        double fz = r->fz, ideal = r->ideal, vmax = r->vmax;
        lade = true;
        vs->set_value(r->vorschub);
        ei->set_value(r->eintauchen);
        lade = false;
        std::string t = TRF("{} U/min × {} Schneiden × {} mm Span je Schneide = {} mm/min",
                            {ganz(n), ganz(z), zahl(fz, 3), ganz(std::round(ideal / 10) * 10)});
        if (ideal > vmax + 1e-6)
            t += "\n" + TRF("Die Maschine fährt höchstens {} mm/min – der Span wird dünner. Wenn die Spindel ruhig "
                             "bleibt: Drehzahl etwas senken.", {ganz(vmax)});
        sd_hinweis->set_text(t);
    };
    auto zeigen = [&](std::string const &key) {
        aktuell = key;
        auto it = arbeit.find(key);
        rechts->set_sensitive(it != arbeit.end());
        if (it == arbeit.end()) return;
        auto const &w = it->value().as_object();
        lade = true;
        name->set_text(str(w, "name").empty() ? std::string() : std::string(TR(str(w, "name").c_str())));
        typ->set_active_id(str(w, "typ", "schaft"));
        dm->set_value(num(w, "durchmesser", 6));
        vw->set_value(num(w, "v_winkel", 60));
        sp->set_value(num(w, "spitze", 0));
        er->set_value(num(w, "eckradius", 0));
        gz->set_value(num(w, "zaehne", 1));
        gs->set_value(num(w, "steigung", 1.0));
        bw->set_value(num(w, "spitzenwinkel", 118));
        sn->set_value(num(w, "schneiden", 2));
        sl->set_value(num(w, "schneidenlaenge", 20));
        al_auto = num(w, "ausspannlaenge", 0) <= 0;
        ue_auto = num(w, "uebergang", 0) <= 0;
        al->set_value(werkzeug_ausspann(w));
        ue->set_value(werkzeug_uebergang(w));
        gl->set_value(num(w, "gesamtlaenge", 0));
        {
            auto mm = fraeser_merkmale(w);     // ältere Fräser: aus dem Namen erkannt
            drall->set_active_id(mm.drall == 'L' ? "ab" : mm.drall == 'K' ? "kompression" : "auf");
            wendeplatten->set_active(mm.art == "plan");
            spanbrecher->set_active(mm.art == "schrupp");
        }
        sd_->set_value(num(w, "schaft", std::max(3.0, num(w, "durchmesser", 6))));
        mk_e = num(w, "messkorrektur", 0);
        mk_info = unter(w, "messkorrektur_info") ? *unter(w, "messkorrektur_info") : bj::object();
        mk->set_value(num(w, "durchmesser", 6) + 2 * mk_e);
        klasse(*mk, "hirch-dim", std::abs(mk_e) < 1e-6);
        mitte->set_active(flag(w, "mittenschnitt", true));
        auto we = fuer_werkstoff(w, mat);
        auto wm = schnittmodus(we);
        smodus->set_active_id(wm);
        // bei «automatisch»: die Werte der letzten Rechnung zeigen
        auto const &q = *wirksam(key);
        dz->set_value(num(wm != "auto" ? we : q, "drehzahl", 18000));
        vs->set_value(num(wm == "hand" ? we : q, "vorschub", 1000));
        ei->set_value(num(wm == "hand" ? we : q, "eintauchen", 300));
        // eingetragene Drehzahl über der Höchstdrehzahl der Maschine: Feld zeigt die Grenze,
        // bei «Alles von Hand» Vorschub und Eintauchen im selben Verhältnis (wie beim Rechnen)
        double n_eingetragen = wm != "auto" ? num(we, "drehzahl", 0) : 0;
        bool begrenzt = n_eingetragen > 0 && std::abs(dz->get_value() - n_eingetragen) > 0.5;
        if (begrenzt && wm == "hand") {
            double f = dz->get_value() / n_eingetragen;
            vs->set_value(zehner(num(we, "vorschub", 1000) * f));
            ei->set_value(zehner(num(we, "eintauchen", 300) * f));
        }
        nr->set_value(num(w, "nummer", 1));
        nr->set_sensitive(!ist_db(key));
        {
            bj::object wl_ = w;
            if (ist_db(key)) wl_["db_id"] = key.substr(3);
            lz_schluessel = laufzeit_schluessel(wl_);
            double lz = laufzeit(wl_);
            long min_ = (long)std::lround(lz);
            lz_l->set_text(lz <= 0 ? TR("noch nicht gefräst")
                                   : TR("gefräst: ") + (min_ >= 60 ? std::to_string(min_ / 60) + " h " : std::string()) +
                                         std::to_string(min_ % 60) + TR(" min seit dem Einsetzen"));
            lz_neu->set_visible(lz > 0);
        }
        sichtbar_setzen();
        lade = false;
        sd_hinweis->set_text(!begrenzt ? (wm == "auto" ? erklaerung(key) : std::string())
                             : n_eingetragen > n_max
                                 ? TRF("Eingetragen waren {} U/min – die Maschine dreht höchstens {} U/min.",
                                       {ganz(n_eingetragen), ganz(n_max)})
                                 : TRF("Eingetragen waren {} U/min – die Maschine dreht mindestens {} U/min.",
                                       {ganz(n_eingetragen), ganz(n_min)}));
        drehzahl_rechnen();
        // In dieser Zeichnung
        if (ist_db(key)) {
            hier_box->hide();
        } else {
            bool nur = flag(w, "nur_hier", false);
            std::string wer;
            if (auto b = benutzt.find(key); b != benutzt.end()) {
                for (auto const &x : b->second) wer += (wer.empty() ? "" : ", ") + x;
            }
            std::string t = TR("Platz ") + key + " · " + (wer.empty() ? TR("nicht benutzt") : TR("benutzt von ") + wer) +
                            ". " + TR("Werkzeugnummer für Steuerungen mit einer Datei je Werkzeug: ") +
                            ganz(num(w, "nummer", 1));
            if (nur) t += std::string("\n") + TR("Nur noch in dieser Zeichnung – nicht in «Meine Werkzeuge». Änderungen gelten "
                                    "nur hier.");
            hier_text->set_text(t);
            klasse(*hier_box, "hirch-warn", nur);
            hier_box->set_no_show_all(false);
            hier_box->show_all();
            hier_knoepfe->set_visible(nur);
        }
    };
    // Liste «eigene Schnittdaten je Werkstoff» mit Zurücksetzen
    std::function<void()> je_zeigen;
    std::function<void(std::string const &)> zeigen_f;
    auto modus_text = [](bj::object const &e) -> std::string {
        auto m = schnittmodus(e);
        if (m == "auto") return TR("automatisch");
        if (m == "drehzahl") return TRF("Drehzahl von Hand {} U/min", {ganz(num(e, "drehzahl", 0))});
        return TRF("von Hand {} U/min · {} mm/min", {ganz(num(e, "drehzahl", 0)), ganz(num(e, "vorschub", 0))});
    };
    je_zeigen = [&] {
        UI::remove_all_children(*je_box);
        auto it = arbeit.find(aktuell);
        if (it == arbeit.end()) return;
        auto const &w = it->value().as_object();
        std::vector<std::pair<std::string, std::string>> zeilen;      // Werkstoff-Schlüssel ("" = alle anderen), Text
        if (auto je = unter(w, "je_werkstoff")) {
            for (auto const &kv : *je) {
                if (auto e = kv.value().if_object()) {
                    bj::object e2 = *e;
                    if (str(e2, "schnittdaten").empty()) e2["schnittdaten"] = "auto";
                    std::string k(kv.key());
                    zeilen.emplace_back(k, werkstoff_von(k) + ": " + modus_text(e2) + (k == mat ? TR(" (diese Zeichnung)") : ""));
                }
            }
        }
        if (schnittmodus(w) != "auto")
            zeilen.emplace_back("", TR("alle anderen Werkstoffe: ") + modus_text(w));
        if (zeilen.empty()) {
            auto l = label(TRF("Für {} gilt: automatisch. Was du hier änderst, merkt sich der Fräser nur für diesen "
                               "Werkstoff.", {werkstoff_von(mat)}), "hirch-dim");
            klasse(*l, "hirch-klein");
            l->set_line_wrap(true);
            l->set_max_width_chars(52);
            je_box->add(*l);
            je_box->show_all();
            return;
        }
        auto kopf_ = label(TR("Eigene Schnittdaten dieses Fräsers"), "hirch-dim");
        klasse(*kopf_, "hirch-klein");
        je_box->add(*kopf_);
        for (auto const &[k, text] : zeilen) {
            auto z = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
            auto l = label(text);
            klasse(*l, "hirch-klein");
            l->set_hexpand(true);
            l->set_ellipsize(Pango::ELLIPSIZE_END);
            z->add(*l);
            auto weg = Gtk::make_managed<Gtk::Button>();
            weg->set_relief(Gtk::RELIEF_NONE);
            weg->set_image_from_icon_name("edit-clear", Gtk::ICON_SIZE_MENU);
            weg->set_tooltip_text(k.empty() ? TR("Zurücksetzen: alle anderen Werkstoffe wieder automatisch")
                                            : TR("Zurücksetzen: für diesen Werkstoff wieder die allgemeinen Angaben"));
            std::string key = aktuell, kk = k;
            weg->signal_clicked().connect([&, key, kk] {
                Glib::signal_idle().connect_once([&, key, kk] {
                    auto it2 = arbeit.find(key);
                    if (it2 == arbeit.end()) return;
                    auto &w2 = it2->value().as_object();
                    if (kk.empty()) {
                        w2["schnittdaten"] = "auto";
                        w2["auto"] = true;
                    } else if (auto je = w2.if_contains("je_werkstoff"); je && je->is_object()) {
                        je->as_object().erase(kk);
                        if (je->as_object().empty()) w2.erase("je_werkstoff");
                    }
                    bearbeitet.insert(key);
                    aendern(true);
                    zeigen_f(key);
                });
            });
            z->add(*weg);
            je_box->add(*z);
        }
        je_box->show_all();
    };
    auto auffrischen = [&] {
        // Name als Vorschlag, Zusammenfassung, Skizze
        auto it = arbeit.find(aktuell);
        if (it == arbeit.end()) return;
        bj::object ohne_name = it->value().as_object();
        ohne_name["name"] = "";
        name->set_placeholder_text(werkzeug_name(ohne_name));
        auto const &q = *wirksam(aktuell);
        auto modus = schnittmodus(fuer_werkstoff(it->value().as_object(), mat));
        zusammenfassung->set_text(modus == "auto"
                                      ? TR("Automatisch: ") + ganz(num(q, "drehzahl", 18000)) + TR(" U/min · ") +
                                            ganz(num(q, "vorschub", 1000)) + " mm/min"
                                  : modus == "drehzahl"
                                      ? TR("Drehzahl von Hand: ") + ganz(dz->get_value()) + TR(" U/min · ") +
                                            ganz(vs->get_value()) + " mm/min"
                                      : TR("Eigene Schnittdaten"));
        // automatische Werte nachführen (Ø, Schaft, Schneidenlänge geändert), blass zeigen
        {
            auto const &w = it->value().as_object();
            lade = true;
            if (ue_auto) ue->set_value(werkzeug_uebergang(w));
            if (al_auto) al->set_value(werkzeug_ausspann_auto(w));
            lade = false;
            for (auto [s_, an] : {std::pair{al, al_auto}, std::pair{ue, ue_auto}}) {
                if (an) s_->get_style_context()->add_class("hirch-dim");
                else s_->get_style_context()->remove_class("hirch-dim");
            }
            if (auto l = dynamic_cast<Gtk::Label *>(grid->get_child_at(2, z_al)))
                l->set_text(al_auto ? TR("mm · automatisch: Schneide + Übergang + 5 mm") : TR("mm · von Hand"));
            if (auto l = dynamic_cast<Gtk::Label *>(grid3->get_child_at(2, z_ue)))
                l->set_text(ue_auto ? TR("mm · automatisch") : TR("mm · von Hand"));
            klasse(*gl, "hirch-dim", num(w, "gesamtlaenge", 0) <= 0);
            if (auto l = dynamic_cast<Gtk::Label *>(grid->get_child_at(2, z_gl)))
                l->set_text(num(w, "gesamtlaenge", 0) > 0 ? TR("mm") : TR("mm · nicht angegeben"));
            // Prüfung der Angaben
            auto t = str(w, "typ", "schaft");
            double sl_ = num(w, "schneidenlaenge", 20), u_ = werkzeug_uebergang(w), a_ = werkzeug_ausspann(w);
            std::vector<std::string> h;
            if (!al_auto && t != "v" && a_ < sl_ + u_ - 1e-6) {
                h.push_back(u_ > 0
                    ? TRF("Ausspannlänge {} mm ist kürzer als Schneide + Übergang ({} + {} = {} mm). Die "
                          "Spannmutter sässe auf dem Kegel – bitte nachmessen.",
                          {zahl(a_, 1), zahl(sl_, 1), zahl(u_, 1), zahl(sl_ + u_, 1)})
                    : TRF("Ausspannlänge {} mm ist kürzer als die Schneidenlänge ({} mm). Die Spannmutter sässe "
                          "auf der Schneide – bitte nachmessen.", {zahl(a_, 1), zahl(sl_, 1)}));
            }
            if (double g_ = num(w, "gesamtlaenge", 0); g_ > 0 && g_ < a_ - 1e-6)
                h.push_back(TRF("Gesamtlänge {} mm ist kürzer als die Ausspannlänge ({} mm) – bitte nachmessen.",
                                {zahl(g_, 1), zahl(a_, 1)}));
            else if (g_ > 0 && g_ < sl_ - 1e-6)
                h.push_back(TRF("Gesamtlänge {} mm ist kürzer als die Schneidenlänge ({} mm) – bitte nachmessen.",
                                {zahl(g_, 1), zahl(sl_, 1)}));
            if (t == "v" && num(w, "spitze", 0) >= num(w, "durchmesser", 6) - 1e-6)
                h.push_back(TR("Die flache Spitze ist so breit wie der Fräser – so schneidet er nicht als "
                               "V-Fräser. Bitte Spitze und Durchmesser prüfen."));
            if (t != "v" && t != "bohrer" && !ue_auto && u_ > 0 && std::abs(num(w, "schaft", 0) -
                                                                           num(w, "durchmesser", 0)) < 0.01)
                h.push_back(TR("Schaft und Schneide sind gleich dick – ein Übergang ist hier nicht nötig."));
            std::string txt;
            for (auto const &z : h) txt += (txt.empty() ? "" : "\n") + z;
            pruef_hinweis->set_text(txt);
            if (txt.empty()) pruef_hinweis->hide(); else pruef_hinweis->show();
        }
        skizze->queue_draw();
        je_zeigen();
    };
    skizze->signal_draw().connect([&](Cairo::RefPtr<Cairo::Context> const &cr) {
        auto it = arbeit.find(aktuell);
        if (it == arbeit.end()) return true;
        auto farbe = skizze->get_style_context()->get_color(skizze->get_state_flags());   // hell und dunkel
        werkzeug_skizze(cr, skizze->get_allocated_width(), skizze->get_allocated_height(),
                        it->value().as_object(), true, &farbe);
        return true;
    });
    zeigen_f = [&](std::string const &k) {
        lade = true;
        liste_fuellen();
        lade = false;
        zeigen(k);
        auffrischen();
    };
    // Arten in fester Reihenfolge; beim Wählen die passenden zuerst, die übrigen blass dahinter
    struct ArtK { char const *key, *name, *bild; int n; };
    static ArtK const ARTEN_K[] = {{"schaft", N_("Schaftfräser"), "schaft", 2}, {"kugel", N_("Kugelfräser"), "kugel", 2},
                                   {"eckradius", N_("Eckradius"), "eck", 2}, {"konisch", N_("Konische"), "konisch", 2},
                                   {"v", N_("V-Fräser · Stichel"), "v", 2}, {"plan", N_("Planfräser"), "plan", 3},
                                   {"bohrer", N_("Bohrer"), "bohrer", 2}, {"gewinde", N_("Gewindefräser"), "gewinde", 1}};
    auto art_typ = [](std::string const &g) { return g == "plan" ? std::string("schaft") : g; };
    auto art_passt = [&, wahl](std::string const &g) {
        return !wahl || wz_passend(wz_kontext, art_typ(g), wahl->bohrer) || (g == "plan" && wz_passend(wz_kontext, "schaft", wahl->bohrer));
    };
    // beim Wählen: was die Bearbeitung nicht nehmen kann, gar nicht zeigen (wie bisher im Feld)
    auto art_zeigen = [wahl](std::string const &g) {
        if (!wahl) return true;
        if (g == "bohrer" && !wahl->bohrer) return false;
        if (g == "v" && wahl->mit_keinem) return false;
        return true;
    };
    auto bild_flaeche = [](fraeserbild::Merkmale m, int bw, int bh, double alpha) {
        auto da = Gtk::make_managed<Gtk::DrawingArea>();
        da->set_size_request(bw, bh);
        da->set_valign(Gtk::ALIGN_CENTER);
        da->signal_draw().connect([da, m, alpha](Cairo::RefPtr<Cairo::Context> const &cr) {
            auto f = da->get_style_context()->get_color(da->get_state_flags());
            bool hell = (f.get_red() + f.get_green() + f.get_blue()) / 3 < 0.5;
            fraeserbild::zeichnen(cr, da->get_allocated_width(), da->get_allocated_height(), m, hell, alpha);
            return true;
        });
        return da;
    };
    auto schild = [](std::string const &platz, bool nur, bool klein) {
        auto l = label(platz, "hirch-wz-schild");
        if (nur) klasse(*l, "hirch-wz-schild-nur");
        if (klein) klasse(*l, "hirch-wz-schild-klein");
        l->set_valign(Gtk::ALIGN_CENTER);
        l->set_tooltip_text(nur ? TR("Platz in dieser Zeichnung – nur hier, nicht in «Meine Werkzeuge»")
                                : TR("Platz in dieser Zeichnung"));
        return l;
    };
    auto masse_text = [](bj::object const &w) {
        auto t = str(w, "typ", "schaft");
        // Zoll: jede Länge mit Einheit («Ø 1/4 Zoll · R 1/8 Zoll»); mm wie bisher
        auto k = [](double v) { return einheit_mass(v, kurzzahl(v)); };
        auto mm = [](char const *t) { return hirch_zoll() ? std::string() : std::string(TR(t)); };
        double d = num(w, "durchmesser", 3), s_ = num(w, "schaft", d);
        if (t == "v") return kurzzahl(num(w, "v_winkel", 60)) + "°" +
                             (num(w, "spitze", 0) > 0 ? TR(" · Spitze ") + k(num(w, "spitze", 0)) : " · Ø " + k(d) + mm(" mm"));
        if (t == "konisch") return "R " + k(num(w, "eckradius", 0.25)) + " · " + kurzzahl(num(w, "v_winkel", 3.6)) + "°";
        if (t == "kugel") return "Ø " + k(d) + " · R " + k(d / 2);
        if (t == "eckradius") return "Ø " + k(d) + " · R " + k(num(w, "eckradius", 1));
        if (t == "bohrer") return "Ø " + k(d) + " · " + kurzzahl(num(w, "spitzenwinkel", 118)) + "°";
        return "Ø " + k(d) + mm(" mm") + (std::abs(s_ - d) > 0.01 ? TR(" · Schaft ") + k(s_) : std::string());
    };
    auto unter_text = [](bj::object const &w) {
        auto m = fraeser_merkmale(w);
        std::string t = m.schneiden == 1 ? TR("1 Schneide") : TRF("{} Schneiden", {std::to_string(m.schneiden)});
        if (m.drall == 'L') t += TR(" · Drall abwärts");
        else if (m.drall == 'K') t += TR(" · Kompression");
        if (m.art == "plan") t += TR(" · Wendeplatten");
        if (m.art == "schrupp") t += TR(" · Spanbrecher");
        return t;
    };
    std::function<void()> arten_fuellen;
    arten_fuellen = [&] {
        UI::remove_all_children(*arten_box);
        std::vector<ArtK const *> reihe;
        for (int runde = 0; runde < 2; ++runde) {
            for (auto const &a : ARTEN_K) {
                if (art_passt(a.key) == (runde == 0)) reihe.push_back(&a);
            }
        }
        bool suche = !such->get_text().empty();
        for (auto const *a : reihe) {
            if (!art_zeigen(a->key)) continue;
            int alle = 0;
            std::string hier;
            for (auto const &kv : arbeit) {
                auto const &w = kv.value().as_object();
                if (fraeser_gruppe(w) != a->key) continue;
                ++alle;
                std::string k(kv.key());
                // B-24 (Peter 03.11.): Buchstaben nur für Fräser, die eine Bearbeitung benutzt
                if (!ist_db(k) && benutzt.count(k)) hier += (hier.empty() ? "" : " ") + k;
            }
            bool ok = art_passt(a->key) && alle > 0;
            auto k = Gtk::make_managed<Gtk::Button>();
            klasse(*k, "hirch-wz-karte");
            klasse(*k, "hirch-wz-art");
            if (!suche && art_gewaehlt == a->key) klasse(*k, "hirch-wz-gewaehlt");
            auto kb = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 10);
            fraeserbild::Merkmale m;
            m.art = a->bild;
            m.schneiden = a->n;
            kb->add(*bild_flaeche(m, 16, 48, ok ? 1.0 : 0.45));
            auto tb = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 3);
            tb->set_valign(Gtk::ALIGN_CENTER);
            auto nl = label(TR(a->name), ok ? "hirch-titel" : "hirch-dim");
            nl->set_xalign(0);
            nl->set_max_width_chars(14);
            nl->set_line_wrap(true);
            tb->add(*nl);
            auto zb = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 3);
            auto zl = label(TRF("{} Fräser", {std::to_string(alle)}), "hirch-dim");
            klasse(*zl, "hirch-klein");
            zb->add(*zl);
            for (auto const &p_ : Glib::Regex::split_simple(" ", hier)) {
                if (!p_.empty()) zb->add(*schild(p_.raw(), false, true));
            }
            tb->add(*zb);
            kb->add(*tb);
            k->add(*kb);
            if (!ok) k->set_tooltip_text(TR("Weniger passend für diese Bearbeitung – wählbar bleibt es"));
            std::string key = a->key;
            k->signal_clicked().connect([&, key] {
                Glib::signal_idle().connect_once([&, key] {
                    lesen();
                    art_gewaehlt = key;
                    lade = true;
                    such->set_text("");
                    lade = false;
                    // erster Fräser der Art: einer dieser Zeichnung, sonst der kleinste
                    std::string erst;
                    double dmin = 1e9;
                    for (auto const &kv : arbeit) {
                        auto const &w = kv.value().as_object();
                        if (fraeser_gruppe(w) != key) continue;
                        std::string kk(kv.key());
                        double d = num(w, "durchmesser", 3) + (ist_db(kk) ? 1000 : 0);
                        if (d < dmin) { dmin = d; erst = kk; }
                    }
                    aktuell = erst;          // leer: diese Art hat noch keinen Fräser
                    zeigen_f(aktuell);
                });
            });
            arten_box->add(*k);
        }
        arten_box->show_all();
    };
    liste_fuellen = [&] {
        UI::remove_all_children(*wl);
        if (!aktuell.empty()) {
            if (auto it = arbeit.find(aktuell); it != arbeit.end()) art_gewaehlt = fraeser_gruppe(it->value().as_object());
        }
        arten_fuellen();
        auto suche = Glib::ustring(such->get_text()).lowercase().raw();
        for (auto const &a : ARTEN_K) {
            if (art_gewaehlt == a.key) sp_name->set_text(suche.empty() ? Glib::ustring(TR(a.name)).uppercase() : TR("TREFFER"));
        }
        // diese Zeichnung zuerst, dann nach Grösse
        std::vector<std::pair<std::string, bj::object const *>> reihe;
        for (auto const &kv : arbeit) {
            auto const &w = kv.value().as_object();
            auto g = fraeser_gruppe(w);
            if (!art_zeigen(g)) continue;
            if (suche.empty() ? g != art_gewaehlt
                              : Glib::ustring(werkzeug_name(w) + " " + masse_text(w)).lowercase().raw().find(suche) == std::string::npos)
                continue;
            reihe.emplace_back(std::string(kv.key()), &w);
        }
        std::stable_sort(reihe.begin(), reihe.end(), [&](auto const &x, auto const &y) {
            bool hx = !ist_db(x.first), hy = !ist_db(y.first);
            if (hx != hy) return hx;
            if (hx) return x.first < y.first;
            return num(*x.second, "durchmesser", 3) < num(*y.second, "durchmesser", 3);
        });
        int i = 0, wahl_i = -1;
        for (auto const &[k, wp] : reihe) {
            auto const &w = *wp;
            bool db_ = ist_db(k), nur = !db_ && flag(w, "nur_hier", false);
            auto row = Gtk::make_managed<Gtk::ListBoxRow>();
            auto rz = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
            klasse(*rz, "hirch-wz-zeile");
            rz->add(*bild_flaeche(fraeser_merkmale(w), 16, 48, art_passt(fraeser_gruppe(w)) ? 1.0 : 0.5));
            auto rb = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 1);
            rb->set_valign(Gtk::ALIGN_CENTER);
            rb->set_hexpand(true);
            auto tl = label(werkzeug_name_mit_art(w), "hirch-titel");
            tl->set_xalign(0);
            tl->set_ellipsize(Pango::ELLIPSIZE_END);
            tl->set_max_width_chars(1);      // gleiche Spaltenbreite in allen Zeilen (Rest füllt hexpand)
            tl->set_tooltip_text(werkzeug_name_mit_art(w));
            rb->add(*tl);
            std::string u;
            char const *uk = "hirch-dim";
            if (nur) {
                u = TR("nur in dieser Zeichnung");
                uk = "hirch-warntext";
            } else if (!db_) {
                std::string wer;
                if (auto b = benutzt.find(k); b != benutzt.end()) {
                    for (auto const &x : b->second) wer += (wer.empty() ? "" : ", ") + x;
                }
                u = wer.empty() ? TR("hier nicht benutzt") : TR("benutzt: ") + wer;
                if (!wer.empty()) uk = "hirch-benutzt";
            } else {
                u = unter_text(w);
            }
            auto ul = label(u, uk);
            klasse(*ul, "hirch-klein");
            ul->set_xalign(0);
            ul->set_ellipsize(Pango::ELLIPSIZE_END);
            ul->set_max_width_chars(1);
            ul->set_tooltip_text(u);
            rb->add(*ul);
            rz->add(*rb);
            auto ml = label(masse_text(w));
            klasse(*ml, "hirch-klein");
            ml->set_xalign(0);
            ml->set_size_request(115, -1);
            rz->add(*ml);
            auto [sd_n, sd_f] = schnitt_anzeige(k);
            auto dl = label(ganz(sd_n) + " · " + ganz(sd_f) + TR(" mm/min"));
            klasse(*dl, "hirch-klein");
            dl->set_xalign(0);
            dl->set_size_request(130, -1);
            rz->add(*dl);
            auto pb = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
            pb->set_size_request(44, -1);
            if (!db_ && benutzt.count(k)) {     // B-24: Schild nur, wenn eine Bearbeitung den Fräser benutzt
                auto sl_ = schild(k, nur, false);
                sl_->set_halign(Gtk::ALIGN_CENTER);
                pb->set_halign(Gtk::ALIGN_FILL);
                pb->set_homogeneous(true);
                sl_->set_valign(Gtk::ALIGN_CENTER);
                pb->add(*sl_);
            }
            rz->add(*pb);
            row->add(*rz);
            row->set_name(k);
            wl->add(*row);
            if (k == aktuell) wahl_i = i;
            ++i;
        }
        if (reihe.empty()) {
            std::string an;
            for (auto const &a : ARTEN_K) {
                if (art_gewaehlt == a.key) an = TR(a.name);
            }
            auto row = Gtk::make_managed<Gtk::ListBoxRow>();
            row->set_selectable(false);
            row->set_activatable(false);
            auto l = label(suche.empty() ? TRF("Noch kein {} in «Meine Werkzeuge». «+ Neuer Fräser» legt einen an.", {an})
                                         : TR("Kein Fräser passt zur Suche."), "hirch-dim");
            l->set_line_wrap(true);
            l->set_margin_top(24);
            l->set_margin_bottom(24);
            row->add(*l);
            wl->add(*row);
        }
        wl->show_all();
        if (wahl_i >= 0) {
            if (auto r = wl->get_row_at_index(wahl_i)) wl->select_row(*r);
        }
    };
    wl->signal_row_selected().connect([&](Gtk::ListBoxRow *row) {
        if (lade) return;
        lesen();
        if (row) zeigen(row->get_name().raw());
        auffrischen();
    });
    if (wahl) {
        wl->signal_row_activated().connect([&](Gtk::ListBoxRow *row) {
            if (row) dlg->response(Gtk::RESPONSE_OK);
        });
    }
    // in der Liste tippen: Buchstaben gehen ins Suchfeld
    wl->signal_key_press_event().connect([&](GdkEventKey *e) {
        auto c = gdk_keyval_to_unicode(e->keyval);
        if (c < 32 || (e->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK))) return false;
        such->grab_focus_without_selecting();
        return (bool)such->handle_event(e);
    }, false);
    such->signal_search_changed().connect([&] {
        if (lade) return;
        lade = true;
        liste_fuellen();
        lade = false;
        // ersten Treffer zeigen, wenn der gewählte Fräser nicht mehr dabei ist
        if (!wl->get_selected_row()) {
            if (auto r = wl->get_row_at_index(0)) {
                lesen();
                wl->select_row(*r);
                zeigen(r->get_name().raw());
                auffrischen();
            }
        }
    });
    auto geaendert = [&] {
        if (lade) return;
        aendern(true);
        sichtbar_setzen();
        drehzahl_rechnen();   // Drehzahl von Hand: Vorschub und Eintauchen sofort mitrechnen
        lesen();
        if (!aktuell.empty()) bearbeitet.insert(aktuell);
        lade = true;
        liste_fuellen();      // Beschriftung der gewählten Zeile auffrischen
        lade = false;
        auffrischen();
    };
    lz_neu->signal_clicked().connect([&] {
        Inkscape::Preferences::get()->setDouble("/hirch/laufzeit/" + lz_schluessel, 0.0);
        lz_l->set_text(TR("noch nicht gefräst"));
        lz_neu->hide();
    });
    typ->signal_changed().connect(geaendert);
    // anderer Durchmesser: die Korrektur je Seite bleibt, «Schneidet wie» folgt
    dm->signal_value_changed().connect([&] {
        if (lade) return;
        lade = true;
        mk->set_value(dm->get_value() + 2 * mk_e);
        lade = false;
    });
    mk->signal_value_changed().connect([&, geaendert] {
        if (lade) return;
        mk_e = (mk->get_value() - dm->get_value()) / 2;
        mk_info = bj::object();
        mk_info["von_hand"] = true;
        klasse(*mk, "hirch-dim", std::abs(mk_e) < 1e-6);
        geaendert();
    });
    mk_mess->signal_clicked().connect([&, geaendert] {
        auto it = arbeit.find(aktuell);
        if (it == arbeit.end()) return;
        bj::object w = it->value().as_object();
        w["durchmesser"] = dm->get_value();
        w["messkorrektur"] = mk_e;
        bj::object info;
        if (auto e = messkorrektur_dialog("", &w, &info, dlg.get())) {
            mk_e = *e;
            mk_info = info;
            lade = true;
            mk->set_value(dm->get_value() + 2 * mk_e);
            lade = false;
            klasse(*mk, "hirch-dim", std::abs(mk_e) < 1e-6);
            geaendert();
        }
    });
    for (auto s : {dm, vw, sp, er, gz, gs, bw, sn, sl, dz, vs, ei, nr, gl}) s->signal_value_changed().connect(geaendert);
    // Ausspannlänge und Übergang: eintippen = von Hand, 0 = wieder automatisch
    al->signal_value_changed().connect([&, geaendert] {
        if (lade) return;
        al_auto = al->get_value() <= 1e-9;
        geaendert();
    });
    ue->signal_value_changed().connect([&, geaendert] {
        if (lade) return;
        ue_auto = ue->get_value() <= 1e-9;
        geaendert();
    });
    name->signal_changed().connect(geaendert);
    mitte->signal_toggled().connect(geaendert);
    drall->signal_changed().connect(geaendert);
    wendeplatten->signal_toggled().connect(geaendert);
    spanbrecher->signal_toggled().connect(geaendert);
    smodus->signal_changed().connect(geaendert);
    sd_->signal_value_changed().connect(geaendert);

    auto freier_platz = [&]() -> std::string {
        for (char c = 'A'; c <= 'Z'; ++c) {
            if (arbeit.find(std::string(1, c)) == arbeit.end()) return std::string(1, c);
        }
        return "";
    };
    auto waehlen = [&](std::string const &key) {      // nach jeder Änderung an der Liste
        aendern(true);
        aktuell = key;
        lade = true;
        liste_fuellen();
        lade = false;
        zeigen(key);
        auffrischen();
    };
    // neuer Fräser: immer in «Meine Werkzeuge»; mit in_zeichnung zugleich als Platz dieser Zeichnung
    auto hinzufuegen = [&](bj::object w, bool in_zeichnung) {
        lesen();
        auto id = neue_id();
        std::string key;
        w.erase("nur_hier");
        w.erase("id");
        if (in_zeichnung) {
            key = freier_platz();
            if (key.empty()) return;
            w["db_id"] = id;
        } else {
            key = "db:" + id;
            w.erase("db_id");
            w.erase("nummer");
        }
        arbeit[key] = std::move(w);
        bearbeitet.insert(key);
        waehlen(key);
        name->grab_focus();
    };
    // Fräser aus «Meine Werkzeuge» als Platz in die Zeichnung (ohne ihn zu ändern)
    auto in_platz = [&](std::string const &db_key) {
        lesen();
        auto key = freier_platz();
        auto it = arbeit.find(db_key);
        if (key.empty() || it == arbeit.end()) return;
        bj::object w = it->value().as_object();
        w["db_id"] = db_key.substr(3);
        w["nummer"] = 1;
        bool war = bearbeitet.count(db_key) > 0;
        arbeit.erase(db_key);
        arbeit[key] = std::move(w);
        if (war) bearbeitet.insert(key);
        waehlen(key);
    };
    // «+ Neuer Fräser»: Arten mit sinnvollen Startwerten
    std::map<std::string, std::function<bj::object()>> erzeuger;
    auto neu_eintrag = [&](std::string const &text, std::function<bj::object()> erzeugen) {
        erzeuger[text] = erzeugen;
        auto mi = Gtk::make_managed<Gtk::MenuItem>(text);
        mi->signal_activate().connect([&, erzeugen] { hinzufuegen(erzeugen(), false); });
        neu_menue->append(*mi);
    };
    auto start = [](std::string const &t, double d, double winkel, double spitze, double sl_) {
        bj::object w = vorgabe_werkzeug(t == "v");
        w["typ"] = t;
        w["durchmesser"] = d;
        w["v_winkel"] = winkel;
        w["spitze"] = spitze;
        w["eckradius"] = t == "kugel" ? d / 2 : (t == "eckradius" ? 1.0 : 0.0);
        w["schneidenlaenge"] = sl_;
        w["schaft"] = t == "v" ? d : (d <= 3 ? 3.0 : d <= 6 ? 6.0 : 8.0);
        w["schneiden"] = t == "v" ? 1 : 2;
        w["mittenschnitt"] = true;
        w["auto"] = true;
        w["schnittdaten"] = "auto";
        w["name"] = "";
        return w;
    };
    neu_eintrag(TR("Gravierstichel"), [=] { return start("v", 3.0, 30, 0.2, 5); });
    neu_eintrag(TR("V-Fräser"), [=] { return start("v", 12.0, 60, 0, 11); });
    neu_eintrag(TR("Schaftfräser"), [=] { return start("schaft", 3.0, 60, 0, 12); });
    neu_eintrag(TR("Kugelfräser"), [=] { return start("kugel", 3.0, 60, 0, 12); });
    neu_eintrag(TR("Eckradiusfräser"), [=] { return start("eckradius", 6.0, 60, 0, 20); });
    neu_eintrag(TR("Konischer Kugelfräser"), [=] {
        auto w = start("konisch", 1.45, 3.6, 0, 15);      // R0,25, 3,6°, 15 mm Kegel
        w["eckradius"] = 0.25;
        return w;
    });
    neu_eintrag(TR("Gewindefräser"), [=] {
        auto w = start("gewinde", 4.8, 60, 0, 12);
        w["zaehne"] = 1;
        w["steigung"] = 1.0;
        w["schneiden"] = 3;
        w["mittenschnitt"] = false;
        return w;
    });
    neu_eintrag(TR("Bohrer"), [=] {
        auto w = start("bohrer", 5.0, 60, 0, 52);
        w["spitzenwinkel"] = 118.0;
        w["schneiden"] = 2;
        return w;
    });
    neu_menue->show_all();
    // «Anderen wählen»: einen Platz, dessen Fräser nicht mehr in «Meine Werkzeuge» ist, neu belegen
    for (auto const &[id, o] : db_alt) {
        auto mi = Gtk::make_managed<Gtk::MenuItem>(werkzeug_name(o));
        mi->signal_activate().connect([&, id = id] {
            if (ist_db(aktuell) || db_weg.count(id)) return;
            auto it = arbeit.find(aktuell);
            if (it == arbeit.end()) return;
            double nr_ = num(it->value().as_object(), "nummer", 1);
            bj::object w = db_alt[id];
            w.erase("id");
            w.erase("notiz");
            schnittdaten_uebernehmen(w);
            bearbeitet.erase(aktuell);
            if (auto d = arbeit.find("db:" + id); d != arbeit.end()) {
                if (bearbeitet.count("db:" + id)) {     // dort schon geändert: diese Werte gelten
                    w = d->value().as_object();
                    bearbeitet.insert(aktuell);
                }
                arbeit.erase("db:" + id);
            }
            w["db_id"] = id;
            w["nummer"] = nr_;
            arbeit[aktuell] = std::move(w);
            waehlen(aktuell);
        });
        anderer_menue->append(*mi);
    }
    anderer_menue->show_all();
    wieder->signal_clicked().connect([&] {
        lesen();
        auto it = arbeit.find(aktuell);
        if (it == arbeit.end() || ist_db(aktuell)) return;
        auto &w = it->value().as_object();
        auto id = str(w, "db_id");
        if (id.empty()) id = neue_id();
        db_weg.erase(id);
        w.erase("nur_hier");
        w["db_id"] = id;
        bearbeitet.insert(aktuell);
        waehlen(aktuell);
    });
    kopie->signal_clicked().connect([&] {
        lesen();
        auto it = arbeit.find(aktuell);
        if (it == arbeit.end()) return;
        bj::object w = it->value().as_object();
        if (!str(w, "name").empty()) w["name"] = str(w, "name") + TR(" (Kopie)");
        hinzufuegen(w, false);
    });
    minus->signal_clicked().connect([&] {
        lesen();
        auto it = arbeit.find(aktuell);
        if (it == arbeit.end()) return;
        auto &w = it->value().as_object();
        std::string nm = werkzeug_name(w);
        bool db_ = ist_db(aktuell), nur = !db_ && flag(w, "nur_hier", false);
        bool wird_benutzt = benutzt.count(aktuell) > 0;
        int plaetze = 0;
        for (auto const &kv : arbeit) plaetze += ist_db(std::string(kv.key())) ? 0 : 1;
        if (!db_ && plaetze <= 1 && nur) return;            // die Zeichnung braucht ein Werkzeug
        std::string titel = nur ? TRF("«{}» aus dieser Zeichnung entfernen?", {nm})
                                : TRF("«{}» aus «Meine Werkzeuge» löschen?", {nm});
        std::string text = db_ ? TR("Er ist dann in keiner Zeichnung mehr wählbar. Zeichnungen, die ihn schon "
                                    "benutzen, behalten ihn.")
                         : nur ? (wird_benutzt ? TR("Die Bearbeitungen, die ihn benutzen, bekommen das erste "
                                                    "Werkzeug der Zeichnung.")
                                               : TR("Er wird hier nicht benutzt."))
                         : wird_benutzt || plaetze <= 1
                               ? TRF("Diese Zeichnung benutzt ihn (Platz {}) und behält ihn – als «nur noch in "
                                     "dieser Zeichnung».", {aktuell})
                               : TR("Er wird auch aus dieser Zeichnung entfernt (hier nicht benutzt). Andere "
                                    "Zeichnungen, die ihn benutzen, behalten ihn.");
        Gtk::MessageDialog m(*dlg, titel, false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
        m.set_title("Hirch CAM");
        m.add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
        klasse(*m.add_button(nur ? TR("Entfernen") : TR("Löschen"), Gtk::RESPONSE_OK), "destructive-action");
        m.set_secondary_text(text);
        if (m.run() != Gtk::RESPONSE_OK) return;
        std::string const gruppe = fraeser_gruppe(w);      // danach in dieser Art bleiben (Peter 27.10.)
        if (db_) {
            db_weg.insert(aktuell.substr(3));
            arbeit.erase(aktuell);
        } else if (nur) {
            arbeit.erase(aktuell);
        } else {
            db_weg.insert(str(w, "db_id"));
            if (wird_benutzt || plaetze <= 1) {
                w["nur_hier"] = true;
                bearbeitet.erase(aktuell);
            } else {
                arbeit.erase(aktuell);
            }
        }
        // nächster Fräser derselben Art (einer dieser Zeichnung, sonst der kleinste); gibt es keinen mehr, bleibt
        // die Art gewählt und zeigt sich leer – nicht in eine andere Art springen
        std::string naechst;
        if (arbeit.find(aktuell) != arbeit.end()) {
            naechst = aktuell;
        } else {
            double dmin = 1e9;
            for (auto const &kv : arbeit) {
                auto const &w2 = kv.value().as_object();
                if (fraeser_gruppe(w2) != gruppe) continue;
                std::string kk(kv.key());
                double d = num(w2, "durchmesser", 3) + (ist_db(kk) ? 1000 : 0);
                if (d < dmin) { dmin = d; naechst = kk; }
            }
        }
        art_gewaehlt = gruppe;
        waehlen(naechst);
    });

    for (auto const &kv : arbeit) {
        if (!ist_db(std::string(kv.key()))) { aktuell = kv.key(); break; }
    }
    if (aktuell.empty() && !arbeit.empty()) aktuell = arbeit.begin()->key();
    if (!start_key.empty() && arbeit.find(start_key) != arbeit.end()) aktuell = start_key;
    lade = true;
    liste_fuellen();
    lade = false;
    zeigen(aktuell);
    if (!neu_text.empty()) {
        auto it = erzeuger.find(neu_text);
        if (it != erzeuger.end()) {
            auto w = it->second();
            if (neu_durchmesser > 0) w["durchmesser"] = neu_durchmesser;
            hinzufuegen(w, true);          // neu für diese Zeichnung: auch in «Meine Werkzeuge»
        } else {
            // aus «Meine Werkzeuge» (Panel: «+ Neu» › Name)
            auto n = neu_text.substr(std::min(neu_text.find_first_not_of(' '), neu_text.size()));
            for (auto const &kv : arbeit) {
                std::string k(kv.key());
                if (ist_db(k) && werkzeug_name(kv.value().as_object()) == n) {
                    in_platz(k);
                    break;
                }
            }
        }
    }
    neu_durchmesser = 0;

    bool keiner_gewaehlt = false;
    if (keiner) {
        keiner->signal_clicked().connect([&] {
            keiner_gewaehlt = true;
            dlg->response(Gtk::RESPONSE_OK);
        });
    }
    if (wahl) {
        // Abbrechen · Übernehmen · Wählen: Änderungen am Fräser speichern, ohne zu schliessen; geändert und nicht
        // übernommen steht links daneben (Peter, 22.10.)
        aend_hinweis = Gtk::make_managed<Gtk::Label>("");
        klasse(*aend_hinweis, "hirch-warntext");
        aend_hinweis->set_xalign(0);
        dlg->add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
        ueb_knopf = dlg->add_button(TR("Übernehmen"), Gtk::RESPONSE_APPLY);
        ok_knopf = dlg->add_button(TR("Wählen"), Gtk::RESPONSE_OK);
        if (auto bb = dynamic_cast<Gtk::ButtonBox *>(ok_knopf->get_parent())) {
            bb->pack_start(*aend_hinweis, false, false);
            bb->set_child_secondary(*aend_hinweis, true);
        }
    } else {
        // Verwalten: «Übernehmen» speichert und lässt das Fenster offen (wird grau bis zur nächsten
        // Änderung), «Schliessen» schliesst (Peter, 20.10.)
        dlg->add_button(TR("Schliessen"), Gtk::RESPONSE_CLOSE);
        ok_knopf = dlg->add_button(TR("Übernehmen"), Gtk::RESPONSE_APPLY);
    }
    klasse(*ok_knopf, "suggested-action");
    dlg->set_default_response(wahl ? Gtk::RESPONSE_OK : Gtk::RESPONSE_APPLY);
    dlg->show_all();
    sichtbar_setzen();          // nach show_all: nicht passende Zeilen wieder ausblenden
    zeigen(aktuell);            // «In dieser Zeichnung» passend ein- oder ausblenden
    auffrischen();
    aendern(veraendert);
    if (auto r = wl->get_selected_row()) wl->grab_focus(), r->grab_focus();   // Enter wählt gleich
    auto uebernehmen = [&] {
    // «Meine Werkzeuge»: geänderte und neue Fräser speichern, gelöschte entfernen – in einem Schritt
    std::map<std::string, bj::object> gespeichert;
    bj::array speichern_l;
    for (auto const &kv : arbeit) {
        std::string key(kv.key());
        auto const &w = kv.value().as_object();
        std::string id = ist_db(key) ? key.substr(3) : (flag(w, "nur_hier", false) ? "" : str(w, "db_id"));
        if (id.empty() || !bearbeitet.count(key) || db_weg.count(id)) continue;
        bj::object d = w;
        d["id"] = id;
        d.erase("db_id");
        if (str(d, "name").empty()) d["name"] = werkzeug_name(d);
        gespeichert[id] = d;
        speichern_l.push_back(d);
    }
    if (!speichern_l.empty() || !db_weg.empty()) {
        bj::object auftrag;
        auftrag["speichern"] = speichern_l;
        bj::array weg;
        for (auto const &id : db_weg) weg.push_back(bj::value(id));
        auftrag["loeschen"] = weg;
        std::string aus, fehler;
        bj::object r;
        if (python_jetzt({"datenbank", "uebernehmen", bj::serialize(auftrag)}, aus, fehler) && json_lesen(aus, r) &&
            flag(r, "ok", false)) {
            if (auto l = r.if_contains("werkzeuge")) analyse["datenbank"] = *l;
        } else {
            meldung(TR("«Meine Werkzeuge» nicht gespeichert"), (fehler.empty() ? aus : fehler).substr(0, 400),
                    Gtk::MESSAGE_ERROR);
        }
    }
    // Werkzeuge der Zeichnung: die Plätze; wer denselben Fräser hat, bekommt die neuen Werte
    bj::object plaetze;
    std::string erster;
    for (auto const &kv : arbeit) {
        std::string key(kv.key());
        if (ist_db(key)) continue;
        bj::object w = kv.value().as_object();
        auto id = str(w, "db_id");
        if (!flag(w, "nur_hier", false) && gespeichert.count(id)) {
            double nr_ = num(w, "nummer", 1);
            w = gespeichert[id];
            w.erase("id");
            w.erase("notiz");
            w["db_id"] = id;
            w["nummer"] = nr_;
        }
        // Plätze ohne Bearbeitung fallen weg (Peter, 20.10.) – ausser «nur in dieser Zeichnung» (sonst wäre
        // der Fräser ganz verloren) und dem Passloch-Werkzeug der zweiseitigen Bearbeitung
        if (aufraeumen && !flag(w, "nur_hier", false) && !benutzt.count(key)) {
            auto zw = unter(einst, "zweiseitig");
            if (!(zw && str(*zw, "pass_werkzeug") == key)) continue;
        }
        if (erster.empty()) erster = key;
        plaetze[key] = std::move(w);
    }
    if (plaetze.empty()) return;              // ohne Werkzeug bleibt die Zeichnung, wie sie war
    if (auto alt_ws = unter(einst, "werkzeuge"); alt_ws && *alt_ws == plaetze) return;   // nur gewählt: nichts zu schreiben
    // Bearbeitungen, deren Werkzeug hier entfernt wurde, bekommen das erste. Fehlte der Platz schon vorher,
    // bleibt er stehen: das Feld zeigt «fehlt – bitte wählen» statt still ein anderes Werkzeug (Befund 22.10.)
    auto vorher_da = [this](std::string const &k) {
        auto ws = unter(einst, "werkzeuge");
        return ws && ws->contains(k);
    };
    if (auto bea = liste_von(einst, "bearbeitungen")) {
        for (auto &v : *bea) {
            if (auto b = v.if_object()) {
                auto w = str(*b, "werkzeug", "A");
                if (plaetze.find(w) == plaetze.end() && vorher_da(w)) (*b)["werkzeug"] = erster;
                auto f = str(*b, "flach_werkzeug");
                if (!f.empty() && plaetze.find(f) == plaetze.end()) (*b)["flach_werkzeug"] = "";
                auto r = str(*b, "raeum_werkzeug");
                if (!r.empty() && plaetze.find(r) == plaetze.end()) (*b)["raeum_werkzeug"] = "";
            }
        }
    }
    einst["werkzeuge"] = plaetze;
    schreiben(TR("Werkzeuge ändern"));
    };

    while (true) {
        int antwort = dlg->run();
        if (wahl && antwort == Gtk::RESPONSE_OK) {
            if (!keiner_gewaehlt && aktuell.empty()) return;      // nichts gewählt (leere Art)
            lesen();
            wahl->gewaehlt = true;
            wahl->ergebnis = keiner_gewaehlt ? std::string() : aktuell;
            uebernehmen();
            return;
        }
        lesen();
        if (wahl && antwort != Gtk::RESPONSE_APPLY) {
            // Abbrechen: offene Änderungen nicht stillschweigend verwerfen
            if (veraendert) {
                Gtk::MessageDialog m(*dlg, TR("Änderungen übernehmen?"), false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
                m.set_title("Hirch CAM");
                m.set_secondary_text(TR("Seit dem letzten «Übernehmen» wurde an den Fräsern etwas geändert."));
                m.add_button(TR("Zurück"), Gtk::RESPONSE_CANCEL);
                m.add_button(TR("Verwerfen"), Gtk::RESPONSE_NO);
                klasse(*m.add_button(TR("Übernehmen"), Gtk::RESPONSE_YES), "suggested-action");
                m.set_default_response(Gtk::RESPONSE_YES);
                int a = m.run();
                if (a == Gtk::RESPONSE_CANCEL || a == Gtk::RESPONSE_DELETE_EVENT) continue;
                if (a == Gtk::RESPONSE_YES) uebernehmen();
            }
            return;
        }
        if (antwort == Gtk::RESPONSE_APPLY) {
            uebernehmen();
            // Schnittdaten gleich neu, nicht erst mit der nächsten Rechnung der Zeichnung: sonst zeigt die
            // Liste die alten Werte (Sammelauftrag 21.10., Punkt 1)
            {
                bj::object a;
                a["werkzeuge"] = unter(einst, "werkzeuge") ? bj::value(*unter(einst, "werkzeuge")) : bj::value(bj::object());
                a["material"] = mat;
                std::string aus, fehler;
                bj::object r;
                if (python_jetzt({"datenbank", "wirksam", bj::serialize(a)}, aus, fehler) && json_lesen(aus, r) &&
                    flag(r, "ok", false)) {
                    for (char const *k : {"werkzeuge_wirksam", "datenbank_wirksam"})
                        if (auto x = r.if_contains(k)) analyse[k] = *x;
                    if (auto x = r.if_contains("werkzeuge")) analyse["datenbank"] = *x;
                }
            }
            // weiter bearbeiten: Liste aus Zeichnung und Katalog neu, derselbe Fräser bleibt gewählt
            std::string war = aktuell;
            std::string war_db = ist_db(war) ? war.substr(3) : std::string();
            arbeit_aufbauen();
            benutzt = werkzeug_benutzung();
            if (arbeit.find(war) == arbeit.end()) {
                aktuell.clear();
                for (auto const &kv : arbeit) {          // aus «db:…» wurde vielleicht ein Platz (oder umgekehrt)
                    auto const &w = kv.value().as_object();
                    std::string k(kv.key());
                    if ((!war_db.empty() && str(w, "db_id") == war_db) || k == "db:" + str(w, "db_id")) {
                        aktuell = k;
                        break;
                    }
                }
                if (aktuell.empty() && !arbeit.empty()) aktuell = arbeit.begin()->key();
            }
            lade = true;
            liste_fuellen();
            lade = false;
            zeigen(aktuell);
            auffrischen();
            aendern(false);
            continue;
        }
        // Schliessen: noch nicht übernommene Änderungen nachfragen
        if (veraendert) {
            Gtk::MessageDialog m(*dlg, TR("Änderungen übernehmen?"), false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
            m.set_title("Hirch CAM");
            m.set_secondary_text(TR("Seit dem letzten «Übernehmen» wurde an den Fräsern etwas geändert."));
            m.add_button(TR("Zurück"), Gtk::RESPONSE_CANCEL);
            m.add_button(TR("Verwerfen"), Gtk::RESPONSE_NO);
            klasse(*m.add_button(TR("Übernehmen"), Gtk::RESPONSE_YES), "suggested-action");
            m.set_default_response(Gtk::RESPONSE_YES);
            int a = m.run();
            if (a == Gtk::RESPONSE_CANCEL || a == Gtk::RESPONSE_DELETE_EVENT) continue;
            if (a == Gtk::RESPONSE_YES) uebernehmen();
        }
        return;
    }
}

std::map<std::string, std::vector<std::string>> HirchFraesen::Impl::werkzeug_benutzung()
{
    std::map<std::string, std::vector<std::string>> benutzt;
    if (auto bea = liste_von(einst, "bearbeitungen")) {
        for (std::size_t i = 0; i < bea->size(); ++i) {
            auto b = (*bea)[i].if_object();
            if (!b) continue;
            std::string wer = std::to_string(i + 1) + " · " + name_von(*b);
            for (auto const &kv : *b) {
                std::string k(kv.key());
                if (k.size() < 8 || k.compare(k.size() - 8, 8, "werkzeug") != 0 || !kv.value().is_string()) continue;
                std::string w(kv.value().get_string());
                if (w.empty()) continue;
                if (k == "raeum_werkzeug" && !(str(*b, "art") == "tasche")) continue;
                if (k == "schlicht_werkzeug" && !flag(*b, "schlichten", false)) continue;
                std::string zusatz = k == "werkzeug" ? "" : k == "raeum_werkzeug" ? TR(" (Räumen)")
                                   : k == "schlicht_werkzeug" ? TR(" (Schlichten)") : k == "flach_werkzeug" ? TR(" (Flachbereich)") : "";
                auto &v = benutzt[w];
                if (std::find(v.begin(), v.end(), wer + zusatz) == v.end()) v.push_back(wer + zusatz);
            }
        }
    }
    return benutzt;
}

void HirchFraesen::Impl::werkzeug_liste(Gtk::Box *ziel)
{
    UI::remove_all_children(*ziel);
    auto kopf = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
    auto kl = label(TR("WERKZEUGE IN DIESER ZEICHNUNG"), "hirch-kopf");
    kl->set_hexpand(true);
    kl->set_xalign(0);
    kopf->add(*kl);
    auto neu_k = Gtk::make_managed<Gtk::MenuButton>();
    neu_k->set_label(TR("+ Neuer Fräser"));
    neu_k->set_tooltip_text(TR("Neues Werkzeug – Art wählen oder aus «Meine Werkzeuge» übernehmen"));
    auto menue = Gtk::make_managed<Gtk::Menu>();
    auto eintrag = [this, menue](std::string const &text) {
        auto mi = Gtk::make_managed<Gtk::MenuItem>(text);
        mi->signal_activate().connect([this, text] {
            Glib::signal_idle().connect_once([this, text] { werkzeuge_dialog({}, text); });
        });
        menue->append(*mi);
    };
    for (auto t : {TR("Gravierstichel"), TR("V-Fräser"), TR("Schaftfräser"), TR("Kugelfräser"), TR("Eckradiusfräser"), TR("Gewindefräser"),
                   TR("Bohrer")})
        eintrag(t);
    if (auto l = liste_von(analyse, "datenbank"); l && !l->empty()) {
        menue->append(*Gtk::make_managed<Gtk::SeparatorMenuItem>());
        auto k = Gtk::make_managed<Gtk::MenuItem>(TR("Meine Werkzeuge:"));
        k->set_sensitive(false);
        menue->append(*k);
        std::set<std::string> namen;
        for (auto const &v : *l) {
            if (auto o = v.if_object()) namen.insert(werkzeug_name(*o));
        }
        for (auto const &n_ : namen) eintrag("   " + n_);
    }
    menue->show_all();
    neu_k->set_popup(*menue);
    kopf->add(*neu_k);
    ziel->add(*kopf);

    auto ws = unter(einst, "werkzeuge");
    if (!ws || ws->empty()) {
        auto l = label(TR("Noch keine Werkzeuge. «+ Neu» legt eins an."), "hirch-dim");
        l->set_xalign(0);
        ziel->add(*l);
        ziel->show_all();
        return;
    }
    // wo wird welches Werkzeug benutzt (Hauptwerkzeug und Räumen, Schlichten, Flachbereich …)
    auto benutzt = werkzeug_benutzung();
    auto karte = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 0);
    klasse(*karte, "hirch-gruppe");
    auto lb = Gtk::make_managed<Gtk::ListBox>();
    klasse(*lb, "hirch-ops");
    lb->set_selection_mode(Gtk::SELECTION_NONE);
    lb->set_activate_on_single_click(false);
    lb->signal_row_activated().connect([this](Gtk::ListBoxRow *row) {
        if (!row) return;
        std::string k = row->get_name().raw();
        Glib::signal_idle().connect_once([this, k] { werkzeuge_dialog(k); });
    });
    for (auto const &kv : *ws) {
        auto wo = kv.value().if_object();
        if (!wo) continue;
        std::string key(kv.key());
        bj::object w = *wo;
        bj::object q = fuer_werkstoff(w, str(einst, "material", "hartholz"));   // Schnittdaten für den Werkstoff
        if (schnittmodus(q) != "hand") {
            if (auto ww = unter(analyse, "werkzeuge_wirksam")) {
                if (auto x = unter(*ww, key.c_str())) q = *x;
            }
        }
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_name(key);
        auto rz = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 10);
        rz->set_margin_start(10);
        rz->set_margin_end(6);
        rz->set_margin_top(7);
        rz->set_margin_bottom(7);
        auto bild = Gtk::make_managed<Gtk::DrawingArea>();
        bild->set_size_request(34, 56);
        bild->set_valign(Gtk::ALIGN_CENTER);
        bild->signal_draw().connect([bild, w](Cairo::RefPtr<Cairo::Context> const &cr) {
            werkzeug_skizze(cr, bild->get_allocated_width(), bild->get_allocated_height(), w, false);
            return true;
        });
        rz->add(*bild);
        auto tb = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
        tb->set_hexpand(true);
        tb->set_valign(Gtk::ALIGN_CENTER);
        auto t1 = label(werkzeug_name(w), "hirch-titel");
        t1->set_ellipsize(Pango::ELLIPSIZE_END);
        tb->add(*t1);
        std::string sd = ganz(num(q, "drehzahl", 18000)) + TR(" U/min · ") + ganz(num(q, "vorschub", 1000)) +
                         TR(" mm/min · Eintauchen ") + ganz(num(q, "eintauchen", 300)) +
                         (schnittmodus(q) == "auto" ? TR(" · automatisch")
                          : schnittmodus(q) == "drehzahl" ? TR(" · Drehzahl von Hand") : "");
        auto t2 = label(TR("Platz ") + key + " · " + sd, "hirch-dim");
        klasse(*t2, "hirch-klein");
        t2->set_line_wrap(true);
        t2->set_xalign(0);
        tb->add(*t2);
        auto it = benutzt.find(key);
        bool frei = it == benutzt.end();
        std::string bt;
        if (!frei) {
            for (auto const &x : it->second) bt += (bt.empty() ? "" : ", ") + x;
        }
        auto t3 = label(frei ? TR("nicht benutzt") : TR("● benutzt: ") + bt, frei ? "hirch-dim" : "hirch-ok");
        klasse(*t3, "hirch-klein");
        t3->set_line_wrap(true);
        t3->set_xalign(0);
        tb->add(*t3);
        // Laufzeit seit dem letzten Einsetzen (gezählt beim Speichern des G-Codes)
        {
            double lz = laufzeit(w);
            auto lzb = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
            long min_ = (long)std::lround(lz);
            std::string lt = lz <= 0 ? TR("noch nicht gefräst")
                           : TR("gefräst: ") + (min_ >= 60 ? std::to_string(min_ / 60) + " h " : std::string()) +
                                 std::to_string(min_ % 60) + " min";
            auto ll = label(lt, "hirch-dim");
            klasse(*ll, "hirch-klein");
            lzb->add(*ll);
            if (lz > 0) {
                auto neu_ = Gtk::make_managed<Gtk::Button>(TR("Neu eingesetzt"));
                neu_->set_relief(Gtk::RELIEF_NONE);
                klasse(*neu_, "hirch-klein");
                neu_->set_tooltip_text(TR("Zähler auf 0 setzen – z. B. nach dem Schärfen oder mit einem neuen Fräser"));
                neu_->signal_clicked().connect([this, w] {
                    Inkscape::Preferences::get()->setDouble("/hirch/laufzeit/" + laufzeit_schluessel(w), 0.0);
                    Glib::signal_idle().connect_once([this] { werkzeug_reiter_auffrischen(); });
                });
                lzb->add(*neu_);
            }
            tb->add(*lzb);
        }
        rz->add(*tb);
        auto bearb = Gtk::make_managed<Gtk::Button>();
        bearb->set_relief(Gtk::RELIEF_NONE);
        bearb->set_valign(Gtk::ALIGN_CENTER);
        bearb->set_image_from_icon_name("document-edit", Gtk::ICON_SIZE_BUTTON);
        bearb->set_tooltip_text(TR("Werkzeug bearbeiten (auch: Doppelklick)"));
        bearb->signal_clicked().connect([this, key] {
            Glib::signal_idle().connect_once([this, key] { werkzeuge_dialog(key); });
        });
        rz->add(*bearb);
        auto weg = Gtk::make_managed<Gtk::Button>();
        weg->set_relief(Gtk::RELIEF_NONE);
        weg->set_valign(Gtk::ALIGN_CENTER);
        weg->set_image_from_icon_name("edit-delete", Gtk::ICON_SIZE_BUTTON);
        weg->set_sensitive(frei && ws->size() > 1);
        weg->set_tooltip_text(frei ? TR("Werkzeug aus der Zeichnung entfernen")
                                   : TR("Wird benutzt – zuerst den Bearbeitungen ein anderes Werkzeug geben"));
        weg->signal_clicked().connect([this, key] {
            Glib::signal_idle().connect_once([this, key] {
                if (auto w2 = einst.if_contains("werkzeuge"); w2 && w2->is_object() && w2->as_object().size() > 1) {
                    w2->as_object().erase(key);
                    schreiben(TR("Werkzeug entfernen"));
                }
            });
        });
        rz->add(*weg);
        row->add(*rz);
        row->set_tooltip_text(TR("Doppelklick: Werkzeug bearbeiten"));
        lb->add(*row);
    }
    karte->add(*lb);
    ziel->add(*karte);
    auto tipp_ = label(TR("Die Werkzeuge gehören zur Zeichnung. «Meine Werkzeuge» (für alle Zeichnungen) "
                       "füllt man im Bearbeiten-Fenster mit «In «Meine Werkzeuge» speichern»."), "hirch-dim");
    klasse(*tipp_, "hirch-klein");
    tipp_->set_line_wrap(true);
    tipp_->set_xalign(0);
    tipp_->set_max_width_chars(44);
    ziel->add(*tipp_);
    ziel->show_all();
}

void HirchFraesen::Impl::farbleiste_fuellen(Gtk::Box *ziel)
{
    UI::remove_all_children(*ziel);
    auto bea = liste_von(einst, "bearbeitungen");
    auto desk = panel.getDesktop();
    auto faerben = [this](std::string hex, std::string name) {
        auto sel = panel.getSelection();
        auto dk = panel.getDesktop();
        if (!sel || sel->isEmpty()) {
            if (dk) dk->messageStack()->flash(Inkscape::WARNING_MESSAGE,
                                              TRF("Erst Objekte auswählen – dann färbt der Klick sie für «{}».", {name}));
            return;
        }
        if (auswahl_faerben(hex)) {
            if (auto d = doc()) DocumentUndo::done(d, (TR("Fräsfarbe: ") + name).c_str(), "");
        }
    };
    static std::map<std::string, char const *> const TEXT = {
        {"vcarve", TR("Schrift und Ornamente mit dem V-Fräser – die Tiefe folgt der Breite.")},
        {"gravur", TR("Entlang der Linie mit fester Tiefe (Gravur, Einlinienschrift).")},
        {"tasche", TR("Fläche ausräumen bis zur Tiefe.")},
        {"kontur", TR("Ausschneiden entlang der Linie – aussen, innen oder auf der Linie.")},
        {"bohren", TR("Löcher in der Mitte der Kreise bohren.")},
        {"gewinde", TR("Gewinde in Kreise fräsen.")}};
    for (auto const &a : ARTEN) {
        if (ohne_objekte(a.key)) continue;
        struct B { int nr; std::string hex, name, tiefe, id; bool aus; };
        std::vector<B> liste;
        if (bea) {
            for (std::size_t i = 0; i < bea->size(); ++i) {
                auto b = (*bea)[i].if_object();
                if (!b || str(*b, "art") != a.key) continue;
                std::string tiefe;
                if (std::string(a.key) == "vcarve") tiefe = TR("bis ") + zahl(num(*b, "max_tiefe", 5)) + " mm";
                else if (std::string(a.key) == "kontur" && ist_null(*b, "tiefe")) tiefe = TR("durch");
                else if (std::string(a.key) == "kontur" && flag(*b, "durch", false)) tiefe = TR("durch");
                else tiefe = zahl(num(*b, "tiefe", 0)) + " mm";
                liste.push_back({(int)i + 1, str(*b, "farbe").empty() ? std::string(a.hex) : str(*b, "farbe"),
                                 name_von(*b), tiefe, str(*b, "id"), !flag(*b, "aktiv", true)});
            }
        }
        std::vector<std::string> farben;
        for (auto const &x : liste) farben.push_back(x.hex);
        auto knopf = Gtk::make_managed<Gtk::Button>();
        knopf->set_relief(Gtk::RELIEF_NONE);
        knopf->set_size_request(34, 30);
        knopf->set_halign(Gtk::ALIGN_CENTER);
        knopf->add(*farbfeld_leiste(farben, a.hex));
        std::string art_name = TR(a.name), hex0 = a.hex, key = a.key;
        if (liste.empty()) {
            hover_karte(*knopf, "<b>" + markup_escape(art_name) + TR("</b> – noch keine Bearbeitung\n") +
                                      markup_escape(TEXT.count(key) ? TEXT.at(key) : "") +
                                      TR("\nKlick: Auswahl einfärben, dann Werkzeug und Tiefe wählen – gerechnet wird danach"));
            // wie «+ Neu»: erst der Dialog (Werkzeug wählen), dann rechnen; «Abbrechen» nimmt die Farbe zurück
            // (Peter, 22.10.)
            knopf->signal_clicked().connect([this, key, art_name] {
                auto sel = panel.getSelection();
                if (!sel || sel->isEmpty()) {
                    if (auto dk = panel.getDesktop())
                        dk->messageStack()->flash(Inkscape::WARNING_MESSAGE,
                                                  TRF("Erst Objekte auswählen – dann färbt der Klick sie für «{}».", {art_name}));
                    return;
                }
                neu(key);
            });
        } else {
            hover_karte(*knopf, "<b>" + markup_escape(art_name) + "</b> – " +
                                      (liste.size() == 1 ? std::string(TR("1 Bearbeitung"))
                                                         : std::to_string(liste.size()) + TR(" Bearbeitungen")) +
                                      TR("\nKlick: Farbton wählen"));
            auto pop = Gtk::make_managed<Gtk::Popover>(*knopf);
            pop->set_position(Gtk::POS_LEFT);
            auto pb = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
            pb->set_margin_top(10);
            pb->set_margin_bottom(8);
            pb->set_margin_start(8);
            pb->set_margin_end(8);
            pb->set_size_request(320, -1);
            auto t = label("");
            t->set_markup("<b>" + markup_escape(art_name) + "</b>");
            t->set_xalign(0);
            t->set_margin_start(4);
            pb->add(*t);
            auto d_ = label(std::string(TEXT.count(key) ? TEXT.at(key) : "") +
                            TR(" Klick auf einen Farbton färbt die Auswahl ein."), "hirch-dim");
            klasse(*d_, "hirch-klein");
            d_->set_xalign(0);
            d_->set_line_wrap(true);
            d_->set_max_width_chars(34);
            d_->set_margin_start(4);
            d_->set_margin_bottom(6);
            pb->add(*d_);
            for (auto const &x : liste) {
                auto zb = Gtk::make_managed<Gtk::Button>();
                zb->set_relief(Gtk::RELIEF_NONE);
                auto z = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 10);
                z->add(*farbfeld_leiste({x.hex}, x.hex));
                auto n = label(std::to_string(x.nr) + " · " + x.name);
                n->set_hexpand(true);
                n->set_xalign(0);
                if (x.aus) klasse(*n, "hirch-aus");
                z->add(*n);
                long k = objekte(x.id);
                z->add(*label(x.tiefe + " · " + std::to_string(k) + TR(" Obj."), "hirch-dim"));
                zb->add(*z);
                std::string hex = x.hex, name = x.name;
                zb->signal_clicked().connect([faerben, hex, name, pop] {
                    pop->popdown();
                    faerben(hex, name);
                });
                pb->add(*zb);
            }
            auto nb = Gtk::make_managed<Gtk::Button>(TR("+ Neue ") + art_name + TR(" im nächsten Farbton"));
            nb->set_relief(Gtk::RELIEF_NONE);
            nb->set_margin_top(4);
            if (auto l = dynamic_cast<Gtk::Label *>(nb->get_child())) l->set_xalign(0);
            nb->signal_clicked().connect([this, key, pop] {
                pop->popdown();
                Glib::signal_idle().connect_once([this, key] { neu(key); });
            });
            pb->add(*nb);
            pb->show_all();
            pop->add(*pb);
            knopf->signal_clicked().connect([pop] { pop->popup(); });
        }
        ziel->add(*knopf);
    }
    auto nicht = Gtk::make_managed<Gtk::Button>();
    nicht->set_relief(Gtk::RELIEF_NONE);
    nicht->set_size_request(34, 30);
    nicht->set_halign(Gtk::ALIGN_CENTER);
    nicht->add(*farbfeld_leiste({}, "#808080", true));
    hover_karte(*nicht, TR("<b>Nicht fräsen</b>\nGrau: Hilfslinien, Notizen – wird nicht gefräst.\n"
                              "Klick: Auswahl grau einfärben"));
    nicht->signal_clicked().connect([faerben] { faerben("#808080", TR("Nicht fräsen")); });
    ziel->add(*nicht);
    ziel->show_all();
    (void)desk;
}

void HirchFraesen::Impl::werkzeug_reiter_auffrischen()
{
    auto desk = panel.getDesktop();
    auto c = desk ? desk->getContainer() : nullptr;
    if (!c) return;
    if (auto w = dynamic_cast<HirchWerkzeuge *>(c->get_dialog("HirchWerkzeuge"))) w->auffrischen();
}

} // namespace Inkscape::UI::Dialog
