#pragma once

#include "core/Roi.h"

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

    double secondsElapsed = 0.0;
    bool   cancelled      = false;

    int total() const { return int(points.size()); }

    // Smallest and largest displacement magnitude among SOLVED points, which is
    // the range the colour scale and the project tree must both report. Rejected
    // points are excluded: a point the solver refused is not a displacement of
    // zero, and letting one into the range would stretch the scale over a value
    // that was never measured. False when nothing was solved.
    bool magnitudeRange(double &lowest, double &highest) const;
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
