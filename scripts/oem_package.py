#!/usr/bin/env python3
"""Pack or safely extract an OEM project archive."""

import argparse
import os
from pathlib import Path, PurePosixPath
import stat
import tempfile
import zipfile


def pack(source: Path, destination: Path) -> None:
    source = source.resolve(strict=True)
    if not source.is_dir():
        raise ValueError(f"Package source is not a directory: {source}")
    destination = destination.resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary_name = tempfile.mkstemp(
        prefix=f".{destination.name}.", suffix=".tmp", dir=destination.parent
    )
    os.close(fd)
    temporary = Path(temporary_name)
    try:
        with zipfile.ZipFile(
            temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6
        ) as archive:
            for entry in sorted(source.rglob("*")):
                if entry.is_symlink():
                    raise ValueError(f"Symbolic links are not allowed: {entry}")
                if not entry.is_file():
                    continue
                relative = entry.relative_to(source).as_posix()
                _validate_member(relative)
                archive.write(entry, relative)
        with zipfile.ZipFile(temporary, "r") as archive:
            bad_member = archive.testzip()
            if bad_member is not None:
                raise ValueError(f"ZIP CRC check failed for {bad_member}")
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


def unpack(source: Path, destination: Path) -> None:
    source = source.resolve(strict=True)
    destination = destination.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(source, "r") as archive:
        members = archive.infolist()
        if len(members) > 100_000:
            raise ValueError("OEM archive contains too many entries")
        for member in members:
            relative = _validate_member(member.filename)
            mode = member.external_attr >> 16
            if stat.S_ISLNK(mode):
                raise ValueError(f"Symbolic links are not allowed: {member.filename}")
            target = (destination / Path(*relative.parts)).resolve()
            if not target.is_relative_to(destination):
                raise ValueError(f"Archive entry escapes extraction directory: {member.filename}")
            if member.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(member, "r") as input_file, target.open("wb") as output_file:
                while chunk := input_file.read(1024 * 1024):
                    output_file.write(chunk)


def _validate_member(name: str) -> PurePosixPath:
    if not name or "\\" in name:
        raise ValueError(f"Invalid archive entry: {name!r}")
    path = PurePosixPath(name)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        raise ValueError(f"Invalid archive entry: {name!r}")
    if path.parts and ":" in path.parts[0]:
        raise ValueError(f"Invalid archive entry: {name!r}")
    return path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    pack_parser = commands.add_parser("pack")
    pack_parser.add_argument("source", type=Path)
    pack_parser.add_argument("destination", type=Path)
    unpack_parser = commands.add_parser("unpack")
    unpack_parser.add_argument("source", type=Path)
    unpack_parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "pack":
            pack(args.source, args.destination)
        else:
            unpack(args.source, args.destination)
    except (OSError, ValueError, zipfile.BadZipFile, RuntimeError) as error:
        parser.exit(1, f"OEM package error: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
