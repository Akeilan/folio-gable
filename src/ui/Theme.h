// Folio Gable — the Folio suite look (purple edition): colours, style sheet, ribbon icons.
#pragma once
#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

class QApplication;

namespace gable {
namespace theme {

// Suite palette (Folio Writer's chrome with Gable's purple accent).
inline QColor accent() { return QColor(0x6b, 0x3f, 0xa0); }      // title bar, active tab, default buttons
inline QColor accentDark() { return QColor(0x4b, 0x2a, 0x78); }
inline QColor accentHover() { return QColor(0x5a, 0x33, 0x8a); }
inline QColor checkedBg() { return QColor(0xdd, 0xd0, 0xee); }
inline QColor checkedBorder() { return QColor(0xb4, 0x9c, 0xd8); }
inline QColor tintBg() { return QColor(0xf4, 0xef, 0xfa); }
inline QColor ribbonBg() { return QColor(0xf3, 0xf3, 0xf3); }
inline QColor ribbonFg() { return QColor(0x26, 0x26, 0x26); }
inline QColor ribbonLine() { return QColor(0xd6, 0xd6, 0xd6); }
inline QColor captionFg() { return QColor(0x66, 0x66, 0x66); }
inline QColor hoverBg() { return QColor(0xe1, 0xe1, 0xe1); }
inline QColor pressBg() { return QColor(0xc8, 0xc8, 0xc8); }

// Applies fonts, palette and the application-wide style sheet.
void install(QApplication& app);

// Ribbon icon by name (resources/ribbon). Small icons (<= 20 px) use the "-sm" artwork.
QPixmap pixmap(const QString& name, int px, qreal dpr = 1.0, bool white = false);
QIcon icon(const QString& name, bool large = true);
QIcon whiteIcon(const QString& name, int px = 16);
bool hasIcon(const QString& name);

}  // namespace theme
}  // namespace gable
