// Masked subsets at cuts (CorrelationSettings::maskAtCuts).
//
// The fixture is tests/fixtures/hole_reference.tif and hole_target.tif, made
// by tools/make-hole-fixture.py: a speckled specimen moving by exactly
// (+2.4, -1.3) px, with a 20 px hole centred on (120, 100) that moves with it,
// and a second speckle behind the hole that does not move at all. A subset
// beside the hole therefore sees part of a pattern that stayed put, which is
// the case masking exists for. The answer is exact by construction, rendered
// analytically and never warped.
//
// Written red first (2026-10-11): seven of ten cases failed against stubs. The
// three that passed describe masking OFF, or no cut, which the stub was.
//
// NEGATIVE CHECKS, each caught: the switch ignored; the original-reference
// rule ignored; the outer boundary masked as well; no pull-in; the floor not
// handed to the solver; no mask on the solver; none on the reliability pass;
// the share not recorded; the masked flag never set; the masked count missing;
// either refusal left in the engine's words; the pull-in, the floor or the note
// reported wrong; a cut built as an addition. NOT caught here: no mask on the
// FFTCC starting guess, because on this fixture an unmasked guess is still
// within a pixel; the engine's masked_subset_smoke_test pins it on a fixture
// built for it. The panel, the report and the readout are checked the same way
// in test_point_readout, test_field_csv, test_field_export and the walkthrough.
//
// The region is drawn the way a person would: a rectangle over the specimen,
// then an ellipse cut over the hole's box. Lopsided where it can be: the
// movement differs in x and y and in sign, and the outer rectangle sits at
// different distances from each edge of the picture.

#include "core/Correlation.h"
#include "core/Masking.h"
#include "core/Roi.h"

#include <QHash>
#include <QTest>

#include <cmath>

namespace {

const float kTrueU = 2.4f;
const float kTrueV = -1.3f;

QString fixture(const QString &name)
{
    return QStringLiteral(SURVIEW_TEST_FIXTURES "/") + name;
}

RegionShape box(RegionShape::Kind kind, int left, int top, int right, int bottom, bool cut)
{
    RegionShape shape;
    shape.kind = kind;
    shape.subtract = cut;
    shape.points = {QPoint(left, top), QPoint(right, bottom)};
    return shape;
}

// The specimen, with the hole's box cut out as an ellipse.
RegionOfInterest specimenWithHole()
{
    RegionOfInterest roi;
    roi.shapes << box(RegionShape::Rectangle, 6, 9, 232, 188, false)
               << box(RegionShape::Ellipse, 100, 80, 140, 120, true);
    return roi;
}

CorrelationSettings settings()
{
    CorrelationSettings s;
    s.subsetRadius = 16;
    s.gridStep = 8;
    s.maxIterations = 20;
    s.strainEnabled = false;
    return s;
}

CorrelationResult runOn(const CorrelationSettings &s, const RegionOfInterest &roi,
                        bool referenceIsTheOriginal = true)
{
    CorrelationRunner runner(s, roi, fixture(QStringLiteral("hole_reference.tif")),
                             fixture(QStringLiteral("hole_target.tif")));
    runner.setReferenceIsTheOriginal(referenceIsTheOriginal);
    CorrelationResult result;
    QString failure;
    QObject::connect(&runner, &CorrelationRunner::finished,
                     [&result](const CorrelationResult &r) { result = r; });
    QObject::connect(&runner, &CorrelationRunner::failed,
                     [&failure](const QString &reason) { failure = reason; });
    runner.run();
    if (!failure.isEmpty())
        qWarning("%s", qPrintable(failure));
    return result;
}

// The run five cases read and none changes, measured once: on a debug build a
// run of this fixture costs several seconds.
const CorrelationResult &defaultRun()
{
    static const CorrelationResult result = runOn(settings(), specimenWithHole());
    return result;
}

double error(const CorrelationPoint &p)
{
    return std::hypot(double(p.u - kTrueU), double(p.v - kTrueV));
}

// Points whose subset reaches the cut, by the region's own rule and nothing
// the run decides: the set both runs are compared over.
bool besideTheHole(const CorrelationPoint &p)
{
    return subsetReachesACut(specimenWithHole(), qRound(p.x), qRound(p.y), 16);
}

double meanErrorBesideTheHole(const CorrelationResult &result, int *counted = nullptr)
{
    double sum = 0.0;
    int n = 0;
    for (const CorrelationPoint &p : result.points) {
        if (p.converged && besideTheHole(p)) {
            sum += error(p);
            n++;
        }
    }
    if (counted)
        *counted = n;
    return n > 0 ? sum / n : 1e9;
}

// The share a point SHOULD be measured on, counted here pixel by pixel from the
// region's own membership rule, with the pull-in as a square neighbourhood: a
// pixel is ground when no pixel within kMaskPullIn of it was removed by a cut.
// An independent count, not the engine's, so the two can be held to each other.
double expectedShare(const RegionOfInterest &roi, int x, int y, int radius)
{
    const auto cutAway = [&roi](int px, int py) {
        for (int i = roi.shapes.size() - 1; i >= 0; i--) {
            if (shapeContains(roi.shapes.at(i), px, py))
                return roi.shapes.at(i).subtract;
        }
        return false;
    };
    int ground = 0;
    for (int py = y - radius; py <= y + radius; py++) {
        for (int px = x - radius; px <= x + radius; px++) {
            bool clear = true;
            for (int dy = -kMaskPullIn; dy <= kMaskPullIn && clear; dy++)
                for (int dx = -kMaskPullIn; dx <= kMaskPullIn && clear; dx++)
                    clear = !cutAway(px + dx, py + dy);
            ground += clear ? 1 : 0;
        }
    }
    return double(ground) / double((2 * radius + 1) * (2 * radius + 1));
}

}  // namespace

