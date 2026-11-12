// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Gravurtext und Schriften
 */
#include "ui/dialog/hirch-intern.h"
#include "ui/dialog/hirch-dateiwahl.h"
#include "actions/actions-tools.h"

namespace Inkscape::UI::Dialog {

SPItem *HirchFraesen::Impl::gravur_auswahl()
{
    auto desk = panel.getDesktop();
    if (!desk) return nullptr;
    auto sel = desk->getSelection();
    if (!sel || sel->size() != 1) return nullptr;
    for (SPObject *o = sel->singleItem(); o && !is<SPRoot>(o); o = o->parent) {
        if (o->getRepr() && o->getRepr()->attribute(GRAVUR)) return cast<SPItem>(o);
    }
    return nullptr;
}

void HirchFraesen::Impl::gravur_oder_text()
{
    // Den Gravurtext-Dialog gibt es nicht mehr (Peter, 21.10.): Text schreibt man mit dem Textwerkzeug.
    // Ein Gravurtext aus einer früheren Zeichnung wird einmal in einen normalen Text umgewandelt.
    auto desk = panel.getDesktop();
    if (!desk) return;
    if (auto g = gravur_auswahl()) {
        gravur_umwandeln(g);
        return;
    }
    set_active_tool(desk, "Text");
}

void HirchFraesen::Impl::gravur_umwandeln(SPItem *gruppe)
{
    auto desk = panel.getDesktop();
    auto d = doc();
    if (!desk || !d || !gruppe || !gruppe->parent) return;
    bj::object p;
    if (!json_lesen(gruppe->getRepr()->attribute(GRAVUR) ? gruppe->getRepr()->attribute(GRAVUR) : "", p)) return;
    double const px = MM_PX;
    // Strich (Farbe) und Lage aus dem Pfad in der Gruppe
    Inkscape::XML::Node *pfad = nullptr;
    for (auto c = gruppe->getRepr()->firstChild(); c; c = c->next()) {
        if (c->name() && std::string(c->name()) == "svg:path") {
            pfad = c;
            break;
        }
    }
    std::string farbe = "#ff8c00";
    Geom::Affine innen = Geom::identity();
    if (pfad) {
        if (auto it = cast<SPItem>(d->getObjectByRepr(pfad))) {
            innen = it->transform;
            if (it->style && it->style->stroke.isColor()) {
                char hex[8];
                g_snprintf(hex, sizeof hex, "#%06x", it->style->stroke.value.color.toRGBA32(1.0) >> 8);
                farbe = hex;
            }
        }
    }
    auto schrift = str(p, "schrift", "Hershey Sans");
    double hoehe = num(p, "versalhoehe", 10);
    auto ausr = str(p, "ausrichtung", "mitte");
    std::string anker = ausr == "links" ? "start" : ausr == "rechts" ? "end" : "middle";
    std::string buendig = ausr == "links" ? "start" : ausr == "rechts" ? "end" : "center";
    auto zahl_ = [](double v) {
        std::ostringstream o;
        o.imbue(std::locale::classic());
        o.precision(8);
        o << v;
        return o.str();
    };
    std::string stil = "font-family:'" + schrift + "';font-size:" + zahl_(hoehe * px) + "px;line-height:" +
                       zahl_(num(p, "zeilenabstand", 1.7)) + ";text-anchor:" + anker + ";text-align:" + buendig +
                       ";fill:none;stroke:" + farbe + ";stroke-width:" + zahl_(0.2 * px);
    auto xml = d->getReprDoc();
    auto text = xml->createElement("svg:text");
    text->setAttribute("style", stil);
    text->setAttribute("xml:space", "preserve");
    text->setAttribute("x", "0");
    text->setAttribute("y", "0");
    Geom::Affine t = innen * gruppe->transform;
    if (!t.isIdentity()) text->setAttribute("transform", sp_svg_transform_write(t));
    for (auto k : {"hirch:auch", "inkscape:label"}) {
        if (auto a = gruppe->getRepr()->attribute(k); a && std::string(k) == "hirch:auch") text->setAttribute(k, a);
    }
    auto inhalt = str(p, "text");
    std::size_t start = 0;
    bool erste = true;
    while (start <= inhalt.size()) {
        auto ende = inhalt.find('\n', start);
        auto zeile = inhalt.substr(start, ende == std::string::npos ? std::string::npos : ende - start);
        auto ts = xml->createElement("svg:tspan");
        ts->setAttribute("sodipodi:role", "line");
        ts->setAttribute("x", "0");
        if (erste) ts->setAttribute("y", "0");
        auto s_ = xml->createTextNode(zeile.c_str());
        ts->appendChild(s_);
        Inkscape::GC::release(s_);
        text->appendChild(ts);
        Inkscape::GC::release(ts);
        erste = false;
        if (ende == std::string::npos) break;
        start = ende + 1;
    }
    auto eltern = gruppe->parent->getRepr();
    eltern->addChild(text, gruppe->getRepr());
    Inkscape::GC::release(text);
    std::string id = gruppe->getRepr()->attribute("id") ? gruppe->getRepr()->attribute("id") : "";
    gruppe->deleteObject(true);
    if (!id.empty()) text->setAttribute("id", id);
    if (auto it = cast<SPItem>(d->getObjectByRepr(text))) desk->getSelection()->set(it);
    DocumentUndo::done(d, TR("Gravurtext in Text umwandeln"), "");
    int linien = (int)num(p, "linien", 1);
    if (linien > 1) {
        meldung(TR("Gravurtext umgewandelt"),
                TR("Der Text ist jetzt ein normaler Text in derselben Schrift. Die 2- oder 3-Linien-Gravur gibt es "
                   "dabei nicht mehr – für breite Schrift eine normale Schrift mit V-Fräser und Räumfräser nehmen."),
                Gtk::MESSAGE_INFO);
    }
}

bj::object HirchFraesen::Impl::gravur_werte(Gravur &g)
{
    bj::object p;
    p["text"] = g.text->get_buffer()->get_text().raw();
    p["schrift"] = g.schrift->get_active_id().empty() ? g.schrift_wunsch : g.schrift->get_active_id().raw();
    p["versalhoehe"] = wert(g.hoehe);
    p["ausrichtung"] = g.ausrichtung->get_active_id().raw();
    p["zeilenabstand"] = wert(g.zeilen);
    p["linien"] = std::atoi(g.linien->get_active_id().c_str());
    p["linienabstand"] = wert(g.linienabstand);
    p["optisch"] = g.abstand->get_active_id() == "optisch";
    p["luft"] = wert(g.luft);
    p["breite"] = wert(g.rille);
    p["bearbeitung"] = g.bearbeitung->get_active_id().raw();
    return p;
}

void HirchFraesen::Impl::gravur_rechnen(std::shared_ptr<Gravur> g)
{
    if (g->zu || g->baut) return;
    int nr = ++g->anfrage;
    g->ok->set_sensitive(false);
    python({"gravur", bj::serialize(gravur_werte(*g))},
           [this, g, nr](bool ok, std::string const &o, std::string const &e) {
               if (g->zu || nr != g->anfrage) return;
               g->erhalten = nr;
               bj::object r;
               if (!ok || !json_lesen(o, r)) {
                   g->info->set_text(TR("Fehler beim Setzen: ") + e.substr(0, 300));
                   return;
               }
               // Schriftliste nachführen
               if (auto l = liste_von(r, "schriften")) {
                   std::map<std::string, std::pair<std::string, bool>> neu;
                   for (auto const &v : *l) {
                       if (auto s = v.if_object()) neu[str(*s, "name")] = {str(*s, "art"), flag(*s, "eigen", false)};
                   }
                   if (neu != g->schriften) {
                       g->schriften = neu;
                       g->baut = true;
                       auto aktiv = g->schrift_wunsch.empty() ? g->schrift->get_active_id().raw() : g->schrift_wunsch;
                       g->schrift->remove_all();
                       for (auto const &[name, a] : neu) {
                           std::string t = name;
                           if (a.second) t += TR("  · eigene");
                           if (a.first == "umriss") t += TR("  · Umriss");
                           g->schrift->append(name, t);
                       }
                       g->schrift->set_active_id(aktiv);
                       if (g->schrift->get_active_row_number() < 0) g->schrift->set_active(0);
                       g->baut = false;
                   }
                   g->schrift_wunsch.clear();
               }
               auto sit = g->schriften.find(g->schrift->get_active_id().raw());
               g->entfernen->set_sensitive(sit != g->schriften.end() && sit->second.second);
               g->striche.clear();
               if (auto l = liste_von(r, "linien")) {
                   for (auto const &v : *l) {
                       std::vector<double> s;
                       if (auto a = v.if_array()) for (auto const &z : *a) s.push_back(z.to_number<double>());
                       if (s.size() >= 4) g->striche.push_back(std::move(s));
                   }
               }
               g->breite = num(r, "breite", 0);
               g->hoehe_mm = num(r, "hoehe", 0);
               g->box.clear();
               if (auto b = liste_von(r, "box")) for (auto const &z : *b) g->box.push_back(z.to_number<double>());
               std::string t;
               if (!flag(r, "ok", false)) {
                   t = TR("Fehler: ") + str(r, "fehler");
               } else if (g->striche.empty()) {
                   t = TR("Kein Text.");
               } else {
                   t = TR("Grösse ") + zahl(g->breite, 1) + " × " + zahl(g->hoehe_mm, 1) + " mm";
                   if (auto f = str(r, "fehlend"); !f.empty()) t += TR("  ·  ⚠ fehlt in der Schrift: ") + f;
                   if (str(r, "art") == "umriss") t += TR("  ·  Umrissschrift – eher mit V-Carve fräsen");
               }
               g->info->set_text(t);
               g->erhalten = nr;
               g->ok->set_sensitive(!g->striche.empty());
               g->bild->queue_draw();
           });
}

void HirchFraesen::Impl::schrift_importieren(Gtk::Window &eltern, std::shared_ptr<Gravur> g)
{
    schrift_import_lauf(eltern, [g](std::string const &t) { if (!g->zu) g->info->set_text(t); },
                        [this, g](std::string const &name) {
                            if (g->zu) return;
                            if (!name.empty()) g->schrift_wunsch = name;
                            gravur_rechnen(g);
                        });
}

void HirchFraesen::Impl::schrift_import_lauf(Gtk::Window &eltern, std::function<void(std::string const &)> info,
                                             std::function<void(std::string const &)> fertig)
{
    HirchDateiwahl dateiwahl(&eltern, TR("Schrift importieren"), Gtk::FILE_CHOOSER_ACTION_OPEN, TR("Abbrechen"),
                        TR("Importieren"));
    auto &fc = dateiwahl.fc();
    auto f = Gtk::FileFilter::create();
    f->set_name(TR("Schriften und Zeichentafeln (TTF, OTF, SVG, Hershey .jhf, DXF)"));
    for (auto m : {"*.ttf", "*.TTF", "*.otf", "*.OTF", "*.svg", "*.SVG", "*.jhf", "*.dxf",
                   "*.DXF"}) {
        f->add_pattern(m);
    }
    fc.add_filter(f);
    // «Alle Dateien»: weitere Formate liest ein Zusatz im Einstellungsordner, falls vorhanden (hirch_cam/zusatz.py)
    auto alle = Gtk::FileFilter::create();
    alle->set_name(TR("Alle Dateien"));
    alle->add_pattern("*");
    fc.add_filter(alle);
    fc.set_filter(f);
    // Lizenzhinweis (Sammelauftrag 21.10., Punkt 4)
    auto lizenz = label(TR("Bitte beachte die Lizenz der importierten Schrift. Manche Schriften dürfen nur mit der "
                           "Software des Herstellers verwendet werden."), "hirch-dim");
    lizenz->set_line_wrap(true);
    lizenz->set_max_width_chars(70);
    lizenz->set_xalign(0);
    dateiwahl.hinweis(*lizenz, lizenz->get_text());
    if (!dateiwahl.run()) return;
    std::string datei = fc.get_filename();
    dateiwahl.hide();
    auto endung = Glib::ustring(datei.substr(datei.find_last_of('.') + 1)).lowercase();
    std::string name;
    if (endung == "dxf" || endung == "svg") {
        Gtk::Dialog nd(TR("Name der Schrift"), eltern, true);
        auto box = nd.get_content_area();
        box->set_spacing(8);
        box->set_margin_start(16);
        box->set_margin_end(16);
        box->set_margin_top(12);
        auto l = label(TR("Ist die Datei eine Zeichentafel (und keine SVG-Schrift), bekommt die Schrift "
                       "diesen Namen:"));
        l->set_line_wrap(true);
        l->set_max_width_chars(48);
        box->add(*l);
        Gtk::Entry e;
        auto stamm = Glib::path_get_basename(datei);
        e.set_text(stamm.substr(0, stamm.find_last_of('.')));
        e.set_activates_default(true);
        box->add(e);
        nd.add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
        klasse(*nd.add_button(TR("Importieren"), Gtk::RESPONSE_OK), "suggested-action");
        nd.set_default_response(Gtk::RESPONSE_OK);
        nd.show_all();
        if (nd.run() != Gtk::RESPONSE_OK) return;
        name = e.get_text();
    }
    info(TR("Schrift wird eingelesen …"));
    python({"schrift", "import", datei, name}, [info, fertig](bool ok, std::string const &o, std::string const &e) {
        bj::object r;
        if (!ok || !json_lesen(o, r)) {
            info(TR("Import fehlgeschlagen: ") + e.substr(0, 300));
            return;
        }
        if (!flag(r, "ok", false)) {
            info(TR("Import fehlgeschlagen: ") + str(r, "fehler"));
            return;
        }
        std::string neu;
        if (auto s = unter(r, "schrift")) neu = str(*s, "name");
        // gleich auch im Textwerkzeug anbieten
        if (auto t = liste_von(r, "ttf"); t && !t->empty()) {
            for (auto const &v : *t) {
                if (v.is_string()) FontFactory::get().AddFontFile(std::string(v.get_string()).c_str());
            }
            Inkscape::FontLister::get_instance()->schriften_neu_laden();
        }
        info(neu.empty() ? std::string(TR("Schrift importiert.")) : TRF("Schrift «{}» importiert – im Textwerkzeug bei den Einlinienschriften.", {neu}));
        fertig(neu);
    });
}

void HirchFraesen::Impl::zeichentafel_zeigen(Gtk::Window &eltern)
{
    Gtk::Dialog d(TR("Zeichentafel"), eltern, true);
    auto box = d.get_content_area();
    box->set_spacing(10);
    box->set_margin_start(18);
    box->set_margin_end(18);
    box->set_margin_top(14);
    auto l = label("");
    l->set_markup(TR("So wird eine Schrift aus einem anderen Programm übernommen:\n\n"
                  "1.  Den Text unten kopieren und dort in der gewünschten Schrift setzen –\n"
                  "     <b>Zeichen mit Leerschlag</b>, <b>Zeilenabstand mindestens doppelte Höhe</b>.\n"
                  "2.  Die Vektoren als <b>DXF</b> (oder SVG) exportieren.\n"
                  "3.  Hier «Schrift importieren …» und die Datei wählen.\n\n"
                  "Das «H» am Zeilenanfang dient als Mass für Grundlinie und Höhe."));
    box->add(*l);
    auto tv = Gtk::make_managed<Gtk::TextView>();
    tv->set_editable(false);
    tv->set_monospace(true);
    tv->set_left_margin(10);
    tv->set_top_margin(8);
    tv->set_bottom_margin(8);
    klasse(*tv, "hirch-karte");
    tv->get_buffer()->set_text("…");
    box->add(*tv);
    python({"schrift", "tafel"}, [tv](bool ok, std::string const &o, std::string const &) {
        bj::object r;
        if (ok && json_lesen(o, r)) tv->get_buffer()->set_text(str(r, "tafel"));
    });
    auto kopieren = Gtk::make_managed<Gtk::Button>(TR("Text kopieren"));
    kopieren->set_halign(Gtk::ALIGN_START);
    kopieren->signal_clicked().connect([tv, kopieren] {
        Gtk::Clipboard::get()->set_text(tv->get_buffer()->get_text());
        kopieren->set_label(TR("Kopiert ✓"));
    });
    box->add(*kopieren);
    d.add_button(TR("Schliessen"), Gtk::RESPONSE_CLOSE);
    d.show_all();
    d.run();
}

void HirchFraesen::Impl::spannmittel_dialog()
{
    auto desk = panel.getDesktop();
    if (!desk) return;
    auto doc = desk->getDocument();
    auto sel = desk->getSelection();
    std::vector<SPItem *> auswahl(sel->items().begin(), sel->items().end());

    auto dlg = dialog(TR("Spannmittel einzeichnen"));
    auto box = dlg->get_content_area();
    auto info = label(TR("Spannmittel sind Flächen, die nicht befahren werden dürfen. Eilgänge führen "
                      "darüber hinweg, Fräsbahnen hinein meldet die Prüfung als Fehler. Die Form lässt sich "
                      "danach wie jedes Objekt verschieben, drehen und vervielfältigen."), "hirch-dim");
    info->set_line_wrap(true);
    info->set_max_width_chars(52);
    klasse(*info, "hirch-klein");
    box->add(*info);
    auto g = formular();
    int z = 0;
    Gtk::RadioButton::Group rg;
    auto neu = Gtk::make_managed<Gtk::RadioButton>(rg, TR("Neues Spannmittel"));
    auto aus = Gtk::make_managed<Gtk::RadioButton>(rg, TR("Ausgewählte Objekte als Spannmittel (") +
                                                        std::to_string(auswahl.size()) + ")");
    aus->set_sensitive(!auswahl.empty());
    (auswahl.empty() ? neu : aus)->set_active(true);
    g->attach(*neu, 0, z++, 3);
    auto form = Gtk::make_managed<Gtk::ComboBoxText>();
    form->append("pratze", TR("Spannpratze (Rechteck)"));
    form->append("rund", TR("Rund (Schraube, Zwinge)"));
    form->set_active_id("pratze");
    g->attach(*label(TR("Form")), 0, z);
    g->attach(*form, 1, z++, 2);
    auto sb = spin_zeile(*g, z++, TR("Breite / Durchmesser"), 40, 1, 500, 1, 1, "mm");
    auto sl = spin_zeile(*g, z++, TR("Länge"), 15, 1, 500, 1, 1, "mm");
    g->attach(*aus, 0, z++, 3);
    auto sh = spin_zeile(*g, z++, TR("Höhe über dem Material"), 15, 0, 200, 1, 1, "mm");
    // Umspannen (Peter 27.10., Entwurf B): die zweite Lage darf eingezeichnet werden, muss aber nicht
    g->attach(*label(TR("Gilt für")), 0, z);
    auto gilt = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
    klasse(*gilt, "linked");
    Gtk::RadioButton::Group sg;
    auto sp1 = Gtk::make_managed<Gtk::RadioButton>(sg, TR("Spannung 1 (jetzt)"));
    auto sp2 = Gtk::make_managed<Gtk::RadioButton>(sg, TR("Spannung 2 (nach dem Umspannen)"));
    for (auto r_ : {sp1, sp2}) {
        r_->set_mode(false);                  // als verbundene Knöpfe
        gilt->add(*r_);
    }
    {
        bool alle2 = !auswahl.empty();
        for (auto it : auswahl) {
            auto a2 = it->getRepr()->attribute("hirch:spannung");
            if (!(a2 && std::string(a2) == "2")) alle2 = false;
        }
        (alle2 ? sp2 : sp1)->set_active(true);
        // schon eingezeichnete Spannmittel gewählt: ihre Höhe vorgeben (umstellen auf Spannung 2 ändert sie nicht)
        if (!auswahl.empty()) {
            if (auto h0 = auswahl.front()->getRepr()->attribute("hirch:spannmittel")) sh->set_value(g_ascii_strtod(h0, nullptr));
        }
    }
    g->attach(*gilt, 1, z++, 2);
    auto gilt_info = label(TR("Spannung 2 nur einzeichnen, wenn du die Lage nach dem Umspannen kennst. Dann prüft "
                              "Hirch auch Spannung 2 und darf dort tiefer freifahren."), "hirch-dim");
    klasse(*gilt_info, "hirch-klein");
    gilt_info->set_line_wrap(true);
    gilt_info->set_max_width_chars(52);
    gilt_info->set_xalign(0);
    g->attach(*gilt_info, 0, z++, 3);
    box->add(*g);

    // Umspannen (Peter 27.10.): derselbe Abschnitt wie in «Auftrag einrichten» – eine Einstellung in der Zeichnung.
    // Sie gilt sofort (auch ohne «Einzeichnen», die Zonen erscheinen gleich); «Abbrechen» nimmt sie zurück.
    std::optional<bj::value> u_vorher;
    if (auto u = einst.if_contains("umspannen")) u_vorher = *u;
    bool u_geaendert = false;
    auto ub = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
    ub->set_margin_top(8);
    std::shared_ptr<UmspannenAbschnitt> ums;
    ums = umspannen_abschnitt(*ub, einst, doc, [this, &ums, &u_geaendert] {
        if (!ums) return;
        ums->speichern(einst);
        u_geaendert = true;
        schreiben(TR("Umspannen"));
    });
    box->add(*ub);
    auto sichtbar = [=] {
        bool n = neu->get_active();
        form->set_sensitive(n);
        sb->set_sensitive(n);
        sl->set_sensitive(n && form->get_active_id() == "pratze");
    };
    neu->signal_toggled().connect(sichtbar);
    form->signal_changed().connect(sichtbar);
    dlg->add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
    klasse(*dlg->add_button(TR("Einzeichnen"), Gtk::RESPONSE_OK), "suggested-action");
    dlg->set_default_response(Gtk::RESPONSE_OK);
    dlg->show_all();
    sichtbar();
    if (dlg->run() != Gtk::RESPONSE_OK) {
        if (u_geaendert) {
            if (u_vorher) einst["umspannen"] = *u_vorher;
            else einst.erase("umspannen");
            schreiben(TR("Umspannen"));
        }
        return;
    }
    ums.reset();

    auto hb_s = attr_zahl(wert(sh));
    char const *hb = hb_s.c_str();
    bool const zweite = sp2->get_active();
    // Spannung 2 blasser und strichpunktiert, damit man beide Lagen übereinander sieht
    std::string const stil = zweite ? "fill:#8c8c8c;fill-opacity:0.18;stroke:#5e5c64;stroke-width:1.2;"
                                      "stroke-dasharray:6,2,1.5,2"
                                    : "fill:#8c8c8c;fill-opacity:0.45;stroke:#4a4a4a;stroke-width:1.2;"
                                      "stroke-dasharray:4,2";
    std::string const titel = std::string(zweite ? TR("Spannmittel 2 · ") : TR("Spannmittel ")) + hb + " mm";
    if (aus->get_active()) {
        for (auto it : auswahl) {
            auto r = it->getRepr();
            r->setAttribute("hirch:spannmittel", hb);
            r->setAttribute("hirch:spannung", zweite ? "2" : nullptr);
            r->setAttribute("style", stil);
            r->setAttribute("inkscape:label", titel);
        }
        DocumentUndo::done(doc, TR("Spannmittel festlegen"), "");
        return;
    }
    // eigene Ebene «Spannmittel» (oben), damit sie sich sperren oder ausblenden lassen
    auto xml = doc->getReprDoc();
    auto root = doc->getReprRoot();
    Inkscape::XML::Node *ebene = nullptr;
    for (auto c = root->firstChild(); c; c = c->next()) {
        auto lab = c->attribute("inkscape:label");
        auto gm = c->attribute("inkscape:groupmode");
        if (lab && gm && (std::string(lab) == "Spannmittel" || std::string(lab) == TR("Spannmittel")) && std::string(gm) == "layer") ebene = c;
    }
    if (!ebene) {
        ebene = xml->createElement("svg:g");
        ebene->setAttribute("inkscape:groupmode", "layer");
        ebene->setAttribute("inkscape:label", TR("Spannmittel"));
        ebene->setAttribute("id", "ebene-spannmittel");
        root->appendChild(ebene);
        Inkscape::GC::release(ebene);
    }
    double const px = MM_PX;
    double b = wert(sb) * px, l = wert(sl) * px;
    std::ostringstream d;
    d.imbue(std::locale::classic());
    if (form->get_active_id() == "rund") {
        double r = b / 2;
        d << "M " << -r << ",0 A " << r << "," << r << " 0 1 0 " << r << ",0 A " << r << "," << r << " 0 1 0 "
          << -r << ",0 Z";
    } else {
        d << "M " << -b / 2 << "," << -l / 2 << " h " << b << " v " << l << " h " << -b << " Z";
    }
    auto pfad = xml->createElement("svg:path");
    pfad->setAttribute("d", d.str());
    pfad->setAttribute("style", stil);
    pfad->setAttribute("hirch:spannmittel", hb);
    if (zweite) pfad->setAttribute("hirch:spannung", "2");
    pfad->setAttribute("inkscape:label", titel);
    ebene->appendChild(pfad);
    Inkscape::GC::release(pfad);
    if (auto item = cast<SPItem>(doc->getObjectByRepr(pfad))) {
        Geom::Affine eltern = Geom::identity();
        if (auto e = cast<SPItem>(item->parent)) eltern = e->i2doc_affine();
        auto mitte = desk->get_display_area().midpoint() * desk->dt2doc();
        item->set_item_transform(Geom::Translate(mitte) * eltern.inverse());
        item->doWriteTransform(item->transform, nullptr, false);
        sel->set(item);
    }
    DocumentUndo::done(doc, TR("Spannmittel einzeichnen"), "");
}

} // namespace Inkscape::UI::Dialog
