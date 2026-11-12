// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Reiter «Einstellungen»
 */
#include "ui/dialog/hirch-intern.h"

namespace Inkscape::UI::Dialog {

struct HirchAnsicht::Impl {
    HirchAnsicht &panel;
    Gtk::Switch dunkel, bahnen, geister, aufr, autor, leuchten;
    Gtk::SpinButton *fang = nullptr, *schliessen = nullptr;
    Gtk::Box maschine{Gtk::ORIENTATION_VERTICAL, 8};
    Gtk::Box werkstoffe{Gtk::ORIENTATION_VERTICAL, 8};
    Gtk::Box vorgaben{Gtk::ORIENTATION_VERTICAL, 8};
    Gtk::ComboBoxText sprache, einheit;
    Gtk::SearchEntry suche;
    Gtk::Widget *treffer = nullptr;         // zuletzt gefundene Zeile (Enter: die nächste)
    Gtk::Label sprache_hinweis, schrift_info;

    HirchFraesen *fraesen()
    {
        auto desk = panel.getDesktop();
        auto c = desk ? desk->getContainer() : nullptr;
        return c ? dynamic_cast<HirchFraesen *>(c->get_dialog("Fraesen")) : nullptr;
    }
    std::vector<Inkscape::PrefObserver> beobachter;
    bool setzt = false;
    std::map<std::string, Gtk::Widget *> koepfe;      // Überschrift → Label (Menü «Einstellungen ›»)
    Gtk::Button *imp_knopf = nullptr, *taf_knopf = nullptr;
    Gtk::ScrolledWindow *sw = nullptr;
    Gtk::Box *inhalt = nullptr;

    void abschnitt_zeigen(std::string const &k)
    {
        auto it = koepfe.find(k);
        if (it == koepfe.end() || !sw || !inhalt) return;
        auto *w = it->second;
        // nach dem Aufbau des Reiters blättern (vorher hat er noch keine Grösse)
        Glib::signal_timeout().connect_once([this, w] {
            int x = 0, y = 0;
            if (w->translate_coordinates(*inhalt, 0, 0, x, y)) {
                auto adj = sw->get_vadjustment();
                adj->set_value(std::min<double>(std::max(0, y - 8), adj->get_upper() - adj->get_page_size()));
            }
        }, 150);
    }

    /// Suche: zur ersten (Enter: nächsten) Zeile springen, deren Text den Suchbegriff enthält; sie kurz markieren
    void suchen(bool weiter)
    {
        auto q = suche.get_text().lowercase();
        if (q.size() < 2 || !inhalt) return;
        std::vector<Gtk::Widget *> alle;
        std::function<void(Gtk::Widget &)> sammeln = [&](Gtk::Widget &w) {
            if (auto l = dynamic_cast<Gtk::Label *>(&w)) {
                if (l->is_visible() && l->get_text().lowercase().find(q) != Glib::ustring::npos) alle.push_back(l);
            } else if (auto b = dynamic_cast<Gtk::Button *>(&w); b && !b->get_label().empty()) {
                if (b->is_visible() && b->get_label().lowercase().find(q) != Glib::ustring::npos) alle.push_back(b);
            }
            if (auto c = dynamic_cast<Gtk::Container *>(&w))
                for (auto k : c->get_children()) sammeln(*k);
        };
        sammeln(*inhalt);
        if (alle.empty()) return;
        std::size_t i = 0;
        if (weiter && treffer) {
            auto it = std::find(alle.begin(), alle.end(), treffer);
            if (it != alle.end()) i = (std::size_t)(it - alle.begin() + 1) % alle.size();
        }
        if (treffer) klasse(*treffer, "hirch-gefunden", false);
        treffer = alle[i];
        klasse(*treffer, "hirch-gefunden", true);
        int x = 0, y = 0;
        if (treffer->translate_coordinates(*inhalt, 0, 0, x, y)) {
            auto adj = sw->get_vadjustment();
            adj->set_value(std::min<double>(std::max(0, y - 40), adj->get_upper() - adj->get_page_size()));
        }
    }

