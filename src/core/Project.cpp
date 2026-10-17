#include "core/Project.h"

#include "core/Defaults.h"

#include "core/ImageRecord.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>

namespace
{

constexpr const char *kMagic = "SurView DIC project";
// 2 since 2026-10-10: the region is a list of shapes. An older SurView would
// find no region in it and silently measure the whole image; with the version
// raised it refuses the file, in words, instead. Version 1 files still open.
//
// 3 since 2026-10-15: a shape may be GROUND, which is not part of the region. A
// version 2 SurView reads any shape that is not a cut as an addition, and would
// open a project with ground in it and measure the bench; with the version
// raised it refuses the file instead. Version 2 files still open as they were.
//
// 4 since 2026-10-16: the region may carry a KNOWN LENGTH. A version 3 SurView
// would open the project without it and fit a separate session's camera scale
// on the specimen, measuring a different strain; it refuses the file instead.
//
// 5 since 2026-10-17: the run may fit a separate session's camera as TILTED
// (CorrelationSettings::perspective). A version 4 SurView would ignore the
// setting and fit a similarity to a tilted camera; it refuses the file instead.
constexpr int kVersion = 5;

QString hashOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return QString();
    return QString::fromLatin1(hash.result().toHex());
}

// Relative where the image sits at or below the project's own folder, absolute
// otherwise. Half a rule on purpose: an image somewhere else entirely is not
// made portable by writing "../../../elsewhere" and pretending.
QString storedPath(const QDir &base, const QString &absolute)
{
    const QString relative = base.relativeFilePath(absolute);
    return relative.startsWith(QStringLiteral("..")) ? absolute : relative;
}

QJsonObject imageEntry(const QDir &base, const QString &path)
{
    QJsonObject entry;
    entry[QStringLiteral("path")] = storedPath(base, QFileInfo(path).absoluteFilePath());
    entry[QStringLiteral("sha256")] = hashOf(path);
    return entry;
}

// Resolves an entry back to a file, and says what was wrong with it.
struct Resolved
{
    QString path;
    bool exists = false;
    bool changed = false;
};

Resolved resolve(const QDir &base, const QJsonObject &entry)
{
    Resolved out;
    const QString stored = entry[QStringLiteral("path")].toString();
    out.path = QDir::isAbsolutePath(stored) ? stored
                                            : QDir::cleanPath(base.absoluteFilePath(stored));
    out.exists = QFileInfo::exists(out.path);
    if (!out.exists)
        return out;

    const QString was = entry[QStringLiteral("sha256")].toString();
    if (!was.isEmpty())
        out.changed = hashOf(out.path) != was;
    return out;
}

}  // namespace

