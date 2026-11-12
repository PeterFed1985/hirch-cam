// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: G-Code speichern, Vorlagen, Fassungen, Maschine
 */
#include "ui/dialog/hirch-intern.h"
#include "ui/dialog/hirch-dateiwahl.h"
#include "object/sp-page.h"
#include "object/sp-root.h"
#include "object/sp-text.h"
#include "object/sp-flowtext.h"
#include "object/sp-item-group.h"
#include "page-manager.h"
#include "xml/repr.h"
#include <gtkmm/radiobutton.h>

#include <gtkmm/filechooserbutton.h>

namespace Inkscape::UI::Dialog {

static std::string dokument_stamm(SPDocument *d)
{
    std::string name = "programm";
    if (d && d->getDocumentFilename()) {
        name = Glib::path_get_basename(d->getDocumentFilename());
    } else if (d && d->getDocumentName()) {
        name = d->getDocumentName();
    } else {
        return name;
    }
    auto p = name.rfind('.');
    if (p != std::string::npos && p > 0) name = name.substr(0, p);
    return name;
}

std::string HirchFraesen::Impl::vorgeschlagener_name()
{
    std::string name = dokument_stamm(doc());
    if (platte == 0) name += TR("_Vorrichtung");
    else if (platten > 1) name += std::string(TR("_Platte")) + std::to_string(platte);
    return name;
}

std::string HirchFraesen::Impl::kurz_stamm(std::string const &s, std::size_t max)
{
    std::string a;
    Glib::ustring u = Glib::ustring(s);
    for (auto c : u) {
        switch (c) {
            case U'ä': case U'Ä': a += "AE"; continue;
            case U'ö': case U'Ö': a += "OE"; continue;
            case U'ü': case U'Ü': a += "UE"; continue;
            case U'ß': a += "SS"; continue;
            default: break;
        }
        if (c < 128) {
            a += std::isalnum((int)c) ? (char)std::toupper((int)c) : '_';
            continue;
        }
        // é, à, ç … ohne Zeichen darauf
        gchar *t = g_str_to_ascii(Glib::ustring(1, c).c_str(), nullptr);
        for (char const *q = t; q && *q; ++q) {
            if (std::isalnum((unsigned char)*q)) a += (char)std::toupper((unsigned char)*q);
        }
        g_free(t);
    }
    std::string b;                                   // «__» zusammenfassen, _ am Rand weg
    for (char c : a) {
        if (c == '_' && (b.empty() || b.back() == '_')) continue;
        b += c;
    }
    if (max > 0 && b.size() > max) b.resize(max);
    while (!b.empty() && b.back() == '_') b.pop_back();
    if (b.empty()) b = std::string("PROGRAMM").substr(0, std::max<std::size_t>(1, max));
    return b;
}

int HirchFraesen::Impl::kurzname()
{ return (int)num(ergebnis, "kurzname", 0); }

std::string HirchFraesen::Impl::ma_wert_text(char const *k)
{
    if (auto m = unter(analyse, "maschine")) return str(*m, k, "");
    return {};
}

double HirchFraesen::Impl::ma_wert(char const *k, double d)
{
    if (auto m = unter(analyse, "maschine")) return num(*m, k, d);
    return d;
}

/// «_1» aus «<stamm_rechnung>_1.TAP» – was nach dem Stamm und vor der Endung steht
static std::string mitte_von(std::string const &base, std::string const &stamm_rechnung, std::string *endung = nullptr)
{
    std::string rest = base.size() >= stamm_rechnung.size() ? base.substr(stamm_rechnung.size()) : base;
    auto p = rest.rfind('.');
    if (endung) *endung = p == std::string::npos ? std::string() : rest.substr(p);
    return p == std::string::npos ? rest : rest.substr(0, p);
}

std::string HirchFraesen::Impl::ziel_vorschlag()
{
    int n = kurzname();
    if (n <= 0) return vorgeschlagener_name() + name_zusatz;
    std::size_t mitte = 0;
    if (auto dateien = liste_von(ergebnis, "dateien")) {
        for (auto const &v : *dateien) {
            mitte = std::max(mitte, mitte_von(Glib::path_get_basename(std::string(v.get_string())), stamm_rechnung).size());
        }
    }
    std::size_t platz = n > (int)mitte + 1 ? n - mitte : 1;
    if (luft_modus) return kurz_stamm("LUFT", platz);
    // Platte, Vorrichtung, Wiederaufnahme: kurzes Zeichen am Ende, damit nichts überschrieben wird
    std::string zusatz = !name_zusatz.empty() ? "W" : platte == 0 ? "V" : platten > 1 ? "P" + std::to_string(platte) : "";
    if (zusatz.size() >= platz) return kurz_stamm(zusatz, platz);
    return kurz_stamm(dokument_stamm(doc()), platz - zusatz.size()) + zusatz;
}

bool HirchFraesen::Impl::pruefliste_ok()
{
    if (ma_wert("pruefliste", 1) < 0.5) return true;
    Gtk::Dialog dlg(TR("Prüfliste"), true);
    if (auto f = fenster()) dlg.set_transient_for(*f);
    dlg.set_resizable(false);
    auto box = dlg.get_content_area();
    box->set_spacing(8);
    box->set_margin_start(16);
    box->set_margin_end(16);
    box->set_margin_top(12);
    box->set_margin_bottom(8);
    box->add(*label(luft_modus ? TR("Vor dem Luftfräsen an der Maschine:") : TR("Vor dem Start an der Maschine:"), "hirch-dim"));
    std::vector<std::string> punkte = {TR("Werkstück fest gespannt"), TR("Nullpunkt X/Y am Werkstück gesetzt")};
    // provisorischer Nullpunkt: X/Y am orangen Kreuz (mit Kamera-Versatz: mit der Kamera) – nicht an der Materialecke
    if (auto pn = unter(ergebnis, "nullpunkt_provisorisch"); pn && !pn->empty()) {
        bool kamera = false;
        for (auto const &[k, v] : *pn) {
            if (auto o = v.if_object(); o && liste_von(*o, "kamera")) kamera = true;
        }
        punkte[1] = kamera ? TR("Nullpunkt X/Y mit der Kamera am orangen Kreuz gesetzt")
                           : TR("Nullpunkt X/Y am orangen Kreuz gesetzt");
    }
    std::size_t n_prog = 0;
    if (auto pr = liste_von(ergebnis, "programme"); pr && !pr->empty()) {
        n_prog = pr->size();
        if (auto o = (*pr)[0].if_object()) {
            auto slot = str(*o, "werkzeug");
            punkte.push_back(slot.size() == 1 ? TRF("Fräser {} eingesetzt: {}", {slot, TR(str(*o, "name"))})
                                              : TRF("Fräser eingesetzt: {}", {TR(str(*o, "name"))}));
        }
    }
    punkte.push_back(TR("Z mit diesem Fräser angetastet"));
    punkte.push_back(TR("Absaugung oder Kühlung an"));
    std::vector<Gtk::CheckButton *> haken;
    for (auto const &t : punkte) {
        auto c = Gtk::make_managed<Gtk::CheckButton>(t);
        haken.push_back(c);
        box->add(*c);
    }
    if (n_prog > 1) {
        auto l = label(TR("Vor jeder weiteren Datei: Fräser wechseln und Z neu antasten – steht auch in der "
                          "Zusammenfassung."), "hirch-dim");
        l->set_line_wrap(true);
        l->set_max_width_chars(48);
        box->add(*l);
    }
    box->add(*Gtk::make_managed<Gtk::Separator>());
    auto immer = Gtk::make_managed<Gtk::CheckButton>(TR("Diese Liste vor jedem Speichern zeigen"));
    immer->set_active(true);
    immer->set_tooltip_text(TR("Wieder einschalten: Einstellungen → Maschine → Prüfliste vor dem Speichern"));
    box->add(*immer);
    dlg.add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
    auto weiter = dlg.add_button(TR("Weiter"), Gtk::RESPONSE_OK);
    klasse(*weiter, "suggested-action");
    auto pruefen = [haken, weiter] {
        bool alle = true;
        for (auto c : haken) alle = alle && c->get_active();
        weiter->set_sensitive(alle);
    };
    for (auto c : haken) c->signal_toggled().connect(pruefen);
    pruefen();
    dlg.show_all();
    bool ok = dlg.run() == Gtk::RESPONSE_OK;
    if (ok && !immer->get_active()) {
        if (auto m = unter(analyse, "maschine")) (*m)["pruefliste"] = 0.0;
        bj::object neu;
        neu["pruefliste"] = 0.0;
        python({"datenbank", "maschine", bj::serialize(neu)}, [](bool, std::string const &, std::string const &) {});
    }
    return ok;
}

void HirchFraesen::Impl::gespeichert_dialog(std::vector<std::string> const &fertig, std::string const &ordner_pfad,
                                            bool stick)
{
    rueckfrage_merken();            // beim nächsten Öffnen: «Wie lief …?» (nicht beim Luftfräsen)
    Gtk::Dialog dlg(luft_modus ? TR("Luftfräsen gespeichert") : TR("G-Code gespeichert"), true);
    if (auto f = fenster()) dlg.set_transient_for(*f);
    dlg.set_resizable(false);
    auto box = dlg.get_content_area();
    box->set_spacing(10);
    box->set_margin_start(16);
    box->set_margin_end(16);
    box->set_margin_top(12);
    box->set_margin_bottom(8);
    auto satz = label(stick ? TRF("Auf «{}» geschrieben:", {Glib::path_get_basename(ordner_pfad)})
                            : TRF("Gespeichert im Ordner «{}»:", {Glib::path_get_basename(ordner_pfad)}), "hirch-dim");
    satz->set_xalign(0);
    box->add(*satz);
    auto grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(6);
    grid->set_column_spacing(10);
    auto pr = liste_von(ergebnis, "programme");
    bool je_datei = pr && pr->size() == fertig.size();
    auto zeit = [](double min) {
        int sek = (int)std::lround(min * 60);
        char b[32];
        std::snprintf(b, sizeof b, "%d:%02d", sek / 60, sek % 60);
        return std::string(b);
    };
    for (std::size_t i = 0; i < fertig.size(); ++i) {
        auto name = label(fertig[i]);
        name->set_xalign(0);
        name->set_markup("<b><tt>" + Glib::Markup::escape_text(fertig[i]) + "</tt></b>");
        grid->attach(*name, 0, (int)i);
        if (je_datei) {
            if (auto o = (*pr)[i].if_object()) {
                auto slot = str(*o, "werkzeug");
                if (slot.size() == 1) {
                    auto sl = label(slot, "hirch-wz-schild");
                    bj::object const *wz = nullptr;
                    if (auto ws = unter(einst, "werkzeuge")) wz = unter(*ws, slot.c_str());
                    if (wz && flag(*wz, "nur_hier", false)) klasse(*sl, "hirch-wz-schild-nur");
                    sl->set_valign(Gtk::ALIGN_CENTER);
                    grid->attach(*sl, 1, (int)i);
                }
                auto wz = label(TR(str(*o, "name")));
                wz->set_xalign(0);
                grid->attach(*wz, 2, (int)i);
                grid->attach(*label(zeit(num(*o, "minuten", 0)), "hirch-dim"), 3, (int)i);
            }
        }
    }
    if (!je_datei && fertig.size() == 1) grid->attach(*label(zeit(num(ergebnis, "minuten", 0)), "hirch-dim"), 3, 0);
    box->add(*grid);
    if (je_datei && fertig.size() > 1) {
        auto l = label(TR("Je Werkzeug eine Datei: in dieser Reihenfolge fräsen – dazwischen Fräser wechseln, "
                          "Z antasten, nächste Datei starten."), "hirch-dim");
        l->set_line_wrap(true);
        l->set_max_width_chars(52);
        l->set_xalign(0);
        box->add(*l);
    }
    // Endprüfung der gespeicherten Dateien: Tiefe, Bereich, Eilgänge, Sicherheitshöhe
    if (auto fg = unter(ergebnis, "freigabe")) {
        auto st = str(*fg, "stufe");
        auto kopf = label(st == "ok" ? TR("✓ Endprüfung: freigegeben") : st == "fehler" ? TR("⛔ Endprüfung: nicht freigegeben")
                                                                                 : TR("⚠ Endprüfung: bitte ansehen"),
                          st == "ok" ? "hirch-wert" : st == "fehler" ? "hirch-fehler" : "hirch-warn");
        kopf->set_xalign(0);
        box->add(*kopf);
        if (auto pk = liste_von(*fg, "punkte")) {
            for (auto const &v : *pk) {
                auto o = v.if_object();
                if (!o) continue;
                auto ps = str(*o, "stufe");
                auto l = label((ps == "ok" ? "✓ " : ps == "fehler" ? "⛔ " : "⚠ ") + str(*o, "text"), "hirch-dim");
                l->set_line_wrap(true);
                l->set_max_width_chars(60);
                l->set_xalign(0);
                klasse(*l, "hirch-klein");
                box->add(*l);
            }
        }
    }
    if (luft_modus) {
        auto l = label(TRF("Alles so hoch, dass die tiefste Stelle {} mm über dem Werkstück bleibt.",
                           {zahl(num(ergebnis, "luft", 10), 1)}), "hirch-dim");
        l->set_xalign(0);
        box->add(*l);
    }
    dlg.add_button(TR("Zusammenfassung …"), 1);
    if (!stick) dlg.add_button(TR("Ordner öffnen"), 2);
    klasse(*dlg.add_button(TR("Fertig"), Gtk::RESPONSE_OK), "suggested-action");
    dlg.set_default_response(Gtk::RESPONSE_OK);
    dlg.show_all();
    int a = dlg.run();
    dlg.hide();
    if (a == 1) {
        zusammenfassung();
    } else if (a == 2) {
        try {
            ordner_zeigen(ordner_pfad);
        } catch (Glib::Error const &e) {
            meldung(TR("Ordner öffnen"), e.what().raw(), Gtk::MESSAGE_WARNING);
        }
    }
}

void HirchFraesen::Impl::luftfraesen(bool stick)
{
    if (rechnet || !ergebnis_aktuell || !flag(ergebnis, "ok", false)) {
        meldung(TR("Luftfräsen"), TR("Bitte warten, bis das Programm fertig gerechnet ist."), Gtk::MESSAGE_INFO);
        return;
    }
    double mm = ma_wert("luft", 10.0);
    auto svg = Glib::build_filename(ordner, "zeichnung.svg");
    status.set_text(TR("Rechnet das Luftfräsen …"));
    unsigned gen = generation;
    python({"rechnen", svg, "--ordner", Glib::build_filename(ordner, "luft"), "--name", stamm_rechnung,
            "--luft", zahl_text(mm), "--platte", std::to_string(platte)},
           [this, gen, stick](bool ok, std::string const &o, std::string const &e) {
               bj::object r;
               if (!ok || !json_lesen(o, r)) {
                   status.set_text("");
                   meldung(TR("Luftfräsen"), TR("Rechnen fehlgeschlagen: ") + (e.empty() ? o : e).substr(0, 600),
                           Gtk::MESSAGE_ERROR);
                   return;
               }
               if (!flag(r, "ok", false)) {
                   status.set_text("");
                   meldung(TR("Luftfräsen"), warnungen_text(r), Gtk::MESSAGE_WARNING);
                   return;
               }
               if (gen != generation) {
                   status.set_text(TR("Die Zeichnung wurde inzwischen geändert – bitte nochmals."));
                   return;
               }
               Glib::signal_idle().connect_once([this, r, stick] {
                   if (rechnet || !ergebnis_aktuell) {
                       status.set_text(TR("Die Zeichnung wird neu gerechnet – bitte nochmals."));
                       return;
                   }
                   auto alt = std::move(ergebnis);
                   ergebnis = r;
                   luft_modus = true;
                   name_zusatz = kurzname() > 0 ? std::string() : std::string(TR("_Luft"));
                   if (stick) auf_stick();
                   else speichern();
                   ergebnis = std::move(alt);
                   luft_modus = false;
                   name_zusatz.clear();
               });
           });
}

void HirchFraesen::Impl::speichern()
{
    if (rechnet || !ergebnis_aktuell || pausiert) {
        pausiert = false;
        speichern_wartet = true;
        status.set_text(TR("Rechnet noch – gespeichert wird gleich danach …"));
        if (!rechnet) {
            if (wartet_knopf) genau_noetig = true;      // 3D noch nicht gerechnet: gleich genau
            rechnen_jetzt();
        }
        return;
    }
    if (!flag(ergebnis, "ok", false)) {
        meldung(TR("Kein Programm zum Speichern"), warnungen_text(ergebnis), Gtk::MESSAGE_INFO);
        return;
    }
    if (flag(ergebnis, "entwurf", false)) {        // 3D-Entwurf: nie so ausgeben – erst genau rechnen
        genau_noetig = true;
        speichern_wartet = true;
        status.set_text(TR("Rechnet genau für die Ausgabe – gespeichert wird gleich danach …"));
        letzte_xml.clear();
        spaeter(0);
        return;
    }
    auto dateien = liste_von(ergebnis, "dateien");
    if (!dateien || dateien->empty()) return;
    if (!fehler_bestaetigt()) return;
    if (!pruefliste_ok()) return;
    auto erste = std::string((*dateien)[0].get_string());
    auto endung = erste.substr(erste.rfind('.'));

    HirchDateiwahl dateiwahl(fenster(), TR("G-Code speichern"), Gtk::FILE_CHOOSER_ACTION_SAVE, TR("Abbrechen"),
                        TR("Speichern"));
    auto &fc = dateiwahl.fc();
    fc.set_do_overwrite_confirmation(true);
    auto filter = Gtk::FileFilter::create();
    filter->set_name("G-Code (*" + endung + ")");
    filter->add_pattern("*" + endung);
    fc.add_filter(filter);
    auto prefs = Inkscape::Preferences::get();
    auto ordner_alt = prefs->getString("/dialogs/hirch-fraesen/ordner");
    // Zielordner der Maschine geht vor (PC an der Fräse, Netzlaufwerk)
    if (auto zo = ma_wert_text("zielordner"); !zo.empty() && Glib::file_test(zo, Glib::FILE_TEST_IS_DIR)) ordner_alt = zo;
    if (auto d = doc(); ordner_alt.empty() && d && d->getDocumentFilename()) {
        ordner_alt = Glib::path_get_dirname(d->getDocumentFilename());
    }
    if (!ordner_alt.empty()) fc.set_current_folder(ordner_alt);
    fc.set_current_name(ziel_vorschlag() + endung);
    if (kurzname() > 0) {
        auto mitte = mitte_von(Glib::path_get_basename(erste), stamm_rechnung);
        auto l = label(TRF("Kurze Namen für die Steuerung: höchstens {} Zeichen, nur A–Z, 0–9 und _ – zum Beispiel {}.",
                           {std::to_string(kurzname()), ziel_vorschlag() + mitte + endung}), "hirch-dim");
        l->show();
        dateiwahl.hinweis(*l, l->get_text());
    } else if (dateien->size() > 1) {
        auto l = label(TR("Pro Werkzeug eine Datei: Name_1_…, Name_2_… – in dieser Reihenfolge fräsen."),
                       "hirch-dim");
        l->show();
        dateiwahl.hinweis(*l, l->get_text());
    }
    if (!dateiwahl.run()) return;
    auto ziel = fc.get_filename();
    dateiwahl.hide();
    auto zordner = Glib::path_get_dirname(ziel);
    auto stamm = Glib::path_get_basename(ziel);
    if (auto p = stamm.rfind('.'); p != std::string::npos && p > 0) stamm = stamm.substr(0, p);
    prefs->setString("/dialogs/hirch-fraesen/ordner", zordner);
    // Peter 29.10. (Windows-Befund): das Fenster fragt nur nach dem gewählten Namen – die Dateien je Werkzeug
    // (NAME_1, NAME_2 …) heissen anders; gibt es eine davon schon, hier fragen
    {
        std::string schon;
        int n = 0;
        for (auto const &nm : ziel_namen(stamm)) {
            if (Glib::build_filename(zordner, nm) == ziel) continue;      // hat das Fenster schon gefragt
            if (Glib::file_test(Glib::build_filename(zordner, nm), Glib::FILE_TEST_EXISTS)) {
                schon += "• " + nm + "\n";
                ++n;
            }
        }
        if (n > 0) {
            Gtk::MessageDialog m(n == 1 ? TR("Diese Datei gibt es schon") : TR("Diese Dateien gibt es schon"), false,
                                 Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
            m.set_title("Hirch CAM");
            if (auto f = fenster()) m.set_transient_for(*f);
            m.set_secondary_text(TRF("{}\nIm Ordner «{}». Ersetzen?", {schon, Glib::path_get_basename(zordner)}));
            m.add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
            m.add_button(TR("Ersetzen"), Gtk::RESPONSE_OK);
            m.set_default_response(Gtk::RESPONSE_CANCEL);
            if (m.run() != Gtk::RESPONSE_OK) return;
        }
    }
    std::vector<std::string> fertig;
    if (!dateien_kopieren(zordner, stamm, fertig)) return;
    if (name_zusatz.empty() && !luft_modus) laufzeit_zaehlen();      // Wiederaufnahme: schon gezählt
    std::string s = TRF("Gespeichert im Ordner «{}»: ", {Glib::path_get_basename(zordner)});
    for (std::size_t i = 0; i < fertig.size(); ++i) s += (i ? ", " : "") + fertig[i];
    status.set_text(s);
    gespeichert_dialog(fertig, zordner, false);
}

bool HirchFraesen::Impl::fehler_bestaetigt()
{
    if (auto pr = liste_von(ergebnis, "pruefung")) {
        std::string texte;
        int n = 0;
        // Endprüfung der fertigen Dateien (freigabe.py): Fehler zählen mit
        if (auto fg = unter(ergebnis, "freigabe")) {
            if (auto pk = liste_von(*fg, "punkte")) {
                for (auto const &v : *pk) {
                    if (auto o = v.if_object(); o && str(*o, "stufe") == "fehler") {
                        texte += std::string("• ") + TR("Endprüfung: ") + str(*o, "text") + "\n";
                        ++n;
                    }
                }
            }
        }
        bool umspannen = false;            // Spannmittel im Weg, Umspannen aus: gleich anbieten (Peter 09.11.)
        for (auto const &v : *pr) {
            if (auto o = v.if_object(); o && str(*o, "stufe") == "fehler") {
                texte += "• " + str(*o, "text") + "\n";
                ++n;
                umspannen = umspannen || str(*o, "aktion") == "umspannen";
            }
        }
        if (n > 0) {
            Gtk::MessageDialog m(n == 1 ? TR("Die Prüfung hat einen Fehler gefunden")
                                        : TR("Die Prüfung hat ") + std::to_string(n) + TR(" Fehler gefunden"),
                                 false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_NONE, true);
            m.set_title("Hirch CAM");
            if (auto f = fenster()) m.set_transient_for(*f);
            if (umspannen)
                texte += "\n" + std::string(TR("Spannmittel im Weg: Mit «Umspannen einschalten» fräst Spannung 1 alles "
                                                "ausser einem Rand um die Spannmittel. Nach dem Umspannen fräst Spannung 2 "
                                                "den Rest.")) + "\n";
            m.set_secondary_text(texte + TR("\nTrotzdem speichern?"));
            m.add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
            m.add_button(TR("Trotzdem speichern"), Gtk::RESPONSE_OK);
            int const UMSPANNEN = 1;
            if (umspannen) klasse(*m.add_button(TR("Umspannen einschalten"), UMSPANNEN), "suggested-action");
            m.set_default_response(umspannen ? UMSPANNEN : (int)Gtk::RESPONSE_CANCEL);
            int antwort = m.run();
            if (antwort == UMSPANNEN) {
                m.hide();
                Glib::signal_idle().connect_once([this] { umspannen_einschalten(); });
                return false;               // erst neu rechnen, dann speichert man mit Umspannen
            }
            if (antwort != Gtk::RESPONSE_OK) return false;
        }
    }
    return true;
}

static gint64 aenderungszeit(std::string const &pfad)
{
    GStatBuf st;
    return g_stat(pfad.c_str(), &st) == 0 ? (gint64)st.st_mtime : -1;
}

/// Name einer gerechneten Datei, wie sie gespeichert wurde – sonst der Name der Rechnung
std::string HirchFraesen::Impl::anzeige_name(std::string const &datei) const
{
    // auch nur der Name der Rechnung («rand_1.TAP», aus dem Ablauf): den vollen Pfad unter den Dateien suchen
    std::string quelle = datei;
    if (Glib::path_get_basename(datei) == datei) {
        if (auto ds = liste_von(ergebnis, "dateien")) {
            for (auto const &v : *ds) {
                if (v.is_string() && Glib::path_get_basename(std::string(v.get_string())) == datei) quelle = v.get_string();
            }
        }
    }
    if (auto it = gespeichert_als.find(quelle);
        it != gespeichert_als.end() && it->second.second == aenderungszeit(quelle))
        return it->second.first;
    return Glib::path_get_basename(quelle);
}

/// B-32 (Peter 04.11.): Name einer Datei so, wie ihn die Steuerung zeigt – gespeichert: der gespeicherte Name, sonst
/// der, den «G-Code speichern» vorschlägt; bei kurzen Namen (Steuerung zeigt höchstens n Zeichen) in Grossbuchstaben
std::string HirchFraesen::Impl::steuerung_name(std::string const &datei)
{
    std::string name;
    auto ds = liste_von(ergebnis, "dateien");
    auto basis = Glib::path_get_basename(datei);
    for (std::size_t i = 0; ds && i < ds->size(); ++i) {
        if (!(*ds)[i].is_string()) continue;
        std::string quelle((*ds)[i].get_string());
        if (Glib::path_get_basename(quelle) != basis) continue;
        if (auto it = gespeichert_als.find(quelle);
            it != gespeichert_als.end() && it->second.second == aenderungszeit(quelle)) {
            name = it->second.first;                      // so gespeichert
        } else {
            auto namen = ziel_namen(ziel_vorschlag());    // so schlägt «G-Code speichern» vor
            if (i < namen.size()) name = namen[i];
        }
    }
    if (name.empty()) name = anzeige_name(datei);
    if (kurzname() > 0) name = Glib::ustring(name).uppercase().raw();
    return name;
}

/// Namen, unter denen die gerechneten Dateien mit dem gewählten Stamm gespeichert werden
std::vector<std::string> HirchFraesen::Impl::ziel_namen(std::string const &stamm)
{
    std::vector<std::string> aus;
    auto dateien = liste_von(ergebnis, "dateien");
    if (!dateien) return aus;
    for (auto const &v : *dateien) {
        auto base = Glib::path_get_basename(std::string(v.get_string()));
        if (int n = kurzname(); n > 0) {
            // "<Zeichnung>_1.TAP" -> "SCHILD_1.TAP": höchstens n Zeichen vor der Endung
            std::string endung;
            auto mitte = mitte_von(base, stamm_rechnung, &endung);
            std::size_t platz = n > (int)mitte.size() + 1 ? n - mitte.size() : 1;
            aus.push_back(kurz_stamm(stamm, platz) + mitte + endung);
        } else {
            // "<Zeichnung>_1_V60.tap" -> "<gewählter Name>_1_V60.tap"
            std::string rest = base.size() >= stamm_rechnung.size() ? base.substr(stamm_rechnung.size()) : base;
            aus.push_back(stamm + rest);
        }
    }
    return aus;
}

bool HirchFraesen::Impl::dateien_kopieren(std::string const &zordner, std::string const &stamm, std::vector<std::string> &fertig)
{
    auto dateien = liste_von(ergebnis, "dateien");
    if (!dateien) return false;
    auto namen = ziel_namen(stamm);
    try {
        for (std::size_t i = 0; i < dateien->size() && i < namen.size(); ++i) {
            std::string quelle((*dateien)[i].get_string());
            auto const &neu = namen[i];
            auto dst = Glib::build_filename(zordner, neu);
            Gio::File::create_for_path(quelle)->copy(Gio::File::create_for_path(dst), Gio::FILE_COPY_OVERWRITE);
            fertig.push_back(neu);
            gespeichert_als[quelle] = {neu, aenderungszeit(quelle)};
        }
    } catch (Glib::Error const &e) {
        meldung(TR("Speichern fehlgeschlagen"), e.what().raw(), Gtk::MESSAGE_ERROR);
        return false;
    }
    return true;
}

std::string HirchFraesen::Impl::vorlagen_ordner()
{
    return Glib::build_filename(Glib::get_user_config_dir(), "hirch-cam", "vorlagen");
}

void HirchFraesen::Impl::als_vorlage()
{
    auto d = doc();
    if (!d) return;
    auto dlg = dialog(TR("Als Vorlage speichern"));
    auto box = dlg->get_content_area();
    auto l = label(TR("Name der Vorlage – z. B. «Jahresschild». Platzhalter im Text wie {JAHR}, {DATUM} "
                   "werden beim Öffnen aus der Vorlage ausgefüllt."), "hirch-dim");
    l->set_line_wrap(true);
    l->set_max_width_chars(50);
    box->add(*l);
    auto name = Gtk::make_managed<Gtk::Entry>();
    if (auto f = d->getDocumentFilename()) {
        auto st = Glib::path_get_basename(f);
        if (auto p = st.rfind('.'); p != std::string::npos) st = st.substr(0, p);
        name->set_text(st);
    }
    name->set_activates_default(true);
    box->add(*name);
    dlg->add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
    klasse(*dlg->add_button(TR("Speichern"), Gtk::RESPONSE_OK), "suggested-action");
    dlg->set_default_response(Gtk::RESPONSE_OK);
    dlg->show_all();
    if (dlg->run() != Gtk::RESPONSE_OK) return;
    std::string n = name->get_text().raw();
    for (auto &c : n) if (c == '/' || c == '\\') c = '-';
    if (n.empty()) return;
    g_mkdir_with_parents(vorlagen_ordner().c_str(), 0755);
    auto ziel = Glib::build_filename(vorlagen_ordner(), n + ".svg");
    try {
        auto xml = sp_repr_save_buf(d->getReprDoc());
        Glib::file_set_contents(ziel, xml.raw());
        status.set_text(TRF("Vorlage «{}» gespeichert.", {n}));
    } catch (Glib::Error const &e) {
        meldung(TR("Vorlage nicht gespeichert"), e.what().raw(), Gtk::MESSAGE_ERROR);
    }
}

// Peter 03.11.: «Gespiegelt speichern unter …» – eine gespiegelte Kopie der Zeichnung als Datei (zweiter Flügel).
// Gespiegelt wird je Seite um ihre Mitte, jede Form einzeln (auch in Gruppen): Linien, 3D-Modelle (Bild-Ecken),
// Stege, Spannmittel, Nullmarken. Text wird nur verschoben, bleibt lesbar. Freie Passlöcher werden mitgespiegelt.
// Der Nullpunkt bleibt an derselben Ecke; Bearbeitungen und Werkzeuge bleiben. Gerechnet wird nichts.
void HirchFraesen::Impl::gespiegelt_speichern()
{
    auto d = doc();
    if (!d) return;
    auto dlg = dialog(TR("Gespiegelt speichern"));
    auto box = dlg->get_content_area();
    auto l = label(TR("Speichert eine gespiegelte Kopie der Zeichnung – zum Beispiel für den zweiten Flügel. Die "
                      "offene Zeichnung bleibt, wie sie ist. Text bleibt lesbar, der Nullpunkt bleibt an derselben "
                      "Ecke."), "hirch-dim");
    l->set_line_wrap(true);
    l->set_max_width_chars(50);
    l->set_xalign(0);
    box->add(*l);
    Gtk::RadioButton::Group g;
    auto lr = Gtk::make_managed<Gtk::RadioButton>(g, TR("links ↔ rechts"));
    auto ou = Gtk::make_managed<Gtk::RadioButton>(g, TR("oben ↔ unten"));
    box->add(*lr);
    box->add(*ou);
    dlg->add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
    klasse(*dlg->add_button(TR("Weiter …"), Gtk::RESPONSE_OK), "suggested-action");
    dlg->set_default_response(Gtk::RESPONSE_OK);
    dlg->show_all();
    if (dlg->run() != Gtk::RESPONSE_OK) return;
    bool const um_x = lr->get_active();        // links ↔ rechts: x wird gespiegelt
    dlg->hide();

    std::string ordner_, stamm = "Zeichnung", endung = ".hir";
    if (auto f = d->getDocumentFilename()) {
        ordner_ = Glib::path_get_dirname(f);
        stamm = Glib::path_get_basename(f);
        if (auto p = stamm.rfind('.'); p != std::string::npos) {
            endung = stamm.substr(p);
            stamm = stamm.substr(0, p);
        }
    }
    HirchDateiwahl dateiwahl(fenster(), TR("Gespiegelt speichern"), Gtk::FILE_CHOOSER_ACTION_SAVE, TR("Abbrechen"),
                             TR("Speichern"));
    auto &fc = dateiwahl.fc();
    fc.set_do_overwrite_confirmation(true);
    if (!ordner_.empty()) fc.set_current_folder(ordner_);
    fc.set_current_name(stamm + TR("_gespiegelt") + endung);
    {   // Dateityp wie beim gewöhnlichen Speichern (Windows-Fenster zeigte ein leeres Feld, Abnahme 03.11.)
        auto filter = Gtk::FileFilter::create();
        filter->set_name(endung == ".hir" ? Glib::ustring(_("Hirch CAM drawing (*.hir)"))
                                          : Glib::ustring("*" + endung));
        filter->add_pattern("*" + endung);
        fc.add_filter(filter);
    }
    if (!dateiwahl.run()) return;
    std::string ziel = fc.get_filename();
    dateiwahl.hide();
    if (ziel.empty()) return;

    // Kopie im Speicher – die offene Zeichnung bleibt unberührt
    auto xml = sp_repr_save_buf(d->getReprDoc());
    std::unique_ptr<SPDocument> k(SPDocument::createNewDocFromMem(xml.c_str(), (int)xml.bytes(), false));
    if (!k) {
        meldung(TR("Nicht gespeichert"), TR("Die Zeichnung liess sich nicht kopieren."), Gtk::MESSAGE_ERROR);
        return;
    }
    k->ensureUpToDate();
    std::vector<Geom::Rect> seiten;
    for (auto page : k->getPageManager().getPages()) {
        if (page->getRepr() && page->getRepr()->attribute("hirch:vorrichtung")) continue;   // Spannvorrichtung
        seiten.push_back(page->getDocumentRect());
    }
    if (seiten.empty())
        seiten.push_back(Geom::Rect(0, 0, k->getWidth().value("px"), k->getHeight().value("px")));
    int n = 0;
    // Passlöcher: Lage aus den Einstellungen. In den Ecken / auf der Achse liegen sie ohnehin spiegelbildlich – die
    // Kreise bleiben, sonst hält das Panel sie für verschoben («frei platziert», Stern, Abnahme 03.11.). Frei
    // platziert: Kreise mitspiegeln und ihre gemerkte Lage (hirch:pass_mm) nachführen.
    bool pass_frei = false;
    if (auto t = k->getReprRoot()->attribute("hirch:einstellungen")) {
        try {
            auto w = bj::parse(t);
            if (auto o = w.if_object())
                if (auto z = o->if_contains("zweiseitig"); z && z->is_object())
                    pass_frei = str(z->as_object(), "pass_lage") == "frei";
        } catch (...) {
        }
    }
    std::function<void(SPItem *)> spiegeln = [&](SPItem *it) {
        if (!it) return;
        bool passloch = it->getRepr() && it->getRepr()->attribute("hirch:passloch");
        if (passloch && !pass_frei) return;
        auto grp = cast<SPGroup>(it);
        if (grp && !cast<SPText>(it)) {           // Ebenen und Gruppen: jede Form für sich (Text bleibt lesbar)
            for (auto c : grp->item_list()) spiegeln(c);
            return;
        }
        auto bb = it->documentVisualBounds();
        if (!bb) return;
        auto mitte = bb->midpoint();
        Geom::Rect const *seite = nullptr;
        for (auto const &r : seiten)
            if (r.contains(mitte)) { seite = &r; break; }
        if (!seite) return;                        // auf der Spannvorrichtung oder neben dem Material
        auto c = seite->midpoint();
        Geom::Affine m = um_x ? Geom::Affine(-1, 0, 0, 1, 2 * c.x(), 0) : Geom::Affine(1, 0, 0, -1, 0, 2 * c.y());
        Geom::Affine neu;
        if (cast<SPText>(it) || cast<SPFlowtext>(it))
            neu = it->i2doc_affine() * Geom::Translate(mitte * m - mitte);
        else
            neu = it->i2doc_affine() * m;
        auto eltern = cast<SPItem>(it->parent);
        Geom::Affine t = eltern ? neu * eltern->i2doc_affine().inverse() : neu;
        it->doWriteTransform(t, nullptr, false);
        if (passloch) {
            if (auto bb2 = it->documentGeometricBounds()) {
                std::ostringstream os;
                os.imbue(std::locale::classic());
                os.precision(10);
                os << bb2->midpoint().x() / MM_PX << "," << bb2->midpoint().y() / MM_PX;
                it->getRepr()->setAttribute("hirch:pass_mm", os.str());
            }
        }
        ++n;
    };
    for (auto &c : k->getRoot()->children)
        if (auto it = cast<SPItem>(&c)) spiegeln(it);
    // frei platzierte Passlöcher (mm, Seite 1, Nullpunkt unten links)
    auto root = k->getReprRoot();
    if (auto t = root->attribute("hirch:einstellungen")) {
        try {
            auto w = bj::parse(t);
            if (auto o = w.if_object()) {
                if (auto z = o->if_contains("zweiseitig"); z && z->is_object()) {
                    if (auto pp = z->as_object().if_contains("pass_punkte"); pp && pp->is_array()) {
                        double b = seiten.front().width() / MM_PX, h = seiten.front().height() / MM_PX;
                        for (auto &p : pp->as_array()) {
                            auto a = p.if_array();
                            if (!a || a->size() < 2) continue;
                            double x = (*a)[0].is_number() ? (*a)[0].to_number<double>() : 0;
                            double y = (*a)[1].is_number() ? (*a)[1].to_number<double>() : 0;
                            (*a)[0] = um_x ? b - x : x;
                            (*a)[1] = um_x ? y : h - y;
                        }
                        root->setAttribute("hirch:einstellungen", bj::serialize(w));
                    }
                }
            }
        } catch (...) {
        }
    }
    try {
        Glib::file_set_contents(ziel, sp_repr_save_buf(k->getReprDoc()).raw());
    } catch (Glib::Error const &e) {
        meldung(TR("Nicht gespeichert"), e.what().raw(), Gtk::MESSAGE_ERROR);
        return;
    }
    status.set_text(TRF("Gespiegelt gespeichert: {}", {Glib::path_get_basename(ziel)}));
    if (auto desk = panel.getDesktop())
        desk->messageStack()->flash(Inkscape::INFORMATION_MESSAGE,
                                    TRF("Gespiegelt gespeichert: {}", {Glib::path_get_basename(ziel)}));
    (void)n;
}

void HirchFraesen::Impl::aus_vorlage()
{
    std::vector<std::string> vorlagen;
    if (Glib::file_test(vorlagen_ordner(), Glib::FILE_TEST_IS_DIR)) {
        Glib::Dir dir(vorlagen_ordner());
        for (auto const &n : dir) if (Glib::ustring(n).lowercase().find(".svg") != Glib::ustring::npos)
            vorlagen.push_back(n);
    }
    std::sort(vorlagen.begin(), vorlagen.end());
    if (vorlagen.empty()) {
        meldung(TR("Noch keine Vorlagen"), TR("Eine Zeichnung über Datei › «Als Vorlage speichern …» ablegen."),
                Gtk::MESSAGE_INFO);
        return;
    }
    auto dlg = dialog(TR("Neu aus Vorlage"));
    auto box = dlg->get_content_area();
    auto lb = Gtk::make_managed<Gtk::ListBox>();
    for (auto const &n : vorlagen) {
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        auto t = n.substr(0, n.rfind('.'));
        auto l = label(t, "hirch-titel");
        l->set_margin_start(8);
        l->set_margin_top(6);
        l->set_margin_bottom(6);
        l->set_xalign(0);
        row->add(*l);
        row->set_name(n);
        lb->add(*row);
    }
    lb->set_activate_on_single_click(false);
    lb->signal_row_activated().connect([&dlg](Gtk::ListBoxRow *) { dlg->response(Gtk::RESPONSE_OK); });
    if (auto r = lb->get_row_at_index(0)) lb->select_row(*r);
    auto sw = Gtk::make_managed<Gtk::ScrolledWindow>();
    sw->set_min_content_height(240);
    sw->set_min_content_width(320);
    sw->add(*lb);
    klasse(*sw, "hirch-karte");
    box->add(*sw);
    auto loeschen = dlg->add_button(TR("Vorlage löschen"), 2);
    (void)loeschen;
    dlg->add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
    klasse(*dlg->add_button(TR("Öffnen"), Gtk::RESPONSE_OK), "suggested-action");
    dlg->set_default_response(Gtk::RESPONSE_OK);
    dlg->show_all();
    int antwort = dlg->run();
    auto row = lb->get_selected_row();
    if (!row) return;
    auto pfad = Glib::build_filename(vorlagen_ordner(), row->get_name().raw());
    if (antwort == 2) {
        try { Gio::File::create_for_path(pfad)->remove(); } catch (Glib::Error const &) {}
        return;
    }
    if (antwort != Gtk::RESPONSE_OK) return;
    dlg->hide();
    Glib::signal_idle().connect_once([pfad] {
        auto app = InkscapeApplication::instance();
        if (!app) return;
        // als neue, unbenannte Zeichnung (die Vorlage bleibt unverändert)
        if (auto d = app->document_new(pfad)) {
            platzhalter_fuellen(d);
            app->create_window(d, false);
        }
    });
}

void HirchFraesen::Impl::platzhalter_fuellen(SPDocument *d)
{
    auto jetzt = Glib::DateTime::create_now_local();
    std::vector<std::pair<std::string, std::string>> ersatz = {
        {"{JAHR}", jetzt.format("%Y").raw()}, {"{DATUM}", jetzt.format("%d.%m.%Y").raw()},
        {"{MONAT}", jetzt.format("%m").raw()}, {"{TAG}", jetzt.format("%d").raw()},
        // englisch zusätzlich (planung 06.11.)
        {"{YEAR}", jetzt.format("%Y").raw()}, {"{DATE}", jetzt.format("%Y-%m-%d").raw()},
        {"{MONTH}", jetzt.format("%m").raw()}, {"{DAY}", jetzt.format("%d").raw()}};
    std::function<void(Inkscape::XML::Node *)> gehen = [&](Inkscape::XML::Node *n) {
        for (auto c = n->firstChild(); c; c = c->next()) {
            if (c->type() == Inkscape::XML::NodeType::TEXT_NODE && c->content()) {
                std::string t = c->content();
                bool geaendert = false;
                for (auto const &[a, b] : ersatz) {
                    for (auto p = t.find(a); p != std::string::npos; p = t.find(a, p + b.size())) {
                        t.replace(p, a.size(), b);
                        geaendert = true;
                    }
                }
                if (geaendert) c->setContent(t.c_str());
            }
            gehen(c);
        }
    };
    gehen(d->getReprRoot());
    d->ensureUpToDate();
}

void HirchFraesen::Impl::versionen_dialog()
{
    auto d = doc();
    if (!d || !d->getDocumentFilename()) {
        meldung(TR("Noch nicht gespeichert"), TR("Frühere Fassungen entstehen, sobald die Zeichnung gespeichert wird."),
                Gtk::MESSAGE_INFO);
        return;
    }
    auto liste = ::versionen_liste(d->getDocumentFilename());
    if (liste.empty()) {
        meldung(TR("Keine früheren Fassungen"), TR("Jedes Speichern legt eine datierte Kopie im Unterordner «.versionen» "
                "an (die letzten 20). Bisher gibt es noch keine."), Gtk::MESSAGE_INFO);
        return;
    }
    auto dlg = dialog(TR("Frühere Fassung öffnen"));
    auto box = dlg->get_content_area();
    auto lb = Gtk::make_managed<Gtk::ListBox>();
    for (auto it = liste.rbegin(); it != liste.rend(); ++it) {
        auto n = Glib::path_get_basename(*it);
        // «Name 2026-10-15 01-23-45.svg» → «15.10.2026 01:23»
        std::string t = n;
        auto p = n.rfind(' ');
        if (p != std::string::npos && p >= 11) {
            std::string datum = n.substr(p - 10, 10);
            std::string uhr = n.substr(p + 1, 5);
            if (datum.size() == 10 && uhr.size() == 5)
                t = datum.substr(8, 2) + "." + datum.substr(5, 2) + "." + datum.substr(0, 4) + "  " +
                    uhr.substr(0, 2) + ":" + uhr.substr(3, 2);
        }
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        auto l = label(t, "hirch-mono");
        l->set_margin_start(8);
        l->set_margin_top(5);
        l->set_margin_bottom(5);
        l->set_xalign(0);
        row->add(*l);
        row->set_name(*it);
        lb->add(*row);
    }
    lb->set_activate_on_single_click(false);
    lb->signal_row_activated().connect([&dlg](Gtk::ListBoxRow *) { dlg->response(Gtk::RESPONSE_OK); });
    if (auto r = lb->get_row_at_index(0)) lb->select_row(*r);
    auto sw = Gtk::make_managed<Gtk::ScrolledWindow>();
    sw->set_min_content_height(280);
    sw->set_min_content_width(280);
    sw->add(*lb);
    klasse(*sw, "hirch-karte");
    box->add(*label(TR("Neueste zuerst. Geöffnet wird eine Kopie in einem neuen Fenster – die aktuelle "
                    "Zeichnung bleibt, wie sie ist."), "hirch-dim"));
    box->add(*sw);
    dlg->add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
    klasse(*dlg->add_button(TR("Öffnen"), Gtk::RESPONSE_OK), "suggested-action");
    dlg->set_default_response(Gtk::RESPONSE_OK);
    dlg->show_all();
    if (dlg->run() != Gtk::RESPONSE_OK) return;
    auto row = lb->get_selected_row();
    if (!row) return;
    auto pfad = row->get_name().raw();
    dlg->hide();
    Glib::signal_idle().connect_once([pfad] {
        if (auto app = InkscapeApplication::instance()) {
            if (auto d2 = app->document_new(pfad)) app->create_window(d2, false);
        }
    });
}

void HirchFraesen::Impl::maschine_abschnitt(Gtk::Box *ziel)
{
    UI::remove_all_children(*ziel);
    if (maschine_ziel != GTK_WIDGET(ziel->gobj())) {
        if (maschine_ziel) g_object_remove_weak_pointer(G_OBJECT(maschine_ziel), (gpointer *)&maschine_ziel);
        maschine_ziel = GTK_WIDGET(ziel->gobj());
        g_object_add_weak_pointer(G_OBJECT(maschine_ziel), (gpointer *)&maschine_ziel);
    }
    maschine_ohne_analyse = !hat_analyse;
    // B-25: oben die Maschinen als Karten; darunter die Werte der Maschine dieser Zeichnung
    auto karten = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
    ziel->add(*karten);
    maschinen_karten(karten);
    bj::object ma;
    if (auto m = unter(analyse, "maschine")) ma = *m;
    auto g4 = formular();
    auto mx = spin_zeile(*g4, 0, TR("Verfahrweg X"), num(ma, "x", 600), 10, 5000, 10, 0, "mm");
    auto my = spin_zeile(*g4, 1, TR("Verfahrweg Y"), num(ma, "y", 900), 10, 5000, 10, 0, "mm");
    auto mz = spin_zeile(*g4, 2, TR("Verfahrweg Z"), num(ma, "z", 150), 10, 1000, 5, 0, "mm");
    auto mn = spin_zeile(*g4, 3, TR("Drehzahl höchstens"), num(ma, "drehzahl_max", 24000), 1000, 60000, 500, 0, TR("U/min"));
    auto mm = spin_zeile(*g4, 4, TR("Drehzahl mindestens"), num(ma, "drehzahl_min", 6000), 0, 30000, 500, 0, TR("U/min"));
    auto mv = spin_zeile(*g4, 5, TR("Vorschub höchstens"), num(ma, "vorschub_max", 5000), 100, 30000, 100, 0, "mm/min");
    // Steuerung für alle Zeichnungen (kein Feld mehr im Panel oder beim Einrichten – Peter, 19.10.)
    // Postprozessoren-Liste mit Kennzeichnung (Peter 06.11.: Entwurf A und B «so bauen»): rechts in der Liste
    // «✓ an der Maschine geprüft» oder «ungeprüft», darunter der Satz zur gewählten Steuerung
    // Eintrag: Schlüssel, Name, an der Maschine geprüft, eigen/geändert
    struct PostEintrag { std::string id, name; bool geprueft = false, eigen = false; };
    auto eintraege = std::make_shared<std::vector<PostEintrag>>();
    bool vorgabe_geprueft = true;
    std::vector<PostEintrag> pliste;
    if (auto p = liste_von(analyse, "postprozessoren")) {
        for (auto const &e : *p) {
            auto const *a = e.if_array();
            if (!(a && a->size() >= 2 && (*a)[0].is_string() && (*a)[1].is_string())) continue;
            PostEintrag pe{std::string((*a)[0].get_string()), std::string((*a)[1].get_string())};
            if (a->size() >= 3)
                if (auto const *f = (*a)[2].if_object()) {
                    pe.geprueft = flag(*f, "geprueft", false);
                    pe.eigen = flag(*f, "eigen", false);
                }
            if (pe.id == "richauto_a11") vorgabe_geprueft = pe.geprueft;     // die Vorgabe des Programms
            pliste.push_back(pe);
        }
    }
    eintraege->push_back({"", TR("Vorgabe des Programms"), vorgabe_geprueft, false});
    eintraege->insert(eintraege->end(), pliste.begin(), pliste.end());
    auto gewaehlt = std::make_shared<std::size_t>(0);
    {
        auto gesucht = str(ma, "postprozessor", "");
        for (std::size_t i = 0; i < eintraege->size(); ++i)
            if ((*eintraege)[i].id == gesucht) *gewaehlt = i;
    }
    // Knopf mit dem Namen; aufgeklappt eine Liste mit der Kennzeichnung rechts
    auto mp = Gtk::make_managed<Gtk::MenuButton>();
    auto mp_innen = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 6);
    auto mp_name = Gtk::make_managed<Gtk::Label>();
    mp_name->set_xalign(0);
    mp_name->set_hexpand(true);
    mp_name->set_ellipsize(Pango::ELLIPSIZE_END);
    mp_innen->add(*mp_name);
    mp_innen->add(*Gtk::make_managed<Gtk::Image>("pan-down-symbolic", Gtk::ICON_SIZE_BUTTON));
    mp->add(*mp_innen);
    mp->set_hexpand(true);
    mp->set_tooltip_text(TR("Für diese Steuerung schreibt Hirch CAM den G-Code – in allen Zeichnungen."));
    auto mp_pop = Gtk::make_managed<Gtk::Popover>(*mp);
    auto mp_liste = Gtk::make_managed<Gtk::ListBox>();
    mp_liste->set_selection_mode(Gtk::SELECTION_BROWSE);
    for (std::size_t i = 0; i < eintraege->size(); ++i) {
        auto const &pe = (*eintraege)[i];
        auto zeile = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 24);
        zeile->set_border_width(4);
        auto n = Gtk::make_managed<Gtk::Label>(pe.name);
        n->set_xalign(0);
        n->set_hexpand(true);
        zeile->add(*n);
        zeile->add(*label(pe.geprueft ? TR("✓ an der Maschine geprüft") : TR("ungeprüft"),
                          pe.geprueft ? "hirch-ok" : "hirch-dim"));
        mp_liste->append(*zeile);
    }
    auto mp_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    mp_scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
    mp_scroll->set_propagate_natural_height(true);
    mp_scroll->set_max_content_height(560);
    mp_scroll->add(*mp_liste);
    mp_scroll->show_all();
    mp_pop->add(*mp_scroll);
    mp->set_popover(*mp_pop);
    auto mp_id = [eintraege, gewaehlt]() -> std::string { return (*eintraege)[*gewaehlt].id; };
    auto mp_box = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
    mp_box->add(*mp);
    auto mp_satz = label("", "hirch-ok");
    mp_satz->set_xalign(0);
    klasse(*mp_satz, "hirch-klein");
    auto mp_eigen = label(TR("Eigene und geänderte Postprozessoren gelten immer als ungeprüft."), "hirch-dim");
    mp_eigen->set_xalign(0);
    mp_eigen->set_line_wrap(true);
    mp_eigen->set_max_width_chars(48);
    klasse(*mp_eigen, "hirch-klein");
    mp_eigen->set_no_show_all(true);
    mp_box->add(*mp_satz);
    mp_box->add(*mp_eigen);
    auto mp_zeigen = [eintraege, gewaehlt, mp_name, mp_satz, mp_eigen, mp_liste] {
        auto const &pe = (*eintraege)[*gewaehlt];
        mp_name->set_text(pe.name);
        mp_satz->set_text(pe.geprueft ? TR("✓ An der Maschine geprüft.")
                                      : TR("⚠ Ungeprüft – den ersten Lauf in der Luft fahren."));
        auto sc = mp_satz->get_style_context();
        sc->remove_class(pe.geprueft ? "hirch-warntext" : "hirch-ok");
        sc->add_class(pe.geprueft ? "hirch-ok" : "hirch-warntext");
        mp_eigen->set_visible(pe.eigen);
        if (auto r = mp_liste->get_row_at_index((int)*gewaehlt)) mp_liste->select_row(*r);
    };
    mp_zeigen();
    mp_liste->signal_row_activated().connect([gewaehlt, mp_zeigen, mp_pop](Gtk::ListBoxRow *r) {
        if (!r) return;
        *gewaehlt = (std::size_t)r->get_index();
        mp_zeigen();
        mp_pop->popdown();
    });
    g4->attach(*label(TR("Steuerung (Postprozessor)")), 0, 6);
    g4->attach(*mp_box, 1, 6, 2);
    // Werkzeugwechsel: RichAuto fährt bei M6 zum Maschinen-Nullpunkt – daher meist eine Datei je Werkzeug
    auto mw = Gtk::make_managed<Gtk::ComboBoxText>();
    mw->append("", TR("wie die Steuerung"));
    mw->append("dateien", TR("eine Datei je Werkzeug"));
    mw->append("halt", TR("alle in einer Datei, Halt bei T…M6"));
    mw->set_active_id(str(ma, "werkzeugwechsel", ""));
    if (mw->get_active_row_number() < 0) mw->set_active(0);
    mw->set_tooltip_text(TR("Eine Datei je Werkzeug: Fräser wechseln, Z antasten, nächste Datei starten. "
                            "Halt in der Datei: die Steuerung hält bei jedem T…M6 an (fährt dabei zu ihrem "
                            "eigenen Wechselpunkt)."));
    g4->attach(*label(TR("Werkzeugwechsel")), 0, 7);
    g4->attach(*mw, 1, 7, 2);
    auto musb = Gtk::make_managed<Gtk::CheckButton>(TR("Dateien per USB-Stick zur Maschine"));
    musb->set_active(num(ma, "usb", 1) >= 0.5);
    musb->set_tooltip_text(TR("Zeigt unten den Knopf «Auf USB-Stick speichern»"));
    g4->attach(*musb, 1, 8, 2);
    auto mpl = Gtk::make_managed<Gtk::CheckButton>(TR("Prüfliste vor dem Speichern"));
    mpl->set_active(num(ma, "pruefliste", 1) >= 0.5);
    mpl->set_tooltip_text(TR("Spannung, Nullpunkt, Fräser, Z und Absaugung abhaken, bevor der G-Code gespeichert wird"));
    g4->attach(*mpl, 1, 9, 2);
    auto mlu = spin_zeile(*g4, 10, TR("Luftfräsen über dem Werkstück"), num(ma, "luft", 10), 1, 100, 1, 0, "mm");
    mlu->set_tooltip_text(TR("Beim Luftfräsen bleibt auch die tiefste Stelle so weit über dem Werkstück"));
    // Messkorrektur (Sammelauftrag 21.10., Punkt 3): je Fräser aus einem Testteil in X und Y. Die alte
    // Masskorrektur für alle Fräser steht nur noch da, solange sie nicht 0 ist.
    auto mko = spin_zeile(*g4, 11, TR("Alte Masskorrektur, alle Fräser"), num(ma, "masskorrektur", 0), -1, 1, 0.01, 2,
                          TR("mm je Seite"));
    mko->set_tooltip_text(TR("Gilt für alle Fräser. Besser: «Messkorrektur …» – sie misst je Fräser und trennt den "
                             "Fräser vom Weg der Achsen. 0 = aus."));
    auto mess = Gtk::make_managed<Gtk::Button>(TR("Messkorrektur …"));
    mess->set_tooltip_text(TR("Testteil (Quadrat und Loch) in X und Y messen: wie breit der Fräser schneidet und ob "
                              "eine Achse zu kurz oder zu weit fährt"));
    mess->set_halign(Gtk::ALIGN_START);
    g4->attach(*mess, 1, 12, 2);
    // Zielordner: steht der PC an der Fräse oder gibt es ein Netzlaufwerk, schaut «G-Code speichern» zuerst dort
    auto mzo = Gtk::make_managed<HirchOrdnerKnopf>(TR("Zielordner wählen"), TR("Abbrechen"), TR("Wählen"));
    if (auto z = str(ma, "zielordner", ""); !z.empty() && Glib::file_test(z, Glib::FILE_TEST_IS_DIR)) mzo->set_filename(z);
    mzo->set_tooltip_text(TR("Hier schlägt «G-Code speichern» zuerst vor (z. B. ein Ordner auf dem PC an der Fräse "
                             "oder ein Netzlaufwerk). Leer: der zuletzt benutzte Ordner."));
    g4->attach(*label(TR("Zielordner")), 0, 13);
    g4->attach(*mzo, 1, 13, 2);
    auto mzo_weg = Gtk::make_managed<Gtk::Button>(TR("leeren"));
    mzo_weg->set_halign(Gtk::ALIGN_START);
    mzo_weg->signal_clicked().connect([mzo] { mzo->unselect_all(); });
    g4->attach(*mzo_weg, 1, 14);
    mess->signal_clicked().connect([this, mko] {
        messkorrektur_dialog();
        if (auto m = unter(analyse, "maschine")) mko->set_value(num(*m, "masskorrektur", 0));
        zeile_zeigen(mko, std::abs(mko->get_value()) > 1e-9);
    });
    ziel->add(*g4);
    zeile_zeigen(mko, std::abs(mko->get_value()) > 1e-9);      // no_show_all: bleibt auch nach show_all verborgen
    // 3D: Masse von Spannmutter, Spindel und Halterung für die Kollisionsprüfung (hirch-3d.cpp)
    auto halter = Gtk::make_managed<Gtk::Button>(TR("Spindel und Halterung …"));
    halter->set_halign(Gtk::ALIGN_START);
    halter->set_tooltip_text(TR("Masse von Spannmutter, Spindel und Halterung – damit prüft Hirch CAM bei tiefen "
                                "3D-Teilen, ob etwas das Material streift"));
    halter->signal_clicked().connect([this] { halterung_dialog(); });
    ziel->add(*halter);
    // Peter 08.11.: Maschinenwerte sofort speichern wie bei den Werkstoffen – kein «Übernehmen», nichts geht verloren
    std::string fuer_maschine = maschine_werte_id;
    auto lebt = std::make_shared<GtkWidget *>(GTK_WIDGET(g4->gobj()));
    g_object_add_weak_pointer(G_OBJECT(*lebt), (gpointer *)lebt.get());
    auto zeit = std::make_shared<sigc::connection>();
    auto speichern = [=, this] {
        if (!*lebt) return;
        // die Felder gehören zu einer anderen Maschine als der jetzt gültigen: nicht schreiben, neu zeigen
        if (fuer_maschine != maschine_werte_id) {
            Glib::signal_idle().connect_once([this, ziel] { maschine_abschnitt(ziel); });
            return;
        }
        bj::object neu;
        neu["x"] = wert(mx); neu["y"] = wert(my); neu["z"] = wert(mz);
        neu["drehzahl_max"] = wert(mn); neu["drehzahl_min"] = wert(mm); neu["vorschub_max"] = wert(mv);
        neu["postprozessor"] = mp_id();
        neu["werkzeugwechsel"] = mw->get_active_id().raw();
        neu["usb"] = musb->get_active() ? 1.0 : 0.0;
        neu["pruefliste"] = mpl->get_active() ? 1.0 : 0.0;
        neu["luft"] = wert(mlu);
        neu["masskorrektur"] = wert(mko);
        neu["zielordner"] = mzo->get_filename();
        if (auto m = unter(analyse, "maschine")) {
            for (auto const &kv : neu) (*m)[kv.key()] = kv.value();
        }
        stick_knopf_auffrischen();
        python({"datenbank", "maschine", bj::serialize(neu)},
               [this](bool, std::string const &, std::string const &) {
                   letzte_xml.clear();   // neu rechnen (Prüfung der Verfahrwege)
                   spaeter(10);
               });
    };
    auto bald = [zeit, speichern] {
        zeit->disconnect();
        *zeit = Glib::signal_timeout().connect([speichern] { speichern(); return false; }, 600);
    };
    for (auto *sp : {mx, my, mz, mn, mm, mv, mlu, mko}) sp->signal_value_changed().connect(bald);
    mw->signal_changed().connect(bald);
    musb->signal_toggled().connect(bald);
    mpl->signal_toggled().connect(bald);
    mzo->signal_file_set().connect(bald);
    mzo_weg->signal_clicked().connect(bald);
    mp_liste->signal_row_activated().connect([bald](Gtk::ListBoxRow *) { bald(); });
    auto sofort = label(TR("Änderungen gelten sofort."), "hirch-dim");
    klasse(*sofort, "hirch-klein");
    sofort->set_xalign(0);
    ziel->add(*sofort);

