// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CAM: Fenster «Rückmeldung senden» – siehe hirch-rueckmeldung.h.
 *
 * Regeln (Peter 03./04.11.2026): nichts vorangehakt ausser «Kein Bild»; Zeichnung nur mit Häkchen und Warnhinweis;
 * Version, Betriebssystem und Sprache sichtbar; Senden nur auf Knopfdruck; ohne Verbindung klare Meldung, die
 * Eingaben bleiben stehen.
 */
#include "ui/dialog/hirch-rueckmeldung.h"
#include "ui/dialog/hirch-sprache.h"

#include <boost/json.hpp>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/main.h>
#include <glibmm/markup.h>
#include <glibmm/miscutils.h>
#include <gdkmm/pixbuf.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/entry.h>
#include <gtkmm/filechoosernative.h>
#include <gtkmm/filefilter.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/linkbutton.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/overlay.h>
#include <gtkmm/radiobutton.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>
#include <gtkmm/spinner.h>
#include <gtkmm/stylecontext.h>
#include <gtkmm/textview.h>
#include <gtkmm/window.h>

#include <memory>

namespace bj = boost::json;

namespace Inkscape::UI::Dialog {

namespace {

constexpr int MAX_TEXT = 5000;
constexpr goffset MAX_BILD = 5 * 1024 * 1024;
constexpr goffset MAX_ZEICHNUNG = 20 * 1024 * 1024;

std::string groesse(goffset b)
{
    if (b < 1024 * 1024) return std::to_string(std::max<goffset>(1, (b + 512) / 1024)) + " KB";
    char t[32];
    g_snprintf(t, sizeof t, "%.1f MB", b / 1048576.0);
    return t;
}

goffset dateigroesse(std::string const &pfad)
{
    GStatBuf st;
    return g_stat(pfad.c_str(), &st) == 0 ? goffset(st.st_size) : -1;
}

bool ist_png_jpg(std::string const &pfad)
{
    FILE *f = g_fopen(pfad.c_str(), "rb");
    if (!f) return false;
    unsigned char k[8] = {};
    auto n = fread(k, 1, 8, f);
    fclose(f);
    static unsigned char const png[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    return (n == 8 && memcmp(k, png, 8) == 0) || (n >= 3 && k[0] == 0xff && k[1] == 0xd8 && k[2] == 0xff);
}

Gtk::Label *text(std::string const &t, char const *klasse = nullptr, bool markup = false)
{
    auto l = Gtk::make_managed<Gtk::Label>();
    if (markup) l->set_markup(t); else l->set_text(t);
    l->set_xalign(0);
    l->set_line_wrap(true);
    l->set_max_width_chars(40);   // umbrechen; die Breite gibt das Fenster vor
    if (klasse) l->get_style_context()->add_class(klasse);
    return l;
}

Gtk::Label *ueberschrift(std::string const &t)
{
    return text("<b>" + Glib::Markup::escape_text(t) + "</b>", nullptr, true);
}

void stil_einmal()
{
    static bool da = false;
    if (da) return;
    da = true;
    auto css = Gtk::CssProvider::create();
    css->load_from_data(
        ".hirch-rm-dim { opacity: 0.7; font-size: 90%; }"
        ".hirch-rm-warnung { background-color: rgba(229,193,74,0.20); border: 1px solid rgba(229,193,74,0.85);"
        "  border-radius: 6px; padding: 6px 10px; }"
        ".hirch-rm-fehler { background-color: rgba(217,83,79,0.16); border: 1px solid rgba(217,83,79,0.85);"
        "  border-radius: 6px; padding: 7px 10px; }"
        ".hirch-rm-immer { background-color: alpha(currentColor, 0.07); border-radius: 6px; padding: 6px 10px; }"
        ".hirch-rm-vorschau { border: 1px solid alpha(currentColor, 0.25); border-radius: 3px; }"
        ".hirch-rm-zuviel { color: #d9534f; }"
        "button.hirch-rm-art:checked { background-image: none; background-color: @theme_selected_bg_color;"
        "  color: @theme_selected_fg_color; border-color: @theme_selected_bg_color; }");
    Gtk::StyleContext::add_provider_for_screen(Gdk::Screen::get_default(), css,
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

/// Bildschirmfoto des Hirch-Fensters (ohne das Rückmeldungs-Fenster) als PNG
bool foto_machen(Gtk::Window *eltern, std::string const &pfad)
{
    if (!eltern) return false;
    auto gw = eltern->get_window();
    if (!gw) return false;
    int w = gw->get_width(), h = gw->get_height();
    if (w <= 0 || h <= 0) return false;
    Glib::RefPtr<Gdk::Pixbuf> pb;
    try {
        pb = Gdk::Pixbuf::create(gw, 0, 0, w, h);   // X11 und Windows: so, wie es auf dem Bildschirm steht
    } catch (...) {
    }
    if (!pb) {                                       // sonst (z. B. Wayland): das Fenster selbst zeichnen lassen
        int s = eltern->get_scale_factor();
        auto fl = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, w * s, h * s);
        cairo_surface_set_device_scale(fl->cobj(), s, s);
        auto cr = Cairo::Context::create(fl);
        gtk_widget_draw(GTK_WIDGET(eltern->gobj()), cr->cobj());
        fl->flush();
        pb = Gdk::Pixbuf::create(fl, 0, 0, w * s, h * s);
    }
    if (!pb) return false;
    try {
        pb->save(pfad, "png");
        return true;
    } catch (...) {
        return false;
    }
}

struct Fenster;
Fenster *offen = nullptr;

struct Fenster : Gtk::Window {
    RueckmeldungKontext k;
    Gtk::Window *eltern;
    std::string ordner;               // Zwischenordner: Foto, Zeichnung
    std::string foto, eigenes, zeichnung;
    std::string adresse;              // vom Python-Teil (--info); leer = keine eingerichtet
    GCancellable *abbruch = nullptr;
    GSubprocess *proc = nullptr;
    std::shared_ptr<bool> lebt = std::make_shared<bool>(true);

    Gtk::Box *fehler_box = nullptr;
    Gtk::Label *fehler_titel = nullptr, *fehler_text = nullptr;
    Gtk::RadioButton *art_fehler = nullptr, *art_wunsch = nullptr, *art_frage = nullptr;
    Gtk::TextView *eingabe = nullptr;
    Gtk::Label *platzhalter = nullptr, *zaehler = nullptr;
    Gtk::Entry *email = nullptr;
    Gtk::RadioButton *kein_bild = nullptr, *mit_foto = nullptr, *mit_eigenem = nullptr;
    Gtk::Button *datei_waehlen = nullptr;
    Gtk::Box *vorschau = nullptr;
    Gtk::Image *vorschau_bild = nullptr;
    Gtk::Label *vorschau_text = nullptr;
    Gtk::Button *neu_aufnehmen = nullptr;
    Gtk::CheckButton *mit_zeichnung = nullptr;
    Gtk::CheckButton *website = nullptr;          // Einwilligung: darf ohne Namen auf der Website erscheinen
    Gtk::Label *warnung = nullptr, *zeichnung_info = nullptr;
    Gtk::Label *immer = nullptr;
    Gtk::LinkButton *datenschutz = nullptr;
    Gtk::Button *abbrechen = nullptr, *senden = nullptr;
    Gtk::Spinner *spinner = nullptr;
    std::string system = "…";
    bool hide_ohne_schliessen = false;
    bool fehler_gezeigt = false;
    bool adresse_bekannt = false;     // --info ist zurück (vorher ist «leer» noch keine Aussage)

    Fenster(Gtk::Window *e, RueckmeldungKontext kontext) : k(std::move(kontext)), eltern(e)
    {
        stil_einmal();
        set_title(TR("Rückmeldung senden"));
        set_default_size(560, -1);
        set_size_request(560, -1);
        set_resizable(false);
        set_type_hint(Gdk::WINDOW_TYPE_HINT_DIALOG);
        if (eltern) {
            set_transient_for(*eltern);
            set_modal(true);
            set_position(Gtk::WIN_POS_CENTER_ON_PARENT);
        }
        char *t = g_dir_make_tmp("hirch-rueckmeldung-XXXXXX", nullptr);
        ordner = t ? t : Glib::get_tmp_dir();
        g_free(t);
        foto = Glib::build_filename(ordner, "bildschirmfoto.png");
        if (!foto_machen(eltern, foto)) foto.clear();
        aufbauen();
        info_holen();
        signal_key_press_event().connect([this](GdkEventKey *ev) {
            if (ev->keyval == GDK_KEY_Escape) { hide(); return true; }
            return false;
        }, false);
        signal_hide().connect([this] {
            if (hide_ohne_schliessen) return;        // nur kurz weg für «Neu aufnehmen»
            Glib::signal_idle().connect_once([this] { delete this; });
        });
        show_all();
        if (!fehler_gezeigt) fehler_box->hide();
        aktualisieren();
    }

    ~Fenster() override
    {
        *lebt = false;
        if (abbruch) { g_cancellable_cancel(abbruch); g_object_unref(abbruch); }
        if (proc) { g_subprocess_force_exit(proc); g_object_unref(proc); }
        for (auto const &p : {foto, zeichnung}) if (!p.empty()) ::g_remove(p.c_str());
        if (ordner.find("hirch-rueckmeldung-") != std::string::npos) g_rmdir(ordner.c_str());
        if (offen == this) offen = nullptr;
    }

    // ---- Aufbau ------------------------------------------------------------------------------------------
    void aufbauen()
    {
        auto aussen = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 0);
        add(*aussen);
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
        box->set_margin_start(18);
        box->set_margin_end(18);
        box->set_margin_top(14);
        box->set_margin_bottom(10);
        aussen->pack_start(*box, Gtk::PACK_SHRINK);

        // Fehlermeldung (nur nach einem misslungenen Senden)
        fehler_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
        fehler_box->get_style_context()->add_class("hirch-rm-fehler");
        fehler_titel = text("", nullptr, true);
        fehler_text = text("");
        fehler_box->add(*fehler_titel);
        fehler_box->add(*fehler_text);
        box->add(*fehler_box);

        // Art
        box->add(*ueberschrift(TR("Worum geht es?")));
        auto arten = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
        art_fehler = Gtk::make_managed<Gtk::RadioButton>(TR("Fehler"));
        art_wunsch = Gtk::make_managed<Gtk::RadioButton>(TR("Wunsch"));
        art_frage = Gtk::make_managed<Gtk::RadioButton>(TR("Frage"));
        art_wunsch->join_group(*art_fehler);
        art_frage->join_group(*art_fehler);
        for (auto r : {art_fehler, art_wunsch, art_frage}) {
            r->set_mode(false);                      // sieht aus wie ein Knopf
            r->get_style_context()->add_class("hirch-rm-art");
            arten->add(*r);
        }
        art_fehler->set_active(true);
        box->add(*arten);

        // Text
        auto t = ueberschrift(TR("Was ist passiert – und was hast du erwartet?"));
        t->set_margin_top(6);
        box->add(*t);
        auto overlay = Gtk::make_managed<Gtk::Overlay>();
        auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
        scroll->set_shadow_type(Gtk::SHADOW_IN);
        scroll->set_size_request(-1, 96);
        eingabe = Gtk::make_managed<Gtk::TextView>();
        eingabe->set_wrap_mode(Gtk::WRAP_WORD_CHAR);
        eingabe->set_left_margin(8);
        eingabe->set_right_margin(8);
        eingabe->set_top_margin(6);
        eingabe->set_bottom_margin(6);
        eingabe->set_accepts_tab(false);
        scroll->add(*eingabe);
        overlay->add(*scroll);
        platzhalter = text(TR("Zum Beispiel: Ich habe eine Tasche mit 3 mm Zustellung gerechnet, die Vorschau zeigt "
                              "aber …"), "hirch-rm-dim");
        platzhalter->set_halign(Gtk::ALIGN_FILL);
        platzhalter->set_valign(Gtk::ALIGN_START);
        platzhalter->set_margin_start(10);
        platzhalter->set_margin_end(10);
        platzhalter->set_margin_top(7);
        overlay->add_overlay(*platzhalter);
        overlay->set_overlay_pass_through(*platzhalter, true);
        box->add(*overlay);
        zaehler = text("", "hirch-rm-dim");
        zaehler->set_xalign(1);
        box->add(*zaehler);
        eingabe->get_buffer()->signal_changed().connect([this] { aktualisieren(); });

        // E-Mail
        auto em = Gtk::make_managed<Gtk::Label>();
        em->set_markup("<b>" + Glib::Markup::escape_text(TR("E-Mail für eine Antwort")) + "</b> " +
                       Glib::Markup::escape_text(TR("(freiwillig)")));
        em->set_xalign(0);
        box->add(*em);
        email = Gtk::make_managed<Gtk::Entry>();
        email->set_placeholder_text(TR("name@beispiel.ch"));
        email->set_input_purpose(Gtk::INPUT_PURPOSE_EMAIL);
        email->set_max_length(254);
        email->signal_changed().connect([this] { aktualisieren(); });
        box->add(*email);
        box->add(*text(TR("Nur für eine Antwort. Wird nicht weitergegeben und nicht veröffentlicht."), "hirch-rm-dim"));

        // Bild
        auto b = ueberschrift(TR("Bild mitschicken?"));
        b->set_margin_top(6);
        box->add(*b);
        kein_bild = Gtk::make_managed<Gtk::RadioButton>(TR("Kein Bild"));
        mit_foto = Gtk::make_managed<Gtk::RadioButton>(TR("Bildschirmfoto des Hirch-Fensters"));
        mit_eigenem = Gtk::make_managed<Gtk::RadioButton>(TR("Eigenes Bild wählen"));
        mit_foto->join_group(*kein_bild);
        mit_eigenem->join_group(*kein_bild);
        kein_bild->set_active(true);
        box->add(*kein_bild);
        box->add(*mit_foto);
        if (foto.empty()) {
            mit_foto->set_sensitive(false);
            mit_foto->set_tooltip_text(TR("Das Bildschirmfoto ist hier nicht möglich."));
        }
        vorschau = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 12);
        vorschau->set_margin_start(26);
        vorschau_bild = Gtk::make_managed<Gtk::Image>();
        vorschau_bild->get_style_context()->add_class("hirch-rm-vorschau");
        vorschau_bild->set_valign(Gtk::ALIGN_START);
        vorschau->add(*vorschau_bild);
        auto vrechts = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
        vorschau_text = text("", "hirch-rm-dim");
        vorschau_text->set_max_width_chars(32);
        vrechts->add(*vorschau_text);
        auto vknoepfe = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 4);
        auto gross = Gtk::make_managed<Gtk::Button>(TR("Gross ansehen"));
        gross->set_relief(Gtk::RELIEF_NONE);
        gross->signal_clicked().connect([this] { gross_zeigen(); });
        neu_aufnehmen = Gtk::make_managed<Gtk::Button>(TR("Neu aufnehmen"));
        neu_aufnehmen->set_relief(Gtk::RELIEF_NONE);
        neu_aufnehmen->signal_clicked().connect([this] { neu_fotografieren(); });
        vknoepfe->add(*gross);
        vknoepfe->add(*neu_aufnehmen);
        vrechts->add(*vknoepfe);
        vorschau->add(*vrechts);
        auto eigen_zeile = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        eigen_zeile->add(*mit_eigenem);
        datei_waehlen = Gtk::make_managed<Gtk::Button>(TR("Datei wählen …"));
        datei_waehlen->signal_clicked().connect([this] { datei_holen(); });
        eigen_zeile->add(*datei_waehlen);
        // Reihenfolge wie im Entwurf: Vorschau direkt unter der gewählten Zeile
        box->add(*eigen_zeile);
        box->add(*vorschau);
        for (auto r : {kein_bild, mit_foto, mit_eigenem})
            r->signal_toggled().connect([this, r] {
                if (!r->get_active()) return;
                if (r == mit_eigenem && eigenes.empty()) datei_holen();
                aktualisieren();
            });

        // Zeichnung
        auto z = ueberschrift(TR("Zeichnung"));
        z->set_margin_top(6);
        box->add(*z);
        auto zzeile = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
        mit_zeichnung = Gtk::make_managed<Gtk::CheckButton>(TR("Offene Zeichnung mitschicken"));
        zzeile->add(*mit_zeichnung);
        zeichnung_info = text("", "hirch-rm-dim");
        zzeile->add(*zeichnung_info);
        box->add(*zzeile);
        if (k.zeichnung_name.empty() || !k.zeichnung_schreiben) {
            mit_zeichnung->set_sensitive(false);
            zeichnung_info->set_text(TR("(keine Zeichnung offen)"));
        } else {
            zeichnung_info->set_text(TRF("(«{}»)", {k.zeichnung_name}));
        }
        warnung = text(TR("Nur mitschicken, wenn die Zeichnung dir gehört. Sie wird nur zum Nachstellen des Fehlers "
                          "verwendet, nie veröffentlicht und danach gelöscht."));
        warnung->get_style_context()->add_class("hirch-rm-warnung");
        warnung->set_margin_start(26);
        warnung->set_no_show_all(true);
        box->add(*warnung);

        // Website (Peter 07.11.2026, Entwurf Variante 1): nur mit Einwilligung, nie vorangehakt
        auto ws = ueberschrift(TR("Website"));
        ws->set_margin_top(6);
        box->add(*ws);
        website = Gtk::make_managed<Gtk::CheckButton>(TR("Darf ohne Namen auf der Website erscheinen"));
        box->add(*website);
        auto ws_hinweis = text(TR("Peter entscheidet, ob und gekürzt wie die Meldung erscheint, und kann sie jederzeit "
                                  "wieder entfernen."), "hirch-rm-dim");
        ws_hinweis->set_margin_start(26);
        box->add(*ws_hinweis);
        mit_zeichnung->signal_toggled().connect([this] { zeichnung_umschalten(); });

        // Immer dabei
        immer = text("", "hirch-rm-immer", true);
        immer->set_margin_top(8);
        box->add(*immer);

        // Fuss
        aussen->pack_start(*Gtk::make_managed<Gtk::Separator>(), Gtk::PACK_SHRINK);
        auto fuss = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        fuss->set_margin_start(14);
        fuss->set_margin_end(18);
        fuss->set_margin_top(10);
        fuss->set_margin_bottom(12);
        datenschutz = Gtk::make_managed<Gtk::LinkButton>("", TR("Datenschutz"));
        datenschutz->set_no_show_all(true);
        fuss->pack_start(*datenschutz, Gtk::PACK_SHRINK);
        spinner = Gtk::make_managed<Gtk::Spinner>();
        spinner->set_no_show_all(true);
        abbrechen = Gtk::make_managed<Gtk::Button>(TR("Abbrechen"));
        senden = Gtk::make_managed<Gtk::Button>(TR("Senden"));
        senden->get_style_context()->add_class("suggested-action");
        abbrechen->signal_clicked().connect([this] { hide(); });
        senden->signal_clicked().connect([this] { los(); });
        fuss->pack_end(*senden, Gtk::PACK_SHRINK);
        fuss->pack_end(*abbrechen, Gtk::PACK_SHRINK);
        fuss->pack_end(*spinner, Gtk::PACK_SHRINK);
        aussen->pack_start(*fuss, Gtk::PACK_SHRINK);
    }