class TestMaskedSubsets : public QObject
{
    Q_OBJECT

private slots:
    void a_point_beside_a_cut_is_measured_on_the_ground_outside_it();
    void every_point_carries_the_share_of_its_subset_it_was_measured_on();
    void the_outer_boundary_keeps_the_whole_subset();
    void an_island_put_back_inside_a_cut_is_ground_again();
    void below_the_floor_a_point_is_refused_and_says_why();
    void switching_masking_off_measures_as_before_and_still_counts_the_points();
    void a_region_without_cuts_is_measured_exactly_as_without_masking();
    void the_run_reports_what_masking_did();
    void the_noise_floor_beside_a_cut_is_that_of_the_ground_it_was_measured_on();
    void a_solver_that_cannot_mask_refuses_points_beside_a_cut_and_names_one_that_can();
    void a_reference_the_cuts_were_not_drawn_on_is_not_masked_and_says_why();

    // --- what the screen says, engine-free ---------------------------------
    void the_panel_note_says_what_masking_will_do_and_that_the_outer_edge_differs();
    void the_panel_note_says_why_masking_would_do_nothing();
    void the_run_report_states_what_masking_did_or_how_many_points_it_would_have_saved();
};

void TestMaskedSubsets::a_point_beside_a_cut_is_measured_on_the_ground_outside_it()
{
    CorrelationSettings masked = settings();
    CorrelationSettings plain = settings();
    plain.maskAtCuts = false;

    int nMasked = 0;
    int nPlain = 0;
    QCOMPARE(masked.maskAtCuts, true);   // the default, so the cached run is this one
    const double withMask = meanErrorBesideTheHole(defaultRun(), &nMasked);
    const double without = meanErrorBesideTheHole(runOn(plain, specimenWithHole()), &nPlain);

    QVERIFY2(nMasked >= 40, qPrintable(QString::number(nMasked)));
    QVERIFY2(withMask < 0.02,
             qPrintable(QStringLiteral("%1 px over %2 points").arg(withMask).arg(nMasked)));
    // The fixture bites: the same points, unmasked, are pulled toward the
    // background that did not move.
    QVERIFY2(without > 0.15,
             qPrintable(QStringLiteral("%1 px over %2 points").arg(without).arg(nPlain)));
}

