#include "core/Series.h"

#include "core/Correlation.h"

#include <QCoreApplication>

#include <cmath>
#include <limits>

namespace {

// The measured point occupying one cell of the grid, or nullptr where the cell
// is empty or its point was rejected. Asked by CELL, never by position in the
// list: a region of interest makes the list sparse, and walking it in order
// would slide the whole field sideways.
const CorrelationPoint *measuredAtCell(const CorrelationResult &field,
                                       int column, int row)
{
    if (column < 0 || row < 0
        || column >= field.gridColumns || row >= field.gridRows) {
        return nullptr;
    }

    const int wanted = row * field.gridColumns + column;
    for (const CorrelationPoint &point : field.points) {
        if (point.gridIndex != wanted)
            continue;
        return point.converged ? &point : nullptr;
    }
    return nullptr;
}

double channelValue(const CorrelationPoint &point, FieldChannel channel)
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
    case FieldChannel::NoiseFloor:
        return double(point.noiseFloor);
    case FieldChannel::MatchConditioning:
        return double(point.conditioning);
    case FieldChannel::RecoveredOnSecondPass:
        return point.recovered ? 1.0 : 0.0;
    }
    return 0.0;
}

// Whether this point carries the channel at all, which is a different question
// from whether it converged: strain is fitted separately and declines on its
// own terms.
bool pointHasChannel(const CorrelationPoint &point, FieldChannel channel)
{
    if (!point.converged)
        return false;
    if (fieldChannelIsStrain(channel))
        return point.strainFitted;
    if (channel == FieldChannel::NoiseFloor)
        return point.noiseFloorMeasured;
    if (channel == FieldChannel::MatchConditioning)
        return point.conditioningMeasured;
    return true;
}

}  // namespace

FieldSample sampleFieldAt(const CorrelationResult &field, double x, double y)
{
    FieldSample sample;

    if (field.step <= 0 || field.gridColumns <= 0 || field.gridRows <= 0)
        return sample;

    const double cx = (x - double(field.originX)) / double(field.step);
    const double cy = (y - double(field.originY)) / double(field.step);

    if (cx < 0.0 || cy < 0.0
        || cx > double(field.gridColumns - 1) || cy > double(field.gridRows - 1)) {
        return sample;
    }

    // The cell containing the position, clamped so that a position sitting
    // exactly on the far edge belongs to the last cell rather than to one that
    // does not exist.
    int column = int(std::floor(cx));
    int row = int(std::floor(cy));
    column = std::min(column, field.gridColumns - 2 >= 0 ? field.gridColumns - 2 : 0);
    row = std::min(row, field.gridRows - 2 >= 0 ? field.gridRows - 2 : 0);

    const CorrelationPoint *p00 = measuredAtCell(field, column, row);
    const CorrelationPoint *p10 = measuredAtCell(field, column + 1, row);
    const CorrelationPoint *p01 = measuredAtCell(field, column, row + 1);
    const CorrelationPoint *p11 = measuredAtCell(field, column + 1, row + 1);

    // ⚑ All four, or nothing. See the header for why every repair is a lie.
    if (!p00 || !p10 || !p01 || !p11)
        return sample;

    const double fx = cx - double(column);
    const double fy = cy - double(row);

    const auto blend = [&](double v00, double v10, double v01, double v11) {
        return v00 * (1.0 - fx) * (1.0 - fy) + v10 * fx * (1.0 - fy)
               + v01 * (1.0 - fx) * fy + v11 * fx * fy;
    };

    sample.u = blend(p00->u, p10->u, p01->u, p11->u);
    sample.v = blend(p00->v, p10->v, p01->v, p11->v);
    sample.measured = true;
    return sample;
}

bool Extensometer::isValid() const
{
    return referenceLength() > 0.0;
}

double Extensometer::referenceLength() const
{
    return std::hypot(bx - ax, by - ay);
}

ExtensometerReading readExtensometer(const Extensometer &gauge,
                                     const CorrelationResult &field)
{
    ExtensometerReading reading;
    if (!gauge.isValid())
        return reading;

    const FieldSample a = sampleFieldAt(field, gauge.ax, gauge.ay);
    const FieldSample b = sampleFieldAt(field, gauge.bx, gauge.by);
    if (!a.measured || !b.measured)
        return reading;

    // ⚑ The distance BETWEEN the deformed anchors. Not the movement of either:
    // a specimen carried bodily across the frame has strained by nothing, and
    // reading one anchor's displacement would turn that into a large strain.
    const double axNow = gauge.ax + a.u;
    const double ayNow = gauge.ay + a.v;
    const double bxNow = gauge.bx + b.u;
    const double byNow = gauge.by + b.v;

    reading.referenceLength = gauge.referenceLength();
    reading.length = std::hypot(bxNow - axNow, byNow - ayNow);
    reading.elongation = reading.length - reading.referenceLength;
    reading.strain = reading.elongation / reading.referenceLength;
    reading.measured = true;
    return reading;
}

QString extensometerQuantityName(ExtensometerQuantity quantity)
{
    switch (quantity) {
    case ExtensometerQuantity::Strain:
        return QCoreApplication::translate("Series", "Engineering strain");
    case ExtensometerQuantity::Elongation:
        return QCoreApplication::translate("Series", "Elongation");
    case ExtensometerQuantity::Length:
        return QCoreApplication::translate("Series", "Gauge length");
    }
    return QString();
}

