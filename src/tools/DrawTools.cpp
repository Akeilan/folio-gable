#include "core/Geometry.h"
#include "tools/Tools.h"
#include "view/Viewport.h"
#include <QRegularExpression>

namespace gable {

// ======================================================================= InferTool

void InferTool::infer(const MouseEv& e) {
    lastX = e.x;
    lastY = e.y;
    cur = inf().compute(e.x, e.y);
}

void InferTool::reinfer() { cur = inf().compute(lastX, lastY); }

bool InferTool::lockKeys(QKeyEvent* e) {
    if (e->isAutoRepeat()) return e->key() == Qt::Key_Shift;
    switch (e->key()) {
    case Qt::Key_Shift: inf().lockFrom(cur); reinfer(); return true;
    case Qt::Key_Right: inf().lockArrow(0); reinfer(); return true;
    case Qt::Key_Left: inf().lockArrow(1); reinfer(); return true;
    case Qt::Key_Up: inf().lockArrow(2); reinfer(); return true;
    case Qt::Key_Down: inf().lockArrow(3); reinfer(); return true;
    default: return false;
    }
}

bool InferTool::lockRelease(QKeyEvent* e) {
    if (e->key() == Qt::Key_Shift && !e->isAutoRepeat()) {
        if (inf().lock != Inference::Lock::None && !inf().lockFromArrow) inf().clearLock();
        reinfer();
        return true;
    }
    return false;
}

void InferTool::drawInf(Overlay& ov) const { inf().feedback(ov, cur, lastX, lastY); }

bool InferTool::parseList(const QString& s, std::vector<double>& out) const {
    out.clear();
    QStringList parts = s.split(QRegularExpression("[;,]"));
    for (auto& p : parts) {
        QString t = p.trimmed();
        if (t.isEmpty()) { out.push_back(std::nan("")); continue; }
        double v;
        if (!parseLength(t, doc()->model.units, v)) return false;
        out.push_back(v);
    }
    return !out.empty();
}

bool InferTool::parseCoords(const QString& sIn, Vec3& v, bool& relative) const {
    QString s = sIn.trimmed();
    if (s.size() < 2) return false;
    if (s.startsWith('[') && s.endsWith(']')) relative = false;
    else if (s.startsWith('<') && s.endsWith('>')) relative = true;
    else return false;
    std::vector<double> l;
    if (!parseList(s.mid(1, s.size() - 2), l) || l.size() != 3) return false;
    for (double& d : l) if (std::isnan(d)) d = 0;
    v = Vec3(l[0], l[1], l[2]);
    return true;
}

Plane InferTool::shapePlane(const InfResult& r, int lockedAxis) const {
    if (lockedAxis >= 0 && lockedAxis < 3) return Plane(inf().axis(lockedAxis), r.p);
    if (r.onFace && std::fabs(r.facePlane.dist(r.p)) < pixelTol(r.p, 2) + 0.01) return Plane(r.facePlane.n, r.p);
    return Plane(inf().axis(2), r.p);
}

Vec3 InferTool::toLocal(const Vec3& w) const { return doc()->activeInv().point(w); }
Vec3 InferTool::dirToLocal(const Vec3& w) const { return doc()->activeInv().vector(w); }

// ======================================================================= Select

std::vector<EntRef> SelectTool::expand(EntRef r) const {
    const Entities& E = doc()->active();
    if (r.kind == EntKind::Edge && E.edges[r.id].curve >= 0) {
        std::vector<EntRef> out;
        for (int e : geom::curveEdges(E, E.edges[r.id].curve)) out.push_back({EntKind::Edge, e});
        return out;
    }
    return {r};
}

void SelectTool::applyClick(const std::vector<EntRef>& refs, Qt::KeyboardModifiers m) {
    bool ctrl = m & Qt::ControlModifier, shift = m & Qt::ShiftModifier;
    if (ctrl && shift) doc()->removeFromSelection(refs);
    else if (ctrl) doc()->addToSelection(refs);
    else if (shift) doc()->toggleSelection(refs);
    else doc()->setSelection(refs);
}

void SelectTool::mousePress(const MouseEv& e) {
    if (e.button == Qt::RightButton) {
        EntRef r = view->picker.pickActive(e.x, e.y);
        if (r.valid() && !doc()->isSelected(r)) doc()->setSelection(expand(r));
        return;
    }
    if (e.button != Qt::LeftButton) return;
    pressed_ = true;
    dragging_ = false;
    px_ = cx_ = e.x;
    py_ = cy_ = e.y;
}

void SelectTool::mouseMove(const MouseEv& e) {
    if (pressed_ && (e.buttons & Qt::LeftButton)) {
        cx_ = e.x;
        cy_ = e.y;
        if (std::hypot(cx_ - px_, cy_ - py_) > 4) dragging_ = true;
        view->hover.clear();
        return;
    }
    EntRef r = view->picker.pickActive(e.x, e.y);
    view->hover = r.valid() ? expand(r) : std::vector<EntRef>{};
}

void SelectTool::mouseRelease(const MouseEv& e) {
    if (e.button != Qt::LeftButton || !pressed_) return;
    pressed_ = false;
    if (dragging_) {
        dragging_ = false;
        bool crossing = cx_ < px_;
        auto refs = view->picker.pickBox(px_, py_, cx_, cy_, crossing);
        applyClick(refs, e.mods);
        return;
    }
    EntRef r = view->picker.pickActive(e.x, e.y);
    if (!r.valid()) {
        if (doc()->inContext()) {
            // clicking outside the open group closes it
            Picker::DeepResult dr = view->picker.deep(e.x, e.y, 0, 5);
            bool hitSomething = dr.face.valid() || dr.edge.valid();
            if (hitSomething || true) { doc()->closeContext(); doc()->clearSelection(); return; }
        }
        if (!(e.mods & (Qt::ControlModifier | Qt::ShiftModifier))) doc()->clearSelection();
        return;
    }
    // triple click => all connected
    if (dblTimer_.isValid() && dblTimer_.elapsed() < 450 && (r.kind == EntKind::Edge || r.kind == EntKind::Face)) {
        dblTimer_.invalidate();
        applyClick(geom::connected(doc()->active(), r, true), e.mods);
        return;
    }
    applyClick(expand(r), e.mods);
}

void SelectTool::doubleClick(const MouseEv& e) {
    EntRef r = view->picker.pickActive(e.x, e.y);
    if (!r.valid()) {
        if (doc()->inContext()) doc()->closeContext();
        return;
    }
    if (r.kind == EntKind::Instance) {
        doc()->openInstance(r.id);
        return;
    }
    if (r.kind == EntKind::Edge || r.kind == EntKind::Face) {
        auto c = geom::connected(doc()->active(), r, false);
        // include whole curves for edges
        std::vector<EntRef> all;
        for (auto& x : c) {
            auto ex = expand(x);
            all.insert(all.end(), ex.begin(), ex.end());
        }
        applyClick(all, e.mods);
        dblTimer_.start();
        dblRef_ = r;
    }
}

void SelectTool::draw(Overlay& ov) {
    if (dragging_) {
        bool crossing = cx_ < px_;
        ov.boxes.push_back({px_, py_, cx_, cy_, crossing, QColor(30, 30, 30)});
    }
}

// ======================================================================= Line

void LineTool::reset() {
    started_ = false;
    inf().reset();
    setVCB(QString());
}

void LineTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    infer(e);
    pressX_ = e.x;
    pressY_ = e.y;
    if (!started_) {
        started_ = true;
        start_ = chainStart_ = cur.p;
        inf().setAnchor(start_);
        pressedNoMove_ = true;
    } else {
        placeEnd(cur.p);
        pressedNoMove_ = false;
    }
}

