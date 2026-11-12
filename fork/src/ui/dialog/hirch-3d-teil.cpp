// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: 3D – gefrästes Teil für die Ansicht im Hauptfenster: Bewegungen, Netz (RTIN), Kacheln (TeilGL),
 * Gewinde, Vergleich mit dem Modell, Spannmittel und Zonen (aus hirch-3d.cpp, siehe hirch-3d-intern.h)
 */
#include "ui/dialog/hirch-3d-intern.h"

namespace Inkscape::UI::Dialog {
namespace hirch3d {

// ---------------------------------------------------------------------------
// 3D-Ansicht für das Hauptfenster (Umschalter 2D | 3D, Platz im Fenster: Basis). Zeigt die Szene des
// Reiters «3D» dieses Fensters mit eigener Kamera; Klicks wählen dort (Flächen, Fräsergebnis).


std::array<float, 3> hex_farbe(std::string const &hex, std::array<float, 3> ersatz)
{
    unsigned r, g, b;
    if (hex.size() == 7 && hex[0] == '#' && std::sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b) == 3)
        return {r / 255.f, g / 255.f, b / 255.f};
    return ersatz;
}

/// Bewegungen aller Programme mit ihrer Bearbeitung (Index in bearbeitungen) – wie die Vorschau
std::vector<AbtragBewegung> teil_bewegungen(VorschauDaten const &d, std::vector<AbtragBearbeitung> &bearbeitungen)
{
    std::vector<AbtragBewegung> moves;
    bearbeitungen.clear();
    std::vector<std::vector<int>> ab_nr(d.programme.size());
    for (std::size_t i = 0; i < d.programme.size(); ++i)
        for (auto const &a : d.programme[i].abschnitte) {
            std::string id = a.id.empty() ? std::to_string(i) + ":" + a.name : a.id;
            int nr = -1;
            for (std::size_t q = 0; q < bearbeitungen.size(); ++q)
                if (bearbeitungen[q].id == id) nr = int(q);
            if (nr < 0) {
                bearbeitungen.push_back({id, a.farbe});
                nr = int(bearbeitungen.size()) - 1;
            }
            ab_nr[i].push_back(nr);
        }
    for (std::size_t i = 0; i < d.programme.size(); ++i) {
        auto m = tap_lesen(datei_lesen(d.programme[i].datei), int(i), d.sicherheit);
        auto const &A = d.programme[i].abschnitte;
        for (auto &b : m) {
            int k = -1;
            for (std::size_t q = 0; q < A.size(); ++q)
                if (A[q].zeile <= b.zeile) k = int(q);
            b.abschnitt = k;
            b.bearbeitung = k >= 0 ? ab_nr[i][k] : -1;
        }
        moves.insert(moves.end(), m.begin(), m.end());
    }
    return moves;
}

// ---------------------------------------------------------------------------
// Teil als fertiges Netz (Peter 23.10.: auf dem iMac mit Nouveau war die Kachel-Darstellung zäh oder grob).
// Nach dem Rechnen wird aus den Höhenbildern einmal ein angepasstes Dreiecksnetz gebaut (RTIN, «right-
// triangulated irregular network»: Dreiecke werden halbiert, solange ihre Höhe um mehr als den erlaubten
// Fehler vom Raster abweicht) – fein an Kanten, Gravuren und Wänden, grob auf ebenen Flächen. Ein Netz für
// Ober- und Unterseite gemeinsam (geteilt wird, wo eine der beiden Seiten es braucht), Normale und Cavity je
// Ecke; ganz durchgefräste Dreiecke fallen weg. Gerechnet in einem eigenen Thread, gezeichnet in einem
// Durchgang mit einfachem Shader – das kann auch eine schwache Grafikkarte. Beim Abspielen bleibt die
// Kachel-Darstellung (sie liest die Höhenbilder direkt).

char const *const NETZ_VS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aNK;      // Normale (xyz) und Cavity (w), je −1…1
uniform mat4 uPV;
uniform vec4 uMat;
out vec2 vUV;
out vec3 vN;
out float vCav;
out vec3 vPos;
void main() {
  vUV = (aPos.xy - uMat.xy) / uMat.zw;
  vN = aNK.xyz;
  vCav = aNK.w;
  vPos = aPos;
  gl_Position = uPV * vec4(aPos, 1.0);
}
)";

char const *const NETZ_FS = R"(#version 330 core
in vec2 vUV;
in vec3 vN;
in float vCav;
in vec3 vPos;
uniform int uSeite, uUmY, uZwei, uModus, uWahl, uAnz, uMitKlasse, uStege, uCavity;
uniform vec4 uGitter;
uniform vec3 uRechts, uHoch, uVor;
uniform sampler2D uH, uH2, uKarte, uKarte2, uFarben, uSoll, uSteg;
uniform float uHmin, uDicke;
uniform vec2 uSchritt;                 // eine Netzzelle in uv (und etwas mehr): über die Wand hinaus
out vec4 farbe;
vec2 gew(vec2 uv) { return uUmY == 1 ? vec2(1.0 - uv.x, uv.y) : vec2(uv.x, 1.0 - uv.y); }
float h1(vec2 uv) { return max(uHmin + textureLod(uH, uv, 0.0).r * (-uHmin), -uDicke); }
float h2(vec2 uv) {
  if (uZwei == 0) return -uDicke;
  return -uDicke - max(uHmin + textureLod(uH2, gew(uv), 0.0).r * (-uHmin), -uDicke);
}
float hs(vec2 uv) { return uSeite == 2 ? h2(uv) : h1(uv); }
float matcap(vec3 n) {
  vec3 v = vec3(dot(n, uRechts), dot(n, uHoch), dot(n, uVor));
  if (v.z < 0.0) v = -v;
  float d = 0.5 + 0.5 * dot(v, normalize(vec3(-0.35, 0.55, 0.76)));
  float glanz = pow(max(dot(v, normalize(vec3(-0.25, 0.35, 0.90))), 0.0), 28.0);
  float rand = pow(1.0 - v.z, 3.0);
  return 0.26 + 0.70 * d * d + 0.16 * glanz + 0.10 * rand;
}
void main() {
  vec3 n = normalize(vN);
  // an Knicken (Wand, Kante) die Fläche des Dreiecks selbst, sonst die weiche Normale der Ecken
  vec3 nf = normalize(cross(dFdx(vPos), dFdy(vPos)));
  if (dot(nf, n) < 0.0) nf = -nf;
  float weich = smoothstep(0.80, 0.95, dot(nf, n));
  n = normalize(mix(nf, n, weich));
  vec2 xy = vPos.xy;
  bool unten = uSeite == 2;
  vec3 c = vec3(0.86, 0.78, 0.62);
  // steile Wand: Farbe der tieferen (gefrästen) Seite – die Bearbeitung, die die Wand gemacht hat
  vec2 uvk = vUV;
  if (abs(nf.z) < 0.7 && length(nf.xy) > 1e-4) {
    vec2 d = normalize(nf.xy) * uSchritt;
    bool a_tiefer = unten ? hs(vUV + d) > hs(vUV - d) : hs(vUV + d) < hs(vUV - d);
    uvk = a_tiefer ? vUV + d : vUV - d;
  }
  if (uModus == 1) {                   // Bearbeitung, die die Stelle gefräst hat
    float k = unten ? (uZwei == 1 ? texture(uKarte2, gew(uvk)).r : 0.0) : texture(uKarte, uvk).r;
    int i = int(k * 65535.0 + 0.5);
    if (i > 0 && i <= uAnz) {
      vec3 f = texelFetch(uFarben, ivec2(i - 1, 0), 0).rgb;
      c = vec3(0.25) + 0.75 * f;
      if (uWahl > 0 && i != uWahl) c = mix(c, vec3(0.80), 0.55);
    }
  } else if (uModus == 2 && uMitKlasse == 1) {   // Vergleich mit dem Modell
    vec4 so = texture(uSoll, (xy - uGitter.xy) / uGitter.zw);
    int k = 0;
    float h = hs(vUV);                 // Höhe aus dem Höhenbild (wie die Kacheln), nicht die der Ecke
    if (!unten) k = h < so.r - 0.1 ? 2 : h > so.g + 0.5 ? 1 : 0;
    else k = h > so.a + 0.1 ? 2 : h < so.b - 0.5 ? 1 : 0;
    c = k == 2 ? vec3(0.88, 0.15, 0.14) : k == 1 ? vec3(0.95, 0.60, 0.15) : vec3(0.78, 0.79, 0.81);
  }
  if (uStege == 1) {
    vec3 st = texture(uSteg, (xy - uGitter.xy) / uGitter.zw).rgb;
    if (st.r > 0.5 && vPos.z >= st.g - 0.05 && vPos.z <= st.b + 0.05) c = mix(c, vec3(1.0, 0.80, 0.10), 0.85);
  }
  float l = matcap(n);
  if (uCavity == 1 && abs(n.z) > 0.7) l *= 1.0 + 0.45 * weich * vCav;   // nicht an Wänden
  farbe = vec4(c * l, 1.0);
}
)";


