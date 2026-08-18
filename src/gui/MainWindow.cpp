#include "MainWindow.h"

#include "ImageViewport.h"
#include "RecordPanel.h"
#include "core/Correlation.h"
#include "core/ImageDecode.h"
#include "core/ImagePairing.h"
#include "core/RoiDetect.h"

#include <QApplication>
#include <QEventLoop>
#include <QGuiApplication>
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
#include <QProgressBar>
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

enum RecordKind { None = 0, Reference = 1, Target = 2 };

}  // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("SurView DIC"));
    resize(1360, 860);

    m_viewport = new ImageViewport(this);
    setCentralWidget(m_viewport);

    connect(m_viewport, &ImageViewport::roiDrawn, this, &MainWindow::onRoiDrawn);
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

void MainWindow::createActions()
{
    // Placeholder actions (project I/O, export) report that they are not yet
    // wired rather than silently doing nothing -- an honest scaffold.
    m_actDefineRoi = new QAction(tr("Define ROI"), this);
    m_actDefineRoi->setStatusTip(
        tr("Draw the region of interest by clicking corners on the image"));
    connect(m_actDefineRoi, &QAction::triggered, this,
            [this] { m_viewport->beginRoiDrawing(); });

    m_actAutoRoi = new QAction(tr("Auto-detect ROI"), this);
    m_actAutoRoi->setStatusTip(
        tr("Propose a region by segmenting the speckled area of the image"));
    connect(m_actAutoRoi, &QAction::triggered, this, &MainWindow::detectRoi);

    m_actClearRoi = new QAction(tr("Clear ROI"), this);
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

    addDock(tr("Analysis"), createAnalysisPanel(), Qt::RightDockWidgetArea);
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
    auto *form = new QFormLayout(panel);

    m_solver = new QComboBox;
    m_solver->addItem(tr("ICGN"), CorrelationSettings::ICGN);
    m_solver->addItem(tr("Newton–Raphson"), CorrelationSettings::NewtonRaphson);
    m_solver->addItem(tr("IC-LM"), CorrelationSettings::ICLM);
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

    updateSolverConstraints();
    return panel;
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
    return settings;
}

int MainWindow::firstUsableTarget() const
{
    for (int i = 0; i < m_targetRecords.size(); i++) {
        const ImageRecord &target = m_targetRecords.at(i);
        if (target.isValid()
            && compareToReference(m_referenceRecord, target).matches()) {
            return i;
        }
    }
    return -1;
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

    for (const QString &path : paths) {
        const QString name = QFileInfo(path).fileName();

        ImageRecord record;
        const bool decoded = decodeImage(path, record) != nullptr;

        auto *item = new QTreeWidgetItem(m_targetsItem);
        item->setData(0, kRecordKindRole, RecordKind::Target);
        item->setData(0, kRecordIndexRole, int(m_targetRecords.size()));
        m_targetRecords.append(record);

        if (!decoded) {
            // Kept in the list rather than dropped: it was named as a target,
            // and silently omitting it would leave the count disagreeing with
            // what was selected.
            ++unreadable;
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
    }

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

    const int targetIndex = firstUsableTarget();
    if (targetIndex < 0) {
        QMessageBox::warning(
            this, tr("Nothing to correlate"),
            tr("No imported target image matches the reference. Correlation "
               "compares the same pixel grid before and after deformation."));
        return;
    }

    const CorrelationSettings settings = currentSettings();
    if (!settings.isAvailable()) {
        QMessageBox::warning(this, tr("Settings not available"),
                             settings.unavailableReason());
        return;
    }

    const ImageRecord &target = m_targetRecords.at(targetIndex);

    // Which target was used is part of the result. With several imported, a run
    // that silently picked one would leave the field unattributable.
    log(tr("Correlating %1 against %2 - %3, %4, subset radius %5 px, grid step %6 px, %7")
            .arg(m_referenceRecord.fileName, target.fileName,
                 m_solver->currentText(), m_shape->currentText())
            .arg(settings.subsetRadius)
            .arg(settings.gridStep)
            .arg(m_roi.isValid()
                     ? tr("inside the region of interest (%1)")
                           .arg(m_roi.originText())
                     : tr("whole image")));

    m_hasResult = false;
    m_viewport->clearField();

    m_workerThread = new QThread(this);
    m_runner = new CorrelationRunner(settings, m_roi,
                                     m_referenceRecord.filePath,
                                     target.filePath);
    m_runner->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::started, m_runner, &CorrelationRunner::run);
    connect(m_runner, &CorrelationRunner::progress, this,
            &MainWindow::onCorrelationProgress);
    connect(m_runner, &CorrelationRunner::finished, this,
            &MainWindow::onCorrelationFinished);
    connect(m_runner, &CorrelationRunner::failed, this,
            &MainWindow::onCorrelationFailed);

    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->show();
    m_stageLabel->setText(tr("Correlating"));

    m_workerThread->start();
    updateActionStates();
}

