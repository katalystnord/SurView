#pragma once

#include "core/PointReadout.h"

#include <QWidget>

class QLabel;
class QVBoxLayout;

// The interpretation side of the workspace, at one point: what a single
// measured point of the field actually says, as numbers.
//
// It exists because a colour map answers "where" and never "how much". A
// reader could see that one region is redder than another and had no way to
// ask what any point measured, nor to reach the reliability channels as
// numbers at all.
//
// ⚑ Always on screen, and it states both of its own gestures before either is
// needed. A readout that appeared only once the pointer happened to be over the
// field would be a hidden mode: nothing would tell a first-time reader the
// capability exists. That is also what allows a walkthrough test to hover and
// click here, since a test may only do what the screen tells it to do.
class PointPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PointPanel(QWidget *parent = nullptr);

    // Show what one point has to say. `pinned` changes only how the panel
    // describes itself: the reading is the same either way.
    void showReadout(const PointReadout &readout, bool pinned);

    // Back to the standing invitation, with no field to read.
    void clear();

private:
    void setInstruction(bool pinned, bool haveField);
    void clearRows();

    QLabel *m_instruction = nullptr;
    QWidget *m_rows = nullptr;
    QVBoxLayout *m_rowLayout = nullptr;
};
