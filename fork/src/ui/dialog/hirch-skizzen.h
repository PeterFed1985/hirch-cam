// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Skizzen im Bearbeitungsdialog, Zustellungen, Gewinde, Begrüssung
 * Nur für hirch-*.cpp – kein öffentlicher Header. Sammelkopf: hirch-intern.h.
 */
#ifndef HIRCH_SKIZZEN_H
#define HIRCH_SKIZZEN_H

#include "ui/dialog/hirch-oberflaeche.h"

namespace Inkscape::UI::Dialog {

// ---------------------------------------------------------------------------
// Skizzen im Bearbeitungsdialog (zeigen live, was die Einstellung bewirkt)
// ---------------------------------------------------------------------------
using Cr = Cairo::RefPtr<Cairo::Context>;

inline double const HOLZ[3] = {0.90, 0.82, 0.65};
inline double const HOLZ_RAND[3] = {0.54, 0.35, 0.17};
inline double const BAHN[3] = {0.0, 0.36, 0.95};

inline void rundrechteck(Cr const &cr, double x, double y, double w, double h, double r)
{
    r = std::max(0.0, std::min(r, std::min(w, h) / 2));
    cr->begin_new_sub_path();
    cr->arc(x + w - r, y + r, r, -M_PI / 2, 0);
    cr->arc(x + w - r, y + h - r, r, 0, M_PI / 2);
    cr->arc(x + r, y + h - r, r, M_PI / 2, M_PI);
    cr->arc(x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cr->close_path();
}

inline void pfeil(Cr const &cr, double x, double y, double winkel, double g = 7)
{
    cr->move_to(x + std::cos(winkel) * g, y + std::sin(winkel) * g);
    cr->line_to(x + std::cos(winkel + 2.5) * g, y + std::sin(winkel + 2.5) * g);
    cr->line_to(x + std::cos(winkel - 2.5) * g, y + std::sin(winkel - 2.5) * g);
    cr->close_path();
    cr->fill();
}

inline void schrift(Cr const &cr, Gdk::RGBA const &fg, double x, double y, std::string const &t,
                    double alpha = 0.9, bool fett = false)
{
    cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), alpha);
    cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL, fett ? Cairo::FONT_WEIGHT_BOLD : Cairo::FONT_WEIGHT_NORMAL);
    cr->set_font_size(11);
    cr->move_to(x, y);
    cr->show_text(einheit_text_zahlen(t));
}

inline void farbe(Cr const &cr, double const c[3], double a = 1.0) { cr->set_source_rgba(c[0], c[1], c[2], a); }

/// Zeichenfläche mit Rahmen; der Zeichner bekommt Breite, Höhe und Textfarbe
using Zeichner = std::function<void(Cr const &, double, double, Gdk::RGBA const &)>;
inline Gtk::DrawingArea *skizze(int hoehe, Zeichner zeichner)
{
    auto d = Gtk::make_managed<Gtk::DrawingArea>();
    d->set_size_request(400, hoehe);
    d->signal_draw().connect([d, zeichner](Cr const &cr) {
        double w = d->get_allocated_width(), h = d->get_allocated_height();
        auto fg = d->get_style_context()->get_color(Gtk::STATE_FLAG_NORMAL);
        rundrechteck(cr, 0.5, 0.5, w - 1, h - 1, 6);
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.18);
        cr->set_line_width(1);
        cr->stroke();
        cr->save();
        zeichner(cr, w, h, fg);
        cr->restore();
        return true;
    });
    return d;
}

