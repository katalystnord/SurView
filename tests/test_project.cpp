// Saving what a session was, and getting it back.
//
// WHY THIS EXISTS. New, Open and Save Project have been on the File menu since
// the first window and did nothing but report themselves unimplemented. That is
// honest and it is still a promise the application makes and does not keep, and
// it is the most conspicuous gap against every other DIC GUI: pyALDIC saves a
// whole session to one file.
//
// ⚑ A project stores WHERE the images are, never the images. Embedding them
// would make the file enormous and, worse, would let it drift from the pictures
// on disk without saying so. Storing paths creates the opposite risk, which is
// the one this file is mostly about: the images can move, or change, between
// saving and opening. So each is recorded with the SHA-256 it had, and a
// project that opens against different pixels says so rather than measuring
// them quietly.
//
// NEGATIVE CHECK (2026-09-01): five breaks. Four went red on the case named for
// them: storing absolute paths, omitting the hash, dropping a missing image
// silently, and failing to restore the strain measure.
//
// ⚑ The fifth stayed green and found a gap. Removing the format check entirely
// changed nothing, because the only "not a project" case used a plain text
// file, which the JSON parser rejects first -- so the check that says the file
// is OURS was never exercised, and any .json from any other tool would have
// opened as an empty session with default settings. A valid-JSON case was added
// and the break then reddened it.

#include "core/Project.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace
{

QString writeImage(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(contents);
    return path;
}

Project sampleProject(const QString &dir)
{
    Project project;
    project.referencePath = writeImage(dir + QStringLiteral("/ref.tif"), "reference");
    project.targetPaths << writeImage(dir + QStringLiteral("/t_00.tif"), "one")
                        << writeImage(dir + QStringLiteral("/t_01.tif"), "two");

    project.roi.origin = RegionOfInterest::Drawn;
    project.roi.vertices << QPoint(10, 20) << QPoint(90, 20)
                         << QPoint(90, 80) << QPoint(10, 80);

    project.settings.solver = CorrelationSettings::ICLM;
    project.settings.shapeOrder = 2;
    project.settings.subsetRadius = 21;
    project.settings.gridStep = 7;
    project.settings.maxIterations = 25;
    project.settings.convergence = 0.0005;
    project.settings.strainEnabled = true;
    project.settings.strainRadius = 33.0;
    project.settings.strainMinPoints = 8;
    project.settings.strainMeasure = StrainMeasure::GreenLagrange;

    // Deliberately every field different from its default, so a field the
    // writer forgot cannot pass by coming back as the default it never left.
    project.settings.recovery.enabled = false;
    project.settings.recovery.retryBelowZncc = 0.55;
    project.settings.recovery.reliableZncc = 0.85;
    project.settings.recovery.maxRounds = 6;

    // Gauges are part of the session: a user who placed three of them on a
    // specimen and reopened the file to find them gone has lost work that took
    // careful clicking to produce.
    Extensometer gauge;
    gauge.name = QStringLiteral("E1");
    gauge.ax = 40.0;
    gauge.ay = 60.5;
    gauge.bx = 160.0;
    gauge.by = 61.5;
    project.extensometers.append(gauge);

    // A hole, because a region that reopened without one would measure across
    // exactly the place the user went to the trouble of excluding.
    project.roi.holes.append({QPoint(40, 40), QPoint(60, 40), QPoint(60, 60), QPoint(40, 60)});

    project.referenceUpdate.enabled = true;
    project.referenceUpdate.znccThreshold = 0.85;
    project.referenceUpdate.percentile = 0.8;
    return project;
}

}  // namespace

class TestProject : public QObject
{
    Q_OBJECT

private slots:
    void everything_a_session_was_comes_back_when_it_is_opened();
    void the_file_says_what_wrote_it();
    void images_beside_the_project_are_stored_relative_to_it();
    void a_project_moved_with_its_images_still_opens();
    void an_image_that_has_gone_missing_is_named_not_dropped();
    void an_image_that_changed_since_it_was_saved_is_reported();
    void something_that_is_not_a_project_is_refused_with_a_reason();
    void a_path_that_cannot_be_written_is_reported_as_a_reason();
};

