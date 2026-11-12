// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Panel: Fräsbahnen und Nullpunkt auf der Zeichenfläche, Zeitleiste, Platten und Seiten
 */
#include "ui/dialog/hirch-intern.h"
#include "path/path-boolop.h"
#include "ui/tools/tool-base.h"
#include "ui/widget/canvas.h"
#include "helper/geom-pathstroke.h"
#include "ui/dialog/hirch-3d.h"

#include <cstdio>

namespace Inkscape::UI::Dialog {

void hirch_nullpunkt_feld_auffrischen(SPDesktop *desktop);   // hirch-ansicht.cpp

void HirchFraesen::Impl::bahnen_weg()
{
    bahn_items.clear();
}

Geom::Point HirchFraesen::Impl::nullpunkt_mm()
{
    double b = 0, h = 0;
    if (auto m = unter(analyse, "material")) {
        b = num(*m, "breite", 0);
        h = num(*m, "hoehe", 0);
    }
    bj::object np;
    if (auto n = unter(einst, "nullpunkt")) np = *n;
    if (rund_einst(einst)) return {b / 2, h / 2};       // rund: immer in der Achse
    auto xy = str(np, "xy", "ul");
    double fy = xy[0] == 'm' ? 0.5 : xy[0] == 'o' ? 1.0 : 0.0;
    double fx = xy.size() > 1 ? (xy[1] == 'm' ? 0.5 : xy[1] == 'r' ? 1.0 : 0.0) : 0.0;
    return {fx * b + num(np, "dx", 0), fy * h + num(np, "dy", 0)};
}

void HirchFraesen::Impl::nullpunkt_zeichnen()
{
    np_kreis.reset();
    np_x.reset();
    np_y.reset();
    np2_kreis.reset();
    np2_x.reset();
    np2_y.reset();
    geister.clear();
    auto desk = panel.getDesktop();
    if (!desk || !hat_analyse) return;
    desktop_beobachten(desk);
    hirch_nullpunkt_feld_auffrischen(desk);
    double hoehe = 0, breite = 0;
    if (auto m = unter(analyse, "material")) {
        hoehe = num(*m, "hoehe", 0);
        breite = num(*m, "breite", 0);
    }
    double const px = MM_PX;
    auto dt = desk->doc2dt();
    double s1x = 0, s1y = 0;            // Seite der gewählten Platte (bzw. der Vorrichtung)
    seite_ecke(1, s1x, s1y);
    auto punkt = [&](double x, double y) { return Geom::Point((s1x + x) * px, (s1y + hoehe - y) * px) * dt; };
    // der eine Nullpunkt kommt aus dem Rechenkern (analyse «nullpunkte», Dokument-mm) – dieselben Zahlen
    // wie im G-Code (tests/test_nullpunkt_matrix.py)
    auto o = nullpunkt_mm();
    if (Geom::Point n; nullpunkt_aus_analyse(platte == 0 ? 0 : platte, 1, n))
        o = Geom::Point(n.x() - s1x, s1y + hoehe - n.y());
    // Lineale, Mausposition, Werkzeugleisten … zählen ab dem Nullpunkt der jeweiligen Seite
    // (hirch_nullpunkt_seite → nullpunkt_doc); hat er sich geändert, neu anzeigen
    {
        std::string stand = bj::serialize(analyse.contains("nullpunkte") ? analyse.at("nullpunkte") : bj::value());
        if (stand != np_stand) {
            np_stand = stand;
            if (auto l = liste_von(analyse, "nullpunkte")) hirch_nullpunkte_merken(doc(), *l);
            if (auto dw = desk->getDesktopWidget()) {
                if (auto cg = dw->get_canvas_grid()) cg->updateRulers();
            }
            if (auto d = doc()) d->getPageManager().hirchNullpunktGeaendert();
        }
    }
    double l = std::clamp(std::min(breite, hoehe) * 0.12, 4.0, 40.0);   // Pfeillänge in mm
    auto pfeil = [&](Geom::Point a, Geom::Point b, guint32 farbe) {
        Geom::PathVector pv;
        Geom::Path p(a);
        p.appendNew<Geom::LineSegment>(b);
        pv.push_back(p);
        auto r = b - a;
        auto n = Geom::Point(-r[Geom::Y], r[Geom::X]) * 0.18;
        Geom::Path spitze(b - r * 0.25 + n);
        spitze.appendNew<Geom::LineSegment>(b);
        spitze.appendNew<Geom::LineSegment>(b - r * 0.25 - n);
        pv.push_back(spitze);
        auto item = make_canvasitem<CanvasItemBpath>(desk->getCanvasSketch(), pv, false);
        item->set_stroke(farbe);
        item->set_stroke_width(2.0);
        item->set_fill(0x0, SP_WIND_RULE_NONZERO);
        item->set_pickable(false);
        return item;
    };
    auto kreis_bei = [&](Geom::Point m) {
        Geom::PathVector kreis;
        kreis.push_back(Geom::Path(Geom::Circle(m, l * 0.12 * px * dt.descrim())));
        auto k = make_canvasitem<CanvasItemBpath>(desk->getCanvasSketch(), kreis, false);
        k->set_stroke(0x202020ff);
        k->set_stroke_width(2.0);
        k->set_fill(0xffffffcc, SP_WIND_RULE_NONZERO);
        k->set_pickable(false);
        return k;
    };
    np_x = pfeil(punkt(o.x(), o.y()), punkt(o.x() + l, o.y()), 0xd9342bff);
    np_y = pfeil(punkt(o.x(), o.y()), punkt(o.x(), o.y() + l), 0x2e9e3eff);
    np_kreis = kreis_bei(punkt(o.x(), o.y()));

    // Rundes Material: der Kreis ist das Werkstück (die Ecken der Seite sind Luft), dazu der
    // Sperrkreis um die Schraube in der Mitte
    if (auto ru = rund_einst(einst); ru && breite > 0 && hoehe > 0 && platte != 0) {
        double R = std::min(breite, hoehe) / 2;
        auto kreis_mm = [&](double r_mm) {
            Geom::PathVector pv;
            Geom::Path p(punkt(breite / 2 + r_mm, hoehe / 2));
            int n = 96;
            for (int i = 1; i <= n; ++i) {
                double a = 2 * M_PI * i / n;
                p.appendNew<Geom::LineSegment>(punkt(breite / 2 + r_mm * std::cos(a), hoehe / 2 + r_mm * std::sin(a)));
            }
            p.close();
            pv.push_back(p);
            return pv;
        };
        auto mat = make_canvasitem<CanvasItemBpath>(desk->getCanvasSketch(), kreis_mm(R), false);
        mat->set_stroke(0x8a6d3bee);
        mat->set_stroke_width(2.0);
        mat->set_fill(0x0, SP_WIND_RULE_NONZERO);
        mat->set_pickable(false);
        geister.push_back(std::move(mat));
        double kopf = std::max(num(*ru, "kopf", 12), num(*ru, "bohrung", 6.5));
        if (flag(*ru, "schraube", true) && kopf > 0) {
            auto s_ = make_canvasitem<CanvasItemBpath>(desk->getCanvasSketch(), kreis_mm(kopf / 2), false);
            s_->set_stroke(0xc01c28dd);
            s_->set_stroke_width(1.5);
            s_->set_dashes({4.0, 3.0});
            s_->set_fill(0x77767b55, SP_WIND_RULE_NONZERO);
            s_->set_pickable(false);
            geister.push_back(std::move(s_));
        }
    }

    // Spannvorrichtung: das Werkstück durchscheinend auf der Seite der Vorrichtung, wo es liegt
    if (auto v = unter(analyse, "vorrichtung"); v && flag(*v, "an", false) && geister_an()) {
        double vl = num(*v, "links", 0), vo_ = num(*v, "oben", 0), vh = num(*v, "hoehe", 0);
        double lx = 0, ly = 0, wb = 0, wh = 0;
        if (auto l = liste_von(*v, "lage"); l && l->size() >= 2) {
            lx = (*l)[0].to_number<double>();
            ly = (*l)[1].to_number<double>();
        }
        if (auto w = liste_von(*v, "werkstueck"); w && w->size() >= 2) {
            wb = (*w)[0].to_number<double>();
            wh = (*w)[1].to_number<double>();
        }
        auto vp = [&](double x, double y) {
            return Geom::Point((vl + lx + x) * px, (vo_ + vh - ly - y) * px) * dt;
        };
        Geom::PathVector rahmen, inhalt_;
        if (rund_einst(einst)) {            // rundes Werkstück: als Kreis auf der Vorrichtung
            double R = std::min(wb, wh) / 2;
            Geom::Path r(vp(wb / 2 + R, wh / 2));
            for (int i = 1; i <= 96; ++i) {
                double a = 2 * M_PI * i / 96;
                r.appendNew<Geom::LineSegment>(vp(wb / 2 + R * std::cos(a), wh / 2 + R * std::sin(a)));
            }
            r.close();
            rahmen.push_back(r);
        } else {
            Geom::Path r(vp(0, 0));
            r.appendNew<Geom::LineSegment>(vp(wb, 0));
            r.appendNew<Geom::LineSegment>(vp(wb, wh));
            r.appendNew<Geom::LineSegment>(vp(0, wh));
            r.close();
            rahmen.push_back(r);
        }
        if (auto um = liste_von(*v, "umrisse")) {
            for (auto const &zv : *um) {
                auto zg = zv.if_array();
                if (!zg || zg->size() < 4) continue;
                Geom::Path pf(vp((*zg)[0].to_number<double>(), (*zg)[1].to_number<double>()));
                for (std::size_t i = 2; i + 1 < zg->size(); i += 2)
                    pf.appendNew<Geom::LineSegment>(vp((*zg)[i].to_number<double>(), (*zg)[i + 1].to_number<double>()));
                inhalt_.push_back(pf);
            }
        }
        auto g = make_canvasitem<CanvasItemBpath>(desk->getCanvasSketch(), rahmen, false);
        g->set_stroke(0x6b8fd6dd);
        g->set_stroke_width(1.5);
        g->set_fill(0xc9d8f560, SP_WIND_RULE_NONZERO);
        g->set_pickable(false);
        geister.push_back(std::move(g));
        if (!inhalt_.empty()) {
            auto g2 = make_canvasitem<CanvasItemBpath>(desk->getCanvasSketch(), inhalt_, false);
            g2->set_stroke(0x8a8a8acc);
            g2->set_stroke_width(1.0);
            g2->set_dashes({4.0, 3.0});
            g2->set_fill(0x0, SP_WIND_RULE_NONZERO);
            g2->set_pickable(false);
            geister.push_back(std::move(g2));
        }
    }
    // Nullpunkt auch auf der Seite der Vorrichtung (derselbe Maschinenpunkt), während am Werkstück gearbeitet wird
    if (Geom::Point n; platte != 0 && nullpunkt_aus_analyse(0, 1, n)) {
        auto bei = [&](double x, double y) { return Geom::Point(x * MM_PX, y * MM_PX) * dt; };   // Dokument-mm
        geister.push_back(pfeil(bei(n.x(), n.y()), bei(n.x() + l, n.y()), 0xd9342bff));
        geister.push_back(pfeil(bei(n.x(), n.y()), bei(n.x(), n.y() - l), 0x2e9e3eff));   // Y nach oben
        geister.push_back(kreis_bei(bei(n.x(), n.y())));
    }
    if (!zweiseitig()) return;

    // Seite 2: derselbe Nullpunkt an der Maschine – in der Zeichnung von Seite 2 an derselben Stelle
    double s2x = 0, s2y = 0;
    seite_ecke(2, s2x, s2y);
    auto punkt2 = [&](double x, double y) { return Geom::Point((s2x + x) * px, (s2y + hoehe - y) * px) * dt; };
    if (Geom::Point n; nullpunkt_aus_analyse(platte, 2, n)) o = Geom::Point(n.x() - s2x, s2y + hoehe - n.y());
    np2_x = pfeil(punkt2(o.x(), o.y()), punkt2(o.x() + l, o.y()), 0xd9342bff);
    np2_y = pfeil(punkt2(o.x(), o.y()), punkt2(o.x(), o.y() + l), 0x2e9e3eff);
    np2_kreis = kreis_bei(punkt2(o.x(), o.y()));

    // Umrisse der anderen Seite blass und an der Wendeachse gespiegelt: so liegt nach dem Wenden
    // alles übereinander
    auto um = unter(analyse, "umrisse");
    if (!um || !geister_an()) return;
    auto z = unter(einst, "zweiseitig");
    bool um_y = z && str(*z, "achse", "x") == "y";
    for (int ziel = 1; ziel <= 2; ++ziel) {
        auto quelle = liste_von(*um, ziel == 1 ? "2" : "1");
        if (!quelle || quelle->empty()) continue;
        double sx = 0, sy = 0;
        seite_ecke(ziel, sx, sy);
        Geom::PathVector pv;
        for (auto const &zv : *quelle) {
            auto zg = zv.if_array();
            if (!zg || zg->size() < 4) continue;
            auto pkt = [&](std::size_t i) {
                double x = (*zg)[i].to_number<double>(), y = (*zg)[i + 1].to_number<double>();
                if (um_y) x = breite - x;
                else y = hoehe - y;
                return Geom::Point((sx + x) * px, (sy + hoehe - y) * px) * dt;
            };
            Geom::Path pf(pkt(0));
            for (std::size_t i = 2; i + 1 < zg->size(); i += 2) pf.appendNew<Geom::LineSegment>(pkt(i));
            pv.push_back(pf);
        }
        if (pv.empty()) continue;
        auto g = make_canvasitem<CanvasItemBpath>(desk->getCanvasSketch(), pv, false);
        g->set_stroke(0x8a8a8acc);
        g->set_stroke_width(1.0);
        g->set_dashes({4.0, 3.0});
        g->set_fill(0x0, SP_WIND_RULE_NONZERO);
        g->set_pickable(false);
        geister.push_back(std::move(g));
    }
}

void HirchFraesen::Impl::einrichten_beim_start()
{
    auto d = doc();
    if (!d || flag(einst, "eingerichtet", false)) return;
    // nur neue, noch nie gespeicherte Zeichnungen; gespeicherte ältere gelten als eingerichtet
    if (d->getDocumentFilename()) return;
    std::string schluessel = std::to_string((uintptr_t)d);
    if (eingerichtet_gefragt.count(schluessel)) return;
    eingerichtet_gefragt.insert(schluessel);
    // B-25: mit mehr als einer Maschine zuerst die Maschine wählen (die zuletzt benutzte ist vorgewählt)
    nach_der_begruessung([this] { maschine_waehlen([this] { material_dialog(true); }); });
}

/// Umspannen (Peter 27.10., Entwurf C): die Zonen, die erst Spannung 2 fräst, orange schraffiert – dort beim
/// Umspannen kein Spannmittel hinlegen. Nur Anzeige, nicht wählbar.
void HirchFraesen::Impl::umspannen_zonen_zeichnen(SPDesktop *desk)
{
    auto us = liste_von(ergebnis, "umspannen");
    if (!us || us->empty()) return;
    double hoehe = 0;
    if (auto m = unter(analyse, "material")) hoehe = num(*m, "hoehe", 0);
    double const px = MM_PX;
    auto dt = desk->doc2dt();
    for (auto const &uv : *us) {
        auto u = uv.if_object();
        if (!u) continue;
        auto zonen = liste_von(*u, "zonen_mm");
        if (!zonen) continue;
        double sx = 0, sy = 0;
        seite_ecke((int)num(*u, "seite", 1), sx, sy);
        auto punkt = [&](double x, double y) { return Geom::Point((sx + x) * px, (sy + hoehe - y) * px) * dt; };
        Geom::PathVector umriss;
        double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
        for (auto const &zv : *zonen) {
            auto ring = zv.if_array();
            if (!ring || ring->size() < 3) continue;
            Geom::Path p;
            bool erst = true;
            for (auto const &pv_ : *ring) {
                auto xy = pv_.if_array();
                if (!xy || xy->size() < 2) continue;
                double x = (*xy)[0].to_number<double>(), y = (*xy)[1].to_number<double>();
                x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y);
                if (erst) p.start(punkt(x, y));
                else p.appendNew<Geom::LineSegment>(punkt(x, y));
                erst = false;
            }
            p.close();
            umriss.push_back(p);
        }
        if (umriss.empty()) continue;
        // Schraffur: schmale schräge Streifen (alle 3 mm, 1 mm breit), mit der Zone geschnitten
        Geom::PathVector streifen;
        double const ab = 3.0, br = 1.0, w = (x1 - x0) + (y1 - y0);
        for (double t = -w; t < w; t += ab) {
            Geom::Path q(punkt(x0 + t, y0));
            q.appendNew<Geom::LineSegment>(punkt(x0 + t + br, y0));
            q.appendNew<Geom::LineSegment>(punkt(x0 + t + br + (y1 - y0) + 1, y1 + 1));
            q.appendNew<Geom::LineSegment>(punkt(x0 + t + (y1 - y0) + 1, y1 + 1));
            q.close();
            streifen.push_back(q);
        }
        auto schraffur = sp_pathvector_boolop(streifen, umriss, bool_op_inters, fill_nonZero, fill_nonZero);
        auto f = make_canvasitem<CanvasItemBpath>(desk->getCanvasSketch(), schraffur, false);
        f->set_stroke(0x0);
        f->set_fill(0xe5a50a70, SP_WIND_RULE_NONZERO);
        f->set_pickable(false);
        bahn_items.push_back(std::move(f));
        auto r = make_canvasitem<CanvasItemBpath>(desk->getCanvasSketch(), umriss, false);
        r->set_stroke(0xe5a50ae0);
        r->set_stroke_width(1.5);
        r->set_fill(0xe5a50a18, SP_WIND_RULE_NONZERO);
        r->set_pickable(false);
        bahn_items.push_back(std::move(r));
    }
}

