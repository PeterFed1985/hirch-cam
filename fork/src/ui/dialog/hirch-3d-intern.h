// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: 3D – was die Dateien hirch-3d*.cpp untereinander brauchen. Nur für diese Dateien.
 * Alles steht im Namensraum hirch3d (bis 29.10. ein unbenannter Namensraum in hirch-3d.cpp).
 *
 * Aufteilung (29.10.2026, nur verschoben):
 *   hirch-3d.cpp          Reiter «3D» (Hirch3D::Impl) und seine Anschlüsse
 *   hirch-3d-grund.cpp    Material, Rechenkern, Modell in der Zeichnung, Netz, Kamera-Mathe, Szene, Einfügen
 *   hirch-3d-ansicht.cpp  Ansicht: Zeichnen, Kamera, Puffer, Glättung, Regelung
 *   hirch-3d-einlesen.cpp Dialog «3D-Modell einlesen»
 *   hirch-3d-anlegen.cpp  Bearbeitungen aus dem Modell anlegen, zweiseitig, Masse lesen, Nullpunkt am Block
 *   hirch-3d-dialoge.cpp  Dialoge 3D-Schruppen/-Schlichten, Böden und Wände, Halterung; Anschluss ans Panel
 *   hirch-3d-teil.cpp     gefrästes Teil: Bewegungen, Netz (RTIN), Kacheln (TeilGL), Gewinde, Vergleich, Spannmittel
 *   hirch-3d-fenster.cpp  3D-Ansicht im Hauptfenster: Bedienung, Abspielen, Zeitleiste, Halt-Karte, Fräser
 */
#ifndef SEEN_HIRCH_3D_INTERN_H
#define SEEN_HIRCH_3D_INTERN_H

#include "ui/dialog/hirch-3d.h"
#include "ui/dialog/hirch-intern.h"

#include <array>
#include <atomic>
#include <functional>
#include <thread>
#include <tuple>
#include <optional>
#include <cstring>
#include <fstream>
#include <epoxy/gl.h>
#include <glibmm/base64.h>
#include <gtkmm/frame.h>
#include <gtkmm/glarea.h>
#include <gtkmm/overlay.h>
#include <gtkmm/togglebutton.h>

#include "object/sp-defs.h"
#include <set>
#include "object/sp-item-group.h"
#include "object/sp-shape.h"
#include "object/sp-text.h"

