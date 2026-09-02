// Turning a measured result into the array a renderer draws.
//
// Every failure mode here produces a picture that still looks like a field.
// A point placed by its position in the list rather than by its own cell gives
// a smooth, plausible, WRONG map; a rejected point written as zero gives a
// cold spot that reads as a real measurement of no movement.
//
// The same trap has a second mouth now that strain is here: strain is fitted,
// not measured, and the fit can decline at a point whose displacement was
// perfectly good. Written as zero, that reads as a measurement of no strain --
// which, on a strain map, is the single most consequential lie available.

#include "core/Correlation.h"
#include "core/FieldLayout.h"

#include <QTest>

#include <cmath>

namespace {

CorrelationPoint measured(int gridIndex, float u, float v)
{
    CorrelationPoint p;
    p.gridIndex = gridIndex;
    p.u = u;
    p.v = v;
    p.converged = true;
    return p;
}

CorrelationPoint strained(int gridIndex, float exx, float eyy, float exy)
{
    CorrelationPoint p = measured(gridIndex, 0.f, 0.f);
    p.exx = exx;
    p.eyy = eyy;
    p.exy = exy;
    p.strainFitted = true;
    return p;
}

CorrelationPoint rejected(int gridIndex)
{
    CorrelationPoint p;
    p.gridIndex = gridIndex;
    p.u = 999.f;   // the solver's leftover guess, which is not a measurement
    p.v = 999.f;
    p.converged = false;
    return p;
}

}  // namespace

class TestFieldLayout : public QObject
{
    Q_OBJECT

private slots:
    void a_value_lands_in_the_cell_its_point_recorded();
    void a_sparse_result_leaves_gaps_rather_than_sliding_along();
    void a_rejected_point_is_not_a_displacement_of_zero();
    void a_cell_no_point_reached_is_not_a_displacement_of_zero();
    void the_magnitude_is_the_length_of_the_displacement();
    void an_out_of_range_cell_index_is_ignored_not_written();
    void an_empty_grid_lays_out_to_nothing();

    void every_offered_channel_has_a_name_and_lays_out_over_the_whole_grid();
    void each_strain_component_comes_from_its_own_field();
    void a_strain_the_fit_declined_is_not_a_strain_of_zero();
    void a_signed_channel_keeps_its_sign();
    void a_strain_channel_gets_a_colour_range_centred_on_zero();
    void a_displacement_channel_gets_the_range_it_actually_measured();
    void a_scale_shows_enough_figures_to_tell_its_own_ticks_apart();
    void every_channel_carries_the_sentence_that_stops_it_being_misread();
    void a_reliability_channel_reads_the_opposite_way_from_the_rest();
    void the_recovered_channel_separates_a_repaired_point_from_a_first_solve_one();
    void a_flag_channel_is_not_drawn_or_described_like_a_measurement();
    void the_noise_floor_is_put_against_the_movement_it_qualifies();
    void one_bad_point_does_not_set_the_number_that_speaks_for_the_field();
    void the_reported_spread_of_the_noise_floor_is_the_fields_not_its_worst_points();
};

void TestFieldLayout::a_value_lands_in_the_cell_its_point_recorded()
{
    CorrelationResult result;
    result.gridColumns = 4;
    result.gridRows = 3;
    result.points.append(measured(6, 3.f, 4.f));   // row 1, column 2

    const QVector<float> values = layoutField(result, FieldChannel::DisplacementMagnitude);

    QCOMPARE(values.size(), 12);
    QCOMPARE(values[6], 5.f);
    QVERIFY(std::isnan(values[5]));
    QVERIFY(std::isnan(values[7]));
}

void TestFieldLayout::a_sparse_result_leaves_gaps_rather_than_sliding_along()
{
    // The region-of-interest case. Three points, at cells 0, 5 and 11 -- if
    // they were written in list order they would land at 0, 1 and 2, and the
    // whole field would be drawn shifted into the corner.
    CorrelationResult result;
    result.gridColumns = 4;
    result.gridRows = 3;
    result.restrictedToRoi = true;
    result.points.append(measured(0, 1.f, 0.f));
    result.points.append(measured(5, 2.f, 0.f));
    result.points.append(measured(11, 3.f, 0.f));

    const QVector<float> values = layoutField(result, FieldChannel::DisplacementMagnitude);

    QCOMPARE(values[0], 1.f);
    QCOMPARE(values[5], 2.f);
    QCOMPARE(values[11], 3.f);
    QVERIFY(std::isnan(values[1]));
    QVERIFY(std::isnan(values[2]));
}

