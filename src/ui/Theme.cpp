#include "ui/Theme.h"
#include <QApplication>
#include <QFile>
#include <QFont>
#include <QHash>
#include <QPainter>
#include <QPalette>
#include <QSvgRenderer>
#include <algorithm>
#include <cmath>

namespace gable {
namespace theme {

static QString hex(const QColor& c) { return c.name(QColor::HexRgb); }

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
#folioTabs QToolButton[active="true"] { border-bottom: 3px solid @accent; color: @accent; font-weight: bold; }
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

/* scene tabs: like the sheet tabs of Folio Sheets */
QTabBar#sceneTabs { background: #f3f3f3; }
QTabBar#sceneTabs::tab { background: transparent; border: none; border-right: 1px solid #d0d0d0; padding: 3px 14px;
  color: #333333; min-height: 18px; }
QTabBar#sceneTabs::tab:hover { background: #e6e6e6; }
QTabBar#sceneTabs::tab:selected { background: #ffffff; color: @accent_dark; font-weight: bold; border-bottom: 2px solid @accent; }

/* task pane (tray) */
QDockWidget { titlebar-close-icon: none; color: #262626; }
QDockWidget::title { background: #ffffff; padding: 6px 8px; border-left: 1px solid @ribbon_line; font-size: 11pt; }
#trayPane, #trayBody, #trayBody QWidget { background: #ffffff; }
#trayBody QLineEdit, #trayBody QComboBox, #trayBody QSpinBox, #trayBody QDoubleSpinBox { background: #ffffff; }
QToolButton[paneHeader="true"] { background: #ffffff; border: none; border-bottom: 1px solid #ececec; text-align: left;
  padding: 5px 4px; font-weight: 600; color: #262626; }
QToolButton[paneHeader="true"]:hover { background: @tint; }
QToolButton[paneHeader="true"]:checked { color: @accent_dark; }

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
QDialog QTabBar::tab:selected { border-bottom: 2px solid @accent; color: @accent_dark; }
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

void install(QApplication& app) {
    QFont f = app.font();
    f.setFamilies({"Segoe UI", "Noto Sans", "Open Sans", "Ubuntu", "Cantarell", "DejaVu Sans"});
    f.setPointSizeF(9);
    app.setFont(f);
    QPalette p = app.palette();
    p.setColor(QPalette::Highlight, accent());
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, accent());
    p.setColor(QPalette::Window, ribbonBg());
    p.setColor(QPalette::ToolTipBase, Qt::white);
    p.setColor(QPalette::ToolTipText, ribbonFg());
    app.setPalette(p);
    app.setStyleSheet(styleSheet());
}

static QString pathFor(const QString& name, bool small) {
    QString base = ":/ribbon/" + name;
    if (small && QFile::exists(base + "-sm.svg")) return base + "-sm.svg";
    return base + ".svg";
}

bool hasIcon(const QString& name) { return QFile::exists(":/ribbon/" + name + ".svg"); }

QPixmap pixmap(const QString& name, int px, qreal dpr, bool white) {
    static QHash<QString, QPixmap> cache;
    QString key = QString("%1|%2|%3|%4").arg(name).arg(px).arg(dpr).arg(white);
    auto it = cache.find(key);
    if (it != cache.end()) return *it;
    int dev = qRound(px * dpr);
    QImage img(dev, dev, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QSvgRenderer r(pathFor(name, px <= 20));
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

QIcon icon(const QString& name, bool large) {
    QIcon ic;
    for (int px : {16, 20, 24, 32, 48}) {
        if (!large && px > 24) continue;
        for (qreal dpr : {1.0, 2.0}) ic.addPixmap(pixmap(name, px, dpr));
    }
    return ic;
}

QIcon whiteIcon(const QString& name, int px) {
    QIcon ic;
    for (qreal dpr : {1.0, 2.0}) ic.addPixmap(pixmap(name, px, dpr, true));
    return ic;
}

}  // namespace theme
}  // namespace gable
