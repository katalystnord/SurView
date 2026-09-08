#pragma once

#include <QString>
#include <QWidget>

class QToolButton;
class QLabel;

// One foldable section of a panel: a header that can be clicked to put its
// controls away, and a line that says what is in there while they are away.
//
// WHY THIS EXISTS. The Analysis panel has grown to five groups and the Record
// panel to three, and a reader looking for one control scrolls past four
// sections of settings they have already decided about. Folding is the ordinary
// answer, and the ordinary answer has one trap this application cannot afford.
//
// ⚑ A FOLDED SECTION IS STILL IN FORCE. Hiding a control does not change it,
// and must not: a run measured under settings that are out of sight is still
// measured under those settings. That is why the header carries a SUMMARY --
// folding away a subset radius of 31 px must not make the panel read as though
// nothing had been chosen. The same hazard as the mouse wheel silently editing
// a spin box, approached from the other side: there, a setting changed without
// being seen; here, a setting could be in force without being seen.
//
// The header is a button with an arrow on it, not a bare label: what can be
// pressed has to look like it can be pressed, and a section that folded on a
// click nobody could predict would be a mode with no way in or out.
class CollapsibleSection : public QWidget
{
    Q_OBJECT

public:
    explicit CollapsibleSection(const QString &title, QWidget *parent = nullptr);

    // The widget this section folds. Takes ownership.
    void setContent(QWidget *content);

    // What the section holds, in one short line, shown beside the title while
    // it is folded. Empty is allowed and says nothing, which is the right
    // answer for a section whose contents cannot be summarised in a phrase.
    void setSummary(const QString &summary);

    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return m_collapsed; }

signals:
    void collapsedChanged(bool collapsed);

private:
    void updateHeader();

    QToolButton *m_header = nullptr;
    QLabel *m_summary = nullptr;
    QWidget *m_content = nullptr;
    QString m_title;
    QString m_summaryText;
    bool m_collapsed = false;
};
