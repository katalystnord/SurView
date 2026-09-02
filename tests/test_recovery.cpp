// A second pass at the points the first solve could not measure well.
//
// WHY THIS EXISTS. Most points a DIC solve loses are not unmeasurable places on
// the specimen; they are places where the initial guess was poor. The engine
// carries the remedy in RegionFit2D, which fits an affine displacement field to
// the reliable points around a bad one. Reliability-guided DIC -- Ncorr's and
// VIC-2D's whole algorithm -- uses exactly that signal from the very first
// point. Our first pass initialises every point independently and throws the
// signal away, so this pass is where we pick it back up.
//
// ⚑ THE RULE THE WHOLE FILE TURNS ON: A FITTED POINT IS NOT A MEASURED POINT.
// The fit borrows an answer from the neighbours, and the engine says so by
// resetting that point's correlation to zero. So the fit is an INITIAL GUESS
// that ICGN then re-solves, and what is reported is a real correlation measured
// at that point. Using the fitted value directly would put an interpolation
// into the field wearing the same colour as a measurement -- the trap already
// ruled out for unmeasured points at export, and for strain fitted onto
// rejected points.
//
// ⚑ THIS FILE IS ENGINE-FREE ON PURPOSE, for the reason core/ReferenceUpdate.h
// is: the bookkeeping is what goes wrong, it is pure arithmetic over points,
// and inside the solve loop it could only be exercised through a real
// correlation. What is tested here is which points are attempted, which seeds
// are trusted, which answers are accepted, and when the pass stops.
//
// Three properties are load-bearing, and each has a case below:
//
//   1. A recovery pass may never make a point WORSE. It re-attempts points that
//      may already hold a number, so an answer is accepted only if it converged
//      AND correlates better than the one it replaces.
//   2. A recovered point is MARKED. It was genuinely re-solved and needs no
//      apology, but it reached its answer by a different route, and this code
//      base marks that everywhere else.
//   3. Recovery spreads. A point recovered in one round becomes a seed for the
//      next, which is how the middle of a large failed patch is ever reached --
//      it has no reliable neighbour at all on the first round.
//
// NEGATIVE CHECK (2026-09-02). Each rule was removed from core/Recovery.cpp in
// turn and the suite re-run. Every one turned exactly one case red:
//
//   never-worse guard removed        -> a_worse_answer_is_never_accepted
//   `recovered` mark not set         -> a_recovered_point_is_marked_as_one
//   position taken from the answer   -> recovery_never_moves_a_point_...
//   threshold compared in double     -> only_confidently_correlated_points_...
//   round cap ignored                -> the_pass_stops_at_the_round_cap_...
//   no-seeds guard removed           -> a_pass_with_no_seeds_at_all_cannot_run
//   convergence ignored when seeding -> a_point_that_did_not_converge_never_...
//
// ⚑ WHAT IT DID NOT CATCH, AND WHY THERE IS AN EXTRA CASE. The last of those
// was green at first. a_rejected_point_never_seeds_the_fit passes whether or
// not the code asks about convergence, because a rejected point's status code
// is negative and so compares below any positive threshold: the correlation
// test alone gives the right answer, by accident. The accident stops holding
// for a point that reports a high correlation at the iteration it gave up on,
// which is what a_point_that_did_not_converge_never_seeds_however_high_its_
// correlation builds, and which is what turns the check red.

#include "core/Correlation.h"
#include "core/Recovery.h"

#include <QTest>

namespace {

CorrelationPoint at(int gridIndex, float x, float y, float zncc, bool converged)
{
    CorrelationPoint point;
    point.gridIndex = gridIndex;
    point.x = x;
    point.y = y;
    point.zncc = zncc;
    point.converged = converged;
    return point;
}

CorrelationPoint solved(int gridIndex, float zncc)
{
    return at(gridIndex, float(gridIndex), 0.f, zncc, true);
}

// A point the engine rejected. Its zncc is a negative status code, not a
// correlation -- which is why nothing below compares it as though it were one.
CorrelationPoint rejected(int gridIndex)
{
    CorrelationPoint point = at(gridIndex, float(gridIndex), 0.f, -3.f, false);
    point.failureReason = QStringLiteral("did not converge");
    return point;
}

CorrelationResult fieldOf(std::initializer_list<CorrelationPoint> points)
{
    CorrelationResult result;
    for (const CorrelationPoint &point : points)
        result.points.append(point);
    result.gridColumns = int(result.points.size());
    result.gridRows = 1;
    result.step = 5;
    return result;
}

}  // namespace

