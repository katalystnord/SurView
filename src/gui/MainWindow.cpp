#include "MainWindow.h"

#include "ImageViewport.h"
#include "PointPanel.h"
#include "RecordPanel.h"
#include "core/Correlation.h"
#include "core/FieldExport.h"
#include "core/FieldLayout.h"
#include "core/ImageDecode.h"
#include "core/Examples.h"
#include "core/ImagePairing.h"
#include "core/PointReadout.h"
#include "core/RoiDetect.h"
#include "core/Sequence.h"
#include "core/StrainFit.h"

#include <QApplication>
#include <QCoreApplication>
#include <QMenu>
#include <QEventLoop>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QGuiApplication>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QThread>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QTime>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

// Which record a project-tree item stands for, and which one of them, so a
// selection can be resolved back to the record it names.
constexpr int kRecordKindRole  = Qt::UserRole;
constexpr int kRecordIndexRole = Qt::UserRole + 1;

enum RecordKind { None = 0, Reference = 1, Target = 2, Frame = 3 };

}  // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("SurView DIC"));
    resize(1360, 860);

    m_viewport = new ImageViewport(this);
    setCentralWidget(m_viewport);

    connect(m_viewport, &ImageViewport::roiDrawn, this, &MainWindow::onRoiDrawn);
    connect(m_viewport, &ImageViewport::importReferenceRequested, this,
            &MainWindow::importReferenceImage);
    connect(m_viewport, &ImageViewport::fieldPointHovered, this,
            &MainWindow::onFieldPointHovered);
    connect(m_viewport, &ImageViewport::fieldPointPicked, this,
            &MainWindow::onFieldPointPicked);
    // While a boundary is being placed the pipeline controls would compete with
    // the mode's own bar for the same decision, so they follow it.
    connect(m_viewport, &ImageViewport::roiDrawingChanged, this,
            [this](bool) { updateActionStates(); });

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

MainWindow::~MainWindow()
{
    // ⚑ A run outlives the window otherwise. Qt destroys the QThread with its
    // parent while the worker is still inside a solve, which aborts the process
    // with "QThread: Destroyed while thread is still running" -- so closing the
    // application mid-correlation crashed it. Found by a walkthrough test that
    // failed early and left a sequence running.
    if (m_runner)
        m_runner->cancel();
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
    }
}

void MainWindow::createActions()
{
    // Placeholder actions (project I/O, export) report that they are not yet
    // wired rather than silently doing nothing -- an honest scaffold.
    // ⚑ An icon beside every name, never instead of one. Half the toolbar had
    // pictures and half was bare words, which reads as two toolbars and gives
    // the eye nothing to aim at; an icon ALONE would be worse, because it is a
    // thing you have to already know.
    m_actDefineRoi = new QAction(
        style()->standardIcon(QStyle::SP_FileDialogDetailedView), tr("Define ROI"), this);
    m_actDefineRoi->setStatusTip(
        tr("Draw the region of interest by clicking corners on the image"));
    connect(m_actDefineRoi, &QAction::triggered, this,
            [this] { m_viewport->beginRoiDrawing(); });

    m_actAutoRoi = new QAction(
        style()->standardIcon(QStyle::SP_FileDialogContentsView), tr("Auto-detect ROI"), this);
    m_actAutoRoi->setStatusTip(
        tr("Propose a region by segmenting the speckled area of the image"));
    connect(m_actAutoRoi, &QAction::triggered, this, &MainWindow::detectRoi);

    m_actClearRoi = new QAction(
        style()->standardIcon(QStyle::SP_DialogResetButton), tr("Clear ROI"), this);
    m_actClearRoi->setStatusTip(
        tr("Discard the region, so the next run measures the whole image"));
    connect(m_actClearRoi, &QAction::triggered, this, &MainWindow::clearRoi);

    m_actRun = new QAction(
        style()->standardIcon(QStyle::SP_MediaPlay), tr("Run Correlation"), this);
    connect(m_actRun, &QAction::triggered, this, &MainWindow::runCorrelation);

    m_actStop = new QAction(
        style()->standardIcon(QStyle::SP_MediaStop), tr("Stop"), this);
    m_actStop->setEnabled(false);
    connect(m_actStop, &QAction::triggered, this, &MainWindow::stopCorrelation);

    m_actExport = new QAction(
        style()->standardIcon(QStyle::SP_DialogSaveButton),
        tr("Export Results (.vtu)…"), this);
    m_actExport->setStatusTip(
        tr("Write displacement/strain fields as VTK data for ParaView / FreeCAD"));
    connect(m_actExport, &QAction::triggered, this, &MainWindow::exportField);

    m_actExportCsv = new QAction(
        style()->standardIcon(QStyle::SP_FileIcon),
        tr("Export Results as Table (.csv)…"), this);
    m_actExportCsv->setStatusTip(
        tr("Write the same fields as a plain table any spreadsheet opens"));
    connect(m_actExportCsv, &QAction::triggered, this, &MainWindow::exportFieldCsv);
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

    QAction *actNew = fileMenu->addAction(tr("&New Project"));
    connect(actNew, &QAction::triggered, this, &MainWindow::newProject);
    QAction *actOpen = fileMenu->addAction(tr("&Open Project…"));
    connect(actOpen, &QAction::triggered, this, &MainWindow::openProject);
    QAction *actSave = fileMenu->addAction(tr("&Save Project…"));
    connect(actSave, &QAction::triggered, this, &MainWindow::saveProjectAs);

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
    // ⚑ Before the Import entries, not after the exports: opening an example is
    // the fastest way to have something on screen, and it is the entry a reader
    // with no speckle images of their own needs first.
    QMenu *examples = fileMenu->addMenu(tr("Open Example"));
    buildExampleMenu(examples);
    fileMenu->addSeparator();

    fileMenu->addAction(m_actExport);
    fileMenu->addAction(m_actExportCsv);
    fileMenu->addSeparator();

    QAction *quit = fileMenu->addAction(tr("&Quit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    // View menu (dock toggles are appended in createDockPanels()).
    menuBar()->addMenu(tr("&View"));

    // Analysis -- the DIC core pipeline.
    QMenu *analysisMenu = menuBar()->addMenu(tr("&Analysis"));
    analysisMenu->addAction(m_actDefineRoi);
    analysisMenu->addAction(m_actAutoRoi);
    analysisMenu->addAction(m_actClearRoi);
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
    toolbar->addAction(m_actClearRoi);
    toolbar->addSeparator();
    toolbar->addAction(m_actRun);
    toolbar->addAction(m_actStop);
}

// ---------------------------------------------------------------------------
// Dock panels -- laid out along the DIC pipeline (project ▸ parameters ▸ log)
// ---------------------------------------------------------------------------

void MainWindow::buildExampleMenu(QMenu *menu)
{
    const QVector<ExampleSet> sets =
        findExamples(exampleSearchPaths(QCoreApplication::applicationDirPath()));

    if (sets.isEmpty()) {
        // Said rather than left empty: an empty submenu reads as a broken
        // feature, where a disabled line naming what is missing reads as a
        // build without its example data.
        QAction *none = menu->addAction(tr("No example data found beside this build"));
        none->setEnabled(false);
        return;
    }

    // Headed by family, because two sets can carry the same name: the real
    // rotation plate and the synthetic rotation both do, and side by side as
    // two identical words they are a choice nobody can make. The difference is
    // the one that matters most here, so it is the heading.
    QString shownGroup;
    for (const ExampleSet &set : sets) {
        if (set.group != shownGroup) {
            shownGroup = set.group;
            if (menu->actions().size() > 0)
                menu->addSeparator();
            QAction *heading = menu->addAction(
                set.group.compare(QStringLiteral("Synthetic"), Qt::CaseInsensitive) == 0
                    ? tr("Synthetic, with an exactly known answer")
                    : tr("Real experiments"));
            heading->setEnabled(false);
        }

        QAction *action = menu->addAction(set.name);
        action->setToolTip(set.summary);
        action->setStatusTip(set.summary);
        action->setData(set.frames);
        connect(action, &QAction::triggered, this, &MainWindow::openExample);
    }
    // Tooltips on menu items are off by default in Qt, and the frame count is
    // the thing that says whether this is a pair or a sequence.
    menu->setToolTipsVisible(true);
}

Project MainWindow::currentProject() const
{
    Project project;
    project.referencePath = m_referenceRecord.filePath;
    for (const ImageRecord &target : m_targetRecords)
        project.targetPaths << target.filePath;
    project.roi = m_roi;
    project.settings = currentSettings();
    project.referenceUpdate = currentReferencePolicy();
    return project;
}

void MainWindow::applySettings(const CorrelationSettings &settings,
                               const ReferenceUpdatePolicy &policy)
{
    for (int i = 0; i < m_solver->count(); i++) {
        if (m_solver->itemData(i).toInt() == int(settings.solver))
            m_solver->setCurrentIndex(i);
    }
    for (int i = 0; i < m_shape->count(); i++) {
        if (m_shape->itemData(i).toInt() == settings.shapeOrder)
            m_shape->setCurrentIndex(i);
    }
    m_subsetRadius->setValue(settings.subsetRadius);
    m_gridStep->setValue(settings.gridStep);
    m_maxIterations->setValue(settings.maxIterations);
    m_convergence->setValue(settings.convergence);

    m_strainEnabled->setChecked(settings.strainEnabled);
    m_strainRadius->setValue(settings.strainRadius);
    m_strainMinPoints->setValue(settings.strainMinPoints);
    for (int i = 0; i < m_strainMeasure->count(); i++) {
        if (m_strainMeasure->itemData(i).toInt() == int(settings.strainMeasure))
            m_strainMeasure->setCurrentIndex(i);
    }

    m_reanchorEnabled->setChecked(policy.enabled);
    m_reanchorThreshold->setValue(policy.znccThreshold);
    m_reanchorShare->setValue(int(qRound(policy.percentile * 100.0)));
}

void MainWindow::newProject()
{
    m_projectPath.clear();
    m_referenceRecord = ImageRecord();
    m_targetRecords.clear();
    m_roi = RegionOfInterest();
    m_frames.clear();
    m_plannedFrames.clear();
    m_displayedFrame = -1;
    m_result = CorrelationResult();
    m_hasResult = false;
    m_pinnedPoint = -1;

    m_viewport->clearField();
    m_viewport->clearRoi();
    m_viewport->showMessage(tr("No image loaded"));
    m_record->clear();
    m_point->clear();

    m_referenceItem->setText(0, tr("Reference image - none"));
    m_targetsItem->setText(0, tr("Target images - none"));
    m_targetsItem->takeChildren();
    m_resultsItem->setText(0, tr("Results - none"));
    m_resultsItem->takeChildren();
    showRoiInProject();
    updateActionStates();
    log(tr("New project."));
}

void MainWindow::openProject()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open project"), QString(),
        tr("SurView project (*.svproj)"));
    if (path.isEmpty())
        return;

    openProjectFrom(path);
}

