#include "ImageViewport.h"

#include "core/Correlation.h"
#include "core/FieldLayout.h"
#include "core/ImageDecode.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageActor.h>
#include <vtkImageData.h>
#include <vtkImageMapToColors.h>
#include <vtkImageProperty.h>
#include <vtkLookupTable.h>
#include <vtkMath.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkScalarBarActor.h>
#include <vtkTextProperty.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vtkInteractorStyleImage.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>

ImageViewport::ImageViewport(QWidget *parent)
    : QVTKOpenGLNativeWidget(parent)
{
    m_renderer->SetBackground(0.11, 0.11, 0.13);

    // The viewport's world coordinates ARE image coordinates: x to the right,
    // y DOWN, origin at the centre of the top-left pixel. That is the frame the
    // file's rows use, the frame the engine's own Point2D uses, and therefore
    // the frame a measured field, a region of interest and a mouse click can
    // all be expressed in without anybody converting between two conventions.
    //
    // VTK's own default is y up, so the image plane is viewed from behind it
    // (-z, looking towards +z) with the up vector pointing along -y. Turning
    // the camera around rather than only inverting the up vector matters:
    // inverting up alone rotates the view 180 degrees, mirroring x as well.
    // Anything drawn over the photograph therefore sits at NEGATIVE z, between
    // the camera and the image.
    vtkCamera *camera = m_renderer->GetActiveCamera();
    camera->ParallelProjectionOn();
    camera->SetPosition(0.0, 0.0, -1.0);
    camera->SetFocalPoint(0.0, 0.0, 0.0);
    camera->SetViewUp(0.0, -1.0, 0.0);

    m_renderWindow->AddRenderer(m_renderer);
    setRenderWindow(m_renderWindow);
    applyImageInteractorStyle();

    // Tracked permanently, not only while a mode wants it. Two things depend on
    // it -- the region's rubber band and the point readout -- and a readout that
    // followed the pointer only after a click would be a gesture nobody
    // discovers. It was previously switched on when region drawing began and
    // off when it ended.
    setMouseTracking(true);

    // On-screen guidance until the first image is loaded. QVTKOpenGLNativeWidget
    // composites regular Qt child widgets over its GL surface, so a plain QLabel
    // works as an overlay here.
    // ⚑ The WHOLE path, not only its first step. An empty workspace said "No
    // image loaded" and named one menu item, which is enough to begin and
    // nothing else: the shape of the work was discoverable only by doing it.
    // All four steps cost nothing to show, and the first is a button rather
    // than a description, so it can be taken from where it is explained.
    m_hint = new QLabel(this);
    m_hint->setTextFormat(Qt::RichText);
    m_hint->setAlignment(Qt::AlignCenter);
    // A table rather than centred lines: centred, the four numerals stagger
    // down the card and the eye cannot follow them as a sequence.
    m_hint->setText(tr(
        "<div align='center' style='color:#e6e9ee; font-size:17px; font-weight:600;'>"
        "Measure displacement and strain</div>"
        "<table cellspacing='0' cellpadding='4' style='margin-top:12px;'>"
        "<tr><td style='color:#4aa3e0; font-weight:bold;'>1</td>"
        "<td style='color:#9aa0a6; font-size:13px;'>Import the reference image, before loading</td></tr>"
        "<tr><td style='color:#4aa3e0; font-weight:bold;'>2</td>"
        "<td style='color:#9aa0a6; font-size:13px;'>Add the target images, one per load step</td></tr>"
        "<tr><td style='color:#4aa3e0; font-weight:bold;'>3</td>"
        "<td style='color:#9aa0a6; font-size:13px;'>Draw or auto-detect the region to measure</td></tr>"
        "<tr><td style='color:#4aa3e0; font-weight:bold;'>4</td>"
        "<td style='color:#9aa0a6; font-size:13px;'>Run the correlation</td></tr>"
        "</table>"));
    m_hint->setStyleSheet(QStringLiteral("background: transparent;"));

    m_hintAction = new QPushButton(tr("Import reference image…"), this);
    m_hintAction->setCursor(Qt::PointingHandCursor);
    m_hintAction->setStyleSheet(QStringLiteral(
        "QPushButton { background: #1a6fb5; color: white; border: none;"
        " padding: 9px 20px; border-radius: 7px; font-weight: 600; }"
        "QPushButton:hover { background: #2585d4; }"));
    connect(m_hintAction, &QPushButton::clicked, this,
            [this] { emit importReferenceRequested(); });

    // Key events (Enter to close a boundary, Esc to abandon it) only reach a
    // widget that can take focus by clicking. They are accelerators throughout:
    // every one of them also has a button on the bar built below.
    setFocusPolicy(Qt::StrongFocus);

    m_roiMapper->SetInputData(m_roiGeometry);
    m_roiActor->SetMapper(m_roiMapper);
    m_roiActor->GetProperty()->SetLineWidth(2.0);
    m_roiActor->GetProperty()->SetPointSize(8.0);
    // Unlit, so the colour on screen is the colour asked for and the boundary
    // stays equally legible over a dark and a bright part of the speckle.
    m_roiActor->GetProperty()->SetLighting(false);

    buildRoiBar();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch(1);
    layout->addWidget(m_hint, 0, Qt::AlignCenter);
    layout->addSpacing(20);
    layout->addWidget(m_hintAction, 0, Qt::AlignCenter);
    layout->addStretch(1);
}

// ---------------------------------------------------------------------------
// Region of interest
// ---------------------------------------------------------------------------