/// Kontur von oben: Teil mit Loch, Fräsbahn je nach Seite, Richtung als Pfeile
inline void skizze_kontur(Cr const &cr, double w, double h, Gdk::RGBA const &fg, std::string const &seite,
                          bool gleichlauf, double aufmass)
{
    double rw = std::min(w * 0.46, 190.0), rh = h - 52;
    double cx = 26 + 12 + rw / 2, cy = h / 2, x0 = cx - rw / 2, y0 = cy - rh / 2, r = 12, kr = rh * 0.22;
    double o = 8, a = aufmass > 1e-9 ? 3 : (aufmass < -1e-9 ? -3 : 0);
    // Teil
    cr->set_fill_rule(Cairo::FILL_RULE_EVEN_ODD);
    rundrechteck(cr, x0, y0, rw, rh, r);
    cr->begin_new_sub_path();
    cr->arc(cx, cy, kr, 0, 2 * M_PI);
    farbe(cr, HOLZ);
    cr->fill_preserve();
    farbe(cr, HOLZ_RAND);
    cr->set_line_width(1.2);
    cr->stroke();
    // Versatz der Bahn: + aussen, − innen
    double aussen_off = 0, loch_off = 0;
    if (seite == "aussen") { aussen_off = o + a; loch_off = o + a; }
    else if (seite == "innen") { aussen_off = -(o + a); loch_off = -(o + a); }
    else if (seite == "auto") { aussen_off = o + a; loch_off = -(o + a); }
    farbe(cr, BAHN, 0.9);
    cr->set_line_width(2);
    rundrechteck(cr, x0 - aussen_off, y0 - aussen_off, rw + 2 * aussen_off, rh + 2 * aussen_off, r + aussen_off);
    cr->stroke();
    double lr = kr + loch_off;
    if (lr > 2) {
        cr->begin_new_sub_path();
        cr->arc(cx, cy, lr, 0, 2 * M_PI);
        cr->stroke();
    }
    // Richtung (von oben gesehen): Werkzeug aussen an der Linie → im Uhrzeigersinn bei Gleichlauf
    bool uz_aussen = (aussen_off >= 0) ? gleichlauf : !gleichlauf;
    bool uz_loch = (loch_off >= 0) ? gleichlauf : !gleichlauf;
    double oben = y0 - aussen_off, unten = y0 + rh + aussen_off;
    pfeil(cr, cx, oben, uz_aussen ? 0 : M_PI);
    pfeil(cr, cx, unten, uz_aussen ? M_PI : 0);
    if (lr > 2) pfeil(cr, cx, cy - lr, uz_loch ? 0 : M_PI, 6);
    // Werkzeug
    cr->set_source_rgba(0.45, 0.45, 0.5, 0.55);
    cr->arc(x0 - aussen_off, cy, o - 1, 0, 2 * M_PI);
    cr->fill();
    // Legende
    double tx = x0 + rw + 30;
    std::string s = seite == "aussen" ? TR("Aussen") : seite == "innen" ? TR("Innen") : seite == "linie" ? TR("Auf der Linie")
                                                                                                 : TR("Aussen, Löcher innen");
    schrift(cr, fg, tx, 24, s, 0.9, true);
    schrift(cr, fg, tx, 42, gleichlauf ? TR("Gleichlauf") : TR("Gegenlauf"));
    schrift(cr, fg, tx, 58, TR("Spindel dreht rechts"), 0.6);
    if (std::abs(aufmass) > 1e-9) {
        schrift(cr, fg, tx, 80, TR("Aufmass ") + std::string(aufmass > 0 ? "+" : "") + zahl(aufmass) + TR(" mm"));
        schrift(cr, fg, tx, 96, aufmass > 0 ? TR("Teil wird grösser") : TR("Teil wird kleiner"), 0.6);
    }
}

/// Tasche von oben: Runden oder adaptive Schlaufen
inline void skizze_tasche(Cr const &cr, double w, double h, Gdk::RGBA const &fg, bool adaptiv, double bahn,
                          double last, bool gleichlauf, double aufmass, double d)
{
    double x0 = 18, y0 = 16, rw = std::min(w * 0.58, 230.0), rh = h - 32, r = 10, R = 7;
    farbe(cr, HOLZ);
    cr->rectangle(6, 6, x0 + rw + 12 - 6, h - 12);
    cr->fill();
    rundrechteck(cr, x0, y0, rw, rh, r);
    cr->set_source_rgba(1, 1, 1, 0.9);
    cr->fill_preserve();
    farbe(cr, HOLZ_RAND);
    cr->set_line_width(1.2);
    cr->stroke();
    double a = aufmass > 1e-9 ? 2.5 : (aufmass < -1e-9 ? -2.5 : 0);
    farbe(cr, BAHN, 0.85);
    cr->set_line_width(1.4);
    double rand = R + a;
    rundrechteck(cr, x0 + rand, y0 + rand, rw - 2 * rand, rh - 2 * rand, r - rand);
    cr->stroke();
    // Aussenbahn einer Tasche: Gegenuhrzeigersinn bei Gleichlauf
    pfeil(cr, x0 + rw / 2, y0 + rand, gleichlauf ? M_PI : 0, 6);
    if (!adaptiv) {
        double s = std::max(2.0, 2 * R * bahn / 100.0);
        for (double e = rand + s; e < std::min(rw, rh) / 2 - 0.5; e += s) {
            rundrechteck(cr, x0 + e, y0 + e, rw - 2 * e, rh - 2 * e, r - e);
            cr->stroke();
        }
    } else {
        double rl = R * 0.9, adv = std::max(3.0, 2 * R * last / 100.0 * 3.0);
        cr->set_line_width(1.1);
        for (double yb = y0 + rand + rl + 3; yb < y0 + rh - rand - rl - 2; yb += 2 * rl + 4) {
            double xs = x0 + rand + rl + 2, xe = x0 + rw - rand - rl - 2;
            bool erst = true;
            for (double t = 0; ; t += 0.25) {
                double x = xs + adv * t / (2 * M_PI) + rl * std::cos(t), y = yb + rl * std::sin(t);
                if (xs + adv * t / (2 * M_PI) > xe) break;
                if (erst) cr->move_to(x, y); else cr->line_to(x, y);
                erst = false;
            }
            cr->stroke();
        }
    }
    double tx = x0 + rw + 26;
    schrift(cr, fg, tx, 24, adaptiv ? TR("Adaptiv") : TR("In Runden"), 0.9, true);
    if (adaptiv) {
        schrift(cr, fg, tx, 42, TR("Schnittbreite ") + zahl(last, 0) + " %");
        schrift(cr, fg, tx, 58, "= " + zahl(d * last / 100.0) + TR(" mm"), 0.6);
    } else {
        schrift(cr, fg, tx, 42, TR("Bahnabstand ") + zahl(bahn, 0) + " %");
        schrift(cr, fg, tx, 58, "= " + zahl(d * bahn / 100.0) + TR(" mm"), 0.6);
    }
    schrift(cr, fg, tx, 80, gleichlauf ? TR("Gleichlauf") : TR("Gegenlauf"));
    if (std::abs(aufmass) > 1e-9) {
        schrift(cr, fg, tx, 98, TR("Aufmass ") + std::string(aufmass > 0 ? "+" : "") + zahl(aufmass) + TR(" mm"));
    }
}

