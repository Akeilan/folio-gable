// Folio Gable — dynamic components: attribute formulas, redraw, and onClick behaviour.
#pragma once
#include "core/Model.h"
#include <QStringList>
#include <functional>

namespace gable {
namespace dyn {

// A formula result: a number (lengths in mm, angles in degrees, true = 1) or text.
struct Value {
    bool isText = false;
    double num = 0;
    QString text;
    Value() = default;
    Value(double d) : num(d) {}
    Value(const QString& t) : isText(true), text(t) {}
    double toNum(bool* ok = nullptr) const;
    QString toText() const;
};

// Evaluates a formula (without the leading '='). lookup resolves names such as LenX,
// Parent!LenX, Shelf!Copies and COPY; it returns false for an unknown name.
using Lookup = std::function<bool(const QString& ref, Value& out, QString& err)>;
bool evaluate(const QString& formula, const Lookup& lookup, Value& out, QString* err = nullptr);

// The attributes Component Attributes offers, grouped as in its Add Attribute menu.
struct StdAttr { const char* group; const char* name; const char* units; const char* tip; };
const std::vector<StdAttr>& standardAttributes();
QString defaultUnits(const QString& name);

const DynAttr* find(const std::vector<DynAttr>& a, const QString& name);
DynAttr* find(std::vector<DynAttr>& a, const QString& name);

// What a standard attribute (X, LenX, RotZ, Name, Hidden, Material ...) measures now on
// E.insts[inst]; false for a name that isn't one.
bool currentValue(Model& m, Entities& E, int inst, const QString& name, Value& out);

// True when the instance (or anything inside it) has attributes.
bool isDynamic(const Model& m, const Instance& in);

// Re-evaluates the instance E.insts[inst] and everything inside it, moving, sizing, hiding,
// painting and copying the parts as their formulas say. A definition that other instances
// share is first made unique, so only this one changes. Returns false if a formula failed
// (errors lists them; the rest is still applied).
bool redraw(Model& m, Entities& E, int inst, QStringList* errors = nullptr);

// One change an onClick action makes: attribute `attr` of the instance at `path` (indices from
// the top instance's Entities down) goes from `from` to `to`; animate = ANIMATE rather than SET.
struct ClickChange {
    std::vector<int> path;
    QString attr;
    Value from, to;
    bool animate = false;
};

// The instance on the path (from Entities E down) whose onClick runs when the deepest part is
// clicked: the deepest one that has onClick, or -1 for none (its depth in path otherwise).
int clickTarget(const Model& m, const Entities& E, const std::vector<int>& path);
// Works out what the onClick of path[0..depth] does, without changing anything.
bool clickChanges(Model& m, Entities& E, const std::vector<int>& path, int depth, std::vector<ClickChange>& out,
                  QStringList* errors = nullptr);
// Makes the definitions along the path unique so a click changes only the clicked instance.
void makePathUnique(Model& m, Entities& E, const std::vector<int>& path);
// Sets an attribute's value (dropping any formula) of the instance at path.
void setAttr(Model& m, Entities& E, const std::vector<int>& path, const QString& attr, const Value& v);

// Ready-made dynamic components the Components panel offers (no files or downloads needed).
QStringList sampleNames();
QString sampleDescription(int which);
// Adds the sample's definitions (and any materials it uses) to the model; returns its definition.
int addSample(Model& m, int which);

}  // namespace dyn
}  // namespace gable
