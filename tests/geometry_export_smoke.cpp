#include "rfsim/Geometry.hpp"

#include <BRepBuilderAPI_MakeFace.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <filesystem>
#include <stdexcept>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        return 2;
    }
    const std::filesystem::path output(argv[1]);
    std::filesystem::create_directories(output);

    rfsim::SolidObject plate;
    rfsim::writeStl(rfsim::makeSolid(plate), output / "plate.stl");

    rfsim::SolidObject box;
    box.kind = rfsim::SolidKind::Box;
    box.name = "Box";
    box.x = 45.0;
    rfsim::writeStl(rfsim::makeSolid(box), output / "box.stl");

    rfsim::SolidObject cylinder;
    cylinder.kind = rfsim::SolidKind::Cylinder;
    cylinder.name = "Cylinder";
    cylinder.x = 60.0;
    rfsim::writeStl(rfsim::makeSolid(cylinder), output / "cylinder.stl");

    for (const auto& file : {"plate.stl", "box.stl", "cylinder.stl"}) {
        if (!std::filesystem::exists(output / file) ||
            std::filesystem::file_size(output / file) < 84) {
            throw std::runtime_error("Expected a non-empty binary STL export.");
        }
    }

    rfsim::SolidObject first;
    first.kind = rfsim::SolidKind::Box;
    first.length = first.width = first.height = 10.0;
    rfsim::SolidObject second = first;
    second.x = 5.0;
    const auto firstShape = rfsim::makeSolid(first);
    const auto secondShape = rfsim::makeSolid(second);
    if (!rfsim::isValidShape(rfsim::fuseShapes(firstShape, secondShape)) ||
        !rfsim::isValidShape(rfsim::cutShapes(firstShape, secondShape))) {
        throw std::runtime_error("Boolean operation produced an invalid shape.");
    }

    const auto spine = rfsim::makePolylineCurve(
        {{0.0, 0.0, 0.0}, {0.0, 0.0, 12.0}});
    const auto lineProfile = rfsim::makePolylineCurve(
        {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}});
    const auto sweptSurface =
        rfsim::sweepCurveToSurface(lineProfile, spine);
    if (!rfsim::isValidShape(sweptSurface) ||
        rfsim::isClosedSolid(sweptSurface)) {
        throw std::runtime_error("Curve sweep did not produce a valid surface.");
    }

    const auto faceProfileWire = rfsim::makePolylineCurve(
        {{-1.0, -1.0, 0.0}, {1.0, -1.0, 0.0}, {1.0, 1.0, 0.0},
         {-1.0, 1.0, 0.0}, {-1.0, -1.0, 0.0}});
    const TopoDS_Face faceProfile =
        BRepBuilderAPI_MakeFace(faceProfileWire).Face();
    const TopoDS_Solid sweptSolid =
        rfsim::sweepFaceToSolid(faceProfile, spine);
    if (!rfsim::isClosedSolid(sweptSolid)) {
        throw std::runtime_error("Face sweep did not produce a closed solid.");
    }
    rfsim::writeStl(sweptSolid, output / "sweep-solid.stl");

    TopExp_Explorer shellExplorer(sweptSolid, TopAbs_SHELL);
    if (!shellExplorer.More() ||
        !rfsim::isClosedSolid(
            rfsim::solidifyClosedShell(TopoDS::Shell(shellExplorer.Current())))) {
        throw std::runtime_error("Closed-shell solidification failed.");
    }
    TopExp_Explorer openShellExplorer(sweptSurface, TopAbs_SHELL);
    if (!openShellExplorer.More()) {
        throw std::runtime_error("Curve sweep did not expose a shell for validation.");
    }
    bool rejectedOpenShell = false;
    try {
        rfsim::solidifyClosedShell(
            TopoDS::Shell(openShellExplorer.Current()));
    } catch (const std::invalid_argument&) {
        rejectedOpenShell = true;
    }
    if (!rejectedOpenShell) {
        throw std::runtime_error("An open shell must not be silently solidified.");
    }

    first.rotationZDegrees = 30.0;
    const auto rotatedShape = rfsim::makeSolid(first);
    const auto stepFile = output / "roundtrip.step";
    rfsim::writeStep(rotatedShape, stepFile);
    if (!rfsim::isValidShape(rfsim::readStep(stepFile))) {
        throw std::runtime_error("STEP import round-trip produced an invalid shape.");
    }
    return 0;
}
