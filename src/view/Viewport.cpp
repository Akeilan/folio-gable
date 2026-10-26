#include "view/Viewport.h"
#include "tools/Tool.h"
#include <QApplication>
#include <QMouseEvent>
#include <QOpenGLFramebufferObject>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <functional>

namespace gable {

Viewport::Viewport(Document* d, QWidget* parent)
    : QOpenGLWidget(parent), doc(d), picker(d, &cam), inference(d, &cam, &picker) {
    cam.s = &doc->model.camera;
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(200, 150);
    connect(&animTimer_, &QTimer::timeout, this, [this] {
        double t = std::min(1.0, animClock_.elapsed() / (double)std::max(animMs_, 1));
        double k = t * t * (3 - 2 * t);
        CameraState& c = *cam.s;
        c.eye = animFrom_.eye + (animTo_.eye - animFrom_.eye) * k;
        c.target = animFrom_.target + (animTo_.target - animFrom_.target) * k;
        c.fov = animFrom_.fov + (animTo_.fov - animFrom_.fov) * k;
        c.orthoHeight = animFrom_.orthoHeight + (animTo_.orthoHeight - animFrom_.orthoHeight) * k;
        c.up = (animFrom_.up + (animTo_.up - animFrom_.up) * k).normalized();
        if (t >= 1) {
            c = animTo_;
            animTimer_.stop();
        }
        emit cameraChanged();
        update();
    });
    connect(doc, &Document::changed, this, [this] { update(); indexTimer_.start(); });
    // once the model has been still for a moment, build the pick index in the background
    indexTimer_.setSingleShot(true);
    indexTimer_.setInterval(250);
    connect(&indexTimer_, &QTimer::timeout, this, [this] { picker.prepare(); });
    connect(doc, &Document::selectionChanged, this, [this] { update(); });
    connect(doc, &Document::viewChanged, this, [this] { update(); });
    connect(doc, &Document::contextChanged, this, [this] { update(); });
}

Viewport::~Viewport() {
    makeCurrent();
    renderer_.cleanup();
    doneCurrent();
}

void Viewport::registerTool(Tool* t) { tools_[t->id()] = t; }
Tool* Viewport::toolById(const QString& id) const {
    auto it = tools_.find(id);
    return it == tools_.end() ? nullptr : it->second;
}

void Viewport::setTool(const QString& id) {
    Tool* t = toolById(id);
    if (!t) return;
    if (tool_ == t) {
        tool_->reset();
        refreshToolUI();
        return;
    }
    if (tool_) {
        tool_->deactivate();
        prevTool_ = tool_->id();
    }
    if (doc->inOperation()) doc->commit();
    inference.reset();
    picker.exVerts.clear();
    picker.exInsts.clear();
    overlay.clear();
    hover.clear();
    tool_ = t;
    tool_->activate();
    setCursor(tool_->cursor());
    refreshToolUI();
    emit toolChanged(id);
    update();
}

void Viewport::returnToPreviousTool() {
    if (!prevTool_.isEmpty()) setTool(prevTool_);
}

void Viewport::refreshToolUI() {
    if (!tool_) return;
    emit vcbChanged(tool_->vcbLabel(), QString());
    emit statusChanged(tool_->status());
    setCursor(tool_->cursor());
}

void Viewport::setVCB(const QString& v) { emit vcbChanged(tool_ ? tool_->vcbLabel() : QString("Measurements"), v); }
void Viewport::setStatus(const QString& s) { emit statusChanged(s); }

// ------------------------------------------------------------------ GL

void Viewport::initializeGL() {
    glOk_ = renderer_.init(&glError_);
    if (!glOk_) qWarning("Renderer init failed: %s", qPrintable(glError_));
}

void Viewport::resizeGL(int w, int h) {
    cam.w = w;
    cam.h = h;
}

void Viewport::paintGL() {
    cam.w = width();
    cam.h = height();
    if (!glOk_) {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        p.drawText(rect(), Qt::AlignCenter, "OpenGL 3.3 is required.\n" + glError_);
        return;
    }
    overlay.clear();
    if (tool_) tool_->draw(overlay);
    Renderer::Params P;
    P.doc = doc;
    P.cam = &cam;
    P.dpr = devicePixelRatioF();
    P.w = (int)std::lround(width() * P.dpr);
    P.h = (int)std::lround(height() * P.dpr);
    P.overlay = &overlay;
    P.hover = hover;
    P.defaultFbo = defaultFramebufferObject();
    renderer_.render(P);
    paintOverlay2D();
}

QImage Viewport::renderImage(int w, int h, bool transparent) {
    makeCurrent();
    QOpenGLFramebufferObjectFormat fmt;
    fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    fmt.setSamples(8);
    QOpenGLFramebufferObject fbo(w, h, fmt);
    fbo.bind();
    int oldW = cam.w, oldH = cam.h;
    // keep the same vertical extent; adjust for aspect automatically
    cam.w = w;
    cam.h = h;
    Renderer::Params P;
    P.doc = doc;
    P.cam = &cam;
    P.dpr = std::max(1.0, h / 900.0);
    P.w = w;
    P.h = h;
    P.overlay = nullptr;
    P.transparentBackground = transparent;
    P.forExport = true;
    P.defaultFbo = fbo.handle();
    renderer_.render(P);
    fbo.release();
    QImage img = fbo.toImage(true);
    cam.w = oldW;
    cam.h = oldH;
    doneCurrent();
    return img;
}

// ------------------------------------------------------------------ 2D overlay

void Viewport::paintOverlay2D() {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (orbitPivotShown_) {
        // the point the view turns about
        double sx, sy, d;
        if (cam.project(orbitPivotPt_, sx, sy, &d) && (!cam.s->perspective || d > 0)) {
            p.setPen(QPen(QColor(255, 255, 255, 220), 3));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(sx, sy), 6, 6);
            p.setPen(QPen(QColor(107, 63, 160), 1.6));
            p.drawEllipse(QPointF(sx, sy), 6, 6);
            p.drawLine(QPointF(sx - 10, sy), QPointF(sx - 3, sy));
            p.drawLine(QPointF(sx + 3, sy), QPointF(sx + 10, sy));
            p.drawLine(QPointF(sx, sy - 10), QPointF(sx, sy - 3));
            p.drawLine(QPointF(sx, sy + 3), QPointF(sx, sy + 10));
        }
    }
    QFont f = font();
    f.setPointSizeF(9.5);
    p.setFont(f);
    QFontMetrics fm(f);
    auto proj = [&](const Vec3& w, QPointF& out) {
        double sx, sy, d;
        if (!cam.project(w, sx, sy, &d)) return false;
        if (cam.s->perspective && d <= 0) return false;
        out = QPointF(sx, sy);
        return true;
    };
    auto drawLabelBox = [&](QPointF at, const QString& text, const QColor& col, bool box, QColor bg = QColor(255, 255, 255, 235)) {
        QRect r = fm.boundingRect(text);
        r.adjust(-5, -3, 5, 3);
        r.moveCenter(at.toPoint());
        if (box) {
            p.setPen(QColor(120, 120, 120));
            p.setBrush(bg);
            p.drawRect(r);
        }
        p.setPen(col);
        p.drawText(r, Qt::AlignCenter, text);
    };
    // Dimensions & text entities across the hierarchy
    const UnitSettings& U = doc->model.units;
    std::function<void(int, const Mat4&, int)> walk = [&](int def, const Mat4& xf, int depth) {
        if (depth > 32) return;
        const Entities& E = def < 0 ? doc->model.root : doc->model.defs[def].ents;
        for (int i = 0; i < (int)E.dims.size(); ++i) {
            const Dimension& d = E.dims[i];
            if (!d.alive || d.hidden || !doc->tagVisible(d.tag)) continue;
            Vec3 a = xf.point(d.a), b = xf.point(d.b);
            QString txt;
            QPointF at;
            if (d.radial) {
                double r = (b - a).length();
                txt = (d.diameter ? "DIA " : "R ") + formatLength(d.diameter ? r * 2 : r, U);
                if (!proj((a + b) * 0.5, at)) continue;
            } else {
                Vec3 off = xf.vector(d.offset);
                txt = formatLength((b - a).length(), U);
                if (!proj((a + b) * 0.5 + off, at)) continue;
            }
            if (!d.textOverride.isEmpty()) txt = QString(d.textOverride).replace("<>", txt);
            bool sel = def == doc->activeDef() && doc->isSelected({EntKind::Dimension, i}) && (def < 0 ? doc->editPath.empty() : true);
            drawLabelBox(at, txt, sel ? icolor::selection() : QColor(0, 0, 0), true);
        }
        for (int i = 0; i < (int)E.texts.size(); ++i) {
            const Text& t = E.texts[i];
            if (!t.alive || t.hidden || !doc->tagVisible(t.tag)) continue;
            bool sel = def == doc->activeDef() && doc->isSelected({EntKind::Text, i});
            QColor tc = sel ? icolor::selection() : QColor(0, 0, 0);
            QStringList lines = t.text.split('\n');
            int tw = 0;
            for (auto& l : lines) tw = std::max(tw, fm.horizontalAdvance(l));
            int th = fm.height() * lines.size();
            QPointF at;
            if (t.screenText) at = QPointF(t.sx * width(), t.sy * height());
            else {
                QPointF a0;
                if (!proj(xf.point(t.anchor), a0)) continue;
                if (!proj(xf.point(t.anchor + t.leader), at)) continue;
                p.setPen(QPen(tc, 1));
                p.drawLine(a0, at);
                p.setBrush(tc);
                p.drawEllipse(a0, 2.5, 2.5);
            }
            QRectF r(at.x(), at.y() - th / 2.0, tw + 10, th + 4);
            if (!t.screenText) {
                if (at.x() < width() / 2.0) r.moveLeft(at.x());
                else r.moveRight(at.x());
            }
            p.setPen(QColor(140, 140, 140));
            p.setBrush(QColor(255, 255, 255, t.screenText ? 0 : 235));
            if (!t.screenText) p.drawRect(r);
            p.setPen(tc);
            p.drawText(r.adjusted(5, 2, -5, -2), Qt::AlignLeft | Qt::AlignVCenter, t.text);
        }
        for (auto& in : E.insts)
            if (in.alive && in.def >= 0 && !in.hidden && doc->tagVisible(in.tag)) walk(in.def, xf * in.xf, depth + 1);
    };
    walk(-1, Mat4(), 0);

