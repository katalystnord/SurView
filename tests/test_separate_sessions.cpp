// A reference and a target photographed in separate sessions.
//
// The camera was moved between the two, so the target sees the scene larger,
// turned and shifted, in an image of a different size. The run estimates that
// move as a similarity (the engine's SessionAlignment2D), starts every point
// from it, measures on the target AS RECORDED, and carries each answer back
// into the reference frame. No image is resampled at any step.
//
// The fixture (tools/make-session-fixture.py) is a speckled rig with a specimen
// disc on it, radius 60 about (120, 100). Between sessions the specimen moves
// by d0 = (1.3, -0.7) and deforms by the gradient
//
//     [ 0.012   0.004 ]
//     [ 0.002  -0.005 ]
//
// and the camera scales by 1.07, turns 3.5 degrees and shifts so the specimen's
// centre lands at (140, 120) in a 280 x 240 target, against a 240 x 200
// reference. Four different non-zero gradient terms, a scale and a rotation
// together, so nothing the code could swap happens to agree.
//
// ⚑ What the alignment is fitted on decides what can be measured, and the
// result says which. Fitted on GROUND marked as stationary, nothing about the
// specimen is absorbed. Fitted on the measured region itself, the similarity
// takes up the specimen's own rigid motion and its uniform dilatation, here
// about (0.012 - 0.005) / 2 = 0.0035, which is a real strain lost. The cases
// below require both the loss and the sentence admitting it.
//
// Negative checks, each run and reverted (core/Correlation.cpp):
// - No carry-back into the reference frame: the three aligned measurement cases
//   red (28 px out on the ground case, the full 3 px shift on the rigid one).
// - The fit region built with the whole image added first (engineRegion() with
//   an image size, the masking convention): the dilatation case red, the fit
//   having used the rig and measured everything. ⚑ This was the first version,
//   and the ground case passed against it, because that ground region happens
//   to start with the whole image. Only the specimen case could see it.
// - FFTCC as the starting guess instead of the camera move: two cases red.
// - The ground ignored: the ground case red.
// - The refusal of a followed sequence removed: that case red.
// - The refusal of a failed fit removed: that case red.
// - A point refused at an edge judged against the REFERENCE's size instead of
//   the target's: NOT caught. No point in these fixtures is refused at an edge.
// - The sequence's refusal of re-anchoring across sessions removed: that case
//   red, a frame measured before anything refused.
// - The size refusal without the switch's name: the first case red.
//
// Ground as a shape of the region (2026-10-15), each run and reverted:
// - The run ignoring the ground: the ground case red, fitted on the region.
// - groundOf() returning the region's additions: the dilatation case red.
// - A region of nothing but ground counted as a region: the one-session case
//   red, the run refusing a region the whole image does not hold.
// - The engine counting ground as an addition: NOT caught here, because every
//   ground band lies outside the specimen's box and no point is placed there.
//   The pixel-by-pixel case in test_roi_engine_boundary catches it.
//
// The report and the files (2026-10-16), each run and reverted:
// - The ground left unrecorded by the run: the report case red, and the
//   walkthrough's log assertion with it.
// - The ground recorded but never named in the report: the report case red.
// - The frame sentence dropped: the report case red.
// - The files' statement reduced to the report's first line: the report case
//   red (test_field_csv catches it too, by the limitation going missing).
// - The ordinary run's statement left empty: the one-session case red.
//
// The known length (2026-10-16), each run and reverted:
// - Never handed to the engine: the known-length case red, knownLengthUsed false.
// - Handed to it with ground marked as well: the not-used case red.
// - Its length measured as |dx| + |dy|: the known-length case red on the length.
// - The limitation, the used line or the not-used line left as before: the
//   case naming each red.
// - The refusal without the tool's name: the refusal case red.
// - A sequence handing a separate session the points followed from the frame
//   before (the defect found by writing the sequence case): the sequence case
//   red at frame 2, as it was before the fix.
//
// Ground that moved, and ground or a known length over the specimen
// (2026-10-17), each run and reverted, each red here: the moved subsets never
// recorded; recorded with the kept ones among them; the farthest taken as the
// first; the overlap, or the known length's ends inside, never recorded; each
// of the three report lines removed.

#include "core/Correlation.h"
#include "core/Roi.h"
#include "core/SequenceRunner.h"
#include "core/SessionAlignment.h"

#include <QTest>

#include <cmath>