    // ---- Zustand -----------------------------------------------------------------------------------------
    int zeichen() const { return eingabe->get_buffer()->get_char_count(); }

    bool text_leer() const
    {
        auto s = eingabe->get_buffer()->get_text();
        return s.find_first_not_of(" \t\r\n") == Glib::ustring::npos;
    }

    bool email_ok() const
    {
        auto e = email->get_text().raw();
        if (e.empty()) return true;
        auto at = e.find('@');
        return at != std::string::npos && at > 0 && e.find('.', at) != std::string::npos &&
               e.find(' ') == std::string::npos;
    }

    std::string bild() const
    {
        if (mit_foto->get_active()) return foto;
        if (mit_eigenem->get_active()) return eigenes;
        return {};
    }

    void aktualisieren()
    {
        int n = zeichen();
        platzhalter->set_visible(n == 0);
        zaehler->set_text(TRF("{} / {} Zeichen", {std::to_string(n), std::to_string(MAX_TEXT)}));
        auto zc = zaehler->get_style_context();
        if (n > MAX_TEXT) zc->add_class("hirch-rm-zuviel"); else zc->remove_class("hirch-rm-zuviel");
        auto ec = email->get_style_context();
        if (email_ok()) ec->remove_class("error"); else ec->add_class("error");

        auto b = bild();
        if (!b.empty()) {
            try {
                vorschau_bild->set(Gdk::Pixbuf::create_from_file(b, 132, 82, true));
            } catch (...) {
                vorschau_bild->clear();
            }
            vorschau_text->set_text(mit_foto->get_active()
                ? TR("So sieht das Bild aus (ohne dieses Fenster). Zeigt es etwas Vertrauliches? «Kein Bild» wählen.")
                : TRF("Eigenes Bild «{}», {}", {Glib::path_get_basename(b), groesse(dateigroesse(b))}));
            vorschau->show();
            neu_aufnehmen->set_visible(mit_foto->get_active());
        } else {
            vorschau->hide();
        }
        // Vorschau direkt unter der gewählten Zeile
        if (auto box = dynamic_cast<Gtk::Box *>(vorschau->get_parent())) {
            auto zeile = mit_eigenem->get_parent();
            auto kinder = box->get_children();
            int pos_foto = 0, pos_eigen = 0, i = 0;
            for (auto w : kinder) {
                if (w == mit_foto) pos_foto = i;
                if (w == zeile) pos_eigen = i;
                i++;
            }
            box->reorder_child(*vorschau, mit_eigenem->get_active() ? pos_eigen + 1 : pos_foto + 1);
        }

        std::string sprache = k.sprache == "en" ? "English" : "Deutsch";
        immer->set_markup(Glib::Markup::escape_text(TR("Wird immer mitgeschickt:")) + " <b>Hirch CAM " +
                          Glib::Markup::escape_text(k.version.empty() ? "?" : k.version) + "</b> · <b>" +
                          Glib::Markup::escape_text(system) + "</b> · " +
                          Glib::Markup::escape_text(TR("Sprache")) + " <b>" + sprache + "</b>");
        bool laeuft = proc != nullptr;
        senden->set_sensitive(!laeuft && !text_leer() && n <= MAX_TEXT && email_ok() && !adresse.empty());
        for (Gtk::Widget *w : std::initializer_list<Gtk::Widget *>{art_fehler, art_wunsch, art_frage, eingabe, email,
                                                                    kein_bild, mit_eigenem, datei_waehlen,
                                                                    website})
            w->set_sensitive(!laeuft);
        mit_foto->set_sensitive(!laeuft && !foto.empty());
        mit_zeichnung->set_sensitive(!laeuft && !k.zeichnung_name.empty() && k.zeichnung_schreiben);
    }

