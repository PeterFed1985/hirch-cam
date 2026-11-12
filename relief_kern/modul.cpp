// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Peter Federspiel
// hirch_relief – Rechenkern der 3D-Fräsung (Hirch CNC)
//
//   hoehenkarten(dreiecke, x0, y0, zelle, nx, ny) -> (oben, unten, flaechen_oben)
//       Oberseite/Unterseite eines Dreiecksnetzes als Höhenraster (Draufsicht). Rasterpunkt
//       (i, j) liegt bei (x0 + i·zelle, y0 + j·zelle). Ausserhalb des Modells NaN.
//       flaechen_oben zählt je Punkt die nach oben gewandten Flächen über ihm; mehr als eine
//       heisst: von oben verdeckt (Hinterschnitt).
//   step_lesen(pfad, abweichung) -> dict          (nur mit OpenCASCADE gebaut)
//       Netz (Dreiecke), Flächen (Art, Fläche) und Kanten (Punkte, Arten der beiden Flächen).
//   mit_step() -> bool
//   abtasten(dreiecke, werkzeug, punkte, z_min) -> z         (nur mit OpenCAMLib gebaut)
//       Drop-Cutter: wie tief der Fräser an jedem Punkt (x, y) sinken kann, ohne das Netz zu
//       verletzen (Fräserspitze). werkzeug = {"typ": kugel|schaft|eckradius|v|konisch, "durchmesser",
//       "eckradius", "v_winkel", "laenge"}. Nie tiefer als z_min.
//   Abtaster(dreiecke, werkzeug).abtasten(punkte, z_min) -> z   (nur mit OpenCAMLib)
//       wie abtasten; der Suchbaum wird einmal gebaut und für jeden Aufruf wiederverwendet.
//   mit_ocl() -> bool
//   abtragen(hoehe, x0, y0, zelle, bewegungen, werkzeug, start, boden)
//       Fräsen auf einem Höhenraster (Simulation): hoehe (ny, nx, float32) wird an Ort und Stelle
//       abgesenkt. bewegungen (N, 3): Ziele der Fräserspitze nacheinander ab start (x, y, z).
//       werkzeug wie bei abtasten (+ "spitze" beim V-Fräser). Nie tiefer als boden.
//   konturen(raster, schwelle, x0, y0, zelle) -> [ (M, 2) ]
//       Umrisse der Stellen mit raster > schwelle (Marching Squares, geschlossene Ringe;
//       NaN zählt als «nicht darüber»). Rasterpunkt (i, j) liegt bei (x0 + i·zelle, y0 + j·zelle).
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include <algorithm>
#include <cstdint>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <thread>
#include <unordered_map>
#include <limits>
#include <string>
#include <vector>

namespace py = pybind11;
using Arr = py::array_t<float, py::array::c_style | py::array::forcecast>;

// --------------------------------------------------------------------------------------------
// Höhenraster: jedes Dreieck wird in der Draufsicht gerastert (Füllregel oben/links, damit ein
// Punkt auf einer gemeinsamen Kante genau einem Dreieck gehört).

static inline bool oben_links(double ax, double ay, double bx, double by) {
    // Kante a->b eines gegen den Uhrzeigersinn laufenden Dreiecks
    return (ay == by && bx < ax) || (by < ay);
}

static py::tuple hoehenkarten(Arr dreiecke, double x0, double y0, double zelle, int nx, int ny) {
    if (dreiecke.ndim() != 3 || dreiecke.shape(1) != 3 || dreiecke.shape(2) != 3)
        throw std::runtime_error("dreiecke muss die Form (N, 3, 3) haben");
    if (nx <= 0 || ny <= 0 || zelle <= 0) throw std::runtime_error("Raster ungültig");
    const float* d = dreiecke.data();
    const py::ssize_t n = dreiecke.shape(0);
    const float NaN = std::numeric_limits<float>::quiet_NaN();
    Arr oben({ny, nx}), unten({ny, nx});
    py::array_t<uint8_t> anzahl({ny, nx});
    float* T = oben.mutable_data();
    float* B = unten.mutable_data();
    uint8_t* U = anzahl.mutable_data();
    std::fill(T, T + (size_t)nx * ny, -std::numeric_limits<float>::infinity());
    std::fill(B, B + (size_t)nx * ny, std::numeric_limits<float>::infinity());
    std::fill(U, U + (size_t)nx * ny, 0);
    {
        py::gil_scoped_release frei;
        for (py::ssize_t k = 0; k < n; ++k) {
            const float* t = d + 9 * k;
            double ax = (t[0] - x0) / zelle, ay = (t[1] - y0) / zelle, az = t[2];
            double bx = (t[3] - x0) / zelle, by = (t[4] - y0) / zelle, bz = t[5];
            double cx = (t[6] - x0) / zelle, cy = (t[7] - y0) / zelle, cz = t[8];
            double fl = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
            if (std::fabs(fl) < 1e-12) continue;            // senkrecht: in der Draufsicht unsichtbar
            const bool aufwaerts = fl > 0;
            if (!aufwaerts) { std::swap(bx, cx); std::swap(by, cy); std::swap(bz, cz); fl = -fl; }
            int i0 = std::max(0, (int)std::ceil(std::min({ax, bx, cx})));
            int i1 = std::min(nx - 1, (int)std::floor(std::max({ax, bx, cx})));
            int j0 = std::max(0, (int)std::ceil(std::min({ay, by, cy})));
            int j1 = std::min(ny - 1, (int)std::floor(std::max({ay, by, cy})));
            if (i0 > i1 || j0 > j1) continue;
            const bool tl0 = oben_links(bx, by, cx, cy), tl1 = oben_links(cx, cy, ax, ay),
                       tl2 = oben_links(ax, ay, bx, by);
            for (int j = j0; j <= j1; ++j) {
                for (int i = i0; i <= i1; ++i) {
                    double w0 = (cx - bx) * (j - by) - (cy - by) * (i - bx);
                    double w1 = (ax - cx) * (j - cy) - (ay - cy) * (i - cx);
                    double w2 = (bx - ax) * (j - ay) - (by - ay) * (i - ax);
                    if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                    if ((w0 == 0 && !tl0) || (w1 == 0 && !tl1) || (w2 == 0 && !tl2)) continue;
                    float z = (float)((w0 * az + w1 * bz + w2 * cz) / fl);
                    size_t p = (size_t)j * nx + i;
                    if (z > T[p]) T[p] = z;
                    if (z < B[p]) B[p] = z;
                    if (aufwaerts && U[p] < 255) U[p]++;
                }
            }
        }
        for (size_t p = 0; p < (size_t)nx * ny; ++p)
            if (!std::isfinite(T[p])) { T[p] = NaN; B[p] = NaN; }
    }
    return py::make_tuple(oben, unten, anzahl);
}


// --------------------------------------------------------------------------------------------
// Marching Squares: Umrisse eines Rasters über einer Schwelle als geschlossene Ringe

