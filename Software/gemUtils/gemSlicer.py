# -*- coding: utf-8 -*-
"""
Created on Wed Nov 20 21:23:23 2024

@author: rusty
"""

import gemLoader
import pathlib
import numpy as np
import cdd
from matplotlib import pyplot as plt
from scipy.spatial import ConvexHull
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

    
def plot_polyhedron(fig, gemDict, vertcolor = 'none', facecolor = 'white', edgecolor = 'black', 
                    titleString = ''):
    """
    Visualize the polyhedron using matplotlib.
    """
    fig.clf()
    ax = fig.add_subplot(111, projection='3d')
    
    # Plot the faces
    for f in gemDict['facetList']:
        if (f['nFacets'] > 0):
            for fz in f['facets']:
                verts = [tuple(fz['points'])]
                
                #centroid = np.mean(fz['points'], axis = 0)
                #ax.text(centroid[0], centroid[1], centroid[2], fz['name'])
                
                ax.add_collection3d(Poly3DCollection(verts, alpha = 0.8, facecolor = facecolor, edgecolor = edgecolor, linewidth = 0.5))
    

        
    plt.axis('off')
    plt.axis('equal')
    plt.title(titleString)
    plt.show()


def facetsInCartesian(facetDictSet):
    
    # Facet dict has pitch, depth, list of roll values
    # For each in list of rolls, want normal vector and point
    # Are both of these not just the vector defined in spherical coordinates? 
    # (pitch, roll, depth) = (phi, psi, rho) -> (x, y, z)
    
    # Aggregate roll list into an array, in radians
    roll = np.array([np.deg2rad(x['deg']) for x in facetDictSet['facets']])
    pitch = np.deg2rad(facetDictSet['angle'])*np.ones((len(roll)))
    rho = np.ones((len(roll)))*facetDictSet['depth']
    
    if not(facetDictSet['isCrown']):
        rho = -rho
        if pitch[0] == 0:
            pitch = 0
            rho = -rho            
        else:
            pitch = pitch
            
    coord = polar2cart(rho, pitch, roll)

    return coord
    
def polar2cart(r, theta, phi):
    
    x = r * np.sin(theta) * np.cos(phi)
    y = r * np.sin(theta) * np.sin(phi)
    z = r * np.cos(theta)
    
    outStack = np.vstack((x, y, z)).T
    
    return outStack


def convertRepresentations(halfspaces):
    
    mat = cdd.matrix_from_array(halfspaces, rep_type=cdd.RepType.INEQUALITY)
    poly = cdd.polyhedron_from_matrix(mat)
    ext = cdd.copy_generators(poly)
    points = -np.array(ext.array)[:,1:]
    adj = cdd.copy_adjacency(poly)
    
    return points, adj


def coplanarPoints(points, facet):
    
    fc = np.tile(facet, (points.shape[0], 1))
    
    v = points[:,0]*fc[:,1] + points[:,1]*fc[:,2] + points[:,2]*fc[:,3] - fc[:,0]
    
    ptsBack = np.where(np.abs(v) < 1e-6)
    
    return ptsBack


def project_points_onto_plane(points, plane_point, plane_normal):
    plane_normal = plane_normal / np.linalg.norm(plane_normal)
    projected_points = []
    for p in points:
        v = p - plane_point
        distance = np.dot(v, plane_normal)
        projected = p - distance * plane_normal
        projected_points.append(projected)
    return np.array(projected_points)

def sort_points_clockwise(points, plane_normal):
    
    projected_points = project_points_onto_plane(points, plane_normal, plane_normal)
    
    # Compute a local basis (u, v) on the plane
    plane_normal = plane_normal / np.linalg.norm(plane_normal)
    u = np.array([1, 0, 0]) if not np.allclose(plane_normal, [1, 0, 0]) else np.array([0, 1, 0])
    u = np.cross(plane_normal, u)
    u = u / np.linalg.norm(u)
    v = np.cross(plane_normal, u)

    # Choose a center point to measure angles from
    center = np.mean(projected_points, axis=0)
    
    # Convert 3D points to 2D in the plane's coordinate system
    points_2d = []
    for p in projected_points:
        vec = p - center
        x = np.dot(vec, u)
        y = np.dot(vec, v)
        angle = np.arctan2(y, x)
        points_2d.append((angle, p))

    # Sort by angle in clockwise order (reverse=False for counter-clockwise)
    points_sorted = [p for _, p in sorted(points_2d, key=lambda x: -x[0])]
    return np.array(points_sorted)


if __name__ == "__main__":
    

    basePath = pathlib.Path(r'./data')
    fName = 'pc01391.asc'
    
    girdleDepth = None

    gemDict = gemLoader.loadGemCADFile(basePath / pathlib.Path(fName))
    
    for f in gemDict['facetList']:
        if (f['nFacets'] > 0):
            
            carts = np.hstack((np.atleast_2d(np.sum(facetsInCartesian(f)**2, axis = 1)).T, facetsInCartesian(f)))
            
            for fi, fz in enumerate(f['facets']):
                fz['coefficients'] = carts[fi,:]


    halfspaces = np.vstack([[i['coefficients'] for i in f['facets']] for f in gemDict['facetList']])

    points, adj = convertRepresentations(halfspaces)
    
    gemDict['vertices'] = points
    

    for f in gemDict['facetList']:
        if (f['nFacets'] > 0):
            for fi, fz in enumerate(f['facets']):
                fz['corners'] = coplanarPoints(points, fz['coefficients'])
                fz['points'] = sort_points_clockwise(np.squeeze(points[fz['corners'], :]), 
                                                     np.atleast_2d(fz['coefficients'])[0,1:])

    #points = np.vstack(create_polyhedron_from_planes(facetPoints, facetPoints))
    #points = np.unique(np.round(points, decimals = 4), axis = 0)
    hull = ConvexHull(points, qhull_options = 'Qc')

    # Plot the polyhedron
    fig = plt.figure(1)
    plot_polyhedron(fig, gemDict, titleString = gemDict['boldTitle'])
    
    #fig.gca().scatter(halfspaces[:,1], halfspaces[:,2], halfspaces[:,3], '.')


