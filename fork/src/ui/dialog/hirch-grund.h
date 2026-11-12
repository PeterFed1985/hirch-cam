// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Grundlagen für hirch-*.cpp – Includes, Fräsarten, JSON-Zugriff, Zahlen und Texte,
 * Werkzeugnamen und -skizze, Vorgaben. Nur für hirch-*.cpp – kein öffentlicher Header. Sammelkopf: hirch-intern.h.
 */
#ifndef HIRCH_GRUND_H
#define HIRCH_GRUND_H

#include "helper/auto-connection.h"
#include "ui/dialog/hirch-fraesen.h"
#include "ui/dialog/hirch-vorschau.h"
#include "ui/dialog/hirch-sprache.h"
#include "ui/dialog/hirch-fraeserbild.h"

#include <boost/json.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <clocale>
#include <map>
#include <array>
#include <deque>
#include <set>
#include <sstream>
#include <regex>
#include <vector>

#include <cairomm/context.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <giomm/file.h>
#include <giomm/mount.h>
#include <giomm/mountoperation.h>
#include <giomm/volumemonitor.h>
#include <giomm/drive.h>
#include "inkscape-application.h"
#include "inkscape.h"
#include "ui/themes.h"
#include <gtkmm/switch.h>
#include <gtkmm/settings.h>

namespace Inkscape { extern double hirch_linien_max; }
#include "ui/widget/canvas-grid.h"
#include "ui/widget/desktop-widget.h"
#include <glibmm/convert.h>
#include <glibmm/fileutils.h>
#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/dialog.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/entry.h>
#include <gtkmm/eventbox.h>
#include <gtkmm/expander.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/filefilter.h>
#include <gtkmm/grid.h>
#include <gtkmm/textview.h>
#include <gtkmm/main.h>
#include <gtkmm/clipboard.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/revealer.h>
#include <gtkmm/menu.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/menuitem.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/notebook.h>
#include <gtkmm/radiobutton.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>
#include <gtkmm/stack.h>
#include <gtkmm/flowbox.h>
#include <gtkmm/searchentry.h>
#include <gtkmm/recentmanager.h>
#include <gdkmm/general.h>
#include <gdkmm/pixbuf.h>
#include <charconv>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <gdkmm/monitor.h>
#include <gdkmm/seat.h>
#include <gdkmm/display.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/image.h>
#include <gdkmm/pixbufloader.h>
#include <gtkmm/separatormenuitem.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/popover.h>
#include <gtkmm/spinner.h>
#include <gtkmm/progressbar.h>
#include <gtkmm/stylecontext.h>
#include <gtkmm/window.h>

#include "desktop-style.h"
#include "display/control/canvas-item-bpath.h"
#include "display/control/canvas-item-group.h"
#include "display/control/canvas-item-ptr.h"
#include "display/control/canvas-item-text.h"
#include "desktop.h"
#include "object/sp-namedview.h"
#include "ui/widget/canvas.h"
#include "document-undo.h"
#include "live_effects/effect.h"
#include "xml/attribute-record.h"
#include "live_effects/lpeobject.h"
#include "object/sp-lpe-item.h"
#include "object/sp-shape.h"
#include "display/curve.h"
#include "message-stack.h"
#include "selection.h"
#include "document.h"
#include "inkscape-window.h"
#include "layer-manager.h"
#include "libnrtype/font-factory.h"
#include "libnrtype/font-lister.h"
#include "svg/svg.h"
#include <2geom/circle.h>
#include <2geom/path.h>
#include <2geom/pathvector.h>
#include "io/resource.h"
#include "object/sp-flowtext.h"
#include "object/sp-item.h"
#include "object/sp-item-group.h"
#include "object/sp-shape.h"
#include "display/curve.h"
#include "object/sp-root.h"
#include "object/sp-text.h"
#include "page-manager.h"
#include "object/sp-page.h"
#include "preferences.h"
#include "selection.h"
#include "ui/dialog/dialog-container.h"
#include "ui/dialog/dialog-notebook.h"
#include "ui/pack.h"
#include "ui/util.h"
#include "util/units.h"
#include "xml/node.h"
#include "xml/node.h"
#include "xml/node-observer.h"
#include "xml/repr.h"

namespace bj = boost::json;

std::vector<std::string> versionen_liste(std::string const &pfad);   // hirch-aktionen.cpp

