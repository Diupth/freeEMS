#!/usr/bin/env python3
"""Run an openEMS patch-antenna reference model and export S11 data."""

import argparse
import csv
import json
import shutil
import time
from pathlib import Path

import numpy as np
from CSXCAD import ContinuousStructure
from openEMS import openEMS
from openEMS.physical_constants import C0, EPS0
from openEMS.utilities import HDF5Dump


def write_touchstone_1port(
    path: Path, frequencies_hz: np.ndarray, s11: np.ndarray,
    reference_ohms: float,
) -> None:
    lines = [
        "[Version] 2.1",
        f"# Hz S RI R {reference_ohms:g}",
        "[Number of Ports] 1",
        f"[Number of Frequencies] {len(frequencies_hz)}",
        f"[Reference] {reference_ohms:g}",
        "[Network Data]",
    ]
    lines.extend(
        f"{float(frequency):.17g} {float(value.real):.17g} {float(value.imag):.17g}"
        for frequency, value in zip(frequencies_hz, s11)
    )
    lines.append("[End]")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", choices=("cpu", "gpu"), default="cpu")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scene", type=Path,
                        help="JSON scene containing closed STL solids in millimeters")
    parser.add_argument("--timesteps", type=int, default=30000)
    parser.add_argument("--mesh-div", type=int, default=30)
    parser.add_argument("--f-start-ghz", type=float, default=1.0)
    parser.add_argument("--f-stop-ghz", type=float, default=3.0)
    parser.add_argument("--frequency-points", type=int, default=201)
    parser.add_argument("--boundary", choices=("MUR", "PML_8"), default="MUR")
    parser.add_argument("--port-impedance", type=float, default=50.0)
    parser.add_argument("--substrate-width-mm", type=float, default=60.0)
    parser.add_argument("--substrate-length-mm", type=float, default=60.0)
    parser.add_argument("--substrate-thickness-mm", type=float, default=1.524)
    parser.add_argument("--patch-width-mm", type=float, default=32.0)
    parser.add_argument("--patch-length-mm", type=float, default=40.0)
    parser.add_argument("--feed-position-mm", type=float, default=-6.0)
    parser.add_argument("--substrate-permittivity", type=float, default=3.38)
    parser.add_argument("--substrate-conductivity-s-per-m", type=float, default=0.0)
    parser.add_argument("--substrate-loss-tangent", type=float, default=0.0)
    parser.add_argument("--max-cells", type=int, default=5_000_000)
    args = parser.parse_args()
    if (args.timesteps < 1000 or not 8 <= args.mesh_div <= 80 or
            args.f_start_ghz <= 0 or args.f_stop_ghz <= args.f_start_ghz or
            args.frequency_points < 2 or args.port_impedance <= 0):
        parser.error("invalid timestep, mesh, frequency, sample-count, or port settings")
    if args.max_cells < 1000:
        parser.error("--max-cells must be at least 1000")
    antenna_values = (
        args.substrate_width_mm,
        args.substrate_length_mm,
        args.substrate_thickness_mm,
        args.patch_width_mm,
        args.patch_length_mm,
        args.feed_position_mm,
        args.substrate_permittivity,
        args.substrate_conductivity_s_per_m,
        args.substrate_loss_tangent,
    )
    if (
        not all(np.isfinite(value) for value in antenna_values)
        or args.substrate_width_mm <= 0
        or args.substrate_length_mm <= 0
        or args.substrate_thickness_mm <= 0
        or args.patch_width_mm <= 0
        or args.patch_length_mm <= 0
        or args.patch_width_mm > args.substrate_width_mm
        or args.patch_length_mm > args.substrate_length_mm
        or abs(args.feed_position_mm) > args.patch_width_mm / 2
        or args.substrate_permittivity < 1
        or args.substrate_conductivity_s_per_m < 0
        or args.substrate_loss_tangent < 0
    ):
        parser.error("invalid antenna geometry or substrate material settings")

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    simulation_path = output / f"patch_{args.engine}_{time.time_ns()}"
    simulation_path.mkdir(parents=True, exist_ok=False)

    f_start_hz = args.f_start_ghz * 1.0e9
    f_stop_hz = args.f_stop_ghz * 1.0e9
    f0 = 0.5 * (f_start_hz + f_stop_hz)
    fc = 0.5 * (f_stop_hz - f_start_hz)
    substrate_width = args.substrate_width_mm
    substrate_length = args.substrate_length_mm
    substrate_thickness = args.substrate_thickness_mm
    patch_width = args.patch_width_mm
    patch_length = args.patch_length_mm
    feed_position = args.feed_position_mm
    mesh_resolution = C0 / f_stop_hz / 1e-3 / args.mesh_div
    near_field_z = substrate_thickness + 5.0
    nf2ff = None

    fdtd_options = {"NrTS": args.timesteps, "EndCriteria": 1e-4}
    fdtd = openEMS(**fdtd_options)
    fdtd.SetGaussExcite(f0, fc)
    fdtd.SetBoundaryCond([args.boundary] * 6)

    csx = ContinuousStructure()
    fdtd.SetCSX(csx)
    mesh = csx.GetGrid()
    mesh.SetDeltaUnit(1e-3)
    if args.scene:
        scene_path = args.scene.resolve()
        scene = json.loads(scene_path.read_text(encoding="utf-8"))
        bounds = scene["bounds_mm"]
        if len(bounds) != 6 or not scene.get("solids"):
            parser.error("scene must contain solids and six bounds_mm values")
        min_x, min_y, min_z, max_x, max_y, max_z = map(float, bounds)
        if not (min_x < max_x and min_y < max_y and min_z < max_z):
            parser.error("scene bounds must have positive size in all axes")
        if "port_start_mm" in scene and "port_stop_mm" in scene:
            port_start = np.asarray(scene["port_start_mm"], dtype=float)
            port_stop = np.asarray(scene["port_stop_mm"], dtype=float)
            port_direction = str(scene.get("port_axis", "z"))
        else:
            port_start = np.array([
                float(scene["port_x_mm"]), float(scene["port_y_mm"]),
                float(scene["port_z_start_mm"])
            ])
            port_stop = np.array([
                float(scene["port_x_mm"]), float(scene["port_y_mm"]),
                float(scene["port_z_stop_mm"])
            ])
            port_direction = "z"
        if (port_start.shape != (3,) or port_stop.shape != (3,) or
                not np.all(np.isfinite(port_start)) or
                not np.all(np.isfinite(port_stop)) or
                port_direction not in ("x", "y", "z")):
            parser.error("port start/stop must be finite 3D points with axis x, y, or z")
        port_axis_index = "xyz".index(port_direction)
        transverse = [axis for axis in range(3) if axis != port_axis_index]
        if (not port_start[port_axis_index] < port_stop[port_axis_index] or
                any(port_start[axis] != port_stop[axis] for axis in transverse)):
            parser.error("lumped port endpoints must vary only along its positive axis")
        margin = max(mesh_resolution * 8, 10.0)
        z_start = min(min_z - margin, port_start[2] - margin * 0.1,
                      port_stop[2] - margin * 0.1)
        z_stop = max(max_z + margin, port_start[2] + margin * 0.1,
                     port_stop[2] + margin * 0.1)
        mesh.AddLine(
            "x", sorted({min_x - margin, min_x, port_start[0], port_stop[0],
                         max_x, max_x + margin})
        )
        mesh.AddLine(
            "y", sorted({min_y - margin, min_y, port_start[1], port_stop[1],
                         max_y, max_y + margin})
        )
        mesh.AddLine(
            "z", sorted({z_start, min_z, max_z, port_start[2], port_stop[2], z_stop})
        )
        for index, solid in enumerate(scene["solids"]):
            stl_path = Path(solid["stl"]).resolve()
            if not stl_path.is_file():
                parser.error(f"solid STL file does not exist: {stl_path}")
            material_type = solid.get("material_type", "pec")
            if material_type == "pec":
                material = csx.AddMetal(f"cad_metal_{index}")
            elif material_type == "dielectric":
                epsilon_r = float(solid["relative_permittivity"])
                conductivity = float(solid.get("conductivity_s_per_m", 0.0))
                loss_tangent = float(solid.get("loss_tangent", 0.0))
                if epsilon_r <= 0 or conductivity < 0 or loss_tangent < 0:
                    parser.error(f"invalid dielectric parameters for {solid.get('name', index)}")
                electric_loss = (
                    conductivity + 2 * np.pi * f0 * EPS0 * epsilon_r * loss_tangent
                )
                material = csx.AddMaterial(
                    f"cad_dielectric_{index}",
                    epsilon=epsilon_r,
                    kappa=electric_loss,
                )
            else:
                parser.error(f"unsupported material type: {material_type}")
            polyhedron = material.AddPolyhedronReader(str(stl_path), priority=10)
            polyhedron.ReadFile()
            fdtd.AddEdges2Grid(
                dirs="xyz", properties=material, metal_edge_res=mesh_resolution / 2
            )
        port = fdtd.AddLumpedPort(
            1, args.port_impedance, port_start.tolist(), port_stop.tolist(),
            port_direction, 1.0, priority=5,
            edges2grid={"x": "yz", "y": "xz", "z": "xy"}[port_direction]
        )
        simulation_kind = "cad_scene"
    else:
        extent_x = max(substrate_width, patch_width, abs(feed_position) * 2)
        extent_y = max(substrate_length, patch_length)
        mesh.AddLine("x", [-max(100.0, extent_x * 1.5), max(100.0, extent_x * 1.5)])
        mesh.AddLine("y", [-max(100.0, extent_y * 1.5), max(100.0, extent_y * 1.5)])
        mesh.AddLine("z", [-50.0, max(100.0, substrate_thickness * 10)])
        patch = csx.AddMetal("patch")
        patch.AddBox(
            priority=10,
            start=[-patch_width / 2, -patch_length / 2, substrate_thickness],
            stop=[patch_width / 2, patch_length / 2, substrate_thickness],
        )
        fdtd.AddEdges2Grid(
            dirs="xy", properties=patch, metal_edge_res=mesh_resolution / 2
        )
        electric_loss = (
            args.substrate_conductivity_s_per_m
            + 2
            * np.pi
            * f0
            * EPS0
            * args.substrate_permittivity
            * args.substrate_loss_tangent
        )
        substrate = csx.AddMaterial(
            "substrate", epsilon=args.substrate_permittivity, kappa=electric_loss
        )
        substrate.AddBox(
            priority=0,
            start=[-substrate_width / 2, -substrate_length / 2, 0],
            stop=[substrate_width / 2, substrate_length / 2, substrate_thickness],
        )
        mesh.AddLine("z", np.linspace(0, substrate_thickness, 5))
        ground = csx.AddMetal("ground")
        ground.AddBox(
            priority=10,
            start=[-substrate_width / 2, -substrate_length / 2, 0],
            stop=[substrate_width / 2, substrate_length / 2, 0],
        )
        fdtd.AddEdges2Grid(dirs="xy", properties=ground)
        port = fdtd.AddLumpedPort(
            1, args.port_impedance, [feed_position, 0, 0],
            [feed_position, 0, substrate_thickness],
            "z", 1.0, priority=5, edges2grid="xy"
        )
        simulation_kind = "patch_reference"
        mesh.AddLine("z", [near_field_z])
        for name, dump_type in (("Ef_patch", 10), ("Hf_patch", 11)):
            dump = csx.AddDump(
                name, dump_type=dump_type, dump_mode=1, file_type=1,
                frequency=[f0]
            )
            dump.AddBox(
                [-substrate_width / 2, -substrate_length / 2, near_field_z],
                [substrate_width / 2, substrate_length / 2, near_field_z],
            )
    mesh.SmoothMeshLines("all", mesh_resolution, 1.4)
    cell_count = int(np.prod([
        max(0, len(mesh.GetLines(axis)) - 1) for axis in "xyz"
    ]))
    if cell_count <= 0:
        parser.error("mesh contains no cells")
    if cell_count > args.max_cells:
        parser.error(
            f"mesh requires {cell_count:,} cells, exceeding --max-cells "
            f"({args.max_cells:,}); coarsen the mesh or increase the limit"
        )
    if not args.scene:
        nf2ff = fdtd.CreateNF2FFBox()

    model_directory = output / "models"
    model_directory.mkdir(parents=True, exist_ok=True)
    model_path = model_directory / "openems.xml"
    fdtd.Write2XML(str(model_path))
    if args.scene:
        geometry_directory = model_directory / "geometry"
        geometry_directory.mkdir(parents=True, exist_ok=True)
        model_xml = model_path.read_text(encoding="utf-8")
        for index, solid in enumerate(scene["solids"]):
            source = Path(solid["stl"]).resolve()
            package_asset = geometry_directory / f"solid_{index}.stl"
            shutil.copyfile(source, package_asset)
            model_xml = model_xml.replace(
                str(source), f"geometry/solid_{index}.stl"
            )
        model_path.write_text(model_xml, encoding="utf-8")

    started = time.perf_counter()
    run_options = {"verbose": 2}
    if args.engine == "gpu":
        run_options["engine"] = "gpu"
    fdtd.Run(str(simulation_path), cleanup=False, **run_options)
    elapsed_s = time.perf_counter() - started

    frequencies = np.linspace(f_start_hz, f_stop_hz, args.frequency_points)
    port.CalcPort(str(simulation_path), frequencies)
    s11 = port.uf_ref / port.uf_inc
    s11_db = 20 * np.log10(np.maximum(np.abs(s11), np.finfo(float).tiny))
    s11_file = output / ("s11_scene.csv" if args.scene else f"s11_{args.engine}.csv")
    touchstone_path = output / (
        "s11_scene.s1p" if args.scene else "s11_antenna.s1p"
    )
    write_touchstone_1port(
        touchstone_path, frequencies, s11, args.port_impedance
    )
    with (output / f"s11_{args.engine}.csv").open("w", newline="") as result_file:
        writer = csv.writer(result_file)
        writer.writerow(("frequency_hz", "s11_real", "s11_imag", "s11_db"))
        writer.writerows(
            (float(frequency), float(value.real), float(value.imag), float(db))
            for frequency, value, db in zip(frequencies, s11, s11_db)
        )
    if args.scene:
        with s11_file.open("w", newline="") as result_file:
            writer = csv.writer(result_file)
            writer.writerow(("frequency_hz", "s11_real", "s11_imag", "s11_db"))
            writer.writerows(
                (float(frequency), float(value.real), float(value.imag), float(db))
                for frequency, value, db in zip(frequencies, s11, s11_db)
            )
        waveform_path = output / "waveform_scene.csv"
        with waveform_path.open("w", newline="") as waveform_file:
            writer = csv.writer(waveform_file)
            writer.writerow(("time_s", "incident_voltage_v", "reflected_voltage_v"))
            writer.writerows(
                (float(t), float(incident), float(reflected))
                for t, incident, reflected in zip(
                    port.u_time, port.ut_inc, port.ut_ref
                )
            )

    if nf2ff is not None:
        antenna_s11_path = output / "s11_antenna.csv"
        with antenna_s11_path.open("w", newline="") as result_file:
            writer = csv.writer(result_file)
            writer.writerow(("frequency_hz", "s11_db", "vswr"))
            writer.writerows(
                (float(frequency), float(db),
                 float((1 + abs(value)) / max(1 - abs(value), 1e-12)))
                for frequency, value, db in zip(frequencies, s11, s11_db)
            )

    field_files = []
    if nf2ff is not None:
        theta = np.arange(0.0, 181.0, 2.0)
        phi = np.array([0.0, 90.0])
        far_field = nf2ff.CalcNF2FF(
            str(simulation_path), f0, theta, phi, center=[0, 0, 0], verbose=1
        )
        maximum_directivity_db = 10.0 * np.log10(
            max(float(far_field.Dmax[0]), np.finfo(float).tiny)
        )
        pattern_db = (
            20.0 * np.log10(
                np.maximum(
                    np.abs(far_field.E_norm[0]) /
                    max(float(np.max(np.abs(far_field.E_norm[0]))),
                        np.finfo(float).tiny),
                    np.finfo(float).tiny,
                )
            ) + maximum_directivity_db
        )
        far_field_path = output / "far_field_antenna.csv"
        with far_field_path.open("w", newline="") as result_file:
            writer = csv.writer(result_file)
            writer.writerow(("theta_deg", "phi_0_dbi", "phi_90_dbi"))
            writer.writerows(
                (float(angle), float(pattern_db[index, 0]),
                 float(pattern_db[index, 1]))
                for index, angle in enumerate(theta)
            )

        with HDF5Dump(str(simulation_path / "Ef_patch.h5")) as e_dump, \
                HDF5Dump(str(simulation_path / "Hf_patch.h5")) as h_dump:
            e_field = e_dump.GetFieldAtFrequency(f0)
            h_field = h_dump.GetFieldAtFrequency(f0)
            field_mesh = e_dump.GetMesh()
            x_mm = field_mesh["lines"][0] / field_mesh["scaling"]
            y_mm = field_mesh["lines"][1] / field_mesh["scaling"]
            e_magnitude = np.sqrt(np.sum(np.abs(e_field) ** 2, axis=0))[:, :, 0]
            h_magnitude = np.sqrt(np.sum(np.abs(h_field) ** 2, axis=0))[:, :, 0]
        near_field_path = output / "near_field_antenna.csv"
        with near_field_path.open("w", newline="") as result_file:
            writer = csv.writer(result_file)
            writer.writerow(("x_mm", "y_mm", "e_magnitude_v_per_m",
                             "h_magnitude_a_per_m"))
            writer.writerows(
                (float(x), float(y), float(e_magnitude[ix, iy]),
                 float(h_magnitude[ix, iy]))
                for ix, x in enumerate(x_mm)
                for iy, y in enumerate(y_mm)
            )
        field_files = [
            str((simulation_path / "Ef_patch.h5").resolve()),
            str((simulation_path / "Hf_patch.h5").resolve()),
            far_field_path.name,
            near_field_path.name,
        ]

    summary = {
        "engine_requested": args.engine,
        "simulation_kind": simulation_kind,
        "elapsed_solver_seconds": elapsed_s,
        "timesteps_limit": args.timesteps,
        "mesh_cells": cell_count,
        "estimated_solver_memory_bytes": cell_count * 72,
        "max_cells_limit": args.max_cells,
        "mesh_divisions_per_wavelength": args.mesh_div,
        "frequency_start_hz": f_start_hz,
        "frequency_stop_hz": f_stop_hz,
        "frequency_points": len(frequencies),
        "boundary": args.boundary,
        "port_impedance_ohms": args.port_impedance,
        "s11_csv": s11_file.name,
        "s11_touchstone": touchstone_path.name,
        "near_field_z_mm": near_field_z if nf2ff is not None else None,
        "antenna_field_outputs": field_files,
        "antenna_geometry_mm": {
            "substrate_width": substrate_width,
            "substrate_length": substrate_length,
            "substrate_thickness": substrate_thickness,
            "patch_width": patch_width,
            "patch_length": patch_length,
            "feed_position": feed_position,
        }
        if nf2ff is not None
        else None,
        "antenna_substrate_material": {
            "relative_permittivity": args.substrate_permittivity,
            "conductivity_s_per_m": args.substrate_conductivity_s_per_m,
            "loss_tangent": args.substrate_loss_tangent,
        }
        if nf2ff is not None
        else None,
    }
    summary_name = "summary_scene.json" if args.scene else f"summary_{args.engine}.json"
    (output / summary_name).write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, sort_keys=True))


if __name__ == "__main__":
    main()