/// Eintauchen von der Seite: Rampe im Zickzack oder senkrecht
inline void skizze_eintauchen(Cr const &cr, double w, double h, Gdk::RGBA const &fg, bool rampe, double winkel)
{
    double ys = 34, yd = h - 30, x0 = 24, L = std::min(w * 0.42, 170.0);
    farbe(cr, HOLZ);
    cr->rectangle(6, ys, w - 12, h - 6 - ys);
    cr->fill();
    cr->set_source_rgba(0.4, 0.4, 0.4, 0.6);
    cr->set_line_width(1);
    std::vector<double> strich{4, 3};
    cr->set_dash(strich, 0);
    cr->move_to(6, yd);
    cr->line_to(w - 6, yd);
    cr->stroke();
    cr->unset_dash();
    farbe(cr, BAHN, 0.9);
    cr->set_line_width(2);
    double x = x0, y = ys - 10, xend;
    cr->move_to(x, y);
    if (rampe) {
        cr->line_to(x, ys);
        y = ys;
        double vis = std::clamp(winkel * 3.0, 6.0, 35.0) * M_PI / 180.0, dy = L * std::tan(vis);
        int richtung = 1;
        for (int i = 0; i < 40 && y < yd - 0.1; ++i) {
            double y2 = std::min(y + dy, yd);
            x += richtung * L * (y2 - y) / dy;
            y = y2;
            cr->line_to(x, y);
            richtung = -richtung;
        }
        xend = x0 + L + 50;
        cr->line_to(xend, yd);
        cr->stroke();
        pfeil(cr, xend, yd, 0, 6);
        schrift(cr, fg, x0, 20, TR("Rampe ") + zahl(winkel, 1) + TR("° – taucht schräg ein, ohne Lastspitze"), 0.9, true);
    } else {
        cr->line_to(x, yd);
        cr->stroke();
        pfeil(cr, x, yd, M_PI / 2, 6);
        cr->move_to(x, yd);
        xend = x0 + L + 50;
        cr->line_to(xend, yd);
        cr->stroke();
        pfeil(cr, xend, yd, 0, 6);
        schrift(cr, fg, x0, 20, TR("Senkrecht – der Fräser braucht Mittenschnitt"), 0.9, true);
    }
    schrift(cr, fg, xend + 14, yd + 4, TR("Tiefe"), 0.6);
}

/// Haltestege: die Schnittfuge aufgerollt, von der Seite
inline void skizze_stege(Cr const &cr, double w, double h, Gdk::RGBA const &fg, int anzahl, double breite,
                         double hoehe, double dicke, bool durch)
{
    double yt = 40, yb = h - 26, x0 = 14, x1 = w - 14;
    if (!durch) cr->push_group();
    cr->set_source_rgba(0.6, 0.6, 0.6, 0.45);   // Opferplatte
    cr->rectangle(x0, yb, x1 - x0, 12);
    cr->fill();
    cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.35);
    cr->set_line_width(1);
    std::vector<double> strich{4, 3};
    cr->set_dash(strich, 0);
    cr->move_to(x0, yt);
    cr->line_to(x1, yt);
    cr->stroke();
    cr->unset_dash();
    double sh = std::max(3.0, (yb - yt) * std::clamp(hoehe / std::max(dicke, 0.1), 0.0, 1.0));
    double sb = std::clamp(breite * 2.5, 8.0, 60.0);
    std::vector<double> mitte;
    for (int i = 0; i < anzahl; ++i) mitte.push_back(x0 + (x1 - x0) * (i + 0.5) / anzahl);
    for (double m : mitte) {
        cr->rectangle(m - sb / 2, yb - sh, sb, sh);
        farbe(cr, HOLZ);
        cr->fill_preserve();
        farbe(cr, HOLZ_RAND);
        cr->stroke();
    }
    // Fräsbahn im letzten Durchgang: über die Stege hinweg
    farbe(cr, BAHN, 0.9);
    cr->set_line_width(2);
    cr->move_to(x0, yb - 1);
    for (double m : mitte) {
        cr->line_to(m - sb / 2 - 3, yb - 1);
        cr->line_to(m - sb / 2 - 3, yb - sh - 2);
        cr->line_to(m + sb / 2 + 3, yb - sh - 2);
        cr->line_to(m + sb / 2 + 3, yb - 1);
    }
    cr->line_to(x1, yb - 1);
    cr->stroke();
    schrift(cr, fg, x1 - 120, yt - 4, TR("Materialoberfläche"), 0.5);
    schrift(cr, fg, x0 + 4, yb + 10, TR("Opferplatte"), 0.6);
    schrift(cr, fg, x0 + 4, (yt + yb) / 2 + 4, TR("Schnittfuge von der Seite"), 0.45);
    if (!durch) {
        cr->pop_group_to_source();
        cr->paint_with_alpha(0.3);
        schrift(cr, fg, x0, 20, TR("Stege nur beim Ausschneiden (Tiefe ab Materialdicke)"), 0.9, true);
        return;
    }
    if (anzahl == 0) {
        schrift(cr, fg, x0, 20, TR("Keine Haltestege – Teile können sich lösen und wegfliegen"), 0.9, true);
    } else {
        schrift(cr, fg, x0, 20, std::to_string(anzahl) + (anzahl == 1 ? TR(" Steg · ") : TR(" Stege · ")) + zahl(breite) +
                                    TR(" mm breit · ") + zahl(hoehe) + TR(" mm hoch"), 0.9, true);
    }
}