void LineTool::mouseRelease(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    // click-drag-release draws a line too
    if (started_ && pressedNoMove_ && std::hypot(e.x - pressX_, e.y - pressY_) > 12) {
        infer(e);
        placeEnd(cur.p);
    }
    pressedNoMove_ = false;
}

void LineTool::mouseMove(const MouseEv& e) {
    infer(e);
    if (started_) setVCB(formatLength((cur.p - start_).length(), doc()->model.units));
}

void LineTool::placeEnd(const Vec3& p) {
    if ((p - start_).length() < kTol) return;
    auto liveFaces = [&] {
        int n = 0;
        for (auto& f : doc()->active().faces) n += f.alive;
        return n;
    };
    int before = liveFaces();
    doc()->begin("Line");
    geom::addLine(doc()->active(), toLocal(start_), toLocal(p));
    doc()->commit();
    // Like the reference app: the chain ends when it closes a loop or creates a face.
    if (p.near(chainStart_, kTol * 10) || liveFaces() > before) {
        reset();
        return;
    }
    start_ = p;
    inf().setAnchor(p);
    if (!inf().lockFromArrow) inf().clearLock();
}

void LineTool::vcbEnter(const QString& s) {
    if (!started_) return;
    Vec3 v;
    bool rel;
    if (parseCoords(s, v, rel)) {
        Vec3 p = rel ? start_ + doc()->activeXf().vector(v) : doc()->activeXf().point(v);
        placeEnd(p);
        return;
    }
    double L;
    if (!parseLength(s, doc()->model.units, L)) return;
    Vec3 d = cur.p - start_;
    if (d.length() < 1e-9) return;
    placeEnd(start_ + d.normalized() * L);
}