    // Tool overlay labels & markers
    for (auto& m : overlay.markers) {
        QPointF s;
        if (!proj(m.p, s)) continue;
        double r = m.size / 2.0;
        QPen pen(m.c, 2);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        switch (m.shape) {
        case Overlay::Circle:
            p.setBrush(QColor(m.c.red(), m.c.green(), m.c.blue(), 70));
            p.drawEllipse(s, r, r);
            p.setPen(QPen(QColor(0, 0, 0, 120), 1));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(s, r + 1, r + 1);
            break;
        case Overlay::FilledCircle: p.setBrush(m.c); p.drawEllipse(s, r, r); break;
        case Overlay::Square:
            p.setBrush(QColor(m.c.red(), m.c.green(), m.c.blue(), 70));
            p.drawRect(QRectF(s.x() - r, s.y() - r, 2 * r, 2 * r));
            break;
        case Overlay::Diamond: {
            p.setBrush(QColor(m.c.red(), m.c.green(), m.c.blue(), 70));
            QPolygonF poly;
            poly << QPointF(s.x(), s.y() - r) << QPointF(s.x() + r, s.y()) << QPointF(s.x(), s.y() + r) << QPointF(s.x() - r, s.y());
            p.drawPolygon(poly);
            break;
        }
        case Overlay::Cross:
            p.drawLine(QPointF(s.x() - r, s.y() - r), QPointF(s.x() + r, s.y() + r));
            p.drawLine(QPointF(s.x() - r, s.y() + r), QPointF(s.x() + r, s.y() - r));
            break;
        case Overlay::Triangle: {
            QPolygonF poly;
            poly << QPointF(s.x(), s.y() - r) << QPointF(s.x() + r, s.y() + r) << QPointF(s.x() - r, s.y() + r);
            p.drawPolygon(poly);
            break;
        }
        }
    }
    for (auto& l : overlay.labels) {
        QPointF s;
        if (!proj(l.p, s)) continue;
        drawLabelBox(s + QPointF(l.dx, l.dy), l.text, l.c, l.box);
    }
    for (auto& b : overlay.boxes) {
        QPen pen(b.c, 1);
        if (b.dashed) pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.setBrush(QColor(b.c.red(), b.c.green(), b.c.blue(), 25));
        p.drawRect(QRectF(QPointF(b.x0, b.y0), QPointF(b.x1, b.y1)).normalized());
    }
    if (!overlay.tooltip.isEmpty()) {
        QRect r = fm.boundingRect(overlay.tooltip);
        r.adjust(-5, -3, 5, 3);
        r.moveTopLeft(QPoint((int)overlay.tipX + 14, (int)overlay.tipY + 18));
        if (r.right() > width()) r.moveRight((int)overlay.tipX - 4);
        if (r.bottom() > height()) r.moveBottom((int)overlay.tipY - 4);
        p.setPen(QColor(90, 90, 90));
        p.setBrush(QColor(255, 255, 225));
        p.drawRect(r);
        p.setPen(Qt::black);
        p.drawText(r, Qt::AlignCenter, overlay.tooltip);
    }
    // Context breadcrumb
    if (!doc->editPath.empty()) {
        QString crumb = "Model";
        const Entities* E = &doc->model.root;
        for (int i : doc->editPath) {
            if (i < 0 || i >= (int)E->insts.size()) break;
            const Instance& in = E->insts[i];
            const ComponentDef& d = doc->model.defs[in.def];
            crumb += "  ›  " + (in.name.isEmpty() ? d.name : in.name);
            E = &d.ents;
        }
        QRect r = fm.boundingRect(crumb).adjusted(-8, -4, 8, 4);
        r.moveTopLeft(QPoint(10, 10));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(40, 40, 40, 200));
        p.drawRoundedRect(r, 4, 4);
        p.setPen(Qt::white);
        p.drawText(r, Qt::AlignCenter, crumb);
    }
    // Camera mode badge
    if (!cam.s->perspective) {
        p.setPen(QColor(110, 110, 110));
        p.drawText(QRect(0, height() - 22, width() - 10, 20), Qt::AlignRight | Qt::AlignVCenter, "Parallel Projection");
    }
}

