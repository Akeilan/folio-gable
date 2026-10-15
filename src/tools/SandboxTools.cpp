#include "core/Geometry.h"
#include "core/Sandbox.h"
#include "tools/Tools.h"
#include "view/Viewport.h"

namespace gable {

// ======================================================================= From Scratch

void SandboxScratchTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    infer(e);
    if (stage_ == 0) { o_ = cur.p; stage_ = 1; inf().setAnchor(o_); return; }
    if (stage_ == 1) {
        Vec3 d = cur.p - o_;
        d.z = 0;
        if (d.length() < kTol) return;
        x_ = o_ + d;
        stage_ = 2;
        inf().setAnchor(x_);
        return;
    }
    finish(cur.p);
}

void SandboxScratchTool::finish(const Vec3& p) {
    Vec3 xd = x_ - o_;
    double L = xd.length();
    xd = xd / L;
    Vec3 yd = Vec3(0, 0, 1).cross(xd);
    double W = (p - o_).dot(yd);
    if (std::fabs(W) < kTol) return;
    if (W < 0) { yd = -yd; W = -W; }
    Mat4 inv = doc()->activeInv();
    doc()->begin("Sandbox From Scratch");
    int def = doc()->model.addDef("Grid", true);
    Entities G;
    sandbox::makeGrid(G, inv.point(o_), inv.vector(xd), inv.vector(yd), L, W, cell_);
    doc()->model.defs[def].ents = std::move(G);
    Instance in;
    in.def = def;
    doc()->active().insts.push_back(in);
    doc()->active().touch();
    doc()->commit();
    doc()->setSelection({{EntKind::Instance, (int)doc()->active().insts.size() - 1}});
    reset();
}

void SandboxScratchTool::vcbEnter(const QString& s) {
    double v;
    if (!parseLength(s, doc()->model.units, v) || v <= 0) return;
    if (stage_ == 0) { cell_ = v; setVCB(formatLength(v, doc()->model.units)); return; }
    if (stage_ == 1) {
        Vec3 d = cur.p - o_;
        d.z = 0;
        if (d.length() < 1e-9) return;
        x_ = o_ + d.normalized() * v;
        stage_ = 2;
        inf().setAnchor(x_);
        return;
    }
    Vec3 xd = (x_ - o_).normalized();
    Vec3 yd = Vec3(0, 0, 1).cross(xd);
    double sign = (cur.p - o_).dot(yd) < 0 ? -1 : 1;
    finish(o_ + yd * (sign * v));
}

void SandboxScratchTool::draw(Overlay& ov) {
    if (stage_ == 1) ov.line(o_, Vec3(cur.p.x, cur.p.y, o_.z), QColor(0, 0, 0), 1.5f, 0, false);
    if (stage_ == 2) {
        Vec3 xd = x_ - o_;
        double L = xd.length();
        xd = xd / L;
        Vec3 yd = Vec3(0, 0, 1).cross(xd);
        double W = (cur.p - o_).dot(yd);
        int nx = std::max(1, (int)std::round(L / cell_)), ny = std::max(1, (int)std::round(std::fabs(W) / cell_));
        if (nx * ny < 20000) {
            for (int i = 0; i <= nx; ++i) ov.line(o_ + xd * (L * i / nx), o_ + xd * (L * i / nx) + yd * W, QColor(60, 60, 60), 1.0f, 0, false);
            for (int j = 0; j <= ny; ++j) ov.line(o_ + yd * (W * j / ny), o_ + yd * (W * j / ny) + xd * L, QColor(60, 60, 60), 1.0f, 0, false);
        }
    }
    drawInf(ov);
}

// ======================================================================= Smoove

void SmooveTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    if (active_) { apply(dz_); return; }
    Picker::DeepResult dr = view->picker.deep(e.x, e.y, 0, 0);
    if (!dr.face.valid()) return;
    // terrain may be loose geometry in the open context or a group directly inside it
    const InstPath& ap = doc()->editPath;
    if (dr.face.path.size() < ap.size() || !std::equal(ap.begin(), ap.end(), dr.face.path.begin())) return;
    path_ = dr.face.path;
    if (path_.size() > ap.size() + 1) path_.resize(ap.size() + 1);  // operate on the top-level group in this context
    xf_ = doc()->xfAt(path_);
    centerW_ = dr.face.point;
    active_ = true;
    dz_ = 0;
    pressX_ = e.x;
    pressY_ = e.y;
    Inference& I = inf();
    I.reset();
    I.setAnchor(centerW_);
    I.lock = Inference::Lock::Line;
    I.lockPt = centerW_;
    I.lockDir = xf_.vector(Vec3(0, 0, 1)).normalized();
    I.lockColor = icolor::blue();
    I.lockAxis = 2;
}

void SmooveTool::mouseMove(const MouseEv& e) {
    lastX = e.x;
    lastY = e.y;
    if (!active_) {
        Picker::DeepResult dr = view->picker.deep(e.x, e.y, 0, 0);
        hoverOk_ = dr.face.valid();
        if (hoverOk_) hoverW_ = dr.face.point;
        return;
    }
    infer(e);
    dz_ = (cur.p - centerW_).dot(xf_.vector(Vec3(0, 0, 1)).normalized());
    setVCB(formatLength(dz_, doc()->model.units));
}

void SmooveTool::mouseRelease(const MouseEv& e) {
    if (active_ && e.button == Qt::LeftButton && std::hypot(e.x - pressX_, e.y - pressY_) > 12) apply(dz_);
}

void SmooveTool::apply(double dz) {
    active_ = false;
    inf().reset();
    if (std::fabs(dz) < kTol) return;
    // make shared groups unique before editing their geometry
    if (path_.size() > doc()->editPath.size()) {
        Entities& parent = doc()->entitiesAt(InstPath(path_.begin(), path_.end() - 1));
        int def = parent.insts[path_.back()].def;
        doc()->begin("Smoove");
        if (doc()->model.instanceCount(def) > 1) {
            ComponentDef copy = doc()->model.defs[def];
            copy.name = doc()->model.uniqueDefName(copy.name);
            doc()->model.defs.push_back(std::move(copy));
            parent.insts[path_.back()].def = (int)doc()->model.defs.size() - 1;
            parent.touch();
        }
    } else doc()->begin("Smoove");
    Entities& T = doc()->entitiesAt(path_);
    Mat4 inv = xf_.inverted();
    Vec3 c = inv.point(centerW_);
    double scale = inv.vector(Vec3(radius_, 0, 0)).length();
    double dzl = inv.vector(xf_.vector(Vec3(0, 0, 1)).normalized() * dz).z;
    sandbox::smoove(T, c, scale, dzl);
    doc()->commit();
}

void SmooveTool::vcbEnter(const QString& s) {
    double v;
    if (!parseLength(s, doc()->model.units, v)) return;
    if (active_) { apply(v * (dz_ < 0 ? -1 : 1)); return; }
    if (v > 0) { radius_ = v; setVCB(formatLength(v, doc()->model.units)); }
}

