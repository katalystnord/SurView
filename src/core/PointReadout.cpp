#include "core/PointReadout.h"

#include "core/FieldLayout.h"

#include <QObject>

#include <cmath>
#include <limits>

namespace
{

// Four significant digits, which is enough to separate two neighbouring points
// in every channel this readout shows and few enough that a float's own noise
// stays out of the number. %g rather than a decimal count for the reason
// fieldScaleSignificantDigits() gives: displacement runs in whole pixels and
// strain at 1e-3 and below, and no fixed number of decimals reads well at both
// ends.
QString measured(double value)
{
    return QString::number(value, 'g', 4);
}

QString notMeasured()
{
    return QObject::tr("not measured");
}

}  // namespace

float pointPickRadius(const CorrelationResult &result)
{
    return std::max(1.f, float(result.step));
}

int pointNearestTo(const CorrelationResult &result, float x, float y)
{
    const float radius = pointPickRadius(result);
    const float reach = radius * radius;

    int nearest = -1;
    float nearestDistance = std::numeric_limits<float>::max();

    for (int i = 0; i < result.points.size(); i++) {
        const CorrelationPoint &point = result.points[i];
        const float dx = point.x - x;
        const float dy = point.y - y;
        const float distance = dx * dx + dy * dy;
        if (distance <= reach && distance < nearestDistance) {
            nearest = i;
            nearestDistance = distance;
        }
    }
    return nearest;
}

bool displacementIsBelowNoiseFloor(const CorrelationPoint &point)
{
    // A point that did not converge has no displacement to compare. Its u and v
    // are zero because nothing was written there, and zero is below every
    // floor, so an unguarded comparison would report the strongest possible
    // finding about the points that measured nothing at all.
    if (!point.converged || !point.noiseFloorMeasured)
        return false;

    const double magnitude = std::sqrt(double(point.u) * double(point.u)
                                       + double(point.v) * double(point.v));
    return magnitude <= double(point.noiseFloor);
}

PointReadout pointReadout(const CorrelationResult &result, int index)
{
    PointReadout readout;

    if (index < 0 || index >= result.points.size()) {
        readout.lines.append({QObject::tr("Point"),
                              QObject::tr("no point measured here"),
                              QObject::tr("The grid reaches only as far as the "
                                          "region that was measured."),
                              false});
        return readout;
    }

    const CorrelationPoint &point = result.points[index];
    readout.measured = true;
    readout.solved = point.converged;

    readout.lines.append({QObject::tr("Reference px"),
                          QStringLiteral("%1, %2")
                              .arg(QString::number(double(point.x), 'g', 6),
                                   QString::number(double(point.y), 'g', 6)),
                          QString(), false});

    if (!point.converged) {
        // ⚑ No number here, ever. The same rule the colour map and the .vtu
        // export keep: a rejected point is not a displacement of zero. A
        // readout printing "0.0 px" would be the one place in the application
        // that says otherwise, and it would be believed, because it is a
        // number beside a label.
        readout.lines.append({QObject::tr("Displacement"),
                              notMeasured(),
                              point.failureReason.isEmpty()
                                  ? QObject::tr("The solver rejected this point.")
                                  : QObject::tr("Rejected: %1.")
                                        .arg(point.failureReason),
                              true});
        return readout;
    }

    // How this point reached its answer. Stated here rather than only in the
    // run report, because the place a reader asks about one point is that
    // point. It is a real measurement and the note says so -- the mark is not
    // a caveat, it is provenance.
    if (point.recovered) {
        readout.lines.append({QObject::tr("Measured on"),
                              QObject::tr("the second pass"),
                              QObject::tr("The first solve did not measure this "
                                          "point well. It was solved again from "
                                          "a displacement fitted to its reliable "
                                          "neighbours, and the correlation below "
                                          "is its own."),
                              false});
    }

    readout.lines.append({QObject::tr("Displacement u, v"),
                          QObject::tr("%1, %2 px")
                              .arg(measured(double(point.u)),
                                   measured(double(point.v))),
                          QString(), false});

    const double magnitude = std::sqrt(double(point.u) * double(point.u)
                                       + double(point.v) * double(point.v));
    readout.lines.append({QObject::tr("Magnitude"),
                          QObject::tr("%1 px").arg(measured(magnitude)),
                          QString(), false});

    readout.lines.append({QObject::tr("Correlation zncc"),
                          measured(double(point.zncc)),
                          point.zncc < double(kStrainFitCorrelationFloor)
                              ? QObject::tr("Below %1, so this point is "
                                            "excluded from every strain fit.")
                                    .arg(double(kStrainFitCorrelationFloor),
                                         0, 'g', 2)
                              : QString(),
                          false});

    // --- how far this one point can be trusted ------------------------------
    if (point.noiseFloorMeasured) {
        const bool belowFloor = displacementIsBelowNoiseFloor(point);
        QString note = fieldChannelNote(FieldChannel::NoiseFloor);
        if (belowFloor) {
            // The finding this whole readout was worth building for. Said at
            // the point, in the place somebody is already asking about it.
            note = QObject::tr("This point moved %1 px, which its own subset "
                               "could not resolve: the displacement is not "
                               "distinguishable from image noise. %2")
                       .arg(measured(magnitude), note);
        }
        readout.lines.append({QObject::tr("Noise floor, sigma"),
                              QObject::tr("%1 px").arg(measured(double(point.noiseFloor))),
                              note, belowFloor});
    } else {
        readout.lines.append({QObject::tr("Noise floor, sigma"), notMeasured(),
                              fieldChannelNote(FieldChannel::NoiseFloor), false});
    }

    if (point.conditioningMeasured) {
        readout.lines.append({QObject::tr("Match conditioning, beta"),
                              measured(double(point.conditioning)),
                              fieldChannelNote(FieldChannel::MatchConditioning),
                              false});
    } else {
        // ⚑ At a CONVERGED point the engine's sentinel cannot mean "not
        // computed" -- that fires only for a failed or out-of-bounds point,
        // neither of which reaches here. It can only mean the probe found the
        // cost too flat to measure, which is the strongest caution this metric
        // can give, so it is stated rather than left blank.
        readout.lines.append({QObject::tr("Match conditioning, beta"),
                              QObject::tr("too flat to probe"),
                              QObject::tr("The correlation cost around this "
                                          "match was too flat for the probe to "
                                          "establish a value, which is itself a "
                                          "caution about the match."),
                              true});
    }

    // --- strain, which was fitted from the neighbours rather than measured ---
    // Absent when the run never asked for strain, rather than present and
    // empty: an empty strain row reads as a fit that failed, when in fact none
    // was ever attempted. The same distinction the .vtu export makes by
    // omitting the arrays entirely.
    if (result.strainRequested) {
        if (point.strainFitted) {
            readout.lines.append({QObject::tr("Strain exx, eyy, exy"),
                                  QStringLiteral("%1, %2, %3")
                                      .arg(measured(double(point.exx)),
                                           measured(double(point.eyy)),
                                           measured(double(point.exy))),
                                  QObject::tr("Fitted from the displacements of "
                                              "this point's neighbours, not "
                                              "measured here."),
                                  false});
        } else {
            readout.lines.append({QObject::tr("Strain exx, eyy, exy"),
                                  QObject::tr("not fitted here"),
                                  QObject::tr("Too few neighbours cleared the "
                                              "correlation floor for a fit. Not "
                                              "a strain of zero."),
                                  true});
        }
    }

    return readout;
}
