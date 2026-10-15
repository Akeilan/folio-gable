#include "ui/Document.h"
#include "core/Geometry.h"
#include "core/Serializer.h"
#include <QFile>
#include <QSaveFile>

namespace gable {

Document::Document(QObject* parent) : QObject(parent) {}

Entities& Document::entitiesAt(const InstPath& path) {
    Entities* e = &model.root;
    for (int i : path) {
        if (i < 0 || i >= (int)e->insts.size() || !e->insts[i].alive) break;
        e = &model.defs[e->insts[i].def].ents;
    }
    return *e;
}

Mat4 Document::xfAt(const InstPath& path) const {
    const Entities* e = &model.root;
    Mat4 m;
    for (int i : path) {
        if (i < 0 || i >= (int)e->insts.size()) break;
        m = m * e->insts[i].xf;
        e = &model.defs[e->insts[i].def].ents;
    }
    return m;
}

Entities& Document::active() { return entitiesAt(editPath); }
const Entities& Document::active() const { return const_cast<Document*>(this)->entitiesAt(editPath); }
Mat4 Document::activeXf() const { return xfAt(editPath); }

int Document::activeDef() const {
    if (editPath.empty()) return -1;
    const Entities* e = &model.root;
    int def = -1;
    for (int i : editPath) {
        def = e->insts[i].def;
        e = &model.defs[def].ents;
    }
    return def;
}

void Document::openInstance(int inst) {
    Entities& a = active();
    if (inst < 0 || inst >= (int)a.insts.size() || !a.insts[inst].alive || a.insts[inst].locked) return;
    int def = a.insts[inst].def;
    if (model.defs[def].isGroup && model.instanceCount(def) > 1) makeUnique(inst);
    editPath.push_back(inst);
    clearSelection();
    emit contextChanged();
    emit viewChanged();
}

void Document::closeContext() {
    if (editPath.empty()) return;
    int inst = editPath.back();
    editPath.pop_back();
    setSelection({{EntKind::Instance, inst}});
    emit contextChanged();
    emit viewChanged();
}

void Document::closeAllContexts() {
    if (editPath.empty()) return;
    editPath.clear();
    clearSelection();
    emit contextChanged();
    emit viewChanged();
}

// ---------------------------------------------------------------- selection

void Document::setSelection(const std::vector<EntRef>& s) {
    sel.clear();
    selSet_.clear();
    for (auto& r : s)
        if (selSet_.insert(r).second) sel.push_back(r);
    emit selectionChanged();
}

void Document::addToSelection(const std::vector<EntRef>& s) {
    for (auto& r : s)
        if (selSet_.insert(r).second) sel.push_back(r);
    emit selectionChanged();
}

void Document::removeFromSelection(const std::vector<EntRef>& s) {
    for (auto& r : s) selSet_.erase(r);
    sel.erase(std::remove_if(sel.begin(), sel.end(), [&](const EntRef& r) { return !selSet_.count(r); }), sel.end());
    emit selectionChanged();
}

void Document::toggleSelection(const std::vector<EntRef>& s) {
    // If all are selected, remove; otherwise add the missing ones.
    bool all = true;
    for (auto& r : s)
        if (!selSet_.count(r)) { all = false; break; }
    if (all) removeFromSelection(s);
    else addToSelection(s);
}

void Document::clearSelection() {
    if (sel.empty()) return;
    sel.clear();
    selSet_.clear();
    emit selectionChanged();
}

void Document::selectAll() {
    Entities& E = active();
    std::vector<EntRef> s;
    for (int i = 0; i < (int)E.edges.size(); ++i)
        if (E.edges[i].alive && (!E.edges[i].hidden || model.style.showHidden) && tagVisible(E.edges[i].tag)) s.push_back({EntKind::Edge, i});
    for (int i = 0; i < (int)E.faces.size(); ++i)
        if (E.faces[i].alive && (!E.faces[i].hidden || model.style.showHidden) && tagVisible(E.faces[i].tag)) s.push_back({EntKind::Face, i});
    for (int i = 0; i < (int)E.insts.size(); ++i)
        if (E.insts[i].alive && !E.insts[i].hidden && tagVisible(E.insts[i].tag)) s.push_back({EntKind::Instance, i});
    for (int i = 0; i < (int)E.guides.size(); ++i) if (E.guides[i].alive && !E.guides[i].hidden) s.push_back({EntKind::Guide, i});
    for (int i = 0; i < (int)E.dims.size(); ++i) if (E.dims[i].alive && !E.dims[i].hidden) s.push_back({EntKind::Dimension, i});
    for (int i = 0; i < (int)E.texts.size(); ++i) if (E.texts[i].alive && !E.texts[i].hidden) s.push_back({EntKind::Text, i});
    for (int i = 0; i < (int)E.sections.size(); ++i) if (E.sections[i].alive) s.push_back({EntKind::Section, i});
    setSelection(s);
}

void Document::pruneSelection() {
    Entities& E = active();
    std::vector<EntRef> s;
    for (auto& r : sel) {
        bool ok = false;
        switch (r.kind) {
        case EntKind::Edge: ok = r.id < (int)E.edges.size() && E.edges[r.id].alive; break;
        case EntKind::Face: ok = r.id < (int)E.faces.size() && E.faces[r.id].alive; break;
        case EntKind::Instance: ok = r.id < (int)E.insts.size() && E.insts[r.id].alive; break;
        case EntKind::Guide: ok = r.id < (int)E.guides.size() && E.guides[r.id].alive; break;
        case EntKind::Dimension: ok = r.id < (int)E.dims.size() && E.dims[r.id].alive; break;
        case EntKind::Text: ok = r.id < (int)E.texts.size() && E.texts[r.id].alive; break;
        case EntKind::Section: ok = r.id < (int)E.sections.size() && E.sections[r.id].alive; break;
        default: break;
        }
        if (ok) s.push_back(r);
    }
    if (s.size() != sel.size()) setSelection(s);
}

std::vector<int> Document::selectedFaces() const {
    std::vector<int> r;
    for (auto& s : sel) if (s.kind == EntKind::Face) r.push_back(s.id);
    return r;
}
std::vector<int> Document::selectedEdges() const {
    std::vector<int> r;
    for (auto& s : sel) if (s.kind == EntKind::Edge) r.push_back(s.id);
    return r;
}
std::vector<int> Document::selectedInstances() const {
    std::vector<int> r;
    for (auto& s : sel) if (s.kind == EntKind::Instance) r.push_back(s.id);
    return r;
}

BBox Document::selectionBBox() const {
    const Entities& E = active();
    Mat4 xf = activeXf();
    BBox b;
    for (auto& r : sel) {
        switch (r.kind) {
        case EntKind::Edge: b.add(xf.point(E.verts[E.edges[r.id].v[0]].p)); b.add(xf.point(E.verts[E.edges[r.id].v[1]].p)); break;
        case EntKind::Face: for (int v : E.faces[r.id].outer) b.add(xf.point(E.verts[v].p)); break;
        case EntKind::Instance: b.add(model.defBBox(E.insts[r.id].def).transformed(xf * E.insts[r.id].xf)); break;
        case EntKind::Guide: if (E.guides[r.id].isPoint) b.add(xf.point(E.guides[r.id].p)); break;
        case EntKind::Dimension: b.add(xf.point(E.dims[r.id].a)); b.add(xf.point(E.dims[r.id].b)); break;
        case EntKind::Text: b.add(xf.point(E.texts[r.id].anchor)); break;
        default: break;
        }
    }
    return b;
}

// ---------------------------------------------------------------- undo

std::shared_ptr<Document::Snapshot> Document::takeSnapshot() {
    auto snap = std::make_shared<Snapshot>();
    // metadata without geometry
    Entities rootTmp = std::move(model.root);
    std::vector<Entities> defTmp;
    defTmp.reserve(model.defs.size());
    for (auto& d : model.defs) defTmp.push_back(std::move(d.ents));
    snap->meta = model;
    model.root = std::move(rootTmp);
    for (size_t i = 0; i < model.defs.size(); ++i) model.defs[i].ents = std::move(defTmp[i]);
    auto share = [](std::pair<uint64_t, std::shared_ptr<const Entities>>& cache, const Entities& e) {
        if (!cache.second || cache.first != e.rev) {
            cache.second = std::make_shared<const Entities>(e);
            cache.first = e.rev;
        }
        return cache.second;
    };
    // The root and the entities being edited are always copied (they may have been changed
    // without a revision bump); other definitions are shared while their revision is unchanged.
    rootCache_.second.reset();
    snap->root = share(rootCache_, model.root);
    defCache_.resize(model.defs.size());
    int act = activeDef();
    if (act >= 0 && act < (int)defCache_.size()) defCache_[act].second.reset();
    for (size_t i = 0; i < model.defs.size(); ++i) snap->defs.push_back(share(defCache_[i], model.defs[i].ents));
    return snap;
}

void Document::begin(const QString& name) {
    if (opActive_) commit();
    opSnapshot_ = takeSnapshot();
    opName_ = name;
    opActive_ = true;
}

void Document::commit() {
    if (!opActive_) return;
    opActive_ = false;
    undo_.push_back({opName_, opSnapshot_, editPath});
    if (undo_.size() > 200) undo_.erase(undo_.begin());
    redo_.clear();
    opSnapshot_.reset();
    pruneSelection();
    notifyChanged();
    emit undoStateChanged();
}

void Document::cancel() {
    if (!opActive_) return;
    opActive_ = false;
    UndoEntry e{opName_, opSnapshot_, editPath};
    restore(e);
    opSnapshot_.reset();
}

void Document::restore(const UndoEntry& e) {
    CameraState cam = model.camera;
    const Snapshot& s = *e.snap;
    model = s.meta;
    model.root = *s.root;
    for (size_t i = 0; i < model.defs.size() && i < s.defs.size(); ++i) model.defs[i].ents = *s.defs[i];
    model.root.invalidateIndex();
    for (auto& d : model.defs) d.ents.invalidateIndex();
    // caches now describe exactly the restored state
    rootCache_ = {model.root.rev, s.root};
    defCache_.assign(s.defs.size(), {});
    for (size_t i = 0; i < s.defs.size(); ++i) defCache_[i] = {model.defs[i].ents.rev, s.defs[i]};
    model.camera = cam;  // undo does not move the camera
    editPath = e.path;
    // validate path
    Entities* E = &model.root;
    for (size_t k = 0; k < editPath.size(); ++k) {
        int i = editPath[k];
        if (i < 0 || i >= (int)E->insts.size() || !E->insts[i].alive) { editPath.resize(k); break; }
        E = &model.defs[E->insts[i].def].ents;
    }
    sel.clear();
    selSet_.clear();
    emit contextChanged();
    emit selectionChanged();
    notifyChanged();
}

void Document::undo() {
    if (opActive_) { cancel(); return; }
    if (undo_.empty()) return;
    UndoEntry e = undo_.back();
    undo_.pop_back();
    redo_.push_back({e.name, takeSnapshot(), editPath});
    restore(e);
    emit undoStateChanged();
}

void Document::redo() {
    if (redo_.empty()) return;
    UndoEntry e = redo_.back();
    redo_.pop_back();
    undo_.push_back({e.name, takeSnapshot(), editPath});
    restore(e);
    emit undoStateChanged();
}

// ---------------------------------------------------------------- file

void Document::newModel() {
    model.reset();
    filePath.clear();
    editPath.clear();
    sel.clear();
    selSet_.clear();
    undo_.clear();
    redo_.clear();
    rootCache_ = {};
    defCache_.clear();
    modified = false;
    emit contextChanged();
    emit selectionChanged();
    emit changed();
    emit fileChanged();
    emit undoStateChanged();
}

bool Document::load(const QString& path, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (err) *err = f.errorString(); return false; }
    Model m;
    if (!loadModel(m, f.readAll(), err)) return false;
    model = std::move(m);
    filePath = path;
    editPath.clear();
    sel.clear();
    selSet_.clear();
    undo_.clear();
    redo_.clear();
    rootCache_ = {};
    defCache_.clear();
    modified = false;
    emit contextChanged();
    emit selectionChanged();
    emit changed();
    emit fileChanged();
    emit undoStateChanged();
    return true;
}

