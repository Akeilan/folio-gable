// Folio Gable — "click everything" audit (folio-gable --audit).
// Drives the real window with synthesized mouse and keyboard events: draws and edits models
// the way a person would, checks the resulting geometry, then runs every command, context
// menu entry, ribbon button and File page under several selection states, validating the
// model after each step. Prints PASS/FAIL lines and exits with the number of failures.
#include "core/FileFormats.h"
#include "core/Geometry.h"
#include "core/Model.h"
#include "ui/Backstage.h"
#include "ui/MainWindow.h"
#include "ui/Panels.h"
#include "ui/Ribbon.h"
#include "ui/TitleBar.h"
#include "view/Viewport.h"
#include "tools/Tool.h"
#include "tools/Tools.h"
#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QImage>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <cstdio>
#include <functional>
#include <map>

namespace gable {

class Auditor {
public:
    explicit Auditor(MainWindow* w) : w(w), v(w->view_), d(w->doc_) {}
    int run();

private:
    MainWindow* w;
    Viewport* v;
    Document* d;
    int pass = 0, fail = 0;
    QString scenario;
    QPointF last;
    int popupIndex = -1, popupCount = 0;
    QStringList popupSeen;

    // ---------------------------------------------------------------- plumbing
    void say(const QString& s) { std::printf("%s\n", qPrintable(s)); std::fflush(stdout); }
    void check(bool c, const QString& what) {
        if (c) ++pass;
        else { ++fail; say("FAIL [" + scenario + "] " + what); }
    }
    void settle(int ms = 0) {
        QElapsedTimer t;
        t.start();
        do { QApplication::processEvents(QEventLoop::AllEvents, 5); } while (t.elapsed() < ms);
    }
    void begin(const QString& name) { scenario = name; say("-- " + name); }
    void dump(const QString& what) {
        if (!qEnvironmentVariableIsSet("AUDIT_DUMP")) return;
        say("   dump " + what);
        for (int f = 0; f < (int)E().faces.size(); ++f) {
            const Face& F = E().faces[f];
            if (!F.alive) continue;
            Vec3 c = E().faceCentroid(f);
            say(QString("     face %1 area %2 n(%3,%4,%5) c(%6,%7,%8) holes %9").arg(f).arg(E().faceArea(f), 0, 'f', 0)
                    .arg(F.plane.n.x, 0, 'f', 2).arg(F.plane.n.y, 0, 'f', 2).arg(F.plane.n.z, 0, 'f', 2)
                    .arg(c.x, 0, 'f', 0).arg(c.y, 0, 'f', 0).arg(c.z, 0, 'f', 0).arg(F.holes.size()));
        }
        say(QString("     edges %1 verts %2 insts %3").arg(edges()).arg(E().aliveCount(EntKind::Vertex)).arg(insts()));
    }

    void validate(const QString& where) {
        auto chk = [&](const Entities& E, const QString& tag) {
            auto probs = geom::validate(E, 5);
            for (auto& p : probs) check(false, where + " " + tag + ": " + QString::fromStdString(p));
            if (probs.empty()) ++pass;
        };
        chk(d->model.root, "root");
        for (size_t i = 0; i < d->model.defs.size(); ++i) chk(d->model.defs[i].ents, QString("def%1").arg(i));
    }

