// A measurement set against the answer the example states about itself.
//
// WHAT MAKES THIS WORTH TESTING. Every failure mode here produces a picture
// that still looks like a comparison: an answer read for the wrong frame gives
// a smooth, plausible error map of an experiment nobody ran; a stated strain
// evaluated in the other strain measure shows the linear form's own well-known
// error as a fault in the instrument; and an error of zero written where the
// solver measured nothing paints the most reassuring reading available over
// exactly the points a reader is looking for.
//
// The stated answers read here are the ones that SHIP, in examples/synthetic,
// so what is under test is what a user gets.
//
// NEGATIVE CHECK (2026-09-08): seven breaks, each reverted after, recorded at
// the assertion it exercises. All seven landed on the case named for them:
// matching an answer by set rather than by file name; evaluating a stated strain
// always in Green-Lagrange; laying the stated field only where the run
// succeeded; differencing regardless of convergence; reporting a run that
// measured nothing as accurate; ranging each panel on its own colours; and
// ranging the error map over the errors themselves.
//
// ⚑ WHAT THESE CASES DO NOT CATCH, said plainly because it looks like coverage.
// They are arithmetic on a stated deformation and a supplied result: nothing
// here runs the engine, so none of them can tell whether the field being
// compared is the field on screen. That is checked in the walkthrough suite,
// by an_example_that_states_its_own_answer_is_measured_against_it_on_screen,
// which measures a real example through the real window and asks the screen
// itself what error it found.

// ⚑ SIX SURVIVORS FROM THE SWEEP OF 2026-09-09 ARE CLOSED BY ARGUMENT, and are
// written down here so the next sweep does not re-derive them:
//
//   - the frame loop's `index < frames.size()` widened to `<=`. QJsonArray::at()
//     past the end returns an undefined value, whose object is empty, whose
//     "file" is an empty string, which matches no image file name -- and if it
//     ever did, the half-an-answer rule below refuses it for stating neither a
//     gradient nor a shift. Same answer by a longer road.
//   - the cell-bounds tests in layoutStatedField and layoutErrorField, four
//     mutants across the two. Widening `>=` to `>`, or joining the two halves
//     with AND, lets an out-of-range grid index reach `cells[index]`, which is
//     a WRITE past the end of a QVector. That is undefined behaviour, not an
//     assertion, and after the chunking family on 2026-09-10 -- a mutant killed
//     by an allocator in one build and green in another -- a case resting on
//     what that write happens to do would be evidence of nothing. The guard
//     stays because a layout is handed a result it did not build.
//   - `magnitude > report.worstAbsolute` widened to `>=`. It changes which
//     point is reported only when two points are EXACTLY equally wrong, and
//     then both are correct answers to "where is the worst". Equivalent in
//     meaning rather than merely unobserved.

#include "core/Correlation.h"
#include "core/FieldLayout.h"
#include "core/KnownAnswer.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <functional>
#include <limits>

