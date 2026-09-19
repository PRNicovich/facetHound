"""Geometry regression checks; runnable directly in Python/Spyder (requires numpy)."""
from pathlib import Path
import contextlib
import io
import sys
import numpy as np

SOFTWARE = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SOFTWARE / "gemUtils"))
from gem_generator import build_gem, plane, idx_to_az
from gemcad_io import loadGemCADFile


def main():
    for name in ("pc04188.asc", "pc42011.asc", "pc01028c.asc", "pc01043.asc"):
        design = loadGemCADFile(str(SOFTWARE / "gemUtils/data" / name))
        wheel = abs(design["wheelIndex"])
        sign = -1 if design["wheelIndex"] < 0 else 1
        count = 0
        for tier in design["facetList"]:
            for facet in tier["facets"]:
                az = idx_to_az(facet["value"], wheel, sign, design.get("meridian", 0))
                n, _ = plane(tier["angle"], az, tier["depth"])
                # Same Rz then Rx convention as the TFT renderer: +Z faces viewer.
                yaw = np.pi / 2 - np.arctan2(n[1], n[0])
                pitch = np.arctan2(np.hypot(n[0], n[1]), n[2])
                x = np.cos(yaw)*n[0] - np.sin(yaw)*n[1]
                y = np.sin(yaw)*n[0] + np.cos(yaw)*n[1]
                projected = [x, np.cos(pitch)*y-np.sin(pitch)*n[2],
                             np.sin(pitch)*y+np.cos(pitch)*n[2]]
                np.testing.assert_allclose(projected, [0, 0, 1], atol=1e-6)
                count += 1
        print(f"{name}: {count} facet normals face viewer at target")
    with contextlib.redirect_stdout(io.StringIO()):
        vertices, support, edges, planes, *_ = build_gem(str(SOFTWARE / "gemUtils/data/pc42011.asc"))
    assert len(edges) > 0
    np.testing.assert_allclose([vertices[:, 2].min(), vertices[:, 2].max()],
                               [-0.60635, 0.99174], atol=1e-5)
    np.testing.assert_allclose(plane(-0.0, 0, 0.5)[0], [0, 0, -1])
    # Reproduce the firmware's edge-derived normal rather than trusting metadata.
    for pid, (expected, _) in enumerate(planes):
        face_edges = [(a, b) for a, b in edges if pid in support[a] and pid in support[b]]
        if not face_edges:
            continue  # redundant clipping planes need not create visible faces
        anchor = vertices[face_edges[0][0]]
        crosses = [np.cross(vertices[a]-anchor, vertices[b]-anchor) for a, b in face_edges]
        normal = max(crosses, key=lambda n: np.dot(n, n))
        normal /= np.linalg.norm(normal)
        if np.dot(normal, anchor) < 0:
            normal = -normal
        np.testing.assert_allclose(normal, expected, atol=1e-6)
    print("Dodecahedron retains crown and pavilion; signed-zero culet OK")


if __name__ == "__main__":
    main()
