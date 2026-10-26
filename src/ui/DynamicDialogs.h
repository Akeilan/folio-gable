// Folio Gable — Component Options and Component Attributes windows for dynamic components.
#pragma once
#include "ui/Document.h"
#include <QDialog>

class QVBoxLayout;
class QLabel;
class QPushButton;
class QScrollArea;

namespace gable {

// The selected dynamic component's options, as its author let users see or change them.
class ComponentOptionsDialog : public QDialog {
    Q_OBJECT
    friend class Auditor;
public:
    ComponentOptionsDialog(Document* d, QWidget* parent = nullptr);
    void refresh();
    bool apply();     // false when a value could not be read (the field is marked)
signals:
    void redrawn();
private:
    Document* doc_;
    int inst_ = -1;
    QLabel *title_, *summary_, *empty_;
    QWidget* form_;
    QPushButton* apply_;
    struct Row { QString attr; QWidget* w; QString units; };
    std::vector<Row> rows_;
    bool updating_ = false;
};

// Every attribute of the selected component and of the parts inside it: values, formulas
// and what users may do with them.
class ComponentAttributesDialog : public QDialog {
    Q_OBJECT
    friend class Auditor;
public:
    ComponentAttributesDialog(Document* d, QWidget* parent = nullptr);
    void refresh();
    // Edits used by the window and by tests. part = -1 is the component itself, otherwise the
    // index of a part inside it. text starting with '=' is a formula.
    bool setAttribute(int part, const QString& name, const QString& text, QString* err = nullptr);
    bool removeAttribute(int part, const QString& name);
    bool setOptions(int part, const QString& name, int access, const QString& label, const QString& units,
                    const std::vector<std::pair<QString, QString>>& options);
    QStringList lastErrors() const { return errors_; }
signals:
    void redrawn();
private:
    Instance* part(int p);
    void commitEdit(const QString& what);
    void optionsDialog(int part, const QString& name);
    void addSection(int part, const QString& title);
    Document* doc_;
    int inst_ = -1;
    QLabel* empty_;
    QScrollArea* scroll_;
    QWidget* body_;
    QVBoxLayout* bodyLay_;
    QStringList errors_;
    bool updating_ = false;
};

// Text a value is shown as: lengths in the model's units, angles in degrees.
QString formatAttrValue(const DynAttr& a, const UnitSettings& u);
// Reads what was typed for an attribute with these units; false if it isn't a valid value.
bool parseAttrValue(const QString& text, const QString& units, const UnitSettings& u, QString& out);

}  // namespace gable
