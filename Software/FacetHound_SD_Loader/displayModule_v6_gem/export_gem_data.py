#!/usr/bin/env python3
"""Export a GemCAD fallback mesh as compact, RP2040-friendly C++ tables.

The live SD path now constructs meshes on the Pico 2. This utility remains for
changing the display firmware's built-in mesh shown before a runtime transfer.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import math
import pathlib
import sys

import numpy as np


SCREEN_W = 320
SCREEN_H = 480

# Kept in the same normalized locations as gem_display_static.py.
PANELS = (
    ("TOP", (0.055, 0.635, 0.430, 0.270), (0, 1), (1.0, 1.0), 2, 1.0, "top"),
    ("BOTTOM", (0.515, 0.635, 0.430, 0.270), (0, 1), (1.0, -1.0), 2, -1.0, "bottom"),
    ("FRONT", (0.055, 0.335, 0.430, 0.270), (0, 2), (1.0, 1.0), 1, -1.0, "front"),
    ("SIDE", (0.515, 0.335, 0.430, 0.270), (1, 2), (1.0, 1.0), 0, 1.0, "side"),
)


def _cpp_string(value: str) -> str:
    return (
        str(value)
        .replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\r", " ")
        .replace("\n", " ")
    )


def _map_panel(points: np.ndarray, panel) -> np.ndarray:
    _, (x0, y0, w, h), axes, flip, *_ = panel
    coords = points[:, list(axes)].copy()
    coords[:, 0] *= flip[0]
    coords[:, 1] *= flip[1]

    cmin = coords.min(axis=0)
    cmax = coords.max(axis=0)
    center = 0.5 * (cmin + cmax)
    span = np.maximum(cmax - cmin, 1e-9)
    scale = 0.88 * min((w * SCREEN_W) / span[0], (h * SCREEN_H) / span[1])

    x = (x0 + 0.5 * w) * SCREEN_W + (coords[:, 0] - center[0]) * scale
    y_axes = (y0 + 0.5 * h) * SCREEN_H + (coords[:, 1] - center[1]) * scale
    y = SCREEN_H - y_axes
    return np.rint(np.column_stack((x, y))).astype(np.int16)


def _edge_visible(points: np.ndarray, edge, panel) -> bool:
    _, _, _, _, view_axis, view_sign, mode = panel
    a, b = edge

    if mode in ("top", "bottom"):
        za = points[a, 2]
        zb = points[b, 2]
        zmid = 0.5 * (za + zb)
        zspan = float(np.ptp(points[:, 2]))
        ztol = max(1e-7, 0.025 * zspan)

        if abs(zmid) <= ztol or (za <= 0.0 <= zb) or (zb <= 0.0 <= za):
            return True
        return zmid > 0.0 if mode == "top" else zmid < 0.0

    midpoint = 0.5 * (points[a] + points[b])
    span = float(np.max(np.abs(points[:, view_axis]))) + 1e-9
    return view_sign * midpoint[view_axis] >= -0.04 * span


def _target_pose(normal: np.ndarray, tilt: float, index_res: int, index_sign: int):
    z_axis = np.asarray(normal, dtype=float)
    z_axis /= max(np.linalg.norm(z_axis), 1e-12)
    up = np.array([0.0, 0.0, -1.0 if tilt < 0.0 else 1.0])
    y_axis = up - np.dot(up, z_axis) * z_axis

    if np.linalg.norm(y_axis) < 1e-12:
        fallback = np.array([0.0, 1.0, 0.0])
        y_axis = fallback - np.dot(fallback, z_axis) * z_axis
    if np.linalg.norm(y_axis) < 1e-12:
        fallback = np.array([1.0, 0.0, 0.0])
        y_axis = fallback - np.dot(fallback, z_axis) * z_axis

    y_axis /= max(np.linalg.norm(y_axis), 1e-12)
    x_axis = np.cross(y_axis, z_axis)
    x_axis /= max(np.linalg.norm(x_axis), 1e-12)
    rotation = np.stack([x_axis, y_axis, z_axis])

    dop = rotation @ np.array([0.0, 0.0, 1.0])
    dop /= max(np.linalg.norm(dop), 1e-12)
    theta = math.degrees(math.atan2(-dop[1], dop[2]))

    rad = math.radians(theta)
    c, s = math.cos(-rad), math.sin(-rad)
    un_tip = np.array(((1, 0, 0), (0, c, -s), (0, s, c)), dtype=float)
    flat = un_tip @ rotation
    twist_deg = math.degrees(math.atan2(flat[1, 0], flat[0, 0]))
    twist = (index_sign * twist_deg * index_res / 360.0) % index_res
    return abs(theta), twist


def _fmt_rows(rows, indent="    "):
    return "\n".join(f"{indent}{row}" for row in rows)


def export_header(gem_utils: pathlib.Path, asc_path: pathlib.Path, output: pathlib.Path):
    sys.path.insert(0, str(gem_utils))
    from gem_generator import build_gem  # pylint: disable=import-error,import-outside-toplevel

    # gem_generator has useful progress output interactively, but generated files
    # should have deterministic stdout from this wrapper.
    captured = io.StringIO()
    with contextlib.redirect_stdout(captured):
        points, support, edges, planes, meta, index_res, index_sign, gem_dict = build_gem(asc_path)

    points = np.asarray(points, dtype=float)
    edges = [tuple(map(int, edge)) for edge in edges]
    static_points = [_map_panel(points, panel) for panel in PANELS]

    radius = float(np.max(np.linalg.norm(points, axis=1)))
    if radius <= 1e-12:
        radius = 1.0

    edge_rows = []
    for edge in edges:
        a, b = edge
        shared = set(support[a]).intersection(support[b])
        masks = [0, 0, 0, 0]
        for plane_id in shared:
            if 0 <= plane_id < 128:
                masks[plane_id // 32] |= 1 << (plane_id % 32)

        view_mask = 0
        for panel_id, panel in enumerate(PANELS):
            if _edge_visible(points, edge, panel):
                view_mask |= 1 << panel_id

        edge_rows.append(
            "{%d, %d, 0x%02Xu, {0x%08Xu, 0x%08Xu, 0x%08Xu, 0x%08Xu}},"
            % (a, b, view_mask, *masks)
        )

    plane_rows = []
    for plane_id, ((normal, _distance), facet) in enumerate(zip(planes, meta)):
        tip, twist = _target_pose(
            np.asarray(normal), float(facet["tilt"]), int(index_res), int(index_sign)
        )
        plane_rows.append(
            "{%0.5ff, %0.5ff, %u, %u},"
            % (tip, twist, int(facet["tier"]) + 1, int(facet["twist"]) + 1)
        )

    point_rows = ["{%0.7ff, %0.7ff, %0.7ff}," % tuple(point) for point in points]

    static_blocks = []
    for mapped in static_points:
        static_blocks.append(
            "{\n"
            + _fmt_rows(["{%d, %d}," % tuple(point) for point in mapped], "        ")
            + "\n    }"
        )

    title = _cpp_string(gem_dict.get("boldTitle") or asc_path.stem)
    text = f"""// Generated by export_gem_data.py from {asc_path.name}. Do not hand-edit.