// The bar that appears while a boundary is being placed. It exists because the
// mode has to be visible and completable from what is on screen: a drawing mode
// that can only be finished by a keystroke is a mode nobody who has not been
// told about it can leave.
void ImageViewport::buildRoiBar()
{
    m_roiBar = new QFrame(this);
    m_roiBar->setStyleSheet(QStringLiteral(
        "QFrame { background: rgba(24, 26, 32, 235); border: 1px solid #4a4f5a;"
        " border-radius: 6px; }"
        "QLabel { color: #e8eaed; background: transparent; border: none; }"
        "QPushButton { color: #e8eaed; background: #2f333c; border: 1px solid"
        " #565c68; border-radius: 4px; padding: 4px 12px; }"
        "QPushButton:disabled { color: #7b8089; border-color: #3a3e46; }"
        "QPushButton:hover:enabled { background: #3a3f4a; }"));

    auto *row = new QHBoxLayout(m_roiBar);
    row->setContentsMargins(12, 8, 12, 8);
    row->setSpacing(10);

    m_roiBarText = new QLabel(m_roiBar);
    m_roiBarText->setWordWrap(true);
    // The text takes whatever the buttons leave and wraps into it. No minimum
    // width: one large enough to keep the wrapping tidy also stopped the label
    // shrinking when the bar was capped to the viewport width, and the text ran
    // on underneath the buttons.
    row->addWidget(m_roiBarText, 1);
    row->addSpacing(6);

    m_roiUndo = new QPushButton(tr("Undo corner"), m_roiBar);
    connect(m_roiUndo, &QPushButton::clicked, this,
            &ImageViewport::undoLastRoiVertex);
    row->addWidget(m_roiUndo);

    m_roiFinish = new QPushButton(tr("Close region"), m_roiBar);
    connect(m_roiFinish, &QPushButton::clicked, this,
            &ImageViewport::finishRoiDrawing);
    row->addWidget(m_roiFinish);

    auto *cancel = new QPushButton(tr("Cancel"), m_roiBar);
    connect(cancel, &QPushButton::clicked, this,
            &ImageViewport::cancelRoiDrawing);
    row->addWidget(cancel);

    m_roiBar->hide();
}

// The bar is placed by hand rather than put in the viewport's layout. In a
// layout it contributes to this widget's own size hint, so the first time it
// appeared the central widget demanded more room, the docks gave it up, and the
// photograph jumped sideways underneath the corners being placed on it. An
// overlay must not resize what it is overlaying.
//
// It sits along the BOTTOM edge, and as wide and short as it can be. Anything
// it covers is image the user cannot click on, and at the top -- over a centred
// specimen, in a tall narrow viewport -- it covered exactly the corners being
// placed. Low and wide keeps it clear of the subject and clear of the pointer.
void ImageViewport::positionRoiBar()
{
    if (!m_roiBar)
        return;

    constexpr int kMargin = 10;
    const int barWidth = std::max(120, width() - 2 * kMargin);

    // Full width, so the text has the whole line to wrap into and the bar stays
    // as low as that wrapping allows.
    int barHeight = m_roiBar->heightForWidth(barWidth);
    if (barHeight <= 0)
        barHeight = m_roiBar->sizeHint().height();

    m_roiBar->setGeometry(kMargin,
                          std::max(kMargin, height() - barHeight - kMargin),
                          barWidth, barHeight);
}

void ImageViewport::beginRoiDrawing()
{
    if (!m_hasImage || m_roiDrawing)
        return;

    m_roiDrawing = true;
    m_roiPlaced.clear();
    m_roiCursorValid = false;

    setCursor(Qt::CrossCursor);
    setFocus(Qt::OtherFocusReason);

    m_roiBar->show();
    m_roiBar->raise();
    updateRoiBar();
    refreshRoiGeometry();
    emit roiDrawingChanged(true);
}

void ImageViewport::cancelRoiDrawing()
{
    if (!m_roiDrawing)
        return;

    m_roiDrawing = false;
    m_roiPlaced.clear();
    m_roiCursorValid = false;

    unsetCursor();
    m_roiBar->hide();

    // Whatever boundary was already committed comes back on screen: abandoning
    // a new one does not discard the one that was there.
    refreshRoiGeometry();
    emit roiDrawingChanged(false);
}

void ImageViewport::finishRoiDrawing()
{
    // Guarded here as well as on the button: Enter reaches this directly.
    if (!m_roiDrawing || m_roiPlaced.size() < 3)
        return;

    RegionOfInterest roi;
    roi.vertices = m_roiPlaced;
    roi.origin = RegionOfInterest::Drawn;

    m_roiDrawing = false;
    m_roiPlaced.clear();
    m_roiCursorValid = false;

    unsetCursor();
    m_roiBar->hide();

    m_roiShown = roi;
    refreshRoiGeometry();

    emit roiDrawingChanged(false);
    emit roiDrawn(roi);
}

void ImageViewport::undoLastRoiVertex()
{
    if (!m_roiDrawing || m_roiPlaced.isEmpty())
        return;
    m_roiPlaced.removeLast();
    refreshRoiGeometry();
    updateRoiBar();
}

void ImageViewport::updateRoiBar()
{
    if (!m_roiDrawing)
        return;

    const int placed = m_roiPlaced.size();
    const bool enough = placed >= 3;

    QString where;
    if (m_roiCursorValid) {
        where = tr("  ·  pointer at %1, %2 px")
                    .arg(m_roiCursor.x())
                    .arg(m_roiCursor.y());
    }

    // Says what to do, how far along it is, and what is still missing -- the
    // count alone would not explain why "Close region" is refusing.
    m_roiBarText->setText(
        tr("<b>Defining a region of interest</b> - click the image to place "
           "corners.<br><span style='color:#a9b0bb;'>%1 placed%2 · %3 · "
           "double-click, right-click or Enter closes it · Esc cancels</span>")
            .arg(placed == 1 ? tr("1 corner") : tr("%1 corners").arg(placed),
                 where,
                 enough ? tr("ready to close")
                        : tr("at least 3 needed")));

    // The text changes width as the count and the pointer position change, so
    // the bar is re-placed with it rather than only when the widget resizes.
    positionRoiBar();

    m_roiUndo->setEnabled(placed > 0);
    m_roiFinish->setEnabled(enough);
    m_roiFinish->setToolTip(enough
                                ? tr("Close the boundary and keep this region")
                                : tr("A region needs at least 3 corners"));
}

void ImageViewport::showRoi(const RegionOfInterest &roi)
{
    m_roiShown = roi.isValid() ? roi : RegionOfInterest();
    refreshRoiGeometry();
}

void ImageViewport::clearRoi()
{
    m_roiShown = RegionOfInterest();
    refreshRoiGeometry();
}

