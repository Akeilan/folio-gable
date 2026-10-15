// Gable — a native 3D modeler for Linux.
// Double-precision vector math used everywhere in the geometry engine.
#pragma once
#include <cmath>
#include <algorithm>
#include <limits>

namespace gable {

constexpr double kPi = 3.14159265358979323846;
// Geometric tolerance in model units (millimetres). Points closer than this are the same point.
constexpr double kTol = 1e-3;
constexpr double kAngTol = 1e-7;

struct Vec2 {
    double x = 0, y = 0;
    Vec2() = default;
    Vec2(double x_, double y_) : x(x_), y(y_) {}
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
    double dot(const Vec2& o) const { return x * o.x + y * o.y; }
    double cross(const Vec2& o) const { return x * o.y - y * o.x; }
    double length() const { return std::sqrt(x * x + y * y); }
};

struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
    double operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    double& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
    double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    double length() const { return std::sqrt(x * x + y * y + z * z); }
    double length2() const { return x * x + y * y + z * z; }
    Vec3 normalized() const { double l = length(); return l > 1e-300 ? *this / l : Vec3(0, 0, 0); }
    bool isZero(double eps = 1e-12) const { return length2() < eps * eps; }
    bool near(const Vec3& o, double eps = kTol) const { return (*this - o).length2() < eps * eps; }
    // Any unit vector perpendicular to this one.
    Vec3 anyPerp() const {
        Vec3 a = std::fabs(x) < 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
        return cross(a).normalized();
    }
};
inline Vec3 operator*(double s, const Vec3& v) { return v * s; }

