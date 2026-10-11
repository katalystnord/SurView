#include "core/Correlation.h"
#include "core/Clipping.h"
#include "core/RoiDetect.h"

#include "core/PoiGrid.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include <omp.h>

#include "opencorr.h"

using namespace opencorr;

namespace {

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

QString tooLittleOutsideTheCuts(double minValidShare)
{
    return QObject::tr("Less than %1 % of its subset lies clear of the cuts, the "
                       "least a point is measured on.")
        .arg(qRound(minValidShare * 100.0));
}

}  // namespace

int solverThreadCount(int coresAvailable)
{
    return coresAvailable > 1 ? coresAvailable - 1 : 1;
}


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

QString shapeOrderName(int shapeOrder)
{
    return shapeOrder == 2 ? QObject::tr("Second order (quadratic)")
                           : QObject::tr("First order (affine)");
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

    const int threads = solverThreadCount(QThread::idealThreadCount());
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

        // ⚑ The engine's own shapes answer membership, every shape in the
        // region's order, so a boundary means the same thing here as
        // everywhere else it is used (regionInsideTest(), core/RoiDetect.h).
        const std::function<bool(int, int)> inside = regionInsideTest(m_roi);

        std::vector<POI2D> queue;
        std::vector<int> gridIndex;
        // How far each followed point sits from the whole pixel it is measured
        // on; empty for a run on a grid, whose points are whole pixels.
        QVector<QPointF> subPixel;

        CorrelationResult result;

        if (m_seeding.isEmpty()) {
            const PoiGrid grid = buildPoiGrid(
                ref_img.width, ref_img.height, radius, m_settings.gridStep, m_roi,
                inside);

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

            // How many of the points that WILL be measured have a subset
            // reaching ground a cut removed. Computed here, where the grid and
            // the subset radius are both known, and reported with the run.
            if (m_roi.hasCuts()) {
                for (const PoiGridCell &cell : grid.cells) {
                    if (subsetReachesACut(m_roi, cell.x, cell.y, radius))
                        result.subsetsReachingAHole++;
                }
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
            //
            // ⚑ EACH ON ITS NEAREST WHOLE PIXEL. The engine reads a reference
            // subset at the TRUNCATED position of a point and warps the target
            // from the exact one, so a point handed in at 194.6 is measured as
            // if it sat at 194 and its answer is wrong by the 0.6 px between
            // them. A followed point is between pixels almost always, so every
            // frame after a re-anchor was off by half a pixel on average while
            // correlating perfectly. Measured on the whole pixel, the answer is
            // carried back to the followed position after the solve (see
            // "back to where each followed point is", below).
            queue.reserve(size_t(m_seeding.points.size()));
            gridIndex.reserve(size_t(m_seeding.points.size()));
            subPixel.reserve(m_seeding.points.size());
            for (const PoiSeeding::Seed &seed : m_seeding.points) {
                const float x = std::round(seed.x);
                const float y = std::round(seed.y);
                queue.emplace_back(Point2D(x, y));
                subPixel.append(QPointF(double(seed.x - x), double(seed.y - y)));
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

        // --- subsets beside a cut -------------------------------------------
        // Built once from the region: ground is every pixel no cut removed,
        // pulled back kMaskPullIn from each cut. The region's outer boundary
        // removes nothing here, so a subset reaching past it keeps every pixel,
        // as it always has (decision 1, 2026-10-11).
        //
        // ⚑ Where the cuts ARE in the image measured against. On the original
        // reference that is where they were drawn. A sequence that has
        // re-anchored measures against a later frame, where every cut has moved
        // and deformed with the specimen, so the sequence carries them there by
        // what it banked (core/CarriedCuts.h). A mask left where the cuts were drawn would remove the wrong
        // pixels while saying it had removed the right ones, so a cut that
        // could not be carried leaves the frame unmasked, saying why.
        std::unique_ptr<ValidPixelMask2D> mask;
        if (m_settings.maskAtCuts && m_roi.isValid() && m_roi.hasCuts()) {
            const RegionOfInterest *cutsHere = nullptr;
            if (m_referenceIsTheOriginal) {
                cutsHere = &m_roi;
            } else if (m_carried.isValid()) {
                cutsHere = &m_carried.region;
                result.maskCarried = true;
            } else {
                result.maskingNote = !m_carried.refusal.isEmpty()
                    ? m_carried.refusal
                    : tr("Subsets beside a cut were not masked on this frame: the "
                         "run has re-anchored to a later frame, where the cuts no "
                         "longer sit where they were drawn.");
            }
            if (cutsHere) {
                mask = std::make_unique<ValidPixelMask2D>(
                    *engineRegion(*cutsHere, QSize(ref_img.width, ref_img.height)),
                    ref_img.width, ref_img.height, kMaskPullIn);
                result.maskingApplied = true;
                result.maskPullIn = kMaskPullIn;
                result.minValidShare = m_settings.minValidShare;
            }
        }
        // Narrowed to float once, here: the engine compares a float share with
        // it, and 0.25 widened back would sit on the other side of a boundary.
        const float minValidShare = float(m_settings.minValidShare);

        // Never zero: an empty grid is a refusal from buildPoiGrid() above, and
        // a seeding with no points is treated as no seeding at all.
        const int total = int(queue.size());
        emit progress(0, total, tr("estimating displacement"));

        // Integer-pixel first guess for every point, then the sub-pixel solve.
        // Both are chunked so Stop stays responsive through either stage.
        FFTCC2D fftcc(radius, radius, threads);
        fftcc.setImages(ref_img, tar_img);
        fftcc.setValidPixels(mask.get(), minValidShare);

        std::unique_ptr<DIC> solver = makeSolver(m_settings, threads);
        if (!solver) {
            emit failed(tr("No engine solver matches the chosen settings."));
            return;
        }
        solver->setImages(ref_img, tar_img);
        solver->setValidPixels(mask.get(), minValidShare);
        solver->prepare();

        result.points.reserve(total);

        // The first estimate at every point, kept after the solver has
        // overwritten it: a point refused near an edge is judged against the
        // movement it was estimated to have (see subsetCarriedPastTheEdge()).
        std::vector<std::pair<float, float>> estimate(size_t(total),
                                                      {qQNaN(), qQNaN()});

        // How far the SOLVER got. Points beyond this still hold the integer-
        // pixel FFTCC estimate, which is a starting guess and not a
        // measurement -- on a stopped run they must not be reported as results.
        int solvedUpTo = 0;

        for (int stage = 0; stage < 2 && !m_cancelled; stage++) {
            const QString stageName = stage == 0
                                          ? tr("estimating displacement")
                                          : tr("refining to sub-pixel");
            for (int start = 0; start < total; start += m_chunkPoints) {
                if (m_cancelled)
                    break;

                const int count = std::min(m_chunkPoints, total - start);
                std::vector<POI2D> chunk(queue.begin() + start,
                                         queue.begin() + start + count);

                if (stage == 0) {
                    fftcc.compute(chunk);
                    for (int k = 0; k < count; k++) {
                        estimate[size_t(start + k)] = {chunk[size_t(k)].deformation.u,
                                                       chunk[size_t(k)].deformation.v};
                    }
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

        // --- a second pass at the points that were not measured well ----------
        //
        // Most points a solve loses are not unmeasurable places on the specimen;
        // they are places where the initial guess was poor. RegionFit2D fits an
        // affine displacement field to the reliable points around a bad one, and
        // that fit is a far better starting point than the one that failed.
        //
        // ⚑ THE FIT IS AN INITIAL GUESS, NEVER AN ANSWER. The engine resets a
        // fitted point's correlation to zero precisely because the value is
        // borrowed from the neighbours. So every fitted point goes straight back
        // through the same solver, and what is reported is a real correlation
        // measured at that point. The rules for which points are attempted,
        // which are trusted to seed, and which answers are kept all live in
        // core/Recovery.h, where they are engine-free and tested exhaustively.
        //
        // Rounds, not one pass: each round's recoveries become the next round's
        // seeds, so repair spreads inward from good ground. The middle of a
        // large failed patch has no reliable neighbour at all on the first
        // round.
        std::vector<bool> recovered(size_t(total), false);
        result.recoveryRequested = m_settings.recovery.enabled;

        if (m_settings.recovery.enabled && !m_cancelled) {
            const RecoveryNeighbourhood hood =
                recoveryNeighbourhood(m_settings.recovery, result.step);
            RegionFit2D region_fit(hood.searchRadius, hood.minNeighbours,
                                   threads);

            // A view of the queue in the terms core/Recovery.h reasons about.
            // The judgement of what counts as a failure stays the engine's, via
            // isFailureStatus(), exactly as it does in the final conversion.
            auto viewOfQueue = [&]() {
                CorrelationResult view;
                view.step = result.step;
                view.points.reserve(total);
                for (int i = 0; i < total; i++) {
                    const POI2D &poi = queue[size_t(i)];
                    CorrelationPoint point;
                    point.gridIndex = gridIndex[size_t(i)];
                    point.x = poi.x;
                    point.y = poi.y;
                    point.zncc = poi.result.zncc;
                    point.converged = !isFailureStatus(poi.result.zncc);
                    point.recovered = recovered[size_t(i)];
                    view.points.append(point);
                }
                return view;
            };

            CorrelationResult view = viewOfQueue();
            result.stillUnrecovered =
                int(pointsNeedingRecovery(view, m_settings.recovery).size());

            const QString stageName = tr("repairing points that failed");
            int accepted = 1;   // enough to enter the loop
            int round = 0;

            while (!m_cancelled && recoveryShouldContinue(round, accepted,
                                                          m_settings.recovery)
                   && recoveryCanRun(view, m_settings.recovery)) {
                const QVector<int> attempted =
                    pointsNeedingRecovery(view, m_settings.recovery);
                const QVector<int> seedIndices =
                    recoverySeeds(view, m_settings.recovery);

                emit progress(0, attempted.size(), stageName);

                std::vector<POI2D> seeds;
                seeds.reserve(size_t(seedIndices.size()));
                for (int index : seedIndices)
                    seeds.push_back(queue[size_t(index)]);

                std::vector<POI2D> trial;
                trial.reserve(size_t(attempted.size()));
                for (int index : attempted)
                    trial.push_back(queue[size_t(index)]);

                region_fit.setNeighbor(seeds);
                region_fit.prepare();

                // Chunked for the reason every other engine call here is: the
                // whole-queue call blocks with no progress and no way to stop.
                const int trialCount = int(trial.size());
                for (int start = 0; start < trialCount; start += m_chunkPoints) {
                    if (m_cancelled)
                        break;
                    const int count = std::min(m_chunkPoints, trialCount - start);
                    std::vector<POI2D> chunk(trial.begin() + start,
                                             trial.begin() + start + count);
                    region_fit.compute(chunk);
                    solver->compute(chunk);
                    std::copy(chunk.begin(), chunk.end(), trial.begin() + start);
                    emit progress(start + count, trialCount, stageName);
                }
                if (m_cancelled)
                    break;

                QVector<CorrelationPoint> resolved;
                resolved.reserve(trialCount);
                for (const POI2D &poi : trial) {
                    CorrelationPoint point;
                    point.x = poi.x;
                    point.y = poi.y;
                    point.u = poi.deformation.u;
                    point.v = poi.deformation.v;
                    point.zncc = poi.result.zncc;
                    point.converged = !isFailureStatus(poi.result.zncc);
                    resolved.append(point);
                }

                const QVector<int> kept =
                    acceptRecoveryRound(view, attempted, resolved);

                // The same answers, written back to the engine's own points, so
                // the reliability pass and the strain fit that follow see the
                // repaired field rather than the one before it.
                for (int i = 0; i < attempted.size(); i++) {
                    if (!kept.contains(attempted[i]))
                        continue;
                    queue[size_t(attempted[i])] = trial[size_t(i)];
                    recovered[size_t(attempted[i])] = true;
                }

                accepted = int(kept.size());
                round++;
            }

            result.recoveryRounds = round;

            // ⚑ Counted from the marks, not summed over the rounds. A point
            // can improve in one round and improve again in a later one, and
            // summing each round's acceptances counts it twice -- so a run
            // reported 42 points recovered while 41 points carried the mark,
            // and the field and the report disagreed by one. What a reader is
            // owed is how many POINTS were recovered, which is what the marks
            // say.
            result.recoveredPoints =
                int(std::count(recovered.begin(), recovered.end(), true));
            result.stillUnrecovered =
                int(pointsNeedingRecovery(viewOfQueue(),
                                          m_settings.recovery).size());
        }

        // --- how far each point can be trusted --------------------------------
        // Always run, with no setting to turn it off: under tenet 9 the account
        // of how far a measurement can be trusted is not an optional extra, and
        // it costs about a second per 30,000 points against a solve that costs
        // far more. Skipped only on a stopped run, where the points it would
        // qualify are first guesses rather than measurements.
        if (!m_cancelled) {
            Uncertainty2D reliability(radius, radius, threads);
            reliability.setImages(ref_img, tar_img);
            reliability.setValidPixels(mask.get(), minValidShare);
            reliability.prepare();

            const QString stageName = tr("checking reliability");
            emit progress(0, total, stageName);

            // Chunked for the same reason the solve and the strain fit are:
            // the engine's whole-queue call blocks with no progress and no way
            // to stop.
            for (int start = 0; start < total; start += m_chunkPoints) {
                if (m_cancelled)
                    break;
                const int count = std::min(m_chunkPoints, total - start);
#pragma omp parallel for num_threads(threads)
                for (int i = start; i < start + count; i++)
                    reliability.compute(&queue[size_t(i)]);
                emit progress(start + count, total, stageName);
            }

            // The one number every noise floor in the field was scaled by. A
            // map of noise floors cannot be read without it.
            result.referenceNoise = Uncertainty2D::noiseStdDev(ref_img);
        }

        // --- back to where each followed point is ---------------------------
        // A followed point was measured on its nearest whole pixel (see the
        // seeding above), so its answer is the movement of the material there.
        // Its position is left on that pixel: a sequence reports every point
        // where it started (composeTotalField()), and strain is fitted there.
        // Carried the fraction of a pixel back to the followed point by the
        // subset's own displacement gradient, which is exact for a movement
        // that is affine across the subset -- the engine's own sub-pixel centre
        // offset for ICGN does the same, and every solver returns the gradient.
        // After the reliability pass, which probes the solution where it was
        // found; before strain, whose fit needs each displacement at the
        // position it belongs to.
        for (int i = 0; i < subPixel.size(); i++) {
            POI2D &poi = queue[size_t(i)];
            const float fx = float(subPixel.at(i).x());
            const float fy = float(subPixel.at(i).y());
            if (!isFailureStatus(poi.result.zncc)) {
                poi.deformation.u += poi.deformation.ux * fx + poi.deformation.uy * fy;
                poi.deformation.v += poi.deformation.vx * fx + poi.deformation.vy * fy;
            }
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
            // ⚑ A FOLLOWED POINT'S STRAIN IS FITTED ON ITS TOTAL MOVEMENT, at the
            // position it started from. What this frame measured is the
            // increment from the reference the run last moved on to, and its
            // gradient is the strain since THEN; the field is reported as the
            // movement from the original reference at the original positions
            // (composeTotalField(), core/ReferenceUpdate.h), so its strain must
            // be measured the same way or a re-anchored frame reports the strain
            // since the last re-anchor under the original reference's name.
            // A lost point's position is unknown, so it fits no neighbour.
            std::vector<POI2D> fitted;
            std::vector<POI2D> &fitOn = m_seeding.isEmpty() ? queue : fitted;
            if (!m_seeding.isEmpty()) {
                fitted = queue;
                for (int i = 0; i < total; i++) {
                    const PoiSeeding::Seed &seed = m_seeding.points.at(i);
                    POI2D &poi = fitted[size_t(i)];
                    poi.x = seed.x0;
                    poi.y = seed.y0;
                    poi.deformation.u += seed.bankedU;
                    poi.deformation.v += seed.bankedV;
                    if (seed.lost)
                        poi.result.zncc = -1.f;
                }
            }
            strain.prepare(fitOn);

            const QString stageName = tr("fitting strain");
            emit progress(0, total, stageName);

            // Chunked for the same reason the solve is: the engine's own
            // whole-queue call would block with no progress and no way to stop.
            // Each point still fits against the WHOLE queue -- only the loop is
            // divided, never the neighbourhood.
            for (int start = 0; start < total; start += m_chunkPoints) {
                if (m_cancelled)
                    break;
                const int count = std::min(m_chunkPoints, total - start);
#pragma omp parallel for num_threads(threads)
                for (int i = start; i < start + count; i++)
                    strain.compute(&fitOn[size_t(i)], fitOn);
                emit progress(start + count, total, stageName);
            }
            if (!m_seeding.isEmpty()) {
                for (int i = 0; i < total; i++)
                    queue[size_t(i)].strain = fitted[size_t(i)].strain;
            }

            result.strainRequested = true;
            result.strainMeasure = m_settings.strainMeasure;
            result.strainRadius = m_settings.strainRadius;
        }

        const QString notReached = tr("not reached before the run was stopped");

        // The extremes the reference image actually holds, against which each
        // subset's clipped share is judged (core/Clipping.h).
        const double darkest = double(ref_img.eg_mat.minCoeff());
        const double brightest = double(ref_img.eg_mat.maxCoeff());
        const auto referencePixel = [&ref_img](int x, int y) {
            return double(ref_img.eg_mat(y, x));
        };

        for (int i = 0; i < total; i++) {
            const POI2D &poi = queue[size_t(i)];
            CorrelationPoint point;
            point.gridIndex = gridIndex[size_t(i)];
            point.x    = poi.x;
            point.y    = poi.y;
            point.u    = poi.deformation.u;
            point.v    = poi.deformation.v;
            point.zncc = poi.result.zncc;

            // Of every point attempted, failed ones included: a hole in the
            // field over glare is explained by it as well.
            const double share = clippedShare(referencePixel, ref_img.width,
                                              ref_img.height, qRound(poi.x),
                                              qRound(poi.y), radius, darkest,
                                              brightest);
            if (share >= 0.0) {
                point.clippedShare = float(share);
                point.clippedShareMeasured = true;
            }

            // The share the engine measured it on, asked of the same mask at
            // the same centre: the whole pixel the point was measured on, which
            // for a followed point is the nearest one (see the seeding above).
            if (mask) {
                point.validShare = mask->validShare(qRound(poi.x), qRound(poi.y), radius, radius);
                point.masked = point.validShare < 1.f;
            }

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
                    if (point.masked)
                        result.maskedPoints++;
                    if (point.clippedShareMeasured
                        && double(point.clippedShare) > kMostlyClipped)
                        result.mostlyClipped++;
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
                    // ⚑ The engine reports a subset out of bounds and an
                    // invalid estimate under one code, which reads to anyone
                    // looking at the hole as a fault in their photograph. Near
                    // an edge with the specimen moving toward it, it is the
                    // specimen's movement, and the reader is told so.
                    if (int(poi.result.zncc) == STATUS_INVALID_SUBSET_OR_GUESS
                        && subsetCarriedPastTheEdge(
                            poi.x, poi.y, radius, estimate[size_t(i)].first,
                            estimate[size_t(i)].second, ref_img.width,
                            ref_img.height)) {
                        point.failureReason = kCarriedPastTheEdge;
                    }
                    // In our words, naming the setting and the remedy: the
                    // engine's describe the mask, and a reader knows cuts.
                    if (int(poi.result.zncc) == STATUS_TOO_FEW_VALID_PIXELS)
                        point.failureReason = tooLittleOutsideTheCuts(m_settings.minValidShare);
                    result.failuresByReason[point.failureReason]++;
                }
            }
            point.recovered = recovered[size_t(i)];
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
