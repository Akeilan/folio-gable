#include "view/Picker.h"
#include "core/Geometry.h"
#include <algorithm>
#include <numeric>

namespace gable {

int Picker::indexMin = 3000;

void Picker::BVH::build(std::vector<BBox>& boxes, const std::vector<int>& ids) {
    nodes.clear();
    items = ids;
    if (items.empty()) return;
    nodes.reserve(items.size() / 2 + 1);
    std::vector<Vec3> cen(boxes.size());
    for (int i : items) cen[i] = boxes[i].center();
    struct Job { int node, a, b; };
    std::vector<Job> stack;
    nodes.push_back(Node());
    stack.push_back({0, 0, (int)items.size()});
    while (!stack.empty()) {
        Job j = stack.back();
        stack.pop_back();
        BBox nb, cb;
        for (int k = j.a; k < j.b; ++k) { nb.add(boxes[items[k]]); cb.add(cen[items[k]]); }
        nodes[j.node].lo = nb.lo;
        nodes[j.node].hi = nb.hi;
        int n = j.b - j.a;
        Vec3 ext = cb.size();
        if (n <= 6 || (ext.x <= 0 && ext.y <= 0 && ext.z <= 0)) {
            nodes[j.node].first = j.a;
            nodes[j.node].count = n;
            continue;
        }
        int axis = ext.x >= ext.y && ext.x >= ext.z ? 0 : (ext.y >= ext.z ? 1 : 2);
        int mid = j.a + n / 2;
        auto key = [&](int i) { const Vec3& c = cen[i]; return axis == 0 ? c.x : (axis == 1 ? c.y : c.z); };
        std::nth_element(items.begin() + j.a, items.begin() + mid, items.begin() + j.b,
                         [&](int x, int y) { return key(x) < key(y); });
        int left = (int)nodes.size();
        nodes.push_back(Node());
        int right = (int)nodes.size();
        nodes.push_back(Node());
        nodes[j.node].first = left;
        nodes[j.node].right = right;
        nodes[j.node].count = 0;
        stack.push_back({left, j.a, mid});
        stack.push_back({right, mid, j.b});
    }
}

void Picker::prepare() const {
    // called when the model has been still for a moment: start building indexes ahead of the next hover
    index(-1, doc->model.root, false, true);
    int ad = doc->activeDef();
    if (ad >= 0) index(ad, doc->model.defs[ad].ents, false, true);
}

const Picker::Index* Picker::index(int def, const Entities& E, bool staleOk, bool force) const {
    if (E.faces.size() + E.edges.size() < (size_t)indexMin) return nullptr;
    CacheEntry& c = cache_[def];
    auto matches = [&](const Index* x) { return x && x->rev == E.rev && x->nf == E.faces.size() && x->ne == E.edges.size(); };
    if (c.job.valid() && c.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto done = c.job.get();
        if (matches(done.get()) || (staleOk && done && done->nf == E.faces.size() && done->ne == E.edges.size())) c.idx = std::move(done);
    }
    if (matches(c.idx.get())) return c.idx.get();
    // During a Move/Rotate/Scale preview only the excluded (moving) vertices change, and every
    // face or edge touching them is skipped, so the index from before the preview still holds.
    if (staleOk && c.idx && c.idx->nf == E.faces.size() && c.idx->ne == E.edges.size()) return c.idx.get();
    if (staleOk) return nullptr;
    // Geometry that changes on every pick (a Move or Push/Pull preview) is scanned directly;
    // once it holds still for a second pick the index is built on a worker thread, and picks
    // scan directly until it is ready.
    if (c.pendingRev != E.rev) { c.pendingRev = E.rev; c.seen = 0; }
    if ((++c.seen < 2 && !force) || c.job.valid()) return nullptr;
    auto boxes = std::make_shared<std::vector<BBox>>(E.faces.size() + E.edges.size());
    auto fids = std::make_shared<std::vector<int>>(), eids = std::make_shared<std::vector<int>>();
    fids->reserve(E.faces.size());
    eids->reserve(E.edges.size());
    const size_t nf = E.faces.size();
    for (int i = 0; i < (int)nf; ++i) {
        const Face& F = E.faces[i];
        if (!F.alive || F.outer.empty()) continue;
        for (int v : F.outer) (*boxes)[i].add(E.verts[v].p);
        fids->push_back(i);
    }
    for (int i = 0; i < (int)E.edges.size(); ++i) {
        const Edge& Ed = E.edges[i];
        if (!Ed.alive) continue;
        (*boxes)[nf + i].add(E.verts[Ed.v[0]].p);
        (*boxes)[nf + i].add(E.verts[Ed.v[1]].p);
        eids->push_back(i);
    }
    uint64_t rev = E.rev;
    size_t ne = E.edges.size();
    c.job = std::async(std::launch::async, [boxes, fids, eids, rev, nf, ne]() {
        auto idx = std::make_unique<Index>();
        idx->rev = rev;
        idx->nf = nf;
        idx->ne = ne;
        std::vector<BBox> fb(boxes->begin(), boxes->begin() + nf), eb(boxes->begin() + nf, boxes->end());
        idx->faces.build(fb, *fids);
        idx->edges.build(eb, *eids);
        return idx;
    });
    return nullptr;
}

// Ray against a box (slab test), local space.
static bool rayBox(const Ray& r, const Vec3& lo, const Vec3& hi, double tol) {
    double t0 = 0, t1 = 1e300;
    const double o[3] = {r.o.x, r.o.y, r.o.z}, d[3] = {r.d.x, r.d.y, r.d.z};
    const double l[3] = {lo.x - tol, lo.y - tol, lo.z - tol}, h[3] = {hi.x + tol, hi.y + tol, hi.z + tol};
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(d[k]) < 1e-300) {
            if (o[k] < l[k] || o[k] > h[k]) return false;
            continue;
        }
        double a = (l[k] - o[k]) / d[k], b = (h[k] - o[k]) / d[k];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) return false;
    }
    return true;
}

