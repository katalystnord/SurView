#pragma once

#include <QDateTime>
#include <QString>

// Everything SurView can truthfully state about one imported image: where the
// pixels came from (provenance) and what they actually are (pristineness).
//
// Every field is read from the file itself or from the decoded pixel data —
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

    // --- Display mapping applied by the viewport (view only) ----------------
    // The intensity window mapped to black..white on screen. Never applied to
    // the data itself; recorded here so the mapping is visible, not implicit.
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
