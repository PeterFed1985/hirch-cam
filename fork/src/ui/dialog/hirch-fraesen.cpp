// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: das Fräsen-Panel. Siehe hirch-fraesen.h.
 *
 * Ablauf: Jede Änderung an der Zeichnung startet (nach einer kurzen Pause)
 *   1. «analyse»  – Material, Einstellungen, Objekte je Fräsart (schnell),
 *   2. «rechnen»  – Werkzeugwege, .tap-Dateien und 3D-Vorschau in einen
 *                   eigenen Arbeitsordner.
 * «G-Code speichern» kopiert dann nur noch die fertigen Dateien.
 */
#include "page-manager.h"
#include "ui/dialog/hirch-intern.h"
#include <boost/json/src.hpp>   // Boost.JSON, nur hier übersetzt

namespace Inkscape::UI::Dialog {

HirchFraesen::HirchFraesen()
    : DialogBase("/dialogs/hirch-fraesen", "Fraesen")
    , _impl(std::make_unique<Impl>(*this))
{
    show_all_children();
    _impl->anzeigen();
    // Den Reiter «Werkzeuge» gibt es nicht mehr (Peter, 20.10.): die Werkzeug-Kopfzeilen im Ablauf und
    // der Werkzeugdialog zeigen alles. Aus einer gespeicherten Fensteranordnung von früher schliessen.
    Glib::signal_timeout().connect_once([this] {
        auto desk = getDesktop();
        auto c = desk ? desk->getContainer() : nullptr;
        auto wz = c ? c->get_dialog("HirchWerkzeuge") : nullptr;
        if (!wz) return;
        for (Gtk::Widget *w = wz; w && w->get_parent(); w = w->get_parent()) {
            if (auto nb = dynamic_cast<Gtk::Notebook *>(w->get_parent())) {
                int n = nb->page_num(*w);
                if (n >= 0) nb->remove_page(n);
                break;
            }
        }
    }, 400);
}

HirchFraesen::~HirchFraesen() = default;

void HirchFraesen::documentReplaced() { _impl->dokument_neu(); }
void HirchFraesen::desktopReplaced() { _impl->leinwand_verbinden(); }
void HirchFraesen::selectionChanged(Inkscape::Selection *sel)
{
    _impl->zuweisen_auffrischen();
    // Klick auf ein Objekt einer anderen Zeichenfläche: deren Seite/Platte zeigen (Peter, 24.10.)
    if (!sel || sel->isEmpty() || !getDocument()) return;
    auto item = sel->singleItem();
    if (!item) return;
    auto pg = getDocument()->getPageManager().getPageFor(item, true);
    if (!pg || !pg->getId()) return;
    std::string id = pg->getId();
    Glib::signal_idle().connect_once([this, id] {
        if (auto d = getDocument())
            if (auto p = cast<SPPage>(d->getObjectById(id))) {
                d->getPageManager().selectPage(p);      // sonst meldet der Seitenverwalter später die alte
                _impl->seite_gewaehlt_von(p);
            }
    });
}
void HirchFraesen::materialBearbeiten() { _impl->material_dialog(); }
bool HirchFraesen::ersteinrichtungWennNoetig()
{
    if (!_impl->ersteinrichtung_noetig()) return false;
    _impl->ersteinrichtung();
    return true;
}
void HirchFraesen::werkzeugeBearbeiten() { _impl->werkzeuge_dialog({}, {}, nullptr, true); }
void HirchFraesen::vorschauZeigen() { _impl->vorschau(); }
void HirchFraesen::gcodeSpeichern() { _impl->speichern(); }
void HirchFraesen::bahnenZeigen(bool an) { _impl->bahnen_setzen(an); }
void HirchFraesen::gravurText() { _impl->gravur_oder_text(); }
void HirchFraesen::aktualisieren() { _impl->aktualisieren(); }
void HirchFraesen::spannmittel() { _impl->spannmittel_dialog(); }
void HirchFraesen::stege() { _impl->stege_leiste(); }
void HirchFraesen::aufraeumen(std::string const &endung, bool nach_import) { _impl->aufraeumen(endung, nach_import); }
void HirchFraesen::verschachteln() { _impl->verschachteln(); }
void HirchFraesen::zusatzOeffnen(std::string const &datei) { _impl->zusatz_oeffnen(datei); }
bool HirchFraesen::zweiteSeite() { return _impl->zweiseitig() || _impl->vorrichtung_an_einst(); }
void HirchFraesen::maschineFuellen(Gtk::Box *ziel) { if (ziel) _impl->maschine_abschnitt(ziel); }
void HirchFraesen::werkstoffeFuellen(Gtk::Box *ziel) { if (ziel) _impl->werkstoffe_abschnitt(ziel); }
void HirchFraesen::vorgabenFuellen(Gtk::Box *ziel) { if (ziel) _impl->vorgaben_abschnitt(ziel); }
void HirchFraesen::schriftImportieren(Gtk::Window &eltern, std::function<void(std::string const &)> info)
{
    _impl->schrift_import_lauf(eltern, std::move(info), [](std::string const &) {});
}
void HirchFraesen::zeichentafel(Gtk::Window &eltern) { _impl->zeichentafel_zeigen(eltern); }
void HirchFraesen::alsVorlage() { _impl->als_vorlage(); }
void HirchFraesen::gespiegeltSpeichern() { _impl->gespiegelt_speichern(); }
void HirchFraesen::ausVorlage() { _impl->aus_vorlage(); }
void HirchFraesen::fassungen() { _impl->versionen_dialog(); }
void HirchFraesen::postprozessoren() { _impl->postprozessoren_dialog(); }
void HirchFraesen::darstellungZeigen() { _impl->darstellung_zeigen(); }
void HirchFraesen::ordnen() { _impl->ordnen(); }
void HirchFraesen::zusammenfassungZeigen() { _impl->zusammenfassung(); }
void HirchFraesen::aufStickSpeichern() { _impl->auf_stick(); }
void HirchFraesen::luftfraesenSpeichern() { _impl->luftfraesen(false); }
void HirchFraesen::werkzeugeExportieren() { _impl->werkzeuge_exportieren(nullptr); }
void HirchFraesen::werkzeugeImportieren() { _impl->werkzeuge_importieren(nullptr); }
void HirchFraesen::messkorrektur() { _impl->messkorrektur_dialog(); }
bool HirchFraesen::nullpunktFuer(double x, double y, double &nx, double &ny)
{
    Geom::Point p;
    if (!_impl->nullpunkt_doc(Geom::Point(x, y), p)) return false;
    nx = p.x();
    ny = p.y();
    return true;
}
void HirchFraesen::werkzeugListe(Gtk::Box *ziel) { if (ziel) _impl->werkzeug_liste(ziel); }
void HirchFraesen::farbleisteFuellen(Gtk::Box *ziel) { if (ziel) _impl->farbleiste_fuellen(ziel); }

// für den Oberflächentest (hirch-test.cpp)
int HirchFraesen::anzahlBearbeitungen()
{
    auto b = liste_von(_impl->einst, "bearbeitungen");
    return b ? (int)b->size() : 0;
}
void HirchFraesen::bearbeitungOeffnen(int index) { _impl->bearbeitung_dialog(index); }
void HirchFraesen::neuAnlegen(std::string const &art) { _impl->neu(art); }
bool HirchFraesen::beschaeftigt() { return _impl->rechnet || !_impl->hat_analyse; }

// ---------------------------------------------------------------------------
// Reiter «Werkzeuge» (Liste der Werkzeuge dieser Zeichnung)

struct HirchWerkzeuge::Impl {
    HirchWerkzeuge &panel;
    Gtk::Box box{Gtk::ORIENTATION_VERTICAL, 10};
    explicit Impl(HirchWerkzeuge &p) : panel(p)
    {
        css_einrichten();
        klasse(box, "hirch-panel");
        box.set_margin_top(10);
        box.set_margin_start(2);
        box.set_margin_end(2);
        auto sw = Gtk::make_managed<Gtk::ScrolledWindow>();
        sw->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
        sw->set_vexpand(true);
        sw->add(box);
        UI::pack_start(panel, *sw, true, true);
    }
    void fuellen()
    {
        auto desk = panel.getDesktop();
        auto c = desk ? desk->getContainer() : nullptr;
        auto f = c ? dynamic_cast<HirchFraesen *>(c->get_dialog("Fraesen")) : nullptr;
        if (f) {
            f->werkzeugListe(&box);
            return;
        }
        UI::remove_all_children(box);
        auto l = label(TR("Werkzeuge erscheinen hier, sobald der Reiter «Fräsen» offen ist."), "hirch-dim");
        l->set_line_wrap(true);
        box.add(*l);
        box.show_all();
    }
};

HirchWerkzeuge::HirchWerkzeuge()
    : DialogBase("/dialogs/hirch-werkzeuge", "HirchWerkzeuge")
    , _impl(std::make_unique<Impl>(*this))
{
    show_all_children();
    Glib::signal_idle().connect_once([this] { _impl->fuellen(); });
}

HirchWerkzeuge::~HirchWerkzeuge() = default;
void HirchWerkzeuge::auffrischen() { _impl->fuellen(); }
void HirchWerkzeuge::documentReplaced() { Glib::signal_idle().connect_once([this] { _impl->fuellen(); }); }


} // namespace Inkscape::UI::Dialog