    void reset() {
        settle();
        d->modified = false;
        w->newFile();
        settle();
        v->stopAnimation();
        if (w->trayDock_ && !w->trayDock_->isVisible()) w->trayDock_->show();
        CameraState& c = d->model.camera;
        c.target = Vec3(1000, 750, 400);
        c.eye = c.target + Vec3(-4200, -6400, 4300);
        c.up = Vec3(0, 0, 1);
        c.perspective = true;
        c.fov = 35;
        v->cam.w = v->width();
        v->cam.h = v->height();
        v->setTool("select");
        v->update();
        settle();
    }
    QPointF scr(const Vec3& p) {
        v->cam.w = v->width();
        v->cam.h = v->height();
        double x, y;
        v->cam.project(p, x, y);
        return QPointF(x, y);
    }
    void sendMouse(QEvent::Type t, QPointF p, Qt::MouseButton b, Qt::MouseButtons bs, Qt::KeyboardModifiers m) {
        QMouseEvent e(t, p, v->mapToGlobal(p), b, bs, m);
        QApplication::sendEvent(v, &e);
        last = p;
    }
    void movePx(QPointF p, Qt::KeyboardModifiers m = {}, Qt::MouseButtons bs = Qt::NoButton) {
        QPointF a = last;
        for (int i = 1; i <= 4; ++i) sendMouse(QEvent::MouseMove, a + (p - a) * (i / 4.0), Qt::NoButton, bs, m);
        settle();
    }
    void move(const Vec3& p, Qt::KeyboardModifiers m = {}) { movePx(scr(p), m); }
    void clickPx(QPointF p, Qt::KeyboardModifiers m = {}, Qt::MouseButton b = Qt::LeftButton) {
        movePx(p, m);
        sendMouse(QEvent::MouseButtonPress, p, b, b, m);
        sendMouse(QEvent::MouseButtonRelease, p, b, Qt::NoButton, m);
        settle();
    }
    void click(const Vec3& p, Qt::KeyboardModifiers m = {}) { clickPx(scr(p), m); }
    void dbl(const Vec3& p) {
        QPointF s = scr(p);
        clickPx(s);
        sendMouse(QEvent::MouseButtonDblClick, s, Qt::LeftButton, Qt::LeftButton, {});
        sendMouse(QEvent::MouseButtonRelease, s, Qt::LeftButton, Qt::NoButton, {});
        settle();
    }
    void dragPx(QPointF a, QPointF b, Qt::KeyboardModifiers m = {}, Qt::MouseButton btn = Qt::LeftButton) {
        movePx(a, m);
        sendMouse(QEvent::MouseButtonPress, a, btn, btn, m);
        for (int i = 1; i <= 8; ++i) sendMouse(QEvent::MouseMove, a + (b - a) * (i / 8.0), Qt::NoButton, btn, m);
        sendMouse(QEvent::MouseButtonRelease, b, btn, Qt::NoButton, m);
        settle();
    }
    void key(int k, Qt::KeyboardModifiers m = {}, const QString& text = QString()) {
        QKeyEvent p(QEvent::KeyPress, k, m, text);
        QApplication::sendEvent(v, &p);
        QKeyEvent r(QEvent::KeyRelease, k, m, text);
        QApplication::sendEvent(v, &r);
        settle();
    }
    void type(const QString& s) {
        w->vcb_->setText(s);
        emit w->vcb_->returnPressed();
        settle();
    }
    void tool(const QString& id) { v->setTool(id); settle(); }
    void act(const QString& id) {
        if (!w->act_.count(id)) { check(false, "no action " + id); return; }
        w->act_[id]->trigger();
        settle();
    }
    Entities& E() { return d->active(); }
    int faces() { return E().aliveCount(EntKind::Face); }
    int edges() { return E().aliveCount(EntKind::Edge); }
    int insts() { return E().aliveCount(EntKind::Instance); }
    double vol() { return geom::volume(E()); }
    bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
    int faceAt(const Vec3& p) {
        for (int f = 0; f < (int)E().faces.size(); ++f) {
            const Face& F = E().faces[f];
            if (F.alive && std::fabs(F.plane.dist(p)) < 0.5 && E().pointInFace(f, F.plane.project(p))) return f;
        }
        return -1;
    }
    void box(double w_, double dpt, double h) {
        tool("rect");
        click(Vec3(0, 0, 0));
        move(Vec3(w_, dpt, 0));
        type(QString("%1,%2").arg(w_).arg(dpt));
        tool("pushpull");
        move(Vec3(w_ * 0.5, dpt * 0.5, 0));
        click(Vec3(w_ * 0.5, dpt * 0.5, 0));
        move(Vec3(w_ * 0.5, dpt * 0.5, h));
        type(QString::number(h));
        tool("select");
    }

    // ---------------------------------------------------------------- modal handling
    // Accept ordinary dialogs, decline file pickers, discard on "save changes?", and walk
    // popup menus entry by entry.
    void startModalCloser(QTimer& t) {
        QObject::connect(&t, &QTimer::timeout, [this] {
            if (QWidget* p = QApplication::activePopupWidget()) {
                auto* m = qobject_cast<QMenu*>(p);
                if (!m) { p->close(); return; }
                std::vector<QAction*> acts;
                std::function<void(QMenu*)> walk = [&](QMenu* mm) {
                    for (QAction* a : mm->actions()) {
                        if (a->isSeparator() || !a->isEnabled()) continue;
                        if (a->menu()) walk(a->menu());
                        else acts.push_back(a);
                    }
                };
                walk(m);
                popupCount = (int)acts.size();
                QAction* pick = (popupIndex >= 0 && popupIndex < popupCount) ? acts[popupIndex] : nullptr;
                if (pick) popupSeen << pick->text();
                m->close();
                if (pick) QTimer::singleShot(0, pick, [pick] { pick->trigger(); });
                return;
            }
            QWidget* mw = QApplication::activeModalWidget();
            if (!mw) return;
            if (auto* fd = qobject_cast<QFileDialog*>(mw)) { fd->reject(); return; }
            if (auto* mb = qobject_cast<QMessageBox*>(mw)) {
                for (auto* b : mb->buttons())
                    if (mb->buttonRole(b) == QMessageBox::DestructiveRole || mb->standardButton(b) == QMessageBox::Discard ||
                        mb->standardButton(b) == QMessageBox::No) { b->click(); return; }
                if (auto* def = mb->defaultButton()) { def->click(); return; }
                mb->reject();
                return;
            }
            if (auto* dlg = qobject_cast<QDialog*>(mw)) {
                if (dlg->property("auditSeen").toBool()) { dlg->reject(); return; }
                dlg->setProperty("auditSeen", true);
                QTimer::singleShot(30, dlg, [dlg] { dlg->accept(); });
            }
        });
        t.start(40);
    }