static py::list konturen(Arr raster, double schwelle, double x0, double y0, double zelle)
{
    if (raster.ndim() != 2) throw std::runtime_error("raster muss 2D sein");
    const int ny = int(raster.shape(0)), nx = int(raster.shape(1));
    const float *r = raster.data();
    // Ausserhalb gilt als «nicht darüber»: so schliessen sich alle Ringe
    auto drin = [&](int i, int j) -> bool {
        if (i < 0 || j < 0 || i >= nx || j >= ny) return false;
        float v = r[std::size_t(j) * nx + i];
        return std::isfinite(v) && v > schwelle;
    };
    auto wert = [&](int i, int j) -> double {
        if (i < 0 || j < 0 || i >= nx || j >= ny) return schwelle - 1;
        float v = r[std::size_t(j) * nx + i];
        return std::isfinite(v) ? double(v) : schwelle - 1;
    };
    // Kanten-Schlüssel: waagrechte Kante (i,j)-(i+1,j): 2*(j*(nx+2)+i), senkrechte (i,j)-(i,j+1): +1
    const long long W = nx + 2;
    auto key = [&](int i, int j, int senk) { return 2 * ((long long)(j + 1) * W + (i + 1)) + senk; };
    auto punkt = [&](int i, int j, int senk) {
        double a = wert(i, j), b = senk ? wert(i, j + 1) : wert(i + 1, j);
        double t = (a == b) ? 0.5 : std::clamp((schwelle - a) / (b - a), 0.0, 1.0);
        return senk ? std::array<double, 2>{x0 + i * zelle, y0 + (j + t) * zelle}
                    : std::array<double, 2>{x0 + (i + t) * zelle, y0 + j * zelle};
    };
    std::unordered_map<long long, long long> weiter;      // gerichtete Strecke: von Kante -> nach Kante
    std::unordered_map<long long, std::array<int, 3>> lage;
    {
        py::gil_scoped_release frei;
        for (int j = -1; j < ny; ++j)
            for (int i = -1; i < nx; ++i) {
                // Zelle mit Ecken (i,j) (i+1,j) (i+1,j+1) (i,j+1)
                int c = (drin(i, j) ? 1 : 0) | (drin(i + 1, j) ? 2 : 0) | (drin(i + 1, j + 1) ? 4 : 0) |
                        (drin(i, j + 1) ? 8 : 0);
                if (c == 0 || c == 15) continue;
                // Kanten: u = unten (i,j)-(i+1,j), r = rechts (i+1,j)-(i+1,j+1),
                //         o = oben (i,j+1)-(i+1,j+1), l = links (i,j)-(i,j+1)
                long long u = key(i, j, 0), rr = key(i + 1, j, 1), o = key(i, j + 1, 0), l = key(i, j, 1);
                lage[u] = {i, j, 0};
                lage[rr] = {i + 1, j, 1};
                lage[o] = {i, j + 1, 0};
                lage[l] = {i, j, 1};
                // Strecken so gerichtet, dass das Innere links liegt (Ringe gegen den Uhrzeigersinn)
                auto s_ = [&](long long a, long long b) { weiter[b] = a; };   // Tabelle unten im Uhrzeigersinn notiert
                switch (c) {
                    case 1: s_(l, u); break;
                    case 2: s_(u, rr); break;
                    case 3: s_(l, rr); break;
                    case 4: s_(rr, o); break;
                    case 5: s_(l, o); s_(rr, u); break;          // Sattel: getrennt lassen
                    case 6: s_(u, o); break;
                    case 7: s_(l, o); break;
                    case 8: s_(o, l); break;
                    case 9: s_(o, u); break;
                    case 10: s_(o, rr); s_(u, l); break;         // Sattel
                    case 11: s_(o, rr); break;
                    case 12: s_(rr, l); break;
                    case 13: s_(rr, u); break;
                    case 14: s_(u, l); break;
                }
            }
    }
    py::list ringe;
    while (!weiter.empty()) {
        long long start = weiter.begin()->first, k = start;
        std::vector<double> pts;
        const std::size_t grenze = weiter.size() + 2;
        for (std::size_t n = 0; n < grenze; ++n) {
            auto it = weiter.find(k);
            if (it == weiter.end()) break;
            auto const &lg = lage[k];
            auto p = punkt(lg[0], lg[1], lg[2]);
            pts.push_back(p[0]);
            pts.push_back(p[1]);
            long long nach = it->second;
            weiter.erase(it);
            k = nach;
            if (k == start) break;
        }
        if (pts.size() >= 6) {
            py::array_t<double> a({(py::ssize_t)(pts.size() / 2), (py::ssize_t)2});
            std::copy(pts.begin(), pts.end(), a.mutable_data());
            ringe.append(a);
        }
    }
    return ringe;
}

// --------------------------------------------------------------------------------------------
// Mittellinien einer Maske (Zhang–Suen-Verdünnung, dann zu Linienzügen verbunden): für «Kehlen
// nachfahren». maske (ny, nx) uint8, Rasterpunkt (i, j) bei (x0 + i·zelle, y0 + j·zelle).
// Ergebnis: [ (M, 2) ] – Züge von Ende zu Ende bzw. Verzweigung zu Verzweigung; geschlossene Ringe
// enden am Anfangspunkt.
static py::list mittellinien(py::array_t<uint8_t, py::array::c_style | py::array::forcecast> maske, double x0,
                             double y0, double zelle, int kuerzest)
{
    if (maske.ndim() != 2) throw std::runtime_error("maske muss 2D sein");
    const int ny = int(maske.shape(0)), nx = int(maske.shape(1));
    std::vector<uint8_t> b(std::size_t(nx + 2) * (ny + 2), 0);
    const int W = nx + 2;
    auto at = [&](int i, int j) -> uint8_t & { return b[std::size_t(j + 1) * W + (i + 1)]; };
    {
        auto m = maske.unchecked<2>();
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) at(i, j) = m(j, i) ? 1 : 0;
    }
    std::vector<std::vector<std::array<double, 2>>> zuege;
    {
        py::gil_scoped_release frei;
        // Zhang–Suen
        std::vector<std::size_t> weg;
        bool geaendert = true;
        while (geaendert) {
            geaendert = false;
            for (int schritt = 0; schritt < 2; ++schritt) {
                weg.clear();
                for (int j = 0; j < ny; ++j)
                    for (int i = 0; i < nx; ++i) {
                        if (!at(i, j)) continue;
                        int p2 = at(i, j + 1), p3 = at(i + 1, j + 1), p4 = at(i + 1, j), p5 = at(i + 1, j - 1),
                            p6 = at(i, j - 1), p7 = at(i - 1, j - 1), p8 = at(i - 1, j), p9 = at(i - 1, j + 1);
                        int nb = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9;
                        if (nb < 2 || nb > 6) continue;
                        int seq[9] = {p2, p3, p4, p5, p6, p7, p8, p9, p2}, a = 0;
                        for (int k = 0; k < 8; ++k) a += (seq[k] == 0 && seq[k + 1] == 1);
                        if (a != 1) continue;
                        if (schritt == 0 ? (p2 * p4 * p6 == 0 && p4 * p6 * p8 == 0)
                                         : (p2 * p4 * p8 == 0 && p2 * p6 * p8 == 0))
                            weg.push_back(std::size_t(j + 1) * W + (i + 1));
                    }
                for (auto q : weg) b[q] = 0;
                if (!weg.empty()) geaendert = true;
            }
        }
        // Züge: Nachbarn (8er) zählen, von Enden und Verzweigungen aus laufen
        const int di[8] = {1, 1, 0, -1, -1, -1, 0, 1}, dj[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        auto grad = [&](int i, int j) {
            int n = 0;
            for (int k = 0; k < 8; ++k) n += at(i + di[k], j + dj[k]);
            return n;
        };
        std::vector<uint8_t> besucht(b.size(), 0);   // Kante (Punkt) schon in einem Zug
        auto idx = [&](int i, int j) { return std::size_t(j + 1) * W + (i + 1); };
        auto laufen = [&](int i, int j, int k0) {
            std::vector<std::array<double, 2>> z;
            z.push_back({x0 + i * zelle, y0 + j * zelle});
            int ci = i + di[k0], cj = j + dj[k0];
            int si = i, sj = j;
            while (true) {
                z.push_back({x0 + ci * zelle, y0 + cj * zelle});
                if (ci == si && cj == sj) break;          // Ring geschlossen
                int g = grad(ci, cj);
                if (g != 2) break;                        // Ende oder Verzweigung
                besucht[idx(ci, cj)] = 1;
                int ni = -1, nj = -1;
                for (int k = 0; k < 8; ++k) {
                    int ai = ci + di[k], aj = cj + dj[k];
                    if (!at(ai, aj)) continue;
                    auto const &v = z[z.size() - 2];
                    int vi = int(std::lround((v[0] - x0) / zelle)), vj = int(std::lround((v[1] - y0) / zelle));
                    if (ai == vi && aj == vj) continue;
                    ni = ai, nj = aj;
                    break;
                }
                if (ni < 0) break;
                ci = ni, cj = nj;
            }
            return z;
        };
        // Kanten zwischen Knoten (Grad != 2) nur einmal: Paar (Start, erster Schritt) merken
        std::set<std::pair<std::size_t, std::size_t>> erledigt;
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                if (!at(i, j) || grad(i, j) == 2) continue;
                for (int k = 0; k < 8; ++k) {
                    int ai = i + di[k], aj = j + dj[k];
                    if (!at(ai, aj) || besucht[idx(ai, aj)]) continue;
                    auto key = std::make_pair(std::min(idx(i, j), idx(ai, aj)), std::max(idx(i, j), idx(ai, aj)));
                    if (erledigt.count(key)) continue;
                    auto z = laufen(i, j, k);
                    if (z.size() >= 2) {
                        auto const &e = z.back();
                        int ei = int(std::lround((e[0] - x0) / zelle)), ej = int(std::lround((e[1] - y0) / zelle));
                        auto const &v = z[z.size() - 2];
                        int vi = int(std::lround((v[0] - x0) / zelle)), vj = int(std::lround((v[1] - y0) / zelle));
                        erledigt.insert(std::make_pair(std::min(idx(ei, ej), idx(vi, vj)), std::max(idx(ei, ej), idx(vi, vj))));
                    }
                    erledigt.insert(key);
                    if ((int)z.size() >= kuerzest) zuege.push_back(std::move(z));
                }
            }
        // reine Ringe (alle Grad 2, nicht besucht)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                if (!at(i, j) || besucht[idx(i, j)] || grad(i, j) != 2) continue;
                int k0 = -1;
                for (int k = 0; k < 8; ++k)
                    if (at(i + di[k], j + dj[k])) { k0 = k; break; }
                besucht[idx(i, j)] = 1;
                auto z = laufen(i, j, k0);
                if ((int)z.size() >= kuerzest) zuege.push_back(std::move(z));
            }
    }
    py::list aus;
    for (auto const &z : zuege) {
        py::array_t<double> a({(py::ssize_t)z.size(), (py::ssize_t)2});
        double *p = a.mutable_data();
        for (auto const &q : z) {
            *p++ = q[0];
            *p++ = q[1];
        }
        aus.append(a);
    }
    return aus;
}

