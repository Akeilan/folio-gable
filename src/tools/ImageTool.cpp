#include "core/Geometry.h"
#include "tools/Tools.h"
#include "ui/Document.h"
#include "view/Viewport.h"
#include <QBuffer>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>

namespace gable {

bool ImageTool::setImage(const QString& path, QString* err) {
    QImageReader r(path);
    r.setAutoTransform(true);
    QImage img = r.read();
    if (img.isNull()) { if (err) *err = "Cannot read the picture: " + r.errorString(); return false; }
    // keep the model small: textures beyond 4096 px add nothing on screen
    if (img.width() > 4096 || img.height() > 4096) img = img.scaled(4096, 4096, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QBuffer buf(&png_);
    png_.clear();
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, img.hasAlphaChannel() ? "PNG" : "JPG", 92);
    aspect_ = double(img.width()) / std::max(1, img.height());
    name_ = QFileInfo(path).completeBaseName();
    stage_ = 0;
    return true;
}

void ImageTool::frameFrom(const Vec3& p) {
    Plane pl = shapePlane(cur, -1);
    n_ = pl.n;
    if (std::fabs(n_.z) > 0.99) {
        // on the ground or a level face the picture faces up, along the red axis
        n_ = inf().axis(2);
        x_ = inf().axis(0);
    } else {
        x_ = Vec3(0, 0, 1).cross(n_).normalized();
    }
    y_ = n_.cross(x_).normalized();
    o_ = p;
}

void ImageTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton || png_.isEmpty()) return;
    infer(e);
    if (stage_ == 0) {
        frameFrom(cur.p);
        stage_ = 1;
        width_ = 0;
        inf().setAnchor(o_);
        return;
    }
    if (width_ > kTol) place(width_);
}

void ImageTool::mouseMove(const MouseEv& e) {
    infer(e);
    if (stage_ == 1) {
        // size from the cursor's position in the picture's plane
        Ray r = view->cam.ray(e.x, e.y);
        double t = r.hit(Plane(n_, o_));
        Vec3 p = std::isnan(t) ? cur.p : r.at(t);
        Vec3 d = p - o_;
        width_ = std::max(std::fabs(d.dot(x_)), std::fabs(d.dot(y_)) * aspect_);
        setVCB(formatLength(width_, doc()->model.units));
    }
}

void ImageTool::vcbEnter(const QString& s) {
    double w;
    if (stage_ == 1 && parseLength(s, doc()->model.units, w) && w > kTol) place(w);
}

void ImageTool::place(double width) {
    double height = width / aspect_;
    Model& M = doc()->model;
    doc()->begin("Place Image");
    Material mt;
    mt.name = name_;
    mt.color = QColor(255, 255, 255);
    mt.hasTexture = true;
    mt.texturePng = png_;
    mt.texW = width;
    mt.texH = height;
    M.materials.push_back(mt);
    int mat = (int)M.materials.size() - 1;
    int def = M.addDef(name_.isEmpty() ? QString("Image") : name_, true);
    Entities& G = M.defs[def].ents;
    geom::addPolyline(G, {{0, 0, 0}, {width, 0, 0}, {width, height, 0}, {0, height, 0}}, true);
    for (int f = 0; f < (int)G.faces.size(); ++f) {
        Face& F = G.faces[f];
        if (!F.alive) continue;
        if (F.plane.n.z < 0) geom::reverseFace(G, f);
        Face& F2 = G.faces[f];
        F2.matFront = F2.matBack = mat;
        TexMap tm;
        tm.custom = true;
        tm.uAxis = Vec3(1, 0, 0);
        tm.vAxis = Vec3(0, 1, 0);
        F2.texFront = tm;
        // the back shows the picture mirrored, as it would through a sheet of paper
        F2.texBack = tm;
    }
    Mat4 inv = doc()->activeInv();
    Vec3 o = inv.point(o_), x = inv.vector(x_).normalized(), y = inv.vector(y_).normalized(), n = inv.vector(n_).normalized();
    Instance in;
    in.def = def;
    in.xf = Mat4::fromAxes(o, x, y, n);
    in.name = name_;
    doc()->active().insts.push_back(in);
    doc()->active().touch();
    doc()->commit();
    doc()->setSelection({{EntKind::Instance, (int)doc()->active().insts.size() - 1}});
    stage_ = 0;
    inf().reset();
    toSelectTool();
}

void ImageTool::draw(Overlay& ov) {
    drawInf(ov);
    if (stage_ != 1 || width_ <= kTol) return;
    double h = width_ / aspect_;
    Vec3 a = o_, b = o_ + x_ * width_, c = b + y_ * h, d = o_ + y_ * h;
    QColor col(110, 60, 170);
    ov.line(a, b, col, 1.5f); ov.line(b, c, col, 1.5f); ov.line(c, d, col, 1.5f); ov.line(d, a, col, 1.5f);
    ov.line(a, c, col, 1.0f, 1); ov.line(b, d, col, 1.0f, 1);
}

}  // namespace gable
