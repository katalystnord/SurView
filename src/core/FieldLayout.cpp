#include "core/FieldLayout.h"

#include "core/Correlation.h"

#include <QObject>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// The channel's value at one point, or not-a-number where that point holds no
// measurement of it. One place, so the layout and the range cannot disagree
// about which points count.
float valueAt(const CorrelationPoint &point, FieldChannel channel)
{
    const float nothing = std::numeric_limits<float>::quiet_NaN();

    if (fieldChannelIsStrain(channel)) {
        if (!point.strainFitted)
            return nothing;
        switch (channel) {
        case FieldChannel::StrainXX:
            return point.exx;
        case FieldChannel::StrainYY:
            return point.eyy;
        case FieldChannel::StrainXY:
            return point.exy;
        default:
            return nothing;
        }
    }

    if (!point.converged)
        return nothing;

    switch (channel) {
    case FieldChannel::NoiseFloor:
        // Zero would claim a perfect measurement, so an unmeasured one is
        // not-a-number exactly as an unsolved displacement is.
        return point.noiseFloorMeasured ? point.noiseFloor : nothing;
    case FieldChannel::MatchConditioning:
        return point.conditioningMeasured ? point.conditioning : nothing;
    default:
        break;
    }

    switch (channel) {
    case FieldChannel::DisplacementMagnitude:
        return float(std::hypot(point.u, point.v));
    case FieldChannel::DisplacementX:
        return point.u;
    case FieldChannel::DisplacementY:
        return point.v;
    default:
        return nothing;
    }
}

}  // namespace

QVector<FieldChannelInfo> offeredFieldChannels()
{
    QVector<FieldChannelInfo> channels;
    for (FieldChannel channel : {FieldChannel::DisplacementMagnitude,
                                 FieldChannel::DisplacementX,
                                 FieldChannel::DisplacementY,
                                 FieldChannel::StrainXX,
                                 FieldChannel::StrainYY,
                                 FieldChannel::StrainXY,
                                 FieldChannel::NoiseFloor,
                                 FieldChannel::MatchConditioning}) {
        // Asked of the same functions everything else asks, so this list
        // cannot drift from how a channel actually behaves.
        channels.append(FieldChannelInfo{
            channel,
            fieldChannelName(channel),
            fieldChannelUnit(channel),
            fieldChannelNote(channel),
            fieldChannelIsStrain(channel),
            fieldChannelIsCentredOnZero(channel),
        });
    }
    return channels;
}

QString fieldChannelName(FieldChannel channel)
{
    switch (channel) {
    case FieldChannel::DisplacementMagnitude:
        return QObject::tr("Displacement magnitude");
    case FieldChannel::DisplacementX:
        return QObject::tr("Displacement u (x)");
    case FieldChannel::DisplacementY:
        return QObject::tr("Displacement v (y)");
    case FieldChannel::StrainXX:
        return QObject::tr("Strain exx");
    case FieldChannel::StrainYY:
        return QObject::tr("Strain eyy");
    case FieldChannel::StrainXY:
        return QObject::tr("Shear strain exy");
    // Named for what it is first and for what the literature calls it second,
    // so both audiences find it: a reader who has never met the symbol learns
    // that this is a floor rather than an error bar, and a reader who knows
    // DIC recognises sigma and beta on sight.
    case FieldChannel::NoiseFloor:
        return QObject::tr("Noise floor, sigma");
    case FieldChannel::MatchConditioning:
        return QObject::tr("Match conditioning, beta");
    }
    return QString();
}

QString fieldChannelUnit(FieldChannel channel)
{
    // Strain is a ratio and conditioning is a reciprocal slope carrying
    // arbitrary per-axis factors; neither has a unit. Saying so beats leaving
    // it blank, which reads as an oversight, and beats inventing one. The noise
    // floor is a displacement and is in pixels like the rest.
    return fieldChannelIsDimensionless(channel) ? QObject::tr("dimensionless")
                                                : QObject::tr("px");
}

bool fieldChannelIsDimensionless(FieldChannel channel)
{
    // Strain is a ratio; conditioning is a reciprocal slope carrying arbitrary
    // per-axis factors. The noise floor is a displacement and is in pixels like
    // the rest.
    return fieldChannelIsStrain(channel)
           || channel == FieldChannel::MatchConditioning;
}

