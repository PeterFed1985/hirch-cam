// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Bearbeitungsdialog (alle Fräsarten), Vorschub, Live-Vorschau
 */
#include "ui/dialog/hirch-intern.h"
#include "ui/dialog/hirch-3d.h"

namespace Inkscape::UI::Dialog {

// in hirch-panel.cpp (ohne Änderung an hirch-intern.h)
void hirch_werkzeug_pflicht(Gtk::Dialog &dlg, Gtk::ComboBoxText &c);

bj::object const *HirchFraesen::Impl::ablauf_op(std::string const &key)
{
    if (!ergebnis_aktuell) return nullptr;
    auto a = unter(ergebnis, "ablauf");
    if (!a) return nullptr;
    if (auto seiten = liste_von(*a, "seiten")) {
        for (auto const &sv : *seiten) {
            auto s = sv.if_object();
            auto abs = s ? liste_von(*s, "abschnitte") : nullptr;
            if (!abs) continue;
            for (auto const &av : *abs) {
                auto ab = av.if_object();
                auto ops = ab ? liste_von(*ab, "ops") : nullptr;
                if (!ops) continue;
                for (auto const &ov : *ops) {
                    if (auto o = ov.if_object(); o && str(*o, "key") == key) return o;
                }
            }
        }
    }
    return nullptr;
}

auto HirchFraesen::Impl::vorschub_zeile(Gtk::Grid *g, int r, std::string const &text, double anfang,
                                                 double alt_prozent, std::function<double()> standard) -> std::shared_ptr<VorschubFeld>
{
    auto v = std::make_shared<VorschubFeld>();
    v->standard = standard;
    double st = standard();
    double wert = st;
    if (anfang > 0) {
        wert = anfang;
        v->folgt = false;
    } else if (alt_prozent > 0 && std::abs(alt_prozent - 100) > 1e-9) {
        wert = st * alt_prozent / 100.0;
        v->folgt = false;
    }
    auto adj = Gtk::Adjustment::create(std::round(wert), 1, 60000, 10, 100);
    v->spin = Gtk::make_managed<Gtk::SpinButton>(adj, 10, 0);
    v->spin->set_numeric(true);
    punkt_und_komma(v->spin);
    v->spin->set_width_chars(7);
    v->spin->set_activates_default(true);
    auto l = label(text);
    l->set_size_request(200, -1);
    g->attach(*l, 0, r);
    v->spin->set_halign(Gtk::ALIGN_START);
    g->attach(*v->spin, 1, r);
    v->hinweis = label("", "hirch-dim");
    klasse(*v->hinweis, "hirch-klein");
    v->hinweis->set_hexpand(true);
    g->attach(*v->hinweis, 2, r);
    auto w = std::weak_ptr<VorschubFeld>(v);
    v->spin->signal_value_changed().connect([w] {
        auto p = w.lock();
        if (!p || p->setzt) return;
        p->folgt = std::abs(p->spin->get_value() - p->standard()) < 0.5;
        if (p->hinweis) p->hinweis->set_text(p->folgt ? TR("mm/min · wie das Werkzeug")
                                                      : TR("mm/min · Werkzeug: ") + (hirch_zoll() ? einheit_zahl(p->standard(), 0, Groesse::VORSCHUB, false) : ganz(p->standard())));
    });
    v->nachfuehren();
    return v;
}

/// Art der Zustellungen: drei Zeilen ab r im Grid (Auswahl, letzte Zustellung, eigene Liste)
static std::shared_ptr<ZustellArt> zustell_art_zeilen(Gtk::Grid &g, int r, bj::object const &b, Gtk::SpinButton *zust,
                                                      bool vcarve)
{
    auto Z = std::make_shared<ZustellArt>();
    Z->zust = zust;
    if (zust) {
        int top = 0;
        gtk_container_child_get(GTK_CONTAINER(g.gobj()), GTK_WIDGET(zust->gobj()), "top-attach", &top, nullptr);
        Z->zust_text = dynamic_cast<Gtk::Label *>(g.get_child_at(0, top));
        if (Z->zust_text) Z->zust_text_normal = Z->zust_text->get_text().raw();
    }
    Z->modus = Gtk::make_managed<Gtk::ComboBoxText>();
    Z->modus->append("", TR("gleichmässig"));
    Z->modus->append("kleiner", TR("letzte Zustellung kleiner"));
    Z->modus->append("liste", TR("eigene Liste"));
    Z->modus->append("eins", TR("in einem Durchgang"));
    auto m = str(b, "zustell_modus", "");
    if (vcarve && m.empty() && num(b, "zustellung", 0) <= 1e-9) m = "eins";   // früher: Zustellung 0
    Z->modus->set_active_id(m);
    if (Z->modus->get_active_row_number() < 0) Z->modus->set_active(0);
    Z->modus->set_tooltip_text(TR("Gleichmässig: die Tiefe in gleich grosse Zustellungen geteilt. Letzte kleiner: "
                                  "zum Schluss eine dünne Zustellung für einen sauberen Boden. Eigene Liste: jede "
                                  "Zustellung selbst festlegen. In einem Durchgang: gleich auf die volle Tiefe."));
    auto l = label(TR("Aufteilung"));
    l->set_hexpand(true);
    g.attach(*l, 0, r);
    Z->modus->set_halign(Gtk::ALIGN_START);
    g.attach(*Z->modus, 1, r, 2);
    Z->letzte = spin_zeile(g, r + 1, TR("Letzte Zustellung"), num(b, "letzte_zustellung", 0.2), 0.01, 50, 0.05, 2,
                           TR("mm – dünn für einen sauberen Boden"));
    auto ll = label(TR("Zustellungen"));
    g.attach(*ll, 0, r + 2);
    Z->liste = Gtk::make_managed<Gtk::Entry>();
    std::vector<double> alt;
    if (auto a = b.if_contains("zustell_liste"); a && a->is_array()) {
        for (auto const &v : a->as_array())
            if (v.is_number()) alt.push_back(v.to_number<double>());
    }
    Z->liste->set_text(zustell_liste_text(alt));
    Z->liste->set_placeholder_text(TR("z. B. 1,0; 0,8; 0,5; 0,3"));
    Z->liste->set_width_chars(18);
    Z->liste->set_activates_default(true);
    Z->liste->set_halign(Gtk::ALIGN_START);
    g.attach(*Z->liste, 1, r + 2);
    g.attach(*label(TR("mm, von oben nach unten – die letzte wiederholt sich"), "hirch-dim"), 2, r + 2);
    return Z;
}

/// Vorschau der Fräsbahnen zum Aufklappen (Peter, 20.10.: nicht fest im Dialog);
/// beim ersten Aufklappen wird gerechnet, falls nötig
static Gtk::Box *vorschau_klappe(Gtk::Box *ziel, std::function<void()> beim_oeffnen)
{
    auto ex = Gtk::make_managed<Gtk::Expander>();
    auto t = label(TR("VORSCHAU DER FRÄSBAHNEN"), "hirch-kopf");
    ex->set_label_widget(*t);
    auto innen = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
    innen->set_margin_top(6);
    ex->add(*innen);
    auto prefs = Inkscape::Preferences::get();
    ex->set_expanded(prefs->getBool("/hirch/bearbeitung/vorschau_offen", false));
    auto offen = std::make_shared<bool>(false);
    auto pruefen = [ex, offen, beim_oeffnen] {
        if (ex->get_expanded() && !*offen) {
            *offen = true;
            if (beim_oeffnen) beim_oeffnen();
        }
    };
    ex->property_expanded().signal_changed().connect([ex, pruefen] {
        Inkscape::Preferences::get()->setBool("/hirch/bearbeitung/vorschau_offen", ex->get_expanded());
        pruefen();
    });
    ziel->add(*ex);
    ex->signal_map().connect(pruefen);   // offen gespeichert: beim Zeigen rechnen (endet mit dem Widget)
    return innen;
}

void HirchFraesen::Impl::live_planen(std::shared_ptr<LiveVorschau> L)
{
    if (!L || !L->lebt || !L->auslesen) return;
    L->timer.disconnect();
    L->timer = Glib::signal_timeout().connect([this, L] {
        live_rechnen(L, false);
        return false;
    }, 700);
}

void HirchFraesen::Impl::live_rechnen(std::shared_ptr<LiveVorschau> L, bool erzwingen)
{
    if (!L->lebt || !L->auslesen) return;
    auto bea = liste_von(einst, "bearbeitungen");
    if (!bea || L->index < 0 || L->index >= (int)bea->size()) return;
    auto alt = (*bea)[L->index].if_object();
    if (!alt) return;
    bj::object b = *alt;
    L->auslesen(b);
    bj::object e = einst;
    e["bearbeitungen"].as_array()[L->index] = b;
    auto text = bj::serialize(e);
    if (L->letzte.empty()) {
        // erster Aufruf: Werte wie beim Öffnen – die letzte Rechnung passt schon
        L->letzte = text;
        if (!erzwingen && ergebnis_aktuell && !L->daten->empty()) return;
    }
    if (!erzwingen && text == L->letzte) return;
    L->letzte = text;
    auto svg = Glib::build_filename(ordner, "zeichnung.svg");
    auto datei = Glib::build_filename(ordner, "vorschau_einst.json");
    try {
        if (!Glib::file_test(svg, Glib::FILE_TEST_EXISTS)) return;
        Glib::file_set_contents(datei, text);
    } catch (Glib::Error const &) {
        return;
    }
    unsigned g = ++L->gen;
    L->rechnet = true;
    if (L->hinweis) L->hinweis->set_text(TR("Vorschau wird gerechnet …"));
    python({"vorschau", svg, "--einst", datei, "--bearbeitung", L->id, "--ordner",
            Glib::build_filename(ordner, "vorschau"), "--platte", std::to_string(platte)},
           [L, g](bool ok, std::string const &o, std::string const &) {
               if (!L->lebt || g != L->gen) return;
               L->rechnet = false;
               bj::object r;
               if (!ok || !json_lesen(o, r) || !flag(r, "ok", false)) {
                   if (L->hinweis) L->hinweis->set_text(TR("Vorschau konnte nicht gerechnet werden – "
                                                        "«Vorschau aktualisieren» versucht es neu."));
                   return;
               }
               L->daten->clear();
               if (auto bahnen = unter(r, "bahnen")) {
                   for (auto const &kv : *bahnen) {
                       std::string k = kv.key();
                       if (auto ob = kv.value().if_object())
                           (*L->daten)[k == L->id ? "haupt" : k.substr(L->id.size() + 1)] = *ob;
                   }
               }
               if (L->bild) L->bild->queue_draw();
               if (L->hinweis) {
                   L->hinweis->set_text(L->daten->empty()
                                            ? TR("Mit diesen Werten entstehen keine Fräsbahnen.")
                                            : TR("Vorschau mit den Werten in diesem Fenster (noch nicht übernommen). "
                                              "Dunkel: was der Fräser wegnimmt."));
               }
           });
}

auto HirchFraesen::Impl::live_vorschau(Gtk::Box *ziel, std::string const &id, int index,
                                               std::function<std::string()> wahl, std::string const &haupt_hex,
                                               std::string const &start_text) -> std::shared_ptr<LiveVorschau>
{
    auto L = std::make_shared<LiveVorschau>();
    L->id = id;
    L->index = index;
    L->bild = bahnen_bild(id, wahl, nullptr, haupt_hex, &L->daten);
    ziel->add(*L->bild);
    auto zeile = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    L->hinweis = hinweis_label(start_text);
    L->hinweis->set_hexpand(true);
    zeile->add(*L->hinweis);
    L->knopf = Gtk::make_managed<Gtk::Button>(TR("Genau rechnen"));
    L->knopf->set_valign(Gtk::ALIGN_START);
    L->knopf->set_tooltip_text(TR("Diese Bearbeitung mit den Werten im Fenster genau rechnen und die Fräsbahnen zeigen."));
    L->knopf->signal_clicked().connect([this, L] { live_rechnen(L, true); });
    zeile->add(*L->knopf);
    ziel->add(*zeile);
    return L;
}

Gtk::DrawingArea *HirchFraesen::Impl::bahnen_bild(std::string const &id, std::function<std::string()> wahl, bool *leer,
                                  std::string const &haupt_hex, std::shared_ptr<BahnDaten> *daten_aus)
{
    double hr = 0.96, hg = 0.38, hb = 0.32;        // Hauptbahn: rot, sonst Farbe der Bearbeitung
    if (!haupt_hex.empty()) hex_rgb(haupt_hex, hr, hg, hb);
    auto bild = Gtk::make_managed<Gtk::DrawingArea>();
    bild->set_size_request(340, 250);
    auto bahnen = ergebnis_aktuell ? unter(ergebnis, "bahnen") : nullptr;
    auto daten = std::make_shared<BahnDaten>();
    if (bahnen) {
        for (auto const &kv : *bahnen) {
            std::string k = kv.key();
            if (k != id && k.rfind(id + "#", 0) != 0) continue;
            if (auto o = kv.value().if_object()) (*daten)[k == id ? "haupt" : k.substr(id.size() + 1)] = *o;
        }
    }
    if (leer) *leer = daten->empty();
    if (daten_aus) *daten_aus = daten;
    bild->signal_draw().connect([bild, wahl, daten, hr, hg, hb](Cr const &cr) {
        auto const &meine = *daten;
        double w = bild->get_allocated_width(), h = bild->get_allocated_height();
        auto fg = bild->get_style_context()->get_color(Gtk::STATE_FLAG_NORMAL);
        rundrechteck(cr, 0.5, 0.5, w - 1, h - 1, 8);
        cr->set_source_rgba(0, 0, 0, 0.25);
        cr->fill();
        if (meine.empty()) {
            schrift(cr, fg, 16, h / 2, TR("Noch nicht gerechnet – die Vorschau"), 0.7);
            schrift(cr, fg, 16, h / 2 + 16, TR("erscheint nach «Aktualisieren»."), 0.7);
            return true;
        }
        double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
        for (auto const &[k, o] : meine) {
            double r = num(o, "durchmesser", 1) / 2;
            if (auto zs = liste_von(o, "zuege")) {
                for (auto const &zv : *zs) {
                    auto z = zv.if_array();
                    if (!z) continue;
                    for (std::size_t i = 0; i + 1 < z->size(); i += 2) {
                        double x = (*z)[i].to_number<double>(), y = (*z)[i + 1].to_number<double>();
                        x0 = std::min(x0, x - r); x1 = std::max(x1, x + r);
                        y0 = std::min(y0, y - r); y1 = std::max(y1, y + r);
                    }
                }
            }
        }
        if (x1 <= x0 || y1 <= y0) return true;
        double rand = 14, s = std::min((w - 2 * rand) / (x1 - x0), (h - 2 * rand) / (y1 - y0));
        double ox = (w - (x1 - x0) * s) / 2, oy = (h - (y1 - y0) * s) / 2;
        // Holz hinter dem Umriss der Bahnen
        farbe(cr, HOLZ, 0.85);
        cr->rectangle(ox - 6, oy - 6, (x1 - x0) * s + 12, (y1 - y0) * s + 12);
        cr->fill();
        struct F { double r, g, b; };
        std::map<std::string, F> farben = {{"raeumen", {0.38, 0.63, 0.92}}, {"kernloch", {0.57, 0.25, 0.67}},
                                           {"haupt", {hr, hg, hb}}, {"flach", {0.90, 0.60, 0.10}},
                                           {"schlicht", {0.30, 0.75, 0.45}}};
        auto gewaehlt = wahl();
        for (std::string k : {"raeumen", "kernloch", "haupt", "flach", "schlicht"}) {
            auto it = meine.find(k);
            if (it == meine.end()) continue;
            bool an = gewaehlt.empty() || gewaehlt == "form" || gewaehlt == k;
            auto c = farben[k];
            double d = num(it->second, "durchmesser", 1) * s;
            cr->set_line_cap(Cairo::LINE_CAP_ROUND);
            cr->set_line_join(Cairo::LINE_JOIN_ROUND);
            if (auto zs = liste_von(it->second, "zuege")) {
                // schmaler Schnitt (Gravur): nur die Schnittbreite im Massstab, in der Farbe des Teils –
                // eine Mittellinie wäre breiter als der Schnitt selbst (Peter, 24.10.2026)
                bool schmal = d < 4;
                for (int durchgang = schmal ? 1 : 0; durchgang < 2; ++durchgang) {
                    // erst die Schnittbreite (was der Fräser wegnimmt), dann die Mittellinie
                    cr->set_source_rgba(durchgang ? c.r : 0.12, durchgang ? c.g : 0.12, durchgang ? c.b : 0.12,
                                        durchgang ? (an ? 0.95 : 0.35) : (an ? 0.55 : 0.30));
                    cr->set_line_width(schmal ? std::max(0.6, d) : durchgang ? (an ? 1.4 : 0.8) : std::max(1.0, d));
                    for (auto const &zv : *zs) {
                        auto z = zv.if_array();
                        if (!z) continue;
                        if (z->size() == 2) {          // Bohrung: Punkt in Werkzeuggrösse
                            double x = ox + ((*z)[0].to_number<double>() - x0) * s;
                            double y = oy + (y1 - (*z)[1].to_number<double>()) * s;
                            cr->arc(x, y, std::max(1.5, durchgang ? 1.5 : d / 2), 0, 2 * M_PI);
                            cr->fill();
                            continue;
                        }
                        for (std::size_t i = 0; i + 1 < z->size(); i += 2) {
                            double x = ox + ((*z)[i].to_number<double>() - x0) * s;
                            double y = oy + (y1 - (*z)[i + 1].to_number<double>()) * s;
                            if (i == 0) cr->move_to(x, y); else cr->line_to(x, y);
                        }
                        cr->stroke();
                    }
                }
            }
        }
        return true;
    });
    return bild;
}

std::vector<std::array<std::string, 3>> HirchFraesen::Impl::aenderungen_liste(bj::object const &start_o,
                                                                              bj::object const &jetzt)
{
    // «Geändert seit dem Öffnen»: Name, alter Wert durchgestrichen → neuer Wert
    auto const *start = &start_o;
    static std::vector<std::pair<char const *, char const *>> const NAMEN = {
        {"werkzeug", "Werkzeug"}, {"tiefe", "Tiefe"}, {"tiefe_ab", "Wie tief"}, {"tiefe_ab_mm", "Abstand zum Modell"}, {"starttiefe", "Beginnt bei Tiefe"},
        {"max_tiefe", "Maximale Tiefe"},
        {"zustellung", "Zustellung je Durchgang"}, {"aufdicken", "Aufdicken"}, {"seite", "Seite"},
        {"aufmass", "Aufmass"}, {"fase", "Fase"}, {"fase_breite", "Fasenbreite"},
        {"stege_anzahl", "Haltestege"}, {"stege_breite", "Stegbreite"}, {"stege_hoehe", "Steghöhe"},
        {"stege_modus", "Haltestege setzen"}, {"eintauchen_art", "Eintauchen"}, {"rampe_winkel", "Rampenwinkel"},
        {"vorschub_mm", "Vorschub"}, {"vollnut_mm", "Vorschub bei Vollnut"}, {"gleichlauf", "Gleichlauf"},
        {"anfahren", "Anfahren"}, {"dogbone", "Dogbone"}, {"methode", "Methode"},
        {"schlichten", "Schlichten"}, {"schlicht_zugabe", "Schlicht-Zugabe"}, {"flach_werkzeug", "Flachwerkzeug"},
        {"strategie", "Strategie"}, {"bahnabstand", "Bahnabstand"}, {"adaptiv_last", "Schnittbreite (adaptiv)"},
        {"v_wand", "Wand beim V-Fräser"}, {"raeum_werkzeug", "Vorräumen mit"},
        {"raeum_zustellung", "Zustellung beim Räumen"}, {"raeum_strategie", "Strategie beim Räumen"},
        {"raeum_bahnabstand", "Bahnabstand beim Räumen"}, {"raeum_vorschub_mm", "Vorschub beim Räumen"},
        {"schlicht_werkzeug", "Werkzeug zum Schlichten"}, {"schlicht_zustellung", "Zustellung beim Schlichten"}, {"schlicht_tiefe", "Tiefe beim Schlichten"},
        {"schlicht_vorschub_mm", "Vorschub beim Schlichten"},
        {"zustell_modus", "Aufteilung der Zustellungen"}, {"letzte_zustellung", "Letzte Zustellung"},
        {"zustell_liste", "Eigene Zustellungen"}, {"schlicht_gleichlauf", "Fräsrichtung beim Schlichten"},
        {"steigung", "Steigung"}, {"kernloch_werkzeug", "Kernloch"}, {"innen", "Innengewinde"},
        {"durchgaenge", "Durchgänge"}, {"passung", "Passung"}};
    auto text = [this](std::string const &k, bj::value const *v) -> std::string {
        if (k == "zustell_modus") {
            std::string m = v && v->is_string() ? std::string(v->as_string()) : std::string();
            return m == "kleiner" ? TR("letzte Zustellung kleiner") : m == "liste" ? TR("eigene Liste")
                   : m == "eins" ? TR("in einem Durchgang") : TR("gleichmässig");
        }
        if (k == "schlicht_gleichlauf" && (!v || !v->is_bool())) return TR("wie beim Schruppen");
        if (k == "schlicht_gleichlauf") return v->as_bool() ? TR("Gleichlauf") : TR("Gegenlauf");
        if (k == "steigung" && (!v || v->is_null())) return TR("Regelgewinde");
        if (k == "kernloch_werkzeug" && (!v || !v->is_string() || v->as_string().empty())) return TR("schon gebohrt");
        if (v && v->is_array()) {
            std::vector<double> w;
            for (auto const &x : v->as_array())
                if (x.is_number()) w.push_back(x.to_number<double>());
            return w.empty() ? "–" : zustell_liste_text(w);
        }
        if (!v || v->is_null()) return "–";
        if (v->is_bool()) return v->as_bool() ? TR("an") : TR("aus");
        if (v->is_number()) return zahl(v->is_double() ? v->as_double() : (double)v->to_number<double>());
        if (v->is_string()) {
            std::string s(v->as_string());
            if (k == "werkzeug" || k == "flach_werkzeug" || k == "raeum_werkzeug" || k == "schlicht_werkzeug" ||
                k == "kernloch_werkzeug") return s.empty() ? TR("keines") : werkzeug_kurz(s);
            return s.empty() ? "–" : s;
        }
        return "…";
    };
    std::vector<std::array<std::string, 3>> aus;
    for (auto const &[k, name] : NAMEN) {
        auto a = start->if_contains(k);
        auto j = jetzt.if_contains(k);
        if ((!a && !j) || (a && j && *a == *j)) continue;
        if (a && j && a->is_number() && j->is_number() &&
            std::abs(a->to_number<double>() - j->to_number<double>()) < 1e-9) continue;
        if (text(k, a) == text(k, j)) continue;          // z. B. fehlt ↔ gleichmässig
        aus.push_back({TR(name), text(k, a), text(k, j)});
    }
    return aus;
}

int HirchFraesen::Impl::aenderungen_zeigen(Gtk::Box *aend_box, bj::object const &start_o, bj::object const &jetzt)
{
    UI::remove_all_children(*aend_box);
    int n = 0;
    for (auto const &[name, alt_t, neu_t] : aenderungen_liste(start_o, jetzt)) {
        auto zeile = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        klasse(*zeile, "hirch-geaendert");
        auto l = label(name);
        l->set_hexpand(true);
        zeile->add(*l);
        auto alt = label(alt_t, "hirch-dim");
        alt->set_markup("<s>" + markup_escape(alt_t) + "</s>");
        zeile->add(*alt);
        zeile->add(*label("→ " + neu_t, "hirch-wert"));
        aend_box->add(*zeile);
        ++n;
    }
    if (n == 0) aend_box->add(*label(TR("Noch nichts geändert."), "hirch-dim"));
    aend_box->show_all();
    return n;
}

std::string HirchFraesen::Impl::aenderung_text(std::string const &name, bj::object const &start, bj::object const &jetzt)
{
    auto liste = aenderungen_liste(start, jetzt);
    if (str(start, "titel") != str(jetzt, "titel")) liste.insert(liste.begin(), {TR("Name der Bearbeitung"), str(start, "titel"), str(jetzt, "titel")});
    if (liste.empty()) return TRF("{} geöffnet, nichts geändert", {name});
    std::string t;
    for (std::size_t i = 0; i < liste.size() && i < 3; ++i)
        t += (i ? "; " : "") + liste[i][0] + " " + liste[i][1] + " → " + liste[i][2];
    if (liste.size() > 3) t += TRF(" (+{} weitere)", {std::to_string(liste.size() - 3)});
    return name + ": " + t;
}

void HirchFraesen::Impl::tasche_dialog(int index, bj::object b, std::string const &teil)
{
    auto info = art_info("tasche");
    auto id = str(b, "id");
    long n = objekte(id);
    auto T = std::make_shared<TaschenFelder>();
    T->raeumen = !str(b, "raeum_werkzeug").empty() && str(b, "raeum_werkzeug") != str(b, "werkzeug", "A");
    T->schlichten = flag(b, "schlichten", false);
    // geöffnet wird der Durchgang, auf den doppelgeklickt wurde (sonst die Tasche selbst)
    T->wahl = teil == "raeumen" && T->raeumen ? "raeumen" : teil == "schlicht" && T->schlichten ? "schlicht" : "haupt";

    auto dlg = dialog(TR("Bearbeitung ") + name_von(b));
    dlg->set_resizable(true);
    dlg->set_default_size(1300, 800);
    auto box = dlg->get_content_area();
    box->set_spacing(0);
    box->set_margin_start(0);
    box->set_margin_end(0);
    box->set_margin_top(0);

    auto beobachten = [T](Gtk::Widget *w) {
        if (auto s = dynamic_cast<Gtk::SpinButton *>(w)) s->signal_value_changed().connect([T] { T->alle(); });
        else if (auto c = dynamic_cast<Gtk::ComboBoxText *>(w)) c->signal_changed().connect([T] { T->alle(); });
        else if (auto k = dynamic_cast<Gtk::CheckButton *>(w)) k->signal_toggled().connect([T] { T->alle(); });
    };
    auto wz = [this](Gtk::ComboBoxText *c) { return wirksam(c->get_active_id().raw()); };
    // Breite, mit der ein Werkzeug den Boden ausräumt (V-Fräser: die Spitze)
    auto breite = [](bj::object const &w) {
        if (str(w, "typ") == "v" && num(w, "spitze", 0) > 0) return num(w, "spitze", 0);
        return num(w, "durchmesser", 6);
    };
    auto ist_v = [](bj::object const &w) { return str(w, "typ") == "v"; };

    // ---- Kopf
    auto kopf = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 12);
    kopf->set_margin_start(20);
    kopf->set_margin_end(20);
    kopf->set_margin_top(12);
    kopf->set_margin_bottom(12);
    kopf->add(*farbfeld(info, 16, str(b, "farbe")));
    auto kt = label("");
    kt->set_markup("<big><b>" + markup_escape(TR("Tasche")) + "</b></big>");
    kopf->add(*kt);
    T->titel = Gtk::make_managed<Gtk::Entry>();
    T->titel->set_text(str(b, "titel"));
    T->titel->set_placeholder_text(TR("Name (automatisch)"));
    T->titel->set_width_chars(28);
    T->titel->set_activates_default(true);
    kopf->add(*T->titel);
    kopf->add(*label(anzahl(n, TR("Objekt"), TR("Objekte")) + TR(" in diesem Farbton"), "hirch-dim"));
    auto summe = label("", "hirch-wert", 1.0f);
    summe->set_hexpand(true);
    kopf->add(*summe);
    box->add(*kopf);
    box->add(*Gtk::make_managed<Gtk::Separator>());

