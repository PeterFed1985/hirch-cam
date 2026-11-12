// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Aussehen (CSS), Beschriftungen, Platten und Seiten, Zahlenfelder, Formulare, Abschnitte Umspannen/Rund
 * Nur für hirch-*.cpp – kein öffentlicher Header. Sammelkopf: hirch-intern.h.
 */
#ifndef HIRCH_OBERFLAECHE_H
#define HIRCH_OBERFLAECHE_H

#include "ui/dialog/hirch-grund.h"
#include <gtkmm/scale.h>

namespace Inkscape::UI::Dialog {

// ---------------------------------------------------------------------------
// Aussehen
// ---------------------------------------------------------------------------
inline void css_einrichten()
{
    static bool fertig = false;
    if (fertig) return;
    fertig = true;
    auto css = Gtk::CssProvider::create();
    css->load_from_data(R"(
.hirch-panel { padding: 4px 10px 10px 10px; }
.hirch-kopf { font-size: 0.85em; font-weight: bold; letter-spacing: 1px; opacity: 0.65; }
.hirch-link { color: @theme_selected_bg_color; }
.hirch-wz-karte { padding: 8px; border-radius: 8px; border: 2px solid alpha(currentColor, 0.15);
  background-image: none; background-color: alpha(currentColor, 0.04); }
.hirch-wz-karte:hover { background-color: alpha(@theme_selected_bg_color, 0.10); }
.hirch-wz-gewaehlt { border-color: @theme_selected_bg_color; background-color: alpha(@theme_selected_bg_color, 0.16); }
.hirch-wz-gewaehlt .hirch-link { color: @theme_fg_color; }
.hirch-wz-platz { font-size: 0.8em; font-weight: bold; padding: 0 5px; border-radius: 4px;
  color: mix(@theme_selected_bg_color, @theme_fg_color, 0.5); background-color: alpha(@theme_selected_bg_color, 0.25); }
.hirch-wz-schild { font-size: 0.85em; font-weight: bold; min-width: 20px; padding: 1px 4px; border-radius: 5px;
  color: #ffffff; background-color: #3584e4; }
.hirch-wz-schild-nur { background-color: #c64600; }
.hirch-wz-schild-klein { font-size: 0.7em; min-width: 14px; padding: 0 3px; }
.hirch-wz-zeile { padding: 3px 6px; }
.hirch-wz-art { padding: 6px 4px; }
.hirch-zuweisen { min-width: 0; min-height: 0; padding: 3px 4px; border-radius: 6px;
  background-color: alpha(@theme_selected_bg_color, 0.20); color: mix(@theme_selected_bg_color, @theme_fg_color, 0.5); }
.hirch-zuweisen:hover { background-color: alpha(@theme_selected_bg_color, 0.35); }
.hirch-badge { font-size: 0.7em; font-weight: bold; color: #ffffff; background-color: @theme_selected_bg_color;
  border-radius: 7px; padding: 0 4px; min-width: 7px; }
.hirch-dim { opacity: 0.7; }
.hirch-leiste { padding: 6px 5px; border-radius: 10px; background-color: alpha(@theme_bg_color, 0.92);
  border: 1px solid alpha(currentColor, 0.18); }
.hirch-leiste button { min-width: 0; min-height: 0; padding: 0; }
.hirch-ok { color: #2e8b3e; }
.hirch-gefunden { background: alpha(@theme_selected_bg_color, 0.25); border-radius: 4px; }
.hirch-klein { font-size: 0.9em; }
.hirch-zf-zeile button { min-height: 0; padding: 2px 8px; font-size: 0.9em; }
.hirch-zf-zeile { margin-top: -4px; }
.hirch-wert { font-feature-settings: "tnum"; }
.hirch-gross { font-size: 1.25em; font-weight: bold; font-feature-settings: "tnum"; }
.hirch-titel { font-weight: bold; }
.hirch-karte { border: 1px solid alpha(currentColor, 0.18); border-radius: 6px; padding: 8px 10px; }
.hirch-liste { background: transparent; }
.hirch-liste row { border: 1px solid alpha(currentColor, 0.15); border-radius: 6px; margin: 0 0 6px 0; padding: 0; }
.hirch-liste row:selected { border-color: alpha(currentColor, 0.45); }
.hirch-aus { opacity: 0.45; }
.hirch-warn { padding: 6px 8px; border-radius: 6px; background: alpha(#e8a33a, 0.18); }
.hirch-fehler { padding: 6px 8px; border-radius: 6px; background: alpha(#d9342b, 0.20); }
.hirch-befund { padding: 0; border: none; background: none; box-shadow: none; }
.hirch-haupt { font-weight: bold; }
/* «Segoe UI Symbol» hinter monospace: unter Windows fehlen dort ◴ ◷ ⚠ ⟲ (leeres Kästchen); unter Linux ohne Wirkung */
.hirch-mono { font-family: monospace, "Segoe UI Symbol"; font-feature-settings: "tnum"; }
.hirch-gruppe { border: 1px solid alpha(currentColor, 0.16); border-radius: 8px; }
.hirch-gruppe-kopf { padding: 5px 12px; background: alpha(currentColor, 0.05); border-bottom: 1px solid alpha(currentColor, 0.12); border-radius: 8px 8px 0 0; }
.hirch-ops { background: transparent; border-radius: 0 0 8px 8px; }
.hirch-ops row { padding: 0; border-top: 1px solid alpha(currentColor, 0.10); }
.hirch-ops row:first-child { border-top: none; }
.hirch-luecke { background-color: alpha(@theme_selected_bg_color, 0.10); }
.hirch-veraltet { opacity: 0.45; }
.hirch-chip { font-family: monospace, "Segoe UI Symbol"; font-weight: bold; padding: 0 6px; border-radius: 4px; background: alpha(currentColor, 0.12); }
.hirch-wenden { border: 1px dashed #9141ac; border-radius: 8px; padding: 8px 10px; }
.hirch-umspannen { border: 1.5px dashed #e5a50a; border-radius: 8px; padding: 8px 10px; margin: 6px 0;
                   background: alpha(#e5a50a, 0.10); }
.hirch-schritt { padding: 8px 10px; border-radius: 8px; border: 1px solid alpha(currentColor, 0.16); background: alpha(currentColor, 0.03); }
.hirch-schritt-an { border-color: @theme_selected_bg_color; background: alpha(@theme_selected_bg_color, 0.20); }
.hirch-schritt-fehler { border-color: #d9342b; background: alpha(#d9342b, 0.15); }
.hirch-schritt-fertig { border-color: #26a269; background: alpha(#26a269, 0.15); }
.hirch-neu { padding: 8px 10px; border-radius: 8px; border: 1px dashed alpha(currentColor, 0.35); background: none; box-shadow: none; }
.hirch-karte2 { border: 1px solid alpha(currentColor, 0.15); border-radius: 10px; padding: 12px 16px; background: alpha(currentColor, 0.03); }
.hirch-sprung { border-radius: 14px; padding: 3px 12px; min-height: 0; background: alpha(currentColor, 0.07); border: none; box-shadow: none; }
.hirch-sprung:hover { background: alpha(currentColor, 0.14); }
.hirch-geaendert { border-radius: 6px; padding: 4px 8px; background: alpha(#e66100, 0.13); }
.hz-blatt { background: #ffffff; color: #000000; padding: 26px 24px; }
.hz-blatt.hz-dunkel { background: #121212; color: #f6f5f4; }
.hz-dim { opacity: 0.8; }
.hz-karte { border: 2px solid currentColor; border-radius: 12px; padding: 12px 14px; }
.hz-start { border: 3px solid currentColor; border-radius: 12px; padding: 12px 14px; }
.hz-halt { border: 3px solid #e66100; border-radius: 12px; padding: 12px 14px; background: #fff4ea; }
.hz-dunkel .hz-halt { background: #2a1a0a; }
.hz-halt-titel { color: #a33c00; }
.hz-dunkel .hz-halt-titel { color: #ffa348; }
.hz-warn { border-radius: 12px; padding: 10px 14px; background: #fff1b8; color: #000000; }
.hz-dunkel .hz-warn { background: #3a2b00; color: #f6f5f4; }
.hirch-seitentitel { font-size: 1.45em; font-weight: bold; }
.hirch-chip-form { background: #26a269; color: transparent; }
.hirch-warntext { color: mix(#b07800, currentColor, 0.25); }
.hirch-rechnet { background: alpha(@theme_selected_bg_color, 0.12); border: 1px solid alpha(@theme_selected_bg_color, 0.30); border-radius: 8px; }
.hirch-rechnet .hirch-rechen-titel { color: mix(@theme_selected_bg_color, currentColor, 0.25); font-weight: bold; }
.hirch-fertig { background: alpha(#26a269, 0.14); border: 1px solid alpha(#26a269, 0.30); border-radius: 8px; }
.hirch-abgebrochen { background: alpha(#e5a50a, 0.20); border: 1px solid alpha(#e5a50a, 0.45); border-radius: 8px; }
.hirch-wartet { background: alpha(currentColor, 0.06); border: 1px solid alpha(currentColor, 0.15); border-radius: 8px; }
.hirch-rechenknopf { padding: 2px 10px; border-radius: 16px; font-weight: bold; background: alpha(@theme_bg_color, 0.96); border: 1px solid alpha(currentColor, 0.25); }
.hirch-marke-ok { font-size: 0.85em; padding: 1px 8px; border-radius: 999px; background: alpha(#26a269, 0.16); color: mix(#26a269, currentColor, 0.35); }
.hirch-marke-warn { font-size: 0.85em; padding: 1px 8px; border-radius: 999px; background: alpha(#e5a50a, 0.22); color: mix(#b07800, currentColor, 0.30); }
.hirch-ergebnis { border-radius: 8px; padding: 7px 9px; background: alpha(currentColor, 0.05); }
.hirch-schild { font-size: 0.75em; font-weight: bold; padding: 1px 6px; border-radius: 4px; background: #ffe6b3; color: #6b4a00; }
.hirch-rechen-puls { min-height: 3px; }
.hirch-3d-dunkel { background: alpha(#2b2a33, 0.45); }
.hirch-3d-hinweis { color: #f6f5f4; font-size: 1.15em; background: alpha(#14141a, 0.75); padding: 12px 18px; border-radius: 10px; }
.hirch-3d-schalter { background: alpha(#241f31, 0.84); border-radius: 8px; padding: 8px 12px; }
.hirch-3d-schalter > label, .hirch-3d-schalter checkbutton label, .hirch-3d-schalter radiobutton label { color: #f6f5f4; }
.hirch-3d-kopf { color: #c0bfbc; font-size: 0.8em; letter-spacing: 1px; }
.hirch-3d-leiste { background: alpha(#1e2028, 0.94); border-top: 1px solid #454a55; padding: 10px 14px 12px; }
.hirch-3d-leiste label { color: #f6f5f4; }
.hirch-3d-leiste label.hirch-3d-dim { color: #c0bfbc; font-size: 0.9em; }
.hirch-3d-leiste label.hirch-3d-uhr { font-family: monospace, "Segoe UI Symbol"; font-size: 1.05em; }
.hirch-3d-leiste button, .hirch-3d-auf { color: #f6f5f4; background: #3a3f4b; box-shadow: none; border: 1px solid #555b68; min-height: 0; padding: 3px 10px; }
.hirch-3d-leiste button:checked, .hirch-3d-leiste button.suggested-action { background: #3584e4; border-color: #3584e4; color: #ffffff; }
.hirch-3d-auf { border-radius: 6px; }
.hirch-3d-pille { padding: 8px 10px 8px 16px; border-radius: 22px; background: alpha(#14141a, 0.85); color: #ffffff; }
.hirch-3d-pille label { color: #ffffff; }
.hirch-3d-pille button { color: #ffffff; background: none; box-shadow: none; border: 1px solid alpha(#ffffff, 0.35); border-radius: 13px; min-height: 0; padding: 2px 10px; }
.hirch-3d-pille button.suggested-action { background: #3584e4; border-color: #3584e4; font-weight: bold; }
.hirch-rechen-puls trough, .hirch-rechen-puls progress { min-height: 3px; }
.hirch-rechenkarte { padding: 4px 6px 4px 10px; border-radius: 16px; background: alpha(@theme_bg_color, 0.96); border: 1px solid alpha(@theme_selected_bg_color, 0.55); }
.hirch-rechenkarte label { font-family: monospace, "Segoe UI Symbol"; font-weight: bold; color: mix(@theme_selected_bg_color, currentColor, 0.25); }
.hirch-rechenkarte button { min-width: 0; min-height: 0; padding: 0 6px; border-radius: 11px; }
.hirch-benutzt { color: mix(#1c71d8, currentColor, 0.2); }
.hirch-wz-benutzt { border-left: 4px solid #3584e4; }
.hirch-wz-nurhier { border-left: 4px solid #e5a50a; background: alpha(#e8a33a, 0.14); }
row:selected .hirch-benutzt, row:selected .hirch-warntext { color: inherit; }
row.hirch-wz-nurhier:selected { background-color: @theme_selected_bg_color; }
.hirch-loeschen { color: #c01c28; }
)");
    Gtk::StyleContext::add_provider_for_screen(Gdk::Screen::get_default(), css,
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

inline void klasse(Gtk::Widget &w, char const *k)
{
    w.get_style_context()->add_class(k);
}

inline void klasse(Gtk::Widget &w, char const *k, bool an)
{
    if (an) w.get_style_context()->add_class(k);
    else w.get_style_context()->remove_class(k);
}

/// Rechnung der Schnittdaten in Klartext (werkzeugdb «rechnung_text», deutsch): Einheiten und Wörter übersetzen
inline std::string rechnung_uebersetzen(std::string t)
{
    for (char const *w : {"U/min", "Maschine höchstens"}) {
        std::string de = w, en = TR(w);
        if (en == de) continue;
        for (auto p = t.find(de); p != std::string::npos; p = t.find(de, p + en.size())) t.replace(p, de.size(), en);
    }
    return t;
}

inline Gtk::Label *label(std::string const &text, char const *k = nullptr, float xalign = 0.0f)
{
    auto l = Gtk::make_managed<Gtk::Label>(text);
    l->set_xalign(xalign);
    if (k) klasse(*l, k);
    return l;
}

/// Seiten der Zeichnung ohne die der Spannvorrichtung (in ihrer Reihenfolge)
inline std::vector<SPPage *> platten_seiten(SPDocument *d)
{
    std::vector<SPPage *> aus;
    if (!d) return aus;
    for (auto pg : d->getPageManager().getPages())
        if (pg && !pg->getRepr()->attribute("hirch:vorrichtung")) aus.push_back(pg);
    return aus;
}

/// Gewählte Platte je Zeichnung: das Panel «Fräsen» trägt sie ein (platte, Seiten je Platte: 1 oder
/// 2 bei zweiseitig, 0 = unbekannt); die 3D-Teile lesen sie, damit Modell und Ergebnis auf der
/// gewählten Platte liegen (3D, 26.10.)
struct PlattenWahl {
    int platte = 1;
    int je = 0;
};

inline std::map<SPDocument const *, PlattenWahl> &platten_wahl()
{
    static std::map<SPDocument const *, PlattenWahl> m;
    return m;
}

inline void platte_merken(SPDocument const *d, int platte, int je)
{
    if (d) platten_wahl()[d] = {platte, je};
}

/// Seiten der Platte (1. Seite, 2. Seite oder nullptr): die, auf der «auf» liegt, sonst die gewählte
inline std::pair<SPPage *, SPPage *> platte_von(SPDocument *d, SPItem *auf = nullptr)
{
    auto ps = platten_seiten(d);
    if (ps.empty()) return {nullptr, nullptr};
    PlattenWahl w;
    if (auto it = platten_wahl().find(d); it != platten_wahl().end()) w = it->second;
    int je = w.je > 0 ? w.je : 1;
    int i = (std::max(1, w.platte) - 1) * je;
    if (auf) {
        if (auto bb = auf->documentVisualBounds()) {
            for (std::size_t k = 0; k < ps.size(); ++k)
                if (ps[k]->getDocumentRect().contains(bb->midpoint())) {
                    i = int(k) / je * je;
                    break;
                }
        }
    }
    if (i < 0 || i >= (int)ps.size()) i = 0;
    SPPage *zweite = nullptr;
    if (w.je == 0) zweite = i == 0 && ps.size() > 1 ? ps[1] : nullptr;   // unbekannt: wie früher
    else if (je == 2 && i + 1 < (int)ps.size()) zweite = ps[i + 1];
    return {ps[i], zweite};
}

inline SPPage *vorrichtung_seite(SPDocument *d)
{
    if (!d) return nullptr;
    for (auto pg : d->getPageManager().getPages())
        if (pg && pg->getRepr()->attribute("hirch:vorrichtung")) return pg;
    return nullptr;
}

/// Kärtchen mit Name und Erklärung beim Darüberfahren (die Werkzeugleiste liegt über der
/// Zeichenfläche – dort erscheinen GTK-Tooltips nicht)
void hover_karte(Gtk::Widget &w, std::string const &markup);

/// Farbfeld der schwebenden Werkzeugleiste (unten definiert)
Gtk::Widget *farbfeld_leiste(std::vector<std::string> const &farben, std::string const &leer_hex,
                                    bool schraffiert = false);

inline bool hex_rgb(std::string const &hex, double &r, double &g, double &b)
{
    unsigned rr, gg, bb;
    if (hex.size() == 7 && hex[0] == '#' && std::sscanf(hex.c_str() + 1, "%02x%02x%02x", &rr, &gg, &bb) == 3) {
        r = rr / 255.0; g = gg / 255.0; b = bb / 255.0;
        return true;
    }
    return false;
}

inline Gtk::DrawingArea *farbfeld(Art const *a, int groesse = 14, std::string const &hex = {})
{
    auto d = Gtk::make_managed<Gtk::DrawingArea>();
    d->set_size_request(groesse, groesse);
    d->set_valign(Gtk::ALIGN_CENTER);
    double r = a ? a->r : 0.5, g = a ? a->g : 0.5, b = a ? a->b : 0.5;
    if (!hex.empty()) hex_rgb(hex, r, g, b);
    d->signal_draw().connect([d, r, g, b](Cairo::RefPtr<Cairo::Context> const &cr) {
        double w = d->get_allocated_width(), h = d->get_allocated_height();
        double s = std::min(w, h), x = (w - s) / 2, y = (h - s) / 2, rad = s * 0.25;
        cr->begin_new_sub_path();
        cr->arc(x + s - rad, y + rad, rad, -M_PI / 2, 0);
        cr->arc(x + s - rad, y + s - rad, rad, 0, M_PI / 2);
        cr->arc(x + rad, y + s - rad, rad, M_PI / 2, M_PI);
        cr->arc(x + rad, y + rad, rad, M_PI, 3 * M_PI / 2);
        cr->close_path();
        cr->set_source_rgb(r, g, b);
        cr->fill();
        return true;
    });
    return d;
}

/// Zahlenfeld nimmt Punkt und Komma als Dezimalzeichen (ein Punkt ist nie ein
/// Tausender-Trennzeichen): getippter Punkt wird zum Dezimalzeichen der Sprache
inline void punkt_und_komma(Gtk::SpinButton *s)
{
    // Dezimalzeichen der Hirch-Sprache (deutsch Komma, englisch Punkt) – nicht das des Gebietsschemas:
    // de_CH hat einen Punkt, darum standen unter Windows «100.0» neben «0,2 mm» (Windows-Sitzung 28.10.)
    if (hirch_englisch()) return;
    std::string zeichen(",");
    s->signal_insert_text().connect([s, zeichen](Glib::ustring const &text, int *pos) {
        if (text.find('.') == Glib::ustring::npos) return;
        std::string neu = text.raw();
        for (std::size_t p = neu.find('.'); p != std::string::npos; p = neu.find('.', p + zeichen.size()))
            neu.replace(p, 1, zeichen);
        g_signal_stop_emission_by_name(s->gobj(), "insert-text");
        s->insert_text(neu, -1, *pos);
    }, false);
}

/// Mausrad über Zahlenfeldern und Auswahllisten: rollt die Seite statt den Wert zu ändern
/// (Peter, 20.10.: beim Rollen durch den Dialog verstellten sich Werte). Erst nach einem Klick
/// ins Feld ändert das Rad den Wert.
inline void rad_schuetzen(Gtk::Widget &w)
{
    if (dynamic_cast<Gtk::SpinButton *>(&w) || dynamic_cast<Gtk::ComboBox *>(&w) || dynamic_cast<Gtk::Range *>(&w)) {
        auto *p = &w;
        w.add_events(Gdk::SCROLL_MASK);
        w.signal_scroll_event().connect([p](GdkEventScroll *) {
            if (p->has_focus()) return false;
            if (auto sp = dynamic_cast<Gtk::SpinButton *>(p); sp && sp->has_focus()) return false;
            g_signal_stop_emission_by_name(p->gobj(), "scroll-event");
            return false;                       // weiter an die Eltern: die Seite rollt
        }, false);
        return;
    }
    if (auto c = dynamic_cast<Gtk::Container *>(&w)) {
        for (auto k : c->get_children()) rad_schuetzen(*k);
    }
}

/// Für alle Zahlenfelder des Programms (einmal beim Start): das Mausrad ändert den Wert nur, wenn das Feld den
/// Fokus hat – sonst rollt die Seite (Peter, 20.10.: beim Rollen durch Einstellungen verstellte sich z. B. der
/// Verfahrweg Z). Auswahllisten schützt rad_schuetzen() je Dialog.
inline void rad_global()
{
    static bool gesetzt = false;
    if (gesetzt) return;
    gesetzt = true;
    // B-22 (Peter 03.11., «sperrt»): das Mausrad ändert nie einen Wert – über Zahlenfeldern und Auswahllisten
    // (auch Inkscapes eigenen, auch mit Fokus) rollt es die Seite, die sie enthält; ohne rollbare Seite nichts.
    // Eine Stelle für alle Fenster, auch die 3D-Dialoge.
    gdk_event_handler_set(
        [](GdkEvent *e, gpointer) {
            if (e->type == GDK_SCROLL) {
                GtkWidget *w = gtk_get_event_widget(e);
                bool feld = false;
                for (GtkWidget *x = w; x && !feld; x = gtk_widget_get_parent(x)) {
                    if (GTK_IS_SPIN_BUTTON(x) || GTK_IS_COMBO_BOX(x)) feld = true;
                    if (GTK_IS_SCROLLED_WINDOW(x) || GTK_IS_WINDOW(x)) break;
                }
                if (feld) {
                    for (GtkWidget *x = w; x; x = gtk_widget_get_parent(x)) {
                        if (GTK_IS_SCROLLED_WINDOW(x)) {
                            gtk_widget_event(x, e);
                            break;
                        }
                    }
                    return;
                }
            }
            gtk_main_do_event(e);
        },
        nullptr, nullptr);
    static gboolean (*alt)(GtkWidget *, GdkEventScroll *) = nullptr;
    auto k = GTK_WIDGET_CLASS(g_type_class_ref(GTK_TYPE_SPIN_BUTTON));
    alt = k->scroll_event;
    k->scroll_event = [](GtkWidget *w, GdkEventScroll *e) -> gboolean {
        if (!gtk_widget_has_focus(w)) return FALSE;     // an die Eltern: die Seite rollt
        return alt ? alt(w, e) : FALSE;
    };
    // Alle Zahlenfelder zeigen Zahlen wie die Texte (Windows-Sitzung 28.10.): Dezimalzeichen der Hirch-Sprache
    // statt des Gebietsschemas, ganze Zahlen ab 10 000 mit Tausender-Abstand («14 300 U/min»). Felder mit eigener
    // Anzeige (verbundenes «output», das TRUE liefert) bleiben, wie sie sind.
    auto sk = GTK_SPIN_BUTTON_CLASS(g_type_class_peek(GTK_TYPE_SPIN_BUTTON));
    sk->output = [](GtkSpinButton *sb) -> gint {
        double v = gtk_adjustment_get_value(gtk_spin_button_get_adjustment(sb));
        int stellen = (int)gtk_spin_button_get_digits(sb);
        char b[64];
        std::snprintf(b, sizeof b, "%.*f", stellen, v);
        std::string t = b;
        char const komma = hirch_englisch() ? '.' : ',';
        for (auto &c : t)
            if (c == '.' || c == ',') c = komma;
        if (stellen == 0 && std::abs(v) >= 10000) {
            std::size_t start = (t[0] == '-') ? 1 : 0;
            for (int i = (int)t.size() - 3; i > (int)start; i -= 3) t.insert(i, " ");
        }
        // «numeric» lässt nur das Dezimalzeichen des Gebietsschemas zu (de_CH: Punkt) – ein Komma würde beim
        // Setzen verschluckt («1,5» → «15», das Feld wüchse endlos). Die Eingabe prüft input() unten.
        if (gtk_spin_button_get_numeric(sb)) gtk_spin_button_set_numeric(sb, FALSE);
        if (t != gtk_entry_get_text(GTK_ENTRY(sb))) gtk_entry_set_text(GTK_ENTRY(sb), t.c_str());
        return TRUE;
    };
    sk->input = [](GtkSpinButton *sb, gdouble *neu) -> gint {
        std::string t = gtk_entry_get_text(GTK_ENTRY(sb)), z;
        for (std::size_t i = 0; i < t.size(); i++) {
            unsigned char c = t[i];
            if (c == ' ' || c == '\'') continue;
            if (c == 0xE2 && i + 2 < t.size() && (unsigned char)t[i + 1] == 0x80 &&
                ((unsigned char)t[i + 2] == 0xAF || (unsigned char)t[i + 2] == 0x89)) {   // schmale Leerzeichen
                i += 2;
                continue;
            }
            if (c == 0xE2 && i + 2 < t.size() && (unsigned char)t[i + 1] == 0x88 && (unsigned char)t[i + 2] == 0x92) {
                z += '-';                                                                  // «−»
                i += 2;
                continue;
            }
            z += (c == ',') ? '.' : (char)c;
        }
        char *ende = nullptr;
        double v = g_ascii_strtod(z.c_str(), &ende);
        if (z.empty() || !ende || ende == z.c_str()) return GTK_INPUT_ERROR;
        *neu = v;
        return TRUE;
    };
}

/// Zeile «Beschriftung  [Zahl] Einheit» in einem Grid
inline Gtk::SpinButton *spin_zeile(Gtk::Grid &grid, int zeile, std::string const &text, double wert,
                                   double min, double max, double schritt, int stellen,
                                   std::string const &einheit)
{
    auto l = label(text);
    l->set_hexpand(true);
    grid.attach(*l, 0, zeile);
    auto adj = Gtk::Adjustment::create(wert, min, max, schritt, schritt * 10);
    auto s = Gtk::make_managed<Gtk::SpinButton>(adj, schritt, stellen);
    s->set_numeric(true);
    punkt_und_komma(s);
    s->set_width_chars(7);
    s->set_activates_default(true);
    grid.attach(*s, 1, zeile);
    if (!einheit.empty()) {
        // Länge und Vorschub: bei Zoll zeigt und nimmt das Feld Zoll, der Wert bleibt mm (hirch-sprache.h)
        Groesse g;
        std::string e = einheit;
        if (einheit_erkennen(einheit, g)) {
            auto n = einheit_feld(s, g);
            if (einheit.find(' ') == std::string::npos) e = n;           // nur die Einheit: «mm», «mm/min»
            else e = einheit_text(einheit);
        }
        grid.attach(*label(e, "hirch-dim"), 2, zeile);
    }
    return s;
}

/// Ein Feld samt Beschriftung und Einheit (dieselbe Zeile im Grid) zeigen oder ausblenden –
/// statt ausgrauen: was nicht gilt, steht nicht da. Ausserhalb eines Grids das Widget selbst.
inline void zeile_zeigen(Gtk::Widget *w, bool an)
{
    if (!w) return;
    auto zeigen = [an](Gtk::Widget *k) {
        k->set_no_show_all(!an);
        if (an) k->show_all();
        else k->hide();
    };
    auto g = dynamic_cast<Gtk::Grid *>(w->get_parent());
    if (!g) {
        zeigen(w);
        return;
    }
    int top = 0;
    gtk_container_child_get(GTK_CONTAINER(g->gobj()), w->gobj(), "top-attach", &top, nullptr);
    for (auto k : g->get_children()) {
        int t = 0;
        gtk_container_child_get(GTK_CONTAINER(g->gobj()), k->gobj(), "top-attach", &t, nullptr);
        if (t == top) zeigen(k);
    }
}

/// Getippten Text übernehmen und Wert lesen. Nur das Feld mit dem Fokus kann ungelesenen Text haben (beim
/// Verlassen liest GTK selbst); bei den anderen würde update() den gerundet angezeigten Text zurücklesen und
/// z. B. 3,175 mm zu 3,17 kürzen (Windows 11.11.).
inline void spin_lesen(Gtk::SpinButton *s)
{
    if (!s->is_focus()) return;
    auto text = s->get_text();
    double alt = s->get_value();
    s->update();
    if (s->get_value() == alt) return;
    // zeigt der alte (genauere) Wert dasselbe wie das Feld, wurde nichts getippt: alten Wert behalten
    double neu = s->get_value();
    s->set_value(alt);
    if (s->get_text() != text) s->set_value(neu);
}

inline double wert(Gtk::SpinButton *s)
{
    spin_lesen(s);
    return s->get_value();
}

inline Gtk::Grid *formular()
{
    auto g = Gtk::make_managed<Gtk::Grid>();
    g->set_row_spacing(8);
    g->set_column_spacing(10);
    return g;
}

inline Gtk::Label *abschnitt(std::string const &text)
{
    auto l = label("");
    l->set_markup("<b>" + markup_escape(text) + "</b>");
    l->set_margin_top(6);
    return l;
}

/// Umspannen (Peter 27.10.): derselbe Abschnitt in «Auftrag einrichten» und in «Spannmittel einzeichnen» –
/// eine Umsetzung, eine Einstellung (einst["umspannen"] = {an, abstand ≥ 10, hoehe2 optional}).
struct UmspannenAbschnitt {
    Gtk::CheckButton *an = nullptr, *eigen = nullptr;
    Gtk::SpinButton *abstand = nullptr, *hoehe2 = nullptr;
    /// Stand der Felder in einst schreiben (nichts anlegen, solange es aus ist und nie an war)
    void speichern(bj::object &einst) const
    {
        bj::object uo;
        if (auto u = unter(einst, "umspannen")) uo = *u;
        else if (!an->get_active()) return;
        uo["an"] = an->get_active();
        uo["abstand"] = std::max(10.0, wert(abstand));
        if (eigen->get_active()) uo["hoehe2"] = wert(hoehe2);
        else uo.erase("hoehe2");
        einst["umspannen"] = uo;
    }
};

/// Höchstes eingezeichnetes Spannmittel der Spannung 1 (mm über dem Material)
inline double spannmittel_hoechstes(SPDocument *d)
{
    double m = 0;
    if (!d) return m;
    std::function<void(Inkscape::XML::Node *)> suchen = [&](Inkscape::XML::Node *n) {
        for (auto c = n->firstChild(); c; c = c->next()) {
            if (auto a = c->attribute("hirch:spannmittel")) {
                auto s2 = c->attribute("hirch:spannung");
                if (!(s2 && std::string(s2) == "2")) m = std::max(m, g_ascii_strtod(a, nullptr));
            }
            suchen(c);
        }
    };
    suchen(d->getReprRoot());
    return m;
}

/// Abschnitt «Umspannen» in box anlegen; geaendert (optional) bei jeder Änderung
inline std::shared_ptr<UmspannenAbschnitt> umspannen_abschnitt(Gtk::Box &box, bj::object const &einst, SPDocument *d,
                                                               std::function<void()> geaendert = {})
{
    auto a = std::make_shared<UmspannenAbschnitt>();
    bj::object uo;
    if (auto u = unter(einst, "umspannen")) uo = *u;
    double spann_max = spannmittel_hoechstes(d);
    auto t = abschnitt(TR("Umspannen"));
    box.add(*t);
    a->an = Gtk::make_managed<Gtk::CheckButton>(TR("Wo nötig umspannen – zwei Programme je Seite"));
    a->an->set_active(flag(uo, "an", false));
    a->an->set_tooltip_text(TR("Liegen Spannmittel über Fräsbahnen, fräst Spannung 1 alles ausser einer Zone um die "
                               "Spannmittel. Danach umspannen; Spannung 2 fräst den Rest. Bohrungen in einer Zone "
                               "kommen ganz in Spannung 2. Seiten, auf denen kein Spannmittel stört, bleiben bei "
                               "einem Programm."));
    box.add(*a->an);
    auto ug = formular();
    ug->set_margin_start(24);
    a->abstand = spin_zeile(*ug, 0, TR("Abstand zu den Spannmitteln"), std::max(10.0, num(uo, "abstand", 10)), 10,
                            200, 1, 1, "mm");
    a->abstand->set_tooltip_text(TR("Mindestens 10 mm. Dazu kommt der Radius des Fräsers."));
    a->eigen = Gtk::make_managed<Gtk::CheckButton>(TR("Eigene Höhe für die Leerfahrten in Spannung 2"));
    bool hat_h2 = uo.if_contains("hoehe2") && uo.at("hoehe2").is_number();
    a->eigen->set_active(hat_h2);
    ug->attach(*a->eigen, 0, 1, 3);
    a->hoehe2 = spin_zeile(*ug, 2, TR("Leerfahrten in Spannung 2 auf"),
                           hat_h2 ? num(uo, "hoehe2", 25) : spann_max + std::max(10.0, num(uo, "abstand", 10)), 0, 500,
                           1, 1, "mm");
    auto info = label("", "hirch-dim");
    klasse(*info, "hirch-klein");
    info->set_line_wrap(true);
    info->set_max_width_chars(48);
    info->set_xalign(0);
    ug->attach(*info, 0, 3, 3);
    box.add(*ug);
    ug->set_no_show_all(true);
    auto roh = a.get();
    auto abh = [roh, ug, info, spann_max] {
        ug->set_no_show_all(!roh->an->get_active());
        if (roh->an->get_active()) ug->show_all();
        else ug->hide();
        if (!roh->an->get_active()) return;
        zeile_zeigen(roh->hoehe2, roh->eigen->get_active());
        if (roh->eigen->get_active()) {
            info->set_text(TR("Die neue Lage der Spannmittel kennt Hirch nicht – darum bleiben die Leerfahrten "
                              "über dieser Höhe. Ist die zweite Lage eingezeichnet, gilt sie."));
        } else {
            info->set_text(TRF("Leerfahrten in Spannung 2 auf {} mm: höchstes Spannmittel {} mm + Abstand. Ist "
                               "die zweite Lage eingezeichnet, gilt sie.",
                               {zahl(spann_max + wert(roh->abstand), 1), zahl(spann_max, 1)}));
        }
    };
    auto bei = [abh, geaendert] {
        abh();
        if (geaendert) geaendert();
    };
    a->an->signal_toggled().connect(bei);
    a->eigen->signal_toggled().connect(bei);
    a->abstand->signal_value_changed().connect(bei);
    a->hoehe2->signal_value_changed().connect(bei);
    abh();
    return a;
}

/// Rundes Material im Dialog «Auftrag einrichten»: Abschnitt mit Form (Platte/Rund), Ø, Befestigung
/// (Schraube durch die Mittelbohrung) und Kerbe zum Wenden. Bei «Rund» sind Breite und Höhe der Ø
/// (die Seite wird ein Quadrat), X0 Y0 liegt immer in der Mitte – der Aufrufer blendet seine
/// XY-Wahl über «bei_rund» aus. speichern() schreibt einst["rund"].
struct RundAbschnitt {
    Gtk::RadioButton *platte = nullptr, *rund = nullptr;
    Gtk::SpinButton *durchmesser = nullptr, *bohrung = nullptr, *kopf = nullptr, *ueberstand = nullptr;
    Gtk::CheckButton *schraube = nullptr, *kerbe = nullptr, *grundplatte = nullptr;
    Gtk::Box *details = nullptr;
    bool ist_rund() const { return rund && rund->get_active(); }
    void speichern(bj::object &einst) const
    {
        bj::object r;
        if (auto alt = unter(einst, "rund")) r = *alt;
        r["an"] = ist_rund();
        r["schraube"] = schraube->get_active();
        r["bohrung"] = wert(bohrung);
        r["kopf"] = wert(kopf);
        r["ueberstand"] = wert(ueberstand);
        r["kerbe"] = kerbe->get_active();
        r["grundplatte"] = grundplatte->get_active();
        einst["rund"] = r;
    }
};

inline std::shared_ptr<RundAbschnitt> rund_abschnitt(Gtk::Box &box, bj::object const &einst, Gtk::SpinButton *sb,
                                                     Gtk::SpinButton *sh, std::function<void(bool)> bei_rund)
{
    auto a = std::make_shared<RundAbschnitt>();
    bj::object r;
    if (auto x = unter(einst, "rund")) r = *x;
    bool an = flag(r, "an", false);
    auto form = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 12);
    Gtk::RadioButton::Group fg;
    a->platte = Gtk::make_managed<Gtk::RadioButton>(fg, TR("Platte"));
    a->rund = Gtk::make_managed<Gtk::RadioButton>(fg, TR("Rund (Zylinder, stehend)"));
    a->rund->set_tooltip_text(TR("Auf der Drehbank vorbereitet, steht auf der Fräse. Die Seite wird ein Quadrat "
                                 "Ø × Ø, das Werkstück der Kreis darin; X0 Y0 liegt in der Mitte."));
    (an ? a->rund : a->platte)->set_active(true);
    form->add(*label(TR("Form")));
    form->add(*a->platte);
    form->add(*a->rund);
    box.add(*form);
    a->details = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);
    auto g = formular();
    double d0 = std::max(1.0, std::min(wert(sb), wert(sh)));
    a->durchmesser = spin_zeile(*g, 0, TR("Ø Durchmesser"), d0, 1, 5000, 1, 1, "mm");
    a->schraube = Gtk::make_managed<Gtk::CheckButton>(TR("Mit einer Schraube durch die Mitte befestigt"));
    a->schraube->set_active(flag(r, "schraube", true));
    g->attach(*a->schraube, 0, 1, 3);
    a->bohrung = spin_zeile(*g, 2, TR("Ø Mittelbohrung"), num(r, "bohrung", 6.5), 0, 200, 0.5, 1, "mm");
    a->kopf = spin_zeile(*g, 3, TR("Ø Schraubenkopf / Scheibe"), num(r, "kopf", 12), 0, 200, 0.5, 1, "mm");
    a->ueberstand = spin_zeile(*g, 4, TR("Kopf steht über"), num(r, "ueberstand", 0), 0, 100, 0.5, 1,
                               TR("mm (0 = versenkt)"));
    a->kopf->set_tooltip_text(TR("Um Kopf bzw. Scheibe wird nie gefräst (Kopf + Fräserradius + 1 mm)."));
    a->ueberstand->set_tooltip_text(TR("Steht der Kopf über, fährt der Fräser zwischen den Bahnen höher."));
    a->kerbe = Gtk::make_managed<Gtk::CheckButton>(TR("Kerbe am Rand bei X+ (zum Wenden)"));
    a->kerbe->set_tooltip_text(TR("Kleine Nut quer über den Rand, 2 mm tief: zeigt nach dem Wenden, wie das Teil "
                                  "gedreht war – für Teile, die nicht rundum gleich sind."));
    a->kerbe->set_active(flag(r, "kerbe", false));
    g->attach(*a->kerbe, 0, 5, 3);
    a->grundplatte = Gtk::make_managed<Gtk::CheckButton>(TR("Grundplatte auf der Spannvorrichtung: Ring und Mittelbohrung"));
    a->grundplatte->set_tooltip_text(TR("Ist eine Spannvorrichtung eingerichtet, fräst Hirch CAM auf ihr einen flachen "
                                        "Ring (Ø + 0,2 mm, 1 mm tief), in dem das Teil mittig steht, die Mittelbohrung "
                                        "für die Schraube ganz durch und – mit Kerbe – eine Marke bei X+. "
                                        "X0 Y0 ist die Mitte des Rings."));
    a->grundplatte->set_active(flag(r, "grundplatte", true));
    g->attach(*a->grundplatte, 0, 6, 3);
    a->details->add(*g);
    box.add(*a->details);
    auto schraube_abh = [a] {
        bool s = a->schraube->get_active();
        for (auto w : {a->bohrung, a->kopf, a->ueberstand}) w->set_sensitive(s);
    };
    a->schraube->signal_toggled().connect(schraube_abh);
    schraube_abh();
    auto anwenden = [a, sb, sh, bei_rund] {
        bool r_ = a->ist_rund();
        // Windows 29.10.: bei «Platte» nie aufblitzen – show_all() des Dialogs lässt sie aus
        a->details->set_no_show_all(!r_);
        if (r_) a->details->show_all();
        else a->details->hide();
        if (r_) {
            sb->set_value(wert(a->durchmesser));
            sh->set_value(wert(a->durchmesser));
        }
        sb->set_sensitive(!r_);
        sh->set_sensitive(!r_);
        if (bei_rund) bei_rund(r_);
    };
    a->rund->signal_toggled().connect(anwenden);
    a->durchmesser->signal_value_changed().connect(anwenden);
    a->details->set_no_show_all(!an);
    // erst wenn der Dialog steht – aber nicht mehr, wenn er bis dahin schon zu ist (Absturz)
    Glib::signal_idle().connect_once(sigc::track_object([anwenden] { anwenden(); }, *a->rund));
    return a;
}

} // namespace Inkscape::UI::Dialog

#endif