    // Zwei PCs, ein Katalog (Peter 27.10.): «Meine Werkzeuge» mit einer Kopie in einem gewählten Ordner abgleichen
    // (werkzeugabgleich.py) – je Werkzeug gilt die neuere Änderung, der Zielordner bleibt auf jedem PC eigen
    ziel->add(*abschnitt(TR("Zwei PCs, ein Werkzeugkatalog")));
    bj::object ab;
    if (auto a = unter(analyse, "abgleich")) ab = *a;
    auto ab_an = Gtk::make_managed<Gtk::CheckButton>(TR("Werkzeugkatalog mit einem Ordner abgleichen"));
    ab_an->set_active(flag(ab, "an", false));
    ziel->add(*ab_an);
    auto ab_g = formular();
    ab_g->set_margin_start(24);
    auto ab_o = Gtk::make_managed<HirchOrdnerKnopf>(TR("Ordner wählen"), TR("Abbrechen"), TR("Wählen"));
    if (auto o = str(ab, "ordner", ""); !o.empty() && Glib::file_test(o, Glib::FILE_TEST_IS_DIR)) ab_o->set_filename(o);
    ab_o->set_hexpand(true);
    ab_g->attach(*label(TR("Ordner")), 0, 0);
    ab_g->attach(*ab_o, 1, 0, 2);
    auto ab_info = label(TR("Zum Beispiel ein Nextcloud-Ordner, den beide PCs sehen. Beim Start und nach jeder Änderung "
                            "gleicht Hirch CAM ab: je Werkzeug gilt die neuere Änderung, Gelöschtes bleibt gelöscht, "
                            "vorher wird gesichert. Der Zielordner bleibt auf jedem PC eigen."), "hirch-dim");
    klasse(*ab_info, "hirch-klein");
    ab_info->set_line_wrap(true);
    ab_info->set_max_width_chars(56);
    ab_info->set_xalign(0);
    ab_g->attach(*ab_info, 0, 1, 3);
    auto ab_stand = label("", "hirch-dim");
    klasse(*ab_stand, "hirch-klein");
    ab_stand->set_line_wrap(true);
    ab_stand->set_xalign(0);
    ab_g->attach(*ab_stand, 0, 2, 3);
    ziel->add(*ab_g);
    auto stand_zeigen = [ab_stand](bj::object const &e) {
        std::string m = str(e, "meldung", ""), t;
        if (!flag(e, "an", false)) {
            ab_stand->set_text("");
            return;
        }
        if (m == "Ordner nicht erreichbar") t = TR("Der Ordner ist nicht erreichbar – es gilt der Katalog dieses PCs.");
        else if (m == "nicht möglich") t = TR("Abgleich nicht möglich: ") + str(e, "fehler", "");
        else if (double z = num(e, "zuletzt", 0); z > 0) {
            auto dt = Glib::DateTime::create_now_local(static_cast<gint64>(z));
            t = TRF("Zuletzt abgeglichen: {}", {dt.format("%d.%m.%Y %H:%M").raw()});
            if (int n = (int)num(e, "anzahl", 0); m == "übernommen" && n > 0)
                t += " · " + anzahl(n, TR("Werkzeug vom anderen PC übernommen"), TR("Werkzeuge vom anderen PC übernommen"));
        }
        ab_stand->set_text(t);
    };
    stand_zeigen(ab);
    auto ab_sichtbar = [ab_an, ab_g] {
        ab_g->set_no_show_all(!ab_an->get_active());
        if (ab_an->get_active()) ab_g->show_all();
        else ab_g->hide();
    };
    auto ab_setzen = [this, ab_an, ab_o, ab_stand, stand_zeigen, ab_sichtbar] {
        ab_sichtbar();
        std::string ordner = ab_o->get_filename();
        if (ab_an->get_active() && ordner.empty()) {
            ab_stand->set_text(TR("Bitte einen Ordner wählen."));
            return;
        }
        bj::object a;
        a["an"] = ab_an->get_active();
        a["ordner"] = ordner;
        ab_stand->set_text(ab_an->get_active() ? TR("Gleiche ab …") : "");
        python({"datenbank", "abgleich", bj::serialize(a)},
               [this, stand_zeigen](bool ok, std::string const &aus, std::string const &) {
                   bj::object r;
                   if (ok && json_lesen(aus, r)) {
                       if (auto e = unter(r, "abgleich")) {
                           stand_zeigen(*e);
                           analyse["abgleich"] = *e;
                       }
                   }
                   letzte_xml.clear();   // Werkzeuge vom anderen PC: neu rechnen
                   spaeter(10);
               });
    };
    ab_an->signal_toggled().connect(ab_setzen);
    ab_o->signal_file_set().connect(ab_setzen);

