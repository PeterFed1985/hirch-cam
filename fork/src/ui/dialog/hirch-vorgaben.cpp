// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CAM: Einstellungen › «So fräse ich» – Vorgaben für neue Bearbeitungen (Peter 07.11.2026).
 * Entwurf: hirch-cnc-wissen/sitzungen/basis/entwuerfe/basis-so-fraese-ich.png (Listen von fraesbahnen und 3d).
 * Die Werte, Grenzen und Felder stehen in hirch_cam/vorgaben.py; hier nur die Bedienung.
 * Formen wie im übrigen Programm (Peter: Knöpfe mit kleinem Radius, keine runden Pillen): GTK-Standard.
 */
#include "ui/dialog/hirch-intern.h"

#include <gtkmm/checkbutton.h>
#include <gtkmm/radiobutton.h>

namespace Inkscape::UI::Dialog {

namespace {

/// Bild aus assets/vorgaben (SVG), Breite b
Gtk::Widget *vorgabe_bild(char const *name, int b)
{
    auto pfad = Glib::build_filename(hirch_prefix(), "share", "inkscape", "hirch-cam", "assets", "vorgaben",
                                     std::string(name) + ".svg");
    try {
        return Gtk::make_managed<Gtk::Image>(Gdk::Pixbuf::create_from_file(pfad, b, b, true));
    } catch (...) {
        return Gtk::make_managed<Gtk::Label>("");
    }
}

Gtk::Label *hinweis(std::string const &text)
{
    auto l = label(text, "hirch-dim");
    klasse(*l, "hirch-klein");
    l->set_line_wrap(true);
    l->set_max_width_chars(44);
    l->set_xalign(0);
    return l;
}

void sichtbar(Gtk::Widget *w, bool an)
{
    w->set_no_show_all(!an);
    if (an) w->show_all();
    else w->hide();
}

Gtk::Label *unterkopf(std::string const &text)
{
    auto l = abschnitt(text);
    l->set_margin_top(6);
    return l;
}

} // namespace

void HirchFraesen::Impl::vorgaben_abschnitt(Gtk::Box *ziel)
{
    UI::remove_all_children(*ziel);
    ziel->add(*hinweis(TR("Gilt für neue Bearbeitungen. Bestehende bleiben, wie sie sind. Was zur Zeichnung oder zum "
                          "Fräser gehört (Tiefe, Werkzeug, Zustellung), stellst du in der Bearbeitung ein.")));
    auto inhalt = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
    ziel->add(*inhalt);
    ziel->show_all();
    Gtk::Box *ziel_p = ziel;
    python({"datenbank", "werkstoffe"}, [this, inhalt, ziel_p](bool ok, std::string const &, std::string const &) {
        if (!ok) return;
        auto l = liste_von(hirch_vorgaben(), "liste");
        if (!l) return;
        // Werte nach Schlüssel
        auto werte = std::make_shared<std::map<std::string, bj::object>>();
        for (auto const &e : *l) {
            if (auto o = e.if_object()) (*werte)[str(*o, "key")] = *o;
        }
        auto wert_von = [werte](std::string const &k) -> bj::value {
            auto it = werte->find(k);
            if (it == werte->end()) return nullptr;
            auto w = it->second.find("wert");
            return w == it->second.end() ? bj::value(nullptr) : w->value();
        };
        // Speichern: sofort (Wahl, Schalter) oder nach kurzer Pause (Zahlen)
        auto setzen = [this](std::string const &key, bj::value const &v) {
            bj::object a;
            a["key"] = key;
            a["wert"] = v;
            python({"datenbank", "vorgabe", bj::serialize(a)}, [](bool, std::string const &, std::string const &) {});
        };
        auto spaeter = std::make_shared<std::map<std::string, sigc::connection>>();
        auto zahl_setzen = [setzen, spaeter](std::string const &key, Gtk::SpinButton *s) {
            (*spaeter)[key].disconnect();
            (*spaeter)[key] = Glib::signal_timeout().connect([setzen, key, s] {
                setzen(key, bj::value(wert(s)));
                return false;
            }, 400);
        };

        // ---- Bausteine
        // Kacheln mit Bild (Eintauchen, Fräsrichtung): zwei Knöpfe, einer gedrückt
        auto kacheln = [&, setzen](std::string const &key, std::vector<std::tuple<bj::value, char const *, std::string>> wahl) {
            auto reihe = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
            reihe->set_homogeneous(true);
            Gtk::RadioButton::Group gruppe;
            auto jetzt = wert_von(key);
            for (auto const &[v, bild, text] : wahl) {
                auto k = Gtk::make_managed<Gtk::RadioButton>(gruppe);
                k->set_mode(false);                       // als Knopf, nicht als Kreis
                auto box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);
                box->set_margin_top(4);
                box->set_margin_bottom(2);
                box->add(*vorgabe_bild(bild, 132));
                auto t = label(text, nullptr, 0.5f);
                t->set_line_wrap(true);
                t->set_justify(Gtk::JUSTIFY_CENTER);
                box->add(*t);
                k->add(*box);
                k->set_active(jetzt == v);
                k->signal_toggled().connect([k, setzen, key, v] {
                    if (k->get_active()) setzen(key, v);
                });
                reihe->add(*k);
            }
            inhalt->add(*reihe);
        };
        // Zusammenhängende Knöpfe (Wahl aus wenigen Werten)
        auto knoepfe = [&, setzen](Gtk::Box *wohin, std::string const &key, std::vector<std::pair<bj::value, std::string>> wahl,
                                   std::function<void(bj::value const &)> danach = {}) {
            auto reihe = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
            klasse(*reihe, "linked");
            reihe->set_halign(Gtk::ALIGN_START);
            Gtk::RadioButton::Group gruppe;
            auto jetzt = wert_von(key);
            for (auto const &[v, text] : wahl) {
                auto k = Gtk::make_managed<Gtk::RadioButton>(gruppe, text);
                k->set_mode(false);
                k->set_active(jetzt == v);
                k->signal_toggled().connect([k, setzen, key, v, danach] {
                    if (!k->get_active()) return;
                    setzen(key, v);
                    if (danach) danach(v);
                });
                reihe->add(*k);
            }
            wohin->add(*reihe);
            return reihe;
        };
        // Schalter mit Text
        auto schalter = [&, setzen](Gtk::Box *wohin, std::string const &key, std::string const &text) {
            auto z = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 12);
            auto t = label(text);
            t->set_hexpand(true);
            t->set_line_wrap(true);
            z->add(*t);
            auto s = Gtk::make_managed<Gtk::Switch>();
            s->set_valign(Gtk::ALIGN_CENTER);
            auto w = wert_von(key);
            s->set_active(w.is_bool() && w.as_bool());
            s->property_active().signal_changed().connect([s, setzen, key] { setzen(key, bj::value(s->get_active())); });
            z->add(*s);
            wohin->add(*z);
        };
        // Zahl in einem Formular
        auto zahl = [&, zahl_setzen](Gtk::Grid &g, int zeile, std::string const &key, std::string const &text,
                                     double schritt, int stellen, std::string const &einheit) {
            auto it = werte->find(key);
            double lo = 0, hi = 1000;
            if (it != werte->end()) {
                if (auto gr = liste_von(it->second, "grenzen"); gr && gr->size() == 2) {
                    lo = (*gr)[0].to_number<double>();
                    hi = (*gr)[1].to_number<double>();
                }
            }
            auto w = wert_von(key);
            double v = w.is_number() ? w.to_number<double>() : lo;
            auto s = spin_zeile(g, zeile, text, v, lo, hi, schritt, stellen, einheit);
            rad_schuetzen(*s);
            s->signal_value_changed().connect([zahl_setzen, key, s] { zahl_setzen(key, s); });
            return s;
        };
        auto formular_ = [&](Gtk::Box *wohin) {
            auto g = formular();
            wohin->add(*g);
            return g;
        };

        // ================================================================ 2D, vorne
        inhalt->add(*unterkopf(TR("Eintauchen")));
        kacheln("eintauchen_art", {{bj::value("senkrecht"), "hirch-eintauchen-senkrecht", TR("Senkrecht")},
                                   {bj::value("rampe"), "hirch-eintauchen-rampe", TR("Schräg (Rampe)")}});
        {
            auto g = formular_(inhalt);
            auto eigen = Gtk::make_managed<Gtk::CheckButton>(TR("Eigener Rampenwinkel"));
            auto w = wert_von("rampe_winkel");
            eigen->set_active(w.is_number());
            eigen->set_hexpand(true);
            g->attach(*eigen, 0, 0);
            auto adj = Gtk::Adjustment::create(w.is_number() ? w.to_number<double>() : 3.0, 0.5, 45, 0.5, 5);
            auto s = Gtk::make_managed<Gtk::SpinButton>(adj, 0.5, 1);
            s->set_numeric(true);
            punkt_und_komma(s);
            s->set_width_chars(7);
            s->set_sensitive(eigen->get_active());
            rad_schuetzen(*s);
            g->attach(*s, 1, 0);
            g->attach(*label("°", "hirch-dim"), 2, 0);
            eigen->signal_toggled().connect([eigen, s, setzen] {
                s->set_sensitive(eigen->get_active());
                setzen("rampe_winkel", eigen->get_active() ? bj::value(wert(s)) : bj::value(nullptr));
            });
            s->signal_value_changed().connect([eigen, s, zahl_setzen] {
                if (eigen->get_active()) zahl_setzen("rampe_winkel", s);
            });
        }
        inhalt->add(*hinweis(TR("Ohne eigenen Winkel gilt der Wert am Fräser. Fräser ohne Mittenschnitt tauchen nie "
                                "senkrecht ein – die Bearbeitung sagt es dann.")));

        inhalt->add(*unterkopf(TR("Fräsrichtung")));
        kacheln("gleichlauf", {{bj::value(true), "hirch-gleichlauf", TR("Gleichlauf · sauberere Kante")},
                               {bj::value(false), "hirch-gegenlauf", TR("Gegenlauf · ruhiger bei Spiel")}});
        inhalt->add(*hinweis(TR("Draufsicht, Spindel rechtsdrehend. Rot: wohin die Schneide läuft, wo sie greift. "
                                "Gilt für Tasche, Kontur und Bohren.")));

        inhalt->add(*unterkopf(TR("Taschen")));
        knoepfe(inhalt, "taschen_strategie", {{bj::value("adaptiv"), TR("Adaptiv (schnell, schonend)")},
                                              {bj::value("runden"), TR("In Runden")}});
        {
            auto g = formular_(inhalt);
            zahl(*g, 0, "taschen_bahnabstand", TR("Bahnabstand bei «In Runden»"), 1, 0, "%");
        }
        inhalt->add(*hinweis(TR("5–60 %. Über 50 % fährt Hirch CAM innen eine Runde mehr.")));

        inhalt->add(*unterkopf(TR("Haltestege (Kontur ganz durch)")));
        auto steg_werte = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 0);
        knoepfe(inhalt, "stege_modus", {{bj::value("auto"), TR("Mit Haltestegen")}, {bj::value("keine"), TR("Keine")}},
                [steg_werte](bj::value const &v) { sichtbar(steg_werte, v != bj::value("keine")); });
        {
            auto g = formular_(steg_werte);
            zahl(*g, 0, "stege_anzahl", TR("Anzahl je Kontur"), 1, 0, "");
            zahl(*g, 1, "stege_breite", TR("Breite"), 0.5, 1, TR("mm"));
            zahl(*g, 2, "stege_hoehe", TR("Dicke"), 0.1, 2, TR("mm"));
            inhalt->add(*steg_werte);
        }