// Column-major 4x4 matrix, affine transforms (m[col*4+row]).
struct Mat4 {
    double m[16];
    Mat4() { setIdentity(); }
    void setIdentity() { for (int i = 0; i < 16; ++i) m[i] = (i % 5 == 0) ? 1 : 0; }
    static Mat4 identity() { return Mat4(); }
    double& at(int r, int c) { return m[c * 4 + r]; }
    double at(int r, int c) const { return m[c * 4 + r]; }
    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int rr = 0; rr < 4; ++rr) {
                double s = 0;
                for (int k = 0; k < 4; ++k) s += at(rr, k) * o.at(k, c);
                r.at(rr, c) = s;
            }
        return r;
    }
    Vec3 point(const Vec3& p) const {
        double w = at(3, 0) * p.x + at(3, 1) * p.y + at(3, 2) * p.z + at(3, 3);
        Vec3 r(at(0, 0) * p.x + at(0, 1) * p.y + at(0, 2) * p.z + at(0, 3),
               at(1, 0) * p.x + at(1, 1) * p.y + at(1, 2) * p.z + at(1, 3),
               at(2, 0) * p.x + at(2, 1) * p.y + at(2, 2) * p.z + at(2, 3));
        if (std::fabs(w - 1.0) > 1e-15 && std::fabs(w) > 1e-300) r = r / w;
        return r;
    }
    Vec3 vector(const Vec3& v) const {
        return {at(0, 0) * v.x + at(0, 1) * v.y + at(0, 2) * v.z,
                at(1, 0) * v.x + at(1, 1) * v.y + at(1, 2) * v.z,
                at(2, 0) * v.x + at(2, 1) * v.y + at(2, 2) * v.z};
    }
    // Transforms a normal (uses inverse transpose of the linear part).
    Vec3 normal(const Vec3& n) const {
        Mat4 inv = inverted();
        return Vec3(inv.at(0, 0) * n.x + inv.at(1, 0) * n.y + inv.at(2, 0) * n.z,
                    inv.at(0, 1) * n.x + inv.at(1, 1) * n.y + inv.at(2, 1) * n.z,
                    inv.at(0, 2) * n.x + inv.at(1, 2) * n.y + inv.at(2, 2) * n.z).normalized();
    }
    Vec3 origin() const { return {at(0, 3), at(1, 3), at(2, 3)}; }
    Vec3 axis(int i) const { return {at(0, i), at(1, i), at(2, i)}; }
    double det3() const {
        return at(0, 0) * (at(1, 1) * at(2, 2) - at(1, 2) * at(2, 1)) -
               at(0, 1) * (at(1, 0) * at(2, 2) - at(1, 2) * at(2, 0)) +
               at(0, 2) * (at(1, 0) * at(2, 1) - at(1, 1) * at(2, 0));
    }
    bool isIdentity() const {
        for (int i = 0; i < 16; ++i)
            if (std::fabs(m[i] - ((i % 5 == 0) ? 1 : 0)) > 1e-12) return false;
        return true;
    }
    static Mat4 translation(const Vec3& t) { Mat4 r; r.at(0, 3) = t.x; r.at(1, 3) = t.y; r.at(2, 3) = t.z; return r; }
    static Mat4 scaling(const Vec3& s) { Mat4 r; r.at(0, 0) = s.x; r.at(1, 1) = s.y; r.at(2, 2) = s.z; return r; }
    static Mat4 scalingAbout(const Vec3& c, const Vec3& s) { return translation(c) * scaling(s) * translation(-c); }
    static Mat4 rotation(const Vec3& axisIn, double ang) {
        Vec3 a = axisIn.normalized();
        double c = std::cos(ang), s = std::sin(ang), t = 1 - c;
        Mat4 r;
        r.at(0, 0) = t * a.x * a.x + c;       r.at(0, 1) = t * a.x * a.y - s * a.z; r.at(0, 2) = t * a.x * a.z + s * a.y;
        r.at(1, 0) = t * a.x * a.y + s * a.z; r.at(1, 1) = t * a.y * a.y + c;       r.at(1, 2) = t * a.y * a.z - s * a.x;
        r.at(2, 0) = t * a.x * a.z - s * a.y; r.at(2, 1) = t * a.y * a.z + s * a.x; r.at(2, 2) = t * a.z * a.z + c;
        return r;
    }
    static Mat4 rotationAbout(const Vec3& p, const Vec3& axis, double ang) {
        return translation(p) * rotation(axis, ang) * translation(-p);
    }
    static Mat4 fromAxes(const Vec3& o, const Vec3& x, const Vec3& y, const Vec3& z) {
        Mat4 r;
        for (int i = 0; i < 3; ++i) { r.at(i, 0) = x[i]; r.at(i, 1) = y[i]; r.at(i, 2) = z[i]; r.at(i, 3) = o[i]; }
        return r;
    }
    static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
        Vec3 f = (target - eye).normalized();
        Vec3 s = f.cross(up).normalized();
        if (s.isZero()) s = f.anyPerp();
        Vec3 u = s.cross(f);
        Mat4 r;
        r.at(0, 0) = s.x; r.at(0, 1) = s.y; r.at(0, 2) = s.z;
        r.at(1, 0) = u.x; r.at(1, 1) = u.y; r.at(1, 2) = u.z;
        r.at(2, 0) = -f.x; r.at(2, 1) = -f.y; r.at(2, 2) = -f.z;
        r.at(0, 3) = -s.dot(eye); r.at(1, 3) = -u.dot(eye); r.at(2, 3) = f.dot(eye);
        return r;
    }
    static Mat4 perspective(double fovyRad, double aspect, double n, double f) {
        Mat4 r; for (double& v : r.m) v = 0;
        double t = 1.0 / std::tan(fovyRad / 2);
        r.at(0, 0) = t / aspect; r.at(1, 1) = t;
        r.at(2, 2) = (f + n) / (n - f); r.at(2, 3) = 2 * f * n / (n - f);
        r.at(3, 2) = -1;
        return r;
    }
    static Mat4 ortho(double l, double rr, double b, double t, double n, double f) {
        Mat4 r;
        r.at(0, 0) = 2 / (rr - l); r.at(1, 1) = 2 / (t - b); r.at(2, 2) = -2 / (f - n);
        r.at(0, 3) = -(rr + l) / (rr - l); r.at(1, 3) = -(t + b) / (t - b); r.at(2, 3) = -(f + n) / (f - n);
        return r;
    }
    Mat4 inverted() const {
        // General 4x4 inverse (cofactor expansion).
        const double* a = m;
        double inv[16];
        inv[0] = a[5]*a[10]*a[15]-a[5]*a[11]*a[14]-a[9]*a[6]*a[15]+a[9]*a[7]*a[14]+a[13]*a[6]*a[11]-a[13]*a[7]*a[10];
        inv[4] = -a[4]*a[10]*a[15]+a[4]*a[11]*a[14]+a[8]*a[6]*a[15]-a[8]*a[7]*a[14]-a[12]*a[6]*a[11]+a[12]*a[7]*a[10];
        inv[8] = a[4]*a[9]*a[15]-a[4]*a[11]*a[13]-a[8]*a[5]*a[15]+a[8]*a[7]*a[13]+a[12]*a[5]*a[11]-a[12]*a[7]*a[9];
        inv[12] = -a[4]*a[9]*a[14]+a[4]*a[10]*a[13]+a[8]*a[5]*a[14]-a[8]*a[6]*a[13]-a[12]*a[5]*a[10]+a[12]*a[6]*a[9];
        inv[1] = -a[1]*a[10]*a[15]+a[1]*a[11]*a[14]+a[9]*a[2]*a[15]-a[9]*a[3]*a[14]-a[13]*a[2]*a[11]+a[13]*a[3]*a[10];
        inv[5] = a[0]*a[10]*a[15]-a[0]*a[11]*a[14]-a[8]*a[2]*a[15]+a[8]*a[3]*a[14]+a[12]*a[2]*a[11]-a[12]*a[3]*a[10];
        inv[9] = -a[0]*a[9]*a[15]+a[0]*a[11]*a[13]+a[8]*a[1]*a[15]-a[8]*a[3]*a[13]-a[12]*a[1]*a[11]+a[12]*a[3]*a[9];
        inv[13] = a[0]*a[9]*a[14]-a[0]*a[10]*a[13]-a[8]*a[1]*a[14]+a[8]*a[2]*a[13]+a[12]*a[1]*a[10]-a[12]*a[2]*a[9];
        inv[2] = a[1]*a[6]*a[15]-a[1]*a[7]*a[14]-a[5]*a[2]*a[15]+a[5]*a[3]*a[14]+a[13]*a[2]*a[7]-a[13]*a[3]*a[6];
        inv[6] = -a[0]*a[6]*a[15]+a[0]*a[7]*a[14]+a[4]*a[2]*a[15]-a[4]*a[3]*a[14]-a[12]*a[2]*a[7]+a[12]*a[3]*a[6];
        inv[10] = a[0]*a[5]*a[15]-a[0]*a[7]*a[13]-a[4]*a[1]*a[15]+a[4]*a[3]*a[13]+a[12]*a[1]*a[7]-a[12]*a[3]*a[5];
        inv[14] = -a[0]*a[5]*a[14]+a[0]*a[6]*a[13]+a[4]*a[1]*a[14]-a[4]*a[2]*a[13]-a[12]*a[1]*a[6]+a[12]*a[2]*a[5];
        inv[3] = -a[1]*a[6]*a[11]+a[1]*a[7]*a[10]+a[5]*a[2]*a[11]-a[5]*a[3]*a[10]-a[9]*a[2]*a[7]+a[9]*a[3]*a[6];
        inv[7] = a[0]*a[6]*a[11]-a[0]*a[7]*a[10]-a[4]*a[2]*a[11]+a[4]*a[3]*a[10]+a[8]*a[2]*a[7]-a[8]*a[3]*a[6];
        inv[11] = -a[0]*a[5]*a[11]+a[0]*a[7]*a[9]+a[4]*a[1]*a[11]-a[4]*a[3]*a[9]-a[8]*a[1]*a[7]+a[8]*a[3]*a[5];
        inv[15] = a[0]*a[5]*a[10]-a[0]*a[6]*a[9]-a[4]*a[1]*a[10]+a[4]*a[2]*a[9]+a[8]*a[1]*a[6]-a[8]*a[2]*a[5];
        double det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
        Mat4 r;
        if (std::fabs(det) < 1e-300) return r;
        for (int i = 0; i < 16; ++i) r.m[i] = inv[i] / det;
        return r;
    }
    void toFloat(float* out) const { for (int i = 0; i < 16; ++i) out[i] = (float)m[i]; }
};