void TestProject::everything_a_session_was_comes_back_when_it_is_opened()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const Project saved = sampleProject(dir.path());
    const QString path = dir.filePath(QStringLiteral("session.svproj"));

    QVERIFY2(saveProject(path, saved).isEmpty(), "saving reported a failure");

    ProjectLoad loaded = loadProject(path);
    QVERIFY2(loaded.failure.isEmpty(), qPrintable(loaded.failure));

    QCOMPARE(QFileInfo(loaded.project.referencePath).canonicalFilePath(),
             QFileInfo(saved.referencePath).canonicalFilePath());
    QCOMPARE(loaded.project.targetPaths.size(), 2);
    QCOMPARE(loaded.project.roi.vertices, saved.roi.vertices);
    QCOMPARE(loaded.project.roi.origin, saved.roi.origin);

    // Every setting, not a subset: a project that restores the region but
    // silently resets the solver measures something different from what was
    // saved while looking as though it did not.
    QCOMPARE(loaded.project.settings.solver, saved.settings.solver);
    QCOMPARE(loaded.project.settings.shapeOrder, saved.settings.shapeOrder);
    QCOMPARE(loaded.project.settings.subsetRadius, saved.settings.subsetRadius);
    QCOMPARE(loaded.project.settings.gridStep, saved.settings.gridStep);
    QCOMPARE(loaded.project.settings.maxIterations, saved.settings.maxIterations);
    QCOMPARE(loaded.project.settings.convergence, saved.settings.convergence);
    QCOMPARE(loaded.project.settings.strainEnabled, saved.settings.strainEnabled);
    QCOMPARE(loaded.project.settings.strainRadius, saved.settings.strainRadius);
    QCOMPARE(loaded.project.settings.strainMinPoints, saved.settings.strainMinPoints);
    QCOMPARE(int(loaded.project.settings.strainMeasure),
             int(saved.settings.strainMeasure));

    QCOMPARE(loaded.project.extensometers.size(), saved.extensometers.size());
    QVERIFY(!loaded.project.extensometers.isEmpty());
    QCOMPARE(loaded.project.extensometers.first().name,
             saved.extensometers.first().name);
    QCOMPARE(loaded.project.extensometers.first().ax,
             saved.extensometers.first().ax);
    QCOMPARE(loaded.project.extensometers.first().ay,
             saved.extensometers.first().ay);
    QCOMPARE(loaded.project.extensometers.first().bx,
             saved.extensometers.first().bx);
    QCOMPARE(loaded.project.extensometers.first().by,
             saved.extensometers.first().by);

    QCOMPARE(loaded.project.roi.holes.size(), saved.roi.holes.size());
    QVERIFY(loaded.project.roi.hasHoles());
    QCOMPARE(loaded.project.roi.holes.first(), saved.roi.holes.first());

    QCOMPARE(loaded.project.settings.recovery.enabled,
             saved.settings.recovery.enabled);
    QCOMPARE(loaded.project.settings.recovery.retryBelowZncc,
             saved.settings.recovery.retryBelowZncc);
    QCOMPARE(loaded.project.settings.recovery.reliableZncc,
             saved.settings.recovery.reliableZncc);
    QCOMPARE(loaded.project.settings.recovery.maxRounds,
             saved.settings.recovery.maxRounds);

    QCOMPARE(loaded.project.referenceUpdate.enabled, saved.referenceUpdate.enabled);
    QCOMPARE(loaded.project.referenceUpdate.znccThreshold,
             saved.referenceUpdate.znccThreshold);
    QCOMPARE(loaded.project.referenceUpdate.percentile,
             saved.referenceUpdate.percentile);
}

void TestProject::the_file_says_what_wrote_it()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("session.svproj"));
    QVERIFY(saveProject(path, sampleProject(dir.path())).isEmpty());

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(file.readAll());

    // A file with no version is a file nothing can ever safely change.
    QVERIFY2(text.contains(QStringLiteral("SurView")), qPrintable(text.left(400)));
    QVERIFY2(text.contains(QStringLiteral("version")), qPrintable(text.left(400)));
}

void TestProject::images_beside_the_project_are_stored_relative_to_it()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("session.svproj"));
    QVERIFY(saveProject(path, sampleProject(dir.path())).isEmpty());

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(file.readAll());

    // So a folder holding the project and its images can be copied to another
    // machine, or another user's home, and still open. An absolute path would
    // make the file work only where it was written.
    QVERIFY2(!text.contains(dir.path()),
             "the project stored absolute paths and cannot be moved");
    QVERIFY2(text.contains(QStringLiteral("ref.tif")), qPrintable(text.left(400)));
}

