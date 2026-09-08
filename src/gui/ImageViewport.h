#pragma once

#include "core/Correlation.h"
#include "core/FieldLayout.h"
#include "core/ImageRecord.h"
#include "core/Roi.h"
#include "core/Series.h"
#include "core/SubsetOverlay.h"

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

class QComboBox;
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

    // Overlay a measured field on the reference image, with the colour scale
    // that makes it readable as numbers rather than as a picture. Which of the
    // result's channels is drawn is chosen on screen, from the bar this puts up
    // alongside it; a result that carries no strain falls back to displacement
    // rather than offering an empty map.
    void showField(const CorrelationResult &result);
    void clearField();
    bool hasField() const { return m_hasField; }

    FieldChannel fieldChannel() const { return m_fieldChannel; }

    bool hasImage() const { return m_hasImage; }

    // --- Region of interest ------------------------------------------------
    // Enter the mode where clicking the image places boundary corners. The
    // mode announces itself with an on-screen bar carrying its own controls,
    // so it can be entered, completed and left without knowing a shortcut.
    void beginRoiDrawing();

    // The same gesture, adding a hole to the region already in force rather
    // than replacing it. A hole is a ring like any other, so it reuses the
    // drawing machinery entirely and differs only in what is done with the
    // ring at the end.
    void beginHoleDrawing();
    void cancelRoiDrawing();
    bool isDrawingRoi() const { return m_roiDrawing; }
    bool isDrawingHole() const { return m_roiDrawing && m_drawingHole; }

    // Display a region that already exists -- the one just drawn, or one the
    // detector proposed. Passing an invalid region clears the display.
    void showRoi(const RegionOfInterest &roi);
    void clearRoi();

    // --- Virtual extensometers ---------------------------------------------
    // Two clicks on the specimen place a gauge. The same shape as region
    // drawing, and for the same reason: a mode the user can see they are in,
    // announced by a bar carrying its own way out, so nothing here needs a
    // shortcut to be discovered.
    void beginExtensometerPlacement();
    void cancelExtensometerPlacement();
    bool isPlacingExtensometer() const { return m_gaugePlacing; }

    // Draw the gauges that exist, so a placed extensometer stays visible on
    // the picture it measures rather than only in the plot's legend.
    void showExtensometers(const QVector<Extensometer> &gauges);

    // --- the settings, drawn at the size they will be measured at ----------
    // The subset square and the strain subregion over the reference image,
    // behind their own switches. See core/SubsetOverlay.h for what is drawn and
    // why it is snapped to the run's own grid.
    //
    // ⚑ It follows the pointer, and STAYS where the pointer last was when that
    // leaves the picture -- so the box can be put over a patch of speckle and
    // then read, rather than only ever being under a moving cursor. Before the
    // pointer has been over the image at all it sits at the middle of it, so
    // switching it on shows something rather than nothing.
    void setSettingsPreview(bool showSubset, bool showSubregion, int subsetRadius,
                            int gridStep, bool strainEnabled, double strainRadius);

    // What is currently drawn. Exposed so a walkthrough test can assert on the
    // geometry the screen is actually showing rather than on the settings it
    // was asked for; nothing in the application reads it.
    const SubsetOverlay &settingsPreview() const { return m_preview; }

    // Whether the individual points of the subregion are on screen. They are
    // drawn only while the grid is open enough to tell them apart -- see
    // refreshSettingsPreview() -- so this is what the picture is showing,
    // as against what settingsPreview() says the fit would use.
    bool settingsPreviewShowsNeighbours() const { return m_previewNeighboursDrawn; }

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
    // The empty workspace's own first step was pressed. The viewport does not
    // import anything itself; it only offers the step where the step is
    // explained, which is the whole point of offering it there.
    void importReferenceRequested();

    // Where the pointer is over the picture, in image pixels, and whether it is
    // over the picture at all. Emitted continuously, which is why this widget
    // tracks the mouse: a readout that only followed a drag would be a gesture
    // nobody discovers.
    void fieldPointHovered(const QPointF &imagePixel, bool insideImage);

    // A point of the picture was clicked, outside any mode that claims clicks
    // for itself. What that means is the window's business, not the viewport's.
    void fieldPointPicked(const QPointF &imagePixel);

    // The displayed channel changed, so anything reporting on the field can say
    // which one it is reporting on.
    void fieldChannelChanged(FieldChannel channel);

    // A boundary was completed. The region is in image pixel coordinates; the
    // viewport draws it, but the project owns it.
    void roiDrawn(const RegionOfInterest &roi);

    // A ring was completed as a hole. The viewport does not own the region, so
    // it hands the ring over and the window decides what to attach it to.
    void holeDrawn(const QVector<QPoint> &ring);

    // Entering or leaving drawing mode, so the rest of the window can keep its
    // own controls consistent with a mode the user can see they are in.
    void roiDrawingChanged(bool drawing);

    // Both anchors of a gauge have been placed, in image pixels. The viewport
    // draws it; the project owns it and gives it a name.
    void extensometerPlaced(double ax, double ay, double bx, double by);
    void extensometerPlacingChanged(bool placing);

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void applyImageInteractorStyle();

    // --- field display -----------------------------------------------------
    void buildFieldBar();
    void positionFieldBar();
    void updateFieldBar();
    void drawField();

    // Fills the lookup table for the channel on display. A channel whose zero
    // is a physical state gets a diverging scale about it; the rest get a
    // sequential one.
    void buildFieldColours(bool diverging, double lowest, double highest);

    // Chooses the intensity window sent to screen and writes it into the
    // record, so the mapping is reported rather than silently applied.
    void applyDisplayMapping(ImageRecord &record);

    // --- ROI drawing internals ---------------------------------------------
    void buildFrameLegend();
    void positionFrameLegend();
    void buildRoiBar();
    void buildGaugeBar();
    void positionGaugeBar();
    void updateGaugeBar();
    void refreshGaugeGeometry();
    void positionRoiBar();
    void finishRoiDrawing();
    void undoLastRoiVertex();
    void updateRoiBar();

    // Rebuilds the subset and subregion drawing from the settings and wherever
    // the pointer last was.
    void refreshSettingsPreview();

    // How far apart two neighbouring grid points are in widget pixels, through
    // the renderer's own projection. Zero when there is nothing to measure.
    double gridStepOnScreen() const;

    // The spacing, in widget pixels, below which a lattice of marks stops being
    // a lattice and becomes a smear. Chosen by looking at the screen.
    static constexpr double kLegibleSpacing = 5.0;

    // Rebuilds the drawn geometry from whichever boundary is current -- the one
    // being placed, or the committed one.
    void refreshRoiGeometry();

    // Widget position to image pixel. False when there is no image to hit.
    // Positions beyond the image are held to its edge rather than refused, so
    // a boundary can be taken right up to the border. `insideImage`, when
    // asked for, reports whether the position was over the picture BEFORE that
    // clamping -- which a readout needs and a boundary corner does not: held to
    // the edge, a pointer beyond the picture would otherwise report on whatever
    // point happens to sit at the border.
    bool widgetToImagePixel(const QPointF &position, QPoint &pixel,
                            bool *insideImage = nullptr) const;

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

    vtkNew<vtkPolyData> m_previewGeometry;
    vtkNew<vtkPolyDataMapper> m_previewMapper;
    vtkNew<vtkActor> m_previewActor;
    bool m_previewActorAdded = false;

    // What the preview is asked to draw, and where it last drew it.
    bool m_previewSubset = false;
    bool m_previewSubregion = false;
    int m_previewSubsetRadius = 0;
    int m_previewGridStep = 0;
    bool m_previewStrainEnabled = false;
    double m_previewStrainRadius = 0.0;
    QPointF m_previewCentre;
    bool m_previewCentreChosen = false;
    SubsetOverlay m_preview;
    bool m_previewNeighboursDrawn = false;

    vtkNew<vtkPolyData> m_gaugeGeometry;
    vtkNew<vtkPolyDataMapper> m_gaugeMapper;
    vtkNew<vtkActor> m_gaugeActor;

    QLabel *m_hint = nullptr;
    QPushButton *m_hintAction = nullptr;
    bool m_hasImage = false;
    bool m_hasField = false;
    ImageRecord m_record;

    QFrame *m_fieldBar = nullptr;
    QComboBox *m_fieldChoice = nullptr;
    QLabel *m_fieldNote = nullptr;

    // The result on display, kept so the channel can be changed without
    // re-running anything. The viewport owns a copy rather than a pointer: the
    // project may drop its result at any time, and a stale pointer here would
    // be drawn over the image.
    CorrelationResult m_fieldResult;
    FieldChannel m_fieldChannel = FieldChannel::DisplacementMagnitude;

    // The coordinate-frame legend: a small permanent overlay saying which way
    // the axes run. Shown whenever there is a picture to orient, because a
    // convention with nothing on screen to apply it to is just trivia.
    QFrame *m_frameLegend = nullptr;

    QFrame *m_roiBar = nullptr;
    QFrame *m_gaugeBar = nullptr;
    QLabel *m_gaugeBarText = nullptr;
    QPushButton *m_gaugeUndo = nullptr;
    QLabel *m_roiBarText = nullptr;
    QPushButton *m_roiUndo = nullptr;
    QPushButton *m_roiFinish = nullptr;

    // Which corner of the committed region is being dragged, or -1. A region
    // could only be redrawn from scratch before this: one corner slightly wrong
    // meant placing all of them again.
    int m_draggingCorner = -1;

    // How far from a corner, in IMAGE pixels, still counts as grabbing it. Kept
    // as a screen distance and converted, so a handle is the same size under
    // the pointer however far the view is zoomed in or out.
    double grabReachInPixels(const QPointF &position) const;

    bool m_roiDrawing = false;
    bool m_drawingHole = false;
    bool m_roiActorAdded = false;
    QVector<QPoint> m_roiPlaced;   // corners placed so far, while drawing
    QPoint m_roiCursor;            // where the rubber band currently reaches
    bool m_roiCursorValid = false;
    RegionOfInterest m_roiShown;   // the committed boundary on display

    bool m_gaugePlacing = false;
    bool m_gaugeActorAdded = false;
    QVector<QPoint> m_gaugeAnchors;   // anchors placed so far, while placing
    QPoint m_gaugeCursor;
    bool m_gaugeCursorValid = false;
    QVector<Extensometer> m_gaugesShown;
};