void TestMaskedSubsets::every_point_carries_the_share_of_its_subset_it_was_measured_on()
{
    const RegionOfInterest roi = specimenWithHole();
    const CorrelationResult &result = defaultRun();
    QVERIFY(!result.points.isEmpty());

    int masked = 0;
    int checked = 0;
    for (const CorrelationPoint &p : result.points) {
        const double expected = expectedShare(roi, qRound(p.x), qRound(p.y), 16);
        // every fifth point is enough to cover the field and keeps this quick
        if ((p.gridIndex % 5) == 0 || p.masked) {
            QVERIFY2(std::abs(double(p.validShare) - expected) < 1e-6,
                     qPrintable(QStringLiteral("(%1, %2): %3, %4 expected")
                                    .arg(p.x).arg(p.y).arg(p.validShare).arg(expected)));
            checked++;
        }
        QCOMPARE(p.masked, p.validShare < 1.f);
        masked += p.masked ? 1 : 0;
    }
    QVERIFY2(masked >= 40, qPrintable(QString::number(masked)));
    QVERIFY(checked > masked);
}

void TestMaskedSubsets::the_outer_boundary_keeps_the_whole_subset()
{
    // Decision 1: cuts only. A point whose subset reaches past the outer
    // rectangle -- every point on its first row does -- keeps its whole subset.
    const CorrelationResult &result = defaultRun();
    int reachingOut = 0;
    for (const CorrelationPoint &p : result.points) {
        if (p.y - 16 < 9 || p.x - 16 < 6) {
            reachingOut++;
            QCOMPARE(p.validShare, 1.f);
            QVERIFY(!p.masked);
        }
    }
    QVERIFY2(reachingOut > 10, qPrintable(QString::number(reachingOut)));
}

void TestMaskedSubsets::an_island_put_back_inside_a_cut_is_ground_again()
{
    RegionOfInterest withIsland = specimenWithHole();
    // in the cut's right half, so the points beside its right edge gain ground
    withIsland.shapes << box(RegionShape::Ellipse, 124, 92, 138, 108, false);

    const CorrelationResult &plain = defaultRun();
    const CorrelationResult island = runOn(settings(), withIsland);

    // Matched by grid cell: the island is ground, so the grid lays points in it
    // too, and the two runs do not hold the same list.
    QHash<int, float> plainShare;
    for (const CorrelationPoint &p : plain.points)
        plainShare.insert(p.gridIndex, p.validShare);

    int gained = 0;
    for (const CorrelationPoint &p : island.points) {
        if (!plainShare.contains(p.gridIndex))
            continue;
        QVERIFY(p.validShare >= plainShare.value(p.gridIndex));
        if (p.validShare > plainShare.value(p.gridIndex)) {
            gained++;
            QVERIFY2(std::abs(double(p.validShare)
                              - expectedShare(withIsland, qRound(p.x), qRound(p.y), 16)) < 1e-6,
                     "the island counted differently from the region's own rule");
        }
    }
    QVERIFY2(gained > 0, "no point gained ground from the island");
}

void TestMaskedSubsets::below_the_floor_a_point_is_refused_and_says_why()
{
    CorrelationSettings strict = settings();
    strict.minValidShare = 0.8;
    const CorrelationResult result = runOn(strict, specimenWithHole());

    int refused = 0;
    QString reason;
    for (const CorrelationPoint &p : result.points) {
        if (!p.converged && p.validShare < 0.8f) {
            refused++;
            reason = p.failureReason;
        }
    }
    // nothing at or above the floor is refused for it
    for (const CorrelationPoint &p : result.points) {
        if (p.validShare >= 0.8f)
            QVERIFY(p.failureReason != reason);
    }
    QVERIFY2(refused > 10, qPrintable(QString::number(refused)));
    QVERIFY2(reason.contains(QStringLiteral("80 %")) && reason.contains(QStringLiteral("cut")),
             qPrintable(reason));
    QCOMPARE(result.failuresByReason.value(reason), refused);

    // At the default floor, the row beside an ordinary hole is kept.
    const CorrelationResult &ordinary = defaultRun();
    for (const CorrelationPoint &p : ordinary.points)
        QVERIFY2(p.converged || !p.masked, qPrintable(p.failureReason));
}