void LineTool::draw(Overlay& ov) {
    if (started_) {
        QColor c = (cur.type == InfType::OnAxis || cur.type == InfType::Parallel) ? cur.lineColor : QColor(0, 0, 0);
        ov.line(start_, cur.p, c, 2.0f, 0, false);
    }
    drawInf(ov);
}

// ======================================================================= Freehand

void FreehandTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    infer(e);
    plane_ = shapePlane(cur, -1);
    pts_ = {cur.p};
    drawing_ = true;
}

void FreehandTool::mouseMove(const MouseEv& e) {
    if (!drawing_) { infer(e); return; }
    lastX = e.x; lastY = e.y;
    Ray r = view->cam.ray(e.x, e.y);
    double t = r.hit(plane_);
    if (std::isnan(t)) return;
    Vec3 p = r.at(t);
    if ((p - pts_.back()).length() > pixelTol(p, 6)) pts_.push_back(p);
}

void FreehandTool::mouseRelease(const MouseEv& e) {
    if (!drawing_ || e.button != Qt::LeftButton) return;
    drawing_ = false;
    if (pts_.size() < 2) { pts_.clear(); return; }
    bool closed = pts_.size() > 3 && (pts_.front() - pts_.back()).length() < pixelTol(pts_.front(), 12);
    if (closed) pts_.pop_back();
    std::vector<Vec3> local;
    for (auto& p : pts_) local.push_back(toLocal(p));
    Curve cv;
    cv.closed = closed;
    doc()->begin("Freehand");
    geom::addPolyline(doc()->active(), local, closed, &cv);
    doc()->commit();
    pts_.clear();
}

void FreehandTool::draw(Overlay& ov) {
    for (size_t i = 1; i < pts_.size(); ++i) ov.line(pts_[i - 1], pts_[i], QColor(0, 0, 0), 2.0f, 0, false);
    if (!drawing_) drawInf(ov);
}

// ======================================================================= Rectangle

static void planeAxes(const Plane& P, const Inference& inf, Vec3& u, Vec3& v) {
    Vec3 n = P.n;
    Vec3 cand[3] = {inf.axis(0), inf.axis(1), inf.axis(2)};
    // pick the axis most perpendicular to the normal as u
    int best = 0;
    double bd = 2;
    for (int i = 0; i < 3; ++i) {
        double d = std::fabs(cand[i].dot(n));
        if (d < bd - 1e-9) { bd = d; best = i; }
    }
    // prefer horizontal edges for vertical planes
    if (std::fabs(n.dot(inf.axis(2))) < 1e-6) {
        Vec3 h = inf.axis(2).cross(n);
        u = h.normalized();
    } else {
        u = (cand[best] - n * cand[best].dot(n)).normalized();
    }
    v = n.cross(u).normalized();
}

QString RectTool::status() const {
    if (!rotated_) {
        if (centered_) return stage_ == 0 ? "Select the centre. Ctrl = draw from a corner." : "Select a corner or enter the size. Ctrl = from a corner.";
        return stage_ == 0 ? "Select first corner. Ctrl = draw from the centre." : "Select opposite corner or enter value. Ctrl = from the centre.";
    }
    return stage_ == 0 ? "Select first corner." : (stage_ == 1 ? "Select second corner (length and angle)." : "Select third corner (width).");
}

bool RectTool::keyPress(QKeyEvent* e) {
    if (!rotated_ && e->key() == Qt::Key_Control && !e->isAutoRepeat()) {
        // Ctrl draws the rectangle out from its centre (toggles)
        centered_ = !centered_;
        return true;
    }
    if (!rotated_ && stage_ == 0 && !e->isAutoRepeat()) {
        int ax = -1;
        if (e->key() == Qt::Key_Right) ax = 0;
        else if (e->key() == Qt::Key_Left) ax = 1;
        else if (e->key() == Qt::Key_Up) ax = 2;
        if (ax >= 0) { lockAxis_ = lockAxis_ == ax ? -1 : ax; return true; }
    }
    if (!rotated_ && stage_ == 1) {
        if (e->key() == Qt::Key_Shift) return true;
        return false;
    }
    return lockKeys(e);
}

void RectTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    infer(e);
    pressX_ = e.x;
    pressY_ = e.y;
    if (stage_ == 0) {
        p0_ = cur.p;
        if (!rotated_) {
            plane_ = shapePlane(cur, lockAxis_);
            planeAxes(plane_, inf(), u_, v_);
            stage_ = 1;
            inf().setAnchor(p0_);
            inf().allowAxes = false;
        } else {
            stage_ = 1;
            inf().setAnchor(p0_);
        }
        return;
    }
    if (!rotated_) {
        finish();
        return;
    }
    if (stage_ == 1) {
        if ((cur.p - p0_).length() < kTol) return;
        p1_ = cur.p;
        stage_ = 2;
        inf().setAnchor(p1_);
        return;
    }
    finish();
}

