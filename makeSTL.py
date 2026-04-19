import numpy as np
from stl import mesh
import sys

# Load grid format ASCII file
def load_grid(filename):
    # Read the file and convert it into a 2D numpy array
    with open(filename, 'r') as f:
        grid = np.loadtxt(f)
    return grid

# Convert grid to STL
def grid_to_stl(grid, output_filename):
    num_rows, num_cols = grid.shape
    vertices = []
    faces = []

    for i in range(num_rows-1):
        for j in range(num_cols-1):
            # Define vertices of the two triangles that make up the quad
            v0 = [j, i, grid[i, j]]
            v1 = [j+1, i, grid[i, j+1]]
            v2 = [j, i+1, grid[i+1, j]]
            v3 = [j+1, i+1, grid[i+1, j+1]]
            vertices.extend([v0, v1, v2, v3])
            # Define two triangles
            index = len(vertices) - 4
            faces.append([index, index + 1, index + 2]) # Triangle 1
            faces.append([index + 1, index + 3, index + 2]) # Triangle 2

    vertices = np.array(vertices)
    faces = np.array(faces)

    # Create the mesh
    mesh_data = mesh.Mesh(np.zeros(faces.shape[0], dtype=mesh.Mesh.dtype))
    for i, f in enumerate(faces):
        for j in range(3):
            mesh_data.vectors[i][j] = vertices[f[j]]
    # Write the mesh to file "output.stl"
    mesh_data.save(output_filename)

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: python makeSTL.py <input_grid_file> <output_stl_file>")
        sys.exit(1)
    input_file = sys.argv[1]
    output_file = sys.argv[2]
    grid = load_grid(input_file)
    grid_to_stl(grid, output_file)
