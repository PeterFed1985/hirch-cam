// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Panel: Ablauf anzeigen, Zuweisen, Neu/Entfernen/Duplizieren, Werkzeugwahl
 */
#include "ui/dialog/hirch-intern.h"
#include "path/path-boolop.h"
#include "ui/tools/tool-base.h"
#include "ui/widget/canvas.h"
#include "helper/geom-pathstroke.h"
#include "ui/dialog/hirch-3d.h"

#include <cstdio>

namespace Inkscape::UI::Dialog {

std::map<void const *, std::map<std::string, bj::object>> &offene_plaetze();   // hirch-panel.cpp
void hirch_werkzeug_fehlt_markieren(Gtk::ComboBoxText &c, std::string const &fehlt_id);   // hirch-panel.cpp

void HirchFraesen::Impl::anzeigen()
{
    platte_merken(doc(), platte, zweiseitig() ? 2 : 1);   // für die 3D-Teile (hirch-3d.cpp)
    if (zg.aktiv) {            // nicht unter der Maus wegbauen – nach dem Loslassen
        anzeigen_wartet = true;
        return;
    }
    // ein angefangener Klick gilt nicht mehr: die Zeile, die das Loslassen melden würde,
    // wird gleich ersetzt (sonst begann das Ziehen später ohne gedrückte Taste)
    zg.gedrueckt = false;
    zg.quell_widget = nullptr;
    zg.quelle.clear();
    anzeigen_wartet = false;
    baut_ui = true;
    bool zwei = zweiseitig();
    if (!zwei) seite_aktiv = 1;
    // Material in einer Zeile
    if (auto m = unter(analyse, "material")) {
        mat_groesse.set_text(zahl(num(*m, "breite", 0), 1) + " × " + zahl(num(*m, "hoehe", 0), 1) +
                             " × " + zahl(num(*m, "dicke", 18), 1) + " mm");
    } else {
        mat_groesse.set_text("–");
    }
    {
        std::string werkstoff;
        auto mid = str(einst, "material", "hartholz");
        if (auto l = liste_von(analyse, "materialien")) {
            for (auto const &v : *l) {
                if (auto a = v.if_array(); a && a->size() == 2 && (*a)[0].is_string() &&
                                           std::string((*a)[0].get_string()) == mid && (*a)[1].is_string()) {
                    werkstoff = std::string((*a)[1].get_string());
                }
            }
        }
        bj::object np;
        if (auto n = unter(einst, "nullpunkt")) np = *n;
        std::string t = werkstoff.empty() ? std::string() : werkstoff + " · ";
        t += str(np, "z", "oben") == "unten" ? TR("Z0 Tisch") : TR("Z0 oben");
        t += TR(" · Null ") + nullpunkt_name(str(np, "xy", "ul"));
        double dx = num(np, "dx", 0), dy = num(np, "dy", 0);
        if (std::abs(dx) > 1e-9 || std::abs(dy) > 1e-9) t += " + " + kurzzahl(dx) + " / " + kurzzahl(dy);
        if (long n = (long)num(analyse, "spannmittel", 0)) t += " · " + anzahl(n, hirch_englisch() ? "clamp" : "Spannmittel", hirch_englisch() ? "clamps" : "Spannmittel");
        if (zwei) {
            auto z = unter(einst, "zweiseitig");
            t += std::string(TR(" · zweiseitig, wenden um ")) + (z && str(*z, "achse", "x") == "y" ? "Y" : "X");
            if (z && flag(*z, "passloecher", true)) t += TR(" · Passstifte");
        }
        mat_zeile.set_text(t);
    }

    bool mit_vor = vorrichtung_an();
    {
        // Knopfzeile unter den Reitern: immer da, sobald es eine Zeichnung gibt
        platten_box.set_visible(doc() != nullptr);
        bool mehr = platten > 1 && platte != 0;
        platte_text.set_visible(mehr);
        platte_zurueck.set_visible(mehr);
        platte_vor.set_visible(mehr);
        if (mehr) {
            platte_text.set_text(TR("Platte ") + std::to_string(platte) + TR(" von ") + std::to_string(platten));
            platte_zurueck.set_sensitive(platte > 1);
            platte_vor.set_sensitive(platte < platten);
        }
        bool baut_alt = baut_ui;
        baut_ui = true;
        zf_vor.set_visible(mit_vor);
        zf_vor.set_active(platte == 0);
        baut_ui = baut_alt;
        umriss_btn.set_visible(mit_vor && platte != 0);
        zf_groesse.set_visible(platte != 0);
        zf_rueck.set_sensitive(!zwei);
        zf_vorr.set_sensitive(!vorrichtung_seite(doc()));
        zf_weg.set_sensitive(platte == 0 || platte > 1 || (zwei && seite_aktiv == 2 && platten <= 1));
    }
    vor_hinweis.set_visible(platte == 0);
    auto ab = ablauf_daten();
    // Seiten-Knöpfe mit Zeit je Seite
    seiten_box.set_visible(zwei);
    if (zwei) {
        for (auto &b_ : seite_btn) b_.show();
        for (int i = 0; i < 2; ++i) {
            std::string t = TR("Seite ") + std::to_string(i + 1);
            if (ab && (ergebnis_aktuell || !zeit_cache.empty())) {
                double m = seiten_minuten(i + 1);
                if (m > 0) t += "   " + kurzzeit(m);
            }
            seite_btn[i].set_label(t);
            seite_btn[i].set_active(platte != 0 && seite_aktiv == i + 1);   // Vorrichtung: kein Reiter aktiv
        }
    }
    ablauf_titel.set_text(zwei ? TR("ABLAUF SEITE ") + std::to_string(seite_aktiv) : std::string(TR("ABLAUF")));
    // Hinweis «Vorher: Werkstück wenden» nicht mehr in der Leiste (Peter, 23.10.: nimmt nur Höhe weg; er
    // steht im Einrichteblatt und in der Zusammenfassung)
    bool wenden = false;
    wenden_box.set_visible(false);
    if (wenden) {
        auto z = unter(einst, "zweiseitig");
        bool um_y = z && str(*z, "achse", "x") == "y";
        bool stifte = z && flag(*z, "passloecher", true);
        wenden_text.set_markup(std::string(TR("<b>Vorher: Werkstück um die ")) + (um_y ? "Y" : "X") + TR("-Achse wenden</b>\n") +
                               "<small>" + (stifte ? TR("Auf die Passstifte legen, ") : TR("An den Anschlag legen, ")) +
                               TR("Z auf der neuen Oberfläche nullen.</small>"));
        for (auto c : wenden_box.get_children()) c->show();
    }

    // Liste: je Werkzeug (Abschnitt) eine Gruppe
    zuweisen_knoepfe.clear();
    UI::remove_all_children(liste);
    op_listen.clear();
    luecken_zu.clear();
    zg.luecke = nullptr;
    std::set<std::string> im_ablauf;
    std::map<int, std::pair<Gtk::ListBox *, Gtk::ListBoxRow *>> letzte_zeile;   // je Bearbeitung
    int n = 0;
    if (ab) {
        if (auto seiten = liste_von(*ab, "seiten")) {
            for (auto const &sv : *seiten) {
                auto so = sv.if_object();
                if (!so) continue;
                int seite = (int)num(*so, "seite", 1);
                auto abs = liste_von(*so, "abschnitte");
                if (!abs) continue;
                for (auto const &av : *abs) {
                    auto a = av.if_object();
                    if (!a) continue;
                    if (auto ops = liste_von(*a, "ops")) {
                        for (auto const &ov : *ops) {
                            if (auto o = ov.if_object()) im_ablauf.insert(str(*o, "id"));
                        }
                    }
                    if (zwei && seite != seite_aktiv) continue;
                    // Umspannen (Entwurf C): Zeile zwischen Spannung 1 und 2, wie beim Wenden
                    if (flag(*a, "nach_umspannen", false)) liste.add(*umspannen_zeile(seite));
                    // rechts: Halt und die Drehzahl, die von Hand einzustellen ist
                    // eine Datei je Werkzeug: kein Halt, sondern die nächste Datei (Steuerung/Maschine)
                    std::string rechts = !flag(*a, "wechsel", false) ? std::string()
                                         : flag(*ab, "eine_datei", true) ? std::string(TR("Halt zum Wechseln"))
                                                                          : std::string(TR("neue Datei"));
                    if (long n = (long)num(*a, "drehzahl", 0)) {
                        std::string u = ganz((double)n) + TR(" U/min");
                        if (flag(*a, "reibt", false)) u = "⚠ " + u;
                        rechts = rechts.empty() ? u : rechts + " · " + u;
                    }
                    if ((int)num(*a, "spannung", 1) == 2)
                        rechts = rechts.empty() ? std::string(TR("Spannung 2")) : std::string(TR("Spannung 2")) + " · " + rechts;
                    auto lb = gruppe("T" + std::to_string((long)num(*a, "t", 1)), str(*a, "werkzeug"), rechts, str(*a, "slot"));
                    if (auto ops = liste_von(*a, "ops")) {
                        int vorher = -2;
                        for (auto const &ov : *ops) {
                            auto o = ov.if_object();
                            if (!o) continue;
                            int idx = index_von(str(*o, "id"));
                            int unter = 0;
                            if (!str(*o, "teil").empty() && idx >= 0) unter = (vorher == idx) ? 1 : 2;
                            auto w = op_zeile(o, idx >= 0 ? bearbeitung(idx) : nullptr, idx, unter);
                            lb->add(*w);
                            if (idx >= 0) letzte_zeile[idx] = {lb, dynamic_cast<Gtk::ListBoxRow *>(w)};
                            vorher = idx;
                            ++n;
                        }
                    }
                }
            }
        }
    }
    // Bearbeitungen, die (noch) nicht im gerechneten Ablauf stehen – ausgeschaltet,
    // gerade eingeschaltet oder noch in Rechnung – bleiben an ihrem Platz in der Liste:
    // bei der Bearbeitung davor bzw. danach mit demselben Werkzeug, sonst in einer
    // eigenen grauen Gruppe an derselben Stelle. «Nicht gefräst»: nur ohne Objekte.
    platte_leer = false;
    if (auto bea = liste_von(einst, "bearbeitungen")) {
        Gtk::ListBox *lb = nullptr;
        std::vector<int> ohne;          // «Nicht gefräst»: erst am Schluss, und nur auf einer Platte mit Inhalt
        auto seite_von = [](bj::object const &b) { return std::max(1, (int)num(b, "auf_seite", 1)); };
        for (std::size_t i = 0; i < bea->size(); ++i) {
            auto b = (*bea)[i].if_object();
            if (!b || im_ablauf.count(str(*b, "id"))) continue;
            if (zwei && seite_von(*b) != seite_aktiv) continue;
            bool hat_objekte = ohne_objekte(str(*b, "art")) || objekte(str(*b, "id")) > 0;
            if (hat_objekte) {
                auto wz = str(*b, "werkzeug");
                auto nachbar = [&](int von, int schritt) -> int {
                    for (int j = von; j >= 0 && j < (int)bea->size(); j += schritt) {
                        auto it = letzte_zeile.find(j);
                        auto bj_ = (*bea)[j].if_object();
                        if (it == letzte_zeile.end() || !bj_ || (zwei && seite_von(*bj_) != seite_aktiv)) continue;
                        if (it->second.first && it->second.second) return j;
                    }
                    return -1;
                };
                int pj = nachbar((int)i - 1, -1), nj = nachbar((int)i + 1, 1);
                auto wz_von = [&](int j) { return str((*bea)[j].as_object(), "werkzeug"); };
                auto erste_zeile = [&](Gtk::ListBox *l, int j) {
                    int pos = 1 << 30;
                    for (auto c : l->get_children()) {
                        auto r = dynamic_cast<Gtk::ListBoxRow *>(c);
                        if (r && r->get_name() != "pass" && std::atoi(r->get_name().c_str()) == j)
                            pos = std::min(pos, r->get_index());
                    }
                    return pos == (1 << 30) ? -1 : pos;
                };
                Gtk::ListBox *ziel = nullptr;
                int pos = -1;
                if (pj >= 0 && wz_von(pj) == wz) {
                    ziel = letzte_zeile[pj].first;
                    pos = letzte_zeile[pj].second->get_index() + 1;
                } else if (nj >= 0 && wz_von(nj) == wz) {
                    ziel = letzte_zeile[nj].first;
                    pos = erste_zeile(ziel, nj);
                } else if (pj >= 0 || nj >= 0 || !werkzeug(wz)) {
                    // eigenes Werkzeug: graue Gruppe hinter der Gruppe davor (bzw. vor der danach).
                    // Fehlt das Werkzeug in der Zeichnung, wird nichts gerechnet – das sagt die Gruppe
                    // (Befund 22.10.: sonst stand dort «wird gerechnet …» ohne Ende).
                    Gtk::Widget *alt = nullptr;
                    if (pj >= 0 || nj >= 0) alt = (pj >= 0 ? letzte_zeile[pj].first : letzte_zeile[nj].first)->get_parent();
                    std::string zustand = !flag(*b, "aktiv", true) ? TR("ausgeschaltet")
                                          : !werkzeug(wz)         ? TR("nicht gerechnet – Doppelklick, Werkzeug wählen")
                                          : neu_offen             ? TR("wartet auf «Übernehmen»")
                                                                  : TR("wird gerechnet …");
                    auto eigen = gruppe("", wz_titel(wz), zustand);
                    int stelle = -1, k = 0;
                    for (auto c : liste.get_children()) {
                        if (alt && c == alt) stelle = k + (pj >= 0 ? 1 : 0);
                        ++k;
                    }
                    if (auto karte = eigen->get_parent()) liste.reorder_child(*karte, stelle);
                    ziel = eigen;
                    pos = -1;
                }
                if (ziel) {
                    auto w = op_zeile(nullptr, b, (int)i);
                    ziel->insert(*w, pos);
                    letzte_zeile[(int)i] = {ziel, dynamic_cast<Gtk::ListBoxRow *>(w)};
                    ++n;
                    continue;
                }
            }
            ohne.push_back((int)i);
        }
        // Eine leere Platte (mehrere Platten, hier nichts gezeichnet) zeigt keinen Ablauf und
        // kein «Jetzt rechnen», nur einen Hinweis (Peter, 25.10.)
        platte_leer = n == 0 && platten > 1 && platte >= 1 && !zwei;
        // Auf der Vorrichtung gehören die Bearbeitungen ohne Objekte zum Werkstück: nicht zeigen (Peter, 26.10.)
        if (!platte_leer && platte != 0) {
            for (int i : ohne) {
                if (!lb) lb = gruppe("", TR("Nicht gefräst"), TR("ohne Objekte"));
                lb->add(*op_zeile(nullptr, (*bea)[i].if_object(), i));
                ++n;
            }
        }
    }
    liste.show_all();
    liste.set_visible(n > 0);
    liste_leer.set_text(platte_leer ? TR("Auf dieser Platte ist noch nichts zu fräsen. Zeichne hier oder schiebe "
                                         "Objekte auf die Platte und färbe sie in einer Fräsfarbe ein.")
                                    : TR("Noch nichts zu fräsen. Linien in einer Fräsfarbe einfärben "
                                         "(Leiste rechts auf der Zeichenfläche) – Rot V-Carve, Orange Gravur, Grün Tasche, "
                                         "Blau Kontur, Violett Bohren, Türkis Gewinde."));
    liste_leer.set_visible(hat_analyse && n == 0);
    tipp.set_visible(n > 0);
    for (auto lb : op_listen) {
        for (auto c : lb->get_children()) {
            auto row = dynamic_cast<Gtk::ListBoxRow *>(c);
            if (row && gewaehlt_index >= 0 && row->get_name() == std::to_string(gewaehlt_index)) {
                lb->select_row(*row);
                break;
            }
        }
    }
    strahl.queue_draw();
    strahl.set_visible(ab != nullptr);
    werkzeug_reiter_auffrischen();
    HirchFarbleiste::alle_auffrischen(panel.getDesktop());

    // Warnungen
    UI::remove_all_children(warnbox);
    std::vector<std::string> warn;
    auto sammeln = [&warn](bj::object const &o, char const *k) {
        if (auto w = liste_von(o, k)) {
            for (auto const &z : *w) {
                if (!z.is_string()) continue;
                std::string s(z.get_string());
                if (s.rfind(TR("Nichts zu fräsen"), 0) == 0) continue;   // steht schon in der Liste
                if (std::find(warn.begin(), warn.end(), s) == warn.end()) warn.push_back(s);
            }
        }
    };
    if (ergebnis_aktuell) {
        sammeln(ergebnis, ergebnis.contains("warnungen_allgemein") ? "warnungen_allgemein" : "warnungen");
    } else {
        sammeln(analyse, "hinweise");
    }
    // Prüfung vor dem Fräsen: Fehler (rot) bleiben sichtbar, Hinweise stehen
    // zugeklappt in einer Zeile «⚠ n Hinweise»; Klick zeigt die Stelle
    int befunde = 0, hinweise = 0, erledigt = 0;
    auto q = quittiert_lesen();
    auto klapp = Gtk::make_managed<Gtk::Expander>();
    klapp->set_expanded(hinweise_offen);
    klapp->property_expanded().signal_changed().connect([this, klapp] { hinweise_offen = klapp->get_expanded(); });
    auto klapp_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);
    klapp_box->set_margin_top(4);
    klapp->add(*klapp_box);
    if (ergebnis_aktuell) {
        if (auto pr = liste_von(ergebnis, "pruefung")) {
            for (auto const &v : *pr) {
                auto o = v.if_object();
                if (!o) continue;
                bool fehler = str(*o, "stufe") == "fehler";
                ++befunde;
                if (fehler) {
                    // Fehler lassen sich nicht abhaken – sie verschwinden, wenn die Ursache behoben ist
                    auto l = label("⛔ " + str(*o, "text"), "hirch-fehler");
                    l->set_line_wrap(true);
                    klasse(*l, "hirch-klein");
                    warnbox.add(*befund_widget(*o, l));
                    continue;
                }
                klapp_box->add(*hinweis_widget(str(*o, "text"), o, q));
                ++hinweise;
                if (q.count(str(*o, "text"))) ++erledigt;
            }
        }
    }
    for (auto const &w : warn) {
        klapp_box->add(*hinweis_widget(w, nullptr, q));
        ++hinweise;
        if (q.count(w)) ++erledigt;
    }
    if (hinweise > 0) {
        int offen = hinweise - erledigt;
        auto kl = offen == 0 ? label("✓ " + anzahl(hinweise, TR("Hinweis"), TR("Hinweise")) + TR(" gelesen"), "hirch-ok")
                : erledigt == 0 ? label("⚠ " + anzahl(hinweise, TR("Hinweis"), TR("Hinweise")), "hirch-warntext")
                                : label("⚠ " + std::to_string(offen) + TR(" von ") +
                                            anzahl(hinweise, TR("Hinweis"), TR("Hinweisen")) + TR(" offen"), "hirch-warntext");
        klapp->set_label_widget(*kl);
        warnbox.add(*klapp);
    }
    warnbox.show_all();
    warnbox.set_visible(!warn.empty() || befunde > 0);

