#pragma once

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Wire.hxx>

#include <filesystem>
#include <string>
#include <vector>

namespace rfsim {

enum class SolidKind { PlateWithHole, Box, Cylinder, Imported, Curve, Surface };
enum class MaterialKind { PerfectConductor, Dielectric };

struct Point3D {
    double x;
    double y;
    double z;
};

struct SolidObject {
    std::string name = "Plate";
    std::string id;
    std::string featureType;
    std::vector<std::string> inputFeatureIds;
    std::string materialId;
    SolidKind kind = SolidKind::PlateWithHole;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double rotationXDegrees = 0.0;
    double rotationYDegrees = 0.0;
    double rotationZDegrees = 0.0;
    double length = 40.0;
    double width = 20.0;
    double height = 1.6;
    double radius = 5.0;
    double holeRadius = 3.0;
    MaterialKind material = MaterialKind::PerfectConductor;
    double relativePermittivity = 4.4;
    double conductivity = 0.0;
    double lossTangent = 0.0;
    std::vector<Point3D> curvePoints;
    TopoDS_Shape customShape;
};

struct PlateParameters {
    double length = 40.0;
    double width = 20.0;
    double thickness = 1.6;
    double holeRadius = 3.0;
};

TopoDS_Shape makePlateWithHole(const PlateParameters& parameters);
TopoDS_Shape makeSolid(const SolidObject& object);
TopoDS_Wire makePolylineCurve(const std::vector<Point3D>& points);
TopoDS_Shape sweepCurveToSurface(const TopoDS_Wire& profile,
                                 const TopoDS_Wire& spine);
TopoDS_Solid sweepFaceToSolid(const TopoDS_Face& profile,
                              const TopoDS_Wire& spine);
TopoDS_Solid solidifyClosedShell(const TopoDS_Shell& shell);
bool isClosedSolid(const TopoDS_Shape& shape);
TopoDS_Shape readStep(const std::filesystem::path& input);
TopoDS_Shape fuseShapes(const TopoDS_Shape& first, const TopoDS_Shape& second);
TopoDS_Shape cutShapes(const TopoDS_Shape& first, const TopoDS_Shape& second);
bool isValidShape(const TopoDS_Shape& shape);
void writeStep(const TopoDS_Shape& shape, const std::filesystem::path& output);
void writeStl(const TopoDS_Shape& shape, const std::filesystem::path& output);

}  // namespace rfsim
