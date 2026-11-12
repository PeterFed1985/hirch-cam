// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Postprozessoren, Stege, Aktualisieren, Seite 2, Vorrichtung, Passlöcher (Teile: hirch-werkzeugdialog.cpp, hirch-auftragdialog.cpp)
 */
#include "ui/dialog/hirch-intern.h"
#include "ui/dialog/hirch-dateiwahl.h"
#include "ui/dialog/hirch-3d.h"
#include <gtkmm/scale.h>

namespace Inkscape::UI::Dialog {

bj::object const *HirchFraesen::Impl::post_finden(PostStand const &st, std::string const &k)
{
    for (auto const &v : st.liste) {
        auto const *o = v.if_object();
        if (o && str(*o, "schluessel") == k) return o;
    }
    return nullptr;
}

std::string HirchFraesen::Impl::zeilen_text(bj::object const &o, char const *feld)
{
    std::string t;
    if (auto a = liste_von(o, feld)) {
        for (auto const &z : *a) {
            if (!t.empty()) t += "\n";
            if (z.is_string()) t += std::string(z.get_string());
        }
    }
    return t;
}

bj::object HirchFraesen::Impl::post_formular(PostStand const &st)
{
    bj::object o;
    o["schluessel"] = st.key;
    o["name"] = st.name->get_text().raw();
    o["beschreibung"] = st.beschr->get_text().raw();
    o["endung"] = st.endung->get_text().raw();
    o["werkzeugwechsel"] = st.eine->get_active();
    o["wechselposition"] = st.wpos->get_active();
    o["wechsel_x"] = wert(st.wx);
    o["wechsel_y"] = wert(st.wy);
    if (st.zsicher->get_active()) o["wechsel_z"] = nullptr;
    else o["wechsel_z"] = wert(st.wz);
    o["leerzeichen"] = st.leer->get_active();
    char const *namen[4] = {"kopf", "werkzeug", "wechsel", "ende"};
    for (int i = 0; i < 4; ++i) o[namen[i]] = st.felder[i]->get_buffer()->get_text().raw();
    o["boegen"] = st.boegen->get_active();
    o["kommentar"] = st.kommentar->get_text().raw();
    o["zeilennummern"] = (int)wert(st.nummern);
    if (st.zyklen->get_active()) {
        bj::object z;
        char const *zn[4] = {"entspanen", "einfach", "verweilen", "ende"};
        for (int i = 0; i < 4; ++i) {
            bj::array a;
            auto t = st.zy[i]->get_text().raw();
            if (!t.empty()) a.push_back(bj::string(t));
            z[zn[i]] = a;
        }
        o["bohrzyklus"] = z;
    } else {
        o["bohrzyklus"] = nullptr;
    }
    return o;
}

void HirchFraesen::Impl::post_liste_bauen(std::shared_ptr<PostStand> st)
{
    st->laden = true;
    UI::remove_all_children(*st->lb);
    Gtk::ListBoxRow *wahl = nullptr;
    for (auto const &v : st->liste) {
        auto const *o = v.if_object();
        if (!o) continue;
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
        box->set_margin_start(8); box->set_margin_end(8); box->set_margin_top(6); box->set_margin_bottom(6);
        auto n = label(str(*o, "name", str(*o, "schluessel")), "hirch-titel");
        n->set_ellipsize(Pango::ELLIPSIZE_END);
        box->add(*n);
        std::string art = flag(*o, "entfernt", false) ? TR("ausgeblendet")
                          : !flag(*o, "mitgeliefert", false) ? TR("eigener")
                          : flag(*o, "geaendert", false) ? TR("mitgeliefert, geändert") : TR("mitgeliefert");
        art += " · " + str(*o, "endung", ".tap") +
               (flag(*o, "werkzeugwechsel", true) ? TR(" · eine Datei") : TR(" · Datei je Werkzeug"));
        auto a = label(art, "hirch-dim");
        klasse(*a, "hirch-klein");
        box->add(*a);
        row->add(*box);
        row->set_name(str(*o, "schluessel"));
        if (flag(*o, "entfernt", false)) klasse(*box, "hirch-aus");
        st->lb->append(*row);
        if (str(*o, "schluessel") == st->key) wahl = row;
    }
    st->lb->show_all();
    if (wahl) st->lb->select_row(*wahl);
    st->laden = false;
}

void HirchFraesen::Impl::post_laden(std::shared_ptr<PostStand> st, std::string const &k)
{
    auto const *o = post_finden(*st, k);
    if (!o) return;
    st->key = k;
    st->laden = true;
    st->name->set_text(str(*o, "name"));
    st->beschr->set_text(str(*o, "beschreibung"));
    st->endung->set_text(str(*o, "endung", ".tap"));
    st->eine->set_active(flag(*o, "werkzeugwechsel", true));
    st->wpos->set_active(flag(*o, "wechselposition", true));
    st->wx->set_value(num(*o, "wechsel_x", 0));
    st->wy->set_value(num(*o, "wechsel_y", 0));
    bool zs = ist_null(*o, "wechsel_z");
    st->zsicher->set_active(zs);
    st->wz->set_value(zs ? 50 : num(*o, "wechsel_z", 50));
    st->leer->set_active(flag(*o, "leerzeichen", false));
    char const *namen[4] = {"kopf", "werkzeug", "wechsel", "ende"};
    for (int i = 0; i < 4; ++i) st->felder[i]->get_buffer()->set_text(zeilen_text(*o, namen[i]));
    st->boegen->set_active(flag(*o, "boegen", true));
    st->kommentar->set_text(str(*o, "kommentar"));
    st->nummern->set_value(num(*o, "zeilennummern", 0));
    {
        auto z = unter(*o, "bohrzyklus");
        char const *zn[4] = {"entspanen", "einfach", "verweilen", "ende"};
        bool an = z && (!zeilen_text(*z, "entspanen").empty() || !zeilen_text(*z, "einfach").empty());
        st->zyklen->set_active(an);
        for (int i = 0; i < 4; ++i) {
            std::string t = z ? zeilen_text(*z, zn[i]) : std::string();
            if (t.empty() && !an) {
                // Vorschlag (RS274/NGC), falls man die Bohrzyklen einschaltet
                static char const *vorschlag[4] = {"G98 G83 X{x} Y{y} Z{z} R{r} Q{q} F{f}",
                                                   "G98 G81 X{x} Y{y} Z{z} R{r} F{f}",
                                                   "G98 G82 X{x} Y{y} Z{z} R{r} P{p} F{f}", "G80"};
                t = vorschlag[i];
            }
            st->zy[i]->set_text(t);
        }
    }
    bool weg = flag(*o, "entfernt", false);
    st->formular->set_sensitive(!weg);
    st->b_entf->set_sensitive(!weg);
    st->b_zur->set_sensitive(flag(*o, "mitgeliefert", false) && (weg || flag(*o, "geaendert", false)));
    st->status->set_text(weg ? TR("Ausgeblendet – «Zurücksetzen» holt ihn zurück.")
                         : flag(*o, "mitgeliefert", false)
                             ? (flag(*o, "geaendert", false) ? TR("Mitgeliefert, von dir geändert.")
                                                             : TR("Mitgeliefert. Änderungen werden als eigene Fassung gespeichert."))
                             : TR("Eigener Postprozessor."));
    st->laden = false;
    st->schmutzig = false;
    post_abhaengig(st);
    post_beispiel(st);
}

void HirchFraesen::Impl::post_abhaengig(std::shared_ptr<PostStand> st)
{
    bool p = st->wpos->get_active();
    st->wx->set_sensitive(p);
    st->wy->set_sensitive(p);
    st->zsicher->set_sensitive(p);
    st->wz->set_sensitive(p && !st->zsicher->get_active());
    st->felder[2]->set_sensitive(st->eine->get_active());
    zeile_zeigen(st->zy_grid, st->zyklen->get_active());
}

void HirchFraesen::Impl::post_beispiel(std::shared_ptr<PostStand> st)
{
    if (!st->lebt) return;
    python({"post", "beispiel", bj::serialize(post_formular(*st))},
           [st](bool ok, std::string const &o, std::string const &e) {
               if (!st->lebt) return;
               bj::object r;
               std::string t;
               if (ok && json_lesen(o, r)) t = flag(r, "ok", false) ? str(r, "text") : TR("Fehler: ") + str(r, "fehler");
               else t = TR("Fehler: ") + e.substr(0, 400);
               st->beispiel->get_buffer()->set_text(t);
           });
}

void HirchFraesen::Impl::post_speichern(std::shared_ptr<PostStand> st, std::function<void(std::string const &)> danach)
{
    auto daten = post_formular(*st);
    st->schmutzig = false;
    python({"post", "speichern", bj::serialize(daten)},
           [this, st, danach](bool ok, std::string const &o, std::string const &e) {
               bj::object r;
               if (!ok || !json_lesen(o, r)) {
                   if (st->lebt) st->status->set_text(TR("Speichern fehlgeschlagen: ") + e.substr(0, 300));
                   return;
               }
               if (auto p = liste_von(r, "postprozessoren")) st->liste = *p;
               if (!flag(r, "ok", false)) {
                   if (st->lebt) st->status->set_text(TR("Speichern fehlgeschlagen: ") + str(r, "fehler"));
                   return;
               }
               if (st->lebt) post_liste_bauen(st);
               if (danach) danach(str(r, "schluessel"));
           });
}

void HirchFraesen::Impl::post_befehl(std::shared_ptr<PostStand> st, std::string const &befehl, std::string const &k)
{
    python({"post", befehl, k}, [this, st](bool ok, std::string const &o, std::string const &) {
        bj::object r;
        if (!st->lebt || !ok || !json_lesen(o, r)) return;
        if (auto p = liste_von(r, "postprozessoren")) st->liste = *p;
        if (!post_finden(*st, st->key)) {               // gelöscht: ersten zeigen
            st->key.clear();
            for (auto const &v : st->liste)
                if (auto const *q = v.if_object()) { st->key = str(*q, "schluessel"); break; }
        }
        post_liste_bauen(st);
        post_laden(st, st->key);
    });
}

void HirchFraesen::Impl::postprozessoren_dialog(std::string const &zeigen,
                                std::function<void(std::vector<std::pair<std::string, std::string>> const &)> danach)
{
    python({"post", "liste"}, [this, zeigen, danach](bool ok, std::string const &o, std::string const &e) {
        bj::object r;
        if (!ok || !json_lesen(o, r)) {
            Gtk::MessageDialog m(TR("Postprozessoren konnten nicht gelesen werden"), false, Gtk::MESSAGE_ERROR);
            m.set_title("Hirch CAM");
            m.set_secondary_text(e.substr(0, 600));
            m.run();
            return;
        }
        auto st = std::make_shared<PostStand>();
        if (auto p = liste_von(r, "postprozessoren")) st->liste = *p;
        st->key = zeigen.empty() ? str(einst, "postprozessor", "richauto_a11") : zeigen;
        if (!post_finden(*st, st->key) && !st->liste.empty())
            if (auto const *q = st->liste[0].if_object()) st->key = str(*q, "schluessel");
        postprozessoren_zeigen(st, danach);
    });
}

void HirchFraesen::Impl::postprozessoren_zeigen(std::shared_ptr<PostStand> st,
                                std::function<void(std::vector<std::pair<std::string, std::string>> const &)> danach)
{
    auto dlg = dialog(TR("Postprozessoren"));
    dlg->set_resizable(true);
    dlg->set_default_size(980, 680);
    auto inhalt = dlg->get_content_area();
    auto haupt = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 14);
    haupt->set_vexpand(true);
    inhalt->pack_start(*haupt, true, true);

    // links: Liste
    auto links = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
    links->set_size_request(260, -1);
    auto sw = Gtk::make_managed<Gtk::ScrolledWindow>();
    sw->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
    sw->set_vexpand(true);
    st->lb = Gtk::make_managed<Gtk::ListBox>();
    klasse(*st->lb, "hirch-liste");
    sw->add(*st->lb);
    links->add(*sw);
    auto knoepfe = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
    auto b_neu = Gtk::make_managed<Gtk::Button>(TR("Kopie anlegen"));
    b_neu->set_tooltip_text(TR("Neuen Postprozessor als Kopie des gewählten anlegen"));
    st->b_entf = Gtk::make_managed<Gtk::Button>(TR("Entfernen"));
    st->b_entf->set_tooltip_text(TR("Eigene werden gelöscht, mitgelieferte nur ausgeblendet"));
    st->b_zur = Gtk::make_managed<Gtk::Button>(TR("Zurücksetzen"));
    st->b_zur->set_tooltip_text(TR("Mitgelieferten Postprozessor wieder wie ausgeliefert"));
    knoepfe->add(*b_neu);
    knoepfe->add(*st->b_entf);
    knoepfe->add(*st->b_zur);
    links->add(*knoepfe);
    // Einzeln weitergeben (Peter 06.11.): eine Datei je Postprozessor; importiert wird immer als eigener, ungeprüft
    auto knoepfe2 = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
    auto b_imp = Gtk::make_managed<Gtk::Button>(TR("Importieren …"));
    b_imp->set_tooltip_text(TR("Postprozessor aus einer Datei übernehmen – als eigener, vorhandene bleiben"));
    auto b_exp = Gtk::make_managed<Gtk::Button>(TR("Exportieren …"));
    b_exp->set_tooltip_text(TR("Den gewählten Postprozessor als Datei speichern, z. B. zum Weitergeben"));
    knoepfe2->add(*b_imp);
    knoepfe2->add(*b_exp);
    links->add(*knoepfe2);
    haupt->add(*links);