    baut_ui = false;
    zuweisen_auffrischen();
    anzeigen_status();
}

void HirchFraesen::Impl::zuweisen_auffrischen()
{
    auto sel = panel.getSelection();
    std::vector<std::string> farben;
    if (sel) {
        for (auto item : sel->items()) {
            auto css = sp_repr_css_attr(item->getRepr(), "style");
            farben.push_back(Glib::ustring(sp_repr_css_property(css, "stroke", "")).lowercase().raw());
            sp_repr_css_attr_unref(css);
        }
    }
    int n = (int)farben.size();
    for (auto const &k : zuweisen_knoepfe) {
        auto f = Glib::ustring(k.farbe).lowercase().raw();
        bool alle = n > 0 && std::all_of(farben.begin(), farben.end(), [&](std::string const &x) { return x == f; });
        bool zeigen = n > 0 && !alle;
        if (zeigen) {
            k.zahl->set_text(std::to_string(n));
            k.knopf->set_tooltip_text(n == 1 ? TRF("Das ausgewählte Objekt «{}» zuweisen", {k.name})
                                             : TRF("Die {} ausgewählten Objekte «{}» zuweisen",
                                                   {std::to_string(n), k.name}));
        }
        k.knopf->set_opacity(zeigen ? 1.0 : 0.0);
        k.knopf->set_sensitive(zeigen);
        if (!zeigen) k.knopf->set_has_tooltip(false);
    }
    // Peter, 23.10.: Klick auf ein Objekt in der Zeichnung rollt sanft zu seiner Bearbeitung –
    // aber nicht, wenn die Auswahl aus der Liste selbst kommt (Maus über der Leiste): sonst rutscht die Zeile
    // unter dem Doppelklick weg
    bool maus_in_liste = false;
    if (auto top = scroll.get_toplevel(); top && top->get_window()) {
        if (auto seat = Gdk::Display::get_default()->get_default_seat()) {
            int wx = 0, wy = 0, tx = 0, ty = 0;
            Gdk::ModifierType m;
            if (scroll.translate_coordinates(*top, 0, 0, wx, wy)) {
                top->get_window()->get_device_position(seat->get_pointer(), tx, ty, m);
                auto a_ = scroll.get_allocation();
                maus_in_liste = tx >= wx && ty >= wy && tx < wx + a_.get_width() && ty < wy + a_.get_height();
            }
        }
    }
    if (!maus_in_liste && !farben.empty() && !farben[0].empty()) {
        for (auto const &k : zuweisen_knoepfe) {
            if (Glib::ustring(k.farbe).lowercase().raw() == farben[0]) {
                hin_rollen(k.knopf);
                break;
            }
        }
    }
}

