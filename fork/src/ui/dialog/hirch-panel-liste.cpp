// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Panel: Zeilen der Bearbeitungsliste, Gruppen, Ziehen und Ordnen
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

Gtk::Widget *HirchFraesen::Impl::op_zeile(bj::object const *op, bj::object const *b, int index, int unterz)
{
    std::string art = op ? str(*op, "art") : (b ? str(*b, "art") : "");
    std::string teil = op ? str(*op, "teil") : "";
    std::string id = op ? str(*op, "id") : (b ? str(*b, "id") : "");
    bool haupt = teil.empty() && index >= 0;
    bool aktiv = b ? flag(*b, "aktiv", true) : true;
    bool pass = id == "pass";
    double dicke = 18;
    if (auto m = unter(analyse, "material")) dicke = num(*m, "dicke", 18);
    std::string hex = op ? str(*op, "farbe") : (b ? str(*b, "farbe") : "");
    double r = 0.5, g = 0.5, bl = 0.5;
    hex_rgb(hex, r, g, bl);

    auto ebox = Gtk::make_managed<Gtk::EventBox>();
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 10);
    box->set_margin_start(unterz == 1 ? 34 : 10);
    box->set_margin_end(6);
    box->set_margin_top(7);
    box->set_margin_bottom(7);

    // Schnittbild mit Tiefe darunter
    auto bild = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 1);
    bild->set_valign(Gtk::ALIGN_CENTER);
    auto da = Gtk::make_managed<Gtk::DrawingArea>();
    da->set_size_request(unterz == 1 ? 44 : 56, unterz == 1 ? 26 : 34);
    double tiefe = op ? num(*op, "tiefe", 0) : (b ? num(*b, art == "vcarve" ? "max_tiefe" : "tiefe", 0) : 0);
    bool durch = op ? flag(*op, "durch", false) : (art == "kontur" && b && ist_null(*b, "tiefe"));
    if (!op && durch) tiefe = dicke + 0.2;
    int n = op ? (int)num(*op, "n", 1) : 1;
    bool stege = op && Glib::ustring(str(*op, "extra")).lowercase().find(Glib::ustring(TR("Haltesteg")).lowercase()) != Glib::ustring::npos;
    std::string zart = (b && flag(*b, "fase", false)) ? "fase" : art;
    da->signal_draw().connect([=](Cr const &cr) {
        schnitt_zeichnen(cr, da->get_allocated_width(), da->get_allocated_height(), zart, teil, tiefe, dicke, n,
                         durch, stege, pass, r, g, bl);
        return true;
    });
    bild->add(*da);
    // Tiefe unter dem Bild: mm ohne Einheit (Platz); bei Zoll die Zahl in Zoll, ebenfalls ohne Einheit
    auto t1 = [](double mm) { return hirch_zoll() ? einheit_zahl(mm, 2, Groesse::LAENGE, false) : zahl(mm, 1); };
    std::string tt = durch ? t1(tiefe) + TR(" durch") : (art == "vcarve" ? "max. " : "") + t1(tiefe);
    double ab = op ? num(*op, "start", 0) : (b && (art == "tasche" || art == "kontur") ? num(*b, "starttiefe", 0) : 0);
    if (ab > 1e-6 && ab < tiefe) tt = t1(ab) + "–" + tt;
    auto tl = label(tt, "hirch-dim");
    klasse(*tl, "hirch-klein");
    klasse(*tl, "hirch-mono");
    tl->set_xalign(0.5);
    bild->add(*tl);
    box->add(*bild);

    // Text
    auto text = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
    text->set_hexpand(true);
    text->set_valign(Gtk::ALIGN_CENTER);
    auto z1 = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 7);
    z1->add(*farbfeld(art_info(art), 10, hex));
    std::string name = op ? str(*op, "name") : (b ? name_von(*b) : "");
    if (pass) name = TR("Passlöcher");
    std::string teil_text = op ? str(*op, "extra") : "";
    std::string kopf_text = (index >= 0 ? std::to_string(index + 1) + " · " : std::string()) + name;
    if (unterz) kopf_text = "↳ " + (teil_text.empty() ? teil : teil_text);
    auto titel = label(kopf_text, unterz == 1 ? "hirch-dim" : "hirch-titel");
    titel->set_ellipsize(Pango::ELLIPSIZE_END);
    titel->set_hexpand(true);
    z1->add(*titel);
    {
        // Zeit bleibt stehen, auch während neu gerechnet wird und bei ausgeschalteten
        // Bearbeitungen (die dauern ja so lange, wenn man sie einschaltet)
        double m = op ? op_minuten(*op) : (b && index >= 0 ? bearbeitung_minuten(id) : 0.0);
        if (m > 0) {
            auto zl = label(kurzzeit(m), op ? "hirch-mono" : "hirch-dim");
            klasse(*zl, "hirch-mono");
            if (rechnet || vorlaeufig_an) klasse(*zl, "hirch-veraltet");
            z1->add(*zl);
        } else if (op && rechnet) {
            z1->add(*label("…", "hirch-dim"));
        }
    }
    text->add(*z1);
    std::string z2;
    if (op) {
        int nn = (int)num(*op, "n", 1);
        // Zoll: mit Einheit (die Zahl ist mm); bei mm wie bisher ohne
        auto laenge = [](double v) { return hirch_zoll() ? einheit_zahl(v) : zahl(v); };
        z2 = nn <= 1 ? TR("1 Zustellung") : std::to_string(nn) + TR(" Zustellungen à ") + laenge(num(*op, "je", 0));
        // ungleiche Zustellungen (letzte kleiner, eigene Liste): jede einzeln, lange Listen gekürzt
        if (auto st = liste_von(*op, "stufen"); st && nn > 1 && !flag(*op, "gleich", true)) {
            std::string t;
            int i = 0;
            for (auto const &v : *st) {
                if (++i > 4 && (int)st->size() > 5) {
                    t += " · …";
                    break;
                }
                t += (t.empty() ? "" : " · ") + laenge(v.is_number() ? v.to_number<double>() : 0.0);
            }
            z2 = std::to_string(nn) + TR(" Zustellungen: ") + t;
        }
        if (num(*op, "start", 0) > 1e-6) z2 += TRF(" ab {} mm", {zahl(num(*op, "start", 0))});
        auto ex = str(*op, "extra");
        if (!ex.empty() && !unterz) z2 += " · " + ex;
    } else if (b) {
        long cnt = objekte(id);
        z2 = !aktiv ? "ausgeschaltet"
             : (ohne_objekte(art) ? TR(art_info(art) ? art_info(art)->farbe : "ganzes Material")
                : cnt == 0 ? (je_art(art) > 1 ? std::string(TR("keine Objekte – Rechtsklick › Auswahl zuweisen"))
                                               : TRF("keine Objekte – {} einfärben", {art_info(art) ? TR(art_info(art)->farbe) : ""}))
                           : anzahl(cnt, TR("Objekt"), TR("Objekte")));
    }
    // Böden und Wände: mit welchem 3D-Schruppen im Wechsel (Peter 25.10.)
    if (b && aktiv && ist_boeden_waende(art)) {
        auto wi = str(*b, "im_wechsel_mit");
        std::string pn;
        if (auto bea = liste_von(einst, "bearbeitungen"); bea && !wi.empty())
            for (auto const &v : *bea)
                if (auto o = v.if_object(); o && str(*o, "id") == wi) pn = name_von(*o);
        z2 = pn.empty() ? TRF("Stufen {} mm · nach dem 3D-Schruppen", {zahl(num(*b, "stufe", 10))})
                        : "⇄ " + TRF("im Wechsel mit {} · alle {} mm", {pn, zahl(num(*b, "stufe", 10))});
    }
    if (unterz == 2) {
        // Durchgang bei einem anderen Werkzeug: zu welcher Bearbeitung er gehört
        auto gl = Gtk::make_managed<Gtk::Label>();
        gl->set_xalign(0);
        gl->set_ellipsize(Pango::ELLIPSIZE_END);
        gl->set_markup("<span foreground=\"" + (hex.empty() ? std::string("#888888") : hex) +
                       TR("\">gehört zu ") + Glib::Markup::escape_text(std::to_string(index + 1) + " · " + name).raw() +
                       "</span>");
        klasse(*gl, "hirch-klein");
        text->add(*gl);
    }
    auto l2 = label(z2, "hirch-dim");
    l2->set_ellipsize(Pango::ELLIPSIZE_END);
    klasse(*l2, "hirch-klein");
    text->add(*l2);
    if (op) {
        if (auto ws = liste_von(*op, "warnungen")) {
            for (auto const &wv : *ws) {
                if (!wv.is_string()) continue;
                auto wl = label("⚠ " + std::string(wv.get_string()), "hirch-warntext");
                wl->set_line_wrap(true);
                klasse(*wl, "hirch-klein");
                // Restmaterial nach dem 3D-Schruppen: gleich mit dem Knopf (Befund der Prüfung, Peter 25.10.)
                bj::object const *rest = nullptr;
                if (auto pr = ergebnis_aktuell ? liste_von(ergebnis, "pruefung") : nullptr)
                    for (auto const &v : *pr)
                        if (auto o = v.if_object(); o && str(*o, "aktion") == "restmaterial" &&
                                                     str(*o, "bearbeitung") == id &&
                                                     str(*o, "text").find(std::string(wv.get_string())) != std::string::npos)
                            rest = o;
                text->add(rest ? *befund_widget(*rest, wl) : *wl);
            }
        }
    }
    box->add(*text);

    // Auge und Ein/Aus nur bei der Bearbeitung selbst
    if (haupt && b && !pass && !ohne_objekte(art)) {
        // «Auswahl zuweisen»: gestricheltes Auswahlrechteck mit Plus und der Anzahl – nur sichtbar,
        // solange Formen ausgewählt sind, die noch nicht zu dieser Bearbeitung gehören
        auto zw = Gtk::make_managed<Gtk::Button>();
        zw->set_relief(Gtk::RELIEF_NONE);
        zw->set_valign(Gtk::ALIGN_CENTER);
        klasse(*zw, "hirch-zuweisen");
        auto ov = Gtk::make_managed<Gtk::Overlay>();
        auto ic = Gtk::make_managed<Gtk::DrawingArea>();
        ic->set_size_request(30, 22);              // rechts oben Platz für die Zahl
        ic->signal_draw().connect([ic](Cr const &cr) {
            auto c = ic->get_style_context()->get_color(ic->get_state_flags());
            cr->set_source_rgba(c.get_red(), c.get_green(), c.get_blue(), c.get_alpha());
            cr->set_line_width(1.5);
            std::vector<double> strich{2.5, 2.0};
            cr->set_dash(strich, 0);
            cr->rectangle(2.75, 6.75, 10.0, 10.0);
            cr->stroke();
            cr->unset_dash();
            cr->set_line_width(1.8);
            cr->set_line_cap(Cairo::LINE_CAP_ROUND);
            cr->move_to(14.5, 13.75);
            cr->line_to(14.5, 20.25);
            cr->move_to(11.25, 17);
            cr->line_to(17.75, 17);
            cr->stroke();
            return true;
        });
        ov->add(*ic);
        auto zahl_l = Gtk::make_managed<Gtk::Label>();
        klasse(*zahl_l, "hirch-badge");
        zahl_l->set_halign(Gtk::ALIGN_END);
        zahl_l->set_valign(Gtk::ALIGN_START);
        ov->add_overlay(*zahl_l);
        zw->add(*ov);
        zw->signal_clicked().connect([this, index] {
            if (baut_ui) return;
            Glib::signal_idle().connect_once([this, index] { zuweisen(index); });
        });
        zw->show_all();
        zw->set_no_show_all(true);
        // Platz bleibt immer frei, der Knopf wird nur sichtbar/unsichtbar (Peter, 23.10.: beim Anklicken
        // von Objekten sprang die Liste, weil Zeilen durch den Knopf schmaler und höher wurden)
        zw->set_opacity(0.0);
        zw->set_sensitive(false);
        zuweisen_knoepfe.push_back({str(*b, "farbe"), name_von(*b), zw, zahl_l});
        box->add(*zw);
    }
    if (haupt) {
        bool sichtbar = flag(*b, "sichtbar", true);
        auto auge = Gtk::make_managed<Gtk::Button>();
        auge->set_relief(Gtk::RELIEF_NONE);
        auge->set_valign(Gtk::ALIGN_CENTER);
        auge->set_image_from_icon_name(sichtbar ? "object-visible" : "object-hidden", Gtk::ICON_SIZE_BUTTON);
        auge->set_tooltip_text(sichtbar ? TR("Objekte und Fräsbahnen dieser Bearbeitung ausblenden (werden trotzdem gefräst)")
                                        : TR("Objekte und Fräsbahnen dieser Bearbeitung wieder zeigen"));
        auge->signal_clicked().connect([this, index] {
            if (baut_ui) return;
            Glib::signal_idle().connect_once([this, index] { sichtbar_umschalten(index); });
        });
        box->add(*auge);
        auto an = Gtk::make_managed<Gtk::CheckButton>();
        an->set_active(aktiv);
        an->set_valign(Gtk::ALIGN_CENTER);
        an->set_tooltip_text(TR("Bearbeitung ein- oder ausschalten"));
        an->signal_toggled().connect([this, index, an] {
            if (baut_ui) return;
            if (auto bb = bearbeitung(index)) {
                (*bb)["aktiv"] = an->get_active();
                schreiben(an->get_active() ? TRF("{} eingeschaltet", {name_von(*bb)}) : TRF("{} ausgeschaltet", {name_von(*bb)}));
            }
        });
        box->add(*an);
    }
    if (!op && b && (!aktiv || (objekte(id) == 0 && !ohne_objekte(art)))) klasse(*text, "hirch-aus");
    ebox->add(*box);
    ebox->set_tooltip_text(pass ? TR("Passlöcher für die Passstifte: einstellen unter «Einrichten …» im Panel")
                                : TR("Doppelklick: einstellen · Rechtsklick: weitere Befehle · Ziehen: Reihenfolge"));
    if (index >= 0) {
        std::string op_key = op ? op_schluessel(*op) : std::string();
        std::string op_name = op ? str(*op, "name") : (b ? str(*b, "name") : std::string());
        ebox->signal_button_press_event().connect([this, index, op_key, op_name](GdkEventButton *e) {
            if (e->type == GDK_BUTTON_PRESS && e->button == 3) {
                kontextmenue(index, (GdkEvent *)e, op_key, op_name);
                return true;
            }
            return false;
        }, false);
        ziehbar(*ebox, [index] { return std::vector<int>{index}; },
                [](Gtk::EventBox &eb) -> Gtk::Widget * { return eb.get_parent(); }, false);
    }
    auto row = Gtk::make_managed<Gtk::ListBoxRow>();
    // Name: Index der Bearbeitung, bei einem Teil (Räumen, Schlichten …) mit «#teil»
    row->set_name(pass ? "pass" : std::to_string(index) + (teil.empty() ? "" : "#" + teil));
    row->add(*ebox);
    return row;
}