void TestMaskedSubsets::switching_masking_off_measures_as_before_and_still_counts_the_points()
{
    CorrelationSettings off = settings();
    off.maskAtCuts = false;
    const CorrelationResult result = runOn(off, specimenWithHole());

    QVERIFY(!result.maskingApplied);
    QCOMPARE(result.maskedPoints, 0);
    QVERIFY(result.maskingNote.isEmpty());
    for (const CorrelationPoint &p : result.points) {
        QCOMPARE(p.validShare, 1.f);
        QVERIFY(!p.masked);
    }
    QVERIFY2(result.subsetsReachingAHole > 40, qPrintable(QString::number(result.subsetsReachingAHole)));
}

void TestMaskedSubsets::a_region_without_cuts_is_measured_exactly_as_without_masking()
{
    RegionOfInterest noCut;
    noCut.shapes << box(RegionShape::Rectangle, 6, 9, 232, 188, false);
    CorrelationSettings off = settings();
    off.maskAtCuts = false;

    const CorrelationResult a = runOn(settings(), noCut);
    const CorrelationResult b = runOn(off, noCut);
    QVERIFY(!a.maskingApplied);
    QCOMPARE(a.points.size(), b.points.size());
    QVERIFY(!a.points.isEmpty());
    for (int i = 0; i < a.points.size(); i++) {
        QCOMPARE(a.points.at(i).u, b.points.at(i).u);
        QCOMPARE(a.points.at(i).v, b.points.at(i).v);
        QCOMPARE(a.points.at(i).zncc, b.points.at(i).zncc);
        QCOMPARE(a.points.at(i).noiseFloor, b.points.at(i).noiseFloor);
    }
}

void TestMaskedSubsets::the_run_reports_what_masking_did()
{
    const CorrelationResult &result = defaultRun();
    QVERIFY(result.maskingApplied);
    QCOMPARE(result.maskPullIn, kMaskPullIn);
    QCOMPARE(result.minValidShare, 0.25);
    QVERIFY(result.maskingNote.isEmpty());

    int maskedSolved = 0;
    for (const CorrelationPoint &p : result.points)
        maskedSolved += (p.masked && p.converged) ? 1 : 0;
    QCOMPARE(result.maskedPoints, maskedSolved);
    QVERIFY(maskedSolved > 40);
}

void TestMaskedSubsets::the_noise_floor_beside_a_cut_is_that_of_the_ground_it_was_measured_on()
{
    // Fewer pixels carry less gradient, so a point measured on part of its
    // subset can resolve less: about 1/sqrt(share) of the whole subset's floor.
    // Claiming the whole subset's would be claiming pixels that took no part.
    CorrelationSettings off = settings();
    off.maskAtCuts = false;
    const CorrelationResult plain = runOn(off, specimenWithHole());
    QHash<int, float> plainFloor;
    for (const CorrelationPoint &p : plain.points) {
        if (p.noiseFloorMeasured)
            plainFloor.insert(p.gridIndex, p.noiseFloor);
    }

    double ratioSum = 0.0;
    double expectedSum = 0.0;
    int n = 0;
    for (const CorrelationPoint &p : defaultRun().points) {
        if (!p.masked || !p.noiseFloorMeasured || p.validShare > 0.7f
            || !plainFloor.contains(p.gridIndex))
            continue;
        ratioSum += double(p.noiseFloor / plainFloor.value(p.gridIndex));
        expectedSum += 1.0 / std::sqrt(double(p.validShare));
        n++;
    }
    QVERIFY2(n >= 5, qPrintable(QString::number(n)));
    const double ratio = ratioSum / n;
    const double expected = expectedSum / n;
    QVERIFY2(ratio > 1.1 && std::abs(ratio / expected - 1.0) < 0.15,
             qPrintable(QStringLiteral("%1 times the whole subset's, about %2 expected")
                            .arg(ratio).arg(expected)));
}

