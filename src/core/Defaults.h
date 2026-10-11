#pragma once

#include "core/Correlation.h"
#include "core/ReferenceUpdate.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

// The settings a new project starts from, kept between sessions.
//
// ⚑ A RUN SETTING BELONGS TO THE PROJECT; ONLY ITS STARTING VALUE BELONGS HERE.
// Every setting that changes a measurement is edited in the Analysis panel and
// saved with the project and in the provenance of every exported file, so a
// project measures the same way on any machine. What is kept here is only where
// a NEW project starts. A project that is opened keeps the settings it was saved
// with, whatever the defaults say -- otherwise one person's defaults would
// silently change another person's measurement.
//
// ⚑ AND DEFAULTS THAT DIFFER FROM SURVIEW'S OWN ARE NEVER SILENT. A window that
// starts with a 20 px subset because of a choice made weeks ago reads exactly
// like one that starts with SurView's own 16 px. settingLines() names each
// setting with both values, so the Settings window and the log can say which
// ones a saved default changed.

struct RunDefaults
{
    CorrelationSettings settings;
    ReferenceUpdatePolicy referenceUpdate;
};

// What reading the defaults produced.
struct DefaultsLoad
{
    // SurView's own when nothing usable was saved.
    RunDefaults defaults;

    // Whether `defaults` came from a saved file.
    bool saved = false;

    // Why a saved file was not used, phrased for a user and naming the file.
    // Empty when it was used or when there was none: having no saved defaults
    // is ordinary, and not a problem to report.
    QString problem;
};

// Where the defaults are kept on this machine: the application's own
// configuration folder. Tests point it elsewhere with setDefaultsPathForTesting().
QString defaultsPath();
void setDefaultsPathForTesting(const QString &path);

DefaultsLoad loadDefaults(const QString &path);

// Writes `defaults` to `path`, creating its folder. Empty on success, otherwise
// the reason.
QString saveDefaults(const QString &path, const RunDefaults &defaults);

// Removes saved defaults, so SurView's own apply again. Empty on success or when
// there was nothing to remove, otherwise the reason.
QString forgetDefaults(const QString &path);

// One setting as a reader sees it: the Analysis panel's own label, the value in
// `defaults`, and SurView's own value, in the same words the panel uses.
struct SettingLine
{
    QString section;   // the Analysis panel section holding the control
    QString label;     // the control's own label in that section
    QString value;
    QString builtIn;
    bool differs() const { return value != builtIn; }
};

// Every run setting, in the order the Analysis panel shows them.
QVector<SettingLine> settingLines(const RunDefaults &defaults);

// The run settings as stored in a project file and in the defaults file. One
// serialisation, so the two files cannot come to disagree about a setting.
QJsonObject correlationSettingsToJson(const CorrelationSettings &settings);
CorrelationSettings correlationSettingsFromJson(const QJsonObject &json);
QJsonObject referencePolicyToJson(const ReferenceUpdatePolicy &policy);
ReferenceUpdatePolicy referencePolicyFromJson(const QJsonObject &json);
