#pragma once

#include <QStringList>

struct ImageRecord;

// Whether a target image can be correlated against the reference at all.
//
// Correlation compares the same pixel grid before and after deformation, so a
// target that disagrees with the reference on its dimensions, channel count or
// scalar type is not a harder correlation — it is a different image of a
// different thing. That is knowable the moment the file is read, which is when
// it should be said, rather than surfacing later as a confusing failure inside
// the solver.
//
// This states the mismatch and stops there. It does not decide what to do
// about it: rejecting, converting or resampling the target would all be
// changes to the record, and the record is not ours to change.
struct PairCompatibility
{
    // Each entry names one disagreement in full, e.g.
    // "dimensions differ: reference 2048 × 1536 px, target 1024 × 768 px".
    QStringList mismatches;

    bool matches() const { return mismatches.isEmpty(); }
};

PairCompatibility compareToReference(const ImageRecord &reference,
                                     const ImageRecord &target);
