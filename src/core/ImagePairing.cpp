#include "core/ImagePairing.h"

#include "core/ImageRecord.h"
#include "core/SessionAlignment.h"

#include <QCoreApplication>
#include <QObject>

PairCompatibility compareToReference(const ImageRecord &reference,
                                     const ImageRecord &target,
                                     bool separateSessions)
{
    PairCompatibility result;

    if (!reference.isValid() || !target.isValid())
        return result;  // nothing to compare against yet; say nothing

    // Across separate sessions the camera moved, and the size may change with
    // it; the run aligns the two. Within one session it is a different picture.
    if (!separateSessions
        && (reference.width != target.width || reference.height != target.height)) {
        result.mismatches << QObject::tr(
            "dimensions differ: reference %1 × %2 px, target %3 × %4 px (if the "
            "camera was moved between two sessions, turn on \"%5\" in the "
            "Reference section of the Analysis panel)")
                                 .arg(reference.width)
                                 .arg(reference.height)
                                 .arg(target.width)
                                 .arg(target.height)
                                 .arg(separateSessionsLabel());
    }

    if (reference.components != target.components) {
        result.mismatches << QObject::tr(
            "channels differ: reference %1, target %2")
                                 .arg(reference.channelsText(),
                                      target.channelsText());
    }

    // Worth reporting even though both would decode: correlating an 8-bit
    // target against a 16-bit reference silently compares two different
    // intensity scales.
    if (reference.scalarType != target.scalarType) {
        result.mismatches << QObject::tr(
            "pixel type differs: reference %1, target %2")
                                 .arg(reference.pixelTypeName(),
                                      target.pixelTypeName());
    }

    return result;
}
