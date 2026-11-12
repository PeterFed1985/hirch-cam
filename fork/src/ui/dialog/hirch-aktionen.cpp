// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Fassungen, Willkommen, Aktionen (Menü «Fräsen»)
 */
#include <fstream>
#include <glib/gstdio.h>
#include "ui/dialog/hirch-hilfen.h"
#include <sstream>
#include <gtkmm/linkbutton.h>
#include "ui/dialog/hirch-3d.h"
#include "ui/dialog/hirch-rueckmeldung.h"
#include "actions/actions-extra-data.h"
#include "inkscape-application.h"

namespace Inkscape::UI::Dialog {
} // namespace Inkscape::UI::Dialog
using Inkscape::UI::Dialog::TR;

// ---------------------------------------------------------------------------
// Versionen: beim Speichern eine datierte Kopie
static std::string versionen_ordner(std::string const &pfad)
{
    auto stamm = Glib::path_get_basename(pfad);
    if (auto p = stamm.rfind('.'); p != std::string::npos && p > 0) stamm = stamm.substr(0, p);
    return Glib::build_filename(Glib::path_get_dirname(pfad), ".versionen", stamm);
}

std::vector<std::string> versionen_liste(std::string const &pfad)
{
    std::vector<std::string> aus;
    auto ordner = versionen_ordner(pfad);
    if (!Glib::file_test(ordner, Glib::FILE_TEST_IS_DIR)) return aus;
    try {
        Glib::Dir dir(ordner);
        for (auto const &n : dir) aus.push_back(Glib::build_filename(ordner, n));
    } catch (Glib::Error const &) {
    }
    std::sort(aus.begin(), aus.end());          // Name enthält Datum und Zeit: älteste zuerst
    return aus;
}

static void ordner_verstecken(std::string const &ordner);   // unten (Windows)

void hirch_version_sichern(std::string const &pfad)
{
    auto k = Glib::ustring(pfad).lowercase().raw();
    auto endet = [&](std::string const &e) { return k.size() >= e.size() && k.compare(k.size() - e.size(), e.size(), e) == 0; };
    // Hirch CNC: eigene Endung .hir (27.10.; .hirch war die erste Fassung desselben Tages)
    std::string const endung = endet(".hir") ? ".hir" : endet(".hirch") ? ".hirch" : ".svg";
    if (!endet(".svg") && !endet(".hir") && !endet(".hirch")) return;
    if (!Glib::file_test(pfad, Glib::FILE_TEST_EXISTS)) return;
    try {
        auto ordner = versionen_ordner(pfad);
        g_mkdir_with_parents(ordner.c_str(), 0755);
        ordner_verstecken(Glib::path_get_dirname(ordner));   // Peter 29.10.: «.versionen» unter Windows versteckt
        auto jetzt = Glib::DateTime::create_now_local();
        auto stamm = Glib::path_get_basename(ordner);
        auto ziel = Glib::build_filename(ordner, stamm + " " + jetzt.format("%Y-%m-%d %H-%M-%S").raw() + endung);
        Gio::File::create_for_path(pfad)->copy(Gio::File::create_for_path(ziel), Gio::FILE_COPY_OVERWRITE);
        auto alle = versionen_liste(pfad);
        for (std::size_t i = 0; i + 20 < alle.size(); ++i) Gio::File::create_for_path(alle[i])->remove();
    } catch (Glib::Error const &e) {
        g_warning("Hirch CAM: Version nicht gesichert: %s", e.what().c_str());
    }
}

// ---------------------------------------------------------------------------
// Aktionen für das Menü «Fräsen»
// ---------------------------------------------------------------------------
static Inkscape::UI::Dialog::HirchFraesen *panel_holen(InkscapeWindow *win)
{
    auto desktop = win->get_desktop();
    if (!desktop) return nullptr;
    auto container = desktop->getContainer();
    if (!container) return nullptr;
    container->new_dialog("Fraesen");
    return dynamic_cast<Inkscape::UI::Dialog::HirchFraesen *>(container->get_dialog("Fraesen"));
}

/// Reiter «Einstellungen» öffnen (Menü «Fräsen › Einstellungen»)
static Inkscape::UI::Dialog::HirchAnsicht *ansicht_holen(InkscapeWindow *win)
{
    auto desktop = win->get_desktop();
    if (!desktop) return nullptr;
    auto container = desktop->getContainer();
    if (!container) return nullptr;
    // immer in der einen Spalte rechts, nie als zweite Spalte (Peter 09.11.)
    hirch_spalte_zeigen(desktop, "HirchAnsicht");
    return dynamic_cast<Inkscape::UI::Dialog::HirchAnsicht *>(container->get_dialog("HirchAnsicht"));
}

bool hirch_zusatz_oeffnen(std::string const &pfad)
{
    // Endungen aus <Einstellungen>/zusatz/hirch_zusatz/formate.txt (eine je Zeile, z. B. «.abc»; wie
    // hirch_cam/zusatz.py). Ohne Zusatz gibt es keine. Gelesen einmal je Start.
    static std::vector<std::string> const endungen = [] {
        std::vector<std::string> e;
        std::string ordner;
        if (auto z = g_getenv("HIRCH_ZUSATZ_ORDNER"); z && *z) {
            ordner = z;
        } else {
#ifdef _WIN32
            auto a = g_getenv("APPDATA");           // wie Python (orte.konfig): Roaming, nicht Local
            ordner = Glib::build_filename(a && *a ? std::string(a) : Glib::get_home_dir(), "hirch-cam", "zusatz");
#else
            ordner = Glib::build_filename(Glib::get_user_config_dir(), "hirch-cam", "zusatz");
#endif
        }
        try {
            auto t = Glib::file_get_contents(Glib::build_filename(ordner, "hirch_zusatz", "formate.txt"));
            std::istringstream in(t);
            for (std::string z; std::getline(in, z);) {
                while (!z.empty() && std::isspace((unsigned char)z.back())) z.pop_back();
                if (!z.empty() && z[0] == '.') e.push_back(Glib::ustring(z).lowercase().raw());
            }
        } catch (...) {
        }
        return e;
    }();
    if (endungen.empty()) return false;
    auto klein = Glib::ustring(pfad).lowercase().raw();
    bool passt = false;
    for (auto const &e : endungen)
        passt = passt || (klein.size() > e.size() && klein.compare(klein.size() - e.size(), e.size(), e) == 0);
    if (!passt) return false;
    auto desk = SP_ACTIVE_DESKTOP;
    auto win = desk ? desk->getInkscapeWindow() : nullptr;
    auto panel = win ? panel_holen(win) : nullptr;
    if (!panel) return false;
    std::string p = pfad;
    Glib::signal_idle().connect_once([panel, p] { panel->zusatzOeffnen(p); });
    return true;
}

