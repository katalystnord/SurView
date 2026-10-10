#pragma once

#include "core/Correlation.h"
#include "core/Series.h"

#include <vtkSmartPointer.h>

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QVTKOpenGLNativeWidget;

class vtkChartXY;
class vtkContextView;

// A quantity plotted against frame, which is how a loading series is read.
//
// Every frame's field could already be looked at one at a time; nothing put
// them on the same axes. This does, for whole-field summaries and for any
// virtual extensometer that has been placed.
//
// Drawn with VTK's own charting rather than Qt Charts, which is GPL-3.0-only
// and would force SurView's licence to GPL. See the build constraint in
// CLAUDE.md.
//
// ⚑ A GAP IN A CURVE IS DRAWN AS A GAP. A frame with no reading breaks the line
// into two, rather than being dropped so the frames either side join up: that
// join is a straight segment through territory nobody measured, and it is
// indistinguishable from data. The arithmetic that decides what is readable
// lives in core/Series.h, engine-free and tested; this only draws it.
class PlotPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PlotPanel(QWidget *parent = nullptr);
    ~PlotPanel() override;

    // The measured sequence, and the gauges placed on it. Passing an empty
    // sequence returns the panel to the state that explains what it is for.
    void setFrames(const QVector<CorrelationResult> &frames);
    void setExtensometers(const QVector<Extensometer> &gauges);

    // The line probe, and what it reads: the frame and the map on screen, so
    // the profile is always of the picture the reader is looking at. An
    // invalid probe takes the profile off the selector.
    void setProbe(const LineProbe &probe);
    void setProfileContext(int frameIndex, FieldChannel channel);
    // Puts the profile on the chart, as placing a probe should.
    void showProfile();

    // The chart, for tests that check what it draws against its own plot area.
    vtkChartXY *chart() const;

    // What is currently plotted, so the window can name it when exporting.
    Series currentSeries() const { return m_series; }

signals:
    // The plotted curve should be written to a file. The panel does not choose
    // the path; that is the window's business, as it is for every other export.
    void exportRequested();

private:
    void rebuildChoices();
    void redraw();
    void updateNote();
    // Shortens the y title to its unit when the full title, measured by VTK at
    // the window's DPI, is longer than the axis. Run after every redraw and
    // whenever the chart's own size changes.
    void layoutAxes();

    bool eventFilter(QObject *watched, QEvent *event) override;

    QVector<CorrelationResult> m_frames;
    QVector<Extensometer> m_gauges;
    LineProbe m_probe;
    int m_profileFrame = 0;
    FieldChannel m_profileChannel = FieldChannel::DisplacementMagnitude;
    Series m_series;
    // The y title as the axis would state it in full, and whether it had to be
    // shortened to fit; the note then names the quantity in its place.
    QString m_fullYTitle;
    bool m_yTitleShortened = false;

    QComboBox *m_choice = nullptr;
    QLabel *m_choiceLabel = nullptr;
    QLabel *m_note = nullptr;
    QPushButton *m_export = nullptr;
    QVTKOpenGLNativeWidget *m_view = nullptr;

    vtkSmartPointer<vtkContextView> m_context;
    vtkSmartPointer<vtkChartXY> m_chart;
};
