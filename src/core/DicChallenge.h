#pragma once

#include <QString>
#include <QVector>

struct CorrelationPoint;

// The answer an external benchmark states, and what a measurement of it comes
// to when set against that answer.
//
// WHY THIS EXISTS. core/KnownAnswer.h reads the answer OUR OWN examples state
// about themselves. Everything it checks is therefore ours end to end: our
// images, our generator, our stated deformation. This file is the other kind of
// evidence -- an answer nobody here wrote, about images nobody here made.
//
// The set it reads is the iDICs/SEM DIC Challenge 1.0, whose rigid-shift
// samples state the prescribed shift in each file's own NAME:
//
//     Sample3-003 X0.30 Y0.30 N2 C0 R0.tif
//
// X and Y are the prescribed shift in pixels, N the grey levels of noise added,
// C the contrast and R the rotation. Reading the name is the whole of it: the
// set ships no answer file, so there is nothing else to read and nothing to
// keep in step with it.
//
// ⚑ THE SET IS NOT IN THIS REPOSITORY AND MUST NOT BE. It states no licence or
// terms of use, so it may be used and its redistribution is unestablished. The
// case that measures it reads the images from wherever they were unpacked and
// skips when they are not there; nothing here embeds any of their data, and the
// eleven numbers this file can produce come out of file names rather than out
// of a copy of theirs.
//
// ⚑ AND THEIR ANSWER IS PRESCRIBED, NOT EXACT BY CONSTRUCTION. Our synthetic
// sets render every frame afresh from an analytic pattern, so no pixel is ever
// resampled and the stated deformation IS the truth. These frames were made by
// shifting one photograph in the Fourier domain with noise added, so the image
// carries whatever that resampling does. A disagreement is evidence about the
// two of us together, which is what an independent check is for and why it must
// not be described in the words the synthetic sets have earned.

// The shift one frame's file name states, in SurView's own frame: a reference
// pixel at (x, y) is at (x + this->x, y + this->y) in the target.
//
// ⚑ Their +Y and our +v are the SAME DIRECTION. Not assumed: measured on all
// eleven frames of Sample 3 on 2026-09-14, where a convention difference would
// have shown as a v of the right size and the wrong sign at every frame.
struct DicChallengeShift
{
    // ⚑ Whether the name states a shift AT ALL, kept apart from the numbers.
    // The set's own reference frame states none, and reading that as 0.00 would
    // set a measurement against an answer nobody gave -- the same rule this
    // code base keeps for a rejected point, an unfitted strain and an
    // unmeasured pixel. A frame that really does state 0.00 is a statement, and
    // says so with `stated` true.
    bool stated = false;

    double x = 0.0;
    double y = 0.0;
};

// The shift stated by a DIC Challenge file name, or an unstated one where the
// name does not carry both axes. Accepts a bare name or a whole path.
DicChallengeShift dicChallengeShiftFromFileName(const QString &fileName);

// What a measured field comes to when set against a stated shift.
struct ShiftAgreement
{
    // ⚑ Whether anything was measured at all. A field with no converged point
    // agrees with nothing, and an error of zero over no points is the most
    // flattering reading available -- a run that lost every point would
    // otherwise report perfect agreement.
    bool measured = false;

    int solved = 0;

    double meanU = 0.0;
    double meanV = 0.0;

    // Measured minus stated, per axis, signed. Kept per axis rather than
    // collapsed into a distance: the mistakes this path can make are a flipped
    // sign and a transposed axis, and a distance hides both.
    double errorX = 0.0;
    double errorY = 0.0;

    // The larger of the two as a magnitude, so one number can carry the case.
    double worstError = 0.0;
};

// Mean measured displacement against the stated shift, over the points that
// converged. A rejected point holds the solver's leftover guess rather than a
// displacement, so it is left out.
ShiftAgreement agreementWithStatedShift(const QVector<CorrelationPoint> &points,
                                        const DicChallengeShift &stated);