namespace Inkscape::UI::Dialog {

/// Alle 3D-Modelle der Zeichnung nur von oben fräsen (hirch-3d.cpp; «Zweiseitig» im Auftrag ausgeschaltet)
void reliefs_einseitig(SPDocument *d);
/// Zweiseitiges 3D-Modell für «Haltestege setzen» (hirch-3d.cpp): das gewählte oder, ohne Wahl, das erste
SPItem *relief_fuer_stege(SPDocument *d, SPItem *wahl);

// ---------------------------------------------------------------------------
// Fräsarten (wie hirch_cam/farben.py)
// ---------------------------------------------------------------------------
struct Art {
    char const *key;
    char const *name;
    char const *farbe;
    char const *hex;
    double r, g, b;
};

inline Art const ARTEN[] = {
    {"vcarve", "V-Carve", "Rot", "#e00000", 0.878, 0.0, 0.0},
    {"gravur", "Gravur", "Orange", "#ff8c00", 1.0, 0.549, 0.0},
    {"tasche", "Tasche", "Grün", "#00a000", 0.0, 0.627, 0.0},
    {"kontur", "Kontur", "Blau", "#0050ff", 0.0, 0.314, 1.0},
    {"bohren", "Bohren", "Violett", "#8000c0", 0.502, 0.0, 0.753},
    {"gewinde", "Gewinde", "Türkis", "#00aabe", 0.0, 0.667, 0.745},
    // ohne gezeichnete Objekte: die ganze Materialoberfläche
    {"planen", "Planfräsen", "ganzes Material", "#9a7b4f", 0.604, 0.482, 0.310},
    // 3D-Fräsung: die 3D-Modelle der Zeichnung (hirch-3d.cpp, ops/relief3d.py)
    // Peter 30.10.: orange und blau (grau sah man auf dem Graustufenbild nicht; farben.py _OHNE_FARBE)
    {"relief_schruppen", "3D-Schruppen", "3D-Modell", "#e66100", 0.902, 0.380, 0.0},
    {"relief_schlichten", "3D-Schlichten", "3D-Modell", "#1c71d8", 0.110, 0.443, 0.847},
    // Böden und Wände des 3D-Modells auf Mass (ops/boeden_waende.py) – kein «relief_»: die Vorteile der
    // 2D-Bearbeitungen (Zwischenspeicher, Vereinfachen, tief freifahren, Passlöcher) gelten
    {"boeden_waende", "Böden und Wände", "3D-Modell", "#2ec27e", 0.180, 0.761, 0.494},
};

inline bool ist_relief(std::string const &art) { return art.rfind("relief_", 0) == 0; }
inline bool ist_boeden_waende(std::string const &art) { return art == "boeden_waende"; }
/// fräst nach dem 3D-Modell (3D-Schruppen, 3D-Schlichten, Böden und Wände)
inline bool am_modell(std::string const &art) { return ist_relief(art) || ist_boeden_waende(art); }
inline bool ohne_objekte(std::string const &art) { return art == "planen" || am_modell(art); }

inline Art const *art_info(std::string const &key)
{
    for (auto const &a : ARTEN) {
        if (key == a.key) return &a;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// JSON-Helfer
// ---------------------------------------------------------------------------
inline double num(bj::object const &o, char const *k, double d)
{
    auto it = o.find(k);
    if (it == o.end()) return d;
    auto const &v = it->value();
    if (v.is_double()) return v.get_double();
    if (v.is_int64()) return (double)v.get_int64();
    if (v.is_uint64()) return (double)v.get_uint64();
    return d;
}

inline bool ist_null(bj::object const &o, char const *k)
{
    auto it = o.find(k);
    return it == o.end() || it->value().is_null();
}

inline std::string str(bj::object const &o, char const *k, std::string const &d = {})
{
    auto it = o.find(k);
    if (it == o.end() || !it->value().is_string()) return d;
    return std::string(it->value().get_string());
}

inline bool flag(bj::object const &o, char const *k, bool d)
{
    auto it = o.find(k);
    if (it == o.end() || !it->value().is_bool()) return d;
    return it->value().get_bool();
}

inline bj::object *unter(bj::object &o, char const *k)
{
    auto it = o.find(k);
    return it == o.end() ? nullptr : it->value().if_object();
}

inline bj::object const *unter(bj::object const &o, char const *k)
{
    auto it = o.find(k);
    return it == o.end() ? nullptr : it->value().if_object();
}

inline bj::array *liste_von(bj::object &o, char const *k)
{
    auto it = o.find(k);
    return it == o.end() ? nullptr : it->value().if_array();
}

inline bj::array const *liste_von(bj::object const &o, char const *k)
{
    auto it = o.find(k);
    return it == o.end() ? nullptr : it->value().if_array();
}

// ---------------------------------------------------------------------------
// Text-Helfer
// ---------------------------------------------------------------------------
/// Zahl mit höchstens 2 Nachkommastellen, deutsches Komma.
inline std::string zahl(double v, int stellen = 2)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", stellen, v);
    std::string s = buf;
    for (auto &c : s) {
        if (c == ',') c = '.';                       // Gebietsschema mit Dezimalkomma
    }
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (!hirch_englisch()) {
        for (auto &c : s) {
            if (c == '.') c = ',';
        }
    }
    return s;
}

/// Tausender mit schmalem Abstand: 18 000
/// Zahl ohne überflüssige Nullen: 0,2 statt 0,20 · 60 statt 60,00
inline std::string kurzzahl(double v)
{
    auto s = zahl(v);
    char const komma = hirch_englisch() ? '.' : ',';
    if (s.find(komma) == std::string::npos) return s;
    while (s.back() == '0') s.pop_back();
    if (s.back() == komma) s.pop_back();
    return s;
}

/// Die einzige Umrechnung mm ↔ Inkscapes Dokument-Pixel (intern; in Dateien und auf dem Bildschirm nur mm)
inline constexpr double MM_PX = 96.0 / 25.4;

inline double wert(bj::value const &v, double d = 0)
{
    if (v.is_double()) return v.get_double();
    if (v.is_int64()) return (double)v.get_int64();
    if (v.is_uint64()) return (double)v.get_uint64();
    return d;
}

/// Werkstück-Nullpunkt (Dokument-px) für einen Punkt bei (Dokument-px) aus analyse["nullpunkte"]: der Eintrag,
/// auf dessen Seite (seite_mm) der Punkt liegt – die Vorrichtung vor dem Werkstück –, sonst Seite 1 des Werkstücks.
inline bool nullpunkt_aus_liste(bj::array const &liste, Geom::Point bei, Geom::Point &aus)
{
    Geom::Point m = bei / MM_PX;
    bj::object const *treffer = nullptr, *erste = nullptr;
    for (auto const &v : liste) {
        auto o = v.if_object();
        if (!o) continue;
        int pl = (int)num(*o, "platte", -1);
        if (pl != 0 && (int)num(*o, "seite", 0) == 1 && !erste) erste = o;
        auto it = o->find("seite_mm");
        auto r = it != o->end() ? it->value().if_array() : nullptr;
        if (!r || r->size() != 4) continue;
        double l = wert((*r)[0]), ob = wert((*r)[1]), b = wert((*r)[2]), h = wert((*r)[3]);
        if (m.x() >= l && m.x() <= l + b && m.y() >= ob && m.y() <= ob + h && (!treffer || pl == 0)) treffer = o;
    }
    auto o = treffer ? treffer : erste;
    if (!o) return false;
    aus = Geom::Point(num(*o, "x", 0), num(*o, "y", 0)) * MM_PX;
    return true;
}

/// Gemerkte Nullpunkte je Zeichnung (das Panel trägt sie nach jeder Analyse ein; gelten auch bei
/// geschlossenem Panel). hirch-aktionen.cpp
void hirch_nullpunkte_merken(SPDocument const *doc, bj::array const &nullpunkte);

inline std::string nullpunkt_name(std::string const &xy)
{
    static std::map<std::string, std::string> const n = {
        {"ul", TR("unten links")}, {"um", TR("unten Mitte")}, {"ur", TR("unten rechts")},
        {"ml", TR("Mitte links")}, {"mm", TR("Mitte")},       {"mr", TR("Mitte rechts")},
        {"ol", TR("oben links")},  {"om", TR("oben Mitte")},  {"or", TR("oben rechts")}};
    auto it = n.find(xy);
    return it == n.end() ? TR("unten links") : it->second;
}

/// Zahl für Attribute in der Zeichnung: immer mit Punkt, unabhängig von der
/// Spracheinstellung (snprintf "%g" schreibt in de/it ein Komma – das liest
/// der Rechenkern nicht und nimmt dann die Vorgabe 18 mm)
inline std::string attr_zahl(double v)
{
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::general, 10);
    return std::string(buf, r.ptr);
}

inline std::string ganz(double v)
{
    long n = std::lround(v);
    std::string s = std::to_string(std::labs(n));
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert(i, " ");
    return (n < 0 ? "-" : "") + s;
}

/// Ordner im Dateimanager zeigen. Unter Windows über den Explorer – der Weg über die Standard-App für
/// «file://» tut dort nichts (Windows-Sitzung 29.10.). Wirft Glib::Error, wenn es nicht geht.
inline void ordner_zeigen(std::string const &ordner)
{
#ifdef _WIN32
    std::string w = ordner;
    std::replace(w.begin(), w.end(), '/', '\\');
    Glib::spawn_async("", std::vector<std::string>{"explorer.exe", w}, Glib::SPAWN_SEARCH_PATH);
#else
    Gio::AppInfo::launch_default_for_uri(Glib::filename_to_uri(ordner));
#endif
}

inline std::string zeit(double minuten)
{
    long s = std::lround(minuten * 60.0);
    char buf[32];
    if (s >= 3600) {
        std::snprintf(buf, sizeof buf, "%ld:%02ld h", s / 3600, (s % 3600) / 60);
    } else {
        std::snprintf(buf, sizeof buf, "%ld:%02ld min", s / 60, s % 60);
    }
    return buf;
}

inline std::string anzahl(long n, char const *eins, char const *viele)
{
    return std::to_string(n) + " " + (n == 1 ? eins : viele);
}

inline std::string werkzeug_name(bj::object const &w)
{
    auto n = str(w, "name");
    if (!n.empty()) return TR(n.c_str());     // mitgelieferte Fräser heissen in der Sprache der Oberfläche
    auto t = str(w, "typ");
    if (t == "v") {
        if (num(w, "spitze", 0) > 0) {
            return TR("Gravierstichel ") + kurzzahl(num(w, "v_winkel", 60)) + "° / " + kurzzahl(num(w, "spitze", 0)) + TR(" mm");
        }
        return TR("V-Fräser ") + kurzzahl(num(w, "v_winkel", 60)) + "° Ø " + kurzzahl(num(w, "durchmesser", 12)) + TR(" mm");
    }
    if (t == "kugel") return TR("Kugelfräser Ø ") + kurzzahl(num(w, "durchmesser", 3)) + TR(" mm");
    if (t == "gewinde") {
        int z = (int)num(w, "zaehne", 1);
        return TR("Gewindefräser Ø ") + kurzzahl(num(w, "durchmesser", 3)) + TR(" mm") +
               (z > 1 ? " · " + std::to_string(z) + TR(" Zähne × ") + kurzzahl(num(w, "steigung", 1)) : std::string(TR(" · Einzahn")));
    }
    if (t == "eckradius") {
        return TR("Eckradiusfräser Ø ") + kurzzahl(num(w, "durchmesser", 3)) + " R" + kurzzahl(num(w, "eckradius", 1));
    }
    if (t == "bohrer") return TR("Bohrer Ø ") + kurzzahl(num(w, "durchmesser", 3)) + TR(" mm");
    if (t == "konisch")
        return TR("Konischer Kugelfräser R") + kurzzahl(num(w, "eckradius", 0.25)) + " · " +
               kurzzahl(num(w, "v_winkel", 3.6)) + "°";
    return TR("Schaftfräser Ø ") + kurzzahl(num(w, "durchmesser", 3)) + TR(" mm");
}

/// Rundes Material (Zylinder stehend): Einstellungen «rund», an = true
inline bj::object const *rund_einst(bj::object const &einst)
{
    auto r = unter(einst, "rund");
    return r && flag(*r, "an", false) ? r : nullptr;
}

/// Woher die Schnittdaten kommen (wie werkzeugdb.schnittmodus): «auto», «drehzahl» (Drehzahl von Hand,
/// Vorschub gerechnet) oder «hand». Ältere Zeichnungen kennen nur «auto»: true/false.
inline std::string schnittmodus(bj::object const &w)
{
    auto m = str(w, "schnittdaten");
    if (m == "auto" || m == "drehzahl" || m == "hand") return m;
    return flag(w, "auto", false) ? "auto" : "hand";
}

/// Werkzeug mit den Schnittdaten für diesen Werkstoff: eigener Eintrag in je_werkstoff, sonst die
/// allgemeinen Angaben (wie werkzeugdb.fuer_werkstoff)
inline bj::object fuer_werkstoff(bj::object const &w, std::string const &mat)
{
    bj::object aus = w;
    auto je = unter(w, "je_werkstoff");
    auto e = je ? unter(*je, mat.c_str()) : nullptr;
    if (!e) return aus;
    auto m = str(*e, "schnittdaten");
    if (m != "drehzahl" && m != "hand") m = "auto";
    aus["schnittdaten"] = m;
    aus["auto"] = m == "auto";
    for (auto f : {"drehzahl", "vorschub", "eintauchen"}) {
        if (auto v = e->if_contains(f)) aus[f] = *v;
    }
    return aus;
}

/// Werkzeug aus «Meine Werkzeuge» in die Zeichnung: Drehzahl von Hand bleibt, sonst automatisch
inline void schnittdaten_uebernehmen(bj::object &w)
{
    auto m = str(w, "schnittdaten");
    if (m != "drehzahl" && m != "hand") m = "auto";      // «Meine Werkzeuge» ohne Angabe: automatisch
    w["schnittdaten"] = m;
    w["auto"] = m == "auto";
}

/// Name mit der Art davor, wenn der Name sie nicht schon nennt («18° / 0,2 mm» → «Gravierstichel 18° / 0,2 mm»)
inline std::string werkzeug_name_mit_art(bj::object const &w)
{
    auto n = werkzeug_name(w);
    auto klein = Glib::ustring(n).lowercase().raw();
    for (char const *wort : {"fräser", "fraeser", "stichel", "bohrer", "schneider", "spirale", "messer", "cutter",
                             "mill", "drill", "bit", "engraver", "router", "nose", "tool"})
        if (klein.find(wort) != std::string::npos) return n;
    auto t = str(w, "typ", "schaft");
    std::string art = t == "v" ? (num(w, "spitze", 0) > 0 ? TR("Gravierstichel") : TR("V-Fräser"))
                    : t == "kugel" ? TR("Kugelfräser") : t == "eckradius" ? TR("Eckradiusfräser")
                    : t == "konisch" ? TR("Konischer Kugelfräser") : t == "gewinde" ? TR("Gewindefräser")
                    : t == "bohrer" ? TR("Bohrer") : TR("Schaftfräser");
    // steht die Art (übersetzt) schon im Namen, nicht doppelt («Ball nose Ball nose Ø 6 mm», Windows 28.10.)
    if (klein.find(Glib::ustring(art).lowercase().raw()) != std::string::npos) return n;
    return art + " " + n;
}

/// Woraus das Fräserbild entsteht (hirch-fraeserbild.h): Art, Schneiden, Drall, Wendeplatten,
/// Spanbrecher – eingetragen oder, bei älteren Fräsern ohne Angabe, aus dem Namen erkannt
inline fraeserbild::Merkmale fraeser_merkmale(bj::object const &w)
{
    fraeserbild::Merkmale m;
    auto t = str(w, "typ", "schaft");
    auto name = Glib::ustring(str(w, "name")).lowercase().raw();
    auto hat = [&](char const *k) { return w.if_contains(k) != nullptr; };
    m.schneiden = std::max(1, (int)num(w, "schneiden", t == "v" && num(w, "spitze", 0) > 0 ? 1 : 2));
    if (t == "kugel") m.art = "kugel";
    else if (t == "konisch") m.art = "konisch";
    else if (t == "gewinde") m.art = "gewinde";
    else if (t == "eckradius") m.art = "eck";
    else if (t == "bohrer") m.art = "bohrer";
    else if (t == "v") m.art = num(w, "spitze", 0) > 0 ? "stichel" : "v";
    else {
        bool platten = hat("wendeplatten") ? flag(w, "wendeplatten", false)
                                           : name.find("planfr") != std::string::npos || name.find("wendeplatt") != std::string::npos;
        bool brecher = hat("spanbrecher") ? flag(w, "spanbrecher", false) : name.find("schrupp") != std::string::npos;
        m.art = platten ? "plan" : brecher ? "schrupp" : "schaft";
    }
    std::string drall = str(w, "drall");
    if (drall.empty()) {
        if (name.find("abwärts") != std::string::npos || name.find("downcut") != std::string::npos) drall = "ab";
        else if (name.find("kompression") != std::string::npos || name.find("compression") != std::string::npos) drall = "kompression";
    }
    m.drall = drall == "ab" ? 'L' : drall == "kompression" ? 'K' : 'R';
    return m;
}

/// Gruppe der Karte, auf der ein Fräser in der Werkzeugwahl steht
inline std::string fraeser_gruppe(bj::object const &w)
{
    auto t = str(w, "typ", "schaft");
    if (t == "v" || t == "kugel" || t == "eckradius" || t == "bohrer" || t == "gewinde" || t == "konisch") return t;
    return fraeser_merkmale(w).art == "plan" ? "plan" : "schaft";
}

/// Passt eine Werkzeugart zur Fräsart («vcarve», «tasche» …)? Weniger passende stehen blass dahinter.
inline bool wz_passend(std::string const &art, std::string const &typ, bool bohrer)
{
    if (art == "vcarve") return typ == "v" || typ == "konisch";
    if (art == "gravur") return typ != "bohrer" && typ != "gewinde";
    if (art == "gewinde" && !bohrer) return typ == "gewinde";
    bool fraeser = typ == "schaft" || typ == "kugel" || typ == "eckradius" || typ == "konisch";
    return bohrer ? fraeser || typ == "bohrer" : fraeser;
}

/// Werkzeugdialog zum Wählen geöffnet (Klick auf das Werkzeugfeld einer Bearbeitung)
struct WzWahl {
    bool mit_keinem = false;         // «keiner» anbieten (Boden der V-Carve)
    std::string keiner_text;         // eigener leerer Eintrag des Feldes («gleiches wie die Tasche», «schon gebohrt» …)
    bool bohrer = false;             // Bohrer erlaubt (Bohren, Gewinde)
    bool gewaehlt = false;           // mit «Wählen» geschlossen
    std::string ergebnis;            // Platz (A, B …), «db:<id>» oder "" = keiner
};

/// Ein Eintrag der Werkzeugliste einer Bearbeitung (eigenes Aufklappfeld, werkzeug_combo)
struct WzEintrag {
    std::string id, name, mass;
    int gruppe;      // 0 in dieser Zeichnung, 1 Meine Werkzeuge, 2 weniger passend, 3 ohne Überschrift, 4 «Neu»
    std::string karte;               // Kartengruppe (fraeser_gruppe), leer: keine Karte
    fraeserbild::Merkmale bild;
    double d = 0;
    std::string platz;               // Platz in dieser Zeichnung (A, B …), leer: aus «Meine Werkzeuge»
};

inline std::string werkzeug_details(bj::object const &w)
{
    return "Ø " + zahl(num(w, "durchmesser", 3)) + TR(" mm · ") + ganz(num(w, "drehzahl", 18000)) +
           TR(" U/min · ") + ganz(num(w, "vorschub", 1000)) + TR(" mm/min");
}

/// Höhe des Kegels zwischen Schneide und Schaft (wie relief_halterung.uebergang): eingetragen,
/// sonst 20° zur Achse bei dickerem Schaft, 45° bei breiterer Schneide (Planfräser); V-Fräser und
/// Bohrer haben keinen.
inline double werkzeug_uebergang(bj::object const &w)
{
    double u = num(w, "uebergang", 0);
    if (u > 0) return u;
    auto t = str(w, "typ", "schaft");
    if (t == "v" || t == "bohrer") return 0;
    double d = num(w, "durchmesser", 6), s = num(w, "schaft", d);
    if (s <= 0 || std::abs(s - d) < 0.01) return 0;
    double winkel = s > d ? 20.0 : 45.0;
    return std::abs(s - d) / 2 / std::tan(winkel * M_PI / 180);
}

/// Ausspannlänge ohne Angabe: Schneidenlänge + Übergang + 5 mm (wie relief_halterung.ausspannlaenge)
inline double werkzeug_ausspann_auto(bj::object const &w)
{
    return num(w, "schneidenlaenge", 20) + werkzeug_uebergang(w) + 5;
}

inline double werkzeug_ausspann(bj::object const &w)
{
    double a = num(w, "ausspannlaenge", 0);
    return a > 0 ? a : werkzeug_ausspann_auto(w);
}

/// Seitenansicht eines Werkzeugs in den Farben der Fräserbilder (Schaft hell, Übergang als Kegel,
/// Schneide grau), massstäblich
/// in w × h; mit Massangaben und Spannmutter (gestrichelt), wenn «beschriften».
inline void werkzeug_skizze(Cairo::RefPtr<Cairo::Context> const &cr, int w, int h, bj::object const &wz,
                            bool beschriften, Gdk::RGBA const *schrift = nullptr)
{
    auto t = str(wz, "typ", "schaft");
    double d = std::max(0.1, num(wz, "durchmesser", 6));
    double sl = std::max(0.5, num(wz, "schneidenlaenge", 20));
    double winkel = std::clamp(num(wz, "v_winkel", 60), 1.0, 170.0);
    double spitze = std::clamp(num(wz, "spitze", 0), 0.0, d);
    double er = t == "kugel" ? d / 2 : std::clamp(num(wz, "eckradius", 0), 0.0, d / 2);
    double bw = std::clamp(num(wz, "spitzenwinkel", 118), 30.0, 179.0);
    // Höhe der Schneide: beim V-Fräser bis zum vollen Durchmesser
    double kegel = t == "v" ? (d - spitze) / 2 / std::tan(winkel / 2 * M_PI / 180)
                 : t == "bohrer" ? d / 2 / std::tan(bw / 2 * M_PI / 180) : 0;
    double schneide = t == "v" ? std::min(std::max(kegel, 0.3 * d), sl + kegel) : sl;
    // Schaft im eigenen Ø (dicker oder dünner als die Schneide), dazwischen der Übergang als Kegel
    bool mit_schaft = t != "v" && t != "bohrer";
    double ds = mit_schaft ? std::max(0.1, num(wz, "schaft", d)) : d;
    double ue = mit_schaft ? werkzeug_uebergang(wz) : 0;
    double breite = std::max(d, ds);
    double schaft = std::max(schneide * 0.7, breite * 1.4);
    double ausspann = werkzeug_ausspann(wz);
    double gesamt = schneide + ue + schaft;
    if (beschriften) gesamt = std::max(gesamt, ausspann * 1.12 + 1);   // Spannmutter noch im Bild
    double rand = beschriften ? 26 : 3;
    double massstab = std::min((w - 2 * rand) / breite, (h - 2 * rand) / gesamt);
    if (beschriften) massstab = std::min(massstab, (w - 2 * rand) / std::max(breite, 3.0));
    double cx = w / 2.0, unten = h - rand - (beschriften ? 8 : 0);
    double r = d / 2 * massstab;
    double hs = schneide * massstab, hu = ue * massstab, hh = (gesamt - schneide - ue) * massstab;
    if (unten - hs - hu - hh < rand) hh = std::max(4.0, unten - hs - hu - rand);
    // Schaft und Übergang (hell wie in den Fräserbildern, Peter 20.10.)
    cr->set_source_rgb(0.81, 0.81, 0.80);
    double rs = ds / 2 * massstab;
    cr->rectangle(cx - rs, unten - hs - hu - hh, 2 * rs, hh);
    cr->fill();
    if (hu > 0) {
        cr->move_to(cx - r, unten - hs + 0.5);
        cr->line_to(cx + r, unten - hs + 0.5);
        cr->line_to(cx + rs, unten - hs - hu);
        cr->line_to(cx - rs, unten - hs - hu);
        cr->close_path();
        cr->fill();
    }
    // Schneide
    cr->begin_new_path();
    if (t == "bohrer") {
        double kh = std::min(hs, kegel * massstab);
        cr->move_to(cx, unten);
        cr->line_to(cx + r, unten - kh);
        cr->line_to(cx + r, unten - hs);
        cr->line_to(cx - r, unten - hs);
        cr->line_to(cx - r, unten - kh);
        cr->close_path();
    } else if (t == "konisch") {
        // Kugelspitze (Radius er), tangential in den Kegel bis zum grössten Ø oben an der Schneide
        double rb = std::clamp(num(wz, "eckradius", 0.25), 0.01, d / 2) * massstab;
        double halb = winkel / 2 * M_PI / 180;
        double dt = rb * std::cos(halb), ht = rb - rb * std::sin(halb);
        cr->move_to(cx - r, unten - hs);
        cr->line_to(cx + r, unten - hs);
        cr->line_to(cx + dt, unten - ht);
        cr->arc(cx, unten - rb, rb, M_PI / 2 - halb, M_PI / 2 + halb);
        cr->line_to(cx - r, unten - hs);
        cr->close_path();
    } else if (t == "v") {
        double sp = spitze / 2 * massstab;
        cr->move_to(cx - sp, unten);
        cr->line_to(cx + sp, unten);
        cr->line_to(cx + r, unten - std::min(hs, kegel * massstab));
        cr->line_to(cx + r, unten - hs);
        cr->line_to(cx - r, unten - hs);
        cr->line_to(cx - r, unten - std::min(hs, kegel * massstab));
        cr->close_path();
    } else {
        double rr = er * massstab;
        cr->move_to(cx - r, unten - hs);
        cr->line_to(cx + r, unten - hs);
        cr->line_to(cx + r, unten - rr);
        if (rr > 0) cr->arc(cx + r - rr, unten - rr, rr, 0, M_PI / 2);
        cr->line_to(cx - r + rr, unten);
        if (rr > 0) cr->arc(cx - r + rr, unten - rr, rr, M_PI / 2, M_PI);
        cr->close_path();
    }
    if (t == "gewinde") {
        // Gewindefräser: schmaler Hals, unten die Gewindezähne (Dreiecke)
        cr->begin_new_path();
        int z = std::max(1, (int)num(wz, "zaehne", 1));
        double zh = std::min(hs / (z + 0.5), r * 0.9);
        double hals = r * 0.45;
        cr->rectangle(cx - hals, unten - hs, 2 * hals, hs);
        for (int i = 0; i < z; ++i) {
            double y = unten - zh * (i + 0.5);
            cr->move_to(cx - hals, y - zh * 0.45);
            cr->line_to(cx - r, y);
            cr->line_to(cx - hals, y + zh * 0.45);
            cr->line_to(cx + hals, y + zh * 0.45);
            cr->line_to(cx + r, y);
            cr->line_to(cx + hals, y - zh * 0.45);
            cr->close_path();
        }
    }
    cr->set_source_rgb(0.68, 0.68, 0.70);
    cr->fill_preserve();
    cr->set_source_rgb(0.47, 0.46, 0.48);
    cr->set_line_width(1.0);
    cr->stroke();
    if (t == "bohrer") {
        // Spannuten: schräge Striche über die Schneide
        double kh = std::min(hs, kegel * massstab);
        double steig = std::max(4.0, 1.6 * r);
        cr->save();
        cr->rectangle(cx - r, unten - hs, 2 * r, hs - kh);
        cr->clip();
        cr->set_source_rgb(0.27, 0.27, 0.30);
        cr->set_line_width(1.0);
        for (double y = unten - kh + steig; y > unten - hs - 2 * r; y -= steig) {
            cr->move_to(cx - r, y);
            cr->line_to(cx + r, y - r * 0.9);
        }
        cr->stroke();
        cr->restore();
    }
    if (!beschriften) return;
    // Masse
    if (schrift) cr->set_source_rgba(schrift->get_red(), schrift->get_green(), schrift->get_blue(), 0.75);
    else cr->set_source_rgb(0.35, 0.35, 0.38);
    cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_NORMAL);
    cr->set_font_size(11);
    auto text_mitte = [&](std::string const &s, double x, double y) {
        Cairo::TextExtents te;
        cr->get_text_extents(s, te);
        cr->move_to(x - te.width / 2 - te.x_bearing, y);
        cr->show_text(einheit_text_zahlen(s));
    };
    auto k = [](double v) { return kurzzahl(v); };
    // Länge mit Einheit: mm «6 mm», Zoll «1/4 Zoll» (Bruch) – nie für Winkel (Windows-Abnahme 09.11.: «R … Zoll mm»)
    auto L = [](double v) { return einheit_mass(v, kurzzahl(v) + TR(" mm")); };
    text_mitte(mit_schaft && std::abs(ds - d) > 0.01
                   ? (hirch_zoll() ? "Ø " + L(d) + TR(" · Schaft ") + L(ds)
                                   : "Ø " + k(d) + TR(" · Schaft ") + k(ds) + TR(" mm"))
                   : "Ø " + L(d),
               cx, unten - hs - hu - hh - 6);
    // Spannmutter: so weit ragt der Fräser heraus (orange, wenn sie auf Schneide oder Kegel säße)
    {
        double ym = unten - ausspann * massstab;
        if (ym > rand - 4) {
            bool kurz = num(wz, "ausspannlaenge", 0) > 0 && ausspann < schneide + ue - 1e-6 && t != "v";
            cr->save();
            if (kurz) cr->set_source_rgb(0.90, 0.55, 0.0);
            else cr->set_source_rgb(0.21, 0.52, 0.89);
            cr->set_line_width(1.5);
            std::vector<double> strich{5.0, 3.0};
            cr->set_dash(strich, 0);
            cr->move_to(4, ym);
            cr->line_to(w - 4, ym);
            cr->stroke();
            cr->restore();
        }
    }
    if (schrift) cr->set_source_rgba(schrift->get_red(), schrift->get_green(), schrift->get_blue(), 0.75);
    else cr->set_source_rgb(0.35, 0.35, 0.38);
    if (t == "v") {
        // Winkel rechts neben dem Kegel
        double yk = unten - std::min(hs, kegel * massstab) * 0.5;
        cr->move_to(cx + r * 0.55 + 8, yk + 4);
        cr->show_text(einheit_text_zahlen(k(winkel) + "°"));
        if (spitze > 0) text_mitte(TR("Spitze ") + L(spitze), cx, unten + 15);
    } else if (t == "eckradius" && er > 0) {
        text_mitte("R " + L(er), cx, unten + 15);
    } else if (t == "kugel") {
        text_mitte(TR("Kugel R ") + L(d / 2), cx, unten + 15);
    } else if (t == "konisch") {
        text_mitte(TR("Kugel R ") + L(num(wz, "eckradius", 0.25)) + " · " + k(winkel) + "°", cx, unten + 15);
    } else if (t == "bohrer") {
        double yk = unten - std::min(hs, kegel * massstab) * 0.5;
        cr->move_to(cx + r * 0.55 + 8, yk + 4);
        cr->show_text(einheit_text_zahlen(k(bw) + "°"));
    }
}

