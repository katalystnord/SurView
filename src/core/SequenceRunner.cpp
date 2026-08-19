#include "core/SequenceRunner.h"

#include <QMutexLocker>

SequenceRunner::SequenceRunner(CorrelationSettings settings, RegionOfInterest roi,
                               QString referencePath, QStringList targetPaths,
                               ReferenceUpdatePolicy policy, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_roi(std::move(roi))
    , m_referencePath(std::move(referencePath))
    , m_targetPaths(std::move(targetPaths))
    , m_policy(policy)
{
}

void SequenceRunner::cancel()
{
    m_cancelled = true;

    // Reach into the frame that is running now. Without this the loop stops
    // only once the current frame finishes on its own.
    QMutexLocker locked(&m_currentGuard);
    if (m_current)
        m_current->cancel();
}

void SequenceRunner::run()
{
    const int frameCount = int(m_targetPaths.size());
    if (frameCount == 0) {
        emit failed(tr("There is no target image to measure."));
        return;
    }

    int measured = 0;

    // What the run is currently comparing against, and where each point has been
    // followed to in it. Both stay put unless the policy re-anchors.
    QString currentReference = m_referencePath;
    QVector<TrackedPoint> tracked;

    for (int frame = 0; frame < frameCount; frame++) {
        if (m_cancelled)
            break;

        // One single-pair run per frame, driven directly rather than through a
        // thread of its own: this already IS the worker thread.
        CorrelationRunner runner(m_settings, m_roi, currentReference,
                                 m_targetPaths.at(frame));

        if (!tracked.isEmpty()) {
            // Measured where the points have been followed TO. Identical to the
            // grid until the first re-anchor, and taking the same path either
            // way means the seeded path is the one every sequence exercises
            // rather than a branch only long runs reach.
            PoiSeeding seeding;
            seeding.gridColumns = m_gridColumns;
            seeding.gridRows = m_gridRows;
            seeding.originX = m_originX;
            seeding.originY = m_originY;
            seeding.step = m_step;
            seeding.restrictedToRoi = m_restrictedToRoi;
            seeding.points.reserve(tracked.size());
            for (const TrackedPoint &point : tracked) {
                // Lost points stay in the queue at their stale position and are
                // discarded when the field is composed. Dropping them here
                // would shift every later index and silently re-pair each
                // remaining point with another point's history.
                seeding.points.append(PoiSeeding::Seed{point.gridIndex,
                                                       trackedX(point),
                                                       trackedY(point)});
            }
            runner.setSeeding(seeding);
        }

        {
            QMutexLocker locked(&m_currentGuard);
            m_current = &runner;
        }

        bool frameFailed = false;
        QString failure;
        connect(&runner, &CorrelationRunner::progress, this,
                [this, frame, frameCount](int done, int total, const QString &stage) {
                    emit frameProgress(frame, frameCount, done, total, stage);
                });
        connect(&runner, &CorrelationRunner::finished, this,
                [&](const CorrelationResult &increment) {
                    measured++;

                    // Followed from the first measured frame onward, so the
                    // grid the whole sequence reports on is the one that frame
                    // laid out.
                    if (tracked.isEmpty()) {
                        tracked = startTracking(increment);
                        m_gridColumns = increment.gridColumns;
                        m_gridRows = increment.gridRows;
                        m_originX = increment.originX;
                        m_originY = increment.originY;
                        m_step = increment.step;
                        m_restrictedToRoi = increment.restrictedToRoi;
                    }

                    // What is reported is always relative to the ORIGINAL
                    // reference, whatever this frame was measured against.
                    emit frameFinished(frame, composeTotalField(tracked, increment));

                    // Decided on the increment, not the total: the question is
                    // how well this frame correlated against what it was
                    // compared to, which is exactly what the increment holds.
                    if (fieldNeedsReanchor(increment, m_policy)) {
                        const int lostBefore = lostCount(tracked);
                        bankIncrement(tracked, increment);
                        currentReference = m_targetPaths.at(frame);
                        emit referenceReanchored(frame,
                                                 lostCount(tracked) - lostBefore);
                    }
                });
        connect(&runner, &CorrelationRunner::failed, this,
                [&frameFailed, &failure](const QString &reason) {
                    frameFailed = true;
                    failure = reason;
                });

        runner.run();

        {
            QMutexLocker locked(&m_currentGuard);
            m_current = nullptr;
        }

        if (frameFailed) {
            // A frame that cannot run at all stops the sequence rather than
            // being skipped: the same settings apply to every frame, so what
            // broke here will break on the rest, and eleven identical
            // complaints help nobody.
            emit failed(tr("Frame %1 of %2 could not be measured: %3")
                            .arg(frame + 1)
                            .arg(frameCount)
                            .arg(failure));
            return;
        }
    }

    emit finished(measured, m_cancelled.load());
}
