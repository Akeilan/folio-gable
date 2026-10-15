#include "core/Sandbox.h"
#include "core/Geometry.h"
#include <array>
#include <map>
#include <set>

namespace gable {
namespace sandbox {

static int triFace(Entities& E, int a, int b, int c, bool up = true) {
    std::vector<int> loop = {a, b, c};
    Vec3 n = geom::newellNormal(E, loop);
    if (n.isZero()) return -1;
    if (up && n.z < 0) { std::swap(loop[1], loop[2]); n = -n; }
    return E.addFace(loop, {}, Plane(n, E.verts[a].p));
}

static void softenInterior(Entities& E, const std::set<int>& faces) {
    for (int f : faces) {
        if (!E.faces[f].alive) continue;
        for (int e : E.faceEdges(f)) {
            Edge& Ed = E.edges[e];
            int n = 0;
            for (int g : Ed.faces) if (E.faces[g].alive) ++n;
            if (n == 2) Ed.soft = Ed.smooth = true;
        }
    }
}

void makeGrid(Entities& E, const Vec3& o, const Vec3& xdIn, const Vec3& ydIn, double width, double depth, double cell) {
    Vec3 xd = xdIn.normalized(), yd = ydIn.normalized();
    int nx = std::max(1, (int)std::round(width / cell)), ny = std::max(1, (int)std::round(depth / cell));
    double cx = width / nx, cy = depth / ny;
    std::vector<int> ids((nx + 1) * (ny + 1));
    for (int j = 0; j <= ny; ++j)
        for (int i = 0; i <= nx; ++i) ids[j * (nx + 1) + i] = E.vertexAt(o + xd * (i * cx) + yd * (j * cy));
    std::set<int> faces;
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            int a = ids[j * (nx + 1) + i], b = ids[j * (nx + 1) + i + 1], c = ids[(j + 1) * (nx + 1) + i + 1], d = ids[(j + 1) * (nx + 1) + i];
            int f1 = triFace(E, a, b, c), f2 = triFace(E, a, c, d);
            if (f1 >= 0) faces.insert(f1);
            if (f2 >= 0) faces.insert(f2);
        }
    softenInterior(E, faces);
    // the cell boundaries stay visible as hard edges only on the outer rim; diagonals soft
    E.touch();
}

namespace {
struct Tri { int a, b, c; double cx, cy, r2; };
bool circum(const std::vector<Vec3>& p, int a, int b, int c, Tri& t) {
    double ax = p[a].x, ay = p[a].y, bx = p[b].x, by = p[b].y, cx = p[c].x, cy = p[c].y;
    double d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (std::fabs(d) < 1e-12) return false;
    double ux = ((ax * ax + ay * ay) * (by - cy) + (bx * bx + by * by) * (cy - ay) + (cx * cx + cy * cy) * (ay - by)) / d;
    double uy = ((ax * ax + ay * ay) * (cx - bx) + (bx * bx + by * by) * (ax - cx) + (cx * cx + cy * cy) * (bx - ax)) / d;
    t = {a, b, c, ux, uy, (ax - ux) * (ax - ux) + (ay - uy) * (ay - uy)};
    return true;
}
}  // namespace

int tinFromPoints(Entities& E, std::vector<Vec3> pts) {
    // dedupe in XY
    std::map<std::pair<long long, long long>, Vec3> uniq;
    for (auto& p : pts) {
        auto k = std::make_pair((long long)std::llround(p.x * 10), (long long)std::llround(p.y * 10));
        auto it = uniq.find(k);
        if (it == uniq.end() || p.z > it->second.z) uniq[k] = p;
    }
    std::vector<Vec3> P;
    for (auto& [k, p] : uniq) P.push_back(p);
    int n = (int)P.size();
    if (n < 3) return 0;
    BBox b;
    for (auto& p : P) b.add(p);
    double span = std::max(b.size().x, b.size().y) * 20 + 1;
    Vec3 c = b.center();
    P.push_back(Vec3(c.x - span, c.y - span, 0));
    P.push_back(Vec3(c.x + span, c.y - span, 0));
    P.push_back(Vec3(c.x, c.y + span, 0));
    std::vector<Tri> tris;
    Tri t0;
    circum(P, n, n + 1, n + 2, t0);
    tris.push_back(t0);
    for (int i = 0; i < n; ++i) {
        const Vec3& p = P[i];
        std::map<std::pair<int, int>, int> edgeCount;
        std::vector<Tri> keep;
        keep.reserve(tris.size());
        for (auto& t : tris) {
            double dx = p.x - t.cx, dy = p.y - t.cy;
            if (dx * dx + dy * dy <= t.r2 * (1 + 1e-12)) {
                for (auto e : {std::make_pair(t.a, t.b), std::make_pair(t.b, t.c), std::make_pair(t.c, t.a)}) {
                    auto key = e.first < e.second ? e : std::make_pair(e.second, e.first);
                    edgeCount[key]++;
                }
            } else keep.push_back(t);
        }
        for (auto& [e, cnt] : edgeCount) {
            if (cnt != 1) continue;
            Tri t;
            if (circum(P, e.first, e.second, i, t)) keep.push_back(t);
        }
        tris.swap(keep);
    }
    std::vector<int> ids(n);
    for (int i = 0; i < n; ++i) ids[i] = E.vertexAt(P[i]);
    std::set<int> faces;
    int count = 0;
    for (auto& t : tris) {
        if (t.a >= n || t.b >= n || t.c >= n) continue;
        int f = triFace(E, ids[t.a], ids[t.b], ids[t.c]);
        if (f >= 0) { faces.insert(f); ++count; }
    }
    softenInterior(E, faces);
    E.touch();
    return count;
}

std::vector<std::pair<int, double>> smooveWeights(const Entities& E, const Vec3& center, double radius, Vec3 up) {
    std::vector<std::pair<int, double>> w;
    up = up.normalized();
    for (int v = 0; v < (int)E.verts.size(); ++v) {
        if (!E.verts[v].alive) continue;
        Vec3 d = E.verts[v].p - center;
        d = d - up * d.dot(up);
        double r = d.length();
        if (r < radius) w.push_back({v, 0.5 * (1 + std::cos(kPi * r / radius))});
    }
    return w;
}

void smoove(Entities& E, const Vec3& center, double radius, double dz, Vec3 up) {
    auto w = smooveWeights(E, center, radius, up);
    std::set<int> faces;
    for (auto& [v, k] : w) {
        E.verts[v].p += up.normalized() * (dz * k);
        for (int e : E.verts[v].edges)
            for (int f : E.edges[e].faces) faces.insert(f);
    }
    E.invalidateIndex();
    geom::fixWarpedFaces(E, std::vector<int>(faces.begin(), faces.end()));
    E.touch();
}

bool flipEdge(Entities& E, int e) {
    if (e < 0 || !E.edges[e].alive) return false;
    std::vector<int> fs;
    for (int f : E.edges[e].faces) if (E.faces[f].alive) fs.push_back(f);
    if (fs.size() != 2) return false;
    const Face F1 = E.faces[fs[0]], F2 = E.faces[fs[1]];
    if (F1.outer.size() != 3 || F2.outer.size() != 3 || !F1.holes.empty() || !F2.holes.empty()) return false;
    int a = E.edges[e].v[0], b = E.edges[e].v[1];
    // orient so F1 contains a->b
    auto has = [](const std::vector<int>& L, int x, int y) {
        for (size_t i = 0; i < L.size(); ++i) if (L[i] == x && L[(i + 1) % L.size()] == y) return true;
        return false;
    };
    const Face* fa = &F1;
    const Face* fb = &F2;
    if (!has(F1.outer, a, b)) std::swap(fa, fb);
    if (!has(fa->outer, a, b)) std::swap(a, b);
    int c = -1, d = -1;
    for (int v : fa->outer) if (v != a && v != b) c = v;
    for (int v : fb->outer) if (v != a && v != b) d = v;
    if (c < 0 || d < 0 || c == d || E.findEdge(c, d) >= 0) return false;
    Edge old = E.edges[e];
    int mf = fa->matFront, mb = fa->matBack;
    E.removeFace(fs[0]);
    E.removeFace(fs[1]);
    E.removeEdge(e);
    auto mk = [&](int x, int y, int z) {
        std::vector<int> L = {x, y, z};
        Vec3 n = geom::newellNormal(E, L);
        int f = E.addFace(L, {}, Plane(n, E.verts[x].p));
        if (f >= 0) { E.faces[f].matFront = mf; E.faces[f].matBack = mb; }
    };
    // boundary b->c->a->d ; new diagonal c-d
    mk(c, a, d);
    mk(d, b, c);
    int ne = E.findEdge(c, d);
    if (ne >= 0) { E.edges[ne].soft = old.soft; E.edges[ne].smooth = old.smooth; }
    E.touch();
    return true;
}

int drape(Entities& E, const std::vector<std::pair<Vec3, Vec3>>& segs) {
    std::vector<int> pieces;
    int count = 0;
    int nf = (int)E.faces.size();
    for (auto& [A, B] : segs) {
        Vec2 a(A.x, A.y), b(B.x, B.y);
        Vec2 d = b - a;
        double len = d.length();
        if (len < kTol) continue;
        for (int f = 0; f < nf; ++f) {
            const Face& F = E.faces[f];
            if (!F.alive || std::fabs(F.plane.n.z) < 1e-6) continue;
            BBox fb = geom::faceBBox(E, f);
            if (std::max(a.x, b.x) < fb.lo.x || std::min(a.x, b.x) > fb.hi.x || std::max(a.y, b.y) < fb.lo.y || std::min(a.y, b.y) > fb.hi.y)
                continue;
            // parameters where the segment crosses the face boundary (XY)
            std::vector<double> ts;
            auto loopX = [&](const std::vector<int>& L) {
                for (size_t i = 0; i < L.size(); ++i) {
                    Vec2 p(E.verts[L[i]].p.x, E.verts[L[i]].p.y), q(E.verts[L[(i + 1) % L.size()]].p.x, E.verts[L[(i + 1) % L.size()]].p.y);
                    Vec2 e = q - p;
                    double den = d.cross(e);
                    if (std::fabs(den) < 1e-12) continue;
                    double t = (p - a).cross(e) / den, s = (p - a).cross(d) / den;
                    if (s >= 0 && s <= 1 && t > 0 && t < 1) ts.push_back(t);
                }
            };
            loopX(F.outer);
            for (auto& h : F.holes) loopX(h);
            ts.push_back(0);
            ts.push_back(1);
            std::sort(ts.begin(), ts.end());
            auto zAt = [&](const Vec2& q) { return -(F.plane.n.x * q.x + F.plane.n.y * q.y + F.plane.d) / F.plane.n.z; };
            for (size_t i = 0; i + 1 < ts.size(); ++i) {
                double t0 = ts[i], t1 = ts[i + 1];
                if ((t1 - t0) * len < kTol) continue;
                Vec2 mid = a + d * ((t0 + t1) / 2);
                Vec3 m3(mid.x, mid.y, zAt(mid));
                if (!E.pointInFace(f, m3)) continue;
                Vec2 q0 = a + d * t0, q1 = a + d * t1;
                Vec3 P0(q0.x, q0.y, zAt(q0)), P1(q1.x, q1.y, zAt(q1));
                auto p = geom::insertSegment(E, P0, P1);
                pieces.insert(pieces.end(), p.begin(), p.end());
                ++count;
            }
        }
    }
    geom::findFaces(E, pieces);
    E.touch();
    return count;
}

void subdivide(Entities& E, const std::vector<int>& faces) {
    std::vector<std::array<Vec3, 3>> tris;
    for (int f : faces) {
        if (f < 0 || !E.faces[f].alive || E.faces[f].outer.size() != 3) continue;
        const Face& F = E.faces[f];
        tris.push_back({E.verts[F.outer[0]].p, E.verts[F.outer[1]].p, E.verts[F.outer[2]].p});
    }
    for (auto& t : tris) {
        Vec3 m[3] = {(t[0] + t[1]) * 0.5, (t[1] + t[2]) * 0.5, (t[2] + t[0]) * 0.5};
        for (int k = 0; k < 3; ++k) {
            auto created = geom::addLine(E, m[k], m[(k + 1) % 3]);
            for (int e : created) E.edges[e].soft = E.edges[e].smooth = true;
            int e = E.findEdge(E.findVertex(m[k]), E.findVertex(m[(k + 1) % 3]));
            if (e >= 0) E.edges[e].soft = E.edges[e].smooth = true;
        }
    }
    E.touch();
}

}  // namespace sandbox
}  // namespace gable