bool MainWindow::openProjectFrom(const QString &path)
{
    const ProjectLoad loaded = loadProject(path);
    if (!loaded.ok()) {
        QMessageBox::warning(this, tr("Could not open project"), loaded.failure);
        log(loaded.failure);
        return false;
    }

    newProject();
    m_projectPath = path;

    applySettings(loaded.project.settings, loaded.project.referenceUpdate);
    if (!loaded.project.referencePath.isEmpty())
        openReferenceImage(loaded.project.referencePath);
    if (!loaded.project.targetPaths.isEmpty())
        addTargetImages(loaded.project.targetPaths);
    if (loaded.project.roi.isValid())
        onRoiDrawn(loaded.project.roi);

    log(tr("Opened project %1.").arg(QFileInfo(path).fileName()));

    // ⚑ Said out loud, both of them. An image that has gone missing makes the
    // sequence shorter than the one that was saved, and an image that has
    // CHANGED makes the session measure different pixels under the same name -
    // which is the risk taken on by storing paths rather than pictures, and is
    // only acceptable because it is reported.
    for (const QString &gone : loaded.missing)
        log(tr("  Missing since this project was saved: %1").arg(gone));
    for (const QString &changed : loaded.changed)
        log(tr("  Changed since this project was saved: %1").arg(changed));

    if (!loaded.missing.isEmpty() || !loaded.changed.isEmpty()) {
        QMessageBox::warning(
            this, tr("The images are not as they were"),
            tr("%1 image(s) are missing and %2 have changed since this project "
               "was saved. The project has opened without them, or with the "
               "files as they are now. See the log for which.")
                .arg(loaded.missing.size())
                .arg(loaded.changed.size()));
    }
    return true;
}

void MainWindow::saveProjectAs()
{
    const QString suggested =
        m_projectPath.isEmpty()
            ? (m_referenceRecord.filePath.isEmpty()
                   ? QString()
                   : QFileInfo(m_referenceRecord.filePath).absolutePath()
                         + QLatin1Char('/')
                         + QFileInfo(m_referenceRecord.fileName).completeBaseName()
                         + QStringLiteral(".svproj"))
            : m_projectPath;

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save project"), suggested, tr("SurView project (*.svproj)"));
    if (path.isEmpty())
        return;

    saveProjectTo(path);
}

bool MainWindow::saveProjectTo(const QString &path)
{
    const QString refusal = saveProject(path, currentProject());
    if (!refusal.isEmpty()) {
        QMessageBox::warning(this, tr("Could not save project"), refusal);
        log(refusal);
        return false;
    }
    m_projectPath = path;
    log(tr("Saved project to %1.").arg(path));
    statusBar()->showMessage(tr("Project saved"), 4000);
    return true;
}

void MainWindow::openExample()
{
    auto *action = qobject_cast<QAction *>(sender());
    if (!action)
        return;
    const QStringList frames = action->data().toStringList();
    if (frames.size() < 2)
        return;

    openReferenceImage(frames.first());
    addTargetImages(frames.mid(1));
    log(tr("Opened the %1 example: %2").arg(action->text(), action->toolTip()));
}

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
        return dock;
    };

    // The two pillars are seated in the geometry: what was recorded on the
    // left, what is interpreted from it on the right.
    QDockWidget *project = addDock(tr("Project"), createProjectPanel(),
                                   Qt::LeftDockWidgetArea);
    QDockWidget *record = addDock(tr("Record"), createRecordPanel(),
                                  Qt::LeftDockWidgetArea);
    splitDockWidget(project, record, Qt::Vertical);
    resizeDocks({project, record}, {200, 560}, Qt::Vertical);

    // ⚑ Inside a scroll area, because the panel is taller than the dock and
    // getting taller. Mounted directly, Qt shrank its word-wrapped notes below
    // the height their text needs and the widgets below drew straight over
    // them: the Reference group's own explanation was cut off mid-sentence with
    // a checkbox on top of it. A wrapped QLabel reports a single line as its
    // minimum, so nothing about that is loud -- the panel simply looks broken,
    // and only at the sizes where it is.
    auto *analysis = new QScrollArea;
    analysis->setWidget(createAnalysisPanel());
    analysis->setWidgetResizable(true);
    analysis->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    analysis->setFrameShape(QFrame::NoFrame);
    QDockWidget *analysisDock = addDock(tr("Analysis"), analysis,
                                       Qt::RightDockWidgetArea);

    // The readout sits under the settings that produced the field, on the
    // interpretation side of the window, where a reader is already looking
    // when they ask what a point measured.
    m_point = new PointPanel;
    auto *pointScroll = new QScrollArea;
    pointScroll->setWidget(m_point);
    pointScroll->setWidgetResizable(true);
    pointScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pointScroll->setFrameShape(QFrame::NoFrame);
    QDockWidget *pointDock = addDock(tr("Point"), pointScroll,
                                     Qt::RightDockWidgetArea);
    splitDockWidget(analysisDock, pointDock, Qt::Vertical);
    addDock(tr("Log"), createLogPanel(), Qt::BottomDockWidgetArea);
}

QWidget *MainWindow::createProjectPanel()
{
    auto *tree = new QTreeWidget;
    tree->setHeaderHidden(true);
    m_projectTree = tree;
    connect(tree, &QTreeWidget::itemSelectionChanged, this,
            &MainWindow::showSelectedImage);

    m_referenceItem = new QTreeWidgetItem(tree);
    m_referenceItem->setText(0, tr("Reference image - none"));

    m_targetsItem = new QTreeWidgetItem(tree);
    m_targetsItem->setText(0, tr("Target images - none"));

    m_roiItem = new QTreeWidgetItem(tree);
    m_roiItem->setText(0, tr("Region of interest - none (whole image)"));

    m_resultsItem = new QTreeWidgetItem(tree);
    m_resultsItem->setText(0, tr("Results - none"));

    tree->expandAll();
    return tree;
}

QWidget *MainWindow::createRecordPanel()
{
    m_record = new RecordPanel;
    m_record->setMinimumWidth(300);
    return m_record;
}

