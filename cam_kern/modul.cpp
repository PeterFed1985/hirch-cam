// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * hirch_cam – das CAM-Modul von Hirch CNC.
 *
 * Enthält aus dem FreeCAD-CAM-Modul (Tag 1.0.2, src/Mod/CAM):
 *   - libarea (Dan Heeks, BSD) mit Clipper (Angus Johnson, Boost-Lizenz):
 *     Versatz (Offset) mit Bögen, Taschen (Spirale, ZigZag);
 *   - die Mittellinien-Logik der V-Carve (FreeCAD App/Voronoi.cpp,
 *     Path/Op/Vcarve.py, Markus Lampert, LGPL-2.1+), hier auf
 *     Boost.Polygon-Voronoi ohne FreeCAD-Basisklassen nachgebaut.
 * Ohne FreeCAD, OpenCASCADE oder Qt; nur Python und die C++-Laufzeit.
 *
 * Austauschformat für Kurven: Liste von Ecken [typ, x, y, cx, cy];
 * typ 0 = Strecke bis (x, y), 1 = Bogen gegen den Uhrzeiger, -1 = Bogen im
 * Uhrzeigersinn, jeweils um (cx, cy). Die erste Ecke ist der Startpunkt.
 */
#include <cstdlib>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <iostream>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

#include <boost/polygon/point_data.hpp>
#include <boost/polygon/segment_data.hpp>
#include <boost/polygon/voronoi.hpp>

#include "libarea/Area.h"
#include "libarea/Adaptive.hpp"

namespace py = pybind11;

using Ecke = std::array<double, 5>;
using Kurve = std::vector<Ecke>;
using Punkt2 = std::array<double, 2>;
using Ring = std::vector<Punkt2>;

// ---------------------------------------------------------------- libarea
static CArea area_aus(const std::vector<Kurve>& kurven)
{
    CArea a;
    for (const auto& k : kurven) {
        if (k.empty()) {
            continue;
        }
        CCurve c;
        for (const auto& e : k) {
            c.m_vertices.emplace_back(int(e[0]), Point(e[1], e[2]), Point(e[3], e[4]));
        }
        a.append(c);
    }
    return a;
}

static CArea area_aus_ringen(const std::vector<Ring>& ringe)
{
    CArea a;
    for (const auto& r : ringe) {
        if (r.size() < 3) {
            continue;
        }
        CCurve c;
        for (const auto& p : r) {
            c.append(Point(p[0], p[1]));
        }
        if (r.front() != r.back()) {
            c.append(Point(r.front()[0], r.front()[1]));
        }
        a.append(c);
    }
    return a;
}

static std::vector<Kurve> kurven_aus(const std::list<CCurve>& curves)
{
    std::vector<Kurve> aus;
    for (const auto& c : curves) {
        Kurve k;
        for (const auto& v : c.m_vertices) {
            k.push_back({double(v.m_type), v.m_p.x, v.m_p.y, v.m_c.x, v.m_c.y});
        }
        aus.push_back(std::move(k));
    }
    return aus;
}

static void genauigkeit(double toleranz)
{
    CArea::set_accuracy(toleranz > 0 ? toleranz : 0.01);
    CArea::set_units(1.0);
}

/// Versatz um «abstand» (positiv = nach aussen) – wie FreeCAD Path.Area
/// (ClipperOffset, runde Ecken). Ringe dürfen Löcher enthalten (gerade-ungerade).
static std::vector<Kurve> versatz(const std::vector<Ring>& ringe, double abstand, double toleranz)
{
    genauigkeit(toleranz);
    CArea a = area_aus_ringen(ringe);
    a.Reorder();
    a.OffsetWithClipper(abstand, ClipperLib::jtRound, ClipperLib::etClosedPolygon, 2.0, 0.0);
    return kurven_aus(a.m_curves);
}

