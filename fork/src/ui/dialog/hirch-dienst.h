// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Hintergrund-Rechnung (Python-Dienst), Pfade, Beobachter der Zeichnung, Design, Symbole
 * Nur für hirch-*.cpp – kein öffentlicher Header. Sammelkopf: hirch-intern.h.
 */
#ifndef HIRCH_DIENST_H
#define HIRCH_DIENST_H

#include "ui/dialog/hirch-skizzen.h"
#include <cstdio>
#include <tuple>

namespace Inkscape::UI::Dialog {

// ---------------------------------------------------------------------------
// Hintergrund-Rechnung (Python)
// ---------------------------------------------------------------------------
struct Lauf {
    bool abgebrochen = false;
    GSubprocess *proc = nullptr;
    GCancellable *cancel = nullptr;
    std::function<void(bool, std::string const &, std::string const &)> fertig;
};

/// Rechenkern als Dienst (hirch_cam/dienst.py): ein Python-Prozess bleibt offen, Aufträge gehen
/// als JSON-Zeilen hin und her – kein Neustart von Python je Rechnung (Peter, 20.10.)
struct DienstAuftrag {
    std::string zeile;
    std::vector<std::string> args;          // für den Notweg: eigener Prozess
    std::function<void(bool, std::string const &, std::string const &)> fertig;
};
struct DienstProzess {
    GSubprocess *proc = nullptr;
    GOutputStream *ein = nullptr;
    GDataInputStream *aus = nullptr;
    GCancellable *cancel = nullptr;
    bool tot = false;                       // abgebrochen/beendet: Antworten verwerfen
    bool bereit = false;                    // erste Zeile «bereit» gelesen
    std::unique_ptr<DienstAuftrag> laufend;
    std::function<void()> weiter;           // nächsten Auftrag schicken (nur solange nicht tot)
    std::function<void(std::unique_ptr<DienstAuftrag>)> gestorben;   // Prozess weg: vergessen, Auftrag anders rechnen
    ~DienstProzess()
    {
        if (aus) g_object_unref(aus);
        if (cancel) g_object_unref(cancel);
        if (proc) g_object_unref(proc);
    }
    void beenden()
    {
        tot = true;
        if (cancel) g_cancellable_cancel(cancel);
        if (proc) g_subprocess_force_exit(proc);
    }
};

inline void dienst_zeile(GObject *quelle, GAsyncResult *res, gpointer data);

/// nächste Zeile vom Dienst lesen (Antwort oder «bereit»)
inline void dienst_lesen(std::shared_ptr<DienstProzess> const &d)
{
    auto halter = new std::shared_ptr<DienstProzess>(d);
    g_data_input_stream_read_line_async(d->aus, G_PRIORITY_DEFAULT, d->cancel, dienst_zeile, halter);
}

inline void dienst_zeile(GObject *quelle, GAsyncResult *res, gpointer data)
{
    auto halter = static_cast<std::shared_ptr<DienstProzess> *>(data);
    auto d = *halter;
    delete halter;
    gsize n = 0;
    GError *fehler = nullptr;
    char *zeile = g_data_input_stream_read_line_finish_utf8(G_DATA_INPUT_STREAM(quelle), res, &n, &fehler);
    std::string text = zeile ? zeile : "";
    g_free(zeile);
    std::string grund = fehler ? fehler->message : "";
    if (fehler) g_error_free(fehler);
    if (d->tot) return;
    boost::system::error_code ec;
    auto v = text.empty() ? bj::value() : bj::parse(text, ec);
    if (text.empty() && !d->bereit && !d->laufend) {
        d->tot = true;
        if (d->gestorben) d->gestorben(nullptr);
        return;
    }
    if (!ec && v.is_object() && v.as_object().if_contains("bereit")) {
        d->bereit = true;
        if (d->laufend) dienst_lesen(d);   // Antwort auf den Auftrag kommt danach
        else if (d->weiter) d->weiter();
        return;
    }
    auto auftrag = std::move(d->laufend);
    if (text.empty() || ec || !v.is_object()) {
        // Dienst abgestürzt oder Unsinn: Dienst vergessen, den Auftrag als eigenen Prozess rechnen
        d->tot = true;
        if (d->gestorben) d->gestorben(std::move(auftrag));
        return;
    }
    auto const &o = v.as_object();
    std::string out, err;
    if (auto x = o.if_contains("out"); x && x->is_string()) out = std::string(x->get_string());
    if (auto x = o.if_contains("err"); x && x->is_string()) err = std::string(x->get_string());
    if (auftrag && auftrag->fertig) auftrag->fertig(true, out, err);
    if (!d->tot && d->weiter) d->weiter();
}

inline void lauf_fertig(GObject *quelle, GAsyncResult *res, gpointer data)
{
    auto lauf = static_cast<Lauf *>(data);
    char *out = nullptr, *err = nullptr;
    GError *fehler = nullptr;
    bool ok = g_subprocess_communicate_utf8_finish(G_SUBPROCESS(quelle), res, &out, &err, &fehler);
    if (!lauf->abgebrochen && lauf->fertig) {
        std::string o = out ? out : "", e = err ? err : "";
        if (!ok && fehler) e += fehler->message;
        lauf->fertig(ok, o, e);
    }
    g_free(out);
    g_free(err);
    if (fehler) g_error_free(fehler);
    g_object_unref(lauf->proc);
    g_object_unref(lauf->cancel);
    delete lauf;
}

/// Programmordner (…/hirch-cam bzw. C:\Programme\Hirch CNC)
inline std::string hirch_prefix()
{
#ifdef _WIN32
    char *d = g_win32_get_package_installation_directory_of_module(nullptr);
    std::string p = d ? d : "";
    g_free(d);
    return p;
#else
    if (auto r = g_getenv("HIRCH_CAM_REPO")) {
        return Glib::path_get_dirname(Glib::path_get_dirname(Glib::path_get_dirname(r)));
    }
    return "/opt/hirch-cam";
#endif
}

/// Ordner eines Rechenkern-Updates (nur Linux): ~/.config/hirch-cam/kern
inline std::string kern_ordner()
{
    return Glib::build_filename(g_get_user_config_dir(), "hirch-cam", "kern");
}

inline long long zahl_aus_datei(std::string const &pfad)
{
    try {
        return std::stoll(Glib::file_get_contents(pfad));
    } catch (...) {
        return 0;
    }
}

/// Ist ein Rechenkern-Update installiert, das neuer ist als das Programm?
inline bool kern_update_gilt()
{
#ifdef _WIN32
    return false;
#else
    auto k = kern_ordner();
    if (!Glib::file_test(Glib::build_filename(k, "hirch_cam", "__init__.py"), Glib::FILE_TEST_EXISTS)) return false;
    return zahl_aus_datei(Glib::build_filename(k, "ZEIT")) >
           zahl_aus_datei(Glib::build_filename(hirch_prefix(), "share", "hirch-cam", "ZEIT"));
#endif
}

inline std::string hirch_repo()
{
    if (kern_update_gilt()) return kern_ordner();
    if (auto e = g_getenv("HIRCH_CAM_REPO"); e && *e) return e;
    return IO::Resource::get_path_string(IO::Resource::SYSTEM, IO::Resource::NONE, "hirch-cam");
}


inline std::string hirch_repo_pfad() { return hirch_repo(); }

/// Testteil für das Probefräsen in Hartschaum (3D, zweiseitig); leer, wenn es im Paket fehlt
inline std::string hirch_testteil()
{
    auto p = Glib::build_filename(hirch_repo(), "hirch_cam", "daten", "testteil_hartschaum.step");
    return Glib::file_test(p, Glib::FILE_TEST_EXISTS) ? p : std::string();
}

/// Version zum Anzeigen: «1.0.0-beta.1» → «1.0 Beta 1», «1.0.0» → «1.0», «1.2.3» bleibt; Peters Fassung
/// (Datum-Commit, z. B. «2026.11.09-af4e82c») unverändert (Peter 09.11.: «Hirch CAM 1.0 Beta»)
inline std::string hirch_version_anzeige(std::string const &v)
{
    int a = 0, b = 0, c = 0, beta = 0, n = 0;
    char rest = 0;
    if (std::sscanf(v.c_str(), "%d.%d.%d-beta.%d%n", &a, &b, &c, &beta, &n) == 4 && n == (int)v.size()) {
        return std::to_string(a) + "." + std::to_string(b) + (c ? "." + std::to_string(c) : std::string()) +
               " Beta " + std::to_string(beta);
    }
    if (std::sscanf(v.c_str(), "%d.%d.%d%c", &a, &b, &c, &rest) == 3 && a < 1000) {
        return std::to_string(a) + "." + std::to_string(b) + (c ? "." + std::to_string(c) : std::string());
    }
    return v;
}

/// Zwei Versionen vergleichen: -1 älter, 0 gleich, 1 neuer, 2 nicht vergleichbar (Datum-Commit gegen 1.0.0-beta.N).
/// «-peter» im Namen zählt nicht. Beta vor der fertigen Fassung: 1.0.0-beta.2 < 1.0.0.
inline int hirch_version_vergleich(std::string a, std::string b)
{
    for (auto *s : {&a, &b}) {
        auto p = s->find("-peter");
        if (p != std::string::npos) s->erase(p, 6);
    }
    if (a == b) return 0;
    struct V { int x = 0, y = 0, z = 0, beta = 1 << 30; bool ok = false; };
    auto lesen = [](std::string const &s) {
        V v;
        int n = 0;
        if (std::sscanf(s.c_str(), "%d.%d.%d-beta.%d%n", &v.x, &v.y, &v.z, &v.beta, &n) == 4 && n == (int)s.size())
            v.ok = true;
        else if (std::sscanf(s.c_str(), "%d.%d.%d%n", &v.x, &v.y, &v.z, &n) == 3 && n == (int)s.size() && v.x < 1000)
            v.ok = true, v.beta = 1 << 30;
        return v;
    };
    V va = lesen(a), vb = lesen(b);
    if (va.ok && vb.ok) {
        auto t = [](V const &v) { return std::make_tuple(v.x, v.y, v.z, v.beta); };
        return t(va) < t(vb) ? -1 : (t(vb) < t(va) ? 1 : 0);
    }
    // beide Datum-Commit (Peters Fassung): nach dem Datum
    bool da = a.size() >= 10 && a[4] == '.' && a[7] == '.', db = b.size() >= 10 && b[4] == '.' && b[7] == '.';
    if (da && db) {
        auto x = a.substr(0, 10), y = b.substr(0, 10);
        return x < y ? -1 : (y < x ? 1 : 2);        // gleicher Tag, anderer Commit: nicht entscheidbar
    }
    return 2;
}

inline std::string hirch_python()
{
    if (auto e = g_getenv("HIRCH_PYTHON"); e && *e) return e;
#ifdef _WIN32
    // mitgeliefertes Python, ohne Konsolenfenster
    return Glib::build_filename(hirch_prefix(), "bin", "pythonw.exe");
#else
    return "python3";
#endif
}

/// Windows: GLib lässt leere Argumente beim Prozessstart weg (aus «--ids ""» wird «--ids», argparse
/// meldet dann «expected one argument»). Darum dort «--name» + "" zu «--name=» zusammenziehen.
inline void hirch_leere_argumente(std::vector<std::string> &args)
{
#ifdef _WIN32
    for (std::size_t i = 1; i < args.size(); i++) {
        if (args[i].empty() && args[i - 1].rfind("--", 0) == 0 && args[i - 1].find('=') == std::string::npos) {
            args[i - 1] += "=";
            args.erase(args.begin() + i--);
        }
    }
#else
    (void)args;
#endif
}

/// Meldet Änderungen an Attributen des Wurzelelements (Einstellungen,
/// Materialdicke, Seitengrösse) – auch beim Rückgängigmachen.
struct WurzelBeobachter : XML::NodeObserver {
    std::function<void()> geaendert;
    void notifyAttributeChanged(XML::Node &, GQuark, Util::ptr_shared, Util::ptr_shared) override
    {
        if (geaendert) geaendert();
    }
};

/// Meldet, wenn sich an einem Objekt «zusätzlich hier fräsen» (hirch:auch)
/// ändert – das ist kein Attribut, auf das Inkscape selbst reagiert.
struct AuchBeobachter : XML::NodeObserver {
    std::function<void()> geaendert;
    void notifyAttributeChanged(XML::Node &, GQuark name, Util::ptr_shared, Util::ptr_shared) override
    {
        static GQuark const auch = g_quark_from_static_string("hirch:auch");
        if (name == auch && geaendert) geaendert();
    }
};

// ---------------------------------------------------------------------------
// Design, Oberflächentest, Symbole (vor dem Panel)
// ---------------------------------------------------------------------------


/// Hell/dunkel umschalten (Adwaita mit oder ohne dunkle Variante), wird gespeichert.
inline void design_setzen(bool dunkel, Gtk::Window *fenster)
{
    auto prefs = Inkscape::Preferences::get();
    prefs->setString("/theme/gtkTheme", "Adwaita");
    prefs->setBool("/theme/preferDarkTheme", dunkel);
    if (auto settings = Gtk::Settings::get_default()) {
        settings->property_gtk_theme_name() = "Adwaita";
        settings->property_gtk_application_prefer_dark_theme() = dunkel;
    }
    auto tc = INKSCAPE.themecontext;
    if (!tc) return;
    bool dark = fenster ? tc->isCurrentThemeDark(fenster) : dunkel;
    prefs->setBool("/theme/darkTheme", dark);
    tc->getChangeThemeSignal().emit();
    tc->add_gtk_css(true);
}

inline bool design_dunkel() { return Inkscape::Preferences::get()->getBool("/theme/darkTheme", false); }

// Oberflächentest (hirch-test.cpp), gestartet mit HIRCH_OBERFLAECHENTEST=bericht.json
bool hirch_oberflaechentest_an();
void hirch_oberflaechentest(InkscapeWindow *win);

// Schwebende Werkzeugleiste (hirch-leiste.cpp)
Gtk::Widget *symbol_bild(std::string const &name, int groesse = 20);

} // namespace Inkscape::UI::Dialog

#endif
