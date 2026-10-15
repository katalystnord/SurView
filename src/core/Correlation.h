#pragma once

#include "core/CarriedCuts.h"
#include "core/Recovery.h"
#include "core/Roi.h"
#include "core/SessionAlignment.h"
#include "core/StrainFit.h"

#include <QMap>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>

// SurView's point of contact with the OpenCorr engine, together with
// RoiDetect.cpp -- the only two files that include an OpenCorr header or name an
// OpenCorr type. That is not an abstraction layer -- it is deliberately thin,
// and the shape of a real interface should be decided once there is more than
// one caller to design it against. It is a containment boundary: upstream is
// rewriting the engine, and when that lands the damage should be confined to
// those two files rather than spread across the widgets.

// What the Analysis panel asks for. Every field here is read by the run; a
// setting SurView cannot honour does not belong in this struct.
struct CorrelationSettings
{
    enum Solver { ICGN, NewtonRaphson, ICLM };

    Solver solver        = ICGN;
    int    shapeOrder    = 1;       // 1 = affine, 2 = quadratic
    int    subsetRadius  = 16;      // px
    int    gridStep      = 5;       // px between points of interest
    int    maxIterations = 10;
    double convergence   = 0.001;   // ‖Δp‖ threshold

    // --- strain -------------------------------------------------------------
    // Strain is fitted from the displacements of neighbouring points, so it has
    // its own neighbourhood and its own way of being unsatisfiable. See
    // core/StrainFit.h, which is where that is worked out and said out loud.
    bool   strainEnabled     = true;
    double strainRadius      = 25.0;   // px, the subregion the fit spans
    int    strainMinPoints   = 5;      // fewest points the fit will accept
    StrainMeasure strainMeasure = StrainMeasure::Cauchy;

    // --- subsets beside a cut -------------------------------------------------
    // Where a point's subset reaches ground a cut removed, correlate only the
    // pixels outside the cut. What shows through a hole is background that does
    // not move with the specimen, and correlated it pulls the answer toward no
    // movement while the point still reports a good correlation. On by default
    // (David, 2026-10-11). Cuts only: the region's outer boundary keeps the
    // whole subset, as it always has. See kMaskPullIn for how far from a cut.
    bool   maskAtCuts    = true;

    // The smallest share of a subset that must lie outside the cuts for the
    // point to be measured at all; below it the point is refused, saying so.
    // 25 per cent, measured: the error grows steadily as the share falls with
    // no cliff, and the row of points beside an ordinary hole sits at 40 to 50
    // per cent. Every point's own share is reported, so a stricter reading can
    // be applied afterwards.
    double minValidShare = 0.25;

    // --- the second pass at points that failed ------------------------------
    // How forgiving the run is about repairing points the first solve could not
    // measure well. On by default; see core/Recovery.h for why, and for why
    // being permissive here is safe.
    RecoveryPolicy recovery;

    // Empty when the strain fit will use the subregion as asked. Restated from
    // strainSubregionWarning() so a caller has one thing to ask.
    QString strainWarning() const;

    // True where the engine offers the combination at all. Newton-Raphson is
    // implemented for the first-order shape function only.
    bool isAvailable() const;
    QString unavailableReason() const;
};

// How far, in pixels, the ground a masked subset uses is pulled back from every
// cut. The engine's gradient stencil reaches 2 px across an edge, and its
// interpolation further, so the pixels right at a cut still carry what is
// behind it. Measured beside a hole: masked to the edge, 0.063 px of error;
// pulled back 2 px, 0.008 px, the same as far from any cut.
inline constexpr int kMaskPullIn = 2;

// One solver and shape order the application offers.
//
// ⚑ Single source of truth. The Analysis panel builds its controls from this
// list, and tests measure every entry in it, so a combination cannot be offered
// to a user without something checking that it works. Before this existed the
// panel offered five combinations and exactly one of them -- ICGN, first order
// - was covered by any test anywhere, in this repository or the engine's.
struct SolverChoice
{
    CorrelationSettings::Solver solver;
    int shapeOrder;
};

// Display name for a solver, as the panel shows it.
QString solverDisplayName(CorrelationSettings::Solver solver);

// "First order (affine)" or "Second order (quadratic)": the panel's choice and
// the Settings window's line, in one wording.
QString shapeOrderName(int shapeOrder);

