#pragma once

#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>

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
// A ground subset the fit left out: it correlated, but was found away from
// where the camera move puts it. Far from the fit, it is ground that moved.
struct MovedGround
{
    QPoint at;            // its centre, reference pixels
    double offset = 0.0;  // px in the target, from where the fit puts it
};

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

    // The ground it was fitted on, as drawn: "4 shapes, rectangle 240 x 30 px,
    // ...". Recorded when the run starts, as the region is, so the report and
    // the files name the ground that was used rather than whatever is marked
    // by the time anyone reads them. Empty unless fitted on ground.
    QString ground;

    // Fitted on ground only: every ground subset that correlated and was left
    // out of the fit, so the report can say WHERE ground disagreed with it, not
    // only that a count fell short. Those near the fit are the fit's own noise
    // tail; those far from it sat on something that moved.
    QVector<MovedGround> groundMoved;

    // The ground marked, and how much of it lies inside the region measured
    // (core/Roi.h, groundInsideRegion()), counted when the run starts.
    qint64 groundPixels = 0;
    qint64 groundInsideRegion = 0;

    // The known length (core/Roi.h): whether one was marked, whether the
    // camera's scale was taken from it, and what it measured. It is used only
    // when the fit is not on ground, which measures the scale itself.
    bool knownLengthMarked = false;
    bool knownLengthUsed = false;
    QPoint knownLengthA;
    QPoint knownLengthB;
    double knownLengthReference = 0.0;   // px, in the reference
    double knownLengthTarget = 0.0;      // px, in the target
    double knownLengthCorrelation = 0.0; // the poorer of its two ends
    int knownLengthEndsInside = 0;       // ends inside the region measured

    bool isAligned() const { return fittedOn != NotAligned; }
};

// The fewest ground subsets the alignment refines on: the engine's own minimum
// (SessionAlignment2D::setMinimums, which SurView leaves at its default).
inline constexpr int kMinGroundSubsets = 6;

// The step of the alignment's lattice of ground subsets: the run's own grid
// step, widened only as far as keeps a large fitting area to a few thousand of
// them. One rule for the run and for what the screen predicts about it.
int alignmentStep(int gridStep, const QRect &fitBounds);

// Why ground that holds too few whole subsets cannot align a target: said in
// the project tree while it is drawn, and in a run refused for it.
QString tooFewGroundSubsetsNote(int count, int subsetRadius);

// The Analysis panel's switch, as it reads there and wherever it is named.
QString separateSessionsLabel();

// The toolbar tool that marks stationary ground (core/Roi.h), as it reads
// there and wherever the screen names it.
QString markGroundLabel();

// The menu entry that marks a known length, and the one that removes it.
QString markKnownLengthLabel();
QString removeKnownLengthLabel();

// The note under the switch: what it does, or what turning it on would allow.
// On, it says what the camera move is fitted on, which depends on whether any
// ground is marked, and names the tool that marks it.
QString separateSessionsPanelNote(bool on, bool groundMarked = false,
                                  bool knownLengthMarked = false);

// Beside a ground shape that overlaps the region measured, and beside a known
// length with an end inside it: what that does, and how to put it right.
QString groundOverlapNote();
QString knownLengthInsideNote();

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

// What the exported files say about the alignment, in one statement: the
// report's lines, or that there was none. Stated for an ordinary run too, so a
// file that never mentions it cannot be mistaken for one that did.
QString alignmentStatement(const SessionAlignmentRecord &alignment);