// ------------------------------------------------------------------ input

static MouseEv toEv(QMouseEvent* e) {
    MouseEv m;
    m.x = e->position().x();
    m.y = e->position().y();
    m.button = e->button();
    m.buttons = e->buttons();
    m.mods = e->modifiers();
    return m;
}

Vec3 Viewport::orbitPivot(double x, double y) {
    // 1) whatever is under the cursor
    Picker::DeepResult r = picker.deep(x, y, 0, 4);
    if (r.face.valid()) return r.face.point;
    if (r.edge.valid()) return r.edge.point;
    return emptyPivot();
}

Vec3 Viewport::emptyPivot() {
    const Vec3 eye = cam.s->eye, f = cam.forward();
    auto inFront = [&](const Vec3& p) { return (p - eye).dot(f) > cam.nearZ * 2; };
    // 2) the selection, when it is in view
    BBox sb = doc->selectionBBox();
    if (sb.valid()) {
        double sx, sy;
        Vec3 c = sb.center();
        if (inFront(c) && cam.project(c, sx, sy) && sx >= 0 && sy >= 0 && sx <= width() && sy <= height()) return c;
    }
    // 3) what is in the middle of the screen
    Picker::DeepResult m = picker.deep(width() * 0.5, height() * 0.5, 0, 6);
    if (m.face.valid()) return m.face.point;
    if (m.edge.valid()) return m.edge.point;
    // 4) on the line of sight, at the depth of the model, so the view turns about the middle of
    //    the screen instead of swinging around a point off to the side
    BBox b = doc->model.worldBBox();
    if (b.valid()) {
        double dmin = 1e300, dmax = -1e300;
        for (int i = 0; i < 8; ++i) {
            Vec3 c((i & 1) ? b.hi.x : b.lo.x, (i & 2) ? b.hi.y : b.lo.y, (i & 4) ? b.hi.z : b.lo.z);
            double d = (c - eye).dot(f);
            dmin = std::min(dmin, d);
            dmax = std::max(dmax, d);
        }
        if (dmax > cam.nearZ * 2) {
            double d = (b.center() - eye).dot(f);
            d = std::clamp(d, std::max(dmin, cam.nearZ * 20), dmax);
            return eye + f * d;
        }
    }
    // 5) nothing to look at: the camera's own target, at a sane distance
    double d = std::clamp(cam.distance(), 500.0, 1e6);
    return eye + f * d;
}