namespace {

constexpr double kU0 = 1.3, kV0 = -0.7;
constexpr double kE11 = 0.012, kE12 = 0.004, kE21 = 0.002, kE22 = -0.005;
constexpr double kCX = 120.0, kCY = 100.0;

QString fixture(const QString &name)
{
    return QStringLiteral(SURVIEW_TEST_FIXTURES "/") + name;
}

RegionShape box(RegionShape::Kind kind, int x0, int y0, int x1, int y1, bool cut = false)
{
    RegionShape shape;
    shape.kind = kind;
    shape.role = cut ? RegionShape::Cut : RegionShape::Add;
    shape.points = {QPoint(x0, y0), QPoint(x1, y1)};
    return shape;
}

// The specimen, kept 20 px inside its edge so no subset reaches the rig.
RegionOfInterest specimenRegion()
{
    RegionOfInterest roi;
    roi.shapes = {box(RegionShape::Ellipse, 80, 60, 160, 140)};
    return roi;
}

// The specimen region with the rig marked as ground around it: four bands,
// the whole reference less a box 10 px clear of the disc. Ground is a shape of
// the region (core/Roi.h), so it travels wherever the region does.
RegionOfInterest withGround(RegionOfInterest roi)
{
    for (const RegionShape &band : {box(RegionShape::Rectangle, 0, 0, 239, 29),
                                    box(RegionShape::Rectangle, 0, 171, 239, 199),
                                    box(RegionShape::Rectangle, 0, 30, 49, 170),
                                    box(RegionShape::Rectangle, 191, 30, 239, 170)}) {
        RegionShape ground = band;
        ground.role = RegionShape::Ground;
        roi.shapes.append(ground);
    }
    return roi;
}

// Two points on the rig, either side of the specimen, whose distance the move
// between sessions did not change. 246.1 px apart, so a length a reader could
// well mark on a scale bar.
RegionOfInterest withKnownLength(RegionOfInterest roi, QPoint a = QPoint(22, 24),
                                 QPoint b = QPoint(214, 178))
{
    roi.knownLength.marked = true;
    roi.knownLength.a = a;
    roi.knownLength.b = b;
    return roi;
}

CorrelationSettings settings()
{
    CorrelationSettings s;
    s.subsetRadius = 16;
    s.gridStep = 8;
    s.maxIterations = 20;
    s.strainEnabled = true;
    s.strainRadius = 25.0;
    return s;
}

struct Outcome
{
    CorrelationResult result;
    QString failure;
};

Outcome run(const RegionOfInterest &roi, const QString &reference, const QString &target,
            bool align)
{
    // The switch is a run setting, saved with the project; the ground is part
    // of the region drawn on the reference.
    CorrelationSettings s = settings();
    s.separateSessions = align;
    CorrelationRunner runner(s, roi, fixture(reference), fixture(target));

    Outcome outcome;
    QObject::connect(&runner, &CorrelationRunner::finished,
                     [&outcome](const CorrelationResult &r) { outcome.result = r; });
    QObject::connect(&runner, &CorrelationRunner::failed,
                     [&outcome](const QString &reason) { outcome.failure = reason; });
    runner.run();
    return outcome;
}

struct Means
{
    int solved = 0;
    double displacementError = 0;   // mean distance from the stated movement, px
    double exx = 0, eyy = 0, exy = 0;
    int strained = 0;
};

Means measure(const CorrelationResult &result)
{
    Means m;
    for (const CorrelationPoint &p : result.points) {
        if (!p.converged)
            continue;
        const double dx = p.x - kCX, dy = p.y - kCY;
        const double u = kU0 + kE11 * dx + kE12 * dy;
        const double v = kV0 + kE21 * dx + kE22 * dy;
        m.displacementError += std::hypot(p.u - u, p.v - v);
        m.solved++;
        if (p.strainFitted) {
            m.exx += p.exx;
            m.eyy += p.eyy;
            m.exy += p.exy;
            m.strained++;
        }
    }
    if (m.solved)
        m.displacementError /= m.solved;
    if (m.strained) {
        m.exx /= m.strained;
        m.eyy /= m.strained;
        m.exy /= m.strained;
    }
    return m;
}

}  // namespace

class TestSeparateSessions : public QObject
{
    Q_OBJECT

private slots:
    void a_target_of_another_size_is_refused_unless_aligned();
    void aligned_on_the_ground_the_specimen_reads_its_own_deformation();
    void aligned_on_the_specimen_the_uniform_dilatation_is_lost_and_said_so();
    void aligned_on_the_specimen_a_rigid_shift_reads_as_none_and_is_said_so();
    void an_alignment_that_finds_nothing_is_refused_in_its_own_words();
    void a_run_without_alignment_records_none();
    void a_followed_point_cannot_be_measured_across_sessions();
    void a_sequence_across_sessions_refuses_reference_updating_before_measuring();
    void a_sequence_aligns_every_frame_on_the_ground_marked();
    void ground_marked_in_one_session_changes_nothing_measured();
    void the_report_names_the_ground_it_was_aligned_on();
    void a_run_in_one_session_states_that_nothing_was_aligned();
    void aligned_on_the_specimen_with_a_known_length_the_dilatation_is_measured();
    void a_known_length_is_not_used_where_ground_is_marked();
    void a_known_length_whose_end_cannot_be_found_refuses_the_run();
    void a_sequence_carries_the_known_length_to_every_frame();
    void a_sequence_across_sessions_measures_every_frame_not_only_the_first();
    void ground_that_moved_is_reported_by_where_it_is_and_how_far();
    void ground_clear_of_the_specimen_reports_nothing_moved_and_no_overlap();
    void a_known_length_end_on_the_specimen_is_said();
    void ground_too_narrow_for_a_subset_is_refused_saying_why();
};