void MainWindow::stopCorrelation()
{
    if (!m_runner)
        return;
    // The engine cannot be interrupted mid-call, so this takes effect at the
    // next chunk boundary rather than instantly. Whatever was measured before
    // that point is kept and reported as partial.
    log(tr("Stop requested - finishing the current block."));
    m_runner->cancel();
    m_actStop->setEnabled(false);
}

void MainWindow::onCorrelationProgress(int done, int total, const QString &stage)
{
    if (total <= 0)
        return;
    m_progress->setValue(int(100.0 * done / total));
    statusBar()->showMessage(tr("%1 - %2 of %3 points")
                                 .arg(stage)
                                 .arg(done)
                                 .arg(total));
}

void MainWindow::onCorrelationFinished(const CorrelationResult &result)
{
    m_result = result;
    m_hasResult = result.converged > 0;

    if (m_hasResult)
        m_viewport->showField(result);

    const double share =
        result.total() > 0 ? 100.0 * result.converged / result.total() : 0.0;

    log(result.cancelled
            ? tr("Correlation stopped after %1 s - %2 of %3 points solved "
                 "(%4%) before stopping")
                  .arg(result.secondsElapsed, 0, 'f', 1)
                  .arg(result.converged)
                  .arg(result.total())
                  .arg(share, 0, 'f', 1)
            : tr("Correlation finished in %1 s - %2 of %3 points solved (%4%)")
                  .arg(result.secondsElapsed, 0, 'f', 1)
                  .arg(result.converged)
                  .arg(result.total())
                  .arg(share, 0, 'f', 1));

    // What the totals above are counted over. "8261 of 8700 solved" says
    // something different when those 8700 were a chosen region than when they
    // were the whole image, and the number alone does not carry that.
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

    // The tree said "Results -- none" through the first working run. A field
    // that exists but is not named in the project is the same defect as a
    // target image we claimed to have added and never read.
    double lowest = 0.0;
    double highest = 0.0;
    if (m_hasResult && result.magnitudeRange(lowest, highest)) {
        const QString summary =
            tr("Displacement field - %1 of %2 points%3, %4 to %5 px")
                .arg(result.converged)
                .arg(result.total())
                .arg(result.restrictedToRoi ? tr(" in the region of interest")
                                            : QString())
                .arg(lowest, 0, 'f', 2)
                .arg(highest, 0, 'f', 2);
        m_resultsItem->setText(0, summary);
        // The panel is narrow enough to clip the range off the end.
        m_resultsItem->setToolTip(0, summary);
    } else {
        m_resultsItem->setText(0, tr("Results - none"));
        m_resultsItem->setToolTip(0, QString());
    }

    m_stageLabel->setText(m_hasResult ? tr("Field measured") : tr("No result"));
    statusBar()->showMessage(
        m_hasResult ? tr("%1 points solved").arg(result.converged)
                    : tr("No point could be solved"),
        6000);

    if (!m_hasResult && !result.cancelled) {
        QMessageBox::warning(
            this, tr("No result"),
            tr("The engine solved none of the %1 points. The log lists the "
               "reason it gave for each.")
                .arg(result.total()));
    }

    m_progress->hide();
    m_workerThread->quit();
    m_workerThread->wait();
    m_runner->deleteLater();
    m_runner = nullptr;
    m_workerThread->deleteLater();
    m_workerThread = nullptr;
    updateActionStates();
}

void MainWindow::onCorrelationFailed(const QString &reason)
{
    log(tr("Correlation failed: %1").arg(reason));
    QMessageBox::warning(this, tr("Correlation failed"), reason);

    m_progress->hide();
    m_stageLabel->setText(tr("Correlation failed"));
    m_workerThread->quit();
    m_workerThread->wait();
    m_runner->deleteLater();
    m_runner = nullptr;
    m_workerThread->deleteLater();
    m_workerThread = nullptr;
    updateActionStates();
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
    const bool hasPair = hasReference && firstUsableTarget() >= 0;

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

    // Export needs results, and writing them is not built yet.
    m_actExport->setEnabled(false);
    m_actExport->setToolTip(m_hasResult
                                ? tr("Exporting results is not implemented yet")
                                : tr("Run a correlation first"));
}
