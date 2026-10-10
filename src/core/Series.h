#pragma once

#include "core/FieldLayout.h"

#include <QPointF>
#include <QString>
#include <QVector>

struct CorrelationResult;

// Reading a quantity across the frames of a sequence, and the virtual
// extensometer that is the usual way of doing it.
//
// A DIC test is a loading series, and what an experimentalist wants out of one
// is a CURVE: strain against load step, elongation against frame. Every frame's
// field could already be looked at one at a time; this is what puts them on the
// same axes.
//
// A virtual extensometer is the digital form of the clip gauge it is named
// after: two points on the specimen, and the change in the distance between
// them. It is a derived quantity twice over -- from displacements interpolated
// between grid points that were themselves solved independently -- so most of
// what follows is about refusing to report a number it has no business
// reporting.
//
// ⚑ ENGINE-FREE, like core/ReferenceUpdate.h and core/Recovery.h, and for the
// same reason: it is arithmetic over measured fields, it is where the mistakes
// live, and here it can be exercised exhaustively against fields written by
// hand with known answers.

// The displacement at a place the grid does not have a point.
struct FieldSample
{
    double u = 0.0;
    double v = 0.0;

    // ⚑ False is not "no movement". It means this place has no reading at all,
    // and every caller has to branch on it rather than use the zeros.
    bool measured = false;
};

// The displacement at any position in the reference image, interpolated
// bilinearly from the four grid points around it.
//
// ⚑ ALL FOUR CORNERS MUST HAVE BEEN MEASURED, or there is no reading. An anchor
// sits between grid points, so its value is always borrowed from neighbours,
// and when one of those is a hole every obvious repair is a lie: using the three
// that remain quietly changes what is being averaged, reaching further out
// reports a displacement from somewhere the anchor is not, and filling the hole
// with zero drags the reading toward no movement -- the most plausible-looking
// wrong answer available. So the reading is refused, the curve breaks where a
// reader can see it, and the anchor can be moved.
FieldSample sampleFieldAt(const CorrelationResult &field, double x, double y);

// Two points on the specimen, in reference-image pixels.
struct Extensometer
{
    QString name;
    double ax = 0.0;
    double ay = 0.0;
    double bx = 0.0;
    double by = 0.0;

    // A gauge of no length divides by zero when it computes strain, and an
    // infinity on a chart rescales the axis so every real reading collapses
    // onto the baseline. Two clicks in the same place is an easy thing to do.
    bool isValid() const;

    // Its length in the reference image, which is the length every strain here
    // is measured against.
    double referenceLength() const;
};

// What one gauge read on one frame.
struct ExtensometerReading
{
    double referenceLength = 0.0;
    double length = 0.0;        // in this frame
    double elongation = 0.0;    // length - referenceLength, px
    double strain = 0.0;        // elongation / referenceLength, engineering
    bool measured = false;
};

// ⚑ Measures the change in the DISTANCE BETWEEN the two anchors, never the
// movement of either. A specimen carried bodily across the frame has strained by
// nothing at all, and an implementation that read one anchor's displacement
// would turn a rigid-body motion into a large and entirely plausible strain.
ExtensometerReading readExtensometer(const Extensometer &gauge,
                                     const CorrelationResult &field);

// Which of a gauge's three numbers to plot. They answer different questions:
// elongation in px is what a clip gauge would have shown, strain is what goes
// on the axis of a stress-strain curve, and length is the raw quantity both are
// derived from.
enum class ExtensometerQuantity
{
    Strain,
    Elongation,
    Length,
};

QString extensometerQuantityName(ExtensometerQuantity quantity);
QString extensometerQuantityUnit(ExtensometerQuantity quantity);

// What a series runs along: frames of a sequence, or distance along a line.
enum class SeriesAxis
{
    Frame,
    Distance,
};

// One reading, on one frame.
struct SeriesPoint
{
    // Along a line probe, how far from its start, in reference-image pixels.
    // Meaningless on a series over frames.
    double distance = 0.0;

    // ⚑ ONE-BASED, because that is what the project tree and the run log call
    // the same frame. Left as the zero-based index it comes from, a chart would
    // put the first target at "frame 0" while every other part of the window
    // called it Frame 1, and a reader comparing the two would be off by one
    // with nothing to tell them so.
    int frame = 1;
    double value = 0.0;

    // ⚑ A gap must stay a gap. Dropping an unmeasured frame would join the
    // frames either side into a straight segment through territory nobody
    // measured, and that segment is indistinguishable from data; writing a zero
    // is worse, since on a rising loading curve it reads as the specimen
    // springing back.
    bool measured = false;
};