void RectTool::mouseRelease(const MouseEv& e) {
    if (e.button != Qt::LeftButton || rotated_) return;
    if (stage_ == 1 && std::hypot(e.x - pressX_, e.y - pressY_) > 12) {
        infer(e);
        finish();
    }
}

void RectTool::mouseMove(const MouseEv& e) {
    infer(e);
    if (stage_ == 1 && !rotated_) {
        Vec3 p;
        bool snapped = cur.type == InfType::Endpoint || cur.type == InfType::Midpoint || cur.type == InfType::OnEdge ||
                       cur.type == InfType::Center || cur.type == InfType::GuidePoint || cur.type == InfType::FromPoint;
        if (snapped) p = plane_.project(cur.p);
        else {
            Ray r = view->cam.ray(e.x, e.y);
            double t = r.hit(plane_);
            p = std::isnan(t) ? plane_.project(cur.p) : r.at(t);
        }
        Vec3 d = p - p0_;
        double a = d.dot(u_), b = d.dot(v_);
        special_.clear();
        double tol = pixelTol(p, 8);
        if (std::fabs(std::fabs(a) - std::fabs(b)) < tol && std::fabs(a) > tol) { b = (b < 0 ? -1 : 1) * std::fabs(a); special_ = "Square"; }
        else if (std::fabs(std::fabs(b) - std::fabs(a) * 0.6180339887) < tol && std::fabs(a) > tol) { b = (b < 0 ? -1 : 1) * std::fabs(a) * 0.6180339887; special_ = "Golden Section"; }
        else if (std::fabs(std::fabs(a) - std::fabs(b) * 0.6180339887) < tol && std::fabs(b) > tol) { a = (a < 0 ? -1 : 1) * std::fabs(b) * 0.6180339887; special_ = "Golden Section"; }
        p1_ = p0_ + u_ * a + v_ * b;
        double k = centered_ ? 2.0 : 1.0;
        setVCB(formatLength(std::fabs(a) * k, doc()->model.units) + ", " + formatLength(std::fabs(b) * k, doc()->model.units));
    } else if (rotated_ && stage_ == 1) {
        setVCB(formatLength((cur.p - p0_).length(), doc()->model.units));
    } else if (rotated_ && stage_ == 2) {
        Vec3 d = (p1_ - p0_).normalized();
        Vec3 w = cur.p - p1_;
        w = w - d * w.dot(d);
        p2_ = p1_ + w;
        setVCB(formatLength(w.length(), doc()->model.units));
    }
}

std::vector<Vec3> RectTool::corners() const {
    if (!rotated_) {
        Vec3 d = p1_ - p0_;
        double a = d.dot(u_), b = d.dot(v_);
        if (centered_) return {p0_ - u_ * a - v_ * b, p0_ + u_ * a - v_ * b, p0_ + u_ * a + v_ * b, p0_ - u_ * a + v_ * b};
        return {p0_, p0_ + u_ * a, p0_ + u_ * a + v_ * b, p0_ + v_ * b};
    }
    Vec3 w = p2_ - p1_;
    return {p0_, p1_, p1_ + w, p0_ + w};
}

void RectTool::finish() {
    auto c = corners();
    double a = (c[1] - c[0]).length(), b = (c[3] - c[0]).length();
    if (a < kTol || b < kTol) {
        // degenerate: draw a line if one dimension exists
        if (a >= kTol || b >= kTol) {
            doc()->begin("Line");
            geom::addLine(doc()->active(), toLocal(c[0]), toLocal(a >= kTol ? c[1] : c[3]));
            doc()->commit();
        }
        reset();
        inf().allowAxes = true;
        return;
    }
    std::vector<Vec3> local;
    for (auto& p : c) local.push_back(toLocal(p));
    doc()->begin("Rectangle");
    geom::addPolyline(doc()->active(), local, true);
    doc()->commit();
    reset();
    inf().allowAxes = true;
}