    // Werkzeuge übertragen: eine Datei mit «Meine Werkzeuge», Maschinenwerten und eigenen Postprozessoren
    ziel->add(*abschnitt(TR("Werkzeuge übertragen")));
    auto ue = label(TR("Eine Datei mit deinen Fräsern, den Maschinenwerten und deinen eigenen Postprozessoren – "
                       "für den USB-Stick oder die Nextcloud."), "hirch-dim");
    ue->set_line_wrap(true);
    ue->set_max_width_chars(56);
    ziel->add(*ue);
    auto uk = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    auto ex = Gtk::make_managed<Gtk::Button>(TR("Exportieren …"));
    auto im = Gtk::make_managed<Gtk::Button>(TR("Importieren …"));
    uk->add(*ex);
    uk->add(*im);
    ziel->add(*uk);
    auto ui = label("", "hirch-dim");
    klasse(*ui, "hirch-klein");
    ui->set_line_wrap(true);
    ziel->add(*ui);
    ex->signal_clicked().connect([this, ui] { werkzeuge_exportieren(ui); });
    im->signal_clicked().connect([this, ui] { werkzeuge_importieren(ui); });
    ziel->show_all();
    ab_sichtbar();
    rad_schuetzen(*ziel);                  // Auswahllisten: Mausrad rollt die Seite
}

