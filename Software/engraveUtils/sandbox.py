# -*- coding: utf-8 -*-

import numpy as np
import matplotlib.pyplot as plt
import trimesh


# ============================================================
# PARAMETERS
# ============================================================

PLATE_X = 100.0
PLATE_Y = 100.0
PLATE_THICKNESS = 5.0

GRID_N = 700

TOOL_ANGLE_DEG = 90.0

CUT_DEPTH = 0.5


# ============================================================
# PATTERN
# ============================================================

PATTERN = "meander"

MEANDER_SPACING = 6

MEANDER_MARGIN = 5.0


# ============================================================
# WORKPIECE MOTION
# ============================================================

WORK_X = 0.0
WORK_Y = 0.0
WORK_Z = 0.0
WORK_THETA_DEG = 0.0


# ============================================================
# OUTPUT
# ============================================================

OUTPUT_STL = "engraving_demo.stl"


# ============================================================
# GENERATE CONTINUOUS MEANDER TOOLPATH
# ============================================================

def generate_meander():

    x_min = (
        -PLATE_X / 2.0
        +
        MEANDER_MARGIN
    )

    x_max = (
        PLATE_X / 2.0
        -
        MEANDER_MARGIN
    )

    y_min = (
        -PLATE_Y / 2.0
        +
        MEANDER_MARGIN
    )

    y_max = (
        PLATE_Y / 2.0
        -
        MEANDER_MARGIN
    )

    y_values = np.arange(
        y_min,
        y_max + MEANDER_SPACING * 0.5,
        MEANDER_SPACING
    )

    path = []

    for row, y in enumerate(y_values):

        if row % 2 == 0:

            x_start = x_min
            x_end = x_max

        else:

            x_start = x_max
            x_end = x_min

        path.append(
            [
                x_start,
                y
            ]
        )

        path.append(
            [
                x_end,
                y
            ]
        )

        if row < len(y_values) - 1:

            path.append(
                [
                    x_end,
                    y_values[row + 1]
                ]
            )

    return np.asarray(
        path,
        dtype=np.float32
    )


# ============================================================
# WORKPIECE MOTION
# ============================================================

def machine_to_plate(
    path
):

    theta = np.radians(
        WORK_THETA_DEG
    )

    c = np.cos(
        theta
    )

    s = np.sin(
        theta
    )

    p = path.copy()

    p[:, 0] -= WORK_X

    p[:, 1] -= WORK_Y

    x = (
        c * p[:, 0]
        +
        s * p[:, 1]
    )

    y = (
        -s * p[:, 0]
        +
        c * p[:, 1]
    )

    return np.column_stack(
        (
            x,
            y
        )
    )


# ============================================================
# POINT-TO-SEGMENT DISTANCE
# ============================================================

def point_to_segment_distance(
    X,
    Y,
    x0,
    y0,
    x1,
    y1
):

    dx = x1 - x0

    dy = y1 - y0

    length_squared = (
        dx * dx
        +
        dy * dy
    )

    if length_squared == 0.0:

        return np.sqrt(
            (
                X
                -
                x0
            )
            ** 2
            +
            (
                Y
                -
                y0
            )
            ** 2
        )

    t = (
        (
            X
            -
            x0
        )
        *
        dx
        +
        (
            Y
            -
            y0
        )
        *
        dy
    )

    t /= length_squared

    t = np.clip(
        t,
        0.0,
        1.0
    )

    closest_x = (
        x0
        +
        t
        *
        dx
    )

    closest_y = (
        y0
        +
        t
        *
        dy
    )

    return np.sqrt(
        (
            X
            -
            closest_x
        )
        ** 2
        +
        (
            Y
            -
            closest_y
        )
        ** 2
    )


# ============================================================
# SIMULATE CONTINUOUS SWEPT V-TOOL
# ============================================================

