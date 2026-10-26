#include "core/Geometry.h"
#include "tools/Tools.h"
#include "view/Viewport.h"

namespace gable {

// ======================================================================= Tape / Protractor

QString TapeTool::status() const {
    if (protractor_) return stage_ == 0 ? "Place the protractor center. Arrow keys lock the plane." : (stage_ == 1 ? "Select the base line." : "Select the angle. Ctrl = toggle guide creation.");
    return stage_ == 0 ? "Select start point. Ctrl = toggle create guide." : "Select end point or type a length (resizes the model if guides are off).";
}

bool TapeTool::keyPress(QKeyEvent* e) {
    if (e->key() == Qt::Key_Control && !e->isAutoRepeat()) {
        createGuides_ = !createGuides_;
        setStatus(createGuides_ ? "Guides on." : "Guides off (measure only).");
        return true;
    }
    return lockKeys(e);
}

void TapeTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    infer(e);
    Mat4 inv = doc()->activeInv();
    Entities& E = doc()->active();
    if (protractor_) {
        if (stage_ == 0) {
            a_ = cur.p;
            normal_ = cur.onFace ? cur.facePlane.n : Vec3(0, 0, 1);
            if (inf().lock == Inference::Lock::Line && inf().lockAxis >= 0 && inf().lockAxis < 3) normal_ = inf().axis(inf().lockAxis);
            stage_ = 1;
            inf().reset();
            inf().setAnchor(a_);
        } else if (stage_ == 1) {
            b_ = Plane(normal_, a_).project(cur.p);
            if ((b_ - a_).length() < kTol) return;
            stage_ = 2;
        } else {
            Vec3 p = Plane(normal_, a_).project(cur.p);
            Vec3 d = (p - a_).normalized();
            // snap to 15 degrees relative to baseline
            Vec3 base = (b_ - a_).normalized();
            double ang = std::atan2(normal_.dot(base.cross(d)), base.dot(d));
            double deg = std::round(ang * 180 / kPi / 15) * 15;
            if (std::fabs(ang * 180 / kPi - deg) < 2) d = Mat4::rotation(normal_, deg * kPi / 180).vector(base);
            if (createGuides_) {
                doc()->begin("Protractor Guide");
                Guide g;
                g.p = inv.point(a_);
                g.dir = inv.vector(d).normalized();
                E.guides.push_back(g);
                E.touch();
                doc()->commit();
            }
            stage_ = 0;
            inf().reset();
        }
        return;
    }
    if (stage_ == 0) {
        a_ = cur.p;
        fromEdge_ = cur.type == InfType::OnEdge && cur.hit.kind == EntKind::Edge;
        if (fromEdge_) {
            const Entities& HE = doc()->entitiesAt(cur.hit.path);
            const Edge& Ed = HE.edges[cur.hit.id];
            edgeDir_ = (cur.hit.xf.point(HE.verts[Ed.v[1]].p) - cur.hit.xf.point(HE.verts[Ed.v[0]].p)).normalized();
        }
        stage_ = 1;
        inf().setAnchor(a_);
        return;
    }
    b_ = cur.p;
    double meas = fromEdge_ ? ((b_ - a_) - edgeDir_ * (b_ - a_).dot(edgeDir_)).length() : (b_ - a_).length();
    lastMeasure_ = meas;
    haveMeasure_ = true;
    if (createGuides_ && meas > kTol) {
        doc()->begin("Guide");
        Guide g;
        if (fromEdge_) {
            g.p = inv.point(b_);
            g.dir = inv.vector(edgeDir_).normalized();
        } else {
            g.isPoint = true;
            g.p = inv.point(b_);
            // also a guide line along the measured direction for convenience
            Guide l;
            l.p = inv.point(a_);
            l.dir = inv.vector((b_ - a_).normalized()).normalized();
            E.guides.push_back(l);
        }
        E.guides.push_back(g);
        E.touch();
        doc()->commit();
    }
    stage_ = 0;
    inf().reset();
}

