// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Spannvorrichtung, Zeichnung aufräumen, Vervielfältigen und Verschachteln
 */
#include "ui/dialog/hirch-intern.h"

#include <cstring>
#include <iomanip>
#include <fstream>
#include "object/object-set.h"
#include "object/sp-defs.h"
#include "object/sp-flowtext.h"
#include "object/sp-image.h"
#include "object/sp-shape.h"
#include "object/sp-switch.h"
#include "object/sp-use.h"
#include "object/sp-text.h"
#include "style.h"

namespace Inkscape::UI::Dialog {

void HirchFraesen::Impl::umriss_kopieren()
{
    auto d = doc();
    auto desk = panel.getDesktop();
    auto sel = panel.getSelection();
    auto v = unter(analyse, "vorrichtung");
    if (!d || !desk || !sel || !v) return;
    if (sel->isEmpty()) {
        desk->messageStack()->flash(Inkscape::WARNING_MESSAGE,
                                    TR("Erst Objekte des Werkstücks auswählen (z. B. die Aussenkontur)."));
        return;
    }
    auto ws = unter(*v, "werkstueck_seite");
    auto l = liste_von(*v, "lage");
    auto w = liste_von(*v, "werkstueck");
    if (!ws || !l || l->size() < 2 || !w || w->size() < 2) return;
    double dx = num(*v, "links", 0) + (*l)[0].to_number<double>() - num(*ws, "links", 0);
    double dy = num(*v, "oben", 0) + num(*v, "hoehe", 0) - (*l)[1].to_number<double>() - (*w)[1].to_number<double>() -
                num(*ws, "oben", 0);
    double const px = MM_PX;
    Geom::Point schub = Geom::Point(dx * px, dy * px) * desk->doc2dt().withoutTranslation();
    sel->duplicate(true);
    sel->moveRelative(schub, false);
    SPCSSAttr *css = sp_repr_css_attr_new();
    sp_repr_css_set_property(css, "stroke", "#808080");
    sp_repr_css_set_property(css, "fill", "none");
    sp_desktop_set_style(desk, css);
    sp_repr_css_attr_unref(css);
    DocumentUndo::done(d, TR("Umriss auf die Vorrichtung"), "");
    desk->messageStack()->flash(Inkscape::INFORMATION_MESSAGE,
                                TR("Umriss grau auf der Vorrichtung – mit Pfad › Versatz das Spiel geben und als Tasche einfärben."));
    platte_waehlen(0, true);
}

bool HirchFraesen::Impl::python_jetzt(std::vector<std::string> args, std::string &aus, std::string &fehler)
{
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
    g_subprocess_launcher_set_cwd(launcher, ordner.c_str());
    args.insert(args.begin(), {hirch_python(), "-m", "hirch_cam.dokument"});
    hirch_leere_argumente(args);
    std::vector<char const *> argv;
    for (auto const &a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    GError *err = nullptr;
    GSubprocess *proc = g_subprocess_launcher_spawnv(launcher, argv.data(), &err);
    g_object_unref(launcher);
    if (!proc) {
        fehler = err ? err->message : TR("Python nicht gestartet");
        if (err) g_error_free(err);
        return false;
    }
    char *o = nullptr, *e = nullptr;
    bool ok = g_subprocess_communicate_utf8(proc, nullptr, nullptr, &o, &e, &err);
    if (o) aus = o;
    if (e) fehler = e;
    g_free(o);
    g_free(e);
    if (err) {
        fehler = err->message;
        g_error_free(err);
    }
    g_object_unref(proc);
    return ok;
}

std::string HirchFraesen::Impl::import_endung(std::string const &pfad)
{
    auto p = pfad.rfind('.');
    if (p == std::string::npos) return {};
    auto e = Glib::ustring(pfad.substr(p + 1)).lowercase().raw();
    for (char const *k : {"dxf", "ai", "pdf", "eps", "ps", "plt", "hpgl"})
        if (e == k) return e;
    return {};
}

namespace {

/// Dateien, aus denen Hirch beim Import nur die Linien übernimmt (Peter 10.11.: «Es sollen wirklich nur die Pfade
/// übernommen werden»). «illustrator»: eine SVG-Datei aus Illustrator.
bool nur_pfade_endung(std::string const &e)
{
    return e == "ai" || e == "pdf" || e == "eps" || e == "ps" || e == "illustrator";
}

bool eigener_name(char const *name)
{
    if (!name) return true;
    auto d = std::strchr(name, ':');
    if (!d) return true;
    std::string pre(name, d - name);
    for (char const *k : {"svg", "inkscape", "sodipodi", "hirch", "xlink", "xml", "rdf", "cc", "dc"})
        if (pre == k) return true;
    if (pre == "xmlns") return eigener_name((std::string(d + 1) + ":x").c_str());
    return false;
}

/// Ballast entfernen. Immer: Farbprofile, Daten anderer Programme, unbenutzte Verläufe/Muster/Beschneidungen.
/// nur_pfade (Import aus Illustrator, PDF, AI, EPS): ausserdem Bilder, Verborgenes, Texte und Formen zu Pfaden,
/// Beschneidungen und Masken weg, alle Linien schwarz ohne Füllung (keine Fräsart aus dem Fremdprogramm).
/// `bereich`: die eingefügten Objekte (leer = ganze Zeichnung); wird nach dem Umwandeln neu gesucht.
std::vector<std::string> ballast_entfernen(SPDocument *d, std::vector<SPItem *> &bereich, bool nur_pfade)
{
    std::vector<std::string> bericht;
    auto wurzeln_holen = [&](std::vector<std::string> const &ids) {
        std::vector<SPItem *> w;
        if (ids.empty()) {
            for (auto &c : d->getRoot()->children)
                if (auto it = cast<SPItem>(&c)) w.push_back(it);
        } else {
            for (auto const &i : ids)
                if (auto it = cast<SPItem>(d->getObjectById(i))) w.push_back(it);
        }
        return w;
    };
    std::vector<std::string> ids;
    for (auto it : bereich)
        if (it->getId()) ids.emplace_back(it->getId());
    if (nur_pfade) {
        int bilder = 0, verborgen = 0, texte = 0, schnitte = 0, linien = 0;
        // 1. Bilder, Verborgenes und Fremdes weg; Umschalter (switch) auflösen
        std::vector<SPObject *> weg, umschalter;
        std::function<void(SPObject *)> gehen = [&](SPObject *o) {
            auto name = o->getRepr()->name();
            if (is<SPImage>(o)) { bilder++; weg.push_back(o); return; }
            if (name && !std::strcmp(name, "svg:foreignObject")) { weg.push_back(o); return; }
            auto it = cast<SPItem>(o);
            if (!it) return;
            if (it->style && it->style->display.computed == SP_CSS_DISPLAY_NONE) {
                verborgen++;
                weg.push_back(o);
                return;
            }
            if (is<SPText>(o) || is<SPFlowtext>(o)) { texte++; return; }
            if (is<SPSwitch>(o)) umschalter.push_back(o);
            for (auto &c : o->children) gehen(&c);
        };
        for (auto w : wurzeln_holen(ids)) gehen(w);
        for (auto o : weg) o->deleteObject(false);
        for (auto it = umschalter.rbegin(); it != umschalter.rend(); ++it) {
            auto r = (*it)->getRepr();
            auto par = r->parent();
            if (!par) continue;
            // verschieben statt kopieren: die IDs bleiben (Klone verweisen darauf)
            std::vector<Inkscape::XML::Node *> kinder;
            for (auto k = r->firstChild(); k; k = k->next())
                if (!k->name() || std::strcmp(k->name(), "svg:foreignObject")) kinder.push_back(k);
            auto vor = r;
            for (auto k : kinder) {
                Inkscape::GC::anchor(k);
                r->removeChild(k);
                par->addChild(k, vor);
                vor = k;
                Inkscape::GC::release(k);
            }
            par->removeChild(r);
        }
        // 2. Texte, Formen und Klone zu Pfaden (die ID bleibt)
        d->ensureUpToDate();
        for (int runde = 0; runde < 5; runde++) {   // Klone (auch verschachtelt) zu eigenen Objekten
            std::vector<SPUse *> klone;
            std::function<void(SPObject *)> klone_suchen = [&](SPObject *o) {
                if (auto u = cast<SPUse>(o)) { klone.push_back(u); return; }
                for (auto &c : o->children) klone_suchen(&c);
            };
            for (auto w : wurzeln_holen(ids)) klone_suchen(w);
            if (klone.empty()) break;
            for (auto u : klone)
                if (!u->unlink()) u->deleteObject(false);
            d->ensureUpToDate();
        }
        d->ensureUpToDate();
        {
            ObjectSet set(d);
            for (auto w : wurzeln_holen(ids)) set.add(w);
            set.toCurves(true, false);
        }
        d->ensureUpToDate();
        // 3. nur die Linie: schwarz, ohne Füllung, ohne Beschneidung und Maske
        Geom::Affine S = d->getRoot()->c2p;
        double strich = 0.2 * MM_PX / std::max(1e-9, S.descrim());   // 0,2 mm in Benutzereinheiten
        std::function<void(SPObject *)> stil = [&](SPObject *o) {
            auto it = cast<SPItem>(o);
            if (!it) return;
            auto r = o->getRepr();
            if (r->attribute("clip-path") || r->attribute("mask")) schnitte++;
            for (char const *a : {"clip-path", "mask", "class", "filter", "style", "fill", "stroke", "stroke-width",
                                  "opacity", "fill-opacity", "stroke-opacity", "stroke-dasharray", "fill-rule",
                                  "stroke-linecap", "stroke-linejoin", "stroke-miterlimit", "color"})
                r->removeAttribute(a);
            if (is<SPShape>(o)) {
                linien++;
                double f = std::max(1e-9, it->i2doc_affine().descrim());
                char zahl[G_ASCII_DTOSTR_BUF_SIZE];
                g_ascii_formatd(zahl, sizeof(zahl), "%.6g", strich / f);
                r->setAttribute("style", std::string("fill:none;stroke:#000000;stroke-width:") + zahl);
            }
            for (auto &c : o->children) stil(&c);
        };
        for (auto w : wurzeln_holen(ids)) stil(w);
        // 4. leere Gruppen weg
        std::function<bool(SPObject *)> leer = [&](SPObject *o) -> bool {
            if (!is<SPGroup>(o)) return false;
            std::vector<SPObject *> kinder;
            for (auto &c : o->children) kinder.push_back(&c);
            bool alle = true;
            for (auto c : kinder) {
                if (leer(c)) c->deleteObject(false);
                else if (cast<SPItem>(c)) alle = false;
            }
            return alle;
        };
        for (auto w : wurzeln_holen(ids))
            if (leer(w) && w->parent) w->deleteObject(false);
        // 5. eingebettete Stile (CSS) weg
        std::vector<Inkscape::XML::Node *> css;
        for (auto k = d->getReprRoot()->firstChild(); k; k = k->next())
            if (k->name() && !std::strcmp(k->name(), "svg:style")) css.push_back(k);
        if (auto defs = d->getDefs())
            for (auto k = defs->getRepr()->firstChild(); k; k = k->next())
                if (k->name() && !std::strcmp(k->name(), "svg:style")) css.push_back(k);
        for (auto k : css) k->parent()->removeChild(k);

        if (linien)
            bericht.push_back(TRF("Nur die Linien übernommen ({}): schwarz, ohne Füllung und ohne Fräsart – "
                                  "die Fräsart weist du selbst zu", {std::to_string(linien)}));
        if (texte) bericht.push_back(TRF("{} Text(e) in Linien umgewandelt", {std::to_string(texte)}));
        if (bilder)
            bericht.push_back(TRF("{} Bild(er) entfernt – Bilder bitte als Bilddatei importieren",
                                  {std::to_string(bilder)}));
        if (schnitte)
            bericht.push_back(TRF("{} Beschneidung(en) oder Maske(n) entfernt – jetzt sind auch die verdeckten "
                                  "Teile sichtbar", {std::to_string(schnitte)}));
        if (verborgen) bericht.push_back(TRF("{} verborgene(s) Objekt(e) entfernt", {std::to_string(verborgen)}));
        bereich = ids.empty() ? std::vector<SPItem *>{} : wurzeln_holen(ids);
    }
    // immer: Farbprofile und Daten anderer Programme
    size_t profil = 0;
    int fremd = 0;
    std::vector<Inkscape::XML::Node *> raus;
    std::function<void(Inkscape::XML::Node *)> xml = [&](Inkscape::XML::Node *n) {
        if (n->type() != Inkscape::XML::NodeType::ELEMENT_NODE) return;
        auto name = n->name();
        if (name && !std::strcmp(name, "svg:color-profile")) {
            if (auto h = n->attribute("xlink:href")) profil += std::strlen(h);
            raus.push_back(n);
            return;
        }
        if (!eigener_name(name) || (nur_pfade && name && !std::strcmp(name, "svg:foreignObject"))) {
            fremd++;
            raus.push_back(n);
            return;
        }
        std::vector<std::string> attr;
        for (auto const &a : n->attributeList()) {
            auto k = g_quark_to_string(a.key);
            if (!eigener_name(k)) attr.emplace_back(k);
        }
        for (auto const &a : attr) n->removeAttribute(a);
        if (!attr.empty()) fremd++;
        for (auto k = n->firstChild(); k; k = k->next()) xml(k);
    };
    xml(d->getReprRoot());
    for (auto n : raus)
        if (n->parent()) n->parent()->removeChild(n);
    // unbenutzte Verläufe, Muster, Beschneidungen, Masken, Filter, Symbole
    d->ensureUpToDate();
    int defs_weg = 0;
    for (int runde = 0; runde < 8; runde++) {
        std::vector<SPObject *> unbenutzt;
        std::function<void(SPObject *)> suchen = [&](SPObject *o) {
            for (auto &c : o->children) {
                auto r = c.getRepr();
                auto name = r->name();
                if (!name) continue;
                if (r->attribute("inkscape:swatch") || r->attribute("osb:paint")) continue;
                bool art = false;
                for (char const *k : {"svg:linearGradient", "svg:radialGradient", "svg:pattern", "svg:clipPath",
                                      "svg:mask", "svg:filter", "svg:symbol"})
                    art = art || !std::strcmp(name, k);
                if (art) {
                    if (c.hrefcount == 0) unbenutzt.push_back(&c);
                    continue;                    // nicht hinein (Inhalt einer Vorlage)
                }
                if (!std::strcmp(name, "svg:image") && is<SPDefs>(o) && c.hrefcount == 0) {
                    unbenutzt.push_back(&c);
                    continue;
                }
                // leere innere defs (aus Illustrator in einer Gruppe)
                if (!std::strcmp(name, "svg:defs") && &c != d->getDefs() &&
                    std::none_of(c.children.begin(), c.children.end(),
                                 [](SPObject const &k) { return k.getRepr()->type() == Inkscape::XML::NodeType::ELEMENT_NODE; })) {
                    unbenutzt.push_back(&c);
                    continue;
                }
                suchen(&c);
            }
        };
        suchen(d->getRoot());
        if (unbenutzt.empty()) break;
        for (auto o : unbenutzt) o->deleteObject(false);
        defs_weg += (int)unbenutzt.size();
        d->ensureUpToDate();
    }
    if (nur_pfade && bereich.empty()) {
        auto wurzel = d->getReprRoot();
        if (auto st = wurzel->attribute("style"); st && std::strstr(st, "enable-background")) wurzel->removeAttribute("style");
        for (auto k = wurzel->firstChild(); k; k = k->next())
            if (k->name() && !std::strcmp(k->name(), "svg:metadata") && !k->firstChild()) {
                wurzel->removeChild(k);
                break;
            }
    }
    if (profil)
        bericht.push_back(TRF("Farbprofil entfernt ({} KB)", {std::to_string((profil * 3 / 4 + 1023) / 1024)}));
    if (fremd || defs_weg) bericht.push_back(TR("Daten anderer Programme und unbenutzte Vorlagen entfernt"));
    return bericht;
}

} // namespace

void HirchFraesen::Impl::aufraeumen(std::string const &endung, bool nach_import)
{
    auto d = doc();
    auto desk = panel.getDesktop();
    if (!d || !desk) return;
    import_geprueft.insert(d);
    std::vector<SPItem *> bereich;
    if (auto sel = panel.getSelection())
        for (auto it : sel->items()) bereich.push_back(it);
    for (auto it : bereich)
        if (!it->getId()) it->setAttribute("id", d->generate_unique_id("g"));
    // Ballast aus anderen Programmen; nach dem Import aus Illustrator, PDF … nur die Linien (Peter 10.11.)
    auto ballast = ballast_entfernen(d, bereich, nach_import && nur_pfade_endung(endung));
    // jedes Objekt braucht eine ID, damit die Änderungen es wiederfinden
    std::function<void(SPObject *)> ids = [&](SPObject *o) {
        for (auto &c : o->children) {
            if (auto it = cast<SPItem>(&c)) {
                if (!it->getId()) it->setAttribute("id", d->generate_unique_id("path"));
                ids(it);
            }
        }
    };
    if (bereich.empty()) ids(d->getRoot());
    else for (auto it : bereich) {
        if (!it->getId()) it->setAttribute("id", d->generate_unique_id("g"));
        ids(it);
    }
    std::string id_liste;
    for (auto it : bereich) id_liste += (id_liste.empty() ? "" : ",") + std::string(it->getId());
    auto svg = Glib::build_filename(ordner, "aufraeumen.svg");
    try {
        Glib::file_set_contents(svg, sp_repr_save_buf(d->getReprDoc()).raw());
    } catch (Glib::Error const &e) {
        meldung(TR("Zeichnung aufräumen"), e.what().raw(), Gtk::MESSAGE_ERROR);
        return;
    }
    auto prefs = Inkscape::Preferences::get();
    status.set_text(TR("Zeichnung wird aufgeräumt …"));
    while (Gtk::Main::events_pending()) Gtk::Main::iteration(false);
    std::string o, e;
    bool ok = python_jetzt({"aufraeumen", svg, "--ids", id_liste, "--endung", endung,
                            "--fang", zahl_text(prefs->getDouble("/hirch/aufraeumen/fang", 0.05)),
                            "--schliessen", zahl_text(prefs->getDouble("/hirch/aufraeumen/schliessen", 1.0))},
                           o, e);
    status.set_text("");
    bj::object r;
    if (!ok || !json_lesen(o, r) || !flag(r, "ok", false)) {
        meldung(TR("Zeichnung aufräumen"), TR("Fehlgeschlagen: ") + (e.empty() ? o : e).substr(0, 600), Gtk::MESSAGE_ERROR);
        return;
    }
    bool geaendert = flag(r, "geaendert", false) || !ballast.empty();
    auto bericht = liste_von(r, "bericht");
    auto stellen = liste_von(r, "stellen");
    auto einheit = unter(r, "einheit");
    if (geaendert) {
        auto xml_doc = d->getReprDoc();
        Geom::Affine S = d->getRoot()->c2p;
        if (auto neu = liste_von(r, "neu")) {
            for (auto const &v : *neu) {
                auto n = v.if_object();
                if (!n) continue;
                auto anker = d->getObjectById(str(*n, "nach"));
                if (!anker || !anker->parent) continue;
                auto rep_ = xml_doc->createElement("svg:path");
                rep_->setAttribute("d", str(*n, "d"));
                rep_->setAttribute("style", str(*n, "style"));
                // weitere Bearbeitungen u. ä. des alten Objekts übernehmen
                for (auto const &a : anker->getRepr()->attributeList()) {
                    auto name = std::string(g_quark_to_string(a.key));
                    if (name.rfind("hirch:", 0) == 0) rep_->setAttribute(name, a.value.pointer());
                }
                Geom::Affine P;
                if (auto par = cast<SPItem>(anker->parent)) P = par->i2doc_affine();
                Geom::Affine L = S * P.inverse();
                if (!L.isIdentity(1e-9)) rep_->setAttribute("transform", sp_svg_transform_write(L));
                anker->parent->getRepr()->addChild(rep_, anker->getRepr());
                Inkscape::GC::release(rep_);
            }
        }
        if (auto weg = liste_von(r, "loeschen")) {
            for (auto const &v : *weg) {
                if (!v.is_string()) continue;
                if (auto obj = d->getObjectById(std::string(v.get_string()))) obj->deleteObject();
            }
        }
        DocumentUndo::done(d, nach_import ? TR("Import aufräumen") : TR("Zeichnung aufräumen"), "");
    }
    bool zu_melden = (bericht && !bericht->empty()) || einheit || !ballast.empty();
    if (!geaendert && !zu_melden) {
        if (nach_import) desk->messageStack()->flash(Inkscape::INFORMATION_MESSAGE,
                                                     TR("Import geprüft: keine offenen Pfade, keine doppelten Linien."));
        else meldung(TR("Zeichnung aufräumen"), TR("Nichts aufzuräumen: keine offenen Pfade, keine doppelten Linien."),
                     Gtk::MESSAGE_INFO);
        return;
    }
    auto dlg = dialog(nach_import ? TR("Nach dem Import aufgeräumt") : TR("Zeichnung aufgeräumt"));
    dlg->set_modal(false);
    auto box = dlg->get_content_area();
    for (auto const &b : ballast) {
        auto l = label("• " + b);
        l->set_xalign(0);
        l->set_line_wrap(true);
        l->set_max_width_chars(60);
        box->add(*l);
    }
    if (bericht) {
        for (auto const &b : *bericht) {
            if (!b.is_string()) continue;
            auto l = label("• " + std::string(b.get_string()));
            l->set_xalign(0);
            l->set_line_wrap(true);
            l->set_max_width_chars(60);
            box->add(*l);
        }
    }
    if (stellen && !stellen->empty()) {
        box->add(*abschnitt(TR("Stellen zum Prüfen")));
        auto liste = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
        for (auto const &sv : *stellen) {
            auto so = sv.if_object();
            if (!so) continue;
            double x = num(*so, "x", 0), y = num(*so, "y", 0);
            auto b = Gtk::make_managed<Gtk::Button>();
            auto l = label(str(*so, "text"));
            l->set_xalign(0);
            b->add(*l);
            b->set_relief(Gtk::RELIEF_NONE);
            b->set_tooltip_text(TR("Klick: Stelle in der Zeichnung zeigen"));
            b->signal_clicked().connect([this, x, y] {
                auto dd = doc();
                auto dk = panel.getDesktop();
                if (!dd || !dk) return;
                Geom::Point p = Geom::Point(x, y) * dd->getRoot()->c2p * dk->doc2dt();
                double r = 12.0 * MM_PX;
                dk->set_display_area(Geom::Rect(p - Geom::Point(r, r), p + Geom::Point(r, r)), 10);
            });
            liste->add(*b);
        }
        if (stellen->size() > 8) {
            auto sw = Gtk::make_managed<Gtk::ScrolledWindow>();
            sw->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
            sw->set_min_content_height(220);
            sw->add(*liste);
            box->add(*sw);
        } else {
            box->add(*liste);
        }
    }
    if (einheit) {
        double f = num(*einheit, "faktor", 1);
        box->add(*abschnitt(TR("Einheit")));
        auto l = label(str(*einheit, "text"));
        l->set_line_wrap(true);
        l->set_max_width_chars(60);
        l->set_xalign(0);
        box->add(*l);
        auto b = Gtk::make_managed<Gtk::Button>(f > 1 ? TR("× 25,4 vergrössern (Zoll → mm)") : TR("÷ 25,4 verkleinern"));
        b->set_halign(Gtk::ALIGN_START);
        b->signal_clicked().connect([this, f, bereich, b] {
            auto dd = doc();
            auto dk = panel.getDesktop();
            if (!dd || !dk) return;
            ObjectSet set(dd);
            if (bereich.empty()) {
                for (auto &c : dd->getRoot()->children)
                    if (auto it = cast<SPItem>(&c)) set.add(it);
            } else {
                set.setList(bereich);
            }
            // um die linke untere Ecke der Seite (Nullpunkt der CAD-Zeichnung)
            Geom::Point ecke = Geom::Point(0, dd->getHeight().value("px")) * dk->doc2dt();
            set.applyAffine(Geom::Translate(-ecke) * Geom::Scale(f) * Geom::Translate(ecke), true, true, true);
            DocumentUndo::done(dd, TR("Einheit Zoll"), "");
            b->set_sensitive(false);
        });
        box->add(*b);
    }
    if (geaendert) dlg->add_button(TR("Rückgängig"), 1);
    klasse(*dlg->add_button(geaendert ? TR("Behalten") : TR("Schliessen"), Gtk::RESPONSE_OK), "suggested-action");
    dlg->set_default_response(Gtk::RESPONSE_OK);
    dlg->show_all();
    int antwort = dlg->run();
    if (antwort == 1) {
        auto dd = doc();
        if (dd) DocumentUndo::undo(dd);
    }
}

std::string HirchFraesen::Impl::zahl_text(double v)
{
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << v;
    return o.str();
}

void HirchFraesen::Impl::verschachteln()
{
    auto d = doc();
    if (!d) return;
    auto svg = Glib::build_filename(ordner, "nesting.svg");
    try {
        auto xml = sp_repr_save_buf(d->getReprDoc());
        Glib::file_set_contents(svg, (hat_text(xml) ? text_als_pfade(d) : xml).raw());
    } catch (Glib::Error const &e) {
        meldung(TR("Verschachteln"), e.what().raw(), Gtk::MESSAGE_ERROR);
        return;
    }
    std::string ids;
    if (auto sel = panel.getSelection()) {
        for (auto item : sel->items()) {
            if (auto id = item->getId()) ids += (ids.empty() ? "" : ",") + std::string(id);
        }
    }
    status.set_text(TR("Teile werden gelesen …"));
    python({"nesting", "teile", svg, "--ids", ids}, [this, svg](bool ok, std::string const &o, std::string const &e) {
        bj::object r;
        status.set_text("");
        if (!ok || !json_lesen(o, r)) {
            meldung(TR("Verschachteln"), TR("Fehlgeschlagen: ") + (e.empty() ? o : e).substr(0, 600), Gtk::MESSAGE_ERROR);
            return;
        }
        if (!flag(r, "ok", false)) {
            meldung(TR("Verschachteln"), warnungen_text(r), Gtk::MESSAGE_WARNING);
            return;
        }
        Glib::signal_idle().connect_once([this, r, svg] { verschachteln_dialog(r, svg); });
    });
}

void HirchFraesen::Impl::verschachteln_dialog(bj::object info, std::string const &svg)
{
    auto teile = liste_von(info, "teile");
    if (!teile || teile->empty()) {
        meldung(TR("Vervielfältigen und verschachteln"),
                TR("Keine Teile gefunden. Teile auswählen (Objekte oder Gruppen in den Fräsfarben) "
                "– oder nichts auswählen: dann alle auf der ersten Platte."), Gtk::MESSAGE_INFO);
        return;
    }
    auto prefs = Inkscape::Preferences::get();
    auto dlg = dialog(TR("Vervielfältigen und verschachteln"));
    dlg->set_resizable(true);
    auto box = dlg->get_content_area();
    double pb = 0, ph = 0;
    if (auto p = unter(info, "platte")) {
        pb = num(*p, "breite", 0);
        ph = num(*p, "hoehe", 0);
    }
    auto zeilen = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 24);
    box->add(*zeilen);
    auto links = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
    zeilen->add(*links);
    auto rechts = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 8);
    zeilen->add(*rechts);

