#include "core/Geometry.h"
#include "tools/Tools.h"
#include "view/Viewport.h"
#include <QRegularExpression>
#include <unordered_set>

namespace gable {

static QColor ghost() { return QColor(40, 40, 40); }

// ======================================================================= Push/Pull

int PushPullTool::faceUnderCursor(const MouseEv& e, Vec3* hit) const {
    PickHit h;
    EntRef r = view->picker.pickActive(e.x, e.y, &h);
    if (r.kind != EntKind::Face) {
        // allow picking the face even if an edge is closer
        Picker::DeepResult dr = view->picker.deep(e.x, e.y, 0, 0);
        if (dr.face.valid() && dr.face.path == doc()->editPath) {
            if (hit) *hit = dr.face.point;
            return dr.face.id;
        }
        return -1;
    }
    if (hit) *hit = h.point;
    return r.id;
}

void PushPullTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    if (active_) {
        commit(dist_);
        return;
    }
    Vec3 hit;
    int f = faceUnderCursor(e, &hit);
    if (f < 0) return;
    face_ = f;
    active_ = true;
    copyMode_ = copyMode_ || e.ctrl();
    startW_ = hit;
    const Face& F = doc()->active().faces[f];
    normalW_ = doc()->activeXf().normal(F.plane.n);
    Inference& I = inf();
    I.reset();
    I.setAnchor(startW_);
    I.lock = Inference::Lock::Line;
    I.lockPt = startW_;
    I.lockDir = normalW_;
    I.lockColor = icolor::magenta();
    I.lockAxis = 3;
    I.excludeVerts.clear();
    pressX_ = e.x;
    pressY_ = e.y;
    dist_ = 0;
    view->hover.clear();
}

void PushPullTool::mouseRelease(const MouseEv& e) {
    if (e.button != Qt::LeftButton || !active_) return;
    if (std::hypot(e.x - pressX_, e.y - pressY_) > 12) commit(dist_);
}

void PushPullTool::mouseMove(const MouseEv& e) {
    if (!active_) {
        int f = faceUnderCursor(e);
        hoverFace_ = f;
        view->hover.clear();
        if (f >= 0) view->hover.push_back({EntKind::Face, f});
        return;
    }
    infer(e);
    dist_ = (cur.p - startW_).dot(normalW_);
    if (doc()->model.units.lengthSnap && doc()->model.units.snapLength > 0)
        dist_ = std::round(dist_ / doc()->model.units.snapLength) * doc()->model.units.snapLength;
    setVCB(formatLength(dist_, doc()->model.units));
}

void PushPullTool::doubleClick(const MouseEv& e) {
    int f = faceUnderCursor(e);
    active_ = false;
    inf().reset();
    if (f < 0 || !haveLast_) return;
    face_ = f;
    const Face& F = doc()->active().faces[f];
    normalW_ = doc()->activeXf().normal(F.plane.n);
    commit(lastDist_);
}

bool PushPullTool::keyPress(QKeyEvent* e) {
    if (e->key() == Qt::Key_Control && !e->isAutoRepeat()) {
        copyMode_ = !copyMode_;
        setStatus(copyMode_ ? "Push/Pull: create new starting face (Ctrl)." : status());
        return true;
    }
    return false;
}

void PushPullTool::commit(double dWorld) {
    active_ = false;
    inf().reset();
    if (face_ < 0 || std::fabs(dWorld) < kTol) { face_ = -1; return; }
    Entities& E = doc()->active();
    if (face_ >= (int)E.faces.size() || !E.faces[face_].alive) { face_ = -1; return; }
    Mat4 inv = doc()->activeInv();
    Vec3 nLocal = E.faces[face_].plane.n;
    double dLocal = inv.vector(normalW_ * dWorld).dot(nLocal);
    doc()->begin("Push/Pull");
    int res = geom::pushPull(E, face_, dLocal, copyMode_);
    doc()->commit();
    lastDist_ = dWorld;
    haveLast_ = true;
    lastResultFace_ = res;
    face_ = -1;
    copyMode_ = false;
}

void PushPullTool::vcbEnter(const QString& s) {
    double d;
    if (!parseLength(s, doc()->model.units, d)) return;
    if (active_) {
        commit(d);
        return;
    }
    // Adjust the previous push/pull.
    if (haveLast_ && lastResultFace_ >= 0) {
        Entities& E = doc()->active();
        if (lastResultFace_ < (int)E.faces.size() && E.faces[lastResultFace_].alive) {
            double delta = d - lastDist_;
            doc()->begin("Push/Pull");
            int r = geom::pushPull(E, lastResultFace_, delta, false);
            doc()->commit();
            lastResultFace_ = r;
            lastDist_ = d;
        }
    }
}

void PushPullTool::draw(Overlay& ov) {
    if (!active_ || face_ < 0) return;
    const Entities& E = doc()->active();
    if (face_ >= (int)E.faces.size() || !E.faces[face_].alive) return;
    const Face& F = E.faces[face_];
    Mat4 xf = doc()->activeXf();
    Vec3 off = normalW_ * dist_;
    std::vector<std::vector<int>> loops = {F.outer};
    for (auto& h : F.holes) loops.push_back(h);
    Overlay::Poly cap;
    for (size_t li = 0; li < loops.size(); ++li) {
        std::vector<Vec3> pts;
        for (int v : loops[li]) pts.push_back(xf.point(E.verts[v].p));
        for (size_t i = 0; i < pts.size(); ++i) {
            Vec3 a = pts[i], b = pts[(i + 1) % pts.size()];
            ov.line(a + off, b + off, ghost(), 1.5f, 0, false);
            ov.line(a, a + off, ghost(), 1.0f, 0, false);
        }
        std::vector<Vec3> moved;
        for (auto& p : pts) moved.push_back(p + off);
        if (li == 0) cap.pts = moved;
        else cap.holes.push_back(moved);
    }
    cap.c = QColor(255, 255, 255, 120);
    ov.polys.push_back(cap);
    ov.label(startW_ + off * 0.5, formatLength(dist_, doc()->model.units));
    drawInf(ov);
}

// ======================================================================= TransformTool

