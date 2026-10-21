#include "core/Solids.h"
#include "core/Geometry.h"
#include <algorithm>
#include <functional>

namespace gable {

void flattenInstance(const Model& m, const Instance& in, const Mat4& parentXf, Entities& out) {
    std::function<void(int, const Mat4&, int)> walk = [&](int def, const Mat4& xf, int mat) {
        const Entities& E = m.defs[def].ents;
        std::vector<EntRef> all;
        for (int i = 0; i < (int)E.edges.size(); ++i) if (E.edges[i].alive) all.push_back({EntKind::Edge, i});
        for (int i = 0; i < (int)E.faces.size(); ++i) if (E.faces[i].alive) all.push_back({EntKind::Face, i});
        auto res = geom::copyEntities(E, all, out, xf, true);
        if (mat >= 0)
            for (auto& r : res)
                if (r.kind == EntKind::Face && out.faces[r.id].matFront < 0) out.faces[r.id].matFront = mat;
        for (auto& ci : E.insts)
            if (ci.alive && ci.def >= 0) walk(ci.def, xf * ci.xf, ci.material >= 0 ? ci.material : mat);
    };
    walk(in.def, parentXf * in.xf, in.material);
}

bool instanceIsSolid(const Model& m, const Instance& in) {
    Entities e;
    flattenInstance(m, in, Mat4(), e);
    return geom::isSolid(e);
}

double instanceVolume(const Model& m, const Instance& in) {
    Entities e;
    flattenInstance(m, in, Mat4(), e);
    if (!geom::isSolid(e)) return 0;
    return geom::volume(e);
}

namespace {
struct TriSet {
    std::vector<Vec3> t;
    BBox box;
    void build(const Entities& E) {
        for (int f = 0; f < (int)E.faces.size(); ++f)
            if (E.faces[f].alive) geom::faceTriangles(E, f, t);
        for (auto& p : t) box.add(p);
    }
    bool inside(const Vec3& p) const {
        if (p.x < box.lo.x || p.y < box.lo.y || p.z < box.lo.z || p.x > box.hi.x || p.y > box.hi.y || p.z > box.hi.z)
            return false;
        // Majority vote over three skewed rays for robustness.
        static const Vec3 dirs[3] = {Vec3(0.5772, 0.5781, 0.5766).normalized(), Vec3(-0.7012, 0.1234, 0.7021).normalized(),
                                     Vec3(0.1113, -0.9871, 0.1149).normalized()};
        int votes = 0;
        for (auto& d : dirs) {
            int hits = 0;
            for (size_t i = 0; i + 2 < t.size(); i += 3) {
                Vec3 e1 = t[i + 1] - t[i], e2 = t[i + 2] - t[i];
                Vec3 pv = d.cross(e2);
                double det = e1.dot(pv);
                if (std::fabs(det) < 1e-14) continue;
                double inv = 1 / det;
                Vec3 tv = p - t[i];
                double u = tv.dot(pv) * inv;
                if (u < 0 || u > 1) continue;
                Vec3 qv = tv.cross(e1);
                double v = d.dot(qv) * inv;
                if (v < 0 || u + v > 1) continue;
                double tt = e2.dot(qv) * inv;
                if (tt > 1e-9) ++hits;
            }
            if (hits & 1) ++votes;
        }
        return votes >= 2;
    }
};

enum class Cls { Outside, Inside, CoSame, CoOpp };

Cls classify(const Entities& E, int f, const TriSet& other) {
    std::vector<Vec3> tris;
    geom::faceTriangles(E, f, tris);
    if (tris.size() < 3) return Cls::Outside;
    size_t best = 0;
    double ba = -1;
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        double a = (tris[i + 1] - tris[i]).cross(tris[i + 2] - tris[i]).length();
        if (a > ba) { ba = a; best = i; }
    }
    Vec3 c = (tris[best] + tris[best + 1] + tris[best + 2]) / 3.0;
    Vec3 n = E.faces[f].plane.n;
    const double eps = 0.05;  // mm
    bool front = other.inside(c + n * eps), back = other.inside(c - n * eps);
    if (front && back) return Cls::Inside;
    if (!front && !back) return Cls::Outside;
    return back ? Cls::CoSame : Cls::CoOpp;
}

// The parts of segment p-q that lie on face f (in its plane).
void clipToFace(const Entities& A, int f, const Vec3& p, const Vec3& q, std::vector<std::pair<Vec3, Vec3>>& out) {
    const Face& F = A.faces[f];
    Vec3 d = q - p;
    double L = d.length();
    if (L < kTol) return;
    std::vector<double> ts = {0.0, 1.0};
    auto loopEdges = [&](const std::vector<int>& lp) {
        for (size_t i = 0; i < lp.size(); ++i) {
            Vec3 a = A.verts[lp[i]].p, b = A.verts[lp[(i + 1) % lp.size()]].p;
            // a loop vertex on the segment
            for (const Vec3& x : {a, b}) {
                double t = (x - p).dot(d) / (L * L);
                if (t > 0 && t < 1 && (p + d * t - x).length() < kTol * 5) ts.push_back(t);
            }
            double t, u;
            if (closestLineLine(p, d, a, b - a, t, u) && t > 0 && t < 1 && u >= -1e-9 && u <= 1 + 1e-9) {
                if ((p + d * t - (a + (b - a) * u)).length() < kTol * 5) ts.push_back(t);
            }
        }
    };
    loopEdges(F.outer);
    for (auto& h : F.holes) loopEdges(h);
    std::sort(ts.begin(), ts.end());
    for (size_t i = 0; i + 1 < ts.size(); ++i) {
        double t0 = ts[i], t1 = ts[i + 1];
        if ((t1 - t0) * L < kTol) continue;
        Vec3 mid = p + d * ((t0 + t1) * 0.5);
        if (A.pointInFace(f, F.plane.project(mid), kTol * 5)) out.push_back({p + d * t0, p + d * t1});
    }
}

void splitAgainst(Entities& A, const Entities& B) {
    std::vector<std::pair<Vec3, Vec3>> segs;
    for (int fa = 0; fa < (int)A.faces.size(); ++fa) {
        if (!A.faces[fa].alive) continue;
        const Plane& PA = A.faces[fa].plane;
        for (int fb = 0; fb < (int)B.faces.size(); ++fb) {
            if (!B.faces[fb].alive) continue;
            const Face& FB = B.faces[fb];
            bool coplanar = std::fabs(std::fabs(PA.n.dot(FB.plane.n)) - 1) < 1e-9 &&
                            std::fabs(PA.dist(B.verts[FB.outer[0]].p)) < 0.01;
            if (coplanar) {
                // faces lying on each other: cut A's face along B's outline so each piece is
                // either covered by B or not, and classifies correctly
                auto loop = [&](const std::vector<int>& lp) {
                    for (size_t i = 0; i < lp.size(); ++i)
                        clipToFace(A, fa, B.verts[lp[i]].p, B.verts[lp[(i + 1) % lp.size()]].p, segs);
                };
                loop(FB.outer);
                for (auto& h : FB.holes) loop(h);
                continue;
            }
            auto s = geom::faceFaceSegments(A, fa, B, fb);
            segs.insert(segs.end(), s.begin(), s.end());
        }
    }
    std::vector<int> pieces;
    for (auto& s : segs) {
        auto p = geom::insertSegment(A, s.first, s.second);
        pieces.insert(pieces.end(), p.begin(), p.end());
    }
    geom::findFaces(A, pieces);
}

void addFaceCopy(Entities& R, const Entities& S, int f, bool flip) {
    const Face& F = S.faces[f];
    auto mapLoop = [&](const std::vector<int>& L) {
        std::vector<int> r;
        for (int v : L) r.push_back(R.vertexAt(S.verts[v].p));
        if (flip) std::reverse(r.begin(), r.end());
        return r;
    };
    std::vector<int> outer = mapLoop(F.outer);
    std::vector<std::vector<int>> holes;
    for (auto& h : F.holes) holes.push_back(mapLoop(h));
    Plane p = F.plane;
    if (flip) { p.n = -p.n; p.d = -p.d; }
    int nf = R.addFace(outer, holes, p);
    if (nf >= 0) {
        R.faces[nf].matFront = flip ? F.matBack : F.matFront;
        R.faces[nf].matBack = flip ? F.matFront : F.matBack;
    }
    // carry edge softness
    auto copyEdgeFlags = [&](const std::vector<int>& L) {
        for (size_t i = 0; i < L.size(); ++i) {
            int se = S.findEdge(L[i], L[(i + 1) % L.size()]);
            int re = R.findEdge(R.vertexAt(S.verts[L[i]].p), R.vertexAt(S.verts[L[(i + 1) % L.size()]].p));
            if (se >= 0 && re >= 0) { R.edges[re].soft |= S.edges[se].soft; R.edges[re].smooth |= S.edges[se].smooth; }
        }
    };
    copyEdgeFlags(F.outer);
    for (auto& h : F.holes) copyEdgeFlags(h);
}

void finishResult(Entities& R) {
    std::vector<int> all;
    for (int e = 0; e < (int)R.edges.size(); ++e) if (R.edges[e].alive) all.push_back(e);
    geom::mergeCoplanarAcross(R, all);
    // remove edges with no faces
    for (int e = 0; e < (int)R.edges.size(); ++e)
        if (R.edges[e].alive && R.edges[e].faces.empty()) R.removeEdge(e);
    // A coplanar edge that survived between same-plane faces should not be soft
    for (auto& e : R.edges)
        if (e.alive && e.faces.size() == 2) {
            const Face& a = R.faces[e.faces[0]];
            const Face& b = R.faces[e.faces[1]];
            if (a.plane.n.dot(b.plane.n) < 0.9) { if (!(e.soft && e.smooth)) e.soft = e.smooth = false; }
        }
}
}  // namespace