    auto intro = label(std::string(flag(info, "auswahl", false) ? TR("Die ausgewählten Teile")
                                                                 : TR("Alle Teile auf der ersten Platte (nichts ausgewählt)")) +
                       TR(" – Platte = Material ") + zahl(pb, 0) + " × " + zahl(ph, 0) + TR(" mm. Was nicht passt, "
                       "kommt auf weitere Platten (neue Seiten der Zeichnung).") +
                       (flag(info, "zweiseitig", false) ? TR(" Zweiseitig: die Unterseite jedes Teils wandert mit, "
                                                         "die Passlöcher bleiben frei.") : std::string()),
                       "hirch-dim");
    intro->set_line_wrap(true);
    intro->set_max_width_chars(46);
    intro->set_xalign(0);
    links->add(*intro);

    links->add(*abschnitt(TR("Teile")));
    auto tg = formular();
    tg->attach(*label(TR("Teil"), "hirch-dim"), 0, 0);
    tg->attach(*label(TR("Grösse"), "hirch-dim"), 1, 0);
    tg->attach(*label(TR("Anzahl"), "hirch-dim"), 2, 0);
    std::vector<std::pair<std::string, Gtk::SpinButton *>> anzahl;
    int z = 1;
    for (auto const &tv : *teile) {
        auto t = tv.if_object();
        if (!t) continue;
        auto n = label(str(*t, "name"));
        n->set_hexpand(true);
        n->set_xalign(0);
        n->set_ellipsize(Pango::ELLIPSIZE_END);
        n->set_max_width_chars(20);
        tg->attach(*n, 0, z);
        tg->attach(*label(zahl(num(*t, "breite", 0), 0) + " × " + zahl(num(*t, "hoehe", 0), 0) + " mm",
                          "hirch-dim"), 1, z);
        auto adj = Gtk::Adjustment::create(prefs->getInt("/hirch/nesting/anzahl", 4), 0, 999, 1, 10);
        auto sp = Gtk::make_managed<Gtk::SpinButton>(adj, 1, 0);
        sp->set_numeric(true);
        sp->set_width_chars(4);
        tg->attach(*sp, 2, z);
        anzahl.emplace_back(str(*t, "id"), sp);
        ++z;
    }
    if (z > 6) {
        auto sw = Gtk::make_managed<Gtk::ScrolledWindow>();
        sw->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
        sw->set_min_content_height(170);
        sw->add(*tg);
        links->add(*sw);
    } else {
        links->add(*tg);
    }
    auto fuellen = Gtk::make_managed<Gtk::CheckButton>(TR("Platte füllen – so viele, wie draufpassen"));
    fuellen->set_active(prefs->getBool("/hirch/nesting/fuellen", false));
    links->add(*fuellen);
    auto bleibt = Gtk::make_managed<Gtk::CheckButton>(TR("Original bleibt, wo es ist (zählt als eines)"));
    bleibt->set_active(prefs->getBool("/hirch/nesting/original_bleibt", true));
    links->add(*bleibt);

