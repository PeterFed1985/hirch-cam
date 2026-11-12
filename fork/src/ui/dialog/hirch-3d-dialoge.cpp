// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: 3D – Dialoge 3D-Schruppen, 3D-Schlichten, Böden und Wände, Spindel und Halterung; Anschluss an das Fräsen-Panel (aus hirch-3d.cpp)
 */
#include "ui/dialog/hirch-3d-intern.h"

namespace Inkscape::UI::Dialog {

// ---------------------------------------------------------------------------
// Bearbeitungen 3D-Schruppen und 3D-Schlichten (Reiter «Fräsen»)

namespace {

/// Skizze einer 3D-Strategie (Peter 26.10.: «eine Demonstrationszeichnung»): Schnitt durch ein einfaches Teil –
/// Boden, eine steile Schaufel mit Rundung am Fuss, eine flache Kuppel – und darüber, wie die Bahnen laufen.
/// art: Strategie des Schlichtens, «schruppen» oder «boeden_waende».
void skizze_3d(Cr const &cr, double w, double h, Gdk::RGBA const &fg, std::string const &art)
{
    double boden = h - 26, oben = 30;
    double x0 = 14, x1 = w - 14;
    // Schaufel: x 0,28 … 0,40, oben flach, Rundung am Fuss; Kuppel: x 0,58 … 0,92
    double sa = x0 + (x1 - x0) * 0.26, sb = x0 + (x1 - x0) * 0.38, kr = (x1 - x0) * 0.035;
    double ka = x0 + (x1 - x0) * 0.56, kb = x0 + (x1 - x0) * 0.94, kh = (boden - oben) * 0.38;
    auto kuppel_y = [&](double x) {
        double t = (x - ka) / (kb - ka);
        return t <= 0 || t >= 1 ? boden : boden - kh * std::sin(M_PI * t);
    };
    // Teil
    cr->move_to(x0, h - 6);
    cr->line_to(x0, boden);
    cr->line_to(sa - kr, boden);
    cr->curve_to(sa - kr * 0.4, boden, sa, boden - kr * 0.4, sa, boden - kr);
    cr->line_to(sa, oben + 4);
    cr->line_to(sb, oben + 4);
    cr->line_to(sb, boden - kr);
    cr->curve_to(sb, boden - kr * 0.4, sb + kr * 0.4, boden, sb + kr, boden);
    for (double x = ka; x <= kb; x += 2) cr->line_to(x, kuppel_y(x));
    cr->line_to(x1, boden);
    cr->line_to(x1, h - 6);
    cr->close_path();
    farbe(cr, HOLZ);
    cr->fill_preserve();
    farbe(cr, HOLZ_RAND);
    cr->set_line_width(1.2);
    cr->stroke();
    double const blau[3] = {0.11, 0.44, 0.85}, gold[3] = {0.80, 0.55, 0.12}, gruen[3] = {0.18, 0.62, 0.35};
    auto punkt = [&](double x, double y, double const c[3]) {
        farbe(cr, c);
        cr->arc(x, y, 2.2, 0, 2 * M_PI);
        cr->fill();
    };
    auto linie = [&](double xa, double ya, double xb, double yb, double const c[3], double lw = 1.6) {
        farbe(cr, c);
        cr->set_line_width(lw);
        cr->move_to(xa, ya);
        cr->line_to(xb, yb);
        cr->stroke();
    };
    double o = 4;      // Bahn liegt knapp über der Fläche (Mitte der Kugel wäre höher – für die Skizze egal)
    auto oberflaeche = [&](double x) {
        if (x >= sa && x <= sb) return oben + 4;
        if (x >= ka && x <= kb) return kuppel_y(x);
        return boden;
    };
    std::string text;
    if (art == "parallel" || art == "kombiniert_flach") {
        for (double x = x0 + 8; x < x1 - 4; x += 11) punkt(x, oberflaeche(x) - o, blau);
        text = TR("gleicher Abstand von oben gesehen – an steilen Wänden weit auseinander");
    } else if (art == "hoehenlinien") {
        for (double y = oben + 10; y < boden - 2; y += 10) {
            linie(sa - 9, y, sa - 3, y, blau);
            linie(sb + 3, y, sb + 9, y, blau);
            for (double x = ka; x < kb; x += 1) {
                if (kuppel_y(x) < y) {
                    double xe = x;
                    while (xe < kb && kuppel_y(xe) < y) xe += 1;
                    linie(x - 3, y, x + 1, y, blau);
                    linie(xe - 1, y, xe + 3, y, blau);
                    break;
                }
            }
        }
        text = TR("rundum auf festen Höhen – an flachen Stellen weit auseinander");
    } else if (art == "kombiniert") {
        for (double y = oben + 10; y < boden - 2; y += 9) {
            linie(sa - 9, y, sa - 3, y, blau);
            linie(sb + 3, y, sb + 9, y, blau);
        }
        for (double x = x0 + 8; x < x1 - 4; x += 11)
            if (!(x > sa - 12 && x < sb + 12)) punkt(x, oberflaeche(x) - o, blau);
        text = TR("steil: Höhenlinien, flach: parallele Bahnen");
    } else if (art == "konturen") {
        // Schraubenlinie an der Schaufel (schräg absteigende Striche), Kuppel als Spirale (Punkte entlang der Kontur),
        // Kehle am Fuss in einem Zug (gold)
        for (double y = oben + 8; y < boden - 4; y += 8) {
            linie(sa - 10, y, sa - 3, y + 5, blau);
            linie(sb + 3, y + 3, sb + 10, y + 8, blau);
        }
        for (int i = 0; i < 9; ++i) {
            double t = 0.06 + 0.11 * i;
            double x = ka + (kb - ka) * (t < 0.5 ? t : t);
            punkt(x, kuppel_y(x) - o, blau);
        }
        // Pfeil: die Spirale läuft rundum nach innen
        punkt(sa - kr * 0.6 - 2, boden - kr * 0.6 - 2, gold);
        punkt(sb + kr * 0.6 + 2, boden - kr * 0.6 - 2, gold);
        text = TR("jede Form von oben nach unten als Schraubenlinie, flache Stellen entlang der Kontur, "
                  "Kehlen (gold) in einem Zug");
    } else if (art == "ebenen") {
        for (double x = x0 + 6; x < sa - 8; x += 9) punkt(x, boden - o, gruen);
        for (double x = sb + 8; x < ka - 4; x += 9) punkt(x, boden - o, gruen);
        for (double x = sa + 4; x < sb - 2; x += 8) punkt(x, oben + 4 - o, gruen);
        text = TR("nur die waagrechten Flächen, mit dem Schaftfräser");
    } else if (art == "kehlen") {
        punkt(sa - kr * 0.6 - 2, boden - kr * 0.6 - 2, gold);
        punkt(sb + kr * 0.6 + 2, boden - kr * 0.6 - 2, gold);
        punkt(ka + 2, boden - 3, gold);
        punkt(kb - 2, boden - 3, gold);
        text = TR("nur entlang der Kehlen und Innenecken – oft mit einem kleineren Fräser");
    } else if (art == "schruppen") {
        // Ebenen als Treppe mit Aufmass
        double const grau[3] = {0.45, 0.45, 0.45};
        for (double y = oben + 14; y < boden; y += 14) {
            linie(x0 + 4, y, sa - 6, y, blau, 1.2);
            linie(sb + 6, y, ka - 2, y, blau, 1.2);
            double xa = ka, xb = kb;
            while (xa < kb && kuppel_y(xa) > y - 4) xa += 1;
            while (xb > xa && kuppel_y(xb) > y - 4) xb -= 1;
            if (xa < xb) {
                linie(ka - 2, y, xa - 4, y, blau, 1.2);
                linie(xb + 4, y, x1 - 4, y, blau, 1.2);
            } else {
                linie(ka - 2, y, x1 - 4, y, blau, 1.2);
            }
        }
        (void)grau;
        text = TR("Ebene um Ebene von oben nach unten, das Aufmass bleibt stehen");
    } else if (art == "boeden_waende") {
        for (double x = x0 + 6; x < sa - 8; x += 9) punkt(x, boden - o, gruen);
        for (double x = sb + 8; x < ka - 4; x += 9) punkt(x, boden - o, gruen);
        for (double y = oben + 10; y < boden - kr; y += 10) {
            linie(sa - 6, y, sa - 3, y, gruen, 2.2);
            linie(sb + 3, y, sb + 6, y, gruen, 2.2);
        }
        text = TR("Böden und senkrechte Wände auf Mass, mit dem Schaftfräser des Schruppens");
    }
    // Beschriftung oben (umbrechen, wenn zu lang)
    cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_NORMAL);
    cr->set_font_size(11);
    std::vector<std::string> zeilen;
    std::string z;
    std::istringstream ws(text);
    std::string wort;
    while (ws >> wort) {
        auto probe = z.empty() ? wort : z + " " + wort;
        Cairo::TextExtents ext;
        cr->get_text_extents(probe, ext);
        if (ext.width > w - 16 && !z.empty()) {
            zeilen.push_back(z);
            z = wort;
        } else {
            z = probe;
        }
    }
    if (!z.empty()) zeilen.push_back(z);
    for (std::size_t i = 0; i < zeilen.size() && i < 2; ++i) schrift(cr, fg, 8, 14 + 13.0 * i, zeilen[i]);
}

} // namespace

/// Rechte Spalte der 3D-Dialoge wie im 2D-Dialog: Skizze, letzte Rechnung (Zeit, Fräsweg), Hinweise mit Knöpfen
void HirchFraesen::Impl::rechte_spalte_3d(Gtk::Box *rechts, std::string const &id, Gtk::Widget *skizze_w,
                                          Gtk::Label *zeit_l)
{
    rechts->add(*label(TR("SO LAUFEN DIE BAHNEN"), "hirch-kopf"));
    rechts->add(*skizze_w);
    rechts->add(*label(TR("LETZTE RECHNUNG"), "hirch-kopf"));
    auto karte = Gtk::make_managed<Gtk::Grid>();
    karte->set_row_spacing(4);
    karte->set_column_spacing(12);
    klasse(*karte, "hirch-karte");
    int r = 0;
    auto zeile = [&](std::string const &a, std::string const &w, char const *k = "hirch-wert") {
        auto l = label(a, "hirch-dim");
        l->set_hexpand(true);
        karte->attach(*l, 0, r);
        karte->attach(*label(w, k, 1.0f), 1, r++);
    };
    bj::object const *st = nullptr;
    if (ergebnis_aktuell)
        if (auto sts = unter(ergebnis, "bearbeitungen")) st = unter(*sts, id.c_str());
    if (st) {
        zeile(TR("Fräsweg"), einheit_weg(num(*st, "weg", 0)));
        zeile(TR("Zeit"), zeit(num(*st, "minuten", 0)), "hirch-haupt");
    }
    rechts->add(*karte);
    karte->set_visible(st != nullptr);
    karte->set_no_show_all(st == nullptr);
    zeit_l->set_xalign(0);
    zeit_l->set_line_wrap(true);
    rechts->add(*zeit_l);
    int befunde = 0;
    if (auto ow = ergebnis_aktuell ? unter(ergebnis, "op_warnungen") : nullptr) {
        if (auto ws = ow->if_contains(id); ws && ws->is_array()) {
            for (auto const &v : ws->as_array()) {
                if (!v.is_string()) continue;
                if (!befunde) rechts->add(*label(TR("HINWEISE"), "hirch-kopf"));
                auto l = label("⚠ " + std::string(v.get_string()), "hirch-warn");
                l->set_line_wrap(true);
                l->set_max_width_chars(40);
                l->set_xalign(0);
                klasse(*l, "hirch-klein");
                rechts->add(*l);
                ++befunde;
            }
        }
    }
    if (ergebnis_aktuell) {
        if (auto pr = liste_von(ergebnis, "pruefung")) {
            for (auto const &v : *pr) {
                auto o = v.if_object();
                if (!o || str(*o, "bearbeitung") != id) continue;
                if (!befunde) rechts->add(*label(TR("HINWEISE"), "hirch-kopf"));
                bool fehler = str(*o, "stufe") == "fehler";
                if (!fehler) {
                    rechts->add(*hinweis_widget(str(*o, "text"), o, quittiert_lesen(), 40));
                } else {
                    auto l = label("⛔ " + str(*o, "text"), "hirch-fehler");
                    l->set_line_wrap(true);
                    l->set_max_width_chars(40);
                    l->set_xalign(0);
                    klasse(*l, "hirch-klein");
                    rechts->add(*befund_widget(*o, l));
                }
                ++befunde;
            }
        }
        if (!befunde) rechts->add(*hinweis_label(TR("✓ Die Prüfung hat bei dieser Bearbeitung nichts gefunden.")));
    } else {
        rechts->add(*hinweis_label(TR("Fräsweg, Zeit und Hinweise erscheinen, sobald neu gerechnet ist.")));
    }
}