    // rechts: Formular
    auto rechts = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
    rechts->set_hexpand(true);
    st->formular = rechts;
    auto g = formular();
    int z = 0;
    auto zeile = [&](std::string const &t, Gtk::Widget &w) {
        g->attach(*label(t), 0, z);
        w.set_hexpand(true);
        g->attach(w, 1, z++, 3);
    };
    st->name = Gtk::make_managed<Gtk::Entry>();
    zeile("Name", *st->name);
    st->beschr = Gtk::make_managed<Gtk::Entry>();
    st->beschr->set_placeholder_text(TR("Hinweise zur Steuerung (frei)"));
    zeile(TR("Beschreibung"), *st->beschr);
    st->endung = Gtk::make_managed<Gtk::Entry>();
    st->endung->set_width_chars(8);
    st->endung->set_hexpand(false);
    g->attach(*label(TR("Dateiendung")), 0, z);
    g->attach(*st->endung, 1, z++);
    st->eine = Gtk::make_managed<Gtk::CheckButton>(TR("Werkzeugwechsel: alle Werkzeuge in eine Datei"));
    st->eine->set_tooltip_text(TR("Aus: eine Datei je Werkzeug (Wechsel von Hand zwischen den Dateien)"));
    g->attach(*st->eine, 0, z++, 4);
    // Peter 27.10.: nicht still übersteuern – sagen, wo es sonst eingestellt wird
    auto eine_info = label(TR("Gilt, solange in «Einstellungen › Maschine» der Werkzeugwechsel auf «wie die "
                              "Steuerung» steht."), "hirch-dim");
    klasse(*eine_info, "hirch-klein");
    eine_info->set_line_wrap(true);
    eine_info->set_xalign(0);
    eine_info->set_margin_start(26);
    g->attach(*eine_info, 0, z++, 4);
    st->wpos = Gtk::make_managed<Gtk::CheckButton>(TR("Zum Wechseln an eine feste Stelle fahren"));
    st->wpos->set_tooltip_text(TR("Aus: Zeilen mit {wx}, {wy} oder {wz} im Werkzeugwechsel werden weggelassen – "
                               "die Maschine wechselt dort, wo sie gerade steht."));
    g->attach(*st->wpos, 0, z++, 4);
    auto wgrid = Gtk::make_managed<Gtk::Grid>();
    wgrid->set_column_spacing(8);
    wgrid->set_row_spacing(6);
    wgrid->set_margin_start(26);
    st->wx = spin_zeile(*wgrid, 0, "X", 0, -5000, 5000, 1, 3, "mm");
    st->wy = spin_zeile(*wgrid, 1, "Y", 0, -5000, 5000, 1, 3, "mm");
    st->wz = spin_zeile(*wgrid, 2, "Z", 50, -1000, 1000, 1, 3, "mm");
    st->zsicher = Gtk::make_managed<Gtk::CheckButton>(TR("Z = Sicherheitshöhe"));
    wgrid->attach(*st->zsicher, 3, 2);
    g->attach(*wgrid, 0, z++, 4);
    st->leer = Gtk::make_managed<Gtk::CheckButton>(TR("Leerzeichen zwischen den Wörtern (G0 X1.000 Y2.000)"));
    g->attach(*st->leer, 0, z++, 4);
    // Was die Steuerung kann
    st->boegen = Gtk::make_managed<Gtk::CheckButton>(TR("Steuerung kann Bögen (G2/G3)"));
    st->boegen->set_tooltip_text(TR("Aus: Rundungen immer als kurze Geraden, auch wenn beim Auftrag «Bögen» "
                                 "eingeschaltet ist"));
    g->attach(*st->boegen, 0, z++, 4);
    st->kommentar = Gtk::make_managed<Gtk::Entry>();
    st->kommentar->set_placeholder_text(TR("z. B. ({text}) – leer: keine Kommentare"));
    st->kommentar->set_tooltip_text(TR("So steht ein Kommentar in der Datei; {text} ist der Name der Bearbeitung. "
                                    "Vor jeder Bearbeitung steht dann ihr Name. Leer, wenn die Steuerung keine "
                                    "Kommentare versteht."));
    st->kommentar->set_hexpand(false);
    st->kommentar->set_width_chars(16);
    g->attach(*label(TR("Kommentare")), 0, z);
    g->attach(*st->kommentar, 1, z++);
    st->nummern = spin_zeile(*g, z++, TR("Zeilennummern"), 0, 0, 100, 1, 0, TR("Abstand (N10, N20 …), 0 = keine"));
    rechts->add(*g);

    st->nb = Gtk::make_managed<Gtk::Notebook>();
    st->nb->set_vexpand(true);
    char const *titel[4] = {TR("Dateianfang"), TR("Werkzeuganfang"), TR("Werkzeugwechsel"), TR("Dateiende")};
    auto neu_text = [](bool edit) {
        auto tv = Gtk::make_managed<Gtk::TextView>();
        tv->set_monospace(true);
        tv->set_editable(edit);
        tv->set_left_margin(6);
        tv->set_top_margin(4);
        auto s = Gtk::make_managed<Gtk::ScrolledWindow>();
        s->add(*tv);
        return std::make_pair(tv, s);
    };
    for (int i = 0; i < 4; ++i) {
        auto [tv, s] = neu_text(true);
        st->felder[i] = tv;
        st->nb->append_page(*s, titel[i]);
    }
    {
        // Bohrzyklen: eine Zeile je Art
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
        box->set_margin_start(10);
        box->set_margin_end(10);
        box->set_margin_top(10);
        st->zyklen = Gtk::make_managed<Gtk::CheckButton>(TR("Steuerung kennt Bohrzyklen"));
        st->zyklen->set_tooltip_text(TR("Aus: jede Bohrung steht als einzelne Bewegungen in der Datei (das versteht "
                                     "jede Steuerung). Ein: eine Zeile je Bohrung."));
        box->add(*st->zyklen);
        auto zg = formular();
        char const *zt[4] = {TR("Mit Entspanen"), TR("In einem Zug"), TR("Mit Verweilen"), TR("Zyklus beenden")};
        for (int i = 0; i < 4; ++i) {
            st->zy[i] = Gtk::make_managed<Gtk::Entry>();
            st->zy[i]->set_hexpand(true);
            zg->attach(*label(zt[i]), 0, i);
            zg->attach(*st->zy[i], 1, i, 2);
        }
        auto zh = label(TR("Platzhalter: {x} {y} Lage · {z} tiefste Stelle · {r} Rückzugsebene · {q} Zustellung je "
                        "Hub · {p} Verweilzeit (s) · {f} Vorschub · {oben} Höhe davor und danach. «Mit Entspanen» "
                        "gilt bei mehreren Hüben, «In einem Zug» sonst (fehlt eine, gilt die andere)."), "hirch-dim");
        zh->set_line_wrap(true);
        zh->set_max_width_chars(80);
        klasse(*zh, "hirch-klein");
        zg->attach(*zh, 0, 4, 3);
        box->add(*zg);
        st->zy_grid = zg;
        st->nb->append_page(*box, TR("Bohrzyklus"));
    }
    {
        auto [tv, s] = neu_text(false);
        st->beispiel = tv;
        st->nb->append_page(*s, TR("Beispiel"));
    }
    rechts->add(*st->nb);
    auto hilfe = label(TR("Platzhalter: {t} Werkzeugnummer · {s} Drehzahl · {werkzeug} Name · {d} Ø · "
                       "{sicher} Sicherheitshöhe · {hx} {hy} Startpunkt · {wx} {wy} {wz} Wechselposition · "
                       "{datei} Dateiname · {datum} Datum. Im Werkzeugwechsel steht {t} für das nächste "
                       "Werkzeug; danach folgt der Werkzeuganfang. Die Fräsbahnen dazwischen schreibt "
                       "Hirch CAM selbst (G0/G1, auf Wunsch G2/G3, Bohrungen siehe «Bohrzyklus»). Anhalten zum "
                       "Werkzeugwechsel (T…M6, M0 …) gehört in «Werkzeugwechsel». Anleitung: "
                       "docs/POSTPROZESSOR.md."), "hirch-dim");
    hilfe->set_line_wrap(true);
    hilfe->set_max_width_chars(90);
    klasse(*hilfe, "hirch-klein");
    rechts->add(*hilfe);
    auto rechts_aussen = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
    rechts_aussen->set_hexpand(true);
    rechts_aussen->pack_start(*rechts, true, true);
    st->status = label("", "hirch-dim");
    klasse(*st->status, "hirch-klein");
    rechts_aussen->add(*st->status);
    haupt->pack_start(*rechts_aussen, true, true);

    // Signale
    auto geaendert = [this, st] {
        if (st->laden) return;
        st->schmutzig = true;
        post_abhaengig(st);
        st->beispiel_zeit.disconnect();
        st->beispiel_zeit = Glib::signal_timeout().connect([this, st] { post_beispiel(st); return false; }, 400);
    };
    st->name->signal_changed().connect(geaendert);
    st->beschr->signal_changed().connect(geaendert);
    st->endung->signal_changed().connect(geaendert);
    for (auto c : {st->eine, st->wpos, st->zsicher, st->leer, st->boegen, st->zyklen}) c->signal_toggled().connect(geaendert);
    for (auto s : {st->wx, st->wy, st->wz, st->nummern}) s->signal_value_changed().connect(geaendert);
    st->kommentar->signal_changed().connect(geaendert);
    for (auto e : st->zy) e->signal_changed().connect(geaendert);
    for (auto tv : st->felder) tv->get_buffer()->signal_changed().connect(geaendert);

