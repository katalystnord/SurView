#include <QApplication>
#include <QSurfaceFormat>

#include <QVTKOpenGLNativeWidget.h>

#include "gui/MainWindow.h"

int main(int argc, char *argv[])
{
    // Must be set before any QVTKOpenGLNativeWidget is created.
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SurView DIC"));
    QApplication::setOrganizationName(QStringLiteral("Katalyst Nord"));

    MainWindow window;
    window.show();

    return app.exec();
}
