#include "core/KnownAnswer.h"

#include "core/Correlation.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr float kNoValue = std::numeric_limits<float>::quiet_NaN();

double readMatrix(const QJsonArray &rows, int row, int column)
{
    return rows.at(row).toArray().at(column).toDouble();
}

// The displacement gradient of the stated deformation: grad u = F - I. Every
// strain form below is written in terms of these four, because that is how the
// engine writes its own (see oc_strain.cpp), and a stated answer computed from a
// different arrangement of the same numbers would differ from the measurement by
// the difference between two conventions rather than by any error.
struct Gradient
{
    double ux, uy, vx, vy;
};

Gradient gradientOf(const KnownAnswer &answer)
{
    return {answer.f00 - 1.0, answer.f01, answer.f10, answer.f11 - 1.0};
}

}  // namespace

QString statedAnswerPathFor(const QString &imagePath)
{
    return QFileInfo(imagePath).absolutePath() + QStringLiteral("/ground_truth.json");
}

KnownAnswer knownAnswerFor(const QString &imagePath)
{
    return knownAnswerFromFile(statedAnswerPathFor(imagePath),
                               QFileInfo(imagePath).fileName());
}

KnownAnswer knownAnswerFromFile(const QString &groundTruthPath,
                                const QString &imageFileName)
{
    KnownAnswer answer;

    QFile file(groundTruthPath);
    if (!file.open(QIODevice::ReadOnly))
        return answer;

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject sets = root[QStringLiteral("sets")].toObject();

    for (auto set = sets.constBegin(); set != sets.constEnd(); ++set) {
        const QJsonObject body = set.value().toObject();
        const QJsonArray frames = body[QStringLiteral("frames")].toArray();
        for (int index = 0; index < frames.size(); index++) {
            const QJsonObject frame = frames.at(index).toObject();
            // By NAME. See the header: the set's frames and the application's
            // target list are two orderings of the same images, and a
            // comparison against the wrong frame's answer is a plausible error
            // map of an experiment nobody ran.
            if (frame[QStringLiteral("file")].toString() != imageFileName)
                continue;

            const QJsonArray f = frame[QStringLiteral("deformation_gradient")].toArray();
            const QJsonArray shift = frame[QStringLiteral("rigid_shift_px")].toArray();
            if (f.size() != 2 || shift.size() != 2)
                return answer;

            answer.valid = true;
            answer.set = set.key();
            answer.frame = index;
            answer.frameFile = imageFileName;
            answer.whatItShows = body[QStringLiteral("what_it_shows")].toString();
            answer.howTheAnswerIsExact =
                root[QStringLiteral("how_the_answer_is_exact")].toString();
            answer.source = groundTruthPath;
            answer.f00 = readMatrix(f, 0, 0);
            answer.f01 = readMatrix(f, 0, 1);
            answer.f10 = readMatrix(f, 1, 0);
            answer.f11 = readMatrix(f, 1, 1);
            answer.shiftX = shift.at(0).toDouble();
            answer.shiftY = shift.at(1).toDouble();

            const QJsonValue amount = frame[QStringLiteral("amount")];
            if (!amount.isUndefined())
                answer.amount = QString::number(amount.toDouble());
            return answer;
        }
    }
    return answer;
}

bool knownAnswerCoversChannel(FieldChannel channel)
{
    // A deformation says everything about what moved and nothing about how well
    // it could be measured or by which pass, so the two reliability channels and
    // the repair flag are outside what it can speak to.
    return !fieldChannelIsReliability(channel) && !fieldChannelIsFlag(channel);
}

double statedValue(const KnownAnswer &answer, FieldChannel channel,
                   double x, double y, StrainMeasure measure)
{
    const Gradient g = gradientOf(answer);

    // The displacement the stated deformation puts at this reference pixel:
    // where the pixel goes, less where it started.
    const double u = answer.f00 * x + answer.f01 * y + answer.shiftX - x;
    const double v = answer.f10 * x + answer.f11 * y + answer.shiftY - y;

    switch (channel) {
    case FieldChannel::DisplacementMagnitude:
        return std::hypot(u, v);
    case FieldChannel::DisplacementX:
        return u;
    case FieldChannel::DisplacementY:
        return v;

    case FieldChannel::StrainXX:
        return measure == StrainMeasure::GreenLagrange
                   ? g.ux + 0.5 * (g.ux * g.ux + g.vx * g.vx)
                   : g.ux;
    case FieldChannel::StrainYY:
        return measure == StrainMeasure::GreenLagrange
                   ? g.vy + 0.5 * (g.uy * g.uy + g.vy * g.vy)
                   : g.vy;
    case FieldChannel::StrainXY:
        return measure == StrainMeasure::GreenLagrange
                   ? 0.5 * (g.uy + g.vx + g.uy * g.ux + g.vy * g.vx)
                   : 0.5 * (g.uy + g.vx);

    case FieldChannel::NoiseFloor:
    case FieldChannel::MatchConditioning:
    case FieldChannel::RecoveredOnSecondPass:
        break;
    }
    return kNoValue;
}

