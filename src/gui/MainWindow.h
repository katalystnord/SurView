#pragma once

#include <QMainWindow>

class QAction;
class QLabel;
class QPlainTextEdit;
class QTreeWidgetItem;
class ImageViewport;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void importReferenceImage();
    void importTargetImages();
    void runCorrelation();
    void showAbout();

private:
    void createActions();
    void createMenus();
    void createToolBar();
    void createDockPanels();
    void createStatusBar();

    QWidget *createProjectPanel();
    QWidget *createAnalysisPanel();
    QWidget *createLogPanel();

    void log(const QString &message);
    void notImplemented(const QString &feature);
    void updateActionStates();

    ImageViewport *m_viewport = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QLabel *m_stageLabel = nullptr;

    QTreeWidgetItem *m_referenceItem = nullptr;
    QTreeWidgetItem *m_targetsItem = nullptr;

    // Actions whose enabled state depends on project progress.
    QAction *m_actRun = nullptr;
    QAction *m_actStop = nullptr;
    QAction *m_actDefineRoi = nullptr;
    QAction *m_actAutoRoi = nullptr;
    QAction *m_actExport = nullptr;
};