QWidget *MainWindow::createAnalysisPanel()
{
    // Real OpenCorr correlation parameters, so the panel reads as the actual
    // control surface even before it drives the engine.
    auto *panel = new QWidget;
    auto *column = new QVBoxLayout(panel);

    auto *correlation = new QWidget(panel);
    auto *form = new QFormLayout(correlation);
    form->setContentsMargins(0, 0, 0, 0);
    column->addWidget(correlation);

    // Built from offeredSolverChoices() rather than listed here, so the panel
    // cannot offer a solver that nothing measures: the tests walk the same list.
    m_solver = new QComboBox;
    QVector<CorrelationSettings::Solver> seen;
    for (const SolverChoice &choice : offeredSolverChoices()) {
        if (seen.contains(choice.solver))
            continue;
        seen.append(choice.solver);
        m_solver->addItem(solverDisplayName(choice.solver), choice.solver);
    }
    connect(m_solver, &QComboBox::currentIndexChanged, this,
            &MainWindow::updateSolverConstraints);
    form->addRow(tr("Solver"), m_solver);

    m_shape = new QComboBox;
    m_shape->addItem(tr("First order (affine)"), 1);
    m_shape->addItem(tr("Second order (quadratic)"), 2);
    form->addRow(tr("Shape function"), m_shape);

    m_subsetRadius = new QSpinBox;
    m_subsetRadius->setRange(3, 100);
    m_subsetRadius->setValue(16);
    m_subsetRadius->setSuffix(tr(" px"));
    form->addRow(tr("Subset radius"), m_subsetRadius);

    m_gridStep = new QSpinBox;
    m_gridStep->setRange(1, 100);
    m_gridStep->setValue(5);
    m_gridStep->setSuffix(tr(" px"));
    form->addRow(tr("Grid step"), m_gridStep);

    m_maxIterations = new QSpinBox;
    m_maxIterations->setRange(1, 100);
    m_maxIterations->setValue(10);
    form->addRow(tr("Max iterations"), m_maxIterations);

    m_convergence = new QDoubleSpinBox;
    m_convergence->setDecimals(4);
    m_convergence->setRange(0.0001, 1.0);
    m_convergence->setSingleStep(0.0001);
    m_convergence->setValue(0.001);
    m_convergence->setPrefix(tr("‖Δp‖ ≤ "));
    form->addRow(tr("Convergence"), m_convergence);

    // Stated, not offered. The engine builds a bicubic B-spline interpolator
    // inside every solver and exposes no way to choose another, so a dropdown
    // here would imply a choice that does not exist.
    auto *interpolation = new QLabel(tr("Bicubic B-spline"));
    interpolation->setToolTip(
        tr("The engine's solvers interpolate the target subset with a bicubic "
           "B-spline and offer no alternative."));
    form->addRow(tr("Interpolation"), interpolation);

    // --- strain --------------------------------------------------------------
    // Its own group, because it is a second measurement rather than another
    // correlation parameter: it runs after the solve, over the solve's own
    // output, and it has a neighbourhood and a failure mode of its own.
    m_strainGroup = new QGroupBox(tr("Strain"), panel);
    auto *strainColumn = new QVBoxLayout(m_strainGroup);

    m_strainEnabled = new QCheckBox(tr("Fit strain from the displacement field"));
    m_strainEnabled->setChecked(true);
    m_strainEnabled->setToolTip(
        tr("Strain is the gradient of displacement, so it is fitted from the "
           "points around each point rather than measured at it."));
    strainColumn->addWidget(m_strainEnabled);

    auto *strainForm = new QFormLayout;
    strainForm->setContentsMargins(0, 6, 0, 0);

    m_strainRadius = new QDoubleSpinBox;
    m_strainRadius->setDecimals(1);
    m_strainRadius->setRange(1.0, 500.0);
    m_strainRadius->setValue(25.0);
    m_strainRadius->setSuffix(tr(" px"));
    strainForm->addRow(tr("Subregion radius"), m_strainRadius);

    m_strainMinPoints = new QSpinBox;
    m_strainMinPoints->setRange(3, 500);   // three points define a plane
    m_strainMinPoints->setValue(5);
    strainForm->addRow(tr("Fewest points in the fit"), m_strainMinPoints);

    // Built from offeredStrainMeasures(), the list the tests walk.
    m_strainMeasure = new QComboBox;
    for (const StrainMeasureChoice &choice : offeredStrainMeasures()) {
        m_strainMeasure->addItem(choice.name, int(choice.measure));
        m_strainMeasure->setItemData(m_strainMeasure->count() - 1, choice.note,
                                     Qt::ToolTipRole);
    }
    strainForm->addRow(tr("Strain measure"), m_strainMeasure);

    strainColumn->addLayout(strainForm);

    // Stated rather than adjustable. It decides which measurements the strain
    // field is built from, so leaving it unsaid would make a sparse strain map
    // over a dense displacement map look like a fault.
    auto *floorNote = new QLabel(
        tr("Points correlating below %1 are left out of the fit.")
            .arg(double(kStrainFitCorrelationFloor), 0, 'g', 2));
    floorNote->setWordWrap(true);
    // Set apart by weight, not by colour. palette(mid) was tried first and is
    // near-white on this palette's near-white panel -- the note was on screen
    // and unreadable, which is worse than absent, because nothing indicates
    // there is anything to read. Italic carries "aside" in every palette.
    QFont floorFont = floorNote->font();
    floorFont.setItalic(true);
    floorNote->setFont(floorFont);
    strainColumn->addWidget(floorNote);

    // The live warning. See updateStrainAdvice().
    m_strainAdvice = new QLabel;
    m_strainAdvice->setWordWrap(true);
    m_strainAdvice->setStyleSheet(
        QStringLiteral("color: #b9770e; border: 1px solid #b9770e;"
                       " border-radius: 4px; padding: 6px;"));
    m_strainAdvice->hide();
    strainColumn->addWidget(m_strainAdvice);

    column->addWidget(m_strainGroup);

    // --- the reference a sequence measures against --------------------------
    auto *referenceGroup = new QGroupBox(tr("Reference"), panel);
    auto *referenceColumn = new QVBoxLayout(referenceGroup);

    auto *referenceNote = new QLabel(
        tr("Every frame is measured against the original reference, so the "
           "displacements are directly comparable. Correlation degrades as the "
           "specimen moves away from where it started."));
    referenceNote->setWordWrap(true);
    referenceColumn->addWidget(referenceNote);

    m_reanchorEnabled =
        new QCheckBox(tr("Re-anchor the reference when tracking degrades"));
    m_reanchorEnabled->setChecked(false);
    referenceColumn->addWidget(m_reanchorEnabled);

    auto *reanchorForm = new QFormLayout;
    reanchorForm->setContentsMargins(0, 6, 0, 0);

    m_reanchorThreshold = new QDoubleSpinBox;
    m_reanchorThreshold->setDecimals(2);
    m_reanchorThreshold->setRange(0.10, 0.99);
    m_reanchorThreshold->setSingleStep(0.05);
    m_reanchorThreshold->setValue(0.90);
    reanchorForm->addRow(tr("Correlation a point must keep"), m_reanchorThreshold);

    m_reanchorShare = new QSpinBox;
    m_reanchorShare->setRange(1, 100);
    m_reanchorShare->setValue(90);
    m_reanchorShare->setSuffix(tr(" %"));
    reanchorForm->addRow(tr("Share of points that must keep it"), m_reanchorShare);

    referenceColumn->addLayout(reanchorForm);

    // The cost, next to the switch. Re-anchoring is the right answer for a
    // specimen that deforms far and the wrong one for a specimen that does not,
    // and the part nobody expects is that it abandons points.
    auto *reanchorCost = new QLabel(
        tr("Once too few points still correlate, later frames are measured "
           "against the current frame instead, and each point's displacement "
           "is carried forward so results stay relative to the original "
           "reference. Any point that could not be measured on that frame is "
           "lost for the rest of the run."));
    reanchorCost->setWordWrap(true);
    QFont costFont = reanchorCost->font();
    costFont.setItalic(true);
    reanchorCost->setFont(costFont);
    referenceColumn->addWidget(reanchorCost);

    column->addWidget(referenceGroup);
    column->addStretch(1);

    connect(m_reanchorEnabled, &QCheckBox::toggled, this,
            &MainWindow::updateReferenceUpdateControls);
    updateReferenceUpdateControls();

    connect(m_strainEnabled, &QCheckBox::toggled, this,
            &MainWindow::updateStrainAdvice);
    connect(m_strainRadius, &QDoubleSpinBox::valueChanged, this,
            &MainWindow::updateStrainAdvice);
    connect(m_strainMinPoints, &QSpinBox::valueChanged, this,
            &MainWindow::updateStrainAdvice);
    connect(m_gridStep, &QSpinBox::valueChanged, this,
            &MainWindow::updateStrainAdvice);

    updateSolverConstraints();
    updateStrainAdvice();
    return panel;
}

