# -*- coding: utf-8 -*-

import pathlib
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation

from gem_generator import (
    build_gem,
    build_tier_nav,
    find_facet_by_tier_twist,
)

# Cache is optional. If the file is missing, fall back cleanly.
try:
    from gem_cache import load_or_build_gem
except ModuleNotFoundError:
    def load_or_build_gem(path, force_rebuild=False, cache_dir=None):
        print("gem_cache.py not found; building without cache")
        return build_gem(path)

from gem_display_style import apply_futuristic_style_patch
from gem_machine_status import apply_machine_status_patch


display_mode = "static"   # "dynamic" or "static"

if display_mode == "static":
    from gem_display_static import Viewer
elif display_mode == "dynamic":
    from gem_display import Viewer
else:
    from uiDisplay import Viewer

apply_futuristic_style_patch(Viewer)
apply_machine_status_patch(Viewer)


if __name__ == "__main__":

    interactive = True
    path = pathlib.Path("../data/pc01236.asc")
    force_rebuild_cache = False

    (
        pts,
        support,
        edges,
        planes,
        facet_meta,
        index_res,
        index_sign,
        gemDict
    ) = load_or_build_gem(
        path,
        force_rebuild=force_rebuild_cache
    )

    print("planes:", len(planes))
    print("vertices:", len(pts))
    print("edges:", len(edges))

    highlight_enabled = True

    start_tier = 1
    start_facet = 1

    tier_base = 1
    twist_base = 1
    transition_steps = 8

    gem_name = gemDict.get("boldTitle") or "BOLD"
    highlighted_facet_always_up = True

    # ----------------------------
    # machine/controller state
    # ----------------------------
    z_value_mm = 0.0
    z_step_ticks = 1

    # user convention:
    #   +1 => CW  => "<"
    #   -1 => CCW => ">"
    motor_direction = 1
    motor_running = False

    rpm_set = 0.0
    rpm_actual = 0.0

    flow_running = False
    flow_set = 0.0
    flow_actual = 0.0

    force_value = 0.35
    status_symbols = "X K 0"

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

    fig, ax = plt.subplots(figsize=(3.2, 4.8), dpi=100)
    fig.subplots_adjust(left=0, right=1, bottom=0, top=1)

    viewer_kwargs = dict(
        index_res=index_res,
        index_sign=index_sign,
        facet_meta=facet_meta,
        highlighted_facet_always_up=highlighted_facet_always_up,
        rpm_set=rpm_set,
        rpm_actual=rpm_actual,
        flow_set=flow_set,
        flow_actual=flow_actual,
        force_value=force_value,
        gem_name=gem_name,
        status_symbols=status_symbols,
        highlight_planes=highlight_planes
    )

    if display_mode == "static":
        viewer_kwargs["meridian"] = gemDict.get("meridian", 0.0)

    view = Viewer(
        ax,
        pts,
        edges,
        support,
        **viewer_kwargs
    )

    view.set_machine_values(
        rpm_set=rpm_set,
        rpm_actual=rpm_actual,
        flow_set=flow_set,
        flow_actual=flow_actual,
        force=force_value,
        z_value_mm=z_value_mm,
        z_step_ticks=z_step_ticks,
        motor_direction=motor_direction,
        motor_running=motor_running,
        flow_running=flow_running
    )

    def tier_theta_value(tier_pos):
        tier_pos %= len(available_tiers)
        tier = available_tiers[tier_pos]
        plane_id = tier_to_planes[tier][0]
        theta, _ = view.target_values_for_plane(plane_id, planes)
        return theta

    def facet_phi_value(planes_here, facet_pos):
        facet_pos %= len(planes_here)
        plane_id = planes_here[facet_pos]
        _, phi = view.target_values_for_plane(plane_id, planes)
        return phi

    def update_highlight_label(animate=True):
        if not available_tiers:
            view.highlight_planes = set()
            view.target_plane_id = None
            view.target_R = None
            view.set_no_tiers(True)
            return

        view.set_no_tiers(False)

        tier = available_tiers[nav["tier_pos"]]
        planes_here = tier_to_planes[tier]
        nav["facet_pos"] %= len(planes_here)

        plane_id = planes_here[nav["facet_pos"]]
        meta = facet_meta[plane_id]

        facet_display = meta["twist"] + twist_base
        tier_display = meta["tier"] + tier_base

        view.set_facet_hud(
            facet_number=facet_display,
            facet_count=len(planes_here),
            facet_prev_value=facet_phi_value(planes_here, nav["facet_pos"] - 1),
            facet_next_value=facet_phi_value(planes_here, nav["facet_pos"] + 1),
            tier_number=tier_display,
            tier_count=len(available_tiers),
            tier_prev_value=tier_theta_value(nav["tier_pos"] - 1),
            tier_next_value=tier_theta_value(nav["tier_pos"] + 1)
        )

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
        fig.savefig(
            out,
            dpi=100,
            facecolor=fig.get_facecolor()
        )
        print(f"saved preview to {out}")