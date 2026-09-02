#pragma once

#include <QString>
#include <QVector>

struct CorrelationPoint;
struct CorrelationResult;

// A second pass at the points the first solve could not measure well.
//
// Most points a DIC solve loses are not unmeasurable places on the specimen;
// they are places where the initial guess was poor. The engine carries the
// remedy in RegionFit2D, which fits an affine displacement field to the
// reliable points around a bad one and uses it as a starting point.
//
// Reliability-guided DIC -- the whole algorithm in Ncorr and VIC-2D -- exploits
// exactly that signal from the very first point: one seed gets a global initial
// guess, and every other subset is initialised from a converged neighbour. Our
// first pass initialises every point independently and throws the signal away,
// so this pass is where we pick it back up. That is why it is on by default:
// not an extra step bolted onto a finished run, but the field's ordinary
// practice, applied late.
//
// ⚑ A FITTED POINT IS NOT A MEASURED POINT. The fit borrows an answer from the
// neighbours, and the engine says so by resetting that point's correlation to
// zero. So the fit is only an INITIAL GUESS, which ICGN then re-solves, and what
// is reported is a real correlation measured at that point. Using the fitted
// value directly would put an interpolation into the field wearing the same
// colour as a measurement -- the trap already ruled out for unmeasured points at
// export, and for strain fitted onto rejected points.
//
// ⚑ ENGINE-FREE ON PURPOSE, for the reason core/ReferenceUpdate.h is: the
// bookkeeping is what goes wrong, it is pure arithmetic over points, and inside
// the solve loop it could only be exercised through a real correlation.

// How forgiving the pass is.
//
// ⚑ THE POSTURE IS FORGIVING (David, 2026-09-02): permissive about what is
// attempted and what is kept, strict about what is claimed. The two are not in
// tension -- the second is what pays for the first. Every point handed back
// still carries its own correlation, its own noise floor and the mark saying it
// was recovered, so a fuller field costs a reader nothing in knowing which
// parts of it were hard won.
//
// The defaults are wider than the engine's own worked example, which seeds at
// 0.9 and retries below 0.5. What is deliberately NOT relaxed is the acceptance
// rule in recoveryImproves(): a pass that may make a point worse is not
// forgiving, it is just noisier.
struct RecoveryPolicy
{
    bool enabled = true;

    // At or above this correlation a point is trusted to say where its
    // neighbours went. Lower than the engine's example, so more of the field
    // can seed a fit.
    double reliableZncc = 0.8;

    // Below this correlation a point is re-attempted, whether or not it
    // converged. Higher than the engine's example, so more mediocre points get
    // another go -- which is close to free, because a worse answer is discarded.
    double retryBelowZncc = 0.7;

    // Rounds are what let recovery spread inward from good ground; the cap is
    // what stops a run that gains one point per round from taking as many
    // rounds as there are points.
    int maxRounds = 20;

    // 0 means derive from the grid step. Set to override, which the Analysis
    // panel allows and states.
    float searchRadius = 0.f;
    int minNeighbours = 0;
};

// The neighbourhood RegionFit2D searches around a point it is repairing.
struct RecoveryNeighbourhood
{
    float searchRadius = 0.f;   // px
    int minNeighbours = 0;      // fewest reliable points the fit will accept
};

// ⚑ DERIVED, NOT INVENTED. Both figures come from the engine's own worked
// example (examples/test_3d_reconstruction_sift_icgn2_regfit.cpp), which
// searches 12 px on a 3 px grid with a floor of 9 neighbours: four grid steps,
// and nine. Two spin boxes defaulting to numbers nobody chose would be two more
// figures a user has no basis to set, which is the reasoning already recorded
// for not colouring the noise floor against an invented threshold.
RecoveryNeighbourhood deriveRecoveryNeighbourhood(int gridStep);

// The neighbourhood a run will actually use: the policy's override where it set
// one, the derivation otherwise. An unset override falls back to the derivation
// rather than to zero, which would search a radius of nothing.
RecoveryNeighbourhood recoveryNeighbourhood(const RecoveryPolicy &policy,
                                            int gridStep);

// Where the derived figures came from, in a sentence the panel shows. A derived
// number a user cannot see the basis of is a magic number with extra steps.
QString recoveryNeighbourhoodDerivation(int gridStep);

// Points to re-attempt, as indices into the field's own point list. Empty when
// the policy is off.
QVector<int> pointsNeedingRecovery(const CorrelationResult &field,
                                   const RecoveryPolicy &policy);

// Points trusted to seed a fit, as indices into the field's own point list.
//
// ⚑ Asks the convergence flag, never the correlation alone. A rejected point's
// zncc is a negative STATUS CODE rather than a correlation, and comparing it
// against a threshold happens to give the right answer only for as long as
// every status code stays negative.
QVector<int> recoverySeeds(const CorrelationResult &field,
                           const RecoveryPolicy &policy);

// Whether there is anything to fit from. A field where nothing correlated has
// no evidence to borrow, and must decline rather than fit an affine field to an
// empty point cloud -- the same shape of guard as "a frame where nothing
// correlated must not re-anchor".
bool recoveryCanRun(const CorrelationResult &field,
                    const RecoveryPolicy &policy);

// Whether a re-solved answer is better than the one it would replace.
//
// ⚑ A RECOVERY PASS MAY NEVER MAKE A POINT WORSE. Once the pass re-attempts
// points that already hold a number, every round is a chance to replace a good
// answer with a bad one, and the bad one would arrive wearing the `recovered`
// mark, which reads as an improvement. This is the one rule the forgiving
// posture does not relax.
//
// ⚑ AND THE CORRELATION MUST BE STRICTLY POSITIVE. RegionFit2D marks a point
// whose displacement it FITTED by resetting that point's correlation to zero --
// the engine's own way of saying "borrowed, not measured". A zero is therefore
// not a poor correlation, it is the absence of one, and accepting it would put
// the interpolation into the field wearing the `recovered` mark: exactly the
// trap this whole pass is built to avoid. The same reasoning as "strictly
// positive, not merely non-negative" for the noise floor and the conditioning,
// and found the same way -- by a negative check, which removed the re-solve
// after the fit and watched every fitted value sail through as a measurement.
bool recoveryImproves(const CorrelationPoint &before,
                      const CorrelationPoint &after);

// Fold a round's re-solved answers back into the field, keeping only the ones
// that improved, and marking those as recovered. Returns the field indices that
// were kept, so a caller holding the engine's own points can write the same
// answers back to the same places without deciding acceptance a second time.
//
// `attempted` are indices into the field's point list; `resolved` holds the
// answer for each, in the same order. A point's position and grid cell are its
// own and are never taken from the answer: a recovered point is the SAME point
// measured again, not a new one somewhere better.
QVector<int> acceptRecoveryRound(CorrelationResult &field,
                                 const QVector<int> &attempted,
                                 const QVector<CorrelationPoint> &resolved);

// Whether to run another round: something was gained, and the cap is not spent.
bool recoveryShouldContinue(int roundsDone, int acceptedThisRound,
                            const RecoveryPolicy &policy);
