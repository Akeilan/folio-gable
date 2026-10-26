// Folio Gable — large-model benchmark (folio-gable --bench).
// Builds three 100,000-face models (terrain, loose boxes, instanced components) and times what a
// person feels: opening and saving, frames while orbiting, hovering with Select and Line, an edit
// (push/pull), undo and redo, and Select All. Prints one line per measurement; with --bench-check
// it also fails (exit 1) when any interactive figure goes over its budget.
#include "core/Geometry.h"
#include "core/Sandbox.h"
#include "ui/MainWindow.h"
#include "view/Renderer.h"
#include "view/Viewport.h"
#include "tools/Tool.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QThread>
#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

namespace gable {

class Bencher {
public:
    explicit Bencher(MainWindow* w) : w(w), v(w->view_), d(w->doc_) {}
    int run();

private:
    MainWindow* w;
    Viewport* v;
    Document* d;
    int over = 0;
    bool checking = QCoreApplication::arguments().contains("--bench-check");

    static double ms(QElapsedTimer& t) { return t.nsecsElapsed() / 1e6; }
    void settle() { for (int i = 0; i < 3; ++i) QApplication::processEvents(QEventLoop::AllEvents, 5); }
    void report(const char* scene, const char* what, double value, double budget, const char* unit = "ms") {
        bool bad = budget > 0 && value > budget;
        if (bad) ++over;
        std::printf("%-10s %-34s %9.1f %s%s\n", scene, what, value, unit,
                    budget > 0 ? (bad ? "   OVER BUDGET" : "") : "");
        std::fflush(stdout);
    }
    double frame() {
        QElapsedTimer t;
        t.start();
        v->grabFramebuffer();
        return ms(t);
    }
    double timeIt(const std::function<void()>& f) {
        QElapsedTimer t;
        t.start();
        f();
        return ms(t);
    }
    void finish(Entities& E) {
        for (auto& F : E.faces) {
            if (!F.alive) continue;
            Vec3 n = geom::newellNormal(E, F.outer);
            if (n.length() > 1e-12) F.plane = Plane(n, E.verts[F.outer[0]].p);
        }
        E.invalidateIndex();
        E.touch();
    }

