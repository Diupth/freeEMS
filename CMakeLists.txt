cmake_minimum_required(VERSION 3.20)
project(rf_sim_prototype LANGUAGES CXX)

include(CTest)
find_package(OpenCASCADE REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_library(rf_geometry STATIC src/Geometry.cpp)
target_compile_features(rf_geometry PUBLIC cxx_std_17)
target_include_directories(rf_geometry
    PUBLIC include ${OpenCASCADE_INCLUDE_DIR}
)
target_link_libraries(rf_geometry PUBLIC ${OpenCASCADE_LIBRARIES})

add_executable(rf_sim_prototype src/main.cpp)
target_link_libraries(rf_sim_prototype PRIVATE rf_geometry)

add_executable(geometry_export_smoke tests/geometry_export_smoke.cpp)
target_link_libraries(geometry_export_smoke PRIVATE rf_geometry)

add_executable(rf_sim_gui
    src/gui/main.cpp
    src/gui/ModelView.cpp
    src/gui/OemProject.cpp
)
target_link_libraries(rf_sim_gui PRIVATE rf_geometry Qt6::Widgets)
add_executable(create_oem_samples
    tests/create_oem_samples.cpp
    src/gui/OemProject.cpp
)
target_include_directories(create_oem_samples PRIVATE src/gui)
target_link_libraries(create_oem_samples PRIVATE rf_geometry Qt6::Widgets)
if(WIN32)
    set(OPENEMS_PYTHON_DEFAULT
        "${CMAKE_BINARY_DIR}/openems-install/venv/Scripts/python.exe")
else()
    set(OPENEMS_PYTHON_DEFAULT
        "${CMAKE_BINARY_DIR}/openems-install/venv/bin/python")
endif()
target_compile_definitions(rf_sim_gui PRIVATE
    RF_SIM_SOLVER_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/scripts/openems_patch.py"
    RF_SIM_OEM_PACKAGE_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/scripts/oem_package.py"
    RF_SIM_PYTHON_DEFAULT="${OPENEMS_PYTHON_DEFAULT}"
    RF_SIM_SMOKE_OUTPUT="${CMAKE_BINARY_DIR}/artifacts/gui-smoke"
    RF_SIM_AXIS_PORT_SMOKE_OUTPUT="${CMAKE_BINARY_DIR}/artifacts/axis-port-smoke"
    RF_SIM_INSTRUCTION_FILE="${CMAKE_CURRENT_SOURCE_DIR}/instruction.md"
)
target_compile_definitions(create_oem_samples PRIVATE
    RF_SIM_OEM_PACKAGE_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/scripts/oem_package.py"
    RF_SIM_PYTHON_DEFAULT="${OPENEMS_PYTHON_DEFAULT}"
)

if(BUILD_TESTING)
    add_test(
        NAME step_export_smoke
        COMMAND rf_sim_prototype "${CMAKE_BINARY_DIR}/test-output/rf_plate.step"
    )
    add_test(
        NAME geometry_export_smoke
        COMMAND geometry_export_smoke "${CMAKE_BINARY_DIR}/test-output/cad-solids"
    )
    add_test(NAME gui_smoke COMMAND rf_sim_gui --smoke-test)
    add_test(NAME gui_draw_smoke COMMAND rf_sim_gui --smoke-draw)
    add_test(NAME gui_curve_draw_smoke COMMAND rf_sim_gui --smoke-curve-draw)
    add_test(NAME gui_direct_pick_smoke COMMAND rf_sim_gui --smoke-direct-pick)
    add_test(NAME gui_project_io_smoke COMMAND rf_sim_gui --smoke-project-io)
    add_test(NAME gui_empty_startup_smoke COMMAND rf_sim_gui --smoke-empty-startup)
    add_test(
        NAME gui_waveguide_project_smoke
        COMMAND rf_sim_gui --smoke-open-project
                "${CMAKE_CURRENT_SOURCE_DIR}/waveguide.oem"
    )
    add_test(
        NAME gui_antenna_project_smoke
        COMMAND rf_sim_gui --smoke-open-project
                "${CMAKE_CURRENT_SOURCE_DIR}/antenna.oem"
    )
    add_test(NAME gui_cancel_smoke COMMAND rf_sim_gui --smoke-cancel)
    add_test(NAME gui_x_port_smoke COMMAND rf_sim_gui --smoke-x-port)
    set_tests_properties(
        gui_smoke gui_draw_smoke gui_curve_draw_smoke gui_project_io_smoke
        gui_empty_startup_smoke
        gui_waveguide_project_smoke gui_antenna_project_smoke
        gui_cancel_smoke
        gui_direct_pick_smoke gui_x_port_smoke
        PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
    if(EXISTS "${OPENEMS_PYTHON_DEFAULT}")
        add_test(
            NAME gui_waveguide_oem_run_smoke
            COMMAND rf_sim_gui --smoke-open-project-run
                    "${CMAKE_CURRENT_SOURCE_DIR}/waveguide.oem"
        )
        set_tests_properties(
            gui_waveguide_oem_run_smoke
            PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        )
        set(REFERENCE_WR90_DIR "${CMAKE_BINARY_DIR}/artifacts/reference_wr90")
        set(REFERENCE_WR90_TE20_DIR "${CMAKE_BINARY_DIR}/artifacts/reference_wr90_te20")
        set(REFERENCE_ANTENNA_DIR "${CMAKE_BINARY_DIR}/artifacts/reference_antenna")
        add_test(
            NAME reference_wr90_run
            COMMAND "${OPENEMS_PYTHON_DEFAULT}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/openems_wr90.py"
                    --output "${REFERENCE_WR90_DIR}" --engine cpu
                    --mesh-div 30 --timesteps 20000
        )
        add_test(
            NAME reference_wr90_te20_run
            COMMAND "${OPENEMS_PYTHON_DEFAULT}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/openems_wr90.py"
                    --output "${REFERENCE_WR90_TE20_DIR}" --engine cpu
                    --mode TE20 --f-start-ghz 13.5 --f-stop-ghz 15
                    --mesh-div 30 --timesteps 20000
        )
        add_test(
            NAME reference_resource_guard
            COMMAND "${OPENEMS_PYTHON_DEFAULT}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_resource_guard.py"
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/openems_patch.py"
        )
        add_test(
            NAME solver_sweep_polyhedron_smoke
            COMMAND "${OPENEMS_PYTHON_DEFAULT}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_solver_polyhedron.py"
                    --stl "${CMAKE_BINARY_DIR}/test-output/cad-solids/sweep-solid.stl"
        )
        add_test(
            NAME reference_antenna_run
            COMMAND "${OPENEMS_PYTHON_DEFAULT}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/openems_patch.py"
                    --output "${REFERENCE_ANTENNA_DIR}" --engine cpu
                    --mesh-div 25 --timesteps 30000
                    --f-start-ghz 1 --f-stop-ghz 3 --frequency-points 51
        )
        add_test(
            NAME reference_results_validate
            COMMAND "${OPENEMS_PYTHON_DEFAULT}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validate_reference_results.py"
                    --wr90 "${REFERENCE_WR90_DIR}"
                    --wr90-te20 "${REFERENCE_WR90_TE20_DIR}"
                    --antenna "${REFERENCE_ANTENNA_DIR}"
        )
        set_tests_properties(
            reference_wr90_run reference_wr90_te20_run reference_antenna_run
            reference_resource_guard
            PROPERTIES RESOURCE_LOCK openems_reference_cpu
        )
        set_tests_properties(
            reference_results_validate
            PROPERTIES DEPENDS "reference_wr90_run;reference_wr90_te20_run;reference_antenna_run"
        )
        set_tests_properties(
            solver_sweep_polyhedron_smoke
            PROPERTIES DEPENDS geometry_export_smoke
        )
    endif()
endif()

if(APPLE)
    target_compile_options(rf_geometry PRIVATE -Wno-deprecated-declarations)
endif()

find_program(GMSH_EXECUTABLE gmsh)
if(GMSH_EXECUTABLE)
    set(PROTOTYPE_OUTPUT_DIR "${CMAKE_BINARY_DIR}/artifacts")
    set(PROTOTYPE_STEP "${PROTOTYPE_OUTPUT_DIR}/rf_plate.step")
    set(PROTOTYPE_MESH "${PROTOTYPE_OUTPUT_DIR}/rf_plate.msh")

    add_custom_command(
        OUTPUT "${PROTOTYPE_STEP}"
        COMMAND $<TARGET_FILE:rf_sim_prototype> "${PROTOTYPE_STEP}"
        DEPENDS rf_sim_prototype
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${PROTOTYPE_MESH}"
        COMMAND "${GMSH_EXECUTABLE}" "${PROTOTYPE_STEP}" -3
                -format msh4 -o "${PROTOTYPE_MESH}" -v 3
        DEPENDS "${PROTOTYPE_STEP}"
        VERBATIM
    )
    add_custom_target(prototype_mesh DEPENDS "${PROTOTYPE_MESH}")
endif()
