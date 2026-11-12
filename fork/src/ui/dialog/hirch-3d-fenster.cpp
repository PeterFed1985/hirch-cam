// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: 3D – 3D-Ansicht im Hauptfenster: Bedienung, Abspielen, Zeitleiste, Halt-Karte, Fräser (aus hirch-3d.cpp)
 */
#include "ui/dialog/hirch-3d-intern.h"

#include <set>

namespace Inkscape::UI::Dialog {

// ---------------------------------------------------------------------------
// 3D-Ansicht für das Hauptfenster (Umschalter 2D | 3D, Platz im Fenster: Basis). Zeigt die Szene des
// Reiters «3D» dieses Fensters mit eigener Kamera; Klicks wählen dort (Flächen, Fräsergebnis).

namespace {
struct AnsichtFenster : Gtk::Box {
    SPDesktop *desk;
    std::unique_ptr<Ansicht> a;
    std::shared_ptr<Szene3D> eigene = std::make_shared<Szene3D>();   // ohne Reiter «3D»
    Gtk::Overlay ueber;
    Gtk::Label hinweis;
    // Bedienung oben links (Peter, Entwurf «Ansicht 2D | 3D»)
    Gtk::Box schalter{Gtk::ORIENTATION_VERTICAL, 2};
    Gtk::ToggleButton k_oben, k_unten;
    bool nur_knoepfe = false;
    Gtk::CheckButton h_linien, h_bahnen, h_eil, h_stege, h_block, h_drehen, h_spann, h_mutter;
    Gtk::Label h_mutter_info;
    // B-16 (Peter 02.11.): fertiges Teil ausblenden, Fräsbahnen je Bearbeitung ein-/ausblenden
    Gtk::CheckButton h_teil;
    Gtk::Box bahn_wahl{Gtk::ORIENTATION_VERTICAL, 2};
    Gtk::Box bahn_liste{Gtk::ORIENTATION_VERTICAL, 0};
    std::set<std::string> bahnen_aus;       // Schlüssel der ausgeblendeten Bearbeitungen (bleibt beim Neurechnen)
    std::vector<AbtragBearbeitung> bea_jetzt;
    std::vector<std::string> bea_titel;     // Name (mit Seite) je Bearbeitung
    bool bahn_still = false;                // «alle»/«keine»: die Häkchen einzeln nicht neu bauen lassen
    double drehung = 0;                         // Fräser: Winkel um die Achse (M3: im Uhrzeigersinn von oben)
    sigc::connection dreh_takt;
    bool dreh_offen = false;                    // ein Bild für die Drehung ist bestellt, noch nicht gezeichnet
    gint64 dreh_zuletzt = 0;

    /// Nächster Schritt der Drehung: erst, wenn das letzte Bild gezeichnet ist, und mit niedriger Priorität –
    /// Rechnen, Bedienung und Leerlauf gehen vor, auch wenn eine Grafikkarte länger je Bild braucht
    void dreh_planen(int ms)
    {
        if (dreh_takt.connected()) return;
        dreh_takt = Glib::signal_timeout().connect([this] {
            dreh_takt.disconnect();
            if (!h_drehen.get_active() || !a || !a->bereich.get_mapped() || !teil || bew.empty() || am_ende()) {
                dreh_zuletzt = 0;
                dreh_planen(300);              // später wieder nachsehen
                return false;
            }
            auto jetzt = g_get_monotonic_time();
            double dt = dreh_zuletzt ? std::min(0.25, (jetzt - dreh_zuletzt) / 1e6) : 0.04;
            dreh_zuletzt = jetzt;
            drehung = std::fmod(drehung - 2 * M_PI * dt, 2 * M_PI);   // eine Umdrehung je Sekunde
            fraeser_bauen();
            dreh_offen = true;
            a->bereich.queue_render();
            return false;
        }, ms, Glib::PRIORITY_LOW);
    }
    sigc::connection neu, teil_neu, doc_mod, sel_mod, linien_spaeter;
    // hirch-abtrag im GL-Kontext der Ansicht, gezeichnet direkt aus seinen Texturen (TeilGL)
    HirchAbtrag abtrag;
    TeilGL teilgl;
    // fertiges Netz (Peter 23.10.: iMac): im Thread gerechnet, gezeichnet, solange die Höhen am Ende stehen
    TeilNetzGL netzgl;
    std::shared_ptr<NetzAuftrag> netz_auftrag;
    bool netz_geladen = false;
    sigc::connection netz_takt;
    bool abtrag_gl = false, klassen_neu = false, pyramide_neu = false;
    // «Färben nach» (Peter, 22.10.): Bearbeitung | Vergleich mit dem Modell | Neutral
    Gtk::RadioButton f_bearb, f_vergl, f_neutral;
    int wahl = 0;                           // Bearbeitung (Index der Karte, ab 1) kräftig, 0: alle
    std::string teil_kennung;
    std::shared_ptr<TeilDaten> teil;
    TeilRaster raster, raster_k;
    double steg_summe = -1;
    std::vector<TeilLinie> linien;
    SPDocument *beobachtet = nullptr;
    // Abspielleiste (Entwurf «Ansicht 2D | 3D»): Zeitleiste je Bearbeitung, Play, Tempo
    struct Abschnitt {
        double t0 = 0, t1 = 0;              // Sekunden
        int bea = -1, p = 0;                // Bearbeitung (Index in bea_namen), Programm
        bool seite2 = false;
        int spannung = 1;                   // Umspannen: 2 nach dem Umspannen
        std::array<float, 3> farbe{0.6f, 0.6f, 0.6f};
        std::string name, wz;
    };
    std::vector<AbtragBewegung> bew;
    std::vector<double> zeit;               // Sekunden bis zum Ende jeder Bewegung
    std::vector<Abschnitt> ablauf;
    std::vector<double> eil_rein;           // Zeitpunkte der Eilgänge ins Material
    struct EilKandidat {
        int k = 0;                          // Bewegung
        double ax = 0, ay = 0, az = 0, t = 0;   // Anfang, Zeitpunkt
    };
    std::vector<EilKandidat> eil_kand;      // Eilgänge unter Z0: gegen die Höhenbilder prüfen
    HirchAbtrag eil_ab;                     // eigener Rechenweg für die Prüfung (die Anzeige bleibt fertig)
    bool eil_gl = false, eil_laeuft = false;
    std::size_t eil_i = 0;
    gint64 eil_uhr = 0;
    std::vector<AbtragWerkzeug> eil_wz;
    sigc::connection eil_takt;
    std::vector<int> bahn_bis;              // je Linienstück der Fräsbahnen: Index der Bewegung
    std::vector<int> eil_bis;               // je Eilgang: Index der Bewegung
    std::vector<std::pair<int, int>> gew_enden;
    double t_jetzt = 0, t_gerechnet = -1, spiel_t0 = 0;
    gint64 spiel_uhr = 0;
    bool spielt = false;
    int tempo_i = 3;                        // ¼× | Echtzeit | 10× | 50× | Übersicht 15 s
    Gtk::Box leiste{Gtk::ORIENTATION_VERTICAL, 8};
    Gtk::Button b_play, b_anfang, b_zu, b_auf;
    Gtk::ToggleButton b_tempo[5];
    Gtk::Label l_uhr, l_jetzt, l_marken, l_eil;
    Gtk::DrawingArea zeitleiste;
    sigc::connection takt;
    // Halt beim Wenden und Umspannen (Peter 27.10.): Abspielen hält an, eine Karte sagt, was zu tun ist
    struct Halt {
        double t = 0;
        bool wenden = false;                // sonst Umspannen
    };
    std::vector<Halt> halte;
    Gtk::Box karte{Gtk::ORIENTATION_VERTICAL, 12};
    Gtk::Label k_zeichen, k_titel, k_bei, k_text, k_schritte;
    Gtk::Button k_weiter, k_anfang;
    int karte_halt = -1;                    // gezeigte Karte (Index in halte), -1: keine
    Gtk::Label chip;                        // oben: Spannung 2 läuft
    int spann_phase = -1;                   // gebaute Spannmittel: seite * 10 + spannung
    Ansicht const *spann_ansicht = nullptr;

    explicit AnsichtFenster(SPDesktop *dt)
        : Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0)
        , desk(dt)
    {
        css_einrichten();
        set_hexpand(true);
        set_vexpand(true);
        klasse(hinweis, "hirch-3d-hinweis");
        hinweis.set_halign(Gtk::ALIGN_CENTER);
        hinweis.set_valign(Gtk::ALIGN_CENTER);
        hinweis.set_line_wrap(true);
        hinweis.set_justify(Gtk::JUSTIFY_CENTER);
        hinweis.set_no_show_all(true);
        ueber.add_overlay(hinweis);
        schalter_bauen();
        leiste_bauen();
        karte_bauen();
        ueber.set_hexpand(true);
        ueber.set_vexpand(true);
        add(ueber);
        neu = szenen_neu().connect([this](SPDesktop *d) {
            if (d == desk) bauen();
        });
        teil_neu = teile_neu().connect([this](SPDesktop *d) {
            if (d == desk) teil_holen();
        });
        if (auto sel = desk ? desk->getSelection() : nullptr)
            sel_mod = sel->connectChanged([this](Inkscape::Selection *) { linien_faerben(); });
        bauen();
        teil_holen();
        show_all();
        // Windows 03.11.: «Vorschau 3D …» (oder 2D | 3D) ohne Rechnung – wie Speichern und USB selbst rechnen, wenn das
        // Panel auf den Knopf wartet (grosse Zeichnung, «Automatisch rechnen» wartet dann)
        signal_map().connect([this] { Glib::signal_idle().connect_once([this] { selbst_rechnen(); }); });
        alle()[desk] = this;
    }
    ~AnsichtFenster() override
    {
        if (alle()[desk] == this) alle().erase(desk);
        for (auto c : {&neu, &teil_neu, &doc_mod, &sel_mod, &linien_spaeter, &takt, &eil_takt, &netz_takt, &dreh_takt})
            c->disconnect();
        if (netz_auftrag) netz_auftrag->abbrechen = true;
    }

    void schalter_bauen()
    {
        klasse(schalter, "hirch-3d-schalter");
        schalter.set_halign(Gtk::ALIGN_START);
        schalter.set_valign(Gtk::ALIGN_START);
        schalter.set_margin_start(12);
        schalter.set_margin_top(12);
        auto kopf = [this](char const *t) {
            auto l = label(t, "hirch-3d-kopf");
            schalter.add(*l);
        };
        kopf(TR("ANSEHEN VON"));
        auto zwei = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
        klasse(*zwei, "linked");
        k_oben.set_label(TR("oben"));
        k_unten.set_label(TR("unten"));
        k_oben.set_active(true);
        zwei->add(k_oben);
        zwei->add(k_unten);
        zwei->set_margin_bottom(6);
        schalter.add(*zwei);
        auto setzen = [this](bool unten) {
            static bool dabei = false;
            if (dabei) return;
            dabei = true;
            k_oben.set_active(!unten);
            k_unten.set_active(unten);
            dabei = false;
            if (a && !nur_knoepfe) a->seite_ansehen(unten);
        };
        k_oben.signal_toggled().connect([this, setzen] { if (k_oben.get_active()) setzen(false); else if (!k_unten.get_active()) k_oben.set_active(true); });
        k_unten.signal_toggled().connect([this, setzen] { if (k_unten.get_active()) setzen(true); else if (!k_oben.get_active()) k_unten.set_active(true); });
        kopf(TR("FÄRBEN NACH"));
        Gtk::RadioButton::Group grp;
        f_bearb.set_group(grp);
        f_vergl.set_group(grp);
        f_neutral.set_group(grp);
        f_bearb.set_label(TR("Bearbeitung"));
        f_vergl.set_label(TR("Vergleich mit dem Modell"));
        f_neutral.set_label(TR("Neutral"));
        f_bearb.set_active(true);
        f_vergl.set_no_show_all(true);
        for (auto rb : {&f_bearb, &f_vergl, &f_neutral}) {
            schalter.add(*rb);
            rb->signal_toggled().connect([this] {
                if (a) a->bereich.queue_render();
            });
        }
        f_neutral.set_margin_bottom(6);
        kopf(TR("ZEIGEN"));
        h_teil.set_label(TR("Gefrästes Teil"));
        h_teil.set_tooltip_text(TR("Ausschalten, um nur die Fräsbahnen zu sehen"));
        h_teil.set_active(true);
        schalter.add(h_teil);
        h_teil.signal_toggled().connect([this] {
            // nur die Bahnen sehen wollen: dann sie auch zeigen
            if (!h_teil.get_active() && !h_bahnen.get_active()) h_bahnen.set_active(true);
            flags_setzen();
        });
        h_linien.set_label(TR("Zeichnung auf dem Teil"));
        h_bahnen.set_label(TR("Fräsbahnen"));
        h_eil.set_label(TR("Eilgänge (rot)"));
        h_eil.set_active(true);
        h_stege.set_label(TR("Haltestege"));
        h_block.set_label(TR("Rohteil (Drahtgitter)"));
        h_linien.set_active(true);
        h_stege.set_active(true);
        h_block.set_active(true);
        h_spann.set_label(TR("Spannmittel und Zonen"));
        h_spann.set_tooltip_text(TR("Spannmittel grau, Schraube dunkel; orange die Zone, die erst nach dem Umspannen "
                                    "gefräst wird"));
        h_spann.set_active(true);
        for (auto h : {&h_linien, &h_bahnen, &h_eil, &h_stege, &h_block, &h_spann}) {
            schalter.add(*h);
            h->signal_toggled().connect([this] { flags_setzen(); });
        }
        // Fräser dreht sich (Peter 26.10.): eine Umdrehung je Sekunde, solange er zu sehen ist – die Spannuten
        // zeigen Drehrichtung und Drall; abschaltbar für schwache Grafikkarten (gemeinsam mit «Vorschau 3D»)
        h_drehen.set_label(TR("Fräser dreht sich"));
        h_drehen.set_tooltip_text(TR("Langsam, nur um die Drehrichtung zu zeigen. Ausschalten, wenn die Ansicht "
                                     "dadurch zu langsam wird."));
        h_drehen.set_active(Inkscape::Preferences::get()->getBool("/hirch/fraeser_drehen", true));
        schalter.add(h_drehen);
        h_drehen.signal_toggled().connect([this] {
            Inkscape::Preferences::get()->setBool("/hirch/fraeser_drehen", h_drehen.get_active());
        });
        // Spannmutter und Spindel (Peter 05.11.): halbtransparent über der Ausspannlänge; orange unter 2 mm zum
        // Material, rot bei Berührung. Masse aus «Spindel und Halterung» – ohne sie keine (keine geschätzte Mutter)
        h_mutter.set_label(TR("Spannmutter und Spindel"));
        h_mutter.set_tooltip_text(TR("Halbtransparent über der Ausspannlänge des Fräsers. Orange: weniger als 2 mm "
                                     "zum Material, rot: berührt es. Massgebend bleibt die Prüfung beim Rechnen."));
        h_mutter.set_active(Inkscape::Preferences::get()->getBool("/hirch/spannmutter_zeigen", true));
        schalter.add(h_mutter);
        klasse(h_mutter_info, "hirch-3d-dim");
        h_mutter_info.set_xalign(0);
        h_mutter_info.set_margin_start(24);
        h_mutter_info.set_line_wrap(true);
        h_mutter_info.set_max_width_chars(28);
        schalter.add(h_mutter_info);
        h_mutter.signal_toggled().connect([this] {
            Inkscape::Preferences::get()->setBool("/hirch/spannmutter_zeigen", h_mutter.get_active());
            mutter_info();
            sicht_kuerzen();
            mutter_pruefen_bald = true;
            if (a) a->bereich.queue_render();
        });
        dreh_planen(40);
        // je Bearbeitung (B-16): nur sichtbar, wenn «Fräsbahnen» an ist; lange Listen rollen
        bahn_wahl.set_margin_top(6);
        bahn_wahl.add(*label(TR("FRÄSBAHNEN JE BEARBEITUNG"), "hirch-3d-kopf"));
        auto knoepfe = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
        klasse(*knoepfe, "linked");
        auto alle_an = Gtk::make_managed<Gtk::Button>(TR("alle"));
        auto alle_aus = Gtk::make_managed<Gtk::Button>(TR("keine"));
        knoepfe->add(*alle_an);
        knoepfe->add(*alle_aus);
        knoepfe->set_halign(Gtk::ALIGN_START);
        knoepfe->set_margin_bottom(2);
        alle_an->signal_clicked().connect([this] { alle_bahnen(true); });
        alle_aus->signal_clicked().connect([this] { alle_bahnen(false); });
        bahn_wahl.add(*knoepfe);
        auto roll = Gtk::make_managed<Gtk::ScrolledWindow>();
        roll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
        roll->set_propagate_natural_height(true);
        roll->set_max_content_height(220);
        roll->add(bahn_liste);
        bahn_wahl.add(*roll);
        bahn_wahl.set_no_show_all(true);
        schalter.add(bahn_wahl);
        ueber.add_overlay(schalter);
    }