inline std::string markup_escape(std::string const &s)
{
    return Glib::Markup::escape_text(s).raw();
}

// ---------------------------------------------------------------------------
// «So fräse ich» (hirch_cam/vorgaben.py): Liste aus jeder Antwort von «datenbank» (Impl::python)
// ---------------------------------------------------------------------------
inline bj::object &hirch_vorgaben()
{
    static bj::object v;
    return v;
}

/// B-25: Liste der Maschinen aus jeder Antwort von «datenbank» (id, name, aktiv, x, y, drehzahl_max, fraeser …)
inline bj::array &hirch_maschinen()
{
    static bj::array m;
    return m;
}

/// Gespeicherte Vorgaben auf eine neue Bearbeitung legen – nur, was vom Standard abweicht
inline void vorgaben_anwenden(bj::object &b, std::string const &art)
{
    auto l = liste_von(hirch_vorgaben(), "liste");
    if (!l) return;
    for (auto const &e : *l) {
        auto o = e.if_object();
        if (!o) continue;
        auto f = unter(*o, "felder");
        auto w = o->find("wert");
        auto s = o->find("standard");
        if (!f || w == o->end()) continue;
        auto it = f->find(art);
        if (it == f->end() || !it->value().is_string()) continue;
        if (s != o->end() && s->value() == w->value()) continue;
        b[std::string(it->value().as_string())] = w->value();
    }
}