    // ---------------------------------------------------------------- scenarios
    void sRectPush() {
        begin("rectangle + push/pull");
        reset();
        tool("rect");
        click(Vec3(0, 0, 0));
        move(Vec3(2000, 1500, 0));
        type("2000,1500");
        check(faces() == 1, QString("rectangle makes one face (got %1)").arg(faces()));
        int f = faceAt(Vec3(1000, 750, 0));
        check(f >= 0 && near(E().faceArea(f), 3e6, 1), "rectangle area 2000x1500");
        tool("pushpull");
        move(Vec3(1000, 750, 0));
        click(Vec3(1000, 750, 0));
        move(Vec3(1000, 750, 700));
        type("1000");
        check(faces() == 6, QString("box has 6 faces (got %1)").arg(faces()));
        check(near(vol(), 3e9, 1e3), QString("box volume (got %1)").arg(vol()));
        check(geom::isSolid(E()), "box is solid");
        validate("box");
    }

    void sPocketAndHole() {
        begin("offset, pocket, hole");
        reset();
        box(2000, 1500, 1000);
        // like the reference app: start near an edge and move inward for an inner offset
        tool("offset");
        move(Vec3(1000, 100, 1000));
        click(Vec3(1000, 100, 1000));
        move(Vec3(1000, 400, 1000));
        type("200");
        dump("after offset");
        check(faces() == 7, QString("offset adds a face (got %1)").arg(faces()));
        tool("pushpull");
        move(Vec3(1000, 750, 1000));
        click(Vec3(1000, 750, 1000));
        move(Vec3(1000, 750, 850));
        type("300");
        double expect = 3e9 - 1600.0 * 1100 * 300;
        check(near(vol(), expect, 1e4), QString("pocket volume %1 (want %2)").arg(vol()).arg(expect));
        // a typed depth goes the way the mouse went, even when the cursor is over the pushed face
        tool("circle");
        move(Vec3(1000, 0, 500));
        click(Vec3(1000, 0, 500));
        move(Vec3(1250, 0, 500));
        type("250");
        dump("after circle");
        int fBefore = faces();
        check(fBefore == 12, QString("circle on the front face (faces %1)").arg(fBefore));
        tool("pushpull");
        move(Vec3(1000, 0, 500));
        click(Vec3(1000, 0, 500));
        move(Vec3(1000, 700, 500));
        if (qEnvironmentVariableIsSet("AUDIT_DUMP")) say("   vcb before typing: " + w->vcb_->text());
        type("1500");
        check(geom::isSolid(E()), "hole leaves a solid");
        check(vol() < expect - 2.5e8, QString("hole removed volume (vol %1)").arg(vol()));
        validate("hole");
    }

    void sLines() {
        begin("lines join and split");
        reset();
        box(2000, 1500, 1000);
        tool("line");
        click(Vec3(1000, 0, 1000));     // midpoint of the front top edge
        click(Vec3(1000, 1500, 1000));  // midpoint of the back top edge
        key(Qt::Key_Escape);
        check(faces() == 7, QString("line across the top splits it (faces %1)").arg(faces()));
        validate("split top");
        // closed loop on the ground by typed lengths along the axes
        reset();
        tool("line");
        click(Vec3(0, 0, 0));
        move(Vec3(800, 0, 0));
        type("1000");
        move(Vec3(1000, 700, 0));
        type("800");
        move(Vec3(300, 800, 0));
        type("1000");
        click(Vec3(0, 0, 0));
        check(faces() == 1, QString("typed rectangle of lines closes (faces %1)").arg(faces()));
        int f = faceAt(Vec3(500, 400, 0));
        check(f >= 0 && near(E().faceArea(f), 8e5, 1), "loop area 1000x800");
        check(!v->tool()->busy(), "line chain ends after closing a face");
        // a line from an edge's midpoint to the opposite corner
        tool("line");
        click(Vec3(500, 0, 0));
        click(Vec3(1000, 800, 0));
        key(Qt::Key_Escape);
        check(faces() == 2, QString("diagonal from midpoint splits (faces %1)").arg(faces()));
        validate("lines");
    }

    void sMoveEdge() {
        begin("move an edge up (arrow lock)");
        reset();
        box(2000, 1500, 1000);
        tool("select");
        click(Vec3(1000, 0, 1000));
        check(d->sel.size() == 1 && d->sel[0].kind == EntKind::Edge, "clicking an edge selects it");
        tool("move");
        click(Vec3(1000, 0, 1000));
        key(Qt::Key_Up);
        move(Vec3(1000, 0, 1300));
        type("300");
        bool up = false;
        for (auto& vx : E().verts) if (vx.alive && near(vx.p.z, 1300, 0.01)) up = true;
        check(up, "edge moved 300 up");
        validate("move edge");
    }

    void sGroupArray() {
        begin("group, copy array, rotate");
        reset();
        box(1000, 600, 400);
        act("selectall");
        act("makegroup");
        check(insts() == 1 && faces() == 0, "make group");
        tool("move");
        click(Vec3(1000, 0, 0));
        key(Qt::Key_Control);
        move(Vec3(1800, 0, 0));
        type("1500");
        type("x3");
        check(insts() == 4, QString("copy array x3 (instances %1)").arg(insts()));
        BBox b = d->model.worldBBox();
        check(near(b.hi.x, 1000 + 3 * 1500, 1), QString("array spacing (max x %1)").arg(b.hi.x));
        validate("array");
        // rotate the first copy 90° about its corner
        d->setSelection({{EntKind::Instance, 0}});
        tool("rotate");
        click(Vec3(0, 0, 0));
        click(Vec3(500, 0, 0));
        type("90");
        Vec3 xa = E().insts[0].xf.axis(0);
        check(near(xa.y, 1, 1e-6) || near(xa.y, -1, 1e-6), QString("rotated 90 (x axis %1,%2)").arg(xa.x).arg(xa.y));
        act("undo");
        xa = E().insts[0].xf.axis(0);
        check(near(xa.x, 1, 1e-6), "undo rotation");
        act("redo");
        validate("rotate");
    }