/// Gerüst der 3D-Dialoge wie im 2D-Dialog: Kopfzeile (Farbe, Name, Art), links die Abschnitte untereinander
/// (rollbar), rechts eine schmale Spalte. Gibt (links, rechts) zurück.
std::pair<Gtk::Box *, Gtk::Box *> HirchFraesen::Impl::geruest_3d(Gtk::Dialog &dlg, bj::object const &b,
                                                                  std::string const &unterzeile)
{
    auto info = art_info(str(b, "art"));
    auto box = dlg.get_content_area();
    box->set_spacing(0);
    box->set_margin_start(0);
    box->set_margin_end(0);
    box->set_margin_top(0);
    auto kopf = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 12);
    kopf->set_margin_start(20);
    kopf->set_margin_end(20);
    kopf->set_margin_top(12);
    kopf->set_margin_bottom(12);
    if (info) kopf->add(*farbfeld(info, 16, str(b, "farbe")));
    auto kt = label("");
    kt->set_markup("<big><b>" + markup_escape(name_von(b)) + "</b></big>");
    kopf->add(*kt);
    kopf->add(*label(unterzeile, "hirch-dim"));
    box->add(*kopf);
    box->add(*Gtk::make_managed<Gtk::Separator>());
    auto haupt = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
    haupt->set_vexpand(true);
    box->add(*haupt);
    auto rollen = Gtk::make_managed<Gtk::ScrolledWindow>();
    rollen->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
    rollen->set_hexpand(true);
    rollen->set_vexpand(true);
    haupt->add(*rollen);
    auto links = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
    links->set_margin_start(22);
    links->set_margin_end(22);
    links->set_margin_top(8);
    links->set_margin_bottom(22);
    rollen->add(*links);
    haupt->add(*Gtk::make_managed<Gtk::Separator>(Gtk::ORIENTATION_VERTICAL));
    auto rechts = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 10);
    rechts->set_size_request(360, -1);
    rechts->set_margin_start(16);
    rechts->set_margin_end(18);
    rechts->set_margin_top(16);
    rechts->set_margin_bottom(14);
    auto rechts_rollen = Gtk::make_managed<Gtk::ScrolledWindow>();
    rechts_rollen->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
    rechts_rollen->set_propagate_natural_width(true);
    rechts_rollen->add(*rechts);
    haupt->add(*rechts_rollen);
    return {links, rechts};
}