// ---------------------------------------------------------------------------
// Zustellungen mit Art (Peter, 20.10.): gleichmässig, letzte kleiner, eigene Liste, ein Durchgang.
// Gleiche Regeln wie hirch_cam/ops/bahn.py zustellungen().
// ---------------------------------------------------------------------------
/// Dicke jeder Zustellung von oben nach unten (Summe = weg)
inline std::vector<double> zustell_stufen(double weg, std::string const &modus, double z, double letzte,
                                          std::vector<double> const &liste)
{
    std::vector<double> aus;
    if (weg <= 1e-9) return {0.0};
    if (modus == "eins") return {weg};
    if (modus == "liste" && !liste.empty()) {
        double summe = 0;
        for (std::size_t i = 0; summe < weg - 1e-9 && i < 10000; ++i) {
            double v = std::min(liste[std::min(i, liste.size() - 1)], weg - summe);
            aus.push_back(v);
            summe += v;
        }
        return aus;
    }
    if (modus == "kleiner" && z > 1e-9 && letzte > 1e-9 && letzte < weg - 1e-9) {
        double rest = weg - letzte;
        int n = std::max(1, (int)std::ceil(rest / z - 1e-9));
        aus.assign(n, rest / n);
        aus.push_back(letzte);
        return aus;
    }
    if (z <= 1e-9) z = weg;
    int n = std::max(1, (int)std::ceil(weg / z - 1e-9));
    aus.assign(n, weg / n);
    return aus;
}

/// «0,7; 0,5 0,3» → {0.7, 0.5, 0.3} (Komma oder Punkt, getrennt durch ; Leerzeichen oder ·)
inline std::vector<double> zustell_liste_lesen(std::string const &t)
{
    std::vector<double> aus;
    std::string zahl_;
    auto fertig = [&] {
        if (zahl_.empty()) return;
        double v = g_ascii_strtod(zahl_.c_str(), nullptr);
        if (v > 1e-9) aus.push_back(v);
        zahl_.clear();
    };
    for (char c : t) {
        if (std::isdigit((unsigned char)c)) zahl_ += c;
        else if ((c == ',' || c == '.') && !zahl_.empty() && zahl_.find('.') == std::string::npos) zahl_ += '.';
        else fertig();
    }
    fertig();
    return aus;
}

inline std::string zustell_liste_text(std::vector<double> const &v)
{
    std::string t;
    for (auto x : v) t += (t.empty() ? "" : "; ") + zahl(x);
    return t;
}

/// Zustellungen einer gespeicherten Bearbeitung (für die grob nachgeführte Zeit)
inline std::vector<double> zustell_stufen_von(bj::object const &b, double weg)
{
    std::vector<double> liste;
    if (auto a = b.if_contains("zustell_liste"); a && a->is_array()) {
        for (auto const &v : a->as_array())
            if (v.is_number() && v.to_number<double>() > 0) liste.push_back(v.to_number<double>());
    }
    std::string modus;
    if (auto m = b.if_contains("zustell_modus"); m && m->is_string()) modus = std::string(m->get_string());
    double z = 0;
    if (auto v = b.if_contains("zustellung"); v && v->is_number()) z = v->to_number<double>();
    double l = 0;
    if (auto v = b.if_contains("letzte_zustellung"); v && v->is_number()) l = v->to_number<double>();
    return zustell_stufen(weg, modus, z, l, liste);
}

/// Schnitt mit den Ebenen der Zustellungen; die letzte durchgezogen
inline void skizze_zustellungen(Cr const &cr, double w, double h, Gdk::RGBA const &fg, std::vector<double> const &stufen,
                                double ab)
{
    double weg = 0;
    for (auto s : stufen) weg += s;
    double tiefe = ab + weg;
    double oben = 18, unten = h - 6;
    double skala = (unten - oben) / std::max(tiefe * 1.15, 0.05);
    double x0 = 8, x1 = w * 0.62;
    farbe(cr, HOLZ);
    cr->rectangle(x0, oben, w - 2 * x0, unten - oben);
    cr->fill();
    cr->set_source_rgb(1, 1, 1);
    cr->rectangle(x0 + 30, oben, x1 - x0 - 60, tiefe * skala);
    cr->fill();
    double y = oben + ab * skala;
    if (ab > 1e-9) {
        cr->set_source_rgb(0.55, 0.55, 0.55);
        cr->set_line_width(1);
        cr->set_dash(std::vector<double>{2, 3}, 0);
        cr->move_to(x0 + 24, y);
        cr->line_to(x1 - 24, y);
        cr->stroke();
        cr->unset_dash();
    }
    std::size_t n = stufen.size();
    for (std::size_t i = 0; i < n; ++i) {
        y += stufen[i] * skala;
        bool letzte = i + 1 == n;
        cr->set_line_width(letzte ? 2 : 1.4);
        cr->set_source_rgb(letzte ? 0.14 : 0.11, letzte ? 0.12 : 0.44, letzte ? 0.19 : 0.85);
        if (!letzte) cr->set_dash(std::vector<double>{4, 3}, 0);
        cr->move_to(x0 + 24, y);
        cr->line_to(x1 - 24, y);
        cr->stroke();
        cr->unset_dash();
        if (n <= 8 || i < 3 || letzte) {          // auf dem Material: dunkle Schrift
            cr->set_source_rgb(0.25, 0.18, 0.1);
            cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_NORMAL);
            cr->set_font_size(11);
            cr->move_to(x1 - 18, y + 4);
            cr->show_text(einheit_text_zahlen(std::to_string(i + 1) + " · " + zahl(stufen[i]) + " mm"));
        }
    }
    schrift(cr, fg, x0, 12, std::to_string(n) + (n == 1 ? TR(" Zustellung bis ") : TR(" Zustellungen bis ")) +
                                zahl(tiefe) + " mm", 0.9, true);
}

