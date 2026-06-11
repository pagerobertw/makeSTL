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
#include <cctype>
#include <iomanip>

struct Point { float x, y, z; };
struct Triangle { Point normal; Point vertices[3]; };

struct stlConfig {
    // Identity
    std::string place;
    std::string boundsFile;
    std::string elevationFile;

    // From bounds file
    double north = 0, south = 0, west = 0, east = 0;

    // User-set model parameters
    double woodNorthSouth = 4.0;    // inches, N-S dimension of wood blank
    double verticalScale  = 2.0;    // Z exaggeration
    double baseThickness  = 0.625;  // inches of solid material below terrain

    // Grid dimensions — must match the elevation file
    int numCols = 0, numRows = 0;

    // Computed geography
    double deltaLat = 0, deltaLon = 0, midLat = 0;
    double milesNorthSouth = 0, milesEastWest = 0;
    double ratio = 0;
    double woodEastWest = 0;
    double inchesPerMile = 0;

    // Computed Z scaling (inches of model height per meter of real elevation)
    double zScale_inchesPerMeter = 0;

    // Populated after reading the elevation file
    float  minElevation = 0, maxElevation = 0;
    double deltaZ_meters = 0, deltaZ_inches = 0;

};

static std::string toLower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

void calculateNormal(const Point& a, const Point& b, const Point& c, Point& normal) {
    float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    normal.x = uy * vz - uz * vy;
    normal.y = uz * vx - ux * vz;
    normal.z = ux * vy - uy * vx;
    float len = std::sqrt(normal.x*normal.x + normal.y*normal.y + normal.z*normal.z);
    if (len > 0.0f) { normal.x /= len; normal.y /= len; normal.z /= len; }
}

void writeSTL(const std::vector<Triangle>& triangles, const std::string& filename) {
    std::ofstream file(filename, std::ios::binary);
    char hdr[80] = {};
    file.write(hdr, sizeof(hdr));
    uint32_t n = (uint32_t)triangles.size();
    file.write(reinterpret_cast<const char*>(&n), sizeof(n));
    for (const auto& tri : triangles) {
        file.write(reinterpret_cast<const char*>(&tri.normal), sizeof(Point));
        for (const auto& v : tri.vertices)
            file.write(reinterpret_cast<const char*>(&v), sizeof(Point));
        uint16_t attr = 0;
        file.write(reinterpret_cast<const char*>(&attr), sizeof(attr));
    }
}

// Read the MapBoundsPicker output file: "Place:", "North:", "South:", "West:", "East:".
bool loadBoundsFile(const std::string& filename, stlConfig& cfg) {
    std::ifstream f(filename);
    if (!f.is_open()) {
        std::cerr << "Error: cannot open bounds file: " << filename << "\n";
        return false;
    }
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string key;
        if (!(ss >> key)) continue;
        if (!key.empty() && key.back() == ':') key.pop_back();
        std::string lk = toLower(key);
        if      (lk == "place") { ss >> cfg.place; }
        else if (lk == "north") { ss >> cfg.north; }
        else if (lk == "south") { ss >> cfg.south; }
        else if (lk == "west")  { ss >> cfg.west;  }
        else if (lk == "east")  { ss >> cfg.east;  }
    }
    if (cfg.place.empty() || cfg.north == cfg.south) {
        std::cerr << "Error: bounds file missing required fields\n";
        return false;
    }
    return true;
}

// Derive all model dimensions from bounds and user parameters.
void computeGeometry(stlConfig& cfg) {
    cfg.deltaLat = cfg.north - cfg.south;
    cfg.deltaLon = cfg.east  - cfg.west;
    cfg.midLat   = (cfg.north + cfg.south) / 2.0;

    cfg.milesNorthSouth = cfg.deltaLat * 69.0468;
    cfg.milesEastWest   = cfg.deltaLon * 69.0468 * std::cos(cfg.midLat * M_PI / 180.0);
    cfg.ratio           = cfg.milesEastWest / cfg.milesNorthSouth;

    // Use whichever wood dimension the user provided; compute the other from the ratio.
    if (cfg.woodEastWest != 0.0) {
        cfg.woodNorthSouth = cfg.woodEastWest / cfg.ratio;
    } else {
        cfg.woodEastWest = cfg.woodNorthSouth * cfg.ratio;
    }
    cfg.inchesPerMile = cfg.woodNorthSouth / cfg.milesNorthSouth;

    cfg.zScale_inchesPerMeter = (cfg.inchesPerMile / 1609.34) * cfg.verticalScale;
}

