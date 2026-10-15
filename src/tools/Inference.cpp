#include "tools/Inference.h"
#include "core/Geometry.h"

namespace gable {

Vec3 Inference::axis(int i) const {
    const Model& m = doc->model;
    return i == 0 ? m.axesX : (i == 1 ? m.axesY : m.axesZ);
}
QColor Inference::axisColor(int i) {
    return i == 0 ? icolor::red() : (i == 1 ? icolor::green() : (i == 2 ? icolor::blue() : icolor::magenta()));
}
QString Inference::axisName(int i) {
    return i == 0 ? "On Red Axis" : (i == 1 ? "On Green Axis" : (i == 2 ? "On Blue Axis" : "Parallel to Edge"));
}

void Inference::reset() {
    hasAnchor = false;
    centers.clear();
    clearLock();
    tracked.clear();
    hasRefEdge = false;
    excludeVerts.clear();
}

bool Inference::screenPt(const Vec3& p, double& sx, double& sy) const {
    double d;
    if (!cam->project(p, sx, sy, &d)) return false;
    if (cam->s->perspective && d <= 0) return false;
    return true;
}

void Inference::track(const Vec3& p) {
    for (auto& t : tracked)
        if (t.near(p, 1e-6)) return;
    tracked.push_back(p);
    if (tracked.size() > 4) tracked.erase(tracked.begin());
}

void Inference::lockFrom(const InfResult& r) {
    if (lock != Lock::None) return;
    switch (r.type) {
    case InfType::OnAxis:
    case InfType::Parallel:
    case InfType::Perpendicular:
        if (hasAnchor) {
            lock = Lock::Line; lockPt = anchor; lockDir = r.dir; lockColor = r.lineColor; lockAxis = r.axis;
        }
        break;
    case InfType::OnEdge: {
        if (r.hit.kind == EntKind::Edge) {
            const Entities& E = doc->entitiesAt(r.hit.path);
            const Edge& Ed = E.edges[r.hit.id];
            Vec3 a = r.hit.xf.point(E.verts[Ed.v[0]].p), b = r.hit.xf.point(E.verts[Ed.v[1]].p);
            if (hasAnchor && std::fabs((a - b).normalized().cross(anchor - a).length()) > 1e-6) {
                // lock to the direction of the edge from the anchor? SketchUp locks to the edge line itself.
            }
            lock = Lock::Line; lockPt = a; lockDir = (b - a).normalized(); lockColor = icolor::magenta(); lockAxis = 3;
        }
        break;
    }
    case InfType::OnFace:
        if (r.onFace) { lock = Lock::Plane; lockPlane = r.facePlane; lockColor = icolor::blue(); }
        break;
    case InfType::Endpoint:
    case InfType::Midpoint:
        if (hasAnchor && !(r.p - anchor).isZero(1e-6)) {
            lock = Lock::Line; lockPt = anchor; lockDir = (r.p - anchor).normalized(); lockColor = icolor::magenta(); lockAxis = 3;
        }
        break;
    default:
        if (hasAnchor && r.axis >= 0) { lock = Lock::Line; lockPt = anchor; lockDir = r.dir; lockColor = axisColor(r.axis); lockAxis = r.axis; }
        break;
    }
    lockFromArrow = false;
}

void Inference::lockArrow(int which) {
    if (lock != Lock::None && lockFromArrow && lockAxis == which) { clearLock(); return; }
    Vec3 d;
    if (which < 3) d = axis(which);
    else if (hasRefEdge) d = (refB - refA).normalized();
    else return;
    lock = Lock::Line;
    lockFromArrow = true;
    lockPt = hasAnchor ? anchor : Vec3();
    lockDir = d;
    lockColor = axisColor(which);
    lockAxis = which;
}

Vec3 Inference::freePoint(const Ray& r) const {
    if (hasAnchor) {
        // horizontal plane through the anchor if the ray meets it reasonably, else a vertical plane facing the camera
        Plane hp(Vec3(0, 0, 1), anchor);
        double t = r.hit(hp);
        Vec3 f = cam->forward();
        if (!std::isnan(t) && t > 0 && std::fabs(r.d.z) > 0.05) {
            Vec3 p = r.at(t);
            if ((p - anchor).length() < cam->distance() * 20) return p;
        }
        Vec3 n(f.x, f.y, 0);
        if (n.length() < 1e-6) n = Vec3(0, 1, 0);
        Plane vp(n.normalized(), anchor);
        t = r.hit(vp);
        if (!std::isnan(t)) return r.at(t);
        return anchor;
    }
    Plane g(Vec3(0, 0, 1), Vec3());
    double t = r.hit(g);
    if (!std::isnan(t) && t > 0 && (r.at(t) - cam->s->eye).length() < cam->distance() * 50) return r.at(t);
    Plane vp(-cam->forward(), cam->s->target);
    t = r.hit(vp);
    return std::isnan(t) ? cam->s->target : r.at(t);
}

static Vec3 closestOnLineToRay(const Vec3& p, const Vec3& d, const Ray& r, bool* ok = nullptr) {
    double t, s;
    if (!closestLineLine(p, d, r.o, r.d, t, s)) { if (ok) *ok = false; return p; }
    if (ok) *ok = true;
    return p + d * t;
}

InfResult Inference::compute(double x, double y) {
    InfResult res;
    Ray ray = cam->ray(x, y);
    // exclusion list for the picker
    picker->exVerts.clear();
    for (int v : excludeVerts) picker->exVerts.insert(v);
    Picker::DeepResult dr = picker->deep(x, y, 10, 7);
    if (dr.face.valid()) {
        const Entities& E = doc->entitiesAt(dr.face.path);
        const Face& F = E.faces[dr.face.id];
        res.onFace = true;
        res.faceHit = dr.face;
        res.facePlane = Plane(dr.face.xf.normal(F.plane.n), dr.face.xf.point(E.verts[F.outer[0]].p));
    }
    double pxTol = 10;
    auto sdist = [&](const Vec3& p) {
        double sx, sy;
        if (!screenPt(p, sx, sy)) return 1e300;
        return std::hypot(sx - x, sy - y);
    };
    auto edgeEnds = [&](const PickHit& h, Vec3& a, Vec3& b) {
        const Entities& E = doc->entitiesAt(h.path);
        const Edge& Ed = E.edges[h.id];
        a = h.xf.point(E.verts[Ed.v[0]].p);
        b = h.xf.point(E.verts[Ed.v[1]].p);
    };
    // Remember hovered edge for parallel inference; curves give centers.
    if (dr.edge.valid()) {
        edgeEnds(dr.edge, refA, refB);
        hasRefEdge = true;
        const Entities& E = doc->entitiesAt(dr.edge.path);
        int cv = E.edges[dr.edge.id].curve;
        if (cv >= 0 && cv < (int)E.curves.size() && E.curves[cv].isArc) {
            Vec3 c = dr.edge.xf.point(E.curves[cv].center);
            bool have = false;
            for (auto& q : centers) if (q.near(c, 1e-6)) have = true;
            if (!have) { centers.push_back(c); if (centers.size() > 3) centers.erase(centers.begin()); }
        }
    }

    // ---------------- locked
    if (lock == Lock::Line) {
        Vec3 L0 = lockFromArrow && hasAnchor ? anchor : lockPt;
        Vec3 p;
        if (dr.vertex.valid()) p = L0 + lockDir * (dr.vertex.point - L0).dot(lockDir);
        else if (dr.edge.valid() && [&] {
                     Vec3 a, b;
                     edgeEnds(dr.edge, a, b);
                     double t, s;
                     if (!closestLineLine(L0, lockDir, a, b - a, t, s) || s < -1e-6 || s > 1 + 1e-6) return false;
                     Vec3 q1 = L0 + lockDir * t, q2 = a + (b - a) * s;
                     if ((q1 - q2).length() > cam->pixelSize(q2) * 4) return false;  // line must really cross the edge
                     p = q1;
                     return true;
                 }()) {
        } else if (res.onFace && std::fabs(res.facePlane.n.dot(lockDir)) > 0.02) {
            Ray lr{L0, lockDir};
            double t = lr.hit(res.facePlane);
            p = std::isnan(t) ? closestOnLineToRay(L0, lockDir, ray) : lr.at(t);
            double sx, sy;
            if (!screenPt(p, sx, sy) || std::hypot(sx - x, sy - y) > 60) p = closestOnLineToRay(L0, lockDir, ray);
        } else p = closestOnLineToRay(L0, lockDir, ray);
        res.type = lockAxis >= 0 && lockAxis < 3 ? InfType::OnAxis : InfType::Parallel;
        res.p = p;
        res.axis = lockAxis;
        res.dir = lockDir;
        res.locked = true;
        res.hasLine = true;
        res.la = L0;
        res.lb = p;
        res.lineColor = lockColor;
        res.tip = lockAxis >= 0 && lockAxis < 3 ? axisName(lockAxis) : QString("Locked");
        if (dr.vertex.valid()) { res.hasLine2 = true; res.l2a = dr.vertex.point; res.l2b = p; res.line2Color = QColor(0, 0, 0); }
        return res;
    }
    if (lock == Lock::Plane) {
        Vec3 p;
        if (dr.vertex.valid()) p = lockPlane.project(dr.vertex.point);
        else {
            double t = ray.hit(lockPlane);
            p = std::isnan(t) ? lockPlane.project(cam->s->target) : ray.at(t);
        }
        res.type = InfType::OnFace;
        res.p = p;
        res.onFace = true;
        res.facePlane = lockPlane;
        res.locked = true;
        res.tip = "On Face (locked)";
        if (dr.vertex.valid()) { res.hasLine2 = true; res.l2a = dr.vertex.point; res.l2b = p; res.line2Color = QColor(0, 0, 0); }
        return res;
    }

    // ---------------- free inference
    auto axisLines = [&](const Vec3& from, double tol, std::vector<std::tuple<double, int, Vec3, Vec3>>& out) {
        for (int i = 0; i < 4; ++i) {
            Vec3 d;
            if (i < 3) d = axis(i);
            else if (hasRefEdge) d = (refB - refA).normalized();
            else continue;
            if (i == 3) {
                // skip parallel when it coincides with an axis
                bool dup = false;
                for (int k = 0; k < 3; ++k) if (std::fabs(std::fabs(d.dot(axis(k))) - 1) < 1e-9) dup = true;
                if (dup) continue;
            }
            bool ok;
            Vec3 q = closestOnLineToRay(from, d, ray, &ok);
            if (!ok) continue;
            double sd = sdist(q);
            // ignore when the line is seen end-on (projection degenerates)
            double sx0, sy0, sx1, sy1;
            if (screenPt(from, sx0, sy0) && screenPt(from + d * cam->pixelSize(from) * 50, sx1, sy1) &&
                std::hypot(sx1 - sx0, sy1 - sy0) < 8)
                continue;
            if (sd < tol) out.push_back({sd, i, q, d});
        }
        std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return std::get<0>(a) < std::get<0>(b); });
    };

    std::vector<std::tuple<double, int, Vec3, Vec3>> anchorAx;
    if (hasAnchor && allowAxes && enabled) axisLines(anchor, pxTol, anchorAx);

    auto markAxisAlignment = [&](InfResult& r) {
        if (!hasAnchor) return;
        Vec3 d = r.p - anchor;
        if (d.length() < 1e-6) return;
        for (int i = 0; i < 3; ++i)
            if (d.normalized().cross(axis(i)).length() < 1e-6) {
                r.hasLine = true; r.la = anchor; r.lb = r.p; r.lineColor = axisColor(i); r.axis = i; r.dir = axis(i);
            }
    };

    // 1) point snaps
    if (enabled && dr.vertex.valid()) {
        res.type = InfType::Endpoint;
        res.p = dr.vertex.point;
        res.hit = dr.vertex;
        res.tip = "Endpoint";
        track(res.p);
        markAxisAlignment(res);
        return res;
    }
    if (enabled && dr.guidePoint.valid()) {
        res.type = InfType::GuidePoint;
        res.p = dr.guidePoint.point;
        res.hit = dr.guidePoint;
        res.tip = "Guide Point";
        track(res.p);
        return res;
    }
    if (enabled) {
        // origin & curve centers
        if (sdist(doc->model.axesOrigin) < pxTol) {
            res.type = InfType::Origin; res.p = doc->model.axesOrigin; res.tip = "Origin";
            markAxisAlignment(res);
            return res;
        }
    }
    if (enabled && dr.edge.valid()) {
        Vec3 a, b;
        edgeEnds(dr.edge, a, b);
        Vec3 mid = (a + b) * 0.5;
        if (sdist(mid) < pxTol) {
            res.type = InfType::Midpoint; res.p = mid; res.hit = dr.edge; res.tip = "Midpoint";
            track(mid);
            markAxisAlignment(res);
            return res;
        }
    }
    // centers tracked from curves
    if (enabled)
        for (auto& c : centers)
            if (sdist(c) < pxTol) {
                res.type = InfType::Center; res.p = c; res.tip = "Center";
                track(c);
                markAxisAlignment(res);
                return res;
            }

    // 2) anchor axis intersected with alignment from a tracked point
    if (enabled && !anchorAx.empty()) {
        auto [sd, ai, q, d] = anchorAx[0];
        for (auto& T : tracked) {
            for (int k = 0; k < 3; ++k) {
                Vec3 dk = axis(k);
                if (std::fabs(dk.dot(d)) > 0.999) continue;
                double t, s;
                if (!closestLineLine(anchor, d, T, dk, t, s)) continue;
                Vec3 P1 = anchor + d * t, P2 = T + dk * s;
                if ((P1 - P2).length() > 1e-6 * std::max(1.0, (P1 - anchor).length())) continue;
                if (sdist(P1) < pxTol && !T.near(anchor)) {
                    res.type = InfType::FromPoint;
                    res.p = P1;
                    res.axis = ai; res.dir = d;
                    res.hasLine = true; res.la = anchor; res.lb = P1; res.lineColor = axisColor(ai);
                    res.hasLine2 = true; res.l2a = T; res.l2b = P1; res.line2Color = axisColor(k);
                    res.tip = "From Point";
                    return res;
                }
            }
        }
    }
    // 3) on edge (possibly where an axis line crosses it)
    if (enabled && dr.edge.valid()) {
        Vec3 a, b;
        edgeEnds(dr.edge, a, b);
        res.hit = dr.edge;
        res.p = dr.edge.point;
        res.type = InfType::OnEdge;
        res.tip = "On Edge";
        if (!anchorAx.empty()) {
            auto [sd, ai, q, d] = anchorAx[0];
            double t, s;
            if (closestLineLine(anchor, d, a, b - a, t, s) && s >= 0 && s <= 1) {
                Vec3 P = a + (b - a) * s;
                if ((anchor + d * t - P).length() < cam->pixelSize(P) * 2 && sdist(P) < pxTol * 1.5) {
                    res.p = P;
                    res.hasLine = true; res.la = anchor; res.lb = P; res.lineColor = axisColor(ai); res.axis = ai; res.dir = d;
                }
            }
        }
        markAxisAlignment(res);
        return res;
    }
    if (enabled && dr.guide.valid()) {
        res.type = InfType::OnGuide; res.p = dr.guide.point; res.hit = dr.guide; res.tip = "On Line";
        return res;
    }
    // 4) on axis from anchor
    if (!anchorAx.empty()) {
        auto [sd, ai, q, d] = anchorAx[0];
        res.type = ai < 3 ? InfType::OnAxis : InfType::Parallel;
        res.p = q;
        res.axis = ai;
        res.dir = d;
        res.hasLine = true; res.la = anchor; res.lb = q; res.lineColor = axisColor(ai);
        res.tip = axisName(ai);
        // if a face is under the cursor and the axis pierces it nearby, snap to that
        if (res.onFace && std::fabs(res.facePlane.n.dot(d)) > 0.05) {
            Ray lr{anchor, d};
            double t = lr.hit(res.facePlane);
            if (!std::isnan(t)) {
                Vec3 P = lr.at(t);
                if (sdist(P) < pxTol) { res.p = P; res.tip += " / On Face"; }
            }
        }
        // alignment with a tracked point along the same axis line
        for (auto& T : tracked) {
            Vec3 proj = anchor + d * (T - anchor).dot(d);
            if ((proj - T).length() < 1e-6) continue;
            if (sdist(proj) < pxTol) {
                res.p = proj;
                res.hasLine2 = true; res.l2a = T; res.l2b = proj; res.line2Color = QColor(0, 0, 0);
                res.tip = "From Point";
                break;
            }
        }
        if (snapLength && doc->model.units.lengthSnap) {
            double L = (res.p - anchor).dot(d);
            double s = doc->model.units.snapLength;
            if (s > 0) res.p = anchor + d * (std::round(L / s) * s);
        }
        return res;
    }
    // 5) alignment from tracked points (no anchor needed)
    if (enabled) {
        std::vector<std::tuple<double, int, Vec3, Vec3>> tl;
        for (auto& T : tracked) {
            std::vector<std::tuple<double, int, Vec3, Vec3>> o;
            axisLines(T, 7, o);
            for (auto& x : o) {
                if (std::get<1>(x) >= 3) continue;
                // only beyond the tracked point a bit
                if ((std::get<2>(x) - T).length() < cam->pixelSize(T) * 10) continue;
                tl.push_back({std::get<0>(x), std::get<1>(x), std::get<2>(x), T});
            }
        }
        std::sort(tl.begin(), tl.end(), [](auto& a, auto& b) { return std::get<0>(a) < std::get<0>(b); });
        if (!tl.empty()) {
            auto [sd, ai, q, T] = tl[0];
            Vec3 d = axis(ai);
            Vec3 P = q;
            // stay on the face plane if over a face
            if (res.onFace && std::fabs(res.facePlane.n.dot(d)) > 1e-6) {
                Ray lr{T, d};
                double t = lr.hit(res.facePlane);
                if (!std::isnan(t) && sdist(lr.at(t)) < pxTol) P = lr.at(t);
            } else if (res.onFace) {
                P = res.facePlane.project(q);
            }
            res.type = InfType::FromPoint;
            res.p = P;
            res.hasLine2 = true; res.l2a = T; res.l2b = P; res.line2Color = axisColor(ai);
            res.tip = "From Point";
            return res;
        }
    }
    // 6) on face
    if (res.onFace) {
        res.type = InfType::OnFace;
        res.p = res.faceHit.point;
        res.hit = res.faceHit;
        res.tip = res.faceHit.path.size() > doc->editPath.size() ? "On Face in Group" : "On Face";
        return res;
    }
    // 7) free
    res.type = InfType::Free;
    res.p = freePoint(ray);
    res.tip.clear();
    return res;
}