void HirchFraesen::Impl::werkzeuge_exportieren(Gtk::Label *info)
{
    HirchDateiwahl dateiwahl(fenster(), TR("Werkzeuge exportieren"), Gtk::FILE_CHOOSER_ACTION_SAVE, TR("Abbrechen"),
                        TR("Speichern"));
    auto &fc = dateiwahl.fc();
    fc.set_do_overwrite_confirmation(true);
    auto filter = Gtk::FileFilter::create();
    filter->set_name(TR("Hirch-Werkzeuge (*.json)"));
    filter->add_pattern("*.json");
    fc.add_filter(filter);
    auto prefs = Inkscape::Preferences::get();
    auto alt = prefs->getString("/dialogs/hirch-fraesen/werkzeuge-ordner");
    if (!alt.empty()) fc.set_current_folder(alt);
    fc.set_current_name("hirch-werkzeuge-" + Glib::DateTime::create_now_local().format("%Y-%m-%d").raw() + ".json");
    if (!dateiwahl.run()) return;
    std::string datei = fc.get_filename();
    dateiwahl.hide();
    if (datei.size() < 5 || datei.substr(datei.size() - 5) != ".json") datei += ".json";
    prefs->setString("/dialogs/hirch-fraesen/werkzeuge-ordner", Glib::path_get_dirname(datei));
    std::string aus, fehler;
    bj::object r;
    if (!python_jetzt({"datenbank", "export", datei}, aus, fehler) || !json_lesen(aus, r) || !flag(r, "ok", false)) {
        meldung(TR("Werkzeuge nicht exportiert"), (fehler.empty() ? aus : fehler).substr(0, 400), Gtk::MESSAGE_ERROR);
        return;
    }
    int np = (int)num(r, "postprozessoren", 0);
    std::string t = TR("Gespeichert: ") + Glib::path_get_basename(datei) + " · " +
                    anzahl((int)num(r, "werkzeuge", 0), TR("Fräser"), TR("Fräser")) + TR(" · Maschine") +
                    (np > 0 ? " · " + anzahl(np, TR("eigener Postprozessor"), TR("eigene Postprozessoren")) : "");
    if (info) info->set_text(t);
    else meldung(TR("Werkzeuge exportiert"), t, Gtk::MESSAGE_INFO);      // aus dem Menü
}