namespace {
/// RTIN (nach «Martini»): Fehler je Rasterpunkt, Dreiecke abwärts
struct Rtin {
    int n = 0;                               // Gitter n × n, n = 2^k + 1
    std::vector<float> fehler;
    /// Fehler über mehrere Kanäle (Höhe oben, unten, Cavity …): je die grösste Abweichung
    void fehler_rechnen(std::vector<std::vector<float> const *> const &kanaele)
    {
        int ts = n - 1;
        long anzahl = long(ts) * ts * 2 - 2, eltern = anzahl - long(ts) * ts;
        fehler.assign(std::size_t(n) * n, 0.f);
        for (long i = anzahl - 1; i >= 0; --i) {
            long id = i + 2;
            int ax = 0, ay = 0, bx = 0, by = 0, cx = 0, cy = 0;
            if (id & 1) bx = by = cx = ts;
            else ax = ay = cy = ts;
            while ((id >>= 1) > 1) {
                int mx = (ax + bx) >> 1, my = (ay + by) >> 1;
                if (id & 1) {
                    bx = ax;
                    by = ay;
                    ax = cx;
                    ay = cy;
                } else {
                    ax = bx;
                    ay = by;
                    bx = cx;
                    by = cy;
                }
                cx = mx;
                cy = my;
            }
            int mx = (ax + bx) >> 1, my = (ay + by) >> 1;
            std::size_t ia = std::size_t(ay) * n + ax, ib = std::size_t(by) * n + bx, im = std::size_t(my) * n + mx;
            float e = 0;
            for (auto const *c : kanaele) e = std::max(e, std::abs(0.5f * ((*c)[ia] + (*c)[ib]) - (*c)[im]));
            float &f = fehler[im];
            f = std::max(f, e);
            if (i < eltern) {
                int ccx = mx + my - ay, ccy = my + ax - mx;     // Spitze des Dreiecks
                std::size_t il = std::size_t((ay + ccy) >> 1) * n + ((ax + ccx) >> 1);
                std::size_t ir = std::size_t((by + ccy) >> 1) * n + ((bx + ccx) >> 1);
                f = std::max({f, fehler[il], fehler[ir]});
            }
        }
    }
    template <typename F>
    void dreiecke(float grenze, F &&aus) const
    {
        std::function<void(int, int, int, int, int, int)> geh = [&](int ax, int ay, int bx, int by, int cx, int cy) {
            int mx = (ax + bx) >> 1, my = (ay + by) >> 1;
            if (std::abs(ax - cx) + std::abs(ay - cy) > 1 && fehler[std::size_t(my) * n + mx] > grenze) {
                geh(cx, cy, ax, ay, mx, my);
                geh(bx, by, cx, cy, mx, my);
            } else {
                aus(ax, ay, bx, by, cx, cy);
            }
        };
        int m = n - 1;
        geh(0, 0, m, m, m, 0);
        geh(m, m, 0, 0, 0, m);
    }
};
}

void netz_rechnen(NetzAuftrag &A)
{
    gint64 t0 = g_get_monotonic_time();
    int W = A.W, H = A.H;
    if (W < 2 || H < 2 || A.oben.size() != std::size_t(W) * H) {
        A.fertig = true;
        return;
    }
    int s = std::max(1, int(std::ceil(std::max(W, H) / 2048.0)));     // höchstens 2049 × 2049 Gitterpunkte
    int gx = (W + s - 1) / s, gy = (H + s - 1) / s;
    A.schritt = s;
    int n = 3;
    while (n - 1 < std::max(gx, gy)) n = (n - 1) * 2 + 1;
    std::size_t N = std::size_t(n) * n;
    float D = float(A.D);
    std::vector<float> o(N), u(N);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            int ci = std::min(i, gx - 1), cj = std::min(j, gy - 1);
            float mo = 1e9f, mu = -1e9f;
            for (int q = cj * s; q < std::min(H, cj * s + s); ++q)
                for (int p = ci * s; p < std::min(W, ci * s + s); ++p) {
                    std::size_t k = std::size_t(q) * W + p;
                    mo = std::min(mo, std::max(A.oben[k], -D));     // tiefste Stelle je Block
                    mu = std::max(mu, std::min(A.unten[k], 0.f));
                }
            o[std::size_t(j) * n + i] = mo;
            u[std::size_t(j) * n + i] = std::min(mu, mo);
        }
    // Durchbrüche: eine durchgefräste Ecke neben Material auf die mittlere Höhe der Nachbarn – so schliessen
    // Ober- und Unterseite die Wand genau zwischen Unter- und Oberkante des Teils (sonst ginge sie bis −D)
    std::vector<char> leer(N, 0);
    for (std::size_t k = 0; k < N; ++k) leer[k] = o[k] <= u[k] + 0.01f;
    {
        std::vector<float> o2 = o;
        for (int j = 0; j < std::min(gy, n); ++j)
            for (int i = 0; i < std::min(gx, n); ++i) {
                std::size_t k = std::size_t(j) * n + i;
                if (!leer[k]) continue;
                float summe = 0;
                int z = 0;
                for (int dj = -1; dj <= 1; ++dj)
                    for (int di = -1; di <= 1; ++di) {
                        int a = i + di, b = j + dj;
                        if (a < 0 || b < 0 || a >= std::min(gx, n) || b >= std::min(gy, n)) continue;
                        std::size_t q = std::size_t(b) * n + a;
                        if (leer[q]) continue;
                        summe += 0.5f * (o[q] + u[q]);
                        ++z;
                    }
                if (z) o2[k] = summe / z;
            }
        for (std::size_t k = 0; k < N; ++k)
            if (leer[k]) o[k] = u[k] = o2[k];
        for (int j = 0; j < n; ++j)                    // Rand des Gitters (bis 2^k + 1): wie die letzte Zelle
            for (int i = 0; i < n; ++i) {
                if (i < gx && j < gy) continue;
                std::size_t k = std::size_t(j) * n + i, kc = std::size_t(std::min(j, gy - 1)) * n + std::min(i, gx - 1);
                o[k] = o[kc];
                u[k] = u[kc];
                leer[k] = leer[kc];
            }
    }
    if (A.abbrechen) return;
    Rtin r;
    r.n = n;
    // Cavity je Gitterpunkt und Seite (Senke dunkler, Grat heller; an Wandkanten keine – die Wand zeichnet die
    // Kante). Sie zählt beim Teilen mit: sonst verschmiert sie über grosse Dreiecke (Sägezahn an Gravuren).
    auto h = [&](std::vector<float> const &f, int a, int b) {
        return f[std::size_t(std::clamp(b, 0, std::min(gy, n) - 1)) * n + std::clamp(a, 0, std::min(gx, n) - 1)];
    };
    std::vector<float> cav[2] = {std::vector<float>(N, 0.f), std::vector<float>(N, 0.f)};
    for (int seite = 0; seite < 2; ++seite) {
        auto const &f = seite == 0 ? o : u;
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                float mitte = h(f, i, j);
                float xv = (h(f, i + 1, j) - mitte) / float(A.zx * s), xr = (mitte - h(f, i - 1, j)) / float(A.zx * s);
                float yv = (h(f, i, j + 1) - mitte) / float(A.zy * s), yr = (mitte - h(f, i, j - 1)) / float(A.zy * s);
                if (std::max({std::abs(xv), std::abs(xr), std::abs(yv), std::abs(yr)}) > 2.5f) continue;
                float rund = 0.25f * (h(f, i + 1, j) + h(f, i - 1, j) + h(f, i, j + 1) + h(f, i, j - 1));
                float c = (mitte - rund) / float(std::max(A.zx, A.zy) * s);
                if (seite == 1) c = -c;
                cav[seite][std::size_t(j) * n + i] = std::clamp(c * 1.6f, -1.f, 1.f);
            }
    }
    if (A.abbrechen) return;
    // im Fehler: 0,05 Helligkeitsstufe der Cavity zählt wie 0,005 mm Höhe
    std::vector<float> cav_f[2] = {cav[0], cav[1]};
    for (auto &c : cav_f)
        for (auto &v : c) v *= 0.1f;
    r.fehler_rechnen({&o, &u, &cav_f[0], &cav_f[1]});
    cav_f[0] = {};
    cav_f[1] = {};
    if (A.abbrechen) return;
    // Fehlergrenze: so fein wie möglich im Rahmen des Budgets
    float grenze = 0.005f;
    for (int versuch = 0; versuch < 30; ++versuch) {
        std::size_t z = 0;
        r.dreiecke(grenze, [&](int, int, int, int, int, int) { ++z; });
        if (z <= A.budget) break;
        grenze *= 1.5f;
    }
    if (A.abbrechen) return;
    double zx = A.zx * s, zy = A.zy * s;
    auto lage_x = [&](int i) { return float(A.x0 + (std::min(i, gx - 1) + 0.5) * zx); };
    auto lage_y = [&](int j) { return float(A.y0 + (std::min(j, gy - 1) + 0.5) * zy); };
    auto durch = [&](std::size_t k) { return leer[k] != 0; };
    auto drin = [&](int i, int j) {
        if (A.rund_r <= 0) return true;
        return std::hypot(lage_x(i) - A.cx, lage_y(j) - A.cy) <= A.rund_r;
    };
    std::vector<int> nr(N, -1);
    auto ecke = [&](int i, int j) -> unsigned {
        std::size_t k = std::size_t(j) * n + i;
        if (nr[k] >= 0) return unsigned(nr[k]);
        nr[k] = int(A.pos[0].size() / 3);
        float x = lage_x(i), y = lage_y(j);
        for (int seite = 0; seite < 2; ++seite) {
            auto const &f = seite == 0 ? o : u;
            // einseitig, die flachere Seite (minmod): eine Ecke an der Kante einer Wand bekommt die Normale der
            // Fläche, nicht der Wand – sonst verschmiert der Glanz über die grossen Dreiecke daneben (Sägezahn)
            float mitte = h(f, i, j);
            float xv = (h(f, i + 1, j) - mitte) / float(zx), xr = (mitte - h(f, i - 1, j)) / float(zx);
            float yv = (h(f, i, j + 1) - mitte) / float(zy), yr = (mitte - h(f, i, j - 1)) / float(zy);
            auto minmod = [](float p, float q) { return p * q <= 0 ? 0.f : std::abs(p) < std::abs(q) ? p : q; };
            float gxv = minmod(xv, xr), gyv = minmod(yv, yr);
            float nx, ny, nz;
            if (seite == 0) {
                nx = -gxv;
                ny = -gyv;
                nz = 1;
            } else {
                nx = gxv;
                ny = gyv;
                nz = -1;
            }
            float l = std::sqrt(nx * nx + ny * ny + nz * nz);
            float cv = cav[seite][k];
            A.pos[seite].insert(A.pos[seite].end(), {x, y, f[k]});
            auto b8 = [](float v) { return std::int8_t(std::lround(std::clamp(v, -1.f, 1.f) * 127)); };
            A.nk[seite].insert(A.nk[seite].end(), {b8(nx / l), b8(ny / l), b8(nz / l), b8(cv)});
        }
        return unsigned(nr[k]);
    };
    r.dreiecke(grenze, [&](int ax, int ay, int bx, int by, int cx, int cy) {
        std::size_t ka = std::size_t(ay) * n + ax, kb = std::size_t(by) * n + bx, kc = std::size_t(cy) * n + cx;
        if (durch(ka) && durch(kb) && durch(kc)) return;                  // ganz durch: hier ist nichts
        if (ax >= gx && bx >= gx && cx >= gx) return;                     // ausserhalb des Rasters
        if (ay >= gy && by >= gy && cy >= gy) return;
        if (!drin(ax, ay) && !drin(bx, by) && !drin(cx, cy)) return;      // rund: ausserhalb des Kreises
        A.idx.push_back(ecke(ax, ay));
        A.idx.push_back(ecke(bx, by));
        A.idx.push_back(ecke(cx, cy));
    });
    A.fehler = grenze;
    A.sekunden = (g_get_monotonic_time() - t0) / 1e6;
    A.fertig = true;
}