def simulate(
    path
):

    print()

    print(
        "Creating simulation grid..."
    )

    x = np.linspace(
        -PLATE_X / 2.0,
        PLATE_X / 2.0,
        GRID_N
    )

    y = np.linspace(
        -PLATE_Y / 2.0,
        PLATE_Y / 2.0,
        GRID_N
    )

    X, Y = np.meshgrid(
        x,
        y
    )

    surface = np.full(
        X.shape,
        PLATE_THICKNESS,
        dtype=np.float32
    )

    angle = np.radians(
        TOOL_ANGLE_DEG
    )

    slope = np.tan(
        angle / 2.0
    )

    tip_z = (
        PLATE_THICKNESS
        -
        CUT_DEPTH
    )

    max_radius = (
        CUT_DEPTH
        /
        slope
    )

    segment_count = (
        len(path)
        -
        1
    )

    print(
        "Tool reach:",
        f"{max_radius:.4f}",
        "mm"
    )

    print(
        "Toolpath segments:",
        f"{segment_count:,}"
    )

    print()

    print(
        "Simulating..."
    )

    for index in range(
        segment_count
    ):

        if index % 25 == 0:

            progress = (
                100.0
                *
                index
                /
                segment_count
            )

            print(
                f"\rProgress: {progress:6.2f}%",
                end=""
            )

        x0 = path[
            index,
            0
        ]

        y0 = path[
            index,
            1
        ]

        x1 = path[
            index + 1,
            0
        ]

        y1 = path[
            index + 1,
            1
        ]

        ix0 = np.searchsorted(
            x,
            min(
                x0,
                x1
            )
            -
            max_radius
        )

        ix1 = np.searchsorted(
            x,
            max(
                x0,
                x1
            )
            +
            max_radius
        )

        iy0 = np.searchsorted(
            y,
            min(
                y0,
                y1
            )
            -
            max_radius
        )

        iy1 = np.searchsorted(
            y,
            max(
                y0,
                y1
            )
            +
            max_radius
        )

        if ix1 <= 0:

            continue

        if ix0 >= GRID_N:

            continue

        if iy1 <= 0:

            continue

        if iy0 >= GRID_N:

            continue

        ix0 = max(
            0,
            ix0
        )

        ix1 = min(
            GRID_N,
            ix1
        )

        iy0 = max(
            0,
            iy0
        )

        iy1 = min(
            GRID_N,
            iy1
        )

        local_x = X[
            iy0:iy1,
            ix0:ix1
        ]

        local_y = Y[
            iy0:iy1,
            ix0:ix1
        ]

        distance = point_to_segment_distance(
            local_x,
            local_y,
            x0,
            y0,
            x1,
            y1
        )

        tool_surface = (
            tip_z
            +
            distance
            *
            slope
        )

        local_surface = surface[
            iy0:iy1,
            ix0:ix1
        ]

        np.minimum(
            local_surface,
            tool_surface,
            out=local_surface
        )

    print(
        "\rProgress: 100.00%"
    )

    print(
        "Simulation complete."
    )

    return (
        X,
        Y,
        surface
    )


# ============================================================
# HEIGHTFIELD TO STL
# ============================================================

def heightfield_to_stl(
    X,
    Y,
    Z
):

    print()

    print(
        "Building STL mesh..."
    )

    ny, nx = Z.shape

    vertices = []

    faces = []

    # --------------------------------------------------------
    # TOP
    # --------------------------------------------------------

    top_vertices = np.column_stack(
        (
            X.ravel(),
            Y.ravel(),
            Z.ravel()
        )
    )

    vertices.extend(
        top_vertices.tolist()
    )

    for j in range(
        ny - 1
    ):

        for i in range(
            nx - 1
        ):

            a = (
                j
                *
                nx
                +
                i
            )

            b = a + 1

            c = (
                (j + 1)
                *
                nx
                +
                i
            )

            d = c + 1

            faces.append(
                [
                    a,
                    b,
                    d
                ]
            )

            faces.append(
                [
                    a,
                    d,
                    c
                ]
            )

    # --------------------------------------------------------
    # BOTTOM
    # --------------------------------------------------------

    bottom_offset = len(
        vertices
    )

    bottom_vertices = np.column_stack(
        (
            X.ravel(),
            Y.ravel(),
            np.zeros(
                X.size
            )
        )
    )

    vertices.extend(
        bottom_vertices.tolist()
    )

    for j in range(
        ny - 1
    ):

        for i in range(
            nx - 1
        ):

            a = (
                bottom_offset
                +
                j
                *
                nx
                +
                i
            )

            b = a + 1

            c = (
                bottom_offset
                +
                (j + 1)
                *
                nx
                +
                i
            )

            d = c + 1

            faces.append(
                [
                    a,
                    d,
                    b
                ]
            )

            faces.append(
                [
                    a,
                    c,
                    d
                ]
            )

    # --------------------------------------------------------
    # WALLS
    # --------------------------------------------------------

    def add_wall(
        t0,
        t1,
        b0,
        b1
    ):

        faces.append(
            [
                t0,
                t1,
                b1
            ]
        )

        faces.append(
            [
                t0,
                b1,
                b0
            ]
        )

    # Bottom edge

    for i in range(
        nx - 1
    ):

        add_wall(
            i,
            i + 1,
            bottom_offset + i,
            bottom_offset + i + 1
        )

    # Top edge

    for i in range(
        nx - 1
    ):

        t0 = (
            (ny - 1)
            *
            nx
            +
            i
        )

        t1 = t0 + 1

        b0 = (
            bottom_offset
            +
            (ny - 1)
            *
            nx
            +
            i
        )

        b1 = b0 + 1

        add_wall(
            t0,
            t1,
            b0,
            b1
        )

    # Left edge

    for j in range(
        ny - 1
    ):

        t0 = (
            j
            *
            nx
        )

        t1 = (
            (j + 1)
            *
            nx
        )

        b0 = (
            bottom_offset
            +
            j
            *
            nx
        )

        b1 = (
            bottom_offset
            +
            (j + 1)
            *
            nx
        )

        add_wall(
            t0,
            t1,
            b0,
            b1
        )

    # Right edge

    for j in range(
        ny - 1
    ):

        t0 = (
            j
            *
            nx
            +
            nx
            -
            1
        )

        t1 = (
            (j + 1)
            *
            nx
            +
            nx
            -
            1
        )

        b0 = (
            bottom_offset
            +
            j
            *
            nx
            +
            nx
            -
            1
        )

        b1 = (
            bottom_offset
            +
            (j + 1)
            *
            nx
            +
            nx
            -
            1
        )

        add_wall(
            t0,
            t1,
            b0,
            b1
        )

    mesh = trimesh.Trimesh(
        vertices=np.asarray(
            vertices,
            dtype=np.float32
        ),
        faces=np.asarray(
            faces,
            dtype=np.int64
        ),
        process=False
    )

    return mesh