    explicit Impl(HirchAnsicht &p) : panel(p)
    {
        css_einrichten();
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 12);
        klasse(*box, "hirch-panel");
        box->set_margin_top(10);
        auto kopf = [&](char const *t) {
            auto l = label(TR(t), "hirch-kopf");
            l->set_margin_top(8);
            box->add(*l);
            koepfe[t] = l;
        };
        auto zeile_ = [&](Gtk::Switch &sw, std::string const &titel, std::string const &text) {
            auto z = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 12);
            auto t = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
            t->set_hexpand(true);
            t->add(*label(titel));
            auto l = label(text, "hirch-dim");
            klasse(*l, "hirch-klein");
            l->set_line_wrap(true);
            l->set_max_width_chars(40);
            t->add(*l);
            z->add(*t);
            sw.set_valign(Gtk::ALIGN_CENTER);
            z->add(sw);
            box->add(*z);
        };
        kopf("DARSTELLUNG");
        zeile_(dunkel, TR("Dunkle Darstellung"), TR("Helle oder dunkle Oberfläche – gilt sofort und beim nächsten Start."));
        zeile_(bahnen, TR("Fräsbahnen zeigen"),
               TR("Mittellinie des Fräsers in der Farbe der Bearbeitung, gestrichelt: Schlichten."));
        zeile_(geister, TR("Andere Seite blass zeigen"),
               TR("Zweiseitig und Vorrichtung: die Umrisse der anderen Seite blass über der Zeichnung."));
        zeile_(leuchten, TR("Ausgewählte Pfade leuchten"),
               TR("Jeder Pfad der Auswahl – auch in Gruppen und Texten – bekommt einen blauen Schein."));
        kopf("RECHNEN");
        zeile_(autor, TR("Automatisch rechnen"),
               TR("An: gerechnet wird, wenn du fertig bist – beim Wechsel zum Auswahlwerkzeug oder kurz nach der "
               "letzten Änderung, und nur, wenn sich Gefrästes geändert hat. Aus: nur mit «Rechnen»; Speichern, "
               "USB-Stick und Vorschau 3D rechnen trotzdem selbst."));
        kopf("IMPORT");
        zeile_(aufr, TR("Nach dem Import aufräumen"),
               TR("DXF, AI, PDF: doppelte Linien entfernen, einzelne Stücke zu Pfaden verbinden, fast geschlossene "
               "Pfade schliessen – mit Bericht und «Rückgängig»."));
        auto g = formular();
        auto prefs = Inkscape::Preferences::get();
        fang = spin_zeile(*g, 0, TR("Fangweite (Enden verbinden)"), prefs->getDouble("/hirch/aufraeumen/fang", 0.05),
                          0.001, 2, 0.01, 3, "mm");
        schliessen = spin_zeile(*g, 1, TR("Lücken schliessen bis"), prefs->getDouble("/hirch/aufraeumen/schliessen", 1.0),
                                0, 20, 0.1, 2, "mm");
        fang->signal_value_changed().connect([this] {
            Inkscape::Preferences::get()->setDouble("/hirch/aufraeumen/fang", fang->get_value());
        });
        schliessen->signal_value_changed().connect([this] {
            Inkscape::Preferences::get()->setDouble("/hirch/aufraeumen/schliessen", schliessen->get_value());
        });
        box->add(*g);
        kopf("SCHRIFTEN");
        {
            auto t = label(TR("Einlinienschriften stehen im Textwerkzeug oben in der Schriftliste. Eigene dazunehmen: "
                           "TTF/OTF, SVG-Schrift, Hershey (.jhf) oder eine Zeichentafel (DXF/SVG)."),
                           "hirch-dim");
            klasse(*t, "hirch-klein");
            t->set_line_wrap(true);
            t->set_max_width_chars(40);
            t->set_xalign(0);
            box->add(*t);
            auto reihe = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
            auto imp = Gtk::make_managed<Gtk::Button>(TR("Schrift importieren …"));
            auto taf = Gtk::make_managed<Gtk::Button>(TR("Zeichentafel …"));
            imp_knopf = imp;
            taf_knopf = taf;
            reihe->add(*imp);
            reihe->add(*taf);
            box->add(*reihe);
            schrift_info.set_line_wrap(true);
            schrift_info.set_max_width_chars(40);
            schrift_info.set_xalign(0);
            klasse(schrift_info, "hirch-klein");
            box->add(schrift_info);
            imp->signal_clicked().connect([this] {
                auto f = fraesen();
                auto w = dynamic_cast<Gtk::Window *>(panel.get_toplevel());
                if (!f || !w) return;
                f->schriftImportieren(*w, [this](std::string const &text) { schrift_info.set_text(text); });
            });
            taf->signal_clicked().connect([this] {
                auto f = fraesen();
                auto w = dynamic_cast<Gtk::Window *>(panel.get_toplevel());
                if (f && w) f->zeichentafel(*w);
            });
        }
        kopf("MASCHINE");
        auto mh = label(TR("Gilt für alle Zeichnungen an dieser Maschine. Die Prüfung warnt, wenn ein Programm die "
                        "Verfahrwege, Drehzahlen oder Vorschübe überschreitet."), "hirch-dim");
        klasse(*mh, "hirch-klein");
        mh->set_line_wrap(true);
        mh->set_max_width_chars(40);
        mh->set_xalign(0);
        box->add(*mh);
        box->add(maschine);
        kopf("SO FRÄSE ICH");
        box->add(vorgaben);
        kopf("WERKSTOFFE");
        box->add(werkstoffe);
        kopf("SPRACHE UND EINHEIT");
        {
            auto gs = formular();
            auto ls = label(TR("Sprache der Oberfläche"));
            ls->set_hexpand(true);
            gs->attach(*ls, 0, 0);
            sprache.append("auto", TR("Wie das System"));
            sprache.append("de", "Deutsch");
            sprache.append("en", "English");
            auto jetzt = prefs->getString("/hirch/sprache");
            sprache.set_active_id(jetzt.empty() ? "auto" : jetzt);
            if (sprache.get_active_row_number() < 0) sprache.set_active_id("auto");
            gs->attach(sprache, 1, 0);
            box->add(*gs);
            sprache_hinweis.set_text(TR("Gilt nach einem Neustart von Hirch CAM. Zeichnungen bleiben, wie sie sind – "
                                        "nur die Oberfläche wechselt die Sprache."));
            klasse(sprache_hinweis, "hirch-dim");
            klasse(sprache_hinweis, "hirch-klein");
            sprache_hinweis.set_line_wrap(true);
            sprache_hinweis.set_max_width_chars(40);
            sprache_hinweis.set_xalign(0);
            box->add(sprache_hinweis);
            // Einheit (Peter 08.11.: mm und Zoll für den Release) – intern bleibt alles mm
            {
                auto le = label(TR("Masse anzeigen in"));
                le->set_hexpand(true);
                gs->attach(*le, 0, 1);
                // ohne TR: der Text nennt beide Einheiten und darf nicht umgerechnet werden
                einheit.append("mm", hirch_englisch() ? "Millimetres (mm)" : "Millimeter (mm)");
                einheit.append("in", hirch_englisch() ? "Inches (in)" : "Zoll (in)");
                einheit.set_active_id(prefs->getString("/hirch/einheit") == "in" ? "in" : "mm");
                gs->attach(einheit, 1, 1);
                einheit.signal_changed().connect([this] {
                    Inkscape::Preferences::get()->setString("/hirch/einheit", einheit.get_active_id().raw());
                    Inkscape::Preferences::get()->save();
                    sprache_hinweis.set_markup("<b>" + markup_escape(TR("Bitte Hirch CAM neu starten, damit die Einheit wechselt. "
                                                                        "Zeichnungen und Werte bleiben, wie sie sind.")) + "</b>");
                });
            }
            sprache.signal_changed().connect([this] {
                auto id = sprache.get_active_id().raw();
                auto pr = Inkscape::Preferences::get();
                pr->setString("/hirch/sprache", id);
                // Inkscapes eigene Texte (Menüs, Werkzeuge) folgen beim nächsten Start
                pr->setString("/ui/language", id == "auto" ? "" : id);
                sprache_hinweis.set_markup("<b>" + markup_escape(TR("Bitte Hirch CAM neu starten, damit die Sprache wechselt.")) + "</b>");
            });
        }
        sw = Gtk::make_managed<Gtk::ScrolledWindow>();
        inhalt = box;
        sw->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
        sw->set_vexpand(true);
        sw->add(*box);
        // Oben (Konzept 06.11.): Suche und Sprung zu den Abschnitten – statt langer Liste zum Rollen
        {
            auto oben = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
            oben->set_margin_top(8);
            oben->set_margin_bottom(4);
            oben->set_margin_start(2);
            oben->set_margin_end(2);
            suche.set_placeholder_text(TR("Einstellung suchen …"));
            suche.set_icon_from_icon_name("edit-find-symbolic");
            oben->add(suche);
            auto reihe = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
            klasse(*reihe, "linked");
            reihe->set_homogeneous(true);
            for (auto const &[t, k] : std::vector<std::pair<char const *, char const *>>{
                     {"Maschine", "MASCHINE"}, {"Fräsen", "SO FRÄSE ICH"}, {"Werkstoffe", "WERKSTOFFE"},
                     {"Programm", "DARSTELLUNG"}}) {
                auto b = Gtk::make_managed<Gtk::Button>(TR(t));
                std::string kopf = k;
                b->signal_clicked().connect([this, kopf] { abschnitt_zeigen(kopf); });
                reihe->add(*b);
            }
            oben->add(*reihe);
            UI::pack_start(panel, *oben, false, false);
            suche.signal_changed().connect([this] { suchen(false); });
            suche.signal_activate().connect([this] { suchen(true); });
        }
        UI::pack_start(panel, *sw, true, true);
        zeigen();

