// Terrain ("sandbox") operations.
#pragma once
#include "core/Model.h"

namespace gable {
namespace sandbox {

// Grid of triangulated cells (soft/smooth diagonals) in the XY plane of the given frame.
void makeGrid(Entities& E, const Vec3& origin, const Vec3& xdir, const Vec3& ydir, double width, double depth, double cell);

// Delaunay triangulation (in XY) of 3D points. Returns number of triangles.
int tinFromPoints(Entities& E, std::vector<Vec3> pts);

// Moves vertices within radius of center (XY distance) vertically by dz with smooth falloff.
void smoove(Entities& E, const Vec3& center, double radius, double dz, Vec3 up = Vec3(0, 0, 1));
// Preview helper: per-vertex offsets for smoove.
std::vector<std::pair<int, double>> smooveWeights(const Entities& E, const Vec3& center, double radius, Vec3 up = Vec3(0, 0, 1));

// Swaps the diagonal shared by two triangles.
bool flipEdge(Entities& E, int e);

// Projects segments (target coordinates) vertically onto faces of E, adding the draped edges.
int drape(Entities& E, const std::vector<std::pair<Vec3, Vec3>>& segs);

// Splits each selected triangle into four (adds detail).
void subdivide(Entities& E, const std::vector<int>& faces);

}  // namespace sandbox
}  // namespace gable