bool TransformTool::pickTargets(const MouseEv& e) {
    autoSelected_ = false;
    if (doc()->sel.empty()) {
        EntRef r = view->picker.pickActive(e.x, e.y);
        if (!r.valid()) return false;
        std::vector<EntRef> s = {r};
        const Entities& E = doc()->active();
        if (r.kind == EntKind::Edge && E.edges[r.id].curve >= 0) {
            s.clear();
            for (int x : geom::curveEdges(E, E.edges[r.id].curve)) s.push_back({EntKind::Edge, x});
        }
        doc()->setSelection(s);
        autoSelected_ = true;
    }
    targets_ = doc()->sel;
    return !targets_.empty();
}

Mat4 TransformTool::toLocalXf(const Mat4& worldXf) const {
    Mat4 a = doc()->activeXf();
    return a.inverted() * worldXf * a;
}

void TransformTool::beginPreview() {
    Entities& E = doc()->active();
    verts_ = geom::selectionVertices(E, targets_);
    origPos_.clear();
    for (int v : verts_) origPos_.push_back(E.verts[v].p);
    origInst_.clear();
    origGuide_.clear();
    std::set<int> exI;
    for (auto& r : targets_) {
        if (r.kind == EntKind::Instance) { origInst_.push_back({r.id, E.insts[r.id].xf}); exI.insert(r.id); }
    }
    inf().excludeVerts = verts_;
    view->picker.exInsts = exI;
    preview_ = true;
}

void TransformTool::applyPreview(const Mat4& worldXf) {
    if (!preview_) return;
    Entities& E = doc()->active();
    Mat4 L = toLocalXf(worldXf);
    for (size_t i = 0; i < verts_.size(); ++i) E.verts[verts_[i]].p = L.point(origPos_[i]);
    if (!verts_.empty()) {
        E.invalidateIndex();
        // refresh face planes so lighting follows rotations
        std::set<int> fs;
        for (int v : verts_)
            for (int e : E.verts[v].edges)
                for (int f : E.edges[e].faces) fs.insert(f);
        for (int f : fs) {
            Face& F = E.faces[f];
            if (!F.alive) continue;
            Vec3 n = geom::newellNormal(E, F.outer);
            if (!n.isZero()) F.plane = Plane(n, E.verts[F.outer[0]].p);
        }
        E.touch();
    }
    for (auto& [i, xf] : origInst_) E.insts[i].xf = L * xf;
}

void TransformTool::endPreview() {
    if (!preview_) return;
    Entities& E = doc()->active();
    for (size_t i = 0; i < verts_.size(); ++i)
        if (verts_[i] < (int)E.verts.size()) E.verts[verts_[i]].p = origPos_[i];
    if (!verts_.empty()) {
        E.invalidateIndex();
        std::set<int> fs;
        for (int v : verts_)
            for (int e : E.verts[v].edges)
                for (int f : E.edges[e].faces) fs.insert(f);
        for (int f : fs) {
            Face& F = E.faces[f];
            if (!F.alive) continue;
            Vec3 n = geom::newellNormal(E, F.outer);
            if (!n.isZero()) F.plane = Plane(n, E.verts[F.outer[0]].p);
        }
        E.touch();
    }
    for (auto& [i, xf] : origInst_)
        if (i < (int)E.insts.size()) E.insts[i].xf = xf;
    preview_ = false;
    inf().excludeVerts.clear();
    view->picker.exInsts.clear();
}

void TransformTool::commitTransform(const Mat4& worldXf, const QString& name) {
    endPreview();
    Entities& E = doc()->active();
    Mat4 L = toLocalXf(worldXf);
    doc()->begin(name);
    if (copy_) {
        auto res = geom::copyEntities(E, targets_, E, L, true);
        // keep only top-level kinds that were selected (faces/edges/instances...)
        lastCopies_ = res;
        doc()->commit();
        doc()->setSelection(res);
    } else {
        std::vector<int> vs = geom::selectionVertices(E, targets_);
        if (!vs.empty()) geom::transformVertices(E, vs, L, true);
        for (auto& r : targets_) {
            switch (r.kind) {
            case EntKind::Instance: E.insts[r.id].xf = L * E.insts[r.id].xf; break;
            case EntKind::Guide: {
                Guide& g = E.guides[r.id];
                g.p = L.point(g.p);
                g.dir = L.vector(g.dir).normalized();
                break;
            }
            case EntKind::Dimension: {
                Dimension& d = E.dims[r.id];
                d.a = L.point(d.a); d.b = L.point(d.b); d.offset = L.vector(d.offset);
                break;
            }
            case EntKind::Text: {
                Text& t = E.texts[r.id];
                t.anchor = L.point(t.anchor);
                t.leader = L.vector(t.leader);
                break;
            }
            case EntKind::Section: {
                SectionPlane& s = E.sections[r.id];
                Vec3 o = L.point(s.plane.origin());
                s.plane = Plane(L.normal(s.plane.n), o);
                break;
            }
            default: break;
            }
        }
        // curves move with their edges
        std::set<int> curves;
        for (auto& r : targets_)
            if (r.kind == EntKind::Edge && E.edges[r.id].alive && E.edges[r.id].curve >= 0) curves.insert(E.edges[r.id].curve);
        for (int c : curves) {
            Curve& cv = E.curves[c];
            double sc = L.vector(cv.xaxis).length();
            cv.center = L.point(cv.center);
            cv.normal = L.vector(cv.normal).normalized();
            cv.xaxis = L.vector(cv.xaxis).normalized();
            cv.radius *= sc;
        }
        E.touch();
        doc()->commit();
        doc()->pruneSelection();
    }
    lastXf_ = worldXf;
    haveLastXf_ = true;
}

