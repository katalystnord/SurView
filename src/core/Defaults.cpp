#include "core/Defaults.h"

#include "core/Masking.h"
#include "core/SessionAlignment.h"
#include "core/StrainFit.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>

namespace {

constexpr const char *kMagic = "SurView DIC defaults";
constexpr int kVersion = 1;

QString &pathOverride()
{
    static QString path;
    return path;
}

QString px(int value) { return QObject::tr("%1 px").arg(value); }
QString onOff(bool value) { return value ? QObject::tr("on") : QObject::tr("off"); }
QString fixed(double value, int decimals) { return QString::number(value, 'f', decimals); }
// As the Analysis panel's own boxes write them, so the two read alike.
QString count(int value, const QString &one, const QString &many)
{
    return QStringLiteral("%1 %2").arg(value).arg(value == 1 ? one : many);
}
QString convergence(double value)
{
    return QObject::tr("‖Δp‖ ≤ %1").arg(fixed(value, 4));
}

} // namespace

QString defaultsPath()
{
    if (!pathOverride().isEmpty())
        return pathOverride();
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath(QStringLiteral("defaults.json"));
}

void setDefaultsPathForTesting(const QString &path)
{
    pathOverride() = path;
}

DefaultsLoad loadDefaults(const QString &path)
{
    DefaultsLoad out;
    const QString name = QDir::toNativeSeparators(path);

    QFile file(path);
    if (!file.exists())
        return out;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        out.problem = QObject::tr("The saved defaults in %1 could not be read (%2), so "
                                  "SurView's own apply.")
                          .arg(name, file.errorString());
        return out;
    }

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    const QJsonObject root = document.object();
    if (error.error != QJsonParseError::NoError || !document.isObject()
        || root[QStringLiteral("format")].toString() != QString::fromLatin1(kMagic)) {
        out.problem = QObject::tr("%1 does not hold SurView defaults, so SurView's own "
                                  "apply.")
                          .arg(name);
        return out;
    }
    if (root[QStringLiteral("version")].toInt() > kVersion) {
        out.problem = QObject::tr("The defaults in %1 were saved by a newer version of "
                                  "SurView, so SurView's own apply.")
                          .arg(name);
        return out;
    }

    out.defaults.settings =
        correlationSettingsFromJson(root[QStringLiteral("settings")].toObject());
    out.defaults.referenceUpdate =
        referencePolicyFromJson(root[QStringLiteral("referenceUpdate")].toObject());
    out.saved = true;
    return out;
}

QString saveDefaults(const QString &path, const RunDefaults &defaults)
{
    const QString name = QDir::toNativeSeparators(path);
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
        return QObject::tr("Could not make the folder for %1.").arg(name);

    QJsonObject root;
    root[QStringLiteral("format")] = QString::fromLatin1(kMagic);
    root[QStringLiteral("version")] = kVersion;
    root[QStringLiteral("settings")] = correlationSettingsToJson(defaults.settings);
    root[QStringLiteral("referenceUpdate")] = referencePolicyToJson(defaults.referenceUpdate);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return QObject::tr("Could not write %1: %2.").arg(name, file.errorString());
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    if (file.error() != QFileDevice::NoError)
        return QObject::tr("Could not finish writing %1: %2.").arg(name, file.errorString());
    return QString();
}

QString forgetDefaults(const QString &path)
{
    QFile file(path);
    if (!file.exists() || file.remove())
        return QString();
    return QObject::tr("Could not remove %1: %2.")
        .arg(QDir::toNativeSeparators(path), file.errorString());
}