Gtk::ListBox *HirchFraesen::Impl::gruppe(std::string const &chip, std::string const &titel, std::string const &rechts,
                                         std::string const &slot)
{
    auto karte = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 0);
    klasse(*karte, "hirch-gruppe");
    auto kopf = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    klasse(*kopf, "hirch-gruppe-kopf");
    if (!chip.empty()) kopf->add(*label(chip, "hirch-chip"));
    // Werkzeug-Kopfzeile (Peter, 20.10.): Platz-Schild und Fräserbild wie im Werkzeugdialog
    bj::object const *wz = nullptr;
    if (!slot.empty()) {
        if (auto ws = unter(einst, "werkzeuge")) wz = unter(*ws, slot.c_str());
    }
    if (wz) {
        auto sl = label(slot, "hirch-wz-schild");
        if (flag(*wz, "nur_hier", false)) klasse(*sl, "hirch-wz-schild-nur");
        sl->set_valign(Gtk::ALIGN_CENTER);
        kopf->add(*sl);
        auto m = fraeser_merkmale(*wz);
        auto da = Gtk::make_managed<Gtk::DrawingArea>();
        da->set_size_request(10, 30);
        da->set_valign(Gtk::ALIGN_CENTER);
        da->signal_draw().connect([da, m](Cr const &cr) {
            auto f = da->get_style_context()->get_color(da->get_state_flags());
            fraeserbild::zeichnen(cr, da->get_allocated_width(), da->get_allocated_height(), m,
                                  (f.get_red() + f.get_green() + f.get_blue()) / 3 < 0.5, 1.0);
            return true;
        });
        kopf->add(*da);
    }
    auto tl = label(titel, "hirch-titel");
    tl->set_hexpand(true);
    tl->set_ellipsize(Pango::ELLIPSIZE_END);
    kopf->add(*tl);
    if (!rechts.empty()) {
        auto rl = label(rechts, "hirch-dim");
        klasse(*rl, "hirch-klein");
        kopf->add(*rl);
    }
    auto lb = Gtk::make_managed<Gtk::ListBox>();
    if (wz) {
        // Bleistift (B-6, Peter 30.10.): ein Fenster – den Fräser bearbeiten oder in der Liste einen anderen für
        // die Bearbeitungen dieser Gruppe wählen (vorher zwei Menüeinträge, das war verwirrend)
        auto bt = Gtk::make_managed<Gtk::Button>();
        bt->set_relief(Gtk::RELIEF_NONE);
        bt->set_valign(Gtk::ALIGN_CENTER);
        bt->set_image_from_icon_name("document-edit", Gtk::ICON_SIZE_MENU);
        bt->set_tooltip_text(TR("Fräser bearbeiten – oder in der Liste einen anderen wählen"));
        bt->signal_clicked().connect([this, lb, slot] {
            auto indizes = gruppen_indizes(lb);
            bool benutzt = false;
            for (int i : indizes)
                if (auto b = bearbeitung(i); b && str(*b, "werkzeug") == slot) benutzt = true;
            Glib::signal_idle().connect_once([this, indizes, slot, benutzt] {
                if (benutzt) gruppe_fraeser_wechseln(indizes, slot);
                else werkzeuge_dialog(slot, {}, nullptr, true);   // nur Räum-/Schlichtwerkzeug: nichts zu wechseln
            });
        });
        kopf->add(*bt);
    }
    // Kopfzeile ziehen: die ganze Werkzeuggruppe verschieben
    auto kbox = Gtk::make_managed<Gtk::EventBox>();
    kbox->add(*kopf);
    kbox->set_tooltip_text(TR("Ziehen: ganze Werkzeuggruppe an eine andere Stelle im Ablauf"));
    ziehbar(*kbox, [this, lb] { return gruppen_indizes(lb); },
            [](Gtk::EventBox &eb) -> Gtk::Widget * { return eb.get_parent(); }, true);
    karte->add(*kbox);
    klasse(*lb, "hirch-ops");
    lb->set_selection_mode(Gtk::SELECTION_SINGLE);
    lb->set_activate_on_single_click(false);
    lb->signal_row_activated().connect([this](Gtk::ListBoxRow *row) {
        if (!row) return;
        if (row->get_name() == "pass") {
            Glib::signal_idle().connect_once([this] { material_dialog(); });
            return;
        }
        std::string name = row->get_name().raw();
        int i = std::atoi(name.c_str());
        auto teil = name.find('#') == std::string::npos ? std::string() : name.substr(name.find('#') + 1);
        Glib::signal_idle().connect_once([this, i, teil] { bearbeitung_dialog(i, teil); });
    });
    lb->signal_row_selected().connect([this, lb](Gtk::ListBoxRow *row) {
        if (!row || baut_ui) return;
        entf_taste_verbinden();
        for (auto andere : op_listen) {
            if (andere != lb) andere->unselect_all();
        }
        // 3D-Ansicht: in der Zeitleiste an den Anfang dieser Bearbeitung springen (Schlüssel wie engine «key»)
        auto springen = [this](std::string const &schluessel) {
            auto desk = panel.getDesktop();
            if (desk && desk->getDesktopWidget() && hirch_ansicht_3d_ist(desk->getDesktopWidget()))
                hirch_ansicht_3d_springen(desk, schluessel);
        };
        if (row->get_name() == "pass") {
            springen("pass");
            return;
        }
        std::string name = row->get_name().raw();
        gewaehlt_index = std::atoi(name.c_str());
        objekte_waehlen(gewaehlt_index);
        if (auto l = liste_von(einst, "bearbeitungen"); l && gewaehlt_index >= 0 && gewaehlt_index < (int)l->size()) {
            if (auto b = (*l)[gewaehlt_index].if_object()) {
                auto teil = name.find('#') == std::string::npos ? std::string() : name.substr(name.find('#'));
                springen(str(*b, "id") + teil);
            }
        }
    });
    karte->add(*lb);
    liste.add(*karte);
    op_listen.push_back(lb);
    return lb;
}

