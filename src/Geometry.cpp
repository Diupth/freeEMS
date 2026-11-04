#include "rfsim/Geometry.hpp"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <BRep_Tool.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Writer.hxx>
#include <STEPControl_Reader.hxx>
#include <StlAPI_Writer.hxx>
#include <GProp_GProps.hxx>
#include <gp_Ax2.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_Trsf.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <cmath>
#include <limits>

#include <stdexcept>

namespace rfsim {
namespace {
constexpr double pi = 3.14159265358979323846;
}

TopoDS_Shape makePlateWithHole(const PlateParameters& parameters) {
    if (parameters.length <= 0.0 || parameters.width <= 0.0 ||
        parameters.thickness <= 0.0 || parameters.holeRadius <= 0.0 ||
        parameters.holeRadius * 2.0 >= parameters.length ||
        parameters.holeRadius * 2.0 >= parameters.width) {
        throw std::invalid_argument("Plate dimensions and hole radius are invalid.");
    }

    const TopoDS_Shape plate =
        BRepPrimAPI_MakeBox(parameters.length, parameters.width,
                            parameters.thickness).Shape();
    const TopoDS_Shape hole =
        BRepPrimAPI_MakeCylinder(
            gp_Ax2(gp_Pnt(parameters.length / 2.0, parameters.width / 2.0, 0.0),
                   gp_Dir(0.0, 0.0, 1.0)),
            parameters.holeRadius, parameters.thickness).Shape();
    const TopoDS_Shape model = BRepAlgoAPI_Cut(plate, hole).Shape();
    if (!BRepCheck_Analyzer(model).IsValid()) {
        throw std::runtime_error("Generated CAD model is not topologically valid.");
    }
    return model;
}

TopoDS_Shape makeSolid(const SolidObject& object) {
    if (!std::isfinite(object.x) || !std::isfinite(object.y) ||
        !std::isfinite(object.z) || !std::isfinite(object.rotationXDegrees) ||
        !std::isfinite(object.rotationYDegrees) ||
        !std::isfinite(object.rotationZDegrees)) {
        throw std::invalid_argument("Solid position and rotation must be finite.");
    }
    TopoDS_Shape shape;
    switch (object.kind) {
    case SolidKind::PlateWithHole:
        shape = makePlateWithHole(
            {object.length, object.width, object.height, object.holeRadius});
        break;
    case SolidKind::Box:
        if (object.length <= 0.0 || object.width <= 0.0 || object.height <= 0.0) {
            throw std::invalid_argument("Box dimensions must be positive.");
        }
        shape = BRepPrimAPI_MakeBox(object.length, object.width, object.height).Shape();
        break;
    case SolidKind::Cylinder:
        if (object.radius <= 0.0 || object.height <= 0.0) {
            throw std::invalid_argument("Cylinder radius and height must be positive.");
        }
        shape = BRepPrimAPI_MakeCylinder(object.radius, object.height).Shape();
        break;
    case SolidKind::Imported:
    case SolidKind::Curve:
    case SolidKind::Surface:
        if (object.customShape.IsNull()) {
            throw std::invalid_argument("Imported geometry has no valid shape.");
        }
        shape = object.customShape;
        break;
    }

    if (object.rotationXDegrees != 0.0 || object.rotationYDegrees != 0.0 ||
        object.rotationZDegrees != 0.0) {
        gp_Trsf rotation;
        rotation.SetRotation(
            gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(1.0, 0.0, 0.0)),
            object.rotationXDegrees * pi / 180.0);
        gp_Trsf rotateY;
        rotateY.SetRotation(
            gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 1.0, 0.0)),
            object.rotationYDegrees * pi / 180.0);
        gp_Trsf rotateZ;
        rotateZ.SetRotation(
            gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)),
            object.rotationZDegrees * pi / 180.0);
        rotation = rotateZ * rotateY * rotation;
        shape = BRepBuilderAPI_Transform(shape, rotation, true).Shape();
    }
    gp_Trsf translation;
    translation.SetTranslation(gp_Vec(object.x, object.y, object.z));
    shape = BRepBuilderAPI_Transform(shape, translation, true).Shape();
    if (!BRepCheck_Analyzer(shape).IsValid()) {
        throw std::runtime_error("Generated CAD solid is not topologically valid.");
    }
    return shape;
}

TopoDS_Wire makePolylineCurve(const std::vector<Point3D>& points) {
    if (points.size() < 2) {
        throw std::invalid_argument("A curve needs at least two points.");
    }
    BRepBuilderAPI_MakeWire wire;
    for (size_t i = 1; i < points.size(); ++i) {
        const auto& start = points[i - 1];
        const auto& stop = points[i];
        if (!std::isfinite(start.x) || !std::isfinite(start.y) ||
            !std::isfinite(start.z) || !std::isfinite(stop.x) ||
            !std::isfinite(stop.y) || !std::isfinite(stop.z)) {
            throw std::invalid_argument("Curve coordinates must be finite.");
        }
        const TopoDS_Edge edge = BRepBuilderAPI_MakeEdge(
            gp_Pnt(start.x, start.y, start.z),
            gp_Pnt(stop.x, stop.y, stop.z));
        wire.Add(edge);
    }
    if (!wire.IsDone() || wire.Wire().IsNull() ||
        !BRepCheck_Analyzer(wire.Wire()).IsValid()) {
        throw std::runtime_error("Failed to build a valid polyline curve.");
    }
    return wire.Wire();
}