    void sScale() {
        begin("scale a group with a grip");
        reset();
        box(1000, 1000, 1000);
        act("selectall");
        act("makegroup");
        tool("scale");
        // opposite-corner grip: the far top corner
        Vec3 g(1000, 1000, 1000);
        move(g);
        click(g);
        move(Vec3(1500, 1500, 1500));
        type("2");
        BBox b = d->model.worldBBox();
        check(near(b.hi.x - b.lo.x, 2000, 1), QString("uniform scale x2 (size %1)").arg(b.hi.x - b.lo.x));
        validate("scale");
    }

    void sEraserPaint() {
        begin("eraser and paint bucket");
        reset();
        box(2000, 1500, 1000);
        tool("line");
        click(Vec3(1000, 0, 1000));
        click(Vec3(1000, 1500, 1000));
        key(Qt::Key_Escape);
        int f7 = faces();
        tool("eraser");
        click(Vec3(1000, 750, 1000));
        check(faces() == f7 - 1 && faces() == 6, QString("erasing the split line merges (faces %1)").arg(faces()));
        Material m;
        m.name = "Brick";
        m.color = QColor(180, 60, 40);
        d->model.materials.push_back(m);
        w->materials_->refresh();
        w->materials_->setCurrent((int)d->model.materials.size() - 1);
        tool("paint");
        click(Vec3(1000, 750, 1000));
        int top = faceAt(Vec3(1000, 750, 1000));
        check(top >= 0 && E().faces[top].matFront == (int)d->model.materials.size() - 1, "paint the top face");
        validate("erase/paint");
    }

    void sConstruction() {
        begin("tape, dimension, text, section, protractor, axes");
        reset();
        box(2000, 1500, 1000);
        tool("tape");
        click(Vec3(1000, 0, 1000));
        move(Vec3(1000, 0, 700));
        type("250");
        check(E().aliveCount(EntKind::Guide) == 1, "tape makes a guide");
        tool("dimension");
        click(Vec3(0, 0, 1000));
        click(Vec3(2000, 0, 1000));
        click(Vec3(1000, -400, 1000));
        check(E().aliveCount(EntKind::Dimension) == 1, "dimension placed");
        tool("text");
        click(Vec3(2000, 0, 0));
        click(Vec3(2400, -400, 300));
        settle(150);
        check(E().aliveCount(EntKind::Text) == 1, "text placed");
        tool("section");
        click(Vec3(1000, 750, 1000));
        settle(50);
        check(E().aliveCount(EntKind::Section) == 1, "section plane placed");
        tool("protractor");
        click(Vec3(0, 0, 0));
        click(Vec3(500, 0, 0));
        type("30");
        check(E().aliveCount(EntKind::Guide) == 2, "protractor guide");
        tool("axes");
        click(Vec3(3000, -1000, 0));
        click(Vec3(4000, -1000, 0));
        click(Vec3(3000, 0, 0));
        check(near(d->model.axesOrigin.x, 3000, 1), QString("axes moved (origin %1,%2)").arg(d->model.axesOrigin.x).arg(d->model.axesOrigin.y));
        act("undo");
        validate("construction");
    }

    void sShapes() {
        begin("arcs, polygon, freehand, rotated rectangle, pie");
        reset();
        tool("polygon");
        click(Vec3(0, 0, 0));
        move(Vec3(500, 0, 0));
        type("500");
        check(faces() == 1, "polygon face");
        tool("arc2");
        click(Vec3(1000, 0, 0));
        click(Vec3(2000, 0, 0));
        click(Vec3(1500, 400, 0));
        int e1 = edges();
        check(e1 > 8, "2-point arc edges");
        tool("line");
        click(Vec3(1000, 0, 0));
        click(Vec3(2000, 0, 0));
        key(Qt::Key_Escape);
        check(faces() == 2, QString("arc closed by a line makes a face (faces %1)").arg(faces()));
        tool("arc");
        click(Vec3(3000, 0, 0));
        click(Vec3(3500, 0, 0));
        click(Vec3(3000, 500, 0));
        tool("arc3");
        click(Vec3(4000, 0, 0));
        click(Vec3(4300, 300, 0));
        click(Vec3(4600, 0, 0));
        tool("pie");
        click(Vec3(5500, 0, 0));
        click(Vec3(6000, 0, 0));
        click(Vec3(5500, 500, 0));
        check(faces() >= 3, QString("pie face (faces %1)").arg(faces()));
        tool("rotrect");
        click(Vec3(0, 1500, 0));
        click(Vec3(600, 1900, 0));
        click(Vec3(400, 2300, 0));
        check(faces() >= 4, QString("rotated rectangle face (faces %1)").arg(faces()));
        tool("freehand");
        dragPx(scr(Vec3(2000, 2000, 0)), scr(Vec3(2600, 2300, 0)));
        validate("shapes");
    }

