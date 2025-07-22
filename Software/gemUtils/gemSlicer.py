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

def intersect_planes(plane1, plane2):
    """
    Given two planes, find the line of intersection.
    Each plane is defined by (a, b, c, d), where the plane equation is: ax + by + cz = d
    Return the intersection point (if any).
    """
    a1, b1, c1, d1 = plane1
    a2, b2, c2, d2 = plane2

    # Solve the system of two linear equations (ax + by + cz = d)
    # We solve for the two variables x, y (we can use z=0 as a simplifying assumption)
    A = np.array([[a1, b1, c1], [a2, b2, c2]])
    B = np.array([d1, d2])

    # Check if A has full rank (i.e., the planes are not parallel)
    if np.linalg.matrix_rank(A) < 2:
        return None  # Planes are parallel, no intersection

    # Solving the system for x, y, z
    try:
        intersection_point = np.linalg.lstsq(A, B, rcond=None)[0]  # Least squares solution
    except np.linalg.LinAlgError:
        return None  # In case the solution fails, return None

    return intersection_point

def updateVertices(vertices, intersection, newPlane, oldPlane):
    # Given this info, remove vertices cut by intersection
    # add new vertices (intersection point)
    # how to get which vertex to remove?
    print(vertices)
    print(intersection)
    print(oldPlane)
    print(newPlane)
    
    # Regnerate faces for current polyhedron
    hull = ConvexHull(vertices)
    polyPlanes = hull.equations

    return polyPlanes, vertices

def create_polyhedron_from_planes(points, normals, vertices = []):
    """
    Given points and normals (each n x 3 array), this function creates the convex polyhedron
    formed by the intersection of the planes defined by these points and normals.
    """
    # Generate the planes
    newPlanes = [plane_from_normal_and_point(normals[i], points[i]) for i in range(len(points))]

    if (vertices is None):
        
        vertices = set()
    else:
        vertices = vertices
        
        hull = ConvexHull(vertices)
        oldPlanes = hull.equations

    # Check intersections between pairs of planes
    # Intersections between original solid and new facets, and new facets themselves, in order given
    # new facets are input from previous faces array
    # for each new plane, 
    #    for each old plane,
    #       check for intersection with new plane
    #       if not none:
    #          remove now-external vertices
    #          intersection points now vertices
    #    update ConvexHull faces calc'd from vertices, now old planes
    # return for next gemList['xxx'] bit
    
    
    # Generate face planes from vertices?
    # Qhull gives planes from ConvexHull?
    f = len(oldPlanes)
    j = 0
    
    for i in range(len(newPlanes)):
        while j < f:
            # intersect planes
            intersection = intersect_planes(newPlanes[i], oldPlanes[j])
            
            if intersection is not None:
                # Found an intersection!

                
                vertices = updateVertices(vertices, intersection, newPlanes[i], oldPlanes[j])
                
                vertices.add(tuple(intersection))  # Add the intersection point as a vertex
                # should update input vertices on each iteration
                # init vertices from input vertices array
                # able to index w/ faces array? and geometry?
                # remove verties of connectivity < 3?
                
            j += 1
                
                
                


    # Convert vertices to a list
    vertices = np.array(list(vertices))

    # Debug: Print the intersection points
    print("Intersection Points:")
    print(vertices)

    if len(vertices) < 4:
        print("Too few vertices to form a polyhedron.")
        return vertices, []

    # Use ConvexHull to find the convex polyhedron
    try:
        hull = ConvexHull(vertices)
        faces = hull.simplices
        return vertices, faces
    except Exception as e:
        print(f"Error in ConvexHull: {e}")
        return vertices, []
    
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

    
    basePath = pathlib.Path(r'C:\Users\rusty\Documents\CAD\faceting\diagrams')
    fName = 'pc01006.asc'
    
    
    gemDict = gemLoader.loadGemCADFile(basePath / pathlib.Path(fName))
    
    facetPointList = []
    for f in gemDict['facetList']:
        facetPointList.append(facetsInCartesian(f))
        vertices, edges = create_polyhedron_from_planes(facetsInCartesian(f), facetsInCartesian(f), vertices = vertices)
        
        
    facetPoints = np.vstack(facetPointList)
    
    # facetPoints = facetPoints / np.sqrt(np.amax(np.sum(facetPoints**2, axis = 1)))
    
    # facetPoints = 0.2*np.array([[1, 1, 1],
    #                         [1, -1, 1],
    #                         [-1, 1, 1],
    #                         [1, 1, -1],
    #                         [-1, -1, 1],
    #                         [1, -1, -1],
    #                         [-1, -1, -1],
    #                         [-1, 1, -1]])
    
    # # Create the polyhedron
    # vertices, edges = create_polyhedron_from_planes(facetPoints, facetPoints)

    print('2')

    # Plot the polyhedron
    plot_polyhedron(vertices, edges)

    fig = plt.figure(figsize=(8, 8))
    ax = fig.add_subplot(111, projection='3d')
    
    s = ax.scatter(facetPoints[:,0], facetPoints[:,1], facetPoints[:,2])
 #   q = ax.quiver3D(0, 0, 0, facetPoints[:,0], facetPoints[:,1], facetPoints[:,2])
    
    
    
    
    boundingBox = np.array([[1, 1, 1],
                            [1, -1, 1],
                            [-1, 1, 1],
                            [1, 1, -1],
                            [-1, -1, 1],
                            [1, -1, -1],
                            [-1, -1, -1]])
    
    b = ax.scatter(boundingBox[:,0], boundingBox[:,1], boundingBox[:,2], 'r.')
    
    o = ax.scatter(0,0,0,'r.')
    
    ax.set_box_aspect((1, 1, 1))
    
    
    plt.show()
    

    
    ax.set_xlabel('X')
    ax.set_ylabel('Y')
    ax.set_zlabel('Z')
    plt.show()