// Read numRows x numCols elevation values (meters) from a headerless .asc file.
bool loadASC(const std::string& filename, stlConfig& cfg,
             std::vector<std::vector<float>>& grid) {
    std::ifstream f(filename);
    if (!f.is_open()) {
        std::cerr << "Error: cannot open elevation file: " << filename << "\n";
        return false;
    }

    grid.assign(cfg.numRows, std::vector<float>(cfg.numCols, 0.0f));
    for (int row = 0; row < cfg.numRows; ++row) {
        for (int col = 0; col < cfg.numCols; ++col) {
            if (!(f >> grid[row][col])) {
                std::cerr << "Error: unexpected EOF at row " << row
                          << ", col " << col << "\n";
                return false;
            }
        }
    }

    cfg.minElevation = grid[0][0];
    cfg.maxElevation = grid[0][0];
    for (const auto& row : grid)
        for (float v : row) {
            if (v < cfg.minElevation) cfg.minElevation = v;
            if (v > cfg.maxElevation) cfg.maxElevation = v;
        }
    cfg.deltaZ_meters = (double)(cfg.maxElevation - cfg.minElevation);
    cfg.deltaZ_inches = cfg.deltaZ_meters * cfg.zScale_inchesPerMeter;

    return true;
}

void printConfigTo(std::ostream& out, const stlConfig& cfg, bool elevationLoaded, bool setupSheet = false) {
    out << "\n=== " << cfg.place << " ===\n";
    if (setupSheet) {
        out << "\n";
        out << "Date:             \n";
        out << "Customer:         \n";
        out << "Wood species:     \n";
        out << "Data source:      \n";
        out << "Shore Allowance:  \n";
        out << "Max cut depth:    \n";
        out << "\n";
    }
    out << "Bounds:        N " << cfg.north << "  S " << cfg.south
        << "  W " << cfg.west  << "  E " << cfg.east << "\n";
    out << "NS:            " << cfg.milesNorthSouth << " mi\n";
    out << "EW:            " << cfg.milesEastWest   << " mi\n";
    out << "Ratio (EW/NS): " << cfg.ratio           << "\n";
    out << "Wood:          " << cfg.woodNorthSouth  << "\" NS  x  "
        << cfg.woodEastWest << "\" EW\n";
    out << "in/mile:       " << cfg.inchesPerMile   << "\n";
    out << "verticalScale: " << cfg.verticalScale   << "\n";
    out << "baseThickness: " << cfg.baseThickness   << "\"\n";
    if (cfg.numCols > 0 && cfg.numRows > 0)
        out << "Grid:          " << cfg.numCols << " cols x " << cfg.numRows << " rows\n";
    else
        out << "Grid:          (not set)\n";
    if (elevationLoaded) {
        out << "Total height:  " << cfg.baseThickness + cfg.deltaZ_inches << "\"\n";
        out << "Elevation:     " << cfg.minElevation << " to " << cfg.maxElevation
            << " m  (delta = " << cfg.deltaZ_meters << " m = "
            << cfg.deltaZ_inches << "\" on model)\n";
    }
    out << "\n";
}

void printConfig(const stlConfig& cfg, bool elevationLoaded) {
    printConfigTo(std::cout, cfg, elevationLoaded);
}

void writeSetupSheet(const stlConfig& cfg) {
    std::string filename = cfg.place + "SetupSheet.txt";
    std::ofstream f(filename);
    if (!f.is_open()) {
        std::cerr << "Warning: could not write " << filename << "\n";
        return;
    }
    printConfigTo(f, cfg, true, true);
    std::cout << "Wrote " << filename << "\n";
}

// Tessellate the elevation grid into triangles.
// x = east-west (inches), y = north-south (inches), z = elevation (inches).
std::vector<Triangle> convertGridToSTL(const std::vector<std::vector<float>>& grid,
                                        const stlConfig& cfg) {
    int   nrows  = cfg.numRows;
    int   ncols  = cfg.numCols;
    float xScale = (float)(cfg.woodEastWest   / (ncols - 1));
    float yScale = (float)(cfg.woodNorthSouth  / (nrows - 1));
    float zScale = (float) cfg.zScale_inchesPerMeter;
    float minZ   = cfg.minElevation;

    float base = (float)cfg.baseThickness;
    auto pt = [&](int row, int col) -> Point {
        return { col * xScale, row * yScale, (grid[row][col] - minZ) * zScale + base };
    };

    std::vector<Triangle> triangles;
    triangles.reserve(2 * (nrows - 1) * (ncols - 1));

    for (int i = 0; i < nrows - 1; ++i) {
        for (int j = 0; j < ncols - 1; ++j) {
            Triangle t1, t2;
            t1.vertices[0] = pt(i,   j  );
            t1.vertices[1] = pt(i,   j+1);
            t1.vertices[2] = pt(i+1, j  );
            calculateNormal(t1.vertices[0], t1.vertices[1], t1.vertices[2], t1.normal);

            t2.vertices[0] = pt(i,   j+1);
            t2.vertices[1] = pt(i+1, j+1);
            t2.vertices[2] = pt(i+1, j  );
            calculateNormal(t2.vertices[0], t2.vertices[1], t2.vertices[2], t2.normal);

            triangles.push_back(t1);
            triangles.push_back(t2);
        }
    }
    return triangles;
}

