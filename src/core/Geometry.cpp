#include "core/Geometry.h"
#include "core/Tessellate.h"
#include <algorithm>
#include <functional>
#include <map>
#include <numeric>
#include <queue>

namespace gable {
namespace geom {

static constexpr double kPlaneTol = 0.01;  // mm: a vertex lies on a plane if closer than this

// ------------------------------------------------------------------ helpers

Vec3 newellNormal(const Entities& E, const std::vector<int>& loop) {
    Vec3 n;
    for (size_t i = 0; i < loop.size(); ++i) {
        const Vec3& a = E.verts[loop[i]].p;
        const Vec3& b = E.verts[loop[(i + 1) % loop.size()]].p;
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return n.normalized();
}

static Vec3 newellNormalPts(const std::vector<Vec3>& loop) {
    Vec3 n;
    for (size_t i = 0; i < loop.size(); ++i) {
        const Vec3& a = loop[i];
        const Vec3& b = loop[(i + 1) % loop.size()];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return n.normalized();
}

BBox faceBBox(const Entities& E, int f) {
    BBox b;
    for (int v : E.faces[f].outer) b.add(E.verts[v].p);
    return b;
}

static bool bboxOverlap(const BBox& a, const BBox& b, double tol) {
    return !(a.hi.x < b.lo.x - tol || b.hi.x < a.lo.x - tol || a.hi.y < b.lo.y - tol || b.hi.y < a.lo.y - tol ||
             a.hi.z < b.lo.z - tol || b.hi.z < a.lo.z - tol);
}

FaceSnap snapFace(const Entities& E, int f) {
    const Face& F = E.faces[f];
    FaceSnap s;
    auto loopPts = [&](const std::vector<int>& l) {
        std::vector<Vec3> r;
        for (int v : l) r.push_back(E.verts[v].p);
        return r;
    };
    s.loops.push_back(loopPts(F.outer));
    for (auto& h : F.holes) s.loops.push_back(loopPts(h));
    s.plane = F.plane;
    s.matFront = F.matFront; s.matBack = F.matBack;
    s.texFront = F.texFront; s.texBack = F.texBack;
    s.hidden = F.hidden; s.tag = F.tag;
    return s;
}

static void removeEdgeFromVertex(Entities& E, int v, int e) {
    auto& ve = E.verts[v].edges;
    ve.erase(std::remove(ve.begin(), ve.end(), e), ve.end());
}

// Merges duplicate edge dup into keep (same endpoints).
static void mergeEdgeInto(Entities& E, int keep, int dup) {
    if (keep == dup) return;
    for (int f : E.edges[dup].faces) {
        auto& fl = E.edges[keep].faces;
        if (std::find(fl.begin(), fl.end(), f) == fl.end()) fl.push_back(f);
    }
    Edge& D = E.edges[dup];
    removeEdgeFromVertex(E, D.v[0], dup);
    removeEdgeFromVertex(E, D.v[1], dup);
    if (E.edges[keep].curve < 0) E.edges[keep].curve = D.curve;
    D.alive = false;
    D.faces.clear();
}

static void dedupeEdgesAt(Entities& E, int v) {
    std::map<int, int> byOther;
    std::vector<int> es = E.verts[v].edges;
    for (int e : es) {
        if (!E.edges[e].alive) continue;
        int o = E.edges[e].other(v);
        auto it = byOther.find(o);
        if (it == byOther.end()) byOther[o] = e;
        else mergeEdgeInto(E, it->second, e);
    }
}

int splitEdge(Entities& E, int e, const Vec3& p) {
    int v0 = E.edges[e].v[0], v1 = E.edges[e].v[1];
    if (E.verts[v0].p.near(p)) return v0;
    if (E.verts[v1].p.near(p)) return v1;
    int m = E.findVertex(p);
    if (m < 0) m = E.addVertex(p);
    if (m == v0 || m == v1) return m;
    Edge copy = E.edges[e];
    removeEdgeFromVertex(E, v1, e);
    E.edges[e].v[1] = m;
    E.verts[m].edges.push_back(e);
    Edge ne = copy;
    ne.v[0] = m; ne.v[1] = v1;
    E.edges.push_back(ne);
    int n = (int)E.edges.size() - 1;
    E.verts[m].edges.push_back(n);
    E.verts[v1].edges.push_back(n);
    for (int f : copy.faces) {
        Face& F = E.faces[f];
        auto fix = [&](std::vector<int>& L) {
            for (size_t i = 0; i < L.size(); ++i) {
                int a = L[i], b = L[(i + 1) % L.size()];
                if ((a == v0 && b == v1) || (a == v1 && b == v0)) {
                    L.insert(L.begin() + i + 1, m);
                    return true;
                }
            }
            return false;
        };
        if (!fix(F.outer))
            for (auto& h : F.holes)
                if (fix(h)) break;
    }
    dedupeEdgesAt(E, m);
    E.touch();
    return m;
}

void mergeVertices(Entities& E, int keep, int dup) {
    if (keep == dup || !E.verts[dup].alive) return;
    std::set<int> faces;
    std::vector<int> es = E.verts[dup].edges;
    for (int e : es)
        for (int f : E.edges[e].faces) faces.insert(f);
    for (int e : es) {
        Edge& Ed = E.edges[e];
        if (!Ed.alive) continue;
        for (int k = 0; k < 2; ++k)
            if (Ed.v[k] == dup) Ed.v[k] = keep;
        if (Ed.v[0] == Ed.v[1]) {
            removeEdgeFromVertex(E, keep, e);
            Ed.alive = false;
            Ed.faces.clear();
        } else {
            E.verts[keep].edges.push_back(e);
        }
    }
    E.verts[dup].edges.clear();
    E.verts[dup].alive = false;
    E.invalidateIndex();
    for (int f : faces) {
        if (!E.faces[f].alive) continue;
        Face& F = E.faces[f];
        auto fixLoop = [&](std::vector<int>& L) {
            for (int& v : L) if (v == dup) v = keep;
            std::vector<int> r;
            for (size_t i = 0; i < L.size(); ++i)
                if (r.empty() || r.back() != L[i]) r.push_back(L[i]);
            while (r.size() > 1 && r.front() == r.back()) r.pop_back();
            L = r;
        };
        fixLoop(F.outer);
        for (auto& h : F.holes) fixLoop(h);
        F.holes.erase(std::remove_if(F.holes.begin(), F.holes.end(), [](auto& h) { return h.size() < 3; }), F.holes.end());
    }
    dedupeEdgesAt(E, keep);
    for (int f : faces) {
        if (!E.faces[f].alive) continue;
        if (E.faces[f].outer.size() < 3) {
            E.removeFace(f);
            continue;
        }
        // Make sure edge->face links are consistent after the merge.
        E.unlinkFace(f);
        E.linkFace(f);
    }
    E.touch();
}

// ------------------------------------------------------------------ insert segment

std::vector<int> insertSegment(Entities& E, const Vec3& a, const Vec3& b, std::vector<int>* createdOut) {
    double len = (b - a).length();
    if (len < kTol) return {};
    Vec3 d = (b - a) / len;
    std::vector<double> ts = {0, len};
    BBox sb;
    sb.add(a); sb.add(b);
    struct Req { int e; Vec3 p; double s; };
    std::vector<Req> reqs;
    int nE = (int)E.edges.size();
    for (int e = 0; e < nE; ++e) {
        const Edge& Ed = E.edges[e];
        if (!Ed.alive) continue;
        Vec3 p0 = E.verts[Ed.v[0]].p, p1 = E.verts[Ed.v[1]].p;
        BBox eb; eb.add(p0); eb.add(p1);
        if (!bboxOverlap(sb, eb, kTol)) continue;
        Vec3 ed = p1 - p0;
        double el = ed.length();
        if (el < 1e-12) continue;
        ed = ed / el;
        Vec3 cr = d.cross(ed);
        if (cr.length() < 1e-9) {
            Vec3 w = p0 - a;
            double dist = (w - d * w.dot(d)).length();
            if (dist > kTol) continue;
            for (const Vec3& q : {p0, p1}) {
                double t = (q - a).dot(d);
                if (t > kTol && t < len - kTol) ts.push_back(t);
            }
            for (const Vec3& X : {a, b}) {
                double s = (X - p0).dot(ed);
                if (s > kTol && s < el - kTol) reqs.push_back({e, X, s});
            }
        } else {
            double t, s;
            if (!closestLineLine(a, d, p0, ed, t, s)) continue;
            if (t < -kTol || t > len + kTol || s < -kTol || s > el + kTol) continue;
            Vec3 X1 = a + d * t, X2 = p0 + ed * s;
            if ((X1 - X2).length() > kTol) continue;
            Vec3 X = X2;
            if (s <= kTol) { X = p0; s = 0; }
            else if (s >= el - kTol) { X = p1; s = el; }
            t = std::clamp((X - a).dot(d), 0.0, len);
            if (t <= kTol) { if (s > 0 && s < el) X = a; t = 0; }
            else if (t >= len - kTol) { if (s > 0 && s < el) X = b; t = len; }
            ts.push_back(t);
            if (s > kTol && s < el - kTol) reqs.push_back({e, X, s});
        }
    }
    std::sort(reqs.begin(), reqs.end(), [](const Req& x, const Req& y) { return x.e != y.e ? x.e < y.e : x.s > y.s; });
    for (auto& r : reqs) {
        if (!E.edges[r.e].alive) continue;
        splitEdge(E, r.e, r.p);
    }
    std::sort(ts.begin(), ts.end());
    std::vector<double> ut;
    for (double t : ts)
        if (ut.empty() || t - ut.back() > kTol) ut.push_back(t);
    if (ut.back() < len - kTol) ut.push_back(len);
    else ut.back() = len;
    std::vector<int> pieces;
    int prev = E.vertexAt(a);
    for (size_t i = 1; i < ut.size(); ++i) {
        Vec3 p = (i + 1 == ut.size()) ? b : a + d * ut[i];
        int v = E.vertexAt(p);
        if (v != prev) {
            int e = E.findEdge(prev, v);
            if (e < 0) {
                e = E.addEdge(prev, v);
                if (createdOut && e >= 0) createdOut->push_back(e);
            }
            if (e >= 0) pieces.push_back(e);
        }
        prev = v;
    }
    return pieces;
}

// ------------------------------------------------------------------ plane rebuild

namespace {
struct OldPoly {
    std::vector<std::vector<Vec2>> loops;
    int sign = 1;
    BBox box2;  // x,y used
    const FaceSnap* snap = nullptr;
    int live = -1;
};

bool polyContains(const OldPoly& op, const Vec2& p) {
    if (p.x < op.box2.lo.x - kTol || p.x > op.box2.hi.x + kTol || p.y < op.box2.lo.y - kTol || p.y > op.box2.hi.y + kTol)
        return false;
    if (!pointInPoly2(p, op.loops[0].data(), op.loops[0].size())) return false;
    for (size_t i = 1; i < op.loops.size(); ++i)
        if (pointInPoly2(p, op.loops[i].data(), op.loops[i].size())) return false;
    return true;
}

double minDistToLoop(const Vec2& p, const std::vector<Vec2>& L) {
    double m = 1e300;
    for (size_t i = 0; i < L.size(); ++i) m = std::min(m, distPointSeg2(p, L[i], L[(i + 1) % L.size()]));
    return m;
}

Vec3 defaultNormal(const Plane& P) {
    Vec3 n = P.n;
    if (std::fabs(n.z) > 0.999) {
        double z = -P.d * n.z;
        return (std::fabs(z) < kTol) ? Vec3(0, 0, -1) : Vec3(0, 0, 1);
    }
    Vec3 ref(-0.5, -0.8, 0.3);
    return n.dot(ref) >= 0 ? n : -n;
}

std::vector<int> loopKey(std::vector<int> outer, std::vector<std::vector<int>> holes) {
    std::sort(outer.begin(), outer.end());
    for (auto& h : holes) std::sort(h.begin(), h.end());
    std::sort(holes.begin(), holes.end());
    std::vector<int> k = outer;
    for (auto& h : holes) { k.push_back(-1); k.insert(k.end(), h.begin(), h.end()); }
    return k;
}
}  // namespace

std::vector<int> rebuildPlane(Entities& E, const Plane& Pin, const std::unordered_set<int>& newEdges,
                              const std::vector<FaceSnap>& removed, RebuildMode mode) {
    Plane P = Pin;
    Frame2 fr(P);
    auto onP = [&](int v) { return std::fabs(P.dist(E.verts[v].p)) < kPlaneTol; };

    // Edges in plane.
    std::vector<int> pe;
    for (int e = 0; e < (int)E.edges.size(); ++e) {
        const Edge& Ed = E.edges[e];
        if (Ed.alive && onP(Ed.v[0]) && onP(Ed.v[1])) pe.push_back(e);
    }
    // Old faces.
    std::vector<int> oldLive;
    for (int f = 0; f < (int)E.faces.size(); ++f)
        if (E.faces[f].alive && E.faces[f].plane.sameAs(P)) oldLive.push_back(f);

    // Region of interest: only the part of the plane touched by this change is rebuilt.
    {
        BBox roi;
        for (int e : newEdges)
            if (e >= 0 && e < (int)E.edges.size() && E.edges[e].alive) { roi.add(E.verts[E.edges[e].v[0]].p); roi.add(E.verts[E.edges[e].v[1]].p); }
        for (auto& sn : removed)
            if (sn.plane.sameAs(P) && !sn.loops.empty())
                for (auto& p : sn.loops[0]) roi.add(p);
        if (roi.valid()) {
            std::unordered_set<int> peSet(pe.begin(), pe.end()), keepE;
            std::vector<int> stack;
            auto seed = [&](int e) {
                if (peSet.count(e) && keepE.insert(e).second) stack.push_back(e);
            };
            for (int e : pe) {
                BBox eb;
                eb.add(E.verts[E.edges[e].v[0]].p);
                eb.add(E.verts[E.edges[e].v[1]].p);
                if (bboxOverlap(eb, roi, kPlaneTol)) seed(e);
            }
            std::vector<char> faceKept(oldLive.size(), 0);
            auto keepFace = [&](size_t i) {
                if (faceKept[i]) return;
                faceKept[i] = 1;
                for (int e : E.faceEdges(oldLive[i])) seed(e);
            };
            for (size_t i = 0; i < oldLive.size(); ++i)
                if (bboxOverlap(faceBBox(E, oldLive[i]), roi, kPlaneTol)) keepFace(i);
            bool grew = true;
            while (grew) {
                while (!stack.empty()) {
                    int e = stack.back();
                    stack.pop_back();
                    for (int k = 0; k < 2; ++k)
                        for (int e2 : E.verts[E.edges[e].v[k]].edges) seed(e2);
                }
                grew = false;
                for (size_t i = 0; i < oldLive.size(); ++i) {
                    if (faceKept[i]) continue;
                    for (int e : E.faceEdges(oldLive[i]))
                        if (keepE.count(e)) { keepFace(i); grew = true; break; }
                }
            }
            pe.assign(keepE.begin(), keepE.end());
            std::sort(pe.begin(), pe.end());
            std::vector<int> ol;
            for (size_t i = 0; i < oldLive.size(); ++i)
                if (faceKept[i]) ol.push_back(oldLive[i]);
            oldLive = ol;
        }
    }

    std::vector<OldPoly> olds;
    for (int f : oldLive) {
        OldPoly op;
        auto add = [&](const std::vector<int>& L) {
            std::vector<Vec2> q;
            for (int v : L) q.push_back(fr.to2(E.verts[v].p));
            op.loops.push_back(q);
        };
        add(E.faces[f].outer);
        for (auto& h : E.faces[f].holes) add(h);
        op.sign = E.faces[f].plane.n.dot(P.n) > 0 ? 1 : -1;
        op.live = f;
        for (auto& q : op.loops[0]) op.box2.add(Vec3(q.x, q.y, 0));
        olds.push_back(std::move(op));
    }
    for (auto& s : removed) {
        if (!s.plane.sameAs(P)) continue;
        OldPoly op;
        for (auto& L : s.loops) {
            std::vector<Vec2> q;
            for (auto& p : L) q.push_back(fr.to2(p));
            op.loops.push_back(q);
        }
        op.sign = s.plane.n.dot(P.n) > 0 ? 1 : -1;
        op.snap = &s;
        for (auto& q : op.loops[0]) op.box2.add(Vec3(q.x, q.y, 0));
        olds.push_back(std::move(op));
    }

    // Planar half-edge structure.
    std::unordered_map<int, int> vloc;
    std::vector<int> vglob;
    std::vector<Vec2> vp;
    auto loc = [&](int v) {
        auto it = vloc.find(v);
        if (it != vloc.end()) return it->second;
        int id = (int)vglob.size();
        vloc[v] = id;
        vglob.push_back(v);
        vp.push_back(fr.to2(E.verts[v].p));
        return id;
    };
    struct HE { int from, to, edge; double ang; int pos = 0; int cycle = -1; };
    std::vector<HE> he;
    for (int e : pe) {
        int a = loc(E.edges[e].v[0]), b = loc(E.edges[e].v[1]);
        Vec2 d = vp[b] - vp[a];
        he.push_back({a, b, e, std::atan2(d.y, d.x)});
        he.push_back({b, a, e, std::atan2(-d.y, -d.x)});
    }
    std::vector<std::vector<int>> out(vglob.size());
    for (int h = 0; h < (int)he.size(); ++h) out[he[h].from].push_back(h);
    for (auto& o : out) {
        std::sort(o.begin(), o.end(), [&](int x, int y) { return he[x].ang < he[y].ang; });
        for (int i = 0; i < (int)o.size(); ++i) he[o[i]].pos = i;
    }
    auto nextHE = [&](int h) {
        int tw = h ^ 1;
        auto& o = out[he[h].to];
        int k = he[tw].pos;
        return o[(k - 1 + (int)o.size()) % (int)o.size()];
    };

    struct Loop { std::vector<int> verts; double area; };
    std::vector<Loop> loops;
    for (int h0 = 0; h0 < (int)he.size(); ++h0) {
        if (he[h0].cycle >= 0) continue;
        std::vector<int> cyc;
        int h = h0;
        int guard = 0;
        while (he[h].cycle < 0 && guard++ < 1000000) {
            he[h].cycle = h0;
            cyc.push_back(h);
            h = nextHE(h);
        }
        // Remove internal edges (both sides in this cycle).
        std::unordered_map<int, int> cnt;
        for (int x : cyc) cnt[he[x].edge]++;
        std::vector<int> rem;
        for (int x : cyc)
            if (cnt[he[x].edge] == 1) rem.push_back(x);
        if (rem.size() < 3) continue;
        std::vector<char> used(rem.size(), 0);
        for (size_t s = 0; s < rem.size(); ++s) {
            if (used[s]) continue;
            std::vector<int> lv;
            size_t cur = s;
            int g2 = 0;
            while (g2++ < 1000000) {
                used[cur] = 1;
                lv.push_back(he[rem[cur]].from);
                int want = he[rem[cur]].to;
                size_t nxt = SIZE_MAX;
                for (size_t k = 1; k <= rem.size(); ++k) {
                    size_t j = (cur + k) % rem.size();
                    if ((!used[j] || j == s) && he[rem[j]].from == want) { nxt = j; break; }
                }
                if (nxt == SIZE_MAX || nxt == s) break;
                cur = nxt;
            }
            if (lv.size() < 3) continue;
            std::vector<Vec2> pts;
            for (int v : lv) pts.push_back(vp[v]);
            Loop L;
            L.area = signedArea2(pts.data(), pts.size());
            if (std::fabs(L.area) < 1e-9) continue;
            for (int v : lv) L.verts.push_back(vglob[v]);
            loops.push_back(std::move(L));
        }
    }

    auto loop2 = [&](const std::vector<int>& L) {
        std::vector<Vec2> q;
        for (int v : L) q.push_back(fr.to2(E.verts[v].p));
        return q;
    };

    // Candidates: positive loops with holes assigned.
    struct Cand { std::vector<int> outer; std::vector<std::vector<int>> holes; double area; std::vector<Vec2> o2; };
    std::vector<Cand> cands;
    for (auto& L : loops)
        if (L.area > 0) cands.push_back({L.verts, {}, L.area, loop2(L.verts)});
    for (auto& L : loops) {
        if (L.area >= 0) continue;
        std::vector<Vec2> q = loop2(L.verts);
        int best = -1;
        double bestA = 1e300;
        for (int c = 0; c < (int)cands.size(); ++c) {
            if (cands[c].area < -L.area - 1e-9 || cands[c].area >= bestA) continue;
            // strictly-inside test using a vertex or edge midpoint not on candidate boundary
            bool decided = false, inside = false;
            for (size_t i = 0; i < q.size() && !decided; ++i) {
                for (int pass = 0; pass < 2 && !decided; ++pass) {
                    Vec2 t = pass == 0 ? q[i] : (q[i] + q[(i + 1) % q.size()]) * 0.5;
                    if (minDistToLoop(t, cands[c].o2) < kTol * 2) continue;
                    inside = pointInPoly2(t, cands[c].o2.data(), cands[c].o2.size());
                    decided = true;
                }
            }
            if (decided && inside) { best = c; bestA = cands[c].area; }
        }
        if (best >= 0) cands[best].holes.push_back(L.verts);
    }

    // Decide which candidates are faces.
    std::unordered_set<int> keep;
    struct NewFace { int cand; int sign; const OldPoly* src; };
    std::vector<NewFace> creates;
    std::map<std::vector<int>, int> liveKeys;
    for (int f : oldLive) liveKeys[loopKey(E.faces[f].outer, E.faces[f].holes)] = f;

    for (int c = 0; c < (int)cands.size(); ++c) {
        Cand& C = cands[c];
        bool hasNew = false;
        for (size_t i = 0; i < C.outer.size() && !hasNew; ++i) {
            int e = E.findEdge(C.outer[i], C.outer[(i + 1) % C.outer.size()]);
            if (e >= 0 && newEdges.count(e)) hasNew = true;
        }
        // Samples
        std::vector<std::vector<Vec2>> l2;
        l2.push_back(C.o2);
        for (auto& h : C.holes) l2.push_back(loop2(h));
        TessResult tr;
        std::vector<std::pair<double, Vec2>> samples;
        if (tessellate(l2, tr)) {
            for (size_t t = 0; t + 2 < tr.tris.size(); t += 3) {
                Vec2 a = tr.pts[tr.tris[t]], b = tr.pts[tr.tris[t + 1]], cc = tr.pts[tr.tris[t + 2]];
                double ar = std::fabs((b - a).cross(cc - a)) * 0.5;
                samples.push_back({ar, (a + b + cc) * (1.0 / 3)});
            }
            std::sort(samples.begin(), samples.end(), [](auto& x, auto& y) { return x.first > y.first; });
            double total = 0;
            for (auto& s : samples) total += s.first;
            std::vector<std::pair<double, Vec2>> filt;
            for (auto& s : samples)
                if (filt.empty() || s.first > total * 1e-6) filt.push_back(s);
            if (filt.size() > 24) filt.resize(24);
            samples = filt;
        }
        if (samples.empty()) continue;
        bool allCovered = true;
        int firstSign = 0;
        const OldPoly* src = nullptr;
        for (size_t si = 0; si < samples.size(); ++si) {
            int net = 0;
            const OldPoly* any = nullptr;
            for (auto& op : olds)
                if (polyContains(op, samples[si].second)) {
                    net += op.sign;
                    if (!any || (op.live >= 0 && any->live < 0)) any = &op;
                }
            if (net == 0) { allCovered = false; if (si == 0) firstSign = 0; break; }
            if (si == 0) {
                firstSign = net > 0 ? 1 : -1;
                // prefer a source with the winning orientation
                for (auto& op : olds)
                    if (op.sign == firstSign && polyContains(op, samples[0].second)) { src = &op; break; }
                if (!src) src = any;
            }
        }
        bool isFace = mode == RebuildMode::Draw ? (allCovered || hasNew) : allCovered;
        if (!isFace) continue;
        int sign = 0;
        if (allCovered && firstSign != 0) sign = firstSign;
        else {
            // vote by neighbours sharing edges (consistent orientation flips direction across edge)
            int vote = 0;
            for (size_t i = 0; i < C.outer.size(); ++i) {
                int a = C.outer[i], b = C.outer[(i + 1) % C.outer.size()];
                int e = E.findEdge(a, b);
                if (e < 0) continue;
                for (int g : E.edges[e].faces) {
                    if (!E.faces[g].alive || E.faces[g].plane.sameAs(P)) continue;
                    const Face& G = E.faces[g];
                    auto dirIn = [&](const std::vector<int>& L) {
                        for (size_t k = 0; k < L.size(); ++k) {
                            int x = L[k], y = L[(k + 1) % L.size()];
                            if (x == a && y == b) return 1;
                            if (x == b && y == a) return -1;
                        }
                        return 0;
                    };
                    int dd = dirIn(G.outer);
                    for (auto& h : G.holes) if (!dd) dd = dirIn(h);
                    vote += dd == 1 ? -1 : (dd == -1 ? 1 : 0);
                }
            }
            if (vote != 0) sign = vote > 0 ? 1 : -1;
            else sign = defaultNormal(P).dot(P.n) > 0 ? 1 : -1;
        }
        auto it = liveKeys.find(loopKey(C.outer, C.holes));
        if (it != liveKeys.end()) {
            int f = it->second;
            int fs = E.faces[f].plane.n.dot(P.n) > 0 ? 1 : -1;
            if (fs == sign) { keep.insert(f); continue; }
        }
        creates.push_back({c, sign, src});
    }

    for (int f : oldLive)
        if (!keep.count(f)) E.removeFace(f);

    std::vector<int> result(keep.begin(), keep.end());
    Plane Pneg = P;
    Pneg.n = -P.n; Pneg.d = -P.d;
    for (auto& nf : creates) {
        Cand& C = cands[nf.cand];
        std::vector<int> outer = C.outer;
        std::vector<std::vector<int>> holes = C.holes;
        Plane pl = P;
        if (nf.sign < 0) {
            std::reverse(outer.begin(), outer.end());
            for (auto& h : holes) std::reverse(h.begin(), h.end());
            pl = Pneg;
        }
        int f = E.addFace(outer, holes, pl);
        if (f < 0) continue;
        Face& F = E.faces[f];
        if (nf.src) {
            bool flip = nf.src->sign != nf.sign;  // source oriented opposite to new face
            if (nf.src->live >= 0) {
                const Face& S = E.faces[nf.src->live];
                F.matFront = flip ? S.matBack : S.matFront;
                F.matBack = flip ? S.matFront : S.matBack;
                F.texFront = flip ? S.texBack : S.texFront;
                F.texBack = flip ? S.texFront : S.texBack;
                F.hidden = S.hidden; F.tag = S.tag;
            } else if (nf.src->snap) {
                const FaceSnap& S = *nf.src->snap;
                F.matFront = flip ? S.matBack : S.matFront;
                F.matBack = flip ? S.matFront : S.matBack;
                F.texFront = flip ? S.texBack : S.texFront;
                F.texBack = flip ? S.texFront : S.texBack;
                F.hidden = S.hidden; F.tag = S.tag;
            }
        }
        result.push_back(f);
    }
    E.touch();
    return result;
}

// ------------------------------------------------------------------ face finding

namespace {
struct PlaneSet {
    std::vector<Plane> planes;
    bool add(const Plane& p) {
        for (auto& q : planes)
            if (q.sameAs(p)) return false;
        planes.push_back(p);
        return true;
    }
};
}  // namespace

void findFaces(Entities& E, const std::vector<int>& newEdges) {
    PlaneSet cand;
    // Faces whose bounding box holds an endpoint (cheap filter); used to catch edges drawn inside faces.
    std::vector<std::pair<BBox, int>> fboxes;
    fboxes.reserve(E.faces.size());
    for (int f = 0; f < (int)E.faces.size(); ++f)
        if (E.faces[f].alive) fboxes.push_back({faceBBox(E, f), f});
    for (int e : newEdges) {
        if (e < 0 || !E.edges[e].alive) continue;
        int va = E.edges[e].v[0], vb = E.edges[e].v[1];
        Vec3 A = E.verts[va].p, B = E.verts[vb].p;
        Vec3 d = (B - A).normalized();
        BBox eb;
        eb.add(A); eb.add(B);
        for (auto& [fb, f] : fboxes) {
            if (!bboxOverlap(fb, eb, kPlaneTol)) continue;
            const Plane& fp = E.faces[f].plane;
            if (std::fabs(fp.dist(A)) < kPlaneTol && std::fabs(fp.dist(B)) < kPlaneTol) cand.add(fp);
        }
        for (int X : {va, vb}) {
            for (int e2 : E.verts[X].edges) {
                if (e2 == e || !E.edges[e2].alive) continue;
                int o = E.edges[e2].other(X);
                Vec3 d2 = (E.verts[o].p - E.verts[X].p).normalized();
                Vec3 c = d.cross(d2);
                if (c.length() > 1e-6) cand.add(Plane(c, A));
                else {
                    // collinear continuation: look one edge further
                    for (int e3 : E.verts[o].edges) {
                        if (e3 == e2 || !E.edges[e3].alive) continue;
                        int o3 = E.edges[e3].other(o);
                        Vec3 d3 = (E.verts[o3].p - E.verts[o].p).normalized();
                        Vec3 c3 = d.cross(d3);
                        if (c3.length() > 1e-6) cand.add(Plane(c3, A));
                    }
                }
            }
        }
    }
    for (auto& P : cand.planes) {
        std::unordered_set<int> ne;
        for (int e : newEdges) {
            if (e < 0 || !E.edges[e].alive) continue;
            if (std::fabs(P.dist(E.verts[E.edges[e].v[0]].p)) < kPlaneTol &&
                std::fabs(P.dist(E.verts[E.edges[e].v[1]].p)) < kPlaneTol)
                ne.insert(e);
        }
        if (!ne.empty()) rebuildPlane(E, P, ne, {}, RebuildMode::Draw);
    }
}

std::vector<int> addLine(Entities& E, const Vec3& a, const Vec3& b) {
    std::vector<int> created;
    std::vector<int> pieces = insertSegment(E, a, b, &created);
    findFaces(E, pieces);
    return created;
}

std::vector<int> addPolyline(Entities& E, const std::vector<Vec3>& pts, bool closed, const Curve* curve) {
    std::vector<int> created, all;
    size_t n = pts.size();
    if (n < 2) return created;
    int cid = -1;
    if (curve) {
        E.curves.push_back(*curve);
        cid = (int)E.curves.size() - 1;
    }
    size_t segs = closed ? n : n - 1;
    for (size_t i = 0; i < segs; ++i) {
        std::vector<int> c;
        auto p = insertSegment(E, pts[i], pts[(i + 1) % n], &c);
        all.insert(all.end(), p.begin(), p.end());
        created.insert(created.end(), c.begin(), c.end());
    }
    if (cid >= 0)
        for (int e : created) E.edges[e].curve = cid;
    findFaces(E, all);
    return created;
}

// ------------------------------------------------------------------ erase

void eraseEdges(Entities& E, const std::vector<int>& edges) {
    std::set<int> faces;
    for (int e : edges)
        if (e >= 0 && E.edges[e].alive)
            for (int f : E.edges[e].faces) faces.insert(f);
    std::vector<FaceSnap> snaps;
    PlaneSet planes;
    for (int f : faces) {
        snaps.push_back(snapFace(E, f));
        planes.add(E.faces[f].plane);
    }
    for (int e : edges) E.removeEdge(e);
    for (auto& P : planes.planes) rebuildPlane(E, P, {}, snaps, RebuildMode::MergeOnly);
    E.touch();
}

void eraseFaces(Entities& E, const std::vector<int>& faces) {
    for (int f : faces) E.removeFace(f);
}

bool mergeCoplanarAcross(Entities& E, const std::vector<int>& edges) {
    std::vector<int> todo;
    for (int e : edges) {
        if (e < 0 || !E.edges[e].alive) continue;
        auto& fl = E.edges[e].faces;
        if (fl.size() != 2) continue;
        const Face& A = E.faces[fl[0]];
        const Face& B = E.faces[fl[1]];
        if (!A.alive || !B.alive) continue;
        if (A.plane.n.dot(B.plane.n) > 1 - 1e-9 && std::fabs(A.plane.d - B.plane.d) < kPlaneTol) todo.push_back(e);
    }
    if (todo.empty()) return false;
    eraseEdges(E, todo);
    return true;
}

// ------------------------------------------------------------------ orientation

void reverseFace(Entities& E, int f) {
    Face& F = E.faces[f];
    std::reverse(F.outer.begin(), F.outer.end());
    for (auto& h : F.holes) std::reverse(h.begin(), h.end());
    F.plane.n = -F.plane.n;
    F.plane.d = -F.plane.d;
    std::swap(F.matFront, F.matBack);
    std::swap(F.texFront, F.texBack);
    E.touch();
}

// ------------------------------------------------------------------ push / pull

static int addFaceFromVerts(Entities& E, std::vector<int> loop, const std::vector<std::vector<int>>& holes = {}) {
    // drop consecutive duplicates
    std::vector<int> r;
    for (int v : loop)
        if (r.empty() || r.back() != v) r.push_back(v);
    while (r.size() > 1 && r.front() == r.back()) r.pop_back();
    if (r.size() < 3) return -1;
    Vec3 n = newellNormal(E, r);
    if (n.isZero()) return -1;
    return E.addFace(r, holes, Plane(n, E.verts[r[0]].p));
}

static bool isPlanarLoop(const Entities& E, const std::vector<int>& loop, const Plane& P) {
    for (int v : loop)
        if (std::fabs(P.dist(E.verts[v].p)) > kPlaneTol) return false;
    return true;
}

// Adds a quad; if non-planar, splits into two triangles with a soft diagonal.
static void addQuad(Entities& E, int a, int b, int c, int d, std::vector<int>& out) {
    std::vector<int> q = {a, b, c, d};
    std::vector<int> r;
    for (int v : q)
        if (std::find(r.begin(), r.end(), v) == r.end()) r.push_back(v);
    if (r.size() < 3) return;
    if (r.size() == 3) {
        int f = addFaceFromVerts(E, r);
        if (f >= 0) out.push_back(f);
        return;
    }
    Vec3 n = newellNormal(E, q);
    Plane P(n, E.verts[a].p);
    if (isPlanarLoop(E, q, P)) {
        int f = E.addFace(q, {}, P);
        if (f >= 0) out.push_back(f);
        return;
    }
    int f1 = addFaceFromVerts(E, {a, b, c});
    int f2 = addFaceFromVerts(E, {a, c, d});
    if (f1 >= 0) out.push_back(f1);
    if (f2 >= 0) out.push_back(f2);
    int diag = E.findEdge(a, c);
    if (diag >= 0) { E.edges[diag].soft = E.edges[diag].smooth = true; }
}

static void resolveCoplanarOverlaps(Entities& E, const std::vector<int>& newFaces, std::vector<int> mergeEdges) {
    PlaneSet planes;
    std::unordered_set<int> nf(newFaces.begin(), newFaces.end());
    for (int f : newFaces) {
        if (!E.faces[f].alive) continue;
        const Face& S = E.faces[f];
        BBox sb = faceBBox(E, f);
        for (int g = 0; g < (int)E.faces.size(); ++g) {
            if (g == f || !E.faces[g].alive || nf.count(g)) continue;
            if (!E.faces[g].plane.sameAs(S.plane)) continue;
            if (!bboxOverlap(sb, faceBBox(E, g), -kTol)) continue;
            planes.add(S.plane);
            break;
        }
    }
    for (auto& P : planes.planes) {
        std::vector<int> pe;
        for (int e = 0; e < (int)E.edges.size(); ++e)
            if (E.edges[e].alive && std::fabs(P.dist(E.verts[E.edges[e].v[0]].p)) < kPlaneTol &&
                std::fabs(P.dist(E.verts[E.edges[e].v[1]].p)) < kPlaneTol)
                pe.push_back(e);
        intersectEdges(E, pe);
        rebuildPlane(E, P, {}, {}, RebuildMode::MergeOnly);
    }
    mergeCoplanarAcross(E, mergeEdges);
}

int pushPull(Entities& E, int f, double dist, bool keepOriginal, std::vector<int>* createdFaces) {
    if (f < 0 || !E.faces[f].alive || std::fabs(dist) < kTol) return f;
    Face F0 = E.faces[f];
    Vec3 n = F0.plane.n;
    std::vector<int> V = E.faceVerts(f);
    std::unordered_set<int> Vs(V.begin(), V.end());

    // ---- stretch mode: every other face touching the face's vertices contains the push direction
    if (!keepOriginal) {
        bool stretch = true;
        bool anyAdjacent = false;
        double minLen = 1e300;
        for (int v : V) {
            for (int e : E.verts[v].edges) {
                const Edge& Ed = E.edges[e];
                if (!Ed.alive) continue;
                bool inF = std::find(Ed.faces.begin(), Ed.faces.end(), f) != Ed.faces.end();
                if (!inF) {
                    Vec3 d = E.verts[Ed.other(v)].p - E.verts[v].p;
                    double l = d.length();
                    if (Vs.count(Ed.other(v))) { stretch = false; break; }
                    if (d.cross(n).length() > 1e-6 * l) { stretch = false; break; }
                    // length along -n available when pushing in
                    if (d.dot(n) < 0) minLen = std::min(minLen, l);
                    else minLen = std::min(minLen, 1e300);
                }
                for (int g : Ed.faces) {
                    if (g == f || !E.faces[g].alive) continue;
                    anyAdjacent = true;
                    if (std::fabs(E.faces[g].plane.n.dot(n)) > 1e-6) { stretch = false; break; }
                }
                if (!stretch) break;
            }
            if (!stretch) break;
        }
        if (stretch && anyAdjacent) {
            if (dist < 0 && -dist >= minLen - kTol) {
                // Pushing all the way through collapses the solid: refuse beyond the limit.
                dist = -(minLen - kTol * 2);
                if (dist >= 0) return f;
            }
            Vec3 off = n * dist;
            for (int v : V) E.moveVertex(v, E.verts[v].p + off);
            Face& F = E.faces[f];
            F.plane = Plane(F.plane.n, E.verts[F.outer[0]].p);
            // adjacent faces keep their planes (they contain n)
            for (int v : V) dedupeEdgesAt(E, v);
            E.invalidateIndex();
            E.touch();
            return f;
        }
    }

    // ---- extrusion mode
    bool lone = false;
    {
        auto fe = E.faceEdges(f);
        for (int e : fe)
            if (E.edges[e].faces.size() < 2) { lone = true; break; }
    }
    int s = (lone || dist > 0) ? 1 : -1;
    bool forward = (dist > 0) != (s < 0);  // side quad order (a,b,b',a')
    Vec3 off = n * dist;

    std::vector<std::vector<int>> loops;
    loops.push_back(F0.outer);
    for (auto& h : F0.holes) loops.push_back(h);

    // Does the cap land on an existing coplanar face? (punch through)
    Plane capPlane(n, E.verts[F0.outer[0]].p + off);
    int punchFace = -1;
    {
        std::vector<Vec3> capPts;
        for (int v : F0.outer) capPts.push_back(E.verts[v].p + off);
        Vec3 c;
        for (auto& p : capPts) c += p;
        c = c / (double)capPts.size();
        for (int g = 0; g < (int)E.faces.size(); ++g) {
            if (g == f || !E.faces[g].alive || !E.faces[g].plane.sameAs(capPlane)) continue;
            if (E.pointInFace(g, c)) { punchFace = g; break; }
            for (auto& p : capPts)
                if (E.pointInFace(g, p)) { punchFace = g; break; }
            if (punchFace >= 0) break;
        }
    }

    // Capture the original boundary edges before topology changes (for merges).
    std::vector<int> boundary = E.faceEdges(f);

    // Create top vertices.
    std::vector<std::vector<int>> top;
    for (auto& L : loops) {
        std::vector<int> t;
        for (int v : L) t.push_back(E.vertexAt(E.verts[v].p + off));
        top.push_back(t);
    }
    std::vector<int> created;
    for (size_t li = 0; li < loops.size(); ++li) {
        auto& L = loops[li];
        auto& T = top[li];
        for (size_t i = 0; i < L.size(); ++i) {
            size_t j = (i + 1) % L.size();
            if (forward) addQuad(E, L[i], L[j], T[j], T[i], created);
            else addQuad(E, T[i], T[j], L[j], L[i], created);
            // soften side seams for curved profiles
            int e0 = E.findEdge(L[i], L[j]);
            int vert = E.findEdge(L[i], T[i]);
            if (e0 >= 0 && vert >= 0 && E.edges[e0].curve >= 0) {
                // the vertical edge between two segments of the same curve is soft
                int prev = E.findEdge(L[(i + L.size() - 1) % L.size()], L[i]);
                if (prev >= 0 && E.edges[prev].curve == E.edges[e0].curve) {
                    E.edges[vert].soft = E.edges[vert].smooth = true;
                }
                int te = E.findEdge(T[i], T[j]);
                if (te >= 0 && E.edges[te].curve < 0) {
                    // replicate the curve on the cap
                    const Curve& oc = E.curves[E.edges[e0].curve];
                    Curve nc = oc;
                    nc.center = oc.center + off;
                    E.curves.push_back(nc);
                    int cid = (int)E.curves.size() - 1;
                    for (size_t k = 0; k < L.size(); ++k) {
                        int ek = E.findEdge(L[k], L[(k + 1) % L.size()]);
                        int tk = E.findEdge(T[k], T[(k + 1) % T.size()]);
                        if (ek >= 0 && tk >= 0 && E.edges[ek].curve == E.edges[e0].curve) E.edges[tk].curve = cid;
                    }
                }
            }
        }
    }
    // Cap
    int cap = -1;
    int capSign = s * (dist > 0 ? 1 : -1);
    if (punchFace >= 0) {
        std::vector<int> capEdges;
        for (auto& T : top)
            for (size_t i = 0; i < T.size(); ++i) {
                int e = E.findEdge(T[i], T[(i + 1) % T.size()]);
                if (e >= 0) capEdges.push_back(e);
            }
        std::unordered_set<int> ce(capEdges.begin(), capEdges.end());
        Plane pp = E.faces[punchFace].plane;
        auto res = rebuildPlane(E, pp, ce, {}, RebuildMode::Draw);
        // delete the region that equals the cap outer loop
        std::vector<int> want = top[0];
        std::sort(want.begin(), want.end());
        for (int g : res) {
            if (!E.faces[g].alive) continue;
            std::vector<int> o = E.faces[g].outer;
            std::sort(o.begin(), o.end());
            if (o == want) { E.removeFace(g); break; }
        }
    } else {
        std::vector<int> outer = top[0];
        std::vector<std::vector<int>> holes(top.begin() + 1, top.end());
        Plane pl(n, E.verts[outer[0]].p);
        if (capSign < 0) {
            std::reverse(outer.begin(), outer.end());
            for (auto& h : holes) std::reverse(h.begin(), h.end());
            pl.n = -pl.n; pl.d = -pl.d;
        }
        cap = E.addFace(outer, holes, pl);
        if (cap >= 0) {
            Face& C = E.faces[cap];
            if (capSign > 0) { C.matFront = F0.matFront; C.matBack = F0.matBack; C.texFront = F0.texFront; C.texBack = F0.texBack; }
            else { C.matFront = F0.matBack; C.matBack = F0.matFront; C.texFront = F0.texBack; C.texBack = F0.texFront; }
            C.tag = F0.tag;
            created.push_back(cap);
        }
    }
    // Side faces take the front material of the pushed face's front (SketchUp-like behaviour).
    for (int g : created)
        if (g != cap && E.faces[g].alive) {
            E.faces[g].matFront = F0.matFront;
            E.faces[g].matBack = F0.matBack;
            E.faces[g].tag = F0.tag;
        }

    // Original face
    if (E.faces[f].alive) {
        if (keepOriginal) {
            // keep as-is (new starting face)
        } else if (lone) {
            int want = -(dist > 0 ? 1 : -1);  // original should face away from the swept volume
            if (want < 0) reverseFace(E, f);
        } else {
            E.removeFace(f);
        }
    }
    if (createdFaces) *createdFaces = created;
    resolveCoplanarOverlaps(E, created, boundary);
    E.touch();
    if (cap >= 0 && E.faces[cap].alive) return cap;
    // Find the face lying on the cap plane that contains the cap centroid (after merges).
    Vec3 c;
    for (int v : top[0]) c += E.verts[v].p;
    c = c / (double)top[0].size();
    for (int g = 0; g < (int)E.faces.size(); ++g)
        if (E.faces[g].alive && E.faces[g].plane.sameAs(capPlane) && E.pointInFace(g, c)) return g;
    return -1;
}

// ------------------------------------------------------------------ offset

static bool offsetLoop2(const std::vector<Vec2>& L, double d, std::vector<Vec2>& out) {
    // L is CCW for outer loops; positive d moves inward (to the left of each edge).
    size_t n = L.size();
    out.clear();
    for (size_t i = 0; i < n; ++i) {
        Vec2 p0 = L[(i + n - 1) % n], p1 = L[i], p2 = L[(i + 1) % n];
        Vec2 d1 = p1 - p0, d2 = p2 - p1;
        double l1 = d1.length(), l2 = d2.length();
        if (l1 < 1e-12 || l2 < 1e-12) return false;
        d1 = d1 * (1 / l1); d2 = d2 * (1 / l2);
        Vec2 n1(-d1.y, d1.x), n2(-d2.y, d2.x);  // left normals
        Vec2 a = p0 + n1 * d, b = p2 + n2 * d;
        // intersect line (a, d1) with (b, d2)
        double den = d1.cross(d2);
        if (std::fabs(den) < 1e-9) { out.push_back(p1 + n1 * d); continue; }
        double t = (b - a).cross(d2) / den;
        out.push_back(a + d1 * t);
    }
    return true;
}

std::vector<std::vector<Vec3>> offsetFaceLoops(const Entities& E, int f, double dist) {
    std::vector<std::vector<Vec3>> newLoops;
    if (f < 0 || !E.faces[f].alive) return newLoops;
    const Face& F = E.faces[f];
    Frame2 fr(F.plane);
    auto doLoop = [&](const std::vector<int>& L, double d) {
        std::vector<Vec2> q, o;
        for (int v : L) q.push_back(fr.to2(E.verts[v].p));
        if (!offsetLoop2(q, d, o)) return;
        std::vector<Vec3> r;
        for (auto& p : o) r.push_back(F.plane.project(fr.to3(p)));
        newLoops.push_back(r);
    };
    doLoop(F.outer, dist);
    for (auto& h : F.holes) doLoop(h, dist);
    return newLoops;
}

bool offsetFace(Entities& E, int f, double dist) {
    if (f < 0 || !E.faces[f].alive) return false;
    std::vector<std::vector<Vec3>> newLoops = offsetFaceLoops(E, f, dist);
    // Curves on offset loops keep being a curve.
    std::vector<int> all;
    for (auto& L : newLoops) {
        Curve c;
        c.closed = true;
        E.curves.push_back(c);
        int cid = (int)E.curves.size() - 1;
        for (size_t i = 0; i < L.size(); ++i) {
            std::vector<int> cr;
            auto p = insertSegment(E, L[i], L[(i + 1) % L.size()], &cr);
            all.insert(all.end(), p.begin(), p.end());
            for (int e : cr) E.edges[e].curve = cid;
        }
    }
    findFaces(E, all);
    return true;
}

bool offsetEdges(Entities& E, const std::vector<int>& edges, double dist, const Vec3& planeNormal) {
    // Orders connected edges into a chain and offsets it in the plane.
    if (edges.empty()) return false;
    std::unordered_map<int, std::vector<int>> adj;
    for (int e : edges) {
        adj[E.edges[e].v[0]].push_back(e);
        adj[E.edges[e].v[1]].push_back(e);
    }
    int start = E.edges[edges[0]].v[0];
    for (auto& [v, es] : adj)
        if (es.size() == 1) { start = v; break; }
    std::vector<int> chain = {start};
    std::unordered_set<int> used;
    int cur = start;
    while (true) {
        int nx = -1;
        for (int e : adj[cur])
            if (!used.count(e)) { nx = e; break; }
        if (nx < 0) break;
        used.insert(nx);
        cur = E.edges[nx].other(cur);
        chain.push_back(cur);
    }
    bool closed = chain.size() > 2 && chain.front() == chain.back();
    if (closed) chain.pop_back();
    std::vector<Vec3> pts;
    for (int v : chain) pts.push_back(E.verts[v].p);
    Plane P(planeNormal, pts[0]);
    Frame2 fr(P);
    std::vector<Vec2> q;
    for (auto& p : pts) q.push_back(fr.to2(p));
    std::vector<Vec2> o;
    size_t n = q.size();
    for (size_t i = 0; i < n; ++i) {
        bool hasPrev = closed || i > 0, hasNext = closed || i + 1 < n;
        Vec2 p1 = q[i];
        Vec2 d1, d2;
        if (hasPrev) d1 = (p1 - q[(i + n - 1) % n]) * (1 / (p1 - q[(i + n - 1) % n]).length());
        if (hasNext) d2 = (q[(i + 1) % n] - p1) * (1 / (q[(i + 1) % n] - p1).length());
        if (!hasPrev) d1 = d2;
        if (!hasNext) d2 = d1;
        Vec2 n1(-d1.y, d1.x), n2(-d2.y, d2.x);
        double den = d1.cross(d2);
        if (std::fabs(den) < 1e-9) { o.push_back(p1 + n1 * dist); continue; }
        Vec2 a = p1 + n1 * dist, b = p1 + n2 * dist;
        double t = (b - a).cross(d2) / den;
        o.push_back(a + d1 * t);
    }
    std::vector<Vec3> r;
    for (auto& p : o) r.push_back(fr.to3(p));
    addPolyline(E, r, closed);
    return true;
}

// ------------------------------------------------------------------ follow me

static bool sweepRings(Entities& E, int f, const std::vector<std::vector<std::vector<Vec3>>>& rings, bool closed,
                       const std::vector<bool>& smoothRing) {
    const Face F0 = E.faces[f];
    size_t nr = rings.size();
    if (nr < 2) return false;
    // Direction of first step decides orientation.
    Vec3 c0, c1;
    for (auto& p : rings[0][0]) c0 += p;
    for (auto& p : rings[1][0]) c1 += p;
    c0 = c0 / (double)rings[0][0].size();
    c1 = c1 / (double)rings[1][0].size();
    Vec3 n = F0.plane.n;
    bool forward = (c1 - c0).dot(n) > 0;
    // Vertices
    std::vector<std::vector<std::vector<int>>> rv(nr);
    for (size_t r = 0; r < nr; ++r)
        for (auto& L : rings[r]) {
            std::vector<int> ids;
            for (auto& p : L) ids.push_back(E.vertexAt(p));
            rv[r].push_back(ids);
        }
    // Profile curve flags (soft longitudinal edges at curve vertices)
    std::vector<std::vector<bool>> softProfile;
    {
        std::vector<std::vector<int>> loops = {F0.outer};
        for (auto& h : F0.holes) loops.push_back(h);
        for (auto& L : loops) {
            std::vector<bool> s(L.size(), false);
            for (size_t i = 0; i < L.size(); ++i) {
                int ep = E.findEdge(L[(i + L.size() - 1) % L.size()], L[i]);
                int en = E.findEdge(L[i], L[(i + 1) % L.size()]);
                if (ep >= 0 && en >= 0 && E.edges[ep].curve >= 0 && E.edges[ep].curve == E.edges[en].curve) s[i] = true;
            }
            softProfile.push_back(s);
        }
    }
    std::vector<int> created;
    size_t steps = closed ? nr : nr - 1;
    for (size_t r = 0; r < steps; ++r) {
        size_t r2 = (r + 1) % nr;
        for (size_t li = 0; li < rv[r].size(); ++li) {
            auto& A = rv[r][li];
            auto& B = rv[r2][li];
            for (size_t i = 0; i < A.size(); ++i) {
                size_t j = (i + 1) % A.size();
                if (forward) addQuad(E, A[i], A[j], B[j], B[i], created);
                else addQuad(E, B[i], B[j], A[j], A[i], created);
                if (li < softProfile.size() && softProfile[li][i]) {
                    int e = E.findEdge(A[i], B[i]);
                    if (e >= 0) E.edges[e].soft = E.edges[e].smooth = true;
                }
            }
            if (smoothRing[r2] && (closed || r2 != nr - 1)) {
                for (size_t i = 0; i < B.size(); ++i) {
                    int e = E.findEdge(B[i], B[(i + 1) % B.size()]);
                    if (e >= 0) E.edges[e].soft = E.edges[e].smooth = true;
                }
            }
        }
    }
    for (int g : created) { E.faces[g].matFront = F0.matFront; E.faces[g].matBack = F0.matBack; }
    if (closed) {
        E.removeFace(f);
    } else {
        // end cap
        auto& last = rv[nr - 1];
        std::vector<int> outer = last[0];
        std::vector<std::vector<int>> holes(last.begin() + 1, last.end());
        Vec3 nn = newellNormal(E, outer);
        Vec3 cl, cp;
        for (auto& p : rings[nr - 1][0]) cl += p;
        for (auto& p : rings[nr - 2][0]) cp += p;
        Vec3 dirOut = (cl - cp);
        if (nn.dot(dirOut) < 0) {
            std::reverse(outer.begin(), outer.end());
            for (auto& h : holes) std::reverse(h.begin(), h.end());
            nn = -nn;
        }
        int cap = E.addFace(outer, holes, Plane(nn, E.verts[outer[0]].p));
        if (cap >= 0) { E.faces[cap].matFront = F0.matFront; E.faces[cap].matBack = F0.matBack; }
        // start face points away from sweep
        if (E.faces[f].alive && E.faces[f].plane.n.dot(c1 - c0) > 0) reverseFace(E, f);
    }
    E.touch();
    return true;
}

bool followMe(Entities& E, int f, std::vector<Vec3> path, bool closedPath) {
    if (f < 0 || !E.faces[f].alive || path.size() < 2) return false;
    // remove duplicates
    std::vector<Vec3> p;
    for (auto& q : path)
        if (p.empty() || !p.back().near(q)) p.push_back(q);
    if (closedPath && p.size() > 2 && p.front().near(p.back())) p.pop_back();
    if (p.size() < 2) return false;
    size_t n = p.size();
    const Face F0 = E.faces[f];
    std::vector<std::vector<Vec3>> profile;
    {
        std::vector<int> l0 = F0.outer;
        std::vector<Vec3> q;
        for (int v : l0) q.push_back(E.verts[v].p);
        profile.push_back(q);
        for (auto& h : F0.holes) {
            q.clear();
            for (int v : h) q.push_back(E.verts[v].p);
            profile.push_back(q);
        }
    }
    auto dir = [&](size_t i) { return (p[(i + 1) % n] - p[i]).normalized(); };
    size_t nseg = closedPath ? n : n - 1;
    std::vector<Vec3> d(nseg);
    for (size_t i = 0; i < nseg; ++i) d[i] = dir(i);
    // Miter plane at vertex i
    auto miter = [&](size_t i) -> Plane {
        if (!closedPath && i == 0) return Plane(d[0], p[0]);
        if (!closedPath && i == n - 1) return Plane(d[nseg - 1], p[n - 1]);
        Vec3 a = d[(i + nseg - 1) % nseg], b = d[i % nseg];
        Vec3 m = (a + b);
        if (m.length() < 1e-9) m = a;
        return Plane(m.normalized(), p[i]);
    };
    auto project = [&](const std::vector<std::vector<Vec3>>& ring, const Vec3& dd, const Plane& pl) {
        std::vector<std::vector<Vec3>> r = ring;
        for (auto& L : r)
            for (auto& q : L) {
                Ray ray{q, dd};
                double t = ray.hit(pl);
                if (!std::isnan(t)) q = ray.at(t);
            }
        return r;
    };
    std::vector<std::vector<std::vector<Vec3>>> rings;
    std::vector<bool> smooth;
    std::vector<std::vector<Vec3>> cur = profile;
    if (closedPath) cur = project(profile, d[0], miter(0));
    rings.push_back(cur);
    smooth.push_back(false);
    for (size_t i = 1; i < n; ++i) {
        cur = project(cur, d[i - 1], miter(i));
        rings.push_back(cur);
        double ang = (i < nseg) ? std::acos(std::clamp(d[i - 1].dot(d[i]), -1.0, 1.0)) : kPi;
        smooth.push_back(ang < 25 * kPi / 180);
    }
    if (closedPath) {
        double ang = std::acos(std::clamp(d[nseg - 1].dot(d[0]), -1.0, 1.0));
        smooth[0] = ang < 25 * kPi / 180;
    }
    return sweepRings(E, f, rings, closedPath, smooth);
}

bool followMeAround(Entities& E, int f, const Vec3& center, const Vec3& axis, int segments) {
    if (f < 0 || !E.faces[f].alive || segments < 3) return false;
    const Face F0 = E.faces[f];
    std::vector<std::vector<Vec3>> profile;
    {
        std::vector<Vec3> q;
        for (int v : F0.outer) q.push_back(E.verts[v].p);
        profile.push_back(q);
        for (auto& h : F0.holes) {
            q.clear();
            for (int v : h) q.push_back(E.verts[v].p);
            profile.push_back(q);
        }
    }
    std::vector<std::vector<std::vector<Vec3>>> rings;
    std::vector<bool> smooth;
    for (int k = 0; k < segments; ++k) {
        Mat4 m = Mat4::rotationAbout(center, axis, 2 * kPi * k / segments);
        auto r = profile;
        for (auto& L : r)
            for (auto& q : L) q = m.point(q);
        rings.push_back(r);
        smooth.push_back(true);
    }
    return sweepRings(E, f, rings, true, smooth);
}

// ------------------------------------------------------------------ intersect

void intersectEdges(Entities& E, std::vector<int> queue) {
    for (size_t qi = 0; qi < queue.size(); ++qi) {
        int e = queue[qi];
        if (e < 0 || !E.edges[e].alive) continue;
        Vec3 a = E.verts[E.edges[e].v[0]].p, b = E.verts[E.edges[e].v[1]].p;
        double len = (b - a).length();
        if (len < kTol) continue;
        Vec3 d = (b - a) / len;
        BBox sb; sb.add(a); sb.add(b);
        std::vector<std::pair<double, Vec3>> mySplits;
        int nE = (int)E.edges.size();
        for (int e2 = 0; e2 < nE; ++e2) {
            if (e2 == e || !E.edges[e2].alive) continue;
            Vec3 p0 = E.verts[E.edges[e2].v[0]].p, p1 = E.verts[E.edges[e2].v[1]].p;
            BBox eb; eb.add(p0); eb.add(p1);
            if (!bboxOverlap(sb, eb, kTol)) continue;
            Vec3 ed = p1 - p0;
            double el = ed.length();
            if (el < kTol) continue;
            ed = ed / el;
            if (d.cross(ed).length() < 1e-9) {
                Vec3 w = p0 - a;
                if ((w - d * w.dot(d)).length() > kTol) continue;
                for (const Vec3& q : {p0, p1}) {
                    double t = (q - a).dot(d);
                    if (t > kTol && t < len - kTol) mySplits.push_back({t, q});
                }
                std::vector<std::pair<double, Vec3>> other;
                for (const Vec3& X : {a, b}) {
                    double s = (X - p0).dot(ed);
                    if (s > kTol && s < el - kTol) other.push_back({s, X});
                }
                std::sort(other.begin(), other.end(), [](auto& x, auto& y) { return x.first > y.first; });
                for (auto& o : other) {
                    int before = (int)E.edges.size();
                    splitEdge(E, e2, o.second);
                    for (int k = before; k < (int)E.edges.size(); ++k) queue.push_back(k);
                }
            } else {
                double t, s;
                if (!closestLineLine(a, d, p0, ed, t, s)) continue;
                if (t < -kTol || t > len + kTol || s < -kTol || s > el + kTol) continue;
                Vec3 X = p0 + ed * s;
                if ((a + d * t - X).length() > kTol) continue;
                if (t > kTol && t < len - kTol) mySplits.push_back({t, X});
                if (s > kTol && s < el - kTol) {
                    int before = (int)E.edges.size();
                    splitEdge(E, e2, X);
                    for (int k = before; k < (int)E.edges.size(); ++k) queue.push_back(k);
                }
            }
        }
        std::sort(mySplits.begin(), mySplits.end(), [](auto& x, auto& y) { return x.first > y.first; });
        double lastT = -1;
        for (auto& sp : mySplits) {
            if (lastT >= 0 && std::fabs(lastT - sp.first) < kTol) continue;
            lastT = sp.first;
            int before = (int)E.edges.size();
            splitEdge(E, e, sp.second);
            for (int k = before; k < (int)E.edges.size(); ++k) queue.push_back(k);
        }
    }
}

// Intervals of line (o + t*dir) lying inside face f.
static std::vector<std::pair<double, double>> lineInFace(const Entities& E, int f, const Vec3& o, const Vec3& dir) {
    const Face& F = E.faces[f];
    Frame2 fr(F.plane);
    Vec2 O = fr.to2(o);
    Vec2 D(dir.dot(fr.u), dir.dot(fr.v));
    double dl = D.length();
    if (dl < 1e-12) return {};
    Vec2 Dn = D * (1 / dl);
    Vec2 N(-Dn.y, Dn.x);
    std::vector<double> ts;
    auto doLoop = [&](const std::vector<int>& L) {
        for (size_t i = 0; i < L.size(); ++i) {
            Vec2 p = fr.to2(E.verts[L[i]].p), q = fr.to2(E.verts[L[(i + 1) % L.size()]].p);
            double sp = (p - O).dot(N), sq = (q - O).dot(N);
            if ((sp > 0) != (sq > 0)) {
                double k = sp / (sp - sq);
                Vec2 X = p + (q - p) * k;
                ts.push_back((X - O).dot(Dn) / dl);
            }
        }
    };
    doLoop(F.outer);
    for (auto& h : F.holes) doLoop(h);
    std::sort(ts.begin(), ts.end());
    std::vector<std::pair<double, double>> iv;
    for (size_t i = 0; i + 1 < ts.size(); i += 2) iv.push_back({ts[i], ts[i + 1]});
    return iv;
}

std::vector<std::pair<Vec3, Vec3>> faceFaceSegments(const Entities& A, int fa, const Entities& B, int fb) {
    std::vector<std::pair<Vec3, Vec3>> segs;
    if (!bboxOverlap(faceBBox(A, fa), faceBBox(B, fb), kTol)) return segs;
    const Plane& PA = A.faces[fa].plane;
    const Plane& PB = B.faces[fb].plane;
    Vec3 dir = PA.n.cross(PB.n);
    if (dir.length() < 1e-9) return segs;
    dir = dir.normalized();
    Vec3 n1 = PA.n, n2 = PB.n;
    double d1 = -PA.d, d2 = -PB.d;
    double n1n2 = n1.dot(n2);
    double det = 1 - n1n2 * n1n2;
    Vec3 o = n1 * ((d1 - d2 * n1n2) / det) + n2 * ((d2 - d1 * n1n2) / det);
    auto ia = lineInFace(A, fa, o, dir);
    auto ib = lineInFace(B, fb, o, dir);
    for (auto& x : ia)
        for (auto& y : ib) {
            double lo = std::max(x.first, y.first), hi = std::min(x.second, y.second);
            if (hi - lo > kTol) segs.push_back({o + dir * lo, o + dir * hi});
        }
    return segs;
}

int intersectFaces(Entities& E, const std::vector<int>& facesA, const std::vector<int>& facesBIn) {
    std::vector<int> facesB = facesBIn;
    if (facesB.empty())
        for (int f = 0; f < (int)E.faces.size(); ++f)
            if (E.faces[f].alive) facesB.push_back(f);
    std::vector<std::pair<Vec3, Vec3>> segs;
    for (int fa : facesA) {
        if (!E.faces[fa].alive) continue;
        for (int fb : facesB) {
            if (fb == fa || !E.faces[fb].alive) continue;
            auto s = faceFaceSegments(E, fa, E, fb);
            segs.insert(segs.end(), s.begin(), s.end());
        }
    }
    std::vector<int> pieces;
    for (auto& s : segs) {
        auto p = insertSegment(E, s.first, s.second);
        pieces.insert(pieces.end(), p.begin(), p.end());
    }
    findFaces(E, pieces);
    return (int)segs.size();
}

// ------------------------------------------------------------------ transform

void fixWarpedFaces(Entities& E, const std::vector<int>& faces) {
    for (int f : faces) {
        if (f < 0 || !E.faces[f].alive) continue;
        Face& F = E.faces[f];
        Vec3 n = newellNormal(E, F.outer);
        if (n.isZero()) { E.removeFace(f); continue; }
        Plane P(n, E.verts[F.outer[0]].p);
        // average d for stability
        double dsum = 0;
        for (int v : F.outer) dsum += -n.dot(E.verts[v].p);
        P.d = dsum / F.outer.size();
        bool planar = true;
        for (int v : E.faceVerts(f))
            if (std::fabs(P.dist(E.verts[v].p)) > kPlaneTol) { planar = false; break; }
        if (planar) { F.plane = P; continue; }
        // Triangulate the warped face with soft, smooth diagonals.
        Face F0 = F;
        Frame2 fr(P);
        std::vector<std::vector<int>> loops = {F0.outer};
        for (auto& h : F0.holes) loops.push_back(h);
        std::vector<std::vector<Vec2>> l2;
        std::vector<int> ids;
        for (auto& L : loops) {
            std::vector<Vec2> q;
            for (int v : L) { q.push_back(fr.to2(E.verts[v].p)); ids.push_back(v); }
            l2.push_back(q);
        }
        TessResult tr;
        if (!tessellate(l2, tr)) continue;
        E.removeFace(f);
        for (size_t t = 0; t + 2 < tr.tris.size(); t += 3) {
            int tv[3];
            for (int k = 0; k < 3; ++k) {
                int idx = tr.tris[t + k];
                tv[k] = idx < (int)ids.size() ? ids[idx] : E.vertexAt(P.project(fr.to3(tr.pts[idx])));
            }
            if (tv[0] == tv[1] || tv[1] == tv[2] || tv[0] == tv[2]) continue;
            int nf = addFaceFromVerts(E, {tv[0], tv[1], tv[2]});
            if (nf < 0) continue;
            Face& NF = E.faces[nf];
            NF.matFront = F0.matFront; NF.matBack = F0.matBack; NF.tag = F0.tag; NF.hidden = F0.hidden;
            NF.texFront = F0.texFront; NF.texBack = F0.texBack;
            for (int k = 0; k < 3; ++k) {
                int a = tv[k], b = tv[(k + 1) % 3];
                bool boundary = false;
                for (auto& L : loops)
                    for (size_t i = 0; i < L.size(); ++i) {
                        int x = L[i], y = L[(i + 1) % L.size()];
                        if ((x == a && y == b) || (x == b && y == a)) boundary = true;
                    }
                if (!boundary) {
                    int e = E.findEdge(a, b);
                    if (e >= 0) E.edges[e].soft = E.edges[e].smooth = true;
                }
            }
        }
    }
}

void transformVertices(Entities& E, const std::vector<int>& verts, const Mat4& m, bool heal) {
    std::set<int> faces;
    std::unordered_set<int> vs(verts.begin(), verts.end());
    for (int v : verts)
        for (int e : E.verts[v].edges)
            for (int f : E.edges[e].faces) faces.insert(f);
    for (int v : verts) E.verts[v].p = m.point(E.verts[v].p);
    E.invalidateIndex();
    bool mirror = m.det3() < 0;
    if (mirror)
        for (int f : faces) {
            bool all = true;
            for (int v : E.faceVerts(f))
                if (!vs.count(v)) { all = false; break; }
            if (all) {
                Face& F = E.faces[f];
                std::reverse(F.outer.begin(), F.outer.end());
                for (auto& h : F.holes) std::reverse(h.begin(), h.end());
            }
        }
    // Textures of faces that move as a whole move with them.
    for (int f : faces) {
        Face& F = E.faces[f];
        if (!F.alive) continue;
        bool all = true;
        for (int v : E.faceVerts(f))
            if (!vs.count(v)) { all = false; break; }
        if (!all) continue;
        // (a face drawn with its own frame gets that placement made explicit, so it stays put on the face)
        F.texFront = transformTexMap(effectiveTexMap(F, false), m);
        F.texBack = transformTexMap(effectiveTexMap(F, true), m);
    }
    // Curves: transform when all their edges move.
    for (auto& c : E.curves) {
        (void)c;
    }
    std::vector<int> touched;
    if (heal) {
        for (int v : verts) {
            if (!E.verts[v].alive) continue;
            E.verts[v].alive = false;
            int w = E.findVertex(E.verts[v].p);
            E.verts[v].alive = true;
            if (w >= 0 && w != v) {
                mergeVertices(E, w, v);
                touched.push_back(w);
            }
        }
    }
    std::vector<int> fl(faces.begin(), faces.end());
    fixWarpedFaces(E, fl);
    if (heal) {
        std::vector<int> es;
        for (int v : verts)
            if (E.verts[v].alive)
                for (int e : E.verts[v].edges) es.push_back(e);
        for (int v : touched)
            for (int e : E.verts[v].edges) es.push_back(e);
        if (es.size() < 400) intersectEdges(E, es);
        std::vector<int> ff;
        for (int v : touched)
            if (E.verts[v].alive)
                for (int e : E.verts[v].edges) ff.push_back(e);
        if (!ff.empty()) findFaces(E, ff);
    }
    E.touch();
}

// ------------------------------------------------------------------ triangles

void faceTriangles(const Entities& E, int f, std::vector<Vec3>& out) {
    const Face& F = E.faces[f];
    if (F.outer.size() == 3 && F.holes.empty()) {
        out.push_back(E.verts[F.outer[0]].p);
        out.push_back(E.verts[F.outer[1]].p);
        out.push_back(E.verts[F.outer[2]].p);
        return;
    }
    Frame2 fr(F.plane);
    std::vector<std::vector<Vec2>> l2;
    std::vector<int> ids;
    auto add = [&](const std::vector<int>& L) {
        std::vector<Vec2> q;
        for (int v : L) { q.push_back(fr.to2(E.verts[v].p)); ids.push_back(v); }
        l2.push_back(q);
    };
    add(F.outer);
    for (auto& h : F.holes) add(h);
    TessResult tr;
    if (!tessellate(l2, tr)) return;
    for (int idx : tr.tris)
        out.push_back(idx < (int)ids.size() ? E.verts[ids[idx]].p : fr.to3(tr.pts[idx]));
}

// ------------------------------------------------------------------ selection helpers

std::vector<EntRef> closeSelection(const Entities& E, const std::vector<EntRef>& sel) {
    std::set<EntRef> s(sel.begin(), sel.end());
    for (auto& r : sel)
        if (r.kind == EntKind::Face && E.faces[r.id].alive)
            for (int e : E.faceEdges(r.id)) s.insert({EntKind::Edge, e});
    return std::vector<EntRef>(s.begin(), s.end());
}

std::vector<int> selectionVertices(const Entities& E, const std::vector<EntRef>& sel) {
    std::set<int> vs;
    for (auto& r : sel) {
        if (r.kind == EntKind::Edge && E.edges[r.id].alive) { vs.insert(E.edges[r.id].v[0]); vs.insert(E.edges[r.id].v[1]); }
        else if (r.kind == EntKind::Face && E.faces[r.id].alive)
            for (int v : E.faceVerts(r.id)) vs.insert(v);
        else if (r.kind == EntKind::Vertex && E.verts[r.id].alive) vs.insert(r.id);
    }
    return std::vector<int>(vs.begin(), vs.end());
}

std::vector<EntRef> connected(const Entities& E, EntRef start, bool all) {
    std::set<EntRef> out;
    if (!all) {
        out.insert(start);
        if (start.kind == EntKind::Edge) {
            for (int f : E.edges[start.id].faces) out.insert({EntKind::Face, f});
        } else if (start.kind == EntKind::Face) {
            for (int e : E.faceEdges(start.id)) out.insert({EntKind::Edge, e});
        }
        return std::vector<EntRef>(out.begin(), out.end());
    }
    std::queue<int> vq;
    std::unordered_set<int> seenV;
    auto pushV = [&](int v) { if (seenV.insert(v).second) vq.push(v); };
    if (start.kind == EntKind::Edge) { pushV(E.edges[start.id].v[0]); pushV(E.edges[start.id].v[1]); }
    else if (start.kind == EntKind::Face) for (int v : E.faceVerts(start.id)) pushV(v);
    while (!vq.empty()) {
        int v = vq.front(); vq.pop();
        for (int e : E.verts[v].edges) {
            if (!E.edges[e].alive) continue;
            out.insert({EntKind::Edge, e});
            pushV(E.edges[e].other(v));
            for (int f : E.edges[e].faces)
                if (E.faces[f].alive) out.insert({EntKind::Face, f});
        }
    }
    return std::vector<EntRef>(out.begin(), out.end());
}

std::vector<int> curveEdges(const Entities& E, int curve) {
    std::vector<int> r;
    if (curve < 0) return r;
    for (int e = 0; e < (int)E.edges.size(); ++e)
        if (E.edges[e].alive && E.edges[e].curve == curve) r.push_back(e);
    return r;
}

// ------------------------------------------------------------------ copy

std::vector<EntRef> copyEntities(const Entities& srcIn, const std::vector<EntRef>& selIn, Entities& dst, const Mat4& xf,
                                 bool sticky, CopyMap* mapOut) {
    // Work from a snapshot in case src == dst.
    const bool same = &srcIn == &dst;
    Entities snapshot;
    if (same) snapshot = srcIn;
    const Entities& src = same ? snapshot : srcIn;
    std::vector<EntRef> sel = closeSelection(src, selIn);
    CopyMap local;
    CopyMap& M = mapOut ? *mapOut : local;
    std::vector<EntRef> result;
    bool mirror = xf.det3() < 0;
    bool touchedExisting = false;
    size_t vertsBefore = dst.verts.size();
    auto mapV = [&](int v) {
        auto it = M.verts.find(v);
        if (it != M.verts.end()) return it->second;
        Vec3 p = xf.point(src.verts[v].p);
        int nv = sticky ? dst.vertexAt(p) : dst.addVertex(p);
        if (nv < (int)vertsBefore) touchedExisting = true;
        M.verts[v] = nv;
        return nv;
    };
    auto mapCurve = [&](int c) {
        if (c < 0) return -1;
        auto it = M.curves.find(c);
        if (it != M.curves.end()) return it->second;
        Curve nc = src.curves[c];
        nc.center = xf.point(nc.center);
        nc.normal = xf.vector(nc.normal).normalized();
        nc.xaxis = xf.vector(nc.xaxis).normalized();
        double sc = xf.vector(src.curves[c].xaxis).length();
        nc.radius *= sc;
        dst.curves.push_back(nc);
        int id = (int)dst.curves.size() - 1;
        M.curves[c] = id;
        return id;
    };
    std::vector<int> newEdges;
    for (auto& r : sel) {
        if (r.kind != EntKind::Edge || !src.edges[r.id].alive) continue;
        const Edge& Ed = src.edges[r.id];
        int a = mapV(Ed.v[0]), b = mapV(Ed.v[1]);
        int e = dst.addEdge(a, b);
        if (e < 0) continue;
        Edge& N = dst.edges[e];
        N.soft = Ed.soft; N.smooth = Ed.smooth; N.hidden = Ed.hidden; N.tag = Ed.tag; N.material = Ed.material;
        N.curve = mapCurve(Ed.curve);
        M.edges[r.id] = e;
        newEdges.push_back(e);
        result.push_back({EntKind::Edge, e});
    }
    auto xfTex = [&](TexMap t) {
        if (!t.custom) return t;
        // Re-express texture axes in the new frame: uv(p') = uv(p) with p = xf^-1 p'.
        Mat4 inv = xf.inverted();
        Vec3 o = inv.point(Vec3());
        Vec3 ua(inv.at(0, 0) * t.uAxis.x + inv.at(1, 0) * t.uAxis.y + inv.at(2, 0) * t.uAxis.z,
                inv.at(0, 1) * t.uAxis.x + inv.at(1, 1) * t.uAxis.y + inv.at(2, 1) * t.uAxis.z,
                inv.at(0, 2) * t.uAxis.x + inv.at(1, 2) * t.uAxis.y + inv.at(2, 2) * t.uAxis.z);
        Vec3 va(inv.at(0, 0) * t.vAxis.x + inv.at(1, 0) * t.vAxis.y + inv.at(2, 0) * t.vAxis.z,
                inv.at(0, 1) * t.vAxis.x + inv.at(1, 1) * t.vAxis.y + inv.at(2, 1) * t.vAxis.z,
                inv.at(0, 2) * t.vAxis.x + inv.at(1, 2) * t.vAxis.y + inv.at(2, 2) * t.vAxis.z);
        t.uOff += t.uAxis.dot(o);
        t.vOff += t.vAxis.dot(o);
        t.uAxis = ua; t.vAxis = va;
        return t;
    };
    for (auto& r : sel) {
        if (r.kind != EntKind::Face || !src.faces[r.id].alive) continue;
        const Face& F = src.faces[r.id];
        std::vector<int> outer;
        for (int v : F.outer) outer.push_back(mapV(v));
        std::vector<std::vector<int>> holes;
        for (auto& h : F.holes) {
            std::vector<int> hh;
            for (int v : h) hh.push_back(mapV(v));
            holes.push_back(hh);
        }
        if (mirror) {
            std::reverse(outer.begin(), outer.end());
            for (auto& h : holes) std::reverse(h.begin(), h.end());
        }
        Vec3 n = newellNormal(dst, outer);
        int nf = dst.addFace(outer, holes, Plane(n, dst.verts[outer[0]].p));
        if (nf < 0) continue;
        Face& NF = dst.faces[nf];
        NF.matFront = F.matFront; NF.matBack = F.matBack;
        NF.texFront = xfTex(F.texFront); NF.texBack = xfTex(F.texBack);
        NF.hidden = F.hidden; NF.tag = F.tag;
        M.faces[r.id] = nf;
        result.push_back({EntKind::Face, nf});
    }
    for (auto& r : sel) {
        if (r.kind == EntKind::Instance && src.insts[r.id].alive) {
            Instance in = src.insts[r.id];
            in.xf = xf * in.xf;
            dst.insts.push_back(in);
            M.insts[r.id] = (int)dst.insts.size() - 1;
            result.push_back({EntKind::Instance, (int)dst.insts.size() - 1});
        } else if (r.kind == EntKind::Guide && src.guides[r.id].alive) {
            Guide g = src.guides[r.id];
            g.p = xf.point(g.p);
            g.dir = xf.vector(g.dir).normalized();
            dst.guides.push_back(g);
            result.push_back({EntKind::Guide, (int)dst.guides.size() - 1});
        } else if (r.kind == EntKind::Dimension && src.dims[r.id].alive) {
            Dimension d = src.dims[r.id];
            d.a = xf.point(d.a); d.b = xf.point(d.b); d.offset = xf.vector(d.offset);
            dst.dims.push_back(d);
            result.push_back({EntKind::Dimension, (int)dst.dims.size() - 1});
        } else if (r.kind == EntKind::Text && src.texts[r.id].alive) {
            Text t = src.texts[r.id];
            t.anchor = xf.point(t.anchor);
            t.leader = xf.vector(t.leader);
            dst.texts.push_back(t);
            result.push_back({EntKind::Text, (int)dst.texts.size() - 1});
        } else if (r.kind == EntKind::Section && src.sections[r.id].alive) {
            SectionPlane s = src.sections[r.id];
            Vec3 o = xf.point(s.plane.origin());
            s.plane = Plane(xf.normal(s.plane.n), o);
            dst.sections.push_back(s);
            result.push_back({EntKind::Section, (int)dst.sections.size() - 1});
        }
    }
    if (sticky && touchedExisting && !newEdges.empty()) {
        if (newEdges.size() < 1500) intersectEdges(dst, newEdges);
        // only edges touching pre-existing geometry can close new faces
        std::vector<int> touching;
        for (int e : newEdges) {
            if (!dst.edges[e].alive) continue;
            for (int k = 0; k < 2; ++k) {
                int v = dst.edges[e].v[k];
                if (v < (int)vertsBefore) { touching.push_back(e); break; }
            }
        }
        findFaces(dst, touching);
    }
    dst.touch();
    return result;
}

// ------------------------------------------------------------------ solids

bool isSolid(const Entities& E) {
    bool any = false;
    for (auto& e : E.edges) {
        if (!e.alive) continue;
        int n = 0;
        for (int f : e.faces)
            if (E.faces[f].alive) ++n;
        if (n != 2) return false;
        any = true;
    }
    for (auto& in : E.insts)
        if (in.alive) return false;
    return any;
}

static int liveFaces(const Entities& E, int e) {
    int n = 0;
    for (int f : E.edges[e].faces)
        if (E.faces[f].alive) ++n;
    return n;
}

// +1 if the face's loops walk a->b, -1 if b->a, 0 if the edge isn't on it
static int walkDir(const Face& F, int a, int b) {
    auto chk = [&](const std::vector<int>& L) {
        for (size_t k = 0; k < L.size(); ++k) {
            int x = L[k], y = L[(k + 1) % L.size()];
            if (x == a && y == b) return 1;
            if (x == b && y == a) return -1;
        }
        return 0;
    };
    int d = chk(F.outer);
    for (auto& h : F.holes)
        if (!d) d = chk(h);
    return d;
}

// Faces that would turn round to make each shell consistent and outward (closed shells only for
// the outward part). Shells are faces joined through edges with exactly two faces.
static std::vector<int> facesToReverse(const Entities& E) {
    std::vector<int> out;
    std::vector<int> shell(E.faces.size(), -1);
    std::vector<char> flip(E.faces.size(), 0);
    int nsh = 0;
    for (int s0 = 0; s0 < (int)E.faces.size(); ++s0) {
        if (!E.faces[s0].alive || shell[s0] >= 0) continue;
        std::vector<int> q{s0};
        shell[s0] = nsh;
        bool closed = true;
        for (size_t i = 0; i < q.size(); ++i) {
            int f = q[i];
            for (int e : E.faceEdges(f)) {
                const Edge& Ed = E.edges[e];
                if (liveFaces(E, e) != 2) { closed = false; continue; }
                int g = -1;
                for (int x : Ed.faces) if (x != f && E.faces[x].alive) g = x;
                if (g < 0 || shell[g] >= 0) continue;
                shell[g] = nsh;
                // neighbours walk their shared edge in opposite directions
                int df = walkDir(E.faces[f], Ed.v[0], Ed.v[1]) * (flip[f] ? -1 : 1);
                int dg = walkDir(E.faces[g], Ed.v[0], Ed.v[1]);
                flip[g] = df != 0 && df == dg;
                q.push_back(g);
            }
        }
        if (closed) {
            // outward: positive volume once the flips are made
            double v = 0;
            std::vector<Vec3> tris;
            for (int f : q) {
                tris.clear();
                faceTriangles(E, f, tris);
                double fv = 0;
                for (size_t i = 0; i + 2 < tris.size(); i += 3) fv += tris[i].dot(tris[i + 1].cross(tris[i + 2]));
                v += flip[f] ? -fv : fv;
            }
            if (v < 0)
                for (int f : q) flip[f] = !flip[f];
        } else {
            // an open shell: keep the larger part as it is
            size_t n = 0;
            for (int f : q) n += flip[f];
            if (n * 2 > q.size())
                for (int f : q) flip[f] = !flip[f];
        }
        for (int f : q)
            if (flip[f]) out.push_back(f);
        ++nsh;
    }
    return out;
}

SolidReport inspectSolid(const Entities& E) {
    SolidReport r;
    std::vector<int> border;
    std::set<int> inner;
    for (int f = 0; f < (int)E.faces.size(); ++f) {
        if (!E.faces[f].alive) continue;
        bool allShared = true;
        for (int e : E.faceEdges(f))
            if (liveFaces(E, e) < 3) { allShared = false; break; }
        if (allShared) { r.innerFaces.push_back(f); inner.insert(f); }
    }
    for (int e = 0; e < (int)E.edges.size(); ++e) {
        const Edge& Ed = E.edges[e];
        if (!Ed.alive) continue;
        int n = liveFaces(E, e);
        if (n == 0) r.strayEdges.push_back(e);
        else if (n == 1) border.push_back(e);
        else if (n >= 3) {
            // still too many once the inner faces are gone?
            int m = 0;
            for (int f : Ed.faces) if (E.faces[f].alive && !inner.count(f)) ++m;
            if (m >= 3) r.extraEdges.push_back(e);
        }
    }
    // border edges into runs: walk from vertex to vertex along unused border edges
    std::map<int, std::vector<int>> at;
    for (int e : border) { at[E.edges[e].v[0]].push_back(e); at[E.edges[e].v[1]].push_back(e); }
    std::set<int> used;
    for (int e0 : border) {
        if (used.count(e0)) continue;
        std::vector<int> run{e0};
        used.insert(e0);
        for (int dir = 0; dir < 2; ++dir) {
            int v = E.edges[e0].v[dir == 0 ? 1 : 0];
            for (;;) {
                int next = -1;
                for (int e : at[v]) if (!used.count(e)) { next = e; break; }
                if (next < 0) break;
                used.insert(next);
                if (dir == 0) run.push_back(next); else run.insert(run.begin(), next);
                v = E.edges[next].other(v);
            }
        }
        r.holes.push_back(run);
    }
    for (int i = 0; i < (int)E.insts.size(); ++i)
        if (E.insts[i].alive) r.nested.push_back(i);
    if (r.holes.empty() && r.innerFaces.empty() && r.extraEdges.empty()) r.reversedFaces = facesToReverse(E);
    else {
        // with holes the shell is open: report faces against their neighbours only
        for (int f : facesToReverse(E)) r.reversedFaces.push_back(f);
    }
    return r;
}

// The vertices round a run of edges, in order; empty unless the run closes.
static std::vector<int> loopVerts(const Entities& E, const std::vector<int>& run) {
    if (run.size() < 3) return {};
    std::vector<int> vs;
    const Edge& a = E.edges[run[0]];
    const Edge& b = E.edges[run[1]];
    int v = (a.v[1] == b.v[0] || a.v[1] == b.v[1]) ? a.v[0] : a.v[1];
    for (int e : run) {
        const Edge& Ed = E.edges[e];
        if (Ed.v[0] != v && Ed.v[1] != v) return {};
        vs.push_back(v);
        v = Ed.other(v);
    }
    if (v != vs[0]) return {};
    return vs;
}

int repairSolid(Entities& E) {
    int changes = 0;
    SolidReport r = inspectSolid(E);
    if (!r.strayEdges.empty()) {
        for (int e : r.strayEdges) E.removeEdge(e);
        changes += (int)r.strayEdges.size();
    }
    if (!r.innerFaces.empty()) {
        std::set<int> touched;
        for (int f : r.innerFaces)
            for (int e : E.faceEdges(f)) touched.insert(e);
        eraseFaces(E, r.innerFaces);
        changes += (int)r.innerFaces.size();
        // the faces either side of a removed wall are one face again
        mergeCoplanarAcross(E, std::vector<int>(touched.begin(), touched.end()));
        r = inspectSolid(E);
    }
    for (auto& run : r.holes) {
        std::vector<int> loop = loopVerts(E, run);
        if (loop.size() < 3) continue;
        Vec3 n = newellNormal(E, loop);
        if (n.isZero()) continue;
        n = n.normalized();
        Plane pl(n, E.verts[loop[0]].p);
        double size = 0, off = 0;
        for (int v : loop) {
            size = std::max(size, (E.verts[v].p - E.verts[loop[0]].p).length());
            off = std::max(off, std::fabs(pl.dist(E.verts[v].p)));
        }
        if (off <= std::max(kPlaneTol, size * 1e-6)) {
            E.addFace(loop, {}, pl);
        } else {
            // not flat: a fan of triangles, the new edges soft and smooth
            for (size_t i = 1; i + 1 < loop.size(); ++i) {
                std::vector<int> tri{loop[0], loop[i], loop[i + 1]};
                Vec3 tn = newellNormal(E, tri);
                if (tn.isZero()) continue;
                E.addFace(tri, {}, Plane(tn.normalized(), E.verts[loop[0]].p));
            }
            for (size_t i = 2; i + 1 < loop.size(); ++i) {
                int e = E.findEdge(loop[0], loop[i]);
                if (e >= 0) E.edges[e].soft = E.edges[e].smooth = true;
            }
        }
        ++changes;
    }
    for (int f : facesToReverse(E)) {
        reverseFace(E, f);
        ++changes;
    }
    E.touch();
    return changes;
}

double volume(const Entities& E) {
    double v = 0;
    std::vector<Vec3> tris;
    for (int f = 0; f < (int)E.faces.size(); ++f) {
        if (!E.faces[f].alive) continue;
        tris.clear();
        faceTriangles(E, f, tris);
        for (size_t i = 0; i + 2 < tris.size(); i += 3) v += tris[i].dot(tris[i + 1].cross(tris[i + 2])) / 6.0;
    }
    return v;
}

// ---------------------------------------------------------------- texture placement

TexMap effectiveTexMap(const Face& F, bool back) {
    const TexMap& t = back ? F.texBack : F.texFront;
    if (t.custom) return t;
    Plane pl = F.plane;
    if (back) { pl.n = -pl.n; pl.d = -pl.d; }
    Frame2 fr(pl);
    TexMap r;
    r.custom = true;
    r.projected = t.projected;
    r.uAxis = fr.u;
    r.vAxis = fr.v;
    r.uOff = r.vOff = 0;
    return r;
}

Vec3 texPoint(const Plane& pl, const TexMap& t, double u, double v) {
    Frame2 fr(pl);
    // p = o + a*fu + b*fv with (p.U + uOff, p.V + vOff) = (u, v)
    double a11 = fr.u.dot(t.uAxis), a12 = fr.v.dot(t.uAxis), a21 = fr.u.dot(t.vAxis), a22 = fr.v.dot(t.vAxis);
    double r1 = u - fr.o.dot(t.uAxis) - t.uOff, r2 = v - fr.o.dot(t.vAxis) - t.vOff;
    double det = a11 * a22 - a12 * a21;
    if (std::fabs(det) < 1e-18) return fr.o;
    double a = (r1 * a22 - a12 * r2) / det, b = (a11 * r2 - a21 * r1) / det;
    return fr.o + fr.u * a + fr.v * b;
}

bool texMapFromPins(const Plane& pl, const Vec3& p0, const Vec3& p1, const Vec3& p2, double w, double h, TexMap& out,
                    double u0, double v0) {
    Frame2 fr(pl);
    Vec2 q0 = fr.to2(p0), q1 = fr.to2(p1), q2 = fr.to2(p2);
    double e1x = q1.x - q0.x, e1y = q1.y - q0.y, e2x = q2.x - q0.x, e2y = q2.y - q0.y;
    double det = e1x * e2y - e2x * e1y;
    double scale = std::max(1e-12, (e1x * e1x + e1y * e1y) * (e2x * e2x + e2y * e2y));
    if (std::fabs(det) < 1e-9 * std::sqrt(scale) || w <= 0 || h <= 0) return false;
    // inverse of [e1 e2] (columns), then rows scaled by w and h
    double i11 = e2y / det, i12 = -e2x / det, i21 = -e1y / det, i22 = e1x / det;
    Vec3 U = (fr.u * i11 + fr.v * i12) * w;
    Vec3 V = (fr.u * i21 + fr.v * i22) * h;
    bool proj = out.projected;
    out = TexMap();
    out.custom = true;
    out.projected = proj;
    out.uAxis = U;
    out.vAxis = V;
    out.uOff = u0 - U.dot(p0);
    out.vOff = v0 - V.dot(p0);
    return true;
}

TexMap transformTexMap(const TexMap& t, const Mat4& m) {
    if (!t.custom) return t;
    Mat4 inv = m.inverted();
    // uv'(p) = uv(inv p): axis' = inv^T axis, offset' = offset + axis . inv(0)
    Vec3 o = inv.point(Vec3());
    auto tr = [&](const Vec3& a) {
        return Vec3(inv.at(0, 0) * a.x + inv.at(1, 0) * a.y + inv.at(2, 0) * a.z,
                    inv.at(0, 1) * a.x + inv.at(1, 1) * a.y + inv.at(2, 1) * a.z,
                    inv.at(0, 2) * a.x + inv.at(1, 2) * a.y + inv.at(2, 2) * a.z);
    };
    TexMap r = t;
    r.uOff = t.uOff + t.uAxis.dot(o);
    r.vOff = t.vOff + t.vAxis.dot(o);
    r.uAxis = tr(t.uAxis);
    r.vAxis = tr(t.vAxis);
    return r;
}

}  // namespace geom
}  // namespace gable

namespace gable { namespace geom {
std::vector<std::string> validate(const Entities& E, int maxReports) {
    std::vector<std::string> out;
    auto rep = [&](const std::string& m) { if ((int)out.size() < maxReports) out.push_back(m); };
    const int nv = (int)E.verts.size(), ne = (int)E.edges.size(), nf = (int)E.faces.size();
    std::map<std::pair<int, int>, int> seen;
    for (int e = 0; e < ne; ++e) {
        const Edge& Ed = E.edges[e];
        if (!Ed.alive) continue;
        if (Ed.v[0] < 0 || Ed.v[1] < 0 || Ed.v[0] >= nv || Ed.v[1] >= nv) { rep("edge " + std::to_string(e) + " bad vertex index"); continue; }
        if (!E.verts[Ed.v[0]].alive || !E.verts[Ed.v[1]].alive) rep("edge " + std::to_string(e) + " uses dead vertex");
        if (Ed.v[0] == Ed.v[1] || E.edgeLength(e) < kTol * 0.5) rep("edge " + std::to_string(e) + " zero length");
        auto key = std::make_pair(std::min(Ed.v[0], Ed.v[1]), std::max(Ed.v[0], Ed.v[1]));
        auto it = seen.find(key);
        if (it != seen.end()) rep("edges " + std::to_string(it->second) + "," + std::to_string(e) + " duplicate");
        else seen[key] = e;
        for (int k = 0; k < 2; ++k) {
            auto& ve = E.verts[Ed.v[k]].edges;
            if (std::find(ve.begin(), ve.end(), e) == ve.end()) rep("vertex " + std::to_string(Ed.v[k]) + " missing back-link to edge " + std::to_string(e));
        }
        for (int f : Ed.faces) {
            if (f < 0 || f >= nf || !E.faces[f].alive) { rep("edge " + std::to_string(e) + " links dead face " + std::to_string(f)); continue; }
            auto fe = E.faceEdges(f);
            if (std::find(fe.begin(), fe.end(), e) == fe.end()) rep("edge " + std::to_string(e) + " links face " + std::to_string(f) + " not using it");
        }
        if (Ed.faces.size() > 2) {
            // allowed (non-manifold) but worth knowing only if extreme
            if (Ed.faces.size() > 8) rep("edge " + std::to_string(e) + " has " + std::to_string(Ed.faces.size()) + " faces");
        }
    }
    for (int v = 0; v < nv; ++v) {
        const Vertex& V = E.verts[v];
        if (!V.alive) continue;
        for (int e : V.edges) {
            if (e < 0 || e >= ne || !E.edges[e].alive) { rep("vertex " + std::to_string(v) + " links dead edge " + std::to_string(e)); continue; }
            if (E.edges[e].v[0] != v && E.edges[e].v[1] != v) rep("vertex " + std::to_string(v) + " links foreign edge " + std::to_string(e));
        }
    }
    for (int f = 0; f < nf; ++f) {
        const Face& F = E.faces[f];
        if (!F.alive) continue;
        std::vector<const std::vector<int>*> loops{&F.outer};
        for (auto& h : F.holes) loops.push_back(&h);
        for (auto* L : loops) {
            if (L->size() < 3) { rep("face " + std::to_string(f) + " loop < 3 verts"); continue; }
            for (size_t i = 0; i < L->size(); ++i) {
                int a = (*L)[i], b = (*L)[(i + 1) % L->size()];
                if (a < 0 || a >= nv || !E.verts[a].alive) { rep("face " + std::to_string(f) + " dead vertex"); break; }
                int e = E.findEdge(a, b);
                if (e < 0) { rep("face " + std::to_string(f) + " loop gap " + std::to_string(a) + "-" + std::to_string(b)); break; }
                auto& ef = E.edges[e].faces;
                if (std::find(ef.begin(), ef.end(), f) == ef.end()) rep("face " + std::to_string(f) + " not linked from edge " + std::to_string(e));
                double d = std::fabs(F.plane.dist(E.verts[a].p));
                if (d > std::max(0.05, kPlaneTol * 5)) rep("face " + std::to_string(f) + " vertex off plane by " + std::to_string(d));
            }
        }
        Vec3 n = newellNormal(E, F.outer);
        if (n.dot(F.plane.n) < 0.98) rep("face " + std::to_string(f) + " winding disagrees with plane normal");
        if (E.faceArea(f) < 1e-6) rep("face " + std::to_string(f) + " zero area");
    }
    return out;
}
} }