void HirchFraesen::Impl::gruppe_fraeser_wechseln(std::vector<int> const &indizes, std::string const &slot)
{
    // Peter 28.10.: einen anderen Fräser für alle Bearbeitungen einer Werkzeuggruppe – nur das Hauptwerkzeug;
    // Räum- und Schlichtwerkzeuge bleiben. Was nicht passt (V-Carve braucht einen V-Fräser …), bleibt mit Hinweis.
    std::vector<int> ziel;
    bool bohren = false;
    for (int i : indizes) {
        auto b = bearbeitung(i);
        if (!b || str(*b, "werkzeug") != slot) continue;
        ziel.push_back(i);
        if (str(*b, "art") == "bohren") bohren = true;
    }
    if (ziel.empty()) return;
    WzWahl wahl;
    wahl.bohrer = bohren;
    werkzeuge_dialog(slot, {}, &wahl);
    if (!wahl.gewaehlt || wahl.ergebnis.empty() || wahl.ergebnis == slot) return;
    bool neu_platz = wahl.ergebnis.rfind("db:", 0) == 0;
    auto key = werkzeug_platz(wahl.ergebnis);
    if (key.empty()) return;
    auto w = werkzeug(key);
    if (!w) return;
    auto typ = str(*w, "typ", "schaft");
    int n = 0;
    std::string uebrig;
    for (int i : ziel) {
        auto b = bearbeitung(i);
        if (!b) continue;
        auto art = str(*b, "art");
        if (wz_passend(art, typ, art == "bohren")) {
            (*b)["werkzeug"] = key;
            ++n;
        } else {
            uebrig += (uebrig.empty() ? "" : ", ") + name_von(*b);
        }
    }
    if (n == 0) {
        if (neu_platz) {
            einst["werkzeuge"].as_object().erase(key);
            offene_plaetze()[this].erase(key);
        }
        meldung(TR("Fräser nicht gewechselt"),
                TR("Der gewählte Fräser passt zu keiner Bearbeitung dieser Gruppe: ") + uebrig, Gtk::MESSAGE_INFO);
        return;
    }
    // der alte Platz bleibt in der Zeichnung (3D-Modelle und Passlöcher können ihn noch brauchen)
    schreiben(TRF("Anderer Fräser für {} Bearbeitungen", {std::to_string(n)}));
    if (!uebrig.empty()) {
        meldung(TR("Nicht alle gewechselt"),
                TR("Diese Bearbeitungen behalten ihren Fräser, weil der gewählte nicht dazu passt: ") + uebrig,
                Gtk::MESSAGE_INFO);
    }
}

