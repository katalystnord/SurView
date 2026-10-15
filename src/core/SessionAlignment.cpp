#include "core/SessionAlignment.h"

#include <QCoreApplication>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("SessionAlignment", text);
}

QString fittedOnText(SessionAlignmentRecord::FittedOn on)
{
    switch (on) {
    case SessionAlignmentRecord::Ground:
        return tr("the ground marked as stationary");
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
    return tr("The camera move was fitted on the specimen itself, so the specimen's "
              "rigid motion and its uniform dilatation (the part of the strain that "
              "is the same in every direction) are not measured: both were taken "
              "for the camera. Strain differences between directions and shear are "
              "measured. Mark ground that did not move to measure all of it.");
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
                 .arg(fittedOnText(alignment.fittedOn))
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
    const QString limitation = alignmentLimitation(alignment);
    if (!limitation.isEmpty())
        lines << limitation;
    return lines;
}

QString separateSessionsLabel()
{
    return tr("Reference and target from separate sessions");
}

QString separateSessionsPanelNote(bool on)
{
    if (!on) {
        return tr("Off: the reference and every target share one camera and one "
                  "pixel grid. Turn this on for a target photographed after the "
                  "camera was moved, refocused or replaced; it may then differ in "
                  "size.");
    }
    return tr("On: each target is aligned to the reference before it is measured, "
              "by the camera's scale, turn and shift, and measured as it was "
              "recorded, with nothing resampled. The camera move is fitted on what "
              "is being measured, which takes up the specimen's own rigid motion "
              "and uniform stretch; the report says so.");
}

QString separateSessionsReanchorNote()
{
    return tr("Across separate sessions each target is measured against the "
              "reference one pair at a time, so the reference is never re-anchored.");
}