/// Endung für das Aufräumen: «illustrator» für eine SVG-Datei aus Illustrator, sonst die Endung klein
static std::string hirch_import_art(std::string const &pfad)
{
    auto p = pfad.rfind('.');
    if (p == std::string::npos) return "";
    auto e = Glib::ustring(pfad.substr(p + 1)).lowercase().raw();
    if (e == "svg" || e == "svgz") {
        // g_fopen: Pfad in UTF-8 auch unter Windows (Umlaute im Namen, Windows 11.11.)
        std::string kopf(8192, '\0');
        size_t n = 0;
        if (FILE *f = g_fopen(pfad.c_str(), "rb")) {
            n = fread(kopf.data(), 1, kopf.size(), f);
            fclose(f);
        }
        kopf.resize(n);
        if (kopf.find("Adobe Illustrator") != std::string::npos ||
            kopf.find("ns.adobe.com/AdobeIllustrator") != std::string::npos)
            return "illustrator";
    }
    return e;
}

static bool hirch_import_aufraeumen(std::string const &e, bool beim_oeffnen)
{
    // beim Öffnen nur Fremdes, das zu Linien wird; beim Import auch CAD (DXF, HPGL)
    for (char const *k : {"ai", "pdf", "eps", "ps", "illustrator"}) if (e == k) return true;
    if (!beim_oeffnen) for (char const *k : {"dxf", "plt", "hpgl"}) if (e == k) return true;
    return false;
}

/// Nach dem Import (Datei › Importieren) von DXF, AI, PDF …: das Eingefügte aufräumen
void hirch_nach_import(std::string const &pfad)
{
    auto e = hirch_import_art(pfad);
    if (!hirch_import_aufraeumen(e, false) || !Inkscape::Preferences::get()->getBool("/hirch/import_aufraeumen", true))
        return;
    auto desk = SP_ACTIVE_DESKTOP;
    auto win = desk ? desk->getInkscapeWindow() : nullptr;
    if (!win) return;
    Glib::signal_idle().connect_once([win, e] {
        if (auto panel = panel_holen(win)) panel->aufraeumen(e, true);
    });
}

/// Nach dem Öffnen (Datei › Öffnen) von AI, PDF, EPS oder einer SVG aus Illustrator: nur die Linien behalten
/// (Peter 10.11.: wie beim Import). Wartet, bis das Fenster des Dokuments vorne ist.
void hirch_nach_oeffnen(std::string const &pfad, SPDocument *doc)
{
    auto e = hirch_import_art(pfad);
    if (!doc || !hirch_import_aufraeumen(e, true)) return;
    auto versuche = std::make_shared<int>(0);
    Glib::signal_timeout().connect([doc, e, versuche]() -> bool {
        auto desk = SP_ACTIVE_DESKTOP;
        if (desk && desk->getDocument() == doc) {
            if (auto win = desk->getInkscapeWindow())
                if (auto panel = panel_holen(win)) panel->aufraeumen(e, true);
            return false;
        }
        return ++*versuche < 40;
    }, 250);
}

/// Fräs-Panel nur, wenn es schon offen ist – ohne es hervorzuholen (Umschalter
/// der Ansicht sollen den Reiter «Ansicht» nicht verlassen)
static Inkscape::UI::Dialog::HirchFraesen *panel_falls_offen(InkscapeWindow *win)
{
    auto desktop = win->get_desktop();
    auto container = desktop ? desktop->getContainer() : nullptr;
    if (!container) return nullptr;
    return dynamic_cast<Inkscape::UI::Dialog::HirchFraesen *>(container->get_dialog("Fraesen"));
}

namespace Inkscape::UI::Dialog {
namespace {
/// je Zeichnung (SPDocument::serial) die zuletzt gerechneten analyse["nullpunkte"]
std::map<unsigned long, bj::array> &gemerkte_nullpunkte()
{
    static std::map<unsigned long, bj::array> m;
    return m;
}

/// Ohne Analyse (keine Fräsdaten, Panel noch nicht gerechnet): der gewählte Nullpunkt der Zeichnung – aus ihren
/// Einstellungen, sonst «unten links» – auf der Seite, auf der der Punkt liegt (die Seite ist das Material).
/// Dieselbe Regel wie einstellungen.nullpunkt (rund: die Mitte).
bool nullpunkt_aus_einstellungen(SPDocument const *doc_c, Geom::Point bei, Geom::Point &aus)
{
    auto doc = const_cast<SPDocument *>(doc_c);
    bj::object e;
    if (auto root = doc->getReprRoot()) {
        if (auto t = root->attribute("hirch:einstellungen")) {
            try {
                auto wert_ = bj::parse(t);                     // erst den Wert halten, dann hineinzeigen
                if (auto o = wert_.if_object()) e = *o;
            } catch (...) {
            }
        }
    }
    Geom::OptRect seite;
    auto const &seiten = doc->getPageManager().getPages();
    for (auto page : seiten) {
        if (page->getDocumentRect().contains(bei)) {
            seite = page->getDocumentRect();
            break;
        }
    }
    if (!seite && !seiten.empty()) seite = seiten.front()->getDocumentRect();
    if (!seite) seite = doc->preferredBounds();
    if (!seite || seite->width() <= 0 || seite->height() <= 0) return false;
    double b = seite->width() / MM_PX, h = seite->height() / MM_PX, x = 0, y = 0;
    if (rund_einst(e)) {
        x = b / 2;
        y = h / 2;
    } else {
        bj::object np;
        if (auto n = unter(e, "nullpunkt")) np = *n;
        auto xy = str(np, "xy", "ul");
        double fy = xy.size() > 0 ? (xy[0] == 'm' ? 0.5 : xy[0] == 'o' ? 1.0 : 0.0) : 0.0;
        double fx = xy.size() > 1 ? (xy[1] == 'm' ? 0.5 : xy[1] == 'r' ? 1.0 : 0.0) : 0.0;
        x = fx * b + num(np, "dx", 0);
        y = fy * h + num(np, "dy", 0);
    }
    aus = Geom::Point(seite->left() + x * MM_PX, seite->bottom() - y * MM_PX);
    return true;
}
} // namespace

void hirch_nullpunkte_merken(SPDocument const *doc, bj::array const &nullpunkte)
{
    if (doc) gemerkte_nullpunkte()[doc->serial()] = nullpunkte;
}
} // namespace Inkscape::UI::Dialog

/// Der eine Werkstück-Nullpunkt für Lineale, Mausposition, Werkzeugleisten, Transformieren, Pfadeffekte und
/// «Anordnen»: die zuletzt gerechnete Analyse der Zeichnung (auch bei geschlossenem Panel), sonst der gewählte
/// Nullpunkt aus den Einstellungen auf der Seite des Punkts. Immer ein Punkt – nie Inkscapes Seitenecke.
bool hirch_nullpunkt_seite(SPDocument const *doc, Geom::Point const &doc_px, Geom::Point &aus_doc_px)
{
    using namespace Inkscape::UI::Dialog;
    if (!doc) return false;
    auto &m = gemerkte_nullpunkte();
    if (auto it = m.find(doc->serial()); it != m.end() && nullpunkt_aus_liste(it->second, doc_px, aus_doc_px))
        return true;
    return nullpunkt_aus_einstellungen(doc, doc_px, aus_doc_px);
}

