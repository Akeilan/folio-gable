// Folio Gable — the Folio suite look (purple edition): colours, style sheet, ribbon icons.
#pragma once
#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

class QApplication;

namespace gable {
namespace theme {

// Light or dark, as the desktop is set (S-08): the freedesktop settings portal, then GNOME's
// setting; FOLIO_THEME=dark|light overrides. Desktop changes apply while running.
bool isDark();
void setDark(bool dark);            // re-styles the whole application (also used by tests)
bool systemPrefersDark();

// Suite palette (Folio Page's chrome with Gable's purple accent); dark mode has its own.
inline QColor accent() { return QColor(0x6b, 0x3f, 0xa0); }      // title bar, active tab, default buttons
inline QColor accentDark() { return QColor(0x4b, 0x2a, 0x78); }
inline QColor accentHover() { return QColor(0x5a, 0x33, 0x8a); }
inline QColor accentText() { return isDark() ? QColor(0xc4, 0xa6, 0xf0) : accent(); }   // accent used for text
inline QColor checkedBg() { return isDark() ? QColor(0x4a, 0x3a, 0x66) : QColor(0xdd, 0xd0, 0xee); }
inline QColor checkedBorder() { return isDark() ? QColor(0x7a, 0x5c, 0xb0) : QColor(0xb4, 0x9c, 0xd8); }
inline QColor tintBg() { return isDark() ? QColor(0x36, 0x30, 0x42) : QColor(0xf4, 0xef, 0xfa); }
inline QColor ribbonBg() { return isDark() ? QColor(0x2b, 0x2b, 0x2b) : QColor(0xf3, 0xf3, 0xf3); }
inline QColor ribbonFg() { return isDark() ? QColor(0xf0, 0xf0, 0xf0) : QColor(0x26, 0x26, 0x26); }
inline QColor ribbonLine() { return isDark() ? QColor(0x40, 0x40, 0x40) : QColor(0xd6, 0xd6, 0xd6); }
inline QColor captionFg() { return isDark() ? QColor(0xb0, 0xb0, 0xb0) : QColor(0x66, 0x66, 0x66); }
inline QColor hoverBg() { return isDark() ? QColor(0x3d, 0x3d, 0x3d) : QColor(0xe1, 0xe1, 0xe1); }
inline QColor pressBg() { return isDark() ? QColor(0x4a, 0x4a, 0x4a) : QColor(0xc8, 0xc8, 0xc8); }

// Applies fonts, palette and the application-wide style sheet, and starts following the desktop.
void install(QApplication& app);
// Re-applies palette and style sheet for the current mode.
void restyle();

// Ribbon icon by name (resources/ribbon). Small icons (<= 20 px) use the "-sm" artwork.
QPixmap pixmap(const QString& name, int px, qreal dpr = 1.0, bool white = false);
QIcon icon(const QString& name, bool large = true);
QIcon whiteIcon(const QString& name, int px = 16);
bool hasIcon(const QString& name);

}  // namespace theme
}  // namespace gable