void Inference::feedback(Overlay& ov, const InfResult& r, double mx, double my) const {
    if (r.hasLine) ov.line(r.la, r.lb, r.lineColor, r.locked ? 2.5f : 1.5f, 2);
    if (r.hasLine2) ov.line(r.l2a, r.l2b, r.line2Color, 1.5f, 2);
    switch (r.type) {
    case InfType::Endpoint: ov.marker(r.p, icolor::endpoint(), Overlay::Circle, 12); break;
    case InfType::Midpoint: ov.marker(r.p, icolor::midpoint(), Overlay::Circle, 12); break;
    case InfType::OnEdge: ov.marker(r.p, icolor::onEdge(), Overlay::Square, 10); break;
    case InfType::OnFace: ov.marker(r.p, icolor::onFace(), Overlay::Diamond, 10); break;
    case InfType::Center: ov.marker(r.p, icolor::endpoint(), Overlay::Circle, 12); break;
    case InfType::Intersection: ov.marker(r.p, icolor::red(), Overlay::Cross, 12); break;
    case InfType::GuidePoint: ov.marker(r.p, QColor(0, 0, 0), Overlay::Cross, 12); break;
    case InfType::OnGuide: ov.marker(r.p, QColor(0, 0, 0), Overlay::Square, 9); break;
    case InfType::Origin: ov.marker(r.p, QColor(240, 180, 0), Overlay::Circle, 12); break;
    case InfType::FromPoint: ov.marker(r.p, QColor(0, 0, 0), Overlay::FilledCircle, 6); break;
    case InfType::OnAxis: case InfType::Parallel: ov.marker(r.p, r.lineColor, Overlay::FilledCircle, 6); break;
    default: break;
    }
    if (!r.tip.isEmpty()) {
        ov.tooltip = r.tip;
        ov.tipX = mx;
        ov.tipY = my;
    }
}

}  // namespace gable
