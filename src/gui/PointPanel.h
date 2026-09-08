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

    // Show what is under the pointer: what the CAMERA recorded there, and --
    // once a run has produced one -- what the correlation MEASURED there.
    //
    // ⚑ The two are different claims and are never merged into one number. The
    // camera lines are the photograph's own values, available from the moment
    // an image is loaded; the field lines are a measurement, and exist only
    // after a run. `pinned` changes only how the panel describes itself: the
    // reading is the same either way.
    void showReading(const QVector<ReadoutLine> &camera, const PointReadout &field,
                     bool haveField, bool pinned);

    // Back to the standing invitation, with no image to read at all.
    void clear();

private:
    void setInstruction(bool pinned, bool haveField, bool haveImage);
    void clearRows();
    void addRows(const QVector<ReadoutLine> &lines);

    QLabel *m_instruction = nullptr;
    QWidget *m_rows = nullptr;
    QVBoxLayout *m_rowLayout = nullptr;
};
