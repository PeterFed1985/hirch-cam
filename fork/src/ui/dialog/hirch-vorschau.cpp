// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: native 3D-Vorschau (OpenGL 3.3 über GtkGLArea).
 *
 * Gleiches Verfahren wie die frühere Web-Vorschau (vorschau_gpu.js):
 *  1. Abtrag: Höhenbild als Tiefentextur (Raster 0,05 mm oder gröber). Jede Bewegung ist eine
 *     Instanz eines Rechtecks um die Bahn; der Fragment-Shader rechnet die
 *     tiefste Stelle der Fräserspitze (Schaft, V mit flacher Spitze, Kugel, konischer Kugelfräser)
 *     und schreibt sie als Tiefe, der Tiefentest LESS behält das Minimum.
 *  2. Anzeige: ein Gitter, das der Vertex-Shader aus der Textur anhebt;
 *     Licht, Holzmaserung und Abschattung in Rillen im Fragment-Shader.
 */
#include <epoxy/gl.h>
#include <glib/gstdio.h>
#include <glibmm/fileutils.h>

#include "ui/dialog/hirch-vorschau.h"
#include "ui/dialog/hirch-abtrag.h"
#include "ui/dialog/hirch-sprache.h"
#include "preferences.h"

#include <algorithm>
#include <cctype>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <set>
#include <functional>
#include <gtkmm/drawingarea.h>
#include <gtkmm/eventbox.h>

#include <giomm/appinfo.h>
#include <glibmm/convert.h>
#include <glibmm/main.h>
#include <glibmm/markup.h>
#include <glibmm/miscutils.h>
#include <cairomm/context.h>
#include <cairomm/surface.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/expander.h>
#include <gtkmm/glarea.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/overlay.h>
#include <gtkmm/scale.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stylecontext.h>

namespace Inkscape::UI::Dialog {

namespace {

// ---------------------------------------------------------------- Mathe

using M4 = std::array<float, 16>;   // spaltenweise wie OpenGL

M4 mal(M4 const &a, M4 const &b)
{
    M4 r{};
    for (int c = 0; c < 4; ++c)
        for (int z = 0; z < 4; ++z) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + z] * b[c * 4 + k];
            r[c * 4 + z] = s;
        }
    return r;
}

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

M4 perspektive(double fov_grad, double seiten, double nah, double fern)
{
    double f = 1 / std::tan(fov_grad * M_PI / 360);
    M4 m{};
    m[0] = float(f / seiten);
    m[5] = float(f);
    m[10] = float((fern + nah) / (nah - fern));
    m[11] = -1;
    m[14] = float(2 * fern * nah / (nah - fern));
    return m;
}

struct V3 {
    double x = 0, y = 0, z = 0;
};
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
V3 kreuz(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double punkt(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 norm(V3 a)
{
    double l = std::sqrt(punkt(a, a));
    return l > 0 ? a * (1 / l) : a;
}

M4 blick(V3 auge, V3 ziel, V3 oben)
{
    V3 f = norm(ziel - auge), s = norm(kreuz(f, oben)), u = kreuz(s, f);
    M4 m{};
    m[0] = float(s.x); m[4] = float(s.y); m[8] = float(s.z);
    m[1] = float(u.x); m[5] = float(u.y); m[9] = float(u.z);
    m[2] = float(-f.x); m[6] = float(-f.y); m[10] = float(-f.z);
    m[12] = float(-punkt(s, auge));
    m[13] = float(-punkt(u, auge));
    m[14] = float(punkt(f, auge));
    m[15] = 1;
    return m;
}

// ---------------------------------------------------------------- Shader
// Zweiseitig: die Gegenseite (gewendet) nimmt von unten weg. boden(uv) = Unterkante des Materials.
#define GEGENSEITE \
"uniform sampler2D uAndere;\n" \
"uniform int uMitAndere;\n" \
"uniform vec2 uSpiegel;\n" \
"uniform vec4 uRund;\n" \
"bool aussen(vec2 uv) {\n" \
"  if (uRund.z <= 0.0) return false;\n" \
"  vec2 q = (uv - uRund.xy) / uRund.zw;\n" \
"  return dot(q, q) > 1.0;\n" \
"}\n" \
"float boden(vec2 uv) {\n" \
"  if (aussen(uv)) return 0.0;\n" \
"  if (uMitAndere == 0) return -uDicke;\n" \
"  vec2 g = vec2(uSpiegel.x > 0.5 ? 1.0 - uv.x : uv.x, uSpiegel.y > 0.5 ? 1.0 - uv.y : uv.y);\n" \
"  float t = -(uHmin + textureLod(uAndere, g, 0.0).r * (-uHmin));\n" \
"  return -uDicke + max(0.0, t);\n" \
"}\n"

char const *const ANZEIGE_VS = "#version 330 core\n"
"uniform mat4 uPV;\n"
"uniform sampler2D uHoehe;\n"
"uniform float uHmin;\n"
"uniform float uDicke;\n"
GEGENSEITE
R"(
uniform vec2 uTexel;
uniform float uSchritt;
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;
out vec2 vUv;
out vec3 vWelt;
void main() {
  // Gitter gröber als das Raster: der Gitterpunkt nimmt die tiefste Stelle seiner Zelle
  // (sonst springen Punkte am Rand einer Tasche zufällig oben/unten – «Grasbüschel»)
  float t = 1.0;
  float hs = max(0.0, (uSchritt - 1.0) * 0.5);
  for (int i = -1; i <= 1; i++)
    for (int j = -1; j <= 1; j++)
      t = min(t, textureLod(uHoehe, uv + vec2(float(i), float(j)) * uTexel * hs, 0.0).r);
  float h = max(max(uHmin + t * (-uHmin), -uDicke), min(boden(uv), 0.0));
  vec4 p = vec4(position, h, 1.0);
  vUv = uv; vWelt = p.xyz;
  gl_Position = uPV * p;
})";

char const *const ANZEIGE_FS = "#version 330 core\n"
"uniform sampler2D uHoehe;\n"
"uniform float uHmin;\n"
"uniform float uDicke;\n"
GEGENSEITE
R"(
uniform vec2 uTexel;
uniform vec2 uZelle;
uniform vec3 uLicht;
uniform vec3 uAuge;
uniform int uSchatten;      // Schritte für den Schlagschatten (0 = aus)
uniform float uSchritt;     // Abstand der Gitterpunkte in Rasterzellen (feines Raster, gröberes Gitter)
uniform sampler2D uSoll;    // Soll-Tiefe der Taschen (16 Bit in Rot/Grün, Anteil von uSollMax)
uniform float uSollMax;
uniform int uRest;          // 1: wo laut Zeichnung noch Material weg müsste, rot
uniform sampler2D uWz;
uniform vec3 uWzFarbe[16];
in vec2 vUv;
in vec3 vWelt;
layout(location = 0) out vec4 farbe;
float hoehe(vec2 uv) { return max(uHmin + textureLod(uHoehe, uv, 0.0).r * (-uHmin), -uDicke); }
bool durch(vec2 uv) { float hh = hoehe(uv); return hh <= -uDicke + 0.005 || hh <= boden(uv) + 0.005; }
void main() {
  float h = hoehe(vUv);
  float hl = hoehe(vUv - vec2(uTexel.x, 0.0)), hr = hoehe(vUv + vec2(uTexel.x, 0.0));
  float hu = hoehe(vUv - vec2(0.0, uTexel.y)), ho = hoehe(vUv + vec2(0.0, uTexel.y));
  // ganz durch: offen – ausser am Rand des Lochs, dort ist die steile Fläche die Wand
  // (nur, wo auch das Gitter unten ist – sonst Löcher in der Wand zwischen zwei Gitterpunkten)
  if (vWelt.z < max(-uDicke, min(boden(vUv), 0.0)) + 0.05 &&
      durch(vUv) && durch(vUv - vec2(uTexel.x, 0.0)) && durch(vUv + vec2(uTexel.x, 0.0)) &&
      durch(vUv - vec2(0.0, uTexel.y)) && durch(vUv + vec2(0.0, uTexel.y))) discard;
  vec3 n = normalize(vec3((hl - hr) / (2.0 * uZelle.x), (hu - ho) / (2.0 * uZelle.y), 1.0));
  // Liegt das Gitter hier deutlich unter/über der Rasterzelle, ist das eine Wand zwischen
  // zwei Gitterpunkten (das Gitter ist gröber als das Raster): Licht aus der Geometrie
  bool zwischen = abs(vWelt.z - h) > 0.05;
  // Höhe für Schatten und Abdunkelung: auf einer Wand zwischen zwei Gitterpunkten die des
  // Gitters (die Rasterzelle darunter ist mal oben, mal unten – das gäbe Streifen)
  float hz = zwischen ? vWelt.z : h;
  vec3 ngeo = cross(dFdx(vWelt), dFdy(vWelt));
  bool steil = length(ngeo) > 1e-12 && abs(normalize(ngeo).z) < 0.5;   // das Gitter ist hier eine Wand
  if (zwischen || steil) {
    // Richtung der Wand aus dem geglätteten Höhenverlauf (Treppen des Gitters sind
    // kleiner als ein Bildpunkt – ihr Licht würde streifig)
    float R = 2.0 * max(1.0, uSchritt);
    vec2 g = vec2(0.0);
    for (int k = 0; k < 8; k++) {
      float w8 = float(k) * 0.7853982;
      vec2 r = vec2(cos(w8), sin(w8));
      g += r * (hoehe(vUv + r * uTexel * R) + hoehe(vUv + r * uTexel * R * 0.5));
    }
    if (length(g) > 1e-6) n = normalize(vec3(-normalize(g), 0.12));
  }
  float umg = 0.0;
  for (int i = 0; i < 8; i++) {
    float w = float(i) * 0.7853982;
    vec2 r = vec2(cos(w), sin(w));
    umg += hoehe(vUv + r * uTexel * 4.0) + hoehe(vUv + r * uTexel * 14.0);
  }
  umg /= 16.0;
  float ao = 1.0 - clamp((umg - hz) * 0.6, 0.0, 0.55);
  // Rohmaterial neutral hellgrau; gefräste Flächen in der Farbe der Bearbeitung
  // (wie im Panel; ohne Angabe die Farbe des Werkzeugs), tiefer = kräftiger
  vec3 basis = vec3(0.80, 0.80, 0.78);
  // Wände (steile Stücke zwischen zwei Rasterpunkten): Farbe der Bearbeitung, die
  // daneben tiefer gefräst hat – sonst wären gefräste Seitenwände rohmaterialgrau
  // gesucht wird bis zum nächsten Gitterpunkt (uSchritt Zellen), sonst bliebe ein Teil
  // der Wand rohmaterialgrau («Grasbüschel» an runden Wänden)
  vec2 uvf = vUv; float hf = h;
  for (int k = 0; k < 8; k++) {
    float w8 = float(k) * 0.7853982;
    for (int j = 1; j <= 2; j++) {
      float d = j == 1 ? 1.0 : max(1.0, uSchritt);
      vec2 uvn = vUv + vec2(cos(w8), sin(w8)) * uTexel * d;
      float hn = hoehe(uvn);
      if (hn < hf) { hf = hn; uvf = uvn; }
    }
  }
  bool wand = hf < h - 0.05 || zwischen;
  if (h < -0.01 || wand) {
    vec2 uvw = wand ? uvf : vUv;
    int wz = int(floor(textureLod(uWz, uvw, 0.0).r * 32.0 + 0.5)) - 1;
    vec3 f = wz >= 0 ? uWzFarbe[min(wz, 15)] : vec3(0.70);
    float tief = clamp(-min(min(h, hf), hz) / max(4.0, 0.5 * uDicke), 0.0, 1.0);
    basis = mix(mix(f, vec3(1.0), 0.30), f * 0.80, tief);
  }
  if (uRest == 1 && !wand) {
    vec4 sv = textureLod(uSoll, vUv, 0.0);
    float soll = (floor(sv.r * 255.0 + 0.5) * 256.0 + floor(sv.g * 255.0 + 0.5)) / 65535.0 * uSollMax;
    if (soll > 0.0 && h > -soll + 0.05) basis = mix(basis, vec3(0.92, 0.12, 0.10), 0.85);
  }
  float diff = max(dot(n, uLicht), 0.0);
  // Schlagschatten: vom Punkt Richtung Licht durch die Höhenkarte, bis der Strahl über
  // der Oberfläche ist (darüber steht nichts). Weich: wie knapp der Strahl vorbeigeht.
  float licht = 1.0;
  if (uSchatten > 0 && hz < -0.005 && diff > 0.0) {
    // gleiche Richtung wie das Licht, aber flacher (≈ 35°): Schatten etwa 1,4 × so lang
    // wie die Tiefe – so lesen sich auch flache Gravuren
    vec2 rxy = uLicht.xy / max(length(uLicht.xy), 1e-4);
    float steig = 0.70;                                         // mm hoch je mm seitwärts
    float weit = (-hz) / steig;                                  // bis zur Oberfläche
    vec2 mm2uv = uTexel / uZelle;
    float zelle = max(uZelle.x, uZelle.y);
    // Abstand: erst nach zwei Rasterzellen prüfen, Toleranz wächst mit der Zelle
    // (sonst wirft eine schräge Wand Schattenkörner auf sich selbst)
    float start = 2.0 * zelle;
    for (int i = 1; i <= 48; i++) {
      if (i > uSchatten) break;
      float t = start + (weit - start) * (float(i) - 0.5) / float(uSchatten);
      if (t >= weit) break;
      float strahl = hz + t * steig;
      float hs = hoehe(vUv + rxy * t * mm2uv);
      float toleranz = 1.5 * zelle * steig + 0.01;
      licht = min(licht, clamp(1.0 - (hs - strahl - toleranz) / max(0.03, 0.2 * t * steig + 0.03), 0.0, 1.0));
      if (licht <= 0.0) break;
    }
  }
  vec3 c = basis * (0.40 + 0.70 * diff * mix(0.18, 1.0, licht)) * ao;
  // leichter Glanz auf gefrästen Flächen
  if (h < -0.01 || wand) {
    vec3 v = normalize(uAuge - vWelt);
    float sp = pow(max(dot(n, normalize(uLicht + v)), 0.0), 48.0);
    c += vec3(0.10) * sp * licht;
  }
  farbe = vec4(pow(c, vec3(0.95)), 1.0);
})";

// Unterseite des Werkstücks: geschlossen, nur wo ganz durchgefräst ist offen
char const *const UNTEN_VS = "#version 330 core\n"
"uniform mat4 uPV;\n"
"uniform sampler2D uHoehe;\n"
"uniform float uHmin;\n"
"uniform float uDicke;\n"
GEGENSEITE
R"(
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;
out vec2 vUv;
void main() {
  vUv = uv;
  gl_Position = uPV * vec4(position, min(boden(uv), 0.0), 1.0);
})";

char const *const UNTEN_FS = "#version 330 core\n"
"uniform sampler2D uHoehe;\n"
"uniform float uHmin;\n"
"uniform float uDicke;\n"
GEGENSEITE
R"(
uniform sampler2D uWzAndere;
uniform vec3 uWzFarbe[16];
uniform vec2 uTexel;
in vec2 vUv;
layout(location = 0) out vec4 farbe;
void main() {
  float h = max(uHmin + textureLod(uHoehe, vUv, 0.0).r * (-uHmin), -uDicke);
  float b = boden(vUv);
  bool loch = h <= -uDicke + 0.005 || h <= b + 0.005;      // hier ist ein Loch ganz durch
  if (loch) {
    // am Rand des Lochs bleibt die steile Fläche (Wand), sonst offen
    bool rand = false;
    for (int i = 0; i < 4; i++) {
      vec2 o = i == 0 ? vec2(uTexel.x, 0.0) : i == 1 ? vec2(-uTexel.x, 0.0) : i == 2 ? vec2(0.0, uTexel.y) : vec2(0.0, -uTexel.y);
      float hn = max(uHmin + textureLod(uHoehe, vUv + o, 0.0).r * (-uHmin), -uDicke);
      if (hn > -uDicke + 0.005 && hn > boden(vUv + o) + 0.005) rand = true;
    }
    if (!rand) discard;
  }
  // Von der Gegenseite gefräst (auch die steilen Wände dazwischen): Farbe der Bearbeitung
  vec2 uvf = vUv; float bf = b;
  for (int i = 0; i < 4; i++) {
    vec2 o = i == 0 ? vec2(uTexel.x, 0.0) : i == 1 ? vec2(-uTexel.x, 0.0) : i == 2 ? vec2(0.0, uTexel.y) : vec2(0.0, -uTexel.y);
    float bn = boden(vUv + o);
    if (bn > bf) { bf = bn; uvf = vUv + o; }
  }
  vec3 c = vec3(0.55, 0.55, 0.53);                        // Rohmaterial von unten etwas dunkler
  if (uMitAndere != 0 && bf > -uDicke + 0.01) {
    vec2 g = vec2(uSpiegel.x > 0.5 ? 1.0 - uvf.x : uvf.x, uSpiegel.y > 0.5 ? 1.0 - uvf.y : uvf.y);
    int wz = int(floor(textureLod(uWzAndere, g, 0.0).r * 32.0 + 0.5)) - 1;
    c = (wz >= 0 ? uWzFarbe[min(wz, 15)] : vec3(0.70)) * 0.85;
  }
  farbe = vec4(c, 1.0);
})";