class TestRecovery : public QObject
{
    Q_OBJECT

private slots:
    // which points the pass touches
    void a_point_the_solver_rejected_is_attempted();
    void a_point_that_converged_but_correlates_poorly_is_attempted_too();
    void a_well_correlated_point_is_never_attempted();
    void a_disabled_pass_attempts_nothing();
    void the_defaults_are_permissive_about_what_is_tried_and_strict_about_what_is_kept();

    // which points the pass trusts to seed a fit
    void only_confidently_correlated_points_seed_the_fit();
    void a_rejected_point_never_seeds_the_fit();
    void a_point_that_did_not_converge_never_seeds_however_high_its_correlation();
    void a_pass_with_no_seeds_at_all_cannot_run();

    // which answers come back
    void a_better_answer_replaces_the_one_it_improves_on();
    void a_recovered_point_is_marked_as_one();
    void a_worse_answer_is_never_accepted();
    void an_unconverged_answer_is_never_accepted_however_high_its_correlation();
    void a_fitted_value_that_was_never_re_solved_is_never_accepted();
    void a_point_that_stays_failed_keeps_its_failure_and_is_not_marked_recovered();
    void recovery_never_moves_a_point_or_changes_which_cell_it_reports_to();

    // when the pass stops
    void a_recovered_point_seeds_the_next_round();
    void a_round_that_recovers_nothing_ends_the_pass();
    void the_pass_stops_at_the_round_cap_even_while_it_is_still_gaining();

    // the neighbourhood the fit searches
    void the_neighbourhood_is_derived_from_the_grid_step();
    void an_overridden_neighbourhood_is_used_instead_of_the_derived_one();
    void the_derivation_says_on_screen_where_its_numbers_came_from();
};

// --- which points the pass touches ------------------------------------------

void TestRecovery::a_point_the_solver_rejected_is_attempted()
{
    const CorrelationResult field = fieldOf({solved(0, 0.99f), rejected(1)});

    QCOMPARE(pointsNeedingRecovery(field, RecoveryPolicy{}), QVector<int>{1});
}

void TestRecovery::a_point_that_converged_but_correlates_poorly_is_attempted_too()
{
    // The scope decision, and the reason it is not "failed points only": a
    // point that converged at 0.30 produced a number, and that number is very
    // probably wrong. Leaving it alone would leave the worst measurements in
    // the field untouched while repairing the ones that at least had the grace
    // to admit failure.
    RecoveryPolicy policy;
    policy.retryBelowZncc = 0.5;

    const CorrelationResult field =
        fieldOf({solved(0, 0.99f), solved(1, 0.30f), solved(2, 0.49f)});

    QCOMPARE(pointsNeedingRecovery(field, policy), (QVector<int>{1, 2}));
}

void TestRecovery::a_well_correlated_point_is_never_attempted()
{
    // What makes this pass safe to run by default: a point that already
    // measured well is not in the queue at all, so no run can be made worse by
    // switching the pass on.
    RecoveryPolicy policy;
    policy.retryBelowZncc = 0.5;

    const CorrelationResult field =
        fieldOf({solved(0, 0.50f), solved(1, 0.90f), solved(2, 1.00f)});

    QVERIFY(pointsNeedingRecovery(field, policy).isEmpty());
}

void TestRecovery::a_disabled_pass_attempts_nothing()
{
    RecoveryPolicy policy;
    policy.enabled = false;

    const CorrelationResult field = fieldOf({solved(0, 0.99f), rejected(1)});

    QVERIFY(pointsNeedingRecovery(field, policy).isEmpty());
}