void TestSeparateSessions::a_target_of_another_size_is_refused_unless_aligned()
{
    const Outcome o = run(specimenRegion(), QStringLiteral("session_reference.tif"),
                          QStringLiteral("session_target.tif"), false);
    QVERIFY2(o.failure.contains(QStringLiteral("differ in size")), qPrintable(o.failure));
    // and names the control that measures such a pair
    QVERIFY2(o.failure.contains(separateSessionsLabel()), qPrintable(o.failure));
    QCOMPARE(o.result.total(), 0);
}

void TestSeparateSessions::aligned_on_the_ground_the_specimen_reads_its_own_deformation()
{
    const Outcome o = run(withGround(specimenRegion()), QStringLiteral("session_reference.tif"),
                          QStringLiteral("session_target.tif"), true);
    QVERIFY2(o.failure.isEmpty(), qPrintable(o.failure));

    const SessionAlignmentRecord &a = o.result.alignment;
    QCOMPARE(a.fittedOn, SessionAlignmentRecord::Ground);
    QVERIFY2(std::abs(a.scale - 1.07) < 1e-4, qPrintable(QString::number(a.scale, 'f', 6)));
    QVERIFY2(std::abs(a.angleDegrees - 3.5) < 0.01, qPrintable(QString::number(a.angleDegrees, 'f', 5)));
    QVERIFY(a.groundPointsUsed >= 20);
    QVERIFY(a.residualRms < 0.01);
    QVERIFY2(alignmentLimitation(a).isEmpty(), qPrintable(alignmentLimitation(a)));

    // Reported on the reference's own grid, in reference pixels.
    const Means m = measure(o.result);
    QCOMPARE(m.solved, o.result.total());
    QVERIFY(m.solved > 50);
    QVERIFY2(m.displacementError < 0.01, qPrintable(QString::number(m.displacementError, 'f', 5)));

    // Small-strain measure: exx = E11, eyy = E22, exy = (E12 + E21) / 2.
    QVERIFY(m.strained > 50);
    const QString strain = QStringLiteral("exx %1 eyy %2 exy %3")
                               .arg(m.exx, 0, 'f', 5).arg(m.eyy, 0, 'f', 5).arg(m.exy, 0, 'f', 5);
    QVERIFY2(std::abs(m.exx - kE11) < 3e-4, qPrintable(strain));
    QVERIFY2(std::abs(m.eyy - kE22) < 3e-4, qPrintable(strain));
    QVERIFY2(std::abs(m.exy - (kE12 + kE21) / 2) < 3e-4, qPrintable(strain));
}

void TestSeparateSessions::aligned_on_the_specimen_the_uniform_dilatation_is_lost_and_said_so()
{
    // No ground marked: the alignment can only be fitted on what is measured.
    const Outcome o = run(specimenRegion(), QStringLiteral("session_reference.tif"),
                          QStringLiteral("session_target.tif"), true);
    QVERIFY2(o.failure.isEmpty(), qPrintable(o.failure));

    const SessionAlignmentRecord &a = o.result.alignment;
    QCOMPARE(a.fittedOn, SessionAlignmentRecord::MeasuredRegion);
    const QString limitation = alignmentLimitation(a);
    QVERIFY2(limitation.contains(QStringLiteral("uniform dilatation")), qPrintable(limitation));
    QVERIFY2(limitation.contains(QStringLiteral("rigid")), qPrintable(limitation));

    const Means m = measure(o.result);
    const QString strain = QStringLiteral("exx %1 eyy %2 exy %3")
                               .arg(m.exx, 0, 'f', 5).arg(m.eyy, 0, 'f', 5).arg(m.exy, 0, 'f', 5);

    // The loss the sentence admits is real, and it is the kind it names: both
    // normal strains fall short by the SAME amount, which is what a dilatation
    // is. About the size of the true one, 0.0035, but not exactly it: a
    // least-squares similarity takes up exactly the dilatation only from points
    // spread evenly in every direction, and the 17 subsets that fit inside this
    // region are not, so a little of the directional strain goes with it.
    // Measured: 0.0040 short in both.
    const double shortX = kE11 - m.exx;
    const double shortY = kE22 - m.eyy;
    QVERIFY2(std::abs(shortX - shortY) < 3e-4, qPrintable(strain));
    QVERIFY2(shortX > 0.002 && shortX < 0.005, qPrintable(strain));
    // ... and nothing else is lost: the difference and the shear survive.
    QVERIFY2(std::abs((m.exx - m.eyy) - (kE11 - kE22)) < 3e-4, qPrintable(strain));
    QVERIFY2(std::abs(m.exy - (kE12 + kE21) / 2) < 3e-4, qPrintable(strain));
}