ReferenceUpdatePolicy MainWindow::currentReferencePolicy() const
{
    ReferenceUpdatePolicy policy;
    policy.enabled = m_reanchorEnabled->isChecked();
    policy.znccThreshold = m_reanchorThreshold->value();
    policy.percentile = m_reanchorShare->value() / 100.0;
    return policy;
}

void MainWindow::updateReferenceUpdateControls()
{
    const bool on = m_reanchorEnabled->isChecked();
    m_reanchorThreshold->setEnabled(on);
    m_reanchorShare->setEnabled(on);
}

void MainWindow::updateStrainAdvice()
{
    // The sub-controls follow the checkbox, so the group always reads as one
    // decision rather than four independent ones.
    const bool on = m_strainEnabled->isChecked();
    m_strainRadius->setEnabled(on);
    m_strainMinPoints->setEnabled(on);
    m_strainMeasure->setEnabled(on);

    // Worked out by core/StrainFit.h, before the run, because the engine will
    // not refuse these settings -- it will quietly fit over a different
    // neighbourhood and return a field that looks exactly as complete as a
    // good one.
    const QString advice = currentSettings().strainWarning();
    m_strainAdvice->setText(advice);
    m_strainAdvice->setVisible(!advice.isEmpty());
}

void MainWindow::updateSolverConstraints()
{
    if (!m_solver || !m_shape)
        return;

    const auto solver = CorrelationSettings::Solver(m_solver->currentData().toInt());

    CorrelationSettings probe;
    probe.solver = solver;
    probe.shapeOrder = 2;
    const bool secondOrderAvailable = probe.isAvailable();

    // Disabled in place rather than removed: the option still exists in the
    // engine for other solvers, and a control that vanishes teaches nothing
    // about why.
    if (auto *model = qobject_cast<QStandardItemModel *>(m_shape->model())) {
        if (QStandardItem *item = model->item(1)) {
            item->setEnabled(secondOrderAvailable);
            item->setToolTip(secondOrderAvailable ? QString()
                                                  : probe.unavailableReason());
        }
    }
    if (!secondOrderAvailable && m_shape->currentIndex() == 1)
        m_shape->setCurrentIndex(0);
}

CorrelationSettings MainWindow::currentSettings() const
{
    CorrelationSettings settings;
    settings.solver = CorrelationSettings::Solver(m_solver->currentData().toInt());
    settings.shapeOrder = m_shape->currentData().toInt();
    settings.subsetRadius = m_subsetRadius->value();
    settings.gridStep = m_gridStep->value();
    settings.maxIterations = m_maxIterations->value();
    settings.convergence = m_convergence->value();

    settings.strainEnabled = m_strainEnabled->isChecked();
    settings.strainRadius = m_strainRadius->value();
    settings.strainMinPoints = m_strainMinPoints->value();
    settings.strainMeasure = StrainMeasure(m_strainMeasure->currentData().toInt());
    return settings;
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
    m_progress = new QProgressBar;
    m_progress->setMaximumWidth(180);
    m_progress->setTextVisible(true);
    m_progress->hide();
    statusBar()->addPermanentWidget(m_progress);

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

    openReferenceImage(path);
}

void MainWindow::openReferenceImage(const QString &path)
{
    const QString name = QFileInfo(path).fileName();
    if (!m_viewport->loadImage(path)) {
        QMessageBox::warning(
            this, tr("Import failed"),
            tr("Could not read the image:\n%1").arg(path));
        log(tr("Failed to load reference image: %1").arg(name));
        return;
    }

    m_referenceRecord = m_viewport->record();
    const ImageRecord &record = m_referenceRecord;
    m_record->setRecord(record);

    m_referenceItem->setText(0, tr("Reference image - %1 (%2×%3)")
                                    .arg(name)
                                    .arg(record.width)
                                    .arg(record.height));
    m_referenceItem->setData(0, kRecordKindRole, RecordKind::Reference);
    m_stageLabel->setText(tr("Reference loaded"));
    statusBar()->showMessage(tr("Loaded %1").arg(name), 4000);
    log(tr("Loaded reference image: %1 - %2×%3 px, %4, %5, no conversion")
            .arg(name)
            .arg(record.width)
            .arg(record.height)
            .arg(record.pixelTypeName(), record.channelsText()));
    updateActionStates();
}

void MainWindow::importTargetImages()
{
    addTargetImages(QFileDialog::getOpenFileNames(
        this, tr("Import Target Image(s)"), QString(),
        tr("Images (*.tif *.tiff *.png *.jpg *.jpeg *.bmp);;All files (*)")));
}

void MainWindow::addTargetImages(const QStringList &paths)
{
    if (paths.isEmpty())
        return;

    // Each target is decoded and recorded on the way in, exactly as the
    // reference is. Reading every file costs time on a long sequence, and that
    // is the intended trade: an image we have not read is an image we cannot
    // make any truthful statement about, and the record is the foundation the
    // measurement stands on.
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);

    int recorded = 0;
    int unreadable = 0;
    int mismatched = 0;

    // Built detached and parented once the whole list has been put in frame
    // order, so the tree never shows an order the run would not follow.
    QVector<QTreeWidgetItem *> pending;

    for (const QString &path : paths) {
        const QString name = QFileInfo(path).fileName();

        ImageRecord record;
        const bool decoded = decodeImage(path, record) != nullptr;

        m_targetRecords.append(record);
        auto *item = new QTreeWidgetItem;
        item->setData(0, kRecordKindRole, RecordKind::Target);

        if (!decoded) {
            // Kept in the list rather than dropped: it was named as a target,
            // and silently omitting it would leave the count disagreeing with
            // what was selected.
            ++unreadable;
            pending.append(item);
            item->setText(0, tr("%1 - could not be read").arg(name));
            item->setIcon(0, style()->standardIcon(QStyle::SP_MessageBoxCritical));
            log(tr("Target image could not be read: %1").arg(name));
            continue;
        }

        ++recorded;
        const PairCompatibility pairing =
            compareToReference(m_referenceRecord, record);

        if (pairing.matches()) {
            item->setText(0, tr("%1 (%2×%3)")
                                 .arg(name)
                                 .arg(record.width)
                                 .arg(record.height));
            log(tr("Recorded target image: %1 - %2×%3 px, %4, %5, no conversion")
                    .arg(name)
                    .arg(record.width)
                    .arg(record.height)
                    .arg(record.pixelTypeName(), record.channelsText()));
        } else {
            ++mismatched;
            // The reason is stated in the log and the tooltip, but this panel
            // is narrow enough to truncate the text -- so the item also carries
            // an icon, which survives any width.
            item->setText(0, tr("%1 (%2×%3) - does not match the reference")
                                 .arg(name)
                                 .arg(record.width)
                                 .arg(record.height));
            item->setIcon(0, style()->standardIcon(QStyle::SP_MessageBoxWarning));
            item->setToolTip(0, pairing.mismatches.join(QStringLiteral("\n")));
            log(tr("Recorded target image: %1 - does not match the reference: %2")
                    .arg(name, pairing.mismatches.join(QStringLiteral("; "))));
        }
        pending.append(item);
    }

    // ⚑ Sorted into FRAME order, not kept in the order the files were chosen.
    // A sequence is a time axis: measured 1, 10, 11, 2 because that is how
    // strings sort, every field is correct and the series they form is
    // nonsense, and nothing on screen could reveal it. The list the project
    // shows is the order the run will use, which is what makes it checkable.
    rebuildTargetList(pending);

    QGuiApplication::restoreOverrideCursor();

    // "recorded" means the pixels were read; a file we could not decode is
    // listed but not recorded, and the heading has to keep those apart rather
    // than folding both into one flattering total.
    // Counted from the records themselves, not from this batch's tally, so the
    // heading stays right across repeated imports. A record whose pixels could
    // not be read holds provenance only, and is not valid.
    const int listed = m_targetRecords.size();
    int readable = 0;
    for (const ImageRecord &target : m_targetRecords) {
        if (target.isValid())
            ++readable;
    }
    m_targetsItem->setText(0, readable == listed
                                  ? tr("Target images - %1 recorded").arg(listed)
                                  : tr("Target images - %1 of %2 recorded")
                                        .arg(readable)
                                        .arg(listed));
    m_targetsItem->setExpanded(true);

    // Say what happened in one line, including the parts that went wrong --
    // a count of successes alone would read as a clean import. Written with
    // explicit singular/plural rather than tr()'s %n, which only selects a
    // plural form when a translation catalogue supplies one; untranslated, the
    // source string prints verbatim and "1 do not match" ships.
    QStringList summary;
    summary << (recorded == 1 ? tr("1 target image recorded")
                              : tr("%1 target images recorded").arg(recorded));
    if (mismatched > 0) {
        summary << (mismatched == 1
                        ? tr("1 does not match the reference")
                        : tr("%1 do not match the reference").arg(mismatched));
    }
    if (unreadable > 0) {
        summary << (unreadable == 1 ? tr("1 could not be read")
                                    : tr("%1 could not be read").arg(unreadable));
    }
    statusBar()->showMessage(summary.join(tr(", ")), 6000);

    if (mismatched > 0 || unreadable > 0) {
        QMessageBox::warning(this, tr("Target images imported"),
                             summary.join(QStringLiteral("\n")));
    }

    updateActionStates();
}

