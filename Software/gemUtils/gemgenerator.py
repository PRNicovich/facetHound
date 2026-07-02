# -*- coding: utf-8 -*-
"""
Created on Wed Jul  1 18:07:01 2026

@author: rusty
"""

# -*- coding: utf-8 -*-

import numpy as np
import pathlib
import matplotlib
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation


# =========================================================
# PARSER
# =========================================================

def parse_gemcad(path):
    """
    Parses a GemCAD .asc file.

    Returns:
        facets: list of (tilt_deg, distance, [index,...])
        index_res: number of index positions per full circle (abs value)
        index_sign: +1 or -1, indexing direction (from the 'g' line)
        meridian: reference-offset in index units (from the 'g' line)
    """
    facets = []
    index_res = 96
    index_sign = 1
    meridian = 0.0

    with open(path, "r") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            tag = line[0]
            p = line.split()

            if tag == "g":
                # g <signed_index_count> <meridian>
                g_raw = int(p[1])
                index_sign = -1 if g_raw < 0 else 1
                index_res = abs(g_raw)
                meridian = float(p[2]) if len(p) > 2 else 0.0
                continue

            if tag != "a":
                continue

            tilt = float(p[1])
            d = float(p[2])

            if "n" in p:
                ni = p.index("n")
                idxs = p[ni + 1:]
            else:
                idxs = p[3:]

            idx = []
            for x in idxs:
                try:
                    idx.append(int(x))
                except ValueError:
                    pass

            facets.append((tilt, d, idx))

    return facets, index_res, index_sign, meridian


# =========================================================
# GEOMETRY
# =========================================================

def idx_to_az(i, index_res, index_sign=1, meridian=0.0):
    """
    Convert a GemCAD facet index into an azimuth angle in radians.
    """
    offset = meridian % index_res
    return index_sign * 2.0 * np.pi * ((i - offset) % index_res) / index_res


def plane(tilt_deg, az, d):
    """
    tilt_deg is measured FROM THE GIRDLE PLANE.

    0 deg = flat/parallel to girdle
    90 deg = vertical
    negative tilt = pavilion
    positive tilt = crown
    """
    t = np.deg2rad(tilt_deg)

    n = np.array([
        np.sin(t) * np.cos(az),
        np.sin(t) * np.sin(az),
        np.cos(t)
    ], dtype=float)

    if tilt_deg < 0:
        n = -n

    norm = np.linalg.norm(n)
    if norm > 1e-12:
        n /= norm

    return n, d


def validate_planes(planes, tol=1e-9):
    """
    Sanity check only.
    """
    for i, (n, d) in enumerate(planes):
        if d < -tol:
            print(f"WARNING: plane {i} has negative distance d={d:.6f} "
                  f"(n={n}) -- check tilt/azimuth/index parsing.")
    return planes


# =========================================================
# SOLVER
# =========================================================

def intersect3(n1, d1, n2, d2, n3, d3):
    A = np.stack([n1, n2, n3])
    b = np.array([d1, d2, d3])

    try:
        return np.linalg.solve(A, b)
    except np.linalg.LinAlgError:
        return None


def inside(p, planes):
    for n, d in planes:
        if np.dot(n, p) > d + 1e-9:
            return False
    return True


def build_vertices(planes):
    pts = []
    support = []

    N = len(planes)
    total = N * (N - 1) * (N - 2) // 6
    c = 0

    for i in range(N):
        n1, d1 = planes[i]
        for j in range(i + 1, N):
            n2, d2 = planes[j]
            for k in range(j + 1, N):
                n3, d3 = planes[k]

                p = intersect3(n1, d1, n2, d2, n3, d3)

                c += 1
                if total > 0 and c % 3000 == 0:
                    print(f"\rbuilding vertices: {100*c/total:.2f}%", end="")

                if p is None:
                    continue

                if inside(p, planes):
                    pts.append(p)
                    support.append({i, j, k})

    print("\rbuilding vertices: done            ")

    uniq = []
    uniq_sup = []
    seen = {}

    for p, s in zip(pts, support):
        key = tuple(np.round(p, 7))

        if key not in seen:
            seen[key] = len(uniq)
            uniq.append(p)
            uniq_sup.append(set(s))
        else:
            uniq_sup[seen[key]] |= s

    return np.array(uniq), uniq_sup


