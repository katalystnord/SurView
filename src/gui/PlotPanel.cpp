#include "gui/PlotPanel.h"

#include <QVTKOpenGLNativeWidget.h>

#include <vtkAxis.h>
#include <vtkChartXY.h>
#include <vtkContextScene.h>
#include <vtkContextView.h>
#include <vtkDoubleArray.h>
#include <vtkFloatArray.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkNew.h>
#include <vtkPlotLine.h>
#include <vtkPlotPoints.h>
#include <vtkRenderer.h>
#include <vtkTable.h>
#include <vtkStringArray.h>
#include <vtkTextProperty.h>

#include <algorithm>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariant>

namespace {

// One choice in the selector: either a whole-field summary, or one gauge's
// reading. Carried as a pair of indices rather than a pointer, so a rebuilt
// gauge list cannot leave the selector holding a dangling entry.
struct PlotChoice
{
    int fieldIndex = -1;       // into offeredFieldSeries(), or -1
    int gaugeIndex = -1;       // into the gauge list, or -1
    ExtensometerQuantity quantity = ExtensometerQuantity::Strain;
};

}  // namespace

Q_DECLARE_METATYPE(PlotChoice)

PlotPanel::PlotPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(8, 8, 8, 8);
    column->setSpacing(8);

    auto *row = new QHBoxLayout;
    m_choiceLabel = new QLabel(tr("Plotting"), this);
    row->addWidget(m_choiceLabel);

    m_choice = new QComboBox(this);
    connect(m_choice, &QComboBox::currentIndexChanged, this, [this](int) {
        redraw();
        updateNote();
    });
    row->addWidget(m_choice, 1);

    m_export = new QPushButton(tr("Save plot data..."), this);
    m_export->setToolTip(tr("Write the plotted curve as a table, one row per "
                            "frame."));
    connect(m_export, &QPushButton::clicked, this, &PlotPanel::exportRequested);
    row->addWidget(m_export);
    column->addLayout(row);

    // ⚑ The note goes ABOVE the chart, not below it. QVTKOpenGLNativeWidget is
    // a native window, and below a stretching one it was drawn straight OVER
    // the word-wrapped label: a wrapped QLabel reports a single line as its
    // minimum, so the layout handed the chart the space and the explanation
    // vanished under it, cut off mid-sentence. The same fault as the Analysis
    // panel's, and found the same way -- by looking at the screen.
    m_note = new QLabel(this);
    m_note->setWordWrap(true);
    column->addWidget(m_note);

    m_view = new QVTKOpenGLNativeWidget(this);
    m_view->setMinimumHeight(140);
    column->addWidget(m_view, 1);

    vtkNew<vtkGenericOpenGLRenderWindow> window;
    m_view->setRenderWindow(window);

    m_context = vtkSmartPointer<vtkContextView>::New();
    m_context->SetRenderWindow(window);
    m_context->GetRenderer()->SetBackground(0.13, 0.13, 0.14);

    m_chart = vtkSmartPointer<vtkChartXY>::New();
    m_context->GetScene()->AddItem(m_chart);

    rebuildChoices();
    updateNote();
}

PlotPanel::~PlotPanel() = default;

void PlotPanel::setFrames(const QVector<CorrelationResult> &frames)
{
    m_frames = frames;
    rebuildChoices();
    redraw();
    updateNote();
}

void PlotPanel::setExtensometers(const QVector<Extensometer> &gauges)
{
    m_gauges = gauges;
    rebuildChoices();
    redraw();
    updateNote();
}

void PlotPanel::rebuildChoices()
{
    const QVariant kept = m_choice->currentData();

    QSignalBlocker blocked(m_choice);
    m_choice->clear();

    // Gauges first: a user who has gone to the trouble of placing one wants to
    // see it, and it is what the panel is for.
    for (int g = 0; g < m_gauges.size(); g++) {
        for (ExtensometerQuantity quantity : {ExtensometerQuantity::Strain,
                                              ExtensometerQuantity::Elongation,
                                              ExtensometerQuantity::Length}) {
            PlotChoice choice;
            choice.gaugeIndex = g;
            choice.quantity = quantity;
            m_choice->addItem(
                tr("%1, %2").arg(m_gauges.at(g).name,
                                 extensometerQuantityName(quantity)),
                QVariant::fromValue(choice));
        }
    }

    // Built from offeredFieldSeries(), the same list the tests walk, so the
    // selector cannot offer a curve nothing can produce.
    const QVector<FieldSeriesChoice> field = offeredFieldSeries();
    for (int i = 0; i < field.size(); i++) {
        PlotChoice choice;
        choice.fieldIndex = i;
        m_choice->addItem(field.at(i).name, QVariant::fromValue(choice));
    }

    // Keep whatever was being looked at, so adding a gauge does not throw the
    // reader back to the first entry.
    for (int i = 0; i < m_choice->count(); i++) {
        if (m_choice->itemData(i) == kept) {
            m_choice->setCurrentIndex(i);
            break;
        }
    }
}