/// Teil als Körper aus den zwei Höhenbildern von hirch-abtrag: Ober- und Unterseite (Seite 2 gewendet
/// in die Lage von Seite 1), Wände an den Rändern und Durchbrüchen; Farbe je Ecke nach der Bearbeitung,
/// die die Stelle gefräst hat (Karte), sonst neutral. Höchstens ~400 000 Punkte je Seite (gröber: die
/// tiefste Stelle je Block, damit keine Rille verschwindet). Ecken: x, y, z, nx, ny, nz, r, g, b.
void teil_bauen(VorschauDaten const &d, HirchAbtrag &ab, std::vector<float> &e, std::vector<unsigned> &idx,
                TeilRaster &R, bool netz, double max_punkte,
                NetzAuftrag *netz_aus)
{
    e.clear();
    idx.clear();
    std::vector<float> h1, h2;
    std::vector<std::uint16_t> k1, k2;
    if (!ab.lesen(1, h1, &k1)) return;
    int W = ab.W, H = ab.H;
    bool zwei = d.zweiseitig && ab.tex_h2 && ab.lesen(2, h2, &k2);
    double D = d.dicke;
    // Seite 2 in die Lage von Seite 1: gewendet um X (Zeilen) oder um Y (Spalten); Tiefe von unten
    std::vector<float> unten(std::size_t(W) * H, float(-D));
    std::vector<std::uint16_t> ku(std::size_t(W) * H, 0);
    if (zwei)
        for (int j = 0; j < H; ++j)
            for (int i = 0; i < W; ++i) {
                int qi = d.um_y ? W - 1 - i : i, qj = d.um_y ? j : H - 1 - j;
                std::size_t p = std::size_t(j) * W + i, q = std::size_t(qj) * W + qi;
                unten[p] = float(-D - h2[q]);
                ku[p] = k2[q];
            }
    if (netz_aus) {                         // Höhen für das fertige Netz (eigener Thread)
        netz_aus->W = W;
        netz_aus->H = H;
        netz_aus->x0 = ab.x0;
        netz_aus->y0 = ab.y0;
        netz_aus->zx = ab.w / W;
        netz_aus->zy = ab.h / H;
        netz_aus->D = D;
        netz_aus->rund_r = d.rund_r;
        netz_aus->cx = d.breite / 2;
        netz_aus->cy = d.hoehe / 2;
        netz_aus->oben = h1;
        netz_aus->unten = unten;
    }
    int s = std::max(1, int(std::ceil(std::sqrt(double(W) * H / max_punkte))));
    int nx = (W + s - 1) / s, ny = (H + s - 1) / s;
    double zx = ab.w / W, zy = ab.h / H;
    std::size_t n = std::size_t(nx) * ny;
    std::vector<float> o(n), u(n);
    std::vector<std::uint16_t> ko(n), kun(n);
    std::vector<char> fest(n, 0);
    double cx = d.breite / 2, cy = d.hoehe / 2;
    for (int J = 0; J < ny; ++J)
        for (int I = 0; I < nx; ++I) {
            float mo = 1e9f, mu = -1e9f;
            std::uint16_t a = 0, b = 0;
            for (int j = J * s; j < std::min(H, J * s + s); ++j)
                for (int i = I * s; i < std::min(W, I * s + s); ++i) {
                    std::size_t p = std::size_t(j) * W + i;
                    if (h1[p] < mo) {
                        mo = h1[p];
                        a = k1[p];
                    }
                    if (unten[p] > mu) {
                        mu = unten[p];
                        b = ku[p];
                    }
                }
            std::size_t P = std::size_t(J) * nx + I;
            o[P] = mo;
            u[P] = mu;
            ko[P] = a;
            kun[P] = b;
            double x = ab.x0 + (I * s + 0.5 * s) * zx, y = ab.y0 + (J * s + 0.5 * s) * zy;
            bool drin = d.rund_r <= 0 || std::hypot(x - cx, y - cy) <= d.rund_r;
            fest[P] = drin && mo > mu + 0.01f;
        }
    std::vector<std::array<float, 3>> farben;
    for (auto const &bb : ab.bearbeitungen) farben.push_back(hex_farbe(bb.farbe, {0.86f, 0.78f, 0.62f}));
    std::array<float, 3> const neutral{0.86f, 0.78f, 0.62f};
    auto farbe = [&](std::uint16_t k) {
        if (k == 0 || k > farben.size()) return neutral;
        auto f = farben[k - 1];             // etwas heller, damit die Form lesbar bleibt
        return std::array<float, 3>{0.25f + 0.75f * f[0], 0.25f + 0.75f * f[1], 0.25f + 0.75f * f[2]};
    };
    R.nx = nx;
    R.ny = ny;
    R.x0 = ab.x0 + 0.5 * s * zx;
    R.y0 = ab.y0 + 0.5 * s * zy;
    R.dx = s * zx;
    R.dy = s * zy;
    R.o = o;
    R.u = u;
    R.fest = fest;
    if (!netz) return;                      // nur das Raster (Linien, Klick); gezeichnet wird aus der Textur
    auto X = [&](int I) { return float(ab.x0 + (I * s + 0.5 * s) * zx); };
    auto Y = [&](int J) { return float(ab.y0 + (J * s + 0.5 * s) * zy); };
    auto ix = [nx](int I, int J) { return std::size_t(J) * nx + I; };
    e.reserve(2 * n * 9);
    for (int seite = 0; seite < 2; ++seite) {
        auto const &h = seite == 0 ? o : u;
        auto const &k = seite == 0 ? ko : kun;
        float vz = seite == 0 ? 1.f : -1.f;
        for (int J = 0; J < ny; ++J)
            for (int I = 0; I < nx; ++I) {
                int i0 = std::max(I - 1, 0), i1 = std::min(I + 1, nx - 1), j0 = std::max(J - 1, 0), j1 = std::min(J + 1, ny - 1);
                float gx = (h[ix(i1, J)] - h[ix(i0, J)]) / float(zx * s * std::max(i1 - i0, 1));
                float gy = (h[ix(I, j1)] - h[ix(I, j0)]) / float(zy * s * std::max(j1 - j0, 1));
                auto f = farbe(k[ix(I, J)]);
                e.insert(e.end(), {X(I), Y(J), h[ix(I, J)], -gx * vz, -gy * vz, vz, f[0], f[1], f[2]});
            }
    }
    auto quad = [&](int I, int J) {          // Viereck (I, J)…(I+1, J+1) ganz aus Material?
        return I >= 0 && J >= 0 && I + 1 < nx && J + 1 < ny && fest[ix(I, J)] && fest[ix(I + 1, J)] &&
               fest[ix(I + 1, J + 1)] && fest[ix(I, J + 1)];
    };
    unsigned basis = unsigned(n);
    for (int J = 0; J + 1 < ny; ++J)
        for (int I = 0; I + 1 < nx; ++I) {
            if (!quad(I, J)) continue;
            unsigned a = unsigned(ix(I, J)), b = unsigned(ix(I + 1, J)), c = unsigned(ix(I + 1, J + 1)),
                     dd = unsigned(ix(I, J + 1));
            idx.insert(idx.end(), {a, b, c, a, c, dd});
            idx.insert(idx.end(), {basis + a, basis + c, basis + b, basis + a, basis + dd, basis + c});
        }
    // Wände: wo an einer Kante nur auf einer Seite ein Viereck liegt, von oben nach unten schliessen
    auto wand = [&](int Ia, int Ja, int Ib, int Jb, float nxw, float nyw) {
        unsigned v = unsigned(e.size() / 9);
        auto ecke = [&](int I, int J, float z, std::uint16_t k) {
            auto f = farbe(k);
            e.insert(e.end(), {X(I), Y(J), z, nxw, nyw, 0.f, f[0], f[1], f[2]});
        };
        ecke(Ia, Ja, o[ix(Ia, Ja)], ko[ix(Ia, Ja)]);
        ecke(Ib, Jb, o[ix(Ib, Jb)], ko[ix(Ib, Jb)]);
        ecke(Ib, Jb, u[ix(Ib, Jb)], kun[ix(Ib, Jb)]);
        ecke(Ia, Ja, u[ix(Ia, Ja)], kun[ix(Ia, Ja)]);
        idx.insert(idx.end(), {v, v + 1, v + 2, v, v + 2, v + 3});
    };
    for (int J = 0; J < ny; ++J)
        for (int I = 0; I + 1 < nx; ++I) {               // waagrechte Kante (I, J)–(I+1, J)
            bool unter = quad(I, J - 1), ueber = quad(I, J);
            if (unter != ueber && fest[ix(I, J)] && fest[ix(I + 1, J)]) wand(I, J, I + 1, J, 0.f, ueber ? -1.f : 1.f);
        }
    for (int J = 0; J + 1 < ny; ++J)
        for (int I = 0; I < nx; ++I) {                   // senkrechte Kante (I, J)–(I, J+1)
            bool links = quad(I - 1, J), rechts = quad(I, J);
            if (links != rechts && fest[ix(I, J)] && fest[ix(I, J + 1)]) wand(I, J, I, J + 1, rechts ? -1.f : 1.f, 0.f);
        }
}

