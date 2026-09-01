#include "core/Examples.h"

#include "core/Sequence.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMap>
#include <QObject>
#include <QRegularExpression>

namespace
{

// The image kinds decodeImage() reads. Kept here rather than shared with the
// decoder because this is a question about FILENAMES, not about what can be
// decoded: a file that matches and cannot be read is reported when it is
// opened, in the words the record uses, rather than being silently skipped
// here where the reason would never be seen.
const QStringList &imagePatterns()
{
    static const QStringList patterns{
        QStringLiteral("*.tif"), QStringLiteral("*.tiff"),
        QStringLiteral("*.png"), QStringLiteral("*.jpg"),
        QStringLiteral("*.jpeg"), QStringLiteral("*.bmp"),
    };
    return patterns;
}

// The part of a file stem before its trailing digits, with any separator left
// hanging on the end removed: "rotation_04" gives "rotation", "image_0000"
// gives "image", "step_10" gives "step".
QString stemOf(const QString &fileName)
{
    QString stem = QFileInfo(fileName).completeBaseName();
    static const QRegularExpression trailing(QStringLiteral("[0-9]+$"));
    stem.remove(trailing);
    while (!stem.isEmpty() && (stem.endsWith(QLatin1Char('_'))
                               || stem.endsWith(QLatin1Char('-'))
                               || stem.endsWith(QLatin1Char('.'))
                               || stem.endsWith(QLatin1Char(' ')))) {
        stem.chop(1);
    }
    return stem;
}

// "01_tension_without_holes" -> "Tension without holes".
QString readableName(const QString &raw)
{
    QString text = raw;
    static const QRegularExpression ordinal(QStringLiteral("^[0-9]+[_-]"));
    text.remove(ordinal);
    text.replace(QLatin1Char('_'), QLatin1Char(' '));
    text.replace(QLatin1Char('-'), QLatin1Char(' '));
    text = text.simplified();
    if (!text.isEmpty())
        text[0] = text[0].toUpper();
    return text;
}

}  // namespace

QVector<ExampleSet> findExamples(const QStringList &roots)
{
    // Grouped by folder AND stem. Sorted keys, so the same folder always
    // produces the same order: a menu whose entries move between runs is a
    // menu nobody learns.
    QMap<QString, QStringList> grouped;

    for (const QString &root : roots) {
        QDir rootDir(root);
        if (!rootDir.exists())
            continue;

        QDirIterator it(root, imagePatterns(), QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const QFileInfo info(path);
            const QString stem = stemOf(info.fileName());
            if (stem.isEmpty())
                continue;
            // The family is the first folder under the root this file was
            // found beneath, so it survives the subdirectory walk.
            const QString relative = rootDir.relativeFilePath(info.absoluteFilePath());
            const QString family = relative.section(QLatin1Char('/'), 0, 0);

            grouped[family + QLatin1Char('\x1f') + info.absolutePath()
                    + QLatin1Char('\x1f') + stem]
                << info.absoluteFilePath();
        }
    }

    QVector<ExampleSet> sets;
    for (auto it = grouped.constBegin(); it != grouped.constEnd(); ++it) {
        // A single image has nothing to correlate against; offering it would
        // give a reference, no targets, and a Run button that cannot run.
        if (it.value().size() < 2)
            continue;

        const QString key = it.key();
        const QString family = key.section(QLatin1Char('\x1f'), 0, 0);
        const QString folder = key.section(QLatin1Char('\x1f'), 1, 1);
        const QString stem = key.section(QLatin1Char('\x1f'), 2);

        ExampleSet set;
        set.frames = sortIntoSequenceOrder(it.value());

        // Named for the stem where the stem says something ("rotation"), and
        // for the folder where it does not ("image_0000.png" in a folder
        // called 01_tension_without_holes).
        const QString fromStem = readableName(stem);
        const QString fromFolder = readableName(QFileInfo(folder).fileName());
        set.name = (fromStem.compare(QStringLiteral("Image"), Qt::CaseInsensitive) == 0
                    || fromStem.isEmpty())
                       ? fromFolder
                       : fromStem;
        if (set.name.isEmpty())
            set.name = fromStem;

        set.group = readableName(family);
        set.summary = QObject::tr("%1 frames, from %2")
                          .arg(set.frames.size())
                          .arg(QFileInfo(folder).fileName());
        sets.append(set);
    }
    return sets;
}

QStringList exampleSearchPaths(const QString &applicationDirectory)
{
    QStringList paths;
    const QDir appDir(applicationDirectory);

    // An installed build: share/surview/examples beside the bin directory.
    paths << appDir.absoluteFilePath(QStringLiteral("../share/surview/examples"));
    // A build tree inside the source, which is where a developer runs from.
    paths << appDir.absoluteFilePath(QStringLiteral("../../examples"));
    paths << appDir.absoluteFilePath(QStringLiteral("../examples"));
    paths << appDir.absoluteFilePath(QStringLiteral("examples"));

    QStringList existing;
    for (const QString &path : paths) {
        const QString clean = QDir::cleanPath(path);
        if (QDir(clean).exists() && !existing.contains(clean))
            existing << clean;
    }
    return existing;
}
