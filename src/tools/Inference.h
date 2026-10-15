#pragma once
#include "view/Overlay.h"
#include "view/Picker.h"

namespace gable {

enum class InfType {
    None, Endpoint, Midpoint, OnEdge, OnFace, Center, Intersection, OnAxis, Parallel, Perpendicular,
    FromPoint, OnGuide, GuidePoint, Origin, Free
};

struct InfResult {
    InfType type = InfType::None;
    Vec3 p;                    // world point
    bool hasLine = false;      // dotted inference line from la to lb
    Vec3 la, lb;
    QColor lineColor;
    bool hasLine2 = false;     // secondary alignment line (from a tracked point)
    Vec3 l2a, l2b;
    QColor line2Color;
    PickHit hit;               // entity responsible (edge/face/vertex)
    bool onFace = false;
    Plane facePlane;           // world plane of face under cursor (valid if onFace)
    PickHit faceHit;
    Vec3 dir;                  // axis/edge direction for axis inferences
    int axis = -1;             // 0 red, 1 green, 2 blue
    QString tip;
    bool locked = false;
};

class Inference {
public:
    Inference(Document* d, Camera* c, Picker* p) : doc(d), cam(c), picker(p) {}
    Document* doc;
    Camera* cam;
    Picker* picker;

    bool hasAnchor = false;
    Vec3 anchor;
    bool enabled = true;          // Alt/Ctrl may disable inferencing for some tools
    bool allowAxes = true;
    bool snapLength = false;

    // Locks
    enum class Lock { None, Line, Plane };
    Lock lock = Lock::None;
    bool lockFromArrow = false;
    Vec3 lockPt, lockDir;
    Plane lockPlane;
    QColor lockColor;
    int lockAxis = -1;

    void setAnchor(const Vec3& p) { hasAnchor = true; anchor = p; }
    void clearAnchor() { hasAnchor = false; }
    void reset();
    void clearLock() { lock = Lock::None; lockFromArrow = false; lockAxis = -1; }
    // Shift: lock whatever the last result inferred.
    void lockFrom(const InfResult& r);
    // Arrow keys: 0 = red, 1 = green, 2 = blue, 3 = parallel to last edge
    void lockArrow(int which);

    InfResult compute(double x, double y);
    void feedback(Overlay& ov, const InfResult& r, double mx, double my) const;

    Vec3 axis(int i) const;
    static QColor axisColor(int i);
    static QString axisName(int i);

    // Remembered points for alignment inference
    std::vector<Vec3> tracked;
    std::vector<Vec3> centers;   // arc/circle centers seen while hovering curves
    bool hasRefEdge = false;
    Vec3 refA, refB;
    std::vector<int> excludeVerts;  // vertices (in active ctx) being moved, ignored for snapping
    bool excludeActiveSelection = false;

private:
    bool screenPt(const Vec3& p, double& sx, double& sy) const;
    void track(const Vec3& p);
    Vec3 freePoint(const Ray& r) const;
};

}  // namespace gable