void HirchFraesen::Impl::hin_rollen(Gtk::Widget *w)
{
    auto zeile = w ? w->get_ancestor(GTK_TYPE_LIST_BOX_ROW) : nullptr;
    auto kind = scroll.get_child();
    if (!zeile || !kind || !zeile->get_mapped()) return;
    int x = 0, y = 0;
    if (!zeile->translate_coordinates(*kind, 0, 0, x, y)) return;
    auto adj = scroll.get_vadjustment();
    double oben = adj->get_value(), seite = adj->get_page_size(), h = zeile->get_allocated_height();
    if (y >= oben + 8 && y + h <= oben + seite - 8) return;          // schon ganz zu sehen
    double ziel = std::clamp(y - seite / 3.0, adj->get_lower(), adj->get_upper() - seite);
    rollen.disconnect();
    auto start = oben;
    auto t0 = g_get_monotonic_time();
    rollen = Glib::signal_timeout().connect([this, start, ziel, t0] {
        double t = std::min(1.0, (g_get_monotonic_time() - t0) / 250000.0);   // 250 ms
        double e = 1 - (1 - t) * (1 - t) * (1 - t);                            // weich auslaufen
        scroll.get_vadjustment()->set_value(start + (ziel - start) * e);
        return t < 1.0;
    }, 16);
}

void HirchFraesen::Impl::anzeigen_status()
{
    bool fertig = ergebnis_aktuell && flag(ergebnis, "ok", false);
    if (rechnet) {
        spinner.start();
        spinner.show();
    } else {
        spinner.stop();
        spinner.hide();
    }
    if (rechnet) gesamtzeit.get_style_context()->add_class("hirch-veraltet");
    else gesamtzeit.get_style_context()->remove_class("hirch-veraltet");
    if (fertig) {
        gesamtzeit.set_text(zeit(num(ergebnis, "minuten", 0)));
    } else if (!zeit_cache.empty()) {
        // sofort nachgeführt (z. B. Bearbeitung ausgeschaltet), bis neu gerechnet ist
        gesamtzeit.set_text(zeit(seiten_minuten(0)));
    } else if (!rechnet) {
        gesamtzeit.set_text("–");
    }
    std::string s;
    if (!fehlertext.empty()) {
        s = fehlertext;
    } else if (rechnet) {
        s = TR("Rechnet …");
    } else if (wartet_knopf) {
        s = TR("Noch nicht gerechnet");
    } else if (ergebnis_aktuell && !flag(ergebnis, "ok", false) && liste_von(ergebnis, "kollisionen")) {
        s = TR("Rechnung beendet – Kollision");
    } else if (fertig && flag(ergebnis, "entwurf", false)) {
        s = TR("Entwurf (3D gröber) · beim Speichern wird genau gerechnet");
    } else if (fertig) {
        auto d = liste_von(ergebnis, "dateien");
        long n = d ? (long)d->size() : 0;
        long wechsel = 0;
        if (auto ab = unter(ergebnis, "ablauf")) {
            if (auto seiten = liste_von(*ab, "seiten")) {
                for (auto const &sv : *seiten) {
                    auto so = sv.if_object();
                    if (!so) continue;
                    if (auto abs = liste_von(*so, "abschnitte")) {
                        for (auto const &av : *abs) {
                            if (auto a = av.if_object(); a && flag(*a, "wechsel", false)) ++wechsel;
                        }
                    }
                }
            }
        }
        s = (n == 1 ? std::string(TR("1 Datei")) : std::to_string(n) + TR(" Dateien")) +
            (zweiseitig() && n == 2 ? TR(" (Seite 1, Seite 2)") : "") +
            (wechsel ? " · " + anzahl(wechsel, hirch_englisch() ? "tool change" : "Werkzeugwechsel", hirch_englisch() ? "tool changes" : "Werkzeugwechsel") : std::string()) + TR(" · bereit");
        // Endprüfung der fertigen Dateien: auf einen Blick
        if (auto fg = unter(ergebnis, "freigabe")) {
            auto st = str(*fg, "stufe");
            s += st == "ok" ? std::string(TR(" · ✓ Endprüfung")) : st == "fehler" ? std::string(TR(" · ⛔ Endprüfung"))
                                                                               : std::string(TR(" · ⚠ Endprüfung"));
        }
    }
    status.set_text(s);
    {
        auto u = liste_von(ergebnis, "umspannen");
        auto d = liste_von(ergebnis, "dateien");
        long n = d ? (long)d->size() : 0;
        speichern_btn.set_label(fertig && u && !u->empty() && n > 1
                                    ? TRF("G-Code speichern ({} Dateien)", {std::to_string(n)})
                                    : std::string(TR("G-Code speichern")));
    }
    // was ein Ergebnis braucht, rechnet selbst, wenn es wartet (3D, «Automatisch rechnen» aus, Pause)
    bool rechnet_selbst = wartet_knopf || pausiert;
    vorschau_btn.set_sensitive(fertig || rechnet || rechnet_selbst);
    speichern_btn.set_sensitive(fertig || rechnet || rechnet_selbst);
    speichern_mehr.set_sensitive(fertig);
    zusammen_btn.set_sensitive(fertig || rechnet_selbst);
    stick_knopf_auffrischen();
    rechenanzeige();
}

