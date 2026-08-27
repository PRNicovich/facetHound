# -*- coding: utf-8 -*-

import numpy as np
from matplotlib.patches import Rectangle

from gem_rotation import (
    tip_twist_from_R,
    wrap_ticks_signed,
    positive_ticks,
)


TITLE_FONT = "Segoe UI"
VALUE_FONT = "Segoe UI"
LABEL_FONT = "Segoe UI"
TECH_FONT = "Consolas"


BG = "#02050a"
PANEL_BG = "#040912"
PANEL_EDGE = "#132334"

HUD_LINE = "#1f3b52"

THETA = "#ffd84d"
THETA_SOFT = "#fff0a6"

PHI = "#40e8ff"
PHI_SOFT = "#9af5ff"

AMBER = "#ff9f00"


def _style_text(
    txt,
    *,
    x=None,
    y=None,
    ha=None,
    va=None,
    fontsize=None,
    color=None,
    family=None,
    fontweight=None,
    alpha=None
):
    if x is not None or y is not None:
        px, py = txt.get_position()
        txt.set_position((
            px if x is None else x,
            py if y is None else y
        ))

    if ha is not None:
        txt.set_ha(ha)
    if va is not None:
        txt.set_va(va)
    if fontsize is not None:
        txt.set_fontsize(fontsize)
    if color is not None:
        txt.set_color(color)
    if family is not None:
        txt.set_family(family)
    if fontweight is not None:
        txt.set_fontweight(fontweight)
    if alpha is not None:
        txt.set_alpha(alpha)


def _rect(view, attr, xywh, face, edge, lw, alpha, zorder):
    x, y, w, h = xywh

    if not hasattr(view, attr):
        r = Rectangle(
            (x, y),
            w,
            h,
            transform=view.ax.transAxes,
            facecolor=face,
            edgecolor=edge,
            linewidth=lw,
            alpha=alpha,
            zorder=zorder
        )
        view.ax.add_patch(r)
        setattr(view, attr, r)

    r = getattr(view, attr)
    r.set_bounds(x, y, w, h)
    r.set_facecolor(face)
    r.set_edgecolor(edge)
    r.set_linewidth(lw)
    r.set_alpha(alpha)
    r.set_zorder(zorder)

    return r


def _setup_screen_backplates(view):
    view.fig.patch.set_facecolor(BG)
    view.ax.set_facecolor(BG)

    # Invisible rectangles are kept only to overwrite older patched versions
    # if Spyder did not fully restart.
    _rect(
        view,
        "_style_top_bar",
        (0.000, 0.932, 1.000, 0.068),
        BG,
        "none",
        0.0,
        0.0,
        80
    )

    _rect(
        view,
        "_style_hud_back",
        (0.030, 0.072, 0.940, 0.250),
        BG,
        "none",
        0.0,
        0.0,
        82
    )

    _rect(
        view,
        "_style_hud_top_glow",
        (0.055, 0.315, 0.890, 0.0020),
        BG,
        "none",
        0.0,
        0.0,
        83
    )

    # One subtle divider only. No bounding boxes.
    _rect(
        view,
        "_style_pose_separator",
        (0.070, 0.196, 0.860, 0.0020),
        HUD_LINE,
        "none",
        0.0,
        0.58,
        83
    )