namespace {

// What a cell nothing was measured in holds, spelled out so the cases below
// read as what they are about rather than as arithmetic.
const float kNothingMeasured = std::numeric_limits<float>::quiet_NaN();

QString synthetic(const QString &name)
{
    return QStringLiteral(SURVIEW_EXAMPLES "/synthetic/") + name;
}

// The file's own statement about one frame, read here independently of the code
// under test. Every expectation below comes from this rather than from a number
// written into the test: a copy of the answer here would be a second source of
// truth, and the two would drift.
QJsonObject statedFrame(const QString &set, int frame)
{
    QFile file(QStringLiteral(SURVIEW_EXAMPLES "/synthetic/ground_truth.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    return root[QStringLiteral("sets")].toObject()[set].toObject()
        [QStringLiteral("frames")].toArray().at(frame).toObject();
}

// A grid of points over the picture, every one measured, at whatever
// displacement `deform` says. Positions are real image pixels, because the
// stated answer is a function of position and a test on a grid of zeros would
// not notice a field evaluated at the wrong place.
CorrelationResult gridOf(int columns, int rows, int step, int origin,
                         const std::function<void(CorrelationPoint &)> &deform)
{
    CorrelationResult result;
    result.gridColumns = columns;
    result.gridRows = rows;
    result.step = step;
    result.originX = float(origin);
    result.originY = float(origin);
    for (int row = 0; row < rows; row++) {
        for (int column = 0; column < columns; column++) {
            CorrelationPoint point;
            point.gridIndex = row * columns + column;
            point.x = float(origin + column * step);
            point.y = float(origin + row * step);
            point.converged = true;
            deform(point);
            result.points.append(point);
            result.converged++;
        }
    }
    return result;
}

}  // namespace

class TestKnownAnswer : public QObject
{
    Q_OBJECT

private slots:
    void the_examples_that_ship_state_their_own_answer_beside_their_images();
    void an_image_with_no_stated_answer_beside_it_says_so_rather_than_inventing_one();
    void an_image_the_answer_file_does_not_name_is_not_given_another_frames_answer();
    void the_stated_displacement_is_the_one_the_file_states_at_the_corners();
    void the_answer_carries_the_files_own_account_of_why_it_is_exact();

    void a_rigid_rotation_states_no_strain_in_the_measure_that_has_none();
    void the_linear_measure_states_the_error_it_is_known_to_have_on_a_rotation();
    void a_stated_strain_is_uniform_over_the_picture_as_the_deformation_is();
    void the_cauchy_shear_adds_the_two_off_diagonal_terms_rather_than_differencing_them();
    void the_two_measures_state_different_shears_for_a_deformation_that_has_both();

    void a_channel_the_stated_answer_cannot_speak_to_is_not_offered();

    void the_stated_field_covers_every_point_the_run_attempted_including_its_failures();
    void a_frame_that_states_half_an_answer_states_none_of_it();
    void an_answer_that_is_not_valid_states_nothing_anywhere();
    void a_run_that_measured_nothing_is_still_scaled_against_what_it_missed();
    void a_cell_no_point_reached_has_no_stated_value_either();
    void a_point_the_solver_rejected_is_not_an_error_of_zero();
    void a_strain_the_fit_declined_is_not_an_error_of_zero();
    void an_exact_measurement_comes_out_as_no_error_at_all();

    void the_error_report_counts_only_the_points_that_were_measured();
    void the_report_says_where_the_worst_point_is_not_only_how_bad_it_is();
    void a_run_with_nothing_measured_reports_no_accuracy_rather_than_a_perfect_one();

    void the_measured_and_stated_panels_are_drawn_on_one_shared_scale();
    void a_shared_scale_for_a_signed_quantity_still_sits_about_zero();
    void the_error_scale_is_centred_on_zero_whatever_the_errors_are();
};

void TestKnownAnswer::the_examples_that_ship_state_their_own_answer_beside_their_images()
{
    // If this fails, every case below is vacuous.
    const KnownAnswer answer = knownAnswerFor(synthetic(QStringLiteral("rotation_02.tif")));
    QVERIFY2(answer.valid, qPrintable(QStringLiteral("no stated answer found at %1")
                                          .arg(statedAnswerPathFor(
                                              synthetic(QStringLiteral("rotation_02.tif"))))));
    QCOMPARE(answer.set, QStringLiteral("rotation"));
    QCOMPARE(answer.frame, 2);
    QCOMPARE(answer.frameFile, QStringLiteral("rotation_02.tif"));
    QVERIFY(!answer.whatItShows.isEmpty());
}

void TestKnownAnswer::an_image_with_no_stated_answer_beside_it_says_so_rather_than_inventing_one()
{
    // The real examples are photographs of real specimens. Nothing states what
    // they did, and a comparison offered against a default-constructed identity
    // would report the specimen's whole movement as the instrument's error.
    const KnownAnswer answer =
        knownAnswerFor(QStringLiteral(SURVIEW_EXAMPLES "/real/does_not_exist_00.tif"));
    QVERIFY(!answer.valid);
}

void TestKnownAnswer::an_image_the_answer_file_does_not_name_is_not_given_another_frames_answer()
{
    // ⚑ The failure this rules out is the quiet one. Matched by position rather
    // than by name, a frame the file does not carry would be handed its
    // neighbour's deformation and produce an error map that looks like an
    // experiment.
    //
    // NEGATIVE CHECK: matching on the set alone, ignoring the file name, hands
    // this image frame 0's answer and the case goes green on a lie.
    const KnownAnswer answer =
        knownAnswerFromFile(QStringLiteral(SURVIEW_EXAMPLES "/synthetic/ground_truth.json"),
                            QStringLiteral("rotation_99.tif"));
    QVERIFY(!answer.valid);
}

void TestKnownAnswer::the_stated_displacement_is_the_one_the_file_states_at_the_corners()
{
    // ⚑ Checked against the file's OWN displacement_at block, not against the
    // deformation gradient this code reads. The gradient and the corner
    // displacements are two independent statements of the same answer, so
    // agreeing with the second is evidence about the arithmetic applied to the
    // first, where re-deriving it here would only be evidence that the test can
    // multiply.
    const QJsonObject stated = statedFrame(QStringLiteral("rotation"), 2);
    const QJsonObject at = stated[QStringLiteral("displacement_at")].toObject();
    QVERIFY(!at.isEmpty());

    const KnownAnswer answer = knownAnswerFor(synthetic(QStringLiteral("rotation_02.tif")));
    QVERIFY(answer.valid);

    struct Corner { const char *name; double x, y; };
    // The file states the image as 640 x 480, and its corners are the extreme
    // pixel centres of that.
    const Corner corners[] = {{"top_left", 0.0, 0.0},
                              {"centre", 320.0, 240.0},
                              {"bottom_right", 640.0, 480.0}};
    for (const Corner &corner : corners) {
        const QJsonArray expected = at[QString::fromLatin1(corner.name)].toArray();
        QVERIFY2(expected.size() == 2, corner.name);

        const double u = statedValue(answer, FieldChannel::DisplacementX,
                                     corner.x, corner.y, StrainMeasure::Cauchy);
        const double v = statedValue(answer, FieldChannel::DisplacementY,
                                     corner.x, corner.y, StrainMeasure::Cauchy);
        QVERIFY2(std::abs(u - expected.at(0).toDouble()) < 1e-9,
                 qPrintable(QStringLiteral("%1: u %2 against a stated %3")
                                .arg(QString::fromLatin1(corner.name))
                                .arg(u).arg(expected.at(0).toDouble())));
        QVERIFY2(std::abs(v - expected.at(1).toDouble()) < 1e-9,
                 qPrintable(QStringLiteral("%1: v %2 against a stated %3")
                                .arg(QString::fromLatin1(corner.name))
                                .arg(v).arg(expected.at(1).toDouble())));

        // And the magnitude is the length of that displacement, not either
        // component of it.
        const double magnitude = statedValue(answer, FieldChannel::DisplacementMagnitude,
                                             corner.x, corner.y, StrainMeasure::Cauchy);
        QVERIFY(std::abs(magnitude - std::hypot(u, v)) < 1e-9);
    }
}

void TestKnownAnswer::the_answer_carries_the_files_own_account_of_why_it_is_exact()
{
    // The claim that the answer is exact rather than merely good is the whole
    // reason a difference map here is an ERROR map. It is the data's claim, and
    // it travels with the answer so the screen can quote it rather than assert
    // it in our own words.
    const KnownAnswer answer = knownAnswerFor(synthetic(QStringLiteral("tension_03.tif")));
    QVERIFY(answer.valid);
    QVERIFY(!answer.howTheAnswerIsExact.isEmpty());
    QVERIFY(answer.howTheAnswerIsExact.contains(QStringLiteral("resampled")));
    QVERIFY(!answer.source.isEmpty());
    QVERIFY(!answer.amount.isEmpty());
}

void TestKnownAnswer::a_rigid_rotation_states_no_strain_in_the_measure_that_has_none()
{
    // A specimen that was turned and not deformed. In the Green-Lagrange form
    // that is exactly zero strain, everywhere, and a stated answer that said
    // otherwise would accuse a correct measurement of an error.
    const KnownAnswer answer = knownAnswerFor(synthetic(QStringLiteral("rotation_02.tif")));
    QVERIFY(answer.valid);

    for (FieldChannel channel : {FieldChannel::StrainXX, FieldChannel::StrainYY,
                                 FieldChannel::StrainXY}) {
        const double stated = statedValue(answer, channel, 100.0, 300.0,
                                          StrainMeasure::GreenLagrange);
        QVERIFY2(std::abs(stated) < 1e-9,
                 qPrintable(QStringLiteral("%1 stated as %2 on a specimen that was "
                                           "only turned")
                                .arg(fieldChannelName(channel)).arg(stated)));
    }
}

void TestKnownAnswer::the_linear_measure_states_the_error_it_is_known_to_have_on_a_rotation()
{
    // ⚑ THE CASE THAT DECIDES WHETHER THIS SCREEN IS HONEST. The two strain
    // measures genuinely disagree about the same deformation: three degrees of
    // rotation is zero Green-Lagrange strain and cos(3 deg) - 1 in the linear
    // form, which is the linear form's own well-known error rather than a fault
    // in the measurement. The stated answer is therefore evaluated in the
    // measure the RUN used, so that disagreement appears as agreement between
    // measured and stated -- which is what it is.
    //
    // NEGATIVE CHECK: evaluating the stated strain always in Green-Lagrange
    // leaves this case red at 1.4e-3, and turns a correct Cauchy run on the
    // rotation set into a field of visible error.
    const KnownAnswer answer = knownAnswerFor(synthetic(QStringLiteral("rotation_02.tif")));
    QVERIFY(answer.valid);
    QCOMPARE(statedFrame(QStringLiteral("rotation"), 2)[QStringLiteral("amount")].toDouble(),
             3.0);

    const double expected = std::cos(3.0 * M_PI / 180.0) - 1.0;
    const double stated = statedValue(answer, FieldChannel::StrainXX, 100.0, 300.0,
                                      StrainMeasure::Cauchy);
    QVERIFY2(std::abs(stated - expected) < 1e-9,
             qPrintable(QStringLiteral("the linear measure states %1 where it is "
                                       "known to state %2").arg(stated).arg(expected)));
}

void TestKnownAnswer::a_stated_strain_is_uniform_over_the_picture_as_the_deformation_is()
{
    // The deformation gradient is one matrix for the whole frame, so a stated
    // strain that varied with position would mean the evaluation had picked up
    // the position somewhere it should not have.
    const KnownAnswer answer = knownAnswerFor(synthetic(QStringLiteral("tension_03.tif")));
    QVERIFY(answer.valid);

    const double corner = statedValue(answer, FieldChannel::StrainXX, 5.0, 5.0,
                                      StrainMeasure::Cauchy);
    const double far = statedValue(answer, FieldChannel::StrainXX, 600.0, 400.0,
                                   StrainMeasure::Cauchy);
    QVERIFY(corner > 0.0);
    QCOMPARE(corner, far);
}

void TestKnownAnswer::a_channel_the_stated_answer_cannot_speak_to_is_not_offered()
{
    // ⚑ The stated answer is a deformation. It says everything about what moved
    // and nothing whatever about how well a subset could be measured, so
    // setting a noise floor against it would be comparing two different kinds
    // of thing and calling the difference an error.
    QVERIFY(knownAnswerCoversChannel(FieldChannel::DisplacementMagnitude));
    QVERIFY(knownAnswerCoversChannel(FieldChannel::DisplacementX));
    QVERIFY(knownAnswerCoversChannel(FieldChannel::DisplacementY));
    QVERIFY(knownAnswerCoversChannel(FieldChannel::StrainXX));
    QVERIFY(knownAnswerCoversChannel(FieldChannel::StrainYY));
    QVERIFY(knownAnswerCoversChannel(FieldChannel::StrainXY));

    QVERIFY(!knownAnswerCoversChannel(FieldChannel::NoiseFloor));
    QVERIFY(!knownAnswerCoversChannel(FieldChannel::MatchConditioning));
    QVERIFY(!knownAnswerCoversChannel(FieldChannel::RecoveredOnSecondPass));

    // And every channel the viewport offers is decided one way or the other, so
    // a channel added later cannot arrive here undecided.
    for (const FieldChannelInfo &info : offeredFieldChannels()) {
        const bool covered = knownAnswerCoversChannel(info.channel);
        QVERIFY2(covered == (!fieldChannelIsReliability(info.channel)
                             && !fieldChannelIsFlag(info.channel)),
                 qPrintable(info.name));
    }
}

void TestKnownAnswer::the_stated_field_covers_every_point_the_run_attempted_including_its_failures()
{
    // ⚑ The stated answer is known wherever the instrument was pointed, whether
    // or not the instrument read anything there. Holding the stated panel to
    // the shape of the measured one would hide the very thing a reader is
    // looking at the pair to see.
    //
    // NEGATIVE CHECK: skipping unconverged points here leaves both panels with
    // the same holes, and the screen can no longer show what was missed.
    const KnownAnswer answer = knownAnswerFor(synthetic(QStringLiteral("translation_03.tif")));
    QVERIFY(answer.valid);

    CorrelationResult result = gridOf(4, 3, 20, 40, [](CorrelationPoint &) {});
    result.points[5].converged = false;
    result.converged--;

    const QVector<float> stated =
        layoutStatedField(result, FieldChannel::DisplacementX, answer);
    QCOMPARE(stated.size(), 12);
    for (int cell = 0; cell < stated.size(); cell++)
        QVERIFY2(!std::isnan(stated[cell]), qPrintable(QString::number(cell)));
}

void TestKnownAnswer::a_cell_no_point_reached_has_no_stated_value_either()
{
    // A region of interest leaves cells with no point at all. The instrument was
    // never pointed there, so there is nothing to state an answer about, and a
    // value drawn there would put the stated panel over ground the measured one
    // deliberately excluded.
    const KnownAnswer answer = knownAnswerFor(synthetic(QStringLiteral("translation_03.tif")));
    QVERIFY(answer.valid);

    CorrelationResult result = gridOf(4, 3, 20, 40, [](CorrelationPoint &) {});
    result.points.remove(7);

    const QVector<float> stated =
        layoutStatedField(result, FieldChannel::DisplacementX, answer);
    QCOMPARE(stated.size(), 12);
    QVERIFY(std::isnan(stated[7]));
    QVERIFY(!std::isnan(stated[6]));
}

void TestKnownAnswer::a_point_the_solver_rejected_is_not_an_error_of_zero()
{
    // The standing rule of this code base, one map further out. An error of zero
    // is the most reassuring reading available, and painted over the points the
    // solver could not measure it would say the instrument did best exactly
    // where it did nothing.
    //
    // NEGATIVE CHECK: differencing every point regardless of convergence writes
    // a large, confident error at the rejected point and this case goes red on
    // the isnan rather than on the value -- which is the point: the difference
    // must be ABSENT, not merely wrong.
    const KnownAnswer answer = knownAnswerFor(synthetic(QStringLiteral("translation_03.tif")));
    QVERIFY(answer.valid);

    CorrelationResult result = gridOf(4, 3, 20, 40, [](CorrelationPoint &) {});
    result.points[5].converged = false;
    result.points[5].u = 999.f;   // the solver's leftover guess
    result.converged--;

    const QVector<float> error =
        layoutErrorField(result, FieldChannel::DisplacementX, answer);
    QVERIFY(std::isnan(error[5]));
    QVERIFY(!std::isnan(error[4]));
}

void TestKnownAnswer::a_strain_the_fit_declined_is_not_an_error_of_zero()
{
    const KnownAnswer answer = knownAnswerFor(synthetic(QStringLiteral("tension_03.tif")));
    QVERIFY(answer.valid);

    CorrelationResult result = gridOf(4, 3, 20, 40, [](CorrelationPoint &point) {
        point.strainFitted = true;
    });
    result.points[2].strainFitted = false;
    result.strainRequested = true;
    result.strainFitted = 11;

    const QVector<float> error =
        layoutErrorField(result, FieldChannel::StrainXX, answer);
    QVERIFY(std::isnan(error[2]));
    QVERIFY(!std::isnan(error[3]));

    // And the displacement at that same point is still compared: the fit
    // declining says nothing about the displacement it was fitted from.
    const QVector<float> displacement =
        layoutErrorField(result, FieldChannel::DisplacementX, answer);
    QVERIFY(!std::isnan(displacement[2]));
}

void TestKnownAnswer::an_exact_measurement_comes_out_as_no_error_at_all()
{
    // A field measured exactly right must read as zero error everywhere. Without
    // this the whole screen could be showing the stated answer twice, or the
    // measurement twice, and every case above would still pass.
    const KnownAnswer answer = knownAnswerFor(synthetic(QStringLiteral("rotation_02.tif")));
    QVERIFY(answer.valid);

    CorrelationResult result = gridOf(5, 4, 30, 60, [&answer](CorrelationPoint &point) {
        point.u = float(statedValue(answer, FieldChannel::DisplacementX,
                                    point.x, point.y, StrainMeasure::Cauchy));
        point.v = float(statedValue(answer, FieldChannel::DisplacementY,
                                    point.x, point.y, StrainMeasure::Cauchy));
    });

    const QVector<float> error =
        layoutErrorField(result, FieldChannel::DisplacementX, answer);
    for (int cell = 0; cell < error.size(); cell++) {
        QVERIFY2(!std::isnan(error[cell]) && std::abs(error[cell]) < 1e-3,
                 qPrintable(QStringLiteral("cell %1 read an error of %2 on an exact "
                                           "measurement").arg(cell).arg(error[cell])));
    }

    const AccuracyReport report =
        accuracyAgainstStated(result, FieldChannel::DisplacementX, answer);
    QVERIFY(report.valid);
    QCOMPARE(report.compared, 20);
    QVERIFY(report.worstAbsolute < 1e-3);
    QVERIFY(report.rms < 1e-3);
}

void TestKnownAnswer::the_error_report_counts_only_the_points_that_were_measured()
{
    // ⚑ A run that measured a tenth of the field perfectly and lost the rest is
    // not a perfect run, and the report must not let the count go unsaid. The
    // figures describe the points that were compared; how many that was of how
    // many attempted is stated beside them.
    const KnownAnswer answer = knownAnswerFor(synthetic(QStringLiteral("translation_03.tif")));
    QVERIFY(answer.valid);

    CorrelationResult result = gridOf(4, 3, 20, 40, [&answer](CorrelationPoint &point) {
        point.u = float(statedValue(answer, FieldChannel::DisplacementX,
                                    point.x, point.y, StrainMeasure::Cauchy));
    });
    for (int index = 0; index < 8; index++) {
        result.points[index].converged = false;
        result.converged--;
    }

    const AccuracyReport report =
        accuracyAgainstStated(result, FieldChannel::DisplacementX, answer);
    QVERIFY(report.valid);
    QCOMPARE(report.compared, 4);
    QCOMPARE(report.attempted, 12);
    QVERIFY(report.worstAbsolute < 1e-3);
}

void TestKnownAnswer::the_report_says_where_the_worst_point_is_not_only_how_bad_it_is()
{
    // On a fitted quantity the answer is usually "at the edge of the picture",
    // which is a property of the fit rather than of the measurement -- and a
    // reader cannot tell those apart from a number alone.
    const KnownAnswer answer = knownAnswerFor(synthetic(QStringLiteral("translation_03.tif")));
    QVERIFY(answer.valid);

    CorrelationResult result = gridOf(4, 3, 20, 40, [&answer](CorrelationPoint &point) {
        point.u = float(statedValue(answer, FieldChannel::DisplacementX,
                                    point.x, point.y, StrainMeasure::Cauchy));
    });
    result.points[9].u += 0.5f;

    const AccuracyReport report =
        accuracyAgainstStated(result, FieldChannel::DisplacementX, answer);
    QVERIFY(std::abs(report.worstAbsolute - 0.5) < 1e-3);
    QCOMPARE(report.worstAtX, double(result.points[9].x));
    QCOMPARE(report.worstAtY, double(result.points[9].y));

    // The mean is over every compared point, so one bad point does not speak
    // for the field -- the same rule the noise floor's own reporting follows.
    QVERIFY(report.meanAbsolute < report.worstAbsolute / 4.0);
}

void TestKnownAnswer::a_run_with_nothing_measured_reports_no_accuracy_rather_than_a_perfect_one()
{
    // ⚑ Zero compared points averages to zero error, which is the flattering
    // reading and the one a default-constructed report would give.
    const KnownAnswer answer = knownAnswerFor(synthetic(QStringLiteral("translation_03.tif")));
    QVERIFY(answer.valid);

    CorrelationResult result = gridOf(4, 3, 20, 40, [](CorrelationPoint &point) {
        point.converged = false;
    });
    result.converged = 0;

    const AccuracyReport report =
        accuracyAgainstStated(result, FieldChannel::DisplacementX, answer);
    QVERIFY2(!report.valid, "a run that measured nothing was reported as accurate");
    QCOMPARE(report.compared, 0);

    // And a channel the answer cannot speak to has no report either, whatever
    // was measured.
    CorrelationResult measured = gridOf(4, 3, 20, 40, [](CorrelationPoint &point) {
        point.noiseFloor = 0.004f;
        point.noiseFloorMeasured = true;
    });
    QVERIFY(!accuracyAgainstStated(measured, FieldChannel::NoiseFloor, answer).valid);
}

void TestKnownAnswer::the_measured_and_stated_panels_are_drawn_on_one_shared_scale()
{
    // ⚑ THE RULE THE WHOLE COMPARISON RESTS ON. Two fields drawn on their own
    // scales look alike however far apart they are: a measurement that is
    // wrong by half would be painted in exactly the colours of the answer it
    // missed, since each panel would stretch its own colours over its own
    // range. One scale across both is what makes a difference visible as a
    // difference.
    //
    // NEGATIVE CHECK: ranging each panel separately leaves this red at the
    // measured panel's own 0..1, and turns the comparison into two pictures of
    // the same shape.
    const QVector<float> measured{0.f, 1.f, 0.5f};
    const QVector<float> stated{0.f, 4.f, 2.f};

    double lowest = 0.0;
    double highest = 0.0;
    QVERIFY(sharedColourRange(measured, stated, false, lowest, highest));
    QCOMPARE(lowest, 0.0);
    QCOMPARE(highest, 4.0);

    // And nothing measured on either side is no scale at all, rather than a
    // range of zero that would paint an empty field in one flat colour.
    const QVector<float> nothing{kNothingMeasured, kNothingMeasured};
    QVERIFY(!sharedColourRange(nothing, nothing, false, lowest, highest));
}

void TestKnownAnswer::a_shared_scale_for_a_signed_quantity_still_sits_about_zero()
{
    // The rule core/FieldLayout.h already applies to one field: a strain scale
    // is centred on zero because zero strain is a physical state. Sharing a
    // scale between two panels must not quietly drop that.
    const QVector<float> measured{-0.002f, 0.001f};
    const QVector<float> stated{-0.001f, 0.0015f};

    double lowest = 0.0;
    double highest = 0.0;
    QVERIFY(sharedColourRange(measured, stated, true, lowest, highest));
    // Compared with a tolerance rather than exactly: a field is held in float
    // and a colour range in double, so -0.002f widens to -0.0020000000949.
    QVERIFY(std::abs(lowest + 0.002) < 1e-9);
    QVERIFY(std::abs(highest - 0.002) < 1e-9);
    QCOMPARE(lowest, -highest);
}

void TestKnownAnswer::the_error_scale_is_centred_on_zero_whatever_the_errors_are()
{
    // ⚑ Zero error IS a physical state -- it is the answer being right -- so the
    // error scale is centred on it in every channel, displacement included.
    // Ranged over the errors alone, a field wrong by between 0.4 and 0.5 px
    // everywhere would put its best point at one end of the colours and its
    // worst at the other, and read as a field with a hole in it rather than as
    // a field that is uniformly wrong.
    //
    // NEGATIVE CHECK: ranging over the data gives 0.4 to 0.5 and this is red.
    const QVector<float> error{0.4f, 0.45f, 0.5f};

    double lowest = 0.0;
    double highest = 0.0;
    QVERIFY(errorColourRange(error, lowest, highest));
    QCOMPARE(lowest, -0.5);
    QCOMPARE(highest, 0.5);

    const QVector<float> nothing{kNothingMeasured};
    QVERIFY(!errorColourRange(nothing, lowest, highest));
}


void TestKnownAnswer::the_cauchy_shear_adds_the_two_off_diagonal_terms_rather_than_differencing_them()
{
    // ⚑ A ROTATION IS THE FIXTURE THAT CAN SEE THIS, and the shipped shear set
    // cannot. Shear states f10 = 0, so uy + vx and uy - vx come to the same
    // number and the formula's sign is invisible. A rotation states equal and
    // OPPOSITE off-diagonal terms, so their sum is zero and their difference
    // is the whole of one of them -- a quarter of a radian on the fifteen
    // degree frame.
    //
    // What is at stake: this is the answer the comparison window states as
    // TRUTH. Wrong here, a correct measurement is shown as a large error, and
    // the reader has every reason to believe the instrument rather than the
    // truth panel.
    const QJsonObject frame = statedFrame(QStringLiteral("rotation"), 4);
    const QJsonArray f = frame.value(QStringLiteral("deformation_gradient")).toArray();
    const double uy = f.at(0).toArray().at(1).toDouble();
    const double vx = f.at(1).toArray().at(0).toDouble();
    QVERIFY2(std::abs(uy + vx) < 1e-12,
             "the fixture's own off-diagonal terms are equal and opposite");
    QVERIFY2(std::abs(uy) > 0.1, "and large enough that the difference could not hide");

    const KnownAnswer answer =
        knownAnswerFor(synthetic(QStringLiteral("rotation_04.tif")));
    QVERIFY(answer.valid);

    const double linear = statedValue(answer, FieldChannel::StrainXY, 100.0, 300.0,
                                      StrainMeasure::Cauchy);
    QVERIFY2(std::abs(linear - 0.5 * (uy + vx)) < 1e-12,
             qPrintable(QStringLiteral("the Cauchy shear was stated as %1 where the "
                                       "file's own gradient gives %2")
                            .arg(linear).arg(0.5 * (uy + vx))));
}

void TestKnownAnswer::the_two_measures_state_different_shears_for_a_deformation_that_has_both()
{
    // ⚑ NO SHIPPED SET CAN TELL THE TWO SHEAR MEASURES APART. Green-Lagrange
    // adds uy*ux + vy*vx to the linear form, and every example states either
    // no shear (tension, translation, large strain) or no stretch (shear), so
    // that correction is zero in all of them and the two measures agree
    // exactly. The dispatch between them was therefore free to be inverted.
    //
    // A stated answer is plain data, so the case states a deformation that has
    // both -- five per cent stretch, a little shear, a little in the other
    // direction -- and asks each measure for its own answer. Nothing is
    // hard-coded: both expectations are the published formulae written out
    // here, and they differ by the correction term, which is the whole point.
    KnownAnswer answer;
    answer.valid = true;
    answer.f00 = 1.05;   // ux = 0.05
    answer.f01 = 0.04;   // uy = 0.04
    answer.f10 = 0.02;   // vx = 0.02
    answer.f11 = 0.97;   // vy = -0.03

    const double ux = answer.f00 - 1.0;
    const double uy = answer.f01;
    const double vx = answer.f10;
    const double vy = answer.f11 - 1.0;

    const double linear = statedValue(answer, FieldChannel::StrainXY, 10.0, 20.0,
                                      StrainMeasure::Cauchy);
    const double green = statedValue(answer, FieldChannel::StrainXY, 10.0, 20.0,
                                     StrainMeasure::GreenLagrange);

    QVERIFY2(std::abs(linear - 0.5 * (uy + vx)) < 1e-12,
             qPrintable(QStringLiteral("linear shear stated as %1").arg(linear)));
    QVERIFY2(std::abs(green - 0.5 * (uy + vx + uy * ux + vy * vx)) < 1e-12,
             qPrintable(QStringLiteral("Green-Lagrange shear stated as %1").arg(green)));
    QVERIFY2(std::abs(green - linear) > 1e-9,
             "the two measures must actually differ here, or the case cannot tell "
             "which one answered");
}

void TestKnownAnswer::a_frame_that_states_half_an_answer_states_none_of_it()
{
    // ⚑ HALF AN ANSWER IS NOT AN ANSWER. The frame states a deformation
    // gradient and a rigid shift, and both are read into fixed-size arrays. A
    // file carrying one of them and not the other is refused outright rather
    // than accepted with the missing half left at whatever a default-constructed
    // value happens to be -- which would put a confident error map on screen
    // against half of an experiment.
    //
    // The sweep of 2026-09-09 found the two conditions could be joined with AND
    // instead of OR, so a frame stating only one of them sailed through. Every
    // frame that ships states both, so nothing here could see it: this case
    // writes the malformed file it needs.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("half_an_answer.json"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({
      "sets": {
        "half": {
          "what_it_shows": "a frame that states a gradient and no shift",
          "frames": [
            {"file": "half_00.tif",
             "deformation_gradient": [[1.0, 0.0], [0.0, 1.0]],
             "rigid_shift_px": [0.0, 0.0]},
            {"file": "half_01.tif",
             "deformation_gradient": [[1.01, 0.0], [0.0, 1.0]]},
            {"file": "half_02.tif",
             "rigid_shift_px": [3.0, 0.0]}
          ]
        }
      }
    })");
    file.close();