void TestRecovery::the_defaults_are_permissive_about_what_is_tried_and_strict_about_what_is_kept()
{
    // ⚑ THE POSTURE, set by David 2026-09-02: FORGIVING DIC. Be permissive
    // about what is attempted and what is kept, strict about what is claimed.
    //
    // Those are not in tension -- the second is what pays for the first. A pass
    // may re-attempt almost anything and keep almost anything it improves,
    // because every point it hands back still carries its own correlation, its
    // own noise floor and the mark saying it was recovered. A user gets a fuller
    // field AND is told exactly which parts of it were hard won. Strictness in
    // reporting is what funds generosity in measuring, and a permissive pass
    // without it would just be a field that lies more confidently.
    //
    // So the defaults below are wider than the engine's own worked example
    // (which seeds at 0.9 and retries below 0.5). What is NOT relaxed is the
    // acceptance rule: see a_worse_answer_is_never_accepted.
    const RecoveryPolicy fresh;

    QVERIFY2(fresh.enabled,
             "the pass runs by default: it cannot make a solved point worse, "
             "and neighbour-guided initialisation is the norm of the field "
             "rather than an extra");
    QVERIFY2(fresh.retryBelowZncc > 0.5,
             "permissive: retry more than the engine's own example does");
    QVERIFY2(fresh.reliableZncc < 0.9,
             "permissive: let more points seed a fit than the engine's own "
             "example trusts");
    QVERIFY2(fresh.reliableZncc > fresh.retryBelowZncc,
             "a point must never be both trusted as a seed and queued for "
             "retry -- it would be evidence for its own replacement");
    QVERIFY2(fresh.maxRounds > 1,
             "recovery has to spread to reach the middle of a failed patch");
}

// --- which points the pass trusts to seed a fit -----------------------------

void TestRecovery::only_confidently_correlated_points_seed_the_fit()
{
    // Two thresholds, not one, and the gap between them matters. A point at
    // 0.70 is too poor to trust as evidence of where its neighbours went, and
    // too good to be worth re-attempting. It does neither job.
    RecoveryPolicy policy;
    policy.reliableZncc = 0.9;
    policy.retryBelowZncc = 0.5;

    const CorrelationResult field = fieldOf(
        {solved(0, 0.95f), solved(1, 0.70f), solved(2, 0.90f), solved(3, 0.30f)});

    QCOMPARE(recoverySeeds(field, policy), (QVector<int>{0, 2}));
}

void TestRecovery::a_rejected_point_never_seeds_the_fit()
{
    // ⚑ A rejected point's zncc is a negative STATUS CODE, not a correlation.
    // Compared numerically against a threshold it simply reads as very poor,
    // which happens to give the right answer here -- but only by accident, and
    // the accident stops holding the moment a status code is chosen that is
    // positive. The convergence flag is the question actually asked.
    const CorrelationResult field = fieldOf({solved(0, 0.99f), rejected(1)});

    QCOMPARE(recoverySeeds(field, RecoveryPolicy{}), QVector<int>{0});
}

void TestRecovery::a_point_that_did_not_converge_never_seeds_however_high_its_correlation()
{
    // ⚑ THIS CASE EXISTS BECAUSE THE ONE ABOVE CANNOT CATCH THE BUG IT NAMES.
    // Negative check, 2026-09-02: deleting the convergence test from isSeed()
    // left a_rejected_point_never_seeds_the_fit GREEN, because a rejected
    // point's status code is negative and compares below any positive
    // threshold, so the correlation test alone happens to give the right
    // answer. It stops giving it the moment a status code is positive, or a
    // solver reports a high correlation at the iteration it gave up on -- which
    // is what this case builds, and what turns the check red.
    CorrelationResult field = fieldOf({solved(0, 0.99f)});
    field.points.append(at(1, 1.f, 0.f, 0.98f, false));

    QCOMPARE(recoverySeeds(field, RecoveryPolicy{}), QVector<int>{0});
}

void TestRecovery::a_pass_with_no_seeds_at_all_cannot_run()
{
    // A field where nothing correlated has nothing to fit from. The pass must
    // decline rather than fit an affine field to an empty point cloud, which is
    // the same shape of guard as "a frame where nothing correlated must not
    // re-anchor".
    const CorrelationResult field = fieldOf({rejected(0), rejected(1)});

    QVERIFY(!recoveryCanRun(field, RecoveryPolicy{}));
}

// --- which answers come back ------------------------------------------------

void TestRecovery::a_better_answer_replaces_the_one_it_improves_on()
{
    CorrelationResult field = fieldOf({solved(0, 0.99f), rejected(1)});

    CorrelationPoint recovered = solved(1, 0.97f);
    recovered.u = 4.f;

    const QVector<int> accepted = acceptRecoveryRound(field, {1}, {recovered});

    QCOMPARE(accepted, QVector<int>{1});
    QCOMPARE(field.points[1].zncc, 0.97f);
    QCOMPARE(field.points[1].u, 4.f);
    QVERIFY(field.points[1].converged);
}

