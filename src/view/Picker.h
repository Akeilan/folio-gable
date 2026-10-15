#pragma once
#include "ui/Document.h"
#include "view/Camera.h"
#include <set>

namespace gable {

struct PickHit {
    EntKind kind = EntKind::None;
    int id = -1;          // entity id in its own Entities
    InstPath path;        // instance path from root to the Entities containing the entity
    Mat4 xf;              // local -> world
    Vec3 point;           // world point
    double depth = 1e300; // distance along view direction
    double screenDist = 1e300;
    int def = -1;         // definition index of containing Entities (-1 root)
    bool valid() const { return kind != EntKind::None; }
};

class Picker {
public:
    Picker(Document* d, Camera* c) : doc(d), cam(c) {}
    Document* doc;
    Camera* cam;
    bool xray = false;
    // Entities of the active context excluded from picking (being moved)
    std::set<int> exVerts, exInsts;
    bool excludeActiveFaces = false;

    struct DeepResult {
        PickHit vertex, edge, face, guide, guidePoint, instanceFace;
        std::vector<PickHit> edges;   // all edges within tolerance (nearest first)
        std::vector<PickHit> verts;   // all visible vertices within tolerance (nearest first)
    };
    // Picks everything visible (inside instances too).
    DeepResult deep(double x, double y, double vtxTol = 9, double edgeTol = 6) const;

    // Select-tool pick in the active context: returns face/edge/instance/guide/dim/text ref.
    EntRef pickActive(double x, double y, PickHit* hitOut = nullptr) const;

    std::vector<EntRef> pickBox(double x0, double y0, double x1, double y1, bool crossing) const;

    // Returns the screen distance from (x,y) to segment a-b (world); t = param of closest point
    double screenDistSeg(const Vec3& a, const Vec3& b, double x, double y, double* t = nullptr) const;
    bool faceHit(const Entities& E, int f, const Ray& localRay, double& tOut) const;

private:
    void walk(int def, const Entities& E, const Mat4& xf, const InstPath& path, const Ray& ray, double x, double y,
              double vtol, double etol, DeepResult& r, int depth) const;
};

}  // namespace gable
