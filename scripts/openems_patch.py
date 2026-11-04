#!/usr/bin/env python3
"""Run an openEMS CAD scene or a standalone patch-reference test."""

import argparse
import csv
import json
import subprocess
import sys
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


def write_touchstone_nport(
    path: Path, frequencies_hz: np.ndarray, scattering: np.ndarray,
    reference_ohms: list[float],
) -> None:
    port_count = scattering.shape[1]
    lines = [
        "[Version] 2.1",
        f"# Hz S RI R {reference_ohms[0]:g}",
        f"[Number of Ports] {port_count}",
        f"[Number of Frequencies] {len(frequencies_hz)}",
        "[Reference] " + " ".join(f"{value:g}" for value in reference_ohms),
        "[Matrix Format] Full",
        "[Network Data]",
    ]
    for frequency_index, frequency in enumerate(frequencies_hz):
        values = [f"{float(frequency):.17g}"]
        for source_index in range(port_count):
            for response_index in range(port_count):
                value = scattering[
                    frequency_index, response_index, source_index
                ]
                values.extend((f"{value.real:.17g}", f"{value.imag:.17g}"))
        lines.append(" ".join(values))
    lines.append("[End]")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def run_multiport_scene(args: argparse.Namespace, scene: dict) -> bool:
    if args.port_excitation is not None:
        return False
    ports = scene.get("ports")
    if not isinstance(ports, list) or not ports:
        return False
    if len(ports) == 1:
        return False

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    run_arguments = []
    skip_next = False
    for argument in sys.argv[1:]:
        if skip_next:
            skip_next = False
            continue
        if argument in ("--output", "--port-excitation"):
            skip_next = True
            continue
        run_arguments.append(argument)

    excitation_data = []
    for source_index in range(len(ports)):
        run_directory = output / f"excitation_{source_index + 1}"
        run_directory.mkdir(parents=True, exist_ok=True)
        print(
            f"Running S-parameter excitation {source_index + 1}/{len(ports)}",
            flush=True,
        )
        command = [
            sys.executable, str(Path(__file__).resolve()), *run_arguments,
            "--output", str(run_directory),
            "--port-excitation", str(source_index),
        ]
        completed = subprocess.run(command, check=False)
        if completed.returncode != 0:
            raise RuntimeError(
                f"Port {source_index + 1} excitation failed "
                f"with exit code {completed.returncode}"
            )
        response_path = run_directory / "excitation.csv"
        with response_path.open(newline="", encoding="utf-8") as response_file:
            rows = list(csv.DictReader(response_file))
        if not rows:
            raise RuntimeError(
                f"Port {source_index + 1} produced no S-parameter data"
            )
        excitation_data.append((run_directory, rows))

    frequency_values = np.asarray([
        float(row["frequency_hz"]) for row in excitation_data[0][1]
    ])
    port_count = len(ports)
    scattering = np.zeros(
        (len(frequency_values), port_count, port_count), dtype=np.complex128
    )
    for source_index, (_, rows) in enumerate(excitation_data):
        if len(rows) != len(frequency_values):
            raise RuntimeError("Port excitations returned different sweep lengths")
        for frequency_index, row in enumerate(rows):
            if not np.isclose(
                float(row["frequency_hz"]),
                frequency_values[frequency_index],
                rtol=1e-12,
            ):
                raise RuntimeError("Port excitations returned mismatched frequencies")
            for response_index in range(port_count):
                scattering[frequency_index, response_index, source_index] = (
                    float(row[f"s{response_index + 1}_real"])
                    + 1j * float(row[f"s{response_index + 1}_imag"])
                )

    s11 = scattering[:, 0, 0]
    s11_db = 20 * np.log10(
        np.maximum(np.abs(s11), np.finfo(float).tiny)
    )
    headers = [
        "frequency_hz", "s11_real", "s11_imag", "s11_db", "vswr_port_1"
    ]
    matrix_columns = []
    for source_index in range(port_count):
        for response_index in range(port_count):
            if source_index == 0 and response_index == 0:
                continue
            label = f"s{response_index + 1}{source_index + 1}_db"
            headers.append(label)
            matrix_columns.append((response_index, source_index))
    headers.extend(
        f"vswr_port_{port_index + 1}"
        for port_index in range(1, port_count)
    )
    with (output / "s11_scene.csv").open("w", newline="") as result_file:
        writer = csv.writer(result_file)
        writer.writerow(headers)
        for frequency_index, frequency in enumerate(frequency_values):
            row = [
                float(frequency),
                float(s11[frequency_index].real),
                float(s11[frequency_index].imag),
                float(s11_db[frequency_index]),
                float(
                    (1 + abs(s11[frequency_index])) /
                    max(1 - abs(s11[frequency_index]), 1e-12)
                ),
            ]
            row.extend(
                float(20 * np.log10(max(
                    abs(scattering[frequency_index, response_index, source_index]),
                    np.finfo(float).tiny,
                )))
                for response_index, source_index in matrix_columns
            )
            row.extend(
                float(
                    (1 + abs(scattering[
                        frequency_index, port_index, port_index
                    ])) /
                    max(1 - abs(scattering[
                        frequency_index, port_index, port_index
                    ]), 1e-12)
                )
                for port_index in range(1, port_count)
            )
            writer.writerow(row)

    write_touchstone_1port(
        output / "s11_scene.s1p", frequency_values, s11, ports[0]["impedance_ohm"]
    )
    write_touchstone_nport(
        output / f"s_parameters_scene.s{port_count}p",
        frequency_values, scattering,
        [float(port["impedance_ohm"]) for port in ports],
    )
    shutil.copyfile(
        excitation_data[0][0] / "excitation_waveform.csv",
        output / "waveform_scene.csv",
    )

    models_directory = output / "models"
    models_directory.mkdir(exist_ok=True)
    for source_index, (run_directory, _) in enumerate(excitation_data):
        source_models = run_directory / "models"
        if source_models.is_dir():
            shutil.copytree(
                source_models,
                models_directory / f"excitation_{source_index + 1}",
                dirs_exist_ok=True,
            )
    summary = {
        "simulation_kind": "cad_multiport",
        "engine_requested": args.engine,
        "port_count": port_count,
        "frequency_points": len(frequency_values),
        "timesteps_limit": args.timesteps,
        "s_parameters_touchstone": f"s_parameters_scene.s{port_count}p",
    }
    (output / "summary_scene.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, sort_keys=True), flush=True)
    return True


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", choices=("cpu", "gpu"), default="cpu")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scene", type=Path,
                        help="JSON scene containing closed STL solids in millimeters")
    parser.add_argument("--port-excitation", type=int,
                        help=argparse.SUPPRESS)
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
    if args.port_excitation is not None and args.port_excitation < 0:
        parser.error("--port-excitation must be non-negative")
    if args.scene and args.port_excitation is None:
        scene_data = json.loads(args.scene.read_text(encoding="utf-8"))
        try:
            if run_multiport_scene(args, scene_data):
                return
        except (OSError, RuntimeError, ValueError, KeyError) as error:
            parser.exit(1, f"Multiport simulation failed: {error}\n")
    if not args.scene:
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
            parser.error("invalid patch-reference geometry or material settings")

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    simulation_name = "cad_scene" if args.scene else "patch_reference"
    simulation_path = output / f"{simulation_name}_{args.engine}_{time.time_ns()}"
    simulation_path.mkdir(parents=True, exist_ok=False)

    f_start_hz = args.f_start_ghz * 1.0e9
    f_stop_hz = args.f_stop_ghz * 1.0e9
    f0 = 0.5 * (f_start_hz + f_stop_hz)
    fc = 0.5 * (f_stop_hz - f_start_hz)
    mesh_resolution = C0 / f_stop_hz / 1e-3 / args.mesh_div
    nf2ff = None

    fdtd_options = {"NrTS": args.timesteps, "EndCriteria": 1e-4}
    fdtd = openEMS(**fdtd_options)
    fdtd.SetGaussExcite(f0, fc)
    fdtd.SetBoundaryCond([args.boundary] * 6)

    csx = ContinuousStructure()
    fdtd.SetCSX(csx)
    mesh = csx.GetGrid()
    mesh.SetDeltaUnit(1e-3)
    response_port = None
    port1_type = "lumped"
    port1_mode = ""
    if args.scene:
        scene_path = args.scene.resolve()
        scene = json.loads(scene_path.read_text(encoding="utf-8"))
        bounds = scene["bounds_mm"]
        if len(bounds) != 6 or not scene.get("solids"):
            parser.error("scene must contain solids and six bounds_mm values")
        min_x, min_y, min_z, max_x, max_y, max_z = map(float, bounds)
        if not (min_x < max_x and min_y < max_y and min_z < max_z):
            parser.error("scene bounds must have positive size in all axes")

        def parse_port(config, fallback=None):
            source = config if config is not None else fallback
            if not isinstance(source, dict):
                parser.error("port configuration must be an object")
            try:
                start = np.asarray(source["start_mm"], dtype=float)
                stop = np.asarray(source["stop_mm"], dtype=float)
                port_type = str(source.get("type", "lumped"))
                mode = str(source.get("mode", ""))
                axis = str(source.get("axis", "z"))
                impedance = float(source.get("impedance_ohm", args.port_impedance))
            except (KeyError, TypeError, ValueError):
                parser.error("port configuration is incomplete or invalid")
            if (
                start.shape != (3,) or stop.shape != (3,)
                or not np.all(np.isfinite(start))
                or not np.all(np.isfinite(stop))
                or axis not in ("x", "y", "z")
                or port_type not in ("lumped", "rect-waveguide")
                or not np.isfinite(impedance) or impedance <= 0
            ):
                parser.error("port configuration contains invalid coordinates or type")
            axis_index = "xyz".index(axis)
            transverse = [index for index in range(3) if index != axis_index]
            if port_type == "lumped":
                valid_shape = (
                    start[axis_index] < stop[axis_index]
                    and all(start[index] == stop[index] for index in transverse)
                )
            else:
                valid_shape = (
                    start[axis_index] != stop[axis_index]
                    and all(start[index] < stop[index] for index in transverse)
                    and mode in ("TE10", "TE20", "TE01", "TE11")
                )
            if not valid_shape:
                parser.error("port coordinates do not match the configured port type")
            return {
                "start": start,
                "stop": stop,
                "type": port_type,
                "mode": mode,
                "axis": axis,
                "impedance": impedance,
            }

        if "ports" in scene:
            if not isinstance(scene["ports"], list):
                parser.error("scene ports must be an array")
            ports = [parse_port(config) for config in scene["ports"]]
        elif "port1" in scene:
            ports = [parse_port(scene["port1"])]
            if "port2" in scene:
                ports.append(parse_port(scene["port2"]))
        else:
            legacy_port = None
            if "port_start_mm" in scene and "port_stop_mm" in scene:
                legacy_port = {
                    "start_mm": scene["port_start_mm"],
                    "stop_mm": scene["port_stop_mm"],
                    "axis": scene.get("port_axis", "z"),
                }
            elif all(key in scene for key in (
                    "port_x_mm", "port_y_mm", "port_z_start_mm",
                    "port_z_stop_mm")):
                legacy_port = {
                    "start_mm": [
                        scene["port_x_mm"], scene["port_y_mm"],
                        scene["port_z_start_mm"],
                    ],
                    "stop_mm": [
                        scene["port_x_mm"], scene["port_y_mm"],
                        scene["port_z_stop_mm"],
                    ],
                    "axis": "z",
                }
            if legacy_port is None:
                parser.error("scene must include at least one configured port")
            ports = [parse_port(legacy_port)]
        if not ports or len(ports) > 128:
            parser.error("scene must contain between one and 128 configured ports")
        if (args.port_excitation is not None and
                args.port_excitation >= len(ports)):
            parser.error("--port-excitation does not refer to a configured port")
        port1_type = ports[0]["type"]
        port1_mode = ports[0]["mode"]
        margin = max(mesh_resolution * 8, 10.0)
        all_port_z = [
            float(coordinate)
            for port_config in ports
            for coordinate in (port_config["start"][2], port_config["stop"][2])
        ]
        z_start = min([min_z - margin] + [z - margin * 0.1 for z in all_port_z])
        z_stop = max([max_z + margin] + [z + margin * 0.1 for z in all_port_z])
        x_lines = {min_x - margin, min_x, max_x, max_x + margin}
        y_lines = {min_y - margin, min_y, max_y, max_y + margin}
        z_lines = {z_start, min_z, max_z, z_stop}
        for port_config in ports:
            x_lines.update((port_config["start"][0], port_config["stop"][0]))
            y_lines.update((port_config["start"][1], port_config["stop"][1]))
            z_lines.update((port_config["start"][2], port_config["stop"][2]))
        mesh.AddLine("x", sorted(x_lines))
        mesh.AddLine("y", sorted(y_lines))
        mesh.AddLine("z", sorted(z_lines))
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
        port_objects = []
        excitation_port = (
            0 if args.port_excitation is None else args.port_excitation
        )
        for port_number, port_config in enumerate(ports):
            port_start = port_config["start"]
            port_stop = port_config["stop"]
            port_direction = port_config["axis"]
            if port_config["type"] == "rect-waveguide":
                axis_index = "xyz".index(port_direction)
                transverse = ((axis_index + 1) % 3, (axis_index + 2) % 3)
                a = abs(port_stop[transverse[0]] - port_start[transverse[0]])
                b = abs(port_stop[transverse[1]] - port_start[transverse[1]])
                port_objects.append(
                    fdtd.AddRectWaveGuidePort(
                        port_number + 1,
                        port_start.tolist(),
                        port_stop.tolist(),
                        port_direction,
                        a * 1e-3,
                        b * 1e-3,
                        port_config["mode"],
                        excite=1 if port_number == excitation_port else 0,
                    )
                )
            else:
                port_objects.append(
                    fdtd.AddLumpedPort(
                        port_number + 1,
                        port_config["impedance"],
                        port_start.tolist(),
                        port_stop.tolist(),
                        port_direction,
                        excite=1 if port_number == excitation_port else 0,
                        priority=5,
                        edges2grid={
                            "x": "yz", "y": "xz", "z": "xy"
                        }[port_direction],
                    )
                )
        port = port_objects[0]
        response_port = port_objects[1] if len(port_objects) > 1 else None
        simulation_kind = (
            "cad_scene_multiport" if len(port_objects) > 1 else "cad_scene"
        )
    else:
        substrate_width = args.substrate_width_mm
        substrate_length = args.substrate_length_mm
        substrate_thickness = args.substrate_thickness_mm
        patch_width = args.patch_width_mm
        patch_length = args.patch_length_mm
        feed_position = args.feed_position_mm
        near_field_z = substrate_thickness + 5.0
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
    if args.port_excitation is not None:
        for configured_port in port_objects:
            configured_port.CalcPort(str(simulation_path), frequencies)
        source_port = port_objects[args.port_excitation]
        response_path = output / "excitation.csv"
        with response_path.open("w", newline="") as result_file:
            writer = csv.writer(result_file)
            headers = ["frequency_hz"]
            for response_index in range(len(port_objects)):
                headers.extend((
                    f"s{response_index + 1}_real",
                    f"s{response_index + 1}_imag",
                ))
            writer.writerow(headers)
            for frequency_index, frequency in enumerate(frequencies):
                row = [float(frequency)]
                for configured_port in port_objects:
                    value = (
                        configured_port.uf_ref[frequency_index] /
                        source_port.uf_inc[frequency_index]
                    )
                    row.extend((float(value.real), float(value.imag)))
                writer.writerow(row)
        waveform_path = output / "excitation_waveform.csv"
        with waveform_path.open("w", newline="") as waveform_file:
            writer = csv.writer(waveform_file)
            if source_port.__class__.__name__ == "RectWGPort":
                writer.writerow(("time_s", "port_total_voltage_v"))
                writer.writerows(
                    (float(t), float(voltage))
                    for t, voltage in zip(
                        source_port.u_time, source_port.ut_tot
                    )
                )
            else:
                writer.writerow(
                    ("time_s", "incident_voltage_v", "reflected_voltage_v")
                )
                writer.writerows(
                    (float(t), float(incident), float(reflected))
                    for t, incident, reflected in zip(
                        source_port.u_time, source_port.ut_inc,
                        source_port.ut_ref,
                    )
                )
        return

    port.CalcPort(str(simulation_path), frequencies)
    s11 = port.uf_ref / port.uf_inc
    s21 = None
    s21_db = None
    if response_port is not None:
        response_port.CalcPort(str(simulation_path), frequencies)
        s21 = response_port.uf_ref / port.uf_inc
        s21_db = 20 * np.log10(
            np.maximum(np.abs(s21), np.finfo(float).tiny)
        )
    s11_magnitude = np.abs(s11)
    s11_db = 20 * np.log10(np.maximum(s11_magnitude, np.finfo(float).tiny))
    vswr = (1 + s11_magnitude) / np.maximum(1 - s11_magnitude, 1e-12)
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
            headers = [
                "frequency_hz", "s11_real", "s11_imag", "s11_db", "vswr"
            ]
            if s21_db is not None:
                headers.append("s21_db")
            writer.writerow(headers)
            if s21_db is None:
                writer.writerows(
                    (float(frequency), float(value.real), float(value.imag),
                     float(db), float(vswr_value))
                    for frequency, value, db, vswr_value in zip(
                        frequencies, s11, s11_db, vswr
                    )
                )
            else:
                writer.writerows(
                    (float(frequency), float(value.real), float(value.imag),
                     float(db), float(vswr_value), float(s21_value_db))
                    for frequency, value, db, vswr_value, s21_value_db in zip(
                        frequencies, s11, s11_db, vswr, s21_db
                    )
                )
        waveform_path = output / "waveform_scene.csv"
        with waveform_path.open("w", newline="") as waveform_file:
            writer = csv.writer(waveform_file)
            if port1_type == "lumped":
                writer.writerow(
                    ("time_s", "incident_voltage_v", "reflected_voltage_v")
                )
                writer.writerows(
                    (float(t), float(incident), float(reflected))
                    for t, incident, reflected in zip(
                        port.u_time, port.ut_inc, port.ut_ref
                    )
                )
            else:
                writer.writerow(("time_s", "port_1_total_voltage_v"))
                writer.writerows(
                    (float(t), float(voltage))
                    for t, voltage in zip(port.u_time, port.ut_tot)
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
    }
    if s21_db is not None:
        summary.update({
            "port_count": 2,
            "port1_type": port1_type,
            "port1_mode": port1_mode,
            "s21_in_csv": True,
            "s21_max_db": float(np.max(s21_db)),
        })
    if nf2ff is not None:
        summary.update({
            "near_field_z_mm": near_field_z,
            "antenna_field_outputs": field_files,
            "antenna_geometry_mm": {
                "substrate_width": substrate_width,
                "substrate_length": substrate_length,
                "substrate_thickness": substrate_thickness,
                "patch_width": patch_width,
                "patch_length": patch_length,
                "feed_position": feed_position,
            },
            "antenna_substrate_material": {
                "relative_permittivity": args.substrate_permittivity,
                "conductivity_s_per_m": args.substrate_conductivity_s_per_m,
                "loss_tangent": args.substrate_loss_tangent,
            },
        })
    summary_name = "summary_scene.json" if args.scene else f"summary_{args.engine}.json"
    (output / summary_name).write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, sort_keys=True))


if __name__ == "__main__":
    main()