void HirchFraesen::Impl::relief_dialog(int index, bj::object b)
{
    // Aufbau nach docs/GESTALTUNG.md (Vorlage Taschendialog): Kopfzeile, Sprung-Chips, Abschnitte
    // als Karten mit «Beschriftung | Feld | Erklärung (live)», Knöpfe unten.
    auto art = str(b, "art");
    auto info = art_info(art);
    if (!info) return;
    bool const schruppen = art == "relief_schruppen";
    auto id = str(b, "id");
    auto dlg = dialog(TR("Bearbeitung ") + name_von(b));
    dlg->set_resizable(true);
    dlg->set_default_size(1240, 780);
    // Aufbau wie der 2D-Dialog (Peter 26.10.): Kopfzeile, links die Abschnitte, rechts Skizze, Rechnung, Hinweise
    std::string modell_name = TR("alle 3D-Modelle");
    for (auto it : reliefs(doc()))
        if (it->getId() && str(b, "relief") == it->getId()) modell_name = str(relief_info(it), "name");
    auto lr = geruest_3d(*dlg, b, std::string(TR(info->name)) + " · " + modell_name);
    auto alles = lr.first, rechts = lr.second;
    auto zeit_l = label("", "hirch-dim");
    klasse(*zeit_l, "hirch-klein");
    double minuten_alt = -1;
    if (ergebnis_aktuell)
        if (auto sts = unter(ergebnis, "bearbeitungen"))
            if (auto st = unter(*sts, id.c_str())) minuten_alt = num(*st, "minuten", 0);
    bj::object start_b = b;
    double start_vorschub = num(wirksam(str(b, "werkzeug", "C")), "vorschub", 0);

    auto warn = label("");
    klasse(*warn, "hirch-warn");
    warn->set_line_wrap(true);
    warn->set_xalign(0);
    warn->set_no_show_all(true);
    alles->add(*warn);

    // Abschnitt: Titel + Karte mit Überschrift und Raster (alle untereinander, wie im 2D-Dialog)
    auto abschnitt_neu = [alles](std::string const &titel, std::string const &kopf_text,
                                         Gtk::Box *ziel = nullptr) {
        (void)ziel;
        auto rahmen = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
        rahmen->set_margin_top(10);
        auto tl = label(titel);
        klasse(*tl, "hirch-seitentitel");
        rahmen->add(*tl);
        auto k = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
        klasse(*k, "hirch-karte2");
        k->add(*label(kopf_text, "hirch-kopf"));
        auto g = Gtk::make_managed<Gtk::Grid>();
        g->set_row_spacing(8);
        g->set_column_spacing(12);
        k->add(*g);
        rahmen->add(*k);
        alles->add(*rahmen);
        return std::make_pair(rahmen, g);
    };
    auto feld = [](Gtk::Grid *g, int r, std::string const &text, Gtk::Widget &w, bool erklaerung = true) {
        auto l = label(text);
        l->set_size_request(200, -1);
        g->attach(*l, 0, r);
        w.set_halign(Gtk::ALIGN_START);
        if (!erklaerung) {
            g->attach(w, 1, r, 2);
            return (Gtk::Label *)nullptr;
        }
        g->attach(w, 1, r);
        auto h = label("", "hirch-dim");
        klasse(*h, "hirch-klein");
        h->set_hexpand(true);
        h->set_line_wrap(true);
        g->attach(*h, 2, r);
        return h;
    };
    auto spin = [](double v, double min, double max, double schritt, int stellen) {
        auto adj = Gtk::Adjustment::create(v, min, max, schritt, schritt * 10);
        auto s = Gtk::make_managed<Gtk::SpinButton>(adj, schritt, stellen);
        s->set_numeric(true);
        punkt_und_komma(s);
        s->set_width_chars(7);
        s->set_activates_default(true);
        return s;
    };
    // Wert oder «automatisch» (null)
    struct Auto {
        Gtk::SpinButton *s = nullptr;
        Gtk::CheckButton *c = nullptr;
        Gtk::Label *h = nullptr;
    };
    auto mit_auto = [&](Gtk::Grid *g, int r, std::string const &text, char const *k, double vorschlag,
                        double min, double max, double schritt, int stellen) {
        Auto a;
        bool automatisch = ist_null(b, k) || b.find(k) == b.end();
        auto zeile = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        a.s = spin(automatisch ? vorschlag : num(b, k, vorschlag), min, max, schritt, stellen);
        a.c = Gtk::make_managed<Gtk::CheckButton>(TR("automatisch"));
        a.c->set_active(automatisch);
        a.s->set_sensitive(!automatisch);
        zeile->add(*a.s);
        zeile->add(*a.c);
        a.h = feld(g, r, text, *zeile);
        auto s = a.s;
        a.c->signal_toggled().connect([s, c = a.c] { s->set_sensitive(!c->get_active()); });
        return a;
    };

    // --- Allgemein
    auto [r_allg, g1] = abschnitt_neu(TR("Allgemein"), TR("MODELL UND WERKZEUG"));
    (void)r_allg;
    int z = 0;
    auto titel = Gtk::make_managed<Gtk::Entry>();
    titel->set_text(str(b, "titel"));
    titel->set_placeholder_text(TR(info->name) + std::string(TR(" (automatisch)")));
    titel->set_activates_default(true);
    titel->set_width_chars(30);
    feld(g1, z++, "Name", *titel, false);   // gleich in beiden Sprachen

    auto modell = Gtk::make_managed<Gtk::ComboBoxText>();
    modell->append("", TR("alle 3D-Modelle der Zeichnung"));
    auto liste = reliefs(doc());
    for (auto it : liste) {
        auto ri = relief_info(it);
        auto rid = it->getId() ? std::string(it->getId()) : std::string();
        if (rid.empty()) continue;
        auto n = str(ri, "name");
        modell->append(rid, n.empty() ? rid : n);
    }
    if (!modell->set_active_id(str(b, "relief"))) modell->set_active_id("");
    feld(g1, z++, TR("3D-Modell"), *modell, false);

    auto wbox = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    auto werkzeug = Gtk::make_managed<Gtk::ComboBoxText>();
    werkzeug_combo(*werkzeug, str(b, "werkzeug", "C"), false);
    wbox->add(*werkzeug);
    auto wbtn = Gtk::make_managed<Gtk::Button>(TR("Werkzeuge …"));
    wbox->add(*wbtn);
    wbox->set_valign(Gtk::ALIGN_CENTER);
    auto wdetail = feld(g1, z++, TR("Werkzeug"), *wbox);

    // --- Bahnen
    auto [r_bahn, g2] = abschnitt_neu(TR("Bahnen"), schruppen ? TR("EBENEN UND BAHNEN") : TR("BAHNEN"), rechts);
    (void)r_bahn;
    z = 0;
    Gtk::SpinButton *zust = nullptr, *aufmass = nullptr, *bahn = nullptr, *lagen = nullptr;
    Gtk::Label *h_zust = nullptr, *h_aufmass = nullptr, *h_bahn = nullptr, *h_richtung = nullptr, *h_lagen = nullptr;
    Gtk::ComboBoxText *richtung = nullptr, *strategie = nullptr;
    Gtk::SpinButton *grenz = nullptr, *rille = nullptr;
    Gtk::Label *h_strategie = nullptr, *h_grenz = nullptr, *h_rille = nullptr;
    int z_grenz = -1, z_rille = -1, z_richtung = -1, z_genau = -1, z_kwinkel = -1, z_kbahnen = -1, z_last = -1;
    Gtk::CheckButton *adaptiv = nullptr;
    Gtk::SpinButton *last = nullptr;
    Gtk::Label *h_adaptiv = nullptr, *h_last = nullptr;
    Gtk::SpinButton *k_winkel = nullptr, *k_bahnen = nullptr;
    Gtk::Label *h_kwinkel = nullptr, *h_kbahnen = nullptr;
    Gtk::CheckButton *genau = nullptr;
    Gtk::Label *h_genau = nullptr;
    Gtk::CheckButton *auslassen = nullptr, *fuss = nullptr, *kehle_zug = nullptr;
    Gtk::Label *h_kehle_zug = nullptr;
    int z_kehle_zug = -1;
    Gtk::Label *h_auslassen = nullptr, *h_fuss = nullptr;
    int z_fuss = -1;
    Gtk::Box *kugel_box = nullptr;
    int z_kugel = -1;
    auto setzt = std::make_shared<bool>(false);   // Bahnabstand ↔ Rillenhöhe ohne Rückkopplung
    Auto rand, abtast;
    if (schruppen) {
        zust = spin(num(b, "zustellung", 3.0), 0.1, 100, 0.5, 1);
        h_zust = feld(g2, z++, TR("Zustellung"), *zust);
        aufmass = spin(num(b, "aufmass", 0.5), 0, 20, 0.1, 2);
        h_aufmass = feld(g2, z++, TR("Aufmass"), *aufmass);
        // Peter 26.10.: Lücke für Lücke in Lagen – die Schaufeln stehen nie mehr als eine Lage einseitig frei,
        // und der Fräser wechselt seltener von Lücke zu Lücke
        lagen = spin(num(b, "lagen", 0.0), 0, 100, 1, 1);
        h_lagen = feld(g2, z++, TR("Lücke für Lücke in Lagen von"), *lagen);
        bahn = spin(num(b, "bahnabstand", 40), 5, 90, 5, 0);
        h_bahn = feld(g2, z++, TR("Bahnabstand"), *bahn);
        rand = mit_auto(g2, z++, TR("Rand um das Modell"), "rand", 6, 0, 200, 1, 1);
        einheit_feld(rand.s);                // Länge (mm)
        adaptiv = Gtk::make_managed<Gtk::CheckButton>(TR("Adaptiv (gleichmässige Last)"));
        adaptiv->set_active(flag(b, "adaptiv", false));
        h_adaptiv = feld(g2, z++, "", *adaptiv);
        last = spin(num(b, "adaptiv_last", 15), 5, 90, 1, 0);
        h_last = feld(g2, z++, TR("Schnittbreite"), *last);
        z_last = z - 1;
    } else {
        strategie = Gtk::make_managed<Gtk::ComboBoxText>();
        strategie->append("konturen", TR("Konturen je Form (von oben nach unten)"));
        strategie->append("parallel", TR("Parallele Bahnen"));
        strategie->append("hoehenlinien", TR("Höhenlinien"));
        strategie->append("kombiniert", TR("Kombiniert: flach parallel, steil Höhenlinien"));
        strategie->append("ebenen", TR("Waagrechte Flächen"));
        strategie->append("kehlen", TR("Kehlen nachfahren"));
        if (!strategie->set_active_id(str(b, "strategie", "parallel"))) strategie->set_active_id("parallel");
        h_strategie = feld(g2, z++, TR("Strategie"), *strategie);
        // Peter 07.11. 18:33: Kugel gewählt → «Konturen je Form» vorschlagen, übernommen nur auf Klick
        kugel_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        auto kl = label(TR("Kugelfräser gewählt – «Konturen je Form» passt meist am besten."), "hirch-dim");
        kl->set_line_wrap(true);
        kl->set_xalign(0);
        kl->set_hexpand(true);
        kugel_box->add(*kl);
        auto kb = Gtk::make_managed<Gtk::Button>(TR("Konturen je Form nehmen"));
        kb->signal_clicked().connect([strategie] { strategie->set_active_id("konturen"); });
        kugel_box->add(*kb);
        z_kugel = z++;
        g2->attach(*kugel_box, 0, z_kugel, 3, 1);
        grenz = spin(num(b, "grenzwinkel", 40), 10, 80, 5, 0);
        h_grenz = feld(g2, z++, TR("Steil ab"), *grenz);
        z_grenz = z - 1;
        k_winkel = spin(num(b, "kehlen_winkel", 15), 2, 80, 1, 0);
        h_kwinkel = feld(g2, z++, TR("Kehle ab Knick"), *k_winkel);
        z_kwinkel = z - 1;
        k_bahnen = spin(num(b, "kehlen_bahnen", 0), 0, 10, 1, 0);
        h_kbahnen = feld(g2, z++, TR("Bahnen beidseits"), *k_bahnen);
        z_kbahnen = z - 1;
        genau = Gtk::make_managed<Gtk::CheckButton>(TR("Höhenlinien genau rechnen"));
        genau->set_active(flag(b, "genau", false));
        h_genau = feld(g2, z++, "", *genau);
        z_genau = z - 1;
        // Peter 25.10.: was «Böden und Wände» schon auf Mass gefräst hat, nicht noch einmal; Rundungen am Fuss
        // der Wände als Umlauf (Kugel in der Rundung)
        auslassen = Gtk::make_managed<Gtk::CheckButton>(TR("Was schon auf Mass ist, auslassen"));
        auslassen->set_active(flag(b, "auslassen_fertig", false));
        h_auslassen = feld(g2, z++, "", *auslassen);
        fuss = Gtk::make_managed<Gtk::CheckButton>(TR("Rundungen am Fuss umfahren"));
        fuss->set_active(flag(b, "fuss_umlauf", true));
        h_fuss = feld(g2, z++, "", *fuss);
        z_fuss = z - 1;
        kehle_zug = Gtk::make_managed<Gtk::CheckButton>(TR("Kehlen zuletzt in einem Zug nachfahren"));
        kehle_zug->set_active(flag(b, "kehlen_nachfahren", true));
        h_kehle_zug = feld(g2, z++, "", *kehle_zug);
        z_kehle_zug = z - 1;
        bahn = spin(num(b, "bahnabstand", 10), 1, 80, 1, 0);
        h_bahn = feld(g2, z++, TR("Bahnabstand"), *bahn);
        rille = spin(0.01, 0.001, 2, 0.005, 3);
        h_rille = feld(g2, z++, TR("Rillenhöhe"), *rille);
        z_rille = z - 1;
        richtung = Gtk::make_managed<Gtk::ComboBoxText>();
        richtung->append("x", TR("entlang X"));
        richtung->append("y", TR("entlang Y"));
        richtung->append("kreuz", TR("X und Y (kreuzweise)"));
        if (!richtung->set_active_id(str(b, "richtung", "x"))) richtung->set_active_id("x");
        h_richtung = feld(g2, z++, TR("Richtung"), *richtung);
        z_richtung = z - 1;
        // Peter 31.10.: auch negativ – eine 6-mm-Tasche nimmt dann den 6-mm-Fräser auf
        aufmass = spin(num(b, "aufmass", 0), -1, 20, 0.05, 2);
        h_aufmass = feld(g2, z++, TR("Aufmass"), *aufmass);
        rand = mit_auto(g2, z++, TR("Rand um das Modell"), "rand", 4, 0, 200, 1, 1);
        einheit_feld(rand.s);                // Länge (mm)
        abtast = mit_auto(g2, z++, TR("Abtastung"), "abtastung", 0.5, 0.05, 5, 0.05, 2);
        einheit_feld(abtast.s);              // Länge (mm): bei Zoll in Zoll
    }
    // «Über die Trennlinie hinaus» (Peter 08.11.: aus dem Reiter «3D» in die Bearbeitung der Seite). Der Wert gehört
    // weiter zum Modell (hirch:relief › zweiseitig: ueber_trenn_auto, ueber_trenn_<Seite>) und gilt für Schruppen
    // und Schlichten dieser Seite; nur bei zweiseitigen Modellen, die durchgefräst werden (ohne Haut)
    int seite_b = std::clamp((int)num(b, "auf_seite", 1), 1, 2);
    std::vector<SPItem *> ueber_modelle;
    for (auto it : liste) {
        if (!str(b, "relief").empty() && (!it->getId() || str(b, "relief") != it->getId())) continue;
        auto ri = relief_info(it);
        if (auto zz = unter(ri, "zweiseitig"); zz && flag(*zz, "an", false) && !flag(*zz, "haut_an", false))
            ueber_modelle.push_back(it);
    }
    Gtk::CheckButton *ueber_auto = nullptr;
    Gtk::SpinButton *ueber_mm = nullptr;
    Gtk::Label *h_ueber_t = nullptr;
    int z_ueber_mm = -1;
    if (!ueber_modelle.empty()) {
        auto ri = relief_info(ueber_modelle.front());
        bj::object zz;
        if (auto q = unter(ri, "zweiseitig")) zz = *q;
        ueber_auto = Gtk::make_managed<Gtk::CheckButton>(TR("Über die Trennlinie hinaus: automatisch"));
        ueber_auto->set_active(zz.find("ueber_trenn_auto") == zz.end() || flag(zz, "ueber_trenn_auto", true));
        h_ueber_t = feld(g2, z++, "", *ueber_auto);
        ueber_mm = spin(num(zz, ("ueber_trenn_" + std::to_string(seite_b)).c_str(), 3), 0, 50, 0.5, 2);
        feld(g2, z++, TRF("Über die Trennlinie (Seite {})", {std::to_string(seite_b)}), *ueber_mm)
            ->set_text(TR("mm – so weit fräst diese Seite über die Trennlinie hinaus"));
        z_ueber_mm = z - 1;
    }

    // --- Bereich: zugeordnete geschlossene Linien begrenzen, wo gefräst wird
    auto [r_ber, g3] = abschnitt_neu(TR("Bereich"), TR("WO GEFRÄST WIRD"));
    (void)r_ber;
    z = 0;
    // Flächen aus dem Reiter «3D» (Peter, 22.10.): Linien mit hirch:flaechen dieser Bearbeitung
    auto flaechen_zaehlen = [this, id]() {
        long n = 0;
        auto d = doc();
        if (!d || id.empty()) return n;
        std::function<void(Inkscape::XML::Node *)> geh = [&](Inkscape::XML::Node *k) {
            for (auto c = k->firstChild(); c; c = c->next()) {
                if (auto a = c->attribute("hirch:flaechen")) {
                    try {
                        auto v = bj::parse(a);
                        if (v.is_object() && str(v.as_object(), "bearbeitung") == id) n += long(num(v.as_object(), "anzahl", 1));
                    } catch (...) {
                    }
                    continue;
                }
                geh(c);
            }
        };
        geh(d->getReprRoot());
        return n;
    };
    long n_flaechen = flaechen_zaehlen();
    auto ber = Gtk::make_managed<Gtk::ComboBoxText>();
    ber->append("alles", TR("Ganzes Modell"));
    ber->append("flaechen", n_flaechen ? TRF("Nur gewählte Flächen ({})", {std::to_string(n_flaechen)})
                                       : std::string(TR("Nur gewählte Flächen")));
    ber->append("ohne_flaechen", TR("Ausser gewählten Flächen"));
    ber->append("innen", TR("Nur innerhalb der Linien"));
    ber->append("aussen", TR("Ausser innerhalb der Linien"));
    if (!ber->set_active_id(str(b, "bereich", "alles"))) ber->set_active_id("alles");
    auto ber_id = [ber]() { return ber->get_active_id().raw(); };
    auto h_ber = feld(g3, z++, TR("Bereich"), *ber);
    auto ueber = spin(num(b, "bereich_ueberlauf", 1.0), 0, 50, 0.5, 1);
    auto h_ueber = feld(g3, z++, TR("Überlauf"), *ueber);
    auto z_ueber = z - 1;
    // Peter 26.10.: Modell so gross wie der Block – der Fräser muss über die Kante hinaus (Kanten dürfen weg, wenn
    // erst auf Seite 2 durchgefräst wird); die Schraubenköpfe in den Passlöchern bleiben ausgespart
    auto rand_aussen = spin(num(b, "ueber_rand", 0.0), 0, 50, 0.5, 1);
    auto h_rand_aussen = feld(g3, z++, TR("Über den Materialrand hinaus"), *rand_aussen);

    // zugeordnete Linien: über «zusätzlich hier fräsen» (hirch:auch) mit dieser Bearbeitung verbunden
    auto linien_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    auto linien_n = label("");
    linien_n->set_margin_end(4);
    linien_box->add(*linien_n);
    Gtk::Button *b_zuweisen = nullptr;
    if (auto sel = panel.getSelection(); sel && !sel->isEmpty() && !id.empty()) {
        b_zuweisen = Gtk::make_managed<Gtk::Button>(TR("Gewählte Linien als Bereich"));
        b_zuweisen->set_tooltip_text(TR("Die ausgewählten geschlossenen Linien begrenzen diese Bearbeitung; "
                                        "sie werden dafür nicht selbst gefräst."));
        linien_box->add(*b_zuweisen);
    }
    auto b_loesen = Gtk::make_managed<Gtk::Button>(TR("Linien lösen"));
    b_loesen->set_tooltip_text(TR("Die Linien bleiben in der Zeichnung, gehören aber nicht mehr zu dieser Bearbeitung."));
    linien_box->add(*b_loesen);
    auto h_linien = feld(g3, z++, TR("Linien"), *linien_box);

    // Restmaterial: nur, was die 3D-Bearbeitungen davor stehen liessen (gehört zum «Wo»)
    auto rest = Gtk::make_managed<Gtk::CheckButton>(TR("Nur Restmaterial bearbeiten"));
    rest->set_active(b.find("rest") != b.end() && b.at("rest").is_bool() && b.at("rest").as_bool());
    auto h_rest = feld(g3, z++, TR("Restmaterial"), *rest);
    auto schwelle = spin(num(b, "rest_schwelle", 0.1), 0.02, 5, 0.05, 2);
    auto h_schwelle = feld(g3, z++, TR("Ab Reststärke"), *schwelle);
    auto z_schwelle = z - 1;

    // Anzahl zugeordneter Linien (in der Zeichnung, nicht aus der letzten Rechnung)
    auto linien_zaehlen = [this, id]() {
        long n = 0;
        auto d = doc();
        if (!d || id.empty()) return n;
        std::function<void(Inkscape::XML::Node *)> geh = [&](Inkscape::XML::Node *k) {
            for (auto c = k->firstChild(); c; c = c->next()) {
                if (c->attribute("hirch:flaechen")) continue;     // gewählte Flächen zählen dort
                if (auto a = c->attribute("hirch:auch"); a && ohne(a, id) != std::string(a)) ++n;
                geh(c);
            }
        };
        geh(d->getReprRoot());
        return n;
    };

    // Werte aus dem Dialog (für «So fräse ich» bei jeder Änderung und beim Übernehmen)
    auto werte_lesen = [=](bj::object &b) {
        b["relief"] = modell->get_active_id().raw();
        b["werkzeug"] = werkzeug->get_active_id().raw();
        b["bahnabstand"] = (int)std::lround(wert(bahn));
        b["aufmass"] = wert(aufmass);
        if (zust) b["zustellung"] = wert(zust);
        if (lagen) b["lagen"] = wert(lagen);
        if (richtung) b["richtung"] = richtung->get_active_id().raw();
        if (strategie) b["strategie"] = strategie->get_active_id().raw();
        if (grenz) b["grenzwinkel"] = wert(grenz);
        if (genau) b["genau"] = genau->get_active();
        if (auslassen) b["auslassen_fertig"] = auslassen->get_active();
        if (fuss) b["fuss_umlauf"] = fuss->get_active();
        if (kehle_zug) b["kehlen_nachfahren"] = kehle_zug->get_active();
        b["ueber_rand"] = wert(rand_aussen);
        if (k_winkel) b["kehlen_winkel"] = wert(k_winkel);
        if (k_bahnen) b["kehlen_bahnen"] = (int)std::lround(wert(k_bahnen));
        if (adaptiv) b["adaptiv"] = adaptiv->get_active();
        if (last) b["adaptiv_last"] = (int)std::lround(wert(last));
        auto auto_lesen = [&b](Auto const &a, char const *k) {
            if (!a.s) return;
            if (a.c->get_active()) b[k] = nullptr;
            else b[k] = wert(a.s);
        };
        auto_lesen(rand, "rand");
        auto_lesen(abtast, "abtastung");
        b["bereich"] = ber_id();
        b["bereich_ueberlauf"] = wert(ueber);
        b["rest"] = rest->get_active();
        b["rest_schwelle"] = wert(schwelle);
    };
    auto vorg_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);    // «So fräse ich» (basis 07.11.)
    auto neu = [=, this] {
        auto w = wirksam(werkzeug->get_active_id().raw());
        wdetail->set_text(w.empty() ? "" : werkzeug_details(w));
        auto typ = str(w, "typ", "schaft");
        double d = num(w, "durchmesser", 6);
        // «Rand um das Modell, automatisch»: der Wert, mit dem gerechnet wird (relief3d.bearbeiten) – zweiseitig
        // weiter hinaus (02.11.); vorher stand fest 6 bzw. 4 da
        if (rand.c && rand.c->get_active()) {
            bool zwei = false;
            for (auto it : liste) {
                auto ri = relief_info(it);              // erst behalten: unter() zeigt hinein
                if (auto zz = unter(ri, "zweiseitig"); zz && flag(*zz, "an", false)) zwei = true;
            }
            double r_auto = schruppen ? (zwei ? 2 * d + 2 : d) : (zwei ? d + 1 : d / 2 + 1);
            if (std::abs(wert(rand.s) - r_auto) > 1e-6) rand.s->set_value(r_auto);
        }
        std::string t;
        if (liste.empty()) {
            t = TR("Die Zeichnung hat noch kein 3D-Modell – zuerst «Fräsen › 3D-Modell einlesen …».");
        } else if (!schruppen && strategie && strategie->get_active_id() == "ebenen") {
            if (typ != "schaft" && typ != "eckradius")
                t = TR("Waagrechte Flächen: dafür eignet sich ein Schaftfräser (flach) am besten.");
        } else if (!schruppen && typ != "kugel" && typ != "eckradius" && typ != "konisch") {
            t = TR("Zum Schlichten eignet sich ein Kugelfräser (Vollradius) am besten.");
        } else if (schruppen && (typ == "v" || typ == "konisch")) {
            t = TR("Zum Schruppen eignet sich ein Schaftfräser am besten.");
        }
        long n = linien_zaehlen();
        auto bi = ber_id();
        bool begrenzt = bi != "alles";
        bool mit_flaechen = bi == "flaechen" || bi == "ohne_flaechen";
        if (t.empty() && mit_flaechen && n_flaechen == 0)
            t = TR("Noch keine Flächen gewählt: in der 3D-Ansicht mit Rechtsklick aufs Modell Flächen wählen und «Gewählte Flächen "
                   "als Bereich» drücken. Sonst wird das ganze Modell bearbeitet.");
        else if (t.empty() && begrenzt && !mit_flaechen && n == 0)
            t = TR("Für «nur innerhalb» bzw. «ausser innerhalb» braucht es geschlossene Linien: Linien auswählen "
                   "und «Gewählte Linien als Bereich» drücken. Sonst wird das ganze Modell bearbeitet.");
        h_ber->set_text(bi == "flaechen" ? TR("fräst nur auf den gewählten Flächen")
                        : bi == "ohne_flaechen" ? TR("fräst überall ausser auf den gewählten Flächen")
                        : bi == "innen" ? TR("fräst nur innerhalb der zugeordneten Linien")
                        : bi == "aussen" ? TR("fräst überall ausser innerhalb der Linien")
                                         : TR("fräst das ganze Modell"));
        zeile_zeigen(linien_box, !mit_flaechen);          // B-9: Knopf auch bei «Ganzes Modell» sichtbar
        warn->set_text(t);
        warn->set_visible(!t.empty());

        double s = d * wert(bahn) / 100.0;
        if (schruppen) {
            h_bahn->set_text(TRF("% vom Ø · {} mm zwischen den Bahnen", {zahl(s, 2)}));
            h_zust->set_text(TR("mm je Ebene, von oben nach unten"));
            h_lagen->set_text(wert(lagen) < 0.05
                                  ? TR("mm · 0 = Ebene für Ebene über das ganze Modell")
                                  : TR("mm – in jeder Lücke so tief, dann die nächste; die Schaufeln stehen nie mehr "
                                       "als eine Lage einseitig frei"));
            bool ad = adaptiv->get_active();
            h_adaptiv->set_text(ad ? TR("der Fräser ist nie voll umschlungen – schont ihn, erlaubt mehr Vorschub; Eintauchen "
                                        "mit Helix")
                                   : TR("Runden von innen nach aussen im Bahnabstand"));
            h_last->set_text(TRF("% vom Ø · {} mm seitlich im Material", {zahl(d * wert(last) / 100.0, 2)}));
            for (int c = 0; c < 3; ++c)
                if (auto wgt = g2->get_child_at(c, z_last)) wgt->set_visible(ad);
            h_aufmass->set_text(TR("mm bleiben für das Schlichten stehen"));
        } else {
            // Rillenhöhe zwischen zwei Bahnen eines Kugelfräsers: h = R − √(R² − (s/2)²)
            double R = typ == "kugel" ? d / 2 : num(w, "eckradius", 0);
            auto st = strategie->get_active_id().raw();
            bool mit_rille = R > 1e-6 && st != "ebenen";
            std::string rh;
            if (mit_rille && s / 2 < R) {
                double h = R - std::sqrt(R * R - s * s / 4);
                if (!*setzt) {
                    *setzt = true;
                    rille->set_value(h);
                    *setzt = false;
                }
                rh = TRF(" · Rillen etwa {} mm hoch", {zahl(h, 3)});
            }
            h_bahn->set_text(TRF("% vom Ø · {} mm zwischen den Bahnen", {zahl(s, 2)}) +
                             (st == "ebenen" ? std::string(TR(" · Abstand der Ringe")) : rh));
            h_rille->set_text(TR("mm – stellt den Bahnabstand passend ein"));
            for (int c = 0; c < 3; ++c) {
                if (auto wgt = g2->get_child_at(c, z_rille)) wgt->set_visible(mit_rille);
                if (auto wgt = g2->get_child_at(c, z_grenz)) wgt->set_visible(st == "kombiniert" || st == "konturen");
                if (auto wgt = g2->get_child_at(c, z_kehle_zug)) wgt->set_visible(st == "konturen");
                if (auto wgt = g2->get_child_at(c, z_genau))
                    wgt->set_visible(st == "hoehenlinien" || st == "kombiniert");
                if (auto wgt = g2->get_child_at(c, z_kwinkel)) wgt->set_visible(st == "kehlen");
                if (auto wgt = g2->get_child_at(c, z_kbahnen)) wgt->set_visible(st == "kehlen");
                if (auto wgt = g2->get_child_at(c, z_richtung))
                    wgt->set_visible(st == "parallel" || st == "kombiniert");
            }
            zeile_zeigen(kugel_box, typ == "kugel" &&
                                        (st == "parallel" || st == "hoehenlinien" || st == "kombiniert"));
            h_strategie->set_text(
                st == "konturen" ? TR("jede Form (Schaufel, Nabe, Taschenwand) für sich von oben nach unten als "
                                      "Schraubenlinie, flache Stellen entlang ihrer Kontur – schnell und ohne Ansätze")
                : st == "hoehenlinien" ? TR("umrundet das Modell auf festen Höhen, von oben nach unten – gut an steilen Wänden")
                : st == "kombiniert" ? TR("flache Stellen in parallelen Bahnen, steile in Höhenlinien – überall gleichmässig")
                : st == "ebenen" ? TR("räumt nur die waagrechten Flächen (Böden, Absätze) mit dem Schaftfräser")
                : st == "kehlen" ? TR("fährt nur den Kehlen und Innenecken entlang, wo der Fräser zwei Flächen zugleich "
                                      "berührt – nach dem Schlichten, oft mit einem kleineren Fräser")
                                 : TR("gleichmässig auf flachen Stellen, an steilen Wänden weiter auseinander"));
            h_grenz->set_text(st == "konturen" ? TR("° Neigung – ab da Konturen, flacher entlang der Kontur")
                                               : TR("° Neigung – ab da Höhenlinien"));
            h_kehle_zug->set_text(kehle_zug->get_active()
                ? TR("eine Bahn genau in jeder Kehle, wo die Kugel zwei Flächen zugleich berührt")
                : TR("die Kehlen bekommen nur, was die Konturen mitnehmen"));
            h_kwinkel->set_text(TR("° – kleinere Knicke gelten nicht als Kehle"));
            h_kbahnen->set_text(wert(k_bahnen) < 0.5 ? TR("nur genau in der Kehle")
                                                     : TR("zusätzlich je Seite, im Bahnabstand"));
            h_auslassen->set_text(auslassen->get_active()
                ? TR("waagrechte Flächen und senkrechte Wände macht «Böden und Wände» – die Kugel nimmt nur Rundungen "
                     "und geneigte Flächen")
                : TR("fährt alles, auch was schon auf Mass ist"));
            h_fuss->set_text(TR("je Boden ein Umlauf, die Kugel liegt in der Rundung – ist ihr Radius gleich, genügt einer"));
            for (int c = 0; c < 3; ++c)
                if (auto wgt = g2->get_child_at(c, z_fuss)) wgt->set_visible(auslassen->get_active() && st != "konturen");
            h_genau->set_text(genau->get_active()
                ? TR("mit OpenCAMLib, auch an senkrechten Wänden genau – kann bei grossen Modellen länger dauern")
                : TR("aus einem Raster (Zelle = Abtastung) – schnell, an senkrechten Wänden so genau wie das Raster"));
            auto ri = richtung->get_active_id().raw();
            h_richtung->set_text(ri == "kreuz" ? TR("zweimal – glatter, dauert doppelt so lange")
                                               : TR("Bahnen quer dazu versetzt"));
            h_aufmass->set_text(wert(aufmass) > 1e-9 ? TR("mm bleiben stehen – für einen weiteren Durchgang")
                                : wert(aufmass) < -1e-9
                                    ? TRF("mm · das ganze Modell wird überall {} mm kleiner gefräst – dafür passt "
                                          "der Fräser in Taschen so gross wie er selbst",
                                          {mm(-wert(aufmass), 2)})
                                    : TR("mm · fräst auf das Mass des Modells"));
            abtast.h->set_text(TRF("{} zwischen zwei Messpunkten auf der Bahn", {einheit_name()}));
        }
        if (ueber_auto) {
            h_ueber_t->set_text(ueber_auto->get_active()
                ? TR("so weit wie der Radius des 3D-Schlichtfräsers der Seite – sonst bleibt am Rand des Modells eine "
                     "Kehle stehen")
                : TR("von Hand, je Seite"));
            for (int c = 0; c < 3; ++c)
                if (auto wgt = g2->get_child_at(c, z_ueber_mm)) wgt->set_visible(!ueber_auto->get_active());
        }
        h_rand_aussen->set_text(wert(rand_aussen) > 1e-9
            ? TR("mm – so weit darf der Fräser über die Kante des Blocks hinaus; um die Passlöcher (Schrauben) "
                 "wird nicht gefräst")
            : TR("mm · bis zur Kante des Blocks – ist das Modell so gross wie der Block, bleibt am Rand etwas stehen"));
        rand.h->set_text(rand.c->get_active() ? TR("so breit, dass der Fräser rund um das Modell kommt")
                                              : TR("mm um das Modell herum"));

        h_ueber->set_text(bi == "innen" || bi == "flaechen"
                              ? TR("mm über den Rand hinaus, damit die Kante sauber ist")
                              : TR("mm um den Rand herum frei lassen, damit sich nichts überschneidet"));
        for (int c = 0; c < 3; ++c)
            if (auto wgt = g3->get_child_at(c, z_ueber)) wgt->set_visible(begrenzt);
        linien_n->set_text(n == 0 ? TR("keine") : anzahl(n, TR("Linie"), TR("Linien")));
        h_linien->set_text(n > 0 ? TR("nur geschlossene zählen")
                           : b_zuweisen ? TR("die ausgewählten Linien mit dem Knopf zuordnen")
                                        : TR("Linien in der Zeichnung auswählen, dann Rechtsklick auf die "
                                             "Bearbeitung → «Gewählte Linien als Bereich»"));
        b_loesen->set_visible(n > 0);

        h_rest->set_text(rest->get_active()
            ? TR("fräst nur, wo die Fräser davor (auf dieser Seite) wegen ihrer Form nicht hinkamen – enge "
                 "Rinnen und Kehlen; die Rillen zwischen ihren Bahnen zählen nicht")
            : TR("fräst überall, auch wo schon gefräst ist"));
        for (int c = 0; c < 3; ++c)
            if (auto wgt = g3->get_child_at(c, z_schwelle)) wgt->set_visible(rest->get_active());
        h_schwelle->set_text(TR("mm – dünnere Reste werden übergangen"));

        // Zeit: gemessen bei der letzten Rechnung, umgerechnet mit Bahnabstand, Zustellung, Richtung
        // und Vorschub; was sich so nicht abschätzen lässt (Strategie, Bereich, Rest), sagt «neu rechnen»
        if (minuten_alt > 0) {
            double m = minuten_alt;
            m *= std::max(1.0, num(start_b, "bahnabstand", schruppen ? 40 : 10)) / std::max(1.0, wert(bahn));
            if (zust) m *= std::max(0.1, num(start_b, "zustellung", 3)) / std::max(0.1, wert(zust));
            if (richtung) {
                auto faktor = [](std::string const &r) { return r == "kreuz" ? 2.0 : 1.0; };
                m *= faktor(richtung->get_active_id().raw()) / faktor(str(start_b, "richtung", "x"));
            }
            double vj = num(w, "vorschub", 0);
            if (start_vorschub > 0 && vj > 0) m *= start_vorschub / vj;
            bool unklar = (strategie && strategie->get_active_id().raw() != str(start_b, "strategie", "parallel")) ||
                          ber_id() != str(start_b, "bereich", "alles") ||
                          rest->get_active() != flag(start_b, "rest", false);
            if (unklar)
                zeit_l->set_text(TRF("Zeit bei der letzten Rechnung {} – nach dieser Änderung neu rechnen", {zeit(minuten_alt)}));
            else if (std::abs(m - minuten_alt) < 0.01)
                zeit_l->set_text("");             // steht schon in «Letzte Rechnung»
            else
                zeit_l->set_text(TR("Zeit geschätzt: ") + zeit(minuten_alt) + " → " + zeit(m));
        } else {
            zeit_l->set_text("");
        }
        bj::object jetzt = b;
        werte_lesen(jetzt);
        vorgaben_zeigen(vorg_box, art, jetzt);
    };
    werkzeug->signal_changed().connect(neu);
    bahn->signal_value_changed().connect(neu);
    aufmass->signal_value_changed().connect(neu);
    if (richtung) richtung->signal_changed().connect(neu);
    if (strategie) strategie->signal_changed().connect(neu);
    if (kehle_zug) kehle_zug->signal_toggled().connect(neu);
    rand_aussen->signal_value_changed().connect(neu);
    if (genau) genau->signal_toggled().connect(neu);
    if (auslassen) auslassen->signal_toggled().connect(neu);
    if (k_bahnen) k_bahnen->signal_value_changed().connect(neu);
    if (rille) {
        // Rillenhöhe eingeben → Bahnabstand s = 2·√(h·(2R − h)) in % vom Ø
        rille->signal_value_changed().connect([this, rille, bahn, werkzeug, setzt] {
            if (*setzt) return;
            auto w = wirksam(werkzeug->get_active_id().raw());
            double d = num(w, "durchmesser", 6);
            double R = str(w, "typ", "schaft") == "kugel" ? d / 2 : num(w, "eckradius", 0);
            double h = std::min(wert(rille), R);
            if (R <= 0 || d <= 0) return;
            *setzt = true;
            bahn->set_value(std::round(100.0 * 2 * std::sqrt(h * (2 * R - h)) / d));
            *setzt = false;
        });
    }
    rand.c->signal_toggled().connect(neu);
    ber->signal_changed().connect(neu);
    rest->signal_toggled().connect(neu);
    if (zust) zust->signal_value_changed().connect(neu);
    if (lagen) lagen->signal_value_changed().connect(neu);
    if (adaptiv) adaptiv->signal_toggled().connect(neu);
    if (last) last->signal_value_changed().connect(neu);
    wbtn->signal_clicked().connect([this, werkzeug, neu] {
        auto alt = werkzeug->get_active_id().raw();
        werkzeuge_dialog();
        werkzeug_combo(*werkzeug, alt, false);
        neu();
    });
    if (b_zuweisen) {
        b_zuweisen->signal_clicked().connect([this, index, b_zuweisen, ber, neu] {
            auch_setzen(index, true);
            auto bi = ber->get_active_id().raw();
            if (bi != "innen" && bi != "aussen") ber->set_active_id("innen");   // wer Linien zuweist, will begrenzen
            b_zuweisen->set_sensitive(false);
            neu();
        });
    }
    b_loesen->signal_clicked().connect([this, id, neu] {
        auto d = doc();
        if (!d) return;
        std::function<void(Inkscape::XML::Node *)> putzen = [&](Inkscape::XML::Node *n) {
            for (auto c = n->firstChild(); c; c = c->next()) {
                if (c->attribute("hirch:flaechen")) continue;     // gewählte Flächen bleiben
                if (auto a = c->attribute("hirch:auch")) c->setAttributeOrRemoveIfEmpty("hirch:auch", ohne(a, id));
                putzen(c);
            }
        };
        putzen(d->getReprRoot());
        DocumentUndo::done(d, TR("Linien lösen"), "");
        neu();
    });

    // rechts: Skizze der Strategie, letzte Rechnung, Hinweise
    auto sk = skizze(150, [strategie, schruppen](Cr const &cr, double w, double h, Gdk::RGBA const &fg) {
        skizze_3d(cr, w, h, fg, schruppen ? std::string("schruppen") : strategie->get_active_id().raw());
    });
    sk->set_size_request(320, 150);
    if (strategie) strategie->signal_changed().connect([sk] { sk->queue_draw(); });
    rechte_spalte_3d(rechts, id, sk, zeit_l);
    rechts->add(*vorg_box);
    if (ueber_auto) ueber_auto->signal_toggled().connect(neu);
    // auch diese Felder führen «So fräse ich» nach
    if (fuss) fuss->signal_toggled().connect(neu);
    if (grenz) grenz->signal_value_changed().connect(neu);
    if (k_winkel) k_winkel->signal_value_changed().connect(neu);

    dlg->add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
    if (entfernbar(b)) dlg->add_button(TR("Entfernen"), 1);
    klasse(*dlg->add_button(TR("Übernehmen"), Gtk::RESPONSE_OK), "suggested-action");
    dlg->set_default_response(Gtk::RESPONSE_OK);
    dlg->show_all();
    rad_sperren(*dlg->get_content_area());     // B-22
    neu();
    int antwort = dlg->run();
    dialog_antwort = antwort;
    if (antwort == 1) {
        entfernen(index);
        return;
    }
    if (antwort != Gtk::RESPONSE_OK) return;
    auto t = titel->get_text().raw();
    while (!t.empty() && std::isspace((unsigned char)t.back())) t.pop_back();
    while (!t.empty() && std::isspace((unsigned char)t.front())) t.erase(0, 1);
    b["titel"] = t;
    werte_lesen(b);
    if (ueber_auto) {
        bool geaendert_m = false;
        for (auto it : ueber_modelle) {
            auto ri = relief_info(it);
            bj::object zz;
            if (auto q = unter(ri, "zweiseitig")) zz = *q;
            bool alt_auto = zz.find("ueber_trenn_auto") == zz.end() || flag(zz, "ueber_trenn_auto", true);
            std::string k = "ueber_trenn_" + std::to_string(seite_b);
            double alt_mm = num(zz, k.c_str(), 3);
            if (alt_auto == ueber_auto->get_active() && std::abs(alt_mm - wert(ueber_mm)) < 1e-9) continue;
            zz["ueber_trenn_auto"] = ueber_auto->get_active();
            zz[k] = wert(ueber_mm);
            ri["zweiseitig"] = zz;
            it->getRepr()->setAttribute("hirch:relief", bj::serialize(ri));
            geaendert_m = true;
        }
        if (geaendert_m)
            if (auto d = doc()) DocumentUndo::done(d, TR("3D-Modell ändern"), "");
    }
    if (auto bea = liste_von(einst, "bearbeitungen"); bea && index < (int)bea->size()) {
        (*bea)[index] = b;
        schreiben(TR("Bearbeitung ändern"));
    }
}