bool Document::save(const QString& path, QString* err) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { if (err) *err = f.errorString(); return false; }
    f.write(saveModel(model, true));
    if (!f.commit()) { if (err) *err = f.errorString(); return false; }
    filePath = path;
    modified = false;
    emit fileChanged();
    return true;
}

// ---------------------------------------------------------------- operations

void Document::eraseSelection() {
    if (sel.empty()) return;
    Entities& E = active();
    std::vector<int> edges, faces;
    for (auto& r : sel) {
        switch (r.kind) {
        case EntKind::Edge: edges.push_back(r.id); break;
        case EntKind::Face: faces.push_back(r.id); break;
        case EntKind::Instance: E.insts[r.id].alive = false; break;
        case EntKind::Guide: E.guides[r.id].alive = false; break;
        case EntKind::Dimension: E.dims[r.id].alive = false; break;
        case EntKind::Text: E.texts[r.id].alive = false; break;
        case EntKind::Section: E.sections[r.id].alive = false; break;
        default: break;
        }
    }
    geom::eraseFaces(E, faces);
    if (!edges.empty()) geom::eraseEdges(E, edges);
    E.touch();
    sel.clear();
    selSet_.clear();
    emit selectionChanged();
}

int Document::makeGroup(const std::vector<EntRef>& s, const QString& name, bool component) {
    if (s.empty()) return -1;
    Entities& E = active();
    // Determine origin: groups use the parent's axes; components use the bbox min corner.
    BBox b;
    for (auto& r : s) {
        if (r.kind == EntKind::Edge) { b.add(E.verts[E.edges[r.id].v[0]].p); b.add(E.verts[E.edges[r.id].v[1]].p); }
        else if (r.kind == EntKind::Face) for (int v : E.faces[r.id].outer) b.add(E.verts[v].p);
        else if (r.kind == EntKind::Instance) b.add(model.defBBox(E.insts[r.id].def).transformed(E.insts[r.id].xf));
    }
    Mat4 xf;
    if (component && b.valid()) xf = Mat4::translation(b.lo);
    int def = model.addDef(name.isEmpty() ? (component ? QString("Component") : QString("Group")) : name, !component);
    std::vector<EntRef> closed = geom::closeSelection(E, s);
    Entities local;
    geom::copyEntities(E, closed, local, xf.inverted(), false);
    model.defs[def].ents = std::move(local);
    // Remove originals: faces, edges not used by remaining faces, other entities.
    Entities& A = active();
    std::set<int> selFaces;
    for (auto& r : closed) if (r.kind == EntKind::Face) selFaces.insert(r.id);
    for (int f : selFaces) A.removeFace(f);
    for (auto& r : closed) {
        switch (r.kind) {
        case EntKind::Edge: {
            bool usedElsewhere = false;
            for (int f : A.edges[r.id].faces)
                if (A.faces[f].alive) usedElsewhere = true;
            if (!usedElsewhere) A.removeEdge(r.id);
            break;
        }
        case EntKind::Instance: A.insts[r.id].alive = false; break;
        case EntKind::Guide: A.guides[r.id].alive = false; break;
        case EntKind::Dimension: A.dims[r.id].alive = false; break;
        case EntKind::Text: A.texts[r.id].alive = false; break;
        case EntKind::Section: A.sections[r.id].alive = false; break;
        default: break;
        }
    }
    Instance in;
    in.def = def;
    in.xf = xf;
    A.insts.push_back(in);
    A.touch();
    int idx = (int)A.insts.size() - 1;
    setSelection({{EntKind::Instance, idx}});
    return idx;
}

