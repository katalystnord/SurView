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
                                 FieldChannel::StrainXY}) {
        // Asked of the same functions everything else asks, so this list
        // cannot drift from how a channel actually behaves.
        channels.append(FieldChannelInfo{
            channel,
            fieldChannelName(channel),
            fieldChannelUnit(channel),
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
    }
    return QString();
}

QString fieldChannelUnit(FieldChannel channel)
{
    // Strain is a ratio and has no unit. Saying so beats leaving it blank,
    // which reads as an oversight, and beats inventing one.
    return fieldChannelIsStrain(channel) ? QObject::tr("dimensionless")
                                         : QObject::tr("px");
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
