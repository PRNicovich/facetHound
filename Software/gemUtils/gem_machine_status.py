# -*- coding: utf-8 -*-

from matplotlib.patches import Rectangle


# =====================================================
# Edit layout/text here
# =====================================================

LABEL_FONT = "Segoe UI"
VALUE_FONT = "Consolas"

HEAD_FONT_SIZE = 8.0
BODY_FONT_SIZE = 14.0

HEAD_Y = 0.065
BODY_Y = 0.026

Z_X = 0.018
M_X = 0.500
F_X = 0.982

# Label templates.
# Available fields:
#   Z_LABEL_TEMPLATE: {step}
#   M_LABEL_TEMPLATE: {dir}
#   F_LABEL_TEMPLATE: {state}
Z_LABEL_TEMPLATE = "Zmmm - {step} - mm"
M_LABEL_TEMPLATE = "M - {dir} - rpm"
F_LABEL_TEMPLATE = "F {state} mL/min"

# Body value formatting.
# Keep widths fixed so / and units do not jump.
Z_VALUE_WIDTH = 8
RPM_VALUE_WIDTH = 4
FLOW_VALUE_WIDTH = 6

M_PAUSED_SET_FIELD = " -- "
M_RUNNING_SET_WIDTH = 4

M_BODY_SPACER = " / "

# =====================================================
# Colors
# =====================================================

MACHINE_BG = "#02050a"

Z_HEAD = "#ff63d8"
Z_BODY = "#ff9ce9"

M_HEAD_ACTIVE = "#eef4ff"
M_BODY_ACTIVE = "#f4f7fb"
M_HEAD_IDLE = "#7f8996"
M_BODY_IDLE = "#b0bac8"

F_HEAD_ACTIVE = "#40e8ff"
F_BODY_ACTIVE = "#9af5ff"
F_HEAD_IDLE = "#7f8996"
F_BODY_IDLE = "#b0bac8"


def _to_float(value, default=0.0):
    try:
        return float(value)
    except (TypeError, ValueError):
        return float(default)


def _to_bool(value):
    return bool(value)


def _clamp_int(value, lo, hi, default=None):
    if default is None:
        default = lo

    try:
        value = int(value)
    except (TypeError, ValueError):
        value = int(default)

    return max(lo, min(hi, value))


def _fixed_signed_mm(value):
    v = _to_float(value, 0.0)
    text = f"{v:+0{Z_VALUE_WIDTH}.3f}"

    if len(text) > Z_VALUE_WIDTH:
        text = "#" * Z_VALUE_WIDTH

    return text


def _fixed_rpm(value, width=RPM_VALUE_WIDTH):
    v = int(round(abs(_to_float(value, 0.0))))
    text = str(v)

    if len(text) > width:
        text = "#" * width

    return text.rjust(width)


def _fixed_flow(value, width=FLOW_VALUE_WIDTH):
    v = _to_float(value, 0.0)
    text = f"{v:.2f}"

    if len(text) > width:
        text = "#" * width

    return text.rjust(width)


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


def ensure_machine_status_attrs(view):
    if not hasattr(view, "z_value_mm"):
        view.z_value_mm = 0.0

    if not hasattr(view, "z_step_ticks"):
        view.z_step_ticks = 1

    if not hasattr(view, "motor_direction"):
        view.motor_direction = 1

    if not hasattr(view, "motor_running"):
        view.motor_running = False

    if not hasattr(view, "flow_running"):
        view.flow_running = False


def _ensure_machine_backplates(view):
    # Invisible bottom backplate only to clear older visible boxed versions.
    _rect(
        view,
        "_machine_bottom_back",
        (0.000, 0.000, 1.000, 0.074),
        MACHINE_BG,
        "none",
        0.0,
        0.0,
        136
    )

    for attr in (
        "_machine_z_box",
        "_machine_m_box",
        "_machine_f_box",
    ):
        _rect(
            view,
            attr,
            (0.0, 0.0, 0.0, 0.0),
            MACHINE_BG,
            "none",
            0.0,
            0.0,
            136
        )