# =========================================================
# EDGES
# =========================================================

def build_edges(pts, support, planes, tol=1e-9):
    """
    Build only real wireframe edges.

    Vertices that share 2 supporting planes lie along a candidate edge line.
    If more than 2 vertices lie on the same line, connect only adjacent ones.
    """
    pair_to_vertices = {}

    for vi, s in enumerate(support):
        ss = sorted(s)

        for a in range(len(ss)):
            for b in range(a + 1, len(ss)):
                key = (ss[a], ss[b])
                pair_to_vertices.setdefault(key, []).append(vi)

    edge_set = set()

    for (pa, pb), vis in pair_to_vertices.items():
        vis = sorted(set(vis))

        if len(vis) < 2:
            continue

        n1, _ = planes[pa]
        n2, _ = planes[pb]

        direction = np.cross(n1, n2)
        norm = np.linalg.norm(direction)

        if norm < tol:
            continue

        direction /= norm

        ordered = sorted(
            vis,
            key=lambda v: float(np.dot(pts[v], direction))
        )

        for a, b in zip(ordered[:-1], ordered[1:]):
            if np.linalg.norm(pts[a] - pts[b]) > tol:
                edge_set.add(tuple(sorted((a, b))))

    return sorted(edge_set)


# =========================================================
# FACET SELECTION
# =========================================================

def find_facet_by_tier_twist(facet_meta, tier, twist, tier_base=1, twist_base=1):
    """
    Select by tier and ordinal facet position within that tier.

    tier_base=1 means the first 'a' line is tier 1.
    twist_base=1 means the first facet in that tier is facet 1.
    """
    target_tier = tier - tier_base
    target_twist = twist - twist_base

    for plane_id, meta in enumerate(facet_meta):
        if meta["tier"] == target_tier and meta["twist"] == target_twist:
            return plane_id

    raise ValueError(f"No facet found for tier={tier}, facet={twist}")


def build_tier_nav(facet_meta):
    """
    Build tier -> plane list lookup for keyboard navigation.
    """
    tier_to_planes = {}

    for plane_id, meta in enumerate(facet_meta):
        tier_to_planes.setdefault(meta["tier"], []).append(plane_id)

    for tier in tier_to_planes:
        tier_to_planes[tier].sort(key=lambda pid: facet_meta[pid]["twist"])

    available_tiers = sorted(tier_to_planes)

    return available_tiers, tier_to_planes


# =========================================================
# ROTATION / QUATERNION HELPERS
# =========================================================

def mat_to_quat(M):
    """
    Convert a 3x3 rotation matrix to a quaternion [w, x, y, z].
    """
    M = np.asarray(M, dtype=float)
    tr = M[0, 0] + M[1, 1] + M[2, 2]

    if tr > 0.0:
        s = np.sqrt(tr + 1.0) * 2.0
        w = 0.25 * s
        x = (M[2, 1] - M[1, 2]) / s
        y = (M[0, 2] - M[2, 0]) / s
        z = (M[1, 0] - M[0, 1]) / s

    elif M[0, 0] > M[1, 1] and M[0, 0] > M[2, 2]:
        s = np.sqrt(1.0 + M[0, 0] - M[1, 1] - M[2, 2]) * 2.0
        w = (M[2, 1] - M[1, 2]) / s
        x = 0.25 * s
        y = (M[0, 1] + M[1, 0]) / s
        z = (M[0, 2] + M[2, 0]) / s

    elif M[1, 1] > M[2, 2]:
        s = np.sqrt(1.0 + M[1, 1] - M[0, 0] - M[2, 2]) * 2.0
        w = (M[0, 2] - M[2, 0]) / s
        x = (M[0, 1] + M[1, 0]) / s
        y = 0.25 * s
        z = (M[1, 2] + M[2, 1]) / s

    else:
        s = np.sqrt(1.0 + M[2, 2] - M[0, 0] - M[1, 1]) * 2.0
        w = (M[1, 0] - M[0, 1]) / s
        x = (M[0, 2] + M[2, 0]) / s
        y = (M[1, 2] + M[2, 1]) / s
        z = 0.25 * s

    q = np.array([w, x, y, z], dtype=float)
    qn = np.linalg.norm(q)

    if qn > 1e-12:
        q /= qn

    return q


