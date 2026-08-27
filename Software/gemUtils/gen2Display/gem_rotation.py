# -*- coding: utf-8 -*-
"""
Created on Wed Jul  1 18:08:06 2026

@author: rusty
"""

# -*- coding: utf-8 -*-

import numpy as np


# =========================================================
# QUATERNION HELPERS
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


# =========================================================
# ROTATION HELPERS
# =========================================================

def smoothstep(t):
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def wrap_degrees(a):
    return (a + 180.0) % 360.0 - 180.0


def wrap_ticks_signed(ticks, index_res):
    half = index_res / 2.0
    return (ticks + half) % index_res - half


def positive_ticks(ticks, index_res):
    return ticks % index_res


def angle_deg_to_index_ticks(deg, index_res, index_sign=1):
    return index_sign * deg * index_res / 360.0


def dop_twist_matrix(ticks, index_res, index_sign=1):
    """
    Twist around the gem's own DOP axis.

    Input:
        ticks: GemCAD index ticks.
    """
    angle = index_sign * 2.0 * np.pi * ticks / index_res
    c, s = np.cos(angle), np.sin(angle)

    return np.array([
        [c, -s, 0],
        [s,  c, 0],
        [0,  0, 1]
    ], dtype=float)


def page_tip_matrix(angle_rad):
    """
    Tip around the page-space left-right axis.
    """
    c, s = np.cos(angle_rad), np.sin(angle_rad)

    return np.array([
        [1, 0,  0],
        [0, c, -s],
        [0, s,  c]
    ], dtype=float)


def tip_twist_from_R(R, index_res, index_sign=1):
    """
    Approximate control-space angles from display rotation.

    Returns:
        theta_tip_deg:
            page-horizontal tip angle, degrees.

        phi_twist_ticks:
            DOP/local-Z twist, positive-able index tick coordinate.
    """
    dop = R @ np.array([0.0, 0.0, 1.0], dtype=float)
    dn = np.linalg.norm(dop)

    if dn > 1e-12:
        dop /= dn

    theta_tip_deg = np.rad2deg(np.arctan2(-dop[1], dop[2]))

    theta = np.deg2rad(theta_tip_deg)
    c, s = np.cos(-theta), np.sin(-theta)

    Rx_un_tip = np.array([
        [1, 0,  0],
        [0, c, -s],
        [0, s,  c]
    ], dtype=float)

    R_flat = Rx_un_tip @ R

    twist_deg = np.rad2deg(np.arctan2(R_flat[1, 0], R_flat[0, 0]))
    phi_twist_ticks = angle_deg_to_index_ticks(
        twist_deg,
        index_res=index_res,
        index_sign=index_sign
    )

    return theta_tip_deg, phi_twist_ticks