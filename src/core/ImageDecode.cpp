#include "core/ImageDecode.h"

#include <QString>

#include <vtkImageData.h>
#include <vtkImageFlip.h>
#include <vtkImageReader2.h>
#include <vtkImageReader2Factory.h>
#include <vtkNew.h>
#include <vtkTIFFReader.h>
#include <vtkType.h>

namespace {

// Count pixels holding the lowest and the highest value present in the image.
//
// A pixel counts if ANY of its components is at the extreme: on a colour image
// a single blown channel is already a value the file could not represent, and
// requiring all three to agree would hide it. On single-channel speckle images
// -- the DIC case -- the distinction does not arise.
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

    // vtkTIFFReader is the one reader whose row order is not fixed: it honours
    // the file's own orientation tag, so the same code path yields top-down
    // rows for one TIFF and bottom-up for the next. Pinning it to the file's
    // own row order makes what arrives here depend on the reader alone, which
    // is what the single flip below is then able to reason about.
    if (auto *tiff = vtkTIFFReader::SafeDownCast(reader.Get())) {
        constexpr unsigned int kOrientationTopLeft = 1;  // row 0 top, col 0 left
        tiff->SetOrientationType(kOrientationTopLeft);
    }

    reader->Update();

    vtkSmartPointer<vtkImageData> image = reader->GetOutput();
    if (!image)
        return nullptr;

    int dims[3] = {0, 0, 0};
    image->GetDimensions(dims);
    if (dims[0] <= 0 || dims[1] <= 0)
        return nullptr;  // reader produced nothing usable

    // Put every image into one row order -- the file's own, row 0 at the top --
    // so that a pixel's position means the same thing whatever decoded it.
    // Without this the viewport shows TIFFs mirrored top-to-bottom while PNGs
    // come out upright, and a measured field drawn in engine coordinates lines
    // up with one of them and not the other.
    //
    // Which readers need it was established by reading a known image through
    // each of the four this application offers to import: with the TIFF reader
    // pinned above, it alone emits the file's rows in file order, and the PNG,
    // JPEG and BMP readers all emit them bottom-up.
    if (!vtkTIFFReader::SafeDownCast(reader.Get())) {
        vtkNew<vtkImageFlip> flip;
        flip->SetInputData(image);
        flip->SetFilteredAxis(1);
        flip->Update();

        image = flip->GetOutput();
        // The flip mirrors about the input's centre and leaves the origin at
        // -(height-1); the pixels are the ones we want, so put the origin back
        // rather than carrying an offset every later coordinate would inherit.
        image->SetOrigin(0.0, 0.0, 0.0);
        record.rowsReversedByDecoder = true;
    }

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