double Picker::screenDistSeg(const Vec3& aIn, const Vec3& bIn, double x, double y, double* tOut) const {
    Vec3 a = aIn, b = bIn;
    double t0 = 0, t1 = 1;
    if (cam->s->perspective) {
        Vec3 f = cam->forward();
        double eps = std::max(cam->nearZ, 1e-3);
        double za = (a - cam->s->eye).dot(f), zb = (b - cam->s->eye).dot(f);
        if (za < eps && zb < eps) return 1e300;
        if (za < eps) { double t = (eps - za) / (zb - za); a = aIn + (bIn - aIn) * t; t0 = t; }
        else if (zb < eps) { double t = (eps - zb) / (za - zb); b = bIn + (aIn - bIn) * t; t1 = 1 - t; }
    }
    double ax, ay, bx, by;
    if (!cam->project(a, ax, ay) || !cam->project(b, bx, by)) return 1e300;
    double dx = bx - ax, dy = by - ay;
    double l2 = dx * dx + dy * dy;
    double s = l2 > 1e-12 ? std::clamp(((x - ax) * dx + (y - ay) * dy) / l2, 0.0, 1.0) : 0;
    double px = ax + dx * s, py = ay + dy * s;
    if (tOut) {
        // map screen param back to 3D param (perspective-correct via ray-line closest point)
        Ray r = cam->ray(x, y);
        double t, u;
        Vec3 d = bIn - aIn;
        if (d.length() > 1e-12 && closestLineLine(aIn, d, r.o, r.d, t, u)) *tOut = std::clamp(t, t0, t1);
        else *tOut = t0 + (t1 - t0) * s;
    }
    return std::hypot(px - x, py - y);
}

bool Picker::faceHit(const Entities& E, int f, const Ray& lr, double& tOut) const {
    const Face& F = E.faces[f];
    double den = F.plane.n.dot(lr.d);
    if (std::fabs(den) < 1e-14) return false;
    double t = -(F.plane.n.dot(lr.o) + F.plane.d) / den;
    if (t <= 0) return false;
    Vec3 p = lr.o + lr.d * t;
    // quick bbox
    BBox b = geom::faceBBox(E, f);
    double tol = kTol + 1e-9 * b.radius();
    if (p.x < b.lo.x - tol || p.y < b.lo.y - tol || p.z < b.lo.z - tol || p.x > b.hi.x + tol || p.y > b.hi.y + tol || p.z > b.hi.z + tol)
        return false;
    if (!E.pointInFace(f, p, 1e-6)) return false;
    tOut = t;
    return true;
}

