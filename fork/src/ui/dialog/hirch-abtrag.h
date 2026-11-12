// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: der eine Rechenweg für das gefräste Teil (OpenGL 3.3, im GL-Kontext des Aufrufers).
 *
 * Aus den .tap-Programmen (was die Maschine wirklich fährt) entstehen zwei Höhenbilder auf demselben Raster:
 * Seite 1 trägt von oben ab, Seite 2 (nach dem Wenden) von unten. Jede Bewegung ist eine Instanz eines
 * Rechtecks um die Bahn; der Fragment-Shader rechnet die tiefste Stelle der Fräserspitze (Schaft, V mit
 * flacher Spitze, Kugel, Eckradius, konischer Kugelfräser), der Tiefentest LESS behält das Minimum.
 *
 * Dazu je Seite die Karte «zuletzt tiefer gemacht von» (uint16: 0 = unberührt, sonst Index der Bearbeitung + 1):
 * die Bearbeitung, die die Stelle am tiefsten gemacht hat; wer sie danach nur bis 0,02 mm tiefer nimmt, färbt
 * nicht um (MINDESTTIEFE). Gilt auch beim Abspielen (Fortschritt).
 *
 * Gewinde sind Hinterschneidungen: im Höhenbild steht die Hülle (Bohrung bis Nenn-Ø, Zapfen bis Kern-Ø), die
 * Gänge kommen als Formteil dazu (gewinde_netz).
 *
 * Benutzt von der Vorschau (hirch-vorschau.cpp) und der 3D-Ansicht im Hauptfenster.
 */
#ifndef SEEN_HIRCH_ABTRAG_H
#define SEEN_HIRCH_ABTRAG_H

#include <epoxy/gl.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Inkscape::UI::Dialog {

/// Eine Bewegung aus einer .tap-Datei (Bögen schon in Geraden aufgelöst), Material-mm, Z0 = Oberfläche der Seite
struct AbtragBewegung {
    bool eil = true;
    double x = 0, y = 0, z = 0, f = 0;
    int p = 0, zeile = 0;          // Programm (Index), Zeile der Datei (1 = erste)
    int farbe = 0;                 // Index in die Farbtafel der Vorschau (Bearbeitung bzw. Werkzeug)
    int abschnitt = -1;            // Bearbeitung in der Datei (Index in den Abschnitten des Programms)
    int bearbeitung = -1;          // Bearbeitung über alles (Index in HirchAbtrag::bearbeitungen), -1 = keine
    int ur = -1;                   // Index in «alle» (auch nach der Auswahl)
    std::string text;
};

/// .tap lesen (G0/G1/G2/G3, X Y Z F I J), Bögen in kurze Geraden (Sehnenfehler ≤ 0,005 mm)
std::vector<AbtragBewegung> tap_lesen(std::string const &text, int prog, double sicherheit);
std::string datei_lesen(std::string const &pfad);

/// Werkzeugform eines Programms (wie die Vorschau: flach | v | kugel | konisch)
struct AbtragWerkzeug {
    std::string form = "flach";
    double durchmesser = 3, vwinkel = 60, spitze = 0, eckradius = 0;
};

/// Gefrästes Gewinde (Formteil): Material-mm, Z0 = Oberfläche der Seite
struct AbtragGewinde {
    double x = 0, y = 0;        // Mitte
    double nenn = 8, steigung = 1.25, kern = 6.65;   // Nenn-Ø, Steigung, Kernloch-Ø (innen) bzw. Kern-Ø (aussen)
    double oben = 0, tiefe = 10;
    bool innen = true, rechts = true;
    int seite = 1;
    int programm = -1;          // Index des Programms, nach dem es fertig ist (Abspielen)
};

/// Gewindeprofil (ISO, 60°): Radius der Gewindefläche bei der axialen Phase s (0 = Mitte der Rille)
double gewinde_radius(AbtragGewinde const &g, double s);
/// Formteil Gewinde als Dreiecke (x, y, z, nx, ny, nz): Schraubenfläche und oben der Ring bis zur Kante
void gewinde_netz(AbtragGewinde const &g, std::vector<float> &av, int n_um = 72);

