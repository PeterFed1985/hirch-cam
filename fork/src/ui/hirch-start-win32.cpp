// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Windows-Teil des Programmstarts (bis 30.10.2026 in 02-start.patch, src/inkscape-main.cpp).
 * Gehört der Windows-Sitzung; unter Linux wird nichts übersetzt.
 */
#ifdef _WIN32

#include "ui/hirch-start-win32.h"

#include <string>
#include <windows.h> // SetDllDirectoryW, LoadLibraryExW
#include <glib.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include "path-prefix.h"

/**
 * 3D-Darstellung mit der Grafikkarte oder in Software (Mesa).
 *
 * Wahl in %APPDATA%\hirch-cam\grafik.txt: «auto» (Standard), «karte» oder «software».
 * Bei «auto» prüft bin\hirch-gltest.exe, ob der Grafiktreiber OpenGL 3.3
 * kann (einmal; war es «software», bei jedem Start neu), Ergebnis in grafik-test.txt. Für die Software-Darstellung
 * wird bin\mesa\opengl32.dll geladen, bevor GTK OpenGL anfasst – alle späteren
 * Aufrufe von «opengl32.dll» bekommen dann Mesa (libgdk ist dafür auf hirchgl.dll
 * umgebogen, siehe fork/windows/bauen.sh und hirchgl.c).
 */
static void hirch_grafik_waehlen(std::string const &prefix)
{
    auto lesen = [](std::string const &pfad) {
        std::string t;
        try {
            t = Glib::file_get_contents(pfad);
        } catch (...) {
        }
        auto ende = t.find_first_of(" \t\r\n");
        return ende == std::string::npos ? t : t.substr(0, ende);
    };
    char const *appdata = g_getenv("APPDATA");
    std::string ordner = Glib::build_filename(appdata ? appdata : Glib::get_home_dir().c_str(), "hirch-cam");
    g_mkdir_with_parents(ordner.c_str(), 0755);
    auto wahl = lesen(Glib::build_filename(ordner, "grafik.txt"));
    if (wahl.empty()) wahl = "auto";
    bool software = wahl == "software";
    if (wahl == "auto") {
        auto test_datei = Glib::build_filename(ordner, "grafik-test.txt");
        auto ergebnis = lesen(test_datei);
        // «software» nicht für immer: nach einer Treiberinstallation soll die Karte gleich wieder gelten
        // (Windows-Sitzung 28.10.) – darum dann bei jedem Start neu prüfen; «karte» bleibt gemerkt
        if (ergebnis.empty() || ergebnis == "software") {
            auto test = Glib::build_filename(prefix, "bin", "hirch-gltest.exe");
            char const *argv_t[] = {test.c_str(), nullptr};
            gchar *aus = nullptr;
            gint status = 1;
            bool lief = g_spawn_sync(nullptr, const_cast<char **>(argv_t), nullptr, G_SPAWN_DEFAULT, nullptr, nullptr,
                                     &aus, nullptr, &status, nullptr);
            ergebnis = (lief && status == 0) ? "karte" : "software";
            try {
                Glib::file_set_contents(test_datei, ergebnis + "\n" + (aus ? aus : ""));
            } catch (...) {
            }
            g_free(aus);
        }
        software = ergebnis == "software";
    }
    auto mesa = Glib::build_filename(prefix, "bin", "mesa", "opengl32.dll");
    if (software && Glib::file_test(mesa, Glib::FileTest::FILE_TEST_EXISTS)) {
        // Mesa sucht seine DLLs im eigenen Ordner, den Rest (libstdc++ …) in bin
        auto bin_w = (wchar_t *)g_utf8_to_utf16(Glib::build_filename(prefix, "bin").c_str(), -1, nullptr, nullptr, nullptr);
        SetDllDirectoryW(bin_w);
        g_free(bin_w);
        auto mesa_w = (wchar_t *)g_utf8_to_utf16(mesa.c_str(), -1, nullptr, nullptr, nullptr);
        if (LoadLibraryExW(mesa_w, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)) {
            Glib::setenv("HIRCH_GRAFIK", "software");
        }
        g_free(mesa_w);
    }
    if (Glib::getenv("HIRCH_GRAFIK").empty()) Glib::setenv("HIRCH_GRAFIK", "karte");
}

void hirch_win32_start()
{
    // Läuft Hirch CNC schon, übernimmt es diesen Start und holt sein Fenster nach vorn – das erlaubt Windows nur,
    // wenn dieser Start (vom Klick aufs Programmsymbol) es weitergibt
    AllowSetForegroundWindow(ASFW_ANY);
    Glib::setenv("PYTHONUTF8", "1");   // Python liest/schreibt Dateien als UTF-8
    // Hirch CNC ist deutsch – auch auf einem Windows in anderer Sprache
    if (Glib::getenv("LANGUAGE").empty()) Glib::setenv("LANGUAGE", "de");

    // eigenes Profil (%APPDATA%\hirch-cam\profil) wie unter Linux der Starter,
    // Rechenkern im Programmordner, nach einem Update geänderte Vorgaben ins Profil übernehmen
    char const *dir = get_program_dir();   // …\bin
    std::string prefix = dir ? Glib::path_get_dirname(dir) : std::string();
    if (Glib::getenv("INKSCAPE_PROFILE_DIR").empty()) {
        char const *appdata = g_getenv("APPDATA");
        std::string basis = appdata ? appdata : Glib::build_filename(Glib::get_home_dir(), "AppData", "Roaming");
        Glib::setenv("INKSCAPE_PROFILE_DIR", Glib::build_filename(basis, "hirch-cam", "profil"));
    }
    if (!prefix.empty()) {
        auto repo = Glib::build_filename(prefix, "share", "inkscape", "hirch-cam");
        if (Glib::getenv("HIRCH_CAM_REPO").empty()) Glib::setenv("HIRCH_CAM_REPO", repo);
        std::string version;
        try {
            version = Glib::file_get_contents(Glib::build_filename(prefix, "VERSION"));
        } catch (...) {
        }
        while (!version.empty() && g_ascii_isspace(version.back())) version.pop_back();
        auto py = Glib::build_filename(prefix, "bin", "pythonw.exe");
        auto profil = Glib::getenv("INKSCAPE_PROFILE_DIR");
        char const *argv_py[] = {py.c_str(), "-m", "hirch_cam.profil", profil.c_str(), version.c_str(), nullptr};
        gchar **env = g_get_environ();
        env = g_environ_setenv(env, "PYTHONPATH", repo.c_str(), TRUE);
        g_spawn_sync(nullptr, const_cast<char **>(argv_py), env, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr,
                     nullptr, nullptr, nullptr);
        g_strfreev(env);
        hirch_grafik_waehlen(prefix);
    }
}

#endif // _WIN32
