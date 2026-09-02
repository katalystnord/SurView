#pragma once

#include "core/Correlation.h"
#include "core/ReferenceUpdate.h"
#include "core/Roi.h"
#include "core/Series.h"

#include <QString>
#include <QStringList>

// A saved session: which images, which region, which settings.
//
// New, Open and Save Project were on the File menu from the first window and
// did nothing but report themselves unimplemented. This is the promise being
// kept.
//
// ⚑ A project stores WHERE the images are, never the images themselves.
// Embedding them would make the file enormous and let it drift from the
// pictures on disk without saying so. Storing paths creates the opposite risk,
// and it is the one this file is built around: images can move, or change,
// between saving and opening. Each is recorded with the SHA-256 it had, so a
// session that opens against different pixels says so instead of measuring them
// quietly.
//
// Results are not saved. They are reproducible from what is here, and a file
// that carried a field would go stale against its own inputs the moment either
// changed. The .vtu export is where a measurement goes to be kept.

struct Project
{
    QString referencePath;
    QStringList targetPaths;
    RegionOfInterest roi;
    CorrelationSettings settings;
    ReferenceUpdatePolicy referenceUpdate;

    // The virtual extensometers placed on the reference image. Part of the
    // session, not of a run: placing one takes careful clicking, and a session
    // that reopened without them would have lost work.
    QVector<Extensometer> extensometers;
};

// What opening a project produced, and what was wrong with it.
struct ProjectLoad
{
    Project project;

    // Fatal: nothing was opened. Phrased for a user and naming the file.
    QString failure;

    // Images the project named that are no longer there, phrased for a user.
    // Reported rather than silently dropped: a sequence one frame shorter than
    // it was is a different measurement wearing the same name.
    QStringList missing;

    // Images that are there and no longer hold the pixels they did when the
    // project was saved.
    QStringList changed;

    bool ok() const { return failure.isEmpty(); }
};

// Writes `project` to `path`. Empty on success, otherwise the reason.
//
// Image paths are stored relative to the project file wherever they sit at or
// below it, so a folder holding a project and its images can be copied to
// another machine and still open.
QString saveProject(const QString &path, const Project &project);

ProjectLoad loadProject(const QString &path);
