#pragma once

#include "core/ImageRecord.h"

#include <vtkSmartPointer.h>

class QString;
class vtkImageData;

// Decode an image file and record what arrived.
//
// Separate from the viewport on purpose: recording an image and displaying one
// are different jobs, and only the reference image is displayed. Target images
// must be recorded just as fully -- a record we only produce for the pixels we
// happen to be looking at is not a record of the evidence.
//
// Fills the pixel half of `record` (dimensions, channels, scalar type, the
// range actually present) and the decoder that read it; the provenance half is
// read from the file by ImageRecord::fromFile. Returns the decoded pixels so a
// caller that wants to display them need not decode twice, or null if the file
// could not be read -- in which case `record` is left holding only whatever
// provenance the file system could supply.
vtkSmartPointer<vtkImageData> decodeImage(const QString &path,
                                          ImageRecord &record);
