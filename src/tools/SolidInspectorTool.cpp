// Folio Gable — Solid Inspector.
#include "core/Geometry.h"
#include "tools/Tools.h"
#include "view/Viewport.h"
#include <QKeyEvent>
#include <QMenu>

namespace gable {

static QString plural(int n, const char* one, const char* many) { return QString("%1 %2").arg(n).arg(n == 1 ? one : many); }

QString SolidInspectorTool::describe(const geom::SolidReport& r) {
    QStringList p;
    if (!r.holes.empty()) p << plural((int)r.holes.size(), "hole", "holes");
    if (!r.strayEdges.empty()) p << plural((int)r.strayEdges.size(), "stray edge", "stray edges");
    if (!r.innerFaces.empty()) p << plural((int)r.innerFaces.size(), "face inside", "faces inside");
    if (!r.reversedFaces.empty()) p << plural((int)r.reversedFaces.size(), "reversed face", "reversed faces");
    if (!r.extraEdges.empty()) p << plural((int)r.extraEdges.size(), "edge shared by several solids", "edges shared by several solids");
    if (!r.nested.empty()) p << plural((int)r.nested.size(), "group inside", "groups inside");
    return p.join(", ");
}

Entities* SolidInspectorTool::target() {
    Model& m = doc()->model;
    if (def_ < 0 || def_ >= (int)m.defs.size() || !m.defs[def_].alive) return nullptr;
    return &m.defs[def_].ents;
}

void SolidInspectorTool::activate() {
    active_ = false;
    inst_ = def_ = -1;
    auto insts = doc()->selectedInstances();
    if (insts.size() == 1) {
        inst_ = insts[0];
        const Instance& in = doc()->active().insts[inst_];
        def_ = in.def;
        xf_ = doc()->activeXf() * in.xf;
    } else if (doc()->inContext() && doc()->sel.empty()) {
        def_ = doc()->activeDef();
        xf_ = doc()->activeXf();
    }
    if (!target()) {
        status_ = "Select one group or component (or open one) to inspect it.";
        setStatus(status_);
        return;
    }
    active_ = true;
    view->hover.clear();
    inspect();
}

void SolidInspectorTool::inspect() {
    Entities* E = target();
    if (!E) { active_ = false; return; }
    report_ = geom::inspectSolid(*E);
    QString name = doc()->model.defs[def_].name;
    if (report_.clean()) {
        status_ = QString("%1 is a solid. Esc closes the inspector.").arg(name);
    } else {
        QString what = describe(report_);
        bool fixable = !report_.holes.empty() || !report_.strayEdges.empty() || !report_.innerFaces.empty() || !report_.reversedFaces.empty();
        status_ = QString("%1 %2: %3.").arg(name, report_.solid() ? "is a solid but has" : "is not a solid", what);
        if (fixable) status_ += " Enter or right-click › Fix All repairs what can be fixed; Esc closes.";
        if (!report_.extraEdges.empty()) status_ += " Edges shared by several solids need the solids pulled apart or joined with Union.";
        if (!report_.nested.empty()) status_ += " Explode the groups inside first.";
    }
    setStatus(status_);
    redraw();
}

int SolidInspectorTool::fixAll() {
    Entities* E = target();
    if (!active_ || !E) return 0;
    doc()->begin("Fix Solid");
    int n = geom::repairSolid(*E);
    if (n) doc()->commit();
    else doc()->cancel();
    inspect();
    return n;
}

void SolidInspectorTool::mousePress(const MouseEv& e) {
    if (!active_ || e.button != Qt::RightButton) return;
    QMenu menu;
    QAction* fix = menu.addAction("Fix All", [this] { fixAll(); });
    fix->setEnabled(!report_.clean());
    menu.addAction("Close Inspector", [this] { active_ = false; toSelectTool(); });
    menu.exec(QCursor::pos());
}

bool SolidInspectorTool::keyPress(QKeyEvent* e) {
    if (!active_) return false;
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        fixAll();
        return true;
    }
    return false;
}

void SolidInspectorTool::draw(Overlay& ov) {
    Entities* Ep = target();
    if (!active_ || !Ep) return;
    const Entities& E = *Ep;
    auto P = [&](int v) { return xf_.point(E.verts[v].p); };
    auto edge = [&](int e, const QColor& c, float w, int dash = 0) {
        if (e < 0 || e >= (int)E.edges.size() || !E.edges[e].alive) return;
        ov.line(P(E.edges[e].v[0]), P(E.edges[e].v[1]), c, w, dash, true);
    };
    auto tag = [&](const Vec3& p, const QString& t, const QColor& c) { ov.label(p, t, c); };
    const QColor hole(225, 30, 30), stray(240, 140, 0), inner(30, 90, 220), rev(150, 40, 190), extra(200, 0, 140);
    for (auto& run : report_.holes) {
        for (int e : run) edge(e, hole, 4);
        if (!run.empty()) tag(xf_.point(E.edgeMid(run[0])), "Hole", hole);
    }
    for (int e : report_.strayEdges) edge(e, stray, 3);
    if (!report_.strayEdges.empty()) tag(xf_.point(E.edgeMid(report_.strayEdges[0])), "Stray edge", stray);
    for (int f : report_.innerFaces) {
        for (int e : E.faceEdges(f)) edge(e, inner, 2, 1);
        tag(xf_.point(E.faceCentroid(f)), "Face inside", inner);
    }
    for (int f : report_.reversedFaces) {
        for (int e : E.faceEdges(f)) edge(e, rev, 2);
        ov.marker(xf_.point(E.faceCentroid(f)), rev, Overlay::Square, 9);
    }
    if (!report_.reversedFaces.empty()) tag(xf_.point(E.faceCentroid(report_.reversedFaces[0])), "Reversed", rev);
    for (int e : report_.extraEdges) edge(e, extra, 3);
    if (!report_.extraEdges.empty()) tag(xf_.point(E.edgeMid(report_.extraEdges[0])), "Shared edge", extra);
}

}  // namespace gable