void TestSeparateSessions::aligned_on_the_specimen_a_rigid_shift_reads_as_none_and_is_said_so()
{
    // The same camera, the target the reference moved +3 px in x. Fitted on the
    // image being measured, that movement IS the camera as far as the alignment
    // can tell, and the field reads zero. Correct, and only honest because the
    // result says so.
    const Outcome o = run(RegionOfInterest(), QStringLiteral("shift_reference.tif"),
                          QStringLiteral("shift_target.tif"), true);
    QVERIFY2(o.failure.isEmpty(), qPrintable(o.failure));
    QCOMPARE(o.result.alignment.fittedOn, SessionAlignmentRecord::WholeImage);
    QVERIFY2(std::abs(o.result.alignment.shiftX - 3.0) < 0.01,
             qPrintable(QString::number(o.result.alignment.shiftX, 'f', 4)));
    QVERIFY(alignmentLimitation(o.result.alignment).contains(QStringLiteral("rigid")));

    double worst = 0;
    int solved = 0;
    for (const CorrelationPoint &p : o.result.points) {
        if (!p.converged)
            continue;
        worst = std::max(worst, std::hypot(double(p.u), double(p.v)));
        solved++;
    }
    QVERIFY(solved > 50);
    QVERIFY2(worst < 0.02, qPrintable(QString::number(worst, 'f', 4)));
}

void TestSeparateSessions::an_alignment_that_finds_nothing_is_refused_in_its_own_words()
{
    const Outcome o = run(RegionOfInterest(), QStringLiteral("shift_reference.tif"),
                          QStringLiteral("blank_frame.tif"), true);
    QVERIFY2(o.failure.contains(QStringLiteral("could not be aligned")), qPrintable(o.failure));
    QCOMPARE(o.result.total(), 0);
}

void TestSeparateSessions::a_run_without_alignment_records_none()
{
    const Outcome o = run(RegionOfInterest(), QStringLiteral("shift_reference.tif"),
                          QStringLiteral("shift_target.tif"), false);
    QVERIFY2(o.failure.isEmpty(), qPrintable(o.failure));
    QCOMPARE(o.result.alignment.fittedOn, SessionAlignmentRecord::NotAligned);
    QVERIFY(!o.result.alignment.isAligned());
    QVERIFY(alignmentLimitation(o.result.alignment).isEmpty());
}

void TestSeparateSessions::a_followed_point_cannot_be_measured_across_sessions()
{
    // A point followed from an earlier frame sits in that frame's camera, which
    // is not the one the alignment describes. Pair by pair only, said so.
    CorrelationSettings aligned = settings();
    aligned.separateSessions = true;
    CorrelationRunner runner(aligned, withGround(RegionOfInterest()),
                             fixture(QStringLiteral("session_reference.tif")),
                             fixture(QStringLiteral("session_target.tif")));
    PoiSeeding seeding;
    PoiSeeding::Seed seed;
    seed.x = seed.x0 = 120.f;
    seed.y = seed.y0 = 100.f;
    seeding.points = {seed};
    seeding.gridColumns = seeding.gridRows = 1;
    runner.setSeeding(seeding);

    QString failure;
    bool finished = false;
    QObject::connect(&runner, &CorrelationRunner::failed,
                     [&failure](const QString &reason) { failure = reason; });
    QObject::connect(&runner, &CorrelationRunner::finished,
                     [&finished](const CorrelationResult &) { finished = true; });
    runner.run();
    QVERIFY(!finished);
    QVERIFY2(failure.contains(QStringLiteral("one pair at a time")), qPrintable(failure));
}

void TestSeparateSessions::a_sequence_across_sessions_refuses_reference_updating_before_measuring()
{
    // Re-anchoring would follow points into a later frame's camera, which the
    // pair-by-pair refusal above catches only once it has happened: after the
    // first frames were measured, at whatever frame tracking first degraded.
    // Refused before anything is measured instead, naming both switches.
    CorrelationSettings aligned = settings();
    aligned.separateSessions = true;
    ReferenceUpdatePolicy policy;
    policy.enabled = true;
    SequenceRunner runner(aligned, specimenRegion(), fixture(QStringLiteral("session_reference.tif")),
                          {fixture(QStringLiteral("session_target.tif"))}, policy);

    QString failure;
    int frames = 0;
    QObject::connect(&runner, &SequenceRunner::failed,
                     [&failure](const QString &reason) { failure = reason; });
    QObject::connect(&runner, &SequenceRunner::frameFinished,
                     [&frames](int, const CorrelationResult &) { frames++; });
    runner.run();
    QCOMPARE(frames, 0);
    QVERIFY2(failure.contains(separateSessionsLabel()), qPrintable(failure));
    QVERIFY2(failure.contains(QStringLiteral("Re-anchor the reference")), qPrintable(failure));
}