/// ISO-Regelgewinde: Steigung zum Nenn-Ø (± 0,15 mm), sonst 0 – wie hirch_cam/ops/gewinde.py
inline double regelsteigung(double d)
{
    static std::vector<std::pair<double, double>> const R = {
        {1.0, 0.25}, {1.2, 0.25}, {1.6, 0.35}, {2.0, 0.4}, {2.5, 0.45}, {3.0, 0.5}, {3.5, 0.6}, {4.0, 0.7},
        {5.0, 0.8}, {6.0, 1.0}, {8.0, 1.25}, {10.0, 1.5}, {12.0, 1.75}, {14.0, 2.0}, {16.0, 2.0}, {18.0, 2.5},
        {20.0, 2.5}, {22.0, 2.5}, {24.0, 3.0}, {27.0, 3.0}, {30.0, 3.5}, {36.0, 4.0}};
    for (auto const &[n, p] : R)
        if (std::abs(n - d) <= 0.15) return p;
    return 0.0;
}

/// Innengewinde, Kreis = Kernloch (Fräsbahnen 25.10., wie ops/gewinde.aus_kreis): passt Ø auf das Kernloch eines
/// Regelgewindes (Bohrer nach DIN 336: D − 1,0825 P oder D − P, ± 0,12)? Liefert Nenn-Ø und Steigung, sonst 0.
/// Mit eigener Steigung p > 0 zählt nur ein Gewinde mit dieser Steigung.
inline double gewinde_nenn_aus_kernloch(double d, double &p)
{
    static double const N[] = {1.0, 1.2, 1.6, 2.0, 2.5, 3.0, 3.5, 4.0, 5.0, 6.0, 8.0, 10.0, 12.0, 14.0, 16.0, 18.0,
                               20.0, 22.0, 24.0, 27.0, 30.0, 36.0};
    for (double n : N) {
        double r = regelsteigung(n);
        double q = p > 0 ? p : r;
        if (std::abs(d - (n - 1.0825 * q)) <= 0.12 || std::abs(d - (n - q)) <= 0.12) {
            p = q;
            return n;
        }
    }
    return 0.0;
}

inline std::string gewinde_name(double d, double p)
{
    double r = regelsteigung(d);
    if (r > 0) {
        double n = std::round(d * 10) / 10;
        return "M" + zahl(n, n == std::floor(n) ? 0 : 1) + (std::abs(r - p) < 1e-6 ? std::string() : " × " + zahl(p));
    }
    return "Ø " + zahl(d, 1) + " × " + zahl(p);
}

/// Felder für die Art der Zustellungen (unter dem Feld «Zustellung»): Auswahl, letzte, Liste
struct ZustellArt {
    Gtk::ComboBoxText *modus = nullptr;
    Gtk::SpinButton *zust = nullptr;       // Feld des Dialogs: je Durchgang bzw. höchstens
    Gtk::Label *zust_text = nullptr;       // dessen Beschriftung
    std::string zust_text_normal;
    Gtk::SpinButton *letzte = nullptr;
    Gtk::Entry *liste = nullptr;
    std::string art() const { return modus ? std::string(modus->get_active_id().raw()) : std::string(); }
    std::vector<double> liste_werte() const { return liste ? zustell_liste_lesen(liste->get_text().raw()) : std::vector<double>{}; }
    std::vector<double> stufen(double weg) const
    {
        return zustell_stufen(weg, art(), zust ? zust->get_value() : weg, letzte ? letzte->get_value() : 0, liste_werte());
    }
    /// Zustellungen kurz: «3 × 0,50 mm» oder «0,60 · 0,60 · 0,30 mm»
    std::string kurz(double weg) const
    {
        auto st = stufen(weg);
        bool gleich = true;
        for (auto v : st) gleich = gleich && std::abs(v - st.front()) < 1e-6;
        if (st.size() == 1) return TR("1 Zustellung") + std::string(" · ") + zahl(st.front()) + " mm";
        if (gleich) return std::to_string(st.size()) + " × " + zahl(st.front()) + " mm";
        std::string t;
        for (std::size_t i = 0; i < st.size(); ++i) {
            if (i >= 5 && st.size() > 6) {
                t += " · …";
                break;
            }
            t += (i ? " · " : "") + zahl(st[i]);
        }
        return t + " mm";
    }
    void nachfuehren() const
    {
        auto m = art();
        zeile_zeigen(letzte, m == "kleiner");
        zeile_zeigen(liste, m == "liste");
        zeile_zeigen(zust, m != "eins" && m != "liste");
        if (zust_text) zust_text->set_text(m == "kleiner" ? std::string(TR("Zustellung höchstens")) : zust_text_normal);
    }
    void lesen_in(bj::object &b) const
    {
        auto m = art();
        if (m.empty()) {
            b.erase("zustell_modus");
        } else {
            b["zustell_modus"] = m;
        }
        if (letzte) b["letzte_zustellung"] = letzte->get_value();
        bj::array a;
        for (auto v : liste_werte()) a.push_back(v);
        if (!a.empty()) b["zustell_liste"] = a;
        else b.erase("zustell_liste");
    }
};