// ---------------------------------------------------------------------------
// Vorgaben (wie hirch_cam/einstellungen.py)
// ---------------------------------------------------------------------------
inline bj::object vorgabe_bearbeitung_ohne(std::string const &art);
inline bj::object vorgabe_bearbeitung(std::string const &art)
{
    auto b = vorgabe_bearbeitung_ohne(art);
    vorgaben_anwenden(b, art);
    return b;
}

inline bj::object vorgabe_bearbeitung_ohne(std::string const &art)
{
    bj::object b;
    b["art"] = art;
    b["aktiv"] = true;
    if (art == "vcarve") {
        b["werkzeug"] = "A"; b["max_tiefe"] = 5.0; b["flach_werkzeug"] = "";
    } else if (art == "gravur") {
        b["werkzeug"] = "A"; b["tiefe"] = 0.5; b["zustellung"] = 0.5;
    } else if (art == "tasche") {
        b["werkzeug"] = "B"; b["tiefe"] = 3.0; b["bahnabstand"] = 40;
        // Peter 01.11.: adaptiv ist die Vorgabe; adaptiv spart vor allem mit grosser Zustellung – die ganze Tiefe
        // (wie «Adaptiv übernehmen»); gespeicherte Taschen ohne «strategie» bleiben Runden (einstellungen.laden)
        b["strategie"] = "adaptiv"; b["zustellung"] = 3.0; b["adaptiv_last"] = 15;
    } else if (art == "kontur") {
        b["werkzeug"] = "C"; b["tiefe"] = nullptr; b["zustellung"] = 2.0; b["seite"] = "auto";
        b["gleichlauf"] = true; b["stege_anzahl"] = 4; b["stege_breite"] = 6.0; b["stege_hoehe"] = 1.5;
        b["anfahren"] = "bogen";        // Peter 25.10.: neue Konturen im Bogen anfahren (keine Marke)
    } else if (art == "bohren") {
        b["werkzeug"] = "B"; b["tiefe"] = 5.0; b["zustellung"] = 2.5; b["methode"] = "auto";
        b["gleichlauf"] = true; b["spitze_dazu"] = true;
    } else if (art == "gewinde") {
        b["werkzeug"] = "B"; b["tiefe"] = 8.0; b["steigung"] = nullptr; b["innen"] = true; b["rechts"] = true;
        b["durchgaenge"] = 1; b["passung"] = 0.0; b["kernloch_werkzeug"] = "";
    } else if (art == "planen") {
        b["werkzeug"] = "C"; b["tiefe"] = 0.5; b["zustellung"] = 0.5; b["bahnabstand"] = 60; b["richtung"] = "x";
        b["ueberstand"] = nullptr;
    } else if (art == "relief_schruppen") {
        b["werkzeug"] = "C"; b["relief"] = ""; b["zustellung"] = 3.0; b["aufmass"] = 0.5;
        b["bahnabstand"] = 40; b["rand"] = nullptr; b["lagen"] = 6.0;
    } else if (art == "relief_schlichten") {
        b["werkzeug"] = "C"; b["relief"] = ""; b["bahnabstand"] = 10; b["richtung"] = "x";
        b["aufmass"] = 0.0; b["rand"] = nullptr; b["abtastung"] = nullptr;
    } else if (art == "boeden_waende") {
        b["werkzeug"] = "C"; b["boeden"] = true; b["waende"] = true; b["aufmass"] = 0.0; b["bahnabstand"] = 40;
        b["stufe"] = 10.0; b["im_wechsel_mit"] = "";
    }
    return b;
}

inline bj::object vorgabe_werkzeug(bool v)
{
    bj::object w;
    w["typ"] = v ? "v" : "schaft";
    w["durchmesser"] = v ? 12.0 : 6.0;
    w["v_winkel"] = 60;
    w["drehzahl"] = 18000;
    w["vorschub"] = 1000;
    w["eintauchen"] = 300;
    w["nummer"] = 1;
    return w;
}

} // namespace Inkscape::UI::Dialog

#endif
