#pragma once

#include "core/ImageRecord.h"
#include "core/PointReadout.h"

#include <QString>
#include <QVector>

// What the camera recorded at one pixel, before anything has been measured
// from it.
//
// WHY THIS EXISTS. The point readout answers what a CORRELATION found, and it
// has nothing to say until a run has been sat through. The question that comes
// first is about the photograph: is this exposed properly, does the speckle
// here carry contrast, has the sensor run out of range in this corner? The
// Record panel answers that for the image as a whole and cannot answer it
// anywhere in particular.
//
// ⚑ THE NUMBER IS THE FILE'S OWN, NOT THE ONE ON SCREEN. The viewport stretches
// the intensity window so a 16-bit image sitting in 0-5000 is not rendered very
// nearly black, so what a reader SEES is a mapping and what correlation uses is
// the file's value. A readout that reported screen brightness would be reporting
// on the rendering.
//
// ⚑ AND SITTING AT AN EXTREME IS A WARNING, NOT A VALUE. Where the sensor ran
// out of range, the speckle in those pixels is gone rather than merely bright:
// every clipped pixel holds the same number, so a subset over them has no
// gradient at all and the correlation there has nothing to lock onto. Judged
// against the extremes the image ACTUALLY holds rather than only against the
// type's limits, for the reason ImageRecord already gives: 12-bit sensor data
// stored in a 16-bit file clips at 4095 while the type allows 65535, and a rule
// that only knew about 65535 would report no clipping at all.

struct PixelReading
{
    bool valid = false;

    int x = 0;
    int y = 0;

    // As the file holds them, one per channel: a grey image has one, a colour
    // image three. Never combined into a brightness -- that would be a number
    // the file does not contain.
    QVector<double> components;

    // Any channel sitting on the lowest or highest value present in this image.
    bool atDataFloor = false;
    bool atDataCeiling = false;

    // ...and on the pixel type's own limit, which is a stronger statement and
    // is only available for an integer type.
    bool atTypeFloor = false;
    bool atTypeCeiling = false;
};

// The reading for one pixel of `record`, given the values the decoder holds
// there. An empty `components` means the position is not over the picture, and
// gives an invalid reading rather than a reading of zero.
PixelReading pixelReading(const ImageRecord &record, int x, int y,
                          const QVector<double> &components);

// What that reading has to say, in the same shape the measured-point readout
// uses, so one panel can show both without two kinds of row.
//
// An invalid reading gives one line saying the pointer is not over the picture,
// rather than nothing: a panel that empties itself looks broken where a panel
// that says so has answered.
QVector<ReadoutLine> pixelReadoutLines(const PixelReading &reading,
                                       const ImageRecord &record);
