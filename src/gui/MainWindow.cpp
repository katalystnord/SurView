#include "MainWindow.h"

#include "ImageViewport.h"

#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QTime>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("SurView DIC"));
    resize(1360, 860);

    m_viewport = new ImageViewport(this);
    setCentralWidget(m_viewport);

    createActions();
    createMenus();
    createToolBar();
    createDockPanels();
    createStatusBar();

    updateActionStates();
    log(tr("SurView DIC ready. Import a reference image to begin."));
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void MainWindow::createActions()
{
    // Placeholder actions (project I/O, ROI, export) report that they are not
    // yet wired rather than silently doing nothing — an honest scaffold.
    m_actDefineRoi = new QAction(tr("Define ROI"), this);
    m_actDefineRoi->setStatusTip(tr("Draw the region of interest to correlate"));
    connect(m_actDefineRoi, &QAction::triggered, this,
            [this] { notImplemented(tr("Manual ROI drawing")); });

    m_actAutoRoi = new QAction(tr("Auto-detect ROI"), this);
    m_actAutoRoi->setStatusTip(
        tr("Segment the speckle region automatically (AutoROI)"));
    connect(m_actAutoRoi, &QAction::triggered, this,
            [this] { notImplemented(tr("Automatic ROI detection")); });

    m_actRun = new QAction(
        style()->standardIcon(QStyle::SP_MediaPlay), tr("Run Correlation"), this);
    connect(m_actRun, &QAction::triggered, this, &MainWindow::runCorrelation);

    m_actStop = new QAction(
        style()->standardIcon(QStyle::SP_MediaStop), tr("Stop"), this);
    m_actStop->setEnabled(false);
    connect(m_actStop, &QAction::triggered, this,
            [this] { notImplemented(tr("Stopping a correlation run")); });

    m_actExport = new QAction(tr("Export Results (.vtu)…"), this);
    m_actExport->setStatusTip(
        tr("Write displacement/strain fields as VTK data for ParaView / FreeCAD"));
    connect(m_actExport, &QAction::triggered, this,
            [this] { notImplemented(tr("VTK .vtu export")); });
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

    QAction *newProject = fileMenu->addAction(tr("&New Project"));
    connect(newProject, &QAction::triggered, this,
            [this] { notImplemented(tr("Projects")); });
    QAction *openProject = fileMenu->addAction(tr("&Open Project…"));
    connect(openProject, &QAction::triggered, this,
            [this] { notImplemented(tr("Projects")); });
    QAction *saveProject = fileMenu->addAction(tr("&Save Project"));
    connect(saveProject, &QAction::triggered, this,
            [this] { notImplemented(tr("Projects")); });

    fileMenu->addSeparator();

    QAction *importRef = fileMenu->addAction(
        style()->standardIcon(QStyle::SP_FileDialogStart),
        tr("Import &Reference Image…"));
    connect(importRef, &QAction::triggered, this,
            &MainWindow::importReferenceImage);
    QAction *importTgt =
        fileMenu->addAction(tr("Import &Target Image(s)…"));
    connect(importTgt, &QAction::triggered, this,
            &MainWindow::importTargetImages);

    fileMenu->addSeparator();
    fileMenu->addAction(m_actExport);
    fileMenu->addSeparator();

    QAction *quit = fileMenu->addAction(tr("&Quit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    // View menu (dock toggles are appended in createDockPanels()).
    menuBar()->addMenu(tr("&View"));

    // Analysis — the DIC core pipeline.
    QMenu *analysisMenu = menuBar()->addMenu(tr("&Analysis"));
    analysisMenu->addAction(m_actDefineRoi);
    analysisMenu->addAction(m_actAutoRoi);
    analysisMenu->addSeparator();
    QAction *settings = analysisMenu->addAction(tr("Correlation Settings…"));
    connect(settings, &QAction::triggered, this,
            [this] { notImplemented(tr("Correlation settings dialog")); });
    analysisMenu->addSeparator();
    analysisMenu->addAction(m_actRun);
    analysisMenu->addAction(m_actStop);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction *about = helpMenu->addAction(tr("&About SurView DIC"));
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::createToolBar()
{
    QToolBar *toolbar = addToolBar(tr("Pipeline"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    QAction *importRef = toolbar->addAction(
        style()->standardIcon(QStyle::SP_FileDialogStart), tr("Reference"));
    importRef->setToolTip(tr("Import reference (undeformed) image"));
    connect(importRef, &QAction::triggered, this,
            &MainWindow::importReferenceImage);

    QAction *importTgt = toolbar->addAction(
        style()->standardIcon(QStyle::SP_FileIcon), tr("Target"));
    importTgt->setToolTip(tr("Import target (deformed) image(s)"));
    connect(importTgt, &QAction::triggered, this,
            &MainWindow::importTargetImages);

    toolbar->addSeparator();
    toolbar->addAction(m_actDefineRoi);
    toolbar->addAction(m_actAutoRoi);
    toolbar->addSeparator();
    toolbar->addAction(m_actRun);
    toolbar->addAction(m_actStop);
}

// ---------------------------------------------------------------------------
// Dock panels — laid out along the DIC pipeline (project ▸ parameters ▸ log)
// ---------------------------------------------------------------------------

void MainWindow::createDockPanels()
{
    QMenu *viewMenu = nullptr;
    for (QAction *action : menuBar()->actions()) {
        if (action->text() == tr("&View"))
            viewMenu = action->menu();
    }

    auto addDock = [&](const QString &title, QWidget *content,
                       Qt::DockWidgetArea area) {
        auto *dock = new QDockWidget(title, this);
        dock->setWidget(content);
        addDockWidget(area, dock);
        if (viewMenu)
            viewMenu->addAction(dock->toggleViewAction());
    };

    addDock(tr("Project"), createProjectPanel(), Qt::LeftDockWidgetArea);
    addDock(tr("Analysis"), createAnalysisPanel(), Qt::RightDockWidgetArea);
    addDock(tr("Log"), createLogPanel(), Qt::BottomDockWidgetArea);
}

QWidget *MainWindow::createProjectPanel()
{
    auto *tree = new QTreeWidget;
    tree->setHeaderHidden(true);

    m_referenceItem = new QTreeWidgetItem(tree);
    m_referenceItem->setText(0, tr("Reference image — none"));

    m_targetsItem = new QTreeWidgetItem(tree);
    m_targetsItem->setText(0, tr("Target images — none"));

    auto *rois = new QTreeWidgetItem(tree);
    rois->setText(0, tr("Regions of interest — none"));

    auto *results = new QTreeWidgetItem(tree);
    results->setText(0, tr("Results — none"));

    tree->expandAll();
    return tree;
}

QWidget *MainWindow::createAnalysisPanel()
{
    // Real OpenCorr correlation parameters, so the panel reads as the actual
    // control surface even before it drives the engine.
    auto *panel = new QWidget;
    auto *form = new QFormLayout(panel);

    auto *solver = new QComboBox;
    solver->addItems({tr("ICGN"), tr("Newton–Raphson"), tr("IC-LM")});
    form->addRow(tr("Solver"), solver);

    auto *shape = new QComboBox;
    shape->addItems({tr("First order (affine)"), tr("Second order (quadratic)")});
    form->addRow(tr("Shape function"), shape);

    auto *subset = new QSpinBox;
    subset->setRange(3, 100);
    subset->setValue(16);
    subset->setSuffix(tr(" px"));
    form->addRow(tr("Subset radius"), subset);

    auto *step = new QSpinBox;
    step->setRange(1, 100);
    step->setValue(5);
    step->setSuffix(tr(" px"));
    form->addRow(tr("Grid step"), step);

    auto *iterations = new QSpinBox;
    iterations->setRange(1, 100);
    iterations->setValue(10);
    form->addRow(tr("Max iterations"), iterations);

    auto *convergence = new QDoubleSpinBox;
    convergence->setDecimals(4);
    convergence->setRange(0.0001, 1.0);
    convergence->setSingleStep(0.0001);
    convergence->setValue(0.001);
    convergence->setPrefix(tr("‖Δp‖ ≤ "));
    form->addRow(tr("Convergence"), convergence);

    auto *interpolation = new QComboBox;
    interpolation->addItem(tr("Bicubic B-spline"));
    form->addRow(tr("Interpolation"), interpolation);

    return panel;
}

QWidget *MainWindow::createLogPanel()
{
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(1000);
    return m_log;
}

void MainWindow::createStatusBar()
{
    m_stageLabel = new QLabel(tr("No project"));
    statusBar()->addPermanentWidget(m_stageLabel);
    statusBar()->showMessage(tr("Ready"));
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void MainWindow::importReferenceImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Reference Image"), QString(),
        tr("Images (*.tif *.tiff *.png *.jpg *.jpeg *.bmp);;All files (*)"));
    if (path.isEmpty())
        return;

    const QString name = QFileInfo(path).fileName();
    if (!m_viewport->loadImage(path)) {
        QMessageBox::warning(
            this, tr("Import failed"),
            tr("Could not read the image:\n%1").arg(path));
        log(tr("Failed to load reference image: %1").arg(name));
        return;
    }

    const QSize size = m_viewport->imageSize();
    m_referenceItem->setText(
        0, tr("Reference image — %1 (%2×%3)").arg(name).arg(size.width()).arg(size.height()));
    m_stageLabel->setText(tr("Reference loaded"));
    statusBar()->showMessage(tr("Loaded %1").arg(name), 4000);
    log(tr("Loaded reference image: %1 (%2×%3 px)")
            .arg(name).arg(size.width()).arg(size.height()));
    updateActionStates();
}

void MainWindow::importTargetImages()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Import Target Image(s)"), QString(),
        tr("Images (*.tif *.tiff *.png *.jpg *.jpeg *.bmp);;All files (*)"));
    if (paths.isEmpty())
        return;

    m_targetsItem->setText(0, tr("Target images — %n file(s)", nullptr, paths.size()));
    for (const QString &path : paths)
        log(tr("Added target image: %1").arg(QFileInfo(path).fileName()));
    statusBar()->showMessage(
        tr("Added %n target image(s)", nullptr, paths.size()), 4000);
    updateActionStates();
}

void MainWindow::runCorrelation()
{
    // The engine is not wired yet; be explicit rather than appearing to run.
    log(tr("Correlation requested — OpenCorr engine not yet connected."));
    notImplemented(tr("Running the correlation"));
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("About SurView DIC"),
        tr("<b>SurView DIC</b><br>"
           "A cross-platform GUI for Digital Image Correlation, wrapping the "
           "OpenCorr engine and built on Qt + VTK.<br><br>"
           "LGPL-2.1-or-later."));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void MainWindow::log(const QString &message)
{
    m_log->appendPlainText(
        QStringLiteral("[%1] %2")
            .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), message));
}

void MainWindow::notImplemented(const QString &feature)
{
    statusBar()->showMessage(tr("%1 — not yet implemented").arg(feature), 4000);
}

void MainWindow::updateActionStates()
{
    const bool hasImage = m_viewport->hasImage();

    m_actDefineRoi->setEnabled(hasImage);
    m_actAutoRoi->setEnabled(hasImage);

    // Run needs at least a reference image; keep the precondition visible via
    // the tooltip rather than letting the button fail silently.
    m_actRun->setEnabled(hasImage);
    m_actRun->setToolTip(hasImage
                             ? tr("Run DIC correlation")
                             : tr("Import a reference image first"));

    // Export needs results, which do not exist yet.
    m_actExport->setEnabled(false);
}