void TransformTool::drawCopyGhost(Overlay& ov, const Mat4& worldXf) const {
    const Entities& E = doc()->active();
    Mat4 W = worldXf * doc()->activeXf();
    std::set<int> edges;
    for (auto& r : targets_) {
        if (r.kind == EntKind::Edge) edges.insert(r.id);
        if (r.kind == EntKind::Face)
            for (int e : E.faceEdges(r.id)) edges.insert(e);
        if (r.kind == EntKind::Instance) {
            const Instance& in = E.insts[r.id];
            BBox b = doc()->model.defBBox(in.def);
            Mat4 m = W * in.xf;
            static const int ed[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            if (b.valid())
                for (auto& x : ed) ov.line(m.point(b.corner(x[0])), m.point(b.corner(x[1])), icolor::selection(), 1.0f, 0, false);
            // also draw instance edges (limited)
            const Entities& D = doc()->model.defs[in.def].ents;
            int count = 0;
            for (auto& Ed : D.edges) {
                if (!Ed.alive || Ed.soft) continue;
                if (++count > 3000) break;
                ov.line(m.point(D.verts[Ed.v[0]].p), m.point(D.verts[Ed.v[1]].p), ghost(), 1.0f, 0, false);
            }
        }
    }
    int count = 0;
    for (int e : edges) {
        if (!E.edges[e].alive) continue;
        if (++count > 5000) break;
        ov.line(W.point(E.verts[E.edges[e].v[0]].p), W.point(E.verts[E.edges[e].v[1]].p), ghost(), 1.0f, 0, false);
    }
}

// ======================================================================= Move

void MoveTool::reset() {
    if (stage_) endPreview();
    stage_ = 0;
    copy_ = false;
    inf().reset();
    if (autoSelected_) { doc()->clearSelection(); autoSelected_ = false; }
}

void MoveTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    infer(e);
    pressX_ = e.x; pressY_ = e.y;
    if (stage_ == 0) {
        if (!pickTargets(e)) return;
        from_ = cur.p;
        stage_ = 1;
        if (e.ctrl()) copy_ = true;
        inf().reset();
        inf().setAnchor(from_);
        if (!copy_) beginPreview();
        else {
            inf().excludeVerts.clear();
        }
        view->hover.clear();
        return;
    }
    finish(cur.p);
}

void MoveTool::mouseRelease(const MouseEv& e) {
    if (e.button != Qt::LeftButton || stage_ != 1) return;
    if (std::hypot(e.x - pressX_, e.y - pressY_) > 12) {
        infer(e);
        finish(cur.p);
    }
}

void MoveTool::mouseMove(const MouseEv& e) {
    if (stage_ == 0) {
        infer(e);
        if (doc()->sel.empty()) {
            EntRef r = view->picker.pickActive(e.x, e.y);
            view->hover = r.valid() ? std::vector<EntRef>{r} : std::vector<EntRef>{};
        } else view->hover.clear();
        return;
    }
    infer(e);
    Vec3 d = cur.p - from_;
    if (!copy_) applyPreview(Mat4::translation(d));
    setVCB(formatLength(d.length(), doc()->model.units));
}

bool MoveTool::keyPress(QKeyEvent* e) {
    if (e->key() == Qt::Key_Control && !e->isAutoRepeat()) {
        copy_ = !copy_;
        if (stage_ == 1) {
            if (copy_) endPreview();
            else { beginPreview(); applyPreview(Mat4::translation(cur.p - from_)); }
        }
        return true;
    }
    return lockKeys(e);
}

void MoveTool::finish(const Vec3& to) {
    Vec3 d = to - from_;
    bool wasCopy = copy_;
    std::vector<EntRef> orig = targets_;
    commitTransform(Mat4::translation(d), wasCopy ? "Copy" : "Move");
    lastDelta_ = d;
    stage_ = 0;
    inf().reset();
    if (wasCopy) { lastCopies_ = doc()->sel; targets_ = orig; }
    else lastCopies_.clear();
    autoSelected_ = false;
    copy_ = false;
}

void MoveTool::vcbEnter(const QString& sIn) {
    QString s = sIn.trimmed();
    const UnitSettings& U = doc()->model.units;
    // Array after a copy: x5, 5x, *5, /5
    static QRegularExpression mul(R"(^(?:[x\*](\d+)|(\d+)[x\*])$)", QRegularExpression::CaseInsensitiveOption);
    static QRegularExpression div(R"(^(?:/(\d+)|(\d+)/)$)");
    auto mm = mul.match(s), dm = div.match(s);
    if (stage_ == 0 && !lastCopies_.empty() && (mm.hasMatch() || dm.hasMatch())) {
        Entities& E = doc()->active();
        doc()->begin("Array");
        std::vector<EntRef> all = lastCopies_;
        if (mm.hasMatch()) {
            int n = (mm.captured(1).isEmpty() ? mm.captured(2) : mm.captured(1)).toInt();
            for (int k = 2; k <= n; ++k) {
                auto r = geom::copyEntities(E, targets_, E, toLocalXf(Mat4::translation(lastDelta_ * k)), true);
                all.insert(all.end(), r.begin(), r.end());
            }
        } else {
            int n = (dm.captured(1).isEmpty() ? dm.captured(2) : dm.captured(1)).toInt();
            for (int k = 1; k < n; ++k) {
                auto r = geom::copyEntities(E, targets_, E, toLocalXf(Mat4::translation(lastDelta_ * ((double)k / n))), true);
                all.insert(all.end(), r.begin(), r.end());
            }
        }
        doc()->commit();
        doc()->setSelection(all);
        return;
    }
    Vec3 v;
    bool rel;
    if (stage_ == 1 && parseCoords(s, v, rel)) {
        finish(rel ? from_ + doc()->activeXf().vector(v) : doc()->activeXf().point(v));
        return;
    }
    double L;
    if (!parseLength(s, U, L)) return;
    if (stage_ == 1) {
        Vec3 d = cur.p - from_;
        if (d.length() < 1e-9) return;
        finish(from_ + d.normalized() * L);
        return;
    }
    // adjust previous move distance
    if (haveLastXf_ && lastDelta_.length() > 1e-9 && lastCopies_.empty() && !doc()->sel.empty()) {
        targets_ = doc()->sel;
        Vec3 nd = lastDelta_.normalized() * L;
        copy_ = false;
        commitTransform(Mat4::translation(nd - lastDelta_), "Move");
        lastDelta_ = nd;
    }
}

void MoveTool::draw(Overlay& ov) {
    if (stage_ == 1) {
        Vec3 d = cur.p - from_;
        if (copy_) drawCopyGhost(ov, Mat4::translation(d));
        ov.line(from_, cur.p, cur.hasLine ? cur.lineColor : QColor(0, 0, 0), 1.5f, 2, false);
        if (copy_) ov.label(cur.p, "+", QColor(0, 0, 0), false);
    }
    drawInf(ov);
}

// ======================================================================= Rotate

void RotateTool::reset() {
    if (stage_ == 2) endPreview();
    stage_ = 0;
    copy_ = false;
    inf().reset();
    lockAxis_ = -1;
    if (autoSelected_) { doc()->clearSelection(); autoSelected_ = false; }
}