void TestMaskedSubsets::a_solver_that_cannot_mask_refuses_points_beside_a_cut_and_names_one_that_can()
{
    CorrelationSettings iclm = settings();
    iclm.solver = CorrelationSettings::ICLM;
    const CorrelationResult result = runOn(iclm, specimenWithHole());

    int refused = 0;
    int measuredFar = 0;
    for (const CorrelationPoint &p : result.points) {
        if (p.masked) {
            QVERIFY(!p.converged);
            QVERIFY2(p.failureReason.contains(QStringLiteral("IC-LM"))
                         && p.failureReason.contains(QStringLiteral("ICGN")),
                     qPrintable(p.failureReason));
            refused++;
        } else if (p.converged) {
            measuredFar++;
        }
    }
    QVERIFY(refused > 40);
    QVERIFY(measuredFar > 400);
}

void TestMaskedSubsets::a_reference_the_cuts_were_not_drawn_on_is_not_masked_and_says_why()
{
    const CorrelationResult result = runOn(settings(), specimenWithHole(), false);
    QVERIFY(!result.maskingApplied);
    QCOMPARE(result.maskedPoints, 0);
    QVERIFY2(result.maskingNote.contains(QStringLiteral("re-anchor")),
             qPrintable(result.maskingNote));
    for (const CorrelationPoint &p : result.points)
        QVERIFY(!p.masked);
}

void TestMaskedSubsets::the_panel_note_says_what_masking_will_do_and_that_the_outer_edge_differs()
{
    CorrelationSettings s = settings();
    s.minValidShare = 0.3;
    const QString note = maskingPanelNote(s, specimenWithHole());
    QVERIFY2(note.contains(QStringLiteral("2 px")), qPrintable(note));
    QVERIFY2(note.contains(QStringLiteral("outer edge is not masked")), qPrintable(note));
    QVERIFY2(note.contains(QStringLiteral("30 %")), qPrintable(note));
}

void TestMaskedSubsets::the_panel_note_says_why_masking_would_do_nothing()
{
    RegionOfInterest noCut;
    noCut.shapes << box(RegionShape::Rectangle, 6, 9, 232, 188, false);
    QVERIFY2(maskingPanelNote(settings(), noCut).contains(QStringLiteral("no cuts")),
             qPrintable(maskingPanelNote(settings(), noCut)));
    QVERIFY(maskingPanelNote(settings(), RegionOfInterest()).contains(QStringLiteral("no cuts")));

    CorrelationSettings off = settings();
    off.maskAtCuts = false;
    QVERIFY(maskingPanelNote(off, specimenWithHole()).startsWith(QStringLiteral("Off")));

    CorrelationSettings nr = settings();
    nr.solver = CorrelationSettings::NewtonRaphson;
    const QString note = maskingPanelNote(nr, specimenWithHole());
    QVERIFY2(note.contains(QStringLiteral("Newton-Raphson")) && note.contains(QStringLiteral("refused")),
             qPrintable(note));
}

void TestMaskedSubsets::the_run_report_states_what_masking_did_or_how_many_points_it_would_have_saved()
{
    CorrelationResult applied;
    applied.maskingApplied = true;
    applied.maskPullIn = 2;
    applied.maskedPoints = 2;
    CorrelationPoint a;
    a.masked = true;
    a.converged = true;
    a.validShare = 0.71f;
    CorrelationPoint b = a;
    b.validShare = 0.43f;
    CorrelationPoint refused = a;   // refused, so not the least share KEPT
    refused.converged = false;
    refused.validShare = 0.1f;
    applied.points << a << b << refused;
    const QString line = maskingReport(applied).join(QLatin1Char(' '));
    QVERIFY2(line.contains(QStringLiteral("2 measured point(s)")) && line.contains(QStringLiteral("43 %"))
                 && line.contains(QStringLiteral("outer edge")),
             qPrintable(line));

    CorrelationResult off;
    off.subsetsReachingAHole = 17;
    const QString offLine = maskingReport(off).join(QLatin1Char(' '));
    QVERIFY2(offLine.contains(QStringLiteral("17")) && offLine.contains(maskAtCutsLabel()),
             qPrintable(offLine));

    QVERIFY(maskingReport(CorrelationResult()).isEmpty());
}

QTEST_GUILESS_MAIN(TestMaskedSubsets)
#include "test_masked_subsets.moc"