/// Taschen-Werkzeugweg wie FreeCAD (libarea MakePocketToolpath).
/// modus: 0 Spirale, 1 ZigZag, 2 nur ein Versatz, 3 ZigZag + ein Versatz.
static std::vector<Kurve> tasche(const std::vector<Ring>& ringe, double werkzeug_radius,
                                 double zugabe, double bahnabstand, int modus, double winkel,
                                 bool von_innen, double toleranz)
{
    genauigkeit(toleranz);
    CArea a = area_aus_ringen(ringe);
    a.Reorder();
    CAreaPocketParams p(werkzeug_radius, zugabe, bahnabstand, von_innen, PocketMode(modus), winkel);
    std::list<CCurve> weg;
    a.MakePocketToolpath(weg, p);
    return kurven_aus(weg);
}

// ---------------------------------------------------------------- Voronoi
namespace bp = boost::polygon;
using Ganz = int32_t;
using VPunkt = bp::point_data<Ganz>;
using VStrecke = bp::segment_data<Ganz>;
using Diagramm = bp::voronoi_diagram<double>;
using Kante = Diagramm::edge_type;
using Ecke_t = Diagramm::vertex_type;

// Farben wie FreeCAD Path/Op/Vcarve.py
enum Farbe : std::size_t { PRIMAER = 0, SEKUNDAER = 1, AUSSEN1 = 2, AUSSEN2 = 3, KOLLINEAR = 4,
                           ZWILLING = 5, GRENZE = 6 };

struct Medial {
    double skala = 1000.0;   // wie FreeCAD: mm * 1000 -> ganze Zahlen
    std::vector<VStrecke> strecken;
    std::vector<Ring> ringe;  // mm, für den Innen-Test
    Diagramm vd;
    std::unordered_map<const Ecke_t*, bool> aussen_cache;
    std::map<Ganz, std::set<Ganz>> endpunkte;

    VPunkt punkt_von(const Diagramm::cell_type* c) const
    {
        auto i = c->source_index();
        auto kat = c->source_category();
        if (kat == bp::SOURCE_CATEGORY_SEGMENT_START_POINT) return bp::low(strecken[i]);
        return bp::high(strecken[i]);
    }
    const VStrecke& strecke_von(const Diagramm::cell_type* c) const { return strecken[c->source_index()]; }

    static double abstand(double ax, double ay, double bx, double by) { return std::hypot(ax - bx, ay - by); }

    // senkrechte Projektion eines Punkts auf die Gerade einer Strecke (wie FreeCAD)
    static void projektion(double px, double py, const VStrecke& s, double& qx, double& qy)
    {
        double ax = bp::low(s).x(), ay = bp::low(s).y(), bx = bp::high(s).x(), by = bp::high(s).y();
        double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
        double t = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;
        qx = ax + t * dx;
        qy = ay + t * dy;
    }

    bool punkt_auf_strecke(const VPunkt& p, const VStrecke& s) const
    {
        auto gleich = [&](const VPunkt& a, const VPunkt& b) {
            return 1e-6 > abstand(a.x(), a.y(), b.x(), b.y()) / skala;
        };
        return gleich(p, bp::low(s)) || gleich(p, bp::high(s));
    }

    bool grenzlinie(const Kante& e) const   // FreeCAD isBorderline()
    {
        if (!e.is_finite() || e.is_linear()) return false;
        bool pc = e.cell()->contains_point();
        VPunkt p = pc ? punkt_von(e.cell()) : punkt_von(e.twin()->cell());
        const VStrecke& s = pc ? strecke_von(e.twin()->cell()) : strecke_von(e.cell());
        return punkt_auf_strecke(p, s);
    }

    void aussen_faerben(const Kante* e, std::size_t farbe)   // FreeCAD colorExterior(edge)
    {
        if (e->color()) return;
        e->color(farbe);
        e->twin()->color(farbe);
        auto v = e->vertex1();
        if (!v || !e->is_primary()) return;
        v->color(farbe);
        auto k = v->incident_edge();
        do {
            aussen_faerben(k, farbe);
            k = k->rot_next();
        } while (k != v->incident_edge());
    }

