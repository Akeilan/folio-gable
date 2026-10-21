// Folio Gable — the Folio suite ribbon: tabs, captioned groups, large/small buttons,
// folding groups for narrow windows, display modes and KeyTips.
#pragma once
#include <QAction>
#include <QFrame>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QToolButton>
#include <QWidget>
#include <functional>
#include <vector>

class QHBoxLayout;
class QStackedWidget;
class QVBoxLayout;

namespace gable {

class Ribbon;
class RibbonGroup;

// Screentip text for an action: shown as the title (with its shortcut) and a description.
void setScreentip(QAction* a, const QString& description);
QString screentipHtml(QAction* a);
QString plainText(const QString& actionText);   // "&Undo…" -> "Undo"

class RibbonButton : public QToolButton {
    Q_OBJECT
public:
    enum Size { Large, Small, IconOnly };
    RibbonButton(QAction* a, Size size, const QString& label, const QString& iconName, QWidget* parent = nullptr);
    QAction* action() const { return act_; }
    Size size() const { return size_; }
    QString label() const { return label_; }
    QString iconName() const { return iconName_; }
    QString keytip;            // assigned by the KeyTips manager when not given explicitly
    bool fixedKeytip = false;
    void press();              // what a KeyTip does: run the action or open the menu
    void sync();
    bool splitBelow = false;   // large split button: lower part opens the menu
protected:
    void mousePressEvent(QMouseEvent* e) override;
private:
    QAction* act_;
    Size size_;
    QString label_, iconName_;
};

class RibbonGroup : public QWidget {
    Q_OBJECT
public:
    RibbonGroup(const QString& caption, Ribbon* ribbon, QAction* launcher = nullptr);
    // A large button (32 px icon over a wrapped label).
    RibbonButton* large(QAction* a, const QString& label = QString(), const QString& icon = QString());
    // A large button that opens a menu; with `def` the button runs it and an arrow opens the menu.
    RibbonButton* largeMenu(const QString& label, const QString& icon, QMenu* menu, QAction* def = nullptr);
    // A column of up to three small buttons (16 px icon beside the label); nullptr leaves a gap.
    std::vector<RibbonButton*> column(const std::vector<QAction*>& acts, bool iconOnly = false);
    RibbonButton* smallMenu(QMenu* menu, const QString& label, const QString& icon, QAction* def = nullptr);
    void columnWidgets(const std::vector<QWidget*>& ws);
    void addWidget(QWidget* w);
    void setCollapsed(bool on);
    bool collapsed() const { return collapsed_; }
    QString caption() const { return caption_; }
    std::vector<RibbonButton*> buttons;
    RibbonButton* launcherButton = nullptr;
    RibbonButton* compactButton() const { return compact_; }
    void openCompact();
private:
    QString caption_;
    Ribbon* ribbon_;
    QWidget* content_;          // body + caption (moves into the popup when folded)
    QHBoxLayout* body_;
    QVBoxLayout* outer_;
    RibbonButton* compact_ = nullptr;
    QAction* compactAct_ = nullptr;
    QString firstIcon_;
    bool collapsed_ = false;
};

struct RibbonTab {
    QString name, keytip;
    bool contextual = false;
    bool visible = true;
    QToolButton* button = nullptr;
    QWidget* page = nullptr;
    QHBoxLayout* lay = nullptr;
    std::vector<RibbonGroup*> groups;
    Ribbon* ribbon = nullptr;
    RibbonGroup* group(const QString& caption, QAction* launcher = nullptr);
};

class Ribbon : public QWidget {
    Q_OBJECT
public:
    enum Mode { AlwaysShow, TabsOnly, FullScreen };
    explicit Ribbon(QWidget* parent = nullptr);
    ~Ribbon() override;
    RibbonTab* addTab(const QString& name, const QString& keytip, bool contextual = false);
    void setTabVisible(RibbonTab* t, bool on);
    void select(RibbonTab* t);
    RibbonTab* current() const { return current_; }
    const std::vector<RibbonTab*>& tabs() const { return tabs_; }
    QToolButton* fileButton() const { return file_; }
    QToolButton* optionsButton() const { return options_; }
    Mode mode() const { return mode_; }
    void setMode(Mode m);
    void toggleCollapsed();     // Ctrl+F1 / double-click a tab
    bool bodyShown() const;
    void hideFloatingBody();
    void fit();
    void commandRan();          // a ribbon command ran: fold a floating ribbon away
    QWidget* body() const { return body_; }
    bool qatShown = true;
    std::function<void(bool)> onQatToggled;
signals:
    void fileClicked();
    void modeChanged(int mode);
protected:
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;
private:
    QWidget* tabRow_;
    QHBoxLayout* tabLay_;
    QWidget* body_;
    QStackedWidget* stack_;
    QToolButton* file_;
    QToolButton* options_;
    std::vector<RibbonTab*> tabs_;
    RibbonTab* current_ = nullptr;
    Mode mode_ = AlwaysShow;
    bool fitQueued_ = false;
    void placeFloatingBody();
    void showOptionsMenu();
};

// Alt (or F10) shows KeyTips over the File button, the tabs and the Quick Access Toolbar;
// typing a tab's letter shows the KeyTips of its commands.
class KeyTips : public QObject {
    Q_OBJECT
public:
    KeyTips(QWidget* window, Ribbon* ribbon);
    std::vector<std::pair<QString, QAbstractButton*>> qat;   // "1", "2", ... -> buttons
    std::function<bool()> allowed;       // false while a tool is mid-operation
    std::function<void()> onFile;        // File (F)
    bool active() const { return level_ > 0; }
    void start(RibbonTab* direct = nullptr);
    void stop();
protected:
    bool eventFilter(QObject* o, QEvent* e) override;
private:
    QWidget* win_;
    Ribbon* ribbon_;
    int level_ = 0;           // 0 off, 1 tabs, 2 commands
    bool altDown_ = false, altClean_ = false;
    QString typed_;
    std::vector<QPointer<QLabel>> badges_;
    struct Target { QString code; std::function<void()> run; QWidget* w; };
    std::vector<Target> targets_;
    void showTabs();
    void showCommands(RibbonTab* t);
    void clearBadges();
    void badge(QWidget* w, const QString& code, bool below);
    void typedKey(const QString& ch);
};

}  // namespace gable
