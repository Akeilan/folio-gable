#pragma once
#include "math/Vec.h"
#include <vector>

namespace gable {

struct TessResult {
    std::vector<Vec2> pts;   // input points (concatenated loops) followed by any combine points
    std::vector<int> tris;   // triangle indices into pts
};

// Triangulates polygon loops (outer + holes) using the odd winding rule.
bool tessellate(const std::vector<std::vector<Vec2>>& loops, TessResult& out);

}  // namespace gable