void ImageViewport::refreshRoiGeometry()
{
    const bool drawing = m_roiDrawing;
    const QVector<QPoint> &ring = drawing ? m_roiPlaced : m_roiShown.vertices;
    const bool committed = !drawing && m_roiShown.isValid();

    if (ring.isEmpty()) {
        if (m_roiActorAdded) {
            m_renderer->RemoveActor(m_roiActor);
            m_roiActorAdded = false;
        }
        m_renderWindow->Render();
        return;
    }

    // Nearest the camera of everything drawn, so the boundary stays visible
    // over both the photograph and a field overlay covering it.
    constexpr double kRoiDepth = -0.2;

    vtkNew<vtkPoints> points;
    vtkNew<vtkCellArray> lines;
    vtkNew<vtkCellArray> markers;

    for (const QPoint &vertex : ring) {
        const vtkIdType id =
            points->InsertNextPoint(vertex.x(), vertex.y(), kRoiDepth);
        markers->InsertNextCell(1, &id);
    }

    const vtkIdType placed = vtkIdType(ring.size());
    auto segment = [&lines](vtkIdType from, vtkIdType to) {
        const vtkIdType ends[2] = {from, to};
        lines->InsertNextCell(2, ends);
    };

    for (vtkIdType i = 0; i + 1 < placed; i++)
        segment(i, i + 1);

    if (committed && placed >= 3) {
        segment(placed - 1, 0);
    } else if (drawing && m_roiCursorValid) {
        // While placing, the shape that WOULD be committed is drawn: a segment
        // from the last corner to the pointer, and the closing one back to the
        // first. Seeing the polygon before committing to it is the whole point
        // of a rubber band.
        const vtkIdType cursor =
            points->InsertNextPoint(m_roiCursor.x(), m_roiCursor.y(), kRoiDepth);
        segment(placed - 1, cursor);
        if (placed >= 2)
            segment(cursor, 0);
    }

    m_roiGeometry->SetPoints(points);
    m_roiGeometry->SetLines(lines);
    m_roiGeometry->SetVerts(markers);
    m_roiGeometry->Modified();

    // Being placed and being committed look different on purpose: an unfinished
    // boundary should not be mistaken for the region a run will use.
    if (drawing) {
        m_roiActor->GetProperty()->SetColor(1.0, 0.78, 0.25);   // amber: in progress
        m_roiActor->GetProperty()->SetPointSize(7.0);
    } else {
        m_roiActor->GetProperty()->SetColor(0.28, 0.95, 0.62);  // green: in force
        // Larger than the line is wide, so a committed region's corners read as
        // handles rather than as decoration. They can be dragged, and something
        // that can be dragged has to look like it.
        m_roiActor->GetProperty()->SetPointSize(11.0);
    }

    if (!m_roiActorAdded) {
        m_renderer->AddActor(m_roiActor);
        m_roiActorAdded = true;
    }
    m_renderWindow->Render();
}

// --- virtual extensometers --------------------------------------------------
//
// The same shape as region drawing, deliberately: a mode the user can see they
// are in, announced by a bar carrying its own way out. Two clicks and it is
// placed, which is the whole gesture -- a gauge is two points and nothing else.

void ImageViewport::buildGaugeBar()
{
    m_gaugeBar = new QFrame(this);
    m_gaugeBar->setStyleSheet(m_roiBar->styleSheet());

    auto *row = new QHBoxLayout(m_gaugeBar);
    row->setContentsMargins(12, 8, 12, 8);
    row->setSpacing(10);

    m_gaugeBarText = new QLabel(m_gaugeBar);
    m_gaugeBarText->setWordWrap(true);
    row->addWidget(m_gaugeBarText, 1);
    row->addSpacing(6);

    m_gaugeUndo = new QPushButton(tr("Undo anchor"), m_gaugeBar);
    connect(m_gaugeUndo, &QPushButton::clicked, this, [this] {
        if (m_gaugeAnchors.isEmpty())
            return;
        m_gaugeAnchors.removeLast();
        refreshGaugeGeometry();
        updateGaugeBar();
    });
    row->addWidget(m_gaugeUndo);

    auto *cancel = new QPushButton(tr("Cancel"), m_gaugeBar);
    connect(cancel, &QPushButton::clicked, this,
            &ImageViewport::cancelExtensometerPlacement);
    row->addWidget(cancel);

    m_gaugeBar->hide();
}

void ImageViewport::positionGaugeBar()
{
    if (!m_gaugeBar || m_gaugeBar->isHidden())
        return;

    constexpr int kMargin = 10;
    const int barWidth = std::max(200, width() - 2 * kMargin);
    int barHeight = m_gaugeBar->heightForWidth(barWidth);
    if (barHeight <= 0)
        barHeight = m_gaugeBar->sizeHint().height();

    m_gaugeBar->setGeometry(kMargin, height() - barHeight - kMargin,
                            barWidth, barHeight);
}

void ImageViewport::updateGaugeBar()
{
    if (!m_gaugeBar)
        return;

    m_gaugeUndo->setEnabled(!m_gaugeAnchors.isEmpty());
    m_gaugeBarText->setText(
        m_gaugeAnchors.isEmpty()
            ? tr("Virtual extensometer: click the FIRST point on the specimen. "
                 "It measures how the distance between two points changes over "
                 "the sequence, which is how a loading curve is read.")
            : tr("Now click the SECOND point. Put both on well-correlated "
                 "speckle: a gauge reads nothing on a frame where the field has "
                 "a gap under either anchor."));
    positionGaugeBar();
}

void ImageViewport::beginExtensometerPlacement()
{
    if (!m_hasImage || m_gaugePlacing)
        return;
    if (m_roiDrawing)
        cancelRoiDrawing();

    m_gaugePlacing = true;
    m_gaugeAnchors.clear();
    m_gaugeCursorValid = false;
    if (!m_gaugeBar)
        buildGaugeBar();
    updateGaugeBar();
    m_gaugeBar->show();
    m_gaugeBar->raise();
    positionGaugeBar();
    setCursor(Qt::CrossCursor);
    refreshGaugeGeometry();
    emit extensometerPlacingChanged(true);
}

void ImageViewport::cancelExtensometerPlacement()
{
    if (!m_gaugePlacing)
        return;
    m_gaugePlacing = false;
    m_gaugeAnchors.clear();
    m_gaugeCursorValid = false;
    if (m_gaugeBar)
        m_gaugeBar->hide();
    unsetCursor();
    refreshGaugeGeometry();
    emit extensometerPlacingChanged(false);
}