void TestSeparateSessions::a_sequence_aligns_every_frame_on_the_ground_marked()
{
    // What the application runs is a sequence, so the ground has to reach every
    // frame through it, and not only a pair measured directly.
    CorrelationSettings aligned = settings();
    aligned.separateSessions = true;
    SequenceRunner runner(aligned, withGround(specimenRegion()),
                          fixture(QStringLiteral("session_reference.tif")),
                          {fixture(QStringLiteral("session_target.tif"))}, ReferenceUpdatePolicy());

    QString failure;
    QVector<CorrelationResult> frames;
    QObject::connect(&runner, &SequenceRunner::failed,
                     [&failure](const QString &reason) { failure = reason; });
    QObject::connect(&runner, &SequenceRunner::frameFinished,
                     [&frames](int, const CorrelationResult &r) { frames << r; });
    runner.run();
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.first().alignment.fittedOn, SessionAlignmentRecord::Ground);
    const Means m = measure(frames.first());
    QVERIFY2(std::abs(m.exx - kE11) < 3e-4, qPrintable(QString::number(m.exx, 'f', 5)));
}

void TestSeparateSessions::ground_marked_in_one_session_changes_nothing_measured()
{
    // Ground is not part of the region measured: with the switch off it does
    // nothing at all, and a region of nothing but ground measures the whole
    // image exactly as no region does. On the shift pair, lopsided ground in a
    // corner where a point would land if it were read as an addition.
    RegionOfInterest onlyGround;
    RegionShape corner = box(RegionShape::Rectangle, 3, 5, 61, 40);
    corner.role = RegionShape::Ground;
    onlyGround.shapes = {corner};

    const Outcome plain = run(RegionOfInterest(), QStringLiteral("shift_reference.tif"),
                              QStringLiteral("shift_target.tif"), false);
    const Outcome grounded = run(onlyGround, QStringLiteral("shift_reference.tif"),
                                 QStringLiteral("shift_target.tif"), false);
    QVERIFY2(grounded.failure.isEmpty(), qPrintable(grounded.failure));
    QCOMPARE(grounded.result.total(), plain.result.total());
    QCOMPARE(grounded.result.alignment.fittedOn, SessionAlignmentRecord::NotAligned);
    for (int i = 0; i < plain.result.points.size(); i++) {
        QCOMPARE(grounded.result.points.at(i).x, plain.result.points.at(i).x);
        QCOMPARE(grounded.result.points.at(i).u, plain.result.points.at(i).u);
    }

    // And beside a specimen region it measures the specimen only.
    const Outcome specimen = run(specimenRegion(), QStringLiteral("session_reference.tif"),
                                 QStringLiteral("session_reference.tif"), false);
    const Outcome beside = run(withGround(specimenRegion()), QStringLiteral("session_reference.tif"),
                               QStringLiteral("session_reference.tif"), false);
    QCOMPARE(beside.result.total(), specimen.result.total());
}

void TestSeparateSessions::the_report_names_the_ground_it_was_aligned_on()
{
    // The report is the run's account of itself: which ground the camera move
    // was fitted on, as drawn, and the frame the answers are given in. Four
    // bands, two of them 240 x 30 px, so a summary of one shape cannot pass.
    const Outcome o = run(withGround(specimenRegion()), QStringLiteral("session_reference.tif"),
                          QStringLiteral("session_target.tif"), true);
    QVERIFY2(o.failure.isEmpty(), qPrintable(o.failure));
    const QString report = alignmentReport(o.result.alignment).join(QLatin1Char('\n'));
    QVERIFY2(report.contains(QStringLiteral("aligned on the ground marked as stationary"))
                 && report.contains(QStringLiteral("4 shapes, rectangle 240 x 30 px")),
             qPrintable(report));
    QVERIFY2(report.contains(QStringLiteral("reference image's pixels")), qPrintable(report));
    // The files say all of it, in one statement.
    const QString statement = alignmentStatement(o.result.alignment);
    for (const QString &line : alignmentReport(o.result.alignment))
        QVERIFY2(statement.contains(line), qPrintable(statement));
}

