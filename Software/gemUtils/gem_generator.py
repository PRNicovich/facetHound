# -*- coding: utf-8 -*-

import numpy as np
from gemcad_io import loadGemCADFile


def positive_index_tick(i, index_res):
    return int(i % index_res)


def idx_to_az(i, index_res, index_sign=1, meridian=0.0):
    offset = meridian % index_res
    return index_sign * 2.0 * np.pi * ((i - offset) % index_res) / index_res


def plane(tilt_deg, az, d):
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
    for i, (n, d) in enumerate(planes):
        if d < -tol:
            print(f"WARNING: plane {i} has negative distance d={d:.6f} "
                  f"(n={n}) -- check tilt/azimuth/index parsing.")
    return planes


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


def build_edges(pts, support, planes, tol=1e-9):
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


def find_facet_by_tier_twist(facet_meta, tier, twist, tier_base=1, twist_base=1):
    target_tier = tier - tier_base
    target_twist = twist - twist_base

    for plane_id, meta in enumerate(facet_meta):
        if meta["tier"] == target_tier and meta["twist"] == target_twist:
            return plane_id

    raise ValueError(f"No facet found for tier={tier}, facet={twist}")


def build_tier_nav(facet_meta):
    tier_to_planes = {}

    for plane_id, meta in enumerate(facet_meta):
        tier_to_planes.setdefault(meta["tier"], []).append(plane_id)

    for tier in tier_to_planes:
        tier_to_planes[tier].sort(key=lambda pid: facet_meta[pid]["twist"])

    available_tiers = sorted(tier_to_planes)

    return available_tiers, tier_to_planes


def build_gem(path):
    gemDict = loadGemCADFile(path)

    wheel_raw = int(gemDict.get("wheelIndex", 96) or 96)
    index_sign = -1 if wheel_raw < 0 else 1
    index_res = abs(wheel_raw)
    meridian = float(gemDict.get("meridian", 0.0) or 0.0)

    print(f"index resolution: {index_res}, sign: {index_sign:+d}, "
          f"meridian offset: {meridian}")

    planes = []
    facet_meta = []

    for tier_id, facet_line in enumerate(gemDict["facetList"]):
        tilt = float(facet_line["angle"])
        d = float(facet_line["depth"])

        for twist_id, facet in enumerate(facet_line["facets"]):
            i = float(facet["value"])

            az = idx_to_az(i, index_res, index_sign, meridian)
            planes.append(plane(tilt, az, d))

            facet_meta.append({
                "tier": tier_id,
                "twist": twist_id,
                "index": positive_index_tick(i, index_res),
                "raw_index": i,
                "facet_name": facet.get("name", ""),
                "tilt": tilt,
                "distance": d,
            })

    planes = validate_planes(planes)

    pts, support = build_vertices(planes)
    edges = build_edges(pts, support, planes)

    return (
        pts,
        support,
        edges,
        planes,
        facet_meta,
        index_res,
        index_sign,
        gemDict
    )