// --------------------------------------------------------------------------------------------
#ifdef MIT_OCL
#include <opencamlib/adaptivewaterline.hpp>
#include <opencamlib/ballcutter.hpp>
#include <opencamlib/batchdropcutter.hpp>
#include <opencamlib/bullcutter.hpp>
#include <opencamlib/clpoint.hpp>
#include <opencamlib/compositecutter.hpp>
#include <opencamlib/conecutter.hpp>
#include <opencamlib/cylcutter.hpp>
#include <opencamlib/stlsurf.hpp>
#include <opencamlib/triangle.hpp>


// Fräser für OpenCAMLib aus dem Werkzeug (typ kugel | eckradius | v | konisch | schaft):
// V-Fräser mit abgeflachter Spitze («spitze» > 0) als Zylinder + Kegel, der konische Kugelfräser
// (Kugelspitze mit Radius «eckradius», Kegelwinkel «v_winkel» gesamt, grösster Ø «durchmesser»)
// als Kugel + Kegel, der Kegel berührt die Kugel tangential.
static std::unique_ptr<ocl::MillingCutter> fraeser_aus(py::dict werkzeug)
{
    auto zahl = [&](char const *k, double d) {
        return werkzeug.contains(k) && !werkzeug[k].is_none() ? werkzeug[k].cast<double>() : d;
    };
    std::string typ = werkzeug.contains("typ") ? werkzeug["typ"].cast<std::string>() : "schaft";
    double d = zahl("durchmesser", 6), laenge = std::max(zahl("laenge", 50), 1.0);
    double halb = std::clamp(zahl("v_winkel", 60), 1.0, 179.0) * M_PI / 360.0;
    if (typ == "kugel") return std::make_unique<ocl::BallCutter>(d, laenge);
    if (typ == "eckradius" && zahl("eckradius", 0) > 1e-6)
        return std::make_unique<ocl::BullCutter>(d, std::min(zahl("eckradius", 0), d / 2), laenge);
    if (typ == "konisch") {
        double rb = std::clamp(zahl("eckradius", 0.25), 0.01, d / 2);
        double dt = 2 * rb * std::cos(halb);          // Ø, wo der Kegel die Kugel berührt
        if (d <= dt + 1e-6) return std::make_unique<ocl::BallCutter>(2 * rb, laenge);
        return std::make_unique<ocl::BallConeCutter>(2 * rb, d, halb);
    }
    if (typ == "v") {
        double sp = zahl("spitze", 0);
        if (sp > 1e-6 && sp < d - 1e-6) return std::make_unique<ocl::CylConeCutter>(sp, d, halb);
        return std::make_unique<ocl::ConeCutter>(d, halb, laenge);
    }
    return std::make_unique<ocl::CylCutter>(d, laenge);
}

static py::array_t<double> abtasten(Arr dreiecke, py::dict werkzeug, py::array_t<double, py::array::c_style | py::array::forcecast> punkte,
                                    double z_min)
{
    if (dreiecke.ndim() != 3 || dreiecke.shape(1) != 3 || dreiecke.shape(2) != 3)
        throw std::runtime_error("dreiecke muss die Form (N, 3, 3) haben");
    if (punkte.ndim() != 2 || punkte.shape(1) != 2) throw std::runtime_error("punkte muss die Form (M, 2) haben");
    auto zahl = [&](char const *k, double d) {
        return werkzeug.contains(k) && !werkzeug[k].is_none() ? werkzeug[k].cast<double>() : d;
    };
    (void)zahl;
    std::unique_ptr<ocl::MillingCutter> fraeser = fraeser_aus(werkzeug);
    const float *t = dreiecke.data();
    const double *p = punkte.data();
    const py::ssize_t n = dreiecke.shape(0), m = punkte.shape(0);
    py::array_t<double> aus(m);
    double *z = aus.mutable_data();
    {
        py::gil_scoped_release frei;
        ocl::STLSurf flaeche;
        for (py::ssize_t k = 0; k < n; ++k) {
            const float *q = t + 9 * k;
            flaeche.addTriangle(ocl::Triangle(ocl::Point(q[0], q[1], q[2]), ocl::Point(q[3], q[4], q[5]),
                                              ocl::Point(q[6], q[7], q[8])));
        }
        ocl::BatchDropCutter bdc;
        bdc.setThreads(std::max(1u, std::thread::hardware_concurrency()));
        bdc.setSTL(flaeche);
        bdc.setCutter(fraeser.get());
        for (py::ssize_t k = 0; k < m; ++k) {
            ocl::CLPoint c(p[2 * k], p[2 * k + 1], z_min);
            bdc.appendPoint(c);
        }
        bdc.run();
        auto cl = bdc.getCLPoints();
        for (py::ssize_t k = 0; k < m && k < (py::ssize_t)cl.size(); ++k) z[k] = std::max(cl[k].z, z_min);
    }
    return aus;
}