    // Innen-Test mit Toleranz (FreeCAD: face.isInside(p, tol, True))
    bool innen(double x, double y, double tol) const
    {
        bool drin = false;
        double best = 1e300;
        for (const auto& r : ringe) {
            std::size_t n = r.size();
            for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
                double xi = r[i][0], yi = r[i][1], xj = r[j][0], yj = r[j][1];
                if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) drin = !drin;
                double dx = xj - xi, dy = yj - yi, l2 = dx * dx + dy * dy;
                double t = l2 > 0 ? std::clamp(((x - xi) * dx + (y - yi) * dy) / l2, 0.0, 1.0) : 0;
                best = std::min(best, std::hypot(x - (xi + t * dx), y - (yi + t * dy)));
            }
        }
        return drin || best <= tol;
    }

    bool aussen(const Ecke_t* v, double tol)
    {
        if (!v) return true;
        auto it = aussen_cache.find(v);
        if (it != aussen_cache.end()) return it->second;
        bool a = !innen(v->x() / skala, v->y() / skala, tol);
        aussen_cache[v] = a;
        return a;
    }

    static double winkel(const VStrecke& s)
    {
        double x0 = bp::low(s).x(), y0 = bp::low(s).y(), x1 = bp::high(s).x(), y1 = bp::high(s).y();
        if (x0 == x1) return y0 < y1 ? M_PI_2 : -M_PI_2;
        return std::atan((y0 - y1) / (x0 - x1));
    }

    static bool verbunden(const VStrecke& a, const VStrecke& b)
    {
        auto g = [](const VPunkt& p, const VPunkt& q) { return p.x() == q.x() && p.y() == q.y(); };
        return g(bp::low(a), bp::low(b)) || g(bp::low(a), bp::high(b)) || g(bp::high(a), bp::low(b))
            || g(bp::high(a), bp::high(b));
    }

    // Abstand einer Voronoi-Ecke zur Kontur (FreeCAD getDistances), mm
    double ecken_abstand(const Kante& e, const Ecke_t* v) const
    {
        const auto* c0 = e.cell();
        const auto* c1 = e.twin()->cell();
        if (c0->contains_point() || c1->contains_point()) {
            VPunkt p = c0->contains_point() ? punkt_von(c0) : punkt_von(c1);
            return abstand(v->x(), v->y(), p.x(), p.y()) / skala;
        }
        double qx, qy;
        projektion(v->x(), v->y(), strecke_von(c0), qx, qy);
        return abstand(v->x(), v->y(), qx, qy) / skala;
    }
};

