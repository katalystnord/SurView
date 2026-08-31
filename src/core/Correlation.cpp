#include "core/Correlation.h"

#include "core/PoiGrid.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>

#include <cmath>
#include <limits>
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

QString CorrelationSettings::strainWarning() const
{
    if (!strainEnabled)
        return QString();
    return strainSubregionWarning(strainRadius, gridStep, strainMinPoints);
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
        return QObject::tr("The engine implements Newton-Raphson for the "
                           "first-order shape function only.");
    }
    return QString();
}

QString solverDisplayName(CorrelationSettings::Solver solver)
{
    switch (solver) {
    case CorrelationSettings::ICGN:
        return QObject::tr("ICGN");
    case CorrelationSettings::NewtonRaphson:
        return QObject::tr("Newton-Raphson");
    case CorrelationSettings::ICLM:
        return QObject::tr("IC-LM");
    }
    return QString();
}

QVector<SolverChoice> offeredSolverChoices()
{
    QVector<SolverChoice> choices;
    for (auto solver : {CorrelationSettings::ICGN,
                        CorrelationSettings::NewtonRaphson,
                        CorrelationSettings::ICLM}) {
        for (int order : {1, 2}) {
            CorrelationSettings probe;
            probe.solver = solver;
            probe.shapeOrder = order;
            // Asked of the settings rather than listed here, so this cannot
            // drift from what a run will actually accept.
            if (probe.isAvailable())
                choices.append(SolverChoice{solver, order});
        }
    }
    return choices;
}