static bool instanceScreenRect(const Camera* cam, const BBox& wb, double& x0, double& y0, double& x1, double& y1) {
    x0 = y0 = 1e300; x1 = y1 = -1e300;
    for (int i = 0; i < 8; ++i) {
        double sx, sy, d;
        Vec3 c = wb.corner(i);
        if (!cam->project(c, sx, sy, &d)) return false;
        if (cam->s->perspective && d <= 0) return false;
        x0 = std::min(x0, sx); y0 = std::min(y0, sy); x1 = std::max(x1, sx); y1 = std::max(y1, sy);
    }
    return true;
}

void Picker::walk(int def, const Entities& E, const Mat4& xf, const InstPath& path, const Ray& ray, double x, double y,
                  double vtol, double etol, DeepResult& r, int depth) const {
    if (depth > 32) return;
    const Style& st = doc->model.style;
    Vec3 f = cam->forward();
    Mat4 inv = xf.inverted();
    Ray lr{inv.point(ray.o), inv.vector(ray.d)};
    const bool isActive = path == doc->editPath;
    const bool exclude = isActive && (!exVerts.empty() || !exInsts.empty());
    auto depthOf = [&](const Vec3& w) { return (w - cam->s->eye).dot(f); };
    // faces
    const Index* ix = index(def, E, exclude && !exVerts.empty());
    std::vector<int> cand;
    if (st.faceStyle != FaceStyle::Wireframe) {
        if (ix) {
            // faces whose box the (local) ray passes through
            const BVH& B = ix->faces;
            std::vector<int> stack;
            if (!B.nodes.empty()) stack.push_back(0);
            while (!stack.empty()) {
                const BVH::Node& n = B.nodes[stack.back()];
                stack.pop_back();
                double tol = kTol + 1e-9 * (n.hi - n.lo).length();
                if (!rayBox(lr, n.lo, n.hi, tol)) continue;
                if (n.count) cand.insert(cand.end(), B.items.begin() + n.first, B.items.begin() + n.first + n.count);
                else { stack.push_back(n.first); stack.push_back(n.right); }
            }
        }
        // visit candidates in index order, exactly as a full scan would (ties resolve the same way)
        std::sort(cand.begin(), cand.end());
        const int nCand = ix ? (int)cand.size() : (int)E.faces.size();
        for (int ci = 0; ci < nCand; ++ci) {
            const int i = ix ? cand[ci] : ci;
            const Face& F = E.faces[i];
            if (!F.alive || (F.hidden && !st.showHidden) || !doc->tagVisible(F.tag)) continue;
            if (exclude) {
                bool ex = false;
                for (int v : F.outer) if (exVerts.count(v)) { ex = true; break; }
                if (ex) continue;
            }
            double t;
            if (!faceHit(E, i, lr, t)) continue;
            Vec3 w = xf.point(lr.o + lr.d * t);
            double dd = depthOf(w);
            if (!cam->s->perspective || dd > 0) {
                if (dd < r.face.depth) {
                    r.face.kind = EntKind::Face; r.face.id = i; r.face.path = path; r.face.xf = xf;
                    r.face.point = w; r.face.depth = dd; r.face.def = def; r.face.screenDist = 0;
                }
            }
        }
    }
    // edges & vertices
    std::vector<char> vdone;
    cand.clear();
    if (ix) {
        // edges in nodes the pick cone can reach: a point within tol pixels of the cursor lies
        // within tol * (pixel size at its depth) of the pick ray
        const double tolPx = std::max(etol, vtol + 2) * 1.5 + 2;
        const bool persp = cam->s->perspective;
        const Vec3 eye = cam->s->eye;
        const double pixAt1 = persp ? cam->pixelSize(eye + f) : cam->pixelSize(eye);
        const double sMax = std::max({xf.axis(0).length(), xf.axis(1).length(), xf.axis(2).length()});
        const double nearZ = std::max(cam->nearZ, 1e-3);
        const BVH& B = ix->edges;
        std::vector<int> stack;
        if (!B.nodes.empty()) stack.push_back(0);
        while (!stack.empty()) {
            const BVH::Node& n = B.nodes[stack.back()];
            stack.pop_back();
            Vec3 c = xf.point((n.lo + n.hi) * 0.5);
            double rad = (n.hi - n.lo).length() * 0.5 * sMax + 1e-6;
            double z = (c - eye).dot(f);
            if (persp && z + rad < nearZ) continue;
            Vec3 q = c - ray.o;
            double perp = (q - ray.d * q.dot(ray.d)).length();
            double allow = tolPx * (persp ? pixAt1 * std::max(z + rad, nearZ) : pixAt1);
            if (perp - rad > allow) continue;
            if (n.count) cand.insert(cand.end(), B.items.begin() + n.first, B.items.begin() + n.first + n.count);
            else { stack.push_back(n.first); stack.push_back(n.right); }
        }
    }
    std::sort(cand.begin(), cand.end());
    const int nEdges = ix ? (int)cand.size() : (int)E.edges.size();
    for (int ci = 0; ci < nEdges; ++ci) {
        const int i = ix ? cand[ci] : ci;
        const Edge& Ed = E.edges[i];
        if (!Ed.alive || ((Ed.hidden || Ed.soft) && !st.showHidden) || !doc->tagVisible(Ed.tag)) continue;
        if (exclude && (exVerts.count(Ed.v[0]) || exVerts.count(Ed.v[1]))) continue;
        Vec3 a = xf.point(E.verts[Ed.v[0]].p), b = xf.point(E.verts[Ed.v[1]].p);
        double t;
        double sd = screenDistSeg(a, b, x, y, &t);
        if (sd < etol) {
            PickHit h;
            h.kind = EntKind::Edge; h.id = i; h.path = path; h.xf = xf; h.def = def;
            h.point = a + (b - a) * t;
            h.depth = depthOf(h.point);
            h.screenDist = sd;
            r.edges.push_back(h);
        }
        if (sd < vtol + 2) {
            if (vdone.empty()) vdone.assign(E.verts.size(), 0);
            for (int k = 0; k < 2; ++k) {
                int vi = Ed.v[k];
                if (vi < (int)vdone.size()) { if (vdone[vi]) continue; vdone[vi] = 1; }
                Vec3 p = k ? b : a;
                double sx, sy, dd;
                if (!cam->project(p, sx, sy, &dd)) continue;
                if (cam->s->perspective && dd <= 0) continue;
                double d2 = std::hypot(sx - x, sy - y);
                if (d2 < vtol) {
                    PickHit h;
                    h.kind = EntKind::Vertex; h.id = vi; h.path = path; h.xf = xf;
                    h.point = p; h.depth = dd; h.screenDist = d2; h.def = def;
                    r.verts.push_back(h);
                }
            }
        }
    }
    // guides
    if (st.showGuides)
        for (int i = 0; i < (int)E.guides.size(); ++i) {
            const Guide& g = E.guides[i];
            if (!g.alive || g.hidden || !doc->tagVisible(g.tag)) continue;
            Vec3 p = xf.point(g.p);
            if (g.isPoint) {
                double sx, sy, dd;
                if (!cam->project(p, sx, sy, &dd)) continue;
                double d2 = std::hypot(sx - x, sy - y);
                if (d2 < vtol && d2 < r.guidePoint.screenDist) {
                    r.guidePoint.kind = EntKind::Guide; r.guidePoint.id = i; r.guidePoint.path = path; r.guidePoint.xf = xf;
                    r.guidePoint.point = p; r.guidePoint.depth = dd; r.guidePoint.screenDist = d2; r.guidePoint.def = def;
                }
            } else {
                Vec3 d = xf.vector(g.dir).normalized();
                double t, u;
                if (!closestLineLine(p, d, ray.o, ray.d, t, u)) continue;
                Vec3 q = p + d * t;
                double sx, sy, dd;
                if (!cam->project(q, sx, sy, &dd)) continue;
                if (cam->s->perspective && dd <= 0) continue;
                double d2 = std::hypot(sx - x, sy - y);
                if (d2 < etol && d2 < r.guide.screenDist) {
                    r.guide.kind = EntKind::Guide; r.guide.id = i; r.guide.path = path; r.guide.xf = xf;
                    r.guide.point = q; r.guide.depth = dd; r.guide.screenDist = d2; r.guide.def = def;
                }
            }
        }
    // instances
    for (int i = 0; i < (int)E.insts.size(); ++i) {
        const Instance& in = E.insts[i];
        if (!in.alive || in.def < 0 || !doc->tagVisible(in.tag) || (in.hidden && !st.showHidden)) continue;
        if (isActive && exInsts.count(i)) continue;
        Mat4 cx = xf * in.xf;
        BBox wb = doc->model.defBBox(in.def).transformed(cx);
        double x0, y0, x1, y1;
        if (instanceScreenRect(cam, wb, x0, y0, x1, y1)) {
            double tol = std::max(vtol, etol);
            if (x < x0 - tol || x > x1 + tol || y < y0 - tol || y > y1 + tol) continue;
        }
        InstPath p2 = path;
        p2.push_back(i);
        walk(in.def, doc->model.defs[in.def].ents, cx, p2, ray, x, y, vtol, etol, r, depth + 1);
    }
}