void HirchFraesen::Impl::werkzeuge_importieren(Gtk::Label *info)
{
    HirchDateiwahl dateiwahl(fenster(), TR("Werkzeuge importieren"), Gtk::FILE_CHOOSER_ACTION_OPEN, TR("Abbrechen"),
                        TR("Öffnen"));
    auto &fc = dateiwahl.fc();
    auto filter = Gtk::FileFilter::create();
    filter->set_name(TR("Hirch-Werkzeuge (*.json)"));
    filter->add_pattern("*.json");
    fc.add_filter(filter);
    auto prefs = Inkscape::Preferences::get();
    auto alt = prefs->getString("/dialogs/hirch-fraesen/werkzeuge-ordner");
    if (!alt.empty()) fc.set_current_folder(alt);
    if (!dateiwahl.run()) return;
    std::string datei = fc.get_filename();
    dateiwahl.hide();
    prefs->setString("/dialogs/hirch-fraesen/werkzeuge-ordner", Glib::path_get_dirname(datei));
    std::string aus, fehler;
    bj::object r;
    if (!python_jetzt({"datenbank", "import_vorschau", datei}, aus, fehler) || !json_lesen(aus, r) ||
        !flag(r, "ok", false) || !unter(r, "vorschau")) {
        std::string grund = json_lesen(aus, r) ? warnungen_text(r) : (fehler.empty() ? aus : fehler);
        meldung(TR("Werkzeuge nicht importiert"), grund.substr(0, 400), Gtk::MESSAGE_ERROR);
        return;
    }
    bj::object v = *unter(r, "vorschau");

    auto dlg = dialog(TR("Werkzeuge importieren"));
    auto box = dlg->get_content_area();
    auto kopf = label(TR("Importieren: ") + Glib::path_get_basename(datei), "hirch-titel");
    box->add(*kopf);
    auto zg = Gtk::make_managed<Gtk::Grid>();
    zg->set_column_spacing(10);
    zg->set_row_spacing(6);
    int zr = 0;
    auto zeile = [&](std::string const &zeichen, char const *k, std::string const &text) {
        auto z = label(zeichen, k);
        klasse(*z, "hirch-titel");
        zg->attach(*z, 0, zr);
        auto t = label(text);
        t->set_line_wrap(true);
        t->set_max_width_chars(60);
        zg->attach(*t, 1, zr++);
    };
    std::vector<std::string> neu_namen;
    if (auto l = liste_von(v, "neu")) {
        for (auto const &x : *l) if (x.is_string()) neu_namen.push_back(std::string(x.get_string()));
    }
    if (!neu_namen.empty()) {
        std::string t;
        for (std::size_t i = 0; i < neu_namen.size() && i < 3; ++i) t += (i ? ", " : "") + neu_namen[i];
        if (neu_namen.size() > 3) t += ", …";
        zeile("+", "hirch-ok", TRF("{} kommen dazu ({})", {anzahl((int)neu_namen.size(), TR("neuer Fräser"),
                                                                  TR("neue Fräser")), t}));
    }
    int gleich = (int)num(v, "gleich", 0);
    if (gleich > 0) zeile("=", "hirch-dim", TRF("{} gleich wie hier – bleiben, wie sie sind",
                                                {std::to_string(gleich)}));
    auto konf = liste_von(v, "konflikte");
    std::map<std::string, Gtk::RadioButton *> aus_datei;
    if (konf && !konf->empty()) {
        zeile("≠", "hirch-warntext", TRF("{} – wähle, welche gelten:",
                                         {anzahl((int)konf->size(), TR("Fräser mit gleichem Namen, anderen Werten"),
                                                 TR("mit gleichem Namen, anderen Werten"))}));
    }
    if (neu_namen.empty() && gleich == 0 && (!konf || konf->empty())) zeile("·", "hirch-dim", TR("Keine Fräser in der Datei."));
    box->add(*zg);
    if (konf && !konf->empty()) {
        auto kg = Gtk::make_managed<Gtk::Grid>();
        klasse(*kg, "hirch-karte");
        kg->set_column_spacing(18);
        kg->set_row_spacing(6);
        auto k1 = label(TR("Fräser"), "hirch-dim");
        auto k2 = label(TR("hier"), "hirch-dim");
        auto k3 = label(TR("aus der Datei"), "hirch-dim");
        for (auto *k : {k1, k2, k3}) klasse(*k, "hirch-klein");
        kg->attach(*k1, 0, 0);
        kg->attach(*k2, 1, 0);
        kg->attach(*k3, 2, 0);
        int r_ = 1;
        for (auto const &x : *konf) {
            auto o = x.if_object();
            if (!o) continue;
            kg->attach(*label(str(*o, "name")), 0, r_);
            auto h = Gtk::make_managed<Gtk::RadioButton>(str(*o, "hier"));
            auto d = Gtk::make_managed<Gtk::RadioButton>(str(*o, "datei"));
            d->join_group(*h);
            h->set_active(true);
            kg->attach(*h, 1, r_);
            kg->attach(*d, 2, r_);
            aus_datei[str(*o, "id")] = d;
            ++r_;
        }
        box->add(*kg);
    }
    Gtk::CheckButton *ma = nullptr;
    if (flag(v, "maschine", false)) {
        ma = Gtk::make_managed<Gtk::CheckButton>(TR("Maschinenwerte übernehmen (Verfahrwege, Drehzahl, Höchstvorschub)"));
        ma->set_active(true);
        box->add(*ma);
    }
    std::vector<std::pair<std::string, Gtk::CheckButton *>> posts;
    if (auto pl = liste_von(v, "postprozessoren")) {
        for (auto const &x : *pl) {
            auto o = x.if_object();
            if (!o || flag(*o, "gleich", false)) continue;
            auto c = Gtk::make_managed<Gtk::CheckButton>(
                flag(*o, "neu", true) ? TRF("Postprozessor «{}» übernehmen", {str(*o, "name")})
                                      : TRF("Postprozessor «{}» ersetzen", {str(*o, "name")}));
            c->set_active(true);
            box->add(*c);
            posts.emplace_back(str(*o, "schluessel"), c);
        }
    }
    auto fuss = label(TR("Gelöscht wird beim Import nichts. Vorher wird werkzeuge.json gesichert "
                         "(werkzeuge-vorher.json)."), "hirch-dim");
    klasse(*fuss, "hirch-klein");
    fuss->set_line_wrap(true);
    box->add(*fuss);
    dlg->add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
    klasse(*dlg->add_button(TR("Importieren"), Gtk::RESPONSE_OK), "suggested-action");
    dlg->set_default_response(Gtk::RESPONSE_OK);
    dlg->show_all();
    if (dlg->run() != Gtk::RESPONSE_OK) return;
    bj::object opt, wahl;
    for (auto const &[id, d] : aus_datei) wahl[id] = d->get_active() ? "datei" : "hier";
    opt["wahl"] = wahl;
    opt["maschine"] = ma && ma->get_active();
    bj::array pp;
    for (auto const &[k, c] : posts) if (c->get_active()) pp.push_back(bj::value(k));
    opt["postprozessoren"] = pp;
    dlg->hide();
    bj::object e2;
    if (!python_jetzt({"datenbank", "import", datei, bj::serialize(opt)}, aus, fehler) || !json_lesen(aus, e2) ||
        !flag(e2, "ok", false)) {
        meldung(TR("Werkzeuge nicht importiert"), (fehler.empty() ? aus : fehler).substr(0, 400), Gtk::MESSAGE_ERROR);
        return;
    }
    std::string t = TRF("Importiert: {} neu, {} ersetzt, {} Postprozessoren.",
                        {ganz(num(e2, "neu", 0)), ganz(num(e2, "ersetzt", 0)), ganz(num(e2, "postprozessoren", 0))});
    if (info) info->set_text(t);
    else meldung(TR("Werkzeuge importiert"), t, Gtk::MESSAGE_INFO);
    letzte_xml.clear();      // neu lesen: «Meine Werkzeuge», Maschine, Postprozessoren
    spaeter(10);
}