// Abtaster: wie abtasten, aber der Suchbaum über den Dreiecken wird nur einmal gebaut und für
// jeden weiteren Aufruf wiederverwendet (Schlichten tastet in mehreren Runden ab).
class Abtaster {
public:
    Abtaster(Arr dreiecke, py::dict werkzeug)
    {
        if (dreiecke.ndim() != 3 || dreiecke.shape(1) != 3 || dreiecke.shape(2) != 3)
            throw std::runtime_error("dreiecke muss die Form (N, 3, 3) haben");
        auto zahl = [&](char const *k, double d) {
            return werkzeug.contains(k) && !werkzeug[k].is_none() ? werkzeug[k].cast<double>() : d;
        };
        (void)zahl;
        fraeser = fraeser_aus(werkzeug);
        const float *t = dreiecke.data();
        const py::ssize_t n = dreiecke.shape(0);
        py::gil_scoped_release frei;
        for (py::ssize_t k = 0; k < n; ++k) {
            const float *q = t + 9 * k;
            flaeche.addTriangle(ocl::Triangle(ocl::Point(q[0], q[1], q[2]), ocl::Point(q[3], q[4], q[5]),
                                              ocl::Point(q[6], q[7], q[8])));
        }
        bdc.setThreads(std::max(1u, std::thread::hardware_concurrency()));
        bdc.setSTL(flaeche);
        bdc.setCutter(fraeser.get());
    }

    py::array_t<double> abtasten(py::array_t<double, py::array::c_style | py::array::forcecast> punkte, double z_min)
    {
        if (punkte.ndim() != 2 || punkte.shape(1) != 2) throw std::runtime_error("punkte muss die Form (M, 2) haben");
        const double *p = punkte.data();
        const py::ssize_t m = punkte.shape(0);
        py::array_t<double> aus(m);
        double *z = aus.mutable_data();
        py::gil_scoped_release frei;
        bdc.clearCLPoints();
        for (py::ssize_t k = 0; k < m; ++k) {
            ocl::CLPoint c(p[2 * k], p[2 * k + 1], z_min);
            bdc.appendPoint(c);
        }
        bdc.run();
        auto cl = bdc.getCLPoints();
        for (py::ssize_t k = 0; k < m; ++k) z[k] = k < (py::ssize_t)cl.size() ? std::max(cl[k].z, z_min) : z_min;
        return aus;
    }

private:
    ocl::STLSurf flaeche;
    std::unique_ptr<ocl::MillingCutter> fraeser;
    ocl::BatchDropCutter bdc;
};

// Profil eines drehsymmetrischen Fräsers: Höhe h(d) der Schneide über der Spitze im Abstand d von
// der Achse (konvex, steigend; nur bis zum Radius r) und wo eine schiefe Ebene mit Steigung G ihn
// berührt (h'(d) = G).
struct Profil {
    std::string typ;
    double r = 3, er = 0, tanh = 1, sp = 0, rb = 0, dt = 0, ht = 0;
    explicit Profil(py::dict w)
    {
        auto zahl = [&](char const *k, double d) {
            return w.contains(k) && !w[k].is_none() ? w[k].cast<double>() : d;
        };
        typ = w.contains("typ") ? w["typ"].cast<std::string>() : "schaft";
        r = std::max(zahl("durchmesser", 6) / 2, 1e-4);
        if (typ == "kugel") er = r;
        else if (typ == "eckradius") er = std::clamp(zahl("eckradius", 0), 0.0, r);
        double halb = std::clamp(zahl("v_winkel", 60), 1.0, 179.0) * M_PI / 360.0;
        tanh = std::tan(halb);
        sp = std::clamp(zahl("spitze", 0) / 2, 0.0, r);
        rb = std::clamp(zahl("eckradius", 0.25), 0.01, r);
        dt = std::min(r, rb * std::cos(halb));
        ht = rb - rb * std::sin(halb);
    }
    double operator()(double d) const
    {
        if (typ == "v") return std::max(0.0, d - sp) / tanh;
        if (typ == "konisch") {
            if (d <= dt) return rb - std::sqrt(std::max(0.0, rb * rb - d * d));
            return ht + (d - dt) / tanh;
        }
        if (er > 0 && d > r - er) {
            double q = std::min(d - (r - er), er);
            return er - std::sqrt(std::max(0.0, er * er - q * q));
        }
        return 0.0;
    }
    /// Abstand, in dem eine Ebene mit Steigung G (≥ 0) den Fräser berührt
    double beruehrt(double G) const
    {
        double k = G / std::sqrt(1 + G * G);
        if (typ == "v") return G * tanh < 1 ? sp : r;
        if (typ == "konisch") return G * tanh < 1 ? std::min(dt, rb * k) : r;
        if (er > 0) return std::min(r, (r - er) + er * k);
        return G > 0 ? r : 0.0;
    }
};