void Document::explode(int inst) {
    Entities& E = active();
    if (inst < 0 || inst >= (int)E.insts.size() || !E.insts[inst].alive) return;
    Instance in = E.insts[inst];
    E.insts[inst].alive = false;
    const Entities& D = model.defs[in.def].ents;
    std::vector<EntRef> all;
    for (int i = 0; i < (int)D.edges.size(); ++i) if (D.edges[i].alive) all.push_back({EntKind::Edge, i});
    for (int i = 0; i < (int)D.faces.size(); ++i) if (D.faces[i].alive) all.push_back({EntKind::Face, i});
    for (int i = 0; i < (int)D.insts.size(); ++i) if (D.insts[i].alive) all.push_back({EntKind::Instance, i});
    for (int i = 0; i < (int)D.guides.size(); ++i) if (D.guides[i].alive) all.push_back({EntKind::Guide, i});
    for (int i = 0; i < (int)D.dims.size(); ++i) if (D.dims[i].alive) all.push_back({EntKind::Dimension, i});
    for (int i = 0; i < (int)D.texts.size(); ++i) if (D.texts[i].alive) all.push_back({EntKind::Text, i});
    Entities copy = D;  // D may live inside E's model; copy for safety
    auto res = geom::copyEntities(copy, all, active(), in.xf, true);
    // Instance material is applied to default-material faces
    Entities& A = active();
    for (auto& r : res) {
        if (r.kind == EntKind::Face && in.material >= 0 && A.faces[r.id].matFront < 0) A.faces[r.id].matFront = in.material;
        if (r.kind == EntKind::Instance && in.material >= 0 && A.insts[r.id].material < 0) A.insts[r.id].material = in.material;
    }
    model.purgeUnused();
    setSelection(res);
}