QString RotateTool::status() const {
    switch (stage_) {
    case 0: return "Select center point of rotation. Arrow keys lock the plane.";
    case 1: return "Select start point of the rotation angle.";
    default: return "Select end angle or type an angle. Ctrl = copy.";
    }
}

Vec3 RotateTool::planeNormalAt(const InfResult& r) const {
    if (lockAxis_ >= 0) return inf().axis(lockAxis_);
    if (r.onFace) return r.facePlane.n;
    return inf().axis(2);
}

double RotateTool::angleFor(const Vec3& p) const {
    Vec3 a = ref_ - center_, b = p - center_;
    a = a - normal_ * a.dot(normal_);
    b = b - normal_ * b.dot(normal_);
    if (a.length() < 1e-9 || b.length() < 1e-9) return 0;
    return std::atan2(normal_.dot(a.cross(b)), a.dot(b));
}

bool RotateTool::keyPress(QKeyEvent* e) {
    if (e->key() == Qt::Key_Control && !e->isAutoRepeat()) {
        copy_ = !copy_;
        if (stage_ == 2) {
            if (copy_) endPreview();
            else { beginPreview(); applyPreview(Mat4::rotationAbout(center_, normal_, angle_)); }
        }
        return true;
    }
    if (stage_ == 0 && !e->isAutoRepeat()) {
        int ax = -1;
        if (e->key() == Qt::Key_Right) ax = 0;
        else if (e->key() == Qt::Key_Left) ax = 1;
        else if (e->key() == Qt::Key_Up) ax = 2;
        if (ax >= 0) { lockAxis_ = lockAxis_ == ax ? -1 : ax; return true; }
    }
    return lockKeys(e);
}

void RotateTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    infer(e);
    if (stage_ == 0) {
        if (!pickTargets(e)) {
            // allow rotating without selection? SketchUp needs something to rotate
            return;
        }
        center_ = cur.p;
        normal_ = planeNormalAt(cur);
        if (e.ctrl()) copy_ = true;
        stage_ = 1;
        inf().reset();
        inf().setAnchor(center_);
        view->hover.clear();
        return;
    }
    if (stage_ == 1) {
        Vec3 p = cur.p;
        Plane pl(normal_, center_);
        p = pl.project(p);
        if ((p - center_).length() < kTol) return;
        ref_ = p;
        stage_ = 2;
        if (!copy_) beginPreview();
        return;
    }
    finish(angle_);
}

void RotateTool::mouseMove(const MouseEv& e) {
    infer(e);
    if (stage_ == 0 && doc()->sel.empty()) {
        EntRef r = view->picker.pickActive(e.x, e.y);
        view->hover = r.valid() ? std::vector<EntRef>{r} : std::vector<EntRef>{};
    }
    if (stage_ == 2) {
        Plane pl(normal_, center_);
        Vec3 p;
        Ray r = view->cam.ray(e.x, e.y);
        bool snapped = cur.type == InfType::Endpoint || cur.type == InfType::Midpoint || cur.type == InfType::OnEdge;
        double t = r.hit(pl);
        p = snapped || std::isnan(t) ? pl.project(cur.p) : r.at(t);
        angle_ = angleFor(p);
        const UnitSettings& U = doc()->model.units;
        if (U.angleSnapOn && U.angleSnap > 0) {
            double deg = angle_ * 180 / kPi;
            double sn = std::round(deg / U.angleSnap) * U.angleSnap;
            if (std::fabs(deg - sn) < std::min(2.0, U.angleSnap / 3)) angle_ = sn * kPi / 180;
        }
        if (!copy_) applyPreview(Mat4::rotationAbout(center_, normal_, angle_));
        setVCB(formatAngle(angle_, U));
    }
}

void RotateTool::finish(double ang) {
    bool wasCopy = copy_;
    std::vector<EntRef> orig = targets_;
    commitTransform(Mat4::rotationAbout(center_, normal_, ang), wasCopy ? "Rotate Copy" : "Rotate");
    lastAngle_ = ang;
    stage_ = 0;
    inf().reset();
    if (wasCopy) { lastCopies_ = doc()->sel; targets_ = orig; }
    else lastCopies_.clear();
    copy_ = false;
    autoSelected_ = false;
}

void RotateTool::vcbEnter(const QString& sIn) {
    QString s = sIn.trimmed();
    static QRegularExpression mul(R"(^(?:[x\*](\d+)|(\d+)[x\*])$)", QRegularExpression::CaseInsensitiveOption);
    static QRegularExpression div(R"(^(?:/(\d+)|(\d+)/)$)");
    auto mm = mul.match(s), dm = div.match(s);
    if (stage_ == 0 && !lastCopies_.empty() && (mm.hasMatch() || dm.hasMatch())) {
        Entities& E = doc()->active();
        doc()->begin("Rotate Array");
        std::vector<EntRef> all = lastCopies_;
        if (mm.hasMatch()) {
            int n = (mm.captured(1).isEmpty() ? mm.captured(2) : mm.captured(1)).toInt();
            for (int k = 2; k <= n; ++k) {
                auto r = geom::copyEntities(E, targets_, E, toLocalXf(Mat4::rotationAbout(center_, normal_, lastAngle_ * k)), true);
                all.insert(all.end(), r.begin(), r.end());
            }
        } else {
            int n = (dm.captured(1).isEmpty() ? dm.captured(2) : dm.captured(1)).toInt();
            for (int k = 1; k < n; ++k) {
                auto r = geom::copyEntities(E, targets_, E, toLocalXf(Mat4::rotationAbout(center_, normal_, lastAngle_ * k / n)), true);
                all.insert(all.end(), r.begin(), r.end());
            }
        }
        doc()->commit();
        doc()->setSelection(all);
        return;
    }
    // degrees, or slope "rise:run"
    double deg;
    if (s.contains(':')) {
        auto p = s.split(':');
        double rise = p[0].toDouble(), run = p.value(1).toDouble();
        if (run == 0) return;
        deg = std::atan2(rise, run) * 180 / kPi;
    } else {
        bool ok;
        deg = s.toDouble(&ok);
        if (!ok) return;
    }
    if (stage_ == 2) {
        double sign = angle_ < 0 ? -1 : 1;
        finish(sign * deg * kPi / 180);
    }
}

