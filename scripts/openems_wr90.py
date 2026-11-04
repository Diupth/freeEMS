#!/usr/bin/env python3
"""Simulate a straight WR90 waveguide and export two-port RF results."""

import argparse
import csv
import json
import time
from pathlib import Path

import numpy as np
from CSXCAD import ContinuousStructure
from openEMS import openEMS
from openEMS.physical_constants import C0


def run_excitation(
    output: Path,
    excitation_port: int,
    engine: str,
    a_mm: float,
    b_mm: float,
    length_mm: float,
    port_depth_mm: float,
    f_start_hz: float,
    f_stop_hz: float,
    mesh_div: int,
    timesteps: int,
    frequency_points: int,
    mode: str,
    max_cells: int,
) -> tuple[object, object, float, str, int]:
    center_hz = 0.5 * (f_start_hz + f_stop_hz)
    mesh_resolution = C0 / f_stop_hz / 1e-3 / mesh_div
    run_path = output / f"wr90_port{excitation_port}_{time.time_ns()}"
    fdtd = openEMS(NrTS=timesteps, EndCriteria=1e-5)
    fdtd.SetGaussExcite(center_hz, 0.5 * (f_stop_hz - f_start_hz))
    fdtd.SetBoundaryCond([0, 0, 0, 0, 3, 3])

    csx = ContinuousStructure()
    fdtd.SetCSX(csx)
    mesh = csx.GetGrid()
    mesh.SetDeltaUnit(1e-3)
    mesh.AddLine("x", [0.0, a_mm])
    mesh.AddLine("y", [0.0, b_mm])
    mesh.AddLine("z", [0.0, length_mm])

    ports = []
    positions = (10.0 * mesh_resolution, length_mm - 10.0 * mesh_resolution)
    for port_number, port_center in enumerate(positions):
        z_low = port_center - port_depth_mm * 0.5
        z_high = port_center + port_depth_mm * 0.5
        if z_low <= 0 or z_high >= length_mm:
            raise ValueError("WR90 port plane is outside the waveguide length")
        mesh.AddLine("z", [z_low, z_high])
        start_z, stop_z = (
            (z_low, z_high) if port_number == 0 else (z_high, z_low)
        )
        ports.append(
            fdtd.AddRectWaveGuidePort(
                port_number,
                [0.0, 0.0, start_z],
                [a_mm, b_mm, stop_z],
                "z",
                a_mm * 1e-3,
                b_mm * 1e-3,
                mode,
                excite=1 if port_number == excitation_port else 0,
            )
        )
    mesh.SmoothMeshLines("all", mesh_resolution, ratio=1.4)
    cell_count = int(np.prod([
        max(0, len(mesh.GetLines(axis)) - 1) for axis in "xyz"
    ]))
    if cell_count <= 0 or cell_count > max_cells:
        raise ValueError(
            f"mesh cell count {cell_count:,} is invalid or exceeds the "
            f"--max-cells limit ({max_cells:,})"
        )
    model_directory = output / "models"
    model_directory.mkdir(parents=True, exist_ok=True)
    fdtd.Write2XML(str(model_directory / f"wr90_port{excitation_port + 1}.xml"))

    run_options = {"verbose": 2}
    if engine == "gpu":
        run_options["engine"] = "gpu"
    started = time.perf_counter()
    fdtd.Run(str(run_path), cleanup=False, **run_options)
    elapsed = time.perf_counter() - started

    frequencies = np.linspace(f_start_hz, f_stop_hz, frequency_points)
    for port in ports:
        port.CalcPort(str(run_path), frequencies)
    return ports[0], ports[1], elapsed, str(run_path), cell_count