void RectTool::vcbEnter(const QString& s) {
    std::vector<double> l;
    if (!parseList(s, l)) return;
    if (!rotated_ && stage_ == 1) {
        Vec3 d = p1_ - p0_;
        double a = d.dot(u_), b = d.dot(v_);
        double k = centered_ ? 0.5 : 1.0;   // from the centre, typed sizes are the whole rectangle
        double na = l.size() > 0 && !std::isnan(l[0]) ? l[0] * k : std::fabs(a);
        double nb = l.size() > 1 && !std::isnan(l[1]) ? l[1] * k : std::fabs(b);
        p1_ = p0_ + u_ * (a < 0 ? -na : na) + v_ * (b < 0 ? -nb : nb);
        finish();
    } else if (rotated_ && stage_ == 1 && !l.empty()) {
        Vec3 d = cur.p - p0_;
        if (d.length() < 1e-9) return;
        p1_ = p0_ + d.normalized() * l[0];
        stage_ = 2;
        inf().setAnchor(p1_);
    } else if (rotated_ && stage_ == 2 && !l.empty()) {
        Vec3 w = p2_ - p1_;
        if (w.length() < 1e-9) return;
        p2_ = p1_ + w.normalized() * l[0];
        finish();
    }
}

void RectTool::draw(Overlay& ov) {
    if (stage_ == 0 && !rotated_) {
        drawInf(ov);
        if (lockAxis_ >= 0) ov.tooltip = QString("Plane locked: ") + (lockAxis_ == 0 ? "Red" : lockAxis_ == 1 ? "Green" : "Blue");
        return;
    }
    if (rotated_ && stage_ == 1) {
        ov.line(p0_, cur.p, QColor(0, 0, 0), 2.0f, 0, false);
        drawInf(ov);
        return;
    }
    if (stage_ == 0) { drawInf(ov); return; }
    auto c = corners();
    Overlay::Poly poly;
    poly.pts = c;
    poly.c = QColor(120, 140, 255, 70);
    ov.polys.push_back(poly);
    for (int i = 0; i < 4; ++i) ov.line(c[i], c[(i + 1) % 4], QColor(0, 0, 0), 2.0f, 0, false);
    if (!special_.isEmpty()) {
        ov.line(c[0], c[2], QColor(0, 0, 0), 1.0f, 2);
        ov.tooltip = special_;
        ov.tipX = lastX; ov.tipY = lastY;
    } else drawInf(ov);
}

// ======================================================================= Circle / Polygon

bool CircleTool::keyPress(QKeyEvent* e) {
    if (stage_ == 0 && !e->isAutoRepeat()) {
        int ax = -1;
        if (e->key() == Qt::Key_Right) ax = 0;
        else if (e->key() == Qt::Key_Left) ax = 1;
        else if (e->key() == Qt::Key_Up) ax = 2;
        if (ax >= 0) { lockAxis_ = lockAxis_ == ax ? -1 : ax; return true; }
    }
    return lockKeys(e);
}

void CircleTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    infer(e);
    pressX_ = e.x; pressY_ = e.y;
    if (stage_ == 0) {
        c_ = cur.p;
        plane_ = shapePlane(cur, lockAxis_);
        stage_ = 1;
        inf().setAnchor(c_);
        r_ = c_;
        return;
    }
    finish(r_);
}

void CircleTool::mouseRelease(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    if (stage_ == 1 && std::hypot(e.x - pressX_, e.y - pressY_) > 12) finish(r_);
}

void CircleTool::mouseMove(const MouseEv& e) {
    infer(e);
    if (stage_ == 1) {
        bool snapped = cur.type == InfType::Endpoint || cur.type == InfType::Midpoint || cur.type == InfType::OnEdge || cur.type == InfType::OnAxis;
        if (snapped) r_ = plane_.project(cur.p);
        else {
            Ray r = view->cam.ray(e.x, e.y);
            double t = r.hit(plane_);
            r_ = std::isnan(t) ? plane_.project(cur.p) : r.at(t);
        }
        setVCB(formatLength((r_ - c_).length(), doc()->model.units));
    }
}

std::vector<Vec3> CircleTool::points(const Vec3& rp) const {
    std::vector<Vec3> pts;
    Vec3 d = rp - c_;
    double R = d.length();
    if (R < kTol) return pts;
    Vec3 u = d / R;
    Vec3 v = plane_.n.cross(u).normalized();
    double offset = polygon_ ? 0 : 0;
    for (int i = 0; i < segs_; ++i) {
        double a = 2 * kPi * i / segs_ + offset;
        pts.push_back(c_ + (u * std::cos(a) + v * std::sin(a)) * R);
    }
    return pts;
}