void HirchFraesen::Impl::entf_taste_verbinden()
{
    // Entf auf der gewählten Zeile löscht die Bearbeitung (Peter 28.10.). Am Fenster vor dessen Kurzbefehlen
    // abfangen – sonst löscht Inkscape die eben mit der Zeile ausgewählten Objekte.
    auto top = dynamic_cast<Gtk::Window *>(panel.get_toplevel());
    if (!top || (top == entf_fenster && entf_taste.connected())) return;
    entf_taste.disconnect();
    entf_fenster = top;
    entf_taste = top->signal_key_press_event().connect([this, top](GdkEventKey *e) {
        if (e->keyval != GDK_KEY_Delete && e->keyval != GDK_KEY_KP_Delete) return false;
        if (e->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_MOD1_MASK)) return false;
        auto f = top->get_focus();
        if (!f) return false;
        for (auto lb : op_listen) {
            if (f != lb && !f->is_ancestor(*lb)) continue;
            auto row = lb->get_selected_row();
            if (!row || row->get_name() == "pass" || row->get_name().empty()) return false;
            int i = std::atoi(row->get_name().c_str());
            Glib::signal_idle().connect_once([this, i] { entfernen(i); });
            return true;
        }
        return false;
    }, false);
}

std::vector<int> HirchFraesen::Impl::gruppen_indizes(Gtk::ListBox *lb)
{
    std::vector<int> v;
    for (auto c : lb->get_children()) {
        auto r = dynamic_cast<Gtk::ListBoxRow *>(c);
        if (!r || r->get_name() == "pass" || r->get_name().empty()) continue;
        int i = std::atoi(r->get_name().c_str());
        if (std::find(v.begin(), v.end(), i) == v.end()) v.push_back(i);
    }
    return v;
}

bool HirchFraesen::Impl::punkt_lokal(Gtk::Widget &w, double xr, double yr, int &lx, int &ly)
{
    auto top = w.get_toplevel();
    if (!top || !top->get_window()) return false;
    int tx = 0, ty = 0, wx = 0, wy = 0;
    top->get_window()->get_origin(tx, ty);
    if (!w.translate_coordinates(*top, 0, 0, wx, wy)) return false;
    lx = (int)std::lround(xr - tx - wx);
    ly = (int)std::lround(yr - ty - wy);
    return true;
}

void HirchFraesen::Impl::ziehbar(Gtk::EventBox &eb, std::function<std::vector<int>()> quelle, Gtk::Widget *(*widget_von)(Gtk::EventBox &),
                 bool gruppe)
{
    eb.add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::BUTTON1_MOTION_MASK);
    eb.signal_button_press_event().connect([this, &eb, quelle, widget_von, gruppe](GdkEventButton *e) {
        if (e->type != GDK_BUTTON_PRESS || e->button != 1 || zg.aktiv) return false;
        zg.gedrueckt = true;
        zg.gruppe = gruppe;
        zg.x0 = e->x_root;
        zg.y0 = e->y_root;
        zg.quelle = quelle();
        zg.quell_widget = widget_von(eb);
        return false;
    }, false);
    eb.signal_motion_notify_event().connect([this](GdkEventMotion *e) {
        if (!zg.gedrueckt) return false;
        if (!(e->state & GDK_BUTTON1_MASK)) {
            // Loslassen ging verloren (z. B. Liste neu gebaut): nie ohne gedrückte Taste ziehen
            if (zg.aktiv) ziehen_beenden(true);
            zg.gedrueckt = false;
            return false;
        }
        zg.xr = e->x_root;
        zg.yr = e->y_root;
        if (!zg.aktiv) {
            if (std::hypot(zg.xr - zg.x0, zg.yr - zg.y0) < 7) return false;
            if (!ziehen_starten()) {
                zg.gedrueckt = false;
                return false;
            }
        }
        ziehen_bewegen();
        return true;
    }, false);
    eb.signal_button_release_event().connect([this](GdkEventButton *e) {
        if (e->button != 1) return false;
        bool war = zg.aktiv;
        zg.gedrueckt = false;
        if (!war) return false;
        zg.xr = e->x_root;
        zg.yr = e->y_root;
        ziehen_beenden(true);
        return true;
    }, false);
    eb.signal_grab_broken_event().connect([this](GdkEventGrabBroken *) {
        if (zg.aktiv) ziehen_beenden(false);
        zg.gedrueckt = false;
        return false;
    }, false);
}