// Show a record in the record panel, and its pixels in the viewport. The
// viewport re-reads the file rather than holding every frame's pixels in
// memory; the record it produces is used only for the display mapping, since
// everything else was already recorded at import.
void MainWindow::displayRecord(const ImageRecord &record)
{
    // A record we hold but cannot display: still report it, and say why the
    // viewport is empty rather than leaving the previously shown image up,
    // which would read as this selection's pixels.
    if (!record.isValid()) {
        m_viewport->showMessage(
            tr("%1\n\ncould not be read - recorded, but no pixels to show")
                .arg(record.fileName));
        m_record->setRecord(record);
        return;
    }

    ImageRecord shown = record;
    if (m_viewport->loadImage(record.filePath)) {
        const ImageRecord &displayed = m_viewport->record();
        shown.displayed  = displayed.displayed;
        shown.displayMin = displayed.displayMin;
        shown.displayMax = displayed.displayMax;
    }

    // The boundary belongs to the reference image's pixel grid, so it is drawn
    // over an image that shares that grid and withheld from one that does not:
    // an outline over a differently sized picture would point at pixels it was
    // never about.
    const bool sameGrid = m_roi.isValid() && record.isValid()
                          && record.width == m_referenceRecord.width
                          && record.height == m_referenceRecord.height;
    if (sameGrid)
        m_viewport->showRoi(m_roi);
    else
        m_viewport->clearRoi();

    m_record->setRecord(shown);
}

void MainWindow::rebuildTargetList(QVector<QTreeWidgetItem *> pending)
{
    // Everything already listed, plus what just arrived, in one list parallel
    // to m_targetRecords.
    QVector<QTreeWidgetItem *> items;
    while (m_targetsItem->childCount() > 0)
        items.append(m_targetsItem->takeChild(0));
    items += pending;

    QVector<int> order(items.size());
    for (int i = 0; i < order.size(); i++)
        order[i] = i;

    // By the name each record carries, using the same comparison the run
    // itself will use, so the list and the measurement cannot disagree.
    std::stable_sort(order.begin(), order.end(), [this](int a, int b) {
        return precedesInSequence(m_targetRecords.at(a).filePath,
                                  m_targetRecords.at(b).filePath);
    });

    QVector<ImageRecord> ordered;
    ordered.reserve(m_targetRecords.size());
    for (int i : order) {
        ordered.append(m_targetRecords.at(i));
        QTreeWidgetItem *item = items.at(i);
        item->setData(0, kRecordIndexRole, int(ordered.size()) - 1);
        m_targetsItem->addChild(item);
    }
    m_targetRecords = ordered;
}

const CorrelationResult &MainWindow::frameResult(int frame) const
{
    static const CorrelationResult nothing;
    if (frame < 0 || frame >= m_frames.size())
        return nothing;
    return m_frames.at(frame).result;
}

void MainWindow::showPoint(int index)
{
    if (!m_hasResult) {
        m_point->clear();
        return;
    }
    m_point->showReadout(pointReadout(m_result, index), m_pinnedPoint >= 0);
}

void MainWindow::onFieldPointHovered(const QPointF &imagePixel, bool insideImage)
{
    // A pinned reading is not disturbed by the pointer. That is the whole
    // point of pinning: a readout that follows the pointer cannot be read and
    // written down at the same time, because looking away erases it.
    if (m_pinnedPoint >= 0)
        return;

    if (!insideImage) {
        showPoint(-1);
        return;
    }
    showPoint(pointNearestTo(m_result, float(imagePixel.x()),
                             float(imagePixel.y())));
}

void MainWindow::onFieldPointPicked(const QPointF &imagePixel)
{
    if (!m_hasResult)
        return;

    const int index = pointNearestTo(m_result, float(imagePixel.x()),
                                     float(imagePixel.y()));

    // Clicking the pinned point again releases it; clicking a different one
    // moves the pin there. Clicking away from the field releases too, which is
    // the reading that matches what a user just did: they pointed somewhere
    // there is no measurement.
    if (m_pinnedPoint >= 0 && (index < 0 || index == m_pinnedPoint)) {
        m_pinnedPoint = -1;
        showPoint(index);
        return;
    }

    m_pinnedPoint = index;
    showPoint(index);
}

void MainWindow::displayFrame(int frame)
{
    if (frame < 0 || frame >= m_frames.size())
        return;

    // Whether this is a DIFFERENT frame, asked before m_displayedFrame moves.
    // Re-displaying the frame already on screen happens for reasons that have
    // nothing to do with the reading -- the project tree re-selecting the same
    // node, a run finishing on the frame already shown -- and throwing the
    // pinned point away then would erase a reading nobody asked to erase.
    const bool frameChanged = frame != m_displayedFrame;

    m_displayedFrame = frame;
    m_result = m_frames.at(frame).result;
    m_hasResult = m_result.converged > 0;

    if (m_hasResult)
        m_viewport->showField(m_result);
    else
        m_viewport->clearField();

    // ⚑ The pin is an index into the result that WAS displayed. Carried across
    // a frame change it would keep reading, in the same authoritative panel, a
    // different point of a different frame.
    if (frameChanged) {
        m_pinnedPoint = -1;
        showPoint(-1);
    }

    updateActionStates();
}

void MainWindow::showSelectedImage()
{
    const QList<QTreeWidgetItem *> selected = m_projectTree->selectedItems();
    if (selected.isEmpty())
        return;

    // Corners are placed against the pixels on screen. Changing which image is
    // on screen mid-boundary would leave the ones already placed referring to a
    // picture the user is no longer looking at, so the mode ends here.
    m_viewport->cancelRoiDrawing();

    const QTreeWidgetItem *item = selected.first();
    const int kind = item->data(0, kRecordKindRole).toInt();

    if (kind == RecordKind::Reference) {
        displayRecord(m_referenceRecord);
    } else if (kind == RecordKind::Target) {
        const int index = item->data(0, kRecordIndexRole).toInt();
        if (index >= 0 && index < m_targetRecords.size())
            displayRecord(m_targetRecords.at(index));
    } else if (kind == RecordKind::Frame) {
        // A measured frame of the sequence. Picking it puts that frame's field
        // on screen: a sequence nobody can step through measured itself for
        // nothing.
        displayFrame(item->data(0, kRecordIndexRole).toInt());
    }
    // Anything else (ROIs, results) has no record to show; leave the panel on
    // whatever it was showing rather than blanking it for a non-image node.
}

// ---------------------------------------------------------------------------
// Region of interest
// ---------------------------------------------------------------------------

// A field belongs to the region it was measured over. Once that region is
// replaced, leaving the field on screen underneath a different boundary invites
// reading the two together, which is a claim about points that were never
// measured inside it. It is dropped rather than annotated: there is no export
// yet, so nothing is lost that a re-run does not reproduce.
void MainWindow::discardStaleResult()
{
    if (!m_hasResult)
        return;

    m_hasResult = false;
    m_result = CorrelationResult();
    m_viewport->clearField();
    m_pinnedPoint = -1;
    m_point->clear();
    m_resultsItem->setText(0, tr("Results - none"));
    m_resultsItem->setToolTip(0, QString());
    log(tr("Previous displacement field discarded - it was measured over a "
           "different region, and showing it beside this one would invite "
           "reading them as the same measurement."));
}

