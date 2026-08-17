#include "core/ImageDecode.h"

#include <QString>

#include <vtkImageData.h>
#include <vtkImageReader2.h>
#include <vtkImageReader2Factory.h>
#include <vtkType.h>

namespace {

// Count pixels holding the lowest and the highest value present in the image.
//
// A pixel counts if ANY of its components is at the extreme: on a colour image
// a single blown channel is already a value the file could not represent, and
// requiring all three to agree would hide it. On single-channel speckle images
// — the DIC case — the distinction does not arise.
//
// Compared with <= and >= rather than == so that the comparison still holds
// when a double-valued range is narrowed to the sample type; the bounds come
// from the image's own scalar range, so nothing can fall outside them.
template <typename T>
void countExtremePixels(const T *data, vtkIdType pixels, int components,
                        double dataMin, double dataMax, qint64 &atMin,
                        qint64 &atMax)
{
    const T low  = static_cast<T>(dataMin);
    const T high = static_cast<T>(dataMax);

    for (vtkIdType p = 0; p < pixels; ++p) {
        const T *pixel = data + p * components;
        bool isLow = false;
        bool isHigh = false;
        for (int c = 0; c < components; ++c) {
            if (pixel[c] <= low)
                isLow = true;
            if (pixel[c] >= high)
                isHigh = true;
        }
        if (isLow)
            ++atMin;
        if (isHigh)
            ++atMax;
    }
}

}  // namespace

vtkSmartPointer<vtkImageData> decodeImage(const QString &path,
                                          ImageRecord &record)
{
    // Provenance first, so that a file we cannot decode still leaves a record
    // of what was pointed at rather than nothing at all.
    record = ImageRecord::fromFile(path);

    vtkSmartPointer<vtkImageReader2> reader;
    reader.TakeReference(
        vtkImageReader2Factory::CreateImageReader2(path.toUtf8().constData()));
    if (!reader)
        return nullptr;  // no reader for this file type

    reader->SetFileName(path.toUtf8().constData());
    reader->Update();

    vtkImageData *image = reader->GetOutput();
    if (!image)
        return nullptr;

    int dims[3] = {0, 0, 0};
    image->GetDimensions(dims);
    if (dims[0] <= 0 || dims[1] <= 0)
        return nullptr;  // reader produced nothing usable

    record.decoderClass = QString::fromLatin1(reader->GetClassName());
    record.width        = dims[0];
    record.height       = dims[1];
    record.components   = image->GetNumberOfScalarComponents();
    record.scalarType   = image->GetScalarType();

    double range[2] = {0.0, 0.0};
    image->GetScalarRange(range);
    record.dataMin = range[0];
    record.dataMax = range[1];

    const vtkIdType pixels =
        vtkIdType(dims[0]) * vtkIdType(dims[1]) * vtkIdType(dims[2] > 0 ? dims[2] : 1);
    if (void *scalars = image->GetScalarPointer()) {
        qint64 atMin = 0;
        qint64 atMax = 0;
        switch (image->GetScalarType()) {
            vtkTemplateMacro(countExtremePixels(static_cast<VTK_TT *>(scalars),
                                                pixels, record.components,
                                                record.dataMin, record.dataMax,
                                                atMin, atMax));
        default:
            // An unrecognised sample type: say nothing rather than guess at how
            // to read it. extremesCounted stays false and the panel reports the
            // count as unavailable instead of as zero.
            return image;
        }

        record.extremesCounted = true;
        record.pixelCount      = pixels;
        record.pixelsAtDataMin = atMin;
        record.pixelsAtDataMax = atMax;
    }

    return image;
}
