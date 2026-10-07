// Measure a sequence headlessly and write every point of every frame as a
// table, so a field can be checked against a known answer outside the window.
//
// WHY THIS EXISTS. Checking a measurement against a stated answer needs the
// field on disk, in one frame of reference, and the application's own CSV
// export needs a window and a completed run to reach. This drives the same SequenceRunner the
// window drives, with the settings stated on the command line, and writes the
// points as they come back: the grid position, the displacement relative to
// the ORIGINAL reference, whether the point converged, its correlation and
// whether the second pass recovered it, and the point's own noise floor and
// match conditioning (not-a-number where the engine wrote none). Nothing is
// filtered here; deciding what counts as measured is the job of
// whatever reads the table, and it is said there.
//
// Coordinates are reference-image pixels, x right and y DOWN, origin at the
// top-left pixel: the project's one coordinate frame.
//
// Usage:
//   surview_measure OUTDIR REFERENCE TARGET... [--radius R] [--step S]
//                   [--no-recovery]

#include "core/Correlation.h"
#include "core/Roi.h"
#include "core/SequenceRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QtNumeric>

#include <cstdio>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QStringList args = app.arguments().mid(1);
    CorrelationSettings settings;   // the application's own defaults ...
    settings.gridStep = 16;         // ... except the step, and strain, which
    settings.strainEnabled = false; // a displacement check does not need

    QStringList positional;
    for (int i = 0; i < args.size(); i++) {
        const QString &a = args[i];
        if (a == QLatin1String("--radius") && i + 1 < args.size())
            settings.subsetRadius = args[++i].toInt();
        else if (a == QLatin1String("--step") && i + 1 < args.size())
            settings.gridStep = args[++i].toInt();
        else if (a == QLatin1String("--no-recovery"))
            settings.recovery.enabled = false;
        else
            positional << a;
    }
    if (positional.size() < 3) {
        std::fprintf(stderr, "usage: surview_measure OUTDIR REFERENCE TARGET... "
                             "[--radius R] [--step S] [--no-recovery]\n");
        return 2;
    }

    const QString outDir = positional.takeFirst();
    const QString reference = positional.takeFirst();
    const QStringList targets = positional;   // in the order given, not re-sorted
    QDir().mkpath(outDir);

    SequenceRunner runner(settings, RegionOfInterest(), reference, targets);
    QString failure;
    int written = 0;
    QObject::connect(&runner, &SequenceRunner::failed,
                     [&failure](const QString &reason) { failure = reason; });
    QObject::connect(&runner, &SequenceRunner::frameFinished,
                     [&](int frame, const CorrelationResult &result) {
        const QString name = QFileInfo(targets.value(frame)).completeBaseName();
        QFile file(QDir(outDir).filePath(QStringLiteral("%1.csv").arg(name)));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            failure = QStringLiteral("cannot write %1").arg(file.fileName());
            return;
        }
        QTextStream out(&file);
        out << "# reference=" << reference << "\n"
            << "# target=" << targets.value(frame) << "\n"
            << "# subset_radius=" << settings.subsetRadius
            << " grid_step=" << settings.gridStep
            << " recovery=" << (settings.recovery.enabled ? 1 : 0) << "\n"
            << "x,y,u,v,converged,zncc,recovered,noise_floor,conditioning\n";
        out.setRealNumberPrecision(9);
        for (const CorrelationPoint &p : result.points) {
            out << p.x << ',' << p.y << ',' << p.u << ',' << p.v << ','
                << (p.converged ? 1 : 0) << ',' << p.zncc << ','
                << (p.recovered ? 1 : 0) << ','
                << (p.noiseFloorMeasured ? p.noiseFloor : qQNaN()) << ','
                << (p.conditioningMeasured ? p.conditioning : qQNaN()) << '\n';
        }
        std::fprintf(stderr, "surview: %s  %d of %d converged\n",
                     qPrintable(name), result.converged, int(result.points.size()));
        written++;
    });
    runner.run();

    if (!failure.isEmpty()) {
        std::fprintf(stderr, "surview_measure: %s\n", qPrintable(failure));
        return 1;
    }
    return written == targets.size() ? 0 : 1;
}
