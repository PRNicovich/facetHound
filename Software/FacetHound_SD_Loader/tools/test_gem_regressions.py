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
        machine_indices=[]
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
                # Crown roll leaves its normal facing the viewer while putting
                # the culet below the crown rather than above it.
                if n[2] > 0.02:
                    np.testing.assert_allclose([-projected[0], -projected[1], projected[2]],
                                               [0, 0, 1], atol=1e-6)
                count += 1
                value=(facet["value"]+(wheel/2 if tier["angle"]<0 or tier["angle"]>90 else 0))%wheel
                machine_indices.append(value)
        print(f"{name}: {count} facet normals face viewer at target")
        unique=[]
        for value in machine_indices:
            if not any(abs((value-old+wheel/2)%wheel-wheel/2)<0.0001 for old in unique):
                unique.append(value)
        unique.sort()
        assert all(a<b for a,b in zip(unique,unique[1:]))
        assert all(any(abs((v-u+wheel/2)%wheel-wheel/2)<0.0001 for u in unique) for v in machine_indices)
        print(f"  Classic: {len(machine_indices)} cuts -> {len(unique)} unique index marks")
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
    with contextlib.redirect_stdout(io.StringIO()):
        points, supports, edges, planes, *_ = build_gem(str(SOFTWARE / "gemUtils/data/pc04188.asc"))
    for panel in range(4):
        uv=points[:, [0, 1] if panel<2 else [0, 2] if panel==2 else [1, 2]].copy()
        if panel==1:
            uv[:,1]*=-1
        center=(uv.min(axis=0)+uv.max(axis=0))/2
        scale=min(106/np.ptp(uv[:,0]),98/np.ptp(uv[:,1]))
        screen=(uv-center)*scale+[69,65]
        assert np.all(screen >= [8,8]) and np.all(screen <= [130,122])
    girdle_edges=0
    for a,b in edges:
        normals=[planes[p][0] for p in set(supports[a]) & set(supports[b])]
        if any(abs(n[2])<0.02 for n in normals):
            for top in (True,False):
                assert any(abs(n[2])<0.02 or (n[2]>0 if top else n[2]<0) for n in normals)
            girdle_edges+=1
    assert girdle_edges
    print(f"Backgammon: all four projections in bounds; {girdle_edges} girdle edges visible in T and B")


if __name__ == "__main__":
    main()