/// Schruppen und Schlichten im Schnitt durch die Wand: zwei Schritte nebeneinander
/// B-18 (Peter 02.11., Variante C): Schnitt durch Material und 3D-Modell – wovon die Tiefe zählt.
/// art: material | modell | bis_modell | haut
inline void skizze_tiefe_ab(Cr const &cr, double w, double h, Gdk::RGBA const &fg, std::string const &art)
{
    double x0 = 10, x1 = w - 10, top = 14, unten = h - 10, mitte = (top + unten) / 2;
    double xa = x0 + (x1 - x0) * 0.22, xb = x0 + (x1 - x0) * 0.78;   // Modell
    auto modell_y = [&](double x, double hoehe) {                      // Oberseite des Modells (Bogen)
        double t = std::clamp((x - xa) / (xb - xa), 0.0, 1.0);
        return mitte - hoehe * std::sin(M_PI * t);
    };
    double hoehe = (mitte - top) * 0.75;
    auto modell_pfad = [&](bool oben_und_unten) {
        cr->move_to(xa, mitte);
        for (int i = 0; i <= 40; ++i) {
            double x = xa + (xb - xa) * i / 40.0;
            cr->line_to(x, modell_y(x, hoehe));
        }
        if (oben_und_unten) {
            for (int i = 40; i >= 0; --i) {
                double x = xa + (xb - xa) * i / 40.0;
                cr->line_to(x, 2 * mitte - modell_y(x, hoehe));
            }
        } else {
            cr->line_to(xb, unten);
            cr->line_to(xa, unten);
        }
        cr->close_path();
    };
    // Beschriftung auf Holz und hellem Grund immer dunkel (Windows 02.11.: mit dunklem Thema kaum lesbar)
    Gdk::RGBA dunkel;
    dunkel.set_rgba(0.15, 0.12, 0.08, 1.0);
    auto bahn = [&](double a = 1.0) {
        farbe(cr, BAHN, a);
        cr->set_line_width(2.5);
    };
    if (art == "haut") {
        // zweiseitig: Modell in der Mitte, rundum ausgeräumt bis auf die dünne Haut auf der Trennlinie
        farbe(cr, HOLZ);
        cr->rectangle(x0, top, xa - 18 - x0, unten - top);
        cr->rectangle(xb + 18, top, x1 - xb - 18, unten - top);
        cr->fill();
        cr->rectangle(xa - 18, mitte - 1.5, 18, 3);
        cr->rectangle(xb, mitte - 1.5, 18, 3);
        cr->fill();
        cr->set_source_rgba(0.62, 0.62, 0.64, 1);
        modell_pfad(true);
        cr->fill();
        bahn();
        for (double x : {xa - 8, xb + 8}) {
            cr->move_to(x, mitte - 9);
            cr->line_to(x, mitte + 9);
        }
        cr->stroke();
        schrift(cr, dunkel, xb + 22, mitte - 8, TR("Haut"), 0.8);
        return;
    }
    farbe(cr, HOLZ);
    cr->rectangle(x0, top, x1 - x0, unten - top);
    cr->fill();
    if (art == "bis_modell") {
        // von oben ausgeräumt, bis der Fräser am Modell liegt
        double pa = x0 + (x1 - x0) * 0.30, pb = x0 + (x1 - x0) * 0.70;
        cr->set_source_rgba(1, 1, 1, 0.9);
        cr->move_to(pa, top);
        cr->line_to(pb, top);
        for (int i = 40; i >= 0; --i) {
            double x = pa + (pb - pa) * i / 40.0;
            cr->line_to(x, modell_y(x, hoehe));
        }
        cr->close_path();
        cr->fill();
    }
    if (art != "material") {
        cr->set_source_rgba(0.62, 0.62, 0.64, 1);
        modell_pfad(false);
        cr->fill();
        schrift(cr, fg, xa + 6, unten - 6, TR("3D-Modell"), 0.8);
    }
    bahn();
    if (art == "material") {
        cr->move_to(x0 + 30, top + 18);
        cr->line_to(x1 - 30, top + 18);
        cr->stroke();
        schrift(cr, dunkel, x1 - 120, top + 32, TR("Tiefe ab oben, flach"), 0.8);
    } else if (art == "modell") {
        double ab = 7;
        cr->move_to(xa + 10, modell_y(xa + 10, hoehe) + ab);
        for (int i = 1; i <= 30; ++i) {
            double x = xa + 10 + (xb - xa - 20) * i / 30.0;
            cr->line_to(x, modell_y(x, hoehe) + ab);
        }
        cr->stroke();
        schrift(cr, dunkel, xb + 8, mitte - 4, TR("gleich tief"), 0.8);
        schrift(cr, dunkel, xb + 8, mitte + 9, TR("unter dem Modell"), 0.8);
    } else {
        double pa = x0 + (x1 - x0) * 0.30, pb = x0 + (x1 - x0) * 0.70;
        cr->move_to(pa, top);
        for (int i = 0; i <= 40; ++i) {
            double x = pa + (pb - pa) * i / 40.0;
            cr->line_to(x, modell_y(x, hoehe));
        }
        cr->line_to(pb, top);
        cr->stroke();
        schrift(cr, dunkel, pb + 8, top + 14, TR("bis aufs Modell"), 0.8);
    }
}