/// Zeile «⟳ Umspannen» im Ablauf (wie die Zeile vor dem Wenden)
Gtk::Widget *HirchFraesen::Impl::umspannen_zeile(int seite)
{
    long stellen = 0;
    double zeit_ = 0;
    if (auto us = liste_von(ergebnis, "umspannen")) {
        for (auto const &uv : *us) {
            auto u = uv.if_object();
            if (!u || (int)num(*u, "seite", 1) != seite) continue;
            if (auto z = liste_von(*u, "zonen_mm")) stellen += (long)z->size();
            zeit_ += num(*u, "zeit", 0);
        }
    }
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
    klasse(*box, "hirch-umspannen");
    std::string kopf = TR("⟳ Umspannen");
    if (stellen > 0) kopf += " · " + anzahl(stellen, hirch_englisch() ? "spot" : "Stelle", hirch_englisch() ? "spots" : "Stellen");
    if (zeit_ > 0) kopf += " · " + TRF("Spannung 2 ca. {}", {zeit(zeit_)});
    auto k = label(kopf, "hirch-titel");
    k->set_xalign(0);
    box->add(*k);
    auto t = label(TR("Teil nicht verschieben: erst die neuen Spannmittel auf schon Gefrästes setzen, dann die alten "
                      "lösen. Nicht in die orange schraffierten Zonen spannen. Der Nullpunkt bleibt."),
                   "hirch-dim");
    klasse(*t, "hirch-klein");
    t->set_line_wrap(true);
    t->set_max_width_chars(44);
    t->set_xalign(0);
    box->add(*t);
    box->show_all();
    return box;
}