void TestFieldLayout::a_rejected_point_is_not_a_displacement_of_zero()
{
    CorrelationResult result;
    result.gridColumns = 2;
    result.gridRows = 1;
    result.points.append(measured(0, 0.f, 1.f));
    result.points.append(rejected(1));

    const QVector<float> values = layoutField(result, FieldChannel::DisplacementMagnitude);

    QCOMPARE(values[0], 1.f);
    QVERIFY(std::isnan(values[1]));
    QVERIFY(values[1] != 0.f);
}

void TestFieldLayout::a_cell_no_point_reached_is_not_a_displacement_of_zero()
{
    CorrelationResult result;
    result.gridColumns = 3;
    result.gridRows = 1;
    result.points.append(measured(1, 0.f, 2.f));

    const QVector<float> values = layoutField(result, FieldChannel::DisplacementMagnitude);

    QVERIFY(std::isnan(values[0]));
    QCOMPARE(values[1], 2.f);
    QVERIFY(std::isnan(values[2]));
}

void TestFieldLayout::the_magnitude_is_the_length_of_the_displacement()
{
    CorrelationResult result;
    result.gridColumns = 2;
    result.gridRows = 1;
    result.points.append(measured(0, -3.f, 4.f));   // sign must not matter
    result.points.append(measured(1, 0.f, -2.5f));

    const QVector<float> values = layoutField(result, FieldChannel::DisplacementMagnitude);

    QCOMPARE(values[0], 5.f);
    QCOMPARE(values[1], 2.5f);
}

void TestFieldLayout::an_out_of_range_cell_index_is_ignored_not_written()
{
    // Defensive, and cheap: a bad index must not write past the array.
    CorrelationResult result;
    result.gridColumns = 2;
    result.gridRows = 1;
    result.points.append(measured(99, 1.f, 1.f));
    result.points.append(measured(-1, 1.f, 1.f));
    result.points.append(measured(0, 0.f, 7.f));

    const QVector<float> values = layoutField(result, FieldChannel::DisplacementMagnitude);

    QCOMPARE(values.size(), 2);
    QCOMPARE(values[0], 7.f);
    QVERIFY(std::isnan(values[1]));
}

void TestFieldLayout::an_empty_grid_lays_out_to_nothing()
{
    QVERIFY(layoutField(CorrelationResult(), FieldChannel::DisplacementMagnitude).isEmpty());
}


void TestFieldLayout::every_offered_channel_has_a_name_and_lays_out_over_the_whole_grid()
{
    // Same rule as the solver list: the viewport builds its selector from this,
    // so every entry reaches a user. A channel that lays out to the wrong
    // length would be drawn over the image at the wrong scale.
    CorrelationResult result;
    result.gridColumns = 4;
    result.gridRows = 3;
    result.points.append(strained(6, 0.01f, -0.02f, 0.003f));

    const QVector<FieldChannelInfo> channels = offeredFieldChannels();
    QVERIFY(channels.size() >= 6);

    QStringList names;
    for (const FieldChannelInfo &channel : channels) {
        const QString name = fieldChannelName(channel.channel);
        QVERIFY2(!name.isEmpty(), "a field channel the viewport offers has no name");
        QCOMPARE(channel.name, name);
        QVERIFY2(!names.contains(name), "two field channels share a name");
        names << name;

        const QVector<float> values = layoutField(result, channel.channel);
        QVERIFY2(values.size() == 12,
                 qPrintable(QStringLiteral("channel %1 laid out %2 cells, not 12")
                                .arg(name)
                                .arg(values.size())));
    }
}

void TestFieldLayout::each_strain_component_comes_from_its_own_field()
{
    // Three components with three distinct values, so a copy-paste between
    // them cannot pass. This is the exact shape of a bug already found once in
    // the engine (FeatureAffine3D's wy/wz).
    CorrelationResult result;
    result.gridColumns = 1;
    result.gridRows = 1;
    result.points.append(strained(0, 0.011f, 0.022f, 0.033f));

    QCOMPARE(layoutField(result, FieldChannel::StrainXX)[0], 0.011f);
    QCOMPARE(layoutField(result, FieldChannel::StrainYY)[0], 0.022f);
    QCOMPARE(layoutField(result, FieldChannel::StrainXY)[0], 0.033f);
}

