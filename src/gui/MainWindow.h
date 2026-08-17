#pragma once

#include "core/ImageRecord.h"

#include <QMainWindow>
#include <QVector>

class QAction;
class QLabel;
class QPlainTextEdit;
class QTreeWidget;
class QTreeWidgetItem;
class ImageViewport;
class RecordPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Import images by path — the same route the File menu takes, exposed so
    // they can also be named on the command line.
    void openReferenceImage(const QString &path);
    void addTargetImages(const QStringList &paths);

private slots:
    void importReferenceImage();
    void importTargetImages();
    void runCorrelation();
    void showAbout();

    // Selecting an image in the project tree shows its record, and displays it
    // in the viewport. Without this a target's record would exist but be
    // unreachable from the interface, which is the same as not having it.
    void showSelectedImage();

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

    ImageViewport *m_viewport = nullptr;
    RecordPanel *m_record = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QLabel *m_stageLabel = nullptr;

    QTreeWidget *m_projectTree = nullptr;
    QTreeWidgetItem *m_referenceItem = nullptr;
    QTreeWidgetItem *m_targetsItem = nullptr;

    // The records themselves, held here rather than in the viewport: an image
    // is recorded when it is imported, whether or not it is ever displayed.
    ImageRecord m_referenceRecord;
    QVector<ImageRecord> m_targetRecords;

    // Actions whose enabled state depends on project progress.
    QAction *m_actRun = nullptr;
    QAction *m_actStop = nullptr;
    QAction *m_actDefineRoi = nullptr;
    QAction *m_actAutoRoi = nullptr;
    QAction *m_actExport = nullptr;
};