void HirchFraesen::Impl::bahnen_zeichnen()
{
    bahnen_weg();
    auto desk = panel.getDesktop();
    if (!desk) return;
    desktop_beobachten(desk);
    kollisionen_zeichnen(desk);           // 3D: Spindel/Halterung streift – immer sichtbar (hirch-3d.cpp)
    umspannen_zonen_zeichnen(desk);       // Zonen der Spannung 2: orange schraffiert, immer sichtbar
    if (!bahnen_an()) return;
    auto bahnen = unter(ergebnis, "bahnen");
    if (!bahnen) return;
    double hoehe = 0;
    if (auto m = unter(analyse, "material")) hoehe = num(*m, "hoehe", 0);
    double const px = MM_PX;
    auto dt = desk->doc2dt();
    double sx = 0, sy = 0;           // linke obere Ecke der Seite (mm im Dokument)
    auto punkt = [&](double x, double y) { return Geom::Point((sx + x) * px, (sy + hoehe - y) * px) * dt; };
    std::map<std::string, std::string> farben, arten;
    std::set<std::string> versteckt;
    if (auto bea = liste_von(einst, "bearbeitungen")) {
        for (auto const &v : *bea) {
            if (auto o = v.if_object()) {
                farben[str(*o, "id")] = str(*o, "farbe");
                arten[str(*o, "id")] = str(*o, "art");
                if (!flag(*o, "sichtbar", true)) versteckt.insert(str(*o, "id"));
            }
        }
    }
    for (auto const &kv : *bahnen) {
        std::string key = kv.key();
        auto info = kv.value().if_object();
        if (!info) continue;
        auto zuege = liste_von(*info, "zuege");
        if (!zuege) continue;
        auto bid = key.substr(0, key.find('#'));
        if (versteckt.count(bid)) continue;
        if (!farben.count(bid) && geloeschte_ids.count(bid)) continue;     // gelöscht (Peter 28.10.)
        auto teil = key.find('#') == std::string::npos ? std::string() : key.substr(key.find('#') + 1);
        seite_ecke((int)num(*info, "seite", 1), sx, sy);
        double r = 0.5, g = 0.5, b = 0.5;
        hex_rgb(bid == "pass" ? std::string("#9141ac") : farben[bid], r, g, b);
        // durchscheinend: die Zeichnung bleibt vorne, die Bahnen dahinter gut lesbar
        guint32 rgba = ((guint32)(r * 255) << 24) | ((guint32)(g * 255) << 16) | ((guint32)(b * 255) << 8) |
                       (teil == "flach" ? 0x38u : 0x5Au);
        double radius = num(*info, "durchmesser", 1) / 2;
        // aus der Bearbeitung genommene Formen: deren Züge nicht mehr zeigen (Peter 02.11.)
        std::vector<Geom::Rect> weg;
        for (auto const &w : weg_rahmen) {
            if (!w.ids.count(bid)) continue;
            Geom::Rect r = w.rahmen;
            r.expandBy((radius + 1.0) * px * dt.descrim());
            weg.push_back(r);
        }
        Geom::PathVector pv;
        for (auto const &zv : *zuege) {
            auto z = zv.if_array();
            if (!z || z->size() < 2) continue;
            auto wert_bei = [&](std::size_t i) { return (*z)[i].is_number() ? (*z)[i].to_number<double>() : 0.0; };
            if (!weg.empty()) {
                bool drin = false;
                for (auto const &r : weg) {
                    bool alle = true;
                    for (std::size_t i = 0; i + 1 < z->size() && alle; i += 2)
                        alle = r.contains(punkt(wert_bei(i), wert_bei(i + 1)));
                    if (alle) { drin = true; break; }
                }
                if (drin) continue;
            }
            if (z->size() == 2) {          // Bohrung: Kreis in Werkzeuggrösse
                auto c = punkt(wert_bei(0), wert_bei(1));
                double rr = std::max(radius, 0.5) * px * dt.descrim();
                pv.push_back(Geom::Path(Geom::Circle(c, rr)));
                continue;
            }
            Geom::Path p(punkt(wert_bei(0), wert_bei(1)));
            for (std::size_t i = 2; i + 1 < z->size(); i += 2) p.appendNew<Geom::LineSegment>(punkt(wert_bei(i), wert_bei(i + 1)));
            pv.push_back(p);
        }
        if (pv.empty()) continue;
        // Gravur (Peter, 24.10.2026): so breit zeigen, wie graviert wird – die Schnittbreite als Fläche
        // im Massstab des Werkstücks, nicht als dünne Mittellinie
        bool flaeche = arten[bid] == "gravur" && teil.empty() && radius > 0;
        if (flaeche) {
            Geom::PathVector umriss;
            double breite = 2 * radius * px * dt.descrim();
            for (auto const &p : pv) {
                if (p.size_default() == 0) { umriss.push_back(p); continue; }
                // geschlossene Züge offen umfahren: sonst bleibt in kleinen Formen (i-Punkt) ein Loch,
                // das der Fräser in Wirklichkeit mitnimmt
                Geom::Path q = p;
                if (q.closed()) q.close(false);
                for (auto const &u : Inkscape::outline(q, breite, 4, Inkscape::JOIN_ROUND, Inkscape::BUTT_ROUND))
                    umriss.push_back(u);
            }
            pv = std::move(umriss);
        }
        auto item = make_canvasitem<CanvasItemBpath>(desk->getCanvasSketch(), pv, false);
        if (flaeche) {
            item->set_stroke(0x0);
            item->set_fill((rgba & 0xFFFFFF00u) | 0xB0u, SP_WIND_RULE_NONZERO);
        } else {
            item->set_stroke(rgba);
            item->set_fill(0x0, SP_WIND_RULE_NONZERO);
        }
        item->set_stroke_width(1.0);
        if (teil == "schlicht") item->set_dashes({6.0, 4.0});
        item->set_pickable(false);
        item->set_visible(true);
        bahn_items.push_back(std::move(item));
    }
}