/// Formteile Gewinde in der Lage von Seite 1 (Seite 2 gewendet)
/// enden (wenn gesetzt): je Gewinde (Programm, nach dem es fertig ist; Ecken bis hier) – nach Programm geordnet
void gewinde_bauen(VorschauDaten const &d, std::vector<float> &aus,
                   std::vector<std::pair<int, int>> *enden)
{
    aus.clear();
    if (enden) enden->clear();
    std::vector<AbtragGewinde const *> reihe;
    for (auto const &g : d.gewinde) reihe.push_back(&g);
    auto nach = [](AbtragGewinde const *g) { return g->programm < 0 ? INT_MAX : g->programm; };   // unbekannt: zuletzt
    std::stable_sort(reihe.begin(), reihe.end(), [&](auto a, auto b) { return nach(a) < nach(b); });
    for (auto gp : reihe) {
        auto const &g = *gp;
        std::vector<float> av;
        gewinde_netz(g, av, d.gewinde.size() > 40 ? 36 : 72);
        if (g.seite == 2 && d.zweiseitig)
            for (std::size_t k = 0; k + 5 < av.size(); k += 6) {
                if (d.um_y) {
                    av[k] = float(d.breite - av[k]);
                    av[k + 3] = -av[k + 3];
                } else {
                    av[k + 1] = float(d.hoehe - av[k + 1]);
                    av[k + 4] = -av[k + 4];
                }
                av[k + 2] = float(-d.dicke - av[k + 2]);
                av[k + 5] = -av[k + 5];
            }
        aus.insert(aus.end(), av.begin(), av.end());
        if (enden) enden->push_back({g.programm, int(aus.size() / 6)});
    }
}

// ---------------------------------------------------------------------------
// Gefrästes Teil direkt aus den Höhenbildern von hirch-abtrag zeichnen (Peter, 22.10.: «wie Nanite»).
// Höhenbilder sind ein regelmässiges Raster – darum Kacheln in einem Quadtree (CDLOD): nah an der
// Kamera feine Kacheln (ein Gitterpunkt je Rasterzelle), weiter weg grobe; die Höhe liest der
// Vertex-Shader aus der Textur, die Normale der Fragment-Shader je Bildpunkt (feine Rillen auch auf
// groben Kacheln). Fugen zwischen Stufen verdecken Schürzen. Kein Netz auf der CPU, nichts
// hochzuladen: beim Abspielen schreibt hirch-abtrag in die Textur und die Ansicht zeigt sie im
// selben Bild.

char const *const TEIL_VS = R"(#version 330 core
layout(location = 0) in vec2 aUV;      // in der Kachel 0…1 (Wand: Material-mm)
layout(location = 1) in float aRand;   // Kachel: 1 = Schürze; Wand: 1 = oben, 0 = unten
layout(location = 2) in vec2 aN;       // Wand: Normale aussen
layout(location = 3) in vec4 aKnoten;  // je Kachel (Instanz): x0, y0, Grösse, Schürze (mm)
uniform vec4 uMat;                     // Raster: x0, y0, w, h
uniform float uHmin, uDicke, uZelle;
uniform int uSeite, uUmY, uWand, uZwei, uStufen;
uniform sampler2D uH, uH2, uMin, uMin2;   // Höhen; Pyramide «tiefste Stelle je Block» (Stufe 1 …)
uniform mat4 uPV;
out vec2 vUV;
out vec3 vN;
out vec3 vPos;
out float vRand;
invariant gl_Position;                 // Vorpass und Hauptpass: genau dieselbe Tiefe (GL_EQUAL)
vec2 gew(vec2 uv) { return uUmY == 1 ? vec2(1.0 - uv.x, uv.y) : vec2(uv.x, 1.0 - uv.y); }
float roh(sampler2D t, sampler2D m, vec2 uv, int st) {
  float v = st <= 0 ? textureLod(t, uv, 0.0).r : textureLod(m, uv, float(min(st, uStufen) - 1)).r;
  return max(uHmin + v * (-uHmin), -uDicke);
}
float h1(vec2 uv, int st) { return roh(uH, uMin, uv, st); }
float h2(vec2 uv, int st) {
  if (uZwei == 0) return -uDicke;
  return -uDicke - roh(uH2, uMin2, gew(uv), st);
}
void main() {
  vec2 xy;
  float z;
  if (uWand == 1) {
    xy = aUV;
    vec2 uv = clamp((xy - uMat.xy) / uMat.zw, vec2(0.0), vec2(1.0));
    vUV = uv;
    z = aRand > 0.5 ? h1(uv, 0) : h2(uv, 0);
    vN = vec3(aN, 0.0);
  } else {
    xy = clamp(aKnoten.xy + aUV * aKnoten.z, uMat.xy, uMat.xy + uMat.zw);
    vec2 uv = (xy - uMat.xy) / uMat.zw;
    vUV = uv;
    // grobe Kachel: tiefste Stelle des Blocks (Block ≥ Abstand der Gitterpunkte) – keine Rille fällt durch
    float abst = aKnoten.z / 32.0;
    int st = abst > uZelle * 1.01 ? int(ceil(log2(abst / uZelle))) : 0;
    z = uSeite == 2 ? h2(uv, st) : h1(uv, st);
    // Schürze: nicht über die andere Seite des Teils hinaus (sonst schaut sie dort durch)
    if (aRand > 0.5) z = uSeite == 2 ? min(z + aKnoten.w, -0.01) : max(z - aKnoten.w, -uDicke + 0.01);
    vN = vec3(0.0);
  }
  vPos = vec3(xy, z);
  vRand = uWand == 1 ? 0.0 : aRand;
  gl_Position = uPV * vec4(xy, z, 1.0);
}
)";

char const *const TEIL_FS = R"(#version 330 core
in vec2 vUV;
in vec3 vN;
in vec3 vPos;
in float vRand;
uniform vec3 uAuge;
uniform vec4 uMat;
uniform float uHmin, uDicke;
uniform int uSeite, uUmY, uWand, uZwei, uModus, uWahl, uAnz, uMitKlasse;
uniform vec2 uTexel;                   // 1/W, 1/H
uniform vec3 uRund;                    // Mitte x, y, Radius (0: Platte)
uniform vec3 uLicht;
uniform vec3 uRechts, uHoch, uVor;     // Kamera: rechts, oben, zur Kamera hin (Matcap hängt an der Kamera)
uniform sampler2D uH, uH2, uKarte, uKarte2, uFarben, uSoll, uSteg;
uniform vec4 uGitter;                  // Raster für Soll und Stege: x0, y0, Breite, Höhe (mm)
uniform int uStege, uCavity, uVorpass;
out vec4 farbe;
// Matcap «Ton»: Helligkeit nur aus der Richtung der Fläche zur Kamera – oben und unten gleich gut lesbar
float matcap(vec3 n) {
  vec3 v = vec3(dot(n, uRechts), dot(n, uHoch), dot(n, uVor));
  if (v.z < 0.0) v = -v;
  float d = 0.5 + 0.5 * dot(v, normalize(vec3(-0.35, 0.55, 0.76)));
  float glanz = pow(max(dot(v, normalize(vec3(-0.25, 0.35, 0.90))), 0.0), 28.0);
  float rand = pow(1.0 - v.z, 3.0);
  return 0.26 + 0.70 * d * d + 0.16 * glanz + 0.10 * rand;
}
float h1(vec2 uv) { return max(uHmin + textureLod(uH, uv, 0.0).r * (-uHmin), -uDicke); }
vec2 gew(vec2 uv) { return uUmY == 1 ? vec2(1.0 - uv.x, uv.y) : vec2(uv.x, 1.0 - uv.y); }
float h2(vec2 uv) {
  if (uZwei == 0) return -uDicke;
  return -uDicke - max(uHmin + textureLod(uH2, gew(uv), 0.0).r * (-uHmin), -uDicke);
}
float hs(vec2 uv) { return uSeite == 2 ? h2(uv) : h1(uv); }
void main() {
  vec2 xy = uMat.xy + vUV * uMat.zw;
  // steile Stellen (Wände zwischen zwei Rasterhöhen): die Normale der Fläche selbst
  vec3 ng = normalize(cross(dFdx(vPos), dFdy(vPos)));
  if (dot(ng, uAuge - vPos) < 0.0) ng = -ng;
  bool steil = uWand == 0 && abs(ng.z) < 0.7;
  // Vorpass: nur die Tiefe, hier fällt weg, was nicht da ist. Der Hauptpass zeichnet mit GL_EQUAL und ohne
  // discard – so prüft die Grafikkarte die Tiefe vor dem Shader, und jeder Bildpunkt wird nur einmal schattiert
  // (Peter, 22.10.: iMac mit Nouveau stockte)
  if (uVorpass == 1) {
    if (uRund.z > 0.0 && distance(xy, uRund.xy) > uRund.z) discard;
    bool durch = h1(vUV) <= h2(vUV) + 0.01;         // ganz durch: hier ist nichts …
    if (durch && (uWand == 1 || vRand > 0.5 || !steil)) discard;   // … ausser der Wand, die hinunterführt
    farbe = vec4(0.0);
    return;
  }
  vec3 n;
  // Nachbarn je ein Bildpunkt (mind. 1,5 Zellen): Normale und Cavity aus denselben vier Werten
  vec2 r = max(uTexel * 1.5, fwidth(vUV) * 1.5);
  float hx1 = 0.0, hx0 = 0.0, hy1 = 0.0, hy0 = 0.0;
  if (uWand == 1) {
    n = normalize(vN);
  } else {
    hx1 = hs(vUV + vec2(r.x, 0.0));
    hx0 = hs(vUV - vec2(r.x, 0.0));
    hy1 = hs(vUV + vec2(0.0, r.y));
    hy0 = hs(vUV - vec2(0.0, r.y));
    float gx = (hx1 - hx0) / (2.0 * r.x * uMat.z);
    float gy = (hy1 - hy0) / (2.0 * r.y * uMat.w);
    n = uSeite == 2 ? normalize(vec3(gx, gy, -1.0)) : normalize(vec3(-gx, -gy, 1.0));
    if (steil) n = ng;
  }
  vec3 holz = vec3(0.86, 0.78, 0.62);
  vec3 c = holz;
  bool unten = uSeite == 2 || (uWand == 1 && false);
  // Aussenwand: unberührte Seite des Rohteils – Holz; nur ein schmaler Streifen an der Kante (oben bzw. unten)
  // trägt die Farbe der Fläche darüber (Peter, 22.10.: Tasche über den Rand färbte die ganze Seite)
  int band = 0;
  if (uWand == 1) band = vPos.z > h1(vUV) - 0.3 ? 1 : (uZwei == 1 && vPos.z < h2(vUV) + 0.3) ? 2 : 0;
  // Schürzen (Fugen zwischen Stufen, am Rand des Rohteils in der Ebene der Aussenwand): wie die Wand
  bool schuerze = uWand == 0 && vRand > 0.5 && abs(vPos.z - hs(vUV)) > 0.3;
  // steile Stelle: Farbe der tieferen (gefrästen) Seite – die Bearbeitung, die die Wand gemacht hat (wie im Netz)
  vec2 uvk = vUV;
  if (steil && length(ng.xy) > 1e-4) {
    vec2 d = normalize(ng.xy) * max(uTexel * 1.5, fwidth(vUV));
    bool a_tiefer = unten ? hs(vUV + d) > hs(vUV - d) : hs(vUV + d) < hs(vUV - d);
    uvk = a_tiefer ? vUV + d : vUV - d;
  }
  if (uModus == 1 && (uWand == 0 || band > 0) && !schuerze) {   // Bearbeitung, die die Stelle gefräst hat
    bool u2 = uWand == 1 ? band == 2 : unten;
    float k = u2 ? (uZwei == 1 ? texture(uKarte2, gew(uvk)).r : 0.0) : texture(uKarte, uvk).r;
    int i = int(k * 65535.0 + 0.5);
    if (i > 0 && i <= uAnz) {
      vec3 f = texelFetch(uFarben, ivec2(i - 1, 0), 0).rgb;
      c = vec3(0.25) + 0.75 * f;
      if (uWahl > 0 && i != uWahl) c = mix(c, vec3(0.80), 0.55);   // nicht gewählte blasser
    }
  } else if (uModus == 2 && uMitKlasse == 1 && uWand == 0) {   // Vergleich mit dem Modell: orange bleibt stehen, rot zu tief
    vec4 so = texture(uSoll, (xy - uGitter.xy) / uGitter.zw);   // mit der Höhe jetzt (auch beim Abspielen)
    int k = 0;
    if (!unten) {
      float h = h1(vUV);
      k = h < so.r - 0.1 ? 2 : h > so.g + 0.5 ? 1 : 0;
    } else {
      float u2 = h2(vUV);
      k = u2 > so.a + 0.1 ? 2 : u2 < so.b - 0.5 ? 1 : 0;
    }
    c = k == 2 ? vec3(0.88, 0.15, 0.14) : k == 1 ? vec3(0.95, 0.60, 0.15) : vec3(0.78, 0.79, 0.81);
  }
  if (uStege == 1) {                   // Haltestege: das Material, das dort stehen bleibt, gelb
    vec3 st = texture(uSteg, (xy - uGitter.xy) / uGitter.zw).rgb;
    if (st.r > 0.5 && vPos.z >= st.g - 0.05 && vPos.z <= st.b + 0.05) c = mix(c, vec3(1.0, 0.80, 0.10), 0.85);
  }
  float l = matcap(n);
  // Cavity: Rillen und Kanten aus dem Höhenbild – Senken dunkler, Grate heller (Abstand: ein Bildpunkt, mind. 1,5 Zellen)
  if (uWand == 0 && !steil && uCavity == 1) {
    float mitte = hs(vUV);
    float rund = 0.25 * (hx1 + hx0 + hy1 + hy0);
    float rmm = max(r.x * uMat.z, r.y * uMat.w);
    float cav = (mitte - rund) / max(rmm, 1e-3);
    if (uSeite == 2) cav = -cav;
    l *= 1.0 + 0.45 * clamp(cav * 1.6, -1.0, 1.0);
  }
  farbe = vec4(c * l, 1.0);
}
)";

