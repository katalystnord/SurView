#include "RecordPanel.h"

#include "core/ImageRecord.h"

#include <QAbstractTextDocumentLayout>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QLocale>
#include <QScrollArea>
#include <QTextDocument>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QtMath>

namespace {

// A value the panel cannot substantiate is shown as such, never as a blank or
// a plausible-looking default.
QString unknownText()
{
    return QStringLiteral("—");
}

// Field names and notes sit behind the values they describe, but must stay
// comfortably readable. Blending the theme's own text colour toward its
// background keeps that true in a light or a dark theme alike — unlike a
// fixed grey, which goes invisible against one of them.
void deemphasise(QWidget *widget, qreal weight)
{
    const QPalette base = widget->palette();
    const QColor text = base.color(QPalette::WindowText);
    const QColor back = base.color(QPalette::Window);

    QPalette faded = base;
    faded.setColor(QPalette::WindowText,
                   QColor::fromRgbF(text.redF() * weight + back.redF() * (1 - weight),
                                    text.greenF() * weight + back.greenF() * (1 - weight),
                                    text.blueF() * weight + back.blueF() * (1 - weight)));
    widget->setPalette(faded);
}

}  // namespace

RecordPanel::RecordPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    m_placeholder = new QWidget(this);
    auto *placeholderLayout = new QVBoxLayout(m_placeholder);
    auto *placeholderText = new QLabel(
        tr("No image imported.\n\nOnce a reference image is imported, its "
           "provenance and pixel facts are reported here."));
    placeholderText->setWordWrap(true);
    placeholderText->setAlignment(Qt::AlignTop);
    deemphasise(placeholderText, 0.62);
    placeholderLayout->addWidget(placeholderText);
    placeholderLayout->addStretch();
    outer->addWidget(m_placeholder);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    // Without this the content is laid out at its unwrapped width and the long
    // values are cut off instead of wrapping to the panel.
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);

    QFormLayout *form = nullptr;

    addSection(layout, tr("Source"), &form);
    m_file     = addRow(form, tr("File"));
    m_size     = addRow(form, tr("Size"));
    m_modified = addRow(form, tr("Modified"));
    m_decoder  = addRow(form, tr("Decoded by"));
    m_location = addRow(form, tr("Location"), /*wide=*/true);
    m_hash     = addHashRow(form, tr("SHA-256 of the file"));

    addSection(layout, tr("Pixel record"), &form);
    m_dimensions = addRow(form, tr("Dimensions"));
    m_channels   = addRow(form, tr("Channels"));
    m_pixelType  = addRow(form, tr("Pixel type"));
    m_typeRange  = addRow(form, tr("Type range"));
    m_dataRange  = addRow(form, tr("Data range"));
    m_rangeUsed  = addRow(form, tr("Range used"));

    addSection(layout, tr("Pristineness"), &form);
    m_conversions    = addRow(form, tr("Conversions"), /*wide=*/true);
    m_displayMapping = addRow(form, tr("Display mapping"), /*wide=*/true);

    // The limit of the claim, stated on screen rather than assumed known: this
    // panel reports what the decoder handed over, and cannot vouch for what
    // happened inside the file's own encoder.
    auto *scope = new QLabel(
        tr("Reported from the file on disk and from the decoded pixels. "
           "Anything the file's own encoder did before SurView saw it is "
           "outside this record."));
    scope->setWordWrap(true);
    deemphasise(scope, 0.62);
    layout->addSpacing(8);
    layout->addWidget(scope);
    layout->addStretch();

    scroll->setWidget(content);
    outer->addWidget(scroll);
    m_scroll = scroll;

    clear();
}

void RecordPanel::addSection(QVBoxLayout *layout, const QString &title,
                             QFormLayout **form)
{
    auto *header = new QLabel(title);
    header->setStyleSheet(QStringLiteral("font-weight: bold;"));
    layout->addSpacing(layout->count() ? 10 : 0);
    layout->addWidget(header);

    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    layout->addWidget(line);

    *form = new QFormLayout;
    (*form)->setContentsMargins(0, 4, 0, 0);
    (*form)->setHorizontalSpacing(10);
    (*form)->setVerticalSpacing(3);
    (*form)->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    (*form)->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    layout->addLayout(*form);
}

