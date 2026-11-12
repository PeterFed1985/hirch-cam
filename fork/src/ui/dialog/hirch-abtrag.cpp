// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: der eine Rechenweg für das gefräste Teil – siehe hirch-abtrag.h.
 */
#include "ui/dialog/hirch-abtrag.h"

#include <glib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace Inkscape::UI::Dialog {

namespace {

using M4 = std::array<float, 16>;   // spaltenweise wie OpenGL

M4 ortho(double l, double r, double u, double o)
{
    M4 m{};
    m[0] = float(2 / (r - l));
    m[5] = float(2 / (o - u));
    m[10] = -1;
    m[12] = float(-(r + l) / (r - l));
    m[13] = float(-(o + u) / (o - u));
    m[15] = 1;
    return m;
}


// ---------------------------------------------------------------- Shader
char const *const SWEEP_VS = R"(#version 330 core
uniform mat4 uPV;
uniform float uRand;
layout(location = 0) in vec2 position;
layout(location = 1) in vec3 aA;
layout(location = 2) in vec3 aB;
layout(location = 3) in vec4 aW;
layout(location = 4) in float aT;
layout(location = 5) in float aK;
out vec2 vP;
flat out float fT;
flat out float fK;
flat out vec3 fA;
flat out vec3 fB;
flat out vec4 fW;
void main() {
  vec2 a = aA.xy, b = aB.xy, d = b - a;
  float L = length(d);
  vec2 ux = L > 1e-6 ? d / L : vec2(1.0, 0.0);
  vec2 uy = vec2(-ux.y, ux.x);
  float R = aW.x + uRand;
  vec2 p = 0.5 * (a + b) + ux * position.x * (0.5 * L + R) + uy * position.y * R;
  vP = p; fA = aA; fB = aB; fW = aW; fT = aT; fK = aK;
  gl_Position = uPV * vec4(p, 0.0, 1.0);
})";