bool hirch_punkt_anzeige(SPDesktop *desktop, SPItem const *item, Geom::Affine &item_zu_anzeige)
{
    if (!desktop || !item) return false;
    auto bb = item->documentVisualBounds();
    Geom::Point n;
    if (!bb || !hirch_nullpunkt_seite(item->document, bb->midpoint(), n)) return false;
    // Objekt → Dokument (px) → vom Nullpunkt aus → mm, Y nach oben (wie an der Maschine)
    item_zu_anzeige = item->i2doc_affine() * Geom::Translate(-n)
                      * Geom::Scale(1.0 / Inkscape::UI::Dialog::MM_PX, -1.0 / Inkscape::UI::Dialog::MM_PX);
    return true;
}

// ---------------------------------------------------------------------------
// Willkommen: Hirch, Inkscape und FreeCAD – mit Dank
// ---------------------------------------------------------------------------
namespace {
using Inkscape::UI::Dialog::label;
using Inkscape::UI::Dialog::klasse;
using Inkscape::UI::Dialog::hirch_prefix;

Gtk::Window *willkommen_fenster = nullptr;

std::string willkommen_datei(char const *name)
{
    return Glib::build_filename(hirch_prefix(), "share", "inkscape", "hirch-cam", "assets", "willkommen", name);
}

/// SVG als Bild in gegebener Breite; farbe: «#1e1e1c» im SVG durch diese Farbe ersetzen
Gtk::Image *svg_bild(std::string const &pfad, int breite, std::string const &farbe = {})
{
    auto bild = Gtk::make_managed<Gtk::Image>();
    try {
        std::string text = Glib::file_get_contents(pfad);
        if (!farbe.empty()) {
            for (std::size_t p = 0; (p = text.find("#1e1e1c", p)) != std::string::npos; p += farbe.size())
                text.replace(p, 7, farbe);
        }
        auto loader = Gdk::PixbufLoader::create("image/svg+xml", true);
        loader->signal_size_prepared().connect([&loader, breite](int w, int h) {
            if (w > 0) loader->set_size(breite, std::max(1, breite * h / w));
        });
        loader->write(reinterpret_cast<guint8 const *>(text.data()), text.size());
        loader->close();
        bild->set(loader->get_pixbuf());
    } catch (Glib::Error const &e) {
        g_warning("Hirch-Willkommen: %s: %s", pfad.c_str(), e.what().c_str());
    } catch (...) {
        g_warning("Hirch-Willkommen: %s nicht geladen", pfad.c_str());
    }
    return bild;
}

std::string hex_von(Gdk::RGBA const &c)
{
    char b[8];
    std::snprintf(b, sizeof b, "#%02x%02x%02x", int(c.get_red() * 255), int(c.get_green() * 255),
                  int(c.get_blue() * 255));
    return b;
}

void willkommen_zeigen(Gtk::Window *eltern)
{
    if (willkommen_fenster) {
        willkommen_fenster->present();
        return;
    }
    auto w = new Gtk::Window();
    willkommen_fenster = w;
    w->set_title(TR("Über Hirch CAM"));
    w->set_resizable(false);
    w->set_type_hint(Gdk::WINDOW_TYPE_HINT_DIALOG);
    // Beim Start ist das Hauptfenster oft noch nicht in seiner endgültigen Grösse
    // («mittig über dem Eltern» landete dann oben links): mittig auf dem Bildschirm,
    // auf dem das Hauptfenster liegt; modal, damit es auch unter Wayland mittig sitzt.
    if (eltern) {
        w->set_transient_for(*eltern);
        w->set_modal(true);
    }
    w->set_position(Gtk::WIN_POS_CENTER);
    auto haupt = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
    w->add(*haupt);
    haupt->add(*svg_bild(willkommen_datei("cnc.svg"), 340));

    auto rechts = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 10);
    rechts->set_margin_start(30);
    rechts->set_margin_end(30);
    rechts->set_margin_top(30);
    rechts->set_margin_bottom(22);
    rechts->set_size_request(400, -1);
    haupt->add(*rechts);
    auto fg = hex_von(w->get_style_context()->get_color(Gtk::STATE_FLAG_NORMAL));
    auto logo = svg_bild(willkommen_datei("hirch.svg"), 190, fg);
    logo->set_halign(Gtk::ALIGN_START);
    rechts->add(*logo);
    std::string version;
    try {
        version = Glib::file_get_contents(Glib::build_filename(hirch_prefix(), "VERSION"));
        version = version.substr(0, version.find_first_of("\r\n"));
    } catch (...) {
    }
    if (!version.empty()) rechts->add(*label("Version " + Inkscape::UI::Dialog::hirch_version_anzeige(version), "hirch-dim"));

    // Peter 30.10. 18:54 (Planung): richtiggestellt – aus FreeCAD stammen Teile, 3D mit OpenCAMLib/OpenCASCADE
    auto dank = label(TR("Danke! Hirch CAM steht auf den Schultern freier Programme: Inkscape liefert die ganze "
                      "Zeichenoberfläche, aus FreeCAD stammen wichtige Rechenteile der Fräsbahnen (Taschen, Versatz, "
                      "V-Carve). Fürs 3D-Fräsen helfen OpenCAMLib und OpenCASCADE. Ohne die vielen Freiwilligen, die "
                      "diese Programme entwickeln und verschenken, gäbe es Hirch CAM nicht."));
    dank->set_line_wrap(true);
    dank->set_max_width_chars(46);
    dank->set_margin_top(14);
    rechts->add(*dank);