void HirchFraesen::Impl::schreiben(std::string const &was)
{
    auto d = doc();
    if (!d) return;
    einst["version"] = 1;
    auto text = bj::serialize(einst);
    auto alt = d->getReprRoot()->attribute("hirch:einstellungen");
    bool geaendert = !alt || text != alt;
    d->getReprRoot()->setAttribute("hirch:einstellungen", text);
    schnell_bis = g_get_monotonic_time() + 600 * 1000;
    DocumentUndo::done(d, was.c_str(), "");
    // Liste gleich in der neuen Ordnung zeigen (Werkzeug gewechselt usw.), bis gerechnet ist
    if (geaendert) vorlaeufig_bauen();
    // Liste erst nach dem laufenden Signal neu aufbauen (der auslösende
    // Knopf gehört selbst zur Liste)
    neu_zeigen.disconnect();
    neu_zeigen = Glib::signal_idle().connect([this] { anzeigen(); return false; });
}

void HirchFraesen::Impl::ordnen()
{
    auto bea = liste_von(einst, "bearbeitungen");
    if (!bea || bea->size() < 2) return;
    auto stufe = [](bj::object const &b) {
        auto a = str(b, "art");
        return a == "planen" ? 0 : (a == "kontur" ? 2 : 1);
    };
    auto seite = [](bj::object const &b) {
        auto it = b.find("auf_seite");
        if (it != b.end() && it->value().is_int64()) return (int)it->value().as_int64();
        return 1;
    };
    auto wz = [](bj::object const &b) {
        auto it = b.find("werkzeug");
        return it == b.end() ? std::string() : bj::serialize(it->value());
    };
    std::map<std::tuple<int, int, std::string>, int> erstes;
    struct E { int s, st, e, i; };
    std::vector<E> k;
    for (int i = 0; i < (int)bea->size(); ++i) {
        bj::object leer;
        auto o = (*bea)[i].if_object();
        auto const &b = o ? *o : leer;
        auto schl = std::make_tuple(seite(b), stufe(b), wz(b));
        erstes.emplace(schl, i);
        k.push_back({std::get<0>(schl), std::get<1>(schl), erstes[schl], i});
    }
    std::stable_sort(k.begin(), k.end(), [](E const &a, E const &b) {
        return std::tie(a.s, a.st, a.e, a.i) < std::tie(b.s, b.st, b.e, b.i);
    });
    std::vector<int> folge;
    for (auto const &e : k) folge.push_back(e.i);
    // «Böden und Wände» direkt hinter sein 3D-Schruppen (im Wechsel, Peter 25.10.)
    for (int i = 0; i < (int)bea->size(); ++i) {
        auto o = (*bea)[i].if_object();
        if (!o || !ist_boeden_waende(str(*o, "art")) || str(*o, "im_wechsel_mit").empty()) continue;
        int p = -1;
        for (int j = 0; j < (int)bea->size(); ++j)
            if (auto q = (*bea)[j].if_object(); q && str(*q, "id") == str(*o, "im_wechsel_mit")) p = j;
        if (p < 0) continue;
        folge.erase(std::find(folge.begin(), folge.end(), i));
        auto pos = std::find(folge.begin(), folge.end(), p) + 1;
        // hinter schon angehängte «Böden und Wände» desselben Schruppens
        while (pos != folge.end()) {
            auto q = (*bea)[*pos].if_object();
            if (!q || str(*q, "im_wechsel_mit") != str(*o, "im_wechsel_mit")) break;
            ++pos;
        }
        folge.insert(pos, i);
    }
    bj::array neu_;
    bool gleich = true;
    for (int j = 0; j < (int)folge.size(); ++j) {
        neu_.push_back((*bea)[folge[j]]);
        if (folge[j] != j) gleich = false;
    }
    if (gleich) return;
    *bea = std::move(neu_);
    gewaehlt_index = -1;
    schreiben(TR("Ablauf ordnen"));
}

bool HirchFraesen::Impl::auswahl_faerben(std::string const &hex)
{
    auto sel = panel.getSelection();
    auto desk = panel.getDesktop();
    if (!sel || !desk || sel->isEmpty()) return false;
    // Peter, 23.10.: Kommt ein Objekt zu einer Bearbeitung einer ANDEREN Fräsart (z. B. Kreis aus der Tasche
    // bekommt eine Kontur mit Fase), bleibt es in der bisherigen – über «zusätzlich hier fräsen»
    // (hirch:auch). Gleiche Fräsart (in eine andere Tasche) heisst umziehen.
    {
        auto art_von = [this](std::string const &farbe, std::string &id) -> std::string {
            if (auto bea = liste_von(einst, "bearbeitungen")) {
                for (auto const &v : *bea) {
                    auto o = v.if_object();
                    if (o && Glib::ustring(str(*o, "farbe")).lowercase() == Glib::ustring(farbe).lowercase()) {
                        id = str(*o, "id");
                        return str(*o, "art");
                    }
                }
            }
            return {};
        };
        std::string neu_id;
        auto neu_art = art_von(hex, neu_id);
        // Bahnen der bisherigen Bearbeitungen sofort ausblenden (die neue Rechnung läuft im Hintergrund)
        std::function<void(SPItem *)> rahmen = [&](SPItem *it) {
            if (!it) return;
            if (auto g = cast<SPGroup>(it); g && !cast<SPText>(it)) {
                for (auto k : g->item_list()) rahmen(k);
                return;
            }
            WegRahmen w;
            if (it->style && it->style->stroke.isColor()) {
                char alt_hex[16];
                g_snprintf(alt_hex, sizeof alt_hex, "#%06x", it->style->stroke.value.color.toRGBA32(1.0) >> 8);
                std::string alt_id;
                art_von(alt_hex, alt_id);
                if (!alt_id.empty() && alt_id != neu_id) w.ids.insert(alt_id);
            }
            if (neu_id.empty()) {          // Grau: auch «zusätzlich hier fräsen» fällt weg
                for (SPObject *o = it; o && !is<SPRoot>(o); o = o->parent) {
                    if (auto a = o->getAttribute("hirch:auch")) {
                        std::istringstream ein(a);
                        std::string x;
                        while (ein >> x) w.ids.insert(x);
                    }
                }
            }
            auto r = it->desktopVisualBounds();
            if (!w.ids.empty() && r) {
                w.rahmen = *r;
                weg_rahmen.push_back(std::move(w));
            }
        };
        for (auto it : sel->items()) rahmen(it);
        // Peter 02.11.: «Nicht fräsen» (Grau) und Farben ohne Bearbeitung nehmen die Auswahl ganz heraus –
        // auch aus «zusätzlich hier fräsen». Vorher wurde die alte Bearbeitung dort eingetragen und weiter gefräst.
        if (neu_id.empty()) {
            std::function<void(SPItem *)> loesen = [&](SPItem *it) {
                if (!it) return;
                if (it->getAttribute("hirch:auch")) it->removeAttribute("hirch:auch");
                if (auto g = cast<SPGroup>(it); g && !cast<SPText>(it))
                    for (auto k : g->item_list()) loesen(k);
            };
            for (auto it : sel->items()) loesen(it);
        }
        std::function<void(SPItem *)> merken = [&](SPItem *it) {
            if (neu_id.empty()) return;
            if (!it) return;
            if (auto g = cast<SPGroup>(it); g && !cast<SPText>(it)) {
                for (auto k : g->item_list()) merken(k);
                return;
            }
            if (!it->style || !it->style->stroke.isColor()) return;
            char alt_hex[16];
            g_snprintf(alt_hex, sizeof alt_hex, "#%06x", it->style->stroke.value.color.toRGBA32(1.0) >> 8);
            std::string alt_id;
            auto alt_art = art_von(alt_hex, alt_id);
            if (alt_id.empty() || alt_id == neu_id || alt_art == neu_art) return;
            auto a = it->getAttribute("hirch:auch");
            it->setAttribute("hirch:auch", mit(a ? std::string(a) : std::string(), alt_id));
        };
        for (auto it : sel->items()) merken(it);
    }
    SPCSSAttr *css = sp_repr_css_attr_new();
    sp_repr_css_set_property(css, "stroke", hex.c_str());
    sp_repr_css_set_property(css, "fill", "none");
    sp_desktop_set_style(desk, css);
    sp_repr_css_attr_unref(css);
    if (!weg_rahmen.empty()) {
        bahnen_zeichnen();
        // im Hintergrund neu rechnen, sobald die Zeichnung gelesen ist
        Glib::signal_idle().connect_once([this] { if (!weg_rahmen.empty()) rechnen_jetzt(); });
    }
    return true;
}