void TestFieldLayout::a_strain_the_fit_declined_is_not_a_strain_of_zero()
{
    // A point whose displacement solved perfectly, but which had too few good
    // neighbours for the gradient fit. Its strain members hold whatever the
    // engine left there, and none of it is a measurement.
    CorrelationResult result;
    result.gridColumns = 2;
    result.gridRows = 1;
    result.points.append(strained(0, 0.05f, 0.f, 0.f));

    CorrelationPoint unfitted = measured(1, 1.f, 1.f);   // displacement is fine
    unfitted.strainFitted = false;
    result.points.append(unfitted);

    const QVector<float> values = layoutField(result, FieldChannel::StrainXX);

    QCOMPARE(values[0], 0.05f);
    QVERIFY(std::isnan(values[1]));
    QVERIFY(values[1] != 0.f);

    // ... while its displacement is still shown, because that part WAS measured.
    QCOMPARE(layoutField(result, FieldChannel::DisplacementX)[1], 1.f);
}

void TestFieldLayout::a_signed_channel_keeps_its_sign()
{
    // Magnitude is a length and cannot be negative; the components and the
    // strains can, and losing the sign turns compression into tension.
    CorrelationResult result;
    result.gridColumns = 2;
    result.gridRows = 1;
    result.points.append(strained(0, -0.004f, 0.f, 0.f));
    CorrelationPoint left = measured(1, -3.f, -4.f);
    result.points.append(left);

    QCOMPARE(layoutField(result, FieldChannel::StrainXX)[0], -0.004f);
    QCOMPARE(layoutField(result, FieldChannel::DisplacementX)[1], -3.f);
    QCOMPARE(layoutField(result, FieldChannel::DisplacementY)[1], -4.f);
    QCOMPARE(layoutField(result, FieldChannel::DisplacementMagnitude)[1], 5.f);
}

void TestFieldLayout::a_strain_channel_gets_a_colour_range_centred_on_zero()
{
    // Tension and compression are opposite physical states, not two ends of
    // one continuum. A scale whose midpoint drifts off zero colours a
    // slightly-compressed region the same as a strongly-stretched one.
    CorrelationResult result;
    result.gridColumns = 3;
    result.gridRows = 1;
    result.points.append(strained(0, -0.001f, 0.f, 0.f));
    result.points.append(strained(1, 0.004f, 0.f, 0.f));
    result.points.append(strained(2, 0.002f, 0.f, 0.f));

    double lowest = 0.0;
    double highest = 0.0;
    QVERIFY(fieldValueRange(result, FieldChannel::StrainXX, lowest, highest));
    QCOMPARE(float(lowest), -0.001f);
    QCOMPARE(float(highest), 0.004f);

    QVERIFY(fieldColourRange(result, FieldChannel::StrainXX, lowest, highest));
    QCOMPARE(float(lowest), -0.004f);
    QCOMPARE(float(highest), 0.004f);
}

void TestFieldLayout::a_displacement_channel_gets_the_range_it_actually_measured()
{
    // Displacement is measured from wherever the reference frame sits, so a
    // specimen that merely translated reads +3 px everywhere and never touches
    // zero. Centring the scale there would spend half the colours on values
    // that cannot occur and flatten the variation that IS the measurement.
    // That is the opposite of the strain case above, and the reason the rule
    // is per channel rather than per sign.
    CorrelationResult result;
    result.gridColumns = 2;
    result.gridRows = 1;
    result.points.append(measured(0, 3.f, 4.f));
    result.points.append(measured(1, 6.f, 8.f));

    double lowest = 0.0;
    double highest = 0.0;
    QVERIFY(fieldColourRange(result, FieldChannel::DisplacementMagnitude,
                             lowest, highest));
    QCOMPARE(float(lowest), 5.f);
    QCOMPARE(float(highest), 10.f);

    QVERIFY(fieldColourRange(result, FieldChannel::DisplacementX, lowest, highest));
    QCOMPARE(float(lowest), 3.f);
    QCOMPARE(float(highest), 6.f);

    // Nothing measured, nothing to scale.
    QVERIFY(!fieldColourRange(CorrelationResult(),
                              FieldChannel::DisplacementMagnitude,
                              lowest, highest));
}


