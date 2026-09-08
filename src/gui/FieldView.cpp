#include "gui/FieldView.h"

#include "gui/FieldColours.h"

#include <vtkCallbackCommand.h>
#include <vtkCamera.h>
#include <vtkImageData.h>
#include <vtkImageMapToColors.h>
#include <vtkImageProperty.h>
#include <vtkNew.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkTextProperty.h>

#include <algorithm>

FieldView::FieldView(QWidget *parent)
    : QVTKOpenGLNativeWidget(parent)
{
    m_renderer->SetBackground(0.11, 0.11, 0.13);

    // The same frame as the workspace viewport, and for the same reasons: world
    // coordinates ARE image pixels with y down, which means viewing the image
    // plane from -z with the up vector along -y. Turning the camera around
    // rather than only inverting up matters -- inverting up alone rotates the
    // view 180 degrees and mirrors x as well.
    vtkCamera *camera = m_renderer->GetActiveCamera();
    camera->ParallelProjectionOn();
    camera->SetPosition(0.0, 0.0, -1.0);
    camera->SetFocalPoint(0.0, 0.0, 0.0);
    camera->SetViewUp(0.0, -1.0, 0.0);

    m_renderWindow->AddRenderer(m_renderer);
    setRenderWindow(m_renderWindow);
    m_style->SetInteractionModeToImage2D();
    if (m_renderWindow->GetInteractor())
        m_renderWindow->GetInteractor()->SetInteractorStyle(m_style);

    m_scalarBar->SetLookupTable(m_colours);
    m_scalarBar->SetWidth(0.10);
    m_scalarBar->SetHeight(0.52);
    // Left of the edge, not against it: the labels are drawn to the RIGHT of
    // the bar, and at 0.83 the widest of them ran off the panel and lost its
    // last digit -- on the error scale, where the digits are the whole point.
    m_scalarBar->SetPosition(0.74, 0.06);
    // ⚑ Unconstrained, or VTK scales the text to the bar and a bar this narrow
    // gets a title too small to read, drawn over its own topmost label.
    m_scalarBar->SetUnconstrainedFontSize(true);
    m_scalarBar->GetLabelTextProperty()->SetFontSize(12);
    m_scalarBar->GetTitleTextProperty()->SetFontSize(13);
    m_scalarBar->SetVerticalTitleSeparation(6);

    // The camera is watched rather than the interactor, so a view driven by the
    // mouse and a view moved by ResetCamera both report the same thing.
    vtkNew<vtkCallbackCommand> moved;
    moved->SetClientData(this);
    moved->SetCallback([](vtkObject *, unsigned long, void *client, void *) {
        emit static_cast<FieldView *>(client)->cameraMoved();
    });
    camera->AddObserver(vtkCommand::ModifiedEvent, moved);
}

void FieldView::showImage(vtkImageData *image, double low, double high)
{
    if (!image)
        return;

    m_imageActor->SetInputData(image);
    if (high <= low)
        high = low + 1.0;   // a constant image still needs a valid window
    m_imageActor->GetProperty()->SetColorWindow(high - low);
    m_imageActor->GetProperty()->SetColorLevel(0.5 * (low + high));

    if (!m_hasImage) {
        m_renderer->AddActor(m_imageActor);
        m_hasImage = true;
    }
    m_renderer->ResetCamera();
    m_renderWindow->Render();
}

void FieldView::showField(const QVector<float> &cells, int columns, int rows,
                          double originX, double originY, double step,
                          const QString &scaleTitle, bool diverging, bool flag,
                          double lowest, double highest, int significantDigits)
{
    if (columns <= 0 || rows <= 0 || cells.size() != columns * rows)
        return;

    if (highest <= lowest) {
        // A uniform field still needs a valid scale, widened about the value
        // itself rather than upwards from it.
        const double nudge = std::max(1e-6, std::abs(highest) * 1e-3);
        lowest -= nudge;
        highest += nudge;
    }

    // One sample per point of interest, positioned by the grid's own origin and
    // spacing, so nothing is resampled -- neither the field nor the photograph.
    vtkNew<vtkImageData> field;
    field->SetDimensions(columns, rows, 1);
    field->SetOrigin(originX, originY, 0.0);
    field->SetSpacing(step, step, 1.0);
    field->AllocateScalars(VTK_FLOAT, 1);
    auto *values = static_cast<float *>(field->GetScalarPointer());
    std::copy(cells.begin(), cells.end(), values);

    buildFieldColourTable(m_colours, diverging, flag, lowest, highest);

    vtkNew<vtkImageMapToColors> colours;
    colours->SetInputData(field);
    colours->SetLookupTable(m_colours);
    colours->SetOutputFormatToRGBA();
    colours->Update();

    m_fieldActor->SetInputData(colours->GetOutput());
    m_fieldActor->SetOpacity(0.72);
    // In front of the photograph, which in this camera's frame is -z.
    m_fieldActor->SetPosition(0.0, 0.0, -0.1);

    m_scalarBar->SetLookupTable(m_colours);
    m_scalarBar->SetTitle(qPrintable(scaleTitle));
    if (flag) {
        m_scalarBar->SetNumberOfLabels(2);
        m_scalarBar->SetLabelFormat("%.0f");
    } else {
        m_scalarBar->SetNumberOfLabels(5);
        const QByteArray format = "%." + QByteArray::number(significantDigits) + "g";
        m_scalarBar->SetLabelFormat(format.constData());
    }

    if (!m_hasField) {
        m_renderer->AddActor(m_fieldActor);
        m_renderer->AddActor2D(m_scalarBar);
        m_hasField = true;
    }
    m_renderWindow->Render();
}

void FieldView::shareCameraWith(FieldView *other)
{
    if (!other || other == this)
        return;
    // ⚑ ONE camera object, not two kept in step. Two cameras synchronised by
    // signals drift apart the moment one of them is moved by something that
    // does not report it, and two panels showing different parts of the
    // specimen while captioned as a comparison is the one failure this screen
    // cannot survive.
    other->m_renderer->SetActiveCamera(m_renderer->GetActiveCamera());
    other->m_renderer->ResetCamera();
}

void FieldView::renderNow()
{
    m_renderWindow->Render();
}