        inhalt->add(*unterkopf(TR("Schlichtgang (Tasche, Kontur)")));
        auto schlicht_werte = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);
        knoepfe(inhalt, "schlichten", {{bj::value(true), TR("Mit Schlichtgang")}, {bj::value(false), TR("Ohne")}},
                [schlicht_werte](bj::value const &v) { sichtbar(schlicht_werte, v == bj::value(true)); });
        {
            auto g = formular_(schlicht_werte);
            zahl(*g, 0, "schlicht_zugabe", TR("Zugabe"), 0.05, 2, TR("mm"));
            schlicht_werte->add(*hinweis(TR("Zustellung beim Schlichten: ganze Tiefe, in der Bearbeitung änderbar. Ist "
                                            "die Zugabe breiter als der Schlichtfräser, fährt er mehrere Runden.")));
            inhalt->add(*schlicht_werte);
        }

        // ================================================================ 3D, vorne
        inhalt->add(*unterkopf(TR("3D-Schlichten: Strategie")));
        {
            auto box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
            // sechs Strategien: zwei Reihen à drei
            auto r1 = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);
            Gtk::RadioButton::Group gruppe;
            auto jetzt = wert_von("schlicht3d_strategie");
            std::vector<std::pair<char const *, std::string>> s3 = {
                {"parallel", TR("Parallel")}, {"hoehenlinien", TR("Höhenlinien")}, {"kombiniert", TR("Kombiniert")},
                {"ebenen", TR("Ebenen")}, {"kehlen", TR("Kehlen")}, {"konturen", TR("Konturen je Form")}};
            Gtk::Box *reihe = nullptr;
            for (std::size_t i = 0; i < s3.size(); ++i) {
                if (i % 3 == 0) {
                    reihe = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
                    klasse(*reihe, "linked");
                    reihe->set_homogeneous(true);
                    r1->add(*reihe);
                }
                auto k = Gtk::make_managed<Gtk::RadioButton>(gruppe, s3[i].second);
                k->set_mode(false);
                std::string v = s3[i].first;
                k->set_active(jetzt == bj::value(v));
                k->signal_toggled().connect([k, setzen, v] {
                    if (k->get_active()) setzen("schlicht3d_strategie", bj::value(v));
                });
                reihe->add(*k);
            }
            box->add(*r1);
            inhalt->add(*box);
            auto g = formular_(inhalt);
            auto lr = label(TR("Richtung (Parallel, Kombiniert)"));
            lr->set_hexpand(true);
            g->attach(*lr, 0, 0);
            auto rb = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
            g->attach(*rb, 1, 0, 2, 1);
            knoepfe(rb, "schlicht3d_richtung", {{bj::value("x"), "X"}, {bj::value("y"), "Y"}});
            zahl(*g, 1, "schlicht3d_grenzwinkel", TR("Grenzwinkel (Kombiniert)"), 1, 0, "°");
        }
        inhalt->add(*hinweis(TR("Ein Schaftfräser schlichtet nur «Ebenen» sinnvoll – bei anderer Strategie zeigt die "
                                "Bearbeitung einen Hinweis.")));

        inhalt->add(*unterkopf(TR("3D: Bahnabstand (% vom Ø)")));
        {
            auto g = formular_(inhalt);
            zahl(*g, 0, "schlicht3d_bahnabstand", TR("3D-Schlichten"), 1, 0, "%");
            zahl(*g, 1, "schrupp3d_bahnabstand", TR("3D-Schruppen, Böden und Wände"), 1, 0, "%");
        }
        inhalt->add(*hinweis(TR("3D-Schlichten 3–40 %: darüber wird die Fläche bei der Kugel sichtbar rau.")));

        inhalt->add(*unterkopf(TR("3D-Schruppen")));
        {
            auto g = formular_(inhalt);
            zahl(*g, 0, "schrupp3d_aufmass", TR("Aufmass fürs Schlichten"), 0.05, 2, TR("mm"));
            schalter(inhalt, "schrupp3d_adaptiv", TR("Schnell schruppen (adaptiv)"));
            auto g2 = formular_(inhalt);
            zahl(*g2, 0, "schrupp3d_adaptiv_last", TR("Last (seitlich, % vom Ø)"), 1, 0, "%");
        }
        inhalt->add(*hinweis(TR("«Böden und Wände» hat 0 mm Aufmass – es muss kleiner bleiben als beim Schruppen, sonst "
                                "bleibt alles stehen.")));

        inhalt->add(*unterkopf(TR("Fertiges auslassen (3D-Schlichten)")));
        schalter(inhalt, "auslassen_fertig", TR("Schon fertige Flächen auslassen"));
        schalter(inhalt, "fuss_umlauf", TR("Umlauf um den Fuss"));
        schalter(inhalt, "kehlen_nachfahren", TR("Kehlen nachfahren"));

        // ================================================================ Details (aufklappen; kein Gtk::Expander –
        // unter Windows liessen sich offene nicht zuklappen, 3d 07.11.)
        auto det_knopf = Gtk::make_managed<Gtk::Button>(TR("Details ›"));
        det_knopf->set_halign(Gtk::ALIGN_START);
        det_knopf->set_relief(Gtk::RELIEF_NONE);
        klasse(*det_knopf, "hirch-link");
        det_knopf->set_margin_top(6);
        inhalt->add(*det_knopf);
        auto det = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
        det->set_no_show_all(true);
        inhalt->add(*det);
        det->add(*unterkopf(TR("2D")));
        {
            auto g = formular_(det);
            auto la = label(TR("Anfahren (Kontur)"));
            la->set_hexpand(true);
            g->attach(*la, 0, 0);
            auto ab = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
            g->attach(*ab, 1, 0, 2, 1);
            knoepfe(ab, "anfahren", {{bj::value("bogen"), TR("Im Bogen")}, {bj::value(""), TR("Senkrecht")}});
            zahl(*g, 1, "planen_bahnabstand", TR("Planen: Bahnabstand"), 1, 0, "%");
            auto lp = label(TR("Planen: Richtung"));
            g->attach(*lp, 0, 2);
            auto pb = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
            g->attach(*pb, 1, 2, 2, 1);
            knoepfe(pb, "planen_richtung", {{bj::value("x"), "X"}, {bj::value("y"), "Y"}});
        }
        det->add(*label(TR("Bohren")));
        knoepfe(det, "bohren_methode", {{bj::value("auto"), TR("Automatisch")}, {bj::value("bohren"), TR("Bohren")},
                                         {bj::value("helix"), TR("Helix")}});
        schalter(det, "bohren_spitze_dazu", TR("Bohrer: voller Ø bis zur Tiefe (Spitze reicht weiter)"));
        schalter(det, "adaptiv_wechsel", TR("Adaptiv: Richtung wechseln"));
        schalter(det, "dogbone", TR("Innenecken freifräsen (Dogbone)"));
        det->add(*label(TR("V-Carve-Wand bei Taschen")));
        knoepfe(det, "v_wand", {{bj::value("oben"), TR("Oben genau auf der Linie")}, {bj::value("unten"), TR("Unten auf der Linie")}});
        det->add(*unterkopf(TR("3D")));
        {
            auto g = formular_(det);
            zahl(*g, 0, "schrupp3d_lagen", TR("3D-Schruppen: Lagen ohne Abheben"), 0.5, 1, TR("mm"));
            zahl(*g, 1, "kehlen_winkel", TR("Kehlen: Knick ab"), 1, 0, "°");
            zahl(*g, 2, "kehlen_bahnen", TR("Kehlen: Bahnen je Seite"), 1, 0, "");
            zahl(*g, 3, "bw_stufe", TR("Böden und Wände: Stufe"), 0.5, 1, TR("mm"));
            zahl(*g, 4, "rest_schwelle", TR("«Nur Restmaterial» ab"), 0.05, 2, TR("mm"));
        }
        schalter(det, "schlicht3d_genau", TR("Höhenlinien genau rechnen"));
        schalter(det, "bw_boeden", TR("Böden und Wände: Böden"));
        schalter(det, "bw_waende", TR("Böden und Wände: Wände"));
        det->add(*hinweis(TR("Abtastung und Rampe beim 3D-Eintauchen: automatisch bzw. Wert am Fräser.")));
        det_knopf->signal_clicked().connect([det, det_knopf] {
            bool an = !det->get_visible();
            det->set_no_show_all(!an);
            if (an) det->show_all();
            else det->hide();
            det_knopf->set_label(an ? TR("Details ‹") : TR("Details ›"));
        });

        auto zurueck = Gtk::make_managed<Gtk::Button>(TR("Alle auf die Vorgaben von Hirch CAM"));
        zurueck->set_halign(Gtk::ALIGN_START);
        zurueck->set_margin_top(10);
        inhalt->add(*zurueck);
        zurueck->signal_clicked().connect([this, ziel_p] {
            bj::object a;
            a["alle_zurueck"] = true;
            python({"datenbank", "vorgabe", bj::serialize(a)}, [this, ziel_p](bool, std::string const &, std::string const &) {
                vorgaben_abschnitt(ziel_p);
            });
        });
        inhalt->show_all();
        // Abhängige Zeilen nach dem Zeigen
        sichtbar(steg_werte, wert_von("stege_modus") != bj::value("keine"));
        sichtbar(schlicht_werte, wert_von("schlichten") == bj::value(true));
    });
}

