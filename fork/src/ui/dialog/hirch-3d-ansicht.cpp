// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: 3D – Ansicht: Zeichnen, Kamera, Puffer, Regelung (aus hirch-3d.cpp, siehe hirch-3d-intern.h)
 */
#include "ui/dialog/hirch-3d-intern.h"

namespace Inkscape::UI::Dialog {
namespace hirch3d {

namespace {

char const *const VS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aN;
layout(location = 2) in vec3 aFarbe;
uniform mat4 uPV;
out vec3 vN;
out float vZ;
out vec3 vFarbe;
void main() {
    vN = aN;
    vZ = aPos.z;
    vFarbe = aFarbe;
    gl_Position = uPV * vec4(aPos, 1.0);
}
)";

char const *const FS = R"(#version 330 core
in vec3 vN;
in float vZ;
in vec3 vFarbe;
uniform vec3 uLicht;
uniform vec4 uFarbe;
uniform float uDicke;
uniform int uModell;
uniform int uFlach;
uniform int uFarbig;       // 1: Farbe je Ecke (gefrästes Teil, «Färben nach»)
out vec4 farbe;
void main() {
    vec3 c = uFarbig == 1 ? vFarbe : uFarbe.rgb;
    if (uModell == 1 && (vZ > 0.02 || vZ < -uDicke - 0.02)) c = vec3(0.88, 0.20, 0.17);
    float l = 1.0;
    if (uFlach == 0) {
        vec3 n = normalize(vN);
        if (!gl_FrontFacing) n = -n;
        l = 0.22 + 0.85 * max(dot(n, uLicht), 0.0) + 0.12 * max(dot(n, -uLicht), 0.0);
    }
    farbe = vec4(c * l, uFarbe.a);
}
)";

} // namespace

Ansicht::Ansicht(std::shared_ptr<Szene3D> s)
    : sz(std::move(s)), ecken(sz->ecken), block(sz->block), linien(sz->linien), steg_ecken(sz->steg_ecken),
      marke_ecken(sz->marke_ecken), erg_ecken(sz->erg_ecken), index(sz->index), hell_index(sz->hell_index),
      hell_ringe(sz->hell_ringe), erg_index(sz->erg_index), zeige_ergebnis(sz->zeige_ergebnis),
      hat_modell(sz->hat_modell), B(sz->B), H(sz->H), D(sz->D), raster(sz->raster), beim_klick(sz->beim_klick),
      beim_klick_modell(sz->beim_klick_modell)
{
    verbunden = sz->geaendert.connect([this](unsigned w) {
        if (w & Szene3D::BLOCK) neu_block = true;
        if (w & Szene3D::MODELL) neu_modell = true;
        if (w & Szene3D::STEG) neu_steg = true;
        if (w & Szene3D::MARKE) neu_marke = true;
        if (w & Szene3D::ERG) neu_erg = true;
        if (w & Szene3D::HELL) neu_hell = true;
        bereich.queue_render();
    });
    bereich.set_required_version(3, 3);
    bereich.set_has_depth_buffer(true);
    bereich.set_size_request(-1, 280);
    bereich.set_hexpand(true);
    bereich.add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::POINTER_MOTION_MASK |
                       Gdk::SCROLL_MASK | Gdk::SMOOTH_SCROLL_MASK);
    bereich.signal_realize().connect([this] { realisieren(); });
    bereich.signal_unrealize().connect([this] { aufraeumen(); }, false);
    bereich.signal_render().connect([this](Glib::RefPtr<Gdk::GLContext> const &) { return malen(); }, false);
    bereich.signal_button_press_event().connect([this](GdkEventButton *e) {
        if (e->button == 2 || e->button == 3) {  // mittlere/rechte Taste: verschieben
            if (e->type != GDK_BUTTON_PRESS) return true;
            if (e->button == 3) {                // Rechtsklick ohne Ziehen: Menü – mit dem Drücken als Auslöser
                if (rk_druck) gdk_event_free(rk_druck);
                rk_druck = gdk_event_copy(reinterpret_cast<GdkEvent *>(e));
            }
            zieht = true;
            zieh_knopf = int(e->button);
            mx = px = e->x;
            my = py = e->y;
            return true;
        }
        if (e->button != 1) return false;
        zieh_knopf = 1;
        if (e->type == GDK_2BUTTON_PRESS) {      // Doppelklick: auf eine Linie → 2D, sonst Schrägansicht
            zieht = false;
            if (zeige_teil && n_teil > 0 && klick_teil && klick_teil(e->x, e->y, true)) return true;
            sicht("schraeg");
            return true;
        }
        zieht = true;
        mx = px = e->x;
        my = py = e->y;
        return true;
    });
    bereich.signal_button_release_event().connect([this](GdkEventButton *e) {
        zieht = false;
        if (e->button == 3 && klick_rechts && std::hypot(e->x - px, e->y - py) < 4) {
            klick_rechts(e->x, e->y, rk_druck ? reinterpret_cast<GdkEventButton *>(rk_druck) : e);
            return true;
        }
        if (e->button != 1) return true;
        if (std::hypot(e->x - px, e->y - py) < 4) {
            if (zeige_teil && n_teil > 0 && klick_teil) klick_teil(e->x, e->y, false);
            else if (zeige_ergebnis && beim_klick) treffen(e->x, e->y);
            else if (!zeige_ergebnis && hat_modell && beim_klick_modell)
                treffen_modell(e->x, e->y, (e->state & GDK_SHIFT_MASK) != 0);
        }
        return true;
    });
    bereich.signal_motion_notify_event().connect([this](GdkEventMotion *e) {
        if (!zieht) return false;
        if (zieh_knopf == 2 || zieh_knopf == 3) {  // verschieben: der Punkt unter der Maus bleibt unter der Maus
            V3 auge, f, sv, u;
            kamera(auge, f, sv, u);
            double k = mm_je_punkt();
            versatz = versatz - sv * ((e->x - mx) * k) + u * ((e->y - my) * k);
            mx = e->x;
            my = e->y;
            bereich.queue_render();
            return true;
        }
        wende_takt.disconnect();
        // ganz herum drehen (Peter 28.10.): über oben und unten weg; kopfüber dreht die Maus seitlich gleich
        az -= (e->x - mx) * 0.4 * (std::cos(el * M_PI / 180) < 0 ? -1 : 1);
        el += (e->y - my) * 0.3;
        if (el > 180) el -= 360;
        if (el <= -180) el += 360;
        seite_nachfuehren();
        mx = e->x;
        my = e->y;
        bereich.queue_render();
        return true;
    });
    bereich.signal_scroll_event().connect([this](GdkEventScroll *e) {
        double dy = 0;
        if (e->direction == GDK_SCROLL_UP) dy = -1;
        else if (e->direction == GDK_SCROLL_DOWN) dy = 1;
        else if (e->direction == GDK_SCROLL_SMOOTH) dy = e->delta_y;
        double alt = abstand > 0 ? abstand : grund_abstand();
        abstand = std::clamp(alt * std::pow(1.12, dy), grund_abstand() * 0.02, grund_abstand() * 6);
        // zur Maus hin zoomen: der Punkt unter der Maus (in der Ebene des Blickpunkts) bleibt stehen
        {
            V3 auge, f, sv, u;
            kamera(auge, f, sv, u);
            int w = std::max(1, bereich.get_allocated_width()), h = std::max(1, bereich.get_allocated_height());
            double k_alt = 2 * alt * std::tan(16.0 * M_PI / 180) / h;
            V3 unter = sv * ((e->x - w / 2.0) * k_alt) - u * ((e->y - h / 2.0) * k_alt);
            versatz = versatz + unter * (1 - abstand / alt);
        }
        bereich.queue_render();
        return true;
    });
}

