// Finding the example data that ships with SurView.
//
// WHY THIS EXISTS. The examples were committed to the repository and reachable
// from nothing: no install rule put them anywhere, the README did not mention
// them, and the application had no way to open one. A first-time reader started
// at an empty window and had to go and find speckle images before the software
// could do anything at all, while a set with an exactly known answer sat in the
// source tree they never looked at.
//
// The discovery rule is on disk rather than in a list compiled into the binary,
// so an example added to the folder is offered without anyone remembering to
// register it, and one deleted stops being offered rather than becoming a menu
// entry that fails.
//
// NEGATIVE CHECK (2026-09-01): five breaks, all five red on the cases named for
// them: grouping by folder alone (which joins two experiments into one bogus
// sequence), leaving frames in string order, offering a lone image with nothing
// to correlate against, dropping the family, and keeping the raw file stem as
// the menu name.
//
// ⚑ `two_examples_with_the_same_name_are_told_apart` was written AFTER the
// feature shipped to a staged install and the menu was opened: "Rotation"
// appeared twice, once real and once synthetic, and nothing in the tests
// noticed because nothing had asked what a reader is supposed to choose
// between them.

#include "core/Examples.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QSet>
#include <QTest>

namespace
{

void touch(const QString &path)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(path));
    file.write("x");
}

// A folder shaped like the real ones: two sets side by side in one directory,
// distinguished by the stem before their trailing digits.
QString buildExampleTree(QTemporaryDir &dir)
{
    const QString root = dir.path();
    QDir().mkpath(root + QStringLiteral("/synthetic"));
    QDir().mkpath(root + QStringLiteral("/real/01_tension"));

    for (int i = 0; i < 5; i++) {
        touch(root + QStringLiteral("/synthetic/rotation_%1.tif")
                         .arg(i, 2, 10, QLatin1Char('0')));
    }
    for (int i = 0; i < 3; i++) {
        touch(root + QStringLiteral("/synthetic/shear_%1.tif")
                         .arg(i, 2, 10, QLatin1Char('0')));
    }
    for (int i = 0; i < 5; i++) {
        touch(root + QStringLiteral("/real/01_tension/image_%1.png")
                         .arg(i, 4, 10, QLatin1Char('0')));
    }
    return root;
}

const ExampleSet *setNamed(const QVector<ExampleSet> &sets, const QString &part)
{
    for (const ExampleSet &set : sets) {
        if (set.name.contains(part, Qt::CaseInsensitive))
            return &set;
    }
    return nullptr;
}

}  // namespace

class TestExamples : public QObject
{
    Q_OBJECT

private slots:
    void every_numbered_run_of_images_is_offered_as_one_example();
    void two_sets_in_one_folder_are_kept_apart_by_their_stem();
    void the_first_frame_is_the_reference_and_the_rest_are_targets();
    void frames_are_offered_in_sequence_order_not_in_string_order();
    void a_lone_image_is_not_an_example();
    void a_folder_with_nothing_in_it_offers_nothing_rather_than_failing();
    void an_example_names_itself_readably();
    void two_examples_with_the_same_name_are_told_apart();
    void a_pair_of_images_is_the_smallest_example_there_is();
    void every_separator_a_name_might_use_is_trimmed_from_its_stem();
    void images_named_only_by_their_number_take_the_folders_name();
    void every_place_the_examples_are_looked_for_is_a_place_that_exists();
};

void TestExamples::every_numbered_run_of_images_is_offered_as_one_example()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QVector<ExampleSet> sets = findExamples({buildExampleTree(dir)});

    // rotation, shear, and the real tension folder.
    QCOMPARE(sets.size(), 3);
}

void TestExamples::two_sets_in_one_folder_are_kept_apart_by_their_stem()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QVector<ExampleSet> sets = findExamples({buildExampleTree(dir)});

    // The synthetic sets share one directory, as they do in the repository, so
    // grouping by folder alone would offer eight frames of two different
    // experiments as a single sequence: a specimen that rotates and then
    // abruptly shears, which would correlate and be wrong.
    const ExampleSet *rotation = setNamed(sets, QStringLiteral("rotation"));
    const ExampleSet *shear = setNamed(sets, QStringLiteral("shear"));
    QVERIFY2(rotation, "the rotation set was not offered");
    QVERIFY2(shear, "the shear set was not offered");
    QCOMPARE(rotation->frames.size(), 5);
    QCOMPARE(shear->frames.size(), 3);
}

void TestExamples::the_first_frame_is_the_reference_and_the_rest_are_targets()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QVector<ExampleSet> sets = findExamples({buildExampleTree(dir)});

    const ExampleSet *rotation = setNamed(sets, QStringLiteral("rotation"));
    QVERIFY(rotation);
    QVERIFY2(rotation->reference().endsWith(QStringLiteral("rotation_00.tif")),
             qPrintable(rotation->reference()));
    QCOMPARE(rotation->targets().size(), 4);
}