void Document::makeUnique(int inst) {
    Entities& E = active();
    if (inst < 0 || inst >= (int)E.insts.size()) return;
    int def = E.insts[inst].def;
    ComponentDef copy = model.defs[def];
    copy.name = model.uniqueDefName(copy.name);
    model.defs.push_back(std::move(copy));
    active().insts[inst].def = (int)model.defs.size() - 1;
    active().touch();
}

void Document::hideSelection(bool hide) {
    Entities& E = active();
    for (auto& r : sel) {
        switch (r.kind) {
        case EntKind::Edge: E.edges[r.id].hidden = hide; break;
        case EntKind::Face: E.faces[r.id].hidden = hide; break;
        case EntKind::Instance: E.insts[r.id].hidden = hide; break;
        case EntKind::Guide: E.guides[r.id].hidden = hide; break;
        case EntKind::Dimension: E.dims[r.id].hidden = hide; break;
        case EntKind::Text: E.texts[r.id].hidden = hide; break;
        case EntKind::Section: E.sections[r.id].hidden = hide; break;
        default: break;
        }
    }
    E.touch();
    if (hide) clearSelection();
}

void Document::unhideAll() {
    Entities& E = active();
    for (auto& x : E.edges) x.hidden = false;
    for (auto& x : E.faces) x.hidden = false;
    for (auto& x : E.insts) x.hidden = false;
    for (auto& x : E.guides) x.hidden = false;
    for (auto& x : E.dims) x.hidden = false;
    for (auto& x : E.texts) x.hidden = false;
    for (auto& x : E.sections) x.hidden = false;
    E.touch();
}