void TestSeparateSessions::a_run_in_one_session_states_that_nothing_was_aligned()
{
    // The files state the alignment whether or not there was one: a file that
    // says nothing about it reads the same from a run that aligned and from a
    // SurView that predates the switch.
    const Outcome o = run(RegionOfInterest(), QStringLiteral("shift_reference.tif"),
                          QStringLiteral("shift_target.tif"), false);
    QVERIFY2(o.failure.isEmpty(), qPrintable(o.failure));
    QVERIFY(alignmentReport(o.result.alignment).isEmpty());
    const QString statement = alignmentStatement(o.result.alignment);
    QVERIFY2(statement.contains(QStringLiteral("one camera")), qPrintable(statement));
}

void TestSeparateSessions::aligned_on_the_specimen_with_a_known_length_the_dilatation_is_measured()
{
    // No ground: the fit is on the specimen, as in the dilatation case above,
    // but the camera's scale is taken from a length that did not change. Only
    // the specimen's rigid motion is then taken for the camera, and every
    // strain term comes back, the uniform dilatation the other case loses
    // among them.
    const Outcome o = run(withKnownLength(specimenRegion()), QStringLiteral("session_reference.tif"),
                          QStringLiteral("session_target.tif"), true);
    QVERIFY2(o.failure.isEmpty(), qPrintable(o.failure));
    const SessionAlignmentRecord &a = o.result.alignment;
    QCOMPARE(a.fittedOn, SessionAlignmentRecord::MeasuredRegion);
    QVERIFY(a.knownLengthUsed);
    QVERIFY2(std::abs(a.scale - 1.07) < 1e-4, qPrintable(QString::number(a.scale, 'f', 6)));
    QVERIFY2(std::abs(a.knownLengthReference - 246.13) < 0.01,
             qPrintable(QString::number(a.knownLengthReference, 'f', 3)));
    QVERIFY2(std::abs(a.knownLengthTarget / a.knownLengthReference - 1.07) < 1e-4,
             qPrintable(QString::number(a.knownLengthTarget, 'f', 3)));

    const Means m = measure(o.result);
    const QString strain = QStringLiteral("exx %1 eyy %2 exy %3")
                               .arg(m.exx, 0, 'f', 5).arg(m.eyy, 0, 'f', 5).arg(m.exy, 0, 'f', 5);
    QVERIFY(m.strained > 10);
    QVERIFY2(std::abs(m.exx - kE11) < 3e-4, qPrintable(strain));
    QVERIFY2(std::abs(m.eyy - kE22) < 3e-4, qPrintable(strain));
    QVERIFY2(std::abs(m.exy - (kE12 + kE21) / 2) < 3e-4, qPrintable(strain));

    // What is said: the rigid motion is still the camera's, the strain is not.
    const QString limitation = alignmentLimitation(a);
    QVERIFY2(limitation.contains(QStringLiteral("rigid motion"))
                 && limitation.contains(QStringLiteral("known length"))
                 && !limitation.contains(QStringLiteral("are not measured: both")),
             qPrintable(limitation));
    const QString report = alignmentReport(a).join(QLatin1Char('\n'));
    QVERIFY2(report.contains(QStringLiteral("(22, 24) to (214, 178)"))
                 && report.contains(QStringLiteral("246.1 px in the reference")),
             qPrintable(report));
}

void TestSeparateSessions::a_known_length_is_not_used_where_ground_is_marked()
{
    // Ground measures the scale itself, from far more of the picture than two
    // points. The length is left out, and the report says it was.
    const Outcome o = run(withKnownLength(withGround(specimenRegion())),
                          QStringLiteral("session_reference.tif"),
                          QStringLiteral("session_target.tif"), true);
    QVERIFY2(o.failure.isEmpty(), qPrintable(o.failure));
    const SessionAlignmentRecord &a = o.result.alignment;
    QCOMPARE(a.fittedOn, SessionAlignmentRecord::Ground);
    QVERIFY(a.knownLengthMarked);
    QVERIFY(!a.knownLengthUsed);
    const QString report = alignmentReport(a).join(QLatin1Char('\n'));
    QVERIFY2(report.contains(QStringLiteral("known length")) && report.contains(QStringLiteral("not used")),
             qPrintable(report));
}

void TestSeparateSessions::a_known_length_whose_end_cannot_be_found_refuses_the_run()
{
    // An end too near the edge for its subset: refused, in words that name the
    // tool that moves it, never measured as though it had not been marked.
    const Outcome o = run(withKnownLength(specimenRegion(), QPoint(3, 3)),
                          QStringLiteral("session_reference.tif"),
                          QStringLiteral("session_target.tif"), true);
    QVERIFY2(o.failure.contains(QStringLiteral("known length"))
                 && o.failure.contains(markKnownLengthLabel()),
             qPrintable(o.failure));
    QCOMPARE(o.result.total(), 0);
}