void CircleTool::finish(const Vec3& rp) {
    auto pts = points(rp);
    if (pts.size() < 3) { reset(); return; }
    Mat4 inv = doc()->activeInv();
    std::vector<Vec3> local;
    for (auto& p : pts) local.push_back(inv.point(p));
    Curve cv;
    cv.isArc = !polygon_;
    cv.polygon = polygon_;
    cv.closed = true;
    cv.center = inv.point(c_);
    cv.normal = inv.vector(plane_.n).normalized();
    cv.xaxis = inv.vector((rp - c_).normalized()).normalized();
    cv.radius = (local[0] - cv.center).length();
    cv.startAng = 0;
    cv.endAng = 2 * kPi;
    cv.segments = segs_;
    doc()->begin(polygon_ ? "Polygon" : "Circle");
    geom::addPolyline(doc()->active(), local, true, &cv);
    doc()->commit();
    reset();
}

void CircleTool::vcbEnter(const QString& sIn) {
    QString s = sIn.trimmed();
    if (s.endsWith('s', Qt::CaseInsensitive) || stage_ == 0) {
        int n = s.left(s.endsWith('s', Qt::CaseInsensitive) ? s.size() - 1 : s.size()).toInt();
        if (n >= 3 && n <= 999) { segs_ = n; setVCB(QString::number(n)); }
        return;
    }
    double R;
    if (stage_ == 1 && parseLength(s, doc()->model.units, R) && R > 0) {
        Vec3 d = r_ - c_;
        if (d.length() < 1e-9) d = plane_.n.anyPerp();
        finish(c_ + d.normalized() * R);
    }
}

void CircleTool::draw(Overlay& ov) {
    if (stage_ == 1) {
        auto pts = points(r_);
        if (pts.size() >= 3) {
            Overlay::Poly poly;
            poly.pts = pts;
            poly.c = QColor(120, 140, 255, 60);
            ov.polys.push_back(poly);
            for (size_t i = 0; i < pts.size(); ++i) ov.line(pts[i], pts[(i + 1) % pts.size()], QColor(0, 0, 0), 2.0f, 0, false);
        }
        ov.line(c_, r_, QColor(0, 0, 0), 1.0f, 2, false);
    } else if (lockAxis_ >= 0) {
        ov.tooltip = QString("Plane locked: ") + (lockAxis_ == 0 ? "Red" : lockAxis_ == 1 ? "Green" : "Blue");
    }
    drawInf(ov);
}

// ======================================================================= Arcs

static Vec3 rotateAround(const Vec3& v, const Vec3& n, double a) {
    return v * std::cos(a) + n.cross(v) * std::sin(a) + n * n.dot(v) * (1 - std::cos(a));
}
static double angleAbout(const Vec3& a, const Vec3& b, const Vec3& n) {
    double ang = std::atan2(n.dot(a.cross(b)), a.dot(b));
    return ang < 0 ? ang + 2 * kPi : ang;
}

QString ArcTool::vcbLabel() const {
    if (mode_ == TwoPoint) return stage_ == 2 ? "Bulge" : "Length";
    if (mode_ == Center || mode_ == Pie) return stage_ == 2 ? "Angle" : "Radius";
    return "Segments";
}

QString ArcTool::status() const {
    switch (mode_) {
    case TwoPoint: return stage_ == 0 ? "Select start point." : (stage_ == 1 ? "Select end point." : "Select bulge amount (or type a radius with r, segments with s).");
    case Center: case Pie: return stage_ == 0 ? "Select center point." : (stage_ == 1 ? "Select start of arc." : "Select end of arc.");
    case ThreePoint: return stage_ == 0 ? "Select start point." : (stage_ == 1 ? "Select a second point on the arc." : "Select end point.");
    }
    return {};
}

Vec3 ArcTool::planePoint(const InfResult& r) const {
    bool snapped = r.type == InfType::Endpoint || r.type == InfType::Midpoint || r.type == InfType::OnEdge || r.type == InfType::Center;
    if (snapped) return plane_.project(r.p);
    Ray ray = view->cam.ray(lastX, lastY);
    double t = ray.hit(plane_);
    return std::isnan(t) ? plane_.project(r.p) : ray.at(t);
}