bool TeilGL::start()
{
    if (prog) return true;
    GLuint v = shader_bauen(GL_VERTEX_SHADER, TEIL_VS), f = shader_bauen(GL_FRAGMENT_SHADER, TEIL_FS);
    prog = glCreateProgram();
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    glLinkProgram(prog);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof log, nullptr, log);
        g_warning("Hirch 3D: Teil-Shader: %s", log);
        glDeleteProgram(prog);
        prog = 0;
        return false;
    }
    // eine Kachel: (N+1)² Punkte, dazu ein Ring Schürze (gleiche Lage, aRand = 1)
    std::vector<float> e;
    std::vector<unsigned> idx;
    for (int j = 0; j <= N; ++j)
        for (int i = 0; i <= N; ++i) e.insert(e.end(), {float(i) / N, float(j) / N, 0.f, 0.f, 0.f});
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            unsigned a = j * (N + 1) + i, b = a + 1, c = a + N + 2, d = a + N + 1;
            idx.insert(idx.end(), {a, b, c, a, c, d});
        }
    auto rand = [&](int i0, int j0, int i1, int j1) {      // Schürze entlang einer Kante
        unsigned basis = unsigned(e.size() / 5);
        int n = 0;
        for (int k = 0; k <= N; ++k) {
            int i = i0 + (i1 - i0) * k / N, j = j0 + (j1 - j0) * k / N;
            e.insert(e.end(), {float(i) / N, float(j) / N, 1.f, 0.f, 0.f});
            ++n;
        }
        for (int k = 0; k < N; ++k) {
            int i = i0 + (i1 - i0) * k / N, j = j0 + (j1 - j0) * k / N;
            unsigned o0 = j * (N + 1) + i;
            int i2 = i0 + (i1 - i0) * (k + 1) / N, j2 = j0 + (j1 - j0) * (k + 1) / N;
            unsigned o1 = j2 * (N + 1) + i2, s0 = basis + k, s1 = basis + k + 1;
            idx.insert(idx.end(), {o0, o1, s1, o0, s1, s0});
        }
    };
    rand(0, 0, N, 0);
    rand(N, 0, N, N);
    rand(N, N, 0, N);
    rand(0, N, 0, 0);
    glGenVertexArrays(1, &vao_kachel);
    glGenBuffers(1, &vbo_kachel);
    glGenBuffers(1, &ebo_kachel);
    glBindVertexArray(vao_kachel);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_kachel);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(e.size() * sizeof(float)), e.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_kachel);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(idx.size() * sizeof(unsigned)), idx.data(), GL_STATIC_DRAW);
    attribute();
    n_kachel = GLsizei(idx.size());
    glGenBuffers(1, &vbo_inst);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_inst);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glVertexAttribDivisor(3, 1);
    glGenVertexArrays(1, &vao_wand);
    glGenBuffers(1, &vbo_wand);
    glBindVertexArray(vao_wand);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_wand);
    attribute();
    glBindVertexArray(0);
    glGenTextures(1, &tex_farben);
    glGenTextures(1, &tex_klasse);
    glGenTextures(1, &tex_steg);
    for (GLuint t : {tex_farben, tex_klasse, tex_steg}) {
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    // Pyramide: ein Durchgang je Stufe, Minimum aus 2 × 2 (am ungeraden Rand 3)
    {
        static char const *const VS_V = R"(#version 330 core
void main() { vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0); gl_Position = vec4(p, 0.0, 1.0); }
)";
        static char const *const FS_M = R"(#version 330 core
uniform sampler2D uQ;
uniform int uL;
uniform float uDurch;                  // normierte Höhe der Unterseite: darunter ist das Material ganz weg
out float aus;
void main() {
  ivec2 d = ivec2(gl_FragCoord.xy), qs = textureSize(uQ, uL), p = d * 2;
  // tiefste Stelle, aber ohne Durchbrüche: sonst fällt eine grobe Kachel an einer Kontur ganz hinunter
  float m = 1.0, m_alle = 1.0;
  for (int j = 0; j < 3; ++j)
    for (int i = 0; i < 3; ++i) {
      if ((i == 2 && p.x + 2 != qs.x - 1) || (j == 2 && p.y + 2 != qs.y - 1)) continue;
      float v = texelFetch(uQ, min(p + ivec2(i, j), qs - 1), uL).r;
      m_alle = min(m_alle, v);
      if (v > uDurch) m = min(m, v);
    }
  aus = m < 1.0 ? m : m_alle;
}
)";
        GLuint v2 = shader_bauen(GL_VERTEX_SHADER, VS_V), f2 = shader_bauen(GL_FRAGMENT_SHADER, FS_M);
        p_min = glCreateProgram();
        glAttachShader(p_min, v2);
        glAttachShader(p_min, f2);
        glLinkProgram(p_min);
        glDeleteShader(v2);
        glDeleteShader(f2);
        GLint ok2 = 0;
        glGetProgramiv(p_min, GL_LINK_STATUS, &ok2);
        if (!ok2) {
            glDeleteProgram(p_min);
            p_min = 0;
        }
        glGenVertexArrays(1, &vao_leer);
        glGenFramebuffers(1, &fbo_min);
    }
    auto r = reinterpret_cast<char const *>(glGetString(GL_RENDERER));
    software = r && (std::strstr(r, "llvmpipe") || std::strstr(r, "softpipe"));
    qual = basis_qual();
    skala = 1.0;
    cavity = true;
    glatt = !software;
    glatt_bis = 12;
    return true;
}

