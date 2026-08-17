#include "ImageRecord.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QLocale>

#include <vtkDataArray.h>
#include <vtkType.h>

ImageRecord ImageRecord::fromFile(const QString &path)
{
    ImageRecord record;

    const QFileInfo info(path);
    record.filePath     = info.absoluteFilePath();
    record.fileName     = info.fileName();
    record.fileBytes    = info.size();
    record.fileModified = info.lastModified();

    // Hashed by streaming: a speckle sequence frame can be large, and the point
    // of the hash is to identify the record, not to hold it in memory.
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (hash.addData(&file))
            record.sha256 = QString::fromLatin1(hash.result().toHex());
    }

    return record;
}

bool ImageRecord::hasTypeRange() const
{
    switch (scalarType) {
    case VTK_CHAR:
    case VTK_SIGNED_CHAR:
    case VTK_UNSIGNED_CHAR:
    case VTK_SHORT:
    case VTK_UNSIGNED_SHORT:
    case VTK_INT:
    case VTK_UNSIGNED_INT:
        return true;
    default:
        return false;
    }
}

double ImageRecord::typeMin() const
{
    double range[2] = {0.0, 0.0};
    vtkDataArray::GetDataTypeRange(scalarType, range);
    return range[0];
}

double ImageRecord::typeMax() const
{
    double range[2] = {0.0, 0.0};
    vtkDataArray::GetDataTypeRange(scalarType, range);
    return range[1];
}

double ImageRecord::rangeUtilization() const
{
    const double span = typeMax() - typeMin();
    if (span <= 0.0)
        return 0.0;
    return (dataMax - dataMin) / span;
}

double ImageRecord::fractionAtDataMin() const
{
    if (pixelCount <= 0)
        return 0.0;
    return double(pixelsAtDataMin) / double(pixelCount);
}

double ImageRecord::fractionAtDataMax() const
{
    if (pixelCount <= 0)
        return 0.0;
    return double(pixelsAtDataMax) / double(pixelCount);
}

QString ImageRecord::pixelTypeName() const
{
    switch (scalarType) {
    case VTK_CHAR:
    case VTK_SIGNED_CHAR:    return QStringLiteral("8-bit signed integer");
    case VTK_UNSIGNED_CHAR:  return QStringLiteral("8-bit unsigned integer");
    case VTK_SHORT:          return QStringLiteral("16-bit signed integer");
    case VTK_UNSIGNED_SHORT: return QStringLiteral("16-bit unsigned integer");
    case VTK_INT:            return QStringLiteral("32-bit signed integer");
    case VTK_UNSIGNED_INT:   return QStringLiteral("32-bit unsigned integer");
    case VTK_FLOAT:          return QStringLiteral("32-bit floating point");
    case VTK_DOUBLE:         return QStringLiteral("64-bit floating point");
    default:                 return QStringLiteral("unrecognised type");
    }
}

QString ImageRecord::channelsText() const
{
    switch (components) {
    case 1:  return QStringLiteral("1 (grayscale)");
    case 2:  return QStringLiteral("2 (grayscale + alpha)");
    case 3:  return QStringLiteral("3 (RGB)");
    case 4:  return QStringLiteral("4 (RGBA)");
    default: return QString::number(components);
    }
}

QString ImageRecord::fileSizeText() const
{
    const QLocale locale;
    const QString human = locale.formattedDataSize(fileBytes);
    const QString exact = locale.toString(fileBytes);

    // Under a kilobyte the human-readable form is already the exact byte count,
    // and "28 bytes (28 bytes)" reads as a mistake rather than as precision.
    if (human == QStringLiteral("%1 bytes").arg(exact))
        return human;

    return QStringLiteral("%1 (%2 bytes)").arg(human, exact);
}