QVector<SettingLine> settingLines(const RunDefaults &defaults)
{
    const CorrelationSettings &s = defaults.settings;
    const ReferenceUpdatePolicy &p = defaults.referenceUpdate;
    const RunDefaults ownDefaults;
    const CorrelationSettings &o = ownDefaults.settings;
    const ReferenceUpdatePolicy &op = ownDefaults.referenceUpdate;

    const QString correlation = QObject::tr("Correlation");
    const QString strain = QObject::tr("Strain");
    const QString reference = QObject::tr("Reference");
    const QString failed = QObject::tr("Points that failed");

    // The labels are the Analysis panel's own, so a line here names the control
    // a reader would change; the walkthrough holds the two to each other.
    return {
        {correlation, QObject::tr("Solver"), solverDisplayName(s.solver),
         solverDisplayName(o.solver)},
        {correlation, QObject::tr("Shape function"), shapeOrderName(s.shapeOrder),
         shapeOrderName(o.shapeOrder)},
        {correlation, QObject::tr("Subset radius"), px(s.subsetRadius), px(o.subsetRadius)},
        {correlation, QObject::tr("Grid step"), px(s.gridStep), px(o.gridStep)},
        {correlation, QObject::tr("Max iterations"), count(s.maxIterations, QObject::tr("iteration"), QObject::tr("iterations")),
         count(o.maxIterations, QObject::tr("iteration"), QObject::tr("iterations"))},
        {correlation, QObject::tr("Convergence"), convergence(s.convergence),
         convergence(o.convergence)},
        {correlation, maskAtCutsLabel(), onOff(s.maskAtCuts), onOff(o.maskAtCuts)},
        {correlation, minValidShareLabel(),
         QObject::tr("%1 %").arg(qRound(s.minValidShare * 100.0)),
         QObject::tr("%1 %").arg(qRound(o.minValidShare * 100.0))},
        {strain, QObject::tr("Fit strain from the displacement field"),
         onOff(s.strainEnabled), onOff(o.strainEnabled)},
        {strain, QObject::tr("Subregion radius"),
         QObject::tr("%1 px").arg(fixed(s.strainRadius, 1)),
         QObject::tr("%1 px").arg(fixed(o.strainRadius, 1))},
        {strain, QObject::tr("Fewest points in the fit"),
         count(s.strainMinPoints, QObject::tr("point"), QObject::tr("points")),
         count(o.strainMinPoints, QObject::tr("point"), QObject::tr("points"))},
        {strain, QObject::tr("Strain measure"), strainMeasureName(s.strainMeasure),
         strainMeasureName(o.strainMeasure)},
        {reference, separateSessionsLabel(), onOff(s.separateSessions),
         onOff(o.separateSessions)},
        {reference, QObject::tr("Re-anchor the reference when tracking degrades"),
         onOff(p.enabled), onOff(op.enabled)},
        {reference, QObject::tr("Correlation a point must keep"),
         QObject::tr("zncc ≥ %1").arg(fixed(p.znccThreshold, 2)),
         QObject::tr("zncc ≥ %1").arg(fixed(op.znccThreshold, 2))},
        {reference, QObject::tr("Share of points that must keep it"),
         QObject::tr("%1 %").arg(qRound(p.percentile * 100.0)),
         QObject::tr("%1 %").arg(qRound(op.percentile * 100.0))},
        {failed, QObject::tr("Try again at points that correlated poorly"),
         onOff(s.recovery.enabled), onOff(o.recovery.enabled)},
        {failed, QObject::tr("Try again below this correlation"),
         QObject::tr("zncc < %1").arg(fixed(s.recovery.retryBelowZncc, 2)),
         QObject::tr("zncc < %1").arg(fixed(o.recovery.retryBelowZncc, 2))},
        {failed, QObject::tr("Fit from points at or above"),
         QObject::tr("zncc ≥ %1").arg(fixed(s.recovery.reliableZncc, 2)),
         QObject::tr("zncc ≥ %1").arg(fixed(o.recovery.reliableZncc, 2))},
        {failed, QObject::tr("Most rounds to try"),
         count(s.recovery.maxRounds, QObject::tr("round"), QObject::tr("rounds")),
         count(o.recovery.maxRounds, QObject::tr("round"), QObject::tr("rounds"))},
    };
}