    auto haupt = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
    haupt->set_vexpand(true);
    box->add(*haupt);

    // ---- Oben: Sprungleiste (Durchgänge mit Kurzinfo) und Suche; darunter alles auf einer Seite
    auto mitte = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 0);
    mitte->set_hexpand(true);
    haupt->add(*mitte);
    auto oben = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    oben->set_margin_start(14);
    oben->set_margin_end(14);
    oben->set_margin_top(10);
    oben->set_margin_bottom(10);
    auto links = Gtk::make_managed<Gtk::FlowBox>();
    links->set_selection_mode(Gtk::SELECTION_NONE);
    links->set_max_children_per_line(6);
    links->set_min_children_per_line(2);
    links->set_column_spacing(6);
    links->set_row_spacing(6);
    links->set_hexpand(true);
    oben->add(*links);
    auto suche = Gtk::make_managed<Gtk::SearchEntry>();
    suche->set_placeholder_text(TR("Einstellung suchen"));
    suche->set_width_chars(22);
    suche->set_valign(Gtk::ALIGN_START);
    oben->add(*suche);
    // Peter, 20.10.: keine Schritt-Übersicht und keine Suche mehr – alles steht untereinander auf
    // einer Seite, jeder Abschnitt zeigt oben in einer Zeile, was eingestellt ist
    oben->set_no_show_all(true);         // bleibt im Dialog (unsichtbar), damit alles mit ihm abgebaut wird
    mitte->add(*oben);

    struct Zeile { Gtk::Label *titel, *unter, *zeit, *chip; };
    auto zeilen = std::make_shared<std::map<std::string, Zeile>>();
    auto schritt = [this, T, zeilen](std::string const &k, bool farbe_statt_chip) {
        auto btn = Gtk::make_managed<Gtk::Button>();
        btn->set_relief(Gtk::RELIEF_NONE);
        klasse(*btn, "hirch-schritt");
        auto g = Gtk::make_managed<Gtk::Grid>();
        g->set_column_spacing(10);
        auto chip = label("", "hirch-chip", 0.5f);
        chip->set_size_request(30, -1);
        chip->set_valign(Gtk::ALIGN_CENTER);
        if (farbe_statt_chip) klasse(*chip, "hirch-chip-form");
        g->attach(*chip, 0, 0, 1, 2);
        auto t = label("", "hirch-titel");
        t->set_hexpand(true);
        g->attach(*t, 1, 0);
        auto u = label("", "hirch-dim");
        klasse(*u, "hirch-klein");
        u->set_line_wrap(true);
        u->set_max_width_chars(30);
        g->attach(*u, 1, 1);
        auto z = label("", "hirch-mono", 1.0f);
        klasse(*z, "hirch-dim");
        z->set_valign(Gtk::ALIGN_CENTER);
        g->attach(*z, 2, 0, 1, 2);
        btn->add(*g);
        btn->signal_clicked().connect([T, k] {
            T->wahl = k;
            T->alle();
            T->hinrollen(k);
        });
        T->schritte[k] = btn;
        (*zeilen)[k] = {t, u, z, chip};
        return btn;
    };
    links->add(*schritt("form", true));
    links->add(*schritt("raeumen", false));
    links->add(*schritt("haupt", false));
    links->add(*schritt("schlicht", false));
    T->neu_raeumen = Gtk::make_managed<Gtk::Button>(TR("+  Vorräumen mit grösserem Fräser"));
    T->neu_schlicht = Gtk::make_managed<Gtk::Button>(TR("+  Schlichten hinzufügen"));
    for (auto nb : {T->neu_raeumen, T->neu_schlicht}) {
        klasse(*nb, "hirch-neu");
        nb->set_relief(Gtk::RELIEF_NONE);
        nb->set_halign(Gtk::ALIGN_START);
        if (auto l = dynamic_cast<Gtk::Label *>(nb->get_child())) l->set_xalign(0.0f);
    }

    // ---- Mitte: Werte des gewählten Schritts
    auto rollen = Gtk::make_managed<Gtk::ScrolledWindow>();
    rollen->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
    rollen->set_vexpand(true);
    mitte->add(*rollen);
    auto alles = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 18);
    alles->set_margin_start(22);
    alles->set_margin_end(22);
    alles->set_margin_top(14);
    alles->set_margin_bottom(22);
    rollen->add(*alles);
    T->hinrollen = [T, rollen, alles](std::string const &k) {
        Glib::signal_idle().connect_once([T, rollen, alles, k] {
            auto it = T->abschnitte.find(k);
            if (it == T->abschnitte.end()) return;
            int x = 0, y = 0;
            if (it->second->translate_coordinates(*alles, 0, 0, x, y)) {
                auto adj = rollen->get_vadjustment();
                adj->set_value(std::min<double>(std::max(0, y - 6), adj->get_upper() - adj->get_page_size()));
            }
        });
    };
    auto seite = [T, alles](std::string const &k) {
        auto s = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 12);
        alles->add(*s);
        T->abschnitte[k] = s;
        return s;
    };
    // Suche: Abschnitte, in denen der Text vorkommt
    std::function<void(Gtk::Widget *, std::string &)> texte = [&texte](Gtk::Widget *w, std::string &aus) {
        if (auto l = dynamic_cast<Gtk::Label *>(w)) aus += " " + l->get_text().raw();
        if (auto c = dynamic_cast<Gtk::ComboBoxText *>(w)) {
            if (auto m = c->get_model()) {
                for (auto const &row : m->children()) {
                    Glib::ustring t;
                    row.get_value(0, t);
                    aus += " " + t.raw();
                }
            }
        }
        if (auto c = dynamic_cast<Gtk::Container *>(w))
            for (auto k : c->get_children()) texte(k, aus);
    };
    suche->signal_search_changed().connect([suche, T, texte] {
        T->suche = suche->get_text().lowercase().raw();
        T->texte = texte;
        T->alle();
    });
    auto titelzeile = [T](Gtk::Box *s, Gtk::Label *&titel, Gtk::Label *&unter, Gtk::Button *knopf = nullptr,
                          std::string const &k = "") {
        auto z = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 12);
        auto v = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
        v->set_hexpand(true);
        titel = label("");
        klasse(*titel, "hirch-seitentitel");
        unter = label("", "hirch-dim");
        unter->set_line_wrap(true);
        v->add(*titel);
        if (!k.empty()) {                  // was eingestellt ist – auf einen Blick
            auto kz = label("", "hirch-wert");
            kz->set_line_wrap(true);
            kz->set_xalign(0);
            v->add(*kz);
            T->kurz[k] = kz;
        }
        v->add(*unter);
        z->add(*v);
        if (knopf) {
            knopf->set_valign(Gtk::ALIGN_START);
            z->add(*knopf);
        }
        s->add(*z);
    };
    auto karte = [](Gtk::Box *s, std::string const &titel) {
        auto k = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
        klasse(*k, "hirch-karte2");
        k->add(*label(titel, "hirch-kopf"));
        s->add(*k);
        auto g = Gtk::make_managed<Gtk::Grid>();
        g->set_row_spacing(8);
        g->set_column_spacing(12);
        k->add(*g);
        return std::make_pair(k, g);
    };
    // Zeile: Beschriftung | Feld | Erklärung (wird live nachgeführt)
    auto feld = [](Gtk::Grid *g, int r, std::string const &text, Gtk::Widget &w, Gtk::Label **hint = nullptr) {
        auto l = label(text);
        l->set_size_request(200, -1);
        g->attach(*l, 0, r);
        w.set_halign(Gtk::ALIGN_START);
        if (!hint) {                       // ohne Erklärung: das Feld darf breiter werden
            g->attach(w, 1, r, 2);
            return l;
        }
        g->attach(w, 1, r);
        auto h = label("", "hirch-dim");
        klasse(*h, "hirch-klein");
        h->set_hexpand(true);
        h->set_line_wrap(true);
        g->attach(*h, 2, r);
        *hint = h;
        return l;
    };
    auto spin = [](double wert, double min, double max, double schritt, int stellen) {
        auto adj = Gtk::Adjustment::create(wert, min, max, schritt, schritt * 10);
        auto s = Gtk::make_managed<Gtk::SpinButton>(adj, schritt, stellen);
        s->set_numeric(true);
        punkt_und_komma(s);
        s->set_width_chars(7);
        s->set_activates_default(true);
        return s;
    };
    auto combo = [](std::initializer_list<std::pair<char const *, char const *>> eintraege, std::string const &aktiv) {
        auto c = Gtk::make_managed<Gtk::ComboBoxText>();
        for (auto &e : eintraege) c->append(e.first, e.second);
        c->set_active_id(aktiv);
        if (c->get_active_row_number() < 0) c->set_active(0);
        return c;
    };
    auto werkzeugzeile = [this](Gtk::Grid *g, int r, Gtk::ComboBoxText *&c, std::string const &aktiv,
                                Gtk::Label *&detail) {
        c = Gtk::make_managed<Gtk::ComboBoxText>();
        c->set_hexpand(true);
        werkzeug_combo(*c, aktiv, false);
        // kein eigener Knopf «Werkzeuge …» mehr: das Feld öffnet den Werkzeugdialog (Peter, 20.10.)
        g->attach(*c, 0, r, 3);
        auto btn = Gtk::make_managed<Gtk::Button>(TR("Werkzeuge …"));
        detail = label("", "hirch-dim");
        klasse(*detail, "hirch-klein");
        g->attach(*detail, 0, r + 1, 3);
        return btn;
    };
    std::vector<Gtk::Button *> werkzeug_knoepfe;
    auto w0 = wirksam(str(b, "werkzeug", "A"));

    // ======== Form und Tiefe
    {
        auto s = seite("form");
        Gtk::Label *t, *u;
        titelzeile(s, t, u, nullptr, "form");
        t->set_text(TR("Form und Tiefe"));
        u->set_text(TR("Gilt für alle Durchgänge dieser Tasche."));
        auto [k1, g1] = karte(s, "TIEFE");
        T->tiefe = spin(num(b, "tiefe", 3), 0.05, 200, 0.1, 2);
        Gtk::Label *h1;
        feld(g1, 0, TR("Tiefe der Tasche"), *T->tiefe, &h1);
        h1->set_text(TR("mm ab Oberfläche"));
        T->starttiefe = spin(num(b, "starttiefe", 0), 0, 200, 0.1, 2);
        T->starttiefe->set_tooltip_text(TR("Erst ab dieser Tiefe fräsen, z. B. wenn eine frühere Bearbeitung oder "
                                        "das vorbearbeitete Werkstück oben schon frei ist. Zugestellt wird nur "
                                        "zwischen dieser Tiefe und der Tiefe der Tasche; bis knapp darüber im "
                                        "Eilgang."));
        Gtk::Label *hst;
        feld(g1, 1, TR("Beginnt bei Tiefe"), *T->starttiefe, &hst);
        T->neu.push_back([T, hst] {
            double a = T->ab();
            if (a <= 1e-6) hst->set_text(TR("mm · 0 = an der Oberfläche"));
            else if (a >= T->tiefe->get_value() - 1e-6) hst->set_text(TR("mm · muss kleiner sein als die Tiefe"));
            else hst->set_text(TR("mm · darüber muss schon frei sein – bis dahin Eilgang"));
        });
        beobachten(T->starttiefe);
        // 3D: Tiefe ab der Oberfläche des 3D-Modells (nur wenn eines in der Zeichnung liegt)
        if (hirch_3d_hat_modell(doc()) || !str(b, "tiefe_ab").empty()) {
            // B-18 (Peter 02.11., Variante C): kurze Namen, Skizze und Satz darunter
            T->tiefe_ab = combo({{"material", TR("flach, ab Materialoberfläche")}, {"modell", TR("dem Modell folgen")},
                                 {"bis_modell", TR("bis aufs Modell hinunter")}},
                                str(b, "tiefe_ab", "material"));
            feld(g1, 2, TR("Wie tief"), *T->tiefe_ab);
            beobachten(T->tiefe_ab);
            // Peter 01.11. (Servoschacht im Flügel): tiefer oder weniger tief als das Modell
            T->tiefe_ab_mm = spin(num(b, "tiefe_ab_mm", 0), -50, 50, 0.1, 2);
            Gtk::Label *hm;
            feld(g1, 5, TR("Abstand zum Modell"), *T->tiefe_ab_mm, &hm);   // unter Skizze und Satz wie bei der Kontur
            hm->set_text(TR("mm · + tiefer ins Modell, − Material bleibt stehen"));
            beobachten(T->tiefe_ab_mm);
            auto tsk = skizze(84, [T](Cr const &cr, double w, double h, Gdk::RGBA const &fg) {
                skizze_tiefe_ab(cr, w, h, fg, T->tiefe_ab->get_active_id().raw());
            });
            T->bilder.push_back(tsk);
            g1->attach(*tsk, 0, 3, 3);
            auto satz = label("", "hirch-dim");
            klasse(*satz, "hirch-klein");
            satz->set_xalign(0);
            satz->set_line_wrap(true);
            satz->set_max_width_chars(60);
            g1->attach(*satz, 0, 4, 3);
            T->neu.push_back([T, h1, satz] {
                auto ab = T->tiefe_ab->get_active_id().raw();
                bool bis = ab == "bis_modell";
                zeile_zeigen(T->tiefe_ab_mm, bis);
                h1->set_text(bis ? TR("mm · höchstens so tief (0 = bis aufs Modell, egal wie tief)")
                                 : TR("mm ab Oberfläche"));
                // Peter 03.11.: «Beginnt bei Tiefe» auch bei «bis aufs Modell» (vorher geschlichtet)
                T->starttiefe->set_sensitive(true);
                satz->set_text(tiefe_ab_satz(ab));
            });
        }
        auto [k2, g2] = karte(s, "RAND");
        T->aufmass = spin(num(b, "aufmass", 0), -5, 5, 0.05, 2);
        Gtk::Label *h2;
        feld(g2, 0, TR("Aufmass"), *T->aufmass, &h2);
        h2->set_text(TR("mm · + lässt Material an der Wand stehen, − nimmt mehr weg"));
        T->richtung = combo({{"1", TR("Gleichlauf")}, {"0", TR("Gegenlauf")}}, flag(b, "gleichlauf", true) ? "1" : "0");
        Gtk::Label *h3;
        feld(g2, 1, TR("Fräsrichtung"), *T->richtung, &h3);
        h3->set_text(TR("Gleichlauf: saubere Kante (Standard)"));
        T->dogbone = Gtk::make_managed<Gtk::CheckButton>(TR("Ecken freifräsen (Dogbone) – damit ein eckiges Teil "
                                                         "hineinpasst"));
        T->dogbone->set_active(flag(b, "dogbone", false));
        g2->attach(*T->dogbone, 1, 2, 2);
        // V-Fräser: wo die schräge Wand auf der Linie liegt
        T->vwand = combo({{"oben", TR("Oben genau auf der Linie")}, {"unten", TR("Unten auf der Linie")}},
                         str(b, "v_wand", "oben"));
        T->vwand->set_tooltip_text(TR("Ein V-Fräser macht schräge Wände. «Oben»: die Kante an der Oberfläche liegt "
                                   "genau auf der Linie, nach unten wird die Tasche schmaler (Brennstempel, "
                                   "Stempel, erhabene Schrift). «Unten»: der Boden folgt der Linie, oben wird die "
                                   "Tasche breiter."));
        Gtk::Label *hv;
        auto lv = feld(g2, 3, TR("Wand mit V-Fräser"), *T->vwand, &hv);
        T->neu.push_back([T, hv, lv, wz, ist_v] {
            auto w = wz(T->werkzeug);
            bool v = ist_v(w);
            zeile_zeigen(T->vwand, v);
            (void)lv;
            if (!v) {
                hv->set_text(TR("nur mit V-Fräser – sonst senkrecht"));
                return;
            }
            double t = std::tan(num(w, "v_winkel", 60) * M_PI / 360.0) * T->tiefe->get_value();
            hv->set_text(T->vwand->get_active_id() == "oben"
                             ? TR("unten ") + zahl(t) + TR(" mm weiter innen; der grosse Fräser bleibt dahinter")
                             : TR("oben ") + zahl(t) + TR(" mm breiter als gezeichnet"));
        });
        for (Gtk::Widget *w : std::initializer_list<Gtk::Widget *>{T->tiefe, T->aufmass, T->richtung, T->dogbone,
                                                                    T->vwand})
            beobachten(w);
    }

    // ======== Räumen (grosser Fräser zuerst)
    {
        alles->add(*T->neu_raeumen);       // «+ Vorräumen …» steht dort, wo der Durchgang hinkäme
        auto s = seite("raeumen");
        auto weg = Gtk::make_managed<Gtk::Button>(TR("Durchgang entfernen"));
        Gtk::Label *t, *u;
        titelzeile(s, t, u, weg, "raeumen");
        t->set_text(TR("Räumen"));
        u->set_text(TR("Der grosse Fräser nimmt zuerst so viel wie möglich weg."));
        weg->signal_clicked().connect([T] {
            T->raeumen = false;
            T->wahl = "haupt";
            T->alle();
        });
        auto [k1, g1] = karte(s, "WERKZEUG");
        Gtk::Label *det;
        werkzeug_knoepfe.push_back(werkzeugzeile(g1, 0, T->r_werkzeug, str(b, "raeum_werkzeug", ""), det));
        T->r_fehler = label("", "hirch-fehler");
        T->r_fehler->set_line_wrap(true);
        T->r_fehler->set_no_show_all(true);
        g1->attach(*T->r_fehler, 0, 2, 3);
        T->neu.push_back([T, det, wz] { det->set_text(werkzeug_details(wz(T->r_werkzeug))); });
        auto [k2, g2] = karte(s, TR("ZUSTELLUNG UND BAHNEN"));
        T->r_zust = spin(num(b, "raeum_zustellung", 0), 0, 200, 0.1, 2);
        T->r_zust->set_tooltip_text(TR("0 = halber Durchmesser (höchstens die Tiefe der Tasche)"));
        Gtk::Label *hz, *hb, *ha;
        feld(g2, 0, TR("Zustellung"), *T->r_zust, &hz);
        auto rs = str(b, "raeum_strategie", "");
        if (rs.empty()) rs = str(b, "strategie", "runden");
        T->r_strategie = combo({{"runden", TR("In Runden")}, {"adaptiv", TR("Adaptiv")}}, rs);
        feld(g2, 1, TR("Strategie"), *T->r_strategie);
        T->r_bahn = spin(ist_null(b, "raeum_bahnabstand") ? num(b, "bahnabstand", 40) : num(b, "raeum_bahnabstand", 40),
                         5, 90, 1, 0);
        feld(g2, 2, TR("Bahnabstand"), *T->r_bahn, &hb);
        T->r_alast = spin(ist_null(b, "raeum_adaptiv_last") ? num(b, "adaptiv_last", 20) : num(b, "raeum_adaptiv_last", 20),
                          5, 60, 1, 0);
        auto la = feld(g2, 3, TR("Schnittbreite (adaptiv)"), *T->r_alast, &ha);
        T->neu.push_back([T, hz, hb, ha, la, wz, breite] {
            auto w = wz(T->r_werkzeug);
            double d = breite(w), tiefe = T->weg();
            double z = T->r_zust->get_value() > 0 ? T->r_zust->get_value() : num(w, "durchmesser", 6) / 2;
            z = std::min(z, tiefe);
            int k = std::max(1, (int)std::ceil(tiefe / std::max(0.01, z) - 1e-9));
            hz->set_text(std::string(T->r_zust->get_value() > 0 ? "mm" : TR("auto = halber Ø")) + " → " +
                         std::to_string(k) + (k == 1 ? TR(" Durchgang") : TR(" Durchgänge")) + " à " +
                         zahl(tiefe / k) + " mm");
            hb->set_text(TR("% vom Ø = ") + zahl(d * T->r_bahn->get_value() / 100.0) + " mm");
            ha->set_text(TR("% vom Ø = ") + zahl(d * T->r_alast->get_value() / 100.0) + " mm");
            bool ad = T->r_strategie->get_active_id() == "adaptiv";
            zeile_zeigen(T->r_bahn, !ad);
            zeile_zeigen(T->r_alast, ad);
            (void)la;
        });
        auto [k3, g3] = karte(s, TR("EINTAUCHEN UND VORSCHUB"));
        T->r_eintauchen = combo({{"rampe", TR("Schräg (Rampe)")}, {"senkrecht", TR("Senkrecht")}},
                                str(b, "raeum_eintauchen_art", "").empty() ? str(b, "eintauchen_art", "rampe")
                                                                          : str(b, "raeum_eintauchen_art", "rampe"));
        Gtk::Label *he = nullptr, *hv = nullptr, *hp = nullptr;
        feld(g3, 0, TR("Eintauchen"), *T->r_eintauchen, &he);
        T->r_rwinkel = spin(ist_null(b, "raeum_rampe_winkel") ? num(b, "rampe_winkel", 3) : num(b, "raeum_rampe_winkel", 3),
                            0.5, 45, 0.5, 1);
        Gtk::Label *hw;
        feld(g3, 1, TR("Rampenwinkel"), *T->r_rwinkel, &hw);
        hw->set_text("°");
        T->v_raeum = vorschub_zeile(g3, 2, TR("Vorschub"), num(b, "raeum_vorschub_mm", 0),
                                    num(b, "raeum_vorschub_prozent", num(b, "vorschub_prozent", 100)),
                                    [T, wz] { return num(wz(T->r_werkzeug), "vorschub", 1000); });
        T->vn_raeum = vorschub_zeile(g3, 3, TR("Vorschub bei Vollnut"), num(b, "raeum_vollnut_mm", 0),
                                     ist_null(b, "raeum_vollnut") ? 0 : num(b, "raeum_vollnut", 0),
                                     [T, wz] { return T->v_raeum->spin->get_value() *
                                                      num(wz(T->r_werkzeug), "vollnut", 0.5); });
        T->r_vprozent = T->v_raeum->spin;
        T->r_vollnut = T->vn_raeum->spin;
        (void)hv;
        (void)hp;
        T->neu.push_back([T, he, wz, ist_v] {
            auto w = wz(T->r_werkzeug);
            bool v = ist_v(w);
            zeile_zeigen(T->r_eintauchen, !v);
            zeile_zeigen(T->r_rwinkel, !v && T->r_eintauchen->get_active_id() == "rampe");
            he->set_text(v ? TR("V-Fräser taucht senkrecht ein") : "");
            T->v_raeum->nachfuehren();
            T->vn_raeum->nachfuehren();
        });
        auto sk = skizze(130, [T, wz, breite](Cr const &cr, double w, double h, Gdk::RGBA const &fg) {
            skizze_tasche(cr, w, h, fg, T->r_strategie->get_active_id() == "adaptiv", T->r_bahn->get_value(),
                          T->r_alast->get_value(), T->richtung->get_active_id() == "1", T->aufmass->get_value(),
                          breite(wz(T->r_werkzeug)));
        });
        T->bilder.push_back(sk);
        s->add(*sk);
        for (Gtk::Widget *w : std::initializer_list<Gtk::Widget *>{T->r_werkzeug, T->r_zust, T->r_strategie, T->r_bahn,
                                                                    T->r_alast, T->r_eintauchen, T->r_rwinkel,
                                                                    T->r_vollnut, T->r_vprozent})
            beobachten(w);
    }

    // ======== Werkzeug der Tasche: Ausräumen bzw. Restmaterial
    {
        auto s = seite("haupt");
        Gtk::Label *t, *u;
        titelzeile(s, t, u, nullptr, "haupt");
        T->neu.push_back([T, t, u] {
            t->set_text(T->raeumen ? TR("Restmaterial") : TR("Ausräumen"));
            u->set_text(T->raeumen ? TR("Fräst nur, wo das Räumwerkzeug nicht hinkommt: Ecken, schmale Stellen, "
                                     "Spitzen.")
                                   : TR("Räumt die ganze Fläche aus."));
        });
        auto [k1, g1] = karte(s, "WERKZEUG");
        Gtk::Label *det;
        werkzeug_knoepfe.push_back(werkzeugzeile(g1, 0, T->werkzeug, str(b, "werkzeug", "A"), det));
        // neue Bearbeitung (Einfärben, «+ Neu»): das Werkzeug ist erst ein Vorschlag (Peter, 22.10.)
        std::string vorschlag = neu_offen ? str(b, "werkzeug", "A") : std::string();
        T->neu.push_back([T, det, wz, ist_v, breite, vorschlag] {
            auto w = wz(T->werkzeug);
            std::string t = ist_v(w) ? TR("Spitze ") + zahl(breite(w)) + " mm · " + werkzeug_details(w) : werkzeug_details(w);
            if (!vorschlag.empty() && T->werkzeug->get_active_id().raw() == vorschlag)
                t = TR("Vorschlag – passt er? Klick ins Feld öffnet die Werkzeugwahl.") + std::string("\n") + t;
            det->set_text(t);
        });
        auto [k2, g2] = karte(s, TR("ZUSTELLUNG UND BAHNEN"));
        T->zust = spin(num(b, "zustellung", 1.5), 0.05, 50, 0.1, 2);
        Gtk::Label *hz, *hb, *ha;
        feld(g2, 0, TR("Zustellung je Durchgang"), *T->zust, &hz);
        T->zart = zustell_art_zeilen(*g2, 1, b, T->zust, false);
        T->zart->zust_text_normal = TR("Zustellung je Durchgang");
        T->zart->liste->signal_changed().connect([T] { T->alle(); });
        T->strategie = combo({{"runden", TR("In Runden")}, {"adaptiv", TR("Adaptiv")}}, str(b, "strategie", "runden"));
        auto ls = feld(g2, 4, TR("Strategie"), *T->strategie);
        T->bahn = spin(num(b, "bahnabstand", 40), 5, 90, 1, 0);
        feld(g2, 5, TR("Bahnabstand"), *T->bahn, &hb);
        T->alast = spin(num(b, "adaptiv_last", 20), 5, 60, 1, 0);
        auto la = feld(g2, 6, TR("Schnittbreite (adaptiv)"), *T->alast, &ha);
        // Richtwerte aus der Werkzeugdatenbank
        auto rk = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        auto rtext = label("", "hirch-dim");
        klasse(*rtext, "hirch-klein");
        rtext->set_hexpand(true);
        rtext->set_line_wrap(true);
        rk->add(*rtext);
        auto rbtn = Gtk::make_managed<Gtk::Button>(TR("Übernehmen"));
        rk->add(*rbtn);
        g2->attach(*rk, 1, 7, 2);
        // Schnitt mit den Zustellungen
        auto zsk = skizze(96, [T](Cr const &cr, double w, double h, Gdk::RGBA const &fg) {
            skizze_zustellungen(cr, w, h, fg, T->zart->stufen(T->weg()), T->ab());
        });
        T->bilder.push_back(zsk);
        k2->add(*zsk);
        auto mat = werkstoff_name();
        T->neu.push_back([T, hz, hb, ha, ls, la, rtext, rbtn, wz, ist_v, breite, mat] {
            auto w = wz(T->werkzeug);
            double d = breite(w), tiefe = T->weg();
            T->zart->nachfuehren();
            hz->set_text("mm → " + T->zart->kurz(tiefe));
            std::string bezug = ist_v(w) ? TR("% der Spitze = ") : TR("% vom Ø = ");
            hb->set_text(bezug + zahl(d * T->bahn->get_value() / 100.0) + " mm");
            ha->set_text(bezug + zahl(d * T->alast->get_value() / 100.0) + " mm");
            // nach dem Räumen immer in Runden
            T->strategie->set_visible(!T->raeumen);
            ls->set_visible(!T->raeumen);
            bool ad = !T->raeumen && T->strategie->get_active_id() == "adaptiv";
            zeile_zeigen(T->bahn, !ad);
            T->alast->set_visible(!T->raeumen);
            la->set_visible(!T->raeumen);
            ha->set_visible(!T->raeumen);
            zeile_zeigen(T->alast, ad);
            (void)la;
            auto e = unter(w, "empfehlung");
            rbtn->set_visible(e != nullptr);
            if (!e) {
                rtext->set_text(TR("Keine Richtwerte für dieses Werkzeug."));
            } else {
                std::string tx = TR("Richtwert ") + mat + TR(": Zustellung ") + zahl(num(*e, "zustellung", 1)) +
                                 TR(" mm · Bahnabstand ") + zahl(num(*e, "bahnabstand", 40), 0) + " %";
                // adaptiv: volle Zustellung ist gewollt (schmaler Eingriff) – dann keine Warnung
                if (!ad && T->zust->get_value() > 1.5 * num(*e, "zustellung", 1) + 1e-9)
                    tx += TR("\n⚠ Die Zustellung ist deutlich höher als empfohlen.");
                rtext->set_text(tx);
            }
        });
        // Weite Tasche: adaptiv vorschlagen (Fräsbahnen 25.10., Peter: vorschlagen, nicht umstellen)
        {
            bool vorschlag = false;
            if (auto sts = unter(ergebnis, "bearbeitungen"))
                if (auto st = unter(*sts, id.c_str())) vorschlag = flag(*st, "adaptiv_vorschlag", false);
            auto ab = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
            auto at = label(TR("Weite Tasche – adaptiv mit voller Zustellung ist hier meist schneller."), "hirch-klein");
            at->set_hexpand(true);
            at->set_line_wrap(true);
            at->set_xalign(0);
            ab->add(*at);
            auto abtn = Gtk::make_managed<Gtk::Button>(TR("Adaptiv übernehmen"));
            abtn->set_valign(Gtk::ALIGN_CENTER);
            ab->add(*abtn);
            ab->set_no_show_all(true);
            at->show();
            abtn->show();
            g2->attach(*ab, 1, 8, 2);
            T->neu.push_back([T, ab, vorschlag] {
                ab->set_visible(vorschlag && !T->raeumen && T->strategie->get_active_id() == "runden");
            });
            abtn->signal_clicked().connect([T, wz] {
                auto w = wz(T->werkzeug);
                double sl = num(w, "schneidenlaenge", 0);
                double z = T->weg();
                if (sl > 0) z = std::min(z, sl);
                T->strategie->set_active_id("adaptiv");
                T->alast->set_value(15);
                if (z > 0) T->zust->set_value(z);
            });
        }
        rbtn->signal_clicked().connect([T, wz] {
            auto w = wz(T->werkzeug);
            auto e = unter(w, "empfehlung");
            if (!e) return;
            T->zust->set_value(num(*e, "zustellung", T->zust->get_value()));
            T->bahn->set_value(num(*e, "bahnabstand", 40));
            T->rwinkel->set_value(num(*e, "rampe", 3));
            T->vn_haupt->folgt = true;          // Vollnut wieder nach dem Werkzeug
            T->vn_haupt->nachfuehren();
        });
        auto [k3, g3] = karte(s, TR("EINTAUCHEN UND VORSCHUB"));
        T->eintauchen = combo({{"rampe", TR("Schräg (Rampe)")}, {"senkrecht", TR("Senkrecht")}}, str(b, "eintauchen_art", "rampe"));
        Gtk::Label *he = nullptr, *hw = nullptr, *hv = nullptr, *hp = nullptr;
        feld(g3, 0, TR("Eintauchen"), *T->eintauchen, &he);
        T->rwinkel = spin(ist_null(b, "rampe_winkel") || num(b, "rampe_winkel", 0) <= 0 ? num(w0, "rampe", 3)
                                                                                         : num(b, "rampe_winkel", 3),
                          0.5, 45, 0.5, 1);
        feld(g3, 1, TR("Rampenwinkel"), *T->rwinkel, &hw);
        hw->set_text("°");
        T->v_haupt = vorschub_zeile(g3, 2, TR("Vorschub"), num(b, "vorschub_mm", 0), num(b, "vorschub_prozent", 100),
                                    [T, wz] { return num(wz(T->werkzeug), "vorschub", 1000); });
        T->vn_haupt = vorschub_zeile(g3, 3, TR("Vorschub bei Vollnut"), num(b, "vollnut_mm", 0),
                                     ist_null(b, "vollnut") ? 0 : num(b, "vollnut", 0),
                                     [T, wz] { return T->v_haupt->spin->get_value() *
                                                      num(wz(T->werkzeug), "vollnut", 0.5); });
        T->vprozent = T->v_haupt->spin;
        T->vollnut = T->vn_haupt->spin;
        T->vfaktor = spin_zeile(*g3, 4, TR("Faktor (Zeitleiste)"), num(b, "vorschub_faktor", 100), 20, 400, 5, 0, "%");
        T->vfaktor->set_tooltip_text(TR("Wird in der Zeitleiste unten durch Ziehen gesetzt: alle Fräsvorschübe dieser "
                                     "Bearbeitung mal diesen Faktor (Eilgang und Eintauchen bleiben)."));
        (void)hv;
        (void)hp;
        T->neu.push_back([T, he, wz, ist_v] {
            auto w = wz(T->werkzeug);
            bool v = ist_v(w);
            zeile_zeigen(T->eintauchen, !v);
            zeile_zeigen(T->rwinkel, !v && T->eintauchen->get_active_id() == "rampe");
            he->set_text(v ? TR("V-Fräser taucht senkrecht ein") : "");
            T->v_haupt->nachfuehren();
            T->vn_haupt->nachfuehren();
        });
        auto sk = skizze(130, [T, wz, breite](Cr const &cr, double w, double h, Gdk::RGBA const &fg) {
            skizze_tasche(cr, w, h, fg, !T->raeumen && T->strategie->get_active_id() == "adaptiv",
                          T->bahn->get_value(), T->alast->get_value(), T->richtung->get_active_id() == "1",
                          T->aufmass->get_value(), breite(wz(T->werkzeug)));
        });
        T->bilder.push_back(sk);
        s->add(*sk);
        for (Gtk::Widget *w : std::initializer_list<Gtk::Widget *>{T->werkzeug, T->zust, T->strategie, T->bahn,
                                                                    T->alast, T->eintauchen, T->rwinkel, T->vollnut,
                                                                    T->vprozent, T->zart->modus, T->zart->letzte})
            beobachten(w);
    }

    // ======== Schlichten
    {
        alles->add(*T->neu_schlicht);
        auto s = seite("schlicht");
        auto weg = Gtk::make_managed<Gtk::Button>(TR("Durchgang entfernen"));
        Gtk::Label *t, *u;
        titelzeile(s, t, u, weg, "schlicht");
        t->set_text(TR("Schlichten"));
        u->set_text(TR("Ein letzter Durchgang fährt nur am Rand entlang auf Mass. Die Durchgänge davor lassen dafür "
                    "die Zugabe stehen."));
        weg->signal_clicked().connect([T] {
            T->schlichten = false;
            T->wahl = "haupt";
            T->alle();
        });
        auto [k1, g1] = karte(s, TR("WERKZEUG UND ZUGABE"));
        T->s_werkzeug = Gtk::make_managed<Gtk::ComboBoxText>();
        werkzeug_combo(*T->s_werkzeug, str(b, "schlicht_werkzeug", ""), false);
        T->s_werkzeug->prepend("", TR("gleiches wie die Tasche"));
        T->s_werkzeug->set_active_id(str(b, "schlicht_werkzeug", ""));
        feld(g1, 0, TR("Werkzeug"), *T->s_werkzeug);
        T->s_zugabe = spin(num(b, "schlicht_zugabe", 0.3), 0, 5, 0.05, 2);
        Gtk::Label *hz = nullptr, *hs = nullptr, *hv = nullptr;
        feld(g1, 1, TR("Schlichtzugabe"), *T->s_zugabe, &hz);
        hz->set_text(TR("mm bleiben bis zum Schlichten stehen"));
        double tj = num(b, "tiefe", 3);
        T->s_zust = spin(ist_null(b, "schlicht_zustellung") ? tj : num(b, "schlicht_zustellung", tj), 0.1, 200, 0.1, 2);
        if (ist_null(b, "schlicht_zustellung")) T->s_zust_ganz = T->s_zust->get_value();
        feld(g1, 2, TR("Zustellung"), *T->s_zust, &hs);
        T->v_schl = vorschub_zeile(g1, 3, TR("Vorschub"), num(b, "schlicht_vorschub_mm", 0),
                                   num(b, "schlicht_vorschub", 100), [T, wz] {
                                       return num(wz(T->s_werkzeug->get_active_id().empty() ? T->werkzeug
                                                                                             : T->s_werkzeug),
                                                  "vorschub", 1000);
                                   });
        T->s_vprozent = T->v_schl->spin;
        T->s_richtung = combo({{"", TR("wie beim Ausräumen")}, {"1", TR("Gleichlauf")}, {"0", TR("Gegenlauf")}}, "");
        if (auto v = b.if_contains("schlicht_gleichlauf"); v && v->is_bool()) T->s_richtung->set_active_id(v->as_bool() ? "1" : "0");
        T->s_richtung->set_tooltip_text(TR("Gegenlauf beim Schlichten gibt bei Spiel in der Maschine oft die genauere Kante – "
                                        "statt einer zweiten Bearbeitung als Kopie."));
        feld(g1, 4, TR("Fräsrichtung"), *T->s_richtung);
        (void)hv;
        T->neu.push_back([T] { T->v_schl->nachfuehren(); });
        T->neu.push_back([T, hs] {
            double tiefe = T->weg(), z = std::max(0.01, T->s_zust->get_value());
            int k = std::max(1, (int)std::ceil(tiefe / z - 1e-9));
            hs->set_text("mm → " + std::to_string(k) + (k == 1 ? TR(" Durchgang") : TR(" Durchgänge")));
        });
        auto sk = skizze(118, [T](Cr const &cr, double w, double h, Gdk::RGBA const &fg) {
            auto sw = T->s_werkzeug->get_active_id().raw();
            skizze_schlichten(cr, w, h, fg, true, T->s_zugabe->get_value(),
                              !sw.empty() && sw != T->werkzeug->get_active_id().raw());
        });
        T->bilder.push_back(sk);
        s->add(*sk);
        for (Gtk::Widget *w : std::initializer_list<Gtk::Widget *>{T->s_werkzeug, T->s_zugabe, T->s_zust, T->s_vprozent,
                                                                    T->s_richtung})
            beobachten(w);
    }

    // ---- Rechts: Vorschau (Stand der letzten Rechnung), Hinweise, Weg und Zeit
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
    rechts->add(*label(TR("GEÄNDERT SEIT DEM ÖFFNEN"), "hirch-kopf"));
    auto aend_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 3);
    rechts->add(*aend_box);
    auto aend_zeit = label("", "hirch-dim");
    klasse(*aend_zeit, "hirch-klein");
    rechts->add(*aend_zeit);
    auto vorg_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);    // «So fräse ich»
    rechts->add(*vorg_box);
    bool vorschau_leer = !ergebnis_aktuell || !ablauf_op(id);
    auto halter = std::make_shared<std::shared_ptr<LiveVorschau>>();
    auto klappe = vorschau_klappe(rechts, [this, halter, vorschau_leer] {
        if (vorschau_leer && *halter) live_rechnen(*halter, true);
    });
    auto live = live_vorschau(klappe, id, index, [T] { return T->wahl; }, {},
                              vorschau_leer ? TR("Die Vorschau erscheint gleich …")
                                            : TR("Stand der letzten Rechnung. Dunkel: was der Fräser wegnimmt; "
                                              "hell: der gewählte Durchgang."));
    *halter = live;
    T->bilder.push_back(live->bild);
    auto legende = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 14);
    for (auto [txt, farbtext] : {std::pair{TR("Räumen"), "#62a0ea"}, std::pair{TR("Tasche"), "#f66151"},
                                 std::pair{TR("Schlichten"), "#4dbf73"}}) {
        auto l = label("");
        l->set_markup(std::string("<span foreground='") + farbtext + "'>━</span> " + txt);
        klasse(*l, "hirch-klein");
        legende->add(*l);
    }
    klappe->add(*legende);

    auto hkopf = label(TR("HINWEISE"), "hirch-kopf");
    hkopf->set_margin_top(6);
    rechts->add(*hkopf);
    int hinweise = 0;
    if (auto op = ablauf_op(id)) {
        if (auto ws = liste_von(*op, "warnungen")) {
            for (auto const &v : *ws) {
                if (!v.is_string()) continue;
                auto l = label("⚠ " + std::string(v.get_string()), "hirch-warn");
                l->set_line_wrap(true);
                l->set_max_width_chars(44);
                klasse(*l, "hirch-klein");
                rechts->add(*l);
                ++hinweise;
            }
        }
    }
    if (ergebnis_aktuell) {
        if (auto pr = liste_von(ergebnis, "pruefung")) {
            for (auto const &v : *pr) {
                auto o = v.if_object();
                if (!o || str(*o, "bearbeitung") != id) continue;
                bool fehler = str(*o, "stufe") == "fehler";
                if (!fehler) {
                    rechts->add(*hinweis_widget(str(*o, "text"), o, quittiert_lesen(), 44));
                    ++hinweise;
                    continue;
                }
                auto l = label("⛔ " + str(*o, "text"), "hirch-fehler");
                l->set_line_wrap(true);
                l->set_max_width_chars(44);
                klasse(*l, "hirch-klein");
                rechts->add(*befund_widget(*o, l));
                ++hinweise;
            }
        }
    }
    if (!hinweise) {
        rechts->add(*hinweis_label(ergebnis_aktuell ? TR("✓ Nichts gefunden.") : TR("Erscheinen nach dem Rechnen.")));
    }
    auto fuss = Gtk::make_managed<Gtk::Grid>();
    fuss->set_row_spacing(4);
    fuss->set_column_spacing(12);
    fuss->set_valign(Gtk::ALIGN_END);
    fuss->set_vexpand(true);
    bj::object const *st = nullptr;
    if (ergebnis_aktuell) {
        if (auto sts = unter(ergebnis, "bearbeitungen")) st = unter(*sts, id.c_str());
    }
    if (st) {
        auto z = [&](int r, std::string const &a, std::string const &v, char const *k) {
            auto l = label(a, "hirch-dim");
            l->set_hexpand(true);
            fuss->attach(*l, 0, r);
            fuss->attach(*label(v, k, 1.0f), 1, r);
        };
        z(0, TR("Fräsweg"), einheit_weg(num(*st, "weg", 0)), "hirch-wert");
        z(1, TR("Zeit"), zeit(num(*st, "minuten", 0)), "hirch-haupt");
        T->zeit_label = dynamic_cast<Gtk::Label *>(fuss->get_child_at(1, 1));
        summe->set_text(zeit(num(*st, "minuten", 0)));
    }
    rechts->add(*fuss);
    // Vorschau zum Aufklappen unter der Zeit
    if (auto ex = klappe->get_parent()) {
        rechts->reorder_child(*ex, -1);
        ex->set_valign(Gtk::ALIGN_END);
    }
    // Passt etwas nicht (Räumwerkzeug zu klein): statt der Zeit steht, warum (Peter, 20.10.)
    std::function<void()> zeit_nf;
    {
        auto zeit_text = T->zeit_label ? T->zeit_label->get_text().raw() : std::string();
        zeit_nf = [T, zeit_text] {
            if (!T->zeit_label) return;
            if (T->ungueltig) {
                T->zeit_label->set_text(TR("⛔ so nicht fräsbar"));
                klasse(*T->zeit_label, "hirch-fehler");
            } else {
                T->zeit_label->set_text(zeit_text);
                T->zeit_label->get_style_context()->remove_class("hirch-fehler");
            }
        };
    }

    // ---- Schritte und Seiten nachführen
    auto zeit_von = [this, id](std::string const &teil) {
        auto op = ablauf_op(teil == "haupt" ? id : id + "#" + teil);
        return op ? zeit(num(*op, "minuten", 0)) : std::string();
    };
    std::string summe_zeit = summe->get_text().raw();       // Zeit der letzten Rechnung (oder leer)
    T->neu.push_back([this, T, zeilen, zeit_von, wz, breite, summe, summe_zeit] {
        auto chip = [](Gtk::ComboBoxText *c) { return std::string(c->get_active_id().raw()); };
        auto kurz = [this](Gtk::ComboBoxText *c) { return werkzeug_kurz(c->get_active_id().raw()); };
        double tiefe = T->tiefe->get_value(), weg = T->weg();
        auto durchg = [weg](double z) {
            int k = std::max(1, (int)std::ceil(weg / std::max(0.01, z) - 1e-9));
            return std::to_string(k) + (k == 1 ? TR(" Zustellung") : TR(" Zustellungen"));
        };
        auto &f = (*zeilen)["form"];
        f.titel->set_text(TR("Form und Tiefe"));
        f.chip->set_text(" ");
        f.unter->set_text((T->ab() > 1e-6 ? zahl(T->ab()) + "–" : std::string()) + zahl(tiefe) +
                          TR(" mm tief · Aufmass ") + zahl(T->aufmass->get_value()) + " · " +
                          (T->richtung->get_active_id() == "1" ? TR("Gleichlauf") : TR("Gegenlauf")));
        auto &r = (*zeilen)["raeumen"];
        r.titel->set_text(TR("Räumen"));
        r.chip->set_text(chip(T->r_werkzeug));
        {
            auto w = wz(T->r_werkzeug);
            double z = T->r_zust->get_value() > 0 ? T->r_zust->get_value() : num(w, "durchmesser", 6) / 2;
            r.unter->set_text(kurz(T->r_werkzeug) + " · " + durchg(std::min(z, tiefe)));
        }
        r.zeit->set_text(zeit_von("raeumen"));
        auto &h = (*zeilen)["haupt"];
        h.titel->set_text(T->raeumen ? TR("Restmaterial") : TR("Ausräumen"));
        h.chip->set_text(chip(T->werkzeug));
        h.unter->set_text(kurz(T->werkzeug) + " · " + durchg(T->zust->get_value()));
        h.zeit->set_text(zeit_von(""));
        auto &s = (*zeilen)["schlicht"];
        s.titel->set_text(TR("Schlichten"));
        s.chip->set_text(chip(T->s_werkzeug->get_active_id().empty() ? T->werkzeug : T->s_werkzeug));
        s.unter->set_text((T->s_werkzeug->get_active_id().empty() ? kurz(T->werkzeug) : kurz(T->s_werkzeug)) +
                          TR(" · Zugabe ") + zahl(T->s_zugabe->get_value()) + " mm");
        s.zeit->set_text(zeit_von("schlicht"));
        // je Abschnitt eine Zeile: was eingestellt ist (statt der Übersicht oben)
        for (auto const &k : {"form", "raeumen", "haupt", "schlicht"}) {
            auto it = T->kurz.find(k);
            if (it == T->kurz.end()) continue;
            auto &zl = (*zeilen)[k];
            std::string t = zl.unter->get_text().raw();
            if (std::string(k) == "haupt") t = kurz(T->werkzeug) + " · " + T->zart->kurz(weg);
            if (std::string(k) == "schlicht" && T->s_richtung && !T->s_richtung->get_active_id().empty())
                t += " · " + std::string(T->s_richtung->get_active_id() == "1" ? TR("Gleichlauf") : TR("Gegenlauf"));
            auto zt = zl.zeit->get_text().raw();
            it->second->set_text(t + (zt.empty() ? std::string() : " · " + zt));
        }
        // Sicherung: das Räumwerkzeug muss breiter räumen als das Werkzeug der Tasche
        {
            double dg = breite(wz(T->r_werkzeug)), dk = breite(wz(T->werkzeug));
            bool zu_klein = T->raeumen && dg <= dk + 1e-9;
            auto mass = [&](Gtk::ComboBoxText *c, double d) {
                return std::string(str(wz(c), "typ") == "v" ? TR("Spitze ") : "Ø ") + zahl(d) + " mm";
            };
            T->r_fehler->set_text(TR("⛔ Das Räumwerkzeug (") + mass(T->r_werkzeug, dg) + TR(") muss grösser sein als das "
                                  "Werkzeug für das Restmaterial (") + mass(T->werkzeug, dk) + TR("). Wähle ein "
                                  "grösseres oder entferne den Durchgang."));
            T->r_fehler->set_visible(zu_klein);
            auto sc = T->schritte["raeumen"]->get_style_context();
            if (zu_klein) sc->add_class("hirch-schritt-fehler");
            else sc->remove_class("hirch-schritt-fehler");
            T->ungueltig = zu_klein;
            if (T->zuweisen) T->zuweisen->set_sensitive(!zu_klein);
            if (T->ok) {
                T->ok->set_sensitive(!zu_klein);
                T->ok->set_tooltip_text(zu_klein ? TR("Das Räumwerkzeug ist nicht grösser als das Werkzeug der Tasche.")
                                                 : "");
            }
        }
        T->schritte["raeumen"]->set_visible(T->raeumen);
        T->schritte["schlicht"]->set_visible(T->schlichten);
        T->neu_raeumen->set_visible(!T->raeumen);
        T->neu_schlicht->set_visible(!T->schlichten);
        if ((T->wahl == "raeumen" && !T->raeumen) || (T->wahl == "schlicht" && !T->schlichten)) T->wahl = "haupt";
        for (auto &[k, btn] : T->schritte) {
            auto sc = btn->get_style_context();
            if (k == T->wahl) sc->add_class("hirch-schritt-an");
            else sc->remove_class("hirch-schritt-an");
        }
        for (auto &[k, w] : T->abschnitte) {
            bool da = (k != "raeumen" || T->raeumen) && (k != "schlicht" || T->schlichten);
            if (da && !T->suche.empty() && T->texte) {
                std::string t;
                T->texte(w, t);
                da = Glib::ustring(t).lowercase().raw().find(T->suche) != std::string::npos;
            }
            zeile_zeigen(w, da);
        }
        int werkzeuge = 1 + (T->raeumen ? 1 : 0) +
                        (T->schlichten && !T->s_werkzeug->get_active_id().empty() &&
                                 T->s_werkzeug->get_active_id() != T->werkzeug->get_active_id()
                             ? 1 : 0);
        auto t = summe_zeit;
        summe->set_text((t.empty() ? std::string() : t + " · ") + anzahl(werkzeuge, TR("Werkzeug"), TR("Werkzeuge")));
    });

    T->neu.push_back(zeit_nf);
    T->neu_raeumen->signal_clicked().connect([this, T] {
        // Vorschlag: der grösste Schaftfräser, der grösser ist als das Werkzeug der Tasche
        auto eigen = T->werkzeug->get_active_id().raw();
        double d_eigen = num(wirksam(eigen), "durchmesser", 0);
        if (str(wirksam(eigen), "typ") == "v") d_eigen = num(wirksam(eigen), "spitze", 0);
        std::string beste;
        double bd = d_eigen;
        if (auto ws = unter(einst, "werkzeuge")) {
            for (auto const &kv : *ws) {
                auto w = kv.value().if_object();
                if (!w || str(*w, "typ") == "v" || std::string(kv.key()) == eigen) continue;
                if (num(*w, "durchmesser", 0) > bd) {
                    bd = num(*w, "durchmesser", 0);
                    beste = kv.key();
                }
            }
        }
        if (!beste.empty()) T->r_werkzeug->set_active_id(beste);
        T->raeumen = true;
        T->wahl = "raeumen";
        T->alle();
        T->hinrollen("raeumen");
    });
    T->neu_schlicht->signal_clicked().connect([T] {
        T->schlichten = true;
        T->wahl = "schlicht";
        T->alle();
        T->hinrollen("schlicht");
    });
    auto auslesen = [this, T](bj::object &b) {
        b["tiefe"] = wert(T->tiefe);
        b["starttiefe"] = wert(T->starttiefe);
        if (T->tiefe_ab) {
            auto ab = T->tiefe_ab->get_active_id().raw();
            if (ab == "modell" || ab == "bis_modell") b["tiefe_ab"] = ab;
            else b.erase("tiefe_ab");
            if (ab == "bis_modell") b["tiefe_ab_mm"] = wert(T->tiefe_ab_mm);
            else b.erase("tiefe_ab_mm");
        }
        b["aufmass"] = wert(T->aufmass);
        b["gleichlauf"] = T->richtung->get_active_id() == "1";
        b["dogbone"] = T->dogbone->get_active();
        b["v_wand"] = T->vwand->get_active_id().raw();
        b["werkzeug"] = T->werkzeug->get_active_id().raw();
        b["zustellung"] = wert(T->zust);
        T->zart->lesen_in(b);
        b["strategie"] = T->strategie->get_active_id().raw();
        b["bahnabstand"] = (int)wert(T->bahn);
        b["adaptiv_last"] = wert(T->alast);
        // Peter 01.11.: Gleich- und Gegenlauf im Wechsel ist Standard (fehlt = an) – ein früher gespeichertes
        // «aus» fällt beim Übernehmen weg; das alte adaptive Räumen gibt es nicht mehr
        b.erase("adaptiv_wechsel");
        b["eintauchen_art"] = T->eintauchen->get_active_id().raw();
        b["rampe_winkel"] = wert(T->rwinkel);
        // Vorschub in mm/min (nichts = wie das Werkzeug); alte Prozentwerte weg
        b["vorschub_mm"] = T->v_haupt->wert_json();
        b["vollnut_mm"] = T->vn_haupt->wert_json();
        b.erase("vorschub_prozent");
        b.erase("vollnut");
        b["raeum_werkzeug"] = T->raeumen ? T->r_werkzeug->get_active_id().raw() : std::string();
        b["raeum_zustellung"] = wert(T->r_zust);
        b["raeum_strategie"] = T->r_strategie->get_active_id().raw();
        b["raeum_bahnabstand"] = (int)wert(T->r_bahn);
        b["raeum_adaptiv_last"] = wert(T->r_alast);
        b["raeum_eintauchen_art"] = T->r_eintauchen->get_active_id().raw();
        b["raeum_rampe_winkel"] = wert(T->r_rwinkel);
        b["raeum_vorschub_mm"] = T->v_raeum->wert_json();
        b["raeum_vollnut_mm"] = T->vn_raeum->wert_json();
        b.erase("raeum_vorschub_prozent");
        b.erase("raeum_vollnut");
        b["schlichten"] = T->schlichten;
        b["schlicht_werkzeug"] = T->s_werkzeug->get_active_id().raw();
        b["schlicht_zugabe"] = wert(T->s_zugabe);
        if (T->s_zust_ganz >= 0 && std::abs(wert(T->s_zust) - T->s_zust_ganz) < 1e-9) b["schlicht_zustellung"] = nullptr;
        else b["schlicht_zustellung"] = wert(T->s_zust);
        if (auto r = T->s_richtung->get_active_id().raw(); r.empty()) b.erase("schlicht_gleichlauf");
        else b["schlicht_gleichlauf"] = r == "1";
        b["schlicht_vorschub_mm"] = T->v_schl->wert_json();
        b.erase("schlicht_vorschub");
        if (T->vfaktor) {
            double vf = wert(T->vfaktor);
            if (std::abs(vf - 100) < 0.5) b.erase("vorschub_faktor");
            else b["vorschub_faktor"] = (int)std::lround(vf);
        }
    };
    live->auslesen = auslesen;
    // keine Rechnung bei jeder Änderung: Liste der Änderungen und grob nachgeführte Zeit
    auto start = std::make_shared<bj::object>();
    auto minuten_alt = std::make_shared<double>(-1);
    if (ergebnis_aktuell) {
        if (auto sts = unter(ergebnis, "bearbeitungen")) {
            if (auto st = unter(*sts, id.c_str())) *minuten_alt = num(*st, "minuten", 0);
        }
    }
    T->neu.push_back([this, T, b, start, aend_box, aend_zeit, auslesen, minuten_alt, vorg_box] {
        if (T->baut) return;
        bj::object jetzt = b;
        auslesen(jetzt);
        if (start->empty()) *start = jetzt;
        int n = aenderungen_zeigen(aend_box, *start, jetzt);
        vorgaben_zeigen(vorg_box, "tasche", jetzt);
        std::string zt;
        if (*minuten_alt > 0) {
            auto gaenge = [](bj::object const &o) {
                double t = num(o, "tiefe", 1), z = num(o, "zustellung", 1);
                return z > 1e-6 ? std::max(1.0, std::ceil(t / z - 1e-9)) : 1.0;
            };
            double m = *minuten_alt * gaenge(jetzt) / gaenge(*start);
            double va = num(*start, "vorschub_mm", 0), vj = num(jetzt, "vorschub_mm", 0);
            if (va > 0 && vj > 0) m *= va / vj;
            m *= std::max(5.0, num(*start, "bahnabstand", 40)) / std::max(5.0, num(jetzt, "bahnabstand", 40));
            zt = n == 0 ? TR("Zeit: ") + zeit(*minuten_alt)
                        : TR("Zeit geschätzt: ") + zeit(*minuten_alt) + " → " + zeit(m);
        }
        aend_zeit->set_text(zt);
    });

    dlg->add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
    if (entfernbar(b)) dlg->add_button(TR("Entfernen"), 1);
    if (auto sel = panel.getSelection(); sel && !sel->isEmpty()) T->zuweisen = dlg->add_button(TR("Auswahl zuweisen"), 2);
    T->ok = dlg->add_button(TR("Übernehmen"), Gtk::RESPONSE_OK);
    klasse(*T->ok, "suggested-action");
    dlg->set_default_response(Gtk::RESPONSE_OK);
    hirch_werkzeug_pflicht(*dlg, *T->werkzeug);
    for (auto wbtn : werkzeug_knoepfe) {
        wbtn->signal_clicked().connect([this, T] {
            auto a = T->werkzeug->get_active_id().raw(), r = T->r_werkzeug->get_active_id().raw(),
                 s = T->s_werkzeug->get_active_id().raw();
            werkzeuge_dialog();
            T->baut = true;
            werkzeug_combo(*T->werkzeug, a, false);
            werkzeug_combo(*T->r_werkzeug, r, false);
            werkzeug_combo(*T->s_werkzeug, s, false);
            T->s_werkzeug->prepend("", TR("gleiches wie die Tasche"));
            T->s_werkzeug->set_active_id(s);
            T->baut = false;
            T->alle();
        });
    }
    dlg->show_all();
    rad_schuetzen(*dlg->get_content_area());
    T->baut = false;
    T->alle();
    if (T->wahl != "haupt") T->hinrollen(T->wahl);
    int antwort = dlg->run();
    dialog_antwort = antwort;
    live->lebt = false;
    live->timer.disconnect();
    if (antwort == 1) {
        entfernen(index);
        return;
    }
    if (T->ungueltig) {
        dialog_antwort = Gtk::RESPONSE_CANCEL;
        return;
    }
    if (antwort != Gtk::RESPONSE_OK && antwort != 2) return;
    if (antwort == 2) zuweisen(index);
    auto titel = T->titel->get_text().raw();
    while (!titel.empty() && std::isspace((unsigned char)titel.back())) titel.pop_back();
    while (!titel.empty() && std::isspace((unsigned char)titel.front())) titel.erase(0, 1);
    bj::object vorher = b;
    b["titel"] = titel;
    auslesen(b);
    if (auto bea2 = liste_von(einst, "bearbeitungen"); bea2 && index < (int)bea2->size()) {
        (*bea2)[index] = b;
        schreiben(aenderung_text(name_von(vorher), vorher, b));      // genaue Bearbeitungshistorie
    }
}

