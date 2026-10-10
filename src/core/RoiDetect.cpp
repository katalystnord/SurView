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

        RegionShape outline;
        outline.kind = RegionShape::Polygon;
        outline.points.reserve(corners);
        for (int i = 0; i < corners; i++)
            outline.points.append(QPoint(x[size_t(i)], y[size_t(i)]));
        detection.roi.shapes = {outline};

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


// What one image's speckle is, independent of any region: the engine's windowed
// gradient maps and the image's own noise. See SpeckleQuality.h for why this is
// separated from the averaging.
class SpeckleFieldData
{
public:
    Eigen::MatrixXf mig;
    Eigen::MatrixXf sssig;
    double noiseStdDev = 0.0;
    int width = 0;
    int height = 0;
};

SpeckleField prepareSpeckleField(const QString &imagePath, int subsetRadius)
{
    using namespace opencorr;

    SpeckleField field;
    field.subsetRadiusPx = subsetRadius;
    if (subsetRadius < 1) {
        field.note = QObject::tr("A subset radius must be at least 1 px.");
        return field;
    }

    try {
        Image2D image(imagePath.toStdString());
        if (image.width <= 0 || image.height <= 0) {
            field.note = QObject::tr("The reference image could not be read.");
            return field;
        }

        // The window IS the subset, so the map answers the question actually
        // being asked: what will a subset of this radius have to work with.
        SpeckleQualityMap map(subsetRadius);
        map.computeGradientMaps(image);

        auto data = std::make_shared<SpeckleFieldData>();
        data->mig = map.migMap();
        data->sssig = map.sssigMap();
        data->noiseStdDev = double(Uncertainty2D::noiseStdDev(image));
        data->width = image.width;
        data->height = image.height;
        field.data = std::move(data);
        return field;
    } catch (const std::string &message) {
        field.note = QString::fromStdString(message);
        return field;
    } catch (const std::exception &error) {
        field.note = QString::fromLatin1(error.what());
        return field;
    }
}

SpeckleQuality speckleQualityIn(const SpeckleField &field,
                                const RegionOfInterest &roi)
{
    using namespace opencorr;

    SpeckleQuality quality;
    if (!field.isValid()) {
        quality.note = field.note.isEmpty()
                           ? QObject::tr("The reference image could not be read.")
                           : field.note;
        return quality;
    }

    const SpeckleFieldData &data = *field.data;

    // The engine's own shapes, as a run uses them, so a pixel is inside the
    // region here exactly when it will be inside it for the run.
    // ⚑ Every shape, cuts included. Until 2026-10-10 this asked the outer
    // boundary alone, so a region with a hole estimated the speckle across the
    // hole -- background, usually -- while the run never measured there.
    const bool restricted = roi.isValid();
    const std::function<bool(int, int)> inside = regionInsideTest(roi);

    double migSum = 0.0;
    double sssigSum = 0.0;
    int counted = 0;

    const QRect box = roi.isValid() ? roi.bounds()
                                    : QRect(0, 0, data.width, data.height);
    for (int y = std::max(0, box.top()); y <= std::min(data.height - 1, box.bottom()); y++) {
        for (int x = std::max(0, box.left()); x <= std::min(data.width - 1, box.right()); x++) {
            if (restricted && !inside(x, y))
                continue;
            migSum += double(data.mig(y, x));
            sssigSum += double(data.sssig(y, x));
            counted++;
        }
    }

    if (counted == 0) {
        quality.note = QObject::tr("The region does not lie over the image.");
        return quality;
    }

    quality.meanMig = migSum / counted;
    quality.meanSssig = sssigSum / counted;
    quality.noiseStdDev = data.noiseStdDev;

    if (!(quality.meanSssig > 0.0) || !(quality.noiseStdDev > 0.0)) {
        quality.note = QObject::tr("There is no gradient here to measure "
                                   "against: this region carries no speckle.");
        return quality;
    }

    // sigma = sqrt(2 * noise^2 / min(sum gx^2, sum gy^2)), which is what the
    // run reports per point. SSSIG sums BOTH axes, so the weaker axis is taken
    // as half of it -- an equal-in-both-directions assumption, stated in the
    // note rather than buried here.
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
}

SpeckleQuality speckleQualityIn(const QString &imagePath,
                                const RegionOfInterest &roi, int subsetRadius)
{
    return speckleQualityIn(prepareSpeckleField(imagePath, subsetRadius), roi);
}

std::function<bool(int x, int y)> regionInsideTest(const RegionOfInterest &roi)
{
    using namespace opencorr;

    struct Applied
    {
        std::unique_ptr<Shape2D> shape;
        bool subtract = false;
    };
    auto applied = std::make_shared<std::vector<Applied>>();

    const auto polygon = [](const QVector<QPoint> &corners) {
        std::vector<int> vertex_x;
        std::vector<int> vertex_y;
        vertex_x.reserve(size_t(corners.size()));
        vertex_y.reserve(size_t(corners.size()));
        for (const QPoint &corner : corners) {
            vertex_x.push_back(corner.x());
            vertex_y.push_back(corner.y());
        }
        return std::make_unique<Polygon2D>(vertex_x, vertex_y);
    };

    if (roi.isValid()) {
        for (const RegionShape &shape : roi.shapes) {
            if (!shape.isValid())
                continue;   // encloses nothing, so it adds and removes nothing
            Applied entry;
            entry.subtract = shape.subtract;
            if (shape.kind == RegionShape::Ellipse) {
                // Inscribed in the box, its edge pixels on the boundary: the
                // same centre and semi-axes shapeContains() mirrors.
                const QRect box = shape.bounds();
                entry.shape = std::make_unique<Ellipse2D>(
                    0.5f * float(box.left() + box.right()),
                    0.5f * float(box.top() + box.bottom()),
                    0.5f * float(box.right() - box.left()),
                    0.5f * float(box.bottom() - box.top()));
            } else {
                // A rectangle is the engine's polygon of its four corners, whose
                // boundary-inclusive rule covers the box's edge pixels.
                entry.shape = polygon(shape.handles());
            }
            applied->push_back(std::move(entry));
        }
    }

    // The last shape containing the pixel decides, as core/Roi.h states.
    return [applied](int x, int y) {
        for (auto it = applied->rbegin(); it != applied->rend(); ++it) {
            if (it->shape->contains(x, y))
                return !it->subtract;
        }
        return false;
    };
}