void HirchFraesen::Impl::neu(std::string const &art)
{
    auto info = art_info(art);
    if (!info) return;
    auto bea = liste_von(einst, "bearbeitungen");
    if (!bea) {
        einst["bearbeitungen"] = bj::array();
        bea = liste_von(einst, "bearbeitungen");
    }
    auto b = vorgabe_bearbeitung(art);
    b["id"] = neue_id();
    b["farbe"] = freier_ton(art);
    if (zweiseitig() && seite_aktiv == 2) b["auf_seite"] = 2;
    // hinter die letzte derselben Fräsart, sonst vor die Kontur, Kontur ans Ende
    int ziel = (int)bea->size(), letzte = -1, kontur = -1;
    for (std::size_t i = 0; i < bea->size(); ++i) {
        if (auto o = (*bea)[i].if_object()) {
            if (str(*o, "art") == art) letzte = (int)i;
            if (kontur < 0 && str(*o, "art") == "kontur") kontur = (int)i;
        }
    }
    if (letzte >= 0) {
        ziel = letzte + 1;
    } else if (art != "kontur" && kontur >= 0) {
        ziel = kontur;
    }
    if (art == "planen") ziel = 0;        // Planfräsen zuerst
    if (ist_boeden_waende(art) && letzte < 0) {
        // hinter das letzte 3D-Schruppen dieser Seite, im Wechsel mit ihm und mit seinem Fräser
        for (std::size_t i = 0; i < bea->size(); ++i) {
            auto o = (*bea)[i].if_object();
            if (o && str(*o, "art") == "relief_schruppen" && !flag(*o, "rest", false) &&
                (int)num(*o, "auf_seite", 1) == (int)num(b, "auf_seite", 1)) {
                ziel = (int)i + 1;
                b["im_wechsel_mit"] = str(*o, "id");
                b["werkzeug"] = str(*o, "werkzeug", "C");
            }
        }
    }
    if (art == "relief_schruppen" && letzte < 0) {      // 3D: Schruppen vor dem Schlichten
        for (std::size_t i = 0; i < bea->size(); ++i) {
            auto o = (*bea)[i].if_object();
            if (o && str(*o, "art") == "relief_schlichten" && (int)i < ziel) {
                ziel = (int)i;
                break;
            }
        }
    }
    if (letzte >= 0) {
        // vorgewählt: das Werkzeug der letzten Bearbeitung dieser Fräsart
        if (auto o = (*bea)[letzte].if_object()) {
            for (char const *k : {"werkzeug", "flach_werkzeug", "kernloch_werkzeug"}) {
                if (auto it = o->find(k); it != o->end() && b.contains(k)) b[k] = it->value();
            }
        }
    }
    bea->insert(bea->begin() + ziel, b);
    // alte Farben merken: «Abbrechen» im Dialog stellt sie wieder her
    neu_stile.clear();
    if (!ohne_objekte(art)) {
        if (auto sel = panel.getSelection(); sel && !sel->isEmpty()) {
            std::function<void(Inkscape::XML::Node *)> merken = [&](Inkscape::XML::Node *n) {
                if (auto i = n->attribute("id")) {
                    auto st = n->attribute("style");
                    neu_stile.emplace_back(i, st ? st : "");
                }
                for (auto c = n->firstChild(); c; c = c->next()) merken(c);
            };
            for (auto item : sel->items()) merken(item->getRepr());
        }
        auswahl_faerben(std::string(b["farbe"].as_string()));
    }
    // erst der Dialog, dann rechnen: er geht auf, sobald die Zeichnung analysiert ist
    // (richtige Objektzahl), spätestens nach zwei Sekunden
    neu_id = str(b, "id");
    neu_offen = true;
    neu_wartet = false;
    neu_ab = generation;
    schreiben(TRF("{} hinzugefügt", {name_von(b)}));
    neu_notfall.disconnect();
    neu_notfall = Glib::signal_timeout().connect([this] { neu_dialog(); return false; }, 2000);
}

void HirchFraesen::Impl::neu_dialog()
{
    neu_notfall.disconnect();
    if (neu_id.empty()) return;
    auto id = neu_id;
    neu_id.clear();
    auto stile = std::move(neu_stile);
    neu_stile.clear();
    int idx = index_von(id);
    dialog_antwort = Gtk::RESPONSE_CANCEL;
    if (idx >= 0) bearbeitung_dialog(idx);
    neu_offen = false;
    bool angenommen = idx >= 0 && (dialog_antwort == Gtk::RESPONSE_OK || dialog_antwort == 2);
    if (!angenommen) {
        // Abbrechen (oder Entfernen): nichts Halbfertiges stehen lassen
        auto d = doc();
        if (d) {
            for (auto const &[sid, stil] : stile) {
                if (auto o = d->getObjectById(sid)) o->getRepr()->setAttributeOrRemoveIfEmpty("style", stil);
            }
        }
        int j = index_von(id);
        if (j >= 0) {
            entfernen(j);                  // schreibt – zusammen mit den Farben ein Schritt
        } else if (d && !stile.empty()) {
            DocumentUndo::done(d, TR("Bearbeitung entfernen"), "");
        }
    }
    // Hat sich danach nichts mehr geändert, die schon analysierte Zeichnung jetzt rechnen;
    // sonst rechnet die Änderung selbst (Zeitgeber «spaeter» läuft)
    Glib::signal_timeout().connect_once([this] {
        if (!neu_wartet || neu_offen) return;
        neu_wartet = false;
        if (neu_gen == generation && !timer.connected() && !rechnet) rechnen(neu_gen, neu_svg);
    }, 150);
}

bj::object *HirchFraesen::Impl::bearbeitung(int index)
{
    auto bea = liste_von(einst, "bearbeitungen");
    if (!bea || index < 0 || index >= (int)bea->size()) return nullptr;
    return (*bea)[index].if_object();
}

bool HirchFraesen::Impl::entfernbar(bj::object const &)
{
    // Peter 28.10.: jede Bearbeitung lässt sich löschen – ihre Objekte werden «keine Bearbeitung» (schwarz) oder
    // gehen in eine andere Bearbeitung, in der sie zusätzlich stehen (entfernen())
    return true;
}

void HirchFraesen::Impl::entfernen(int index)
{
    auto bea = liste_von(einst, "bearbeitungen");
    if (!bea || index < 0 || index >= (int)bea->size()) return;
    auto id = bea->at(index).is_object() ? str(bea->at(index).get_object(), "id") : std::string();
    auto weg_name = bea->at(index).is_object() ? name_von(bea->at(index).get_object()) : std::string();
    auto weg_farbe = bea->at(index).is_object()
                         ? Glib::ustring(str(bea->at(index).get_object(), "farbe")).lowercase().raw()
                         : std::string();
    bea->erase(bea->begin() + index);
    if (!id.empty()) geloeschte_ids.insert(id);
    // Objekte dieser Bearbeitung (gleiche Linienfarbe): in die erste Bearbeitung, in der sie zusätzlich stehen
    // (hirch:auch), sonst schwarz = keine Bearbeitung (Peter 28.10.)
    if (auto d = doc(); d && !weg_farbe.empty()) {
        auto farbe_von = [bea](std::string const &bid) -> std::string {
            for (auto const &v : *bea)
                if (auto o = v.if_object(); o && str(*o, "id") == bid) return str(*o, "farbe");
            return {};
        };
        std::function<void(SPItem *)> umfaerben = [&](SPItem *it) {
            if (!it) return;
            if (auto g = cast<SPGroup>(it); g && !cast<SPText>(it)) {
                for (auto k : g->item_list()) umfaerben(k);
                return;
            }
            if (!it->style || !it->style->stroke.isColor()) return;
            char hex[16];
            g_snprintf(hex, sizeof hex, "#%06x", it->style->stroke.value.color.toRGBA32(1.0) >> 8);
            if (Glib::ustring(hex).lowercase().raw() != weg_farbe) return;
            std::string neu = "#000000", rest;
            if (auto a = it->getAttribute("hirch:auch")) {
                std::istringstream ein(a);
                std::string w;
                while (ein >> w) {
                    if (w == id) continue;
                    if (neu == "#000000" && !farbe_von(w).empty()) {
                        neu = farbe_von(w);       // wird die Hauptbearbeitung – nicht mehr «zusätzlich»
                        continue;
                    }
                    rest += (rest.empty() ? "" : " ") + w;
                }
                it->setAttributeOrRemoveIfEmpty("hirch:auch", rest);
            }
            SPCSSAttr *css = sp_repr_css_attr_new();
            sp_repr_css_set_property(css, "stroke", neu.c_str());
            sp_repr_css_change(it->getRepr(), css, "style");
            sp_repr_css_attr_unref(css);
        };
        for (auto &k : d->getRoot()->children)
            if (auto it = cast<SPItem>(&k)) umfaerben(it);
    }
    // Verweise «zusätzlich hier fräsen» auf diese Bearbeitung entfernen
    if (auto d = doc(); d && !id.empty()) {
        std::function<void(Inkscape::XML::Node *)> putzen = [&](Inkscape::XML::Node *n) {
            for (auto c = n->firstChild(); c; c = c->next()) {
                if (auto a = c->attribute("hirch:auch")) c->setAttributeOrRemoveIfEmpty("hirch:auch", ohne(a, id));
                putzen(c);
            }
        };
        putzen(d->getReprRoot());
    }
    gewaehlt_index = -1;
    vorlaeufig_bauen();        // Liste und Zeitleiste sofort ohne die Bearbeitung
    bahnen_zeichnen();         // ihre Bahnen weg von der Zeichenfläche
    schreiben(weg_name.empty() ? std::string(TR("Bearbeitung entfernen")) : TRF("{} entfernt", {weg_name}));

}