    // ---- models
    void terrain() {
        Entities& E = d->model.root;
        sandbox::makeGrid(E, Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0), 230 * 1000.0, 230 * 1000.0, 1000.0);
        for (auto& V : E.verts) V.p.z = 3000 * std::sin(V.p.x / 17000.0) * std::cos(V.p.y / 23000.0);
        finish(E);
    }
    static void box(Entities& E, const Vec3& o, double sx, double sy, double sz) {
        Vec3 c[8];
        for (int i = 0; i < 8; ++i) c[i] = o + Vec3((i & 1) ? sx : 0, (i & 2) ? sy : 0, (i & 4) ? sz : 0);
        int id[8];
        for (int i = 0; i < 8; ++i) id[i] = E.addVertex(c[i]);
        static const int quads[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
        for (auto& q : quads) {
            for (int k = 0; k < 4; ++k) E.addEdge(id[q[k]], id[q[(k + 1) % 4]]);
            std::vector<int> loop{id[q[0]], id[q[1]], id[q[2]], id[q[3]]};
            Vec3 n = (c[q[1]] - c[q[0]]).cross(c[q[2]] - c[q[0]]).normalized();
            E.addFace(loop, {}, Plane(n, c[q[0]]));
        }
    }
    void boxes() {
        Entities& E = d->model.root;
        for (int j = 0; j < 130; ++j)
            for (int i = 0; i < 130; ++i) box(E, Vec3(i * 1500.0, j * 1500.0, 0), 1000, 1000, 600 + (i * 7 + j * 13) % 9 * 200);
        finish(E);
    }
    void instances() {
        int def = d->model.addDef("Detail", false);
        Entities& C = d->model.defs[def].ents;
        for (int j = 0; j < 19; ++j)
            for (int i = 0; i < 19; ++i) box(C, Vec3(i * 300.0, j * 300.0, 0), 200, 200, 150 + (i + j) % 5 * 50);
        finish(C);
        Entities& E = d->model.root;
        for (int j = 0; j < 7; ++j)
            for (int i = 0; i < 7; ++i) {
                Instance I;
                I.def = def;
                I.xf = Mat4::translation(Vec3(i * 7000.0, j * 7000.0, 0));
                E.insts.push_back(I);
            }
        E.touch();
    }

    void measure(const char* name) {
        int faces = 0;
        for (auto& F : d->model.root.faces) faces += F.alive;
        for (auto& I : d->model.root.insts)
            for (auto& F : d->model.defs[I.def].ents.faces) faces += F.alive;
        report(name, "faces", faces, 0, "");
        v->zoomExtents();
        settle();
        report(name, "first frame (mesh build)", frame(), 0);
        report(name, "  of which mesh build (CPU)", Renderer::lastBuildMs, 400);

        // orbit: 40 frames turning the camera
        Vec3 pivot = d->model.worldBBox().center();
        if (d->model.root.faces.empty()) {
            BBox b = d->model.entitiesBBox(d->model.root);
            pivot = b.center();
        }
        std::vector<double> ft;
        for (int i = 0; i < 40; ++i) {
            v->cam.orbit(12, i % 10 == 0 ? 3 : 0, pivot);
            ft.push_back(frame());
        }
        std::sort(ft.begin(), ft.end());
        report(name, "orbit frame, median", ft[ft.size() / 2], 0);
        report(name, "orbit frame, slowest", ft.back(), 0);
        frame();
        report(name, "  render() call (CPU incl. driver)", Renderer::lastRenderMs, 0);

        // hover with Select and with Line (inference)
        v->zoomExtents();
        settle();
        for (const char* tool : {"select", "line"}) {
            v->setTool(tool);
            std::vector<double> ht;
            for (int i = 0; i < 40; ++i) {
                MouseEv m;
                m.x = v->width() * (0.2 + 0.6 * ((i * 37) % 40) / 40.0);
                m.y = v->height() * (0.2 + 0.6 * ((i * 23) % 40) / 40.0);
                v->mouseX = m.x;
                v->mouseY = m.y;
                ht.push_back(timeIt([&] { v->tool()->mouseMove(m); }));
            }
            std::sort(ht.begin(), ht.end());
            QByteArray what = QByteArray("hover with ") + tool + ", median";
            report(name, what.constData(), ht[ht.size() / 2], 16);
            what = QByteArray("hover with ") + tool + ", slowest";
            report(name, what.constData(), ht.back(), 50);
        }
        v->setTool("select");

        // the index must find exactly what a full scan finds
        {
            v->picker.prepare();
            QElapsedTimer wait;
            wait.start();
            while (wait.elapsed() < 1500) {   // picks start and then collect the background builds
                QThread::msleep(20);
                v->picker.deep(v->width() / 2, v->height() / 2);
            }
            int differ = 0;
            for (int i = 0; i < 80; ++i) {
                double x = v->width() * (0.05 + 0.9 * ((i * 53) % 80) / 80.0), y = v->height() * (0.05 + 0.9 * ((i * 29) % 80) / 80.0);
                Picker::DeepResult a = v->picker.deep(x, y);
                int keep = Picker::indexMin;
                Picker::indexMin = 1 << 30;
                Picker::DeepResult b = v->picker.deep(x, y);
                Picker::indexMin = keep;
                auto same = [](const PickHit& p, const PickHit& q) { return p.kind == q.kind && p.id == q.id && p.path == q.path; };
                if (!same(a.face, b.face) || !same(a.edge, b.edge) || !same(a.vertex, b.vertex) || a.edges.size() != b.edges.size()) ++differ;
            }
            report(name, "picks that differ from a full scan", differ, 0.5, "");
        }

        // an edit: push/pull one face        v->setTool("select");

        // an edit: push/pull one face, then the frame that shows it
        Entities& E = d->model.root;
        int f = -1;
        for (int i = (int)E.faces.size() / 2; i < (int)E.faces.size(); ++i)
            if (E.faces[i].alive && std::fabs(E.faces[i].plane.n.z) > 0.9) { f = i; break; }
        if (f >= 0) {
            report(name, "push/pull (operation)", timeIt([&] {
                d->begin("Push/Pull");
                geom::pushPull(E, f, 500, false);
                d->commit();
                d->notifyChanged();
            }), 150);
            report(name, "frame after the edit", frame(), 0);
            report(name, "  of which mesh build (CPU)", Renderer::lastBuildMs, 150);
            report(name, "undo", timeIt([&] { d->undo(); }), 150);
            report(name, "frame after undo", frame(), 0);
            report(name, "  of which mesh build (CPU)", Renderer::lastBuildMs, 150);
            report(name, "redo", timeIt([&] { d->redo(); }), 150);
            report(name, "frame after redo", frame(), 0);
        }
        // move a small part of the model (Move tool preview): pick, preview, draw, per mouse move
        if (f >= 0 && std::string(name) == "boxes") {
            d->setSelection(geom::connected(E, {EntKind::Face, f}, true));
            settle();
            v->setTool("move");
            Vec3 c = E.faceCentroid(f);
            double sx, sy;
            if (v->cam.project(c, sx, sy)) {
                MouseEv m;
                m.x = sx; m.y = sy; m.button = Qt::LeftButton;
                v->tool()->mousePress(m);
                std::vector<double> mt, ft2, bt;
                Vec3 before = E.verts[E.faces[f].outer[0]].p;
                for (int i = 1; i <= 20; ++i) {
                    MouseEv mv;
                    mv.x = sx + i * 3; mv.y = sy - i * 2;
                    mt.push_back(timeIt([&] { v->tool()->mouseMove(mv); }));
                    ft2.push_back(frame());
                    bt.push_back(Renderer::lastBuildMs);
                }
                bool moved = (E.verts[E.faces[f].outer[0]].p - before).length() > 1;
                std::sort(mt.begin(), mt.end());
                std::sort(ft2.begin(), ft2.end());
                std::sort(bt.begin(), bt.end());
                report(name, moved ? "move preview: pick, median" : "move preview DID NOT MOVE", mt[mt.size() / 2], moved ? 16 : 0.001);
                report(name, "move preview: frame, median", ft2[ft2.size() / 2], 0);
                report(name, "  of which mesh update (CPU), median", bt[bt.size() / 2], 10);
                v->tool()->reset();
            }
            v->setTool("select");
            d->clearSelection();
            settle();
        }
        report(name, "select all (command)", timeIt([&] { d->selectAll(); }), 100);
        v->setUpdatesEnabled(false);
        report(name, "select all (panels refresh)", timeIt([&] { settle(); }), 100);
        v->setUpdatesEnabled(true);
        report(name, "frame with all selected", frame(), 0);
        frame();
        report(name, "  next frame, render() call", Renderer::lastRenderMs, 0);
        d->clearSelection();
        settle();

        QString path = QDir::tempPath() + "/folio-gable-bench.gbl";
        QString err;
        report(name, "save", timeIt([&] { d->save(path, &err); }), 0);
        report(name, "open", timeIt([&] { d->load(path, &err); settle(); }), 0);
        report(name, "first frame after open", frame(), 0);
        QFile::remove(path);
    }

    void scene(const char* name, void (Bencher::*make)()) {
        d->newModel();
        settle();
        QElapsedTimer t;
        t.start();
        (this->*make)();
        report(name, "build model", ms(t), 0);
        d->notifyChanged();
        settle();
        measure(name);
    }
};

int Bencher::run() {
    w->resize(1280, 860);
    settle();
    std::printf("Folio Gable benchmark  (viewport %d x %d)\n", v->width(), v->height());
    scene("terrain", &Bencher::terrain);
    scene("boxes", &Bencher::boxes);
    scene("instances", &Bencher::instances);
    d->newModel();
    d->modified = false;
    std::printf("%d over budget\n", over);
    return checking && over ? 1 : 0;
}

int runBench(MainWindow* w) { return Bencher(w).run(); }

}  // namespace gable