// Append four side walls and a flat base to close the terrain surface into a solid.
// Base is at z=0; terrain surface starts at z=baseThickness.
// Each wall uses a fixed outward normal to avoid degenerate cross-products at
// zero-height edge points.
void addSidesAndBase(const std::vector<std::vector<float>>& grid,
                     const stlConfig& cfg,
                     std::vector<Triangle>& tris) {
    int   nrows  = cfg.numRows;
    int   ncols  = cfg.numCols;
    float xScale = (float)(cfg.woodEastWest   / (ncols - 1));
    float yScale = (float)(cfg.woodNorthSouth  / (nrows - 1));
    float zScale = (float) cfg.zScale_inchesPerMeter;
    float minZ   = cfg.minElevation;
    float base   = (float)cfg.baseThickness;
    float xMax   = (ncols - 1) * xScale;
    float yMax   = (nrows - 1) * yScale;

    auto elev = [&](int row, int col) -> float {
        return (grid[row][col] - minZ) * zScale + base;
    };

    // Helper: push two triangles that form a quad, with a fixed normal.
    auto quad = [&](Point a, Point b, Point c, Point d, Point n) {
        tris.push_back({n, {a, b, c}});
        tris.push_back({n, {a, c, d}});
    };

    // South wall  (y=0, row=0, outward normal = -y)
    {
        Point n{0, -1, 0};
        for (int j = 0; j < ncols-1; ++j) {
            float x0 = j*xScale, x1 = (j+1)*xScale;
            float z0 = elev(0,j), z1 = elev(0,j+1);
            quad({x0,0,0}, {x1,0,0}, {x1,0,z1}, {x0,0,z0}, n);
        }
    }

    // North wall  (y=yMax, row=nrows-1, outward normal = +y)
    {
        Point n{0, 1, 0};
        for (int j = 0; j < ncols-1; ++j) {
            float x0 = j*xScale, x1 = (j+1)*xScale;
            float z0 = elev(nrows-1,j), z1 = elev(nrows-1,j+1);
            quad({x1,yMax,0}, {x0,yMax,0}, {x0,yMax,z0}, {x1,yMax,z1}, n);
        }
    }

    // West wall  (x=0, col=0, outward normal = -x)
    {
        Point n{-1, 0, 0};
        for (int i = 0; i < nrows-1; ++i) {
            float y0 = i*yScale, y1 = (i+1)*yScale;
            float z0 = elev(i,0), z1 = elev(i+1,0);
            quad({0,y1,0}, {0,y0,0}, {0,y0,z0}, {0,y1,z1}, n);
        }
    }

    // East wall  (x=xMax, col=ncols-1, outward normal = +x)
    {
        Point n{1, 0, 0};
        for (int i = 0; i < nrows-1; ++i) {
            float y0 = i*yScale, y1 = (i+1)*yScale;
            float z0 = elev(i,ncols-1), z1 = elev(i+1,ncols-1);
            quad({xMax,y0,0}, {xMax,y1,0}, {xMax,y1,z1}, {xMax,y0,z0}, n);
        }
    }

    // Base  (z=0, outward normal = -z)
    {
        Point n{0, 0, -1};
        tris.push_back({n, {{0,0,0}, {0,yMax,0}, {xMax,yMax,0}}});
        tris.push_back({n, {{0,0,0}, {xMax,yMax,0}, {xMax,0,0}}});
    }
}

static const std::string CFG_FILE = "makeSTL.cfg";

