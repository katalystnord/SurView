#include "core/SessionAlignment.h"

#include <QCoreApplication>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("SessionAlignment", text);
}

QString fittedOnText(const SessionAlignmentRecord &alignment)
{
    switch (alignment.fittedOn) {
    case SessionAlignmentRecord::Ground:
        return alignment.ground.isEmpty()
            ? tr("the ground marked as stationary")
            : tr("the ground marked as stationary (%1)").arg(alignment.ground);
    case SessionAlignmentRecord::MeasuredRegion:
        return tr("the region being measured, since no ground was marked");
    case SessionAlignmentRecord::WholeImage:
        return tr("the whole image, since no ground or region was marked");
    case SessionAlignmentRecord::NotAligned:
        break;
    }
    return QString();
}

}  // namespace

QString alignmentLimitation(const SessionAlignmentRecord &alignment)
{
    if (alignment.fittedOn != SessionAlignmentRecord::MeasuredRegion
        && alignment.fittedOn != SessionAlignmentRecord::WholeImage)
        return QString();
    if (alignment.knownLengthUsed) {
        return tr("The camera move was fitted on the specimen itself, with its scale "
                  "taken from the known length, so the specimen's rigid motion is not "
                  "measured: it was taken for the camera. Its strain is measured, the "
                  "uniform dilatation included. %1 on the toolbar marks ground that did "
                  "not move, so that the rigid motion is measured too.")
            .arg(markGroundLabel());
    }
    return tr("The camera move was fitted on the specimen itself, so the specimen's "
              "rigid motion and its uniform dilatation (the part of the strain that "
              "is the same in every direction) are not measured: both were taken "
              "for the camera. Strain differences between directions and shear are "
              "measured. %1 on the toolbar marks ground that did not move, so that "
              "all of it is measured; where there is none, a known length from the "
              "same menu restores the dilatation.").arg(markGroundLabel());
}

QStringList alignmentReport(const SessionAlignmentRecord &alignment)
{
    if (!alignment.isAligned())
        return {};
    QStringList lines;
    lines << tr("Target from a separate session (%1 x %2 px), aligned on %3: scale "
                "%4, turned %5 degrees, shifted (%6, %7) px.")
                 .arg(alignment.targetWidth)
                 .arg(alignment.targetHeight)
                 .arg(fittedOnText(alignment))
                 .arg(alignment.scale, 0, 'f', 5)
                 .arg(alignment.angleDegrees, 0, 'f', 4)
                 .arg(alignment.shiftX, 0, 'f', 3)
                 .arg(alignment.shiftY, 0, 'f', 3);
    lines << tr("Alignment: %1 feature matches, %2 agreeing; refined on %3 of %4 "
                "correlated subsets, %5 px rms about the fit. No image was resampled.")
                 .arg(alignment.matches)
                 .arg(alignment.inliers)
                 .arg(alignment.groundPointsUsed)
                 .arg(alignment.groundPoints)
                 .arg(alignment.residualRms, 0, 'f', 4);
    if (alignment.knownLengthUsed) {
        // A length L found to within e px at each end moves the scale by at most
        // e / L either way, and the dilatation with it: arithmetic, not an estimate.
        lines << tr("Camera scale taken from the known length, (%1, %2) to "
                    "(%3, %4): %5 px in the reference, %6 px in the target, its ends "
                    "correlating at %7 or better. Each 0.01 px of error in finding "
                    "either end changes the measured dilatation by at most %8.")
                     .arg(alignment.knownLengthA.x())
                     .arg(alignment.knownLengthA.y())
                     .arg(alignment.knownLengthB.x())
                     .arg(alignment.knownLengthB.y())
                     .arg(alignment.knownLengthReference, 0, 'f', 1)
                     .arg(alignment.knownLengthTarget, 0, 'f', 1)
                     .arg(alignment.knownLengthCorrelation, 0, 'f', 4)
                     .arg(0.01 / alignment.knownLengthTarget, 0, 'g', 2);
    } else if (alignment.knownLengthMarked) {
        lines << tr("The known length marked was not used: the camera move was fitted "
                    "on ground, which measures the scale itself.");
    }
    // The field is not in the target's pixels, which is where a reader holding
    // the target image would otherwise look for it.
    lines << tr("Positions and displacements are in the reference image's pixels, "
                "with the camera move taken out.");
    const QString limitation = alignmentLimitation(alignment);
    if (!limitation.isEmpty())
        lines << limitation;
    return lines;
}

QString alignmentStatement(const SessionAlignmentRecord &alignment)
{
    if (!alignment.isAligned())
        return tr("none: the reference and the target share one camera and one "
                  "pixel grid.");
    return alignmentReport(alignment).join(QLatin1Char(' '));
}

QString separateSessionsLabel()
{
    return tr("Reference and target from separate sessions");
}

QString markGroundLabel()
{
    return tr("Mark Ground");
}

QString markKnownLengthLabel()
{
    return tr("Mark Known Length");
}

QString removeKnownLengthLabel()
{
    return tr("Remove Known Length");
}

QString markGroundUnavailableNote()
{
    return tr("Ground is what a target from a separate session is aligned on. Turn "
              "on \"%1\" in the Reference section of the Analysis panel to mark it.")
        .arg(separateSessionsLabel());
}

QString separateSessionsPanelNote(bool on, bool groundMarked, bool knownLengthMarked)
{
    if (!on) {
        return tr("Off: the reference and every target share one camera and one "
                  "pixel grid. Turn this on for a target photographed after the "
                  "camera was moved, refocused or replaced; it may then differ in "
                  "size.");
    }
    const QString how = tr("On: each target is aligned to the reference before it is "
                           "measured, by the camera's scale, turn and shift, and "
                           "measured as it was recorded, with nothing resampled.");
    if (groundMarked) {
        return tr("%1 The camera move is fitted on the ground marked, so the "
                  "specimen's own movement is measured in full.%2").arg(how)
            .arg(knownLengthMarked
                     ? tr(" The known length marked is not needed and not used.")
                     : QString());
    }
    if (knownLengthMarked) {
        return tr("%1 With no stationary ground marked, the camera move is fitted on "
                  "what is being measured, with its scale taken from the known length "
                  "marked: the specimen's rigid motion is taken up and its strain is "
                  "measured. %2 on the toolbar marks ground that did not move, such "
                  "as the bench or a fixture, so that the rigid motion is measured too.")
            .arg(how, markGroundLabel());
    }
    return tr("%1 With no stationary ground marked, the camera move is fitted on what "
              "is being measured, which takes up the specimen's own rigid motion "
              "and uniform stretch; the report says so. %2 on the toolbar marks "
              "ground that did not move, such as the bench or a fixture; where there "
              "is none, %3 in the same menu marks a length that did not change, "
              "which restores the stretch.")
        .arg(how, markGroundLabel(), markKnownLengthLabel());
}

QString separateSessionsReanchorNote()
{
    return tr("Across separate sessions each target is measured against the "
              "reference one pair at a time, so the reference is never re-anchored.");
}