def _ensure_machine_texts(view):
    if getattr(view, "_machine_texts_ready", False):
        _ensure_machine_backplates(view)
        return

    ax = view.ax

    _ensure_machine_backplates(view)

    for name in ("z_text", "rpm_text", "flow_text"):
        if hasattr(view, name):
            getattr(view, name).set_visible(False)

    view.z_head_text = ax.text(
        Z_X, HEAD_Y, "",
        transform=ax.transAxes,
        ha="left",
        va="center",
        fontsize=HEAD_FONT_SIZE,
        color=Z_HEAD,
        family=LABEL_FONT,
        fontweight="normal",
        zorder=151
    )

    view.z_body_text = ax.text(
        Z_X, BODY_Y, "",
        transform=ax.transAxes,
        ha="left",
        va="center",
        fontsize=BODY_FONT_SIZE,
        color=Z_BODY,
        family=VALUE_FONT,
        fontweight="normal",
        zorder=151
    )

    view.m_head_text = ax.text(
        M_X, HEAD_Y, "",
        transform=ax.transAxes,
        ha="center",
        va="center",
        fontsize=HEAD_FONT_SIZE,
        color=M_HEAD_IDLE,
        family=LABEL_FONT,
        fontweight="normal",
        zorder=151
    )

    view.m_body_text = ax.text(
        M_X, BODY_Y, "",
        transform=ax.transAxes,
        ha="center",
        va="center",
        fontsize=BODY_FONT_SIZE,
        color=M_BODY_IDLE,
        family=VALUE_FONT,
        fontweight="normal",
        zorder=151
    )

    view.f_head_text = ax.text(
        F_X, HEAD_Y, "",
        transform=ax.transAxes,
        ha="right",
        va="center",
        fontsize=HEAD_FONT_SIZE,
        color=F_HEAD_IDLE,
        family=LABEL_FONT,
        fontweight="normal",
        zorder=151
    )

    view.f_body_text = ax.text(
        F_X, BODY_Y, "",
        transform=ax.transAxes,
        ha="right",
        va="center",
        fontsize=BODY_FONT_SIZE,
        color=F_BODY_IDLE,
        family=VALUE_FONT,
        fontweight="normal",
        zorder=151
    )

    view._machine_texts_ready = True


def _apply_machine_layout(view):
    """
    Re-apply font sizes and positions every update.

    This makes quick edits to constants above show up reliably when the module
    is reloaded and also prevents old patch versions from leaving stale layout.
    """
    view.z_head_text.set_position((Z_X, HEAD_Y))
    view.z_head_text.set_fontsize(HEAD_FONT_SIZE)
    view.z_head_text.set_family(LABEL_FONT)

    view.z_body_text.set_position((Z_X, BODY_Y))
    view.z_body_text.set_fontsize(BODY_FONT_SIZE)
    view.z_body_text.set_family(VALUE_FONT)

    view.m_head_text.set_position((M_X, HEAD_Y))
    view.m_head_text.set_fontsize(HEAD_FONT_SIZE)
    view.m_head_text.set_family(LABEL_FONT)

    view.m_body_text.set_position((M_X, BODY_Y))
    view.m_body_text.set_fontsize(BODY_FONT_SIZE)
    view.m_body_text.set_family(VALUE_FONT)

    view.f_head_text.set_position((F_X, HEAD_Y))
    view.f_head_text.set_fontsize(HEAD_FONT_SIZE)
    view.f_head_text.set_family(LABEL_FONT)

    view.f_body_text.set_position((F_X, BODY_Y))
    view.f_body_text.set_fontsize(BODY_FONT_SIZE)
    view.f_body_text.set_family(VALUE_FONT)


def format_z_header(z_step_ticks):
    step = _clamp_int(z_step_ticks, 1, 3, default=1)
    return Z_LABEL_TEMPLATE.format(step=step)


def format_z_body(z_value_mm):
    return _fixed_signed_mm(z_value_mm)


def format_motor_header(motor_direction):
    direction = "<" if _to_float(motor_direction, 1.0) >= 0.0 else ">"
    return M_LABEL_TEMPLATE.format(dir=direction)


def format_motor_body(rpm_set, rpm_actual, motor_running):
    if _to_bool(motor_running):
        set_field = _fixed_rpm(rpm_set, width=M_RUNNING_SET_WIDTH)
    else:
        set_field = M_PAUSED_SET_FIELD

    actual_field = _fixed_rpm(rpm_actual, width=RPM_VALUE_WIDTH)

    return f"{set_field}{M_BODY_SPACER}{actual_field}"