// Every solver and shape order combination the application offers, which is
// every pair the engine actually implements.
QVector<SolverChoice> offeredSolverChoices();

// One measured point. Positions are in reference-image pixels.
struct CorrelationPoint
{
    // Which cell of the result's grid this point occupies, as row * columns +
    // column. Carried explicitly because the measured points are no longer
    // necessarily every cell: a region of interest leaves cells outside it with
    // no point at all, and a renderer that assumed points arrived one per cell
    // in order would smear the field sideways.
    int gridIndex = 0;

    float x = 0.f;
    float y = 0.f;
    float u = 0.f;     // displacement, px
    float v = 0.f;
    float zncc = 0.f;  // correlation, or a negative engine status code
    bool  converged = false;

    // Why the engine rejected this point, in its own words, empty where it did
    // not. The run report already counts failures by reason; this is the same
    // fact carried per point, because the place a reader asks "why is there a
    // hole here" is the hole, not the summary. Resolved here rather than at the
    // point of reading: statusDescription() is the engine's, and core/
    // Correlation.cpp is one of only two files allowed to know that.
    QString failureReason;

    // Measured on the second pass, from an initial guess fitted to the reliable
    // points around it, rather than on the first solve's own initialisation.
    //
    // It is a real measurement -- ICGN re-solved here and this correlation is
    // its own -- and it needs no apology. It is marked because it reached its
    // answer by a different route, and this code base marks that everywhere
    // else. Marking is also what makes a forgiving pass honest: we can afford
    // to keep more points precisely because a reader can see which ones were
    // hard won. See core/Recovery.h.
    bool recovered = false;

    // Strain, FITTED from this point's neighbours rather than measured here.
    // Meaningless unless strainFitted: the fit declines wherever too few
    // neighbours cleared the correlation floor, and it leaves the components
    // untouched when it does, so a zero here is indistinguishable from an
    // unstrained point unless the flag is carried alongside.
    float exx = 0.f;
    float eyy = 0.f;
    float exy = 0.f;
    bool  strainFitted = false;

    // --- how far this point can be trusted ----------------------------------
    // Two different questions, deliberately not combined into one score.
    //
    // noiseFloor (the engine's sigma, px) is what this subset's speckle could
    // resolve against the image noise. It is computed from the REFERENCE image
    // alone, so it says how well the measurement could ever have gone here, not
    // how well it did: it cannot see decorrelation, out-of-plane motion or a
    // shape function too poor for the deformation. A lower bound, never a total
    // error bar.
    //
    // conditioning (the engine's beta) probes the correlation cost around the
    // solution that was actually found, so it is the other half: larger means a
    // flatter cost and a less certain match. Dimensionless and relative, with
    // no absolute scale.
    float noiseFloor = 0.f;
    bool  noiseFloorMeasured = false;
    float conditioning = 0.f;
    bool  conditioningMeasured = false;

    // The share, 0 to 1, of this point's reference subset sitting at the
    // darkest or brightest value the reference image holds: pixels where the
    // camera recorded nothing, so a subset made mostly of them has no pattern
    // to correlate. A property of the photograph, like the noise floor, and
    // stated rather than used to refuse the point. See core/Clipping.h.
    float clippedShare = 0.f;
    bool  clippedShareMeasured = false;

    // The share, 0 to 1, of this point's subset it was measured on. 1 for a
    // subset no cut reaches. Where masking at cuts was applied and the subset
    // reached one, `masked` is set and this is the share outside the cut, less
    // the pull-in: what took part in the correlation.
    float validShare = 1.f;
    bool  masked = false;
};

struct CorrelationResult
{
    QVector<CorrelationPoint> points;

    int gridColumns = 0;
    int gridRows    = 0;
    float originX   = 0.f;   // reference-image position of the first point
    float originY   = 0.f;
    int   step      = 1;

    int converged = 0;

    // Whether a region of interest confined this run, and the boundary it used.
    // Kept with the result because "8261 of 8700 points solved" means something
    // different when those 8700 were the whole image than when they were a
    // region somebody chose.
    bool restrictedToRoi = false;
    RegionOfInterest roi;

    // Measured points whose subset reaches ground a cut removed, by the
    // region's own rule and without the pull-in: counted whether or not
    // masking was on. With masking OFF this is the number of points whose
    // answer was pulled toward whatever shows through the cut, and the run
    // report says so and names the control that would leave the cut out. With
    // it on, maskedPoints below is the account of what was done about them.
    int subsetsReachingAHole = 0;