namespace Inkscape::UI::Dialog {
namespace hirch3d {

char const *const RELIEF = "hirch:relief";
char const *const RELIEF2 = "hirch:relief2";   // Unterseite des Modells auf Seite 2 der Zeichnung (nur Anzeige)
char const *const DATEN = "hirch:reliefdaten";

// ---------------------------------------------------------------------------
// Material und Masse

struct Material {
    double b = 300, h = 200, d = 18;
    Geom::Rect seite;           // Zeichnungskoordinaten (y nach unten)
    Geom::Rect seite2;          // zweiseitig: die Seite 2 derselben Platte (sonst = seite)
    bool hat2 = false;
};

/// Material der Platte, auf der «auf» liegt (sonst der im Panel gewählten Platte)
Material material(SPDocument *doc, SPItem *auf = nullptr);

inline std::string mm(double v, int stellen = 1) { return zahl(v, stellen); }

/// B-22 (Peter 03.11.: unbemerkt verstellte Werte haben schon zu Fehlern geführt): über Zahlenfeldern und
/// Auswahllisten ändert das Mausrad nie einen Wert – auch nicht nach einem Klick ins Feld; es rollt die Seite.
/// Für alle Felder unter «w», auch später hinzugekommene (Behälter melden neue Kinder).
inline void rad_sperren(Gtk::Widget &w)
{
    if (dynamic_cast<Gtk::SpinButton *>(&w) || dynamic_cast<Gtk::ComboBox *>(&w)) {
        if (w.get_data("hirch-rad")) return;
        w.set_data("hirch-rad", (void *)1);
        w.add_events(Gdk::SCROLL_MASK);
        auto *p = &w;
        w.signal_scroll_event().connect([p](GdkEventScroll *) {
            g_signal_stop_emission_by_name(p->gobj(), "scroll-event");
            return false;                       // weiter an die Eltern: die Seite rollt
        }, false);
        return;
    }
    if (auto c = dynamic_cast<Gtk::Container *>(&w)) {
        if (!w.get_data("hirch-rad")) {
            w.set_data("hirch-rad", (void *)1);
            c->signal_add().connect([](Gtk::Widget *k) { if (k) rad_sperren(*k); });
        }
        for (auto k : c->get_children()) rad_sperren(*k);
    }
}

// ---------------------------------------------------------------------------
// Rechenkern (Python) asynchron: python3 -m hirch_cam.dokument relief …

using Antwort = std::function<void(bool ok, bj::object const &erg, std::string const &fehler)>;

std::shared_ptr<bool> kern(std::vector<std::string> args, std::string const &ordner, Antwort fertig);
std::string neuer_ordner();
void ordner_loeschen(std::string const &ordner);
std::string warnungen(bj::object const &erg);
std::string hinweise(bj::object const &erg);
std::string datei(bj::object const &erg, char const *k);
double liste_zahl(bj::object const &o, char const *k, std::size_t i, double d = 0);
std::string art_name(std::string const &art);

// ---------------------------------------------------------------------------
// Modell in der Zeichnung

bj::object relief_info(SPItem const *item);
void reliefs_sammeln(SPObject *o, std::vector<SPItem *> &aus);
std::vector<SPItem *> reliefs(SPDocument *d);
XML::Node *daten_knoten(SPDocument *d, std::string const &id);
std::string knoten_text(XML::Node *n);
bool daten_schreiben(SPDocument *d, bj::object const &info, std::string const &pfad);
std::string png_uri(std::string const &pfad);
Geom::Affine modell_zu_material(SPItem *bild, bj::object const &info, Material const &m, double f);
Geom::Affine doc_zu_material(Material const &m, double f);
std::array<Geom::Point, 4> ecken_mm(SPItem *it, Material const &m, double f);
bj::array punkte_json(std::array<Geom::Point, 4> const &p, int n = 4);
bj::array bild_ecken(SPItem *bild, Material const &m, double f);

char const *const STEG3D = "hirch:steg3d";

bj::object steg_werte(SPItem const *it);
void stege_sammeln_in(SPObject *o, std::vector<SPItem *> &aus);

// ---------------------------------------------------------------------------
// Netz für die 3D-Ansicht (netz.bin vom Rechenkern)

struct Netz {
    int nx = 0, ny = 0;
    float zelle = 1, x0 = 0, y0 = 0;
    std::vector<float> oben, unten;
};

bool netz_lesen(std::string const &pfad, Netz &n);
void netz_bauen(Netz const &n, Geom::Affine const &lage, double zoff, std::vector<float> &ecken,
                std::vector<unsigned> &index);

// ---------------------------------------------------------------------------
// Mathe für die Kamera (spaltenweise wie OpenGL)

using M4 = std::array<float, 16>;

M4 mal(M4 const &a, M4 const &b);
M4 perspektive(double fov_grad, double seiten, double nah, double fern);

struct V3 {
    double x = 0, y = 0, z = 0;
};
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline V3 kreuz(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double punkt(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 norm(V3 a)
{
    double l = std::sqrt(punkt(a, a));
    return l > 0 ? a * (1 / l) : a;
}

M4 blick(V3 auge, V3 ziel, V3 oben);

/// Kamera für das Teil: Achsen (rechts, oben, zur Kamera hin) und Bildpunkte je Radiant (Detailstufen)
struct Blick {
    V3 rechts{1, 0, 0}, hoch{0, 1, 0}, vor{0, 0, 1};
    double pix = 1000;
};

GLuint shader_bauen(GLenum art, char const *quelle);
/// Quader über einem Viereck (Draufsicht) von z0 bis z1 als Dreiecke mit Normalen
void quader(std::vector<float> &aus, std::array<Geom::Point, 4> const &p, double z0, double z1);

// ---------------------------------------------------------------------------
// Szene der 3D-Ansicht: was gezeigt wird – eine je Zeichenfenster. Der Reiter «3D» füllt sie; jede
// Ansicht (der Reiter, das Hauptfenster über hirch_ansicht_3d_neu) zeigt sie mit eigener Kamera und
// eigenen GL-Puffern. «geaendert» sagt allen Ansichten, welche Teile neu sind.

struct Szene3D {
    enum : unsigned { BLOCK = 1, MODELL = 2, STEG = 4, MARKE = 8, ERG = 16, HELL = 32, MODUS = 64 };
    std::vector<float> ecken, block, linien, steg_ecken, marke_ecken;
    std::vector<unsigned> index;
    // gewählte Flächen (Reiter «3D» › Flächen wählen): Umrisse (Material-mm, Seite 1) je Seite und Art
    // – [0] Seite 1 flach, [1] Seite 1 Wand, [2] Seite 2 flach, [3] Seite 2 Wand; die Dreiecke des
    // Modells darin werden blau (Wände nur die steilen, sonst nur die flacheren)
    std::array<std::vector<std::vector<Geom::Point>>, 4> hell_ringe;
    std::vector<unsigned> hell_index;
    // Fräsergebnis: Oberseite und Unterseite als Gitternetz; je Klasse eine Indexliste
    // (0 grau wie gefräst, 1 orange: bleibt stehen, 2 rot: zu tief)
    std::vector<float> erg_ecken;
    std::array<std::vector<unsigned>, 3> erg_index;
    bool zeige_ergebnis = false, hat_modell = false;
    double B = 300, H = 200, D = 18;
    // Fräsergebnis als Raster (Oberseite und Modell darunter) für den Klick
    struct Raster {
        int nx = 0, ny = 0;
        float x0 = 0, y0 = 0, zelle = 1;
        std::vector<float> oben, modell;
    } raster;
    /// Klick (ohne Ziehen) ins Fräsergebnis: Stelle (x, y), Höhe dort, Modell darunter (NaN: daneben)
    std::function<void(double, double, double, double)> beim_klick;
    /// Klick (ohne Ziehen) auf das Modell: Stelle (x, y, z) auf der Oberfläche, Umschalt gedrückt,
    /// Seite (1: Oberseite des Modells getroffen, 2: Unterseite)
    std::function<void(double, double, double, bool, int)> beim_klick_modell;
    sigc::signal<void(unsigned)> geaendert;
};

/// Szenen je Zeichenfenster (vom Reiter «3D» eingetragen) und Meldung, wenn sich eine ändert
std::map<SPDesktop *, std::shared_ptr<Szene3D>> &szenen();
sigc::signal<void(SPDesktop *)> &szenen_neu();


/// Gefrästes Teil je Fenster: die Daten der letzten Rechnung (wie die Vorschau sie bekommt) und eine
/// Kennung, damit nur bei einer neuen Rechnung neu abgetragen wird
struct TeilDaten {
    VorschauDaten d;
    std::string kennung;
};

std::map<SPDesktop *, std::shared_ptr<TeilDaten>> &teile();
sigc::signal<void(SPDesktop *)> &teile_neu();

// ---------------------------------------------------------------------------
// Ausrichtung und Lage: Bedienelemente <-> JSON

char const *const SEITEN[] = {"+Z", "-Z", "+Y", "-Y", "+X", "-X"};

std::string seite_text(std::string const &s);
bool ist_bild_datei(std::string const &pfad);
Gtk::Label *hinweis_label();
void hinweis_setzen(Gtk::Label &l, std::string const &text, char const *art);
/// Modell in die Zeichnung: Daten in die defs, Schattenbild als <svg:image> in die Mitte des Materials
SPItem *einfuegen(SPDesktop *desk, bj::object const &erg, bj::object const &lage);
/// Wendung Seite 1 → Seite 2 in Dokumentkoordinaten (Material-mm (x, y) auf Seite 1 → gewendet auf Seite 2)
Geom::Affine seite2_wendung(SPDocument *doc, SPItem *auf);
/// B-14: die Bilder der Unterseite (hirch:relief2) liegen immer gewendet unter ihrem Modell – nach jedem
/// Verschieben, Drehen, Skalieren (auch per Zahlenfeld, auch ohne offenen Reiter «3D»). Ändert nur, was abweicht.
void seite2_nachfuehren(SPDocument *doc);

// ---------------------------------------------------------------------------
// 3D-Ansicht: Werkstück als durchsichtiger Block, Modell darin; was herausragt, rot

struct Ansicht {
    Gtk::GLArea bereich;
    std::shared_ptr<Szene3D> sz;
    // die Daten liegen in der Szene (gemeinsam für alle Ansichten des Fensters)
    std::vector<float> &ecken, &block, &linien, &steg_ecken, &marke_ecken, &erg_ecken;
    std::vector<unsigned> &index, &hell_index;
    std::array<std::vector<std::vector<Geom::Point>>, 4> &hell_ringe;
    std::array<std::vector<unsigned>, 3> &erg_index;
    bool &zeige_ergebnis, &hat_modell;
    double &B, &H, &D;
    Szene3D::Raster &raster;
    std::function<void(double, double, double, double)> &beim_klick;
    std::function<void(double, double, double, bool, int)> &beim_klick_modell;
    sigc::connection verbunden;
    // gefrästes Teil (Hauptfenster: aus hirch-abtrag): Ecken x, y, z, nx, ny, nz, r, g, b – nur diese Ansicht
    std::vector<float> teil_ecken;
    std::vector<unsigned> teil_index;
    std::vector<float> gewinde_ecken;           // Formteile Gewinde (x, y, z, nx, ny, nz)
    std::vector<float> fraeser_ecken;           // Fräser beim Abspielen (x, y, z, nx, ny, nz), leer: keiner
    GLsizei n_fr_schneide = 0, n_fr_oben = 0;   // davon zuerst die Schneide, dann Schaft, Schaft über der Mutter,
    GLsizei n_fr_pfeil = 0;                     // zuletzt der Pfeil in Fahrrichtung
    // danach Spannmutter und Spindel (Peter 05.11., halbtransparent); Farbe der Mutter: 0 grau, 1 orange (unter 2 mm
    // zum Material), 2 rot (berührt)
    GLsizei n_fr_mutter = 0, n_fr_spindel = 0;
    int mutter_zustand = 0;
    bool neu_teil = false, zeige_teil = false, neu_fraeser = false;
    GLuint vao_teil = 0, vbo_teil = 0, ebo_teil = 0, vao_gew = 0, vbo_gew = 0, vao_fr = 0, vbo_fr = 0;
    GLsizei n_teil = 0, n_gew = 0, n_fr = 0;
    GLint u_farbig = -1;
    // Linien der Zeichnung auf dem Teil und Fräsbahnen (x, y, z, nx, ny, nz, r, g, b; je zwei Ecken eine Linie)
    std::vector<float> linien_teil, bahn_ecken, eil_ecken;
    bool neu_linien = false, zeige_linien = true, zeige_bahnen = false, zeige_block = true, zeige_stege = true;
    bool zeige_eil = true;                      // Eilgänge rot (wie in der Vorschau)
    // B-16 (Peter 02.11.): Fräsbahnen je Bearbeitung neu (nur die Linien hochladen); Teil ausblenden, damit nur
    // die Bahnen zu sehen sind
    bool neu_bahnen = false, teil_aus = false;
    bool von_unten = false;
    GLuint vao_lin = 0, vbo_lin = 0, vao_bahn = 0, vbo_bahn = 0, vao_eil = 0, vbo_eil = 0;
    GLsizei n_lin = 0, n_bahn = 0, n_eil = 0;
    // Umspannen (Peter 27.10.): Spannmittel grau, zuletzt die Schrauben (dunkel); Sperrzone orange als Fläche
    // (durchsichtig) und Umriss – je (x, y, z, nx, ny, nz)
    std::vector<float> spann_ecken, zone_flaeche, zone_umriss;
    GLsizei n_sp_schraube = 0;
    bool neu_spann = false, zeige_spann = true;
    GLuint vao_sp = 0, vbo_sp = 0, vao_zf = 0, vbo_zf = 0, vao_zl = 0, vbo_zl = 0;
    GLsizei n_sp = 0, n_sp_schr = 0, n_zf = 0, n_zl = 0;
    /// Teil selbst zeichnen (im GL-Kontext, mit Kamera pv, Auge und Licht); danach stellt malen() sein
    /// Programm wieder her
    std::function<void(M4 const &, V3 const &, V3 const &, Blick const &)> teil_malen;
    /// beim Abspielen nur so viele Ecken der Bahnen / Gewinde zeigen (-1: alle)
    GLsizei n_bahn_sicht = -1, n_gew_sicht = -1, n_eil_sicht = -1;
    /// Klick (doppelt: Doppelklick) auf das gefräste Teil bei Bildpunkt (x, y); true: erledigt
    std::function<bool(double, double, bool)> klick_teil;
    /// Rechtsklick ohne Ziehen (Peter 08.11.: Flächen wählen und Klick ins Ergebnis in der grossen Ansicht)
    std::function<void(double, double, GdkEventButton *)> klick_rechts;
    GdkEvent *rk_druck = nullptr;               // letztes Drücken der rechten Taste (Auslöser fürs Menü)
    /// im GL-Kontext vor jedem Bild (z. B. hirch-abtrag rechnen lassen)
    std::function<void()> vor_malen;
    /// beim Drehen über die Kante auf die andere Seite gekommen (true: jetzt von unten)
    std::function<void(bool)> seite_gewechselt;
    /// im GL-Kontext, bevor er endet (eigene Puffer freigeben)
    std::function<void()> aufraeumen_gl;
    bool bereit = false, neu_modell = false, neu_block = true;
    GLuint prog = 0, vao[6] = {0, 0, 0, 0, 0, 0}, vbo[6] = {0, 0, 0, 0, 0, 0}, ebo = 0, ebo_erg[3] = {0, 0, 0}, ebo_hell = 0;
    GLint u_pv = -1, u_licht = -1, u_farbe = -1, u_dicke = -1, u_modell = -1, u_flach = -1;
    GLsizei n_index = 0, n_block = 0, n_linien = 0, n_steg = 0, n_marke = 0, n_erg[3] = {0, 0, 0}, n_hell = 0;
    bool neu_steg = false, neu_marke = false, neu_erg = false, neu_hell = false;
    double az = -122, el = 30, abstand = 0;   // Blick von vorne links; el ganz herum (−180 … 180, Peter 28.10.)
    bool wende_um_y = false;                    // zweiseitig: gewendet um die Y-Achse (sonst um X)
    sigc::connection wende_takt;                // Ansicht wird eben gewendet (animiert)
    double mx = 0, my = 0, px = 0, py = 0;
    bool zieht = false;
    int zieh_knopf = 0;                         // 1: drehen, 2/3: verschieben
    V3 versatz{0, 0, 0};                        // Blickpunkt verschoben (mittlere oder rechte Maustaste, Zoom zur Maus)
    V3 ziel_punkt() const { return V3{B / 2, H / 2, -D / 2} + versatz; }
    /// mm je Bildpunkt in der Ebene des Blickpunkts
    double mm_je_punkt() const
    {
        int h = std::max(1, bereich.get_allocated_height());
        double r = abstand > 0 ? abstand : grund_abstand();
        return 2 * r * std::tan(16.0 * M_PI / 180) / h;
    }

    explicit Ansicht(std::shared_ptr<Szene3D> s = std::make_shared<Szene3D>());

    double grund_abstand() const { return 1.25 * std::sqrt(B * B + H * H + D * D); }

    /// oben auf dem Bildschirm: Ableitung der Augenrichtung nach el – stetig über die Pole, kopfüber −Z
    V3 oben_vektor() const
    {
        double a = az * M_PI / 180, e = el * M_PI / 180;
        return V3{-std::sin(e) * std::cos(a), -std::sin(e) * std::sin(a), std::cos(e)};
    }

    /// von unten gesehen (Licht, Knöpfe oben/unten) – dem Drehen nachführen
    void seite_nachfuehren()
    {
        bool u = std::sin(el * M_PI / 180) < -1e-9;
        if (u != von_unten) {
            von_unten = u;
            if (seite_gewechselt) seite_gewechselt(von_unten);
        }
    }

    /// Ansicht wenden wie das Teil auf der Maschine (Peter 28.10.: Seite 2 von oben sehen, Fräser von oben):
    /// Auge, Oben und Blickpunkt drehen sich 180° um die Wendeachse durch die Mitte des Teils
    void ansicht_wenden(bool animiert = true);

    /// Kamera wie in malen(): Auge und Achsen (vorwärts, rechts, oben)
    void kamera(V3 &auge, V3 &f, V3 &s, V3 &u) const
    {
        double r = abstand > 0 ? abstand : grund_abstand();
        double a = az * M_PI / 180, e = el * M_PI / 180;
        V3 ziel = ziel_punkt();
        auge = ziel + V3{std::cos(e) * std::cos(a), std::cos(e) * std::sin(a), std::sin(e)} * r;
        V3 oben = oben_vektor();
        f = norm(ziel - auge);
        s = norm(kreuz(f, oben));
        u = kreuz(s, f);
    }

    /// Strahl durch den Bildpunkt (x, y)
    V3 strahl(double x, double y, V3 &auge)
    {
        int w = std::max(1, bereich.get_allocated_width()), h = std::max(1, bereich.get_allocated_height());
        V3 f, s, u;
        kamera(auge, f, s, u);
        double t = std::tan(16.0 * M_PI / 180);
        return norm(f + s * ((2 * x / w - 1) * t * double(w) / h) + u * ((1 - 2 * y / h) * t));
    }

    /// Strahl gegen alle Dreiecke des Modells (Möller–Trumbore), der nächste Treffer
    void treffen_modell(double x, double y, bool umschalt = false);

    /// Gewählte Flächen blau zeigen: Ringe je Seite (Material-mm, Seite 1; gerade Anzahl = Loch)
    void hervorheben(std::array<std::vector<std::vector<Geom::Point>>, 4> ringe)
    {
        hell_ringe = std::move(ringe);
        hell_bauen();
    }

    void hell_bauen();

    /// Strahl durch den Bildpunkt (x, y) bis zur Oberseite des Fräsergebnisses (in Schritten von
    /// einer halben Zelle, dann genauer); meldet die Stelle über beim_klick
    void treffen(double x, double y);

    void sicht(char const *art);

    /// Ansehen von oben oder von unten (Unterseite, Seite 2): das Teil wendet sich wie auf der Maschine
    void seite_ansehen(bool unten)
    {
        if (unten != von_unten) ansicht_wenden(true);
    }

    void material(double b, double h, double d);

    void modell(std::vector<float> e, std::vector<unsigned> i)
    {
        ecken = std::move(e);
        index = std::move(i);
        hat_modell = !index.empty();
        sz->geaendert.emit(Szene3D::MODELL);
        hell_bauen();
    }

    /// Haltestege als gelbe Quader (Dreiecke: x, y, z, nx, ny, nz)
    void stege(std::vector<float> e)
    {
        steg_ecken = std::move(e);
        sz->geaendert.emit(Szene3D::STEG);
    }

    /// Fräsergebnis zeigen (statt des Modells) oder zum Modell zurück
    void ergebnis(std::vector<float> e, std::array<std::vector<unsigned>, 3> i)
    {
        erg_ecken = std::move(e);
        erg_index = std::move(i);
        sz->geaendert.emit(Szene3D::ERG);
    }

    void ergebnis_zeigen(bool an)
    {
        zeige_ergebnis = an;
        sz->geaendert.emit(Szene3D::MODUS);
    }

    /// Stelle, an der Spindel oder Halterung streift (rot)
    void marke(std::vector<float> e)
    {
        marke_ecken = std::move(e);
        sz->geaendert.emit(Szene3D::MARKE);
    }

    void kein_modell()
    {
        ecken.clear();
        index.clear();
        hell_index.clear();
        hat_modell = false;
        sz->geaendert.emit(Szene3D::MODELL | Szene3D::HELL);
    }

    void realisieren();

    ~Ansicht()
    {
        verbunden.disconnect();
        if (rk_druck) gdk_event_free(rk_druck);
    }

    void aufraeumen();

    // HIRCH_3D_ZEIT: je Sekunde Bilder, Abstand zwischen den Bildern und CPU-Zeit in malen() ins Protokoll –
    // trennt «Grafikkarte zu langsam» (Teil-Zeit gross) von «CPU/Fenster zu langsam» (Abstand gross, Teil klein)
    // verkleinert zeichnen (Grafikkarte zu langsam für die volle Fläche, Peter 22.10.: iMac mit Nouveau, 2044 × 1270):
    // in einen eigenen Puffer mit skala × Grösse, dann weich auf die Fläche hochziehen
    double skala = 1.0;
    GLuint fbo_s = 0, rb_s_farbe = 0, rb_s_tiefe = 0;
    int fbo_s_w = 0, fbo_s_h = 0;
    void klein_puffer(int w, int h);
    // Kantenglättung (Basis 28.10., Windows-Sitzung: Quadro zeichnete ohne): in einen Multisample-Puffer zeichnen,
    // in fbo_s auflösen (gleiches Format), von dort auf die Fläche. Der Besitzer schaltet sie je Bild (glatt_wunsch);
    // ohne Grafikkarte (llvmpipe) nie. HIRCH_3D_GLATT=0/1: fest aus/an (Fehlersuche, auch ohne Grafikkarte).
    bool glatt_wunsch = false, software_gl = false;
    int proben_max = -1;                        // GL_MAX_SAMPLES, höchstens 4; -1: noch nicht gefragt
    int max_puffer = 4096;                      // GL_MAX_RENDERBUFFER_SIZE
    float linie_max = 1.0f, linie_jetzt = 1.0f; // Linienbreite: möglich (GL_ALIASED_LINE_WIDTH_RANGE), gesetzt
    double skala_jetzt = 1.0;                   // wirklich benutzt (skala, für grösser begrenzt)
    int proben_jetzt = 0;                       // im letzten Bild benutzt
    GLuint fbo_m = 0, rb_m_farbe = 0, rb_m_tiefe = 0;
    int fbo_m_w = 0, fbo_m_h = 0, fbo_m_p = 0;
    bool ms_puffer(int w, int h, int p);
    gint64 z_letzt = 0, z_log = 0;
    int z_bilder = 0;
    double z_cpu = 0, z_abstand = 0;
    bool malen();
};

// ---------------------------------------------------------------------------
// Dialog «3D-Modell einlesen»

struct Einlesen : Gtk::Dialog {
    SPDesktop *desk;
    Material mat;
    std::string ordner = neuer_ordner();
    std::shared_ptr<bool> lauf;
    unsigned gen = 0;
    bj::object erg;
    bool fertig = false, setzt = false;
    double drehen_z = 0;

    Gtk::Entry pfad;
    Gtk::Label erkannt, status, groesse, hoehe, material_l, rand, hinweis;
    Gtk::Box bild_box{Gtk::ORIENTATION_VERTICAL, 8}, netz_box{Gtk::ORIENTATION_VERTICAL, 8};
    Gtk::ComboBoxText oben, lage, hell;
    Gtk::SpinButton *neig_x = nullptr, *neig_y = nullptr, *weiss = nullptr, *breite = nullptr, *glaetten = nullptr;
    Gtk::Image vorschau;
    Gtk::Button *ok = nullptr;
    sigc::connection verzoegert;

    Einlesen(Gtk::Window &eltern, SPDesktop *d);

    ~Einlesen() override
    {
        verzoegert.disconnect();
        if (lauf) *lauf = false;
        ordner_loeschen(ordner);
    }

    void zeigen_fuer(std::string const &p)
    {
        bool bild = ist_bild_datei(p);
        zeile_zeigen(&bild_box, bild);
        zeile_zeigen(&netz_box, !bild);
    }

    void durchsuchen();

    bj::object ausrichtung_json();

    bj::object lage_json()
    {
        bj::object l;
        l["art"] = lage.get_active_id().raw();
        return l;
    }

    void lesen();

    void antwort(bool okay, bj::object const &e, std::string const &fehler);
};

// ---------------------------------------------------------------------------
// Gefrästes Teil für die 3D-Ansicht im Hauptfenster (hirch-3d-teil.cpp)

std::array<float, 3> hex_farbe(std::string const &hex, std::array<float, 3> ersatz);

/// Bewegungen aller Programme mit ihrer Bearbeitung (Index in bearbeitungen) – wie die Vorschau
std::vector<AbtragBewegung> teil_bewegungen(VorschauDaten const &d, std::vector<AbtragBearbeitung> &bearbeitungen);

/// Raster des gebauten Teils (für Linien auf dem Teil und den Klick): Punkt (I, J) bei (x0 + I·dx, y0 + J·dy)
struct TeilRaster {
    int nx = 0, ny = 0;
    double x0 = 0, y0 = 0, dx = 1, dy = 1;
    std::vector<float> o, u;
    std::vector<char> fest;
    bool gilt(int I, int J) const { return I >= 0 && J >= 0 && I < nx && J < ny; }
    std::size_t at(int I, int J) const { return std::size_t(J) * nx + I; }
    /// nächster Rasterpunkt zu (x, y), -1: ausserhalb
    long punkt(double x, double y) const
    {
        int I = int(std::lround((x - x0) / dx)), J = int(std::lround((y - y0) / dy));
        return gilt(I, J) ? long(at(I, J)) : -1;
    }
};

/// Auftrag an den Thread: Höhen in, Netz aus
struct NetzAuftrag {
    int W = 0, H = 0;
    double x0 = 0, y0 = 0, zx = 1, zy = 1, D = 18;
    double rund_r = 0, cx = 0, cy = 0;
    std::vector<float> oben, unten;          // Material-z je Zelle (Seite 2 in der Lage von Seite 1)
    std::size_t budget = 3000000;            // höchstens so viele Dreiecke
    // Ergebnis
    std::vector<float> pos[2];               // x, y, z je Ecke (Oberseite, Unterseite)
    std::vector<std::int8_t> nk[2];          // Normale und Cavity je Ecke
    std::vector<unsigned> idx;
    int schritt = 1;                         // Gitterpunkte alle so viele Zellen
    double fehler = 0;
    double sekunden = 0;
    std::atomic<bool> fertig{false}, abbrechen{false};
};

void netz_rechnen(NetzAuftrag &A);

void teil_bauen(VorschauDaten const &d, HirchAbtrag &ab, std::vector<float> &e, std::vector<unsigned> &idx,
                TeilRaster &R, bool netz = true, double max_punkte = 400000,
                NetzAuftrag *netz_aus = nullptr);

/// Formteile Gewinde in der Lage von Seite 1 (Seite 2 gewendet)
/// enden (wenn gesetzt): je Gewinde (Programm, nach dem es fertig ist; Ecken bis hier) – nach Programm geordnet
void gewinde_bauen(VorschauDaten const &d, std::vector<float> &aus,
                   std::vector<std::pair<int, int>> *enden = nullptr);

struct TeilGL {
    GLuint prog = 0, vao_kachel = 0, vbo_kachel = 0, ebo_kachel = 0, vao_wand = 0, vbo_wand = 0;
    GLuint tex_farben = 0, tex_klasse = 0, tex_steg = 0;   // tex_klasse: Soll (lo, hi) je Seite
    double gitter[4] = {0, 0, 1, 1};
    bool mit_stegen = false;
    GLuint vbo_inst = 0;                   // je Kachel: x0, y0, Grösse, Schürze
    // Pyramide «tiefste Stelle je Block» je Seite: Stufe k (Mip k−1) = Blöcke aus 2^k × 2^k Zellen
    GLuint p_min = 0, vao_leer = 0, fbo_min = 0, tex_min[2] = {0, 0};
    int min_w = 0, min_h = 0, stufen = 0;
    bool software = false;
    GLsizei n_kachel = 0, n_wand = 0;
    int anz_farben = 0;
    bool mit_klasse = false;
    static constexpr int N = 32;           // Kacheln: N × N Vierecke
    std::map<std::string, GLint> u;

    GLint ort(char const *n)
    {
        auto it = u.find(n);
        if (it != u.end()) return it->second;
        return u[n] = glGetUniformLocation(prog, n);
    }

    bool start();

    /// Pyramide neu (nach jedem Rechnen von hirch-abtrag): je Seite Stufe für Stufe das Minimum (= tiefste Stelle)
    void pyramide(HirchAbtrag const &ab);

    static void attribute()
    {
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(2 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(3 * sizeof(float)));
    }

    void ende();

    /// Farben der Bearbeitungen (Index der Karte − 1)
    void farben(std::vector<AbtragBearbeitung> const &bea);

    /// Raster für Soll und Stege (TeilRaster: Mitte der ersten Zelle x0, y0, Schritt dx, dy)
    void gitter_setzen(TeilRaster const &R)
    {
        gitter[0] = R.x0 - 0.5 * R.dx;
        gitter[1] = R.y0 - 0.5 * R.dy;
        gitter[2] = R.nx * R.dx;
        gitter[3] = R.ny * R.dy;
    }

    /// Vergleich mit dem Modell: je Zelle (lo, hi) Oberseite, (lo, hi) Unterseite
    void soll(int w, int h, std::vector<float> const &v)
    {
        mit_klasse = w > 0 && h > 0 && v.size() == std::size_t(w) * h * 4;
        if (!mit_klasse) return;
        glBindTexture(GL_TEXTURE_2D, tex_klasse);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, v.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    /// Haltestege: je Zelle (1, z unten, z oben)
    void stege(int w, int h, std::vector<float> const &v)
    {
        mit_stegen = w > 0 && h > 0 && v.size() == std::size_t(w) * h * 3;
        if (!mit_stegen) return;
        glBindTexture(GL_TEXTURE_2D, tex_steg);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, w, h, 0, GL_RGB, GL_FLOAT, v.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    /// Aussenwand: oben die Oberseite, unten die Unterseite des Teils (Platte: Rechteck, rund: Kreis)
    void wand(double x0, double y0, double w, double h, double zelle, double rund_r, double cx, double cy);

    struct Knoten { double x, y, s; };

    /// Kacheln wählen (Peter, 22.10.: kleine Schilder): teilen, solange ein Gitterabstand auf dem Bildschirm
    /// grösser als «schwelle» Bildpunkte ist und die Kachel gröber als das Raster. pix: Bildpunkte je Radiant.
    /// Kacheln ausserhalb des Bildes fallen weg. Die Kacheln liegen auf den Mitten der Zellen.
    void kacheln(double x0, double y0, double w, double h, double zelle, double dicke, V3 const &auge, M4 const &pv,
                 double pix, double schwelle, std::vector<Knoten> &aus) const;

    /// Zeichnen: modus 0 neutral, 1 Bearbeitung, 2 Vergleich; wahl: Index der Karte (+1) kräftig, 0 alle
    void malen(HirchAbtrag const &ab, double dicke, bool zwei, bool um_y, double rund_r, double cx, double cy,
               int modus, int wahl, bool stege_an, M4 const &pv, V3 const &auge, V3 const &licht, Blick const &bl,
               std::function<void()> const &netz = {})
    {
        if (!prog || !ab.tex_h) return;
        glUseProgram(prog);
        glUniformMatrix4fv(ort("uPV"), 1, GL_FALSE, pv.data());
        glUniform4f(ort("uMat"), float(ab.x0), float(ab.y0), float(ab.w), float(ab.h));
        glUniform1f(ort("uHmin"), float(ab.hmin));
        glUniform1f(ort("uDicke"), float(dicke));
        glUniform1i(ort("uUmY"), um_y ? 1 : 0);
        bool z2 = zwei && ab.tex_h2;
        glUniform1i(ort("uZwei"), z2 ? 1 : 0);
        glUniform1i(ort("uModus"), modus);
        glUniform1i(ort("uWahl"), wahl);
        glUniform1i(ort("uAnz"), anz_farben);
        glUniform1i(ort("uMitKlasse"), mit_klasse ? 1 : 0);
        glUniform2f(ort("uTexel"), 1.f / std::max(1, ab.W), 1.f / std::max(1, ab.H));
        glUniform3f(ort("uRund"), float(cx), float(cy), float(rund_r));
        glUniform3f(ort("uLicht"), float(licht.x), float(licht.y), float(licht.z));
        glUniform3f(ort("uAuge"), float(auge.x), float(auge.y), float(auge.z));
        glUniform3f(ort("uRechts"), float(bl.rechts.x), float(bl.rechts.y), float(bl.rechts.z));
        glUniform3f(ort("uHoch"), float(bl.hoch.x), float(bl.hoch.y), float(bl.hoch.z));
        glUniform3f(ort("uVor"), float(bl.vor.x), float(bl.vor.y), float(bl.vor.z));
        glUniform1f(ort("uZelle"), float(std::min(ab.w / std::max(1, ab.W), ab.h / std::max(1, ab.H))));
        glUniform1i(ort("uStufen"), stufen);
        glUniform4f(ort("uGitter"), float(gitter[0]), float(gitter[1]), float(gitter[2]), float(gitter[3]));
        glUniform1i(ort("uStege"), stege_an && mit_stegen ? 1 : 0);
        GLuint texe[9] = {ab.tex_h, z2 ? ab.tex_h2 : ab.tex_h, ab.tex_karte, z2 ? ab.tex_karte2 : ab.tex_karte,
                          tex_farben, tex_klasse, tex_min[0], z2 ? tex_min[1] : tex_min[0], tex_steg};
        char const *namen[9] = {"uH", "uH2", "uKarte", "uKarte2", "uFarben", "uSoll", "uMin", "uMin2", "uSteg"};
        for (int k = 0; k < 9; ++k) {
            glActiveTexture(GL_TEXTURE0 + k);
            glBindTexture(GL_TEXTURE_2D, texe[k]);
            glUniform1i(ort(namen[k]), k);
        }
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        // Zeit des letzten Bilds auf der Grafikkarte (Abfrage von vorher, ohne zu warten): Feinheit nachführen –
        // zu langsam: gröber und zuletzt ohne Cavity, viel Luft: wieder feiner (Peter, 22.10.: iMac mit Nouveau)
        if (!abfrage[0]) glGenQueries(2, abfrage);
        int alt = 1 - abfrage_i;
        if (abfrage_laeuft[alt]) {
            GLint da = 0;
            glGetQueryObjectiv(abfrage[alt], GL_QUERY_RESULT_AVAILABLE, &da);
            if (da) {
                GLuint64 ns = 0;
                glGetQueryObjectui64v(abfrage[alt], GL_QUERY_RESULT, &ns);
                abfrage_laeuft[alt] = false;
                nachfuehren(ns / 1e6);
            }
        }
        double zelle = std::min(ab.w / std::max(1, ab.W), ab.h / std::max(1, ab.H));
        // Oberseite (und Unterseite bei zweiseitig) nach Bildpunkten; einseitig ist die Unterseite eben: grob genügt
        // (Durchbrüche schneidet der Fragment-Shader je Bildpunkt aus)
        std::vector<Knoten> kn1, kn2;
        double schwelle = qual;
        for (int versuch = 0; versuch < 12 && !netz; ++versuch) {
            kn1.clear();
            kacheln(ab.x0, ab.y0, ab.w, ab.h, zelle, dicke, auge, pv, bl.pix, schwelle, kn1);
            if (kn1.size() <= std::size_t(software ? 1500 : 6000)) break;
            schwelle *= 1.4;
        }
        if (netz || z2) {
            kn2 = kn1;
        } else {
            kacheln(ab.x0, ab.y0, ab.w, ab.h, zelle, dicke, auge, pv, bl.pix, schwelle * 24, kn2);
        }
        std::vector<float> inst;
        inst.reserve((kn1.size() + kn2.size()) * 4);
        for (auto const *liste : {&kn1, &kn2})
            for (auto const &k : *liste) {
                // Schürze: deckt Fugen zwischen Stufen (grobe Kachel: tiefste Stelle je Block, darum etwas mehr)
                double schuerze = 4.0 * k.s / N + 0.2;
                inst.insert(inst.end(), {float(k.x), float(k.y), float(k.s), float(schuerze)});
            }
        glBindBuffer(GL_ARRAY_BUFFER, vbo_inst);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(inst.size() * sizeof(float)), inst.data(), GL_STREAM_DRAW);
        glBindVertexArray(vao_kachel);
        glUniform1i(ort("uWand"), 0);
        glUniform1i(ort("uCavity"), cavity ? 1 : 0);
        glBeginQuery(GL_TIME_ELAPSED, abfrage[abfrage_i]);
        if (netz) {                         // fertiges Netz statt Kacheln (Texturen bleiben gebunden)
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            netz();
            glUseProgram(prog);
        }
        // zwei Durchgänge: Tiefe (mit Löchern), dann Farbe nur für den vordersten Bildpunkt
        for (int pass = 1; pass >= 0; --pass) {
            glUniform1i(ort("uVorpass"), pass);
            if (pass == 1) {
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                glDepthMask(GL_TRUE);
                glDepthFunc(GL_LESS);
            } else {
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glDepthMask(GL_FALSE);
                glDepthFunc(GL_EQUAL);
            }
            glBindVertexArray(vao_kachel);
            glUniform1i(ort("uWand"), 0);
            for (int seite = 1; seite <= 2; ++seite) {
                auto const &kn = seite == 1 ? kn1 : kn2;
                std::size_t ab_ = seite == 1 ? 0 : kn1.size();
                glUniform1i(ort("uSeite"), seite);
                glBindBuffer(GL_ARRAY_BUFFER, vbo_inst);
                glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(ab_ * 4 * sizeof(float)));
                if (!kn.empty())
                    glDrawElementsInstanced(GL_TRIANGLES, n_kachel, GL_UNSIGNED_INT, nullptr, GLsizei(kn.size()));
            }
            if (n_wand > 0) {
                glUniform1i(ort("uWand"), 1);
                glUniform1i(ort("uSeite"), 1);
                glBindVertexArray(vao_wand);
                glDrawArrays(GL_TRIANGLES, 0, n_wand);
            }
        }
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glEndQuery(GL_TIME_ELAPSED);
        abfrage_laeuft[abfrage_i] = true;
        abfrage_i = 1 - abfrage_i;
        glBindVertexArray(0);
        for (int k = 8; k >= 0; --k) {
            glActiveTexture(GL_TEXTURE0 + k);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        letzte_kacheln = int(kn1.size() + kn2.size());
    }
    int letzte_kacheln = 0;

    // Feinheit nach der Zeit je Bild: qual = Bildpunkte je Gitterabstand (1 = so fein wie der Bildschirm)
    double qual = 0;                       // 0: beim ersten Bild nach Grafikkarte setzen
    double skala = 1.0;                    // Anteil der Fläche, in dem gezeichnet wird (Ansicht::skala)
    bool cavity = true;
    // Kantenglättung (Basis 28.10.): wird es zu langsam, geht sie zuerst weg; wieder an erst, wenn alles andere
    // voll ist und das Bild deutlich schneller als nötig – nach jedem Abschalten strenger (kein Hin und Her)
    bool glatt = true;
    double glatt_bis = 12;
    static constexpr double SKALA_GROSS = 2.0;   // höchstens doppelt so gross zeichnen (Peter 28.10.)
    GLuint abfrage[2] = {0, 0};
    bool abfrage_laeuft[2] = {false, false};
    int abfrage_i = 0;
    gint64 log_uhr = 0;
    double basis_qual() const { return software ? 3.0 : 2.0; }
    void nachfuehren(double ms);
};

/// Netz auf der Grafikkarte und zeichnen
struct TeilNetzGL {
    GLuint prog = 0, vao[2] = {0, 0}, vbo_p[2] = {0, 0}, vbo_n[2] = {0, 0}, ebo = 0;
    GLsizei n_idx = 0;
    float mat[4] = {0, 0, 1, 1};            // Material: x0, y0, Breite, Höhe (Karten)
    float schritt[2] = {0, 0};              // Netzzelle in uv
    bool da = false;
    std::map<std::string, GLint> u;
    GLint ort(char const *n)
    {
        auto it = u.find(n);
        if (it != u.end()) return it->second;
        return u[n] = glGetUniformLocation(prog, n);
    }
    bool start();
    void ende();
    void laden(NetzAuftrag const &A);

    /// im Rahmen von TeilGL::malen: dessen Texturen sind gebunden (2 Karte, 3 Karte 2, 4 Farben, 5 Soll, 8 Stege)
    void malen(TeilGL &T, HirchAbtrag const &ab, double dicke, bool zwei, bool um_y, int modus, int wahl,
               bool stege_an, M4 const &pv, Blick const &bl);
};

/// Vergleich mit dem Modell (wie relief_ergebnis): je Rasterpunkt R = Oberseite, G = Unterseite –
/// 0 wie gewollt, 128 Material bleibt stehen (mehr als 0,5 mm), 255 zu tief (mehr als 0,1 mm). Das
/// Modell ist das Netz der Szene (Reiter «3D»); an steilen Wänden zählt die Nachbarschaft (3 × 3).
/// Ober- und Unterseite des 3D-Modells je Zelle des Rasters (aus den Dreiecken der Szene; NaN: kein Modell)
void modell_hoehen(Szene3D const &sz, TeilRaster const &R, std::vector<float> &mo, std::vector<float> &mu);
std::vector<float> soll_bauen(Szene3D const &sz, TeilRaster const &R, bool zwei);

/// Haltestege (Quader aus quader(): je 36 Ecken, die ersten 6 sind die Oberseite) auf dem Raster: je Zelle
/// (1, z unten, z oben), sonst 0 – die Ansicht färbt dort das stehen gebliebene Material gelb
std::vector<float> steg_raster(std::vector<float> const &q, TeilRaster const &R);

/// Eine Linie der Zeichnung auf dem Teil: Stücke (Material-mm, Lage von Seite 1), Farbe, Seite
struct TeilLinie {
    std::string id;
    std::array<float, 3> farbe{0.5f, 0.5f, 0.5f};
    int seite = 1;
    std::vector<std::array<double, 2>> pkt;    // Polylinien hintereinander; nan trennt
};

/// Spannmittel, Schrauben und Sperrzonen für eine Stelle des Ablaufs (Seite, Spannung). Spannmittel: die der
/// Spannung; auf einer Seite ohne Umspannen die der Spannung 1. Zone: Umriss immer, Fläche nur in Spannung 1.
void spann_szene(VorschauDaten const &d, int seite, int spannung, std::vector<float> &sp, GLsizei &n_schraube,
                        std::vector<float> &zf, std::vector<float> &zl);

} // namespace hirch3d
using namespace hirch3d;
} // namespace Inkscape::UI::Dialog

#endif // SEEN_HIRCH_3D_INTERN_H