void TeilGL::pyramide(HirchAbtrag const &ab)
{
    if (!p_min || !ab.tex_h || ab.W < 2 || ab.H < 2) {
        stufen = 0;
        return;
    }
    int n = 0;
    for (int w = ab.W, h = ab.H; w > 1 || h > 1; w = std::max(1, w / 2), h = std::max(1, h / 2)) ++n;
    n = std::min(n, 14);
    if (ab.W != min_w || ab.H != min_h || !tex_min[0]) {
        for (GLuint &t : tex_min) {
            if (t) glDeleteTextures(1, &t);
            glGenTextures(1, &t);
            glBindTexture(GL_TEXTURE_2D, t);
            int w = ab.W, h = ab.H;
            for (int k = 0; k < n; ++k) {
                w = std::max(1, w / 2);
                h = std::max(1, h / 2);
                glTexImage2D(GL_TEXTURE_2D, k, GL_R32F, w, h, 0, GL_RED, GL_FLOAT, nullptr);
            }
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, n - 1);
        }
        min_w = ab.W;
        min_h = ab.H;
    }
    GLint alt_fb = 0, vp[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &alt_fb);
    glGetIntegerv(GL_VIEWPORT, vp);
    GLboolean tiefe = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(p_min);
    glBindVertexArray(vao_leer);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_min);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(p_min, "uQ"), 0);
    GLint u_l = glGetUniformLocation(p_min, "uL");
    // Unterseite (−Dicke) normiert: hmin = −(Dicke + 10) → (−Dicke − hmin) / −hmin = 10 / −hmin
    glUniform1f(glGetUniformLocation(p_min, "uDurch"), float(10.0 / std::max(1e-6, -ab.hmin) + 1e-5));
    GLuint quelle[2] = {ab.tex_h, ab.tex_h2};
    for (int s_ = 0; s_ < 2; ++s_) {
        if (!quelle[s_]) continue;
        int w = ab.W, h = ab.H;
        for (int k = 0; k < n; ++k) {
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_min[s_], k);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            if (k == 0) {
                glBindTexture(GL_TEXTURE_2D, quelle[s_]);
                glUniform1i(u_l, 0);
            } else {                         // aus der Stufe davor derselben Textur (nur diese freigeben)
                glBindTexture(GL_TEXTURE_2D, tex_min[s_]);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, k - 1);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, k - 1);
                // texelFetch/textureSize zählen ab BASE_LEVEL: die Stufe davor ist hier Stufe 0
                // (vorher k − 1: las auf manchen Treibern eine leere Stufe – Teil zeigte ungefräste Flächen)
                glUniform1i(u_l, 0);
            }
            glViewport(0, 0, w, h);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        glBindTexture(GL_TEXTURE_2D, tex_min[s_]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, n - 1);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, GLuint(alt_fb));
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    if (tiefe) glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    stufen = n;
}

void TeilGL::ende()
{
    if (!prog) return;
    glDeleteProgram(prog);
    glDeleteVertexArrays(1, &vao_kachel);
    glDeleteBuffers(1, &vbo_kachel);
    glDeleteBuffers(1, &ebo_kachel);
    glDeleteVertexArrays(1, &vao_wand);
    glDeleteBuffers(1, &vbo_wand);
    glDeleteTextures(1, &tex_farben);
    glDeleteTextures(1, &tex_klasse);
    glDeleteTextures(1, &tex_steg);
    mit_klasse = mit_stegen = false;
    glDeleteBuffers(1, &vbo_inst);
    if (p_min) glDeleteProgram(p_min);
    glDeleteVertexArrays(1, &vao_leer);
    glDeleteFramebuffers(1, &fbo_min);
    for (GLuint &t : tex_min)
        if (t) glDeleteTextures(1, &t), t = 0;
    p_min = vao_leer = fbo_min = vbo_inst = 0;
    if (abfrage[0]) glDeleteQueries(2, abfrage);
    abfrage[0] = abfrage[1] = 0;
    abfrage_laeuft[0] = abfrage_laeuft[1] = false;
    min_w = min_h = stufen = 0;
    prog = 0;
    u.clear();
}

void TeilGL::farben(std::vector<AbtragBearbeitung> const &bea)
{
    std::vector<float> f;
    for (auto const &b : bea) {
        auto c = hex_farbe(b.farbe, {0.86f, 0.78f, 0.62f});
        f.insert(f.end(), {c[0], c[1], c[2]});
    }
    if (f.empty()) f = {0.86f, 0.78f, 0.62f};
    anz_farben = int(bea.size());
    glBindTexture(GL_TEXTURE_2D, tex_farben);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, GLsizei(f.size() / 3), 1, 0, GL_RGB, GL_FLOAT, f.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void TeilGL::wand(double x0, double y0, double w, double h, double zelle, double rund_r, double cx, double cy)
{
    std::vector<float> e;
    std::vector<std::array<double, 4>> rand;     // x, y, nx, ny
    if (rund_r > 0) {
        for (int k = 0; k <= 360; ++k) {
            double a = 2 * M_PI * k / 360;
            rand.push_back({cx + (rund_r - 0.5 * zelle) * std::cos(a), cy + (rund_r - 0.5 * zelle) * std::sin(a),
                            std::cos(a), std::sin(a)});
        }
    } else {
        int n = std::clamp(int(std::max(w, h) / zelle), 4, 4000);
        double e0 = 0.5 * zelle;                // auf die Mitte der Randzellen
        auto kante = [&](double ax, double ay, double bx, double by, double nx, double ny) {
            for (int k = 0; k < n; ++k)
                rand.push_back({ax + (bx - ax) * k / n, ay + (by - ay) * k / n, nx, ny});
        };
        double l = x0 + e0, r = x0 + w - e0, b = y0 + e0, t = y0 + h - e0;
        kante(l, b, r, b, 0, -1);
        kante(r, b, r, t, 1, 0);
        kante(r, t, l, t, 0, 1);
        kante(l, t, l, b, -1, 0);
        rand.push_back({l, b, -1, 0});
    }
    for (std::size_t k = 1; k < rand.size(); ++k) {
        auto const &p = rand[k - 1], &q = rand[k];
        float nx = float(p[2]), ny = float(p[3]);
        auto ecke = [&](std::array<double, 4> const &a, float oben) {
            e.insert(e.end(), {float(a[0]), float(a[1]), oben, nx, ny});
        };
        ecke(p, 1);
        ecke(q, 1);
        ecke(q, 0);
        ecke(p, 1);
        ecke(q, 0);
        ecke(p, 0);
    }
    glBindVertexArray(vao_wand);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_wand);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(e.size() * sizeof(float)), e.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
    n_wand = GLsizei(e.size() / 5);
}

void TeilGL::kacheln(double x0, double y0, double w, double h, double zelle, double dicke, V3 const &auge, M4 const &pv,
             double pix, double schwelle, std::vector<Knoten> &aus) const
{
    double S = std::max(w, h);
    double fein = N * zelle;
    double ox = x0 + 0.5 * zelle, oy = y0 + 0.5 * zelle;
    auto ausserhalb = [&](double x, double y, double s) {
        double xa = std::max(x, x0), xb = std::min(x + s, x0 + w), ya = std::max(y, y0), yb = std::min(y + s, y0 + h);
        int raus[6] = {0, 0, 0, 0, 0, 0};
        for (int k = 0; k < 8; ++k) {
            double px = k & 1 ? xb : xa, py = k & 2 ? yb : ya, pz = k & 4 ? 0.5 : -dicke - 0.5;
            double c[4];
            for (int r = 0; r < 4; ++r) c[r] = pv[r] * px + pv[4 + r] * py + pv[8 + r] * pz + pv[12 + r];
            raus[0] += c[0] < -c[3];
            raus[1] += c[0] > c[3];
            raus[2] += c[1] < -c[3];
            raus[3] += c[1] > c[3];
            raus[4] += c[2] < -c[3];
            raus[5] += c[2] > c[3];
        }
        for (int r : raus)
            if (r == 8) return true;
        return false;
    };
    std::function<void(double, double, double)> geh = [&](double x, double y, double s) {
        if (x > x0 + w || y > y0 + h) return;             // ausserhalb des Materials
        if (ausserhalb(x, y, s)) return;
        double qx = std::clamp(auge.x, x, std::min(x + s, x0 + w)), qy = std::clamp(auge.y, y, std::min(y + s, y0 + h));
        double qz = std::clamp(auge.z, -dicke, 0.0);
        double dd = std::max(1e-6, std::sqrt((auge.x - qx) * (auge.x - qx) + (auge.y - qy) * (auge.y - qy) +
                                             (auge.z - qz) * (auge.z - qz)));
        if (s > fein * 1.01 && (s / N) / dd * pix > schwelle) {
            double h2 = s / 2;
            geh(x, y, h2);
            geh(x + h2, y, h2);
            geh(x, y + h2, h2);
            geh(x + h2, y + h2, h2);
            return;
        }
        aus.push_back({x, y, s});
    };
    // Wurzel: Zweierpotenz × feinste Kachel, damit die feinste Stufe genau eine Zelle je Gitterpunkt hat
    double wurzel = fein;
    while (wurzel < S) wurzel *= 2;
    geh(ox, oy, wurzel);
}

void TeilGL::nachfuehren(double ms)
{
    if (auto f = g_getenv("HIRCH_3D_FEINHEIT")) {   // fest (Fehlersuche): Bildpunkte je Gitterabstand
        qual = std::clamp(g_ascii_strtod(f, nullptr), 0.5, 64.0);
        return;
    }
    if (qual <= 0) qual = basis_qual();
    static char const *fest_s = g_getenv("HIRCH_3D_AUFLOESUNG");      // fest (Fehlersuche): 0,3 … 2
    // Reihenfolge (Peter 22.10.: die Zeit geht in die Fläche, nicht in die Dreiecke): zuerst kleiner zeichnen
    // (bis 0,55), bei viel zu langsam zugleich gröber; zuletzt ohne Cavity. Zurück umgekehrt.
    if (skala > 1.001 && (ms > 20 || software)) {     // zuerst das Grösser-Zeichnen zurück
        skala = skala * 0.85 < 1.03 ? 1.0 : skala * 0.85;
    } else if (ms > 30 && glatt) {
        glatt = false;
        glatt_bis = std::max(1.5, glatt_bis / 2);
    } else if (ms > 30) {
        if (!fest_s && skala > 0.56) skala = std::max(0.55, skala * (ms > 80 ? 0.75 : 0.88));
        if (ms > 60 || fest_s || skala <= 0.56) {
            if (qual < 12) qual = std::min(12.0, qual * (ms > 80 ? 1.6 : 1.25));
            else cavity = false;
        }
    } else if (ms < 12) {
        if (!cavity) cavity = true;
        else if (qual > basis_qual()) qual = std::max(basis_qual(), qual / 1.15);
        else if (!fest_s && skala < 1.0) skala = std::min(1.0, skala * 1.06);
        else if (!glatt && !software && ms < glatt_bis) glatt = true;
        else if (glatt && !software && !fest_s && ms < 8 && skala < SKALA_GROSS)   // zuletzt: grösser zeichnen
            skala = std::min(SKALA_GROSS, std::max(1.0, skala) * 1.08);
    }
    if (fest_s) skala = std::clamp(g_ascii_strtod(fest_s, nullptr), 0.3, SKALA_GROSS);
    if (g_getenv("HIRCH_3D_ZEIT") && g_get_monotonic_time() - log_uhr > 1000000) {
        log_uhr = g_get_monotonic_time();
        g_message("Hirch 3D: Teil %.1f ms je Bild, Feinheit %.2f Bildpunkte, %d Kacheln, Cavity %s, Auflösung %.0f %%, "
                  "Glättung %s", ms, qual, letzte_kacheln, cavity ? "an" : "aus", skala * 100, glatt ? "an" : "aus");
    }
}


