// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Oberflächentest.
 *
 * Mit HIRCH_OBERFLAECHENTEST=/pfad/bericht.json gestartet, öffnet Hirch CNC nach dem
 * Laden der Zeichnung der Reihe nach alle Fenster und Dialoge (Menü «Fräsen», jede
 * Bearbeitung, Reiter), liest alle sichtbaren Texte, schliesst wieder und schreibt einen
 * Bericht: welche Schritte gelaufen sind, welche Fenster aufgingen und – in der englischen
 * Oberfläche – welche Texte noch deutsch sind. Danach beendet sich das Programm.
 * tests/test_oberflaeche.py startet das unter Xvfb auf Deutsch und Englisch.
 */
#include "ui/dialog/hirch-hilfen.h"

#include <gtkmm/dialog.h>
#include <gtkmm/notebook.h>
#include <gtkmm/window.h>
#include <unistd.h>

#include "inkscape-window.h"
#include "ui/dialog/hirch-3d.h"
#include "xml/repr.h"
#include "object/sp-shape.h"
#include "document-undo.h"
#include "actions/actions-tools.h"
#include <gtkmm/scrolledwindow.h>

namespace Inkscape::UI::Dialog {

namespace {

struct Testlauf {
    InkscapeWindow *win = nullptr;
    std::string bericht_pfad;
    struct Schritt {
        std::string name;
        std::vector<std::string> fenster;
        bool ok = true;
        std::string fehler;
    };
    std::vector<Schritt> schritte;
    std::vector<std::pair<std::string, std::string>> deutsch;   // Text, wo
    std::vector<std::pair<std::string, std::string>> metrisch;  // bei Zoll: Text mit «mm», wo
    std::vector<std::pair<std::string, std::string>> ohne_umrechnung;   // Zahlenfeld neben «Zoll», aber in mm
    std::set<std::string> gesehen;
    std::vector<std::function<void()>> folge;
    std::size_t naechster = 0;
    sigc::connection schliesser;
    bool halten = false;            // Fenster offen lassen (ein Schritt bedient sie selbst)
};

Testlauf &lauf()
{
    static Testlauf l;
    return l;
}

/// Ist das ein deutscher Text, der in der englischen Oberfläche übersetzt sein müsste?
bool sieht_deutsch_aus(std::string const &t)
{
    if (t.size() < 3) return false;
    if (hirch_im_katalog(t)) return true;
    for (char const *z : {"ä", "ö", "ü", "Ä", "Ö", "Ü", "ß"})
        if (t.find(z) != std::string::npos) return true;
    static std::regex const woerter(R"(\b(und|oder|nicht|mit|der|die|das|ein|eine|wird|werden|ist|Bearbeitung|Werkzeug|Tiefe|Zustellung|Speichern|Abbrechen|Schliessen|Datei)\b)");
    return std::regex_search(t, woerter);
}

void texte_sammeln(Gtk::Widget *w, std::string const &wo)
{
    if (!w || !w->get_visible()) return;
    auto pruefen = [&](std::string t) {
        if (t.empty()) return;
        // Markup entfernen
        t = std::regex_replace(t, std::regex("<[^>]*>"), "");
        auto &l = lauf();
        if (!l.gesehen.insert(wo + "|" + t).second) return;
        if (hirch_englisch() && sieht_deutsch_aus(t)) l.deutsch.emplace_back(t, wo);
        // Zoll: kein «mm» mehr sichtbar (an einheit_text/einheit_feld vorbei)
        static std::regex const mm(R"((^|[^A-Za-z])mm([^A-Za-z]|$))");
        if (hirch_zoll() && std::regex_search(t, mm)) l.metrisch.emplace_back(t, wo);
    };
    // Zoll: ein Zahlenfeld mit einer Längen-Einheit daneben muss umrechnen (sonst stünde ein mm-Wert als Zoll da)
    if (hirch_zoll())
        if (auto sp = dynamic_cast<Gtk::SpinButton *>(w);
            sp && sp->get_mapped() && !sp->get_data("hirch-einheit") && !sp->get_data("hirch-einheit-nie") &&
            einheit_nachbar(sp))     // nur gezeigte: ungezeigte rechnen beim ersten Zeigen um (beim_zeigen)
            lauf().ohne_umrechnung.emplace_back(
                sp->get_text().raw() + " | " + static_cast<Gtk::Label *>(einheit_nachbar(sp))->get_text().raw(), wo);
    if (auto lab = dynamic_cast<Gtk::Label *>(w)) pruefen(lab->get_text());
    if (auto b = dynamic_cast<Gtk::Button *>(w)) pruefen(b->get_label());
    if (w->get_has_tooltip()) pruefen(w->get_tooltip_text());
    if (auto c = dynamic_cast<Gtk::Container *>(w)) {
        for (auto k : c->get_children()) texte_sammeln(k, wo);
    }
    if (auto nb = dynamic_cast<Gtk::Notebook *>(w)) {
        for (int i = 0; i < nb->get_n_pages(); ++i)
            if (auto tab = nb->get_tab_label(*nb->get_nth_page(i))) texte_sammeln(tab, wo);
    }
}

/// Neue Fenster lesen und schliessen (läuft auch in der Schleife eines modalen Dialogs)
bool fenster_schliessen()
{
    auto &l = lauf();
    if (l.halten) return true;
    for (auto w : Gtk::Window::list_toplevels()) {
        if (!w || !w->get_visible() || w == l.win || w->get_type_hint() == Gdk::WINDOW_TYPE_HINT_TOOLTIP) continue;
        if (w->get_type_hint() == Gdk::WINDOW_TYPE_HINT_DND || w->get_type_hint() == Gdk::WINDOW_TYPE_HINT_POPUP_MENU ||
            w->get_type_hint() == Gdk::WINDOW_TYPE_HINT_DROPDOWN_MENU || w->get_type_hint() == Gdk::WINDOW_TYPE_HINT_COMBO)
            continue;
        std::string titel = w->get_title().raw();
        if (!l.schritte.empty()) l.schritte.back().fenster.push_back(titel.empty() ? "(ohne Titel)" : titel);
        texte_sammeln(w, l.schritte.empty() ? titel : l.schritte.back().name + " › " + titel);
        // mit HIRCH_TEST_FENSTERBILDER=Ordner ein Bildschirmfoto je Fenster (zum Anschauen)
        if (auto ordner = g_getenv("HIRCH_TEST_FENSTERBILDER"); ordner && w->get_window()) {
            static int nr = 0;
            auto gw = w->get_window();
            if (auto pb = Gdk::Pixbuf::create(gw, 0, 0, gw->get_width(), gw->get_height())) {
                char name[32];
                std::snprintf(name, sizeof name, "/fenster_%03d.png", ++nr);
                pb->save(std::string(ordner) + name, "png");
            }
        }
        if (auto d = dynamic_cast<Gtk::Dialog *>(w)) {
            d->response(Gtk::RESPONSE_CANCEL);
            d->hide();
        } else {
            w->hide();
        }
    }
    return true;
}

void bericht_schreiben()
{
    auto &l = lauf();
    bj::object b;
    b["sprache"] = hirch_sprache();
    bj::array s;
    for (auto const &x : l.schritte) {
        bj::array f;
        for (auto const &t : x.fenster) f.push_back(bj::value(t));
        s.push_back(bj::object{{"name", x.name}, {"ok", x.ok}, {"fehler", x.fehler}, {"fenster", std::move(f)}});
    }
    b["schritte"] = std::move(s);
    bj::array d;
    for (auto const &[t, wo] : l.deutsch) d.push_back(bj::object{{"text", t}, {"wo", wo}});
    b["deutsch"] = std::move(d);
    b["einheit"] = hirch_einheit();
    {
        // Versionen (Peter 09.11.: 1.0 Beta): Anzeige und Vergleich fürs Aktualisieren
        bj::object v;
        v["anzeige beta"] = hirch_version_anzeige("1.0.0-beta.1");
        v["anzeige fertig"] = hirch_version_anzeige("1.0.0");
        v["anzeige peter"] = hirch_version_anzeige("2026.11.09-af4e82c");
        v["beta1<beta2"] = hirch_version_vergleich("1.0.0-beta.1", "1.0.0-beta.2");
        v["beta2<1.0.0"] = hirch_version_vergleich("1.0.0-beta.2", "1.0.0");
        v["1.0.1>1.0.0"] = hirch_version_vergleich("1.0.1", "1.0.0");
        v["datum<datum"] = hirch_version_vergleich("2026.11.08-aaa", "2026.11.09-bbb");
        v["peter gleich"] = hirch_version_vergleich("2026.11.09-bbb-peter", "2026.11.09-bbb");
        v["gemischt"] = hirch_version_vergleich("1.0.0-beta.1", "2026.11.09-bbb");
        b["version_proben"] = std::move(v);
    }
    {
        // Stichproben der Text-Umrechnung (gleiche Regeln wie hirch_cam/einheit.py)
        bj::object p;
        for (char const *t : {"Ø 6,35 mm", "Ø 6 mm", "Tiefe 6,35 mm", "Ø 38,1 mm", "600 × 900 mm", "X -25,4 … 254 mm"})
            p[t] = einheit_text(t);
        p["mass 3.175"] = einheit_mass(3.175, "3,175");
        p["mass 0.79375"] = einheit_mass(0.79375, "0,794");      // 1/32" (Windows 10.11.)
        b["einheit_proben"] = std::move(p);
    }
    if (auto ein = g_getenv("HIRCH_TEST_BALLAST")) {
        // Import aus Illustrator/PDF: nur die Linien (Peter 10.11.)
        bj::array a;
        for (auto const &t : hirch_ballast_probe(ein, std::string(ein) + ".aus.svg")) a.push_back(bj::string(t));
        b["ballast"] = std::move(a);
    }
    bj::array m, o;
    for (auto const &[t, wo] : l.metrisch) m.push_back(bj::object{{"text", t}, {"wo", wo}});
    for (auto const &[t, wo] : l.ohne_umrechnung) o.push_back(bj::object{{"text", t}, {"wo", wo}});
    b["metrisch"] = std::move(m);
    b["ohne_umrechnung"] = std::move(o);
    try {
        Glib::file_set_contents(l.bericht_pfad, bj::serialize(b));
    } catch (...) {
    }
}

void weiter()
{
    auto &l = lauf();
    if (l.naechster >= l.folge.size()) {
        fenster_schliessen();
        bericht_schreiben();
        _exit(0);                       // ohne «Änderungen speichern?»
    }
    auto f = l.folge[l.naechster++];
    Glib::signal_timeout().connect_once([f] {
        try {
            f();
        } catch (std::exception const &e) {
            lauf().schritte.back().ok = false;
            lauf().schritte.back().fehler = e.what();
        }
        Glib::signal_timeout().connect_once([] {
            fenster_schliessen();
            weiter();
        }, 900);
    }, 300);
}

HirchFraesen *panel(InkscapeWindow *win)
{
    auto dt = win->get_desktop();
    auto c = dt ? dt->getContainer() : nullptr;
    return c ? dynamic_cast<HirchFraesen *>(c->get_dialog("Fraesen")) : nullptr;
}

} // namespace

bool hirch_oberflaechentest_an() { return g_getenv("HIRCH_OBERFLAECHENTEST") != nullptr; }

void hirch_oberflaechentest(InkscapeWindow *win)
{
    auto &l = lauf();
    l.win = win;
    l.bericht_pfad = g_getenv("HIRCH_OBERFLAECHENTEST");
    // Dialoge, die im Ablauf aufgehen, alle 400 ms lesen und schliessen
    l.schliesser = Glib::signal_timeout().connect([] { return fenster_schliessen(); }, 400);

    auto schritt = [&l](std::string name, std::function<void()> f) {
        l.folge.push_back([&l, name, f] {
            l.schritte.push_back({name, {}, true, {}});
            g_printerr("Hirch-Test: %s\n", name.c_str());   // bei einem Absturz: letzter Schritt im Log
            f();
        });
    };
    // Hauptfenster mit Panel
    schritt("Hauptfenster", [win] {
        texte_sammeln(win, "Hauptfenster");
        if (!panel(win)) throw std::runtime_error("Fräsen-Panel fehlt");
    });
    for (char const *a : {"hirch-material", "hirch-werkzeuge", "hirch-postprozessoren",
                          "hirch-spannmittel", "hirch-stege", "hirch-verschachteln", "hirch-aufraeumen",
                          "hirch-als-vorlage", "hirch-aus-vorlage", "hirch-fassungen", "hirch-vorschau",
                          "hirch-willkommen", "hirch-3d-einlesen", "hirch-messkorrektur"}) {
        schritt(a, [win, a] {
            if (!win->lookup_action(a)) throw std::runtime_error("Aktion fehlt");
            win->activate_action(a);
        });
    }
    // «+ Neu» mit ausgewählter Form: der Dialog geht zuerst auf; «Abbrechen» (hier: der
    // Schliesser) nimmt die Bearbeitung wieder heraus und gibt der Form ihre Farbe zurück
    schritt("Neu mit Auswahl, Abbrechen", [win, &l] {
        auto dt = win->get_desktop();
        auto p = panel(win);
        if (!dt || !p) throw std::runtime_error("kein Panel");
        auto d = dt->getDocument();
        SPItem *form = nullptr;
        for (auto o : d->getObjectsByElement("path")) {
            if (auto it = cast<SPItem>(o); it && it->getRepr()->attribute("id")) {
                form = it;
                break;
            }
        }
        if (!form) throw std::runtime_error("keine Form in der Zeichnung");
        std::string id = form->getRepr()->attribute("id");
        auto stil = [&] {
            auto o = d->getObjectById(id);
            auto s = o ? o->getRepr()->attribute("style") : nullptr;
            return std::string(s ? s : "");
        };
        auto vorher = stil();
        int n0 = p->anzahlBearbeitungen();
        dt->getSelection()->set(form);
        p->neuAnlegen("kontur");
        bool mehr = p->anzahlBearbeitungen() == n0 + 1, anders = stil() != vorher;
        auto start = g_get_monotonic_time();
        while (g_get_monotonic_time() - start < 6'000'000) {   // Dialog auf, Schliesser bricht ab
            while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
            g_usleep(50'000);
        }
        std::string f;
        if (!mehr || !anders) f += "beim Anlegen nicht eingefärbt/angelegt; ";
        if (l.schritte.back().fenster.empty()) f += "kein Dialog aufgegangen; ";
        if (p->anzahlBearbeitungen() != n0) f += "Bearbeitung nach «Abbrechen» noch da; ";
        if (stil() != vorher) f += "Farbe nicht zurück (" + stil() + " statt " + vorher + "); ";
        dt->getSelection()->clear();
        if (!f.empty()) throw std::runtime_error(f);
    });
    // HIRCH_TEST_NEU=art,art,…: je eine neue Bearbeitung anlegen – ihr Dialog geht auf (Bild mit
    // HIRCH_TEST_FENSTERBILDER), der Schliesser bricht ab und nimmt sie wieder heraus
    if (auto arten = g_getenv("HIRCH_TEST_NEU")) {
        std::string rest = arten;
        while (!rest.empty()) {
            auto k = rest.find(',');
            std::string a = rest.substr(0, k);
            rest = k == std::string::npos ? std::string() : rest.substr(k + 1);
            if (a.empty()) continue;
            schritt("Neu " + a, [win, a] {
                auto p = panel(win);
                if (!p) throw std::runtime_error("kein Panel");
                if (auto dt = win->get_desktop()) dt->getSelection()->clear();
                p->neuAnlegen(a);
                auto start = g_get_monotonic_time();
                while (g_get_monotonic_time() - start < 6'000'000) {
                    while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                    g_usleep(50'000);
                }
            });
        }
    }
    // Dialog «Neue Zeichnung einrichten» (sonst nur bei neuen, nie gespeicherten Zeichnungen)
    schritt("Anfangsdialog", [win] {
        if (auto p = panel(win)) p->anfangsdialog();
    });
    // Einstellungen › Maschine › «Spindel und Halterung …» (3D)
    schritt("Spindel und Halterung", [win] {
        // erst fertig rechnen lassen: der Dialog zeigt die Prüfung der letzten Rechnung
        auto start = g_get_monotonic_time();
        while (g_get_monotonic_time() - start < 90'000'000) {
            while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
            g_usleep(50'000);
            auto p = panel(win);
            if (p && !p->beschaeftigt() && g_get_monotonic_time() - start > 3'000'000) break;
        }
        // HIRCH_TEST_BILD7: Hauptfenster (z. B. mit der roten Markierung einer Kollision)
        if (auto bild = g_getenv("HIRCH_TEST_BILD7")) {
            win->activate_action("canvas-zoom-page");
            for (int i = 0; i < 20; ++i) {
                while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                g_usleep(50'000);
            }
            if (auto gw = win->get_window())
                if (auto pb = Gdk::Pixbuf::create(gw, 0, 0, gw->get_width(), gw->get_height()))
                    pb->save(bild, "png");
        }
        if (auto p = panel(win)) p->halterung();
    });
    // mit HIRCH_TEST_START_MODELL=Datei: im Anfangsdialog das 3D-Modell wählen, «Los geht's» –
    // danach wird es eingelesen und (Netz) zweiseitig mit Stegen; HIRCH_TEST_BILD5: Dialog,
    // HIRCH_TEST_BILD6: danach das Fenster; HIRCH_TEST_SVG: Zeichnung speichern
    if (g_getenv("HIRCH_TEST_START_MODELL")) {
        schritt("Anfangsdialog mit 3D-Modell", [win] {
            lauf().halten = true;
            auto phase = std::make_shared<int>(0);
            auto beginn = g_get_monotonic_time();
            Glib::signal_timeout().connect([phase, beginn]() -> bool {
                Gtk::Dialog *dlg = nullptr;
                for (auto w : Gtk::Window::list_toplevels())
                    if (auto d = dynamic_cast<Gtk::Dialog *>(w); d && d->get_visible() &&
                                                                d->get_title() == TR("Neue Zeichnung einrichten"))
                        dlg = d;
                if (!dlg) return g_get_monotonic_time() - beginn < 20'000'000;
                std::function<Gtk::Button *(Gtk::Widget *, std::string const &)> knopf =
                    [&](Gtk::Widget *w, std::string const &t) -> Gtk::Button * {
                    if (auto b = dynamic_cast<Gtk::Button *>(w); b && b->get_label() == t) return b;
                    if (auto ct = dynamic_cast<Gtk::Container *>(w))
                        for (auto k : ct->get_children())
                            if (auto r = knopf(k, t)) return r;
                    return nullptr;
                };
                if (*phase == 0) {
                    // HIRCH_TEST_START_MODELL=testteil: Knopf «Testteil laden»
                    bool tt = std::string(g_getenv("HIRCH_TEST_START_MODELL")) == "testteil";
                    if (auto b = knopf(dlg, tt ? TR("Testteil laden") : TR("3D-Modell einlesen …"))) {
                        b->clicked();
                        *phase = 1;
                    }
                    return true;
                }
                if (!knopf(dlg, TR("Anderes …")) && g_get_monotonic_time() - beginn < 30'000'000) return true;
                for (int i = 0; i < 10; ++i) {
                    while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                    g_usleep(30'000);
                }
                if (auto bild = g_getenv("HIRCH_TEST_BILD5"))
                    if (auto gw = dlg->get_window())
                        if (auto pb = Gdk::Pixbuf::create(gw, 0, 0, gw->get_width(), gw->get_height()))
                            pb->save(bild, "png");
                texte_sammeln(dlg, "Anfangsdialog › 3D-Modell");
                dlg->response(Gtk::RESPONSE_OK);
                return false;
            }, 300);
            if (auto p = panel(win)) p->anfangsdialog();
            lauf().halten = false;
            // einlesen, zweiseitig, Stege: warten, bis der Reiter «3D» fertig ist
            auto start = g_get_monotonic_time();
            while (g_get_monotonic_time() - start < 60'000'000) {
                while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                g_usleep(50'000);
                auto dt = win->get_desktop();
                auto c = dt ? dt->getContainer() : nullptr;
                auto d3 = c ? dynamic_cast<Hirch3D *>(c->get_dialog("Hirch3D")) : nullptr;
                if (d3 && !d3->beschaeftigt() && g_get_monotonic_time() - start > 8'000'000) break;
            }
            for (int i = 0; i < 20; ++i) {
                while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                g_usleep(50'000);
            }
            if (auto bild = g_getenv("HIRCH_TEST_BILD6")) {
                win->activate_action("canvas-zoom-page");
                for (int i = 0; i < 10; ++i) {
                    while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                    g_usleep(50'000);
                }
                if (auto gw = win->get_window())
                    if (auto pb = Gdk::Pixbuf::create(gw, 0, 0, gw->get_width(), gw->get_height()))
                        pb->save(bild, "png");
            }
            if (auto ziel = g_getenv("HIRCH_TEST_SVG"))
                if (auto doc = win->get_document()) sp_repr_save_file(doc->getReprDoc(), ziel, SP_SVG_NS_URI);
        });
    }
    // jede Bearbeitung
    l.folge.push_back([&l, win] {
        auto p = panel(win);
        int n = p ? p->anzahlBearbeitungen() : 0;
        // die Schritte für die Bearbeitungen hinten anhängen
        for (int i = 0; i < n; ++i) {
            l.folge.push_back([&l, win, i] {
                l.schritte.push_back({"Bearbeitung " + std::to_string(i + 1), {}, true, {}});
                if (auto p_ = panel(win)) p_->bearbeitungOeffnen(i);
            });
        }
        l.schritte.push_back({"Bearbeitungen: " + std::to_string(n), {}, n > 0, n > 0 ? "" : "keine"});
    });
    // HIRCH_TEST_BILD12: ganze Zeichnung (Seite eingepasst, 1600 × 1000) – z. B. Rillen der Gravuren
    // HIRCH_TEST_WERKZEUGE: mit Text- und Knotenwerkzeug ändern, dann zum Auswahlwerkzeug – meldet
    // (stderr «WERKZEUGTEST …», Zeitstempel in µs Echtzeit), ob das Panel dazwischen rechnet
    if (g_getenv("HIRCH_TEST_WERKZEUGE")) {
        schritt("Werkzeuge pausieren", [win] {
            auto p = panel(win);
            auto dt = win->get_desktop();
            if (!p || !dt) throw std::runtime_error("Panel fehlt");
            auto pumpen = [](double s) {
                auto t0 = g_get_monotonic_time();
                while (g_get_monotonic_time() - t0 < s * 1e6) {
                    while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                    g_usleep(20'000);
                }
            };
            auto zustand = [p](char const *was) {
                g_printerr("WERKZEUGTEST %ld %s beschaeftigt=%d wartet=%d\n", (long)g_get_real_time(), was,
                           (int)p->beschaeftigt(), (int)p->wartetAufKnopf());
            };
            // beobachten: hat das Panel in der Phase je gerechnet?
            auto phase = [&](char const *name, double s) {
                bool je = false;
                auto t0 = g_get_monotonic_time();
                while (g_get_monotonic_time() - t0 < s * 1e6) {
                    while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                    g_usleep(20'000);
                    je = je || (p->beschaeftigt() && !p->wartetAufKnopf());
                }
                g_printerr("WERKZEUGTEST %ld %s je_beschaeftigt=%d\n", (long)g_get_real_time(), name, (int)je);
            };
            pumpen(1.0);
            auto t0 = g_get_monotonic_time();
            while (p->beschaeftigt() && g_get_monotonic_time() - t0 < 60'000'000) pumpen(0.05);
            if (p->wartetAufKnopf()) p->jetztRechnen();
            pumpen(0.5);
            while ((p->beschaeftigt() || p->wartetAufKnopf()) && g_get_monotonic_time() - t0 < 300'000'000) pumpen(0.05);
            zustand("gerechnet");
            auto wurzel = dt->getDocument()->getReprRoot();
            std::function<Inkscape::XML::Node *(Inkscape::XML::Node *, char const *)> suche =
                [&](Inkscape::XML::Node *n, char const *name) -> Inkscape::XML::Node * {
                for (auto c = n->firstChild(); c; c = c->next()) {
                    if (c->name() && std::string(c->name()) == name) return c;
                    if (auto r = suche(c, name)) return r;
                }
                return nullptr;
            };
            auto text = suche(wurzel, "svg:text");
            auto pfad = suche(wurzel, "svg:path");
            set_active_tool(win, "Text");
            pumpen(0.5);
            zustand("text_an");
            for (char const *t : {"HIRCH ", "HIRCH C", "HIRCH CN", "HIRCH CNC"}) {
                if (text && text->firstChild()) text->firstChild()->setContent(t);
                DocumentUndo::done(dt->getDocument(), "Test Text", "");
                pumpen(0.4);
            }
            phase("text", 3.0);
            set_active_tool(win, "Node");
            pumpen(0.5);
            zustand("knoten_an");
            for (char const *d : {"M 25,30 L 75,31", "M 25,30 L 75,32", "M 25,30 L 74,32"}) {
                if (pfad) pfad->setAttribute("d", d);
                DocumentUndo::done(dt->getDocument(), "Test Knoten", "");
                pumpen(0.4);
            }
            phase("knoten", 3.0);
            set_active_tool(win, "Select");
            phase("auswahl", 4.0);
            zustand("ende");
        });
    }

    // HIRCH_TEST_BILD2D=Bild: rechnen lassen und ein Bild des Hauptfensters in 2D (Zeichenfläche und Panel)
    if (auto bild_c = g_getenv("HIRCH_TEST_BILD2D")) {
        std::string bild = bild_c;
        schritt("Bild 2D", [win, bild] {
            win->resize(1600, 1000);
            auto pumpen = [](double sek) {
                auto t0 = g_get_monotonic_time();
                while (g_get_monotonic_time() - t0 < sek * 1e6) {
                    while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                    g_usleep(30'000);
                }
            };
            if (auto p = panel(win)) {
                auto t0 = g_get_monotonic_time();
                pumpen(1.0);
                while (p->beschaeftigt() && g_get_monotonic_time() - t0 < 120'000'000) pumpen(0.1);
                if (p->wartetAufKnopf()) p->jetztRechnen();
                pumpen(0.5);
                while ((p->beschaeftigt() || p->wartetAufKnopf()) && g_get_monotonic_time() - t0 < 300'000'000) pumpen(0.1);
            }
            win->activate_action("canvas-zoom-page");
            pumpen(2.0);
            if (auto gw = win->get_window())
                if (auto pb = Gdk::Pixbuf::create(gw, 0, 0, gw->get_width(), gw->get_height())) pb->save(bild, "png");
        });
    }

    // HIRCH_TEST_HAUPT3D=Bild: rechnen lassen, im Hauptfenster auf «3D Ansicht» umschalten (win.hirch-ansicht),
    // Bild, zurück auf 2D. HIRCH_TEST_HAUPT3D_UNTEN=Bild: dazu ein Bild von unten (Kamera gedreht)
    if (auto bild_c = g_getenv("HIRCH_TEST_HAUPT3D")) {
        std::string bild = bild_c;
        schritt("3D-Ansicht im Hauptfenster", [win, bild] {
            win->resize(1600, 1000);
            auto pumpen = [](double sek) {
                auto t0 = g_get_monotonic_time();
                while (g_get_monotonic_time() - t0 < sek * 1e6) {
                    while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                    g_usleep(30'000);
                }
            };
            // HIRCH_TEST_PLATTE=N: zuerst Platte N wählen (Klick auf ihre Seite); nach dem Bild Platte 1
            // rechnen und zeigen (…p1.png), dann zurück auf N (…zurueck.png, aufgehobenes Ergebnis)
            auto seite_waehlen = [win](int nr) {
                if (auto dt = win->get_desktop()) {
                    auto ps = platten_seiten(dt->getDocument());
                    if (nr >= 1 && nr <= (int)ps.size()) dt->getDocument()->getPageManager().selectPage(ps[nr - 1]);
                }
            };
            auto rechnen_warten = [win, pumpen] {
                if (auto p = panel(win)) {
                    auto t0 = g_get_monotonic_time();
                    pumpen(1.0);
                    while (p->beschaeftigt() && g_get_monotonic_time() - t0 < 120'000'000) pumpen(0.1);
                    if (p->wartetAufKnopf()) p->jetztRechnen();
                    pumpen(0.5);
                    while ((p->beschaeftigt() || p->wartetAufKnopf()) && g_get_monotonic_time() - t0 < 300'000'000) pumpen(0.1);
                }
            };
            int platte_nr = g_getenv("HIRCH_TEST_PLATTE") ? std::atoi(g_getenv("HIRCH_TEST_PLATTE")) : 0;
            if (platte_nr > 0) {
                pumpen(1.5);
                seite_waehlen(platte_nr);
            }
            rechnen_warten();
            win->activate_action("hirch-ansicht", Glib::Variant<Glib::ustring>::create("3d"));
            pumpen(4.0);
            auto foto = [win](std::string const &pfad) {
                if (auto gw = win->get_window())
                    if (auto pb = Gdk::Pixbuf::create(gw, 0, 0, gw->get_width(), gw->get_height())) pb->save(pfad, "png");
            };
            // Bedienung der 3D-Ansicht: Knöpfe und Häkchen über ihre Beschriftung finden
            std::function<Gtk::Widget *(Gtk::Widget *, std::string const &)> finden =
                [&](Gtk::Widget *w, std::string const &t) -> Gtk::Widget * {
                if (auto b = dynamic_cast<Gtk::Button *>(w); b && b->get_label() == t) return b;
                if (auto c = dynamic_cast<Gtk::Container *>(w))
                    for (auto k : c->get_children())
                        if (auto r = finden(k, t)) return r;
                return nullptr;
            };
            foto(bild);
            // HIRCH_TEST_DREHBILDER=N: N weitere Bilder im Abstand von 0,1 s (Fräser dreht sich)
            if (auto nb = g_getenv("HIRCH_TEST_DREHBILDER"))
                for (int k = 1; k <= std::atoi(nb); ++k) {
                    pumpen(0.1);
                    foto(bild + ".dreh" + std::to_string(k) + ".png");
                }
            // HIRCH_TEST_VORSCHAU3D=1: dazu das Fenster «Vorschau 3D» öffnen (Stelle mit HIRCH_TEST_VORSCHAU_STELLE;
            // das Bild macht HIRCH_TEST_FENSTERBILDER, bevor der Test das Fenster schliesst)
            if (g_getenv("HIRCH_TEST_VORSCHAU3D"))
                if (auto p = panel(win)) {
                    p->vorschauZeigen();
                    pumpen(5.0);
                }
            if (platte_nr > 1) {
                seite_waehlen(1);
                pumpen(1.0);
                foto(bild + ".wechsel.png");             // gleich nach dem Wechsel, vor dem Rechnen
                rechnen_warten();
                pumpen(4.0);
                foto(bild + ".p1.png");
                seite_waehlen(platte_nr);
                rechnen_warten();
                pumpen(4.0);
                foto(bild + ".zurueck.png");
            }
            if (g_getenv("HIRCH_TEST_HAUPT3D_OHNE_BLOCK"))       // ohne Rohteil, zweites Bild
                if (auto h = dynamic_cast<Gtk::ToggleButton *>(finden(win, TR("Rohteil (Drahtgitter)")))) {
                    h->set_active(false);
                    pumpen(1.5);
                    foto(bild + ".ohne.png");
                    h->set_active(true);
                }
            if (auto b2 = g_getenv("HIRCH_TEST_HAUPT3D_UNTEN")) {
                // Auswahl in beide Richtungen: das erste Objekt mit Fräsfarbe wählen (Linie hell)
                if (auto dt = win->get_desktop()) {
                    std::function<SPItem *(SPObject *)> erstes = [&](SPObject *o) -> SPItem * {
                        for (auto &c : o->children) {
                            if (auto sh = cast<SPShape>(&c); sh && !c.getRepr()->attribute("hirch:steg3d")) return sh;
                            if (auto r = erstes(&c)) return r;
                        }
                        return nullptr;
                    };
                    if (auto it = erstes(dt->getDocument()->getRoot())) dt->getSelection()->set(it);
                }
                if (auto h = dynamic_cast<Gtk::ToggleButton *>(finden(win, TR("Fräsbahnen")))) h->set_active(true);
                pumpen(1.5);
                foto(std::string(b2) + ".oben.png");
                if (auto k = dynamic_cast<Gtk::ToggleButton *>(finden(win, TR("unten")))) k->set_active(true);
                pumpen(2.0);
                foto(b2);
                if (auto k = dynamic_cast<Gtk::ToggleButton *>(finden(win, TR("oben")))) k->set_active(true);
                if (auto h = dynamic_cast<Gtk::ToggleButton *>(finden(win, TR("Fräsbahnen")))) h->set_active(false);
            }
            win->activate_action("hirch-ansicht", Glib::Variant<Glib::ustring>::create("2d"));
            pumpen(0.5);
        });
    }

    // HIRCH_TEST_KENNUNG: 3D-Zeichnung rechnen, dann Unbeteiligtes ändern (schwarze Linie neu,
    // verschieben) – Ergebnis muss bleiben; Haltesteg (id steg1) bzw. Modell (erstes <image> mit
    // hirch:relief) verschieben – muss wieder auf «Rechnen» warten. Meldet «KENNUNGTEST …» auf stderr.
    if (g_getenv("HIRCH_TEST_KENNUNG")) {
        schritt("Kennung 3D", [win] {
            auto p = panel(win);
            auto dt = win->get_desktop();
            if (!p || !dt) throw std::runtime_error("Panel fehlt");
            auto pumpen = [](double s) {
                auto t0 = g_get_monotonic_time();
                while (g_get_monotonic_time() - t0 < s * 1e6) {
                    while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                    g_usleep(20'000);
                }
            };
            auto rechnen = [&] {
                auto t0 = g_get_monotonic_time();
                while (p->beschaeftigt() && g_get_monotonic_time() - t0 < 60'000'000) pumpen(0.05);
                if (p->wartetAufKnopf()) p->jetztRechnen();
                pumpen(0.5);
                while ((p->beschaeftigt() || p->wartetAufKnopf()) && g_get_monotonic_time() - t0 < 300'000'000)
                    pumpen(0.05);
            };
            auto zustand = [p](char const *was) {
                g_printerr("KENNUNGTEST %ld %s beschaeftigt=%d wartet=%d\n", (long)g_get_real_time(), was,
                           (int)p->beschaeftigt(), (int)p->wartetAufKnopf());
            };
            auto doc = dt->getDocument();
            auto wurzel = doc->getReprRoot();
            std::function<Inkscape::XML::Node *(Inkscape::XML::Node *, std::function<bool(Inkscape::XML::Node *)>)> suche =
                [&](Inkscape::XML::Node *n, std::function<bool(Inkscape::XML::Node *)> ok) -> Inkscape::XML::Node * {
                for (auto c = n->firstChild(); c; c = c->next()) {
                    if (ok(c)) return c;
                    if (auto r = suche(c, ok)) return r;
                }
                return nullptr;
            };
            auto modell = suche(wurzel, [](auto n) { return n->attribute("hirch:relief") != nullptr; });
            auto steg = suche(wurzel, [](auto n) { return n->attribute("id") && std::string(n->attribute("id")) == "steg1"; });
            auto text = suche(wurzel, [](auto n) { return n->name() && std::string(n->name()) == "svg:text"; });
            set_active_tool(win, "Select");
            pumpen(1.0);
            rechnen();
            zustand("gerechnet");
            // schwarze Linie neu
            auto linie = doc->getReprDoc()->createElement("svg:path");
            linie->setAttribute("d", "M 5,5 L 15,5");
            linie->setAttribute("style", "fill:none;stroke:#000000;stroke-width:0.3");
            (text ? text->parent() : wurzel)->appendChild(linie);
            DocumentUndo::done(doc, "Test Linie", "");
            pumpen(4.0);
            zustand("linie_neu");
            linie->setAttribute("d", "M 5,8 L 15,8");
            DocumentUndo::done(doc, "Test Linie verschieben", "");
            pumpen(4.0);
            zustand("linie_verschoben");
            if (steg) {
                auto x = steg->getAttributeDouble("x", 0.0);
                steg->setAttributeSvgDouble("x", x + 2.0);
                DocumentUndo::done(doc, "Test Steg", "");
                pumpen(4.0);
                zustand("steg_verschoben");
                steg->setAttributeSvgDouble("x", x);
                DocumentUndo::done(doc, "Test Steg zurück", "");
                pumpen(4.0);
                zustand("steg_zurueck");
                rechnen();
                zustand("gerechnet2");
            }
            if (modell) {
                auto x = modell->getAttributeDouble("x", 0.0);
                modell->setAttributeSvgDouble("x", x + 2.0);
                DocumentUndo::done(doc, "Test Modell", "");
                pumpen(4.0);
                zustand("modell_verschoben");
            }
        });
    }

    if (auto bild = g_getenv("HIRCH_TEST_BILD12")) {
        std::string ziel = bild;
        schritt("Bild Zeichnung", [win, ziel] {
            win->resize(1600, 1000);
            for (int i = 0; i < 20; ++i) {
                while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                g_usleep(50'000);
            }
            win->activate_action("canvas-zoom-page");
            for (int i = 0; i < 60; ++i) {
                while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                g_usleep(50'000);
            }
            if (auto gw = win->get_window())
                if (auto pb = Gdk::Pixbuf::create(gw, 0, 0, gw->get_width(), gw->get_height())) pb->save(ziel, "png");
        });
    }
    // HIRCH_TEST_BILD11: Fräsen-Panel (Leiste «Neu rechnen» unter den Bearbeitungen) als Bild
    if (auto bild = g_getenv("HIRCH_TEST_BILD11")) {
        std::string ziel = bild;
        schritt("Bild Fräsen-Panel", [win, ziel] {
            win->resize(1600, 1000);
            win->activate_action("dialog-open", Glib::Variant<Glib::ustring>::create("Fraesen"));
            // 3D-Zeichnung: erst rechnen (wartet sonst auf den Knopf), höchstens 2 min
            if (auto p = panel(win); p && p->wartetAufKnopf()) {
                p->jetztRechnen();
                auto t0 = g_get_monotonic_time();
                do {
                    while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                    g_usleep(50'000);
                } while ((p->beschaeftigt() || g_get_monotonic_time() - t0 < 1'000'000) &&
                         g_get_monotonic_time() - t0 < 120'000'000);
            }
            for (int i = 0; i < 40; ++i) {
                while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                g_usleep(50'000);
            }
            if (auto gw = win->get_window())
                if (auto pb = Gdk::Pixbuf::create(gw, 0, 0, gw->get_width(), gw->get_height())) pb->save(ziel, "png");
        });
    }
    // die Reiter rechts (Einstellungen; «Werkzeuge» gibt es seit 20.10. nicht mehr)
    for (char const *d : {"HirchAnsicht", "Hirch3D", "Fraesen"}) {
        schritt(std::string("Reiter ") + d, [win, d] {
            win->activate_action("dialog-open", Glib::Variant<Glib::ustring>::create(d));
            Glib::signal_timeout().connect_once([win, d] {
                if (auto dt = win->get_desktop())
                    if (auto c = dt->getContainer())
                        if (auto dlg = c->get_dialog(d)) texte_sammeln(dlg, std::string("Reiter ") + d);
            }, 600);
        });
    }

    // Rechenanzeige (nur mit HIRCH_TEST_BILD9=Ordner): während einer Rechnung Bilder vom Reiter «3D»
    // (Fräsergebnis, Kärtchen an der Leiste) und vom Panel, dann «Abbrechen» und «Neu rechnen»
    if (auto ordner = g_getenv("HIRCH_TEST_BILD9")) {
        std::string o = ordner;
        schritt("Rechenanzeige", [win, o] {
            win->resize(1600, 1000);
            auto warten = [](double s) {
                // höchstens 50 Ereignisse am Stück: Kreisel und Streifen erzeugen laufend neue
                auto t0 = g_get_monotonic_time();
                while (g_get_monotonic_time() - t0 < s * 1e6) {
                    for (int i = 0; i < 50 && Gtk::Main::events_pending(); ++i) Gtk::Main::iteration(false);
                    g_usleep(30'000);
                }
            };
            auto bild = [win](std::string const &pfad) {
                if (auto gw = win->get_window())
                    if (auto pb = Gdk::Pixbuf::create(gw, 0, 0, gw->get_width(), gw->get_height()))
                        pb->save(pfad, "png");
            };
            auto dt = win->get_desktop();
            auto c = dt ? dt->getContainer() : nullptr;
            win->activate_action("dialog-open", Glib::Variant<Glib::ustring>::create("Hirch3D"));
            if (auto d3 = c ? dynamic_cast<Hirch3D *>(c->get_dialog("Hirch3D")) : nullptr) d3->fraesergebnisZeigen(true);
            auto p = panel(win);
            if (!p) throw std::runtime_error("Fräsen-Panel fehlt");
            warten(1.0);
            // zuerst das Panel, dann der Reiter «3D» (ohne Grafikkarte zeichnet die Software das
            // Fräsergebnis-Netz langsam – die Uhr darüber hält die Oberfläche dann lange auf)
            win->activate_action("dialog-open", Glib::Variant<Glib::ustring>::create("Fraesen"));
            warten(0.3);
            // 3D-Zeichnung: wartet auf «Rechnen» (Panel, Reiter «3D», Knopf an der Leiste)
            p->neuRechnen();
            warten(2.0);
            bild(o + "/wartet_panel.png");
            win->activate_action("dialog-open", Glib::Variant<Glib::ustring>::create("Hirch3D"));
            warten(0.5);
            bild(o + "/wartet_3d.png");
            win->activate_action("dialog-open", Glib::Variant<Glib::ustring>::create("Fraesen"));
            warten(0.3);
            p->jetztRechnen();
            warten(1.5);
            bild(o + "/rechnet_panel.png");
            win->activate_action("dialog-open", Glib::Variant<Glib::ustring>::create("Hirch3D"));
            warten(0.3);
            bild(o + "/rechnet_3d.png");
            win->activate_action("dialog-open", Glib::Variant<Glib::ustring>::create("Fraesen"));
            warten(0.3);
            p->rechnenAbbrechen();
            warten(0.8);
            bild(o + "/abgebrochen.png");
            p->jetztRechnen();
            warten(0.5);
            auto t0 = g_get_monotonic_time();
            while (p->beschaeftigt() && g_get_monotonic_time() - t0 < 120'000'000) warten(0.2);
            warten(1.0);
            bild(o + "/fertig.png");
        });
    }

    // Reiter «3D» › «Fräsergebnis»: umschalten, die Simulation abwarten, Texte lesen;
    // HIRCH_TEST_BILD8: Bildschirmfoto des Fensters (Fensterbreite 1600 × 1000)
    schritt("3D Fräsergebnis", [win] {
        if (g_getenv("HIRCH_TEST_BILD8") || g_getenv("HIRCH_TEST_BILD10") || g_getenv("HIRCH_TEST_KLICK") ||
            g_getenv("HIRCH_TEST_FLAECHE"))
            win->resize(1600, 1000);
        win->activate_action("dialog-open", Glib::Variant<Glib::ustring>::create("Hirch3D"));
        auto hol = [win]() -> Hirch3D * {
            auto dt = win->get_desktop();
            auto c = dt ? dt->getContainer() : nullptr;
            return c ? dynamic_cast<Hirch3D *>(c->get_dialog("Hirch3D")) : nullptr;
        };
        auto d = hol();
        if (!d) throw std::runtime_error("Reiter 3D fehlt");
        // 3D-Zeichnung rechnet auf Knopfdruck: «Rechnen» und die Rechnung abwarten
        if (auto p = panel(win)) {
            auto t0 = g_get_monotonic_time();
            while (p->beschaeftigt() && g_get_monotonic_time() - t0 < 60'000'000) {
                while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                g_usleep(50'000);
            }
            if (p->wartetAufKnopf()) p->jetztRechnen();
            while (g_get_monotonic_time() - t0 < 180'000'000) {
                while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                g_usleep(50'000);
                if (!p->beschaeftigt() && !p->wartetAufKnopf()) break;
            }
        }
        d = hol();
        if (!d) throw std::runtime_error("Reiter 3D fehlt");
        d->fraesergebnisZeigen(true);
        auto start = g_get_monotonic_time();
        while (g_get_monotonic_time() - start < 120'000'000) {
            while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
            g_usleep(50'000);
            if (auto d2 = hol(); d2 && !d2->beschaeftigt() && g_get_monotonic_time() - start > 2'000'000) break;
        }
        for (int i = 0; i < 20; ++i) {
            while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
            g_usleep(50'000);
        }
        if (auto d2 = hol()) texte_sammeln(d2, "Reiter Hirch3D › Fräsergebnis");
        if (auto bild = g_getenv("HIRCH_TEST_BILD8"))
            if (auto gw = win->get_window())
                if (auto pb = Gdk::Pixbuf::create(gw, 0, 0, gw->get_width(), gw->get_height()))
                    pb->save(bild, "png");
        // HIRCH_TEST_KLICK=Bild: Klick in die Mitte des Fräsergebnisses, Bild des Fensters;
        // HIRCH_TEST_KLICK_SVG=Datei: dann «Restmaterial hier nachfräsen» (gibt es den Knopf) und speichern
        if (auto bild = g_getenv("HIRCH_TEST_KLICK"))
            if (auto d2 = hol()) {
                d2->klickTesten(0.5, 0.5, false);
                for (int i = 0; i < 10; ++i) {
                    while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                    g_usleep(20'000);
                }
                if (auto gw = win->get_window())
                    if (auto pb = Gdk::Pixbuf::create(gw, 0, 0, gw->get_width(), gw->get_height())) pb->save(bild, "png");
                if (auto ziel = g_getenv("HIRCH_TEST_KLICK_SVG")) {
                    // eine Stelle mit Restmaterial suchen und dort anlegen
                    bool ok = false;
                    for (int j = 1; j < 20 && !ok; ++j)
                        for (int i = 1; i < 20 && !ok; ++i) ok = d2->klickTesten(i / 20.0, j / 20.0, true);
                    for (int i = 0; i < 20; ++i) {
                        while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                        g_usleep(30'000);
                    }
                    if (auto doc = win->get_document()) sp_repr_save_file(doc->getReprDoc(), ziel, SP_SVG_NS_URI);
                }
            }
        // HIRCH_TEST_BILD10: Reiter «3D» bis zum Abschnitt «Zweiseitig» (Spalt, Haut, Stege) scrollen
        if (auto bild = g_getenv("HIRCH_TEST_BILD10"))
            if (auto d2 = hol()) {
                Gtk::Widget *ziel = nullptr;
                Gtk::ScrolledWindow *sw = nullptr;
                std::function<void(Gtk::Widget *)> suchen = [&](Gtk::Widget *w) {
                    if (auto l = dynamic_cast<Gtk::Label *>(w); l && !ziel && l->get_text() == TR("Spalt um das Modell"))
                        ziel = l;
                    if (auto c = dynamic_cast<Gtk::Container *>(w))
                        for (auto k : c->get_children()) suchen(k);
                };
                suchen(d2);
                for (auto p_ = ziel ? ziel->get_parent() : nullptr; p_ && !sw; p_ = p_->get_parent())
                    sw = dynamic_cast<Gtk::ScrolledWindow *>(p_);
                win->activate_action("canvas-zoom-drawing");      // beide Seiten der Zeichnung im Bild
                if (auto ziel_svg = g_getenv("HIRCH_TEST_SVG3D"))
                    if (auto doc = win->get_document()) sp_repr_save_file(doc->getReprDoc(), ziel_svg, SP_SVG_NS_URI);
                if (ziel && sw && sw->get_child()) {
                    int x = 0, y = 0;
                    ziel->translate_coordinates(*sw->get_child(), 0, 0, x, y);
                    sw->get_vadjustment()->set_value(std::max(0, y - 260));
                }
                for (int i = 0; i < 20; ++i) {
                    while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                    g_usleep(50'000);
                }
                if (auto gw = win->get_window())
                    if (auto pb = Gdk::Pixbuf::create(gw, 0, 0, gw->get_width(), gw->get_height()))
                        pb->save(bild, "png");
            }
        if (auto d2 = hol()) d2->fraesergebnisZeigen(false);
        // HIRCH_TEST_FLAECHE=Bild: Klick auf das Modell (Mitte der Ansicht), Umriss abwarten, Bild
        // HIRCH_TEST_FLAECHE_UMSCHALT=fx,fy: danach Umschalt+Klick dort (zweite Fläche dazu);
        // HIRCH_TEST_FLAECHE_BEREICH=Bild: «Gewählte Flächen als Bereich», Bild danach; meldet
        // «FLAECHENTEST n=…» auf stderr; HIRCH_TEST_FLAECHE_SVG speichert die Zeichnung danach
        if (auto bild = g_getenv("HIRCH_TEST_FLAECHE"))
            if (auto d2 = hol()) {
                auto pumpen = [](double s) {
                    auto t0 = g_get_monotonic_time();
                    while (g_get_monotonic_time() - t0 < s * 1e6) {
                        while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                        g_usleep(50'000);
                    }
                };
                auto foto = [win](std::string const &pfad) {
                    if (auto gw = win->get_window())
                        if (auto pb = Gdk::Pixbuf::create(gw, 0, 0, gw->get_width(), gw->get_height())) pb->save(pfad, "png");
                };
                d2->modellKlickTesten(0.5, 0.5);
                pumpen(8.0);
                g_printerr("FLAECHENTEST n=%d nach Klick\n", d2->flaechenGewaehlt());
                if (auto u = g_getenv("HIRCH_TEST_FLAECHE_UMSCHALT")) {
                    double fx = 0.4, fy = 0.6;
                    std::sscanf(u, "%lf,%lf", &fx, &fy);
                    d2->modellKlickTesten(fx, fy, true);
                    pumpen(8.0);
                    g_printerr("FLAECHENTEST n=%d nach Umschalt+Klick\n", d2->flaechenGewaehlt());
                }
                foto(bild);
                if (auto b2 = g_getenv("HIRCH_TEST_FLAECHE_BEREICH")) {
                    d2->flaechenAlsBereichTesten();
                    pumpen(8.0);
                    foto(b2);
                    if (auto ziel = g_getenv("HIRCH_TEST_FLAECHE_SVG"))
                        if (auto doc = win->get_document()) sp_repr_save_file(doc->getReprDoc(), ziel, SP_SVG_NS_URI);
                }
            }
        // HIRCH_TEST_ANSICHT3D_BILD=Bild: die 3D-Ansicht fürs Hauptfenster (hirch_ansicht_3d_neu) in einem eigenen
        // Fenster 900 × 600 – zeigt dieselbe Szene wie der Reiter «3D»
        if (auto bild = g_getenv("HIRCH_TEST_ANSICHT3D_BILD")) {
            lauf().halten = true;              // das Fenster nicht gleich wieder schliessen
            auto fenster = new Gtk::Window();
            fenster->set_default_size(900, 600);
            fenster->add(*hirch_ansicht_3d_neu(win->get_desktop()));
            fenster->show_all();
            auto t0 = g_get_monotonic_time();
            while (g_get_monotonic_time() - t0 < 3'000'000) {
                while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                g_usleep(50'000);
            }
            if (auto gw = fenster->get_window())
                if (auto pb = Gdk::Pixbuf::create(gw, 0, 0, gw->get_width(), gw->get_height())) pb->save(bild, "png");
            else g_printerr("ANSICHT3D: kein Fenster\n");
            delete fenster;
            lauf().halten = false;
        }
    });

    // 3D: mit HIRCH_TEST_3D=Datei ein Modell einlesen und den Reiter lesen; mit
    // HIRCH_TEST_BILD=bild.png danach ein Bildschirmfoto des Fensters
    if (auto datei = g_getenv("HIRCH_TEST_3D")) {
        std::string pfad = datei;
        schritt("3D einlesen", [win, pfad] {
            if (g_getenv("HIRCH_TEST_BILD")) win->resize(1600, 1000);
            win->activate_action("dialog-open", Glib::Variant<Glib::ustring>::create("Hirch3D"));
            auto dt = win->get_desktop();
            auto c = dt ? dt->getContainer() : nullptr;
            auto d = c ? dynamic_cast<Hirch3D *>(c->get_dialog("Hirch3D")) : nullptr;
            if (!d) throw std::runtime_error("Reiter 3D fehlt");
            d->einlesenDatei(pfad);
        });
        schritt("3D warten", [win] {
            auto start = g_get_monotonic_time();
            while (g_get_monotonic_time() - start < 30'000'000) {
                auto dt = win->get_desktop();
                auto c = dt ? dt->getContainer() : nullptr;
                auto d = c ? dynamic_cast<Hirch3D *>(c->get_dialog("Hirch3D")) : nullptr;
                if (d && !d->beschaeftigt() && g_get_monotonic_time() - start > 3'000'000) {
                    texte_sammeln(d, "Reiter 3D");
                    // Zeichnung nach dem Einlesen (angelegte 3D-Bearbeitungen und Werkzeuge prüfen)
                    if (auto ziel = g_getenv("HIRCH_TEST_SVG3D"))
                        if (auto doc = win->get_document()) sp_repr_save_file(doc->getReprDoc(), ziel, SP_SVG_NS_URI);
                    break;
                }
                while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                g_usleep(50'000);
            }
            for (int i = 0; i < 20; ++i) {
                while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                g_usleep(50'000);
            }
            if (auto bild = g_getenv("HIRCH_TEST_BILD")) {
                win->activate_action("canvas-zoom-page");
                for (int i = 0; i < 10; ++i) {
                    while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                    g_usleep(50'000);
                }
                auto gw = win->get_window();
                if (gw) {
                    auto pb = Gdk::Pixbuf::create(gw, 0, 0, gw->get_width(), gw->get_height());
                    if (pb) pb->save(bild, "png");
                }
            }
        });
    }

    // 3D: Ausrichtung im Reiter ändern (Unterseite nach oben) – prüft das Neuberechnen und
    // Ersetzen des Bildes; mit HIRCH_TEST_BILD2 ein zweites Bildschirmfoto
    if (g_getenv("HIRCH_TEST_3D")) {
        schritt("3D umdrehen", [win] {
            auto dt = win->get_desktop();
            auto c = dt ? dt->getContainer() : nullptr;
            auto d = c ? dynamic_cast<Hirch3D *>(c->get_dialog("Hirch3D")) : nullptr;
            if (!d) throw std::runtime_error("Reiter 3D fehlt");
            std::function<Gtk::ComboBoxText *(Gtk::Widget *)> suchen = [&](Gtk::Widget *w) -> Gtk::ComboBoxText * {
                if (auto cb = dynamic_cast<Gtk::ComboBoxText *>(w); cb && cb->get_active_id() == "+Z") return cb;
                if (auto ct = dynamic_cast<Gtk::Container *>(w))
                    for (auto k : ct->get_children())
                        if (auto r = suchen(k)) return r;
                return nullptr;
            };
            auto cb = suchen(d);
            if (!cb) throw std::runtime_error("Auswahl «Seite oben» fehlt");
            cb->set_active_id("-Z");
            auto start = g_get_monotonic_time();
            while (g_get_monotonic_time() - start < 30'000'000) {
                while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                g_usleep(50'000);
                if (!d->beschaeftigt() && g_get_monotonic_time() - start > 2'500'000) break;
            }
            for (int i = 0; i < 20; ++i) {
                while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                g_usleep(50'000);
            }
            if (auto bild = g_getenv("HIRCH_TEST_BILD2")) {
                auto gw = win->get_window();
                if (gw) {
                    auto pb = Gdk::Pixbuf::create(gw, 0, 0, gw->get_width(), gw->get_height());
                    if (pb) pb->save(bild, "png");
                }
            }
        });
    }

    // 3D zweiseitig: «Von beiden Seiten fräsen» einschalten – legt Seite 2, die Bearbeitungen
    // für Seite 2 und vier Haltestege an; mit HIRCH_TEST_BILD3 ein Bildschirmfoto
    if (g_getenv("HIRCH_TEST_3D")) {
        schritt("3D zweiseitig", [win] {
            auto dt = win->get_desktop();
            auto c = dt ? dt->getContainer() : nullptr;
            auto d = c ? dynamic_cast<Hirch3D *>(c->get_dialog("Hirch3D")) : nullptr;
            if (!d) throw std::runtime_error("Reiter 3D fehlt");
            std::string text = TR("Von beiden Seiten fräsen (Seite 2 nach dem Wenden)");
            std::function<Gtk::CheckButton *(Gtk::Widget *)> suchen = [&](Gtk::Widget *w) -> Gtk::CheckButton * {
                if (auto cb = dynamic_cast<Gtk::CheckButton *>(w); cb && cb->get_label() == text) return cb;
                if (auto ct = dynamic_cast<Gtk::Container *>(w))
                    for (auto k : ct->get_children())
                        if (auto r = suchen(k)) return r;
                return nullptr;
            };
            auto cb = suchen(d);
            if (!cb) throw std::runtime_error("Schalter «Von beiden Seiten fräsen» fehlt");
            cb->set_active(true);
            auto start = g_get_monotonic_time();
            while (g_get_monotonic_time() - start < 40'000'000) {
                while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                g_usleep(50'000);
                if (!d->beschaeftigt() && g_get_monotonic_time() - start > 4'000'000) break;
            }
            for (int i = 0; i < 20; ++i) {
                while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                g_usleep(50'000);
            }
            texte_sammeln(d, "Reiter 3D zweiseitig");
            if (auto bild = g_getenv("HIRCH_TEST_BILD3")) {
                // den Reiter nach unten rollen (Zweiseitig, Haltestege)
                std::function<void(Gtk::Widget *)> rollen = [&](Gtk::Widget *w) {
                    if (auto sw = dynamic_cast<Gtk::ScrolledWindow *>(w)) {
                        auto a = sw->get_vadjustment();
                        a->set_value(a->get_upper() - a->get_page_size());
                    }
                    if (auto ct = dynamic_cast<Gtk::Container *>(w))
                        for (auto k : ct->get_children()) rollen(k);
                };
                if (auto oben = g_getenv("HIRCH_TEST_BILD4")) {     // oben: 3D-Ansicht mit Stegen
                    std::function<void(Gtk::Widget *)> hoch = [&](Gtk::Widget *w) {
                        if (auto sw = dynamic_cast<Gtk::ScrolledWindow *>(w)) sw->get_vadjustment()->set_value(0);
                        if (auto ct = dynamic_cast<Gtk::Container *>(w))
                            for (auto k : ct->get_children()) hoch(k);
                    };
                    hoch(d);
                    for (int i = 0; i < 20; ++i) {
                        while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                        g_usleep(50'000);
                    }
                    if (auto gw = win->get_window())
                        if (auto pb = Gdk::Pixbuf::create(gw, 0, 0, gw->get_width(), gw->get_height()))
                            pb->save(oben, "png");
                }
                rollen(d);
                win->activate_action("canvas-zoom-page");
                for (int i = 0; i < 20; ++i) {
                    while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                    g_usleep(50'000);
                }
                auto gw = win->get_window();
                if (gw) {
                    auto pb = Gdk::Pixbuf::create(gw, 0, 0, gw->get_width(), gw->get_height());
                    if (pb) pb->save(bild, "png");
                }
            }
        });
    }

    // 3D: Vektoren aus dem Modell – jeden sichtbaren Knopf «In die Zeichnung» drücken; mit
    // HIRCH_TEST_SVG danach die Zeichnung speichern (zum Nachrechnen)
    if (g_getenv("HIRCH_TEST_3D")) {
        schritt("3D Vektoren", [win] {
            auto dt = win->get_desktop();
            auto c = dt ? dt->getContainer() : nullptr;
            auto d = c ? dynamic_cast<Hirch3D *>(c->get_dialog("Hirch3D")) : nullptr;
            if (!d) throw std::runtime_error("Reiter 3D fehlt");
            std::string text = TR("In die Zeichnung");
            std::vector<Gtk::Button *> knoepfe;
            std::function<void(Gtk::Widget *)> suchen = [&](Gtk::Widget *w) {
                if (auto b = dynamic_cast<Gtk::Button *>(w); b && b->get_label() == text && b->is_visible())
                    knoepfe.push_back(b);
                if (auto ct = dynamic_cast<Gtk::Container *>(w))
                    for (auto k : ct->get_children()) suchen(k);
            };
            suchen(d);
            if (knoepfe.empty()) throw std::runtime_error("Knöpfe «In die Zeichnung» fehlen");
            for (auto b : knoepfe) {
                b->clicked();
                auto start = g_get_monotonic_time();
                while (g_get_monotonic_time() - start < 3'000'000) {
                    while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                    g_usleep(50'000);
                }
            }
            if (auto ziel = g_getenv("HIRCH_TEST_SVG"))
                if (auto doc = win->get_document()) sp_repr_save_file(doc->getReprDoc(), ziel, SP_SVG_NS_URI);
        });
        // STEP: «Bohrungen und Taschen › Erkennen …» (der Dialog legt im Test gleich an, mit
        // HIRCH_TEST_MERKMALE_ZEIGEN wird er nur gezeigt); HIRCH_TEST_MERKMALE_SVG: danach die Zeichnung
        schritt("3D Bohrungen und Taschen", [win] {
            auto dt = win->get_desktop();
            auto c = dt ? dt->getContainer() : nullptr;
            auto d = c ? dynamic_cast<Hirch3D *>(c->get_dialog("Hirch3D")) : nullptr;
            if (!d) throw std::runtime_error("Reiter 3D fehlt");
            Gtk::Button *knopf = nullptr;
            std::string text = TR("Erkennen …");
            std::function<void(Gtk::Widget *)> suchen = [&](Gtk::Widget *w) {
                if (auto b = dynamic_cast<Gtk::Button *>(w); b && b->get_label() == text && b->is_visible()) knopf = b;
                if (auto ct = dynamic_cast<Gtk::Container *>(w))
                    for (auto k : ct->get_children()) suchen(k);
            };
            suchen(d);
            if (!knopf) return;                               // kein STEP-Modell
            knopf->clicked();
            // warten, bis die Gruppe mit den Kreisen und Umrissen in der Zeichnung ist (höchstens 90 s)
            auto da = [win] {
                auto doc = win->get_document();
                if (!doc) return false;
                std::string t = TR("Bohrungen und Taschen");
                std::function<bool(Inkscape::XML::Node *)> suchen = [&](Inkscape::XML::Node *n) {
                    for (auto c = n->firstChild(); c; c = c->next()) {
                        if (auto l = c->attribute("inkscape:label"); l && std::string(l).rfind(t, 0) == 0) return true;
                        if (suchen(c)) return true;
                    }
                    return false;
                };
                return suchen(doc->getReprRoot());
            };
            auto start = g_get_monotonic_time();
            auto bis = g_getenv("HIRCH_TEST_MERKMALE_ZEIGEN") ? 20'000'000 : 90'000'000;
            while (!da() && g_get_monotonic_time() - start < bis) {
                while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
                g_usleep(50'000);
            }
            if (!da() && !g_getenv("HIRCH_TEST_MERKMALE_ZEIGEN"))
                throw std::runtime_error("keine Bohrungen und Taschen angelegt");
            if (auto ziel = g_getenv("HIRCH_TEST_MERKMALE_SVG"))
                if (auto doc = win->get_document()) sp_repr_save_file(doc->getReprDoc(), ziel, SP_SVG_NS_URI);
        });
    }

    // warten, bis die Zeichnung gerechnet ist (höchstens 60 s)
    auto start = g_get_monotonic_time();
    Glib::signal_timeout().connect([win, start] {
        auto p = panel(win);
        bool fertig = p && !p->beschaeftigt();
        if (!fertig && g_get_monotonic_time() - start < 60'000'000) return true;
        weiter();
        return false;
    }, 500);
}

} // namespace Inkscape::UI::Dialog
