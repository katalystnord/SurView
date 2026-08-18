#pragma once

#include <QWidget>

struct ImageRecord;
class QFormLayout;
class QLabel;
class QTextEdit;
class QVBoxLayout;

// The record side of the workspace: what SurView has actually recorded about
// the imported image, stated without interpretation. It reports provenance
// (which file, verified by hash), the pixel facts as decoded, and the fact
// that nothing was converted on the way in -- including an explicit statement
// of where its own knowledge stops.
class RecordPanel : public QWidget
{
    Q_OBJECT

public:
    explicit RecordPanel(QWidget *parent = nullptr);

    void setRecord(const ImageRecord &record);
    void clear();

private:
    // A "wide" row puts its value on its own full-width line beneath the label,
    // for values too long for a label/value column pair.
    QLabel *addRow(QFormLayout *form, const QString &label, bool wide = false);

    // A hash has no word boundaries to wrap at, so it needs a widget that can
    // break anywhere while still yielding the exact digits when copied.
    QTextEdit *addHashRow(QFormLayout *form, const QString &label);
    void addSection(QVBoxLayout *layout, const QString &title,
                    QFormLayout **form);

    // Fills one of the two "pixels sitting at an extreme value" rows.
    void setExtremeRow(QLabel *row, const ImageRecord &record, qint64 pixels,
                       double fraction, double value, bool isTypeLimit);

    QWidget *m_placeholder = nullptr;
    QWidget *m_scroll = nullptr;  // holds the populated rows

    // Provenance
    QLabel *m_file = nullptr;
    QLabel *m_location = nullptr;
    QLabel *m_size = nullptr;
    QLabel *m_modified = nullptr;
    QTextEdit *m_hash = nullptr;
    QLabel *m_decoder = nullptr;

    // Pixel record
    QLabel *m_dimensions = nullptr;
    QLabel *m_channels = nullptr;
    QLabel *m_pixelType = nullptr;
    QLabel *m_typeRange = nullptr;
    QLabel *m_dataRange = nullptr;
    QLabel *m_rangeUsed = nullptr;
    QLabel *m_atDataMin = nullptr;
    QLabel *m_atDataMax = nullptr;

    // Pristineness
    QLabel *m_rowOrder = nullptr;
    QLabel *m_conversions = nullptr;
    QLabel *m_displayMapping = nullptr;
};
