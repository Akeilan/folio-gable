#pragma once
#include "ui/Document.h"
#include <QAction>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QTabBar>
#include <QTimer>
#include <map>
#include <memory>

namespace gable {

class Viewport;
class Tray;
class MaterialsPanel;
class ScenesPanel;
class EntityInfoPanel;
class StylesPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;
    bool openFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    Document* doc_;
    Viewport* view_;
    Tray* tray_;
    MaterialsPanel* materials_ = nullptr;
    ScenesPanel* scenes_ = nullptr;
    StylesPanel* styles_ = nullptr;
    QTabBar* sceneTabs_;
    QLabel* status_;
    QLabel* vcbLabel_;
    QLineEdit* vcb_;
    QTimer autosave_;
    std::map<QString, QAction*> act_;
    std::map<QString, QAction*> toolAct_;
    std::shared_ptr<Model> clipboard_;
    Vec3 clipRef_;
    QMenu* recentMenu_ = nullptr;

    QAction* addAct(const QString& id, const QString& text, const QKeySequence& ks, std::function<void()> fn,
                    const QString& icon = QString());
    QAction* addToolAct(const QString& toolId, const QString& text, const QKeySequence& ks, const QString& icon);
    void buildTools();
    void buildActions();
    void buildMenus();
    void buildToolbars();
    void buildTray();
    void buildStatus();
    void updateTitle();
    void updateSceneTabs();
    void updateUndoActions();
    void syncViewToggles();

    // commands
    bool maybeSave();
    void newFile();
    void open();
    bool save();
    bool saveAs();
    void importFile();
    void exportModel(const QString& forcedExt = QString());
    void exportImage();
    void print();
    void addRecent(const QString& path);
    void refreshRecent();
    void copy(bool cut);
    void paste(bool inPlace);
    void makeComponentDialog();
    void contextMenu(const QPoint& at);
    void modelInfo();
    void shortcutsHelp();
    void about();
    void solidOp(int op);
    void intersect(int mode);
    void reverseFaces();
    void orientFaces();
    void flipAlong(int axis);
    void text3D();
    void alignView();
    void selectSame(int what);
    void invertSelection();
    void deleteGuides();
    void hideRest(bool on);
    void doAutosave();
    void preferences();
    void loadShortcuts();
    void divideEdge();
    void weldEdges();
    void explodeCurve();
    void playScenes(bool on);
    void checkRecovery();
    void fromContours();
    void addDetail();
    QTimer playTimer_;
    int playIndex_ = 0;
    std::map<QString, QKeySequence> defaultShortcuts_;
};

}  // namespace gable
