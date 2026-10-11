// The settings a new project starts from, kept between sessions (core/Defaults.h).
//
// The fixture is lopsided on purpose: every setting is moved off SurView's own
// value, and each to a value no other setting holds, so a field written under
// another field's key, or read back into the wrong one, cannot come back right
// by coincidence.
//
// Written red first (2026-10-11), against a stub. NEGATIVE CHECKS, each caught by
// the case named: the reliable correlation read from the retry key (read back
// exactly); a newer file accepted (newer SurView); the format left unchecked
// (not SurView defaults); a damaged file passed over without a word (damaged,
// and not SurView defaults); one setting left without a line, and SurView's own
// column showing the saved value (every setting has a line, and differs named);
// the folder not made (creates the folder); forgetting that removed nothing
// (forgetting). Two cases first passed against the stub, because an empty list
// has nothing in it to differ, and were tightened to require lines at all.

#include "core/Defaults.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

namespace {

RunDefaults lopsided()
{
    RunDefaults d;
    CorrelationSettings &s = d.settings;
    s.solver = CorrelationSettings::ICLM;
    s.shapeOrder = 2;
    s.subsetRadius = 21;
    s.gridStep = 7;
    s.maxIterations = 13;
    s.convergence = 0.0007;
    s.strainEnabled = false;
    s.strainRadius = 31.5;
    s.strainMinPoints = 9;
    s.strainMeasure = StrainMeasure::GreenLagrange;
    s.recovery.enabled = false;
    s.recovery.retryBelowZncc = 0.65;
    s.recovery.reliableZncc = 0.85;
    s.recovery.maxRounds = 6;
    d.referenceUpdate.enabled = true;
    d.referenceUpdate.znccThreshold = 0.83;
    d.referenceUpdate.percentile = 0.77;
    return d;
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

bool isBuiltIn(const RunDefaults &d)
{
    const RunDefaults own;
    return d.settings.subsetRadius == own.settings.subsetRadius
           && d.settings.gridStep == own.settings.gridStep
           && d.settings.strainRadius == own.settings.strainRadius
           && d.referenceUpdate.enabled == own.referenceUpdate.enabled;
}

} // namespace

class TestDefaults : public QObject
{
    Q_OBJECT

private slots:
    void saved_defaults_are_read_back_exactly();
    void with_nothing_saved_surview_s_own_apply_and_nothing_is_reported();
    void a_damaged_defaults_file_is_not_used_and_says_so();
    void defaults_from_a_newer_surview_are_not_used_and_say_so();
    void a_file_that_is_not_surview_defaults_is_not_used();
    void forgetting_the_defaults_brings_surview_s_own_back();
    void saving_creates_the_folder_it_needs();
    void a_save_that_cannot_be_written_says_why();
    void a_saved_default_that_differs_is_named_with_both_values();
    void surview_s_own_defaults_differ_from_nothing();
    void every_setting_a_project_saves_has_a_line();
};

void TestDefaults::saved_defaults_are_read_back_exactly()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("defaults.json"));
    const RunDefaults saved = lopsided();
    QCOMPARE(saveDefaults(path, saved), QString());

    const DefaultsLoad read = loadDefaults(path);
    QVERIFY2(read.saved, "saved defaults were not used");
    QCOMPARE(read.problem, QString());

    const CorrelationSettings &s = read.defaults.settings;
    QCOMPARE(int(s.solver), int(CorrelationSettings::ICLM));
    QCOMPARE(s.shapeOrder, 2);
    QCOMPARE(s.subsetRadius, 21);
    QCOMPARE(s.gridStep, 7);
    QCOMPARE(s.maxIterations, 13);
    QCOMPARE(s.convergence, 0.0007);
    QCOMPARE(s.strainEnabled, false);
    QCOMPARE(s.strainRadius, 31.5);
    QCOMPARE(s.strainMinPoints, 9);
    QCOMPARE(int(s.strainMeasure), int(StrainMeasure::GreenLagrange));
    QCOMPARE(s.recovery.enabled, false);
    QCOMPARE(s.recovery.retryBelowZncc, 0.65);
    QCOMPARE(s.recovery.reliableZncc, 0.85);
    QCOMPARE(s.recovery.maxRounds, 6);
    QCOMPARE(read.defaults.referenceUpdate.enabled, true);
    QCOMPARE(read.defaults.referenceUpdate.znccThreshold, 0.83);
    QCOMPARE(read.defaults.referenceUpdate.percentile, 0.77);
}

void TestDefaults::with_nothing_saved_surview_s_own_apply_and_nothing_is_reported()
{
    QTemporaryDir dir;
    const DefaultsLoad read = loadDefaults(dir.filePath(QStringLiteral("defaults.json")));
    QVERIFY(!read.saved);
    // Having no saved defaults is the ordinary case and not a problem.
    QCOMPARE(read.problem, QString());
    QVERIFY(isBuiltIn(read.defaults));
}

