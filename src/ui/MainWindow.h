#pragma once
#include "ui/Document.h"
#include <QAction>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QDockWidget>
#include <QTabBar>
#include <QTimer>
#include <QPointer>
#include <map>
#include <memory>

namespace gable {

class Viewport;
class Tray;
class MaterialsPanel;
class ScenesPanel;
class EntityInfoPanel;
class StylesPanel;
class TitleBar;
class Ribbon;
struct RibbonTab;
class Backstage;
class KeyTips;
class FramelessResizer;
class SheetsWindow;

class MainWindow;
int runAudit(MainWindow* w);   // folio-gable --audit
int runBench(MainWindow* w);   // folio-gable --bench

class MainWindow : public QMainWindow {
    Q_OBJECT
    friend class Auditor;
    friend class Bencher;
public:
    MainWindow();
    ~MainWindow() override;
    bool openFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void changeEvent(QEvent* e) override;
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
    QPointer<SheetsWindow> sheets_;
    QLabel* status_;
    QLabel* vcbLabel_;
    QLineEdit* vcb_;
    QTimer autosave_;
    std::map<QString, QAction*> act_;
    std::map<QString, QAction*> toolAct_;
    std::shared_ptr<Model> clipboard_;
    Vec3 clipRef_;
    QMenu* recentMenu_ = nullptr;
    TitleBar* titleBar_ = nullptr;
    Ribbon* ribbon_ = nullptr;
    RibbonTab* groupTab_ = nullptr;
    Backstage* backstage_ = nullptr;
    KeyTips* keytips_ = nullptr;
    FramelessResizer* resizer_ = nullptr;
    QDockWidget* trayDock_ = nullptr;
    QStringList qatIds_;

    QAction* addAct(const QString& id, const QString& text, const QKeySequence& ks, std::function<void()> fn,
                    const QString& icon = QString());
    QAction* addToolAct(const QString& toolId, const QString& text, const QKeySequence& ks, const QString& icon);
    void buildTools();
    void buildActions();
    void buildChrome();
    void buildRibbon();
    void decorateActions();
    void applyQat();
    void showBackstage(const QString& page = "home");
    void saveThumbnail(const QString& path);
    QPixmap thumbnailFor(const QString& path) const;
    void newFromTemplate(int i);
    void forgetRecent(const QString& path);
    void placeBackstage();
    void buildTray();
    void buildStatus();
    void updateTitle();
    void updateSceneTabs();
    void updateUndoActions();
    void syncViewToggles();

    // commands
    bool maybeSave();
    void newFile();
    // Camera › Match Photo from a picture file (asks for one when path is empty)
    bool newMatchPhoto(const QString& path);
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
    void notAvailable(const QString& what);
    void drawingSheets();                     // G2-03: the Drawing Sheets window
    QString sendEmail(const QString& what);   // M-12: "model" or "picture"; returns the attached file
    void showDynamicDialog(bool attributes);
    class ComponentOptionsDialog* compOptions_ = nullptr;
    class ComponentAttributesDialog* compAttrs_ = nullptr;
    void intersect(int mode);
    void reverseFaces();
    // Texture commands on the one selected face (side facing the camera): 0 Position, 1 Reset,
    // 2 Projected on/off, 3/4/5 rotate 90/180/270, 6 flip left-right, 7 flip up-down.
    void textureCommand(int what);
    bool textureTarget(int& face, bool& back) const;
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
