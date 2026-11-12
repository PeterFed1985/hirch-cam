// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: das Panel selbst (struct HirchFraesen::Impl)
 * Nur für hirch-*.cpp – kein öffentlicher Header. Sammelkopf: hirch-intern.h.
 */
#ifndef HIRCH_PANEL_IMPL_H
#define HIRCH_PANEL_IMPL_H

#include "ui/dialog/hirch-hilfen.h"

namespace Inkscape::UI::Dialog {

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct HirchFraesen::Impl {
    HirchFraesen &panel;

    // Daten
    bj::object analyse;      // Ergebnis von «analyse»
    bj::object einst;        // Einstellungen (Werkzeuge, Bearbeitungen, …)
    bj::object ergebnis;     // Ergebnis von «rechnen»
    bool hat_analyse = false;
    bool ergebnis_aktuell = false;
    bool rechnet = false;
    // Rechenanzeige oben im Panel (Uhr, Fortschritt, Abbrechen); Takt meldet auch an Leiste und Reiter «3D»
    Gtk::Box rechen_balken{Gtk::ORIENTATION_VERTICAL, 0};
    Gtk::ProgressBar rechen_puls;
    Gtk::Spinner rechen_kreisel;
    Gtk::Label rechen_titel, rechen_schritt, rechen_schild;
    Gtk::CheckButton rechen_entwurf;        // 3d 08.11.: «Entwurf» aus dem Reiter «3D» hierher (nur mit 3D-Modell)
    bool rechen_entwurf_setzt = false;
    Gtk::Button rechen_knopf;
    gint64 rechen_start = 0;
    double rechen_dauer = 0;
    bool rechnen_abgebrochen = false, war_rechnet = false;
    // 3D-Zeichnungen rechnen auf Knopfdruck: nach der Analyse wartet das Panel auf «Rechnen»
    // (wartet_knopf); knopf_gen/knopf_svg: die analysierte Zeichnung, die dann gerechnet wird
    bool wartet_knopf = false, rechnen_gewollt = false, war_wartet = false;
    // Rechnen erst, wenn du fertig bist (Peter, 21.10.): mit Text-, Knoten-, Zeichenwerkzeug wartet es
    bool pausiert = false;                  // Änderung wartet auf den Werkzeugwechsel
    bool pause_text = false;                // … und das Werkzeug ist das Textwerkzeug
    gint64 schnell_bis = 0;                 // eben eine Einstellung geschrieben: gleich rechnen
    sigc::connection werkzeug_wechsel;
    // Klick auf eine leere Stelle einer Zeichenfläche wählt sie (Peter, 25.10.)
    Inkscape::auto_connection leinwand_druck, leinwand_los;
    Geom::Point leinwand_von;
    bool leinwand_gedrueckt = false;
    void leinwand_klick(Geom::Point const &fenster);
    void leinwand_verbinden();
    std::function<void()> nach_rechnen;     // Vorschau/Zusammenfassung nach einer Rechnung öffnen
    void aenderung();
    bool werkzeug_pausiert(bool *text = nullptr);
    static bool auto_rechnen();
    std::string modus_kennung();            // Entwurf oder genau gehört zur Kennung eines Ergebnisses
    unsigned knopf_gen = 0;
    std::string knopf_svg;
    void rechnen_jetzt();
    // «+ Neu»: der Dialog der neuen Bearbeitung geht zuerst auf, gerechnet wird erst nach
    // «Übernehmen»; «Abbrechen» nimmt Bearbeitung und Farbe wieder heraus
    std::string neu_id;                         // neue Bearbeitung, deren Dialog aufgeht/offen ist
    bool neu_offen = false, neu_wartet = false;
    unsigned neu_gen = 0, neu_ab = 0;
    std::string neu_svg;
    std::vector<std::pair<std::string, std::string>> neu_stile;   // id -> style vor dem Einfärben
    sigc::connection neu_notfall;               // Dialog spätestens nach kurzer Zeit öffnen
    int dialog_antwort = 0;                     // letzte Antwort eines Bearbeitungsdialogs
    void neu_dialog();
    void wartet_melden();
    sigc::connection rechen_takt;
    std::string fortschritt_datei() { return Glib::build_filename(ordner, "fortschritt.json"); }
    void rechen_balken_bauen();
    void rechenanzeige();
    bool rechen_tick();
    void rechen_melden(bool laeuft, std::string const &uhr);
    void rechnen_abbrechen();
    bool speichern_wartet = false;
    // 3D-Entwurfsmodus (/hirch/3d/entwurf): vor dem Speichern/USB-Stick genau nachrechnen
    bool genau_noetig = false, stick_wartet = false;
    bool entwurf_an() { return Inkscape::Preferences::get()->getBool("/hirch/3d/entwurf", true); }
    std::string fehlertext;
    std::string stamm_rechnung = "programm";   // Dateiname-Stamm der letzten Rechnung

    // Hintergrund
    std::string ordner;      // Arbeitsordner
    Glib::ustring letzte_xml;
    // Neu rechnen nur bei Änderungen am Gefrästen (Kennung aus der Analyse, Peter 21.10.)
    std::string kennung_fertig;     // Kennung des aktuellen Ergebnisses
    std::string kennung_laufend;    // Kennung der laufenden Rechnung
    unsigned rechen_gen = 0;        // Generation, deren Rechnung gilt (0 = keine läuft)
    bool erzwungen = false;
    sigc::connection status_spaeter;
    std::function<void()> geparkt;  // fertige Rechnung, während schon die nächste Analyse lief
    bool nur_sichtbar_geaendert = false;   ///< Auge umgeschaltet: nicht neu rechnen
    unsigned generation = 0;
    std::set<Lauf *> laeufe;
    std::shared_ptr<DienstProzess> dienst;        // Rechenkern als Dienst (analyse, rechnen, vorschau)
    std::deque<DienstAuftrag> dienst_warte;
    void dienst_weiter();
    bool dienst_starten();
    bool dienst_meiden = false;                   // Notweg: diesen Auftrag als eigenen Prozess
    std::shared_ptr<int> panel_lebt = std::make_shared<int>(1);   // für verzögerte Aufrufe: Panel noch da?
    sigc::connection timer, doc_mod, neu_zeigen, doc_tot;
    sigc::connection seite2_bald;     // 3D (B-14): Unterseite auf Seite 2 dem Modell nachführen
    WurzelBeobachter beobachter;
    AuchBeobachter auch_beobachter;
    XML::Node *beobachtet = nullptr;
    bool baut_ui = false;

    // Oberfläche
    Gtk::ScrolledWindow scroll;
    Gtk::Box inhalt{Gtk::ORIENTATION_VERTICAL, 14};
    Gtk::Label mat_groesse, mat_z0, mat_null, mat_sicher, mat_spann;
    Gtk::Box liste{Gtk::ORIENTATION_VERTICAL, 10};
    std::vector<Gtk::ListBox *> op_listen;
    sigc::connection entf_taste;          ///< Entf im Fenster: gewählte Bearbeitung löschen (vor Inkscapes «Löschen»)
    Gtk::Window *entf_fenster = nullptr;
    void entf_taste_verbinden();
    // Knöpfe «Auswahl zuweisen» der Zeilen (sichtbar je nach Auswahl in der Zeichnung)
    struct ZuweisenKnopf {
        std::string farbe, name;
        Gtk::Button *knopf;
        Gtk::Label *zahl;
    };
    std::vector<ZuweisenKnopf> zuweisen_knoepfe;
    void zuweisen_auffrischen();
    void hin_rollen(Gtk::Widget *w);         // Liste sanft zu dieser Zeile rollen
    sigc::connection rollen;
    // Ziehen im Ablauf (eigene Umsetzung statt GTK-Drag&Drop: Karte folgt der Maus,
    // am Ziel öffnet sich weich eine Lücke, gerechnet wird erst nach dem Loslassen)
    struct Ziehen {
        bool gedrueckt = false, aktiv = false, gruppe = false;
        double x0 = 0, y0 = 0, xr = 0, yr = 0;  // Druckpunkt und Maus (Bildschirm)
        std::vector<int> quelle;                // Bearbeitungen, die wandern
        Gtk::Widget *quell_widget = nullptr;    // Zeile bzw. Gruppenkarte
        Gtk::ListBox *quell_lb = nullptr;
        Cairo::RefPtr<Cairo::ImageSurface> bild;
        int bw = 0, bh = 0, bx = 0;             // Grösse, linker Rand (Überlagerung)
        double greif_y = 0;                     // Maus im Widget
        Gtk::ListBox *ziel_lb = nullptr;        // Zeilen: Liste und Stelle
        int ziel_pos = -1;                      // Gruppen: Stelle in «liste»
        Gtk::Widget *luecke = nullptr;
        sigc::connection takt;
        sigc::connection taste;                 // Esc bricht ab
        int los = 0;                            // Takte, in denen die Taste nicht mehr gedrückt ist
    } zg;
    std::vector<Gtk::Widget *> luecken_zu;      // schliessen gerade (werden danach entfernt)
    Gtk::Window schwebe_fenster{Gtk::WINDOW_POPUP};
    Gtk::DrawingArea schwebe;
    bool anzeigen_wartet = false;
    bj::object vorlaeufig;                      // Ablauf sofort nach dem Umordnen, bis neu gerechnet ist
    bool vorlaeufig_an = false;
    /// Peter 02.11.: aus einer Bearbeitung genommene Formen (Rahmen auf der Arbeitsfläche, alte Bearbeitungen) –
    /// ihre Bahnen verschwinden sofort, bis die neue Rechnung da ist
    struct WegRahmen { std::set<std::string> ids; Geom::Rect rahmen; };
    std::vector<WegRahmen> weg_rahmen;
    std::set<std::string> geloeschte_ids;   ///< in dieser Sitzung gelöschte Bearbeitungen: Bahnen und Zeitleiste ohne sie
    int gewaehlt_index = -1;
    int seite_aktiv = 1;
    // mehrere Platten (Seiten der Zeichnung, z. B. nach dem Verschachteln): welche gerechnet wird
    int platte = 1, platten = 1;
    Gtk::Box platten_box{Gtk::ORIENTATION_HORIZONTAL, 6};
    Gtk::Button platte_zurueck, platte_vor;
    Gtk::Label platte_text;
    Gtk::Button umriss_btn;
    // Knopfzeile unter den Reitern (Peter, 24.10.): Zeichenfläche hinzufügen/entfernen, Vorrichtung
    Gtk::MenuButton zf_plus;
    Gtk::Menu zf_menu;
    Gtk::MenuItem zf_rueck, zf_vorr, zf_platte;
    Gtk::Button zf_weg, zf_groesse;
    Gtk::MenuButton maschine_knopf;          // B-25: Maschine der Zeichnung (nur bei mehr als einer Maschine)
    Gtk::ToggleButton zf_vor;
    std::string einrichten_vorwahl;            // «zweiseitig» / «vorrichtung»: im Einrichten-Dialog vorab an
    void zeichenflaeche_neu_platte();
    void zeichenflaeche_entfernen();
    Gtk::Label vor_hinweis;
    sigc::connection seite_gewaehlt;
    Gtk::Box seiten_box{Gtk::ORIENTATION_HORIZONTAL, 0};
    Gtk::ToggleButton seite_btn[2];
    Gtk::Label ablauf_titel, mat_zeile;
    Gtk::Box wenden_box{Gtk::ORIENTATION_HORIZONTAL, 10};
    Gtk::Label wenden_text;
    Gtk::DrawingArea strahl;
    Gtk::Label strahl_info;                 // beim Ziehen: Bearbeitung · % · mm/min · Zeit
    // Zeitstrahl: Lage der Abschnitte (für das Ziehen des Vorschubs)
    struct StrahlStueck { double x0, x1; std::string key, id; double min; };
    std::vector<StrahlStueck> strahl_teile;
    int zieh = -1;                          // gezogener Abschnitt
    double zieh_min = 0, zieh_neu = 0, zieh_rest = 0, zieh_vm = 0, zieh_f_alt = 100, zieh_f = 100;
    bool zieh_zu_schnell = false, blink_an = false;
    sigc::connection blink;
    Gtk::Label liste_leer, tipp;
    Gtk::Box warnbox{Gtk::ORIENTATION_VERTICAL, 4};
    Gtk::Label gesamtzeit, status;
    Gtk::Spinner spinner;
    Gtk::Button vorschau_btn, speichern_btn, stick_btn, zusammen_btn;
    Gtk::MenuButton speichern_mehr;            // ▾ an «G-Code speichern»: Luftfräsen
    Gtk::Menu speichern_menu;
    Gtk::MenuItem *luft_stick_item = nullptr;
    Gtk::Switch bahnen_check, geister_switch;
    Inkscape::PrefObserver geister_beobachter;
    Gtk::Box seite_fraesen{Gtk::ORIENTATION_VERTICAL, 0};
    Gtk::Switch dunkel_switch, feine_switch;


    void darstellung_zeigen();
    std::vector<CanvasItemPtr<CanvasItemBpath>> bahn_items;
    SPDesktop *bahn_desktop = nullptr;
    sigc::connection bahn_desktop_tot;
    Gtk::MenuButton neu_btn;
    Gtk::Menu neu_menu;
    void neu_menu_bauen();

    // 3D-Vorschau
    HirchVorschau *vorschau_fenster = nullptr;

    explicit Impl(HirchFraesen &p) : panel(p)
    {
        beobachter.geaendert = [this] { spaeter(); };
        auch_beobachter.geaendert = [this] { spaeter(); };
        css_einrichten();
        rad_global();
        char *d = g_dir_make_tmp("hirch-cam-XXXXXX", nullptr);
        ordner = d ? d : Glib::build_filename(Glib::get_tmp_dir(), "hirch-cam");
        g_free(d);
        bauen();
    }

    ~Impl()
    {
        if (maschine_ziel) g_object_remove_weak_pointer(G_OBJECT(maschine_ziel), (gpointer *)&maschine_ziel);
        entf_taste.disconnect();
        marke_weg.disconnect();
        marke.reset();
        bahn_desktop_tot.disconnect();
        bahnen_weg();
        timer.disconnect();
        seite_timer.disconnect();
        canvas_groesse.disconnect();
        doc_mod.disconnect();
        seite2_bald.disconnect();
        doc_tot.disconnect();
        seite_gewaehlt.disconnect();
        neu_zeigen.disconnect();
        beobachten(nullptr);
        abbrechen();
        if (dienst) {                // Dienst beenden (stdin zu: Python endet von selbst)
            dienst->beenden();
            dienst.reset();
        }
        if (vorschau_fenster) {
            vorschau_fenster->hide();
            delete vorschau_fenster;
        }
        ordner_leeren(true);
    }

    /// Arbeitsordner (zwei Ebenen: zeichnung.svg, ausgabe/…) leeren
    static void leeren(std::string const &pfad, bool auch_ordner);

    void ordner_leeren(bool auch_ordner);

    /// Stelle (mm, Nullpunkt unten links) in der Zeichnung zeigen: hinzoomen
    /// und kurz mit einem roten Kreis markieren
    CanvasItemPtr<CanvasItemBpath> marke;
    sigc::connection marke_weg;
    void ort_zeigen(double x, double y, int seite = 1);

    /// Befund der Prüfung als Zeile: mit Ort und/oder Objekten klickbar
    /// (Objekte werden ausgewählt, die Stelle gezeigt)
    Gtk::Widget *befund_widget(bj::object const &o, Gtk::Label *l);

    // ---- Hinweise abhaken (Checkliste): Texte stehen in hirch:quittiert (JSON-Liste) an der
    // Wurzel; ändert sich ein Hinweis inhaltlich, ist er wieder offen
    static constexpr char const *QUITTIERT = "hirch:quittiert";

    std::set<std::string> quittiert_lesen();

    void quittieren(std::string const &text, bool an);

    /// Hinweis mit Häkchen davor: abgehakt grün mit ✓. o: Befund der Prüfung (klickbar) oder null
    Gtk::Widget *hinweis_widget(std::string const &text, bj::object const *o, std::set<std::string> const &q,
                                int max_zeichen = 0);

    Gtk::Window *fenster();

    void meldung(std::string const &titel, std::string const &text, Gtk::MessageType typ);

    /// Reiter «Ansicht»: allgemeine Darstellung (gilt für alle Zeichnungen)
    static bool geister_an();

    // ------------------------------------------------------------- Aufbau
    void bauen();

    // ------------------------------------------------------- Dokument
    SPDocument *doc();

    void beobachten(XML::Node *wurzel);

    void dokument_neu();

    /// Dokument und Seite auf die Materialgrösse setzen (mm). Die Vorlage hat
    /// eine eigene Seiten-Angabe (inkscape:page); die muss mit, sonst bleibt
    /// die sichtbare Seite gross, während das Dokument schon klein ist.
    static void material_groesse(SPDocument *d, double b_mm, double h_mm);
    /// eigene Grösse einer Platte (Seite hat hirch:platte-groesse, mm) in Benutzereinheiten
    static bool platte_groesse_von(Inkscape::XML::Node *r, double &w_uu, double &h_uu, double uu_px);
    void platte_groesse_dialog();

    /// Ansicht auf die Seite (= Material) einpassen
    sigc::connection seite_timer, canvas_groesse;
    gint64 einpassen_bis = 0;
    void seite_zeigen(int ms = 0);

    void spaeter(int ms = 450);

    void abbrechen();

    void python(std::vector<std::string> args,
                std::function<void(bool, std::string const &, std::string const &)> fertig, bool abbrechbar = true);

    static bool json_lesen(std::string const &text, bj::object &ziel);

    /// Für die Rechnung: Text (auch Fliesstext) in einer Kopie der Zeichnung
    /// in Pfade umwandeln. Die Zeichnung selbst bleibt unverändert.
    static Glib::ustring text_als_pfade(SPDocument *d);

    static bool hat_text(Glib::ustring const &xml);

    void auffrischen();

    void rechnen(unsigned gen, std::string const &svg);

    // ------------------------------------------------------- Fräsbahnen
    static bool bahnen_an();

    void bahnen_setzen(bool an);

    /// Mit dem Fenster verschwindet auch die Leinwand – eigene Elemente darauf
    /// dann nur noch loslassen, nicht selbst entfernen
    void desktop_beobachten(SPDesktop *desk);

    void bahnen_weg();

    /// Nullpunkt in Materialkoordinaten (mm von unten links)
    Geom::Point nullpunkt_mm();

    /// Nullpunkt in der Zeichnung zeigen: Kreis mit X-Pfeil (rot) und Y-Pfeil (grün)
    CanvasItemPtr<CanvasItemBpath> np_kreis, np_x, np_y, np2_kreis, np2_x, np2_y;
    // Passlöcher (Peter 26.10.): Paare sichtbar – Nummern, Linie zum Partner, Stifte auf Seite 2
    std::vector<CanvasItemPtr<CanvasItemBpath>> pass_linien;
    std::vector<CanvasItemPtr<CanvasItemText>> pass_texte;
    std::string np_stand;          // zuletzt gemeldete analyse["nullpunkte"] (Lineale neu zeichnen, wenn anders)
    std::vector<CanvasItemPtr<CanvasItemBpath>> geister;   // zweiseitig: andere Seite blass, gespiegelt
    void nullpunkt_zeichnen();

    /// Neue Zeichnung: zuerst Material und Nullpunkt einrichten
    std::set<std::string> eingerichtet_gefragt;
    void einrichten_beim_start();

    /// Fräsbahnen der letzten Rechnung über die Zeichnung legen (nur Anzeige,
    /// die Zeichnung selbst bleibt unverändert)
    void bahnen_zeichnen();

    /// Linke obere Ecke einer Seite der Zeichnung in mm (Seite 1 meist 0/0)
    void seite_ecke(int seite, double &x, double &y);
    /// Nullpunkt des Werkstücks (bzw. der Vorrichtung) in Dokument-px auf der Seite, auf der «bei» (Dokument-px)
    /// liegt – Drehpunkt für «Anordnen» (Peter, 21.10.: Impeller um den Werkstück-Nullpunkt)
    bool nullpunkt_doc(Geom::Point bei, Geom::Point &aus);
    /// Nullpunkt der Platte/Seite aus dem Rechenkern (analyse «nullpunkte», Dokument-mm); Platte 0 = Vorrichtung
    bool nullpunkt_aus_analyse(int platte_, int seite, Geom::Point &aus_mm);

    static std::string warnungen_text(bj::object const &o);

    // ------------------------------------------------------- Anzeige
    /// Anzahl Objekte einer Bearbeitung (nach ID)
    long objekte(std::string const &id);

    /// Objekte, die nur über «zusätzlich hier fräsen» (hirch:auch) dazugehören
    long zusaetzlich(std::string const &id);

    std::string name_von(bj::object const &b);

    /// Wie viele Bearbeitungen hat diese Fräsart?
    int je_art(std::string const &art);

    /// Nächster freier Farbton einer Fräsart (Liste kommt vom Rechenkern)
    std::string freier_ton(std::string const &art);

    std::string neue_id();

    bj::object const *werkzeug(std::string const &key);

    std::string werkzeug_kurz(std::string const &key);

    /// Name eines Werkzeugs, wie ihn der Ablauf zeigt (vom Rechenkern)
    std::string wz_titel(std::string const &key);


    /// «· geschlichtet · Aufmass +0,2» für Tasche und Kontur
    std::string zusatz_text(bj::object const &b);

    // Zeiten der letzten Rechnung je Teil (id bzw. id#teil): bleiben stehen, solange neu
    // gerechnet wird; Gesamt- und Seitenzeit werden daraus sofort geschätzt
    std::map<std::string, double> zeit_cache;
    std::map<int, double> seiten_rest;          // Zeit je Seite ausserhalb der Bearbeitungen (Wechsel …)
    // Ergebnis je Platte: beim Wechsel zurück steht die Rechnung wieder da (Peter, 25.10.)
    struct PlattenStand {
        bj::object ergebnis;
        std::string kennung;
        std::map<std::string, double> zeit_cache;
        std::map<int, double> seiten_rest;
        double dauer = 0;
    };
    std::map<int, PlattenStand> platten_stand;
    double dauer_wieder = 0, dauer_vor_analyse = 0;
    std::string ausgabe_ordner() const        // je Platte eigener Ordner: Ergebnisse bleiben stehen
    { return Glib::build_filename(ordner, platte == 1 ? std::string("ausgabe") : "ausgabe" + std::to_string(platte)); }
    bool bahnen_wieder = false;
    bool platte_leer = false;                   // mehrere Platten, auf dieser nichts zu fräsen                 // zurückgeholtes Ergebnis: Bahnen neu zeichnen

    static std::string op_schluessel(bj::object const &op);

    void zeiten_merken();

    /// Minuten eines Teils im Ablauf: gerechnet, sonst die zuletzt bekannte Zeit
    double op_minuten(bj::object const &op);

    /// Zuletzt bekannte Zeit einer ganzen Bearbeitung (alle Teile)
    double bearbeitung_minuten(std::string const &id);

    /// Zeit einer Seite (0 = alle): gerechnet oder aus den gemerkten Zeiten geschätzt
    double seiten_minuten(int seite);

    /// Ablauf (Seiten › Werkzeuge › Bearbeitungen): nach dem Rechnen mit Zeiten, sonst vorab
    bj::object const *ablauf_daten();

    bool zweiseitig();

    /// Seite 1 oder 2 zeigen: Liste umschalten und die Seite der Zeichnung ins Bild holen
    void seite_waehlen(int n, bool zoomen);

    bool vorrichtung_an();

    int platte_min();

    /// Andere Platte rechnen (und auf ihre Seite zoomen)
    void platte_waehlen(int n, bool zoomen);

    /// Seite im Zeichenfenster gewählt (Seiten-Werkzeug, Auswahl): Platte und Seite folgen
    void seite_gewaehlt_von(SPPage *pg);

    /// Index einer Bearbeitung in den Einstellungen (−1: keine, z. B. Passlöcher)
    int index_von(std::string const &id);

    /// Zeitstrahl: Bearbeitungen in ihrer Farbe, Striche für Werkzeugwechsel, violett für Wenden
    void strahl_zeichnen(Cr const &cr);

    /// Abschnitt, dessen rechte Kante bei x liegt (±6 Bildpunkte)
    int strahl_kante(double x);

    int strahl_abschnitt(double x);

    bj::object const *bearbeitung_statistik(std::string const &id);

    std::string strahl_text(int i, double f, double minuten, double alt_min);
    std::string strahl_rechnung(int i, double f);

    bool strahl_druck(GdkEventButton *e);

    bool strahl_bewegung(GdkEventMotion *e);

    bool strahl_los(GdkEventButton *);

    /// Eine Zeile im Ablauf. op: Bearbeitung aus dem Ablauf (null: nicht gefräst, aus
    /// den Einstellungen); index: Bearbeitung in den Einstellungen (−1: Passlöcher)
    /// unter: 0 eigene Zeile, 1 Durchgang direkt unter seiner Bearbeitung (gleiches
    /// Werkzeug, eingerückt), 2 Durchgang in einer anderen Werkzeuggruppe
    Gtk::Widget *op_zeile(bj::object const *op, bj::object const *b, int index, int unterz = 0);

    /// Gruppe (Werkzeug) mit Kopfzeile und ihren Bearbeitungen
    Gtk::ListBox *gruppe(std::string const &chip, std::string const &titel, std::string const &rechts = {}, std::string const &slot = {});

    /// Bearbeitungen (Index in den Einstellungen) einer Gruppe im Ablauf, in Reihenfolge
    std::vector<int> gruppen_indizes(Gtk::ListBox *lb);
    void gruppe_fraeser_wechseln(std::vector<int> const &indizes, std::string const &slot);



    // ------------------------------------------------------- Ziehen im Ablauf
    /// Bildschirmpunkt → Koordinaten im Widget
    static bool punkt_lokal(Gtk::Widget &w, double xr, double yr, int &lx, int &ly);

    /// Maustasten und Bewegung an einer Zeile bzw. Gruppen-Kopfzeile anmelden
    void ziehbar(Gtk::EventBox &eb, std::function<std::vector<int>()> quelle, Gtk::Widget *(*widget_von)(Gtk::EventBox &),
                 bool gruppe);

    bool ziehen_starten();

    void ziehen_bewegen();

    void schwebe_setzen();

    bool schwebe_malen(Cairo::RefPtr<Cairo::Context> const &cr);

    /// Zeilen einer Liste ohne Lücken (Name «luecke»)
    static std::vector<Gtk::ListBoxRow *> echte_zeilen(Gtk::ListBox *lb);

    /// Karten (Werkzeuggruppen) im Ablauf ohne Lücken
    std::vector<Gtk::Widget *> echte_karten();

    void ziel_suchen();

    void luecke_schliessen();

    void luecke_entfernen(Gtk::Widget *l);

    void ziehen_beenden(bool ablegen);

    /// Duplikat aus seiner Werkzeuggruppe gezogen: löst sich von den Objekten (eigener
    /// Farbton). Eine leere Bearbeitung neben eine derselben Art gelegt: übernimmt deren
    /// Farbe und damit deren Objekte.
    void farbe_beim_ablegen(int i, int anker, std::vector<int> const &abschnitt);

    /// Bearbeitungen q direkt vor bzw. hinter die Bearbeitung «anker» setzen
    void einordnen(std::vector<int> q, int anker, bool davor);

    /// Ablauf sofort in der neuen Reihenfolge zeigen (dieselben Regeln wie ablauf.py),
    /// mit den Daten der letzten Rechnung – bis die neue Rechnung da ist
    void vorlaeufig_bauen();

    void anzeigen();

    bool hinweise_offen = false;   // Hinweise unter dem Ablauf aufgeklappt?

    void anzeigen_status();

    // ------------------------------------------------------- Ändern
    void schreiben(std::string const &was);

    /// Ablauf einmal sinnvoll ordnen (wie einstellungen.sinnvoll_ordnen):
    /// je Seite Planen zuerst, Kontur zuletzt, gleiche Werkzeuge beieinander
    void ordnen();

    /// Ausgewählte Objekte in einer Farbe einfärben (Linie, ohne Füllung)
    bool auswahl_faerben(std::string const &hex);

    /// Neue Bearbeitung anlegen – auch eine weitere derselben Fräsart (eigener Farbton)
    void neu(std::string const &art);

    bj::object *bearbeitung(int index);

    /// Darf weg: ohne Objekte, oder es gibt eine andere derselben Fräsart,
    /// die die Objekte übernimmt
    bool entfernbar(bj::object const &b);

    void entfernen(int index);

    /// Liste von IDs (Leerzeichen) ohne «id» bzw. mit «id»
    static std::string ohne(std::string const &liste, std::string const &id);

    static std::string mit(std::string const &liste, std::string const &id);

    /// Auswahl zusätzlich in dieser Bearbeitung fräsen (Farbe bleibt) – oder
    /// diese Zusatz-Zuweisung wieder lösen
    void auch_setzen(int index, bool hinzu);

    /// Hat ein ausgewähltes Objekt (oder seine Gruppe) diese Zusatz-Zuweisung?
    bool auswahl_hat_auch(std::string const &id);

    void duplizieren(int index);

    /// Kopie mit denselben Objekten (gleiche Farbe) und denselben Einstellungen direkt
    /// darunter – z. B. dieselbe Tasche nochmals im Gegenlauf. Zieht man sie im Ablauf
    /// aus ihrer Werkzeuggruppe, löst sie sich von den Objekten (eigener Farbton).
    void duplizieren_gleich(int index);

    /// Bearbeitungen derselben Art mit genau derselben Farbe (Duplikate)
    std::vector<int> geschwister(int index);

    /// Objekte einer Bearbeitung aus- oder einblenden. Ausgeblendet heisst
    /// hier nur «nicht zeigen»: hirch:versteckt sorgt dafür, dass sie trotzdem
    /// gefräst werden. Die Fräsbahnen folgen (bahnen_zeichnen).
    void sichtbar_umschalten(int index);

    void zuweisen(int index);

    /// Kommt dieser Teil (id bzw. id#teil) im gerechneten Programm vor?
    bool im_programm(std::string const &key);

    void kontextmenue(int index, GdkEvent *ereignis, std::string const &op_key = {}, std::string const &op_name = {});

    void objekte_waehlen(int index);

    // ------------------------------------------------------- Dialoge
    std::unique_ptr<Gtk::Dialog> dialog(std::string const &titel);

    /// bohrer: Bohrer zur Wahl stellen (nur wo gebohrt wird); sonst nur, wenn einer gewählt ist
    void werkzeug_combo(Gtk::ComboBoxText &c, std::string const &aktiv, bool mit_keinem, bool bohrer = false);
    /// Alle Werkzeuge: die der Zeichnung, dann «Meine Werkzeuge» (id «db:…»), zuletzt «Neues Werkzeug …» (id «neu»)
    void werkzeug_combo_alle(Gtk::ComboBoxText &c, std::string const &aktiv, bool bohrer = true);
    /// Fräsart des offenen Bearbeitungsdialogs: bestimmt, welche Werkzeuge «passend» oben stehen
    std::string wz_kontext;
    /// Aufklappfeld der Werkzeugliste: direkt unter dem Feld, Suche, Überschriften
    void werkzeug_aufklappen(Gtk::ComboBoxText &c);
    /// Wahl aus werkzeug_combo_alle in einen Platz der Zeichnung verwandeln («db:…» wird übernommen)
    std::string werkzeug_platz(std::string const &wahl);

    /// Zuletzt offener Reiter je Fräsart (der Dialog öffnet wieder dort)
    std::map<std::string, int> letzter_reiter;

    /// Werkzeug mit den Schnittdaten, die im gewählten Material gelten
    bj::object wirksam(std::string const &key);

    std::string werkstoff_name();

    /// Felder des Bearbeitungsdialogs; lebt länger als der Dialog, damit
    /// Signale beim Abbau nie ins Leere greifen
    struct Felder {
        Gtk::Entry *titel = nullptr;
        Gtk::ComboBoxText *werkzeug = nullptr, *flach = nullptr, *seite = nullptr, *richtung = nullptr,
                          *strategie = nullptr, *eintauchen = nullptr, *swerkzeug = nullptr, *smodus = nullptr,
                          *methode = nullptr, *anfahren = nullptr, *zweck = nullptr, *prichtung = nullptr,
                          *gart = nullptr, *gdreh = nullptr, *gkern = nullptr, *rwerkzeug = nullptr,
                          *tiefe_ab = nullptr;   // 3D: Tiefe ab Materialoberfläche, 3D-Modell, bis aufs Modell, Haut
        Gtk::SpinButton *tiefe = nullptr, *zust = nullptr, *maxt = nullptr, *bahn = nullptr, *alast = nullptr,
                        *aufmass = nullptr, *rwinkel = nullptr, *vollnut = nullptr, *vprozent = nullptr,
                        *sanz = nullptr, *sbreit = nullptr, *shoch = nullptr, *szugabe = nullptr, *szust = nullptr,
                        *svprozent = nullptr, *aradius = nullptr, *fbreite = nullptr, *ueber = nullptr,
                        *gsteig = nullptr, *gdurch = nullptr, *gpass = nullptr, *rzust = nullptr,
                        *vzust = nullptr, *dick = nullptr,   // V-Carve: Zustellung, Aufdicken
                        *starttiefe = nullptr,               // Kontur: «Beginnt bei Tiefe»
                        *stiefe = nullptr,                   // B-2: eigene Tiefe beim Schlichten
                        *tiefe_ab_mm = nullptr;              // «bis auf das 3D-Modell»: tiefer (+) / weniger tief (−)
        Gtk::ComboBoxText *stiefe_art = nullptr;             // B-2: «wie beim Schruppen» | «eigene»
        double szust_ganz = -1;      // «ganze Tiefe»: diese Zahl zeigt das Feld; unverändert bleibt der Wert leer
        std::shared_ptr<ZustellArt> zart;                    // Art der Zustellungen (Kontur, Gravur, V-Carve)
        Gtk::ComboBoxText *gsteig_art = nullptr;             // Gewinde: Regelgewinde oder eigene Steigung
        bool ungueltig = false;                              // so nicht fräsbar (Gewinde: falsches Werkzeug)
        Gtk::ComboBoxText *srichtung = nullptr;              // Fräsrichtung beim Schlichten
        Gtk::CheckButton *durch = nullptr, *schlichten = nullptr, *dogbone = nullptr,
                         *spitze = nullptr;   // Bohren mit Bohrer: Spitze zur Tiefe dazu
        std::vector<std::function<void()>> neu;   // alles, was live nachgeführt wird
        std::vector<Gtk::Widget *> skizzen;
        bool baut = true;
        bool laeuft = false, nochmal = false;
        /// Alles nachführen. Ändert dabei ein Feld seinen Wert (value_changed → alle), läuft es
        /// danach noch einmal statt verschachtelt – höchstens dreimal, damit zwei Felder, die
        /// sich gegenseitig setzen, nie endlos kreisen (Peter 22.10.: Hänger beim Öffnen).
        void alle()
        {
            if (baut) return;
            if (laeuft) {
                nochmal = true;
                return;
            }
            laeuft = true;
            for (int runde = 0; runde < 3; ++runde) {
                nochmal = false;
                for (size_t i = 0; i < neu.size(); ++i) neu[i]();
                if (!nochmal) break;
            }
            laeuft = false;
            for (auto s : skizzen) s->queue_draw();
        }
    };

    static Gtk::Label *hinweis_label(std::string const &t);

    static Gtk::ComboBoxText *combo_zeile(Gtk::Grid &g, int zeile, std::string const &text);

    // ------------------------------------------------------- Tasche: Schritte, Karten, Vorschau
    /// Ablauf-Eintrag (ops) eines Teils der letzten Rechnung: «id», «id#raeumen» …
    bj::object const *ablauf_op(std::string const &key);

    // ------------------------------------------------------- Vorschub in mm/min
    /// Vorschub-Feld in mm/min: folgt dem Werkzeug (und dessen Werten im
    /// gewählten Werkstoff), bis man selbst etwas eintippt
    struct VorschubFeld {
        Gtk::SpinButton *spin = nullptr;
        Gtk::Label *hinweis = nullptr;
        bool folgt = true, setzt = false;
        std::function<double()> standard;
        void nachfuehren()
        {
            // auf die Stellen des Feldes runden: sonst setzt update() 1901 aus dem Text «1901», das
            // Nachführen wieder 1900,6 – und beide wechseln sich endlos ab (Hänger, 22.10.2026)
            double f10 = std::pow(10.0, spin->get_digits());
            double st = std::round(standard() * f10) / f10;
            if (folgt && std::abs(spin->get_value() - st) > 0.5 / f10) {
                setzt = true;
                spin->set_value(st);
                setzt = false;
            }
            if (hinweis) hinweis->set_text(folgt ? TR("mm/min · wie das Werkzeug") : TR("mm/min · Werkzeug: ") + (hirch_zoll() ? einheit_zahl(st, 0, Groesse::VORSCHUB, false) : ganz(st)));
        }
        /// zu speichern: nichts (= wie das Werkzeug) oder der eigene Wert
        bj::value wert_json()
        {
            spin->update();
            if (folgt || std::abs(spin->get_value() - standard()) < 0.5) return nullptr;
            return std::round(spin->get_value());
        }
    };

    /// Zeile «Vorschub» bzw. «Vorschub bei Vollnut» in einem Formular-Gitter.
    /// anfang: gespeicherter Wert in mm/min (≤ 0: keiner); alt_prozent: ältere
    /// Zeichnungen speicherten Prozent – daraus der Anfangswert.
    std::shared_ptr<VorschubFeld> vorschub_zeile(Gtk::Grid *g, int r, std::string const &text, double anfang,
                                                 double alt_prozent, std::function<double()> standard);

    struct TaschenFelder {
        Gtk::Entry *titel = nullptr;
        // Form und Tiefe
        Gtk::SpinButton *tiefe = nullptr, *aufmass = nullptr;
        Gtk::SpinButton *starttiefe = nullptr;          // «Beginnt bei Tiefe»
        double ab() const { return starttiefe ? std::max(0.0, starttiefe->get_value()) : 0.0; }
        /// Weg, auf dem zugestellt wird (Start- bis Endtiefe)
        double weg() const { return std::max(0.01, (tiefe ? tiefe->get_value() : 1.0) - ab()); }
        Gtk::ComboBoxText *richtung = nullptr, *vwand = nullptr, *tiefe_ab = nullptr;
        Gtk::SpinButton *tiefe_ab_mm = nullptr;   // «bis auf das 3D-Modell»: tiefer (+) / weniger tief (−)
        Gtk::CheckButton *dogbone = nullptr;
        // Werkzeug der Tasche (Ausräumen / Restmaterial)
        Gtk::ComboBoxText *werkzeug = nullptr, *strategie = nullptr, *eintauchen = nullptr;
        Gtk::SpinButton *zust = nullptr, *bahn = nullptr, *alast = nullptr, *rwinkel = nullptr, *vollnut = nullptr,
                        *vprozent = nullptr;
        // Räumen
        Gtk::ComboBoxText *r_werkzeug = nullptr, *r_strategie = nullptr, *r_eintauchen = nullptr;
        Gtk::SpinButton *r_zust = nullptr, *r_bahn = nullptr, *r_alast = nullptr, *r_rwinkel = nullptr,
                        *r_vollnut = nullptr, *r_vprozent = nullptr;
        // Schlichten
        Gtk::ComboBoxText *s_werkzeug = nullptr, *s_richtung = nullptr;
        Gtk::SpinButton *s_zugabe = nullptr, *s_zust = nullptr, *s_vprozent = nullptr;
        double s_zust_ganz = -1;     // «ganze Tiefe»: diese Zahl zeigt das Feld; unverändert bleibt der Wert leer
        std::shared_ptr<ZustellArt> zart;               // Art der Zustellungen beim Ausräumen
        std::map<std::string, Gtk::Label *> kurz;       // je Abschnitt: was eingestellt ist (eine Zeile)
        Gtk::Label *zeit_label = nullptr;               // rechts: Zeit oder «so nicht fräsbar»

        Gtk::Stack *seiten = nullptr;
        std::map<std::string, Gtk::Button *> schritte;
        std::map<std::string, Gtk::Widget *> abschnitte;          // alles auf einer Seite
        std::function<void(std::string const &)> hinrollen;
        std::string suche;
        std::function<void(Gtk::Widget *, std::string &)> texte;
        Gtk::Button *neu_raeumen = nullptr, *neu_schlicht = nullptr, *ok = nullptr, *zuweisen = nullptr;
        bool ungueltig = false;
        Gtk::Label *r_fehler = nullptr;
        std::string wahl = "haupt";
        bool raeumen = false, schlichten = false, baut = true;
        std::shared_ptr<VorschubFeld> v_haupt, vn_haupt, v_raeum, vn_raeum, v_schl;
        Gtk::SpinButton *vfaktor = nullptr;     // Faktor aus der Zeitleiste (%)
        std::vector<std::function<void()>> neu;
        std::vector<Gtk::Widget *> bilder;
        bool laeuft = false, nochmal = false;
        /// Alles nachführen. Ändert dabei ein Feld seinen Wert (value_changed → alle), läuft es
        /// danach noch einmal statt verschachtelt – höchstens dreimal, damit zwei Felder, die
        /// sich gegenseitig setzen, nie endlos kreisen (Peter 22.10.: Hänger beim Öffnen).
        void alle()
        {
            if (baut) return;
            if (laeuft) {
                nochmal = true;
                return;
            }
            laeuft = true;
            for (int runde = 0; runde < 3; ++runde) {
                nochmal = false;
                for (size_t i = 0; i < neu.size(); ++i) neu[i]();
                if (!nochmal) break;
            }
            laeuft = false;
            for (auto b : bilder) b->queue_draw();
        }
    };

    /// Vorschau der Bearbeitung aus der letzten Rechnung: Fräsbahnen mit
    /// Schnittbreite, je Teil (Räumen, Tasche/Haupt, Schlichten …) eigene Farbe;
    /// wahl() nennt den hervorgehobenen Teil («form»/leer: alle)
    using BahnDaten = std::map<std::string, bj::object>;   // Teil -> {durchmesser, zuege}

    /// Vorschau im Dialog, die den Werten im Fenster folgt: nach einer kurzen Pause
    /// wird nur diese Bearbeitung im Hintergrund neu gerechnet
    struct LiveVorschau {
        std::shared_ptr<BahnDaten> daten;
        Gtk::DrawingArea *bild = nullptr;
        Gtk::Label *hinweis = nullptr;
        Gtk::Button *knopf = nullptr;
        std::function<void(bj::object &)> auslesen;
        sigc::connection timer;
        unsigned gen = 0;
        bool lebt = true, rechnet = false;
        std::string letzte;
        std::string id;
        int index = -1;
    };

    /// «Geändert seit dem Öffnen» in aend_box; gibt die Anzahl geänderter Werte zurück
    int aenderungen_zeigen(Gtk::Box *aend_box, bj::object const &start, bj::object const &jetzt);
    /// Änderungen als {Name, alt, neu} (für die Liste im Dialog und die Bearbeitungshistorie)
    std::vector<std::array<std::string, 3>> aenderungen_liste(bj::object const &start, bj::object const &jetzt);
    /// Text für «Rückgängig» / Bearbeitungshistorie: «Senkung: Tiefe 0,7 → 0,8; Zustellung 0,6 → 0,5»
    std::string aenderung_text(std::string const &name, bj::object const &start, bj::object const &jetzt);

    void live_planen(std::shared_ptr<LiveVorschau> L);

    void live_rechnen(std::shared_ptr<LiveVorschau> L, bool erzwingen);

    /// Vorschau rechts im Dialog samt Hinweiszeile und Knopf «Vorschau aktualisieren»
    std::shared_ptr<LiveVorschau> live_vorschau(Gtk::Box *ziel, std::string const &id, int index,
                                               std::function<std::string()> wahl, std::string const &haupt_hex,
                                               std::string const &start_text);

    Gtk::DrawingArea *bahnen_bild(std::string const &id, std::function<std::string()> wahl, bool *leer = nullptr,
                                  std::string const &haupt_hex = {}, std::shared_ptr<BahnDaten> *daten_aus = nullptr);

    void tasche_dialog(int index, bj::object b, std::string const &teil = {});
    /// 3D-Schruppen und 3D-Schlichten (hirch-3d.cpp)
    void relief_dialog(int index, bj::object b);
    void boeden_waende_dialog(int index, bj::object b);
    void restmaterial_anlegen(std::string const &bid, double durchmesser);
    /// Umspannen in der Zeichnung einschalten (Abstand wie in den Einstellungen) und neu rechnen (Peter 09.11.)
    void umspannen_einschalten();
    void relief_zweiseitig(bool an, std::string const &achse);
    /// beim Einlesen eines 3D-Modells: 3D-Schruppen und 3D-Schlichten mit Werkzeugen aus «Meine Werkzeuge»
    void relief_anlegen(double tiefe, std::function<void(std::string const &)> fertig);
    std::vector<std::string> merkmal_bearbeitungen(std::string const &json);
    /// Einstellungen › Maschine › «Spindel und Halterung …» (hirch-3d.cpp)
    void halterung_dialog();
    /// Messkorrektur: Testteil in X und Y ausmessen (hirch-messkorrektur.cpp). fester: nur für diesen Fräser
    /// rechnen und den neuen Wert je Seite zurückgeben (der Werkzeug-Dialog speichert ihn); sonst beim gewählten
    /// Fräser (vorwahl: "slot:A" oder "db:<id>") ablegen.
    std::optional<double> messkorrektur_dialog(std::string const &vorwahl = {}, bj::object const *fester = nullptr,
                                               bj::object *info_aus = nullptr, Gtk::Window *eltern = nullptr);
    /// Stelle, an der Spindel oder Halterung streift (Ergebnis «kollisionen»), rot auf der Zeichnung
    void kollisionen_zeichnen(SPDesktop *desk);
    void umspannen_zonen_zeichnen(SPDesktop *desk);   // Zonen der Spannung 2 (hirch-panel.cpp)
    Gtk::Widget *umspannen_zeile(int seite);          // «⟳ Umspannen» im Ablauf
    std::string rechnung_fuer_3d();
    // 3D-Dialoge im Stil der 2D-Dialoge (hirch-3d.cpp, Peter 26.10.)
    std::pair<Gtk::Box *, Gtk::Box *> geruest_3d(Gtk::Dialog &dlg, bj::object const &b, std::string const &unterzeile);
    void rechte_spalte_3d(Gtk::Box *rechts, std::string const &id, Gtk::Widget *skizze_w, Gtk::Label *zeit_l);
    void teil_vergessen();                     // Plattenwechsel: Teil der alten Platte aus den 3D-Ansichten

    void bearbeitung_dialog(int index, std::string const &teil = {});

    // ------------------------------------------------------- Gravurtext
    /// Gruppe mit hirch:gravurtext in der Auswahl (oder ein Pfad darin)
    SPItem *gravur_auswahl();
    void gravur_oder_text();
    void gravur_umwandeln(SPItem *gruppe);

    /// Breite der Rille der ersten Gravur-Bearbeitung (V-Fräser: aus Tiefe und Winkel)
    double rille_von(bj::object const &b);
    // Rille der Gravuren und Abstand der Einlinientexte (hirch-rille.cpp)
    struct GravurRille { std::string id; guint32 farbe = 0; double breite = 0; };
    static constexpr double RILLE_ANTEIL = 0.05;   // Bezugsrille der Schriften im Textwerkzeug (einlinien.py)
    std::vector<GravurRille> gravur_rillen();
    std::pair<double, guint32> rille_fuer(SPItem *it, std::vector<GravurRille> const &gr);
    void rillen_spaeter();
    void rillen_zeichnen();
    void zuschlaege_setzen();
    std::vector<CanvasItemPtr<CanvasItemBpath>> rillen;
    sigc::connection rillen_takt;

    struct Gravur {
        Gtk::TextView *text = nullptr;
        Gtk::ComboBoxText *schrift = nullptr, *ausrichtung = nullptr, *linien = nullptr, *abstand = nullptr,
                          *bearbeitung = nullptr;
        Gtk::SpinButton *hoehe = nullptr, *zeilen = nullptr, *linienabstand = nullptr, *luft = nullptr,
                        *rille = nullptr;
        Gtk::Button *entfernen = nullptr, *ok = nullptr;
        Gtk::Label *info = nullptr;
        Gtk::DrawingArea *bild = nullptr;
        std::vector<std::vector<double>> striche;
        std::vector<double> box;               // Ausdehnung um den Bezugspunkt (mm, y nach oben)
        double breite = 0, hoehe_mm = 0;
        std::string schrift_wunsch;            // nach Import oder aus der Zeichnung
        std::map<std::string, std::pair<std::string, bool>> schriften;   // Name → (Art, eigen)
        std::map<std::string, std::string> farben;                       // Bearbeitung → Farbe
        int anfrage = 0, erhalten = -1;
        bool zu = false, baut = true;
        sigc::connection warten;
    };

    bj::object gravur_werte(Gravur &g);

    void gravur_rechnen(std::shared_ptr<Gravur> g);

    /// Schrift importieren: Datei wählen, bei einer Zeichentafel nach dem Namen fragen
    void schrift_importieren(Gtk::Window &eltern, std::shared_ptr<Gravur> g);
    void schrift_import_lauf(Gtk::Window &eltern, std::function<void(std::string const &)> info,
                             std::function<void(std::string const &)> fertig);

    void zeichentafel_zeigen(Gtk::Window &eltern);


    /// Spannmittel (Pratzen, Zwingen, Klötze) einzeichnen: Sperrflächen mit Höhe.
    /// Eilgänge gehen darüber hinweg, Fräswege hinein meldet die Prüfung.
    void spannmittel_dialog();

    // ------------------------------------------------------- Postprozessoren
    struct PostStand {
        bj::array liste;                 // alle Vorlagen (mit mitgeliefert/geaendert/entfernt)
        std::string key;                 // im Formular gezeigt
        bool laden = false, schmutzig = false, lebt = true;
        Gtk::ListBox *lb = nullptr;
        Gtk::Entry *name = nullptr, *endung = nullptr, *beschr = nullptr;
        Gtk::CheckButton *eine = nullptr, *wpos = nullptr, *zsicher = nullptr, *leer = nullptr;
        Gtk::SpinButton *wx = nullptr, *wy = nullptr, *wz = nullptr;
        // was die Steuerung kann
        Gtk::CheckButton *boegen = nullptr, *zyklen = nullptr;
        Gtk::Entry *kommentar = nullptr, *zy[4] = {};          // Bohrzyklus: entspanen, einfach, verweilen, ende
        Gtk::SpinButton *nummern = nullptr;
        Gtk::Widget *zy_grid = nullptr;
        Gtk::TextView *felder[4] = {};
        Gtk::TextView *beispiel = nullptr;
        Gtk::Notebook *nb = nullptr;
        Gtk::Widget *formular = nullptr;
        Gtk::Label *status = nullptr;
        Gtk::Button *b_entf = nullptr, *b_zur = nullptr;
        sigc::connection beispiel_zeit;
    };

    static bj::object const *post_finden(PostStand const &st, std::string const &k);

    static std::string zeilen_text(bj::object const &o, char const *feld);

    static bj::object post_formular(PostStand const &st);

    void post_liste_bauen(std::shared_ptr<PostStand> st);

    void post_laden(std::shared_ptr<PostStand> st, std::string const &k);

    static void post_abhaengig(std::shared_ptr<PostStand> st);

    void post_beispiel(std::shared_ptr<PostStand> st);

    /// geändert → speichern; danach(fertig) wird mit der neuen Liste aufgerufen
    void post_speichern(std::shared_ptr<PostStand> st, std::function<void(std::string const &)> danach = {});

    void post_befehl(std::shared_ptr<PostStand> st, std::string const &befehl, std::string const &k);

    /// «Fräsen → Postprozessoren …»: eigene anlegen, ändern, entfernen.
    /// danach() erhält die wählbaren [Schlüssel, Name].
    void postprozessoren_dialog(std::string const &zeigen = {},
                                std::function<void(std::vector<std::pair<std::string, std::string>> const &)> danach = {});

    void postprozessoren_zeigen(std::shared_ptr<PostStand> st,
                                std::function<void(std::vector<std::pair<std::string, std::string>> const &)> danach);

    /// Haltestege von Hand: Markierungen auf die ausgewählten Linien legen
    // Haltestege als Marken je Bearbeitung (Entwurf B, Peter 23./24.10.): hirch:steg="<Id>", die
    // gerechnete Lage steht in hirch:steg-lage – so sieht man, ob eine Marke von Hand verschoben wurde
    std::vector<SPItem *> steg_marken(std::string const &bid);
    int steg_verschoben(std::string const &bid);
    Inkscape::XML::Node *steg_ebene();
    SPItem *steg_marke(Geom::Point const &mitte, double breite_mm, std::string const &bid);
    /// gleichmässig verteilen (Rechenteil «stege»); b: die Bearbeitung mit den Werten aus dem Dialog.
    /// fertig(Anzahl Marken, Hinweis); Anzahl < 0: Fehler
    void stege_verteilen(bj::object const &b, std::function<void(int, std::string const &)> fertig);
    void stege_entfernen_von(std::string const &bid);
    void stege_breite_setzen(std::string const &bid, double breite_mm);
    bool stege_rueckfrage(Gtk::Window *eltern, int verschoben);
    /// nach dem Rechnen: Stege der Konturen auf «auto» (Ergebnis `stege_auto`) als Marken setzen, auf «manuell»
    void stege_auto_uebernehmen();
    bool marken_uebernommen = false;
    void stege_nach_dialog(bj::object const &b, std::string const &modus, int anzahl_vert, double breite_vert,
                           bool durch);
    /// Leiste «Haltestege setzen»: Konturdialog der Auswahl beim Abschnitt Haltestege
    void stege_leiste();
    /// Passlöcher als Kreise auf Seite 1 abgleichen: gezogene, gelöschte oder kopierte Löcher
    /// übernehmen (pass_lage «frei»), Partner gespiegelt nachführen, zu nahe Löcher orange
    void passloecher_abgleichen(bool nach_analyse);
    void pass_deko_zeichnen(std::vector<Geom::Point> const &prim, double B, double H, bool um_y, double r,
                            std::function<bool(Geom::Point)> const &gewarnt);
    std::vector<Geom::Point> pass_primaer(double B, double H);
    bool pass_sperre = false;

    /// Neuestes Programm-Archiv (~/Projekte/hirch_CAM, ~/Downloads) installieren
    void aktualisieren();
    /// Meldung mit «Ordner wählen …» (gemerkt unter pref); true = neu suchen
    bool ordner_waehlen(std::string const &titel, std::string const &text, std::string const &pref);


    /// start: dieses Werkzeug zuerst zeigen; neu: gleich ein neues Werkzeug anlegen
    /// (Text eines Eintrags im «+ Neu»-Menü, z. B. «Schaftfräser»)
    /// aufraeumen: Plätze, die keine Bearbeitung benutzt, beim Übernehmen weglassen (nur wenn keine Bearbeitung
    /// offen ist – aus dem Ablauf oder dem Menü geöffnet)
    void werkzeuge_dialog(std::string const &start_key = {}, std::string const &neu_text = {}, WzWahl *wahl = nullptr,
                          bool aufraeumen = false);
    /// Platz → «n · Bearbeitung (Räumen)» … für alle Bearbeitungen, die ihn benutzen
    std::map<std::string, std::vector<std::string>> werkzeug_benutzung();
    double neu_durchmesser = 0;   ///< werkzeuge_dialog(…, neu_text): Ø des neuen Werkzeugs (0 = Vorgabe)

    /// Reiter «Werkzeuge»: Werkzeuge der Zeichnung als Karten
    void werkzeug_liste(Gtk::Box *ziel);

    /// Beschriftete Fräsfarben (Statusleiste): je Bearbeitung ein Knopf, Fräsarten ohne
    /// Bearbeitung mit ihrem Grundton, dazu «Nicht fräsen»
    void farbleiste_fuellen(Gtk::Box *ziel);

    /// Reiter «Werkzeuge» (falls offen) neu füllen
    void werkzeug_reiter_auffrischen();

    /// modell: beim Start gleich dieses 3D-Modell vorwählen (z. B. das Testteil)
    void material_dialog(bool beim_start = false, std::string const &modell = {});

    /// Zweite Seite der Zeichnung (= Seite 2 des Werkstücks) rechts neben der ersten anlegen
    static void seite2_anlegen(SPDocument *d);

    /// Seite «Vorrichtung» links vor Seite 1 anlegen: Material des Werkstücks + 50 mm ringsum
    static void vorrichtung_anlegen(SPDocument *d, double b_mm, double h_mm, double rand_mm = 50);

    // ------------------------------------------------------- Ausgabe
    std::string vorgeschlagener_name();

    /// Kurzer Dateiname für Steuerungen wie den RichAuto-Regler: nur A–Z 0–9 _, höchstens max Zeichen
    /// (Umlaute als AE/OE/UE, Leerzeichen und Zeichen dazwischen als _)
    static std::string kurz_stamm(std::string const &s, std::size_t max);

    /// Höchstens so viele Zeichen vor der Endung (0: Name wie gewählt) – aus der letzten Rechnung
    int kurzname();

    /// Vorschlag für den Dateinamen ohne Endung (bei kurzen Namen schon gekürzt)
    std::string ziel_vorschlag();

    /// Wert aus den Maschinen-Einstellungen (alle Zeichnungen)
    double ma_wert(char const *k, double d);
    std::string ma_wert_text(char const *k);

    /// Prüfliste vor dem Speichern (abhaken); false = abgebrochen
    bool pruefliste_ok();

    /// Nach dem Speichern: Dateien mit Fräser und Zeit, Einrichteblatt, Ordner
    void gespeichert_dialog(std::vector<std::string> const &fertig, std::string const &ordner_pfad, bool stick);

    /// Luftfräsen: alles höher rechnen und speichern (auf_stick: direkt auf den Stick)
    void luftfraesen(bool auf_stick);
    bool luft_modus = false;

    void speichern();

    /// Fehler der Prüfung vor dem Speichern bestätigen lassen
    bool fehler_bestaetigt();

    /// Ausgabedateien nach zordner kopieren, umbenannt auf «stamm…»
    bool dateien_kopieren(std::string const &zordner, std::string const &stamm, std::vector<std::string> &fertig);

    // ------------------------------------------------------- Vorlagen
    static std::string vorlagen_ordner();

    /// Aktuelle Zeichnung als Vorlage (Material, Nullpunkt, Bearbeitungen, Texte an ihrem Platz)
    void als_vorlage();
    void gespiegelt_speichern();

    /// Neue Zeichnung aus einer Vorlage; Platzhalter werden ausgefüllt
    void aus_vorlage();

    /// {JAHR}, {DATUM}, {MONAT}, {TAG} in allen Texten ersetzen
    static void platzhalter_fuellen(SPDocument *d);

    // ------------------------------------------------------- Frühere Fassungen
    void versionen_dialog();

    // ------------------------------------------------------- Maschine (Reiter Einstellungen)
    void maschine_abschnitt(Gtk::Box *ziel);
    /// Einstellungen › Werkstoffe: je Werkstoff ein Schieber «langsamer – schneller» mit Ergebnis (Peter 06.11.)
    void werkstoffe_abschnitt(Gtk::Box *ziel);
    /// Einstellungen › «So fräse ich»: Vorgaben für neue Bearbeitungen (Peter 07.11., hirch-vorgaben.cpp)
    void vorgaben_abschnitt(Gtk::Box *ziel);
    /// B-25: Karten der Maschinen oben in «Einstellungen › Maschine» (hirch-maschinen.cpp)
    void maschinen_karten(Gtk::Box *ziel);
    std::string maschine_name_fragen(std::string const &titel, std::string const &vorgabe,
                                     std::string const &frage = {});
    /// Neue Zeichnung: bei mehr als einer Maschine fragen, für welche (danach: weiter mit dem Einrichten)
    void maschine_waehlen(std::function<void()> danach);
    /// Maschine der Zeichnung wechseln: Fräser vergleichen, fehlende auf Wunsch übernehmen (Konzept B-25)
    void maschine_wechseln(std::string const &ziel, std::function<void()> danach = {});
    void maschine_knopf_auffrischen();
    void maschine_werte_nachfuehren(bj::object const &r);
    bool ersteinrichtung_noetig();
    void ersteinrichtung();     // erster Start: Sprache, Einheit, Maschine, Fräser (Peter 08.11.)
    /// Rückmeldung nach dem Fräsen (hirch-rueckfrage.cpp)
    void rueckfrage_merken();
    void rueckfrage_pruefen();
    std::set<std::string> rueckfrage_gefragt;
    /// In der Bearbeitung: was von «So fräse ich» abweicht, mit «Als Vorgabe merken». art: Fräsart der Vorgaben
    /// (Tasche, Kontur …); jetzt: die Werte im Dialog. Gibt die Zahl der Abweichungen zurück. Auch für 3D-Dialoge.
    int vorgaben_zeigen(Gtk::Box *box, std::string const &art, bj::object const &jetzt);
    // Einstellungen › Maschine wurde vor der ersten Analyse gefüllt (ohne Steuerungen und Maschinenwerte): mit der
    // Analyse neu füllen – sonst stünde dort nur «Vorgabe des Programms» und «Übernehmen» schriebe Vorgabewerte
    GtkWidget *maschine_ziel = nullptr;     // schwacher Zeiger (g_object_add_weak_pointer)
    bool maschine_ohne_analyse = false;
    std::string maschine_werte_id;          // Maschine, deren Werte in analyse["maschine"] stehen
    std::function<void()> maschine_karten_neu;   // Karten in Einstellungen › Maschine neu zeigen (falls offen)
    void werkzeuge_exportieren(Gtk::Label *info);   ///< «Meine Werkzeuge», Maschine, eigene Postprozessoren
    void werkzeuge_importieren(Gtk::Label *info);

    bool vorrichtung_an_einst();

    // ------------------------------------------------------- Spannvorrichtung
    /// Ausgewählte Objekte des Werkstücks grau dorthin auf die Vorrichtung kopieren, wo es liegt
    void umriss_kopieren();

    // ------------------------------------------------------- Zeichnung aufräumen
    /// Python blockierend (kurze Rechnungen, deren Ergebnis sofort gebraucht wird)
    bool python_jetzt(std::vector<std::string> args, std::string &aus, std::string &fehler);

    std::set<SPDocument *> import_geprueft;

    /// Dateityp, nach dessen Import aufgeräumt wird (leer: keiner)
    static std::string import_endung(std::string const &pfad);

    /// Doppelte Linien entfernen, Stücke verbinden, offene Pfade schliessen.
    /// endung: Dateityp beim Import (dxf, ai, pdf …), leer beim Aufruf als Werkzeug.
    /// Bereich: die Auswahl (nach dem Import: das Eingefügte) – ohne Auswahl die ganze Zeichnung.
    void aufraeumen(std::string const &endung, bool nach_import);

    static std::string zahl_text(double v);

    // ------------------------------------------------------- Vervielfältigen und Verschachteln
    /// Teile lesen (Auswahl oder alles auf der ersten Platte), dann den Dialog zeigen
    void verschachteln();
    void zusatz_oeffnen(std::string const &datei);

    void verschachteln_dialog(bj::object info, std::string const &svg);

    /// Ergebnis übernehmen: neue Seiten (Platten), Kopien anlegen, alles an seinen Platz
    void verschachteln_anwenden(bj::object const &r);

    // ------------------------------------------------------- Wiederaufnahme
    std::string name_zusatz;    // beim Speichern einer Wiederaufnahme: «_ab_…»
    /// zuletzt gespeichert (Windows-Befund 29.10.): Quelldatei → (gewählter Name, Änderungszeit der Quelle) –
    /// die Zusammenfassung nennt den gewählten Namen, solange die Quelle nicht neu gerechnet ist
    std::map<std::string, std::pair<std::string, gint64>> gespeichert_als;
    std::string anzeige_name(std::string const &quelle) const;
    std::string steuerung_name(std::string const &datei);
    std::vector<std::string> ziel_namen(std::string const &stamm);

    /// Nach einem Abbruch: Programm ab einer Bearbeitung oder Zeile neu rechnen und speichern
    void ab_hier_ausgeben(std::string const &ab_json, std::string const &wo);

    void ab_speichern(bj::object r, std::string const &wo);

    // ------------------------------------------------------- USB-Stick
    /// Eingesteckte Wechseldatenträger (USB-Stick, Speicherkarte)
    std::vector<Glib::RefPtr<Gio::Mount>> sticks();

    void stick_knopf_auffrischen();

    void auf_stick();

    // ------------------------------------------------------- Laufzeit je Fräser
    static std::string laufzeit_schluessel(bj::object const &w);

    /// Nach dem Speichern: Fräszeit je Werkzeug dazuzählen (Einstellungen, für alle Zeichnungen)
    void laufzeit_zaehlen();

    double laufzeit(bj::object const &w);

    void vorschau();
    /// Blatt zum Fotografieren: Material, Nullpunkt, Werkzeuge mit Drehzahl, Halts
    void zusammenfassung();

    void vorschau_laden();
    VorschauDaten vorschau_daten();   // für Vorschau und 3D-Ansicht im Hauptfenster (hirch-ausgabe.cpp)
};

} // namespace Inkscape::UI::Dialog

#endif
