// Folio Gable — a native 3D modeler for Linux.
#include "ui/Icons.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include <QApplication>
#include <QTimer>
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
    QApplication::setApplicationVersion(GABLE_VERSION);
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
    // tests: start as if the desktop had a dark or loud colour theme (X2-01)
    if (qEnvironmentVariable("GABLE_DESKTOP_THEME") == "colour") {
        QPalette loud;
        for (auto r : {QPalette::Window, QPalette::Base, QPalette::Button, QPalette::AlternateBase, QPalette::ToolTipBase})
            loud.setColor(r, QColor(0x13, 0xd6, 0xc2));
        for (auto r : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::ToolTipText, QPalette::PlaceholderText})
            loud.setColor(r, QColor(0xff, 0x2f, 0xd0));
        loud.setColor(QPalette::Highlight, QColor(0xd6, 0xff, 0x13));
        QApplication::setPalette(loud);
    } else if (qEnvironmentVariable("GABLE_DESKTOP_THEME") == "dark") {
        QPalette dk;
        for (auto r : {QPalette::Window, QPalette::Base, QPalette::Button, QPalette::AlternateBase})
            dk.setColor(r, QColor(0x24, 0x24, 0x24));
        for (auto r : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            dk.setColor(r, QColor(0xee, 0xee, 0xec));
        QApplication::setPalette(dk);
    }
    gable::theme::install(app);
    QApplication::setWindowIcon(QIcon(gable::appIconPixmap(256)));
    gable::MainWindow w;
    w.show();
    QStringList args = app.arguments();
    if (args.contains("--audit")) {
        QTimer::singleShot(400, &w, [&w] { QCoreApplication::exit(gable::runAudit(&w) ? 1 : 0); });
        return app.exec();
    }
    if (args.contains("--bench") || args.contains("--bench-check")) {
        QTimer::singleShot(400, &w, [&w] { QCoreApplication::exit(gable::runBench(&w)); });
        return app.exec();
    }
    if (args.size() > 1) w.openFile(args[1]);
    return app.exec();
}
