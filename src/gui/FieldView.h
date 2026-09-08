#pragma once

#include <QString>
#include <QVector>

#include <QVTKOpenGLNativeWidget.h>
#include <vtkNew.h>

#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageActor.h>
#include <vtkInteractorStyleImage.h>
#include <vtkLookupTable.h>
#include <vtkRenderer.h>
#include <vtkScalarBarActor.h>

class vtkImageData;

// One panel of a side-by-side field display: a speckle image with one laid-out
// field drawn over it, its own scale bar, and nothing else.
//
// Deliberately not ImageViewport. That widget is the workspace's single central
// view and owns modes, clicks and bars that only make sense there -- region
// drawing, gauge placement, a channel selector, the point readout. A panel in a
// comparison is a picture and a scale, told what to draw by whatever put it on
// screen, and giving each of three panels its own channel selector would let
// them disagree about what they are comparing.
//
// Coordinate frame: the same one as everywhere else. World coordinates are
// image pixels, x right, y DOWN, origin at the top-left pixel, so a field lays
// straight over the photograph it was measured from.
class FieldView : public QVTKOpenGLNativeWidget
{
    Q_OBJECT

public:
    explicit FieldView(QWidget *parent = nullptr);

    // The photograph behind the field. `low` and `high` are the intensity
    // window, taken from the record so the picture looks the same here as it
    // does in the workspace.
    void showImage(vtkImageData *image, double low, double high);

    // The field itself: one value per grid cell, not-a-number where nothing was
    // measured, positioned by the grid's own origin and spacing so every value
    // sits exactly over the pixels it came from.
    //
    // The colour range is given rather than derived, because the whole point of
    // this widget is that two panels can be told to use the SAME one.
    void showField(const QVector<float> &cells, int columns, int rows,
                   double originX, double originY, double step,
                   const QString &scaleTitle, bool diverging, bool flag,
                   double lowest, double highest, int significantDigits);

    // Pan and zoom every view given the same camera together, so two panels
    // cannot end up showing different parts of the specimen while being read as
    // a comparison. Called after both views exist.
    void shareCameraWith(FieldView *other);

    // Re-render without touching the camera. Used by whatever owns a group of
    // views to keep the others up to date when one of them is driven.
    void renderNow();

signals:
    // The camera moved, which for a shared camera means every other view is now
    // out of date on screen.
    void cameraMoved();

private:
    vtkNew<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkNew<vtkRenderer> m_renderer;
    vtkNew<vtkImageActor> m_imageActor;
    vtkNew<vtkImageActor> m_fieldActor;
    vtkNew<vtkLookupTable> m_colours;
    vtkNew<vtkScalarBarActor> m_scalarBar;
    vtkNew<vtkInteractorStyleImage> m_style;

    bool m_hasImage = false;
    bool m_hasField = false;
};
