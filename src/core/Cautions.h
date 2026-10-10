#pragma once

#include <QRectF>
#include <QString>
#include <QVector>

struct CorrelationPoint;
struct CorrelationResult;

// Which measured points carry a stated caution, and how many, so the places
// worth doubting can be seen over whatever map is on screen.
//
// WHY THIS EXISTS. Every caution here was already stated somewhere -- in the
// point readout, in the run report, on a map of its own -- and a reader looking
// at a displacement or strain map saw none of them. The question the map raises
// is "which of these colours should I not lean on", and the answer was three
// clicks and a channel change away.
//
// ⚑ A COUNT, NEVER A CONFIDENCE. David asked for a number giving the level of
// confidence in the view. Nothing computed here converts to a probability that
// a point is right, and a percentage labelled confidence claims a calibration
// this application does not have -- the same reason the noise floor and the
// conditioning are two numbers and not one score. What can be stated exactly is
// how many measured points carry no caution, how many carry one, and why.
//
// ⚑ CAUTIONS, NOT REJECTIONS. A cautioned point stays in the field, coloured
// like any other; the overlay marks it. Refusing points would lose them, and
// marking them is what lets them be kept (the same posture as the second pass).
//
// A point the second pass repaired is NOT cautioned: it was solved in full and
// carries its own correlation, which is judged here like anyone else's.

enum class Caution
{
    // More than kMostlyClipped of the reference subset at the image's own
    // darkest or brightest value (core/Clipping.h).
    MostlyClipped,
    // Moved no further than its own noise floor, so the movement is not
    // distinguishable from image noise (displacementIsBelowNoiseFloor()).
    BelowNoiseFloor,
    // Correlated below kStrainFitCorrelationFloor, and so excluded from every
    // strain fit already.
    PoorCorrelation,
    // The match conditioning probe found the cost too flat to establish.
    ConditioningUnusable,
};

// Every cause, in the order they are drawn and listed.
QVector<Caution> allCautions();

// The cautions this point carries. Empty for a point that was not measured:
// a rejected point is a hole already, and a caution is something said about a
// measurement.
QVector<Caution> cautionsAt(const CorrelationPoint &point);

// The mark that stands for a cause on screen and in its legend.
QString cautionMark(Caution caution);

// The cause in a few words, as the bar and the legend say it.
QString cautionName(Caution caution);

struct CautionCount
{
    int measured = 0;    // converged points counted
    int cautioned = 0;   // of those, carrying at least one caution
    // Per cause, in allCautions() order. A point with two cautions counts once
    // in `cautioned` and once under each cause.
    QVector<int> byCause;
    bool inView = false; // counted over part of the field, not all of it
};

// Counted over the points whose position lies inside `view`, in image pixels,
// or over every point when `view` is null.
CautionCount countCautions(const CorrelationResult &result, const QRectF &view = QRectF());

// What the bar says beside the marks: the count, and each cause with its mark.
QString cautionSummary(const CautionCount &count);
