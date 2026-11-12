// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: 3D – Bearbeitungen aus dem Modell anlegen, zweiseitig, Masse lesen, Nullpunkt am Block (aus hirch-3d.cpp)
 */
#include "ui/dialog/hirch-3d-intern.h"

namespace Inkscape::UI::Dialog {

static std::string platz_fuer_werkzeug(bj::object &ws, bj::object const &w);

/// Neue Bearbeitungen aus dem Reiter «3D» (Erkennen, Flächen): [{art, …Werte…}] – «aendern»: ID einer
/// bestehenden Bearbeitung, deren Werte übernommen werden; «werkzeug_neu»: Werkzeug aus «Meine Werkzeuge»
/// (bekommt einen Platz); «Böden und Wände» kommt hinter sein 3D-Schruppen (im_wechsel_mit).
std::vector<std::string> HirchFraesen::Impl::merkmal_bearbeitungen(std::string const &json)
{
    std::vector<std::string> farben;
    bj::value v;
    try {
        v = bj::parse(json);
    } catch (...) {
        return farben;
    }
    auto liste = v.if_array();
    if (!liste || liste->empty()) return farben;
    int oeffnen = -1;
    auto bea = liste_von(einst, "bearbeitungen");
    if (!bea) {
        einst["bearbeitungen"] = bj::array();
        bea = liste_von(einst, "bearbeitungen");
    }
    // vor die erste Kontur (ausschneiden kommt zuletzt), sonst ans Ende
    std::size_t ziel = bea->size();
    for (std::size_t i = 0; i < bea->size(); ++i) {
        auto o = (*bea)[i].if_object();
        if (o && str(*o, "art") == "kontur") {
            ziel = i;
            break;
        }
    }
    auto platz_fuer = [this](bj::object &b, bj::object const &o) {
        if (auto wn = o.if_contains("werkzeug_neu"); wn && wn->is_object()) {
            auto ws = unter(einst, "werkzeuge");
            if (!ws) {
                einst["werkzeuge"] = bj::object();
                ws = unter(einst, "werkzeuge");
            }
            b["werkzeug"] = platz_fuer_werkzeug(*ws, wn->as_object());
        }
        b.erase("werkzeug_neu");
    };
    for (auto const &e : *liste) {
        auto o = e.if_object();
        auto art = o ? str(*o, "art") : std::string();
        if (o && !str(*o, "aendern").empty()) {          // bestehende Bearbeitung umstellen
            std::string farbe;
            for (auto &w : *bea) {
                auto q = w.if_object();
                if (!q || str(*q, "id") != str(*o, "aendern")) continue;
                for (auto const &kv : *o) {
                    std::string k = kv.key();
                    if (k == "art" || k == "aendern" || k == "werkzeug_neu") continue;
                    (*q)[k] = kv.value();
                }
                platz_fuer(*q, *o);
                farbe = str(*q, "farbe");
            }
            farben.push_back(farbe);
            continue;
        }
        if (art != "bohren" && art != "tasche" && art != "relief_schlichten" && art != "boeden_waende") {
            farben.emplace_back();
            continue;
        }
        auto b = vorgabe_bearbeitung(art);
        if (str(*o, "vorlage") == "letzte")            // wie die letzte Bearbeitung dieser Art (Seite 1)
            for (auto const &w : *bea)
                if (auto q = w.if_object(); q && str(*q, "art") == art && num(*q, "auf_seite", 1) == 1) b = *q;
        for (auto const &kv : *o) {
            std::string k = kv.key();
            if (k == "art" || k == "vorlage" || k == "oeffnen") continue;
            b[k] = kv.value();
        }
        b["id"] = neue_id();
        b["farbe"] = freier_ton(art);
        if (num(*o, "starttiefe", 0) <= 1e-6) b.erase("starttiefe");
        platz_fuer(b, *o);
        farben.push_back(str(b, "farbe"));
        std::size_t hier = ziel;
        if (art == "boeden_waende" && !str(b, "im_wechsel_mit").empty()) {
            for (std::size_t i = 0; i < bea->size(); ++i)
                if (auto q = (*bea)[i].if_object(); q && str(*q, "id") == str(b, "im_wechsel_mit")) hier = i + 1;
            if (hier <= ziel) ++ziel;
        }
        if (flag(*o, "oeffnen", false)) oeffnen = int(hier);
        bea->insert(bea->begin() + hier, b);
        if (hier == ziel) ++ziel;
    }
    schreiben(TR("Bearbeitungen anlegen"));
    if (oeffnen >= 0) Glib::signal_idle().connect_once([this, oeffnen] { bearbeitung_dialog(oeffnen); });
    return farben;
}

/// Werkzeug aus «Meine Werkzeuge» für das 3D-Fräsen: Schaftfräser (schruppen) – der grösste bis
/// Ø 12 mm mit Mittenschnitt, dessen Schneide die Tiefe schafft, sonst der längste; Kugelfräser
/// (schlichten) – Ø 6 mm oder der nächste, lieber einer, der tief genug reicht.
static std::optional<bj::object> relief_werkzeug(bj::array const &db, bool kugel, double tiefe)
{
    bj::object const *best = nullptr;
    auto wert = [&](bj::object const &w) {
        double d = num(w, "durchmesser", 0), sl = num(w, "schneidenlaenge", 0);
        bool reicht = sl + 1e-6 >= tiefe;
        if (kugel) return std::make_tuple(reicht ? 1 : 0, -std::abs(d - 6.0), -d, sl);
        return std::make_tuple(reicht ? 1 : 0, reicht ? d : sl, reicht ? sl : d, 0.0);
    };
    for (auto const &v : db) {
        auto o = v.if_object();
        if (!o) continue;
        std::string t = str(*o, "typ");
        double d = num(*o, "durchmesser", 0);
        if (kugel ? t != "kugel" : (t != "schaft" || !flag(*o, "mittenschnitt", true) || d > 12.0 + 1e-6 || d <= 0))
            continue;
        if (!best || wert(*o) > wert(*best)) best = o;
    }
    if (!best) return std::nullopt;
    bj::object w = *best;
    w.erase("notiz");
    w["db_id"] = str(*best, "id");
    w.erase("id");
    w["auto"] = true;
    return w;
}

void HirchFraesen::Impl::relief_anlegen(double tiefe, std::function<void(std::string const &)> fertig)
{
    if (auto bea = liste_von(einst, "bearbeitungen")) {
        for (auto const &v : *bea) {
            auto o = v.if_object();
            if (o && ist_relief(str(*o, "art"))) {              // schon eingerichtet: nichts anlegen
                if (fertig) fertig("");
                return;
            }
        }
    }
    python({"datenbank"}, [this, tiefe, fertig](bool ok, std::string const &aus, std::string const &) {
        bj::object r;
        bj::array db;
        if (ok && json_lesen(aus, r))
            if (auto l = liste_von(r, "werkzeuge")) db = *l;
        auto bea = liste_von(einst, "bearbeitungen");
        if (!bea) {
            einst["bearbeitungen"] = bj::array();
            bea = liste_von(einst, "bearbeitungen");
        }
        for (auto const &v : *bea) {
            auto o = v.if_object();
            if (o && ist_relief(str(*o, "art"))) {
                if (fertig) fertig("");
                return;
            }
        }
        auto ws = unter(einst, "werkzeuge");
        if (!ws) {
            einst["werkzeuge"] = bj::object();
            ws = unter(einst, "werkzeuge");
        }
        // gleiches Werkzeug schon in der Zeichnung: diesen Platz nehmen, sonst den ersten freien
        auto platz = [&](bj::object const &w) -> std::string {
            for (auto const &kv : *ws) {
                auto o = kv.value().if_object();
                if (!o) continue;
                bool gleich = !str(w, "db_id").empty() ? str(*o, "db_id") == str(w, "db_id")
                                                        : false;
                if (!gleich)
                    gleich = str(*o, "typ") == str(w, "typ") &&
                             std::abs(num(*o, "durchmesser", 0) - num(w, "durchmesser", 0)) < 1e-6 &&
                             std::abs(num(*o, "schneidenlaenge", 0) - num(w, "schneidenlaenge", 0)) < 1e-6;
                if (gleich) return std::string(kv.key());
            }
            for (char c = 'A'; c <= 'Z'; ++c) {
                if (!ws->if_contains(std::string(1, c))) {
                    (*ws)[std::string(1, c)] = w;
                    return std::string(1, c);
                }
            }
            return "C";
        };
        auto ersatz = [](bool kugel) {
            bj::object w = vorgabe_werkzeug(false);
            w["typ"] = kugel ? "kugel" : "schaft";
            w["durchmesser"] = 6.0;
            w["eckradius"] = kugel ? 3.0 : 0.0;
            w["schneidenlaenge"] = 22.0;
            w["schneiden"] = 2;
            w["mittenschnitt"] = true;
            w["auto"] = true;
            w["schnittdaten"] = "auto";
            w["name"] = "";
            return w;
        };
        std::vector<std::string> text;
        // vor die erste Kontur (ausschneiden kommt zuletzt), sonst ans Ende
        std::size_t ziel = bea->size();
        for (std::size_t i = 0; i < bea->size(); ++i) {
            auto o = (*bea)[i].if_object();
            if (o && str(*o, "art") == "kontur") {
                ziel = i;
                break;
            }
        }
        for (bool kugel : {false, true}) {
            auto gefunden = relief_werkzeug(db, kugel, tiefe);
            bj::object w = gefunden ? *gefunden : ersatz(kugel);
            std::string slot = platz(w);
            char const *art = kugel ? "relief_schlichten" : "relief_schruppen";
            auto b = vorgabe_bearbeitung(art);
            b["id"] = neue_id();
            b["farbe"] = freier_ton(art);
            b["werkzeug"] = slot;
            bea->insert(bea->begin() + ziel++, b);
            text.push_back(werkzeug_name(w) +
                           (gefunden ? "" : std::string(" ") + TR("(neu – nicht in «Meine Werkzeuge»)")));
        }
        schreiben(TR("3D-Bearbeitungen anlegen"));
        if (fertig)
            fertig(TRF("Angelegt: 3D-Schruppen mit {}, 3D-Schlichten mit {}. Werte prüfen, dann rechnen.",
                       {text[0], text[1]}));
    });
}

/// Werkzeug aus «Meine Werkzeuge» nach Art und Ø (der nächste; bei gleichem Ø die längste Schneide)
static std::optional<bj::object> db_werkzeug(bj::array const &db, std::string const &typ, double d)
{
    bj::object const *best = nullptr;
    auto wert = [&](bj::object const &w) {
        return std::make_tuple(-std::abs(num(w, "durchmesser", 0) - d), num(w, "schneidenlaenge", 0));
    };
    for (auto const &v : db) {
        auto o = v.if_object();
        if (!o || str(*o, "typ") != typ || num(*o, "durchmesser", 0) <= 0) continue;
        if (typ == "schaft" && !flag(*o, "mittenschnitt", true)) continue;
        if (!best || wert(*o) > wert(*best)) best = o;
    }
    if (!best || std::abs(num(*best, "durchmesser", 0) - d) > 0.51) return std::nullopt;
    bj::object w = *best;
    w.erase("notiz");
    w["db_id"] = str(*best, "id");
    w.erase("id");
    w["auto"] = true;
    return w;
}

/// Platz in der Zeichnung für ein Werkzeug: gleiches schon da → dieser Platz, sonst der erste freie
static std::string platz_fuer_werkzeug(bj::object &ws, bj::object const &w)
{
    for (auto const &kv : ws) {
        auto o = kv.value().if_object();
        if (!o) continue;
        bool gleich = !str(w, "db_id").empty() && str(*o, "db_id") == str(w, "db_id");
        if (!gleich)
            gleich = str(*o, "typ") == str(w, "typ") &&
                     std::abs(num(*o, "durchmesser", 0) - num(w, "durchmesser", 0)) < 1e-6 &&
                     std::abs(num(*o, "eckradius", 0) - num(w, "eckradius", 0)) < 1e-6;
        if (gleich) return std::string(kv.key());
    }
    for (char c = 'A'; c <= 'Z'; ++c) {
        if (!ws.if_contains(std::string(1, c))) {
            ws[std::string(1, c)] = w;
            return std::string(1, c);
        }
    }
    return "C";
}

/// «Restmaterial mit Ø … anlegen» (Befund nach dem 3D-Schruppen, Peter 25.10.): ein zweites 3D-Schruppen mit
/// einem kleineren Schaftfräser aus «Meine Werkzeuge», «nur Restmaterial», direkt dahinter (hinter seinem
/// «Böden und Wände»).
void HirchFraesen::Impl::restmaterial_anlegen(std::string const &bid, double durchmesser)
{
    python({"datenbank"}, [this, bid, durchmesser](bool ok, std::string const &aus, std::string const &) {
        bj::object r;
        bj::array db;
        if (ok && json_lesen(aus, r))
            if (auto l = liste_von(r, "werkzeuge")) db = *l;
        auto bea = liste_von(einst, "bearbeitungen");
        if (!bea) return;
        int i0 = -1;
        for (int i = 0; i < (int)bea->size(); ++i)
            if (auto o = (*bea)[i].if_object(); o && str(*o, "id") == bid) i0 = i;
        if (i0 < 0) return;
        bj::object b = (*bea)[i0].as_object();
        auto gefunden = db_werkzeug(db, "schaft", durchmesser);
        bj::object w;
        if (gefunden) {
            w = *gefunden;
        } else {
            w = vorgabe_werkzeug(false);
            w["durchmesser"] = durchmesser;
            w["schneidenlaenge"] = std::max(6.0, 4 * durchmesser);
            w["schneiden"] = 2;
            w["mittenschnitt"] = true;
            w["auto"] = true;
            w["schnittdaten"] = "auto";
            w["name"] = "";
        }
        auto ws = unter(einst, "werkzeuge");
        if (!ws) {
            einst["werkzeuge"] = bj::object();
            ws = unter(einst, "werkzeuge");
        }
        b["id"] = neue_id();
        b["farbe"] = freier_ton("relief_schruppen");
        b["werkzeug"] = platz_fuer_werkzeug(*ws, w);
        b["rest"] = true;
        b["adaptiv"] = false;
        b["titel"] = TRF("Restmaterial Ø {}", {zahl(durchmesser)});
        int ziel = i0 + 1;
        while (ziel < (int)bea->size()) {
            auto o = (*bea)[ziel].if_object();
            if (!o || str(*o, "im_wechsel_mit") != bid) break;
            ++ziel;
        }
        bea->insert(bea->begin() + ziel, b);
        schreiben(TR("Restmaterial anlegen"));
        if (!gefunden)
            Glib::signal_idle().connect_once([this, durchmesser] {
                meldung(TR("Restmaterial"),
                        TRF("Kein Schaftfräser Ø {} in «Meine Werkzeuge» – ein neuer ist eingetragen. Masse und "
                            "Schnittdaten bitte prüfen.", {zahl(durchmesser)}), Gtk::MESSAGE_INFO);
            });
    });
}

void HirchFraesen::Impl::relief_zweiseitig(bool an, std::string const &achse)
{
    auto d = doc();
    if (!d) return;
    bj::object zw;
    if (auto z = unter(einst, "zweiseitig")) zw = *z;
    bool war = flag(zw, "an", false);
    if (!an && !war) return;
    zw["an"] = an;
    if (!achse.empty()) zw["achse"] = achse;
    einst["zweiseitig"] = zw;
    if (an) {
        seite2_anlegen(d);
        auto bea = liste_von(einst, "bearbeitungen");
        if (!bea) {
            einst["bearbeitungen"] = bj::array();
            bea = liste_von(einst, "bearbeitungen");
        }
        // 3D-Bearbeitungen von Seite 1 auch für Seite 2; gibt es noch keine: Schruppen und
        // Schlichten für beide Seiten
        std::vector<bj::object> seite1;
        std::set<std::string> auf2;
        for (auto const &v : *bea) {
            auto o = v.if_object();
            if (!o || !ist_relief(str(*o, "art"))) continue;
            if (num(*o, "auf_seite", 1) == 2) auf2.insert(str(*o, "art"));
            else seite1.push_back(*o);
        }
        if (seite1.empty()) {
            for (char const *a : {"relief_schruppen", "relief_schlichten"}) {
                auto b = vorgabe_bearbeitung(a);
                b["id"] = neue_id();
                b["farbe"] = freier_ton(a);
                bea->push_back(b);
                seite1.push_back(b);
            }
        }
        for (auto const &o : seite1) {
            if (auf2.count(str(o, "art"))) continue;
            bj::object b = o;
            b["id"] = neue_id();
            b["auf_seite"] = 2;
            bea->push_back(b);
            auf2.insert(str(o, "art"));
        }
    }
    schreiben(TR("Zweiseitig fräsen"));
    if (an && !war) Glib::signal_idle().connect_once([this] { anzeigen(); });
}


void hirch_3d_masse(std::string const &pfad,
                    std::function<void(bool, double, double, double, bool, std::string const &)> fertig)
{
    auto ordner = neuer_ordner();
    // ohne Material: nur lesen und die Masse in der gespeicherten Ausrichtung
    kern({"einbetten", pfad, "--ordner", ordner}, ordner,
         [fertig, ordner](bool ok, bj::object const &e, std::string const &f) {
             ordner_loeschen(ordner);
             if (!ok || !flag(e, "ok", false)) {
                 fertig(false, 0, 0, 0, false, warnungen(e).empty() ? f : warnungen(e));
                 return;
             }
             fertig(true, liste_zahl(e, "masse", 0), liste_zahl(e, "masse", 1), liste_zahl(e, "masse", 2),
                    str(e, "art") == "bild", "");
         });
}

// ---------------------------------------------------------------------------
// Nullpunkt am Block (Anfangsdialog)

Gtk::Widget *nullpunkt_block(std::shared_ptr<NullpunktWahl> wahl)
{
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
    auto bild = Gtk::make_managed<Gtk::DrawingArea>();
    bild->set_size_request(330, 240);
    bild->add_events(Gdk::BUTTON_PRESS_MASK);
    box->add(*bild);
    auto zeile = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    auto text = label("");
    text->set_xalign(0);
    text->set_hexpand(true);
    text->set_line_wrap(true);
    zeile->add(*text);
    auto knoepfe = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
    knoepfe->get_style_context()->add_class("linked");
    auto oben = Gtk::make_managed<Gtk::ToggleButton>(TR("Oberfläche"));
    auto unten = Gtk::make_managed<Gtk::ToggleButton>(TR("Werktisch"));
    oben->set_tooltip_text(TR("Z0 wird auf der Oberseite des Materials angetastet – Fräsen geht ins Minus"));
    unten->set_tooltip_text(TR("Z0 liegt auf dem Werktisch – Fräsen bleibt im Plus (die Materialdicke muss genau "
                               "stimmen)"));
    knoepfe->add(*oben);
    knoepfe->add(*unten);
    zeile->add(*knoepfe);
    box->add(*zeile);

    // Lage der Punkte: Schrägansicht von vorne links; die Dicke überhöht, damit oben und unten
    // auseinanderliegen
    struct Abb {
        double k = 1, ox = 0, oy = 0, zf = 1;
        Geom::Point p(double X, double Y, double Z) const
        {
            double const c = 0.866, s = 0.5;
            return {ox + ((X - Y) * c) * k, oy + (-(X + Y) * s - Z * zf) * k};
        }
    };
    auto abb = [wahl, bild] {
        Abb a;
        double W = std::max(1.0, wahl->b), H = std::max(1.0, wahl->h), D = std::max(0.1, wahl->d);
        a.zf = std::clamp(0.15 * (W + H) / 2 / D, 1.0, 6.0);
        Abb e = a;
        e.k = 1;
        std::vector<Geom::Point> q = {e.p(0, 0, 0), e.p(W, 0, 0), e.p(0, H, 0), e.p(0, 0, D), e.p(W, H, D),
                                      e.p(W, 0, D), e.p(0, H, D)};
        double x0 = 1e18, x1 = -1e18, y0 = 1e18, y1 = -1e18;
        for (auto &pt : q) {
            x0 = std::min(x0, pt.x());
            x1 = std::max(x1, pt.x());
            y0 = std::min(y0, pt.y());
            y1 = std::max(y1, pt.y());
        }
        double w = bild->get_allocated_width(), hh = bild->get_allocated_height();
        a.k = std::min((w - 70) / std::max(1e-9, x1 - x0), (hh - 50) / std::max(1e-9, y1 - y0));
        a.ox = w / 2 - a.k * (x0 + x1) / 2;
        a.oy = hh / 2 - a.k * (y0 + y1) / 2 + 6;
        return a;
    };
    auto anteil = [](char c) { return c == 'm' ? 0.5 : (c == 'o' || c == 'r') ? 1.0 : 0.0; };
    // sichtbare Punkte unten: vordere und linke Kante
    auto sichtbar_unten = [](std::string const &xy) {
        return xy[0] == 'u' || xy[1] == 'l';
    };
    auto setzen = [wahl, text, oben, unten, bild, sichtbar_unten](bool laut) {
        static bool sperre = false;
        if (sperre) return;
        sperre = true;
        oben->set_active(!wahl->unten);
        unten->set_active(wahl->unten);
        sperre = false;
        char const *ny[] = {"vorne", "Mitte", "hinten"}, *nx[] = {"links", "Mitte", "rechts"};
        int j = wahl->xy[0] == 'u' ? 0 : wahl->xy[0] == 'm' ? 1 : 2;
        int i = wahl->xy[1] == 'l' ? 0 : wahl->xy[1] == 'm' ? 1 : 2;
        std::string ort = (i == 1 && j == 1) ? std::string(TR("Mitte"))
                          : i == 1         ? std::string(TR(ny[j])) + " " + TR("Mitte")
                          : j == 1         ? std::string(TR(nx[i])) + " " + TR("Mitte")
                                           : std::string(TR(ny[j])) + " " + TR(nx[i]);
        if (wahl->rund) ort = TR("Mitte (Achse)");
        std::string t = TRF("X0 Y0: {} · Z0: {}", {ort, wahl->unten ? TR("Werktisch") : TR("Materialoberfläche")});
        if (wahl->unten && !wahl->rund && !sichtbar_unten(wahl->xy)) t += " " + std::string(TR("(Punkt unten verdeckt)"));
        text->set_text(t);
        bild->queue_draw();
        (void)laut;
    };
    wahl->neu_zeichnen = [setzen] { setzen(false); };
    oben->signal_toggled().connect([wahl, oben, setzen] {
        if (oben->get_active() == !wahl->unten) return;
        wahl->unten = !oben->get_active();
        setzen(true);
    });
    unten->signal_toggled().connect([wahl, unten, setzen] {
        if (unten->get_active() == wahl->unten) return;
        wahl->unten = unten->get_active();
        setzen(true);
    });

    bild->signal_draw().connect([=](Cairo::RefPtr<Cairo::Context> const &cr) {
        Abb a = abb();
        double W = std::max(1.0, wahl->b), H = std::max(1.0, wahl->h), D = std::max(0.1, wahl->d);
        auto flaeche = [&](std::vector<std::array<double, 3>> pts, double r, double g, double b) {
            for (std::size_t n = 0; n < pts.size(); ++n) {
                auto q = a.p(pts[n][0], pts[n][1], pts[n][2]);
                if (n == 0) cr->move_to(q.x(), q.y());
                else cr->line_to(q.x(), q.y());
            }
            cr->close_path();
            cr->set_source_rgb(r, g, b);
            cr->fill_preserve();
            cr->set_source_rgb(0.54, 0.46, 0.31);
            cr->set_line_width(1);
            cr->stroke();
        };
        // Werktisch
        auto t0 = a.p(-W * 0.12, 0, 0), t1 = a.p(W * 1.12, 0, 0);
        cr->set_source_rgb(0.6, 0.6, 0.59);
        std::vector<double> strich = {4, 3};
        cr->set_dash(strich, 0);
        cr->move_to(t0.x(), t0.y());
        cr->line_to(t1.x(), t1.y());
        cr->stroke();
        cr->unset_dash();
        cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_NORMAL);
        cr->set_font_size(10);
        cr->move_to(t1.x() + 4, t1.y() + 4);
        cr->set_source_rgb(0.47, 0.46, 0.48);
        cr->show_text(einheit_text_zahlen(TR("Werktisch")));
        if (wahl->rund) {
            // Zylinder stehend: Mantel von hinten nach vorne, dann der Deckel
            double R = W / 2, cx = W / 2, cy = H / 2;
            int const N = 72;
            std::vector<std::pair<double, int>> folge;
            for (int n = 0; n < N; ++n) {
                double t = 2 * M_PI * (n + 0.5) / N;
                folge.push_back({-(std::cos(t) + std::sin(t)), n});      // hinten (X+Y gross) zuerst
            }
            std::sort(folge.begin(), folge.end());
            for (auto const &[_t, n] : folge) {
                double t0 = 2 * M_PI * n / N, t1 = 2 * M_PI * (n + 1) / N;
                auto q = [&](double t, double z) { return a.p(cx + R * std::cos(t), cy + R * std::sin(t), z); };
                auto p0 = q(t0, 0), p1 = q(t1, 0), p2 = q(t1, D), p3 = q(t0, D);
                cr->move_to(p0.x(), p0.y());
                cr->line_to(p1.x(), p1.y());
                cr->line_to(p2.x(), p2.y());
                cr->line_to(p3.x(), p3.y());
                cr->close_path();
                double hell = 0.62 + 0.12 * std::cos(t0 - 3.9);
                cr->set_source_rgb(hell, hell * 0.89, hell * 0.67);
                cr->fill_preserve();
                cr->set_line_width(0.6);
                cr->stroke();
            }
            std::vector<std::array<double, 3>> deckel;
            for (int n = 0; n < N; ++n)
                deckel.push_back({cx + R * std::cos(2 * M_PI * n / N), cy + R * std::sin(2 * M_PI * n / N), D});
            flaeche(deckel, 0.86, 0.80, 0.64);
        } else {
            flaeche({{0, 0, 0}, {0, H, 0}, {0, H, D}, {0, 0, D}}, 0.76, 0.68, 0.52);
            flaeche({{0, 0, 0}, {W, 0, 0}, {W, 0, D}, {0, 0, D}}, 0.69, 0.61, 0.43);
            flaeche({{0, 0, D}, {W, 0, D}, {W, H, D}, {0, H, D}}, 0.86, 0.80, 0.64);
        }
        // Modell (Rechteck auf der Oberfläche)
        if (wahl->modell[2] > 0 && wahl->modell[3] > 0) {
            double mx = wahl->modell[0], my = wahl->modell[1], mw = wahl->modell[2], mh = wahl->modell[3];
            std::array<Geom::Point, 4> q = {a.p(mx, my, D), a.p(mx + mw, my, D), a.p(mx + mw, my + mh, D),
                                            a.p(mx, my + mh, D)};
            cr->move_to(q[0].x(), q[0].y());
            for (int n = 1; n < 4; ++n) cr->line_to(q[n].x(), q[n].y());
            cr->close_path();
            cr->set_source_rgba(0.45, 0.45, 0.44, 0.55);
            cr->fill_preserve();
            cr->set_source_rgb(0.37, 0.36, 0.39);
            cr->set_line_width(0.8);
            cr->stroke();
        }
        // Punkte
        for (char ky : {'u', 'm', 'o'}) {
            for (char kx : {'l', 'm', 'r'}) {
                std::string id{ky, kx};
                if (wahl->rund && id != "mm") continue;
                for (bool u : {false, true}) {
                    bool sicht = !u || (sichtbar_unten(id) && !wahl->rund);
                    bool an = wahl->xy == id && wahl->unten == u;
                    if (!sicht && !an && !wahl->rund) continue;     // rund: unten Mitte gestrichelt
                    auto q = a.p(anteil(kx) * W, anteil(ky) * H, u ? 0 : D);
                    cr->arc(q.x(), q.y(), 5, 0, 2 * M_PI);
                    if (an) cr->set_source_rgb(0.10, 0.37, 0.71);
                    else if (u) cr->set_source_rgb(0.93, 0.91, 0.85);
                    else cr->set_source_rgb(1, 1, 1);
                    cr->fill_preserve();
                    if (!sicht) cr->set_dash(strich, 0);
                    if (u) cr->set_source_rgb(0.42, 0.36, 0.23);
                    else cr->set_source_rgb(0.14, 0.12, 0.19);
                    cr->set_line_width(1.5);
                    cr->stroke();
                    cr->unset_dash();
                }
            }
        }
        // Nullpunkt mit Versatz und Achsen
        double X0 = anteil(wahl->xy[1]) * W + wahl->dx, Y0 = anteil(wahl->xy[0]) * H + wahl->dy;
        auto nz = a.p(X0, Y0, wahl->unten ? 0 : D);
        double L = 36;
        auto achse = [&](double ddx, double ddy, double r, double g, double b, char const *t) {
            cr->set_source_rgb(r, g, b);
            cr->set_line_width(2.2);
            cr->move_to(nz.x(), nz.y());
            cr->line_to(nz.x() + ddx, nz.y() + ddy);
            cr->stroke();
            cr->move_to(nz.x() + ddx * 1.12 - 4, nz.y() + ddy * 1.12 + 4);
            cr->show_text(einheit_text_zahlen(t));
        };
        cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_BOLD);
        cr->set_font_size(11);
        achse(0.866 * L, -0.5 * L, 0.88, 0.11, 0.14, "X");
        achse(-0.866 * L, -0.5 * L, 0.18, 0.76, 0.49, "Y");
        achse(0, -L, 0.21, 0.52, 0.89, "Z");
        cr->arc(nz.x(), nz.y(), 6, 0, 2 * M_PI);
        cr->set_source_rgb(0.10, 0.37, 0.71);
        cr->fill_preserve();
        cr->set_source_rgb(1, 1, 1);
        cr->set_line_width(2);
        cr->stroke();
        return true;
    });
    bild->signal_button_press_event().connect([=](GdkEventButton *ev) {
        Abb a = abb();
        double W = std::max(1.0, wahl->b), H = std::max(1.0, wahl->h), D = std::max(0.1, wahl->d);
        double best = 18;
        std::string neu;
        bool neu_u = false;
        for (char ky : {'u', 'm', 'o'}) {
            for (char kx : {'l', 'm', 'r'}) {
                std::string id{ky, kx};
                if (wahl->rund && id != "mm") continue;
                for (bool u : {false, true}) {
                    if (u && !sichtbar_unten(id) && !wahl->rund) continue;
                    auto q = a.p(anteil(kx) * W, anteil(ky) * H, u ? 0 : D);
                    double dd = std::hypot(ev->x - q.x(), ev->y - q.y());
                    if (dd < best) {
                        best = dd;
                        neu = id;
                        neu_u = u;
                    }
                }
            }
        }
        if (neu.empty()) return true;
        wahl->xy = neu;
        wahl->unten = neu_u;
        setzen(true);
        return true;
    });
    setzen(false);
    return box;
}


} // namespace Inkscape::UI::Dialog