void TapeTool::mouseMove(const MouseEv& e) {
    infer(e);
    const UnitSettings& U = doc()->model.units;
    if (protractor_) {
        if (stage_ == 2) {
            Vec3 p = Plane(normal_, a_).project(cur.p);
            Vec3 base = (b_ - a_), d = p - a_;
            if (d.length() > 1e-9 && base.length() > 1e-9)
                setVCB(formatAngle(std::fabs(std::atan2(normal_.dot(base.cross(d)), base.dot(d))), U));
        }
        return;
    }
    if (stage_ == 1) {
        double meas = fromEdge_ ? ((cur.p - a_) - edgeDir_ * (cur.p - a_).dot(edgeDir_)).length() : (cur.p - a_).length();
        setVCB(formatLength(meas, U));
    }
}

void TapeTool::vcbEnter(const QString& s) {
    const UnitSettings& U = doc()->model.units;
    Mat4 inv = doc()->activeInv();
    Entities& E = doc()->active();
    if (protractor_) {
        if (stage_ != 2) return;
        bool ok;
        double deg = s.toDouble(&ok);
        if (!ok) return;
        Vec3 base = (b_ - a_).normalized();
        Vec3 p = Plane(normal_, a_).project(cur.p);
        double sign = normal_.dot(base.cross(p - a_)) < 0 ? -1 : 1;
        Vec3 d = Mat4::rotation(normal_, sign * deg * kPi / 180).vector(base);
        doc()->begin("Protractor Guide");
        Guide g;
        g.p = inv.point(a_);
        g.dir = inv.vector(d).normalized();
        E.guides.push_back(g);
        E.touch();
        doc()->commit();
        stage_ = 0;
        inf().reset();
        return;
    }
    double L;
    if (!parseLength(s, U, L)) return;
    if (stage_ == 1) {
        // place a guide at exactly L in the current direction
        Vec3 dir;
        if (fromEdge_) {
            Vec3 d = (cur.p - a_) - edgeDir_ * (cur.p - a_).dot(edgeDir_);
            if (d.length() < 1e-9) return;
            dir = d.normalized();
        } else {
            Vec3 d = cur.p - a_;
            if (d.length() < 1e-9) return;
            dir = d.normalized();
        }
        b_ = a_ + dir * L;
        doc()->begin("Guide");
        Guide g;
        if (fromEdge_) { g.p = inv.point(b_); g.dir = inv.vector(edgeDir_).normalized(); }
        else { g.isPoint = true; g.p = inv.point(b_); }
        E.guides.push_back(g);
        E.touch();
        doc()->commit();
        stage_ = 0;
        inf().reset();
        lastMeasure_ = L;
        return;
    }
    // resize the active context to make the last measurement equal to L
    if (haveMeasure_ && lastMeasure_ > kTol && L > 0) {
        double f = L / lastMeasure_;
        if (confirmResize && !confirmResize(f)) return;
        doc()->begin("Resize");
        Entities& A = doc()->active();
        std::vector<int> vs;
        for (int v = 0; v < (int)A.verts.size(); ++v) if (A.verts[v].alive) vs.push_back(v);
        Mat4 S = Mat4::scaling(Vec3(f, f, f));
        for (int v : vs) A.verts[v].p = S.point(A.verts[v].p);
        A.invalidateIndex();
        for (auto& F : A.faces) if (F.alive) F.plane.d *= f;
        for (auto& in : A.insts) if (in.alive) in.xf = S * in.xf;
        for (auto& g : A.guides) g.p = S.point(g.p);
        for (auto& d : A.dims) { d.a = S.point(d.a); d.b = S.point(d.b); d.offset = d.offset * f; }
        for (auto& t : A.texts) { t.anchor = S.point(t.anchor); t.leader = t.leader * f; }
        for (auto& c : A.curves) { c.center = S.point(c.center); c.radius *= f; }
        A.touch();
        doc()->commit();
        haveMeasure_ = false;
    }
}