void Ansicht::ansicht_wenden(bool animiert)
{
    wende_takt.disconnect();
    double a = az * M_PI / 180, e = el * M_PI / 180;
    V3 d0{std::cos(e) * std::cos(a), std::cos(e) * std::sin(a), std::sin(e)}, u0 = oben_vektor(), v0 = versatz;
    bool um_y = wende_um_y;
    auto drehen = [um_y](V3 v, double w) {
        double c = std::cos(w), s = std::sin(w);
        if (um_y) return V3{v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
        return V3{v.x, v.y * c - v.z * s, v.y * s + v.z * c};
    };
    auto setzen = [this, d0, u0, v0, drehen](double w) {
        V3 d = drehen(d0, w), u = drehen(u0, w);
        // el und az aus Richtung und Oben zurück: sin e = d.z, cos e = u.z
        double e = std::atan2(d.z, u.z);
        double ce = std::cos(e), se = std::sin(e);
        az = std::atan2(d.y * ce - u.y * se, d.x * ce - u.x * se) * 180 / M_PI;
        el = e * 180 / M_PI;
        versatz = drehen(v0, w);
        seite_nachfuehren();
        bereich.queue_render();
    };
    if (!animiert) {
        setzen(M_PI);
        return;
    }
    gint64 start = g_get_monotonic_time();
    wende_takt = Glib::signal_timeout().connect([start, setzen] {
        double t = std::min(1.0, (g_get_monotonic_time() - start) / 6.0e5);   // 0,6 s
        setzen(M_PI * t * t * (3 - 2 * t));
        return t < 1.0;
    }, 16);
}

void Ansicht::treffen_modell(double x, double y, bool umschalt)
{
    V3 o;
    V3 d = strahl(x, y, o);
    double best = std::numeric_limits<double>::infinity();
    std::size_t best_k = 0;
    for (std::size_t k = 0; k + 2 < index.size(); k += 3) {
        float const *a = &ecken[6 * index[k]], *b = &ecken[6 * index[k + 1]], *c = &ecken[6 * index[k + 2]];
        V3 A{a[0], a[1], a[2]}, e1 = V3{b[0], b[1], b[2]} - A, e2 = V3{c[0], c[1], c[2]} - A;
        V3 pv = kreuz(d, e2);
        double det = punkt(e1, pv);
        if (std::abs(det) < 1e-12) continue;
        V3 tv = o - A;
        double uu = punkt(tv, pv) / det;
        if (uu < 0 || uu > 1) continue;
        V3 q = kreuz(tv, e1);
        double vv = punkt(d, q) / det;
        if (vv < 0 || uu + vv > 1) continue;
        double t = punkt(e2, q) / det;
        if (t > 0 && t < best) {
            best = t;
            best_k = k;
        }
    }
    if (std::isfinite(best)) {
        V3 p = o + d * best;
        // netz_bauen: erst alle Punkte der Oberseite, dann die der Unterseite
        int seite = index[best_k] >= ecken.size() / 12 ? 2 : 1;
        beim_klick_modell(p.x, p.y, p.z, umschalt, seite);
    }
}

void Ansicht::hell_bauen()
{
    hell_index.clear();
    std::size_t halb = ecken.size() / 12;
    for (int s = 0; s < 4; ++s) {
        auto const &rs = hell_ringe[s];
        bool wand = s % 2 == 1;
        if (rs.empty()) continue;
        Geom::Rect rahmen;
        bool erst = true;
        for (auto const &r : rs)
            for (auto const &q : r) {
                if (erst) rahmen = Geom::Rect(q, q);
                else rahmen.expandTo(q);
                erst = false;
            }
        if (erst) continue;
        auto drin = [&rs](double px, double py) {
            bool in = false;
            for (auto const &r : rs)
                for (std::size_t i = 0, j = r.size() - 1; i < r.size(); j = i++)
                    if (((r[i].y() > py) != (r[j].y() > py)) &&
                        px < (r[j].x() - r[i].x()) * (py - r[i].y()) / (r[j].y() - r[i].y()) + r[i].x())
                        in = !in;
            return in;
        };
        for (std::size_t k = 0; k + 2 < index.size(); k += 3) {
            bool unten = index[k] >= halb;
            if (unten != (s >= 2)) continue;
            {                               // steil (Wand) oder flacher?
                float const *a = &ecken[6 * index[k]], *b = &ecken[6 * index[k + 1]], *c = &ecken[6 * index[k + 2]];
                double ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
                double vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
                double nx_ = uy * vz - uz * vy, ny_ = uz * vx - ux * vz, nz_ = ux * vy - uy * vx;
                double l = std::sqrt(nx_ * nx_ + ny_ * ny_ + nz_ * nz_);
                bool steil = l > 0 && std::abs(nz_) < 0.35 * l;
                if (steil != wand) continue;
            }
            double cx = 0, cy = 0;
            for (int m = 0; m < 3; ++m) {
                cx += ecken[6 * index[k + m]];
                cy += ecken[6 * index[k + m] + 1];
            }
            cx /= 3;
            cy /= 3;
            if (!rahmen.contains(Geom::Point(cx, cy))) continue;
            if (drin(cx, cy)) hell_index.insert(hell_index.end(), {index[k], index[k + 1], index[k + 2]});
        }
    }
    sz->geaendert.emit(Szene3D::HELL);
}

void Ansicht::treffen(double x, double y)
{
    auto const &R = raster;
    if (R.nx < 2 || R.ny < 2 || R.oben.size() != std::size_t(R.nx) * R.ny) return;
    int w = std::max(1, bereich.get_allocated_width()), h = std::max(1, bereich.get_allocated_height());
    V3 auge, f, s, u;
    kamera(auge, f, s, u);
    double t = std::tan(16.0 * M_PI / 180);
    double nx_ = (2 * x / w - 1) * t * double(w) / h, ny_ = (1 - 2 * y / h) * t;
    V3 d = norm(f + s * nx_ + u * ny_);
    auto hoehe = [&](double qx, double qy, float const *feld) -> double {
        int i = int(std::lround((qx - R.x0) / R.zelle)), j = int(std::lround((qy - R.y0) / R.zelle));
        if (i < 0 || j < 0 || i >= R.nx || j >= R.ny) return std::numeric_limits<double>::quiet_NaN();
        return feld[std::size_t(j) * R.nx + i];
    };
    double r = abstand > 0 ? abstand : grund_abstand();
    double schritt = R.zelle * 0.5, vorher = 0;
    for (double k = 0; k < r * 4; k += schritt) {
        V3 p = auge + d * k;
        double hz = hoehe(p.x, p.y, R.oben.data());
        if (std::isfinite(hz) && p.z <= hz) {
            // zwischen vorher und k halbieren
            double a = vorher, b = k;
            for (int n = 0; n < 20; ++n) {
                double m = (a + b) / 2;
                V3 q = auge + d * m;
                double hm = hoehe(q.x, q.y, R.oben.data());
                (std::isfinite(hm) && q.z <= hm ? b : a) = m;
            }
            V3 q = auge + d * b;
            beim_klick(q.x, q.y, hoehe(q.x, q.y, R.oben.data()),
                       R.modell.empty() ? std::numeric_limits<double>::quiet_NaN() : hoehe(q.x, q.y, R.modell.data()));
            return;
        }
        vorher = k;
    }
}

void Ansicht::sicht(char const *art)
{
    wende_takt.disconnect();
    abstand = 0;
    versatz = V3{0, 0, 0};
    bool unten = von_unten;
    if (!std::strcmp(art, "oben")) {
        az = -90;
        el = 89.5;
    } else if (!std::strcmp(art, "vorne")) {
        az = -90;
        el = 4;
    } else {
        az = -122;
        el = 30;
    }
    von_unten = false;
    if (unten) ansicht_wenden(false);   // Unterseite: dieselbe Sicht auf das gewendete Teil (Fräser von oben)
    else bereich.queue_render();
}

void Ansicht::material(double b, double h, double d)
{
    B = std::max(b, 1.0);
    H = std::max(h, 1.0);
    D = std::max(d, 0.1);
    block.clear();
    linien.clear();
    auto flaeche = [this](std::array<double, 3> n, std::array<std::array<double, 3>, 4> p) {
        for (int k : {0, 1, 2, 0, 2, 3})
            block.insert(block.end(), {float(p[k][0]), float(p[k][1]), float(p[k][2]), float(n[0]),
                                       float(n[1]), float(n[2])});
    };
    double x0 = 0, x1 = B, y0 = 0, y1 = H, z0 = -D, z1 = 0;
    flaeche({0, 0, 1}, {{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}});
    flaeche({0, 0, -1}, {{{x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}}});
    flaeche({0, -1, 0}, {{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}});
    flaeche({0, 1, 0}, {{{x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}}});
    flaeche({-1, 0, 0}, {{{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}});
    flaeche({1, 0, 0}, {{{x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}}});
    double ecke[8][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
                         {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
    int kanten[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4},
                         {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (auto &k : kanten)
        for (int e : k)
            linien.insert(linien.end(), {float(ecke[e][0]), float(ecke[e][1]), float(ecke[e][2]), 0.f, 0.f, 1.f});
    sz->geaendert.emit(Szene3D::BLOCK);
}

void Ansicht::realisieren()
{
    bereich.make_current();
    try {
        bereich.throw_if_error();
    } catch (Glib::Error const &e) {
        g_warning("Hirch 3D: OpenGL: %s", e.what().c_str());
        return;
    }
    prog = glCreateProgram();
    GLuint v = shader_bauen(GL_VERTEX_SHADER, VS), f = shader_bauen(GL_FRAGMENT_SHADER, FS);
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    glLinkProgram(prog);
    glDeleteShader(v);
    glDeleteShader(f);
    u_pv = glGetUniformLocation(prog, "uPV");
    u_licht = glGetUniformLocation(prog, "uLicht");
    u_farbe = glGetUniformLocation(prog, "uFarbe");
    u_dicke = glGetUniformLocation(prog, "uDicke");
    u_modell = glGetUniformLocation(prog, "uModell");
    u_flach = glGetUniformLocation(prog, "uFlach");
    u_farbig = glGetUniformLocation(prog, "uFarbig");
    glGenVertexArrays(1, &vao_teil);
    glGenBuffers(1, &vbo_teil);
    glGenBuffers(1, &ebo_teil);
    glBindVertexArray(vao_teil);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_teil);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_teil);
    for (int k = 0; k < 3; ++k) {
        glEnableVertexAttribArray(k);
        glVertexAttribPointer(k, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void *)(3 * k * sizeof(float)));
    }
    for (auto [va, vb] : {std::pair<GLuint *, GLuint *>{&vao_lin, &vbo_lin}, {&vao_bahn, &vbo_bahn}, {&vao_eil, &vbo_eil}}) {
        glGenVertexArrays(1, va);
        glGenBuffers(1, vb);
        glBindVertexArray(*va);
        glBindBuffer(GL_ARRAY_BUFFER, *vb);
        for (int k = 0; k < 3; ++k) {
            glEnableVertexAttribArray(k);
            glVertexAttribPointer(k, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void *)(3 * k * sizeof(float)));
        }
    }
    for (auto [va, vb] : {std::pair{&vao_gew, &vbo_gew}, std::pair{&vao_fr, &vbo_fr}, std::pair{&vao_sp, &vbo_sp},
                          std::pair{&vao_zf, &vbo_zf}, std::pair{&vao_zl, &vbo_zl}}) {
        glGenVertexArrays(1, va);
        glGenBuffers(1, vb);
        glBindVertexArray(*va);
        glBindBuffer(GL_ARRAY_BUFFER, *vb);
        for (int k = 0; k < 2; ++k) {
            glEnableVertexAttribArray(k);
            glVertexAttribPointer(k, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(3 * k * sizeof(float)));
        }
    }
    glBindVertexArray(0);
    glGenVertexArrays(6, vao);
    glGenBuffers(6, vbo);
    glGenBuffers(1, &ebo);
    glGenBuffers(3, ebo_erg);
    glGenBuffers(1, &ebo_hell);
    for (int k = 0; k < 6; ++k) {
        glBindVertexArray(vao[k]);
        glBindBuffer(GL_ARRAY_BUFFER, vbo[k]);
        if (k == 0) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(3 * sizeof(float)));
    }
    glBindVertexArray(0);
    bereit = true;
    neu_block = neu_modell = neu_steg = neu_marke = neu_erg = neu_hell = neu_teil = neu_linien = neu_fraeser = true;
    neu_spann = true;
}