Picker::DeepResult Picker::deep(double x, double y, double vtol, double etol) const {
    DeepResult r;
    Ray ray = cam->ray(x, y);
    walk(-1, doc->model.root, Mat4(), {}, ray, x, y, vtol, etol, r, 0);
    bool see = xray || doc->model.style.xray || doc->model.style.faceStyle == FaceStyle::Wireframe;
    Plane facePlane;
    if (r.face.valid()) {
        const Entities& FE = doc->entitiesAt(r.face.path);
        const Face& F = FE.faces[r.face.id];
        facePlane = Plane(r.face.xf.normal(F.plane.n), r.face.xf.point(FE.verts[F.outer[0]].p));
    }
    auto occluded = [&](const PickHit& h) {
        if (see || !r.face.valid()) return false;
        double fd = r.face.depth;
        if (h.depth <= fd + std::max(1.0, fd * 2e-4)) return false;
        // Geometry lying on the surface under the cursor is visible even at grazing angles.
        double tol = std::max(0.05, cam->pixelSize(h.point) * 0.5);
        return std::fabs(facePlane.dist(h.point)) > tol;
    };
    // Choose the nearest visible vertex: a hidden vertex closer on screen must not mask a visible one.
    {
        std::vector<PickHit> vs;
        for (auto& v : r.verts)
            if (!occluded(v)) vs.push_back(v);
        const InstPath& ap = doc->editPath;
        std::sort(vs.begin(), vs.end(), [&](const PickHit& a, const PickHit& b) {
            if (std::fabs(a.screenDist - b.screenDist) > 0.75) return a.screenDist < b.screenDist;
            // coincident on screen: prefer the active context (so new lines join it), then the nearer one
            bool aa = a.path == ap, ba = b.path == ap;
            if (aa != ba) return aa;
            return a.depth < b.depth;
        });
        r.verts = vs;
        r.vertex = vs.empty() ? PickHit() : vs[0];
    }
    std::vector<PickHit> es;
    for (auto& e : r.edges)
        if (!occluded(e)) es.push_back(e);
    std::sort(es.begin(), es.end(), [](const PickHit& a, const PickHit& b) {
        if (std::fabs(a.screenDist - b.screenDist) > 1.0) return a.screenDist < b.screenDist;
        return a.depth < b.depth;
    });
    r.edges = es;
    if (!es.empty()) r.edge = es[0];
    if (r.guide.valid() && occluded(r.guide)) r.guide = PickHit();
    if (r.guidePoint.valid() && occluded(r.guidePoint)) r.guidePoint = PickHit();
    return r;
}