def _style_static_panels(view):
    if not hasattr(view, "panels"):
        return

    panel_boxes = [p["box"] for p in view.panels.values()]

    for patch in view.ax.patches:
        if not isinstance(patch, Rectangle):
            continue

        px = patch.get_x()
        py = patch.get_y()
        pw = patch.get_width()
        ph = patch.get_height()

        is_panel_frame = False

        for x, y, w, h in panel_boxes:
            if (
                abs(px - x) < 1e-6
                and abs(py - y) < 1e-6
                and abs(pw - w) < 1e-6
                and abs(ph - h) < 1e-6
            ):
                is_panel_frame = True
                break

        if not is_panel_frame:
            continue

        patch.set_facecolor(PANEL_BG)
        patch.set_edgecolor(PANEL_EDGE)
        patch.set_linewidth(0.65)
        patch.set_alpha(0.98)
        patch.set_zorder(4)

    for txt in view.ax.texts:
        if txt.get_text() in ("TOP", "BOTTOM", "FRONT", "SIDE"):
            _style_text(
                txt,
                fontsize=6.2,
                color="#4f6476",
                family=TECH_FONT,
                fontweight="normal",
                alpha=0.88
            )

        if txt.get_text() == ".":
            _style_text(
                txt,
                fontsize=15,
                color=AMBER,
                family=TECH_FONT,
                fontweight="normal",
                alpha=0.98
            )

    if hasattr(view, "base_collections"):
        for collection in view.base_collections.values():
            collection.set_colors([(0.58, 0.66, 0.74, 0.60)])
            collection.set_linewidths([0.70])
            collection.set_zorder(12)

    if hasattr(view, "highlight_collections"):
        for collection in view.highlight_collections.values():
            collection.set_colors([(0.20, 1.00, 0.42, 0.98)])
            collection.set_linewidths([2.00])
            collection.set_zorder(32)

    if hasattr(view, "ghost_patches"):
        for patch in view.ghost_patches.values():
            patch.set_facecolor((0.00, 0.95, 0.25, 0.055))
            patch.set_edgecolor((0.25, 1.00, 0.40, 0.38))
            patch.set_linewidth(1.05)
            patch.set_linestyle("--")
            patch.set_zorder(22)

    if hasattr(view, "orientation_collections"):
        for collection in view.orientation_collections.values():
            collection.set_colors([(1.00, 0.62, 0.00, 0.95)])
            collection.set_linewidths([1.15])
            collection.set_zorder(42)


def _style_top_labels(view):
    if hasattr(view, "title_text"):
        _style_text(
            view.title_text,
            x=0.022,
            y=0.978,
            fontsize=9.5,
            color="#eef4ff",
            family=TITLE_FONT,
            fontweight="normal",
            alpha=1.0
        )

    if hasattr(view, "status_text"):
        view.status_text.set_text(f"o > ^ W{view.index_res}")

        _style_text(
            view.status_text,
            x=0.974,
            y=0.978,
            fontsize=9.2,
            color="#c7d0dc",
            family=TECH_FONT,
            fontweight="normal",
            alpha=1.0
        )

    if hasattr(view, "no_tiers_text"):
        _style_text(
            view.no_tiers_text,
            fontsize=12,
            family=TECH_FONT,
            color="#f0f0f0",
            fontweight="normal"
        )


def _ensure_pose_readouts(view):
    if getattr(view, "_futuristic_pose_ready", False):
        return

    for name in (
        "theta_text",
        "theta_nav_text",
        "index_text",
        "index_nav_text",
    ):
        if hasattr(view, name):
            getattr(view, name).set_visible(False)

    ax = view.ax

    # -------------------------------------------------
    # θ row
    # -------------------------------------------------

    view.theta_symbol_text = ax.text(
        0.075, 0.294, "θ",
        transform=ax.transAxes,
        ha="left",
        va="center",
        fontsize=12.0,
        color=THETA,
        family=LABEL_FONT,
        fontweight="normal",
        zorder=132
    )

    view.theta_value_text = ax.text(
        0.590, 0.270, "",
        transform=ax.transAxes,
        ha="right",
        va="center",
        fontsize=31,
        color=THETA,
        family=VALUE_FONT,
        fontweight="normal",
        zorder=132
    )

    view.theta_error_text = ax.text(
        0.918, 0.270, "",
        transform=ax.transAxes,
        ha="right",
        va="center",
        fontsize=17.0,
        color=THETA_SOFT,
        family=TECH_FONT,
        fontweight="normal",
        zorder=132
    )

    view.theta_set_text = ax.text(
        0.500, 0.225, "",
        transform=ax.transAxes,
        ha="center",
        va="center",
        fontsize=11.2,
        color="#e6ca56",
        family=TECH_FONT,
        fontweight="normal",
        zorder=132
    )

    # -------------------------------------------------
    # φ row
    # -------------------------------------------------

    view.phi_symbol_text = ax.text(
        0.075, 0.173, "φ",
        transform=ax.transAxes,
        ha="left",
        va="center",
        fontsize=12.0,
        color=PHI,
        family=LABEL_FONT,
        fontweight="normal",
        zorder=132
    )

    view.phi_value_text = ax.text(
        0.590, 0.151, "",
        transform=ax.transAxes,
        ha="right",
        va="center",
        fontsize=25,
        color=PHI,
        family=VALUE_FONT,
        fontweight="normal",
        zorder=132
    )

    view.phi_error_text = ax.text(
        0.918, 0.151, "",
        transform=ax.transAxes,
        ha="right",
        va="center",
        fontsize=15.0,
        color=PHI_SOFT,
        family=TECH_FONT,
        fontweight="normal",
        zorder=132
    )

    view.phi_set_text = ax.text(
        0.500, 0.096,
        "",
        transform=ax.transAxes,
        ha="center",
        va="center",
        fontsize=10.8,
        color="#70ddea",
        family=TECH_FONT,
        fontweight="normal",
        zorder=132
    )

    view._futuristic_pose_ready = True