    /// Fehlermeldung wegnehmen – ohne eingerichtete Adresse bleibt deren Hinweis stehen
    void fehler_weg()
    {
        if (adresse.empty() && adresse_bekannt)
            fehler_zeigen(TR("Für Rückmeldungen ist noch keine Adresse eingerichtet."),
                          TR("Senden geht erst mit einer neueren Version von Hirch CAM."));
        else
            fehler_box->hide();
    }

    void fehler_zeigen(std::string const &titel, std::string const &rest)
    {
        fehler_titel->set_markup("<b>" + Glib::Markup::escape_text(titel) + "</b>");
        fehler_text->set_text(rest);
        fehler_box->show();
        fehler_titel->show();
        fehler_text->set_visible(!rest.empty());
        fehler_gezeigt = true;
    }

    // ---- Bild --------------------------------------------------------------------------------------------
    void datei_holen()
    {
        auto w = Gtk::FileChooserNative::create(TR("Bild wählen"), *this, Gtk::FILE_CHOOSER_ACTION_OPEN,
                                                TR("Wählen"), TR("Abbrechen"));
        auto f = Gtk::FileFilter::create();
        f->set_name(TR("Bilder (PNG, JPG)"));
        for (auto m : {"*.png", "*.PNG", "*.jpg", "*.JPG", "*.jpeg", "*.JPEG"}) f->add_pattern(m);
        w->add_filter(f);
        int antwort = w->run();
        if (antwort == Gtk::RESPONSE_ACCEPT) {
            auto p = w->get_filename();
            auto g = dateigroesse(p);
            if (!ist_png_jpg(p)) {
                fehler_zeigen(TR("Dieses Bild geht nicht."), TR("Bitte ein PNG- oder JPG-Bild wählen."));
            } else if (g > MAX_BILD) {
                fehler_zeigen(TR("Das Bild ist zu gross."),
                              TRF("Höchstens 5 MB – dieses hat {}.", {groesse(g)}));
            } else {
                eigenes = p;
                fehler_weg();
                mit_eigenem->set_active(true);
            }
        }
        if (eigenes.empty() && mit_eigenem->get_active()) kein_bild->set_active(true);
        aktualisieren();
    }