void TestRecovery::a_recovered_point_is_marked_as_one()
{
    // David's call, 2026-09-02. The measurement is real and needs no apology,
    // but it reached its answer by a different route, and that is marked here
    // exactly as rowsReversedByDecoder, strainFitted and solved are elsewhere.
    CorrelationResult field = fieldOf({solved(0, 0.99f), rejected(1)});

    acceptRecoveryRound(field, {1}, {solved(1, 0.97f)});

    QVERIFY(field.points[1].recovered);
    QVERIFY2(!field.points[0].recovered,
             "a point the pass never touched must not be marked recovered");
}

void TestRecovery::a_worse_answer_is_never_accepted()
{
    // ⚑ A RECOVERY PASS MAY NEVER MAKE A POINT WORSE. Once the pass re-attempts
    // points that already hold a number, every round is a chance to replace a
    // good answer with a bad one -- and the bad one would arrive wearing the
    // `recovered` mark, which reads as an improvement.
    CorrelationResult field = fieldOf({solved(0, 0.99f), solved(1, 0.45f)});

    const QVector<int> accepted = acceptRecoveryRound(field, {1}, {solved(1, 0.20f)});

    QVERIFY(accepted.isEmpty());
    QCOMPARE(field.points[1].zncc, 0.45f);
    QVERIFY(!field.points[1].recovered);
}

void TestRecovery::an_unconverged_answer_is_never_accepted_however_high_its_correlation()
{
    // A solver that ran out of iterations can still be sitting on a high
    // correlation at the moment it gives up. That is not a measurement, and the
    // convergence flag is the only thing that says so.
    CorrelationResult field = fieldOf({solved(0, 0.99f), solved(1, 0.45f)});

    const QVector<int> accepted =
        acceptRecoveryRound(field, {1}, {at(1, 1.f, 0.f, 0.99f, false)});

    QVERIFY(accepted.isEmpty());
    QCOMPARE(field.points[1].zncc, 0.45f);
}

void TestRecovery::a_fitted_value_that_was_never_re_solved_is_never_accepted()
{
    // ⚑ THE TRAP THE WHOLE PASS IS BUILT TO AVOID, and it very nearly went in.
    // RegionFit2D marks a point whose displacement it FITTED by resetting that
    // point's correlation to zero -- "borrowed, not measured", in the engine's
    // own terms. A zero is therefore the absence of a correlation, not a poor
    // one, and a rule that merely asked "did it converge" let every fitted
    // value through as a measurement.
    //
    // Found by a negative check that deleted the re-solve after the fit. The
    // accuracy suite did NOT catch it: on a smoothly rotating synthetic field
    // the affine fit is itself accurate to a tenth of a pixel, so displacement
    // error cannot tell an interpolation from a measurement. The correlation
    // can, because the engine put the answer there on purpose.
    CorrelationResult field = fieldOf({solved(0, 0.99f), rejected(1)});

    CorrelationPoint fittedOnly = at(1, 1.f, 0.f, 0.f, true);
    fittedOnly.u = 4.f;   // a perfectly plausible displacement, and borrowed

    QVERIFY(acceptRecoveryRound(field, {1}, {fittedOnly}).isEmpty());
    QVERIFY(!field.points[1].converged);
    QVERIFY(!field.points[1].recovered);
}

void TestRecovery::a_point_that_stays_failed_keeps_its_failure_and_is_not_marked_recovered()
{
    // A point that still fails stays failed, and says why. The pass adds
    // points; it never quietly launders one.
    CorrelationResult field = fieldOf({solved(0, 0.99f), rejected(1)});

    acceptRecoveryRound(field, {1}, {rejected(1)});

    QVERIFY(!field.points[1].converged);
    QVERIFY(!field.points[1].recovered);
    QCOMPARE(field.points[1].failureReason, QStringLiteral("did not converge"));
}

void TestRecovery::recovery_never_moves_a_point_or_changes_which_cell_it_reports_to()
{
    // The property that keeps a recovered field laid over the same photograph
    // as the field before it. A recovered point is the SAME point, measured
    // again -- not a new one somewhere better.
    CorrelationResult field = fieldOf({solved(0, 0.99f), rejected(1)});
    field.points[1].x = 40.f;
    field.points[1].y = 25.f;
    field.points[1].gridIndex = 7;

    CorrelationPoint recovered = solved(1, 0.97f);
    recovered.x = 99.f;          // as if the fit had wandered
    recovered.y = 99.f;
    recovered.gridIndex = 99;

    acceptRecoveryRound(field, {1}, {recovered});

    QCOMPARE(field.points[1].x, 40.f);
    QCOMPARE(field.points[1].y, 25.f);
    QCOMPARE(field.points[1].gridIndex, 7);
}