    // The complete frame is read, or this case is testing a broken file rather
    // than a broken rule.
    QVERIFY2(knownAnswerFromFile(path, QStringLiteral("half_00.tif")).valid,
             "the frame stating both halves was refused, so this file is wrong "
             "rather than the rule");

    QVERIFY2(!knownAnswerFromFile(path, QStringLiteral("half_01.tif")).valid,
             "a frame stating a deformation and no shift was accepted");
    QVERIFY2(!knownAnswerFromFile(path, QStringLiteral("half_02.tif")).valid,
             "a frame stating a shift and no deformation was accepted");
}

void TestKnownAnswer::an_answer_that_is_not_valid_states_nothing_anywhere()
{
    // The comparison window is only offered where an answer exists, so the
    // guard at the top of each layout looks redundant -- and it is the last
    // thing standing between an image with NO stated answer and a full panel of
    // confident numbers computed from a default-constructed deformation. A
    // reader cannot tell those from measured ones: that is the whole hazard
    // this file exists for, arriving through the door nobody watches.
    const KnownAnswer nothingKnown;   // valid == false
    QVERIFY(!nothingKnown.valid);

    const CorrelationResult result = gridOf(4, 3, 20, 40, [](CorrelationPoint &) {});

    for (const FieldChannel channel : {FieldChannel::DisplacementX,
                                       FieldChannel::DisplacementY,
                                       FieldChannel::StrainXX}) {
        const QVector<float> stated = layoutStatedField(result, channel, nothingKnown);
        const QVector<float> error = layoutErrorField(result, channel, nothingKnown);
        QCOMPARE(stated.size(), 12);
        QCOMPARE(error.size(), 12);
        for (int cell = 0; cell < stated.size(); cell++) {
            QVERIFY2(std::isnan(stated[cell]),
                     qPrintable(QStringLiteral("cell %1 states %2 with no answer "
                                               "to state it from")
                                    .arg(cell).arg(double(stated[cell]))));
            QVERIFY2(std::isnan(error[cell]),
                     qPrintable(QStringLiteral("cell %1 reports an error of %2 "
                                               "against nothing")
                                    .arg(cell).arg(double(error[cell]))));
        }
    }

    // And the accuracy report, which is the same guard a third time and the one
    // that would put a NUMBER in the run log rather than a colour on a map: an
    // error of zero against an answer that does not exist reads as a perfect
    // measurement, which is the flattering reading this file already refuses
    // for a run that measured nothing.
    const AccuracyReport report =
        accuracyAgainstStated(result, FieldChannel::DisplacementX, nothingKnown);
    QVERIFY2(!report.valid, "an accuracy was reported against no answer at all");
    QCOMPARE(report.compared, 0);
    QCOMPARE(report.worstAbsolute, 0.0);
}

