# OEM project format

An `.oem` project is a self-contained ZIP archive. Its `project.xml` manifest is
XML and has `format-version="0.0.1"`. The archive contains native solver and
geometry assets rather than converting every payload to a custom XML encoding.

Typical archive layout:

```text
project.oem
├── project.xml
├── geometry/
│   └── solid_0.step
├── models/
│   ├── openems.xml
│   └── geometry/solid_0.stl
└── results/
    ├── s11_antenna.s1p
    ├── s11_antenna.csv
    └── summary_cpu.json
```

`project.xml` describes the design parameters, materials, geometry bodies,
feature history, simulation settings, native solver assets, and derived
simulation results. Asset paths are relative to the archive root.

## Design and feature history

- Each parameter has an ID, name, unit, expression, and evaluated value.
  Optimizable parameters can also carry bounds and a step size.
- Each body references its own material ID. Different body materials are stored
  separately; bodies may share an ID only when they use the same material.
- Feature-history nodes preserve primitive creation, transforms, Boolean
  operations, curve profiles, curve/face sweeps, and closed-shell-to-solid
  operations. Inputs refer to earlier feature outputs.
- Curves created in the viewport are saved as point-based polyline features;
  the curve plane Z can change between points to make a 3D path. Sweep-generated
  surfaces and imported B-rep shapes are stored as STEP assets; non-primitive
  feature outputs are cached as `history/<feature-id>.step` so their evaluated
  shapes remain available alongside the operation graph.
- Feature arguments reference parameter IDs and carry an evaluated value.
  Optimization metadata (bounds/step) is supported by the manifest. The current
  GUI stores numeric expressions for its built-in dimensions; it does not yet
  provide a general expression editor or run an optimizer. The feature graph
  and non-primitive shape caches are preserved, but the GUI does not yet offer
  a general editor that replays every downstream feature after changing an
  upstream feature.

An open shell is not silently made thick or treated as a solid. The
`solidify.closed-shell` operation requires a valid closed shell with positive
volume. OpenEMS volume export accepts only closed solids; a surface must be
solidified or otherwise converted through an explicitly supported solver
representation before running a volume simulation.

## Native formats and solver assets

- `models/*.xml` is written by openEMS `Write2XML()` and contains the
  solver-ready CSXCAD geometry, material properties, and simulation setup.
- Imported or sweep-generated geometry may also be stored as STEP B-rep assets.
- Tessellated solver geometry is stored as STL/PLY assets under `models/`.
  References inside the native XML use package-relative paths.
- S-parameters are included in Touchstone (`.s1p`/`.s2p`) as well as the CSV
  cache used by the current plots. Processed near/far-field exports and
  summaries are included. Raw field dumps such as HDF5
  `Ef_patch.h5`/`Hf_patch.h5` are not included in version 0.0.1; rerun the
  simulation for new raw-field post-processing.

The application currently writes only version 0.0.1 and does not migrate files
from the earlier prototype layout. Unsupported versions are rejected.

## Sample projects

- [Waveguide sample](../waveguide.oem): TE10 rectangular waveguide with native
  openEMS model and processed S-parameter, VSWR, and waveform results.
- [Antenna sample](../antenna.oem): patch antenna with native openEMS model and
  processed S11, near-field, and far-field results.