        auto win = [this]() -> InkscapeWindow * {
            auto desk = panel.getDesktop();
            return desk ? desk->getInkscapeWindow() : nullptr;
        };
        dunkel.property_active().signal_changed().connect([this, win] {
            if (setzt || dunkel.get_active() == design_dunkel()) return;
            if (auto w = win()) w->activate_action("hirch-dunkel");
            else design_setzen(dunkel.get_active(), nullptr);
        });
        bahnen.property_active().signal_changed().connect([this, win] {
            if (setzt) return;
            bool an = Inkscape::Preferences::get()->getBool("/hirch/fraesbahnen", true);
            if (bahnen.get_active() == an) return;
            if (auto w = win()) w->activate_action("hirch-bahnen");
            else Inkscape::Preferences::get()->setBool("/hirch/fraesbahnen", bahnen.get_active());
        });
        geister.property_active().signal_changed().connect([this] {
            if (setzt) return;
            Inkscape::Preferences::get()->setBool("/hirch/andere_seite", geister.get_active());
        });
        leuchten.property_active().signal_changed().connect([this] {
            if (setzt) return;
            Inkscape::Preferences::get()->setBool("/hirch/auswahl_leuchten", leuchten.get_active());
        });
        autor.property_active().signal_changed().connect([this] {
            if (setzt) return;
            Inkscape::Preferences::get()->setBool("/hirch/auto_rechnen", autor.get_active());
        });
        aufr.property_active().signal_changed().connect([this] {
            if (setzt) return;
            Inkscape::Preferences::get()->setBool("/hirch/import_aufraeumen", aufr.get_active());
        });
        // Menü und andere Fenster ändern dieselben Einstellungen: Schalter folgen
        for (char const *pfad : {"/theme/darkTheme", "/hirch/fraesbahnen", "/hirch/andere_seite"})
            beobachter.push_back(prefs->createObserver(pfad, [this] { zeigen(); }));
        panel.signal_map().connect([this] { maschine_fuellen(); });
    }

    void maschine_fuellen()
    {
        auto desk = panel.getDesktop();
        auto c = desk ? desk->getContainer() : nullptr;
        if (auto f = c ? dynamic_cast<HirchFraesen *>(c->get_dialog("Fraesen")) : nullptr) {
            f->maschineFuellen(&maschine);
            f->werkstoffeFuellen(&werkstoffe);
            f->vorgabenFuellen(&vorgaben);
        }
    }

    void zeigen()
    {
        setzt = true;
        auto prefs = Inkscape::Preferences::get();
        dunkel.set_active(design_dunkel());
        bahnen.set_active(prefs->getBool("/hirch/fraesbahnen", true));
        geister.set_active(prefs->getBool("/hirch/andere_seite", true));
        leuchten.set_active(prefs->getBool("/hirch/auswahl_leuchten", true));
        aufr.set_active(prefs->getBool("/hirch/import_aufraeumen", true));
        autor.set_active(prefs->getBool("/hirch/auto_rechnen", true));
        setzt = false;
    }
};

HirchAnsicht::HirchAnsicht()
    : DialogBase("/dialogs/hirch-ansicht", "HirchAnsicht")
    , _impl(std::make_unique<Impl>(*this))
{
    show_all_children();
}

HirchAnsicht::~HirchAnsicht() = default;
void HirchAnsicht::abschnittZeigen(std::string const &kopf) { _impl->abschnitt_zeigen(kopf); }
void HirchAnsicht::schriftImportieren() { if (_impl->imp_knopf) _impl->imp_knopf->clicked(); }
void HirchAnsicht::zeichentafel() { if (_impl->taf_knopf) _impl->taf_knopf->clicked(); }

} // namespace Inkscape::UI::Dialog