// Schneller Abtaster (Drop-Cutter, genau): Kugel- und Schaftfräser direkt gegen die Dreiecke –
// Fläche, Kanten und Ecken analytisch; Eckradius-, V- und konische Fräser über ihr Profil (Fläche:
// Berührabstand aus der Steigung, Kanten: Maximum einer konkaven Funktion entlang der Kante, golden
// geteilt). Die Dreiecke liegen in einem Gitter (Draufsicht), je Punkt werden nur die unter dem Fräser
// geprüft. Gleiches Ergebnis wie OpenCAMLib, aber ohne dessen Verwaltungsaufwand.
class SchnellAbtaster {
public:
    SchnellAbtaster(Arr dreiecke, py::dict werkzeug) : profil(werkzeug)
    {
        if (dreiecke.ndim() != 3 || dreiecke.shape(1) != 3 || dreiecke.shape(2) != 3)
            throw std::runtime_error("dreiecke muss die Form (N, 3, 3) haben");
        auto zahl = [&](char const *k, double d) {
            return werkzeug.contains(k) && !werkzeug[k].is_none() ? werkzeug[k].cast<double>() : d;
        };
        std::string typ = werkzeug.contains("typ") ? werkzeug["typ"].cast<std::string>() : "schaft";
        r = std::max(zahl("durchmesser", 6) / 2, 1e-4);
        double er = zahl("eckradius", 0);
        eigen = true;
        if (typ == "kugel" || (typ == "eckradius" && er >= r - 1e-9)) kugel = true;
        else if (typ == "schaft" || (typ == "eckradius" && er <= 1e-9)) kugel = false;
        else allgemein = true;
        if (typ == "bohrer") eigen = false;
        const py::ssize_t n = dreiecke.shape(0);
        const float *t = dreiecke.data();
        tri.assign(t, t + 9 * n);
        if (!eigen) {
            ocl = std::make_unique<Abtaster>(dreiecke, werkzeug);
            return;
        }
        // Gitter: Zellen etwa so gross wie der Fräser
        double lo[2] = {1e300, 1e300}, hi[2] = {-1e300, -1e300};
        for (py::ssize_t k = 0; k < 3 * n; ++k) {
            lo[0] = std::min(lo[0], double(t[3 * k]));
            hi[0] = std::max(hi[0], double(t[3 * k]));
            lo[1] = std::min(lo[1], double(t[3 * k + 1]));
            hi[1] = std::max(hi[1], double(t[3 * k + 1]));
        }
        if (n == 0) return;
        g = std::max(r, std::sqrt((hi[0] - lo[0] + 1) * (hi[1] - lo[1] + 1) / 4e6));
        gx0 = lo[0];
        gy0 = lo[1];
        gnx = int((hi[0] - lo[0]) / g) + 1;
        gny = int((hi[1] - lo[1]) / g) + 1;
        std::vector<int> anz(std::size_t(gnx) * gny + 1, 0);
        auto bereich = [&](py::ssize_t k, int &i0, int &i1, int &j0, int &j1) {
            const float *q = t + 9 * k;
            i0 = std::clamp(int((std::min({q[0], q[3], q[6]}) - gx0) / g), 0, gnx - 1);
            i1 = std::clamp(int((std::max({q[0], q[3], q[6]}) - gx0) / g), 0, gnx - 1);
            j0 = std::clamp(int((std::min({q[1], q[4], q[7]}) - gy0) / g), 0, gny - 1);
            j1 = std::clamp(int((std::max({q[1], q[4], q[7]}) - gy0) / g), 0, gny - 1);
        };
        for (py::ssize_t k = 0; k < n; ++k) {
            int i0, i1, j0, j1;
            bereich(k, i0, i1, j0, j1);
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i) ++anz[std::size_t(j) * gnx + i + 1];
        }
        for (std::size_t q = 1; q < anz.size(); ++q) anz[q] += anz[q - 1];
        start = anz;
        liste.resize(anz.back());
        for (py::ssize_t k = 0; k < n; ++k) {
            int i0, i1, j0, j1;
            bereich(k, i0, i1, j0, j1);
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i) liste[anz[std::size_t(j) * gnx + i]++] = int(k);
        }
    }

    /// tiefste Spitze bei (px, py), nie unter z_min. stempel: je Thread, gegen doppelte Dreiecke
    double punkt(double px, double py, double z_min, std::vector<uint32_t> &stempel, uint32_t marke) const
    {
        double best = -1e300;
        int i0 = std::max(0, int(std::floor((px - r - gx0) / g))), i1 = std::min(gnx - 1, int((px + r - gx0) / g));
        int j0 = std::max(0, int(std::floor((py - r - gy0) / g))), j1 = std::min(gny - 1, int((py + r - gy0) / g));
        double r2 = r * r;
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) {
                std::size_t zelle = std::size_t(j) * gnx + i;
                for (int a = start[zelle]; a < start[zelle + 1]; ++a) {
                    int k = liste[a];
                    if (stempel[k] == marke) continue;
                    stempel[k] = marke;
                    const float *q = &tri[9 * std::size_t(k)];
                    double P[3][3] = {{q[0], q[1], q[2]}, {q[3], q[4], q[5]}, {q[6], q[7], q[8]}};
                    // schnell aussortieren: Dreieck ganz ausserhalb des Kreises (Rechteck)
                    if (std::min({P[0][0], P[1][0], P[2][0]}) > px + r || std::max({P[0][0], P[1][0], P[2][0]}) < px - r ||
                        std::min({P[0][1], P[1][1], P[2][1]}) > py + r || std::max({P[0][1], P[1][1], P[2][1]}) < py - r)
                        continue;
                    if (allgemein) {
                        best = std::max(best, allgemein_dreieck(P, px, py));
                        continue;
                    }
                    // Fläche
                    double ux = P[1][0] - P[0][0], uy = P[1][1] - P[0][1], uz = P[1][2] - P[0][2];
                    double vx = P[2][0] - P[0][0], vy = P[2][1] - P[0][1], vz = P[2][2] - P[0][2];
                    double nxx = uy * vz - uz * vy, nyy = uz * vx - ux * vz, nzz = ux * vy - uy * vx;
                    double nl = std::sqrt(nxx * nxx + nyy * nyy + nzz * nzz);
                    // (fast) senkrechte Flächen: die Kanten oben bestimmen die Höhe
                    if (nl > 1e-15 && std::abs(nzz) > 1e-6 * nl) {
                        if (nzz < 0) nxx = -nxx, nyy = -nyy, nzz = -nzz;
                        nxx /= nl, nyy /= nl, nzz /= nl;
                        double cx_, cy_;           // Berührpunkt in der Draufsicht
                        if (kugel) {
                            cx_ = px - r * nxx;
                            cy_ = py - r * nyy;
                        } else {
                            double gl = std::hypot(nxx, nyy);
                            cx_ = gl > 1e-12 ? px - r * nxx / gl : px;   // bergauf am Rand des Fräsers
                            cy_ = gl > 1e-12 ? py - r * nyy / gl : py;
                        }
                        double den = (P[1][1] - P[2][1]) * (P[0][0] - P[2][0]) + (P[2][0] - P[1][0]) * (P[0][1] - P[2][1]);
                        if (std::abs(den) > 1e-18) {
                            double l0 = ((P[1][1] - P[2][1]) * (cx_ - P[2][0]) + (P[2][0] - P[1][0]) * (cy_ - P[2][1])) / den;
                            double l1 = ((P[2][1] - P[0][1]) * (cx_ - P[2][0]) + (P[0][0] - P[2][0]) * (cy_ - P[2][1])) / den;
                            double l2 = 1 - l0 - l1;
                            if (l0 >= -1e-12 && l1 >= -1e-12 && l2 >= -1e-12) {
                                double zc = l0 * P[0][2] + l1 * P[1][2] + l2 * P[2][2];
                                double spitze = kugel ? zc + r * nzz - r : zc;
                                best = std::max(best, spitze);
                            }
                        }
                    }
                    // Kanten und Ecken
                    for (int e = 0; e < 3; ++e) {
                        double const *A = P[e], *B = P[(e + 1) % 3];
                        double dx = A[0] - px, dy = A[1] - py;
                        double d2 = dx * dx + dy * dy;
                        if (d2 <= r2) best = std::max(best, kugel ? A[2] + std::sqrt(r2 - d2) - r : A[2]);
                        double ex = B[0] - A[0], ey = B[1] - A[1], ez = B[2] - A[2];
                        double l2xy = ex * ex + ey * ey;
                        if (l2xy < 1e-24) continue;    // senkrechte Kante: die Ecken reichen
                        if (kugel) {
                            // Abstand der Fräserachse zur Kante (Draufsicht) → Kreis r' in der senkrechten
                            // Ebene der Kante, Gerade mit Steigung m darin
                            double lxy = std::sqrt(l2xy);
                            double t0 = ((px - A[0]) * ex + (py - A[1]) * ey) / l2xy;     // Fusspunkt (0..1)
                            double qx = A[0] + t0 * ex - px, qy = A[1] + t0 * ey - py;
                            double dd = qx * qx + qy * qy;
                            if (dd >= r2) continue;
                            double rs = std::sqrt(r2 - dd);
                            double m = ez / lxy;                  // Steigung entlang der Kante
                            double w = std::sqrt(1 + m * m);
                            double s = rs * m / w;                // Berührpunkt: Abstand (mm) vom Fusspunkt
                            double tt = t0 + s / lxy;
                            if (tt < 0 || tt > 1) continue;       // ausserhalb: Ecken übernehmen
                            double zline = A[2] + t0 * ez;        // Höhe der Kante am Fusspunkt
                            double mitte = zline + rs * w;        // Mittelpunkt der Kugel
                            best = std::max(best, mitte - r);
                        } else {
                            // Kante im Kreis (Draufsicht): höchster Punkt an einem Ende des Stücks
                            double b_ = 2 * ((A[0] - px) * ex + (A[1] - py) * ey);
                            double c_ = d2 - r2;
                            double disk = b_ * b_ - 4 * l2xy * c_;
                            if (disk < 0) continue;
                            double sq = std::sqrt(disk);
                            double ta = std::max(0.0, (-b_ - sq) / (2 * l2xy)), tb = std::min(1.0, (-b_ + sq) / (2 * l2xy));
                            if (ta > tb) continue;
                            best = std::max(best, A[2] + std::max(ta, tb) * ez);
                            best = std::max(best, A[2] + std::min(ta, tb) * ez);
                        }
                    }
                }
            }
        return std::max(best, z_min);
    }

    /// beliebiges (konvexes) Profil gegen ein Dreieck: höchste Spitze
    double allgemein_dreieck(double const P[3][3], double px, double py) const
    {
        double best = -1e300, r = profil.r, r2 = r * r;
        double ux = P[1][0] - P[0][0], uy = P[1][1] - P[0][1], uz = P[1][2] - P[0][2];
        double vx = P[2][0] - P[0][0], vy = P[2][1] - P[0][1], vz = P[2][2] - P[0][2];
        double nzz = ux * vy - uy * vx;
        if (std::abs(nzz) > 1e-12) {
            // Ebene z = P0z + a (x − P0x) + b (y − P0y)
            double a = -(uy * vz - uz * vy) / nzz, b = -(uz * vx - ux * vz) / nzz;
            double G = std::hypot(a, b);
            double d = profil.beruehrt(G);
            double cx_ = px + (G > 1e-15 ? d * a / G : 0.0), cy_ = py + (G > 1e-15 ? d * b / G : 0.0);
            double den = (P[1][1] - P[2][1]) * (P[0][0] - P[2][0]) + (P[2][0] - P[1][0]) * (P[0][1] - P[2][1]);
            double l0 = ((P[1][1] - P[2][1]) * (cx_ - P[2][0]) + (P[2][0] - P[1][0]) * (cy_ - P[2][1])) / den;
            double l1 = ((P[2][1] - P[0][1]) * (cx_ - P[2][0]) + (P[0][0] - P[2][0]) * (cy_ - P[2][1])) / den;
            if (l0 >= -1e-12 && l1 >= -1e-12 && 1 - l0 - l1 >= -1e-12)
                best = P[0][2] + a * (cx_ - P[0][0]) + b * (cy_ - P[0][1]) - profil(d);
        }
        for (int e = 0; e < 3; ++e) {
            double const *A = P[e], *B = P[(e + 1) % 3];
            double ex = B[0] - A[0], ey = B[1] - A[1], ez = B[2] - A[2];
            double dx = A[0] - px, dy = A[1] - py;
            double l2xy = ex * ex + ey * ey;
            auto f = [&](double t) {
                double qx = dx + t * ex, qy = dy + t * ey;
                return A[2] + t * ez - profil(std::sqrt(qx * qx + qy * qy));
            };
            double ta = 0, tb = 1;
            if (l2xy < 1e-24) {                      // senkrechte Kante: die obere Ecke
                if (dx * dx + dy * dy <= r2) best = std::max(best, std::max(A[2], B[2]) - profil(std::sqrt(dx * dx + dy * dy)));
                continue;
            }
            double b_ = 2 * (dx * ex + dy * ey), c_ = dx * dx + dy * dy - r2;
            double disk = b_ * b_ - 4 * l2xy * c_;
            if (disk < 0) continue;
            double sq = std::sqrt(disk);
            ta = std::max(0.0, (-b_ - sq) / (2 * l2xy));
            tb = std::min(1.0, (-b_ + sq) / (2 * l2xy));
            if (ta > tb) continue;
            // konkav: goldener Schnitt
            const double phi = 0.6180339887498949;
            double lo = ta, hi = tb, m1 = hi - phi * (hi - lo), m2 = lo + phi * (hi - lo);
            double f1 = f(m1), f2 = f(m2);
            for (int it = 0; it < 60 && hi - lo > 1e-10; ++it) {
                if (f1 < f2) {
                    lo = m1; m1 = m2; f1 = f2; m2 = lo + phi * (hi - lo); f2 = f(m2);
                } else {
                    hi = m2; m2 = m1; f2 = f1; m1 = hi - phi * (hi - lo); f1 = f(m1);
                }
            }
            best = std::max({best, f1, f2, f(ta), f(tb)});
        }
        return best;
    }

    py::array_t<double> abtasten(py::array_t<double, py::array::c_style | py::array::forcecast> punkte, double z_min)
    {
        if (punkte.ndim() != 2 || punkte.shape(1) != 2) throw std::runtime_error("punkte muss die Form (M, 2) haben");
        if (!eigen) return ocl->abtasten(punkte, z_min);
        const py::ssize_t m = punkte.shape(0);
        const double *p = punkte.data();
        py::array_t<double> aus(m);
        double *z = aus.mutable_data();
        if (tri.empty()) {
            for (py::ssize_t i = 0; i < m; ++i) z[i] = z_min;
            return aus;
        }
        py::gil_scoped_release frei;
        unsigned nt = std::max(1u, std::thread::hardware_concurrency());
        std::vector<std::thread> th;
        for (unsigned k = 0; k < nt; ++k)
            th.emplace_back([&, k] {
                std::vector<uint32_t> stempel(tri.size() / 9, 0);
                uint32_t marke = 0;
                // Stücke von 256 Punkten je Thread (Nachbarpunkte teilen die Zwischenspeicher)
                for (py::ssize_t b = py::ssize_t(k) * 256; b < m; b += py::ssize_t(nt) * 256)
                    for (py::ssize_t i = b; i < std::min(m, b + 256); ++i)
                        z[i] = punkt(p[2 * i], p[2 * i + 1], z_min, stempel, ++marke);
            });
        for (auto &t : th) t.join();
        return aus;
    }

    bool genau_eigen() const { return eigen; }