// A named curve, ready to be drawn.
struct Series
{
    // The full name, for a legend or a caption: "E1, Engineering strain".
    QString name;

    // Just the quantity, for the axis: "Engineering strain". Separate because
    // a rotated axis title has only the chart's height to fit in, and the
    // longer form was clipped mid-word -- which reads as a rendering fault
    // rather than as a long name. The gauge is already named beside the chart.
    QString quantity;

    QString unit;
    SeriesAxis axis = SeriesAxis::Frame;
    QVector<SeriesPoint> points;

    // How many frames actually produced a reading. Reported rather than left to
    // be counted off the chart: a curve with most of its frames missing looks
    // like a short test rather than a failed measurement.
    int measuredCount() const;
    bool isEmpty() const { return points.isEmpty(); }
};

// A line on the specimen along which the map on screen is read, in
// reference-image pixels: the other half of what a loading curve gives, a
// quantity ALONG the specimen at one frame rather than at one place over all
// of them.
struct LineProbe
{
    QString name;
    double ax = 0.0;
    double ay = 0.0;
    double bx = 0.0;
    double by = 0.0;

    // A line of no length has nowhere to read along.
    bool isValid() const;
    double length() const;
};

// Any channel at a place the grid does not have a point, interpolated
// bilinearly from the four grid points around it. ⚑ All four must CARRY the
// channel, which for strain means the fit succeeded there and for the noise
// floor that it was established, not merely that the point converged: the
// same all-or-nothing rule sampleFieldAt() keeps, for the same reasons.
// False, with `value` untouched, where there is no reading; always false for a
// flag, which has two states and nothing between them to interpolate.
bool sampleChannelAt(const CorrelationResult &field, FieldChannel channel,
                     double x, double y, double &value);

// The channel along the probe, on one field. Sampled evenly from end to end,
// both ends included, never further apart than one grid step: the field holds
// one measurement per grid step, so denser sampling would only draw the
// interpolation more finely, and sparser would step over measurements.
//
// ⚑ A HOLE IS A GAP. A sample with no reading is kept, unmeasured, so the
// profile breaks where the field does rather than bridging the hole with a
// straight line nobody measured. `frame` is one-based, for the name only.
Series probeProfile(const LineProbe &probe, const CorrelationResult &field,
                    FieldChannel channel, int frame);

// The series as a table: comment lines naming it and its unit, then a header
// and one row per reading, against frame or against distance along the line as
// the series runs. ⚑ A reading that does not exist is an EMPTY cell, never a
// zero: a curve exported with its gaps closed up loses them the moment it
// leaves this application, and a zero reads as a measurement.
QString seriesCsv(const Series &series);

// Where along the probe those samples are taken, in image pixels, for a grid of
// `step`: one source, so the ticks drawn on the line are the samples plotted.
QVector<QPointF> probeSamplePositions(const LineProbe &probe, int step);

Series extensometerSeries(const Extensometer &gauge,
                          const QVector<CorrelationResult> &frames,
                          ExtensometerQuantity quantity);

// How a whole field is reduced to one number per frame.
enum class FieldAggregate
{
    Mean,
    Largest,
    SolvedShare,   // the share of attempted points that were measured
};

// ⚑ Averaged over the points that were MEASURED, never over the grid. A
// rejected point holds the solver's leftover guess rather than a displacement,
// so averaging it in lets abandoned scratch values into a curve somebody reads
// a material property off.
// What a whole-field curve is called. One function, so the selector's entry and
// the curve's own legend cannot drift apart -- they did, and the difference was
// one capital letter, which is exactly the kind of drift nobody notices until
// the two are read side by side.
QString fieldSeriesName(FieldChannel channel, FieldAggregate aggregate);
QString fieldSeriesUnit(FieldChannel channel, FieldAggregate aggregate);

Series fieldSeries(const QVector<CorrelationResult> &frames,
                   FieldChannel channel, FieldAggregate aggregate);

// One whole-field curve the plot panel offers.
//
// Single source of truth, in the same sense as offeredSolverChoices() and
// offeredFieldChannels(): the selector is built from this list and the tests
// walk it, so a curve cannot be offered without something checking it comes out.
struct FieldSeriesChoice
{
    QString name;          // always fieldSeriesName() of the two below
    FieldChannel channel;
    FieldAggregate aggregate;
};

QVector<FieldSeriesChoice> offeredFieldSeries();
