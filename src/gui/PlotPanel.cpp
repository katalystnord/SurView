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
#include <vtkTextRenderer.h>
#include <vtkRenderWindow.h>

#include <algorithm>
#include <cmath>

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariant>

namespace {

// The chart's minimum margins, in device pixels. VTK widens them itself to hold
// the axis labels and titles.
constexpr int kLeftBorder = 66;
constexpr int kBottomBorder = 44;
constexpr int kRightBorder = 14;
constexpr int kTopBorder = 14;


// One choice in the selector: either a whole-field summary, or one gauge's
// reading. Carried as a pair of indices rather than a pointer, so a rebuilt
// gauge list cannot leave the selector holding a dangling entry.
struct PlotChoice
{
    int fieldIndex = -1;       // into offeredFieldSeries(), or -1
    int gaugeIndex = -1;       // into the gauge list, or -1
    bool profile = false;      // the line probe's profile
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
    m_view->installEventFilter(this);
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

vtkChartXY *PlotPanel::chart() const
{
    return m_chart;
}

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

void PlotPanel::setProbe(const LineProbe &probe)
{
    m_probe = probe;
    rebuildChoices();
    redraw();
    updateNote();
}

void PlotPanel::setProfileContext(int frameIndex, FieldChannel channel)
{
    m_profileFrame = frameIndex;
    m_profileChannel = channel;
    redraw();
    updateNote();
}

void PlotPanel::showProfile()
{
    for (int i = 0; i < m_choice->count(); i++) {
        if (m_choice->itemData(i).value<PlotChoice>().profile) {
            m_choice->setCurrentIndex(i);
            break;
        }
    }
    redraw();
    updateNote();
}

void PlotPanel::rebuildChoices()
{
    const QVariant kept = m_choice->currentData();
    const bool keptProfile = kept.isValid() && kept.value<PlotChoice>().profile;

    QSignalBlocker blocked(m_choice);
    m_choice->clear();

    // The probe first when there is one: it was placed to be looked at. Named
    // for what it follows rather than for a channel, because it changes with
    // the map on screen.
    if (m_probe.isValid()) {
        PlotChoice choice;
        choice.profile = true;
        m_choice->addItem(tr("Profile along %1 (the map on screen)").arg(m_probe.name),
                          QVariant::fromValue(choice));
    }

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
    if (keptProfile && m_probe.isValid()) {
        m_choice->setCurrentIndex(0);
        return;
    }
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
    if (choice.profile) {
        if (m_probe.isValid() && m_profileFrame >= 0 && m_profileFrame < m_frames.size())
            m_series = probeProfile(m_probe, m_frames.at(m_profileFrame), m_profileChannel,
                                    m_profileFrame + 1);
    } else if (choice.gaugeIndex >= 0 && choice.gaugeIndex < m_gauges.size()) {
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
        frameColumn->SetName(m_series.axis == SeriesAxis::Distance ? "Distance" : "Frame");
        table->AddColumn(frameColumn);
        vtkNew<vtkDoubleArray> valueColumn;
        valueColumn->SetName(qPrintable(m_series.name));
        table->AddColumn(valueColumn);
        table->SetNumberOfRows(run.size());

        for (int i = 0; i < run.size(); i++) {
            table->SetValue(i, 0, m_series.axis == SeriesAxis::Distance
                                      ? run.at(i).distance
                                      : double(run.at(i).frame));
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
    // A minimum: VTK widens the bands itself to hold the labels and titles.
    m_chart->SetAutoAxes(false);
    m_chart->SetBorders(kLeftBorder, kBottomBorder, kRightBorder, kTopBorder);

    // ⚑ Whole frames only. Left to choose its own ticks the axis labelled a
    // four-frame series 0, 0.2, 0.4 ... 3, and there is no frame 1.4 -- an axis
    // offering readings the data cannot produce, which is the same fault as a
    // five-tick scale over a two-state flag.
    vtkAxis *bottom = m_chart->GetAxis(vtkAxis::BOTTOM);
    bottom->SetNotation(vtkAxis::FIXED_NOTATION);
    bottom->SetPrecision(0);
    if (m_series.axis == SeriesAxis::Distance) {
        // Distance is continuous, so the axis may choose its own ticks; it
        // runs over the whole line, so where the profile has gaps at its ends
        // they show as gaps rather than as a shorter line.
        bottom->SetTitle(qPrintable(tr("Distance along %1 (px)").arg(m_probe.name)));
        bottom->SetCustomTickPositions(nullptr);
        bottom->SetBehavior(vtkAxis::FIXED);
        bottom->SetRange(0.0, m_probe.length());
    } else if (!m_series.points.isEmpty()) {
        bottom->SetTitle(qPrintable(tr("Frame")));
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
    m_fullYTitle = m_series.unit == QStringLiteral("dimensionless")
                       ? m_series.quantity
                       : tr("%1 (%2)").arg(m_series.quantity, m_series.unit);

    for (int axis : {vtkAxis::BOTTOM, vtkAxis::LEFT}) {
        m_chart->GetAxis(axis)->GetTitleProperties()->SetColor(0.88, 0.88, 0.90);
        m_chart->GetAxis(axis)->GetLabelProperties()->SetColor(0.78, 0.78, 0.80);
        m_chart->GetAxis(axis)->GetPen()->SetColorF(0.45, 0.45, 0.48);
    }

    layoutAxes();
}

namespace {

// What text takes on screen laid flat, in the render window's pixels, measured
// by the renderer that draws it, at the window's DPI.
QSize textExtent(vtkTextProperty *properties, const QString &text, int dpi)
{
    vtkNew<vtkTextProperty> flat;
    flat->ShallowCopy(properties);
    flat->SetOrientation(0.0);
    int box[4] = {0, 0, 0, 0};
    if (text.isEmpty()
        || !vtkTextRenderer::GetInstance()->GetBoundingBox(flat, text.toStdString(), box, dpi))
        return QSize();
    return QSize(box[1] - box[0] + 1, box[3] - box[2] + 1);
}

}  // namespace

void PlotPanel::layoutAxes()
{
    // ⚑ THE Y TITLE RUNS ALONG THE AXIS, so its LENGTH has the plot's height to
    // fit in, and in a short panel the full title did not: it read "acement
    // magnitude (me", cut at both ends. VTK's own layout already makes room
    // for the labels and the titles' thickness, which is why only this needs
    // doing here -- checked by putting fixed borders back, which changed none
    // of the bands the chart reserved.
    //
    // ⚑ The size is the WIDGET'S, in device pixels, not the render window's:
    // at the moment the view is resized the render window still reports its
    // previous size (250 device pixels for a view just made 700 tall), so a
    // decision taken against it shortened a title that had room to spare.
    vtkRenderWindow *window = m_view->renderWindow();
    const double ratio = m_view->devicePixelRatioF();
    const int height = int(std::lround(m_view->height() * ratio));
    if (height <= 0)
        return;
    const int dpi = window->GetDPI();
    vtkAxis *left = m_chart->GetAxis(vtkAxis::LEFT);
    vtkAxis *bottom = m_chart->GetAxis(vtkAxis::BOTTOM);

    // What the plot will have, vertically: the view less the band VTK keeps for
    // the x labels and title, and the fixed margin above.
    const int xBand = textExtent(bottom->GetLabelProperties(), QStringLiteral("0"), dpi).height()
                      + textExtent(bottom->GetTitleProperties(),
                                   QString::fromStdString(bottom->GetTitle()), dpi).height();
    const int plotHeight = height - xBand - kTopBorder - kBottomBorder;

    // When the full title does not fit, the unit alone, and the note names the
    // quantity -- a title cut mid-word reads as a fault in the rendering, where
    // a unit with the name beside the chart reads as a choice.
    QString yTitle = m_fullYTitle;
    m_yTitleShortened = false;
    if (textExtent(left->GetTitleProperties(), yTitle, dpi).width() > plotHeight) {
        m_yTitleShortened = true;
        yTitle = m_series.unit == QStringLiteral("dimensionless") || m_series.unit.isEmpty()
                     ? QString()
                     : m_series.unit;
        // No unit to give, or even that too long: a short ellipsis rather than
        // nothing, so the axis still visibly HAS a title.
        if (yTitle.isEmpty()
            || textExtent(left->GetTitleProperties(), yTitle, dpi).width() > plotHeight)
            yTitle = QStringLiteral("...");
    }
    left->SetTitle(qPrintable(yTitle));
    window->Render();
}

bool PlotPanel::eventFilter(QObject *watched, QEvent *event)
{
    // The chart's own size, not the panel's: the view is what the borders and
    // the title length are measured against.
    if (watched == m_view && event->type() == QEvent::Resize) {
        layoutAxes();
        updateNote();
    }
    return QWidget::eventFilter(watched, event);
}

void PlotPanel::updateNote()
{
    // When the axis could only carry its unit, the quantity is named here, so
    // shortening the title loses nothing a reader needs.
    const auto setNote = [this](const QString &text) {
        m_note->setText(m_yTitleShortened && !m_series.points.isEmpty()
                            ? text + QLatin1Char(' ')
                                  + tr("The vertical axis is too short for its full title, "
                                       "so it carries the unit alone: it shows %1.")
                                        .arg(m_fullYTitle)
                            : text);
    };
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
        setNote(tr("Measure a sequence of targets, then a quantity can "
                           "be plotted against frame here. Place a virtual "
                           "extensometer on the image to plot the change in "
                           "distance between two points, which is how a "
                           "loading curve is read."));
        return;
    }

    const int measured = m_series.measuredCount();
    const int total = m_series.points.size();

    if (m_series.axis == SeriesAxis::Distance) {
        if (fieldChannelIsFlag(m_profileChannel)) {
            setNote(tr("The map on screen has two states and nothing between "
                               "them, so it cannot be read along a line. Show another "
                               "map to see the profile along %1.")
                                .arg(m_probe.name));
            return;
        }
        // What the samples are, said where the curve is: one per grid step,
        // interpolated from the four grid points around each.
        const double spacing = total > 1 ? m_probe.length() / (total - 1) : 0.0;
        QString text = tr("%1: %2 of %3 samples read, one every %4 px along the line, "
                          "each interpolated from the four grid points around it.")
                           .arg(m_series.name)
                           .arg(measured)
                           .arg(total)
                           .arg(spacing, 0, 'f', 1);
        if (measured < total)
            text += QLatin1Char(' ')
                    + tr("The breaks are gaps in the field under the line, not "
                         "smoothing.");
        setNote(text);
        return;
    }

    if (measured == total) {
        // Spelled out rather than left as "frame(s)". Qt's %n plural needs a
        // loaded translation to do anything, and without one the literal
        // brackets reach the screen.
        setNote(total == 1
                            ? tr("%1 over 1 frame.").arg(m_series.name)
                            : tr("%1 over %2 frames.")
                                  .arg(m_series.name).arg(total));
        return;
    }

    if (measured == 0) {
        // The gauge case that actually happens: both anchors, or one of them,
        // sit where the field has holes.
        setNote(tr("No frame could be read. An extensometer needs all "
                           "four measured points around each of its anchors, so "
                           "an anchor over a gap in the field reads nothing. "
                           "Move it onto well-correlated specimen."));
        return;
    }

    // ⚑ Said outright rather than left to be counted off the chart. A curve
    // with half its frames missing looks like a short test rather than a failed
    // measurement, and the breaks in the line are easy to read as style.
    setNote(tr("%1 over %2 of %3 frames. The breaks are frames that "
                       "could not be read, not smoothing.")
                        .arg(m_series.name).arg(measured).arg(total));
}