    // Whether subsets beside a cut were correlated on the ground outside it,
    // and with what pull-in and floor. Off whenever there was no cut to mask,
    // or masking was not asked for.
    bool   maskingApplied = false;
    int    maskPullIn = 0;
    double minValidShare = 0.0;

    // The cuts were carried with the specimen to a reference the run re-anchored
    // to (core/CarriedCuts.h), rather than used where they were drawn.
    bool   maskCarried = false;

    // Points measured on part of their subset, counted among the solved.
    int maskedPoints = 0;

    // Why masking was asked for and not applied, in a reader's words; empty
    // when it was applied or not asked for.
    QString maskingNote;

    // Failures counted by the engine's own reason, never lumped into one
    // number: "did not converge" and "subset out of bounds" call for different
    // responses, and reporting a bare failure count would hide that.
    QMap<QString, int> failuresByReason;

    // What the reliability pass found. Always run: under tenet 9 the account of
    // how far a measurement can be trusted is not an optional extra, and it
    // costs about a second per 30,000 points against a solve that costs far
    // more.
    int noiseFloorMeasured = 0;

    // Converged points where the conditioning probe could not establish a
    // value. ⚑ Counted as a WARNING, not as missing data. The engine returns
    // the same -1 for "not computed" and "the cost was too flat to probe", but
    // "not computed" only fires for a failed or out-of-bounds point, and
    // neither reaches this count -- so among converged points it can only mean
    // the probe found the cost unusable, which is the strongest caution the
    // metric can give.
    int conditioningUnusable = 0;

    // Converged points whose subset is more than kMostlyClipped at the
    // reference image's extremes. Counted among the SOLVED because that is
    // where it matters: a failed point already says it failed, and a mostly
    // clipped one that converged reads as a measurement.
    int mostlyClipped = 0;

    // The reference image's estimated noise, in grey levels, which scaled every
    // noise floor above. Reported with the result rather than with the image
    // record: estimating it needs the engine, and core/Correlation.cpp is one of
    // only two files allowed to know the engine exists.
    double referenceNoise = 0.0;

    // What the strain fit did, reported rather than inferred from the points:
    // a strain field with holes in it needs to say whether the fit was asked
    // for at all, how much of the grid it reached, and what it excluded.
    bool strainRequested = false;
    int  strainFitted    = 0;
    int  belowStrainFloor = 0;   // points too poorly correlated to feed a fit
    StrainMeasure strainMeasure = StrainMeasure::Cauchy;
    double strainRadius = 0.0;

    // What the recovery pass did. Reported rather than inferred: a run where
    // recovery was switched off and a run where it was on and recovered nothing
    // produce identical fields, and they mean different things.
    bool recoveryRequested = false;
    int  recoveredPoints = 0;
    int  recoveryRounds = 0;
    int  stillUnrecovered = 0;   // attempted, and still below the retry bar

    // A target from a separate session: how it was brought onto the reference,
    // and on what. NotAligned for the ordinary run. See core/SessionAlignment.h.
    SessionAlignmentRecord alignment;

    double secondsElapsed = 0.0;
    bool   cancelled      = false;

    int total() const { return int(points.size()); }

    // Whether the run produced a strain field anyone can look at. False when
    // strain was not asked for, and false when it was asked for and reached
    // nothing -- the two are different, and only the result knows which.
    bool hasStrain() const { return strainRequested && strainFitted > 0; }
};

// Crosses a thread boundary as a queued signal argument.
Q_DECLARE_METATYPE(CorrelationResult)

// Where to put the points, when they are not to be laid out fresh from the
// settings. Used by sequence tracking, where a frame is measured at the
// positions its points have been followed TO, not where they started -- and
// still has to report on the grid they started on.
//
// Empty means the ordinary case: build the grid from the settings and the
// region, as every run did before sequences existed.
struct PoiSeeding
{
    struct Seed
    {
        int gridIndex = 0;
        float x = 0.f;     // where it is measured: in the current reference
        float y = 0.f;

        // Where it started, in the original reference, and how far it had
        // moved from there when the reference last moved on. Strain is fitted
        // on these: the field reported is the total movement from the original
        // reference at the original positions, so its gradient must be too.
        float x0 = 0.f;
        float y0 = 0.f;
        float bankedU = 0.f;
        float bankedV = 0.f;