void TapeTool::draw(Overlay& ov) {
    if (protractor_) {
        Vec3 c = stage_ == 0 ? cur.p : a_;
        Vec3 n = stage_ == 0 ? (cur.onFace ? cur.facePlane.n : Vec3(0, 0, 1)) : normal_;
        double R = pixelTol(c, 40);
        Vec3 u = n.anyPerp(), v = n.cross(u);
        for (int i = 0; i < 24; ++i) {
            double a0 = kPi * i / 24, a1 = kPi * (i + 1) / 24;
            ov.line(c + (u * std::cos(a0) + v * std::sin(a0)) * R, c + (u * std::cos(a1) + v * std::sin(a1)) * R, QColor(30, 60, 230), 1.5f, 0, false);
        }
        if (stage_ >= 1) ov.line(a_, stage_ == 1 ? cur.p : b_, QColor(0, 0, 0), 1.0f, 2, false);
        if (stage_ == 2) {
            Vec3 p = Plane(normal_, a_).project(cur.p);
            Vec3 d = (p - a_).normalized();
            double big = pixelTol(a_, 3000);
            ov.line(a_ - d * big, a_ + d * big, QColor(60, 60, 60), 1.0f, 1, false);
        }
        drawInf(ov);
        return;
    }
    if (stage_ == 1) {
        if (fromEdge_) {
            double big = pixelTol(cur.p, 3000);
            ov.line(cur.p - edgeDir_ * big, cur.p + edgeDir_ * big, QColor(60, 60, 60), 1.0f, 1, false);
            Vec3 foot = a_ + edgeDir_ * (cur.p - a_).dot(edgeDir_);
            ov.line(foot, cur.p, QColor(0, 0, 0), 1.0f, 2, false);
        } else {
            ov.line(a_, cur.p, QColor(0, 0, 0), 1.5f, 2, false);
        }
        double meas = fromEdge_ ? ((cur.p - a_) - edgeDir_ * (cur.p - a_).dot(edgeDir_)).length() : (cur.p - a_).length();
        ov.label(cur.p, formatLength(meas, doc()->model.units));
        ov.labels.back().dx = 30;
        ov.labels.back().dy = -20;
    }
    drawInf(ov);
}

// ======================================================================= Dimension

void DimensionTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    infer(e);
    if (stage_ == 0) {
        radial_ = false;
        if (cur.type == InfType::OnEdge && cur.hit.kind == EntKind::Edge) {
            const Entities& HE = doc()->entitiesAt(cur.hit.path);
            const Edge& Ed = HE.edges[cur.hit.id];
            if (Ed.curve >= 0 && HE.curves[Ed.curve].isArc) {
                radial_ = true;
                a_ = cur.hit.xf.point(HE.curves[Ed.curve].center);
                b_ = cur.p;
            } else {
                a_ = cur.hit.xf.point(HE.verts[Ed.v[0]].p);
                b_ = cur.hit.xf.point(HE.verts[Ed.v[1]].p);
            }
            stage_ = 2;
            return;
        }
        a_ = cur.p;
        stage_ = 1;
        inf().setAnchor(a_);
        return;
    }
    if (stage_ == 1) {
        if ((cur.p - a_).length() < kTol) return;
        b_ = cur.p;
        stage_ = 2;
        return;
    }
    finish();
}

void DimensionTool::mouseMove(const MouseEv& e) {
    infer(e);
    if (stage_ == 2 && !radial_) {
        Vec3 d = (b_ - a_).normalized();
        Vec3 f = view->cam.forward();
        Vec3 n = d.cross(f.cross(d));
        if (n.length() < 1e-6) n = f;
        Plane pl(n.normalized(), a_);
        Ray r = view->cam.ray(e.x, e.y);
        double t = r.hit(pl);
        Vec3 p = std::isnan(t) ? cur.p : r.at(t);
        Vec3 o = p - a_;
        off_ = o - d * o.dot(d);
        // snap offset to axis directions
        for (int i = 0; i < 3; ++i) {
            Vec3 ax = inf().axis(i);
            if (std::fabs(ax.dot(d)) > 0.999) continue;
            Vec3 axp = (ax - d * ax.dot(d)).normalized();
            double proj = off_.dot(axp);
            if ((off_ - axp * proj).length() < pixelTol(p, 15)) off_ = axp * proj;
        }
    }
    if (stage_ == 2 && radial_) off_ = cur.p - b_;
}

void DimensionTool::finish() {
    Mat4 inv = doc()->activeInv();
    Dimension d;
    d.a = inv.point(a_);
    d.b = inv.point(b_);
    d.offset = inv.vector(off_);
    d.radial = radial_;
    doc()->begin("Dimension");
    doc()->active().dims.push_back(d);
    doc()->active().touch();
    doc()->commit();
    reset();
}

