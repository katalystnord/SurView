#pragma once

#include "core/Correlation.h"
#include "core/FieldLayout.h"
#include "core/KnownAnswer.h"

#include <QWidget>

#include <vtkSmartPointer.h>

class QComboBox;
class QLabel;
class FieldView;
class vtkImageData;

// The measured field, the answer the example states, and the difference between
// them, side by side on one shared colour scale.
//
// WHY THIS SCREEN EXISTS. The synthetic examples ship with their exact answer,
// and until now only the test suite ever read it: a reader with no reason yet to
// believe a number this application produced could be told that 10,733 points
// converged, and nothing on screen ever put one of those numbers beside the
// answer it was supposed to be. This is the most direct evidence SurView can
// offer that the field it draws is right.
//
// ⚑ IT IS A COMPARISON, SO ONE SCALE AND ONE CAMERA GOVERN ALL THREE PANELS.
// Two fields drawn on their own scales look alike however far apart they are,
// and two panels showing different parts of the specimen while captioned as a
// comparison would be worse than showing nothing. The scale comes from
// sharedColourRange(); the camera is one object the three renderers share.
//
// The difference is drawn only where the run MEASURED something. A point the
// solver rejected has no error of zero -- an error of zero is the answer being
// right, and painted over the points the instrument could not read it would
// claim it did best exactly where it did nothing.
class ComparisonWindow : public QWidget
{
    Q_OBJECT

public:
    ComparisonWindow(QWidget *parent = nullptr);

    // Put one measured frame up against the answer its target image states.
    // `referenceImagePath` is the photograph the field is drawn over. Named
    // compare() rather than show(): a second show() here would hide QWidget's
    // own, and every caller of the wrong one would compile.
    void compare(const CorrelationResult &result, const KnownAnswer &answer,
                 const QString &referenceImagePath, const QString &targetName);

    // What the screen currently says about accuracy, for the channel on
    // display. Exposed so a walkthrough test can assert on the comparison the
    // window actually made rather than on the text it happens to print.
    AccuracyReport report() const;
    FieldChannel channel() const;

private:
    void rebuildChannels();
    void redraw();

    CorrelationResult m_result;
    KnownAnswer m_answer;
    QString m_targetName;

    vtkSmartPointer<vtkImageData> m_image;
    double m_displayLow = 0.0;
    double m_displayHigh = 1.0;

    QLabel *m_heading = nullptr;
    QLabel *m_whatItShows = nullptr;
    QLabel *m_exactness = nullptr;
    QComboBox *m_channel = nullptr;
    QLabel *m_channelNote = nullptr;

    struct Panel
    {
        QLabel *title = nullptr;
        QLabel *caption = nullptr;
        FieldView *view = nullptr;
    };
    Panel m_measured;
    Panel m_stated;
    Panel m_error;
};