void TestExamples::frames_are_offered_in_sequence_order_not_in_string_order()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.path();
    QDir().mkpath(root + QStringLiteral("/odd"));
    // Unpadded, so string order would run 1, 10, 2. A sequence measured in that
    // order solves perfectly and means nothing.
    for (int i : {1, 2, 10}) {
        touch(root + QStringLiteral("/odd/step_%1.tif").arg(i));
    }

    const QVector<ExampleSet> sets = findExamples({root});
    QCOMPARE(sets.size(), 1);
    QCOMPARE(sets.first().frames.size(), 3);
    QVERIFY2(sets.first().frames.at(1).endsWith(QStringLiteral("step_2.tif")),
             qPrintable(sets.first().frames.join(QLatin1Char(' '))));
    QVERIFY2(sets.first().frames.at(2).endsWith(QStringLiteral("step_10.tif")),
             qPrintable(sets.first().frames.join(QLatin1Char(' '))));
}

void TestExamples::a_lone_image_is_not_an_example()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QDir().mkpath(dir.path() + QStringLiteral("/single"));
    touch(dir.path() + QStringLiteral("/single/only_00.tif"));

    // There is nothing to correlate against, so offering it would produce a
    // reference and no targets, and a Run button that cannot run.
    QVERIFY(findExamples({dir.path()}).isEmpty());
}

void TestExamples::a_folder_with_nothing_in_it_offers_nothing_rather_than_failing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(findExamples({dir.path()}).isEmpty());
    QVERIFY(findExamples({QStringLiteral("/no/such/place")}).isEmpty());
    QVERIFY(findExamples({}).isEmpty());
}

void TestExamples::an_example_names_itself_readably()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QVector<ExampleSet> sets = findExamples({buildExampleTree(dir)});

    for (const ExampleSet &set : sets) {
        // A menu entry, so it has to read as words rather than as a file stem:
        // no underscores, no leading ordinal, and it says how many frames it
        // has, because that is what tells a reader whether it is a pair or a
        // sequence before they open it.
        QVERIFY2(!set.name.contains(QLatin1Char('_')), qPrintable(set.name));
        QVERIFY2(!set.name.isEmpty(), "an example with no name");
        QVERIFY2(set.name.at(0).isLetter() && set.name.at(0).isUpper(),
                 qPrintable(set.name));
        QVERIFY2(set.summary.contains(QString::number(set.frames.size())),
                 qPrintable(set.summary));
    }
}

void TestExamples::two_examples_with_the_same_name_are_told_apart()
{
    // ⚑ Found by installing and opening the menu: "Rotation" appeared twice,
    // once for the real speckled plate and once for the synthetic set. Two
    // identical entries side by side is a choice a reader cannot make, and the
    // difference between them is exactly the one that matters here -- the
    // synthetic set has an exactly known answer and the real one does not.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.path();
    QDir().mkpath(root + QStringLiteral("/synthetic"));
    QDir().mkpath(root + QStringLiteral("/real/03_rotation"));
    for (int i = 0; i < 3; i++) {
        touch(root + QStringLiteral("/synthetic/rotation_%1.tif").arg(i));
        touch(root + QStringLiteral("/real/03_rotation/image_%1.png").arg(i));
    }

    const QVector<ExampleSet> sets = findExamples({root});
    QCOMPARE(sets.size(), 2);
    QVERIFY2(sets.at(0).group != sets.at(1).group,
             qPrintable(QStringLiteral("both are in group '%1'").arg(sets.at(0).group)));

    for (const ExampleSet &set : sets)
        QVERIFY2(!set.group.isEmpty(), qPrintable(set.name));
}


void TestExamples::a_pair_of_images_is_the_smallest_example_there_is()
{
    // ⚑ The boundary of "a lone image is not an example". Two images are a
    // reference and one target, which is a whole DIC measurement -- the
    // smallest one there is -- and turning it away would hide the shift pair
    // that this project's own fixtures are built on.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QDir().mkpath(dir.path() + QStringLiteral("/pair"));
    touch(dir.path() + QStringLiteral("/pair/shift_00.tif"));
    touch(dir.path() + QStringLiteral("/pair/shift_01.tif"));

    const QVector<ExampleSet> sets = findExamples({dir.path()});
    QCOMPARE(sets.size(), 1);
    QCOMPARE(sets.first().frames.size(), 2);
}