void RotateTool::draw(Overlay& ov) {
    // protractor
    Vec3 c = stage_ == 0 ? cur.p : center_;
    Vec3 n = stage_ == 0 ? planeNormalAt(cur) : normal_;
    double R = pixelTol(c, 45);
    Vec3 u = n.anyPerp(), v = n.cross(u);
    QColor pc = std::fabs(n.z) > 0.99 ? icolor::blue() : (std::fabs(n.x) > 0.99 ? icolor::red() : (std::fabs(n.y) > 0.99 ? icolor::green() : QColor(80, 80, 80)));
    for (int i = 0; i < 48; ++i) {
        double a0 = 2 * kPi * i / 48, a1 = 2 * kPi * (i + 1) / 48;
        ov.line(c + (u * std::cos(a0) + v * std::sin(a0)) * R, c + (u * std::cos(a1) + v * std::sin(a1)) * R, pc, 1.5f, 0, false);
    }
    for (int i = 0; i < 24; ++i) {
        double a = 2 * kPi * i / 24;
        double inner = i % 6 == 0 ? 0.75 : 0.88;
        ov.line(c + (u * std::cos(a) + v * std::sin(a)) * (R * inner), c + (u * std::cos(a) + v * std::sin(a)) * R, pc, 1.0f, 0, false);
    }
    if (stage_ >= 1) {
        Vec3 p = stage_ == 1 ? Plane(normal_, center_).project(cur.p) : ref_;
        ov.line(center_, p, QColor(0, 0, 0), 1.5f, 2, false);
    }
    if (stage_ == 2) {
        Vec3 a = ref_ - center_;
        Vec3 b = Mat4::rotation(normal_, angle_).vector(a);
        ov.line(center_, center_ + b, QColor(0, 0, 0), 1.5f, 2, false);
        if (copy_) {
            drawCopyGhost(ov, Mat4::rotationAbout(center_, normal_, angle_));
        }
    }
    if (lockAxis_ >= 0 && stage_ == 0) {
        ov.tooltip = QString("Plane locked: ") + (lockAxis_ == 0 ? "Red" : lockAxis_ == 1 ? "Green" : "Blue");
        ov.tipX = lastX; ov.tipY = lastY;
        return;
    }
    drawInf(ov);
}

// ======================================================================= Scale

void ScaleTool::activate() {
    reset();
}

void ScaleTool::reset() {
    if (dragging_) endPreview();
    dragging_ = false;
    grip_ = hoverGrip_ = -1;
    refreshBox();
}

void ScaleTool::refreshBox() {
    box_ = BBox();
    targets_ = doc()->sel;
    const Entities& E = doc()->active();
    frame_ = doc()->activeXf();
    // single instance: use its own axes
    if (targets_.size() == 1 && targets_[0].kind == EntKind::Instance) {
        const Instance& in = E.insts[targets_[0].id];
        frame_ = frame_ * in.xf;
        box_ = doc()->model.defBBox(in.def);
        return;
    }
    for (auto& r : targets_) {
        switch (r.kind) {
        case EntKind::Edge: box_.add(E.verts[E.edges[r.id].v[0]].p); box_.add(E.verts[E.edges[r.id].v[1]].p); break;
        case EntKind::Face: for (int v : E.faces[r.id].outer) box_.add(E.verts[v].p); break;
        case EntKind::Instance: box_.add(doc()->model.defBBox(E.insts[r.id].def).transformed(E.insts[r.id].xf)); break;
        default: break;
        }
    }
}

Vec3 ScaleTool::gripPos(int g) const {
    int i = g / 9, j = (g / 3) % 3, k = g % 3;
    Vec3 s = box_.size();
    Vec3 p(box_.lo.x + s.x * i * 0.5, box_.lo.y + s.y * j * 0.5, box_.lo.z + s.z * k * 0.5);
    return frame_.point(p);
}

int ScaleTool::gripAxes(int g, int out[3]) const {
    int c[3] = {g / 9, (g / 3) % 3, g % 3};
    int n = 0;
    for (int a = 0; a < 3; ++a)
        if (c[a] != 1) out[n++] = a;
    return n;
}

Mat4 ScaleTool::scaleXf(const Vec3& s, bool center) const {
    int c[3] = {grip_ / 9, (grip_ / 3) % 3, grip_ % 3};
    Vec3 anchor;
    Vec3 sz = box_.size();
    for (int a = 0; a < 3; ++a) {
        double opp = center ? 1 : (2 - c[a]);
        anchor[a] = box_.lo[a] + sz[a] * opp * 0.5;
    }
    Mat4 local = Mat4::scalingAbout(anchor, s);
    return frame_ * local * frame_.inverted();
}

void ScaleTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    if (dragging_) {
        commitTransform(scaleXf(scale_, ctr_), "Scale");
        dragging_ = false;
        refreshBox();
        return;
    }
    if (targets_.empty()) {
        EntRef r = view->picker.pickActive(e.x, e.y);
        if (r.valid()) {
            doc()->setSelection({r});
            refreshBox();
        }
        return;
    }
    if (hoverGrip_ < 0) return;
    grip_ = hoverGrip_;
    ctr_ = e.ctrl();
    uniformToggle_ = e.shift();
    dragging_ = true;
    scale_ = Vec3(1, 1, 1);
    pressX_ = e.x; pressY_ = e.y;
    copy_ = false;
    beginPreview();
}

void ScaleTool::mouseRelease(const MouseEv& e) {
    if (e.button != Qt::LeftButton || !dragging_) return;
    if (std::hypot(e.x - pressX_, e.y - pressY_) > 12) {
        commitTransform(scaleXf(scale_, ctr_), "Scale");
        dragging_ = false;
        refreshBox();
    }
}