CorrelationRunner::CorrelationRunner(CorrelationSettings settings,
                                     RegionOfInterest roi,
                                     QString referencePath, QString targetPath,
                                     QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_roi(std::move(roi))
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

        // Where the points go is worked out by buildPoiGrid() (core/PoiGrid.h),
        // which is engine-free and therefore testable on its own. Membership in
        // the region is still the engine's own judgement -- it is handed in as
        // the predicate, so the boundary means the same thing here as it does
        // wherever else the shape is used.
        const int radius = m_settings.subsetRadius;

        std::unique_ptr<Polygon2D> region;
        if (m_roi.isValid()) {
            std::vector<int> vertex_x;
            std::vector<int> vertex_y;
            vertex_x.reserve(size_t(m_roi.vertices.size()));
            vertex_y.reserve(size_t(m_roi.vertices.size()));
            for (const QPoint &vertex : m_roi.vertices) {
                vertex_x.push_back(vertex.x());
                vertex_y.push_back(vertex.y());
            }
            region = std::make_unique<Polygon2D>(vertex_x, vertex_y);
        }

        std::vector<POI2D> queue;
        std::vector<int> gridIndex;

        CorrelationResult result;

        if (m_seeding.isEmpty()) {
            const PoiGrid grid = buildPoiGrid(
                ref_img.width, ref_img.height, radius, m_settings.gridStep, m_roi,
                [&region](int x, int y) { return region->contains(x, y); });

            if (!grid.isValid()) {
                emit failed(grid.refusal);
                return;
            }

            queue.reserve(size_t(grid.cells.size()));
            gridIndex.reserve(size_t(grid.cells.size()));
            for (const PoiGridCell &cell : grid.cells) {
                queue.emplace_back(Point2D(float(cell.x), float(cell.y)));
                gridIndex.push_back(cell.gridIndex);
            }

            result.gridColumns = grid.columns;
            result.gridRows    = grid.rows;
            result.originX     = float(grid.originX);
            result.originY     = float(grid.originY);
            result.step        = grid.step;
            result.restrictedToRoi = grid.restricted;
            if (grid.restricted)
                result.roi = m_roi;
        } else {
            // Positions handed in, because these points have been followed from
            // an earlier frame and are no longer where a grid would put them.
            // Deliberately NOT filtered against the image bounds here: a point
            // that has moved out of the picture is a measurement that failed,
            // and the engine says so in its own words. Dropping it quietly
            // would shrink the field with no account of why.
            queue.reserve(size_t(m_seeding.points.size()));
            gridIndex.reserve(size_t(m_seeding.points.size()));
            for (const PoiSeeding::Seed &seed : m_seeding.points) {
                queue.emplace_back(Point2D(seed.x, seed.y));
                gridIndex.push_back(seed.gridIndex);
            }

            result.gridColumns = m_seeding.gridColumns;
            result.gridRows    = m_seeding.gridRows;
            result.originX     = m_seeding.originX;
            result.originY     = m_seeding.originY;
            result.step        = m_seeding.step;
            result.restrictedToRoi = m_seeding.restrictedToRoi;
            if (m_seeding.restrictedToRoi)
                result.roi = m_roi;
        }

        // Never zero: an empty grid is a refusal from buildPoiGrid() above, and
        // a seeding with no points is treated as no seeding at all.
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

        result.points.reserve(total);

        // How far the SOLVER got. Points beyond this still hold the integer-
        // pixel FFTCC estimate, which is a starting guess and not a
        // measurement -- on a stopped run they must not be reported as results.
        int solvedUpTo = 0;

        for (int stage = 0; stage < 2 && !m_cancelled; stage++) {
            const QString stageName = stage == 0
                                          ? tr("estimating displacement")
                                          : tr("refining to sub-pixel");
            for (int start = 0; start < total; start += kChunkPoints) {
                if (m_cancelled)
                    break;

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

        // ⚑ Asked of the flag once, here, rather than set at the point the loop
        // breaks. Set inside the chunk loop it was missed entirely whenever the
        // cancel arrived during the LAST chunk of a stage: the outer loop's own
        // `!m_cancelled` condition then ended the run without passing through
        // the break that recorded it. On a grid small enough to be a single
        // chunk that was every cancel, and the run reported a clean finish over
        // a solve it had abandoned -- a result claiming to be complete when it
        // is not, which is the worst thing this class can produce.
        //
        // Found by the sequence suite, which cancels mid-frame and asks the
        // interrupted frame whether it knows: that case,
        // stopping_reaches_into_the_frame_that_is_running, is the regression
        // guard for this line.
        if (m_cancelled)
            result.cancelled = true;

        // --- how far each point can be trusted --------------------------------
        // Always run, with no setting to turn it off: under tenet 9 the account
        // of how far a measurement can be trusted is not an optional extra, and
        // it costs about a second per 30,000 points against a solve that costs
        // far more. Skipped only on a stopped run, where the points it would
        // qualify are first guesses rather than measurements.
        if (!m_cancelled) {
            Uncertainty2D reliability(radius, radius, threads);
            reliability.setImages(ref_img, tar_img);
            reliability.prepare();

            const QString stageName = tr("checking reliability");
            emit progress(0, total, stageName);

            // Chunked for the same reason the solve and the strain fit are:
            // the engine's whole-queue call blocks with no progress and no way
            // to stop.
            for (int start = 0; start < total; start += kChunkPoints) {
                if (m_cancelled)
                    break;
                const int count = std::min(kChunkPoints, total - start);
#pragma omp parallel for num_threads(threads)
                for (int i = start; i < start + count; i++)
                    reliability.compute(&queue[size_t(i)]);
                emit progress(start + count, total, stageName);
            }

            // The one number every noise floor in the field was scaled by. A
            // map of noise floors cannot be read without it.
            result.referenceNoise = Uncertainty2D::noiseStdDev(ref_img);
        }

        // --- strain ---------------------------------------------------------
        // Fitted from the solved displacements, so it can only run once they
        // exist. Skipped outright on a stopped run: a gradient fitted through
        // points the solver never reached would be a strain field built partly
        // from first guesses.
        if (m_settings.strainEnabled && !m_cancelled) {
            // Sentinel first. The engine leaves a POI's strain untouched when
            // its fit declines, and POI2D starts life with strain zeroed -- so
            // without this, "the fit found nothing here" and "this point is
            // unstrained" are the same three zeros. Not-a-number is the only
            // value the engine will never write.
            const float unfitted = std::numeric_limits<float>::quiet_NaN();
            for (POI2D &poi : queue) {
                poi.strain.exx = unfitted;
                poi.strain.eyy = unfitted;
                poi.strain.exy = unfitted;
            }

            Strain strain(float(m_settings.strainRadius),
                          m_settings.strainMinPoints, threads);
            strain.setZnccThreshold(kStrainFitCorrelationFloor);
            strain.setDescription(1);   // Lagrangian: gradients about the reference
            strain.setApproximation(
                m_settings.strainMeasure == StrainMeasure::GreenLagrange ? 2 : 1);
            strain.prepare(queue);

            const QString stageName = tr("fitting strain");
            emit progress(0, total, stageName);

            // Chunked for the same reason the solve is: the engine's own
            // whole-queue call would block with no progress and no way to stop.
            // Each point still fits against the WHOLE queue -- only the loop is
            // divided, never the neighbourhood.
            for (int start = 0; start < total; start += kChunkPoints) {
                if (m_cancelled)
                    break;
                const int count = std::min(kChunkPoints, total - start);
#pragma omp parallel for num_threads(threads)
                for (int i = start; i < start + count; i++)
                    strain.compute(&queue[size_t(i)], queue);
                emit progress(start + count, total, stageName);
            }

            result.strainRequested = true;
            result.strainMeasure = m_settings.strainMeasure;
            result.strainRadius = m_settings.strainRadius;
        }

        const QString notReached = tr("not reached before the run was stopped");

        for (int i = 0; i < total; i++) {
            const POI2D &poi = queue[size_t(i)];
            CorrelationPoint point;
            point.gridIndex = gridIndex[size_t(i)];
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
                point.failureReason = notReached;
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
                    // Solved, but too poorly correlated to be trusted as input
                    // to a neighbour's strain fit. Counted so a sparse strain
                    // field over a dense displacement field has a stated
                    // reason rather than looking like a bug.
                    if (poi.result.zncc < kStrainFitCorrelationFloor)
                        result.belowStrainFloor++;

                    // Fitted only where the engine wrote over the sentinel --
                    // all three components come from one fit, so one of them
                    // answers for all -- and only at a point whose OWN
                    // displacement was measured.
                    //
                    // ⚑ That second condition is not the engine's. Strain::
                    // compute() fits at every point it is given: a rejected
                    // centre merely excludes itself from its own regression and
                    // still receives a value from its neighbours. That value is
                    // an extrapolation into a place the instrument measured
                    // nothing, indistinguishable downstream from a measured
                    // one, and it made a run report "strain fitted at 1092 of
                    // the 1025 solved points".
                    // ⚑ STRICTLY positive, not merely non-negative. The engine
                    // marks both of these -1 when it cannot produce a value,
                    // which a >= 0 test would exclude -- but POI2D::clear()
                    // leaves them at 0, and zero is the flattering reading for
                    // both: a zero noise floor claims a perfect measurement and
                    // a zero conditioning a perfectly sharp cost. Neither is
                    // reachable as a real result (sigma is a square root of a
                    // positive ratio, beta a sum of non-zero reciprocal slopes),
                    // so anything not above zero was never written.
                    //
                    // Found by a negative check: with the reliability pass
                    // removed, a >= 0 test happily reported a noise floor of
                    // exactly zero at every solved point.
                    if (poi.result.sigma > 0.f) {
                        point.noiseFloor = poi.result.sigma;
                        point.noiseFloorMeasured = true;
                        result.noiseFloorMeasured++;
                    }
                    if (poi.result.beta > 0.f) {
                        point.conditioning = poi.result.beta;
                        point.conditioningMeasured = true;
                    } else {
                        // ⚑ Among converged points this cannot mean "not
                        // computed": that fires only for a failed or
                        // out-of-bounds point, neither of which reaches here.
                        // It means the probe found the cost unusable, which is
                        // the strongest caution the metric gives, so it is
                        // counted rather than left blank.
                        result.conditioningUnusable++;
                    }

                    if (result.strainRequested && !std::isnan(poi.strain.exx)) {
                        point.strainFitted = true;
                        point.exx = poi.strain.exx;
                        point.eyy = poi.strain.eyy;
                        point.exy = poi.strain.exy;
                        result.strainFitted++;
                    }
                } else {
                    point.failureReason = QString::fromStdString(
                        statusDescription(poi.result.zncc));
                    result.failuresByReason[point.failureReason]++;
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