void TestKnownAnswer::a_run_that_measured_nothing_is_still_scaled_against_what_it_missed()
{
    // ⚑ The two panels share one colour scale so that a measurement wrong by
    // half is not painted in exactly the colours of the answer it missed. The
    // case where the MEASURED half is empty is the one that matters most and
    // was covered by nothing: a run that solved no points at all still has an
    // answer to show, and refusing to produce a range leaves the stated panel
    // with no scale to draw itself against - a blank window where the reader
    // most needs to see what the run failed to find.
    const QVector<float> nothingMeasured(12, std::numeric_limits<float>::quiet_NaN());
    QVector<float> stated(12, 0.0f);
    for (int cell = 0; cell < stated.size(); cell++)
        stated[cell] = float(cell) * 0.5f;

    double lowest = 0.0;
    double highest = 0.0;
    QVERIFY2(sharedColourRange(nothingMeasured, stated, false, lowest, highest),
             "a run that measured nothing left the stated answer with no scale");
    QCOMPARE(lowest, 0.0);
    QCOMPARE(highest, 5.5);

    // And the other way round, which is the same rule seen from the other side.
    QVERIFY2(sharedColourRange(stated, nothingMeasured, false, lowest, highest),
             "an answer that states nothing left the measurement with no scale");
    QCOMPARE(lowest, 0.0);
    QCOMPARE(highest, 5.5);

    // Only when there is nothing on either side is there no range to give.
    QVERIFY2(!sharedColourRange(nothingMeasured, nothingMeasured, false, lowest, highest),
             "two empty fields produced a colour range out of nothing");
}

QTEST_MAIN(TestKnownAnswer)
#include "test_known_answer.moc"