private:
    Profil profil;
    double r = 3;
    bool kugel = true, eigen = false, allgemein = false;
    std::vector<float> tri;
    double g = 1, gx0 = 0, gy0 = 0;
    int gnx = 0, gny = 0;
    std::vector<int> start, liste;
    std::unique_ptr<Abtaster> ocl;
};

// Wasserlinie: Höhenlinien der Fräserspitze mit OpenCAMLib (AdaptiveWaterline – Fasern in X und Y,
// an Kanten verdichtet). Fläche und Fräser einmal aufgebaut, dann je Höhe ein Aufruf.
//   Wasserlinie(dreiecke, werkzeug).linien(z, abstand, feinst) -> [ (M, 2) geschlossene Züge ]
class Wasserlinie {
public:
    Wasserlinie(Arr dreiecke, py::dict werkzeug)
    {
        if (dreiecke.ndim() != 3 || dreiecke.shape(1) != 3 || dreiecke.shape(2) != 3)
            throw std::runtime_error("dreiecke muss die Form (N, 3, 3) haben");
        fraeser = fraeser_aus(werkzeug);
        const float *t = dreiecke.data();
        for (py::ssize_t k = 0; k < dreiecke.shape(0); ++k) {
            const float *q = t + 9 * k;
            flaeche.addTriangle(ocl::Triangle(ocl::Point(q[0], q[1], q[2]), ocl::Point(q[3], q[4], q[5]),
                                              ocl::Point(q[6], q[7], q[8])));
        }
    }

    py::list linien(double z, double abstand, double feinst)
    {
        std::vector<std::vector<ocl::Point>> loops;
        {
            py::gil_scoped_release frei;
            ocl::AdaptiveWaterline w;
            w.setThreads(std::max(1u, std::thread::hardware_concurrency()));
            w.setSTL(flaeche);
            w.setCutter(fraeser.get());
            w.setZ(z);
            w.setSampling(std::max(abstand, 0.01));
            w.setMinSampling(std::max(feinst, 0.001));
            w.run();
            loops = w.getLoops();
        }
        py::list aus;
        for (auto const &l : loops) {
            if (l.size() < 3) continue;
            py::array_t<double> a({(py::ssize_t)l.size(), (py::ssize_t)2});
            double *p = a.mutable_data();
            for (auto const &q : l) {
                *p++ = q.x;
                *p++ = q.y;
            }
            aus.append(a);
        }
        return aus;
    }

private:
    ocl::STLSurf flaeche;
    std::unique_ptr<ocl::MillingCutter> fraeser;
};
#endif

// --------------------------------------------------------------------------------------------
#ifdef MIT_OCCT
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GProp_GProps.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Reader.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