void ScaleTool::mouseMove(const MouseEv& e) {
    lastX = e.x; lastY = e.y;
    if (!dragging_) {
        if (doc()->sel != targets_) refreshBox();
        hoverGrip_ = -1;
        if (!box_.valid()) return;
        double best = 10;
        for (int g = 0; g < 27; ++g) {
            if (g == 13) continue;
            // skip grips on degenerate (flat) dimensions
            int ax[3];
            int n = gripAxes(g, ax);
            bool deg = false;
            for (int k = 0; k < n; ++k) if (box_.size()[ax[k]] < kTol) deg = true;
            if (deg) continue;
            double sx, sy;
            if (!view->cam.project(gripPos(g), sx, sy)) continue;
            double d = std::hypot(sx - e.x, sy - e.y);
            if (d < best) { best = d; hoverGrip_ = g; }
        }
        return;
    }
    bool shift = e.shift() != uniformToggle_;
    int ax[3];
    int n = gripAxes(grip_, ax);
    bool uniform = (n == 3) != shift;
    Mat4 inv = frame_.inverted();
    Ray rw = view->cam.ray(e.x, e.y);
    Ray r{inv.point(rw.o), inv.vector(rw.d)};
    int c[3] = {grip_ / 9, (grip_ / 3) % 3, grip_ % 3};
    Vec3 sz = box_.size();
    Vec3 anchor, gp;
    for (int a = 0; a < 3; ++a) {
        double opp = ctr_ ? 1 : (2 - c[a]);
        anchor[a] = box_.lo[a] + sz[a] * opp * 0.5;
        gp[a] = box_.lo[a] + sz[a] * c[a] * 0.5;
    }
    Vec3 dir = gp - anchor;
    if (dir.length() < 1e-9) return;
    Vec3 s(1, 1, 1);
    if (uniform || n == 1) {
        double t, u;
        if (!closestLineLine(anchor, dir, r.o, r.d, t, u)) return;
        if (uniform) s = Vec3(t, t, t);
        else s[ax[0]] = t;
    } else {
        // plane through anchor spanned by the involved axes
        Vec3 nrm;
        if (n == 2) {
            Vec3 e1, e2;
            e1[ax[0]] = 1; e2[ax[1]] = 1;
            nrm = e1.cross(e2);
        } else {
            nrm = r.d;  // corner non-uniform: use camera-facing plane through grip
        }
        Plane pl(nrm, gp);
        double t = r.hit(pl);
        if (std::isnan(t)) return;
        Vec3 p = r.at(t);
        for (int k = 0; k < n; ++k) {
            int a = ax[k];
            double den = gp[a] - anchor[a];
            if (std::fabs(den) > 1e-9) s[a] = (p[a] - anchor[a]) / den;
        }
    }
    // snap factors near round numbers
    for (int a = 0; a < 3; ++a) {
        double q = std::round(s[a] * 10) / 10;
        if (std::fabs(s[a] - q) < 0.02) s[a] = q;
        if (std::fabs(s[a]) < 1e-3) s[a] = s[a] < 0 ? -1e-3 : 1e-3;
    }
    scale_ = s;
    applyPreview(scaleXf(scale_, ctr_));
    QString v;
    if (uniform) v = QString::number(s.x, 'f', 2);
    else {
        QStringList parts;
        for (int k = 0; k < n; ++k) parts << QString::number(s[ax[k]], 'f', 2);
        v = parts.join(", ");
    }
    setVCB(v);
}

void ScaleTool::vcbEnter(const QString& s) {
    if (!dragging_ && grip_ < 0) return;
    if (!dragging_) return;
    int ax[3];
    int n = gripAxes(grip_, ax);
    QStringList parts = s.split(QRegularExpression("[;,]"));
    Vec3 sc = scale_;
    bool uniform = parts.size() == 1 && n == 3;
    for (int k = 0; k < parts.size() && k < 3; ++k) {
        QString t = parts[k].trimmed();
        double f;
        bool ok;
        f = t.toDouble(&ok);
        if (!ok) {
            double L;
            if (!parseLength(t, doc()->model.units, L)) return;
            int a = k < n ? ax[k] : ax[0];
            double cur = box_.size()[a];
            if (cur < 1e-9) return;
            f = L / cur;
        }
        if (uniform) sc = Vec3(f, f, f);
        else if (parts.size() == 1) for (int j = 0; j < n; ++j) sc[ax[j]] = f;
        else if (k < n) sc[ax[k]] = f;
    }
    scale_ = sc;
    commitTransform(scaleXf(scale_, ctr_), "Scale");
    dragging_ = false;
    refreshBox();
}

void ScaleTool::draw(Overlay& ov) {
    if (!box_.valid() || targets_.empty()) return;
    BBox b = box_;
    static const int ed[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    Mat4 F = dragging_ ? scaleXf(scale_, ctr_) * frame_ : frame_;
    for (auto& x : ed) ov.line(F.point(b.corner(x[0])), F.point(b.corner(x[1])), QColor(240, 200, 0), 1.5f, 0, false);
    for (int g = 0; g < 27; ++g) {
        if (g == 13) continue;
        int i = g / 9, j = (g / 3) % 3, k = g % 3;
        Vec3 s = b.size();
        Vec3 p = F.point(Vec3(b.lo.x + s.x * i * 0.5, b.lo.y + s.y * j * 0.5, b.lo.z + s.z * k * 0.5));
        bool hot = g == (dragging_ ? grip_ : hoverGrip_);
        ov.marker(p, hot ? QColor(220, 30, 30) : QColor(40, 160, 40), Overlay::Square, hot ? 9 : 7);
    }
}

// ======================================================================= Offset

double OffsetTool::boundaryDist(const Vec3& lp) const {
    const Entities& E = doc()->active();
    const Face& F = E.faces[face_];
    Frame2 fr(F.plane);
    Vec2 q = fr.to2(lp);
    double best = 1e300;
    auto loopD = [&](const std::vector<int>& L) {
        for (size_t i = 0; i < L.size(); ++i)
            best = std::min(best, distPointSeg2(q, fr.to2(E.verts[L[i]].p), fr.to2(E.verts[L[(i + 1) % L.size()]].p)));
    };
    loopD(F.outer);
    for (auto& h : F.holes) loopD(h);
    bool inside = E.pointInFace(face_, F.plane.project(lp), 0);
    return inside ? best : -best;
}

void OffsetTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    if (active_) { commit(dist_); return; }
    PickHit h;
    EntRef r = view->picker.pickActive(e.x, e.y, &h);
    int f = -1;
    if (r.kind == EntKind::Face) f = r.id;
    else if (r.kind == EntKind::Edge) {
        // pick the face next to the edge
        const Entities& E = doc()->active();
        for (int g : E.edges[r.id].faces) if (E.faces[g].alive) { f = g; break; }
    }
    if (f < 0) {
        auto sf = doc()->selectedFaces();
        if (sf.size() == 1) f = sf[0];
    }
    if (f < 0) return;
    face_ = f;
    active_ = true;
    pressX_ = e.x; pressY_ = e.y;
    const Entities& E = doc()->active();
    Mat4 inv = doc()->activeInv();
    Ray rw = view->cam.ray(e.x, e.y);
    Ray rl{inv.point(rw.o), inv.vector(rw.d)};
    double t = rl.hit(E.faces[f].plane);
    Vec3 p = std::isnan(t) ? E.faceCentroid(f) : rl.at(t);
    startD_ = boundaryDist(p);
    dist_ = 0;
}

