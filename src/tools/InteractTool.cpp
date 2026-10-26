// Folio Gable — the Interact tool for dynamic components.
#include "core/Dynamic.h"
#include "tools/Tools.h"
#include "view/Viewport.h"
#include <QDateTime>
#include <QTimer>
#include <cmath>

namespace gable {

static const char* kIdle = "Click a dynamic component to make it act: open a door, change a colour.";

InteractTool::~InteractTool() { delete timer_; }

void InteractTool::activate() {
    hot_ = false;
    status_ = kIdle;
    setStatus(status_);
}

bool InteractTool::pathAt(double x, double y, std::vector<int>& path, int& depth) const {
    auto r = view->picker.deep(x, y);
    if (!r.face.valid()) return false;
    const InstPath& full = r.face.path;
    const InstPath& ctx = doc()->editPath;
    if (full.size() <= ctx.size() || !std::equal(ctx.begin(), ctx.end(), full.begin())) return false;
    path.assign(full.begin() + ctx.size(), full.end());
    depth = dyn::clickTarget(doc()->model, doc()->active(), path);
    return depth >= 0;
}

void InteractTool::mouseMove(const MouseEv& e) {
    if (timer_) return;
    std::vector<int> path;
    int depth = -1;
    bool hot = pathAt(e.x, e.y, path, depth);
    QString st = kIdle;
    if (hot) {
        const Entities* E = &doc()->active();
        const Instance* in = nullptr;
        for (int i = 0; i <= depth; ++i) {
            in = &E->insts[path[i]];
            E = &doc()->model.defs[in->def].ents;
        }
        QString name = in->name.isEmpty() ? doc()->model.defs[in->def].name : in->name;
        st = QString("Click to operate %1.").arg(name);
    }
    if (hot != hot_ || st != status_) {
        hot_ = hot;
        status_ = st;
        setStatus(status_);
        view->setCursor(cursor());
    }
}

void InteractTool::mousePress(const MouseEv& e) {
    if (e.button != Qt::LeftButton) return;
    if (!click(e.x, e.y)) setStatus("Nothing here reacts to clicks.");
}

bool InteractTool::click(double x, double y) {
    finish();
    std::vector<int> path;
    int depth = -1;
    if (!pathAt(x, y, path, depth)) return false;
    Document* d = doc();
    Model& m = d->model;
    std::vector<dyn::ClickChange> changes;
    QStringList errors;
    // work out the changes on a copy, so a failing onClick leaves nothing behind
    d->begin("Interact");
    dyn::makePathUnique(m, d->active(), path);
    dyn::clickChanges(m, d->active(), path, depth, changes, &errors);
    if (changes.empty()) {
        d->cancel();
        status_ = errors.isEmpty() ? QString(kIdle) : errors.first();
        setStatus(status_);
        return !errors.isEmpty();
    }
    top_ = path[0];
    anims_.clear();
    for (auto& c : changes) {
        bool okA = true, okB = true;
        double a = c.from.toNum(&okA), b = c.to.toNum(&okB);
        if (c.animate && okA && okB && !c.from.isText && !c.to.isText && animationMs > 0) {
            dyn::makePathUnique(m, d->active(), c.path);
            anims_.push_back({c.path, c.attr, a, b});
        } else {
            dyn::setAttr(m, d->active(), c.path, c.attr, c.to);
        }
    }
    if (anims_.empty()) {
        dyn::redraw(m, d->active(), top_, &errors);
        d->commit();
        d->notifyChanged();
        redraw();
        return true;
    }
    started_ = QDateTime::currentMSecsSinceEpoch();
    timer_ = new QTimer;
    timer_->setInterval(16);
    QObject::connect(timer_, &QTimer::timeout, [this] { step(); });
    timer_->start();
    step();
    return true;
}

void InteractTool::step() {
    if (!timer_) return;
    double t = std::min(1.0, (QDateTime::currentMSecsSinceEpoch() - started_) / (double)std::max(1, animationMs));
    double e = t < 0.5 ? 2 * t * t : 1 - std::pow(-2 * t + 2, 2) / 2;   // ease in and out
    Document* d = doc();
    for (auto& a : anims_) dyn::setAttr(d->model, d->active(), a.path, a.attr, dyn::Value(a.from + (a.to - a.from) * e));
    dyn::redraw(d->model, d->active(), top_);
    redraw();
    if (t >= 1.0) finish();
}

void InteractTool::finish() {
    if (!timer_) return;
    QTimer* t = timer_;
    timer_ = nullptr;
    t->stop();
    t->deleteLater();
    Document* d = doc();
    for (auto& a : anims_) dyn::setAttr(d->model, d->active(), a.path, a.attr, dyn::Value(a.to));
    dyn::redraw(d->model, d->active(), top_);
    anims_.clear();
    d->commit();
    d->notifyChanged();
    redraw();
}

}  // namespace gable
