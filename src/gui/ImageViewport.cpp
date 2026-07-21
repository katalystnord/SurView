#include "ImageViewport.h"

#include <QLabel>
#include <QShowEvent>
#include <QVBoxLayout>

#include <vtkCamera.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageActor.h>
#include <vtkImageData.h>
#include <vtkImageReader2.h>
#include <vtkImageReader2Factory.h>
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
    vtkSmartPointer<vtkImageReader2> reader;
    reader.TakeReference(
        vtkImageReader2Factory::CreateImageReader2(path.toUtf8().constData()));
    if (!reader)
        return false;  // no reader for this file type

    reader->SetFileName(path.toUtf8().constData());
    reader->Update();

    vtkImageData *image = reader->GetOutput();
    if (!image)
        return false;

    int dims[3] = {0, 0, 0};
    image->GetDimensions(dims);
    if (dims[0] <= 0 || dims[1] <= 0)
        return false;  // reader produced nothing usable

    m_imageActor->SetInputData(image);
    if (!m_hasImage) {
        m_renderer->AddActor(m_imageActor);
        m_hasImage = true;
    }
    m_renderer->ResetCamera();
    m_renderWindow->Render();

    m_imageSize = QSize(dims[0], dims[1]);
    m_hint->hide();
    return true;
}