bool ArcTool::arcPoints(const Vec3& p, std::vector<Vec3>& pts, Curve& cv, double* bulgeOut) const {
    pts.clear();
    Vec3 C, start;
    double sweep = 0;
    Vec3 n = plane_.n;
    if (mode_ == TwoPoint) {
        Vec3 chord = b_ - a_;
        double c = chord.length();
        if (c < kTol) return false;
        Vec3 mid = (a_ + b_) * 0.5;
        Vec3 perp = n.cross(chord).normalized();
        double s = (p - mid).dot(perp);
        if (halfCircle_) s = (s < 0 ? -1 : 1) * c / 2;
        if (bulgeOut) *bulgeOut = s;
        if (std::fabs(s) < kTol) {
            pts = {a_, b_};
            return true;
        }
        if (s < 0) { perp = -perp; s = -s; }
        double R = (c * c / 4 + s * s) / (2 * s);
        C = mid + perp * (s - R);
        Vec3 va = a_ - C, vb = b_ - C, vp = (mid + perp * s) - C;
        double tb = angleAbout(va, vb, n), tp = angleAbout(va, vp, n);
        sweep = tp < tb ? tb : tb - 2 * kPi;
        start = va;
    } else if (mode_ == Center || mode_ == Pie) {
        C = a_;
        start = b_ - C;
        Vec3 vp = p - C;
        if (vp.length() < kTol) return false;
        sweep = std::atan2(n.dot(start.cross(vp)), start.dot(vp));
        // continue in the direction of motion beyond 180°
        if (std::fabs(c_.x) > 0 || true) {
            double prev = c_.y;  // previous sweep stored in c_.y
            if (std::fabs(prev) > kPi / 2 && (sweep > 0) != (prev > 0)) sweep += sweep < 0 ? 2 * kPi : -2 * kPi;
        }
        // 15° snapping when close
        double deg = sweep * 180 / kPi;
        double snapped = std::round(deg / 15) * 15;
        if (std::fabs(deg - snapped) < 1.5) sweep = snapped * kPi / 180;
    } else {
        // three point: circle through a_, b_, p
        Vec3 A = a_, B = b_, Cc = p;
        Vec3 ab = B - A, ac = Cc - A;
        Vec3 nn = ab.cross(ac);
        if (nn.length() < 1e-6 * ab.length() * ac.length()) return false;
        Vec3 cc = A + (nn.cross(ab) * ac.length2() + ac.cross(nn) * ab.length2()) / (2 * nn.length2());
        C = cc;
        n = nn.normalized();
        start = A - C;
        double tb = angleAbout(start, B - C, n), tc = angleAbout(start, Cc - C, n);
        sweep = tb < tc ? tc : tc - 2 * kPi;
        if (sweep < 0) { n = -n; sweep = -sweep; }
    }
    double R = start.length();
    if (R < kTol || std::fabs(sweep) < 1e-6) return false;
    int segs = std::max(2, (int)std::ceil(segs_ * std::fabs(sweep) / kPi));
    if (mode_ == TwoPoint || mode_ == ThreePoint) segs = segs_;
    if (mode_ == Center || mode_ == Pie) segs = std::max(2, (int)std::lround(24 * std::fabs(sweep) / (2 * kPi)));
    for (int i = 0; i <= segs; ++i) pts.push_back(C + rotateAround(start, n, sweep * i / segs));
    cv.isArc = true;
    cv.closed = false;
    cv.center = C;
    cv.normal = n;
    cv.xaxis = start.normalized();
    cv.radius = R;
    cv.startAng = 0;
    cv.endAng = sweep;
    cv.segments = segs;
    return true;
}

void ArcTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    infer(e);
    if (stage_ == 0) {
        a_ = cur.p;
        plane_ = shapePlane(cur, -1);
        stage_ = 1;
        inf().setAnchor(a_);
        c_ = Vec3();
        return;
    }
    if (stage_ == 1) {
        Vec3 p = cur.p;
        if ((p - a_).length() < kTol) return;
        b_ = p;
        if (mode_ == TwoPoint) {
            // plane: keep the face plane if b lies in it, else vertical/horizontal through chord
            if (std::fabs(plane_.dist(b_)) > kTol) {
                Vec3 chord = (b_ - a_).normalized();
                Vec3 nn = chord.cross(Vec3(0, 0, 1));
                if (nn.length() < 1e-6) nn = chord.cross(Vec3(1, 0, 0));
                plane_ = Plane(chord.cross(nn).normalized(), a_);
            }
        } else if (mode_ == Center || mode_ == Pie) {
            b_ = plane_.project(b_);
        }
        stage_ = 2;
        inf().setAnchor(b_);
        return;
    }
    finish(mode_ == ThreePoint ? cur.p : planePoint(cur));
}

void ArcTool::mouseMove(const MouseEv& e) {
    infer(e);
    halfCircle_ = false;
    const UnitSettings& U = doc()->model.units;
    if (stage_ == 1) setVCB(formatLength((cur.p - a_).length(), U));
    if (stage_ == 2 && mode_ == TwoPoint) {
        Vec3 p = planePoint(cur);
        Vec3 chord = b_ - a_;
        Vec3 perp = plane_.n.cross(chord).normalized();
        double s = (p - (a_ + b_) * 0.5).dot(perp);
        if (std::fabs(std::fabs(s) - chord.length() / 2) < pixelTol(p, 8)) halfCircle_ = true;
        setVCB(formatLength(halfCircle_ ? chord.length() / 2 : std::fabs(s), U));
    }
    if (stage_ == 2 && (mode_ == Center || mode_ == Pie)) {
        Vec3 p = planePoint(cur);
        Vec3 start = b_ - a_, vp = p - a_;
        double sweep = std::atan2(plane_.n.dot(start.cross(vp)), start.dot(vp));
        double prev = c_.y;
        if (std::fabs(prev) > kPi / 2 && (sweep > 0) != (prev > 0)) sweep += sweep < 0 ? 2 * kPi : -2 * kPi;
        c_.y = sweep;
        setVCB(formatAngle(std::fabs(sweep), U));
    }
}