    void neu_fotografieren()
    {
        hide_ohne_schliessen = true;
        hide();
        Glib::signal_timeout().connect_once([this, lebt = lebt] {
            if (!*lebt) return;
            if (!foto_machen(eltern, foto)) foto.clear();
            hide_ohne_schliessen = false;
            show();
            aktualisieren();
        }, 350);
    }
    void gross_zeigen()
    {
        auto b = bild();
        if (b.empty()) return;
        auto w = new Gtk::Window();
        w->set_title(TR("Bild zur Rückmeldung"));
        w->set_transient_for(*this);
        w->set_modal(true);
        w->set_position(Gtk::WIN_POS_CENTER_ON_PARENT);
        try {
            w->add(*Gtk::make_managed<Gtk::Image>(Gdk::Pixbuf::create_from_file(b, 1100, 750, true)));
        } catch (...) {
            delete w;
            return;
        }
        w->signal_hide().connect([w] { Glib::signal_idle().connect_once([w] { delete w; }); });
        w->show_all();
    }

    // ---- Zeichnung ---------------------------------------------------------------------------------------
    void zeichnung_umschalten()
    {
        bool an = mit_zeichnung->get_active();
        warnung->set_visible(an);
        if (an) {
            zeichnung = Glib::build_filename(ordner, "zeichnung.hir");
            if (!k.zeichnung_schreiben || !k.zeichnung_schreiben(zeichnung)) {
                zeichnung.clear();
                mit_zeichnung->set_active(false);
                fehler_zeigen(TR("Die Zeichnung liess sich nicht anhängen."), "");
                return;
            }
            auto g = dateigroesse(zeichnung);
            zeichnung_info->set_text(TRF("(«{}», {})", {k.zeichnung_name, groesse(g)}));
            if (g > MAX_ZEICHNUNG) {
                ::g_remove(zeichnung.c_str());
                zeichnung.clear();
                mit_zeichnung->set_active(false);
                fehler_zeigen(TR("Die Zeichnung ist zu gross."), TR("Höchstens 20 MB."));
            }
        } else if (!zeichnung.empty()) {
            ::g_remove(zeichnung.c_str());
            zeichnung.clear();
            zeichnung_info->set_text(TRF("(«{}»)", {k.zeichnung_name}));
        }
        aktualisieren();
    }