def quat_to_mat(q):
    """
    Convert quaternion [w, x, y, z] to a 3x3 rotation matrix.
    """
    q = np.asarray(q, dtype=float)
    qn = np.linalg.norm(q)

    if qn < 1e-12:
        return np.eye(3)

    q = q / qn
    w, x, y, z = q

    return np.array([
        [1.0 - 2.0 * (y*y + z*z),       2.0 * (x*y - z*w),       2.0 * (x*z + y*w)],
        [      2.0 * (x*y + z*w), 1.0 - 2.0 * (x*x + z*z),       2.0 * (y*z - x*w)],
        [      2.0 * (x*z - y*w),       2.0 * (y*z + x*w), 1.0 - 2.0 * (x*x + y*y)]
    ], dtype=float)


def quat_slerp(q0, q1, t):
    """
    Spherical interpolation between two quaternions.
    """
    q0 = np.asarray(q0, dtype=float)
    q1 = np.asarray(q1, dtype=float)

    q0 /= max(np.linalg.norm(q0), 1e-12)
    q1 /= max(np.linalg.norm(q1), 1e-12)

    dot = float(np.dot(q0, q1))

    if dot < 0.0:
        q1 = -q1
        dot = -dot

    dot = np.clip(dot, -1.0, 1.0)

    if dot > 0.9995:
        q = q0 + t * (q1 - q0)
        q /= max(np.linalg.norm(q), 1e-12)
        return q

    theta_0 = np.arccos(dot)
    theta = theta_0 * t

    s0 = np.cos(theta) - dot * np.sin(theta) / np.sin(theta_0)
    s1 = np.sin(theta) / np.sin(theta_0)

    return s0 * q0 + s1 * q1


def smoothstep(t):
    """
    Small ease-in/ease-out curve.
    """
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def wrap_degrees(a):
    """
    Wrap angle to [-180, 180).
    """
    return (a + 180.0) % 360.0 - 180.0


def wrap_ticks(ticks, index_res):
    """
    Wrap index ticks to [-index_res/2, index_res/2).
    """
    half = index_res / 2.0
    return (ticks + half) % index_res - half


