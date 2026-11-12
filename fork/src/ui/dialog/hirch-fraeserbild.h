// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Fräserbilder für die Werkzeugwahl (Karten).
 *
 * Flache Seitenansicht ohne Umriss («Variante B», Peter 19.10.2026, Mockup «Hirch CNC –
 * Fräserbilder»): Schaft, Schneide, Spannuten als Wendel mit Spanfläche und Schneidkante, je
 * nach Art Kugel, Eckradien, Kegel, Wendeplatten oder Bohrerspitze. Das Bild entsteht aus den
 * Angaben des Fräsers (Art, Schneiden, Drall, Wendeplatten, Spanbrecher), nicht aus einer Datei.
 * Gezeichnet wird in einem Raster von 40 × 120; die Formen sind SVG-Pfade (wie im Mockup), die
 * `pfad_zeichnen` für Cairo liest (M L H V Q A Z, a relativ).
 */
#ifndef HIRCH_FRAESERBILD_H
#define HIRCH_FRAESERBILD_H

#include <algorithm>
#include <cairomm/context.h>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <glib.h>
#include <string>
#include <vector>

namespace Inkscape::UI::Dialog::fraeserbild {

/// Merkmale, aus denen das Bild entsteht
struct Merkmale {
    std::string art = "schaft";   // schaft | schrupp | kugel | konisch | eck | v | stichel | plan | bohrer | gewinde
    int schneiden = 2;
    char drall = 'R';             // R aufwärts, L abwärts, K Kompression
};

struct Form {
    std::string d;
    std::string fill, stroke;     // "#rrggbb" oder leer
    double sw = 0;
    int clip = 0;                 // 0 keiner, 1 Körper, 2 Körper ab Auslauf, 3 Kompression unten, 4 oben
    bool strich = false;          // gestrichelt
};

struct Bild {
    std::vector<Form> formen;
    std::string koerper, auslauf, unten, oben;
};

inline std::string n2(double v)
{
    // immer mit Punkt – snprintf schriebe unter deutscher Spracheinstellung ein Komma (Peter, 20.10.)
    char b[G_ASCII_DTOSTR_BUF_SIZE];
    return g_ascii_formatd(b, sizeof b, "%.2f", v);
}

inline Bild bauen(Merkmale const &m, bool hell)
{
    Bild B;
    auto P = [&](std::string d, std::string fill, std::string stroke = "", double sw = 0, int clip = 0,
                 bool strich = false) { B.formen.push_back({std::move(d), std::move(fill), std::move(stroke), sw, clip, strich}); };
    auto const &art = m.art;
    int n = std::max(1, m.schneiden);
    bool bohrer = art == "bohrer", plan = art == "plan";
    std::string schaftF = bohrer ? "#c7bea9" : "#cfcecb", schaftKante = bohrer ? "#ddd6c6" : "#e6e5e2",
                koerperF = bohrer ? "#b8ae97" : "#aeadb2", nut = "#46454d", span = "#6c6b73", kante = "#ffffff",
                fase = "#e2dccd", spitze = bohrer ? "#d6cebd" : "#c4c3c7", akzent = hell ? "#c88800" : "#f6d32d",
                gold = "#e5a50a", goldHell = "#f8d775", schraube = "#3d3846";
    double const cx = 20, r = 10, yb = 114, rk = 3.6;   // rk: Kugel des konischen Kugelfräsers
    double y0 = plan ? 62 : 50;
    double ys0 = plan ? 70 : (art == "v" ? 60 : art == "stichel" ? 62 : 58);
    auto N = n2;
    std::string schaftPfad = plan ? "M14.6 4H25.4L27 5.6V" + N(y0) + "H13V5.6Z"
                                  : "M11.6 4H28.4L30 5.6V" + N(y0) + "H10V5.6Z";
    std::string koerper;
    if (art == "schrupp") {
        // Spanbrecher: kleine Kerben in der Kontur
        std::string l = "M10 " + N(yb), rr;
        for (double y = yb - 4; y > ys0 + 2; y -= 5.5) l += "L10 " + N(y + 1.1) + "A1.1 1.1 0 0 0 10 " + N(y - 1.1);
        for (double y = ys0 + 4.5; y < yb - 2; y += 5.5) rr += "L30 " + N(y - 1.1) + "A1.1 1.1 0 0 0 30 " + N(y + 1.1);
        koerper = l + "L10 " + N(y0) + "H30" + rr + "L30 " + N(yb) + "Z";
    } else if (art == "gewinde") {
        // Gewindefräser: schmaler Hals, unten die Zähne (Peter, 20.10.)
        double zt = yb - 16;
        std::string r1 = "M10 " + N(y0) + "H30L23.5 " + N(y0 + 5) + "V" + N(zt), l1;
        for (int i = 0; i < 3; ++i) r1 += "L30 " + N(zt + i * 5 + 2.5) + "L23.5 " + N(zt + (i + 1) * 5);
        r1 += "V" + N(yb) + "H16.5V" + N(zt + 15);
        for (int i = 2; i >= 0; --i) l1 += "L10 " + N(zt + i * 5 + 2.5) + "L16.5 " + N(zt + i * 5);
        koerper = r1 + l1 + "V" + N(y0 + 5) + "Z";
    } else if (art == "konisch") {
        // konischer Kugelfräser: Kegel bis zur kleinen Kugel an der Spitze (Peter, 20.10.)
        koerper = "M10 " + N(y0) + "H30V" + N(ys0) + "L" + N(cx + rk) + " " + N(yb - rk) + "A" + N(rk) + " " + N(rk) +
                  " 0 0 1 " + N(cx - rk) + " " + N(yb - rk) + "L10 " + N(ys0) + "Z";
    } else if (art == "kugel") {
        koerper = "M10 " + N(y0) + "H30V" + N(yb - r) + "A10 10 0 0 1 10 " + N(yb - r) + "Z";
    } else if (art == "eck") {
        koerper = "M10 " + N(y0) + "H30V" + N(yb - 4) + "A4 4 0 0 1 26 " + N(yb) + "H14A4 4 0 0 1 10 " + N(yb - 4) + "Z";
    } else if (bohrer) {
        koerper = "M10 " + N(y0) + "H30V" + N(yb - 10) + "L20.6 " + N(yb - 0.5) + "Q20 " + N(yb + 0.2) + " 19.4 " +
                  N(yb - 0.5) + "L10 " + N(yb - 10) + "Z";
    } else if (art == "v") {
        koerper = "M10 " + N(y0) + "H30V" + N(ys0 + 2) + "L20.5 " + N(yb) + "H19.5L10 " + N(ys0 + 2) + "Z";
    } else if (art == "stichel") {
        koerper = "M10 " + N(y0) + "H30V" + N(ys0 + 4) + "L21.8 " + N(yb) + "H18.2L10 " + N(ys0 + 4) + "Z";
    } else if (plan) {
        koerper = "M13 " + N(y0) + "H27L29 66H34L36 69V106H4V69L6 66H11Z";
    } else if (n == 1) {
        koerper = "M10 " + N(y0) + "H30V" + N(yb) + "L10 " + N(yb - 5) + "Z";      // Einschneider: Stirn schräg
    } else {
        koerper = "M10 " + N(y0) + "H30V" + N(yb - 0.8) + "L29.2 " + N(yb) + "H10.8L10 " + N(yb - 0.8) + "Z";
    }
    double yk = yb - 18;   // Kompression: Wechsel der Drallrichtung
    B.koerper = koerper;
    B.auslauf = "M0 " + N(ys0 + 3) + "Q20 " + N(ys0 - 5) + " 40 " + N(ys0 + 3) + "V120H0Z";
    B.unten = "M0 " + N(yk) + "H40V120H0Z";
    B.oben = "M0 " + N(ys0 + 3) + "Q20 " + N(ys0 - 5) + " 40 " + N(ys0 + 3) + "V" + N(yk) + "H0Z";

    P(schaftPfad, schaftF);
    P(plan ? "M14.6 4H17V" + N(y0) + "H13V5.6Z" : "M11.6 4H14V" + N(y0) + "H10V5.6Z", schaftKante);
    P(koerper, koerperF);
    auto pfad = [&](std::vector<std::pair<double, double>> const &pts) {
        std::string s;
        for (std::size_t i = 0; i < pts.size(); ++i) s += (i ? "L" : "") + N(pts[i].first) + " " + N(pts[i].second);
        return s;
    };
    auto wendel = [&](double richtung, int clip, double steig, double breite, int nn, bool mit_fase) {
        auto kurve = [&](double ys) {
            std::vector<std::pair<double, double>> p;
            for (int i = 0; i <= 16; ++i) {
                double th = -M_PI / 2 + i * M_PI / 16;
                p.emplace_back(cx + richtung * r * std::sin(th), ys - steig * (th + M_PI / 2) / (2 * M_PI));
            }
            return p;
        };
        auto umgekehrt = [](std::vector<std::pair<double, double>> v) { return std::vector<std::pair<double, double>>(v.rbegin(), v.rend()); };
        auto verbinden = [](std::vector<std::pair<double, double>> a, std::vector<std::pair<double, double>> const &b) {
            a.insert(a.end(), b.begin(), b.end());
            return a;
        };
        std::string nuten, sp, kanten, fasen;
        for (double ys = yb + steig; ys > ys0 - steig; ys -= steig / nn) {
            double b = steig / nn * breite;
            auto k1 = kurve(ys), ks = kurve(ys - 1.4), k2 = kurve(ys - b);
            nuten += "M" + pfad(verbinden(ks, umgekehrt(k2))) + "Z";
            sp += "M" + pfad(verbinden(k1, umgekehrt(ks))) + "Z";
            kanten += "M" + pfad(k1);
            if (mit_fase) fasen += "M" + pfad(verbinden(kurve(ys + 1.8), umgekehrt(k1))) + "Z";
        }
        if (mit_fase) P(fasen, fase, "", 0, clip);
        P(nuten, nut, "", 0, clip);
        P(sp, span, "", 0, clip);
        P(kanten, "", kante, 0.8, clip);
    };
    if (art == "v") {
        P("M20 " + N(ys0 - 2) + "Q25 " + N(ys0 - 4) + " 30 " + N(ys0 - 1) + "V" + N(ys0 + 2) + "L20.5 " + N(yb) + "H20Z", nut, "", 0, 1);
        P("M20 " + N(ys0 - 2) + "H21.4L20.6 " + N(yb - 1) + "H20Z", span, "", 0, 1);
        P("M20 " + N(ys0 - 2) + "V" + N(yb - 0.6), "", kante, 0.9, 1);
    } else if (art == "stichel") {
        P("M20 " + N(ys0) + "H30V" + N(ys0 + 4) + "L21.8 " + N(yb) + "H20Z", "#e6e5e2", "", 0, 1);
        P("M20 " + N(ys0) + "H30", "", nut, 1, 1);
        P("M20 " + N(ys0) + "V" + N(yb), "", "#77767b", 0.7, 1);
    } else if (plan) {
        P("M4 92H11.5V106H4Z M28.5 92H36V106H28.5Z M15 92H25V106H15Z", nut);
        P("M4.6 106V98.2H10.8V106Z M35.4 106V98.2H29.2V106Z", gold);
        P("M4.6 98.2H10.8V99.6H4.6Z M29.2 98.2H35.4V99.6H29.2Z", goldHell);
        P("M15.6 106V97.6H24.4V106Z", gold);
        P("M15.6 97.6H24.4V99H15.6Z", goldHell);
        P("M7.7 102.4a1.2 1.2 0 1 0 0.01 0Z M32.3 102.4a1.2 1.2 0 1 0 0.01 0Z M20 102.2a1.3 1.3 0 1 0 0.01 0Z", schraube);
        P("M4 106H36", "", kante, 0.8);
    } else if (art != "gewinde") {
        double steig = bohrer ? 38 : 48;
        double breite = bohrer ? 0.6 : (n == 1 ? 0.56 : n == 2 ? 0.46 : n == 3 ? 0.42 : 0.38);
        if (m.drall == 'K') {
            wendel(1, 3, steig, breite, n, false);
            wendel(-1, 4, steig, breite, n, false);
        } else {
            wendel(m.drall == 'L' ? -1 : 1, 2, steig, breite, n, bohrer);
        }
        if (n == 1 && art == "schaft") P("M10.5 " + N(yb - 4.6) + "L29.6 " + N(yb - 0.2), "", kante, 0.9);
        if ((art == "schaft" && n > 1) || art == "schrupp") {
            std::string z;
            int k_ = std::min(n, 4);
            for (int k = 0; k < k_; ++k) {
                double x = 12 + (k + 0.5) * 16.0 / k_;
                z += "M" + N(x - 1.6) + " " + N(yb) + "L" + N(x) + " " + N(yb - 3) + "L" + N(x + 1.6) + " " + N(yb) + "Z";
            }
            P(z, nut, "", 0, 1);
        }
    }
    if (art == "kugel") {
        P("M10.7 " + N(yb - r + 1.5) + "Q13 " + N(yb - 0.6) + " 20 " + N(yb - 0.1) + "M29.3 " + N(yb - r - 0.5) + "Q28.4 " +
              N(yb - 2.6) + " 20 " + N(yb - 0.1), "", kante, 1.1, 1);
        P("M10.4 " + N(yb - r + 1) + "A9.6 9.6 0 0 0 29.6 " + N(yb - r + 1), "", akzent, 1.2);
    } else if (art == "gewinde") {
        double zt = yb - 16;
        P("M18.6 " + N(zt - 2) + "H21.4V" + N(yb) + "H18.6Z", nut, "", 0, 1);
        std::string sp;
        for (int i = 0; i < 3; ++i)
            sp += "M29 " + N(zt + i * 5 + 2.5) + "H30.6M9.4 " + N(zt + i * 5 + 2.5) + "H11";
        P(sp, "", akzent, 1.3);
    } else if (art == "konisch") {
        P("M" + N(cx - rk + 0.4) + " " + N(yb - rk) + "A" + N(rk - 0.4) + " " + N(rk - 0.4) + " 0 0 0 " + N(cx + rk - 0.4) + " " +
              N(yb - rk), "", akzent, 1.2);
        P("M" + N(cx + rk - 0.6) + " " + N(yb - rk - 2) + "Q" + N(cx + 1) + " " + N(yb - 1) + " " + N(cx) + " " + N(yb - 0.2), "", kante,
          0.9, 1);
    } else if (art == "eck") {
        P("M10.5 " + N(yb - 4) + "A3.5 3.5 0 0 0 14 " + N(yb - 0.5) + "M26 " + N(yb - 0.5) + "A3.5 3.5 0 0 0 29.5 " + N(yb - 4), "", akzent, 1.4);
        P("M14 " + N(yb - 0.6) + "H26", "", kante, 0.8);
    } else if (bohrer) {
        P("M10 " + N(yb - 10) + "L20 " + N(yb) + "L30 " + N(yb - 10) + "Z", spitze, "", 0, 1);
        P("M10.6 " + N(yb - 9.7) + "L19.4 " + N(yb - 1) + "M29.4 " + N(yb - 9.7) + "L20.6 " + N(yb - 1), "", akzent, 1.3);
        P("M19 " + N(yb - 2.4) + "L21 " + N(yb - 1.4), "", nut, 0.9);
    } else if (art == "stichel") {
        P("M18.4 " + N(yb - 0.7) + "H21.6", "", akzent, 1.4);
    } else if (art == "v") {
        P("M19.4 " + N(yb - 1.5) + "H20.6", "", akzent, 1.2);
    }
    if (m.drall == 'L') P("M34.5 70V90M31.8 86.5L34.5 90.5L37.2 86.5", "", akzent, 1.3);
    if (m.drall == 'K') {
        P("M10 " + N(yk) + "H30", "", akzent, 1.1, 1, true);
        P("M34.5 " + N(yk - 4) + "V" + N(yk - 16) + "M31.8 " + N(yk - 7.5) + "L34.5 " + N(yk - 3.5) + "L37.2 " + N(yk - 7.5) +
              "M34.5 " + N(yk + 4) + "V" + N(yk + 14) + "M31.8 " + N(yk + 7.5) + "L34.5 " + N(yk + 3.5) + "L37.2 " + N(yk + 7.5),
          "", akzent, 1.2);
    }
    return B;
}

/// SVG-Pfad (M L H V Q A Z, a relativ – was bauen() erzeugt) als Cairo-Pfad anlegen
inline void pfad_anlegen(Cairo::RefPtr<Cairo::Context> const &cr, std::string const &d)
{
    std::size_t i = 0;
    char cmd = 0;
    double x = 0, y = 0, sx = 0, sy = 0;
    auto leer = [&] { while (i < d.size() && (d[i] == ' ' || d[i] == ',' || d[i] == '\n')) ++i; };
    auto zahl = [&]() -> double {
        leer();
        std::size_t start = i;
        if (i < d.size() && (d[i] == '-' || d[i] == '+')) ++i;
        while (i < d.size() && (std::isdigit((unsigned char)d[i]) || d[i] == '.')) ++i;
        if (i < d.size() && (d[i] == 'e' || d[i] == 'E')) {
            ++i;
            if (i < d.size() && (d[i] == '-' || d[i] == '+')) ++i;
            while (i < d.size() && std::isdigit((unsigned char)d[i])) ++i;
        }
        return start == i ? 0.0 : g_ascii_strtod(d.substr(start, i - start).c_str(), nullptr);
    };
    auto bogen = [&](double r, bool gross, bool sweep, double x2, double y2) {
        double x1 = x, y1 = y;
        if (r <= 0 || (std::abs(x2 - x1) < 1e-12 && std::abs(y2 - y1) < 1e-12)) {
            cr->line_to(x2, y2);
            return;
        }
        double dx2 = (x1 - x2) / 2, dy2 = (y1 - y2) / 2;
        double lam = (dx2 * dx2 + dy2 * dy2) / (r * r);
        if (lam > 1) r *= std::sqrt(lam);
        double num = r * r * r * r - r * r * dy2 * dy2 - r * r * dx2 * dx2;
        double den = r * r * dy2 * dy2 + r * r * dx2 * dx2;
        double coef = (gross == sweep ? -1 : 1) * std::sqrt(std::max(0.0, num / den));
        double cxp = coef * dy2, cyp = -coef * dx2;
        double ccx = cxp + (x1 + x2) / 2, ccy = cyp + (y1 + y2) / 2;
        double t1 = std::atan2(y1 - ccy, x1 - ccx), t2 = std::atan2(y2 - ccy, x2 - ccx);
        if (sweep) cr->arc(ccx, ccy, r, t1, t2);
        else cr->arc_negative(ccx, ccy, r, t1, t2);
    };
    while (i < d.size()) {
        leer();
        if (i >= d.size()) break;
        if (std::isalpha((unsigned char)d[i])) cmd = d[i++];
        switch (cmd) {
        case 'M': x = zahl(); y = zahl(); cr->move_to(x, y); sx = x; sy = y; cmd = 'L'; break;
        case 'L': x = zahl(); y = zahl(); cr->line_to(x, y); break;
        case 'H': x = zahl(); cr->line_to(x, y); break;
        case 'V': y = zahl(); cr->line_to(x, y); break;
        case 'Q': {
            double qx = zahl(), qy = zahl(), ex = zahl(), ey = zahl();
            cr->curve_to(x + 2.0 / 3 * (qx - x), y + 2.0 / 3 * (qy - y), ex + 2.0 / 3 * (qx - ex), ey + 2.0 / 3 * (qy - ey), ex, ey);
            x = ex; y = ey;
            break;
        }
        case 'A': case 'a': {
            double rx = zahl(); zahl(); zahl();
            bool gross = zahl() != 0, sweep = zahl() != 0;
            double ex = zahl(), ey = zahl();
            if (cmd == 'a') { ex += x; ey += y; }
            bogen(rx, gross, sweep, ex, ey);
            x = ex; y = ey;
            break;
        }
        case 'Z': case 'z': cr->close_path(); x = sx; y = sy; break;
        default: ++i; break;
        }
    }
}

inline void farbe(Cairo::RefPtr<Cairo::Context> const &cr, std::string const &hex, double alpha = 1.0)
{
    unsigned v = 0;
    std::sscanf(hex.c_str() + 1, "%x", &v);
    cr->set_source_rgba(((v >> 16) & 255) / 255.0, ((v >> 8) & 255) / 255.0, (v & 255) / 255.0, alpha);
}

/// Bild in w × h zeichnen (zentriert, Seitenverhältnis 1 : 3)
inline void zeichnen(Cairo::RefPtr<Cairo::Context> const &cr, double w, double h, Merkmale const &m, bool hell,
                     double alpha = 1.0)
{
    auto B = bauen(m, hell);
    double s = std::min(w / 40.0, h / 120.0);
    cr->save();
    cr->translate((w - 40 * s) / 2, (h - 120 * s) / 2);
    cr->scale(s, s);
    for (auto const &f : B.formen) {
        cr->save();
        if (f.clip) {
            cr->begin_new_path();
            pfad_anlegen(cr, B.koerper);
            cr->clip();
            if (f.clip >= 2) {
                pfad_anlegen(cr, f.clip == 2 ? B.auslauf : f.clip == 3 ? B.unten : B.oben);
                cr->clip();
            }
        }
        cr->begin_new_path();
        pfad_anlegen(cr, f.d);
        if (!f.fill.empty()) {
            farbe(cr, f.fill, alpha);
            if (f.stroke.empty()) cr->fill();
            else cr->fill_preserve();
        }
        if (!f.stroke.empty()) {
            farbe(cr, f.stroke, alpha);
            cr->set_line_width(f.sw);
            cr->set_line_cap(Cairo::LINE_CAP_ROUND);
            cr->set_line_join(Cairo::LINE_JOIN_ROUND);
            if (f.strich) cr->set_dash(std::vector<double>{2.0, 1.4}, 0);
            cr->stroke();
        }
        cr->restore();
    }
    cr->restore();
}

} // namespace Inkscape::UI::Dialog::fraeserbild

#endif