void TestExamples::every_separator_a_name_might_use_is_trimmed_from_its_stem()
{
    // The stem is what names the example in the menu, and the separator is
    // trimmed so "rotation_04" reads as "rotation" rather than "rotation_".
    // Every fixture in this file separates with an underscore, so the other
    // three were never exercised at all.
    //
    // ⚑ Only the DOT can actually fail, and the reason is worth recording so
    // that the other three mutants are not hunted: readableName() replaces
    // '_' and '-' with spaces and then simplifies, which strips a trailing one
    // -- so a stem left as "dash-" is displayed as "Dash" regardless, and the
    // trimming of those three is belt and braces over a normalisation that has
    // already happened. A dot survives readableName untouched, so an untrimmed
    // one reaches the menu as "Dot." and this case reddens. The three are kept
    // because stemOf is also what the grouping key is built from, where no
    // such normalisation applies.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QVector<QPair<QString, QString>> named{
        {QStringLiteral("under_"), QStringLiteral("under")},
        {QStringLiteral("dash-"), QStringLiteral("dash")},
        {QStringLiteral("dot."), QStringLiteral("dot")},
        {QStringLiteral("space "), QStringLiteral("space")},
    };

    for (const auto &entry : named) {
        const QString folder = dir.path() + QStringLiteral("/") + entry.second;
        QDir().mkpath(folder);
        touch(folder + QStringLiteral("/") + entry.first + QStringLiteral("00.tif"));
        touch(folder + QStringLiteral("/") + entry.first + QStringLiteral("01.tif"));
    }

    const QVector<ExampleSet> sets = findExamples({dir.path()});
    QCOMPARE(sets.size(), named.size());

    for (const auto &entry : named) {
        bool found = false;
        for (const ExampleSet &set : sets) {
            if (set.name.contains(entry.second, Qt::CaseInsensitive)
                && !set.name.contains(entry.first, Qt::CaseInsensitive)) {
                found = true;
            }
        }
        QVERIFY2(found,
                 qPrintable(QStringLiteral("no example named for \"%1\" with its "
                                           "separator trimmed; the names offered were %2")
                                .arg(entry.second, [&sets] {
                                    QStringList all;
                                    for (const ExampleSet &set : sets)
                                        all << set.name;
                                    return all.join(QStringLiteral(", "));
                                }())));
    }
}


void TestExamples::images_named_only_by_their_number_take_the_folders_name()
{
    // ⚑ THEY ARE NOT OFFERED AT ALL, which is not what the naming code below
    // suggests and is worth pinning either way. findExamples() skips any file
    // whose stem is empty before it groups anything, so a folder of 00.tif and
    // 01.tif produces no example, and the "name it after the folder when the
    // stem says nothing" fallback further down can never be reached: every set
    // that gets that far has a stem. Its mutant survives and always will.
    //
    // Recorded rather than changed. A file named only by its number is not
    // recognisably a numbered RUN -- 00.tif beside 01.tif could as easily be
    // two unrelated pictures -- and every example this project ships names its
    // set. If a real dataset ever arrives named that way, this case is where
    // the decision is written down.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString folder = dir.path() + QStringLiteral("/03_tension_with_holes");
    QDir().mkpath(folder);
    touch(folder + QStringLiteral("/00.tif"));
    touch(folder + QStringLiteral("/01.tif"));

    QVERIFY2(findExamples({dir.path()}).isEmpty(),
             "a folder of images named only by their number was offered as an example");

    // A single word in front of the numbers is all it takes, so the rule is
    // about the stem rather than about the folder being unusable.
    touch(folder + QStringLiteral("/frame_00.tif"));
    touch(folder + QStringLiteral("/frame_01.tif"));
    const QVector<ExampleSet> sets = findExamples({dir.path()});
    QCOMPARE(sets.size(), 1);
    QCOMPARE(sets.first().frames.size(), 2);
}

void TestExamples::every_place_the_examples_are_looked_for_is_a_place_that_exists()
{
    // The application looks for its examples in several places relative to the
    // binary, because a build tree, an installed prefix and an AppImage put
    // them in different ones. The list it returns is the list it will search,
    // and a path that does not exist has no business in it: it turns a menu
    // that finds nothing into a menu that looked in four imaginary folders.
    //
    // ⚑ The two halves of that test are separate questions - does this path
    // exist, and have we already got it - and joining them with OR keeps every
    // candidate, existing or not. Nothing here had ever looked at the list.
    // Asked about the real running binary's directory, which is what the
    // application asks about, and about a directory that holds nothing, which
    // is what a stripped-down install looks like.
    const QStringList paths =
        exampleSearchPaths(QCoreApplication::applicationDirPath());

    for (const QString &path : paths) {
        QVERIFY2(QDir(path).exists(),
                 qPrintable(QStringLiteral("the examples are looked for in %1, "
                                           "which does not exist")
                                .arg(path)));
    }

    // And no place is searched twice: several of the candidates resolve to the
    // same folder in an ordinary build, and a menu built from a list with
    // duplicates offers every example twice.
    QSet<QString> seen;
    for (const QString &path : paths) {
        QVERIFY2(!seen.contains(path),
                 qPrintable(QStringLiteral("%1 is searched twice").arg(path)));
        seen.insert(path);
    }

    // A directory with nothing around it offers nowhere to look, rather than
    // four places that do not exist.
    QTemporaryDir empty;
    QVERIFY(empty.isValid());
    const QStringList nowhere = exampleSearchPaths(empty.path());
    for (const QString &path : nowhere) {
        QVERIFY2(QDir(path).exists(),
                 qPrintable(QStringLiteral("a stripped install would search %1")
                                .arg(path)));
    }
}

QTEST_MAIN(TestExamples)
#include "test_examples.moc"