/// Mittellinien (Medial Axis) für V-Carve wie FreeCAD Path/Op/Vcarve.py.
/// Liefert Kanten [ecke0, ecke1, [[x, y, abstand_zur_kontur], …]] in mm;
/// gebogene Kanten (Parabeln) sind mit «toleranz» in Punkte aufgelöst.
static std::vector<py::tuple> mittellinien(const std::vector<Ring>& ringe, double toleranz,
                                           double kollinear_grad, double innen_toleranz)
{
    Medial m;
    m.ringe = ringe;
    for (const auto& r : ringe) {
        std::vector<VPunkt> pts;
        for (const auto& p : r) {
            VPunkt q(Ganz(std::lround(p[0] * m.skala)), Ganz(std::lround(p[1] * m.skala)));
            if (pts.empty() || pts.back().x() != q.x() || pts.back().y() != q.y()) pts.push_back(q);
        }
        while (pts.size() > 1 && pts.front().x() == pts.back().x() && pts.front().y() == pts.back().y()) {
            pts.pop_back();
        }
        if (pts.size() < 3) continue;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            m.strecken.emplace_back(pts[i], pts[(i + 1) % pts.size()]);
        }
    }
    std::vector<py::tuple> aus;
    if (m.strecken.empty()) return aus;
    bp::construct_voronoi(m.strecken.begin(), m.strecken.end(), &m.vd);

    // Farben wie in FreeCAD: primär/Grenzlinie/sekundär, aussen, kollinear, Zwillinge
    for (auto& e : m.vd.edges()) {
        if (e.is_primary()) {
            e.color(m.grenzlinie(e) ? GRENZE : PRIMAER);
        } else {
            e.color(SEKUNDAER);
        }
    }
    for (auto& e : m.vd.edges()) {
        if (e.is_infinite()) m.aussen_faerben(&e, AUSSEN1);
    }
    for (auto& s : m.strecken) {
        m.endpunkte[bp::low(s).x()].insert(bp::low(s).y());
        m.endpunkte[bp::high(s).x()].insert(bp::high(s).y());
    }
    for (auto& e : m.vd.edges()) {
        if (e.is_finite() && e.color() == 0) {
            const Ecke_t* v0 = e.vertex0();
            const Ecke_t* v1 = e.vertex1();
            if (m.aussen(v0, innen_toleranz) && m.aussen(v1, innen_toleranz)) {
                m.aussen_faerben(&e, AUSSEN2);
            } else if (m.aussen(v1, innen_toleranz)) {
                auto ys = m.endpunkte.find(Ganz(v0->x()));
                if (ys != m.endpunkte.end() && ys->second.count(Ganz(v0->y()))) m.aussen_faerben(&e, AUSSEN2);
            }
        }
    }
    double rad = kollinear_grad * M_PI / 180.0;
    for (auto& e : m.vd.edges()) {
        if (e.color() == 0 && e.cell()->contains_segment() && e.twin()->cell()->contains_segment()) {
            const auto& s0 = m.strecke_von(e.cell());
            const auto& s1 = m.strecke_von(e.twin()->cell());
            if (Medial::verbunden(s0, s1)) {
                double a = Medial::winkel(s0) - Medial::winkel(s1);
                if (a > M_PI_2) a -= M_PI;
                else if (a < -M_PI_2) a += M_PI;
                if (std::fabs(a) < rad) {
                    e.color(KOLLINEAR);
                    e.twin()->color(KOLLINEAR);
                }
            }
        }
    }
    for (auto& e : m.vd.edges()) {
        if (!e.color() && !e.twin()->color()) e.twin()->color(ZWILLING);
    }

    // Ecken nummerieren
    std::unordered_map<const Ecke_t*, int> nummer;
    int n = 0;
    for (auto& v : m.vd.vertices()) nummer[&v] = n++;

    for (auto& e : m.vd.edges()) {
        if (e.color() != PRIMAER || !e.is_finite()) continue;
        const Ecke_t* v0 = e.vertex0();
        const Ecke_t* v1 = e.vertex1();
        std::vector<std::array<double, 3>> pts;
        double d0 = m.ecken_abstand(e, v0), d1 = m.ecken_abstand(e, v1);
        bool punkt_zelle = e.cell()->contains_point() || e.twin()->cell()->contains_point();
        if (e.is_linear() && punkt_zelle) {
            // Gerade Kante neben einem Eckpunkt S (z. B. zwischen zwei inneren Ecken): der
            // Randabstand ist |P − S| und damit NICHT linear (Hyperbel, in der Mitte kleiner).
            // Abtasten mit exaktem Abstand, sonst würde dazwischen zu tief gefräst.
            VPunkt sp = e.cell()->contains_point() ? m.punkt_von(e.cell()) : m.punkt_von(e.twin()->cell());
            double sx = sp.x(), sy = sp.y();
            double ax = v0->x(), ay = v0->y(), bx = v1->x(), by = v1->y();
            double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
            double laenge = std::sqrt(l2) / m.skala;
            // kleinster Abstand auf der Kante (Lot von S), daraus die Schrittweite:
            // Sehnenfehler von sqrt(h² + s²) ~ Schritt² / (8·h)
            double t = l2 > 0 ? std::clamp(((sx - ax) * dx + (sy - ay) * dy) / l2, 0.0, 1.0) : 0.0;
            double h = Medial::abstand(ax + t * dx, ay + t * dy, sx, sy) / m.skala;
            double schritt = std::sqrt(8.0 * std::max(toleranz, 1e-4) * std::max(h, 0.01));
            int stuecke = std::max(1, int(std::ceil(laenge / std::max(schritt, 1e-3))));
            stuecke = std::min(stuecke, 400);
            pts.push_back({v0->x() / m.skala, v0->y() / m.skala, d0});
            for (int i = 1; i < stuecke; ++i) {
                double px = ax + dx * i / stuecke, py = ay + dy * i / stuecke;
                pts.push_back({px / m.skala, py / m.skala, Medial::abstand(px, py, sx, sy) / m.skala});
            }
            pts.push_back({v1->x() / m.skala, v1->y() / m.skala, d1});
        } else if (e.is_linear()) {
            pts.push_back({v0->x() / m.skala, v0->y() / m.skala, d0});
            pts.push_back({v1->x() / m.skala, v1->y() / m.skala, d1});
        } else {
            // Parabel zwischen Punkt S und Strecke: P = X + n·h, h = |X−S|² / (2·d)
            bool pc = e.cell()->contains_point();
            VPunkt sp = pc ? m.punkt_von(e.cell()) : m.punkt_von(e.twin()->cell());
            const VStrecke& s = pc ? m.strecke_von(e.twin()->cell()) : m.strecke_von(e.cell());
            double ax = bp::low(s).x(), ay = bp::low(s).y(), bx = bp::high(s).x(), by = bp::high(s).y();
            double ux = bx - ax, uy = by - ay, ul = std::hypot(ux, uy);
            ux /= ul; uy /= ul;
            double nx = -uy, ny = ux;
            double sx = sp.x(), sy = sp.y();
            double dS = (sx - ax) * nx + (sy - ay) * ny;   // Abstand S zur Geraden (vorzeichenbehaftet)
            if (dS < 0) { nx = -nx; ny = -ny; dS = -dS; }
            auto t_von = [&](double px, double py) { return (px - ax) * ux + (py - ay) * uy; };
            double t0 = t_von(v0->x(), v0->y()), t1 = t_von(v1->x(), v1->y());
            double laenge = std::hypot(v1->x() - v0->x(), v1->y() - v0->y()) / m.skala;
            // Sehnenfehler einer Parabel ~ L²/(8·R); R ≈ Brennweite → Schrittweite
            double brenn = std::max(dS / m.skala / 2.0, 1e-3);
            double schritt = std::sqrt(8.0 * std::max(toleranz, 1e-4) * brenn);
            int stuecke = std::max(1, int(std::ceil(laenge / std::max(schritt, 1e-3))));
            stuecke = std::min(stuecke, 400);
            pts.push_back({v0->x() / m.skala, v0->y() / m.skala, d0});
            for (int i = 1; i < stuecke; ++i) {
                double t = t0 + (t1 - t0) * i / stuecke;
                double xx = ax + ux * t, xy = ay + uy * t;
                double h = ((xx - sx) * (xx - sx) + (xy - sy) * (xy - sy)) / (2.0 * dS);
                double px = xx + nx * h, py = xy + ny * h;
                pts.push_back({px / m.skala, py / m.skala, h / m.skala});
            }
            pts.push_back({v1->x() / m.skala, v1->y() / m.skala, d1});
        }
        aus.push_back(py::make_tuple(nummer[v0], nummer[v1], pts));
    }
    return aus;
}