void HirchFraesen::Impl::bearbeitung_dialog(int index, std::string const &teil)
{
    auto bea = liste_von(einst, "bearbeitungen");
    if (!bea || index < 0 || index >= (int)bea->size()) return;
    auto bp = (*bea)[index].if_object();
    if (!bp) return;
    bj::object b = *bp;
    auto art = str(b, "art");
    auto info = art_info(art);
    if (!info) return;
    wz_kontext = art;               // Werkzeuglisten: was zu dieser Fräsart passt, steht oben
    auto id = str(b, "id");
    long n = objekte(id);
    double dicke = 18;
    if (auto m = unter(analyse, "material")) dicke = num(*m, "dicke", 18);
    if (art == "tasche") {          // eigener Dialog: Schritte, Karten, Vorschau
        tasche_dialog(index, b, teil);
        return;
    }
    if (ist_relief(art)) {          // 3D-Fräsung: eigener Dialog (hirch-3d.cpp)
        relief_dialog(index, b);
        return;
    }
    if (ist_boeden_waende(art)) {   // Böden und Wände aus dem 3D-Modell (hirch-3d.cpp)
        boeden_waende_dialog(index, b);
        return;
    }
    bool hat_bahnen = art == "tasche" || art == "kontur";

    Felder f;                       // vor dem Dialog: wird nach ihm abgebaut
    auto F = &f;
    // Haltestege (Entwurf B): immer gelbe Marken; «keine» oder «manuell», «auto» nur noch für alte Zeichnungen
    struct StegZustand {
        std::string modus;
        int anzahl = 0;             // so verteilt (für «Übernehmen»: neu verteilen, wenn geändert)
        double breite = 0;
        bool laeuft = false, durch = false;
        Gtk::Label *status = nullptr;
        Gtk::Button *verteilen = nullptr, *keine = nullptr;
        std::string hinweis;
        std::function<void(bj::object &)> auslesen;
        std::shared_ptr<bool> lebt = std::make_shared<bool>(true);
    };
    std::shared_ptr<StegZustand> sst;
    std::shared_ptr<VorschubFeld> v_haupt, vn_haupt, v_schl;   // Vorschub in mm/min
    Gtk::SpinButton *vfaktor = nullptr;                          // Faktor aus der Zeitleiste
    auto dlg = dialog(TR("Bearbeitung ") + name_von(b));
    dlg->set_resizable(true);
    dlg->set_default_size(1240, 760);
    auto box = dlg->get_content_area();
    box->set_spacing(0);
    box->set_margin_start(0);
    box->set_margin_end(0);
    box->set_margin_top(0);

    auto kopf = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 12);
    kopf->set_margin_start(20);
    kopf->set_margin_end(20);
    kopf->set_margin_top(12);
    kopf->set_margin_bottom(12);
    kopf->add(*farbfeld(info, 16, str(b, "farbe")));
    auto kt = label("");
    kt->set_markup("<big><b>" + markup_escape(name_von(b)) + "</b></big>");
    kopf->add(*kt);
    kopf->add(*label(ohne_objekte(art) ? std::string(TR("ganze Materialoberfläche"))
                                       : std::string(TR(info->name)) + " · " + anzahl(n, TR("Objekt"), TR("Objekte")) +
                                             TR(" in diesem Farbton"),
                     "hirch-dim"));
    box->add(*kopf);
    box->add(*Gtk::make_managed<Gtk::Separator>());

    // Alles auf einer Seite: oben Sprungleiste und Suche, in der Mitte alle Abschnitte
    // untereinander, rechts die Sofort-Vorschau mit den Änderungen
    auto haupt = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
    haupt->set_vexpand(true);
    box->add(*haupt);
    auto mitte = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 0);
    mitte->set_hexpand(true);
    haupt->add(*mitte);
    auto leiste = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
    leiste->set_margin_start(18);
    leiste->set_margin_end(18);
    leiste->set_margin_top(10);
    leiste->set_margin_bottom(10);
    auto sprung = Gtk::make_managed<Gtk::FlowBox>();
    sprung->set_selection_mode(Gtk::SELECTION_NONE);
    sprung->set_max_children_per_line(8);
    sprung->set_column_spacing(4);
    sprung->set_row_spacing(4);
    sprung->set_hexpand(true);
    sprung->set_homogeneous(false);
    leiste->add(*sprung);
    auto suche = Gtk::make_managed<Gtk::SearchEntry>();
    suche->set_placeholder_text(TR("Einstellung suchen"));
    suche->set_width_chars(22);
    suche->set_valign(Gtk::ALIGN_START);
    leiste->add(*suche);
    // Peter, 20.10.: nur ein Weg zu jeder Einstellung – keine Sprungleiste und keine Suche mehr,
    // alle Abschnitte stehen untereinander auf einer Seite
    leiste->set_no_show_all(true);       // bleibt im Dialog (unsichtbar), damit alles mit ihm abgebaut wird
    mitte->add(*leiste);
    auto rollen = Gtk::make_managed<Gtk::ScrolledWindow>();
    rollen->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
    rollen->set_vexpand(true);
    mitte->add(*rollen);
    auto alles = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
    alles->set_margin_start(22);
    alles->set_margin_end(22);
    alles->set_margin_top(8);
    alles->set_margin_bottom(22);
    rollen->add(*alles);
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
    struct Abschnitt { std::string titel; Gtk::Box *rahmen; Gtk::Button *chip; };
    auto schritte = std::make_shared<std::vector<Abschnitt>>();
    // zum Abschnitt rollen
    auto zeige_schritt = [rollen, alles, schritte](std::string const &name) {
        for (auto &a : *schritte) {
            if (a.titel != name) continue;
            int x = 0, y = 0;
            if (a.rahmen->translate_coordinates(*alles, 0, 0, x, y)) {
                auto adj = rollen->get_vadjustment();
                adj->set_value(std::min<double>(std::max(0, y - 6), adj->get_upper() - adj->get_page_size()));
            }
        }
    };
    // Titel der Abschnitte
    static std::map<std::string, std::string> const SCHRITT = {
        {TR("Allgemein"), TR("Werkzeug und Tiefe")},
        {TR("Kontur"), TR("Seite und Aufmass")},
        {TR("Haltestege"), TR("Haltestege")},
        {TR("Bahnen"), TR("Bahnen")},
        {TR("Eintauchen"), TR("Eintauchen und Vorschub")},
        {TR("Vorschub"), TR("Vorschub")},
        {TR("Schlichten"), TR("Schlichten")}};
    auto reiter = [alles, sprung, schritte, zeige_schritt](std::string const &titel) {
        auto it = SCHRITT.find(titel);
        std::string t = it != SCHRITT.end() ? it->second : titel;
        auto rahmen = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
        rahmen->set_margin_top(10);
        auto tl = label(t);
        klasse(*tl, "hirch-seitentitel");
        rahmen->add(*tl);
        auto s = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
        klasse(*s, "hirch-karte2");
        rahmen->add(*s);
        alles->add(*rahmen);
        auto chip = Gtk::make_managed<Gtk::Button>(t);
        klasse(*chip, "hirch-sprung");
        chip->signal_clicked().connect([zeige_schritt, titel] { zeige_schritt(titel); });
        sprung->add(*chip);
        schritte->push_back({titel, rahmen, chip});
        return s;
    };
    // Suche: Abschnitte, in denen der Text vorkommt (Titel, Beschriftungen, Auswahllisten)
    std::function<void(Gtk::Widget *, std::string &)> texte = [&texte](Gtk::Widget *w, std::string &aus) {
        if (auto l = dynamic_cast<Gtk::Label *>(w)) aus += " " + l->get_text().raw();
        if (auto c = dynamic_cast<Gtk::ComboBoxText *>(w)) {
            if (auto m = c->get_model()) {
                for (auto const &row : m->children()) {
                    Glib::ustring t;
                    row.get_value(0, t);
                    aus += " " + t.raw();
                }
            }
        }
        if (auto b = dynamic_cast<Gtk::Button *>(w)) aus += " " + b->get_label().raw();
        if (auto c = dynamic_cast<Gtk::Container *>(w))
            for (auto k : c->get_children()) texte(k, aus);
    };
    suche->signal_search_changed().connect([suche, schritte, texte] {
        auto q = suche->get_text().lowercase().raw();
        for (auto &a : *schritte) {
            std::string t;
            texte(a.rahmen, t);
            bool an = q.empty() || Glib::ustring(t).lowercase().raw().find(q) != std::string::npos;
            a.rahmen->set_visible(an);
            a.chip->set_visible(an);
        }
    });
    auto beobachten = [F](Gtk::Widget *w) {
        if (auto s = dynamic_cast<Gtk::SpinButton *>(w)) s->signal_value_changed().connect([F] { F->alle(); });
        else if (auto c = dynamic_cast<Gtk::ComboBoxText *>(w)) c->signal_changed().connect([F] { F->alle(); });
        else if (auto k = dynamic_cast<Gtk::CheckButton *>(w)) k->signal_toggled().connect([F] { F->alle(); });
    };
    auto wz_id = [F] { return F->werkzeug->get_active_id().raw(); };
    // Bohrer mit «Spitze dazu»: so viel tiefer bohrt er (für die Zahl der Hübe)
    auto spitze_extra = [this, F, wz_id]() -> double {
        if (!F->spitze || !F->spitze->get_active()) return 0.0;
        auto w = wirksam(wz_id());
        if (str(w, "typ") != "bohrer") return 0.0;
        return num(w, "durchmesser", 3) / 2 / std::tan(std::clamp(num(w, "spitzenwinkel", 118), 30.0, 179.0) * M_PI / 360.0);
    };
    auto wz_d = [this, wz_id] { return num(wirksam(wz_id()), "durchmesser", 6); };
    // Breite, mit der der Boden ausgeräumt wird: V-Fräser / Gravierstichel nur mit der Spitze
    auto raeum_d = [this, wz_id] {
        auto w = wirksam(wz_id());
        if (str(w, "typ") == "v" && num(w, "spitze", 0) > 0) return num(w, "spitze", 0);
        return num(w, "durchmesser", 6);
    };
    auto ist_v = [this, wz_id] { return str(wirksam(wz_id()), "typ") == "v"; };

    // ================================================= Allgemein
    auto s1 = reiter(TR("Allgemein"));
    auto g0 = formular();
    auto nl = label("Name");
    nl->set_hexpand(true);
    g0->attach(*nl, 0, 0);
    f.titel = Gtk::make_managed<Gtk::Entry>();
    f.titel->set_text(str(b, "titel"));
    f.titel->set_placeholder_text(TR(info->name) + std::string(TR(" (automatisch)")));
    f.titel->set_activates_default(true);
    f.titel->set_hexpand(true);
    g0->attach(*f.titel, 1, 0, 2);
    s1->add(*g0);

    s1->add(*abschnitt(TR("Werkzeug")));
    auto wbox = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    f.werkzeug = Gtk::make_managed<Gtk::ComboBoxText>();
    f.werkzeug->set_hexpand(true);
    werkzeug_combo(*f.werkzeug, str(b, "werkzeug", "A"), false, art == "bohren");
    wbox->add(*f.werkzeug);
    auto wbtn = Gtk::make_managed<Gtk::Button>(TR("Werkzeuge …"));   // nicht mehr gezeigt (Peter, 20.10.)
    s1->add(*wbox);
    auto wdetail = label("", "hirch-dim");
    klasse(*wdetail, "hirch-klein");
    s1->add(*wdetail);
    // neue Bearbeitung (Einfärben, «+ Neu»): das Werkzeug ist erst ein Vorschlag (Peter, 22.10.)
    std::string vorschlag = neu_offen ? str(b, "werkzeug", "A") : std::string();
    f.neu.push_back([this, wdetail, wz_id, vorschlag] {
        auto w = wirksam(wz_id());
        std::string t = w.empty() ? "" : werkzeug_details(w);
        if (!vorschlag.empty() && wz_id() == vorschlag)
            t = TR("Vorschlag – passt er? Klick ins Feld öffnet die Werkzeugwahl.") + std::string("\n") + t;
        wdetail->set_text(t);
    });
    beobachten(f.werkzeug);

    auto grid = formular();
    int z = 0;
    s1->add(*abschnitt(art == "vcarve" ? TR("Tiefe") : TR("Tiefe und Zustellung")));
    if (art == "vcarve") {
        f.maxt = spin_zeile(*grid, z++, TR("Maximale Tiefe"), num(b, "max_tiefe", 5), 0.1, 50, 0.1, 2, "mm");
        // Peter, 23.10.: auch auf einer tieferen Stufe (wie Kontur und Tasche)
        f.starttiefe = spin_zeile(*grid, z++, TR("Beginnt bei Tiefe"), num(b, "starttiefe", 0), 0, 200, 0.1, 2,
                                  TR("mm · 0 = an der Oberfläche"));
        f.starttiefe->set_tooltip_text(TR("Erst ab dieser Tiefe fräsen, z. B. wenn eine Tasche oder das "
                                       "vorbearbeitete Werkstück oben schon frei ist. Zugestellt wird nur zwischen "
                                       "dieser Tiefe und der Endtiefe; bis knapp darüber im Eilgang."));
        beobachten(f.starttiefe);
        double vz0 = num(b, "zustellung", 0);
        f.vzust = spin_zeile(*grid, z++, TR("Zustellung je Durchgang"), vz0 > 1e-9 ? vz0 : std::max(0.1, num(b, "max_tiefe", 5) / 2),
                             0.01, 50, 0.05, 2, "mm");
        f.vzust->set_tooltip_text(TR("Tiefe je Durchgang. Bei mehreren Durchgängen fräst der Stichel Ebene für "
                                  "Ebene, die letzte Ebene ist die fertige Form."));
        f.zart = zustell_art_zeilen(*grid, z, b, f.vzust, true);
        z += 3;
        f.dick = spin_zeile(*grid, z++, TR("Aufdicken"), num(b, "aufdicken", 0), -2, 2, 0.01, 2, TR("mm je Seite"));
        f.dick->set_tooltip_text(TR("Jede Form ringsum um dieses Mass grösser fräsen (negativ: dünner). "
                                 "+0,05 macht einen Strich 0,1 mm breiter. Breitere Striche brauchen mehr "
                                 "Tiefe – die maximale Tiefe muss dafür reichen."));
        beobachten(f.vzust);
        beobachten(f.dick);
        // Ab welcher Strichbreite die maximale Tiefe einen flachen Boden ergibt
        auto flach_info = label("", "hirch-dim");
        klasse(*flach_info, "hirch-klein");
        flach_info->set_line_wrap(true);
        flach_info->set_xalign(0);
        grid->attach(*flach_info, 0, z++, 3);
        f.neu.push_back([this, F, flach_info, wz_id] {
            auto w = wirksam(wz_id());
            if (str(w, "typ") != "v") {
                flach_info->set_text("");
                return;
            }
            double winkel = num(w, "v_winkel", 60), spitze = num(w, "spitze", 0), t = F->maxt->get_value();
            double breite = 2 * t * std::tan(winkel * M_PI / 360.0) + spitze;
            flach_info->set_text(TRF("Striche bis {} mm Breite werden reine V-Rillen; breitere bekommen auf {} mm "
                                     "einen flachen Boden.", {zahl(breite, 2), zahl(t, 2)}));
        });
        auto fl = label(TR("Boden ab max. Tiefe ausräumen mit"));
        grid->attach(*fl, 0, z++, 3);
        f.flach = Gtk::make_managed<Gtk::ComboBoxText>();
        werkzeug_combo(*f.flach, str(b, "flach_werkzeug", ""), true);
        grid->attach(*f.flach, 0, z++, 3);
        grid->attach(*hinweis_label(TR("Die Tiefe ergibt sich aus der Breite der Form: schmale Stellen bleiben "
                                    "flach, breite werden tiefer – bis zur maximalen Tiefe.")), 0, z++, 3);
        // Schnitt durch die Rille bei maximaler Tiefe
        auto sk = skizze(96, [this, F](Cr const &cr, double w, double h, Gdk::RGBA const &fg) {
            auto wz = wirksam(F->werkzeug->get_active_id().raw());
            double winkel = str(wz, "typ") == "v" ? num(wz, "v_winkel", 60) : 60;
            double spitze = num(wz, "spitze", 0);
            double tiefe = F->maxt->get_value();
            double breite = 2 * tiefe * std::tan(winkel * M_PI / 360.0) + spitze;
            double oben = 30, unten = h - 10;
            double skala = std::min((unten - oben) / std::max(tiefe, 0.1), (w * 0.45) / std::max(breite, 0.1));
            double bw = breite * skala, bt = tiefe * skala, sp = spitze * skala, mx = w * 0.4;
            farbe(cr, HOLZ);
            cr->rectangle(1, oben, w - 2, h - oben - 1);
            cr->fill();
            cr->move_to(mx - bw / 2, oben);
            cr->line_to(mx - sp / 2, oben + bt);
            cr->line_to(mx + sp / 2, oben + bt);
            cr->line_to(mx + bw / 2, oben);
            cr->close_path();
            cr->set_source_rgb(1, 1, 1);
            cr->fill_preserve();
            farbe(cr, HOLZ_RAND);
            cr->set_line_width(1.5);
            cr->stroke();
            schrift(cr, fg, mx - bw / 2, oben - 8, zahl(breite, 1) + TR(" mm breit"));
            cr->set_source_rgb(0.2, 0.2, 0.2);
            cr->move_to(mx + bw / 2 + 12, oben + bt * 0.6);
            cr->show_text(einheit_text_zahlen(zahl(tiefe, 1) + TR(" mm tief (") + zahl(winkel) + "°)"));
        });
        f.skizzen.push_back(sk);
        grid->attach(*sk, 0, z++, 3);
        beobachten(f.maxt);
    } else if (art == "kontur") {
        f.zweck = combo_zeile(*grid, z++, TR("Zweck"));
        f.zweck->append("", TR("Ausschneiden / Kontur fräsen"));
        f.zweck->append("fase", TR("Fase – Kante mit dem V-Fräser brechen"));
        f.zweck->set_active_id(flag(b, "fase", false) ? "fase" : "");
        f.zweck->set_tooltip_text(TR("Fase: der V-Fräser fährt neben der Kante entlang und bricht sie in der "
                                  "eingestellten Breite. Tiefe und Abstand rechnet Hirch CAM aus dem "
                                  "Spitzenwinkel."));
        f.fbreite = spin_zeile(*grid, z++, TR("Fasenbreite"), num(b, "fase_breite", 0.5), 0.05, 20, 0.05, 2, "mm");
        beobachten(f.zweck);
        beobachten(f.fbreite);
        f.tiefe = spin_zeile(*grid, z++, TR("Tiefe"), ist_null(b, "tiefe") ? dicke + 0.2 : num(b, "tiefe", 6),
                             0.1, 200, 0.1, 2, "mm");
        f.durch = Gtk::make_managed<Gtk::CheckButton>(TR("Ganz durch – folgt der Materialdicke (") +
                                                      einheit_zahl(dicke, 2, Groesse::LAENGE, false) +
                                                      TR(" + 0,2 mm)"));
        f.durch->set_active(ist_null(b, "tiefe"));
        grid->attach(*f.durch, 0, z++, 3);
        f.starttiefe = spin_zeile(*grid, z++, TR("Beginnt bei Tiefe"), num(b, "starttiefe", 0), 0, 200, 0.1, 2,
                                  TR("mm · 0 = an der Oberfläche"));
        f.starttiefe->set_tooltip_text(TR("Erst ab dieser Tiefe fräsen, z. B. wenn eine Tasche oder das "
                                       "vorbearbeitete Werkstück oben schon frei ist. Zugestellt wird nur zwischen "
                                       "dieser Tiefe und der Endtiefe; bis knapp darüber im Eilgang."));
        beobachten(f.starttiefe);
        f.zust = spin_zeile(*grid, z++, TR("Zustellung je Durchgang"), num(b, "zustellung", 2), 0.1, 50, 0.1, 2, "mm");
        f.zart = zustell_art_zeilen(*grid, z, b, f.zust, false);
        z += 3;
        auto setzt = std::make_shared<bool>(false);
        f.durch->signal_toggled().connect([F, dicke, setzt] {
            if (F->durch->get_active()) {
                *setzt = true;
                F->tiefe->set_value(dicke + 0.2);
                *setzt = false;
            }
        });
        // Eine eigene Tiefe eintippen schaltet «ganz durch» aus
        f.tiefe->signal_value_changed().connect([F, dicke, setzt] {
            if (!*setzt && std::abs(F->tiefe->get_value() - (dicke + 0.2)) > 1e-6) F->durch->set_active(false);
        });
        beobachten(f.tiefe);
        beobachten(f.zust);
        beobachten(f.durch);
        f.neu.push_back([F] {
            bool fase = F->zweck->get_active_id() == "fase";
            zeile_zeigen(F->fbreite, fase);
            zeile_zeigen(F->tiefe, !fase);
            zeile_zeigen(F->durch, !fase);
            zeile_zeigen(F->starttiefe, true);      // Peter, 23.10.: Fase an einer tieferen Kante
        });
    } else if (art == "gewinde") {
        f.gart = combo_zeile(*grid, z++, TR("Gewinde"));
        f.gart->append("innen", TR("Innengewinde (Mutter, Loch)"));
        f.gart->append("aussen", TR("Aussengewinde (Bolzen, Zapfen)"));
        f.gart->set_active_id(flag(b, "innen", true) ? "innen" : "aussen");
        f.tiefe = spin_zeile(*grid, z++, TR("Gewindelänge"), num(b, "tiefe", 8), 0.5, 200, 0.5, 2, "mm");
        // Steigung als Wahl statt «0 = Regelgewinde» (Peter, 20.10.)
        f.gsteig_art = combo_zeile(*grid, z++, TR("Steigung"));
        f.gsteig_art->append("regel", TR("Regelgewinde – aus dem Ø der Kreise (M6 = 1,0 mm)"));
        f.gsteig_art->append("eigen", TR("Feingewinde – eigene Steigung"));
        f.gsteig_art->set_active_id(ist_null(b, "steigung") || num(b, "steigung", 0) <= 1e-9 ? "regel" : "eigen");
        f.gsteig = spin_zeile(*grid, z++, TR("Eigene Steigung"),
                              ist_null(b, "steigung") || num(b, "steigung", 0) <= 1e-9 ? 1.0 : num(b, "steigung", 1), 0.1, 6,
                              0.05, 2, TR("mm je Umdrehung"));
        f.gdreh = combo_zeile(*grid, z++, TR("Drehsinn"));
        f.gdreh->append("rechts", TR("Rechtsgewinde (normal)"));
        f.gdreh->append("links", TR("Linksgewinde"));
        f.gdreh->set_active_id(flag(b, "rechts", true) ? "rechts" : "links");
        f.gdurch = spin_zeile(*grid, z++, TR("Durchgänge"), num(b, "durchgaenge", 1), 1, 6, 1, 0,
                              TR("(radial aufgeteilt)"));
        f.gpass = spin_zeile(*grid, z++, TR("Passung"), num(b, "passung", 0), -0.5, 0.5, 0.01, 2,
                             TR("mm (+ = lockerer)"));
        f.gkern = combo_zeile(*grid, z++, TR("Kernloch"));
        werkzeug_combo(*f.gkern, str(b, "kernloch_werkzeug", ""), false, true);
        f.gkern->prepend("", TR("schon gebohrt – nicht fräsen"));
        f.gkern->set_active_id(str(b, "kernloch_werkzeug", ""));
        f.gkern->set_tooltip_text(TR("Innengewinde: vorher das Kernloch mit diesem Fräser als Helix fräsen – in "
                                  "derselben Datei, vor dem Gewinde. Ist der Kreis schon das Kernloch (Ø 10,2 für "
                                  "M12), bleibt es so gross wie gezeichnet."));
        // Anleitung: so entsteht dieses Gewinde – je gezeichnetem Ø, live mit Werkzeug und Kernloch
        auto anleitung = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);
        klasse(*anleitung, "hirch-karte");
        anleitung->set_margin_top(6);
        grid->attach(*anleitung, 0, z++, 3);
        std::vector<double> kreise;
        if (auto gk = unter(analyse, "gewinde_kreise")) {
            if (auto a = gk->if_contains(id); a && a->is_array()) {
                for (auto const &v : a->as_array())
                    if (v.is_number()) kreise.push_back(v.to_number<double>());
            }
        }
        f.neu.push_back([this, F, anleitung, kreise, dicke, wz_id] {
            UI::remove_all_children(*anleitung);
            bool innen = F->gart->get_active_id() == "innen";
            bool eigen = F->gsteig_art->get_active_id() == "eigen";
            zeile_zeigen(F->gkern, innen);
            zeile_zeigen(F->gsteig, eigen);
            auto w = wirksam(wz_id());
            double dw = num(w, "durchmesser", 0);
            bool ist_gf = str(w, "typ") == "gewinde";
            F->ungueltig = !ist_gf;
            auto zeile = [anleitung](std::string const &nr, std::string const &t, char const *k = nullptr) {
                auto l = label(nr.empty() ? t : nr + "  " + t, k ? k : "");
                l->set_line_wrap(true);
                l->set_max_width_chars(70);
                l->set_xalign(0);
                anleitung->add(*l);
            };
            zeile("", TR("SO ENTSTEHT DAS GEWINDE"), "hirch-kopf");
            if (kreise.empty()) {
                zeile("1", TR("Kreis mit dem Nenn-Ø (Ø 6 für M6) oder dem Kernloch (Ø 5 für M6) zeichnen und türkis "
                              "einfärben."));
            } else {
                std::string t;
                for (auto d : kreise) t += (t.empty() ? "" : ", ") + std::string("Ø ") + einheit_mass(d, zahl(d, 1));
                zeile("1", TRF("Gezeichnet: {} – je Gewinde ein Kreis mit dem Nenn-Ø oder dem Kernloch.", {t}));
            }
            double laenge = F->tiefe->get_value();
            for (auto d : kreise.empty() ? std::vector<double>{6.0} : kreise) {
                double p = eigen ? F->gsteig->get_value() : regelsteigung(d);
                // Kreis = Kernloch? (Innengewinde, kein Nenn-Ø): Nenn-Ø und Steigung daraus, das Loch bleibt
                double dk = d;                     // gezeichneter Kreis
                bool als_kernloch = false;
                if (innen && (p <= 0 || (eigen && regelsteigung(d) <= 0))) {
                    double q = eigen ? F->gsteig->get_value() : 0.0;
                    if (double n = gewinde_nenn_aus_kernloch(d, q); n > 0) {
                        d = n;
                        p = q;
                        als_kernloch = true;
                    }
                }
                std::string name = gewinde_name(d, p);
                if (p <= 0) {
                    zeile("", TRF("⚠ Ø {} ist kein Regelgewinde – «Feingewinde – eigene Steigung» wählen.", {zahl(d, 1)}),
                          "hirch-warn");
                    F->ungueltig = true;
                    continue;
                }
                double kern = als_kernloch ? dk : innen ? d - 1.0825 * p : d - 1.2269 * p;
                if (innen) {
                    auto k = F->gkern->get_active_id().raw();
                    zeile("2", k.empty() ? TRF("{}: Kernloch Ø {} mm – schon gebohrt (kein Werkzeug).", {name, zahl(kern, 2)})
                                         : TRF("{}: Kernloch Ø {} mm – vorher gefräst mit {}.",
                                               {name, zahl(kern, 2), werkzeug_name(wirksam(k))}));
                    bool passt = dw > 0 && dw < kern - 0.05;
                    if (!ist_gf) {
                        zeile("3", TR("⚠ Das Werkzeug ist kein Gewindefräser – so entsteht kein Gewinde."), "hirch-fehler");
                    } else {
                        zeile("3", passt ? TRF("Gewindefräser Ø {} ist kleiner als das Kernloch ✓", {zahl(dw, 1)})
                                         : TRF("⚠ Gewindefräser Ø {} passt nicht ins Kernloch Ø {} – für {} einen "
                                               "kleineren wählen.", {zahl(dw, 1), zahl(kern, 2), name}),
                              passt ? nullptr : "hirch-fehler");
                        if (!passt) F->ungueltig = true;
                    }
                } else {
                    zeile("2", TRF("{}: Zapfen Ø {} mm stehen lassen, Kern Ø {} mm.", {name, zahl(d, 1), zahl(kern, 2)}));
                    if (!ist_gf) zeile("3", TR("⚠ Das Werkzeug ist kein Gewindefräser – so entsteht kein Gewinde."),
                                       "hirch-fehler");
                }
                if (laenge < d - 1e-6)
                    zeile("4", TRF("Gewindelänge {} mm ist kürzer als 1 × Ø – hält weniger; besser {}–{} mm.",
                                   {zahl(laenge, 1), zahl(d, 1), zahl(1.5 * d, 1)}), "hirch-warn");
                else
                    zeile("4", TRF("Gewindelänge {} mm ≥ 1 × Ø ✓", {zahl(laenge, 1)}));
                if (laenge > dicke + 1e-6)
                    zeile("", TRF("⚠ Länger als das Material ({} mm).", {zahl(dicke, 1)}), "hirch-warn");
            }
            zeile("", TRF("Der Fräser taucht ein und fährt schraubenförmig nach oben – je Umdrehung {} höher.",
                          {eigen ? zahl(F->gsteig->get_value()) + " mm" : std::string(TR("um die Steigung"))}),
                  "hirch-dim");
            anleitung->show_all();
        });
        for (Gtk::Widget *w : std::initializer_list<Gtk::Widget *>{f.gart, f.tiefe, f.gsteig, f.gsteig_art, f.gdreh,
                                                                   f.gdurch, f.gpass, f.gkern}) beobachten(w);
    } else {
        if (art == "bohren") {
            f.methode = combo_zeile(*grid, z++, TR("Methode"));
            f.methode->append("auto", TR("Automatisch (grössere Kreise als Helix)"));
            f.methode->append("bohren", TR("Bohren (nur in der Mitte)"));
            f.methode->append("helix", TR("Helix (Loch auf Durchmesser fräsen)"));
            f.methode->set_active_id(str(b, "methode", "bohren"));
            f.methode->set_tooltip_text(TR("Helix: der Fräser fährt schraubenförmig am Rand der gezeichneten Form "
                                        "nach unten – so entstehen Löcher, die grösser sind als der Fräser."));
            beobachten(f.methode);
        }
        f.tiefe = spin_zeile(*grid, z++, TR("Tiefe"), num(b, "tiefe", 1), 0.05, 200, 0.1, 2, "mm");
        if (art == "gravur") {
            // Peter, 23.10.: Schrift auch auf einer tieferen Stufe (wie Kontur und Tasche)
            f.starttiefe = spin_zeile(*grid, z++, TR("Beginnt bei Tiefe"), num(b, "starttiefe", 0), 0, 200, 0.1, 2,
                                      TR("mm · 0 = an der Oberfläche"));
            f.starttiefe->set_tooltip_text(TR("Erst ab dieser Tiefe fräsen, z. B. wenn eine Tasche oder das "
                                           "vorbearbeitete Werkstück oben schon frei ist. Zugestellt wird nur zwischen "
                                           "dieser Tiefe und der Endtiefe; bis knapp darüber im Eilgang."));
            beobachten(f.starttiefe);
        }
        f.zust = spin_zeile(*grid, z++, art == "bohren" ? TR("Zustellung je Hub / Umdrehung") : TR("Zustellung je Durchgang"),
                            num(b, "zustellung", 1), 0.05, 50, 0.1, 2, "mm");
        if (art == "gravur") {
            f.zart = zustell_art_zeilen(*grid, z, b, f.zust, false);
            z += 3;
        }
        beobachten(f.tiefe);
        beobachten(f.zust);
        if (art == "bohren") {
            f.richtung = combo_zeile(*grid, z++, TR("Fräsrichtung (Helix)"));
            f.spitze = Gtk::make_managed<Gtk::CheckButton>(TR("Spitze dazu – der volle Durchmesser reicht bis zur Tiefe"));
            f.spitze->set_active(flag(b, "spitze_dazu", true));
            f.spitze->set_tooltip_text(TR("Ein Bohrer hat eine kegelige Spitze. Ein: er bohrt um die Länge der Spitze "
                                       "tiefer, damit das Loch auf der ganzen Tiefe den vollen Durchmesser hat "
                                       "(Durchgangs- und Passlöcher). Aus: die Spitze endet auf der Tiefe."));
            grid->attach(*f.spitze, 0, z++, 3);
            beobachten(f.spitze);
            f.neu.push_back([this, F, wz_id] {
                auto w = wirksam(wz_id());
                bool bohrer = str(w, "typ") == "bohrer";
                // Bohrer: nur bohren – Methode und Fräsrichtung gibt es dann nicht
                zeile_zeigen(F->methode, !bohrer);
                zeile_zeigen(F->richtung, !bohrer && F->methode->get_active_id() != "bohren");
                zeile_zeigen(F->spitze, bohrer);
                if (bohrer) {
                    double d = num(w, "durchmesser", 3);
                    double wi = std::clamp(num(w, "spitzenwinkel", 118), 30.0, 179.0);
                    double l = d / 2 / std::tan(wi / 2 * M_PI / 180);
                    F->spitze->set_label(TRF("Spitze dazu – {} mm tiefer, damit der volle Durchmesser bis zur Tiefe reicht",
                                             {zahl(l, 2)}));
                }
            });
        }
    }
    // 3D: Tiefe ab der Oberfläche des 3D-Modells (nur wenn eines in der Zeichnung liegt)
    if ((art == "gravur" || art == "vcarve" || art == "kontur" || art == "bohren") &&
        (hirch_3d_hat_modell(doc()) || !str(b, "tiefe_ab").empty())) {
        // B-18 (Peter 02.11., Variante C): kurze Namen, Skizze und Satz darunter
        f.tiefe_ab = combo_zeile(*grid, z++, TR("Wie tief"));
        f.tiefe_ab->append("material", TR("flach, ab Materialoberfläche"));
        f.tiefe_ab->append("modell", TR("dem Modell folgen"));
        bool kontur = art == "kontur";
        if (kontur) {
            // Peter 01.11.: bis auf das 3D-Modell (Servoschacht) und Haut (B-15) – Rechnung: Fräsbahnen
            f.tiefe_ab->append("bis_modell", TR("bis aufs Modell hinunter"));
            f.tiefe_ab->append("haut", TR("Haut durchtrennen (zweiseitig)"));
        }
        auto ab0 = str(b, "tiefe_ab", "material");
        if (!kontur && ab0 != "modell") ab0 = "material";
        f.tiefe_ab->set_active_id(ab0);
        beobachten(f.tiefe_ab);
        auto tsk = skizze(84, [F](Cr const &cr, double w, double h, Gdk::RGBA const &fg) {
            skizze_tiefe_ab(cr, w, h, fg, F->tiefe_ab->get_active_id().raw());
        });
        f.skizzen.push_back(tsk);
        grid->attach(*tsk, 0, z++, 3);
        auto tsatz = label("", "hirch-dim");
        klasse(*tsatz, "hirch-klein");
        tsatz->set_xalign(0);
        tsatz->set_line_wrap(true);
        tsatz->set_max_width_chars(60);
        grid->attach(*tsatz, 0, z++, 3);
        f.neu.push_back([F, tsatz] { tsatz->set_text(tiefe_ab_satz(F->tiefe_ab->get_active_id().raw())); });
        if (kontur) {
            f.tiefe_ab_mm = spin_zeile(*grid, z++, TR("Abstand zum Modell"), num(b, "tiefe_ab_mm", 0), -50, 50, 0.1,
                                       2, TR("mm · + tiefer ins Modell, − Material bleibt stehen"));
            beobachten(f.tiefe_ab_mm);
            f.neu.push_back([F] {
                auto ab = F->tiefe_ab->get_active_id();
                bool bis = ab == "bis_modell", haut = ab == "haut";
                // Peter 02.11. 14:12: bei «Haut» Zustellung und Haltestege von Hand, Abstand einstellbar – nur die
                // Tiefe rechnet Hirch CNC
                zeile_zeigen(F->tiefe_ab_mm, bis || haut);
                if (F->tiefe) F->tiefe->set_sensitive(!haut);
                // Einheit neben «Tiefe»: bei «bis auf das 3D-Modell» ist die Tiefe die Grenze
                if (auto g = F->tiefe ? dynamic_cast<Gtk::Grid *>(F->tiefe->get_parent()) : nullptr) {
                    int top = 0;
                    gtk_container_child_get(GTK_CONTAINER(g->gobj()), GTK_WIDGET(F->tiefe->gobj()), "top-attach", &top, nullptr);
                    if (auto l = dynamic_cast<Gtk::Label *>(g->get_child_at(2, top)))
                        l->set_text(bis ? TR("mm · höchstens so tief (0 = bis aufs Modell, egal wie tief)") : "mm");
                }
                if (F->durch) F->durch->set_sensitive(!haut);
                if (F->starttiefe) F->starttiefe->set_sensitive(!haut);   // Peter 03.11.: auch bei «bis aufs Modell»
                if (F->zweck) F->zweck->set_sensitive(!bis);
                for (auto w : {F->sanz, F->sbreit, F->shoch})
                    if (w) w->set_sensitive(!bis);
            });
        }
    }
    s1->add(*grid);
    if (f.zart) {
        beobachten(f.zart->modus);
        beobachten(f.zart->letzte);
        f.zart->liste->signal_changed().connect([F] { F->alle(); });
        f.neu.push_back([F] { F->zart->nachfuehren(); });
    }

    if (f.zust) {
        auto gaenge = label("", "hirch-dim");
        klasse(*gaenge, "hirch-klein");
        s1->add(*gaenge);
        f.neu.push_back([F, gaenge, art, spitze_extra] {
            // Gravur: die Tiefe zählt ab «Beginnt bei Tiefe» (Kontur, Tasche: ab der Oberfläche)
            double ab = F->starttiefe && art != "gravur" ? F->starttiefe->get_value() : 0.0;
            double t = std::max(0.01, F->tiefe->get_value() + spitze_extra() - ab), zu = std::max(0.01, F->zust->get_value());
            int k = std::max(1, (int)std::ceil(t / zu - 1e-9));
            if (F->zart && !F->zart->art().empty()) {
                gaenge->set_text("→ " + F->zart->kurz(t));
                return;
            }
            gaenge->set_text("→ " + std::to_string(k) + (art == "bohren" ? (k == 1 ? TR(" Hub") : TR(" Hübe"))
                                                                          : (k == 1 ? TR(" Durchgang") : TR(" Durchgänge"))) +
                             TR(", je höchstens ") + zahl(std::min(t, zu)) + " mm");
        });
        // Richtwerte aus der Werkzeugdatenbank für das gewählte Material
        auto rk = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        klasse(*rk, "hirch-karte");
        rk->set_margin_top(4);
        auto rtext = label("");
        rtext->set_line_wrap(true);
        rtext->set_max_width_chars(40);
        rtext->set_hexpand(true);
        klasse(*rtext, "hirch-klein");
        rk->add(*rtext);
        auto rbtn = Gtk::make_managed<Gtk::Button>(TR("Übernehmen"));
        rbtn->set_valign(Gtk::ALIGN_CENTER);
        rbtn->set_tooltip_text(TR("Zustellung, Bahnabstand, Rampenwinkel und Vollnut-Vorschub auf die Richtwerte "
                               "für dieses Werkzeug und diesen Werkstoff setzen"));
        rk->add(*rbtn);
        s1->add(*rk);
        auto mat = werkstoff_name();
        f.neu.push_back([this, F, rtext, wz_id, mat] {
            auto w = wirksam(wz_id());
            auto e = unter(w, "empfehlung");
            if (!e) {
                rtext->set_text(TR("Keine Richtwerte für dieses Werkzeug."));
                return;
            }
            double ez = num(*e, "zustellung", 1);
            std::string t = TR("Richtwert in ") + mat + TR(": Zustellung ") + zahl(ez) + " mm";
            if (F->bahn) t += TR(" · Bahnabstand ") + zahl(num(*e, "bahnabstand", 40), 0) + " %";
            if (F->zust->get_value() > 1.5 * ez + 1e-9) {
                t += TR("\n⚠ Die Zustellung ist deutlich höher als empfohlen.");
            }
            rtext->set_text(t);
        });
        rbtn->signal_clicked().connect([this, F, wz_id] {
            auto w = wirksam(wz_id());
            auto e = unter(w, "empfehlung");
            if (!e) return;
            F->zust->set_value(num(*e, "zustellung", F->zust->get_value()));
            if (F->bahn) F->bahn->set_value(num(*e, "bahnabstand", 40));
            if (F->rwinkel) F->rwinkel->set_value(num(*e, "rampe", 3));
            if (F->vollnut) F->vollnut->set_value(100 * num(*e, "vollnut", 0.5));
        });
    }

    // ================================================= Bahnen
    if (art == "planen") {
        auto s = reiter(TR("Bahnen"));
        auto g = formular();
        f.bahn = spin_zeile(*g, 0, TR("Bahnabstand"), num(b, "bahnabstand", 60), 5, 90, 1, 0, TR("% vom Ø"));
        f.prichtung = combo_zeile(*g, 1, TR("Fahrrichtung"));
        f.prichtung->append("x", TR("Entlang X (links ↔ rechts)"));
        f.prichtung->append("y", TR("Entlang Y (vorne ↔ hinten)"));
        f.prichtung->set_active_id(str(b, "richtung", "x") == "y" ? "y" : "x");
        f.ueber = spin_zeile(*g, 2, TR("Überstand über den Rand"), ist_null(b, "ueberstand") ? 0 : num(b, "ueberstand", 0),
                             0, 100, 0.5, 1, TR("mm (0 = Radius + 1 mm)"));
        s->add(*g);
        s->add(*hinweis_label(TR("Der Fräser fährt in Bahnen über das ganze Material und taucht neben dem Material "
                              "ein. Nimm einen möglichst grossen Fräser (Planfräser). Die anderen Bearbeitungen "
                              "fräsen danach ab der neuen Oberfläche – ganz durchgehende Konturen bleiben, "
                              "wie sie sind.")));
        for (Gtk::Widget *w : std::initializer_list<Gtk::Widget *>{f.bahn, f.prichtung, f.ueber}) beobachten(w);
    }
    if (art == "tasche") {
        auto s = reiter(TR("Ausräumen"));
        auto g = formular();
        f.strategie = combo_zeile(*g, 0, TR("Strategie"));
        f.strategie->append("runden", TR("In Runden (Versatz nach innen)"));
        f.strategie->append("adaptiv", TR("Adaptiv (gleichmässige Last)"));
        f.strategie->set_active_id(str(b, "strategie", "runden"));
        f.strategie->set_tooltip_text(TR("Adaptiv: der Fräser schneidet überall gleich viel Material – keine "
                                      "Lastspitzen in Ecken und Übergängen, tiefere Zustellung möglich. "
                                      "Eingetaucht wird mit einer Helix."));
        f.bahn = spin_zeile(*g, 1, TR("Bahnabstand"), num(b, "bahnabstand", 40), 5, 90, 1, 0, TR("% vom Ø"));
        f.alast = spin_zeile(*g, 2, TR("Schnittbreite (adaptiv)"), num(b, "adaptiv_last", 20), 5, 60, 1, 0, TR("% vom Ø"));
        f.richtung = combo_zeile(*g, 3, TR("Fräsrichtung"));
        f.aufmass = spin_zeile(*g, 4, TR("Aufmass"), num(b, "aufmass", 0), -5, 5, 0.05, 2, "mm");
        f.dogbone = Gtk::make_managed<Gtk::CheckButton>(TR("Ecken freifräsen (Dogbone) – damit ein eckiges Teil "
                                                        "hineinpasst"));
        f.dogbone->set_active(flag(b, "dogbone", false));
        f.dogbone->set_tooltip_text(TR("Ein runder Fräser lässt in jeder Ecke einen Rest stehen. Mit Dogbone "
                                    "fährt er in die Ecke hinein, bis er sie berührt – es entsteht eine kleine "
                                    "runde Ausbuchtung, die Ecke selbst ist aber frei."));
        g->attach(*f.dogbone, 0, 5, 3);
        beobachten(f.dogbone);
        // Restmaterial: ein grösserer Fräser räumt die Fläche zuerst
        f.rwerkzeug = combo_zeile(*g, 6, TR("Zuerst räumen mit"));
        werkzeug_combo(*f.rwerkzeug, str(b, "raeum_werkzeug", ""), false);
        f.rwerkzeug->prepend("", TR("keinem – alles mit dem Werkzeug der Tasche"));
        f.rwerkzeug->set_active_id(str(b, "raeum_werkzeug", ""));
        f.rwerkzeug->set_tooltip_text(TR("Ein grösserer Fräser räumt die Fläche zuerst aus. Das Werkzeug der "
                                      "Tasche fräst danach nur noch, wo der grosse nicht hinkommt: Ecken, "
                                      "schmale Stellen, feine Details."));
        f.rzust = spin_zeile(*g, 7, TR("Zustellung beim Räumen"), num(b, "raeum_zustellung", 0), 0, 200, 0.1, 2,
                             TR("mm (0 = halber Ø)"));
        beobachten(f.rwerkzeug);
        beobachten(f.rzust);
        s->add(*g);
        auto raeum_info = hinweis_label("");
        s->add(*raeum_info);
        f.neu.push_back([this, F, raeum_info, raeum_d, wz_id] {
            auto r = F->rwerkzeug->get_active_id().raw();
            bool an = !r.empty() && r != wz_id();
            zeile_zeigen(F->rzust, an);
            if (!an) {
                raeum_info->set_text(r.empty() ? "" : TR("Räumwerkzeug = Werkzeug der Tasche – wird nicht vorgeräumt."));
                return;
            }
            auto w = wirksam(r);
            double dg = num(w, "durchmesser", 6);
            double z = F->rzust->get_value() > 0 ? F->rzust->get_value() : dg / 2;
            if (F->tiefe && F->tiefe->get_value() > 0) z = std::min(z, F->tiefe->get_value());
            raeum_info->set_text("→ " + werkzeug_name(w) + TR(" räumt die Fläche mit ") + zahl(z, 2) +
                                 TR(" mm Zustellung, danach fräst das Werkzeug der Tasche (Ø ") + zahl(raeum_d(), 2) +
                                 TR(" mm) nur noch den Rest. Der Bahnabstand gilt für beide.") +
                                 (dg <= raeum_d() + 1e-6 ? TR(" Achtung: das Räumwerkzeug ist nicht grösser.") : ""));
        });
        auto bahn_info = hinweis_label("");
        s->add(*bahn_info);
        f.neu.push_back([F, bahn_info, raeum_d, ist_v] {
            double d = raeum_d(), proz = F->bahn->get_value();
            bahn_info->set_text(TR("→ Bahnabstand ") + zahl(d * proz / 100.0, 2) + " mm (" + zahl(proz, 0) + " % " +
                                (ist_v() ? TR("der Spitze ") : TR("vom Ø ")) + zahl(d, 2) + " mm)" +
                                (ist_v() ? TR(" – mit einem V-Fräser räumt nur die Spitze den Boden aus.") : ""));
        });
        auto sk = skizze(128, [F, raeum_d](Cr const &cr, double w, double h, Gdk::RGBA const &fg) {
            skizze_tasche(cr, w, h, fg, F->strategie->get_active_id() == "adaptiv", F->bahn->get_value(),
                          F->alast->get_value(), F->richtung->get_active_id() == "1", F->aufmass->get_value(),
                          raeum_d());
        });
        f.skizzen.push_back(sk);
        s->add(*sk);
        s->add(*hinweis_label(TR("Adaptiv lohnt sich bei grossen Taschen und harten Werkstoffen: die Zustellung "
                              "darf dann ein Mehrfaches betragen. Aufmass + lässt Material an der Wand stehen "
                              "(Tasche kleiner), − nimmt mehr weg.")));
        f.neu.push_back([F] {
            bool ad = F->strategie->get_active_id() == "adaptiv";
            zeile_zeigen(F->bahn, !ad);
            zeile_zeigen(F->alast, ad);
        });
        for (Gtk::Widget *w : std::initializer_list<Gtk::Widget *>{f.strategie, f.bahn, f.alast, f.aufmass}) {
            beobachten(w);
        }
    }
    if (art == "kontur") {
        auto s = reiter(TR("Kontur"));
        auto g = formular();
        f.seite = combo_zeile(*g, 0, TR("Seite"));
        f.seite->append("auto", TR("Automatisch (aussen, Löcher innen)"));
        f.seite->append("aussen", TR("Aussen"));
        f.seite->append("innen", TR("Innen"));
        f.seite->append("linie", TR("Auf der Linie"));
        f.seite->set_tooltip_text(TR("Offene Linien: Aussen fräst rechts, Innen links der Zeichenrichtung. Der Pfeil auf "
                                     "der Linie zeigt die Richtung, der Strich die gefräste Seite. Pfad › Richtung "
                                     "umkehren tauscht die Seite."));
        f.seite->set_active_id(str(b, "seite", "auto"));
        f.richtung = combo_zeile(*g, 1, TR("Fräsrichtung"));
        f.aufmass = spin_zeile(*g, 2, TR("Aufmass"), num(b, "aufmass", 0), -5, 5, 0.05, 2, "mm");
        f.anfahren = combo_zeile(*g, 3, TR("Anfahren"));
        f.anfahren->append("", TR("Senkrecht auf der Kontur"));
        f.anfahren->append("bogen", TR("Im Bogen von der Abfallseite (keine Marke)"));
        f.anfahren->set_active_id(str(b, "anfahren", ""));
        f.anfahren->set_tooltip_text(TR("Im Bogen: der Fräser taucht neben dem Teil ein und läuft tangential in "
                                     "die Kontur ein und wieder aus – an der Kante bleibt keine Eintauchmarke. "
                                     "Nur sinnvoll, wenn daneben Abfall ist (Ausschneiden)."));
        f.aradius = spin_zeile(*g, 4, TR("Radius des Bogens"), num(b, "anfahr_radius", 0), 0, 50, 0.5, 1,
                               TR("mm (0 = halber Fräser)"));
        f.neu.push_back([F] { zeile_zeigen(F->aradius, F->anfahren->get_active_id() == "bogen"); });
        f.dogbone = Gtk::make_managed<Gtk::CheckButton>(TR("Innenecken freifräsen (Dogbone)"));
        f.dogbone->set_active(flag(b, "dogbone", false));
        f.dogbone->set_tooltip_text(TR("In Innenecken lässt der runde Fräser einen Rest stehen. Mit Dogbone fährt "
                                    "er in die Ecke hinein, bis er sie berührt – so passen eckige Teile "
                                    "ineinander (Zapfen, Einlagen, Steckverbindungen)."));
        g->attach(*f.dogbone, 0, 5, 3);
        beobachten(f.dogbone);
        beobachten(f.anfahren);
        s->add(*g);
        auto sk = skizze(128, [F](Cr const &cr, double w, double h, Gdk::RGBA const &fg) {
            skizze_kontur(cr, w, h, fg, F->seite->get_active_id().raw(), F->richtung->get_active_id() == "1",
                          F->aufmass->get_value());
        });
        f.skizzen.push_back(sk);
        s->add(*sk);
        s->add(*hinweis_label(TR("Aufmass + lässt Material stehen (Teil grösser, Löcher kleiner), − nimmt mehr "
                              "weg – praktisch für Passungen.")));
        beobachten(f.seite);
        beobachten(f.aufmass);

        auto s2 = reiter(TR("Haltestege"));
        sst = std::make_shared<StegZustand>();
        auto S = sst;
        {
            auto m = str(b, "stege_modus", "auto");
            S->modus = m == "keine" ? "keine" : !steg_marken(id).empty() ? "manuell" : m;
            S->anzahl = (int)num(b, "stege_anzahl", 4);
            S->breite = num(b, "stege_breite", 6);
        }
        auto g3 = formular();
        f.sanz = spin_zeile(*g3, 0, TR("Anzahl je Kontur"), num(b, "stege_anzahl", 4), 1, 20, 1, 0, "");
        f.sbreit = spin_zeile(*g3, 1, TR("Breite"), num(b, "stege_breite", 6), 1, 50, 0.5, 1, "mm");
        f.shoch = spin_zeile(*g3, 2, TR("Dicke"), num(b, "stege_hoehe", 1.5), 0.2, 20, 0.1, 2, "mm");
        f.shoch->set_tooltip_text(TR("So viel Material bleibt am Steg stehen – Hirch legt ihn unten an den Boden"));
        s2->add(*g3);
        auto knoepfe = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        S->verteilen = Gtk::make_managed<Gtk::Button>(TR("Gleichmässig verteilen"));
        S->verteilen->set_tooltip_text(TR("Legt die Marken neu auf jede Kontur dieser Bearbeitung"));
        S->keine = Gtk::make_managed<Gtk::Button>(TR("Keine Haltestege"));
        S->keine->set_tooltip_text(TR("Entfernt die Marken dieser Bearbeitung – das Teil anders festhalten"));
        knoepfe->add(*S->verteilen);
        knoepfe->add(*S->keine);
        s2->add(*knoepfe);
        S->status = label("", "hirch-dim");
        S->status->set_line_wrap(true);
        S->status->set_xalign(0);
        klasse(*S->status, "hirch-klein");
        s2->add(*S->status);
        auto status_auf = [this, S, id] {
            if (!S->status) return;
            std::string t;
            if (S->laeuft) t = TR("Haltestege werden verteilt …");
            else if (S->modus == "keine") t = TR("Keine Haltestege – Teile können sich lösen und wegfliegen.");
            else {
                auto mk = steg_marken(id);
                if (mk.empty()) t = TR("Noch keine Marken – beim Übernehmen werden sie gleichmässig verteilt.");
                else {
                    int v = steg_verschoben(id);
                    t = anzahl((long)mk.size(), TR("Marke"), TR("Marken")) + TR(" auf dieser Bearbeitung");
                    if (v > 0) t += " · " + (v == 1 ? std::string(TR("1 von Hand verschoben"))
                                                     : TRF("{} von Hand verschoben", {std::to_string(v)}));
                }
            }
            if (!S->hinweis.empty() && !S->laeuft) t += "\n" + S->hinweis;
            S->status->set_text(t);
        };
        status_auf();
        auto sk2 = skizze(118, [F, S, dicke](Cr const &cr, double w, double h, Gdk::RGBA const &fg) {
            double t = F->tiefe->get_value();
            skizze_stege(cr, w, h, fg, S->modus == "keine" ? 0 : (int)F->sanz->get_value(),
                         F->sbreit->get_value(), F->shoch->get_value(), dicke, t >= dicke - 1e-6);
        });
        f.skizzen.push_back(sk2);
        s2->add(*sk2);
        s2->add(*hinweis_label(TR("Die Stege halten ausgeschnittene Teile fest, bis alles gefräst ist. Danach mit "
                               "dem Cutter oder einer Feile lösen. Kleine Teile brauchen mindestens zwei.\n"
                               "Die gelben Marken auf der Zeichnung sind die Stege: verschieben, kopieren (Strg+D) "
                               "oder löschen wie jedes Objekt. Breite und Dicke gelten für alle Stege dieser "
                               "Bearbeitung.")));
        // zweiseitig: oft trennt erst der Schnitt der anderen Seite das Teil heraus – dann gelten die Stege
        // auch hier (Peter, 23.10.; engine._durch_mit_gegenseite prüft, ob sich die Schnitte treffen)
        bool zwei = zweiseitig();
        f.neu.push_back([F, S, g3, knoepfe, dicke, zwei] {
            bool an = zwei || F->tiefe->get_value() >= dicke - 1e-6;
            S->durch = an;
            g3->set_sensitive(an && S->modus != "keine");
            knoepfe->set_sensitive(an && !S->laeuft);
        });
        auto dlg_p = dlg.get();
        S->verteilen->signal_clicked().connect([this, F, S, b, dlg_p, id, status_auf] {
            if (S->laeuft || !S->auslesen) return;
            if (S->modus != "keine" && !stege_rueckfrage(dlg_p, steg_verschoben(id))) return;
            bj::object jetzt = b;
            S->auslesen(jetzt);
            jetzt["stege_modus"] = "manuell";
            S->laeuft = true;
            S->hinweis.clear();
            status_auf();
            F->alle();
            auto lebt = S->lebt;
            stege_verteilen(jetzt, [F, S, jetzt, lebt, status_auf](int k, std::string const &h) {
                if (!*lebt) return;
                S->laeuft = false;
                S->hinweis = k < 0 ? TR("Verteilen ging nicht: ") + h : h;
                if (k > 0) {
                    S->modus = "manuell";
                    S->anzahl = (int)num(jetzt, "stege_anzahl", 4);
                    S->breite = num(jetzt, "stege_breite", 6);
                }
                status_auf();
                F->alle();
            });
        });
        S->keine->signal_clicked().connect([this, F, S, status_auf] {
            if (S->laeuft) return;
            S->modus = "keine";
            S->hinweis.clear();
            status_auf();
            F->alle();
        });
        for (Gtk::Widget *w : std::initializer_list<Gtk::Widget *>{f.sanz, f.sbreit, f.shoch}) beobachten(w);
    }
    if (f.richtung) {
        f.richtung->append("1", TR("Gleichlauf"));
        f.richtung->append("0", TR("Gegenlauf"));
        f.richtung->set_active_id(flag(b, "gleichlauf", true) ? "1" : "0");
        f.richtung->set_tooltip_text(TR("Gleichlauf: saubere Kante, Standard. Gegenlauf: bei Spiel in der "
                                     "Maschine oder sehr hartem Material."));
        beobachten(f.richtung);
    }

    // ================================================= Eintauchen / Vorschub
    {
        auto s = reiter(hat_bahnen ? TR("Eintauchen") : TR("Vorschub"));
        auto g = formular();
        int r = 0;
        auto w0 = wirksam(str(b, "werkzeug", "A"));
        if (hat_bahnen) {
            f.eintauchen = combo_zeile(*g, r++, TR("Eintauchen"));
            f.eintauchen->append("rampe", TR("Schräg (Rampe)"));
            f.eintauchen->append("senkrecht", TR("Senkrecht"));
            f.eintauchen->set_active_id(str(b, "eintauchen_art", "rampe"));
            f.rwinkel = spin_zeile(*g, r++, TR("Rampenwinkel"),
                                   ist_null(b, "rampe_winkel") || num(b, "rampe_winkel", 0) <= 0
                                       ? num(w0, "rampe", 3) : num(b, "rampe_winkel", 3),
                                   0.5, 45, 0.5, 1, "°");
        }
        v_haupt = vorschub_zeile(g, r++, TR("Vorschub"), num(b, "vorschub_mm", 0), num(b, "vorschub_prozent", 100),
                                 [this, wz_id] { return num(wirksam(wz_id()), "vorschub", 1000); });
        v_haupt->spin->set_tooltip_text(TR("Ändert nur diese Bearbeitung; die Werte des Werkzeugs bleiben, wie sie sind."));
        f.vprozent = v_haupt->spin;
        if (hat_bahnen) {
            auto vh = v_haupt;
            vn_haupt = vorschub_zeile(g, r++, TR("Vorschub bei Vollnut"), num(b, "vollnut_mm", 0),
                                      ist_null(b, "vollnut") ? 0 : num(b, "vollnut", 0),
                                      [this, wz_id, vh] { return vh->spin->get_value() *
                                                                 num(wirksam(wz_id()), "vollnut", 0.5); });
            f.vollnut = vn_haupt->spin;
            vfaktor = spin_zeile(*g, r++, TR("Faktor (Zeitleiste)"), num(b, "vorschub_faktor", 100), 20, 400, 5, 0, "%");
            vfaktor->set_tooltip_text(TR("Wird in der Zeitleiste unten durch Ziehen gesetzt: alle Fräsvorschübe "
                                      "dieser Bearbeitung mal diesen Faktor (Eilgang und Eintauchen bleiben)."));
        }
        s->add(*g);
        if (hat_bahnen) {
            auto sk = skizze(120, [F](Cr const &cr, double w, double h, Gdk::RGBA const &fg) {
                skizze_eintauchen(cr, w, h, fg, F->eintauchen->get_active_id() == "rampe", F->rwinkel->get_value());
            });
            f.skizzen.push_back(sk);
            s->add(*sk);
            f.neu.push_back([F] { zeile_zeigen(F->rwinkel, F->eintauchen->get_active_id() == "rampe"); });
            beobachten(f.eintauchen);
            beobachten(f.rwinkel);
            beobachten(f.vollnut);
        }
        beobachten(f.vprozent);
        auto vk = label("", "hirch-wert");
        klasse(*vk, "hirch-karte");
        vk->set_line_wrap(true);
        s->add(*vk);
        auto vh = v_haupt, vnh = vn_haupt;
        f.neu.push_back([this, vk, wz_id, vh, vnh] {
            vh->nachfuehren();
            if (vnh) vnh->nachfuehren();
            auto w = wirksam(wz_id());
            vk->set_text(TR("Eintauchen ") + ganz(num(w, "eintauchen", 300)) + TR(" mm/min · Drehzahl ") +
                         ganz(num(w, "drehzahl", 18000)) + TR(" U/min"));
        });
        s->add(*hinweis_label(hat_bahnen
                                  ? TR("Wo der Fräser mit dem ganzen Durchmesser schneidet (Kontur, innerste Runde, "
                                    "Übergänge), fährt er mit dem Vollnut-Vorschub.")
                                  : TR("Drehzahl und Grundvorschub kommen vom Werkzeug und dem Werkstoff.")));
    }

    // ================================================= Schlichten
    if (hat_bahnen) {
        auto s = reiter(TR("Schlichten"));
        f.schlichten = Gtk::make_managed<Gtk::CheckButton>(TR("Schlichten – eigener Durchgang für den Rest"));
        f.schlichten->set_active(flag(b, "schlichten", false));
        s->add(*f.schlichten);
        auto g4 = formular();
        f.szugabe = spin_zeile(*g4, 0, TR("Schlichtzugabe"), num(b, "schlicht_zugabe", 0.3), 0, 5, 0.05, 2, "mm");
        f.swerkzeug = combo_zeile(*g4, 1, TR("Werkzeug"));
        werkzeug_combo(*f.swerkzeug, str(b, "schlicht_werkzeug", ""), false);
        f.swerkzeug->prepend("", TR("gleiches wie beim Schruppen"));
        f.swerkzeug->set_active_id(str(b, "schlicht_werkzeug", ""));
        double tiefe_jetzt = f.tiefe->get_value();
        // B-2 (Peter 30.10.): eigene Tiefe beim Schlichten – das Schruppen lässt unten Material stehen, damit die
        // Teile halten, das Schlichten fräst auf diese Tiefe durch. Ohne Eintrag wie beim Schruppen.
        {
            g4->attach(*label(TR("Tiefe")), 0, 2);
            auto box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
            f.stiefe_art = Gtk::make_managed<Gtk::ComboBoxText>();
            f.stiefe_art->append("", TR("wie beim Schruppen"));
            f.stiefe_art->append("eigen", TR("eigene"));
            bool eigen = !ist_null(b, "schlicht_tiefe");
            f.stiefe_art->set_active_id(eigen ? "eigen" : "");
            auto adj = Gtk::Adjustment::create(eigen ? num(b, "schlicht_tiefe", tiefe_jetzt) : tiefe_jetzt, 0.05, 200,
                                               0.1, 1);
            f.stiefe = Gtk::make_managed<Gtk::SpinButton>(adj, 0.1, 2);
            f.stiefe->set_numeric(true);
            punkt_und_komma(f.stiefe);
            f.stiefe->set_width_chars(7);
            box->add(*f.stiefe_art);
            box->add(*f.stiefe);
            box->add(*label("mm", "hirch-dim"));
            g4->attach(*box, 1, 2, 2);
            auto F2 = F;
            auto grau = [F2] { F2->stiefe->set_sensitive(F2->stiefe_art->get_active_id() == "eigen"); };
            f.stiefe_art->signal_changed().connect([F2, grau] {
                // beim Umschalten auf «eigene»: von der heutigen Tiefe aus
                if (F2->stiefe_art->get_active_id() == "eigen" && F2->tiefe)
                    F2->stiefe->set_value(std::max(F2->stiefe->get_value(), F2->tiefe->get_value()));
                grau();
            });
            grau();
            beobachten(f.stiefe_art);
            beobachten(f.stiefe);
        }
        f.szust = spin_zeile(*g4, 3, TR("Zustellung"),
                             ist_null(b, "schlicht_zustellung") ? tiefe_jetzt : num(b, "schlicht_zustellung", tiefe_jetzt),
                             0.1, 200, 0.1, 2, "mm");
        if (ist_null(b, "schlicht_zustellung")) f.szust_ganz = f.szust->get_value();
        v_schl = vorschub_zeile(g4, 4, TR("Vorschub beim Schlichten"), num(b, "schlicht_vorschub_mm", 0),
                                num(b, "schlicht_vorschub", 100), [this, F] {
                                    auto sw = F->swerkzeug->get_active_id().raw();
                                    return num(wirksam(sw.empty() ? F->werkzeug->get_active_id().raw() : sw),
                                               "vorschub", 1000);
                                });
        f.svprozent = v_schl->spin;
        {
            auto vs = v_schl;
            f.neu.push_back([vs] { vs->nachfuehren(); });
        }
        // eigene Fräsrichtung: z. B. Schruppen im Gleichlauf, Schlichten im Gegenlauf (Peter, 20.10.)
        f.srichtung = combo_zeile(*g4, 5, TR("Fräsrichtung beim Schlichten"));
        f.srichtung->append("", TR("wie beim Schruppen"));
        f.srichtung->append("1", TR("Gleichlauf"));
        f.srichtung->append("0", TR("Gegenlauf"));
        if (auto v = b.if_contains("schlicht_gleichlauf"); v && v->is_bool()) f.srichtung->set_active_id(v->as_bool() ? "1" : "0");
        else f.srichtung->set_active_id("");
        f.srichtung->set_tooltip_text(TR("Gegenlauf beim Schlichten gibt bei Spiel in der Maschine oft die genauere Kante – "
                                      "statt einer zweiten Bearbeitung als Kopie."));
        beobachten(f.srichtung);
        s->add(*g4);
        auto sk = skizze(118, [F](Cr const &cr, double w, double h, Gdk::RGBA const &fg) {
            auto sw = F->swerkzeug->get_active_id().raw();
            skizze_schlichten(cr, w, h, fg, F->schlichten->get_active(), F->szugabe->get_value(),
                              !sw.empty() && sw != F->werkzeug->get_active_id().raw());
        });
        f.skizzen.push_back(sk);
        s->add(*sk);
        s->add(*hinweis_label(TR("Schruppen räumt das meiste aus und lässt die Zugabe stehen; das Schlichten fährt "
                              "danach nur am Rand entlang auf Mass – meist in einem Durchgang über die ganze "
                              "Tiefe. Mit anderem Werkzeug entsteht eine eigene Datei.")));
        s->add(*hinweis_label(TR("Schruppen kann unten Material stehen lassen, damit die Teile halten – das Schlichten "
                              "fräst dann auf diese Tiefe durch.")));
        f.neu.push_back([F, g4] { zeile_zeigen(g4, F->schlichten->get_active()); });
        for (Gtk::Widget *w : std::initializer_list<Gtk::Widget *>{f.schlichten, f.szugabe, f.swerkzeug, f.szust,
                                                                    f.svprozent}) {
            beobachten(w);
        }
    }

    // ================================================= Sofort-Vorschau (rechts oben)
    // Schnitt durch die Rille: rechnet nichts, folgt jeder Änderung sofort
    rechts->add(*label(TR("SOFORT-VORSCHAU"), "hirch-kopf"));
    bool ab_relativ = art == "gravur" || art == "vcarve";     // Tiefe zählt ab «Beginnt bei Tiefe»
    auto werte_jetzt = [F, spitze_extra, ab_relativ]() {
        double t = F->maxt ? F->maxt->get_value() : (F->tiefe ? F->tiefe->get_value() : 0.0);
        double z = F->vzust ? F->vzust->get_value() : (F->zust ? F->zust->get_value() : 0.0);
        if (F->zweck && F->zweck->get_active_id() == "fase") z = 0;
        double ab = F->starttiefe && F->starttiefe->is_visible() && !ab_relativ ? F->starttiefe->get_value() : 0.0;
        if (ab >= t) ab = 0;
        double weg = t - ab + spitze_extra();
        int n = (z > 1e-6 && weg > 1e-6) ? std::max(1, (int)std::ceil(weg / z - 1e-9)) : 1;
        if (F->zart && weg > 1e-6) n = (int)F->zart->stufen(weg).size();
        return std::make_tuple(t, z, n, ab);
    };
    auto schnitt = skizze(150, [this, F, dicke, werte_jetzt, wz_id](Cr const &cr, double w, double h, Gdk::RGBA const &fg) {
        // Haut durchtrennen (Windows 02.11.): die Tiefe rechnet Hirch CNC – kein Schnitt «ganz durch» zeigen
        if (F->tiefe_ab && F->tiefe_ab->get_active_id() == "haut") {
            skizze_tiefe_ab(cr, w, h, fg, "haut");
            schrift(cr, fg, 4, 12, TR("Tiefe rechnet Hirch CAM aus der Haut"));
            return;
        }
        auto [t, z, n, ab] = werte_jetzt();
        (void)z;
        auto wz = wirksam(wz_id());
        bool v = str(wz, "typ") == "v";
        double d = num(wz, "durchmesser", 3), winkel = num(wz, "v_winkel", 60), spitze = num(wz, "spitze", 0);
        double breite = v ? std::min(d, spitze + 2 * t * std::tan(winkel * M_PI / 360.0)) : d;
        // Bohrer: kegelige Spitze; mit «Spitze dazu» reicht sie unter die Tiefe
        bool bohrer = str(wz, "typ") == "bohrer";
        double kl = bohrer ? d / 2 / std::tan(std::clamp(num(wz, "spitzenwinkel", 118), 30.0, 179.0) * M_PI / 360.0) : 0;
        bool dazu = bohrer && F->spitze && F->spitze->get_active();
        double tg = t + (dazu ? kl : 0);
        double oben = 28, unten = h - 14;
        double tief_max = std::max({tg, dicke * 0.6, 0.5});
        double skala = std::min((unten - oben) / (tief_max * 1.15), (w * 0.5) / std::max(breite, 0.1));
        double mx = w * 0.45;
        double mat_h = std::min(dicke * skala, unten - oben + 6);
        farbe(cr, HOLZ);
        cr->rectangle(1, oben, w - 2, mat_h);
        cr->fill();
        double bt = t * skala, bw = breite * skala, sp = (v ? spitze : d) * skala;
        if (bohrer) {
            double ys = oben + (tg - kl) * skala;
            cr->move_to(mx - bw / 2, oben);
            cr->line_to(mx - bw / 2, ys);
            cr->line_to(mx, oben + tg * skala);
            cr->line_to(mx + bw / 2, ys);
            cr->line_to(mx + bw / 2, oben);
        } else {
            cr->move_to(mx - bw / 2, oben);
            cr->line_to(mx - sp / 2, oben + bt);
            cr->line_to(mx + sp / 2, oben + bt);
            cr->line_to(mx + bw / 2, oben);
        }
        cr->close_path();
        cr->set_source_rgb(1, 1, 1);
        cr->fill_preserve();
        farbe(cr, HOLZ_RAND);
        cr->set_line_width(1.5);
        cr->stroke();
        // Ebenen der Durchgänge
        cr->set_line_width(1.2);
        std::vector<double> strich{4, 3};
        double ya = ab * skala;                    // ab hier wird zugestellt
        if (ya > 0) {
            cr->set_source_rgb(0.55, 0.55, 0.55);
            cr->set_dash(std::vector<double>{2, 3}, 0);
            cr->move_to(mx - bw / 2 - 18, oben + ya);
            cr->line_to(mx + bw / 2 + 18, oben + ya);
            cr->stroke();
            cr->unset_dash();
            schrift(cr, fg, mx + bw / 2 + 20, oben + ya + 4, TR("ab ") + zahl(ab));
        }
        // Ebenen: gleichmässig oder nach der Art der Zustellungen (letzte kleiner, Liste)
        std::vector<double> anteile;
        if (F->zart && t - ab > 1e-6) {
            double summe = 0, weg = t - ab;
            for (auto st : F->zart->stufen(weg)) anteile.push_back((summe += st) / weg);
        }
        for (int i = 1; i <= n; ++i) {
            double q = i <= (int)anteile.size() ? anteile[i - 1] : (double)i / n;
            double y = oben + ya + (bt - ya) * q;
            double anteil = (ya + (bt - ya) * q) / std::max(bt, 1e-9);
            double halb = v ? (sp + (bw - sp) * (1.0 - anteil)) / 2 : bw / 2;
            cr->set_source_rgb(i == n ? 0.14 : 0.11, i == n ? 0.12 : 0.44, i == n ? 0.19 : 0.85);
            cr->set_dash(strich, 0);
            cr->move_to(mx - halb - 10, y);
            cr->line_to(mx + halb + 10, y);
            cr->stroke();
        }
        cr->unset_dash();
        schrift(cr, fg, 4, 16, (ab > 0 ? zahl(ab) + "–" : std::string()) + zahl(t) +
                                   (dazu ? TRF(" mm + Spitze {}", {zahl(kl, 1)}) : std::string()) + TR(" mm in ") +
                                   std::to_string(n) +
                                   (F->spitze ? (n == 1 ? TR(" Hub") : TR(" Hüben"))
                                              : (n == 1 ? TR(" Durchgang") : TR(" Durchgängen"))) +
                                   (v ? " · " + zahl(breite) + TR(" mm breit oben") : std::string()));
    });
    rechts->add(*schnitt);
    f.skizzen.push_back(schnitt);
    // Geändert seit dem Öffnen
    rechts->add(*label(TR("GEÄNDERT SEIT DEM ÖFFNEN"), "hirch-kopf"));
    auto aend_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 3);
    rechts->add(*aend_box);
    auto aend_zeit = label("", "hirch-dim");
    klasse(*aend_zeit, "hirch-klein");
    rechts->add(*aend_zeit);
    auto vorg_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);    // «So fräse ich»
    rechts->add(*vorg_box);

    // ================================================= Letzte Rechnung (rechts)
    std::shared_ptr<LiveVorschau> live;
    bool live_leer = false;
    {
        auto s = rechts;
        auto wahl = std::make_shared<std::string>("");
        live_leer = !ergebnis_aktuell || !ablauf_op(id);
        auto halter = std::make_shared<std::shared_ptr<LiveVorschau>>();
        bool leer_ = live_leer;
        auto klappe = vorschau_klappe(s, [this, halter, leer_] {
            if (leer_ && *halter) live_rechnen(*halter, true);
        });
        live = live_vorschau(klappe, id, index, [wahl] { return *wahl; },
                             str(b, "farbe").empty() ? std::string(info->hex) : str(b, "farbe"),
                             live_leer ? TR("Die Vorschau erscheint gleich …")
                                       : TR("Stand der letzten Rechnung. Dunkel: was der Fräser wegnimmt."));
        *halter = live;
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
        zeile(TR("Objekte im Farbton"), std::to_string(n));
        if (long zu = zusaetzlich(id); zu > 0) zeile(TR("zusätzlich zugewiesen"), std::to_string(zu));
        bj::object const *st = nullptr;
        if (ergebnis_aktuell) {
            if (auto sts = unter(ergebnis, "bearbeitungen")) st = unter(*sts, id.c_str());
        }
        if (st) {
            zeile(TR("Fräsweg"), einheit_weg(num(*st, "weg", 0)));
            if (num(*st, "senkrecht", 0) > 0) {
                zeile(TR("senkrecht eingetaucht"), std::to_string((long)num(*st, "senkrecht", 0)) + " ×");
            }
            // Fehler der Prüfung bei dieser Bearbeitung: keine Zeit, sondern was nicht passt (Peter, 20.10.)
            bool fehler = false;
            if (auto pr = liste_von(ergebnis, "pruefung")) {
                for (auto const &v : *pr) {
                    auto o = v.if_object();
                    fehler = fehler || (o && str(*o, "bearbeitung") == id && str(*o, "stufe") == "fehler");
                }
            }
            if (fehler) zeile(TR("Zeit"), TR("⛔ so nicht fräsbar – siehe Hinweise"), "hirch-fehler");
            else zeile(TR("Zeit"), zeit(num(*st, "minuten", 0)), "hirch-haupt");
            // live: passt das Werkzeug nicht (Gewinde), steht statt der Zeit die Warnung
            if (auto zl = dynamic_cast<Gtk::Label *>(karte->get_child_at(1, r - 1)); zl && !fehler) {
                auto text = zl->get_text().raw();
                f.neu.push_back([F, zl, text] {
                    zl->set_text(F->ungueltig ? std::string(TR("⛔ so nicht fräsbar")) : text);
                    if (F->ungueltig) klasse(*zl, "hirch-fehler");
                    else zl->get_style_context()->remove_class("hirch-fehler");
                });
            }
        }
        s->add(*karte);
        if (!st) {
            s->add(*hinweis_label(ergebnis_aktuell ? TR("Diese Bearbeitung erzeugt zurzeit keine Fräsbahnen.")
                                                   : TR("Fräsweg und Zeit erscheinen, sobald neu gerechnet ist.")));
        }
        int befunde = 0;
        // Warnungen der Rechnung zu dieser Bearbeitung (z. B. «kein Gewindefräser – ausgelassen»)
        if (auto ow = ergebnis_aktuell ? unter(ergebnis, "op_warnungen") : nullptr) {
            if (auto ws = ow->if_contains(id); ws && ws->is_array()) {
                for (auto const &v : ws->as_array()) {
                    if (!v.is_string()) continue;
                    if (!befunde) s->add(*label(TR("HINWEISE"), "hirch-kopf"));
                    auto l = label("⚠ " + std::string(v.get_string()), "hirch-warn");
                    l->set_line_wrap(true);
                    l->set_max_width_chars(40);
                    klasse(*l, "hirch-klein");
                    s->add(*l);
                    ++befunde;
                }
            }
        }
        if (ergebnis_aktuell) {
            if (auto pr = liste_von(ergebnis, "pruefung")) {
                for (auto const &v : *pr) {
                    auto o = v.if_object();
                    if (!o || str(*o, "bearbeitung") != id) continue;
                    if (!befunde) s->add(*label(TR("HINWEISE"), "hirch-kopf"));
                    bool fehler = str(*o, "stufe") == "fehler";
                    if (!fehler) {
                        s->add(*hinweis_widget(str(*o, "text"), o, quittiert_lesen(), 40));
                        ++befunde;
                        continue;
                    }
                    auto l = label("⛔ " + str(*o, "text"), "hirch-fehler");
                    l->set_line_wrap(true);
                    l->set_max_width_chars(40);
                    klasse(*l, "hirch-klein");
                    s->add(*befund_widget(*o, l));
                    ++befunde;
                }
            }
            if (!befunde) s->add(*hinweis_label(TR("✓ Die Prüfung hat bei dieser Bearbeitung nichts gefunden.")));
        }
        // Vorschau zum Aufklappen ganz unten
        if (auto ex = klappe->get_parent()) s->reorder_child(*ex, -1);
    }

    dlg->add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
    if (entfernbar(b)) dlg->add_button(TR("Entfernen"), 1);
    if (auto sel = panel.getSelection(); sel && !sel->isEmpty()) dlg->add_button(TR("Auswahl zuweisen"), 2);
    klasse(*dlg->add_button(TR("Übernehmen"), Gtk::RESPONSE_OK), "suggested-action");
    dlg->set_default_response(Gtk::RESPONSE_OK);
    hirch_werkzeug_pflicht(*dlg, *F->werkzeug);
    wbtn->signal_clicked().connect([this, F] {
        auto alt = F->werkzeug->get_active_id().raw();
        auto altf = F->flach ? F->flach->get_active_id().raw() : std::string();
        auto alts = F->swerkzeug ? F->swerkzeug->get_active_id().raw() : std::string();
        werkzeuge_dialog();
        F->baut = true;
        werkzeug_combo(*F->werkzeug, alt, false, F->spitze != nullptr);
        if (F->flach) werkzeug_combo(*F->flach, altf, true);
        if (F->swerkzeug) {
            werkzeug_combo(*F->swerkzeug, alts, false);
            F->swerkzeug->prepend("", TR("gleiches wie beim Schruppen"));
            F->swerkzeug->set_active_id(alts);
        }
        F->baut = false;
        F->alle();
    });
    std::function<void(bj::object &)> auslesen = [&](bj::object &b) {
        b["werkzeug"] = f.werkzeug->get_active_id().raw();
        if (f.maxt) b["max_tiefe"] = wert(f.maxt);
        if (f.vzust) b["zustellung"] = f.zart && f.zart->art() == "eins" ? 0.0 : wert(f.vzust);
        if (f.dick) b["aufdicken"] = wert(f.dick);
        if (f.flach) b["flach_werkzeug"] = f.flach->get_active_id().raw();
        if (f.durch && f.durch->get_active()) {
            b["tiefe"] = nullptr;
        } else if (f.tiefe) {
            b["tiefe"] = wert(f.tiefe);
        }
        if (f.zust) b["zustellung"] = wert(f.zust);
        if (f.zart) f.zart->lesen_in(b);
        if (f.starttiefe) b["starttiefe"] = wert(f.starttiefe);
        if (f.bahn) b["bahnabstand"] = (int)wert(f.bahn);
        if (f.seite) b["seite"] = f.seite->get_active_id().raw();
        if (f.richtung) b["gleichlauf"] = f.richtung->get_active_id() == "1";
        if (f.spitze) b["spitze_dazu"] = f.spitze->get_active();
        if (f.aufmass) b["aufmass"] = wert(f.aufmass);
        if (f.eintauchen) b["eintauchen_art"] = f.eintauchen->get_active_id().raw();
        if (f.rwinkel) b["rampe_winkel"] = wert(f.rwinkel);
        // Vorschub in mm/min (nichts = wie das Werkzeug); alte Prozentwerte weg
        if (v_haupt) {
            b["vorschub_mm"] = v_haupt->wert_json();
            b.erase("vorschub_prozent");
        }
        if (vn_haupt) {
            b["vollnut_mm"] = vn_haupt->wert_json();
            b.erase("vollnut");
        }
        if (f.strategie) b["strategie"] = f.strategie->get_active_id().raw();
        if (f.methode) b["methode"] = f.methode->get_active_id().raw();
        if (f.tiefe_ab) {
            auto ab = f.tiefe_ab->get_active_id().raw();
            if (ab == "modell" || ab == "bis_modell" || ab == "haut") b["tiefe_ab"] = ab;
            else b.erase("tiefe_ab");
            if ((ab == "bis_modell" || ab == "haut") && f.tiefe_ab_mm) b["tiefe_ab_mm"] = wert(f.tiefe_ab_mm);
            else b.erase("tiefe_ab_mm");
        }
        if (f.dogbone) b["dogbone"] = f.dogbone->get_active();
        if (f.prichtung) b["richtung"] = f.prichtung->get_active_id().raw();
        if (f.gart) {
            b["innen"] = f.gart->get_active_id() == "innen";
            if (f.gsteig_art->get_active_id() == "eigen" && wert(f.gsteig) > 1e-9) b["steigung"] = wert(f.gsteig);
            else b["steigung"] = nullptr;
            b["rechts"] = f.gdreh->get_active_id() == "rechts";
            b["durchgaenge"] = (int)wert(f.gdurch);
            b["passung"] = wert(f.gpass);
            b["kernloch_werkzeug"] = f.gkern->get_active_id().raw();
        }
        if (f.ueber) {
            if (wert(f.ueber) > 1e-9) b["ueberstand"] = wert(f.ueber);
            else b["ueberstand"] = nullptr;
        }
        if (f.zweck) {
            b["fase"] = f.zweck->get_active_id() == "fase";
            b["fase_breite"] = wert(f.fbreite);
        }
        if (f.anfahren) {
            b["anfahren"] = f.anfahren->get_active_id().raw();
            b["anfahr_radius"] = wert(f.aradius);
        }
        if (f.alast) b["adaptiv_last"] = wert(f.alast);
        if (f.rwerkzeug) b["raeum_werkzeug"] = f.rwerkzeug->get_active_id().raw();
        if (f.rzust) b["raeum_zustellung"] = wert(f.rzust);
        if (f.schlichten) {
            b["schlichten"] = f.schlichten->get_active();
            b["schlicht_zugabe"] = wert(f.szugabe);
            b["schlicht_werkzeug"] = f.swerkzeug->get_active_id().raw();
            // «ganze Tiefe» bleibt leer, solange die Zahl nicht geändert ist (sonst «geändert» bei So fräse ich)
            if (f.szust_ganz >= 0 && std::abs(wert(f.szust) - f.szust_ganz) < 1e-9) b["schlicht_zustellung"] = nullptr;
            else b["schlicht_zustellung"] = wert(f.szust);
            if (f.stiefe_art && f.stiefe_art->get_active_id() == "eigen") b["schlicht_tiefe"] = wert(f.stiefe);
            else b.erase("schlicht_tiefe");
            if (f.srichtung) {
                auto r = f.srichtung->get_active_id().raw();
                if (r.empty()) b.erase("schlicht_gleichlauf");
                else b["schlicht_gleichlauf"] = r == "1";
            }
            if (v_schl) {
                b["schlicht_vorschub_mm"] = v_schl->wert_json();
                b.erase("schlicht_vorschub");
            }
        }
        if (f.smodus) b["stege_modus"] = f.smodus->get_active_id().raw();
        if (sst) b["stege_modus"] = sst->modus;
        if (f.sanz) b["stege_anzahl"] = (int)wert(f.sanz);
        if (f.sbreit) b["stege_breite"] = wert(f.sbreit);
        if (f.shoch) b["stege_hoehe"] = wert(f.shoch);
        if (vfaktor) {
            double vf = wert(vfaktor);
            if (std::abs(vf - 100) < 0.5) b.erase("vorschub_faktor");
            else b["vorschub_faktor"] = (int)std::lround(vf);
        }
    };
    live->auslesen = auslesen;
    if (sst) sst->auslesen = auslesen;
    // Keine Rechnung bei jeder Änderung: die Sofort-Vorschau zeigt Schnitt und Änderungen,
    // genau gerechnet wird mit «Genau rechnen» oder beim Übernehmen
    auto start = std::make_shared<bj::object>();
    auto minuten_alt = std::make_shared<double>(-1);
    if (ergebnis_aktuell) {
        if (auto sts = unter(ergebnis, "bearbeitungen")) {
            if (auto st = unter(*sts, id.c_str())) *minuten_alt = num(*st, "minuten", 0);
        }
    }
    auto start_n = std::make_shared<int>(1);
    f.neu.push_back([this, F, b, start, aend_box, aend_zeit, auslesen, minuten_alt, start_n, werte_jetzt, vorg_box] {
        if (F->baut) return;
        bj::object jetzt = b;
        auslesen(jetzt);
        if (start->empty()) {
            *start = jetzt;
            *start_n = std::get<2>(werte_jetzt());
        }
        int n = aenderungen_zeigen(aend_box, *start, jetzt);
        vorgaben_zeigen(vorg_box, str(jetzt, "art"), jetzt);
        // Zeit grob nachgeführt: Anzahl Durchgänge und Vorschub
        std::string zt;
        if (*minuten_alt > 0) {
            double m = *minuten_alt * std::get<2>(werte_jetzt()) / std::max(1, *start_n);
            double va = num(*start, "vorschub_mm", 0), vj = num(jetzt, "vorschub_mm", 0);
            if (va > 0 && vj > 0) m *= va / vj;
            zt = n == 0 ? TR("Zeit: ") + zeit(*minuten_alt)
                        : TR("Zeit geschätzt: ") + zeit(*minuten_alt) + " → " + zeit(m);
        }
        aend_zeit->set_text(zt);
    });
    f.baut = false;
    f.alle();
    dlg->show_all();
    rad_schuetzen(*dlg->get_content_area());
    // nach run() abmelden: wird der Dialog geschlossen, bevor das Rollen dran war (Oberflächentest
    // unter Windows), griffe es sonst auf die abgebauten Felder zu
    sigc::connection rollen_zu;
    if (teil == "schlicht") {
        rollen_zu = Glib::signal_idle().connect([zeige_schritt] { zeige_schritt(TR("Schlichten")); return false; });
    }
    if (teil == "stege") {
        rollen_zu = Glib::signal_idle().connect([zeige_schritt] { zeige_schritt(TR("Haltestege")); return false; });
    }
    int antwort = dlg->run();
    rollen_zu.disconnect();
    dialog_antwort = antwort;
    live->lebt = false;
    if (sst) {
        *sst->lebt = false;
        sst->auslesen = nullptr;        // hält Verweise auf die Felder des Dialogs
    }
    live->timer.disconnect();
    if (antwort == 1) {
        entfernen(index);
        return;
    }
    if (antwort != Gtk::RESPONSE_OK && antwort != 2) return;
    if (antwort == 2) zuweisen(index);   // «Auswahl zuweisen»: Werte übernehmen und einfärben
    auto titel = f.titel->get_text().raw();
    while (!titel.empty() && std::isspace((unsigned char)titel.back())) titel.pop_back();
    while (!titel.empty() && std::isspace((unsigned char)titel.front())) titel.erase(0, 1);
    bj::object vorher = b;
    b["titel"] = titel;
    auslesen(b);
    if (auto bea2 = liste_von(einst, "bearbeitungen"); bea2 && index < (int)bea2->size()) {
        (*bea2)[index] = b;
        schreiben(aenderung_text(name_von(vorher), vorher, b));      // genaue Bearbeitungshistorie
    }
    if (sst) {
        dlg->hide();                    // eine Rückfrage zu den Stegen nicht über dem alten Dialog
        stege_nach_dialog(b, sst->modus, sst->anzahl, sst->breite, sst->durch);
    }
}