/// Satz unter der Skizze «Wie tief»
inline std::string tiefe_ab_satz(std::string const &art)
{
    if (art == "modell") return TR("Die Bahn folgt der Fläche des Modells – überall gleich tief darunter (z. B. Rille, Schrift).");
    if (art == "bis_modell")
        return TR("Fräst von oben in Ebenen hinunter, bis der Fräser am Modell liegt. Die Tiefe ist die Grenze.");
    if (art == "haut") return TR("Trennt die dünne Haut um das zweiseitige Modell durch. Die Tiefe rechnet Hirch CAM: "
                                 "Dicke der Haut + 0,3 mm + Abstand zum Modell.");
    return TR("Die Tiefe zählt flach ab der Oberseite des Materials.");
}

inline void skizze_schlichten(Cr const &cr, double w, double h, Gdk::RGBA const &fg, bool aktiv, double zugabe,
                              bool anderes)
{
    double pw = (w - 24) / 2, top = 30, yb = h - 18, zp = std::clamp(zugabe * 25.0, 5.0, 22.0);
    for (int schritt = 0; schritt < 2; ++schritt) {
        double x0 = 8 + schritt * (pw + 8), xw = x0 + pw * 0.62;
        bool grau = schritt == 1 && !aktiv;
        if (grau) cr->push_group();
        farbe(cr, HOLZ);
        cr->rectangle(xw, top, x0 + pw - xw, yb - top + 10);
        cr->rectangle(x0, yb, xw - x0, 10);
        cr->fill();
        double wand = xw;
        if (aktiv) {
            if (schritt == 0) {
                cr->set_source_rgba(0.93, 0.55, 0.15, 0.75);   // Zugabe bleibt stehen
                cr->rectangle(xw - zp, top, zp, yb - top);
                cr->fill();
                wand = xw - zp;
            } else {
                cr->set_source_rgba(0.93, 0.55, 0.15, 0.9);    // hier war die Zugabe
                cr->set_line_width(1);
                std::vector<double> strich{3, 2};
                cr->set_dash(strich, 0);
                cr->rectangle(xw - zp + 0.5, top + 0.5, zp - 1, yb - top - 1);
                cr->stroke();
                cr->unset_dash();
            }
        }
        double R = (schritt == 1 && anderes) ? 12 : 20;
        if (schritt == 0) cr->set_source_rgba(0.45, 0.45, 0.5, 0.45);
        else farbe(cr, BAHN, 0.45);
        cr->rectangle(wand - 2 * R, top - 6, 2 * R, yb - top + 6);
        cr->fill();
        if (grau) {
            cr->pop_group_to_source();
            cr->paint_with_alpha(0.3);
        }
        std::string titel = schritt == 0 ? TR("1 · Schruppen") : (aktiv ? TR("2 · Schlichten") : TR("2 · Schlichten (aus)"));
        schrift(cr, fg, x0 + 4, 16, titel, 0.9, true);
        if (schritt == 0 && aktiv) schrift(cr, fg, xw + 6, top + 16, TR("Zugabe"), 0.8);
        if (schritt == 0 && aktiv) schrift(cr, fg, xw + 6, top + 30, zahl(zugabe) + TR(" mm"), 0.8);
        if (schritt == 0 && !aktiv) schrift(cr, fg, xw + 6, top + 16, TR("direkt"), 0.8);
        if (schritt == 0 && !aktiv) schrift(cr, fg, xw + 6, top + 30, TR("auf Mass"), 0.8);
        if (schritt == 1 && aktiv) schrift(cr, fg, xw + 6, top + 16, anderes ? TR("eigener") : TR("auf Mass"), 0.8);
        if (schritt == 1 && aktiv && anderes) schrift(cr, fg, xw + 6, top + 30, TR("Fräser"), 0.8);
    }
}

/// Attribut einer Gravurtext-Gruppe: Einstellungen als JSON (zum Ändern)
/// Zeit kurz: 5:14 (Minuten:Sekunden) oder 1:05 h
inline std::string kurzzeit(double minuten)
{
    long s = std::lround(minuten * 60.0);
    char buf[32];
    if (s >= 3600) std::snprintf(buf, sizeof buf, "%ld:%02ld h", s / 3600, (s % 3600) / 60);
    else std::snprintf(buf, sizeof buf, "%ld:%02ld", s / 60, s % 60);
    return buf;
}