// Rewrite makeSTL.cfg with all currently known values.
// Called after every run so the user has a fully populated file to edit.
void writeConfig(const stlConfig& cfg) {
    std::ofstream f(CFG_FILE);
    if (!f.is_open()) {
        std::cerr << "Warning: could not write " << CFG_FILE << "\n";
        return;
    }
    f << std::fixed << std::setprecision(4);
    f << "# makeSTL.cfg  —  edit and run makeSTL.exe\n\n";
    f << "BoundsFile     = " << cfg.boundsFile << "\n";
    f << "ElevationFile  = " << cfg.elevationFile << "   # headerless .asc file\n";
    f << "\n";
    f << "WoodNorthSouth = " << cfg.woodNorthSouth
      << "   # N-S wood dimension (inches); set one of NS/EW, leave the other blank\n";
    if (cfg.woodEastWest == 0.0)
        f << "WoodEastWest   =   "
          << "   # E-W wood dimension (inches); if both set, EW takes precedence\n";
    else
        f << "WoodEastWest   = " << cfg.woodEastWest
          << "   # E-W wood dimension (inches); if both set, EW takes precedence\n";
    f << "VerticalScale  = " << cfg.verticalScale
      << "   # Z exaggeration (1.0 = true scale)\n";
    f << "BaseThickness  = " << cfg.baseThickness
      << "   # inches of solid material below terrain (0.50 to 0.75 typical)\n";
    f << "\n";
    if (cfg.numCols > 0)
        f << "NumCols        = " << cfg.numCols << "   # columns in the .asc file\n";
    else
        f << "NumCols        =    # columns in the .asc file\n";
    if (cfg.numRows > 0)
        f << "NumRows        = " << cfg.numRows << "   # rows in the .asc file\n";
    else
        f << "NumRows        =    # rows in the .asc file\n";
}

// Read makeSTL.cfg; override only the keys that are present.
bool loadConfig(stlConfig& cfg) {
    std::ifstream f(CFG_FILE);
    if (!f.is_open()) return false;

    std::string line;
    while (std::getline(f, line)) {
        // strip inline comment
        auto h = line.find('#');
        if (h != std::string::npos) line = line.substr(0, h);

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        auto trim = [](const std::string& s) {
            size_t a = s.find_first_not_of(" \t");
            size_t b = s.find_last_not_of(" \t");
            return (a == std::string::npos) ? std::string{} : s.substr(a, b - a + 1);
        };

        std::string key = toLower(trim(line.substr(0, eq)));
        std::string val =         trim(line.substr(eq + 1));
        if (val.empty()) continue;

        if      (key == "boundsfile")     cfg.boundsFile     = val;
        else if (key == "elevationfile")  cfg.elevationFile  = val;
        else if (key == "woodnorthsouth") cfg.woodNorthSouth = std::stod(val);
        else if (key == "woodeastwest")   cfg.woodEastWest   = std::stod(val);
        else if (key == "verticalscale")  cfg.verticalScale  = std::stod(val);
        else if (key == "numcols")        cfg.numCols        = std::stoi(val);
        else if (key == "numrows")        cfg.numRows        = std::stoi(val);
        else if (key == "basethickness")  cfg.baseThickness  = std::stod(val);
    }
    return true;
}

int main(int argc, char* argv[]) {

    stlConfig cfg;

    // Factory defaults — overridden by makeSTL.cfg when it exists.
    // ElevationFile, NumCols, NumRows intentionally left blank/zero: they must
    // be set explicitly in the cfg before a full run is attempted.
    cfg.boundsFile     = "CopperMountain.txt";
    cfg.elevationFile  = "";
    cfg.woodNorthSouth = 4.00;
    cfg.verticalScale  = 1.00;
    cfg.baseThickness  = 0.625;
    cfg.numCols        = 0;
    cfg.numRows        = 0;

    if (!loadConfig(cfg)) {
        writeConfig(cfg);
        std::cout << CFG_FILE << " not found — template written.\n"
                  << "Edit " << CFG_FILE << " to set BoundsFile, ElevationFile, and parameters.\n";
        return 0;
    }

    if (argc > 1) cfg.elevationFile = argv[1];

    if (!loadBoundsFile(cfg.boundsFile, cfg)) return 1;
    computeGeometry(cfg);
    writeConfig(cfg);   // update cfg with all computed/default values

    // Determine whether we have everything needed to generate the STL.
    bool elevationReady = !cfg.elevationFile.empty() && cfg.numCols > 0 && cfg.numRows > 0;

    std::vector<std::vector<float>> grid;
    bool elevationLoaded = false;

    if (elevationReady) {
        elevationLoaded = loadASC(cfg.elevationFile, cfg, grid);
        if (!elevationLoaded)
            std::cout << "Printing partial configuration without elevation data.\n";
    } else {
        if (cfg.elevationFile.empty())
            std::cout << "ElevationFile not set — printing partial configuration.\n";
        else
            std::cout << "NumCols/NumRows not set — printing partial configuration.\n";
    }

    printConfig(cfg, elevationLoaded);

    if (!elevationLoaded) return 0;

    // .asc row 0 is the northernmost row; reverse so y=0 = south.
    std::reverse(grid.begin(), grid.end());

    auto triangles = convertGridToSTL(grid, cfg);
    addSidesAndBase(grid, cfg, triangles);

    std::string outputFile = cfg.place + ".stl";
    writeSTL(triangles, outputFile);
    std::cout << "Wrote " << outputFile << " (" << triangles.size() << " triangles)\n";

    writeSetupSheet(cfg);
    return 0;
}