    void sSelect() {
        begin("select: click, double, triple, window, crossing");
        reset();
        box(2000, 1500, 1000);
        tool("select");
        click(Vec3(1000, 750, 1000));
        check(d->sel.size() == 1 && d->sel[0].kind == EntKind::Face, "click selects a face");
        dbl(Vec3(1000, 750, 1000));
        check(d->sel.size() == 5, QString("double-click adds its edges (sel %1)").arg(d->sel.size()));
        // triple click: third click soon after the double click
        clickPx(scr(Vec3(1000, 750, 1000)));
        check(d->sel.size() == 18, QString("triple-click selects all connected (sel %1)").arg(d->sel.size()));
        clickPx(QPointF(5, 5));
        check(d->sel.empty(), "click on empty space clears");
        // window (left to right) fully around the box vs crossing (right to left) through it
        QPointF a(5, 5), b(v->width() - 5, v->height() - 5);
        dragPx(a, b);
        check(d->sel.size() == 18, QString("window selection (sel %1)").arg(d->sel.size()));
        clickPx(QPointF(5, 5));
        QPointF c = scr(Vec3(1000, 750, 1000));
        dragPx(c + QPointF(40, 40), c - QPointF(40, 40));
        check(!d->sel.empty(), QString("crossing selection picks what it touches (sel %1)").arg(d->sel.size()));
        click(Vec3(1000, 0, 500), Qt::ShiftModifier);
        validate("select");
    }

    void sFollowMe() {
        begin("follow me");
        reset();
        // path: rectangle on the ground; profile: small vertical rectangle at one corner
        tool("rect");
        click(Vec3(0, 0, 0));
        move(Vec3(1000, 1000, 0));
        type("1000,1000");
        Entities& EE = E();
        geom::addPolyline(EE, {{0, 0, 0}, {0, 0, 200}, {0, -100, 200}, {0, -100, 0}}, true);
        d->notifyChanged();
        int ground = faceAt(Vec3(500, 500, 0));
        check(ground >= 0, "ground face");
        std::vector<EntRef> path;
        for (int e : EE.faceEdges(ground)) path.push_back({EntKind::Edge, e});
        d->setSelection(path);
        tool("followme");
        int prof = faceAt(Vec3(0, -50, 100));
        check(prof >= 0, "profile face exists");
        click(Vec3(0, -50, 100));
        dump("after follow me");
        check(faces() > 8, QString("follow me sweeps the profile (faces %1)").arg(faces()));
        validate("follow me");
    }

    void sClipboardUndo() {
        begin("copy/paste, undo/redo, hide/unhide, components");
        reset();
        box(1000, 1000, 1000);
        int f = faces();
        act("selectall");
        act("copy");
        act("paste");
        move(Vec3(2500, 0, 0));
        click(Vec3(2500, 0, 0));
        check(faces() + insts() > f, QString("paste adds geometry (faces %1, insts %2)").arg(faces()).arg(insts()));
        act("undo");
        check(faces() == f, "undo paste");
        act("redo");
        act("undo");
        act("selectall");
        act("makecomponent");
        settle(100);
        check(insts() == 1, QString("make component (insts %1)").arg(insts()));
        d->setSelection({{EntKind::Instance, 0}});
        act("hide");
        check(E().insts[0].hidden, "hide");
        act("unhideall");
        check(!E().insts[0].hidden, "unhide all");
        d->openInstance(0);
        settle();
        check(d->inContext(), "edit component");
        check(w->groupTab_ && w->groupTab_->visible, "Group Editing tab shows while editing");
        act("closegroup");
        check(!d->inContext(), "close component");
        check(!w->groupTab_->visible, "Group Editing tab hides again");
        d->setSelection({{EntKind::Instance, 0}});
        act("explode");
        check(faces() == 6, QString("explode (faces %1)").arg(faces()));
        validate("clipboard");
    }

    void sSolids() {
        begin("solid tools and intersect");
        reset();
        box(1000, 1000, 1000);
        act("selectall");
        act("makegroup");
        tool("rect");
        click(Vec3(500, -500, 0));
        move(Vec3(1500, 500, 0));
        type("1000,1000");
        tool("pushpull");
        click(Vec3(1000, -300, 0));
        move(Vec3(1000, -300, 500));
        type("1000");
        // the second box overlaps the first group; group it too
        std::vector<EntRef> s;
        for (int i = 0; i < (int)E().faces.size(); ++i) if (E().faces[i].alive) s.push_back({EntKind::Face, i});
        for (int i = 0; i < (int)E().edges.size(); ++i) if (E().edges[i].alive) s.push_back({EntKind::Edge, i});
        d->setSelection(s);
        act("makegroup");
        check(insts() == 2, "two solid groups");
        for (auto& in : E().insts)
            if (in.alive) say(QString("   solid volume %1").arg(geom::volume(d->model.defs[in.def].ents)));
        act("selectall");
        act("solid_union");
        check(insts() == 1, QString("union makes one solid (insts %1)").arg(insts()));
        if (insts() == 1) {
            int def = -1;
            for (auto& in : E().insts) if (in.alive) def = in.def;
            double vv = def >= 0 ? geom::volume(d->model.defs[def].ents) : 0;
            check(near(vv, 2e9 - 0.25e9, 1e5), QString("union volume %1").arg(vv));
        }
        validate("solids");
        reset();
        box(1000, 1000, 1000);
        geom::addPolyline(E(), {{500, -500, 300}, {1500, -500, 300}, {1500, 500, 300}, {500, 500, 300}}, true);
        d->notifyChanged();
        int e0 = edges();
        act("selectall");
        act("intersectmodel");
        check(edges() > e0, QString("intersect adds edges (%1 -> %2)").arg(e0).arg(edges()));
        validate("intersect");
    }