// ---------------------------------------------------------------- Adaptiv
/// Adaptives Ausräumen (FreeCAD CAM «Adaptive», Kresimir Tusek): Bahnen mit
/// gleichbleibender Schnittbreite. Liefert je Bereich
/// (Helix-Mitte, Startpunkt, [(Bewegungsart, [[x, y], …]), …]).
/// Bewegungsart: 0 schneiden, 1 Übergang im Freien, 2 Übergang durch
/// Material (abheben), 3 Übergang frei auf der vorigen Ebene.
static py::list adaptiv(const std::vector<Ring>& ringe, const std::vector<Ring>& rohteil,
                        double werkzeug_d, double helix_d, double bahnabstand_faktor,
                        double toleranz, double zugabe, bool schlichtbahn)
{
    // Adaptive wählt Ausweichwinkel zufällig: für jede Tasche neu bei 1 anfangen,
    // damit dieselbe Zeichnung immer denselben G-Code ergibt (auch mehrere im Prozess,
    // und unter Windows wie unter Linux – eigener Generator statt rand())
    AdaptivePath::ZufallSaat(1);
    AdaptivePath::Adaptive2d a;
    a.toolDiameter = werkzeug_d;
    a.helixRampDiameter = helix_d;
    a.stepOverFactor = bahnabstand_faktor;
    a.tolerance = toleranz;
    a.stockToLeave = zugabe;
    a.forceInsideOut = true;
    a.finishingProfile = schlichtbahn;
    a.opType = AdaptivePath::OperationType::otClearingInside;
    auto umwandeln = [](const std::vector<Ring>& rs) {
        AdaptivePath::DPaths aus;
        for (const auto& r : rs) {
            AdaptivePath::DPath p;
            for (const auto& q : r) p.emplace_back(q[0], q[1]);
            aus.push_back(std::move(p));
        }
        return aus;
    };
    std::list<AdaptivePath::AdaptiveOutput> erg;
    {
        // Adaptive schreibt Fortschritt nach std::cout – das Panel liest aber
        // JSON von stdout: Ausgabe so lange still legen
        py::gil_scoped_release frei;
        auto alt = std::cout.rdbuf(nullptr);
        erg = a.Execute(umwandeln(rohteil), umwandeln(ringe), [](AdaptivePath::TPaths) { return false; });
        std::cout.rdbuf(alt);
        std::cout.clear();
    }
    py::list aus;
    for (const auto& o : erg) {
        py::list wege;
        for (const auto& tp : o.AdaptivePaths) {
            py::list pts;
            for (const auto& q : tp.second) pts.append(py::make_tuple(q.first, q.second));
            wege.append(py::make_tuple(tp.first, pts));
        }
        aus.append(py::make_tuple(py::make_tuple(o.HelixCenterPoint.first, o.HelixCenterPoint.second),
                                  py::make_tuple(o.StartPoint.first, o.StartPoint.second), wege,
                                  o.ReturnMotionType));
    }
    return aus;
}