    links->add(*abschnitt(TR("Anordnen")));
    auto og = formular();
    int r = 0;
    auto combo = [&](std::string const &titel, std::vector<std::pair<char const *, char const *>> eintraege,
                     std::string const &pref, char const *vorgabe) {
        og->attach(*label(titel), 0, r);
        auto c = Gtk::make_managed<Gtk::ComboBoxText>();
        for (auto [id, text] : eintraege) c->append(id, text);
        auto alt = prefs->getString(pref, vorgabe);
        if (!c->set_active_id(alt)) c->set_active_id(vorgabe);
        og->attach(*c, 1, r, 2, 1);
        ++r;
        return c;
    };
    auto art = combo(TR("Art"), {{"eng", TR("eng verschachteln")}, {"raster", TR("Raster (Reihen und Spalten)")}},
                     "/hirch/nesting/art", "eng");
    auto richtung = combo(TR("Füllen"), {{"auto", TR("automatisch (längere Seite)")}, {"x", TR("zuerst nach X (Reihen)")},
                                     {"y", TR("zuerst nach Y (Spalten)")}},
                          "/hirch/nesting/richtung", "auto");
    auto ecke = combo(TR("Startecke"), {{"ul", TR("unten links")}, {"ol", TR("oben links")}, {"ur", TR("unten rechts")},
                                    {"or", TR("oben rechts")}},
                      "/hirch/nesting/ecke", "ul");
    auto drehen = combo(TR("Drehen"), {{"keine", TR("nicht drehen (Faserrichtung)")}, {"90", TR("in 90°-Schritten")},
                                   {"frei", TR("frei (auch schräg)")}},
                        "/hirch/nesting/drehen", "90");
    double ab_v = prefs->getDouble("/hirch/nesting/abstand", 0);
    double aby_v = prefs->getDouble("/hirch/nesting/abstand_y", 0);
    double rd_v = prefs->getDouble("/hirch/nesting/rand", -1);
    auto abstand = spin_zeile(*og, r++, TR("Abstand (X)"), ab_v > 0 ? ab_v : num(info, "abstand", 9), 0, 500, 0.5, 1, "mm");
    abstand->set_tooltip_text(TR("Zwischen den gezeichneten Umrissen – mindestens der Fräserdurchmesser. "
                              "Vorschlag: grösster Konturfräser + 3 mm."));
    auto abstand_y = spin_zeile(*og, r++, TR("Abstand Y (Raster)"), aby_v > 0 ? aby_v : num(info, "abstand", 9),
                                0, 500, 0.5, 1, "mm");
    auto rand = spin_zeile(*og, r++, TR("Rand der Platte"), rd_v >= 0 ? rd_v : num(info, "rand", 10), 0, 500, 0.5, 1, "mm");
    links->add(*og);
    auto in_l = Gtk::make_managed<Gtk::CheckButton>(TR("Kleine Teile in Löcher grosser Teile setzen"));
    in_l->set_active(prefs->getBool("/hirch/nesting/in_loechern", true));
    in_l->set_tooltip_text(TR("Löcher sind ganz durchgefräste innere Konturen"));
    links->add(*in_l);
    auto mitte = Gtk::make_managed<Gtk::CheckButton>(TR("Auf der Platte zentrieren"));
    mitte->set_active(prefs->getBool("/hirch/nesting/zentrieren", false));
    links->add(*mitte);

