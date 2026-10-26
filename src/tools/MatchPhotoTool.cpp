// Folio Gable — Match Photo.
#include "tools/Tools.h"
#include "view/Viewport.h"
#include <QImage>
#include <QKeyEvent>
#include <QMenu>
#include <cmath>

namespace gable {

void MatchPhotoTool::start(int scene, const QByteArray& newImage, const QString& newName) {
    scene_ = scene;
    newImage_ = newImage;
    newName_ = newName;
}

MatchPhoto& MatchPhotoTool::photo() { return doc()->model.scenes[scene_].photo; }

void MatchPhotoTool::activate() {
    active_ = false;
    drag_ = hot_ = -1;
    problem_.clear();
    Model& m = doc()->model;
    if (scene_ < 0 && newImage_.isEmpty()) return;
    if (scene_ >= (int)m.scenes.size() || (scene_ >= 0 && !m.scenes[scene_].matchPhoto)) { scene_ = -1; return; }
    startCam_ = m.camera;
    doc()->begin(scene_ < 0 ? "New Match Photo" : "Edit Matched Photo");
    if (scene_ < 0) {
        Scene sc;
        sc.name = newName_.isEmpty() ? "Match Photo" : newName_;
        sc.matchPhoto = true;
        sc.photo.image = newImage_;
        sc.cam = m.camera;
        sc.tagVisible.clear();
        for (auto& t : m.tags) sc.tagVisible.push_back(t.visible);
        sc.style = m.style;
        sc.shadow = m.shadow;
        m.scenes.push_back(sc);
        scene_ = (int)m.scenes.size() - 1;
        newImage_.clear();
    }
    QImage img = QImage::fromData(photo().image);
    aspect_ = img.isNull() ? 1.5 : (double)img.width() / std::max(1, img.height());
    view->stopAnimation();
    m.camera.perspective = true;
    m.camera.twoPoint = false;
    m.shownPhoto = scene_;
    active_ = true;
    doc()->clearSelection();
    view->hover.clear();
    solve();
    setVCB(formatLength(photo().eyeHeight, m.units));
    setStatus(status());
}

QString MatchPhotoTool::status() const {
    if (!problem_.isEmpty()) return problem_;
    return "Drag the red and green lines onto edges in the photo that run along the red and green axes, and the yellow grip to "
           "where the axes meet. Type the eye height. Enter or right-click › Done when finished; Esc cancels.";
}

// the photo fills the view's height, centred
QPointF MatchPhotoTool::toScreen(const Vec2& q) const {
    double H = view->height(), W = view->width();
    double pw = H * aspect_;
    return QPointF((W - pw) / 2 + q.x * pw, q.y * H);
}

Vec2 MatchPhotoTool::toPhoto(double x, double y) const {
    double H = std::max(1, view->height()), W = view->width();
    double pw = H * aspect_;
    return Vec2((x - (W - pw) / 2) / pw, y / H);
}

QPointF MatchPhotoTool::gripScreen(int k) const {
    const MatchPhoto& p = doc()->model.scenes[scene_].photo;
    return toScreen(k == 8 ? p.origin : p.lines[k / 2][k % 2]);
}

int MatchPhotoTool::gripAt(double x, double y) const {
    if (!active_) return -1;
    int best = -1;
    double bd = 10;
    for (int k = 0; k < 9; ++k) {
        QPointF s = gripScreen(k);
        double d = std::hypot(s.x() - x, s.y() - y);
        if (d < bd) { bd = d; best = k; }
    }
    return best;
}

void MatchPhotoTool::solve() {
    if (!active_) return;
    Model& m = doc()->model;
    CameraState c = m.camera;
    QString why;
    if (solveMatchPhoto(photo(), aspect_, c, &why)) {
        problem_.clear();
        m.camera = c;
        m.scenes[scene_].cam = c;
        m.shownPhoto = scene_;
        emit view->cameraChanged();
    } else {
        problem_ = why;
    }
    setStatus(status());
    redraw();
}

void MatchPhotoTool::mousePress(const MouseEv& e) {
    if (!active_) return;
    if (e.button == Qt::RightButton) {
        QMenu menu;
        menu.addAction("Done", [this] { finish(true); toSelectTool(); });
        menu.addAction("Turn Axes 180°", [this] { turnAxes(); });
        menu.addAction("Cancel", [this] { finish(false); toSelectTool(); });
        menu.exec(QCursor::pos());
        return;
    }
    if (e.button != Qt::LeftButton) return;
    drag_ = gripAt(e.x, e.y);
}

void MatchPhotoTool::mouseMove(const MouseEv& e) {
    if (!active_) return;
    if (drag_ < 0) {
        int h = gripAt(e.x, e.y);
        if (h != hot_) { hot_ = h; redraw(); }
        return;
    }
    Vec2 q = toPhoto(e.x, e.y);
    q.x = std::clamp(q.x, -0.5, 1.5);
    q.y = std::clamp(q.y, -0.5, 1.5);
    if (drag_ == 8) photo().origin = q;
    else photo().lines[drag_ / 2][drag_ % 2] = q;
    solve();
}

void MatchPhotoTool::mouseRelease(const MouseEv&) { drag_ = -1; }

bool MatchPhotoTool::keyPress(QKeyEvent* e) {
    if (!active_) return false;
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        finish(true);
        toSelectTool();
        return true;
    }
    return false;
}