TopoDS_Shape sweepCurveToSurface(const TopoDS_Wire& profile,
                                 const TopoDS_Wire& spine) {
    if (profile.IsNull() || spine.IsNull()) {
        throw std::invalid_argument("A sweep needs a profile curve and a path.");
    }
    BRepOffsetAPI_MakePipe operation(spine, profile);
    operation.Build();
    if (!operation.IsDone() || !isValidShape(operation.Shape())) {
        throw std::runtime_error("Curve sweep failed to create a valid surface.");
    }
    return operation.Shape();
}

TopoDS_Solid sweepFaceToSolid(const TopoDS_Face& profile,
                              const TopoDS_Wire& spine) {
    if (profile.IsNull() || spine.IsNull()) {
        throw std::invalid_argument("A solid sweep needs a face profile and a path.");
    }
    BRepOffsetAPI_MakePipe operation(spine, profile);
    operation.Build();
    if (!operation.IsDone() || !isClosedSolid(operation.Shape())) {
        throw std::runtime_error(
            "Face sweep did not create a closed solid with positive volume.");
    }
    TopExp_Explorer solids(operation.Shape(), TopAbs_SOLID);
    if (!solids.More()) {
        throw std::runtime_error("Face sweep result contains no solid.");
    }
    return TopoDS::Solid(solids.Current());
}

TopoDS_Solid solidifyClosedShell(const TopoDS_Shell& shell) {
    if (shell.IsNull() || !BRep_Tool::IsClosed(shell) ||
        !BRepCheck_Analyzer(shell).IsValid()) {
        throw std::invalid_argument(
            "Only a valid closed shell can be converted to a solid.");
    }
    BRepBuilderAPI_MakeSolid operation(shell);
    operation.Build();
    if (!operation.IsDone() || !isClosedSolid(operation.Solid())) {
        throw std::runtime_error(
            "Closed shell did not produce a valid solid with positive volume.");
    }
    return operation.Solid();
}

bool isClosedSolid(const TopoDS_Shape& shape) {
    if (shape.IsNull() || !BRepCheck_Analyzer(shape).IsValid()) return false;
    bool foundSolid = false;
    for (TopExp_Explorer explorer(shape, TopAbs_SOLID); explorer.More();
         explorer.Next()) {
        GProp_GProps properties;
        BRepGProp::VolumeProperties(explorer.Current(), properties);
        const double volume = properties.Mass();
        if (!std::isfinite(volume) || volume <= 0.0) return false;
        foundSolid = true;
    }
    return foundSolid;
}

TopoDS_Shape readStep(const std::filesystem::path& input) {
    STEPControl_Reader reader;
    if (reader.ReadFile(input.string().c_str()) != IFSelect_RetDone ||
        reader.TransferRoots() <= 0) {
        throw std::runtime_error("Failed to read STEP geometry: " + input.string());
    }
    const TopoDS_Shape shape = reader.OneShape();
    if (!isValidShape(shape)) {
        throw std::runtime_error("Imported STEP geometry is invalid: " + input.string());
    }
    return shape;
}

TopoDS_Shape fuseShapes(const TopoDS_Shape& first, const TopoDS_Shape& second) {
    BRepAlgoAPI_Fuse operation(first, second);
    operation.Build();
    if (!operation.IsDone() || !isValidShape(operation.Shape())) {
        throw std::runtime_error("Boolean union failed or produced invalid geometry.");
    }
    return operation.Shape();
}

TopoDS_Shape cutShapes(const TopoDS_Shape& first, const TopoDS_Shape& second) {
    BRepAlgoAPI_Cut operation(first, second);
    operation.Build();
    if (!operation.IsDone() || !isValidShape(operation.Shape())) {
        throw std::runtime_error("Boolean subtraction failed or produced invalid geometry.");
    }
    return operation.Shape();
}

bool isValidShape(const TopoDS_Shape& shape) {
    return !shape.IsNull() && BRepCheck_Analyzer(shape).IsValid();
}

void writeStep(const TopoDS_Shape& shape, const std::filesystem::path& output) {
    STEPControl_Writer writer;
    if (writer.Transfer(shape, STEPControl_AsIs) != IFSelect_RetDone) {
        throw std::runtime_error("Failed to transfer the model to STEP.");
    }
    if (writer.Write(output.string().c_str()) != IFSelect_RetDone) {
        throw std::runtime_error("Failed to write STEP file: " + output.string());
    }
}

void writeStl(const TopoDS_Shape& shape, const std::filesystem::path& output) {
    if (!isClosedSolid(shape)) {
        throw std::invalid_argument(
            "The openEMS polyhedron export requires a valid closed solid.");
    }
    BRepMesh_IncrementalMesh mesh(shape, 0.15, false, 0.35, true);
    if (!mesh.IsDone()) {
        throw std::runtime_error("Failed to triangulate the CAD solid for the solver.");
    }
    StlAPI_Writer writer;
    writer.ASCIIMode() = Standard_False;
    if (!writer.Write(shape, output.string().c_str())) {
        throw std::runtime_error("Failed to write STL file: " + output.string());
    }
}

}  // namespace rfsim