void ImageViewport::showExtensometers(const QVector<Extensometer> &gauges)
{
    m_gaugesShown = gauges;
    refreshGaugeGeometry();
}

void ImageViewport::refreshGaugeGeometry()
{
    // Nearer the camera than the region, so a gauge drawn across a boundary
    // stays readable.
    constexpr double kGaugeDepth = -0.25;

    vtkNew<vtkPoints> points;
    vtkNew<vtkCellArray> lines;
    vtkNew<vtkCellArray> markers;

    auto anchor = [&](double x, double y) {
        const vtkIdType id = points->InsertNextPoint(x, y, kGaugeDepth);
        markers->InsertNextCell(1, &id);
        return id;
    };
    auto segment = [&lines](vtkIdType from, vtkIdType to) {
        const vtkIdType ends[2] = {from, to};
        lines->InsertNextCell(2, ends);
    };

    for (const Extensometer &gauge : m_gaugesShown) {
        const vtkIdType a = anchor(gauge.ax, gauge.ay);
        const vtkIdType b = anchor(gauge.bx, gauge.by);
        segment(a, b);
    }

    if (m_gaugePlacing) {
        vtkIdType previous = -1;
        for (const QPoint &placed : m_gaugeAnchors)
            previous = anchor(placed.x(), placed.y());
        // The gauge that WOULD be placed, drawn to the pointer. Seeing the span
        // before committing to it is the whole point of a rubber band, and a
        // gauge's length is the one thing that matters about where it sits.
        if (previous >= 0 && m_gaugeCursorValid) {
            const vtkIdType cursor =
                points->InsertNextPoint(m_gaugeCursor.x(), m_gaugeCursor.y(),
                                        kGaugeDepth);
            segment(previous, cursor);
        }
    }

    if (points->GetNumberOfPoints() == 0) {
        if (m_gaugeActorAdded) {
            m_renderer->RemoveActor(m_gaugeActor);
            m_gaugeActorAdded = false;
        }
        m_renderWindow->Render();
        return;
    }

    m_gaugeGeometry->SetPoints(points);
    m_gaugeGeometry->SetLines(lines);
    m_gaugeGeometry->SetVerts(markers);
    m_gaugeGeometry->Modified();

    m_gaugeMapper->SetInputData(m_gaugeGeometry);
    m_gaugeActor->SetMapper(m_gaugeMapper);
    m_gaugeActor->GetProperty()->SetLineWidth(2.0);
    m_gaugeActor->GetProperty()->SetPointSize(10.0);
    // Amber while placing, as the region uses, and a distinct cyan once placed:
    // a gauge is not a region, and the two are drawn over the same picture.
    if (m_gaugePlacing)
        m_gaugeActor->GetProperty()->SetColor(1.0, 0.78, 0.25);
    else
        m_gaugeActor->GetProperty()->SetColor(0.36, 0.85, 0.95);

    if (!m_gaugeActorAdded) {
        m_renderer->AddActor(m_gaugeActor);
        m_gaugeActorAdded = true;
    }
    m_renderWindow->Render();
}

double ImageViewport::grabReachInPixels(const QPointF &position) const
{
    constexpr double kScreenReach = 11.0;

    QPoint here;
    QPoint away;
    if (!widgetToImagePixel(position, here)
        || !widgetToImagePixel(position + QPointF(kScreenReach, 0.0), away)) {
        return kScreenReach;
    }
    const double reach = std::abs(double(away.x() - here.x()));
    // Never zero: at a deep zoom, eleven screen pixels can map to less than one
    // image pixel, and a reach of zero would make the handles ungrabbable.
    return std::max(reach, 1.5);
}

bool ImageViewport::widgetPositionForImagePixel(const QPointF &pixel,
                                                QPointF &position) const
{
    if (!m_hasImage)
        return false;

    // World coordinates ARE image coordinates here, so the pixel goes straight
    // in; only the display convention differs (VTK counts rows from the bottom
    // and in device pixels, Qt from the top and in logical ones).
    m_renderer->SetWorldPoint(pixel.x(), pixel.y(), 0.0, 1.0);
    m_renderer->WorldToDisplay();

    double display[3] = {0.0, 0.0, 0.0};
    m_renderer->GetDisplayPoint(display);

    const double ratio = devicePixelRatioF();
    position.setX(display[0] / ratio);
    position.setY(double(height()) - display[1] / ratio);
    return true;
}