        // Its position is no longer known (core/ReferenceUpdate.h). Measured
        // anyway so indices stay aligned, never used to fit a neighbour.
        bool lost = false;
    };

    QVector<Seed> points;

    // The grid the results are reported on, which is the ORIGINAL grid however
    // far the points have since moved. Carried here because the runner can no
    // longer derive it: it did not lay these points out.
    int gridColumns = 0;
    int gridRows = 0;
    float originX = 0.f;
    float originY = 0.f;
    int step = 1;
    bool restrictedToRoi = false;

    bool isEmpty() const { return points.isEmpty(); }
};

// Runs one correlation on a worker thread.
//
// The engine's own compute() is a single blocking call with no progress and no
// cancellation -- feedback we have already sent upstream. Until that changes,
// the queue is split into chunks here and handed over a chunk at a time, which
// is what makes both progress reporting and a working Stop possible without
// touching the engine.
// The number of points handed to the engine per call, and so how often
// progress is reported and cancellation can take effect.
inline constexpr int kDefaultChunkPoints = 2000;

// How many threads the engine is given, out of however many the machine says
// it has. ⚑ ONE IS LEFT FOR THE INTERFACE: a solve that takes every core makes
// the window it is reporting into stop redrawing, and a Stop button that does
// not repaint is a Stop button nobody can trust. And never fewer than one --
// omp_set_num_threads() is handed this number directly.
//
// A free function rather than a detail of the run, because a number nothing
// can call is a number nothing can check: it decided how every correlation in
// this application is dispatched and no test could reach it.
int solverThreadCount(int coresAvailable);

class CorrelationRunner : public QObject
{
    Q_OBJECT

public:
    // An invalid region means the whole image, which is what the grid covered
    // before regions existed.
    CorrelationRunner(CorrelationSettings settings, RegionOfInterest roi,
                      QString referencePath, QString targetPath,
                      QObject *parent = nullptr);

    // Measure at these positions instead of laying out a fresh grid. Must be
    // set before run().
    void setSeeding(PoiSeeding seeding) { m_seeding = std::move(seeding); }

    // ⚑ How many points are handed to the engine at once. The default is the
    // one the application runs at; it is settable ONLY so that a test can make
    // a small grid take more than one chunk.
    //
    // The chunking is not an optimisation -- it is the whole reason this class
    // exists, since the engine's compute() blocks with neither progress nor
    // cancellation, and every field this application draws comes back through
    // it. And every grid in the suite is smaller than the default 2000, so the
    // loop had only ever run ONCE, with `start` at zero, where `total - start`
    // and `total + start` are the same expression. A mutation sweep against
    // the complete suite found sixteen mutants living in that blind spot.
    //
    // Must be set before run(). A value below one is ignored.
    void setChunkPoints(int points) { if (points >= 1) m_chunkPoints = points; }

    // False when the reference is not the image the region was drawn on: a
    // sequence that has re-anchored measures against a later frame, where every
    // cut has moved with the specimen. Must be set before run().
    void setReferenceIsTheOriginal(bool original) { m_referenceIsTheOriginal = original; }

    // The cuts as they sit in a reference that is not the original, carried
    // there by the sequence (core/CarriedCuts.h); or, when they could not be,
    // the reason. Used only when the reference is not the original. Must be set
    // before run().
    void setCarriedCuts(const CarriedCuts &cuts) { m_carried = cuts; }

    // The target was photographed in a separate session: align it to the
    // reference first, fitted on `ground` (stationary, in reference pixels) or,
    // where that is empty, on the region being measured. The target may then
    // differ in size. Pair by pair only: a followed sequence is refused.
    void setSessionAlignment(const RegionOfInterest &ground)
    {
        m_align = true;
        m_ground = ground;
    }

public slots:
    void run();
    void cancel();

signals:
    void progress(int done, int total, const QString &stage);
    void finished(const CorrelationResult &result);
    void failed(const QString &reason);

private:
    CorrelationSettings m_settings;
    RegionOfInterest m_roi;
    QString m_referencePath;
    QString m_targetPath;
    PoiSeeding m_seeding;
    int m_chunkPoints = kDefaultChunkPoints;
    bool m_referenceIsTheOriginal = true;
    CarriedCuts m_carried;
    bool m_align = false;
    RegionOfInterest m_ground;
    std::atomic<bool> m_cancelled{false};
};