    /// B-16: Liste der Bearbeitungen (nach jeder Rechnung): Farbe, Name, Häkchen – ausgeblendet bleibt ausgeblendet
    void bahn_liste_bauen()
    {
        for (auto k : bahn_liste.get_children()) bahn_liste.remove(*k);
        for (std::size_t q = 0; q < bea_jetzt.size(); ++q) {
            auto const &B = bea_jetzt[q];
            auto h = Gtk::make_managed<Gtk::CheckButton>();
            auto l = Gtk::make_managed<Gtk::Label>();
            std::string farbe = B.farbe.size() == 7 && B.farbe[0] == '#' ? B.farbe : "#999999";
            l->set_markup("<span foreground=\"" + farbe + "\">\u25A0</span>  " +
                          Glib::Markup::escape_text(q < bea_titel.size() && !bea_titel[q].empty() ? bea_titel[q] : B.id)
                              .raw());
            h->add(*l);
            h->set_active(!bahnen_aus.count(B.id));
            std::string id = B.id;
            h->signal_toggled().connect([this, h, id] {
                if (bahn_still) return;
                if (h->get_active()) bahnen_aus.erase(id);
                else bahnen_aus.insert(id);
                bahnen_neu();
            });
            bahn_liste.add(*h);
        }
        bahn_liste.show_all();
        bahn_wahl_zeigen();
    }

    void bahn_wahl_zeigen()
    {
        bool an = h_bahnen.get_active() && bea_jetzt.size() > 1;
        bahn_wahl.set_no_show_all(!an);
        if (an) bahn_wahl.show_all();
        else bahn_wahl.hide();
    }

    void alle_bahnen(bool an)
    {
        bahnen_aus.clear();
        if (!an)
            for (auto const &B : bea_jetzt) bahnen_aus.insert(B.id);
        bahn_still = true;
        for (auto k : bahn_liste.get_children())
            if (auto h = dynamic_cast<Gtk::CheckButton *>(k)) h->set_active(an);
        bahn_still = false;
        bahnen_neu();
    }

    /// Fräsbahnen und Eilgänge ohne die ausgeblendeten Bearbeitungen neu (nur Linien, kein Neuabtragen)
    void bahnen_neu()
    {
        if (!a || !teil) return;
        std::set<int> aus;
        for (std::size_t q = 0; q < bea_jetzt.size(); ++q)
            if (bahnen_aus.count(bea_jetzt[q].id)) aus.insert(int(q));
        bahnen_bauen(teil->d, bew, bea_jetzt, a->bahn_ecken, &bahn_bis, &a->eil_ecken, &eil_bis, &aus);
        a->neu_bahnen = true;
        sicht_kuerzen();
        a->bereich.queue_render();
    }

    void flags_setzen()
    {
        if (!a) return;
        a->zeige_linien = h_linien.get_active();
        a->zeige_bahnen = h_bahnen.get_active();
        a->zeige_eil = h_eil.get_active();
        a->zeige_stege = h_stege.get_active();
        a->zeige_block = h_block.get_active();
        a->zeige_spann = h_spann.get_active();
        a->teil_aus = !h_teil.get_active();
        h_linien.set_sensitive(h_teil.get_active());   // die Zeichnung liegt auf dem Teil
        bahn_wahl_zeigen();
        a->bereich.queue_render();
    }

    void bauen()
    {
        auto it = szenen().find(desk);
        auto sz = it == szenen().end() ? eigene : it->second;
        if (a && a->sz == sz) return;
        if (a) ueber.remove();
        a = std::make_unique<Ansicht>(sz);
        a->bereich.set_vexpand(true);
        a->bereich.set_hexpand(true);
        a->vor_malen = [this] {
            if (dreh_offen) {                  // Bild gezeichnet: den nächsten Schritt der Drehung planen
                dreh_offen = false;
                dreh_planen(40);
            }
            abtragen();
            stelle_rechnen();
            a->skala = teil && abtrag_gl ? teilgl.skala : 1.0;
            a->glatt_wunsch = teilgl.glatt;
            if (eil_laeuft && !eil_takt.connected())       // Prüfung im Leerlauf, nicht im Bild
                eil_takt = Glib::signal_idle().connect([this] {
                    if (!a || !eil_laeuft || !a->bereich.get_realized()) return false;
                    a->bereich.make_current();
                    eil_schritt();
                    return eil_laeuft;
                });
        };
        a->aufraeumen_gl = [this] {
            if (abtrag_gl) abtrag.gl_ende();
            abtrag_gl = false;
            if (eil_gl) eil_ab.gl_ende();
            eil_gl = eil_laeuft = false;
            teilgl.ende();
            netzgl.ende();
            netz_geladen = false;
        };
        a->teil_malen = [this](M4 const &pv, V3 const &auge, V3 const &licht, Blick const &bl) {
            if (!teil || !abtrag_gl) return;
            auto const &d = teil->d;
            if (pyramide_neu) {                  // Höhen geändert: tiefste Stelle je Block neu
                teilgl.pyramide(abtrag);
                pyramide_neu = false;
            }
            if (klassen_neu) {
                teilgl.gitter_setzen(raster_k);
                teilgl.soll(raster_k.nx, raster_k.ny, soll_bauen(*a->sz, raster_k, d.zweiseitig));
                klassen_neu = false;
                steg_summe = -1;
                Glib::signal_idle().connect_once([this] { f_vergl.set_visible(teilgl.mit_klasse); });
            }
            {                                    // Haltestege geändert (Reiter «3D»): Raster neu
                double sm = double(a->steg_ecken.size());
                for (std::size_t k = 0; k < a->steg_ecken.size(); k += 7) sm += a->steg_ecken[k] * (1.0 + k % 13);
                if (sm != steg_summe) {
                    steg_summe = sm;
                    teilgl.stege(raster_k.nx, raster_k.ny, steg_raster(a->steg_ecken, raster_k));
                }
            }
            int modus = f_neutral.get_active() ? 0 : (f_vergl.get_active() && teilgl.mit_klasse) ? 2 : 1;
            if (netz_auftrag && netz_auftrag->fertig && !netz_geladen) {   // fertig gerechnet: einmal hochladen
                netzgl.laden(*netz_auftrag);
                netz_geladen = true;
                if (g_getenv("HIRCH_3D_ZEIT"))
                    g_message("Hirch 3D: Netz %zu Dreiecke je Seite, %zu Ecken, Fehler %.3f mm, %.2f s",
                              netz_auftrag->idx.size() / 3, netz_auftrag->pos[0].size() / 3, netz_auftrag->fehler,
                              netz_auftrag->sekunden);
                auto alt = netz_auftrag;        // die Kopie im Speicher braucht es nicht mehr
                alt->pos[0] = {};
                alt->pos[1] = {};
                alt->nk[0] = {};
                alt->nk[1] = {};
                alt->idx = {};
            }
            a->wende_um_y = d.um_y;          // Ansicht wenden wie das Teil
            bool z2 = d.zweiseitig && abtrag.tex_h2;
            bool mit_netz = netz_geladen && netzgl.da && !spielt && !zeit.empty() && t_gerechnet >= t_ges() - 1e-9;
            std::function<void()> netz;
            if (mit_netz)
                netz = [&] { netzgl.malen(teilgl, abtrag, d.dicke, z2, d.um_y, modus, wahl, h_stege.get_active(), pv, bl); };
            teilgl.malen(abtrag, d.dicke, d.zweiseitig, d.um_y, d.rund_r, d.breite / 2, d.hoehe / 2, modus, wahl,
                         h_stege.get_active(), pv, auge, licht, bl, netz);
        };
        a->klick_teil = [this](double x, double y, bool doppel) { return klicken(x, y, doppel); };
        a->klick_rechts = [this](double x, double y, GdkEventButton *e) { rechts_menue(x, y, e); };
        a->seite_gewechselt = [this](bool unten) {   // über die Kante gedreht: nur die Knöpfe nachführen
            nur_knoepfe = true;
            (unten ? k_unten : k_oben).set_active(true);
            nur_knoepfe = false;
        };
        a->von_unten = k_unten.get_active();
        if (a->von_unten) a->sicht("schraeg");
        teil_kennung.clear();                // neuer GL-Kontext: neu abtragen
        ueber.add(a->bereich);
        a->bereich.show();
        flags_setzen();
        hinweis_zeigen();
    }

    void teil_holen()
    {
        auto it = teile().find(desk);
        teil = it == teile().end() ? nullptr : it->second;
        if (teil && teil->d.meldung.empty() && teil->d.breite > 0 && a)
            a->material(teil->d.breite, teil->d.hoehe, teil->d.dicke);
        if (!teil && a) {                // andere Platte, noch ohne Rechnung: das alte Teil nicht stehen lassen
            a->zeige_teil = false;
            teil_kennung.clear();
        }
        hinweis_zeigen();
        mutter_info();
        mutter_pruefen_bald = true;
        leiste_auffrischen();
        if (a) a->bereich.queue_render();
    }

    HirchFraesen *panel_fraesen()
    {
        auto c = desk ? desk->getContainer() : nullptr;
        return c ? dynamic_cast<HirchFraesen *>(c->get_dialog("Fraesen")) : nullptr;
    }

    /// ohne Rechnung angezeigt: rechnen lassen (der Knopf «Jetzt rechnen» des Panels)
    void selbst_rechnen()
    {
        if (teil || !get_mapped()) return;
        if (auto p = panel_fraesen(); p && p->wartetAufKnopf()) p->jetztRechnen();
        hinweis_zeigen();
    }

    void hinweis_zeigen()
    {
        std::string t;
        auto p = teil ? nullptr : panel_fraesen();
        if (!teil && p && p->beschaeftigt()) t = TR("Wird gerechnet …");
        else if (!teil) t = TR("Noch nicht gerechnet – «Jetzt rechnen» drücken.");
        else if (!teil->d.meldung.empty()) t = teil->d.meldung;
        hinweis.set_text(t);
        hinweis.set_visible(!t.empty());
    }