std::string HirchFraesen::Impl::ohne(std::string const &liste, std::string const &id)
{
    std::string aus, w;
    std::istringstream ein(liste);
    while (ein >> w) {
        if (w != id) aus += (aus.empty() ? "" : " ") + w;
    }
    return aus;
}

std::string HirchFraesen::Impl::mit(std::string const &liste, std::string const &id)
{
    auto rest = ohne(liste, id);
    return rest.empty() ? id : rest + " " + id;
}

void HirchFraesen::Impl::auch_setzen(int index, bool hinzu)
{
    auto b = bearbeitung(index);
    auto sel = panel.getSelection();
    auto d = doc();
    if (!b || !sel || sel->isEmpty() || !d) return;
    auto id = str(*b, "id");
    for (auto item : sel->items()) {
        auto a = item->getAttribute("hirch:auch");
        auto alt = a ? std::string(a) : std::string();
        item->setAttributeOrRemoveIfEmpty("hirch:auch", hinzu ? mit(alt, id) : ohne(alt, id));
    }
    DocumentUndo::done(d, hinzu ? TR("Zusätzlich fräsen") : TR("Nicht mehr zusätzlich fräsen"), "");
}

bool HirchFraesen::Impl::auswahl_hat_auch(std::string const &id)
{
    auto sel = panel.getSelection();
    if (!sel) return false;
    for (auto item : sel->items()) {
        if (auto a = item->getAttribute("hirch:auch"); a && ohne(a, id) != std::string(a)) return true;
    }
    return false;
}

void HirchFraesen::Impl::duplizieren(int index)
{
    auto b = bearbeitung(index);
    if (!b) return;
    bj::object kopie = *b;
    kopie["id"] = neue_id();
    kopie["farbe"] = freier_ton(str(*b, "art"));
    auto bea = liste_von(einst, "bearbeitungen");
    bea->insert(bea->begin() + index + 1, kopie);
    bool gefaerbt = auswahl_faerben(std::string(kopie["farbe"].as_string()));
    schreiben(TRF("{} dupliziert", {name_von(*b)}));
    if (!gefaerbt) {
        Glib::signal_idle().connect_once([this, index] { bearbeitung_dialog(index + 1); });
    }
}

void HirchFraesen::Impl::duplizieren_gleich(int index)
{
    auto b = bearbeitung(index);
    auto bea = liste_von(einst, "bearbeitungen");
    if (!b || !bea) return;
    bj::object kopie = *b;
    kopie["id"] = neue_id();
    // B-23 (Peter 03.11.): die Kopie übernimmt nur die Einstellungen, keine Objekte – eigener Farbton, sonst
    // würden dieselben Objekte unbemerkt doppelt gefräst. Die Liste zeigt «keine Objekte – …».
    kopie["farbe"] = freier_ton(str(*b, "art"));
    auto titel = str(*b, "titel");
    if (!titel.empty()) kopie["titel"] = titel + TR(" (Kopie)");
    bea->insert(bea->begin() + index + 1, kopie);
    gewaehlt_index = index + 1;
    schreiben(TRF("{} dupliziert", {name_von(*b)}));
    Glib::signal_idle().connect_once([this, index] { bearbeitung_dialog(index + 1); });
}

std::vector<int> HirchFraesen::Impl::geschwister(int index)
{
    std::vector<int> v;
    auto bea = liste_von(einst, "bearbeitungen");
    auto b = bearbeitung(index);
    if (!bea || !b) return v;
    auto farbe = Glib::ustring(str(*b, "farbe")).lowercase();
    for (int i = 0; i < (int)bea->size(); ++i) {
        auto o = (*bea)[i].if_object();
        if (i != index && o && str(*o, "art") == str(*b, "art") &&
            Glib::ustring(str(*o, "farbe")).lowercase() == farbe && !farbe.empty())
            v.push_back(i);
    }
    return v;
}

void HirchFraesen::Impl::sichtbar_umschalten(int index)
{
    auto b = bearbeitung(index);
    auto d = doc();
    auto ids = unter(analyse, "ids");
    if (!b || !d) return;
    bool neu = !flag(*b, "sichtbar", true);
    (*b)["sichtbar"] = neu;
    if (ids) {
        if (auto l = liste_von(*ids, str(*b, "id").c_str())) {
            for (auto const &id : *l) {
                if (!id.is_string()) continue;
                auto item = cast<SPItem>(d->getObjectById(std::string(id.get_string())));
                if (!item) continue;
                if (neu) {
                    if (item->getAttribute("hirch:versteckt")) {
                        item->setHidden(false);
                        item->removeAttribute("hirch:versteckt");
                    }
                } else if (!item->isHidden()) {
                    item->setHidden(true);
                    item->setAttribute("hirch:versteckt", "1");
                }
            }
        }
    }
    // Ein- und Ausblenden ändert nichts am Fräsen: die nächste Prüfung der Zeichnung rechnet nicht neu
    nur_sichtbar_geaendert = !letzte_xml.empty() && !rechnet;
    schreiben(neu ? TR("Bearbeitung einblenden") : TR("Bearbeitung ausblenden"));
    bahnen_zeichnen();
}

void HirchFraesen::Impl::zuweisen(int index)
{
    if (auto b = bearbeitung(index)) auswahl_faerben(str(*b, "farbe"));
}

bool HirchFraesen::Impl::im_programm(std::string const &key)
{
    if (key.empty() || !ergebnis_aktuell || !flag(ergebnis, "ok", false)) return false;
    if (auto ps = liste_von(ergebnis, "programme"))
        for (auto const &pv : *ps)
            if (auto po = pv.if_object())
                if (auto abs = liste_von(*po, "abschnitte"))
                    for (auto const &av : *abs)
                        if (auto a = av.if_object(); a && str(*a, "key") == key) return true;
    return false;
}

void HirchFraesen::Impl::kontextmenue(int index, GdkEvent *ereignis, std::string const &op_key, std::string const &op_name)
{
    auto b = bearbeitung(index);
    if (!b) return;
    auto menue = Gtk::make_managed<Gtk::Menu>();
    auto eintrag = [&](char const *text, std::function<void()> was, bool aktiv = true) {
        auto item = Gtk::make_managed<Gtk::MenuItem>(text);
        item->set_sensitive(aktiv);
        item->signal_activate().connect([was] { Glib::signal_idle().connect_once(was); });
        menue->append(*item);
    };
    auto sel = panel.getSelection();
    bool auswahl = sel && !sel->isEmpty();
    eintrag(TR("Einstellen …"), [this, index] { bearbeitung_dialog(index); });
    if (str(*b, "art").rfind("relief_", 0) == 0) {
        // 3D (Peter 30.10., B-9): Linien färben hiesse «nicht fräsen» – die gewählten Linien begrenzen die
        // Bearbeitung («Nur innerhalb der Linien», wie der Knopf im Dialog)
        eintrag(TR("Gewählte Linien als Bereich"), [this, index] {
            auch_setzen(index, true);
            if (auto bb = bearbeitung(index)) {
                auto bi = str(*bb, "bereich", "alles");
                if (bi != "innen" && bi != "aussen") {
                    (*bb)["bereich"] = "innen";
                    schreiben(TR("Bereich der Bearbeitung"));
                }
            }
        }, auswahl);
    } else {
        eintrag(TR("Ausgewählte Objekte zuweisen (einfärben)"), [this, index] { zuweisen(index); }, auswahl);
        eintrag(TR("Ausgewählte Objekte zusätzlich hier fräsen"), [this, index] { auch_setzen(index, true); }, auswahl);
    }
    if (auswahl && auswahl_hat_auch(str(*b, "id"))) {
        eintrag(TR("Ausgewählte Objekte nicht mehr zusätzlich hier fräsen"),
                [this, index] { auch_setzen(index, false); });
    }
    eintrag(TR("Objekte auswählen"), [this, index] { objekte_waehlen(index); }, objekte(str(*b, "id")) > 0);
    menue->append(*Gtk::make_managed<Gtk::SeparatorMenuItem>());
    eintrag(TR("Duplizieren"), [this, index] { duplizieren_gleich(index); });
    eintrag(TR("Weitere Bearbeitung dieser Art"), [this, index] { duplizieren(index); });
    eintrag(TR("Entfernen"), [this, index] { entfernen(index); }, entfernbar(*b));
    if (im_programm(op_key)) {
        menue->append(*Gtk::make_managed<Gtk::SeparatorMenuItem>());
        eintrag(TR("Ab hier neu ausgeben …"), [this, op_key, op_name] {
            bj::object ab;
            ab["key"] = op_key;
            ab_hier_ausgeben(bj::serialize(ab), op_name.empty() ? op_key : op_name);
        });
    }
    menue->show_all();
    if (auto f = fenster()) menue->attach_to_widget(*f);
    menue->popup_at_pointer(ereignis);
}