EntRef Picker::pickActive(double x, double y, PickHit* hitOut) const {
    DeepResult r = deep(x, y, 0, 5);
    const InstPath& ap = doc->editPath;
    auto inActive = [&](const PickHit& h, int& topInst) -> int {
        // returns 0 = not inside active, 1 = directly in active entities, 2 = inside an instance of the active ctx
        if (h.path.size() < ap.size()) return 0;
        for (size_t i = 0; i < ap.size(); ++i)
            if (h.path[i] != ap[i]) return 0;
        if (h.path.size() == ap.size()) return 1;
        topInst = h.path[ap.size()];
        return 2;
    };
    // Edges first (front-most within tolerance)
    for (auto& e : r.edges) {
        int ti = -1;
        int k = inActive(e, ti);
        if (k == 1) { if (hitOut) *hitOut = e; return {EntKind::Edge, e.id}; }
        if (k == 2) {
            if (doc->active().insts[ti].locked && false) continue;
            if (hitOut) *hitOut = e;
            return {EntKind::Instance, ti};
        }
        break;  // nearest edge is outside the active context: fall through to faces
    }
    // Dimensions and text in the active context (screen proximity)
    const Entities& A = doc->active();
    Mat4 axf = doc->activeXf();
    for (int i = 0; i < (int)A.dims.size(); ++i) {
        const Dimension& d = A.dims[i];
        if (!d.alive || d.hidden || !doc->tagVisible(d.tag)) continue;
        Vec3 a = axf.point(d.a + d.offset), b = axf.point(d.b + d.offset);
        if (d.radial) { a = axf.point(d.a); b = axf.point(d.b); }
        if (screenDistSeg(a, b, x, y) < 6) return {EntKind::Dimension, i};
        double sx, sy;
        if (cam->project((a + b) * 0.5, sx, sy) && std::hypot(sx - x, sy - y - 8) < 18) return {EntKind::Dimension, i};
    }
    for (int i = 0; i < (int)A.texts.size(); ++i) {
        const Text& t = A.texts[i];
        if (!t.alive || t.hidden || !doc->tagVisible(t.tag)) continue;
        double sx, sy;
        if (t.screenText) {
            sx = t.sx * cam->w; sy = t.sy * cam->h;
            if (x >= sx - 4 && x <= sx + 220 && y >= sy - 16 && y <= sy + 6) return {EntKind::Text, i};
            continue;
        }
        Vec3 p = axf.point(t.anchor + t.leader);
        if (cam->project(p, sx, sy) && x >= sx - 6 && x <= sx + 200 && std::fabs(y - sy) < 14) return {EntKind::Text, i};
    }
    for (int i = 0; i < (int)A.guides.size(); ++i) {
        (void)i;
    }
    if (r.guidePoint.valid()) {
        int ti = -1;
        if (inActive(r.guidePoint, ti) == 1) return {EntKind::Guide, r.guidePoint.id};
    }
    if (r.face.valid()) {
        int ti = -1;
        int k = inActive(r.face, ti);
        if (hitOut) *hitOut = r.face;
        if (k == 1) return {EntKind::Face, r.face.id};
        if (k == 2) return {EntKind::Instance, ti};
    }
    if (r.guide.valid()) {
        int ti = -1;
        if (inActive(r.guide, ti) == 1) return {EntKind::Guide, r.guide.id};
    }
    // Section planes: distance to outline
    for (int i = 0; i < (int)A.sections.size(); ++i) {
        const SectionPlane& s = A.sections[i];
        if (!s.alive || s.hidden || !doc->model.style.showSectionPlanes) continue;
        Ray ray = cam->ray(x, y);
        Plane wp(axf.normal(s.plane.n), axf.point(s.plane.origin()));
        double t = ray.hit(wp);
        if (std::isnan(t) || t < 0) continue;
        BBox b = doc->model.worldBBox();
        double rr = b.valid() ? b.radius() * 1.1 : 2000;
        Vec3 c = b.valid() ? wp.project(b.center()) : wp.origin();
        Vec3 q = ray.at(t) - c;
        Vec3 u = wp.n.anyPerp(), v = wp.n.cross(u);
        double du = std::fabs(q.dot(u)), dv = std::fabs(q.dot(v));
        double px = cam->pixelSize(ray.at(t)) * 6;
        if ((std::fabs(du - rr) < px && dv <= rr) || (std::fabs(dv - rr) < px && du <= rr)) return {EntKind::Section, i};
    }
    if (hitOut) *hitOut = PickHit();
    return {};
}