    // --- Vorschau
    rechts->add(*abschnitt(TR("Vorschau")));
    auto bild = Gtk::make_managed<Gtk::DrawingArea>();
    bild->set_size_request(440, 330);
    bild->set_vexpand(true);
    bild->set_hexpand(true);
    rechts->add(*bild);
    auto zusammen = label(TR("Rechnet …"), "hirch-dim");
    zusammen->set_xalign(0);
    zusammen->set_line_wrap(true);
    zusammen->set_max_width_chars(52);
    rechts->add(*zusammen);
    auto legende = label(TR("Grün: neue Kopien · Blau: Original bleibt · Grau: schon auf der Platte"), "hirch-dim");
    klasse(*legende, "hirch-klein");
    legende->set_xalign(0);
    rechts->add(*legende);

    struct Zustand {
        bool lebt = true;
        unsigned gen = 0;
        bj::object erg;
        sigc::connection timer;
    };
    auto zs = std::make_shared<Zustand>();

    auto auftrag_bauen = [=, &anzahl]() {
        bj::object a;
        bj::array tt;
        for (auto &x : anzahl) {
            bj::object t;
            t["id"] = x.first;
            t["anzahl"] = (int)wert(x.second);
            tt.push_back(t);
        }
        a["teile"] = tt;
        a["drehen"] = drehen->get_active_id().raw();
        a["art"] = art->get_active_id().raw();
        a["richtung"] = richtung->get_active_id().raw();
        a["ecke"] = ecke->get_active_id().raw();
        a["abstand"] = wert(abstand);
        a["abstand_y"] = art->get_active_id() == "raster" ? wert(abstand_y) : wert(abstand);
        a["rand"] = wert(rand);
        a["in_loechern"] = in_l->get_active();
        a["fuellen"] = fuellen->get_active();
        a["original_bleibt"] = bleibt->get_active();
        a["zentrieren"] = mitte->get_active();
        return a;
    };