    // ---- Python ------------------------------------------------------------------------------------------
    GSubprocess *python(std::vector<std::string> args, GError **err)
    {
        auto l = g_subprocess_launcher_new(GSubprocessFlags(G_SUBPROCESS_FLAGS_STDIN_PIPE |
                                                            G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                                            G_SUBPROCESS_FLAGS_STDERR_PIPE));
        if (!k.pythonpath.empty()) g_subprocess_launcher_setenv(l, "PYTHONPATH", k.pythonpath.c_str(), TRUE);
        g_subprocess_launcher_setenv(l, "PYTHONIOENCODING", "utf-8", TRUE);
        g_subprocess_launcher_setenv(l, "PYTHONUTF8", "1", TRUE);
        g_subprocess_launcher_unsetenv(l, "PYTHONHOME");
        args.insert(args.begin(), {k.python, "-m", "hirch_cam.rueckmeldung"});
        std::vector<char const *> argv;
        for (auto const &a : args) argv.push_back(a.c_str());
        argv.push_back(nullptr);
        auto p = g_subprocess_launcher_spawnv(l, argv.data(), err);
        g_object_unref(l);
        return p;
    }

    struct Rueckruf {
        std::shared_ptr<bool> lebt;
        std::function<void(bool, std::string const &)> fertig;
    };