bool HirchFraesen::Impl::nullpunkt_aus_analyse(int platte_, int seite, Geom::Point &aus_mm)
{
    if (auto l = liste_von(analyse, "nullpunkte")) {
        for (auto const &v : *l) {
            auto o = v.if_object();
            if (o && (int)num(*o, "platte", -1) == platte_ && (int)num(*o, "seite", 0) == seite) {
                aus_mm = Geom::Point(num(*o, "x", 0), num(*o, "y", 0));
                return true;
            }
        }
    }
    return false;
}

bool HirchFraesen::Impl::nullpunkt_doc(Geom::Point bei, Geom::Point &aus)
{
    // derselbe Weg wie Lineale und Werkzeugleisten: gemerkte Analyse, sonst die Einstellungen der Zeichnung
    return hirch_nullpunkt_seite(doc(), bei, aus);
}

void HirchFraesen::Impl::seite_ecke(int seite, double &x, double &y)
{
    x = y = 0;
    if (auto l = liste_von(analyse, "seiten")) {
        if (seite >= 1 && seite <= (int)l->size()) {
            if (auto o = (*l)[seite - 1].if_object()) {
                x = num(*o, "links", 0);
                y = num(*o, "oben", 0);
            }
        }
    }
}

std::string HirchFraesen::Impl::warnungen_text(bj::object const &o)
{
    std::string t;
    if (auto w = liste_von(o, "warnungen")) {
        for (auto const &z : *w) {
            if (z.is_string()) t += (t.empty() ? "" : "\n") + std::string(z.get_string());
        }
    }
    return t;
}

long HirchFraesen::Impl::objekte(std::string const &id)
{
    if (auto a = unter(analyse, "anzahl")) return (long)num(*a, id.c_str(), 0);
    return 0;
}

long HirchFraesen::Impl::zusaetzlich(std::string const &id)
{
    if (auto a = unter(analyse, "zusaetzlich")) return (long)num(*a, id.c_str(), 0);
    return 0;
}

std::string HirchFraesen::Impl::name_von(bj::object const &b)
{
    if (auto n = unter(analyse, "namen")) {
        auto t = str(*n, str(b, "id").c_str());
        if (!t.empty()) return t;
    }
    auto info = art_info(str(b, "art"));
    return info ? std::string(TR(info->name)) : str(b, "art");
}

int HirchFraesen::Impl::je_art(std::string const &art)
{
    int n = 0;
    if (auto bea = liste_von(einst, "bearbeitungen")) {
        for (auto const &v : *bea) {
            if (auto o = v.if_object(); o && str(*o, "art") == art) ++n;
        }
    }
    return n;
}

std::string HirchFraesen::Impl::freier_ton(std::string const &art)
{
    std::set<std::string> belegt;
    if (auto bea = liste_von(einst, "bearbeitungen")) {
        for (auto const &v : *bea) {
            if (auto o = v.if_object(); o && str(*o, "art") == art) belegt.insert(str(*o, "farbe"));
        }
    }
    if (auto t = unter(analyse, "farbtoene")) {
        if (auto l = liste_von(*t, art.c_str())) {
            for (auto const &v : *l) {
                if (v.is_string() && !belegt.count(std::string(v.get_string()))) return std::string(v.get_string());
            }
        }
    }
    auto info = art_info(art);
    return info ? info->hex : "#808080";
}

std::string HirchFraesen::Impl::neue_id()
{
    std::set<std::string> ids;
    if (auto bea = liste_von(einst, "bearbeitungen")) {
        for (auto const &v : *bea) {
            if (auto o = v.if_object()) ids.insert(str(*o, "id"));
        }
    }
    for (int n = 1;; ++n) {
        auto id = "b" + std::to_string(n);
        if (!ids.count(id)) return id;
    }
}

bj::object const *HirchFraesen::Impl::werkzeug(std::string const &key)
{
    if (auto w = unter(einst, "werkzeuge")) return unter(*w, key.c_str());
    return nullptr;
}

std::string HirchFraesen::Impl::werkzeug_kurz(std::string const &key)
{
    auto w = werkzeug(key);
    return w ? werkzeug_name(*w) : TR("Werkzeug fehlt");
}

std::string HirchFraesen::Impl::wz_titel(std::string const &key)
{
    if (auto n = unter(analyse, "werkzeug_namen")) {
        auto s_ = str(*n, key.c_str());
        if (!s_.empty()) return s_;
    }
    return werkzeug_kurz(key);
}