void DimensionTool::draw(Overlay& ov) {
    if (stage_ == 1) ov.line(a_, cur.p, QColor(0, 0, 0), 1.0f, 2, false);
    if (stage_ == 2) {
        if (radial_) {
            ov.line(a_, b_, QColor(0, 0, 0), 1.0f, 0, false);
            ov.label((a_ + b_) * 0.5, "R " + formatLength((b_ - a_).length(), doc()->model.units));
        } else {
            ov.line(a_, a_ + off_, QColor(0, 0, 0), 1.0f, 0, false);
            ov.line(b_, b_ + off_, QColor(0, 0, 0), 1.0f, 0, false);
            ov.line(a_ + off_, b_ + off_, QColor(0, 0, 0), 1.0f, 0, false);
            ov.label((a_ + b_) * 0.5 + off_, formatLength((b_ - a_).length(), doc()->model.units));
        }
        return;
    }
    drawInf(ov);
}

// ======================================================================= Text

void TextTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    infer(e);
    const UnitSettings& U = doc()->model.units;
    Mat4 inv = doc()->activeInv();
    if (stage_ == 0) {
        Picker::DeepResult dr = view->picker.deep(e.x, e.y, 9, 6);
        if (!dr.vertex.valid() && !dr.edge.valid() && !dr.face.valid()) {
            QString t = "Enter text";
            if (editText && !editText(t)) return;
            Text tx;
            tx.screenText = true;
            tx.sx = e.x / std::max(1, view->width());
            tx.sy = e.y / std::max(1, view->height());
            tx.text = t;
            doc()->begin("Text");
            doc()->active().texts.push_back(tx);
            doc()->active().touch();
            doc()->commit();
            return;
        }
        anchor_ = cur.p;
        if (dr.vertex.valid()) {
            Vec3 p = dr.vertex.point;
            text_ = QString("%1, %2, %3").arg(formatLength(p.x, U), formatLength(p.y, U), formatLength(p.z, U));
            anchor_ = p;
        } else if (dr.edge.valid()) {
            const Entities& E = doc()->entitiesAt(dr.edge.path);
            const Edge& Ed = E.edges[dr.edge.id];
            double L = (dr.edge.xf.point(E.verts[Ed.v[0]].p) - dr.edge.xf.point(E.verts[Ed.v[1]].p)).length();
            text_ = formatLength(L, U);
            anchor_ = dr.edge.point;
        } else {
            const Entities& E = doc()->entitiesAt(dr.face.path);
            double s = dr.face.xf.vector(Vec3(1, 0, 0)).length();
            text_ = formatArea(E.faceArea(dr.face.id) * s * s, U);
            if (dr.face.path.size() > doc()->editPath.size()) {
                const Entities* P = &doc()->model.root;
                const Instance* in = nullptr;
                for (size_t k = 0; k <= doc()->editPath.size() && k < dr.face.path.size(); ++k) {
                    in = &P->insts[dr.face.path[k]];
                    P = &doc()->model.defs[in->def].ents;
                }
                if (in) text_ = in->name.isEmpty() ? doc()->model.defs[in->def].name : in->name;
            }
            anchor_ = dr.face.point;
        }
        stage_ = 1;
        return;
    }
    // place text
    Vec3 f = view->cam.forward();
    Plane pl(-f, anchor_);
    Ray r = view->cam.ray(e.x, e.y);
    double t = r.hit(pl);
    Vec3 p = std::isnan(t) ? anchor_ : r.at(t);
    QString txt = text_;
    if (editText && !editText(txt)) { stage_ = 0; return; }
    Text tx;
    tx.anchor = inv.point(anchor_);
    tx.leader = inv.vector(p - anchor_);
    tx.text = txt;
    doc()->begin("Text");
    doc()->active().texts.push_back(tx);
    doc()->active().touch();
    doc()->commit();
    stage_ = 0;
}

void TextTool::mouseMove(const MouseEv& e) { infer(e); }

void TextTool::draw(Overlay& ov) {
    if (stage_ == 1) {
        Vec3 f = view->cam.forward();
        Plane pl(-f, anchor_);
        Ray r = view->cam.ray(lastX, lastY);
        double t = r.hit(pl);
        Vec3 p = std::isnan(t) ? anchor_ : r.at(t);
        ov.line(anchor_, p, QColor(0, 0, 0), 1.0f, 0, false);
        ov.label(p, text_);
        return;
    }
    drawInf(ov);
}

// ======================================================================= Axes

void AxesTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    infer(e);
    if (stage_ == 0) { o_ = cur.p; stage_ = 1; inf().setAnchor(o_); return; }
    if (stage_ == 1) {
        if ((cur.p - o_).length() < kTol) return;
        x_ = (cur.p - o_).normalized();
        stage_ = 2;
        return;
    }
    Vec3 y = cur.p - o_;
    y = y - x_ * y.dot(x_);
    if (y.length() < kTol) return;
    y = y.normalized();
    Model& M = doc()->model;
    doc()->begin("Axes");
    M.axesOrigin = o_;
    M.axesX = x_;
    M.axesY = y;
    M.axesZ = x_.cross(y).normalized();
    doc()->commit();
    reset();
    toSelectTool();
}

void AxesTool::mouseMove(const MouseEv& e) { infer(e); }

void AxesTool::draw(Overlay& ov) {
    double L = pixelTol(stage_ == 0 ? cur.p : o_, 80);
    if (stage_ == 0) {
        ov.line(cur.p, cur.p + inf().axis(0) * L, icolor::red(), 2.0f, 0, false);
        ov.line(cur.p, cur.p + inf().axis(1) * L, icolor::green(), 2.0f, 0, false);
        ov.line(cur.p, cur.p + inf().axis(2) * L, icolor::blue(), 2.0f, 0, false);
    } else if (stage_ == 1) {
        ov.line(o_, cur.p, icolor::red(), 2.0f, 0, false);
    } else {
        ov.line(o_, o_ + x_ * L, icolor::red(), 2.0f, 0, false);
        Vec3 y = cur.p - o_;
        y = y - x_ * y.dot(x_);
        if (y.length() > 1e-9) {
            ov.line(o_, o_ + y.normalized() * L, icolor::green(), 2.0f, 0, false);
            ov.line(o_, o_ + x_.cross(y.normalized()) * L, icolor::blue(), 2.0f, 0, false);
        }
    }
    drawInf(ov);
}

// ======================================================================= Section plane

bool SectionTool::keyPress(QKeyEvent* e) {
    int ax = -1;
    if (e->key() == Qt::Key_Right) ax = 0;
    else if (e->key() == Qt::Key_Left) ax = 1;
    else if (e->key() == Qt::Key_Up) ax = 2;
    if (ax < 0) return false;
    lockAxis_ = lockAxis_ == ax ? -1 : ax;
    return true;
}

void SectionTool::mouseMove(const MouseEv& e) {
    infer(e);
    valid_ = true;
    if (lockAxis_ >= 0) n_ = inf().axis(lockAxis_);
    else if (cur.onFace) n_ = cur.facePlane.n;
    else {
        Vec3 f = view->cam.forward();
        n_ = -Vec3(f.x, f.y, 0).normalized();
        if (n_.isZero()) n_ = Vec3(0, 0, 1);
    }
}

void SectionTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton || !valid_) return;
    infer(e);
    Mat4 inv = doc()->activeInv();
    Entities& E = doc()->active();
    doc()->begin("Section Plane");
    for (auto& s : E.sections) s.active = false;
    SectionPlane sp;
    sp.plane = Plane(inv.normal(n_), inv.point(cur.p));
    sp.name = QString("Section %1").arg(E.aliveCount(EntKind::Section) + 1);
    sp.active = true;
    E.sections.push_back(sp);
    E.touch();
    doc()->commit();
    doc()->setSelection({{EntKind::Section, (int)E.sections.size() - 1}});
    toSelectTool();
}

void SectionTool::draw(Overlay& ov) {
    if (!valid_) return;
    double R = pixelTol(cur.p, 60);
    Vec3 u = n_.anyPerp(), v = n_.cross(u);
    Vec3 q[4] = {cur.p + (u + v) * R, cur.p + (u - v) * R, cur.p + (-u - v) * R, cur.p + (-u + v) * R};
    for (int k = 0; k < 4; ++k) ov.line(q[k], q[(k + 1) % 4], QColor(230, 120, 30), 2.0f, 0, false);
    ov.line(cur.p, cur.p - n_ * R * 0.6, QColor(230, 120, 30), 2.0f, 0, false);
    drawInf(ov);
}

// ======================================================================= Place component

void Text3DPlaceTool::mouseMove(const MouseEv& e) { infer(e); }