def _style_force_bar(view):
    if not hasattr(view, "force_bar_bg") or not hasattr(view, "force_bar_fill"):
        return

    x0 = 0.070
    y0 = 0.205
    w = 0.860
    h = 0.0035

    view.force_bar_bg.set_bounds(x0, y0, w, h)
    view.force_bar_bg.set_facecolor("#151a22")
    view.force_bar_bg.set_edgecolor("none")
    view.force_bar_bg.set_linewidth(0.0)
    view.force_bar_bg.set_alpha(0.92)
    view.force_bar_bg.set_zorder(120)

    v = np.clip(float(getattr(view, "force_value", 0.0)), 0.0, 1.0)
    view.force_bar_fill.set_bounds(x0, y0, w * v, h)
    view.force_bar_fill.set_facecolor("#cdb72f")
    view.force_bar_fill.set_edgecolor("none")
    view.force_bar_fill.set_alpha(1.0)
    view.force_bar_fill.set_zorder(121)


def _clear_pose_text(view):
    for name in (
        "theta_value_text",
        "theta_error_text",
        "theta_set_text",
        "phi_value_text",
        "phi_error_text",
        "phi_set_text",
    ):
        if hasattr(view, name):
            getattr(view, name).set_text("")


def _update_pose_text(view):
    _ensure_pose_readouts(view)

    if getattr(view, "target_R", None) is None:
        _clear_pose_text(view)
        return

    target_theta, target_phi = tip_twist_from_R(
        view.target_R,
        index_res=view.index_res,
        index_sign=view.index_sign
    )

    current_theta, current_phi = tip_twist_from_R(
        view.gem_R,
        index_res=view.index_res,
        index_sign=view.index_sign
    )

    theta_value = abs(target_theta)
    theta_error = abs(target_theta - current_theta)

    phi_value = positive_ticks(target_phi, view.index_res)
    phi_error = wrap_ticks_signed(
        target_phi - current_phi,
        index_res=view.index_res
    )

    view.theta_value_text.set_text(f"{theta_value:0.2f}°")
    view.theta_error_text.set_text(f"{theta_error:+0.2f}")

    view.theta_set_text.set_text(
        view.nav_line(
            view.tier_prev_value,
            view.tier_number,
            view.tier_count,
            view.tier_next_value,
            suffix="°"
        )
    )

    view.phi_value_text.set_text(f"{phi_value:0.2f}")
    view.phi_error_text.set_text(f"{phi_error:+0.2f}")

    view.phi_set_text.set_text(
        view.nav_line(
            view.facet_prev_value,
            view.facet_number,
            view.facet_count,
            view.facet_next_value,
            suffix=""
        )
    )


def _configure_style_once(view):
    _setup_screen_backplates(view)
    _style_static_panels(view)
    _style_top_labels(view)
    _ensure_pose_readouts(view)
    _style_force_bar(view)


def _apply_style_frame(view):
    _setup_screen_backplates(view)
    _style_static_panels(view)
    _style_top_labels(view)
    _style_force_bar(view)
    _update_pose_text(view)


def apply_futuristic_style_patch(Viewer):
    if getattr(Viewer, "_futuristic_style_patched", False):
        return Viewer

    old_init = Viewer.__init__
    old_update_hud = Viewer.update_hud

    def patched_init(self, *args, **kwargs):
        old_init(self, *args, **kwargs)
        _configure_style_once(self)

    def patched_update_hud(self):
        old_update_hud(self)
        _apply_style_frame(self)

    Viewer.__init__ = patched_init
    Viewer.update_hud = patched_update_hud
    Viewer._futuristic_style_patched = True

    return Viewer