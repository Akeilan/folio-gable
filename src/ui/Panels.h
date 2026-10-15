#pragma once
#include "ui/Document.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace gable {

class Viewport;

class ColorButton : public QToolButton {
    Q_OBJECT
public:
    explicit ColorButton(QWidget* parent = nullptr);
    void setColor(const QColor& c);
    QColor color() const { return c_; }
signals:
    void colorChanged(const QColor& c);
private:
    QColor c_;
};

class CollapsibleSection : public QWidget {
    Q_OBJECT
public:
    CollapsibleSection(const QString& title, QWidget* content, QWidget* parent = nullptr);
    void setExpanded(bool e);
    bool expanded() const;
    QString title() const { return title_; }
private:
    QToolButton* header_;
    QWidget* content_;
    QString title_;
};

class Tray : public QScrollArea {
    Q_OBJECT
public:
    explicit Tray(QWidget* parent = nullptr);
    CollapsibleSection* addPanel(const QString& title, QWidget* w, bool expanded);
    void showPanel(const QString& title);
private:
    QVBoxLayout* lay_;
    std::vector<CollapsibleSection*> sections_;
};

class EntityInfoPanel : public QWidget {
    Q_OBJECT
public:
    EntityInfoPanel(Document* d, QWidget* parent = nullptr);
    void refresh();
signals:
    void requestRedraw();
private:
    Document* doc_;
    QLabel* title_;
    QWidget* body_;
    QVBoxLayout* bodyLay_;
    bool updating_ = false;
    void clearBody();
    QComboBox* tagCombo(int current);
};

class MaterialsPanel : public QWidget {
    Q_OBJECT
public:
    MaterialsPanel(Document* d, QWidget* parent = nullptr);
    void refresh();
    int current() const { return current_; }
    void setCurrent(int m);
signals:
    void materialChosen(int m);
private:
    Document* doc_;
    QComboBox* source_;
    QListWidget* list_;
    QLabel* currentLabel_;
    QLabel* currentSwatch_;
    int current_ = -1;
    void editMaterial(int m);
    void addFromLibrary(int libIndex);
    QIcon swatch(const Material& m) const;
};

class ComponentsPanel : public QWidget {
    Q_OBJECT
public:
    ComponentsPanel(Document* d, QWidget* parent = nullptr);
    void refresh();
signals:
    void placeComponent(int def);
private:
    Document* doc_;
    QListWidget* list_;
    QCheckBox* showGroups_;
};

class StylesPanel : public QWidget {
    Q_OBJECT
public:
    StylesPanel(Document* d, QWidget* parent = nullptr);
    void refresh();
signals:
    void styleChanged();
private:
    Document* doc_;
    bool updating_ = false;
    QComboBox* preset_;
    QComboBox* faceStyle_;
    QCheckBox *edges_, *profiles_, *backEdges_, *xray_, *extension_, *sky_, *ground_, *byAxis_, *byTag_, *guides_, *axes_;
    QSpinBox* profileW_;
    ColorButton *bg_, *skyC_, *groundC_, *front_, *back_, *edgeC_;
};

class TagsPanel : public QWidget {
    Q_OBJECT
public:
    TagsPanel(Document* d, QWidget* parent = nullptr);
    void refresh();
private:
    Document* doc_;
    QTreeWidget* tree_;
    bool updating_ = false;
};

class ScenesPanel : public QWidget {
    Q_OBJECT
public:
    ScenesPanel(Document* d, Viewport* v, QWidget* parent = nullptr);
    void refresh();
    void applyScene(int i);
    void addScene();
    void updateScene(int i);
signals:
    void scenesChanged();
private:
    Document* doc_;
    Viewport* view_;
    QListWidget* list_;
    QLineEdit* name_;
    QPlainTextEdit* desc_;
    bool updating_ = false;
};

class ShadowsPanel : public QWidget {
    Q_OBJECT
public:
    ShadowsPanel(Document* d, QWidget* parent = nullptr);
    void refresh();
private:
    Document* doc_;
    bool updating_ = false;
    QCheckBox *enabled_, *sunShading_, *onFaces_, *onGround_;
    QSlider *time_, *date_, *light_, *dark_;
    QLabel *timeL_, *dateL_;
    QDoubleSpinBox *utc_, *lat_, *lon_, *north_;
};

class FogPanel : public QWidget {
    Q_OBJECT
public:
    FogPanel(Document* d, QWidget* parent = nullptr);
    void refresh();
private:
    Document* doc_;
    bool updating_ = false;
    QCheckBox* enabled_;
    QSlider *start_, *end_;
    ColorButton* color_;
};

class OutlinerPanel : public QWidget {
    Q_OBJECT
public:
    OutlinerPanel(Document* d, QWidget* parent = nullptr);
    void refresh();
private:
    Document* doc_;
    QTreeWidget* tree_;
    QLineEdit* filter_;
    bool updating_ = false;
};

class SoftenPanel : public QWidget {
    Q_OBJECT
public:
    SoftenPanel(Document* d, QWidget* parent = nullptr);
private:
    Document* doc_;
    QSlider* angle_;
    QLabel* angleL_;
    QCheckBox *smooth_, *coplanar_;
    void apply();
};

// Built-in material library
struct LibraryMaterial { QString name; QColor color; double alpha; QString texture; double size; };
const std::vector<LibraryMaterial>& materialLibrary();
QImage proceduralTexture(const QString& kind, int px = 256);

}  // namespace gable