// --- when the pass stops ----------------------------------------------------

void TestRecovery::a_recovered_point_seeds_the_next_round()
{
    // ⚑ WHY THE PASS RUNS IN ROUNDS AT ALL. The interior of a large failed
    // patch has no reliable neighbour within any sensible radius on the first
    // round, so a single pass cannot reach it however good the fit is. Each
    // round's recoveries become the next round's seeds and the repair spreads
    // inward from good ground -- which is reliability-guided propagation,
    // arrived at from the other end.
    RecoveryPolicy policy;
    policy.reliableZncc = 0.9;

    CorrelationResult field =
        fieldOf({solved(0, 0.99f), rejected(1), rejected(2)});

    QCOMPARE(recoverySeeds(field, policy), QVector<int>{0});

    acceptRecoveryRound(field, {1}, {solved(1, 0.95f)});

    QCOMPARE(recoverySeeds(field, policy), (QVector<int>{0, 1}));
}

void TestRecovery::a_round_that_recovers_nothing_ends_the_pass()
{
    QVERIFY(!recoveryShouldContinue(1, 0, RecoveryPolicy{}));
    QVERIFY(recoveryShouldContinue(1, 3, RecoveryPolicy{}));
}

void TestRecovery::the_pass_stops_at_the_round_cap_even_while_it_is_still_gaining()
{
    // Without a cap, a pass that recovers one point per round on a large field
    // runs for as many rounds as there are points, and the Stop button becomes
    // the only way out of a run that is technically progressing.
    RecoveryPolicy policy;
    policy.maxRounds = 3;

    QVERIFY(recoveryShouldContinue(2, 1, policy));
    QVERIFY(!recoveryShouldContinue(3, 1, policy));
}

// --- the neighbourhood the fit searches -------------------------------------

void TestRecovery::the_neighbourhood_is_derived_from_the_grid_step()
{
    // ⚑ DERIVED, NOT INVENTED. Both numbers come from the engine's own worked
    // example (examples/test_3d_reconstruction_sift_icgn2_regfit.cpp), which
    // searches 12 px on a 3 px grid with a floor of 9 neighbours -- four grid
    // steps, and nine. Two spin boxes defaulting to numbers nobody chose would
    // be two more figures a user has no basis to set, which is the reasoning
    // already recorded for not colouring the noise floor against an invented
    // threshold.
    const RecoveryNeighbourhood derived = deriveRecoveryNeighbourhood(3);

    QCOMPARE(derived.searchRadius, 12.f);
    QCOMPARE(derived.minNeighbours, 9);

    QCOMPARE(deriveRecoveryNeighbourhood(5).searchRadius, 20.f);
}

void TestRecovery::an_overridden_neighbourhood_is_used_instead_of_the_derived_one()
{
    // Derived by default and selectable, per David 2026-09-02: the derivation
    // is what a user gets without choosing, and choosing is still possible.
    RecoveryPolicy policy;
    policy.searchRadius = 30.f;
    policy.minNeighbours = 20;

    const RecoveryNeighbourhood used = recoveryNeighbourhood(policy, 3);

    QCOMPARE(used.searchRadius, 30.f);
    QCOMPARE(used.minNeighbours, 20);

    // and an unset override falls back to the derivation rather than to zero,
    // which would search a radius of nothing and find no neighbours at all.
    QCOMPARE(recoveryNeighbourhood(RecoveryPolicy{}, 3).searchRadius, 12.f);
}

void TestRecovery::the_derivation_says_on_screen_where_its_numbers_came_from()
{
    // A derived number a user cannot see the basis of is a magic number with
    // extra steps. The panel states it, so this asserts the sentence exists and
    // carries the figures it claims to explain.
    const QString said = recoveryNeighbourhoodDerivation(3);

    QVERIFY(!said.isEmpty());
    QVERIFY2(said.contains(QStringLiteral("12")),
             qPrintable(QStringLiteral("derivation must state the radius it "
                                       "produced, said: ") + said));
    QVERIFY2(said.contains(QStringLiteral("9")),
             qPrintable(QStringLiteral("derivation must state the neighbour "
                                       "floor, said: ") + said));
}

QTEST_MAIN(TestRecovery)
#include "test_recovery.moc"
