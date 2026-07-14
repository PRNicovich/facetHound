# -*- coding: utf-8 -*-
"""
GemViewer - interactive gem CAD visualizer (refactored)

Includes:
- interactive rotate (index axis)
- interactive rock (girdle axis)
- facet selection and highlighting
- view normal to facet
"""

import numpy as np
import pathlib
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

import gemLoader
import cdd


def polar2cart(r, theta, phi):
    x = r * np.sin(theta) * np.cos(phi)
    y = r * np.sin(theta) * np.sin(phi)
    z = r * np.cos(theta)
    return np.vstack((x, y, z)).T


def facetsInCartesian(facetDictSet):
    roll = np.array([np.deg2rad(x['deg']) for x in facetDictSet['facets']])
    pitch = np.deg2rad(facetDictSet['angle']) * np.ones(len(roll))
    rho = np.ones(len(roll)) * facetDictSet['depth']

    if not facetDictSet['isCrown']:
        rho = -rho
        if len(pitch) > 0 and pitch[0] == 0:
            rho = -rho

    return polar2cart(rho, pitch, roll)


def convertRepresentations(halfspaces):
    mat = cdd.matrix_from_array(halfspaces, rep_type=cdd.RepType.INEQUALITY)
    poly = cdd.polyhedron_from_matrix(mat)
    ext = cdd.copy_generators(poly)
    points = -np.array(ext.array)[:, 1:]
    return points


def coplanarPoints(points, facet):
    fc = np.tile(facet, (points.shape[0], 1))
    v = (points[:, 0] * fc[:, 1] +
         points[:, 1] * fc[:, 2] +
         points[:, 2] * fc[:, 3] -
         fc[:, 0])
    return np.where(np.abs(v) < 1e-6)[0]


def sort_points_clockwise(points, normal):
    n = normal / np.linalg.norm(normal)

    u = np.cross(n, [1, 0, 0])
    if np.linalg.norm(u) < 1e-6:
        u = np.cross(n, [0, 1, 0])
    u /= np.linalg.norm(u)
    v = np.cross(n, u)

    center = np.mean(points, axis=0)

    ang = []
    for p in points:
        vec = p - center
        x = np.dot(vec, u)
        y = np.dot(vec, v)
        ang.append((np.arctan2(y, x), p))

    ang.sort(key=lambda x: -x[0])
    return np.array([p for _, p in ang])


def setPointsClockwise(gemDict, points):
    for f in gemDict['facetList']:
        if f['nFacets'] == 0:
            continue
        for fz in f['facets']:
            idx = coplanarPoints(points, fz['coefficients'])
            fz['corners'] = idx
            pts = points[idx]
            fz['points'] = sort_points_clockwise(
                pts,
                np.array(fz['coefficients'][1:])
            )
    return gemDict


class GemViewer:
    def __init__(self, gemDict):
        self.gem = gemDict
        self.fig = plt.figure()
        self.ax = self.fig.add_subplot(111, projection='3d')

        self.az = 0.0
        self.el = 20.0

        self.rot_speed = 0.0
        self.rock_speed = 0.0

        self.selected = (0, 0)

        self.fig.canvas.mpl_connect('key_press_event', self.on_key)

        self.timer = self.fig.canvas.new_timer(interval=30)
        self.timer.add_callback(self.update)
        self.timer.start()

        self.draw()

    def on_key(self, event):
        if event.key == 'left':
            self.rot_speed = 2.0
        elif event.key == 'right':
            self.rot_speed = -2.0
        elif event.key == 'up':
            self.rock_speed = 1.5
        elif event.key == 'down':
            self.rock_speed = -1.5
        elif event.key == ' ':
            self.rot_speed = 0
            self.rock_speed = 0
        elif event.key == 'n':
            self.next_facet()
        elif event.key == 'p':
            self.prev_facet()

    def update(self):
        self.az += self.rot_speed
        self.el += self.rock_speed
        self.draw()

    def draw(self):
        self.ax.cla()

        for ti, tier in enumerate(self.gem['facetList']):
            if tier['nFacets'] == 0:
                continue

            for fi, fac in enumerate(tier['facets']):
                color = 'yellow' if (ti, fi) == self.selected else 'white'
                self.ax.add_collection3d(
                    Poly3DCollection(
                        [fac['points']],
                        facecolor=color,
                        edgecolor='black',
                        linewidth=0.5,
                        alpha=0.9
                    )
                )

        self.ax.view_init(elev=self.el, azim=self.az)
        self.ax.set_box_aspect([1, 1, 1])
        plt.axis('off')
        self.fig.canvas.draw_idle()

    def view_facet(self, tier, facet):
        self.selected = (tier, facet)
        n = np.array(self.gem['facetList'][tier]['facets'][facet]['coefficients'][1:])
        n = n / np.linalg.norm(n)

        self.el = np.degrees(np.arcsin(n[2]))
        self.az = np.degrees(np.arctan2(n[1], n[0]))
        self.draw()

    def next_facet(self):
        t, f = self.selected
        f += 1
        if f >= len(self.gem['facetList'][t]['facets']):
            f = 0
        self.view_facet(t, f)

    def prev_facet(self):
        t, f = self.selected
        f -= 1
        if f < 0:
            f = len(self.gem['facetList'][t]['facets']) - 1
        self.view_facet(t, f)


if __name__ == "__main__":
    basePath = pathlib.Path('./data')
    fName = 'pc01391.asc'

    gemDict = gemLoader.loadGemCADFile(basePath / fName)

    for f in gemDict['facetList']:
        if f['nFacets'] == 0:
            continue
        carts = facetsInCartesian(f)
        carts = np.hstack((np.sum(carts**2, axis=1, keepdims=True), carts))
        for i, fx in enumerate(f['facets']):
            fx['coefficients'] = carts[i]

    halfspaces = np.vstack([
        [i['coefficients'] for i in f['facets']]
        for f in gemDict['facetList']
    ])

    points = convertRepresentations(halfspaces)
    gemDict['vertices'] = points
    gemDict = setPointsClockwise(gemDict, points)

    viewer = GemViewer(gemDict)
    plt.show()