/// Schnittbild einer Bearbeitung: Material von der Seite, die Fräsung in ihrer
/// Farbe, weitere Zustellungen gestrichelt.
inline void schnitt_zeichnen(Cr const &cr, int w, int h, std::string const &art, std::string const &teil,
                             double tiefe, double dicke, int n, bool durch, bool stege, bool pass,
                             double r, double g, double b)
{
    double const x0 = 1, y0 = 5, bw = w - 2, bh = h - 9;
    double const k = bh / std::max(1.0, dicke);
    double t = std::clamp(tiefe * k, 2.0, bh + 4);
    if (durch) t = bh + 4;
    double const cx = x0 + bw / 2;
    std::string form = art;
    if (art == "gravur" || art == "vcarve" || art == "fase") form = "v";
    if (teil == "flach") form = "tasche";
    if (art == "planen") form = "planen";
    if (am_modell(art)) {
        form = "relief";
        t = bh * 0.7;
    }
    if (pass) form = "pass";
    // Umriss der Fräsung (offener Pfad oben)
    auto pfad = [&](bool zu) {
        if (form == "v") {
            double halb = std::clamp(t * 0.9, 3.0, bw * 0.3);
            cr->move_to(cx - halb, y0);
            cr->line_to(cx, y0 + t);
            cr->line_to(cx + halb, y0);
        } else if (form == "tasche") {
            double l = x0 + bw * 0.18, rr = x0 + bw * 0.82;
            cr->move_to(l, y0);
            cr->line_to(l, y0 + t);
            cr->line_to(rr, y0 + t);
            cr->line_to(rr, y0);
        } else if (form == "planen") {
            cr->move_to(x0, y0);
            cr->line_to(x0, y0 + std::min(t, 3.0));
            cr->line_to(x0 + bw, y0 + std::min(t, 3.0));
            cr->line_to(x0 + bw, y0);
        } else if (form == "pass") {
            for (double f : {0.25, 0.75}) {
                cr->move_to(x0 + bw * f - 3, y0);
                cr->line_to(x0 + bw * f - 3, y0 + t);
                cr->line_to(x0 + bw * f + 3, y0 + t);
                cr->line_to(x0 + bw * f + 3, y0);
                if (zu) cr->close_path();
            }
            return;
        } else if (form == "relief") {             // 3D-Fläche: Hügel aus dem Block
            cr->move_to(x0, y0);
            cr->line_to(x0, y0 + t);
            cr->curve_to(x0 + bw * 0.3, y0 + t, x0 + bw * 0.3, y0 + 2, x0 + bw * 0.5, y0 + 2);
            cr->curve_to(x0 + bw * 0.75, y0 + 2, x0 + bw * 0.7, y0 + t * 0.6, x0 + bw, y0 + t * 0.6);
            cr->line_to(x0 + bw, y0);
        } else if (form == "bohren" || form == "gewinde") {
            cr->move_to(cx - 3, y0);
            cr->line_to(cx - 3, y0 + t);
            cr->line_to(cx + 3, y0 + t);
            cr->line_to(cx + 3, y0);
        } else {                                   // Kontur: schmaler Schnitt
            cr->move_to(cx - 4, y0);
            cr->line_to(cx - 4, y0 + t);
            cr->line_to(cx + 4, y0 + t);
            cr->line_to(cx + 4, y0);
        }
        if (zu) cr->close_path();
    };
    cr->save();
    cr->push_group();
    rundrechteck(cr, x0, y0, bw, bh, 2);
    farbe(cr, HOLZ);
    cr->fill_preserve();
    farbe(cr, HOLZ_RAND);
    cr->set_line_width(1);
    cr->stroke();
    cr->set_operator(Cairo::OPERATOR_CLEAR);
    pfad(true);
    cr->fill();
    cr->set_operator(Cairo::OPERATOR_OVER);
    if (form == "kontur" && durch && stege) {      // Haltesteg unten
        cr->rectangle(cx - 4, y0 + bh - 6, 8, 6);
        farbe(cr, HOLZ);
        cr->fill_preserve();
        farbe(cr, HOLZ_RAND);
        cr->stroke();
    }
    cr->pop_group_to_source();
    cr->paint();
    cr->set_source_rgb(r, g, b);
    cr->set_line_width(1.5);
    cr->set_line_join(Cairo::LINE_JOIN_ROUND);
    pfad(false);
    cr->stroke();
    if (n > 1 && (form == "tasche" || form == "kontur" || form == "pass" || form == "bohren")) {
        std::vector<double> strich = {2.5, 2.0};
        cr->set_dash(strich, 0);
        cr->set_line_width(1);
        double tt = std::min(t, bh);
        for (int i = 1; i < n; ++i) {
            double y = y0 + tt * i / n;
            if (form == "tasche") {
                cr->move_to(x0 + bw * 0.18, y);
                cr->line_to(x0 + bw * 0.82, y);
            } else if (form == "pass") {
                for (double f : {0.25, 0.75}) {
                    cr->move_to(x0 + bw * f - 3, y);
                    cr->line_to(x0 + bw * f + 3, y);
                }
            } else {
                double hb = form == "kontur" ? 4 : 3;
                cr->move_to(cx - hb, y);
                cr->line_to(cx + hb, y);
            }
        }
        cr->stroke();
    }
    cr->restore();
}

/// Begrüssung beim Start: geplant oder offen – das Einrichten neuer Zeichnungen wartet darauf
inline bool willkommen_wartet = false;
inline std::vector<std::function<void()>> nach_willkommen;

inline void nach_der_begruessung(std::function<void()> f)
{
    if (willkommen_wartet) nach_willkommen.push_back(std::move(f));
    else Glib::signal_idle().connect_once(std::move(f));
}

inline char const *const GRAVUR = "hirch:gravurtext";

} // namespace Inkscape::UI::Dialog

#endif