std::string HirchFraesen::Impl::zusatz_text(bj::object const &b)
{
    std::string t;
    if (str(b, "strategie") == "adaptiv") t += TR(" · adaptiv");
    auto rw = str(b, "raeum_werkzeug");
    if (!rw.empty() && rw != str(b, "werkzeug", "A")) t += TR(" · geräumt mit ") + werkzeug_kurz(rw);
    if (flag(b, "schlichten", false)) {
        t += TR(" · geschlichtet");
        auto sw = str(b, "schlicht_werkzeug");
        if (!sw.empty() && sw != str(b, "werkzeug", "A")) t += " (" + werkzeug_kurz(sw) + ")";
    }
    if (str(b, "anfahren") == "bogen") t += TR(" · Anfahren im Bogen");
    if (flag(b, "dogbone", false)) t += TR(" · Dogbone");
    double a = num(b, "aufmass", 0);
    if (std::abs(a) > 1e-9) t += TR(" · Aufmass ") + std::string(a > 0 ? "+" : "") + zahl(a);
    double vp = num(b, "vorschub_prozent", 100);
    if (std::abs(vp - 100) > 1e-9) t += TR(" · Vorschub ") + zahl(vp, 0) + " %";
    return t;
}

std::string HirchFraesen::Impl::op_schluessel(bj::object const &op)
{
    auto k = str(op, "key");
    if (!k.empty()) return k;
    auto t = str(op, "teil");
    return str(op, "id") + (t.empty() ? "" : "#" + t);
}

void HirchFraesen::Impl::zeiten_merken()
{
    auto ab = unter(ergebnis, "ablauf");
    if (!ab) return;
    // Bearbeitungen dieser Rechnung ersetzen ihre alten Zeiten; ausgeschaltete behalten
    // die zuletzt bekannte Zeit (die wird weiter angezeigt)
    std::set<std::string> neu_ids;
    if (auto seiten = liste_von(*ab, "seiten"))
        for (auto const &sv : *seiten)
            if (auto so = sv.if_object())
                if (auto abs = liste_von(*so, "abschnitte"))
                    for (auto const &av : *abs)
                        if (auto a = av.if_object())
                            if (auto ops = liste_von(*a, "ops"))
                                for (auto const &ov : *ops)
                                    if (auto o = ov.if_object()) neu_ids.insert(str(*o, "id"));
    for (auto it = zeit_cache.begin(); it != zeit_cache.end();) {
        auto id = it->first.substr(0, it->first.find('#'));
        if (neu_ids.count(id)) it = zeit_cache.erase(it);
        else ++it;
    }
    seiten_rest.clear();
    if (auto seiten = liste_von(*ab, "seiten")) {
        for (auto const &sv : *seiten) {
            auto so = sv.if_object();
            if (!so) continue;
            int seite = (int)num(*so, "seite", 1);
            double summe = 0;
            if (auto abs = liste_von(*so, "abschnitte")) {
                for (auto const &av : *abs) {
                    auto a = av.if_object();
                    if (!a) continue;
                    if (auto ops = liste_von(*a, "ops")) {
                        for (auto const &ov : *ops) {
                            if (auto o = ov.if_object()) {
                                double m = num(*o, "minuten", 0);
                                zeit_cache[op_schluessel(*o)] += m;
                                summe += m;
                            }
                        }
                    }
                }
            }
            seiten_rest[seite] = std::max(0.0, num(*so, "minuten", 0) - summe);
        }
    }
}

double HirchFraesen::Impl::op_minuten(bj::object const &op)
{
    if (ergebnis_aktuell) return num(op, "minuten", 0);
    auto it = zeit_cache.find(op_schluessel(op));
    return it == zeit_cache.end() ? 0.0 : it->second;
}

double HirchFraesen::Impl::bearbeitung_minuten(std::string const &id)
{
    double m = 0;
    for (auto const &[k, v] : zeit_cache)
        if (k == id || k.rfind(id + "#", 0) == 0) m += v;
    return m;
}

double HirchFraesen::Impl::seiten_minuten(int seite)
{
    auto ab = ablauf_daten();
    double m = 0;
    if (!ab) return 0;
    if (auto seiten = liste_von(*ab, "seiten")) {
        for (auto const &sv : *seiten) {
            auto so = sv.if_object();
            if (!so) continue;
            int s_ = (int)num(*so, "seite", 1);
            if (seite && s_ != seite) continue;
            if (ergebnis_aktuell) {
                m += num(*so, "minuten", 0);
                continue;
            }
            if (auto abs = liste_von(*so, "abschnitte")) {
                for (auto const &av : *abs) {
                    auto a = av.if_object();
                    if (!a) continue;
                    if (auto ops = liste_von(*a, "ops"))
                        for (auto const &ov : *ops)
                            if (auto o = ov.if_object()) m += op_minuten(*o);
                }
            }
            m += seiten_rest.count(s_) ? seiten_rest[s_] : 0.0;
        }
    }
    return m;
}

bj::object const *HirchFraesen::Impl::ablauf_daten()
{
    if (vorlaeufig_an) return &vorlaeufig;
    if (ergebnis_aktuell && flag(ergebnis, "ok", false)) {
        if (auto a = unter(ergebnis, "ablauf")) return a;
    }
    return unter(analyse, "ablauf");
}

bool HirchFraesen::Impl::zweiseitig()
{
    auto z = unter(einst, "zweiseitig");
    return z && flag(*z, "an", false);
}

void HirchFraesen::Impl::seite_waehlen(int n, bool zoomen)
{
    seite_aktiv = n == 2 ? 2 : 1;
    anzeigen();
    if (!zoomen) return;
    auto d = doc();
    auto desk = panel.getDesktop();
    if (!d || !desk) return;
    auto &pm = d->getPageManager();
    auto ps = platten_seiten(d);
    int i = (std::max(platte, 1) - 1) * 2 + seite_aktiv - 1;
    if (platte == 0) {
        if (auto vp = vorrichtung_seite(d)) {
            pm.selectPage(vp);
            pm.zoomToPage(desk, vp);
        }
    } else if (i >= 0 && i < (int)ps.size()) {
        pm.selectPage(ps[i]);
        pm.zoomToPage(desk, ps[i]);
    }
}

bool HirchFraesen::Impl::vorrichtung_an()
{
    auto v = unter(analyse, "vorrichtung");
    return v && flag(*v, "an", false) && !flag(*v, "vorhanden", false) && vorrichtung_seite(doc());
}

int HirchFraesen::Impl::platte_min()
{ return vorrichtung_an() ? 0 : 1; }

void HirchFraesen::Impl::platte_waehlen(int n, bool zoomen)
{
    n = std::clamp(n, platte_min(), std::max(1, platten));
    if (n != platte) {
        // fertige Rechnung der alten Platte aufheben, die der neuen zurückholen; die Analyse
        // prüft mit der Kennung, ob sie noch gilt (Peter, 25.10.)
        if (!ergebnis.empty() && !kennung_fertig.empty() && rechen_gen == 0)
            platten_stand[platte] = {ergebnis, kennung_fertig, zeit_cache, seiten_rest, rechen_dauer};
        if (rechen_gen != 0) abbrechen();
        platte = n;
        // neu lesen, aber nicht erzwingen (leer hiesse «neu rechnen»): passt die Kennung zum
        // aufgehobenen Ergebnis, gilt es wieder
        letzte_xml = "\x01platte";
        ergebnis_aktuell = false;
        auto it = platten_stand.find(n);
        if (it != platten_stand.end()) {
            ergebnis = it->second.ergebnis;
            kennung_fertig = it->second.kennung;
            zeit_cache = it->second.zeit_cache;
            seiten_rest = it->second.seiten_rest;
            dauer_wieder = it->second.dauer;
            bahnen_wieder = true;
        } else {
            ergebnis = {};
            kennung_fertig.clear();
            zeit_cache.clear();
            seiten_rest.clear();
        }
        bahnen_weg();
        platte_merken(doc(), platte, zweiseitig() ? 2 : 1);
        teil_vergessen();                   // 3D-Ansichten: nicht das Teil der alten Platte zeigen
        spaeter(10);
        anzeigen();
    }
    if (!zoomen) return;
    auto d = doc();
    auto desk = panel.getDesktop();
    if (!d || !desk) return;
    auto &pm = d->getPageManager();
    int pp = zweiseitig() ? 2 : 1;
    SPPage *pg = nullptr;
    if (platte == 0) {
        pg = vorrichtung_seite(d);
    } else {
        auto ps = platten_seiten(d);
        int i = (platte - 1) * pp + (pp == 2 ? seite_aktiv - 1 : 0);
        if (i >= 0 && i < (int)ps.size()) pg = ps[i];
    }
    if (pg) {
        pm.selectPage(pg);
        pm.zoomToPage(desk, pg);
    }
}