void TestFieldLayout::a_scale_shows_enough_figures_to_tell_its_own_ticks_apart()
{
    // FOUND ON SCREEN, 2026-08-19, twice over. A rigid translation makes every
    // point read 3 px, so the colour scale printed "3.00" against all five of
    // its ticks: the field was right and the scale said nothing, which reads as
    // a broken instrument. Fixing that with more DECIMALS then produced
    // "0.000000083" on the strain channel, where every value is small -- right,
    // and unreadable. Significant digits answer both.
    //
    // The counts below are what a %g format needs to keep five ticks distinct.

    // An ordinary displacement scale: ticks 0.0015 apart against a reach of 3,
    // so four digits reach the tick and a fifth separates them.
    QCOMPARE(fieldScaleSignificantDigits(2.997, 3.003), 5);

    // A strain scale, where every value is small but the ticks are a good
    // fraction of the reach: the floor is enough, and asking for more would
    // print trailing noise.
    QCOMPARE(fieldScaleSignificantDigits(-8.3e-8, 8.3e-8), 3);
    QCOMPARE(fieldScaleSignificantDigits(-0.004, 0.004), 3);

    // Ticks a millionth of the reach apart need the digits that separate them.
    QVERIFY(fieldScaleSignificantDigits(3.0, 3.000002) >= 7);

    // Capped, in both directions.
    QVERIFY(fieldScaleSignificantDigits(3.0, 3.0 + 1e-30) <= 9);
    QVERIFY(fieldScaleSignificantDigits(0.0, 1e30) >= 3);

    // A scale of no width at all must not divide by it, or take log of it.
    QCOMPARE(fieldScaleSignificantDigits(4.0, 4.0), 3);
    QCOMPARE(fieldScaleSignificantDigits(4.0, 3.0), 3);   // reversed, not a crash
    QCOMPARE(fieldScaleSignificantDigits(0.0, 0.0), 3);
}


void TestFieldLayout::every_channel_carries_the_sentence_that_stops_it_being_misread()
{
    // A field channel is a number on a colour scale, and a number on a colour
    // scale is trusted. The note is where each one says what it is NOT, so it
    // travels with the channel rather than living in documentation nobody has
    // open. Required for all of them, so a channel cannot be added without one.
    for (const FieldChannelInfo &channel : offeredFieldChannels()) {
        const QString note = fieldChannelNote(channel.channel);
        QVERIFY2(!note.isEmpty(),
                 qPrintable(QStringLiteral("channel %1 says nothing about itself")
                                .arg(channel.name)));
        QCOMPARE(channel.note, note);
    }

    // The two that matter most, because both are read as more than they are.
    const QString floor = fieldChannelNote(FieldChannel::NoiseFloor);
    QVERIFY2(floor.contains(QStringLiteral("lower bound"), Qt::CaseInsensitive),
             "the noise floor does not say it is a bound rather than an error bar");
    QVERIFY2(floor.contains(QStringLiteral("target"), Qt::CaseInsensitive),
             "the noise floor does not say it never examines the target image");

    const QString conditioning = fieldChannelNote(FieldChannel::MatchConditioning);
    QVERIFY2(conditioning.contains(QStringLiteral("no absolute"), Qt::CaseInsensitive),
             "match conditioning does not say its scale is relative");
}

void TestFieldLayout::the_recovered_channel_separates_a_repaired_point_from_a_first_solve_one()
{
    // Provenance as a map. The run report says HOW MANY points the second pass
    // repaired; this says WHERE they are, which is the question a reader
    // actually has -- recovered points clustered in one corner mean something
    // quite different from recovered points scattered evenly.
    CorrelationResult result;
    result.gridColumns = 3;
    result.gridRows = 1;
    result.recoveryRequested = true;

    CorrelationPoint first = measured(0, 1.f, 0.f);
    CorrelationPoint repaired = measured(1, 1.f, 0.f);
    repaired.recovered = true;

    result.points << first << repaired << rejected(2);

    const QVector<float> values =
        layoutField(result, FieldChannel::RecoveredOnSecondPass);
    QCOMPARE(values.size(), 3);
    QCOMPARE(values[0], 0.f);
    QCOMPARE(values[1], 1.f);

    // ⚑ A point that was never measured is not "not recovered", it is nothing.
    // Drawing it as a zero would colour it identically to a point the first
    // solve got right, so the holes in the field would fill in with the most
    // reassuring reading available.
    QVERIFY2(std::isnan(values[2]),
             "a point that was never measured was drawn as a first-solve point");
}

