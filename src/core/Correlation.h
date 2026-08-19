#pragma once

#include "core/Roi.h"
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

    // Empty when the strain fit will use the subregion as asked. Restated from
    // strainSubregionWarning() so a caller has one thing to ask.
    QString strainWarning() const;

    // True where the engine offers the combination at all. Newton-Raphson is
    // implemented for the first-order shape function only.
    bool isAvailable() const;
    QString unavailableReason() const;
};

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

// Runs one correlation on a worker thread.
//
// The engine's own compute() is a single blocking call with no progress and no
// cancellation -- feedback we have already sent upstream. Until that changes,
// the queue is split into chunks here and handed over a chunk at a time, which
// is what makes both progress reporting and a working Stop possible without
// touching the engine.
class CorrelationRunner : public QObject
{
    Q_OBJECT

public:
    // An invalid region means the whole image, which is what the grid covered
    // before regions existed.
    CorrelationRunner(CorrelationSettings settings, RegionOfInterest roi,
                      QString referencePath, QString targetPath,
                      QObject *parent = nullptr);

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
    std::atomic<bool> m_cancelled{false};
};
