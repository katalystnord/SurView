#include "gui/CollapsibleSection.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

CollapsibleSection::CollapsibleSection(const QString &title, QWidget *parent)
    : QWidget(parent), m_title(title)
{
    QSizePolicy own = sizePolicy();
    own.setHeightForWidth(true);
    own.setVerticalPolicy(QSizePolicy::MinimumExpanding);
    setSizePolicy(own);

    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(2);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(6);

    m_header = new QToolButton(this);
    m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_header->setArrowType(Qt::DownArrow);
    m_header->setText(title);
    m_header->setCheckable(true);
    m_header->setChecked(true);
    m_header->setAutoRaise(true);
    m_header->setCursor(Qt::PointingHandCursor);
    // ⚑ Flush LEFT, and the alignment has to be asked for twice: a QToolButton
    // centres its contents, so a header that only had its width stretched came
    // out floating in the middle of the panel, reading as a caption rather than
    // as something to press. The rule underneath separates one section from the
    // next without a box around each.
    m_header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_header->setStyleSheet(QStringLiteral(
        "QToolButton { font-weight: 600; border: none; text-align: left;"
        " padding: 4px 2px; border-bottom: 1px solid #d7dbe0; }"
        "QToolButton:hover { color: #1a6fb5; }"));
    connect(m_header, &QToolButton::toggled, this,
            [this](bool open) { setCollapsed(!open); });
    headerRow->addWidget(m_header);

    // ⚑ What the section holds, while it is folded. A panel that hides a
    // setting without saying what it is would let a run be conducted under
    // settings nobody can see -- the same hazard as a control changed without
    // being noticed, approached from the other side.
    m_summary = new QLabel(this);
    m_summary->setWordWrap(false);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // The same rule under the summary as under the header, so a folded section
    // is one band across the panel rather than a heading with a loose line
    // beside it.
    m_summary->setStyleSheet(QStringLiteral(
        "color: #55616d; font-size: 11px; padding-bottom: 4px;"
        " border-bottom: 1px solid #d7dbe0;"));
    headerRow->addWidget(m_summary, 1);

    column->addLayout(headerRow);
    updateHeader();
}

void CollapsibleSection::setContent(QWidget *content)
{
    if (m_content) {
        layout()->removeWidget(m_content);
        delete m_content;
    }
    m_content = content;
    if (!m_content)
        return;

    m_content->setParent(this);

    // ⚑ HEIGHT-FOR-WIDTH HAS TO BE ASKED FOR. A widget's size policy does not
    // carry it by default, so a section wrapping a form with a word-wrapped
    // note in it was allocating the note ONE line and clipping the other four
    // through the middle -- text on screen and unreadable, which this code base
    // has now met on four different panels. The wrapper must not be the thing
    // that breaks what worked before it.
    QSizePolicy policy = m_content->sizePolicy();
    policy.setHeightForWidth(true);
    policy.setVerticalPolicy(QSizePolicy::MinimumExpanding);
    m_content->setSizePolicy(policy);

    layout()->addWidget(m_content);
    m_content->setVisible(!m_collapsed);
}

void CollapsibleSection::setSummary(const QString &summary)
{
    m_summaryText = summary;
    updateHeader();
}

void CollapsibleSection::setCollapsed(bool collapsed)
{
    if (m_collapsed == collapsed)
        return;
    m_collapsed = collapsed;
    if (m_content)
        m_content->setVisible(!collapsed);
    if (m_header->isChecked() == collapsed) {
        const QSignalBlocker blocked(m_header);
        m_header->setChecked(!collapsed);
    }
    updateHeader();
    emit collapsedChanged(collapsed);
}

void CollapsibleSection::updateHeader()
{
    m_header->setArrowType(m_collapsed ? Qt::RightArrow : Qt::DownArrow);
    // The summary is only worth the room when the controls it stands in for are
    // not on screen; open, they say it better than a line of prose can.
    m_summary->setText(m_collapsed ? m_summaryText : QString());
    m_summary->setVisible(m_collapsed && !m_summaryText.isEmpty());
    m_header->setToolTip(m_collapsed ? tr("Show the %1 settings").arg(m_title)
                                     : tr("Fold the %1 settings away").arg(m_title));
}