    auto projekte = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 12);
    projekte->set_homogeneous(true);
    projekte->set_margin_top(10);
    // Spenden (Peter 27.10.): im Programm selbst gibt es dafür keinen Menüpunkt mehr, hier je Projekt ein Link
    for (auto [datei, name, was, spende, spende_url] :
         {std::tuple{"inkscape.svg", "Inkscape", TR("Zeichnen · inkscape.org"), TR("An Inkscape spenden"),
                     "https://inkscape.org/support-us/donate/"},
          std::tuple{"freecad.svg", "FreeCAD", TR("Fräsbahnen · freecad.org"), TR("An FreeCAD spenden"),
                     "https://wiki.freecad.org/Donate"}}) {
        auto karte = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 10);
        klasse(*karte, "hirch-karte");
        // B-12, Peter 30.10.: die Logos von Inkscape und FreeCAD stehen hier – und nur hier – beim Dank und dem
        // Spenden-Link (sonst gilt weiter: kein Inkscape-Logo in der Oberfläche)
        auto bild = svg_bild(willkommen_datei(datei), 40);
        bild->set_valign(Gtk::ALIGN_CENTER);
        karte->add(*bild);
        auto t = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 1);
        t->set_valign(Gtk::ALIGN_CENTER);
        t->add(*label(name, "hirch-titel"));
        auto l = label(was, "hirch-dim");
        klasse(*l, "hirch-klein");
        t->add(*l);
        auto sp = Gtk::make_managed<Gtk::LinkButton>(spende_url, spende);
        sp->set_halign(Gtk::ALIGN_START);
        sp->set_relief(Gtk::RELIEF_NONE);
        sp->set_tooltip_text(spende_url);
        klasse(*sp, "hirch-klein");
        t->add(*sp);
        karte->add(*t);
        projekte->add(*karte);
    }
    rechts->add(*projekte);
    auto frei = label(TR("Hirch CAM ist freie Software (GNU GPL)."), "hirch-dim");
    klasse(*frei, "hirch-klein");
    frei->set_line_wrap(true);
    frei->set_max_width_chars(52);
    rechts->add(*frei);

    auto platz = Gtk::make_managed<Gtk::Box>();
    platz->set_vexpand(true);
    rechts->add(*platz);
    auto unten = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 10);
    auto start = Gtk::make_managed<Gtk::CheckButton>(TR("Beim Start zeigen"));
    start->set_active(Inkscape::Preferences::get()->getBool("/hirch/willkommen", true));
    start->set_hexpand(true);
    start->signal_toggled().connect([start] {
        Inkscape::Preferences::get()->setBool("/hirch/willkommen", start->get_active());
    });
    unten->add(*start);
    auto los = Gtk::make_managed<Gtk::Button>(TR("Los geht's"));
    klasse(*los, "suggested-action");
    los->set_size_request(130, 36);
    los->signal_clicked().connect([w] { w->hide(); });
    unten->add(*los);
    rechts->add(*unten);

    w->signal_key_press_event().connect([w](GdkEventKey *e) {
        if (e->keyval == GDK_KEY_Escape) {
            w->hide();
            return true;
        }
        return false;
    }, false);
    w->signal_hide().connect([w] {
        willkommen_fenster = nullptr;
        using Inkscape::UI::Dialog::willkommen_wartet;
        using Inkscape::UI::Dialog::nach_willkommen;
        willkommen_wartet = false;
        auto offen = std::move(nach_willkommen);
        nach_willkommen.clear();
        for (auto &f : offen) Glib::signal_idle().connect_once(f);
        Glib::signal_idle().connect_once([w] { delete w; });
    });
    w->show_all();
    los->grab_focus();
    // ausdrücklich in die Mitte des Arbeitsbereichs des Bildschirms setzen (X11)
    auto anzeige = Gdk::Display::get_default();
    Glib::RefPtr<Gdk::Monitor> mon;
    if (anzeige && eltern && eltern->get_window()) mon = anzeige->get_monitor_at_window(eltern->get_window());
    if (!mon && anzeige) mon = anzeige->get_primary_monitor();
    if (mon) {
        Gdk::Rectangle r;
        mon->get_workarea(r);
        int bw = 0, bh = 0;
        w->get_size(bw, bh);
        w->move(r.get_x() + std::max(0, (r.get_width() - bw) / 2), r.get_y() + std::max(0, (r.get_height() - bh) / 2));
    }
}

} // namespace