PYBIND11_MODULE(hirch_cam, mod)
{
    mod.doc() = "CAM-Modul von Hirch CNC: libarea (FreeCAD CAM) und V-Carve-Mittellinien";
    mod.attr("VERSION") = "1.0.2-hirch3";
    mod.def("versatz", &versatz, py::arg("ringe"), py::arg("abstand"), py::arg("toleranz") = 0.01,
            "Versatz der Fläche um «abstand» mm (positiv nach aussen), Ergebnis als Kurven");
    mod.def("tasche", &tasche, py::arg("ringe"), py::arg("werkzeug_radius"), py::arg("zugabe") = 0.0,
            py::arg("bahnabstand") = 1.0, py::arg("modus") = 0, py::arg("winkel") = 0.0,
            py::arg("von_innen") = false, py::arg("toleranz") = 0.01,
            "Taschen-Werkzeugweg (libarea MakePocketToolpath)");
    mod.def("mittellinien", &mittellinien, py::arg("ringe"), py::arg("toleranz") = 0.01,
            py::arg("kollinear_grad") = 10.0, py::arg("innen_toleranz") = 0.01,
            "Mittellinien für V-Carve (Voronoi wie FreeCAD Vcarve)");
    mod.def("adaptiv", &adaptiv, py::arg("ringe"), py::arg("rohteil"), py::arg("werkzeug_d"),
            py::arg("helix_d"), py::arg("bahnabstand_faktor") = 0.2, py::arg("toleranz") = 0.05,
            py::arg("zugabe") = 0.0, py::arg("schlichtbahn") = true,
            "Adaptives Ausräumen (FreeCAD CAM Adaptive)");
}
