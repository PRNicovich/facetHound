#!/usr/bin/env python3
"""Build Facet Hound .fhc SD caches for one ASC file or an entire folder.

The output is the same versioned, source-CRC-checked format written by the
Pico 2 base firmware. Keep each .fhc beside its same-named .asc on the card.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import pathlib
import struct
import sys
import zlib

import numpy as np


MAGIC = b"FHCACHE1"
VERSION = 1
HEADER = struct.Struct("<8sHHIIddbBHIHHHHf48s64s")
CUT = struct.Struct("<dddHH12s")
VERTEX = struct.Struct("<ddd")
EDGE = struct.Struct("<HHB8H")
PLANE = struct.Struct("<ddddffHH12s")

MAX_CUTS = 2048
MAX_PLANES = 512
MAX_VERTICES = 1024
MAX_EDGES = 2048
MAX_EDGE_SUPPORTS = 8

assert HEADER.size == 168
assert CUT.size == 40
assert VERTEX.size == 24
assert EDGE.size == 21
assert PLANE.size == 56


def fixed_text(value: object, size: int) -> bytes:
    encoded = str(value or "").replace("\r", " ").replace("\n", " ").encode(
        "utf-8", "replace"
    )
    return encoded[: size - 1].ljust(size, b"\0")


def source_crc(path: pathlib.Path) -> int:
    value = 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value = zlib.crc32(block, value)
    return value & 0xFFFFFFFF


def load_builder(script_path: pathlib.Path):
    gem_utils = script_path.resolve().parents[2] / "gemUtils"
    sys.path.insert(0, str(gem_utils))
    from gem_generator import build_gem  # pylint: disable=import-error,import-outside-toplevel

    return build_gem


def propagated_names(meta: list[dict]) -> list[str]:
    names: dict[int, str] = {}
    for item in meta:
        name = str(item.get("facet_name") or "")
        if name and int(item["tier"]) not in names:
            names[int(item["tier"])] = name
    return [names.get(int(item["tier"]), "") for item in meta]


def convert_file(source: pathlib.Path, output: pathlib.Path, build_gem) -> None:
    captured = io.StringIO()
    with contextlib.redirect_stdout(captured):
        points, supports, edges, planes, meta, wheel, index_sign, gem = build_gem(source)

    points = np.asarray(points, dtype=float)
    if not (0 < len(meta) <= MAX_CUTS and len(meta) <= MAX_PLANES):
        raise ValueError(f"unsupported plane/cut count: {len(meta)}")
    if not (0 < len(points) <= MAX_VERTICES):
        raise ValueError(f"unsupported vertex count: {len(points)}")
    if len(edges) > MAX_EDGES:
        raise ValueError(f"unsupported edge count: {len(edges)}")

    names = propagated_names(meta)
    tier_count = max((int(item["tier"]) + 1 for item in meta), default=0)
    radius = float(np.max(np.linalg.norm(points, axis=1))) if len(points) else 1.0
    meridian = float(gem.get("meridian", 0.0) or 0.0)
    title = str(gem.get("boldTitle") or source.stem)
    source_size = source.stat().st_size

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    with temporary.open("wb") as stream:
        stream.write(
            HEADER.pack(
                MAGIC,
                VERSION,
                HEADER.size,
                source_size,
                source_crc(source),
                float(wheel),
                meridian,
                int(index_sign),
                0,
                tier_count,
                len(meta),
                len(points),
                len(edges),
                len(planes),
                0,
                radius,
                fixed_text(title, 48),
                fixed_text(source.name, 64),
            )
        )

        for item, name in zip(meta, names):
            stream.write(
                CUT.pack(
                    float(item["tilt"]),
                    float(item["distance"]),
                    float(item["raw_index"]),
                    int(item["tier"]) + 1,
                    int(item["twist"]) + 1,
                    fixed_text(name, 12),
                )
            )

        for point in points:
            stream.write(VERTEX.pack(*(float(value) for value in point)))

        for a, b in edges:
            shared = sorted(set(supports[int(a)]).intersection(supports[int(b)]))
            if len(shared) > MAX_EDGE_SUPPORTS:
                raise ValueError(f"edge {a}-{b} has {len(shared)} supporting planes")
            padded = shared + [0] * (MAX_EDGE_SUPPORTS - len(shared))
            stream.write(EDGE.pack(int(a), int(b), len(shared), *padded))

        for ((normal, distance), item, name) in zip(planes, meta, names):
            stream.write(
                PLANE.pack(
                    float(normal[0]),
                    float(normal[1]),
                    float(normal[2]),
                    float(distance),
                    float(item["tilt"]),
                    float(item["raw_index"]),
                    int(item["tier"]) + 1,
                    int(item["twist"]) + 1,
                    fixed_text(name, 12),
                )
            )

    temporary.replace(output)
    print(
        f"{source.name} -> {output.name}: {len(meta)} planes, "
        f"{len(points)} vertices, {len(edges)} edges"
    )


def find_sources(path: pathlib.Path, recursive: bool) -> list[pathlib.Path]:
    if path.is_file():
        return [path]
    iterator = path.rglob("*.asc") if recursive else path.glob("*.asc")
    return sorted(item for item in iterator if item.is_file())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=pathlib.Path, help="ASC file or folder")
    parser.add_argument("--output-dir", type=pathlib.Path)
    parser.add_argument("--recursive", action="store_true")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    sources = find_sources(args.path.resolve(), args.recursive)
    if not sources:
        parser.error("no .asc files found")
    build_gem = load_builder(pathlib.Path(__file__))
    failures = 0
    for source in sources:
        output = (
            args.output_dir.resolve() / f"{source.stem}.fhc"
            if args.output_dir
            else source.with_suffix(".fhc")
        )
        if output.exists() and not args.force:
            print(f"skip existing: {output}")
            continue
        try:
            convert_file(source, output, build_gem)
        except Exception as exc:  # continue a bulk run and summarize failures
            failures += 1
            print(f"ERROR {source}: {exc}", file=sys.stderr)
    print(f"done: {len(sources) - failures} converted, {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