    void sTerrain() {
        begin("sandbox terrain");
        reset();
        tool("sbscratch");
        click(Vec3(0, 0, 0));
        click(Vec3(3000, 0, 0));
        click(Vec3(3000, 3000, 0));
        settle(50);
        int f = 0;
        for (auto& in : E().insts) if (in.alive) f += d->model.defs[in.def].ents.aliveCount(EntKind::Face);
        check(insts() == 1 && f > 10, QString("terrain grid group (insts %1, faces %2)").arg(insts()).arg(f));
        validate("terrain");
    }

    void sCamera() {
        begin("camera tools and views");
        reset();
        box(2000, 1500, 1000);
        CameraState c0 = d->model.camera;
        tool("orbit");
        dragPx(scr(Vec3(1000, 750, 500)), scr(Vec3(1000, 750, 500)) + QPointF(120, 30));
        check(!(d->model.camera.eye - c0.eye).isZero(1), "orbit moves the camera");
        tool("pan");
        dragPx(QPointF(300, 300), QPointF(400, 330));
        tool("zoom");
        dragPx(QPointF(300, 300), QPointF(300, 250));
        act("zoomextents");
        for (auto id : {"view_top", "view_front", "view_right", "view_back", "view_left", "view_bottom", "view_iso"}) act(id);
        act("parallel");
        check(!d->model.camera.perspective, "parallel projection");
        act("perspective");
        check(d->model.camera.perspective, "perspective");
        tool("zoomwindow");
        dragPx(QPointF(200, 200), QPointF(500, 400));
        act("prevview");
        tool("poscam");
        click(Vec3(-1000, -1000, 0));
        tool("lookaround");
        dragPx(QPointF(300, 300), QPointF(340, 300));
        tool("walk");
        dragPx(QPointF(300, 300), QPointF(300, 280));
        tool("select");
        // middle-button orbit and wheel zoom
        dragPx(QPointF(400, 300), QPointF(450, 320), {}, Qt::MiddleButton);
        QWheelEvent we(QPointF(400, 300), v->mapToGlobal(QPointF(400, 300)), QPoint(), QPoint(0, 120), Qt::NoButton, {},
                       Qt::NoScrollPhase, false);
        QApplication::sendEvent(v, &we);
        settle();
        check(std::isfinite(d->model.camera.eye.x) && std::isfinite(d->model.camera.target.x), "camera stays finite");
        validate("camera");
    }

    void sFiles() {
        begin("save, reopen, export");
        reset();
        box(2000, 1500, 1000);
        QString dir = QDir::tempPath() + "/folio-gable-audit";
        QDir().mkpath(dir);
        QString err;
        check(d->save(dir + "/a.gbl", &err), "save .gbl " + err);
        int f = faces();
        w->newFile();
        check(w->openFile(dir + "/a.gbl"), "reopen");
        check(faces() == f, "reopened faces");
        for (auto ext : {"stl", "obj", "dae", "dxf"}) {
            ExportOptions eo;
            check(exportFile(d->model, dir + "/a." + ext, eo, &err), QString("export %1 %2").arg(ext, err));
        }
        validate("files");
    }

