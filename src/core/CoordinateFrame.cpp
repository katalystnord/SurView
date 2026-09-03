#include "core/CoordinateFrame.h"

#include <QCoreApplication>

QString axisLabel(Axis axis)
{
    return axis == Axis::X ? QStringLiteral("x") : QStringLiteral("y");
}

QString axisDirection(Axis axis)
{
    return axis == Axis::X
               ? QCoreApplication::translate("CoordinateFrame", "right")
               // ⚑ Down, not up. The images, the viewport, the engine and the
               // exported file all agree on this, and a reader's instinct does
               // not, which is the entire reason any of this is on screen.
               : QCoreApplication::translate("CoordinateFrame", "down");
}

QString coordinateFrameCaption()
{
    return QCoreApplication::translate(
        "CoordinateFrame",
        "Image pixels: x right, y down, origin at the top-left pixel.");
}
