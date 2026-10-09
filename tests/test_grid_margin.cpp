// The grid's edge margin, held against the engine that decides it.
//
// WHY THIS EXISTS. ROADMAP decision 1, chosen 2026-10-09: inset the grid so
// no point is laid where the engine can never measure it. The margin is
// copied from the engine's bicubic interpolator, which refuses any sample at
// x < 1, y < 1, x >= width - 2 or y >= height - 2, and a copied number
// drifts silently when its source changes. So this asks the engine itself.
//
// ⚑ THE INSET IS r + 2 AT THE START, NOT THE r + 1 FIRST WRITTEN DOWN. Found
// by the first version of this case: at r + 1 a point's outermost sample sits
// exactly on the interpolator's limit, with no room at all, and the top point
// failed on a frame that does not move in y, because the solver's own iterate
// wandered a few thousandths of a pixel negative on the way to v = 0. At the
// far end width - 3 - r already left most of a pixel. r + 2 gives every edge
// room for sub-pixel movement either way.
//
// The fixture is the shipped translation set, rendered analytically, so the
// shift is exact: translation_00 against translation_02 is +0.5 px in x and 0
// in y, and the same pair swapped is -0.5 px.
//
// The second half of the decision: where the specimen's movement takes a
// subset past the edge, the reader is told so in place of the engine's
// "subset out of image bounds, or invalid initial guess".
//
// Written red first (2026-10-09). NEGATIVE CHECK, four mutations, each caught:
// the wording never wired into the run (the readout case); the left-hand
// movement's sign swapped and the slack widened to two pixels (both by
// `a_point_with_room_to_spare...`, which against a stub that never blames
// passes either way); the bottom edge reading u instead of v (`...says_so`).

#include "core/Correlation.h"
#include "core/PoiGrid.h"

#include <QTest>

#include <cmath>

namespace {

QString example(const QString &name)
{
    return QStringLiteral(SURVIEW_EXAMPLES "/synthetic/") + name;
}

constexpr int kWidth = 640, kHeight = 480, kRadius = 16;
constexpr int kFirst = kRadius + 2;                 // the inset, stated outright
constexpr int kLastX = kWidth - 3 - kRadius;
constexpr int kLastY = kHeight - 3 - kRadius;

enum class Expect { Solves, Fails };

struct Probe
{
    const char *where;
    int x, y;
    Expect forward;    // specimen moving +0.5 px in x
    Expect backward;   // specimen moving -0.5 px in x
};

const Probe kProbes[] = {
    {"left, at the inset", kFirst, 240, Expect::Solves, Expect::Solves},
    {"right, at the inset", kLastX, 240, Expect::Solves, Expect::Solves},
    {"top, at the inset", 320, kFirst, Expect::Solves, Expect::Solves},
    {"bottom, at the inset", 320, kLastY, Expect::Solves, Expect::Solves},
    // One pixel further out: the engine's own limits, which is what the inset
    // was copied from. On the left the room is spent the moment the specimen
    // moves toward the edge. On the right there is none to spend: the
    // reference subset itself reaches width - 2 and is refused before any
    // movement, whichever way the specimen goes.
    {"left, one pixel out", kFirst - 1, 240, Expect::Solves, Expect::Fails},
    {"right, one pixel out", kLastX + 1, 240, Expect::Fails, Expect::Fails},
};

QStringList measure(const QString &reference, const QString &target, bool forward,
                    QStringList *reasons = nullptr)
{
    PoiSeeding seeding;
    int index = 0;
    for (const Probe &probe : kProbes) {
        PoiSeeding::Seed seed;
        seed.gridIndex = index++;
        seed.x = float(probe.x);
        seed.y = float(probe.y);
        seeding.points.append(seed);
    }
    seeding.gridColumns = index;
    seeding.gridRows = 1;

    CorrelationSettings settings;
    settings.subsetRadius = kRadius;
    settings.strainEnabled = false;
    settings.recovery.enabled = false;   // a second pass would only obscure this
    CorrelationRunner runner(settings, RegionOfInterest(), reference, target);
    runner.setSeeding(seeding);

    CorrelationResult result;
    QString failure;
    QObject::connect(&runner, &CorrelationRunner::finished,
                     [&result](const CorrelationResult &r) { result = r; });
    QObject::connect(&runner, &CorrelationRunner::failed,
                     [&failure](const QString &reason) { failure = reason; });
    runner.run();
    if (!failure.isEmpty())
        return {failure};

    // Every probe reported, not only the first wrong one: which edges agree
    // and which do not is the whole diagnosis.
    const float shift = forward ? 0.5f : -0.5f;
    if (reasons) {
        for (const CorrelationPoint &point : result.points)
            *reasons << point.failureReason;
    }
    QStringList wrong;
    for (const CorrelationPoint &point : result.points) {
        const Probe &probe = kProbes[point.gridIndex];
        const bool solved = point.converged && std::isfinite(point.u);
        const Expect expected = forward ? probe.forward : probe.backward;
        const QString line =
            QStringLiteral("%1, moving %2 px (%3, %4): %5, u %6, v %7")
                .arg(QLatin1String(probe.where)).arg(shift)
                .arg(probe.x).arg(probe.y)
                .arg(solved ? QStringLiteral("solved")
                            : QStringLiteral("not solved (%1)").arg(point.failureReason))
                .arg(point.u).arg(point.v);
        qInfo("%s", qPrintable(line));
        if (solved != (expected == Expect::Solves))
            wrong << line;
        else if (solved && std::abs(point.u - shift) >= 0.02f)
            wrong << line + QStringLiteral(" -- solved, but not at the stated shift");
    }
    return wrong;
}

}  // namespace

