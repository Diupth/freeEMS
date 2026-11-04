#!/usr/bin/env python3
"""Verify that a swept-solid STL imports into CSXCAD and writes native XML."""

import argparse
from pathlib import Path
import tempfile

from CSXCAD import ContinuousStructure
from openEMS import openEMS


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stl", type=Path, required=True)
    args = parser.parse_args()
    if not args.stl.is_file():
        parser.error(f"swept-solid mesh does not exist: {args.stl}")

    csx = ContinuousStructure()
    material = csx.AddMetal("swept_solid")
    polyhedron = material.AddPolyhedronReader(str(args.stl), priority=10)
    if not polyhedron.ReadFile():
        raise ValueError(f"CSXCAD could not read swept-solid mesh: {args.stl}")

    fdtd = openEMS()
    fdtd.SetCSX(csx)
    fdtd.SetGaussExcite(1.0e9, 0.5e9)
    fdtd.SetBoundaryCond(["MUR"] * 6)
    fdtd.AddLumpedPort(
        1, 50.0, [0.0, 0.0, 0.0], [0.0, 0.0, 1.0],
        "z", 1.0, edges2grid="xy"
    )
    with tempfile.TemporaryDirectory(prefix="oem-sweep-") as directory:
        model = Path(directory) / "sweep.xml"
        fdtd.Write2XML(str(model))
        if not model.is_file() or model.stat().st_size == 0:
            raise ValueError("openEMS did not write the swept-solid model XML")


if __name__ == "__main__":
    main()