# ============================================================
# SHOW RESULTS
# ============================================================

def show_results(
    path,
    X,
    Y,
    Z
):

    print()

    print(
        "Creating toolpath plot..."
    )

    fig1 = plt.figure(
        figsize=(
            9,
            9
        )
    )

    ax1 = fig1.add_subplot(
        111
    )

    ax1.plot(
        path[
            :,
            0
        ],
        path[
            :,
            1
        ],
        linewidth=0.5
    )

    ax1.set_aspect(
        "equal"
    )

    ax1.set_xlabel(
        "X (mm)"
    )

    ax1.set_ylabel(
        "Y (mm)"
    )

    ax1.set_title(
        "Continuous CAM Toolpath"
    )

    ax1.grid(
        True
    )

    fig1.tight_layout()


    # ========================================================
    # DEPTH MAP
    # ========================================================

    print(
        "Creating depth map..."
    )

    depth = (
        PLATE_THICKNESS
        -
        Z
    )

    fig2 = plt.figure(
        figsize=(
            10,
            9
        )
    )

    ax2 = fig2.add_subplot(
        111
    )

    image = ax2.imshow(
        depth,
        extent=[
            -PLATE_X / 2.0,
            PLATE_X / 2.0,
            -PLATE_Y / 2.0,
            PLATE_Y / 2.0
        ],
        origin="lower",
        interpolation="nearest"
    )

    ax2.plot(
        path[
            :,
            0
        ],
        path[
            :,
            1
        ],
        linewidth=0.3,
        alpha=0.35
    )

    ax2.set_aspect(
        "equal"
    )

    ax2.set_xlabel(
        "X (mm)"
    )

    ax2.set_ylabel(
        "Y (mm)"
    )

    ax2.set_title(
        "Full-Resolution Engraving Depth"
    )

    fig2.colorbar(
        image,
        ax=ax2,
        label="Depth (mm)"
    )

    fig2.tight_layout()


    # ========================================================
    # CONTOUR PLOT
    # ========================================================

    print(
        "Creating contour plot..."
    )

    fig3 = plt.figure(
        figsize=(
            10,
            9
        )
    )

    ax3 = fig3.add_subplot(
        111
    )

    levels = np.linspace(
        0.0,
        CUT_DEPTH,
        21
    )

    contour = ax3.contourf(
        X,
        Y,
        depth,
        levels=levels
    )

    ax3.contour(
        X,
        Y,
        depth,
        levels=levels,
        linewidths=0.25
    )

    ax3.plot(
        path[
            :,
            0
        ],
        path[
            :,
            1
        ],
        linewidth=0.3,
        alpha=0.25
    )

    ax3.set_aspect(
        "equal"
    )

    ax3.set_xlabel(
        "X (mm)"
    )

    ax3.set_ylabel(
        "Y (mm)"
    )

    ax3.set_title(
        "Engraving Contours"
    )

    fig3.colorbar(
        contour,
        ax=ax3,
        label="Depth (mm)"
    )

    fig3.tight_layout()


    # ========================================================
    # 3D SURFACE
    # ========================================================

    print(
        "Creating 3D surface..."
    )

    fig4 = plt.figure(
        figsize=(
            12,
            9
        )
    )

    ax4 = fig4.add_subplot(
        111,
        projection="3d"
    )

    display_step = max(
        1,
        GRID_N // 250
    )

    ax4.plot_surface(
        X[
            ::display_step,
            ::display_step
        ],
        Y[
            ::display_step,
            ::display_step
        ],
        Z[
            ::display_step,
            ::display_step
        ],
        linewidth=0,
        antialiased=False
    )

    ax4.set_xlabel(
        "X (mm)"
    )

    ax4.set_ylabel(
        "Y (mm)"
    )

    ax4.set_zlabel(
        "Z (mm)"
    )

    ax4.set_title(
        "Engraved Surface"
    )

    ax4.set_xlim(
        -PLATE_X / 2.0,
        PLATE_X / 2.0
    )

    ax4.set_ylim(
        -PLATE_Y / 2.0,
        PLATE_Y / 2.0
    )

    ax4.set_zlim(
        PLATE_THICKNESS - CUT_DEPTH,
        PLATE_THICKNESS
    )

    ax4.set_box_aspect(
        (
            1.0,
            1.0,
            0.2
        )
    )

    ax4.view_init(
        elev=35.0,
        azim=-60.0
    )

    fig4.tight_layout()


    # ========================================================
    # CENTER CROSS-SECTION
    # ========================================================

    print(
        "Creating center cross-section..."
    )

    center_row = GRID_N // 2

    fig5 = plt.figure(
        figsize=(
            12,
            5
        )
    )

    ax5 = fig5.add_subplot(
        111
    )

    ax5.plot(
        X[
            center_row,
            :
        ],
        Z[
            center_row,
            :
        ],
        linewidth=1.0
    )

    ax5.axhline(
        PLATE_THICKNESS,
        linestyle="--",
        linewidth=0.8
    )

    ax5.set_xlabel(
        "X (mm)"
    )

    ax5.set_ylabel(
        "Z (mm)"
    )

    ax5.set_title(
        "Center Cross-Section"
    )

    ax5.set_xlim(
        -PLATE_X / 2.0,
        PLATE_X / 2.0
    )

    ax5.set_ylim(
        PLATE_THICKNESS - CUT_DEPTH - 0.05,
        PLATE_THICKNESS + 0.05
    )

    ax5.grid(
        True
    )

    fig5.tight_layout()