void HirchFraesen::Impl::leinwand_verbinden()
{
    leinwand_druck.disconnect();
    leinwand_los.disconnect();
    if (auto desk = panel.getDesktop(); desk && desk->getCanvas()) {
        auto cv = desk->getCanvas();
        // über signal_event: kommt vor den Werkzeugen der Leinwand an (die Knopf-Signale erreichen uns nicht)
        leinwand_druck = cv->signal_event().connect([this, cv](GdkEvent *ev) {
            if (ev->type == GDK_BUTTON_PRESS) {
                auto e = &ev->button;
                leinwand_gedrueckt = e->button == 1 &&
                                     !(e->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK));
                leinwand_von = {e->x, e->y};
            } else if (ev->type == GDK_2BUTTON_PRESS) {
                leinwand_gedrueckt = false;
                // Doppelklick aufs 3D-Modell (graues Bild mit hirch:relief): Fenster «3D-Modell» (3d 06.11.)
                auto e = &ev->button;
                auto desk = panel.getDesktop();
                if (e->button == 1 && desk) {
                    auto w = cv->canvas_to_world(Geom::Point(e->x, e->y));
                    for (SPObject *o = desk->getItemAtPoint(w, true); o; o = o->parent) {
                        auto it = cast<SPItem>(o);
                        if (!it || !o->getRepr()->attribute("hirch:relief")) continue;
                        desk->getSelection()->set(it);
                        Glib::signal_idle().connect_once([desk] { hirch_3d_fenster(desk); });
                        return true;          // nicht in die Gruppe oder das Bild hinein
                    }
                }
            } else if (ev->type == GDK_BUTTON_RELEASE) {
                auto e = &ev->button;
                if (!leinwand_gedrueckt || e->button != 1) return false;
                leinwand_gedrueckt = false;
                if (Geom::distance(leinwand_von, Geom::Point(e->x, e->y)) > 4) return false;   // gezogen
                auto w = cv->canvas_to_world(Geom::Point(e->x, e->y));
                Glib::signal_idle().connect_once([this, w] { leinwand_klick(w); });
            }
            return false;
        }, false);
    }
}

void HirchFraesen::Impl::leinwand_klick(Geom::Point const &fenster)
{
    auto desk = panel.getDesktop();
    auto d = doc();
    if (!desk || !d) return;
    auto treffer = desk->getItemAtPoint(fenster, false);
    auto p = desk->w2d(fenster);
    if (treffer) return;      // ein Objekt: das zeigt die Auswahl
    for (auto pg : d->getPageManager().getPages()) {
        if (pg && pg->getDesktopRect().contains(p)) {
            // auch im Seitenverwalter wählen: sonst meldet er später wieder die alte Seite
            d->getPageManager().selectPage(pg);
            seite_gewaehlt_von(pg);
            return;
        }
    }
}

void HirchFraesen::Impl::seite_gewaehlt_von(SPPage *pg)
{
    auto d = doc();
    if (!d || !pg) return;
    if (pg->getRepr()->attribute("hirch:vorrichtung")) {
        if (vorrichtung_an() && platte != 0) platte_waehlen(0, false);
        return;
    }
    auto ps = platten_seiten(d);
    int idx = -1;
    for (int i = 0; i < (int)ps.size(); ++i) if (ps[i] == pg) idx = i;
    if (idx < 0) return;
    int pp = zweiseitig() ? 2 : 1;
    int n = idx / pp + 1, s_ = idx % pp + 1;
    if (n > platten) return;
    if (n != platte) platte_waehlen(n, false);
    if (pp == 2 && s_ != seite_aktiv) seite_waehlen(s_, false);
}

void HirchFraesen::Impl::zeichenflaeche_neu_platte()
{
    auto d = doc();
    if (!d) return;
    auto &pm = d->getPageManager();
    if (!pm.hasPages()) pm.enablePages();
    auto ps = platten_seiten(d);
    if (ps.empty()) return;
    double const px_mm = MM_PX;
    double uu_px = d->getDocumentScale()[Geom::X];
    if (uu_px <= 0) uu_px = px_mm;
    double luecke = 20.0 * px_mm / uu_px;
    int pp = zweiseitig() ? 2 : 1;
    auto r = ps.back()->getRect();
    double x = r.right() + luecke;
    for (int k = 0; k < pp; ++k) {
        pm.newPage(Geom::Rect::from_xywh(x, r.top(), ps[0]->getRect().width(), ps[0]->getRect().height()));
        x += ps[0]->getRect().width() + luecke;
    }
    d->ensureUpToDate();
    DocumentUndo::done(d, TR("Neue Platte"), "");
    int neu = (int)platten_seiten(d).size() / pp;
    platten = std::max(platten, neu);
    platte_waehlen(neu, true);
}