// ---------------------------------------------------------------------------
// Provisorischer Nullpunkt (Peter 29.10.): ein oranges Kreuz in der Zeichnung (hirch:nullpunkt) ist für seine
// Seite X0 Y0, z. B. an einem im Vorjahr gravierten Namen. Frei verschiebbar (rastet wie jedes Objekt ein),
// wird nie gefräst. Rechenteil: svg_lesen «nullmarken», dokument «nullpunkt_seiten».
// ---------------------------------------------------------------------------
namespace Inkscape::UI::Dialog {
void hirch_nullpunkt_feld_auffrischen(SPDesktop *desktop);   // hirch-ansicht.cpp

SPItem *hirch_nullmarke_von(SPObject *o)
{
    for (; o; o = o->parent) {
        if (o->getRepr() && o->getRepr()->attribute("hirch:nullpunkt")) return cast<SPItem>(o);
    }
    return nullptr;
}

static void nullmarken_sammeln(SPObject *o, std::vector<SPItem *> &aus)
{
    for (auto &c : o->children) {
        if (c.getRepr() && c.getRepr()->attribute("hirch:nullpunkt")) {
            if (auto it = cast<SPItem>(&c)) aus.push_back(it);
        } else {
            nullmarken_sammeln(&c, aus);
        }
    }
}

bool hirch_hat_nullmarke(SPDocument *doc)
{
    if (!doc || !doc->getRoot()) return false;
    std::vector<SPItem *> m;
    nullmarken_sammeln(doc->getRoot(), m);
    return !m.empty();
}

static Geom::Rect seite_bei(SPDocument *doc, Geom::Point p)
{
    auto seiten = doc->getPageManager().getPages();
    for (auto pg : seiten) {
        if (pg->getDocumentRect().contains(p)) return pg->getDocumentRect();
    }
    if (!seiten.empty()) return seiten.front()->getDocumentRect();
    auto b = doc->preferredBounds();
    return b ? *b : Geom::Rect(0, 0, 100, 100);
}

/// Mitte des Kreuzes: der Kreis (erstes Pfad-Kind) – die Beschriftung «Kamera …» daneben zählt nicht mit
static Geom::Point nullmarke_mitte(SPItem *it)
{
    for (auto &c : it->children) {
        if (c.getRepr() && c.getRepr()->name() && std::string(c.getRepr()->name()) == "svg:path") {
            if (auto k = cast<SPItem>(&c)) {
                if (auto bb = k->documentGeometricBounds()) return bb->midpoint();
            }
        }
    }
    auto bb = it->documentGeometricBounds();
    return bb ? bb->midpoint() : Geom::Point();
}

static void nullmarke_setzen(SPDesktop *desktop, Geom::Point p)
{
    auto doc = desktop ? desktop->getDocument() : nullptr;
    if (!doc) return;
    auto seite = seite_bei(doc, p);
    std::vector<SPItem *> alte;
    nullmarken_sammeln(doc->getRoot(), alte);
    for (auto a : alte) {
        if (seite.contains(nullmarke_mitte(a))) a->deleteObject(true);    // eines je Seite
    }
    SPObject *ebene = desktop->layerManager().currentLayer();
    if (!ebene) ebene = doc->getRoot();
    auto xml = doc->getReprDoc();
    auto zahl_ = [](double v) {
        std::ostringstream o;
        o.imbue(std::locale::classic());
        o << v;
        return o.str();
    };
    double r = 2.5 * MM_PX, a = 6.0 * MM_PX;
    std::string stil = "fill:none;stroke:#e66100;stroke-width:" + zahl_(0.35 * MM_PX);
    auto g = xml->createElement("svg:g");
    g->setAttribute("hirch:nullpunkt", "provisorisch");
    g->setAttribute("inkscape:label", TR("Provisorischer Nullpunkt"));
    auto kreis = xml->createElement("svg:path");
    kreis->setAttribute("d", "M " + zahl_(-r) + ",0 A " + zahl_(r) + "," + zahl_(r) + " 0 1 0 " + zahl_(r) +
                                 ",0 A " + zahl_(r) + "," + zahl_(r) + " 0 1 0 " + zahl_(-r) + ",0 Z");
    kreis->setAttribute("style", stil + ";fill:#e66100;fill-opacity:0.15");
    auto kreuz = xml->createElement("svg:path");
    kreuz->setAttribute("d", "M " + zahl_(-a) + ",0 H " + zahl_(a) + " M 0," + zahl_(-a) + " V " + zahl_(a));
    kreuz->setAttribute("style", stil);
    g->appendChild(kreis);
    g->appendChild(kreuz);
    Inkscape::GC::release(kreis);
    Inkscape::GC::release(kreuz);
    ebene->getRepr()->appendChild(g);
    Inkscape::GC::release(g);
    auto item = cast<SPItem>(doc->getObjectByRepr(g));
    if (item) {
        Geom::Affine eltern = Geom::identity();
        if (auto e = cast<SPItem>(item->parent)) eltern = e->i2doc_affine();
        item->set_item_transform(Geom::Translate(p) * eltern.inverse());
        item->doWriteTransform(item->transform, nullptr, false);
        desktop->getSelection()->set(item);
    }
    DocumentUndo::done(doc, TR("Provisorischer Nullpunkt"), "");
    hirch_nullpunkt_feld_auffrischen(desktop);
}

/// X/Y des Kreuzes eintippen: mm von der Ecke unten links des Materials (der Seite)
static void nullmarke_beschriftung_auffrischen(SPItem *m);   // unten

static void nullmarke_eingeben(InkscapeWindow *win)
{
    auto desktop = win ? win->get_desktop() : nullptr;
    if (!desktop) return;
    SPItem *m = nullptr;
    for (auto it : desktop->getSelection()->items()) {
        if ((m = hirch_nullmarke_von(it))) break;
    }
    if (!m) return;
    auto doc = desktop->getDocument();
    auto mitte = nullmarke_mitte(m);
    auto seite = seite_bei(doc, mitte);
    Gtk::Dialog dlg(TR("Provisorischer Nullpunkt"), *win, true);
    dlg.set_resizable(false);
    auto box = dlg.get_content_area();
    box->set_spacing(10);
    box->set_border_width(14);
    auto l = label(TR("Lage des Kreuzes, gemessen von der Ecke unten links des Materials."), "hirch-dim");
    l->set_line_wrap(true);
    l->set_max_width_chars(44);
    l->set_xalign(0);
    box->add(*l);
    auto g = formular();
    auto sx = spin_zeile(*g, 0, "X", (mitte.x() - seite.left()) / MM_PX, -5000, 5000, 0.1, 2, "mm");
    auto sy = spin_zeile(*g, 1, "Y", (seite.bottom() - mitte.y()) / MM_PX, -5000, 5000, 0.1, 2, "mm");
    box->add(*g);
    dlg.add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
    klasse(*dlg.add_button(TR("Übernehmen"), Gtk::RESPONSE_OK), "suggested-action");
    dlg.set_default_response(Gtk::RESPONSE_OK);
    dlg.show_all();
    if (dlg.run() != Gtk::RESPONSE_OK) return;
    Geom::Point ziel(seite.left() + sx->get_value() * MM_PX, seite.bottom() - sy->get_value() * MM_PX);
    m->move_rel(Geom::Translate(desktop->doc2dt(ziel) - desktop->doc2dt(mitte)));   // move_rel rechnet in Desktop-Koordinaten (Y nach oben)
    nullmarke_beschriftung_auffrischen(m);     // Seite der Beschriftung neu wählen (Rand)
    DocumentUndo::done(doc, TR("Provisorischer Nullpunkt"), "");
    hirch_nullpunkt_feld_auffrischen(desktop);
}

/// Kamera-Versatz am Kreuz (Peter 29.10.): mit der Kamera am Fräskopf wird X0 Y0 auf dem Kreuz gesetzt, der Fräser
/// steht um den Versatz daneben. Eingetragen wird, was die Maschine zeigt, wenn die Kamera über einem bei X0 Y0
/// gefrästen Punkt steht. Gespeichert als hirch:kamera="dx,dy" an der Gruppe, dazu eine Beschriftung am Kreuz.
static void nullmarke_kamera_beschriften(SPItem *m, double dx, double dy, bool an)
{
    auto repr = m->getRepr();
    for (auto &c : m->children) {
        if (c.getRepr() && c.getRepr()->attribute("hirch:nullteil")) {
            c.deleteObject(true);
            break;
        }
    }
    if (!an) {
        repr->removeAttribute("hirch:kamera");
        return;
    }
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << dx << "," << dy;
    repr->setAttribute("hirch:kamera", o.str());
    auto xml = repr->document();
    auto t = xml->createElement("svg:text");
    t->setAttribute("hirch:nullteil", "kamera");
    std::ostringstream st;
    st.imbue(std::locale::classic());
    auto text = TRF("Kamera-Versatz X {} / Y {} mm", {zahl(dx, 2), zahl(dy, 2)});
    // Peter 29.10. (Windows-Befund): ragt die Beschriftung rechts über das Material, steht sie links vom Kreuz
    double breite = (7.5 + 0.62 * 3.2 * g_utf8_strlen(text.c_str(), -1)) * MM_PX;
    auto mitte = nullmarke_mitte(m);
    bool links = mitte.x() + breite > seite_bei(m->document, mitte).right();
    st << "font-size:" << 3.2 * MM_PX << "px;font-family:sans-serif;font-weight:bold;fill:#c64600;stroke:none"
       << (links ? ";text-anchor:end" : "");
    t->setAttribute("style", st.str());
    std::ostringstream px;
    px.imbue(std::locale::classic());
    px << (links ? -7.5 : 7.5) * MM_PX;
    t->setAttribute("x", px.str());
    t->setAttribute("y", std::to_string(0));
    auto inhalt = xml->createTextNode(text.c_str());
    t->appendChild(inhalt);
    Inkscape::GC::release(inhalt);
    repr->appendChild(t);
    Inkscape::GC::release(t);
}

/// Beschriftung mit dem gespeicherten Versatz neu schreiben (nach dem Verschieben: Seite links/rechts neu wählen)
static void nullmarke_beschriftung_auffrischen(SPItem *m)
{
    auto k = m->getRepr()->attribute("hirch:kamera");
    if (!k) return;
    double dx = 0, dy = 0;
    std::istringstream i(k);
    i.imbue(std::locale::classic());
    char komma = 0;
    if (i >> dx >> komma >> dy) nullmarke_kamera_beschriften(m, dx, dy, true);
}

static void nullmarke_kamera(InkscapeWindow *win)
{
    auto desktop = win ? win->get_desktop() : nullptr;
    if (!desktop) return;
    SPItem *m = nullptr;
    for (auto it : desktop->getSelection()->items()) {
        if ((m = hirch_nullmarke_von(it))) break;
    }
    if (!m) return;
    double dx = 0, dy = 0;
    bool an = false;
    if (auto a = m->getRepr()->attribute("hirch:kamera")) {
        std::istringstream is(a);
        is.imbue(std::locale::classic());
        char k = 0;
        an = bool(is >> dx >> k >> dy);
    }
    Gtk::Dialog dlg(TR("Kamera-Versatz"), *win, true);
    dlg.set_resizable(false);
    auto box = dlg.get_content_area();
    box->set_spacing(10);
    box->set_border_width(14);
    auto l = label(TR("Mit der Kamera am Fräskopf: einen Punkt bei X0 Y0 fräsen, mit der Kamera genau darüber fahren "
                      "und hier eintragen, was die Maschine dann zeigt. An der Maschine setzt du X0 Y0 mit der "
                      "Kamera auf dem orangen Kreuz – der Versatz ist im G-Code eingerechnet."), "hirch-dim");
    l->set_line_wrap(true);
    l->set_max_width_chars(50);
    l->set_xalign(0);
    box->add(*l);
    auto ein = Gtk::make_managed<Gtk::CheckButton>(TR("Kamera-Versatz verwenden"));
    ein->set_active(an);
    box->add(*ein);
    auto g = formular();
    auto sx = spin_zeile(*g, 0, "X", dx, -2000, 2000, 0.01, 2, "mm");
    auto sy = spin_zeile(*g, 1, "Y", dy, -2000, 2000, 0.01, 2, "mm");
    box->add(*g);
    auto grau = [ein, g] { g->set_sensitive(ein->get_active()); };
    ein->signal_toggled().connect(grau);
    grau();
    dlg.add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
    klasse(*dlg.add_button(TR("Übernehmen"), Gtk::RESPONSE_OK), "suggested-action");
    dlg.set_default_response(Gtk::RESPONSE_OK);
    dlg.show_all();
    if (dlg.run() != Gtk::RESPONSE_OK) return;
    nullmarke_kamera_beschriften(m, sx->get_value(), sy->get_value(), ein->get_active());
    DocumentUndo::done(desktop->getDocument(), TR("Kamera-Versatz"), "");
    hirch_nullpunkt_feld_auffrischen(desktop);
}

static void nullmarke_entfernen(InkscapeWindow *win)
{
    auto desktop = win ? win->get_desktop() : nullptr;
    if (!desktop) return;
    std::vector<SPItem *> weg;
    for (auto it : desktop->getSelection()->items()) {
        if (auto m = hirch_nullmarke_von(it); m && std::find(weg.begin(), weg.end(), m) == weg.end()) weg.push_back(m);
    }
    if (weg.empty()) return;
    desktop->getSelection()->clear();
    for (auto m : weg) m->deleteObject(true);
    DocumentUndo::done(desktop->getDocument(), TR("Provisorischen Nullpunkt entfernen"), "");
    hirch_nullpunkt_feld_auffrischen(desktop);
}
} // namespace Inkscape::UI::Dialog