void TestSeparateSessions::a_sequence_carries_the_known_length_to_every_frame()
{
    CorrelationSettings s = settings();
    s.separateSessions = true;
    SequenceRunner runner(s, withKnownLength(specimenRegion()),
                          fixture(QStringLiteral("session_reference.tif")),
                          {fixture(QStringLiteral("session_target.tif")),
                           fixture(QStringLiteral("session_target.tif"))},
                          ReferenceUpdatePolicy());
    QVector<CorrelationResult> frames;
    QString failure;
    QObject::connect(&runner, &SequenceRunner::frameFinished,
                     [&frames](int, const CorrelationResult &r) { frames.append(r); });
    QObject::connect(&runner, &SequenceRunner::failed,
                     [&failure](const QString &reason) { failure = reason; });
    runner.run();
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QCOMPARE(frames.size(), 2);
    for (const CorrelationResult &frame : frames)
        QVERIFY(frame.alignment.knownLengthUsed);
}

void TestSeparateSessions::a_sequence_across_sessions_measures_every_frame_not_only_the_first()
{
    // ⚑ Found 2026-10-16 by the known-length sequence case: from the second
    // frame on, a sequence handed the run the points as followed from the
    // frame before, and a run across sessions refuses a followed point, so
    // every sequence of more than one target stopped at frame 2 telling the
    // reader to turn off reference updating that was already off. Every case
    // before this one ran a single target.
    CorrelationSettings s = settings();
    s.separateSessions = true;
    SequenceRunner runner(s, withGround(specimenRegion()),
                          fixture(QStringLiteral("session_reference.tif")),
                          {fixture(QStringLiteral("session_target.tif")),
                           fixture(QStringLiteral("session_target.tif")),
                           fixture(QStringLiteral("session_target.tif"))},
                          ReferenceUpdatePolicy());
    QVector<CorrelationResult> frames;
    QString failure;
    QObject::connect(&runner, &SequenceRunner::frameFinished,
                     [&frames](int, const CorrelationResult &r) { frames.append(r); });
    QObject::connect(&runner, &SequenceRunner::failed,
                     [&failure](const QString &reason) { failure = reason; });
    runner.run();
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QCOMPARE(frames.size(), 3);
    for (const CorrelationResult &frame : frames) {
        QCOMPARE(frame.alignment.fittedOn, SessionAlignmentRecord::Ground);
        const Means m = measure(frame);
        QVERIFY2(m.displacementError < 0.01, qPrintable(QString::number(m.displacementError, 'f', 5)));
    }
}

void TestSeparateSessions::ground_that_moved_is_reported_by_where_it_is_and_how_far()
{
    // Ground marked over the specimen as well as the rig: a box on the disc,
    // which moved 1.3 px and stretched between the sessions. The fit still
    // rests on the rig, since the refinement leaves out what the camera move
    // does not describe, but the reader marked moving ground as stationary
    // and is told so, with WHERE: the run names the subsets left out, how far
    // from the fit each was found, and the farthest by its position.
    RegionOfInterest roi = withGround(specimenRegion());
    const QRect onDisc(QPoint(84, 66), QPoint(136, 134));
    RegionShape moved = box(RegionShape::Rectangle, onDisc.left(), onDisc.top(),
                            onDisc.right(), onDisc.bottom());
    moved.role = RegionShape::Ground;
    roi.shapes.append(moved);

    const Outcome o = run(roi, QStringLiteral("session_reference.tif"),
                          QStringLiteral("session_target.tif"), true);
    QVERIFY2(o.failure.isEmpty(), qPrintable(o.failure));
    const SessionAlignmentRecord &a = o.result.alignment;
    QCOMPARE(a.fittedOn, SessionAlignmentRecord::Ground);
    const Means m = measure(o.result);
    QVERIFY2(std::abs(m.exx - kE11) < 3e-4 && std::abs(m.eyy - kE22) < 3e-4,
             qPrintable(QStringLiteral("%1 %2").arg(m.exx).arg(m.eyy)));

    int far = 0;
    MovedGround farthest;
    for (const MovedGround &g : a.groundMoved) {
        if (g.offset > farthest.offset)
            farthest = g;
        if (g.offset <= 0.1)
            continue;
        far++;
        QVERIFY2(onDisc.contains(g.at),
                 qPrintable(QStringLiteral("(%1, %2) reported %3 px off, on the rig")
                                .arg(g.at.x()).arg(g.at.y()).arg(g.offset)));
    }
    QVERIFY2(far >= 4, qPrintable(QString::number(far)));
    // Only what the fit left out: none of the subsets it kept.
    QVERIFY(a.groundMoved.size() + a.groundPointsUsed <= a.groundPoints);
    // The specimen moved 1.3 px and the target camera magnifies by 1.07.
    QVERIFY2(farthest.offset > 1.0 && onDisc.contains(farthest.at),
             qPrintable(QString::number(farthest.offset)));

    const QString report = alignmentReport(a).join(QLatin1Char('\n'));
    QVERIFY2(report.contains(QStringLiteral("%1 ground subsets were left out of the fit")
                                 .arg(a.groundMoved.size())),
             qPrintable(report));
    QVERIFY2(report.contains(QStringLiteral("the farthest, %1 px from it, at (%2, %3)")
                                 .arg(farthest.offset, 0, 'f', 2)
                                 .arg(farthest.at.x()).arg(farthest.at.y())),
             qPrintable(report));
    QVERIFY2(report.contains(markGroundLabel()), qPrintable(report));

    // And it overlaps the region measured, counted before anything correlated.
    QVERIFY2(a.groundInsideRegion > 0 && a.groundInsideRegion < a.groundPixels,
             qPrintable(QStringLiteral("%1 of %2").arg(a.groundInsideRegion).arg(a.groundPixels)));
    QVERIFY2(report.contains(QStringLiteral("%1 px of the ground marked lies inside the region measured")
                                 .arg(a.groundInsideRegion)),
             qPrintable(report));
}