void HirchFraesen::Impl::zeichenflaeche_entfernen()
{
    auto d = doc();
    if (!d) return;
    bool zwei = zweiseitig();
    int pp = zwei ? 2 : 1;
    auto ps = platten_seiten(d);
    std::vector<SPPage *> weg;
    std::string name;
    enum { VORR, RUECK, PLATTE } art;
    if (platte == 0) {
        if (auto vs = vorrichtung_seite(d)) weg.push_back(vs);
        name = TR("Die Spannvorrichtung");
        art = VORR;
    } else if (platte > 1) {
        for (int k = 0; k < pp; ++k) {
            int i = (platte - 1) * pp + k;
            if (i < (int)ps.size()) weg.push_back(ps[i]);
        }
        name = TR("Platte ") + std::to_string(platte);
        art = PLATTE;
    } else if (zwei && seite_aktiv == 2 && ps.size() >= 2 && platten <= 1) {
        weg.push_back(ps[1]);
        name = TR("Die Rückseite (Seite 2)");
        art = RUECK;
    } else {
        return;
    }
    if (weg.empty()) return;
    Gtk::MessageDialog m(TR("Zeichenfläche entfernen?"), false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
    m.set_title("Hirch CAM");
    if (auto f = fenster()) m.set_transient_for(*f);
    m.set_secondary_text(name + TR(" wird entfernt. Die Objekte darauf bleiben in der Zeichnung liegen und werden "
                                   "nicht mehr gefräst; bei Bedarf verschieben oder löschen."));
    m.add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
    m.add_button(TR("Entfernen"), Gtk::RESPONSE_OK)->get_style_context()->add_class("destructive-action");
    m.set_default_response(Gtk::RESPONSE_CANCEL);
    if (m.run() != Gtk::RESPONSE_OK) return;
    m.hide();
    auto &pm = d->getPageManager();
    for (auto pg : weg) pm.deletePage(pg, false);
    d->ensureUpToDate();
    if (art == VORR) {
        bj::object vo;
        if (auto v = unter(einst, "vorrichtung")) vo = *v;
        vo["an"] = false;
        einst["vorrichtung"] = vo;
    } else if (art == RUECK) {
        bj::object zw;
        if (auto z = unter(einst, "zweiseitig")) zw = *z;
        zw["an"] = false;
        einst["zweiseitig"] = zw;
        seite_aktiv = 1;
    }
    schreiben(TR("Zeichenfläche entfernen"));
    platte_waehlen(art == PLATTE ? std::max(1, platte - 1) : 1, true);
}

int HirchFraesen::Impl::index_von(std::string const &id)
{
    if (auto bea = liste_von(einst, "bearbeitungen")) {
        for (std::size_t i = 0; i < bea->size(); ++i) {
            if (auto o = (*bea)[i].if_object(); o && str(*o, "id") == id) return (int)i;
        }
    }
    return -1;
}

void HirchFraesen::Impl::strahl_zeichnen(Cr const &cr)
{
    // art 0 Fräsen, 1 Halt, 2 Wenden, 3 Umspannen; offen: noch nicht (neu) gerechnet – blass, gestrichelt
    struct Stueck { double min; double r, g, b; int art; std::string key, id; bool offen = false; bool schmal = false; };
    std::vector<Stueck> st;
    double summe = 0;
    if (auto ab = ablauf_daten()) {
        if (auto seiten = liste_von(*ab, "seiten")) {
            for (auto const &sv : *seiten) {
                auto so = sv.if_object();
                if (!so) continue;
                if (auto abs = liste_von(*so, "abschnitte")) {
                    for (auto const &av : *abs) {
                        auto a = av.if_object();
                        if (!a) continue;
                        if (flag(*a, "nach_wenden", false)) st.push_back({0, 0, 0, 0, 2, {}, {}});
                        if (flag(*a, "nach_umspannen", false)) st.push_back({0, 0, 0, 0, 3, {}, {}});
                        if (flag(*a, "wechsel", false)) st.push_back({0, 0, 0, 0, 1, {}, {}});
                        if (auto ops = liste_von(*a, "ops")) {
                            for (auto const &ov : *ops) {
                                auto o = ov.if_object();
                                if (!o) continue;
                                auto oid = str(*o, "id");
                                if (index_von(oid) < 0 && geloeschte_ids.count(oid)) continue;   // gelöscht
                                double r = 0.5, g = 0.5, b = 0.5;
                                hex_rgb(str(*o, "farbe"), r, g, b);
                                auto key = op_schluessel(*o);
                                bool ungerechnet = flag(*o, "ungerechnet", false);
                                bool bekannt = !ungerechnet && (ergebnis_aktuell || zeit_cache.count(key));
                                double m = bekannt ? std::max(op_minuten(*o), 0.0) : 0.0;
                                if (zieh >= 0 && zieh < (int)strahl_teile.size() && strahl_teile[zieh].key == key)
                                    m = zieh_neu;
                                Stueck x{m, r, g, b, 0, key, oid};
                                x.offen = !ergebnis_aktuell || ungerechnet;
                                x.schmal = !bekannt;
                                st.push_back(x);
                                summe += m;
                            }
                        }
                    }
                }
            }
        }
    }
    int w = strahl.get_allocated_width(), h = strahl.get_allocated_height();
    if (st.empty()) return;
    auto fg = strahl.get_style_context()->get_color(strahl.get_state_flags());
    int fraesen = 0, marken = 0, schmale = 0;
    for (auto const &x : st) {
        (x.art == 0 ? fraesen : marken)++;
        if (x.art == 0 && x.schmal) ++schmale;
    }
    double luecke = 2, marke = 3, schmal_b = 12;
    double frei = w - marken * (marke + 2 * luecke) - std::max(0, fraesen - 1) * luecke;
    // ohne bekannte Zeit: ein schmales Feld an seinem Platz; der Rest teilt sich nach Zeit
    if (schmale < fraesen) frei = std::max(0.0, frei - schmale * schmal_b);
    else schmal_b = frei / std::max(1, fraesen);
    double x = 0;
    bool vorher_fraesen = false;
    std::vector<StrahlStueck> neu_teile;
    for (auto const &e : st) {
        if (e.art == 0) {
            if (vorher_fraesen) x += luecke;
            double bw = e.schmal ? schmal_b
                      : summe > 0 ? frei * e.min / summe : frei / std::max(1, fraesen - schmale);
            bw = std::max(bw, 3.0);
            rundrechteck(cr, x, 3, bw, h - 6, 2);
            if (e.offen) {
                // noch nicht gerechnet: blass mit gestricheltem Rand in der Farbe der Bearbeitung
                cr->set_source_rgba(e.r, e.g, e.b, 0.35);
                cr->fill_preserve();
                cr->set_source_rgb(e.r, e.g, e.b);
                cr->set_line_width(1);
                cr->set_dash(std::vector<double>{3, 2}, 0);
                cr->stroke();
                cr->unset_dash();
            } else {
                cr->set_source_rgb(e.r, e.g, e.b);
                cr->fill();
            }
            bool gezogen = zieh >= 0 && zieh < (int)strahl_teile.size() && strahl_teile[zieh].key == e.key;
            if (gezogen && zieh_zu_schnell && blink_an) {
                // zu schnell: orange blinkender Rahmen
                rundrechteck(cr, x + 0.5, 1.5, bw - 1, h - 3, 3);
                cr->set_source_rgb(1.0, 0.55, 0.0);
                cr->set_line_width(2.5);
                cr->stroke();
            }
            neu_teile.push_back({x, x + bw, e.key, e.id, e.min});
            x += bw;
            vorher_fraesen = true;
        } else {
            x += luecke;
            bool gross = e.art == 2 || e.art == 3;
            cr->rectangle(x, gross ? 0 : 1, marke, gross ? h : h - 2);
            if (e.art == 2) cr->set_source_rgb(0.57, 0.25, 0.67);
            else if (e.art == 3) cr->set_source_rgb(0.90, 0.65, 0.04);      // Umspannen: orange wie die Zonen
            else cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.85);
            cr->fill();
            x += marke + luecke;
            vorher_fraesen = false;
        }
    }
    if (zieh < 0) strahl_teile = std::move(neu_teile);   // während des Ziehens: Lage vom Anfang
}

int HirchFraesen::Impl::strahl_kante(double x)
{
    int best = -1;
    double bd = 6;
    for (int i = 0; i < (int)strahl_teile.size(); ++i) {
        double dd = std::abs(x - strahl_teile[i].x1);
        if (dd <= bd) { bd = dd; best = i; }
    }
    return best;
}

int HirchFraesen::Impl::strahl_abschnitt(double x)
{
    for (int i = 0; i < (int)strahl_teile.size(); ++i)
        if (x >= strahl_teile[i].x0 && x <= strahl_teile[i].x1) return i;
    return -1;
}

bj::object const *HirchFraesen::Impl::bearbeitung_statistik(std::string const &id)
{
    if (auto sts = unter(ergebnis, "bearbeitungen")) return unter(*sts, id.c_str());
    return nullptr;
}

std::string HirchFraesen::Impl::strahl_text(int i, double f, double minuten, double alt_min)
{
    auto const &t = strahl_teile[i];
    int idx = index_von(t.id);
    std::string name = idx >= 0 ? name_von(*bearbeitung(idx)) : t.id;
    std::string txt = name + " · " + std::to_string((int)std::lround(f)) + " %";
    if (auto st = bearbeitung_statistik(t.id)) {
        double basis = num(*st, "vorschub_basis", 0), vmax = num(*st, "vorschub_max", 0);
        double v = basis * f / 100.0;
        if (vmax > 0) v = std::min(v, vmax);       // so fährt die Maschine wirklich
        if (basis > 0) txt += " · " + ganz(v) + " mm/min";
    }
    txt += " · " + kurzzeit(minuten);
    if (std::abs(minuten - alt_min) > 1e-6) txt += TR(" statt ") + kurzzeit(alt_min);
    return txt;
}