bool fieldChannelIsReliability(FieldChannel channel)
{
    return channel == FieldChannel::NoiseFloor
           || channel == FieldChannel::MatchConditioning;
}

QString fieldChannelNote(FieldChannel channel)
{
    switch (channel) {
    case FieldChannel::DisplacementMagnitude:
        return QObject::tr("How far each point moved, regardless of direction. "
                           "Measured against the reference frame, so a specimen "
                           "that simply shifted reads the same as one that "
                           "deformed.");
    case FieldChannel::DisplacementX:
        return QObject::tr("Movement along x, positive to the right.");
    case FieldChannel::DisplacementY:
        return QObject::tr("Movement along y, positive DOWN the image, matching "
                           "the image's own row order.");
    case FieldChannel::StrainXX:
    case FieldChannel::StrainYY:
    case FieldChannel::StrainXY:
        return QObject::tr("Fitted from the displacements of neighbouring "
                           "points, not measured at this one, so it describes a "
                           "subregion rather than a pixel.");
    case FieldChannel::NoiseFloor:
        // The sentence this whole channel exists to be read alongside.
        return QObject::tr(
            "DIC's sigma: the finest displacement this subset's speckle can "
            "resolve against the image noise. A lower bound on error, not a "
            "total error bar - it excludes interpolation bias, out-of-plane "
            "motion and systematic error, and never examines the target image. "
            "Larger is worse.");
    case FieldChannel::MatchConditioning:
        return QObject::tr(
            "DIC's beta: how sharply the correlation cost rises around the "
            "solution that was found. Unlike the noise floor it does judge this "
            "particular match, but it is a relative score with no absolute "
            "meaning - compare points within one run, never across runs. "
            "Larger is worse.");
    }
    return QString();
}

bool fieldChannelIsStrain(FieldChannel channel)
{
    switch (channel) {
    case FieldChannel::StrainXX:
    case FieldChannel::StrainYY:
    case FieldChannel::StrainXY:
        return true;
    case FieldChannel::DisplacementMagnitude:
    case FieldChannel::DisplacementX:
    case FieldChannel::DisplacementY:
    case FieldChannel::NoiseFloor:
    case FieldChannel::MatchConditioning:
        return false;
    }
    return false;
}

bool fieldChannelIsCentredOnZero(FieldChannel channel)
{
    // Strain only. Displacement is measured against wherever the reference
    // frame happens to sit, so a specimen that simply translated has a large
    // displacement everywhere and no strain anywhere -- centring its scale on
    // zero would spend half the colours on values that cannot occur and flatten
    // the variation that is actually the measurement.
    return fieldChannelIsStrain(channel);
}

QVector<float> layoutField(const CorrelationResult &result, FieldChannel channel)
{
    const int cells = result.gridColumns * result.gridRows;
    if (cells <= 0)
        return {};

    QVector<float> values(cells, std::numeric_limits<float>::quiet_NaN());

    for (const CorrelationPoint &point : result.points) {
        // Placed by the cell the point recorded, never by its position in the
        // list: with a region excluding cells the two differ, and filling in
        // list order slides the whole field sideways.
        if (point.gridIndex < 0 || point.gridIndex >= cells)
            continue;
        const float value = valueAt(point, channel);
        if (!std::isnan(value))
            values[point.gridIndex] = value;
    }

    return values;
}

bool fieldValueRange(const CorrelationResult &result, FieldChannel channel,
                     double &lowest, double &highest)
{
    bool any = false;
    for (const CorrelationPoint &point : result.points) {
        const float value = valueAt(point, channel);
        if (std::isnan(value))
            continue;
        if (!any) {
            lowest = highest = value;
            any = true;
        } else {
            lowest = std::min(lowest, double(value));
            highest = std::max(highest, double(value));
        }
    }
    return any;
}

