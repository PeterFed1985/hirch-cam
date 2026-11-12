// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: 3D – Dialog «3D-Modell einlesen» (aus hirch-3d.cpp, siehe hirch-3d-intern.h)
 */
#include "ui/dialog/hirch-3d-intern.h"
#include "ui/dialog/hirch-dateiwahl.h"

namespace Inkscape::UI::Dialog {
namespace hirch3d {

Einlesen::Einlesen(Gtk::Window &eltern, SPDesktop *d)
    : Gtk::Dialog(TR("3D-Modell einlesen"), eltern, true)
    , desk(d)
    , mat(material(d ? d->getDocument() : nullptr))
{
    css_einrichten();
    set_default_size(900, 620);
    add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
    ok = add_button(TR("Einlesen"), Gtk::RESPONSE_OK);
    ok->get_style_context()->add_class("suggested-action");
    ok->set_sensitive(false);
    set_default_response(Gtk::RESPONSE_OK);

    auto haupt = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 0);
    auto links = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 10);
    klasse(*links, "hirch-panel");
    links->set_margin_start(8);
    links->set_margin_end(12);
    links->set_hexpand(true);

    links->add(*abschnitt(TR("Datei")));
    auto zp = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    pfad.set_hexpand(true);
    pfad.set_placeholder_text(TR("STL, OBJ, STEP oder Graustufenbild wählen"));
    zp->add(pfad);
    auto such = Gtk::make_managed<Gtk::Button>(TR("Durchsuchen …"));
    such->signal_clicked().connect([this] { durchsuchen(); });
    zp->add(*such);
    links->add(*zp);
    auto formate = label(TR("STL, OBJ, STEP (.step/.stp) oder Graustufenbild (PNG, JPG, TIFF; 8 oder 16 Bit)"),
                         "hirch-dim");
    klasse(*formate, "hirch-klein");
    formate->set_line_wrap(true);
    links->add(*formate);
    erkannt.set_xalign(0);
    erkannt.set_line_wrap(true);
    klasse(erkannt, "hirch-karte");
    erkannt.set_no_show_all(true);
    links->add(erkannt);
    auto gesp = label(TR("Das Modell wird in der Zeichnung gespeichert – sie braucht die Datei danach nicht mehr."),
                      "hirch-dim");
    klasse(*gesp, "hirch-klein");
    gesp->set_line_wrap(true);
    links->add(*gesp);

    // Bild
    bild_box.add(*abschnitt(TR("Bild")));
    auto gb = formular();
    auto lh = label(TR("Höhe kommt aus"));
    lh->set_hexpand(true);
    gb->attach(*lh, 0, 0);
    hell.append("hell", TR("Helligkeit: hell = hoch"));
    hell.append("dunkel", TR("Helligkeit: dunkel = hoch"));
    hell.set_active_id("hell");
    gb->attach(hell, 1, 0, 2, 1);
    weiss = spin_zeile(*gb, 1, TR("Höhe für Weiss"), 5, 0.1, 200, 0.5, 2, "mm");
    breite = spin_zeile(*gb, 2, TR("Breite des Bildes"), std::min(180.0, mat.b - 20), 1, 5000, 1, 1, "mm");
    glaetten = spin_zeile(*gb, 3, TR("Glätten"), 0, 0, 20, 0.1, 2, "mm");
    bild_box.add(*gb);
    auto hb = label(TR("Durchsichtige Stellen im Bild gelten als tiefste Stelle."), "hirch-dim");
    klasse(*hb, "hirch-klein");
    bild_box.add(*hb);
    bild_box.set_no_show_all(true);
    links->add(bild_box);

    // Ausrichtung (Netze)
    netz_box.add(*abschnitt(TR("Ausrichtung")));
    auto ga = formular();
    auto lo = label(TR("Welche Seite zeigt nach oben"));
    lo->set_hexpand(true);
    ga->attach(*lo, 0, 0);
    for (auto s : SEITEN) oben.append(s, seite_text(s));
    oben.set_active_id("+Z");
    ga->attach(oben, 1, 0, 2, 1);
    neig_x = spin_zeile(*ga, 1, TR("Neigung um X"), 0, -90, 90, 0.5, 1, "°");
    neig_y = spin_zeile(*ga, 2, TR("Neigung um Y"), 0, -90, 90, 0.5, 1, "°");
    netz_box.add(*ga);
    auto hn = label(TR("Lässt sich nachher im Fenster «3D-Modell» jederzeit ändern."), "hirch-dim");
    klasse(*hn, "hirch-klein");
    netz_box.add(*hn);
    links->add(netz_box);