void HirchFraesen::Impl::stege_nach_dialog(bj::object const &b, std::string const &modus, int anzahl_vert,
                                            double breite_vert, bool durch)
{
    // Nach «Übernehmen»: Marken passend machen (Entwurf B). Neu verteilt wird bei geänderter Anzahl
    // (Rückfrage, wenn von Hand verschoben) oder wenn es noch keine Marken gibt; die Breite zieht nur
    // die Kreise nach.
    auto d = doc();
    if (!d) return;
    auto bid = str(b, "id");
    auto marken = steg_marken(bid);
    if (modus == "keine") {
        if (!marken.empty()) {
            stege_entfernen_von(bid);
            DocumentUndo::done(d, TR("Haltestege entfernen"), "");
        }
        return;
    }
    int n = (int)num(b, "stege_anzahl", 4);
    double br = num(b, "stege_breite", 6);
    bool neu = (marken.empty() && durch) || (!marken.empty() && n != anzahl_vert);
    if (neu && !marken.empty() && !stege_rueckfrage(nullptr, steg_verschoben(bid))) neu = false;
    if (neu) {
        stege_verteilen(b, [](int, std::string const &) {});
        return;
    }
    if (!marken.empty() && std::abs(br - breite_vert) > 1e-6) {
        stege_breite_setzen(bid, br);
        DocumentUndo::done(d, TR("Haltestegbreite ändern"), "");
    }
}

} // namespace Inkscape::UI::Dialog