int fieldScaleSignificantDigits(double lowest, double highest)
{
    // Three is the floor: fewer cannot separate two neighbouring ticks of any
    // scale worth drawing. Nine is the ceiling: a float carries about seven
    // significant figures, so digits past this are the storage format talking
    // rather than the instrument.
    constexpr int kFewest = 3;
    constexpr int kMost = 9;

    const double span = highest - lowest;
    if (!(span > 0.0))
        return kFewest;

    const double reach = std::max(std::abs(lowest), std::abs(highest));
    if (!(reach > 0.0))
        return kFewest;

    // Against the gap between neighbouring ticks, not the span: the labels have
    // to differ from EACH OTHER, and the scale bar draws five of them. The
    // number of digits between the largest label and that gap is how many the
    // reader needs before the labels stop repeating.
    const double tickStep = span / 4.0;
    const int needed = int(std::ceil(std::log10(reach / tickStep))) + 1;
    return std::clamp(needed, kFewest, kMost);
}

namespace
{

// Every measured noise floor in the run, sorted. Shared by the two functions
// below so they cannot disagree about which points count.
QVector<double> sortedNoiseFloors(const CorrelationResult &result)
{
    QVector<double> floors;
    floors.reserve(result.points.size());
    for (const CorrelationPoint &point : result.points) {
        if (point.converged && point.noiseFloorMeasured && point.noiseFloor > 0.f)
            floors.append(double(point.noiseFloor));
    }
    std::sort(floors.begin(), floors.end());
    return floors;
}

// Nearest rank, so the answer is always a floor some point actually has rather
// than an interpolation between two of them.
double percentileOf(const QVector<double> &sorted, double fraction)
{
    const int rank = int(std::ceil(fraction * sorted.size())) - 1;
    return sorted.at(std::clamp(rank, 0, int(sorted.size()) - 1));
}

}  // namespace

bool noiseFloorSpread(const CorrelationResult &result, double &lowest,
                      double &typicalHighest)
{
    const QVector<double> floors = sortedNoiseFloors(result);
    if (floors.isEmpty())
        return false;

    lowest = floors.first();
    typicalHighest = percentileOf(floors, 0.95);
    return true;
}

QString noiseFloorAgainstMovement(const CorrelationResult &result)
{
    // ⚑ A PERCENTILE, not the worst point. The worst point is not the field.
    //
    // Found on real data, and only findable there: a synthetic pattern fills
    // the frame, while a photograph of a specimen leaves dark background around
    // it that the grid covers too. A subset out there carries almost no
    // gradient energy, so its noise floor is enormous -- and it correlates
    // beautifully, because flat matches flat. On the pyALDIC tension sequence
    // just 8 of 5401 solved points fell below 0.9, so no correlation threshold
    // would have excluded them, and one of them made an excellent measurement
    // report itself as "at worst one part in 3".
    const QVector<double> floors = sortedNoiseFloors(result);
    if (floors.isEmpty())
        return QString();

    // With two points the 95th percentile is the poorer of the two, which is
    // the honest reading of "95 per cent" on a sample that small.
    const double floor = percentileOf(floors, 0.95);
    if (!(floor > 0.0))
        return QString();

    double smallest = 0.0;
    double largest = 0.0;
    if (!fieldValueRange(result, FieldChannel::DisplacementMagnitude, smallest,
                         largest)) {
        return QString();
    }

    // A movement no larger than the floor that qualifies it is not a movement
    // this can put in proportion; saying "one part in 1" would be worse than
    // saying nothing.
    if (largest <= floor)
        return QString();

    const double ratio = largest / floor;
    return QObject::tr("For 95 per cent of measured points, one part in %1 of "
                       "the largest displacement measured, or better.")
        .arg(qRound(ratio));
}

bool fieldColourRange(const CorrelationResult &result, FieldChannel channel,
                      double &lowest, double &highest)
{
    if (!fieldValueRange(result, channel, lowest, highest))
        return false;

    // Tension and compression are opposite states, not two ends of one
    // continuum. Left on the range the data happens to occupy, a field that
    // ran from -0.001 to +0.004 would put the scale's midpoint at +0.0015 and
    // colour a compressed region the same as an unstrained one.
    if (fieldChannelIsCentredOnZero(channel)) {
        const double reach = std::max(std::abs(lowest), std::abs(highest));
        lowest = -reach;
        highest = reach;
    }

    return true;
}