void TestFieldLayout::a_flag_channel_is_not_drawn_or_described_like_a_measurement()
{
    // ⚑ It takes two values and no others, so every rule written for a
    // continuous quantity is wrong for it. Five evenly spaced tick labels on a
    // 0-to-1 scale read 0, 0.25, 0.5, 0.75, 1, and three of those cannot occur
    // -- the same flaw already recorded as the reason a displacement scale is
    // not centred on zero, one channel further along. The screen asks this
    // before it decides how to label the bar.
    QVERIFY(fieldChannelIsFlag(FieldChannel::RecoveredOnSecondPass));

    // And nothing else is one, or the bar would start labelling real
    // measurements with two ticks.
    for (const FieldChannelInfo &channel : offeredFieldChannels()) {
        if (channel.channel == FieldChannel::RecoveredOnSecondPass)
            continue;
        QVERIFY2(!fieldChannelIsFlag(channel.channel),
                 qPrintable(QStringLiteral("%1 claims to be a flag")
                                .arg(channel.name)));
    }

    // It is provenance, not a measurement, so it is none of the other kinds
    // either: not strain, not reliability, and not centred on zero -- zero here
    // is one of two states, not a physical middle.
    QVERIFY(!fieldChannelIsStrain(FieldChannel::RecoveredOnSecondPass));
    QVERIFY(!fieldChannelIsReliability(FieldChannel::RecoveredOnSecondPass));
    QVERIFY(!fieldChannelIsCentredOnZero(FieldChannel::RecoveredOnSecondPass));
}

void TestFieldLayout::a_reliability_channel_reads_the_opposite_way_from_the_rest()
{
    // Every other channel is a measurement, where a large value is simply a
    // large value. In these two a large value is a WARNING, and a reader who
    // carries the usual habit across will read a bad region as an interesting
    // one. The distinction is carried in the data, so the screen can say it.
    QVERIFY(fieldChannelIsReliability(FieldChannel::NoiseFloor));
    QVERIFY(fieldChannelIsReliability(FieldChannel::MatchConditioning));
    QVERIFY(!fieldChannelIsReliability(FieldChannel::DisplacementMagnitude));
    QVERIFY(!fieldChannelIsReliability(FieldChannel::StrainXX));

    // Neither is centred on zero: zero is unreachable for both, not a
    // meaningful midpoint, so a diverging scale would spend half its colours on
    // values that cannot occur.
    QVERIFY(!fieldChannelIsCentredOnZero(FieldChannel::NoiseFloor));
    QVERIFY(!fieldChannelIsCentredOnZero(FieldChannel::MatchConditioning));

    // The noise floor is a displacement and carries the same unit as one; the
    // conditioning is a reciprocal slope with arbitrary factors and carries none.
    QCOMPARE(fieldChannelUnit(FieldChannel::NoiseFloor), QStringLiteral("px"));
    QCOMPARE(fieldChannelUnit(FieldChannel::MatchConditioning),
             QStringLiteral("dimensionless"));
}


void TestFieldLayout::the_noise_floor_is_put_against_the_movement_it_qualifies()
{
    // "0.004 px" is unreadable on its own: whether that is excellent or useless
    // depends entirely on how much movement there was. The ratio is the sentence
    // a scientist actually needs, and it comes from this run's own numbers, so
    // it invents no threshold for what counts as good.
    CorrelationResult result;
    result.gridColumns = 2;
    result.gridRows = 1;

    CorrelationPoint a = measured(0, 3.f, 0.f);      // 3 px of movement
    a.zncc = 0.99f;
    a.noiseFloor = 0.004f;
    a.noiseFloorMeasured = true;
    CorrelationPoint b = measured(1, 4.f, 0.f);      // 4 px, the largest
    b.zncc = 0.99f;
    b.noiseFloor = 0.002f;
    b.noiseFloorMeasured = true;
    result.points << a << b;

    // Two points, so the floor 95 per cent of them beat is the poorer of the
    // two: 0.004 against the largest displacement 4, one part in 1000.
    const QString stated = noiseFloorAgainstMovement(result);
    QVERIFY2(!stated.isEmpty(), "the noise floor was left without context");
    QVERIFY2(stated.contains(QStringLiteral("1000")),
             qPrintable(QStringLiteral("expected a ratio of 1000, got: %1").arg(stated)));

    // Nothing to compare against, and nothing claimed. A specimen that did not
    // move has no ratio, and inventing one would divide by its own noise.
    CorrelationResult still;
    still.gridColumns = 1;
    still.gridRows = 1;
    CorrelationPoint c = measured(0, 0.f, 0.f);
    c.zncc = 0.99f;
    c.noiseFloor = 0.004f;
    c.noiseFloorMeasured = true;
    still.points << c;
    QVERIFY2(noiseFloorAgainstMovement(still).isEmpty(),
             "a ratio was claimed against no movement at all");

    // And a run with no reliability at all says nothing rather than dividing by
    // a floor it never measured.
    QVERIFY(noiseFloorAgainstMovement(CorrelationResult()).isEmpty());
}