// ---------------------------------------------------------------------------
// Böden und Wände (Peter 25.10.): waagrechte Flächen und senkrechte Wände des 3D-Modells auf Mass, Stufe um
// Stufe im Wechsel mit dem 3D-Schruppen (ops/boeden_waende.py). Aufbau wie der Dialog des 3D-Schruppens.

void HirchFraesen::Impl::boeden_waende_dialog(int index, bj::object b)
{
    auto info = art_info("boeden_waende");
    if (!info) return;
    auto id = str(b, "id");
    int seite = (int)num(b, "auf_seite", 1);
    auto dlg = dialog(TR("Bearbeitung ") + name_von(b));
    dlg->set_resizable(true);
    dlg->set_default_size(1240, 720);
    // Aufbau wie der 2D-Dialog (Peter 26.10.)
    auto lr = geruest_3d(*dlg, b, std::string(TR(info->name)) + " · " + std::string(TR("aus dem 3D-Modell")));
    auto links = lr.first, rechts = lr.second;
    auto zeit_l = label("", "hirch-dim");
    klasse(*zeit_l, "hirch-klein");
    double minuten_alt = -1;
    if (ergebnis_aktuell)
        if (auto sts = unter(ergebnis, "bearbeitungen"))
            if (auto st = unter(*sts, id.c_str())) minuten_alt = num(*st, "minuten", 0);

    auto warn = label("");
    klasse(*warn, "hirch-warn");
    warn->set_line_wrap(true);
    warn->set_no_show_all(true);
    links->add(*warn);

    auto abschnitt = [](Gtk::Box *ziel, std::string const &titel, std::string const &kopf_text) {
        auto rahmen = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
        rahmen->set_margin_top(10);
        auto tl = label(titel);
        klasse(*tl, "hirch-seitentitel");
        rahmen->add(*tl);
        auto k = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
        klasse(*k, "hirch-karte2");
        k->add(*label(kopf_text, "hirch-kopf"));
        auto g = Gtk::make_managed<Gtk::Grid>();
        g->set_row_spacing(8);
        g->set_column_spacing(12);
        k->add(*g);
        rahmen->add(*k);
        ziel->add(*rahmen);
        return std::make_pair(k, g);
    };
    auto feld = [](Gtk::Grid *g, int r, std::string const &text, Gtk::Widget &w) {
        auto l = label(text);
        l->set_size_request(190, -1);
        l->set_xalign(0);
        g->attach(*l, 0, r);
        w.set_halign(Gtk::ALIGN_START);
        g->attach(w, 1, r);
        auto h = label("", "hirch-dim");
        klasse(*h, "hirch-klein");
        h->set_hexpand(true);
        h->set_line_wrap(true);
        h->set_xalign(0);
        g->attach(*h, 2, r);
        return h;
    };
    auto spin = [](double v, double min, double max, double schritt, int stellen) {
        auto adj = Gtk::Adjustment::create(v, min, max, schritt, schritt * 10);
        auto sp = Gtk::make_managed<Gtk::SpinButton>(adj, schritt, stellen);
        sp->set_numeric(true);
        punkt_und_komma(sp);
        sp->set_width_chars(7);
        sp->set_activates_default(true);
        return sp;
    };

    // --- Allgemein
    auto [k1, g1] = abschnitt(links, TR("Allgemein"), TR("NAME UND WERKZEUG"));
    (void)k1;
    int z = 0;
    auto titel = Gtk::make_managed<Gtk::Entry>();
    titel->set_text(str(b, "titel"));
    titel->set_placeholder_text(TR(info->name) + std::string(TR(" (automatisch)")));
    titel->set_activates_default(true);
    titel->set_width_chars(30);
    feld(g1, z++, "Name", *titel);
    auto wbox = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    auto werkzeug = Gtk::make_managed<Gtk::ComboBoxText>();
    werkzeug_combo(*werkzeug, str(b, "werkzeug", "C"), false);
    wbox->add(*werkzeug);
    auto wbtn = Gtk::make_managed<Gtk::Button>(TR("Werkzeuge …"));
    wbox->add(*wbtn);
    wbox->set_valign(Gtk::ALIGN_CENTER);
    auto wdetail = feld(g1, z++, TR("Werkzeug"), *wbox);

    // --- Was
    auto [k2, g2] = abschnitt(links, TR("Was"), TR("FLÄCHEN AUS DEM MODELL"));
    (void)k2;
    z = 0;
    auto boeden = Gtk::make_managed<Gtk::CheckButton>(TR("Böden auf Mass"));
    boeden->set_active(flag(b, "boeden", true));
    auto h_boeden = feld(g2, z++, "", *boeden);
    auto waende = Gtk::make_managed<Gtk::CheckButton>(TR("Senkrechte Wände auf Mass"));
    waende->set_active(flag(b, "waende", true));
    auto h_waende = feld(g2, z++, "", *waende);
    auto aufmass = spin(num(b, "aufmass", 0.0), 0, 5, 0.05, 2);
    auto h_aufmass = feld(g2, z++, TR("Aufmass"), *aufmass);
    auto bahn = spin(num(b, "bahnabstand", 40), 5, 90, 5, 0);
    auto h_bahn = feld(g2, z++, TR("Bahnabstand"), *bahn);

    // --- Stufen
    auto [k3, g3] = abschnitt(links, TR("Stufen"), TR("DÜNNE WÄNDE HALTEN"));
    z = 0;
    auto stufe = spin(num(b, "stufe", 10.0), 1, 100, 1, 1);
    auto h_stufe = feld(g3, z++, TR("Stufenhöhe"), *stufe);
    auto wechsel = Gtk::make_managed<Gtk::ComboBoxText>();
    std::map<std::string, std::string> s_werkzeug;      // 3D-Schruppen → Werkzeug
    if (auto bea = liste_von(einst, "bearbeitungen")) {
        for (auto const &v : *bea) {
            auto o = v.if_object();
            if (!o || str(*o, "art") != "relief_schruppen" || (int)num(*o, "auf_seite", 1) != seite ||
                flag(*o, "rest", false))
                continue;
            wechsel->append(str(*o, "id"), name_von(*o));
            s_werkzeug[str(*o, "id")] = str(*o, "werkzeug", "C");
        }
    }
    wechsel->append("", TR("– nicht verschränken –"));
    if (!wechsel->set_active_id(str(b, "im_wechsel_mit"))) wechsel->set_active_id("");
    auto h_wechsel = feld(g3, z++, TR("Im Wechsel mit"), *wechsel);
    auto tipp = label(TR("Dünne Wände (z. B. Schaufeln) halten so: Stufe um Stufe erst schruppen, dann die Wände dieser "
                         "Stufe fertig – darunter hält das Material die Wand noch. Beim Erkennen aus dem STEP wird "
                         "gemeldet, wo dünne Wände stehen."), "hirch-dim");
    klasse(*tipp, "hirch-klein");
    tipp->set_line_wrap(true);
    tipp->set_xalign(0);
    k3->add(*tipp);

    auto vorg_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);    // «So fräse ich» (basis 07.11.)
    auto neu = [=, this] {
        auto w = wirksam(werkzeug->get_active_id().raw());
        wdetail->set_text(w.empty() ? "" : werkzeug_details(w));
        auto typ = str(w, "typ", "schaft");
        double d = num(w, "durchmesser", 6);
        double schneide = num(w, "schneidenlaenge", 0);
        std::string t;
        if (s_werkzeug.empty())
            t = TR("Zuerst ein 3D-Schruppen auf dieser Seite anlegen – «Böden und Wände» fräst nur, wo es hinkam.");
        else if (typ != "schaft" && typ != "eckradius")
            t = TR("Für Böden und Wände auf Mass braucht es einen Schaftfräser (flach).");
        warn->set_text(t);
        warn->set_visible(!t.empty());
        h_boeden->set_text(TR("waagrechte Flächen genau auf ihrer Höhe – Runden von innen nach aussen wie die Tasche"));
        h_waende->set_text(TR("Fräsermitte genau einen Radius neben der Wand; geneigte Flächen und Rundungen bleiben "
                              "fürs 3D-Schlichten"));
        h_aufmass->set_text(wert(aufmass) > 1e-9 ? TR("mm bleiben stehen – für einen weiteren Durchgang")
                                                 : TR("mm · 0 = genau auf Mass"));
        h_bahn->set_text(TRF("% vom Ø · {} mm zwischen den Runden am Boden", {zahl(d * wert(bahn) / 100.0, 2)}));
        std::string sh = TR("mm · von Hand, Vorgabe 10 – bei fragilem Material (Holz, XPS) eher weniger");
        if (schneide > 0) {
            sh += " · " + TRF("höchstens die Schneide ({} mm)", {zahl(std::max(1.0, schneide - 0.5), 1)});
            if (wert(stufe) > schneide - 0.5) sh += " – " + std::string(TR("so wird mit der Schneide gerechnet"));
        }
        h_stufe->set_text(sh);
        auto wi = wechsel->get_active_id().raw();
        if (wi.empty())
            h_wechsel->set_text(TR("erst das ganze 3D-Schruppen, dann Böden und Wände"));
        else if (s_werkzeug.count(wi) && s_werkzeug.at(wi) != werkzeug->get_active_id().raw())
            h_wechsel->set_text(TR("anderer Fräser als beim Schruppen – je Stufe wären es zwei Werkzeugwechsel; so "
                                   "fräst es nach dem ganzen Schruppen"));
        else
            h_wechsel->set_text(TR("Stufe um Stufe abwechselnd, gleicher Fräser – kein Werkzeugwechsel"));
        zeit_l->set_text("");                     // Zeit steht in «Letzte Rechnung»
        bj::object jetzt = b;
        jetzt["werkzeug"] = werkzeug->get_active_id().raw();
        jetzt["boeden"] = boeden->get_active();
        jetzt["waende"] = waende->get_active();
        jetzt["aufmass"] = wert(aufmass);
        jetzt["bahnabstand"] = (int)std::lround(wert(bahn));
        jetzt["stufe"] = wert(stufe);
        jetzt["im_wechsel_mit"] = wechsel->get_active_id().raw();
        vorgaben_zeigen(vorg_box, "boeden_waende", jetzt);
    };
    boeden->signal_toggled().connect(neu);
    waende->signal_toggled().connect(neu);
    werkzeug->signal_changed().connect(neu);
    aufmass->signal_value_changed().connect(neu);
    bahn->signal_value_changed().connect(neu);
    stufe->signal_value_changed().connect(neu);
    wechsel->signal_changed().connect(neu);
    wbtn->signal_clicked().connect([this, werkzeug, neu] {
        auto alt = werkzeug->get_active_id().raw();
        werkzeuge_dialog();
        werkzeug_combo(*werkzeug, alt, false);
        neu();
    });

    auto sk = skizze(150, [](Cr const &cr, double w, double h, Gdk::RGBA const &fg) {
        skizze_3d(cr, w, h, fg, "boeden_waende");
    });
    sk->set_size_request(320, 150);
    rechte_spalte_3d(rechts, id, sk, zeit_l);
    rechts->add(*vorg_box);

    dlg->add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
    if (entfernbar(b)) dlg->add_button(TR("Entfernen"), 1);
    klasse(*dlg->add_button(TR("Übernehmen"), Gtk::RESPONSE_OK), "suggested-action");
    dlg->set_default_response(Gtk::RESPONSE_OK);
    dlg->show_all();
    rad_sperren(*dlg->get_content_area());     // B-22
    neu();
    int antwort = dlg->run();
    dialog_antwort = antwort;
    if (antwort == 1) {
        entfernen(index);
        return;
    }
    if (antwort != Gtk::RESPONSE_OK) return;
    auto t = titel->get_text().raw();
    while (!t.empty() && std::isspace((unsigned char)t.back())) t.pop_back();
    while (!t.empty() && std::isspace((unsigned char)t.front())) t.erase(0, 1);
    b["titel"] = t;
    b["werkzeug"] = werkzeug->get_active_id().raw();
    b["boeden"] = boeden->get_active();
    b["waende"] = waende->get_active();
    b["aufmass"] = wert(aufmass);
    b["bahnabstand"] = (int)std::lround(wert(bahn));
    b["stufe"] = wert(stufe);
    b["im_wechsel_mit"] = wechsel->get_active_id().raw();
    if (auto bea = liste_von(einst, "bearbeitungen"); bea && index < (int)bea->size()) {
        (*bea)[index] = b;
        schreiben(TR("Bearbeitung ändern"));
    }
}