    /// im GL-Kontext: neue Rechnung abtragen und das Teil bauen
    void abtragen()
    {
        if (!teil || !teil->d.meldung.empty() || teil->kennung == teil_kennung) return;
        teil_kennung = teil->kennung;
        auto const &d = teil->d;
        if (!abtrag_gl) {
            std::string fehler;
            abtrag_gl = abtrag.gl_start(fehler);
            if (!abtrag_gl) {
                g_warning("Hirch 3D: hirch-abtrag: %s", fehler.c_str());
                return;
            }
        }
        std::vector<AbtragBearbeitung> bea;
        auto moves = teil_bewegungen(d, bea);
        std::vector<AbtragWerkzeug> wz;
        std::vector<int> seiten;
        for (auto const &P : d.programme) {
            wz.push_back({P.form, P.durchmesser, P.vwinkel, P.spitze, P.eckradius});
            seiten.push_back(P.seite);
        }
        auto r = reinterpret_cast<char const *>(glGetString(GL_RENDERER));
        bool software = r && (std::strstr(r, "llvmpipe") || std::strstr(r, "softpipe"));
        double B = d.breite > 0 ? d.breite : 100, Hh = d.hoehe > 0 ? d.hoehe : 100;
        abtrag.einrichten(0, 0, B, Hh, d.dicke, d.zweiseitig, software, moves, wz, seiten, d.sicherheit, bea);
        ablauf_bauen(d, moves, bea);
        abtrag.rechnen(int(moves.size()), 0);
        pyramide_neu = true;
        // Eilgänge ins Material: in einem zweiten hirch-abtrag, stückweise über die nächsten Bilder
        eil_rein.clear();
        eil_i = 0;
        eil_laeuft = false;
        eil_wz = wz;
        if (!eil_kand.empty()) {
            std::string fehler;
            if (!eil_gl) eil_gl = eil_ab.gl_start(fehler);
            if (eil_gl) {
                eil_ab.einrichten(0, 0, B, Hh, d.dicke, d.zweiseitig, software, moves, wz, seiten, d.sicherheit, bea);
                eil_laeuft = true;
                eil_uhr = g_get_monotonic_time();
            }
        }
        {
            std::vector<float> leer_e;
            std::vector<unsigned> leer_i;
            if (netz_auftrag) netz_auftrag->abbrechen = true;
            netz_auftrag.reset();
            netz_geladen = false;
            netz_takt.disconnect();
            bool netz_an = !g_getenv("HIRCH_3D_NETZ") || g_getenv("HIRCH_3D_NETZ")[0] != '0';   // 0: nur Kacheln
            auto A = netz_an ? std::make_shared<NetzAuftrag>() : nullptr;
            teil_bauen(d, abtrag, leer_e, leer_i, raster, false, 4.2e6, A.get());   // Linien und Klick: so fein wie gerechnet
            if (A && A->W > 1) {
                if (software) A->budget = 400000;   // ohne Grafikkarte: Dreiecke kosten Zeit
                netz_auftrag = A;
                std::thread([A] { netz_rechnen(*A); }).detach();
                netz_takt = Glib::signal_timeout().connect([this, A] {
                    if (A != netz_auftrag || A->abbrechen) return false;
                    if (!A->fertig) return true;
                    if (a) a->bereich.queue_render();
                    return false;
                }, 40);
            }
            teil_bauen(d, abtrag, leer_e, leer_i, raster_k, false, 1.0e6); // Vergleich und Stege
        }
        a->teil_ecken.clear();
        a->teil_index.clear();
        if (teilgl.start()) {
            teilgl.farben(bea);
            teilgl.wand(abtrag.x0, abtrag.y0, abtrag.w, abtrag.h, abtrag.zelle, d.rund_r, d.breite / 2, d.hoehe / 2);
        }
        klassen_neu = true;
        gewinde_bauen(d, a->gewinde_ecken, &gew_enden);
        bea_jetzt = bea;
        bea_titel.assign(bea.size(), "");
        for (std::size_t i = 0; i < d.programme.size(); ++i)
            for (auto const &A : d.programme[i].abschnitte) {
                std::string id = A.id.empty() ? std::to_string(i) + ":" + A.name : A.id;
                for (std::size_t q = 0; q < bea.size(); ++q)
                    if (bea[q].id == id && bea_titel[q].empty())
                        bea_titel[q] = d.zweiseitig ? TRF("Seite {} · {}", {std::to_string(d.programme[i].seite), A.name})
                                                    : A.name;
            }
        {
            std::set<int> aus;
            for (std::size_t q = 0; q < bea.size(); ++q)
                if (bahnen_aus.count(bea[q].id)) aus.insert(int(q));
            bahnen_bauen(d, moves, bea, a->bahn_ecken, &bahn_bis, &a->eil_ecken, &eil_bis, &aus);
        }
        bew = std::move(moves);
        sicht_kuerzen();
        a->zeige_teil = true;
        a->neu_teil = true;
        a->material(B, Hh, d.dicke);          // dasselbe Material wie im Fenster «3D-Modell»
        // Linien der Zeichnung brauchen das neue Raster (Höhe): ausserhalb des GL-Kontexts bauen
        Glib::signal_idle().connect_once([this] {
            linien_bauen();
            leiste_auffrischen();
            bahn_liste_bauen();
            if (g_getenv("HIRCH_TEST_VERGLEICH")) f_vergl.set_active(true);   // Test: Färben nach Vergleich
            if (g_getenv("HIRCH_TEST_NEUTRAL")) f_neutral.set_active(true);   // Test: neutral (nur die Form)
            if (g_getenv("HIRCH_TEST_VERSCHIEBEN") && a) {      // Test: mittlere Taste ziehen, dann zur Maus zoomen
                auto w = GTK_WIDGET(a->bereich.gobj());
                auto knopf = [&](GdkEventType t, double x, double y) {
                    GdkEvent *e = gdk_event_new(t);
                    e->button.window = GDK_WINDOW(g_object_ref(gtk_widget_get_window(w)));
                    e->button.button = 2;
                    e->button.x = x;
                    e->button.y = y;
                    gtk_widget_event(w, e);
                    gdk_event_free(e);
                };
                knopf(GDK_BUTTON_PRESS, 500, 400);
                GdkEvent *m = gdk_event_new(GDK_MOTION_NOTIFY);
                m->motion.window = GDK_WINDOW(g_object_ref(gtk_widget_get_window(w)));
                m->motion.x = 750;
                m->motion.y = 300;
                gtk_widget_event(w, m);
                gdk_event_free(m);
                knopf(GDK_BUTTON_RELEASE, 750, 300);
                for (int k = 0; k < 6; ++k) {
                    GdkEvent *sc = gdk_event_new(GDK_SCROLL);
                    sc->scroll.window = GDK_WINDOW(g_object_ref(gtk_widget_get_window(w)));
                    sc->scroll.direction = GDK_SCROLL_UP;
                    sc->scroll.x = 300;
                    sc->scroll.y = 350;
                    gtk_widget_event(w, sc);
                    gdk_event_free(sc);
                }
                g_message("Hirch 3D Verschieben: Versatz %.1f %.1f %.1f, Abstand %.1f", a->versatz.x, a->versatz.y,
                          a->versatz.z, a->abstand);
            }
            if (g_getenv("HIRCH_TEST_VORSCHAU_ZU")) {           // Test: alte Vorschau öffnen, schliessen, weiter drehen
                Glib::signal_timeout().connect_once([this] {
                    std::function<Gtk::Button *(Gtk::Widget *)> suche = [&](Gtk::Widget *w) -> Gtk::Button * {
                        if (auto b = dynamic_cast<Gtk::Button *>(w))
                            if (b->get_label() == TR("Vorschau 3D …")) return b;
                        if (auto c = dynamic_cast<Gtk::Container *>(w))
                            for (auto k : c->get_children())
                                if (auto r = suche(k)) return r;
                        return nullptr;
                    };
                    Gtk::Button *b = nullptr;
                    for (auto t : Gtk::Window::list_toplevels())
                        if (!b) b = suche(t);
                    g_message("Hirch 3D Vorschau-Test: Knopf %s", b ? "gefunden" : "fehlt");
                    if (b) b->clicked();
                    Glib::signal_timeout().connect_once([this] {
                        auto haupt = dynamic_cast<Gtk::Window *>(get_toplevel());
                        for (auto t : Gtk::Window::list_toplevels())
                            if (t != haupt && t->get_visible() && t->get_transient_for() == haupt) {
                                g_message("Hirch 3D Vorschau-Test: schliesse «%s»", t->get_title().c_str());
                                GdkEvent *e = gdk_event_new(GDK_DELETE);
                                e->any.window = GDK_WINDOW(g_object_ref(t->get_window()->gobj()));
                                gtk_main_do_event(e);
                                gdk_event_free(e);
                            }
                        Glib::signal_timeout().connect_once([this] {
                            if (a) {
                                a->az += 20;
                                a->bereich.queue_render();
                            }
                            Glib::signal_timeout().connect_once([] { g_message("Hirch 3D Vorschau-Test: überlebt"); }, 2000);
                        }, 1500);
                    }, 8000);
                }, 500);
            }
            if (g_getenv("HIRCH_TEST_ZEICHNEN_ZAEHLEN")) {      // Messung: welche Widgets je Bild neu zeichnen
                static std::map<std::string, int> zahl;
                static bool an = false;
                if (!an) {
                    an = true;
                    guint sig = g_signal_lookup("draw", GTK_TYPE_WIDGET);
                    g_signal_add_emission_hook(sig, 0, [](GSignalInvocationHint *, guint, GValue const *p, gpointer) -> gboolean {
                        auto w = GTK_WIDGET(g_value_get_object(p));
                        std::string n = G_OBJECT_TYPE_NAME(w);
                        if (auto nm = gtk_widget_get_name(w); nm && std::string(nm) != n) n += std::string("#") + nm;
                        n += "[" + std::to_string(gtk_widget_get_allocated_width(w)) + "x" +
                             std::to_string(gtk_widget_get_allocated_height(w));
                        for (auto q = gtk_widget_get_parent(w); q; q = gtk_widget_get_parent(q)) {
                            auto qn = gtk_widget_get_name(q);
                            n += std::string("<") + (qn ? qn : G_OBJECT_TYPE_NAME(q));
                            if (n.size() > 400) break;
                        }
                        n += "]";
                        zahl[n]++;
                        return TRUE;
                    }, nullptr, nullptr);
                    Glib::signal_timeout().connect([] {
                        std::vector<std::pair<int, std::string>> v;
                        for (auto &[k, n] : zahl) v.push_back({n, k});
                        std::sort(v.rbegin(), v.rend());
                        std::string t;
                        for (std::size_t i = 0; i < std::min<std::size_t>(12, v.size()); ++i)
                            t += " " + v[i].second + "=" + std::to_string(v[i].first);
                        g_message("Hirch 3D Zeichnen/s:%s", t.c_str());
                        zahl.clear();
                        return true;
                    }, 2000);
                }
            }
            if (auto k = g_getenv("HIRCH_TEST_KAMERA"); k && a) {  // Test: az, el, Abstand, Blickpunkt x, y, z (mm)
                double v[6] = {0, 0, 0, 0, 0, 0};
                std::istringstream is(k);
                std::string t;
                for (int i = 0; i < 6 && std::getline(is, t, ','); ++i) v[i] = g_ascii_strtod(t.c_str(), nullptr);
                a->az = v[0];
                a->el = v[1];
                a->seite_nachfuehren();                         // wie beim Drehen mit der Maus: Knöpfe mitführen
                a->abstand = v[2];
                a->versatz = V3{v[3] - a->B / 2, v[4] - a->H / 2, v[5] + a->D / 2};
                a->bereich.queue_render();
            }
            if (g_getenv("HIRCH_TEST_OHNE_UEBERLAGE")) {        // Messung: ohne Bedienung über der GL-Fläche
                schalter.hide();
                leiste.hide();
                b_auf.hide();
            }
            if (auto dv = g_getenv("HIRCH_TEST_DREHEN")) {       // Test: 8 s lang drehen (Wert: erst nach so vielen s)
                double warte = std::max(0.0, g_ascii_strtod(dv, nullptr) - 1);
                auto start = std::make_shared<gint64>(g_get_monotonic_time() + gint64(warte * 1e6));
                Glib::signal_timeout().connect([this, start] {
                    if (g_get_monotonic_time() < *start) return true;
                    if (!a || g_get_monotonic_time() - *start > 8000000) return false;
                    a->az += 1.5;
                    a->bereich.queue_render();
                    return true;
                }, 16);
            }
            if (g_getenv("HIRCH_TEST_LINIENKLICK"))              // Test: Klick und Doppelklick auf eine Linie
                Glib::signal_timeout().connect_once([this] { linienklick_testen(); }, 2500);
            if (auto n = g_getenv("HIRCH_TEST_SPRUNG")) {          // Test: Sprung aus dem Panel (Bearbeitung Nr. n)
                int q = atoi(n);
                if (q >= 0 && q < int(bea_ids.size()))
                    hirch_ansicht_3d_springen(desk, bea_ids[q].substr(0, bea_ids[q].find('#')));
            }
            if (auto f = g_getenv("HIRCH_TEST_ABSPIELEN")) {      // Test: Stelle der Abspielleiste (0…1)
                double q = g_ascii_strtod(f, nullptr);
                auto w = g_getenv("HIRCH_TEST_ABSPIELEN_WAHL");    // Test: Abschnitt wählen (Index)
                if (w && !ablauf.empty()) {
                    auto const &A = ablauf[std::clamp(atoi(w), 0, int(ablauf.size()) - 1)];
                    wahl = A.bea + 1;
                }
                stelle_setzen(q * t_ges());
                if (auto hz = g_getenv("HIRCH_TEST_HALT"))        // Test: Karte zum Halt (Index) zeigen
                    halt_zeigen(atoi(hz));
                if (g_getenv("HIRCH_TEST_ABSPIELEN_PLAY")) {
                    b_tempo[4].set_active(true);
                    spielen(true);
                }
            }
        });
    }

    // ------------------------------------------------------------------ Abspielleiste
    static std::map<SPDesktop *, AnsichtFenster *> &alle()
    {
        static std::map<SPDesktop *, AnsichtFenster *> m;
        return m;
    }
    std::vector<std::string> bea_ids;       // Schlüssel je Bearbeitung (engine «abschnitte» key)

    /// Panel → Zeitleiste (Basis, 22.10. 10:50): an den Anfang der Bearbeitung springen und sie wählen.
    /// Schlüssel genau, sonst der erste Teil «Schlüssel#…» (Räumen, Schlichten, Kernloch …)
    void springen(std::string const &schluessel)
    {
        int nr = -1;
        for (std::size_t q = 0; q < bea_ids.size() && nr < 0; ++q)
            if (bea_ids[q] == schluessel) nr = int(q);
        for (std::size_t q = 0; q < bea_ids.size() && nr < 0; ++q)
            if (bea_ids[q].rfind(schluessel + "#", 0) == 0) nr = int(q);
        if (nr < 0) return;
        for (auto const &A : ablauf)
            if (A.bea == nr) {
                spielen(false);
                wahl = nr + 1;
                stelle_setzen(A.t0);
                return;
            }
    }

    double t_ges() const { return zeit.empty() ? 0.0 : zeit.back(); }
    bool am_ende() const { return zeit.empty() || t_jetzt >= t_ges() - 1e-9; }

    static std::string uhr(double sek)
    {
        long s_ = std::max(0L, long(std::lround(sek)));
        char b[32];
        if (s_ >= 3600) std::snprintf(b, sizeof b, "%ld:%02ld:%02ld", s_ / 3600, s_ / 60 % 60, s_ % 60);
        else std::snprintf(b, sizeof b, "%ld:%02ld", s_ / 60, s_ % 60);
        return b;
    }

    /// Zeiten (wie die Laufzeit der Vorschau: Eilgang 3000 mm/min, sonst Vorschub), Abschnitte je
    /// Bearbeitung in der Reihenfolge des Fräsens, Eilgänge ins Material
    void ablauf_bauen(VorschauDaten const &d, std::vector<AbtragBewegung> const &m,
                      std::vector<AbtragBearbeitung> const &bea)
    {
        std::vector<std::string> namen(bea.size());
        seite_abspiel = -1;
        bea_ids.clear();
        for (auto const &B : bea) bea_ids.push_back(B.id);
        for (std::size_t i = 0; i < d.programme.size(); ++i)
            for (auto const &A : d.programme[i].abschnitte) {
                std::string id = A.id.empty() ? std::to_string(i) + ":" + A.name : A.id;
                for (std::size_t q = 0; q < bea.size(); ++q)
                    if (bea[q].id == id && namen[q].empty()) namen[q] = A.name;
            }
        zeit.assign(m.size(), 0.0);
        ablauf.clear();
        eil_rein.clear();
        eil_kand.clear();
        double t = 0, px = 0, py = 0, pz = d.sicherheit;
        int prog = -1;
        for (std::size_t k = 0; k < m.size(); ++k) {
            auto const &b = m[k];
            if (b.p != prog) {
                px = py = 0;
                pz = d.sicherheit;
                prog = b.p;
            }
            double dxy = std::hypot(b.x - px, b.y - py), dd = std::hypot(dxy, b.z - pz);
            double t_vor = t;
            t += b.eil ? dd / 3000 * 60 : (b.f > 0 ? dd / b.f * 60 : 0);
            zeit[k] = t;
            if (b.eil && std::min(b.z, pz) < -EIL_TOLERANZ) eil_kand.push_back({int(k), px, py, pz, t_vor});
            px = b.x;
            py = b.y;
            pz = b.z;
            // Eilgänge gehören zum laufenden Abschnitt; ein neuer beginnt mit Vorschub einer anderen Bearbeitung
            bool neu = ablauf.empty() || b.p != ablauf.back().p || (!b.eil && b.bearbeitung != ablauf.back().bea);
            if (neu) {
                if (!ablauf.empty() && ablauf.back().bea < 0 && b.p == ablauf.back().p) {
                    ablauf.back().bea = b.bearbeitung;     // Anfahrt vor der ersten Bearbeitung
                } else {
                    Abschnitt A;
                    A.t0 = t_vor;
                    A.p = b.p;
                    A.bea = b.eil ? -1 : b.bearbeitung;
                    ablauf.push_back(A);
                }
            }
            ablauf.back().t1 = t;
        }
        // leere Abschnitte (nur Eilgang) dem davor zuschlagen; Namen und Farben
        std::vector<Abschnitt> rein;
        for (auto &A : ablauf) {
            if (A.bea < 0 && !rein.empty() && rein.back().p == A.p) {
                rein.back().t1 = A.t1;
                continue;
            }
            rein.push_back(A);
        }
        ablauf = std::move(rein);
        for (auto &A : ablauf) {
            auto const *P = A.p >= 0 && A.p < int(d.programme.size()) ? &d.programme[A.p] : nullptr;
            A.wz = P ? P->werkzeug : std::string();
            A.seite2 = P && d.zweiseitig && P->seite == 2;
            A.spannung = P && P->spannung == 2 ? 2 : 1;
            if (A.bea >= 0 && A.bea < int(bea.size())) {
                A.farbe = hex_farbe(bea[A.bea].farbe, A.farbe);
                A.name = namen[A.bea];
            }
            if (A.name.empty()) A.name = A.wz;
        }
        halte.clear();
        for (std::size_t i = 1; i < ablauf.size(); ++i) {
            auto const &v = ablauf[i - 1], &A = ablauf[i];
            if (A.seite2 != v.seite2) halte.push_back({A.t0, true});
            else if (A.spannung == 2 && v.spannung != 2) halte.push_back({A.t0, false});
        }
        karte_zeigen(-1);
        spann_phase = -1;
        spielt = false;
        takt.disconnect();
        t_jetzt = t_gerechnet = t_ges();
    }

    static constexpr double ZL_OBEN = 20, ZL_UNTEN = 8;   // Zeitleiste: Platz für Zeit und Dreiecke (B-29)
    static constexpr double EIL_TOLERANZ = 0.03;   // mm, wie kollision.TOLERANZ (Rundung des Rasters)

