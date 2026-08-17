#include "ImageViewport.h"

#include "core/Correlation.h"
#include "core/ImageDecode.h"

#include <QLabel>
#include <QShowEvent>
#include <QVBoxLayout>

#include <vtkCamera.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageActor.h>
#include <vtkImageData.h>
#include <vtkImageMapToColors.h>
#include <vtkImageProperty.h>
#include <vtkLookupTable.h>
#include <vtkScalarBarActor.h>
#include <vtkTextProperty.h>

#include <cmath>
#include <limits>
#include <vtkInteractorStyleImage.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>

ImageViewport::ImageViewport(QWidget *parent)
    : QVTKOpenGLNativeWidget(parent)
{
    m_renderer->SetBackground(0.11, 0.11, 0.13);
    m_renderer->GetActiveCamera()->ParallelProjectionOn();
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

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_hint, 0, Qt::AlignCenter);
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
    // handed to the actor exactly as decoded — the only thing that varies is
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
    // its own right — one sample per point, positioned by the grid's own origin
    // and spacing. That puts every value exactly over the pixels it was
    // measured from, with no resampling of either the field or the photograph.
    vtkNew<vtkImageData> field;
    field->SetDimensions(result.gridColumns, result.gridRows, 1);
    field->SetOrigin(result.originX, result.originY, 0.0);
    field->SetSpacing(result.step, result.step, 1.0);
    field->AllocateScalars(VTK_FLOAT, 1);

    auto *values = static_cast<float *>(field->GetScalarPointer());
    for (int i = 0; i < result.points.size(); i++) {
        const CorrelationPoint &point = result.points.at(i);
        // A point the solver rejected is not a displacement of zero. NaN keeps
        // it out of the colour scale entirely, and out of the range the legend
        // reports.
        values[i] = point.converged
                        ? float(std::hypot(point.u, point.v))
                        : std::numeric_limits<float>::quiet_NaN();
    }

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
    // Forward of the photograph so the two do not fight for the same depth.
    m_fieldActor->SetPosition(0.0, 0.0, 0.1);

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
    // type's range (a 16-bit speckle image sitting in 0–5000 would render very
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
