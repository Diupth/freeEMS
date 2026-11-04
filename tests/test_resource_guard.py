#!/usr/bin/env python3
"""Verify oversized meshes are rejected before the FDTD engine starts."""

import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> None:
    script = Path(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="rfsim_resource_guard_") as output:
        result = subprocess.run(
            [
                sys.executable,
                str(script),
                "--output",
                output,
                "--max-cells",
                "1000",
            ],
            capture_output=True,
            text=True,
            check=False,
        )
    if result.returncode != 2 or "exceeding --max-cells" not in result.stderr:
        raise SystemExit(
            "Expected mesh resource guard to reject the test model.\n"
            f"exit={result.returncode}\nstdout={result.stdout}\nstderr={result.stderr}"
        )
    print("Resource guard rejected an oversized mesh before solver execution.")


if __name__ == "__main__":
    main()