void Ansicht::aufraeumen()
{
    if (!bereit) return;
    bereich.make_current();
    glDeleteVertexArrays(6, vao);
    glDeleteBuffers(6, vbo);
    glDeleteBuffers(1, &ebo);
    glDeleteBuffers(3, ebo_erg);
    glDeleteBuffers(1, &ebo_hell);
    glDeleteVertexArrays(1, &vao_teil);
    glDeleteBuffers(1, &vbo_teil);
    glDeleteBuffers(1, &ebo_teil);
    glDeleteVertexArrays(1, &vao_gew);
    glDeleteVertexArrays(1, &vao_fr);
    glDeleteBuffers(1, &vbo_fr);
    glDeleteVertexArrays(1, &vao_lin);
    glDeleteBuffers(1, &vbo_lin);
    glDeleteVertexArrays(1, &vao_bahn);
    glDeleteBuffers(1, &vbo_bahn);
    glDeleteVertexArrays(1, &vao_eil);
    glDeleteBuffers(1, &vbo_eil);
    glDeleteBuffers(1, &vbo_gew);
    for (GLuint *va : {&vao_sp, &vao_zf, &vao_zl}) glDeleteVertexArrays(1, va);
    for (GLuint *vb : {&vbo_sp, &vbo_zf, &vbo_zl}) glDeleteBuffers(1, vb);
    if (aufraeumen_gl) aufraeumen_gl();
    if (fbo_s) {
        glDeleteFramebuffers(1, &fbo_s);
        glDeleteRenderbuffers(1, &rb_s_farbe);
        glDeleteRenderbuffers(1, &rb_s_tiefe);
        fbo_s = rb_s_farbe = rb_s_tiefe = 0;
        fbo_s_w = fbo_s_h = 0;
    }
    if (fbo_m) {
        glDeleteFramebuffers(1, &fbo_m);
        glDeleteRenderbuffers(1, &rb_m_farbe);
        glDeleteRenderbuffers(1, &rb_m_tiefe);
        fbo_m = rb_m_farbe = rb_m_tiefe = 0;
        fbo_m_w = fbo_m_h = fbo_m_p = 0;
    }
    proben_max = -1;
    linie_jetzt = 1.0f;
    glDeleteProgram(prog);
    bereit = false;
}