void PlotPanel::redraw()
{
    m_chart->ClearPlots();
    m_series = Series();

    if (m_frames.isEmpty() || m_choice->currentIndex() < 0) {
        m_view->renderWindow()->Render();
        return;
    }

    const PlotChoice choice = m_choice->currentData().value<PlotChoice>();
    if (choice.gaugeIndex >= 0 && choice.gaugeIndex < m_gauges.size()) {
        m_series = extensometerSeries(m_gauges.at(choice.gaugeIndex), m_frames,
                                      choice.quantity);
    } else {
        const QVector<FieldSeriesChoice> field = offeredFieldSeries();
        if (choice.fieldIndex < 0 || choice.fieldIndex >= field.size()) {
            m_view->renderWindow()->Render();
            return;
        }
        m_series = fieldSeries(m_frames, field.at(choice.fieldIndex).channel,
                               field.at(choice.fieldIndex).aggregate);
    }

    // ⚑ One line plot per unbroken run of readable frames. A single plot with
    // the unreadable frames left out would join the frames either side into a
    // straight segment across a gap nobody measured, and that segment looks
    // exactly like data. Broken into runs, the gap is visible as a gap.
    QVector<QVector<SeriesPoint>> runs;
    for (const SeriesPoint &point : m_series.points) {
        if (!point.measured) {
            if (!runs.isEmpty() && !runs.last().isEmpty())
                runs.append(QVector<SeriesPoint>());
            continue;
        }
        if (runs.isEmpty())
            runs.append(QVector<SeriesPoint>());
        runs.last().append(point);
    }

    for (const QVector<SeriesPoint> &run : runs) {
        if (run.isEmpty())
            continue;

        vtkNew<vtkTable> table;
        vtkNew<vtkDoubleArray> frameColumn;
        frameColumn->SetName("Frame");
        table->AddColumn(frameColumn);
        vtkNew<vtkDoubleArray> valueColumn;
        valueColumn->SetName(qPrintable(m_series.name));
        table->AddColumn(valueColumn);
        table->SetNumberOfRows(run.size());

        for (int i = 0; i < run.size(); i++) {
            table->SetValue(i, 0, run.at(i).frame);
            table->SetValue(i, 1, run.at(i).value);
        }

        // A single readable frame between two gaps has no line to draw, so it
        // is drawn as a marker: dropped, it would be a measurement the chart
        // silently withheld.
        vtkPlot *plot = run.size() == 1 ? m_chart->AddPlot(vtkChart::POINTS)
                                        : m_chart->AddPlot(vtkChart::LINE);
        plot->SetInputData(table, 0, 1);
        // ⚑ SetColorF, not SetColor. vtkPlot::SetColor also takes unsigned
        // chars, and 0.30 chosen against that overload is 0 -- the curve came
        // out black on a dark chart, which reads as a rendering failure rather
        // than as a colour.
        plot->SetColorF(0.36, 0.72, 0.98);
        plot->SetWidth(2.0);
        // Only the first run carries the name, or a curve broken into four runs
        // arrives with four identical legend entries.
        plot->SetLabel(&run == &runs.first() ? qPrintable(m_series.name) : "");
    }

    m_chart->SetShowLegend(false);
    // Room for a rotated axis title. At the default the y title was clipped
    // mid-word, which reads as a rendering fault rather than as a long name.
    m_chart->SetAutoAxes(false);
    m_chart->SetBorders(66, 44, 14, 14);

    // ⚑ Whole frames only. Left to choose its own ticks the axis labelled a
    // four-frame series 0, 0.2, 0.4 ... 3, and there is no frame 1.4 -- an axis
    // offering readings the data cannot produce, which is the same fault as a
    // five-tick scale over a two-state flag.
    vtkAxis *bottom = m_chart->GetAxis(vtkAxis::BOTTOM);
    bottom->SetTitle(qPrintable(tr("Frame")));
    bottom->SetNotation(vtkAxis::FIXED_NOTATION);
    bottom->SetPrecision(0);
    if (!m_series.points.isEmpty()) {
        const int frames = m_series.points.size();
        bottom->SetBehavior(vtkAxis::FIXED);
        bottom->SetRange(0.5, frames + 0.5);

        // ⚑ The tick positions are STATED, not left to the axis to choose.
        // Asked only for a tick count it labelled a four-frame series 0, 2, 3,
        // 4: a frame 0 that does not exist, and no frame 1. An axis that
        // invents a reading is the same fault as a colour scale that does.
        // Every frame gets a tick, thinned to keep the labels legible on a long
        // sequence -- and thinned by a whole number of frames, so every tick
        // still lands on one.
        const int every = std::max(1, (frames + 10) / 11);
        vtkNew<vtkDoubleArray> at;
        vtkNew<vtkStringArray> labels;
        for (int frame = 1; frame <= frames; frame += every) {
            at->InsertNextValue(frame);
            labels->InsertNextValue(std::to_string(frame));
        }
        bottom->SetCustomTickPositions(at, labels);
    }
    m_chart->GetAxis(vtkAxis::LEFT)->SetTitle(qPrintable(
        m_series.unit == QStringLiteral("dimensionless")
            ? m_series.quantity
            : tr("%1 (%2)").arg(m_series.quantity, m_series.unit)));

    for (int axis : {vtkAxis::BOTTOM, vtkAxis::LEFT}) {
        m_chart->GetAxis(axis)->GetTitleProperties()->SetColor(0.88, 0.88, 0.90);
        m_chart->GetAxis(axis)->GetLabelProperties()->SetColor(0.78, 0.78, 0.80);
        m_chart->GetAxis(axis)->GetPen()->SetColorF(0.45, 0.45, 0.48);
    }

    m_view->renderWindow()->Render();
}