void OffsetTool::mouseMove(const MouseEv& e) {
    lastX = e.x; lastY = e.y;
    if (!active_) {
        EntRef r = view->picker.pickActive(e.x, e.y);
        view->hover.clear();
        if (r.kind == EntKind::Face) view->hover.push_back(r);
        return;
    }
    const Entities& E = doc()->active();
    Mat4 inv = doc()->activeInv();
    Ray rw = view->cam.ray(e.x, e.y);
    Ray rl{inv.point(rw.o), inv.vector(rw.d)};
    double t = rl.hit(E.faces[face_].plane);
    if (std::isnan(t)) return;
    dist_ = boundaryDist(rl.at(t)) - startD_;
    setVCB(formatLength(dist_, doc()->model.units));
}

void OffsetTool::mouseRelease(const MouseEv& e) {
    if (active_ && e.button == Qt::LeftButton && std::hypot(e.x - pressX_, e.y - pressY_) > 12) commit(dist_);
}

void OffsetTool::doubleClick(const MouseEv& e) {
    active_ = false;
    EntRef r = view->picker.pickActive(e.x, e.y);
    if (r.kind == EntKind::Face && haveLast_) { face_ = r.id; commit(last_); }
}

void OffsetTool::commit(double d) {
    active_ = false;
    if (face_ < 0 || std::fabs(d) < kTol) return;
    doc()->begin("Offset");
    geom::offsetFace(doc()->active(), face_, d);
    doc()->commit();
    last_ = d;
    haveLast_ = true;
    face_ = -1;
}

void OffsetTool::vcbEnter(const QString& s) {
    double d;
    if (!parseLength(s, doc()->model.units, d)) return;
    if (active_) commit(d);
}

void OffsetTool::draw(Overlay& ov) {
    if (!active_ || face_ < 0) return;
    auto loops = geom::offsetFaceLoops(doc()->active(), face_, dist_);
    Mat4 xf = doc()->activeXf();
    for (auto& L : loops)
        for (size_t i = 0; i < L.size(); ++i) ov.line(xf.point(L[i]), xf.point(L[(i + 1) % L.size()]), QColor(0, 0, 0), 2.0f, 0, false);
}

// ======================================================================= Follow Me

static std::vector<int> chainEdges(const Entities& E, const std::vector<int>& edges, bool& closed) {
    std::unordered_map<int, std::vector<int>> adj;
    for (int e : edges) {
        adj[E.edges[e].v[0]].push_back(e);
        adj[E.edges[e].v[1]].push_back(e);
    }
    int start = E.edges[edges[0]].v[0];
    closed = true;
    for (auto& [v, es] : adj)
        if (es.size() == 1) { start = v; closed = false; break; }
    std::vector<int> chain = {start};
    std::unordered_set<int> used;
    int cur = start;
    while (true) {
        int nx = -1;
        for (int e : adj[cur]) if (!used.count(e)) { nx = e; break; }
        if (nx < 0) break;
        used.insert(nx);
        cur = E.edges[nx].other(cur);
        chain.push_back(cur);
    }
    if (closed && chain.size() > 1 && chain.front() == chain.back()) chain.pop_back();
    return chain;
}

bool FollowMeTool::buildPath(int toEdge) {
    const Entities& E = doc()->active();
    std::unordered_set<int> faceEdges;
    for (int e : E.faceEdges(face_)) faceEdges.insert(e);
    std::vector<int> fv = E.faceVerts(face_);
    // BFS from face vertices through non-face edges
    std::unordered_map<int, int> prev;
    std::vector<int> q;
    for (int v : fv) { prev[v] = -1; q.push_back(v); }
    int target = -1;
    for (size_t i = 0; i < q.size() && target < 0; ++i) {
        int v = q[i];
        for (int e : E.verts[v].edges) {
            if (!E.edges[e].alive || faceEdges.count(e)) continue;
            int o = E.edges[e].other(v);
            if (prev.count(o)) continue;
            prev[o] = v;
            q.push_back(o);
            if (e == toEdge) { target = o; break; }
        }
    }
    path_.clear();
    if (target < 0) return false;
    for (int v = target; v >= 0; v = prev[v]) path_.push_back(v);
    std::reverse(path_.begin(), path_.end());
    return path_.size() >= 2;
}

void FollowMeTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    PickHit h;
    EntRef r = view->picker.pickActive(e.x, e.y, &h);
    Entities& E = doc()->active();
    if (face_ >= 0 && e.alt() && r.kind == EntKind::Face && r.id != face_) {
        // Alt: use the clicked face's perimeter as a closed path
        std::vector<Vec3> pts;
        for (int v : E.faces[r.id].outer) pts.push_back(E.verts[v].p);
        doc()->begin("Follow Me");
        geom::followMe(E, face_, pts, true);
        doc()->commit();
        reset();
        return;
    }
    if (r.kind != EntKind::Face) return;
    auto sel = doc()->selectedEdges();
    if (!sel.empty()) {
        bool closed;
        auto chain = chainEdges(E, sel, closed);
        std::vector<Vec3> pts;
        for (int v : chain) pts.push_back(E.verts[v].p);
        // start the path at the end nearest the profile
        Vec3 c = E.faceCentroid(r.id);
        if (!closed && (pts.back() - c).length() < (pts.front() - c).length()) std::reverse(pts.begin(), pts.end());
        if (closed) {
            // rotate so the path starts at the vertex nearest the profile
            size_t best = 0;
            double bd = 1e300;
            for (size_t i = 0; i < pts.size(); ++i) { double d = (pts[i] - c).length(); if (d < bd) { bd = d; best = i; } }
            std::rotate(pts.begin(), pts.begin() + best, pts.end());
        }
        doc()->begin("Follow Me");
        bool ok = geom::followMe(E, r.id, pts, closed);
        doc()->commit();
        if (ok) doc()->clearSelection();
        return;
    }
    face_ = r.id;
}

void FollowMeTool::mouseMove(const MouseEv& e) {
    lastX = e.x; lastY = e.y;
    if (face_ < 0) {
        EntRef r = view->picker.pickActive(e.x, e.y);
        view->hover.clear();
        if (r.kind == EntKind::Face) view->hover.push_back(r);
        return;
    }
    EntRef r = view->picker.pickActive(e.x, e.y);
    if (r.kind == EntKind::Edge) buildPath(r.id);
}