/// Pfadeffekt aus der linken Werkzeugleiste: Rotierte Kopien, Versatz, Muster entlang Pfad
static void pfadeffekt_anwenden(InkscapeWindow *win, Glib::ustring const &art)
{
    auto desk = win ? win->get_desktop() : nullptr;
    if (!desk) return;
    auto sel = desk->getSelection();
    auto doc = desk->getDocument();
    auto meldung_ = [desk](char const *t) {
        desk->messageStack()->flash(Inkscape::WARNING_MESSAGE, t);
    };
    std::vector<SPItem *> items(sel->items().begin(), sel->items().end());
    SPItem *muster = nullptr;
    if (art == "skeletal") {
        if (items.size() != 2) {
            meldung_(TR("Muster entlang Pfad: zwei Objekte auswählen – das Muster (oben) und den Pfad, dem es folgt."));
            return;
        }
        // das obere Objekt ist das Muster (wie in Inkscape üblich)
        bool erstes_oben = sp_repr_compare_position(items[0]->getRepr(), items[1]->getRepr()) > 0;
        muster = erstes_oben ? items[0] : items[1];
        auto pfad = erstes_oben ? items[1] : items[0];
        sel->set(muster);
        sel->toCurves(true);
        muster = sel->singleItem();
        sel->set(pfad);
    } else if (items.size() != 1) {
        meldung_(items.empty() ? TR("Zuerst ein Objekt auswählen.") : TR("Bitte nur ein Objekt auswählen."));
        return;
    }
    auto item = sel->singleItem();
    if (!item) return;
    if (!Inkscape::LivePathEffect::can_have_lpe(item)) {
        sel->toCurves(true);
        item = sel->singleItem();
    }
    auto lpeitem = cast<SPLPEItem>(item);
    if (!lpeitem) {
        meldung_(TR("Auf dieses Objekt lässt sich kein Pfadeffekt anwenden."));
        return;
    }
    auto bb = lpeitem->documentVisualBounds();         // vor dem Effekt: wo das Objekt liegt
    Inkscape::LivePathEffect::Effect::createAndApply(art.c_str(), doc, lpeitem);
    // Anordnen: um den Nullpunkt des Werkstücks drehen, nicht um Inkscapes Vorgabe (linke Mitte des Objekts)
    if (art == "copy_rotate") {
        auto lpe = lpeitem->getCurrentLPE();
        Geom::Point n;
        if (lpe && bb && hirch_nullpunkt_seite(doc, bb->midpoint(), n)) {
            Geom::Point o = n * lpeitem->i2doc_affine().inverse();
            std::ostringstream os;
            os.imbue(std::locale::classic());
            os.precision(10);
            os << o.x() << "," << o.y();
            lpe->getRepr()->setAttribute("origin", os.str());
        }
    }
    if (muster) {
        // Musterform übernehmen (Pfaddaten), das Musterobjekt wird nicht mehr gebraucht
        if (auto shape = cast<SPShape>(muster); shape && shape->curve()) {
            auto pv = shape->curve()->get_pathvector() * muster->i2doc_affine();
            if (auto lpe = lpeitem->getCurrentLPE()) {
                lpe->getRepr()->setAttribute("pattern", sp_svg_write_path(pv));
                lpe->getRepr()->setAttribute("copytype", "repeated_stretched");
            }
            muster->deleteObject();
        }
    }
    if (auto lpe = lpeitem->getCurrentLPE()) lpe->refresh_widgets = true;
    Inkscape::DocumentUndo::done(doc, art == "copy_rotate" ? TR("Rotierte Kopien")
                            : art == "offset" ? TR("Versatz") : TR("Muster entlang Pfad"), "");
    // B-20 (Peter 03.11.): das Objekt bleibt ausgewählt, die Einstellungen erscheinen sofort. Beim Anwenden kann
    // Inkscape das Objekt ersetzen (Form → Pfad): über die Id neu holen, auch nach dem Öffnen des Fensters
    std::string id = lpeitem->getId() ? lpeitem->getId() : "";
    auto waehlen = [desk, doc, id] {
        if (id.empty()) return;
        if (auto it = cast<SPItem>(doc->getObjectById(id))) desk->getSelection()->set(it);
    };
    waehlen();
    win->activate_action("dialog-open", Glib::Variant<Glib::ustring>::create("LivePathEffect"));
    Glib::signal_idle().connect_once(waehlen);
}