char const *const SWEEP_FS = R"(#version 330 core
uniform float uHmin;
in vec2 vP;
flat in vec3 fA;
flat in vec3 fB;
flat in vec4 fW;
flat in float fT;
flat in float fK;
layout(location = 0) out vec4 farbe;
layout(location = 1) out vec4 kand;
void main() {
  vec2 a = fA.xy, u = fB.xy - a, w = vP - a;
  float L2 = dot(u, u), R = fW.x, tanH = fW.y, rt = fW.z, rc = fW.w, dz = fB.z - fA.z, h;
  if (tanH > 0.0 && rc > 0.0) {
    // konischer Kugelfräser: Kugel (Radius rc) an der Spitze, darüber der Kegel, der sie berührt
    // (halber Kegelwinkel atan(tanH)). Höhe über der Spitze im Abstand d von der Achse:
    //   d <= dt: rc - sqrt(rc² - d²);  sonst rc - rc/sin + d/tan
    float sn = tanH / sqrt(1.0 + tanH * tanH), dt = rc * sqrt(max(0.0, 1.0 - sn * sn));
    float tk = 0.5;                       // Stelle, an der der Kegel am tiefsten schneidet
    if (L2 >= 1e-12) {
      float L = sqrt(L2), ts = dot(w, u) / L2;
      float dp = length(w - ts * u);
      float k = -dz * tanH / L;
      if (abs(k) < 1.0) tk = ts + k * dp / sqrt(1.0 - k * k) / L;
      else tk = k > 0.0 ? 1.0 : 0.0;
      tk = clamp(tk, 0.0, 1.0);
    }
    float ts = L2 < 1e-12 ? 0.0 : clamp(dot(w, u) / L2, 0.0, 1.0);
    h = 1e9;
    for (int i = 0; i < 4; i++) {
      float t = i == 0 ? ts : (i == 1 ? tk : (i == 2 ? 0.0 : 1.0));
      float d = length(vP - (a + t * u));
      if (d > R) continue;
      float prof = d <= dt ? rc - sqrt(max(0.0, rc * rc - d * d)) : rc - rc / sn + d / tanH;
      h = min(h, fA.z + dz * t + prof);
    }
    if (h > 1e8) discard;
  } else if (tanH <= 0.0 && rc > 0.0) {
    float ts = L2 < 1e-12 ? 0.0 : clamp(dot(w, u) / L2, 0.0, 1.0);
    h = 1e9;
    for (int i = 0; i < 3; i++) {
      float t = i == 0 ? ts : (i == 1 ? 0.0 : 1.0);
      float dist = length(vP - (a + t * u));
      if (dist > R) continue;
      float aus = max(0.0, dist - (R - rc));
      h = min(h, fA.z + dz * t + rc - sqrt(max(0.0, rc * rc - aus * aus)));
    }
    if (h > 1e8) discard;
  } else if (tanH <= 0.0) {
    float tlo = 0.0, thi = 1.0;
    if (L2 < 1e-12) {
      if (dot(w, w) > R * R) discard;
    } else {
      float ts = dot(w, u) / L2;
      vec2 q = w - ts * u;
      float d2 = dot(q, q);
      if (d2 > R * R) discard;
      float halb = sqrt((R * R - d2) / L2);
      tlo = max(0.0, ts - halb); thi = min(1.0, ts + halb);
      if (tlo > thi) discard;
    }
    h = min(fA.z + dz * tlo, fA.z + dz * thi);
  } else {
    float t;
    if (L2 < 1e-12) {
      t = dz < 0.0 ? 1.0 : 0.0;
    } else {
      float L = sqrt(L2), ts = dot(w, u) / L2;
      float dp = length(w - ts * u);
      float k = -dz * tanH / L;
      if (abs(k) < 1.0) t = ts + k * dp / sqrt(1.0 - k * k) / L;
      else t = k > 0.0 ? 1.0 : 0.0;
      t = clamp(t, 0.0, 1.0);
    }
    float dist = length(vP - (a + t * u));
    if (dist > R) discard;
    h = fA.z + dz * t + max(0.0, dist - rt) / tanH;
  }
  if (h >= 0.0) discard;
  gl_FragDepth = clamp((h - uHmin) / (-uHmin), 0.0, 1.0);
  farbe = vec4((fT + 1.0) / 32.0, 0.0, 0.0, 1.0);   // wer zuletzt am tiefsten gefräst hat
  kand = vec4((fK + 1.0) / 65535.0, 0.0, 0.0, 1.0); // Bearbeitung (Index + 1) der tiefsten Stelle
})";


// Karte «zuletzt tiefer gemacht von»: wo dieses Stück die Stelle um mehr als die Mindesttiefe tiefer gemacht hat,
// gilt die Bearbeitung seiner tiefsten Stelle
char const *const KARTE_VS = R"(#version 330 core
out vec2 vUV;
void main() {
  vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
  vUV = p * 0.5 + 0.5;
  gl_Position = vec4(p, 0.0, 1.0);
})";

char const *const KARTE_FS = R"(#version 330 core
uniform sampler2D uTiefe;
uniform sampler2D uVorher;
uniform sampler2D uKand;
uniform float uMindest;     // Mindesttiefe, normiert wie die Tiefe
in vec2 vUV;
layout(location = 0) out vec4 karte;
void main() {
  float neu = textureLod(uTiefe, vUV, 0.0).r, alt = textureLod(uVorher, vUV, 0.0).r;
  if (!(neu < alt - uMindest)) discard;
  karte = vec4(textureLod(uKand, vUV, 0.0).r, 0.0, 0.0, 1.0);
})";

GLuint shader(GLenum art, char const *quelle, std::string &fehler)
{
    GLuint s = glCreateShader(art);
    glShaderSource(s, 1, &quelle, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        fehler += log;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint programm(char const *vs, char const *fs, std::string &fehler)
{
    GLuint v = shader(GL_VERTEX_SHADER, vs, fehler), f = shader(GL_FRAGMENT_SHADER, fs, fehler);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        fehler += log;
        glDeleteProgram(p);
        return 0;
    }
    return p;
}


constexpr int STRIDE = 12;     // Instanz: A (3), B (3), Werkzeug (4), Farbtafel-Index, Bearbeitung

} // namespace