    void sNewFeatures() {
        begin("centred rectangle, intersection inference, image, edge length");
        reset();
        tool("rect");
        key(Qt::Key_Control);
        click(Vec3(0, 0, 0));
        move(Vec3(500, 400, 0));
        type("1000,800");
        int f = faceAt(Vec3(0, 0, 0));
        check(f >= 0 && near(E().faceArea(f), 8e5, 1), "Ctrl rectangle is centred on the first click");
        BBox b = E().bbox();
        check(near(b.lo.x, -500, 0.01) && near(b.hi.y, 400, 0.01), QString("centred extents (%1..%2)").arg(b.lo.x).arg(b.hi.x));
        // two crossing lines: a third line ending at the crossing joins both
        reset();
        geom::addLine(E(), Vec3(0, 0, 0), Vec3(2000, 2000, 0));
        geom::addLine(E(), Vec3(0, 2000, 0), Vec3(2000, 0, 0));
        d->notifyChanged();
        int e0 = edges();
        tool("line");
        click(Vec3(1000, -1000, 0));
        click(Vec3(1000, 1000, 0));
        key(Qt::Key_Escape);
        check(edges() == e0 + 1, QString("line to the crossing (edges %1 -> %2)").arg(e0).arg(edges()));
        // image placement
        reset();
        QImage img(200, 100, QImage::Format_RGB32);
        img.fill(Qt::red);
        QString ip = QDir::tempPath() + "/folio-gable-audit-img.png";
        img.save(ip);
        auto* it = static_cast<ImageTool*>(v->toolById("image"));
        QString err;
        check(it->setImage(ip, &err), "load picture " + err);
        tool("image");
        click(Vec3(0, 0, 0));
        move(Vec3(1500, 0, 0));
        type("2000");
        check(insts() == 1, "picture placed as a group");
        BBox ib = d->model.worldBBox();
        check(near(ib.hi.x - ib.lo.x, 2000, 0.5) && near(ib.hi.y - ib.lo.y, 1000, 0.5), "picture keeps its proportions");
        // Entity Info: change an edge's length
        reset();
        tool("line");
        click(Vec3(0, 0, 0));
        move(Vec3(800, 0, 0));
        type("1000");
        key(Qt::Key_Escape);
        for (int i = 0; i < (int)E().edges.size(); ++i)
            if (E().edges[i].alive) d->setSelection({{EntKind::Edge, i}});
        settle(50);
        bool found = false;
        auto* info = w->findChild<EntityInfoPanel*>();
        if (info) info->refresh();
        settle(20);
        if (info && qEnvironmentVariableIsSet("AUDIT_DUMP"))
            for (auto* le : info->findChildren<QLineEdit*>()) say("   info field: '" + le->text() + "' visible " + QString::number(le->isVisible()));
        QLineEdit* target = nullptr;
        // the panel's newest widgets come last (older ones may still await deletion)
        for (auto* le : info ? info->findChildren<QLineEdit*>() : QList<QLineEdit*>())
            if (le->isVisible() && le->text() == formatLength(1000, d->model.units)) target = le;
        if (target) {
            target->setText("1500");
            emit target->editingFinished();
            found = true;
        }
        settle(50);
        int le0 = -1;
        for (int i = 0; i < (int)E().edges.size(); ++i) if (E().edges[i].alive) le0 = i;
        check(found && E().aliveCount(EntKind::Edge) == 1 && le0 >= 0 && near(E().edgeLength(le0), 1500, 0.01),
              QString("edge length typed in Entity Info (found %1, edges %2, length %3)").arg(found).arg(E().aliveCount(EntKind::Edge))
                  .arg(le0 >= 0 ? E().edgeLength(le0) : -1));
        validate("new features");
    }

    // ---------------------------------------------------------------- click everything
    void quickBox(double a) {
        Entities& EE = E();
        geom::addPolyline(EE, {{0, 0, 0}, {a, 0, 0}, {a, a, 0}, {0, a, 0}}, true);
        int f = faceAt(Vec3(a / 2, a / 2, 0));
        if (f >= 0) geom::pushPull(EE, f, EE.faces[f].plane.n.z < 0 ? -a : a, false);
        EE.touch();
        d->notifyChanged();
    }
    void prepareState(int s) {
        reset();
        quickBox(1000);
        if (s == 1) d->setSelection({{EntKind::Face, faceAt(Vec3(500, 500, 1000))}});
        if (s == 2) { act("selectall"); act("makegroup"); d->setSelection({{EntKind::Instance, 0}}); }
        if (s == 3) { act("selectall"); act("makegroup"); d->openInstance(0); d->selectAll(); }
        if (s == 4) { act("selectall"); }
        settle();
    }

    void sAllActions() {
        static const QSet<QString> skip = {"quit", "playscenes", "leavefullscreen", "ribboncollapse", "search"};
        const char* states[] = {"empty selection", "a face", "a group", "inside a group", "everything"};
        for (int s = 0; s < 5; ++s) {
            begin(QString("every command with %1").arg(states[s]));
            std::vector<QString> ids;
            for (auto& [id, a] : w->act_) if (!skip.contains(id)) ids.push_back(id);
            for (auto& id : ids) {
                prepareState(s);
                if (id.startsWith("tool_")) {
                    w->act_[id]->trigger();
                    settle();
                    // exercise the tool briefly: a few clicks on the model, then Esc
                    click(Vec3(500, 500, 1000));
                    move(Vec3(700, 600, 1000));
                    click(Vec3(700, 600, 1000));
                    key(Qt::Key_Escape);
                } else {
                    w->act_[id]->trigger();
                    settle(20);
                    if (w->act_[id]->isCheckable() && w->act_[id]->isChecked()) { w->act_[id]->trigger(); settle(); }
                }
                validate(id);
                if (w->backstage_->isOpen()) w->backstage_->close();
            }
        }
    }

    void sContextMenus() {
        for (int s = 0; s < 5; ++s) {
            begin(QString("context menu, state %1").arg(s));
            for (int i = 0; i < 60; ++i) {
                prepareState(s);
                popupIndex = i;
                popupCount = 0;
                w->contextMenu(v->mapToGlobal(QPoint(300, 300)));
                settle(60);
                validate(QString("context item %1").arg(i));
                if (popupCount <= i + 1) break;
            }
        }
        popupIndex = -1;
    }

