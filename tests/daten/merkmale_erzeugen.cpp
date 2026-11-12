// Erzeugt tests/daten/merkmale.step (Testkörper für test_bohrungen_und_taschen_aus_step):
// Platte 80 × 60 × 20 mit Durchgangsbohrung Ø 8, Sackloch Ø 6 (8 tief), Tasche 20 × 14 × 6
// und einer zur Seite offenen Stufe (keine Tasche).
//
//   g++ -std=c++17 -I/usr/include/opencascade merkmale_erzeugen.cpp -o merkmale_erzeugen \
//       -lTKSTEP -lTKXSBase -lTKBRep -lTKernel -lTKMath -lTKG3d -lTKG2d -lTKGeomBase \
//       -lTKTopAlgo -lTKPrim -lTKBO -lTKGeomAlgo -lTKShHealing -lTKBool
//   ./merkmale_erzeugen merkmale.step
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <STEPControl_Writer.hxx>
#include <gp_Ax2.hxx>

int main(int, char **argv)
{
    TopoDS_Shape k = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 80, 60, 20).Shape();
    auto weg = [&](TopoDS_Shape const &s) { k = BRepAlgoAPI_Cut(k, s).Shape(); };
    weg(BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(15, 15, -1), gp_Dir(0, 0, 1)), 4, 22).Shape());  // durch
    weg(BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(15, 45, 12), gp_Dir(0, 0, 1)), 3, 9).Shape());   // Sackloch
    weg(BRepPrimAPI_MakeBox(gp_Pnt(40, 20, 14), 20, 14, 7).Shape());                            // Tasche
    weg(BRepPrimAPI_MakeBox(gp_Pnt(70, -1, 15), 11, 62, 6).Shape());                            // Stufe
    STEPControl_Writer w;
    w.Transfer(k, STEPControl_AsIs);
    return w.Write(argv[1]) == IFSelect_RetDone ? 0 : 1;
}