namespace {

/// Name einer Vorgabe in der Bearbeitung (Schlüssel aus vorgaben.py)
std::string vorgabe_name(std::string const &k)
{
    static std::map<std::string, char const *> const N = {
        {"eintauchen_art", "Eintauchen"}, {"rampe_winkel", "Rampenwinkel"}, {"gleichlauf", "Fräsrichtung"},
        {"taschen_strategie", "Strategie"}, {"taschen_bahnabstand", "Bahnabstand"}, {"stege_modus", "Haltestege"},
        {"stege_anzahl", "Anzahl je Kontur"}, {"stege_breite", "Stegbreite"}, {"stege_hoehe", "Steghöhe"},
        {"schlichten", "Schlichtgang"}, {"schlicht_zugabe", "Schlicht-Zugabe"},
        {"schlicht_zustellung", "Zustellung beim Schlichten"}, {"anfahren", "Anfahren"},
        {"adaptiv_wechsel", "Adaptiv: Richtung wechseln"}, {"planen_bahnabstand", "Bahnabstand"},
        {"planen_richtung", "Richtung"}, {"bohren_methode", "Methode"}, {"bohren_spitze_dazu", "Spitze dazu"},
        {"dogbone", "Dogbone"}, {"v_wand", "Wand beim V-Fräser"},
        // 3D (Liste der 3D-Sitzung, 08.11.)
        {"schlicht3d_strategie", "Strategie (3D-Schlichten)"}, {"schlicht3d_richtung", "Richtung (3D-Schlichten)"},
        {"schlicht3d_grenzwinkel", "Steil ab"}, {"schlicht3d_bahnabstand", "Bahnabstand (3D-Schlichten)"},
        {"schrupp3d_bahnabstand", "Bahnabstand (3D-Schruppen)"}, {"schrupp3d_aufmass", "Aufmass"},
        {"schrupp3d_adaptiv", "Adaptiv"}, {"schrupp3d_adaptiv_last", "Schnittbreite"},
        {"auslassen_fertig", "Was schon auf Mass ist, auslassen"}, {"fuss_umlauf", "Rundungen am Fuss umfahren"},
        {"kehlen_nachfahren", "Kehlen zuletzt nachfahren"}, {"schrupp3d_lagen", "Lücke für Lücke in Lagen von"},
        {"kehlen_winkel", "Kehle ab Knick"}, {"kehlen_bahnen", "Bahnen beidseits"},
        {"schlicht3d_genau", "Höhenlinien genau rechnen"}, {"schlicht3d_abtastung", "Abtastung"},
        {"bw_boeden", "Böden"}, {"bw_waende", "Wände"}, {"bw_stufe", "Stufe"}, {"rest_schwelle", "Restmaterial ab"}};
    auto it = N.find(k);
    return it == N.end() ? k : TR(it->second);
}

std::string vorgabe_text(std::string const &k, bj::value const *v)
{
    if (!v || v->is_null()) {
        if (k == "rampe_winkel") return TR("Wert am Fräser");
        if (k == "schlicht_zustellung") return TR("ganze Tiefe");
        if (k == "schlicht3d_abtastung") return TR("automatisch");
        return "–";
    }
    if (v->is_bool()) {
        if (k == "gleichlauf") return v->as_bool() ? TR("Gleichlauf") : TR("Gegenlauf");
        return v->as_bool() ? TR("an") : TR("aus");
    }
    if (v->is_number()) {
        auto t = zahl(v->to_number<double>());
        if (k == "taschen_bahnabstand" || k == "planen_bahnabstand" || k == "schlicht3d_bahnabstand" ||
            k == "schrupp3d_bahnabstand" || k == "schrupp3d_adaptiv_last")
            return t + " %";
        if (k == "rampe_winkel" || k == "schlicht3d_grenzwinkel" || k == "kehlen_winkel") return t + "°";
        if (k == "stege_anzahl" || k == "kehlen_bahnen") return t;
        return t + " " + TR("mm");
    }
    if (v->is_string()) {
        static std::map<std::string, char const *> const W = {
            {"rampe", "Schräg (Rampe)"}, {"senkrecht", "Senkrecht"}, {"adaptiv", "Adaptiv"}, {"runden", "In Runden"},
            {"auto", "Automatisch"}, {"keine", "Keine"}, {"bogen", "Im Bogen"}, {"", "Senkrecht"},
            {"bohren", "Bohren"}, {"helix", "Helix"}, {"oben", "Oben"}, {"unten", "Unten"}, {"x", "X"}, {"y", "Y"},
            {"konturen", "Konturen je Form"}, {"parallel", "Parallele Bahnen"}, {"hoehenlinien", "Höhenlinien"},
            {"kombiniert", "Kombiniert"}, {"ebenen", "Waagrechte Flächen"}, {"kehlen", "Kehlen nachfahren"}};
        std::string s(v->as_string());
        if (k == "schlicht3d_richtung" && (s == "x" || s == "y")) return s == "x" ? TR("entlang X") : TR("entlang Y");
        if (k == "stege_modus" && s == "auto") return TR("Mit Haltestegen");
        auto it = W.find(s);
        return it == W.end() ? s : TR(it->second);
    }
    return "…";
}

} // namespace