// ---------------------------------------------------------------- .tap lesen
std::vector<AbtragBewegung> tap_lesen(std::string const &text, int prog, double sicherheit)
{
    std::vector<AbtragBewegung> aus;
    std::istringstream in(text);
    std::string roh;
    double x = 0, y = 0, z = sicherheit, f = 0;
    bool eil = true;
    int bogen = 0;                       // 2 = G2 (im Uhrzeigersinn), 3 = G3
    int nr = 0;
    while (std::getline(in, roh)) {
        ++nr;
        std::string l;
        int klammer = 0;
        for (char c : roh) {
            if (c == '(') ++klammer;
            else if (c == ')') { if (klammer) --klammer; continue; }
            else if (c == ';' && !klammer) break;
            if (!klammer) l += (char)std::toupper((unsigned char)c);
        }
        bool pos = false;
        double nx = x, ny = y, nz = z, bi = 0, bj = 0;
        bool ij = false;
        for (size_t i = 0; i < l.size(); ++i) {
            char c = l[i];
            if (c != 'G' && c != 'X' && c != 'Y' && c != 'Z' && c != 'F' && c != 'I' && c != 'J') continue;
            // Zahl selbst lesen: strtod hielte «0X12.3» (aus «G0X12.3») für eine Hex-Zahl
            size_t j = i + 1;
            while (j < l.size() && l[j] == ' ') ++j;
            size_t start = j;
            if (j < l.size() && (l[j] == '-' || l[j] == '+')) ++j;
            bool ziffer = false;
            while (j < l.size() && (std::isdigit((unsigned char)l[j]) || l[j] == '.')) {
                ziffer = ziffer || std::isdigit((unsigned char)l[j]);
                ++j;
            }
            if (!ziffer) continue;
            // g_ascii_strtod: unabhängig von der Spracheinstellung (it_IT/de_DE: Komma!)
            double v = g_ascii_strtod(l.substr(start, j - start).c_str(), nullptr);
            i = j - 1;
            if (c == 'G') {
                if (v == 0) eil = true;
                else if (v == 1 || v == 2 || v == 3) eil = false;
                if (v == 0 || v == 1) bogen = 0;
                else if (v == 2 || v == 3) bogen = (int)v;
            } else if (c == 'I' || c == 'J') {
                ij = true;
                (c == 'I' ? bi : bj) = v;
            } else if (c == 'F') {
                f = v;
            } else {
                pos = true;
                if (c == 'X') nx = v;
                if (c == 'Y') ny = v;
                if (c == 'Z') nz = v;
            }
        }
        if (pos && bogen && ij && !eil) {
            // Bogen in kurze Geraden auflösen (Sehnenfehler höchstens ~0.005 mm)
            double mx = x + bi, my = y + bj;
            double r = std::hypot(x - mx, y - my);
            double w0 = std::atan2(y - my, x - mx), w1 = std::atan2(ny - my, nx - mx);
            double dw = bogen == 3 ? std::fmod(w1 - w0 + 4 * M_PI, 2 * M_PI) : -std::fmod(w0 - w1 + 4 * M_PI, 2 * M_PI);
            if (std::abs(dw) < 1e-9) dw = bogen == 3 ? 2 * M_PI : -2 * M_PI;
            double schritt = r > 0.005 ? 2 * std::acos(std::max(-1.0, 1 - 0.005 / r)) : M_PI / 4;
            int k = std::max(1, (int)std::ceil(std::abs(dw) / std::max(schritt, 1e-3)));
            double z0 = z;
            for (int q = 1; q <= k; ++q) {
                double w = w0 + dw * q / k;
                AbtragBewegung b;
                b.eil = false;
                b.x = q == k ? nx : mx + r * std::cos(w);
                b.y = q == k ? ny : my + r * std::sin(w);
                b.z = z0 + (nz - z0) * q / k;
                b.f = f; b.p = prog; b.zeile = nr; b.text = roh;
                aus.push_back(std::move(b));
            }
            x = nx; y = ny; z = nz;
        } else if (pos) {
            x = nx; y = ny; z = nz;
            AbtragBewegung b;
            b.eil = eil; b.x = x; b.y = y; b.z = z; b.f = f; b.p = prog; b.zeile = nr; b.text = roh;
            aus.push_back(std::move(b));
        }
    }
    return aus;
}

