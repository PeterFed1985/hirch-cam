// SPDX-License-Identifier: GPL-2.0-or-later
// Hirch CAM: zentrale Liste, was von Inkscape erreichbar bleibt – siehe hirch-erlaubt.h.
// Entscheidungen: Windows-Bericht «Inkscape-Werkzeuge» (Einteilung A/B/C) und Peter 07.11. zu den C-Punkten.
#include "ui/dialog/hirch-erlaubt.h"

#include <set>
#include <string>

#include "live_effects/effect-enum.h"
#include "ui/dialog/inkscape-preferences.h"

namespace HirchErlaubt {

namespace {

using Liste = std::set<std::string, std::less<>>;

bool drin(Liste const &l, std::string_view n) { return l.find(n) != l.end(); }

// Fenster. Weg: Exportieren (Bild), XML-Editor, Objekteigenschaften, Dokumenteinstellungen (doppelt zu «Grösse …»),
// Symbole (macht die Spalte dauerhaft breit), Meldungen (Peter 07.11.) und alles, was es schon vorher nicht gab.
Liste const FENSTER{
    "AlignDistribute", "Find", "Fraesen", "Glyphs", "Hirch3D", "HirchAnsicht", "LivePathEffect", "Objects",
    "Preferences", "Text", "Trace", "Transform", "UndoHistory",
};

// Werkzeuge. Spirale, Freihand, Radierer und Konstruktion (LPETool) kommen dazu (Peter 07.11.).
// Formerstellung (Booleans) bekommt einen Knopf (Peter 07.11. 11:14).
// Weg: 3D-Box, Kalligrafie, Farbverlauf, Verlaufsgitter, Pipette, Farbeimer, Modellieren, Sprühen, Verbinder, Zoom,
// Seitenwerkzeug (Zeichenfläche = Werkstück).
Liste const WERKZEUGE{
    "Select", "Node", "Booleans", "Rect", "Arc", "Star", "Spiral", "Pen", "Pencil", "Text", "Eraser", "LPETool",
    "Measure",
};
Liste const ALLE_WERKZEUGE{
    "Select", "Node", "Booleans", "Tweak", "Spray", "Rect", "3DBox", "Arc", "Star", "Spiral", "Pencil", "Pen",
    "Calligraphic", "Text", "Gradient", "Mesh", "Zoom", "Measure", "Dropper", "Connector", "PaintBucket", "Eraser",
    "LPETool", "Pages",
};

} // namespace

bool fenster(std::string_view name) { return drin(FENSTER, name); }

bool werkzeug(std::string_view name) { return drin(WERKZEUGE, name); }

bool werkzeug_knopf(std::string_view name)
{
    if (!drin(ALLE_WERKZEUGE, name)) return true;      // andere Knöpfe der Leiste (Hirch: Versetzen, Ausrichten …)
    return werkzeug(name);
}

bool pfadeffekt(int typ)
{
    using namespace Inkscape::LivePathEffect;
    switch (static_cast<EffectType>(typ)) {
        // Bearbeiten
        case FILLET_CHAMFER: case KNOT: case SIMPLIFY: case OFFSET:
        // Verzerren
        case BEND_PATH: case ENVELOPE: case LATTICE2: case PERSPECTIVE_ENVELOPE: case TRANSFORM_2PTS:
        case PATTERN_ALONG_PATH:
        // Erzeugen (Schraffur: Peter 07.11.)
        case BOOL_OP: case COPY_ROTATE: case MIRROR_SYMMETRY: case TILING: case SLICE: case INTERPOLATE:
        case ROUGH_HATCHES:
        // Konvertieren (Gestrichelte Kontur, Bemassungen: Peter 07.11.)
        case BSPLINE: case SPIRO: case PTS2ELLIPSE: case ELLIPSE_5PTS: case INTERPOLATE_POINTS: case GEARS:
        case DASHED_STROKE: case MEASURE_SEGMENTS:
            return true;
        default:
            return false;
    }
}

bool speichern(std::string_view id)
{
    // Hirch-CAM-Zeichnung, SVG (mit Hirch-Angaben, normal, optimiert), PDF, EPS, DXF R12/R14, HPGL
    static Liste const erlaubt{
        "org.inkscape.output.svg.hirch", "org.inkscape.output.svg.inkscape", "org.inkscape.output.svg.plain",
        "org.inkscape.output.scour_inkscape", "org.inkscape.output.pdf.cairorenderer", "org.inkscape.print.eps.cairo",
        "org.inkscape.output.dxf_twelve", "org.ekips.output.dxf_outlines", "org.ekips.output.hpgl_output",
    };
    return drin(erlaubt, id);
}

bool einstellungsseite(int id)
{
    using namespace Inkscape::UI::Dialog;
    switch (id) {
        // Seiten der entfernten Werkzeuge
        case PREFS_PAGE_TOOLS_SHAPES_3DBOX: case PREFS_PAGE_TOOLS_CALLIGRAPHY: case PREFS_PAGE_TOOLS_GRADIENT:
        case PREFS_PAGE_TOOLS_DROPPER: case PREFS_PAGE_TOOLS_PAINTBUCKET: case PREFS_PAGE_TOOLS_TWEAK:
        case PREFS_PAGE_TOOLS_SPRAY: case PREFS_PAGE_TOOLS_CONNECTOR: case PREFS_PAGE_TOOLS_ZOOM:
        // «Leisten» holte alle ausgeblendeten Werkzeuge zurück; Farben gibt es in Hirch CAM nicht;
        // die Befehlspalette hat keine Taste mehr
        case PREFS_PAGE_UI_TOOLBARS: case PREFS_PAGE_UI_COLOR_PICKERS: case PREFS_PAGE_COMMAND_PALETTE:
        case PREFS_PAGE_IO_CMS: case PREFS_PAGE_IO_OPENCLIPART:
            return false;
        default:
            return true;
    }
}

} // namespace HirchErlaubt