/// Woraus der gefahrene Vorschub entsteht (Peter 27.10.): Werkzeug × Bearbeitung × Zeitleiste, höchstens die Maschine
std::string HirchFraesen::Impl::strahl_rechnung(int i, double f)
{
    auto const &t = strahl_teile[i];
    auto st = bearbeitung_statistik(t.id);
    if (!st) return {};
    double basis = num(*st, "vorschub_basis", 0), vmax = num(*st, "vorschub_max", 0);
    if (basis <= 0) return {};
    double wz = num(*st, "vorschub_empf", 0), mm = num(*st, "vorschub_bearbeitung_mm", 0);
    double pr = num(*st, "vorschub_bearbeitung_prozent", 100);
    std::string z;
    if (wz > 0) z += TRF("Werkzeug {} mm/min", {ganz(wz)}) + "\n";
    if (mm > 0) z += TRF("Bearbeitung: eigener Vorschub {} mm/min", {ganz(mm)}) + "\n";
    else if (std::abs(pr - 100) > 0.5) z += TRF("Bearbeitung × {} % = {} mm/min", {ganz(pr), ganz(basis)}) + "\n";
    else if (wz <= 0 || std::abs(basis - wz) > 0.5) z += TRF("Bearbeitung {} mm/min", {ganz(basis)}) + "\n";
    double v = basis * f / 100.0;
    if (std::abs(f - 100) > 0.5) z += TRF("Zeitleiste × {} % = {} mm/min", {ganz(f), ganz(v)}) + "\n";
    if (vmax > 0 && v > vmax + 1e-6) {
        z += TRF("durch die Maschine begrenzt auf {} mm/min", {ganz(vmax)}) + "\n";
        v = vmax;
    }
    z += TRF("Gefahren: {} mm/min", {ganz(v)});
    return z;
}

bool HirchFraesen::Impl::strahl_druck(GdkEventButton *e)
{
    if (e->button != 1 || !ergebnis_aktuell) return false;
    if (e->type == GDK_2BUTTON_PRESS) {
        // Doppelklick: Vorschub dieser Bearbeitung zurück auf 100 %
        int i = strahl_abschnitt(e->x);
        if (i < 0) return true;
        int idx = index_von(strahl_teile[i].id);
        if (auto b = bearbeitung(idx); b && b->contains("vorschub_faktor")) {
            b->erase("vorschub_faktor");
            schreiben(TR("Vorschub zurück auf 100 %"));
        }
        return true;
    }
    int i = strahl_kante(e->x);
    if (i < 0) return false;
    auto const &t = strahl_teile[i];
    int idx = index_von(t.id);
    if (idx < 0) return false;
    double vm = 0;
    if (auto tv = unter(ergebnis, "teil_vorschub")) vm = num(*tv, t.key.c_str(), 0);
    if (vm <= 1e-6) return false;            // ohne Fräsvorschub (z. B. nur Bohren): nichts zu ziehen
    zieh = i;
    zieh_min = zieh_neu = t.min;
    zieh_vm = vm;
    zieh_rest = std::max(0.0, t.min - vm);
    zieh_f_alt = zieh_f = num(*bearbeitung(idx), "vorschub_faktor", 100);
    zieh_zu_schnell = false;
    strahl_info.set_text(strahl_text(i, zieh_f, zieh_neu, zieh_min));
    return true;
}

bool HirchFraesen::Impl::strahl_bewegung(GdkEventMotion *e)
{
    if (zieh < 0) {
        int i = ergebnis_aktuell ? strahl_kante(e->x) : -1;
        if (auto w = strahl.get_window()) {
            if (i >= 0) w->set_cursor(Gdk::Cursor::create(strahl.get_display(), "col-resize"));
            else w->set_cursor();
        }
        if (i >= 0) {
            int idx = index_von(strahl_teile[i].id);
            double f = idx >= 0 ? num(*bearbeitung(idx), "vorschub_faktor", 100) : 100;
            strahl_info.set_text(strahl_text(i, f, strahl_teile[i].min, strahl_teile[i].min) +
                                 TR(" · ziehen: Vorschub ändern"));
            strahl.set_tooltip_text(strahl_rechnung(i, f));
        } else if (int j = ergebnis_aktuell ? strahl_abschnitt(e->x) : -1; j >= 0) {
            int idx = index_von(strahl_teile[j].id);
            double f = idx >= 0 ? num(*bearbeitung(idx), "vorschub_faktor", 100) : 100;
            strahl_info.set_text(strahl_text(j, f, strahl_teile[j].min, strahl_teile[j].min) +
                                 TR(" · rechte Kante ziehen: Vorschub · Doppelklick: 100 %"));
            strahl.set_tooltip_text(strahl_rechnung(j, f));
        } else {
            strahl_info.set_text("");
            strahl.set_tooltip_text("");
        }
        return false;
    }
    auto const &t = strahl_teile[zieh];
    double breite = std::max(1.0, t.x1 - t.x0);
    double neu = zieh_min * std::max(2.0, e->x - t.x0) / breite;
    // Zeit = Rest (Eilgang, Eintauchen) + Fräsvorschub-Zeit · alt/neu
    double f = neu - zieh_rest > 1e-6 ? zieh_f_alt * zieh_vm / (neu - zieh_rest) : 400.0;
    f = std::clamp(std::round(f / 5.0) * 5.0, 20.0, 400.0);
    zieh_f = f;
    zieh_neu = zieh_rest + zieh_vm * zieh_f_alt / f;
    // zu schnell für Werkzeug und Material?
    bool schnell = false;
    if (auto st = bearbeitung_statistik(t.id)) {
        double basis = num(*st, "vorschub_basis", 0), empf = num(*st, "vorschub_empf", 0);
        schnell = empf > 0 && basis * f / 100.0 > 1.2 * empf;
    }
    if (schnell && !zieh_zu_schnell) {
        blink.disconnect();
        blink_an = true;
        blink = Glib::signal_timeout().connect([this] {
            blink_an = !blink_an;
            strahl.queue_draw();
            return zieh >= 0 && zieh_zu_schnell;
        }, 250);
    }
    zieh_zu_schnell = schnell;
    strahl_info.set_markup(Glib::Markup::escape_text(strahl_text(zieh, zieh_f, zieh_neu, zieh_min)) +
                           (schnell ? TR("  <span foreground='#e07000'><b>Achtung, zu schnell!</b></span>") : ""));
    strahl.queue_draw();
    return true;
}

bool HirchFraesen::Impl::strahl_los(GdkEventButton *)
{
    if (zieh < 0) return false;
    auto t = strahl_teile[zieh];
    double f = zieh_f;
    zieh = -1;
    blink.disconnect();
    zieh_zu_schnell = false;
    strahl_info.set_text("");
    int idx = index_von(t.id);
    if (auto b = bearbeitung(idx); b && std::abs(f - zieh_f_alt) > 0.5) {
        if (std::abs(f - 100) < 0.5) b->erase("vorschub_faktor");
        else (*b)["vorschub_faktor"] = (int)std::lround(f);
        // geschätzte Zeit bleibt stehen, bis neu gerechnet ist
        zeit_cache[t.key] = zieh_neu;
        schreiben(TRF("{}: Vorschub {} %", {name_von(*b), std::to_string((int)std::lround(f))}));
    }
    strahl.queue_draw();
    return true;
}

} // namespace Inkscape::UI::Dialog