std::string datei_lesen(std::string const &pfad)
{
    // g_file_get_contents statt std::ifstream: unter Windows nimmt ifstream den Pfad in der ANSI-Codepage –
    // Dateien mit Umlauten im Namen (test_flügel_s1_1_B6.tap) blieben leer (B-11)
    gchar *inhalt = nullptr;
    gsize laenge = 0;
    if (!g_file_get_contents(pfad.c_str(), &inhalt, &laenge, nullptr)) return {};
    std::string s(inhalt, laenge);
    g_free(inhalt);
    return s;
}


// ---------------------------------------------------------------- Gewinde (Formteil)
/// Gewindeprofil (ISO, 60°): Radius der Gewindefläche bei der axialen Phase s (0 = Mitte der Rille).
/// Innen: Rille bis zum Nenn-Ø (Grund P/8 breit), dazwischen das Material bis zum Kernloch.
/// Aussen: Rille bis zum Kern-Ø (P/4 breit), Spitzen am Nenn-Ø.
double gewinde_radius(AbtragGewinde const &g, double s)
{
    double P = g.steigung;
    double t = std::fmod(s, P);
    if (t < 0) t += P;
    t = std::min(t, P - t);                                 // Abstand zur Rillenmitte, 0 … P/2
    double rmaj = g.nenn / 2, rmin = std::min(g.kern, g.nenn) / 2, k = std::sqrt(3.0);   // Flanke: √3 radial je mm
    double r = g.innen ? rmaj - std::max(0.0, t - P / 16) * k : rmin + std::max(0.0, t - P / 8) * k;
    return std::clamp(r, rmin, rmaj);
}

/// Formteil Gewinde: die Gewindefläche (Schraubenfläche) und oben der Ring bis zur Kante der Bohrung bzw.
/// des Zapfens, als Dreiecke (x, y, z, nx, ny, nz) – Material-mm, Z0 = Oberfläche der Seite.
void gewinde_netz(AbtragGewinde const &g, std::vector<float> &av, int n_um)
{
    double P = g.steigung, z1 = g.oben, z0 = g.oben - g.tiefe;
    if (P <= 0 || g.tiefe <= 0) return;
    int nz = std::clamp((int)std::ceil(g.tiefe / (P / 16)), 2, 1200);
    double dir = g.rechts ? -1.0 : 1.0;                     // Rechtsgewinde: Rille steigt gegen den Uhrzeigersinn
    auto punkt = [&](double w, double z) {
        double r = gewinde_radius(g, z + dir * P * w / (2 * M_PI));
        return std::array<double, 3>{g.x + r * std::cos(w), g.y + r * std::sin(w), z};
    };
    auto ablegen = [&](std::array<double, 3> const &q, std::array<double, 3> const &n) {
        av.insert(av.end(), {float(q[0]), float(q[1]), float(q[2]), float(n[0]), float(n[1]), float(n[2])});
    };
    auto normale = [&](double w, double z) {
        double e = 1e-4;
        auto a = punkt(w, z), bw = punkt(w + e, z), bz = punkt(w, z + e);
        std::array<double, 3> tw{bw[0] - a[0], bw[1] - a[1], bw[2] - a[2]}, tz{bz[0] - a[0], bz[1] - a[1], bz[2] - a[2]};
        std::array<double, 3> n{tw[1] * tz[2] - tw[2] * tz[1], tw[2] * tz[0] - tw[0] * tz[2], tw[0] * tz[1] - tw[1] * tz[0]};
        double l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (l < 1e-12) return std::array<double, 3>{0, 0, 1};
        double rad = n[0] * std::cos(w) + n[1] * std::sin(w);
        double sg = (g.innen ? rad > 0 : rad < 0) ? -1.0 : 1.0;   // innen zur Achse, aussen nach aussen
        return std::array<double, 3>{sg * n[0] / l, sg * n[1] / l, sg * n[2] / l};
    };
    for (int j = 0; j < nz; ++j) {
        double za = z0 + (z1 - z0) * j / nz, zb = z0 + (z1 - z0) * (j + 1) / nz;
        for (int i = 0; i < n_um; ++i) {
            double wa = 2 * M_PI * i / n_um, wb = 2 * M_PI * (i + 1) / n_um;
            auto p00 = punkt(wa, za), p10 = punkt(wb, za), p11 = punkt(wb, zb), p01 = punkt(wa, zb);
            auto n00 = normale(wa, za), n10 = normale(wb, za), n11 = normale(wb, zb), n01 = normale(wa, zb);
            ablegen(p00, n00); ablegen(p10, n10); ablegen(p11, n11);
            ablegen(p00, n00); ablegen(p11, n11); ablegen(p01, n01);
        }
    }
    // oben: Ring zwischen Gewindefläche und Kante (innen: Bohrung Nenn-Ø, aussen: Zapfen Kern-Ø)
    double kante = g.innen ? g.nenn / 2 : std::min(g.kern, g.nenn) / 2;
    std::array<double, 3> hoch{0, 0, 1};
    for (int i = 0; i < n_um; ++i) {
        double wa = 2 * M_PI * i / n_um, wb = 2 * M_PI * (i + 1) / n_um;
        auto a = punkt(wa, z1), b = punkt(wb, z1);
        std::array<double, 3> ka{g.x + kante * std::cos(wa), g.y + kante * std::sin(wa), z1};
        std::array<double, 3> kb{g.x + kante * std::cos(wb), g.y + kante * std::sin(wb), z1};
        ablegen(a, hoch); ablegen(b, hoch); ablegen(kb, hoch);
        ablegen(a, hoch); ablegen(kb, hoch); ablegen(ka, hoch);
    }
}
using M4 = std::array<float, 16>;   // spaltenweise wie OpenGL


