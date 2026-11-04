#!/usr/bin/env python3
"""Check reference solver output shape, finiteness, and broad physical bounds."""

import argparse
import csv
import json
import math
from pathlib import Path


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as result_file:
        rows = list(csv.DictReader(result_file))
    if not rows:
        raise ValueError(f"{path} contains no result rows")
    for row_index, row in enumerate(rows, start=2):
        for column, value in row.items():
            if not math.isfinite(float(value)):
                raise ValueError(f"{path}:{row_index} has non-finite {column}")
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wr90", type=Path, required=True)
    parser.add_argument("--antenna", type=Path, required=True)
    parser.add_argument("--wr90-te20", type=Path)
    parser.add_argument(
        "--expectations",
        type=Path,
        default=Path(__file__).with_name("reference_expectations.json"),
    )
    args = parser.parse_args()
    limits = json.loads(args.expectations.read_text(encoding="utf-8"))

    wr90 = read_rows(args.wr90 / "s_parameters_wr90.csv")
    if len(wr90) != 201:
        raise ValueError(f"WR90 expected 201 frequency samples, got {len(wr90)}")
    s11_max = max(float(row["s11_db"]) for row in wr90)
    s21_values = [float(row["s21_db"]) for row in wr90]
    reciprocity = max(
        abs(float(row["s21_db"]) - float(row["s12_db"])) for row in wr90
    )
    wr90_expected = limits["wr90"]
    if s11_max >= wr90_expected["s11_max_db"]:
        raise ValueError(f"WR90 S11 too high: max {s11_max:.3f} dB")
    if min(s21_values) < wr90_expected["s21_min_db"] or max(
        s21_values
    ) > wr90_expected["s21_max_db"]:
        raise ValueError("WR90 through response is outside the reference range")
    if reciprocity > wr90_expected["reciprocity_max_db_difference"]:
        raise ValueError(f"WR90 reciprocity error is {reciprocity:.4g} dB")
    wr90_summary = json.loads(
        (args.wr90 / "summary_wr90.json").read_text(encoding="utf-8")
    )
    if wr90_summary["mesh_cells"] > wr90_summary["max_cells_limit"]:
        raise ValueError("WR90 mesh exceeded its configured cell limit")

    te20_values = None
    if args.wr90_te20:
        te20 = read_rows(args.wr90_te20 / "s_parameters_wr90.csv")
        te20_summary = json.loads(
            (args.wr90_te20 / "summary_wr90.json").read_text(encoding="utf-8")
        )
        if te20_summary["mode"] != "TE20" or te20_summary[
            "frequency_start_hz"
        ] <= te20_summary["cutoff_frequency_hz"]:
            raise ValueError("TE20 sweep is not wholly above its cutoff")
        if max(float(row["s11_db"]) for row in te20) >= -20.0:
            raise ValueError("Straight WR90 TE20 reference has excessive reflection")
        te20_through = [float(row["s21_db"]) for row in te20]
        if min(te20_through) < -0.5 or max(te20_through) > 0.5:
            raise ValueError("Straight WR90 TE20 through response is outside bounds")
        te20_values = {
            "mode": te20_summary["mode"],
            "max_s11_db": max(float(row["s11_db"]) for row in te20),
            "max_s21_db": max(te20_through),
            "cutoff_hz": te20_summary["cutoff_frequency_hz"],
        }

    antenna_s11 = read_rows(args.antenna / "s11_antenna.csv")
    far_field = read_rows(args.antenna / "far_field_antenna.csv")
    near_field = read_rows(args.antenna / "near_field_antenna.csv")
    antenna_expected = limits["patch_antenna"]
    minimum_s11 = min(float(row["s11_db"]) for row in antenna_s11)
    peak_directivity = max(
        max(float(row["phi_0_dbi"]), float(row["phi_90_dbi"]))
        for row in far_field
    )
    if minimum_s11 > antenna_expected["s11_min_db"]:
        raise ValueError(f"Patch antenna has no acceptable S11 minimum: {minimum_s11:.3f} dB")
    if not (
        antenna_expected["far_field_peak_min_dbi"]
        <= peak_directivity
        <= antenna_expected["far_field_peak_max_dbi"]
    ):
        raise ValueError(
            f"Patch antenna peak directivity is implausible: {peak_directivity:.3f} dBi"
        )
    if len(near_field) < antenna_expected["minimum_near_field_samples"]:
        raise ValueError(f"Near-field slice has too few samples: {len(near_field)}")
    if max(float(row["e_magnitude_v_per_m"]) for row in near_field) <= 0.0:
        raise ValueError("Near-field E dump is empty")
    if max(float(row["h_magnitude_a_per_m"]) for row in near_field) <= 0.0:
        raise ValueError("Near-field H dump is empty")
    antenna_summary = json.loads(
        (args.antenna / "summary_cpu.json").read_text(encoding="utf-8")
    )
    if antenna_summary["mesh_cells"] > antenna_summary["max_cells_limit"]:
        raise ValueError("Antenna mesh exceeded its configured cell limit")

    print(json.dumps({
        "wr90_samples": len(wr90),
        "wr90_max_s11_db": s11_max,
        "wr90_max_reciprocity_error_db": reciprocity,
        "patch_s11_min_db": minimum_s11,
        "patch_peak_directivity_dbi": peak_directivity,
        "near_field_samples": len(near_field),
        "wr90_mesh_cells": wr90_summary["mesh_cells"],
        "wr90_te20": te20_values,
        "patch_mesh_cells": antenna_summary["mesh_cells"],
    }, indent=2))


if __name__ == "__main__":
    main()
