#include "core/RoiDetect.h"

#include "core/SpeckleQuality.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QObject>

#include <memory>

#include "opencorr.h"

using namespace opencorr;

RoiDetection detectSpeckleRegion(const QString &imagePath)
{
    RoiDetection detection;

    QElapsedTimer timer;
    timer.start();

    try {
        Image2D image(imagePath.toStdString());
        if (image.width <= 0 || image.height <= 0) {
            detection.reason =
                QObject::tr("The engine could not read the image.");
            return detection;
        }

        AutoROI detector;
        std::unique_ptr<Shape2D> shape = detector.detect(image);

        detection.secondsElapsed = timer.elapsed() / 1000.0;

        if (!shape) {
            // The detector refuses in several distinct situations and does not
            // report which one it hit, so its conditions are stated instead of
            // a guess being made between them. Saying only "detection failed"
            // would leave nothing to act on.
            detection.reason = QObject::tr(
                "The detector found no region it would stand behind. It "
                "declines when the image has no genuine speckled-versus-"
                "background split - including a frame that is speckled all "
                "over, where there is no background to separate - and when the "
                "region it does find is too small to be a meaningful part of "
                "the image. Drawing the region by hand states directly what it "
                "could not infer.");
            return detection;
        }

        // detect() is declared to return the Shape2D base so a future
        // hole-aware detection can return a different concrete type. Today it
        // always builds a polygon, and reading the boundary back needs the
        // concrete one -- so the cast is checked rather than assumed.
        const auto *polygon = dynamic_cast<const Polygon2D *>(shape.get());
        if (!polygon) {
            detection.reason = QObject::tr(
                "The detector returned a region of a shape this version cannot "
                "read back as a boundary.");
            return detection;
        }

        // The engine stores the ring closed, repeating the first vertex at the
        // end; SurView's own boundary is the open ring, so the repeat is
        // dropped rather than carried as a duplicate corner.
        const std::vector<int> &x = polygon->vertexX();
        const std::vector<int> &y = polygon->vertexY();
        const int corners = polygon->numVertices();
        if (corners < 3 || int(x.size()) < corners || int(y.size()) < corners) {
            detection.reason =
                QObject::tr("The detector returned a boundary with too few "
                            "corners to enclose an area.");
            return detection;
        }

        detection.roi.vertices.reserve(corners);
        for (int i = 0; i < corners; i++)
            detection.roi.vertices.append(QPoint(x[size_t(i)], y[size_t(i)]));

        detection.roi.origin = RegionOfInterest::Detected;

        // Carried with the region rather than shown once and forgotten: this is
        // the detector's own documented limit, and a user who adjusts the
        // proposal later still needs to know a second patch was never on offer.
        detection.roi.limitation = QObject::tr(
            "Detected regions are a single outline without holes: a second "
            "speckled patch, or a void inside this one, is not represented. "
            "The quality measure behind the segmentation is a fast indicator "
            "of speckle texture, not a guarantee of correlation accuracy.");

        detection.found = true;
        return detection;
    } catch (const std::string &message) {
        // OpenCorr throws std::string, not std::exception.
        detection.reason = QObject::tr("The detector stopped: %1")
                               .arg(QString::fromStdString(message));
    } catch (const std::exception &error) {
        detection.reason = QObject::tr("The detector stopped: %1")
                               .arg(QString::fromLatin1(error.what()));
    }

    return detection;
}


SpeckleQuality speckleQualityIn(const QString &imagePath,
                                const RegionOfInterest &roi, int subsetRadius)
{
    using namespace opencorr;

    SpeckleQuality quality;
    if (subsetRadius < 1) {
        quality.note = QObject::tr("A subset radius must be at least 1 px.");
        return quality;
    }

    try {
        Image2D image(imagePath.toStdString());
        if (image.width <= 0 || image.height <= 0) {
            quality.note = QObject::tr("The reference image could not be read.");
            return quality;
        }

        // The window IS the subset, so the map answers the question actually
        // being asked: what will a subset of this radius have to work with.
        SpeckleQualityMap map(subsetRadius);
        map.computeGradientMaps(image);

        const Eigen::MatrixXf &mig = map.migMap();
        const Eigen::MatrixXf &sssig = map.sssigMap();

        // The engine's own polygon test, as Correlation.cpp uses, so a pixel is
        // inside the region here exactly when it will be inside it for the run.
        std::unique_ptr<Polygon2D> region;
        if (roi.isValid()) {
            std::vector<int> vertex_x;
            std::vector<int> vertex_y;
            vertex_x.reserve(size_t(roi.vertices.size()));
            vertex_y.reserve(size_t(roi.vertices.size()));
            for (const QPoint &vertex : roi.vertices) {
                vertex_x.push_back(vertex.x());
                vertex_y.push_back(vertex.y());
            }
            region = std::make_unique<Polygon2D>(vertex_x, vertex_y);
        }

        double migSum = 0.0;
        double sssigSum = 0.0;
        int counted = 0;

        const QRect box = roi.isValid() ? roi.bounds()
                                        : QRect(0, 0, image.width, image.height);
        for (int y = std::max(0, box.top()); y <= std::min(image.height - 1, box.bottom()); y++) {
            for (int x = std::max(0, box.left()); x <= std::min(image.width - 1, box.right()); x++) {
                if (region && !region->contains(x, y))
                    continue;
                migSum += double(mig(y, x));
                sssigSum += double(sssig(y, x));
                counted++;
            }
        }

        if (counted == 0) {
            quality.note = QObject::tr("The region does not lie over the image.");
            return quality;
        }

        quality.meanMig = migSum / counted;
        quality.meanSssig = sssigSum / counted;
        quality.noiseStdDev = double(Uncertainty2D::noiseStdDev(image));

        if (!(quality.meanSssig > 0.0) || !(quality.noiseStdDev > 0.0)) {
            quality.note = QObject::tr("There is no gradient here to measure "
                                       "against: this region carries no speckle.");
            return quality;
        }

        // sigma = sqrt(2 * noise^2 / min(sum gx^2, sum gy^2)), which is what the
        // run reports per point. SSSIG sums BOTH axes, so the weaker axis is
        // taken as half of it -- an equal-in-both-directions assumption, stated
        // in the note rather than buried here.
        const double perAxis = quality.meanSssig / 2.0;
        quality.resolutionPx =
            std::sqrt(2.0 * quality.noiseStdDev * quality.noiseStdDev / perAxis);
        quality.measured = true;
        quality.note = QObject::tr(
            "Estimated from the reference image alone, taking the speckle to be "
            "equally strong in both directions. A pattern with a grain to it "
            "will do worse than this in its weaker direction. The run reports "
            "the real figure at every point.");
        return quality;
    } catch (const std::string &message) {
        quality.note = QString::fromStdString(message);
        return quality;
    } catch (const std::exception &error) {
        quality.note = QString::fromLatin1(error.what());
        return quality;
    }
}