void MainWindow::showRoiInProject()
{
    if (!m_roi.isValid()) {
        m_roiItem->setText(0, tr("Region of interest - none (whole image)"));
        m_roiItem->setToolTip(0, QString());
        m_viewport->clearRoi();
        return;
    }

    const QRect box = m_roi.bounds();
    const QString summary = tr("Region of interest - %1, %2 corners, %3×%4 px box")
                                .arg(m_roi.originText())
                                .arg(m_roi.vertices.size())
                                .arg(box.width())
                                .arg(box.height());
    m_roiItem->setText(0, summary);

    // What the region does NOT do sits beside it rather than in a manual. Both
    // sentences are easy to assume wrongly, and assuming either one wrongly
    // changes how the resulting field should be read.
    QStringList notes;
    notes << summary;
    notes << tr("Selects the point centres that get measured. Each subset still "
                "reaches up to its radius beyond the boundary, so pixels just "
                "outside it contribute to the points near its edge.");
    if (!m_roi.limitation.isEmpty())
        notes << m_roi.limitation;
    m_roiItem->setToolTip(0, notes.join(QStringLiteral("\n\n")));

    m_viewport->showRoi(m_roi);
}

void MainWindow::onRoiDrawn(const RegionOfInterest &roi)
{
    discardStaleResult();
    m_roi = roi;
    showRoiInProject();

    const QRect box = m_roi.bounds();
    log(tr("Region of interest defined by hand - %1 corners, bounding box "
           "%2×%3 px at (%4, %5). Point centres are taken inside it; each "
           "subset still reaches up to its radius beyond it.")
            .arg(roi.vertices.size())
            .arg(box.width())
            .arg(box.height())
            .arg(box.left())
            .arg(box.top()));
    statusBar()->showMessage(tr("Region of interest defined"), 4000);
    updateActionStates();
}

void MainWindow::detectRoi()
{
    if (!m_referenceRecord.isValid())
        return;

    // The proposal is made from the REFERENCE image whatever is on screen: the
    // region applies to the reference's pixel grid, and detecting from a
    // deformed target would propose a boundary around a shape that has already
    // moved.
    // The detector runs on this thread, so the window is unresponsive while it
    // works -- half a second on a small image, longer on a large one. The
    // message and the cursor are therefore flushed to screen BEFORE the work
    // starts; set and then blocked on, they would only appear once it finished,
    // which is the one moment they are no longer wanted. The action is disabled
    // across that flush so the pending events cannot start a second pass.
    m_actAutoRoi->setEnabled(false);
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    statusBar()->showMessage(tr("Looking for the speckled region…"));
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    const RoiDetection detection =
        detectSpeckleRegion(m_referenceRecord.filePath);

    QGuiApplication::restoreOverrideCursor();
    updateActionStates();

    if (!detection.found) {
        log(tr("Auto-detect found no region: %1").arg(detection.reason));
        statusBar()->showMessage(tr("No region detected"), 6000);
        QMessageBox::information(this, tr("No region detected"),
                                 detection.reason);
        return;
    }

    discardStaleResult();
    m_roi = detection.roi;
    showRoiInProject();

    const QRect box = m_roi.bounds();
    log(tr("Region of interest detected in %1 s - %2 corners, bounding box "
           "%3×%4 px at (%5, %6), from %7.")
            .arg(detection.secondsElapsed, 0, 'f', 1)
            .arg(m_roi.vertices.size())
            .arg(box.width())
            .arg(box.height())
            .arg(box.left())
            .arg(box.top())
            .arg(m_referenceRecord.fileName));
    // The detector's own limitation travels with the proposal into the log,
    // where the run that used it is also recorded -- not only into a tooltip
    // that the record of this session will not keep.
    log(tr("  %1").arg(m_roi.limitation));
    statusBar()->showMessage(
        tr("Region detected - check it, and redraw it by hand if it is wrong"),
        8000);
    updateActionStates();
}

void MainWindow::clearRoi()
{
    if (!m_roi.isValid())
        return;

    discardStaleResult();
    m_roi = RegionOfInterest();
    showRoiInProject();
    log(tr("Region of interest cleared - the next run measures the whole image."));
    statusBar()->showMessage(tr("Region of interest cleared"), 4000);
    updateActionStates();
}

void MainWindow::runCorrelation()
{
    if (m_workerThread)
        return;  // already running

    // Which targets can be measured, in which order, and why the rest cannot.
    // Worked out by core/Sequence.h rather than here, so the same judgement is
    // testable without a window.
    const SequencePlan plan = planSequence(m_referenceRecord, m_targetRecords);

    if (plan.isEmpty()) {
        QMessageBox::warning(
            this, tr("Nothing to correlate"),
            m_targetRecords.isEmpty()
                ? tr("No target image has been imported. Correlation compares "
                     "the same pixel grid before and after deformation.")
                : tr("None of the %1 imported target images can be correlated "
                     "against the reference. The log lists the reason for each.")
                      .arg(m_targetRecords.size()));
        for (auto it = plan.skipped.constBegin(); it != plan.skipped.constEnd(); ++it) {
            log(tr("  %1 cannot be measured: %2")
                    .arg(m_targetRecords.at(it.key()).fileName, it.value()));
        }
        return;
    }

    const CorrelationSettings settings = currentSettings();
    if (!settings.isAvailable()) {
        QMessageBox::warning(this, tr("Settings not available"),
                             settings.unavailableReason());
        return;
    }

    // Which targets were used is part of the result. Named in full, in frame
    // order, because a sequence that quietly measured a different set than the
    // one on screen would be unattributable.
    QStringList targetPaths;
    QStringList frameNames;
    for (int index : plan.order) {
        targetPaths << m_targetRecords.at(index).filePath;
        frameNames << m_targetRecords.at(index).fileName;
    }

    log(targetPaths.size() == 1
            ? tr("Correlating %1 against %2 - %3, %4, subset radius %5 px, "
                 "grid step %6 px, %7")
                  .arg(m_referenceRecord.fileName, frameNames.first(),
                       m_solver->currentText(), m_shape->currentText())
                  .arg(settings.subsetRadius)
                  .arg(settings.gridStep)
                  .arg(m_roi.isValid() ? tr("inside the region of interest (%1)")
                                             .arg(m_roi.originText())
                                       : tr("whole image"))
            : tr("Correlating a %1 frame sequence against %2 - %3, %4, subset "
                 "radius %5 px, grid step %6 px, %7")
                  .arg(targetPaths.size())
                  .arg(m_referenceRecord.fileName, m_solver->currentText(),
                       m_shape->currentText())
                  .arg(settings.subsetRadius)
                  .arg(settings.gridStep)
                  .arg(m_roi.isValid() ? tr("inside the region of interest (%1)")
                                             .arg(m_roi.originText())
                                       : tr("whole image")));

    if (targetPaths.size() > 1) {
        // ⚑ Said out loud, because a sequence that stops correlating halfway
        // through looks exactly like a specimen that stopped deforming.
        log(tr("  Frame order: %1").arg(frameNames.join(QStringLiteral(", "))));
        log(tr("  Every frame is measured against %1, never against the frame "
               "before it, so the displacements are directly comparable - and "
               "correlation degrades as the specimen moves away from where it "
               "started.")
                .arg(m_referenceRecord.fileName));
    }

    // Named rather than passed over: a run that measured 9 of 12 imported
    // frames has to say which three it left out and why.
    for (auto it = plan.skipped.constBegin(); it != plan.skipped.constEnd(); ++it) {
        log(tr("  Skipping %1: %2")
                .arg(m_targetRecords.at(it.key()).fileName, it.value()));
    }

    if (settings.strainEnabled) {
        log(tr("  Strain: %1, fitted over a %2 px subregion, at least %3 points, "
               "excluding anything correlating below %4")
                .arg(strainMeasureName(settings.strainMeasure))
                .arg(settings.strainRadius, 0, 'g', 4)
                .arg(settings.strainMinPoints)
                .arg(double(kStrainFitCorrelationFloor), 0, 'g', 2));
        const QString advice = settings.strainWarning();
        if (!advice.isEmpty())
            log(tr("  %1").arg(advice));
    } else {
        log(tr("  Strain: not fitted."));
    }

    m_hasResult = false;
    m_displayedFrame = -1;
    m_frames.clear();
    m_viewport->clearField();
    m_resultsItem->takeChildren();

    // Provenance captured here, per frame, from the very values handed to the
    // runner. Read back from the panel at export time it would state, with a
    // SHA-256 beside it, a configuration that never measured anything.
    m_plannedFrames.clear();
    for (int index : plan.order) {
        FieldProvenance provenance;
        provenance.reference = m_referenceRecord;
        provenance.target = m_targetRecords.at(index);
        provenance.settings = settings;
        provenance.applicationVersion = QStringLiteral(SURVIEW_VERSION);
        provenance.enginePin = QStringLiteral(SURVIEW_OPENCORR_PIN);
        m_plannedFrames.append(provenance);
    }

    m_workerThread = new QThread(this);
    m_runner = new SequenceRunner(settings, m_roi, m_referenceRecord.filePath,
                                  targetPaths, currentReferencePolicy());
    m_runner->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::started, m_runner, &SequenceRunner::run);
    connect(m_runner, &SequenceRunner::frameProgress, this,
            &MainWindow::onFrameProgress);
    connect(m_runner, &SequenceRunner::frameFinished, this,
            &MainWindow::onFrameFinished);
    connect(m_runner, &SequenceRunner::finished, this,
            &MainWindow::onSequenceFinished);
    connect(m_runner, &SequenceRunner::referenceReanchored, this,
            [this](int frame, int pointsLost) {
                // Never silent: after this, every later frame is compared
                // against a different picture, and a reader who does not know
                // that cannot judge the numbers.
                log(tr("  Frame %1: too little of the field was still tracking, "
                       "so the reference re-anchored to %2.%3")
                        .arg(frame + 1)
                        .arg(frame < m_plannedFrames.size()
                                 ? m_plannedFrames.at(frame).target.fileName
                                 : QString())
                        .arg(pointsLost > 0
                                 ? tr(" %1 point(s) could not be measured on that "
                                      "frame and are lost for the rest of the run.")
                                       .arg(pointsLost)
                                 : QString()));
            });

    connect(m_runner, &SequenceRunner::failed, this,
            &MainWindow::onCorrelationFailed);

    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->show();
    m_stageLabel->setText(targetPaths.size() == 1
                              ? tr("Correlating")
                              : tr("Correlating %1 frames").arg(targetPaths.size()));

    m_workerThread->start();
    updateActionStates();
}

