# -*- coding: utf-8 -*-
"""
Created on Wed Nov 20 21:23:23 2024

@author: rusty
"""

import gemLoader
import pathlib
import numpy as np
from matplotlib import pyplot as plt
from scipy.spatial import ConvexHull
from mpl_toolkits.mplot3d.art3d import Poly3DCollection


def plane_from_normal_and_point(normal, point):
    """
    Given a normal vector and a point on the plane, return the coefficients (a, b, c, d) of the plane equation:
    a*x + b*y + c*z = d
    """
    a, b, c = normal
    d = np.dot(normal, point)  # d = a*x0 + b*y0 + c*z0
    return a, b, c, d


def create_polyhedron_from_planes(points, normals):
    """
    Given points and normals (each n x 3 array), this function creates the convex polyhedron
    formed by the intersection of the planes defined by these points and normals.
    """
    # Generate the planes
    newPlanes = [plane_from_normal_and_point(normals[i], points[i]) for i in range(len(points))]

    points = planesToPoints(newPlanes)

    return points


def getPlaneIntersectionPoint(plane1, plane2, plane3):
    """
    Calculates the intersection point of three planes.

    Args:
        plane1: Tuple (a1, b1, c1, d1) representing the first plane (a1x + b1y + c1z = d1).
        plane2: Tuple (a2, b2, c2, d2) representing the second plane.
        plane3: Tuple (a3, b3, c3, d3) representing the third plane.

    Returns:
        A NumPy array [x, y, z] representing the intersection point, or None if no unique intersection exists.
    """
    # Create the coefficient matrix (A)
    A = np.array([plane1[:3], plane2[:3], plane3[:3]])

    # Create the constant vector (b)
    b = np.array([plane1[3], plane2[3], plane3[3]])

    try:
        # Solve the system of equations
        intersection_point = np.linalg.solve(A, b)
        return intersection_point
    except np.linalg.LinAlgError:
        # If the matrix is singular (no unique solution), return None
        return None



def pointInHull(planes, point):
    
    for plane in planes:
        dist = np.dot(point, plane[:-1]) - plane[-1]
        
        print(dist)
        
        if (dist > 0.001):
            return False
    
    return True



def planesToPoints(planes):
    pointCloud = []
    for p1 in planes:
        for p2 in planes:
            for p3 in planes:
                
                point = getPlaneIntersectionPoint(p1, p2, p3)
                
                if (not(point is None) and (pointInHull(planes, point))):
                    pointCloud.append(point)
                
    return pointCloud


    
def plot_polyhedron(vertices, faces):
    """
    Visualize the polyhedron using matplotlib.
    """
    fig = plt.figure()
    ax = fig.add_subplot(111, projection='3d')

    # Plot the vertices
    ax.scatter(vertices[:, 0], vertices[:, 1], vertices[:, 2], color='c', label='Vertices')

    # Plot the faces
    for face in faces:
        # Each face should be a list of vertices (3D points), which should be a 2D array of shape (n, 3)
        poly3d = [vertices[face]]  # Face is already a list of indices, extract the corresponding vertices
        ax.add_collection3d(Poly3DCollection(poly3d, facecolors='cyan', linewidths=1, edgecolors='r', alpha=.25))

    ax.set_xlabel('X')
    ax.set_ylabel('Y')
    ax.set_zlabel('Z')
    ax.set_title("Convex Polyhedron")
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
            pitch = -pitch
            
    coord = polar2cart(rho, pitch, roll)

    return coord
    
def polar2cart(r, theta, phi):
    
    x = r * np.sin(theta) * np.cos(phi)
    y = r * np.sin(theta) * np.sin(phi)
    z = r * np.cos(theta)
    
    outStack = np.vstack((x, y, z)).T
    
    return outStack

if __name__ == "__main__":
    
    # Example usage
    vertices = 2*np.array([[-1, -1, -1], 
                        [1, -1, -1], 
                        [1, 1, -1], 
                        [1, 1, 1], 
                        [-1, 1, 1],
                        [-1, -1, 1],
                        [-1, 1, -1]])
    
    hull = ConvexHull(vertices)

    
    basePath = pathlib.Path(r'./data')
    fName = 'pc42011.asc'
    
    
    gemDict = gemLoader.loadGemCADFile(basePath / pathlib.Path(fName))
    
    facetPointList = []
    for f in gemDict['facetList']:
        if (f['nFacets'] > 0):
            facetPointList.append(facetsInCartesian(f))
        
    facetPoints = np.vstack(facetPointList)
        
    points = np.vstack(create_polyhedron_from_planes(facetPoints, facetPoints))
    points = np.unique(np.round(points, decimals = 4), axis = 0)
    hull = ConvexHull(points)

    # Plot the polyhedron
    #plot_polyhedron(points, hull.equations)

    fig = plt.figure(figsize=(8, 8))
    ax = fig.add_subplot(111, projection='3d')
    
    gg = ax.scatter(points[:,0], points[:,1], points[:,2], c = 'blue')
    ss = ax.scatter(facetPoints[:,0], facetPoints[:,1], facetPoints[:,2], c = 'green')
    
 #   q = ax.quiver3D(0, 0, 0, facetPoints[:,0], facetPoints[:,1], facetPoints[:,2])
    
    
#    for hs in hull.simplices:
#        hs = np.append(hs, hs[0])  # Here we cycle back to the first coordinate
#        ax.plot(points[hs, 0], points[hs, 1], points[hs, 2], "r-")
    
    boundingBox = np.array([[1, 1, 1],
                            [1, -1, 1],
                            [-1, 1, 1],
                            [1, 1, -1],
                            [-1, -1, 1],
                            [1, -1, -1],
                            [-1, -1, -1]])
    
    b = ax.scatter(boundingBox[:,0], boundingBox[:,1], boundingBox[:,2], c = 'orange')
    
    o = ax.scatter(0,0,0, c = 'red')
    
    ax.set_box_aspect((1, 1, 1))
    
    
    plt.show()
    

    
    ax.set_xlabel('X')
    ax.set_ylabel('Y')
    ax.set_zlabel('Z')
    plt.show()