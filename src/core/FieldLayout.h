#pragma once

#include <QString>
#include <QVector>

struct CorrelationResult;

// Laying a measured result out over its own grid, ready to be rendered.
//
// Separated from the viewport because it is where a result stops being a list
// and becomes a picture, and getting it wrong is invisible: points placed by
// their position in the list rather than by the cell they belong to still
// produce a smooth, plausible field -- just shifted. That was a real risk the
// moment a region of interest made the list sparse.
//
// A cell with no point, a point the solver rejected, and a point whose strain
// fit declined are ALL not-a-number in the channel concerned: none of them was
// measured, and a rejected point is not a displacement of zero any more than an
// unfitted one is a strain of zero.

// What a field can be drawn as. One entry per quantity the run produces.
enum class FieldChannel
{
    DisplacementMagnitude,
    DisplacementX,
    DisplacementY,
    StrainXX,
    StrainYY,
    StrainXY,
};

// One channel the viewport offers, as its selector shows it.
//
// Single source of truth, in the same sense as offeredSolverChoices(): the
// selector is built from this list and the tests walk it, so a channel cannot
// be offered to a user without something checking that it lays out.
struct FieldChannelInfo
{
    FieldChannel channel;
    QString name;
    QString unit;

    // True where the values come from the strain fit rather than the
    // correlation, which is what decides whether a channel can be shown at all
    // for a given result.
    bool fromStrainFit;

    // True where zero is a physical state rather than an accident of where the
    // reference frame sits, so the colour scale should be centred on it.
    // Strain qualifies and displacement does not: a specimen that merely moved
    // has a large displacement and no strain at all.
    bool centredOnZero;
};

QVector<FieldChannelInfo> offeredFieldChannels();
QString fieldChannelName(FieldChannel channel);
QString fieldChannelUnit(FieldChannel channel);
bool fieldChannelIsStrain(FieldChannel channel);
bool fieldChannelIsCentredOnZero(FieldChannel channel);

// The channel's value in every cell of the grid, not-a-number where nothing
// was measured.
QVector<float> layoutField(const CorrelationResult &result, FieldChannel channel);

// Smallest and largest value the channel actually holds. False when the
// channel measured nothing.
bool fieldValueRange(const CorrelationResult &result, FieldChannel channel,
                     double &lowest, double &highest);

// How many SIGNIFICANT digits a colour scale's tick labels need in order to
// differ from one another over this range, for use with a %g-style format.
//
// Significant digits rather than decimal places, because the two quantities on
// display sit at opposite ends of the number line and a fixed number of
// decimals fails at both: displacement runs in whole pixels, where two decimals
// print "3.00" against all five ticks of a near-uniform field and read as a
// broken instrument, while strain runs at 1e-3 and below, where enough decimals
// to separate the ticks gives "0.000000083" and reads as a wall of zeros. One
// count of significant digits, fed to %g, gets "3.0015" and "8.3e-08" from the
// same rule. Capped at both ends: too few is a rounded summary rather than a
// measurement, too many is float noise dressed as precision.
int fieldScaleSignificantDigits(double lowest, double highest);

// The range a colour scale should span, which is not always the range the data
// occupies: a signed channel is widened to sit symmetrically about zero, so the
// scale's midpoint means "no strain" rather than "the middle of what happened".
bool fieldColourRange(const CorrelationResult &result, FieldChannel channel,
                      double &lowest, double &highest);
