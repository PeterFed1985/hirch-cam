// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CAM: Rückmeldung nach dem Fräsen (Konzept 06.11.2026, Peter: «gut»).
 * Nach «G-Code speichern» / «Auf USB-Stick» merkt sich Hirch CAM die Zeichnung (nicht in der Zeichnung selbst – sie
 * soll dadurch nicht «geändert» werden). Beim nächsten Öffnen, frühestens 15 Minuten später, fragt es einmal:
 * «Wie lief …?». «zu langsam» / «zu schnell» schlägt den Schieber des Werkstoffs an dieser Maschine um 10 % vor –
 * übernommen wird nur mit Klick.
 */
#include "ui/dialog/hirch-intern.h"

namespace Inkscape::UI::Dialog {

namespace {
constexpr char const *PFAD = "/hirch/rueckfragen";
constexpr gint64 FRUEHESTENS = 15LL * 60 * G_USEC_PER_SEC;

bj::object rueckfragen_lesen()
{
    bj::object r;
    auto t = Inkscape::Preferences::get()->getString(PFAD);
    if (!t.empty()) {
        boost::system::error_code ec;
        auto v = bj::parse(t.raw(), ec);
        if (!ec && v.is_object()) r = v.as_object();
    }
    return r;
}

void rueckfragen_schreiben(bj::object const &r)
{
    Inkscape::Preferences::get()->setString(PFAD, bj::serialize(r));
    // sofort auf die Platte: sonst gilt es erst nach sauberem Beenden (Windows 08.11.: Frage kam nach der Antwort
    // noch einmal, weil das Programm nicht sauber beendet wurde)
    Inkscape::Preferences::get()->save();
}
} // namespace

void HirchFraesen::Impl::rueckfrage_merken()
{
    auto d = doc();
    if (!d || !d->getDocumentFilename() || luft_modus) return;
    auto r = rueckfragen_lesen();
    bj::object e;
    e["zeit"] = (double)g_get_real_time();
    e["material"] = str(einst, "material", "hartholz");
    // die Maschine, mit der gerechnet wurde (ohne Eintrag in der Zeichnung: die gültige – Windows 08.11., Fund 5)
    e["maschine"] = str(einst, "maschine_id").empty() ? maschine_werte_id : str(einst, "maschine_id");
    r[d->getDocumentFilename()] = e;
    // nur die letzten 30 Zeichnungen
    while (r.size() > 30) {
        auto aelteste = r.begin();
        for (auto it = r.begin(); it != r.end(); ++it)
            if (num(it->value().as_object(), "zeit", 0) < num(aelteste->value().as_object(), "zeit", 0)) aelteste = it;
        r.erase(aelteste);
    }
    rueckfragen_schreiben(r);
}

void HirchFraesen::Impl::rueckfrage_pruefen()
{
    auto d = doc();
    if (!d || !d->getDocumentFilename()) return;
    std::string datei = d->getDocumentFilename();
    if (rueckfrage_gefragt.count(datei)) return;
    rueckfrage_gefragt.insert(datei);
    auto r = rueckfragen_lesen();
    auto it = r.find(datei);
    if (it == r.end() || !it->value().is_object()) return;
    auto e = it->value().as_object();
    if (g_get_real_time() - (gint64)num(e, "zeit", 0) < FRUEHESTENS) return;      // gerade erst gespeichert
    std::string material = str(e, "material", "hartholz");
    std::string maschine = str(e, "maschine");
    nach_der_begruessung([this, datei, material, maschine] {
        auto dlg = dialog(TRF("Wie lief «{}»?", {Glib::path_get_basename(datei)}));
        auto t = label(TRF("Du hast den G-Code dieser Zeichnung gespeichert ({}). Deine Antwort hilft, die "
                           "Schnittdaten anzupassen.", {werkstoff_name()}), "hirch-dim");
        t->set_line_wrap(true);
        t->set_max_width_chars(50);
        t->set_xalign(0);
        dlg->get_content_area()->add(*t);
        dlg->add_button(TR("zu langsam"), 1);
        dlg->add_button(TR("passt"), 2);
        dlg->add_button(TR("zu schnell"), 3);
        dlg->add_button(TR("nicht gefräst"), 4);
        dlg->show_all();
        int a = dlg->run();
        dlg.reset();
        if (a < 1 || a > 4) return;                   // geschlossen: beim nächsten Öffnen noch einmal
        auto r2 = rueckfragen_lesen();
        r2.erase(datei);
        rueckfragen_schreiben(r2);
        if (a != 1 && a != 3) return;
        // gefräst an einer anderen Maschine als der, die jetzt gilt: kein Vorschlag (er träfe die falsche Maschine)
        std::string jetzt = str(einst, "maschine_id").empty() ? maschine_werte_id : str(einst, "maschine_id");
        if (!maschine.empty() && !jetzt.empty() && maschine != jetzt) return;
        // Vorschlag für den Schieber dieses Werkstoffs an dieser Maschine
        std::string aus, fehler;
        bj::object w;
        if (!python_jetzt({"datenbank", "werkstoffe"}, aus, fehler) || !json_lesen(aus, w)) return;
        int vorschub = 100, drehzahl = 100;
        std::string name = material;
        if (auto l = liste_von(w, "werkstoffe")) {
            for (auto const &v : *l) {
                auto o = v.if_object();
                if (o && str(*o, "key") == material) {
                    vorschub = (int)num(*o, "vorschub", 100);
                    drehzahl = (int)num(*o, "drehzahl", 100);
                    name = flag(*o, "eigen", false) ? str(*o, "name") : std::string(TR(str(*o, "name")));
                }
            }
        }
        int neu = std::clamp(vorschub + (a == 1 ? 10 : -10), 50, 150);
        if (neu == vorschub) return;
        auto prozent = [](int p) { return p == 100 ? std::string(TR("Herstellerwert")) : (p > 100 ? "+" : "−") + std::to_string(std::abs(p - 100)) + " %"; };
        auto d2 = dialog(TR("Vorschlag"));
        auto t2 = label(TRF("{} an dieser Maschine: Vorschub {} statt {}. Gilt für die nächsten Rechnungen.",
                            {name, prozent(neu), prozent(vorschub)}));
        t2->set_line_wrap(true);
        t2->set_max_width_chars(50);
        d2->get_content_area()->add(*t2);
        d2->add_button(TR("Nein danke"), Gtk::RESPONSE_CANCEL);
        auto ok = d2->add_button(TR("Übernehmen"), Gtk::RESPONSE_OK);
        klasse(*ok, "suggested-action");
        d2->show_all();
        int b = d2->run();
        d2.reset();
        if (b != Gtk::RESPONSE_OK) return;
        bj::object s;
        s["key"] = material;
        s["vorschub"] = neu;
        s["drehzahl"] = drehzahl;
        python({"datenbank", "werkstoff", bj::serialize(s)}, [this](bool, std::string const &, std::string const &) {
            letzte_xml.clear();
            spaeter(10);
        });
    });
}

} // namespace Inkscape::UI::Dialog