bool TeilNetzGL::start()
{
    if (prog) return true;
    GLuint v = shader_bauen(GL_VERTEX_SHADER, NETZ_VS), f = shader_bauen(GL_FRAGMENT_SHADER, NETZ_FS);
    prog = glCreateProgram();
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    glLinkProgram(prog);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof log, nullptr, log);
        g_warning("Hirch 3D: Netz-Shader: %s", log);
        glDeleteProgram(prog);
        prog = 0;
        return false;
    }
    glGenVertexArrays(2, vao);
    glGenBuffers(2, vbo_p);
    glGenBuffers(2, vbo_n);
    glGenBuffers(1, &ebo);
    return true;
}

void TeilNetzGL::ende()
{
    if (!prog) return;
    glDeleteProgram(prog);
    glDeleteVertexArrays(2, vao);
    glDeleteBuffers(2, vbo_p);
    glDeleteBuffers(2, vbo_n);
    glDeleteBuffers(1, &ebo);
    prog = 0;
    da = false;
    u.clear();
}

void TeilNetzGL::laden(NetzAuftrag const &A)
{
    if (!start()) return;
    for (int s = 0; s < 2; ++s) {
        glBindVertexArray(vao[s]);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_p[s]);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(A.pos[s].size() * sizeof(float)), A.pos[s].data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_n[s]);
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(A.nk[s].size()), A.nk[s].data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_BYTE, GL_TRUE, 0, nullptr);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        if (s == 0)
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(A.idx.size() * sizeof(unsigned)), A.idx.data(),
                         GL_STATIC_DRAW);
    }
    glBindVertexArray(0);
    n_idx = GLsizei(A.idx.size());
    mat[0] = float(A.x0);
    mat[1] = float(A.y0);
    mat[2] = float(A.zx * A.W);
    mat[3] = float(A.zy * A.H);
    schritt[0] = float((A.schritt + 0.5) / std::max(1, A.W));
    schritt[1] = float((A.schritt + 0.5) / std::max(1, A.H));
    da = n_idx > 0;
}

void TeilNetzGL::malen(TeilGL &T, HirchAbtrag const &ab, double dicke, bool zwei, bool um_y, int modus, int wahl,
           bool stege_an, M4 const &pv, Blick const &bl)
{
    if (!da) return;
    glUseProgram(prog);
    glUniform1i(ort("uH"), 0);
    glUniform1i(ort("uH2"), 1);
    glUniform1f(ort("uHmin"), float(ab.hmin));
    glUniform1f(ort("uDicke"), float(dicke));
    glUniform2f(ort("uSchritt"), schritt[0], schritt[1]);
    glUniformMatrix4fv(ort("uPV"), 1, GL_FALSE, pv.data());
    glUniform4f(ort("uMat"), mat[0], mat[1], mat[2], mat[3]);
    glUniform1i(ort("uUmY"), um_y ? 1 : 0);
    glUniform1i(ort("uZwei"), zwei ? 1 : 0);
    glUniform1i(ort("uModus"), modus);
    glUniform1i(ort("uWahl"), wahl);
    glUniform1i(ort("uAnz"), T.anz_farben);
    glUniform1i(ort("uMitKlasse"), T.mit_klasse ? 1 : 0);
    glUniform1i(ort("uStege"), stege_an && T.mit_stegen ? 1 : 0);
    glUniform1i(ort("uCavity"), T.cavity ? 1 : 0);
    glUniform4f(ort("uGitter"), float(T.gitter[0]), float(T.gitter[1]), float(T.gitter[2]), float(T.gitter[3]));
    glUniform3f(ort("uRechts"), float(bl.rechts.x), float(bl.rechts.y), float(bl.rechts.z));
    glUniform3f(ort("uHoch"), float(bl.hoch.x), float(bl.hoch.y), float(bl.hoch.z));
    glUniform3f(ort("uVor"), float(bl.vor.x), float(bl.vor.y), float(bl.vor.z));
    glUniform1i(ort("uKarte"), 2);
    glUniform1i(ort("uKarte2"), 3);
    glUniform1i(ort("uFarben"), 4);
    glUniform1i(ort("uSoll"), 5);
    glUniform1i(ort("uSteg"), 8);
    for (int s = 0; s < 2; ++s) {
        glUniform1i(ort("uSeite"), s + 1);
        glBindVertexArray(vao[s]);
        glDrawElements(GL_TRIANGLES, n_idx, GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
}


/// Vergleich mit dem Modell (wie relief_ergebnis): je Rasterpunkt R = Oberseite, G = Unterseite –
/// 0 wie gewollt, 128 Material bleibt stehen (mehr als 0,5 mm), 255 zu tief (mehr als 0,1 mm). Das
/// Modell ist das Netz der Szene (Reiter «3D»); an steilen Wänden zählt die Nachbarschaft (3 × 3).
void modell_hoehen(Szene3D const &sz, TeilRaster const &R, std::vector<float> &mo, std::vector<float> &mu)
{
    float const nanv = std::numeric_limits<float>::quiet_NaN();
    std::size_t n = R.nx > 0 && R.ny > 0 ? std::size_t(R.nx) * R.ny : 0;
    mo.assign(n, nanv);
    mu.assign(n, nanv);
    if (!sz.hat_modell || R.nx < 2 || sz.index.empty()) return;
    std::size_t halb = sz.ecken.size() / 12;
    for (std::size_t k = 0; k + 2 < sz.index.size(); k += 3) {
        bool unten = sz.index[k] >= halb;
        float const *P[3] = {&sz.ecken[6 * sz.index[k]], &sz.ecken[6 * sz.index[k + 1]], &sz.ecken[6 * sz.index[k + 2]]};
        double ax = (P[0][0] - R.x0) / R.dx, ay = (P[0][1] - R.y0) / R.dy;
        double bx = (P[1][0] - R.x0) / R.dx, by = (P[1][1] - R.y0) / R.dy;
        double cx = (P[2][0] - R.x0) / R.dx, cy = (P[2][1] - R.y0) / R.dy;
        int i0 = std::max(0, int(std::ceil(std::min({ax, bx, cx})))), i1 = std::min(R.nx - 1, int(std::floor(std::max({ax, bx, cx}))));
        int j0 = std::max(0, int(std::ceil(std::min({ay, by, cy})))), j1 = std::min(R.ny - 1, int(std::floor(std::max({ay, by, cy}))));
        double den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
        if (std::abs(den) < 1e-12) continue;
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i) {
                double l0 = ((by - cy) * (i - cx) + (cx - bx) * (j - cy)) / den;
                double l1 = ((cy - ay) * (i - cx) + (ax - cx) * (j - cy)) / den;
                double l2 = 1 - l0 - l1;
                if (l0 < -1e-9 || l1 < -1e-9 || l2 < -1e-9) continue;
                float z = float(l0 * P[0][2] + l1 * P[1][2] + l2 * P[2][2]);
                auto &f = unten ? mu[R.at(i, j)] : mo[R.at(i, j)];
                if (std::isnan(f) || (unten ? z < f : z > f)) f = z;
            }
    }
}

std::vector<float> soll_bauen(Szene3D const &sz, TeilRaster const &R, bool zwei)
{
    std::vector<float> rg;
    if (!sz.hat_modell || R.nx < 2 || sz.index.empty()) return rg;
    std::size_t n = std::size_t(R.nx) * R.ny;
    std::vector<float> mo, mu;
    modell_hoehen(sz, R, mo, mu);
    auto nachbarn = [&](std::vector<float> const &a, std::size_t p, int I, int J, bool max_) {
        float v = a[p];
        for (int dj = -1; dj <= 1; ++dj)
            for (int di = -1; di <= 1; ++di) {
                if (!R.gilt(I + di, J + dj)) continue;
                float q = a[R.at(I + di, J + dj)];
                if (std::isnan(q)) q = max_ ? v : -1e9f;    // am Umriss fällt die Wand ab: dort nie «zu tief»
                if (!std::isnan(q)) v = std::isnan(v) ? q : (max_ ? std::max(v, q) : std::min(v, q));
            }
        return v;
    };
    // je Zelle (lo, hi) Oberseite, (lo, hi) Unterseite; ohne Modell: nie «zu tief», nie «bleibt stehen».
    // Verglichen wird im Shader mit der Höhe zum Zeitpunkt (auch beim Abspielen): Oberseite zu tief, wenn
    // h < lo − 0,1, bleibt stehen, wenn h > hi + 0,5; Unterseite gespiegelt.
    rg.assign(n * 4, 0.f);
    for (int J = 0; J < R.ny; ++J)
        for (int I = 0; I < R.nx; ++I) {
            std::size_t p = R.at(I, J);
            float *q = &rg[4 * p];
            q[0] = -1e9f;
            q[1] = 1e9f;
            q[2] = -1e9f;
            q[3] = 1e9f;
            if (!std::isnan(mo[p])) {
                q[0] = nachbarn(mo, p, I, J, false);
                q[1] = nachbarn(mo, p, I, J, true);
            }
            if (zwei && !std::isnan(mu[p])) {
                q[2] = nachbarn(mu, p, I, J, false);
                q[3] = nachbarn(mu, p, I, J, true);
            }
        }
    return rg;
}

/// Haltestege (Quader aus quader(): je 36 Ecken, die ersten 6 sind die Oberseite) auf dem Raster: je Zelle
/// (1, z unten, z oben), sonst 0 – die Ansicht färbt dort das stehen gebliebene Material gelb
std::vector<float> steg_raster(std::vector<float> const &q, TeilRaster const &R)
{
    std::vector<float> m;
    std::size_t je = 36 * 6;
    if (q.size() < je || R.nx < 2) return m;
    m.assign(std::size_t(R.nx) * R.ny * 3, 0.f);
    for (std::size_t b = 0; b + je <= q.size(); b += je) {
        float const *v = &q[b];
        float z1 = v[2], z0 = v[6 * 6 + 2];
        if (z0 > z1) std::swap(z0, z1);
        for (int t = 0; t < 2; ++t) {          // Oberseite: zwei Dreiecke
            float const *P[3] = {&v[(3 * t) * 6], &v[(3 * t + 1) * 6], &v[(3 * t + 2) * 6]};
            double ax = (P[0][0] - R.x0) / R.dx, ay = (P[0][1] - R.y0) / R.dy;
            double bx = (P[1][0] - R.x0) / R.dx, by = (P[1][1] - R.y0) / R.dy;
            double cx = (P[2][0] - R.x0) / R.dx, cy = (P[2][1] - R.y0) / R.dy;
            int i0 = std::max(0, int(std::floor(std::min({ax, bx, cx})))), i1 = std::min(R.nx - 1, int(std::ceil(std::max({ax, bx, cx}))));
            int j0 = std::max(0, int(std::floor(std::min({ay, by, cy})))), j1 = std::min(R.ny - 1, int(std::ceil(std::max({ay, by, cy}))));
            double den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
            if (std::abs(den) < 1e-12) continue;
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i) {
                    double l0 = ((by - cy) * (i - cx) + (cx - bx) * (j - cy)) / den;
                    double l1 = ((cy - ay) * (i - cx) + (ax - cx) * (j - cy)) / den;
                    if (l0 < -1e-6 || l1 < -1e-6 || 1 - l0 - l1 < -1e-6) continue;
                    float *c = &m[3 * R.at(i, j)];
                    if (c[0] > 0.5f) {
                        c[1] = std::min(c[1], z0);
                        c[2] = std::max(c[2], z1);
                    } else {
                        c[0] = 1;
                        c[1] = z0;
                        c[2] = z1;
                    }
                }
        }
    }
    return m;
}