static const char* flaechenart(GeomAbs_SurfaceType t) {
    switch (t) {
        case GeomAbs_Plane: return "ebene";
        case GeomAbs_Cylinder: return "zylinder";
        case GeomAbs_Cone: return "kegel";
        case GeomAbs_Sphere: return "kugel";
        case GeomAbs_Torus: return "torus";
        case GeomAbs_BezierSurface: return "bezier";
        case GeomAbs_BSplineSurface: return "bspline";
        case GeomAbs_SurfaceOfRevolution: return "rotation";
        case GeomAbs_SurfaceOfExtrusion: return "extrusion";
        case GeomAbs_OffsetSurface: return "versatz";
        default: return "andere";
    }
}

static py::dict step_lesen(const std::string& pfad, double abweichung) {
    STEPControl_Reader leser;
    if (leser.ReadFile(pfad.c_str()) != IFSelect_RetDone)
        throw std::runtime_error("STEP-Datei kann nicht gelesen werden");
    if (leser.TransferRoots() == 0) throw std::runtime_error("STEP-Datei enthält keine Körper");
    TopoDS_Shape form = leser.OneShape();
    BRepMesh_IncrementalMesh netz(form, abweichung, false, 0.2, true);

    TopTools_IndexedMapOfShape flaechen;
    TopExp::MapShapes(form, TopAbs_FACE, flaechen);
    std::vector<float> tri;
    py::list fl_liste;
    std::vector<std::string> art(flaechen.Extent() + 1);
    for (int i = 1; i <= flaechen.Extent(); ++i) {
        TopoDS_Face f = TopoDS::Face(flaechen(i));
        art[i] = flaechenart(BRepAdaptor_Surface(f).GetType());
        GProp_GProps g;
        BRepGProp::SurfaceProperties(f, g);
        py::dict e;
        e["art"] = art[i];
        e["flaeche"] = g.Mass();
        // Lage der Fläche (für Bohrungen und Taschen): Begrenzung, Normale in der Mitte (zeigt aus
        // dem Körper heraus), beim Zylinder Achse und Radius
        {
            Bnd_Box bb;
            BRepBndLib::Add(f, bb);
            if (!bb.IsVoid()) {
                double x0, y0, z0, x1, y1, z1;
                bb.Get(x0, y0, z0, x1, y1, z1);
                e["box"] = std::vector<double>{x0, y0, z0, x1, y1, z1};
            }
            BRepAdaptor_Surface fl(f);
            double u = (fl.FirstUParameter() + fl.LastUParameter()) / 2;
            double v = (fl.FirstVParameter() + fl.LastVParameter()) / 2;
            gp_Pnt P;
            gp_Vec du, dv;
            try {
                fl.D1(u, v, P, du, dv);
                gp_Vec n = du.Crossed(dv);
                if (n.Magnitude() > 1e-12) {
                    n.Normalize();
                    if (f.Orientation() == TopAbs_REVERSED) n.Reverse();
                    e["punkt"] = std::vector<double>{P.X(), P.Y(), P.Z()};
                    e["normale"] = std::vector<double>{n.X(), n.Y(), n.Z()};
                }
            } catch (...) {
            }
            if (fl.GetType() == GeomAbs_Cylinder) {
                gp_Cylinder c = fl.Cylinder();
                gp_Pnt o = c.Location();
                gp_Dir a = c.Axis().Direction();
                e["achse_punkt"] = std::vector<double>{o.X(), o.Y(), o.Z()};
                e["achse"] = std::vector<double>{a.X(), a.Y(), a.Z()};
                e["radius"] = c.Radius();
            }
        }
        std::size_t tri_von = tri.size() / 9;
        TopLoc_Location ort;
        Handle(Poly_Triangulation) T = BRep_Tool::Triangulation(f, ort);
        if (T.IsNull()) {
            e["dreiecke"] = std::vector<std::size_t>{tri_von, tri_von};
            fl_liste.append(e);
            continue;
        }
        const bool umgekehrt = f.Orientation() == TopAbs_REVERSED;
        for (int k = 1; k <= T->NbTriangles(); ++k) {
            int a, b, c;
            T->Triangle(k).Get(a, b, c);
            if (umgekehrt) std::swap(b, c);
            for (int q : {a, b, c}) {
                gp_Pnt p = T->Node(q).Transformed(ort);
                tri.push_back((float)p.X()); tri.push_back((float)p.Y()); tri.push_back((float)p.Z());
            }
        }
        e["dreiecke"] = std::vector<std::size_t>{tri_von, tri.size() / 9};
        fl_liste.append(e);
    }
    py::array_t<float> dreiecke({(py::ssize_t)(tri.size() / 9), (py::ssize_t)3, (py::ssize_t)3});
    std::copy(tri.begin(), tri.end(), dreiecke.mutable_data());

    TopTools_IndexedDataMapOfShapeListOfShape kf;
    TopExp::MapShapesAndAncestors(form, TopAbs_EDGE, TopAbs_FACE, kf);
    py::list kanten;
    for (int i = 1; i <= kf.Extent(); ++i) {
        const TopoDS_Edge& kante = TopoDS::Edge(kf.FindKey(i));
        if (BRep_Tool::Degenerated(kante)) continue;
        py::list arten;
        for (const auto& f : kf.FindFromIndex(i)) arten.append(art[flaechen.FindIndex(f)]);
        BRepAdaptor_Curve kurve(kante);
        GCPnts_QuasiUniformDeflection teil(kurve, abweichung / 2);
        if (!teil.IsDone() || teil.NbPoints() < 2) continue;
        py::array_t<double> pkt({(py::ssize_t)teil.NbPoints(), (py::ssize_t)3});
        double* p = pkt.mutable_data();
        for (int k = 1; k <= teil.NbPoints(); ++k) {
            gp_Pnt q = teil.Value(k);
            *p++ = q.X(); *p++ = q.Y(); *p++ = q.Z();
        }
        py::dict e;
        e["punkte"] = pkt;
        e["flaechen"] = arten;
        kanten.append(e);
    }
    py::dict erg;
    erg["dreiecke"] = dreiecke;
    erg["flaechen"] = fl_liste;
    erg["kanten"] = kanten;
    return erg;
}
#endif