void MainWindow::stopCorrelation()
{
    if (!m_runner)
        return;
    // The engine cannot be interrupted mid-call, so this takes effect at the
    // next chunk boundary rather than instantly. Whatever was measured before
    // that point is kept and reported as partial, and on a sequence the frames
    // already finished are kept in full.
    log(tr("Stop requested - finishing the current block."));
    m_runner->cancel();
    m_actStop->setEnabled(false);
}

void MainWindow::onFrameProgress(int frame, int frameCount, int done, int total,
                                 const QString &stage)
{
    if (total <= 0 || frameCount <= 0)
        return;

    // Across the whole sequence, not restarted per frame: a bar that runs to
    // the end twelve times says nothing about how long is left.
    const double withinFrame = double(done) / double(total);
    m_progress->setValue(int(100.0 * (frame + withinFrame) / frameCount));

    statusBar()->showMessage(
        frameCount == 1
            ? tr("%1 - %2 of %3 points").arg(stage).arg(done).arg(total)
            : tr("Frame %1 of %2, %3 - %4 of %5 points")
                  .arg(frame + 1)
                  .arg(frameCount)
                  .arg(stage)
                  .arg(done)
                  .arg(total));
}

void MainWindow::logFrameResult(int frame, const CorrelationResult &result)
{
    const double share =
        result.total() > 0 ? 100.0 * result.converged / result.total() : 0.0;
    const QString name = frame < m_plannedFrames.size()
                             ? m_plannedFrames.at(frame).target.fileName
                             : QString();

    log(result.cancelled
            ? tr("Frame %1 (%2) stopped after %3 s - %4 of %5 points solved (%6%)")
                  .arg(frame + 1)
                  .arg(name)
                  .arg(result.secondsElapsed, 0, 'f', 1)
                  .arg(result.converged)
                  .arg(result.total())
                  .arg(share, 0, 'f', 1)
            : tr("Frame %1 (%2) finished in %3 s - %4 of %5 points solved (%6%)")
                  .arg(frame + 1)
                  .arg(name)
                  .arg(result.secondsElapsed, 0, 'f', 1)
                  .arg(result.converged)
                  .arg(result.total())
                  .arg(share, 0, 'f', 1));

    if (result.restrictedToRoi) {
        log(tr("  those %1 points are the ones inside the region of interest, "
               "not the whole image")
                .arg(result.total()));
    }

    // Every failure reason the engine gave, each with its own count. A single
    // "N failed" would hide that "subset out of bounds" and "did not converge"
    // call for different responses.
    for (auto it = result.failuresByReason.constBegin();
         it != result.failuresByReason.constEnd(); ++it) {
        log(tr("  %1 point(s): %2").arg(it.value()).arg(it.key()));
    }

    // How far the field can be trusted, as its own line rather than folded into
    // the solved count: "1025 of 1092 solved" says how much was measured, not
    // how well.
    if (result.noiseFloorMeasured > 0) {
        double lowest = 0.0;
        double typical = 0.0;
        noiseFloorSpread(result, lowest, typical);
        log(tr("  Noise floor %1 to %2 px for 95 per cent of measured points, "
               "against an estimated %3 grey levels of noise in the reference "
               "image. A lower bound on error, not a total error bar.")
                .arg(lowest, 0, 'g', 3)
                .arg(typical, 0, 'g', 3)
                .arg(result.referenceNoise, 0, 'g', 3));
        const QString context = noiseFloorAgainstMovement(result);
        if (!context.isEmpty())
            log(tr("  %1").arg(context));
    }
    if (result.conditioningUnusable > 0) {
        log(tr("  %1 solved point(s) had a correlation cost too flat to probe, "
               "so their match conditioning could not be established. Treat "
               "those points with caution.")
                .arg(result.conditioningUnusable));
    }

    if (result.strainRequested) {
        log(tr("  Strain fitted at %1 of the %2 solved points (%3, %4 px "
               "subregion)")
                .arg(result.strainFitted)
                .arg(result.converged)
                .arg(strainMeasureName(result.strainMeasure))
                .arg(result.strainRadius, 0, 'g', 4));
        if (result.belowStrainFloor > 0) {
            log(tr("  %1 solved point(s) correlated below %2 and were left out "
                   "of every fit they fell inside")
                    .arg(result.belowStrainFloor)
                    .arg(double(kStrainFitCorrelationFloor), 0, 'g', 2));
        }
        if (result.strainFitted == 0) {
            log(tr("  No point had enough well-correlated neighbours inside its "
                   "subregion. Widen the subregion, lower the minimum, or "
                   "improve the correlation."));
        }
    }
}

void MainWindow::onFrameFinished(int frame, const CorrelationResult &result)
{
    if (frame < 0 || frame >= m_plannedFrames.size())
        return;

    MeasuredFrame arrived;
    arrived.result = result;
    arrived.provenance = m_plannedFrames.at(frame);
    m_frames.append(arrived);
    logFrameResult(frame, result);

    // Listed as it arrives, so a long sequence fills in rather than staying
    // empty until the end. Each entry carries the frame it measured and enough
    // of the answer to choose between them without opening each one.
    double lowest = 0.0;
    double highest = 0.0;
    const bool measured =
        fieldValueRange(result, FieldChannel::DisplacementMagnitude, lowest, highest);

    auto *item = new QTreeWidgetItem(m_resultsItem);
    item->setData(0, kRecordKindRole, RecordKind::Frame);
    item->setData(0, kRecordIndexRole, frame);
    const QString summary =
        measured ? tr("Frame %1: %2 - %3 of %4 points, %5 to %6 px")
                       .arg(frame + 1)
                       .arg(m_plannedFrames.at(frame).target.fileName)
                       .arg(result.converged)
                       .arg(result.total())
                       .arg(lowest, 0, 'f', 2)
                       .arg(highest, 0, 'f', 2)
                 : tr("Frame %1: %2 - nothing could be solved")
                       .arg(frame + 1)
                       .arg(m_plannedFrames.at(frame).target.fileName);
    item->setText(0, summary);
    item->setToolTip(0, summary);
    m_resultsItem->setExpanded(true);

    // The newest frame goes on screen as it lands, so a sequence can be watched
    // rather than waited for.
    displayFrame(frame);
}

