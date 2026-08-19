#include "core/Sequence.h"

#include "core/ImagePairing.h"
#include "core/ImageRecord.h"

#include <QFileInfo>
#include <QObject>

#include <algorithm>

namespace {

// The name a file's position in the sequence is decided by.
QString sequenceKey(const QString &path)
{
    return QFileInfo(path).fileName();
}

bool isDigit(QChar c)
{
    return c >= QLatin1Char('0') && c <= QLatin1Char('9');
}

// Negative, zero or positive, like strcmp, comparing digit runs as numbers.
int compareNaturally(const QString &left, const QString &right)
{
    int i = 0;
    int j = 0;
    while (i < left.size() && j < right.size()) {
        if (isDigit(left.at(i)) && isDigit(right.at(j))) {
            // Whole runs at once, so 2 beats 10 on value rather than on first
            // character. Leading zeros are skipped rather than compared, so a
            // padded name and an unpadded one of the same value agree.
            int startI = i;
            int startJ = j;
            while (i < left.size() && isDigit(left.at(i)))
                i++;
            while (j < right.size() && isDigit(right.at(j)))
                j++;

            QStringView runLeft = QStringView(left).mid(startI, i - startI);
            QStringView runRight = QStringView(right).mid(startJ, j - startJ);
            while (runLeft.size() > 1 && runLeft.at(0) == QLatin1Char('0'))
                runLeft = runLeft.mid(1);
            while (runRight.size() > 1 && runRight.at(0) == QLatin1Char('0'))
                runRight = runRight.mid(1);

            // Compared by length first, so a run longer than any integer type
            // still orders correctly instead of overflowing on the way in.
            if (runLeft.size() != runRight.size())
                return runLeft.size() < runRight.size() ? -1 : 1;
            const int digits = runLeft.compare(runRight);
            if (digits != 0)
                return digits;
            continue;
        }

        // Case-insensitively, so the order is one alphabet rather than two with
        // every capitalised name ahead of every lowercase one.
        const int letters = QString::compare(left.mid(i, 1), right.mid(j, 1),
                                             Qt::CaseInsensitive);
        if (letters != 0)
            return letters;
        i++;
        j++;
    }

    if (i < left.size())
        return 1;
    if (j < right.size())
        return -1;

    // Identical but for case: settled case-sensitively so the order is total
    // and a sort of the same list twice cannot come back different.
    return left.compare(right);
}

}  // namespace

bool precedesInSequence(const QString &a, const QString &b)
{
    return compareNaturally(sequenceKey(a), sequenceKey(b)) < 0;
}

QStringList sortIntoSequenceOrder(const QStringList &paths)
{
    QStringList sorted = paths;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const QString &a, const QString &b) {
                         return precedesInSequence(a, b);
                     });
    return sorted;
}

SequencePlan planSequence(const ImageRecord &reference,
                          const QVector<ImageRecord> &targets)
{
    SequencePlan plan;

    for (int i = 0; i < targets.size(); i++) {
        const ImageRecord &target = targets.at(i);

        if (!target.isValid()) {
            // Named as a target, and its pixels could not be read. It holds
            // provenance only, so there is nothing to correlate.
            plan.skipped.insert(i, QObject::tr("its pixels could not be read"));
            continue;
        }

        // The judgement itself belongs to core/ImagePairing.h and is not
        // repeated here; only the decision to skip is made here.
        const PairCompatibility pairing = compareToReference(reference, target);
        if (!pairing.matches()) {
            plan.skipped.insert(i, pairing.mismatches.join(QStringLiteral("; ")));
            continue;
        }

        plan.order.append(i);
    }

    // Sorted after selection, by the name each record carries, so the sequence
    // runs in frame order rather than in the order the files were chosen.
    std::stable_sort(plan.order.begin(), plan.order.end(),
                     [&targets](int a, int b) {
                         return precedesInSequence(targets.at(a).filePath,
                                                   targets.at(b).filePath);
                     });

    return plan;
}