void Viewport::beginOrbit(const Vec3& pivot) {
    // keep the camera target on the line of sight at the pivot's depth, so later zooms and
    // empty-space orbits use a meaningful distance
    Vec3 f = cam.forward();
    double d = (pivot - cam.s->eye).dot(f);
    if (d > cam.nearZ * 2) cam.s->target = cam.s->eye + f * d;
    orbitPivotShown_ = true;
    orbitPivotPt_ = pivot;
    update();
}

void Viewport::endOrbit() {
    orbitPivotShown_ = false;
    update();
}

void Viewport::mousePressEvent(QMouseEvent* e) {
    setFocus();
    animTimer_.stop();
    lastX_ = e->position().x();
    lastY_ = e->position().y();
    if (e->button() == Qt::MiddleButton) {
        if (middleClickTimer_.isValid() && middleClickTimer_.elapsed() < 350 &&
            std::hypot(lastX_ - middlePressX_, lastY_ - middlePressY_) < 5) {
            // double middle click: center view on point
            Vec3 p = orbitPivot(lastX_, lastY_);
            CameraState to = *cam.s;
            Vec3 off = p - to.target;
            Vec3 f = cam.forward();
            Vec3 lateral = off - f * off.dot(f);
            to.eye += lateral;
            to.target += lateral;
            animateTo(to, 300);
            middleClickTimer_.invalidate();
            return;
        }
        middleClickTimer_.start();
        middlePressX_ = lastX_;
        middlePressY_ = lastY_;
        nav_ = (e->modifiers() & Qt::ShiftModifier) ? Nav::Pan : Nav::Orbit;
        navPivot_ = orbitPivot(lastX_, lastY_);
        if (nav_ == Nav::Orbit) beginOrbit(navPivot_);
        if (nav_ == Nav::Orbit) setCursor(Qt::ClosedHandCursor);
        else setCursor(Qt::SizeAllCursor);
        pushViewHistory();
        return;
    }
    if (tool_) {
        MouseEv m = toEv(e);
        mouseX = m.x; mouseY = m.y;
        if (e->button() == Qt::RightButton && !tool_->busy()) {
            tool_->mousePress(m);
            emit contextMenuRequested(e->globalPosition().toPoint());
            return;
        }
        tool_->mousePress(m);
        if (tool_) emit statusChanged(tool_->status());
        update();
    }
}

