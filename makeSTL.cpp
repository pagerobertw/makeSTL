// makeSTL.cpp

#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <cstdint>

struct Point {
    float x, y, z;
};

struct Triangle {
    Point normal;
    Point vertices[3];
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
    file << "STL\0"; // STL format header
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

std::vector<Triangle> convertGridToSTL(const std::vector<std::vector<float>>& grid) {
    std::vector<Triangle> triangles;
    // Assuming grid is a 2D vector of elevation values
    for (size_t i = 0; i < grid.size() - 1; ++i) {
        for (size_t j = 0; j < grid[i].size() - 1; ++j) {
            Triangle triangle1;
            Triangle triangle2;

            // Define the vertices of the triangles
            triangle1.vertices[0] = {static_cast<float>(i), static_cast<float>(j), grid[i][j]};
            triangle1.vertices[1] = {static_cast<float>(i+1), static_cast<float>(j), grid[i+1][j]};
            triangle1.vertices[2] = {static_cast<float>(i), static_cast<float>(j+1), grid[i][j+1]};
            calculateNormal(triangle1.vertices[0], triangle1.vertices[1], triangle1.vertices[2], triangle1.normal);

            triangle2.vertices[0] = triangle1.vertices[1];
            triangle2.vertices[1] = triangle1.vertices[2];
            triangle2.vertices[2] = {static_cast<float>(i+1), static_cast<float>(j+1), grid[i+1][j+1]};
            calculateNormal(triangle2.vertices[0], triangle2.vertices[1], triangle2.vertices[2], triangle2.normal);

            triangles.push_back(triangle1);
            triangles.push_back(triangle2);
        }
    }
    return triangles;
}

int main() {
    // Example usage
    std::vector<std::vector<float>> grid = {{0, 0, 0}, {0, 1, 0}, {0, 0, 0}}; // Replace with actual elevation data
    auto triangles = convertGridToSTL(grid);
    writeSTL(triangles, "output.stl");
    return 0;
}