bool HirchFraesen::Impl::vorrichtung_an_einst()
{
    auto v = unter(einst, "vorrichtung");
    return v && flag(*v, "an", false) && vorrichtung_seite(doc());
}

void HirchFraesen::Impl::ab_hier_ausgeben(std::string const &ab_json, std::string const &wo)
{
    if (rechnet || !ergebnis_aktuell || !flag(ergebnis, "ok", false)) {
        meldung(TR("Ab hier neu ausgeben"), TR("Bitte warten, bis das Programm fertig gerechnet ist."),
                Gtk::MESSAGE_INFO);
        return;
    }
    // Sicherheit (3D 05.11.): Datei und Zeile aus dem Entwurf passen nicht zum genau gerechneten Programm – sonst
    // ginge es an der falschen Stelle weiter (Vorschau «Ab hier neu ausgeben …» und 3D-Zeitleiste)
    if (ab_json.find("\"zeile\"") != std::string::npos && flag(ergebnis, "entwurf", false)) {
        meldung(TR("Ab hier neu ausgeben"),
                TR("Die Ansicht zeigt den Entwurf. Ab einer Stelle geht es erst mit genau gerechneten Bahnen – "
                   "zuerst «G-Code speichern»."), Gtk::MESSAGE_INFO);
        return;
    }
    auto svg = Glib::build_filename(ordner, "zeichnung.svg");
    status.set_text(TRF("Rechnet ab «{}» …", {wo}));
    unsigned gen = generation;
    python({"rechnen", svg, "--ordner", Glib::build_filename(ordner, "ab"), "--name", stamm_rechnung,
            "--ab", ab_json, "--platte", std::to_string(platte)},
           [this, gen, wo](bool ok, std::string const &o, std::string const &e) {
               bj::object r;
               if (!ok || !json_lesen(o, r)) {
                   status.set_text("");
                   meldung(TR("Ab hier neu ausgeben"), TR("Rechnen fehlgeschlagen: ") + (e.empty() ? o : e).substr(0, 600),
                           Gtk::MESSAGE_ERROR);
                   return;
               }
               if (!flag(r, "ok", false)) {
                   status.set_text("");
                   meldung(TR("Ab hier neu ausgeben"), warnungen_text(r), Gtk::MESSAGE_WARNING);
                   return;
               }
               if (gen != generation) {
                   status.set_text(TR("Die Zeichnung wurde inzwischen geändert – bitte nochmals."));
                   return;
               }
               Glib::signal_idle().connect_once([this, r, wo] { ab_speichern(r, wo); });
           });
}

