#pragma once

#include <QMap>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>

// SurView's only point of contact with the OpenCorr engine.
//
// Nothing outside Correlation.cpp includes an OpenCorr header or names an
// OpenCorr type. That is not an abstraction layer — it is deliberately thin,
// and the shape of a real interface should be decided once there is more than
// one caller to design it against. It is a containment boundary: upstream is
// rewriting the engine, and when that lands the damage should be confined to
// one file rather than spread across the widgets.

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

    // True where the engine offers the combination at all. Newton–Raphson is
    // implemented for the first-order shape function only.
    bool isAvailable() const;
    QString unavailableReason() const;
};

// One measured point. Positions are in reference-image pixels.
struct CorrelationPoint
{
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
// cancellation — feedback we have already sent upstream. Until that changes,
// the queue is split into chunks here and handed over a chunk at a time, which
// is what makes both progress reporting and a working Stop possible without
// touching the engine.
class CorrelationRunner : public QObject
{
    Q_OBJECT

public:
    CorrelationRunner(CorrelationSettings settings, QString referencePath,
                      QString targetPath, QObject *parent = nullptr);

public slots:
    void run();
    void cancel();

signals:
    void progress(int done, int total, const QString &stage);
    void finished(const CorrelationResult &result);
    void failed(const QString &reason);

private:
    CorrelationSettings m_settings;
    QString m_referencePath;
    QString m_targetPath;
    std::atomic<bool> m_cancelled{false};
};
