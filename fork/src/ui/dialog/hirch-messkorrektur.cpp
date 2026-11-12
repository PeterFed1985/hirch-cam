// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Messkorrektur – ein gefrästes Testteil (Quadrat aussen, Loch innen) in X und Y ausmessen
 * (Sammelauftrag Peter, 21.10.2026, Punkt 3; Rechnung wie hirch_cam/messkorrektur.py)
 *
 *   s = (Qm + Dm) / (Q + D)      Weg der Achse        e = (Dm − D · s) / 2      Fräser je Seite
 *
 * e (Mittel aus X und Y) wird beim Fräser abgelegen («messkorrektur», mm je Seite): Hirch CNC rechnet an
 * Konturen, Taschenwänden und gefrästen Löchern dieses Fräsers mit diesem Aufmass. s_X und s_Y sind ein
 * Hinweis für die Steuerung; wahlweise gleicht der G-Code sie aus (ab Werk aus).
 */
#include "ui/dialog/hirch-intern.h"
#include "inkscape-application.h"

namespace Inkscape::UI::Dialog {

namespace {
double const GRENZE_SKALIERUNG = 0.01, GRENZE_E = 0.3, GRENZE_SPIEL = 0.10, GRENZE_RUND = 0.05;

/// feste Stellen (3,00 statt 3) – deutsch mit Komma, englisch mit Punkt
std::string fest(double v, int n)
{
    char b[32];
    std::snprintf(b, sizeof b, "%.*f", n, v);
    std::string s = b;
    char const komma = hirch_englisch() ? '.' : ',';
    for (auto &c : s) {
        if (c == '.' || c == ',') c = komma;
    }
    return s;
}

std::string vz(double v, int n)        // mit Vorzeichen
{
    return (v >= -0.5 * std::pow(10.0, -n) ? "+" : "−") + fest(std::abs(v), n);
}

Gtk::Label *wert_label()
{
    auto l = label("", "hirch-wert");
    l->set_xalign(0);
    return l;
}
} // namespace

std::optional<double> HirchFraesen::Impl::messkorrektur_dialog(std::string const &vorwahl, bj::object const *fester,
                                                               bj::object *info_aus, Gtk::Window *eltern)
{
    // Fräser zur Wahl: die der Zeichnung, dann die übrigen aus «Meine Werkzeuge»
    struct Wahl {
        std::string key, name, db_id;
        bj::object w;
    };
    std::vector<Wahl> wahl;
    auto passt = [](bj::object const &w) {
        auto t = str(w, "typ", "schaft");
        return t != "v" && t != "bohrer" && t != "gewinde" && t != "konisch";
    };
    std::set<std::string> schon;
    if (!fester) {
        if (auto ws = unter(einst, "werkzeuge")) {
            for (auto const &kv : *ws) {
                auto w = kv.value().if_object();
                if (!w || !passt(*w)) continue;
                std::string id = flag(*w, "nur_hier", false) ? "" : str(*w, "db_id");
                wahl.push_back({"slot:" + std::string(kv.key()), std::string(kv.key()) + " · " + werkzeug_name(*w), id, *w});
                if (!id.empty()) schon.insert(id);
            }
        }
        if (auto l = liste_von(analyse, "datenbank")) {
            for (auto const &v : *l) {
                auto o = v.if_object();
                if (!o || !passt(*o) || schon.count(str(*o, "id"))) continue;
                wahl.push_back({"db:" + str(*o, "id"), TR("Meine Werkzeuge: ") + werkzeug_name(*o), str(*o, "id"), *o});
            }
        }
        if (wahl.empty()) {
            meldung(TR("Messkorrektur"), TR("Kein passender Fräser – gemessen wird mit einem Schaft-, Kugel- oder "
                                            "Eckradiusfräser."), Gtk::MESSAGE_INFO);
            return {};
        }
    }
    bj::object ma;
    if (auto m = unter(analyse, "maschine")) ma = *m;
    double const alt_global = num(ma, "masskorrektur", 0);

    auto dlg = dialog(TR("Messkorrektur – Testteil ausmessen"));
    if (eltern) dlg->set_transient_for(*eltern);
    auto box = dlg->get_content_area();

    // Testteil
    box->add(*abschnitt(TR("Testteil")));
    auto g1 = formular();
    auto sq = spin_zeile(*g1, 0, TR("Quadrat Soll"), 40, 5, 1000, 1, 2, TR("mm · grösser = genauer, z. B. 100"));
    auto sd = spin_zeile(*g1, 1, TR("Loch Soll Ø"), 20, 2, 500, 1, 2, "mm");
    Gtk::ComboBoxText *werkzeug = nullptr;
    if (fester) {
        g1->attach(*label(TR("Gefräst mit")), 0, 2);
        auto l = label(werkzeug_name(*fester));
        l->set_xalign(0);
        g1->attach(*l, 1, 2, 2);
    } else {
        werkzeug = combo_zeile(*g1, 2, TR("Gefräst mit"));
        for (auto const &x : wahl) werkzeug->append(x.key, x.name);
        if (!vorwahl.empty()) werkzeug->set_active_id(vorwahl);
        if (werkzeug->get_active_row_number() < 0) werkzeug->set_active(0);
    }
    auto zeichnen = Gtk::make_managed<Gtk::Button>(TR("Testteil zeichnen …"));
    zeichnen->set_tooltip_text(TR("Neue Zeichnung: Quadrat als Kontur aussen und Loch als Kontur innen mit diesem "
                                  "Fräser, 3 mm tief"));
    zeichnen->set_halign(Gtk::ALIGN_START);
    g1->attach(*zeichnen, 1, 3, 2);
    box->add(*g1);
    auto hinweis = label(TR("Messen mit dem Messschieber auf halber Höhe, nach dem Entgraten. Beim Loch die Schenkel "
                            "gerade halten, nicht schräg stellen – den grössten Wert nehmen."));
    hinweis->set_line_wrap(true);
    hinweis->set_max_width_chars(60);
    hinweis->set_xalign(0);
    klasse(*hinweis, "hirch-karte");
    box->add(*hinweis);

    // Gemessen: X und Y
    box->add(*abschnitt(TR("Gemessen")));
    auto g2 = formular();
    g2->attach(*label(TR("X (waagrecht)"), "hirch-titel"), 1, 0);
    g2->attach(*label(TR("Y (senkrecht)"), "hirch-titel"), 2, 0);
    auto messfeld = [](double w) {
        auto adj = Gtk::Adjustment::create(w, 0, 2000, 0.01, 0.1);
        auto s = Gtk::make_managed<Gtk::SpinButton>(adj, 0.01, 2);
        s->set_numeric(true);
        punkt_und_komma(s);
        s->set_width_chars(8);
        return s;
    };
    auto zeile = [&](int z, std::string const &text, Gtk::SpinButton *a, Gtk::SpinButton *b, std::string const &mehr) {
        g2->attach(*label(text), 0, z);
        g2->attach(*a, 1, z);
        if (b) g2->attach(*b, 2, z);
        auto l = label(mehr, "hirch-dim");
        l->set_xalign(0);
        g2->attach(*l, b ? 3 : 2, z, b ? 1 : 2);
    };
    auto qx = messfeld(40), qy = messfeld(40), lx = messfeld(20), ly = messfeld(20), ld = messfeld(0);
    zeile(1, TR("Quadrat aussen"), qx, qy, "mm");
    zeile(2, TR("Loch innen"), lx, ly, "mm");
    zeile(3, TR("Loch diagonal"), ld, nullptr, TR("mm · freiwillig, zeigt, ob das Loch rund ist (0 = nicht gemessen)"));
    box->add(*g2);

    // Ergebnis
    box->add(*abschnitt(TR("Ergebnis")));
    auto g3 = formular();
    g3->attach(*label(TR("Weg der Achse")), 0, 0);
    g3->attach(*label(TR("Fräser je Seite")), 0, 1);
    auto sx_l = wert_label(), sy_l = wert_label(), ex_l = wert_label(), ey_l = wert_label(), em_l = wert_label();
    g3->attach(*sx_l, 1, 0);
    g3->attach(*sy_l, 2, 0);
    g3->attach(*ex_l, 1, 1);
    g3->attach(*ey_l, 2, 1);
    g3->attach(*em_l, 3, 1);
    box->add(*g3);
    auto karte = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);
    klasse(*karte, "hirch-fertig");
    karte->set_margin_top(4);
    auto k_titel = label("", "hirch-titel");
    auto k_text = label("");
    for (auto l : {k_titel, k_text}) {
        l->set_xalign(0);
        l->set_line_wrap(true);
        l->set_max_width_chars(60);
        l->set_margin_start(10);
        l->set_margin_end(10);
        karte->add(*l);
    }
    k_titel->set_margin_top(8);
    k_text->set_margin_bottom(8);
    box->add(*karte);
    auto achse = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);
    klasse(*achse, "hirch-rechnet");
    auto a_titel = label("", "hirch-titel");
    auto a_text = label("");
    auto a_an = Gtk::make_managed<Gtk::CheckButton>(TR("Zusätzlich im G-Code ausgleichen (wenn die Steuerung es nicht kann)"));
    a_an->set_active(num(ma, "skalierung_an", 0) >= 0.5);
    for (Gtk::Widget *l : std::initializer_list<Gtk::Widget *>{a_titel, a_text, a_an}) {
        l->set_margin_start(10);
        l->set_margin_end(10);
        achse->add(*l);
    }
    for (auto l : {a_titel, a_text}) {
        l->set_xalign(0);
        l->set_line_wrap(true);
        l->set_max_width_chars(60);
    }
    a_titel->set_margin_top(8);
    a_an->set_margin_bottom(8);
    achse->set_no_show_all(true);
    box->add(*achse);
    auto warn_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
    box->add(*warn_box);
    Gtk::CheckButton *alte_null = nullptr;
    if (std::abs(alt_global) > 1e-9) {
        alte_null = Gtk::make_managed<Gtk::CheckButton>(
            TRF("Alte Masskorrektur für alle Fräser ({} mm je Seite) auf 0 setzen – sie steckt im Messwert und gilt "
                "künftig nur noch beim gemessenen Fräser", {vz(alt_global, 2)}));
        alte_null->set_active(true);
        if (auto l = dynamic_cast<Gtk::Label *>(alte_null->get_child())) {
            l->set_line_wrap(true);
            l->set_max_width_chars(60);
        }
        box->add(*alte_null);
    }

    dlg->add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
    auto ok = dlg->add_button(TR("Übernehmen"), Gtk::RESPONSE_OK);
    klasse(*ok, "suggested-action");

    auto gewaehlt = [&]() -> Wahl const * {
        if (!werkzeug) return nullptr;
        auto id = werkzeug->get_active_id().raw();
        for (auto const &x : wahl) {
            if (x.key == id) return &x;
        }
        return nullptr;
    };
    double neu_e = 0, s_x = 1, s_y = 1, e_m = 0;
    bool tipp = false, gueltig = false;
    auto rechnen = [&] {
        UI::remove_all_children(*warn_box);
        double Q = wert(sq), D = wert(sd);
        double QX = wert(qx), QY = wert(qy), LX = wert(lx), LY = wert(ly), LD = wert(ld);
        bj::object const *w = fester ? fester : (gewaehlt() ? &gewaehlt()->w : nullptr);
        std::string wname = w ? werkzeug_name(*w) : std::string();
        double d0 = w ? num(*w, "durchmesser", 3) : 3;
        double e_alt = w ? num(*w, "messkorrektur", 0) : 0;
        gueltig = Q > 0 && D > 0 && QX > 0 && QY > 0 && LX > 0 && LY > 0;
        if (!gueltig) {
            for (auto l : {sx_l, sy_l, ex_l, ey_l, em_l}) l->set_text("–");
            ok->set_sensitive(false);
            return;
        }
        s_x = (QX + LX) / (Q + D);
        s_y = (QY + LY) / (Q + D);
        double ex = (LX - D * s_x) / 2, ey = (LY - D * s_y) / 2;
        e_m = (ex + ey) / 2;
        bool null_global = alte_null && alte_null->get_active();
        neu_e = std::clamp(e_alt + e_m + (null_global ? alt_global : 0.0), -1.0, 1.0);
        auto achs_text = [](double s) {
            return std::abs(s - 1) < 0.00005 ? TR("stimmt")
                                             : fest(std::abs(s - 1) * 100, 2) + " % " + (s < 1 ? TR("zu kurz") : TR("zu weit"));
        };
        sx_l->set_text(achs_text(s_x));
        sy_l->set_text(achs_text(s_y));
        ex_l->set_text(vz(ex, 3) + " mm");
        ey_l->set_text(vz(ey, 3) + " mm");
        em_l->set_text(TR("Mittel ") + vz(e_m, 3) + " mm");
        k_titel->set_text(TRF("{} schneidet wie Ø {} mm", {wname, fest(d0 + 2 * neu_e, 2)}));
        std::string t = TRF("Angegeben Ø {} mm. Hirch CAM rechnet mit diesem Fräser künftig so – Konturen, Taschenwände "
                            "und gefräste Löcher kommen aufs Mass.", {fest(d0, 2)});
        if (std::abs(e_alt) > 1e-9 || std::abs(alt_global) > 1e-9)
            t += "\n" + TRF("Beim Fräsen des Testteils galten schon {} mm je Seite – sie sind eingerechnet.",
                            {vz(e_alt + alt_global, 3)});
        k_text->set_text(t);
        // Weg der Achsen: Hinweis ab 0,05 %
        std::string at, tt;
        for (auto [a, s] : {std::pair{"X", s_x}, std::pair{"Y", s_y}}) {
            if (std::abs(s - 1) < 0.0005) continue;
            at += (at.empty() ? "" : ", ") + TRF("{} fährt {} % {}", {a, fest(std::abs(s - 1) * 100, 2),
                                                                    s < 1 ? TR("zu kurz") : TR("zu weit")});
            tt += (tt.empty() ? "" : "\n") +
                  TRF("Achse {}: Schritte pro mm in der Steuerung × {} (Beispiel 400 → {}). RichAuto/NcStudio "
                      "«Impulsäquivalent»: × {}.", {a, fest(1 / s, 4), fest(400 / s, 1), fest(s, 4)});
        }
        a_titel->set_text(at);
        a_text->set_text(tt);
        zeile_zeigen(achse, !at.empty() || a_an->get_active());
        // Prüfungen
        std::vector<std::string> warn;
        if (std::abs(LX - LY) > GRENZE_SPIEL)
            warn.push_back(TRF("Loch in X und Y {} mm verschieden – Spiel oder Umkehrspiel in einer Achse? Riemen, "
                               "Spindelmutter und Lager prüfen, bevor du korrigierst.", {fest(std::abs(LX - LY), 2)}));
        tipp = std::abs(s_x - 1) > GRENZE_SKALIERUNG || std::abs(s_y - 1) > GRENZE_SKALIERUNG ||
               std::abs(ex) > GRENZE_E || std::abs(ey) > GRENZE_E;
        if (tipp) warn.push_back(TR("Unwahrscheinlich grosse Abweichung – wohl ein Tippfehler. Bitte die Messwerte prüfen."));
        if (LD > 0 && std::abs(LD - (LX + LY) / 2) > GRENZE_RUND)
            warn.push_back(TRF("Diagonal {} mm, X und Y im Mittel {} mm – das Loch ist nicht rund (Spiel oder der "
                               "Fräser schwingt).", {fest(LD, 2), fest((LX + LY) / 2, 2)}));
        for (auto const &x : warn) {
            auto l = label(x);
            l->set_xalign(0);
            l->set_line_wrap(true);
            l->set_max_width_chars(60);
            klasse(*l, "hirch-warn");
            warn_box->add(*l);
        }
        warn_box->show_all();
        ok->set_sensitive(true);
        if (auto b = dynamic_cast<Gtk::Button *>(ok)) {
            b->set_label(tipp ? TR("Trotzdem übernehmen") : fester ? TR("Übernehmen") : TR("Für diesen Fräser übernehmen"));
            klasse(*b, "destructive-action", tipp);
            klasse(*b, "suggested-action", !tipp);
        }
    };
    // Sollmasse geändert: Messfelder, die noch auf dem alten Soll stehen, gehen mit
    double q_alt = 40, d_alt = 20;
    sq->signal_value_changed().connect([&] {
        for (auto s : {qx, qy}) {
            if (std::abs(s->get_value() - q_alt) < 1e-9) s->set_value(sq->get_value());
        }
        q_alt = sq->get_value();
        rechnen();
    });
    sd->signal_value_changed().connect([&] {
        for (auto s : {lx, ly}) {
            if (std::abs(s->get_value() - d_alt) < 1e-9) s->set_value(sd->get_value());
        }
        d_alt = sd->get_value();
        rechnen();
    });
    for (auto s : {qx, qy, lx, ly, ld}) s->signal_value_changed().connect(rechnen);
    if (werkzeug) werkzeug->signal_changed().connect(rechnen);
    if (alte_null) alte_null->signal_toggled().connect(rechnen);
    a_an->signal_toggled().connect(rechnen);

    zeichnen->signal_clicked().connect([&] {
        bj::object w = fester ? *fester : (gewaehlt() ? gewaehlt()->w : bj::object());
        if (w.empty()) return;
        if (!fester && gewaehlt() && !gewaehlt()->db_id.empty()) w["db_id"] = gewaehlt()->db_id;
        w.erase("nur_hier");
        bj::object a;
        a["q"] = wert(sq);
        a["d"] = wert(sd);
        a["werkzeug"] = w;
        a["material"] = str(einst, "material", "hartholz");
        if (auto m = unter(analyse, "material")) a["dicke"] = num(*m, "dicke", 18);
        std::string aus, fehler;
        bj::object r;
        if (!python_jetzt({"messkorrektur", "testteil", bj::serialize(a)}, aus, fehler) || !json_lesen(aus, r) ||
            !flag(r, "ok", false)) {
            meldung(TR("Testteil nicht gezeichnet"), (fehler.empty() ? str(r, "fehler", aus) : fehler).substr(0, 400),
                    Gtk::MESSAGE_ERROR);
            return;
        }
        auto pfad = str(r, "datei");
        Glib::signal_idle().connect_once([pfad] {
            if (auto app = InkscapeApplication::instance()) {
                if (auto d2 = app->document_new(pfad)) app->create_window(d2, false);
            }
        });
        dlg->response(Gtk::RESPONSE_CANCEL);
    });

    dlg->show_all();
    zeile_zeigen(achse, false);
    rechnen();
    if (dlg->run() != Gtk::RESPONSE_OK || !gueltig) return {};
    dlg->hide();

    bj::object info;
    info["datum"] = Glib::DateTime::create_now_local().format("%Y-%m-%d").raw();
    info["quadrat"] = wert(sq);
    info["loch"] = wert(sd);
    info["gemessen"] = bj::array{wert(qx), wert(qy), wert(lx), wert(ly)};
    info["material"] = str(einst, "material", "hartholz");
    info["s_x"] = s_x;
    info["s_y"] = s_y;
    if (info_aus) *info_aus = info;
    bool null_global = alte_null && alte_null->get_active();

    // ablegen: Fräser in «Meine Werkzeuge» (nicht im Werkzeug-Dialog – der speichert selbst), Maschine
    bj::object auftrag;
    auto const *x = gewaehlt();
    auftrag["id"] = (!fester && x) ? x->db_id : std::string();
    auftrag["messkorrektur"] = neu_e;
    auftrag["info"] = info;
    auftrag["s_x"] = s_x;
    auftrag["s_y"] = s_y;
    auftrag["skalierung_an"] = a_an->get_active();
    auftrag["alte_null"] = null_global;
    std::string aus, fehler;
    bj::object r;
    if (!python_jetzt({"messkorrektur", "speichern", bj::serialize(auftrag)}, aus, fehler) || !json_lesen(aus, r) ||
        !flag(r, "ok", false)) {
        meldung(TR("Messkorrektur nicht gespeichert"), (fehler.empty() ? str(r, "fehler", aus) : fehler).substr(0, 400),
                Gtk::MESSAGE_ERROR);
        return {};
    }
    if (auto m = unter(analyse, "maschine")) {
        (*m)["skalierung_an"] = a_an->get_active() ? 1.0 : 0.0;
        (*m)["skalierung_x"] = s_x;
        (*m)["skalierung_y"] = s_y;
        if (null_global) (*m)["masskorrektur"] = 0.0;
    }
    if (auto l = liste_von(analyse, "datenbank"); l && r.if_contains("werkzeug") && r["werkzeug"].is_object()) {
        for (auto &v : *l) {
            if (v.is_object() && str(v.as_object(), "id") == str(r["werkzeug"].as_object(), "id")) v = r["werkzeug"];
        }
    }
    if (fester) return neu_e;
    // Werkzeuge der Zeichnung: der gewählte Platz und alle mit demselben Fräser aus «Meine Werkzeuge»
    bool geaendert = false;
    if (auto ws = einst.if_contains("werkzeuge"); ws && ws->is_object() && x) {
        for (auto &kv : ws->as_object()) {
            if (!kv.value().is_object()) continue;
            auto &w = kv.value().as_object();
            bool trifft = x->key == "slot:" + std::string(kv.key()) ||
                          (!x->db_id.empty() && !flag(w, "nur_hier", false) && str(w, "db_id") == x->db_id);
            if (!trifft) continue;
            if (std::abs(neu_e) < 1e-6) {
                w.erase("messkorrektur");
                w.erase("messkorrektur_info");
            } else {
                w["messkorrektur"] = std::round(neu_e * 1e4) / 1e4;
                w["messkorrektur_info"] = info;
            }
            geaendert = true;
        }
    }
    if (geaendert) {
        schreiben(TR("Messkorrektur"));
    } else {
        letzte_xml.clear();
        spaeter(10);
    }
    return neu_e;
}

} // namespace Inkscape::UI::Dialog