// --------------------------------------------------------------------------------------------
// Abtrag auf einem Höhenraster: für jede Bewegung die Rasterpunkte im Umkreis des Fräsers;
// Höhe = Spitze an der nächsten Stelle der Bahn + Profil des Fräsers in diesem Abstand.
static void abtragen(py::array_t<float, py::array::c_style> hoehe, double x0, double y0, double zelle,
                     py::array_t<double, py::array::c_style | py::array::forcecast> bewegungen, py::dict werkzeug,
                     std::vector<double> start, double boden)
{
    if (hoehe.ndim() != 2) throw std::runtime_error("hoehe muss zweidimensional sein");
    if (bewegungen.ndim() != 2 || bewegungen.shape(1) != 3) throw std::runtime_error("bewegungen muss (N, 3) sein");
    if (start.size() != 3) throw std::runtime_error("start muss (x, y, z) sein");
    auto zahl = [&](char const *k, double d) {
        return werkzeug.contains(k) && !werkzeug[k].is_none() ? werkzeug[k].cast<double>() : d;
    };
    std::string typ = werkzeug.contains("typ") ? werkzeug["typ"].cast<std::string>() : "schaft";
    const double r = std::max(zahl("durchmesser", 6) / 2, 1e-3);
    double er = 0;
    if (typ == "kugel") er = r;
    else if (typ == "eckradius") er = std::min(std::max(zahl("eckradius", 0), 0.0), r);
    const bool v = typ == "v";
    const bool kon = typ == "konisch";
    const double halb = std::max(1.0, std::min(179.0, zahl("v_winkel", 60))) * M_PI / 360.0;
    const double tanh = std::tan(halb);
    const double sp = std::max(zahl("spitze", 0), 0.0) / 2;
    // konischer Kugelfräser: Kugel (Radius rb) bis zum Berührpunkt dt, dann Kegel
    const double rb = std::clamp(zahl("eckradius", 0.25), 0.01, r);
    const double dt = rb * std::cos(halb), ht = rb - rb * std::sin(halb);
    auto profil = [&](double d) {
        if (v) return std::max(0.0, d - sp) / tanh;
        if (kon) {
            if (d <= dt) return rb - std::sqrt(std::max(0.0, rb * rb - d * d));
            return ht + (d - dt) / tanh;
        }
        if (er > 0 && d > r - er) {
            double q = std::min(d - (r - er), er);
            return er - std::sqrt(std::max(0.0, er * er - q * q));
        }
        return 0.0;
    };
    const py::ssize_t ny = hoehe.shape(0), nx = hoehe.shape(1), n = bewegungen.shape(0);
    float *H = hoehe.mutable_data();
    const double *b = bewegungen.data();
    py::gil_scoped_release frei;
    double ax = start[0], ay = start[1], az = start[2];
    for (py::ssize_t k = 0; k < n; ++k) {
        double bx = b[3 * k], by = b[3 * k + 1], bz = b[3 * k + 2];
        if (std::min(az, bz) < 0) {
            int i0 = std::max<py::ssize_t>(0, (py::ssize_t)std::floor((std::min(ax, bx) - r - x0) / zelle));
            int i1 = std::min<py::ssize_t>(nx - 1, (py::ssize_t)std::ceil((std::max(ax, bx) + r - x0) / zelle));
            int j0 = std::max<py::ssize_t>(0, (py::ssize_t)std::floor((std::min(ay, by) - r - y0) / zelle));
            int j1 = std::min<py::ssize_t>(ny - 1, (py::ssize_t)std::ceil((std::max(ay, by) + r - y0) / zelle));
            double dx = bx - ax, dy = by - ay, L2 = dx * dx + dy * dy;
            bool senkrecht = L2 < 1e-12;
            double zmin = std::min(az, bz);
            // Schräge Bewegung: die tiefste Stelle des Fräsers über einer Zelle liegt nicht beim nächsten
            // Punkt der Strecke, sondern Richtung Abstieg. Kandidaten (genau für Schaft und Kugel, nahe
            // für Eckradius und V): nächster Punkt, Enden, Rand des flachen Teils, Minimum der Kugel/V.
            const double L = std::sqrt(L2), m = senkrecht ? 0.0 : (bz - az) / L;
            const double ri = r - er;                          // flacher Teil der Stirn
            const double kv = v ? 1.0 / tanh : 0.0;
            for (int j = j0; j <= j1; ++j) {
                double py_ = y0 + j * zelle;
                float *zeile = H + (py::ssize_t)j * nx;
                for (int i = i0; i <= i1; ++i) {
                    double px = x0 + i * zelle;
                    double best = 1e300;
                    auto wert = [&](double t) {
                        t = std::clamp(t, 0.0, 1.0);
                        double qx = px - (ax + t * dx), qy = py_ - (ay + t * dy);
                        double d2 = qx * qx + qy * qy;
                        if (d2 > r * r + 1e-9) return;           // Rand: Rundung der Kandidaten erlauben
                        double z = senkrecht ? zmin : az + t * (bz - az);
                        best = std::min(best, z + profil(std::min(r, std::sqrt(d2))));
                    };
                    if (senkrecht) {
                        wert(0.0);
                    } else {
                        double tp = ((px - ax) * dx + (py_ - ay) * dy) / L2;
                        double qx = px - (ax + tp * dx), qy = py_ - (ay + tp * dy);
                        double dp2 = qx * qx + qy * qy;
                        if (dp2 > r * r) continue;
                        wert(tp);
                        wert(0.0);
                        wert(1.0);
                        if (std::abs(m) > 1e-12) {
                            double s = m < 0 ? 1.0 : -1.0;     // Richtung Abstieg
                            for (double rr : {r, ri})
                                if (rr > 0 && dp2 <= rr * rr) wert(tp + s * std::sqrt(rr * rr - dp2) / L);
                            if (er > 0) {                       // Kugelteil: m·u − √(R² − u²) minimal
                                double ddp = std::sqrt(dp2);
                                double R2 = er * er - std::max(0.0, ddp - ri) * std::max(0.0, ddp - ri);
                                if (R2 > 0) wert(tp + (-m * std::sqrt(R2) / std::sqrt(1 + m * m)) / L);
                                if (ri > 0) {                   // Eckradius: Torus – dazu Stichproben
                                    double h = std::sqrt(std::max(0.0, r * r - dp2)) / L;
                                    for (int q = 1; q < 12; ++q) wert(tp + s * h * q / 12.0);
                                }
                            }
                            if (v && kv > std::abs(m)) {        // V: m·u + k·√(dp² + u²) minimal
                                double ddp = std::sqrt(dp2);
                                wert(tp + (-m * ddp / std::sqrt(kv * kv - m * m)) / L);
                            }
                        }
                    }
                    if (best > 1e299) continue;
                    double h = std::max(best, boden);
                    if (h < zeile[i]) zeile[i] = (float)h;
                }
            }
        }
        ax = bx;
        ay = by;
        az = bz;
    }
}

PYBIND11_MODULE(hirch_relief, m) {
    m.doc() = "Rechenkern der 3D-Fräsung (Höhenraster, STEP)";
    m.def("hoehenkarten", &hoehenkarten, py::arg("dreiecke"), py::arg("x0"), py::arg("y0"),
          py::arg("zelle"), py::arg("nx"), py::arg("ny"));
    m.def("abtragen", &abtragen, py::arg("hoehe"), py::arg("x0"), py::arg("y0"), py::arg("zelle"),
          py::arg("bewegungen"), py::arg("werkzeug"), py::arg("start"), py::arg("boden"));
    m.def("mittellinien", &mittellinien, py::arg("maske"), py::arg("x0"), py::arg("y0"), py::arg("zelle"),
          py::arg("kuerzest") = 3);
    m.def("konturen", &konturen, py::arg("raster"), py::arg("schwelle"), py::arg("x0"), py::arg("y0"),
          py::arg("zelle"));
#ifdef MIT_OCL
    m.def("abtasten", &abtasten, py::arg("dreiecke"), py::arg("werkzeug"), py::arg("punkte"), py::arg("z_min"));
    py::class_<Abtaster>(m, "Abtaster")
        .def(py::init<Arr, py::dict>(), py::arg("dreiecke"), py::arg("werkzeug"))
        .def("abtasten", &Abtaster::abtasten, py::arg("punkte"), py::arg("z_min"));
    py::class_<SchnellAbtaster>(m, "SchnellAbtaster")
        .def(py::init<Arr, py::dict>(), py::arg("dreiecke"), py::arg("werkzeug"))
        .def("abtasten", &SchnellAbtaster::abtasten, py::arg("punkte"), py::arg("z_min"))
        .def_property_readonly("eigen", &SchnellAbtaster::genau_eigen);
    py::class_<Wasserlinie>(m, "Wasserlinie")
        .def(py::init<Arr, py::dict>(), py::arg("dreiecke"), py::arg("werkzeug"))
        .def("linien", &Wasserlinie::linien, py::arg("z"), py::arg("abstand"), py::arg("feinst"));
    m.def("mit_ocl", []() { return true; });
#else
    m.def("mit_ocl", []() { return false; });
#endif
#ifdef MIT_OCCT
    m.def("step_lesen", &step_lesen, py::arg("pfad"), py::arg("abweichung") = 0.02);
    m.def("mit_step", []() { return true; });
#else
    m.def("mit_step", []() { return false; });
#endif
}
