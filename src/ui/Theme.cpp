#include "ui/Theme.h"
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QIconEngine>
#include <QProcess>
#include <QRegularExpression>
#include <QStyle>
#include <QWidget>
#include <QApplication>
#include <QFile>
#include <QFont>
#include <QHash>
#include <QPainter>
#include <QPalette>
#include <QStyleFactory>
#include <QStyle>
#include <QSvgRenderer>
#include <algorithm>
#include <cmath>

namespace gable {
namespace theme {

static QString hex(const QColor& c) { return c.name(QColor::HexRgb); }

// Dark mode: the chrome goes dark (like Office's Black theme); the model keeps its own colours.
static QString darkSheet() {
    return R"QSS(
QToolTip { background: #2b2b2b; color: #f0f0f0; border: 1px solid #555555; }
QToolButton[ribbonBtn="true"]:disabled { color: #6e6e6e; }
QFrame#ribbonPopup { border-color: #555555; }
QLabel[keytip="true"] { background: #f0f0f0; color: #1f1f1f; border-color: #ffffff; }
#folioBackstage { background: #1f1f1f; }
#bsBody, #bsBody > QWidget, QScrollArea#bsScroll, QScrollArea#bsScroll > QWidget > QWidget { background: #1f1f1f; }
QLabel[bsTitle="true"], QLabel[bsSub="true"], QLabel[bsCard="true"] { color: #f0f0f0; }
QLabel[dim="true"] { color: #a0a0a0; }
QToolButton[bsTile="true"], QToolButton[bsPlace="true"] { color: #f0f0f0; }
QPushButton[bsButton="true"] { background: #2b2b2b; border-color: #555555; color: #f0f0f0; }
QTreeWidget#bsRecent { background: #1f1f1f; color: #f0f0f0; }
QTreeWidget#bsRecent::item:selected { color: #ffffff; }
QStatusBar, QStatusBar QLabel { color: #d0d0d0; }
QStatusBar QLineEdit { background: #1f1f1f; color: #f0f0f0; border-color: #4a4a4a; }
QTabBar#sceneTabs { background: #2b2b2b; }
QTabBar#sceneTabs::tab { color: #e0e0e0; border-right-color: #404040; }
QTabBar#sceneTabs::tab:hover { background: #3a3a3a; }
QTabBar#sceneTabs::tab:selected { background: #1f1f1f; }
QDockWidget { color: #f0f0f0; }
QDockWidget::title { background: #252525; }
#trayPane, #trayBody, #trayBody QWidget { background: #252525; color: #f0f0f0; }
#trayBody QLineEdit, #trayBody QComboBox, #trayBody QSpinBox, #trayBody QDoubleSpinBox { background: #1f1f1f; color: #f0f0f0; }
QToolButton[paneHeader="true"] { background: #252525; border-bottom-color: #333333; color: #f0f0f0; }
QMenu { background: #2b2b2b; border-color: #555555; }
QMenu::item { color: #f0f0f0; }
QMenu::item:selected { color: #ffffff; }
QMenu::item:disabled { color: #6e6e6e; }
QMenu::separator { background: #404040; }
QDialog { background: #2b2b2b; color: #f0f0f0; }
QDialog QPushButton { background: #333333; border-color: #555555; color: #f0f0f0; }
QDialog QTabWidget::pane { border-color: #404040; background: #2b2b2b; }
QSlider::handle:horizontal { background: #d0d0d0; border-color: #777777; }
QSlider::groove:horizontal { background: #4a4a4a; }
QScrollBar::handle:vertical, QScrollBar::handle:horizontal { background: #555555; }
QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover { background: #6e6e6e; }
)QSS";
}

static QString styleSheet() {
    QString s = R"QSS(
/* ---------- Folio suite chrome (Gable: purple) ---------- */
QMainWindow { background: @ribbon_bg; }
QToolTip { background: #ffffff; color: #262626; border: 1px solid #b8b8b8; padding: 4px 6px; }

/* title bar */
#folioTitleBar { background: @accent; }
#folioTitleBar QLabel#folioTitle { color: #ffffff; font-size: 9pt; }
#folioTitleBar QToolButton { background: transparent; border: none; color: #ffffff; padding: 0 4px;
  min-height: 30px; min-width: 30px; }
#folioTitleBar QToolButton:hover { background: rgba(255,255,255,0.15); }
#folioTitleBar QToolButton:pressed { background: rgba(255,255,255,0.25); }
#folioTitleBar QToolButton#closeButton:hover { background: #e81123; }
#folioTitleBar QToolButton:disabled { color: rgba(255,255,255,0.45); }
#folioTitleBar QToolButton::menu-indicator { image: none; width: 0; }
QLineEdit#folioSearch { background: rgba(255,255,255,0.18); color: #ffffff; border: 1px solid rgba(255,255,255,0.25);
  border-radius: 4px; min-height: 24px; padding: 0 8px; selection-background-color: @accent_dark; }
QLineEdit#folioSearch:focus { background: #ffffff; color: #222222; }

/* ribbon tabs */
#folioTabs { background: @ribbon_bg; }
#folioTabs QToolButton { background: transparent; border: none; border-bottom: 3px solid transparent;
  color: @ribbon_fg; padding: 6px 10px 4px 10px; font-size: 9.5pt; }
#folioTabs QToolButton:hover { background: @hover_bg; }
#folioTabs QToolButton[active="true"] { border-bottom: 3px solid @accent_line; color: @accent_text; font-weight: bold; }
#folioTabs QToolButton[contextual="true"] { color: #8a4a00; }
#folioTabs QToolButton#ribbonOptions { border: none; padding: 2px 6px; }

/* ribbon body */
#folioRibbonBody { background: @ribbon_bg; border-bottom: 1px solid @ribbon_line; }
QWidget[ribbonPage="true"] { background: @ribbon_bg; }
QLabel[caption="true"] { color: @caption_fg; font-size: 7.5pt; }
QFrame[ribbonSep="true"] { background: @ribbon_line; min-width: 1px; max-width: 1px; margin: 6px 2px 6px 2px; }
QToolButton[ribbonBtn="true"] { background: transparent; border: 1px solid transparent; border-radius: 3px;
  color: @ribbon_fg; padding: 1px 3px; font-size: 8.5pt; }
QToolButton[ribbonBtn="true"]:hover { background: @hover_bg; border-color: @hover_bg; }
QToolButton[ribbonBtn="true"]:pressed { background: @press_bg; }
QToolButton[ribbonBtn="true"]:checked { background: @checked_bg; border-color: @checked_border; }
QToolButton[ribbonBtn="true"]:disabled { color: #a8a8a8; }
QToolButton[ribbonBtn="true"][large="true"] { padding: 3px 3px 2px 3px; }
QToolButton[ribbonBtn="true"]::menu-button { border: none; width: 12px; }
QToolButton[ribbonBtn="true"]::menu-arrow { width: 7px; height: 7px; }
QToolButton[ribbonBtn="true"][large="true"]::menu-indicator { image: none; width: 0; }
QToolButton[launcher="true"] { background: transparent; border: none; padding: 0; }
QToolButton[launcher="true"]:hover { background: @hover_bg; }
QFrame#ribbonPopup { background: @ribbon_bg; border: 1px solid #b8b8b8; }

/* KeyTips */
QLabel[keytip="true"] { background: #3a3a3a; color: #ffffff; font-weight: bold; font-size: 7.5pt;
  padding: 0 4px; border: 1px solid #222222; border-radius: 2px; }

/* backstage */
#folioBackstage { background: #ffffff; }
#bsNav { background: @accent; }
#bsNav QPushButton { background: transparent; color: #ffffff; border: none; text-align: left;
  padding: 9px 22px; font-size: 10pt; }
#bsNav QPushButton:hover { background: rgba(255,255,255,0.14); }
#bsNav QPushButton[active="true"] { background: rgba(0,0,0,0.25); }
#bsNav QPushButton#bsBack { padding: 12px 18px; }
#bsNav QFrame[bsSep="true"] { background: rgba(255,255,255,0.30); max-height: 1px; min-height: 1px; margin: 4px 20px; }
#bsBody, #bsBody > QWidget, QScrollArea#bsScroll, QScrollArea#bsScroll > QWidget > QWidget { background: #ffffff; }
QLabel[bsTitle="true"] { font-size: 22pt; font-weight: 300; color: #262626; }
QLabel[bsSub="true"] { font-size: 15pt; font-weight: 300; color: #262626; }
QLabel[bsCard="true"] { font-size: 11pt; font-weight: 600; color: #262626; }
QLabel[dim="true"] { color: #6b6b6b; }
QToolButton[bsTile="true"] { background: transparent; border: 1px solid transparent; padding: 6px; color: #262626; }
QToolButton[bsTile="true"]:hover { background: @tint; border-color: @checked_border; }
QToolButton[bsPlace="true"] { background: transparent; border: 1px solid transparent; padding: 8px 12px;
  text-align: left; color: #262626; font-size: 10pt; }
QToolButton[bsPlace="true"]:hover { background: @tint; }
QToolButton[bsPlace="true"]:checked { background: @checked_bg; }
QPushButton[bsButton="true"] { background: #ffffff; border: 1px solid #c8c8c8; border-radius: 2px; padding: 8px 14px;
  color: #262626; text-align: left; }
QPushButton[bsButton="true"]:hover { background: @tint; border-color: @checked_border; }
QTreeWidget#bsRecent { border: none; background: #ffffff; }
QTreeWidget#bsRecent::item { padding: 4px 2px; }
QTreeWidget#bsRecent::item:hover { background: @tint; }
QTreeWidget#bsRecent::item:selected { background: @checked_bg; color: #1a1a1a; }

/* status bar */
QStatusBar { background: @ribbon_bg; color: #444444; border-top: 1px solid @ribbon_line; min-height: 24px; }
QStatusBar QLabel { color: #444444; font-size: 8.5pt; padding: 0 4px; }
QStatusBar::item { border: none; }
QStatusBar QLineEdit { background: #ffffff; border: 1px solid #c6c6c6; border-radius: 2px; min-height: 18px; padding: 0 4px; }
QStatusBar QLineEdit:focus { border-color: @accent; }

/* scene tabs: like the sheet tabs of Folio Sheet */
QTabBar#sceneTabs { background: #f3f3f3; }
QTabBar#sceneTabs::tab { background: transparent; border: none; border-right: 1px solid #d0d0d0; padding: 3px 14px;
  color: #333333; min-height: 18px; }
QTabBar#sceneTabs::tab:hover { background: #e6e6e6; }
QTabBar#sceneTabs::tab:selected { background: #ffffff; color: @accent_text; font-weight: bold; border-bottom: 2px solid @accent_line; }

/* task pane (tray) */
QDockWidget { titlebar-close-icon: none; color: #262626; }
QDockWidget::title { background: #ffffff; padding: 6px 8px; border-left: 1px solid @ribbon_line; font-size: 11pt; }
#trayPane, #trayBody, #trayBody QWidget { background: #ffffff; }
#trayBody QLineEdit, #trayBody QComboBox, #trayBody QSpinBox, #trayBody QDoubleSpinBox { background: #ffffff; }
QToolButton[paneHeader="true"] { background: #ffffff; border: none; border-bottom: 1px solid #ececec; text-align: left;
  padding: 5px 4px; font-weight: 600; color: #262626; }
QToolButton[paneHeader="true"]:hover { background: @tint; }
QToolButton[paneHeader="true"]:checked { color: @accent_text; }

/* menus, dialogs, controls */
QMenu { background: #ffffff; border: 1px solid #c6c6c6; padding: 3px 0; }
QMenu::item { padding: 4px 22px 4px 26px; color: #262626; }
QMenu::item:selected { background: @checked_bg; color: #1a1a1a; }
QMenu::item:disabled { color: #a0a0a0; }
QMenu::separator { height: 1px; background: #e2e2e2; margin: 3px 8px; }
QMenu::icon { padding-left: 4px; }
QDialog { background: #f3f3f3; }
QDialog QPushButton { min-height: 22px; padding: 2px 14px; border-radius: 2px; border: 1px solid #b8b8b8; background: #fdfdfd; }
QDialog QPushButton:hover { background: @tint; border-color: @checked_border; }
QPushButton:default { background: @accent; color: #ffffff; border: 1px solid @accent_dark; min-width: 72px; }
QPushButton:default:hover { background: @accent_dark; }
QDialog QTabWidget::pane { border: 1px solid #d6d6d6; background: #f3f3f3; }
QDialog QTabBar::tab { padding: 4px 12px; background: transparent; border: none; }
QDialog QTabBar::tab:selected { border-bottom: 2px solid @accent_line; color: @accent_text; }
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: @accent; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked { }
QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
QSlider::handle:horizontal { background: #ffffff; border: 1px solid #9a9a9a; width: 10px; margin: -5px 0; border-radius: 5px; }
QSlider::groove:horizontal { background: #d6d6d6; height: 4px; border-radius: 2px; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: #c6c6c6; border-radius: 4px; min-height: 24px; margin: 1px; }
QScrollBar::handle:vertical:hover { background: #a8a8a8; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle:horizontal { background: #c6c6c6; border-radius: 4px; min-width: 24px; margin: 1px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)QSS";
    if (isDark()) s += darkSheet();
    s.replace("@accent_text", isDark() ? "#c4a6f0" : hex(accentDark()));
    s.replace("@accent_line", isDark() ? "#9b74d6" : hex(accent()));
    s.replace("@accent_dark", hex(accentDark()));
    s.replace("@accent", hex(accent()));
    s.replace("@checked_bg", hex(checkedBg()));
    s.replace("@checked_border", hex(checkedBorder()));
    s.replace("@tint", hex(tintBg()));
    s.replace("@ribbon_bg", hex(ribbonBg()));
    s.replace("@ribbon_fg", hex(ribbonFg()));
    s.replace("@ribbon_line", hex(ribbonLine()));
    s.replace("@caption_fg", hex(captionFg()));
    s.replace("@hover_bg", hex(hoverBg()));
    s.replace("@press_bg", hex(pressBg()));
    return s;
}

// ------------------------------------------------------------------ light or dark

static int gDark = -1;   // -1 not decided yet

static int portalScheme() {
    // org.freedesktop.portal.Settings.Read("org.freedesktop.appearance", "color-scheme"):
    // 1 prefer dark, 2 prefer light, 0 no preference; -1 when there is no portal
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return -1;
    QDBusInterface iface("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                         "org.freedesktop.portal.Settings", bus);
    if (!iface.isValid()) return -1;
    iface.setTimeout(400);
    QDBusMessage r = iface.call("Read", "org.freedesktop.appearance", "color-scheme");
    if (r.type() != QDBusMessage::ReplyMessage || r.arguments().isEmpty()) return -1;
    QVariant v = r.arguments().first();
    while (v.canConvert<QDBusVariant>()) v = v.value<QDBusVariant>().variant();
    bool ok = false;
    int n = v.toInt(&ok);
    return ok ? n : -1;
}

static int gnomeScheme() {
    QProcess p;
    p.start("gsettings", {"get", "org.gnome.desktop.interface", "color-scheme"});
    if (p.waitForFinished(600) && p.exitCode() == 0) {
        QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        if (out.contains("prefer-dark")) return 1;
        if (out.contains("prefer-light")) return 0;
    }
    QProcess t;
    t.start("gsettings", {"get", "org.gnome.desktop.interface", "gtk-theme"});
    if (t.waitForFinished(600) && t.exitCode() == 0) {
        QString out = QString::fromUtf8(t.readAllStandardOutput()).trimmed().toLower();
        if (!out.isEmpty()) return out.contains("dark") ? 1 : 0;
    }
    return -1;
}

bool systemPrefersDark() {
    QString env = qEnvironmentVariable("FOLIO_THEME").trimmed().toLower();
    if (env == "dark" || env == "black") return true;
    if (env == "light" || env == "colorful" || env == "white") return false;
    int p = portalScheme();
    if (p == 1 || p == 2) return p == 1;
    int g = gnomeScheme();
    if (g >= 0) return g == 1;
    return false;
}

bool isDark() {
    if (gDark < 0) gDark = systemPrefersDark() ? 1 : 0;
    return gDark == 1;
}

// X2-01: light mode has its own palette too, not the desktop's: a dark or coloured desktop theme
// (Qt style or palette set system-wide) never shows through Gable's windows and dialogs.
static QPalette lightPalette() {
    QPalette p;
    QColor win(0xf3, 0xf3, 0xf3), text(0x26, 0x26, 0x26), base(Qt::white), btn(0xfd, 0xfd, 0xfd);
    p.setColor(QPalette::Window, win);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, QColor(0xf5, 0xf5, 0xf5));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, btn);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Light, Qt::white);
    p.setColor(QPalette::Midlight, QColor(0xe3, 0xe3, 0xe3));
    p.setColor(QPalette::Mid, QColor(0xa0, 0xa0, 0xa0));
    p.setColor(QPalette::Dark, QColor(0x80, 0x80, 0x80));
    p.setColor(QPalette::Shadow, QColor(0x40, 0x40, 0x40));
    p.setColor(QPalette::PlaceholderText, QColor(0x80, 0x80, 0x80));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, QColor(0xa0, 0xa0, 0xa0));
    p.setColor(QPalette::Disabled, QPalette::Base, QColor(0xf3, 0xf3, 0xf3));
    p.setColor(QPalette::Disabled, QPalette::Button, QColor(0xf3, 0xf3, 0xf3));
    return p;
}

static QPalette darkPalette() {
    QPalette p;
    QColor win(0x2b, 0x2b, 0x2b), base(0x1f, 0x1f, 0x1f), text(0xf0, 0xf0, 0xf0), btn(0x33, 0x33, 0x33);
    p.setColor(QPalette::Window, win);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, QColor(0x26, 0x26, 0x26));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, btn);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Light, QColor(0x4a, 0x4a, 0x4a));
    p.setColor(QPalette::Midlight, QColor(0x3d, 0x3d, 0x3d));
    p.setColor(QPalette::Mid, QColor(0x8a, 0x8a, 0x8a));
    p.setColor(QPalette::Dark, QColor(0x18, 0x18, 0x18));
    p.setColor(QPalette::Shadow, Qt::black);
    p.setColor(QPalette::PlaceholderText, QColor(0x8a, 0x8a, 0x8a));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, QColor(0x6e, 0x6e, 0x6e));
    p.setColor(QPalette::Disabled, QPalette::Base, QColor(0x26, 0x26, 0x26));
    return p;
}

void restyle() {
    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app) return;
    QPalette p = isDark() ? darkPalette() : lightPalette();
    p.setColor(QPalette::Highlight, accent());
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, isDark() ? QColor(0xc4, 0xa6, 0xf0) : accent());
    p.setColor(QPalette::Window, ribbonBg());
    p.setColor(QPalette::ToolTipBase, isDark() ? QColor(0x2b, 0x2b, 0x2b) : QColor(Qt::white));
    p.setColor(QPalette::ToolTipText, ribbonFg());
    // X3-05: a window without focus (Wayland shows dialogs that way) keeps exactly the same colours
    for (int r = 0; r < QPalette::NColorRoles; ++r) {
        auto role = (QPalette::ColorRole)r;
        if (role == QPalette::NoRole) continue;
        p.setBrush(QPalette::Inactive, role, p.brush(QPalette::Active, role));
    }
    app->setPalette(p);
    app->setStyleSheet(styleSheet());
    for (QWidget* w : QApplication::allWidgets()) w->update();
}

void setDark(bool dark) {
    if (gDark == (dark ? 1 : 0)) return;
    gDark = dark ? 1 : 0;
    restyle();
}

// Follows the desktop: the settings portal's SettingChanged signal.
class Follower : public QObject {
    Q_OBJECT
public:
    explicit Follower(QObject* parent) : QObject(parent) {}
    void start() {
        if (!qEnvironmentVariableIsEmpty("FOLIO_THEME")) return;
        QDBusConnection::sessionBus().connect("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                              "org.freedesktop.portal.Settings", "SettingChanged", this,
                                              SLOT(changed(QString, QString, QDBusVariant)));
    }
public slots:
    void changed(const QString& ns, const QString& key, const QDBusVariant& v) {
        if (ns != "org.freedesktop.appearance" || key != "color-scheme") return;
        QVariant x = v.variant();
        while (x.canConvert<QDBusVariant>()) x = x.value<QDBusVariant>().variant();
        int n = x.toInt();
        if (n == 1 || n == 2) setDark(n == 1);
        else setDark(systemPrefersDark());
    }
};

void install(QApplication& app) {
    QFont f = app.font();
    f.setFamilies({"Segoe UI", "Noto Sans", "Open Sans", "Ubuntu", "Cantarell", "DejaVu Sans"});
    f.setPointSizeF(9);
    app.setFont(f);
    // the same widget style everywhere, whatever Qt style the desktop picks (X2-01)
    if (QStyle* st = QStyleFactory::create("Fusion")) app.setStyle(st);
    isDark();
    restyle();
    auto* fol = new Follower(&app);
    fol->start();
}

static QString pathFor(const QString& name, bool small) {
    QString base = ":/ribbon/" + name;
    if (small && QFile::exists(base + "-sm.svg")) return base + "-sm.svg";
    return base + ".svg";
}

bool hasIcon(const QString& name) { return QFile::exists(":/ribbon/" + name + ".svg"); }

// Dark mode artwork: greys turn round (ink light, white bodies dark), the purple tints deepen.
static QByteArray darkSvg(QByteArray svg) {
    static const QRegularExpression hexRe("#([0-9a-fA-F]{6}|[0-9a-fA-F]{3})(?![0-9a-fA-F])");
    static const QHash<QString, QString> accent = {
        {"e6dcf3", "4a3a66"}, {"c9b4e6", "5d4785"}, {"7a4cc0", "a07ae0"}, {"4f2a85", "b89ae6"},
    };
    QString t = QString::fromUtf8(svg), out;
    int last = 0;
    auto it = hexRe.globalMatch(t);
    while (it.hasNext()) {
        auto m = it.next();
        QString h = m.captured(1).toLower();
        if (h.size() == 3) h = QString("%1%1%2%2%3%3").arg(h[0]).arg(h[1]).arg(h[2]);
        QString rep = m.captured(0);
        if (accent.contains(h)) rep = "#" + accent[h];
        else {
            int r = h.mid(0, 2).toInt(nullptr, 16), g = h.mid(2, 2).toInt(nullptr, 16), b = h.mid(4, 2).toInt(nullptr, 16);
            if (std::max({r, g, b}) - std::min({r, g, b}) <= 6) {
                int v = std::clamp(int(std::lround(263.6 - 0.806 * (r + g + b) / 3.0)), 0, 255);
                rep = QColor(v, v, v).name();
            }
        }
        out += t.mid(last, m.capturedStart() - last) + rep;
        last = m.capturedEnd();
    }
    out += t.mid(last);
    return out.toUtf8();
}

QPixmap pixmap(const QString& name, int px, qreal dpr, bool white) {
    static QHash<QString, QPixmap> cache;
    bool dark = isDark() && !white;
    QString key = QString("%1|%2|%3|%4|%5").arg(name).arg(px).arg(dpr).arg(white).arg(dark);
    auto it = cache.find(key);
    if (it != cache.end()) return *it;
    int dev = qRound(px * dpr);
    QImage img(dev, dev, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QByteArray svg;
    {
        QFile f(pathFor(name, px <= 20));
        if (f.open(QIODevice::ReadOnly)) svg = f.readAll();
    }
    if (dark) svg = darkSvg(svg);
    QSvgRenderer r(svg);
    if (r.isValid()) {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        r.render(&p, QRectF(0, 0, dev, dev));
    }
    if (white) {
        // For the accent-coloured title bar: outlines become white, light fills fade out,
        // so the icon reads as a line drawing (like the suite's white QAT icons).
        QImage a = img.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < a.height(); ++y) {
            QRgb* row = reinterpret_cast<QRgb*>(a.scanLine(y));
            for (int x = 0; x < a.width(); ++x) {
                QRgb c = row[x];
                int al = qAlpha(c);
                if (!al) continue;
                double lum = (0.299 * qRed(c) + 0.587 * qGreen(c) + 0.114 * qBlue(c)) / 255.0;
                double k = std::pow(std::clamp(1.0 - lum, 0.0, 1.0) / 0.8, 0.8);
                int na = std::clamp(int(al * std::min(1.0, k)), 0, 255);
                row[x] = qRgba(255, 255, 255, na);
            }
        }
        img = a.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    QPixmap pm = QPixmap::fromImage(img);
    pm.setDevicePixelRatio(dpr);
    cache.insert(key, pm);
    return pm;
}

// An icon drawn from the artwork at paint time, so it follows light and dark without being set again.
class ThemedIconEngine : public QIconEngine {
public:
    ThemedIconEngine(const QString& n, bool large) : name_(n), large_(large) {}
    QIconEngine* clone() const override { return new ThemedIconEngine(name_, large_); }
    void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        qreal dpr = p->device() ? p->device()->devicePixelRatioF() : 1.0;
        p->drawPixmap(rect, pixmapAt(rect.size(), mode, state, dpr));
    }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        return pixmapAt(size, mode, state, 1.0);
    }
    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
        QPixmap pm = pixmapAt(size, mode, state, scale);
        return pm;
    }
    QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override {
        int s = std::min(size.width(), size.height());
        return QSize(s, s);
    }
    QList<QSize> availableSizes(QIcon::Mode, QIcon::State) override {
        QList<QSize> l;
        for (int px : {16, 20, 24, 32, 48}) if (large_ || px <= 24) l << QSize(px, px);
        return l;
    }
    QString key() const override { return "folio-themed"; }
private:
    QPixmap pixmapAt(const QSize& size, QIcon::Mode mode, QIcon::State, qreal dpr) {
        int px = std::max(8, std::min(size.width(), size.height()));
        QPixmap pm = theme::pixmap(name_, px, dpr);
        if (mode != QIcon::Disabled) return pm;
        // disabled: faded and without colour, as the style would make it
        QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < img.height(); ++y) {
            QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                int g = qGray(row[x]);
                g = isDark() ? g / 2 + 40 : 128 + g / 3;
                row[x] = qRgba(g, g, g, qAlpha(row[x]) * 45 / 100);
            }
        }
        QPixmap out = QPixmap::fromImage(img);
        out.setDevicePixelRatio(dpr);
        return out;
    }
    QString name_;
    bool large_;
};

QIcon icon(const QString& name, bool large) { return QIcon(new ThemedIconEngine(name, large)); }

QIcon whiteIcon(const QString& name, int px) {
    QIcon ic;
    for (qreal dpr : {1.0, 2.0}) ic.addPixmap(pixmap(name, px, dpr, true));
    return ic;
}

}  // namespace theme
}  // namespace gable

#include "Theme.moc"