// ---------------------------------------------------------------------------
// Einstellungen › Maschine › «Spindel und Halterung …» (Kollisionsprüfung beim 3D-Fräsen,
// Rechenkern hirch_cam/ops/relief_halterung.py). Aufbau wie der Taschendialog: links die Masse
// in Karten, rechts die Seitenansicht (das gewählte Mass blau) und die letzte Prüfung.

namespace {

struct HalterMass {
    char const *key, *marke, *name, *hilfe;
    double max, schritt;
    int stellen;
};

HalterMass const HALTER_MASSE[] = {
    {"mutter_hoehe", "B", "Höhe unter der Spindel", "Spindelnase bis Unterkante", 200, 0.5, 1},
    {"mutter_d", "C", "Grösster Durchmesser", "über die Schlüsselflächen", 200, 0.5, 1},
    {"spindel_d", "D", "Durchmesser", "aussen", 300, 0.5, 1},
    {"spindel_laenge", "E", "Länge bis zur Halterung", "Spindelnase bis Unterkante Halterung", 1000, 1, 1},
    {"halter_b", "F", "Breite", "in X", 1000, 5, 0},
    {"halter_t", "T", "Tiefe", "in Y", 1000, 5, 0},
};

// Typische Werte zum Ausgangspunkt – danach nachmessen
struct HalterVorlage {
    char const *id, *name;
    double b, c, d, e;
};
HalterVorlage const HALTER_VORLAGEN[] = {
    {"er11", "ER11 · 65er", 18, 19, 65, 120},
    {"er16", "ER16 · 52er", 20, 28, 52, 110},
    {"er20", "ER20 · 80er", 24, 32, 80, 140},
};

void halter_css()
{
    static bool fertig = false;
    if (fertig) return;
    fertig = true;
    auto css = Gtk::CssProvider::create();
    css->load_from_data(R"(
.h3d-marke { font-family: monospace, "Segoe UI Symbol"; font-weight: bold; border-radius: 4px; padding: 1px 6px;
  background: alpha(currentColor, 0.10); }
.h3d-marke-an { background: @theme_selected_bg_color; color: @theme_selected_fg_color; }
.h3d-zeile { border-radius: 6px; padding: 3px 8px; }
.h3d-zeile-an { background: alpha(@theme_selected_bg_color, 0.16); }
.h3d-fehler { padding: 8px 10px; border-radius: 6px; background: alpha(#d9342b, 0.14);
  border: 1px solid alpha(#d9342b, 0.35); }
)");
    Gtk::StyleContext::add_provider_for_screen(Gdk::Screen::get_default(), css,
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

struct HalterDialog {
    std::map<std::string, Gtk::SpinButton *> spins;
    std::map<std::string, Gtk::Widget *> zeilen;
    std::map<std::string, Gtk::Label *> marken;
    std::vector<std::pair<std::string, Gtk::ToggleButton *>> vorlagen;
    std::string aktiv = "mutter_hoehe";
    std::string streift;            // Körper der letzten Kollision (rot in der Zeichnung)
    double streift_tief = 0;
    double ausspannen = 0;          // Ausspannlänge des Werkzeugs der letzten Kollision (0 = keine)
    bool setzt = false;
    Gtk::DrawingArea *bild = nullptr;

    double v(char const *k) { return spins.count(k) ? spins[k]->get_value() : 0.0; }
    std::string wert_text(char const *k, int st) { return v(k) > 0 ? zahl(v(k), st) : std::string("–"); }

    void aktiv_setzen(std::string const &k)
    {
        aktiv = k;
        for (auto &[key, z] : zeilen) {
            auto c = z->get_style_context();
            if (key == k) c->add_class("h3d-zeile-an");
            else c->remove_class("h3d-zeile-an");
        }
        for (auto &[key, l] : marken) {
            auto c = l->get_style_context();
            if (key == k) c->add_class("h3d-marke-an");
            else c->remove_class("h3d-marke-an");
        }
        if (bild) bild->queue_draw();
    }

    void vorlage_zeigen()
    {
        std::string id = "eigen";
        for (auto const &vl : HALTER_VORLAGEN) {
            if (std::abs(v("mutter_hoehe") - vl.b) < 1e-6 && std::abs(v("mutter_d") - vl.c) < 1e-6 &&
                std::abs(v("spindel_d") - vl.d) < 1e-6 && std::abs(v("spindel_laenge") - vl.e) < 1e-6)
                id = vl.id;
        }
        setzt = true;
        for (auto &[k, b] : vorlagen) b->set_active(k == id);
        setzt = false;
    }

    /// Seitenansicht wie im Mockup: Halterung, Spindel, Mutter, Fräser über Material und Modell
    void malen(Cairo::RefPtr<Cairo::Context> const &cr, int w, int h)
    {
        double s = std::min(w / 464.0, h / 470.0);
        cr->save();
        cr->set_source_rgb(0.722, 0.722, 0.718);           // grauer Kasten wie die Vorschau
        rund(cr, 0, 0, w, h, 6);
        cr->fill();
        cr->translate((w - 464 * s) / 2, (h - 470 * s) / 2);
        cr->scale(s, s);
        auto farbe = [&](unsigned rgb, double a = 1.0) {
            cr->set_source_rgba(((rgb >> 16) & 255) / 255.0, ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0, a);
        };
        auto rot = 0xC01C28u;
        auto text = [&](double x, double y, std::string const &t, unsigned rgb, double gr, bool mitte = false,
                        bool rechts = false, bool fett = false) {
            cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL, fett ? Cairo::FONT_WEIGHT_BOLD : Cairo::FONT_WEIGHT_NORMAL);
            cr->set_font_size(gr);
            Cairo::TextExtents te;
            cr->get_text_extents(t, te);
            double x0 = mitte ? x - te.x_advance / 2 : rechts ? x - te.x_advance : x;
            farbe(rgb);
            cr->move_to(x0, y);
            cr->show_text(einheit_text_zahlen(t));
        };
        // Halterung, Spindel, Mutter, Fräser
        farbe(streift == "halterung" ? rot : 0x5E5C64);
        rund(cr, 92, 24, 280, 64, 4);
        cr->fill();
        text(232, 61, TR("Halterung"), 0xF6F5F4, 13, true);
        farbe(streift == "spindel" ? rot : 0x77767B);
        cr->rectangle(177, 88, 110, 170);
        cr->fill();
        text(232, 180, TR("Spindel"), 0xF6F5F4, 13, true);
        farbe(streift == "mutter" ? rot : 0x5E5C64);
        cr->move_to(200, 258); cr->line_to(264, 258); cr->line_to(256, 296); cr->line_to(208, 296);
        cr->close_path();
        cr->fill();
        farbe(streift == "schaft" ? rot : 0x3D3846);
        cr->rectangle(227, 296, 10, 106);
        cr->fill();
        // Material mit Modell
        farbe(0xE5D3AE);
        cr->rectangle(14, 374, 436, 88);
        cr->fill();
        farbe(0x3D3846);
        cr->move_to(14, 374); cr->line_to(120, 374);
        cr->curve_to(180, 374, 200, 402, 232, 410);
        cr->curve_to(270, 418, 300, 410, 450, 410);
        cr->line_to(450, 462); cr->line_to(14, 462); cr->close_path();
        cr->fill();
        text(28, 450, TR("Material · Modell"), 0xDEDDDA, 12);
        if (!streift.empty()) {
            farbe(rot, 0.85);
            cr->rectangle(196, 362, 72, 14);
            cr->fill();
            farbe(rot);
            cr->set_line_width(2);
            std::vector<double> strich{5, 4};
            cr->set_dash(strich, 0);
            cr->arc(232, 369, 28, 0, 2 * M_PI);
            cr->stroke();
            cr->unset_dash();
            text(270, 352, TRF("streift {} mm", {zahl(streift_tief, 1)}), 0xA51D2D, 12, false, false, true);
        }
        // Masse: das gewählte blau und kräftig
        auto mass = [&](char const *k, std::vector<std::array<double, 4>> const &linien, double tx, double ty,
                        std::string const &t, bool mitte, bool rechts) {
            bool an = aktiv == k;
            farbe(an ? 0x1A5FB4 : 0x3D3846);
            cr->set_line_width(an ? 2.6 : 1.1);
            for (auto const &l : linien) {
                cr->move_to(l[0], l[1]);
                cr->line_to(l[2], l[3]);
            }
            cr->stroke();
            text(tx, ty, t, an ? 0x1A5FB4 : 0x3D3846, 13, mitte, rechts, an);
        };
        std::string a_text = ausspannen > 0 ? "A " + zahl(ausspannen, 1) : "A";
        mass("ausspannen", {{150, 296, 150, 402}, {144, 296, 156, 296}, {144, 402, 156, 402}}, 140, 354, a_text, false, true);
        mass("mutter_hoehe", {{120, 258, 120, 296}, {114, 258, 126, 258}, {114, 296, 126, 296}}, 110, 282,
             "B " + wert_text("mutter_hoehe", 1), false, true);
        mass("mutter_d", {{200, 248, 264, 248}, {200, 242, 200, 254}, {264, 242, 264, 254}}, 232, 239,
             "C " + wert_text("mutter_d", 1), true, false);
        mass("spindel_d", {{177, 104, 287, 104}, {177, 98, 177, 110}, {287, 98, 287, 110}}, 232, 124,
             "D " + wert_text("spindel_d", 1), true, false);
        mass("spindel_laenge", {{318, 88, 318, 258}, {312, 88, 324, 88}, {312, 258, 324, 258}}, 328, 178,
             "E " + wert_text("spindel_laenge", 1), false, false);
        mass("halter_b", {{92, 12, 372, 12}, {92, 6, 92, 18}, {372, 6, 372, 18}}, 382, 16,
             "F " + wert_text("halter_b", 0), false, false);
        mass("halter_t", {{396, 30, 396, 82}}, 404, 60, "T " + wert_text("halter_t", 0), false, false);
        cr->restore();
    }

    static void rund(Cairo::RefPtr<Cairo::Context> const &cr, double x, double y, double w, double h, double r)
    {
        cr->begin_new_sub_path();
        cr->arc(x + w - r, y + r, r, -M_PI / 2, 0);
        cr->arc(x + w - r, y + h - r, r, 0, M_PI / 2);
        cr->arc(x + r, y + h - r, r, M_PI / 2, M_PI);
        cr->arc(x + r, y + r, r, M_PI, 3 * M_PI / 2);
        cr->close_path();
    }
};

} // namespace

void HirchFraesen::Impl::halterung_dialog()
{
    halter_css();
    bj::object ma;
    if (auto m = unter(analyse, "maschine")) ma = *m;
    auto H = std::make_shared<HalterDialog>();

    auto dlg = dialog(TR("Spindel und Halterung"));
    dlg->set_resizable(true);
    dlg->set_default_size(1180, 860);
    auto box = dlg->get_content_area();
    box->set_spacing(0);
    box->set_margin_start(0);
    box->set_margin_end(0);
    box->set_margin_top(0);

    auto kopf = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 14);
    kopf->set_margin_start(22);
    kopf->set_margin_end(22);
    kopf->set_margin_top(14);
    kopf->set_margin_bottom(14);
    auto kt = label("");
    kt->set_markup("<big><b>" + markup_escape(TR("Spindel und Halterung")) + "</b></big>");
    kopf->add(*kt);
    kopf->add(*label(TR("Einstellungen › Maschine · gilt für alle Zeichnungen"), "hirch-dim"));
    box->add(*kopf);
    box->add(*Gtk::make_managed<Gtk::Separator>(Gtk::ORIENTATION_HORIZONTAL));

    auto haupt = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
    haupt->set_vexpand(true);
    box->add(*haupt);

    // ---- Links: Vorlage und Masse
    auto links_rollen = Gtk::make_managed<Gtk::ScrolledWindow>();
    links_rollen->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
    links_rollen->set_hexpand(true);
    auto links = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 0);
    links_rollen->add(*links);
    haupt->add(*links_rollen);

    auto vz = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 12);
    vz->set_margin_start(22);
    vz->set_margin_end(22);
    vz->set_margin_top(12);
    vz->set_margin_bottom(12);
    vz->add(*label(TR("Vorlage")));
    auto vknoepfe = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
    klasse(*vknoepfe, "linked");
    for (auto const &vl : HALTER_VORLAGEN) {
        auto b = Gtk::make_managed<Gtk::ToggleButton>(vl.name);
        H->vorlagen.emplace_back(vl.id, b);
        vknoepfe->add(*b);
    }
    auto eigen = Gtk::make_managed<Gtk::ToggleButton>(TR("Eigene"));
    H->vorlagen.emplace_back("eigen", eigen);
    vknoepfe->add(*eigen);
    vz->add(*vknoepfe);
    auto vh = label(TR("füllt typische Werte ein – danach nachmessen"), "hirch-dim");
    klasse(*vh, "hirch-klein");
    vz->add(*vh);
    links->add(*vz);
    links->add(*Gtk::make_managed<Gtk::Separator>(Gtk::ORIENTATION_HORIZONTAL));

    auto inhalt = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
    inhalt->set_margin_start(22);
    inhalt->set_margin_end(22);
    inhalt->set_margin_bottom(16);
    links->add(*inhalt);

    struct Gruppe {
        char const *titel, *unter, *klein;
        int von, bis;
    };
    Gruppe const gruppen[] = {
        {"Spannmutter", "Was unter der Spindel hervorsteht.", "MASSE", 0, 2},
        {"Spindel", "Der Motor bis zur Halterung.", "MASSE", 2, 4},
        {"Halterung", "Als Quader, die Spindel sitzt in der Mitte.", "QUADER", 4, 6},
    };
    for (auto const &g : gruppen) {
        auto t = label(TR(g.titel));
        klasse(*t, "hirch-seitentitel");
        t->set_margin_top(10);
        inhalt->add(*t);
        inhalt->add(*label(TR(g.unter), "hirch-dim"));
        auto k = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);
        klasse(*k, "hirch-karte2");
        k->set_margin_top(6);
        k->add(*label(TR(g.klein), "hirch-kopf"));
        for (int i = g.von; i < g.bis; ++i) {
            auto const &m = HALTER_MASSE[i];
            auto z = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 12);
            klasse(*z, "h3d-zeile");
            auto mk = label(m.marke, "h3d-marke");
            mk->set_size_request(26, -1);
            mk->set_xalign(0.5);
            z->add(*mk);
            auto n = label(TR(m.name));
            n->set_size_request(200, -1);
            z->add(*n);
            auto adj = Gtk::Adjustment::create(num(ma, m.key, 0), 0, m.max, m.schritt, m.schritt * 10);
            auto s = Gtk::make_managed<Gtk::SpinButton>(adj, m.schritt, m.stellen);
            s->set_numeric(true);
            punkt_und_komma(s);
            s->set_width_chars(7);
            z->add(*s);
            auto hl = label(std::string("mm · ") + TR(m.hilfe), "hirch-dim");
            klasse(*hl, "hirch-klein");
            hl->set_hexpand(true);
            z->add(*hl);
            k->add(*z);
            std::string key = m.key;
            H->spins[key] = s;
            H->zeilen[key] = z;
            H->marken[key] = mk;
            s->signal_focus_in_event().connect([H, key](GdkEventFocus *) {
                H->aktiv_setzen(key);
                return false;
            }, false);
            s->signal_value_changed().connect([H, key] {
                if (H->setzt) return;
                H->aktiv_setzen(key);
                H->vorlage_zeigen();
            });
        }
        inhalt->add(*k);
    }

    // Ausspannlänge: je Werkzeug im Werkzeugdialog
    auto az = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 12);
    klasse(*az, "h3d-zeile");
    az->set_margin_top(14);
    auto am = label("A", "h3d-marke");
    am->set_size_request(26, -1);
    am->set_xalign(0.5);
    az->add(*am);
    H->zeilen["ausspannen"] = az;
    H->marken["ausspannen"] = am;
    auto at = label(TR("Die Ausspannlänge steht je Werkzeug im Werkzeugdialog (ohne Angabe: Schneidenlänge + 5 mm)."),
                    "hirch-dim");
    at->set_line_wrap(true);
    at->set_hexpand(true);
    az->add(*at);
    auto wbtn = Gtk::make_managed<Gtk::Button>(TR("Werkzeuge …"));
    wbtn->set_valign(Gtk::ALIGN_CENTER);
    az->add(*wbtn);
    inhalt->add(*az);

    // ---- Rechts: Seitenansicht und letzte Prüfung
    haupt->add(*Gtk::make_managed<Gtk::Separator>(Gtk::ORIENTATION_VERTICAL));
    auto rechts = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
    rechts->set_size_request(440, -1);
    rechts->set_margin_start(16);
    rechts->set_margin_end(18);
    rechts->set_margin_top(16);
    rechts->set_margin_bottom(14);
    haupt->add(*rechts);
    rechts->add(*label(TR("SEITENANSICHT"), "hirch-kopf"));
    auto bild = Gtk::make_managed<Gtk::DrawingArea>();
    bild->set_size_request(420, 420);
    H->bild = bild;
    bild->signal_draw().connect([H, bild](Cairo::RefPtr<Cairo::Context> const &cr) {
        H->malen(cr, bild->get_allocated_width(), bild->get_allocated_height());
        return true;
    });
    rechts->add(*bild);
    auto bu = label(TR("Klick in ein Feld zeigt sein Mass blau. Die Spindel sitzt mitten in der Halterung."),
                    "hirch-dim");
    klasse(*bu, "hirch-klein");
    bu->set_line_wrap(true);
    bu->set_max_width_chars(52);
    rechts->add(*bu);
    auto pk = label(TR("PRÜFUNG BEIM RECHNEN"), "hirch-kopf");
    pk->set_margin_top(8);
    rechts->add(*pk);

    // Letzte Rechnung: Kollision (rot, mit «Zur Stelle») oder frei
    bj::object const *koll = nullptr;
    if (auto kl = liste_von(ergebnis, "kollisionen"); ergebnis_aktuell && kl && !kl->empty())
        koll = (*kl)[0].if_object();
    bool hat_3d = false;
    if (auto bea = liste_von(einst, "bearbeitungen")) {
        for (auto const &bv : *bea) {
            if (auto b = bv.if_object()) {
                auto art = str(*b, "art");
                if (am_modell(art) || str(*b, "tiefe_ab") == "modell")
                    hat_3d = true;
            }
        }
    }
    if (koll) {
        H->streift = str(*koll, "teil");
        H->streift_tief = num(*koll, "tief", 0);
        H->ausspannen = num(*koll, "ausspannlaenge", 0);
        auto kb = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
        klasse(*kb, "h3d-fehler");
        auto kt2 = label("");
        kt2->set_markup("<b>" + markup_escape(TR("Rechnung beendet – Kollision")) + "</b>");
        kb->add(*kt2);
        std::string text = str(*koll, "text");
        if (auto bea = liste_von(einst, "bearbeitungen")) {       // welche Bearbeitung
            for (auto const &bv : *bea)
                if (auto b = bv.if_object(); b && str(*b, "id") == str(*koll, "bearbeitung"))
                    text = name_von(*b) + ": " + text;
        }
        auto kl2 = label(text);
        kl2->set_line_wrap(true);
        kl2->set_max_width_chars(52);
        klasse(*kl2, "hirch-klein");
        kb->add(*kl2);
        auto kk = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        auto zur = Gtk::make_managed<Gtk::Button>(TR("Zur Stelle"));
        auto ort = liste_von(*koll, "ort");
        double ox = ort && ort->size() == 2 ? (*ort)[0].to_number<double>() : 0;
        double oy = ort && ort->size() == 2 ? (*ort)[1].to_number<double>() : 0;
        int seite = (int)num(*koll, "seite", 1);
        zur->signal_clicked().connect([this, ox, oy, seite] {
            if (zweiseitig() && seite != seite_aktiv) seite_waehlen(seite, false);
            ort_zeigen(ox, oy, seite);
        });
        kk->add(*zur);
        std::string slot = str(*koll, "werkzeug");
        auto wz = Gtk::make_managed<Gtk::Button>(TRF("Werkzeug {} bearbeiten …", {slot}));
        wz->signal_clicked().connect([this, slot] { werkzeuge_dialog(slot); });
        kk->add(*wz);
        kb->add(*kk);
        rechts->add(*kb);
    } else {
        auto l = label(hat_3d && ergebnis_aktuell && flag(ergebnis, "ok", false)
                           ? TR("✓ Keine Kollision bei der letzten Rechnung.")
                           : TR("Geprüft wird beim Rechnen von 3D-Schruppen, 3D-Schlichten und Bahnen, deren Tiefe "
                                "ab dem 3D-Modell gemessen wird. Bei einer Kollision endet die Rechnung, und die "
                                "Stelle wird rot markiert."),
                       "hirch-dim");
        l->set_line_wrap(true);
        l->set_max_width_chars(52);
        klasse(*l, "hirch-klein");
        rechts->add(*l);
    }
    auto offen = label("", "hirch-dim");
    klasse(*offen, "hirch-klein");
    offen->set_line_wrap(true);
    offen->set_max_width_chars(52);
    rechts->add(*offen);
    auto offen_zeigen = [H, offen] {
        std::vector<std::string> fehlt;
        if (H->v("mutter_hoehe") <= 0 || H->v("mutter_d") <= 0) fehlt.push_back(TR("Spannmutter"));
        if (H->v("spindel_d") <= 0) fehlt.push_back(TR("Spindel"));
        if (H->v("halter_b") <= 0 || H->v("halter_t") <= 0 || H->v("spindel_laenge") <= 0)
            fehlt.push_back(TR("Halterung"));
        std::string t;
        for (auto const &f : fehlt) t += (t.empty() ? "" : ", ") + f;
        offen->set_text(t.empty() ? "" : TRF("Noch ohne Masse, darum nicht geprüft: {}.", {t}));
        offen->set_visible(!t.empty());
    };
    for (auto &[k, s] : H->spins) s->signal_value_changed().connect(offen_zeigen);

    // Vorlagen: füllen die Masse von Mutter und Spindel
    for (auto &[id, b] : H->vorlagen) {
        std::string vid = id;
        b->signal_toggled().connect([H, vid, b] {
            if (H->setzt) return;
            if (!b->get_active()) {               // der gewählte Knopf bleibt gedrückt
                H->setzt = true;
                b->set_active(true);
                H->setzt = false;
                return;
            }
            for (auto const &vl : HALTER_VORLAGEN) {
                if (vid != vl.id) continue;
                H->setzt = true;
                H->spins["mutter_hoehe"]->set_value(vl.b);
                H->spins["mutter_d"]->set_value(vl.c);
                H->spins["spindel_d"]->set_value(vl.d);
                H->spins["spindel_laenge"]->set_value(vl.e);
                H->setzt = false;
            }
            H->vorlage_zeigen();
            if (vid == "eigen") {
                H->setzt = true;
                for (auto &[k2, b2] : H->vorlagen) b2->set_active(k2 == "eigen");
                H->setzt = false;
            }
            if (H->bild) H->bild->queue_draw();
        });
    }
    wbtn->signal_clicked().connect([this, H] {
        H->aktiv_setzen("ausspannen");
        werkzeuge_dialog();
    });

    dlg->add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
    klasse(*dlg->add_button(TR("Übernehmen"), Gtk::RESPONSE_OK), "suggested-action");
    dlg->set_default_response(Gtk::RESPONSE_OK);
    dlg->show_all();
    H->vorlage_zeigen();
    H->aktiv_setzen(H->streift == "spindel" ? "spindel_d" : H->streift == "halterung" ? "halter_b"
                    : H->streift == "schaft" || H->streift == "mutter" ? "ausspannen" : "mutter_hoehe");
    offen_zeigen();
    rad_sperren(*dlg->get_content_area());     // B-22
    if (dlg->run() != Gtk::RESPONSE_OK) return;
    bj::object neu;
    for (auto const &m : HALTER_MASSE) neu[m.key] = wert(H->spins[m.key]);
    if (auto m = analyse.if_contains("maschine"); m && m->is_object())
        for (auto const &kv : neu) m->as_object()[kv.key()] = kv.value();
    python({"datenbank", "maschine", bj::serialize(neu)}, [this](bool, std::string const &, std::string const &) {
        letzte_xml.clear();         // neu rechnen: die Prüfung gilt mit den neuen Massen
        spaeter(10);
    });
}

