// Folio Gable — a native 3D modeler for Linux.
#include "ui/Icons.h"
#include "ui/MainWindow.h"
#include <QApplication>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QSurfaceFormat>

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    QApplication::setApplicationName("Folio Gable");
    QApplication::setOrganizationName("Folio");
    QApplication::setApplicationVersion("1.2.0");
    QApplication::setDesktopFileName("folio-gable");
    // Carry settings and the crash-recovery file over from the pre-suite "Gable" name.
    {
        QString cfg = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        QString oldCfg = cfg + "/Gable/Gable.conf", newCfg = cfg + "/Folio/Folio Gable.conf";
        if (QFile::exists(oldCfg) && !QFile::exists(newCfg)) {
            QDir().mkpath(QFileInfo(newCfg).absolutePath());
            QFile::copy(oldCfg, newCfg);
        }
        QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        QString oldAuto = data + "/Gable/Gable/autosave.gbl";
        QString newDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (QFile::exists(oldAuto) && !QFile::exists(newDir + "/autosave.gbl")) {
            QDir().mkpath(newDir);
            QFile::rename(oldAuto, newDir + "/autosave.gbl");
        }
    }
    QApplication::setWindowIcon(QIcon(gable::appIconPixmap(256)));
    gable::MainWindow w;
    w.show();
    QStringList args = app.arguments();
    if (args.size() > 1) w.openFile(args[1]);
    return app.exec();
}