    /// Eilgänge ins Material wie kollision.trifft, aber gegen die Höhenbilder von hirch-abtrag zum Zeitpunkt der
    /// Bewegung: Material unter der Fräserunterseite (Profil des Werkzeugs) auf dem Weg, ausser ganz durchgefräst.
    /// Eilgänge in schon ausgeräumten Taschen zählen nicht. Im GL-Kontext; liest nur das Fenster um den Weg.
    /// Ein Stück der Prüfung (höchstens ~40 ms, im Leerlauf mit dem GL-Kontext der Ansicht)
    void eil_schritt()
    {
        if (!teil || bew.empty() || !eil_ab.tex_h) {
            eil_laeuft = false;
            return;
        }
        auto const &d = teil->d;
        auto const &m = bew;
        auto const &wz = eil_wz;
        auto &abtrag = eil_ab;
        gint64 bis_uhr = g_get_monotonic_time() + 40000;
        GLint alt_fb = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &alt_fb);
        GLuint fb = 0;
        glGenFramebuffers(1, &fb);
        double zx = abtrag.w / abtrag.W, zy = abtrag.h / abtrag.H, zelle = std::min(zx, zy);
        double hmin = abtrag.hmin;
        std::vector<float> fenster;
        int gebunden = 0;
        for (; eil_i < eil_kand.size() && g_get_monotonic_time() < bis_uhr; ++eil_i) {
            auto const &K = eil_kand[eil_i];
            auto const &b = m[K.k];
            int prog = b.p;
            AbtragWerkzeug W_ = prog >= 0 && prog < int(wz.size()) ? wz[prog] : AbtragWerkzeug{};
            int seite = prog >= 0 && prog < int(d.programme.size()) && d.zweiseitig && d.programme[prog].seite == 2 ? 2 : 1;
            double r = std::max(0.01, W_.durchmesser / 2);
            double tanh = std::tan(std::clamp(W_.vwinkel, 1.0, 179.0) * M_PI / 360), sp = std::max(0.0, W_.spitze) / 2;
            double er = W_.form == "kugel" ? r : std::clamp(W_.eckradius, 0.0, r);
            auto profil = [&](double dd) {
                if (W_.form == "v") return std::max(0.0, dd - sp) / tanh;
                if (W_.form == "konisch") {
                    double rb = std::max(0.01, er), halb = std::atan(tanh), dt = rb * std::cos(halb),
                           ht = rb - rb * std::sin(halb);
                    return dd <= dt ? rb - std::sqrt(std::max(0.0, rb * rb - std::min(dd, rb) * std::min(dd, rb)))
                                    : ht + (dd - dt) / tanh;
                }
                if (er > 0) {
                    double innen = r - er, q = std::clamp(dd - innen, 0.0, er);
                    return dd <= innen ? 0.0 : er - std::sqrt(std::max(0.0, er * er - q * q));
                }
                return 0.0;
            };
            // Fenster in Zellen
            int i0 = std::max(0, int(std::floor((std::min(K.ax, b.x) - r - abtrag.x0) / zx)));
            int i1 = std::min(abtrag.W, int(std::ceil((std::max(K.ax, b.x) + r - abtrag.x0) / zx)) + 1);
            int j0 = std::max(0, int(std::floor((std::min(K.ay, b.y) - r - abtrag.y0) / zy)));
            int j1 = std::min(abtrag.H, int(std::ceil((std::max(K.ay, b.y) + r - abtrag.y0) / zy)) + 1);
            if (i0 >= i1 || j0 >= j1) continue;
            abtrag.rechnen(K.k, 0);                  // Stand vor dem Eilgang (vorwärts: nur das Neue)
            GLuint th = seite == 2 ? abtrag.tex_h2 : abtrag.tex_h;
            glBindFramebuffer(GL_FRAMEBUFFER, fb);
            if (gebunden != int(th)) {
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, th, 0);
                glDrawBuffer(GL_NONE);
                glReadBuffer(GL_NONE);
                gebunden = int(th);
            }
            int fw = i1 - i0, fh = j1 - j0;
            fenster.resize(std::size_t(fw) * fh);
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            glReadPixels(i0, j0, fw, fh, GL_DEPTH_COMPONENT, GL_FLOAT, fenster.data());
            double vx = b.x - K.ax, vy = b.y - K.ay, l2 = vx * vx + vy * vy;
            bool trifft = false;
            for (int j = 0; j < fh && !trifft; ++j)
                for (int i = 0; i < fw; ++i) {
                    double x = abtrag.x0 + (i0 + i + 0.5) * zx, y = abtrag.y0 + (j0 + j + 0.5) * zy;
                    if (d.rund_r > 0 && std::hypot(x - d.breite / 2, y - d.hoehe / 2) > d.rund_r) continue;
                    double t = l2 > 1e-12 ? std::clamp(((x - K.ax) * vx + (y - K.ay) * vy) / l2, 0.0, 1.0) : 0.0;
                    double dd = std::hypot(x - (K.ax + t * vx), y - (K.ay + t * vy));
                    if (dd > r - 0.5 * zelle) continue;
                    double oben = hmin * (1.0 - fenster[std::size_t(j) * fw + i]);
                    if (oben <= -d.dicke + 1e-3) continue;          // ganz durch: kein Material mehr
                    double unten = K.az + t * (b.z - K.az) + profil(dd);
                    if (oben - unten > EIL_TOLERANZ) {
                        trifft = true;
                        break;
                    }
                }
            if (trifft) eil_rein.push_back(K.t);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, GLuint(alt_fb));
        glDeleteFramebuffers(1, &fb);
        bool fertig = eil_i >= eil_kand.size();
        if (fertig) {
            eil_laeuft = false;
            eil_ab.puffer_weg();
            if (g_getenv("HIRCH_TEST_EILZEIT"))
                g_message("Hirch 3D: %zu Eilgänge unter Z0 geprüft, %zu treffen, %.2f s", eil_kand.size(),
                          eil_rein.size(), (g_get_monotonic_time() - eil_uhr) / 1e6);
        }
        if (fertig) leiste_auffrischen();
    }

    /// Stelle (fertige Bewegungen, Anteil der nächsten) zur Zeit t
    void stelle_von(double t, int &bis, double &anteil) const
    {
        auto it = std::upper_bound(zeit.begin(), zeit.end(), t);
        bis = int(it - zeit.begin());
        anteil = 0;
        if (bis >= int(zeit.size())) return;
        double a = bis > 0 ? zeit[bis - 1] : 0, b = zeit[bis];
        anteil = b > a ? std::clamp((t - a) / (b - a), 0.0, 1.0) : 0.0;
    }

    /// im GL-Kontext: Höhenbilder auf die Stelle der Abspielleiste bringen
    void stelle_rechnen()
    {
        if (mutter_pruefen_bald && abtrag_gl && t_gerechnet == t_jetzt) {
            mutter_pruefen_bald = false;
            mutter_pruefen();
        }
        if (!abtrag_gl || bew.empty() || t_gerechnet == t_jetzt) return;
        int bis;
        double anteil;
        stelle_von(t_jetzt, bis, anteil);
        abtrag.rechnen(bis, anteil);
        pyramide_neu = true;
        t_gerechnet = t_jetzt;
        sicht_kuerzen();
        mutter_pruefen();
    }

    // --- Spannmutter und Spindel (Peter 05.11.) ---------------------------------------------------------------
    bool mutter_pruefen_bald = false;
    gint64 mutter_uhr = 0;
    bool mutter_da() const
    {
        return teil && h_mutter.get_active() && teil->d.mutter_d > 0 && teil->d.mutter_h > 0;
    }
    /// Unterkante der Mutter über der Fräserspitze: Ausspannlänge, sonst wie relief_halterung (Schneide + Übergang +
    /// 5 mm), ohne alles 25 mm wie fraeser_netz
    static double ausspann(VorschauProgramm const &P)
    {
        if (P.ausspann > 0) return P.ausspann;
        if (P.schneide > 0) return P.schneide + P.uebergang + 5.0;
        return 25.0;
    }
    /// Zylinder um die Achse (Spitze bei 0) als Dreiecke mit Normalen (x, y, z, nx, ny, nz) – Mantel und Deckel
    static void zylinder(double r, double z0, double z1, std::vector<float> &v, int n = 48)
    {
        auto ecke = [&](double x, double y, double z, double nx, double ny, double nz) {
            v.insert(v.end(), {float(x), float(y), float(z), float(nx), float(ny), float(nz)});
        };
        for (int k = 0; k < n; ++k) {
            double a0 = 2 * M_PI * k / n, a1 = 2 * M_PI * (k + 1) / n;
            double c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
            ecke(r * c0, r * s0, z0, c0, s0, 0);
            ecke(r * c1, r * s1, z0, c1, s1, 0);
            ecke(r * c1, r * s1, z1, c1, s1, 0);
            ecke(r * c0, r * s0, z0, c0, s0, 0);
            ecke(r * c1, r * s1, z1, c1, s1, 0);
            ecke(r * c0, r * s0, z1, c0, s0, 0);
            ecke(0, 0, z1, 0, 0, 1);                     // Deckel oben
            ecke(r * c0, r * s0, z1, 0, 0, 1);
            ecke(r * c1, r * s1, z1, 0, 0, 1);
            ecke(0, 0, z0, 0, 0, -1);                    // unten
            ecke(r * c1, r * s1, z0, 0, 0, -1);
            ecke(r * c0, r * s0, z0, 0, 0, -1);
        }
    }
    void mutter_info()
    {
        if (!teil) {
            h_mutter_info.hide();
            return;
        }
        auto const &d = teil->d;
        auto z = [](double v) {
            char b[32];
            std::snprintf(b, sizeof b, "%g", std::round(v * 10) / 10);
            return std::string(b);
        };
        if (d.mutter_d > 0 && d.mutter_h > 0)
            h_mutter_info.set_text("Ø " + z(d.mutter_d) + " × " + z(d.mutter_h) + " mm" +
                                   (d.spindel_d > 0 ? " · " + TRF("Spindel Ø {}", {z(d.spindel_d)}) : std::string()));
        else
            h_mutter_info.set_text(TR("Masse unter «Spindel und Halterung» eintragen"));
        h_mutter_info.show();
        h_mutter.set_sensitive(d.mutter_d > 0 && d.mutter_h > 0);
    }
    /// Abstand von Mutter und Spindel zum Material an der Stelle (Höhenbild der Seite, braucht den GL-Kontext):
    /// orange unter 2 mm, rot bei Berührung. Nur, wenn die Mutter tiefer als 2 mm über der Oberfläche kommt;
    /// beim Abspielen höchstens alle 0,2 s (das Höhenbild zu lesen kostet).
    void mutter_pruefen()
    {
        if (!a) return;
        int alt = a->mutter_zustand;
        a->mutter_zustand = 0;
        if (!mutter_da() || !abtrag_gl || bew.empty() || am_ende()) {
            if (alt) a->bereich.queue_render();
            return;
        }
        auto const &d = teil->d;
        int bis;
        double anteil;
        stelle_von(t_jetzt, bis, anteil);
        bis = std::clamp(bis, 0, int(bew.size()) - 1);
        auto const &b = bew[bis];
        if (b.p < 0 || b.p >= int(d.programme.size())) return;
        double px = b.x, py = b.y, pz = d.sicherheit;
        if (bis > 0 && bew[bis - 1].p == b.p) {
            px = bew[bis - 1].x;
            py = bew[bis - 1].y;
            pz = bew[bis - 1].z;
        }
        double x = px + anteil * (b.x - px), y = py + anteil * (b.y - py), z = pz + anteil * (b.z - pz);
        auto const &P = d.programme[b.p];
        double unten = z + ausspann(P);                  // Unterkante der Mutter (Material-Z der Seite, oben 0)
        if (unten >= 2.0) {                              // über der Oberfläche: nichts zu prüfen
            if (std::getenv("HIRCH_3D_MUTTER"))
                std::fprintf(stderr, "Hirch-3D: Mutter t=%.1f s, Unterkante %.2f über der Oberfläche\n", t_jetzt, unten);
            if (alt) a->bereich.queue_render();
            return;
        }
        gint64 jetzt = g_get_monotonic_time();
        if (spielt && jetzt - mutter_uhr < 200000) {
            a->mutter_zustand = alt;
            return;
        }
        mutter_uhr = jetzt;
        std::vector<float> h;
        int seite = d.zweiseitig && P.seite == 2 ? 2 : 1;
        if (!abtrag.lesen(seite, h) || h.size() < std::size_t(abtrag.W) * abtrag.H) return;
        double zx = abtrag.w / abtrag.W, zy = abtrag.h / abtrag.H;
        auto hoechste = [&](double r) {                  // höchstes Material im Kreis um (x, y)
            double m = -1e9;
            int i0 = std::max(0, int((x - r - abtrag.x0) / zx)), i1 = std::min(abtrag.W - 1, int((x + r - abtrag.x0) / zx));
            int j0 = std::max(0, int((y - r - abtrag.y0) / zy)), j1 = std::min(abtrag.H - 1, int((y + r - abtrag.y0) / zy));
            for (int j = j0; j <= j1; ++j) {
                double cy = abtrag.y0 + (j + 0.5) * zy - y;
                for (int i = i0; i <= i1; ++i) {
                    double cx = abtrag.x0 + (i + 0.5) * zx - x;
                    if (cx * cx + cy * cy <= r * r) m = std::max(m, double(h[std::size_t(j) * abtrag.W + i]));
                }
            }
            return m;
        };
        double luft = unten - hoechste(d.mutter_d / 2);
        if (d.spindel_d > 0) luft = std::min(luft, unten + d.mutter_h - hoechste(d.spindel_d / 2));
        a->mutter_zustand = luft < 0.05 ? 2 : luft < 2.0 ? 1 : 0;
        if (std::getenv("HIRCH_3D_MUTTER"))             // Messung: Abstand ins Protokoll
            std::fprintf(stderr, "Hirch-3D: Mutter t=%.1f s, Spitze Z %.2f, Unterkante %.2f, Luft %.2f mm, Zustand %d\n",
                         t_jetzt, z, unten, luft, a->mutter_zustand);
        if (a->mutter_zustand != alt) a->bereich.queue_render();
    }

    /// Fräser an der Stelle der Abspielleiste (Form des Werkzeugs, Schaft nach oben bzw. auf Seite 2 nach unten);
    /// am Ende keiner
    void fraeser_bauen()
    {
        if (!a) return;
        auto &e = a->fraeser_ecken;
        e.clear();
        a->neu_fraeser = true;
        if (!teil || bew.empty() || am_ende()) return;
        auto const &d = teil->d;
        int bis;
        double anteil;
        stelle_von(t_jetzt, bis, anteil);
        bis = std::clamp(bis, 0, int(bew.size()) - 1);
        auto const &b = bew[bis];
        double px = 0, py = 0, pz = d.sicherheit;
        if (bis > 0 && bew[bis - 1].p == b.p) {
            px = bew[bis - 1].x;
            py = bew[bis - 1].y;
            pz = bew[bis - 1].z;
        }
        double x = px + anteil * (b.x - px), y = py + anteil * (b.y - py), z = pz + anteil * (b.z - pz);
        if (b.p < 0 || b.p >= int(d.programme.size())) return;
        auto const &P = d.programme[b.p];
        bool s2 = d.zweiseitig && P.seite == 2;
        // Form (Schneide mit Spannuten im Drall, Schaft bis zur Spannmutter …) wie im Fenster «Vorschau 3D»;
        // «drehung» lässt ihn sich drehen (Peter 26.10.)
        FraeserNetz fn;
        fraeser_netz(P, drehung, fn);
        auto v = std::move(fn.ecken);
        std::size_t vor = v.size();
        double dx = 0, dy = 0;
        if (!b.eil && bis > 0 && bew[bis - 1].p == b.p && std::hypot(b.x - px, b.y - py) > 1e-6) {
            dx = b.x - px;
            dy = b.y - py;
        }
        // B-17 (Peter 02.11.): kein Pfeil mehr für die Fahrrichtung – er verwirrte; der Fräser dreht sich weiter
        (void)dx;
        (void)dy;
        a->n_fr_schneide = fn.n_schneide;
        a->n_fr_oben = fn.n_oben;
        a->n_fr_pfeil = GLsizei((v.size() - vor) / 6);
        a->n_fr_mutter = a->n_fr_spindel = 0;
        if (mutter_da()) {                       // Spannmutter, darüber 30 mm Spindel (Peter 05.11.)
            double A = ausspann(P), mh = d.mutter_h;
            std::size_t n0 = v.size();
            zylinder(d.mutter_d / 2, A, A + mh, v);
            a->n_fr_mutter = GLsizei((v.size() - n0) / 6);
            if (d.spindel_d > 0) {
                std::size_t n1 = v.size();
                zylinder(d.spindel_d / 2, A + mh, A + mh + 30.0, v);
                a->n_fr_spindel = GLsizei((v.size() - n1) / 6);
            }
        }
        e.reserve(v.size());
        for (std::size_t k = 0; k + 5 < v.size(); k += 6) {
            double wx = x + v[k], wy = y + v[k + 1], wz = z + v[k + 2];
            double nx = v[k + 3], ny = v[k + 4], nzz = v[k + 5];
            if (s2) {                            // Seite 2: gewendet (wie die Fräsbahnen)
                if (d.um_y) {
                    wx = d.breite - wx;
                    nx = -nx;
                } else {
                    wy = d.hoehe - wy;
                    ny = -ny;
                }
                wz = -d.dicke - wz;
                nzz = -nzz;
            }
            e.insert(e.end(), {float(wx), float(wy), float(wz), float(nx), float(ny), float(nzz)});
        }
    }

    /// Fräsbahnen und Gewinde nur bis zur Stelle zeigen
    void sicht_kuerzen()
    {
        if (!a) return;
        fraeser_bauen();
        spann_bauen();
        if (am_ende()) {
            a->n_bahn_sicht = a->n_gew_sicht = a->n_eil_sicht = -1;
            return;
        }
        int bis;
        double anteil;
        stelle_von(t_jetzt, bis, anteil);
        a->n_bahn_sicht = GLsizei(2 * (std::lower_bound(bahn_bis.begin(), bahn_bis.end(), bis) - bahn_bis.begin()));
        a->n_eil_sicht = GLsizei(2 * (std::lower_bound(eil_bis.begin(), eil_bis.end(), bis) - eil_bis.begin()));
        int p = bew.empty() ? 0 : bew[std::min<std::size_t>(bis, bew.size() - 1)].p;
        int n = 0;
        for (auto const &[prog, ende] : gew_enden) {
            if (prog < 0 || prog >= p) break;
            n = ende;
        }
        a->n_gew_sicht = n;
    }

    double faktor() const
    {
        static double const f[4] = {0.25, 1, 10, 50};
        return tempo_i < 4 ? f[tempo_i] : std::max(1e-3, t_ges() / 15.0);
    }

    void stelle_setzen(double t)
    {
        t_jetzt = std::clamp(t, 0.0, t_ges());
        if (karte_halt >= 0 && (karte_halt >= int(halte.size()) || std::abs(t_jetzt - (halte[karte_halt].t - HALT_VOR)) > 1e-6))
            karte_zeigen(-1);
        if (spielt) {
            spiel_t0 = t_jetzt;
            spiel_uhr = g_get_monotonic_time();
        }
        sicht_kuerzen();
        seite_folgen();
        if (a) a->bereich.queue_render();
        leiste_auffrischen();
    }

    /// Abspielen (Peter 28.10.): wechselt die Stelle die Seite, wendet sich die Ansicht wie das Teil auf der
    /// Maschine – Seite 2 von oben, der Fräser kommt von oben. Nur beim Wechsel, frei Gedrehtes bleibt sonst.
    int seite_abspiel = -1;
    void seite_folgen()
    {
        if (!a || !teil || !teil->d.zweiseitig || ablauf.empty()) return;
        int s = phase_jetzt().first;
        if (seite_abspiel >= 0 && s != seite_abspiel && (s == 2) != a->von_unten) a->ansicht_wenden(true);
        seite_abspiel = s;
    }

    void spielen(bool an)
    {
        if (an && am_ende()) t_jetzt = 0;
        if (an) karte_zeigen(-1);
        spielt = an && !zeit.empty();
        takt.disconnect();
        if (spielt) {
            spiel_t0 = t_jetzt;
            spiel_uhr = g_get_monotonic_time();
            takt = Glib::signal_timeout().connect([this] {
                double t = spiel_t0 + (g_get_monotonic_time() - spiel_uhr) / 1e6 * faktor();
                int h = naechster_halt(spiel_t0);
                if (h >= 0 && t >= halte[h].t - HALT_VOR) {   // Wenden, Umspannen: anhalten
                    spielt = false;
                    halt_zeigen(h);
                    return false;
                }
                if (t >= t_ges()) {
                    spielt = false;
                    stelle_setzen(t_ges());
                    return false;
                }
                stelle_setzen(t);
                return true;
            }, 30);
        }
        stelle_setzen(t_jetzt);
    }

    Abschnitt const *abschnitt_bei(double t) const
    {
        for (auto const &A : ablauf)
            if (t < A.t1) return &A;
        return ablauf.empty() ? nullptr : &ablauf.back();
    }

    static constexpr double HALT_VOR = 1e-4;   // s: beim Halt knapp vor dem neuen Abschnitt stehen (noch der alte)

    /// erster Halt nach der Zeit t (Index in halte), -1: keiner
    int naechster_halt(double t) const
    {
        for (std::size_t i = 0; i < halte.size(); ++i)
            if (halte[i].t - HALT_VOR > t + 1e-6) return int(i);
        return -1;
    }

    void halt_zeigen(int h)
    {
        if (h < 0 || h >= int(halte.size())) return;
        spielt = false;
        takt.disconnect();
        stelle_setzen(halte[h].t - HALT_VOR);
        karte_zeigen(h);
    }

    /// Seite und Spannung an der Stelle (am Ende: die letzte Seite, Spannung 1 – so sind die Spannmittel zu sehen)
    std::pair<int, int> phase_jetzt() const
    {
        auto A = abschnitt_bei(t_jetzt);
        if (!A) return {1, 1};
        return {A->seite2 ? 2 : 1, am_ende() ? 1 : A->spannung};
    }

    void spann_bauen()
    {
        if (!a) return;
        auto [seite, spannung] = phase_jetzt();
        int ph = teil && teil->d.meldung.empty() ? seite * 10 + spannung : 0;
        if (ph == spann_phase && spann_ansicht == a.get()) return;
        spann_phase = ph;
        spann_ansicht = a.get();
        if (ph) spann_szene(teil->d, seite, spannung, a->spann_ecken, a->n_sp_schraube, a->zone_flaeche, a->zone_umriss);
        else {
            a->spann_ecken.clear();
            a->zone_flaeche.clear();
            a->zone_umriss.clear();
            a->n_sp_schraube = 0;
        }
        a->neu_spann = true;
    }

    void karte_bauen()
    {
        klasse(karte, "hirch-3d-karte");
        karte.set_halign(Gtk::ALIGN_CENTER);
        karte.set_valign(Gtk::ALIGN_CENTER);
        karte.set_size_request(470, -1);
        karte.set_no_show_all(true);
        auto kopf = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 10);
        klasse(k_zeichen, "hirch-3d-karte-zeichen");
        klasse(k_titel, "hirch-3d-karte-titel");
        klasse(k_bei, "hirch-3d-karte-dim");
        k_bei.set_hexpand(true);
        k_bei.set_xalign(1);
        kopf->add(k_zeichen);
        kopf->add(k_titel);
        kopf->add(k_bei);
        karte.add(*kopf);
        for (auto l : {&k_text, &k_schritte}) {
            l->set_xalign(0);
            l->set_line_wrap(true);
            l->set_max_width_chars(52);
            karte.add(*l);
        }
        klasse(k_text, "hirch-3d-karte-dim");
        auto knoepfe = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 10);
        klasse(k_weiter, "suggested-action");
        knoepfe->add(k_weiter);
        knoepfe->add(k_anfang);
        karte.add(*knoepfe);
        k_anfang.set_label(TR("Zurück zum Anfang"));
        k_weiter.signal_clicked().connect([this] {
            if (karte_halt < 0 || karte_halt >= int(halte.size())) return;
            double t = halte[karte_halt].t;
            karte_zeigen(-1);
            stelle_setzen(t);
            spielen(true);
        });
        k_anfang.signal_clicked().connect([this] {
            karte_zeigen(-1);
            spielen(false);
            stelle_setzen(0);
        });
        for (auto w : std::initializer_list<Gtk::Widget *>{kopf, &k_zeichen, &k_titel, &k_bei, &k_text, &k_schritte,
                                                           knoepfe, &k_weiter, &k_anfang})
            w->show();
        ueber.add_overlay(karte);
        klasse(chip, "hirch-3d-chip");
        chip.set_halign(Gtk::ALIGN_CENTER);
        chip.set_valign(Gtk::ALIGN_START);
        chip.set_margin_top(14);
        chip.set_margin_start(280);                 // neben den Schaltern links und «2D | 3D» rechts
        chip.set_margin_end(100);
        chip.set_line_wrap(true);
        chip.set_no_show_all(true);
        ueber.add_overlay(chip);
        umspann_css();
    }

    static void umspann_css()
    {
        static bool fertig = false;
        if (fertig) return;
        fertig = true;
        auto css = Gtk::CssProvider::create();
        css->load_from_data(R"(
.hirch-3d-karte { background: alpha(#24262e, 0.97); border: 1px solid #ff7800; border-radius: 12px; padding: 22px 24px; }
.hirch-3d-karte label { color: #ffffff; }
.hirch-3d-karte label.hirch-3d-karte-dim { color: #deddda; }
.hirch-3d-karte label.hirch-3d-karte-zeichen { color: #ffa348; font-size: 1.8em; }
.hirch-3d-karte label.hirch-3d-karte-titel { font-size: 1.45em; font-weight: bold; }
.hirch-3d-karte button { color: #ffffff; background: transparent; box-shadow: none; border: 1px solid #5e5c64; border-radius: 6px; padding: 8px 18px; }
.hirch-3d-karte button.suggested-action { background: #3584e4; border-color: #3584e4; }
.hirch-3d-chip { color: #deddda; background: alpha(#24262e, 0.94); border: 1px solid #ff7800; border-radius: 17px; padding: 7px 14px; }
)");
        Gtk::StyleContext::add_provider_for_screen(Gdk::Screen::get_default(), css,
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    /// Karte zum Halt h zeigen (-1: verstecken)
    void karte_zeigen(int h)
    {
        karte_halt = h >= 0 && h < int(halte.size()) ? h : -1;
        karte.set_visible(karte_halt >= 0);
        if (karte_halt < 0) return;
        auto const &H = halte[karte_halt];
        k_bei.set_text(TRF("bei {}", {uhr(H.t)}));
        std::string s1, s2, s3;
        if (H.wenden) {
            k_zeichen.set_text("⟲");
            k_titel.set_text(TR("Wenden"));
            k_text.set_text(TR("Seite 1 ist fertig."));
            s1 = TR("Werkstück wenden und auf die Passstifte legen.");
            s2 = TR("Neu spannen – nicht dort, wo Seite 2 fräst.");
            s3 = TR("Werkzeug einsetzen, Z neu antasten, Seite 2 starten.");
            k_weiter.set_label("▶ " + std::string(TR("Seite 2 abspielen")));
        } else {
            k_zeichen.set_text("⟳");
            k_titel.set_text(TR("Umspannen"));
            k_text.set_text(TR("Spannung 1 ist fertig – hier endet die erste Datei. Die orange Zone fräst erst "
                               "Spannung 2."));
            s1 = TR("Neue Spannmittel auf schon Gefrästes setzen – nicht in die orange Zone.");
            s2 = TR("Erst dann die alten lösen. Das Teil nicht verschieben.");
            s3 = TR("Werkzeug einsetzen, Z antasten, Spannung 2 starten.");
            k_weiter.set_label("▶ " + std::string(TR("Spannung 2 abspielen")));
        }
        k_schritte.set_text("1.  " + s1 + "\n2.  " + s2 + "\n3.  " + s3);
        leiste_auffrischen();
    }

    /// Spannung 2: Höhe der Eilgänge ausserhalb der Zone (wie engine._umspannen_hoehe2); <0: Spannmittel eingezeichnet
    double hoehe2(int seite) const
    {
        if (!teil) return 0;
        auto const &d = teil->d;
        double hoch = 0;
        for (auto const &sm : d.spannmittel) {
            if ((d.zweiseitig ? sm.seite : 1) != seite) continue;
            if (sm.spannung == 2 && sm.punkte.size() >= 3) return -1;
            hoch = std::max(hoch, sm.hoehe);
        }
        // wie engine (fraesbahnen 04.11., Sicherheitsprüfung): geschätzt nie tiefer als die Sicherheitshöhe
        return d.umspann_hoehe2 > 0 ? d.umspann_hoehe2 : std::max(hoch + d.umspann_abstand, d.sicherheit);
    }

    void leiste_bauen()
    {
        klasse(leiste, "hirch-3d-leiste");
        leiste.set_valign(Gtk::ALIGN_END);
        leiste.set_halign(Gtk::ALIGN_FILL);
        leiste.set_no_show_all(true);
        auto kopf = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 10);
        b_play.set_label("▶");
        b_play.set_size_request(44, -1);
        b_play.set_tooltip_text(TR("Abspielen"));
        b_play.signal_clicked().connect([this] { spielen(!spielt); });
        b_anfang.set_label("⏮");
        b_anfang.set_tooltip_text(TR("An den Anfang"));
        b_anfang.signal_clicked().connect([this] {
            spielen(false);
            stelle_setzen(0);
        });
        klasse(l_uhr, "hirch-3d-uhr");
        l_uhr.set_width_chars(16);
        l_uhr.set_xalign(0);
        klasse(l_jetzt, "hirch-3d-dim");
        l_jetzt.set_xalign(0);
        l_jetzt.set_hexpand(true);
        l_jetzt.set_ellipsize(Pango::ELLIPSIZE_END);
        for (auto w : std::initializer_list<Gtk::Widget *>{&b_play, &b_anfang, &l_uhr, &l_jetzt}) kopf->add(*w);
        auto tl = label(TR("Tempo"), "hirch-3d-dim");
        kopf->add(*tl);
        auto gruppe = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
        klasse(*gruppe, "linked");
        char const *namen[5] = {"¼ ×", TR("Echtzeit"), "10 ×", "50 ×", TR("Übersicht 15 s")};
        for (int i = 0; i < 5; ++i) {
            b_tempo[i].set_label(namen[i]);
            b_tempo[i].set_active(i == tempo_i);
            b_tempo[i].signal_toggled().connect([this, i] {
                static bool dabei = false;
                if (dabei) return;
                dabei = true;
                if (spielt) {                    // ab hier im neuen Tempo
                    spiel_t0 = t_jetzt;
                    spiel_uhr = g_get_monotonic_time();
                }
                tempo_i = i;
                for (int q = 0; q < 5; ++q) b_tempo[q].set_active(q == i);
                dabei = false;
            });
            gruppe->add(b_tempo[i]);
        }
        b_tempo[4].set_tooltip_text(TR("Übersicht: alles in etwa 15 Sekunden abspielen, egal wie lange es dauert"));
        kopf->add(*gruppe);
        b_zu.set_label("▾");
        b_zu.set_tooltip_text(TR("Abspielleiste schliessen"));
        b_zu.signal_clicked().connect([this] { leiste_offen(false); });
        kopf->add(b_zu);
        leiste.add(*kopf);
        // B-29 (Peter 05.11., V1): oben Platz für die Zeit an der Stelle, oben und unten für die Dreiecke
        zeitleiste.set_size_request(-1, ZL_OBEN + 34 + ZL_UNTEN);
        zeitleiste.set_hexpand(true);
        zeitleiste.add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::BUTTON1_MOTION_MASK);
        zeitleiste.signal_draw().connect([this](Cairo::RefPtr<Cairo::Context> const &cr) { return zeitleiste_malen(cr); });
        zeitleiste.signal_button_press_event().connect([this](GdkEventButton *e) {
            if (e->button == 3 && !ablauf.empty() && e->type == GDK_BUTTON_PRESS) {
                zeitleiste_menue(e);
                return true;
            }
            if (e->button != 1 || ablauf.empty()) return false;
            zeitleiste_klick(e->x, true);
            return true;
        });
        zeitleiste.signal_motion_notify_event().connect([this](GdkEventMotion *e) {
            zeitleiste_klick(e->x, false);
            return true;
        });
        leiste.add(zeitleiste);
        auto unten = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 14);
        klasse(l_marken, "hirch-3d-dim");
        l_marken.set_xalign(0);
        l_marken.set_hexpand(true);
        l_marken.set_line_wrap(true);
        klasse(l_eil, "hirch-3d-dim");
        unten->add(l_marken);
        unten->add(l_eil);
        leiste.add(*unten);
        for (auto w : std::initializer_list<Gtk::Widget *>{kopf, &b_play, &b_anfang, &l_uhr, &l_jetzt, tl, gruppe, &b_zu,
                                                           &zeitleiste, unten, &l_marken, &l_eil})
            w->show();
        for (auto &b : b_tempo) b.show();
        ueber.add_overlay(leiste);
        klasse(b_auf, "hirch-3d-auf");
        b_auf.set_halign(Gtk::ALIGN_END);
        b_auf.set_valign(Gtk::ALIGN_END);
        b_auf.set_margin_end(12);
        b_auf.set_margin_bottom(10);
        b_auf.set_no_show_all(true);
        b_auf.signal_clicked().connect([this] { leiste_offen(true); });
        ueber.add_overlay(b_auf);
    }

    bool leiste_zu = false;
    void leiste_offen(bool an)
    {
        leiste_zu = !an;
        if (!an) spielen(false);
        leiste_auffrischen();
    }

    void leiste_auffrischen()
    {
        bool da = !ablauf.empty() && teil && teil->d.meldung.empty();
        leiste.set_visible(da && !leiste_zu);
        b_auf.set_visible(da && leiste_zu);
        if (!da) return;
        b_auf.set_label("▸ " + std::string(TR("Abspielen")) + " · " + uhr(t_ges()));
        b_play.set_label(spielt ? "❚❚" : "▶");
        b_play.set_tooltip_text(spielt ? TR("Pause") : TR("Abspielen"));
        auto ctx = b_play.get_style_context();
        ctx->add_class("suggested-action");
        l_uhr.set_text(uhr(t_jetzt) + " / " + uhr(t_ges()));
        auto orange = [](std::string const &t) {
            return "<span foreground='#FFA348'>" + Glib::Markup::escape_text(t).raw() + "</span>";
        };
        if (karte_halt >= 0 && karte_halt < int(halte.size())) {
            bool w = halte[karte_halt].wenden;
            l_jetzt.set_markup(orange(std::string(w ? "⟲ " : "⟳ ") + (w ? TR("Wenden") : TR("Umspannen")) + " – " +
                                      TR("angehalten")));
        } else if (am_ende()) {
            l_jetzt.set_text(TR("fertig"));
        } else if (auto A = abschnitt_bei(t_jetzt)) {
            std::string t = A->seite2 ? std::string(TR("Seite 2")) + " · " : std::string();
            t += A->name;
            if (!A->wz.empty() && A->wz != A->name) t += " · " + A->wz;
            std::string vor = A->spannung == 2 ? orange(TR("Spannung 2")) + " · " : std::string();
            l_jetzt.set_markup(vor + Glib::Markup::escape_text(t).raw());
        }
        std::string m;
        int n = 0;
        for (std::size_t i = 1; i < ablauf.size(); ++i) {
            auto const &v = ablauf[i - 1], &A = ablauf[i];
            bool wechsel = A.wz != v.wz, wenden = A.seite2 && !v.seite2;
            bool umsp = !wenden && A.seite2 == v.seite2 && A.spannung == 2 && v.spannung != 2;
            if (!wechsel && !wenden && !umsp) continue;
            if (++n > 4) continue;
            if (!m.empty()) m += "    ";
            if (umsp) m += orange("⟳ " + TRF("Umspannen bei {}", {uhr(A.t0)})) + (wechsel ? "    " : "");
            if (wenden) m += Glib::Markup::escape_text(std::string("⟲ ") + TR("Wenden") + (wechsel ? " · " : "")).raw();
            if (wechsel) m += Glib::Markup::escape_text(TRF("Werkzeugwechsel bei {} → {}", {uhr(A.t0), A.wz})).raw();
            else if (wenden) m += " " + uhr(A.t0);
        }
        if (n > 4) m += "    " + Glib::Markup::escape_text(TRF("+{} weitere", {std::to_string(n - 4)})).raw();
        l_marken.set_markup(m);
        {                                                // Spannung 2 läuft: Hinweis oben
            auto A = am_ende() ? nullptr : abschnitt_bei(t_jetzt);
            bool zeigen = A && A->spannung == 2 && karte_halt < 0;
            if (zeigen) {
                double h2 = hoehe2(A->seite2 ? 2 : 1);
                char b[32];
                std::snprintf(b, sizeof b, "%g", std::round(h2 * 10) / 10);
                std::string t = h2 < 0 ? std::string(TR("Spannmittel wie eingezeichnet"))
                                       : TRF("neue Spannmittel nicht eingezeichnet – Eilgänge ausserhalb der Zone auf {} mm", {b});
                chip.set_markup(orange(TR("Spannung 2")) + "  ·  " + Glib::Markup::escape_text(t).raw());
            }
            chip.set_visible(zeigen);
        }
        std::string e = eil_laeuft ? std::string(TR("Eilgänge werden geprüft …"))
                        : eil_rein.empty() ? std::string(TR("Eilgang ins Material: keiner"))
                                          : TRF("Eilgang ins Material: {}", {std::to_string(eil_rein.size())});
        l_eil.set_markup("<span foreground='#E01B24'>■</span> " + Glib::Markup::escape_text(e));
        zeitleiste.queue_draw();
    }

    double zeit_von_x(double x) const
    {
        int w = zeitleiste.get_allocated_width();
        return w > 0 ? std::clamp(x / w, 0.0, 1.0) * t_ges() : 0.0;
    }

    void zeitleiste_klick(double x, bool druck)
    {
        if (ablauf.empty()) return;
        double t = zeit_von_x(x);
        if (druck) spielen(false);
        if (auto A = abschnitt_bei(t)) {
            int w = A->bea >= 0 ? A->bea + 1 : 0;
            if (w != wahl) {
                wahl = w;
                if (a) a->bereich.queue_render();
            }
        }
        stelle_setzen(t);
    }

    bool zeitleiste_malen(Cairo::RefPtr<Cairo::Context> const &cr)
    {
        double W = zeitleiste.get_allocated_width(), T = t_ges();
        double H = zeitleiste.get_allocated_height() - ZL_OBEN - ZL_UNTEN;     // Höhe der Leiste
        cr->save();
        cr->translate(0, ZL_OBEN);
        auto rund = [&](double x, double y, double w, double h, double r) {
            cr->begin_new_sub_path();
            cr->arc(x + w - r, y + r, r, -M_PI / 2, 0);
            cr->arc(x + w - r, y + h - r, r, 0, M_PI / 2);
            cr->arc(x + r, y + h - r, r, M_PI / 2, M_PI);
            cr->arc(x + r, y + r, r, M_PI, 3 * M_PI / 2);
            cr->close_path();
        };
        rund(0, 0, W, H, 5);
        cr->set_source_rgb(0.227, 0.247, 0.294);
        cr->fill_preserve();
        cr->save();
        cr->clip();
        if (T > 0) {
            auto lay = zeitleiste.create_pango_layout("");
            for (std::size_t i = 0; i < ablauf.size(); ++i) {
                auto const &A = ablauf[i];
                double x0 = std::round(A.t0 / T * W), x1 = std::round(A.t1 / T * W);
                if (x1 - x0 < 1) x1 = x0 + 1;
                bool an = wahl > 0 && A.bea + 1 == wahl, vorbei = t_jetzt >= A.t1 - 1e-9;
                double alpha = an ? 1.0 : vorbei ? 0.75 : 0.5;
                if (wahl > 0 && !an) alpha *= 0.7;
                cr->rectangle(x0, 0, x1 - x0, H);
                cr->set_source_rgba(A.farbe[0], A.farbe[1], A.farbe[2], alpha);
                cr->fill();
                if (an) {
                    cr->rectangle(x0 + 1, 1, x1 - x0 - 2, H - 2);
                    cr->set_source_rgb(1, 1, 1);
                    cr->set_line_width(2);
                    cr->stroke();
                }
                if (A.spannung == 2) {                  // Spannung 2: schräg gestreift
                    cr->save();
                    cr->rectangle(x0, 0, x1 - x0, H);
                    cr->clip();
                    cr->set_source_rgba(1, 1, 1, 0.12);
                    cr->set_line_width(4);
                    for (double sx = x0 - H; sx < x1 + H; sx += 9) {
                        cr->move_to(sx, H);
                        cr->line_to(sx + H, 0);
                    }
                    cr->stroke();
                    cr->restore();
                }
                if (i + 1 < ablauf.size()) {            // Trenner; Werkzeugwechsel und Wenden: hell, Umspannen orange
                    auto const &B = ablauf[i + 1];
                    bool mark = B.wz != A.wz || (B.seite2 && !A.seite2);
                    bool umsp = B.seite2 == A.seite2 && B.spannung == 2 && A.spannung != 2;
                    cr->rectangle(umsp ? x1 - 1.5 : x1 - 1, 0, umsp ? 3 : 2, H);
                    if (umsp) cr->set_source_rgb(1.0, 0.47, 0.0);
                    else if (mark) cr->set_source_rgb(0.96, 0.96, 0.95);
                    else cr->set_source_rgb(0.118, 0.125, 0.157);
                    cr->fill();
                }
                lay->set_text(A.name);
                int tw, th;
                lay->get_pixel_size(tw, th);
                if (x1 - x0 > tw + 16) {
                    cr->move_to(x0 + 8, (H - th) / 2);
                    cr->set_source_rgb(1, 1, 1);
                    lay->show_in_cairo_context(cr);
                }
            }
            cr->set_source_rgb(0.878, 0.106, 0.141);     // Eilgänge ins Material: rote Striche unten
            for (double t : eil_rein) cr->rectangle(std::round(t / T * W) - 1, H - 7, 2, 7);
            cr->fill();
        }
        cr->restore();
        cr->restore();
        if (T > 0) {                                     // Stelle (B-29, V1): Strich mit Dreiecken, darüber die Zeit
            double x = std::round(t_jetzt / T * W);
            x = std::clamp(x, 8.0, W - 9);
            double y0 = ZL_OBEN, y1 = ZL_OBEN + H;
            auto dunkel = [&] { cr->set_source_rgb(0.118, 0.125, 0.157); };
            cr->rectangle(x - 3.5, y0 - 4, 7, y1 - y0 + 8);
            dunkel();
            cr->fill();
            cr->rectangle(x - 2, y0 - 4, 4, y1 - y0 + 8);
            cr->set_source_rgb(1, 1, 1);
            cr->fill();
            for (int k = 0; k < 2; ++k) {                // Dreieck oben (Spitze nach unten) und unten (nach oben)
                double yb = k == 0 ? y0 - 7 : y1 + 7, ys = k == 0 ? y0 + 5 : y1 - 5;
                cr->move_to(x - 8, yb);
                cr->line_to(x + 8, yb);
                cr->line_to(x, ys);
                cr->close_path();
                cr->set_source_rgb(1, 1, 1);
                cr->fill_preserve();
                dunkel();
                cr->set_line_width(1.2);
                cr->stroke();
            }
            auto lay = zeitleiste.create_pango_layout("");
            lay->set_markup("<b><small>" + Glib::Markup::escape_text(uhr(t_jetzt) + " / " + uhr(T)).raw() +
                            "</small></b>");
            int tw, th;
            lay->get_pixel_size(tw, th);
            double bw = tw + 12, bh = std::min<double>(th + 2, ZL_OBEN - 7);
            double bx = std::clamp(x - bw / 2, 0.0, std::max(0.0, W - bw));
            double r = bh / 2;
            cr->begin_new_sub_path();
            cr->arc(bx + bw - r, r, r, -M_PI / 2, M_PI / 2);
            cr->arc(bx + r, r, r, M_PI / 2, 3 * M_PI / 2);
            cr->close_path();
            cr->set_source_rgb(1, 1, 1);
            cr->fill();
            dunkel();
            cr->move_to(bx + 6, (bh - th) / 2);
            lay->show_in_cairo_context(cr);
        }
        return true;
    }

    /// B-30: Rechtsklick auf die Zeitleiste – Stelle setzen oder ab dem Anfang des Zugs an dieser Stelle neu ausgeben
    /// (nie mitten in einem Schnitt; die sichere Anfahrt macht «Ab hier neu ausgeben» der Basis)
    std::unique_ptr<Gtk::Menu> zl_menue;
    void zeitleiste_menue(GdkEventButton *e)
    {
        double t = zeit_von_x(e->x);
        // Bewegung an dieser Stelle, dann zurück bis zum Anfang ihres Zugs (erste Vorschubbewegung nach einem
        // Eilgang); steht die Stelle auf einem Eilgang: der nächste Zug
        int k = int(std::lower_bound(zeit.begin(), zeit.end(), t) - zeit.begin());
        int n = int(bew.size());
        if (k >= n) k = n - 1;
        while (k < n && k >= 0 && bew[k].eil) ++k;
        int k0 = -1;
        if (k >= 0 && k < n) {
            k0 = k;
            while (k0 > 0 && !bew[k0 - 1].eil && bew[k0 - 1].p == bew[k0].p) --k0;
        }
        zl_menue = std::make_unique<Gtk::Menu>();
        auto setzen = Gtk::make_managed<Gtk::MenuItem>(TR("Stelle hierher setzen"));
        setzen->signal_activate().connect([this, t] {
            spielen(false);
            stelle_setzen(t);
        });
        zl_menue->append(*setzen);
        zl_menue->append(*Gtk::make_managed<Gtk::SeparatorMenuItem>());
        auto aus = Gtk::make_managed<Gtk::MenuItem>();
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 1);
        auto l1 = Gtk::make_managed<Gtk::Label>(TR("G-Code ab hier ausgeben …"));
        l1->set_xalign(0);
        box->add(*l1);
        auto p = panel_fraesen();
        // Entwurf: die Zeilen der Ansicht passen nicht zur genauen Ausgabe – dann ab dem Anfang der Bearbeitung
        bool entwurf = p && p->ergebnisEntwurf();
        std::string wo, info, ab;
        if (k0 >= 0) {
            double t0 = k0 > 0 ? zeit[k0 - 1] : 0.0;
            auto A = abschnitt_bei(t0 + 1e-6);
            std::string name = A ? A->name : std::string();
            if (entwurf) {
                int q = A ? A->bea : -1;
                if (q >= 0 && q < int(bea_ids.size())) {
                    bj::object o;
                    o["key"] = bea_ids[q];
                    ab = bj::serialize(o);
                    wo = name;
                    info = TRF("ab Anfang von «{}» – genau ab dem Zug erst nach «G-Code speichern»", {name});
                }
            } else if (bew[k0].zeile > 0) {
                bj::object o;
                o["programm"] = bew[k0].p + 1;
                o["zeile"] = bew[k0].zeile;
                ab = bj::serialize(o);
                wo = TRF("Anfang des Zugs bei {}", {uhr(t0)}) + (name.empty() ? "" : " · " + name);
                info = TRF("ab Anfang des Zugs bei {}", {uhr(t0)});
                if (A) {
                    info += " · " + name;
                    if (!A->wz.empty() && A->wz != A->name) info += " · " + A->wz;
                }
            }
        }
        if (!info.empty()) {
            auto l2 = Gtk::make_managed<Gtk::Label>();
            l2->set_markup("<small>" + Glib::Markup::escape_text(info).raw() + "</small>");
            l2->set_xalign(0);
            l2->get_style_context()->add_class("dim-label");
            box->add(*l2);
        }
        aus->add(*box);
        aus->set_sensitive(!ab.empty() && p);
        aus->signal_activate().connect([this, ab, wo] {
            if (auto p2 = panel_fraesen()) p2->abHierAusgeben(ab, wo);
        });
        zl_menue->append(*aus);
        zl_menue->show_all();
        // ans Fenster hängen, nicht an die Leiste: sonst erbt das Menü deren helle Schrift (auf hellem Grund)
        if (auto top = get_toplevel()) zl_menue->attach_to_widget(*top);
        zl_menue->popup_at_pointer(reinterpret_cast<GdkEvent *>(e));
    }

    /// Fräsbahnen: alle Vorschub-Bewegungen in der Farbe ihrer Bearbeitung (Seite 2 gewendet)
    static void bahnen_bauen(VorschauDaten const &d, std::vector<AbtragBewegung> const &m,
                             std::vector<AbtragBearbeitung> const &bea, std::vector<float> &aus,
                             std::vector<int> *bis = nullptr, std::vector<float> *eil = nullptr,
                             std::vector<int> *eil_bis = nullptr, std::set<int> const *aus_nr = nullptr)
    {
        auto weg = [&](AbtragBewegung const &b) { return aus_nr && aus_nr->count(b.bearbeitung); };
        aus.clear();
        if (bis) bis->clear();
        if (eil) eil->clear();
        if (eil_bis) eil_bis->clear();
        std::size_t schritt = std::max<std::size_t>(1, m.size() / 1'500'000);
        auto lage = [&](AbtragBewegung const &b, float out[3]) {
            int seite = b.p >= 0 && b.p < int(d.programme.size()) ? d.programme[b.p].seite : 1;
            double x = b.x, y = b.y, z = b.z;
            if (seite == 2 && d.zweiseitig) {
                if (d.um_y) x = d.breite - x;
                else y = d.hoehe - y;
                z = -d.dicke - z;
            }
            out[0] = float(x);
            out[1] = float(y);
            out[2] = float(z);
        };
        for (std::size_t k = schritt; k < m.size(); k += schritt) {
            auto const &b = m[k], &v = m[k - schritt];
            if (b.eil || b.p != v.p || weg(b)) continue;
            auto f = b.bearbeitung >= 0 && b.bearbeitung < int(bea.size())
                ? hex_farbe(bea[b.bearbeitung].farbe, {0.6f, 0.6f, 0.6f}) : std::array<float, 3>{0.6f, 0.6f, 0.6f};
            float p0[3], p1[3];
            lage(v, p0);
            lage(b, p1);
            aus.insert(aus.end(), {p0[0], p0[1], p0[2], 0, 0, 1, f[0], f[1], f[2]});
            aus.insert(aus.end(), {p1[0], p1[1], p1[2], 0, 0, 1, f[0], f[1], f[2]});
            if (bis) bis->push_back(int(k));
        }
        // Eilgänge: jeder einzeln (nicht ausgedünnt), rot wie in der Vorschau
        if (eil)
            for (std::size_t k = 1; k < m.size(); ++k) {
                auto const &b = m[k], &v = m[k - 1];
                if (!b.eil || b.p != v.p || weg(b)) continue;
                float p0[3], p1[3];
                lage(v, p0);
                lage(b, p1);
                eil->insert(eil->end(), {p0[0], p0[1], p0[2], 0, 0, 1, 0.90f, 0.12f, 0.14f});
                eil->insert(eil->end(), {p1[0], p1[1], p1[2], 0, 0, 1, 0.90f, 0.12f, 0.14f});
                if (eil_bis) eil_bis->push_back(int(k));
            }
    }

    /// Höhe des Teils bei (x, y) auf der Seite (1: Oberseite, 2: Unterseite); ausserhalb: Material
    // Höhe des 3D-Modells je Zelle von «raster» (Peter 06.11.: die Linien liegen auf dem Modell, nicht auf dem
    // Fräsergebnis – genau, ohne Sprünge an steilen Kanten; für 3D-Fasen ohnehin nötig). Neu, wenn Raster oder Modell
    // sich ändern.
    std::vector<float> m_o, m_u;
    std::tuple<int, int, float const *, std::size_t, std::size_t> m_kennung{};
    void modell_auffrischen()
    {
        if (!a || !a->sz) {
            m_o.clear();
            m_u.clear();
            return;
        }
        auto const &sz = *a->sz;
        auto k = std::make_tuple(raster.nx, raster.ny, sz.ecken.empty() ? nullptr : sz.ecken.data(), sz.ecken.size(),
                                 sz.index.size());
        if (k == m_kennung && m_o.size() == std::size_t(raster.nx) * raster.ny) return;
        m_kennung = k;
        modell_hoehen(sz, raster, m_o, m_u);
    }
    /// Höhe des Modells an (x, y): bilinear aus den vier Zellen, sonst die nächste; NaN, wo kein Modell ist
    double modell_bei(double x, double y, int seite) const
    {
        auto const &m = seite == 2 ? m_u : m_o;
        if (m.empty() || raster.nx < 2) return NAN;
        double fx = (x - raster.x0) / raster.dx, fy = (y - raster.y0) / raster.dy;
        int i = int(std::floor(fx)), j = int(std::floor(fy));
        double tx = fx - i, ty = fy - j;
        if (raster.gilt(i, j) && raster.gilt(i + 1, j + 1)) {
            float a00 = m[raster.at(i, j)], a10 = m[raster.at(i + 1, j)], a01 = m[raster.at(i, j + 1)],
                  a11 = m[raster.at(i + 1, j + 1)];
            if (!std::isnan(a00) && !std::isnan(a10) && !std::isnan(a01) && !std::isnan(a11))
                return (a00 * (1 - tx) + a10 * tx) * (1 - ty) + (a01 * (1 - tx) + a11 * tx) * ty;
        }
        long q = raster.punkt(x, y);
        return q >= 0 ? m[q] : NAN;
    }

    /// leer_nan: ohne Material in der Nähe NaN statt der unberührten Oberfläche (Linien überbrücken solche Lücken).
    /// Über dem 3D-Modell: seine Oberfläche (Seite 2 die Unterseite), daneben das Fräsergebnis.
    double hoehe(double x, double y, int seite, bool leer_nan = false) const
    {
        double hm = modell_bei(x, y, seite);
        if (!std::isnan(hm)) return hm;
        long q = raster.punkt(x, y);
        double leer = leer_nan ? NAN : seite == 2 ? -teil->d.dicke : 0.0;
        if (q < 0) return leer;
        // die höhere Kante der Nachbarn (3 × 3): eine Linie am Rand einer Tasche bleibt oben, statt hin und her
        // zu springen; in einer Rille, die breiter ist als zwei Zellen, bleibt sie unten. Liegt die Stelle selbst in
        // einem Durchbruch (Schnittfuge der Aussenkontur), zählen auch nur die Nachbarn – bisher sprang die Linie
        // dort auf die unberührte Oberfläche (Peter 06.11.: Aussenkontur des Flügels sprang auf extreme Z-Werte)
        int I = int(q % raster.nx), J = int(q / raster.nx);
        bool da = raster.fest[q];
        double h = da ? (seite == 2 ? raster.u[q] : raster.o[q]) : leer;
        // Linien liegen auf dem Material (Peter 06.11.: «eine selbst eingezeichnete Kante, die ich als Grenze angelegt
        // habe»): im Umkreis von 1 mm das höchste Material (Seite 2 das tiefste) – eine Linie auf der Wand einer Tasche
        // oder an der Grenze des 3D-Schruppens bleibt oben auf der Kante, statt je nach Rasterzelle auf den Boden zu
        // fallen. Im Durchbruch (Fuge einer Kontur) zuerst bis 4 mm nach Material suchen, dann ebenso 1 mm weiter.
        double c = std::max(1e-3, std::min(raster.dx, raster.dy));
        int kante = std::clamp(int(std::ceil(1.0 / c)), 1, 30);
        int such = std::clamp(int(std::ceil(4.0 / c)), 3, 80);
        int bis_r = da ? kante : such;
        for (int r = 1; r <= bis_r; ++r) {
            bool neu = false;
            for (int dj = -r; dj <= r; ++dj)
                for (int di = -r; di <= r; ++di) {
                    if (std::abs(di) != r && std::abs(dj) != r) continue;   // nur der Ring r (innen schon geprüft)
                    if (!raster.gilt(I + di, J + dj)) continue;
                    auto p = raster.at(I + di, J + dj);
                    if (!raster.fest[p]) continue;
                    double v = seite == 2 ? raster.u[p] : raster.o[p];
                    h = !da ? v : seite == 2 ? std::min<double>(h, v) : std::max<double>(h, v);
                    if (!da) neu = true;
                    da = true;
                }
            if (neu) bis_r = std::min(such + kante, r + kante);   // Material gefunden: noch 1 mm weiter
        }
        return h;
    }

    void doc_beobachten()
    {
        auto doc = desk ? desk->getDocument() : nullptr;
        if (doc == beobachtet) return;
        doc_mod.disconnect();
        beobachtet = doc;
        if (doc)
            doc_mod = doc->connectModified([this](unsigned) {
                linien_spaeter.disconnect();
                linien_spaeter = Glib::signal_timeout().connect([this] {
                    linien_bauen();
                    return false;
                }, 400);
            });
    }

    /// Linien der Zeichnung (Pfade, Texte) auf das Teil legen: Seite 1 auf die Oberseite, Seite 2 gewendet
    /// auf die Unterseite; Farbe wie in der Zeichnung, gewählte hell
    void linien_bauen()
    {
        doc_beobachten();
        linien.clear();
        auto doc = desk ? desk->getDocument() : nullptr;
        if (!doc || !teil || raster.nx < 2 || !a) return;
        auto const &d = teil->d;
        Material m = material(doc);
        Geom::Rect p1 = m.seite, p2 = m.seite2;
        std::function<void(SPObject *)> geh = [&](SPObject *o) {
            for (auto &c : o->children) {
                auto it = cast<SPItem>(&c);
                if (!it || it->isHidden()) continue;
                auto r = it->getRepr();
                if (r->attribute(RELIEF) || r->attribute(RELIEF2) || r->attribute("hirch:steg3d") ||
                    r->attribute("hirch:vorrichtung"))
                    continue;
                if (is<SPGroup>(it)) {
                    geh(it);
                    continue;
                }
                Geom::PathVector pv;
                if (auto sh = cast<SPShape>(it); sh && sh->curve()) pv = sh->curve()->get_pathvector();
                else if (auto tx = cast<SPText>(it)) pv = tx->getNormalizedBpath().get_pathvector();
                if (pv.empty()) continue;
                pv *= it->i2doc_affine();
                auto bb = pv.boundsFast();
                if (!bb) continue;
                int seite = p1.contains(bb->midpoint()) ? 1 : (d.zweiseitig && m.hat2 && p2.contains(bb->midpoint())) ? 2 : 0;
                if (!seite) continue;
                Geom::Rect pg = seite == 2 ? p2 : p1;
                TeilLinie L;
                L.id = r->attribute("id") ? r->attribute("id") : "";
                L.seite = seite;
                auto st = it->style;
                if (st && st->stroke.isColor()) {
                    auto const &c = st->stroke.value.color;
                    L.farbe = {c.v.c[0], c.v.c[1], c.v.c[2]};
                } else if (st && st->fill.isColor()) {
                    auto const &c = st->fill.value.color;
                    L.farbe = {c.v.c[0], c.v.c[1], c.v.c[2]};
                }
                if (L.farbe[0] + L.farbe[1] + L.farbe[2] < 0.3f) L.farbe = {0.85f, 0.85f, 0.85f};   // schwarz: hell zeigen
                auto mm = [&](Geom::Point q) -> std::array<double, 2> {
                    double x = (q.x() - pg.left()) / MM_PX, y = (pg.bottom() - q.y()) / MM_PX;
                    if (seite == 2) {
                        if (d.um_y) x = d.breite - x;
                        else y = d.hoehe - y;
                    }
                    return {x, y};
                };
                for (auto const &path : pv) {
                    if (!L.pkt.empty()) L.pkt.push_back({NAN, NAN});
                    L.pkt.push_back(mm(path.initialPoint()));
                    for (auto const &cv : path) {
                        int n = 1;
                        if (!cv.isLineSegment()) n = std::clamp(int(std::ceil(cv.length(0.1) / (0.5 * MM_PX))), 2, 200);
                        for (int k = 1; k <= n; ++k) L.pkt.push_back(mm(cv.pointAt(double(k) / n)));
                    }
                    if (path.closed()) L.pkt.push_back(mm(path.initialPoint()));
                }
                linien.push_back(std::move(L));
            }
        };
        geh(doc->getRoot());
        linien_faerben();
    }

    /// Douglas-Peucker: Punkte weglassen, solange der Zug höchstens tol abweicht (seitlich und in der Höhe getrennt)
    static std::vector<std::array<double, 3>> vereinfachen(std::vector<std::array<double, 3>> const &p, double tol)
    {
        std::size_t n = p.size();
        if (n < 3) return p;
        std::vector<char> behalten(n, 0);
        behalten[0] = behalten[n - 1] = 1;
        std::vector<std::pair<std::size_t, std::size_t>> stapel{{0, n - 1}};
        while (!stapel.empty()) {
            auto [a0, e0] = stapel.back();
            stapel.pop_back();
            if (e0 <= a0 + 1) continue;
            double dx = p[e0][0] - p[a0][0], dy = p[e0][1] - p[a0][1], dzz = p[e0][2] - p[a0][2];
            double L = dx * dx + dy * dy;
            double best = -1;
            std::size_t k = a0;
            for (std::size_t i = a0 + 1; i < e0; ++i) {
                double qx = p[i][0] - p[a0][0], qy = p[i][1] - p[a0][1];
                double t = L > 0 ? std::clamp((qx * dx + qy * dy) / L, 0.0, 1.0) : 0.0;
                double seit = std::hypot(qx - t * dx, qy - t * dy);
                double hoch = std::abs(p[i][2] - p[a0][2] - t * dzz);
                double ab = std::max(seit, hoch);
                if (ab > best) {
                    best = ab;
                    k = i;
                }
            }
            if (best > tol) {
                behalten[k] = 1;
                stapel.push_back({a0, k});
                stapel.push_back({k, e0});
            }
        }
        std::vector<std::array<double, 3>> aus;
        for (std::size_t i = 0; i < n; ++i)
            if (behalten[i]) aus.push_back(p[i]);
        return aus;
    }

    /// Ecken der Linien neu (gewählte Objekte hell)
    void linien_faerben()
    {
        if (!a || !teil) return;
        std::set<std::string> gewaehlt;          // gewählte Objekte, bei Gruppen alles darin
        if (auto sel = desk ? desk->getSelection() : nullptr) {
            std::function<void(SPObject *)> alle = [&](SPObject *o) {
                if (auto id = o->getRepr()->attribute("id")) gewaehlt.insert(id);
                for (auto &c : o->children) alle(&c);
            };
            for (auto it : sel->items()) alle(it);
        }
        auto &e = a->linien_teil;
        e.clear();
        // Linien folgen der Höhe des Teils: lange Stücke in Schritte von etwa zwei Rasterzellen teilen (über eine
        // Rille hinweg taucht die Linie hinein statt darüber zu schweben); höchstens ~2 Mio. Stücke
        double laenge = 0;
        for (auto const &L : linien)
            for (std::size_t k = 1; k < L.pkt.size(); ++k)
                if (!std::isnan(L.pkt[k - 1][0]) && !std::isnan(L.pkt[k][0]))
                    laenge += std::hypot(L.pkt[k][0] - L.pkt[k - 1][0], L.pkt[k][1] - L.pkt[k - 1][1]);
        modell_auffrischen();
        double schritt = std::max(2 * std::min(raster.dx, raster.dy), laenge / 3.0e5);
        for (auto const &L : linien) {
            bool an = gewaehlt.count(L.id) > 0;
            auto f = an ? std::array<float, 3>{0.60f, 0.85f, 1.0f} : L.farbe;
            float dz = L.seite == 2 ? -0.05f : 0.05f;
            // je Zug: abtasten, dann vereinfachen (gerade Stücke ohne Höhenänderung zu einem; Peter 06.11.: «wenige
            // Punkte») – die Abweichung bleibt unter 0,05 mm, seitlich und in der Höhe
            std::vector<std::array<double, 3>> zug;
            auto ausgeben = [&] {
                // ohne Material darunter (Schnittfuge, geräumt): gerade von der letzten zur nächsten echten Höhe;
                // ganz ohne: auf der unberührten Oberfläche
                {
                    std::size_t n = zug.size(), i = 0;
                    long letzter = -1;
                    while (i < n) {
                        if (!std::isnan(zug[i][2])) {
                            letzter = long(i++);
                            continue;
                        }
                        std::size_t j = i;
                        while (j < n && std::isnan(zug[j][2])) ++j;
                        double za = letzter >= 0 ? zug[letzter][2] : (j < n ? zug[j][2] : (L.seite == 2 ? -teil->d.dicke : 0.0) + dz);
                        double zb = j < n ? zug[j][2] : za;
                        for (std::size_t k = i; k < j; ++k) {
                            double t = double(k - i + 1) / double(j - i + 1);
                            zug[k][2] = za + (zb - za) * t;
                        }
                        i = j;
                    }
                }
                if (zug.size() >= 2) {
                    auto v = vereinfachen(zug, 0.05);
                    if (std::getenv("HIRCH_3D_LINIEN")) {      // Messung: Punkte und Höhen je Zug ins Protokoll
                        double lo = 1e9, hi = -1e9, sprung = 0;
                        for (std::size_t k = 0; k < v.size(); ++k) {
                            lo = std::min(lo, v[k][2]);
                            hi = std::max(hi, v[k][2]);
                            if (k) sprung = std::max(sprung, std::abs(v[k][2] - v[k - 1][2]));
                        }
                        std::fprintf(stderr, "Hirch-3D: Linie %s Seite %d: %zu von %zu Punkten, Z %.2f … %.2f, Sprung bis %.2f\n",
                                     L.id.c_str(), L.seite, v.size(), zug.size(), lo, hi, sprung);
                    }
                    for (std::size_t k = 1; k < v.size(); ++k) {
                        e.insert(e.end(), {float(v[k - 1][0]), float(v[k - 1][1]), float(v[k - 1][2]), 0, 0, 1, f[0], f[1], f[2]});
                        e.insert(e.end(), {float(v[k][0]), float(v[k][1]), float(v[k][2]), 0, 0, 1, f[0], f[1], f[2]});
                    }
                }
                zug.clear();
            };
            for (std::size_t k = 1; k < L.pkt.size(); ++k) {
                auto const &p = L.pkt[k - 1], &q = L.pkt[k];
                if (std::isnan(p[0]) || std::isnan(q[0])) {
                    ausgeben();
                    continue;
                }
                int n = std::clamp(int(std::ceil(std::hypot(q[0] - p[0], q[1] - p[1]) / schritt)), 1, 20000);
                if (zug.empty()) zug.push_back({p[0], p[1], hoehe(p[0], p[1], L.seite, true) + dz});
                for (int t = 1; t <= n; ++t) {
                    double xb = p[0] + (q[0] - p[0]) * t / n, yb = p[1] + (q[1] - p[1]) * t / n;
                    zug.push_back({xb, yb, hoehe(xb, yb, L.seite, true) + dz});
                }
            }
            ausgeben();
        }
        a->neu_linien = true;
        a->bereich.queue_render();
    }

    /// Test (HIRCH_TEST_LINIENKLICK): einen sichtbaren Punkt einer Linie auf den Bildschirm rechnen, dort klicken
    /// (Auswahl prüfen), dann doppelklicken (die Ansicht muss nach 2D wechseln); Ergebnis ins Protokoll
    void linienklick_testen()
    {
        if (!a || linien.empty()) {
            g_message("Hirch 3D Linienklick: keine Linien");
            return;
        }
        V3 auge, f, sv, u;
        a->kamera(auge, f, sv, u);
        double w = std::max(1, a->bereich.get_allocated_width()), h = std::max(1, a->bereich.get_allocated_height());
        double t = std::tan(16.0 * M_PI / 180);
        for (auto const &L : linien) {
            if (L.seite != 1 || L.pkt.size() < 2) continue;
            for (std::size_t k = 1; k < L.pkt.size(); ++k) {
                auto const &p = L.pkt[k - 1], &q = L.pkt[k];
                if (std::isnan(p[0]) || std::isnan(q[0])) continue;
                double mx = (p[0] + q[0]) / 2, my = (p[1] + q[1]) / 2;
                V3 pt{mx, my, hoehe(mx, my, 1)};
                V3 r = pt - auge;
                double zc = punkt(r, f);
                if (zc <= 0) continue;
                double sx = (punkt(r, sv) / (zc * t * w / h) + 1) * w / 2, sy = (1 - punkt(r, u) / (zc * t)) * h / 2;
                if (sx < 300 || sy < 40 || sx > w - 40 || sy > h - 150) continue;    // nicht unter der Bedienung
                bool ok = klicken(sx, sy, false);
                std::string gew;
                if (auto sel = desk->getSelection())
                    for (auto it : sel->items())
                        if (it->getId()) gew += std::string(gew.empty() ? "" : ",") + it->getId();
                g_message("Hirch 3D Linienklick: Linie %s bei (%.0f, %.0f) → %s, gewählt: %s", L.id.c_str(), sx, sy,
                          ok ? "ok" : "nichts", gew.c_str());
                klicken(sx, sy, true);
                Glib::signal_timeout().connect_once([this] {
                    g_message("Hirch 3D Linienklick: nach Doppelklick 3D-Ansicht %s", get_mapped() ? "noch da" : "weg (2D)");
                }, 800);
                return;
            }
        }
        g_message("Hirch 3D Linienklick: keine Linie im Bild");
    }

    /// Punkt auf dem gefrästen Teil unter (sx, sy): Weltkoordinaten und Seite (1 oben, 2 unten), 0: daneben
    int teil_treffer(double sx, double sy, V3 &treffer)
    {
        if (!teil || raster.nx < 2) return 0;
        V3 auge;
        V3 dir = a->strahl(sx, sy, auge);
        double r = a->abstand > 0 ? a->abstand : a->grund_abstand();
        double schritt = 0.5 * std::min(raster.dx, raster.dy);
        for (double k = 0; k < r * 4; k += schritt) {
            V3 p = auge + dir * k;
            long q = raster.punkt(p.x, p.y);
            if (q < 0 || !raster.fest[q]) continue;
            if (p.z <= raster.o[q] + 1e-3 && p.z >= raster.u[q] - 1e-3) {
                treffer = p;
                return (raster.o[q] - p.z) <= (p.z - raster.u[q]) ? 1 : 2;
            }
        }
        return 0;
    }

    /// Tiefe des Planfräsens auf einer Seite (eingeschaltete Bearbeitungen «planen», grösste Tiefe), sonst 0
    double plan_tiefe(int seite) const
    {
        auto doc = desk ? desk->getDocument() : nullptr;
        auto t = doc ? doc->getReprRoot()->attribute("hirch:einstellungen") : nullptr;
        if (!t) return 0.0;
        double tiefe = 0.0;
        try {
            auto v = bj::parse(t);
            if (auto bea = v.is_object() ? v.as_object().if_contains("bearbeitungen") : nullptr; bea && bea->is_array())
                for (auto const &x : bea->as_array()) {
                    auto o = x.if_object();
                    if (!o || str(*o, "art") != "planen" || !flag(*o, "aktiv", true)) continue;
                    if (int(num(*o, "auf_seite", 1)) != seite) continue;
                    tiefe = std::max(tiefe, num(*o, "tiefe", 0));
                }
        } catch (...) {
        }
        return tiefe;
    }

    /// Rechtsklick ins Bild (Peter 08.11., Reiter «3D» aufgelöst): Flächen des Modells wählen und «Was steht hier?»
    /// mit «Restmaterial hier nachfräsen» – die Karten dazu erscheinen im Fenster «3D-Modell»
    std::unique_ptr<Gtk::Menu> rk_menue;
    bool rk_nochmal = false;
    void rechts_menue(double sx, double sy, GdkEventButton *e)
    {
        // ohne Fenster «3D-Modell» gibt es die Szene mit dem Modell noch nicht: erst öffnen, dann das Menü, sobald
        // das Modell in der Szene steht (höchstens 15 s)
        auto bereit = [this] { return a && a->hat_modell && a->beim_klick_modell && !a->index.empty(); };
        if (!bereit() && !rk_nochmal && desk && hirch_3d_hat_modell(desk->getDocument())) {
            ::hirch_3d_fenster(desk);
            rk_nochmal = true;
            auto start = g_get_monotonic_time();
            Glib::signal_timeout().connect([this, sx, sy, start, bereit] {
                if (!bereit() && g_get_monotonic_time() - start < 15'000'000) return true;
                rechts_menue(sx, sy, nullptr);
                rk_nochmal = false;
                return false;
            }, 200);
            return;
        }
        rk_menue = std::make_unique<Gtk::Menu>();
        bool modell = a->hat_modell && a->beim_klick_modell && !a->index.empty();
        auto f1 = Gtk::make_managed<Gtk::MenuItem>(TR("Fläche des Modells wählen"));
        auto f2 = Gtk::make_managed<Gtk::MenuItem>(TR("Fläche dazu wählen"));
        f1->set_sensitive(modell);
        f2->set_sensitive(modell);
        f1->signal_activate().connect([this, sx, sy] {
            a->treffen_modell(sx, sy, false);
            ::hirch_3d_fenster(desk);
        });
        f2->signal_activate().connect([this, sx, sy] {
            a->treffen_modell(sx, sy, true);
            ::hirch_3d_fenster(desk);
        });
        rk_menue->append(*f1);
        rk_menue->append(*f2);
        rk_menue->append(*Gtk::make_managed<Gtk::SeparatorMenuItem>());
        V3 p;
        int seite = teil_treffer(sx, sy, p);
        auto w = Gtk::make_managed<Gtk::MenuItem>(TR("Was steht hier? · Restmaterial nachfräsen …"));
        w->set_sensitive(seite > 0 && bool(a->beim_klick));
        w->signal_activate().connect([this, p] {
            long q = raster.punkt(p.x, p.y);
            double z = q >= 0 ? raster.o[q] : p.z;
            // Planfräsen verschiebt alle Bahnen der Seite um seine Tiefe nach unten (engine: plan_tiefe) – das
            // Modell steht in der Szene an seiner gezeichneten Lage; sonst hiesse es «zu tief» um die Plantiefe
            a->beim_klick(p.x, p.y, z, modell_bei(p.x, p.y, 1) - plan_tiefe(1));
            ::hirch_3d_fenster(desk);
        });
        rk_menue->append(*w);
        rk_menue->show_all();
        if (auto top = get_toplevel()) rk_menue->attach_to_widget(*top);
        if (g_getenv("HIRCH_3D_LINIEN")) g_message("Hirch 3D Rechtsklick (%.0f, %.0f) Modell %d Teil %d", sx, sy, modell, seite);
        // an der Stelle des Klicks (auch, wenn es später aufgeht und kein Auslöser mehr da ist)
        if (auto gw = a->bereich.get_window())
            rk_menue->popup_at_rect(gw, Gdk::Rectangle(int(sx), int(sy), 1, 1), Gdk::GRAVITY_NORTH_WEST,
                                    Gdk::GRAVITY_NORTH_WEST, reinterpret_cast<GdkEvent *>(e));
    }

    /// Klick auf das Teil: welche Linie liegt dort? Wählt sie (auch in 2D); Doppelklick: zur 2D-Zeichnung
    bool klicken(double sx, double sy, bool doppel)
    {
        if (!teil || raster.nx < 2) return false;
        V3 auge;
        V3 dir = a->strahl(sx, sy, auge);
        double r = a->abstand > 0 ? a->abstand : a->grund_abstand();
        double schritt = 0.5 * std::min(raster.dx, raster.dy);
        V3 treffer;
        int seite = 0;
        for (double k = 0; k < r * 4; k += schritt) {
            V3 p = auge + dir * k;
            long q = raster.punkt(p.x, p.y);
            if (q < 0 || !raster.fest[q]) continue;
            if (p.z <= raster.o[q] + 1e-3 && p.z >= raster.u[q] - 1e-3) {
                treffer = p;
                seite = (raster.o[q] - p.z) <= (p.z - raster.u[q]) ? 1 : 2;
                break;
            }
        }
        if (!seite) return false;
        double tol = std::max(1.0, 0.006 * std::sqrt(punkt(treffer - auge, treffer - auge)));
        double best = tol;
        std::string id;
        for (auto const &L : linien) {
            if (L.seite != seite) continue;
            for (std::size_t k = 1; k < L.pkt.size(); ++k) {
                auto const &p = L.pkt[k - 1], &q = L.pkt[k];
                if (std::isnan(p[0]) || std::isnan(q[0])) continue;
                double vx = q[0] - p[0], vy = q[1] - p[1], l2 = vx * vx + vy * vy;
                double t = l2 > 0 ? std::clamp(((treffer.x - p[0]) * vx + (treffer.y - p[1]) * vy) / l2, 0.0, 1.0) : 0;
                double dd = std::hypot(p[0] + t * vx - treffer.x, p[1] + t * vy - treffer.y);
                if (dd < best) {
                    best = dd;
                    id = L.id;
                }
            }
        }
        auto doc = desk->getDocument();
        auto sel = desk->getSelection();
        if (id.empty() || !doc || !sel) {
            if (!doppel && sel) sel->clear();
            return !doppel;
        }
        if (auto it = cast<SPItem>(doc->getObjectById(id))) sel->set(it);
        if (doppel)
            if (auto win = desk->getInkscapeWindow())
                win->activate_action("hirch-ansicht", Glib::Variant<Glib::ustring>::create("2d"));
        return true;
    }
};

}

Gtk::Widget *hirch_ansicht_3d_neu(SPDesktop *desktop)
{
    return Gtk::make_managed<AnsichtFenster>(desktop);
}

namespace {
/// beim Start anmelden (Basis, 22.10.): der Umschalter 2D | 3D ist dann in jedem Fenster da; die Ansicht
/// zeigt die Szene, sobald der Reiter «3D» sie füllt. hirch-ansicht.cpp hält alles in Funktions-Statics,
/// darum ist das als statische Initialisierung sicher.
struct AnsichtAnmelden {
    AnsichtAnmelden()
    {
        hirch_ansicht_3d_anmelden(hirch_ansicht_3d_neu);
        hirch_ansicht_3d_sprung_anmelden([](SPDesktop *d, std::string const &schluessel) {
            auto it = AnsichtFenster::alle().find(d);
            if (it != AnsichtFenster::alle().end()) it->second->springen(schluessel);
        });
    }
} ansicht_anmelden;
}


} // namespace Inkscape::UI::Dialog