void MainWindow::onSequenceFinished(int framesMeasured, bool cancelled)
{
    const int planned = int(m_plannedFrames.size());

    m_resultsItem->setText(0,
        framesMeasured == 0
            ? tr("Results - none")
            : (cancelled
                   ? tr("Results - %1 of %2 frames measured before stopping")
                         .arg(framesMeasured)
                         .arg(planned)
                   : (framesMeasured == 1
                          ? tr("Results - 1 frame measured")
                          : tr("Results - %1 frames measured").arg(framesMeasured))));
    m_resultsItem->setToolTip(0, m_resultsItem->text(0));

    if (planned > 1) {
        log(cancelled ? tr("Sequence stopped after %1 of %2 frames.")
                            .arg(framesMeasured)
                            .arg(planned)
                      : tr("Sequence finished - %1 frames measured.")
                            .arg(framesMeasured));
    }

    m_stageLabel->setText(framesMeasured > 0 ? tr("Field measured")
                                             : tr("No result"));
    statusBar()->showMessage(
        framesMeasured > 0
            ? (planned == 1 ? tr("%1 points solved").arg(m_result.converged)
                            : tr("%1 frames measured").arg(framesMeasured))
            : tr("No point could be solved"),
        6000);

    if (framesMeasured > 0 && !m_hasResult && !cancelled) {
        QMessageBox::warning(
            this, tr("No result"),
            tr("The engine solved none of the %1 points. The log lists the "
               "reason it gave for each.")
                .arg(m_result.total()));
    }

    tearDownWorker();
}

void MainWindow::tearDownWorker()
{
    m_progress->hide();
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
    }
    if (m_runner) {
        m_runner->deleteLater();
        m_runner = nullptr;
    }
    updateActionStates();
}

void MainWindow::onCorrelationFailed(const QString &reason)
{
    log(tr("Correlation failed: %1").arg(reason));
    QMessageBox::warning(this, tr("Correlation failed"), reason);
    m_stageLabel->setText(tr("Correlation failed"));
    tearDownWorker();
}

void MainWindow::exportField()
{
    // Suggested from the images the field came from, so a folder of exports
    // stays attributable without anyone having to name them carefully.
    const QString suggested =
        QFileInfo(m_referenceRecord.filePath).absolutePath() + QLatin1Char('/')
        + QFileInfo(m_referenceRecord.fileName).completeBaseName()
        + QStringLiteral("_field.vtu");

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export measured field"), suggested,
        tr("VTK unstructured grid (*.vtu)"));
    if (path.isEmpty())
        return;

    exportFieldTo(path);
}

void MainWindow::exportFieldCsv()
{
    const QString suggested =
        QFileInfo(m_referenceRecord.filePath).absolutePath() + QLatin1Char('/')
        + QFileInfo(m_referenceRecord.fileName).completeBaseName()
        + QStringLiteral("_field.csv");

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export measured field as a table"), suggested,
        tr("Comma-separated values (*.csv)"));
    if (path.isEmpty())
        return;

    exportFieldCsvTo(path);
}

bool MainWindow::exportFieldTo(const QString &path)
{
    return exportFrames(path, FieldFormat::Vtu);
}

bool MainWindow::exportFieldCsvTo(const QString &path)
{
    return exportFrames(path, FieldFormat::Csv);
}

bool MainWindow::exportFrames(const QString &path, FieldFormat format)
{
    if (m_frames.isEmpty()) {
        QMessageBox::warning(this, tr("Nothing to export"),
                             tr("Run a correlation first: there is no measured "
                                "field to write."));
        return false;
    }

    const QFileInfo chosen(path);
    const QString folder = chosen.absolutePath();
    const QString stem = chosen.completeBaseName();
    const QString extension = format == FieldFormat::Vtu
                                  ? QStringLiteral(".vtu")
                                  : QStringLiteral(".csv");

    QStringList written;
    for (int frame = 0; frame < m_frames.size(); frame++) {
        // ⚑ One file per frame, NUMBERED AND PADDED. A single file holding the
        // last frame would silently discard everything the sequence was run
        // for, and ParaView groups a numbered series into a time series by the
        // names alone -- unpadded, frame 10 would sort before frame 2 and the
        // animation would play out of order, which is the same trap the frame
        // ordering itself had to solve.
        const QString framePath =
            m_frames.size() == 1
                ? folder + QLatin1Char('/') + stem + extension
                : folder + QLatin1Char('/') + stem
                      + QStringLiteral("_%1").arg(frame, 4, 10, QLatin1Char('0'))
                      + extension;

        const QString refusal =
            format == FieldFormat::Vtu
                ? writeFieldVtu(framePath, m_frames.at(frame).result,
                                m_frames.at(frame).provenance)
                : writeFieldCsv(framePath, m_frames.at(frame).result,
                                m_frames.at(frame).provenance);
        if (!refusal.isEmpty()) {
            // Stopped at the first failure rather than pressing on: whatever
            // stopped this frame stops the rest, and the files already written
            // are named so the user knows exactly how far it got.
            log(tr("Export failed on frame %1 of %2: %3")
                    .arg(frame + 1)
                    .arg(m_frames.size())
                    .arg(refusal));
            if (!written.isEmpty()) {
                log(tr("  %1 file(s) were written before that: %2")
                        .arg(written.size())
                        .arg(written.join(QStringLiteral(", "))));
            }
            QMessageBox::warning(this, tr("Export failed"), refusal);
            return false;
        }
        written << QFileInfo(framePath).fileName();
    }

    // Named in full, and in the log rather than only in a status message that
    // disappears: files written to a path chosen in a dialog that has since
    // closed are files the user cannot find again.
    if (written.size() == 1) {
        log(tr("Exported the measured field to %1 - %2 points, %3 of them "
               "solved%4")
                .arg(folder + QLatin1Char('/') + written.first())
                .arg(m_result.total())
                .arg(m_result.converged)
                .arg(m_result.hasStrain()
                         ? tr(", with strain at %1").arg(m_result.strainFitted)
                         : QString()));
    } else {
        log(tr("Exported %1 frames to %2, as %3 through %4")
                .arg(written.size())
                .arg(folder, written.first(), written.last()));
    }

    statusBar()->showMessage(written.size() == 1
                                 ? tr("Field exported to %1").arg(written.first())
                                 : tr("%1 frames exported").arg(written.size()),
                             6000);
    return true;
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
    statusBar()->showMessage(tr("%1 - not yet implemented").arg(feature), 4000);
}

void MainWindow::updateActionStates()
{
    const bool hasImage = m_viewport->hasImage();
    const bool running = m_workerThread != nullptr;
    const bool drawing = m_viewport->isDrawingRoi();
    const bool hasReference = m_referenceRecord.isValid();
    // Asked of the same planner the run itself uses, so Run cannot be enabled
    // for a project the run would then refuse, nor disabled for one it would
    // accept. Before this there were two answers to the same question.
    const bool hasPair =
        hasReference && !planSequence(m_referenceRecord, m_targetRecords).isEmpty();

    // While a boundary is being placed, the viewport's own bar is the control
    // surface. Leaving these live would offer two ways to decide the same
    // thing, one of which the user cannot see they are already inside.
    m_actDefineRoi->setEnabled(hasImage && !running && !drawing);
    m_actDefineRoi->setToolTip(
        drawing ? tr("A region is being defined - use the bar on the image")
                : (hasImage ? tr("Click corners on the image to enclose a region")
                            : tr("Import an image first")));

    m_actAutoRoi->setEnabled(hasImage && !running && !drawing);
    m_actAutoRoi->setToolTip(
        hasImage ? tr("Propose a region by segmenting the speckled area")
                 : tr("Import an image first"));

    m_actClearRoi->setEnabled(m_roi.isValid() && !running && !drawing);
    m_actClearRoi->setToolTip(m_roi.isValid()
                                  ? tr("Discard the region and measure the "
                                       "whole image")
                                  : tr("No region is defined"));

    // Run needs a reference AND a target that matches it. The tooltip says
    // which of the two is missing, so the disabled button explains itself
    // instead of merely refusing.
    m_actRun->setEnabled(hasPair && !running && !drawing);
    if (drawing)
        m_actRun->setToolTip(tr("Close or cancel the region being defined first"));
    else if (running)
        m_actRun->setToolTip(tr("A correlation is already running"));
    else if (!hasReference)
        m_actRun->setToolTip(tr("Import a reference image first"));
    else if (!hasPair)
        m_actRun->setToolTip(
            tr("Import a target image that matches the reference"));
    else
        m_actRun->setToolTip(tr("Run DIC correlation"));

    m_actStop->setEnabled(running);

    m_actExport->setEnabled(m_hasResult && !running);
    m_actExportCsv->setEnabled(m_hasResult && !running);
    m_actExportCsv->setToolTip(
        running       ? tr("Wait for the correlation to finish")
        : m_hasResult ? tr("Write the measured field as a plain table, with the "
                           "provenance in a commented header. Anything that was "
                           "not measured is an empty cell, never a zero")
                      : tr("Run a correlation first: there is no field to export"));
    m_actExport->setToolTip(
        running    ? tr("Wait for the correlation to finish")
        : m_hasResult ? tr("Write the measured field to a VTK .vtu file, which "
                           "ParaView and FreeCAD open directly")
                      : tr("Run a correlation first: there is no field to export"));
}