    static void fertig_cb(GObject *quelle, GAsyncResult *res, gpointer data)
    {
        std::unique_ptr<Rueckruf> r(static_cast<Rueckruf *>(data));
        char *aus = nullptr, *err = nullptr;
        GError *e = nullptr;
        bool ok = g_subprocess_communicate_utf8_finish(G_SUBPROCESS(quelle), res, &aus, &err, &e);
        std::string s = aus ? aus : "";
        g_free(aus);
        g_free(err);
        if (e) g_error_free(e);
        if (*r->lebt) r->fertig(ok, s);
    }

    void info_holen()
    {
        GError *e = nullptr;
        auto p = python({"--info"}, &e);
        if (!p) {
            if (e) g_error_free(e);
            system = "?";
            return;
        }
        auto r = new Rueckruf{lebt, [this, p](bool ok, std::string const &aus) {
            g_object_unref(p);
            try {
                auto o = bj::parse(aus).as_object();
                system = bj::value_to<std::string>(o.at("system"));
                adresse = bj::value_to<std::string>(o.at("adresse"));
                auto ds = bj::value_to<std::string>(o.at(k.sprache == "en" ? "datenschutz_en" : "datenschutz_de"));
                if (!ds.empty()) {
                    datenschutz->set_uri(ds);
                    datenschutz->show();
                }
            } catch (...) {
                system = "?";
            }
            adresse_bekannt = true;
            if (adresse.empty()) fehler_weg();
            aktualisieren();
        }};
        g_subprocess_communicate_utf8_async(p, nullptr, nullptr, fertig_cb, r);
    }

