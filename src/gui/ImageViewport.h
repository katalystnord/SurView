#pragma once

#include "core/ImageRecord.h"
#include "core/Roi.h"

#include <QSize>
#include <QVTKOpenGLNativeWidget.h>
#include <vtkNew.h>
#include <vtkSmartPointer.h>

// Full definitions are required for the vtkNew<> members below: moc instantiates
// this class's destructor, and vtkNew's destructor needs the complete type.
#include <vtkActor.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageActor.h>
#include <vtkInteractorStyleImage.h>
#include <vtkLookupTable.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkRenderer.h>
#include <vtkScalarBarActor.h>

struct CorrelationResult;
class QFrame;
class QLabel;
class QPushButton;

// Central viewport of the workspace: renders a speckle image, the region of
// interest drawn on it, and displacement/strain field overlays, inside the Qt
// shell via VTK. Until an image is loaded it shows an on-screen hint so the
// next step is discoverable rather than a blank canvas.
//
// Coordinate frame: the renderer's world coordinates are image pixel
// coordinates -- x right, y DOWN, origin at the top-left pixel -- set up in the
// constructor. Every position this class accepts or reports is in that frame.
class ImageViewport : public QVTKOpenGLNativeWidget
{
    Q_OBJECT

public:
    explicit ImageViewport(QWidget *parent = nullptr);

    // Load and display an image. Returns false if the file could not be read.
    bool loadImage(const QString &path);

    // Show a message instead of an image, and stop displaying whatever was
    // there. Used when the selected image cannot be shown: leaving the previous
    // one on screen would attribute those pixels to the current selection.
    void showMessage(const QString &text);

    // Overlay a measured displacement field on the reference image, with the
    // colour scale that makes it readable as numbers rather than as a picture.
    void showField(const CorrelationResult &result);
    void clearField();
    bool hasField() const { return m_hasField; }

    bool hasImage() const { return m_hasImage; }

    // --- Region of interest ------------------------------------------------
    // Enter the mode where clicking the image places boundary corners. The
    // mode announces itself with an on-screen bar carrying its own controls,
    // so it can be entered, completed and left without knowing a shortcut.
    void beginRoiDrawing();
    void cancelRoiDrawing();
    bool isDrawingRoi() const { return m_roiDrawing; }

    // Display a region that already exists -- the one just drawn, or one the
    // detector proposed. Passing an invalid region clears the display.
    void showRoi(const RegionOfInterest &roi);
    void clearRoi();

    // Where an image pixel currently sits in this widget's own coordinates.
    // The inverse of the mapping a click takes, exposed because anything that
    // has to point AT the picture from outside the renderer needs it -- and
    // because a walkthrough test aiming at a feature has to aim through the
    // real projection, not through a second copy of the arithmetic.
    // False when there is no image to point at.
    bool widgetPositionForImagePixel(const QPointF &pixel,
                                     QPointF &position) const;

    // Provenance and pixel facts for the currently displayed image.
    const ImageRecord &record() const { return m_record; }

signals:
    // A boundary was completed. The region is in image pixel coordinates; the
    // viewport draws it, but the project owns it.
    void roiDrawn(const RegionOfInterest &roi);

    // Entering or leaving drawing mode, so the rest of the window can keep its
    // own controls consistent with a mode the user can see they are in.
    void roiDrawingChanged(bool drawing);

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void applyImageInteractorStyle();

    // Chooses the intensity window sent to screen and writes it into the
    // record, so the mapping is reported rather than silently applied.
    void applyDisplayMapping(ImageRecord &record);

    // --- ROI drawing internals ---------------------------------------------
    void buildRoiBar();
    void positionRoiBar();
    void finishRoiDrawing();
    void undoLastRoiVertex();
    void updateRoiBar();

    // Rebuilds the drawn geometry from whichever boundary is current -- the one
    // being placed, or the committed one.
    void refreshRoiGeometry();

    // Widget position to image pixel. False when there is no image to hit.
    // Positions beyond the image are held to its edge rather than refused, so
    // a boundary can be taken right up to the border.
    bool widgetToImagePixel(const QPointF &position, QPoint &pixel) const;

    vtkNew<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkNew<vtkRenderer> m_renderer;
    vtkNew<vtkImageActor> m_imageActor;
    vtkNew<vtkImageActor> m_fieldActor;
    vtkNew<vtkLookupTable> m_fieldColours;
    vtkNew<vtkScalarBarActor> m_scalarBar;
    vtkNew<vtkInteractorStyleImage> m_style;

    vtkNew<vtkPolyData> m_roiGeometry;
    vtkNew<vtkPolyDataMapper> m_roiMapper;
    vtkNew<vtkActor> m_roiActor;

    QLabel *m_hint = nullptr;
    bool m_hasImage = false;
    bool m_hasField = false;
    ImageRecord m_record;

    QFrame *m_roiBar = nullptr;
    QLabel *m_roiBarText = nullptr;
    QPushButton *m_roiUndo = nullptr;
    QPushButton *m_roiFinish = nullptr;

    bool m_roiDrawing = false;
    bool m_roiActorAdded = false;
    QVector<QPoint> m_roiPlaced;   // corners placed so far, while drawing
    QPoint m_roiCursor;            // where the rubber band currently reaches
    bool m_roiCursorValid = false;
    RegionOfInterest m_roiShown;   // the committed boundary on display
};