void Document::lockSelection(bool lock) {
    Entities& E = active();
    for (auto& r : sel)
        if (r.kind == EntKind::Instance) E.insts[r.id].locked = lock;
    E.touch();
}

void Document::setTagOnSelection(int tag) {
    Entities& E = active();
    for (auto& r : sel) {
        switch (r.kind) {
        case EntKind::Edge: E.edges[r.id].tag = tag; break;
        case EntKind::Face: E.faces[r.id].tag = tag; break;
        case EntKind::Instance: E.insts[r.id].tag = tag; break;
        case EntKind::Guide: E.guides[r.id].tag = tag; break;
        case EntKind::Dimension: E.dims[r.id].tag = tag; break;
        case EntKind::Text: E.texts[r.id].tag = tag; break;
        case EntKind::Section: E.sections[r.id].tag = tag; break;
        default: break;
        }
    }
    E.touch();
}

int Document::addTag(const QString& name) {
    Tag t;
    t.name = name;
    static const QColor palette[] = {QColor(222, 106, 90), QColor(98, 160, 202), QColor(140, 190, 98), QColor(232, 180, 80),
                                     QColor(170, 120, 200), QColor(90, 190, 180), QColor(200, 140, 110), QColor(150, 150, 150)};
    t.color = palette[model.tags.size() % 8];
    model.tags.push_back(t);
    return (int)model.tags.size() - 1;
}

void Document::deleteTag(int tag, bool moveToUntagged) {
    if (tag <= 0 || tag >= (int)model.tags.size()) return;
    std::function<void(Entities&)> fix = [&](Entities& E) {
        // returns true if the entity should be deleted
        auto f = [&](int& t) {
            if (t == tag) {
                if (moveToUntagged) { t = 0; return false; }
                return true;
            }
            if (t > tag) --t;
            return false;
        };
        std::vector<int> delFaces, delEdges;
        for (int i = 0; i < (int)E.faces.size(); ++i) if (E.faces[i].alive && f(E.faces[i].tag)) delFaces.push_back(i);
        for (int i = 0; i < (int)E.edges.size(); ++i) if (E.edges[i].alive && f(E.edges[i].tag)) delEdges.push_back(i);
        for (auto& x : E.insts) if (x.alive && f(x.tag)) x.alive = false;
        for (auto& x : E.guides) if (x.alive && f(x.tag)) x.alive = false;
        for (auto& x : E.dims) if (x.alive && f(x.tag)) x.alive = false;
        for (auto& x : E.texts) if (x.alive && f(x.tag)) x.alive = false;
        for (auto& x : E.sections) if (x.alive && f(x.tag)) x.alive = false;
        for (int fc : delFaces) E.removeFace(fc);
        for (int e : delEdges) E.removeEdge(e);
        E.touch();
    };
    fix(model.root);
    for (auto& d : model.defs) fix(d.ents);
    model.tags.erase(model.tags.begin() + tag);
    for (auto& sc : model.scenes)
        if (tag < (int)sc.tagVisible.size()) sc.tagVisible.erase(sc.tagVisible.begin() + tag);
}

}  // namespace gable