void Text3DPlaceTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton || def < 0) return;
    infer(e);
    Mat4 inv = doc()->activeInv();
    Mat4 xf = Mat4::translation(inv.point(cur.p));
    // glue to faces: orient z to face normal when placed on a vertical face
    if (cur.onFace && doc()->model.defs[def].glue) {
        Vec3 n = inv.normal(cur.facePlane.n);
        Vec3 x = std::fabs(n.z) > 0.99 ? Vec3(1, 0, 0) : Vec3(0, 0, 1).cross(n).normalized();
        Vec3 y = n.cross(x);
        xf = Mat4::fromAxes(inv.point(cur.p), x, y, n);
    } else if (cur.onFace && std::fabs(cur.facePlane.n.z) < 0.5) {
        // stand text-like components on vertical faces
        Vec3 n = inv.normal(cur.facePlane.n);
        Vec3 x = Vec3(0, 0, 1).cross(n).normalized();
        Vec3 y = n.cross(x);
        xf = Mat4::fromAxes(inv.point(cur.p), x, y, n);
    }
    Instance in;
    in.def = def;
    in.xf = xf;
    in.attrs = doc()->model.defs[def].attrs;   // a dynamic component starts with its definition's attributes
    doc()->begin("Place Component");
    doc()->active().insts.push_back(in);
    // Cut opening: punch the component's footprint (its faces on local z=0) into the face it sits on.
    const ComponentDef& D = doc()->model.defs[def];
    if (D.cutsOpening && cur.onFace && cur.faceHit.path == doc()->editPath) {
        Entities& A = doc()->active();
        std::vector<std::vector<Vec3>> loops;
        for (int f = 0; f < (int)D.ents.faces.size(); ++f) {
            const Face& F = D.ents.faces[f];
            if (!F.alive || std::fabs(F.plane.n.z) < 0.999 || std::fabs(F.plane.d) > kTol) continue;
            std::vector<Vec3> L;
            for (int v : F.outer) L.push_back(xf.point(D.ents.verts[v].p));
            loops.push_back(L);
        }
        if (loops.empty()) {
            // fall back to the bounding box footprint
            BBox b = doc()->model.defBBox(def);
            if (b.valid())
                loops.push_back({xf.point(Vec3(b.lo.x, b.lo.y, 0)), xf.point(Vec3(b.hi.x, b.lo.y, 0)),
                                 xf.point(Vec3(b.hi.x, b.hi.y, 0)), xf.point(Vec3(b.lo.x, b.hi.y, 0))});
        }
        int target = cur.faceHit.id;
        Plane tp = A.faces[target].plane;
        for (auto& L : loops) {
            geom::addPolyline(A, L, true);
            // remove the face that now fills the footprint
            Vec3 c;
            for (auto& p : L) c += p;
            c = c / (double)L.size();
            for (int f = 0; f < (int)A.faces.size(); ++f)
                if (A.faces[f].alive && A.faces[f].plane.sameAs(tp) && A.pointInFace(f, c) && A.faces[f].holes.empty()) {
                    BBox fb;
                    for (int v : A.faces[f].outer) fb.add(A.verts[v].p);
                    BBox lb;
                    for (auto& p : L) lb.add(p);
                    if ((fb.size() - lb.size()).length() < 1.0) { A.removeFace(f); break; }
                }
        }
    }
    doc()->active().touch();
    doc()->commit();
    doc()->setSelection({{EntKind::Instance, (int)doc()->active().insts.size() - 1}});
    toSelectTool();
}

void Text3DPlaceTool::draw(Overlay& ov) {
    if (def < 0) return;
    BBox b = doc()->model.defBBox(def);
    if (b.valid()) {
        Mat4 m = Mat4::translation(cur.p);
        static const int ed[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (auto& x : ed) ov.line(m.point(b.corner(x[0])), m.point(b.corner(x[1])), icolor::selection(), 1.0f, 0, false);
        const Entities& D = doc()->model.defs[def].ents;
        int n = 0;
        for (auto& Ed : D.edges) {
            if (!Ed.alive || Ed.soft) continue;
            if (++n > 4000) break;
            ov.line(m.point(D.verts[Ed.v[0]].p), m.point(D.verts[Ed.v[1]].p), QColor(60, 60, 60), 1.0f, 0, false);
        }
    }
    drawInf(ov);
}

// ======================================================================= Camera tools

void OrbitTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    down_ = true;
    lx_ = e.x; ly_ = e.y;
    pivot_ = view->orbitPivot(e.x, e.y);
    view->pushViewHistory();
    view->beginOrbit(pivot_);
}
void OrbitTool::mouseRelease(const MouseEv&) {
    down_ = false;
    view->endOrbit();
}
void OrbitTool::mouseMove(const MouseEv& e) {
    if (!down_) return;
    if (e.shift()) view->cam.pan(e.x - lx_, e.y - ly_, pivot_);
    else if (e.ctrl()) view->cam.orbitFree(e.x - lx_, e.y - ly_, pivot_);
    else view->cam.orbit(e.x - lx_, e.y - ly_, pivot_);
    lx_ = e.x; ly_ = e.y;
}

void PanTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    down_ = true;
    lx_ = e.x; ly_ = e.y;
    pivot_ = view->orbitPivot(e.x, e.y);
    view->pushViewHistory();
}
void PanTool::mouseMove(const MouseEv& e) {
    if (!down_) return;
    view->cam.pan(e.x - lx_, e.y - ly_, pivot_);
    lx_ = e.x; ly_ = e.y;
}

void ZoomTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    down_ = true;
    ly_ = e.y; px_ = e.x; py_ = e.y;
    pivot_ = view->orbitPivot(e.x, e.y);
    view->pushViewHistory();
}
void ZoomTool::mouseMove(const MouseEv& e) {
    if (!down_) return;
    double dy = e.y - ly_;
    ly_ = e.y;
    if (e.shift()) {
        view->cam.setFov(view->cam.s->fov + dy * 0.2);
        setVCB(QString::number(view->cam.s->fov, 'f', 1) + "°");
    } else {
        view->cam.zoomAt(std::exp(-dy * 0.01), px_, py_, pivot_, true);
    }
}
void ZoomTool::vcbEnter(const QString& sIn) {
    QString s = sIn.trimmed().toLower();
    if (s.endsWith("mm")) {
        double f = s.left(s.size() - 2).toDouble();
        if (f > 0) view->cam.setFov(2 * std::atan(12.0 / f) * 180 / kPi);
    } else {
        s.remove("deg").remove("°");
        double d = s.toDouble();
        if (d > 0) view->cam.setFov(d);
    }
    setVCB(QString::number(view->cam.s->fov, 'f', 1) + "°");
    redraw();
}

void ZoomWindowTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    down_ = true;
    x0_ = x1_ = e.x;
    y0_ = y1_ = e.y;
}
void ZoomWindowTool::mouseMove(const MouseEv& e) {
    if (!down_) return;
    x1_ = e.x; y1_ = e.y;
}
void ZoomWindowTool::mouseRelease(const MouseEv& e) {
    if (!down_ || e.button != Qt::LeftButton) return;
    down_ = false;
    view->pushViewHistory();
    BBox b = doc()->model.worldBBox();
    Vec3 dp = b.valid() ? b.center() : view->cam.s->target;
    Picker::DeepResult dr = view->picker.deep((x0_ + x1_) / 2, (y0_ + y1_) / 2, 0, 3);
    if (dr.face.valid()) dp = dr.face.point;
    view->cam.zoomBox(x0_, y0_, x1_, y1_, dp);
    view->returnToPreviousTool();
}
void ZoomWindowTool::draw(Overlay& ov) {
    if (down_) ov.boxes.push_back({x0_, y0_, x1_, y1_, false, QColor(0, 0, 0)});
}