void Ansicht::klein_puffer(int w, int h)
{
    if (fbo_s && w == fbo_s_w && h == fbo_s_h) return;
    if (!fbo_s) {
        glGenFramebuffers(1, &fbo_s);
        glGenRenderbuffers(1, &rb_s_farbe);
        glGenRenderbuffers(1, &rb_s_tiefe);
    }
    glBindRenderbuffer(GL_RENDERBUFFER, rb_s_farbe);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, rb_s_tiefe);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_s);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb_s_farbe);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb_s_tiefe);
    fbo_s_w = w;
    fbo_s_h = h;
}

bool Ansicht::ms_puffer(int w, int h, int p)
{
    if (fbo_m && w == fbo_m_w && h == fbo_m_h && p == fbo_m_p) return true;
    if (!fbo_m) {
        glGenFramebuffers(1, &fbo_m);
        glGenRenderbuffers(1, &rb_m_farbe);
        glGenRenderbuffers(1, &rb_m_tiefe);
    }
    glBindRenderbuffer(GL_RENDERBUFFER, rb_m_farbe);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, p, GL_RGBA8, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, rb_m_tiefe);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, p, GL_DEPTH_COMPONENT24, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_m);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb_m_farbe);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb_m_tiefe);
    fbo_m_w = w;
    fbo_m_h = h;
    fbo_m_p = p;
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        proben_max = 0;                     // geht nicht: ohne Glättung weiter
        return false;
    }
    return true;
}

