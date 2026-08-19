#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

struct ImageRecord;

// What a sequence will measure, and in what order.
//
// ⚑ ORDER IS A CORRECTNESS PROBLEM HERE, not a presentation one. A sequence is
// a time axis, and sorted the way a computer sorts strings `frame_10.tif` comes
// before `frame_2.tif` -- so a twelve-frame test is measured 1, 10, 11, 12, 2,
// 3 ... and every individual field is right while the sequence they form is
// nonsense. Nothing downstream can detect it: each frame solves perfectly and
// the displacements are real. The specimen simply appears to jump about.

// Whether `a` belongs before `b` in a sequence, comparing runs of digits as
// numbers and everything else as text. Only the file name is considered: frames
// gathered from more than one folder still form one sequence, and the directory
// a file sits in is not part of its position in it.
bool precedesInSequence(const QString &a, const QString &b);

QStringList sortIntoSequenceOrder(const QStringList &paths);

// Which targets will be measured, in which order, and why the rest will not.
struct SequencePlan
{
    // Indices into the target list, in the order they will be measured.
    QVector<int> order;

    // Index -> why that target is not in the order. Kept rather than dropped:
    // "measured 9 frames" reads very differently when 12 were imported, and the
    // reason is already known at the point the decision is made.
    QMap<int, QString> skipped;

    bool isEmpty() const { return order.isEmpty(); }
};

SequencePlan planSequence(const ImageRecord &reference,
                          const QVector<ImageRecord> &targets);