void WalkTool::activate() {
    if (!view->cam.s->perspective) view->setPerspective(true);
}
void WalkTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    down_ = true;
    px_ = lx_ = e.x;
    py_ = ly_ = e.y;
}
void WalkTool::mouseMove(const MouseEv& e) {
    if (!down_) return;
    Camera& c = view->cam;
    if (look_) {
        c.lookAround(e.x - lx_, e.y - ly_);
    } else {
        double speed = (e.ctrl() ? 4.0 : 1.0) * 10;  // mm per pixel offset per event
        double dx = e.x - px_, dy = e.y - py_;
        if (e.shift()) {
            Vec3 up(0, 0, -dy * speed * 0.2);
            Vec3 side = c.right() * (dx * speed * 0.2);
            c.s->eye += up + side;
            c.s->target += up + side;
        } else {
            Vec3 f = c.forward();
            Vec3 fh = Vec3(f.x, f.y, 0).normalized();
            Vec3 mv = fh * (-dy * speed * 0.2);
            c.s->eye += mv;
            c.s->target += mv;
            Mat4 r = Mat4::rotationAbout(c.s->eye, Vec3(0, 0, 1), -dx * 0.0004);
            c.s->target = r.point(c.s->target);
        }
    }
    lx_ = e.x; ly_ = e.y;
}
bool WalkTool::keyPress(QKeyEvent* e) {
    Camera& c = view->cam;
    double step = (e->modifiers() & Qt::ControlModifier) ? 600 : 150;
    Vec3 f = c.forward();
    Vec3 fh = Vec3(f.x, f.y, 0).normalized();
    switch (e->key()) {
    case Qt::Key_Up: case Qt::Key_W: c.s->eye += fh * step; c.s->target += fh * step; break;
    case Qt::Key_Down: case Qt::Key_S: c.s->eye -= fh * step; c.s->target -= fh * step; break;
    case Qt::Key_Left: { Mat4 r = Mat4::rotationAbout(c.s->eye, Vec3(0, 0, 1), 0.06); c.s->target = r.point(c.s->target); break; }
    case Qt::Key_Right: { Mat4 r = Mat4::rotationAbout(c.s->eye, Vec3(0, 0, 1), -0.06); c.s->target = r.point(c.s->target); break; }
    default: return false;
    }
    return true;
}
void WalkTool::vcbEnter(const QString& s) {
    double h;
    if (!parseLength(s, doc()->model.units, h)) return;
    Camera& c = view->cam;
    double dz = h - c.s->eye.z;
    c.s->eye.z += dz;
    c.s->target.z += dz;
    redraw();
}

void PositionCameraTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    infer(e);
    p0_ = cur.p;
    down_ = true;
}
void PositionCameraTool::mouseMove(const MouseEv& e) { infer(e); }
void PositionCameraTool::mouseRelease(const MouseEv& e) {
    if (!down_ || e.button != Qt::LeftButton) return;
    down_ = false;
    infer(e);
    Camera& c = view->cam;
    view->pushViewHistory();
    Vec3 eye = p0_ + Vec3(0, 0, eyeH_);
    Vec3 dir;
    if ((cur.p - p0_).length() > pixelTol(p0_, 10)) dir = (cur.p + Vec3(0, 0, eyeH_)) - eye;
    else {
        Vec3 f = c.forward();
        dir = Vec3(f.x, f.y, 0);
        if (dir.length() < 1e-6) dir = Vec3(0, 1, 0);
    }
    dir = dir.normalized();
    c.s->perspective = true;
    c.s->eye = eye;
    c.s->target = eye + dir * 3000;
    c.s->up = Vec3(0, 0, 1);
    view->setTool("lookaround");
}
void PositionCameraTool::vcbEnter(const QString& s) {
    double h;
    if (parseLength(s, doc()->model.units, h)) eyeH_ = h;
}
void PositionCameraTool::draw(Overlay& ov) {
    if (down_) ov.line(p0_, cur.p, QColor(0, 0, 0), 1.0f, 2, false);
    drawInf(ov);
}

}  // namespace gable

#include "core/Serializer.h"
namespace gable {

void PasteTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton || !clip) return;
    infer(e);
    Mat4 world = Mat4::translation(cur.p - ref);
    Mat4 local = doc()->activeInv() * world;
    doc()->begin("Paste");
    auto res = mergeModelInto(doc()->model, doc()->active(), *clip, local);
    doc()->commit();
    doc()->setSelection(res);
    toSelectTool();
}

void PasteTool::draw(Overlay& ov) {
    if (!clip) return;
    Mat4 m = Mat4::translation(cur.p - ref);
    int n = 0;
    std::function<void(const Entities&, const Mat4&, int)> walk = [&](const Entities& E, const Mat4& xf, int depth) {
        if (depth > 8) return;
        for (auto& Ed : E.edges) {
            if (!Ed.alive || Ed.soft) continue;
            if (++n > 5000) return;
            ov.line(xf.point(E.verts[Ed.v[0]].p), xf.point(E.verts[Ed.v[1]].p), QColor(40, 40, 40), 1.0f, 0, false);
        }
        for (auto& in : E.insts)
            if (in.alive && in.def >= 0 && in.def < (int)clip->defs.size()) walk(clip->defs[in.def].ents, xf * in.xf, depth + 1);
    };
    walk(clip->root, m, 0);
    drawInf(ov);
}

}  // namespace gable
