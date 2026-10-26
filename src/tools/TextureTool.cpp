// Folio Gable — Texture ▸ Position.
#include "core/Geometry.h"
#include "tools/Tools.h"
#include "view/Viewport.h"
#include <QKeyEvent>
#include <cmath>

namespace gable {

void TexturePositionTool::activate() {
    active_ = false;
    drag_ = -1;
    Entities& E = doc()->active();
    if (face_ < 0 || face_ >= (int)E.faces.size() || !E.faces[face_].alive) { face_ = -1; return; }
    const Face& F = E.faces[face_];
    int mat = back_ ? F.matBack : F.matFront;
    w_ = h_ = 1000;
    if (mat >= 0 && mat < (int)doc()->model.materials.size()) {
        const Material& M = doc()->model.materials[mat];
        w_ = std::max(M.texW, 1e-3);
        h_ = std::max(M.texH, 1e-3);
    }
    TexMap t = geom::effectiveTexMap(F, back_);
    // put the pins on the tile under the face's centre, so they sit on the face
    Vec3 c = E.faceCentroid(face_);
    double u0 = std::floor((c.dot(t.uAxis) + t.uOff) / w_) * w_, v0 = std::floor((c.dot(t.vAxis) + t.vOff) / h_) * h_;
    u0_ = u0;
    v0_ = v0;
    pins_[0] = geom::texPoint(F.plane, t, u0, v0);
    pins_[1] = geom::texPoint(F.plane, t, u0 + w_, v0);
    pins_[2] = geom::texPoint(F.plane, t, u0, v0 + h_);
    doc()->begin("Position Texture");
    doc()->clearSelection();   // the selection pattern would hide the texture being placed
    active_ = true;
    view->hover.clear();
    setStatus(status());
}

Vec3 TexturePositionTool::pinWorld(int i) const { return doc()->activeXf().point(pins_[std::clamp(i, 0, 2)]); }

bool TexturePositionTool::planePoint(double x, double y, Vec3& out) const {
    const Entities& E = doc()->active();
    if (face_ < 0 || face_ >= (int)E.faces.size()) return false;
    Mat4 inv = doc()->activeInv();
    Ray r = view->cam.ray(x, y);
    Ray lr{inv.point(r.o), inv.vector(r.d).normalized()};
    double t = lr.hit(E.faces[face_].plane);
    if (std::isnan(t)) return false;
    out = lr.at(t);
    return true;
}

void TexturePositionTool::apply() {
    Entities& E = doc()->active();
    if (face_ < 0 || face_ >= (int)E.faces.size()) return;
    Face& F = E.faces[face_];
    TexMap t = back_ ? F.texBack : F.texFront;
    if (!geom::texMapFromPins(F.plane, pins_[0], pins_[1], pins_[2], w_, h_, t, u0_, v0_)) return;
    (back_ ? F.texBack : F.texFront) = t;
    E.touch();
    redraw();
}

void TexturePositionTool::mousePress(const MouseEv& e) {
    if (!active_ || e.button != Qt::LeftButton) return;
    // pins first (screen distance), then the face plane
    double best = 10;
    drag_ = -1;
    for (int i = 0; i < 3; ++i) {
        double sx, sy;
        if (!view->cam.project(pinWorld(i), sx, sy)) continue;
        double d = std::hypot(sx - e.x, sy - e.y);
        if (d < best) { best = d; drag_ = i; }
    }
    Vec3 q;
    if (drag_ < 0) {
        PickHit h;
        EntRef r = view->picker.pickActive(e.x, e.y, &h);
        if (!(r.kind == EntKind::Face && r.id == face_)) {
            // a click away from the face finishes
            finish(true);
            toSelectTool();
            return;
        }
        drag_ = 3;
    }
    if (!planePoint(e.x, e.y, q)) { drag_ = -1; return; }
    grab_ = q;
    for (int i = 0; i < 3; ++i) start_[i] = pins_[i];
}

void TexturePositionTool::mouseMove(const MouseEv& e) {
    if (!active_ || drag_ < 0) return;
    Vec3 q;
    if (!planePoint(e.x, e.y, q)) return;
    const Vec3 n = doc()->active().faces[face_].plane.n;
    if (drag_ == 0 || drag_ == 3) {
        Vec3 d = q - grab_;
        for (int i = 0; i < 3; ++i) pins_[i] = start_[i] + d;
    } else if (drag_ == 1) {
        // rotate and scale about the red pin so the green pin follows the cursor
        Vec3 a = start_[1] - start_[0], b = q - start_[0];
        double la = a.length(), lb = b.length();
        if (la < 1e-9 || lb < 1e-9) return;
        double ang = std::atan2(a.cross(b).dot(n), a.dot(b));
        Mat4 R = Mat4::rotation(n, ang);
        double s = lb / la;
        pins_[0] = start_[0];
        pins_[1] = q;
        pins_[2] = start_[0] + R.vector(start_[2] - start_[0]) * s;
    } else if (drag_ == 2) {
        pins_[0] = start_[0];
        pins_[1] = start_[1];
        pins_[2] = q;
    }
    apply();
}

void TexturePositionTool::mouseRelease(const MouseEv& e) {
    if (e.button == Qt::LeftButton) drag_ = -1;
}

bool TexturePositionTool::keyPress(QKeyEvent* e) {
    if (!active_) return false;
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) { finish(true); toSelectTool(); return true; }
    if (e->key() == Qt::Key_Escape) { finish(false); toSelectTool(); return true; }
    return false;
}

void TexturePositionTool::finish(bool keep) {
    if (!active_) return;
    active_ = false;
    drag_ = -1;
    if (doc()->inOperation()) {
        if (keep) doc()->commit();
        else doc()->cancel();
    }
    const Entities& E = doc()->active();
    if (face_ >= 0 && face_ < (int)E.faces.size() && E.faces[face_].alive) doc()->setSelection({{EntKind::Face, face_}});
    redraw();
}

void TexturePositionTool::draw(Overlay& ov) {
    if (!active_) return;
    Vec3 p0 = pinWorld(0), p1 = pinWorld(1), p2 = pinWorld(2), p3 = p1 + (p2 - p0);
    QColor frame(120, 70, 190);
    ov.line(p0, p1, frame, 1.5f, 1);
    ov.line(p1, p3, frame, 1.5f, 1);
    ov.line(p3, p2, frame, 1.5f, 1);
    ov.line(p2, p0, frame, 1.5f, 1);
    ov.marker(p0, icolor::red(), Overlay::FilledCircle, 12);
    ov.marker(p1, icolor::green(), Overlay::FilledCircle, 12);
    ov.marker(p2, icolor::blue(), Overlay::FilledCircle, 12);
}

}  // namespace gable
