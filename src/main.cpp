// Gable — a native 3D modeler for Linux.
#include "ui/Icons.h"
#include "ui/MainWindow.h"
#include <QApplication>
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
    QApplication::setApplicationName("Gable");
    QApplication::setOrganizationName("Gable");
    QApplication::setApplicationVersion("1.1.0");
    QApplication::setDesktopFileName("gable");
    QApplication::setWindowIcon(QIcon(gable::appIconPixmap(256)));
    gable::MainWindow w;
    w.show();
    QStringList args = app.arguments();
    if (args.size() > 1) w.openFile(args[1]);
    return app.exec();
}
