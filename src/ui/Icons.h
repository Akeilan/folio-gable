#pragma once
#include <QIcon>
#include <QString>

namespace gable {
// Programmatically drawn tool icons (no external assets needed).
QIcon toolIcon(const QString& name);
QPixmap appIconPixmap(int size);
}  // namespace gable