    links->add(*abschnitt(TR("Ablegen")));
    auto gl = formular();
    auto ll = label(TR("Höhe im Material"));
    ll->set_hexpand(true);
    gl->attach(*ll, 0, 0);
    lage.append("mittig", TR("mittig (für zweiseitig)"));
    lage.append("oben", TR("Oberkante bündig mit der Oberfläche"));
    lage.set_active_id("mittig");
    gl->attach(lage, 1, 0);
    links->add(*gl);
    auto hw = label(TR("Es kommt in die Mitte des Materials und lässt sich danach wie jedes Objekt verschieben, "
                       "drehen und skalieren."), "hirch-dim");
    klasse(*hw, "hirch-klein");
    hw->set_line_wrap(true);
    links->add(*hw);

    auto sw = Gtk::make_managed<Gtk::ScrolledWindow>();
    sw->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
    sw->add(*links);
    sw->set_hexpand(true);
    haupt->add(*sw);
    haupt->add(*Gtk::make_managed<Gtk::Separator>(Gtk::ORIENTATION_VERTICAL));

    auto rechts = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 10);
    klasse(*rechts, "hirch-panel");
    rechts->set_size_request(360, -1);
    rechts->set_margin_start(12);
    rechts->add(*abschnitt(TR("Vorschau")));
    vorschau.set_size_request(330, 300);
    rechts->add(vorschau);
    status.set_xalign(0);
    klasse(status, "hirch-dim");
    rechts->add(status);
    auto gz = formular();
    auto zeile = [&](int z, char const *t, Gtk::Label &w) {
        gz->attach(*label(TR(t), "hirch-dim"), 0, z);
        w.set_xalign(0);
        klasse(w, "hirch-wert");
        gz->attach(w, 1, z);
    };
    zeile(0, "Grösse", groesse);
    zeile(1, "Höhe", hoehe);
    zeile(2, "Material", material_l);
    zeile(3, "Rand oben / unten", rand);
    rechts->add(*gz);
    hinweis.set_xalign(0);
    hinweis.set_line_wrap(true);
    hinweis.set_max_width_chars(40);
    hinweis.set_no_show_all(true);
    rechts->add(hinweis);
    haupt->add(*rechts);
    get_content_area()->pack_start(*haupt, true, true);
    material_l.set_text(mm(mat.b) + " × " + mm(mat.h) + " × " + mm(mat.d, 2) + " mm");

    auto neu = [this] {
        if (setzt) return;
        verzoegert.disconnect();
        verzoegert = Glib::signal_timeout().connect([this] {
            lesen();
            return false;
        }, 350);
    };
    for (auto s : {neig_x, neig_y, weiss, breite, glaetten}) s->signal_value_changed().connect(neu);
    for (auto c : {&oben, &lage, &hell}) c->signal_changed().connect(neu);
    pfad.signal_activate().connect([this] { lesen(); });
    show_all_children();
    zeigen_fuer(std::string());
}

void Einlesen::durchsuchen()
{
    HirchDateiwahl dateiwahl(this, TR("3D-Modell einlesen"), Gtk::FILE_CHOOSER_ACTION_OPEN, TR("Abbrechen"), TR("Öffnen"));
    auto &fc = dateiwahl.fc();
    auto alle = Gtk::FileFilter::create();
    alle->set_name(TR("3D-Modelle und Graustufenbilder"));
    auto netze = Gtk::FileFilter::create();
    netze->set_name(TR("3D-Modelle (STL, OBJ, STEP)"));
    auto bilder = Gtk::FileFilter::create();
    bilder->set_name(TR("Graustufenbilder (PNG, JPG, TIFF)"));
    for (char const *e : {"stl", "obj", "step", "stp"}) {
        for (auto const &x : {std::string(e), Glib::ustring(e).uppercase().raw()}) {
            alle->add_pattern("*." + x);
            netze->add_pattern("*." + x);
        }
    }
    for (char const *e : {"png", "jpg", "jpeg", "tif", "tiff", "bmp"}) {
        for (auto const &x : {std::string(e), Glib::ustring(e).uppercase().raw()}) {
            alle->add_pattern("*." + x);
            bilder->add_pattern("*." + x);
        }
    }
    fc.add_filter(alle);
    fc.add_filter(netze);
    fc.add_filter(bilder);
    auto zuletzt = Inkscape::Preferences::get()->getString("/hirch/3d/ordner");
    if (!zuletzt.empty()) fc.set_current_folder(zuletzt);
    if (!dateiwahl.run()) return;
    auto p = fc.get_filename();
    Inkscape::Preferences::get()->setString("/hirch/3d/ordner", Glib::path_get_dirname(p));
    pfad.set_text(p);
    drehen_z = 0;
    lesen();
}