void PlotPanel::updateNote()
{
    const bool anything = !m_frames.isEmpty();
    m_export->setEnabled(anything && m_series.measuredCount() > 0);

    // ⚑ No chart at all until there is something to draw in it. An empty
    // vtkChartXY is a large dark rectangle with no axes, and a large dark
    // rectangle is exactly as consistent with "broken" as with "waiting" -- so
    // before a run the panel is the sentence saying what would appear here, and
    // nothing else.
    m_view->setVisible(anything);
    m_choice->setVisible(anything);
    m_choiceLabel->setVisible(anything);
    m_export->setVisible(anything);

    if (!anything) {
        m_note->setText(tr("Measure a sequence of targets, then a quantity can "
                           "be plotted against frame here. Place a virtual "
                           "extensometer on the image to plot the change in "
                           "distance between two points, which is how a "
                           "loading curve is read."));
        return;
    }

    const int measured = m_series.measuredCount();
    const int total = m_series.points.size();

    if (measured == total) {
        // Spelled out rather than left as "frame(s)". Qt's %n plural needs a
        // loaded translation to do anything, and without one the literal
        // brackets reach the screen.
        m_note->setText(total == 1
                            ? tr("%1 over 1 frame.").arg(m_series.name)
                            : tr("%1 over %2 frames.")
                                  .arg(m_series.name).arg(total));
        return;
    }

    if (measured == 0) {
        // The gauge case that actually happens: both anchors, or one of them,
        // sit where the field has holes.
        m_note->setText(tr("No frame could be read. An extensometer needs all "
                           "four measured points around each of its anchors, so "
                           "an anchor over a gap in the field reads nothing. "
                           "Move it onto well-correlated specimen."));
        return;
    }

    // ⚑ Said outright rather than left to be counted off the chart. A curve
    // with half its frames missing looks like a short test rather than a failed
    // measurement, and the breaks in the line are easy to read as style.
    m_note->setText(tr("%1 over %2 of %3 frames. The breaks are frames that "
                       "could not be read, not smoothing.")
                        .arg(m_series.name).arg(measured).arg(total));
}