    bild->signal_draw().connect([zs, bild](Cr const &cr) {
        int w = bild->get_allocated_width(), h = bild->get_allocated_height();
        cr->set_source_rgb(0.90, 0.90, 0.89);
        cr->rectangle(0, 0, w, h);
        cr->fill();
        auto b = unter(zs->erg, "bild");
        if (!b) return true;
        int n = std::max(1, (int)num(*b, "platten", 1));
        double pw = num(*b, "breite", 1), ph = num(*b, "hoehe", 1);
        double luecke = 14;
        // Platten nebeneinander, sonst untereinander – was grösser wird
        double s1 = std::min((w - 20 - luecke * (n - 1)) / (n * pw), (h - 36) / ph);
        double s2 = std::min((w - 20) / pw, (h - 20 - (luecke + 16) * (n - 1)) / (n * ph));
        bool neben = s1 >= s2;
        double sk = std::max(0.01, neben ? s1 : s2);
        auto ursprung = [&](int k, double &ox, double &oy) {
            if (neben) {
                double gesamt = n * pw * sk + luecke * (n - 1);
                ox = (w - gesamt) / 2 + k * (pw * sk + luecke);
                oy = (h - ph * sk) / 2 + 8;
            } else {
                double gesamt = n * ph * sk + (luecke + 16) * (n - 1);
                ox = (w - pw * sk) / 2;
                oy = (h - gesamt) / 2 + 8 + k * (ph * sk + luecke + 16);
            }
        };
        for (int k = 0; k < n; ++k) {
            double ox, oy;
            ursprung(k, ox, oy);
            cr->set_source_rgb(1, 1, 1);
            cr->rectangle(ox, oy, pw * sk, ph * sk);
            cr->fill_preserve();
            cr->set_source_rgb(0.55, 0.55, 0.53);
            cr->set_line_width(1);
            cr->stroke();
            cr->set_source_rgb(0.35, 0.35, 0.33);
            cr->move_to(ox, oy - 4);
            cr->set_font_size(11);
            cr->show_text(einheit_text_zahlen(TR("Platte ") + std::to_string(k + 1)));
        }
        if (auto ts = liste_von(*b, "teile")) {
            for (auto const &tv : *ts) {
                auto t = tv.if_object();
                if (!t) continue;
                int k = (int)num(*t, "platte", 0);
                double ox, oy;
                ursprung(k, ox, oy);
                auto a = str(*t, "art");
                auto ringe = liste_von(*t, "ringe");
                if (!ringe) continue;
                for (auto const &rv : *ringe) {
                    auto ring = rv.if_array();
                    if (!ring || ring->size() < 6) continue;
                    for (std::size_t i = 0; i + 1 < ring->size(); i += 2) {
                        double x = ox + (*ring)[i].to_number<double>() * sk;
                        double y = oy + (ph - (*ring)[i + 1].to_number<double>()) * sk;
                        if (i == 0) cr->move_to(x, y);
                        else cr->line_to(x, y);
                    }
                    cr->close_path();
                }
                cr->set_fill_rule(Cairo::FILL_RULE_EVEN_ODD);
                if (a == "kopie") cr->set_source_rgb(0.62, 0.83, 0.68);
                else if (a == "original") cr->set_source_rgb(0.58, 0.71, 0.96);
                else cr->set_source_rgb(0.80, 0.80, 0.78);
                cr->fill_preserve();
                if (a == "kopie") cr->set_source_rgb(0.08, 0.50, 0.24);
                else if (a == "original") cr->set_source_rgb(0.11, 0.30, 0.85);
                else cr->set_source_rgb(0.55, 0.55, 0.53);
                cr->set_line_width(1);
                cr->stroke();
            }
        }
        return true;
    });