bool ImageViewport::widgetToImagePixel(const QPointF &position, QPoint &pixel,
                                       bool *insideImage) const
{
    if (insideImage)
        *insideImage = false;
    if (!m_hasImage || m_record.width <= 0 || m_record.height <= 0)
        return false;

    // Qt counts widget rows from the top and in logical units; VTK counts
    // display rows from the bottom and in device pixels.
    const double ratio = devicePixelRatioF();
    const double displayX = position.x() * ratio;
    const double displayY = (double(height()) - position.y()) * ratio;

    m_renderer->SetDisplayPoint(displayX, displayY, 0.0);
    m_renderer->DisplayToWorld();

    double world[4] = {0.0, 0.0, 0.0, 0.0};
    m_renderer->GetWorldPoint(world);
    if (world[3] == 0.0)
        return false;

    // World coordinates ARE image coordinates here (see the constructor), so
    // this is a rounding, not a conversion. The projection is parallel, so the
    // depth chosen above does not affect the x and y that come back.
    const double x = world[0] / world[3];
    const double y = world[1] / world[3];

    // Held to the image rather than refused: dragging a corner slightly past
    // the edge means the edge, and silently dropping the click would read as
    // the application having missed it.
    if (insideImage) {
        *insideImage = x >= 0.0 && x <= double(m_record.width - 1)
                       && y >= 0.0 && y <= double(m_record.height - 1);
    }

    pixel.setX(std::clamp(int(std::lround(x)), 0, m_record.width - 1));
    pixel.setY(std::clamp(int(std::lround(y)), 0, m_record.height - 1));
    return true;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void ImageViewport::mousePressEvent(QMouseEvent *event)
{
    if (m_gaugePlacing) {
        QPoint pixel;
        if (event->button() == Qt::LeftButton
            && widgetToImagePixel(event->position(), pixel)) {
            m_gaugeAnchors.append(pixel);
            m_gaugeCursor = pixel;
            m_gaugeCursorValid = true;

            if (m_gaugeAnchors.size() == 2) {
                const QPoint a = m_gaugeAnchors.at(0);
                const QPoint b = m_gaugeAnchors.at(1);
                // Two clicks in the same place would make a gauge of no length,
                // which divides by zero computing strain. Refused here, where
                // the bar can say so, rather than silently accepted and dropped
                // later where nothing explains the missing curve.
                if (a == b) {
                    m_gaugeAnchors.removeLast();
                    m_gaugeBarText->setText(
                        tr("Both anchors landed on the same pixel, so the gauge "
                           "would have no length. Click a second point further "
                           "away."));
                    refreshGaugeGeometry();
                    event->accept();
                    return;
                }
                m_gaugePlacing = false;
                m_gaugeAnchors.clear();
                m_gaugeCursorValid = false;
                m_gaugeBar->hide();
                unsetCursor();
                emit extensometerPlaced(a.x(), a.y(), b.x(), b.y());
                emit extensometerPlacingChanged(false);
                event->accept();
                return;
            }

            refreshGaugeGeometry();
            updateGaugeBar();
            event->accept();
            return;
        }
        event->accept();
        return;
    }
    if (m_roiDrawing) {
        QPoint pixel;
        if (event->button() == Qt::LeftButton
            && widgetToImagePixel(event->position(), pixel)) {
            m_roiPlaced.append(pixel);
            m_roiCursor = pixel;
            m_roiCursorValid = true;
            refreshRoiGeometry();
            updateRoiBar();
            event->accept();
            return;
        }
        if (event->button() == Qt::RightButton) {
            finishRoiDrawing();
            event->accept();
            return;
        }
        // Any other button is swallowed rather than passed on: the interactor's
        // own window/level and pan gestures would fight the mode.
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_roiShown.isValid()) {
        QPoint pixel;
        if (widgetToImagePixel(event->position(), pixel)) {
            const int corner =
                cornerNear(m_roiShown, pixel, grabReachInPixels(event->position()));
            if (corner >= 0) {
                m_draggingCorner = corner;
                setCursor(Qt::ClosedHandCursor);
                event->accept();
                return;
            }
        }
    }

    if (event->button() == Qt::LeftButton) {
        QPoint pixel;
        bool inside = false;
        if (widgetToImagePixel(event->position(), pixel, &inside) && inside)
            emit fieldPointPicked(QPointF(pixel));
    }

    // Passed on rather than accepted: the interactor's own pan and zoom still
    // belong to the left button, and claiming the click here would take them
    // away to add a readout.
    QVTKOpenGLNativeWidget::mousePressEvent(event);
}

void ImageViewport::mouseMoveEvent(QMouseEvent *event)
{
    if (m_gaugePlacing) {
        QPoint pixel;
        m_gaugeCursorValid = widgetToImagePixel(event->position(), pixel);
        if (m_gaugeCursorValid) {
            m_gaugeCursor = pixel;
            refreshGaugeGeometry();
        }
        // Falls through, so the readout still follows the pointer while a gauge
        // is being placed.
    }

    if (m_draggingCorner >= 0) {
        QPoint pixel;
        if (widgetToImagePixel(event->position(), pixel)) {
            m_roiShown = withCornerMoved(m_roiShown, m_draggingCorner, pixel);
            refreshRoiGeometry();
        }
        event->accept();
        return;
    }

    // The cursor is the affordance: a handle that can be picked up says so
    // before it is pressed, which is the only warning a reader gets that these
    // corners are not just decoration.
    if (!m_roiDrawing && m_roiShown.isValid()) {
        QPoint pixel;
        if (widgetToImagePixel(event->position(), pixel)
            && cornerNear(m_roiShown, pixel, grabReachInPixels(event->position())) >= 0) {
            setCursor(Qt::OpenHandCursor);
        } else {
            unsetCursor();
        }
    }

    if (!m_roiDrawing) {
        // Reported whether or not it lands on the picture: "off the field" is
        // an answer a readout has to be able to give, and going silent instead
        // would leave the last point on screen as though it were still under
        // the pointer.
        QPoint pixel;
        bool inside = false;
        if (widgetToImagePixel(event->position(), pixel, &inside))
            emit fieldPointHovered(QPointF(pixel), inside);
        else
            emit fieldPointHovered(QPointF(), false);
    }

    if (m_roiDrawing) {
        QPoint pixel;
        if (widgetToImagePixel(event->position(), pixel)) {
            m_roiCursor = pixel;
            m_roiCursorValid = true;
            refreshRoiGeometry();
            updateRoiBar();
        }
        event->accept();
        return;
    }
    QVTKOpenGLNativeWidget::mouseMoveEvent(event);
}

void ImageViewport::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_draggingCorner >= 0 && event->button() == Qt::LeftButton) {
        m_draggingCorner = -1;
        unsetCursor();
        // Announced like any other boundary change, so the project, the log and
        // any measured field that no longer matches all react the same way they
        // do to a region drawn from scratch.
        emit roiDrawn(m_roiShown);
        event->accept();
        return;
    }
    QVTKOpenGLNativeWidget::mouseReleaseEvent(event);
}

void ImageViewport::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Qt delivers the first click of a double click as an ordinary press, so
    // the corner under the pointer has already been placed by the time this
    // arrives; closing here neither adds nor removes one.
    if (m_roiDrawing && event->button() == Qt::LeftButton) {
        finishRoiDrawing();
        event->accept();
        return;
    }
    QVTKOpenGLNativeWidget::mouseDoubleClickEvent(event);
}

void ImageViewport::keyPressEvent(QKeyEvent *event)
{
    if (m_roiDrawing) {
        switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            finishRoiDrawing();
            event->accept();
            return;
        case Qt::Key_Escape:
            cancelRoiDrawing();
            event->accept();
            return;
        case Qt::Key_Backspace:
            undoLastRoiVertex();
            event->accept();
            return;
        default:
            break;
        }
    }
    QVTKOpenGLNativeWidget::keyPressEvent(event);
}