// ---------------------------------------------------------------- Abtrag
bool HirchAbtrag::gl_start(std::string &fehler)
{
    p_sweep = programm(SWEEP_VS, SWEEP_FS, fehler);
    p_karte = programm(KARTE_VS, KARTE_FS, fehler);
    if (!vao_leer) glGenVertexArrays(1, &vao_leer);
    return p_sweep && p_karte;
}

void HirchAbtrag::gl_ende()
{
    puffer_weg();
    for (auto p : {p_sweep, p_karte}) if (p) glDeleteProgram(p);
    p_sweep = p_karte = 0;
    if (vao_leer) glDeleteVertexArrays(1, &vao_leer);
    vao_leer = 0;
}

void HirchAbtrag::puffer_weg()
{
    GLuint b[] = {vbo_teil, vbo_quad, vbo_inst};
    glDeleteBuffers(3, b);
    vbo_teil = vbo_quad = vbo_inst = 0;
    for (GLuint *v : {&vao_sweep, &vao_sweep2, &vao_teil, &vao_bereich}) {
        if (*v) glDeleteVertexArrays(1, v);
        *v = 0;
    }
    for (GLuint *f : {&fbo_h, &fbo_h2, &fbo_k, &fbo_k2, &fbo_v, &fbo_v2}) {
        if (*f) glDeleteFramebuffers(1, f);
        *f = 0;
    }
    for (GLuint *t : {&tex_h, &tex_wz, &tex_karte, &tex_h2, &tex_wz2, &tex_karte2, &tex_v, &tex_v2, &tex_kand, &tex_kand2}) {
        if (*t) glDeleteTextures(1, t);
        *t = 0;
    }
    gez_inst = -1;
    instanzen = zeige_instanzen = 0;
}

static GLuint textur(GLint intern, GLenum format, GLenum typ, int W, int H)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, intern, W, H, 0, format, typ, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (format == GL_DEPTH_COMPONENT) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    return t;
}

void HirchAbtrag::seite_anlegen(GLuint &fbo, GLuint &th, GLuint &twz, GLuint &tk, GLuint &fbo_k_, GLuint &fbo_v_,
                                GLuint &tv, GLuint &tkand)
{
    th = textur(GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, W, H);
    twz = textur(GL_R8, GL_RED, GL_UNSIGNED_BYTE, W, H);     // Farbtafel der Vorschau: wer am tiefsten gefräst hat
    tkand = textur(GL_R16, GL_RED, GL_UNSIGNED_SHORT, W, H);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, th, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, twz, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, tkand, 0);
    GLenum ziele[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, ziele);
    glReadBuffer(GL_NONE);
    // Karte (eigener Puffer: die Übernahme liest Tiefe, Vorher und Kandidat)
    tk = textur(GL_R16, GL_RED, GL_UNSIGNED_SHORT, W, H);
    glGenFramebuffers(1, &fbo_k_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_k_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tk, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_NONE);
    // Tiefe vor dem Stück
    tv = textur(GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, W, H);
    glGenFramebuffers(1, &fbo_v_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_v_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tv, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
}