    void los()
    {
        if (proc || !senden->get_sensitive()) return;
        fehler_weg();
        bj::object m;
        m["art"] = art_wunsch->get_active() ? "wunsch" : art_frage->get_active() ? "frage" : "fehler";
        m["text"] = eingabe->get_buffer()->get_text().raw();
        m["email"] = email->get_text().raw();
        m["bild"] = bild();
        m["zeichnung"] = mit_zeichnung->get_active() ? zeichnung : std::string();
        m["version"] = k.version;
        m["sprache"] = k.sprache;
        m["website"] = website->get_active();
        GError *e = nullptr;
        proc = python({"--json"}, &e);
        if (!proc) {
            std::string f = e ? e->message : "";
            if (e) g_error_free(e);
            fehler_zeigen(TR("Nicht gesendet – Python liess sich nicht starten."), f);
            return;
        }
        abbruch = g_cancellable_new();
        senden->set_label(TR("Wird gesendet …"));
        spinner->show();
        spinner->start();
        aktualisieren();
        auto r = new Rueckruf{lebt, [this](bool ok, std::string const &aus) { antwort(ok, aus); }};
        g_subprocess_communicate_utf8_async(proc, bj::serialize(m).c_str(), abbruch, fertig_cb, r);
    }

    void antwort(bool ok, std::string const &aus)
    {
        g_object_unref(proc);
        proc = nullptr;
        g_object_unref(abbruch);
        abbruch = nullptr;
        spinner->stop();
        spinner->hide();
        std::string nummer, grund, text_server;
        try {
            auto o = bj::parse(aus).as_object();
            if (auto n = o.if_contains("nummer")) nummer = bj::value_to<std::string>(*n);
            if (auto g = o.if_contains("fehler")) grund = bj::value_to<std::string>(*g);
            if (auto t = o.if_contains("text")) text_server = bj::value_to<std::string>(*t);
        } catch (...) {
            grund = "server";
        }
        if (ok && !nummer.empty()) {
            auto d = new Gtk::MessageDialog(*this, TR("Danke, deine Rückmeldung ist angekommen."), false,
                                            Gtk::MESSAGE_INFO, Gtk::BUTTONS_OK, true);
            d->set_secondary_text(TRF("Nummer {}. Bitte notieren, falls du später danach fragen willst.", {nummer}));
            d->signal_response().connect([this, d](int) {
                d->hide();
                Glib::signal_idle().connect_once([d] { delete d; });
                hide();
            });
            d->show();
            senden->set_label(TR("Senden"));
            senden->set_sensitive(false);
            return;
        }
        senden->set_label(TR("Nochmals senden"));
        if (grund == "keine_verbindung")
            fehler_zeigen(TR("Nicht gesendet – keine Verbindung zum Server."),
                          TR("Deine Eingaben bleiben stehen. Prüfe die Internetverbindung und drücke nochmals «Senden»."));
        else if (grund == "nicht_erreichbar")
            fehler_zeigen(TR("Nicht gesendet – der Server ist gerade nicht erreichbar."),
                          TR("Deine Eingaben bleiben stehen. Bitte später nochmals senden."));
        else if (grund == "zu_viele")
            fehler_zeigen(TR("Nicht gesendet – zu viele Meldungen in kurzer Zeit."),
                          TR("Deine Eingaben bleiben stehen. Bitte etwas später nochmals versuchen."));
        else if (grund == "zu_gross")
            fehler_zeigen(TR("Nicht gesendet – Bild oder Zeichnung ist zu gross."),
                          TR("Bild höchstens 5 MB, Zeichnung höchstens 20 MB."));
        else if (grund == "keine_adresse")
            fehler_zeigen(TR("Für Rückmeldungen ist noch keine Adresse eingerichtet."), "");
        else
            fehler_zeigen(TR("Nicht gesendet."), k.sprache == "en" ? std::string(TR("Der Server hat die Meldung "
                                                                                    "nicht angenommen."))
                                                                   : text_server);
        aktualisieren();
    }
};

} // namespace