# =========================================================
# VIEWER
# =========================================================

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
        highlight_planes=None
    ):
        self.ax = ax
        self.pts = pts
        self.edges = edges
        self.support = support
        self.index_res = index_res
        self.index_sign = index_sign
        self.facet_meta = facet_meta
        self.highlighted_facet_always_up = highlighted_facet_always_up
        self.highlight_planes = set(highlight_planes or [])

        # GemCAD origin:
        # x=0, y=0, z=0 is the middle of the girdle plane.
        # This is the physical gem origin used for DOP and tip-axis behavior.
        self.girdle_center = np.array([0.0, 0.0, 0.0], dtype=float)
        self.center = self.girdle_center.copy()

        # Display transform:
        #   v = gem_R @ (pts - center) - display_shift
        #
        # gem_R rotates the gem around the girdle center / GemCAD origin.
        # display_shift is only a display-space pan used to center a selected facet.
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

        self.info_text = ax.text(
            0.03, 0.98, "",
            transform=ax.transAxes,
            ha="left",
            va="top",
            fontsize=8,
            zorder=100,
            bbox=dict(
                facecolor="white",
                alpha=0.68,
                edgecolor="none",
                pad=2.0
            )
        )

        self.tip_hud_text = ax.text(
            0.03, 0.02, "",
            transform=ax.transAxes,
            ha="left",
            va="bottom",
            fontsize=8,
            zorder=100,
            bbox=dict(
                facecolor="white",
                alpha=0.68,
                edgecolor="none",
                pad=2.0
            )
        )

        self.twist_hud_text = ax.text(
            0.97, 0.02, "",
            transform=ax.transAxes,
            ha="right",
            va="bottom",
            fontsize=8,
            zorder=100,
            bbox=dict(
                facecolor="white",
                alpha=0.68,
                edgecolor="none",
                pad=2.0
            )
        )

        self.lines = []
        for _ in edges:
            line, = ax.plot([], [], 'k-', lw=0.8)
            self.lines.append(line)

        ax.set_aspect("equal")
        ax.axis("off")

        # Fixed display scale. Do not update this during rotation or animation.
        # Smaller factor = bigger gem in the portrait window.
        base_radius = np.max(np.linalg.norm(self.pts - self.center, axis=1))
        self.display_radius = max(base_radius * 1.45, 1e-6)

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

        # Draw the actual tip axis:
        # page-horizontal, passing through the displayed GemCAD origin/girdle center.
        self.show_tip_axis = True

        if self.show_tip_axis:
            self.tip_axis_line, = ax.plot(
                [
                    -self.display_radius * self.display_aspect,
                     self.display_radius * self.display_aspect
                ],
                [0.0, 0.0],
                color="blue",
                lw=0.8,
                alpha=0.35,
                zorder=200
            )
        else:
            self.tip_axis_line = None

    def highlighted_facet_up_vector(self, plane_id):
        """
        Screen-up reference used when snapping to a highlighted facet.

        If highlighted_facet_always_up is True:
            crown/table facets use +Z as screen-up
            pavilion facets use -Z as screen-up

        This means:
            crown selected    -> table side points up
            pavilion selected -> culet side points up
        """
        if (
            self.highlighted_facet_always_up
            and self.facet_meta is not None
            and 0 <= plane_id < len(self.facet_meta)
        ):
            tilt = self.facet_meta[plane_id]["tilt"]

            if tilt < 0:
                return np.array([0.0, 0.0, -1.0], dtype=float)

            return np.array([0.0, 0.0, 1.0], dtype=float)

        # Old behavior: always keep table/crown direction up.
        return np.array([0.0, 0.0, 1.0], dtype=float)

    def angle_deg_to_index_ticks(self, deg):
        """
        Convert an angular twist value to GemCAD index ticks.
        """
        return self.index_sign * deg * self.index_res / 360.0

    def tip_twist_from_R(self, R):
        """
        Approximate current control-space angles from display rotation.

        tip:
            page-horizontal X-axis tip angle, in degrees

        twist:
            DOP/local-Z twist angle, in index ticks
        """
        dop = R @ np.array([0.0, 0.0, 1.0], dtype=float)
        dn = np.linalg.norm(dop)

        if dn > 1e-12:
            dop /= dn

        tip_deg = np.rad2deg(np.arctan2(-dop[1], dop[2]))

        tip = np.deg2rad(tip_deg)
        c, s = np.cos(-tip), np.sin(-tip)
        Rx_un_tip = np.array([
            [1, 0,  0],
            [0, c, -s],
            [0, s,  c]
        ], dtype=float)

        R_flat = Rx_un_tip @ R
        twist_deg = np.rad2deg(np.arctan2(R_flat[1, 0], R_flat[0, 0]))
        twist_ticks = self.angle_deg_to_index_ticks(twist_deg)

        return tip_deg, twist_ticks

    def update_hud(self):
        """
        Update bottom HUD with target/current angular error.
        """
        if self.target_R is None:
            self.tip_hud_text.set_text("")
            self.twist_hud_text.set_text("")
            return

        target_tip, target_twist_ticks = self.tip_twist_from_R(self.target_R)
        current_tip, current_twist_ticks = self.tip_twist_from_R(self.gem_R)

        tip_err = wrap_degrees(target_tip - current_tip)
        twist_err_ticks = wrap_ticks(
            target_twist_ticks - current_twist_ticks,
            self.index_res
        )

        self.tip_hud_text.set_text(
            f"TIP\n"
            f"target {target_tip:+6.1f} deg\n"
            f"error  {tip_err:+6.1f} deg"
        )

        self.twist_hud_text.set_text(
            f"TWIST\n"
            f"target {target_twist_ticks:+6.2f} idx\n"
            f"error  {twist_err_ticks:+6.2f} idx"
        )

    def girdle_center_display(self):
        """
        Display-space location of the GemCAD origin/girdle center.
        """
        return (
            self.gem_R @ (self.girdle_center - self.center)
            - self.display_shift
        )

    def update_tip_axis_line(self):
        """
        Keep the visible tip axis page-horizontal and passing through
        the current displayed girdle center.
        """
        if self.tip_axis_line is None:
            return

        p = self.girdle_center_display()

        self.tip_axis_line.set_data(
            [
                -self.display_radius * self.display_aspect,
                 self.display_radius * self.display_aspect
            ],
            [p[1], p[1]]
        )

    def target_rotation_for_plane(self, plane_id, planes):
        """
        Build the gem rotation that puts the selected facet normal into screen Z.

        The screen-up direction can optionally flip by crown/pavilion side:
            crown/table -> +Z up
            pavilion    -> -Z up
        """
        n, _ = planes[plane_id]
        z_axis = np.array(n, dtype=float)

        zn = np.linalg.norm(z_axis)
        if zn < 1e-12:
            return self.gem_R.copy()

        z_axis /= zn

        up_ref = self.highlighted_facet_up_vector(plane_id)

        # Project chosen up reference into the selected facet's screen plane.
        y_axis = up_ref - np.dot(up_ref, z_axis) * z_axis

        yn = np.linalg.norm(y_axis)

        # If selected normal is almost aligned with the up reference,
        # screen-up is ambiguous. Use world Y as a stable fallback.
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

        # Rows are display axes. This rotates gem/world coordinates into display space.
        return np.stack([x_axis, y_axis, z_axis])

    def facet_center(self, plane_id):
        """
        Average all vertices supported by this plane.
        """
        face_vertices = [
            vi for vi, s in enumerate(self.support)
            if plane_id in s
        ]

        if face_vertices:
            return self.pts[face_vertices].mean(axis=0)

        return self.center.copy()

    def display_shift_for_facet(self, plane_id, R):
        """
        Compute display-space pan that places the selected facet center at screen center.

        This is NOT a rotation pivot. Rotation still happens around the GemCAD origin.
        """
        fc = self.facet_center(plane_id)
        return R @ (fc - self.center)

    def animate_to_plane(self, plane_id, planes, steps=8):
        """
        Start an eased gem rotation and display-pan transition toward a highlighted facet.
        """
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
        """
        Immediately complete any active transition.
        """
        if not self.animating:
            return

        self.gem_R = quat_to_mat(self.anim_q1)
        self.display_shift = self.anim_shift1.copy()

        self.animating = False
        self.anim_step = self.anim_steps

    def step_animation(self):
        """
        Advance one display-frame of the active transition.
        """
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
        """
        Twist around the gem's own DOP axis.

        Input is in GemCAD index ticks.
        This is fixed to the gem and goes through the GemCAD origin.
        """
        self.animating = False

        angle = self.index_sign * 2.0 * np.pi * ticks / self.index_res
        c, s = np.cos(angle), np.sin(angle)

        Rz_dop = np.array([
            [c, -s, 0],
            [s,  c, 0],
            [0,  0, 1]
        ], dtype=float)

        self.gem_R = self.gem_R @ Rz_dop

    def tip_girdle_axis(self, angle):
        """
        Tip around a PAGE-HORIZONTAL axis passing through the displayed
        GemCAD origin / middle of girdle / z=0 point.

        Axis:
            left-right on the page
            through gem-space point (0, 0, 0)
            independent of selected facet
            not above the gem
        """
        self.animating = False

        c, s = np.cos(angle), np.sin(angle)

        # Page-space rotation around the page X axis.
        # This is left-right on the display.
        Rx_page = np.array([
            [1, 0,  0],
            [0, c, -s],
            [0, s,  c]
        ], dtype=float)

        # The hinge point is the currently displayed GemCAD origin.
        # This makes the axis pass through the middle of the girdle.
        P = self.girdle_center_display()

        # Current projected form:
        #   v = gem_R @ x - display_shift
        #
        # Apply page hinge:
        #   v_new = Rx_page @ (v - P) + P
        #
        # Rewritten into same form:
        #   gem_R_new = Rx_page @ gem_R
        #   shift_new = Rx_page @ shift + (Rx_page - I) @ P
        self.gem_R = Rx_page @ self.gem_R
        self.display_shift = (
            Rx_page @ self.display_shift +
            (Rx_page - np.eye(3)) @ P
        )

    def update(self):
        R = self.gem_R

        # Rotate around the GemCAD origin/girdle center, then pan in display space.
        # Do NOT subtract the selected facet center here.
        v = (R @ (self.pts - self.center).T).T - self.display_shift

        self.update_tip_axis_line()
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
            depth = (z - zmin) / zrng   # 0 = far, 1 = near

            edge_planes = self.support[a].intersection(self.support[b])
            is_highlight = bool(edge_planes.intersection(self.highlight_planes))

            alpha = 0.25 + 0.75 * depth
            lw = 0.35 + 1.10 * depth

            if is_highlight:
                # Red highlight, still depth-shaded.
                r = 1.0
                g = 0.15 * (1.0 - depth)
                bcol = 0.15 * (1.0 - depth)

                alpha = max(alpha, 0.55)
                lw += 0.75
                zorder = 10.0 + depth
            else:
                shade = 1.0 - 0.85 * depth
                r = shade
                g = shade
                bcol = shade
                zorder = depth

            self.lines[idx].set_data([pa[0], pb[0]], [pa[1], pb[1]])
            self.lines[idx].set_color((r, g, bcol, alpha))
            self.lines[idx].set_linewidth(lw)
            self.lines[idx].set_zorder(zorder)