void ImageViewport::resizeEvent(QResizeEvent *event)
{
    QVTKOpenGLNativeWidget::resizeEvent(event);
    positionRoiBar();
    positionGaugeBar();
    positionFieldBar();
}

void ImageViewport::showEvent(QShowEvent *event)
{
    QVTKOpenGLNativeWidget::showEvent(event);
    // The interactor is created lazily; make sure our 2D style is attached once
    // the widget (and therefore its interactor) actually exists.
    applyImageInteractorStyle();
}

void ImageViewport::applyImageInteractorStyle()
{
    if (auto *interactor = m_renderWindow->GetInteractor())
        interactor->SetInteractorStyle(m_style);
}

bool ImageViewport::loadImage(const QString &path)
{
    // Record what arrived, before anything is done with it. The pixels are
    // handed to the actor exactly as decoded -- the only thing that varies is
    // how they are mapped to screen intensities, and that mapping is recorded
    // too rather than left implicit.
    ImageRecord record;
    vtkSmartPointer<vtkImageData> image = decodeImage(path, record);
    if (!image)
        return false;

    m_imageActor->SetInputData(image);
    applyDisplayMapping(record);

    if (!m_hasImage) {
        m_renderer->AddActor(m_imageActor);
        m_hasImage = true;
    }
    m_renderer->ResetCamera();
    m_renderWindow->Render();

    m_record = record;
    m_hint->hide();
    m_hintAction->hide();
    return true;
}

void ImageViewport::showField(const CorrelationResult &result)
{
    m_fieldResult = result;

    // A run that fitted no strain must not leave the display on a strain
    // channel, which would be an empty overlay with no explanation of itself.
    if (fieldChannelIsStrain(m_fieldChannel) && !m_fieldResult.hasStrain())
        m_fieldChannel = FieldChannel::DisplacementMagnitude;

    drawField();
    updateFieldBar();
}

void ImageViewport::drawField()
{
    const CorrelationResult &result = m_fieldResult;

    if (result.gridColumns <= 0 || result.gridRows <= 0) {
        clearField();
        return;
    }

    double lowest = 0.0;
    double highest = 0.0;
    if (!fieldColourRange(result, m_fieldChannel, lowest, highest)) {
        clearField();
        return;
    }
    if (highest <= lowest) {
        // A uniform field still needs a valid scale. Widened about the value
        // itself rather than upwards from it, so a uniform -0.002 does not
        // acquire a range that excludes it.
        const double nudge = std::max(1e-6, std::abs(highest) * 1e-3);
        lowest -= nudge;
        highest += nudge;
    }

    // The points of interest are a regular grid, so the field is an image in
    // its own right -- one sample per point, positioned by the grid's own origin
    // and spacing. That puts every value exactly over the pixels it was
    // measured from, with no resampling of either the field or the photograph.
    vtkNew<vtkImageData> field;
    field->SetDimensions(result.gridColumns, result.gridRows, 1);
    field->SetOrigin(result.originX, result.originY, 0.0);
    field->SetSpacing(result.step, result.step, 1.0);
    field->AllocateScalars(VTK_FLOAT, 1);

    // Laid out by core/FieldLayout.h, which is where that arithmetic is tested.
    const QVector<float> laid = layoutField(result, m_fieldChannel);
    auto *values = static_cast<float *>(field->GetScalarPointer());
    std::copy(laid.begin(), laid.end(), values);

    buildFieldColours(fieldChannelIsCentredOnZero(m_fieldChannel), lowest, highest);

    vtkNew<vtkImageMapToColors> colours;
    colours->SetInputData(field);
    colours->SetLookupTable(m_fieldColours);
    colours->SetOutputFormatToRGBA();
    colours->Update();

    m_fieldActor->SetInputData(colours->GetOutput());
    m_fieldActor->SetOpacity(0.72);
    // In front of the photograph, which in this camera's frame means towards
    // -z, so the two do not fight for the same depth.
    m_fieldActor->SetPosition(0.0, 0.0, -0.1);

    m_scalarBar->SetLookupTable(m_fieldColours);
    // Named with its unit, because "0.004" and "0.004 px" are different
    // measurements and the map alone cannot say which one is on screen.
    m_scalarBar->SetTitle(qPrintable(
        QStringLiteral("%1 (%2)")
            .arg(fieldChannelName(m_fieldChannel))
            .arg(fieldChannelUnit(m_fieldChannel))));
    if (fieldChannelIsFlag(m_fieldChannel)) {
        // ⚑ Two labels and no decimals, because the channel takes two values
        // and no others. Five evenly spaced ticks over a flag read 0, 0.25,
        // 0.5, 0.75, 1 and three of those cannot occur -- a scale offering
        // readings the data can never produce, which is the same fault as
        // centring a displacement scale on zero.
        m_scalarBar->SetNumberOfLabels(2);
        m_scalarBar->SetLabelFormat("%.0f");
    } else {
        m_scalarBar->SetNumberOfLabels(5);
        // Enough figures that the five labels differ from one another, and %g
        // so that a strain scale reads as 8.3e-08 rather than as 0.000000083.
        // Worked out and tested in core/FieldLayout.h.
        const QByteArray labelFormat =
            "%." + QByteArray::number(fieldScaleSignificantDigits(lowest, highest))
            + "g";
        m_scalarBar->SetLabelFormat(labelFormat.constData());
    }
    m_scalarBar->SetWidth(0.08);
    m_scalarBar->SetHeight(0.42);
    m_scalarBar->SetPosition(0.90, 0.06);

    if (!m_hasField) {
        m_renderer->AddActor(m_fieldActor);
        m_renderer->AddActor2D(m_scalarBar);
        m_hasField = true;
    }
    m_renderWindow->Render();
}

