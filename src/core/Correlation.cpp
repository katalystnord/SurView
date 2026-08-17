#include "core/Correlation.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>

#include <memory>
#include <vector>

#include <omp.h>

#include "opencorr.h"

using namespace opencorr;

namespace {

// POIs handed to the engine per call. Small enough that Stop feels immediate
// and the progress bar moves, large enough that the engine's own OpenMP
// parallelism still has work to spread across threads.
constexpr int kChunkPoints = 2000;

int workerThreadCount()
{
    const int cores = QThread::idealThreadCount();
    return cores > 1 ? cores - 1 : 1;
}

// Build the solver the settings ask for. Returned as the DIC base so the
// chunked loop below does not care which one it got.
std::unique_ptr<DIC> makeSolver(const CorrelationSettings &settings, int threads)
{
    const int r = settings.subsetRadius;
    const auto convergence = float(settings.convergence);
    const auto iterations = float(settings.maxIterations);

    switch (settings.solver) {
    case CorrelationSettings::ICGN:
        if (settings.shapeOrder == 2)
            return std::make_unique<ICGN2D2>(r, r, convergence, iterations, threads);
        return std::make_unique<ICGN2D1>(r, r, convergence, iterations, threads);

    case CorrelationSettings::ICLM:
        if (settings.shapeOrder == 2)
            return std::make_unique<ICLM2D2>(r, r, convergence, iterations, threads);
        return std::make_unique<ICLM2D1>(r, r, convergence, iterations, threads);

    case CorrelationSettings::NewtonRaphson:
        // First order only; CorrelationSettings::isAvailable() refuses the
        // second-order combination before a run is ever started.
        return std::make_unique<NR2D1>(r, r, convergence, iterations, threads);
    }
    return nullptr;
}

}  // namespace

bool CorrelationResult::magnitudeRange(double &lowest, double &highest) const
{
    bool any = false;
    for (const CorrelationPoint &point : points) {
        if (!point.converged)
            continue;
        const double magnitude = std::hypot(point.u, point.v);
        if (!any) {
            lowest = highest = magnitude;
            any = true;
        } else {
            lowest = std::min(lowest, magnitude);
            highest = std::max(highest, magnitude);
        }
    }
    return any;
}

bool CorrelationSettings::isAvailable() const
{
    if (solver == NewtonRaphson && shapeOrder != 1)
        return false;
    return true;
}

QString CorrelationSettings::unavailableReason() const
{
    if (solver == NewtonRaphson && shapeOrder != 1) {
        return QObject::tr("The engine implements Newton–Raphson for the "
                           "first-order shape function only.");
    }
    return QString();
}

