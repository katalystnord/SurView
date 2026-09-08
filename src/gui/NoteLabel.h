#pragma once

#include <QLabel>

// A word-wrapped label that reports the height its text actually needs.
//
// ⚑ THE TRAP THIS EXISTS FOR HAS NOW BITTEN FIVE PANELS. A wrapped QLabel
// reports ONE LINE as its minimum height, in both directions, so any layout
// that can starve it will: the Analysis panel's notes were drawn over, the plot
// panel's explanation was painted under a native GL widget, the field bar grew
// over the specimen it was explaining, the comparison window's captions had to
// be given room by hand, and folding the Analysis panel into sections clipped
// its speckle estimate through the middle. Every one of those was a note on
// screen and unreadable, which is worse than a note that is absent: nothing
// indicates there is anything to read.
//
// The fix is to answer the layout's question honestly. `heightForWidth()` on a
// wrapped QLabel already returns the right answer; what is missing is
// `minimumSizeHint()` USING it, which is what this class does and what a plain
// QLabel does not.
class NoteLabel : public QLabel
{
    Q_OBJECT

public:
    explicit NoteLabel(QWidget *parent = nullptr) : QLabel(parent)
    {
        setWordWrap(true);
        setTextInteractionFlags(Qt::TextSelectableByMouse);
        QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }

    QSize minimumSizeHint() const override
    {
        // The width a layout has already given this label, or its own hint the
        // first time round. Never the text's own natural width: that is one
        // long line, which is the answer that starves it.
        const int usable = width() > 0 ? width() : QLabel::minimumSizeHint().width();
        return QSize(0, heightForWidth(usable > 0 ? usable : 200));
    }

    QSize sizeHint() const override { return minimumSizeHint(); }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        // A new width is a new height. Without this the label keeps the height
        // it needed at the width it used to have, which is how a panel that
        // reads correctly at one size clips at another.
        updateGeometry();
    }
};