class TestGridMargin : public QObject
{
    Q_OBJECT

private slots:
    void the_grid_lays_its_edge_points_at_the_inset();
    void a_point_at_the_inset_solves_moving_either_way();
    void a_point_the_movement_carried_off_the_edge_says_so();
    void a_point_with_room_to_spare_does_not_blame_the_specimen();
    void the_specimen_is_named_in_the_readout_where_it_did_this();
};

void TestGridMargin::the_grid_lays_its_edge_points_at_the_inset()
{
    const PoiGridExtent extent =
        poiGridExtent(kWidth, kHeight, kRadius, 1, RegionOfInterest());
    QVERIFY(extent.valid);
    QCOMPARE(extent.firstX, kFirst);
    QCOMPARE(extent.firstY, kFirst);
    QCOMPARE(extent.lastX, kLastX);
    QCOMPARE(extent.lastY, kLastY);
}

void TestGridMargin::a_point_at_the_inset_solves_moving_either_way()
{
    const QString still = example(QStringLiteral("translation_00.tif"));
    const QString moved = example(QStringLiteral("translation_02.tif"));
    QStringList wrong = measure(still, moved, true);
    wrong += measure(moved, still, false);
    QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join(QStringLiteral("\n"))));
}

void TestGridMargin::a_point_the_movement_carried_off_the_edge_says_so()
{
    // ROADMAP decision 1's second half, chosen with it. The engine reports a
    // subset out of bounds and an invalid initial guess under one code, which
    // reads to anyone looking at the hole as a fault in their photograph.
    // Near an edge, with the specimen moving toward it, it is neither: the
    // specimen took the subset off the image. Lopsided per side, so a swapped
    // axis or sign cannot pass: room 0 px on the left, moving left.
    QVERIFY(subsetCarriedPastTheEdge(17, 240, 16, -0.4, 0.0, 640, 480));    // left
    QVERIFY(subsetCarriedPastTheEdge(621, 240, 16, 1.2, 0.0, 640, 480));    // right
    QVERIFY(subsetCarriedPastTheEdge(320, 17, 16, 0.0, -0.3, 640, 480));    // top
    QVERIFY(subsetCarriedPastTheEdge(320, 461, 16, 0.0, 1.5, 640, 480));    // bottom
    // An integer first estimate of zero still counts at zero room: the solve
    // itself moves the subset by the fraction the estimate rounded away.
    QVERIFY(subsetCarriedPastTheEdge(17, 240, 16, 0.0, 0.0, 640, 480));
}

void TestGridMargin::a_point_with_room_to_spare_does_not_blame_the_specimen()
{
    // In the middle of the image there is nowhere to be carried to: a refusal
    // there came from a nonsense estimate, and blaming the specimen would be a
    // false account of it.
    QVERIFY(!subsetCarriedPastTheEdge(320, 240, 16, -0.5, 0.5, 640, 480));
    // Near the left edge but moving AWAY from it.
    QVERIFY(!subsetCarriedPastTheEdge(20, 240, 16, 3.0, 0.0, 640, 480));
    // At the inset, not moving: one pixel of room, which is the slack.
    QVERIFY(!subsetCarriedPastTheEdge(18, 240, 16, 0.0, 0.0, 640, 480));
    // An estimate that is not a number says nothing about any movement.
    QVERIFY(!subsetCarriedPastTheEdge(17, 240, 16, std::nan(""), 0.0, 640, 480));
}

void TestGridMargin::the_specimen_is_named_in_the_readout_where_it_did_this()
{
    // Through the engine: the point one pixel outside the inset on the left,
    // with the specimen moving left, fails -- and says the specimen did it.
    const QString still = example(QStringLiteral("translation_00.tif"));
    const QString moved = example(QStringLiteral("translation_02.tif"));
    QStringList reasons;
    measure(moved, still, false, &reasons);
    const int leftOut = 4;   // "left, one pixel out" in kProbes
    QVERIFY2(reasons.value(leftOut).contains(kCarriedPastTheEdge),
             qPrintable(reasons.value(leftOut)));
}

QTEST_GUILESS_MAIN(TestGridMargin)
#include "test_grid_margin.moc"