void HirchAbtrag::einrichten(double x0_, double y0_, double w_, double h_, double dicke, bool zweiseitig_,
                             bool software, std::vector<AbtragBewegung> const &moves,
                             std::vector<AbtragWerkzeug> const &wz, std::vector<int> const &seite_je_prog,
                             double sicherheit, std::vector<AbtragBearbeitung> bearb)
{
    puffer_weg();
    x0 = x0_; y0 = y0_; w = w_; h = h_;
    zweiseitig = zweiseitig_;
    bearbeitungen = std::move(bearb);
    // Raster: kleine Werkstücke sehr fein (bis 0,008 mm – feine Gravuren, V-Carve-Spitzen), grosse gröber:
    // höchstens ~4 Mio. Zellen; ohne Grafikkarte 1,5 Mio., nicht feiner als 0,05 mm
    GLint maxtex = 4096;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxtex);
    double mx = std::min(16384, (int)maxtex);
    zelle = std::max({software ? 0.05 : 0.008, w / mx, h / mx, std::sqrt(w * h / (software ? 1.5e6 : 4.0e6))});
    W = std::max(2, (int)std::lround(w / zelle));
    H = std::max(2, (int)std::lround(h / zelle));
    hmin = -(dicke + 10);
    seite_anlegen(fbo_h, tex_h, tex_wz, tex_karte, fbo_k, fbo_v, tex_v, tex_kand);
    if (zweiseitig) seite_anlegen(fbo_h2, tex_h2, tex_wz2, tex_karte2, fbo_k2, fbo_v2, tex_v2, tex_kand2);

    auto seite_von = [&](AbtragBewegung const &m) {
        return zweiseitig && m.p >= 0 && m.p < (int)seite_je_prog.size() ? seite_je_prog[m.p] : 1;
    };
    // Wegstücke als Instanzen (nur, was unter die Oberfläche reicht)
    std::vector<float> inst;
    bis_instanz.assign(moves.size() + 1, 0);
    seite_der_bewegung.assign(moves.size(), 1);
    AbtragBewegung p;
    p.z = sicherheit;
    int prog = moves.empty() ? 0 : moves[0].p;
    n_inst1 = -1;
    for (size_t k = 0; k < moves.size(); ++k) {
        auto const &m = moves[k];
        seite_der_bewegung[k] = seite_von(m);
        if (m.p != prog) {
            p = AbtragBewegung{};
            p.z = sicherheit;
            prog = m.p;
        }
        if (n_inst1 < 0 && seite_von(m) == 2) n_inst1 = (int)(inst.size() / STRIDE);
        if (std::min(p.z, m.z) < 0 && m.p >= 0 && m.p < (int)wz.size()) {
            auto const &P = wz[m.p];
            bool v = P.form == "v", kk = P.form == "konisch";
            // konisch: Kegel (tan) und Kugelradius zusammen – der Shader erkennt die Form daran
            inst.insert(inst.end(), {float(p.x), float(p.y), float(p.z), float(m.x), float(m.y), float(m.z),
                                     float(P.durchmesser / 2),
                                     v || kk ? float(std::tan(std::clamp(P.vwinkel, 0.5, 179.0) / 2 * M_PI / 180)) : 0.f,
                                     v ? float(P.spitze / 2) : 0.f,
                                     P.form == "kugel" ? float(P.eckradius > 0 ? P.eckradius : P.durchmesser / 2)
                                     : kk ? float(std::max(0.01, P.eckradius)) : 0.f,
                                     float(m.farbe), float(m.bearbeitung)});
        }
        bis_instanz[k + 1] = (int)(inst.size() / STRIDE);
        p = m;
    }
    instanzen = (int)(inst.size() / STRIDE);
    if (n_inst1 < 0) n_inst1 = instanzen;
    glGenVertexArrays(1, &vao_sweep);
    glBindVertexArray(vao_sweep);
    float quad[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    glGenBuffers(1, &vbo_quad);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_quad);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glGenBuffers(1, &vbo_inst);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_inst);
    glBufferData(GL_ARRAY_BUFFER, std::max<size_t>(1, inst.size()) * sizeof(float),
                 inst.empty() ? nullptr : inst.data(), GL_STATIC_DRAW);
    auto instanz_zeiger = [&](size_t erste) {
        int teile[] = {3, 3, 4, 1, 1}, ort = 1, versatz = 0;
        for (int t : teile) {
            glEnableVertexAttribArray(ort);
            glVertexAttribPointer(ort, t, GL_FLOAT, GL_FALSE, STRIDE * sizeof(float),
                                  reinterpret_cast<void *>((erste * STRIDE + size_t(versatz)) * sizeof(float)));
            glVertexAttribDivisor(ort, 1);
            ++ort;
            versatz += t;
        }
    };
    instanz_zeiger(0);
    // angefangene Bewegung: eine Instanz, bei jedem Bild neu
    inst_daten = inst;
    glGenVertexArrays(1, &vao_teil);
    glBindVertexArray(vao_teil);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_quad);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glGenBuffers(1, &vbo_teil);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_teil);
    glBufferData(GL_ARRAY_BUFFER, STRIDE * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    instanz_zeiger(0);
    glGenVertexArrays(1, &vao_bereich);
    glBindVertexArray(vao_bereich);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_quad);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_inst);
    instanz_zeiger(0);
    if (zweiseitig) {
        // Seite 2: dieselben Daten ab der ersten Instanz von Seite 2
        glGenVertexArrays(1, &vao_sweep2);
        glBindVertexArray(vao_sweep2);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_quad);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_inst);
        instanz_zeiger((size_t)n_inst1);
    }
    glBindVertexArray(0);
    gez_inst = -1;
}

