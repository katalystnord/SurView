#pragma once

#include <QString>
#include <QStringList>

// A reference and a target photographed in separate sessions: what the run
// recorded about bringing one camera's view onto the other's, and what the
// screen, the report and the files say about it. Engine-free. The alignment
// itself is the engine's SessionAlignment2D, run by core/Correlation.cpp.
//
// The camera move is estimated as a similarity (scale, rotation, shift) and
// applied to COORDINATES: every point starts from it, is measured on the target
// as it was recorded, and is carried back into the reference frame. No image is
// resampled at any step (tenet 10).
//
// ⚑ WHAT IT IS FITTED ON DECIDES WHAT CAN BE MEASURED. On ground marked as
// stationary, nothing about the specimen is absorbed. With no ground, the fit
// can only use what is being measured, and a similarity fitted on the specimen
// takes up the specimen's own rigid motion and uniform dilatation: a real
// strain, lost while every point still reads plausibly. That is allowed (David,
// 2026-10-12: a recovered strip has no ground at all) only because the result
// states it, which is what alignmentLimitation() is for.
struct SessionAlignmentRecord
{
    enum FittedOn {
        NotAligned,      // same camera, same pixel grid: the ordinary run
        Ground,          // fitted on ground marked as stationary
        MeasuredRegion,  // no ground: fitted on the region being measured
        WholeImage,      // no ground and no region: fitted on the whole image
    };

    FittedOn fittedOn = NotAligned;

    // Reference pixel X to target pixel: x = scale * R(angle) * X + shift.
    double scale = 1.0;
    double angleDegrees = 0.0;   // turning +x toward +y: clockwise on screen
    double shiftX = 0.0;
    double shiftY = 0.0;

    int targetWidth = 0;
    int targetHeight = 0;

    int matches = 0;            // feature matches on what it was fitted on
    int inliers = 0;            // of those, agreeing with the similarity
    int groundPoints = 0;       // subsets correlated to refine it
    int groundPointsUsed = 0;   // of those, kept by the final fit
    double residualRms = 0.0;   // px, of the kept subsets about the fit

    bool isAligned() const { return fittedOn != NotAligned; }
};

// The Analysis panel's switch, as it reads there and wherever it is named.
QString separateSessionsLabel();

// The toolbar tool that marks stationary ground (core/Roi.h), as it reads
// there and wherever the screen names it.
QString markGroundLabel();

// The note under the switch: what it does, or what turning it on would allow.
// On, it says what the camera move is fitted on, which depends on whether any
// ground is marked, and names the tool that marks it.
QString separateSessionsPanelNote(bool on, bool groundMarked = false);

// Why the ground tool is unavailable: with one session there is nothing to
// align, and the tool says which switch would change that.
QString markGroundUnavailableNote();

// Why re-anchoring is unavailable while the switch is on. A followed point sits
// in a later frame's camera, which no alignment describes.
QString separateSessionsReanchorNote();

// What an alignment fitted on anything but ground could not measure, in one
// sentence for the report and the files. Empty when nothing was lost.
QString alignmentLimitation(const SessionAlignmentRecord &alignment);

// The run report's lines about the alignment; empty for an ordinary run.
QStringList alignmentReport(const SessionAlignmentRecord &alignment);
