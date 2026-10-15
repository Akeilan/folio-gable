#pragma once
#include "core/Model.h"

namespace gable {

// Stateless helpers operating on CameraState for a viewport of w x h pixels.
class Camera {
public:
    CameraState* s = nullptr;
    int w = 800, h = 600;
    double nearZ = 10, farZ = 1e6;

    void fitClip(const BBox& scene);
    Mat4 view() const { return Mat4::lookAt(s->eye, s->target, s->up); }
    Mat4 proj() const;
    Mat4 viewProj() const { return proj() * view(); }
    Vec3 forward() const { return (s->target - s->eye).normalized(); }
    Vec3 right() const;
    Vec3 upv() const { return right().cross(forward()); }
    double distance() const { return (s->target - s->eye).length(); }

    Ray ray(double x, double y) const;
    bool project(const Vec3& p, double& sx, double& sy, double* depth = nullptr) const;
    // World size of one pixel at point p.
    double pixelSize(const Vec3& p) const;

    void orbit(double dxPix, double dyPix, const Vec3& pivot);
    void pan(double dxPix, double dyPix, const Vec3& pivot);
    void zoomAt(double factor, double x, double y, const Vec3& pivotHint, bool havePivot);
    void dolly(double amount);
    void zoomExtents(const BBox& b);
    void zoomBox(double x0, double y0, double x1, double y1, const Vec3& depthPoint);
    void lookAround(double dxPix, double dyPix);
    void setView(const Vec3& dir, const Vec3& up, const BBox& b);
    void setFov(double deg);
};

}  // namespace gable