void Viewport::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) {
        nav_ = Nav::None;
        endOrbit();
        if (tool_) setCursor(tool_->cursor());
        emit cameraChanged();
        return;
    }
    if (tool_) {
        tool_->mouseRelease(toEv(e));
        if (tool_) emit statusChanged(tool_->status());
        update();
    }
}

void Viewport::mouseMoveEvent(QMouseEvent* e) {
    double x = e->position().x(), y = e->position().y();
    double dx = x - lastX_, dy = y - lastY_;
    lastX_ = x;
    lastY_ = y;
    if (nav_ == Nav::Orbit) {
        if (e->modifiers() & Qt::ShiftModifier) cam.pan(dx, dy, navPivot_);
        else if (e->modifiers() & Qt::ControlModifier) cam.orbitFree(dx, dy, navPivot_);
        else cam.orbit(dx, dy, navPivot_);
        update();
        return;
    }
    if (nav_ == Nav::Pan) {
        cam.pan(dx, dy, navPivot_);
        update();
        return;
    }
    mouseX = x;
    mouseY = y;
    if (tool_) {
        tool_->mouseMove(toEv(e));
        update();
    }
}

void Viewport::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) return;
    if (tool_) {
        tool_->doubleClick(toEv(e));
        update();
    }
}

void Viewport::wheelEvent(QWheelEvent* e) {
    animTimer_.stop();
    double steps = e->angleDelta().y() / 120.0;
    if (steps == 0) return;
    double factor = std::pow(1.25, steps);
    double x = e->position().x(), y = e->position().y();
    Picker::DeepResult r = picker.deep(x, y, 0, 3);
    bool have = r.face.valid() || r.edge.valid();
    Vec3 p = r.face.valid() ? r.face.point : (r.edge.valid() ? r.edge.point : Vec3());
    if (!have) {
        // over empty space: zoom toward the cursor at the depth the view is turning about
        Vec3 ep = emptyPivot();
        double d = (ep - cam.s->eye).dot(cam.forward());
        if (d > cam.nearZ * 2) {
            Ray ray = cam.ray(x, y);
            p = ray.at(d / std::max(ray.d.dot(cam.forward()), 1e-3));
            have = true;
        }
    }
    cam.zoomAt(factor, x, y, p, have);
    emit cameraChanged();
    if (tool_) {
        MouseEv m;
        m.x = x; m.y = y; m.mods = e->modifiers();
        tool_->mouseMove(m);
    }
    update();
}

