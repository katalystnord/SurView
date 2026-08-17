#include <QApplication>
#include <QCommandLineParser>
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
    QApplication::setApplicationVersion(QStringLiteral(SURVIEW_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("SurView DIC — Digital Image Correlation workspace."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("reference"),
        QStringLiteral("Reference (undeformed) speckle image to import."));
    parser.addPositionalArgument(
        QStringLiteral("target"),
        QStringLiteral("Deformed target image(s) to import and record."),
        QStringLiteral("[target...]"));
    parser.process(app);

    MainWindow window;
    window.show();

    const QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        window.openReferenceImage(positional.first());
        // Targets after the reference, so a pair or a sequence can be opened in
        // one go — and so the import path is drivable without the file dialog.
        window.addTargetImages(positional.mid(1));
    }

    return app.exec();
}
