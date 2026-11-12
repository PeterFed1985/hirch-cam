// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * Hirch CNC: Werkzeugleiste «Kreis»: Radius eintippen, Kreis anlegen (Teile von ArcToolbar)
 * Aus dem Inkscape-Patch hierher verschoben (29.10.2026, nur verschoben) – im Patch bleiben die Aufrufe.
 */
#include "arc-toolbar.h"
#include "ui/dialog/hirch-sprache.h"   // Hirch CNC: TR()
#include <glibmm/i18n.h>
#include <gtkmm/radiobutton.h>
#include "desktop.h"
#include "desktop-style.h"
#include "document.h"
#include "layer-manager.h"
#include "object/sp-root.h"
#include "util/units.h"
#include "xml/repr.h"
#include "document-undo.h"
#include "mod360.h"
#include "object/sp-ellipse.h"
#include "object/sp-namedview.h"
#include "selection.h"
#include "ui/builder-utils.h"
#include "ui/icon-names.h"
#include "ui/tools/arc-tool.h"
#include "ui/widget/canvas.h"
#include "ui/widget/combo-tool-item.h"
#include "ui/widget/spinbutton.h"
#include "ui/widget/toolbar-menu-button.h"
#include "ui/widget/unit-tracker.h"
#include "widgets/widget-sizes.h"

using Inkscape::DocumentUndo;
using Inkscape::Util::Quantity;
using Inkscape::Util::Unit;

namespace Inkscape::UI::Toolbar {

void ArcToolbar::radius_setzen(bool neu_zeichnen)
{
    Unit const *unit = _tracker->getActiveUnit();
    g_return_if_fail(unit != nullptr);
    double r = Quantity::convert(_r_item->get_adjustment()->get_value(), unit, "px");
    if (r <= 0) return;
    Preferences::get()->setDouble("/tools/shapes/arc/radius", r);
    auto doc = _desktop->getDocument();
    bool gemacht = false;
    _freeze = true;
    for (auto item : _desktop->getSelection()->items()) {
        if (auto ge = cast<SPGenericEllipse>(item)) {
            ge->setVisibleRx(r);
            ge->setVisibleRy(r);
            ge->normalize();
            ge->updateRepr();
            ge->requestDisplayUpdate(SP_OBJECT_MODIFIED_FLAG);
            gemacht = true;
        }
    }
    _freeze = false;
    if (gemacht) {
        DocumentUndo::done(doc, "Kreis: Radius", INKSCAPE_ICON("draw-ellipse"));
        return;
    }
    if (!neu_zeichnen) return;
    // Neuer Kreis in der Mitte der Ansicht
    auto layer = _desktop->layerManager().currentLayer();
    if (!layer) return;
    // wie das Kreiswerkzeug: in Dokument-Koordinaten (px) anlegen, dann die
    // Ebenen-Transformation einrechnen (gleicht auch die Linienstärke an)
    auto mitte = _desktop->get_display_area().midpoint() * _desktop->dt2doc();
    auto repr = doc->getReprDoc()->createElement("svg:path");
    repr->setAttribute("sodipodi:type", "arc");
    sp_desktop_apply_style_tool(_desktop, repr, "/tools/shapes/arc", false);
    auto ge = cast<SPGenericEllipse>(layer->appendChildRepr(repr));
    Inkscape::GC::release(repr);
    if (ge) {
        ge->transform = layer->i2doc_affine().inverse();
        ge->position_set(mitte[Geom::X], mitte[Geom::Y], r, r);
        ge->updateRepr();
        ge->doWriteTransform(ge->transform, nullptr, true);
        _desktop->getSelection()->set(ge);
    }
    DocumentUndo::done(doc, "Kreis zeichnen", INKSCAPE_ICON("draw-ellipse"));
}

void ArcToolbar::radius_zeigen()
{
    auto ge = cast<SPGenericEllipse>(_item);
    if (!ge || !_r_item) return;
    Unit const *unit = _tracker->getActiveUnit();
    if (!unit) return;
    bool alt = _freeze;
    _freeze = true;
    _r_item->get_adjustment()->set_value(Quantity::convert(ge->getVisibleRx(), "px", unit));
    _freeze = alt;
}

} // namespace Inkscape::UI::Toolbar