    auto rechnen_ = [=, this]() {
        unsigned g = ++zs->gen;
        auto datei = Glib::build_filename(ordner, "nesting_vorschau.json");
        try {
            Glib::file_set_contents(datei, bj::serialize(auftrag_bauen()));
        } catch (Glib::Error const &) {
            return;
        }
        zusammen->set_text(TR("Rechnet …"));
        python({"nesting", "rechnen", svg, "--auftrag", datei},
               [zs, g, bild, zusammen](bool ok, std::string const &o, std::string const &) {
                   if (!zs->lebt || g != zs->gen) return;
                   bj::object r_;
                   if (!ok || !json_lesen(o, r_)) {
                       zusammen->set_text(TR("Vorschau fehlgeschlagen."));
                       return;
                   }
                   zs->erg = r_;
                   if (!flag(r_, "ok", false) && !unter(r_, "bild")) {
                       zusammen->set_text(warnungen_text(r_));
                   } else {
                       int n_pl = std::max(1, (int)num(r_, "platten", 1));
                       std::string t = std::to_string((long)num(r_, "teile_gesamt", num(r_, "gelegt", 0))) +
                                       TR(" Teile auf ") + (n_pl == 1 ? std::string(TR("1 Platte"))
                                                                   : std::to_string(n_pl) + TR(" Platten")) +
                                       TR(" · Ausnutzung ") + zahl(num(r_, "nutzung", 0), 0) + " %";
                       auto w_ = warnungen_text(r_);
                       if (!w_.empty()) t += "\n" + w_;
                       zusammen->set_text(t);
                   }
                   bild->queue_draw();
               });
    };
    auto spaeter_ = [zs, rechnen_]() {
        zs->timer.disconnect();
        zs->timer = Glib::signal_timeout().connect([zs, rechnen_] {
            if (zs->lebt) rechnen_();
            return false;
        }, 350);
    };
    auto sensitiv = [=, &anzahl]() {
        bool ra = art->get_active_id() == "raster";
        abstand_y->set_sensitive(ra);
        for (auto &x : anzahl) x.second->set_sensitive(!fuellen->get_active());
    };
    for (auto &x : anzahl) x.second->signal_value_changed().connect(spaeter_);
    for (auto sp : {abstand, abstand_y, rand}) sp->signal_value_changed().connect(spaeter_);
    for (auto c : {art, richtung, ecke, drehen}) c->signal_changed().connect([=] { sensitiv(); spaeter_(); });
    for (auto c : {fuellen, bleibt, in_l, mitte}) c->signal_toggled().connect([=] { sensitiv(); spaeter_(); });
    sensitiv();

