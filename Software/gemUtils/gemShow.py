# -*- coding: utf-8 -*-

import numpy as np
import pathlib
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation


# =========================================================
# PARSER
# =========================================================

def parse_gemcad(path):
    raw = []

    with open(path, "r") as f:
        for line in f:
            if not line.startswith("a"):
                continue

            p = line.split()

            tilt = float(p[1])
            d = float(p[2])

            if "n" in p:
                ni = p.index("n")
                idxs = p[ni+1:]
            else:
                idxs = p[3:]

            idx = []
            for x in idxs:
                try:
                    idx.append(int(x))
                except:
                    pass

            raw.append((tilt, d, idx))

    return raw


# =========================================================
# HALFSPACE CONSTRUCTION (CRITICAL FIXED ORIENTATION)
# =========================================================

def idx_to_az(i, n=96):
    return 2.0 * np.pi * i / n


def plane(tilt_deg, az, d):
    t = np.deg2rad(tilt_deg)

    n = np.array([
        np.cos(t) * np.cos(az),
        np.cos(t) * np.sin(az),
        np.sin(t)
    ], dtype=float)

    n /= (np.linalg.norm(n) + 1e-12)

    # IMPORTANT:
    # Ensure halfspace is consistently oriented so origin is inside polyhedron.
    # We enforce: n·0 + d <= 0 → d <= 0
    if d > 0:
        n = -n
        d = -d

    return n, d


# =========================================================
# VERTEX SOLVER
# =========================================================

def intersect3(n1, d1, n2, d2, n3, d3):
    A = np.stack([n1, n2, n3])
    b = np.array([-d1, -d2, -d3])
    try:
        return np.linalg.solve(A, b)
    except:
        return None


def inside(p, planes):
    for n, d in planes:
        if np.dot(n, p) + d > 1e-9:
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
                if c % 3000 == 0:
                    print(f"\rbuilding vertices: {100.0 * c / total:.2f}%", end="")

                if p is None:
                    continue

                if inside(p, planes):
                    pts.append(p)
                    support.append({i, j, k})

    print("\rbuilding vertices: done            ")

    # deduplicate vertices + merge supports
    uniq = []
    uniq_sup = []
    seen = {}

    for p, s in zip(pts, support):
        key = tuple(np.round(p, 10))

        if key not in seen:
            seen[key] = len(uniq)
            uniq.append(p)
            uniq_sup.append(set(s))
        else:
            uniq_sup[seen[key]] |= s

    return np.array(uniq), uniq_sup


# =========================================================
# EDGE CONSTRUCTION
# =========================================================

def build_edges(support):
    edges = []
    N = len(support)

    for i in range(N):
        for j in range(i + 1, N):
            if len(support[i].intersection(support[j])) >= 2:
                edges.append((i, j))

    return edges


# =========================================================
# VIEWER
# =========================================================

class Viewer:
    def __init__(self, ax, pts, edges):
        self.ax = ax
        self.pts = pts
        self.edges = edges

        self.lines = [
            ax.plot([], [], 'k-', lw=0.8)[0]
            for _ in edges
        ]

        ax.set_aspect("equal")
        ax.set_xlim(-1.2, 1.2)
        ax.set_ylim(-1.2, 1.2)
        ax.axis("off")

        self.spin = 0.0
        self.tilt = 0.0

    def update(self):

        c, s = np.cos(self.spin), np.sin(self.spin)
        Rz = np.array([
            [c, -s, 0],
            [s,  c, 0],
            [0,  0, 1]
        ])

        c, s = np.cos(self.tilt), np.sin(self.tilt)
        Rx = np.array([
            [1, 0,  0],
            [0, c, -s],
            [0, s,  c]
        ])

        R = Rx @ Rz

        v = (R @ self.pts.T).T

        for idx, (a, b) in enumerate(self.edges):
            pa = v[a]
            pb = v[b]

            self.lines[idx].set_data(
                [pa[0], pb[0]],
                [pa[1], pb[1]]
            )


# =========================================================
# MAIN (CACHE + PROGRESS)
# =========================================================

if __name__ == "__main__":

    path = pathlib.Path("./data/pc01391.asc")
    cache = path.with_suffix(".npz")

    if False: #cache.exists():
        print("loading cache...")
        data = np.load(cache, allow_pickle=True)
        pts = data["pts"]
        support = data["support"]
        edges = data["edges"]

    else:
        raw = parse_gemcad(path)

        planes = []
        for t, d, idxs in raw:
            for i in idxs:
                planes.append(plane(t, idx_to_az(i), d))

        pts, support = build_vertices(planes)
        edges = build_edges(support)

        np.savez_compressed(
            cache,
            pts=pts,
            support=np.array(support, dtype=object),
            edges=np.array(edges, dtype=object)
        )

    print("vertices:", len(pts))
    print("edges:", len(edges))

    fig, ax = plt.subplots()
    view = Viewer(ax, pts, edges)

    def on_key(e):
        if e.key == "left":
            view.spin -= 0.1
        elif e.key == "right":
            view.spin += 0.1
        elif e.key == "up":
            view.tilt += 0.1
        elif e.key == "down":
            view.tilt -= 0.1

    fig.canvas.mpl_connect("key_press_event", on_key)

    def update(_):
        view.update()

    anim = FuncAnimation(fig, update, interval=30)
    plt.show()