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

QTEST_MAIN(TestSeparateSessions)
#include "test_separate_sessions.moc"