bj::object Einlesen::ausrichtung_json()
{
    bj::object a;
    a["oben"] = oben.get_active_id().raw();
    bj::array n;
    n.push_back(wert(neig_x));
    n.push_back(wert(neig_y));
    a["neigung"] = n;
    a["drehen_z"] = drehen_z;
    a["breite"] = wert(breite);
    a["hoehe"] = wert(weiss);
    a["glaetten"] = wert(glaetten);
    a["invertieren"] = hell.get_active_id() == "dunkel";
    return a;
}

void Einlesen::lesen()
{
    auto p = pfad.get_text().raw();
    zeigen_fuer(p);
    if (p.empty()) return;
    unsigned g = ++gen;
    fertig = false;
    ok->set_sensitive(false);
    status.set_text(TR("Wird gelesen …"));
    if (lauf) *lauf = false;
    std::vector<std::string> args = {"einbetten", p, "--ordner", ordner, "--material",
                                     attr_zahl(mat.b) + "," + attr_zahl(mat.h) + "," + attr_zahl(mat.d),
                                     "--ausrichtung", bj::serialize(ausrichtung_json()),
                                     "--lage", bj::serialize(lage_json())};
    lauf = kern(args, ordner, [this, g](bool okay, bj::object const &e, std::string const &fehler) {
        if (g != gen) return;
        antwort(okay && flag(e, "ok", false), e, fehler);
    });
}

void Einlesen::antwort(bool okay, bj::object const &e, std::string const &fehler)
{
    status.set_text("");
    if (!okay) {
        erg = {};
        auto t = warnungen(e);
        if (t.empty()) t = fehler.empty() ? TR("Die Datei liess sich nicht lesen.") : fehler;
        hinweis_setzen(hinweis, t, "hirch-fehler");
        vorschau.clear();
        erkannt.hide();
        for (auto l : {&groesse, &hoehe, &rand}) l->set_text("–");
        return;
    }
    erg = e;
    if (auto a = unter(erg, "ausrichtung")) drehen_z = num(*a, "drehen_z", drehen_z);
    std::string art = str(erg, "art");
    std::string t = art_name(art);
    if (art == "bild") {
        t += " · " + ganz(liste_zahl(erg, "bildpunkte", 0)) + " × " + ganz(liste_zahl(erg, "bildpunkte", 1)) +
             " " + TR("Bildpunkte") + " · " + ganz(num(erg, "bild_bits", 8)) + " Bit";
    } else if (art == "step") {
        t += " · " + TRF("{} Flächen, {} Kanten", {ganz(num(erg, "flaechen", 0)), ganz(num(erg, "kanten", 0))});
    } else {
        t += " · " + TRF("{} Dreiecke", {ganz(num(erg, "dreiecke", 0))});
    }
    t += "\n" + TRF("In der Zeichnung gespeichert: ca. {} MB", {zahl(num(erg, "daten_bytes", 0) / 1048576.0, 1)});
    erkannt.set_text(t);
    erkannt.set_no_show_all(false);
    erkannt.show();
    groesse.set_text(mm(liste_zahl(erg, "masse", 0)) + " × " + mm(liste_zahl(erg, "masse", 1)) + " mm");
    hoehe.set_text(mm(liste_zahl(erg, "masse", 2), 2) + " mm");
    rand.set_text(mm(num(erg, "rand_oben", 0), 2) + " / " + mm(num(erg, "rand_unten", 0), 2) + " mm");
    try {
        auto pb = Gdk::Pixbuf::create_from_file(datei(erg, "bild"), 330, 300, true);
        vorschau.set(pb);
    } catch (...) {
        vorschau.clear();
    }
    auto w = warnungen(erg), h = hinweise(erg);
    if (!w.empty()) hinweis_setzen(hinweis, w + (h.empty() ? "" : "\n" + h), "hirch-warn");
    else hinweis_setzen(hinweis, h, "hirch-karte");
    fertig = true;
    ok->set_sensitive(true);
}

} // namespace hirch3d
} // namespace Inkscape::UI::Dialog