    dlg->add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
    klasse(*dlg->add_button(TR("Anordnen"), Gtk::RESPONSE_OK), "suggested-action");
    dlg->set_default_response(Gtk::RESPONSE_OK);
    dlg->show_all();
    rechnen_();
    int antwort = dlg->run();
    zs->lebt = false;
    zs->timer.disconnect();
    if (antwort != Gtk::RESPONSE_OK) return;
    auto auftrag = auftrag_bauen();
    prefs->setString("/hirch/nesting/drehen", drehen->get_active_id());
    prefs->setString("/hirch/nesting/art", art->get_active_id());
    prefs->setString("/hirch/nesting/richtung", richtung->get_active_id());
    prefs->setString("/hirch/nesting/ecke", ecke->get_active_id());
    prefs->setDouble("/hirch/nesting/abstand", wert(abstand));
    prefs->setDouble("/hirch/nesting/abstand_y", wert(abstand_y));
    prefs->setDouble("/hirch/nesting/rand", wert(rand));
    prefs->setBool("/hirch/nesting/in_loechern", in_l->get_active());
    prefs->setBool("/hirch/nesting/fuellen", fuellen->get_active());
    prefs->setBool("/hirch/nesting/original_bleibt", bleibt->get_active());
    prefs->setBool("/hirch/nesting/zentrieren", mitte->get_active());
    if (!anzahl.empty()) prefs->setInt("/hirch/nesting/anzahl", (int)wert(anzahl.front().second));
    dlg->hide();
    auto datei = Glib::build_filename(ordner, "nesting.json");
    try {
        Glib::file_set_contents(datei, bj::serialize(auftrag));
    } catch (Glib::Error const &e) {
        meldung(TR("Verschachteln"), e.what().raw(), Gtk::MESSAGE_ERROR);
        return;
    }
    status.set_text(TR("Verschachtelt …"));
    python({"nesting", "rechnen", svg, "--auftrag", datei},
           [this](bool ok, std::string const &o, std::string const &e) {
               bj::object r_;
               status.set_text("");
               if (!ok || !json_lesen(o, r_)) {
                   meldung(TR("Verschachteln"), TR("Fehlgeschlagen: ") + (e.empty() ? o : e).substr(0, 600),
                           Gtk::MESSAGE_ERROR);
                   return;
               }
               if (!flag(r_, "ok", false)) {
                   meldung(TR("Verschachteln"), warnungen_text(r_).empty() ? TR("Es wurde nichts gelegt.")
                                                                     : warnungen_text(r_),
                           Gtk::MESSAGE_WARNING);
                   return;
               }
               Glib::signal_idle().connect_once([this, r_] { verschachteln_anwenden(r_); });
           });
}