# =========================================================
# BUILD
# =========================================================

def build_gem(path):
    facets, index_res, index_sign, meridian = parse_gemcad(path)

    print(f"index resolution: {index_res}, sign: {index_sign:+d}, "
          f"meridian offset: {meridian}")

    planes = []
    facet_meta = []

    for tier_id, (tilt, d, idxs) in enumerate(facets):
        for twist_id, i in enumerate(idxs):
            az = idx_to_az(i, index_res, index_sign, meridian)
            planes.append(plane(tilt, az, d))

            facet_meta.append({
                "tier": tier_id,
                "twist": twist_id,
                "index": i,
                "tilt": tilt,
                "distance": d,
            })

    planes = validate_planes(planes)

    pts, support = build_vertices(planes)
    edges = build_edges(pts, support, planes)

    return pts, support, edges, planes, facet_meta, index_res, index_sign


# =========================================================
# MAIN
# =========================================================

if __name__ == "__main__":

    interactive = True

    path = pathlib.Path("./data/pc01391.asc")

    (
        pts,
        support,
        edges,
        planes,
        facet_meta,
        index_res,
        index_sign
    ) = build_gem(path)

    print("planes:", len(planes))
    print("vertices:", len(pts))
    print("edges:", len(edges))

    # -----------------------------------------------------
    # HIGHLIGHT SETTINGS
    # -----------------------------------------------------

    highlight_enabled = True

    start_tier = 1
    start_facet = 1

    tier_base = 1
    twist_base = 1

    transition_steps = 8

    # If True:
    #   crown/table facets show table side up
    #   pavilion facets show culet side up
    #
    # If False:
    #   old behavior, world +Z/table direction is always up
    highlighted_facet_always_up = True

    available_tiers, tier_to_planes = build_tier_nav(facet_meta)

    nav = {
        "tier_pos": 0,
        "facet_pos": 0,
    }

    highlight_planes = []

    if available_tiers:
        target_tier = start_tier - tier_base

        if target_tier in tier_to_planes:
            nav["tier_pos"] = available_tiers.index(target_tier)

        if highlight_enabled:
            try:
                highlight_plane = find_facet_by_tier_twist(
                    facet_meta,
                    tier=start_tier,
                    twist=start_facet,
                    tier_base=tier_base,
                    twist_base=twist_base
                )

                tier = facet_meta[highlight_plane]["tier"]
                if tier in tier_to_planes:
                    nav["tier_pos"] = available_tiers.index(tier)
                    nav["facet_pos"] = tier_to_planes[tier].index(highlight_plane)

                highlight_planes = [highlight_plane]
                print("highlight plane:", highlight_plane, facet_meta[highlight_plane])

            except ValueError as e:
                print("highlight warning:", e)

    # Portrait 320 x 480 pixel display
    fig, ax = plt.subplots(figsize=(3.2, 4.8), dpi=100)

    view = Viewer(
        ax,
        pts,
        edges,
        support,
        index_res=index_res,
        index_sign=index_sign,
        facet_meta=facet_meta,
        highlighted_facet_always_up=highlighted_facet_always_up,
        highlight_planes=highlight_planes
    )

    def update_highlight_label(animate=True):
        if not available_tiers:
            view.highlight_planes = set()
            view.target_plane_id = None
            view.target_R = None
            view.info_text.set_text("no tiers")
            return

        tier = available_tiers[nav["tier_pos"]]
        planes_here = tier_to_planes[tier]

        nav["facet_pos"] %= len(planes_here)

        plane_id = planes_here[nav["facet_pos"]]
        meta = facet_meta[plane_id]

        if highlight_enabled:
            view.highlight_planes = {plane_id}

            if animate:
                view.animate_to_plane(
                    plane_id,
                    planes,
                    steps=transition_steps
                )
            else:
                view.animate_to_plane(
                    plane_id,
                    planes,
                    steps=1
                )
                view.finish_animation()
        else:
            view.highlight_planes = set()
            view.target_plane_id = None
            view.target_R = None
            view.display_shift[:] = 0.0
            view.animating = False

        tier_display = meta["tier"] + tier_base
        facet_display = meta["twist"] + twist_base

        view.info_text.set_text(
            f"tier {tier_display}\n"
            f"facet {facet_display}/{len(planes_here)}\n"
            f"index {meta['index']}"
        )

    update_highlight_label(animate=True)
    view.update()

    if interactive:
        def on_key(e):
            if e.key == "left":
                view.twist_dop(-1.0)

            elif e.key == "right":
                view.twist_dop(1.0)

            elif e.key == "up":
                view.tip_girdle_axis(0.1)

            elif e.key == "down":
                view.tip_girdle_axis(-0.1)

            elif e.key in ("+", "="):
                if available_tiers:
                    nav["tier_pos"] = (nav["tier_pos"] + 1) % len(available_tiers)
                    nav["facet_pos"] = 0
                    update_highlight_label(animate=True)

            elif e.key in ("-", "_"):
                if available_tiers:
                    nav["tier_pos"] = (nav["tier_pos"] - 1) % len(available_tiers)
                    nav["facet_pos"] = 0
                    update_highlight_label(animate=True)

            elif e.key == "]":
                if available_tiers:
                    tier = available_tiers[nav["tier_pos"]]
                    planes_here = tier_to_planes[tier]
                    nav["facet_pos"] = (nav["facet_pos"] + 1) % len(planes_here)
                    update_highlight_label(animate=True)

            elif e.key == "[":
                if available_tiers:
                    tier = available_tiers[nav["tier_pos"]]
                    planes_here = tier_to_planes[tier]
                    nav["facet_pos"] = (nav["facet_pos"] - 1) % len(planes_here)
                    update_highlight_label(animate=True)

            view.update()
            fig.canvas.draw_idle()

        def tick(_):
            if view.step_animation():
                view.update()
                fig.canvas.draw_idle()

        fig.canvas.mpl_connect("key_press_event", on_key)

        ani = FuncAnimation(
            fig,
            tick,
            interval=30,
            cache_frame_data=False
        )
        fig._gemcad_animation = ani

        plt.show()

    else:
        view.finish_animation()
        view.update()

        out = pathlib.Path("./gem_preview.png")
        fig.savefig(out, dpi=100, bbox_inches="tight")
        print(f"saved preview to {out}")