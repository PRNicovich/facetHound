# -*- coding: utf-8 -*-

import numpy as np
from matplotlib.patches import Rectangle

from gem_rotation import (
    mat_to_quat,
    quat_to_mat,
    quat_slerp,
    smoothstep,
    wrap_ticks_signed,
    positive_ticks,
    dop_twist_matrix,
    page_tip_matrix,
    tip_twist_from_R,
)


MCU_FONT = "DejaVu Sans Mono"


def wrap_title_for_left_side(title, max_chars=16, max_lines=2):
    """
    Keep gem title inside the left half of a 320 px wide display.
    """
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
    def __init__(
        self,
        ax,
        pts,
        edges,
        support,
        index_res=96,
        index_sign=1,
        facet_meta=None,
        highlighted_facet_always_up=True,
        rpm_set=0.0,
        rpm_actual=0.0,
        flow_set=0.0,
        flow_actual=0.0,
        force_value=0.35,
        gem_name="BOLD",
        meridian=0,
        status_symbols="● ◆ ▲",
        highlight_planes=None
    ):
        self.ax = ax
        self.fig = ax.figure

        self.meridian = meridian

        self.pts = pts
        self.edges = edges
        self.support = support
        self.index_res = index_res
        self.index_sign = index_sign
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
        
        # Facet orientation still snaps to the selected facet,
        # but translation mostly centers the whole gem mass.
        #
        # 0.00 = always center whole gem
        # 1.00 = old behavior, center selected facet exactly
        self.facet_centering_blend = 0.22

        self.lines = []
        for _ in edges:
            line, = ax.plot([], [], "-", lw=0.8)
            self.lines.append(line)

        ax.set_aspect("equal")
        ax.axis("off")

        base_radius = np.max(np.linalg.norm(self.pts - self.center, axis=1))
        self.display_radius = max(base_radius * 1.82, 1e-6)

        fig_w, fig_h = ax.figure.get_size_inches()
        self.display_aspect = fig_w / fig_h

        self.ax.set_xlim(
            -self.display_radius * self.display_aspect,
            self.display_radius * self.display_aspect
        )
        self.ax.set_ylim(
            -self.display_radius,
            self.display_radius
        )

        self.screen_offset = np.array([
            0.0,
            self.display_radius * 0.24,
            0.0
        ], dtype=float)

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
        # Lower HUD
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
            0.50, 0.235, "",
            transform=ax.transAxes,
            ha="center",
            va="bottom",
            fontsize=11,
            color="#ffd84d",
            zorder=100,
            family=MCU_FONT
        )

        self.force_bar_bg = Rectangle(
            (0.045, 0.206),
            0.91,
            0.010,
            transform=ax.transAxes,
            facecolor="#20242e",
            edgecolor="#554900",
            linewidth=0.7,
            alpha=0.90,
            zorder=101
        )
        ax.add_patch(self.force_bar_bg)

        self.force_bar_fill = Rectangle(
            (0.045, 0.206),
            0.01,
            0.010,
            transform=ax.transAxes,
            facecolor="#ffd84d",
            edgecolor="none",
            alpha=0.95,
            zorder=102
        )
        ax.add_patch(self.force_bar_fill)

        self.index_text = ax.text(
            0.50, 0.128, "",
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
            0.50, 0.088, "",
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
            fontsize=12,
            color="#d8d8d8",
            zorder=100,
            family=MCU_FONT
        )

        self.flow_text = ax.text(
            0.975, 0.032, "",
            transform=ax.transAxes,
            ha="right",
            va="bottom",
            fontsize=12,
            color="#d8d8d8",
            zorder=100,
            family=MCU_FONT
        )

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

    def facet_center(self, plane_id):
        face_vertices = [
            vi for vi, s in enumerate(self.support)
            if plane_id in s
        ]

        if face_vertices:
            return self.pts[face_vertices].mean(axis=0)

        return self.center.copy()

    def display_shift_for_facet(self, plane_id, R):
        """
        Compute display-space pan for the selected facet.

        Rotation still targets the selected facet.

        Translation is blended:
            mostly whole-gem visual mass center
            slightly selected-facet center

        This keeps the gem from wandering around too much while still giving
        a useful nudge toward the active facet.
        """
        # Selected facet center in display space.
        fc = self.facet_center(plane_id)
        facet_shift = R @ (fc - self.center)

        # Whole gem visual mass/bounding center in display space.
        projected = (R @ (self.pts - self.center).T).T

        pmin = projected.min(axis=0)
        pmax = projected.max(axis=0)

        gem_mass_shift = 0.5 * (pmin + pmax)

        a = np.clip(float(self.facet_centering_blend), 0.0, 1.0)

        return (
            (1.0 - a) * gem_mass_shift +
            a * facet_shift
        )

    def animate_to_plane(self, plane_id, planes, steps=8):
        target_R = self.target_rotation_for_plane(plane_id, planes)
        target_shift = self.display_shift_for_facet(plane_id, target_R)

        self.target_plane_id = plane_id
        self.target_R = target_R.copy()

        self.animating = True
        self.anim_step = 0
        self.anim_steps = max(1, int(steps))

        self.anim_q0 = mat_to_quat(self.gem_R)
        self.anim_q1 = mat_to_quat(target_R)

        self.anim_shift0 = self.display_shift.copy()
        self.anim_shift1 = target_shift.copy()

    def finish_animation(self):
        if not self.animating:
            return

        self.gem_R = quat_to_mat(self.anim_q1)
        self.display_shift = self.anim_shift1.copy()

        self.animating = False
        self.anim_step = self.anim_steps

    def step_animation(self):
        if not self.animating:
            return False

        self.anim_step += 1

        raw_t = self.anim_step / max(self.anim_steps, 1)
        t = smoothstep(raw_t)

        q = quat_slerp(self.anim_q0, self.anim_q1, t)
        self.gem_R = quat_to_mat(q)

        self.display_shift = (
            (1.0 - t) * self.anim_shift0 +
            t * self.anim_shift1
        )

        if self.anim_step >= self.anim_steps:
            self.gem_R = quat_to_mat(self.anim_q1)
            self.display_shift = self.anim_shift1.copy()
            self.animating = False

        return True

    def twist_dop(self, ticks):
        self.animating = False

        Rz = dop_twist_matrix(
            ticks=ticks,
            index_res=self.index_res,
            index_sign=self.index_sign
        )

        self.gem_R = self.gem_R @ Rz

    def tip_girdle_axis(self, angle):
        self.animating = False

        Rx_page = page_tip_matrix(angle)
        self.gem_R = Rx_page @ self.gem_R

    def nav_line(self, left_value, number, count, right_value, suffix=""):
        return (
            f"{left_value:.2f}{suffix}  "
            f"< {number}/{count} >  "
            f"{right_value:.2f}{suffix}"
        )

    def current_z_position(self):
        p = self.girdle_center_display()
        return p[2]

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

            # Do not wrap tip error. Direction is visually obvious.
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

    def girdle_center_display(self):
        return (
            self.gem_R @ (self.girdle_center - self.center)
            - self.display_shift
            + self.screen_offset
        )

    def transformed_vertices(self):
        return (
            (self.gem_R @ (self.pts - self.center).T).T
            - self.display_shift
            + self.screen_offset
        )

    def update(self):
        v = self.transformed_vertices()

        self.update_hud()

        edge_depths = []

        for a, b in self.edges:
            pa = v[a]
            pb = v[b]
            edge_depths.append(0.5 * (pa[2] + pb[2]))

        if edge_depths:
            zmin = min(edge_depths)
            zmax = max(edge_depths)
            zrng = zmax - zmin + 1e-9
        else:
            zmin = 0.0
            zrng = 1.0

        for idx, (a, b) in enumerate(self.edges):
            pa = v[a]
            pb = v[b]

            z = 0.5 * (pa[2] + pb[2])
            depth = (z - zmin) / zrng

            edge_planes = self.support[a].intersection(self.support[b])
            is_highlight = bool(edge_planes.intersection(self.highlight_planes))

            alpha = 0.28 + 0.72 * depth
            lw = 0.35 + 1.10 * depth

            if is_highlight:
                r = 0.15 * (1.0 - depth)
                g = 0.85 + 0.15 * depth
                bcol = 0.20 * (1.0 - depth)

                alpha = max(alpha, 0.78)
                lw += 0.95
                zorder = 10.0 + depth
            else:
                shade = 0.22 + 0.72 * depth
                r = shade
                g = shade
                bcol = shade
                zorder = depth

            self.lines[idx].set_data([pa[0], pb[0]], [pa[1], pb[1]])
            self.lines[idx].set_color((r, g, bcol, alpha))
            self.lines[idx].set_linewidth(lw)
            self.lines[idx].set_zorder(zorder)