    void sRibbonButtons() {
        begin("every ribbon button");
        for (auto* t : w->ribbon_->tabs()) {
            w->ribbon_->setTabVisible(t, true);
            w->ribbon_->select(t);
            settle();
            for (auto* g : t->groups)
                for (auto* b : g->buttons) {
                    prepareState(4);
                    w->ribbon_->select(t);
                    if (!b->isEnabled()) continue;
                    if (b->menu()) {
                        popupIndex = 0;
                        b->showMenu();
                        settle(60);
                        popupIndex = -1;
                    } else b->click();
                    settle(20);
                    key(Qt::Key_Escape);
                    validate(t->name + "/" + b->label());
                }
        }
        w->ribbon_->setTabVisible(w->groupTab_, d->inContext());
    }

    void sChrome() {
        begin("File view, ribbon modes, KeyTips, search");
        reset();
        for (auto p : {"home", "new", "open", "info", "saveas", "print", "export"}) {
            w->showBackstage(p);
            settle(30);
            check(w->backstage_->isOpen(), QString("File view page %1").arg(p));
            w->backstage_->close();
        }
        w->newFromTemplate(4);
        check(d->model.units.unit == LengthUnit::ArchitecturalInch, "architectural template units");
        w->ribbon_->setMode(Ribbon::TabsOnly);
        settle(30);
        w->ribbon_->select(w->ribbon_->tabs()[1]);
        settle(30);
        check(w->ribbon_->body()->isVisible(), "tabs-only: a tab shows the commands");
        clickPx(QPointF(400, 400));
        check(!w->ribbon_->body()->isVisible(), "tabs-only: clicking the model hides them");
        w->ribbon_->setMode(Ribbon::AlwaysShow);
        settle(30);
        check(w->ribbon_->body()->isVisible(), "always show");
        // narrow window: groups fold
        QSize old = w->size();
        w->resize(700, 600);
        settle(150);
        int folded = 0;
        for (auto* g : w->ribbon_->current()->groups) folded += g->collapsed();
        check(folded > 0, QString("narrow window folds groups (%1)").arg(folded));
        w->resize(old);
        settle(150);
        // KeyTips: Alt, then H, then L (Line)
        reset();
        QKeyEvent ap(QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier);
        QApplication::sendEvent(v, &ap);
        QKeyEvent ar(QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier);
        QApplication::sendEvent(v, &ar);
        settle(20);
        check(w->keytips_->active(), "Alt shows KeyTips");
        key(Qt::Key_H, {}, "h");
        key(Qt::Key_L, {}, "l");
        settle(30);
        check(v->tool() && v->tool()->id() == "line", QString("Alt, H, L picks the Line tool (%1)").arg(v->tool() ? v->tool()->id() : "none"));
        w->keytips_->stop();
        // search
        w->titleBar_->search()->setText("Push/Pull");
        emit w->titleBar_->search()->returnPressed();
        settle();
        check(v->tool() && v->tool()->id() == "pushpull", "search runs Push/Pull");
    }
};

int Auditor::run() {
    QTimer closer;
    startModalCloser(closer);
    settle(300);
    QString only = qEnvironmentVariable("AUDIT_ONLY");
    if (!only.isEmpty()) {
        std::map<QString, std::function<void()>> m = {
            {"push", [this] { sRectPush(); }}, {"pocket", [this] { sPocketAndHole(); }}, {"lines", [this] { sLines(); }},
            {"followme", [this] { sFollowMe(); }}, {"solids", [this] { sSolids(); }}, {"terrain", [this] { sTerrain(); }},
            {"construct", [this] { sConstruction(); }}, {"select", [this] { sSelect(); }}, {"shapes", [this] { sShapes(); }},
            {"chrome", [this] { sChrome(); }}, {"actions", [this] { sAllActions(); }}, {"context", [this] { sContextMenus(); }},
            {"ribbon", [this] { sRibbonButtons(); }}, {"group", [this] { sGroupArray(); }}, {"scale", [this] { sScale(); }},
            {"clip", [this] { sClipboardUndo(); }}, {"camera", [this] { sCamera(); }}, {"files", [this] { sFiles(); }},
            {"erase", [this] { sEraserPaint(); }}, {"move", [this] { sMoveEdge(); }}, {"new", [this] { sNewFeatures(); }}};
        for (const QString& k : only.split(','))
            if (m.count(k)) m[k]();
        say(QString("\n%1 passed, %2 failed").arg(pass).arg(fail));
        d->modified = false;
        return fail;
    }
    sRectPush();
    sPocketAndHole();
    sLines();
    sMoveEdge();
    sGroupArray();
    sScale();
    sEraserPaint();
    sConstruction();
    sShapes();
    sSelect();
    sFollowMe();
    sClipboardUndo();
    sSolids();
    sTerrain();
    sCamera();
    sFiles();
    sChrome();
    sNewFeatures();
    if (!qEnvironmentVariableIsSet("AUDIT_QUICK")) {
        sAllActions();
        sContextMenus();
        sRibbonButtons();
    }
    say(QString("\n%1 passed, %2 failed").arg(pass).arg(fail));
    d->modified = false;
    return fail;
}

int runAudit(MainWindow* w) {
    Auditor a(w);
    return a.run();
}

}  // namespace gable