void SmooveTool::draw(Overlay& ov) {
    Vec3 c = active_ ? centerW_ : hoverW_;
    if (!active_ && !hoverOk_) return;
    Vec3 up = xf_.vector(Vec3(0, 0, 1)).normalized();
    if (up.isZero()) up = Vec3(0, 0, 1);
    Vec3 u = up.anyPerp(), v = up.cross(u);
    for (int i = 0; i < 48; ++i) {
        double a0 = 2 * kPi * i / 48, a1 = 2 * kPi * (i + 1) / 48;
        ov.line(c + (u * std::cos(a0) + v * std::sin(a0)) * radius_, c + (u * std::cos(a1) + v * std::sin(a1)) * radius_,
                QColor(220, 40, 40), 1.5f, 0, false);
    }
    if (active_) {
        // preview of displaced edges
        const Entities& T = doc()->entitiesAt(path_);
        Mat4 inv = xf_.inverted();
        Vec3 cl = inv.point(centerW_);
        double rl = inv.vector(Vec3(radius_, 0, 0)).length();
        auto w = sandbox::smooveWeights(T, cl, rl);
        std::unordered_map<int, double> wm(w.begin(), w.end());
        double dzl = inv.vector(up * dz_).z;
        int n = 0;
        for (auto& Ed : T.edges) {
            if (!Ed.alive) continue;
            auto ia = wm.find(Ed.v[0]), ib = wm.find(Ed.v[1]);
            if (ia == wm.end() && ib == wm.end()) continue;
            if (++n > 6000) break;
            Vec3 a = T.verts[Ed.v[0]].p + Vec3(0, 0, ia == wm.end() ? 0 : dzl * ia->second);
            Vec3 b = T.verts[Ed.v[1]].p + Vec3(0, 0, ib == wm.end() ? 0 : dzl * ib->second);
            ov.line(xf_.point(a), xf_.point(b), QColor(30, 30, 30), 1.0f, 0, false);
        }
        ov.line(centerW_, centerW_ + up * dz_, icolor::blue(), 2.0f, 2, false);
    }
}

// ======================================================================= Flip Edge

void FlipEdgeTool::mouseMove(const MouseEv& e) {
    Picker::DeepResult dr = view->picker.deep(e.x, e.y, 0, 6);
    view->hover.clear();
    if (dr.edge.valid() && dr.edge.path == doc()->editPath) view->hover.push_back({EntKind::Edge, dr.edge.id});
}

void FlipEdgeTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    // soft edges are hidden; search them too
    bool showHidden = doc()->model.style.showHidden;
    doc()->model.style.showHidden = true;
    Picker::DeepResult dr = view->picker.deep(e.x, e.y, 0, 6);
    doc()->model.style.showHidden = showHidden;
    if (!dr.edge.valid()) return;
    const InstPath& ap = doc()->editPath;
    InstPath p = dr.edge.path;
    if (p.size() < ap.size() || !std::equal(ap.begin(), ap.end(), p.begin())) return;
    doc()->begin("Flip Edge");
    bool ok = sandbox::flipEdge(doc()->entitiesAt(p), dr.edge.id);
    if (ok) doc()->commit();
    else doc()->cancel();
}

// ======================================================================= Drape

void DrapeTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    auto es = doc()->selectedEdges();
    if (es.empty()) { setStatus("Select the edges to drape first."); return; }
    Picker::DeepResult dr = view->picker.deep(e.x, e.y, 0, 0);
    if (!dr.face.valid()) return;
    const Entities& A = doc()->active();
    Mat4 axf = doc()->activeXf();
    Mat4 inv = dr.face.xf.inverted();
    std::vector<std::pair<Vec3, Vec3>> segs;
    for (int e2 : es) {
        const Edge& Ed = A.edges[e2];
        segs.push_back({inv.point(axf.point(A.verts[Ed.v[0]].p)), inv.point(axf.point(A.verts[Ed.v[1]].p))});
    }
    doc()->begin("Drape");
    InstPath p = dr.face.path;
    if (p.size() > doc()->editPath.size()) {
        Entities& parent = doc()->entitiesAt(InstPath(p.begin(), p.end() - 1));
        int def = parent.insts[p.back()].def;
        if (doc()->model.instanceCount(def) > 1) {
            ComponentDef copy = doc()->model.defs[def];
            copy.name = doc()->model.uniqueDefName(copy.name);
            doc()->model.defs.push_back(std::move(copy));
            parent.insts[p.back()].def = (int)doc()->model.defs.size() - 1;
            parent.touch();
        }
    }
    int n = sandbox::drape(doc()->entitiesAt(p), segs);
    doc()->commit();
    setStatus(QString("Draped %1 segments.").arg(n));
}

}  // namespace gable
