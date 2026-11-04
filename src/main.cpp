#include "rfsim/Geometry.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char* argv[]) {
    const std::filesystem::path output =
        argc > 1 ? argv[1] : "artifacts/rf_plate.step";

    std::error_code error;
    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path(), error);
        if (error) {
            std::cerr << "Cannot create output directory: " << error.message() << '\n';
            return 1;
        }
    }

    try {
        rfsim::writeStep(rfsim::makePlateWithHole({}), output);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }

    std::cout << "Created plate-with-hole model: " << output << '\n';
    std::cout << "Units: mm; plate: 40 x 20 x 1.6; hole radius: 3 at (20, 10).\n";
    return 0;
}