struct Plane {
    Vec3 n{0, 0, 1};
    double d = 0;  // n·p + d = 0
    Plane() = default;
    Plane(const Vec3& normal, const Vec3& p) : n(normal.normalized()), d(-n.dot(p)) {}
    double dist(const Vec3& p) const { return n.dot(p) + d; }
    Vec3 project(const Vec3& p) const { return p - n * dist(p); }
    Vec3 origin() const { return n * -d; }
    bool contains(const Vec3& p, double eps = kTol) const { return std::fabs(dist(p)) < eps; }
    bool sameAs(const Plane& o) const {
        double c = n.dot(o.n);
        if (c > 1 - 1e-9) return std::fabs(d - o.d) < kTol;
        if (c < -1 + 1e-9) return std::fabs(d + o.d) < kTol;
        return false;
    }
};

struct Ray {
    Vec3 o, d;  // d normalized
    Vec3 at(double t) const { return o + d * t; }
    // Intersect with plane, returns t or NaN.
    double hit(const Plane& p) const {
        double den = p.n.dot(d);
        if (std::fabs(den) < 1e-12) return std::numeric_limits<double>::quiet_NaN();
        return -(p.n.dot(o) + p.d) / den;
    }
};

struct BBox {
    Vec3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    bool valid() const { return lo.x <= hi.x; }
    void add(const Vec3& p) {
        lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); lo.z = std::min(lo.z, p.z);
        hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); hi.z = std::max(hi.z, p.z);
    }
    void add(const BBox& b) { if (b.valid()) { add(b.lo); add(b.hi); } }
    Vec3 center() const { return (lo + hi) * 0.5; }
    Vec3 size() const { return hi - lo; }
    double radius() const { return valid() ? (hi - lo).length() * 0.5 : 0; }
    Vec3 corner(int i) const { return {(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z}; }
    BBox transformed(const Mat4& m) const {
        BBox r;
        if (!valid()) return r;
        for (int i = 0; i < 8; ++i) r.add(m.point(corner(i)));
        return r;
    }
};