void HirchFraesen::Impl::kollisionen_zeichnen(SPDesktop *desk)
{
    auto kl = ergebnis_aktuell ? liste_von(ergebnis, "kollisionen") : nullptr;
    // Reiter «3D»: Kollision als roter Klotz, Programme für das Fräsergebnis
    if (auto c = desk ? desk->getContainer() : nullptr) {
        if (auto d3 = dynamic_cast<Hirch3D *>(c->get_dialog("Hirch3D"))) d3->rechnungNeu(rechnung_fuer_3d());
    }
    // 3D-Ansicht im Hauptfenster: das gefräste Teil (nur bei einer neuen Rechnung neu abtragen)
    if (desk && ergebnis_aktuell) {
        auto t = std::make_shared<TeilDaten>();
        t->d = vorschau_daten();
        std::string k = t->d.meldung;
        for (auto const &P : t->d.programme) {
            GStatBuf st;
            k += "|" + P.datei;
            if (g_stat(P.datei.c_str(), &st) == 0) k += ":" + std::to_string(st.st_size) + ":" + std::to_string(st.st_mtime);
        }
        t->kennung = k;
        auto &alt = teile()[desk];
        if (!alt || alt->kennung != t->kennung) {
            alt = t;
            teile_neu().emit(desk);
        }
    }
    if (!kl) return;
    double hoehe = 0;
    if (auto m = unter(analyse, "material")) hoehe = num(*m, "hoehe", 0);
    double const px = MM_PX;
    auto dt = desk->doc2dt();
    for (auto const &kv : *kl) {
        auto k = kv.if_object();
        if (!k) continue;
        double sx = 0, sy = 0;
        seite_ecke((int)num(*k, "seite", 1), sx, sy);
        auto punkt = [&](double x, double y) { return Geom::Point((sx + x) * px, (sy + hoehe - y) * px) * dt; };
        Geom::PathVector pv;
        if (auto zuege = liste_von(*k, "zuege")) {
            for (auto const &zv : *zuege) {
                auto z = zv.if_array();
                if (!z || z->size() < 4) continue;
                auto w = [&](std::size_t i) { return (*z)[i].is_number() ? (*z)[i].to_number<double>() : 0.0; };
                Geom::Path p(punkt(w(0), w(1)));
                for (std::size_t i = 2; i + 1 < z->size(); i += 2) p.appendNew<Geom::LineSegment>(punkt(w(i), w(i + 1)));
                pv.push_back(p);
            }
        }
        if (auto ort = liste_von(*k, "ort"); ort && ort->size() == 2) {
            auto c = punkt((*ort)[0].to_number<double>(), (*ort)[1].to_number<double>());
            pv.push_back(Geom::Path(Geom::Circle(c, 4.0 * px * dt.descrim())));
        }
        if (pv.empty()) continue;
        auto item = make_canvasitem<CanvasItemBpath>(desk->getCanvasSketch(), pv, false);
        item->set_stroke(0xd9342bff);
        item->set_fill(0x0, SP_WIND_RULE_NONZERO);
        item->set_stroke_width(3.0);
        item->set_pickable(false);
        item->set_visible(true);
        bahn_items.push_back(std::move(item));
    }
}