CorrelationRunner::CorrelationRunner(CorrelationSettings settings,
                                     QString referencePath, QString targetPath,
                                     QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_referencePath(std::move(referencePath))
    , m_targetPath(std::move(targetPath))
{
}

void CorrelationRunner::cancel()
{
    m_cancelled = true;
}

void CorrelationRunner::run()
{
    QElapsedTimer timer;
    timer.start();

    const int threads = workerThreadCount();
    omp_set_num_threads(threads);

    try {
        Image2D ref_img(m_referencePath.toStdString());
        Image2D tar_img(m_targetPath.toStdString());

        if (ref_img.width <= 0 || ref_img.height <= 0) {
            emit failed(tr("The engine could not read the reference image."));
            return;
        }
        if (tar_img.width != ref_img.width || tar_img.height != ref_img.height) {
            emit failed(tr("Reference and target differ in size (%1×%2 and "
                           "%3×%4). They must describe the same pixel grid.")
                            .arg(ref_img.width)
                            .arg(ref_img.height)
                            .arg(tar_img.width)
                            .arg(tar_img.height));
            return;
        }

        // A subset must lie wholly inside the image, so the grid starts one
        // subset radius in and stops one short of the far edge.
        const int radius = m_settings.subsetRadius;
        const int step   = m_settings.gridStep;
        const int firstX = radius;
        const int firstY = radius;
        const int lastX  = ref_img.width - 1 - radius;
        const int lastY  = ref_img.height - 1 - radius;

        if (lastX < firstX || lastY < firstY) {
            emit failed(tr("A subset radius of %1 px leaves no room for a "
                           "single point in a %2×%3 image.")
                            .arg(radius)
                            .arg(ref_img.width)
                            .arg(ref_img.height));
            return;
        }

        const int columns = (lastX - firstX) / step + 1;
        const int rows    = (lastY - firstY) / step + 1;

        std::vector<POI2D> queue;
        queue.reserve(size_t(columns) * size_t(rows));
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < columns; c++) {
                queue.emplace_back(Point2D(float(firstX + c * step),
                                           float(firstY + r * step)));
            }
        }

        const int total = int(queue.size());
        emit progress(0, total, tr("estimating displacement"));

        // Integer-pixel first guess for every point, then the sub-pixel solve.
        // Both are chunked so Stop stays responsive through either stage.
        FFTCC2D fftcc(radius, radius, threads);
        fftcc.setImages(ref_img, tar_img);

        std::unique_ptr<DIC> solver = makeSolver(m_settings, threads);
        if (!solver) {
            emit failed(tr("No engine solver matches the chosen settings."));
            return;
        }
        solver->setImages(ref_img, tar_img);
        solver->prepare();

        CorrelationResult result;
        result.gridColumns = columns;
        result.gridRows    = rows;
        result.originX     = float(firstX);
        result.originY     = float(firstY);
        result.step        = step;
        result.points.reserve(total);

        // How far the SOLVER got. Points beyond this still hold the integer-
        // pixel FFTCC estimate, which is a starting guess and not a
        // measurement — on a stopped run they must not be reported as results.
        int solvedUpTo = 0;

        for (int stage = 0; stage < 2 && !m_cancelled; stage++) {
            const QString stageName = stage == 0
                                          ? tr("estimating displacement")
                                          : tr("refining to sub-pixel");
            for (int start = 0; start < total; start += kChunkPoints) {
                if (m_cancelled) {
                    result.cancelled = true;
                    break;
                }

                const int count = std::min(kChunkPoints, total - start);
                std::vector<POI2D> chunk(queue.begin() + start,
                                         queue.begin() + start + count);

                if (stage == 0) {
                    fftcc.compute(chunk);
                } else {
                    solver->compute(chunk);
                    solvedUpTo = start + count;
                }

                std::copy(chunk.begin(), chunk.end(), queue.begin() + start);
                emit progress(start + count, total, stageName);
            }
        }

        const QString notReached = tr("not reached before the run was stopped");

        for (int i = 0; i < total; i++) {
            const POI2D &poi = queue[size_t(i)];
            CorrelationPoint point;
            point.x    = poi.x;
            point.y    = poi.y;
            point.u    = poi.deformation.u;
            point.v    = poi.deformation.v;
            point.zncc = poi.result.zncc;

            if (i >= solvedUpTo) {
                // Stopped before the solver reached this point. Counted and
                // named, not silently dropped: the grid the run set out to
                // measure is part of what happened.
                point.converged = false;
                result.failuresByReason[notReached]++;
            } else {
                // The engine reports a failure as a negative code in the same
                // field it otherwise uses for correlation, and
                // isFailureStatus() is its own test for which negatives are
                // codes rather than genuinely poor correlations. Asking it here
                // keeps that judgement in the engine.
                point.converged = !isFailureStatus(poi.result.zncc);
                if (point.converged) {
                    result.converged++;
                } else {
                    result.failuresByReason[QString::fromStdString(
                        statusDescription(poi.result.zncc))]++;
                }
            }
            result.points.append(point);
        }

        result.secondsElapsed = timer.elapsed() / 1000.0;
        emit finished(result);
    } catch (const std::string &message) {
        // OpenCorr throws std::string, not std::exception.
        emit failed(tr("The engine stopped: %1")
                        .arg(QString::fromStdString(message)));
    } catch (const std::exception &error) {
        emit failed(tr("The engine stopped: %1").arg(QString::fromLatin1(error.what())));
    }
}
