// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Rille der Gravuren auf der Zeichenfläche und Abstand der Einlinientexte zur Rille
 * (Peter, 21.10.: «Bei Gravuren gibt es keinen Fall, bei dem eine dünne Linie Sinn macht»)
 *
 * - Jede Linie mit einer Gravur wird so breit gezeigt, wie sie gefräst wird (breite Mittellinie, Peter 11.11.): halbtransparent in der
 *   Farbe der Bearbeitung, nur auf dem Bildschirm (wie die Fräsbahnen), ohne zu rechnen.
 * - Einlinientexte bekommen zum Abstand der Schrift (gesetzt für eine Rille von 5 % der Schrifthöhe)
 *   einen Zuschlag = Rillenbreite − 5 % der Schrifthöhe; er gilt für alle Buchstabenpaare gleich.
 *   Gespeichert in hirch:zuschlag (Einheiten des Texts); der Abstand im Stil ist Korrektur + Zuschlag,
 *   das Feld «Abstand» des Textwerkzeugs zeigt nur die Korrektur (text-toolbar.cpp).
 */
#include "ui/dialog/hirch-intern.h"
#include "libnrtype/Layout-TNG.h"
#include "text-editing.h"

namespace Inkscape::UI::Dialog {

namespace {
double const PX = MM_PX;

guint32 rgb_aus_hex(std::string const &hex)
{
    if (hex.size() != 7 || hex[0] != '#') return 0xffffffff;
    return (guint32)std::strtoul(hex.c_str() + 1, nullptr, 16);
}

double abstand_rgb(guint32 a, guint32 b)
{
    int dr = int((a >> 16) & 0xff) - int((b >> 16) & 0xff);
    int dg = int((a >> 8) & 0xff) - int((b >> 8) & 0xff);
    int db = int(a & 0xff) - int(b & 0xff);
    return std::sqrt(double(dr * dr + dg * dg + db * db));
}

/// Ein Strich einer Einlinienschrift ist hin und zurück gefahren (geschlossen, Fläche null):
/// nur die erste Hälfte als offene Linie
std::optional<Geom::Path> halbieren(Geom::Path const &p)
{
    if (!p.closed() || p.size_default() < 2) return {};
    std::vector<Geom::Point> k;
    k.push_back(p.initialPoint());
    for (auto const &c : p) k.push_back(c.finalPoint());
    if (k.size() > 1 && Geom::distance(k.front(), k.back()) < 1e-9) k.pop_back();
    std::size_t n = k.size();
    if (n < 2) return {};
    double eps = 1e-3;
    {
        auto bb = p.boundsFast();
        if (bb) eps = std::max(1e-6, (bb->width() + bb->height()) * 1e-3);
    }
    for (std::size_t i = 1; i < n / 2 + 1; ++i) {
        if (Geom::distance(k[i], k[(n - i) % n]) > eps) return {};
    }
    Geom::Path offen(k[0]);
    for (std::size_t i = 1; i <= n / 2; ++i) offen.appendNew<Geom::LineSegment>(k[i]);
    return offen;
}
} // namespace

double HirchFraesen::Impl::rille_von(bj::object const &b)
{
    auto w = wirksam(str(b, "werkzeug", "A"));
    if (w.empty()) return 0.5;
    auto typ = str(w, "typ");
    double d = num(w, "durchmesser", 1);
    double t = str(b, "art") == "vcarve" ? 0.5 : num(b, "tiefe", 0.5);
    if (typ == "v") {
        double br = 2 * t * std::tan(num(w, "v_winkel", 60) * M_PI / 360.0) + num(w, "spitze", 0);
        return d > 0 ? std::min(d, br) : br;
    }
    if (typ == "kugel" && t < d / 2) {                  // Sehne der Kugel in der Tiefe
        double r = d / 2;
        return 2 * std::sqrt(std::max(0.0, r * r - (r - t) * (r - t)));
    }
    return d;
}

std::vector<HirchFraesen::Impl::GravurRille> HirchFraesen::Impl::gravur_rillen()
{
    std::vector<GravurRille> aus;
    auto bea = liste_von(einst, "bearbeitungen");
    if (!bea) return aus;
    for (auto const &v : *bea) {
        auto o = v.if_object();
        if (!o || str(*o, "art") != "gravur" || !flag(*o, "aktiv", true)) continue;
        GravurRille g;
        g.id = str(*o, "id");
        g.farbe = rgb_aus_hex(str(*o, "farbe", "#ff8c00"));
        g.breite = rille_von(*o);
        if (g.breite > 0) aus.push_back(g);
    }
    return aus;
}

std::pair<double, guint32> HirchFraesen::Impl::rille_fuer(SPItem *it, std::vector<GravurRille> const &gr)
{
    double breite = 0;
    guint32 farbe = 0;
    if (!it || gr.empty()) return {0, 0};
    // Farbe der Linie (Kontur); Text: Kontur des Texts
    guint32 rgb = 0xffffffff;
    if (it->style && it->style->stroke.isColor()) rgb = it->style->stroke.value.color.toRGBA32(1.0) >> 8;
    double best = 30;          // nur fast gleiche Farbe gilt als zugeordnet
    for (auto const &g : gr) {
        double a = rgb == 0xffffffff ? 1e9 : abstand_rgb(rgb, g.farbe);
        if (a <= best) {
            if (a < best || g.breite > breite) {
                breite = g.breite;
                farbe = g.farbe;
            }
            best = a;
        }
    }
    // zusätzlich zugeordnet (hirch:auch)
    for (SPObject *o = it; o && !is<SPRoot>(o); o = o->parent) {
        auto a = o->getRepr() ? o->getRepr()->attribute("hirch:auch") : nullptr;
        if (!a) continue;
        std::istringstream is(a);
        std::string id;
        while (is >> id) {
            for (auto const &g : gr) {
                if (g.id == id && g.breite > breite) {
                    breite = g.breite;
                    farbe = g.farbe;
                }
            }
        }
    }
    return {breite, farbe};
}

void HirchFraesen::Impl::rillen_spaeter()
{
    // die Rille kostet nur noch Millisekunden: sofort zeichnen, damit sie beim Ziehen mitgeht (Windows 11.11.:
    // sie blieb bis zum Ende der Rechnung an der alten Stelle); den Abstand der Texte erst nach einer Ruhe setzen
    rillen_zeichnen();
    rillen_takt.disconnect();
    rillen_takt = Glib::signal_timeout().connect([this] {
        zuschlaege_setzen();
        rillen_zeichnen();          // falls der Abstand eines Texts sich geändert hat
        return false;
    }, 120);
}

/// Kontur «Aussen»/«Innen» auf offenen Linien (Peter 11.11.): ein Pfeil in der Mitte jeder offenen Linie
/// zeigt die Zeichenrichtung, ein Strich davon zur Seite, die gefräst wird (aussen rechts, innen links).
/// «Pfad › Richtung umkehren» dreht die Linie und damit die Seite.
static void pfeile_zeichnen(SPDesktop *desk, SPDocument *d, bj::object const &einst,
                            std::vector<CanvasItemPtr<CanvasItemBpath>> &aus)
{
    struct Seite { guint32 farbe; bool aussen; };
    std::vector<Seite> seiten;
    if (auto bea = einst.if_contains("bearbeitungen"); bea && bea->is_array()) {
        for (auto const &v : bea->as_array()) {
            auto o = v.if_object();
            if (!o || str(*o, "art") != "kontur" || !flag(*o, "aktiv", true)) continue;
            auto seite = str(*o, "seite", "auto");
            if (seite != "aussen" && seite != "innen") continue;
            seiten.push_back({rgb_aus_hex(str(*o, "farbe", "#3c78dc")), seite == "aussen"});
        }
    }
    if (seiten.empty()) return;
    double const L = 3.0 * PX;      // Pfeil 3 mm, Strich zur Seite 2 mm (wachsen mit dem Zoom wie die Zeichnung)
    Geom::PathVector pv;
    auto dt = desk->doc2dt();
    std::function<void(SPObject *)> gehen = [&](SPObject *o) {
        for (auto &c : o->children) {
            auto it = cast<SPItem>(&c);
            if (!it || it->isHidden()) continue;
            if (is<SPGroup>(it)) {
                gehen(it);
                continue;
            }
            auto sh = cast<SPShape>(it);
            if (!sh || !sh->curve() || !it->style || !it->style->stroke.isColor()) continue;
            guint32 rgb = it->style->stroke.value.color.toRGBA32(1.0) >> 8;
            std::optional<bool> aussen;
            for (auto const &sd : seiten)
                if (abstand_rgb(rgb, sd.farbe) <= 30) aussen = sd.aussen;
            if (!aussen) continue;
            // in Koordinaten der Zeichnung rechnen (y nach unten wie auf dem Bildschirm), erst am Ende in die Ansicht
            for (auto const &p : sh->curve()->get_pathvector() * it->i2doc_affine()) {
                if (p.closed() || p.empty()) continue;
                if (Geom::distance(p.initialPoint(), p.finalPoint()) < 1e-6) continue;
                double t = p.size_default() / 2.0;
                Geom::Point m = p.pointAt(t);
                // Richtung: kurze Sehne um die Mitte (robust auch an einem Knoten)
                Geom::Point r = p.pointAt(std::min(t + 0.05, p.size_default() - 1e-9)) -
                                p.pointAt(std::max(t - 0.05, 0.0));
                if (!r.isFinite() || r.length() < 1e-9) continue;
                r = Geom::unit_vector(r);
                // rechts der Zeichenrichtung (y nach unten): (−ry, rx)
                Geom::Point n(-r[Geom::Y], r[Geom::X]);
                if (!*aussen) n = -n;
                Geom::Point spitze = m + r * (L / 2);
                Geom::Path pfeil(spitze - r * L * 0.45 + n * L * 0.3);
                pfeil.appendNew<Geom::LineSegment>(spitze);
                pfeil.appendNew<Geom::LineSegment>(spitze - r * L * 0.45 - n * L * 0.3);
                pv.push_back(pfeil);
                Geom::Path schaft(m - r * (L / 2));
                schaft.appendNew<Geom::LineSegment>(spitze);
                pv.push_back(schaft);
                Geom::Path strich(m);
                strich.appendNew<Geom::LineSegment>(m + n * (L * 0.7));
                pv.push_back(strich);
            }
        }
    };
    gehen(d->getRoot());
    if (pv.empty()) return;
    auto item = make_canvasitem<CanvasItemBpath>(desk->getCanvasSketch(), pv * dt, false);
    item->set_fill(0x0, SP_WIND_RULE_NONZERO);
    item->set_stroke(0x1a1a1ae6);
    item->set_stroke_width(1.5);
    item->set_pickable(false);
    aus.push_back(std::move(item));
}

void HirchFraesen::Impl::rillen_zeichnen()
{
    rillen.clear();
    auto desk = panel.getDesktop();
    auto d = doc();
    if (!desk || !d || !bahnen_an() || platte == 0) return;
    pfeile_zeichnen(desk, d, einst, rillen);
    auto gr = gravur_rillen();
    if (gr.empty()) return;
    // je Farbe und Breite eine Mittellinie, gezeichnet mit einem breiten, runden Strich (Peter 11.11.: der Umriss
    // der Rille kostete bei 35 Textzeilen über 6 s je Schritt – Verschieben ruckelte)
    std::map<std::pair<guint32, double>, Geom::PathVector> nach_farbe;
    auto zufuegen = [&](Geom::PathVector const &pv, double breite_mm, guint32 farbe, bool einlinig) {
        auto &ziel = nach_farbe[{farbe, breite_mm * PX}];
        for (auto const &p : pv) {
            if (p.empty()) continue;
            std::optional<Geom::Path> h;
            if (einlinig) h = halbieren(p);
            ziel.push_back(h ? *h : p);
        }
    };
    std::function<void(SPObject *)> gehen = [&](SPObject *o) {
        for (auto &c : o->children) {
            auto it = cast<SPItem>(&c);
            if (!it || it->isHidden()) continue;
            auto r = c.getRepr();
            if (r->attribute("hirch:passloch") || r->attribute("hirch:steg") || r->attribute("hirch:spannmittel") ||
                r->attribute("hirch:steg3d")) {
                continue;
            }
            if (is<SPText>(it) || is<SPFlowtext>(it)) {
                auto [b, f] = rille_fuer(it, gr);
                if (b <= 0) continue;
                auto layout = te_get_layout(it);
                if (!layout) continue;
                auto pv = layout->convertToCurves().get_pathvector() * it->i2doc_affine();
                bool einlinig = it->style && FontLister::ist_einlinie(it->style->font_family.value());
                zufuegen(pv, b, f, einlinig);
                continue;
            }
            if (is<SPGroup>(it)) {
                gehen(it);
                continue;
            }
            auto sh = cast<SPShape>(it);
            if (!sh || !sh->curve()) continue;
            auto [b, f] = rille_fuer(it, gr);
            if (b <= 0) continue;
            // Gravurtext aus dem früheren Dialog: Striche sind schon offene Linien
            zufuegen(sh->curve()->get_pathvector() * it->i2doc_affine(), b, f, false);
        }
    };
    gehen(d->getRoot());
    auto dt = desk->doc2dt();
    for (auto &[art, pv] : nach_farbe) {
        if (pv.empty()) continue;
        auto [farbe, breite] = art;
        auto item = make_canvasitem<CanvasItemBpath>(desk->getCanvasSketch(), pv * dt, false);
        item->set_fill(0x0, SP_WIND_RULE_NONZERO);
        item->set_stroke((farbe << 8) | 0x5c);
        item->set_rille(breite);
        item->set_pickable(false);
        rillen.push_back(std::move(item));
    }
}

void HirchFraesen::Impl::zuschlaege_setzen()
{
    auto d = doc();
    if (!d || !d->isSensitive()) return;
    bool text = false;
    if (werkzeug_pausiert(&text) && text) return;        // nicht mitten im Schreiben
    auto gr = gravur_rillen();
    bool geaendert = false;
    std::function<void(SPObject *)> gehen = [&](SPObject *o) {
        for (auto &c : o->children) {
            auto it = cast<SPItem>(&c);
            if (!it) continue;
            if (is<SPGroup>(it)) {
                gehen(it);
                continue;
            }
            if (!is<SPText>(it) || !it->style) continue;
            auto r = it->getRepr();
            double alt = r->attribute("hirch:zuschlag") ? g_ascii_strtod(r->attribute("hirch:zuschlag"), nullptr) : 0.0;
            double neu = 0;
            if (FontLister::ist_einlinie(it->style->font_family.value())) {
                auto [b, f] = rille_fuer(it, gr);
                (void)f;
                double fs = it->style->font_size.computed;
                double skala = it->i2doc_affine().descrim();
                if (b > 0 && fs > 0 && skala > 0) neu = b * PX / skala - RILLE_ANTEIL * fs;
            }
            double fs = std::max(1e-6, (double)it->style->font_size.computed);
            if (std::abs(neu - alt) < 1e-4 * fs) continue;
            double ls = it->style->letter_spacing.normal ? 0.0 : it->style->letter_spacing.computed;
            double korrektur = ls - alt;
            std::ostringstream os;
            os.imbue(std::locale::classic());
            os.precision(6);
            os << (korrektur + neu) << "px";
            SPCSSAttr *css = sp_repr_css_attr_new();
            sp_repr_css_set_property(css, "letter-spacing", os.str().c_str());
            sp_repr_css_change(r, css, "style");
            sp_repr_css_attr_unref(css);
            if (std::abs(neu) < 1e-9) {
                r->removeAttribute("hirch:zuschlag");
            } else {
                std::ostringstream z;
                z.imbue(std::locale::classic());
                z.precision(6);
                z << neu;
                r->setAttribute("hirch:zuschlag", z.str());
            }
            geaendert = true;
        }
    };
    gehen(d->getRoot());
    if (geaendert) DocumentUndo::anhaengen(d);      // gehört zum Schritt, der ihn ausgelöst hat
}

} // namespace Inkscape::UI::Dialog