QString extensometerQuantityUnit(ExtensometerQuantity quantity)
{
    // Strain is a ratio. Saying "dimensionless" beats a blank, which reads as
    // an oversight, and beats inventing a unit -- the same rule the field
    // channels keep.
    return quantity == ExtensometerQuantity::Strain
               ? QCoreApplication::translate("Series", "dimensionless")
               : QCoreApplication::translate("Series", "px");
}

int Series::measuredCount() const
{
    int measured = 0;
    for (const SeriesPoint &point : points) {
        if (point.measured)
            measured++;
    }
    return measured;
}

Series extensometerSeries(const Extensometer &gauge,
                          const QVector<CorrelationResult> &frames,
                          ExtensometerQuantity quantity)
{
    Series series;
    // Named for the gauge as well as the quantity, so two gauges on one chart
    // can be told apart in the legend.
    series.name = gauge.name.isEmpty()
                      ? extensometerQuantityName(quantity)
                      : QCoreApplication::translate("Series", "%1, %2")
                            .arg(gauge.name, extensometerQuantityName(quantity));
    series.quantity = extensometerQuantityName(quantity);
    series.unit = extensometerQuantityUnit(quantity);

    for (int frame = 0; frame < frames.size(); frame++) {
        SeriesPoint point;
        point.frame = frame + 1;

        const ExtensometerReading reading = readExtensometer(gauge, frames.at(frame));
        if (reading.measured) {
            switch (quantity) {
            case ExtensometerQuantity::Strain:
                point.value = reading.strain;
                break;
            case ExtensometerQuantity::Elongation:
                point.value = reading.elongation;
                break;
            case ExtensometerQuantity::Length:
                point.value = reading.length;
                break;
            }
            point.measured = true;
        }

        // Appended whether or not it was measured, so the frames that WERE read
        // stay on the frames they belong to and the gap stays visible.
        series.points.append(point);
    }
    return series;
}

QString fieldSeriesName(FieldChannel channel, FieldAggregate aggregate)
{
    switch (aggregate) {
    // ⚑ The aggregate goes in brackets AFTER the channel's own name, never in
    // front of it. Composed the other way it read "Mean Displacement
    // magnitude", with a capital in the middle of a phrase, because the channel
    // name is a sentence-case name in its own right and cannot also serve as
    // the tail of someone else's. Bracketing sidesteps the case problem
    // entirely instead of performing surgery on the other name's first letter.
    case FieldAggregate::Mean:
        return QCoreApplication::translate("Series", "%1 (mean)")
            .arg(fieldChannelName(channel));
    case FieldAggregate::Largest:
        return QCoreApplication::translate("Series", "%1 (largest)")
            .arg(fieldChannelName(channel));
    case FieldAggregate::SolvedShare:
        // Not about any one channel: it counts points, whatever they measured.
        return QCoreApplication::translate("Series", "Points measured");
    }
    return QString();
}

QString fieldSeriesUnit(FieldChannel channel, FieldAggregate aggregate)
{
    return aggregate == FieldAggregate::SolvedShare
               ? QCoreApplication::translate("Series", "%")
               : fieldChannelUnit(channel);
}

Series fieldSeries(const QVector<CorrelationResult> &frames,
                   FieldChannel channel, FieldAggregate aggregate)
{
    Series series;
    series.name = fieldSeriesName(channel, aggregate);
    series.quantity = series.name;
    series.unit = fieldSeriesUnit(channel, aggregate);

    for (int frame = 0; frame < frames.size(); frame++) {
        const CorrelationResult &field = frames.at(frame);
        SeriesPoint point;
        point.frame = frame + 1;

        if (aggregate == FieldAggregate::SolvedShare) {
            if (field.total() > 0) {
                point.value = 100.0 * double(field.converged) / double(field.total());
                point.measured = true;
            }
            series.points.append(point);
            continue;
        }

        double sum = 0.0;
        double largest = 0.0;
        int counted = 0;
        for (const CorrelationPoint &measured : field.points) {
            // ⚑ Measured points only. A rejected point holds the solver's
            // leftover guess rather than a displacement.
            if (!pointHasChannel(measured, channel))
                continue;
            const double value = channelValue(measured, channel);
            sum += value;
            if (counted == 0 || std::abs(value) > std::abs(largest))
                largest = value;
            counted++;
        }

        if (counted > 0) {
            point.value = aggregate == FieldAggregate::Mean ? sum / counted : largest;
            point.measured = true;
        }
        // ⚑ A frame that measured nothing has NO reading. Plotted as zero it
        // reads as a specimen that stopped moving, which is a measurement.
        series.points.append(point);
    }

    return series;
}

QVector<FieldSeriesChoice> offeredFieldSeries()
{
    // ⚑ Every name comes from fieldSeriesName(), never restated here. Restated,
    // the selector said "Mean displacement magnitude" while the curve's legend
    // said "Mean Displacement magnitude", and the two were only ever read in
    // different places.
    const QVector<QPair<FieldChannel, FieldAggregate>> offered{
        {FieldChannel::DisplacementMagnitude, FieldAggregate::Mean},
        {FieldChannel::DisplacementMagnitude, FieldAggregate::Largest},
        {FieldChannel::StrainXX, FieldAggregate::Mean},
        {FieldChannel::StrainYY, FieldAggregate::Mean},
        {FieldChannel::StrainXY, FieldAggregate::Mean},
        {FieldChannel::DisplacementMagnitude, FieldAggregate::SolvedShare},
    };

    QVector<FieldSeriesChoice> choices;
    for (const auto &entry : offered) {
        choices.append(FieldSeriesChoice{
            fieldSeriesName(entry.first, entry.second), entry.first, entry.second});
    }
    return choices;
}