void HirchFraesen::Impl::objekte_waehlen(int index)
{
    auto bea = liste_von(einst, "bearbeitungen");
    auto d = doc();
    auto sel = panel.getSelection();
    if (!bea || !d || !sel || index < 0 || index >= (int)bea->size()) return;
    auto b = (*bea)[index].if_object();
    auto ids = unter(analyse, "ids");
    if (!b || !ids) return;
    std::vector<SPItem *> items;
    if (auto l = liste_von(*ids, str(*b, "id").c_str())) {
        for (auto const &id : *l) {
            if (!id.is_string()) continue;
            if (auto item = cast<SPItem>(d->getObjectById(std::string(id.get_string())))) items.push_back(item);
        }
    }
    if (!items.empty()) sel->setList(items);
}

std::unique_ptr<Gtk::Dialog> HirchFraesen::Impl::dialog(std::string const &titel)
{
    auto dlg = std::make_unique<Gtk::Dialog>(titel, true);
    // über dem Fenster, aus dem es geöffnet wird – auch über einem anderen Dialog (Peter, 24.10.: das
    // Fräserfenster aus der Bearbeitung verschwand hinter ihr, und die Bearbeitung liess sich nicht schliessen)
    Gtk::Window *eltern = nullptr;
    for (auto w : Gtk::Window::list_toplevels()) {
        if (w && w->get_visible() && w->is_active() && dynamic_cast<Gtk::Dialog *>(w)) eltern = w;
    }
    if (!eltern) eltern = fenster();
    if (eltern) dlg->set_transient_for(*eltern);
    dlg->set_resizable(false);
    auto inhalt = dlg->get_content_area();
    inhalt->set_spacing(10);
    inhalt->set_margin_start(18);
    inhalt->set_margin_end(18);
    inhalt->set_margin_top(14);
    inhalt->set_margin_bottom(8);
    return dlg;
}

namespace {
using WzListe = std::vector<WzEintrag>;

WzListe *wz_liste(GObject *o) { return static_cast<WzListe *>(g_object_get_data(o, "hirch-eintraege")); }
} // namespace

void HirchFraesen::Impl::werkzeug_combo(Gtk::ComboBoxText &c, std::string const &aktiv, bool mit_keinem,
                                        bool bohrer)
{
    c.remove_all();
    auto liste = new WzListe;
    if (mit_keinem) liste->push_back({"", TR("keiner – V-Fräser räumt aus"), "", 3, "", {}, 0, ""});
    auto mass = [](bj::object const &w) { return "Ø " + zahl(num(w, "durchmesser", 3)) + TR(" mm"); };
    // erst die Werkzeuge dieser Zeichnung (A, B, C …), dann «Meine Werkzeuge»; was für die Fräsart
    // weniger passt, kommt unter «Weniger passend»
    if (auto ws = unter(einst, "werkzeuge")) {
        std::vector<std::string> keys;
        for (auto const &kv : *ws) keys.push_back(std::string(kv.key()));
        std::sort(keys.begin(), keys.end());
        for (auto const &k : keys) {
            auto w = unter(*ws, k.c_str());
            if (!w) continue;
            auto typ = str(*w, "typ");
            bool v = typ == "v";
            if (mit_keinem && v) continue;          // Boden: nur Schaftfräser
            // Bohrer nur, wo gebohrt wird (gewählt bleibt er sichtbar – die Rechnung warnt)
            if (typ == "bohrer" && !bohrer && k != aktiv) continue;
            liste->push_back({k, k + " · " + werkzeug_name_mit_art(*w), mass(*w),
                              wz_passend(wz_kontext, typ, bohrer) ? 0 : 2, fraeser_gruppe(*w), fraeser_merkmale(*w),
                              num(*w, "durchmesser", 3), k});
        }
    }
    // dahinter die übrigen Fräser aus «Meine Werkzeuge»; gewählt, werden sie ein Platz der Zeichnung
    std::set<std::string> schon;
    if (auto ws = unter(einst, "werkzeuge")) {
        for (auto const &kv : *ws) {
            if (auto w = kv.value().if_object(); w && !flag(*w, "nur_hier", false)) {
                if (auto d = str(*w, "db_id"); !d.empty()) schon.insert(d);
            }
        }
    }
    if (auto l = liste_von(analyse, "datenbank")) {
        for (int runde = bohrer ? 0 : 1; runde < 2; ++runde) {     // wo gebohrt wird, die Bohrer zuerst
            for (auto const &val : *l) {
                auto o = val.if_object();
                if (!o) continue;
                auto id = str(*o, "id");
                bool b = str(*o, "typ") == "bohrer", v = str(*o, "typ") == "v";
                if (id.empty() || schon.count(id) || b != (runde == 0)) continue;
                if ((mit_keinem && v) || (b && !bohrer)) continue;
                liste->push_back({"db:" + id, werkzeug_name_mit_art(*o), mass(*o),
                                  wz_passend(wz_kontext, str(*o, "typ"), bohrer) ? 1 : 2, fraeser_gruppe(*o),
                                  fraeser_merkmale(*o), num(*o, "durchmesser", 3), ""});
            }
        }
    }
    // in der Liste (und damit im geschlossenen Feld) in der Reihenfolge der Gruppen
    std::stable_sort(liste->begin(), liste->end(), [](WzEintrag const &a, WzEintrag const &b) {
        auto rang = [](int g) { return g == 3 ? -1 : g; };
        return rang(a.gruppe) < rang(b.gruppe);
    });
    for (auto const &e : *liste) c.append(e.id, e.name);
    auto obj = G_OBJECT(c.gobj());
    g_object_set_data_full(obj, "hirch-eintraege", liste, [](gpointer p) { delete static_cast<WzListe *>(p); });
    g_object_set_data(obj, "hirch-mit-keinem", GINT_TO_POINTER(mit_keinem ? 1 : 0));
    g_object_set_data(obj, "hirch-bohrer", GINT_TO_POINTER(bohrer ? 1 : 0));
    if (!g_object_get_data(obj, "hirch-pop")) {
        // GTKs Klappmenü legt den gewählten Eintrag unter die Maus (oben bleibt viel leer):
        // stattdessen das eigene Aufklappfeld direkt unter dem Feld
        g_object_set_data(obj, "hirch-pop", this);
        // «event» kommt vor GTKs eigener Behandlung des Knopfs (die die Liste sonst sofort öffnet)
        auto auf = +[](GtkWidget *, GdkEvent *ev, gpointer combo) -> gboolean {
            if (ev->type != GDK_BUTTON_PRESS || ev->button.button != 1) return FALSE;
            auto c_ = Glib::wrap(GTK_COMBO_BOX_TEXT(combo));
            if (!c_->is_sensitive()) return FALSE;
            static_cast<Impl *>(g_object_get_data(G_OBJECT(combo), "hirch-pop"))->werkzeug_aufklappen(*c_);
            return TRUE;
        };
        struct Ziel { GtkWidget *combo; GCallback cb; };
        Ziel z{GTK_WIDGET(c.gobj()), G_CALLBACK(auf)};
        gtk_container_forall(GTK_CONTAINER(c.gobj()), [](GtkWidget *k, gpointer data) {
            auto z_ = static_cast<Ziel *>(data);
            if (GTK_IS_TOGGLE_BUTTON(k)) g_signal_connect(k, "event", z_->cb, z_->combo);
        }, &z);
        // Tastatur (Leertaste, Alt+↓) und alles andere: falls GTKs Liste doch aufgeht, gleich ersetzen
        g_signal_connect(obj, "notify::popup-shown", G_CALLBACK(+[](GObject *o, GParamSpec *, gpointer) {
            gboolean auf_ = FALSE;
            g_object_get(o, "popup-shown", &auf_, nullptr);
            if (!auf_) return;
            gtk_combo_box_popdown(GTK_COMBO_BOX(o));
            g_object_ref(o);
            g_idle_add_once([](gpointer p) {
                auto c_ = Glib::wrap(GTK_COMBO_BOX_TEXT(p));
                if (auto im = static_cast<Impl *>(g_object_get_data(G_OBJECT(p), "hirch-pop"));
                    im && gtk_widget_get_mapped(GTK_WIDGET(p)))
                    im->werkzeug_aufklappen(*c_);
                g_object_unref(p);
            }, o);
        }), nullptr);
    }
    if (!g_object_get_data(obj, "hirch-db")) {
        g_object_set_data(obj, "hirch-db", GINT_TO_POINTER(1));
        c.signal_changed().connect([this, &c] {
            auto id = c.get_active_id().raw();
            if (id.rfind("db:", 0) != 0) return;
            Glib::signal_idle().connect_once(sigc::track_object([this, &c, id] {
                if (c.get_active_id().raw() != id) return;
                auto o = G_OBJECT(c.gobj());
                // der Platz, den diese Liste zuletzt angelegt hat, fällt weg, solange ihn nichts benutzt
                if (auto alt = static_cast<char const *>(g_object_get_data(o, "hirch-letzter-platz"))) {
                    std::string k(alt);
                    auto ws = unter(einst, "werkzeuge");
                    auto zw = unter(einst, "zweiseitig");
                    if (ws && ws->size() > 1 && !werkzeug_benutzung().count(k) &&
                        !(zw && str(*zw, "pass_werkzeug") == k))
                    {
                        einst["werkzeuge"].as_object().erase(k);
                        offene_plaetze()[this].erase(k);
                    }
                }
                auto key = werkzeug_platz(id);
                if (key.empty()) return;
                g_object_set_data_full(o, "hirch-letzter-platz", g_strdup(key.c_str()), g_free);
                bool b = g_object_get_data(o, "hirch-bohrer") != nullptr;
                if (g_object_get_data(o, "hirch-neu")) werkzeug_combo_alle(c, key, b);
                else werkzeug_combo(c, key, g_object_get_data(o, "hirch-mit-keinem") != nullptr, b);
            }, c));
        });
    }
    c.set_active_id(aktiv);
    // Platz, den es in der Zeichnung nicht gibt (Befund 22.10.): sichtbar machen statt still das erste Werkzeug
    bool fehlt = c.get_active_row_number() < 0 && !aktiv.empty() && aktiv.rfind("db:", 0) != 0 && !werkzeug(aktiv);
    if (fehlt) {
        auto t = aktiv + " · " + TR("fehlt in dieser Zeichnung – bitte wählen");
        c.prepend(aktiv, t);
        liste->insert(liste->begin(), {aktiv, t, "", 3, "", {}, 0, ""});
        c.set_active_id(aktiv);
    }
    hirch_werkzeug_fehlt_markieren(c, fehlt ? aktiv : std::string());
    if (c.get_active_row_number() < 0 && !mit_keinem) c.set_active(0);
}

