#pragma once

#include "core/Correlation.h"
#include "core/Roi.h"

#include <QObject>
#include <QStringList>

#include <QMutex>

#include <atomic>

// Measuring a whole sequence: one reference, many targets, in frame order.
//
// A separate class rather than a mode inside CorrelationRunner, which stays the
// single-pair engine it already was and keeps the tests that cover it. This
// owns only the loop and the cancellation across frames.
//
// ⚑ EVERY FRAME IS MEASURED AGAINST THE ORIGINAL REFERENCE, never against the
// frame before it. That is the ordinary meaning of a DIC sequence and it makes
// the displacements directly comparable, but it also means correlation degrades
// as the specimen moves away from where it started: past some deformation the
// engine's reference-update tracking is what a run needs instead, and this does
// not do that yet. Said here because a sequence that quietly stops correlating
// halfway through looks like a specimen that stopped deforming.
//
// Known cost of the same simplicity: each frame re-reads the reference image
// and rebuilds the solver's preparation. That is a fraction of a frame's solve,
// and paying it buys leaving the engine boundary in core/Correlation.cpp exactly
// as it is.
class SequenceRunner : public QObject
{
    Q_OBJECT

public:
    SequenceRunner(CorrelationSettings settings, RegionOfInterest roi,
                   QString referencePath, QStringList targetPaths,
                   QObject *parent = nullptr);

public slots:
    void run();
    void cancel();

signals:
    // Which frame, how far through it, and what it is doing -- so a long
    // sequence can say "frame 4 of 12, refining to sub-pixel" rather than
    // restarting a bar with no explanation eleven times.
    void frameProgress(int frame, int frameCount, int done, int total,
                       const QString &stage);

    // One frame measured. Carried out as it happens rather than held to the
    // end: a twelve-frame run is minutes long, and a result nobody can look at
    // until all of them finish is a result withheld for no reason.
    void frameFinished(int frame, const CorrelationResult &result);

    void finished(int framesMeasured, bool cancelled);
    void failed(const QString &reason);

private:
    CorrelationSettings m_settings;
    RegionOfInterest m_roi;
    QString m_referencePath;
    QStringList m_targetPaths;
    std::atomic<bool> m_cancelled{false};

    // The frame currently running, so Stop reaches INTO it rather than merely
    // ending the loop after it. A single frame can take half a minute, and a
    // button that waits that long reads as a button that did nothing. Guarded
    // because cancel() arrives on the GUI thread while run() owns this one.
    QMutex m_currentGuard;
    CorrelationRunner *m_current = nullptr;
};