char const *const WAND_VS = "#version 330 core\n"
"uniform mat4 uPV;\n"
"uniform sampler2D uHoehe;\n"
"uniform float uHmin;\n"
"uniform float uDicke;\n"
GEGENSEITE
R"(
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 normal;
out float vLicht;
void main() {
  float b = min(boden(uv), 0.0);
  float h = max(max(uHmin + textureLod(uHoehe, uv, 0.0).r * (-uHmin), -uDicke), b);
  float z = position.z > 0.5 ? h : b;
  vLicht = 0.45 + 0.5 * max(dot(normal, normalize(vec3(-0.6, -1.0, 1.4))), 0.0);
  gl_Position = uPV * vec4(position.xy, z, 1.0);
})";

char const *const WAND_FS = R"(#version 330 core
in float vLicht;
layout(location = 0) out vec4 farbe;
void main() { farbe = vec4(vec3(0.66, 0.66, 0.64) * vLicht, 1.0); })";

// Linien und einfache Körper (Werkzeug, Tisch)
char const *const LINIE_VS = R"(#version 330 core
uniform mat4 uPV;
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;
out vec3 vFarbe;
void main() { vFarbe = color; gl_Position = uPV * vec4(position, 1.0); })";

char const *const LINIE_FS = R"(#version 330 core
uniform float uAlpha;
uniform int uEinfarbig;
uniform vec3 uFarbe;
in vec3 vFarbe;
layout(location = 0) out vec4 farbe;
void main() { farbe = vec4(uEinfarbig != 0 ? uFarbe : vFarbe, uAlpha); })";

char const *const KOERPER_VS = R"(#version 330 core
uniform mat4 uPV;
uniform vec3 uVersatz;
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
out vec3 vN;
void main() { vN = normal; gl_Position = uPV * vec4(position + uVersatz, 1.0); })";

char const *const KOERPER_FS = R"(#version 330 core
uniform vec3 uFarbe;
uniform float uAlpha;
in vec3 vN;
layout(location = 0) out vec4 farbe;
void main() {
  vec3 l = normalize(vec3(-0.6, -1.0, 1.4));
  float d = max(dot(normalize(vN), l), 0.0);
  float himmel = 0.5 + 0.5 * normalize(vN).z;
  farbe = vec4(uFarbe * (0.35 + 0.35 * himmel + 0.55 * d), uAlpha);
})";

char const *const HG_VS = R"(#version 330 core
out float vY;
void main() {
  vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
  vY = p.y * 0.5 + 0.5;
  gl_Position = vec4(p, 0.0, 1.0);
})";

char const *const HG_FS = R"(#version 330 core
in float vY;
layout(location = 0) out vec4 farbe;
void main() { farbe = vec4(mix(vec3(0.80, 0.83, 0.86), vec3(0.46, 0.52, 0.60), clamp(vY, 0.0, 1.0)), 1.0); })";

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

using Bewegung = AbtragBewegung;

std::string zahl(double v, int stellen)
{
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", stellen, v);
    std::string s = b;
    for (auto &c : s) if (c == ',') c = '.';
    if (!hirch_englisch()) for (auto &c : s) if (c == '.') c = ',';
    return s;
}

std::string markup(std::string const &s) { return Glib::Markup::escape_text(s).raw(); }

std::array<float, 3> const FARBEN[] = {{1.0f, 0.55f, 0.0f}, {0.25f, 0.66f, 1.0f}, {0.48f, 0.84f, 0.35f},
                                       {0.83f, 0.48f, 1.0f}};
char const *const FARBEN_HEX[] = {"#ff8c00", "#3fa9ff", "#7bd65a", "#d47aff"};

} // namespace

void fraeser_netz(VorschauProgramm const &P, double drehung, FraeserNetz &aus)
{
    auto &e = aus.ecken;
    e.clear();
    aus.n_schneide = aus.n_oben = 0;
    double R = std::max(0.2, P.durchmesser / 2);
    double tanh = std::tan(std::clamp(P.vwinkel, 1.0, 179.0) * M_PI / 360), sp = std::max(0.0, P.spitze) / 2;
    double er = P.form == "kugel" ? R : std::clamp(P.eckradius, 0.0, R);
    auto profil = [&](double r) {
        if (P.form == "v") return std::max(0.0, r - sp) / tanh;
        if (P.form == "konisch") {
            double rb = std::max(0.01, er), halb = std::atan(tanh), dt = rb * std::cos(halb), ht = rb - rb * std::sin(halb);
            return r <= dt ? rb - std::sqrt(std::max(0.0, rb * rb - r * r)) : ht + (r - dt) / tanh;
        }
        if (er > 0) {
            double innen = R - er, q = std::clamp(r - innen, 0.0, er);
            return r <= innen ? 0.0 : er - std::sqrt(std::max(0.0, er * er - q * q));
        }
        return 0.0;
    };
    bool mit_laengen = P.ausspann > 0;
    double h_profil = profil(R);
    double hs = P.form == "v" ? h_profil : std::max(h_profil, P.schneide > 0 ? P.schneide : 0.0);
    if (!mit_laengen) hs = std::clamp(std::max(h_profil + 2 * R, 3 * R), 3.0, 40.0);
    double Rs = P.schaft > 0 ? P.schaft / 2 : R;
    double ue = mit_laengen ? P.uebergang : 0;
    double A = mit_laengen ? std::max(P.ausspann, hs + ue + 0.5) : hs + 25;
    double G = mit_laengen && P.gesamt > A + 0.5 ? P.gesamt : 0;

    // Schneide: Profil von der Spitze bis zur Schneidenlänge, die Spannuten als Rillen im Drall (Steigung bei
    // 30° Drallwinkel π·D / tan 30°; Kompression: unten aufwärts, ab einem Drittel abwärts)
    std::vector<std::array<double, 2>> u;
    int n = P.form == "flach" && er <= 0 ? 4 : 12;
    u.push_back({0, 0});
    for (int k = 1; k <= n; ++k) {
        double r = R * k / n;
        u.push_back({r, profil(r)});
    }
    if (hs > h_profil + 1e-6) {
        int nz = std::clamp(int(std::ceil((hs - h_profil) / std::max(0.2, R * 0.35))), 1, 80);
        for (int k = 1; k <= nz; ++k) u.push_back({R, h_profil + (hs - h_profil) * k / nz});
    }
    double steigung = M_PI * 2 * R / std::tan(30 * M_PI / 180);
    double zk = hs / 3;
    auto verdreht = [&](double z) {                  // Winkel der Spannut auf der Höhe z
        double w = 2 * M_PI / steigung;
        switch (P.drall) {
        case 'L': return -w * z;
        case 'G': return 0.0;
        case 'K': return w * (z < zk ? z : 2 * zk - z);
        default: return w * z;
        }
    };
    int nut = std::max(1, P.schneiden);
    int m = std::clamp(nut * 16, 48, 96);
    auto punkt = [&](std::size_t i, int j) {
        double phi = 2 * M_PI * j / m;
        double psi = phi - drehung - verdreht(u[i][1]);
        double c = std::max(0.0, std::cos(nut * psi));
        double f = 1 - 0.22 * c * c * c * c * c * c;       // schmale Rille, breiter Rücken
        double r = u[i][0] * f;
        return std::array<double, 3>{r * std::cos(phi), r * std::sin(phi), u[i][1]};
    };
    auto dreieck = [&](std::array<double, 3> a, std::array<double, 3> b, std::array<double, 3> c) {
        double ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
        double vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
        double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        double l = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (l < 1e-12) return;
        nx /= l; ny /= l; nz /= l;
        for (auto const *q : {&a, &b, &c})
            e.insert(e.end(), {float((*q)[0]), float((*q)[1]), float((*q)[2]), float(nx), float(ny), float(nz)});
    };
    for (std::size_t i = 1; i < u.size(); ++i)
        for (int j = 0; j < m; ++j) {
            auto p00 = punkt(i - 1, j), p01 = punkt(i - 1, j + 1), p10 = punkt(i, j), p11 = punkt(i, j + 1);
            // Profil nach aussen/oben, Winkel gegen den Uhrzeigersinn: so zeigen die Normalen nach aussen
            dreieck(p00, p11, p10);
            dreieck(p00, p01, p11);
        }
    aus.n_schneide = mit_laengen ? int(e.size() / 6) : 0;

    // Übergang und Schaft (glatt), mit Gesamtlänge der Teil in der Spannzange extra
    int ms = 36;
    auto drehen = [&](std::vector<std::array<double, 2>> const &v) {
        for (std::size_t k = 1; k < v.size(); ++k) {
            double r0 = v[k - 1][0], z0 = v[k - 1][1], r1 = v[k][0], z1 = v[k][1];
            double tr = r1 - r0, tz = z1 - z0, l = std::hypot(tr, tz);
            if (l < 1e-9) continue;
            double nr = tz / l, nz = -tr / l;
            for (int q = 0; q < ms; ++q) {
                double a0 = 2 * M_PI * q / ms, a1 = 2 * M_PI * (q + 1) / ms;
                auto ecke = [&](double r, double z, double w) {
                    e.insert(e.end(), {float(r * std::cos(w)), float(r * std::sin(w)), float(z),
                                       float(nr * std::cos(w)), float(nr * std::sin(w)), float(nz)});
                };
                ecke(r0, z0, a0);
                ecke(r1, z1, a1);
                ecke(r1, z1, a0);
                ecke(r0, z0, a0);
                ecke(r0, z0, a1);
                ecke(r1, z1, a1);
            }
        }
    };
    std::vector<std::array<double, 2>> u2{{R, hs}};
    if (std::abs(Rs - R) > 1e-6) u2.push_back({Rs, hs + ue});
    u2.push_back({Rs, A});
    if (G <= 0) u2.push_back({0, A});
    drehen(u2);
    if (G > 0) {
        std::size_t vor = e.size() / 6;
        drehen({{Rs, A}, {Rs, G}, {0, G}});
        aus.n_oben = int(e.size() / 6 - vor);
    }
}

void fraeser_pfeil(double r, double z, double dx, double dy, std::vector<float> &aus)
{
    double l = std::hypot(dx, dy);
    if (l < 1e-9) return;
    dx /= l;
    dy /= l;
    double qx = -dy, qy = dx;                       // quer
    double a0 = r + 1.0, lang = std::max(4.0, 2.5 * r), b = std::max(0.6, 0.35 * r), spitze = std::max(1.6, 0.9 * r);
    auto pt = [&](double s, double q) { return std::array<double, 2>{dx * s + qx * q, dy * s + qy * q}; };
    auto drei = [&](std::array<double, 2> p, std::array<double, 2> q, std::array<double, 2> t) {
        for (auto const &v : {p, q, t}) aus.insert(aus.end(), {float(v[0]), float(v[1]), float(z), 0.f, 0.f, 1.f});
    };
    double s1 = a0 + lang - spitze;
    drei(pt(a0, -b / 2), pt(s1, -b / 2), pt(s1, b / 2));
    drei(pt(a0, -b / 2), pt(s1, b / 2), pt(a0, b / 2));
    drei(pt(s1, -spitze * 0.6), pt(a0 + lang, 0), pt(s1, spitze * 0.6));
}


// ============================================================================
struct HirchVorschau::Impl {
    HirchVorschau &fenster;
    VorschauDaten d;
    std::vector<Bewegung> moves;
    std::vector<Bewegung> alle;                                  // alle Bewegungen (moves: Auswahl daraus)
    Gtk::ComboBoxText grafik_wahl;                               // Windows: Grafikkarte / Software
    std::vector<std::pair<int, std::string>> wege_schluessel;    // je Eintrag: Datei, Name der Bearbeitung
    std::vector<Gtk::CheckButton *> wege_haken;                  // je Eintrag ein Häkchen in der Liste
    Gtk::Expander bericht_klapp;                                 // Bericht ganz unten, zugeklappt
    bool wege_lade = false;
    std::vector<std::array<float, 3>> tafel;   // Farben (Index = Bewegung::farbe), höchstens 16
    HirchAbtrag abtrag;                    // der Rechenweg: Höhenbilder und Karte (hirch-abtrag.h)
    std::vector<AbtragBearbeitung> bearbeitungen;   // Index → Bearbeitung (Karte «zuletzt tiefer gemacht von»)
    double x0 = 0, y0 = 0, w = 100, h = 100, zelle = 0.1, hmin = -28;
    int W = 2, H = 2;

    // GL
    Gtk::GLArea bereich;
    bool gl_ok = false, neu_aufbauen = false, hoehe_neu = true, software = false;
    std::string gl_fehler;
    GLuint p_unten = 0;
    GLuint p_anz = 0, p_wand = 0, p_linie = 0, p_koerper = 0, p_hg = 0, vao_leer = 0;
    GLuint tex_soll = 0, tex_soll2 = 0;                // Soll-Tiefe der Taschen (Restmaterial zeigen)
    double gitter_schritt = 1;                     // Gitterabstand in Rasterzellen                             // so viele Instanzen stehen schon in der Höhenkarte
    // Darstellung: Schatten in Stufen (2 voll, 1 grob, 0 aus); beim Drehen/Abspielen eine Stufe
    // weniger; zu langsame Bilder schalten selbst zurück
    int stufe_ruhe = 2, stufe_bewegt = 1;
    bool bewegt = false;
    sigc::connection ruhe_takt;
    GLsizei n_vor1 = 0, n_eil1 = 0;                    // Linien auf Seite 1 (davor)
    GLsizei spann_start2 = 0, spann_anzahl2 = 0;
    int anzeige_wahl = 0;                              // 0 = Seite, die gerade gefräst wird; 1/2 fest
    Gtk::Box seiten_knoepfe{Gtk::ORIENTATION_HORIZONTAL, 6};
    GLuint vao_gitter = 0, vbo_gitter = 0, ibo_gitter = 0;
    GLsizei n_gitter = 0;
    GLuint vao_wand = 0, vbo_wand = 0, ibo_wand = 0;
    GLsizei n_wand = 0;
    GLuint vao_vor = 0, vbo_vor = 0, vao_eil = 0, vbo_eil = 0;
    GLsizei n_vor = 0, n_eil = 0;
    GLuint vao_wz = 0, vbo_wz = 0, vao_tisch = 0, vbo_tisch = 0;
    GLsizei n_wz_schneide = 0, n_wz_oben = 0;     // Werkzeug: zuerst die Schneide, dann Schaft, Schaft in der Zange,
    GLsizei n_wz_pfeil = 0;                       // zuletzt der Pfeil in Fahrrichtung
    double pfeil_dx = 0, pfeil_dy = 0, pfeil_z = 0;
    GLsizei n_wz = 0, n_tisch = 0, achse_start[3] = {0, 0, 0}, achse_anzahl[3] = {0, 0, 0};
    GLsizei spann_start = 0, spann_anzahl = 0;
    struct GewindeTeil { GLsizei start = 0, anzahl = 0; int seite = 1, programm = -1; std::array<float, 3> farbe{0.f, 0.67f, 0.75f}; };
    std::vector<GewindeTeil> gewinde_teile;        // Formteile Gewinde im Puffer vao_tisch
    GLuint fbo_ms = 0, rb_farbe = 0, rb_tiefe = 0;
    int ms_w = 0, ms_h = 0, proben = 0;
    int werkzeug_prog = -1;

    // Kamera
    V3 ziel;
    double azimut = -1.27, hoehenwinkel = 0.65, abstand = 500;
    double maus_x = 0, maus_y = 0;
    int maus_knopf = 0;

    // Bedienung
    Gtk::Box haupt{Gtk::ORIENTATION_HORIZONTAL, 0};
    Gtk::ScrolledWindow seiten_scroll;
    Gtk::Box seite{Gtk::ORIENTATION_VERTICAL, 14};
    Gtk::Overlay ueber;
    Gtk::Label status, meldung, warn, dateien_titel;
    Gtk::Button ab_knopf;
    Gtk::Label ab_hinweis;
    Gtk::Box dateien{Gtk::ORIENTATION_VERTICAL, 0}, bericht_box{Gtk::ORIENTATION_VERTICAL, 3};
    // Ablauf: links ein senkrechter Fortschrittsbalken, daneben die Werkzeuge und Bearbeitungen.
    // Jede Bearbeitung ist so hoch, wie sie Zeit braucht; ist eine viel länger als die anderen,
    // wird sie in der Höhe gestaucht und der Balken legt dort seine Länge als Schlangenlinie hinein.
    struct Band {
        bool kopf = false;
        int p = 0;
        double t0 = 0, t1 = 0;          // Minuten (Laufzeit über alle Bewegungen)
        int y = 0, h = 0;               // Lage in der Spalte
        double l0 = 0, l1 = 0;          // Lage auf dem Balken (Länge entlang der Linie)
        Gtk::Widget *zeile = nullptr;
        Gtk::Label *zeit = nullptr;
        int zustand = -1;               // 0 offen, 1 läuft, 2 fertig
    };
    struct BPunkt { double x, y, l; };
    std::vector<Band> baender;
    std::vector<BPunkt> bpfad;
    std::vector<double> zeit_alle;      // wie zeit_bis, aber für «alle»
    static constexpr int KNOPF_RAND = 8;  // Platz für den Knopf über und unter der Linie
    Gtk::Box ablauf{Gtk::ORIENTATION_HORIZONTAL, 8};
    Gtk::DrawingArea balken;
    bool balken_zieht = false;
    Gtk::Scale fort{Gtk::ORIENTATION_HORIZONTAL};
    Gtk::Button play{TR("▶ Abspielen")}, ende{TR("Fertig zeigen")};
    Gtk::CheckButton zeig_bahn{TR("Fräsbahnen")}, zeig_eil{TR("Eilgänge (rot)")}, zeig_mat{"Material"},
        zeig_null{TR("Nullpunkt (X rot, Y grün, Z blau)")}, zeig_schatten{TR("Schatten")},
        zeig_rest{TR("Restmaterial rot zeigen")}, zeig_drehen{TR("Fräser dreht sich")};
    double drehung = 0;              // Fräser: Winkel um die Achse (dreht im Uhrzeigersinn von oben, M3)
    sigc::connection dreh_takt;
    bool dreh_offen = false;         // ein Bild für die Drehung ist bestellt, noch nicht gezeichnet
    gint64 dreh_zuletzt = 0;