void TestSeparateSessions::ground_clear_of_the_specimen_reports_nothing_moved_and_no_overlap()
{
    // The rig alone: whatever the refinement leaves out sits within a
    // hundredth of a pixel or so of the fit, nothing overlaps, and the report
    // has nothing to warn about.
    const Outcome o = run(withGround(specimenRegion()), QStringLiteral("session_reference.tif"),
                          QStringLiteral("session_target.tif"), true);
    QVERIFY2(o.failure.isEmpty(), qPrintable(o.failure));
    const SessionAlignmentRecord &a = o.result.alignment;
    for (const MovedGround &g : a.groundMoved)
        QVERIFY2(g.offset < 0.1, qPrintable(QString::number(g.offset)));
    QVERIFY(a.groundMoved.size() + a.groundPointsUsed <= a.groundPoints);
    QCOMPARE(a.groundInsideRegion, qint64(0));
    QVERIFY(a.groundPixels > 0);
    const QString report = alignmentReport(a).join(QLatin1Char('\n'));
    QVERIFY2(!report.contains(QStringLiteral("inside the region measured")), qPrintable(report));
}

void TestSeparateSessions::a_known_length_end_on_the_specimen_is_said()
{
    // A known length is taken as unchanged, so an end on the specimen hands
    // the specimen's stretch along it to the camera's zoom. One end on the rig,
    // one inside the region measured.
    const Outcome o = run(withKnownLength(specimenRegion(), QPoint(22, 24), QPoint(130, 104)),
                          QStringLiteral("session_reference.tif"),
                          QStringLiteral("session_target.tif"), true);
    QVERIFY2(o.failure.isEmpty(), qPrintable(o.failure));
    QVERIFY(o.result.alignment.knownLengthUsed);
    QCOMPARE(o.result.alignment.knownLengthEndsInside, 1);
    const QString report = alignmentReport(o.result.alignment).join(QLatin1Char('\n'));
    QVERIFY2(report.contains(QStringLiteral("An end of the known length lies inside the region measured")),
             qPrintable(report));

    const Outcome clear = run(withKnownLength(specimenRegion()), QStringLiteral("session_reference.tif"),
                              QStringLiteral("session_target.tif"), true);
    QCOMPARE(clear.result.alignment.knownLengthEndsInside, 0);
    QVERIFY(!alignmentReport(clear.result.alignment).join(QLatin1Char('\n'))
                 .contains(QStringLiteral("of the known length lies inside")));
}

void TestSeparateSessions::ground_too_narrow_for_a_subset_is_refused_saying_why()
{
    // Ground 30 x 40 px, under 33 px subsets: no subset lies wholly on it, so
    // the alignment has nothing to correlate. Found by screenshot, where the
    // refusal read "only 0 of 0 subsets correlated and agreed", which is true
    // and says nothing about why. Negative check: the size branch removed, red
    // here on the wording.
    RegionOfInterest roi = specimenRegion();
    RegionShape narrow = box(RegionShape::Rectangle, 10, 10, 39, 49);
    narrow.role = RegionShape::Ground;
    roi.shapes.append(narrow);
    const Outcome o = run(roi, QStringLiteral("session_reference.tif"),
                          QStringLiteral("session_target.tif"), true);
    QVERIFY2(o.failure.contains(QStringLiteral("only subsets lying wholly on the ground"))
                 && o.failure.contains(QStringLiteral("each 33 px across"))
                 && o.failure.contains(QStringLiteral("the ground marked holds 0")),
             qPrintable(o.failure));
    QCOMPARE(groundSubsetCount(roi, settings().subsetRadius,
                               alignmentStep(settings().gridStep, narrow.bounds()), QSize(240, 200)),
             0);
}

QTEST_MAIN(TestSeparateSessions)
#include "test_separate_sessions.moc"
