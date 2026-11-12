// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: 3D – Hilfen: Material, Rechenkern, Modell in der Zeichnung, Netz, Kamera-Mathe, Szene, Einfügen (aus hirch-3d.cpp, siehe hirch-3d-intern.h)
 */
#include "ui/dialog/hirch-3d-intern.h"

namespace Inkscape::UI::Dialog {
namespace hirch3d {

// ---------------------------------------------------------------------------
// Material und Masse

/// Material der Platte, auf der «auf» liegt (sonst der im Panel gewählten Platte) – ab Platte 2
/// mit eigener Grösse und Lage (3D, 26.10.)
Material material(SPDocument *doc, SPItem *auf)
{
    Material m;
    if (!doc) return m;
    double f = MM_PX;
    auto [p1, p2] = platte_von(doc, auf);
    if (p1) {
        m.seite = p1->getDocumentRect();
        m.hat2 = p2 != nullptr;
        m.seite2 = p2 ? p2->getDocumentRect() : m.seite;
    } else {
        m.seite = Geom::Rect(0, 0, doc->getWidth().value("px"), doc->getHeight().value("px"));
    }
    m.b = m.seite.width() / f;
    m.h = m.seite.height() / f;
    if (auto a = doc->getReprRoot()->attribute("hirch:materialdicke")) {
        std::string t(a);
        std::replace(t.begin(), t.end(), ',', '.');
        double v = g_ascii_strtod(t.c_str(), nullptr);
        if (v > 0) m.d = v;
    }
    return m;
}

// ---------------------------------------------------------------------------
// Rechenkern (Python) asynchron: python3 -m hirch_cam.dokument relief …

std::shared_ptr<bool> kern(std::vector<std::string> args, std::string const &ordner, Antwort fertig)
{
    auto lebt = std::make_shared<bool>(true);
    auto launcher = g_subprocess_launcher_new(
        GSubprocessFlags(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE));
    std::string pfad = hirch_repo() + G_SEARCHPATH_SEPARATOR_S +
                       IO::Resource::get_path_string(IO::Resource::SYSTEM, IO::Resource::EXTENSIONS);
    g_subprocess_launcher_setenv(launcher, "PYTHONPATH", pfad.c_str(), TRUE);
    g_subprocess_launcher_setenv(launcher, "PYTHONIOENCODING", "utf-8", TRUE);
    g_subprocess_launcher_setenv(launcher, "PYTHONUTF8", "1", TRUE);
    g_subprocess_launcher_setenv(launcher, "HIRCH_SPRACHE", hirch_sprache(), TRUE);
    g_subprocess_launcher_setenv(launcher, "HIRCH_EINHEIT", hirch_einheit(), TRUE);
    g_subprocess_launcher_unsetenv(launcher, "PYTHONHOME");
    if (!ordner.empty()) g_subprocess_launcher_set_cwd(launcher, ordner.c_str());
    args.insert(args.begin(), {hirch_python(), "-m", "hirch_cam.dokument", "relief"});
    hirch_leere_argumente(args);
    std::vector<char const *> argv;
    for (auto const &a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    GError *err = nullptr;
    GSubprocess *proc = g_subprocess_launcher_spawnv(launcher, argv.data(), &err);
    g_object_unref(launcher);
    if (!proc) {
        std::string f = err ? err->message : TR("Python nicht gestartet");
        if (err) g_error_free(err);
        Glib::signal_idle().connect_once([fertig, f] { fertig(false, {}, f); });
        return lebt;
    }
    auto lauf = new Lauf;
    lauf->proc = proc;
    lauf->cancel = g_cancellable_new();
    lauf->fertig = [fertig, lebt](bool ok, std::string const &aus, std::string const &fehler) {
        if (!*lebt) return;
        bj::object erg;
        std::string f = fehler;
        try {
            auto v = bj::parse(aus);
            if (v.is_object()) erg = v.as_object();
        } catch (...) {
            ok = false;
            if (f.empty()) f = aus;
        }
        if (erg.empty()) ok = false;
        fertig(ok, erg, f);
    };
    g_subprocess_communicate_utf8_async(proc, nullptr, lauf->cancel, lauf_fertig, lauf);
    return lebt;
}

std::string neuer_ordner()
{
    GError *e = nullptr;
    char *p = g_dir_make_tmp("hirch-3d-XXXXXX", &e);
    if (e) g_error_free(e);
    std::string s = p ? p : Glib::get_tmp_dir();
    g_free(p);
    return s;
}

void ordner_loeschen(std::string const &ordner)
{
    if (ordner.empty() || ordner.find("hirch-3d-") == std::string::npos) return;
    try {
        Glib::Dir d(ordner);
        for (auto const &n : d) g_remove(Glib::build_filename(ordner, n).c_str());
    } catch (...) {
    }
    g_rmdir(ordner.c_str());
}

/// Texte der Fehlermeldungen des Rechenkerns (warnungen) zusammen
std::string warnungen(bj::object const &erg)
{
    std::string t;
    if (auto w = liste_von(erg, "warnungen"))
        for (auto const &x : *w)
            if (x.is_string()) t += (t.empty() ? "" : "\n") + std::string(x.as_string());
    return t;
}

std::string hinweise(bj::object const &erg)
{
    std::string t;
    if (auto w = liste_von(erg, "hinweise"))
        for (auto const &x : *w)
            if (x.is_string()) t += (t.empty() ? "" : "\n") + std::string(x.as_string());
    return t;
}

std::string datei(bj::object const &erg, char const *k)
{
    if (auto d = unter(erg, "dateien")) return str(*d, k);
    return {};
}

double liste_zahl(bj::object const &o, char const *k, std::size_t i, double d)
{
    auto l = liste_von(o, k);
    if (!l || l->size() <= i) return d;
    try {
        return (*l)[i].to_number<double>();
    } catch (...) {
        return d;
    }
}

std::string art_name(std::string const &art)
{
    if (art == "step") return "STEP";
    if (art == "stl") return "STL";
    if (art == "obj") return "OBJ";
    if (art == "bild") return TR("Graustufenbild");
    return art;
}

// ---------------------------------------------------------------------------
// Modell in der Zeichnung

bj::object relief_info(SPItem const *item)
{
    if (!item) return {};
    auto a = item->getRepr()->attribute(RELIEF);
    if (!a) return {};
    try {
        auto v = bj::parse(a);
        if (v.is_object()) return v.as_object();
    } catch (...) {
    }
    return {};
}

void reliefs_sammeln(SPObject *o, std::vector<SPItem *> &aus)
{
    for (auto &k : o->children) {
        auto it = cast<SPItem>(&k);
        if (!it) continue;
        if (it->getRepr()->attribute(RELIEF)) aus.push_back(it);
        else if (cast<SPGroup>(it)) reliefs_sammeln(it, aus);
    }
}

std::vector<SPItem *> reliefs(SPDocument *d)
{
    std::vector<SPItem *> aus;
    if (d && d->getRoot()) reliefs_sammeln(d->getRoot(), aus);
    return aus;
}

XML::Node *daten_knoten(SPDocument *d, std::string const &id)
{
    if (!d || id.empty() || !d->getDefs()) return nullptr;
    for (auto k = d->getDefs()->getRepr()->firstChild(); k; k = k->next()) {
        if (k->name() && std::strcmp(k->name(), DATEN) == 0 && k->attribute("id") && id == k->attribute("id"))
            return k;
    }
    return nullptr;
}

std::string knoten_text(XML::Node *n)
{
    std::string t;
    if (!n) return t;
    for (auto k = n->firstChild(); k; k = k->next())
        if (k->content()) t += k->content();
    return t;
}

/// Gespeicherte Modelldaten in eine Datei schreiben (für den Rechenkern)
bool daten_schreiben(SPDocument *d, bj::object const &info, std::string const &pfad)
{
    auto t = knoten_text(daten_knoten(d, str(info, "daten")));
    if (t.empty()) return false;
    try {
        Glib::file_set_contents(pfad, t);
    } catch (...) {
        return false;
    }
    return true;
}

std::string png_uri(std::string const &pfad)
{
    try {
        return "data:image/png;base64," + Glib::Base64::encode(Glib::file_get_contents(pfad));
    } catch (...) {
        return {};
    }
}

/// Lage des Bildes: Modellkoordinaten (mm, Y nach oben) → Material (mm ab der linken unteren Ecke)
Geom::Affine modell_zu_material(SPItem *bild, bj::object const &info, Material const &m, double f)
{
    auto r = bild->getRepr();
    auto zahl_attr = [r](char const *k) {
        auto a = r->attribute(k);
        return a ? g_ascii_strtod(a, nullptr) : 0.0;
    };
    double x = zahl_attr("x"), y = zahl_attr("y"), w = zahl_attr("width"), h = zahl_attr("height");
    double mx0 = liste_zahl(info, "bild_lage", 0), my0 = liste_zahl(info, "bild_lage", 1);
    double mw = liste_zahl(info, "bild_lage", 2, 1), mh = liste_zahl(info, "bild_lage", 3, 1);
    if (mw <= 0) mw = 1;
    if (mh <= 0) mh = 1;
    Geom::Affine a1 = Geom::Scale(w / mw, -h / mh) * Geom::Translate(x - mx0 * w / mw, y + h + my0 * h / mh);
    Geom::Affine a3 = Geom::Translate(-m.seite.left(), -m.seite.bottom()) * Geom::Scale(1 / f, -1 / f);
    return a1 * bild->i2doc_affine() * a3;
}

/// Zeichnungskoordinaten → Material (mm ab der linken unteren Ecke der ersten Seite)
Geom::Affine doc_zu_material(Material const &m, double f)
{
    return Geom::Translate(-m.seite.left(), -m.seite.bottom()) * Geom::Scale(1 / f, -1 / f);
}

/// Ecken eines Rechtecks oder Bildes (eigene x, y, width, height) in Materialkoordinaten:
/// oben links, oben rechts, unten rechts, unten links
std::array<Geom::Point, 4> ecken_mm(SPItem *it, Material const &m, double f)
{
    auto r = it->getRepr();
    auto z = [r](char const *k) {
        auto v = r->attribute(k);
        return v ? g_ascii_strtod(v, nullptr) : 0.0;
    };
    double x = z("x"), y = z("y"), w = z("width"), h = z("height");
    Geom::Affine a = it->i2doc_affine() * doc_zu_material(m, f);
    return {Geom::Point(x, y) * a, Geom::Point(x + w, y) * a, Geom::Point(x + w, y + h) * a,
            Geom::Point(x, y + h) * a};
}


/// Bildecken wie relief_svg.py: oben links, oben rechts, unten links
bj::array bild_ecken(SPItem *bild, Material const &m, double f)
{
    auto e = ecken_mm(bild, m, f);
    return punkte_json({e[0], e[1], e[3], e[2]}, 3);
}

bj::array punkte_json(std::array<Geom::Point, 4> const &p, int n)
{
    bj::array a;
    for (int i = 0; i < n; ++i) {
        bj::array q;
        q.push_back(p[i].x());
        q.push_back(p[i].y());
        a.push_back(q);
    }
    return a;
}

bj::object steg_werte(SPItem const *it)
{
    auto a = it ? it->getRepr()->attribute(STEG3D) : nullptr;
    if (!a) return {};
    try {
        auto v = bj::parse(a);
        if (v.is_object()) return v.as_object();
    } catch (...) {
    }
    return {};
}

void stege_sammeln_in(SPObject *o, std::vector<SPItem *> &aus)
{
    for (auto &k : o->children) {
        auto it = cast<SPItem>(&k);
        if (!it) continue;
        if (it->getRepr()->attribute(STEG3D)) aus.push_back(it);
        else if (cast<SPGroup>(it)) stege_sammeln_in(it, aus);
    }
}

// ---------------------------------------------------------------------------
// Netz für die 3D-Ansicht (netz.bin vom Rechenkern)

bool netz_lesen(std::string const &pfad, Netz &n)
{
    std::string roh;
    try {
        roh = Glib::file_get_contents(pfad);
    } catch (...) {
        return false;
    }
    if (roh.size() < 20) return false;
    std::int32_t k[2];
    float f[3];
    std::memcpy(k, roh.data(), 8);
    std::memcpy(f, roh.data() + 8, 12);
    if (k[0] <= 1 || k[1] <= 1) return false;
    std::size_t z = std::size_t(k[0]) * std::size_t(k[1]);
    if (roh.size() != 20 + 8 * z) return false;
    n.nx = k[0];
    n.ny = k[1];
    n.zelle = f[0];
    n.x0 = f[1];
    n.y0 = f[2];
    n.oben.resize(z);
    n.unten.resize(z);
    std::memcpy(n.oben.data(), roh.data() + 20, 4 * z);
    std::memcpy(n.unten.data(), roh.data() + 20 + 4 * z, 4 * z);
    return true;
}

/// Ober- und Unterseite als Dreiecke (Lage im Material, Z mit Versatz). Die Punkte direkt am
/// Rand bekommen Ober- = Unterseite (Mitte der Nachbarn): so schliessen sich beide Flächen.
/// Ecken: x, y, z, nx, ny, nz
void netz_bauen(Netz const &n, Geom::Affine const &lage, double zoff, std::vector<float> &ecken,
                std::vector<unsigned> &index)
{
    ecken.clear();
    index.clear();
    int nx = n.nx, ny = n.ny;
    std::size_t z = std::size_t(nx) * ny;
    std::vector<float> o(n.oben), u(n.unten);
    std::vector<char> gilt(z, 0);
    for (std::size_t i = 0; i < z; ++i) gilt[i] = std::isfinite(o[i]) && std::isfinite(u[i]);
    std::vector<char> rand(z, 0);
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            std::size_t p = std::size_t(j) * nx + i;
            if (gilt[p]) continue;
            double s = 0;
            int c = 0;
            for (int dj = -1; dj <= 1; ++dj)
                for (int di = -1; di <= 1; ++di) {
                    int ii = i + di, jj = j + dj;
                    if (ii < 0 || jj < 0 || ii >= nx || jj >= ny) continue;
                    std::size_t q = std::size_t(jj) * nx + ii;
                    if (gilt[q]) {
                        s += (o[q] + u[q]) / 2;
                        ++c;
                    }
                }
            if (c) {
                o[p] = u[p] = float(s / c);
                rand[p] = 1;
            }
        }
    auto ok = [&](std::size_t p) { return gilt[p] || rand[p]; };
    ecken.resize(2 * z * 6, 0.f);
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            std::size_t p = std::size_t(j) * nx + i;
            Geom::Point q = Geom::Point(n.x0 + i * n.zelle, n.y0 + j * n.zelle) * lage;
            float *a = &ecken[6 * p], *b = &ecken[6 * (p + z)];
            a[0] = b[0] = float(q.x());
            a[1] = b[1] = float(q.y());
            a[2] = ok(p) ? float(o[p] + zoff) : 0.f;
            b[2] = ok(p) ? float(u[p] + zoff) : 0.f;
        }
    for (int j = 0; j + 1 < ny; ++j)
        for (int i = 0; i + 1 < nx; ++i) {
            unsigned a = unsigned(j * nx + i), b = a + 1, c = a + unsigned(nx) + 1, d = a + unsigned(nx);
            if (!ok(a) || !ok(b) || !ok(c) || !ok(d)) continue;
            // nur Rand: keine Fläche
            if (rand[a] && rand[b] && rand[c] && rand[d]) continue;
            unsigned zz = unsigned(z);
            index.insert(index.end(), {a, b, c, a, c, d});
            index.insert(index.end(), {a + zz, c + zz, b + zz, a + zz, d + zz, c + zz});
        }
    // Normalen aus den Flächen
    for (std::size_t t = 0; t + 2 < index.size(); t += 3) {
        float *p0 = &ecken[6 * index[t]], *p1 = &ecken[6 * index[t + 1]], *p2 = &ecken[6 * index[t + 2]];
        float ux = p1[0] - p0[0], uy = p1[1] - p0[1], uz = p1[2] - p0[2];
        float vx = p2[0] - p0[0], vy = p2[1] - p0[1], vz = p2[2] - p0[2];
        float nx_ = uy * vz - uz * vy, ny_ = uz * vx - ux * vz, nz_ = ux * vy - uy * vx;
        for (int k = 0; k < 3; ++k) {
            float *e = &ecken[6 * index[t + k]];
            e[3] += nx_;
            e[4] += ny_;
            e[5] += nz_;
        }
    }
}

// ---------------------------------------------------------------------------
// Mathe für die Kamera (spaltenweise wie OpenGL)

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

GLuint shader_bauen(GLenum art, char const *quelle)
{
    GLuint sh = glCreateShader(art);
    glShaderSource(sh, 1, &quelle, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(sh, sizeof log, nullptr, log);
        g_warning("Hirch 3D: Shader: %s", log);
    }
    return sh;
}

/// Quader über einem Viereck (Draufsicht) von z0 bis z1 als Dreiecke mit Normalen
void quader(std::vector<float> &aus, std::array<Geom::Point, 4> const &p, double z0, double z1)
{
    auto drei = [&](std::array<double, 3> a, std::array<double, 3> b, std::array<double, 3> c) {
        double ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
        double vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
        float nx = float(uy * vz - uz * vy), ny = float(uz * vx - ux * vz), nz = float(ux * vy - uy * vx);
        for (auto const &q : {a, b, c})
            aus.insert(aus.end(), {float(q[0]), float(q[1]), float(q[2]), nx, ny, nz});
    };
    auto o = [&](int i) { return std::array<double, 3>{p[i].x(), p[i].y(), z1}; };
    auto u = [&](int i) { return std::array<double, 3>{p[i].x(), p[i].y(), z0}; };
    drei(o(0), o(1), o(2));
    drei(o(0), o(2), o(3));
    drei(u(0), u(2), u(1));
    drei(u(0), u(3), u(2));
    for (int i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        drei(u(i), u(j), o(j));
        drei(u(i), o(j), o(i));
    }
}

/// Szenen je Zeichenfenster (vom Reiter «3D» eingetragen) und Meldung, wenn sich eine ändert
std::map<SPDesktop *, std::shared_ptr<Szene3D>> &szenen()
{
    static std::map<SPDesktop *, std::shared_ptr<Szene3D>> s;
    return s;
}

sigc::signal<void(SPDesktop *)> &szenen_neu()
{
    static sigc::signal<void(SPDesktop *)> s;
    return s;
}

/// Gefrästes Teil je Fenster: die Daten der letzten Rechnung (wie die Vorschau sie bekommt) und eine
/// Kennung, damit nur bei einer neuen Rechnung neu abgetragen wird
std::map<SPDesktop *, std::shared_ptr<TeilDaten>> &teile()
{
    static std::map<SPDesktop *, std::shared_ptr<TeilDaten>> m;
    return m;
}

sigc::signal<void(SPDesktop *)> &teile_neu()
{
    static sigc::signal<void(SPDesktop *)> s;
    return s;
}

// ---------------------------------------------------------------------------
// Ausrichtung und Lage: Bedienelemente <-> JSON

std::string seite_text(std::string const &s)
{
    std::string t = s;
    if (!t.empty() && t[0] == '-') t = "\xe2\x88\x92" + t.substr(1);          // echtes Minus
    if (s == "+Z") t += " " + std::string(TR("(wie gespeichert)"));
    return t;
}

bool ist_bild_datei(std::string const &pfad)
{
    auto p = pfad.rfind('.');
    if (p == std::string::npos) return false;
    auto e = Glib::ustring(pfad.substr(p + 1)).lowercase().raw();
    for (char const *k : {"png", "jpg", "jpeg", "tif", "tiff", "bmp"})
        if (e == k) return true;
    return false;
}

Gtk::Label *hinweis_label()
{
    auto l = Gtk::make_managed<Gtk::Label>();
    l->set_xalign(0);
    l->set_line_wrap(true);
    l->set_max_width_chars(44);
    return l;
}

void hinweis_setzen(Gtk::Label &l, std::string const &text, char const *art)
{
    for (char const *k : {"hirch-warn", "hirch-fehler", "hirch-karte"}) l.get_style_context()->remove_class(k);
    if (text.empty()) {
        l.hide();
        return;
    }
    l.set_text(text);
    klasse(l, art);
    l.set_no_show_all(false);
    l.show();
}

Geom::Affine seite2_wendung(SPDocument *doc, SPItem *auf)
{
    Material m = material(doc, auf);
    double f = MM_PX;
    Geom::Rect p1 = m.seite, p2 = m.seite2;
    std::string achse = "x";
    if (auto a = doc->getReprRoot()->attribute("hirch:einstellungen")) {
        try {
            auto v = bj::parse(a);
            if (v.is_object())
                if (auto z = unter(v.as_object(), "zweiseitig")) achse = str(*z, "achse", "x");
        } catch (...) {
        }
    }
    // Material-mm (x, y) liegt auf Seite 1 bei (L1 + x·f, B1 − y·f); gewendet um X auf Seite 2 bei
    // (x, H − y), um Y bei (B − x, y)
    return achse == "y"
        ? Geom::Affine(-1, 0, 0, 1, p2.left() + p1.left() + m.b * f, p2.bottom() - p1.bottom())
        : Geom::Affine(1, 0, 0, -1, p2.left() - p1.left(), p2.bottom() + p1.bottom() - m.h * f);
}

void seite2_nachfuehren(SPDocument *doc)
{
    if (!doc || !doc->getReprRoot()) return;
    std::vector<Inkscape::XML::Node *> bilder;
    std::function<void(Inkscape::XML::Node *)> suchen = [&](Inkscape::XML::Node *n) {
        for (auto c = n->firstChild(); c; c = c->next()) {
            if (c->attribute(RELIEF2)) bilder.push_back(c);
            else suchen(c);
        }
    };
    suchen(doc->getReprRoot());
    if (bilder.empty()) return;
    DocumentUndo::ScopedInsensitive ohne_undo(doc);
    for (auto b : bilder) {
        auto it = cast<SPItem>(doc->getObjectById(b->attribute(RELIEF2)));
        if (!it || !it->parent) continue;            // verwaist: räumt der Reiter «3D» weg
        auto ich = cast<SPItem>(doc->getObjectByRepr(b));
        Geom::Affine eltern = Geom::identity();
        if (ich && ich->parent) {
            if (auto ei = cast<SPItem>(ich->parent)) eltern = ei->i2doc_affine();
        }
        Geom::Affine tr = it->i2doc_affine() * seite2_wendung(doc, it) * eltern.inverse();
        auto r = it->getRepr();
        auto setzen = [&](char const *k, std::string const &v) {
            auto alt = b->attribute(k);
            if (!alt || v != alt) b->setAttribute(k, v);
        };
        for (char const *k : {"x", "y", "width", "height"})
            if (auto v = r->attribute(k)) setzen(k, v);
        // nur bei echter Abweichung schreiben (Rundung des Textes: sonst endlos «geändert»)
        Geom::Affine alt = Geom::identity();
        if (auto t = b->attribute("transform")) sp_svg_transform_read(t, &alt);
        if (!Geom::are_near(alt, tr, 1e-6)) setzen("transform", sp_svg_transform_write(tr));
    }
}

/// Modell in die Zeichnung: Daten in die defs, Schattenbild als <svg:image> in die Mitte des Materials
SPItem *einfuegen(SPDesktop *desk, bj::object const &erg, bj::object const &lage)
{
    auto doc = desk ? desk->getDocument() : nullptr;
    if (!doc) return nullptr;
    std::string daten;
    try {
        daten = Glib::file_get_contents(datei(erg, "daten"));
    } catch (...) {
        return nullptr;
    }
    auto uri = png_uri(datei(erg, "bild"));
    if (uri.empty() || daten.empty()) return nullptr;
    auto xml = doc->getReprDoc();
    Material m = material(doc);
    double f = MM_PX;
    auto did = doc->generate_unique_id("reliefdaten");
    auto dk = xml->createElement(DATEN);
    dk->setAttribute("id", did);
    dk->setAttribute("hirch:name", datei(erg, "name"));
    auto text = xml->createTextNode(daten.c_str());
    dk->appendChild(text);
    Inkscape::GC::release(text);
    doc->getDefs()->getRepr()->appendChild(dk);
    Inkscape::GC::release(dk);

    bj::object info;
    info["daten"] = did;
    info["name"] = datei(erg, "name");
    info["art"] = str(erg, "art");
    if (auto a = unter(erg, "ausrichtung")) info["ausrichtung"] = *a;
    info["lage"] = lage;
    if (auto bl = unter(erg, "bild_lage"))
        if (auto mm_ = liste_von(*bl, "mm")) info["bild_lage"] = *mm_;
    if (auto ms = liste_von(erg, "masse")) info["masse"] = *ms;
    if (auto z = liste_von(erg, "z")) info["z"] = *z;
    info["original"] = datei(erg, "original");
    info["original_zeit"] = num(erg, "original_zeit", 0);
    info["daten_bytes"] = num(erg, "daten_bytes", double(daten.size()));

    double w = liste_zahl(info, "bild_lage", 2, 100) * f, h = liste_zahl(info, "bild_lage", 3, 100) * f;
    auto bild = xml->createElement("svg:image");
    bild->setAttribute("id", doc->generate_unique_id("relief"));
    bild->setAttribute("x", attr_zahl(-w / 2));
    bild->setAttribute("y", attr_zahl(-h / 2));
    bild->setAttribute("width", attr_zahl(w));
    bild->setAttribute("height", attr_zahl(h));
    bild->setAttribute("preserveAspectRatio", "none");
    bild->setAttribute("xlink:href", uri);
    bild->setAttribute("inkscape:label", datei(erg, "name"));
    bild->setAttribute(RELIEF, bj::serialize(info));
    SPObject *ebene = desk->layerManager().currentLayer();
    if (!ebene) ebene = doc->getRoot();
    ebene->getRepr()->appendChild(bild);
    Inkscape::GC::release(bild);
    auto item = cast<SPItem>(doc->getObjectByRepr(bild));
    if (item) {
        Geom::Affine eltern = Geom::identity();
        if (auto e = cast<SPItem>(item->parent)) eltern = e->i2doc_affine();
        item->set_item_transform(Geom::Translate(m.seite.midpoint()) * eltern.inverse());
        item->doWriteTransform(item->transform, nullptr, false);
    }
    DocumentUndo::done(doc, TR("3D-Modell einlesen"), "");
    if (item) desk->getSelection()->set(item);
    return item;
}

} // namespace hirch3d
} // namespace Inkscape::UI::Dialog
