#include "gui/PointPanel.h"

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

namespace
{

// Note text, and the stronger form for a line that is cautioning rather than
// reporting. Colour is not the only carrier: a warning line keeps its full
// sentence, so the caution survives being read in greyscale or by someone who
// does not see the colour difference.
constexpr const char *kNoteStyle = "color: #9aa0a8;";
constexpr const char *kWarningStyle = "color: #d08540; font-weight: bold;";

QLabel *wrapped(const QString &text, const char *style)
{
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (style)
        label->setStyleSheet(QString::fromLatin1(style));
    return label;
}

}  // namespace

PointPanel::PointPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_instruction = wrapped(QString(), kNoteStyle);
    layout->addWidget(m_instruction);

    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    m_rows = new QWidget;
    m_rowLayout = new QVBoxLayout(m_rows);
    m_rowLayout->setContentsMargins(0, 0, 0, 0);
    m_rowLayout->setSpacing(8);
    layout->addWidget(m_rows);

    layout->addStretch(1);

    clear();
}

void PointPanel::setInstruction(bool pinned, bool haveField)
{
    if (pinned) {
        // The way out of a mode is stated in the mode, never remembered.
        m_instruction->setText(tr("Pinned. Click the field again to release it, "
                                  "or click another point to pin that one."));
        return;
    }
    if (!haveField) {
        m_instruction->setText(tr("Measure a field, then move the pointer over "
                                  "it to read any point. Click to pin a point "
                                  "so it stays while you look away."));
        return;
    }
    m_instruction->setText(tr("Move the pointer over the field to read a point. "
                              "Click to pin it."));
}

void PointPanel::clearRows()
{
    while (QLayoutItem *item = m_rowLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

void PointPanel::clear()
{
    clearRows();
    setInstruction(false, false);
}

void PointPanel::showReadout(const PointReadout &readout, bool pinned)
{
    clearRows();
    setInstruction(pinned, true);

    for (const ReadoutLine &line : readout.lines) {
        auto *row = new QWidget;
        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(1);

        auto *value = wrapped(QStringLiteral("<b>%1</b><br>%2")
                                  .arg(line.label.toHtmlEscaped(),
                                       line.value.toHtmlEscaped()),
                              line.warning ? kWarningStyle : nullptr);
        rowLayout->addWidget(value);

        // The note is where a channel says what it is NOT. Kept beside the
        // number rather than in documentation nobody has open: a single value
        // next to a label reads as definitive, and at this range more so, not
        // less.
        if (!line.note.isEmpty())
            rowLayout->addWidget(wrapped(line.note, kNoteStyle));

        m_rowLayout->addWidget(row);
    }
}