#ifdef HIRCH_RUECKMELDUNG_VORSCHAU
/// nur für feedback/vorschau-fenster: Zustände aus dem freigegebenen Entwurf herstellen
/// 5 = ausgefüllt mit Häkchen «Website», dann senden
/// 4 = Befund windows 04.11.: ohne Adresse zu grosses, dann gültiges eigenes Bild → Adress-Hinweis bleibt
void rueckmeldung_vorschau(int zustand)
{
    if (!offen) return;
    if (zustand == 4) {
        offen->fehler_zeigen(TR("Das Bild ist zu gross."), TRF("Höchstens 5 MB – dieses hat {}.", {"7.2 MB"}));
        offen->eigenes = offen->foto;    // als «gültiges eigenes Bild»
        offen->fehler_weg();
        offen->mit_eigenem->set_active(true);
        offen->aktualisieren();
        return;
    }
    if (zustand >= 2) {
        offen->eingabe->get_buffer()->set_text("Tasche 20 × 30 mm, 6 mm tief, 3-mm-Fräser. In der Vorschau fehlt die "
                                               "letzte Zustellung, der Boden ist bei 4,5 mm.");
        offen->email->set_text("name@beispiel.ch");
        offen->mit_foto->set_active(true);
        offen->mit_zeichnung->set_active(true);
    }
    if (zustand == 5) offen->website->set_active(true);   // Einwilligung gesetzt, dann senden
    if (zustand == 3 || zustand == 5) offen->los();
    offen->aktualisieren();
}
#endif

void rueckmeldung_zeigen(Gtk::Window *eltern, RueckmeldungKontext k)
{
    if (offen) {
        offen->present();
        return;
    }
    offen = new Fenster(eltern, std::move(k));
}

} // namespace Inkscape::UI::Dialog