void HirchFraesen::Impl::werkzeug_combo_alle(Gtk::ComboBoxText &c, std::string const &aktiv, bool bohrer)
{
    werkzeug_combo(c, aktiv, false, bohrer);     // mit «Meine Werkzeuge»
    g_object_set_data(G_OBJECT(c.gobj()), "hirch-neu", GINT_TO_POINTER(1));
    c.append("neu", TR("Neuer Fräser …"));
    if (auto l = wz_liste(G_OBJECT(c.gobj()))) l->push_back({"neu", TR("Neuer Fräser …"), "", 4, "", {}, 0, ""});
    c.set_active_id(aktiv);
    if (c.get_active_row_number() < 0) c.set_active(0);
}

void HirchFraesen::Impl::werkzeug_aufklappen(Gtk::ComboBoxText &c)
{
    // Werkzeugwahl (Peter, 20.10.): ein Dialog für Wählen und Verwalten – Arten als Karten, Fräser als
    // Zeilen, rechts alle Angaben. Klick auf das Werkzeugfeld öffnet ihn auf dem Fräser der Bearbeitung.
    auto o = G_OBJECT(c.gobj());
    WzWahl wahl;
    wahl.mit_keinem = g_object_get_data(o, "hirch-mit-keinem") != nullptr;
    wahl.bohrer = g_object_get_data(o, "hirch-bohrer") != nullptr;
    auto alt = c.get_active_id().raw();
    // leerer Eintrag, den die Bearbeitung selbst vorangestellt hat (Kernloch «schon gebohrt», Schlichten
    // «gleiches wie die Tasche»): im Dialog als eigener Knopf anbieten (Peter, 20.10.)
    if (!wahl.mit_keinem) {
        if (auto m = c.get_model()) {
            for (auto const &row : m->children()) {
                Glib::ustring rid, text;
                row.get_value(1, rid);
                row.get_value(0, text);
                if (rid.empty()) { wahl.keiner_text = text.raw(); break; }
            }
        }
    }
    werkzeuge_dialog(alt, {}, &wahl);
    if (!wahl.gewaehlt) return;
    auto id = wahl.ergebnis;
    // neue oder geänderte Fräser: Eintrag im Feld anlegen bzw. Beschriftung erneuern
    std::string name;
    if (id.rfind("db:", 0) == 0) {
        if (auto l = liste_von(analyse, "datenbank")) {
            for (auto const &v : *l) {
                if (auto w = v.if_object(); w && str(*w, "id") == id.substr(3)) name = werkzeug_name_mit_art(*w);
            }
        }
    } else if (!id.empty()) {
        if (auto ws = unter(einst, "werkzeuge")) {
            if (auto w = unter(*ws, id.c_str())) name = id + " · " + werkzeug_name_mit_art(*w);
        }
    }
    bool da = false;
    if (auto m = c.get_model()) {
        for (auto const &row : m->children()) {
            Glib::ustring rid;
            row.get_value(1, rid);
            if (rid.raw() != id) continue;
            da = true;
            if (!name.empty()) row.set_value(0, Glib::ustring(name));
        }
    }
    if (!da && (!id.empty() || wahl.mit_keinem)) {
        c.append(id, id.empty() ? TR("keiner – V-Fräser räumt aus") : name);
        if (auto l = wz_liste(o)) l->push_back({id, name, "", id.rfind("db:", 0) == 0 ? 1 : 0, "", {}, 0, ""});
    }
    if (id == alt) {
        // derselbe Fräser, vielleicht mit neuen Werten: die Bearbeitung soll neu lesen
        c.set_active(-1);
    }
    c.set_active_id(id);
}

std::string HirchFraesen::Impl::werkzeug_platz(std::string const &wahl)
{
    if (wahl.rfind("db:", 0) != 0) return wahl;
    auto id = wahl.substr(3);
    auto l = liste_von(analyse, "datenbank");
    if (!l) return "";
    for (auto const &v : *l) {
        auto o = v.if_object();
        if (!o || str(*o, "id") != id) continue;
        if (!unter(einst, "werkzeuge")) einst["werkzeuge"] = bj::object();
        auto &ws = einst["werkzeuge"].as_object();
        std::string key;
        for (char ch = 'A'; ch <= 'Z'; ++ch) {
            if (ws.find(std::string(1, ch)) == ws.end()) { key = std::string(1, ch); break; }
        }
        if (key.empty()) return "";
        bj::object w = *o;
        w.erase("notiz");
        w.erase("id");
        w["db_id"] = id;
        schnittdaten_uebernehmen(w);
        offene_plaetze()[this][key] = w;
        ws[key] = std::move(w);
        return key;
    }
    return "";
}

bj::object HirchFraesen::Impl::wirksam(std::string const &key)
{
    if (auto ww = unter(analyse, "werkzeuge_wirksam")) {
        if (auto w = unter(*ww, key.c_str())) return *w;
    }
    // eben aus «Meine Werkzeuge» übernommen: dessen Schnittdaten, bis die Rechnung ihn kennt
    if (auto w = werkzeug(key);
        w && !flag(*w, "nur_hier", false) && schnittmodus(fuer_werkstoff(*w, str(einst, "material", "hartholz"))) == "auto") {
        if (auto dw = unter(analyse, "datenbank_wirksam")) {
            if (auto x = unter(*dw, str(*w, "db_id").c_str())) return *x;
        }
    }
    if (auto w = werkzeug(key)) return *w;
    return {};
}

std::string HirchFraesen::Impl::werkstoff_name()
{
    auto k = str(einst, "material", "hartholz");
    if (auto l = liste_von(analyse, "materialien")) {
        for (auto const &e : *l) {
            auto p = e.if_array();
            if (p && p->size() >= 2 && (*p)[0].is_string() && (*p)[0].get_string() == k && (*p)[1].is_string()) {
                return std::string((*p)[1].get_string());
            }
        }
    }
    return k;
}

Gtk::Label *HirchFraesen::Impl::hinweis_label(std::string const &t)
{
    auto l = label(t, "hirch-dim");
    l->set_line_wrap(true);
    l->set_max_width_chars(56);
    klasse(*l, "hirch-klein");
    return l;
}

Gtk::ComboBoxText *HirchFraesen::Impl::combo_zeile(Gtk::Grid &g, int zeile, std::string const &text)
{
    auto l = label(text);
    l->set_hexpand(true);
    g.attach(*l, 0, zeile);
    auto c = Gtk::make_managed<Gtk::ComboBoxText>();
    g.attach(*c, 1, zeile, 2);
    return c;
}

} // namespace Inkscape::UI::Dialog
