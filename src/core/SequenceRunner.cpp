#include "core/SequenceRunner.h"

#include <QMutexLocker>

SequenceRunner::SequenceRunner(CorrelationSettings settings, RegionOfInterest roi,
                               QString referencePath, QStringList targetPaths,
                               QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_roi(std::move(roi))
    , m_referencePath(std::move(referencePath))
    , m_targetPaths(std::move(targetPaths))
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

    for (int frame = 0; frame < frameCount; frame++) {
        if (m_cancelled)
            break;

        // One single-pair run per frame, driven directly rather than through a
        // thread of its own: this already IS the worker thread.
        CorrelationRunner runner(m_settings, m_roi, m_referencePath,
                                 m_targetPaths.at(frame));

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
                [this, frame, &measured](const CorrelationResult &result) {
                    measured++;
                    emit frameFinished(frame, result);
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