bool HirchFraesen::Impl::ziehen_starten()
{
    auto w = zg.quell_widget;
    if (!w || zg.quelle.empty() || !w->get_realized()) return false;
    auto al = w->get_allocation();
    zg.bw = std::max(1, al.get_width());
    zg.bh = std::max(1, al.get_height());
    int sf = std::max(1, w->get_scale_factor());
    // Abbild der Zeile: Hintergrund des Themas, darüber die Zeile selbst
    zg.bild = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, zg.bw * sf, zg.bh * sf);
    cairo_surface_set_device_scale(zg.bild->cobj(), sf, sf);
    {
        auto cr = Cairo::Context::create(zg.bild);
        Gdk::RGBA bg;
        auto ctx = panel.get_style_context();
        if (!ctx->lookup_color("theme_base_color", bg)) bg.set_rgba(1, 1, 1, 1);
        cr->set_source_rgba(bg.get_red(), bg.get_green(), bg.get_blue(), 1.0);
        cr->paint();
        gtk_widget_draw(w->gobj(), cr->cobj());
    }
    int lx = 0, ly = 0;
    punkt_lokal(*w, zg.x0, zg.y0, lx, ly);
    zg.greif_y = ly;
    {
        // linker Rand der Zeile auf dem Bildschirm: die Karte bleibt senkrecht über der Liste
        int ox = 0, oy = 0, wx = 0, wy = 0;
        auto top = w->get_toplevel();
        top->get_window()->get_origin(ox, oy);
        w->translate_coordinates(*top, 0, 0, wx, wy);
        zg.bx = ox + wx;
    }
    zg.quell_lb = zg.gruppe ? nullptr : dynamic_cast<Gtk::ListBox *>(w->get_parent());
    zg.aktiv = true;
    zg.ziel_lb = nullptr;
    zg.ziel_pos = -1;
    zg.luecke = nullptr;
    w->set_opacity(0.3);
    schwebe_fenster.set_default_size(zg.bw + 12, zg.bh + 14);
    schwebe_fenster.resize(zg.bw + 12, zg.bh + 14);
    if (auto top = dynamic_cast<Gtk::Window *>(panel.get_toplevel())) schwebe_fenster.set_transient_for(*top);
    schwebe_setzen();
    schwebe_fenster.show();
    // die schwebende Karte liegt unter der Maus: sie darf keine Klicks abfangen
    if (auto gw = schwebe_fenster.get_window()) gw->input_shape_combine_region(Cairo::Region::create(), 0, 0);
    if (auto win = panel.get_window()) win->set_cursor(Gdk::Cursor::create(panel.get_display(), "grabbing"));
    zg.los = 0;
    if (auto top = dynamic_cast<Gtk::Window *>(panel.get_toplevel())) {
        zg.taste = top->signal_key_press_event().connect([this](GdkEventKey *e) {
            if (!zg.aktiv || e->keyval != GDK_KEY_Escape) return false;
            ziehen_beenden(false);
            return true;
        }, false);
    }
    // Rand-Bildlauf und Ziel nachführen, auch wenn die Maus stillsteht
    zg.takt = Glib::signal_timeout().connect([this] {
        if (!zg.aktiv) return false;
        // Sicherheitsnetz: ist die Taste nicht mehr gedrückt (Loslassen verloren), ablegen
        if (auto gw = panel.get_window()) {
            if (auto seat = panel.get_display()->get_default_seat()) {
                if (auto zeiger = seat->get_pointer()) {
                    int mx = 0, my = 0;
                    Gdk::ModifierType maske{};
                    gw->get_device_position(zeiger, mx, my, maske);
                    zg.los = (maske & Gdk::BUTTON1_MASK) ? 0 : zg.los + 1;
                    if (zg.los >= 3) {
                        ziehen_beenden(true);
                        return false;
                    }
                }
            }
        }
        int lx_ = 0, ly_ = 0;
        if (punkt_lokal(scroll, zg.xr, zg.yr, lx_, ly_)) {
            int h = scroll.get_allocated_height(), rand = 48;
            double v = 0;
            if (ly_ < rand) v = -(rand - std::max(ly_, -rand)) * 0.35;
            else if (ly_ > h - rand) v = (std::min(ly_, h + rand) - (h - rand)) * 0.35;
            if (v != 0) {
                auto adj = scroll.get_vadjustment();
                double neu = std::clamp(adj->get_value() + v, adj->get_lower(),
                                        adj->get_upper() - adj->get_page_size());
                if (neu != adj->get_value()) {
                    adj->set_value(neu);
                    ziel_suchen();
                }
            }
        }
        return true;
    }, 16);
    return true;
}

void HirchFraesen::Impl::ziehen_bewegen()
{
    schwebe_setzen();
    ziel_suchen();
}

void HirchFraesen::Impl::schwebe_setzen()
{
    schwebe_fenster.move(zg.bx - 6, (int)std::lround(zg.yr - zg.greif_y) - 4);
}

