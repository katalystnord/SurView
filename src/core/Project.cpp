#include "core/Project.h"

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
constexpr int kVersion = 1;

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

    QJsonArray vertices;
    for (const QPoint &vertex : project.roi.vertices) {
        QJsonArray point;
        point.append(vertex.x());
        point.append(vertex.y());
        vertices.append(point);
    }
    QJsonObject roi;
    roi[QStringLiteral("vertices")] = vertices;
    roi[QStringLiteral("origin")] = int(project.roi.origin);
    // Written in the same [x, y] form the outer boundary uses, so one reader
    // serves both and the two cannot drift apart.
    QJsonArray holes;
    for (const QVector<QPoint> &hole : project.roi.holes) {
        QJsonArray ring;
        for (const QPoint &vertex : hole) {
            QJsonArray point;
            point.append(vertex.x());
            point.append(vertex.y());
            ring.append(point);
        }
        holes.append(ring);
    }
    roi[QStringLiteral("holes")] = holes;
    root[QStringLiteral("region")] = roi;

    const CorrelationSettings &s = project.settings;
    QJsonObject settings;
    settings[QStringLiteral("solver")] = int(s.solver);
    settings[QStringLiteral("shapeOrder")] = s.shapeOrder;
    settings[QStringLiteral("subsetRadius")] = s.subsetRadius;
    settings[QStringLiteral("gridStep")] = s.gridStep;
    settings[QStringLiteral("maxIterations")] = s.maxIterations;
    settings[QStringLiteral("convergence")] = s.convergence;
    settings[QStringLiteral("strainEnabled")] = s.strainEnabled;
    settings[QStringLiteral("strainRadius")] = s.strainRadius;
    settings[QStringLiteral("strainMinPoints")] = s.strainMinPoints;
    settings[QStringLiteral("strainMeasure")] = int(s.strainMeasure);
    settings[QStringLiteral("recoveryEnabled")] = s.recovery.enabled;
    settings[QStringLiteral("recoveryRetryBelow")] = s.recovery.retryBelowZncc;
    settings[QStringLiteral("recoveryReliable")] = s.recovery.reliableZncc;
    settings[QStringLiteral("recoveryMaxRounds")] = s.recovery.maxRounds;
    root[QStringLiteral("settings")] = settings;

    const ReferenceUpdatePolicy &p = project.referenceUpdate;
    QJsonObject policy;
    policy[QStringLiteral("enabled")] = p.enabled;
    policy[QStringLiteral("znccThreshold")] = p.znccThreshold;
    policy[QStringLiteral("percentile")] = p.percentile;
    root[QStringLiteral("referenceUpdate")] = policy;

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
    for (const QJsonValue &value : roi[QStringLiteral("vertices")].toArray()) {
        const QJsonArray point = value.toArray();
        out.project.roi.vertices << QPoint(point.at(0).toInt(), point.at(1).toInt());
    }
    for (const QJsonValue &ringValue : roi[QStringLiteral("holes")].toArray()) {
        QVector<QPoint> hole;
        for (const QJsonValue &value : ringValue.toArray()) {
            const QJsonArray point = value.toArray();
            hole << QPoint(point.at(0).toInt(), point.at(1).toInt());
        }
        // A ring of fewer than three corners encloses nothing, so it is dropped
        // on the way in rather than carried as something to keep re-checking.
        if (hole.size() >= 3)
            out.project.roi.holes.append(hole);
    }
    out.project.roi.origin =
        RegionOfInterest::Origin(roi[QStringLiteral("origin")].toInt());

    const QJsonObject settings = root[QStringLiteral("settings")].toObject();
    CorrelationSettings &s = out.project.settings;
    s.solver = CorrelationSettings::Solver(settings[QStringLiteral("solver")].toInt());
    s.shapeOrder = settings[QStringLiteral("shapeOrder")].toInt(s.shapeOrder);
    s.subsetRadius = settings[QStringLiteral("subsetRadius")].toInt(s.subsetRadius);
    s.gridStep = settings[QStringLiteral("gridStep")].toInt(s.gridStep);
    s.maxIterations = settings[QStringLiteral("maxIterations")].toInt(s.maxIterations);
    s.convergence = settings[QStringLiteral("convergence")].toDouble(s.convergence);
    s.strainEnabled = settings[QStringLiteral("strainEnabled")].toBool(s.strainEnabled);
    s.strainRadius = settings[QStringLiteral("strainRadius")].toDouble(s.strainRadius);
    s.strainMinPoints =
        settings[QStringLiteral("strainMinPoints")].toInt(s.strainMinPoints);
    s.strainMeasure =
        StrainMeasure(settings[QStringLiteral("strainMeasure")].toInt(int(s.strainMeasure)));
    s.recovery.enabled =
        settings[QStringLiteral("recoveryEnabled")].toBool(s.recovery.enabled);
    s.recovery.retryBelowZncc =
        settings[QStringLiteral("recoveryRetryBelow")].toDouble(s.recovery.retryBelowZncc);
    s.recovery.reliableZncc =
        settings[QStringLiteral("recoveryReliable")].toDouble(s.recovery.reliableZncc);
    s.recovery.maxRounds =
        settings[QStringLiteral("recoveryMaxRounds")].toInt(s.recovery.maxRounds);

    const QJsonObject policy = root[QStringLiteral("referenceUpdate")].toObject();
    ReferenceUpdatePolicy &p = out.project.referenceUpdate;
    p.enabled = policy[QStringLiteral("enabled")].toBool(p.enabled);
    p.znccThreshold = policy[QStringLiteral("znccThreshold")].toDouble(p.znccThreshold);

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
    p.percentile = policy[QStringLiteral("percentile")].toDouble(p.percentile);

    return out;
}