std::vector<EntRef> Picker::pickBox(double x0, double y0, double x1, double y1, bool crossing) const {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    const Entities& E = doc->active();
    Mat4 xf = doc->activeXf();
    const Style& st = doc->model.style;
    auto inside = [&](const Vec3& p) {
        double sx, sy, d;
        if (!cam->project(p, sx, sy, &d)) return false;
        if (cam->s->perspective && d <= 0) return false;
        return sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1;
    };
    auto segCrosses = [&](const Vec3& a, const Vec3& b) {
        if (inside(a) || inside(b)) return true;
        double ax, ay, bx, by;
        if (!cam->project(a, ax, ay) || !cam->project(b, bx, by)) return false;
        // segment vs rectangle edges
        auto inter = [](double p0x, double p0y, double p1x, double p1y, double q0x, double q0y, double q1x, double q1y) {
            double d1x = p1x - p0x, d1y = p1y - p0y, d2x = q1x - q0x, d2y = q1y - q0y;
            double den = d1x * d2y - d1y * d2x;
            if (std::fabs(den) < 1e-12) return false;
            double t = ((q0x - p0x) * d2y - (q0y - p0y) * d2x) / den;
            double u = ((q0x - p0x) * d1y - (q0y - p0y) * d1x) / den;
            return t >= 0 && t <= 1 && u >= 0 && u <= 1;
        };
        return inter(ax, ay, bx, by, x0, y0, x1, y0) || inter(ax, ay, bx, by, x1, y0, x1, y1) ||
               inter(ax, ay, bx, by, x1, y1, x0, y1) || inter(ax, ay, bx, by, x0, y1, x0, y0);
    };
    std::vector<EntRef> out;
    for (int i = 0; i < (int)E.edges.size(); ++i) {
        const Edge& Ed = E.edges[i];
        if (!Ed.alive || ((Ed.hidden || Ed.soft) && !st.showHidden && !Ed.soft) || !doc->tagVisible(Ed.tag)) continue;
        if (Ed.hidden && !st.showHidden) continue;
        Vec3 a = xf.point(E.verts[Ed.v[0]].p), b = xf.point(E.verts[Ed.v[1]].p);
        bool ok = crossing ? segCrosses(a, b) : (inside(a) && inside(b));
        if (ok) out.push_back({EntKind::Edge, i});
    }
    for (int i = 0; i < (int)E.faces.size(); ++i) {
        const Face& F = E.faces[i];
        if (!F.alive || (F.hidden && !st.showHidden) || !doc->tagVisible(F.tag)) continue;
        bool ok;
        if (!crossing) {
            ok = true;
            for (int v : F.outer) if (!inside(xf.point(E.verts[v].p))) { ok = false; break; }
        } else {
            ok = false;
            for (size_t k = 0; k < F.outer.size() && !ok; ++k)
                ok = segCrosses(xf.point(E.verts[F.outer[k]].p), xf.point(E.verts[F.outer[(k + 1) % F.outer.size()]].p));
            if (!ok) {
                // rectangle entirely inside the face?
                Ray r = cam->ray((x0 + x1) / 2, (y0 + y1) / 2);
                Mat4 inv = xf.inverted();
                double t;
                if (faceHit(E, i, Ray{inv.point(r.o), inv.vector(r.d)}, t)) ok = true;
            }
        }
        if (ok) out.push_back({EntKind::Face, i});
    }
    for (int i = 0; i < (int)E.insts.size(); ++i) {
        const Instance& in = E.insts[i];
        if (!in.alive || in.def < 0 || !doc->tagVisible(in.tag) || (in.hidden && !st.showHidden)) continue;
        BBox b = doc->model.defBBox(in.def).transformed(xf * in.xf);
        bool all = true, any = false;
        for (int k = 0; k < 8; ++k) { bool ins = inside(b.corner(k)); all &= ins; any |= ins; }
        bool ok = crossing ? any : all;
        if (crossing && !ok) {
            // check actual edges of the instance geometry crossing the box
            Entities flat;
            const Entities& DE = doc->model.defs[in.def].ents;
            Mat4 cx = xf * in.xf;
            for (auto& Ed : DE.edges) {
                if (!Ed.alive) continue;
                if (segCrosses(cx.point(DE.verts[Ed.v[0]].p), cx.point(DE.verts[Ed.v[1]].p))) { ok = true; break; }
            }
        }
        if (ok) out.push_back({EntKind::Instance, i});
    }
    for (int i = 0; i < (int)E.guides.size(); ++i) {
        const Guide& g = E.guides[i];
        if (!g.alive || g.hidden) continue;
        if (g.isPoint ? inside(xf.point(g.p)) : false) out.push_back({EntKind::Guide, i});
        else if (!g.isPoint && crossing) {
            Vec3 p = xf.point(g.p), d = xf.vector(g.dir).normalized();
            if (segCrosses(p - d * 1e7, p + d * 1e7)) out.push_back({EntKind::Guide, i});
        }
    }
    for (int i = 0; i < (int)E.dims.size(); ++i) {
        const Dimension& d = E.dims[i];
        if (!d.alive || d.hidden) continue;
        Vec3 a = xf.point(d.a + d.offset), b = xf.point(d.b + d.offset);
        if (crossing ? segCrosses(a, b) : (inside(a) && inside(b))) out.push_back({EntKind::Dimension, i});
    }
    for (int i = 0; i < (int)E.texts.size(); ++i) {
        const Text& t = E.texts[i];
        if (!t.alive || t.hidden || t.screenText) continue;
        if (inside(xf.point(t.anchor + t.leader))) out.push_back({EntKind::Text, i});
    }
    return out;
}

}  // namespace gable
