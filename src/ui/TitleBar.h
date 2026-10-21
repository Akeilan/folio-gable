// Folio Gable — the suite title bar: Quick Access Toolbar, document title, command search
// and the window buttons (the window draws its own frame, like the other Folio apps).
#pragma once
#include <QAction>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QWidget>
#include <functional>
#include <vector>

class QHBoxLayout;
class QCompleter;
class QStringListModel;

namespace gable {

class TitleBar : public QWidget {
    Q_OBJECT
public:
    explicit TitleBar(QWidget* window);
    void setTitle(const QString& t);
    void setQat(const std::vector<std::pair<QAction*, QString>>& actions);   // action, icon name
    const std::vector<QToolButton*>& qatButtons() const { return qatButtons_; }
    QToolButton* qatMenuButton() const { return qatMenu_; }
    void setWindowButtonsVisible(bool on);
    void syncMaximized();
    // Command search ("Search (Alt+Q)"): every action, by its plain name.
    void setSearchActions(const std::vector<QAction*>& acts);
    QLineEdit* search() const { return search_; }
    void focusSearch();
protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;
private:
    QWidget* win_;
    QHBoxLayout* qatLay_;
    QWidget* qatBox_;
    std::vector<QToolButton*> qatButtons_;
    QToolButton* qatMenu_;
    QLabel* title_;
    QLineEdit* search_;
    QCompleter* completer_;
    QStringListModel* model_;
    std::vector<QAction*> searchActs_;
    QToolButton *min_, *max_, *close_;
    void runSearch(const QString& text);
};

// Lets a frameless top-level window be resized from its edges (5 px band).
class FramelessResizer : public QObject {
public:
    explicit FramelessResizer(QWidget* window);
    bool enabled = true;
protected:
    bool eventFilter(QObject* o, QEvent* e) override;
private:
    QWidget* win_;
    Qt::Edges hover_;
    bool overriding_ = false;
    Qt::Edges edgesAt(const QPoint& global) const;
    void update(const QPoint& global);
};

}  // namespace gable