/// Eine Bearbeitung der Karte «zuletzt tiefer gemacht von»
struct AbtragBearbeitung {
    std::string id;             // Schlüssel der Bearbeitung (wie engine «abschnitte» → key)
    std::string farbe;          // #rrggbb
};

class HirchAbtrag
{
public:
    static constexpr double MINDESTTIEFE = 0.02;   // mm: so viel tiefer muss es werden, damit die Karte umfärbt

    /// Shader übersetzen (im aktuellen GL-Kontext). false: fehler gesetzt.
    bool gl_start(std::string &fehler);
    /// Puffer und Shader weg (im GL-Kontext, vor dessen Ende)
    void gl_ende();

    /// Raster und Puffer. x0/y0/w/h: Fläche in Material-mm; seite_je_prog: 1 oder 2 je Programm (zweiseitig);
    /// software: ohne Grafikkarte gröber
    void einrichten(double x0, double y0, double w, double h, double dicke, bool zweiseitig, bool software,
                    std::vector<AbtragBewegung> const &moves, std::vector<AbtragWerkzeug> const &werkzeug_je_prog,
                    std::vector<int> const &seite_je_prog, double sicherheit,
                    std::vector<AbtragBearbeitung> bearbeitungen = {});
    void puffer_weg();

    /// Höhenbilder bis zur Bewegung bis (Anzahl fertiger Bewegungen) und anteil (0…1) der nächsten.
    /// Vorwärts nur die neuen Stücke dazu, rückwärts neu von vorn.
    void rechnen(int bis, double anteil);

    /// CPU: Höhen je Zelle (mm, Material-Z, Zeile für Zeile ab y0) und Karte (0 = unberührt, sonst Index + 1)
    bool lesen(int seite, std::vector<float> &hoehe, std::vector<std::uint16_t> *karte = nullptr);

    // Raster: Zelle (i, j) hat die Mitte x0 + (i + 0,5)·zelle_x, y0 + (j + 0,5)·zelle_y (zelle_x = w / W)
    double x0 = 0, y0 = 0, w = 100, h = 100, zelle = 0.1, hmin = -28;
    int W = 2, H = 2;
    bool zweiseitig = false;
    /// Tiefentextur (normiert: 0 = hmin, 1 = Oberfläche), Farbtafel-Index der Vorschau (R8, (i + 1) / 32),
    /// Karte (R16, (Index + 1) / 65535) – je Seite
    GLuint tex_h = 0, tex_wz = 0, tex_karte = 0, tex_h2 = 0, tex_wz2 = 0, tex_karte2 = 0;
    std::vector<AbtragBearbeitung> bearbeitungen;
    int instanzen = 0, zeige_instanzen = 0;

private:
    void seite_anlegen(GLuint &fbo, GLuint &th, GLuint &twz, GLuint &tk, GLuint &fbo_k, GLuint &fbo_v, GLuint &tv,
                       GLuint &tkand);
    void fraesen(GLuint vao, int anzahl);
    void bereich_fraesen(int von, int bis_);
    void vorher_merken(GLuint fbo, GLuint fbo_v);
    void karte_uebernehmen(GLuint th, GLuint tv, GLuint tkand, GLuint fbo_k);

    GLuint p_sweep = 0, p_karte = 0, vao_leer = 0;
    GLuint fbo_h = 0, fbo_h2 = 0;
    GLuint fbo_k = 0, fbo_k2 = 0;                  // Karte (Übernahme)
    GLuint fbo_v = 0, fbo_v2 = 0, tex_v = 0, tex_v2 = 0;   // Tiefe vor dem Stück (Vergleich für die Karte)
    GLuint tex_kand = 0, tex_kand2 = 0;            // Bearbeitung der tiefsten Stelle dieses Stücks
    GLuint vao_sweep = 0, vao_sweep2 = 0, vao_teil = 0, vao_bereich = 0;
    GLuint vbo_quad = 0, vbo_inst = 0, vbo_teil = 0;
    std::vector<float> inst_daten;
    std::vector<int> bis_instanz, seite_der_bewegung;
    int n_inst1 = 0, gez_inst = -1;
};

} // namespace Inkscape::UI::Dialog

#endif // SEEN_HIRCH_ABTRAG_H