QLabel *RecordPanel::addRow(QFormLayout *form, const QString &label, bool wide)
{
    auto *name = new QLabel(label);
    deemphasise(name, 0.62);

    auto *value = new QLabel(unknownText());
    // Provenance is only useful if it can be carried elsewhere — paths and
    // hashes are meant to be copied out whole.
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);

    if (wide) {
        // A path or a hash is longer than any sensible label column, so it gets
        // the panel's full width beneath its name rather than being clipped or
        // elided — a truncated hash identifies nothing.
        value->setWordWrap(true);
        value->setContentsMargins(12, 0, 0, 4);

        // A wrapped label still asks for its unwrapped width, which would push
        // the panel wider than the dock and clip the text instead of wrapping
        // it. Let it shrink to whatever width the dock has, and take its height
        // from that width.
        QSizePolicy policy = value->sizePolicy();
        policy.setHorizontalPolicy(QSizePolicy::Ignored);
        policy.setVerticalPolicy(QSizePolicy::MinimumExpanding);
        policy.setHeightForWidth(true);
        value->setSizePolicy(policy);
        value->setMinimumWidth(0);

        form->addRow(name);
        form->addRow(value);
    } else {
        form->addRow(name, value);
    }
    return value;
}

QTextEdit *RecordPanel::addHashRow(QFormLayout *form, const QString &label)
{
    auto *name = new QLabel(label);
    deemphasise(name, 0.62);

    auto *value = new QTextEdit;
    value->setReadOnly(true);
    value->setFrameShape(QFrame::NoFrame);
    value->setWordWrapMode(QTextOption::WrapAnywhere);
    value->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    value->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    value->setMinimumWidth(0);
    value->setContentsMargins(12, 0, 0, 4);

    QFont mono = value->font();
    mono.setFamily(QStringLiteral("monospace"));
    value->setFont(mono);

    QPalette transparent = value->palette();
    transparent.setColor(QPalette::Base, Qt::transparent);
    value->setPalette(transparent);

    // Grow to exactly the number of lines the digits wrap onto at the panel's
    // current width — no scrollbar, no truncation.
    connect(value->document()->documentLayout(),
            &QAbstractTextDocumentLayout::documentSizeChanged, value,
            [value](const QSizeF &size) {
                value->setFixedHeight(qCeil(size.height()) + 4);
            });

    form->addRow(name);
    form->addRow(value);
    return value;
}

void RecordPanel::clear()
{
    m_placeholder->show();
    m_scroll->hide();
}

void RecordPanel::setRecord(const ImageRecord &record)
{
    if (!record.isValid()) {
        clear();
        return;
    }

    const QLocale locale;

    m_file->setText(record.fileName);
    m_location->setText(record.filePath);
    m_size->setText(record.fileSizeText());
    m_modified->setText(
        record.fileModified.isValid()
            ? locale.toString(record.fileModified, QLocale::ShortFormat)
            : unknownText());
    m_hash->setPlainText(record.sha256.isEmpty()
                             ? tr("not computed — file unreadable")
                             : record.sha256);
    m_decoder->setText(record.decoderClass.isEmpty() ? unknownText()
                                                     : record.decoderClass);

    m_dimensions->setText(tr("%1 × %2 px")
                              .arg(locale.toString(record.width),
                                   locale.toString(record.height)));
    m_channels->setText(record.channelsText());
    m_pixelType->setText(record.pixelTypeName());

    if (record.hasTypeRange()) {
        m_typeRange->setText(tr("%1 to %2")
                                 .arg(locale.toString(record.typeMin(), 'f', 0),
                                      locale.toString(record.typeMax(), 'f', 0)));
        m_rangeUsed->setText(tr("%1% of the type's range")
                                 .arg(100.0 * record.rangeUtilization(), 0, 'f', 1));
    } else {
        // Floating-point pixels have no fixed range to measure against, so no
        // percentage is claimed.
        m_typeRange->setText(tr("no fixed range (floating point)"));
        m_rangeUsed->setText(tr("not applicable"));
    }
    m_dataRange->setText(tr("%1 to %2")
                             .arg(locale.toString(record.dataMin, 'g', 6),
                                  locale.toString(record.dataMax, 'g', 6)));

    m_conversions->setText(tr("none — pixels held exactly as decoded"));
    m_displayMapping->setText(
        tr("%1 to %2 shown as black to white — view only, the pixels are "
           "unchanged")
            .arg(locale.toString(record.displayMin, 'g', 6),
                 locale.toString(record.displayMax, 'g', 6)));

    m_placeholder->hide();
    m_scroll->show();
}