/// Plattenwechsel: das gefräste Teil der alten Platte aus beiden 3D-Ansichten nehmen; ein
/// aufgehobenes Ergebnis der neuen Platte kommt mit kollisionen_zeichnen wieder (3D, 26.10.)
void HirchFraesen::Impl::teil_vergessen()
{
    auto desk = panel.getDesktop();
    if (!desk) return;
    if (teile().erase(desk)) teile_neu().emit(desk);
    if (auto c = desk->getContainer())
        if (auto d3 = dynamic_cast<Hirch3D *>(c->get_dialog("Hirch3D"))) d3->rechnungNeu({});
}

/// Was der Reiter «3D» von der letzten Rechnung braucht: Zeichnung, Programme, Kollisionen
std::string HirchFraesen::Impl::rechnung_fuer_3d()
{
    if (!ergebnis_aktuell) return {};
    bj::object r;
    r["svg"] = Glib::build_filename(ordner, "zeichnung.svg");
    r["ok"] = flag(ergebnis, "ok", false);
    r["platte"] = std::max(1, platte);           // ab Platte 2: Grösse und Modelle dieser Platte
    if (auto p = liste_von(ergebnis, "programme")) r["programme"] = *p;
    if (auto k = liste_von(ergebnis, "kollisionen")) r["kollisionen"] = *k;
    return bj::serialize(r);
}


} // namespace Inkscape::UI::Dialog