void ImageViewport::buildFieldColours(bool diverging, double lowest, double highest)
{
    // ⚑ A FLAG GETS TWO COLOURS, not a ramp through 254 it can never take. A
    // rainbow over a two-state channel says there is a continuum between the
    // states and invites a reader to look for the middle of it. Two flat
    // colours say what is true: a point is one thing or the other.
    if (fieldChannelIsFlag(m_fieldChannel)) {
        m_fieldColours->SetNumberOfTableValues(2);
        m_fieldColours->SetRange(lowest, highest);
        m_fieldColours->Build();
        // The quiet colour for the ordinary case, so the eye goes to the
        // repairs rather than to the bulk of the field that needed none.
        m_fieldColours->SetTableValue(0, 0.29, 0.40, 0.52, 1.0);
        m_fieldColours->SetTableValue(1, 0.98, 0.58, 0.16, 1.0);
        return;
    }

    constexpr int kEntries = 256;

    m_fieldColours->SetNumberOfTableValues(kEntries);
    m_fieldColours->SetRange(lowest, highest);
    m_fieldColours->Build();   // sizes the table; the entries below replace it

    for (int i = 0; i < kEntries; i++) {
        const double t = double(i) / double(kEntries - 1);
        double r = 0.0;
        double g = 0.0;
        double b = 0.0;

        if (diverging) {
            // Blue for one sign, red for the other, pale in the middle. A
            // sequential ramp cannot show a sign change: it puts compression
            // and tension at two ends of one continuum and hides the zero
            // crossing somewhere in the middle of the colours.
            const double s = t * 2.0 - 1.0;          // -1 .. +1
            const double m = std::abs(s);
            const double lo = 1.0 - m;
            if (s < 0.0) {
                r = 0.19 + 0.72 * lo;
                g = 0.34 + 0.57 * lo;
                b = 0.75 + 0.16 * lo;
            } else {
                r = 0.79 + 0.12 * lo;
                g = 0.22 + 0.69 * lo;
                b = 0.18 + 0.73 * lo;
            }
        } else {
            // The hue ramp the displacement field has always used: blue at the
            // least, red at the most.
            double hsv[3] = {0.667 * (1.0 - t), 1.0, 1.0};
            double rgb[3];
            vtkMath::HSVToRGB(hsv, rgb);
            r = rgb[0];
            g = rgb[1];
            b = rgb[2];
        }
        m_fieldColours->SetTableValue(i, r, g, b, 1.0);
    }

    // Rejected, unreached, or unfitted: visibly not data, in every channel.
    m_fieldColours->SetNanColor(0.55, 0.55, 0.58, 0.45);
    m_fieldColours->Modified();
}

void ImageViewport::buildFieldBar()
{
    m_fieldBar = new QFrame(this);
    m_fieldBar->setStyleSheet(QStringLiteral(
        "QFrame { background: rgba(24, 26, 32, 235); border: 1px solid #4a4f5a;"
        " border-radius: 6px; }"
        "QLabel { color: #e8eaed; background: transparent; border: none; }"
        "QComboBox { color: #e8eaed; background: #2f333c; border: 1px solid"
        " #565c68; border-radius: 4px; padding: 3px 8px; }"
        "QComboBox QAbstractItemView { color: #e8eaed; background: #2f333c;"
        " selection-background-color: #3a4250; }"));

    // ⚑ TWO ROWS, not one. Beside the selector the note had whatever width the
    // combo box left it -- about 130 px in a viewport 455 px wide -- so it
    // wrapped to eight lines and the bar grew until it covered the specimen
    // being measured. The same lesson the region bar already records one method
    // down: an overlay must not eat the picture it is overlaying. On its own
    // row the note has the full width and wraps to two lines.
    auto *stack = new QVBoxLayout(m_fieldBar);
    stack->setContentsMargins(12, 8, 12, 8);
    stack->setSpacing(6);

    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    stack->addLayout(row);

    row->addWidget(new QLabel(tr("Showing"), m_fieldBar));

    m_fieldChoice = new QComboBox(m_fieldBar);
    // Built from offeredFieldChannels(), the same list the layout and the
    // tests walk, so the selector cannot offer a channel nothing can draw.
    for (const FieldChannelInfo &channel : offeredFieldChannels()) {
        m_fieldChoice->addItem(channel.name, QVariant::fromValue(int(channel.channel)));
    }
    connect(m_fieldChoice, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0)
            return;
        const auto chosen = FieldChannel(m_fieldChoice->itemData(index).toInt());
        if (chosen == m_fieldChannel)
            return;
        m_fieldChannel = chosen;
        drawField();
        updateFieldBar();
        emit fieldChannelChanged(m_fieldChannel);
    });
    row->addWidget(m_fieldChoice);

    // Says why the strain entries are unselectable, when they are. A disabled
    // control with no stated reason is a dead end -- the reader cannot tell
    // whether it is broken, not yet reached, or not applicable.
    row->addStretch(1);

    m_fieldNote = new QLabel(m_fieldBar);
    m_fieldNote->setWordWrap(true);
    stack->addWidget(m_fieldNote);

    m_fieldBar->hide();
}

