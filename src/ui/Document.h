#pragma once
#include "core/Model.h"
#include <QObject>
#include <QString>
#include <set>
#include <vector>

namespace gable {

// A path from the root through instances (indices into each level's insts).
using InstPath = std::vector<int>;

class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(QObject* parent = nullptr);

    Model model;
    QString filePath;
    bool modified = false;
    bool hideRest = false;   // View > Component Edit > Hide Rest of Model

    // ---- edit context (open group/component)
    InstPath editPath;
    Entities& active();
    const Entities& active() const;
    Mat4 activeXf() const;          // active local -> world
    Mat4 activeInv() const { return activeXf().inverted(); }
    int activeDef() const;          // -1 for root
    Entities& entitiesAt(const InstPath& path);
    Mat4 xfAt(const InstPath& path) const;
    void openInstance(int inst);
    void closeContext();            // one level up
    void closeAllContexts();
    bool inContext() const { return !editPath.empty(); }

    // ---- selection (refs into active())
    std::vector<EntRef> sel;
    bool isSelected(const EntRef& r) const { return selSet_.count(r) > 0; }
    void setSelection(const std::vector<EntRef>& s);
    void addToSelection(const std::vector<EntRef>& s);
    void toggleSelection(const std::vector<EntRef>& s);
    void removeFromSelection(const std::vector<EntRef>& s);
    void clearSelection();
    void selectAll();
    void pruneSelection();
    std::vector<int> selectedFaces() const;
    std::vector<int> selectedEdges() const;
    std::vector<int> selectedInstances() const;
    BBox selectionBBox() const;   // world space

    // ---- undo
    void begin(const QString& name);   // snapshot before an operation
    void commit();                      // finish operation
    void cancel();                      // revert to snapshot
    bool inOperation() const { return opActive_; }
    void undo();
    void redo();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    QString undoName() const { return undo_.empty() ? QString() : undo_.back().name; }
    QString redoName() const { return redo_.empty() ? QString() : redo_.back().name; }

    // ---- file
    void newModel();
    bool load(const QString& path, QString* err);
    bool save(const QString& path, QString* err);

    // ---- common operations
    void eraseSelection();
    int makeGroup(const std::vector<EntRef>& s, const QString& name = QString(), bool component = false);
    void explode(int inst);
    void makeUnique(int inst);
    void hideSelection(bool hide);
    void unhideAll();
    void lockSelection(bool lock);
    void setTagOnSelection(int tag);
    int addTag(const QString& name);
    void deleteTag(int tag, bool moveToUntagged);
    bool tagVisible(int t) const { return t < 0 || t >= (int)model.tags.size() || model.tags[t].visible; }

    // Notifications
    void notifyChanged() { modified = true; emit changed(); }
    void notifySelection() { emit selectionChanged(); }
    void notifyView() { emit viewChanged(); }

signals:
    void changed();
    void selectionChanged();
    void contextChanged();
    void viewChanged();
    void fileChanged();
    void undoStateChanged();

private:
    std::set<EntRef> selSet_;
    struct UndoEntry { QString name; QByteArray data; InstPath path; };
    std::vector<UndoEntry> undo_, redo_;
    QByteArray opSnapshot_;
    QString opName_;
    bool opActive_ = false;
    void restore(const UndoEntry& e);
};

}  // namespace gable
