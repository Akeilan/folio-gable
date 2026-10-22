#include "view/Camera.h"

namespace gable {

Vec3 Camera::right() const {
    Vec3 r = forward().cross(s->up);
    if (r.length() < 1e-9) r = forward().anyPerp();
    return r.normalized();
}

void Camera::fitClip(const BBox& scene) {
    double d = distance();
    double rad = scene.valid() ? std::max(scene.radius(), 1000.0) : 10000.0;
    Vec3 c = scene.valid() ? scene.center() : Vec3();
    double dc = (c - s->eye).length();
    farZ = std::max(dc + rad * 4, d * 4) + 100000;
    if (s->perspective) {
        // keep depth precision reasonable: near ~ 1/20000 of far, but not more than what is in front
        double depthToNearest = dc - rad * 2;
        nearZ = std::max({farZ / 50000.0, 1.0, std::min(depthToNearest * 0.5, d * 0.05)});
        if (nearZ > d * 0.5) nearZ = std::max(1.0, d * 0.05);
    } else {
        nearZ = -farZ;
    }
}

Mat4 Camera::proj() const {
    double aspect = h > 0 ? (double)w / h : 1;
    if (s->perspective) return Mat4::perspective(s->fov * kPi / 180, aspect, nearZ, farZ);
    double hh = s->orthoHeight * 0.5, ww = hh * aspect;
    return Mat4::ortho(-ww, ww, -hh, hh, -farZ, farZ);
}

Ray Camera::ray(double x, double y) const {
    double nx = 2 * x / w - 1, ny = 1 - 2 * y / h;
    double aspect = h > 0 ? (double)w / h : 1;
    Vec3 f = forward(), r = right(), u = upv();
    Ray ry;
    if (s->perspective) {
        double t = std::tan(s->fov * kPi / 360);
        ry.o = s->eye;
        ry.d = (f + r * (nx * t * aspect) + u * (ny * t)).normalized();
    } else {
        double hh = s->orthoHeight * 0.5;
        ry.o = s->eye + r * (nx * hh * aspect) + u * (ny * hh) - f * farZ * 0.5;
        ry.d = f;
    }
    return ry;
}

bool Camera::project(const Vec3& p, double& sx, double& sy, double* depth) const {
    Vec3 d = p - s->eye;
    Vec3 f = forward(), r = right(), u = upv();
    double z = d.dot(f);
    double aspect = h > 0 ? (double)w / h : 1;
    double nx, ny;
    if (s->perspective) {
        if (z <= 1e-6) return false;
        double t = std::tan(s->fov * kPi / 360);
        nx = d.dot(r) / (z * t * aspect);
        ny = d.dot(u) / (z * t);
    } else {
        double hh = s->orthoHeight * 0.5;
        nx = d.dot(r) / (hh * aspect);
        ny = d.dot(u) / hh;
    }
    sx = (nx + 1) * 0.5 * w;
    sy = (1 - ny) * 0.5 * h;
    if (depth) *depth = z;
    return true;
}

double Camera::pixelSize(const Vec3& p) const {
    if (!s->perspective) return s->orthoHeight / std::max(h, 1);
    double z = std::max((p - s->eye).dot(forward()), 1e-3);
    return 2 * z * std::tan(s->fov * kPi / 360) / std::max(h, 1);
}

void Camera::orbit(double dx, double dy, const Vec3& pivot) {
    // Turntable orbit with the blue axis kept vertical. Heading and pitch are tracked
    // explicitly so looking straight down or up (top/bottom views) stays stable.
    const double k = 0.0065;   // radians per pixel
    const Vec3 Z(0, 0, 1);
    Vec3 f = forward();
    Vec3 h(f.x, f.y, 0);
    if (h.length() < 1e-4) {
        // looking straight down/up: the screen's up direction gives the heading
        Vec3 u(s->up.x, s->up.y, 0);
        if (u.length() < 1e-9) u = Vec3(0, 1, 0);
        h = f.z < 0 ? u : -u;
    }
    h = h.normalized();
    Vec3 r0 = h.cross(Z).normalized();       // camera right (horizontal)
    Vec3 u0 = r0.cross(f).normalized();      // camera up
    double pitch = std::asin(std::clamp(f.z, -1.0, 1.0));
    // eye relative to the pivot, in the old camera frame
    Vec3 v = s->eye - pivot;
    double a = v.dot(f), b = v.dot(r0), c = v.dot(u0);
    double dist = distance();
    // new heading and pitch
    double yaw = -dx * k;
    Vec3 h1 = Mat4::rotation(Z, yaw).vector(h).normalized();
    double p1 = std::clamp(pitch - dy * k, -kPi / 2 + 1e-3, kPi / 2 - 1e-3);
    Vec3 f1 = (h1 * std::cos(p1) + Z * std::sin(p1)).normalized();
    Vec3 r1 = h1.cross(Z).normalized();
    Vec3 u1 = r1.cross(f1).normalized();
    s->eye = pivot + f1 * a + r1 * b + u1 * c;
    s->target = s->eye + f1 * dist;
    s->up = Z;
}

void Camera::pan(double dx, double dy, const Vec3& pivot) {
    double px = pixelSize(pivot);
    Vec3 off = right() * (-dx * px) + upv() * (dy * px);
    s->eye += off;
    s->target += off;
}

void Camera::zoomAt(double factor, double x, double y, const Vec3& pivotHint, bool havePivot) {
    // factor > 1 zooms in
    if (s->perspective) {
        Ray r = ray(x, y);
        Vec3 P;
        if (havePivot) P = pivotHint;
        else P = r.at(distance());
        double dist = (P - s->eye).length();
        double move = dist * (1 - 1 / factor);
        Vec3 dir = (P - s->eye).normalized();
        s->eye += dir * move;
        s->target += dir * move;
        // keep target ahead of eye at reasonable distance
        double td = distance();
        if (td < 1) s->target = s->eye + forward() * 1;
    } else {
        Ray r0 = ray(x, y);
        s->orthoHeight /= factor;
        Ray r1 = ray(x, y);
        Vec3 off = r0.o - r1.o;
        s->eye += off;
        s->target += off;
    }
}

void Camera::dolly(double amount) {
    Vec3 f = forward();
    s->eye += f * amount;
    s->target += f * amount;
}

void Camera::zoomExtents(const BBox& b) {
    BBox bb = b;
    if (!bb.valid()) { bb.add(Vec3(-5000, -5000, 0)); bb.add(Vec3(5000, 5000, 3000)); }
    Vec3 c = bb.center();
    double rad = std::max(bb.radius(), 100.0);
    Vec3 f = forward();
    double aspect = h > 0 ? (double)w / h : 1;
    double vfov = s->fov * kPi / 180;
    double hfov = 2 * std::atan(std::tan(vfov / 2) * aspect);
    double fov = std::min(vfov, hfov);
    double dist = rad / std::sin(fov / 2) * 1.12;
    s->target = c;
    s->eye = c - f * dist;
    if (!s->perspective) {
        s->orthoHeight = rad * 2.1 / std::min(1.0, aspect);
        s->eye = c - f * std::max(dist, rad * 4);
    }
}

void Camera::zoomBox(double x0, double y0, double x1, double y1, const Vec3& depthPoint) {
    double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    double bw = std::fabs(x1 - x0), bh = std::fabs(y1 - y0);
    if (bw < 4 || bh < 4) return;
    double factor = std::min(w / bw, h / bh);
    Ray r = ray(cx, cy);
    double depth = (depthPoint - s->eye).dot(forward());
    if (depth <= 0) depth = distance();
    Vec3 P = r.at(depth / std::max(r.d.dot(forward()), 1e-6));
    // center on P
    Vec3 off = P - s->target;
    Vec3 f = forward();
    off = off - f * off.dot(f);
    s->eye += off;
    s->target += off;
    double sx, sy;
    project(P, sx, sy);
    zoomAt(factor, w / 2.0, h / 2.0, P, true);
}

void Camera::lookAround(double dx, double dy) {
    Vec3 z(0, 0, 1);
    double d = distance();
    Mat4 ry = Mat4::rotationAbout(s->eye, z, -dx * 0.004);
    s->target = ry.point(s->target);
    Vec3 f = forward();
    double cur = std::asin(std::clamp(f.z, -1.0, 1.0));
    double np = std::clamp(cur - dy * 0.004, -kPi / 2 + 0.01, kPi / 2 - 0.01);
    Mat4 rp = Mat4::rotationAbout(s->eye, right(), np - cur);
    s->target = rp.point(s->target);
    s->target = s->eye + forward() * d;
    s->up = z;
}

void Camera::setView(const Vec3& dir, const Vec3& up, const BBox& b) {
    s->up = up;
    Vec3 c = b.valid() ? b.center() : s->target;
    double d = std::max(distance(), 1000.0);
    s->target = c;
    s->eye = c - dir.normalized() * d;
    zoomExtents(b);
}

void Camera::setFov(double deg) { s->fov = std::clamp(deg, 1.0, 120.0); }

}  // namespace gable