namespace {

// Whether the run actually MEASURED this channel at this point. A strain
// channel needs the fit to have succeeded here; a displacement channel needs
// the solver to have converged. Neither is implied by the other.
bool pointCarries(const CorrelationPoint &point, FieldChannel channel)
{
    if (!point.converged)
        return false;
    if (fieldChannelIsStrain(channel))
        return point.strainFitted;
    return true;
}

double measuredValue(const CorrelationPoint &point, FieldChannel channel)
{
    switch (channel) {
    case FieldChannel::DisplacementMagnitude:
        return std::hypot(double(point.u), double(point.v));
    case FieldChannel::DisplacementX:
        return double(point.u);
    case FieldChannel::DisplacementY:
        return double(point.v);
    case FieldChannel::StrainXX:
        return double(point.exx);
    case FieldChannel::StrainYY:
        return double(point.eyy);
    case FieldChannel::StrainXY:
        return double(point.exy);
    default:
        break;
    }
    return kNoValue;
}

}  // namespace

QVector<float> layoutStatedField(const CorrelationResult &result,
                                 FieldChannel channel, const KnownAnswer &answer)
{
    QVector<float> cells(result.gridColumns * result.gridRows, kNoValue);
    if (!answer.valid || !knownAnswerCoversChannel(channel))
        return cells;

    for (const CorrelationPoint &point : result.points) {
        if (point.gridIndex < 0 || point.gridIndex >= cells.size())
            continue;
        // Every point the run ATTEMPTED, its failures included: the answer is
        // known wherever the instrument was pointed.
        cells[point.gridIndex] = float(statedValue(answer, channel, double(point.x),
                                                   double(point.y),
                                                   result.strainMeasure));
    }
    return cells;
}

QVector<float> layoutErrorField(const CorrelationResult &result,
                                FieldChannel channel, const KnownAnswer &answer)
{
    QVector<float> cells(result.gridColumns * result.gridRows, kNoValue);
    if (!answer.valid || !knownAnswerCoversChannel(channel))
        return cells;

    for (const CorrelationPoint &point : result.points) {
        if (point.gridIndex < 0 || point.gridIndex >= cells.size())
            continue;
        if (!pointCarries(point, channel))
            continue;   // absent, never zero
        const double stated = statedValue(answer, channel, double(point.x),
                                          double(point.y), result.strainMeasure);
        cells[point.gridIndex] = float(measuredValue(point, channel) - stated);
    }
    return cells;
}

AccuracyReport accuracyAgainstStated(const CorrelationResult &result,
                                     FieldChannel channel,
                                     const KnownAnswer &answer)
{
    AccuracyReport report;
    if (!answer.valid || !knownAnswerCoversChannel(channel))
        return report;

    report.attempted = int(result.points.size());

    double sumAbsolute = 0.0;
    double sumSquares = 0.0;
    for (const CorrelationPoint &point : result.points) {
        if (!pointCarries(point, channel))
            continue;
        const double stated = statedValue(answer, channel, double(point.x),
                                          double(point.y), result.strainMeasure);
        const double error = measuredValue(point, channel) - stated;
        const double magnitude = std::abs(error);

        report.compared++;
        sumAbsolute += magnitude;
        sumSquares += error * error;
        if (magnitude > report.worstAbsolute) {
            report.worstAbsolute = magnitude;
            report.worstAtX = double(point.x);
            report.worstAtY = double(point.y);
        }
    }

    // ⚑ Nothing compared is NOT an accuracy of zero. A default-constructed
    // report reads as a perfect measurement, which is the flattering reading of
    // a run that measured nothing at all.
    if (report.compared == 0)
        return report;

    report.valid = true;
    report.meanAbsolute = sumAbsolute / report.compared;
    report.rms = std::sqrt(sumSquares / report.compared);
    return report;
}

namespace {

// Smallest and largest value present, skipping every cell nothing was measured
// in. False when there is nothing at all.
bool rangeOf(const QVector<float> &cells, double &lowest, double &highest)
{
    bool any = false;
    for (float value : cells) {
        if (std::isnan(value))
            continue;
        if (!any) {
            lowest = highest = value;
            any = true;
            continue;
        }
        lowest = std::min(lowest, double(value));
        highest = std::max(highest, double(value));
    }
    return any;
}

}  // namespace

bool sharedColourRange(const QVector<float> &measured, const QVector<float> &stated,
                       bool centredOnZero, double &lowest, double &highest)
{
    double measuredLow = 0.0, measuredHigh = 0.0;
    double statedLow = 0.0, statedHigh = 0.0;
    const bool hasMeasured = rangeOf(measured, measuredLow, measuredHigh);
    const bool hasStated = rangeOf(stated, statedLow, statedHigh);
    if (!hasMeasured && !hasStated)
        return false;

    if (!hasMeasured) {
        lowest = statedLow;
        highest = statedHigh;
    } else if (!hasStated) {
        lowest = measuredLow;
        highest = measuredHigh;
    } else {
        lowest = std::min(measuredLow, statedLow);
        highest = std::max(measuredHigh, statedHigh);
    }

    if (centredOnZero) {
        const double reach = std::max(std::abs(lowest), std::abs(highest));
        lowest = -reach;
        highest = reach;
    }
    return true;
}

bool errorColourRange(const QVector<float> &error, double &lowest, double &highest)
{
    double low = 0.0, high = 0.0;
    if (!rangeOf(error, low, high))
        return false;

    const double reach = std::max(std::abs(low), std::abs(high));
    lowest = -reach;
    highest = reach;
    return true;
}