int HirchFraesen::Impl::vorgaben_zeigen(Gtk::Box *box, std::string const &art, bj::object const &jetzt)
{
    UI::remove_all_children(*box);
    auto l = liste_von(hirch_vorgaben(), "liste");
    if (!l) return 0;
    bj::object merken;               // abweichende Werte dieser Bearbeitung (für «Als Vorgabe merken»)
    int gleich = 0, anders = 0;
    auto zeilen = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 3);
    for (auto const &e : *l) {
        auto o = e.if_object();
        if (!o) continue;
        auto f = unter(*o, "felder");
        if (!f) continue;
        auto it = f->find(art);
        if (it == f->end() || !it->value().is_string()) continue;
        std::string feld(it->value().as_string());
        std::string key = str(*o, "key");
        auto vw = o->if_contains("wert");
        if (!vw) continue;
        // fehlt der Wert in der Bearbeitung, gilt der Standard von Hirch CAM (so rechnet auch die Bearbeitung)
        auto jw = jetzt.if_contains(feld);
        if (!jw) jw = o->if_contains("standard");
        bool gleich_w = jw && ((*jw == *vw) || (jw->is_null() && vw->is_null()) ||
                               (jw->is_number() && vw->is_number() &&
                                std::abs(jw->to_number<double>() - vw->to_number<double>()) < 1e-9));
        // «Wert am Fräser» / «ganze Tiefe»: der Dialog trägt die Zahl ein, die daraus folgt – kein Unterschied
        if (!gleich_w && vw->is_null() && jw && jw->is_number()) {
            if (key == "rampe_winkel") gleich_w = true;
            if (key == "schlicht_zustellung")
                gleich_w = std::abs(jw->to_number<double>() - num(jetzt, "tiefe", -1)) < 1e-6 ||
                           jw->to_number<double>() <= 0;
        }
        if (gleich_w) {
            ++gleich;
            continue;
        }
        ++anders;
        merken[key] = jw ? *jw : bj::value(nullptr);
        auto z = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        auto n = label(vorgabe_name(key) + ": " + vorgabe_text(key, jw));
        n->set_hexpand(true);
        n->set_line_wrap(true);
        z->add(*n);
        auto v = label("● " + TRF("geändert (Vorgabe: {})", {vorgabe_text(key, vw)}), "hirch-warntext");
        klasse(*v, "hirch-klein");
        z->add(*v);
        zeilen->add(*z);
    }
    if (gleich + anders == 0) {
        box->show_all();
        return 0;
    }
    auto kopf = label(TR("SO FRÄSE ICH"), "hirch-kopf");
    kopf->set_margin_top(10);
    box->add(*kopf);
    auto t = anders == 0 ? TRF("Alle {} Werte wie «So fräse ich».", {std::to_string(gleich)})
                         : TRF("{} Werte wie «So fräse ich», {} geändert:", {std::to_string(gleich), std::to_string(anders)});
    auto lt = label(t, "hirch-dim");
    klasse(*lt, "hirch-klein");
    lt->set_line_wrap(true);
    lt->set_xalign(0);
    box->add(*lt);
    box->add(*zeilen);
    if (anders > 0) {
        auto k = Gtk::make_managed<Gtk::Button>(TR("Als Vorgabe merken"));
        k->set_tooltip_text(TR("Die geänderten Werte gelten ab jetzt für neue Bearbeitungen (Einstellungen › So fräse ich)"));
        k->set_halign(Gtk::ALIGN_START);
        box->add(*k);
        auto jetzt_k = std::make_shared<bj::object>(jetzt);
        k->signal_clicked().connect([this, box, art, jetzt_k, merken] {
            bj::object a;
            a["werte"] = merken;
            python({"datenbank", "vorgabe", bj::serialize(a)},
                   [this, box, art, jetzt_k](bool ok, std::string const &, std::string const &) {
                       if (ok) vorgaben_zeigen(box, art, *jetzt_k);
                   });
        });
    }
    box->show_all();
    return anders;
}

} // namespace Inkscape::UI::Dialog
