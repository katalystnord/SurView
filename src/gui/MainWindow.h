#pragma once

#include "core/Correlation.h"
#include "core/FieldExport.h"
#include "core/Project.h"
#include "core/Series.h"
#include "core/SpeckleQuality.h"
#include "core/ReferenceUpdate.h"
#include "core/SequenceRunner.h"
#include "core/ImageRecord.h"
#include "core/Roi.h"

#include <QMainWindow>
#include <QVector>

class QAction;
class QMenu;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QSpinBox;
class QThread;
class QTreeWidget;
class QTreeWidgetItem;
class ImageViewport;
class PlotPanel;
class PointPanel;
class RecordPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Import images by path -- the same route the File menu takes, exposed so
    // they can also be named on the command line.
    void openReferenceImage(const QString &path);
    void addTargetImages(const QStringList &paths);

    // The most recent measurement, and the region in force. Exposed so the
    // walkthrough tests can assert on what a run actually produced rather than
    // on what the screen appears to show; nothing in the application reads
    // these.
    // The frame currently on screen, and the sequence behind it. Exposed so the
    // walkthrough tests can assert on what a run actually produced rather than
    // on what the screen appears to show; nothing in the application reads
    // these.
    const CorrelationResult &displayedResult() const { return m_result; }
    const CorrelationResult &lastResult() const { return m_result; }
    int measuredFrames() const { return int(m_frames.size()); }
    const CorrelationResult &frameResult(int frame) const;

    const RegionOfInterest &roi() const { return m_roi; }

    // Write the measured field to `path`. The same route the File menu takes,
    // which asks for the path and then calls this -- exposed for the same
    // reason openReferenceImage() is: everything except choosing the file
    // should be drivable without a modal dialog in the way. Reports failure
    // to the user itself; the return value is for a caller that wants to know.
    bool exportFieldTo(const QString &path);

    // The same field as a plain table. Separate entry point rather than a
    // format argument on the one above, so a caller cannot pick the wrong
    // format by passing the wrong enumerator, and so each has a name that says
    // which file it writes.
    bool exportFieldCsvTo(const QString &path);

    // Save and open a session by path. The same route the File menu takes,
    // exposed for the reason exportFieldTo() is: everything except choosing the
    // file should be drivable without a modal dialog in the way.
    bool saveProjectTo(const QString &path);
    bool openProjectFrom(const QString &path);

private slots:
    void importReferenceImage();
    void openExample();
    void newProject();
    void openProject();
    void saveProjectAs();
    void importTargetImages();
    void runCorrelation();
    void exportField();
    void exportFieldCsv();
    void showAbout();

    // Selecting an image in the project tree shows its record, and displays it
    // in the viewport. Without this a target's record would exist but be
    // unreachable from the interface, which is the same as not having it.
    void showSelectedImage();

    // Reading one point of the measured field. Hover follows the pointer;
    // a click pins, so a reading can be looked at and written down rather than
    // erased by looking away.
    void onFieldPointHovered(const QPointF &imagePixel, bool insideImage);
    void onFieldPointPicked(const QPointF &imagePixel);

    // A boundary was completed in the viewport, or discarded from the project.
    void onRoiDrawn(const RegionOfInterest &roi);
    void detectRoi();
    void onHoleDrawn(const QVector<QPoint> &ring);
    void clearRoi();

    void stopCorrelation();
    void onFrameProgress(int frame, int frameCount, int done, int total,
                         const QString &stage);
    void onFrameFinished(int frame, const CorrelationResult &result);
    void onSequenceFinished(int framesMeasured, bool cancelled);
    void onCorrelationFailed(const QString &reason);

    // Keeps the panel honest about what the engine actually offers: the
    // second-order shape function is disabled while Newton-Raphson is chosen,
    // because the engine implements that solver for first order only.
    void updateSolverConstraints();

    // Re-estimate what the speckle in the region can resolve, at the subset
    // radius currently chosen. Live, because both of the things it depends on
    // are being chosen right there and an answer that waited for the run would
    // arrive after the decision it is about.
    void updateSpeckleQuality();

    // Keeps the strain warning on screen honest about the numbers currently in
    // the panel. Live, not on Run: the settings that make the fit impossible
    // are chosen here, and a warning that waited for the run would arrive after
    // the decision it is about.
    void updateStrainAdvice();

    // The re-anchor rule's own numbers mean nothing while it is off, so they
    // follow the switch that turns it on.
    void updateReferenceUpdateControls();
    void updateRecoveryControls();

    // A gauge was placed on the image, or the plotted curve is to be written
    // out. Both are the window's business rather than the viewport's or the
    // plot's, for the same reason every other export is.
    void onExtensometerPlaced(double ax, double ay, double bx, double by);
    void exportPlotData();
    void clearExtensometers();