void add_actions_hirch(InkscapeWindow *win)
{
    // Einmal umstellen (Peter, 23.10.): Grösse und Lage ohne Strichbreite – gefräst wird die Linie, nicht
    // der Strich; mit Strich nahm «B 20» bei gesperrtem Seitenverhältnis H 19,984 an
    {
        auto pr = Inkscape::Preferences::get();
        if (!pr->getBool("/hirch/umgestellt/geometrisch", false)) {
            pr->setInt("/tools/bounding_box", 1);
            pr->setBool("/hirch/umgestellt/geometrisch", true);
        }
    }
    win->add_action_with_parameter("hirch-lpe", Glib::VARIANT_TYPE_STRING, [win](Glib::VariantBase const &v) {
        auto art = Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(v).get();
        Glib::signal_idle().connect_once([win, art] { pfadeffekt_anwenden(win, art); });
    });
    // Tooltips der drei Pfadeffekt-Knöpfe: Inkscape ersetzt den Tooltip jedes Knopfs mit Aktion
    // durch die Daten der Aktion – ohne Eintrag bliebe er leer.
    if (auto app = InkscapeApplication::instance()) {
        auto esc = [](char const *t) { return Glib::Markup::escape_text(t); };
        app->get_action_extra_data().add_data({
            {"win.hirch-lpe('copy_rotate')", esc(TR("Anordnen")), "Hirch CAM",
             esc(TR("Das ausgewählte Objekt mehrfach im Kreis um einen Mittelpunkt kopieren. Anzahl und Winkel im Fenster Pfadeffekte."))},
            {"win.hirch-lpe('offset')", esc(TR("Kurve versetzen")), "Hirch CAM",
             esc(TR("Den Umriss um ein genaues Mass nach aussen oder innen versetzen."))},
            {"win.hirch-lpe('skeletal')", esc(TR("An Kurve ausrichten")), "Hirch CAM",
             esc(TR("Zwei Objekte auswählen: Das obere (Muster) wird entlang des unteren Pfads wiederholt."))},
        });
    }
    win->add_action("hirch-willkommen", [win] { willkommen_zeigen(win); });
    // Hilfe › Rückmeldung senden … (Sitzung rueckmeldungen; Fenster in hirch-rueckmeldung.cpp)
    win->add_action("hirch-rueckmeldung", [win] {
        Inkscape::UI::Dialog::RueckmeldungKontext k;
        k.python = Inkscape::UI::Dialog::hirch_python();
        k.pythonpath = Inkscape::UI::Dialog::hirch_repo() + G_SEARCHPATH_SEPARATOR_S +
                       Inkscape::IO::Resource::get_path_string(Inkscape::IO::Resource::SYSTEM,
                                                               Inkscape::IO::Resource::EXTENSIONS);
        try {
            k.version = Glib::file_get_contents(Glib::build_filename(hirch_prefix(), "VERSION"));
            k.version = k.version.substr(0, k.version.find_first_of("\r\n"));
            // öffentliche Fassung: dazu der Bau (Datum-Commit), damit eine Meldung zum Stand passt
            auto bau = Glib::file_get_contents(Glib::build_filename(hirch_prefix(), "share", "hirch-cam", "BAU"));
            bau = bau.substr(0, bau.find_first_of("\r\n"));
            if (!bau.empty() && bau != k.version) k.version += " (" + bau + ")";
        } catch (...) {
        }
        k.sprache = Inkscape::UI::Dialog::hirch_sprache();
        if (auto d = win->get_document()) {
            k.zeichnung_name = d->getDocumentFilename() ? Glib::path_get_basename(d->getDocumentFilename())
                                                        : std::string(TR("Unbenannt"));
            // nur auf das Häkchen hin: jetziger Stand, auch ungespeichert
            k.zeichnung_schreiben = [win](std::string const &pfad) {
                auto doc = win->get_document();
                return doc && sp_repr_save_file(doc->getReprDoc(), pfad.c_str(), SP_SVG_NS_URI);
            };
        }
        Inkscape::UI::Dialog::rueckmeldung_zeigen(win, std::move(k));
    });
    // Provisorischer Nullpunkt: «x,y» = Punkt des Rechtsklicks in Dokument-px (contextmenu.cpp)
    win->add_action_with_parameter("hirch-nullpunkt-hier", Glib::VARIANT_TYPE_STRING, [win](Glib::VariantBase const &v) {
        auto t = Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(v).get().raw();
        double x = 0, y = 0;
        std::istringstream is(t);
        is.imbue(std::locale::classic());
        char komma = 0;
        if (!(is >> x >> komma >> y)) return;
        Inkscape::UI::Dialog::nullmarke_setzen(win->get_desktop(), Geom::Point(x, y));
    });
    win->add_action("hirch-nullpunkt-eingeben", [win] { Inkscape::UI::Dialog::nullmarke_eingeben(win); });
    win->add_action("hirch-nullpunkt-entfernen", [win] { Inkscape::UI::Dialog::nullmarke_entfernen(win); });
    win->add_action("hirch-nullpunkt-kamera", [win] { Inkscape::UI::Dialog::nullmarke_kamera(win); });
    // Einmal je Start begrüssen (abschaltbar im Fenster selbst)
    static bool begruesst = false;
    if (Inkscape::UI::Dialog::hirch_oberflaechentest_an()) {
        begruesst = true;               // der Test öffnet das Willkommen selbst
        static bool laeuft = false;
        if (!laeuft) {
            laeuft = true;
            Glib::signal_timeout().connect_once([win] { Inkscape::UI::Dialog::hirch_oberflaechentest(win); }, 1500);
        }
    }
    if (!begruesst) {
        begruesst = true;
        // allererster Start: zuerst die Ersteinrichtung (Peter 08.11.), danach das Willkommen wie immer
        bool willkommen = Inkscape::Preferences::get()->getBool("/hirch/willkommen", true);
        if (willkommen) Inkscape::UI::Dialog::willkommen_wartet = true;
        Glib::signal_timeout().connect_once([win, willkommen] {
            if (auto p = panel_holen(win)) p->ersteinrichtungWennNoetig();
            if (willkommen) willkommen_zeigen(win);
        }, 400);
    }
    win->add_action("hirch-material", [win] { if (auto p = panel_holen(win)) p->materialBearbeiten(); });
    win->add_action("hirch-werkzeuge", [win] { if (auto p = panel_holen(win)) p->werkzeugeBearbeiten(); });
    win->add_action("hirch-vorschau", [win] { if (auto p = panel_holen(win)) p->vorschauZeigen(); });
    win->add_action("hirch-speichern", [win] { if (auto p = panel_holen(win)) p->gcodeSpeichern(); });
    win->add_action("hirch-gravurtext", [win] { if (auto p = panel_holen(win)) p->gravurText(); });
    win->add_action("hirch-aktualisieren", [win] { if (auto p = panel_holen(win)) p->aktualisieren(); });
    win->add_action("hirch-spannmittel", [win] { if (auto p = panel_holen(win)) p->spannmittel(); });
    win->add_action("hirch-stege", [win] { if (auto p = panel_holen(win)) p->stege(); });
    win->add_action("hirch-verschachteln", [win] { if (auto p = panel_holen(win)) p->verschachteln(); });
    win->add_action("hirch-aufraeumen", [win] { if (auto p = panel_holen(win)) p->aufraeumen("", false); });
    win->add_action("hirch-als-vorlage", [win] { if (auto p = panel_holen(win)) p->alsVorlage(); });
    win->add_action("hirch-gespiegelt-speichern", [win] { if (auto p = panel_holen(win)) p->gespiegeltSpeichern(); });
    win->add_action("hirch-aus-vorlage", [win] { if (auto p = panel_holen(win)) p->ausVorlage(); });
    win->add_action("hirch-fassungen", [win] { if (auto p = panel_holen(win)) p->fassungen(); });
    win->add_action("hirch-postprozessoren", [win] { if (auto p = panel_holen(win)) p->postprozessoren(); });
    win->add_action("hirch-3d-einlesen", [win] { hirch_3d_einlesen(win); });
    win->add_action("hirch-3d-modell", [win] { hirch_3d_fenster(win->get_desktop()); });
    // Menü «Fräsen» nach Themen (Sammelauftrag 21.10., Punkt 2): dieselben Funktionen wie Panel und Leiste
    win->add_action_with_parameter("hirch-neu", Glib::VARIANT_TYPE_STRING, [win](Glib::VariantBase const &v) {
        auto art = Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(v).get().raw();
        if (auto p = panel_holen(win)) p->neuAnlegen(art);
    });
    win->add_action("hirch-ordnen", [win] { if (auto p = panel_holen(win)) p->ordnen(); });
    win->add_action("hirch-rechnen", [win] { if (auto p = panel_holen(win)) p->jetztRechnen(); });
    win->add_action("hirch-zusammenfassung", [win] { if (auto p = panel_holen(win)) p->zusammenfassungZeigen(); });
    win->add_action("hirch-usb", [win] { if (auto p = panel_holen(win)) p->aufStickSpeichern(); });
    win->add_action("hirch-luftfraesen", [win] { if (auto p = panel_holen(win)) p->luftfraesenSpeichern(); });
    win->add_action("hirch-werkzeuge-export", [win] { if (auto p = panel_holen(win)) p->werkzeugeExportieren(); });
    win->add_action("hirch-werkzeuge-import", [win] { if (auto p = panel_holen(win)) p->werkzeugeImportieren(); });
    win->add_action("hirch-messkorrektur", [win] { if (auto p = panel_holen(win)) p->messkorrektur(); });
    win->add_action("hirch-halterung", [win] { if (auto p = panel_holen(win)) p->halterung(); });
    win->add_action("hirch-schrift-import", [win] {
        if (auto a = ansicht_holen(win)) {
            a->abschnittZeigen("SCHRIFTEN");
            Glib::signal_idle().connect_once([a] { a->schriftImportieren(); });
        }
    });
    win->add_action("hirch-zeichentafel", [win] {
        if (auto a = ansicht_holen(win)) {
            a->abschnittZeigen("SCHRIFTEN");
            Glib::signal_idle().connect_once([a] { a->zeichentafel(); });
        }
    });
    win->add_action_with_parameter("hirch-einstellungen", Glib::VARIANT_TYPE_STRING, [win](Glib::VariantBase const &v) {
        auto k = Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(v).get().raw();
        if (k == "MASCHINE") panel_holen(win);       // der Abschnitt «Maschine» kommt vom Fräsen-Panel
        if (auto a = ansicht_holen(win)) a->abschnittZeigen(k);
    });
    // «Andere Seite blass zeigen»: eine Einstellung für Menü, Leiste und Reiter «Einstellungen»
    win->add_action_bool("hirch-andere-seite", [win] {
        auto pr = Inkscape::Preferences::get();
        pr->setBool("/hirch/andere_seite", !pr->getBool("/hirch/andere_seite", true));
    }, Inkscape::Preferences::get()->getBool("/hirch/andere_seite", true));
    {
        // Häkchen folgt, wenn Leiste oder Reiter «Einstellungen» umschalten; je Fenster, bis es zugeht
        static std::map<InkscapeWindow *, Inkscape::PrefObserver> beobachter;
        beobachter[win] = Inkscape::Preferences::get()->createObserver("/hirch/andere_seite", [win] {
            if (auto a = Glib::RefPtr<Gio::SimpleAction>::cast_dynamic(win->lookup_action("hirch-andere-seite")))
                a->change_state(Inkscape::Preferences::get()->getBool("/hirch/andere_seite", true));
        });
        win->signal_hide().connect([win] { beobachter.erase(win); });
    }
    bool an = Inkscape::Preferences::get()->getBool("/hirch/fraesbahnen", true);
    win->add_action_bool("hirch-bahnen", [win] {
        auto a = Glib::RefPtr<Gio::SimpleAction>::cast_dynamic(win->lookup_action("hirch-bahnen"));
        bool st = true;
        if (a) {
            a->get_state(st);
            st = !st;
            a->change_state(st);
        }
        Inkscape::Preferences::get()->setBool("/hirch/fraesbahnen", st);
        if (auto p = panel_falls_offen(win)) p->bahnenZeigen(st);
    }, an);

    Inkscape::UI::Dialog::hirch_ansicht_aktionen(win);    // 2D Zeichnung | 3D Ansicht
    Inkscape::hirch_linien_max = 1.0;   // Linien immer fein (die Strichbreite bedeutet beim Fräsen nichts)
    win->add_action_bool("hirch-dunkel", [win] {
        bool st = !Inkscape::UI::Dialog::design_dunkel();
        Inkscape::UI::Dialog::design_setzen(st, win);
        if (auto a = Glib::RefPtr<Gio::SimpleAction>::cast_dynamic(win->lookup_action("hirch-dunkel")))
            a->change_state(Inkscape::UI::Dialog::design_dunkel());
        if (auto p = panel_falls_offen(win)) p->darstellungZeigen();
    }, Inkscape::UI::Dialog::design_dunkel());
}

// ---------------------------------------------------------------------------
// Ganz am Ende: windows.h bringt Makros mit, die oben stören würden
#ifdef _WIN32
#include <windows.h>
static void ordner_verstecken(std::string const &ordner)
{
    // unter Linux/macOS versteckt schon der Punkt am Anfang; Windows braucht das Merkmal «versteckt»
    gunichar2 *w = g_utf8_to_utf16(ordner.c_str(), -1, nullptr, nullptr, nullptr);
    if (!w) return;
    DWORD a = GetFileAttributesW(reinterpret_cast<LPCWSTR>(w));
    if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_HIDDEN))
        SetFileAttributesW(reinterpret_cast<LPCWSTR>(w), a | FILE_ATTRIBUTE_HIDDEN);
    g_free(w);
}
#else
static void ordner_verstecken(std::string const &) {}
#endif