void TestDefaults::a_damaged_defaults_file_is_not_used_and_says_so()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("defaults.json"));
    writeFile(path, "{ \"format\": \"SurView DIC defaults\", \"version\": 1, \"settings\": ");

    const DefaultsLoad read = loadDefaults(path);
    QVERIFY(!read.saved);
    QVERIFY2(read.problem.contains(QStringLiteral("defaults.json")),
             qPrintable(read.problem));
    QVERIFY(isBuiltIn(read.defaults));
}

void TestDefaults::defaults_from_a_newer_surview_are_not_used_and_say_so()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("defaults.json"));
    QCOMPARE(saveDefaults(path, lopsided()), QString());
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    root[QStringLiteral("version")] = 99;
    writeFile(path, QJsonDocument(root).toJson());

    const DefaultsLoad read = loadDefaults(path);
    QVERIFY(!read.saved);
    QVERIFY2(read.problem.contains(QStringLiteral("newer")), qPrintable(read.problem));
    QVERIFY(isBuiltIn(read.defaults));
}

void TestDefaults::a_file_that_is_not_surview_defaults_is_not_used()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("defaults.json"));
    // A project file is JSON with the same settings in it, and still not this.
    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("SurView DIC project");
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("settings")] = correlationSettingsToJson(lopsided().settings);
    writeFile(path, QJsonDocument(root).toJson());

    const DefaultsLoad read = loadDefaults(path);
    QVERIFY(!read.saved);
    QVERIFY(!read.problem.isEmpty());
    QVERIFY(isBuiltIn(read.defaults));
}

void TestDefaults::forgetting_the_defaults_brings_surview_s_own_back()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("defaults.json"));
    QCOMPARE(saveDefaults(path, lopsided()), QString());
    QVERIFY(loadDefaults(path).saved);

    QCOMPARE(forgetDefaults(path), QString());
    const DefaultsLoad read = loadDefaults(path);
    QVERIFY(!read.saved);
    QVERIFY(isBuiltIn(read.defaults));
    // Forgetting what is already forgotten is not a failure.
    QCOMPARE(forgetDefaults(path), QString());
}

void TestDefaults::saving_creates_the_folder_it_needs()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("not/yet/there/defaults.json"));
    QCOMPARE(saveDefaults(path, lopsided()), QString());
    QVERIFY(loadDefaults(path).saved);
}

void TestDefaults::a_save_that_cannot_be_written_says_why()
{
    QTemporaryDir dir;
    // A file where the folder should be: the folder cannot be made.
    writeFile(dir.filePath(QStringLiteral("blocked")), "x");
    const QString path = dir.filePath(QStringLiteral("blocked/defaults.json"));
    const QString refusal = saveDefaults(path, lopsided());
    QVERIFY2(!refusal.isEmpty(), "a save that wrote nothing reported success");
}

void TestDefaults::a_saved_default_that_differs_is_named_with_both_values()
{
    RunDefaults d;
    d.settings.subsetRadius = 20;
    d.referenceUpdate.enabled = true;

    QVector<SettingLine> differing;
    for (const SettingLine &line : settingLines(d)) {
        if (line.differs())
            differing << line;
    }
    QCOMPARE(differing.size(), 2);

    QCOMPARE(differing[0].label, QStringLiteral("Subset radius"));
    QCOMPARE(differing[0].section, QStringLiteral("Correlation"));
    QCOMPARE(differing[0].value, QStringLiteral("20 px"));
    QCOMPARE(differing[0].builtIn, QStringLiteral("16 px"));

    QCOMPARE(differing[1].section, QStringLiteral("Reference"));
    QCOMPARE(differing[1].value, QStringLiteral("on"));
    QCOMPARE(differing[1].builtIn, QStringLiteral("off"));
}

void TestDefaults::surview_s_own_defaults_differ_from_nothing()
{
    const QVector<SettingLine> lines = settingLines(RunDefaults());
    QVERIFY2(!lines.isEmpty(), "no settings are described at all");
    for (const SettingLine &line : lines) {
        QVERIFY2(!line.differs(), qPrintable(line.label));
        QVERIFY2(!line.value.isEmpty(), qPrintable(line.label));
        QVERIFY2(!line.label.isEmpty(), "a setting with no name");
    }
}

void TestDefaults::every_setting_a_project_saves_has_a_line()
{
    // A setting added to the project file and not to the lines would be a
    // default nobody could see. One line per stored key.
    const RunDefaults d;
    const int stored = correlationSettingsToJson(d.settings).size()
                       + referencePolicyToJson(d.referenceUpdate).size();
    QVERIFY2(stored > 0, "nothing is stored");
    QCOMPARE(settingLines(d).size(), stored);

    // And each one of them, moved on its own, is the one line that differs.
    const RunDefaults moved = lopsided();
    int differing = 0;
    for (const SettingLine &line : settingLines(moved))
        differing += line.differs();
    QCOMPARE(differing, stored);
}

QTEST_GUILESS_MAIN(TestDefaults)
#include "test_defaults.moc"