def write_touchstone_2port(
    path: Path,
    frequencies_hz: np.ndarray,
    s11: np.ndarray,
    s21: np.ndarray,
    s12: np.ndarray,
    s22: np.ndarray,
    reference_ohms: float = 50.0,
) -> None:
    lines = [
        "[Version] 2.1",
        f"# Hz S RI R {reference_ohms:g}",
        "[Number of Ports] 2",
        f"[Number of Frequencies] {len(frequencies_hz)}",
        f"[Reference] {reference_ohms:g} {reference_ohms:g}",
        "[Matrix Format] Full",
        "[Network Data]",
    ]
    for values in zip(frequencies_hz, s11, s21, s12, s22):
        frequency, *parameters = values
        columns = [float(frequency)]
        for parameter in parameters:
            columns.extend((float(parameter.real), float(parameter.imag)))
        lines.append(" ".join(f"{value:.17g}" for value in columns))
    lines.append("[End]")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--engine", choices=("cpu", "gpu"), default="cpu")
    parser.add_argument("--a-mm", type=float, default=22.86)
    parser.add_argument("--b-mm", type=float, default=10.16)
    parser.add_argument("--length-mm", type=float, default=100.0)
    parser.add_argument("--port-depth-mm", type=float, default=5.0)
    parser.add_argument("--mode", choices=("TE10", "TE20", "TE01", "TE11"),
                        default="TE10")
    parser.add_argument("--f-start-ghz", type=float, default=8.2)
    parser.add_argument("--f-stop-ghz", type=float, default=12.4)
    parser.add_argument("--frequency-points", type=int, default=201)
    parser.add_argument("--mesh-div", type=int, default=30)
    parser.add_argument("--timesteps", type=int, default=20000)
    parser.add_argument("--max-cells", type=int, default=5_000_000)
    args = parser.parse_args()
    if (
        min(args.a_mm, args.b_mm, args.length_mm, args.port_depth_mm) <= 0
        or args.f_start_ghz <= 0
        or args.f_stop_ghz <= args.f_start_ghz
        or args.frequency_points < 2
        or not 8 <= args.mesh_div <= 80
        or args.timesteps < 1000
        or args.max_cells < 1000
    ):
        parser.error("invalid waveguide dimensions or simulation settings")

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    f_start_hz = args.f_start_ghz * 1e9
    f_stop_hz = args.f_stop_ghz * 1e9
    mode_m, mode_n = int(args.mode[2]), int(args.mode[3])
    cutoff_hz = 0.5 * C0 * np.sqrt(
        (mode_m / (args.a_mm * 1e-3)) ** 2 +
        (mode_n / (args.b_mm * 1e-3)) ** 2
    )
    if f_start_hz <= cutoff_hz:
        parser.error(
            f"{args.mode} cutoff is {cutoff_hz / 1e9:.4g} GHz; "
            "the full sweep must be above cutoff"
        )
    p0_forward, p1_response, elapsed_0, run_0, cells_0 = run_excitation(
        output, 0, args.engine, args.a_mm, args.b_mm, args.length_mm,
        args.port_depth_mm, f_start_hz, f_stop_hz, args.mesh_div, args.timesteps,
        args.frequency_points, args.mode, args.max_cells
    )
    p0_response, p1_forward, elapsed_1, run_1, cells_1 = run_excitation(
        output, 1, args.engine, args.a_mm, args.b_mm, args.length_mm,
        args.port_depth_mm, f_start_hz, f_stop_hz, args.mesh_div, args.timesteps,
        args.frequency_points, args.mode, args.max_cells
    )

    frequencies = np.linspace(f_start_hz, f_stop_hz, args.frequency_points)
    s11 = p0_forward.uf_ref / p0_forward.uf_inc
    s21 = p1_response.uf_ref / p0_forward.uf_inc
    s22 = p1_forward.uf_ref / p1_forward.uf_inc
    s12 = p0_response.uf_ref / p1_forward.uf_inc
    touchstone_path = output / "s_parameters_wr90.s2p"
    write_touchstone_2port(touchstone_path, frequencies, s11, s21, s12, s22)
    s11_db = 20 * np.log10(np.maximum(np.abs(s11), np.finfo(float).tiny))
    s21_db = 20 * np.log10(np.maximum(np.abs(s21), np.finfo(float).tiny))
    s12_db = 20 * np.log10(np.maximum(np.abs(s12), np.finfo(float).tiny))
    s22_db = 20 * np.log10(np.maximum(np.abs(s22), np.finfo(float).tiny))
    vswr_1 = (1 + np.abs(s11)) / np.maximum(1 - np.abs(s11), 1e-12)
    vswr_2 = (1 + np.abs(s22)) / np.maximum(1 - np.abs(s22), 1e-12)

    with (output / "s_parameters_wr90.csv").open("w", newline="") as result_file:
        writer = csv.writer(result_file)
        writer.writerow(
            ("frequency_hz", "s11_db", "s21_db", "s12_db", "s22_db")
        )
        writer.writerows(
            (float(f), float(v11), float(v21), float(v12), float(v22))
            for f, v11, v21, v12, v22 in zip(
                frequencies, s11_db, s21_db, s12_db, s22_db
            )
        )
    with (output / "vswr_wr90.csv").open("w", newline="") as result_file:
        writer = csv.writer(result_file)
        writer.writerow(("frequency_hz", "vswr_port_1", "vswr_port_2"))
        writer.writerows(
            (float(f), float(v1), float(v2))
            for f, v1, v2 in zip(frequencies, vswr_1, vswr_2)
        )
    with (output / "waveform_wr90.csv").open("w", newline="") as result_file:
        writer = csv.writer(result_file)
        writer.writerow(("time_s", "port_1_voltage_v"))
        writer.writerows(
            (float(t), float(voltage))
            for t, voltage in zip(p0_forward.u_time, p0_forward.ut_tot)
        )

    summary = {
        "model": f"WR90 straight rectangular waveguide, {args.mode}",
        "mode": args.mode,
        "cutoff_frequency_hz": cutoff_hz,
        "engine_requested": args.engine,
        "frequency_start_hz": f_start_hz,
        "frequency_stop_hz": f_stop_hz,
        "waveguide_a_mm": args.a_mm,
        "waveguide_b_mm": args.b_mm,
        "waveguide_length_mm": args.length_mm,
        "port_depth_mm": args.port_depth_mm,
        "mesh_divisions_per_wavelength": args.mesh_div,
        "timesteps_limit": args.timesteps,
        "mesh_cells": max(cells_0, cells_1),
        "estimated_solver_memory_bytes": max(cells_0, cells_1) * 72,
        "max_cells_limit": args.max_cells,
        "run_time_port_1_seconds": elapsed_0,
        "run_time_port_2_seconds": elapsed_1,
        "simulation_directories": [run_0, run_1],
        "s_parameter_csv": "s_parameters_wr90.csv",
        "s_parameter_touchstone": touchstone_path.name,
        "vswr_csv": "vswr_wr90.csv",
    }
    (output / "summary_wr90.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, sort_keys=True))


if __name__ == "__main__":
    main()