private:
    void createActions();
    void createMenus();

    // Fill the Open Example submenu from whatever is on disk. Built once, at
    // start-up, because the examples ship with the application and do not
    // change under it.
    void buildExampleMenu(QMenu *menu);
    void createToolBar();
    void createDockPanels();
    void createStatusBar();

    QWidget *createProjectPanel();
    QWidget *createRecordPanel();
    QWidget *createAnalysisPanel();
    QWidget *createLogPanel();

    void log(const QString &message);
    void notImplemented(const QString &feature);
    void updateActionStates();

    // Show `record` in the record panel, and its pixels in the viewport.
    void displayRecord(const ImageRecord &record);

    // Reads the Analysis panel. One place, so the run and the panel cannot
    // drift apart.
    void updatePlot();

    CorrelationSettings currentSettings() const;

    // Put a loaded project's settings back into the panel. The inverse of
    // currentSettings(), and deliberately beside it: a field added to one and
    // forgotten in the other is a session that opens looking right and measures
    // something else.
    void applySettings(const CorrelationSettings &settings,
                       const ReferenceUpdatePolicy &policy);

    // Everything the current session is, ready to be written.
    Project currentProject() const;

    // Reads the reference-update controls. One place, as with currentSettings().
    ReferenceUpdatePolicy currentReferencePolicy() const;

    // Restate the region in the project tree and the log. One place, so a
    // region set by hand and one proposed by the detector are reported in the
    // same words.
    void showRoiInProject();

    // Drop a measured field once the region it was measured over stops being
    // the region in force.
    void discardStaleResult();

    // Which file format an export writes. The per-frame loop, the numbering,
    // the stop-at-first-failure rule and the reporting are identical for both;
    // only the extension and the writer differ, and duplicating the rest is how
    // the two would drift apart.
    enum class FieldFormat { Vtu, Csv };
    bool exportFrames(const QString &path, FieldFormat format);

    // Everything one measured frame has to say about itself, in the log.
    void logFrameResult(int frame, const CorrelationResult &result);

    // Shut the worker thread down. One place, because it happens on three
    // different endings and a missed teardown leaks a thread per run.
    void tearDownWorker();

    // Show one point of the displayed field, or the standing invitation when
    // there is nothing to show. One place, so hovering, pinning and a new
    // frame arriving cannot describe the same state in different words.
    void showPoint(int index);

    // Put one measured frame on screen, and say in the project which one.
    void displayFrame(int frame);

    // Restate the target list in frame order and rebuild its tree entries. A
    // sequence is a time axis, so the order shown has to be the order measured.
    void rebuildTargetList(QVector<QTreeWidgetItem *> pending);

    ImageViewport *m_viewport = nullptr;
    RecordPanel *m_record = nullptr;
    PointPanel *m_point = nullptr;

    // The point held on screen, as an index into the displayed result, or -1
    // when the readout is following the pointer. Dropped whenever the field
    // under it changes: an index into a result that is no longer displayed
    // would read out a different point of a different frame while looking
    // exactly as authoritative.
    int m_pinnedPoint = -1;
    QPlainTextEdit *m_log = nullptr;
    QLabel *m_stageLabel = nullptr;

    QTreeWidget *m_projectTree = nullptr;
    QTreeWidgetItem *m_referenceItem = nullptr;
    QTreeWidgetItem *m_targetsItem = nullptr;
    QTreeWidgetItem *m_roiItem = nullptr;
    QTreeWidgetItem *m_resultsItem = nullptr;

    // The records themselves, held here rather than in the viewport: an image
    // is recorded when it is imported, whether or not it is ever displayed.
    ImageRecord m_referenceRecord;
    QVector<ImageRecord> m_targetRecords;

    // The region the next run will measure inside. Held here rather than in the
    // viewport: it is part of the project, and the viewport only draws it.
    RegionOfInterest m_roi;

    // Analysis panel -- held so the run can read them. Controls nothing reads
    // are controls that do nothing.
    QComboBox *m_solver = nullptr;
    QComboBox *m_shape = nullptr;
    QSpinBox *m_subsetRadius = nullptr;
    QSpinBox *m_gridStep = nullptr;
    QSpinBox *m_maxIterations = nullptr;
    QDoubleSpinBox *m_convergence = nullptr;

    QCheckBox *m_strainEnabled = nullptr;
    QDoubleSpinBox *m_strainRadius = nullptr;
    QSpinBox *m_strainMinPoints = nullptr;
    QComboBox *m_strainMeasure = nullptr;
    QLabel *m_strainAdvice = nullptr;
    QLabel *m_speckleAdvice = nullptr;
    QGroupBox *m_strainGroup = nullptr;

    QCheckBox *m_reanchorEnabled = nullptr;
    QDoubleSpinBox *m_reanchorThreshold = nullptr;
    QSpinBox *m_reanchorShare = nullptr;

    QCheckBox *m_recoveryEnabled = nullptr;
    QDoubleSpinBox *m_recoveryRetryBelow = nullptr;
    QDoubleSpinBox *m_recoveryReliable = nullptr;
    QSpinBox *m_recoveryRounds = nullptr;
    QLabel *m_recoveryNeighbourhood = nullptr;

    QProgressBar *m_progress = nullptr;

    QThread *m_workerThread = nullptr;
    SequenceRunner *m_runner = nullptr;

    // One measured frame, with the account of how it was produced captured at
    // the moment the run started. Kept together so an export cannot pair a
    // frame with another frame's provenance.
    struct MeasuredFrame
    {
        CorrelationResult result;
        FieldProvenance provenance;
    };
    // Frames that have actually been MEASURED. Not pre-sized to the planned
    // count: a size that means "planned" reads identically to one that means
    // "done", and anything asking how far a run has got would be told the
    // answer before it started.
    QVector<MeasuredFrame> m_frames;

    // The gauges placed on the reference image. Owned here, not by the viewport
    // that draws them or the panel that plots them, because they are part of
    // the session.
    QVector<Extensometer> m_gauges;
    PlotPanel *m_plot = nullptr;

    // How each planned frame will be attributed, captured when the run starts
    // and paired with its result as that arrives.
    QVector<FieldProvenance> m_plannedFrames;

    int m_displayedFrame = -1;

    // Where this session was last saved or opened from, so Save can write back
    // without asking again.
    QString m_projectPath;

    // The frame on screen, held separately so everything that already reads it
    // keeps working whether the run measured one target or twelve.
    CorrelationResult m_result;
    bool m_hasResult = false;

    // How m_result was produced, captured when the run STARTS rather than read
    // back from the panel at export time. The panel keeps taking input after a
    // run finishes, so reading it later would write a file stating, with a
    // SHA-256 beside it, a configuration that never measured anything.


    // Actions whose enabled state depends on project progress.
    QAction *m_actRun = nullptr;
    QAction *m_actStop = nullptr;
    QAction *m_actAddHole = nullptr;
    QAction *m_actExtensometer = nullptr;
    QAction *m_actDefineRoi = nullptr;
    QAction *m_actAutoRoi = nullptr;
    QAction *m_actClearRoi = nullptr;
    QAction *m_actExport = nullptr;
    QAction *m_actExportCsv = nullptr;
};