#pragma once

#include <Arduino.h>

namespace GemData {{

constexpr uint16_t kVertexCount = {len(points)};
constexpr uint16_t kEdgeCount = {len(edges)};
constexpr uint16_t kPlaneCount = {len(planes)};
constexpr uint16_t kIndexResolution = {int(index_res)};
constexpr int8_t kIndexSign = {int(index_sign)};
constexpr float kRadius = {radius:.7f}f;
constexpr char kTitle[] = "{title}";

struct Vertex {{ float x, y, z; }};
struct ScreenPoint {{ int16_t x, y; }};
struct Edge {{
    uint8_t a;
    uint8_t b;
    uint8_t viewMask;
    uint32_t planeMask[4];
}};
struct PlanePose {{ float tipDegrees, twistTicks; uint8_t tier, facet; }};

constexpr Vertex kVertices[kVertexCount] = {{
{_fmt_rows(point_rows)}
}};

constexpr Edge kEdges[kEdgeCount] = {{
{_fmt_rows(edge_rows)}
}};

constexpr ScreenPoint kStaticPoints[4][kVertexCount] = {{
{',\n'.join(static_blocks)}
}};

constexpr PlanePose kPlanes[kPlaneCount] = {{
{_fmt_rows(plane_rows)}
}};

}}  // namespace GemData
"""

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding="utf-8", newline="\n")
    print(
        f"wrote {output} ({len(points)} vertices, {len(edges)} edges, "
        f"{len(planes)} facet planes)"
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("asc", type=pathlib.Path, help="GemCAD .asc design")
    parser.add_argument("output", type=pathlib.Path, help="output gem_data.h")
    parser.add_argument(
        "--gem-utils",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[3] / "gemUtils",
        help="directory containing gem_generator.py",
    )
    args = parser.parse_args()
    export_header(args.gem_utils.resolve(), args.asc.resolve(), args.output.resolve())


if __name__ == "__main__":
    main()