void HirchAbtrag::fraesen(GLuint vao, int anzahl)
{
    if (anzahl <= 0) return;
    auto pv = ortho(x0, x0 + w, y0, y0 + h);
    glUseProgram(p_sweep);
    glUniformMatrix4fv(glGetUniformLocation(p_sweep, "uPV"), 1, GL_FALSE, pv.data());
    glUniform1f(glGetUniformLocation(p_sweep, "uRand"), float(1.5 * zelle));
    glUniform1f(glGetUniformLocation(p_sweep, "uHmin"), float(hmin));
    glBindVertexArray(vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, anzahl);
}

void HirchAbtrag::bereich_fraesen(int von, int bis_)
{
    if (bis_ <= von) return;
    glBindVertexArray(vao_bereich);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_inst);
    int teile[] = {3, 3, 4, 1, 1}, ort = 1, versatz = 0;
    for (int t : teile) {
        glVertexAttribPointer(ort, t, GL_FLOAT, GL_FALSE, STRIDE * sizeof(float),
                              reinterpret_cast<void *>((size_t(von) * STRIDE + size_t(versatz)) * sizeof(float)));
        ++ort;
        versatz += t;
    }
    fraesen(vao_bereich, bis_ - von);
}

void HirchAbtrag::vorher_merken(GLuint fbo, GLuint fbo_v_)
{
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_v_);
    glBlitFramebuffer(0, 0, W, H, 0, 0, W, H, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
}

void HirchAbtrag::karte_uebernehmen(GLuint th, GLuint tv, GLuint tkand, GLuint fbo_k_)
{
    GLint fbo_alt = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo_alt);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_k_);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(p_karte);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, th);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tv);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, tkand);
    glUniform1i(glGetUniformLocation(p_karte, "uTiefe"), 0);
    glUniform1i(glGetUniformLocation(p_karte, "uVorher"), 1);
    glUniform1i(glGetUniformLocation(p_karte, "uKand"), 2);
    glUniform1f(glGetUniformLocation(p_karte, "uMindest"), float(MINDESTTIEFE / -hmin));
    glBindVertexArray(vao_leer);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo_alt);
}