void MatchPhotoTool::vcbEnter(const QString& s) {
    if (!active_) return;
    double v;
    if (!parseLength(s, doc()->model.units, v) || v <= 0) return;
    photo().eyeHeight = v;
    solve();
    setVCB(formatLength(v, doc()->model.units));
}

void MatchPhotoTool::turnAxes() {
    if (!active_) return;
    photo().turned = !photo().turned;
    solve();
}

void MatchPhotoTool::finish(bool keep) {
    if (!active_) return;
    active_ = false;
    drag_ = hot_ = -1;
    if (keep) {
        Model& m = doc()->model;
        m.scenes[scene_].cam = m.camera;
        m.shownPhoto = scene_;
        doc()->commit();
    } else {
        doc()->cancel();
        doc()->model.camera = startCam_;
        emit view->cameraChanged();
    }
    setVCB(QString());
    redraw();
}

void MatchPhotoTool::draw(Overlay& ov) {
    if (!active_) return;
    const MatchPhoto& p = photo();
    Vec2 vr, vg;
    bool ok = matchPhotoVanishing(p, aspect_, vr, vg);
    const QColor cols[2] = {QColor(225, 30, 30), QColor(20, 160, 30)};
    for (int l = 0; l < 4; ++l) {
        QColor c = cols[l / 2];
        QPointF a = toScreen(p.lines[l][0]), b = toScreen(p.lines[l][1]);
        ov.lines2d.push_back({a.x(), a.y(), b.x(), b.y(), c, 2, false});
        if (ok) {
            // dashed on to the vanishing point, from the end nearer to it
            QPointF v = toScreen(l < 2 ? vr : vg);
            QPointF n = std::hypot(a.x() - v.x(), a.y() - v.y()) < std::hypot(b.x() - v.x(), b.y() - v.y()) ? a : b;
            QColor cd = c;
            cd.setAlpha(150);
            ov.lines2d.push_back({n.x(), n.y(), v.x(), v.y(), cd, 1, true});
        }
        for (int k = 0; k < 2; ++k) {
            QPointF g = toScreen(p.lines[l][k]);
            ov.grips2d.push_back({g.x(), g.y(), c, 9, hot_ == l * 2 + k || drag_ == l * 2 + k});
        }
    }
    if (ok) {
        // the horizon through both vanishing points, across the view
        QPointF a = toScreen(vr), b = toScreen(vg);
        double dx = b.x() - a.x(), dy = b.y() - a.y(), L = std::hypot(dx, dy);
        if (L > 1e-6) {
            dx /= L; dy /= L;
            double big = 4 * (view->width() + view->height());
            ov.lines2d.push_back({a.x() - dx * big, a.y() - dy * big, a.x() + dx * big, a.y() + dy * big, QColor(230, 180, 0), 1, true});
        }
    }
    QPointF o = toScreen(p.origin);
    ov.grips2d.push_back({o.x(), o.y(), QColor(250, 200, 0), 11, hot_ == 8 || drag_ == 8});
}

}  // namespace gable