bool HirchFraesen::Impl::schwebe_malen(Cairo::RefPtr<Cairo::Context> const &cr)
{
    cr->save();
    cr->set_operator(Cairo::OPERATOR_SOURCE);
    cr->set_source_rgba(0, 0, 0, 0);
    cr->paint();
    cr->restore();
    if (!zg.bild) return true;
    if (!schwebe_fenster.get_screen() || !schwebe_fenster.get_screen()->is_composited()) {
        // ohne Compositor keine Transparenz: die Karte als Rechteck
        cr->set_source(zg.bild, 6, 4);
        cr->paint();
        cr->rectangle(0.75, 0.75, zg.bw + 10.5, zg.bh + 12.5);
        cr->set_source_rgba(0.21, 0.52, 0.89, 1);
        cr->set_line_width(1.5);
        cr->stroke();
        return true;
    }
    double x = 6, y = 4, w = zg.bw, h = zg.bh, r = 8;
    auto pfad = [&](double dx, double dy) {
        cr->begin_new_sub_path();
        cr->arc(x + dx + w - r, y + dy + r, r, -M_PI / 2, 0);
        cr->arc(x + dx + w - r, y + dy + h - r, r, 0, M_PI / 2);
        cr->arc(x + dx + r, y + dy + h - r, r, M_PI / 2, M_PI);
        cr->arc(x + dx + r, y + dy + r, r, M_PI, 3 * M_PI / 2);
        cr->close_path();
    };
    // weicher Schatten
    for (int i = 3; i >= 1; --i) {
        pfad(0, i * 1.5);
        cr->set_source_rgba(0, 0, 0, 0.06);
        cr->fill();
    }
    cr->save();
    pfad(0, 0);
    cr->clip();
    cr->set_source(zg.bild, x, y);
    cr->paint_with_alpha(0.94);
    cr->restore();
    pfad(0, 0);
    cr->set_source_rgba(0.21, 0.52, 0.89, 0.9);   // Auswahlblau wie Adwaita
    cr->set_line_width(1.5);
    cr->stroke();
    return true;
}

std::vector<Gtk::ListBoxRow *> HirchFraesen::Impl::echte_zeilen(Gtk::ListBox *lb)
{
    std::vector<Gtk::ListBoxRow *> v;
    for (auto c : lb->get_children()) {
        auto r = dynamic_cast<Gtk::ListBoxRow *>(c);
        if (r && r->get_name() != "luecke") v.push_back(r);
    }
    return v;
}

std::vector<Gtk::Widget *> HirchFraesen::Impl::echte_karten()
{
    std::vector<Gtk::Widget *> v;
    for (auto c : liste.get_children())
        if (c->get_name() != "luecke") v.push_back(c);
    return v;
}