# ============================================================
# MAIN
# ============================================================

def main():

    print(
        "="
        *
        60
    )

    print(
        "CONTINUOUS V-TOOL ENGRAVING SIMULATOR"
    )

    print(
        "="
        *
        60
    )

    print()

    print(
        "Generating toolpath..."
    )

    raw_path = generate_meander()

    print(
        "Path points:",
        len(raw_path)
    )

    print()

    print(
        "Applying workpiece motion..."
    )

    plate_path = machine_to_plate(
        raw_path
    )

    print()

    print(
        "Running simulation..."
    )

    X, Y, Z = simulate(
        plate_path
    )

    print()

    print(
        "Simulation finished."
    )

    print()

    print(
        "Building STL..."
    )

    mesh = heightfield_to_stl(
        X,
        Y,
        Z
    )

    print(
        "STL vertices:",
        f"{len(mesh.vertices):,}"
    )

    print(
        "STL faces:",
        f"{len(mesh.faces):,}"
    )

    print(
        "Watertight:",
        mesh.is_watertight
    )

    print()

    print(
        "Exporting STL..."
    )

    mesh.export(
        OUTPUT_STL
    )

    print(
        "STL exported:"
    )

    print(
        OUTPUT_STL
    )

    print()

    print(
        "Creating plots..."
    )

    show_results(
        plate_path,
        X,
        Y,
        Z
    )

    print()

    print(
        "All plots created."
    )

    print(
        "Launching Qt/Matplotlib windows..."
    )

    plt.show(
        block=True
    )


# ============================================================
# ENTRY POINT
# ============================================================

if __name__ == "__main__":

    main()