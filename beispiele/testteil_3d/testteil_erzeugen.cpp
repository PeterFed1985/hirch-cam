// Testteil für das Probefräsen in Hartschaum (Hirch CNC, 3D zweiseitig): 180 × 180 × 39 mm (passt in 50 mm Hartschaum).
// Erzeugt testteil_hartschaum.step. Bauen und laufen lassen:
//
//   g++ -std=c++17 -I/usr/include/opencascade testteil_erzeugen.cpp -o testteil_erzeugen \
//       -lTKSTEP -lTKXSBase -lTKBRep -lTKernel -lTKMath -lTKG3d -lTKG2d -lTKGeomBase \
//       -lTKTopAlgo -lTKPrim -lTKBO -lTKFillet -lTKGeomAlgo -lTKShHealing -lTKBool
//   ./testteil_erzeugen ../../hirch_cam/daten/testteil_hartschaum.step   (Paket: Reiter 3D › «Testteil laden»)
//
// Oberseite (Seite 1):
//   A Kuppel Ø 70, 14 hoch            – Schlichten parallel / Höhenlinien / kombiniert, Fräsergebnis
//   B Block 50 × 35 × 14, oben R3     – senkrechte Wände, scharfe Innenecke unten (Kehlen nachfahren),
//                                        waagrechte Fläche oben (Waagrechte Flächen)
//   C Treppe 3 Stufen à 5 mm          – waagrechte Flächen, Innenecken, Höhenlinien
//   D Rinne R2 (halbrund)             – enger als ein Kugelfräser Ø 6: Nur Restmaterial / Kehlen mit Ø 3
//   E Tasche 40 × 25 × 10, Ecken R4   – Bohrungen und Taschen erkennen (Tasche)
//   F Durchgangsbohrung Ø 8           – erkennen, durch beide Seiten
//   G Sackloch Ø 6, 8 tief            – erkennen von oben
// Unterseite (Seite 2):
//   H Mulde Ø 60, 6 tief              – 3D-Schlichten Seite 2
//   I Tasche 40 × 20 × 8              – Taschen von unten erkennen
//   J Sackloch Ø 6, 10 tief           – Bohrung von unten erkennen
// Aussen: Ecken R15, senkrechte Seiten – Spalt um das Modell, Haut statt durchfräsen, Haltestege.
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <STEPControl_Writer.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <BRep_Tool.hxx>
#include <gp_Ax2.hxx>
#include <cmath>
#include <cstdio>

static TopoDS_Shape kasten(double x0, double y0, double z0, double dx, double dy, double dz)
{
    return BRepPrimAPI_MakeBox(gp_Pnt(x0, y0, z0), dx, dy, dz).Shape();
}

/// senkrechte Kanten eines Quaders runden (Eckradius in der Draufsicht)
static TopoDS_Shape senkrecht_runden(TopoDS_Shape s, double r)
{
    BRepFilletAPI_MakeFillet f(s);
    for (TopExp_Explorer ex(s, TopAbs_EDGE); ex.More(); ex.Next()) {
        TopoDS_Edge e = TopoDS::Edge(ex.Current());
        TopoDS_Vertex a, b;
        TopExp::Vertices(e, a, b);
        gp_Pnt pa = BRep_Tool::Pnt(a), pb = BRep_Tool::Pnt(b);
        if (std::abs(pa.X() - pb.X()) < 1e-6 && std::abs(pa.Y() - pb.Y()) < 1e-6) f.Add(r, e);
    }
    return f.Shape();
}

/// obere waagrechte Kanten (auf Höhe z) runden
static TopoDS_Shape oben_runden(TopoDS_Shape s, double z, double r)
{
    BRepFilletAPI_MakeFillet f(s);
    for (TopExp_Explorer ex(s, TopAbs_EDGE); ex.More(); ex.Next()) {
        TopoDS_Edge e = TopoDS::Edge(ex.Current());
        TopoDS_Vertex a, b;
        TopExp::Vertices(e, a, b);
        gp_Pnt pa = BRep_Tool::Pnt(a), pb = BRep_Tool::Pnt(b);
        if (std::abs(pa.Z() - z) < 1e-6 && std::abs(pb.Z() - z) < 1e-6) f.Add(r, e);
    }
    return f.Shape();
}

int main(int argc, char **argv)
{
    const double B = 180, H = 180, D = 24;           // Grundplatte, Oberseite auf Z 24
    TopoDS_Shape k = senkrecht_runden(kasten(0, 0, 0, B, H, D), 15);

    // A Kuppel: Kugelkappe Ø 70, 14 hoch, Mitte (50, 50)
    {
        double a = 35, h = 14, R = (a * a + h * h) / (2 * h);
        TopoDS_Shape kugel = BRepPrimAPI_MakeSphere(gp_Pnt(50, 50, D + h - R), R).Shape();
        TopoDS_Shape kappe = BRepAlgoAPI_Common(kugel, kasten(0, 0, D, B, H, 50)).Shape();
        k = BRepAlgoAPI_Fuse(k, kappe).Shape();
    }
    // B Block 50 × 35 × 14, obere Kanten R3
    k = BRepAlgoAPI_Fuse(k, oben_runden(kasten(105, 25, D, 50, 35, 14), D + 14, 3)).Shape();
    // C Treppe: drei Stufen à 5 mm
    k = BRepAlgoAPI_Fuse(k, kasten(105, 110, D, 60, 55, 5)).Shape();
    k = BRepAlgoAPI_Fuse(k, kasten(125, 110, D, 40, 55, 10)).Shape();
    k = BRepAlgoAPI_Fuse(k, kasten(145, 110, D, 20, 55, 15)).Shape();
    // D Rinne R2 entlang X (y = 88), halbrund in der Oberseite
    k = BRepAlgoAPI_Cut(k, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(100, 88, D), gp_Dir(1, 0, 0)), 2, 70).Shape()).Shape();
    // E Tasche 40 × 25 × 10, Ecken R4 (von oben)
    k = BRepAlgoAPI_Cut(k, senkrecht_runden(kasten(20, 115, D - 10, 40, 25, 11), 4)).Shape();
    // F Durchgangsbohrung Ø 8
    k = BRepAlgoAPI_Cut(k, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(25, 92, -1), gp_Dir(0, 0, 1)), 4, D + 2).Shape()).Shape();
    // G Sackloch Ø 6, 8 tief (von oben)
    k = BRepAlgoAPI_Cut(k, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(75, 92, D - 8), gp_Dir(0, 0, 1)), 3, 9).Shape()).Shape();
    // H Mulde unten: Kugelkappe Ø 60, 6 tief, Mitte (50, 50)
    {
        double a = 30, h = 6, R = (a * a + h * h) / (2 * h);
        TopoDS_Shape kugel = BRepPrimAPI_MakeSphere(gp_Pnt(50, 50, h - R), R).Shape();
        k = BRepAlgoAPI_Cut(k, BRepAlgoAPI_Common(kugel, kasten(0, 0, 0, B, H, 50)).Shape()).Shape();
    }
    // I Tasche unten 40 × 20 × 8 (unter dem Block B)
    k = BRepAlgoAPI_Cut(k, kasten(110, 32, -1, 40, 20, 9)).Shape();
    // J Sackloch Ø 6, 10 tief (von unten)
    k = BRepAlgoAPI_Cut(k, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(155, 92, -1), gp_Dir(0, 0, 1)), 3, 11).Shape()).Shape();

    STEPControl_Writer w;
    w.Transfer(k, STEPControl_AsIs);
    return w.Write(argc > 1 ? argv[1] : "testteil_hartschaum.step") == IFSelect_RetDone ? 0 : 1;
}