void FollowMeTool::mouseRelease(const MouseEv& e) {
    if (e.button != Qt::LeftButton || face_ < 0) return;
    if (path_.size() >= 2) {
        Entities& E = doc()->active();
        std::vector<Vec3> pts;
        for (int v : path_) pts.push_back(E.verts[v].p);
        doc()->begin("Follow Me");
        geom::followMe(E, face_, pts, false);
        doc()->commit();
        reset();
    }
}

void FollowMeTool::draw(Overlay& ov) {
    if (face_ < 0 || path_.size() < 2) return;
    const Entities& E = doc()->active();
    Mat4 xf = doc()->activeXf();
    for (size_t i = 1; i < path_.size(); ++i)
        ov.line(xf.point(E.verts[path_[i - 1]].p), xf.point(E.verts[path_[i]].p), QColor(220, 30, 30), 3.0f, 0, false);
}

// ======================================================================= Eraser

void EraserTool::markAt(double x, double y) {
    EntRef r = view->picker.pickActive(x, y);
    if (!r.valid() || r.kind == EntKind::Face) return;
    const Entities& E = doc()->active();
    if (r.kind == EntKind::Edge && E.edges[r.id].curve >= 0 && !(mods_ & Qt::ControlModifier)) {
        for (int e : geom::curveEdges(E, E.edges[r.id].curve)) marked_.insert({EntKind::Edge, e});
    } else marked_.insert(r);
    view->hover.assign(marked_.begin(), marked_.end());
}

void EraserTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    down_ = true;
    mods_ = e.mods;
    marked_.clear();
    markAt(e.x, e.y);
}

void EraserTool::mouseMove(const MouseEv& e) {
    if (down_) { markAt(e.x, e.y); return; }
    EntRef r = view->picker.pickActive(e.x, e.y);
    view->hover.clear();
    if (r.valid() && r.kind != EntKind::Face) view->hover.push_back(r);
}

void EraserTool::mouseRelease(const MouseEv& e) {
    if (e.button != Qt::LeftButton || !down_) return;
    down_ = false;
    if (marked_.empty()) return;
    Entities& E = doc()->active();
    bool shift = mods_ & Qt::ShiftModifier, ctrl = mods_ & Qt::ControlModifier;
    std::vector<int> edges;
    doc()->begin(shift && !ctrl ? "Hide" : (ctrl ? "Soften" : "Erase"));
    for (auto& r : marked_) {
        if (r.kind == EntKind::Edge) {
            Edge& Ed = E.edges[r.id];
            if (ctrl && shift) { Ed.soft = Ed.smooth = false; }
            else if (ctrl) { Ed.soft = Ed.smooth = true; }
            else if (shift) Ed.hidden = true;
            else edges.push_back(r.id);
        } else if (!ctrl) {
            switch (r.kind) {
            case EntKind::Instance: if (shift) E.insts[r.id].hidden = true; else E.insts[r.id].alive = false; break;
            case EntKind::Guide: E.guides[r.id].alive = false; break;
            case EntKind::Dimension: E.dims[r.id].alive = false; break;
            case EntKind::Text: E.texts[r.id].alive = false; break;
            case EntKind::Section: E.sections[r.id].alive = false; break;
            default: break;
            }
        }
    }
    if (!edges.empty()) geom::eraseEdges(E, edges);
    E.touch();
    doc()->commit();
    marked_.clear();
    view->hover.clear();
}

void EraserTool::draw(Overlay&) {}

// ======================================================================= Paint

void PaintTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    PickHit h;
    EntRef r = view->picker.pickActive(e.x, e.y, &h);
    Entities& E = doc()->active();
    // which side of the face?
    bool back = false;
    if (r.kind == EntKind::Face) {
        Vec3 n = doc()->activeXf().normal(E.faces[r.id].plane.n);
        Ray ray = view->cam.ray(e.x, e.y);
        back = n.dot(ray.d) > 0;
    }
    if (e.alt()) {
        int m = -1;
        if (r.kind == EntKind::Face) m = back ? E.faces[r.id].matBack : E.faces[r.id].matFront;
        else if (r.kind == EntKind::Instance) m = E.insts[r.id].material;
        if (sampled) sampled(m);
        return;
    }
    int mat = currentMaterial ? currentMaterial() : -1;
    if (!r.valid()) return;
    doc()->begin("Paint");
    auto paintFace = [&](int f, bool b) {
        if (b) E.faces[f].matBack = mat;
        else E.faces[f].matFront = mat;
    };
    if (r.kind == EntKind::Face) {
        int old = back ? E.faces[r.id].matBack : E.faces[r.id].matFront;
        if (doc()->isSelected(r)) {
            for (auto& s : doc()->sel) {
                if (s.kind == EntKind::Face) paintFace(s.id, back);
                else if (s.kind == EntKind::Instance) E.insts[s.id].material = mat;
            }
        } else if (e.ctrl() && e.shift()) {
            for (auto& c : geom::connected(E, r, true))
                if (c.kind == EntKind::Face && (back ? E.faces[c.id].matBack : E.faces[c.id].matFront) == old) paintFace(c.id, back);
        } else if (e.ctrl()) {
            for (auto& c : geom::connected(E, r, true))
                if (c.kind == EntKind::Face && (back ? E.faces[c.id].matBack : E.faces[c.id].matFront) == old) paintFace(c.id, back);
        } else if (e.shift()) {
            for (int f = 0; f < (int)E.faces.size(); ++f) {
                if (!E.faces[f].alive) continue;
                if (E.faces[f].matFront == old) E.faces[f].matFront = mat;
                if (E.faces[f].matBack == old && back) E.faces[f].matBack = mat;
            }
        } else paintFace(r.id, back);
    } else if (r.kind == EntKind::Instance) {
        if (doc()->isSelected(r)) {
            for (auto& s : doc()->sel) {
                if (s.kind == EntKind::Instance) E.insts[s.id].material = mat;
                else if (s.kind == EntKind::Face) paintFace(s.id, false);
            }
        } else E.insts[r.id].material = mat;
    } else if (r.kind == EntKind::Edge) {
        E.edges[r.id].material = mat;
    }
    E.touch();
    doc()->commit();
}

}  // namespace gable