    st->lb->signal_row_selected().connect([this, st](Gtk::ListBoxRow *row) {
        if (st->laden || !row) return;
        std::string k = row->get_name().raw();
        if (k == st->key) return;
        if (st->schmutzig) post_speichern(st);
        post_laden(st, k);
    });
    b_neu->signal_clicked().connect([this, st] {
        std::string n = st->name->get_text().raw();
        st->laden = true;
        st->name->set_text(TR("Kopie von ") + n);
        st->laden = false;
        std::string alt = st->key;
        st->key.clear();                                // neuer Schlüssel aus dem Namen
        post_speichern(st, [this, st, alt](std::string const &k) {
            if (!st->lebt) return;
            if (k.empty()) { st->key = alt; return; }
            st->key = k;
            post_liste_bauen(st);
            post_laden(st, k);
            st->name->grab_focus();
        });
    });
    Gtk::Dialog *dlg_p = dlg.get();
    st->b_entf->signal_clicked().connect([this, st, dlg_p] {
        auto const *o = post_finden(*st, st->key);
        if (!o) return;
        bool mit = flag(*o, "mitgeliefert", false);
        Gtk::MessageDialog m(*dlg_p, mit ? TR("Postprozessor ausblenden?") : TR("Postprozessor löschen?"), false,
                             Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
        m.set_title("Hirch CAM");
        m.add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
        klasse(*m.add_button(mit ? TR("Ausblenden") : TR("Löschen"), Gtk::RESPONSE_OK), "destructive-action");
        m.set_secondary_text(mit ? "«" + str(*o, "name") + TR("» wird mitgeliefert und nur ausgeblendet; "
                                   "«Zurücksetzen» holt ihn zurück.")
                                 : "«" + str(*o, "name") + TR("» wird gelöscht."));
        if (m.run() != Gtk::RESPONSE_OK) return;
        st->schmutzig = false;
        post_befehl(st, "entfernen", st->key);
    });
    st->b_zur->signal_clicked().connect([this, st] {
        st->schmutzig = false;
        post_befehl(st, "zuruecksetzen", st->key);
    });
    auto ordner_merken = [](std::string const &datei) {
        Inkscape::Preferences::get()->setString("/dialogs/hirch-fraesen/post-ordner", Glib::path_get_dirname(datei));
    };
    auto ordner_setzen = [](Gtk::FileChooser &fc) {
        auto alt = Inkscape::Preferences::get()->getString("/dialogs/hirch-fraesen/post-ordner");
        if (!alt.empty()) fc.set_current_folder(alt);
        auto filter = Gtk::FileFilter::create();
        filter->set_name(TR("Hirch-CAM-Postprozessor (*.json)"));
        filter->add_pattern("*.json");
        fc.add_filter(filter);
    };
    b_imp->signal_clicked().connect([this, st, dlg_p, ordner_merken, ordner_setzen] {
        HirchDateiwahl dateiwahl(dlg_p, TR("Postprozessor importieren"), Gtk::FILE_CHOOSER_ACTION_OPEN,
                                 TR("Abbrechen"), TR("Öffnen"));
        ordner_setzen(dateiwahl.fc());
        if (!dateiwahl.run()) return;
        std::string datei = dateiwahl.fc().get_filename();
        dateiwahl.hide();
        ordner_merken(datei);
        auto weiter = [this, st, datei, dlg_p] {
            python({"post", "importieren", datei}, [this, st, dlg_p](bool ok, std::string const &o, std::string const &e) {
                bj::object r;
                if (!st->lebt) return;
                if (!ok || !json_lesen(o, r) || !flag(r, "ok", false)) {
                    Gtk::MessageDialog m(*dlg_p, TR("Postprozessor nicht importiert"), false, Gtk::MESSAGE_ERROR);
                    m.set_title("Hirch CAM");
                    m.set_secondary_text(str(r, "fehler", (e.empty() ? o : e).substr(0, 400)));
                    m.run();
                    return;
                }
                if (auto p = liste_von(r, "postprozessoren")) st->liste = *p;
                st->key = str(r, "schluessel");
                post_liste_bauen(st);
                post_laden(st, st->key);
                st->status->set_text(TR("Importiert – ungeprüft: den ersten Lauf in der Luft fahren."));
            });
        };
        if (st->schmutzig) post_speichern(st, [weiter](std::string const &) { weiter(); });
        else weiter();
    });
    b_exp->signal_clicked().connect([this, st, dlg_p, ordner_merken, ordner_setzen] {
        auto const *o = post_finden(*st, st->key);
        if (!o) return;
        HirchDateiwahl dateiwahl(dlg_p, TR("Postprozessor exportieren"), Gtk::FILE_CHOOSER_ACTION_SAVE,
                                 TR("Abbrechen"), TR("Speichern"));
        auto &fc = dateiwahl.fc();
        fc.set_do_overwrite_confirmation(true);
        ordner_setzen(fc);
        std::string vorschlag;
        for (char c : str(*o, "name", st->key)) {
            unsigned char u = (unsigned char)c;
            vorschlag += (std::isalnum(u) || c == '-' || c == '_' || u >= 0x80) ? c : (c == ' ' ? '_' : '\0');
        }
        vorschlag.erase(std::remove(vorschlag.begin(), vorschlag.end(), '\0'), vorschlag.end());
        fc.set_current_name((vorschlag.empty() ? st->key : vorschlag) + ".json");
        if (!dateiwahl.run()) return;
        std::string datei = fc.get_filename();
        dateiwahl.hide();
        if (datei.size() < 5 || datei.substr(datei.size() - 5) != ".json") datei += ".json";
        ordner_merken(datei);
        auto weiter = [this, st, datei, dlg_p] {
            python({"post", "exportieren", st->key, datei},
                   [st, datei, dlg_p](bool ok, std::string const &o, std::string const &e) {
                       bj::object r;
                       if (!st->lebt) return;
                       if (!ok || !json_lesen(o, r) || !flag(r, "ok", false)) {
                           Gtk::MessageDialog m(*dlg_p, TR("Postprozessor nicht exportiert"), false, Gtk::MESSAGE_ERROR);
                           m.set_title("Hirch CAM");
                           m.set_secondary_text(str(r, "fehler", (e.empty() ? o : e).substr(0, 400)));
                           m.run();
                           return;
                       }
                       st->status->set_text(TR("Gespeichert: ") + Glib::path_get_basename(datei));
                   });
        };
        if (st->schmutzig) post_speichern(st, [weiter](std::string const &) { weiter(); });
        else weiter();
    });

    dlg->add_button(TR("Schliessen"), Gtk::RESPONSE_CLOSE);
    auto b_speichern = dlg->add_button(TR("Speichern"), Gtk::RESPONSE_APPLY);   // Hauptknopf rechts (GESTALTUNG)
    klasse(*b_speichern, "suggested-action");
    dlg->signal_response().connect([this, st](int a) {
        if (a == Gtk::RESPONSE_APPLY && st->schmutzig) {
            post_speichern(st, [this, st](std::string const &) {
                if (st->lebt) {
                    post_laden(st, st->key);
                    st->status->set_text("Gespeichert.");
                }
            });
        }
    });

    post_liste_bauen(st);
    post_laden(st, st->key);
    dlg->show_all();
    int antwort;
    do antwort = dlg->run(); while (antwort == Gtk::RESPONSE_APPLY);
    st->beispiel_zeit.disconnect();

    auto fertig = [this, st, danach] {
        std::vector<std::pair<std::string, std::string>> paare;
        for (auto const &v : st->liste) {
            auto const *o = v.if_object();
            if (o && !flag(*o, "entfernt", false))
                paare.emplace_back(str(*o, "schluessel"), str(*o, "name", str(*o, "schluessel")));
        }
        if (danach) danach(paare);
        letzte_xml.clear();          // Panel: Liste der Steuerungen neu
        spaeter(10);
    };
    bool offen = st->schmutzig;
    if (offen) {
        auto daten = post_formular(*st);
        st->lebt = false;
        python({"post", "speichern", bj::serialize(daten)},
               [st, fertig](bool ok, std::string const &o, std::string const &) {
                   bj::object r;
                   if (ok && json_lesen(o, r))
                       if (auto p = liste_von(r, "postprozessoren")) st->liste = *p;
                   fertig();
               });
    } else {
        st->lebt = false;
        fertig();
    }
}

Inkscape::XML::Node *HirchFraesen::Impl::steg_ebene()
{
    auto d = doc();
    if (!d) return nullptr;
    auto root = d->getReprRoot();
    for (auto c = root->firstChild(); c; c = c->next()) {
        auto lab = c->attribute("inkscape:label");
        auto gm = c->attribute("inkscape:groupmode");
        if (lab && gm && (std::string(lab) == "Haltestege" || std::string(lab) == TR("Haltestege")) &&
            std::string(gm) == "layer")
            return c;
    }
    auto ebene = d->getReprDoc()->createElement("svg:g");
    ebene->setAttribute("inkscape:groupmode", "layer");
    ebene->setAttribute("inkscape:label", TR("Haltestege"));
    ebene->setAttribute("id", "ebene-haltestege");
    root->appendChild(ebene);
    Inkscape::GC::release(ebene);
    return ebene;
}

std::vector<SPItem *> HirchFraesen::Impl::steg_marken(std::string const &bid)
{
    std::vector<SPItem *> aus;
    auto d = doc();
    if (!d) return aus;
    std::function<void(SPObject *)> suchen = [&](SPObject *x) {
        if (auto a = x->getRepr()->attribute("hirch:steg")) {
            if (a == bid)
                if (auto it = cast<SPItem>(x)) aus.push_back(it);
            return;
        }
        for (auto &c : x->children) suchen(&c);
    };
    suchen(d->getRoot());
    return aus;
}

static Geom::Point steg_mitte(SPItem *it)
{
    // Mitte des Kreises (die Umgrenzung ist um ihn herum gleichmässig)
    auto b = it->documentVisualBounds();
    return b ? b->midpoint() : Geom::Point();
}

int HirchFraesen::Impl::steg_verschoben(std::string const &bid)
{
    int n = 0;
    double tol = 0.2 * MM_PX;                 // Lage in Dokument-Koordinaten (px)
    for (auto it : steg_marken(bid)) {
        auto lage = it->getRepr()->attribute("hirch:steg-lage");
        if (!lage) {
            ++n;            // von Hand gesetzt oder kopiert
            continue;
        }
        double x = 0, y = 0;
        std::istringstream is(lage);
        is.imbue(std::locale::classic());
        char k;
        if (!(is >> x >> k >> y)) {
            ++n;
            continue;
        }
        if (Geom::distance(steg_mitte(it), Geom::Point(x, y)) > tol) ++n;
    }
    return n;
}

SPItem *HirchFraesen::Impl::steg_marke(Geom::Point const &mitte, double breite_mm, std::string const &bid)
{
    auto d = doc();
    auto ebene = steg_ebene();
    if (!d || !ebene) return nullptr;
    double r = breite_mm * MM_PX / 2;          // mitte und Kreis in Dokument-Koordinaten (px)
    std::ostringstream ds;
    ds.imbue(std::locale::classic());
    ds << "M " << -r << ",0 A " << r << "," << r << " 0 1 0 " << r << ",0 A " << r << "," << r << " 0 1 0 " << -r
       << ",0 Z";
    auto pfad = d->getReprDoc()->createElement("svg:path");
    pfad->setAttribute("d", ds.str());
    pfad->setAttribute("style", "fill:#ffd21a;fill-opacity:0.6;stroke:#6b5800;stroke-width:0.8");
    pfad->setAttribute("hirch:steg", bid.c_str());
    pfad->setAttribute("inkscape:label", TR("Haltesteg"));
    ebene->appendChild(pfad);
    Inkscape::GC::release(pfad);
    auto item = cast<SPItem>(d->getObjectByRepr(pfad));
    if (!item) return nullptr;
    Geom::Affine eltern = Geom::identity();
    if (auto e = cast<SPItem>(item->parent)) eltern = e->i2doc_affine();
    item->set_item_transform(Geom::Translate(mitte) * eltern.inverse());
    item->doWriteTransform(item->transform, nullptr, false);
    d->ensureUpToDate();
    auto m = steg_mitte(item);
    std::ostringstream ls;
    ls.imbue(std::locale::classic());
    ls << m.x() << "," << m.y();
    pfad->setAttribute("hirch:steg-lage", ls.str());
    return item;
}

void HirchFraesen::Impl::stege_entfernen_von(std::string const &bid)
{
    for (auto it : steg_marken(bid)) it->deleteObject(true);
}

void HirchFraesen::Impl::stege_breite_setzen(std::string const &bid, double breite_mm)
{
    // Kreis um seine Mitte neu zeichnen: die Lage (und ob von Hand verschoben) bleibt
    auto d = doc();
    if (!d) return;
    for (auto it : steg_marken(bid)) {
        auto m = steg_mitte(it);
        auto lage = it->getRepr()->attribute("hirch:steg-lage");
        std::string lage_alt = lage ? lage : "";
        it->deleteObject(true);
        auto neu = steg_marke(m, breite_mm, bid);
        if (neu) {
            if (lage_alt.empty()) neu->getRepr()->removeAttribute("hirch:steg-lage");
            else neu->getRepr()->setAttribute("hirch:steg-lage", lage_alt);
        }
    }
}

bool HirchFraesen::Impl::stege_rueckfrage(Gtk::Window *eltern, int verschoben)
{
    if (verschoben <= 0) return true;
    Gtk::MessageDialog m(verschoben == 1 ? TR("1 Haltesteg wurde von Hand verschoben. Alle neu verteilen?")
                                         : TRF("{} Haltestege wurden von Hand verschoben. Alle neu verteilen?",
                                               {std::to_string(verschoben)}),
                         false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
    m.set_title("Hirch CAM");
    if (eltern) m.set_transient_for(*eltern);
    else if (auto f = fenster()) m.set_transient_for(*f);
    m.add_button(TR("Behalten"), Gtk::RESPONSE_CANCEL);
    m.add_button(TR("Neu verteilen"), Gtk::RESPONSE_OK);
    m.set_default_response(Gtk::RESPONSE_CANCEL);
    return m.run() == Gtk::RESPONSE_OK;
}

void HirchFraesen::Impl::stege_verteilen(bj::object const &b, std::function<void(int, std::string const &)> fertig)
{
    auto bid = str(b, "id");
    auto svg = Glib::build_filename(ordner, "zeichnung.svg");
    auto datei = Glib::build_filename(ordner, "stege_einst.json");
    bj::object e = einst;
    if (auto bea = e.if_contains("bearbeitungen"); bea && bea->is_array())
        for (auto &v : bea->as_array())
            if (v.is_object() && str(v.as_object(), "id") == bid) v = b;
    try {
        if (auto d = doc()) {
            auto xml = sp_repr_save_buf(d->getReprDoc());
            Glib::file_set_contents(svg, (hat_text(xml) ? text_als_pfade(d) : xml).raw());
        }
        Glib::file_set_contents(datei, bj::serialize(e));
    } catch (Glib::Error const &ex) {
        fertig(-1, ex.what().raw());
        return;
    }
    int n = std::max(1, (int)num(b, "stege_anzahl", 4));
    double br = num(b, "stege_breite", 6);
    python({"stege", svg, "--einst", datei, "--bearbeitung", bid, "--ordner", Glib::build_filename(ordner, "stege"),
            "--platte", std::to_string(std::max(1, platte)), "--anzahl", std::to_string(n), "--breite", zahl_text(br)},
           [this, bid, br, fertig](bool ok, std::string const &o, std::string const &err) {
               bj::object r;
               if (!ok || !json_lesen(o, r)) {
                   fertig(-1, (err.empty() ? o : err).substr(0, 300));
                   return;
               }
               std::string hinweis;
               if (auto w = liste_von(r, "warnungen"))
                   for (auto const &v : *w)
                       if (v.is_string()) hinweis += (hinweis.empty() ? "" : " ") + std::string(v.as_string());
               if (!flag(r, "ok", false)) {
                   fertig(-1, hinweis);
                   return;
               }
               auto d = doc();
               if (!d) return;
               // nichts zu setzen (z. B. Kontur nicht ganz durch) und nichts da: Zeichnung nicht anfassen
               if (auto l = liste_von(r, "stege"); (!l || l->empty()) && steg_marken(bid).empty()) {
                   fertig(0, hinweis);
                   return;
               }
               stege_entfernen_von(bid);
               int k = 0;
               if (auto l = liste_von(r, "stege")) {
                   for (auto const &v : *l) {
                       auto m = v.if_object();
                       auto p = m ? m->if_contains("svg") : nullptr;
                       if (!p || !p->is_array() || p->as_array().size() < 2) continue;
                       auto const &a = p->as_array();
                       // Benutzereinheiten des Dokuments → Dokument-Koordinaten (mit der viewBox)
                       Geom::Point pt = Geom::Point(a[0].to_number<double>(), a[1].to_number<double>()) *
                                        d->getRoot()->c2p;
                       if (steg_marke(pt, br, bid)) ++k;
                   }
               }
               // Marken da: die Bearbeitung rechnet mit ihnen (sonst bliebe sie «automatisch»)
               if (auto bea = liste_von(einst, "bearbeitungen")) {
                   for (auto &v : *bea) {
                       if (auto *ob = v.if_object(); ob && str(*ob, "id") == bid) {
                           (*ob)["stege_modus"] = k > 0 ? "manuell" : str(*ob, "stege_modus", "auto");
                       }
                   }
                   einst["version"] = 1;
                   d->getReprRoot()->setAttribute("hirch:einstellungen", bj::serialize(einst));
               }
               DocumentUndo::done(d, TR("Haltestege verteilen"), "");
               fertig(k, hinweis);
           }, false);
}

void HirchFraesen::Impl::stege_auto_uebernehmen()
{
    // Peter 23./24.10.: Haltestege sind immer gelbe Marken. Konturen, deren Dialog nie offen war, rechnen noch
    // «auto» – deren Stege aus dem Ergebnis als Marken setzen. Der G-Code bleibt gleich, ausser beim Schlichten
    // (eigene Steglage) und wenn alte Marken «für jede Kontur» (hirch:steg="1") da sind – dann nicht.
    auto d = doc();
    auto bea = liste_von(einst, "bearbeitungen");
    auto sts = unter(ergebnis, "bearbeitungen");
    if (!d || !bea || !sts) return;
    if (!steg_marken("1").empty()) return;
    // Windows 01.11.: kein Rückgängig-Schritt und kein «geändert» (Stern im Titel, Speicherfrage beim Schliessen) –
    // der G-Code bleibt gleich, und ungespeichert entstehen die Marken beim nächsten Öffnen wieder
    DocumentUndo::ScopedInsensitive ohne_undo(d);
    bool geaendert = false;
    for (auto &v : *bea) {
        auto *b = v.if_object();
        if (!b || str(*b, "art") != "kontur" || str(*b, "stege_modus", "auto") != "auto") continue;
        if (flag(*b, "schlichten", false)) continue;
        auto bid = str(*b, "id");
        auto st = unter(*sts, bid.c_str());
        auto l = st ? liste_von(*st, "stege_auto") : nullptr;
        if (!l || l->empty() || !steg_marken(bid).empty()) continue;
        double br = num(*b, "stege_breite", 6);
        int k = 0;
        for (auto const &m : *l) {
            auto o = m.if_object();
            auto p = o ? o->if_contains("svg") : nullptr;
            if (!p || !p->is_array() || p->as_array().size() < 2) continue;
            auto const &a = p->as_array();
            Geom::Point pt = Geom::Point(a[0].to_number<double>(), a[1].to_number<double>()) * d->getRoot()->c2p;
            if (steg_marke(pt, br, bid)) ++k;
        }
        if (k > 0) {
            (*b)["stege_modus"] = "manuell";
            geaendert = true;
        }
    }
    if (!geaendert) return;
    einst["version"] = 1;
    d->getReprRoot()->setAttribute("hirch:einstellungen", bj::serialize(einst));
    marken_uebernommen = true;
}

void HirchFraesen::Impl::stege_leiste()
{
    // Peter 27.10.: ein Werkzeug für alle Haltestege. Gewählt ist ein zweiseitiges 3D-Modell: dessen Stege im Reiter
    // «3D»; sonst die Kontur der Auswahl (gleiche Linienfarbe) oder die erste Kontur; sonst das erste zweiseitige
    // Modell. Die Höhe bestimmt Hirch (Kontur: unten am Boden, Modell: an der Trennlinie).
    auto d = doc();
    if (!d) return;
    SPItem *wahl = nullptr;
    std::string farbe;
    if (auto sel = panel.getSelection(); sel && !sel->isEmpty()) {
        wahl = sel->items().front();
        auto css = sp_repr_css_attr(wahl->getRepr(), "style");
        farbe = Glib::ustring(sp_repr_css_property(css, "stroke", "")).lowercase().raw();
        sp_repr_css_attr_unref(css);
    }
    auto modell_oeffnen = [this](SPItem *m) {
        auto desk = panel.getDesktop();
        auto c = desk ? desk->getContainer() : nullptr;
        if (!c) return;
        desk->getSelection()->set(m);
        c->new_dialog("Hirch3D");
        if (auto d3 = dynamic_cast<Hirch3D *>(c->get_dialog("Hirch3D"))) d3->stegeZeigen();
    };
    if (auto m = wahl ? relief_fuer_stege(d, wahl) : nullptr) {
        modell_oeffnen(m);
        return;
    }
    int erste = -1, passend = -1;
    if (auto bea = liste_von(einst, "bearbeitungen")) {
        for (int i = 0; i < (int)bea->size(); ++i) {
            auto o = (*bea)[i].if_object();
            if (!o || str(*o, "art") != "kontur") continue;
            if (erste < 0) erste = i;
            if (!farbe.empty() && Glib::ustring(str(*o, "farbe")).lowercase().raw() == farbe && passend < 0) passend = i;
        }
    }
    if (int i = passend >= 0 ? passend : erste; i >= 0) {
        bearbeitung_dialog(i, "stege");
        return;
    }
    if (auto m = relief_fuer_stege(d, nullptr)) {
        modell_oeffnen(m);
        return;
    }
    meldung(TR("Noch keine Haltestege möglich"),
            TR("Haltestege halten ein Teil fest, das ausgeschnitten wird: an einer Kontur, die durch das Material "
               "geht, oder an einem 3D-Modell, das von beiden Seiten gefräst wird. Zuerst den Umriss als Kontur "
               "fräsen – danach hier die Stege setzen."),
            Gtk::MESSAGE_INFO);
}


bool HirchFraesen::Impl::ordner_waehlen(std::string const &titel, std::string const &text, std::string const &pref)
{
    // Meldung mit «Ordner wählen …»: der gewählte Ordner wird gemerkt und künftig mit durchsucht
    Gtk::MessageDialog m(titel, false, Gtk::MESSAGE_INFO, Gtk::BUTTONS_NONE, true);
    m.set_title("Hirch CAM");
    if (auto f = fenster()) m.set_transient_for(*f);
    m.set_secondary_text(text);
    m.add_button(TR("Ordner wählen …"), 1);
    klasse(*m.add_button(TR("Schliessen"), Gtk::RESPONSE_CLOSE), "suggested-action");
    m.set_default_response(Gtk::RESPONSE_CLOSE);
    if (m.run() != 1) return false;
    m.hide();
    HirchDateiwahl dateiwahl(fenster(), TR("Ordner mit dem Hirch-CAM-Paket wählen"), Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER, TR("Abbrechen"),
                        TR("Wählen"));
    auto &fc = dateiwahl.fc();
    auto alt = Inkscape::Preferences::get()->getString(pref);
    if (!alt.empty()) fc.set_current_folder(alt);
    if (!dateiwahl.run()) return false;
    Inkscape::Preferences::get()->setString(pref, fc.get_filename());
    return true;
}

void HirchFraesen::Impl::aktualisieren()
{
    std::string prefix = hirch_prefix();
    std::string installiert;
    try {
        installiert = Glib::file_get_contents(Glib::build_filename(prefix, "VERSION"));
    } catch (...) {
    }
    while (!installiert.empty() && std::isspace((unsigned char)installiert.back())) installiert.pop_back();
    std::string bestes, version;
    gint64 zeit = 0;
    auto home = Glib::get_home_dir();
    char const *dl = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    char const *desk = g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP);
    // Downloads, Desktop und der zuletzt selbst gewählte Ordner (keine festen privaten Ordner mehr, 09.11.:
    // öffentliches Repository; wer anderswo ablegt, wählt den Ordner einmal, er bleibt gespeichert)
    std::string const pref_ordner = "/hirch/aktualisieren/ordner";
    std::vector<std::string> orte = {Glib::build_filename(home, "Downloads"), dl ? std::string(dl) : std::string(),
                                     desk ? std::string(desk) : std::string(),
                                     Inkscape::Preferences::get()->getString(pref_ordner).raw()};
    for (auto const &ordner_ : orte) {
        if (ordner_.empty()) continue;
        GDir *dir = g_dir_open(ordner_.c_str(), 0, nullptr);
        if (!dir) continue;
        while (char const *n = g_dir_read_name(dir)) {
            std::string name = n;
#ifdef _WIN32
            std::string const hinten = "-windows-setup.exe";
#else
            std::string const hinten = "-linux-x86_64.tar.xz";
#endif
            // seit 03.11.2026 heisst das Paket «hirch-cam-…»; ältere «hirch-cnc-…» gelten weiter
            std::string const vorne = name.rfind("hirch-cam-", 0) == 0 ? "hirch-cam-" : "hirch-cnc-";
            if (name.size() <= vorne.size() + hinten.size() || name.rfind(vorne, 0) != 0 ||
                name.compare(name.size() - hinten.size(), hinten.size(), hinten) != 0) continue;
            // nur das Fräsprogramm (Version beginnt mit dem Datum) – nicht Laser- oder Rechenkern-Archive
            if (!std::isdigit((unsigned char)name[vorne.size()])) continue;
            auto pfad = Glib::build_filename(ordner_, name);
            GStatBuf st;
            if (g_stat(pfad.c_str(), &st) != 0) continue;
            if ((gint64)st.st_mtime > zeit) {
                zeit = st.st_mtime;
                bestes = pfad;
                version = name.substr(vorne.size(), name.size() - vorne.size() - hinten.size());
            }
        }
        g_dir_close(dir);
    }
#ifndef _WIN32
    // Rechenkern-Update (hirch-cam-kern-VERSION.tar.xz): ohne Passwort ins Profil
    {
        std::string kern, kern_version;
        gint64 kern_zeit = 0;
        for (auto const &ordner_ : orte) {
            if (ordner_.empty()) continue;
            GDir *dir = g_dir_open(ordner_.c_str(), 0, nullptr);
            if (!dir) continue;
            while (char const *n = g_dir_read_name(dir)) {
                std::string name = n;
                // nur «hirch-cam-kern-…»: ältere Rechenkerne (Paket hirch_cnc) passen nicht mehr (Umzug 04.11.)
                std::string const vorne = "hirch-cam-kern-", hinten = ".tar.xz";
                if (name.size() <= vorne.size() + hinten.size() || name.rfind(vorne, 0) != 0 ||
                    name.compare(name.size() - hinten.size(), hinten.size(), hinten) != 0) continue;
                auto pfad = Glib::build_filename(ordner_, name);
                GStatBuf st;
                if (g_stat(pfad.c_str(), &st) != 0) continue;
                if ((gint64)st.st_mtime > kern_zeit) {
                    kern_zeit = st.st_mtime;
                    kern = pfad;
                    kern_version = name.substr(vorne.size(), name.size() - vorne.size() - hinten.size());
                }
            }
            g_dir_close(dir);
        }
        std::string kern_installiert;
        try {
            kern_installiert = Glib::file_get_contents(Glib::build_filename(kern_ordner(), "VERSION"));
        } catch (...) {
        }
        while (!kern_installiert.empty() && std::isspace((unsigned char)kern_installiert.back()))
            kern_installiert.pop_back();
        bool neuer_als_programm = installiert.size() < 10 || kern_version.size() < 10 ||
                                  kern_version.substr(0, 10) >= installiert.substr(0, 10);
        if (!kern.empty() && kern_zeit >= zeit && kern_version != kern_installiert &&
            kern_version != installiert && neuer_als_programm) {
            Gtk::MessageDialog m(TR("Rechenkern aktualisieren?"), false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE,
                                 true);
            m.set_title("Hirch CAM");
            if (auto f = fenster()) m.set_transient_for(*f);
            m.set_secondary_text(TR("Gefunden: ") + Glib::path_get_basename(kern) + TR("\nProgramm: ") + installiert +
                                 TR("\n\nNur die Berechnung (Fräsbahnen, G-Code) wird erneuert – ohne Passwort und "
                                 "ohne Neustart. Ein späteres Programm-Update geht automatisch wieder vor."));
            m.add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
            m.add_button(TR("Aktualisieren"), Gtk::RESPONSE_OK);
            m.set_default_response(Gtk::RESPONSE_OK);
            if (m.run() != Gtk::RESPONSE_OK) return;
            auto basis = Glib::build_filename(g_get_user_config_dir(), "hirch-cam");
            g_mkdir_with_parents(basis.c_str(), 0755);
            char *tmp = g_dir_make_tmp("hirch-kern-XXXXXX", nullptr);
            std::string neu_ordner = tmp ? tmp : "";
            g_free(tmp);
            gint status = 1;
            GError *fehler = nullptr;
            char const *argv_t[] = {"tar", "-xJf", kern.c_str(), "-C", neu_ordner.c_str(), nullptr};
            bool ok = !neu_ordner.empty() &&
                      g_spawn_sync(nullptr, const_cast<char **>(argv_t), nullptr, G_SPAWN_SEARCH_PATH, nullptr,
                                   nullptr, nullptr, nullptr, &status, &fehler) &&
                      status == 0;
            auto entpackt = Glib::build_filename(neu_ordner, "hirch-cam-kern");
            ok = ok && Glib::file_test(Glib::build_filename(entpackt, "hirch_cam", "__init__.py"),
                                       Glib::FILE_TEST_EXISTS);
            if (ok) {
                auto ziel = kern_ordner();
                auto alt = ziel + ".alt";
                char const *rm_alt[] = {"rm", "-rf", alt.c_str(), nullptr};
                g_spawn_sync(nullptr, const_cast<char **>(rm_alt), nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr,
                             nullptr, nullptr, nullptr, nullptr);
                if (Glib::file_test(ziel, Glib::FILE_TEST_EXISTS)) g_rename(ziel.c_str(), alt.c_str());
                ok = g_rename(entpackt.c_str(), ziel.c_str()) == 0;
                g_spawn_sync(nullptr, const_cast<char **>(rm_alt), nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr,
                             nullptr, nullptr, nullptr, nullptr);
            }
            if (!neu_ordner.empty()) {
                char const *rm_tmp[] = {"rm", "-rf", neu_ordner.c_str(), nullptr};
                g_spawn_sync(nullptr, const_cast<char **>(rm_tmp), nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr,
                             nullptr, nullptr, nullptr, nullptr);
            }
            if (fehler) g_error_free(fehler);
            if (!ok) {
                meldung(TR("Aktualisieren fehlgeschlagen"), TR("Das Archiv ") + Glib::path_get_basename(kern) +
                        TR(" liess sich nicht auspacken."), Gtk::MESSAGE_ERROR);
                return;
            }
            letzte_xml.clear();
            spaeter(10);
            meldung(TR("Rechenkern aktualisiert"),
                    kern_update_gilt() ? TR("Neuer Rechenkern: ") + kern_version + TR(" – gilt ab sofort.")
                                       : TR("Installiert, aber das Programm ist neuer – es bleibt beim Rechenkern "
                                         "des Programms."),
                    Gtk::MESSAGE_INFO);
            return;
        }
    }
#endif
    if (bestes.empty()) {
#ifdef _WIN32
        std::string muster_datei = "hirch-cam-…-windows-setup.exe";
#else
        std::string muster_datei = "hirch-cam-…-linux-x86_64.tar.xz";
#endif
        if (ordner_waehlen(TR("Kein neues Programm gefunden"),
                           TR("In «Downloads», auf dem Desktop und im zuletzt gewählten Ordner liegt keine Datei ") + muster_datei + ".\n\n" + TR("Installiert ist Version ") + installiert +
                               ".", pref_ordner))
            aktualisieren();
        return;
    }
    // älteres oder gleiches Archiv: nicht zurückstufen (Datum-Commit bei Peter, 1.0.0-beta.N öffentlich)
    if (int v = hirch_version_vergleich(version, installiert); v == 0 || v == -1) {
        if (ordner_waehlen(TR("Hirch CAM ist aktuell"), TR("Installiert ist Version ") + installiert +
                           TR(" – das ist auch die neueste gefundene Datei."), pref_ordner))
            aktualisieren();
        return;
    }
    {
        Gtk::MessageDialog m(TR("Hirch CAM aktualisieren?"), false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
        m.set_title("Hirch CAM");
        if (auto f = fenster()) m.set_transient_for(*f);
        auto datum = Glib::DateTime::create_now_local(zeit).format("%d.%m.%Y %H:%M");
#ifdef _WIN32
        std::string hinweis_ = TR("Hirch CAM wird dazu beendet (offene Zeichnungen vorher speichern), "
                                  "das Installationsprogramm startet es danach wieder.");
#else
        std::string hinweis_ = TR("Zum Installieren wird das Passwort verlangt. Danach Hirch CAM neu starten – "
                                  "offene Zeichnungen vorher speichern.");
#endif
        m.set_secondary_text(TR("Gefunden: ") + Glib::path_get_basename(bestes) + " (" + datum + ")\n" +
                             TR("Installiert: ") + (installiert.empty() ? "?" : installiert) + "\n\n" + hinweis_);
        m.add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
        m.add_button(TR("Installieren"), Gtk::RESPONSE_OK);
        m.set_default_response(Gtk::RESPONSE_OK);
        if (m.run() != Gtk::RESPONSE_OK) return;
    }
#ifdef _WIN32
    {
        // Installationsprogramm starten und dieses Programm beenden (fragt nach ungespeicherten Zeichnungen)
        GError *fehler = nullptr;
        char const *argv[] = {bestes.c_str(), "/SP-", "/SILENT", "/NOCANCEL", nullptr};
        if (!g_spawn_async(nullptr, const_cast<char **>(argv), nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr,
                           &fehler)) {
            meldung(TR("Aktualisieren fehlgeschlagen"), fehler ? fehler->message : TR("Start nicht möglich"),
                    Gtk::MESSAGE_ERROR);
            if (fehler) g_error_free(fehler);
            return;
        }
        if (auto app = Gio::Application::get_default()) app->activate_action("quit");
        return;
    }
#endif
    status.set_text(TR("Hirch CAM wird aktualisiert …"));
    auto skript = Glib::build_filename(prefix, "bin", "hirch-cam-aktualisieren");
    if (!Glib::file_test(skript, Glib::FILE_TEST_IS_EXECUTABLE)) {
        meldung(TR("Aktualisieren nicht möglich"),
                TR("Diese Version kann sich noch nicht selbst aktualisieren. Einmal im Terminal:\n\n"
                "cd ~/Projekte/hirch-cnc && git pull && ./installieren.sh \"") + bestes + "\"",
                Gtk::MESSAGE_INFO);
        status.set_text("");
        return;
    }
    // root sieht manche Ordner nicht (Nextcloud, Netzlaufwerke): das Archiv erst in den Temp-Ordner kopieren
    {
        auto kopie = Glib::build_filename(Glib::get_tmp_dir(), Glib::path_get_basename(bestes));
        try {
            Gio::File::create_for_path(bestes)->copy(Gio::File::create_for_path(kopie), Gio::FILE_COPY_OVERWRITE);
            bestes = kopie;
        } catch (Glib::Error const &e) {
            g_warning("Archiv nicht nach %s kopiert: %s", kopie.c_str(), e.what().c_str());
        }
    }
    GError *fehler = nullptr;
    char const *argv[] = {"pkexec", skript.c_str(), bestes.c_str(), nullptr};
    auto proc = g_subprocess_newv(argv, GSubprocessFlags(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE),
                                  &fehler);
    if (!proc) {
        meldung(TR("Aktualisieren fehlgeschlagen"), fehler ? fehler->message : TR("pkexec nicht gefunden"), Gtk::MESSAGE_ERROR);
        if (fehler) g_error_free(fehler);
        status.set_text("");
        return;
    }
    struct Daten { Impl *self; std::string prefix; };
    g_subprocess_communicate_utf8_async(proc, nullptr, nullptr, [](GObject *quelle, GAsyncResult *res, gpointer p) {
        std::unique_ptr<Daten> d(static_cast<Daten *>(p));
        auto proc = G_SUBPROCESS(quelle);
        char *aus = nullptr, *err = nullptr;
        GError *e = nullptr;
        g_subprocess_communicate_utf8_finish(proc, res, &aus, &err, &e);
        bool ok = !e && g_subprocess_get_if_exited(proc) && g_subprocess_get_exit_status(proc) == 0;
        std::string text = ok ? std::string(aus ? aus : "") : std::string(err && *err ? err : (e ? e->message : "abgebrochen"));
        g_free(aus);
        g_free(err);
        if (e) g_error_free(e);
        g_object_unref(proc);
        d->self->status.set_text("");
        if (!ok) {
            d->self->meldung(TR("Aktualisieren nicht abgeschlossen"), text, Gtk::MESSAGE_WARNING);
            return;
        }
        Gtk::MessageDialog m(TR("Hirch CAM ist aktualisiert"), false, Gtk::MESSAGE_INFO, Gtk::BUTTONS_NONE, true);
        m.set_title("Hirch CAM");
        if (auto f = d->self->fenster()) m.set_transient_for(*f);
        m.set_secondary_text(TR("Neue Version: ") + text + TR("\nDie Änderungen gelten nach einem Neustart."));
        m.add_button(TR("Später"), Gtk::RESPONSE_CANCEL);
        m.add_button(TR("Jetzt neu starten"), Gtk::RESPONSE_OK);
        m.set_default_response(Gtk::RESPONSE_OK);
        if (m.run() != Gtk::RESPONSE_OK) return;
        // Erst beenden, dann neu starten: Hirch CNC läuft als einzelne Anwendung –
        // ein zweiter Start, solange dieser noch lebt, gibt nur an ihn weiter und
        // endet sofort (dann war nach dem Beenden nichts mehr offen). Ein kleiner
        // Wächter wartet, bis dieser Prozess weg ist (höchstens 10 min, falls beim
        // Beenden «Abbrechen» gewählt wird), und startet dann die neue Version.
        auto start = Glib::build_filename(d->prefix, "bin", "hirch-cam");
        std::string pid = std::to_string((long)getpid());
        std::string skript = "i=0; while kill -0 " + pid + " 2>/dev/null && [ $i -lt 2000 ]; do sleep 0.3; "
                             "i=$((i+1)); done; kill -0 " + pid + " 2>/dev/null || exec \"$0\"";
        char const *argv2[] = {"/bin/sh", "-c", skript.c_str(), start.c_str(), nullptr};
        g_spawn_async(nullptr, const_cast<char **>(argv2), nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, nullptr);
        if (auto app = Gio::Application::get_default()) app->activate_action("quit");
    }, new Daten{this, prefix});
}

/// Projekt in einem Zusatzformat öffnen (Zusatz im Einstellungsordner, hirch_cam/zusatz.py): wird in
/// eine Zeichnung umgewandelt und in einem neuen Fenster geöffnet (Datei › Öffnen und Datei › Importieren)
void HirchFraesen::Impl::zusatz_oeffnen(std::string const &datei)
{
    status.set_text(TR("Projekt wird gelesen …"));
    python({"zusatz", datei}, [this](bool ok, std::string const &o, std::string const &e) {
        status.set_text("");
        bj::object r;
        if (!ok || !json_lesen(o, r) || !flag(r, "ok", false)) {
            std::string grund = json_lesen(o, r) ? str(r, "fehler") : e.substr(0, 400);
            meldung(TR("Projekt nicht geöffnet"), grund, Gtk::MESSAGE_ERROR);
            return;
        }
        auto svg = str(r, "datei");
        std::string info = TRF("Gespeichert als «{}» neben dem Projekt.\n\n", {Glib::path_get_basename(svg)}) +
                           zahl(num(r, "breite", 0), 1) + " × " + zahl(num(r, "hoehe", 0), 1) + " × " +
                           zahl(num(r, "dicke", 0), 1) + " mm · " + std::to_string((long)num(r, "texte", 0)) +
                           TR(" Texte · ") + std::to_string((long)num(r, "kurven", 0)) + TR(" Linien");
        auto liste_text = [&](char const *k) {
            std::string t;
            if (auto l = liste_von(r, k)) {
                for (auto const &v : *l) {
                    if (v.is_string()) t += (t.empty() ? "" : ", ") + std::string(v.get_string());
                }
            }
            return t;
        };
        if (auto t = liste_text("fehlende_schriften"); !t.empty()) {
            info += "\n\n" + TRF("Noch nicht importierte Einlinienschrift: {}. Vorläufig ersetzt – nach «Einstellungen › "
                                 "Schriften › Schrift importieren …» den Text doppelt anklicken und die Schrift wählen.",
                                 {t});
        }
        if (auto t = liste_text("ttf_schriften"); !t.empty()) {
            info += "\n\n" + TRF("Texte in {} sind als gewöhnlicher Text übernommen (änderbar mit dem Textwerkzeug); "
                                 "fehlt die Schrift auf diesem Rechner, zeigt Hirch CAM eine ähnliche.", {t});
        }
        if (auto n = unter(r, "nullpunkt")) {
            auto t = nullpunkt_name(str(*n, "xy", "ul"));
            double dx = num(*n, "dx", 0), dy = num(*n, "dy", 0);
            if (std::abs(dx) > 1e-9 || std::abs(dy) > 1e-9) t += " + " + kurzzahl(dx) + " / " + kurzzahl(dy) + " mm";
            info += TR("\n\nNullpunkt wie im Projekt: XY ") + t + ", Z0 " +
                    (str(*n, "z", "oben") == "unten" ? TR("Unterseite") : TR("Materialoberfläche")) + ".";
        }
        std::string ops;
        if (auto l = liste_von(r, "bearbeitungen")) {
            for (auto const &v : *l) {
                if (auto o = v.if_object()) {
                    auto art = str(*o, "art");
                    static std::map<std::string, std::string> const arten = {
                        {"vcarve", "V-Carve"}, {"gravur", TR("Gravur")}, {"tasche", TR("Tasche")},
                        {"kontur", TR("Kontur")}, {"bohren", TR("Bohren")}};
                    auto it = arten.find(art);
                    ops += "\n• " + str(*o, "name") + " → " + (it != arten.end() ? it->second : art);
                }
            }
        }
        if (!ops.empty()) {
            info += TR("\n\nAus den Fräsbahnen des Projekts übernommen (Tiefen und Werkzeuge bitte prüfen):") + ops +
                    TR("\n\nLinien ohne Fräsbahn sind schwarz und noch keiner Bearbeitung zugeordnet.");
        } else {
            info += TR("\n\nKeine Fräsbahnen gefunden: Einlinientexte sind orange (Gravur), alles andere "
                    "schwarz und noch keiner Bearbeitung zugeordnet.");
        }
        if (auto t = liste_text("hinweise"); !t.empty()) info += "\n\n" + t;
        if (auto app = InkscapeApplication::instance()) app->create_window(Gio::File::create_for_path(svg));
        meldung(TR("Projekt geöffnet"), info, Gtk::MESSAGE_INFO);
    });
}

void HirchFraesen::Impl::seite2_anlegen(SPDocument *d)
{
    auto &pm = d->getPageManager();
    if (!pm.hasPages()) pm.enablePages();
    auto ps = platten_seiten(d);
    if (ps.empty()) return;
    auto p1 = ps[0];
    if (ps.size() < 2) {
        double const px_mm = MM_PX;
        double uu_px = d->getDocumentScale()[Geom::X];
        if (uu_px <= 0) uu_px = px_mm;
        double luecke = 20.0 * px_mm / uu_px;
        auto r = p1->getRect();
        pm.newPage(Geom::Rect::from_xywh(r.right() + luecke, r.top(), r.width(), r.height()));
    }
    ps = platten_seiten(d);
    if (ps.size() > 0) ps[0]->getRepr()->setAttribute("inkscape:label", TR("Seite 1"));
    if (ps.size() > 1) ps[1]->getRepr()->setAttribute("inkscape:label", TR("Seite 2 (nach dem Wenden)"));
    d->ensureUpToDate();
}

void HirchFraesen::Impl::vorrichtung_anlegen(SPDocument *d, double b_mm, double h_mm, double rand_mm)
{
    auto &pm = d->getPageManager();
    if (!pm.hasPages()) pm.enablePages();
    auto ps = platten_seiten(d);
    if (ps.empty()) return;
    double const px_mm = MM_PX;
    double uu_px = d->getDocumentScale()[Geom::X];
    if (uu_px <= 0) uu_px = px_mm;
    double k = px_mm / uu_px;                 // Benutzereinheiten je mm
    auto r = ps[0]->getRect();
    double vb = (b_mm + 2 * rand_mm) * k, vh = (h_mm + 2 * rand_mm) * k, luecke = 20.0 * k;
    // schon da: nur die Grösse nachführen (rechte untere Ecke bleibt, die Zeichnung darauf bleibt liegen)
    if (auto vs = vorrichtung_seite(d)) {
        auto alt = vs->getRect();
        if (std::abs(alt.width() - vb) > 1e-6 || std::abs(alt.height() - vh) > 1e-6) {
            vs->setRect(Geom::Rect::from_xywh(alt.right() - vb, alt.bottom() - vh, vb, vh));
            d->ensureUpToDate();
        }
        return;
    }
    // untere Kante wie Seite 1 (die Werkstücke liegen unten links)
    if (auto pg = pm.newPage(Geom::Rect::from_xywh(-(vb + luecke), r.bottom() - vh, vb, vh))) {
        pg->getRepr()->setAttribute("hirch:vorrichtung", "1");
        pg->getRepr()->setAttribute("inkscape:label", TR("Vorrichtung"));
    }
    d->ensureUpToDate();
}

// ---------------------------------------------------------------------------------------------
// Passlöcher als Kreise auf Seite 1 (Peter, 21.10.): ziehen wie jedes Objekt, der Partner folgt
// gespiegelt an der Wendeachse. Gelesen werden sie nie (svg_lesen: hirch:passloch); die Lage
// steht in den Einstellungen (zweiseitig.pass_lage / pass_punkte).

std::vector<Geom::Point> HirchFraesen::Impl::pass_primaer(double B, double H)
{
    bj::object leer;
    auto z = unter(einst, "zweiseitig");
    auto const &zw = z ? *z : leer;
    bool um_y = str(zw, "achse", "x") == "y";
    double rand = num(zw, "pass_rand", 15);
    auto lage = str(zw, "pass_lage", "achse");
    std::vector<Geom::Point> p;
    if (lage == "frei") {
        if (auto l = liste_von(zw, "pass_punkte")) {
            for (auto const &v : *l) {
                auto a = v.if_array();
                if (a && a->size() >= 2 && (*a)[0].is_number() && (*a)[1].is_number())
                    p.emplace_back((*a)[0].to_number<double>(), (*a)[1].to_number<double>());
            }
        }
        if (!p.empty()) return p;
    }
    if (lage == "ecken") {
        if (um_y) return {{rand, rand}, {rand, H - rand}};       // Partner rechts
        return {{rand, rand}, {B - rand, rand}};                  // Partner oben
    }
    if (um_y) return {{B / 2, rand}, {B / 2, H - rand}};
    return {{rand, H / 2}, {B - rand, H / 2}};
}

void HirchFraesen::Impl::passloecher_abgleichen(bool nach_analyse)
{
    auto d = doc();
    if (!d || !hat_analyse || platte == 0 || pass_sperre) return;
    double B = 0, H = 0;
    if (auto m = unter(analyse, "material")) {
        B = num(*m, "breite", 0);
        H = num(*m, "hoehe", 0);
    }
    if (B <= 0 || H <= 0) return;
    pass_sperre = true;
    // Windows f37b405 Fund 10: Fehlt die Ebene der Passlöcher in der Datei (z. B. muster_alle.hir), zeichnet der Abgleich
    // sie beim Öffnen – die Zeichnung galt dann als geändert (Stern im Titel, Speicherfrage). Die Kreise folgen nur den
    // Einstellungen (wie die Steg-Marken, Windows 01.11.). Bleiben die Einstellungen gleich, bleibt die Zeichnung
    // ungeändert. Gezogene oder gelöschte Kreise ändern die Einstellungen und gelten weiter als Änderung.
    auto einstellungen = [d] {
        auto e = d->getReprRoot()->attribute("hirch:einstellungen");
        return std::string(e ? e : "");
    };
    bool const war_geaendert = d->isModifiedSinceSave();
    auto const vorher = einstellungen();
    auto anhaengen = [&] {
        DocumentUndo::anhaengen(d);
        if (!war_geaendert && einstellungen() == vorher) d->setModifiedSinceSave(false);
    };
    double const px = MM_PX;
    double uu_px = d->getDocumentScale()[Geom::X];      // px je Benutzereinheit (Strichbreite)
    if (uu_px <= 0) uu_px = 1;
    double sx = 0, sy = 0;
    seite_ecke(1, sx, sy);
    auto doc_pt = [&](Geom::Point p) { return Geom::Point((sx + p.x()) * px, (sy + H - p.y()) * px); };
    auto mm_pt = [&](Geom::Point q) { return Geom::Point(q.x() / px - sx, sy + H - q.y() / px); };
    // Massgebend ist die Zeichnung (nach Rückgängig steht dort der alte Stand, im Panel noch der neue)
    if (auto t = d->getReprRoot()->attribute("hirch:einstellungen")) {
        bj::object de;
        if (json_lesen(t, de)) {
            if (auto z = unter(de, "zweiseitig")) einst["zweiseitig"] = *z;
            else einst.erase("zweiseitig");
        }
    }
    auto zp = unter(einst, "zweiseitig");
    bool an = zweiseitig() && zp && flag(*zp, "passloecher", true);
    bool um_y = zp && str(*zp, "achse", "x") == "y";
    double r = (zp ? num(*zp, "pass_d", 6) : 6) / 2;
    auto partner = [&](Geom::Point p) { return um_y ? Geom::Point(B - p.x(), p.y()) : Geom::Point(p.x(), H - p.y()); };
    auto auf_achse = [&](Geom::Point p) { return Geom::distance(p, partner(p)) < 1e-6; };

    auto root = d->getReprRoot();
    Inkscape::XML::Node *ebene = nullptr;
    for (auto c = root->firstChild(); c; c = c->next()) {
        if (auto id = c->attribute("id"); id && std::string(id) == "ebene-passloecher") ebene = c;
    }
    bool geaendert = false;
    if (!an) {
        pass_linien.clear();
        pass_texte.clear();
        if (ebene) {
            if (auto o = d->getObjectByRepr(ebene)) o->deleteObject(false);
            anhaengen();
        }
        pass_sperre = false;
        return;
    }
    int gezeichnet = ebene && ebene->attribute("hirch:pass_n") ? std::atoi(ebene->attribute("hirch:pass_n")) : 0;

    // Kreise einsammeln: Paar n, Rolle a/b, Lage jetzt und beim letzten Zeichnen (Dokument-px)
    struct Kreis { SPItem *it; int n; bool b; Geom::Point ist; bool hat_alt; Geom::Point alt; };
    std::vector<Kreis> kreise;
    if (auto eo = ebene ? d->getObjectByRepr(ebene) : nullptr) {
        std::function<void(SPObject *)> suchen = [&](SPObject *x) {
            auto rep = x->getRepr();
            if (auto pl = rep->attribute("hirch:passloch")) {
                auto it = cast<SPItem>(x);
                auto bb = it ? it->documentGeometricBounds() : Geom::OptRect();
                if (!bb) return;
                Kreis k{it, std::atoi(pl) - 1, false, bb->midpoint(), false, {}};
                auto rolle = rep->attribute("hirch:pass_rolle");
                k.b = rolle && std::string(rolle) == "b";
                // zuletzt gesetzte Lage: hirch:pass_mm (Dokument-mm); ältere Dateien: hirch:pass_xy (Dokument-px)
                auto a_mm = rep->attribute("hirch:pass_mm");
                if (auto a = a_mm ? a_mm : rep->attribute("hirch:pass_xy")) {
                    double ax = 0, ay = 0;
                    std::istringstream is(a);
                    is.imbue(std::locale::classic());
                    char komma = 0;
                    if (is >> ax >> komma >> ay) {
                        k.hat_alt = true;
                        k.alt = a_mm ? Geom::Point(ax, ay) * MM_PX : Geom::Point(ax, ay);
                    }
                }
                kreise.push_back(k);
                return;
            }
            for (auto &c : x->children) suchen(&c);
        };
        suchen(eo);
    }

    // Was hat der Benutzer getan? Verschoben, gelöscht, kopiert
    auto prim = pass_primaer(B, H);
    if (g_getenv("HIRCH_PASS_LOG")) {
        g_message("pass: %zu Kreise, %zu Soll, gezeichnet %d", kreise.size(), prim.size(), gezeichnet);
        for (auto const &k : kreise)
            g_message("  n=%d b=%d ist=%.2f,%.2f alt=%d %.2f,%.2f", k.n, (int)k.b, k.ist.x(), k.ist.y(), (int)k.hat_alt,
                      k.alt.x(), k.alt.y());
    }
    std::vector<Geom::Point> neu = prim;
    std::vector<bool> weg(prim.size(), false), hat_a(prim.size(), false), hat_b(prim.size(), false);
    std::vector<Geom::Point> dazu;
    std::vector<std::pair<SPItem *, bool>> doppelt;   // Kopie, war Rolle b
    bool vom_benutzer = false;
    for (auto const &k : kreise) {
        Geom::Point p = mm_pt(k.ist);
        if (k.b) p = partner(p);
        bool bekannt = k.n >= 0 && k.n < (int)prim.size() && !(k.b ? hat_b[k.n] : hat_a[k.n]);
        if (!bekannt) {                       // Kopie (Strg+D) oder fremd: ein neues Lochpaar
            dazu.push_back(p);
            doppelt.emplace_back(k.it, k.b);
            vom_benutzer = true;
            continue;
        }
        (k.b ? hat_b : hat_a)[k.n] = true;
        if (k.hat_alt && Geom::distance(k.ist, k.alt) > 0.02 * px) {
            neu[k.n] = p;
            vom_benutzer = true;
        }
    }
    for (int n = 0; n < (int)prim.size() && n < gezeichnet; ++n) {
        if (!hat_a[n] || (!auf_achse(prim[n]) && !hat_b[n])) {   // ein Loch gelöscht: das Paar ist weg
            weg[n] = true;
            vom_benutzer = true;
        }
    }
    if (vom_benutzer) {
        std::vector<Geom::Point> liste;
        auto einrasten = [&](Geom::Point p) {
            // auf der Wendeachse (±1 mm) einrasten: dann ist das Loch sein eigener Partner
            if (um_y && std::abs(p.x() - B / 2) <= 1.0) p[Geom::X] = B / 2;
            if (!um_y && std::abs(p.y() - H / 2) <= 1.0) p[Geom::Y] = H / 2;
            return Geom::Point(std::round(p.x() * 100) / 100, std::round(p.y() * 100) / 100);
        };
        for (std::size_t n = 0; n < neu.size(); ++n) if (!weg[n]) liste.push_back(einrasten(neu[n]));
        for (auto const &p : dazu) liste.push_back(einrasten(p));
        bj::object zw = *zp;
        bj::array pp;
        for (auto const &p : liste) {
            bj::array a;
            a.push_back(p.x());
            a.push_back(p.y());
            pp.push_back(a);
        }
        zw["pass_lage"] = "frei";
        zw["pass_punkte"] = pp;
        if (liste.empty()) zw["passloecher"] = false;      // alle gelöscht: keine Passlöcher
        einst["zweiseitig"] = zw;
        einst["version"] = 1;
        root->setAttribute("hirch:einstellungen", bj::serialize(einst));
        geaendert = true;
        prim = pass_primaer(B, H);
        if (liste.empty()) {
            pass_linien.clear();
            pass_texte.clear();
            if (auto o = d->getObjectByRepr(ebene)) o->deleteObject(false);
            anhaengen();
            pass_sperre = false;
            return;
        }
    }

    // Soll: je Paar a (und b, wenn nicht auf der Achse)
    std::set<std::pair<long, long>> warn;
    if (nach_analyse || !vom_benutzer) {
        auto pl = liste_von(analyse, "passloecher");
        if (auto w = liste_von(analyse, "passloecher_warnung"); w && pl) {
            for (auto const &v : *w) {
                if (!v.is_int64()) continue;
                auto i = (std::size_t)v.as_int64();
                if (i >= pl->size()) continue;
                if (auto a = (*pl)[i].if_array(); a && a->size() >= 2)
                    warn.insert({std::lround((*a)[0].to_number<double>() * 10), std::lround((*a)[1].to_number<double>() * 10)});
            }
        }
    }
    auto gewarnt = [&](Geom::Point p) { return warn.count({std::lround(p.x() * 10), std::lround(p.y() * 10)}) > 0; };

    auto xml = d->getReprDoc();
    if (!ebene) {
        ebene = xml->createElement("svg:g");
        ebene->setAttribute("inkscape:groupmode", "layer");
        ebene->setAttribute("inkscape:label", TR("Passlöcher"));
        ebene->setAttribute("id", "ebene-passloecher");
        root->appendChild(ebene);
        Inkscape::GC::release(ebene);
        geaendert = true;
    }
    auto eo = cast<SPItem>(d->getObjectByRepr(ebene));
    Geom::Affine eltern = eo ? eo->i2doc_affine() : Geom::identity();
    std::map<std::pair<int, bool>, SPItem *> da;
    auto ist_kopie = [&](SPItem *it) {
        return std::any_of(doppelt.begin(), doppelt.end(), [it](auto const &q) { return q.first == it; });
    };
    for (auto const &k : kreise) {
        if (ist_kopie(k.it)) continue;
        if (k.n >= (int)prim.size() || (vom_benutzer && k.n < (int)weg.size() && weg[k.n])) {
            k.it->deleteObject(false);
            geaendert = true;
            continue;
        }
        da[{k.n, k.b}] = k.it;
    }
    // nach dem Löschen eines Paars rücken die Nummern nach: neu durchzählen
    if (vom_benutzer && std::find(weg.begin(), weg.end(), true) != weg.end()) {
        std::map<std::pair<int, bool>, SPItem *> um;
        int z = 0;
        for (int n = 0; n < (int)weg.size(); ++n) {
            if (weg[n]) continue;
            for (bool b : {false, true}) {
                auto f = da.find({n, b});
                if (f != da.end()) um[{z, b}] = f->second;
            }
            ++z;
        }
        da = um;
    }
    // Kopien (Strg+D) werden zu neuen Paaren: der Knoten bleibt (und damit die Auswahl)
    if (!doppelt.empty()) {
        int basis = 0;
        for (bool w : weg) basis += w ? 0 : 1;
        for (std::size_t j = 0; j < doppelt.size(); ++j) da[{basis + (int)j, doppelt[j].second}] = doppelt[j].first;
    }
    auto zahl = [](double v) {
        std::ostringstream o;
        o.imbue(std::locale::classic());
        o.precision(10);
        o << v;
        return o.str();
    };
    for (int n = 0; n < (int)prim.size(); ++n) {
        for (bool b : {false, true}) {
            Geom::Point p = b ? partner(prim[n]) : prim[n];
            auto f = da.find({n, b});
            if (b && auf_achse(prim[n])) {
                if (f != da.end()) {
                    f->second->deleteObject(false);
                    geaendert = true;
                }
                continue;
            }
            Geom::Point soll = doc_pt(p);
            // Farbe der Bohrung «Passlöcher» (Violett); zu nah an etwas: orangefarbener Ring (Peter 26.10.)
            std::string stil = std::string("fill:#8000c0;fill-opacity:0.3;stroke:") +
                               (gewarnt(p) ? "#e66100" : "#8000c0") + ";stroke-width:" +
                               zahl((gewarnt(p) ? 1.2 : 0.4) * px / uu_px);
            std::string name = std::string(TR("Passloch")) + " " + std::to_string(n + 1) + (b ? "′" : "");
            SPItem *it = f != da.end() ? f->second : nullptr;
            if (it) {
                auto bb = it->documentGeometricBounds();
                double soll_b = 2 * r * px;
                if (!bb || std::abs(bb->width() - soll_b) > 0.05 * px) {     // anderer Ø: neu anlegen
                    it->deleteObject(false);
                    it = nullptr;
                }
            }
            if (!it) {
                auto k = xml->createElement("svg:circle");
                k->setAttribute("cx", "0");
                k->setAttribute("cy", "0");
                k->setAttribute("r", zahl(r * px));
                ebene->appendChild(k);
                Inkscape::GC::release(k);
                it = cast<SPItem>(d->getObjectByRepr(k));
                if (!it) continue;
                it->set_item_transform(Geom::Translate(soll) * eltern.inverse());
                it->doWriteTransform(it->transform, nullptr, false);
                geaendert = true;
            } else {
                auto bb = it->documentGeometricBounds();
                auto delta = soll - bb->midpoint();
                if (Geom::L2(delta) > 1e-4) {
                    Geom::Affine eltern_it = Geom::identity();
                    if (auto e = cast<SPItem>(it->parent)) eltern_it = e->i2doc_affine();
                    it->set_item_transform(it->i2doc_affine() * Geom::Translate(delta) * eltern_it.inverse());
                    it->doWriteTransform(it->transform, nullptr, false);
                    geaendert = true;
                }
            }
            auto rep = it->getRepr();
            auto setzen = [&](char const *k, std::string const &v) {
                auto a = rep->attribute(k);
                if (!a || v != a) {
                    rep->setAttribute(k, v);
                    geaendert = true;
                }
            };
            setzen("hirch:passloch", std::to_string(n + 1));
            setzen("hirch:pass_rolle", b ? "b" : "a");
            {
                std::ostringstream os;              // in der Datei: mm, Punkt als Dezimalzeichen
                os.imbue(std::locale::classic());
                os.precision(10);
                os << soll.x() / MM_PX << "," << soll.y() / MM_PX;
                setzen("hirch:pass_mm", os.str());
            }
            if (rep->attribute("hirch:pass_xy")) {
                rep->removeAttribute("hirch:pass_xy");
                geaendert = true;
            }
            setzen("style", stil);
            setzen("inkscape:label", name);
        }
    }
    if (auto a = ebene->attribute("hirch:pass_n"); !a || std::to_string(prim.size()) != a) {
        ebene->setAttribute("hirch:pass_n", std::to_string(prim.size()));
        geaendert = true;
    }
    if (geaendert) anhaengen();
    pass_deko_zeichnen(prim, B, H, um_y, r, gewarnt);
    pass_sperre = false;
}

void HirchFraesen::Impl::pass_deko_zeichnen(std::vector<Geom::Point> const &prim, double B, double H, bool um_y,
                                            double r, std::function<bool(Geom::Point)> const &gewarnt)
{
    // Nur Anzeige (keine Objekte der Zeichnung): Nummer an jedem Loch («1» und sein Partner «1′»), eine
    // gestrichelte Linie quer über die Wendeachse zum Partner und auf Seite 2 die Stifte, wo sie nach dem Wenden
    // sitzen. So sieht man, dass Passlöcher immer als Paar wandern.
    pass_linien.clear();
    pass_texte.clear();
    auto desk = panel.getDesktop();
    if (!desk || prim.empty()) return;
    auto dt = desk->doc2dt();
    double const px = MM_PX;
    double s1x = 0, s1y = 0, s2x = 0, s2y = 0;
    seite_ecke(1, s1x, s1y);
    seite_ecke(2, s2x, s2y);
    bool hat2 = zweiseitig() && (std::abs(s2x - s1x) > 1e-6 || std::abs(s2y - s1y) > 1e-6);
    auto pt = [&](double sx, double sy, Geom::Point p) { return Geom::Point((sx + p.x()) * px, (sy + H - p.y()) * px) * dt; };
    auto partner = [&](Geom::Point p) { return um_y ? Geom::Point(B - p.x(), p.y()) : Geom::Point(p.x(), H - p.y()); };
    guint32 const violett = 0x8000c0ff;
    auto text = [&](Geom::Point an, std::string const &t, bool warn) {
        auto it = make_canvasitem<CanvasItemText>(desk->getCanvasSketch(), an, t);
        it->set_fontsize(12);
        it->set_fill(warn ? 0xc64600ff : violett);
        it->set_background(0xffffffcc);
        it->set_border(2);
        it->set_anchor(Geom::Point(0, 1));         // links unten am Punkt
        it->set_pickable(false);
        pass_texte.push_back(std::move(it));
    };
    auto linie = [&](Geom::Point a, Geom::Point b, guint32 farbe, bool gestrichelt) {
        Geom::PathVector pv;
        Geom::Path p(a);
        p.appendNew<Geom::LineSegment>(b);
        pv.push_back(p);
        auto it = make_canvasitem<CanvasItemBpath>(desk->getCanvasSketch(), pv, false);
        it->set_stroke(farbe);
        it->set_stroke_width(1.0);
        if (gestrichelt) it->set_dashes({5.0, 4.0});
        it->set_fill(0x0, SP_WIND_RULE_NONZERO);
        it->set_pickable(false);
        pass_linien.push_back(std::move(it));
    };
    auto stift = [&](Geom::Point m) {
        Geom::PathVector pv;
        pv.push_back(Geom::Path(Geom::Circle(m, r * px * dt.descrim())));
        auto it = make_canvasitem<CanvasItemBpath>(desk->getCanvasSketch(), pv, false);
        it->set_stroke(violett);
        it->set_stroke_width(1.5);
        it->set_dashes({3.0, 3.0});
        it->set_fill(0x8000c022, SP_WIND_RULE_NONZERO);
        it->set_pickable(false);
        pass_linien.push_back(std::move(it));
    };
    double ab = (r + 1.0);                           // Nummer rechts oben neben dem Kreis (mm)
    for (std::size_t n = 0; n < prim.size(); ++n) {
        Geom::Point a = prim[n], b = partner(a);
        bool achse = Geom::distance(a, b) < 1e-6;
        std::string na = std::to_string(n + 1), nb = na + "′";
        auto marke = [&](double sx, double sy, Geom::Point p, std::string const &t) {
            text(pt(sx, sy, p + Geom::Point(ab, ab)), t, gewarnt(p));
        };
        // Seite 1: Nummern und Linie zum Partner (vom Rand des einen Kreises zum Rand des anderen)
        marke(s1x, s1y, a, na);
        if (!achse) {
            marke(s1x, s1y, b, nb);
            Geom::Point dir = (b - a) / std::max(1e-9, Geom::distance(a, b));
            linie(pt(s1x, s1y, a + dir * r), pt(s1x, s1y, b - dir * r), violett, true);
        }
        // Seite 2: nach dem Wenden sitzt Loch n dort, wo auf Seite 1 sein Partner liegt (und umgekehrt)
        if (hat2) {
            stift(pt(s2x, s2y, b));
            marke(s2x, s2y, b, na);
            if (!achse) {
                stift(pt(s2x, s2y, a));
                marke(s2x, s2y, a, nb);
            }
        }
    }
}


// ---- Einstellungen › Werkstoffe (Peter 06.11.2026, Entwurf «modern»): je Werkstoff eine Karte mit einem Schieber
// «langsamer – Herstellerwert – schneller» (Vorschub 50 … 150 %, gilt an dieser Maschine) und dem Ergebnis für einen
// Schaftfräser Ø 6 mit 2 Schneiden samt Rechnung. Aufgeklappt ist der Werkstoff der Zeichnung.
void HirchFraesen::Impl::werkstoffe_abschnitt(Gtk::Box *ziel)
{
    // Windows 08.11. (Fund 4): nach «Kopie …»/«Löschen» sprang die Seite – die Rollposition merken und danach wieder setzen
    Glib::RefPtr<Gtk::Adjustment> rollen;          // hält sich selbst am Leben, auch wenn die Seite zugeht
    double roll_vorher = -1;
    for (Gtk::Widget *w = ziel->get_parent(); w; w = w->get_parent())
        if (auto sw = dynamic_cast<Gtk::ScrolledWindow *>(w)) {
            rollen = sw->get_vadjustment();
            roll_vorher = rollen->get_value();
            break;
        }
    UI::remove_all_children(*ziel);
    auto hinweis = label(TR("Schieber: langsamer oder schneller als die Herstellerwerte – gilt an dieser Maschine. "
                            "Das Ergebnis siehst du sofort."), "hirch-dim");
    klasse(*hinweis, "hirch-klein");
    hinweis->set_line_wrap(true);
    hinweis->set_max_width_chars(44);
    hinweis->set_xalign(0);
    ziel->add(*hinweis);
    auto liste = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
    ziel->add(*liste);
    ziel->show_all();
    Gtk::Box *ziel_p = ziel;
    python({"datenbank", "werkstoffe"}, [this, liste, ziel_p, rollen, roll_vorher](bool ok, std::string const &o,
                                                                                  std::string const &) {
        bj::object r;
        if (!ok || !json_lesen(o, r)) return;
        if (rollen && roll_vorher >= 0)
            Glib::signal_idle().connect_once([rollen, roll_vorher] { rollen->set_value(roll_vorher); },
                                             Glib::PRIORITY_LOW);
        auto ws = liste_von(r, "werkstoffe");
        if (!ws) return;
        std::string aktuell = str(einst, "material", "hartholz");
        struct Karte { Gtk::Label *marke, *ergebnis, *rechnung; Gtk::Scale *schieber; Gtk::Scale *dreh = nullptr; };
        auto karten = std::make_shared<std::map<std::string, Karte>>();
        auto zeigen = [karten](bj::array const &a) {
            for (auto const &v : a) {
                auto const *o = v.if_object();
                if (!o) continue;
                auto it = karten->find(str(*o, "key"));
                if (it == karten->end()) continue;
                int p = (int)num(*o, "vorschub", 100);
                auto &k = it->second;
                k.marke->set_text(p == 100 ? TR("Herstellerwert") : (p > 100 ? "+" : "−") + std::to_string(std::abs(p - 100)) + " %");
                auto sc = k.marke->get_style_context();
                sc->remove_class(p == 100 ? "hirch-marke-warn" : "hirch-marke-ok");
                sc->add_class(p == 100 ? "hirch-marke-ok" : "hirch-marke-warn");
                k.ergebnis->set_markup(markup_escape(TRF("Schaftfräser Ø {}, 2 Schneiden: ", {einheit_mass(6, "6")})) + "<b>" +
                                       ganz(num(*o, "ergebnis_drehzahl", 0)) + " " + TR("U/min") + " · " +
                                       ganz(num(*o, "ergebnis_vorschub", 0)) + " mm/min</b>");
                k.rechnung->set_text(rechnung_uebersetzen(str(*o, "rechnung")));
            }
        };
        for (auto const &v : *ws) {
            auto const *o = v.if_object();
            if (!o) continue;
            std::string key = str(*o, "key");
            auto karte = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);
            klasse(*karte, "hirch-karte2");
            auto kopf_k = Gtk::make_managed<Gtk::Button>();
            kopf_k->set_relief(Gtk::RELIEF_NONE);
            auto kz = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
            bool eigen = flag(*o, "eigen", false);
            auto nl = label(eigen ? str(*o, "name") : std::string(TR(str(*o, "name"))), "hirch-titel");
            nl->set_hexpand(true);
            nl->set_xalign(0);
            kz->add(*nl);
            if (eigen) kz->add(*label(TR("eigener"), "hirch-badge"));
            if (flag(*o, "geaendert", false)) kz->add(*label(TR("Fachwerte geändert"), "hirch-marke-warn"));
            auto marke = label("", "hirch-marke-ok");
            kz->add(*marke);
            kopf_k->add(*kz);
            karte->add(*kopf_k);
            auto inhalt = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);
            auto schieber = Gtk::make_managed<Gtk::Scale>(Gtk::ORIENTATION_HORIZONTAL);
            schieber->set_range(50, 150);
            schieber->set_increments(5, 10);
            schieber->set_round_digits(0);
            schieber->set_draw_value(false);
            schieber->add_mark(100, Gtk::POS_BOTTOM, "");
            schieber->set_value(num(*o, "vorschub", 100));
            inhalt->add(*schieber);
            auto skala = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL);
            auto l1 = label(TR("langsamer"), "hirch-dim"), l2 = label(TR("Herstellerwert"), "hirch-dim"),
                 l3 = label(TR("schneller"), "hirch-dim");
            for (auto l : {l1, l2, l3}) klasse(*l, "hirch-klein");
            l2->set_hexpand(true);
            l2->set_xalign(0.5);
            l1->set_xalign(0);
            l3->set_xalign(1);
            skala->add(*l1);
            skala->add(*l2);
            skala->add(*l3);
            inhalt->add(*skala);
            auto erg = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
            klasse(*erg, "hirch-ergebnis");
            auto el = Gtk::make_managed<Gtk::Label>();
            el->set_xalign(0);
            el->set_line_wrap(true);
            el->set_max_width_chars(44);
            auto rl = label("", "hirch-dim");
            klasse(*rl, "hirch-klein");
            rl->set_xalign(0);
            rl->set_line_wrap(true);
            rl->set_max_width_chars(48);
            erg->add(*el);
            erg->add(*rl);
            inhalt->add(*erg);
            // ---- Details (Konzept 06.11.): Drehzahl, Fachwerte, Herstellerwerte, Kopie, Löschen
            auto det_knopf = Gtk::make_managed<Gtk::Button>(TR("Details ›"));
            det_knopf->set_relief(Gtk::RELIEF_NONE);
            det_knopf->set_halign(Gtk::ALIGN_START);
            klasse(*det_knopf, "hirch-link");
            inhalt->add(*det_knopf);
            auto det = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
            inhalt->add(*det);
            auto dz = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
            dz->add(*label(TR("Drehzahl")));
            auto dreh = Gtk::make_managed<Gtk::Scale>(Gtk::ORIENTATION_HORIZONTAL);
            dreh->set_range(50, 150);
            dreh->set_increments(5, 10);
            dreh->set_round_digits(0);
            dreh->set_digits(0);
            dreh->set_value_pos(Gtk::POS_RIGHT);
            dreh->add_mark(100, Gtk::POS_BOTTOM, "");
            dreh->set_value(num(*o, "drehzahl", 100));
            dreh->set_hexpand(true);
            dz->add(*dreh);
            dz->add(*label("%", "hirch-dim"));
            det->add(*dz);
            auto dh = label(TR("Drehzahl langsamer oder schneller als der Herstellerwert – der Vorschub folgt "
                               "(gleicher Zahnvorschub). Gilt an dieser Maschine."), "hirch-dim");
            klasse(*dh, "hirch-klein");
            dh->set_line_wrap(true);
            dh->set_max_width_chars(44);
            dh->set_xalign(0);
            det->add(*dh);
            det->add(*abschnitt(TR("Fachwerte (für alle Maschinen)")));
            auto fg = formular();
            struct Fach { char const *k, *name, *einheit; double schritt; int stellen; };
            static Fach const FACH[] = {
                {"vc", "Schnittgeschwindigkeit vc", "m/min", 5, 0},
                {"k", "Zahnvorschub je mm Ø (k)", "", 0.0005, 4},   // Verhältnis mm/mm: in Zoll dieselbe Zahl
                {"fz_min", "Zahnvorschub mindestens", "mm", 0.005, 3},
                {"fz_max", "Zahnvorschub höchstens", "mm", 0.005, 3},
                {"zustellung", "Zustellung × Ø", "", 0.05, 2},
                {"bahnabstand", "Bahnabstand", "%", 1, 0},
                {"rampe", "Rampe", "°", 0.5, 1},
                {"vollnut", "Vorschub bei Vollnut (Anteil)", "", 0.01, 2}};
            auto fw = unter(*o, "fachwerte");
            auto spins = std::make_shared<std::map<std::string, Gtk::SpinButton *>>();
            int fz = 0;
            for (auto const &f : FACH) {
                if (!fw || !fw->contains(f.k)) continue;
                auto sp = spin_zeile(*fg, fz++, TR(f.name), num(*fw, f.k, 0), 0, 5000, f.schritt, f.stellen, f.einheit);
                rad_schuetzen(*sp);
                (*spins)[f.k] = sp;
            }
            det->add(*fg);
            auto knoepfe = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
            auto zur = Gtk::make_managed<Gtk::Button>(TR("Herstellerwerte"));
            zur->set_tooltip_text(TR("Fachwerte zurück auf die mitgelieferten Werte"));
            auto kop = Gtk::make_managed<Gtk::Button>(TR("Kopie …"));
            kop->set_tooltip_text(TR("Eigener Werkstoff als Kopie von diesem, z. B. eine Holzart"));
            knoepfe->add(*zur);
            knoepfe->add(*kop);
            Gtk::Button *los = nullptr;
            if (eigen) {
                los = Gtk::make_managed<Gtk::Button>(TR("Löschen"));
                klasse(*los, "hirch-loeschen");
                knoepfe->add(*los);
            }
            det->add(*knoepfe);
            det->show_all();
            det->set_no_show_all(true);
            det->hide();
            det_knopf->signal_clicked().connect([det, det_knopf] {
                bool an = !det->get_visible();
                det->set_visible(an);
                det_knopf->set_label(an ? TR("Details ‹") : TR("Details ›"));
            });
            // Fachwerte, Kopie, Löschen: danach den ganzen Abschnitt neu (Namen, Liste)
            auto neu_aufbauen = [this, ziel_p](bool, std::string const &, std::string const &) {
                letzte_xml.clear();
                spaeter(10);
                Glib::signal_idle().connect_once([this, ziel_p] { werkstoffe_abschnitt(ziel_p); });
            };
            auto fach_zeit = std::make_shared<sigc::connection>();
            for (auto const &[fk, sp] : *spins) {
                sp->signal_value_changed().connect([this, key, spins, fach_zeit, zeigen] {
                    fach_zeit->disconnect();
                    *fach_zeit = Glib::signal_timeout().connect([this, key, spins, zeigen] {
                        bj::object werte;
                        for (auto const &[k2, s2] : *spins) werte[k2] = wert(s2);
                        bj::object a;
                        a["was"] = "werte";
                        a["key"] = key;
                        a["werte"] = werte;
                        python({"datenbank", "werkstoff_details", bj::serialize(a)},
                               [this, zeigen](bool ok2, std::string const &o2, std::string const &) {
                                   bj::object r2;
                                   if (!ok2 || !json_lesen(o2, r2)) return;
                                   if (auto a2 = liste_von(r2, "werkstoffe")) zeigen(*a2);
                                   letzte_xml.clear();
                                   spaeter(10);
                               });
                        return false;
                    }, 600);
                });
            }
            zur->signal_clicked().connect([this, key, neu_aufbauen] {
                bj::object a;
                a["was"] = "zurueck";
                a["key"] = key;
                python({"datenbank", "werkstoff_details", bj::serialize(a)}, neu_aufbauen);
            });
            std::string anzeige_name = eigen ? str(*o, "name") : std::string(TR(str(*o, "name")));
            kop->signal_clicked().connect([this, key, anzeige_name, neu_aufbauen] {
                auto name = maschine_name_fragen(TR("Eigener Werkstoff"), TRF("{} (Kopie)", {anzeige_name}),
                                                 TR("Name des Werkstoffs"));
                if (name.empty()) return;
                bj::object a;
                a["was"] = "neu";
                a["key"] = key;
                a["name"] = name;
                python({"datenbank", "werkstoff_details", bj::serialize(a)}, neu_aufbauen);
            });
            if (los) los->signal_clicked().connect([this, key, anzeige_name, neu_aufbauen] {
                auto dlg = dialog(TR("Werkstoff löschen"));
                auto t = label(TRF("«{}» löschen? Zeichnungen mit diesem Werkstoff rechnen danach mit Hartholz.",
                                   {anzeige_name}));
                t->set_line_wrap(true);
                t->set_max_width_chars(50);
                dlg->get_content_area()->add(*t);
                dlg->add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
                auto ok = dlg->add_button(TR("Löschen"), Gtk::RESPONSE_OK);
                klasse(*ok, "destructive-action");
                dlg->show_all();
                if (dlg->run() != Gtk::RESPONSE_OK) return;
                dlg.reset();
                bj::object a;
                a["was"] = "loeschen";
                a["key"] = key;
                python({"datenbank", "werkstoff_details", bj::serialize(a)}, neu_aufbauen);
            });
            karte->add(*inhalt);
            liste->add(*karte);
            (*karten)[key] = {marke, el, rl, schieber, dreh};
            inhalt->show_all();                      // zuerst zeigen, dann no_show_all (sonst bleibt es leer)
            inhalt->set_no_show_all(true);
            inhalt->set_visible(key == aktuell);
            kopf_k->signal_clicked().connect([inhalt] { inhalt->set_visible(!inhalt->get_visible()); });
            // nach dem Loslassen bzw. kurzer Pause rechnen und speichern
            auto zeit = std::make_shared<sigc::connection>();
            auto speichern_ = [this, key, schieber, dreh, zeit, zeigen] {
                zeit->disconnect();
                *zeit = Glib::signal_timeout().connect([this, key, schieber, dreh, zeigen] {
                    bj::object a;
                    a["key"] = key;
                    a["vorschub"] = (int)std::lround(schieber->get_value());
                    a["drehzahl"] = (int)std::lround(dreh->get_value());
                    python({"datenbank", "werkstoff", bj::serialize(a)},
                           [this, zeigen](bool ok2, std::string const &o2, std::string const &) {
                               bj::object r2;
                               if (!ok2 || !json_lesen(o2, r2)) return;
                               if (auto a2 = liste_von(r2, "werkstoffe")) zeigen(*a2);
                               letzte_xml.clear();          // Schnittdaten anders: neu rechnen
                               spaeter(10);
                           });
                    return false;
                }, 400);
            };
            schieber->signal_value_changed().connect([schieber, marke, speichern_] {
                int p = (int)std::lround(schieber->get_value());
                marke->set_text(p == 100 ? TR("Herstellerwert") : (p > 100 ? "+" : "−") + std::to_string(std::abs(p - 100)) + " %");
                speichern_();
            });
            dreh->signal_value_changed().connect([speichern_] { speichern_(); });
        }
        zeigen(*ws);
        auto plus = Gtk::make_managed<Gtk::Button>(TR("+ Eigener Werkstoff (Kopie von …)"));
        plus->set_halign(Gtk::ALIGN_START);
        plus->set_tooltip_text(TR("Kopie des Werkstoffs dieser Zeichnung – für eine andere Kopie: «Details ›» › «Kopie …»"));
        plus->signal_clicked().connect([this, aktuell, ziel_p] {
            auto name = maschine_name_fragen(TR("Eigener Werkstoff"), "", TR("Name des Werkstoffs"));
            if (name.empty()) return;
            bj::object a;
            a["was"] = "neu";
            a["key"] = aktuell;
            a["name"] = name;
            python({"datenbank", "werkstoff_details", bj::serialize(a)},
                   [this, ziel_p](bool, std::string const &, std::string const &) {
                       Glib::signal_idle().connect_once([this, ziel_p] { werkstoffe_abschnitt(ziel_p); });
                   });
        });
        liste->add(*plus);
        liste->show_all();
        rad_schuetzen(*ziel_p);
    });
}

} // namespace Inkscape::UI::Dialog
