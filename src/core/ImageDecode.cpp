#include "core/ImageDecode.h"

#include <QString>

#include <vtkImageData.h>
#include <vtkImageReader2.h>
#include <vtkImageReader2Factory.h>

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

    return image;
}
