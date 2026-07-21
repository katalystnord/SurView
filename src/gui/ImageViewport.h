#pragma once

#include <QSize>
#include <QVTKOpenGLNativeWidget.h>
#include <vtkNew.h>
#include <vtkSmartPointer.h>

// Full definitions are required for the vtkNew<> members below: moc instantiates
// this class's destructor, and vtkNew's destructor needs the complete type.
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageActor.h>
#include <vtkInteractorStyleImage.h>
#include <vtkRenderer.h>

class QLabel;

// Central viewport of the workspace: renders a speckle image (and, later,
// displacement/strain field overlays) inside the Qt shell via VTK. Until an
// image is loaded it shows an on-screen hint so the next step is discoverable
// rather than a blank canvas.
class ImageViewport : public QVTKOpenGLNativeWidget
{
    Q_OBJECT

public:
    explicit ImageViewport(QWidget *parent = nullptr);

    // Load and display an image. Returns false if the file could not be read.
    bool loadImage(const QString &path);

    bool hasImage() const { return m_hasImage; }
    QSize imageSize() const { return m_imageSize; }  // invalid until an image loads

protected:
    void showEvent(QShowEvent *event) override;

private:
    void applyImageInteractorStyle();

    vtkNew<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkNew<vtkRenderer> m_renderer;
    vtkNew<vtkImageActor> m_imageActor;
    vtkNew<vtkInteractorStyleImage> m_style;

    QLabel *m_hint = nullptr;
    bool m_hasImage = false;
    QSize m_imageSize;
};