// Closest points between two infinite lines p1+t*d1 and p2+s*d2. Returns false if parallel.
inline bool closestLineLine(const Vec3& p1, const Vec3& d1, const Vec3& p2, const Vec3& d2, double& t, double& s) {
    Vec3 r = p1 - p2;
    double a = d1.dot(d1), e = d2.dot(d2), f = d2.dot(r), b = d1.dot(d2), c = d1.dot(r);
    double den = a * e - b * b;
    if (std::fabs(den) < 1e-14 * a * e) return false;
    t = (b * f - c * e) / den;
    s = (a * f - b * c) / den;
    return true;
}

// Closest point on segment [a,b] to p; returns parameter in [0,1].
inline double closestOnSegment(const Vec3& a, const Vec3& b, const Vec3& p) {
    Vec3 d = b - a;
    double l2 = d.length2();
    if (l2 < 1e-300) return 0;
    return std::clamp((p - a).dot(d) / l2, 0.0, 1.0);
}

// 2D frame for a plane: projects 3D points into plane coordinates.
struct Frame2 {
    Vec3 o, u, v, n;
    Frame2() = default;
    explicit Frame2(const Plane& p) {
        n = p.n; o = p.origin();
        // Choose u so axis-aligned planes map to axis-aligned coordinates.
        Vec3 ref = std::fabs(n.z) > 0.9 ? Vec3(1, 0, 0) : Vec3(0, 0, 1);
        if (std::fabs(n.z) > 0.9) { u = Vec3(1, 0, 0) - n * n.x; u = u.normalized(); v = n.cross(u); }
        else { v = ref - n * n.dot(ref); v = v.normalized(); u = v.cross(n); }
    }
    Vec2 to2(const Vec3& p) const { Vec3 d = p - o; return {d.dot(u), d.dot(v)}; }
    Vec3 to3(const Vec2& q) const { return o + u * q.x + v * q.y; }
};

inline double signedArea2(const Vec2* pts, size_t n) {
    double a = 0;
    for (size_t i = 0; i < n; ++i) a += pts[i].cross(pts[(i + 1) % n]);
    return a * 0.5;
}

inline bool pointInPoly2(const Vec2& p, const Vec2* pts, size_t n) {
    bool in = false;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        if (((pts[i].y > p.y) != (pts[j].y > p.y)) &&
            (p.x < (pts[j].x - pts[i].x) * (p.y - pts[i].y) / (pts[j].y - pts[i].y) + pts[i].x))
            in = !in;
    }
    return in;
}

inline double distPointSeg2(const Vec2& p, const Vec2& a, const Vec2& b) {
    Vec2 d = b - a;
    double l2 = d.dot(d);
    double t = l2 > 0 ? std::clamp((p - a).dot(d) / l2, 0.0, 1.0) : 0;
    return (a + d * t - p).length();
}

}  // namespace gable