void HirchFraesen::Impl::ziel_suchen()
{
    if (!zg.aktiv) return;
    if (zg.gruppe) {
        // zwischen die Karten
        auto karten = echte_karten();
        int pos = (int)karten.size();
        for (std::size_t k = 0; k < karten.size(); ++k) {
            int lx = 0, ly = 0;
            punkt_lokal(*karten[k], zg.xr, zg.yr, lx, ly);
            if (ly < karten[k]->get_allocated_height() / 2) {
                pos = (int)k;
                break;
            }
        }
        int q = -1;
        for (std::size_t k = 0; k < karten.size(); ++k)
            if (karten[k] == zg.quell_widget) q = (int)k;
        if (pos == q || pos == q + 1) pos = -1;          // bliebe, wo sie ist
        if (pos == zg.ziel_pos) return;
        luecke_schliessen();
        zg.ziel_pos = pos;
        if (pos < 0) return;
        auto rv = Gtk::make_managed<Gtk::Revealer>();
        rv->set_name("luecke");
        rv->set_transition_type(Gtk::REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
        rv->set_transition_duration(150);
        auto platz = Gtk::make_managed<Gtk::Box>();
        platz->set_size_request(-1, zg.bh);
        rv->add(*platz);
        liste.add(*rv);
        int stelle = 0, k = 0;
        for (auto c : liste.get_children()) {
            if (c == karten[std::min<std::size_t>(pos, karten.size() - 1)]) stelle = k;
            ++k;
        }
        liste.reorder_child(*rv, pos >= (int)karten.size() ? -1 : stelle);
        rv->show_all();
        rv->set_reveal_child(true);
        zg.luecke = rv;
        return;
    }
    // Zeilen: die Liste unter der Maus (oder die nächste), darin die Stelle
    Gtk::ListBox *best = nullptr;
    int best_d = 1 << 30;
    for (auto lb : op_listen) {
        auto karte = lb->get_parent();
        if (!karte || !lb->get_mapped()) continue;
        int lx = 0, ly = 0;
        punkt_lokal(*lb, zg.xr, zg.yr, lx, ly);
        int h = lb->get_allocated_height();
        int d = ly < 0 ? -ly : (ly > h ? ly - h : 0);
        if (d < best_d) {
            best_d = d;
            best = lb;
        }
    }
    if (!best) return;
    auto zeilen = echte_zeilen(best);
    int pos = (int)zeilen.size();
    for (std::size_t k = 0; k < zeilen.size(); ++k) {
        int lx = 0, ly = 0;
        punkt_lokal(*zeilen[k], zg.xr, zg.yr, lx, ly);
        if (ly < zeilen[k]->get_allocated_height() / 2) {
            pos = (int)k;
            break;
        }
    }
    // direkt vor oder hinter der eigenen Zeile: nichts ändert sich
    if (best == zg.quell_lb) {
        for (std::size_t k = 0; k < zeilen.size(); ++k) {
            if (zeilen[k] == zg.quell_widget && (pos == (int)k || pos == (int)k + 1)) pos = -1;
        }
    }
    if (pos >= 0 && pos < (int)zeilen.size() && zeilen[pos]->get_name() == "pass") pos = -1;
    if (best == zg.ziel_lb && pos == zg.ziel_pos) return;
    luecke_schliessen();
    zg.ziel_lb = best;
    zg.ziel_pos = pos;
    if (pos < 0) return;
    auto row = Gtk::make_managed<Gtk::ListBoxRow>();
    row->set_name("luecke");
    row->set_activatable(false);
    row->set_selectable(false);
    auto rv = Gtk::make_managed<Gtk::Revealer>();
    rv->set_transition_type(Gtk::REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    rv->set_transition_duration(150);
    auto platz = Gtk::make_managed<Gtk::Box>();
    platz->set_size_request(-1, zg.bh);
    klasse(*platz, "hirch-luecke");
    rv->add(*platz);
    row->add(*rv);
    // Stelle unter allen Kindern (schliessende Lücken mitgezählt)
    int stelle = -1;
    if (pos < (int)zeilen.size()) stelle = zeilen[pos]->get_index();
    best->insert(*row, stelle);
    row->show_all();
    rv->set_reveal_child(true);
    zg.luecke = row;
}

void HirchFraesen::Impl::luecke_schliessen()
{
    auto l = zg.luecke;
    zg.luecke = nullptr;
    if (!l) return;
    auto rv = dynamic_cast<Gtk::Revealer *>(l);
    if (!rv) {
        if (auto bin = dynamic_cast<Gtk::Bin *>(l)) rv = dynamic_cast<Gtk::Revealer *>(bin->get_child());
    }
    luecken_zu.push_back(l);
    if (!rv) {
        luecke_entfernen(l);
        return;
    }
    rv->property_child_revealed().signal_changed().connect([this, l, rv] {
        if (rv->get_child_revealed()) return;
        Glib::signal_idle().connect_once([this, l] { luecke_entfernen(l); });
    });
    rv->set_reveal_child(false);
}

void HirchFraesen::Impl::luecke_entfernen(Gtk::Widget *l)
{
    auto it = std::find(luecken_zu.begin(), luecken_zu.end(), l);
    if (it == luecken_zu.end()) return;              // schon mit der Liste verschwunden
    luecken_zu.erase(it);
    if (auto p = dynamic_cast<Gtk::Container *>(l->get_parent())) p->remove(*l);
}

void HirchFraesen::Impl::ziehen_beenden(bool ablegen)
{
    zg.aktiv = false;
    zg.gedrueckt = false;
    zg.takt.disconnect();
    zg.taste.disconnect();
    schwebe_fenster.hide();
    if (auto win = panel.get_window()) win->set_cursor();
    if (zg.quell_widget) zg.quell_widget->set_opacity(1.0);
    std::vector<int> q = zg.quelle;
    int anker = -1;
    bool davor = true;
    if (ablegen && zg.luecke) {
        if (zg.gruppe) {
            auto karten = echte_karten();
            int pos = zg.ziel_pos;
            Gtk::Widget *k = pos < (int)karten.size() ? karten[pos] : (karten.empty() ? nullptr : karten.back());
            davor = pos < (int)karten.size();
            for (auto lb : op_listen) {
                if (lb->get_parent() == k) {
                    auto z = gruppen_indizes(lb);
                    if (!z.empty()) anker = davor ? z.front() : z.back();
                }
            }
        } else if (zg.ziel_lb) {
            auto zeilen = echte_zeilen(zg.ziel_lb);
            int pos = zg.ziel_pos;
            davor = pos < (int)zeilen.size();
            auto r = davor ? zeilen[pos] : (zeilen.empty() ? nullptr : zeilen.back());
            if (r && r->get_name() != "pass") anker = std::atoi(r->get_name().c_str());
        }
    }
    zg.quell_widget = nullptr;
    zg.bild.clear();
    if (anker < 0 || std::find(q.begin(), q.end(), anker) != q.end()) {
        luecke_schliessen();
        if (anzeigen_wartet) {
            anzeigen_wartet = false;
            anzeigen();
        }
        return;
    }
    zg.luecke = nullptr;   // die Liste wird gleich neu gebaut – die Lücke bleibt bis dahin offen
    std::vector<int> abschnitt;   // Bearbeitungen der Werkzeuggruppe, in die abgelegt wird
    if (!zg.gruppe && zg.ziel_lb) abschnitt = gruppen_indizes(zg.ziel_lb);
    if (!zg.gruppe && q.size() == 1) farbe_beim_ablegen(q.front(), anker, abschnitt);
    einordnen(q, anker, davor);
}

void HirchFraesen::Impl::farbe_beim_ablegen(int i, int anker, std::vector<int> const &abschnitt)
{
    auto b = bearbeitung(i);
    if (!b) return;
    auto art = str(*b, "art");
    auto gs = geschwister(i);
    if (!gs.empty()) {
        bool bleibt = false;
        for (auto g : gs) bleibt |= std::find(abschnitt.begin(), abschnitt.end(), g) != abschnitt.end();
        if (!bleibt) (*b)["farbe"] = freier_ton(art);
        return;
    }
    auto a = bearbeitung(anker);
    if (a && str(*a, "art") == art && objekte(str(*b, "id")) == 0 && !ohne_objekte(art) &&
        num(*a, "auf_seite", 1) == num(*b, "auf_seite", 1))
        (*b)["farbe"] = str(*a, "farbe");
}

void HirchFraesen::Impl::einordnen(std::vector<int> q, int anker, bool davor)
{
    auto bea = liste_von(einst, "bearbeitungen");
    if (!bea || q.empty()) return;
    int n = (int)bea->size();
    for (auto i : q) if (i < 0 || i >= n) return;
    if (anker < 0 || anker >= n) return;
    std::sort(q.begin(), q.end());
    std::string anker_id = str((*bea)[anker].as_object(), "id");
    std::string erste_id = str((*bea)[q.front()].as_object(), "id");
    std::string erste_name = name_von((*bea)[q.front()].as_object());
    std::string anker_name = name_von((*bea)[anker].as_object());
    bj::array rest, wandern;
    for (int i = 0; i < n; ++i)
        (std::binary_search(q.begin(), q.end(), i) ? wandern : rest).push_back((*bea)[i]);
    bj::array neu_;
    for (auto const &v : rest) {
        bool ist = v.is_object() && str(v.as_object(), "id") == anker_id;
        if (ist && davor) for (auto const &w : wandern) neu_.push_back(w);
        neu_.push_back(v);
        if (ist && !davor) for (auto const &w : wandern) neu_.push_back(w);
    }
    if ((int)neu_.size() != n) return;
    *bea = std::move(neu_);
    gewaehlt_index = index_von(erste_id);
    vorlaeufig_bauen();
    schreiben(q.size() > 1 ? TRF("Werkzeuggruppe verschoben ({} {})", {davor ? TR("vor") : TR("nach"), anker_name})
                           : TRF("{} verschoben ({} {})", {erste_name, davor ? TR("vor") : TR("nach"), anker_name}));
}

void HirchFraesen::Impl::vorlaeufig_bauen()
{
    auto ab = ablauf_daten();
    vorlaeufig_an = false;
    if (!ab) return;
    // spannung: Umspannen (Entwurf C) – Spannung 1 und 2 bleiben getrennte Abschnitte (Windows 08.11.: sonst standen die
    // Bearbeitungen der Spannung 2 doppelt in der Gruppe der Spannung 1)
    struct T { bj::object op; std::string slot, werkzeug; int seite; double pos; std::string art; int spannung; };
    std::vector<T> teile;
    std::map<std::string, int> je_id;
    std::map<std::string, bj::object> spindel_je_slot;   // Drehzahl/Hinweis der letzten Rechnung
    std::set<std::string> schon, in_analyse, alte_ids;
    double letzte_pos = -1e6;
    int erzeugt = 0;
    // quelle: der bisherige Ablauf (mit Zeiten); dazu aus der neuesten Analyse, was dort neu ist
    // (Peter 28.10.: neue Bearbeitungen stehen sofort an ihrem Platz, gelöschte verschwinden)
    auto einlesen = [&](bj::object const &quelle, bool nur_neue) {
        auto seiten = liste_von(quelle, "seiten");
        if (!seiten) return;
        for (auto const &sv : *seiten) {
            auto so = sv.if_object();
            if (!so) continue;
            int seite = (int)num(*so, "seite", 1);
            letzte_pos = -1e6;
            auto abs = liste_von(*so, "abschnitte");
            if (!abs) continue;
            for (auto const &av : *abs) {
                auto a = av.if_object();
                if (!a) continue;
                if (!nur_neue) {
                    bj::object sp;
                    for (char const *k : {"drehzahl", "reibt", "fz", "drehzahl_ziel", "vorschub_ziel"})
                        if (a->contains(k)) sp[k] = a->at(k);
                    spindel_je_slot[str(*a, "slot")] = sp;
                }
                auto ops = liste_von(*a, "ops");
                if (!ops) continue;
                for (auto const &ov : *ops) {
                    auto o = ov.if_object();
                    if (!o) continue;
                    auto id = str(*o, "id");
                    auto key = op_schluessel(*o);
                    if (nur_neue) {
                        in_analyse.insert(id);
                        // nur ganz neue Bearbeitungen: was die Rechnung weggelassen hat (z. B. Restmaterial
                        // ohne Rest), bleibt weg
                        if (schon.count(key) || alte_ids.count(id)) continue;
                    } else {
                        alte_ids.insert(id);
                    }
                    schon.insert(key);
                    int idx = index_von(id);
                    int k = je_id[id]++;
                    // Hauptteil: Werkzeug aus den Einstellungen – nach einem Wechsel des
                    // Fräsers steht die Zeile sofort in der richtigen Gruppe
                    std::string slot = str(*a, "slot"), wz = str(*a, "werkzeug");
                    if (idx >= 0 && str(*o, "teil").empty()) {
                        if (auto bb = bearbeitung(idx)) {
                            auto neu_slot = str(*bb, "werkzeug");
                            if (!neu_slot.empty() && neu_slot != slot) {
                                slot = neu_slot;
                                wz = wz_titel(neu_slot);
                            }
                        }
                    }
                    bj::object op = *o;
                    if (nur_neue) op["ungerechnet"] = true;
                    // erzeugte Bearbeitungen (Passlöcher, Kerbe – nicht in der Liste): an ihrer Stelle im
                    // gerechneten Ablauf bleiben, nicht ans Ende rücken (Abnahme Windows 03.11.: Passlöcher)
                    double pos = idx < 0 ? letzte_pos + 0.001 * (++erzeugt) : idx * 100.0 + k;
                    if (idx >= 0) letzte_pos = pos;
                    teile.push_back({std::move(op), slot, wz, seite, pos, str(*o, "art"), (int)num(*a, "spannung", 0)});
                }
            }
        }
    };
    einlesen(*ab, false);
    if (auto an = unter(analyse, "ablauf"); an && an != ab) einlesen(*an, true);
    // gelöscht (oder in der neuesten Analyse nicht mehr da): weg
    teile.erase(std::remove_if(teile.begin(), teile.end(), [&](T const &t) {
        auto id = str(t.op, "id");
        if (index_von(id) >= 0) return false;
        return geloeschte_ids.count(id) > 0 || (!in_analyse.empty() && !in_analyse.count(id));
    }), teile.end());
    if (teile.empty()) return;
    bool liste_modus = str(einst, "reihenfolge", "auto") == "liste";
    struct A { int seite; std::string slot, werkzeug; std::vector<T const *> ops; int spannung = 0; };
    std::vector<A> folge;
    std::set<int> seiten_nr;
    for (auto const &t : teile) seiten_nr.insert(t.seite);
    for (int seite : seiten_nr) {
      std::set<int> spannungen;
      for (auto const &t : teile)
          if (t.seite == seite) spannungen.insert(t.spannung);
      for (int spannung : spannungen) {
        std::vector<T const *> hier;
        for (auto const &t : teile)
            if (t.seite == seite && t.spannung == spannung) hier.push_back(&t);
        std::stable_sort(hier.begin(), hier.end(), [](T const *a, T const *b) { return a->pos < b->pos; });
        if (liste_modus) {
            for (auto t : hier) {
                if (!folge.empty() && folge.back().seite == seite && folge.back().spannung == spannung &&
                    folge.back().slot == t->slot)
                    folge.back().ops.push_back(t);
                else
                    folge.push_back({seite, t->slot, t->werkzeug, {t}, spannung});
            }
        } else {
            std::vector<A> gruppen;
            for (auto t : hier) {
                auto it = std::find_if(gruppen.begin(), gruppen.end(), [&](A const &g) { return g.slot == t->slot; });
                if (it == gruppen.end()) gruppen.push_back({seite, t->slot, t->werkzeug, {t}, spannung});
                else it->ops.push_back(t);
            }
            auto stufe = [](A const &g) {
                bool kontur = false, planen = false;
                for (auto t : g.ops) {
                    kontur |= t->art == "kontur";
                    planen |= t->art == "planen";
                }
                return kontur ? 2 : (planen ? 0 : 1);
            };
            std::stable_sort(gruppen.begin(), gruppen.end(), [&](A const &a, A const &b) {
                int sa = stufe(a), sb = stufe(b);
                if (sa != sb) return sa < sb;
                return a.ops.front()->pos < b.ops.front()->pos;
            });
            for (auto &g : gruppen) folge.push_back(std::move(g));
        }
      }
    }
    std::map<std::string, int> nummer;
    for (auto const &a : folge)
        if (!nummer.count(a.slot)) nummer[a.slot] = (int)nummer.size() + 1;
    bj::array seiten;
    std::string vorher_slot;
    bool erster_slot = true;
    A const *davor = nullptr;
    for (auto const &a : folge) {
        bool neue_seite = seiten.empty() || (int)num(seiten.back().as_object(), "seite", 1) != a.seite;
        if (neue_seite) seiten.push_back(bj::object{{"seite", a.seite}, {"minuten", 0.0}, {"abschnitte", bj::array{}}});
        auto &so = seiten.back().as_object();
        bj::array ops;
        double m = 0;
        for (auto t : a.ops) {
            ops.push_back(t->op);
            m += num(t->op, "minuten", 0);
        }
        so["minuten"] = num(so, "minuten", 0) + m;
        bj::object abo{
            {"slot", a.slot}, {"t", nummer[a.slot]}, {"werkzeug", a.werkzeug},
            {"wechsel", !erster_slot && a.slot != vorher_slot}, {"minuten", m}, {"ops", std::move(ops)},
            {"nach_wenden", neue_seite && a.seite == 2}};
        if (auto it = spindel_je_slot.find(a.slot); it != spindel_je_slot.end())
            for (auto const &kv : it->second) abo[kv.key()] = kv.value();
        if (a.spannung > 0) {
            abo["spannung"] = a.spannung;
            abo["nach_umspannen"] = a.spannung == 2 && (!davor || davor->spannung != 2 || davor->seite != a.seite);
        }
        davor = &a;
        so["abschnitte"].as_array().push_back(std::move(abo));
        vorher_slot = a.slot;
        erster_slot = false;
    }
    vorlaeufig = bj::object{{"zweiseitig", zweiseitig()}, {"seiten", std::move(seiten)}, {"vorab", true}};
    vorlaeufig_an = true;
}

} // namespace Inkscape::UI::Dialog