// ---------------------------------------------------------------------------
// Umspannen in der 3D-Ansicht (Peter 27.10.: Entwurf «Umspannen in 3D»): Spannmittel als graue Klötze, die
// Schraube durch die Mitte (rundes Material), Sperrzonen orange

/// Dreiecke eines einfachen Rings (auch nicht konvex; Ohren abschneiden) als Indizes in den Ring
static std::vector<int> ring_dreiecke(std::vector<std::pair<double, double>> const &P)
{
    std::vector<int> aus, rest;
    int n = int(P.size());
    if (n >= 2 && std::hypot(P[0].first - P[n - 1].first, P[0].second - P[n - 1].second) < 1e-9) --n;   // geschlossen
    if (n < 3) return aus;
    double fl = 0;
    for (int i = 0; i < n; ++i) {
        auto const &a = P[i], &b = P[(i + 1) % n];
        fl += a.first * b.second - b.first * a.second;
    }
    for (int i = 0; i < n; ++i) rest.push_back(fl >= 0 ? i : n - 1 - i);   // gegen den Uhrzeigersinn
    auto kreuz2 = [&](int a, int b, int c) {
        return (P[b].first - P[a].first) * (P[c].second - P[a].second) -
               (P[b].second - P[a].second) * (P[c].first - P[a].first);
    };
    int schutz = 0;
    while (rest.size() > 3 && schutz < 4 * n * n) {
        bool ab = false;
        int m = int(rest.size());
        for (int k = 0; k < m && !ab; ++k) {
            ++schutz;
            int a = rest[(k + m - 1) % m], b = rest[k], c = rest[(k + 1) % m];
            if (kreuz2(a, b, c) <= 1e-12) continue;          // nicht konvex
            bool drin = false;
            for (int q : rest) {
                if (q == a || q == b || q == c) continue;
                if (kreuz2(a, b, q) >= 0 && kreuz2(b, c, q) >= 0 && kreuz2(c, a, q) >= 0) {
                    drin = true;
                    break;
                }
            }
            if (drin) continue;
            aus.insert(aus.end(), {a, b, c});
            rest.erase(rest.begin() + k);
            ab = true;
        }
        if (!ab) break;                                        // entartet: Rest als Fächer
    }
    for (std::size_t k = 1; k + 1 < rest.size(); ++k) aus.insert(aus.end(), {rest[0], rest[k], rest[k + 1]});
    return aus;
}

/// Lage im Teil: Seite 2 gewendet wie Fräser und Fräsbahnen
struct SpannLage {
    bool s2 = false, um_y = false;
    double breite = 0, hoehe = 0, dicke = 0;
    void ecke(std::vector<float> &v, double x, double y, double z, double nx, double ny, double nz) const
    {
        if (s2) {
            if (um_y) {
                x = breite - x;
                nx = -nx;
            } else {
                y = hoehe - y;
                ny = -ny;
            }
            z = -dicke - z;
            nz = -nz;
        }
        v.insert(v.end(), {float(x), float(y), float(z), float(nx), float(ny), float(nz)});
    }
};

/// Klotz: Umriss P von z0 bis z1 (Deckel oben)
static void klotz_netz(std::vector<std::pair<double, double>> const &P, double z0, double z1, SpannLage const &L,
                       std::vector<float> &v)
{
    auto tri = ring_dreiecke(P);
    for (std::size_t k = 0; k + 2 < tri.size(); k += 3)
        for (int q : {tri[k], tri[k + 1], tri[k + 2]}) L.ecke(v, P[q].first, P[q].second, z1, 0, 0, 1);
    int n = int(P.size());
    double fl = 0;
    for (int i = 0; i < n; ++i)
        fl += P[i].first * P[(i + 1) % n].second - P[(i + 1) % n].first * P[i].second;
    for (int i = 0; i < n; ++i) {
        auto a = P[i], b = P[(i + 1) % n];
        double dx = b.first - a.first, dy = b.second - a.second, l = std::hypot(dx, dy);
        if (l < 1e-9) continue;
        double nx = dy / l, ny = -dx / l;                  // aussen bei Umlauf gegen den Uhrzeigersinn
        if (fl < 0) {
            nx = -nx;
            ny = -ny;
        }
        L.ecke(v, a.first, a.second, z0, nx, ny, 0);
        L.ecke(v, b.first, b.second, z0, nx, ny, 0);
        L.ecke(v, b.first, b.second, z1, nx, ny, 0);
        L.ecke(v, a.first, a.second, z0, nx, ny, 0);
        L.ecke(v, b.first, b.second, z1, nx, ny, 0);
        L.ecke(v, a.first, a.second, z1, nx, ny, 0);
    }
}

/// Spannmittel, Schrauben und Sperrzonen für eine Stelle des Ablaufs (Seite, Spannung). Spannmittel: die der
/// Spannung; auf einer Seite ohne Umspannen die der Spannung 1. Zone: Umriss immer, Fläche nur in Spannung 1.
void spann_szene(VorschauDaten const &d, int seite, int spannung, std::vector<float> &sp, GLsizei &n_schraube,
                        std::vector<float> &zf, std::vector<float> &zl)
{
    sp.clear();
    zf.clear();
    zl.clear();
    n_schraube = 0;
    if (!d.zweiseitig) seite = 1;
    SpannLage L{d.zweiseitig && seite == 2, d.um_y, d.breite, d.hoehe, d.dicke};
    bool zonen = false;
    for (auto const &z : d.zonen) zonen = zonen || (d.zweiseitig ? z.seite : 1) == seite;
    int sp_n = zonen ? spannung : 1;
    for (auto const &sm : d.spannmittel) {
        if ((d.zweiseitig ? sm.seite : 1) != seite || (sm.spannung == 2 ? 2 : 1) != sp_n || sm.punkte.size() < 3)
            continue;
        klotz_netz(sm.punkte, 0, std::max(0.5, sm.hoehe), L, sp);
    }
    std::size_t vor = sp.size();
    SpannLage L1{false, d.um_y, d.breite, d.hoehe, d.dicke};
    for (auto const &sc : d.schrauben) {                  // Kopf auf der Oberfläche (Seite 1)
        std::vector<std::pair<double, double>> kreis;
        for (int i = 0; i < 32; ++i)
            kreis.push_back({sc.x + sc.kopf / 2 * std::cos(i * M_PI / 16), sc.y + sc.kopf / 2 * std::sin(i * M_PI / 16)});
        klotz_netz(kreis, 0, std::max(0.4, sc.ueberstand), L1, sp);
    }
    n_schraube = GLsizei((sp.size() - vor) / 6);
    for (auto const &z : d.zonen) {
        if ((d.zweiseitig ? z.seite : 1) != seite || z.ring.size() < 3) continue;
        double hz = 0.15;                                  // knapp über der Fläche
        if (spannung != 2) {
            auto tri = ring_dreiecke(z.ring);
            for (std::size_t k = 0; k + 2 < tri.size(); k += 3)
                for (int q : {tri[k], tri[k + 1], tri[k + 2]}) L.ecke(zf, z.ring[q].first, z.ring[q].second, hz, 0, 0, 1);
        }
        std::size_t n = z.ring.size();
        for (std::size_t i = 0; i < n; ++i) {
            auto a = z.ring[i], b = z.ring[(i + 1) % n];
            L.ecke(zl, a.first, a.second, hz, 0, 0, 1);
            L.ecke(zl, b.first, b.second, hz, 0, 0, 1);
        }
    }
}

} // namespace hirch3d
} // namespace Inkscape::UI::Dialog