void HirchAbtrag::rechnen(int bis, double anteil)
{
    if (!fbo_h || !p_sweep) return;
    int inst = bis_instanz.empty() ? 0 : bis_instanz[std::clamp(bis, 0, (int)bis_instanz.size() - 1)];
    glViewport(0, 0, W, H);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glClearDepth(1.0);          // Tiefe 1 = Oberfläche (Höhe 0)
    glClearColor(0, 0, 0, 0);   // 0 = noch nicht gefräst
    // Vorwärts (Abspielen): nur die neuen Stücke dazu – Fräsen nimmt nur weg, die Reihenfolge spielt in der
    // Höhenkarte keine Rolle. Rückwärts: neu von vorn.
    bool weiter = gez_inst >= 0 && inst >= gez_inst && vao_bereich;
    auto stueck = [&](int seite, auto &&zeichnen) {
        GLuint fbo = seite == 2 ? fbo_h2 : fbo_h, fv = seite == 2 ? fbo_v2 : fbo_v;
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        vorher_merken(fbo, fv);
        zeichnen();
        karte_uebernehmen(seite == 2 ? tex_h2 : tex_h, seite == 2 ? tex_v2 : tex_v,
                          seite == 2 ? tex_kand2 : tex_kand, seite == 2 ? fbo_k2 : fbo_k);
    };
    auto leeren = [&](int seite) {
        glBindFramebuffer(GL_FRAMEBUFFER, seite == 2 ? fbo_h2 : fbo_h);
        glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, seite == 2 ? fbo_k2 : fbo_k);
        glClear(GL_COLOR_BUFFER_BIT);
    };
    int grenze1 = zweiseitig ? n_inst1 : instanzen;
    if (weiter) {
        if (std::min(inst, grenze1) > gez_inst) stueck(1, [&] { bereich_fraesen(gez_inst, std::min(inst, grenze1)); });
        if (zweiseitig && fbo_h2 && inst > std::max(gez_inst, n_inst1))
            stueck(2, [&] { bereich_fraesen(std::max(gez_inst, n_inst1), inst); });
    } else {
        leeren(1);
        stueck(1, [&] { fraesen(vao_sweep, zweiseitig ? std::min(inst, n_inst1) : inst); });
        if (zweiseitig && fbo_h2) {
            leeren(2);
            stueck(2, [&] { fraesen(vao_sweep2, inst - n_inst1); });
        }
    }
    gez_inst = inst;
    // angefangene Bewegung bis zur aktuellen Stelle
    if (anteil > 0 && vao_teil && bis >= 0 && bis < (int)seite_der_bewegung.size() && bis + 1 < (int)bis_instanz.size() &&
        bis_instanz[bis + 1] > bis_instanz[bis] && size_t(bis_instanz[bis] + 1) * STRIDE <= inst_daten.size()) {
        float t[STRIDE];
        std::copy_n(inst_daten.begin() + size_t(bis_instanz[bis]) * STRIDE, STRIDE, t);
        for (int k = 0; k < 3; ++k) t[3 + k] = t[k] + float(anteil) * (t[3 + k] - t[k]);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_teil);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof t, t);
        bool s2 = zweiseitig && fbo_h2 && seite_der_bewegung[bis] == 2;
        stueck(s2 ? 2 : 1, [&] { fraesen(vao_teil, 1); });
    }
    zeige_instanzen = inst;
    glBindVertexArray(0);
}

bool HirchAbtrag::lesen(int seite, std::vector<float> &hoehe, std::vector<std::uint16_t> *karte)
{
    GLuint th = seite == 2 ? tex_h2 : tex_h, tk = seite == 2 ? tex_karte2 : tex_karte;
    if (!th) return false;
    std::vector<float> t(size_t(W) * H);
    glBindTexture(GL_TEXTURE_2D, th);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT, t.data());
    hoehe.resize(t.size());
    for (size_t i = 0; i < t.size(); ++i) hoehe[i] = float(hmin + t[i] * (-hmin));
    if (karte && tk) {
        karte->assign(size_t(W) * H, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glBindTexture(GL_TEXTURE_2D, tk);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_UNSIGNED_SHORT, karte->data());
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

} // namespace Inkscape::UI::Dialog