void TestProject::a_project_moved_with_its_images_still_opens()
{
    QTemporaryDir from;
    QTemporaryDir to;
    QVERIFY(from.isValid() && to.isValid());

    const Project saved = sampleProject(from.path());
    QVERIFY(saveProject(from.filePath(QStringLiteral("s.svproj")), saved).isEmpty());

    // Move the lot, as copying a results folder to a colleague would.
    for (const QString &name : {QStringLiteral("s.svproj"), QStringLiteral("ref.tif"),
                                QStringLiteral("t_00.tif"), QStringLiteral("t_01.tif")}) {
        QVERIFY(QFile::copy(from.filePath(name), to.filePath(name)));
    }

    const ProjectLoad loaded = loadProject(to.filePath(QStringLiteral("s.svproj")));
    QVERIFY2(loaded.failure.isEmpty(), qPrintable(loaded.failure));
    QVERIFY2(loaded.missing.isEmpty(), qPrintable(loaded.missing.join(QLatin1Char(' '))));
    QVERIFY2(loaded.project.referencePath.startsWith(to.path()),
             qPrintable(loaded.project.referencePath));
}

void TestProject::an_image_that_has_gone_missing_is_named_not_dropped()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const Project saved = sampleProject(dir.path());
    const QString path = dir.filePath(QStringLiteral("s.svproj"));
    QVERIFY(saveProject(path, saved).isEmpty());

    QVERIFY(QFile::remove(dir.filePath(QStringLiteral("t_01.tif"))));

    const ProjectLoad loaded = loadProject(path);
    // Opened, not refused: one missing frame out of two should not cost the
    // session. But named, because a sequence quietly one frame shorter than it
    // was is a different measurement wearing the same name.
    QVERIFY2(loaded.failure.isEmpty(), qPrintable(loaded.failure));
    QCOMPARE(loaded.missing.size(), 1);
    QVERIFY2(loaded.missing.first().contains(QStringLiteral("t_01.tif")),
             qPrintable(loaded.missing.first()));
    QCOMPARE(loaded.project.targetPaths.size(), 1);
}

void TestProject::an_image_that_changed_since_it_was_saved_is_reported()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const Project saved = sampleProject(dir.path());
    const QString path = dir.filePath(QStringLiteral("s.svproj"));
    QVERIFY(saveProject(path, saved).isEmpty());

    // Same name, different pixels. This is the case that makes storing paths
    // rather than images safe to do at all: without the hash, the session comes
    // back looking identical and measures something else.
    writeImage(dir.filePath(QStringLiteral("t_00.tif")), "something else entirely");

    const ProjectLoad loaded = loadProject(path);
    QVERIFY2(loaded.failure.isEmpty(), qPrintable(loaded.failure));
    QCOMPARE(loaded.changed.size(), 1);
    QVERIFY2(loaded.changed.first().contains(QStringLiteral("t_00.tif")),
             qPrintable(loaded.changed.first()));

    // Still opened. The user is told, and decides.
    QCOMPARE(loaded.project.targetPaths.size(), 2);
}

void TestProject::something_that_is_not_a_project_is_refused_with_a_reason()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("notes.txt"));
    writeImage(path, "this is not a project file");

    const ProjectLoad loaded = loadProject(path);
    QVERIFY2(!loaded.failure.isEmpty(), "a text file was accepted as a project");
    QVERIFY2(loaded.failure.contains(QStringLiteral("notes.txt")),
             qPrintable(loaded.failure));

    const ProjectLoad missing = loadProject(dir.filePath(QStringLiteral("no.svproj")));
    QVERIFY2(!missing.failure.isEmpty(), "a file that does not exist was accepted");

    // ⚑ Valid JSON that is not ours. Found by negative check: the case above
    // uses a plain text file, which the JSON parser rejects before the format
    // check is ever reached, so removing that check left everything green. Any
    // .json at all would have opened as an empty session with default settings
    // and no sign that it was not a project.
    const QString json = dir.filePath(QStringLiteral("elsewhere.json"));
    // No version field, deliberately: with one, the newer-version guard would
    // reject it first and the format check would still go unexercised. That is
    // exactly how the first attempt at this case passed for the wrong reason.
    writeImage(json, "{\"some\": \"other tool's file\"}");
    const ProjectLoad wrong = loadProject(json);
    QVERIFY2(!wrong.failure.isEmpty(),
             "a JSON file from another tool was accepted as a project");
    QVERIFY2(wrong.failure.contains(QStringLiteral("elsewhere.json")),
             qPrintable(wrong.failure));
}

void TestProject::a_path_that_cannot_be_written_is_reported_as_a_reason()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString reason =
        saveProject(dir.filePath(QStringLiteral("no/such/place/s.svproj")),
                    sampleProject(dir.path()));
    QVERIFY2(!reason.isEmpty(), "a failed save reported success");
    QVERIFY2(reason.contains(QStringLiteral("s.svproj")), qPrintable(reason));
}

QTEST_MAIN(TestProject)
#include "test_project.moc"