    /// Nächster Schritt der Drehung: erst nach dem letzten Bild, mit niedriger Priorität (wie im Hauptfenster)
    void dreh_planen(int ms)
    {
        if (dreh_takt.connected()) return;
        dreh_takt = Glib::signal_timeout().connect([this] {
            dreh_takt.disconnect();
            Bewegung wo;
            if (!zeig_drehen.get_active() || !fenster.get_visible() || !bereich.get_realized() || !werkzeug_ort(wo)) {
                dreh_zuletzt = 0;
                dreh_planen(300);
                return false;
            }
            auto jetzt = g_get_monotonic_time();
            double dt = dreh_zuletzt ? std::min(0.25, (jetzt - dreh_zuletzt) / 1e6) : 0.04;
            dreh_zuletzt = jetzt;
            drehung = std::fmod(drehung - 2 * M_PI * dt, 2 * M_PI);
            werkzeug_prog = -1;
            dreh_offen = true;
            bereich.queue_render();
            return false;
        }, ms, Glib::PRIORITY_LOW);
    }
    Gtk::Grid zahlen;
    bool spielt = false, setzt = false, start_ansicht = true;
    sigc::connection takt;
    // Abspielgeschwindigkeit: Faktor zur echten Laufzeit (Regler logarithmisch ½× … 1000×);
    // «Übersicht» spielt alles in etwa 15 s ab
    Gtk::Scale tempo{Gtk::ORIENTATION_HORIZONTAL};
    Gtk::CheckButton uebersicht{TR("Übersicht – alles in ≈ 15 s abspielen")};
    Gtk::Label tempo_text, rest_text;
    double anteil = 0;                 // angefangene Bewegung moves[bis]: so weit (0…1)
    std::vector<double> zeit_bis;      // Minuten bis Ende jeder Bewegung (wie die Laufzeit)
    double spiel_min = 0;
    gint64 spiel_uhr = 0;
    int bis = 0;

    ~Impl() { dreh_takt.disconnect(); }