void Viewport::keyPressEvent(QKeyEvent* e) {
    if (tool_ && tool_->keyPress(e)) { update(); return; }
    if (e->key() == Qt::Key_Escape) {
        if (tool_) { tool_->reset(); emit statusChanged(tool_->status()); setVCB(QString()); }
        if (doc->inOperation()) doc->cancel();
        update();
        return;
    }
    QOpenGLWidget::keyPressEvent(e);
}

void Viewport::keyReleaseEvent(QKeyEvent* e) {
    if (tool_ && tool_->keyRelease(e)) { update(); return; }
    QOpenGLWidget::keyReleaseEvent(e);
}

void Viewport::leaveEvent(QEvent*) {
    hover.clear();
    update();
}
void Viewport::enterEvent(QEnterEvent*) {}

// ------------------------------------------------------------------ camera helpers

void Viewport::pushViewHistory() {
    viewHistory_.push_back(*cam.s);
    if (viewHistory_.size() > 50) viewHistory_.erase(viewHistory_.begin());
}

void Viewport::previousView() {
    if (viewHistory_.empty()) return;
    CameraState s = viewHistory_.back();
    viewHistory_.pop_back();
    animateTo(s, 300);
}

void Viewport::zoomExtents() {
    pushViewHistory();
    cam.w = width(); cam.h = height();
    cam.zoomExtents(doc->model.worldBBox());
    emit cameraChanged();
    update();
}

void Viewport::zoomSelection() {
    BBox b = doc->selectionBBox();
    if (!b.valid()) { zoomExtents(); return; }
    pushViewHistory();
    cam.w = width(); cam.h = height();
    cam.zoomExtents(b);
    emit cameraChanged();
    update();
}

void Viewport::standardView(int which) {
    pushViewHistory();
    CameraState to = *cam.s;
    CameraState save = *cam.s;
    Vec3 dir, up(0, 0, 1);
    switch (which) {
    case 0: dir = Vec3(0, 0, -1); up = Vec3(0, 1, 0); break;
    case 1: dir = Vec3(0, 0, 1); up = Vec3(0, -1, 0); break;
    case 2: dir = Vec3(0, 1, 0); break;
    case 3: dir = Vec3(0, -1, 0); break;
    case 4: dir = Vec3(1, 0, 0); break;
    case 5: dir = Vec3(-1, 0, 0); break;
    default: dir = Vec3(0.62, 0.78, -0.55); break;
    }
    cam.w = width(); cam.h = height();
    cam.setView(dir, up, doc->model.worldBBox());
    to = *cam.s;
    *cam.s = save;
    animateTo(to, 450);
}

void Viewport::setPerspective(bool persp) {
    CameraState& c = *cam.s;
    if (c.perspective == persp) return;
    double d = cam.distance();
    if (!persp) {
        c.orthoHeight = 2 * d * std::tan(c.fov * kPi / 360);
    } else {
        double nd = c.orthoHeight / (2 * std::tan(c.fov * kPi / 360));
        Vec3 f = cam.forward();
        c.eye = c.target - f * nd;
    }
    c.perspective = persp;
    emit cameraChanged();
    update();
}

void Viewport::animateTo(const CameraState& target, int ms) {
    animFrom_ = *cam.s;
    animTo_ = target;
    animFrom_.perspective = animTo_.perspective;
    cam.s->perspective = target.perspective;
    animMs_ = ms;
    animClock_.start();
    animTimer_.start(16);
}

}  // namespace gable
