#pragma once

#include <QDateTime>
#include <QString>

// Everything SurView can truthfully state about one imported image: where the
// pixels came from (provenance) and what they actually are (pristineness).
//
// Every field is read from the file itself or from the decoded pixel data --
// nothing here is inferred or interpreted. Code that displays an ImageRecord
// only formats these values; it never adds meaning to them.
struct ImageRecord
{
    // --- Provenance: the file as it exists on disk --------------------------
    QString   filePath;
    QString   fileName;
    qint64    fileBytes = 0;
    QDateTime fileModified;
    QString   sha256;        // over the file bytes, so the record is verifiable
    QString   decoderClass;  // the reader that decoded it, named outright

    // --- The decoded record: what the pixels are ----------------------------
    int    width      = 0;
    int    height     = 0;
    int    components = 0;
    int    scalarType = -1;   // VTK scalar type id
    double dataMin    = 0.0;  // range actually present in the pixels
    double dataMax    = 0.0;

    // --- Row order as the decoder handed the pixels over --------------------
    // VTK's readers disagree about this, so it cannot be left implicit:
    // vtkTIFFReader emits the file's rows in file order, while the PNG, JPEG
    // and BMP readers emit them bottom-up. SurView holds every image in the
    // file's own row order -- row 0 at the top -- so an image that arrived
    // reversed had its rows put back. That reordering touches no pixel value
    // and loses nothing, but it is a change to what the decoder produced, so
    // it is stated rather than left for the reader to assume.
    bool rowsReversedByDecoder = false;

    // --- How much of the evidence sits against the extremes -----------------
    // Pixels holding the lowest and the highest value present. A handful is
    // ordinary; a large share means the sensor ran out of range there and the
    // true values were never captured -- the speckle in those pixels is gone,
    // not merely dark or bright.
    //
    // Counted against the extremes actually present rather than against the
    // type's limits, because they are frequently not the same: 12-bit sensor
    // data stored in a 16-bit file clips at 4095 while the type allows 65535,
    // and counting only pixels at 65535 would report no clipping at all. The
    // type range is reported separately, so whether the two coincide is
    // visible without being asserted here.
    bool   extremesCounted = false;
    qint64 pixelCount      = 0;
    qint64 pixelsAtDataMin = 0;
    qint64 pixelsAtDataMax = 0;

    // Share of the image sitting at each extreme, 0..1.
    double fractionAtDataMin() const;
    double fractionAtDataMax() const;

    // --- Display mapping applied by the viewport (view only) ----------------
    // The intensity window mapped to black..white on screen. Never applied to
    // the data itself; recorded here so the mapping is visible, not implicit.
    //
    // Only set once the image has actually been displayed. An image that has
    // been recorded but not shown -- every target image on import -- has no
    // mapping to report, and reporting 0..0 as if it were one would be a
    // claim about a rendering that never happened.
    bool   displayed  = false;
    double displayMin = 0.0;
    double displayMax = 0.0;

    bool isValid() const { return width > 0 && height > 0; }

    // Fill the provenance half from a path. The pixel half is filled by
    // whoever decodes the file.
    static ImageRecord fromFile(const QString &path);

    // Integer types have a fixed representable range worth reporting against;
    // floating-point ones do not, so those rows are simply not claimed.
    bool   hasTypeRange() const;
    double typeMin() const;
    double typeMax() const;

    // Fraction of the type's representable range the data occupies.
    // Only meaningful when hasTypeRange().
    double rangeUtilization() const;

    QString pixelTypeName() const;  // e.g. "16-bit unsigned integer"
    QString channelsText() const;   // e.g. "1 (grayscale)"
    QString fileSizeText() const;   // e.g. "8.0 MB (8388738 bytes)"
};