void HirchFraesen::Impl::ab_speichern(bj::object r, std::string const &wo)
{
    if (rechnet || !ergebnis_aktuell) {
        status.set_text(TR("Die Zeichnung wird neu gerechnet – bitte nochmals."));
        return;
    }
    double minuten = num(r, "minuten", 0), voll = num(ergebnis, "minuten", 0);
    auto s_ = sticks();
    Gtk::MessageDialog m(TRF("Programm ab «{}»", {wo}), false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
    m.set_title("Hirch CAM");
    if (auto f = fenster()) m.set_transient_for(*f);
    // B-27 (Peter 06.11.: «passt»)
    m.set_secondary_text(TRF("Ab hier: ca. {} min von {} min. Der Fräser fährt auf Sicherheitshöhe zur Stelle und "
                             "taucht dort ein.", {zahl(minuten, 1), zahl(voll, 1)}) + "\n\n" +
                         TR("Nullpunkt nicht neu setzen – derselbe wie vorher."));
    m.add_button(TR("Abbrechen"), Gtk::RESPONSE_CANCEL);
    if (!s_.empty() && ma_wert("usb", 1) >= 0.5) m.add_button(TRF("Auf «{}»", {s_.front()->get_name()}), 2);
    m.add_button(TR("Speichern unter …"), Gtk::RESPONSE_OK);
    m.set_default_response(Gtk::RESPONSE_OK);
    int antwort = m.run();
    m.hide();
    if (antwort != Gtk::RESPONSE_OK && antwort != 2) return;
    std::string kurz;
    for (unsigned char c : wo) {
        if (kurz.size() >= 24) break;
        kurz += (std::isalnum(c) || c >= 0x80) ? (char)c : '_';
    }
    auto alt = std::move(ergebnis);
    ergebnis = std::move(r);
    name_zusatz = std::string(TR("_ab_")) + kurz;
    if (antwort == 2) auf_stick();
    else speichern();
    ergebnis = std::move(alt);
    name_zusatz.clear();
}

std::vector<Glib::RefPtr<Gio::Mount>> HirchFraesen::Impl::sticks()
{
    std::vector<Glib::RefPtr<Gio::Mount>> aus;
    for (auto const &m : Gio::VolumeMonitor::get()->get_mounts()) {
        if (!m || m->is_shadowed()) continue;
        auto root = m->get_root();
        if (!root || !root->get_path().size()) continue;
        auto dr = m->get_drive();
        bool wechsel = m->can_eject() || m->can_unmount();
        if (dr) wechsel = wechsel && (dr->is_removable() || dr->is_media_removable() || dr->can_eject());
        if (wechsel) aus.push_back(m);
    }
    return aus;
}

void HirchFraesen::Impl::stick_knopf_auffrischen()
{
    auto s_ = sticks();
    bool fertig = ergebnis_aktuell && flag(ergebnis, "ok", false);
    // nur wer Dateien per USB-Stick zur Maschine bringt, sieht den Knopf (Maschinen-Einstellung)
    bool usb = ma_wert("usb", 1) >= 0.5;
    stick_btn.set_visible(usb);
    if (luft_stick_item) luft_stick_item->set_visible(usb && !s_.empty());
    stick_btn.set_sensitive(!s_.empty() && (fertig || rechnet || wartet_knopf || pausiert));
    stick_btn.set_label(s_.empty() ? TR("Auf USB-Stick speichern") : TRF("Auf «{}» speichern", {s_.front()->get_name()}));
    stick_btn.set_tooltip_text(s_.empty() ? TR("Kein USB-Stick eingesteckt")
                                          : TR("G-Code direkt auf den Stick schreiben, alte .tap-Dateien dort in den "
                                            "Ordner «alt» verschieben und den Stick sicher auswerfen"));
}

void HirchFraesen::Impl::auf_stick()
{
    if (!rechnet && (wartet_knopf || pausiert)) {   // noch nicht gerechnet: genau rechnen, dann auf den Stick
        genau_noetig = true;
        stick_wartet = true;
        status.set_text(TR("Rechnet genau für die Ausgabe – danach auf den Stick …"));
        rechnen_jetzt();
        return;
    }
    if (rechnet || !ergebnis_aktuell) {
        status.set_text(TR("Rechnet noch – bitte gleich nochmals."));
        return;
    }
    if (!flag(ergebnis, "ok", false)) return;
    if (flag(ergebnis, "entwurf", false)) {        // 3D-Entwurf: erst genau rechnen
        genau_noetig = true;
        stick_wartet = true;
        status.set_text(TR("Rechnet genau für die Ausgabe – danach auf den Stick …"));
        letzte_xml.clear();
        spaeter(0);
        return;
    }
    auto s_ = sticks();
    if (s_.empty()) {
        meldung(TR("Kein USB-Stick"), TR("Bitte den Stick einstecken."), Gtk::MESSAGE_INFO);
        return;
    }
    if (!fehler_bestaetigt()) return;
    if (!pruefliste_ok()) return;
    auto mount = s_.front();
    auto wurzel = mount->get_root()->get_path();
    // alte Programme auf dem Stick in «alt» verschieben (am Handgerät nicht verwechseln)
    int verschoben = 0;
    try {
        Glib::Dir dir(wurzel);
        std::vector<std::string> alte;
        for (auto const &n : dir) {
            auto k = Glib::ustring(n).lowercase().raw();
            if (k.size() > 4 && (k.compare(k.size() - 4, 4, ".tap") == 0 || k.compare(k.size() - 3, 3, ".nc") == 0))
                alte.push_back(n);
        }
        if (!alte.empty()) {
            auto alt = Glib::build_filename(wurzel, "alt");
            if (!Glib::file_test(alt, Glib::FILE_TEST_IS_DIR)) Gio::File::create_for_path(alt)->make_directory();
            for (auto const &n : alte) {
                Gio::File::create_for_path(Glib::build_filename(wurzel, n))
                    ->move(Gio::File::create_for_path(Glib::build_filename(alt, n)), Gio::FILE_COPY_OVERWRITE);
                ++verschoben;
            }
        }
    } catch (Glib::Error const &e) {
        meldung("USB-Stick", TR("Alte Dateien liessen sich nicht verschieben: ") + e.what().raw(), Gtk::MESSAGE_WARNING);
    }
    std::vector<std::string> fertig;
    if (!dateien_kopieren(wurzel, ziel_vorschlag(), fertig)) return;
    if (name_zusatz.empty() && !luft_modus) laufzeit_zaehlen();
    std::string name = mount->get_name();
    status.set_text(TRF("Auf «{}» gespeichert: {} Datei(en)", {name, std::to_string(fertig.size())}) +
                    (verschoben ? ", " + std::to_string(verschoben) + TR(" alte in «alt»") : std::string()) +
                    TR(" – wird ausgeworfen …"));
    auto op = Gio::MountOperation::create();
    mount->eject(op, [this, mount, name](Glib::RefPtr<Gio::AsyncResult> &r) {
        try {
            mount->eject_finish(r);
            status.set_text("«" + name + TR("» kann abgezogen werden."));
        } catch (Glib::Error const &e) {
            try {
                mount->unmount([this, mount, name](Glib::RefPtr<Gio::AsyncResult> &r2) {
                    try {
                        mount->unmount_finish(r2);
                        status.set_text("«" + name + TR("» kann abgezogen werden."));
                    } catch (Glib::Error const &e2) {
                        status.set_text(TR("Gespeichert – auswerfen ging nicht: ") + e2.what().raw());
                    }
                });
            } catch (...) {
                status.set_text(TR("Gespeichert – auswerfen ging nicht: ") + e.what().raw());
            }
        }
    });
    gespeichert_dialog(fertig, wurzel, true);
}

std::string HirchFraesen::Impl::laufzeit_schluessel(bj::object const &w)
{
    auto k = str(w, "db_id");
    if (k.empty()) k = werkzeug_name(w);
    std::string aus;
    for (unsigned char c : k) aus += std::isalnum(c) ? (char)c : '_';
    return aus;
}

void HirchFraesen::Impl::laufzeit_zaehlen()
{
    auto ab = unter(ergebnis, "ablauf");
    if (!ab) return;
    std::map<std::string, double> je;
    if (auto seiten = liste_von(*ab, "seiten"))
        for (auto const &sv : *seiten)
            if (auto so = sv.if_object())
                if (auto abs = liste_von(*so, "abschnitte"))
                    for (auto const &av : *abs)
                        if (auto a = av.if_object()) je[str(*a, "slot")] += num(*a, "minuten", 0);
    auto prefs = Inkscape::Preferences::get();
    for (auto const &[slot, m] : je) {
        auto w = werkzeug(slot);
        if (!w || m <= 0) continue;
        auto pfad = "/hirch/laufzeit/" + laufzeit_schluessel(*w);
        prefs->setDouble(pfad, prefs->getDouble(pfad, 0.0) + m);
    }
    werkzeug_reiter_auffrischen();
}

double HirchFraesen::Impl::laufzeit(bj::object const &w)
{
    return Inkscape::Preferences::get()->getDouble("/hirch/laufzeit/" + laufzeit_schluessel(w), 0.0);
}

void HirchFraesen::Impl::vorschau()
{
    // erst rechnen, dann zeigen (3D-Zeichnungen wie bisher: die Vorschau zeigt, was es schon gibt)
    if (!ergebnis_aktuell && !rechnet && (pausiert || (wartet_knopf && !auto_rechnen()))) {
        nach_rechnen = [this] { vorschau(); };
        rechnen_jetzt();
        return;
    }
    if (!vorschau_fenster) {
        vorschau_fenster = new HirchVorschau();
        vorschau_fenster->ab_hier = [this](int programm, int zeile) {
            bj::object ab;
            ab["programm"] = programm;
            ab["zeile"] = zeile;
            ab_hier_ausgeben(bj::serialize(ab), (programm > 1 ? TR("Datei ") + std::to_string(programm) + " " : std::string()) +
                                                     TR("Zeile ") + std::to_string(zeile));
        };
        if (auto f = fenster()) vorschau_fenster->set_transient_for(*f);
    }
    vorschau_fenster->present();
    if (ergebnis_aktuell) {
        vorschau_laden();
    } else {
        VorschauDaten d;
        d.meldung = TR("Rechnet …");
        vorschau_fenster->zeigen(d);
    }
}

void HirchFraesen::Impl::vorschau_laden()
{
    if (!vorschau_fenster) return;
    vorschau_fenster->zeigen(vorschau_daten());
}

/// Was die Vorschau und die 3D-Ansicht im Hauptfenster aus der letzten Rechnung zeigen
VorschauDaten HirchFraesen::Impl::vorschau_daten()
{
    VorschauDaten d;
    if (auto ds = liste_von(ergebnis, "dateien")) d.ausgaben = (int)ds->size();   // Windows 01.11.: «eine Datei»?
    if (!flag(ergebnis, "ok", false)) {
        d.meldung = TR("Nichts zu zeigen.\n\n") + warnungen_text(ergebnis);
        return d;
    }
    if (auto m = unter(analyse, "material")) {
        d.dicke = num(*m, "dicke", 18);
        d.breite = num(*m, "breite", 0);
        d.hoehe = num(*m, "hoehe", 0);
    }
    if (auto h = unter(einst, "hoehen")) d.sicherheit = num(*h, "sicherheitshoehe", 20);
    d.mutter_d = std::max(0.0, ma_wert("mutter_d", 0));          // 3D-Ansicht: Spannmutter und Spindel
    d.mutter_h = std::max(0.0, ma_wert("mutter_hoehe", 0));
    d.spindel_d = std::max(0.0, ma_wert("spindel_d", 0));
    if (zweiseitig()) {
        d.zweiseitig = true;
        auto z = unter(einst, "zweiseitig");
        d.um_y = z && str(*z, "achse", "x") == "y";
    }
    if (auto sp = liste_von(ergebnis, "spannmittel")) {
        for (auto const &v : *sp) {
            auto o = v.if_object();
            if (!o) continue;
            VorschauSpannmittel x;
            x.hoehe = num(*o, "hoehe", 15);
            x.seite = (int)num(*o, "seite", 1);
            x.spannung = (int)num(*o, "spannung", 1);
            if (auto pts = liste_von(*o, "punkte")) {
                for (auto const &q : *pts) {
                    if (auto a = q.if_array(); a && a->size() >= 2) {
                        x.punkte.push_back({(*a)[0].to_number<double>(), (*a)[1].to_number<double>()});
                    }
                }
            }
            if (x.punkte.size() >= 3) d.spannmittel.push_back(std::move(x));
        }
    }
    // Umspannen (3D-Ansicht): Sperrzonen je Seite; Höhe der Eilgänge in Spannung 2 ohne eingezeichnete Spannmittel
    // (Abstand und eingetragene Höhe wie engine._umspannen_hoehe2)
    if (auto us = liste_von(ergebnis, "umspannen")) {
        for (auto const &v : *us) {
            auto o = v.if_object();
            if (!o) continue;
            int seite = (int)num(*o, "seite", 1);
            if (auto zl = liste_von(*o, "zonen_mm")) {
                for (auto const &zv : *zl) {
                    auto za = zv.if_array();
                    if (!za) continue;
                    VorschauZone z;
                    z.seite = seite;
                    for (auto const &q : *za)
                        if (auto a = q.if_array(); a && a->size() >= 2)
                            z.ring.push_back({(*a)[0].to_number<double>(), (*a)[1].to_number<double>()});
                    if (z.ring.size() >= 3) d.zonen.push_back(std::move(z));
                }
            }
        }
        if (auto u = unter(einst, "umspannen")) {
            d.umspann_abstand = std::max(0.0, num(*u, "abstand", 10));
            d.umspann_hoehe2 = std::max(0.0, num(*u, "hoehe2", 0));
        }
    }
    {
        // der eine Nullpunkt aus dem Rechenkern (auf der Vorrichtung: derselbe Maschinenpunkt)
        auto o = nullpunkt_mm();
        double sx = 0, sy = 0;
        seite_ecke(1, sx, sy);
        if (Geom::Point n; nullpunkt_aus_analyse(platte, 1, n)) o = Geom::Point(n.x() - sx, sy + d.hoehe - n.y());
        d.null_x = o.x();
        d.null_y = o.y();
        if (auto n = unter(einst, "nullpunkt")) d.null_unten = str(*n, "z", "oben") == "unten";
        // rund: das Werkstück ist der Zylinder – nicht die (eckige) Vorrichtung
        if (rund_einst(einst) && platte != 0 && d.breite > 0 && d.hoehe > 0) d.rund_r = std::min(d.breite, d.hoehe) / 2;
        // Schraube durch die Mitte (rund.schraube): Kopf bzw. Scheibe auf der Oberfläche
        if (auto ru = rund_einst(einst); ru && d.rund_r > 0 && flag(*ru, "schraube", true)) {
            VorschauSchraube sc;
            sc.x = d.breite / 2;
            sc.y = d.hoehe / 2;
            sc.kopf = std::max(num(*ru, "kopf", 12), num(*ru, "bohrung", 6.5));
            sc.ueberstand = num(*ru, "ueberstand", 0);
            if (sc.kopf > 0) d.schrauben.push_back(sc);
        }
    }
    if (auto sl = liste_von(ergebnis, "soll")) {
        for (auto const &v : *sl) {
            auto o = v.if_object();
            if (!o) continue;
            VorschauSoll s_;
            s_.seite = (int)num(*o, "seite", 1);
            s_.tiefe = num(*o, "tiefe", 0);
            if (auto fl = liste_von(*o, "flaechen")) {
                for (auto const &fv : *fl) {
                    auto fa = fv.if_array();
                    if (!fa) continue;
                    std::vector<std::vector<double>> ringe;
                    for (auto const &rv : *fa) {
                        auto ra = rv.if_array();
                        if (!ra) continue;
                        std::vector<double> r;
                        for (auto const &z : *ra) r.push_back(z.to_number<double>());
                        ringe.push_back(std::move(r));
                    }
                    s_.flaechen.push_back(std::move(ringe));
                }
            }
            if (s_.tiefe > 0 && !s_.flaechen.empty()) d.soll.push_back(std::move(s_));
        }
    }
    if (auto ps = liste_von(ergebnis, "programme")) {
        for (auto const &v : *ps) {
            auto o = v.if_object();
            if (!o) continue;
            VorschauProgramm p;
            p.datei = str(*o, "datei");
            p.werkzeug = TR(str(*o, "name"));
            p.form = str(*o, "form", "flach");
            // genauere Form, wo «form» nur eine Annäherung ist (konischer Kugelfräser: Kugel + Kegel)
            if (str(*o, "form3d") == "konisch") p.form = "konisch";
            p.durchmesser = num(*o, "durchmesser", 3);
            p.vwinkel = num(*o, "v_winkel", 60);
            p.spitze = num(*o, "spitze", 0);
            p.eckradius = num(*o, "eckradius", 0);
            p.schneide = num(*o, "schneidenlaenge", 0);
            p.schaft = num(*o, "schaft", 0);
            p.uebergang = num(*o, "uebergang", 0);
            p.ausspann = num(*o, "ausspannlaenge", 0);
            p.gesamt = num(*o, "gesamtlaenge", 0);
            p.schneiden = std::max(1, (int)num(*o, "schneiden", 2));
            if (auto dr = str(*o, "drall"); !dr.empty()) p.drall = dr[0];
            p.seite = (int)num(*o, "seite", 1);
            p.spannung = (int)num(*o, "spannung", 1);
            p.t = (int)num(*o, "t", 0);
            if (auto ab = liste_von(*o, "abschnitte")) {
                for (auto const &a : *ab) {
                    if (auto ao = a.if_object()) {
                        VorschauAbschnitt x;
                        x.zeile = (int)num(*ao, "zeile", 1);
                        x.farbe = str(*ao, "farbe");
                        x.name = str(*ao, "name");
                        x.id = str(*ao, "key");
                        p.abschnitte.push_back(x);
                    }
                }
            }
            d.programme.push_back(p);
        }
    }
    if (auto gl = liste_von(ergebnis, "gewinde")) {
        for (auto const &v : *gl) {
            auto o = v.if_object();
            if (!o) continue;
            VorschauGewinde g;
            if (auto m = liste_von(*o, "mitte"); m && m->size() >= 2) {
                g.x = wert((*m)[0]);
                g.y = wert((*m)[1]);
            }
            g.nenn = num(*o, "nenn", 8);
            g.steigung = num(*o, "steigung", 1.25);
            g.kern = num(*o, "kernloch", g.nenn - 1.0825 * g.steigung);
            g.oben = num(*o, "oben", 0);
            g.tiefe = num(*o, "tiefe", 10);
            g.innen = flag(*o, "innen", true);
            g.rechts = flag(*o, "rechts", true);
            g.seite = (int)num(*o, "seite", 1);
            // nach dem letzten Programm dieses Werkzeugs auf dieser Seite ist es fertig
            if (auto ps = liste_von(ergebnis, "programme")) {
                for (std::size_t i = 0; i < ps->size(); ++i) {
                    auto po = (*ps)[i].if_object();
                    if (po && str(*po, "werkzeug") == str(*o, "werkzeug") && (int)num(*po, "seite", 1) == g.seite)
                        g.programm = (int)i;
                }
            }
            if (g.nenn > 0 && g.steigung > 0 && g.tiefe > 0) d.gewinde.push_back(g);
        }
    }
    auto texte = [&](char const *k, std::vector<std::string> &ziel) {
        if (auto l = liste_von(ergebnis, k)) {
            for (auto const &v : *l) if (v.is_string()) ziel.emplace_back(v.get_string());
        }
    };
    texte("bericht", d.bericht);
    texte("warnungen", d.warnungen);
    return d;
}

} // namespace Inkscape::UI::Dialog