void TestFieldLayout::one_bad_point_does_not_set_the_number_that_speaks_for_the_field()
{
    // ⚑ Found on real data, which is the only place it could be found. Our
    // synthetic patterns fill the frame; a photograph of a specimen does not,
    // and the grid covers the dark background around it as well. A subset out
    // there has almost no gradient energy, so its noise floor is enormous --
    // and it CORRELATES beautifully, because flat matches flat. On the pyALDIC
    // tension sequence only 8 of 5401 solved points fell below 0.9, so no
    // correlation threshold would have excluded these; the run reported "at
    // worst one part in 3" about a measurement that was excellent.
    //
    // A headline number describes the field. One point cannot be the field.
    CorrelationResult result;
    result.gridColumns = 21;
    result.gridRows = 1;

    for (int i = 0; i < 20; i++) {
        CorrelationPoint good = measured(i, 4.f, 0.f);
        good.zncc = 0.99f;
        good.noiseFloor = 0.002f;
        good.noiseFloorMeasured = true;
        result.points << good;
    }

    CorrelationPoint background = measured(20, 4.f, 0.f);
    background.zncc = 0.99f;          // a flat region correlates perfectly
    background.noiseFloor = 2.0f;     // and resolves nothing at all
    background.noiseFloorMeasured = true;
    result.points << background;

    const QString stated = noiseFloorAgainstMovement(result);
    QVERIFY2(!stated.isEmpty(), "the noise floor was left without context");

    // 4 px of movement against the floor 95 per cent of points beat: 0.002 px,
    // one part in 2000.
    QVERIFY2(stated.contains(QStringLiteral("2000")),
             qPrintable(QStringLiteral("one outlier still speaks for the whole "
                                       "field: %1").arg(stated)));

    // And it says whose floor it is quoting, rather than implying every point.
    QVERIFY2(stated.contains(QStringLiteral("95")), qPrintable(stated));

    // NEGATIVE CHECK (2026-09-01): three breaks, all three red. Taking the
    // worst floor again reddens this case; taking the median instead of the
    // 95th percentile reddens the case above, which pins the percentile from
    // the other side; dropping "95 per cent" from the sentence reddens this
    // one, because a number that speaks for most of the field has to say so.
}

void TestFieldLayout::the_reported_spread_of_the_noise_floor_is_the_fields_not_its_worst_points()
{
    // The run report gave the noise floor as a plain minimum to maximum, and
    // then, on the very next line, put it against the movement using a 95th
    // percentile. One line robust and the next not, about the same quantity:
    // on a real photograph the maximum is a subset out on the unspeckled
    // background, and it made an excellent field read as "0.0052 to 2.31 px".
    CorrelationResult result;
    result.gridColumns = 21;
    result.gridRows = 1;

    for (int i = 0; i < 20; i++) {
        CorrelationPoint good = measured(i, 4.f, 0.f);
        good.zncc = 0.99f;
        good.noiseFloor = 0.002f + 0.0001f * float(i);
        good.noiseFloorMeasured = true;
        result.points << good;
    }
    CorrelationPoint background = measured(20, 4.f, 0.f);
    background.zncc = 0.99f;
    background.noiseFloor = 2.31f;
    background.noiseFloorMeasured = true;
    result.points << background;

    double lowest = 0.0;
    double typical = 0.0;
    QVERIFY(noiseFloorSpread(result, lowest, typical));

    QVERIFY2(qAbs(lowest - 0.002) < 1e-6,
             qPrintable(QStringLiteral("lowest was %1").arg(lowest)));
    // The 95th percentile of 21 values is the 20th by nearest rank, which here
    // is the poorest of the twenty good points: 0.0039, not the background
    // point's 2.31 and not the median's 0.003. Pinned from both sides, because
    // "less than the outlier" passes for any percentile at all -- a negative
    // check with the median substituted stayed green until this said which.
    QVERIFY2(qAbs(typical - 0.0039) < 1e-6,
             qPrintable(QStringLiteral("expected the 95th percentile 0.0039, "
                                       "got %1").arg(typical)));

    // And nothing is claimed for a run that measured no reliability at all.
    double a = 0.0, b = 0.0;
    QVERIFY(!noiseFloorSpread(CorrelationResult(), a, b));
}

QTEST_MAIN(TestFieldLayout)
#include "test_field_layout.moc"