void ArcTool::finish(const Vec3& p) {
    std::vector<Vec3> pts;
    Curve cv;
    if (!arcPoints(p, pts, cv)) { reset(); return; }
    Mat4 inv = doc()->activeInv();
    std::vector<Vec3> local;
    for (auto& q : pts) local.push_back(inv.point(q));
    cv.center = inv.point(cv.center);
    cv.normal = inv.vector(cv.normal).normalized();
    cv.xaxis = inv.vector(cv.xaxis).normalized();
    doc()->begin(title());
    if (mode_ == Pie) {
        std::vector<Vec3> loop = local;
        // radii closing the pie
        geom::addPolyline(doc()->active(), local, false, &cv);
        geom::addLine(doc()->active(), local.back(), cv.center);
        geom::addLine(doc()->active(), cv.center, local.front());
    } else if (pts.size() == 2) {
        geom::addLine(doc()->active(), local[0], local[1]);
    } else {
        geom::addPolyline(doc()->active(), local, false, &cv);
    }
    doc()->commit();
    reset();
}

void ArcTool::vcbEnter(const QString& sIn) {
    QString s = sIn.trimmed();
    if (s.endsWith('s', Qt::CaseInsensitive)) {
        int n = s.left(s.size() - 1).toInt();
        if (n >= 2 && n < 1000) segs_ = n;
        return;
    }
    const UnitSettings& U = doc()->model.units;
    if (stage_ == 1) {
        double L;
        if (!parseLength(s, U, L)) return;
        Vec3 d = cur.p - a_;
        if (d.length() < 1e-9) return;
        b_ = a_ + d.normalized() * L;
        stage_ = 2;
        inf().setAnchor(b_);
        return;
    }
    if (stage_ != 2) return;
    if (mode_ == TwoPoint) {
        Vec3 chord = b_ - a_;
        Vec3 perp = plane_.n.cross(chord).normalized();
        Vec3 mid = (a_ + b_) * 0.5;
        double sign = (planePoint(cur) - mid).dot(perp) < 0 ? -1 : 1;
        double v;
        if (s.endsWith('r', Qt::CaseInsensitive)) {
            if (!parseLength(s.left(s.size() - 1), U, v)) return;
            double c = chord.length();
            if (v < c / 2) return;
            double sag = v - std::sqrt(v * v - c * c / 4);
            finish(mid + perp * sign * sag);
            return;
        }
        if (!parseLength(s, U, v)) return;
        finish(mid + perp * sign * v);
    } else if (mode_ == Center || mode_ == Pie) {
        double deg = s.toDouble();
        if (deg == 0) return;
        double sign = c_.y < 0 ? -1 : 1;
        Vec3 start = b_ - a_;
        Vec3 p = a_ + rotateAround(start, plane_.n, sign * deg * kPi / 180);
        c_.y = sign * deg * kPi / 180;
        finish(p);
    }
}

void ArcTool::draw(Overlay& ov) {
    if (stage_ == 1) ov.line(a_, cur.p, QColor(0, 0, 0), 1.5f, mode_ == TwoPoint ? 0 : 2, false);
    if (stage_ == 2) {
        std::vector<Vec3> pts;
        Curve cv;
        Vec3 p = mode_ == ThreePoint ? cur.p : planePoint(cur);
        if (arcPoints(p, pts, cv)) {
            for (size_t i = 1; i < pts.size(); ++i) ov.line(pts[i - 1], pts[i], QColor(0, 0, 0), 2.0f, 0, false);
            if (mode_ == Pie || mode_ == Center) {
                ov.line(a_, pts.front(), QColor(0, 0, 0), mode_ == Pie ? 2.0f : 1.0f, mode_ == Pie ? 0 : 2, false);
                ov.line(a_, pts.back(), QColor(0, 0, 0), mode_ == Pie ? 2.0f : 1.0f, mode_ == Pie ? 0 : 2, false);
            }
        }
        if (halfCircle_) { ov.tooltip = "Half Circle"; ov.tipX = lastX; ov.tipY = lastY; return; }
    }
    drawInf(ov);
}

}  // namespace gable
