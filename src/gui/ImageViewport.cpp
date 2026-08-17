#include "ImageViewport.h"

#include "core/ImageDecode.h"

#include <QLabel>
#include <QShowEvent>
#include <QVBoxLayout>

#include <vtkCamera.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageActor.h>
#include <vtkImageData.h>
#include <vtkImageProperty.h>
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