bool Ansicht::malen()
{
    gint64 z_anfang = g_get_monotonic_time();
    if (z_letzt) z_abstand = std::max(z_abstand, (z_anfang - z_letzt) / 1000.0);
    z_letzt = z_anfang;
    if (bereit && vor_malen) {             // z. B. hirch-abtrag: rechnet in eigene Puffer
        vor_malen();
        bereich.attach_buffers();          // danach wieder in die Fläche der Ansicht
        glViewport(0, 0, std::max(1, bereich.get_allocated_width() * bereich.get_scale_factor()),
                   std::max(1, bereich.get_allocated_height() * bereich.get_scale_factor()));
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
    }
    GLint ziel_fb = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &ziel_fb);
    int voll_w = std::max(1, bereich.get_allocated_width() * bereich.get_scale_factor());
    int voll_h = std::max(1, bereich.get_allocated_height() * bereich.get_scale_factor());
    if (bereit && proben_max < 0) {
        GLint m = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &m);
        proben_max = std::clamp(int(m), 0, 4);
        if (proben_max < 2) proben_max = 0;
        GLint rb = 0;
        glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &rb);
        max_puffer = std::max(1024, int(rb));
        auto r = reinterpret_cast<char const *>(glGetString(GL_RENDERER));
        software_gl = r && (std::strstr(r, "llvmpipe") || std::strstr(r, "softpipe"));
        GLfloat lb[2] = {1, 1};
        glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, lb);
        linie_max = std::max(1.0f, lb[1]);
    }
    // skala > 1 (Peter 28.10.: starke Rechner): grösser zeichnen und verkleinern – glättet auch in den Flächen.
    // Höchstens 16 Mio. Bildpunkte und so gross, wie die Karte Puffer anlegt.
    skala_jetzt = skala;
    if (skala > 1.0) {
        double grenze = std::min({std::sqrt(16e6 / (double(voll_w) * voll_h)), double(max_puffer) / voll_w,
                                  double(max_puffer) / voll_h});
        skala_jetzt = std::max(1.0, std::min(skala, grenze));
    }
    bool klein = bereit && std::abs(skala_jetzt - 1.0) > 0.01;      // eigene Grösse (kleiner oder grösser)
    int klein_w = std::max(1, int(voll_w * skala_jetzt)), klein_h = std::max(1, int(voll_h * skala_jetzt));
    if (!klein) {
        klein_w = voll_w;
        klein_h = voll_h;
    }
    // Linien bleiben auf dem Bildschirm gleich dick (wo die Karte breite Linien kann)
    float lw = klein && skala_jetzt > 1.0 ? std::min(float(skala_jetzt), linie_max) : 1.0f;
    if (bereit && lw != linie_jetzt) {
        glLineWidth(lw);
        if (glGetError() != GL_NO_ERROR) {   // kann sie nicht: dünner, aber richtig
            linie_max = 1.0f;
            glLineWidth(1.0f);
            lw = 1.0f;
        }
        linie_jetzt = lw;
    }
    static char const *glatt_fest = g_getenv("HIRCH_3D_GLATT");
    bool glatt = glatt_fest ? glatt_fest[0] == '1' : glatt_wunsch && !software_gl;
    int proben = bereit && glatt && proben_max > 0 ? proben_max : 0;
    if (proben && skala_jetzt > 1.01) proben = std::min(proben, 2);   // grösser gezeichnet: 2 Proben genügen
    if (proben && !ms_puffer(klein_w, klein_h, proben)) proben = 0;
    proben_jetzt = proben;
    if (proben) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_m);
        glViewport(0, 0, klein_w, klein_h);
    } else if (klein) {
        klein_puffer(klein_w, klein_h);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_s);
        glViewport(0, 0, klein_w, klein_h);
    }
    bool dunkel = design_dunkel();
    if (dunkel) glClearColor(0.13f, 0.13f, 0.15f, 1);
    else glClearColor(0.17f, 0.165f, 0.20f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!bereit) return true;
    if (neu_teil) {
        glBindVertexArray(vao_teil);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_teil);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(teil_ecken.size() * sizeof(float)), teil_ecken.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_teil);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(teil_index.size() * sizeof(unsigned)), teil_index.data(),
                     GL_STATIC_DRAW);
        n_teil = GLsizei(teil_index.size());
        glBindVertexArray(vao_gew);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_gew);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(gewinde_ecken.size() * sizeof(float)), gewinde_ecken.data(),
                     GL_STATIC_DRAW);
        n_gew = GLsizei(gewinde_ecken.size() / 6);
        glBindVertexArray(vao_bahn);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_bahn);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(bahn_ecken.size() * sizeof(float)), bahn_ecken.data(), GL_STATIC_DRAW);
        n_bahn = GLsizei(bahn_ecken.size() / 9);
        glBindVertexArray(vao_eil);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_eil);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(eil_ecken.size() * sizeof(float)), eil_ecken.data(), GL_STATIC_DRAW);
        n_eil = GLsizei(eil_ecken.size() / 9);
        glBindVertexArray(0);
        neu_teil = false;
        neu_bahnen = false;
    }
    if (neu_bahnen) {                       // B-16: Bearbeitungen ein-/ausgeblendet – nur die Linien neu
        glBindVertexArray(vao_bahn);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_bahn);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(bahn_ecken.size() * sizeof(float)), bahn_ecken.data(), GL_STATIC_DRAW);
        n_bahn = GLsizei(bahn_ecken.size() / 9);
        glBindVertexArray(vao_eil);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_eil);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(eil_ecken.size() * sizeof(float)), eil_ecken.data(), GL_STATIC_DRAW);
        n_eil = GLsizei(eil_ecken.size() / 9);
        glBindVertexArray(0);
        neu_bahnen = false;
    }
    if (neu_fraeser) {
        glBindVertexArray(vao_fr);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_fr);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(fraeser_ecken.size() * sizeof(float)), fraeser_ecken.data(),
                     GL_DYNAMIC_DRAW);
        n_fr = GLsizei(fraeser_ecken.size() / 6);
        glBindVertexArray(0);
        neu_fraeser = false;
    }
    if (neu_linien) {
        glBindVertexArray(vao_lin);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_lin);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(linien_teil.size() * sizeof(float)), linien_teil.data(), GL_STATIC_DRAW);
        n_lin = GLsizei(linien_teil.size() / 9);
        glBindVertexArray(0);
        neu_linien = false;
    }
    if (neu_spann) {
        for (auto [va, vb, v, n] : {std::tuple{vao_sp, vbo_sp, &spann_ecken, &n_sp},
                                    std::tuple{vao_zf, vbo_zf, &zone_flaeche, &n_zf},
                                    std::tuple{vao_zl, vbo_zl, &zone_umriss, &n_zl}}) {
            glBindVertexArray(va);
            glBindBuffer(GL_ARRAY_BUFFER, vb);
            glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(v->size() * sizeof(float)), v->data(), GL_STATIC_DRAW);
            *n = GLsizei(v->size() / 6);
        }
        n_sp_schr = std::min(n_sp_schraube, n_sp);
        glBindVertexArray(0);
        neu_spann = false;
    }

    if (neu_block) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(block.size() * sizeof(float)), block.data(), GL_STATIC_DRAW);
        n_block = GLsizei(block.size() / 6);
        glBindBuffer(GL_ARRAY_BUFFER, vbo[2]);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(linien.size() * sizeof(float)), linien.data(), GL_STATIC_DRAW);
        n_linien = GLsizei(linien.size() / 6);
        neu_block = false;
    }
    if (neu_modell) {
        glBindVertexArray(vao[0]);
        glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(ecken.size() * sizeof(float)), ecken.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(index.size() * sizeof(unsigned)), index.data(),
                     GL_STATIC_DRAW);
        glBindVertexArray(0);
        n_index = GLsizei(index.size());
        neu_modell = false;
    }
    if (neu_hell) {                         // Indexpuffer gehört zum VAO: dort hochladen, dann zurück
        glBindVertexArray(vao[0]);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_hell);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(hell_index.size() * sizeof(unsigned)), hell_index.data(),
                     GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBindVertexArray(0);
        n_hell = GLsizei(hell_index.size());
        neu_hell = false;
    }
    if (neu_steg) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo[3]);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(steg_ecken.size() * sizeof(float)), steg_ecken.data(),
                     GL_STATIC_DRAW);
        n_steg = GLsizei(steg_ecken.size() / 6);
        neu_steg = false;
    }
    if (neu_marke) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo[4]);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(marke_ecken.size() * sizeof(float)), marke_ecken.data(),
                     GL_STATIC_DRAW);
        n_marke = GLsizei(marke_ecken.size() / 6);
        neu_marke = false;
    }
    if (neu_erg) {
        glBindVertexArray(vao[5]);
        glBindBuffer(GL_ARRAY_BUFFER, vbo[5]);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(erg_ecken.size() * sizeof(float)), erg_ecken.data(),
                     GL_STATIC_DRAW);
        for (int k = 0; k < 3; ++k) {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_erg[k]);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(erg_index[k].size() * sizeof(unsigned)),
                         erg_index[k].data(), GL_STATIC_DRAW);
            n_erg[k] = GLsizei(erg_index[k].size());
        }
        glBindVertexArray(0);
        neu_erg = false;
    }
    int w = std::max(1, bereich.get_allocated_width()), h = std::max(1, bereich.get_allocated_height());
    double r = abstand > 0 ? abstand : grund_abstand();
    double a = az * M_PI / 180, e = el * M_PI / 180;
    V3 ziel = ziel_punkt();
    V3 auge = ziel + V3{std::cos(e) * std::cos(a), std::cos(e) * std::sin(a), std::sin(e)} * r;
    V3 oben = oben_vektor();
    M4 pv = mal(perspektive(32, double(w) / h, r * 0.05, r * 4), blick(auge, ziel, oben));
    glUseProgram(prog);
    glUniformMatrix4fv(u_pv, 1, GL_FALSE, pv.data());
    V3 l = norm(V3{-0.55, -0.70, von_unten ? -0.45 : 0.45});   // von unten: Licht von unten
    glUniform3f(u_licht, float(l.x), float(l.y), float(l.z));
    glUniform1f(u_dicke, float(D));
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUniform1i(u_farbig, 0);
    bool teil = zeige_teil && (n_teil > 0 || teil_malen);
    if (teil) {                             // gefrästes Teil (Hauptfenster)
        if (teil_aus) {
            // B-16: Teil ausgeblendet – Fräser und Bahnen bleiben
        } else if (teil_malen) {                   // direkt aus den Höhenbildern (Detailstufen nah/fern)
            Blick bl;
            bl.vor = norm(auge - ziel);
            bl.rechts = norm(kreuz(ziel - auge, oben));
            bl.hoch = kreuz(bl.rechts, ziel - auge);
            bl.hoch = norm(bl.hoch);
            bl.pix = h / (2 * std::tan(16 * M_PI / 180)) * std::min(skala_jetzt, 1.0);   // grösser: Feinheit wie 100 %
            if (von_unten) {                // von unten: Matcap wie beim gewendeten Teil (Fläche zur Kamera hell)
                bl.hoch = bl.hoch * -1.0;
                bl.rechts = bl.rechts * -1.0;
            }
            teil_malen(pv, auge, l, bl);
            glUseProgram(prog);
            glUniformMatrix4fv(u_pv, 1, GL_FALSE, pv.data());
            glUniform3f(u_licht, float(l.x), float(l.y), float(l.z));
            glUniform1f(u_dicke, float(D));
            glEnable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
        } else {                            // Netz: Farbe je Ecke
            glUniform1i(u_modell, 0);
            glUniform1i(u_flach, 0);
            glUniform1i(u_farbig, 1);
            glBindVertexArray(vao_teil);
            glDrawElements(GL_TRIANGLES, n_teil, GL_UNSIGNED_INT, nullptr);
        }
        glUniform1i(u_modell, 0);
        glUniform1i(u_flach, 0);
        glUniform1i(u_farbig, 0);
        GLsizei ng = n_gew_sicht >= 0 ? std::min(n_gew, n_gew_sicht) : n_gew;
        if (ng > 0 && !teil_aus) {          // Gewinde gehört zum Teil (Peter 03.11.: blieb beim Ausblenden stehen)
            // Gewinde (Peter 23.10.: nicht zu sehen): in der Bohrung wäre es mit Licht von schräg oben dunkel
            // und in der Farbe der Bearbeitung – hier hell und mit Licht von der Kamera her, so zeichnen sich
            // die Gänge ab
            V3 kl = norm(auge - ziel);
            glUniform3f(u_licht, float(kl.x), float(kl.y), float(kl.z));
            glUniform4f(u_farbe, 0.80f, 0.86f, 0.88f, 1.f);
            glBindVertexArray(vao_gew);
            glFrontFace(GL_CW);             // gewinde_netz legt die Dreiecke im Uhrzeigersinn (Normale zur Achse)
            glDrawArrays(GL_TRIANGLES, 0, ng);
            glFrontFace(GL_CCW);
            glUniform3f(u_licht, float(l.x), float(l.y), float(l.z));
        }
        // Spannmutter und Spindel liegen hinter dem Fräser im Puffer (Peter 05.11.)
        GLsizei n_mu = std::clamp(n_fr_mutter, 0, n_fr), n_sp = std::clamp(n_fr_spindel, 0, n_fr - n_mu);
        GLsizei n_wz = n_fr - n_mu - n_sp;
        if (n_wz > 0) {                     // Fräser beim Abspielen: Schneide eingefärbt, Schaft hell
            GLsizei n_fr = n_wz;
            GLsizei ns = std::min(n_fr_schneide, n_fr), no = std::min(n_fr_oben, n_fr - ns),
                    np = std::min(n_fr_pfeil, n_fr - ns - no);
            glBindVertexArray(vao_fr);
            if (ns > 0) {
                glUniform4f(u_farbe, 0.80f, 0.62f, 0.22f, 1.f);   // wie eine TiN-Beschichtung
                glDrawArrays(GL_TRIANGLES, 0, ns);
            }
            glUniform4f(u_farbe, 0.86f, 0.88f, 0.92f, 1.f);
            glDrawArrays(GL_TRIANGLES, ns, n_fr - ns - no - np);
            if (np > 0) {                   // Fahrrichtung
                glUniform4f(u_farbe, 0.15f, 0.55f, 0.95f, 1.f);
                glDrawArrays(GL_TRIANGLES, n_fr - np, np);
            }
            if (no > 0) {                   // in der Spannzange (Gesamtlänge): blass
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
                glUniform4f(u_farbe, 0.86f, 0.88f, 0.92f, 0.30f);
                glDrawArrays(GL_TRIANGLES, n_fr - no - np, no);
                glDepthMask(GL_TRUE);
                glDisable(GL_BLEND);
            }
        }
        if (n_mu + n_sp > 0) {              // Spannmutter und Spindel halbtransparent (ohne Tiefe zu schreiben)
            glBindVertexArray(vao_fr);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            if (n_sp > 0) {
                glUniform4f(u_farbe, 0.62f, 0.63f, 0.67f, 0.20f);
                glDrawArrays(GL_TRIANGLES, n_wz + n_mu, n_sp);
            }
            if (n_mu > 0) {
                if (mutter_zustand == 2) glUniform4f(u_farbe, 0.88f, 0.11f, 0.14f, 0.60f);
                else if (mutter_zustand == 1) glUniform4f(u_farbe, 1.0f, 0.55f, 0.16f, 0.55f);
                else glUniform4f(u_farbe, 0.70f, 0.71f, 0.75f, 0.42f);
                glDrawArrays(GL_TRIANGLES, n_wz, n_mu);
            }
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
        // Linien der Zeichnung und Fräsbahnen: knapp über der Fläche, ohne Licht
        glUniform1i(u_flach, 1);
        glUniform1i(u_farbig, 1);
        glDepthFunc(GL_LEQUAL);
        if (zeige_linien && !teil_aus && n_lin > 0) {   // liegen auf dem Teil: ohne Teil nicht (Peter 03.11.)
            glBindVertexArray(vao_lin);
            glDrawArrays(GL_LINES, 0, n_lin);
        }
        GLsizei nb = n_bahn_sicht >= 0 ? std::min(n_bahn, n_bahn_sicht) : n_bahn;
        if (zeige_bahnen && nb > 0) {
            glBindVertexArray(vao_bahn);
            glDrawArrays(GL_LINES, 0, nb);
        }
        GLsizei ne = n_eil_sicht >= 0 ? std::min(n_eil, n_eil_sicht) : n_eil;
        if (zeige_eil && ne > 0) {          // Eilgänge rot
            glBindVertexArray(vao_eil);
            glDrawArrays(GL_LINES, 0, ne);
        }
        glDepthFunc(GL_LESS);
        glUniform1i(u_farbig, 0);
        glUniform1i(u_flach, 0);
    }
    if (zeige_ergebnis && !teil) {          // Fräsergebnis: Holz, orange bleibt stehen, rot zu tief
        glUniform1i(u_modell, 0);
        glUniform1i(u_flach, 0);
        float const farben[3][3] = {{0.86f, 0.78f, 0.62f}, {0.95f, 0.60f, 0.15f}, {0.88f, 0.15f, 0.14f}};
        glBindVertexArray(vao[5]);
        for (int k = 0; k < 3; ++k) {
            if (n_erg[k] <= 0) continue;
            glUniform4f(u_farbe, farben[k][0], farben[k][1], farben[k][2], 1.f);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_erg[k]);
            glDrawElements(GL_TRIANGLES, n_erg[k], GL_UNSIGNED_INT, nullptr);
        }
    }
    if (n_index > 0 && !zeige_ergebnis && !teil) {
        glUniform1i(u_modell, 1);
        glUniform1i(u_flach, 0);
        glUniform4f(u_farbe, 0.84f, 0.84f, 0.82f, 1.f);
        glBindVertexArray(vao[0]);
        glDrawElements(GL_TRIANGLES, n_index, GL_UNSIGNED_INT, nullptr);
        if (n_hell > 0) {                   // gewählte Flächen blau, knapp vor dem Modell
            glUniform1i(u_modell, 0);
            glUniform4f(u_farbe, 0.21f, 0.52f, 0.89f, 1.f);
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.f, -2.f);
            glDepthFunc(GL_LEQUAL);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_hell);
            glDrawElements(GL_TRIANGLES, n_hell, GL_UNSIGNED_INT, nullptr);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
            glDepthFunc(GL_LESS);
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
    }
    glUniform1i(u_modell, 0);
    // Haltestege gelb wie auf der Zeichnung; im gefrästen Teil färbt TeilGL das stehen gebliebene Material
    if (n_steg > 0 && !zeige_ergebnis && !(teil && teil_malen) && (!teil || zeige_stege)) {
        glUniform1i(u_flach, 0);
        glUniform4f(u_farbe, 1.0f, 0.80f, 0.10f, 1.f);
        if (teil) {                         // im gefrästen Teil: dort, wo das Material stehen bleibt
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.f, -2.f);
            glDepthFunc(GL_LEQUAL);
        }
        glBindVertexArray(vao[3]);
        glDrawArrays(GL_TRIANGLES, 0, n_steg);
        glDepthFunc(GL_LESS);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    if (n_marke > 0) {                      // Spindel/Halterung streift hier (rot)
        glUniform1i(u_flach, 0);
        glUniform4f(u_farbe, 0.88f, 0.11f, 0.14f, 1.f);
        glBindVertexArray(vao[4]);
        glDrawArrays(GL_TRIANGLES, 0, n_marke);
    }
    if (zeige_spann && n_sp > 0) {          // Spannmittel grau, Schrauben dunkel
        glUniform1i(u_flach, 0);
        glBindVertexArray(vao_sp);
        glUniform4f(u_farbe, 0.50f, 0.50f, 0.53f, 1.f);
        glDrawArrays(GL_TRIANGLES, 0, n_sp - n_sp_schr);
        if (n_sp_schr > 0) {
            glUniform4f(u_farbe, 0.25f, 0.25f, 0.28f, 1.f);
            glDrawArrays(GL_TRIANGLES, n_sp - n_sp_schr, n_sp_schr);
        }
    }
    if (zeige_spann && n_zl > 0) {          // Sperrzone: Umriss orange
        glUniform1i(u_flach, 1);
        glUniform4f(u_farbe, 1.0f, 0.47f, 0.0f, 1.f);
        glDepthFunc(GL_LEQUAL);
        glBindVertexArray(vao_zl);
        glDrawArrays(GL_LINES, 0, n_zl);
        glDepthFunc(GL_LESS);
        glUniform1i(u_flach, 0);
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glUniform1i(u_flach, 0);
    if (zeige_spann && n_zf > 0) {          // Sperrzone (noch nicht gefräst): orange, durchsichtig
        glUniform1i(u_flach, 1);
        glUniform4f(u_farbe, 1.0f, 0.47f, 0.0f, 0.30f);
        glBindVertexArray(vao_zf);
        glDrawArrays(GL_TRIANGLES, 0, n_zf);
        glUniform1i(u_flach, 0);
    }
    if (zeige_block) {                      // Rohteil: durchsichtig mit Kanten
        glUniform4f(u_farbe, 0.52f, 0.60f, 0.68f, 0.20f);
        glBindVertexArray(vao[1]);
        // etwas nach hinten: wo die Fläche des Teils mit dem Rohteil zusammenfällt, gewinnt das Teil (kein Flimmern)
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.f, 8.f);
        glDrawArrays(GL_TRIANGLES, 0, n_block);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glUniform1i(u_flach, 1);
        glUniform4f(u_farbe, 0.66f, 0.74f, 0.82f, 0.9f);
        glBindVertexArray(vao[2]);
        glDrawArrays(GL_LINES, 0, n_linien);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
    glUseProgram(0);
    // Deckkraft überall 1: halbdurchsichtige Zonen/Schaft senken sonst Alpha im Puffer. Unter Windows mit Mesa
    // (ohne Grafiktreiber) liest GTK das Bild mit Alpha zurück – dort schien das Teil schwarz durch (28.10.).
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (proben) {                           // geglättet: erst in fbo_s auflösen (gleiches Format), dann auf die Fläche
        klein_puffer(klein_w, klein_h);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_m);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_s);
        glBlitFramebuffer(0, 0, klein_w, klein_h, 0, 0, klein_w, klein_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_s);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(ziel_fb));
        glBlitFramebuffer(0, 0, klein_w, klein_h, 0, 0, voll_w, voll_h, GL_COLOR_BUFFER_BIT,
                          klein ? GL_LINEAR : GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, GLuint(ziel_fb));
        glViewport(0, 0, voll_w, voll_h);
    } else if (klein) {                     // verkleinertes Bild weich auf die Fläche
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_s);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(ziel_fb));
        glBlitFramebuffer(0, 0, klein_w, klein_h, 0, 0, voll_w, voll_h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, GLuint(ziel_fb));
        glViewport(0, 0, voll_w, voll_h);
    }
    if (g_getenv("HIRCH_3D_ZEIT")) {
        static bool gemeldet = false;
        if (!gemeldet) {                    // einmal: welche Grafik (Fehlersuche Windows/Mesa)
            gemeldet = true;
            auto s = [](GLenum e) { auto p = reinterpret_cast<char const *>(glGetString(e)); return p ? p : "?"; };
            GLint fb_a = -1;
            glBindFramebuffer(GL_FRAMEBUFFER, GLuint(ziel_fb));
            if (ziel_fb == 0)
                glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_BACK_LEFT,
                                                      GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE, &fb_a);
            else
                glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                                      GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE, &fb_a);
            g_message("Hirch 3D: Grafik %s · %s · %s, Ziel-Puffer %d (Alpha %d Bit), Fehler 0x%x", s(GL_VENDOR),
                      s(GL_RENDERER), s(GL_VERSION), int(ziel_fb), int(fb_a), unsigned(glGetError()));
        }
        ++z_bilder;
        z_cpu = std::max(z_cpu, (g_get_monotonic_time() - z_anfang) / 1000.0);
        if (z_anfang - z_log > 1000000) {
            g_message("Hirch 3D: %d Bilder/s, längster Abstand %.0f ms, CPU in malen bis %.1f ms, Fenster %d × %d, "
                      "Glättung %d Proben", z_bilder, z_abstand, z_cpu, bereich.get_allocated_width(),
                      bereich.get_allocated_height(), proben_jetzt);
            z_log = z_anfang;
            z_bilder = 0;
            z_cpu = z_abstand = 0;
        }
    }
    return true;
}

} // namespace hirch3d
} // namespace Inkscape::UI::Dialog