namespace Inkscape::UI::Dialog {

// ---------------------------------------------------------------- Zusammenfassung
// Ein Blatt zum Fotografieren: hoch und schmal wie ein Handybildschirm, grosse Schrift.
// Material und Nullpunkt, Gesamtzeit, das Werkzeug beim Start und jeder Halt am Regler
// mit dem nächsten Werkzeug und der Drehzahl zum Einstellen von Hand.
void HirchFraesen::Impl::zusammenfassung()
{
    if (!ergebnis_aktuell && !rechnet && (pausiert || (wartet_knopf && !auto_rechnen()))) {
        nach_rechnen = [this] { zusammenfassung(); };
        rechnen_jetzt();
        return;
    }
    auto ab = ablauf_daten();
    if (!ab || !ergebnis_aktuell) return;
    auto fensterchen = new Gtk::Window();
    fensterchen->set_title(TR("Zusammenfassung"));
    if (auto f = fenster()) fensterchen->set_transient_for(*f);
    fensterchen->set_default_size(560, 940);
    fensterchen->signal_hide().connect([fensterchen] { Glib::signal_idle().connect_once([fensterchen] { delete fensterchen; }); });
    auto rollen = Gtk::make_managed<Gtk::ScrolledWindow>();
    rollen->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
    fensterchen->add(*rollen);
    auto blatt = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 14);
    klasse(*blatt, "hz-blatt");
    rollen->add(*blatt);
    auto m = [](std::string const &markup, char const *k = nullptr) {
        auto l = Gtk::make_managed<Gtk::Label>();
        l->set_markup(markup);
        l->set_xalign(0);
        l->set_line_wrap(true);
        if (k) klasse(*l, k);
        return l;
    };
    auto esc = [](std::string const &s) { return markup_escape(s); };
    auto zwei = [&](std::string const &links, std::string const &rechts, char const *groesse) {
        auto z = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 10);
        auto l = m("<span size='" + std::string(groesse) + "'>" + links + "</span>");
        l->set_hexpand(true);
        z->add(*l);
        auto r = m("<span size='" + std::string(groesse) + "' font_family='monospace, Segoe UI Symbol'>" + rechts + "</span>");
        r->set_xalign(1);
        z->add(*r);
        return z;
    };

    // Kopf mit Umschalter hell/dunkel
    {
        auto z = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
        auto l = m("<span size='small' weight='heavy' letter_spacing='1200'>" + esc(TR("HIRCH CAM · ZUSAMMENFASSUNG")) + "</span>", "hz-dim");
        l->set_hexpand(true);
        z->add(*l);
        auto dunkel = Gtk::make_managed<Gtk::ToggleButton>(TR("Dunkel"));
        dunkel->set_tooltip_text(TR("Helle Schrift auf dunklem Grund – spiegelt weniger auf dem Foto"));
        dunkel->signal_toggled().connect([blatt, dunkel] {
            auto sc = blatt->get_style_context();
            if (dunkel->get_active()) sc->add_class("hz-dunkel");
            else sc->remove_class("hz-dunkel");
        });
        z->add(*dunkel);
        blatt->add(*z);
    }
    blatt->add(*m("<span size='xx-large' weight='heavy'>" + esc(vorgeschlagener_name()) + "</span>"));
    {
        std::string datei;
        if (auto ds = liste_von(ergebnis, "dateien")) {
            for (auto const &v : *ds) {
                if (!v.is_string()) continue;
                std::string p(v.get_string());
                datei += (datei.empty() ? "" : ", ") + steuerung_name(p);
            }
        }
        auto heute = Glib::DateTime::create_now_local().format("%d.%m.%Y").raw();
        blatt->add(*m("<span size='large'>" + esc(datei.empty() ? heute : datei + " · " + heute) + "</span>", "hz-dim"));
    }

    // Material und Nullpunkt
    {
        auto karte = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 14);
        klasse(*karte, "hz-karte");
        auto links = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
        links->set_hexpand(true);
        double br = 0, ho = 0, di = 0;
        if (auto mat = unter(analyse, "material")) {
            br = num(*mat, "breite", 0);
            ho = num(*mat, "hoehe", 0);
            di = num(*mat, "dicke", 18);
        }
        bj::object np;
        if (auto n = unter(einst, "nullpunkt")) np = *n;
        auto xy = str(np, "xy", "ul");
        bool z_unten = str(np, "z", "oben") == "unten";
        links->add(*m("<span size='x-large' weight='bold'>" + esc(zahl(br, 1) + " × " + zahl(ho, 1) + " × " + zahl(di, 2) + " mm") + "</span>"));
        links->add(*m("<span size='x-large'>" + esc(werkstoff_name()) + "</span>"));
        // provisorischer Nullpunkt (oranges Kreuz in der Zeichnung): gilt auf seiner Seite statt des gewählten
        double prov_x = -1, prov_y = -1;
        std::string prov_text;
        if (auto pn = unter(ergebnis, "nullpunkt_provisorisch")) {
            for (auto const &[k, v] : *pn) {
                auto o = v.if_object();
                if (!o) continue;
                // mit Kamera-Versatz: «kreuz» ist die Stelle des Kreuzes (dort setzt die Kamera X0 Y0)
                double kx = num(*o, "x", 0), ky = num(*o, "y", 0);
                auto kr = liste_von(*o, "kreuz");
                if (kr && kr->size() == 2) {
                    kx = (*kr)[0].to_number<double>();
                    ky = (*kr)[1].to_number<double>();
                }
                if (prov_x < 0) {
                    prov_x = kx;
                    prov_y = ky;
                }
                if (!prov_text.empty()) prov_text += " · ";
                prov_text += TRF("Seite {}: X {} / Y {} mm von der Ecke unten links",
                                 {std::string(k), zahl(kx, 2), zahl(ky, 2)});
                auto ka = liste_von(*o, "kamera");
                if (ka && ka->size() == 2) {
                    prov_text += " – " + TRF("mit der Kamera antasten, Kamera-Versatz X {} / Y {} mm eingerechnet",
                                             {zahl((*ka)[0].to_number<double>(), 2), zahl((*ka)[1].to_number<double>(), 2)});
                }
            }
        }
        if (prov_text.empty()) {
            links->add(*m("<span size='x-large'>" + esc(TR("XY-Null: ")) + "<b>" + esc(nullpunkt_name(xy)) + "</b></span>"));
        } else {
            links->add(*m("<span size='x-large' foreground='#c64600'>" + esc(TR("XY-Null: ")) + "<b>" +
                          esc(TR("provisorisch – am orangen Kreuz antasten, nicht an der Materialecke")) +
                          "</b></span>"));
            links->add(*m("<span foreground='#c64600'>" + esc(prov_text) + "</span>"));
        }
        links->add(*m("<span size='x-large'>" + esc(TR("Z-Null: ")) + "<b>" +
                      esc(z_unten ? TR("Werktisch (Unterseite)") : TR("Materialoberfläche")) + "</b></span>"));
        karte->add(*links);
        // Skizze: Material mit Nullpunkt
        auto sk = Gtk::make_managed<Gtk::DrawingArea>();
        sk->set_size_request(150, 110);
        double verh = (br > 0 && ho > 0) ? ho / br : 0.6;
        double rel_x = (prov_x >= 0 && br > 0) ? std::clamp(prov_x / br, 0.0, 1.0) : -1;
        double rel_y = (prov_y >= 0 && ho > 0) ? std::clamp(prov_y / ho, 0.0, 1.0) : -1;
        sk->signal_draw().connect([sk, xy, verh, rel_x, rel_y](Cairo::RefPtr<Cairo::Context> const &cr) {
            auto fg = sk->get_style_context()->get_color(sk->get_state_flags());
            double w = 126, h = std::clamp(126 * verh, 30.0, 90.0);
            if (h >= 90) w = std::clamp(90 / verh, 30.0, 126.0);
            double x0 = 14, y0 = 8;
            cr->rectangle(x0, y0, w, h);
            cr->set_source_rgb(0.90, 0.83, 0.70);
            cr->fill_preserve();
            cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 1);
            cr->set_line_width(2);
            cr->stroke();
            double fx = xy.size() == 2 ? (xy[1] == 'l' ? 0 : xy[1] == 'm' ? 0.5 : 1) : 0;
            double fy = xy.size() == 2 ? (xy[0] == 'u' ? 1 : xy[0] == 'm' ? 0.5 : 0) : 1;
            if (rel_x >= 0) {
                fx = rel_x;
                fy = 1 - rel_y;
            }
            double px = x0 + fx * w, py = y0 + fy * h;
            cr->set_source_rgb(0.90, 0.38, 0.0);
            cr->set_line_width(3);
            cr->move_to(px, py);
            cr->line_to(px + (fx < 1 ? 26 : -26), py);
            cr->move_to(px, py);
            cr->line_to(px, py + (fy > 0 ? -26 : 26));
            cr->stroke();
            cr->arc(px, py, 7, 0, 2 * M_PI);
            cr->fill_preserve();
            cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 1);
            cr->set_line_width(2);
            cr->stroke();
            return true;
        });
        karte->add(*sk);
        blatt->add(*karte);
    }
    blatt->add(*zwei(esc(TR("Gesamtzeit")), "<b>" + esc(zeit(num(ergebnis, "minuten", 0))) + "</b>", "xx-large"));

    // Werkzeuge in der Reihenfolge der Halts
    int halt = 0;
    bool erster = true;
    // Steuerung mit einer Datei je Werkzeug: kein Halt in der Datei, sondern die nächste Datei laden
    bool eine_datei = flag(*ab, "eine_datei", true);
    bool z_oben = true;
    if (auto n = unter(einst, "nullpunkt")) z_oben = str(*n, "z", "oben") != "unten";
    if (auto seiten = liste_von(*ab, "seiten")) {
        for (auto const &sv : *seiten) {
            auto so = sv.if_object();
            if (!so) continue;
            auto abs = liste_von(*so, "abschnitte");
            if (!abs) continue;
            for (auto const &av : *abs) {
                auto a = av.if_object();
                if (!a) continue;
                bool wenden = flag(*a, "nach_wenden", false);
                bool wechsel = flag(*a, "wechsel", false);
                auto block = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 6);
                Gtk::CheckButton *erledigt = nullptr;
                if (wenden) {
                    blatt->add(*m("<span size='x-large' weight='heavy'>" + esc(TR("SEITE 2 – WERKSTÜCK WENDEN")) + "</span>"));
                }
                if (erster) {
                    klasse(*block, "hz-start");
                    block->add(*m("<span size='small' weight='heavy' letter_spacing='1200'>" +
                                  esc(TR("BEIM START EINGESETZT")) + "</span>", "hz-dim"));
                } else if (wechsel || wenden) {
                    klasse(*block, "hz-halt");
                    auto z = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
                    auto t = wenden ? TR("NACH DEM WENDEN EINSETZEN")
                             : eine_datei ? TRF("HALT {} – WERKZEUG WECHSELN", {std::to_string(++halt)})
                                          : TRF("DATEI {} – WERKZEUG WECHSELN", {std::to_string(++halt + 1)});
                    auto l = m("<span weight='heavy' letter_spacing='800'>" + esc(t) + "</span>", "hz-halt-titel");
                    l->set_hexpand(true);
                    z->add(*l);
                    erledigt = Gtk::make_managed<Gtk::CheckButton>(TR("erledigt"));
                    z->add(*erledigt);
                    block->add(*z);
                } else {
                    klasse(*block, "hz-start");
                }
                block->add(*m("<span size='xx-large' weight='heavy' font_family='monospace, Segoe UI Symbol'>T" +
                              std::to_string((long)num(*a, "t", 1)) + "</span>  <span size='x-large' weight='bold'>" +
                              esc(str(*a, "werkzeug")) + "</span>"));
                if (!eine_datei && !str(*a, "datei").empty()) {
                    block->add(*m("<span size='large' font_family='monospace, Segoe UI Symbol'>" + esc(steuerung_name(str(*a, "datei"))) +
                                  "</span>"));
                }
                if (long n = (long)num(*a, "drehzahl", 0)) {
                    block->add(*m("<span size='xx-large' weight='heavy'>" + esc(TR("Spindel ") + ganz((double)n) + TR(" U/min")) +
                                  "</span>"));
                }
                if (!erster && (wechsel || wenden) && z_oben) {
                    block->add(*m("<span size='large'>" + esc(TR("Z auf der Materialoberfläche neu nullen")) + "</span>"));
                }
                if (auto ops = liste_von(*a, "ops")) {
                    for (auto const &ov : *ops) {
                        auto o = ov.if_object();
                        if (!o) continue;
                        std::string name = str(*o, "name");
                        if (!str(*o, "teil").empty()) name = "↳ " + (str(*o, "extra").empty() ? name : str(*o, "extra"));
                        double t = num(*o, "tiefe", 0);
                        if (t > 0) name += " · " + zahl(t) + " mm";
                        block->add(*zwei(esc(name), esc(kurzzeit(num(*o, "minuten", 0))), "large"));
                    }
                }
                if (erledigt) {
                    erledigt->signal_toggled().connect([block, erledigt] {
                        block->set_opacity(erledigt->get_active() ? 0.45 : 1.0);
                    });
                }
                blatt->add(*block);
                erster = false;
            }
        }
    }

    // Was vor dem Start zu beachten ist (Fehler und offene Hinweise der Prüfung)
    {
        std::vector<std::string> punkte;
        auto q = quittiert_lesen();
        if (auto pr = liste_von(ergebnis, "pruefung")) {
            for (auto const &v : *pr) {
                auto o = v.if_object();
                if (!o) continue;
                auto t = str(*o, "text");
                if (str(*o, "stufe") == "fehler" || !q.count(t)) punkte.push_back(t);
            }
        }
        if (!punkte.empty()) {
            auto w = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 4);
            klasse(*w, "hz-warn");
            w->add(*m("<span size='large' weight='heavy'>" + esc(TR("Vor dem Start beachten")) + "</span>"));
            int k = 0;
            for (auto const &p : punkte) {
                if (++k > 5) break;
                w->add(*m("<span size='large'>" + esc(p) + "</span>"));
            }
            blatt->add(*w);
        }
    }
    fensterchen->show_all();
}

} // namespace Inkscape::UI::Dialog
