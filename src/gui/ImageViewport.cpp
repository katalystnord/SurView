#include "ImageViewport.h"

#include "core/Correlation.h"
#include "core/FieldLayout.h"
#include "core/ImageDecode.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
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

    // On-screen guidance until the first image is loaded. QVTKOpenGLNativeWidget
    // composites regular Qt child widgets over its GL surface, so a plain QLabel
    // works as an overlay here.
    m_hint = new QLabel(
        tr("No image loaded\n\nFile ▸ Import Reference Image…"), this);
    m_hint->setAlignment(Qt::AlignCenter);
    m_hint->setStyleSheet(
        QStringLiteral("color: #9aa0a6; font-size: 15px; background: transparent;"));

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
    layout->addWidget(m_hint, 0, Qt::AlignCenter);
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

    // Tracking is on only for the duration, so the rubber band can follow the
    // pointer without the widget watching every movement the rest of the time.
    setMouseTracking(true);
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

    setMouseTracking(false);
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

    setMouseTracking(false);
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
    if (drawing)
        m_roiActor->GetProperty()->SetColor(1.0, 0.78, 0.25);   // amber: in progress
    else
        m_roiActor->GetProperty()->SetColor(0.28, 0.95, 0.62);  // green: in force

    if (!m_roiActorAdded) {
        m_renderer->AddActor(m_roiActor);
        m_roiActorAdded = true;
    }
    m_renderWindow->Render();
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

bool ImageViewport::widgetToImagePixel(const QPointF &position,
                                       QPoint &pixel) const
{
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
    pixel.setX(std::clamp(int(std::lround(x)), 0, m_record.width - 1));
    pixel.setY(std::clamp(int(std::lround(y)), 0, m_record.height - 1));
    return true;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void ImageViewport::mousePressEvent(QMouseEvent *event)
{
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
    QVTKOpenGLNativeWidget::mousePressEvent(event);
}

void ImageViewport::mouseMoveEvent(QMouseEvent *event)
{
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
    return true;
}

void ImageViewport::showField(const CorrelationResult &result)
{
    if (result.gridColumns <= 0 || result.gridRows <= 0) {
        clearField();
        return;
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
    const QVector<float> laid = layoutDisplacementMagnitude(result);
    auto *values = static_cast<float *>(field->GetScalarPointer());
    std::copy(laid.begin(), laid.end(), values);

    double lowest = 0.0;
    double highest = 0.0;
    if (!result.magnitudeRange(lowest, highest)) {
        clearField();
        return;
    }
    if (highest <= lowest)
        highest = lowest + 1e-6;  // a uniform field still needs a valid scale

    m_fieldColours->SetRange(lowest, highest);
    m_fieldColours->SetHueRange(0.667, 0.0);  // blue (least) to red (most)
    m_fieldColours->SetNanColor(0.55, 0.55, 0.58, 0.45);  // rejected: visibly not data
    m_fieldColours->Build();

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
    m_scalarBar->SetTitle("Displacement (px)");
    m_scalarBar->SetNumberOfLabels(5);
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

void ImageViewport::clearField()
{
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
