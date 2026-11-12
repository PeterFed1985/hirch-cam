// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: 3D-Fräsung – Reiter «3D» (Hirch3D::Impl) und seine Anschlüsse (siehe hirch-3d.h; Aufteilung der
 * übrigen Teile: hirch-3d-intern.h)
 */
#include "ui/dialog/hirch-3d-intern.h"
#include "ui/dialog/hirch-abtrag.h"

#include <sstream>

namespace Inkscape::UI::Dialog {

/// Peter 27.10.: «Zweiseitig» im Auftrag und «Von beiden Seiten fräsen» im Reiter 3D gehören zusammen –
/// ist die Zeichnung einseitig, ist es auch jedes Modell (ohne eigenen Rückgängig-Schritt)
void reliefs_einseitig(SPDocument *d)
{
    for (auto it : reliefs(d)) {
        auto info = relief_info(it);
        auto z = unter(info, "zweiseitig");
        if (!z || !flag(*z, "an", false)) continue;
        bj::object zw = *z;
        zw["an"] = false;
        info["zweiseitig"] = zw;
        it->getRepr()->setAttribute(RELIEF, bj::serialize(info));
    }
}

/// Modell für «Haltestege setzen»: das gewählte, wenn es von beiden Seiten gefräst wird; ohne Wahl
/// (wahl = nullptr) das erste solche Modell der Zeichnung
SPItem *relief_fuer_stege(SPDocument *d, SPItem *wahl)
{
    for (auto it : reliefs(d)) {
        if (wahl && it != wahl) continue;
        auto info = relief_info(it);
        if (auto z = unter(info, "zweiseitig"); z && flag(*z, "an", false)) return it;
    }
    return nullptr;
}

/// Ist ein anderes Modell als «ausser» von beiden Seiten zu fräsen?
static bool anderes_relief_zweiseitig(SPDocument *d, SPItem const *ausser)
{
    for (auto it : reliefs(d)) {
        if (it == ausser) continue;
        auto info = relief_info(it);
        if (auto z = unter(info, "zweiseitig"); z && flag(*z, "an", false)) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Reiter «3D»

struct Hirch3D::Impl {
    Hirch3D &panel;
    std::shared_ptr<Szene3D> szene = std::make_shared<Szene3D>();
    Ansicht ansicht{szene};
    std::string ordner = neuer_ordner();
    std::string daten_datei;
    std::string item_id, attr_alt;
    std::string unten_uri;      // Unterseite des gewählten Modells (PNG) für Seite 2 der Zeichnung
    bj::object info;
    std::shared_ptr<bool> lauf, lauf_einlesen;
    unsigned gen = 0;
    bool setzt = false, rechnet = false;
    sigc::connection verzoegert;

    Gtk::Box leer{Gtk::ORIENTATION_VERTICAL, 10}, voll{Gtk::ORIENTATION_VERTICAL, 10};
    Gtk::Label name, details, original, status, warn, rand_l;
    Gtk::Label angelegt;     // nach dem Einlesen: welche 3D-Bearbeitungen mit welchen Werkzeugen angelegt
    Gtk::Button aktualisieren;
    Gtk::ComboBoxText lage, oben;
    Gtk::SpinButton *tiefe = nullptr, *zfaktor = nullptr, *neig_x = nullptr, *neig_y = nullptr, *weiss = nullptr;
    Gtk::Grid *g_lage = nullptr, *g_ausr = nullptr;

    // zweiseitig: Trennfläche und Haltestege
    Gtk::CheckButton zwei_an;
    Gtk::Box zwei_box{Gtk::ORIENTATION_VERTICAL, 8}, steg_liste{Gtk::ORIENTATION_VERTICAL, 4};
    Gtk::ComboBoxText achse, trennung;
    double trenn_mitte = NAN;               // Mitte der erkannten Trennlinie (Vorschlag für «auf einer Ebene»)
    Gtk::SpinButton *ebene_z = nullptr, *steg_b = nullptr, *steg_d = nullptr, *steg_n = nullptr;
    Gtk::Widget *steg_titel = nullptr;           // «Haltestege setzen» aus der Leiste: bis hierher rollen
    // Spalt um das Modell: durchfräsen oder eine Haut an der Trennlinie stehen lassen
    Gtk::ToggleButton spalt_durch, spalt_haut;
    Gtk::SpinButton *haut_d = nullptr;
    // über die Trennlinie hinaus fräsen (Peter 30.10.): automatisch nach dem Schlichtfräser oder je Seite von Hand
    Gtk::CheckButton ueber_auto;
    Gtk::SpinButton *ueber_1 = nullptr, *ueber_2 = nullptr;
    Gtk::Grid ueber_versteckt;                    // nicht gezeigt (steht in der Bearbeitung)
    Gtk::Label haut_hinweis;
    Gtk::Label trenn_l, steg_hinweis;
    std::shared_ptr<bool> lauf_stege;
    unsigned gen_stege = 0;
    sigc::connection stege_spaeter;
    std::map<std::string, double> z_auto;      // Steg-id → automatische Höhe (zuletzt gerechnet)
    std::vector<std::string> zeilen_ids;       // Stege in der Liste (gleich: nur Werte nachführen)
    std::map<std::string, std::pair<Gtk::SpinButton *, Gtk::CheckButton *>> zeilen;
    bool steg_setzt = false, stege_laeuft = false;

    // Vektoren aus dem Modell
    Gtk::SpinButton *hl_z = nullptr;
    Gtk::Box *kanten_zeile = nullptr, *merkmal_zeile = nullptr;
    Gtk::Label vek_hinweis;
    // Fenster «3D-Modell» (V1, Peter 06.11.): Abschnitte zum Aufklappen, Zustand gemerkt
    Gtk::Box ans_box{Gtk::ORIENTATION_VERTICAL, 10};   // kleine 3D-Ansicht und Flächenwahl (zieht in die grosse um)
    Gtk::Widget *x_stege = nullptr;
    std::function<void()> x_stege_auf;
    Gtk::Label *x_stege_l = nullptr;
    std::shared_ptr<bool> lauf_vek;

    // Fräsergebnis (Simulation der letzten Rechnung)
    Gtk::ToggleButton t_modell, t_ergebnis;
    Gtk::ComboBoxText genau;
    Gtk::CheckButton entwurf;
    // Rechenanzeige über der 3D-Ansicht (Fräsergebnis): Panel rechnet oder Simulation läuft
    Gtk::Box dunkel{Gtk::ORIENTATION_VERTICAL, 0}, pille{Gtk::ORIENTATION_HORIZONTAL, 10};
    Gtk::Label pille_text;
    std::string pille_alt;
    Gtk::Button pille_knopf;
    bool panel_rechnet = false;
    bool panel_wartet = false;         // 3D-Zeichnung noch nicht gerechnet: «Rechnen» über der Ansicht
    std::string panel_text, wartet_text;
    gint64 sim_start = 0;
    sigc::connection sim_takt;
    Gtk::Label erg_status;
    Gtk::Box klick_box{Gtk::ORIENTATION_VERTICAL, 6};
    Gtk::Label klick_text;
    Gtk::Button klick_rest, klick_umriss;
    bj::array klick_linien;
    std::shared_ptr<bool> lauf_flaeche;
    double klick_x = 0, klick_y = 0;
    // Flächen wählen (Peter, 22.10.): Klick wählt eine Fläche, Umschalt+Klick nimmt weitere dazu/weg
    struct GewFlaeche {
        std::string id, name;
        int seite = 1;
        bj::array umriss, linien;      // Umriss: Material-mm Seite 1 (Ansicht); Linien: wie die Zeichnung
        double flaeche = 0, x = 0, y = 0, z = 0;
        bool wand = false;             // senkrechte Wand (STEP): in der Ansicht nur steile Dreiecke blau
    };
    std::vector<GewFlaeche> fl_wahl;
    int fl_seite = 1;
    unsigned fl_gen = 0;
    Gtk::Box fl_box{Gtk::ORIENTATION_VERTICAL, 6}, fl_liste{Gtk::ORIENTATION_VERTICAL, 4};
    Gtk::Label fl_kopf, fl_text;
    Gtk::Box *fl_winkel_zeile = nullptr;
    Gtk::SpinButton *fl_winkel = nullptr;
    Gtk::ComboBoxText fl_ziel, fl_art;
    Gtk::Button fl_leeren, fl_umriss, fl_bereich;
    Gtk::Label *legende_l = nullptr;
    std::string rechnung;              // {svg, ok, programme, kollisionen} vom Panel
    bool erg_alt = true, erg_laeuft = false, schaltet = false;
    std::shared_ptr<bool> lauf_erg;
    unsigned gen_erg = 0;

    explicit Impl(Hirch3D &p) : panel(p)
    {
        css_einrichten();
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 10);
        klasse(*box, "hirch-panel");
        box->set_margin_top(8);

        warn.set_xalign(0);
        warn.set_line_wrap(true);
        warn.set_max_width_chars(44);
        warn.set_no_show_all(true);
        box->add(warn);
        angelegt.set_xalign(0);
        angelegt.set_line_wrap(true);
        angelegt.set_max_width_chars(44);
        angelegt.set_no_show_all(true);
        box->add(angelegt);

        // ohne Modell
        auto lt = label(TR("Kein 3D-Modell in der Zeichnung."));
        leer.add(*lt);
        auto lh = label(TR("Ein 3D-Modell (STL, OBJ, STEP) oder ein Graustufenbild einlesen. Es liegt dann grau "
                           "schattiert auf der Zeichnung; hier stellst du Höhe und Ausrichtung ein."), "hirch-dim");
        lh->set_line_wrap(true);
        lh->set_max_width_chars(40);
        leer.add(*lh);
        auto el = Gtk::make_managed<Gtk::Button>(TR("3D-Modell einlesen …"));
        el->set_halign(Gtk::ALIGN_START);
        el->signal_clicked().connect([this] { panel.einlesen(); });
        leer.add(*el);
        if (!hirch_testteil().empty()) {
            // Testteil: über «Neue Zeichnung einrichten», damit Material und Rand gleich passen
            auto tt = Gtk::make_managed<Gtk::Button>(TR("Testteil laden …"));
            tt->set_halign(Gtk::ALIGN_START);
            tt->set_tooltip_text(TR("Testteil für das Probefräsen in Hartschaum (180 × 180 × 39 mm, beide Seiten): "
                                    "Kuppel, Block, Treppe, Rinne, Taschen und Bohrungen."));
            tt->signal_clicked().connect([this] {
                if (auto fr = fraesen()) fr->anfangsdialog(hirch_testteil());
            });
            leer.add(*tt);
        }
        box->add(leer);

        // mit Modell
        // keine Ansichtsknöpfe (Peter, 19.10.): Ziehen dreht, Rad zoomt, Doppelklick stellt zurück
        // Modell oder Fräsergebnis (Simulation der letzten Rechnung)
        auto erg_kopf = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        auto umschalter = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
        klasse(*umschalter, "linked");
        t_modell.set_label(TR("Modell"));
        t_ergebnis.set_label(TR("Fräsergebnis"));
        t_ergebnis.set_tooltip_text(TR("Was nach allen Programmen der letzten Rechnung vom Material übrig bleibt"));
        t_modell.set_active(true);
        umschalter->add(t_modell);
        umschalter->add(t_ergebnis);
        erg_kopf->add(*umschalter);
        auto gl = label(TR("Genauigkeit"), "hirch-dim");
        gl->set_margin_start(8);
        erg_kopf->add(*gl);
        genau.append("grob", TR("grob"));
        genau.append("mittel", TR("mittel"));
        genau.append("fein", TR("fein"));
        genau.set_tooltip_text(TR("Rasterweite der Simulation: grob rechnet am schnellsten, fein zeigt kleine Stellen"));
        {
            auto g = Inkscape::Preferences::get()->getString("/hirch/3d/genauigkeit", "mittel");
            if (!genau.set_active_id(g.empty() ? "mittel" : g)) genau.set_active_id("mittel");
        }
        erg_kopf->add(genau);
        ans_box.add(*erg_kopf);
        // Entwurfsmodus: 3D-Bahnen gröber und schneller rechnen, solange man einstellt
        entwurf.set_label(TR("Entwurf: 3D-Bahnen schneller und gröber rechnen"));
        entwurf.set_tooltip_text(TR("Zum Einstellen: Schlichten mit dreifachem Bahnabstand und dreifacher Abtastung. "
                                    "Gespeichert und auf den USB-Stick geschrieben wird immer genau gerechnet."));
        entwurf.set_active(Inkscape::Preferences::get()->getBool("/hirch/3d/entwurf", true));
        entwurf.signal_toggled().connect([this] {
            Inkscape::Preferences::get()->setBool("/hirch/3d/entwurf", entwurf.get_active());
            if (auto f = fraesen()) f->neuRechnen();
        });
        t_modell.signal_toggled().connect([this] { modus_setzen(!t_modell.get_active()); });
        t_ergebnis.signal_toggled().connect([this] { modus_setzen(t_ergebnis.get_active()); });
        genau.signal_changed().connect([this] {
            Inkscape::Preferences::get()->setString("/hirch/3d/genauigkeit", genau.get_active_id());
            erg_alt = true;
            if (t_ergebnis.get_active()) ergebnis_rechnen();
        });
        auto rahmen = Gtk::make_managed<Gtk::Frame>();
        auto ueber = Gtk::make_managed<Gtk::Overlay>();
        ueber->add(ansicht.bereich);
        klasse(dunkel, "hirch-3d-dunkel");
        klasse(pille, "hirch-3d-pille");
        pille.set_halign(Gtk::ALIGN_CENTER);
        pille.set_valign(Gtk::ALIGN_CENTER);
        pille.set_vexpand(true);
        pille.add(pille_text);
        pille_knopf.set_label(TR("Abbrechen"));
        pille_knopf.signal_clicked().connect([this] {
            if (erg_laeuft) {                       // eigene Simulation abbrechen
                ++gen_erg;
                erg_laeuft = false;
                erg_alt = true;
                erg_status.set_text(TR("Simulation abgebrochen."));
                pille_zeigen();
            } else if (auto f = fraesen()) {
                if (panel_wartet && !panel_rechnet) f->jetztRechnen();
                else f->rechnenAbbrechen();
            }
        });
        pille.add(pille_knopf);
        dunkel.add(pille);
        dunkel.set_no_show_all(true);
        ueber->add_overlay(dunkel);
        rahmen->add(*ueber);
        ans_box.add(*rahmen);
        auto legende = label(TR("Grau: Modell · gelb: Haltestege · blau: Werkstück · rot: ragt heraus oder wird unten abgeschnitten · "
                                "Ziehen dreht, Rad zoomt, Doppelklick stellt zurück"), "hirch-dim");
        klasse(*legende, "hirch-klein");
        legende->set_line_wrap(true);
        legende->set_max_width_chars(44);
        ans_box.add(*legende);
        legende_l = legende;
        erg_status.set_xalign(0);
        erg_status.set_line_wrap(true);
        erg_status.set_max_width_chars(44);
        erg_status.set_no_show_all(true);
        klasse(erg_status, "hirch-klein");
        ans_box.add(erg_status);
        // Klick ins Fräsergebnis: was dort steht, und «Restmaterial hier nachfräsen»
        klick_box.set_no_show_all(true);
        klasse(klick_box, "hirch-karte");
        klick_text.set_xalign(0);
        klick_text.set_line_wrap(true);
        klick_text.set_max_width_chars(44);
        klick_box.add(klick_text);
        klick_rest.set_label(TR("Restmaterial hier nachfräsen"));
        klick_rest.set_tooltip_text(TR("Legt ein 3D-Schlichten «nur Restmaterial» an, begrenzt auf einen Kreis um diese "
                                       "Stelle, und öffnet es – dort einen kleineren Kugelfräser wählen."));
        klick_rest.set_halign(Gtk::ALIGN_START);
        klick_rest.signal_clicked().connect([this] { rest_hier(); });
        klick_box.add(klick_rest);
        klick_umriss.set_label(TR("Umriss in die Zeichnung"));
        klick_umriss.set_tooltip_text(TR("Der Umriss dieser Fläche kommt schwarz und ausgewählt in die Zeichnung – z. B. "
                                         "als Bereich einer 3D-Bearbeitung («Gewählte Linien als Bereich»)."));
        klick_umriss.set_halign(Gtk::ALIGN_START);
        klick_umriss.signal_clicked().connect([this] {
            linien_einfuegen(klick_linien, TR("Fläche") + std::string(" ") + str(info, "name"));
            klick_box.hide();
        });
        klick_box.add(klick_umriss);
        klick_umriss.set_no_show_all(true);
        flaechen_bauen();
        ansicht.beim_klick = [this](double x, double y, double z, double m) { klick_zeigen(x, y, z, m); };
        ansicht.beim_klick_modell = [this](double x, double y, double z, bool umschalt, int seite) {
            flaeche_holen(x, y, z, umschalt, seite);
        };
        status.set_xalign(0);
        klasse(status, "hirch-dim");
        voll.add(status);

        name.set_xalign(0);
        klasse(name, "hirch-titel");
        voll.add(name);
        for (auto l : {&details, &original}) {
            l->set_xalign(0);
            l->set_line_wrap(true);
            l->set_max_width_chars(44);
            klasse(*l, "hirch-dim");
            klasse(*l, "hirch-klein");
            voll.add(*l);
        }
        aktualisieren.set_label(TR("Aus der Datei neu einlesen"));
        aktualisieren.set_halign(Gtk::ALIGN_START);
        aktualisieren.set_no_show_all(true);
        aktualisieren.signal_clicked().connect([this] { aus_datei(); });
        voll.add(aktualisieren);
        // Karten aus der grossen 3D-Ansicht (Rechtsklick): was an einer Stelle steht, gewählte Flächen
        voll.add(klick_box);
        voll.add(fl_box);

        auto b_lage = aufklapp(TR("Lage im Material"), "lage", true);
        g_lage = formular();
        auto ll = label(TR("Lage"));
        ll->set_hexpand(true);
        g_lage->attach(*ll, 0, 0);
        lage.append("mittig", TR("mittig im Material"));
        lage.append("oben", TR("Oberkante bündig"));
        lage.append("tiefe", TR("Oberkante unter der Oberfläche"));
        g_lage->attach(lage, 1, 0, 2, 1);
        tiefe = spin_zeile(*g_lage, 1, TR("Tiefe der Oberkante"), 0, 0, 500, 0.1, 2, "mm");
        zfaktor = spin_zeile(*g_lage, 2, TR("Stauchung in Z"), 100, 1, 1000, 1, 0, "%");
        weiss = spin_zeile(*g_lage, 3, TR("Höhe für Weiss"), 5, 0.1, 200, 0.5, 2, "mm");
        auto lr = label(TR("Rand oben / unten"), "hirch-dim");
        g_lage->attach(*lr, 0, 4);
        rand_l.set_xalign(0);
        klasse(rand_l, "hirch-wert");
        g_lage->attach(rand_l, 1, 4, 2, 1);
        b_lage->add(*g_lage);

        auto b_ausr = aufklapp(TR("Ausrichtung"), "ausrichtung", true);
        g_ausr = formular();
        auto lo = label(TR("Welche Seite ist oben"));
        lo->set_hexpand(true);
        g_ausr->attach(*lo, 0, 0);
        for (auto s : SEITEN) oben.append(s, seite_text(s));
        g_ausr->attach(oben, 1, 0, 2, 1);
        neig_x = spin_zeile(*g_ausr, 1, TR("Neigung um X"), 0, -90, 90, 0.5, 1, "°");
        neig_y = spin_zeile(*g_ausr, 2, TR("Neigung um Y"), 0, -90, 90, 0.5, 1, "°");
        b_ausr->add(*g_ausr);
        auto ha = label(TR("Drehen in der Ebene, Grösse und Lage: wie jedes Objekt auf der Zeichnung."), "hirch-dim");
        klasse(*ha, "hirch-klein");
        ha->set_line_wrap(true);
        ha->set_max_width_chars(44);
        b_ausr->add(*ha);
        zweiseitig_bauen();
        vektoren_bauen();
        auto verweis = label(TR("Wenden um X oder Y: in «Auftrag einrichten»"), "hirch-dim");
        klasse(*verweis, "hirch-klein");
        verweis->set_line_wrap(true);
        verweis->set_max_width_chars(44);
        voll.add(*verweis);
        // Peter 08.11. «Alles heute»: keine kleine Ansicht mehr (die grosse 3D-Ansicht zeigt dasselbe; Flächen und
        // Klick ins Ergebnis dort per Rechtsklick), «Entwurf» steht in der Fräsen-Spalte. ans_box bleibt ungezeigt
        // für die Szene und die Oberflächentests.
        box->add(voll);

        auto sw = Gtk::make_managed<Gtk::ScrolledWindow>();
        sw->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
        sw->set_vexpand(true);
        sw->add(*box);
        UI::pack_start(panel, *sw, true, true);

        auto neu_rechnen = [this] {
            if (setzt) return;
            zeile_zeigen(tiefe, lage.get_active_id() == "tiefe");
            verzoegert.disconnect();
            verzoegert = Glib::signal_timeout().connect([this] {
                rechnen(true);
                return false;
            }, 350);
        };
        for (auto s : {tiefe, zfaktor, neig_x, neig_y, weiss}) s->signal_value_changed().connect(neu_rechnen);
        for (auto c : {&lage, &oben}) c->signal_changed().connect(neu_rechnen);
    }

    /// Szene für das Zeichenfenster eintragen (3D-Ansicht im Hauptfenster zeigt dieselbe)
    void anmelden()
    {
        auto dt = panel.getDesktop();
        if (!dt) return;
        auto &m = szenen();
        if (m[dt] == szene) return;
        m[dt] = szene;
        szenen_neu().emit(dt);
    }

    ~Impl()
    {
        for (auto it = szenen().begin(); it != szenen().end();) {
            if (it->second == szene) {
                auto dt = it->first;
                it = szenen().erase(it);
                szenen_neu().emit(dt);
            } else {
                ++it;
            }
        }
        verzoegert.disconnect();
        stege_spaeter.disconnect();
        if (lauf_stege) *lauf_stege = false;
        if (lauf) *lauf = false;
        if (lauf_einlesen) *lauf_einlesen = false;
        if (lauf_vek) *lauf_vek = false;
        ordner_loeschen(ordner);
    }

    SPItem *item()
    {
        auto d = panel.getDocument();
        if (!d || item_id.empty()) return nullptr;
        auto it = cast<SPItem>(d->getObjectById(item_id));
        return (it && it->getRepr()->attribute(RELIEF)) ? it : nullptr;
    }

    /// Abschnitt zum Aufklappen (Fenster «3D-Modell»): Titel in Grossbuchstaben, offen/zu bleibt gemerkt. Eigener
    /// Kopf (flacher Knopf mit ▸/▾) statt Gtk::Expander: unter Windows liessen sich die anfangs offenen Expander
    /// nicht zuklappen (Windows 07.11.). rahmen: Kopf und Inhalt (zum Ein- und Ausblenden), oeffnen: aufklappen.
    Gtk::Box *aufklapp(std::string const &titel, char const *schluessel, bool offen,
                       Gtk::Widget **rahmen = nullptr, Gtk::Label **titel_l = nullptr,
                       std::function<void()> *oeffnen = nullptr)
    {
        auto r = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 0);
        auto kopf = Gtk::make_managed<Gtk::Button>();
        kopf->set_relief(Gtk::RELIEF_NONE);
        kopf->set_focus_on_click(false);
        kopf->set_halign(Gtk::ALIGN_FILL);
        auto kz = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
        auto pfeil = label("", "hirch-kopf");
        auto t = label(Glib::ustring(titel).uppercase(), "hirch-kopf");
        t->set_hexpand(true);
        t->set_xalign(0);
        kz->add(*pfeil);
        kz->add(*t);
        kopf->add(*kz);
        r->add(*kopf);
        auto innen = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
        innen->set_margin_top(6);
        innen->set_margin_bottom(4);
        r->add(*innen);
        voll.add(*r);
        std::string pfad = std::string("/hirch/3d/fenster/") + schluessel;
        auto stand = std::make_shared<bool>(Inkscape::Preferences::get()->getBool(pfad, offen));
        auto zeigen = [innen, pfeil, stand] {
            pfeil->set_text(*stand ? "▾" : "▸");
            zeile_zeigen(innen, *stand);
        };
        zeigen();
        kopf->signal_clicked().connect([stand, pfad, zeigen] {
            *stand = !*stand;
            Inkscape::Preferences::get()->setBool(pfad, *stand);
            zeigen();
        });
        if (rahmen) *rahmen = r;
        if (titel_l) *titel_l = t;
        if (oeffnen)
            *oeffnen = [stand, pfad, zeigen] {
                if (*stand) return;
                *stand = true;
                Inkscape::Preferences::get()->setBool(pfad, true);
                zeigen();
            };
        return innen;
    }

    /// Welches Modell zeigen: das gewählte, sonst das bisherige, sonst das erste der Zeichnung
    void waehlen()
    {
        auto d = panel.getDocument();
        SPItem *neu = nullptr;
        if (auto sel = panel.getSelection())
            for (auto it : sel->items())
                if (it->getRepr()->attribute(RELIEF)) {
                    neu = it;
                    break;
                }
        if (!neu) neu = item();
        if (!neu) {
            auto alle = reliefs(d);
            if (!alle.empty()) neu = alle.front();
        }
        if (!neu) {
            item_id.clear();
            attr_alt.clear();
            // Windows 07.11.: nach «Datei › Schliessen» stand die Warnung des alten Modells noch da
            hinweis_setzen(warn, "", "hirch-karte");
            hinweis_setzen(angelegt, "", "hirch-karte");
            zeile_zeigen(&voll, false);
            zeile_zeigen(&leer, true);
            ansicht.kein_modell();
            return;
        }
        zeile_zeigen(&leer, false);
        zeile_zeigen(&voll, true);
        std::string id = neu->getId() ? neu->getId() : "";
        std::string attr = neu->getRepr()->attribute(RELIEF);
        if (id == item_id && attr == attr_alt) return;
        bool anderes = id != item_id;
        item_id = id;
        attr_alt = attr;
        laden(neu, anderes);
    }

    void laden(SPItem *it, bool anderes)
    {
        auto d = panel.getDocument();
        info = relief_info(it);
        if (anderes) {
            daten_datei = Glib::build_filename(ordner, "daten-" + item_id + ".txt");
            if (!daten_schreiben(d, info, daten_datei)) daten_datei.clear();
        }
        setzt = true;
        bj::object a, l;
        if (auto x = unter(info, "ausrichtung")) a = *x;
        if (auto x = unter(info, "lage")) l = *x;
        bool bild = str(info, "art") == "bild";
        lage.set_active_id(str(l, "art", bild ? "oben" : "mittig"));
        tiefe->set_value(num(l, "tiefe", 0));
        zfaktor->set_value(num(a, "z_faktor", 1) * 100);
        weiss->set_value(num(a, "hoehe", 5));
        oben.set_active_id(str(a, "oben", "+Z"));
        neig_x->set_value(liste_zahl(a, "neigung", 0));
        neig_y->set_value(liste_zahl(a, "neigung", 1));
        zeile_zeigen(tiefe, lage.get_active_id() == "tiefe");
        zeile_zeigen(weiss, bild);
        zeile_zeigen(g_ausr, !bild);
        zweiseitig_laden();
        {
            auto n = Glib::ustring(str(info, "name")).lowercase().raw();
            bool step = n.size() > 4 && (n.substr(n.size() - 5) == ".step" || n.substr(n.size() - 4) == ".stp");
            zeile_zeigen(kanten_zeile, step);
            zeile_zeigen(merkmal_zeile, step);
            vek_hinweis.hide();
        }
        setzt = false;
        if (anderes) fl_wahl.clear();         // anderes Modell: Flächenwahl neu
        zeile_zeigen(&fl_box, !fl_wahl.empty());  // Karte erst nach einem Rechtsklick in der 3D-Ansicht
        flaechen_zeigen();
        name.set_text(str(info, "name"));
        details.set_text(art_name(str(info, "art")) + " · " + mm(liste_zahl(info, "masse", 0)) + " × " +
                         mm(liste_zahl(info, "masse", 1)) + " × " + mm(liste_zahl(info, "masse", 2), 2) + " mm · " +
                         TRF("in der Zeichnung gespeichert ({} MB)", {zahl(num(info, "daten_bytes", 0) / 1048576.0, 1)}));
        original_zeigen();
        rechnen(false);
    }

    void original_zeigen()
    {
        auto o = str(info, "original");
        aktualisieren.hide();
        if (o.empty()) {
            original.set_text("");
            return;
        }
        std::string zustand;
        GStatBuf st;
        if (g_stat(o.c_str(), &st) != 0) {
            zustand = TR("nicht mehr da");
        } else if (double(st.st_mtime) > num(info, "original_zeit", 0) + 1) {
            zustand = TR("seither geändert");
            aktualisieren.set_no_show_all(false);
            aktualisieren.show();
        } else {
            zustand = TR("unverändert");
        }
        original.set_text(TR("Original") + std::string(": ") + o + " · " + zustand);
    }

    bj::object ausrichtung_json()
    {
        bj::object a;
        if (auto x = unter(info, "ausrichtung")) a = *x;
        a["oben"] = oben.get_active_id().raw();
        bj::array n;
        n.push_back(wert(neig_x));
        n.push_back(wert(neig_y));
        a["neigung"] = n;
        a["z_faktor"] = wert(zfaktor) / 100.0;
        a["hoehe"] = wert(weiss);
        return a;
    }

    bj::object lage_json()
    {
        bj::object l;
        l["art"] = lage.get_active_id().raw();
        l["tiefe"] = wert(tiefe);
        return l;
    }

    /// aendern: Ergebnis in die Zeichnung schreiben (Bild, Einstellungen); sonst nur die Ansicht
    void rechnen(bool aendern)
    {
        auto d = panel.getDocument();
        if (!d || daten_datei.empty()) {
            status.set_text(daten_datei.empty() && !item_id.empty() ? TR("Die Modelldaten fehlen in der Zeichnung.") : "");
            return;
        }
        Material m = material(d, item());
        ansicht.material(m.b, m.h, m.d);
        unsigned g = ++gen;
        if (lauf) *lauf = false;
        status.set_text(TR("Wird gerechnet …"));
        rechnet = true;
        auto a = ausrichtung_json(), l = lage_json();
        std::string ziel = Glib::build_filename(ordner, "lauf-" + std::to_string(g));
        g_mkdir_with_parents(ziel.c_str(), 0700);
        std::vector<std::string> args = {"ansicht", "--daten", daten_datei, "--name", str(info, "name"),
                                         "--ordner", ziel, "--ausrichtung", bj::serialize(a),
                                         "--lage", bj::serialize(l), "--dicke", attr_zahl(m.d),
                                         "--achse", achse_jetzt()};
        lauf = kern(args, ziel, [this, g, aendern, a, l, ziel](bool ok, bj::object const &e, std::string const &f) {
            if (g != gen) {
                ordner_loeschen_unter(ziel);
                return;
            }
            fertig(ok && flag(e, "ok", false), e, f, aendern, a, l);
            ordner_loeschen_unter(ziel);
        });
    }

    static void ordner_loeschen_unter(std::string const &ziel)
    {
        try {
            Glib::Dir dd(ziel);
            for (auto const &n : dd) ::g_remove(Glib::build_filename(ziel, n).c_str());
        } catch (...) {
        }
        g_rmdir(ziel.c_str());
    }

    void fertig(bool ok, bj::object const &e, std::string const &fehler, bool aendern, bj::object const &a,
                bj::object const &l)
    {
        status.set_text("");
        rechnet = false;
        if (!ok) {
            auto t = warnungen(e);
            hinweis_setzen(warn, t.empty() ? (fehler.empty() ? TR("Rechnung fehlgeschlagen.") : fehler) : t,
                           "hirch-fehler");
            return;
        }
        auto it = item();
        auto d = panel.getDocument();
        if (!it || !d) return;
        if (aendern) bild_setzen(it, e, a, l);
        unten_uri = datei(e, "bild_unten").empty() ? std::string() : png_uri(datei(e, "bild_unten"));
        seite2_bild();
        // Ansicht
        Netz n;
        if (netz_lesen(datei(e, "netz"), n)) {
            Material m = material(d, item());
            std::vector<float> ecken;
            std::vector<unsigned> idx;
            netz_bauen(n, modell_zu_material(it, info, m, MM_PX), num(e, "z_versatz", 0), ecken, idx);
            ansicht.modell(std::move(ecken), std::move(idx));
        }
        rand_l.set_text(mm(num(e, "rand_oben", 0), 2) + " / " + mm(num(e, "rand_unten", 0), 2) + " mm");
        stege_rechnen(0);
        auto w = warnungen(e), h = hinweise(e);
        if (!w.empty()) hinweis_setzen(warn, w, "hirch-fehler");
        else hinweis_setzen(warn, h, "hirch-karte");
    }

    // ------------------------------------------------------------------ Vektoren aus dem Modell

    void vektoren_bauen()
    {
        auto b_vek = aufklapp(TR("Vektoren aus dem Modell"), "vektoren", false);
        auto zeile = [&](Gtk::Widget *links, char const *art) {
            auto z = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
            links->set_hexpand(true);
            links->set_halign(Gtk::ALIGN_START);
            z->add(*links);
            auto b = Gtk::make_managed<Gtk::Button>(TR("In die Zeichnung"));
            std::string a = art;
            b->signal_clicked().connect([this, a] { vektoren_holen(a); });
            z->add(*b);
            b_vek->add(*z);
            return z;
        };
        zeile(label(TR("Umriss (Draufsicht)")), "umriss");
        auto hl = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
        hl->add(*label(TR("Höhenlinie bei Z")));
        hl_z = Gtk::make_managed<Gtk::SpinButton>(Gtk::Adjustment::create(-5, -1000, 100, 0.5, 5), 0.5, 2);
        hl_z->set_numeric(true);
        punkt_und_komma(hl_z);
        hl_z->set_width_chars(7);
        hl->add(*hl_z);
        hl->add(*label("mm", "hirch-dim"));
        zeile(hl, "hoehenlinie");
        kanten_zeile = zeile(label(TR("Kanten (nur STEP)")), "kanten");
        {
            // Bohrungen und Taschen (nur STEP): erkennen, auswählen, als Bearbeitungen anlegen
            merkmal_zeile = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
            auto l = label(TR("Bohrungen und Taschen (nur STEP)"));
            l->set_hexpand(true);
            l->set_halign(Gtk::ALIGN_START);
            merkmal_zeile->add(*l);
            auto b = Gtk::make_managed<Gtk::Button>(TR("Erkennen …"));
            b->set_tooltip_text(TR("Sucht im STEP-Modell Bohrungen und Taschen, die von oben zugänglich sind, und legt "
                                   "dafür Bearbeitungen mit Kreisen und Umrissen in der Zeichnung an."));
            b->signal_clicked().connect([this] { merkmale_holen(); });
            merkmal_zeile->add(*b);
            b_vek->add(*merkmal_zeile);
        }
        auto h = label(TR("Die Linien kommen schwarz in die Zeichnung (noch keine Fräsart) – einfärben wie jedes "
                          "Objekt. Kanten der Unterseite kommen auf Seite 2."),
                       "hirch-dim");
        klasse(*h, "hirch-klein");
        h->set_line_wrap(true);
        h->set_max_width_chars(44);
        h->set_xalign(0);
        b_vek->add(*h);
        vek_hinweis.set_xalign(0);
        vek_hinweis.set_line_wrap(true);
        vek_hinweis.set_max_width_chars(44);
        vek_hinweis.set_no_show_all(true);
        b_vek->add(vek_hinweis);
    }

    void vektoren_holen(std::string const &art)
    {
        auto it = item();
        auto d = panel.getDocument();
        if (!it || !d || daten_datei.empty()) return;
        Material m = material(d, item());
        double f = MM_PX;
        std::string ziel = Glib::build_filename(ordner, "vek-" + std::to_string(++gen_stege));
        g_mkdir_with_parents(ziel.c_str(), 0700);
        std::vector<std::string> args = {"vektoren", "--daten", daten_datei, "--info", bj::serialize(info),
                                         "--ecken", bj::serialize(bild_ecken(it, m, f)), "--dicke", attr_zahl(m.d),
                                         "--art", art, "--material", attr_zahl(m.b) + "," + attr_zahl(m.h),
                                         "--achse", achse_jetzt()};
        if (art == "hoehenlinie") args.insert(args.end(), {"--z", attr_zahl(wert(hl_z))});
        status.set_text(TR("Wird gerechnet …"));
        if (lauf_vek) *lauf_vek = false;
        lauf_vek = kern(args, ziel, [this, ziel, art](bool ok, bj::object const &e, std::string const &fehler) {
            ordner_loeschen_unter(ziel);
            status.set_text("");
            if (!ok || !flag(e, "ok", false)) {
                hinweis_setzen(vek_hinweis, warnungen(e).empty() ? fehler : warnungen(e), "hirch-fehler");
                return;
            }
            auto l = liste_von(e, "linien");
            std::string w = warnungen(e);
            if (!l || l->empty()) {
                hinweis_setzen(vek_hinweis, w.empty() ? TR("Nichts gefunden.") : w, "hirch-karte");
                return;
            }
            std::string titel = art == "umriss" ? TR("Umriss") : art == "kanten" ? TR("Kanten") : TR("Höhenlinie");
            int auf2 = linien_einfuegen(*l, titel + " " + str(info, "name"));
            std::string t = TRF("{} Linien in der Zeichnung.", {std::to_string(l->size())});
            if (auf2 > 0) t += " " + TRF("Davon {} auf Seite 2 (Unterseite).", {std::to_string(auf2)});
            if (!w.empty()) t += "\n" + w;
            if (auto dd = panel.getDocument(); auf2 > 0 && dd && !material(dd, item()).hat2) {
                hinweis_setzen(vek_hinweis, t + "\n" + TR("Seite 2 fehlt – die Kanten der Unterseite liegen gewendet auf "
                                                          "Seite 1. Zuerst «Von beiden Seiten fräsen» einschalten."),
                               "hirch-fehler");
                return;
            }
            hinweis_setzen(vek_hinweis, t, "hirch-karte");
        });
    }

    void merkmale_holen()
    {
        auto it = item();
        auto d = panel.getDocument();
        if (!it || !d || daten_datei.empty()) return;
        Material m = material(d, item());
        double f = MM_PX;
        std::string ziel = Glib::build_filename(ordner, "mm-" + std::to_string(++gen_stege));
        g_mkdir_with_parents(ziel.c_str(), 0700);
        std::vector<std::string> args = {"merkmale", "--daten", daten_datei, "--info", bj::serialize(info),
                                         "--ecken", bj::serialize(bild_ecken(it, m, f)), "--dicke", attr_zahl(m.d),
                                         "--material", attr_zahl(m.b) + "," + attr_zahl(m.h), "--achse", achse_jetzt()};
        status.set_text(TR("Wird gerechnet …"));
        if (lauf_vek) *lauf_vek = false;
        lauf_vek = kern(args, ziel, [this, ziel](bool ok, bj::object const &e, std::string const &fehler) {
            ordner_loeschen_unter(ziel);
            status.set_text("");
            if (!ok || !flag(e, "ok", false)) {
                hinweis_setzen(vek_hinweis, warnungen(e).empty() ? fehler : warnungen(e), "hirch-fehler");
                return;
            }
            auto lb = liste_von(e, "bohrungen");
            auto lt = liste_von(e, "taschen");
            bj::object bw;
            if (auto x = unter(e, "boeden_waende")) bw = *x;
            auto lbw = liste_von(bw, "boeden");
            if ((!lb || lb->empty()) && (!lt || lt->empty()) && (!lbw || lbw->empty())) {
                hinweis_setzen(vek_hinweis, warnungen(e).empty() ? TR("Nichts gefunden.") : warnungen(e), "hirch-karte");
                return;
            }
            merkmale_dialog(lb ? *lb : bj::array(), lt ? *lt : bj::array(), bw);
        });
    }

    /// Gefundene Bohrungen (gleiche Ø, Tiefe, durch: eine Zeile) und Taschen zum Anhaken; «Anlegen»:
    /// je Zeile eine Bearbeitung (Bohren bzw. Tasche) und die Kreise/Umrisse in ihrer Farbe.
    void merkmale_dialog(bj::array const &bohrungen, bj::array const &taschen, bj::object const &bw = {})
    {
        auto win = panel.getDesktop() ? panel.getDesktop()->getInkscapeWindow() : nullptr;
        if (!win) return;
        Gtk::Dialog dlg(TR("Bohrungen und Taschen"), *win, true);
        css_einrichten();
        dlg.set_default_size(620, -1);
        auto box = dlg.get_content_area();
        box->set_spacing(8);
        box->set_margin_start(16);
        box->set_margin_end(16);
        box->set_margin_top(10);
        auto kt = label(TR("Bohrungen und Taschen"));
        klasse(*kt, "hirch-seitentitel");
        box->add(*kt);
        auto ku = label(TRF("Aus dem STEP-Modell «{}», in Material-mm; was nur von unten zugänglich ist, kommt auf "
                            "Seite 2 (gewendet).", {str(info, "name")}),
                        "hirch-dim");
        ku->set_line_wrap(true);
        ku->set_xalign(0);
        box->add(*ku);

        struct Gruppe {
            std::string titel, text;
            bj::object op;
            bj::array linien;
            Gtk::CheckButton *an = nullptr;
            bool seite2 = false;
        };
        auto gruppen = std::make_shared<std::vector<Gruppe>>();
        auto kreis = [](double x, double y, double r) {
            bj::array pts;
            for (int k = 0; k <= 72; ++k) {
                double t = 2 * M_PI * k / 72;
                pts.push_back(bj::array{x + r * std::cos(t), y + r * std::sin(t)});
            }
            return pts;
        };
        auto doc_ = panel.getDocument();
        bool seite2_da = doc_ && material(doc_, item()).hat2;
        // Bohrungen gruppieren (je Seite)
        std::map<std::tuple<int, long, long, bool>, std::size_t> idx;
        for (auto const &v : bohrungen) {
            auto o = v.if_object();
            if (!o) continue;
            double dm = num(*o, "d", 0), t = num(*o, "tiefe", 0), unten = num(*o, "unten", 0);
            bool durch = flag(*o, "durch", false);
            int seite = (int)num(*o, "seite", 1);
            auto key = std::make_tuple(seite, std::lround(dm * 100), std::lround(-unten * 10), durch);
            if (!idx.count(key)) {
                idx[key] = gruppen->size();
                Gruppe g;
                g.titel = TRF("Bohrung Ø {} mm", {zahl(dm, 2)});
                g.op["art"] = "bohren";
                g.op["tiefe"] = -unten + (durch ? 0.3 : 0.0);   // durch: etwas tiefer, sauber durch
                g.op["titel"] = g.titel;
                g.text = durch ? TRF("{} mm tief, durch", {zahl(t, 2)}) : TRF("{} mm tief (Sackloch)", {zahl(t, 2)});
                if (seite == 2) {
                    g.op["auf_seite"] = 2;
                    g.text += " · " + std::string(TR("Seite 2"));
                    g.seite2 = true;
                }
                gruppen->push_back(g);
            }
            bj::object l;
            l["punkte"] = kreis(num(*o, "x", 0), num(*o, "y", 0), dm / 2);
            l["geschlossen"] = true;
            if (seite == 2) l["seite"] = 2;
            (*gruppen)[idx[key]].linien.push_back(l);
        }
        std::size_t n_bohr = gruppen->size();
        for (auto const &v : taschen) {
            auto o = v.if_object();
            if (!o) continue;
            Gruppe g;
            g.titel = TRF("Tasche {} × {} mm", {zahl(num(*o, "laenge", 0), 1), zahl(num(*o, "breite", 0), 1)});
            g.op["art"] = "tasche";
            g.op["tiefe"] = -num(*o, "boden", 0);
            g.op["starttiefe"] = std::max(0.0, -num(*o, "oben", 0));
            g.op["titel"] = g.titel;
            g.text = TRF("Tiefe der Tasche {} mm", {zahl(num(*o, "tiefe", 0), 2)});
            if (num(*o, "eckradius", 0) > 0.05) g.text += " · " + TRF("Ecken gerundet, Radius {} mm", {zahl(num(*o, "eckradius", 0), 1)});
            bj::object l;
            if (auto p = liste_von(*o, "punkte")) l["punkte"] = *p;
            l["geschlossen"] = true;
            if (num(*o, "seite", 1) == 2) {
                l["seite"] = 2;
                g.op["auf_seite"] = 2;
                g.text += " · " + std::string(TR("Seite 2"));
                g.seite2 = true;
            }
            g.linien.push_back(l);
            gruppen->push_back(g);
        }
        auto karte = [&](std::string const &kopf, std::size_t von, std::size_t bis) {
            if (von >= bis) return;
            auto k = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
            klasse(*k, "hirch-karte2");
            k->add(*label(kopf, "hirch-kopf"));
            for (std::size_t i = von; i < bis; ++i) {
                auto &g = (*gruppen)[i];
                auto z = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 10);
                std::string n = g.linien.size() > 1 ? std::to_string(g.linien.size()) + " × " : std::string();
                g.an = Gtk::make_managed<Gtk::CheckButton>(n + g.titel);
                g.an->set_active(!g.seite2 || seite2_da);
                g.an->set_sensitive(!g.seite2 || seite2_da);
                g.an->set_size_request(260, -1);
                z->add(*g.an);
                auto h = label(g.text, "hirch-dim");
                klasse(*h, "hirch-klein");
                z->add(*h);
                k->add(*z);
            }
            box->add(*k);
        };
        karte(TR("BOHRUNGEN"), 0, n_bohr);
        karte(TR("TASCHEN"), n_bohr, gruppen->size());

        // Böden, Wände, Rundungen, dünne Wände (Peter 25.10.): was erkannt ist, und der Vorschlag dazu
        struct Vorschlag {
            bj::object op;
            Gtk::CheckButton *an = nullptr;
        };
        auto vorschlaege = std::make_shared<std::vector<Vorschlag>>();
        if (auto lbw = liste_von(bw, "boeden"); lbw && !lbw->empty()) {
            auto k = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
            klasse(*k, "hirch-karte2");
            k->add(*label(TR("BÖDEN, WÄNDE UND RUNDUNGEN (SEITE 1)"), "hirch-kopf"));
            auto info_zeile = [&](std::string const &links, std::string const &rechts) {
                auto z = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 10);
                auto l = label(links);
                l->set_size_request(260, -1);
                l->set_xalign(0);
                z->add(*l);
                auto h = label(rechts, "hirch-dim");
                klasse(*h, "hirch-klein");
                h->set_line_wrap(true);
                h->set_xalign(0);
                z->add(*h);
                k->add(*z);
            };
            std::string bt;
            for (auto const &v : *lbw)
                if (auto o = v.if_object())
                    bt += (bt.empty() ? "" : " · ") + zahl(num(*o, "z", 0), 1) + " (" + zahl(num(*o, "flaeche", 0), 0) + " mm²)";
            info_zeile(anzahl((long)lbw->size(), TR("Boden"), TR("Böden")), bt);
            if (num(bw, "waende", 0) > 0)
                info_zeile(anzahl((long)num(bw, "waende", 0), TR("senkrechte Wand"), TR("senkrechte Wände")),
                           TRF("bis {} mm hoch", {zahl(num(bw, "wand_hoehe", 0), 1)}));
            double R = 0;
            if (auto lr = liste_von(bw, "rundungen"))
                for (auto const &v : *lr)
                    if (auto o = v.if_object()) {
                        R = std::max(R, num(*o, "radius", 0));
                        info_zeile(TRF("Rundung am Fuss R {}", {zahl(num(*o, "radius", 0), 1)}),
                                   TRF("{} × auf dem Boden {}", {zahl(num(*o, "anzahl", 0), 0), zahl(num(*o, "z", 0), 1)}));
                    }
            auto du = unter(bw, "duenn");
            if (du) {
                double dk = num(*du, "dicke", 0), hh = num(*du, "hoehe", 0);
                info_zeile(TRF("{} × dünne Wand", {zahl(num(*du, "anzahl", 0), 0)}),
                           TRF("{} mm dick · {} mm hoch (1 : {})", {zahl(dk, 1), zahl(hh, 1), zahl(dk > 0 ? hh / dk : 0, 0)}));
            }
            k->add(*label(TR("VORSCHLAG"), "hirch-kopf"));
            // 3D-Bearbeitungen von Seite 1
            std::string s_id, s_wz, s_name, f_id;
            bool bw_da = false;
            for (auto const &v : bearbeitungen_lesen()) {
                auto o = v.if_object();
                if (!o || (int)num(*o, "auf_seite", 1) != 1) continue;
                auto art = str(*o, "art");
                if (art == "relief_schruppen" && !flag(*o, "rest", false) && s_id.empty()) {
                    s_id = str(*o, "id");
                    s_wz = str(*o, "werkzeug", "C");
                    s_name = str(*o, "titel");
                    if (s_name.empty()) s_name = TR("3D-Schruppen");
                }
                if (art == "relief_schlichten" && f_id.empty()) f_id = str(*o, "id");
                if (art == "boeden_waende") bw_da = true;
            }
            auto vorschlag_zeile = [&](bj::object op, std::string const &titel, std::string const &text) {
                auto z = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 10);
                Vorschlag v;
                v.op = std::move(op);
                v.an = Gtk::make_managed<Gtk::CheckButton>(titel);
                v.an->set_active(true);
                v.an->set_size_request(260, -1);
                z->add(*v.an);
                auto h = label(text, "hirch-dim");
                klasse(*h, "hirch-klein");
                h->set_line_wrap(true);
                h->set_xalign(0);
                z->add(*h);
                k->add(*z);
                vorschlaege->push_back(v);
            };
            if (bw_da) {
                info_zeile(TR("Böden und Wände"), TR("ist schon eingerichtet"));
            } else if (s_id.empty()) {
                info_zeile(TR("Böden und Wände"), TR("zuerst ein 3D-Schruppen anlegen – es fräst nur, wo das Schruppen hinkam"));
            } else {
                bj::object op;
                op["art"] = "boeden_waende";
                op["im_wechsel_mit"] = s_id;
                op["werkzeug"] = s_wz;
                op["stufe"] = 10.0;
                vorschlag_zeile(op, TR("Böden und Wände"),
                                TRF("⇄ im Wechsel mit {} · alle 10 mm · gleicher Fräser, kein Werkzeugwechsel", {s_name}) +
                                    (du ? std::string(" · ") + TR("hält die dünnen Wände") : std::string()));
            }
            if (R > 0) {
                bj::object op;
                op["art"] = "relief_schlichten";
                op["strategie"] = "kombiniert";
                op["auslassen_fertig"] = true;
                op["fuss_umlauf"] = true;
                std::string wz_text;
                if (auto kg = unter(bw, "kugel")) {
                    op["werkzeug_neu"] = *kg;
                    wz_text = TRF("Kugelfräser Ø {} aus «Meine Werkzeuge»", {zahl(num(*kg, "durchmesser", 0), 1)});
                } else {
                    wz_text = TRF("kein Kugelfräser bis R {} in «Meine Werkzeuge» – Werkzeug danach wählen", {zahl(R, 1)});
                }
                if (!f_id.empty()) op["aendern"] = f_id;
                vorschlag_zeile(op, f_id.empty() ? TRF("3D-Schlichten für R {}", {zahl(R, 1)})
                                                 : TR("3D-Schlichten umstellen"),
                                TR("kombiniert · Fertiges auslassen · Umlauf am Fuss · ") + wz_text);
            }
            box->add(*k);
        }
        if (!seite2_da && std::any_of(gruppen->begin(), gruppen->end(), [](Gruppe const &g) { return g.seite2; })) {
            auto w2 = label(TR("Einiges liegt auf der Unterseite (Seite 2). Seite 2 gibt es noch nicht – zuerst im Fenster "
                               "«3D-Modell» «Von beiden Seiten fräsen» einschalten."));
            klasse(*w2, "hirch-warn");
            w2->set_line_wrap(true);
            w2->set_xalign(0);
            box->add(*w2);
        }
        auto h = label(TR("«Anlegen» legt je Zeile eine Bearbeitung an und zeichnet die Kreise und Umrisse in ihrer "
                          "Farbe. Werkzeug und Zustellung danach in der Bearbeitung prüfen."), "hirch-dim");
        klasse(*h, "hirch-klein");
        h->set_line_wrap(true);
        h->set_xalign(0);
        box->add(*h);
        dlg.add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
        klasse(*dlg.add_button(TR("Anlegen"), Gtk::RESPONSE_OK), "suggested-action");
        dlg.set_default_response(Gtk::RESPONSE_OK);
        dlg.show_all();
        // Oberflächentest: gleich «Anlegen» (mit HIRCH_TEST_MERKMALE_ZEIGEN wie sonst warten – dann
        // fotografiert und schliesst ihn der Test, HIRCH_TEST_FENSTERBILDER)
        if (!(hirch_oberflaechentest_an() && !g_getenv("HIRCH_TEST_MERKMALE_ZEIGEN")) &&
            dlg.run() != Gtk::RESPONSE_OK)
            return;
        dlg.hide();
        auto fr = fraesen();
        if (!fr) {                       // Panel «Fräsen» noch zu: öffnen, es führt die Bearbeitungen
            win->activate_action("dialog-open", Glib::Variant<Glib::ustring>::create("Fraesen"));
            fr = fraesen();
        }
        if (!fr) return;
        bj::array ops;
        std::vector<std::size_t> gewaehlt;
        for (std::size_t i = 0; i < gruppen->size(); ++i) {
            if (!(*gruppen)[i].an->get_active()) continue;
            ops.push_back((*gruppen)[i].op);
            gewaehlt.push_back(i);
        }
        std::size_t n_vor = 0;
        for (auto const &v : *vorschlaege)
            if (v.an->get_active()) {
                ops.push_back(v.op);
                ++n_vor;
            }
        if (gewaehlt.empty() && !n_vor) return;
        auto farben = fr->bearbeitungenAnlegen(bj::serialize(ops));
        bj::array linien;
        for (std::size_t k = 0; k < gewaehlt.size(); ++k) {
            for (auto const &l : (*gruppen)[gewaehlt[k]].linien) {
                bj::object o = l.as_object();
                o["farbe"] = k < farben.size() && !farben[k].empty() ? farben[k] : "#000000";
                linien.push_back(o);
            }
        }
        if (!linien.empty())
            linien_einfuegen(linien, TR("Bohrungen und Taschen") + std::string(" ") + str(info, "name"),
                             TR("Bohrungen und Taschen anlegen"));
        hinweis_setzen(vek_hinweis, TRF("{} Bearbeitungen angelegt oder umgestellt – Werkzeuge prüfen, dann rechnen.",
                                        {std::to_string(gewaehlt.size() + n_vor)}), "hirch-karte");
    }

    /// Linien (Material-mm) als schwarze Pfade in eine Gruppe; Seite 2 auf die zweite Seite der
    /// Zeichnung (gibt es sie nicht: auf Seite 1, mit Hinweis). Rückgabe: Anzahl auf Seite 2.
    int linien_einfuegen(bj::array const &linien, std::string const &titel,
                         std::string const &undo = TR("Vektoren aus dem 3D-Modell"), std::string const &flaechen = {},
                         std::string const &auch = {})
    {
        auto d = panel.getDocument();
        auto bild = item();
        if (!d || !bild) return 0;
        Material m = material(d, item());
        double f = MM_PX;
        auto xml = d->getReprDoc();
        auto eltern = bild->parent ? bild->parent : d->getRoot();
        auto g = xml->createElement("svg:g");
        g->setAttribute("inkscape:label", titel);
        int auf2 = 0;
        for (auto const &v : linien) {
            auto o = v.if_object();
            if (!o) continue;
            auto pts = liste_von(*o, "punkte");
            if (!pts || pts->size() < 2) continue;
            Geom::Rect pg = m.seite;
            if (num(*o, "seite", 1) == 2) {
                ++auf2;
                if (m.hat2) pg = m.seite2;
            }
            std::ostringstream dd;
            dd.imbue(std::locale::classic());
            dd.precision(10);
            bool erst = true;
            for (auto const &q : *pts) {
                auto a = q.if_array();
                if (!a || a->size() < 2) continue;
                double x = pg.left() + (*a)[0].to_number<double>() * f;
                double y = pg.bottom() - (*a)[1].to_number<double>() * f;
                dd << (erst ? "M " : " L ") << x << "," << y;
                erst = false;
            }
            if (flag(*o, "geschlossen", false)) dd << " Z";
            auto pf = xml->createElement("svg:path");
            pf->setAttribute("d", dd.str());
            pf->setAttribute("style", "fill:none;stroke:" + str(*o, "farbe", "#000000") +
                                          ";stroke-width:" + attr_zahl(0.3 * f));
            if (!flaechen.empty()) {          // Linie aus «Gewählte Flächen als Bereich» (svg_lesen: flaechen)
                pf->setAttribute("hirch:flaechen", flaechen);
                g->setAttribute("hirch:flaechen", flaechen);
            }
            if (!auch.empty()) pf->setAttribute("hirch:auch", auch);   // gehört zu dieser Bearbeitung
            g->appendChild(pf);
            Inkscape::GC::release(pf);
        }
        eltern->getRepr()->appendChild(g);
        Inkscape::GC::release(g);
        if (auto gi = cast<SPItem>(d->getObjectByRepr(g))) {
            Geom::Affine e = Geom::identity();
            if (auto ei = cast<SPItem>(gi->parent)) e = ei->i2doc_affine();
            gi->set_item_transform(e.inverse());
            gi->doWriteTransform(gi->transform, nullptr, true);
            if (auto sel = panel.getSelection()) sel->set(gi);
        }
        DocumentUndo::done(d, undo, "");
        return auf2;
    }

    // ------------------------------------------------------------------ zweiseitig

    /// Zweiseitig: auf Seite 2 der Zeichnung die Unterseite des Modells zeigen – gewendet wie das
    /// Werkstück (um X oder Y), an der Stelle, wo sie nach dem Wenden liegt. Ein gesperrtes Bild
    /// (hirch:relief2 = id des Modells), wird nie gefräst. Ändert nichts am Rückgängig-Verlauf.
    void seite2_bild()
    {
        auto d = panel.getDocument();
        if (!d) return;
        auto it = item();
        DocumentUndo::ScopedInsensitive ohne_undo(d);
        // vorhandene Bilder von Seite 2: zum gewählten Modell gehörig, sonst verwaist?
        Inkscape::XML::Node *mein = nullptr;
        std::vector<Inkscape::XML::Node *> weg;
        std::function<void(Inkscape::XML::Node *)> suchen = [&](Inkscape::XML::Node *n) {
            for (auto c = n->firstChild(); c; c = c->next()) {
                if (auto r = c->attribute(RELIEF2)) {
                    if (it && item_id == r && !mein) mein = c;
                    else if (!d->getObjectById(r)) weg.push_back(c);
                } else {
                    suchen(c);
                }
            }
        };
        suchen(d->getReprRoot());
        for (auto n : weg)
            if (auto o = d->getObjectByRepr(n)) o->deleteObject(false);
        bool an = it && flag(zw_info(), "an", false) && material(d, it).hat2 && !unten_uri.empty();
        if (!an) {
            if (mein)
                if (auto o = d->getObjectByRepr(mein)) o->deleteObject(false);
            return;
        }
        Geom::Affine M = seite2_wendung(d, it);
        Geom::Affine eltern = Geom::identity();
        if (auto ei = cast<SPItem>(it->parent)) eltern = ei->i2doc_affine();
        Geom::Affine tr = it->i2doc_affine() * M * eltern.inverse();
        auto r = it->getRepr();
        auto xml = d->getReprDoc();
        bool neu = !mein;
        if (neu) {
            mein = xml->createElement("svg:image");
            mein->setAttribute(RELIEF2, item_id);
            mein->setAttribute("sodipodi:insensitive", "true");
            mein->setAttribute("inkscape:label", TR("3D-Modell, Unterseite (Seite 2)"));
            mein->setAttribute("preserveAspectRatio", "none");
            it->parent->getRepr()->addChild(mein, r);
            Inkscape::GC::release(mein);
        }
        auto setzen = [&](char const *k, std::string const &v) {
            auto alt = mein->attribute(k);
            if (!alt || v != alt) mein->setAttribute(k, v);
        };
        for (char const *k : {"x", "y", "width", "height"})
            if (auto v = r->attribute(k)) setzen(k, v);
        setzen("xlink:href", unten_uri);
        setzen("transform", sp_svg_transform_write(tr));
    }

    bj::object zw_info()
    {
        if (auto z = unter(info, "zweiseitig")) return *z;
        return {};
    }

    void zweiseitig_bauen()
    {
        auto b_zw = aufklapp(TR("Zweiseitig"), "zweiseitig", true);
        zwei_an.set_label(TR("Von beiden Seiten fräsen (Seite 2 nach dem Wenden)"));
        b_zw->add(zwei_an);
        auto g = formular();
        // Wenden um X/Y: nur noch in «Auftrag einrichten» (Peter 06.11.); achse bleibt als Wert hier
        achse.append("x", TR("X-Achse"));
        achse.append("y", TR("Y-Achse"));
        g->attach(*label(TR("Seiten treffen sich")), 0, 1);
        trennung.append("trennlinie", TR("an der Trennlinie des Modells"));
        trennung.append("ebene", TR("auf einer Ebene"));
        g->attach(trennung, 1, 1, 2, 1);
        ebene_z = spin_zeile(*g, 2, TR("Höhe der Ebene"), 0, -1000, 0, 0.1, 2, "mm");
        g->attach(*label(TR("Trennlinie (erkannt)"), "hirch-dim"), 0, 3);
        trenn_l.set_xalign(0);
        klasse(trenn_l, "hirch-wert");
        g->attach(trenn_l, 1, 3, 2, 1);
        zwei_box.add(*g);

        // Spalt um das Modell: durchfräsen oder rundherum eine Haut stehen lassen (Messer)
        auto gh = formular();
        auto ls = label(TR("Spalt um das Modell"));
        ls->set_hexpand(true);
        gh->attach(*ls, 0, 0);
        auto um = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
        klasse(*um, "linked");
        spalt_durch.set_label(TR("Durchfräsen"));
        spalt_haut.set_label(TR("Haut stehen lassen"));
        spalt_durch.set_tooltip_text(TR("Die Seiten treffen sich – nur die Haltestege halten das Teil"));
        spalt_haut.set_tooltip_text(TR("Rundherum bleibt an der Trennlinie eine dünne Haut stehen – mit dem Messer "
                                       "durchschneiden"));
        spalt_durch.set_active(true);
        um->add(spalt_durch);
        um->add(spalt_haut);
        gh->attach(*um, 1, 0, 2, 1);
        haut_d = spin_zeile(*gh, 1, TR("Dicke der Haut"), 0.5, 0.1, 10, 0.1, 2, "mm");
        punkt_und_komma(haut_d);
        ueber_auto.set_label(TR("Automatisch nach dem Schlichtfräser"));
        ueber_auto.set_tooltip_text(TR("Jede Seite fräst so weit über die Trennlinie hinaus, wie der Radius ihres "
                                       "3D-Schlichtfräsers ist – sonst bleibt am Rand des Modells eine Kehle stehen"));
        // Peter 08.11. («Alles heute»): «Über die Trennlinie hinaus» steht in der 3D-Bearbeitung der Seite
        // (hirch-3d-dialoge.cpp); die Felder hier bleiben ungezeigt, damit Laden und Speichern gleich bleiben
        ueber_versteckt.attach(*label(TR("Über die Trennlinie hinaus")), 0, 2);
        ueber_versteckt.attach(ueber_auto, 1, 2, 2, 1);
        ueber_1 = spin_zeile(ueber_versteckt, 3, TR("Seite 1"), 3, 0, 50, 0.5, 2, "mm");
        ueber_2 = spin_zeile(ueber_versteckt, 4, TR("Seite 2"), 3, 0, 50, 0.5, 2, "mm");
        zwei_box.add(*gh);
        haut_hinweis.set_xalign(0);
        haut_hinweis.set_line_wrap(true);
        haut_hinweis.set_max_width_chars(44);
        klasse(haut_hinweis, "hirch-dim");
        klasse(haut_hinweis, "hirch-klein");
        zwei_box.add(haut_hinweis);

        // Haltestege wie an einer Kontur (Peter 27.10.: überall dieselben): Anzahl, Breite, Dicke, dieselben Knöpfe.
        // Die Höhe bestimmt Hirch – hier an der Trennlinie des Modells.
        b_zw->add(zwei_box);
        auto b_st = aufklapp(TR("Haltestege"), "stege", false, &x_stege, &x_stege_l, &x_stege_auf);
        steg_titel = x_stege;
        b_st->add(steg_liste);
        auto gs = formular();
        steg_n = spin_zeile(*gs, 0, TR("Anzahl"), 4, 1, 20, 1, 0, "");
        steg_b = spin_zeile(*gs, 1, TR("Breite"), 8, 1, 100, 0.5, 1, "mm");
        steg_d = spin_zeile(*gs, 2, TR("Dicke"), 3, 0.2, 50, 0.1, 2, "mm");
        steg_d->set_tooltip_text(TR("So viel Material bleibt am Steg stehen – Hirch legt ihn an die Trennlinie"));
        b_st->add(*gs);
        auto knoepfe = Gtk::make_managed<Gtk::FlowBox>();
        knoepfe->set_selection_mode(Gtk::SELECTION_NONE);
        knoepfe->set_max_children_per_line(3);
        auto verteilen = Gtk::make_managed<Gtk::Button>(TR("Gleichmässig verteilen"));
        verteilen->set_tooltip_text(TR("Entfernt die Stege dieses Modells und verteilt neue gleichmässig um den Umriss"));
        verteilen->signal_clicked().connect([this] {
            stege_entfernen();
            stege_rechnen(std::max(1, (int)wert(steg_n)));
        });
        knoepfe->add(*verteilen);
        auto plus = Gtk::make_managed<Gtk::Button>(TR("+ Steg setzen"));
        plus->signal_clicked().connect([this] { stege_rechnen(1); });
        knoepfe->add(*plus);
        auto weg_alle = Gtk::make_managed<Gtk::Button>(TR("Keine Haltestege"));
        weg_alle->set_tooltip_text(TR("Entfernt alle Haltestege dieses Modells (z. B. wenn die Haut genügt)"));
        weg_alle->signal_clicked().connect([this] {
            stege_entfernen();
            stege_bald();
        });
        knoepfe->add(*weg_alle);
        auto alle = Gtk::make_managed<Gtk::Button>(TR("Alle auf die Trennlinie"));
        alle->set_tooltip_text(TR("Stege, deren Höhe von Hand geändert wurde, wieder an die Trennlinie legen"));
        alle->signal_clicked().connect([this] { stege_auto(); });
        knoepfe->add(*alle);
        b_st->add(*knoepfe);
        steg_hinweis.set_text(TR("Die Stege sind gelbe Rechtecke auf der Zeichnung: verschieben, drehen und länger "
                                 "ziehen wie jedes Objekt. Sie müssen vom Modell bis über den Spalt hinaus reichen."));
        steg_hinweis.set_xalign(0);
        steg_hinweis.set_line_wrap(true);
        steg_hinweis.set_max_width_chars(44);
        klasse(steg_hinweis, "hirch-dim");
        klasse(steg_hinweis, "hirch-klein");
        b_st->add(steg_hinweis);

        zwei_an.signal_toggled().connect([this] {
            if (setzt) return;
            zweiseitig_setzen(zwei_an.get_active());
        });
        achse.signal_changed().connect([this] {
            if (setzt) return;
            if (auto fr = fraesen()) fr->reliefZweiseitig(true, achse.get_active_id().raw());
            rechnen(false);                      // Licht der Unterseite und Lage auf Seite 2
        });
        trennung.signal_changed().connect([this] {
            if (setzt) return;
            bool eb = trennung.get_active_id() == "ebene";
            zeile_zeigen(ebene_z, eb);
            // Peter 03.11.: ohne Höhe galt Z 0 (die Oberfläche) – zuerst die erkannte Trennlinie (Mitte) vorschlagen
            auto z = zw_info();
            if (eb && z.find("z") == z.end() && std::isfinite(trenn_mitte)) {
                bool alt = setzt;
                setzt = true;
                ebene_z->set_value(trenn_mitte);
                setzt = alt;
                zw_aendern("z", bj::value(wert(ebene_z)), false);
            }
            zw_aendern("trennung", bj::value(trennung.get_active_id().raw()));
        });
        ebene_z->signal_value_changed().connect([this] {
            if (!setzt) zw_aendern("z", bj::value(wert(ebene_z)));
        });
        steg_d->signal_value_changed().connect([this] {
            if (!setzt) zw_aendern("steg_dicke", bj::value(wert(steg_d)));
        });
        steg_n->signal_value_changed().connect([this] {
            if (!setzt) zw_aendern("steg_anzahl", bj::value((int)wert(steg_n)), false);
        });
        spalt_durch.signal_toggled().connect([this] {
            if (!setzt) haut_setzen(!spalt_durch.get_active());
        });
        spalt_haut.signal_toggled().connect([this] {
            if (!setzt) haut_setzen(spalt_haut.get_active());
        });
        haut_d->signal_value_changed().connect([this] {
            if (setzt) return;
            haut_text();
            zw_aendern("haut", bj::value(wert(haut_d)));
        });
        ueber_auto.signal_toggled().connect([this] {
            if (setzt) return;
            ueber_zeigen();
            zw_aendern("ueber_trenn_auto", bj::value(ueber_auto.get_active()), false);
        });
        ueber_1->signal_value_changed().connect([this] {
            if (setzt) return;
            ueber_zeigen();
            zw_aendern("ueber_trenn_1", bj::value(wert(ueber_1)), false);
        });
        ueber_2->signal_value_changed().connect([this] {
            if (setzt) return;
            ueber_zeigen();
            zw_aendern("ueber_trenn_2", bj::value(wert(ueber_2)), false);
        });
        steg_b->signal_value_changed().connect([this] {
            if (setzt) return;
            zw_aendern("steg_breite", bj::value(wert(steg_b)), false);
            breite_setzen(wert(steg_b));
        });
    }

    HirchFraesen *fraesen()
    {
        auto desk = panel.getDesktop();
        auto c = desk ? desk->getContainer() : nullptr;
        return c ? dynamic_cast<HirchFraesen *>(c->get_dialog("Fraesen")) : nullptr;
    }

    /// Stelle der letzten Kollision (Spindel/Halterung, Ergebnis «kollisionen») als roter Klotz:
    /// 4 × 4 mm, von der Unterkante des streifenden Körpers bis zur Oberkante des Materials dort
    /// Modell (aus) oder Fräsergebnis (an) in der 3D-Ansicht
    /// Klick ins Fräsergebnis: Stelle, Höhe, Unterschied zum Modell
    void klick_zeigen(double x, double y, double z, double m)
    {
        klick_x = x;
        klick_y = y;
        std::string t = TRF("Bei X {} · Y {}: gefräst bis Z {} mm", {mm(x, 1), mm(y, 1), mm(z, 2)});
        bool rest = false;
        if (!std::isfinite(m)) {
            t += "\n" + std::string(TR("Neben dem Modell."));
        } else {
            double diff = z - m;
            if (diff > 0.05) {
                t += "\n" + TRF("Hier bleiben {} mm über dem Modell stehen.", {mm(diff, 2)});
                rest = true;
            } else if (diff < -0.05) {
                t += "\n" + TRF("Hier ist {} mm zu tief gefräst.", {mm(-diff, 2)});
            } else {
                t += "\n" + std::string(TR("Hier stimmt das Mass."));
            }
        }
        klick_text.set_text(t);
        klick_box.show();
        klick_text.show();
        klick_rest.set_visible(rest);
        klick_umriss.hide();
    }

    // ------------------------------------------------------------------ Flächen wählen

    void flaechen_bauen()
    {
        fl_box.set_no_show_all(true);
        klasse(fl_box, "hirch-karte");
        fl_kopf.set_xalign(0);
        klasse(fl_kopf, "hirch-titel");
        fl_box.add(fl_kopf);
        fl_text.set_xalign(0);
        fl_text.set_line_wrap(true);
        fl_text.set_max_width_chars(44);
        klasse(fl_text, "hirch-dim");
        klasse(fl_text, "hirch-klein");
        fl_box.add(fl_text);
        fl_box.add(fl_liste);
        // Netz (STL/OBJ/Bild): wie ähnlich geneigt eine Fläche zusammenhängt
        fl_winkel_zeile = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        fl_winkel_zeile->add(*label(TR("Ähnlich geneigt bis")));
        fl_winkel = Gtk::make_managed<Gtk::SpinButton>();
        fl_winkel->set_range(1, 60);
        fl_winkel->set_increments(1, 5);
        fl_winkel->set_digits(0);
        fl_winkel->set_value(Inkscape::Preferences::get()->getDouble("/hirch/3d/flaechen_winkel", 5.0));
        fl_winkel->set_tooltip_text(TR("Klein: gewölbte Flächen zerfallen in Stücke – gut für ebene Flächen. Gross: "
                                       "gewölbte Flächen kommen ganz, angrenzende können mitkommen."));
        fl_winkel_zeile->add(*fl_winkel);
        fl_winkel_zeile->add(*label("°", "hirch-dim"));
        fl_winkel_zeile->set_no_show_all(true);
        fl_box.add(*fl_winkel_zeile);
        fl_winkel->signal_value_changed().connect([this] {
            Inkscape::Preferences::get()->setDouble("/hirch/3d/flaechen_winkel", wert(fl_winkel));
            flaechen_neu_holen();
        });
        // Ziel: welche 3D-Bearbeitung die Flächen begrenzen, nur diese oder alles ausser diesen
        auto ziel_zeile = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        ziel_zeile->add(*label(TR("Als Bereich für")));
        fl_ziel.set_hexpand(true);
        ziel_zeile->add(fl_ziel);
        fl_box.add(*ziel_zeile);
        fl_art.append("flaechen", TR("nur diese Flächen"));
        fl_art.append("ohne_flaechen", TR("alles ausser diesen Flächen"));
        fl_art.set_active_id("flaechen");
        fl_art.set_halign(Gtk::ALIGN_END);
        fl_box.add(fl_art);
        auto knoepfe = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
        fl_leeren.set_label(TR("Alles abwählen"));
        fl_umriss.set_label(TR("Umriss in die Zeichnung"));
        fl_umriss.set_tooltip_text(TR("Die gewählten Flächen kommen als eine schwarze Linie in die Zeichnung (aneinander"
                                      "grenzende Flächen ohne Naht)."));
        fl_bereich.set_label(TR("Gewählte Flächen als Bereich"));
        fl_bereich.set_tooltip_text(TR("Die Bearbeitung fräst nur auf diesen Flächen (bzw. überall ausser dort). Die "
                                       "Flächen kommen als eine Linie in der Farbe der Bearbeitung in die Zeichnung "
                                       "und ersetzen ihre bisher gewählten Flächen."));
        klasse(fl_bereich, "suggested-action");
        fl_bereich.set_halign(Gtk::ALIGN_END);
        knoepfe->add(fl_leeren);
        knoepfe->add(fl_umriss);
        fl_box.add(*knoepfe);
        fl_box.add(fl_bereich);                    // eigene Zeile: die Spalte bleibt schmal
        fl_leeren.signal_clicked().connect([this] {
            fl_wahl.clear();
            flaechen_zeigen();
            zeile_zeigen(&fl_box, false);
        });
        fl_umriss.signal_clicked().connect([this] { flaechen_vereinigen(false); });
        fl_bereich.signal_clicked().connect([this] { flaechen_vereinigen(true); });
    }

    std::vector<std::string> flaechen_args(double x, double y, double z, int seite)
    {
        auto it = item();
        auto d = panel.getDocument();
        if (!it || !d || daten_datei.empty()) return {};
        Material m = material(d, item());
        return {"flaeche", "--daten", daten_datei, "--info", bj::serialize(info),
                "--ecken", bj::serialize(bild_ecken(it, m, MM_PX)), "--dicke", attr_zahl(m.d),
                "--x", attr_zahl(x), "--y", attr_zahl(y), "--z", attr_zahl(z), "--seite", std::to_string(seite),
                "--winkel", attr_zahl(fl_winkel ? wert(fl_winkel) : 5.0),
                "--material", attr_zahl(m.b) + "," + attr_zahl(m.h), "--achse", achse_jetzt()};
    }

    static GewFlaeche flaeche_aus(bj::object const &e, double x, double y, double z)
    {
        GewFlaeche f;
        f.id = str(e, "id");
        f.name = str(e, "name");
        f.seite = (int)num(e, "seite", 1);
        if (auto u = liste_von(e, "umriss")) f.umriss = *u;
        if (auto l = liste_von(e, "linien")) f.linien = *l;
        f.flaeche = num(e, "flaeche", 0);
        f.wand = flag(e, "wand", false);
        f.x = x;
        f.y = y;
        f.z = z;
        return f;
    }

    /// Klick auf das Modell: Fläche an dieser Stelle holen (STEP genau, Netz: ähnlich geneigt). Ohne
    /// Umschalt ersetzt sie die Wahl, mit Umschalt kommt sie dazu (oder fällt weg, wenn schon gewählt).
    void flaeche_holen(double x, double y, double z, bool umschalt, int seite)
    {
        auto args = flaechen_args(x, y, z, seite);
        if (args.empty()) return;
        std::string ziel = Glib::build_filename(ordner, "fl-" + std::to_string(++gen_stege));
        g_mkdir_with_parents(ziel.c_str(), 0700);
        fl_text.set_text(TRF("Bei X {} · Y {} · Z {} mm: Fläche wird gesucht …", {mm(x, 1), mm(y, 1), mm(z, 2)}));
        zeile_zeigen(&fl_box, true);
        if (lauf_flaeche) *lauf_flaeche = false;
        unsigned g = ++fl_gen;
        lauf_flaeche = kern(args, ziel, [this, ziel, x, y, z, umschalt, g](bool ok, bj::object const &e,
                                                                          std::string const &fehler) {
            ordner_loeschen_unter(ziel);
            if (g != fl_gen) return;
            if (!ok || !flag(e, "ok", false)) {
                fl_text.set_text(warnungen(e).empty() ? fehler : warnungen(e));
                return;
            }
            auto f = flaeche_aus(e, x, y, z);
            if (f.umriss.empty()) {
                fl_text.set_text(warnungen(e).empty() ? std::string(TR("Nichts gefunden.")) : warnungen(e));
                return;
            }
            std::string hinweis;
            if (!umschalt || f.seite != fl_seite) {
                if (umschalt && !fl_wahl.empty())
                    hinweis = TR("Flächen beider Seiten lassen sich nicht zusammen wählen – neu auf dieser Seite.");
                fl_wahl.clear();
            }
            fl_seite = f.seite;
            auto da = std::find_if(fl_wahl.begin(), fl_wahl.end(), [&f](GewFlaeche const &q) { return q.id == f.id; });
            if (da != fl_wahl.end()) fl_wahl.erase(da);         // Umschalt+Klick auf eine gewählte: weg
            else fl_wahl.push_back(std::move(f));
            flaechen_zeigen(hinweis);
        });
    }

    /// Winkel geändert: die Flächen aus Netzen (STL/OBJ/Bild) an denselben Stellen neu holen
    void flaechen_neu_holen()
    {
        unsigned g = ++fl_gen;
        for (std::size_t k = 0; k < fl_wahl.size(); ++k) {
            auto const &f = fl_wahl[k];
            if (f.id.rfind("netz:", 0) != 0) continue;
            auto args = flaechen_args(f.x, f.y, f.z, f.seite);
            if (args.empty()) return;
            std::string ziel = Glib::build_filename(ordner, "fl-" + std::to_string(++gen_stege));
            g_mkdir_with_parents(ziel.c_str(), 0700);
            kern(args, ziel, [this, ziel, k, g](bool ok, bj::object const &e, std::string const &) {
                ordner_loeschen_unter(ziel);
                if (g != fl_gen || k >= fl_wahl.size() || !ok || !flag(e, "ok", false)) return;
                auto alt = fl_wahl[k];
                auto f = flaeche_aus(e, alt.x, alt.y, alt.z);
                if (f.umriss.empty()) return;
                fl_wahl[k] = std::move(f);
                flaechen_zeigen();
            });
        }
    }

    /// Bearbeitungen der Zeichnung (aus hirch:einstellungen)
    bj::array bearbeitungen_lesen()
    {
        auto d = panel.getDocument();
        auto a = d ? d->getReprRoot()->attribute("hirch:einstellungen") : nullptr;
        if (!a) return {};
        try {
            auto v = bj::parse(a);
            if (v.is_object())
                if (auto b = liste_von(v.as_object(), "bearbeitungen")) return *b;
        } catch (...) {
        }
        return {};
    }

    void flaechen_zeigen(std::string const &hinweis = {})
    {
        std::array<std::vector<std::vector<Geom::Point>>, 4> ringe;
        for (auto const &f : fl_wahl)
            for (auto const &l : f.umriss) {
                auto o = l.if_object();
                auto pts = o ? liste_von(*o, "punkte") : nullptr;
                if (!pts) continue;
                std::vector<Geom::Point> r;
                for (auto const &q : *pts)
                    if (auto a = q.if_array(); a && a->size() >= 2)
                        r.emplace_back((*a)[0].to_number<double>(), (*a)[1].to_number<double>());
                if (r.size() >= 3) ringe[(f.seite == 2 ? 2 : 0) + (f.wand ? 1 : 0)].push_back(std::move(r));
            }
        ansicht.hervorheben(std::move(ringe));
        for (auto k : fl_liste.get_children()) fl_liste.remove(*k);   // verwaltet: entfernt = zerstört
        bool netz = str(info, "art") != "step";
        zeile_zeigen(fl_winkel_zeile, netz);
        if (fl_wahl.empty()) {
            fl_kopf.set_text(TR("Flächen wählen"));
            fl_text.set_text(hinweis.empty() ? std::string(TR("Rechtsklick auf das Modell in der 3D-Ansicht: «Fläche des Modells wählen» oder «Fläche dazu wählen» "
                                                                           "(nochmals: wieder weg)."))
                                             : hinweis);
            for (auto w : std::initializer_list<Gtk::Widget *>{&fl_ziel, &fl_art, &fl_leeren, &fl_umriss, &fl_bereich})
                w->set_sensitive(false);
            fl_liste.show_all();
            return;
        }
        double summe = 0;
        for (auto const &f : fl_wahl) summe += f.flaeche;
        fl_kopf.set_text(TRF("Gewählt: {} · Seite {}", {anzahl(long(fl_wahl.size()), TR("Fläche"), TR("Flächen")),
                                                         std::to_string(fl_seite)}));
        fl_text.set_text((hinweis.empty() ? std::string() : hinweis + "\n") +
                         TRF("zusammen {} mm² · in der 3D-Ansicht Rechtsklick › «Fläche dazu wählen» nimmt weitere dazu oder wieder weg", {mm(summe, 0)}));
        for (std::size_t k = 0; k < fl_wahl.size(); ++k) {
            auto zeile = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
            auto l = label(fl_wahl[k].name + " · " + mm(fl_wahl[k].flaeche, 0) + " mm²");
            l->set_hexpand(true);
            l->set_ellipsize(Pango::ELLIPSIZE_END);
            zeile->add(*l);
            auto weg = Gtk::make_managed<Gtk::Button>("×");
            weg->set_relief(Gtk::RELIEF_NONE);
            weg->set_tooltip_text(TR("Diese Fläche abwählen"));
            auto id = fl_wahl[k].id;
            weg->signal_clicked().connect([this, id] {
                Glib::signal_idle().connect_once([this, id] {
                    fl_wahl.erase(std::remove_if(fl_wahl.begin(), fl_wahl.end(),
                                                 [&id](GewFlaeche const &q) { return q.id == id; }),
                                  fl_wahl.end());
                    flaechen_zeigen();
                });
            });
            zeile->add(*weg);
            fl_liste.add(*zeile);
        }
        fl_liste.show_all();
        // Ziel: 3D-Bearbeitungen dieser Seite, dazu «Neues 3D-Schlichten»
        auto alt = fl_ziel.get_active_id();
        fl_ziel.remove_all();
        for (auto const &v : bearbeitungen_lesen()) {
            auto b = v.if_object();
            if (!b) continue;
            auto art = str(*b, "art");
            if (art.rfind("relief_", 0) != 0 || (int)num(*b, "auf_seite", 1) != fl_seite) continue;
            auto t = str(*b, "titel");
            if (t.empty()) t = art == "relief_schruppen" ? TR("3D-Schruppen") : TR("3D-Schlichten");
            fl_ziel.append(str(*b, "id"), t);
        }
        fl_ziel.append("neu", TR("Neues 3D-Schlichten"));
        if (alt.empty() || !fl_ziel.set_active_id(alt)) fl_ziel.set_active(0);
        for (auto w : std::initializer_list<Gtk::Widget *>{&fl_ziel, &fl_art, &fl_leeren, &fl_umriss, &fl_bereich})
            w->set_sensitive(true);
    }

    /// Gewählte Flächen vereinigen; als Umriss (schwarz) oder als Bereich einer 3D-Bearbeitung
    void flaechen_vereinigen(bool als_bereich)
    {
        auto d = panel.getDocument();
        if (!d || fl_wahl.empty()) return;
        Material m = material(d, item());
        bj::array umrisse;
        for (auto const &f : fl_wahl) umrisse.push_back(f.umriss);
        std::string ziel = Glib::build_filename(ordner, "fv-" + std::to_string(++gen_stege));
        g_mkdir_with_parents(ziel.c_str(), 0700);
        auto datei = Glib::build_filename(ziel, "umrisse.json");
        try {
            Glib::file_set_contents(datei, bj::serialize(umrisse));
        } catch (...) {
            return;
        }
        std::vector<std::string> args = {"flaeche", "--vereinigen", datei, "--seite", std::to_string(fl_seite),
                                         "--material", attr_zahl(m.b) + "," + attr_zahl(m.h), "--achse", achse_jetzt()};
        auto bid = fl_ziel.get_active_id().raw();
        auto art = fl_art.get_active_id().raw();
        int seite = fl_seite;
        long n = long(fl_wahl.size());
        fl_text.set_text(TR("Flächen werden vereinigt …"));
        kern(args, ziel, [this, ziel, als_bereich, bid, art, seite, n](bool ok, bj::object const &e,
                                                                      std::string const &fehler) {
            ordner_loeschen_unter(ziel);
            auto l = liste_von(e, "linien");
            if (!ok || !l || l->empty()) {
                fl_text.set_text(warnungen(e).empty() ? fehler : warnungen(e));
                return;
            }
            bj::array linien = *l;
            std::string titel = TRF("Flächen ({}) {}", {std::to_string(n), str(info, "name")});
            if (!als_bereich) {
                linien_einfuegen(linien, titel, TR("Umriss der Flächen"));
                flaechen_zeigen();
                return;
            }
            auto fr = fraesen();
            if (!fr) return;
            std::string id = bid, farbe;
            if (bid == "neu" || bid.empty()) {
                bj::object op;
                op["art"] = "relief_schlichten";
                op["vorlage"] = "letzte";
                op["bereich"] = art;
                op["bereich_ueberlauf"] = 1.0;
                op["auf_seite"] = seite;
                op["titel"] = TRF("3D-Schlichten · {}", {anzahl(n, TR("Fläche"), TR("Flächen"))});
                bj::array ops;
                ops.push_back(op);
                auto farben = fr->bearbeitungenAnlegen(bj::serialize(ops));
                farbe = farben.empty() ? std::string() : farben[0];
                // die neue Bearbeitung ist die letzte mit dieser Farbe
                for (auto const &v : bearbeitungen_lesen())
                    if (auto b = v.if_object(); b && str(*b, "farbe") == farbe) id = str(*b, "id");
            } else {
                for (auto const &v : bearbeitungen_lesen())
                    if (auto b = v.if_object(); b && str(*b, "id") == bid) farbe = str(*b, "farbe");
                fr->bereichSetzen(bid, art);
            }
            if (farbe.empty() || id.empty() || id == "neu") {
                fl_text.set_text(TR("Die Bearbeitung wurde nicht gefunden."));
                return;
            }
            for (auto &v : linien)
                if (auto o = v.if_object()) (*o)["farbe"] = farbe;
            // bisherige Flächen dieser Bearbeitung ersetzen
            if (auto doc = panel.getDocument()) {
                std::vector<SPObject *> weg;
                std::function<void(Inkscape::XML::Node *)> suchen = [&](Inkscape::XML::Node *k) {
                    for (auto c = k->firstChild(); c; c = c->next()) {
                        if (auto a = c->attribute("hirch:flaechen")) {
                            try {
                                auto v = bj::parse(a);
                                if (v.is_object() && str(v.as_object(), "bearbeitung") == id)
                                    if (auto o = doc->getObjectByRepr(c)) weg.push_back(o);
                            } catch (...) {
                            }
                            continue;
                        }
                        suchen(c);
                    }
                };
                suchen(doc->getReprRoot());
                for (auto o : weg) o->deleteObject(false);
            }
            bj::object merk;
            merk["bearbeitung"] = id;
            merk["relief"] = item_id;
            merk["seite"] = seite;
            merk["anzahl"] = n;
            linien_einfuegen(linien, titel, TR("Gewählte Flächen als Bereich"), bj::serialize(merk), id);
            fl_text.set_text(TRF("Bereich gesetzt: {} – neu rechnen, um die Bahnen zu sehen.",
                                 {anzahl(n, TR("Fläche"), TR("Flächen"))}));
        });
    }

    /// 3D-Schlichten «nur Restmaterial» in einem Kreis (Ø 20 mm) um die angeklickte Stelle anlegen
    void rest_hier()
    {
        auto d = panel.getDocument();
        auto fr = fraesen();
        if (!d || !fr) return;
        bj::object op;
        op["art"] = "relief_schlichten";
        op["vorlage"] = "letzte";                // wie das letzte 3D-Schlichten (Werkzeug, Bahnabstand …)
        op["rest"] = true;
        op["rest_schwelle"] = 0.1;
        op["bereich"] = "innen";
        op["bereich_ueberlauf"] = 1.0;
        op["titel"] = TRF("Restmaterial bei X {} · Y {}", {mm(klick_x, 0), mm(klick_y, 0)});
        op["oeffnen"] = true;
        bj::array ops;
        ops.push_back(op);
        auto farben = fr->bearbeitungenAnlegen(bj::serialize(ops));
        bj::array pts;
        for (int k = 0; k <= 72; ++k) {
            double t = 2 * M_PI * k / 72;
            pts.push_back(bj::array{klick_x + 10 * std::cos(t), klick_y + 10 * std::sin(t)});
        }
        bj::object l;
        l["punkte"] = pts;
        l["geschlossen"] = true;
        l["farbe"] = !farben.empty() && !farben[0].empty() ? farben[0] : "#000000";
        bj::array linien;
        linien.push_back(l);
        linien_einfuegen(linien, TR("Restmaterial"), TR("Restmaterial hier nachfräsen"));
        klick_box.hide();
    }

    void modus_setzen(bool ergebnis)
    {
        if (ergebnis)                          // Reiter erst jetzt offen: wartet das Panel auf «Rechnen»?
            if (auto f = fraesen()) {
                panel_wartet = f->wartetAufKnopf();
                if (panel_wartet && wartet_text.empty()) wartet_text = TR("Noch nicht gerechnet");
            }
        if (schaltet) return;
        schaltet = true;
        t_modell.set_active(!ergebnis);
        t_ergebnis.set_active(ergebnis);
        schaltet = false;
        ansicht.ergebnis_zeigen(ergebnis);
        erg_status.set_visible(ergebnis);
        klick_box.hide();
        zeile_zeigen(&fl_box, !ergebnis && item() && !fl_wahl.empty());
        pille_zeigen();
        if (legende_l)
            legende_l->set_text(ergebnis ? TR("Holz: gefräst wie das Modell · orange: bleibt stehen (mehr als 0,5 mm) · "
                                              "rot: zu tief ins Modell · Ziehen dreht, Rad zoomt, Doppelklick stellt zurück")
                                         : TR("Grau: Modell · gelb: Haltestege · blau: Werkstück · rot: ragt heraus oder "
                                              "wird unten abgeschnitten · Ziehen dreht, Rad zoomt, Doppelklick stellt zurück"));
        if (legende_l && ergebnis)
            legende_l->set_text(legende_l->get_text() + TR(" · Klick zeigt, was an der Stelle steht"));
        if (legende_l && !ergebnis)
            legende_l->set_text(legende_l->get_text() + TR(" · Klick wählt eine Fläche, Umschalt+Klick weitere"));
        if (ergebnis && erg_alt) ergebnis_rechnen();
    }

    static std::string uhr(double s)
    {
        int t = std::max(0, (int)std::floor(s));
        char buf[32];
        std::snprintf(buf, sizeof buf, "%d:%02d", t / 60, t % 60);
        return buf;
    }

    /// Uhr über der Ansicht: nur im Fräsergebnis, solange das Panel rechnet oder simuliert wird. Ohne
    /// drehenden Kreisel und nur bei neuem Text: jede Änderung zeichnet die 3D-Ansicht darunter neu
    void pille_zeigen()
    {
        bool an = t_ergebnis.get_active() && (erg_laeuft || panel_rechnet || panel_wartet);
        static char const *const ZEIGER[] = {"◴", "◷", "◶", "◵"};
        std::string t;
        bool knopf_rechnen = false;
        if (erg_laeuft) {
            double s = (g_get_monotonic_time() - sim_start) / 1e6;
            t = std::string(ZEIGER[int(s) % 4]) + "  <b>" + markup_escape(TR("Simuliert …")) + "</b> " + uhr(s);
        } else if (panel_rechnet) {
            t = "<b>" + markup_escape(panel_text) + "</b>";
        } else if (panel_wartet) {
            t = markup_escape(wartet_text);
            knopf_rechnen = true;
        }
        if (t != pille_alt) {
            pille_alt = t;
            pille_text.set_markup(t);
            pille_knopf.set_label(knopf_rechnen ? TR("Jetzt rechnen") : TR("Abbrechen"));
            if (knopf_rechnen) pille_knopf.get_style_context()->add_class("suggested-action");
            else pille_knopf.get_style_context()->remove_class("suggested-action");
        }
        if (an) {
            // no_show_all: show_all() würde die Abdunkelung überspringen – Teile einzeln zeigen
            pille_text.show();
            pille_knopf.show_all();
            pille.show();
            dunkel.show();
        } else {
            dunkel.hide();
        }
    }

    void wartet(bool an, std::string const &text)
    {
        panel_wartet = an;
        wartet_text = text;
        pille_zeigen();
    }

    void rechenstand(bool laeuft, std::string const &text, std::string const &zeit)
    {
        static char const *const ZEIGER[] = {"◴", "◷", "◶", "◵"};
        int sek = 0;
        if (auto p = zeit.find(':'); p != std::string::npos) sek = std::atoi(zeit.c_str() + p + 1);
        panel_rechnet = laeuft;
        panel_text = std::string(ZEIGER[sek % 4]) + "  " + text + " " + zeit;
        pille_zeigen();
    }

    /// Neue Rechnung im Panel: Markierung nachführen, Fräsergebnis gilt nicht mehr
    void rechnung_neu(std::string const &json)
    {
        rechnung = json;
        std::string koll;
        bj::value v;
        boost::system::error_code ec;
        if (!json.empty()) v = bj::parse(json, ec);
        if (!ec && v.is_object())
            if (auto k = liste_von(v.as_object(), "kollisionen")) koll = bj::serialize(*k);
        kollision_zeigen(koll);
        erg_alt = true;
        if (t_ergebnis.get_active()) ergebnis_rechnen();
    }

    void ergebnis_rechnen()
    {
        if (rechnung.empty())
            if (auto f = fraesen()) rechnung = f->rechnungFuer3D();
        bj::object r;
        boost::system::error_code ec;
        auto v = rechnung.empty() ? bj::value() : bj::parse(rechnung, ec);
        if (!ec && v.is_object()) r = v.as_object();
        auto pr = liste_von(r, "programme");
        if (!flag(r, "ok", false) || !pr || pr->empty()) {
            erg_status.set_text(liste_von(r, "kollisionen")
                                    ? TR("Die letzte Rechnung wurde wegen einer Kollision beendet – kein Fräsergebnis.")
                                    : TR("Noch keine fertige Rechnung – das Fräsergebnis erscheint, sobald das Panel "
                                         "«Fräsen» gerechnet hat."));
            ansicht.ergebnis({}, {});
            return;
        }
        auto ziel = Glib::build_filename(ordner, "ergebnis");
        g_mkdir_with_parents(ziel.c_str(), 0700);
        auto pdatei = Glib::build_filename(ziel, "programme.json");
        try {
            Glib::file_set_contents(pdatei, bj::serialize(*pr));
        } catch (...) {
            return;
        }
        erg_alt = false;
        erg_laeuft = true;
        sim_start = g_get_monotonic_time();
        sim_takt.disconnect();
        sim_takt = Glib::signal_timeout().connect([this] {
            pille_zeigen();
            return erg_laeuft;
        }, 250);
        pille_zeigen();
        unsigned g = ++gen_erg;
        erg_status.set_text(TR("Simuliert …"));
        lauf_erg = kern({"ergebnis", str(r, "svg"), "--programme", pdatei, "--ordner", ziel, "--genauigkeit",
                         genau.get_active_id().empty() ? std::string("mittel") : genau.get_active_id().raw(),
                         "--platte", std::to_string((int)num(r, "platte", 1))},
                        ordner, [this, g](bool ok, bj::object const &e, std::string const &fehler) {
            if (g != gen_erg) return;
            erg_laeuft = false;
            pille_zeigen();
            if (!ok || !flag(e, "ok", false)) {
                auto w = warnungen(e);
                erg_status.set_text(w.empty() ? TR("Fräsergebnis fehlgeschlagen: ") + fehler.substr(0, 300) : w);
                erg_alt = true;
                return;
            }
            ergebnis_laden(str(e, "datei"));
            std::string t = TRF("Raster {} mm", {mm(num(e, "zelle", 0.25), 2)});
            double tief = num(e, "zu_tief", 0), rest = num(e, "anteil_rest", 0);
            t += " · " + (tief > 0 ? TRF("zu tief: bis {} mm", {mm(tief, 2)}) : std::string(TR("nirgends zu tief")));
            t += " · " + (rest > 0 ? TRF("bleibt stehen: {} % der Fläche (bis {} mm)",
                                         {mm(rest, 1), mm(num(e, "rest", 0), 1)})
                                   : std::string(TR("nichts bleibt stehen")));
            t += " · " + mm(num(e, "sekunden", 0), 1) + " s";
            if (auto h = hinweise(e); !h.empty()) t += "\n" + h;
            erg_status.set_text(t);
        });
    }

    /// Binärdatei aus relief_ergebnis.py → Gitternetz (Oberseite und Unterseite) für die Ansicht
    void ergebnis_laden(std::string const &datei)
    {
        std::istringstream f(datei_lesen(datei), std::ios::binary); // Umlaute im Pfad unter Windows (B-11)
        int32_t nx = 0, ny = 0;
        float x0 = 0, y0 = 0, z = 1;
        f.read(reinterpret_cast<char *>(&nx), 4);
        f.read(reinterpret_cast<char *>(&ny), 4);
        f.read(reinterpret_cast<char *>(&x0), 4);
        f.read(reinterpret_cast<char *>(&y0), 4);
        f.read(reinterpret_cast<char *>(&z), 4);
        if (!f || nx < 2 || ny < 2 || (int64_t)nx * ny > 20'000'000) return;
        std::size_t n = std::size_t(nx) * ny;
        std::vector<float> oben(n), unten(n);
        std::vector<uint8_t> ko(n), ku(n);
        f.read(reinterpret_cast<char *>(oben.data()), std::streamsize(4 * n));
        f.read(reinterpret_cast<char *>(unten.data()), std::streamsize(4 * n));
        f.read(reinterpret_cast<char *>(ko.data()), std::streamsize(n));
        f.read(reinterpret_cast<char *>(ku.data()), std::streamsize(n));
        if (!f) return;
        // Oberfläche des Modells (neuere Dateien) – für den Klick ins Fräsergebnis
        std::vector<float> mo(n, std::numeric_limits<float>::quiet_NaN());
        f.read(reinterpret_cast<char *>(mo.data()), std::streamsize(4 * n));
        if (!f) std::fill(mo.begin(), mo.end(), std::numeric_limits<float>::quiet_NaN());
        ansicht.raster = {nx, ny, x0, y0, z, oben, mo};
        std::vector<float> e;
        e.reserve(2 * n * 6);
        auto ix = [nx](int i, int j) { return std::size_t(j) * nx + i; };
        for (int seite = 0; seite < 2; ++seite) {
            auto const &h = seite == 0 ? oben : unten;
            float vz = seite == 0 ? 1.f : -1.f;
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    int i0 = std::max(i - 1, 0), i1 = std::min(i + 1, nx - 1);
                    int j0 = std::max(j - 1, 0), j1 = std::min(j + 1, ny - 1);
                    float gx = (h[ix(i1, j)] - h[ix(i0, j)]) / (z * float(std::max(i1 - i0, 1)));
                    float gy = (h[ix(i, j1)] - h[ix(i, j0)]) / (z * float(std::max(j1 - j0, 1)));
                    e.insert(e.end(), {x0 + i * z, y0 + j * z, h[ix(i, j)], -gx * vz, -gy * vz, vz});
                }
        }
        std::array<std::vector<unsigned>, 3> idx;
        for (int seite = 0; seite < 2; ++seite) {
            auto const &k = seite == 0 ? ko : ku;
            unsigned basis = seite == 0 ? 0u : unsigned(n);
            for (int j = 0; j + 1 < ny; ++j)
                for (int i = 0; i + 1 < nx; ++i) {
                    std::size_t a = ix(i, j), b = ix(i + 1, j), c = ix(i + 1, j + 1), d = ix(i, j + 1);
                    bool durch = false;
                    for (auto q : {a, b, c, d})
                        if (oben[q] <= unten[q] + 0.01f) durch = true;
                    if (durch) continue;                 // ganz durchgefräst: dort ist nichts
                    int kl = std::min<int>(2, std::max({k[a], k[b], k[c], k[d]}));
                    auto &l = idx[kl];
                    l.insert(l.end(), {basis + unsigned(a), basis + unsigned(b), basis + unsigned(c),
                                       basis + unsigned(a), basis + unsigned(c), basis + unsigned(d)});
                }
        }
        ansicht.ergebnis(std::move(e), std::move(idx));
    }

    void kollision_zeigen(std::string const &json)
    {
        std::vector<float> e;
        bj::value v;
        boost::system::error_code ec;
        if (!json.empty()) v = bj::parse(json, ec);
        if (!ec && v.is_array() && !v.as_array().empty()) {
            if (auto k = v.as_array()[0].if_object()) {
                auto ort = liste_von(*k, "ort");
                auto hoehe = liste_von(*k, "hoehe");
                if (ort && ort->size() == 2 && hoehe && hoehe->size() == 2) {
                    auto d = panel.getDocument();
                    Material m = material(d, item());
                    double x = (*ort)[0].to_number<double>(), y = (*ort)[1].to_number<double>();
                    double z0 = (*hoehe)[0].to_number<double>(), z1 = (*hoehe)[1].to_number<double>();
                    if ((int)num(*k, "seite", 1) == 2) {          // Seite 2: gewendet
                        if (achse_jetzt() == "y") x = m.b - x;
                        else y = m.h - y;
                        double a0 = -m.d - z1, a1 = -m.d - z0;
                        z0 = a0;
                        z1 = a1;
                    }
                    if (z1 - z0 < 1.0) z1 = z0 + 1.0;
                    double r = 2.0;
                    quader(e, {Geom::Point(x - r, y - r), Geom::Point(x + r, y - r), Geom::Point(x + r, y + r),
                               Geom::Point(x - r, y + r)}, z0, z1);
                }
            }
        }
        ansicht.marke(std::move(e));
    }

    /// Wendeachse aus den Einstellungen der Zeichnung
    std::string achse_jetzt()
    {
        auto d = panel.getDocument();
        auto a = d ? d->getReprRoot()->attribute("hirch:einstellungen") : nullptr;
        if (a) {
            try {
                auto v = bj::parse(a);
                if (v.is_object())
                    if (auto z = unter(v.as_object(), "zweiseitig")) return str(*z, "achse", "x");
            } catch (...) {
            }
        }
        return "x";
    }

    /// Spalt: Haut an (true) oder durchfräsen; die Knöpfe schliessen sich gegenseitig aus
    void haut_setzen(bool an)
    {
        bool alt = setzt;
        setzt = true;
        spalt_durch.set_active(!an);
        spalt_haut.set_active(an);
        setzt = alt;
        zeile_zeigen(haut_d, an);
        haut_text();
        ueber_zeigen();
        zeilen_ids.clear();                    // Hinweis bei leerer Stegliste anpassen
        zw_aendern("haut_an", bj::value(an));
    }

    void haut_text()
    {
        haut_hinweis.set_text(spalt_haut.get_active()
            ? TRF("Rundherum bleibt mitten auf der Trennlinie eine Haut von {} mm stehen – mit dem Messer "
                  "durchschneiden. Jede Seite fräst dafür {} mm weniger tief. Die Stege bleiben dazu.",
                  {mm(wert(haut_d), 2), mm(wert(haut_d) / 2, 2)})
            : TR("Die Seiten treffen sich – nur die Haltestege halten das Teil."));
    }

    /// «Über die Trennlinie hinaus»: nur beim Durchfräsen; die Felder je Seite nur von Hand
    void ueber_zeigen()
    {
        bool durch = !spalt_haut.get_active();
        bool hand = !ueber_auto.get_active();
        zeile_zeigen(&ueber_auto, durch);
        zeile_zeigen(ueber_1, durch && hand);
        zeile_zeigen(ueber_2, durch && hand);
    }

    void zweiseitig_laden()
    {
        auto z = zw_info();
        bool an = flag(z, "an", false);
        zwei_an.set_active(an);
        achse.set_active_id(achse_jetzt());
        trennung.set_active_id(str(z, "trennung", "trennlinie"));
        ebene_z->set_value(num(z, "z", 0));
        steg_b->set_value(num(z, "steg_breite", 8));
        steg_d->set_value(num(z, "steg_dicke", 3));
        steg_n->set_value(num(z, "steg_anzahl", 4));
        {
            bool alt = setzt;
            setzt = true;
            bool h = flag(z, "haut_an", false);
            spalt_durch.set_active(!h);
            spalt_haut.set_active(h);
            haut_d->set_value(num(z, "haut", 0.5));
            ueber_auto.set_active(z.find("ueber_trenn_auto") == z.end() || flag(z, "ueber_trenn_auto", true));
            ueber_1->set_value(num(z, "ueber_trenn_1", 3));
            ueber_2->set_value(num(z, "ueber_trenn_2", 3));
            setzt = alt;
            zeile_zeigen(haut_d, h);
            haut_text();
            ueber_zeigen();
        }
        zeile_zeigen(&zwei_box, an);
        zeile_zeigen(x_stege, an);
        zeile_zeigen(ebene_z, trennung.get_active_id() == "ebene");
    }

    /// Einstellung des Modells ändern (hirch:relief › zweiseitig), Rückgängig möglich
    void zw_aendern(char const *k, bj::value v, bool neu_rechnen = true)
    {
        auto it = item();
        auto d = panel.getDocument();
        if (!it || !d) return;
        auto z = zw_info();
        z[k] = v;
        info["zweiseitig"] = z;
        attr_alt = bj::serialize(info);
        it->getRepr()->setAttribute(RELIEF, attr_alt);
        DocumentUndo::done(d, TR("3D-Modell ändern"), "");
        if (neu_rechnen) stege_rechnen(0);
    }

    void zweiseitig_setzen(bool an)
    {
        zw_aendern("an", bj::value(an), false);
        Glib::signal_idle().connect_once([this] { seite2_bild(); });
        zeile_zeigen(&zwei_box, an);
        zeile_zeigen(x_stege, an);
        if (an) {
            if (auto fr = fraesen()) fr->reliefZweiseitig(true, achse.get_active_id().raw());
            setzt = true;
            achse.set_active_id(achse_jetzt());
            setzt = false;
        } else if (!anderes_relief_zweiseitig(panel.getDocument(), item())) {
            // das letzte zweiseitige Modell: auch die Zeichnung wieder einseitig (wie «Zweiseitig» im Auftrag)
            if (auto fr = fraesen()) fr->reliefZweiseitig(false, "");
        }
        stege_rechnen(an && stege().empty() ? std::max(1, (int)num(zw_info(), "steg_anzahl", 4)) : 0);
    }

    /// Haltestege dieses Modells (auf der ganzen Zeichnung)
    std::vector<SPItem *> stege()
    {
        std::vector<SPItem *> alle, aus;
        auto d = panel.getDocument();
        if (!d || !d->getRoot()) return aus;
        stege_sammeln_in(d->getRoot(), alle);
        auto modelle = reliefs(d);
        for (auto s : alle) {
            auto w = steg_werte(s);
            auto r = str(w, "relief");
            bool gibts = false;
            for (auto m : modelle)
                if (m->getId() && r == m->getId()) gibts = true;
            // ohne (gültige) Zuordnung: zum einzigen Modell
            if (r == item_id || (!gibts && modelle.size() == 1)) aus.push_back(s);
        }
        return aus;
    }

    void stege_entfernen()
    {
        auto d = panel.getDocument();
        auto st = stege();
        if (!d || st.empty()) return;
        for (auto s : st) s->deleteObject(true);
        DocumentUndo::done(d, TR("Haltestege entfernen"), "");
    }

    void stege_auto()
    {
        auto d = panel.getDocument();
        if (!d) return;
        for (auto s : stege()) {
            auto w = steg_werte(s);
            w["z"] = nullptr;
            s->getRepr()->setAttribute(STEG3D, bj::serialize(w));
        }
        DocumentUndo::done(d, TR("Haltestege auf die Trennlinie"), "");
        stege_rechnen(0);
    }

    void breite_setzen(double b_mm)
    {
        auto d = panel.getDocument();
        if (!d) return;
        double f = MM_PX;
        for (auto s : stege()) {
            auto r = s->getRepr();
            double k = s->i2doc_affine().descrim();
            if (k <= 1e-12) continue;
            auto z = [r](char const *a) {
                auto v = r->attribute(a);
                return v ? g_ascii_strtod(v, nullptr) : 0.0;
            };
            double w = z("width"), x = z("x"), wn = b_mm * f / k;
            r->setAttribute("x", attr_zahl(x + (w - wn) / 2));
            r->setAttribute("width", attr_zahl(wn));
        }
        DocumentUndo::done(d, TR("Haltestege ändern"), "");
        stege_rechnen(0);
    }

    /// Später (nach dem Verschieben eines Stegs) neu rechnen
    void stege_bald()
    {
        if (!flag(zw_info(), "an", false)) return;
        stege_spaeter.disconnect();
        stege_spaeter = Glib::signal_timeout().connect([this] {
            stege_rechnen(0);
            return false;
        }, 400);
    }

    /// Trennlinie und Höhen der Stege rechnen (Rechenkern «relief stege»); anzahl > 0: so
    /// viele neue Stege vorschlagen und setzen
    void stege_rechnen(int anzahl)
    {
        auto it = item();
        auto d = panel.getDocument();
        if (!it || !d || daten_datei.empty()) return;
        if (!flag(zw_info(), "an", false)) {
            ansicht.stege({});
            return;
        }
        Material m = material(d, item());
        double f = MM_PX;
        bj::array st;
        for (auto s : stege()) {
            bj::object o;
            o["id"] = s->getId() ? s->getId() : "";
            o["ecken"] = punkte_json(ecken_mm(s, m, f));
            auto w = steg_werte(s);
            o["z"] = w.contains("z") ? w.at("z") : bj::value(nullptr);
            st.push_back(o);
        }
        unsigned g = ++gen_stege;
        if (lauf_stege) *lauf_stege = false;
        std::string ziel = Glib::build_filename(ordner, "stege-" + std::to_string(g));
        g_mkdir_with_parents(ziel.c_str(), 0700);
        auto z = zw_info();
        std::vector<std::string> args = {"stege", "--daten", daten_datei, "--info", bj::serialize(info),
                                         "--ecken", bj::serialize(bild_ecken(it, m, f)),
                                         "--dicke", attr_zahl(m.d), "--stege", bj::serialize(st)};
        if (anzahl > 0) {
            args.insert(args.end(), {"--anzahl", std::to_string(anzahl), "--breite", attr_zahl(wert(steg_b)),
                                     "--laenge", attr_zahl(num(z, "steg_laenge", 30))});
        }
        stege_laeuft = true;
        lauf_stege = kern(args, ziel, [this, g, ziel, anzahl](bool ok, bj::object const &e, std::string const &) {
            ordner_loeschen_unter(ziel);
            if (g != gen_stege) return;
            stege_laeuft = false;
            if (!ok || !flag(e, "ok", false)) return;
            if (anzahl > 0) {
                if (auto v = liste_von(e, "vorschlag")) stege_anlegen(*v);
                stege_rechnen(0);
                return;
            }
            stege_zeigen(e);
        });
    }

    void stege_anlegen(bj::array const &vorschlag)
    {
        auto d = panel.getDocument();
        auto bild = item();
        if (!d || !bild || vorschlag.empty()) return;
        Material m = material(d, item());
        double f = MM_PX;
        auto xml = d->getReprDoc();
        auto eltern = bild->parent ? bild->parent : d->getRoot();
        for (auto const &vv : vorschlag) {
            auto v = vv.if_object();
            if (!v) continue;
            double b = num(*v, "breite", 8) * f, l = num(*v, "laenge", 30) * f;
            Geom::Point mitte(liste_zahl(*v, "mitte", 0), liste_zahl(*v, "mitte", 1));
            Geom::Point p = mitte * doc_zu_material(m, f).inverse();
            auto r = xml->createElement("svg:rect");
            r->setAttribute("id", d->generate_unique_id("steg3d"));
            r->setAttribute("x", attr_zahl(-b / 2));
            r->setAttribute("y", attr_zahl(-l / 2));
            r->setAttribute("width", attr_zahl(b));
            r->setAttribute("height", attr_zahl(l));
            r->setAttribute("style", "fill:#ffd21a;fill-opacity:0.6;stroke:#6b5800;stroke-width:0.6");
            bj::object w;
            w["relief"] = item_id;
            w["z"] = nullptr;
            r->setAttribute(STEG3D, bj::serialize(w));
            r->setAttribute("inkscape:label", TR("Haltesteg"));
            eltern->getRepr()->appendChild(r);
            Inkscape::GC::release(r);
            if (auto s = cast<SPItem>(d->getObjectByRepr(r))) {
                Geom::Affine e = Geom::identity();
                if (auto ei = cast<SPItem>(s->parent)) e = ei->i2doc_affine();
                // Material: Winkel gegen den Uhrzeigersinn, y nach oben – Zeichnung: y nach unten
                s->set_item_transform(Geom::Rotate::from_degrees(-num(*v, "winkel", 0)) * Geom::Translate(p) *
                                      e.inverse());
                s->doWriteTransform(s->transform, nullptr, false);
            }
        }
        DocumentUndo::done(d, TR("Haltestege setzen"), "");
    }

    void stege_zeigen(bj::object const &e)
    {
        auto d = panel.getDocument();
        if (!d) return;
        Material m = material(d, item());
        double f = MM_PX;
        if (auto tl = liste_von(e, "trennlinie"); tl && tl->size() >= 2) {
            double a = (*tl)[0].to_number<double>(), b = (*tl)[1].to_number<double>();
            trenn_mitte = (a + b) / 2;
            trenn_l.set_text(b - a < 0.05 ? mm(a, 2) + " mm"
                                          : mm(a, 1) + " … " + mm(b, 1) + " mm · " +
                                                (flag(e, "schief", false) ? TR("schief") : TR("gerade")));
        } else {
            trenn_l.set_text("–");
        }
        z_auto.clear();
        if (auto l = liste_von(e, "stege"))
            for (auto const &v : *l)
                if (auto o = v.if_object()) z_auto[str(*o, "id")] = num(*o, "z_auto", 0);
        double dicke = wert(steg_d);
        std::vector<float> quader_e;
        auto liste = stege();
        std::vector<std::string> ids;
        for (auto s : liste) ids.push_back(s->getId() ? s->getId() : "");
        bool neu_bauen = ids != zeilen_ids || ids.empty();
        if (!neu_bauen) {
            // gleiche Stege: nur Werte und Ansicht nachführen (die Liste bleibt, der Fokus auch)
            steg_setzt = true;
            for (auto s : liste) {
                std::string id = s->getId() ? s->getId() : "";
                auto w = steg_werte(s);
                bool automatisch = !w.contains("z") || w.at("z").is_null();
                double za = z_auto.count(id) ? z_auto[id] : 0.0;
                double zm = automatisch ? za : num(w, "z", za);
                quader(quader_e, ecken_mm(s, m, f), zm - dicke / 2, zm + dicke / 2);
                auto [sp, ck] = zeilen[id];
                if (sp && automatisch && !sp->has_focus()) sp->set_value(zm);
                if (ck) ck->set_active(automatisch);
            }
            steg_setzt = false;
            ansicht.stege(std::move(quader_e));
            return;
        }
        zeilen_ids = ids;
        zeilen.clear();
        for (auto k : steg_liste.get_children()) steg_liste.remove(*k);
        int n = 0;
        for (auto s : liste) {
            ++n;
            std::string id = s->getId() ? s->getId() : "";
            auto w = steg_werte(s);
            bool automatisch = !w.contains("z") || w.at("z").is_null();
            double za = z_auto.count(id) ? z_auto[id] : 0.0;
            double zm = automatisch ? za : num(w, "z", za);
            quader(quader_e, ecken_mm(s, m, f), zm - dicke / 2, zm + dicke / 2);
            auto zeile = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
            auto nl = label(TRF("Steg {}", {std::to_string(n)}));
            nl->set_hexpand(true);
            zeile->add(*nl);
            auto sp = Gtk::make_managed<Gtk::SpinButton>(Gtk::Adjustment::create(zm, -m.d, 0, 0.1, 1), 0.1, 2);
            sp->set_numeric(true);
            punkt_und_komma(sp);
            sp->set_width_chars(7);
            sp->set_tooltip_text(TR("Höhe der Stegmitte (0 = Materialoberfläche)"));
            zeile->add(*sp);
            zeile->add(*label("mm", "hirch-dim"));
            auto ck = Gtk::make_managed<Gtk::CheckButton>(TR("Trennlinie"));
            ck->set_active(automatisch);
            ck->set_tooltip_text(TR("Höhe automatisch: Trennfläche, wo der Steg am Modell ansetzt"));
            zeile->add(*ck);
            auto zeigen = Gtk::make_managed<Gtk::Button>();
            zeigen->set_label(TR("wählen"));
            zeigen->set_relief(Gtk::RELIEF_NONE);
            zeigen->set_tooltip_text(TR("Auf der Zeichnung wählen"));
            zeile->add(*zeigen);
            auto weg = Gtk::make_managed<Gtk::Button>("×");
            weg->set_relief(Gtk::RELIEF_NONE);
            weg->set_tooltip_text(TR("Steg entfernen"));
            zeile->add(*weg);
            zeilen[id] = {sp, ck};
            auto sid = std::make_shared<std::string>(id);
            auto finden = [this, sid]() -> SPItem * {
                auto dd = panel.getDocument();
                return dd ? cast<SPItem>(dd->getObjectById(*sid)) : nullptr;
            };
            sp->signal_value_changed().connect([this, sp, ck, finden] {
                if (steg_setzt) return;
                auto st = finden();
                auto dd = panel.getDocument();
                if (!st || !dd) return;
                auto w2 = steg_werte(st);
                w2["z"] = wert(sp);
                st->getRepr()->setAttribute(STEG3D, bj::serialize(w2));
                DocumentUndo::done(dd, TR("Haltesteg ändern"), "");
                if (ck->get_active()) {
                    steg_setzt = true;
                    ck->set_active(false);
                    steg_setzt = false;
                }
                stege_bald();
            });
            ck->signal_toggled().connect([this, ck, finden] {
                if (steg_setzt || !ck->get_active()) return;
                auto st = finden();
                auto dd = panel.getDocument();
                if (!st || !dd) return;
                auto w2 = steg_werte(st);
                w2["z"] = nullptr;
                st->getRepr()->setAttribute(STEG3D, bj::serialize(w2));
                DocumentUndo::done(dd, TR("Haltesteg ändern"), "");
                stege_bald();
            });
            zeigen->signal_clicked().connect([this, finden] {
                if (auto st = finden())
                    if (auto sel = panel.getSelection()) sel->set(st);
            });
            weg->signal_clicked().connect([this, finden] {
                auto st = finden();
                auto dd = panel.getDocument();
                if (!st || !dd) return;
                if (auto sel = panel.getSelection()) sel->clear();
                st->deleteObject(true);
                DocumentUndo::done(dd, TR("Haltesteg entfernen"), "");
                stege_bald();
            });
            steg_liste.add(*zeile);
        }
        if (liste.empty()) {
            bool h = spalt_haut.get_active();
            auto l = label(h ? TR("Keine Haltestege – die Haut hält das Teil.")
                             : TR("Noch keine Haltestege – ohne sie löst sich das Teil beim Fräsen von Seite 2."),
                           h ? "hirch-dim" : "hirch-fehler");
            l->set_line_wrap(true);
            l->set_max_width_chars(44);
            l->set_xalign(0);
            steg_liste.add(*l);
        }
        steg_liste.show_all();
        if (x_stege_l) x_stege_l->set_text(Glib::ustring(TR("Haltestege")).uppercase() + " · " + std::to_string(n));
        ansicht.stege(std::move(quader_e));
    }

    void bild_setzen(SPItem *it, bj::object const &e, bj::object const &a, bj::object const &l)
    {
        auto d = panel.getDocument();
        auto r = it->getRepr();
        auto uri = png_uri(datei(e, "bild"));
        if (uri.empty()) return;
        double f = MM_PX;
        auto z = [r](char const *k) {
            auto v = r->attribute(k);
            return v ? g_ascii_strtod(v, nullptr) : 0.0;
        };
        double cx = z("x") + z("width") / 2, cy = z("y") + z("height") / 2;
        // Bildgrösse in den eigenen Einheiten des Bildes (seine Transformation bleibt)
        double k = it->i2doc_affine().descrim();
        if (k > 1e-12) f /= k;
        double w = 100, h = 100;
        bj::object neu = info;
        if (auto bl = unter(e, "bild_lage"))
            if (auto mm_ = liste_von(*bl, "mm")) {
                neu["bild_lage"] = *mm_;
                if (mm_->size() >= 4) {
                    w = (*mm_)[2].to_number<double>() * f;
                    h = (*mm_)[3].to_number<double>() * f;
                }
            }
        neu["ausrichtung"] = a;
        neu["lage"] = l;
        if (auto ms = liste_von(e, "masse")) neu["masse"] = *ms;
        if (auto zz = liste_von(e, "z")) neu["z"] = *zz;
        info = neu;
        r->setAttribute("x", attr_zahl(cx - w / 2));
        r->setAttribute("y", attr_zahl(cy - h / 2));
        r->setAttribute("width", attr_zahl(w));
        r->setAttribute("height", attr_zahl(h));
        r->setAttribute("xlink:href", uri);
        attr_alt = bj::serialize(neu);
        r->setAttribute(RELIEF, attr_alt);
        DocumentUndo::done(d, TR("3D-Modell ändern"), "");
        details.set_text(art_name(str(info, "art")) + " · " + mm(liste_zahl(info, "masse", 0)) + " × " +
                         mm(liste_zahl(info, "masse", 1)) + " × " + mm(liste_zahl(info, "masse", 2), 2) + " mm · " +
                         TRF("in der Zeichnung gespeichert ({} MB)", {zahl(num(info, "daten_bytes", 0) / 1048576.0, 1)}));
    }

    /// Originaldatei geändert (z. B. in FreeCAD): Daten und Bild neu, Lage und Ausrichtung bleiben
    void aus_datei()
    {
        auto o = str(info, "original");
        auto d = panel.getDocument();
        if (o.empty() || !d) return;
        unsigned g = ++gen;
        status.set_text(TR("Wird gelesen …"));
        auto a = ausrichtung_json(), l = lage_json();
        std::string ziel = Glib::build_filename(ordner, "neu-" + std::to_string(g));
        g_mkdir_with_parents(ziel.c_str(), 0700);
        Material m = material(d);
        lauf = kern({"einbetten", o, "--ordner", ziel, "--ausrichtung", bj::serialize(a), "--lage", bj::serialize(l),
                     "--dicke", attr_zahl(m.d)},
                    ziel, [this, g, a, l, ziel](bool ok, bj::object const &e, std::string const &f) {
                        if (g == gen) {
                            if (ok && flag(e, "ok", false)) daten_ersetzen(e, a, l);
                            else hinweis_setzen(warn, warnungen(e).empty() ? f : warnungen(e), "hirch-fehler");
                        }
                        ordner_loeschen_unter(ziel);
                    });
    }

    void daten_ersetzen(bj::object const &e, bj::object const &a, bj::object const &l)
    {
        auto d = panel.getDocument();
        auto it = item();
        if (!d || !it) return;
        auto k = daten_knoten(d, str(info, "daten"));
        std::string text;
        try {
            text = Glib::file_get_contents(datei(e, "daten"));
        } catch (...) {
            return;
        }
        if (!k || text.empty()) return;
        while (k->firstChild()) k->removeChild(k->firstChild());
        auto t = d->getReprDoc()->createTextNode(text.c_str());
        k->appendChild(t);
        Inkscape::GC::release(t);
        info["original_zeit"] = num(e, "original_zeit", 0);
        info["daten_bytes"] = num(e, "daten_bytes", double(text.size()));
        try {
            Glib::file_set_contents(daten_datei, text);
        } catch (...) {
        }
        bild_setzen(it, e, a, l);
        original_zeigen();
        rechnen(false);
    }
};