    explicit Impl(HirchVorschau &f)
        : fenster(f)
    {
        auto css = Gtk::CssProvider::create();
        css->load_from_data(R"(
.hv-seite, .hv-seite viewport, .hv-hinten { background: #24272c; color: #e8e6e1; }
.hv-seite > box { padding: 16px; }
.hv-seite button { background: #30343a; color: #e8e6e1; border: 1px solid #454a51; border-radius: 6px;
  box-shadow: none; text-shadow: none; padding: 4px 10px; }
.hv-seite button:hover { border-color: #ff8c00; }
.hv-seite button label { color: #e8e6e1; }
.hv-seite button:disabled { background: #2a2d32; border-color: #383c42; }
.hv-seite button:disabled label { color: #6b6964; }
.hv-seite check { background: #30343a; border-color: #454a51; }
.hv-seite scale trough { background: #454a51; }
.hv-seite scale highlight { background: #ff8c00; }
.hv-seite label { color: #e8e6e1; }
.hv-kopf { font-size: 0.82em; font-weight: bold; letter-spacing: 1px; color: #a09d96; }
.hv-dim { color: #a09d96; font-size: 0.92em; }
.hv-warn { color: #ffcf8a; }
.hv-schlecht { color: #ffb3a6; }
.hv-status { background: rgba(0,0,0,0.45); color: #c9c6bf; padding: 4px 8px; border-radius: 4px; font-size: 0.9em; }
.hv-meldung { color: #ccc; font-size: 1.1em; padding: 40px; }
.hv-gl { background: #9aa3ae; }
.hv-band { border-radius: 6px; padding: 0 6px; }
.hv-band-op { background: rgba(255,255,255,0.03); border-bottom: 1px solid #24272c; }
.hv-band-an { background: rgba(255,140,0,0.22); }
.hv-band-an label { font-weight: bold; color: #ffffff; }
.hv-band-fertig label { color: #8a8f97; }
.hv-band-zeit { font-family: monospace, "Segoe UI Symbol"; font-size: 0.9em; color: #a09d96; }
)");
        Gtk::StyleContext::add_provider_for_screen(Gdk::Screen::get_default(), css,
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

        seite.set_margin_start(16);
        seite.set_margin_end(16);
        seite.set_margin_top(16);
        seite.set_margin_bottom(16);
        seiten_scroll.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
        seiten_scroll.set_size_request(330, -1);
        seiten_scroll.set_hexpand(false);
        seiten_scroll.set_propagate_natural_width(true);
        haupt.get_style_context()->add_class("hv-hinten");
        seiten_scroll.add(seite);
        seiten_scroll.get_style_context()->add_class("hv-seite");
        haupt.pack_start(seiten_scroll, false, false);
        haupt.pack_start(ueber, true, true);
        fenster.add(haupt);

        auto kopf = [](char const *t) {
            auto l = Gtk::make_managed<Gtk::Label>(t);
            l->set_xalign(0);
            l->get_style_context()->add_class("hv-kopf");
            return l;
        };
        auto abschnitt = [&](Gtk::Widget *kopfzeile, std::initializer_list<Gtk::Widget *> teile) {
            auto b = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
            b->add(*kopfzeile);
            for (auto t : teile) b->add(*t);
            seite.add(*b);
            return b;
        };
        dateien_titel.set_xalign(0);
        dateien_titel.get_style_context()->add_class("hv-kopf");
        balken.set_size_request(30, -1);
        balken.add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::BUTTON1_MOTION_MASK);
        balken.set_tooltip_text(TR("Fortschritt – ziehen, um an eine Stelle zu springen"));
        balken.signal_draw().connect([this](Cairo::RefPtr<Cairo::Context> const &cr) { return balken_malen(cr); });
        balken.signal_button_press_event().connect([this](GdkEventButton *e) {
            if (e->button != 1) return false;
            balken_zieht = true;
            springen_y(e->y);
            return true;
        });
        balken.signal_motion_notify_event().connect([this](GdkEventMotion *e) {
            if (balken_zieht) springen_y(e->y);
            return balken_zieht;
        });
        balken.signal_button_release_event().connect([this](GdkEventButton *) {
            balken_zieht = false;
            return true;
        });
        dateien.set_hexpand(true);
        dateien.set_margin_top(KNOPF_RAND);
        dateien.set_valign(Gtk::ALIGN_START);
        balken.set_valign(Gtk::ALIGN_START);
        ablauf.add(balken);
        ablauf.add(dateien);
        abschnitt(&dateien_titel, {&ablauf});

        fort.set_range(0, 1000);
        fort.set_value(1000);
        fort.set_draw_value(false);
        auto knoepfe = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
        knoepfe->add(play);
        knoepfe->add(ende);
        // Tempo: Regler 0…1 → Faktor ½× … 1000× (logarithmisch), Echtzeit rastet ein
        tempo.set_range(0, 1);
        tempo.set_increments(0.01, 0.1);
        tempo.set_draw_value(false);
        tempo.set_hexpand(true);
        tempo.set_round_digits(-1);
        tempo.add_mark(regler_von(1), Gtk::POS_BOTTOM, "");
        tempo.add_mark(regler_von(10), Gtk::POS_BOTTOM, "");
        tempo.add_mark(regler_von(100), Gtk::POS_BOTTOM, "");
        tempo.set_tooltip_text(TR("Abspielgeschwindigkeit von ½× bis 1000×. Echtzeit (1×) fährt mit den echten "
                               "Vorschüben – so lange wie die Laufzeit unten. Echtzeit rastet ein."));
        uebersicht.set_tooltip_text(TR("Übersicht: alles in etwa 15 Sekunden abspielen, egal wie lange es dauert"));
        {
            auto prefs = Inkscape::Preferences::get();
            double f = prefs->getDouble("/hirch/vorschau_faktor", 50.0);
            tempo.set_value(regler_von(std::clamp(f, 0.5, 1000.0)));
            uebersicht.set_active(prefs->getBool("/hirch/vorschau_uebersicht", true));
        }
        tempo.signal_value_changed().connect([this] {
            double t = tempo.get_value();
            double echt = regler_von(1);
            if (std::abs(t - echt) < 0.025 && std::abs(t - echt) > 1e-9) {
                tempo.set_value(echt);       // einrasten
                return;
            }
            Inkscape::Preferences::get()->setDouble("/hirch/vorschau_faktor", faktor());
            if (uebersicht.get_active()) uebersicht.set_active(false);   // Regler bewegt: echtes Tempo
            tempo_neu_ansetzen();
            tempo_anzeigen();
        });
        uebersicht.signal_toggled().connect([this] {
            Inkscape::Preferences::get()->setBool("/hirch/vorschau_uebersicht", uebersicht.get_active());
            tempo_neu_ansetzen();
            tempo_anzeigen();
        });
        auto tempo_zeile = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
        auto tkopf = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        auto tl = Gtk::make_managed<Gtk::Label>(TR("Tempo"));
        tl->get_style_context()->add_class("hv-dim");
        tkopf->add(*tl);
        tempo_text.set_hexpand(true);
        tempo_text.set_xalign(1);
        tkopf->add(tempo_text);
        tempo_zeile->add(*tkopf);
        tempo_zeile->add(tempo);
        tempo_zeile->add(uebersicht);
        rest_text.set_xalign(0);
        rest_text.get_style_context()->add_class("hv-dim");
        ab_knopf.set_label(TR("Ab hier neu ausgeben …"));
        ab_knopf.set_halign(Gtk::ALIGN_START);
        ab_knopf.set_tooltip_text(TR("Nach einem Abbruch: das Programm ab dieser Stelle neu ausgeben. Den Fortschritt "
                                  "neben dem Ablauf dorthin ziehen, wo die Maschine stehen blieb – es geht über die "
                                  "Sicherheitshöhe zum Punkt davor und fräst von da an weiter."));
        ab_knopf.set_sensitive(false);
        ab_knopf.signal_clicked().connect([this] {
            if (bis <= 0 || bis > (int)moves.size() || !fenster.ab_hier) return;
            auto const &m = moves[bis - 1];
            fenster.ab_hier(m.p + 1, m.zeile);
        });
        // B-27 (Peter 06.11.: «passt»): klein unter dem Knopf
        ab_hinweis.set_text(TR("Gibt das Programm ab der gewählten Stelle aus – was davor liegt, muss schon gefräst "
                               "sein."));
        ab_hinweis.set_xalign(0);
        ab_hinweis.set_line_wrap(true);
        ab_hinweis.set_max_width_chars(34);
        ab_hinweis.get_style_context()->add_class("hv-dim");
        abschnitt(kopf(TR("ABSPIELEN")), {knoepfe, tempo_zeile, &rest_text, &ab_knopf, &ab_hinweis});
        fort.set_round_digits(-1);
        fort.set_digits(4);
        tempo_anzeigen();

        zeig_bahn.set_active(true);
        zeig_eil.set_active(true);
        zeig_mat.set_active(true);
        zeig_null.set_active(true);
        auto ansichten = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
        for (auto [t, art] : {std::pair{TR("Schräg"), 0}, {TR("Von oben"), 1}, {TR("Von vorne"), 2}}) {
            auto b = Gtk::make_managed<Gtk::Button>(t);
            int a = art;
            b->signal_clicked().connect([this, a] { ansicht(a); });
            ansichten->add(*b);
        }
#ifdef _WIN32
        // Windows: Grafikkarte oder Software-Darstellung (Mesa) – gilt ab dem nächsten Start
        {
            auto ordner = Glib::build_filename(g_getenv("APPDATA") ? g_getenv("APPDATA") : Glib::get_home_dir().c_str(),
                                               "hirch-cam");
            std::string wahl = "auto";
            try {
                wahl = Glib::file_get_contents(Glib::build_filename(ordner, "grafik.txt"));
            } catch (...) {
            }
            wahl = wahl.substr(0, wahl.find_first_of(" \t\r\n"));
            if (wahl != "karte" && wahl != "software") wahl = "auto";
            auto jetzt = Glib::getenv("HIRCH_GRAFIK");
            grafik_wahl.append("auto", jetzt == "software" ? TR("Grafik: automatisch (Software)")
                                                          : TR("Grafik: automatisch (Grafikkarte)"));
            grafik_wahl.append("karte", TR("Grafik: Grafikkarte"));
            grafik_wahl.append("software", TR("Grafik: Software"));
            grafik_wahl.set_active_id(wahl);
            grafik_wahl.set_tooltip_text(TR("Ältere Grafikkarten können die 3D-Ansicht nicht. Die Software-Darstellung "
                                         "rechnet alles mit dem Prozessor. Gilt ab dem nächsten Start."));
            grafik_wahl.signal_changed().connect([this, ordner] {
                auto id = grafik_wahl.get_active_id().raw();
                try {
                    g_mkdir_with_parents(ordner.c_str(), 0755);
                    Glib::file_set_contents(Glib::build_filename(ordner, "grafik.txt"), id + "\n");
                    if (id == "auto") g_remove(Glib::build_filename(ordner, "grafik-test.txt").c_str());
                } catch (...) {
                }
                meldung.set_text(TR("Die Darstellung wechselt beim nächsten Start von Hirch CAM."));
                meldung.show();
            });
            abschnitt(kopf(TR("ANZEIGE")), {&zeig_bahn, &zeig_eil, &zeig_mat, &zeig_schatten, &zeig_rest, &zeig_null, &zeig_drehen, ansichten, &grafik_wahl});
        }
#else
        abschnitt(kopf(TR("ANZEIGE")), {&zeig_bahn, &zeig_eil, &zeig_mat, &zeig_schatten, &zeig_rest, &zeig_null, &zeig_drehen, ansichten});
#endif
        // Zweiseitig: Seite 1 oder Seite 2 nach oben drehen (sonst die, die gerade gefräst wird)
        for (int sn : {1, 2}) {
            auto b = Gtk::make_managed<Gtk::Button>(sn == 1 ? TR("Seite 1 oben") : TR("Seite 2 oben"));
            b->set_tooltip_text(sn == 1 ? TR("Werkstück wie vor dem Wenden zeigen") : TR("Werkstück gewendet zeigen"));
            b->signal_clicked().connect([this, sn] {
                anzeige_wahl = sn;
                status_setzen();
                bereich.queue_render();
            });
            seiten_knoepfe.add(*b);
        }
        seiten_knoepfe.set_no_show_all(true);
        seite.add(seiten_knoepfe);

        zahlen.set_column_spacing(12);
        zahlen.set_row_spacing(3);
        warn.set_xalign(0);
        warn.set_line_wrap(true);
        warn.set_max_width_chars(36);
        warn.get_style_context()->add_class("hv-schlecht");
        abschnitt(kopf(TR("KENNZAHLEN")), {&zahlen, &warn});

        auto hinweis = Gtk::make_managed<Gtk::Label>(
            TR("Maus: links drehen, rechts verschieben, Rad zoomen. Die Simulation zeigt, was die Werkzeuge "
            "abtragen – auch Eilgänge, die versehentlich ins Material fahren."));
        hinweis->set_line_wrap(true);
        hinweis->set_max_width_chars(36);
        hinweis->set_xalign(0);
        hinweis->get_style_context()->add_class("hv-dim");
        seite.add(*hinweis);
        // Bericht: ganz unten, zugeklappt (selten gebraucht)
        bericht_klapp.set_expanded(false);
        bericht_klapp.add(bericht_box);
        bericht_box.set_margin_top(6);
        seite.add(bericht_klapp);

        // Zeichenfläche
        bereich.set_required_version(3, 3);
        bereich.set_has_depth_buffer(true);
        bereich.set_auto_render(true);
        bereich.get_style_context()->add_class("hv-gl");
        bereich.add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::POINTER_MOTION_MASK |
                           Gdk::SCROLL_MASK | Gdk::SMOOTH_SCROLL_MASK);
        ueber.add(bereich);
        ueber.set_hexpand(true);
        status.set_halign(Gtk::ALIGN_START);
        status.set_valign(Gtk::ALIGN_END);
        status.set_margin_start(12);
        status.set_margin_bottom(10);
        status.get_style_context()->add_class("hv-status");
        ueber.add_overlay(status);
        meldung.set_line_wrap(true);
        meldung.set_halign(Gtk::ALIGN_CENTER);
        meldung.set_valign(Gtk::ALIGN_CENTER);
        meldung.get_style_context()->add_class("hv-meldung");
        meldung.set_no_show_all(true);
        ueber.add_overlay(meldung);

        bereich.signal_realize().connect([this] { gl_start(); });
        bereich.signal_unrealize().connect([this] { gl_ende(); }, false);
        bereich.signal_render().connect([this](Glib::RefPtr<Gdk::GLContext> const &) {
            // Bildzeit messen: zu langsam → Schatten eine Stufe zurück (beim Drehen/Abspielen
            // ab ~60 ms, im Stillstand ab ~250 ms)
            gint64 t0 = g_get_monotonic_time();
            bool r = zeichnen();
            if (dreh_offen) {                    // Bild gezeichnet: nächster Schritt der Drehung
                dreh_offen = false;
                dreh_planen(40);
            }
            static bool fest = std::getenv("HIRCH_VORSCHAU_VOLL") != nullptr;   // Tests: nie zurückschalten
            if (gl_ok && zeig_schatten.get_active() && !fest) {
                glFinish();
                double ms = (g_get_monotonic_time() - t0) / 1000.0;
                if ((bewegt || spielt) && ms > 60 && stufe_bewegt > 0) --stufe_bewegt;
                else if (!bewegt && !spielt && ms > 250 && stufe_ruhe > 0) {
                    --stufe_ruhe;
                    Glib::signal_idle().connect_once([this] { bereich.queue_render(); });
                }
            }
            return r;
        }, false);
        bereich.signal_resize().connect([this](int, int) {
            if (start_ansicht) ansicht(0);
        });
        bereich.signal_button_press_event().connect([this](GdkEventButton *e) {
            start_ansicht = false;
            maus_knopf = (int)e->button;
            maus_x = e->x;
            maus_y = e->y;
            return true;
        });
        bereich.signal_button_release_event().connect([this](GdkEventButton *) {
            maus_knopf = 0;
            return true;
        });
        bereich.signal_motion_notify_event().connect([this](GdkEventMotion *e) {
            if (maus_knopf) bewegung_melden();
            double dx = e->x - maus_x, dy = e->y - maus_y;
            maus_x = e->x;
            maus_y = e->y;
            if (maus_knopf == 1) {
                azimut -= dx * 0.006;
                hoehenwinkel = std::clamp(hoehenwinkel + dy * 0.006, -1.5607, 1.5607);
                bereich.queue_render();
            } else if (maus_knopf == 2 || maus_knopf == 3) {
                double s = abstand * 0.0018;
                V3 vorne{std::cos(hoehenwinkel) * std::cos(azimut), std::cos(hoehenwinkel) * std::sin(azimut),
                         std::sin(hoehenwinkel)};
                V3 rechts = norm(kreuz(vorne * -1, V3{0, 0, 1}));
                V3 oben = kreuz(rechts, vorne * -1);
                ziel = ziel + rechts * (-dx * s) + oben * (dy * s);
                bereich.queue_render();
            }
            return true;
        });
        bereich.signal_scroll_event().connect([this](GdkEventScroll *e) {
            start_ansicht = false;
            double f = 1.0;
            if (e->direction == GDK_SCROLL_UP) f = 0.88;
            else if (e->direction == GDK_SCROLL_DOWN) f = 1.0 / 0.88;
            else if (e->direction == GDK_SCROLL_SMOOTH) f = std::pow(0.88, -e->delta_y);
            abstand = std::clamp(abstand * f, 2.0, 20000.0);
            bewegung_melden();
            bereich.queue_render();
            return true;
        });

        fort.signal_value_changed().connect([this] {
            if (setzt) return;
            anzeige_wahl = 0;
            stopp();
            fortschritt(fort.get_value());
        });
        play.signal_clicked().connect([this] {
            spielt = !spielt;
            play.set_label(spielt ? TR("❚❚ Anhalten") : TR("▶ Abspielen"));
            if (spielt && fort.get_value() >= 1000) setze_fort(0);
            takt.disconnect();
            if (spielt) {
                if (zeit_bis.size() != moves.size()) zeiten_rechnen();
                tempo_neu_ansetzen();
                takt = Glib::signal_timeout().connect([this] {
                    double v;
                    gint64 jetzt = g_get_monotonic_time();
                    double dt = (jetzt - spiel_uhr) / 1e6;     // Sekunden seit dem letzten Takt
                    spiel_uhr = jetzt;
                    if (uebersicht.get_active() || zeit_bis.empty() || zeit_bis.back() <= 0) {
                        // Übersicht: alles in etwa 15 s
                        v = std::min(1000.0, fort.get_value() + 1000.0 * dt / 15.0);
                    } else {
                        spiel_min += dt / 60.0 * faktor();
                        v = std::min(1000.0, 1000.0 * stelle_von_zeit(spiel_min) / std::max<size_t>(1, moves.size()));
                    }
                    rest_anzeigen();
                    setze_fort(v);
                    if (v >= 1000) {
                        stopp();
                        return false;
                    }
                    return true;
                }, 30);
            }
        });
        ende.signal_clicked().connect([this] {
            stopp();
            setze_fort(1000);
        });
        for (auto c : {&zeig_bahn, &zeig_eil, &zeig_mat, &zeig_null}) c->signal_toggled().connect([this] { bereich.queue_render(); });
        // Fräser dreht sich (Peter 26.10.): eine Umdrehung je Sekunde, nur solange er zu sehen ist; die Spannuten
        // zeigen Drehrichtung und Drall. Abschaltbar für schwache Grafikkarten.
        zeig_drehen.set_tooltip_text(TR("Langsam, nur um die Drehrichtung zu zeigen. Ausschalten, wenn die Ansicht "
                                        "dadurch zu langsam wird."));
        zeig_drehen.set_active(Inkscape::Preferences::get()->getBool("/hirch/fraeser_drehen", true));
        zeig_drehen.signal_toggled().connect([this] {
            Inkscape::Preferences::get()->setBool("/hirch/fraeser_drehen", zeig_drehen.get_active());
            bereich.queue_render();
        });
        dreh_planen(40);
        zeig_rest.set_tooltip_text(TR("Am Ende: rot, wo laut Zeichnung noch Material weg müsste – z. B. Ecken einer "
                                   "Tasche, die der Fräser nicht erreicht."));
        zeig_rest.set_active(Inkscape::Preferences::get()->getBool("/hirch/vorschau_rest", true));
        zeig_rest.signal_toggled().connect([this] {
            Inkscape::Preferences::get()->setBool("/hirch/vorschau_rest", zeig_rest.get_active());
            bereich.queue_render();
        });
        zeig_schatten.set_tooltip_text(TR("Schlagschatten in Taschen und Gravuren. Wird die Ansicht zu langsam, "
                                       "schaltet die Vorschau selbst zurück."));
        zeig_schatten.set_active(Inkscape::Preferences::get()->getBool("/hirch/vorschau_schatten", true));
        zeig_schatten.signal_toggled().connect([this] {
            Inkscape::Preferences::get()->setBool("/hirch/vorschau_schatten", zeig_schatten.get_active());
            stufe_ruhe = 2;
            stufe_bewegt = 1;
            bereich.queue_render();
        });
        haupt.show_all();
    }

    int seite_von(Bewegung const &m) const
    {
        return d.zweiseitig && m.p >= 0 && m.p < (int)d.programme.size() && d.programme[m.p].seite == 2 ? 2 : 1;
    }

    /// Welche Seite liegt oben? Die, die gerade gefräst wird – oder die gewählte
    int oben_seite() const
    {
        if (!d.zweiseitig) return 1;
        if (anzeige_wahl) return anzeige_wahl;
        if (moves.empty()) return 1;
        return seite_von(moves[std::max(0, bis - 1)]);
    }

    /// Zeit bis zum Ende jeder Bewegung – wie die Laufzeit in den Kennzahlen
    std::vector<double> zeiten_von(std::vector<Bewegung> const &mv) const
    {
        std::vector<double> z(mv.size(), 0.0);
        Bewegung p;
        p.z = d.sicherheit;
        int prog = -1;
        double t = 0;
        for (std::size_t i = 0; i < mv.size(); ++i) {
            auto const &m = mv[i];
            if (m.p != prog) {
                p = Bewegung{};
                p.z = d.sicherheit;
                prog = m.p;
            }
            double dd = std::sqrt((m.x - p.x) * (m.x - p.x) + (m.y - p.y) * (m.y - p.y) + (m.z - p.z) * (m.z - p.z));
            t += m.eil ? dd / 3000 : (m.f > 0 ? dd / m.f : 0);
            z[i] = t;
            p = m;
        }
        return z;
    }

    void zeiten_rechnen() { zeit_bis = zeiten_von(moves); }

    // ------------------------------------------------------------ Ablauf mit Fortschrittsbalken
    static std::string m_ss(double minuten)
    {
        int s = std::max(0, (int)std::lround(minuten * 60));
        char b[24];
        if (s >= 3600) std::snprintf(b, sizeof b, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
        else std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
        return b;
    }

    /// Zeilen bauen: je Werkzeug ein Kopf (Häkchen für alle seine Wege), darunter jede
    /// Bearbeitung in der Reihenfolge, in der sie gefräst wird
    void ablauf_bauen(bool gemeinsam, std::function<std::string(int)> const &hex_von,
                      std::vector<std::vector<int>> const &abschnitt_farbe)
    {
        baender.clear();
        wege_schluessel.clear();
        wege_haken.clear();
        int n = (int)d.programme.size();
        std::vector<std::vector<Gtk::CheckButton *>> eigene(n);
        std::vector<Gtk::CheckButton *> koepfe(n, nullptr);
        std::set<std::pair<int, std::string>> gesehen;
        auto zeile = [&](Gtk::Widget *inhalt, bool kopf, double t0) -> Gtk::Label * {
            auto box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
            box->get_style_context()->add_class("hv-band");
            if (!kopf) box->get_style_context()->add_class("hv-band-op");
            inhalt->set_hexpand(true);
            inhalt->set_valign(kopf ? Gtk::ALIGN_CENTER : Gtk::ALIGN_START);
            if (!kopf) inhalt->set_margin_top(3);
            box->add(*inhalt);
            auto zl = Gtk::make_managed<Gtk::Label>();
            zl->get_style_context()->add_class("hv-band-zeit");
            zl->set_valign(kopf ? Gtk::ALIGN_CENTER : Gtk::ALIGN_START);
            if (!kopf) zl->set_margin_top(5);
            box->add(*zl);
            auto eb = Gtk::make_managed<Gtk::EventBox>();
            eb->add(*box);
            eb->signal_button_press_event().connect([this, t0](GdkEventButton *e) {
                if (e->button != 1) return false;
                springen_zeit(t0);
                return true;
            });
            dateien.add(*eb);
            baender.back().zeile = box;
            baender.back().zeit = zl;
            return zl;
        };
        int prog = -1;
        double vortrag = -1;
        std::size_t k = 0;
        while (k < alle.size()) {
            auto const &m0 = alle[k];
            auto const &P = d.programme[m0.p];
            auto name_von = [&](Bewegung const &m) {
                auto const &A = d.programme[m.p].abschnitte;
                return (m.abschnitt >= 0 && m.abschnitt < (int)A.size()) ? A[m.abschnitt].name : std::string();
            };
            std::string name = name_von(m0);
            std::size_t e = k;
            while (e < alle.size() && alle[e].p == m0.p && name_von(alle[e]) == name) ++e;
            double t0 = k > 0 ? zeit_alle[k - 1] : 0.0, t1 = e > 0 ? zeit_alle[e - 1] : 0.0;
            int i = m0.p;
            if (name.empty() && !P.abschnitte.empty()) {
                // Anfahren, Werkzeugwechsel: gehört zur nächsten Bearbeitung dieses Werkzeugs
                bool weiter = e < alle.size() && alle[e].p == m0.p;
                if (weiter) {
                    if (vortrag < 0) vortrag = t0;
                } else if (!baender.empty() && !baender.back().kopf && baender.back().p == i) {
                    baender.back().t1 = t1;
                }
                if (weiter || (!baender.empty() && baender.back().p == i)) {
                    k = e;
                    continue;
                }
            }
            if (vortrag >= 0) {
                t0 = vortrag;
                vortrag = -1;
            }
            if (i != prog) {
                prog = i;
                // Kopf des Werkzeugs
                auto tname = Glib::path_get_basename(P.datei);
                if (P.datei.find(G_DIR_SEPARATOR_S ".teile" G_DIR_SEPARATOR_S) != std::string::npos)
                    tname = "T" + std::to_string(P.t > 0 ? P.t : i + 1);
                if (d.zweiseitig) tname = TR("Seite ") + std::to_string(P.seite) + " · " + tname;
                auto kh = Gtk::make_managed<Gtk::CheckButton>();
                auto kl = Gtk::make_managed<Gtk::Label>();
                std::string text = "<b>" + markup(tname) + "</b>  <small>" + markup(P.werkzeug) + "</small>";
                if (P.abschnitte.empty()) text = "<span foreground='" + std::string(FARBEN_HEX[i % 4]) + "'>●</span>  " + text;
                kl->set_markup(text);
                kl->set_xalign(0);
                kl->set_ellipsize(Pango::ELLIPSIZE_END);
                kh->add(*kl);
                kh->set_active(true);
                kh->set_tooltip_text(TR("Werkzeug in der Simulation zeigen"));
                koepfe[i] = kh;
                Band b;
                b.kopf = true;
                b.p = i;
                b.t0 = t0;
                baender.push_back(b);
                zeile(kh, true, t0);
                if (P.abschnitte.empty()) {
                    wege_schluessel.push_back({i, ""});
                    wege_haken.push_back(kh);
                }
            }
            // die Bearbeitung
            Band b;
            b.p = i;
            b.t0 = t0;
            b.t1 = t1;
            baender.push_back(b);
            Gtk::Widget *inhalt = nullptr;
            int j = m0.abschnitt;
            std::string farbe = (j >= 0 && j < (int)abschnitt_farbe[i].size()) ? hex_von(abschnitt_farbe[i][j])
                                                                                 : std::string(FARBEN_HEX[i % 4]);
            auto l = Gtk::make_managed<Gtk::Label>();
            l->set_markup("<span foreground='" + farbe + "'>●</span> <small>" +
                          markup(name.empty() ? std::string(TR("Fräsbahnen")) : name) + "</small>");
            l->set_xalign(0);
            l->set_ellipsize(Pango::ELLIPSIZE_END);
            if (!P.abschnitte.empty() && !gesehen.count({i, name})) {
                gesehen.insert({i, name});
                auto h = Gtk::make_managed<Gtk::CheckButton>();
                h->add(*l);
                h->set_active(true);
                wege_schluessel.push_back({i, name});
                wege_haken.push_back(h);
                eigene[i].push_back(h);
                h->signal_toggled().connect([this] {
                    if (!wege_lade) auswahl_anwenden();
                });
                inhalt = h;
            } else {
                l->set_margin_start(P.abschnitte.empty() ? 0 : 26);
                inhalt = l;
            }
            zeile(inhalt, false, t0);
            k = e;
        }
        for (int i = 0; i < n; ++i) {
            auto kh = koepfe[i];
            if (!kh) continue;
            auto ei = eigene[i];
            kh->signal_toggled().connect([this, kh, ei] {
                if (wege_lade) return;
                wege_lade = true;
                for (auto h : ei) h->set_active(kh->get_active());
                wege_lade = false;
                auswahl_anwenden();
            });
        }
        // Zeit eines Werkzeugs = seine Bearbeitungen
        for (std::size_t a = 0; a < baender.size(); ++a) {
            if (!baender[a].kopf) continue;
            std::size_t b = a + 1;
            while (b < baender.size() && !baender[b].kopf) ++b;
            baender[a].t1 = b > a + 1 ? baender[b - 1].t1 : baender[a].t0;
        }
        ablauf_anordnen();
    }

    /// Höhen und Linie des Balkens: Höhe nach der Zeit (mindestens eine Textzeile); eine
    /// Bearbeitung, die mehr als 2,5 × so hoch würde wie die nächstgrösste, wird gestaucht und
    /// bekommt ihre Länge als Schlangenlinie
    void ablauf_anordnen()
    {
        const double KOPF = 30, MIN = 28, X = 15, AMP = 6, WELLE = 26, RAND = 7;
        std::vector<Band *> ops;
        int nk = 0;
        for (auto &b : baender) (b.kopf ? (void)++nk : ops.push_back(&b));
        bpfad.clear();
        if (baender.empty()) return;
        double H = std::max(420.0, nk * KOPF + ops.size() * MIN);
        double frei = H - nk * KOPF;
        auto dauer = [](Band const *b) { return std::max(1e-4, b->t1 - b->t0); };
        auto deckel = [&](double k) {
            if (ops.size() < 2) return 1e18;
            std::vector<double> l;
            for (auto b : ops) l.push_back(std::max(MIN, dauer(b) * k));
            std::sort(l.begin(), l.end(), std::greater<>());
            return std::max(l[1] * 2.5, MIN * 3);
        };
        double kmin = 1e18;
        for (auto b : ops) kmin = std::min(kmin, dauer(b));
        double lo = 0, hi = ops.empty() ? 1 : frei / kmin;
        for (int it = 0; it < 80; ++it) {
            double k = (lo + hi) / 2, dk = deckel(k), summe = 0;
            for (auto b : ops) summe += std::min(std::max(MIN, dauer(b) * k), dk);
            (summe > frei ? hi : lo) = k;
        }
        double k = lo, dk = deckel(k);
        auto dazu = [&](double x, double y) {
            if (bpfad.empty()) {
                bpfad.push_back({x, y, 0});
                return;
            }
            auto const &p = bpfad.back();
            bpfad.push_back({x, y, p.l + std::hypot(x - p.x, y - p.y)});
        };
        dazu(X, 0);
        double y = 0;
        for (auto &b : baender) {
            double soll = b.kopf ? KOPF : std::max(MIN, dauer(&b) * k);
            double hv = b.kopf ? KOPF : std::min(soll, dk);
            int y0 = (int)std::lround(y), y1 = (int)std::lround(y + hv);
            b.y = y0;
            b.h = y1 - y0;
            b.l0 = bpfad.back().l;
            // Schlange aus lauter weichen S-Bögen: von der Mitte zum ersten Ausschlag, dann
            // hin und her, zuletzt zurück zur Mitte. Jeder Bogen läuft nach (1 − cos)/2, beginnt
            // und endet also senkrecht – die Übergänge sind ohne Knick, auch zur Geraden.
            double hw = b.h - 2.0 * RAND;              // oben und unten ein Stück gerade
            int mmax = (int)std::floor(hw / (WELLE / 2)) - 1;
            if (!b.kopf && soll > hv + 1 && mmax >= 1) {
                auto punkte = [&](int m, bool zeichnen) {
                    double einheit = hw / (m + 1), l = 2 * RAND, px = X, py = 0;
                    auto bogen = [&](double xa, double xb, double ya, double dy) {
                        int n = 24;
                        for (int q = 1; q <= n; ++q) {
                            double t = (double)q / n;
                            double xx = xa + (xb - xa) * (1 - std::cos(M_PI * t)) / 2, yy = ya + dy * t;
                            if (zeichnen) dazu(xx, y0 + RAND + yy);
                            l += std::hypot(xx - px, yy - py);
                            px = xx;
                            py = yy;
                        }
                    };
                    double yy = 0;
                    bogen(X, X + AMP, yy, einheit / 2);
                    yy += einheit / 2;
                    double seite = 1;
                    for (int q = 0; q < m; ++q) {
                        bogen(X + seite * AMP, X - seite * AMP, yy, einheit);
                        yy += einheit;
                        seite = -seite;
                    }
                    bogen(X + seite * AMP, X, yy, einheit / 2);
                    return l;
                };
                int m = 1;
                while (m < mmax && punkte(m, false) < soll) ++m;
                dazu(X, y0 + RAND);
                punkte(m, true);
                dazu(X, y1);
            } else {
                dazu(X, y1);
            }
            b.l1 = bpfad.back().l;
            b.zeile->set_size_request(-1, b.h);
            y += hv;
        }
        balken.set_size_request(30, (int)std::lround(y) + 2 * KNOPF_RAND);
        for (auto &b : baender) b.zustand = -1;
        ablauf_auffrischen();
    }

    /// Laufzeit (über «alle») an der aktuellen Stelle
    double zeit_alle_jetzt() const
    {
        if (zeit_alle.empty() || moves.empty() || bis <= 0) return 0;
        int a = moves[std::min<size_t>(bis, moves.size()) - 1].ur;
        double t = a >= 0 ? zeit_alle[a] : 0;
        if (bis < (int)moves.size() && anteil > 0 && moves[bis].ur >= 0) t += anteil * (zeit_alle[moves[bis].ur] - t);
        return t;
    }

    double laenge_bei(double t) const
    {
        Band const *letzte = nullptr;
        for (auto const &b : baender) {
            if (b.kopf) continue;
            letzte = &b;
            if (t < b.t1) {
                double f = b.t1 > b.t0 ? std::clamp((t - b.t0) / (b.t1 - b.t0), 0.0, 1.0) : 0.0;
                return b.l0 + f * (b.l1 - b.l0);
            }
        }
        return letzte ? letzte->l1 : 0;
    }

    void springen_zeit(double t)
    {
        if (moves.empty() || zeit_alle.empty()) return;
        // Stelle in «alle», dann die erste ausgewählte Bewegung ab dort
        auto it = std::upper_bound(zeit_alle.begin(), zeit_alle.end(), t);
        int a = (int)(it - zeit_alle.begin());
        double s = moves.size();
        for (std::size_t j = 0; j < moves.size(); ++j) {
            if (moves[j].ur >= a) {
                double t0 = a > 0 ? zeit_alle[a - 1] : 0, t1 = a < (int)zeit_alle.size() ? zeit_alle[a] : t0;
                double f = (moves[j].ur == a && t1 > t0) ? std::clamp((t - t0) / (t1 - t0), 0.0, 1.0) : 0.0;
                s = j + f;
                break;
            }
        }
        if (t >= zeit_alle.back()) s = moves.size();
        anzeige_wahl = 0;
        stopp();
        setze_fort(1000.0 * s / moves.size());
    }

    void springen_y(double yy)
    {
        yy -= KNOPF_RAND;
        for (auto const &b : baender) {
            if (yy < b.y + b.h || &b == &baender.back()) {
                if (b.kopf) {
                    springen_zeit(b.t0);
                } else {
                    double f = std::clamp((yy - b.y) / std::max(1, b.h), 0.0, 1.0);
                    springen_zeit(b.t0 + f * (b.t1 - b.t0));
                }
                return;
            }
        }
    }

    bool balken_malen(Cairo::RefPtr<Cairo::Context> const &cr)
    {
        if (bpfad.size() < 2) return true;
        cr->translate(0, KNOPF_RAND);
        double lf = laenge_bei(zeit_alle_jetzt());
        cr->set_line_cap(Cairo::LINE_CAP_ROUND);
        cr->set_line_join(Cairo::LINE_JOIN_ROUND);
        cr->set_line_width(6);
        cr->move_to(bpfad[0].x, bpfad[0].y);
        for (std::size_t i = 1; i < bpfad.size(); ++i) cr->line_to(bpfad[i].x, bpfad[i].y);
        cr->set_source_rgba(1, 1, 1, 0.14);
        cr->stroke();
        // Füllung bis zur aktuellen Stelle
        double kx = bpfad[0].x, ky = bpfad[0].y;
        cr->move_to(kx, ky);
        for (std::size_t i = 1; i < bpfad.size(); ++i) {
            auto const &a = bpfad[i - 1], &b = bpfad[i];
            if (b.l >= lf) {
                double f = b.l > a.l ? (lf - a.l) / (b.l - a.l) : 0;
                kx = a.x + f * (b.x - a.x);
                ky = a.y + f * (b.y - a.y);
                cr->line_to(kx, ky);
                break;
            }
            cr->line_to(b.x, b.y);
            kx = b.x;
            ky = b.y;
        }
        cr->set_source_rgb(1.0, 0.549, 0.0);
        if (lf > 0.5) cr->stroke();
        else cr->begin_new_path();
        // Werkzeugwechsel: kleine Kerbe
        for (auto const &b : baender) {
            if (!b.kopf || b.y <= 0) continue;
            cr->rectangle(5, b.y - 1, 20, 2);
            if (lf >= b.l0) cr->set_source_rgb(0.141, 0.153, 0.173);
            else cr->set_source_rgba(1, 1, 1, 0.4);
            cr->fill();
        }
        // Knopf
        cr->arc(kx, ky, 6.5, 0, 2 * M_PI);
        cr->set_source_rgb(1, 1, 1);
        cr->fill_preserve();
        cr->set_line_width(3);
        cr->set_source_rgb(1.0, 0.549, 0.0);
        cr->stroke();
        return true;
    }

    /// Zeilen nachführen: läuft (hervorgehoben), fertig (blass), Zeiten; mitrollen beim Abspielen
    void ablauf_auffrischen()
    {
        double t = zeit_alle_jetzt();
        bool ende = !moves.empty() && bis >= (int)moves.size();
        Gtk::Widget *aktiv = nullptr;
        for (auto &b : baender) {
            int z = (ende || t >= b.t1 - 1e-9) && b.t1 > b.t0 ? 2 : (t >= b.t0 && t < b.t1 ? 1 : 0);
            if (ende && b.t1 <= b.t0) z = 2;
            if (z == 1) aktiv = b.zeile;
            std::string txt = b.kopf ? (z == 2 ? std::string(TR("fertig")) : z == 1 ? TR("noch ") + m_ss(b.t1 - t) : m_ss(b.t1 - b.t0))
                                     : (z == 1 ? m_ss(b.t1 - t) : m_ss(b.t1 - b.t0));
            if (b.zeit && b.zeit->get_text() != txt) b.zeit->set_text(txt);
            if (z == b.zustand) continue;
            b.zustand = z;
            auto ctx = b.zeile->get_style_context();
            ctx->remove_class("hv-band-an");
            ctx->remove_class("hv-band-fertig");
            if (z == 1 && !b.kopf) ctx->add_class("hv-band-an");
            if (z == 2) ctx->add_class("hv-band-fertig");
        }
        balken.queue_draw();
        if (spielt && aktiv && aktiv->get_mapped()) {
            int ax = 0, ay = 0;
            if (aktiv->translate_coordinates(seite, 0, 0, ax, ay)) {
                auto adj = seiten_scroll.get_vadjustment();
                double oben = adj->get_value(), seite_h = adj->get_page_size(), h = aktiv->get_allocated_height();
                if (ay < oben + 20 || ay + h > oben + seite_h - 20)
                    adj->set_value(std::clamp(ay - seite_h / 3, adj->get_lower(), adj->get_upper() - seite_h));
            }
        }
    }

    /// Regler 0…1 ↔ Faktor ½× … 1000×
    static double regler_von(double faktor) { return std::log(faktor / 0.5) / std::log(2000.0); }
    double faktor() const { return 0.5 * std::pow(2000.0, tempo.get_value()); }

    static std::string faktor_text(double f)
    {
        if (std::abs(f - 1) < 1e-6) return TR("Echtzeit");
        char b[32];
        if (f < 1) std::snprintf(b, sizeof b, "%.1f×", f);
        else if (f < 10) std::snprintf(b, sizeof b, "%.1f×", f);
        else if (f < 100) std::snprintf(b, sizeof b, "%.0f×", f);
        else std::snprintf(b, sizeof b, "%.0f×", std::round(f / 10) * 10);
        std::string t(b);
        char const komma = hirch_englisch() ? '.' : ',';     // englisch mit Punkt (Windows-Sitzung 28.10.)
        for (auto &c : t) if (c == '.' || c == ',') c = komma;
        if (t.size() > 4 && t.compare(t.size() - 4, 4, std::string(1, komma) + "0×") == 0) t.erase(t.size() - 4, 2);
        return t;
    }

    /// Stelle (Bewegungen, mit Bruchteil der angefangenen) zu einer Laufzeit in Minuten
    double stelle_von_zeit(double t) const
    {
        if (zeit_bis.empty()) return 0;
        auto it = std::upper_bound(zeit_bis.begin(), zeit_bis.end(), t);
        int n = (int)(it - zeit_bis.begin());
        if (n >= (int)zeit_bis.size()) return (double)zeit_bis.size();
        double a = n > 0 ? zeit_bis[n - 1] : 0, b = zeit_bis[n];
        return n + (b > a ? std::clamp((t - a) / (b - a), 0.0, 1.0) : 0.0);
    }

    /// Laufzeit (Minuten) an der aktuellen Stelle
    double zeit_jetzt() const
    {
        if (zeit_bis.empty() || bis <= 0) return 0;
        double a = zeit_bis[std::min<size_t>(bis, zeit_bis.size()) - 1];
        if (bis < (int)zeit_bis.size()) a += (zeit_bis[bis] - a) * anteil;
        return a;
    }

    void tempo_neu_ansetzen()
    {
        if (zeit_bis.size() != moves.size()) zeiten_rechnen();
        spiel_min = zeit_jetzt();
        spiel_uhr = g_get_monotonic_time();
    }

    static std::string dauer_text(double sekunden)
    {
        long s_ = (long)std::lround(sekunden);
        char b[32];
        if (s_ >= 3600) std::snprintf(b, sizeof b, "%ld:%02ld:%02ld h", s_ / 3600, (s_ / 60) % 60, s_ % 60);
        else std::snprintf(b, sizeof b, "%ld:%02ld min", s_ / 60, s_ % 60);
        return b;
    }

    void rest_anzeigen()
    {
        if (zeit_bis.size() != moves.size()) zeiten_rechnen();
        double gesamt = zeit_bis.empty() ? 0 : zeit_bis.back();
        double rest_min = std::max(0.0, gesamt - zeit_jetzt());
        double sek;
        if (uebersicht.get_active()) sek = 15.0 * (1000.0 - fort.get_value()) / 1000.0;
        else sek = rest_min * 60.0 / faktor();
        if (rest_min <= 1e-9) rest_text.set_text(TR("Abspielen: ") + dauer_text(uebersicht.get_active() ? 15.0
                                                                                     : gesamt * 60.0 / faktor()));
        else rest_text.set_text((spielt ? TR("noch ") : TR("Abspielen ab hier: ")) + dauer_text(sek));
    }

    void tempo_anzeigen()
    {
        tempo_text.set_text(uebersicht.get_active() ? TR("Übersicht") : faktor_text(faktor()));
        tempo.set_sensitive(true);
        rest_anzeigen();
    }

    /// Ort des Werkzeugs: Ende der letzten fertigen Bewegung, in der angefangenen
    /// Bewegung entsprechend weiter
    bool werkzeug_ort(Bewegung &aus) const
    {
        if (moves.empty() || (bis <= 0 && anteil <= 0)) return false;
        if (anteil > 0 && bis < (int)moves.size()) {
            auto const &m = moves[bis];
            Bewegung a;
            a.z = d.sicherheit;
            if (bis > 0 && moves[bis - 1].p == m.p) a = moves[bis - 1];
            aus = m;
            aus.x = a.x + (m.x - a.x) * anteil;
            aus.y = a.y + (m.y - a.y) * anteil;
            aus.z = a.z + (m.z - a.z) * anteil;
            return true;
        }
        aus = moves[std::min<size_t>(bis, moves.size()) - 1];
        return true;
    }

    /// Ansicht wird gerade gedreht/verschoben/gezoomt: einfacher zeichnen, kurz
    /// nach der letzten Bewegung wieder in voller Qualität
    void bewegung_melden()
    {
        bewegt = true;
        ruhe_takt.disconnect();
        ruhe_takt = Glib::signal_timeout().connect([this] {
            bewegt = false;
            bereich.queue_render();
            return false;
        }, 250);
    }

    void stopp()
    {
        spielt = false;
        takt.disconnect();
        play.set_label(TR("▶ Abspielen"));
        bereich.queue_render();          // wieder in voller Qualität
    }

    void setze_fort(double v)
    {
        setzt = true;
        fort.set_value(v);
        setzt = false;
        fortschritt(v);
    }

    // ------------------------------------------------------------ Daten
    void laden(VorschauDaten const &neu)
    {
        stopp();
        d = neu;
        moves.clear();
        zeit_bis.clear();
        for (auto c : dateien.get_children()) dateien.remove(*c), delete c;
        for (auto c : bericht_box.get_children()) bericht_box.remove(*c), delete c;
        for (auto c : zahlen.get_children()) zahlen.remove(*c), delete c;
        if (!d.meldung.empty()) {
            meldung.set_text(d.meldung);
            meldung.show();
            bereich.hide();
            status.hide();
            return;
        }
        meldung.hide();
        bereich.show();
        status.show();
        anzeige_wahl = 0;
        seiten_knoepfe.set_visible(d.zweiseitig);
        if (d.zweiseitig) for (auto c : seiten_knoepfe.get_children()) c->show();
        for (size_t i = 0; i < d.programme.size(); ++i) {
            auto m = tap_lesen(datei_lesen(d.programme[i].datei), (int)i, d.sicherheit);
            moves.insert(moves.end(), m.begin(), m.end());
        }
        // Farbtafel: je Bearbeitung ihre Farbe aus dem Panel; fehlt die
        // Angabe, die Farbe des Werkzeugs (Datei)
        tafel.clear();
        std::vector<std::string> tafel_hex;
        auto index_von = [&](std::string const &hex, std::array<float, 3> ersatz) -> int {
            for (size_t i = 0; i < tafel_hex.size(); ++i) if (tafel_hex[i] == hex) return (int)i;
            if (tafel.size() >= 16) return (int)tafel.size() - 1;
            std::array<float, 3> f = ersatz;
            unsigned r, g, b;
            if (hex.size() == 7 && hex[0] == '#' && std::sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
                f = {r / 255.f, g / 255.f, b / 255.f};
            }
            tafel.push_back(f);
            tafel_hex.push_back(hex);
            return (int)tafel.size() - 1;
        };
        std::vector<std::vector<int>> abschnitt_farbe(d.programme.size());
        for (size_t i = 0; i < d.programme.size(); ++i) {
            for (auto const &a : d.programme[i].abschnitte) abschnitt_farbe[i].push_back(index_von(a.farbe, FARBEN[i % 4]));
        }
        std::vector<int> wz_farbe(d.programme.size(), -1);
        // Bearbeitungen über alle Programme (Karte «zuletzt tiefer gemacht von»): je Schlüssel einmal
        bearbeitungen.clear();
        std::vector<std::vector<int>> abschnitt_bearbeitung(d.programme.size());
        for (size_t i = 0; i < d.programme.size(); ++i) {
            for (auto const &a : d.programme[i].abschnitte) {
                std::string id = a.id.empty() ? std::to_string(i) + ":" + a.name : a.id;
                int nr = -1;
                for (size_t q = 0; q < bearbeitungen.size(); ++q) if (bearbeitungen[q].id == id) nr = (int)q;
                if (nr < 0) {
                    bearbeitungen.push_back({id, a.farbe});
                    nr = (int)bearbeitungen.size() - 1;
                }
                abschnitt_bearbeitung[i].push_back(nr);
            }
        }
        for (auto &m : moves) {
            auto const &A = d.programme[m.p].abschnitte;
            int k = -1;
            for (size_t j = 0; j < A.size(); ++j) if (A[j].zeile <= m.zeile) k = (int)j;
            m.abschnitt = k;
            m.bearbeitung = k >= 0 ? abschnitt_bearbeitung[m.p][k] : -1;
            if (k >= 0) {
                m.farbe = abschnitt_farbe[m.p][k];
            } else {
                if (wz_farbe[m.p] < 0) wz_farbe[m.p] = index_von("werkzeug" + std::to_string(m.p), FARBEN[m.p % 4]);
                m.farbe = wz_farbe[m.p];
            }
        }
        if (tafel.empty()) tafel.push_back(FARBEN[0]);
        auto hex_von = [&](int i) {
            char b[8];
            auto const &f = tafel[std::min<size_t>(i, tafel.size() - 1)];
            std::snprintf(b, sizeof b, "#%02x%02x%02x", int(f[0] * 255), int(f[1] * 255), int(f[2] * 255));
            return std::string(b);
        };
        int n = (int)d.programme.size();
        // eine Datei mit Werkzeugwechsel: gespeichert wird eine Datei, gerechnet sind mehrere Teile (.teile). Die Teile
        // liegen inzwischen immer in .teile – zählen, was gespeichert wird (Windows 01.11.: «2, eine Datei» bei 2 Dateien)
        bool gemeinsam = d.ausgaben > 0 ? (d.ausgaben == 1 && n > 1)
                                        : n > 0 && d.programme[0].datei.find(G_DIR_SEPARATOR_S ".teile" G_DIR_SEPARATOR_S) != std::string::npos;
        dateien_titel.set_text(d.zweiseitig ? (gemeinsam ? TR("ABLAUF (") + std::to_string(n) + TR(" WERKZEUGEINSÄTZE, 2 DATEIEN)")
                                                         : TR("DATEIEN (") + std::to_string(n) + TR(", IN DIESER REIHENFOLGE)"))
                               : gemeinsam ? (n > 1 ? TR("WERKZEUGE (") + std::to_string(n) + TR(", EINE DATEI)") : "WERKZEUG")
                               : n > 1 ? TR("DATEIEN (") + std::to_string(n) + TR(", IN DIESER REIHENFOLGE)") : "DATEI");
        for (auto const &t : d.warnungen) {
            auto l = new Gtk::Label("⚠ " + t);
            l->set_xalign(0);
            l->set_line_wrap(true);
            l->set_max_width_chars(36);
            l->get_style_context()->add_class("hv-warn");
            bericht_box.add(*l);
        }
        for (auto const &t : d.bericht) {
            auto pos = t.find_first_not_of(' ');
            bool unter = pos != std::string::npos && pos >= 2;
            auto l = new Gtk::Label(unter ? "– " + t.substr(pos) : t);
            l->set_xalign(0);
            l->set_line_wrap(true);
            l->set_max_width_chars(36);
            if (unter) {
                l->set_margin_start(10);
                l->get_style_context()->add_class("hv-dim");
            }
            bericht_box.add(*l);
        }
        bericht_klapp.set_visible(!d.bericht.empty() || !d.warnungen.empty());
        bericht_klapp.set_label(d.warnungen.empty() ? TR("Bericht")
                                                    : TR("Bericht · ⚠ ") + std::to_string(d.warnungen.size()) +
                                                          (d.warnungen.size() == 1 ? TR(" Hinweis") : TR(" Hinweise")));
        for (size_t k = 0; k < moves.size(); ++k) moves[k].ur = (int)k;
        alle = moves;
        zeit_alle = zeiten_von(alle);
        ablauf_bauen(gemeinsam, hex_von, abschnitt_farbe);
        kennzahlen();
        dateien.show_all();
        bericht_box.show_all();
        zahlen.show_all();
        flaeche_bestimmen();
        neu_aufbauen = true;
        setzt = true;
        fort.set_value(1000);
        setzt = false;
        bis = (int)moves.size();
        start_ansicht = true;
        ansicht(0);
        fortschritt(1000);
        if (auto q = g_getenv("HIRCH_TEST_VORSCHAU_STELLE")) {   // Test: Stelle 0…1 (Werkzeug sichtbar)
            setzt = true;
            fort.set_value(1000 * g_ascii_strtod(q, nullptr));
            setzt = false;
            fortschritt(1000 * g_ascii_strtod(q, nullptr));
        }
    }

    /// Nur die angehakten Werkzeugwege simulieren. Sind alle an, steht die
    /// Simulation am Ende; einzelne fehlen einfach (ihr Material bleibt stehen).
    void auswahl_anwenden()
    {
        stopp();
        std::set<std::pair<int, std::string>> an;
        bool alle_an = true;
        for (size_t k = 0; k < wege_schluessel.size() && k < wege_haken.size(); ++k) {
            if (wege_haken[k]->get_active()) an.insert(wege_schluessel[k]);
            else alle_an = false;
        }
        moves.clear();
        zeit_bis.clear();
        if (alle_an) {
            moves = alle;
        } else {
            bool davor_dabei = false;
            for (size_t k = 0; k < alle.size(); ++k) {
                auto const &m = alle[k];
                auto const &A = d.programme[m.p].abschnitte;
                std::string name = (m.abschnitt >= 0 && m.abschnitt < (int)A.size()) ? A[m.abschnitt].name : "";
                bool dabei = an.count({m.p, name}) || (A.empty() && an.count({m.p, ""}));
                if (!dabei) {
                    davor_dabei = false;
                    continue;
                }
                // nach einer Lücke: von der Bewegung davor im Eilgang ansetzen
                if (!davor_dabei && k > 0) {
                    Bewegung s0 = alle[k - 1];
                    s0.eil = true;
                    s0.z = std::max(s0.z, d.sicherheit);
                    moves.push_back(s0);
                }
                moves.push_back(m);
                davor_dabei = true;
            }
        }
        for (auto c : zahlen.get_children()) zahlen.remove(*c), delete c;
        kennzahlen();
        zahlen.show_all();
        neu_aufbauen = true;
        setze_fort(1000);
        bereich.queue_render();
    }

    void flaeche_bestimmen()
    {
        if (d.breite > 0 && d.hoehe > 0) {
            x0 = 0; y0 = 0; w = d.breite; h = d.hoehe;
            return;
        }
        double a0 = 1e30, a1 = 1e30, a2 = -1e30, a3 = -1e30, rmax = 0;
        for (auto const &m : moves) {
            if (!m.eil || m.z < 0) {
                a0 = std::min(a0, m.x); a1 = std::min(a1, m.y);
                a2 = std::max(a2, m.x); a3 = std::max(a3, m.y);
            }
        }
        for (auto const &P : d.programme) rmax = std::max(rmax, P.durchmesser);
        if (a0 > a2) { a0 = 0; a1 = 0; a2 = 100; a3 = 100; }
        double rand = 10 + rmax;
        x0 = std::min(0.0, a0 - rand);
        y0 = std::min(0.0, a1 - rand);
        w = a2 + rand - x0;
        h = a3 + rand - y0;
    }

    struct Werte {
        double vor = 0, eil = 0, min = 0, zmin = 0;
        int im_material = 0;
    };
    Werte werte()
    {
        Werte r;
        Bewegung p;
        p.z = d.sicherheit;
        int prog = -1;
        for (auto const &m : moves) {
            if (m.p != prog) {
                p = Bewegung{};
                p.z = d.sicherheit;
                prog = m.p;
            }
            double dd = std::sqrt((m.x - p.x) * (m.x - p.x) + (m.y - p.y) * (m.y - p.y) + (m.z - p.z) * (m.z - p.z));
            if (m.eil) {
                r.eil += dd;
                r.min += dd / 3000;
                if (std::hypot(m.x - p.x, m.y - p.y) > 1e-6 && std::min(p.z, m.z) < 0) ++r.im_material;
            } else {
                r.vor += dd;
                r.min += m.f > 0 ? dd / m.f : 0;
            }
            // Passlöcher gehen absichtlich bis ins Opferbrett
            auto const &A = d.programme[m.p].abschnitte;
            bool pass = m.abschnitt >= 0 && m.abschnitt < (int)A.size() && A[m.abschnitt].name == "Passlöcher";
            if (!pass) r.zmin = std::min(r.zmin, m.z);
            p = m;
        }
        return r;
    }

    void kennzahlen()
    {
        auto w_ = werte();
        auto zeit = [](double min) {
            return min < 1 ? TR("ca. ") + std::to_string(std::max(1, (int)std::lround(min * 60))) + " s"
                           : TR("ca. ") + zahl(min, 1) + " min";
        };
        std::vector<std::pair<std::string, std::string>> zs = {
            {TR("Bewegungen"), std::to_string(moves.size())},
            {TR("Fräsweg"), zahl(w_.vor, 0) + " mm"},
            {TR("Leerweg"), zahl(w_.eil, 0) + " mm"},
            {TR("Laufzeit"), zeit(w_.min) + (d.programme.size() > 1 ? TR(" + Wechsel") : "")},
            {TR("Tiefste Stelle"), zahl(w_.zmin, 2) + " mm"}};
        int r = 0;
        for (auto const &[a, b] : zs) {
            auto la = new Gtk::Label(a), lb = new Gtk::Label(b);
            la->set_xalign(0);
            la->get_style_context()->add_class("hv-dim");
            la->set_hexpand(true);
            lb->set_xalign(1);
            zahlen.attach(*la, 0, r);
            zahlen.attach(*lb, 1, r++);
        }
        std::string t;
        // Eilgänge unter der Oberfläche sind in schon ausgeräumten Taschen gewollt; ob einer
        // Material trifft, prüft die Kollisionsprüfung genau (Hinweise im Panel)
        if (-w_.zmin > d.dicke + 0.5) t += TR("Achtung: tiefer als das Material (") + zahl(-w_.zmin, 2) + " mm).";
        warn.set_text(t);
        warn.set_visible(!t.empty());
    }

    void fortschritt(double wert)
    {
        double stelle = moves.size() * wert / 1000.0 + 1e-9;
        bis = std::clamp((int)std::floor(stelle), 0, (int)moves.size());
        anteil = bis < (int)moves.size() ? std::clamp(stelle - bis, 0.0, 1.0) : 0.0;
        if (anteil < 1e-6) anteil = 0;
        if (!spielt) rest_anzeigen();
        ab_knopf.set_sensitive(bis > 0 && bis < (int)moves.size());
        ablauf_auffrischen();
        hoehe_neu = true;
        status_setzen();
        bereich.queue_render();
    }

    void status_setzen()
    {
        status.set_text((d.zweiseitig ? TR("Seite ") + std::to_string(oben_seite()) + TR(" oben · ") : std::string()) +
                        std::to_string(bis) + " / " + std::to_string(moves.size()) + TR(" Bewegungen · Raster ") +
                        zahl(zelle, 2) + " mm" + (gl_ok ? (software ? TR(" · ohne Grafikkarte") : TR(" · Grafikkarte")) : ""));
    }

    void ansicht(int art)
    {
        ziel = {x0 + w / 2, y0 + h / 2, -d.dicke / 2};
        double s = std::max(w, h);
        V3 pos;
        if (art == 1) pos = {ziel.x, ziel.y - 0.001, s * 1.6};
        else if (art == 2) pos = {ziel.x, ziel.y - s * 1.6, s * 0.15};
        else pos = {ziel.x - s * 0.35, ziel.y - s * 1.15, s * 0.95};
        V3 dv = pos - ziel;
        abstand = std::sqrt(punkt(dv, dv));
        hoehenwinkel = std::asin(std::clamp(dv.z / abstand, -1.0, 1.0));
        azimut = std::atan2(dv.y, dv.x);
        // schmale Ansicht: weiter weg, damit das ganze Material Platz hat
        double bw = std::max(1, bereich.get_allocated_width()), bh = std::max(1, bereich.get_allocated_height());
        if (bereich.get_realized()) abstand *= std::max(1.0, 1.45 / (bw / bh));
        bereich.queue_render();
    }

    // ------------------------------------------------------------ OpenGL
    void gl_start()
    {
        bereich.make_current();
        try {
            bereich.throw_if_error();
        } catch (Glib::Error const &e) {
            gl_fehler = e.what();
            return;
        }
        std::string fehler;
        bool abtrag_ok = abtrag.gl_start(fehler);
        p_anz = programm(ANZEIGE_VS, ANZEIGE_FS, fehler);
        p_wand = programm(WAND_VS, WAND_FS, fehler);
        p_unten = programm(UNTEN_VS, UNTEN_FS, fehler);
        p_linie = programm(LINIE_VS, LINIE_FS, fehler);
        p_koerper = programm(KOERPER_VS, KOERPER_FS, fehler);
        p_hg = programm(HG_VS, HG_FS, fehler);
        glGenVertexArrays(1, &vao_leer);
        if (!abtrag_ok || !p_anz || !p_wand || !p_unten || !p_linie || !p_koerper || !p_hg) {
            gl_fehler = TR("Shader nicht übersetzt: ") + fehler;
            g_warning("Hirch-Vorschau: %s", gl_fehler.c_str());
            return;
        }
        auto r = reinterpret_cast<char const *>(glGetString(GL_RENDERER));
        std::string renderer = r ? r : "";
        software = renderer.find("llvmpipe") != std::string::npos || renderer.find("softpipe") != std::string::npos ||
                   renderer.find("SwiftShader") != std::string::npos || renderer.find("Software") != std::string::npos;
        if (std::getenv("HIRCH_VORSCHAU_WIE_GPU")) software = false;    // Tests: Grenzen wie mit Grafikkarte
        GLint mx = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &mx);
        proben = std::min(4, (int)mx);
        gl_ok = true;
        neu_aufbauen = true;
    }

    /// Soll-Tiefe der Taschen einer Seite als Textur (W × H, Tiefe in Rot/Grün als 16 Bit)
    GLuint soll_bild(int seite)
    {
        bool da = false;
        for (auto const &s_ : d.soll) da = da || s_.seite == seite;
        int sw = da ? W : 1, sh = da ? H : 1;
        auto flaeche = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, sw, sh);
        if (da) {
            auto cr = Cairo::Context::create(flaeche);
            cr->set_antialias(Cairo::ANTIALIAS_NONE);
            cr->set_fill_rule(Cairo::FILL_RULE_EVEN_ODD);
            std::vector<VorschauSoll const *> liste;
            for (auto const &s_ : d.soll) if (s_.seite == seite) liste.push_back(&s_);
            std::sort(liste.begin(), liste.end(), [](auto a, auto b) { return a->tiefe < b->tiefe; });
            double tmax = d.dicke + 10;
            for (auto s_ : liste) {
                int v = std::clamp((int)std::lround(s_->tiefe / tmax * 65535.0), 1, 65535);
                cr->set_source_rgb((v >> 8) / 255.0, (v & 255) / 255.0, 0);
                for (auto const &fl : s_->flaechen) {
                    for (auto const &r : fl) {
                        for (size_t i = 0; i + 1 < r.size(); i += 2) {
                            double px = (r[i] - x0) / w * W, py = (r[i + 1] - y0) / h * H;
                            if (i == 0) cr->move_to(px, py); else cr->line_to(px, py);
                        }
                        cr->close_path();
                    }
                    cr->fill();
                }
            }
            flaeche->flush();
        }
        // Cairo: BGRA im Speicher (little endian) – als GL_BGRA hochladen
        GLuint t = 0;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, flaeche->get_stride() / 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sw, sh, 0, GL_BGRA, GL_UNSIGNED_BYTE, flaeche->get_data());
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return t;
    }

    void puffer_weg()
    {
        abtrag.puffer_weg();
        GLuint b[] = {vbo_gitter, ibo_gitter, vbo_wand, ibo_wand, vbo_vor, vbo_eil, vbo_wz, vbo_tisch};
        glDeleteBuffers(8, b);
        GLuint v[] = {vao_gitter, vao_wand, vao_vor, vao_eil, vao_wz, vao_tisch};
        glDeleteVertexArrays(6, v);
        vbo_gitter = ibo_gitter = vbo_wand = ibo_wand = vbo_vor = vbo_eil = vbo_wz = vbo_tisch = 0;
        vao_gitter = vao_wand = vao_vor = vao_eil = vao_wz = vao_tisch = 0;
        if (tex_soll) glDeleteTextures(1, &tex_soll);
        if (tex_soll2) glDeleteTextures(1, &tex_soll2);
        tex_soll = tex_soll2 = 0;
        werkzeug_prog = -1;
    }

    void gl_ende()
    {
        bereich.make_current();
        if (!gl_ok) return;
        puffer_weg();
        if (fbo_ms) glDeleteFramebuffers(1, &fbo_ms);
        if (rb_farbe) glDeleteRenderbuffers(1, &rb_farbe);
        if (rb_tiefe) glDeleteRenderbuffers(1, &rb_tiefe);
        fbo_ms = rb_farbe = rb_tiefe = 0;
        abtrag.gl_ende();
        for (auto p : {p_anz, p_wand, p_unten, p_linie, p_koerper, p_hg}) glDeleteProgram(p);
        p_unten = 0;
        if (vao_leer) glDeleteVertexArrays(1, &vao_leer);
        vao_leer = 0;
        gl_ok = false;
    }

    static GLuint vao_mit(GLuint &vbo, std::vector<float> const &daten, std::initializer_list<int> teile)
    {
        GLuint vao;
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, std::max<size_t>(1, daten.size()) * sizeof(float),
                     daten.empty() ? nullptr : daten.data(), GL_STATIC_DRAW);
        int stride = 0;
        for (int t : teile) stride += t;
        int ort = 0, versatz = 0;
        for (int t : teile) {
            glEnableVertexAttribArray(ort);
            glVertexAttribPointer(ort, t, GL_FLOAT, GL_FALSE, stride * sizeof(float),
                                  reinterpret_cast<void *>(size_t(versatz) * sizeof(float)));
            ++ort;
            versatz += t;
        }
        return vao;
    }

    void aufbauen()
    {
        puffer_weg();
        // Höhenbilder, Karte und Instanzen: der eine Rechenweg (hirch-abtrag)
        {
            std::vector<AbtragWerkzeug> wz;
            std::vector<int> seiten;
            for (auto const &P : d.programme) {
                wz.push_back({P.form, P.durchmesser, P.vwinkel, P.spitze, P.eckradius});
                seiten.push_back(P.seite);
            }
            abtrag.einrichten(x0, y0, w, h, d.dicke, d.zweiseitig, software, moves, wz, seiten, d.sicherheit,
                              bearbeitungen);
            zelle = abtrag.zelle;
            W = abtrag.W;
            H = abtrag.H;
            hmin = abtrag.hmin;
        }
        tex_soll = soll_bild(1);
        if (d.zweiseitig) tex_soll2 = soll_bild(2);

        Bewegung p;          // für die Bahnen unten
        int prog = 0;
        // Anzeige-Gitter: höchstens ~0,9 Mio. Ecken (ohne Grafikkarte weniger)
        double grenze = software ? 400000 : 4100000;
        int f = std::max(1, (int)std::ceil(std::sqrt((double)W * H / grenze)));
        int gx = std::max(1, W / f), gy = std::max(1, H / f);
        gitter_schritt = std::max(double(W) / gx, double(H) / gy);
        std::vector<float> gv;
        gv.reserve(size_t(gx + 1) * (gy + 1) * 4);
        for (int j = 0; j <= gy; ++j)
            for (int i = 0; i <= gx; ++i) {
                double u = double(i) / gx, v = double(j) / gy;
                gv.insert(gv.end(), {float(x0 + u * w), float(y0 + v * h), float(u), float(v)});
            }
        std::vector<GLuint> gi;
        gi.reserve(size_t(gx) * gy * 6);
        for (int j = 0; j < gy; ++j)
            for (int i = 0; i < gx; ++i) {
                GLuint a = j * (gx + 1) + i, b = a + 1, c = a + gx + 1, e = c + 1;
                gi.insert(gi.end(), {a, b, e, a, e, c});
            }
        vao_gitter = vao_mit(vbo_gitter, gv, {2, 2});
        glGenBuffers(1, &ibo_gitter);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_gitter);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, gi.size() * sizeof(GLuint), gi.data(), GL_STATIC_DRAW);
        n_gitter = (GLsizei)gi.size();

        // Seitenwände: obere Kante folgt dem Höhenbild
        std::vector<std::array<double, 5>> rand;   // u, v, nx, ny, 0
        int schritt = std::max(1, std::max(W, H) / 1500);
        if (d.rund_r > 0 && w > 0 && h > 0) {
            // rundes Material: Mantel des Zylinders (knapp innen, damit er das Höhenbild trifft)
            double cu = (d.breite / 2 - x0) / w, cv = (d.hoehe / 2 - y0) / h;
            double r_ = d.rund_r - 0.5 * std::max(w / W, h / H);
            int n = 360;
            for (int k = 0; k <= n; ++k) {
                double a = 2 * M_PI * k / n;
                rand.push_back({cu + r_ * std::cos(a) / w, cv + r_ * std::sin(a) / h, std::cos(a), std::sin(a), 0});
            }
        } else {
        for (int i = 0; i <= W; i += schritt) rand.push_back({double(i) / W, 0, 0, -1, 0});
        for (int j = 0; j <= H; j += schritt) rand.push_back({1, double(j) / H, 1, 0, 0});
        for (int i = W; i >= 0; i -= schritt) rand.push_back({double(i) / W, 1, 0, 1, 0});
        for (int j = H; j >= 0; j -= schritt) rand.push_back({0, double(j) / H, -1, 0, 0});
        }
        std::vector<float> wv;
        std::vector<GLuint> wi;
        for (size_t k = 0; k < rand.size(); ++k) {
            auto [u, v, nx, ny, _] = rand[k];
            double x = x0 + u * w, y = y0 + v * h;
            float uu = float(std::clamp(u, 0.5 / W, 1 - 0.5 / W)), vv = float(std::clamp(v, 0.5 / H, 1 - 0.5 / H));
            wv.insert(wv.end(), {float(x), float(y), 1, uu, vv, float(nx), float(ny), 0});
            wv.insert(wv.end(), {float(x), float(y), 0, uu, vv, float(nx), float(ny), 0});
            if (k) {
                GLuint a = GLuint((k - 1) * 2), c = GLuint(k * 2);
                wi.insert(wi.end(), {a, a + 1, c, c, a + 1, c + 1});
            }
        }
        vao_wand = vao_mit(vbo_wand, wv, {3, 2, 3});
        glGenBuffers(1, &ibo_wand);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_wand);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, wi.size() * sizeof(GLuint), wi.data(), GL_STATIC_DRAW);
        n_wand = (GLsizei)wi.size();

        // Nullpunkt: Achsenpfeile X, Y, Z am gewählten Nullpunkt (Oberfläche oder Unterseite)
        {
            double L = std::clamp(0.12 * std::max(w, h), 8.0, 60.0), r = L * 0.035;
            std::vector<float> av;
            for (int achse = 0; achse < 3; ++achse) {
                achse_start[achse] = (GLsizei)(av.size() / 6);
                auto ablegen = [&](double lx, double ly, double lz, double nx, double ny, double nz) {
                    double p[3], n[3];
                    // lokale z-Achse → Weltachse
                    if (achse == 0) { p[0] = lz; p[1] = lx; p[2] = ly; n[0] = nz; n[1] = nx; n[2] = ny; }
                    else if (achse == 1) { p[0] = ly; p[1] = lz; p[2] = lx; n[0] = ny; n[1] = nz; n[2] = nx; }
                    else { p[0] = lx; p[1] = ly; p[2] = lz; n[0] = nx; n[1] = ny; n[2] = nz; }
                    p[0] += d.null_x;
                    p[1] += d.null_y;
                    if (d.null_unten) p[2] -= d.dicke;
                    av.insert(av.end(), {float(p[0]), float(p[1]), float(p[2]), float(n[0]), float(n[1]), float(n[2])});
                };
                auto mantel = [&](double z0, double r0, double z1, double r1) {
                    int N = 20;
                    double nz = (r0 - r1) / std::max(1e-9, std::hypot(r0 - r1, z1 - z0));
                    double nr = std::sqrt(std::max(0.0, 1 - nz * nz));
                    for (int k = 0; k < N; ++k) {
                        double a0 = 2 * M_PI * k / N, a1 = 2 * M_PI * (k + 1) / N;
                        auto pk = [&](double ang, double z, double rr) {
                            ablegen(std::cos(ang) * rr, std::sin(ang) * rr, z, std::cos(ang) * nr, std::sin(ang) * nr, nz);
                        };
                        pk(a0, z0, r0); pk(a1, z0, r0); pk(a1, z1, r1);
                        pk(a0, z0, r0); pk(a1, z1, r1); pk(a0, z1, r1);
                    }
                };
                mantel(0, r, L * 0.75, r);
                mantel(L * 0.75, r * 2.6, L, 0.001);
                mantel(L * 0.75, 0.001, L * 0.75, r * 2.6);   // Boden der Spitze
                achse_anzahl[achse] = (GLsizei)(av.size() / 6) - achse_start[achse];
            }
            // Spannmittel: graue Klötze auf dem Material
            auto ecke = [&](double x, double y, double z, double nx, double ny, double nz) {
                av.insert(av.end(), {float(x), float(y), float(z), float(nx), float(ny), float(nz)});
            };
            for (int seite_sm = 1; seite_sm <= 2; ++seite_sm) {
            (seite_sm == 1 ? spann_start : spann_start2) = (GLsizei)(av.size() / 6);
            for (auto const &sm : d.spannmittel) {
                if ((d.zweiseitig ? sm.seite : 1) != seite_sm) continue;
                auto const &P = sm.punkte;
                size_t n = P.size();
                if (n < 3) continue;
                double cx = 0, cy = 0;
                for (auto const &q : P) { cx += q.first; cy += q.second; }
                cx /= n;
                cy /= n;
                double h = sm.hoehe;
                for (size_t i = 0; i < n; ++i) {
                    auto a = P[i], b = P[(i + 1) % n];
                    ecke(cx, cy, h, 0, 0, 1); ecke(a.first, a.second, h, 0, 0, 1); ecke(b.first, b.second, h, 0, 0, 1);
                    double dx = b.first - a.first, dy = b.second - a.second, l = std::hypot(dx, dy);
                    if (l < 1e-9) continue;
                    double nx = dy / l, ny = -dx / l;
                    if ((a.first - cx) * nx + (a.second - cy) * ny < 0) { nx = -nx; ny = -ny; }
                    ecke(a.first, a.second, 0, nx, ny, 0); ecke(b.first, b.second, 0, nx, ny, 0); ecke(b.first, b.second, h, nx, ny, 0);
                    ecke(a.first, a.second, 0, nx, ny, 0); ecke(b.first, b.second, h, nx, ny, 0); ecke(a.first, a.second, h, nx, ny, 0);
                }
            }
            (seite_sm == 1 ? spann_anzahl : spann_anzahl2) =
                (GLsizei)(av.size() / 6) - (seite_sm == 1 ? spann_start : spann_start2);
            }
            // Gewinde: Formteile (die Gänge sind Hinterschneidungen – nicht in den Höhenbildern)
            gewinde_teile.clear();
            for (auto const &g : d.gewinde) {
                GewindeTeil t;
                t.start = (GLsizei)(av.size() / 6);
                gewinde_netz(g, av, d.gewinde.size() > 40 ? 36 : 72);
                t.anzahl = (GLsizei)(av.size() / 6) - t.start;
                t.seite = d.zweiseitig ? g.seite : 1;
                t.programm = g.programm;
                // Farbe der Bearbeitung (wie die übrigen gefrästen Flächen)
                if (g.programm >= 0 && g.programm < (int)d.programme.size()) {
                    for (auto const &a : d.programme[g.programm].abschnitte) {
                        unsigned r, gr, b;
                        if (a.farbe.size() == 7 && std::sscanf(a.farbe.c_str() + 1, "%02x%02x%02x", &r, &gr, &b) == 3) {
                            t.farbe = {r / 255.f, gr / 255.f, b / 255.f};
                            break;
                        }
                    }
                }
                if (t.anzahl > 0) gewinde_teile.push_back(t);
            }
            vao_tisch = vao_mit(vbo_tisch, av, {3, 3});
            n_tisch = (GLsizei)(av.size() / 6);
        }

        // Bahnen: Vorschub farbig (tiefer = dunkler), Eilgänge rot
        std::vector<float> vor, eil;
        double zmin = -0.01;
        for (auto const &m : moves) zmin = std::min(zmin, m.z);
        p = Bewegung{};
        p.z = d.sicherheit;
        prog = moves.empty() ? 0 : moves[0].p;
        n_vor1 = -1;
        n_eil1 = -1;
        for (auto const &m : moves) {
            if (m.p != prog) {
                p = Bewegung{};
                p.z = d.sicherheit;
                prog = m.p;
            }
            if (n_vor1 < 0 && seite_von(m) == 2) {
                n_vor1 = (GLsizei)(vor.size() / 6);
                n_eil1 = (GLsizei)(eil.size() / 6);
            }
            if (m.eil) {
                eil.insert(eil.end(), {float(p.x), float(p.y), float(p.z), 1, 0.25f, 0.25f,
                                       float(m.x), float(m.y), float(m.z), 1, 0.25f, 0.25f});
            } else {
                auto basis = tafel[std::min<size_t>(m.farbe, tafel.size() - 1)];
                for (Bewegung const *q : {static_cast<Bewegung const *>(&p), &m}) {
                    double t = 0.5 * std::clamp(q->z / zmin, 0.0, 1.0);
                    vor.insert(vor.end(), {float(q->x), float(q->y), float(q->z + 0.02),
                                           float(basis[0] + (0.25 - basis[0]) * t), float(basis[1] + (0.10 - basis[1]) * t),
                                           float(basis[2] + (0.0 - basis[2]) * t)});
                }
            }
            p = m;
        }
        vao_vor = vao_mit(vbo_vor, vor, {3, 3});
        n_vor = (GLsizei)(vor.size() / 6);
        vao_eil = vao_mit(vbo_eil, eil, {3, 3});
        n_eil = (GLsizei)(eil.size() / 6);
        if (n_vor1 < 0) {
            n_vor1 = n_vor;
            n_eil1 = n_eil;
        }
        glBindVertexArray(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        neu_aufbauen = false;
        hoehe_neu = true;
        status_setzen();
    }

    /// Werkzeug als Körper (Spitze bei 0, nach oben) – fraeser_netz, dazu der Pfeil in Fahrrichtung (dx, dy)
    void werkzeug_bauen(int i, double dx, double dy, double pz)
    {
        if (i == werkzeug_prog && vao_wz && dx == pfeil_dx && dy == pfeil_dy && pz == pfeil_z) return;
        if (vbo_wz) glDeleteBuffers(1, &vbo_wz);
        if (vao_wz) glDeleteVertexArrays(1, &vao_wz);
        auto const &P = d.programme[i];
        FraeserNetz fn;
        fraeser_netz(P, drehung, fn);
        auto v = std::move(fn.ecken);
        n_wz_schneide = fn.n_schneide;
        n_wz_oben = fn.n_oben;
        std::size_t vor = v.size();
        // Pfeil knapp über der Fräserspitze bzw. der Oberfläche – liegt die Spitze im Material, sähe man ihn nicht
        fraeser_pfeil(P.durchmesser / 2, pz, dx, dy, v);
        n_wz_pfeil = (GLsizei)((v.size() - vor) / 6);
        pfeil_dx = dx;
        pfeil_dy = dy;
        pfeil_z = pz;
        vao_wz = vao_mit(vbo_wz, v, {3, 3});
        n_wz = (GLsizei)(v.size() / 6);
        werkzeug_prog = i;
        glBindVertexArray(0);
    }

    void msaa_puffer(int bw, int bh)
    {
        if (proben <= 0) return;
        if (fbo_ms && ms_w == bw && ms_h == bh) return;
        if (!fbo_ms) {
            glGenFramebuffers(1, &fbo_ms);
            glGenRenderbuffers(1, &rb_farbe);
            glGenRenderbuffers(1, &rb_tiefe);
        }
        glBindRenderbuffer(GL_RENDERBUFFER, rb_farbe);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, proben, GL_RGBA8, bw, bh);
        glBindRenderbuffer(GL_RENDERBUFFER, rb_tiefe);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, proben, GL_DEPTH_COMPONENT24, bw, bh);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_ms);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb_farbe);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb_tiefe);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) proben = 0;
        ms_w = bw;
        ms_h = bh;
    }

    void hoehen_rechnen()
    {
        abtrag.rechnen(bis, anteil);
        hoehe_neu = false;
        if (char const *test = std::getenv("HIRCH_VORSCHAU_TEST")) {
            // Für Tests: Höhen als Textdatei (nx ny zelle x0 y0, dann Werte zeilenweise); dazu die Karte
            // «zuletzt tiefer gemacht von» (gleicher Name + ".karte", Werte zeilenweise)
            std::vector<float> t;
            std::vector<std::uint16_t> k;
            if (abtrag.lesen(1, t, &k)) {
                if (FILE *f = std::fopen(test, "w")) {
                    std::fprintf(f, "%d %d %.6f %.6f %.6f %.6f\n", W, H, w / W, h / H, x0, y0);
                    for (float v : t) std::fprintf(f, "%.4f\n", v);
                    std::fclose(f);
                }
                if (FILE *f = std::fopen((std::string(test) + ".karte").c_str(), "w")) {
                    for (auto v : k) std::fprintf(f, "%u\n", unsigned(v));
                    std::fclose(f);
                }
            }
            if (abtrag.zweiseitig && abtrag.lesen(2, t, &k)) {
                if (FILE *f = std::fopen((std::string(test) + ".seite2").c_str(), "w")) {
                    for (float v : t) std::fprintf(f, "%.4f\n", v);
                    std::fclose(f);
                }
                if (FILE *f = std::fopen((std::string(test) + ".seite2.karte").c_str(), "w")) {
                    for (auto v : k) std::fprintf(f, "%u\n", unsigned(v));
                    std::fclose(f);
                }
            }
        }
    }

    bool zeichnen()
    {
        if (!gl_ok) {
            glClearColor(0.84f, 0.85f, 0.86f, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            if (!gl_fehler.empty() && !meldung.get_visible()) {
#ifdef _WIN32
                meldung.set_text(TR("Die 3D-Ansicht braucht OpenGL 3.3.\n") + gl_fehler +
                                 TR("\nAbhilfe: links «Grafik: Software» wählen und Hirch CAM neu starten."));
#else
                meldung.set_text(TR("Die 3D-Ansicht braucht OpenGL 3.3.\n") + gl_fehler);
#endif
                meldung.show();
            }
            return true;
        }
        GLint ziel_fbo = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &ziel_fbo);
        if (neu_aufbauen) aufbauen();
        if (moves.empty() && d.programme.empty()) {
            glBindFramebuffer(GL_FRAMEBUFFER, ziel_fbo);
            glClearColor(0.84f, 0.85f, 0.86f, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            return true;
        }
        if (hoehe_neu) hoehen_rechnen();

        int sf = bereich.get_scale_factor();
        int bw = bereich.get_allocated_width() * sf, bh = bereich.get_allocated_height() * sf;
        msaa_puffer(bw, bh);
        glBindFramebuffer(GL_FRAMEBUFFER, proben > 0 ? fbo_ms : (GLuint)ziel_fbo);
        glViewport(0, 0, bw, bh);
        glClearColor(0.84f, 0.85f, 0.86f, 1);
        glClearDepth(1.0);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // Hintergrund: ruhiger Verlauf blaugrau → hell
        glDisable(GL_DEPTH_TEST);
        glUseProgram(p_hg);
        glBindVertexArray(vao_leer);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glEnable(GL_DEPTH_TEST);

        V3 auge = ziel + V3{std::cos(hoehenwinkel) * std::cos(azimut), std::cos(hoehenwinkel) * std::sin(azimut),
                            std::sin(hoehenwinkel)} * abstand;
        V3 oben{0, 0, 1};
        if (std::abs(hoehenwinkel) > 1.55) oben = {-std::cos(azimut), -std::sin(azimut), 0};
        double s = std::max(w, h);
        auto pv = mal(perspektive(40, double(bw) / std::max(1, bh), std::max(0.05, abstand * 0.002),
                                  abstand + s * 4 + 1000),
                      blick(auge, ziel, oben));

        int oben_s = oben_seite();
        GLuint t_oben = oben_s == 2 && abtrag.tex_h2 ? abtrag.tex_h2 : abtrag.tex_h, t_wz = oben_s == 2 && abtrag.tex_wz2 ? abtrag.tex_wz2 : abtrag.tex_wz;
        GLuint t_andere = oben_s == 2 ? abtrag.tex_h : abtrag.tex_h2;
        auto gegenseite = [&](GLuint prog_) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, t_andere ? t_andere : t_oben);
            glUniform1i(glGetUniformLocation(prog_, "uAndere"), 2);
            glUniform1i(glGetUniformLocation(prog_, "uMitAndere"), d.zweiseitig && t_andere ? 1 : 0);
            glUniform2f(glGetUniformLocation(prog_, "uSpiegel"), d.um_y ? 1.f : 0.f, d.um_y ? 0.f : 1.f);
            // rundes Material: ausserhalb des Kreises ist kein Material (Ellipse in uv)
            if (d.rund_r > 0 && w > 0 && h > 0)
                glUniform4f(glGetUniformLocation(prog_, "uRund"), float((d.breite / 2 - x0) / w),
                            float((d.hoehe / 2 - y0) / h), float(d.rund_r / w), float(d.rund_r / h));
            else
                glUniform4f(glGetUniformLocation(prog_, "uRund"), 0, 0, 0, 0);
            glActiveTexture(GL_TEXTURE0);
        };
        if (zeig_mat.get_active()) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, t_oben);
            glUseProgram(p_anz);
            gegenseite(p_anz);
            glUniformMatrix4fv(glGetUniformLocation(p_anz, "uPV"), 1, GL_FALSE, pv.data());
            glUniform1i(glGetUniformLocation(p_anz, "uHoehe"), 0);
            glUniform1f(glGetUniformLocation(p_anz, "uHmin"), float(hmin));
            glUniform1f(glGetUniformLocation(p_anz, "uDicke"), float(d.dicke));
            glUniform2f(glGetUniformLocation(p_anz, "uTexel"), 1.0f / W, 1.0f / H);
            glUniform2f(glGetUniformLocation(p_anz, "uZelle"), float(w / W), float(h / H));
            V3 l = norm(V3{-0.6, -1.0, 1.4});
            glUniform3f(glGetUniformLocation(p_anz, "uLicht"), float(l.x), float(l.y), float(l.z));
            glUniform3f(glGetUniformLocation(p_anz, "uAuge"), float(auge.x), float(auge.y), float(auge.z));
            int stufe = !zeig_schatten.get_active() ? 0 : (bewegt || spielt) ? std::min(stufe_bewegt, stufe_ruhe)
                                                                             : stufe_ruhe;
            glUniform1i(glGetUniformLocation(p_anz, "uSchatten"), stufe == 2 ? 32 : stufe == 1 ? 10 : 0);
            glUniform1f(glGetUniformLocation(p_anz, "uSchritt"), float(gitter_schritt));
            // Restmaterial erst am Ende zeigen (sonst wäre beim Abspielen alles Kommende rot)
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, oben_s == 2 && tex_soll2 ? tex_soll2 : tex_soll);
            glUniform1i(glGetUniformLocation(p_anz, "uSoll"), 4);
            glUniform1f(glGetUniformLocation(p_anz, "uSollMax"), float(d.dicke + 10));
            glUniform1i(glGetUniformLocation(p_anz, "uRest"),
                        zeig_rest.get_active() && bis >= (int)moves.size() && anteil <= 0 ? 1 : 0);
            glActiveTexture(GL_TEXTURE0);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, t_wz);
            glUniform1i(glGetUniformLocation(p_anz, "uWz"), 1);
            float wf[48] = {};
            for (size_t i = 0; i < tafel.size() && i < 16; ++i)
                for (int k = 0; k < 3; ++k) wf[i * 3 + k] = tafel[i][k];
            glUniform3fv(glGetUniformLocation(p_anz, "uWzFarbe"), 16, wf);
            glActiveTexture(GL_TEXTURE0);
            glBindVertexArray(vao_gitter);
            glDrawElements(GL_TRIANGLES, n_gitter, GL_UNSIGNED_INT, nullptr);

            // Unterseite: dasselbe Gitter, flach auf dem Boden
            glUseProgram(p_unten);
            gegenseite(p_unten);
            glUniformMatrix4fv(glGetUniformLocation(p_unten, "uPV"), 1, GL_FALSE, pv.data());
            glUniform1i(glGetUniformLocation(p_unten, "uHoehe"), 0);
            glUniform1f(glGetUniformLocation(p_unten, "uHmin"), float(hmin));
            glUniform1f(glGetUniformLocation(p_unten, "uDicke"), float(d.dicke));
            glUniform2f(glGetUniformLocation(p_unten, "uTexel"), 1.0f / W, 1.0f / H);
            glUniform3fv(glGetUniformLocation(p_unten, "uWzFarbe"), 16, wf);
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, oben_s == 2 ? abtrag.tex_wz : (abtrag.tex_wz2 ? abtrag.tex_wz2 : abtrag.tex_wz));
            glUniform1i(glGetUniformLocation(p_unten, "uWzAndere"), 3);
            glActiveTexture(GL_TEXTURE0);
            glDrawElements(GL_TRIANGLES, n_gitter, GL_UNSIGNED_INT, nullptr);

            glUseProgram(p_wand);
            gegenseite(p_wand);
            glUniformMatrix4fv(glGetUniformLocation(p_wand, "uPV"), 1, GL_FALSE, pv.data());
            glUniform1i(glGetUniformLocation(p_wand, "uHoehe"), 0);
            glUniform1f(glGetUniformLocation(p_wand, "uHmin"), float(hmin));
            glUniform1f(glGetUniformLocation(p_wand, "uDicke"), float(d.dicke));
            glBindVertexArray(vao_wand);
            glDrawElements(GL_TRIANGLES, n_wand, GL_UNSIGNED_INT, nullptr);
        }

        GLsizei sp_start = oben_s == 2 ? spann_start2 : spann_start, sp_anz = oben_s == 2 ? spann_anzahl2 : spann_anzahl;
        if (sp_anz > 0) {
            glUseProgram(p_koerper);
            glUniformMatrix4fv(glGetUniformLocation(p_koerper, "uPV"), 1, GL_FALSE, pv.data());
            glUniform3f(glGetUniformLocation(p_koerper, "uVersatz"), 0, 0, 0);
            glUniform1f(glGetUniformLocation(p_koerper, "uAlpha"), 1);
            glUniform3f(glGetUniformLocation(p_koerper, "uFarbe"), 0.48f, 0.50f, 0.53f);
            glBindVertexArray(vao_tisch);
            glDrawArrays(GL_TRIANGLES, sp_start, sp_anz);
        }

        // Gewinde (Formteil), sobald sein Programm gelaufen ist
        if (!gewinde_teile.empty()) {
            glUseProgram(p_koerper);
            glUniformMatrix4fv(glGetUniformLocation(p_koerper, "uPV"), 1, GL_FALSE, pv.data());
            glUniform3f(glGetUniformLocation(p_koerper, "uVersatz"), 0, 0, 0);
            glUniform1f(glGetUniformLocation(p_koerper, "uAlpha"), 1);
            glBindVertexArray(vao_tisch);
            for (auto const &t : gewinde_teile) {
                if (t.seite != oben_s) continue;
                bool fertig = bis >= (int)moves.size() || (bis > 0 && moves[bis - 1].p > t.programm);
                if (!fertig) continue;
                glUniform3f(glGetUniformLocation(p_koerper, "uFarbe"), t.farbe[0], t.farbe[1], t.farbe[2]);
                glDrawArrays(GL_TRIANGLES, t.start, t.anzahl);
            }
        }

        if (zeig_null.get_active() && n_tisch) {
            glUseProgram(p_koerper);
            glUniformMatrix4fv(glGetUniformLocation(p_koerper, "uPV"), 1, GL_FALSE, pv.data());
            glUniform3f(glGetUniformLocation(p_koerper, "uVersatz"), 0, 0, 0.05f);
            glUniform1f(glGetUniformLocation(p_koerper, "uAlpha"), 1);
            float const farben[3][3] = {{0.90f, 0.20f, 0.18f}, {0.20f, 0.72f, 0.25f}, {0.18f, 0.42f, 0.95f}};
            glBindVertexArray(vao_tisch);
            for (int a = 0; a < 3; ++a) {
                glUniform3f(glGetUniformLocation(p_koerper, "uFarbe"), farben[a][0], farben[a][1], farben[a][2]);
                glDrawArrays(GL_TRIANGLES, achse_start[a], achse_anzahl[a]);
            }
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(p_linie);
        glUniformMatrix4fv(glGetUniformLocation(p_linie, "uPV"), 1, GL_FALSE, pv.data());
        glUniform1i(glGetUniformLocation(p_linie, "uEinfarbig"), 0);
        GLsizei v0 = oben_s == 2 ? n_vor1 : 0, v1 = oben_s == 2 ? n_vor : n_vor1;
        GLsizei e0 = oben_s == 2 ? n_eil1 : 0, e1 = oben_s == 2 ? n_eil : n_eil1;
        if (zeig_bahn.get_active() && v1 > v0) {
            glUniform1f(glGetUniformLocation(p_linie, "uAlpha"), 1);
            glBindVertexArray(vao_vor);
            glDrawArrays(GL_LINES, v0, v1 - v0);
        }
        if (zeig_eil.get_active() && e1 > e0) {
            glUniform1f(glGetUniformLocation(p_linie, "uAlpha"), 0.45f);
            glDepthMask(GL_FALSE);
            glBindVertexArray(vao_eil);
            glDrawArrays(GL_LINES, e0, e1 - e0);
            glDepthMask(GL_TRUE);
        }

        // Werkzeug an der aktuellen Stelle (mitten in der angefangenen Bewegung)
        Bewegung wo;
        if (werkzeug_ort(wo) && !d.programme.empty() && seite_von(wo) == oben_s) {
            auto const &m = wo;
            // Pfeil in Fahrrichtung (nur im Vorschub), knapp über der Oberfläche
            double dx = 0, dy = 0;
            if (bis < (int)moves.size() && !moves[bis].eil && bis > 0 && moves[bis - 1].p == moves[bis].p) {
                dx = moves[bis].x - moves[bis - 1].x;
                dy = moves[bis].y - moves[bis - 1].y;
                if (std::hypot(dx, dy) < 1e-6) dx = dy = 0;
            }
            double oben = d.null_unten ? d.dicke : 0.0;
            werkzeug_bauen(m.p, dx, dy, std::max(0.0, oben - m.z) + 0.3);
            glUseProgram(p_koerper);
            glUniformMatrix4fv(glGetUniformLocation(p_koerper, "uPV"), 1, GL_FALSE, pv.data());
            glUniform3f(glGetUniformLocation(p_koerper, "uVersatz"), float(m.x), float(m.y), float(m.z));
            auto farbe = glGetUniformLocation(p_koerper, "uFarbe"), alpha = glGetUniformLocation(p_koerper, "uAlpha");
            GLsizei ns = std::min(n_wz_schneide, n_wz), no = std::min(n_wz_oben, n_wz - ns);
            glBindVertexArray(vao_wz);
            glUniform1f(alpha, 0.85f);
            if (ns > 0) {                       // Schneide eingefärbt (wie eine TiN-Beschichtung)
                glUniform3f(farbe, 0.80f, 0.62f, 0.22f);
                glDrawArrays(GL_TRIANGLES, 0, ns);
            }
            GLsizei np = std::min(n_wz_pfeil, n_wz - ns - no);
            glUniform3f(farbe, 0.78f, 0.80f, 0.82f);
            glDrawArrays(GL_TRIANGLES, ns, n_wz - ns - no - np);
            if (no > 0) {                       // Schaft in der Spannzange (Gesamtlänge): blass
                glUniform1f(alpha, 0.25f);
                glDrawArrays(GL_TRIANGLES, n_wz - no - np, no);
            }
            if (np > 0) {                       // Fahrrichtung
                glUniform1f(alpha, 0.95f);
                glUniform3f(farbe, 0.15f, 0.55f, 0.95f);
                glDrawArrays(GL_TRIANGLES, n_wz - np, np);
            }
        }
        glDisable(GL_BLEND);
        glBindVertexArray(0);

        if (proben > 0) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_ms);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ziel_fbo);
            glBlitFramebuffer(0, 0, bw, bh, 0, 0, bw, bh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, ziel_fbo);
        }
        return true;
    }
};

HirchVorschau::HirchVorschau()
    : _impl(std::make_unique<Impl>(*this))
{
    set_title(TR("Vorschau 3D – Hirch CAM"));
    set_default_size(1200, 800);
    set_icon_name("hirch-cam");
    signal_key_press_event().connect([this](GdkEventKey *e) {
        if (e->keyval == GDK_KEY_Escape) {
            _impl->stopp();
            hide();
            return true;
        }
        return false;
    }, false);
    // Windows 01.11.: während des Abspielens schloss ✕ nicht – erst anhalten, dann schliessen
    signal_delete_event().connect([this](GdkEventAny *) {
        _impl->stopp();
        hide();
        return true;
    }, false);
    signal_hide().connect([this] { _impl->stopp(); });
}

HirchVorschau::~HirchVorschau() = default;

void HirchVorschau::zeigen(VorschauDaten const &daten)
{
    _impl->laden(daten);
}

} // namespace Inkscape::UI::Dialog
