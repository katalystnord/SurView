#pragma once

#include "core/Correlation.h"
#include "core/ImageRecord.h"

#include <QString>

// Writing a measured field out as VTK's own unstructured-grid format, which is
// what ParaView and FreeCAD's FEM workbench open directly.
//
// This is the point at which the measurement stops being ours. Everything it
// carries has to survive without us: a value that was never measured must not
// arrive as a zero another program will colour like a result, and the file has
// to state how it was made, because a displacement field with no reference
// image, no solver and no settings is a picture rather than a measurement.

// How the field was produced, written into the file alongside it. Tenet 10:
// provenance travels with the record, and an export is the moment the record
// leaves the only place that knows any of this.
struct FieldProvenance
{
    ImageRecord reference;
    ImageRecord target;
    CorrelationSettings settings;

    QString applicationVersion;
    QString enginePin;   // the OpenCorr commit this build is pinned to
};

// Writes `result` to `path`. Returns an empty string on success, otherwise the
// reason, phrased for a user and naming the file.
//
// Refuses a result with no points rather than writing an empty file: an empty
// export succeeds, and the emptiness is then discovered somewhere else.
QString writeFieldVtu(const QString &path, const CorrelationResult &result,
                      const FieldProvenance &provenance);
