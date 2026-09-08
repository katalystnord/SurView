#include "core/PixelReadout.h"

#include <QLocale>
#include <QObject>
#include <QStringList>

#include <cmath>

namespace {

QString tr(const char *text)
{
    return QObject::tr(text);
}

// A stored value as a reader wants to see it: whole numbers for an integer
// image, and enough figures for a floating-point one to differ from its
// neighbours.
QString valueText(double value, const ImageRecord &record)
{
    if (record.hasTypeRange())
        return QString::number(qint64(std::llround(value)));
    return QString::number(value, 'g', 6);
}

}  // namespace

PixelReading pixelReading(const ImageRecord &record, int x, int y,
                          const QVector<double> &components)
{
    PixelReading reading;
    if (components.isEmpty())
        return reading;   // not over the picture: absent, never zero

    reading.valid = true;
    reading.x = x;
    reading.y = y;
    reading.components = components;

    for (double value : components) {
        // Against the extremes the image actually holds, which are frequently
        // not the type's: 12-bit sensor data in a 16-bit file clips at 4095.
        if (value <= record.dataMin)
            reading.atDataFloor = true;
        if (value >= record.dataMax)
            reading.atDataCeiling = true;

        if (record.hasTypeRange()) {
            if (value <= record.typeMin())
                reading.atTypeFloor = true;
            if (value >= record.typeMax())
                reading.atTypeCeiling = true;
        }
    }
    return reading;
}

QVector<ReadoutLine> pixelReadoutLines(const PixelReading &reading,
                                       const ImageRecord &record)
{
    QVector<ReadoutLine> lines;

    if (!reading.valid) {
        lines.append(ReadoutLine{
            tr("Camera"),
            tr("not over the picture"),
            tr("The pointer is off the image, so there is no pixel to read."),
            false});
        return lines;
    }

    QStringList values;
    for (double value : reading.components)
        values << valueText(value, record);

    // ⚑ ONE row, with the position in its own note rather than in a row of its
    // own. The measured-point readout below it already carries a position -- the
    // GRID point it is reporting on, which is a different one -- and two
    // headings each showing a pair of coordinates is the "two numbers for one
    // thing" confusion, in the one panel where a reader is comparing numbers.
    //
    // The file's own value, and the type it is stored in. What is on screen is a
    // stretched rendering of it, which the note says outright: a reader
    // comparing this number against what they can see needs to know the two are
    // not the same scale.
    // The position, then the type. Kept to one sentence apiece: the row sits in
    // a dock a few lines tall, and every line it takes is a line of the
    // measurement below it that a reader has to scroll for.
    QString note = tr("At pixel %1, %2. ").arg(reading.x).arg(reading.y)
                   + record.pixelTypeName();
    if (record.hasTypeRange()) {
        note = tr("%1, so this pixel could hold %2 to %3.")
                   .arg(note)
                   .arg(valueText(record.typeMin(), record))
                   .arg(valueText(record.typeMax(), record));
    }
    if (record.displayed
        && (record.displayMin != record.typeMin()
            || record.displayMax != record.typeMax())) {
        note += QLatin1Char(' ')
                + tr("The picture on screen is stretched from %1 to %2 for "
                     "display; this is what the file holds.")
                      .arg(valueText(record.displayMin, record))
                      .arg(valueText(record.displayMax, record));
    }

    lines.append(ReadoutLine{
        reading.components.size() > 1 ? tr("Camera, per channel") : tr("Camera"),
        values.join(tr(", ")), note, false});

    // ⚑ An extreme is a warning, not a value. Every pixel that ran out of range
    // holds the same number, so a subset over them has no gradient at all --
    // the correlation there has nothing to lock onto, however good the speckle
    // looked before the exposure was set.
    if (reading.atDataFloor || reading.atDataCeiling) {
        const bool ceiling = reading.atDataCeiling;
        const qint64 shared = ceiling ? record.pixelsAtDataMax : record.pixelsAtDataMin;
        const double share = ceiling ? record.fractionAtDataMax()
                                     : record.fractionAtDataMin();

        QString warning = ceiling
                              ? tr("at the highest value in this image")
                              : tr("at the lowest value in this image");
        if ((ceiling && reading.atTypeCeiling) || (!ceiling && reading.atTypeFloor)) {
            // A stronger statement than the image's own extreme, and a
            // different one: the type's limit is where the file itself stops.
            warning += tr(", which is the pixel type's own limit");
        }

        QString consequence =
            tr("Where the sensor ran out of range the speckle is gone rather "
               "than merely bright or dark: every such pixel holds the same "
               "number, so a subset over them carries no gradient for the "
               "correlation to lock onto.");
        if (record.extremesCounted && record.pixelCount > 0) {
            consequence = tr("%1 pixels share it, %2% of the image. ")
                              .arg(QLocale().toString(shared))
                              .arg(share * 100.0, 0, 'f', 1)
                          + consequence;
        }

        lines.append(ReadoutLine{tr("Exposure"), warning, consequence, true});
    }

    return lines;
}