def format_flow_header(flow_running):
    state = ">" if _to_bool(flow_running) else "-"
    return F_LABEL_TEMPLATE.format(state=state)


def format_flow_body(flow_set):
    return _fixed_flow(flow_set, width=FLOW_VALUE_WIDTH)


def set_machine_status_values(
    view,
    z_value_mm=None,
    z_step_ticks=None,
    motor_direction=None,
    motor_running=None,
    flow_running=None
):
    ensure_machine_status_attrs(view)

    if z_value_mm is not None:
        view.z_value_mm = _to_float(z_value_mm, 0.0)

    if z_step_ticks is not None:
        view.z_step_ticks = _clamp_int(z_step_ticks, 1, 3, default=1)

    if motor_direction is not None:
        view.motor_direction = 1 if _to_float(motor_direction, 1.0) >= 0.0 else -1

    if motor_running is not None:
        view.motor_running = _to_bool(motor_running)

    if flow_running is not None:
        view.flow_running = _to_bool(flow_running)


def _apply_state_colors(view):
    motor_active = _to_bool(view.motor_running)
    flow_active = _to_bool(view.flow_running)

    view.z_head_text.set_color(Z_HEAD)
    view.z_body_text.set_color(Z_BODY)

    if motor_active:
        view.m_head_text.set_color(M_HEAD_ACTIVE)
        view.m_body_text.set_color(M_BODY_ACTIVE)
    else:
        view.m_head_text.set_color(M_HEAD_IDLE)
        view.m_body_text.set_color(M_BODY_IDLE)

    if flow_active:
        view.f_head_text.set_color(F_HEAD_ACTIVE)
        view.f_body_text.set_color(F_BODY_ACTIVE)
    else:
        view.f_head_text.set_color(F_HEAD_IDLE)
        view.f_body_text.set_color(F_BODY_IDLE)


def apply_machine_status_text(view):
    ensure_machine_status_attrs(view)
    _ensure_machine_texts(view)
    _apply_machine_layout(view)

    view.z_head_text.set_text(format_z_header(view.z_step_ticks))
    view.z_body_text.set_text(format_z_body(view.z_value_mm))

    view.m_head_text.set_text(format_motor_header(view.motor_direction))
    view.m_body_text.set_text(
        format_motor_body(
            view.rpm_set,
            view.rpm_actual,
            view.motor_running
        )
    )

    view.f_head_text.set_text(format_flow_header(view.flow_running))
    view.f_body_text.set_text(format_flow_body(view.flow_set))

    _apply_state_colors(view)


def apply_machine_status_patch(Viewer):
    if getattr(Viewer, "_machine_status_patched", False):
        return Viewer

    old_init = Viewer.__init__
    old_update_hud = Viewer.update_hud
    old_set_machine_values = getattr(Viewer, "set_machine_values", None)

    def patched_init(self, *args, **kwargs):
        old_init(self, *args, **kwargs)
        _ensure_machine_texts(self)

    def patched_update_hud(self):
        old_update_hud(self)
        apply_machine_status_text(self)

    def patched_set_machine_values(
        self,
        rpm_set=None,
        rpm_actual=None,
        flow_set=None,
        flow_actual=None,
        force=None,
        z_value_mm=None,
        z_step_ticks=None,
        motor_direction=None,
        motor_running=None,
        flow_running=None
    ):
        if old_set_machine_values is not None:
            old_set_machine_values(
                self,
                rpm_set=rpm_set,
                rpm_actual=rpm_actual,
                flow_set=flow_set,
                flow_actual=flow_actual,
                force=force
            )
        else:
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

        set_machine_status_values(
            self,
            z_value_mm=z_value_mm,
            z_step_ticks=z_step_ticks,
            motor_direction=motor_direction,
            motor_running=motor_running,
            flow_running=flow_running
        )

    Viewer.__init__ = patched_init
    Viewer.update_hud = patched_update_hud
    Viewer.set_machine_values = patched_set_machine_values
    Viewer._machine_status_patched = True

    return Viewer