bool solidOperation(Model& m, Entities& ctx, int first, int second, SolidOp op, QString* err, std::vector<int>* resultInsts) {
    if (first < 0 || second < 0 || first == second) { if (err) *err = "Select two different groups or components."; return false; }
    Instance IA = ctx.insts[first], IB = ctx.insts[second];
    Entities A, B;
    flattenInstance(m, IA, Mat4(), A);
    flattenInstance(m, IB, Mat4(), B);
    if (!geom::isSolid(A) || !geom::isSolid(B)) { if (err) *err = "Both objects must be solid (closed, manifold) groups or components."; return false; }
    Entities A0 = A, B0 = B;
    TriSet ta, tb;
    ta.build(A0);
    tb.build(B0);
    splitAgainst(A, B0);
    splitAgainst(B, A0);
    std::vector<Cls> ca(A.faces.size()), cb(B.faces.size());
    for (int f = 0; f < (int)A.faces.size(); ++f) if (A.faces[f].alive) ca[f] = classify(A, f, tb);
    for (int f = 0; f < (int)B.faces.size(); ++f) if (B.faces[f].alive) cb[f] = classify(B, f, ta);

    // X - Y, where X keeps outside Y, Y keeps inside X flipped.
    auto minus = [&](const Entities& X, const std::vector<Cls>& cx, const Entities& Y, const std::vector<Cls>& cy, Entities& R) {
        for (int f = 0; f < (int)X.faces.size(); ++f)
            if (X.faces[f].alive && (cx[f] == Cls::Outside || cx[f] == Cls::CoOpp)) addFaceCopy(R, X, f, false);
        for (int f = 0; f < (int)Y.faces.size(); ++f)
            if (Y.faces[f].alive && cy[f] == Cls::Inside) addFaceCopy(R, Y, f, true);
        finishResult(R);
    };
    auto unite = [&](Entities& R) {
        for (int f = 0; f < (int)A.faces.size(); ++f)
            if (A.faces[f].alive && (ca[f] == Cls::Outside || ca[f] == Cls::CoSame)) addFaceCopy(R, A, f, false);
        for (int f = 0; f < (int)B.faces.size(); ++f)
            if (B.faces[f].alive && cb[f] == Cls::Outside) addFaceCopy(R, B, f, false);
        finishResult(R);
    };
    auto intersect = [&](Entities& R) {
        for (int f = 0; f < (int)A.faces.size(); ++f)
            if (A.faces[f].alive && (ca[f] == Cls::Inside || ca[f] == Cls::CoSame)) addFaceCopy(R, A, f, false);
        for (int f = 0; f < (int)B.faces.size(); ++f)
            if (B.faces[f].alive && cb[f] == Cls::Inside) addFaceCopy(R, B, f, false);
        finishResult(R);
    };
    std::vector<Entities> results;
    std::vector<const Instance*> templ;
    bool removeFirst = true, removeSecond = true;
    switch (op) {
    case SolidOp::Union:
    case SolidOp::OuterShell: results.emplace_back(); unite(results.back()); templ.push_back(&IB); break;
    case SolidOp::Intersect: results.emplace_back(); intersect(results.back()); templ.push_back(&IB); break;
    case SolidOp::Subtract: results.emplace_back(); minus(B, cb, A, ca, results.back()); templ.push_back(&IB); break;
    case SolidOp::Trim: results.emplace_back(); minus(B, cb, A, ca, results.back()); templ.push_back(&IB); removeFirst = false; break;
    case SolidOp::Split:
        results.emplace_back(); minus(A, ca, B, cb, results.back()); templ.push_back(&IA);
        results.emplace_back(); minus(B, cb, A, ca, results.back()); templ.push_back(&IB);
        results.emplace_back(); intersect(results.back()); templ.push_back(&IA);
        break;
    }
    if (removeFirst) ctx.insts[first].alive = false;
    if (removeSecond) ctx.insts[second].alive = false;
    for (size_t i = 0; i < results.size(); ++i) {
        if (results[i].aliveCount(EntKind::Face) == 0) continue;
        const Instance& t = *templ[i];
        // Place the result in a new group whose origin matches the template's.
        Mat4 inv = t.xf.inverted();
        std::vector<EntRef> all;
        for (int f = 0; f < (int)results[i].faces.size(); ++f) if (results[i].faces[f].alive) all.push_back({EntKind::Face, f});
        int def = m.addDef(m.defs[t.def].isGroup ? "Group" : m.defs[t.def].name, true);
        Entities local;
        geom::copyEntities(results[i], all, local, inv, true);
        m.defs[def].ents = std::move(local);
        Instance in;
        in.def = def;
        in.xf = t.xf;
        in.tag = t.tag;
        in.name = t.name;
        ctx.insts.push_back(in);
        if (resultInsts) resultInsts->push_back((int)ctx.insts.size() - 1);
    }
    ctx.touch();
    return true;
}

}  // namespace gable