QString saveProject(const QString &path, const Project &project)
{
    const QString name = QFileInfo(path).fileName();
    const QDir base(QFileInfo(path).absolutePath());

    QJsonObject root;
    root[QStringLiteral("format")] = QString::fromLatin1(kMagic);
    root[QStringLiteral("version")] = kVersion;

    if (!project.referencePath.isEmpty())
        root[QStringLiteral("reference")] = imageEntry(base, project.referencePath);

    QJsonArray targets;
    for (const QString &target : project.targetPaths)
        targets.append(imageEntry(base, target));
    root[QStringLiteral("targets")] = targets;

    // One entry per shape, in the order they apply, each its kind, whether it
    // cuts or is ground, and its points in the [x, y] form every point here uses.
    QJsonArray shapes;
    for (const RegionShape &shape : project.roi.shapes) {
        QJsonObject entry;
        entry[QStringLiteral("kind")] = shape.kind == RegionShape::Rectangle ? QStringLiteral("rectangle")
                                        : shape.kind == RegionShape::Ellipse ? QStringLiteral("ellipse")
                                                                             : QStringLiteral("polygon");
        entry[QStringLiteral("cut")] = shape.isCut();
        if (shape.isGround())
            entry[QStringLiteral("ground")] = true;
        QJsonArray points;
        for (const QPoint &vertex : shape.points) {
            QJsonArray point;
            point.append(vertex.x());
            point.append(vertex.y());
            points.append(point);
        }
        entry[QStringLiteral("points")] = points;
        shapes.append(entry);
    }
    QJsonObject roi;
    roi[QStringLiteral("shapes")] = shapes;
    roi[QStringLiteral("origin")] = int(project.roi.origin);
    // The detector's own caveat travels with the region it describes; reopened
    // without it, a detected region would stop saying what it cannot represent.
    if (!project.roi.limitation.isEmpty())
        roi[QStringLiteral("limitation")] = project.roi.limitation;
    // Its two ends, when one is marked. Version 4: a version 3 SurView would
    // open the project without it and fit the camera's scale on the specimen.
    if (project.roi.knownLength.marked) {
        const KnownLength &known = project.roi.knownLength;
        roi[QStringLiteral("knownLength")] =
            QJsonArray{QJsonArray{known.a.x(), known.a.y()},
                       QJsonArray{known.b.x(), known.b.y()}};
    }
    root[QStringLiteral("region")] = roi;

    root[QStringLiteral("settings")] = correlationSettingsToJson(project.settings);
    root[QStringLiteral("referenceUpdate")] = referencePolicyToJson(project.referenceUpdate);

    QJsonArray gauges;
    for (const Extensometer &gauge : project.extensometers) {
        QJsonObject entry;
        entry[QStringLiteral("name")] = gauge.name;
        entry[QStringLiteral("ax")] = gauge.ax;
        entry[QStringLiteral("ay")] = gauge.ay;
        entry[QStringLiteral("bx")] = gauge.bx;
        entry[QStringLiteral("by")] = gauge.by;
        gauges.append(entry);
    }
    root[QStringLiteral("extensometers")] = gauges;

    if (project.probe.isValid()) {
        QJsonObject probe;
        probe[QStringLiteral("name")] = project.probe.name;
        probe[QStringLiteral("ax")] = project.probe.ax;
        probe[QStringLiteral("ay")] = project.probe.ay;
        probe[QStringLiteral("bx")] = project.probe.bx;
        probe[QStringLiteral("by")] = project.probe.by;
        root[QStringLiteral("lineProbe")] = probe;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return QObject::tr("Could not write %1: %2.").arg(name, file.errorString());
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    if (file.error() != QFileDevice::NoError)
        return QObject::tr("Could not finish writing %1: %2.").arg(name, file.errorString());
    return QString();
}

ProjectLoad loadProject(const QString &path)
{
    ProjectLoad out;
    const QString name = QFileInfo(path).fileName();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        out.failure = QObject::tr("Could not open %1: %2.").arg(name, file.errorString());
        return out;
    }

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        out.failure = QObject::tr("%1 is not a SurView project file.").arg(name);
        return out;
    }

    const QJsonObject root = document.object();
    if (root[QStringLiteral("format")].toString() != QString::fromLatin1(kMagic)) {
        out.failure = QObject::tr("%1 is not a SurView project file.").arg(name);
        return out;
    }
    if (root[QStringLiteral("version")].toInt() > kVersion) {
        out.failure = QObject::tr("%1 was written by a newer version of SurView "
                                  "and cannot be opened by this one.").arg(name);
        return out;
    }

    const QDir base(QFileInfo(path).absolutePath());

    if (root.contains(QStringLiteral("reference"))) {
        const Resolved ref = resolve(base, root[QStringLiteral("reference")].toObject());
        if (ref.exists) {
            out.project.referencePath = ref.path;
            if (ref.changed)
                out.changed << ref.path;
        } else {
            out.missing << ref.path;
        }
    }

    const QJsonArray targets = root[QStringLiteral("targets")].toArray();
    for (const QJsonValue &value : targets) {
        const Resolved target = resolve(base, value.toObject());
        if (target.exists) {
            out.project.targetPaths << target.path;
            if (target.changed)
                out.changed << target.path;
        } else {
            out.missing << target.path;
        }
    }

    const QJsonObject roi = root[QStringLiteral("region")].toObject();
    const auto pointsOf = [](const QJsonArray &array) {
        QVector<QPoint> points;
        for (const QJsonValue &value : array) {
            const QJsonArray point = value.toArray();
            points << QPoint(point.at(0).toInt(), point.at(1).toInt());
        }
        return points;
    };
    if (roi.contains(QStringLiteral("shapes"))) {
        for (const QJsonValue &value : roi[QStringLiteral("shapes")].toArray()) {
            const QJsonObject entry = value.toObject();
            RegionShape shape;
            const QString kind = entry[QStringLiteral("kind")].toString();
            shape.kind = kind == QStringLiteral("rectangle") ? RegionShape::Rectangle
                         : kind == QStringLiteral("ellipse") ? RegionShape::Ellipse
                                                             : RegionShape::Polygon;
            shape.role = entry[QStringLiteral("ground")].toBool() ? RegionShape::Ground
                         : entry[QStringLiteral("cut")].toBool()  ? RegionShape::Cut
                                                                  : RegionShape::Add;
            shape.points = pointsOf(entry[QStringLiteral("points")].toArray());
            // A shape that encloses nothing is dropped on the way in rather than
            // carried as something to keep re-checking.
            if (shape.isValid())
                out.project.roi.shapes.append(shape);
        }
    } else {
        // ⚑ A project written before shapes existed: one outer boundary and its
        // holes, which is exactly "+ polygon, then a cut polygon per hole".
        // Read as that, so a saved session measures the same region it did.
        RegionShape outline;
        outline.points = pointsOf(roi[QStringLiteral("vertices")].toArray());
        if (outline.isValid()) {
            out.project.roi.shapes.append(outline);
            for (const QJsonValue &ring : roi[QStringLiteral("holes")].toArray()) {
                RegionShape hole;
                hole.role = RegionShape::Cut;
                hole.points = pointsOf(ring.toArray());
                if (hole.isValid())
                    out.project.roi.shapes.append(hole);
            }
        }
    }
    out.project.roi.limitation = roi[QStringLiteral("limitation")].toString();
    if (roi.contains(QStringLiteral("knownLength"))) {
        const QVector<QPoint> ends = pointsOf(roi[QStringLiteral("knownLength")].toArray());
        if (ends.size() == 2) {
            out.project.roi.knownLength.marked = true;
            out.project.roi.knownLength.a = ends.at(0);
            out.project.roi.knownLength.b = ends.at(1);
        }
    }
    out.project.roi.origin =
        RegionOfInterest::Origin(roi[QStringLiteral("origin")].toInt());

    out.project.settings =
        correlationSettingsFromJson(root[QStringLiteral("settings")].toObject());
    out.project.referenceUpdate =
        referencePolicyFromJson(root[QStringLiteral("referenceUpdate")].toObject());

    const QJsonArray gauges = root[QStringLiteral("extensometers")].toArray();
    for (const QJsonValue &value : gauges) {
        const QJsonObject entry = value.toObject();
        Extensometer gauge;
        gauge.name = entry[QStringLiteral("name")].toString();
        gauge.ax = entry[QStringLiteral("ax")].toDouble();
        gauge.ay = entry[QStringLiteral("ay")].toDouble();
        gauge.bx = entry[QStringLiteral("bx")].toDouble();
        gauge.by = entry[QStringLiteral("by")].toDouble();
        // A gauge of no length divides by zero computing strain. One cannot be
        // placed through the interface, but a file can be edited, and a bad
        // value should cost the gauge rather than the session.
        if (gauge.isValid())
            out.project.extensometers.append(gauge);
    }

    // Absent from a file written before probes existed, which then opens with
    // none; and a probe of no length, which only an edited file can hold, costs
    // the probe rather than the session.
    const QJsonObject probe = root[QStringLiteral("lineProbe")].toObject();
    if (!probe.isEmpty()) {
        LineProbe read;
        read.name = probe[QStringLiteral("name")].toString();
        read.ax = probe[QStringLiteral("ax")].toDouble();
        read.ay = probe[QStringLiteral("ay")].toDouble();
        read.bx = probe[QStringLiteral("bx")].toDouble();
        read.by = probe[QStringLiteral("by")].toDouble();
        if (read.isValid())
            out.project.probe = read;
    }

    return out;
}