void ImageViewport::updateFieldBar()
{
    if (!m_fieldBar)
        buildFieldBar();

    const bool strainAvailable = m_fieldResult.hasStrain();
    // A recovery map over a run that never ran the pass would be a field of
    // solid "first solve" -- true, and indistinguishable from a bug. Offered
    // and disabled, with the reason said, exactly as the strain entries are.
    const bool recoveryAvailable = m_fieldResult.recoveryRequested;

    QSignalBlocker blocked(m_fieldChoice);
    for (int i = 0; i < m_fieldChoice->count(); i++) {
        const auto channel = FieldChannel(m_fieldChoice->itemData(i).toInt());
        const bool usable =
            fieldChannelIsStrain(channel)
                ? strainAvailable
                : (channel == FieldChannel::RecoveredOnSecondPass
                       ? recoveryAvailable
                       : true);
        auto *model = qobject_cast<QStandardItemModel *>(m_fieldChoice->model());
        if (model && model->item(i))
            model->item(i)->setEnabled(usable);
        if (channel == m_fieldChannel)
            m_fieldChoice->setCurrentIndex(i);
    }

    // The range the channel on display actually covers. Stated because the
    // colours alone cannot distinguish a field that varies from one that is
    // uniform to within float noise: both fill the same rainbow. With the
    // numbers beside it, a scale spanning 3.000000 to 3.000002 px explains
    // itself.
    QString range;
    double lowest = 0.0;
    double highest = 0.0;
    if (fieldChannelIsFlag(m_fieldChannel)) {
        // A flag has no range worth stating -- "runs from 0 to 1" says nothing
        // a reader did not already know from the legend. What they want is how
        // many, so that the map's scattering of colour has a number beside it.
        int flagged = 0;
        int measured = 0;
        for (const CorrelationPoint &point : m_fieldResult.points) {
            if (!point.converged)
                continue;
            measured++;
            if (point.recovered)
                flagged++;
        }
        range = flagged > 0
                    ? tr("%1 of the %2 measured points were repaired by the "
                         "second pass. ")
                          .arg(flagged)
                          .arg(measured)
                    : tr("No point needed the second pass. ");
    } else if (fieldValueRange(m_fieldResult, m_fieldChannel, lowest, highest)) {
        const int digits = fieldScaleSignificantDigits(lowest, highest);
        // Bare in prose, and only when there is a unit to name: "runs from
        // 0.004 to 0.006 dimensionless" is not a sentence anyone writes, and
        // "is 3 (px)" reads as a footnote marker rather than a unit.
        const QString unit =
            fieldChannelIsDimensionless(m_fieldChannel)
                ? QString()
                : QLatin1Char(' ') + fieldChannelUnit(m_fieldChannel);
        range = lowest == highest
                    // Said as one value rather than as a range from a number
                    // to itself, which reads as a rounding artefact.
                    ? tr("%1 is %2%3 at every measured point. ")
                          .arg(fieldChannelName(m_fieldChannel))
                          .arg(lowest, 0, 'g', digits)
                          .arg(unit)
                    : tr("%1 runs from %2 to %3%4. ")
                          .arg(fieldChannelName(m_fieldChannel))
                          .arg(lowest, 0, 'g', digits)
                          .arg(highest, 0, 'g', digits)
                          .arg(unit);
    }

    // What the channel on display is NOT, from core/FieldLayout.h, ahead of
    // everything else: it is the sentence that stops the number beside it being
    // read as more than it is, and it belongs where the number is.
    QString note = fieldChannelNote(m_fieldChannel) + QLatin1Char(' ') + range;

    if (fieldChannelIsReliability(m_fieldChannel)) {
        if (m_fieldChannel == FieldChannel::NoiseFloor) {
            // A noise floor read on its own is unreadable. This puts it against
            // the movement it qualifies, from the run's own numbers.
            const QString context = noiseFloorAgainstMovement(m_fieldResult);
            if (!context.isEmpty())
                note += context + QLatin1Char(' ');
        }
        if (m_fieldChannel == FieldChannel::MatchConditioning
            && m_fieldResult.conditioningUnusable > 0) {
            note += tr("The cost was too flat to probe at %1 solved point(s), "
                       "which is a caution rather than a gap: those points are "
                       "blank here.")
                        .arg(m_fieldResult.conditioningUnusable);
        }
    } else if (strainAvailable) {
        note += tr("Strain fitted at %1 of %2 points, over a %3 px subregion.")
                    .arg(m_fieldResult.strainFitted)
                    .arg(m_fieldResult.total())
                    .arg(m_fieldResult.strainRadius, 0, 'g', 4);
    } else if (m_fieldResult.strainRequested) {
        note += tr("Strain was fitted at no point in this run, so the strain "
                   "channels are unavailable.");
    } else {
        note += tr("Strain was not fitted for this run. Turn it on in the "
                   "Analysis panel and run again.");
    }

    // Why the recovery map is unselectable, when it is. A disabled entry with
    // no stated reason is a dead end: the reader cannot tell whether it is
    // broken, not yet reached, or not applicable.
    if (!recoveryAvailable) {
        note += QLatin1Char(' ')
                + tr("The second pass at poorly correlated points did not run, "
                     "so there is no repair map. Turn it on in the Analysis "
                     "panel and run again.");
    }

    m_fieldNote->setText(note);

    m_fieldBar->show();
    m_fieldBar->raise();
    positionFieldBar();
}

void ImageViewport::positionFieldBar()
{
    if (!m_fieldBar || m_fieldBar->isHidden())
        return;

    constexpr int kMargin = 10;
    // Top of the viewport: the region-drawing bar owns the bottom, and the two
    // are on screen together whenever a region is redrawn over a field.
    const int barWidth = std::max(120, width() - 2 * kMargin);
    int barHeight = m_fieldBar->heightForWidth(barWidth);
    if (barHeight <= 0)
        barHeight = m_fieldBar->sizeHint().height();

    m_fieldBar->setGeometry(kMargin, kMargin, barWidth, barHeight);
}

void ImageViewport::clearField()
{
    if (m_fieldBar)
        m_fieldBar->hide();
    if (!m_hasField)
        return;
    m_renderer->RemoveActor(m_fieldActor);
    m_renderer->RemoveActor2D(m_scalarBar);
    m_hasField = false;
    m_renderWindow->Render();
}

void ImageViewport::showMessage(const QString &text)
{
    // A boundary is a statement about pixels. With no pixels on screen it has
    // nothing to sit on, so it goes with them rather than floating over the
    // message. The project still holds it; only the drawing stops.
    cancelRoiDrawing();
    m_roiShown = RegionOfInterest();
    refreshRoiGeometry();

    if (m_hasImage) {
        m_renderer->RemoveActor(m_imageActor);
        m_hasImage = false;
        m_renderWindow->Render();
    }
    m_record = ImageRecord();
    m_hint->setText(text);
    m_hint->show();
    if (m_hintAction)
        m_hintAction->hide();
}

void ImageViewport::applyDisplayMapping(ImageRecord &record)
{
    // Single-channel scientific images routinely occupy a small part of their
    // type's range (a 16-bit speckle image sitting in 0-5000 would render very
    // nearly black), so the full data range is mapped to black..white. Colour
    // images already sit in their type range, so they are left alone rather
    // than having their appearance stretched.
    double lo = record.dataMin;
    double hi = record.dataMax;
    if (record.components > 1 && record.hasTypeRange()) {
        lo = record.typeMin();
        hi = record.typeMax();
    }
    if (hi <= lo)
        hi = lo + 1.0;  // a constant image still needs a valid window

    m_imageActor->GetProperty()->SetColorWindow(hi - lo);
    m_imageActor->GetProperty()->SetColorLevel(0.5 * (lo + hi));

    record.displayed  = true;
    record.displayMin = lo;
    record.displayMax = hi;
}
