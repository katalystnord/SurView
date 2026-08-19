#pragma once

#include "core/Correlation.h"
#include "core/ReferenceUpdate.h"
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
// ⚑ BY DEFAULT EVERY FRAME IS MEASURED AGAINST THE ORIGINAL REFERENCE, never
// against the frame before it. That is the ordinary meaning of a DIC sequence
// and it keeps the displacements directly comparable, but correlation degrades
// as the specimen moves away from where it started.
//
// With a ReferenceUpdatePolicy switched on, the reference re-anchors to the
// current frame once too little of the field is still tracking, and each
// point's displacement is banked so that what is REPORTED stays relative to the
// original reference and on the original grid. The bookkeeping for that lives
// in core/ReferenceUpdate.h, engine-free and unit-tested; this class only
// decides when to apply it.
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
                   ReferenceUpdatePolicy policy = ReferenceUpdatePolicy(),
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

    // The reference moved on after this frame, so everything after it is
    // measured against a different picture. Reported because a sequence that
    // silently changed what it was comparing against would be unaccountable.
    void referenceReanchored(int frame, int pointsLost);

    void finished(int framesMeasured, bool cancelled);
    void failed(const QString &reason);

private:
    CorrelationSettings m_settings;
    RegionOfInterest m_roi;
    QString m_referencePath;
    QStringList m_targetPaths;
    ReferenceUpdatePolicy m_policy;
    std::atomic<bool> m_cancelled{false};

    // The frame currently running, so Stop reaches INTO it rather than merely
    // ending the loop after it. A single frame can take half a minute, and a
    // button that waits that long reads as a button that did nothing. Guarded
    // because cancel() arrives on the GUI thread while run() owns this one.
    QMutex m_currentGuard;
    CorrelationRunner *m_current = nullptr;

    // The grid the first measured frame laid out, which every later frame
    // reports on however far its points have been followed.
    int m_gridColumns = 0;
    int m_gridRows = 0;
    float m_originX = 0.f;
    float m_originY = 0.f;
    int m_step = 1;
    bool m_restrictedToRoi = false;
};