Hirch3D::Hirch3D()
    : DialogBase("/dialogs/hirch-3d", "Hirch3D")
    , _impl(std::make_unique<Impl>(*this))
{
    show_all_children();
    // Peter 03.11.: beim Rollen durch den Reiter verstellte das Mausrad Auswahllisten (vermutlich «Seiten treffen
    // sich» → «auf einer Ebene» bei Z 0) – das Rad rollt jetzt die Seite, erst nach einem Klick ändert es Werte
    rad_sperren(*this);                         // B-22: auch nach einem Klick nie über das Mausrad
    _impl->waehlen();
}

Hirch3D::~Hirch3D() = default;

void Hirch3D::documentReplaced()
{
    _impl->item_id.clear();
    _impl->attr_alt.clear();
    _impl->anmelden();
    _impl->waehlen();
}

void Hirch3D::selectionChanged(Inkscape::Selection *)
{
    _impl->waehlen();
    _impl->stege_bald();
}

void Hirch3D::selectionModified(Inkscape::Selection *sel, guint)
{
    // ein Haltesteg verschoben, gedreht, länger gezogen: Höhen und Ansicht nachführen
    if (!sel) return;
    for (auto it : sel->items()) {
        if (it->getRepr()->attribute(STEG3D)) {
            _impl->stege_bald();
            return;
        }
        if (it->getRepr()->attribute(RELIEF) && it->getId() && _impl->item_id == it->getId()) {
            _impl->seite2_bild();            // Modell verschoben oder gedreht: Unterseite auf Seite 2 mit
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Zweiseitig (Reiter «Fräsen»): «Zweiseitig» einschalten, 3D-Bearbeitungen für Seite 2

void HirchFraesen::reliefZweiseitig(bool an, std::string const &achse) { _impl->relief_zweiseitig(an, achse); }
void Hirch3D::kollisionZeigen(std::string const &json) { _impl->kollision_zeigen(json); }
void Hirch3D::rechnungNeu(std::string const &json) { _impl->rechnung_neu(json); }
void Hirch3D::fraesergebnisZeigen(bool an) { _impl->modus_setzen(an); }
void Hirch3D::rechenstand(bool laeuft, std::string const &text, std::string const &uhr)
{
    _impl->rechenstand(laeuft, text, uhr);
}
void Hirch3D::wartet(bool an, std::string const &text) { _impl->wartet(an, text); }
std::string HirchFraesen::rechnungFuer3D() { return _impl->rechnung_fuer_3d(); }
void HirchFraesen::rechnenAbbrechen() { _impl->rechnen_abbrechen(); }
void HirchFraesen::neuRechnen()
{
    _impl->letzte_xml.clear();
    _impl->spaeter(10);
}
void HirchFraesen::jetztRechnen() { _impl->rechnen_jetzt(); }
bool HirchFraesen::wartetAufKnopf() { return _impl->wartet_knopf && !_impl->rechnet; }
bool HirchFraesen::ergebnisEntwurf() { return flag(_impl->ergebnis, "entwurf", false); }
void HirchFraesen::abHierAusgeben(std::string const &ab_json, std::string const &wo)
{
    // Sicherheit: Zeilen aus dem Entwurf passen nicht zum genau gerechneten Programm – nie danach ausgeben
    if (ab_json.find("\"zeile\"") != std::string::npos && ergebnisEntwurf()) {
        _impl->meldung(TR("Ab hier neu ausgeben"),
                       TR("Die Ansicht zeigt den Entwurf. Ab einer Stelle geht es erst mit genau gerechneten Bahnen – "
                          "zuerst «G-Code speichern»."), Gtk::MESSAGE_INFO);
        return;
    }
    _impl->ab_hier_ausgeben(ab_json, wo);
}
void HirchFraesen::anfangsdialog(std::string const &modell) { _impl->material_dialog(true, modell); }
void HirchFraesen::halterung() { _impl->halterung_dialog(); }

void HirchFraesen::reliefBearbeitungen(double tiefe, std::function<void(std::string const &)> fertig)
{
    _impl->relief_anlegen(tiefe, std::move(fertig));
}

std::vector<std::string> HirchFraesen::bearbeitungenAnlegen(std::string const &json)
{
    return _impl->merkmal_bearbeitungen(json);
}

void HirchFraesen::bereichSetzen(std::string const &id, std::string const &bereich)
{
    auto &e = _impl->einst;
    auto bea = liste_von(e, "bearbeitungen");
    if (!bea) return;
    for (auto &v : *bea)
        if (auto b = v.if_object(); b && str(*b, "id") == id) {
            if (str(*b, "bereich", "alles") == bereich) return;
            (*b)["bereich"] = bereich;
            if (b->find("bereich_ueberlauf") == b->end()) (*b)["bereich_ueberlauf"] = 1.0;
            _impl->schreiben(TR("Bereich der Bearbeitung"));
            return;
        }
}

void Hirch3D::einlesen()
{
    auto desk = getDesktop();
    auto win = desk ? desk->getInkscapeWindow() : nullptr;
    if (!win) return;
    Einlesen dlg(*win, desk);
    if (dlg.run() != Gtk::RESPONSE_OK || !dlg.fertig) return;
    dlg.hide();
    if (auto it = einfuegen(desk, dlg.erg, dlg.lage_json())) {
        (void)it;
        _impl->waehlen();
        zeigen();
        bearbeitungen_anlegen(liste_zahl(dlg.erg, "masse", 2, 0), material(desk->getDocument()).d, {});
    }
}

void Hirch3D::bearbeitungen_anlegen(double modell_hoehe, double dicke, std::function<void()> danach)
{
    auto fr = _impl->fraesen();
    if (!fr) {
        if (danach) danach();
        return;
    }
    // so tief muss das Werkzeug höchstens reichen: Höhe des Modells, höchstens die Materialdicke
    double tiefe = std::min(modell_hoehe > 0 ? modell_hoehe : dicke, dicke > 0 ? dicke : 1e9);
    fr->reliefBearbeitungen(tiefe, [this, danach](std::string const &text) {
        hinweis_setzen(_impl->angelegt, text, "hirch-karte");
        if (danach) danach();
    });
}

void Hirch3D::einlesenDatei(std::string const &pfad, bool zweiseitig)
{
    auto desk = getDesktop();
    if (!desk) return;
    Material m = material(desk->getDocument());
    auto ordner = neuer_ordner();
    bj::object lage;
    lage["art"] = ist_bild_datei(pfad) ? "oben" : "mittig";
    _impl->rechnet = true;
    if (_impl->lauf_einlesen) *_impl->lauf_einlesen = false;
    _impl->lauf_einlesen = kern({"einbetten", pfad, "--ordner", ordner, "--material",
          attr_zahl(m.b) + "," + attr_zahl(m.h) + "," + attr_zahl(m.d), "--lage", bj::serialize(lage)},
         ordner, [this, desk, lage, ordner, zweiseitig](bool ok, bj::object const &e, std::string const &f) {
             _impl->rechnet = false;
             if (ok && flag(e, "ok", false)) {
                 einfuegen(desk, e, lage);
                 _impl->waehlen();
                 zeigen();
                 // erst die Bearbeitungen mit Werkzeugen anlegen, dann (zweiseitig) für Seite 2 kopieren
                 bearbeitungen_anlegen(liste_zahl(e, "masse", 2, 0), material(desk->getDocument()).d,
                                       [this, zweiseitig] {
                     if (zweiseitig && _impl->item()) {
                         _impl->setzt = true;
                         _impl->zwei_an.set_active(true);
                         _impl->setzt = false;
                         _impl->zweiseitig_setzen(true);
                     }
                 });
             } else {
                 hinweis_setzen(_impl->warn, warnungen(e).empty() ? f : warnungen(e), "hirch-fehler");
             }
             ordner_loeschen(ordner);
         });
}

bool hirch_3d_hat_modell(SPDocument *doc) { return !reliefs(doc).empty(); }

void Hirch3D::modellKlickTesten(double fx, double fy, bool umschalt)
{
    auto &a = _impl->ansicht;
    a.treffen_modell(fx * a.bereich.get_allocated_width(), fy * a.bereich.get_allocated_height(), umschalt);
}

int Hirch3D::flaechenGewaehlt() const { return int(_impl->fl_wahl.size()); }

void Hirch3D::flaechenAlsBereichTesten() { _impl->flaechen_vereinigen(true); }

bool Hirch3D::klickTesten(double fx, double fy, bool rest)
{
    auto &a = _impl->ansicht;
    a.treffen(fx * a.bereich.get_allocated_width(), fy * a.bereich.get_allocated_height());
    if (rest && _impl->klick_rest.get_visible()) {
        _impl->klick_rest.clicked();
        return true;
    }
    return false;
}

void Hirch3D::stegeZeigen()
{
    auto w = _impl->steg_titel;
    if (!w) return;
    zeigen();
    if (_impl->x_stege_auf) _impl->x_stege_auf();
    Glib::signal_timeout().connect_once([w] {
        for (auto p = w->get_parent(); p; p = p->get_parent()) {
            auto sw = dynamic_cast<Gtk::ScrolledWindow *>(p);
            if (!sw || !sw->get_child()) continue;
            int x = 0, y = 0;
            if (w->translate_coordinates(*sw->get_child(), 0, 0, x, y)) {
                auto a = sw->get_vadjustment();
                a->set_value(std::clamp<double>(y - 8, a->get_lower(), a->get_upper() - a->get_page_size()));
            }
            break;
        }
    }, 250);
}

void Hirch3D::zeigen()
{
    // in der rechten Spalte nach vorne (die Reiter sind dort ausgeblendet, Basis 06.11.)
    for (Gtk::Widget *w = this; w && w->get_parent(); w = w->get_parent())
        if (auto nb = dynamic_cast<Gtk::Notebook *>(w->get_parent())) {
            int n = nb->page_num(*w);
            if (n >= 0) nb->set_current_page(n);
            return;
        }
}

bool Hirch3D::beschaeftigt() const { return _impl->rechnet || _impl->stege_laeuft || _impl->erg_laeuft; }

} // namespace Inkscape::UI::Dialog

void hirch_3d_fenster(SPDesktop *desk)
{
    using namespace Inkscape::UI::Dialog;
    auto c = desk ? desk->getContainer() : nullptr;
    if (!c) return;
    if (!c->get_dialog("Hirch3D")) c->new_dialog("Hirch3D");
    if (auto d = dynamic_cast<Hirch3D *>(c->get_dialog("Hirch3D"))) d->zeigen();
}

void hirch_3d_einlesen(InkscapeWindow *win)
{
    using namespace Inkscape::UI::Dialog;
    auto desk = win ? win->get_desktop() : nullptr;
    auto c = desk ? desk->getContainer() : nullptr;
    if (!c) return;
    c->new_dialog("Hirch3D");
    if (auto d = dynamic_cast<Hirch3D *>(c->get_dialog("Hirch3D"))) d->einlesen();
}