void HirchFraesen::Impl::verschachteln_anwenden(bj::object const &r)
{
    auto d = doc();
    if (!d) return;
    auto &pm = d->getPageManager();
    bool zwei = zweiseitig();
    if (auto ns = liste_von(r, "neue_seiten")) {
        for (auto const &v : *ns) {
            auto a = v.if_array();
            if (!a || a->size() < 4) continue;
            auto zahl_ = [](bj::value const &x) { return x.is_double() ? x.get_double() : (double)x.to_number<double>(); };
            pm.newPage(Geom::Rect::from_xywh(zahl_((*a)[0]), zahl_((*a)[1]), zahl_((*a)[2]), zahl_((*a)[3])));
        }
        if (!ns->empty()) {
            int pp = zwei ? 2 : 1;
            for (int i = 0; i < pm.getPageCount(); ++i) {
                auto pg = pm.getPage(i);
                if (!pg) continue;
                std::string l = TR("Platte ") + std::to_string(i / pp + 1);
                if (zwei) l += i % 2 ? TR(" · Seite 2 (nach dem Wenden)") : TR(" · Seite 1");
                pg->getRepr()->setAttribute("inkscape:label", l);
            }
        }
    }
    // erst alle Kopien vom unveränderten Original, dann bewegen
    struct Zug { SPItem *item; Geom::Affine m; };
    std::vector<Zug> zuege;
    auto xml_doc = d->getReprDoc();
    if (auto bw = liste_von(r, "bewegen")) {
        for (auto const &v : *bw) {
            auto o = v.if_object();
            if (!o) continue;
            auto orig = cast<SPItem>(d->getObjectById(str(*o, "id")));
            auto mv = liste_von(*o, "matrix");
            if (!orig || !mv || mv->size() != 6) continue;
            double m[6];
            for (int i = 0; i < 6; ++i) {
                auto const &x = (*mv)[i];
                m[i] = x.is_double() ? x.get_double() : x.to_number<double>();
            }
            SPItem *ziel = orig;
            if (num(*o, "kopie", 0) > 0) {
                auto neu = orig->getRepr()->duplicate(xml_doc);
                orig->getRepr()->parent()->addChild(neu, orig->getRepr());
                Inkscape::GC::release(neu);
                ziel = cast<SPItem>(d->getObjectByRepr(neu));
            }
            if (ziel) zuege.push_back({ziel, Geom::Affine(m[0], m[1], m[2], m[3], m[4], m[5])});
        }
    }
    Geom::Affine S = d->getRoot()->c2p;       // Benutzereinheiten -> px
    for (auto &z_ : zuege) {
        auto item = z_.item;
        Geom::Affine P;
        if (auto par = cast<SPItem>(item->parent)) P = par->i2doc_affine();
        Geom::Affine T = S.inverse() * z_.m * S;
        item->doWriteTransform(item->transform * P * T * P.inverse(), nullptr, true);
    }
    DocumentUndo::done(d, TR("Vervielfältigen und verschachteln"), "");
    int n_pl = (int)num(r, "platten", 1);
    std::string t = std::to_string((long)num(r, "teile_gesamt", num(r, "gelegt", 0))) + TR(" Teile auf ") +
                    (n_pl == 1 ? std::string(TR("1 Platte")) : std::to_string(n_pl) + TR(" Platten")) +
                    TR(" gelegt · Ausnutzung ") + zahl(num(r, "nutzung", 0), 0) + " %";
    status.set_text(t);
    if (auto ws = liste_von(r, "warnungen"); ws && !ws->empty()) {
        meldung(TR("Verschachteln"), t + "\n\n" + warnungen_text(r), Gtk::MESSAGE_WARNING);
    }
    if (auto desk = panel.getDesktop()) {
        if (auto w = desk->getInkscapeWindow()) w->activate_action("canvas-zoom-drawing");
    }
    letzte_xml.clear();
    spaeter(50);
}

} // namespace Inkscape::UI::Dialog

/// Oberflächentest: Ballast aus `ein` entfernen wie nach dem Import aus Illustrator, Ergebnis nach `aus`
std::vector<std::string> hirch_ballast_probe(std::string const &ein, std::string const &aus)
{
    auto d = SPDocument::createNewDoc(ein.c_str(), false);
    if (!d) return {"nicht geladen"};
    std::vector<SPItem *> bereich;
    auto b = Inkscape::UI::Dialog::ballast_entfernen(d, bereich, true);
    sp_repr_save_file(d->getReprDoc(), aus.c_str(), SP_SVG_NS_URI);
    delete d;
    return b;
}
