# -*- coding: utf-8 -*-

import numpy as np
from matplotlib.collections import LineCollection
from matplotlib.patches import Rectangle, Polygon

from gem_rotation import (
    mat_to_quat,
    wrap_ticks_signed,
    positive_ticks,
    dop_twist_matrix,
    page_tip_matrix,
    tip_twist_from_R,
)


MCU_FONT = "DejaVu Sans Mono"


def wrap_title_for_left_side(title, max_chars=16, max_lines=2):
    title = str(title).strip()

    if not title:
        return "BOLD"

    words = title.split()
    lines = []
    current = ""

    for word in words:
        test = word if not current else current + " " + word

        if len(test) <= max_chars:
            current = test
        else:
            if current:
                lines.append(current)
            current = word

        if len(lines) >= max_lines:
            break

    if current and len(lines) < max_lines:
        lines.append(current)

    if not lines:
        lines = [title[:max_chars]]

    used = " ".join(lines)
    if len(used) < len(title):
        last = lines[-1]
        if len(last) >= max_chars:
            last = last[:max_chars - 1]
        lines[-1] = last + "…"

    return "\n".join(lines)


class Viewer:
    """
    Static GemCAD-style diagram display.

    Four lightweight paper-style projections:

        TOP      = looking from +Z, crown/table side
        BOTTOM   = looking from -Z, pavilion/culet side
        FRONT    = looking from -Y, X/Z profile
        SIDE     = looking from +X, Y/Z profile

    Green:
        selected facet

    Dim green ghost:
        selected far-side facet only, FRONT/SIDE only

    Amber:
        index-0 orientation fiducial

    TOP:
        table + crown + girdle

    BOTTOM:
        pavilion + culet + girdle
    """

    def __init__(
        self,
        ax,
        pts,
        edges,
        support,
        index_res=96,
        index_sign=1,
        meridian=0.0,
        facet_meta=None,
        highlighted_facet_always_up=True,
        rpm_set=0.0,
        rpm_actual=0.0,
        flow_set=0.0,
        flow_actual=0.0,
        force_value=0.35,
        gem_name="BOLD",
        status_symbols="● ◆ ▲",
        highlight_planes=None
    ):
        self.ax = ax
        self.fig = ax.figure

        self.pts = pts
        self.edges = edges
        self.support = support
        self.index_res = index_res
        self.index_sign = index_sign
        self.meridian = float(meridian or 0.0)
        self.facet_meta = facet_meta
        self.highlighted_facet_always_up = highlighted_facet_always_up

        self.rpm_set = rpm_set
        self.rpm_actual = rpm_actual
        self.flow_set = flow_set
        self.flow_actual = flow_actual
        self.force_value = force_value

        self.gem_name = gem_name
        self.status_symbols = status_symbols

        self.highlight_planes = set(highlight_planes or [])

        self.tier_number = 1
        self.tier_count = 1
        self.tier_prev_value = 0.0
        self.tier_next_value = 0.0

        self.facet_number = 1
        self.facet_count = 1
        self.facet_prev_value = 0.0
        self.facet_next_value = 0.0

        self.girdle_center = np.array([0.0, 0.0, 0.0], dtype=float)
        self.center = self.girdle_center.copy()

        self.gem_R = np.eye(3)
        self.display_shift = np.zeros(3)

        self.target_plane_id = None
        self.target_R = None

        self.animating = False
        self.anim_step = 0
        self.anim_steps = 1
        self.anim_q0 = mat_to_quat(self.gem_R)
        self.anim_q1 = mat_to_quat(self.gem_R)
        self.anim_shift0 = self.display_shift.copy()
        self.anim_shift1 = self.display_shift.copy()

        self.fig.patch.set_facecolor("#050608")
        self.ax.set_facecolor("#050608")

        self.ax.set_xlim(0.0, 1.0)
        self.ax.set_ylim(0.0, 1.0)
        self.ax.axis("off")

        # -------------------------------------------------
        # Four panels above the HUD.
        # -------------------------------------------------

        self.panels = {
            "TOP": {
                "box": (0.055, 0.635, 0.430, 0.270),
                "axes": (0, 1),
                "flip": (1.0, 1.0),
                "view_axis": 2,
                "view_sign": 1.0,
                "mode": "top",
                "label": "TOP"
            },
            "BOTTOM": {
                "box": (0.515, 0.6350, 0.430, 0.270),
                "axes": (0, 1),
                "flip": (1.0, -1.0),
                "view_axis": 2,
                "view_sign": -1.0,
                "mode": "bottom",
                "label": "BOTTOM"
            },
            "FRONT": {
                "box": (0.055, 0.3350, 0.430, 0.270),
                "axes": (0, 2),
                "flip": (1.0, 1.0),
                "view_axis": 1,
                "view_sign": -1.0,
                "mode": "front",
                "label": "FRONT"
            },
            "SIDE": {
                "box": (0.515, 0.3350, 0.430, 0.270),
                "axes": (1, 2),
                "flip": (1.0, 1.0),
                "view_axis": 0,
                "view_sign": 1.0,
                "mode": "side",
                "label": "SIDE"
            },
        }

        self.proj_pts = {}
        self.proj_maps = {}
        self.panel_edges = {}
        self.base_collections = {}
        self.highlight_collections = {}
        self.ghost_patches = {}
        self.orientation_collections = {}
        self.orientation_texts = {}

        for name, p in self.panels.items():
            p_edges = self.filter_edges_for_view(
                view_axis=p["view_axis"],
                view_sign=p["view_sign"],
                mode=p["mode"]
            )
            self.panel_edges[name] = p_edges

            coords = self.pts[:, list(p["axes"])].copy()
            coords[:, 0] *= p["flip"][0]
            coords[:, 1] *= p["flip"][1]

            mapped, mapping = self.map_projection_to_box(
                coords,
                p["box"],
                return_mapping=True
            )

            self.proj_pts[name] = mapped
            self.proj_maps[name] = mapping

            base_segments = [
                [mapped[a], mapped[b]]
                for a, b in p_edges
            ]

            base_collection = LineCollection(
                base_segments,
                colors=[(0.70, 0.70, 0.70, 0.56)],
                linewidths=0.70,
                zorder=10
            )
            ax.add_collection(base_collection)

            ghost_patch = Polygon(
                np.zeros((3, 2)),
                closed=True,
                transform=ax.transAxes,
                facecolor=(0.00, 0.90, 0.25, 0.075),
                edgecolor=(0.00, 1.00, 0.25, 0.34),
                linewidth=1.35,
                linestyle="--",
                zorder=22,
                visible=False
            )
            ax.add_patch(ghost_patch)
            self.ghost_patches[name] = ghost_patch

            highlight_collection = LineCollection(
                [],
                colors=[(0.00, 1.00, 0.25, 0.98)],
                linewidths=2.15,
                zorder=30
            )
            ax.add_collection(highlight_collection)

            self.base_collections[name] = base_collection
            self.highlight_collections[name] = highlight_collection

            orient_collection = LineCollection(
                [],
                colors=[(1.00, 0.68, 0.00, 0.98)],
                linewidths=1.25,
                zorder=42
            )
            ax.add_collection(orient_collection)
            self.orientation_collections[name] = orient_collection

            x0, y0, w, h = p["box"]

            ax.text(
                x0 + 0.006,
                y0 + h - 0.006,
                p["label"],
                transform=ax.transAxes,
                ha="left",
                va="top",
                fontsize=8,
                color="#777777",
                family=MCU_FONT,
                zorder=50
            )

            frame = Rectangle(
                (x0, y0),
                w,
                h,
                transform=ax.transAxes,
                facecolor="none",
                edgecolor="#1a1f2a",
                linewidth=0.7,
                alpha=0.90,
                zorder=5
            )
            ax.add_patch(frame)

        self.build_orientation_marker()

        # -------------------------------------------------
        # Top status
        # -------------------------------------------------

        self.no_tiers_text = ax.text(
            0.50, 0.50, "",
            transform=ax.transAxes,
            ha="center",
            va="center",
            fontsize=13,
            color="#e8e8e8",
            zorder=100,
            family=MCU_FONT,
            bbox=dict(
                facecolor="#11151c",
                alpha=0.84,
                edgecolor="#333946",
                linewidth=0.7,
                pad=3.0
            )
        )

        self.title_text = ax.text(
            0.025, 0.975,
            wrap_title_for_left_side(self.gem_name),
            transform=ax.transAxes,
            ha="left",
            va="top",
            fontsize=10,
            color="#f0f0f0",
            zorder=100,
            family=MCU_FONT,
            fontweight="bold",
            linespacing=0.90
        )

        self.status_text = ax.text(
            0.975, 0.975,
            f"{self.status_symbols} W{self.index_res}",
            transform=ax.transAxes,
            ha="right",
            va="top",
            fontsize=11,
            color="#d8d8d8",
            zorder=100,
            family=MCU_FONT,
            fontweight="bold"
        )

        # -------------------------------------------------
        # Bottom HUD
        # -------------------------------------------------

        self.theta_text = ax.text(
            0.50, 0.268, "",
            transform=ax.transAxes,
            ha="center",
            va="bottom",
            fontsize=18,
            color="#ffd84d",
            zorder=100,
            family=MCU_FONT,
            fontweight="bold"
        )

        self.theta_nav_text = ax.text(
            0.50, 0.232, "",
            transform=ax.transAxes,
            ha="center",
            va="bottom",
            fontsize=13,
            color="#ffd84d",
            zorder=100,
            family=MCU_FONT,
            fontweight="bold"
        )

        self.force_bar_bg = Rectangle(
            (0.045, 0.201),
            0.91,
            0.020,
            transform=ax.transAxes,
            facecolor="#20242e",
            edgecolor="#554900",
            linewidth=0.7,
            alpha=0.90,
            zorder=101
        )
        ax.add_patch(self.force_bar_bg)

        self.force_bar_fill = Rectangle(
            (0.045, 0.201),
            0.01,
            0.020,
            transform=ax.transAxes,
            facecolor="#ffd84d",
            edgecolor="none",
            alpha=0.95,
            zorder=102
        )
        ax.add_patch(self.force_bar_fill)

        self.index_text = ax.text(
            0.50, 0.133, "",
            transform=ax.transAxes,
            ha="center",
            va="bottom",
            fontsize=18,
            color="#40e8ff",
            zorder=100,
            family=MCU_FONT,
            fontweight="bold"
        )

        self.index_nav_text = ax.text(
            0.50, 0.095, "",
            transform=ax.transAxes,
            ha="center",
            va="bottom",
            fontsize=11,
            color="#40e8ff",
            zorder=100,
            family=MCU_FONT
        )

        self.z_text = ax.text(
            0.025, 0.032, "",
            transform=ax.transAxes,
            ha="left",
            va="bottom",
            fontsize=12,
            color="#ff63d8",
            zorder=100,
            family=MCU_FONT,
            fontweight="bold"
        )

        self.rpm_text = ax.text(
            0.50, 0.032, "",
            transform=ax.transAxes,
            ha="center",
            va="bottom",
            fontsize=10,
            color="#d8d8d8",
            zorder=100,
            family=MCU_FONT
        )

        self.flow_text = ax.text(
            0.975, 0.032, "",
            transform=ax.transAxes,
            ha="right",
            va="bottom",
            fontsize=10,
            color="#d8d8d8",
            zorder=100,
            family=MCU_FONT
        )

    # =====================================================
    # Orientation fiducial
    # =====================================================

    def index_zero_angle(self):
        """
        Static paper-diagram orientation marker.

        Gem geometry uses mathematical azimuth:
            0 rad = +X

        The diagram marker should read like an index wheel:
            index 0 = paper-up / +Y

        So add +90 degrees to the GemCAD index azimuth.
        """
        gemcad_angle = (
            self.index_sign
            * 2.0
            * np.pi
            * ((0.0 - self.meridian) % self.index_res)
            / self.index_res
        )

        return gemcad_angle + 0.5 * np.pi

    def orientation_marker_3d_points(self):
        """
        Build a tiny 3D fiducial at index 0.

        base  = at girdle radius
        outer = just outside girdle
        flag  = vertical flag point so FRONT/SIDE still show orientation
        label = a little farther out for top/bottom "0"
        """
        a = self.index_zero_angle()

        u = np.array([np.cos(a), np.sin(a), 0.0], dtype=float)

        xy = self.pts[:, :2]
        r = np.max(np.linalg.norm(xy, axis=1))

        z_span = np.max(self.pts[:, 2]) - np.min(self.pts[:, 2])
        flag_z = 0.12 * z_span

        base = 1.005 * r * u
        outer = 1.005 * r * u
        flag = outer + np.array([0.0, 0.0, flag_z], dtype=float)
        label = 1.235 * r * u

        return {
            "base": base,
            "outer": outer,
            "flag": flag,
            "label": label,
        }

    def project_marker_point(self, point3, panel_name):
        panel = self.panels[panel_name]
        mapping = self.proj_maps[panel_name]

        coords = np.array(
            [[point3[panel["axes"][0]], point3[panel["axes"][1]]]],
            dtype=float
        )

        coords[:, 0] *= panel["flip"][0]
        coords[:, 1] *= panel["flip"][1]

        return self.apply_projection_mapping(coords, mapping)[0]

    def build_orientation_marker(self):
        marker = self.orientation_marker_3d_points()

        for name, panel in self.panels.items():
            # No tick lines, no labels, no patch. Just a small orange ".".
            if name in self.orientation_collections:
                self.orientation_collections[name].set_segments([])

            dot_pos = self.project_marker_point(marker["outer"], name)

            self.ax.text(
                dot_pos[0],
                dot_pos[1],
                ".",
                transform=self.ax.transAxes,
                ha="center",
                va="center",
                fontsize=8,
                color="#ff9f00",
                family=MCU_FONT,
                fontweight="bold",
                zorder=44
            )

    # =====================================================
    # Edge / facet visibility filtering
    # =====================================================

    def plane_side(self, plane_id):
        if self.facet_meta is None:
            return "both"

        if not (0 <= plane_id < len(self.facet_meta)):
            return "both"

        tilt = float(self.facet_meta[plane_id].get("tilt", 0.0))

        # Near-vertical tiers are girdle / girdle-like cutting planes.
        # They should be allowed in both top and bottom diagrams.
        if abs(abs(tilt) - 90.0) < 1e-6:
            return "both"

        # Flat table-like tiers are top/crown, not both.
        if tilt >= 0.0:
            return "top"

        return "bottom"

    def edge_has_same_facet_side(self, edge, side):
        """
        TOP/BOTTOM visibility.

        Important:
            Table is top only.
            Pavilion is bottom only.
            Girdle is visible in both.

        The girdle is detected geometrically near GemCAD Z=0 because many
        girdle outline edges are formed by crown/pavilion intersections rather
        than by an explicit girdle facet plane.
        """
        a, b = edge

        za = self.pts[a, 2]
        zb = self.pts[b, 2]
        zmid = 0.5 * (za + zb)

        z_span = np.max(self.pts[:, 2]) - np.min(self.pts[:, 2])
        z_tol = max(1e-7, 0.025 * z_span)

        # Girdle line: show in both top and bottom diagrams.
        if abs(zmid) <= z_tol or (za <= 0.0 <= zb) or (zb <= 0.0 <= za):
            return True

        # Pure geometric near-side rule prevents table leaking to bottom.
        if side == "top" and zmid > 0.0:
            return True

        if side == "bottom" and zmid < 0.0:
            return True

        return False

    def edge_near_view_axis(self, edge, view_axis, view_sign):
        """
        Simple hidden-edge filter for side/front views.

        Keep edges whose midpoint is on the viewer-facing half,
        plus near-midline edges.
        """
        a, b = edge
        mid = 0.5 * (self.pts[a] + self.pts[b])

        coord = mid[view_axis]
        span = np.max(np.abs(self.pts[:, view_axis])) + 1e-9
        threshold = -0.04 * span

        return view_sign * coord >= threshold

    def filter_edges_for_view(self, view_axis, view_sign, mode):
        filtered = []

        for edge in self.edges:
            if mode == "top":
                if not self.edge_has_same_facet_side(edge, "top"):
                    continue

            elif mode == "bottom":
                if not self.edge_has_same_facet_side(edge, "bottom"):
                    continue

            elif mode in ("front", "side"):
                if not self.edge_near_view_axis(edge, view_axis, view_sign):
                    continue

            filtered.append(edge)

        return filtered

    def facet_vertices(self, plane_id):
        return [
            vi for vi, s in enumerate(self.support)
            if plane_id in s
        ]

    def facet_center(self, plane_id):
        face_vertices = self.facet_vertices(plane_id)

        if face_vertices:
            return self.pts[face_vertices].mean(axis=0)

        return self.center.copy()

    def facet_near_for_view(self, plane_id, panel):
        mode = panel["mode"]

        fc = self.facet_center(plane_id)

        z_span = np.max(self.pts[:, 2]) - np.min(self.pts[:, 2])
        z_tol = max(1e-7, 0.025 * z_span)

        if mode == "top":
            # Table/crown side only. Girdle-adjacent facets can still count.
            return fc[2] >= -z_tol

        if mode == "bottom":
            # Pavilion/culet side only. This keeps the table off bottom.
            return fc[2] <= z_tol

        # FRONT/SIDE: use selected facet center relative to view direction.
        view_axis = panel["view_axis"]
        view_sign = panel["view_sign"]

        span = np.max(np.abs(self.pts[:, view_axis])) + 1e-9
        threshold = -0.04 * span

        return view_sign * fc[view_axis] >= threshold

    # =====================================================
    # Projection drawing
    # =====================================================

    def apply_projection_mapping(self, coords, mapping):
        coords = np.asarray(coords, dtype=float)

        mapped = np.empty_like(coords)
        mapped[:, 0] = (
            mapping["x0"]
            + 0.5 * mapping["w"]
            + (coords[:, 0] - mapping["cc"][0])
            * mapping["scale_px"]
            / mapping["fig_w_px"]
        )

        mapped[:, 1] = (
            mapping["y0"]
            + 0.5 * mapping["h"]
            + (coords[:, 1] - mapping["cc"][1])
            * mapping["scale_px"]
            / mapping["fig_h_px"]
        )

        return mapped

    def map_projection_to_box(self, coords, box, return_mapping=False):
        coords = np.asarray(coords, dtype=float)

        x0, y0, w, h = box

        fig_w_px = self.fig.get_size_inches()[0] * self.fig.dpi
        fig_h_px = self.fig.get_size_inches()[1] * self.fig.dpi

        panel_w_px = w * fig_w_px
        panel_h_px = h * fig_h_px

        cmin = coords.min(axis=0)
        cmax = coords.max(axis=0)
        cc = 0.5 * (cmin + cmax)

        rng = np.maximum(cmax - cmin, 1e-9)

        scale_px = 0.88 * min(
            panel_w_px / rng[0],
            panel_h_px / rng[1]
        )

        mapping = {
            "x0": x0,
            "y0": y0,
            "w": w,
            "h": h,
            "fig_w_px": fig_w_px,
            "fig_h_px": fig_h_px,
            "cc": cc,
            "scale_px": scale_px,
        }

        mapped = self.apply_projection_mapping(coords, mapping)

        if return_mapping:
            return mapped, mapping

        return mapped

    def ordered_projected_facet_polygon(self, plane_id, proj_name):
        vis = self.facet_vertices(plane_id)

        if len(vis) < 3:
            return None

        mapped = self.proj_pts[proj_name][vis]

        c = mapped.mean(axis=0)
        ang = np.arctan2(mapped[:, 1] - c[1], mapped[:, 0] - c[0])

        order = np.argsort(ang)
        poly = mapped[order]

        clean = []

        for p in poly:
            if not clean:
                clean.append(p)
                continue

            if np.linalg.norm(p - clean[-1]) > 1e-6:
                clean.append(p)

        if len(clean) >= 2 and np.linalg.norm(clean[0] - clean[-1]) < 1e-6:
            clean.pop()

        if len(clean) < 3:
            return None

        return np.array(clean)

    def highlighted_segments_for_projection(self, proj_name):
        """
        Near selected facet: draw only the visible near-side selected-facet
        outline segments.
        """
        mapped = self.proj_pts[proj_name]
        panel_edges = self.panel_edges[proj_name]

        segments = []

        for a, b in panel_edges:
            edge_planes = self.support[a].intersection(self.support[b])

            if edge_planes.intersection(self.highlight_planes):
                segments.append([mapped[a], mapped[b]])

        return segments

    def update_projection_highlights(self):
        for name, collection in self.highlight_collections.items():
            collection.set_segments([])

        for patch in self.ghost_patches.values():
            patch.set_visible(False)

        if self.target_plane_id is None:
            return

        for name, panel in self.panels.items():
            near = self.facet_near_for_view(
                self.target_plane_id,
                panel
            )

            if near:
                self.highlight_collections[name].set_segments(
                    self.highlighted_segments_for_projection(name)
                )
                self.ghost_patches[name].set_visible(False)

            else:
                # No far-side ghost on TOP/BOTTOM.
                # Those diagrams should stay clean like paper faceting diagrams.
                if panel["mode"] in ("top", "bottom"):
                    self.ghost_patches[name].set_visible(False)
                    self.highlight_collections[name].set_segments([])
                    continue

                # FRONT/SIDE only:
                # Draw selected far facet as a dim green ghost polygon.
                poly = self.ordered_projected_facet_polygon(
                    self.target_plane_id,
                    name
                )

                if poly is None:
                    self.ghost_patches[name].set_visible(False)
                    continue

                self.ghost_patches[name].set_xy(poly)
                self.ghost_patches[name].set_visible(True)
                self.highlight_collections[name].set_segments([])

    # =====================================================
    # Public UI state
    # =====================================================

    def set_no_tiers(self, enabled):
        self.no_tiers_text.set_text("no tiers" if enabled else "")

    def set_facet_hud(
        self,
        facet_number,
        facet_count,
        facet_prev_value=0.0,
        facet_next_value=0.0,
        tier_number=1,
        tier_count=1,
        tier_prev_value=0.0,
        tier_next_value=0.0
    ):
        self.facet_number = facet_number
        self.facet_count = max(1, facet_count)
        self.facet_prev_value = facet_prev_value
        self.facet_next_value = facet_next_value

        self.tier_number = tier_number
        self.tier_count = max(1, tier_count)
        self.tier_prev_value = tier_prev_value
        self.tier_next_value = tier_next_value

    def set_machine_values(
        self,
        rpm_set=None,
        rpm_actual=None,
        flow_set=None,
        flow_actual=None,
        force=None
    ):
        if rpm_set is not None:
            self.rpm_set = rpm_set
        if rpm_actual is not None:
            self.rpm_actual = rpm_actual
        if flow_set is not None:
            self.flow_set = flow_set
        if flow_actual is not None:
            self.flow_actual = flow_actual
        if force is not None:
            self.force_value = force

    def set_status_symbols(self, symbols):
        self.status_symbols = symbols
        self.status_text.set_text(f"{self.status_symbols} W{self.index_res}")

    def set_gem_name(self, gem_name):
        self.gem_name = gem_name
        self.title_text.set_text(wrap_title_for_left_side(gem_name))

    # =====================================================
    # Target orientation math
    # =====================================================

    def highlighted_facet_up_vector(self, plane_id):
        if (
            self.highlighted_facet_always_up
            and self.facet_meta is not None
            and 0 <= plane_id < len(self.facet_meta)
        ):
            tilt = self.facet_meta[plane_id]["tilt"]

            if tilt < 0:
                return np.array([0.0, 0.0, -1.0], dtype=float)

            return np.array([0.0, 0.0, 1.0], dtype=float)

        return np.array([0.0, 0.0, 1.0], dtype=float)

    def target_rotation_for_plane(self, plane_id, planes):
        n, _ = planes[plane_id]
        z_axis = np.array(n, dtype=float)

        zn = np.linalg.norm(z_axis)
        if zn < 1e-12:
            return self.gem_R.copy()

        z_axis /= zn

        up_ref = self.highlighted_facet_up_vector(plane_id)

        y_axis = up_ref - np.dot(up_ref, z_axis) * z_axis
        yn = np.linalg.norm(y_axis)

        if yn < 1e-12:
            fallback_up = np.array([0.0, 1.0, 0.0])
            y_axis = fallback_up - np.dot(fallback_up, z_axis) * z_axis
            yn = np.linalg.norm(y_axis)

        if yn < 1e-12:
            fallback_up = np.array([1.0, 0.0, 0.0])
            y_axis = fallback_up - np.dot(fallback_up, z_axis) * z_axis
            yn = np.linalg.norm(y_axis)

        if yn < 1e-12:
            return self.gem_R.copy()

        y_axis /= yn

        x_axis = np.cross(y_axis, z_axis)
        xn = np.linalg.norm(x_axis)

        if xn < 1e-12:
            return self.gem_R.copy()

        x_axis /= xn

        return np.stack([x_axis, y_axis, z_axis])

    def target_values_for_plane(self, plane_id, planes):
        R = self.target_rotation_for_plane(plane_id, planes)

        theta, phi = tip_twist_from_R(
            R,
            index_res=self.index_res,
            index_sign=self.index_sign
        )

        theta = abs(theta)
        phi = positive_ticks(phi, self.index_res)

        return theta, phi

    # =====================================================
    # Compatibility controls
    # =====================================================

    def animate_to_plane(self, plane_id, planes, steps=8):
        self.target_plane_id = plane_id
        self.target_R = self.target_rotation_for_plane(plane_id, planes)

        self.gem_R = self.target_R.copy()

        self.animating = False
        self.anim_step = 0
        self.anim_steps = 1

    def finish_animation(self):
        self.animating = False

    def step_animation(self):
        return False

    def twist_dop(self, ticks):
        Rz = dop_twist_matrix(
            ticks=ticks,
            index_res=self.index_res,
            index_sign=self.index_sign
        )

        self.gem_R = self.gem_R @ Rz

    def tip_girdle_axis(self, angle):
        Rx_page = page_tip_matrix(angle)
        self.gem_R = Rx_page @ self.gem_R

    # =====================================================
    # HUD
    # =====================================================

    def nav_line(self, left_value, number, count, right_value, suffix=""):
        return (
            f"{left_value:.2f}{suffix}  "
            f"< {number}/{count} >  "
            f"{right_value:.2f}{suffix}"
        )

    def current_z_position(self):
        if self.target_plane_id is None:
            return 0.0

        return self.facet_center(self.target_plane_id)[2]

    def update_force_bar(self):
        v = np.clip(float(self.force_value), 0.0, 1.0)
        self.force_bar_fill.set_width(0.91 * v)

    def update_hud(self):
        if self.target_R is None:
            self.theta_text.set_text("")
            self.theta_nav_text.set_text("")
            self.index_text.set_text("")
            self.index_nav_text.set_text("")
            self.z_text.set_text("")
        else:
            target_theta, target_phi = tip_twist_from_R(
                self.target_R,
                index_res=self.index_res,
                index_sign=self.index_sign
            )

            current_theta, current_phi = tip_twist_from_R(
                self.gem_R,
                index_res=self.index_res,
                index_sign=self.index_sign
            )

            theta_value = abs(target_theta)
            theta_delta = abs(target_theta - current_theta)

            phi_delta = wrap_ticks_signed(
                target_phi - current_phi,
                index_res=self.index_res
            )

            target_phi_positive = positive_ticks(target_phi, self.index_res)

            self.theta_text.set_text(
                f"θ {theta_value:.2f}° +{theta_delta:.2f}"
            )

            self.theta_nav_text.set_text(
                self.nav_line(
                    self.tier_prev_value,
                    self.tier_number,
                    self.tier_count,
                    self.tier_next_value,
                    suffix="°"
                )
            )

            self.index_text.set_text(
                f"φ {target_phi_positive:.2f} {phi_delta:+.2f}"
            )

            self.index_nav_text.set_text(
                self.nav_line(
                    self.facet_prev_value,
                    self.facet_number,
                    self.facet_count,
                    self.facet_next_value,
                    suffix=""
                )
            )

            self.z_text.set_text(
                f"Z {self.current_z_position():+.3f}"
            )

        self.update_force_bar()

        self.rpm_text.set_text(
            f"↻ {self.rpm_set:.0f}/{self.rpm_actual:.0f}"
        )

        self.flow_text.set_text(
            f"💧 {self.flow_set:.2f}/{self.flow_actual:.2f}"
        )

    def update(self):
        self.update_projection_highlights()
        self.update_hud()