QJsonObject correlationSettingsToJson(const CorrelationSettings &s)
{
    QJsonObject settings;
    settings[QStringLiteral("solver")] = int(s.solver);
    settings[QStringLiteral("shapeOrder")] = s.shapeOrder;
    settings[QStringLiteral("subsetRadius")] = s.subsetRadius;
    settings[QStringLiteral("gridStep")] = s.gridStep;
    settings[QStringLiteral("maxIterations")] = s.maxIterations;
    settings[QStringLiteral("convergence")] = s.convergence;
    settings[QStringLiteral("maskAtCuts")] = s.maskAtCuts;
    settings[QStringLiteral("minValidShare")] = s.minValidShare;
    settings[QStringLiteral("separateSessions")] = s.separateSessions;
    settings[QStringLiteral("strainEnabled")] = s.strainEnabled;
    settings[QStringLiteral("strainRadius")] = s.strainRadius;
    settings[QStringLiteral("strainMinPoints")] = s.strainMinPoints;
    settings[QStringLiteral("strainMeasure")] = int(s.strainMeasure);
    settings[QStringLiteral("recoveryEnabled")] = s.recovery.enabled;
    settings[QStringLiteral("recoveryRetryBelow")] = s.recovery.retryBelowZncc;
    settings[QStringLiteral("recoveryReliable")] = s.recovery.reliableZncc;
    settings[QStringLiteral("recoveryMaxRounds")] = s.recovery.maxRounds;
    return settings;
}

CorrelationSettings correlationSettingsFromJson(const QJsonObject &settings)
{
    // Absent keys keep SurView's own value, so a file written before a setting
    // existed opens with that setting where it would have been.
    CorrelationSettings s;
    s.solver = CorrelationSettings::Solver(settings[QStringLiteral("solver")].toInt(int(s.solver)));
    s.shapeOrder = settings[QStringLiteral("shapeOrder")].toInt(s.shapeOrder);
    s.subsetRadius = settings[QStringLiteral("subsetRadius")].toInt(s.subsetRadius);
    s.gridStep = settings[QStringLiteral("gridStep")].toInt(s.gridStep);
    s.maxIterations = settings[QStringLiteral("maxIterations")].toInt(s.maxIterations);
    s.convergence = settings[QStringLiteral("convergence")].toDouble(s.convergence);
    s.maskAtCuts = settings[QStringLiteral("maskAtCuts")].toBool(s.maskAtCuts);
    s.minValidShare = settings[QStringLiteral("minValidShare")].toDouble(s.minValidShare);
    s.separateSessions =
        settings[QStringLiteral("separateSessions")].toBool(s.separateSessions);
    s.strainEnabled = settings[QStringLiteral("strainEnabled")].toBool(s.strainEnabled);
    s.strainRadius = settings[QStringLiteral("strainRadius")].toDouble(s.strainRadius);
    s.strainMinPoints = settings[QStringLiteral("strainMinPoints")].toInt(s.strainMinPoints);
    s.strainMeasure =
        StrainMeasure(settings[QStringLiteral("strainMeasure")].toInt(int(s.strainMeasure)));
    s.recovery.enabled = settings[QStringLiteral("recoveryEnabled")].toBool(s.recovery.enabled);
    s.recovery.retryBelowZncc =
        settings[QStringLiteral("recoveryRetryBelow")].toDouble(s.recovery.retryBelowZncc);
    s.recovery.reliableZncc =
        settings[QStringLiteral("recoveryReliable")].toDouble(s.recovery.reliableZncc);
    s.recovery.maxRounds =
        settings[QStringLiteral("recoveryMaxRounds")].toInt(s.recovery.maxRounds);
    return s;
}

QJsonObject referencePolicyToJson(const ReferenceUpdatePolicy &p)
{
    QJsonObject policy;
    policy[QStringLiteral("enabled")] = p.enabled;
    policy[QStringLiteral("znccThreshold")] = p.znccThreshold;
    policy[QStringLiteral("percentile")] = p.percentile;
    return policy;
}

ReferenceUpdatePolicy referencePolicyFromJson(const QJsonObject &policy)
{
    ReferenceUpdatePolicy p;
    p.enabled = policy[QStringLiteral("enabled")].toBool(p.enabled);
    p.znccThreshold = policy[QStringLiteral("znccThreshold")].toDouble(p.znccThreshold);
    p.percentile = policy[QStringLiteral("percentile")].toDouble(p.percentile);
    return p;
}
