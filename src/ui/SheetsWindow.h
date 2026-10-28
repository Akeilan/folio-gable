#pragma once
// Drawing sheets (G2-03): the window where scenes go on paper, with dimensions, labels and a
// title block, to print or save as PDF.
#include "ui/Document.h"
#include <QMainWindow>
#include <QPointer>
#include <QWidget>
#include <functional>
#include <map>

class QListWidget;
class QComboBox;
class QLineEdit;
class QDoubleSpinBox;
class QCheckBox;
class QFormLayout;
class QStackedWidget;
class QPainter;
class QLabel;
class QActionGroup;

namespace gable {

class Viewport;
class SheetsWindow;

// What is selected on the sheet
struct SheetSel {
    enum Kind { None, View, Label, Dim, Title } kind = None;
    int index = -1;
    bool operator==(const SheetSel& o) const { return kind == o.kind && index == o.index; }
};

// Draws a sheet with a painter at pxPerMm; viewImage gives the picture for viewport i at a size in pixels.
void paintSheet(QPainter& p, const Sheet& s, const Model& m, double pxPerMm, const std::function<QImage(int, QSize)>& viewImage,
                const SheetSel* sel = nullptr);
// The text a dimension shows: the model length when its viewport has a scale, else paper millimetres.
QString dimensionText(const Sheet& s, const SheetDimension& d, const Model& m);
// "1:50" for a scale of 50 model mm per paper mm.
QString scaleText(double scale);

class SheetCanvas : public QWidget {
    Q_OBJECT
public:
    explicit SheetCanvas(SheetsWindow* w);
    enum Tool { Select, AddView, AddLabel, AddDim };
    void setTool(Tool t);
    Tool tool() const { return tool_; }
    QPointF toMm(const QPointF& px) const;
    QPointF toPx(const QPointF& mm) const;
    double pxPerMm() const;
    SheetSel hit(const QPointF& mm) const;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    SheetsWindow* w_;
    Tool tool_ = Select;
    // a drag in progress
    bool dragging_ = false;
    QPointF start_, now_;
    int handle_ = -1;          // viewport corner being dragged (0..3), -1 = move
    Sheet before_;             // the sheet as it was when the drag began
    int dimStage_ = 0;         // AddDim: 0 nothing, 1 first point placed, 2 second placed (now the offset)
    QPointF dimA_, dimB_;
    QPointF snap(const QPointF& mm) const;
};

class SheetsWindow : public QMainWindow {
    Q_OBJECT
public:
    SheetsWindow(Document* doc, Viewport* view, QWidget* parent);
    Document* doc() const { return doc_; }
    Sheet* sheet();                       // the current sheet (one is made if there are none)
    int current() const { return current_; }
    void setCurrent(int i);
    SheetSel sel;
    void select(const SheetSel& s);
    // an edit to the current sheet, as one undo step
    void edit(const QString& name, const std::function<void(Sheet&)>& fn);
    void beginDrag();
    void endDrag(const QString& name);
    void refreshAll();
    void refreshProps();
    QImage viewImage(int sheetIdx, int vpIdx, QSize px, bool cached = true);
    int defaultScene() const;
    // print or save every sheet: to a PDF file (path) or the printer
    bool exportPdf(const QString& path, QString* err = nullptr);
    void printSheets();
    void addViewport(const QRectF& mm);
    void addSheet();
    void deleteSheet();
    void duplicateSheet();
    void deleteSelected();
    SheetCanvas* canvas() const { return canvas_; }
    QListWidget* sheetList() const { return list_; }

protected:
    void changeEvent(QEvent* e) override;

private:
    Document* doc_;
    Viewport* view_;
    int current_ = 0;
    SheetCanvas* canvas_;
    QListWidget* list_;
    QComboBox* paper_;
    QComboBox* orient_;
    QLineEdit* tProject_; QLineEdit* tTitle_; QLineEdit* tDrawn_; QLineEdit* tDate_; QLineEdit* tSheet_; QLineEdit* tScale_;
    QCheckBox* tShow_;
    QStackedWidget* props_;
    QComboBox* vpScene_; QComboBox* vpScale_; QCheckBox* vpFrame_;
    QLineEdit* lbText_; QDoubleSpinBox* lbSize_;
    QLineEdit* dmText_; QLabel* dmMeasured_;
    QComboBox* newScene_;
    QActionGroup* tools_;
    bool syncing_ = false;
    bool inDrag_ = false;
    struct Cached { QString key; QImage img; };
    std::map<std::pair<int, int>, Cached> cache_;
    QString viewKey(int sheetIdx, int vpIdx, QSize px) const;
    void buildUi();
    void syncSheetFields();
};

}  // namespace gable
