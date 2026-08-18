#pragma once

#include "core/Correlation.h"
#include "core/ImageRecord.h"
#include "core/Roi.h"

#include <QMainWindow>
#include <QVector>

class QAction;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QSpinBox;
class QThread;
class QTreeWidget;
class QTreeWidgetItem;
class ImageViewport;
class RecordPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Import images by path -- the same route the File menu takes, exposed so
    // they can also be named on the command line.
    void openReferenceImage(const QString &path);
    void addTargetImages(const QStringList &paths);

    // The most recent measurement, and the region in force. Exposed so the
    // walkthrough tests can assert on what a run actually produced rather than
    // on what the screen appears to show; nothing in the application reads
    // these.
    const CorrelationResult &lastResult() const { return m_result; }
    const RegionOfInterest &roi() const { return m_roi; }

private slots:
    void importReferenceImage();
    void importTargetImages();
    void runCorrelation();
    void showAbout();

    // Selecting an image in the project tree shows its record, and displays it
    // in the viewport. Without this a target's record would exist but be
    // unreachable from the interface, which is the same as not having it.
    void showSelectedImage();

    // A boundary was completed in the viewport, or discarded from the project.
    void onRoiDrawn(const RegionOfInterest &roi);
    void detectRoi();
    void clearRoi();

    void stopCorrelation();
    void onCorrelationProgress(int done, int total, const QString &stage);
    void onCorrelationFinished(const CorrelationResult &result);
    void onCorrelationFailed(const QString &reason);

    // Keeps the panel honest about what the engine actually offers: the
    // second-order shape function is disabled while Newton–Raphson is chosen,
    // because the engine implements that solver for first order only.
    void updateSolverConstraints();

private:
    void createActions();
    void createMenus();
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
    CorrelationSettings currentSettings() const;

    // First target that can actually be correlated against the reference, or
    // -1 if there is none.
    int firstUsableTarget() const;

    // Restate the region in the project tree and the log. One place, so a
    // region set by hand and one proposed by the detector are reported in the
    // same words.
    void showRoiInProject();

    // Drop a measured field once the region it was measured over stops being
    // the region in force.
    void discardStaleResult();

    ImageViewport *m_viewport = nullptr;
    RecordPanel *m_record = nullptr;
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

    QProgressBar *m_progress = nullptr;

    QThread *m_workerThread = nullptr;
    CorrelationRunner *m_runner = nullptr;

    CorrelationResult m_result;
    bool m_hasResult = false;

    // Actions whose enabled state depends on project progress.
    QAction *m_actRun = nullptr;
    QAction *m_actStop = nullptr;
    QAction *m_actDefineRoi = nullptr;
    QAction *m_actAutoRoi = nullptr;
    QAction *m_actClearRoi = nullptr;
    QAction *m_actExport = nullptr;
};
