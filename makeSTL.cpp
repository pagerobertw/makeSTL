// makeSTL.cpp
#define _USE_MATH_DEFINES

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

struct Point {
    float x, y, z;
};

struct Triangle {
    Point normal;
    Point vertices[3];
};

struct GridHeader {
    int ncols;
    int nrows;
    double xllcorner;
    double yllcorner;
    double cellsize;
    float nodata_value;
};

void calculateNormal(const Point& a, const Point& b, const Point& c, Point& normal) {
    float ux = b.x - a.x;
    float uy = b.y - a.y;
    float uz = b.z - a.z;
    float vx = c.x - a.x;
    float vy = c.y - a.y;
    float vz = c.z - a.z;

    normal.x = uy * vz - uz * vy;
    normal.y = uz * vx - ux * vz;
    normal.z = ux * vy - uy * vx;

    float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    normal.x /= length;
    normal.y /= length;
    normal.z /= length;
}

void writeSTL(const std::vector<Triangle>& triangles, const std::string& filename) {
    std::ofstream file(filename, std::ios::binary);
    char header[80] = {};
    file.write(header, sizeof(header));
    uint32_t numTriangles = triangles.size();
    file.write(reinterpret_cast<const char*>(&numTriangles), sizeof(numTriangles));

    for (const auto& triangle : triangles) {
        file.write(reinterpret_cast<const char*>(&triangle.normal), sizeof(Point));
        for (const auto& vertex : triangle.vertices) {
            file.write(reinterpret_cast<const char*>(&vertex), sizeof(Point));
        }
        uint16_t attrByteCount = 0;
        file.write(reinterpret_cast<const char*>(&attrByteCount), sizeof(attrByteCount));
    }
    file.close();
}

bool loadASC(const std::string& filename, GridHeader& header, std::vector<std::vector<float>>& grid) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: could not open file: " << filename << std::endl;
        return false;
    }

    // Parse the 6-line header
    std::string key;
    file >> key >> header.ncols;
    file >> key >> header.nrows;
    file >> key >> header.xllcorner;
    file >> key >> header.yllcorner;
    file >> key >> header.cellsize;
    file >> key >> header.nodata_value;

    std::cout << "Grid: " << header.ncols << " cols x " << header.nrows << " rows" << std::endl;
    std::cout << "NODATA value: " << header.nodata_value << std::endl;

    // Read elevation data row by row
    grid.resize(header.nrows, std::vector<float>(header.ncols));
    for (int row = 0; row < header.nrows; ++row) {
        for (int col = 0; col < header.ncols; ++col) {
            float val;
            file >> val;
            if (val == header.nodata_value) {
                std::cerr << "Error: NODATA value found at row " << row << ", col " << col << std::endl;
                return false;
            }
            grid[row][col] = val;
        }
    }

    return true;
}

std::vector<Triangle> convertGridToSTL(const std::vector<std::vector<float>>& grid,
                                        float modelWidth, float modelHeight,
                                        double cellsize, float vertExag = 1.0f) {
    std::vector<Triangle> triangles;
    int nrows = grid.size();
    int ncols = grid[0].size();
    float xScale = modelWidth  / (ncols - 1);
    float yScale = modelHeight / (nrows - 1);

    // cellsize is in decimal degrees; convert to meters at Rainier's latitude (~46.85N)
    // 1 degree latitude ~ 111320 m; 1 degree longitude ~ 111320 * cos(lat) m
    const double lat_rad = 46.85 * M_PI / 180.0;
    double metersPerDegLon = 111320.0 * std::cos(lat_rad);
    float realWidth = (ncols - 1) * (float)(cellsize * metersPerDegLon);
    float zScale = (modelWidth / realWidth) * vertExag;

    std::cout << "realWidth: " << realWidth << " m, zScale: " << zScale << " in/m" << std::endl;

    float minZ = grid[0][0];
    for (const auto& row : grid)
        for (float v : row)
            minZ = std::min(minZ, v);

    for (int i = 0; i < nrows - 1; ++i) {
        for (int j = 0; j < ncols - 1; ++j) {
            Triangle triangle1;
            Triangle triangle2;

            triangle1.vertices[0] = {j * xScale,       i * yScale,       (grid[i][j]     - minZ) * zScale};
            triangle1.vertices[1] = {(j+1) * xScale,   i * yScale,       (grid[i][j+1]   - minZ) * zScale};
            triangle1.vertices[2] = {j * xScale,       (i+1) * yScale,   (grid[i+1][j]   - minZ) * zScale};
            calculateNormal(triangle1.vertices[0], triangle1.vertices[1], triangle1.vertices[2], triangle1.normal);

            triangle2.vertices[0] = triangle1.vertices[1];
            triangle2.vertices[1] = {(j+1) * xScale,   (i+1) * yScale,   (grid[i+1][j+1] - minZ) * zScale};
            triangle2.vertices[2] = triangle1.vertices[2];
            calculateNormal(triangle2.vertices[0], triangle2.vertices[1], triangle2.vertices[2], triangle2.normal);

            triangles.push_back(triangle1);
            triangles.push_back(triangle2);
        }
    }
    return triangles;
}

int main(int argc, char* argv[]) {
    std::string inputFile = "RainierPeak.asc";
    if (argc > 1)
        inputFile = argv[1];

    GridHeader header;
    std::vector<std::vector<float>> grid;

    if (!loadASC(inputFile, header, grid))
        return 1;

    const float modelWidth  = 2.006f; // east-west, inches
    const float modelHeight = 2.000f; // north-south, inches
    auto triangles = convertGridToSTL(grid, modelWidth, modelHeight, header.cellsize);
    writeSTL(triangles, "output.stl");
    std::cout << "Wrote output.stl (" << triangles.size() << " triangles)" << std::endl;
    return 0;
}
