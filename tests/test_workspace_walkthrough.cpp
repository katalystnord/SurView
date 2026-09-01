// The walkthrough suite: the real MainWindow, driven the way a user drives it.
//
// ⚑ THE RULE THIS SUITE IS WRITTEN UNDER. A walkthrough test may only do what
// something on screen tells it to do. If a step needs a coordinate, an order or
// a precondition that no visible text describes, that is a UI defect found at
// the moment the test is written -- not a detail of the test. Several
// assertions below therefore check the WORDING on screen before acting on it:
// they are the "could Parallel Universe David do this?" question turned into a
// constraint the suite has to satisfy.
//
// These need a display: the viewport is a real VTK OpenGL widget, and mapping a
// click to a pixel goes through the renderer's own projection. tools/run-tests.sh
// provides one.

#include "core/Correlation.h"
#include "core/FieldLayout.h"
#include "core/PointReadout.h"
#include "core/Sequence.h"
#include "core/Roi.h"
#include "gui/ImageViewport.h"
#include "gui/PointPanel.h"
#include "gui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QToolBar>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QFormLayout>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTreeWidget>

#include <vtkFieldData.h>
#include <vtkNew.h>
#include <vtkStringArray.h>
#include <vtkUnstructuredGrid.h>
#include <vtkXMLUnstructuredGridReader.h>

namespace {

QString fixture(const QString &name)
{
    return QStringLiteral(SURVIEW_TEST_FIXTURES "/") + name;
}

// Find a control the way a user finds it: by the text it shows.
template <typename T>
T *byVisibleText(QWidget *root, const QString &text)
{
    for (T *candidate : root->findChildren<T *>()) {
        if (candidate->text().contains(text, Qt::CaseInsensitive))
            return candidate;
    }
    return nullptr;
}

// An action reachable from the MENU BAR, as opposed to one that merely exists
// as a child of the window.
//
// ⚑ The difference is the whole rule. findChildren<QAction*> finds an action
// that was constructed and then added to nothing, which is a capability no user
// can reach -- and a negative check proved that is not hypothetical: removing
// the CSV export from the File menu left its case green.
QAction *menuActionLabelled(QMainWindow *window, const QString &text)
{
    for (QAction *menuAction : window->menuBar()->actions()) {
        QMenu *menu = menuAction->menu();
        if (!menu)
            continue;
        for (QAction *action : menu->actions()) {
            if (action->text().contains(text, Qt::CaseInsensitive))
                return action;
        }
    }
    return nullptr;
}

QAction *actionLabelled(QWidget *root, const QString &text)
{
    for (QAction *action : root->findChildren<QAction *>()) {
        if (action->text().contains(text, Qt::CaseInsensitive))
            return action;
    }
    return nullptr;
}

// The project tree line whose text starts with a given heading.
QString projectLine(QWidget *root, const QString &heading)
{
    for (QTreeWidget *tree : root->findChildren<QTreeWidget *>()) {
        for (int i = 0; i < tree->topLevelItemCount(); i++) {
            const QString text = tree->topLevelItem(i)->text(0);
            if (text.startsWith(heading))
                return text;
        }
    }
    return QString();
}

// The control on the row a given label names. Found through the visible label
// rather than by object name, so the test cannot reach a control the user
// would have no way to identify.
template <typename T>
T *controlLabelled(QWidget *root, const QString &labelText)
{
    for (QFormLayout *form : root->findChildren<QFormLayout *>()) {
        for (int row = 0; row < form->rowCount(); row++) {
            QLayoutItem *labelItem = form->itemAt(row, QFormLayout::LabelRole);
            QLayoutItem *fieldItem = form->itemAt(row, QFormLayout::FieldRole);
            if (!labelItem || !fieldItem)
                continue;
            auto *label = qobject_cast<QLabel *>(labelItem->widget());
            if (!label || !label->text().contains(labelText, Qt::CaseInsensitive))
                continue;
            if (auto *control = qobject_cast<T *>(fieldItem->widget()))
                return control;
        }
    }
    return nullptr;
}

// Any visible label under `root` whose text contains `text`. Used to assert
// that a condition is EXPLAINED on screen, not merely handled.
bool somethingOnScreenSays(QWidget *root, const QString &text)
{
    for (QLabel *label : root->findChildren<QLabel *>()) {
        if (label->isVisible() && label->text().contains(text, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

// Everything an exported file states about how it was made, as one string.
QString provenanceIn(const QString &path)
{
    vtkNew<vtkXMLUnstructuredGridReader> reader;
    reader->SetFileName(path.toLocal8Bit().constData());
    reader->Update();

    QString all;
    vtkFieldData *data = reader->GetOutput()->GetFieldData();
    for (int i = 0; i < data->GetNumberOfArrays(); i++) {
        auto *array = vtkStringArray::SafeDownCast(data->GetAbstractArray(i));
        if (!array)
            continue;
        for (vtkIdType v = 0; v < array->GetNumberOfValues(); v++)
            all += QString::fromStdString(array->GetValue(v)) + QLatin1Char('\n');
    }
    return all;
}

// Mean measured displacement along x over the solved points of one frame.
// Lives here rather than on CorrelationResult: it is how these cases tell one
// frame from another, not something the application itself needs.
double meanU(const CorrelationResult &result)
{
    double sum = 0.0;
    int counted = 0;
    for (const CorrelationPoint &point : result.points) {
        if (!point.converged)
            continue;
        sum += point.u;
        counted++;
    }
    return counted > 0 ? sum / counted : 0.0;
}

// Everything the point readout panel currently says, as one string. Read from
// the visible labels rather than from the panel's own model, so the case is
// asserting what reaches a reader.
QString pointPanelText(QWidget *root)
{
    auto *panel = root->findChild<PointPanel *>();
    if (!panel)
        return QString();
    QStringList parts;
    for (QLabel *label : panel->findChildren<QLabel *>()) {
        if (label->isVisible())
            parts << label->text();
    }
    return parts.join(QLatin1Char('\n'));
}

// Where a given image pixel currently sits inside the viewport widget.
//
// This asks the viewport through its real projection rather than recomputing
// the camera's framing here. A first version of this helper DID recompute it,
// assuming ResetCamera frames the image exactly; it does not -- it leaves a
// margin -- and the test failed by 16 px while the application was correct.
// A test that re-derives the code's own arithmetic tests the copy.
//
// Aiming through the projection means this helper cannot, on its own, prove the
// frame is the right way up -- so that is proved separately and independently,
// by moving_down_and_right_on_screen_moves_down_and_right_in_the_image below.
QPoint widgetPointForPixel(ImageViewport *viewport, double px, double py)
{
    QPointF position;
    if (!viewport->widgetPositionForImagePixel(QPointF(px, py), position))
        return QPoint(-1, -1);
    return position.toPoint();
}

}  // namespace

class TestWorkspaceWalkthrough : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void the_pipeline_controls_say_what_they_do_before_anything_is_loaded();
    void a_region_cannot_be_started_before_there_is_an_image_to_draw_on();
    void entering_the_mode_puts_its_own_instructions_on_screen();
    void the_mode_bar_names_every_way_to_finish_it();
    void closing_is_refused_until_the_bar_says_it_is_ready();
    void moving_down_and_right_on_screen_moves_down_and_right_in_the_image();
    void a_region_drawn_on_screen_is_reported_in_the_project();
    void cancelling_keeps_the_region_that_was_already_in_force();
    void a_correlation_inside_a_region_recovers_a_known_shift();

    void the_panel_warns_when_the_strain_subregion_cannot_hold_the_fit();
    void a_measured_field_can_be_switched_to_strain_from_the_screen();
    void the_strain_channels_say_why_they_are_unavailable();
    void exporting_is_refused_with_a_reason_until_there_is_a_field();
    void a_measured_field_leaves_the_application_and_says_where_it_went();
    void an_export_records_the_settings_that_produced_it_not_the_ones_on_screen();
    void the_reliability_of_a_field_is_reachable_and_qualified_on_screen();

    void targets_are_listed_in_frame_order_not_the_order_they_were_chosen();
    void every_frame_of_a_sequence_is_measured_and_listed();
    void a_frame_can_be_picked_from_the_project_and_shows_its_own_field();
    void exporting_a_sequence_writes_one_numbered_file_for_each_frame();
    void re_anchoring_is_offered_off_by_default_and_says_what_it_costs();

    void the_point_panel_says_how_to_read_a_field_before_one_exists();
    void pointing_at_a_measured_point_reads_out_what_it_measured();
    void a_pinned_point_stays_on_screen_when_the_pointer_moves_away();
    void pointing_away_from_the_field_says_so_rather_than_going_blank();

    void a_measured_field_can_also_leave_as_a_table_anything_opens();
    void the_first_screen_shows_the_whole_path_not_only_its_first_step();
    void the_toolbar_carries_an_icon_beside_every_name();
    void exporting_a_sequence_as_tables_numbers_them_and_keeps_the_extension();
};

void TestWorkspaceWalkthrough::initTestCase()
{
    QVERIFY2(qApp, "the walkthrough needs a QApplication");
}

void TestWorkspaceWalkthrough::the_pipeline_controls_say_what_they_do_before_anything_is_loaded()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // Every capability has to be visible; nothing may exist only as a shortcut.
    QVERIFY(actionLabelled(&window, QStringLiteral("Define ROI")));
    QVERIFY(actionLabelled(&window, QStringLiteral("Auto-detect ROI")));
    QVERIFY(actionLabelled(&window, QStringLiteral("Clear ROI")));
    QVERIFY(actionLabelled(&window, QStringLiteral("Run Correlation")));

    // A disabled control must explain itself rather than merely refuse.
    QAction *run = actionLabelled(&window, QStringLiteral("Run Correlation"));
    QVERIFY(!run->isEnabled());
    QVERIFY(!run->toolTip().isEmpty());
    QVERIFY(run->toolTip().contains(QStringLiteral("reference"), Qt::CaseInsensitive));

    // And the project must say there is no region, not stay silent about it.
    QVERIFY(projectLine(&window, QStringLiteral("Region of interest"))
                .contains(QStringLiteral("none")));
}

void TestWorkspaceWalkthrough::a_region_cannot_be_started_before_there_is_an_image_to_draw_on()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QAction *define = actionLabelled(&window, QStringLiteral("Define ROI"));
    QVERIFY(!define->isEnabled());
    QVERIFY(define->toolTip().contains(QStringLiteral("image"), Qt::CaseInsensitive));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));
    QVERIFY(define->isEnabled());
}

void TestWorkspaceWalkthrough::entering_the_mode_puts_its_own_instructions_on_screen()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));

    auto *viewport = window.findChild<ImageViewport *>();
    QVERIFY(viewport);
    QVERIFY(!viewport->isDrawingRoi());

    actionLabelled(&window, QStringLiteral("Define ROI"))->trigger();
    QVERIFY(viewport->isDrawingRoi());

    // The mode must announce itself. A mode you cannot see you are in is the
    // failure this bar exists to prevent.
    QLabel *instructions =
        byVisibleText<QLabel>(viewport, QStringLiteral("Defining a region"));
    QVERIFY2(instructions, "entering ROI mode showed no on-screen instruction");
    QVERIFY(instructions->isVisible());
    QVERIFY(instructions->text().contains(QStringLiteral("click"), Qt::CaseInsensitive));
}

void TestWorkspaceWalkthrough::the_mode_bar_names_every_way_to_finish_it()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));

    auto *viewport = window.findChild<ImageViewport *>();
    actionLabelled(&window, QStringLiteral("Define ROI"))->trigger();

    // Buttons, not shortcuts. If the keyboard were the only way out, a user who
    // was never told would be stuck in a mode with no visible exit.
    QVERIFY2(byVisibleText<QPushButton>(viewport, QStringLiteral("Close region")),
             "no visible control to finish the boundary");
    QVERIFY2(byVisibleText<QPushButton>(viewport, QStringLiteral("Cancel")),
             "no visible control to abandon the boundary");
    QVERIFY2(byVisibleText<QPushButton>(viewport, QStringLiteral("Undo")),
             "no visible control to take back a corner");

    // The keyboard routes are accelerators, and are named on screen so they can
    // be discovered rather than guessed.
    QLabel *instructions =
        byVisibleText<QLabel>(viewport, QStringLiteral("Defining a region"));
    QVERIFY(instructions->text().contains(QStringLiteral("Enter")));
    QVERIFY(instructions->text().contains(QStringLiteral("Esc")));
}

void TestWorkspaceWalkthrough::closing_is_refused_until_the_bar_says_it_is_ready()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));

    auto *viewport = window.findChild<ImageViewport *>();
    actionLabelled(&window, QStringLiteral("Define ROI"))->trigger();

    QPushButton *close = byVisibleText<QPushButton>(viewport, QStringLiteral("Close region"));
    QLabel *instructions = byVisibleText<QLabel>(viewport, QStringLiteral("Defining a region"));

    // Refusing is not enough on its own: the reason has to be readable.
    QVERIFY(!close->isEnabled());
    QVERIFY(instructions->text().contains(QStringLiteral("at least 3")));

    for (const QPoint &pixel : {QPoint(40, 40), QPoint(180, 40), QPoint(180, 120)}) {
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier,
                          widgetPointForPixel(viewport, pixel.x(), pixel.y()));
    }

    QVERIFY(close->isEnabled());
    QVERIFY(instructions->text().contains(QStringLiteral("ready to close")));
}

void TestWorkspaceWalkthrough::moving_down_and_right_on_screen_moves_down_and_right_in_the_image()
{
    // ⚑ The independent orientation check. Everything else in this file aims
    // through the viewport's own projection, so a mirrored or transposed frame
    // would move the aim and the target together and stay green. This case
    // assumes NOTHING about scale, margin or centring: it clicks two points a
    // fixed distance apart on screen and asserts only the direction of travel.
    //
    // It exists because that exact bug was real. TIFFs were displayed
    // vertically mirrored, and a measured field agreed with the photograph only
    // for one file format, by accident.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));

    auto *viewport = window.findChild<ImageViewport *>();
    QSignalSpy drawn(viewport, &ImageViewport::roiDrawn);

    // Three corners, placed by pure widget offsets from the widget's centre.
    const QPoint centre(viewport->width() / 2, viewport->height() / 2);
    const QPoint downRight = centre + QPoint(60, 40);

    actionLabelled(&window, QStringLiteral("Define ROI"))->trigger();
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, centre);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, downRight);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier,
                      centre + QPoint(0, 40));
    byVisibleText<QPushButton>(viewport, QStringLiteral("Close region"))->click();

    QCOMPARE(drawn.count(), 1);
    const auto roi = drawn.first().first().value<RegionOfInterest>();
    QCOMPARE(roi.vertices.size(), 3);

    const QPoint first = roi.vertices.at(0);
    const QPoint second = roi.vertices.at(1);

    // Right on screen must be a LARGER x in the image.
    QVERIFY2(second.x() > first.x(),
             qPrintable(QStringLiteral("moving right on screen gave x %1 then %2")
                            .arg(first.x()).arg(second.x())));
    // Down on screen must be a LARGER y in the image -- the assertion a
    // vertically mirrored frame fails.
    QVERIFY2(second.y() > first.y(),
             qPrintable(QStringLiteral("moving down on screen gave y %1 then %2")
                            .arg(first.y()).arg(second.y())));

    // And the horizontal move must not have leaked into the vertical axis, nor
    // the reverse: a transposed frame passes both tests above.
    const QPoint third = roi.vertices.at(2);
    QCOMPARE(third.x(), first.x());
    QVERIFY(third.y() > first.y());
}

void TestWorkspaceWalkthrough::a_region_drawn_on_screen_is_reported_in_the_project()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));

    auto *viewport = window.findChild<ImageViewport *>();
    QSignalSpy drawn(viewport, &ImageViewport::roiDrawn);

    actionLabelled(&window, QStringLiteral("Define ROI"))->trigger();
    for (const QPoint &pixel : {QPoint(40, 40), QPoint(180, 40),
                                QPoint(180, 120), QPoint(40, 120)}) {
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier,
                          widgetPointForPixel(viewport, pixel.x(), pixel.y()));
    }
    byVisibleText<QPushButton>(viewport, QStringLiteral("Close region"))->click();

    QCOMPARE(drawn.count(), 1);
    QVERIFY(!viewport->isDrawingRoi());

    const auto roi = drawn.first().first().value<RegionOfInterest>();
    QCOMPARE(roi.vertices.size(), 4);
    QCOMPARE(roi.origin, RegionOfInterest::Drawn);

    // The corners must land where they were aimed. A few pixels of slack for
    // the widget-to-pixel rounding; a mirrored or transposed frame would be off
    // by the whole image, not by two.
    const QRect box = roi.bounds();
    QVERIFY2(qAbs(box.left() - 40) <= 3, qPrintable(QString::number(box.left())));
    QVERIFY2(qAbs(box.top() - 40) <= 3, qPrintable(QString::number(box.top())));
    QVERIFY2(qAbs(box.right() - 180) <= 3, qPrintable(QString::number(box.right())));
    QVERIFY2(qAbs(box.bottom() - 120) <= 3, qPrintable(QString::number(box.bottom())));

    // And the project has to say so, in words, without being asked.
    const QString line = projectLine(&window, QStringLiteral("Region of interest"));
    QVERIFY(!line.contains(QStringLiteral("none")));
    QVERIFY(line.contains(QStringLiteral("4")));
    QVERIFY(line.contains(QStringLiteral("drawn by hand")));

    // Clearing it is offered only once there is something to clear.
    QAction *clear = actionLabelled(&window, QStringLiteral("Clear ROI"));
    QVERIFY(clear->isEnabled());
    clear->trigger();
    QVERIFY(projectLine(&window, QStringLiteral("Region of interest"))
                .contains(QStringLiteral("none")));
    QVERIFY(!clear->isEnabled());
}

void TestWorkspaceWalkthrough::cancelling_keeps_the_region_that_was_already_in_force()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));

    auto *viewport = window.findChild<ImageViewport *>();

    // Draw one and keep it.
    actionLabelled(&window, QStringLiteral("Define ROI"))->trigger();
    for (const QPoint &pixel : {QPoint(40, 40), QPoint(180, 40), QPoint(180, 120)}) {
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier,
                          widgetPointForPixel(viewport, pixel.x(), pixel.y()));
    }
    byVisibleText<QPushButton>(viewport, QStringLiteral("Close region"))->click();
    const QString kept = projectLine(&window, QStringLiteral("Region of interest"));
    QVERIFY(!kept.contains(QStringLiteral("none")));

    // Start another, then abandon it. Abandoning a NEW boundary must not
    // discard the one already in force -- that would lose work the user never
    // asked to lose.
    actionLabelled(&window, QStringLiteral("Define ROI"))->trigger();
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier,
                      widgetPointForPixel(viewport, 60, 60));
    byVisibleText<QPushButton>(viewport, QStringLiteral("Cancel"))->click();

    QVERIFY(!viewport->isDrawingRoi());
    QCOMPARE(projectLine(&window, QStringLiteral("Region of interest")), kept);
}

void TestWorkspaceWalkthrough::a_correlation_inside_a_region_recovers_a_known_shift()
{
    // End to end, against an answer known independently of the code: the target
    // fixture IS the reference displaced by exactly +3 px in x and 0 in y.
    // Anything that transposes, mirrors or mis-scales the frame between the
    // screen, the region and the engine shows up here as the wrong number.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));
    window.addTargetImages({fixture(QStringLiteral("shift_target.tif"))});

    auto *viewport = window.findChild<ImageViewport *>();
    actionLabelled(&window, QStringLiteral("Define ROI"))->trigger();
    for (const QPoint &pixel : {QPoint(60, 50), QPoint(170, 50),
                                QPoint(170, 110), QPoint(60, 110)}) {
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier,
                          widgetPointForPixel(viewport, pixel.x(), pixel.y()));
    }
    byVisibleText<QPushButton>(viewport, QStringLiteral("Close region"))->click();

    QAction *run = actionLabelled(&window, QStringLiteral("Run Correlation"));
    QVERIFY2(run->isEnabled(), "a matching target and a region were not enough to run");
    run->trigger();

    // The run is on a worker thread; wait for the field to appear.
    QVERIFY2(QTest::qWaitFor([viewport] { return viewport->hasField(); }, 120000),
             "the correlation produced no field within two minutes");

    const CorrelationResult &result = window.lastResult();
    QVERIFY(result.restrictedToRoi);
    QVERIFY2(result.converged > 100,
             qPrintable(QStringLiteral("only %1 points solved").arg(result.converged)));

    // Every solved point measures the same rigid shift, so the mean is the
    // measurement and the spread is the noise on it.
    double sumU = 0.0, sumV = 0.0;
    int counted = 0;
    for (const CorrelationPoint &point : result.points) {
        if (!point.converged)
            continue;
        sumU += point.u;
        sumV += point.v;
        counted++;
    }
    QVERIFY(counted > 0);
    const double meanU = sumU / counted;
    const double meanV = sumV / counted;

    QVERIFY2(qAbs(meanU - 3.0) < 0.1,
             qPrintable(QStringLiteral("mean u was %1, expected 3").arg(meanU)));
    QVERIFY2(qAbs(meanV) < 0.1,
             qPrintable(QStringLiteral("mean v was %1, expected 0").arg(meanV)));

    // And the points must sit inside the region that was drawn, not over the
    // whole picture. Checked against the region the project actually recorded,
    // so this asserts the restriction held rather than re-asserting where the
    // clicks landed.
    const QRect box = window.roi().bounds();
    QVERIFY(box.width() < 200);          // a real restriction, not the whole image
    for (const CorrelationPoint &point : result.points) {
        QVERIFY(point.x >= box.left() && point.x <= box.right());
        QVERIFY(point.y >= box.top() && point.y <= box.bottom());
    }
}


void TestWorkspaceWalkthrough::the_panel_warns_when_the_strain_subregion_cannot_hold_the_fit()
{
    // The condition this covers is invisible by construction: the engine does
    // not refuse a subregion too small for its minimum, it quietly fits over
    // the nearest points instead. If the panel does not say so while the
    // numbers are being chosen, nothing ever does.
    //
    // NEGATIVE CHECK (2026-08-19): with the advice label forced hidden, this
    // failed on "a strain subregion too small for its minimum drew no warning".
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *strainRadius =
        controlLabelled<QDoubleSpinBox>(&window, QStringLiteral("Subregion radius"));
    auto *strainMinimum =
        controlLabelled<QSpinBox>(&window, QStringLiteral("Fewest points"));
    auto *gridStep = controlLabelled<QSpinBox>(&window, QStringLiteral("Grid step"));
    QVERIFY2(strainRadius && strainMinimum && gridStep,
             "the Analysis panel has no strain controls to set");

    // Defaults must be usable without adjustment, or the first run of every
    // new project starts on a warning.
    QVERIFY2(!somethingOnScreenSays(&window, QStringLiteral("nearest")),
             "the panel's own default strain settings draw a warning");

    // Now ask for something the grid cannot supply.
    gridStep->setValue(20);
    strainRadius->setValue(20.0);
    strainMinimum->setValue(30);

    QVERIFY2(somethingOnScreenSays(&window, QStringLiteral("nearest")),
             "a strain subregion too small for its minimum drew no warning");
    QVERIFY2(somethingOnScreenSays(&window, QStringLiteral("best case")),
             "the warning did not say the count is for a point with grid all around it");

    // And it goes away again when the settings become satisfiable, rather than
    // staying up as permanent background noise.
    strainMinimum->setValue(4);
    QVERIFY2(!somethingOnScreenSays(&window, QStringLiteral("nearest")),
             "the warning stayed up after the settings were made satisfiable");
}

void TestWorkspaceWalkthrough::a_measured_field_can_be_switched_to_strain_from_the_screen()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));
    window.addTargetImages({fixture(QStringLiteral("shift_target.tif"))});

    auto *viewport = window.findChild<ImageViewport *>();

    // A coarser grid than the default, so this stays a UI test rather than a
    // two-minute correlation. Set through the same controls a user would.
    controlLabelled<QSpinBox>(&window, QStringLiteral("Grid step"))->setValue(12);

    actionLabelled(&window, QStringLiteral("Run Correlation"))->trigger();
    QVERIFY2(QTest::qWaitFor([viewport] { return viewport->hasField(); }, 120000),
             "the correlation produced no field within two minutes");

    // What is on screen must name the channel being shown. Without that, a
    // strain map and a displacement map are the same picture in the same
    // colours meaning entirely different numbers.
    auto *choice = viewport->findChild<QComboBox *>();
    QVERIFY2(choice, "the field on screen offers no way to see which channel it is");
    QVERIFY2(choice->isVisible(), "the field channel selector is not visible");
    QCOMPARE(viewport->fieldChannel(), FieldChannel::DisplacementMagnitude);
    QVERIFY(choice->currentText().contains(QStringLiteral("Displacement")));

    // Switch to strain the way a user does: by picking its name from the list.
    const int strainIndex = choice->findText(fieldChannelName(FieldChannel::StrainXX));
    QVERIFY2(strainIndex >= 0, "the selector does not offer the strain channel");
    choice->setCurrentIndex(strainIndex);

    QCOMPARE(viewport->fieldChannel(), FieldChannel::StrainXX);
    QVERIFY(viewport->hasField());

    // And the run really did fit strain, so this is a channel with something
    // in it rather than an empty overlay the selector was happy to switch to.
    QVERIFY(window.lastResult().hasStrain());
}

void TestWorkspaceWalkthrough::the_strain_channels_say_why_they_are_unavailable()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));
    window.addTargetImages({fixture(QStringLiteral("shift_target.tif"))});

    controlLabelled<QSpinBox>(&window, QStringLiteral("Grid step"))->setValue(12);

    // Turn strain off through the visible control.
    auto *strainOn = byVisibleText<QCheckBox>(&window, QStringLiteral("Fit strain"));
    QVERIFY2(strainOn, "there is no visible control for whether strain is fitted");
    strainOn->setChecked(false);

    auto *viewport = window.findChild<ImageViewport *>();
    actionLabelled(&window, QStringLiteral("Run Correlation"))->trigger();
    QVERIFY2(QTest::qWaitFor([viewport] { return viewport->hasField(); }, 120000),
             "the correlation produced no field within two minutes");

    QVERIFY(!window.lastResult().hasStrain());

    // The strain entries must be unselectable AND accounted for. A control
    // that is merely greyed out cannot say whether it is broken, not yet
    // reached, or not applicable.
    //
    // NEGATIVE CHECK (2026-08-19): with the entries left enabled, this failed
    // on "a strain channel was selectable after a run that fitted no strain".
    auto *choice = viewport->findChild<QComboBox *>();
    QVERIFY(choice);
    const int strainIndex = choice->findText(fieldChannelName(FieldChannel::StrainXX));
    QVERIFY(strainIndex >= 0);
    QVERIFY2(!(choice->model()->flags(choice->model()->index(strainIndex, 0))
               & Qt::ItemIsEnabled),
             "a strain channel was selectable after a run that fitted no strain");
    QVERIFY2(somethingOnScreenSays(viewport, QStringLiteral("Strain was not fitted")),
             "nothing on screen says why the strain channels cannot be picked");
}


void TestWorkspaceWalkthrough::exporting_is_refused_with_a_reason_until_there_is_a_field()
{
    // The menu item existed for weeks and answered "not implemented yet" when
    // clicked, which is the worst of the three possible states: it looks like
    // a capability, behaves like a bug, and teaches the reader to distrust the
    // rest of the menu. Disabled with a stated reason is the honest version.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QAction *exportAction =
        actionLabelled(&window, QStringLiteral("Export Results (.vtu)"));
    QVERIFY2(exportAction, "there is no way to get results out of the application");
    QVERIFY(!exportAction->isEnabled());
    QVERIFY2(exportAction->toolTip().contains(QStringLiteral("correlation"),
                                              Qt::CaseInsensitive),
             "the disabled export does not say what is missing");

    // NEGATIVE CHECK (2026-08-19): this case does NOT catch the export being
    // unbuilt, and it was written believing it did. With the action forced
    // disabled and its tooltip restored to "not implemented yet", it still
    // passed -- because before a run the tooltip takes the "run a correlation
    // first" branch either way. The assertion that has teeth against that
    // state lives in the case below, where a result exists.
}

void TestWorkspaceWalkthrough::a_measured_field_leaves_the_application_and_says_where_it_went()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));
    window.addTargetImages({fixture(QStringLiteral("shift_target.tif"))});
    controlLabelled<QSpinBox>(&window, QStringLiteral("Grid step"))->setValue(12);

    auto *viewport = window.findChild<ImageViewport *>();
    actionLabelled(&window, QStringLiteral("Run Correlation"))->trigger();
    QVERIFY2(QTest::qWaitFor([viewport] { return viewport->hasField(); }, 120000),
             "the correlation produced no field within two minutes");

    QAction *exportAction = actionLabelled(&window, QStringLiteral("Export"));
    QVERIFY2(exportAction->isEnabled(),
             "a measured field still could not be exported");
    QVERIFY2(!exportAction->toolTip().contains(QStringLiteral("not implemented"),
                                               Qt::CaseInsensitive),
             "the export still describes itself as unbuilt");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("walkthrough.vtu"));
    QVERIFY2(window.exportFieldTo(path), "the export reported failure");
    QVERIFY2(QFile::exists(path), "the export reported success and wrote nothing");
    QVERIFY(QFileInfo(path).size() > 0);

    // Where it went has to be visible afterwards. A file written silently to a
    // path chosen in a dialog that has since closed is a file the user cannot
    // find again.
    auto *log = window.findChild<QPlainTextEdit *>();
    QVERIFY(log);
    QVERIFY2(log->toPlainText().contains(QStringLiteral("walkthrough.vtu")),
             "nothing on screen says where the export went");
}


void TestWorkspaceWalkthrough::an_export_records_the_settings_that_produced_it_not_the_ones_on_screen()
{
    // The Analysis panel keeps taking input after a run finishes, so the
    // settings on screen at export time are not necessarily the ones that
    // measured the field on screen. Reading the panel at export would write a
    // file that states, with full confidence and a SHA-256 beside it, a
    // configuration that never produced anything -- which is worse than
    // omitting provenance, because it is provenance that cannot be doubted by
    // looking.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));
    window.addTargetImages({fixture(QStringLiteral("shift_target.tif"))});

    auto *subsetRadius =
        controlLabelled<QSpinBox>(&window, QStringLiteral("Subset radius"));
    controlLabelled<QSpinBox>(&window, QStringLiteral("Grid step"))->setValue(12);
    subsetRadius->setValue(16);

    auto *viewport = window.findChild<ImageViewport *>();
    actionLabelled(&window, QStringLiteral("Run Correlation"))->trigger();
    QVERIFY2(QTest::qWaitFor([viewport] { return viewport->hasField(); }, 120000),
             "the correlation produced no field within two minutes");

    // Change the panel after the run, the way anyone setting up the next one
    // would, and only then export.
    subsetRadius->setValue(31);

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("provenance.vtu"));
    QVERIFY(window.exportFieldTo(path));

    // Read back through a reader rather than by searching the file's bytes:
    // the writer stores field data in binary, so the provenance is base64 and
    // compressed on disk. A first version of this case grepped the raw text and
    // failed against correct code, which is its own small lesson -- the file is
    // for programs, and the test has to be one.
    const QString stated = provenanceIn(path);

    QVERIFY2(stated.contains(QStringLiteral("subset radius 16 px")),
             "the file does not state the subset radius the field was measured with");
    QVERIFY2(!stated.contains(QStringLiteral("subset radius 31 px")),
             "the file states a subset radius that never measured anything");
}


void TestWorkspaceWalkthrough::the_reliability_of_a_field_is_reachable_and_qualified_on_screen()
{
    // Tenet 9 as something a test can enforce. It is not enough for the numbers
    // to exist: a noise floor shown without the sentence that says what it is
    // NOT will be read as a total error bar by everyone, which is worse than
    // showing nothing, because it looks like due diligence.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));
    window.addTargetImages({fixture(QStringLiteral("shift_target.tif"))});
    controlLabelled<QSpinBox>(&window, QStringLiteral("Grid step"))->setValue(12);

    auto *viewport = window.findChild<ImageViewport *>();
    actionLabelled(&window, QStringLiteral("Run Correlation"))->trigger();
    QVERIFY2(QTest::qWaitFor([viewport] { return viewport->hasField(); }, 120000),
             "the correlation produced no field within two minutes");

    // Nobody had to ask for this, and there is no setting to have forgotten.
    const CorrelationResult &result = window.lastResult();
    QCOMPARE(result.noiseFloorMeasured, result.converged);

    // Reachable by name from the screen, like any other channel.
    auto *choice = viewport->findChild<QComboBox *>();
    const int index = choice->findText(fieldChannelName(FieldChannel::NoiseFloor));
    QVERIFY2(index >= 0, "the field's reliability cannot be reached from the screen");
    choice->setCurrentIndex(index);
    QCOMPARE(viewport->fieldChannel(), FieldChannel::NoiseFloor);

    // And qualified where it is shown, not in documentation nobody has open.
    QVERIFY2(somethingOnScreenSays(viewport, QStringLiteral("not a total error bar")),
             "the noise floor is on screen without saying it is not an error bar");
    QVERIFY2(somethingOnScreenSays(viewport, QStringLiteral("Larger is worse")),
             "nothing says which direction of this scale is the bad one");
    QVERIFY2(somethingOnScreenSays(viewport, QStringLiteral("one part in")),
             "the noise floor is not put against the movement it qualifies");
}


// The two fixture images, imported in the WRONG order on purpose: as targets
// they are a two-frame sequence whose frame order is alphabetical, and
// shift_reference sorts before shift_target.
static QStringList outOfOrderFrames()
{
    return {fixture(QStringLiteral("shift_target.tif")),
            fixture(QStringLiteral("shift_reference.tif"))};
}

void TestWorkspaceWalkthrough::targets_are_listed_in_frame_order_not_the_order_they_were_chosen()
{
    // A sequence is a time axis, and the project has to show the order it will
    // actually be measured in. If the list on screen and the order of
    // measurement disagree, every frame is right and the series is nonsense --
    // and there is nothing on screen that could reveal it.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));
    window.addTargetImages(outOfOrderFrames());

    QStringList listed;
    for (QTreeWidget *tree : window.findChildren<QTreeWidget *>()) {
        for (int i = 0; i < tree->topLevelItemCount(); i++) {
            QTreeWidgetItem *top = tree->topLevelItem(i);
            if (!top->text(0).startsWith(QStringLiteral("Target")))
                continue;
            for (int c = 0; c < top->childCount(); c++)
                listed << top->child(c)->text(0);
        }
    }

    QCOMPARE(listed.size(), 2);
    QVERIFY2(listed.at(0).startsWith(QStringLiteral("shift_reference")),
             qPrintable(QStringLiteral("the project lists %1 first, but frame "
                                       "order puts shift_reference there")
                            .arg(listed.at(0))));
    QVERIFY2(listed.at(1).startsWith(QStringLiteral("shift_target")),
             qPrintable(listed.at(1)));
}

void TestWorkspaceWalkthrough::every_frame_of_a_sequence_is_measured_and_listed()
{
    // The interface has accepted several targets since the first window and
    // measured exactly one of them. That is the gap this closes.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));
    window.addTargetImages(outOfOrderFrames());
    controlLabelled<QSpinBox>(&window, QStringLiteral("Grid step"))->setValue(20);

    auto *viewport = window.findChild<ImageViewport *>();
    actionLabelled(&window, QStringLiteral("Run Correlation"))->trigger();
    QVERIFY2(QTest::qWaitFor([&window] { return window.measuredFrames() == 2; }, 180000),
             "the run did not measure both frames within three minutes");
    QVERIFY(viewport->hasField());

    // Frame order, verified by the measurement rather than by the label: frame
    // 0 is the reference against itself and cannot have moved, frame 1 is the
    // +3 px target. Measured in the other order, both numbers land on the
    // wrong frame and nothing else would show it.
    QVERIFY2(qAbs(meanU(window.frameResult(0))) < 0.05,
             "frame 0 should be the reference against itself, which does not move");
    QVERIFY2(qAbs(meanU(window.frameResult(1)) - 3.0) < 0.1,
             "frame 1 should be the +3 px target");

    // And the project says so, with both frames on it rather than one result.
    QVERIFY2(projectLine(&window, QStringLiteral("Results"))
                     .contains(QStringLiteral("2"))
                 || projectLine(&window, QStringLiteral("Displacement"))
                        .contains(QStringLiteral("2")),
             qPrintable(QStringLiteral("the project does not report two frames: %1")
                            .arg(projectLine(&window, QStringLiteral("Results")))));
}

void TestWorkspaceWalkthrough::a_frame_can_be_picked_from_the_project_and_shows_its_own_field()
{
    // A sequence nobody can step through is a sequence that measured itself for
    // nothing. Each frame has to be reachable, and reachable by pointing at it.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));
    window.addTargetImages(outOfOrderFrames());
    controlLabelled<QSpinBox>(&window, QStringLiteral("Grid step"))->setValue(20);

    auto *viewport = window.findChild<ImageViewport *>();
    actionLabelled(&window, QStringLiteral("Run Correlation"))->trigger();
    QVERIFY(QTest::qWaitFor([&window] { return window.measuredFrames() == 2; }, 180000));

    // Find the frame entries under Results and click the first one, the way a
    // user picks a frame.
    QTreeWidgetItem *firstFrame = nullptr;
    for (QTreeWidget *tree : window.findChildren<QTreeWidget *>()) {
        for (int i = 0; i < tree->topLevelItemCount(); i++) {
            QTreeWidgetItem *top = tree->topLevelItem(i);
            if (top->childCount() > 0 && top->text(0).contains(QStringLiteral("frame"),
                                                               Qt::CaseInsensitive)) {
                firstFrame = top->child(0);
            }
        }
    }
    QVERIFY2(firstFrame, "no frame of the sequence can be picked from the project");

    firstFrame->treeWidget()->setCurrentItem(firstFrame);

    QVERIFY(viewport->hasField());
    QVERIFY2(qAbs(meanU(window.displayedResult())) < 0.05,
             "picking frame 0 did not put frame 0 on screen");
}

void TestWorkspaceWalkthrough::exporting_a_sequence_writes_one_numbered_file_for_each_frame()
{
    // One file per frame, numbered, because that is what ParaView opens as a
    // time series. A single file holding the last frame would silently discard
    // everything the sequence was run for.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));
    window.addTargetImages(outOfOrderFrames());
    controlLabelled<QSpinBox>(&window, QStringLiteral("Grid step"))->setValue(20);

    auto *viewport = window.findChild<ImageViewport *>();
    actionLabelled(&window, QStringLiteral("Run Correlation"))->trigger();
    QVERIFY(QTest::qWaitFor([&window] { return window.measuredFrames() == 2; }, 180000));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(window.exportFieldTo(dir.filePath(QStringLiteral("run.vtu"))));

    const QStringList written =
        QDir(dir.path()).entryList({QStringLiteral("*.vtu")}, QDir::Files, QDir::Name);
    QCOMPARE(written.size(), 2);
    QVERIFY2(written.at(0).contains(QStringLiteral("0000")),
             qPrintable(QStringLiteral("frames are not numbered for a time "
                                       "series: %1").arg(written.join(QLatin1Char(' ')))));

    // Numbered so that they sort into frame order as filenames, which is how
    // ParaView groups them: unpadded, frame 10 would open before frame 2.
    QVERIFY(precedesInSequence(written.at(0), written.at(1)));
}


void TestWorkspaceWalkthrough::re_anchoring_is_offered_off_by_default_and_says_what_it_costs()
{
    // Re-anchoring changes what every later frame is compared against. That is
    // the right answer for a specimen that deforms far, and the wrong one for a
    // specimen that does not - it resets each point's baseline and abandons any
    // point that could not be measured on the frame it happens. So it is
    // offered, defaulted OFF, and explained where it is switched on rather than
    // in documentation nobody has open.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *reanchor = byVisibleText<QCheckBox>(&window, QStringLiteral("Re-anchor"));
    QVERIFY2(reanchor, "the panel offers no way to re-anchor the reference");
    QVERIFY2(!reanchor->isChecked(),
             "re-anchoring is on by default, which changes every sequence "
             "silently");

    QVERIFY2(somethingOnScreenSays(&window, QStringLiteral("original reference")),
             "nothing says what the reference is measured against by default");
    QVERIFY2(somethingOnScreenSays(&window, QStringLiteral("lost")),
             "nothing says that re-anchoring abandons points it cannot measure");

    // The two numbers that decide it are only meaningful while it is on, and
    // must be reachable by the label a reader sees.
    auto *threshold =
        controlLabelled<QDoubleSpinBox>(&window, QStringLiteral("Correlation a point"));
    auto *share =
        controlLabelled<QSpinBox>(&window, QStringLiteral("Share of points"));
    QVERIFY2(threshold && share, "the re-anchor rule has no visible settings");
    QVERIFY2(!threshold->isEnabled() && !share->isEnabled(),
             "the re-anchor settings are live while re-anchoring is off");

    reanchor->setChecked(true);
    QVERIFY2(threshold->isEnabled() && share->isEnabled(),
             "switching re-anchoring on did not enable its own settings");
}

// --- reading one point out of the field ------------------------------------
//
// A field could only be read as colour before this. These cases are written the
// way the rules require: everything they do is something the screen tells them
// to do, and the FIRST case is what licenses the other three -- if the panel did
// not say "move the pointer over the field, click to pin", then hovering and
// clicking would be tribal knowledge and the test would be cheating.
//
// NEGATIVE CHECK (2026-08-31): five breaks, each reverted after. All five
// turned the case named for them red -- the panel omitting its own
// instructions, clicking not pinning, a pinned reading still following the
// pointer, the viewport no longer tracking the pointer, and a position off the
// picture reporting the point nearest the border.
//
// ⚑ That last one did NOT redden at first, and the reason is worth keeping.
// The POI grid is inset from the image edge by one SUBSET RADIUS, while a point
// is read out to one GRID STEP. At the defaults (16 and 5) a position held to
// the border can never reach a measured point, so the inset was doing the work
// and the case was reporting on a guard it never touched. It now sets the two
// controls to 8 and 20, where the guard is the only thing between a pointer
// off the picture and a real measurement attributed to it, and the break goes
// red as it should.

void TestWorkspaceWalkthrough::the_point_panel_says_how_to_read_a_field_before_one_exists()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const QString said = pointPanelText(&window);
    QVERIFY2(!said.isEmpty(), "there is no point panel on screen at all");

    // Both gestures have to be visible before they are needed. A readout that
    // only appears once you happen to hover is a hidden mode: nothing would
    // ever tell a reader the capability exists.
    QVERIFY2(said.contains(QStringLiteral("pointer"), Qt::CaseInsensitive),
             qPrintable(said));
    QVERIFY2(said.contains(QStringLiteral("click"), Qt::CaseInsensitive),
             qPrintable(said));
    QVERIFY2(said.contains(QStringLiteral("pin"), Qt::CaseInsensitive),
             qPrintable(said));
}

void TestWorkspaceWalkthrough::pointing_at_a_measured_point_reads_out_what_it_measured()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));
    window.addTargetImages({fixture(QStringLiteral("shift_target.tif"))});

    auto *viewport = window.findChild<ImageViewport *>();
    actionLabelled(&window, QStringLiteral("Run Correlation"))->trigger();
    QVERIFY2(QTest::qWaitFor([viewport] { return viewport->hasField(); }, 120000),
             "the correlation produced no field within two minutes");

    // Point at the middle of the picture, the way the panel says to.
    const ImageRecord &record = viewport->record();
    const double px = record.width / 2.0;
    const double py = record.height / 2.0;
    // Moved twice, with a wait between. QTest's synthetic move and the X
    // server's own pointer motion both arrive, and the last one wins; repeating
    // the gesture is what a user does anyway, and makes which one that is
    // stop mattering.
    QTest::mouseMove(viewport, widgetPointForPixel(viewport, px, py));
    QTest::qWait(50);
    QTest::mouseMove(viewport, widgetPointForPixel(viewport, px, py));
    QTest::qWait(50);

    const CorrelationResult &result = window.lastResult();
    const int index = pointNearestTo(result, float(px), float(py));
    QVERIFY2(index >= 0, "no measured point near the middle of the picture");
    const CorrelationPoint &point = result.points[index];
    QVERIFY(point.converged);

    const QString said = pointPanelText(&window);

    // The screen must carry this point's OWN measurement, not a summary and
    // not a placeholder. Compared against the value in the result, so what is
    // asserted is that the panel is wired to the real measurement.
    QVERIFY2(said.contains(QString::number(double(point.u), 'g', 4)),
             qPrintable(QStringLiteral("panel said:\n%1\nu was %2")
                            .arg(said, QString::number(double(point.u)))));
    QVERIFY2(said.contains(QStringLiteral("Displacement"), Qt::CaseInsensitive),
             qPrintable(said));

    // And the reliability half, which is the thing no colour map could show:
    // a number a reader can put against the displacement beside it.
    QVERIFY2(said.contains(QStringLiteral("Noise floor"), Qt::CaseInsensitive),
             qPrintable(said));
    QVERIFY2(said.contains(fieldChannelNote(FieldChannel::NoiseFloor)),
             qPrintable(said));
}

void TestWorkspaceWalkthrough::a_pinned_point_stays_on_screen_when_the_pointer_moves_away()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));
    window.addTargetImages({fixture(QStringLiteral("shift_target.tif"))});

    auto *viewport = window.findChild<ImageViewport *>();
    actionLabelled(&window, QStringLiteral("Run Correlation"))->trigger();
    QVERIFY2(QTest::qWaitFor([viewport] { return viewport->hasField(); }, 120000),
             "the correlation produced no field within two minutes");

    // Let the window finish arriving before reading it. The field appears as
    // soon as the frame lands; the project tree and the status line follow.
    QTest::qWait(300);

    const ImageRecord &record = viewport->record();
    const QPoint at = widgetPointForPixel(viewport, record.width / 2.0,
                                          record.height / 2.0);
    // Click to pin, as the panel says. The reading is taken from the pinned
    // state rather than from a hover beforehand: a synthetic pointer move races
    // with the X server's own, so which of them lands last is not something
    // this case is about. A click is unambiguous.
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, at);
    QTest::qWait(50);
    const QString hovered = pointPanelText(&window);
    QVERIFY2(hovered.contains(QStringLiteral("pinned"), Qt::CaseInsensitive),
             qPrintable(hovered));
    QVERIFY2(hovered.contains(QStringLiteral("Displacement"), Qt::CaseInsensitive),
             qPrintable(hovered));

    // Now leave. Without pinning, a readout that follows the pointer cannot be
    // read and written down at the same time: looking away is what erases it.
    QTest::mouseMove(viewport, QPoint(4, 4));
    QTest::qWait(50);

    const QString afterLeaving = pointPanelText(&window);
    QVERIFY2(afterLeaving.contains(hovered.section(QLatin1Char('\n'), 1, 4)),
             qPrintable(QStringLiteral("pinned readout changed when the pointer "
                                       "left:\nwas:\n%1\nnow:\n%2")
                            .arg(hovered, afterLeaving)));

    // And the way out is stated, not remembered.
    QVERIFY2(afterLeaving.contains(QStringLiteral("release"), Qt::CaseInsensitive)
                 || afterLeaving.contains(QStringLiteral("unpin"), Qt::CaseInsensitive),
             qPrintable(afterLeaving));
}

void TestWorkspaceWalkthrough::pointing_away_from_the_field_says_so_rather_than_going_blank()
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));
    window.addTargetImages({fixture(QStringLiteral("shift_target.tif"))});

    // ⚑ These two numbers are the case, not decoration. The grid is inset from
    // the image edge by one SUBSET RADIUS, and a point is read out to a
    // distance of one GRID STEP, so a position held to the border only reaches
    // a measured point when the step is the larger of the two. At the default
    // 16 and 5 it never does, and an earlier version of this case passed
    // against a build with the guard removed: the inset was doing the work, and
    // the case was reporting on a mechanism it never touched.
    controlLabelled<QSpinBox>(&window, QStringLiteral("Subset radius"))->setValue(8);
    controlLabelled<QSpinBox>(&window, QStringLiteral("Grid step"))->setValue(20);

    auto *viewport = window.findChild<ImageViewport *>();
    actionLabelled(&window, QStringLiteral("Run Correlation"))->trigger();
    QVERIFY2(QTest::qWaitFor([viewport] { return viewport->hasField(); }, 120000),
             "the correlation produced no field within two minutes");
    QTest::qWait(300);

    // The very corner of the widget, which is off the picture entirely.
    QTest::mouseMove(viewport, QPoint(2, 2));
    QTest::qWait(50);
    QTest::mouseMove(viewport, QPoint(2, 2));
    QTest::qWait(50);

    const QString said = pointPanelText(&window);
    QVERIFY2(!said.trimmed().isEmpty(),
             "the panel emptied itself, which reads as broken software rather "
             "than as an absence of measurement");

    // And it must not report on the point nearest the edge instead. A position
    // off the picture is held to the border everywhere else in this widget,
    // which is right for a region corner and wrong here: it would attribute a
    // real measurement to a place the pointer is not.
    QVERIFY2(said.contains(QStringLiteral("no point measured here")),
             qPrintable(said));
    QVERIFY2(!said.contains(QStringLiteral("Displacement"), Qt::CaseInsensitive),
             qPrintable(said));
}

// --- the field as a table ---------------------------------------------------
//
// NEGATIVE CHECK (2026-08-31): three breaks. Two of them stayed GREEN at first
// and both were real gaps, of the same kind: a case reporting on a mechanism it
// never touched.
//
//   - removing the export from the File menu did not redden anything, because
//     the case looked the action up with findChildren<QAction*>, which finds an
//     action that was constructed and added to nothing. That is exactly a
//     capability no user can reach. menuActionLabelled() now walks the menu bar.
//   - writing the frame number and extension together as "_%1.vtu" meant a
//     sequence exported as tables would write VTK files, and no case noticed,
//     because every sequence export case used the .vtu path. Covered now.
//
// The third (the table export writing a .vtu) reddened immediately, and reddens
// both cases now.

void TestWorkspaceWalkthrough::a_measured_field_can_also_leave_as_a_table_anything_opens()
{
    // The .vtu needs a VTK-aware tool to open at all. A reader checking three
    // numbers should not have to install ParaView, so the same field goes out
    // as a table too -- and the capability has to be VISIBLE, not a matter of
    // typing a different extension into the save dialog and hoping.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));
    window.addTargetImages({fixture(QStringLiteral("shift_target.tif"))});
    controlLabelled<QSpinBox>(&window, QStringLiteral("Grid step"))->setValue(12);

    auto *viewport = window.findChild<ImageViewport *>();
    actionLabelled(&window, QStringLiteral("Run Correlation"))->trigger();
    QVERIFY2(QTest::qWaitFor([viewport] { return viewport->hasField(); }, 120000),
             "the correlation produced no field within two minutes");

    // Looked up through the MENU BAR, not among the window's children: an
    // action that exists but sits in no menu is a capability nobody can reach.
    QAction *csv = menuActionLabelled(&window, QStringLiteral(".csv"));
    QVERIFY2(csv, "no menu offers the field as a table");
    QVERIFY2(csv->isEnabled(), "a measured field still could not be exported as a table");
    QVERIFY2(!csv->toolTip().isEmpty(), "the action does not say what it does");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("walkthrough.csv"));
    QVERIFY2(window.exportFieldCsvTo(path), "the export reported failure");
    QVERIFY2(QFile::exists(path), "the export reported success and wrote nothing");

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString text = QString::fromUtf8(file.readAll());

    // Unlike the .vtu's, this provenance is greppable -- which is most of why
    // the format is worth having beside it.
    QVERIFY2(text.contains(QStringLiteral("shift_reference.tif")), qPrintable(text.left(600)));
    QVERIFY2(text.contains(QStringLiteral("x_px,y_px")), qPrintable(text.left(600)));

    // And where it went has to be visible afterwards, as with the .vtu.
    auto *log = window.findChild<QPlainTextEdit *>();
    QVERIFY2(log->toPlainText().contains(QStringLiteral("walkthrough.csv")),
             "nothing on screen says where the table went");
}

void TestWorkspaceWalkthrough::exporting_a_sequence_as_tables_numbers_them_and_keeps_the_extension()
{
    // The same numbering rule as the .vtu sequence, in the other format. Found
    // by a negative check: the frame number and the extension were formatted
    // together as "_%1.vtu", so a sequence exported as tables would have
    // written .vtu files, and no case noticed because none exported a sequence
    // as anything but a .vtu.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));
    window.addTargetImages(outOfOrderFrames());
    controlLabelled<QSpinBox>(&window, QStringLiteral("Grid step"))->setValue(20);

    actionLabelled(&window, QStringLiteral("Run Correlation"))->trigger();
    QVERIFY(QTest::qWaitFor([&window] { return window.measuredFrames() == 2; }, 180000));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(window.exportFieldCsvTo(dir.filePath(QStringLiteral("run.csv"))));

    const QStringList written =
        QDir(dir.path()).entryList({QStringLiteral("*.csv")}, QDir::Files, QDir::Name);
    QCOMPARE(written.size(), 2);
    QVERIFY2(written.at(0).contains(QStringLiteral("0000")),
             qPrintable(written.join(QLatin1Char(' '))));
    QVERIFY(precedesInSequence(written.at(0), written.at(1)));

    // And nothing of the other format was left in the folder.
    QVERIFY2(QDir(dir.path()).entryList({QStringLiteral("*.vtu")}, QDir::Files).isEmpty(),
             "exporting tables wrote VTK files as well");
}

// --- approachability -------------------------------------------------------
//
// The rules already forbid a capability that can only be reached by knowing it
// is there. These two cases are the weaker cousin of that: a capability that is
// reachable, and that a first-time reader has no reason to look for.

void TestWorkspaceWalkthrough::the_first_screen_shows_the_whole_path_not_only_its_first_step()
{
    // An empty workspace used to say "No image loaded" and name one menu item.
    // That is enough to take the first step and nothing else: a reader learns
    // the shape of the work only by discovering it a step at a time. The whole
    // path is four steps and it costs nothing to show all four.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *viewport = window.findChild<ImageViewport *>();
    QVERIFY(viewport);

    QStringList said;
    for (QLabel *label : viewport->findChildren<QLabel *>()) {
        if (label->isVisible())
            said << label->text();
    }
    const QString all = said.join(QLatin1Char('\n'));

    for (const QString &step : {QStringLiteral("reference"), QStringLiteral("target"),
                                QStringLiteral("region"), QStringLiteral("correlation")}) {
        QVERIFY2(all.contains(step, Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("the empty workspace never mentions "
                                           "'%1':\n%2").arg(step, all)));
    }

    // And the first step is offered as something to press, not only described.
    QVERIFY2(byVisibleText<QPushButton>(viewport, QStringLiteral("reference")),
             "the first step is described but cannot be taken from where it is described");
}

void TestWorkspaceWalkthrough::the_toolbar_carries_an_icon_beside_every_name()
{
    // Half the toolbar had icons and half was bare words, which reads as two
    // different toolbars and gives the eye nothing to aim at. Nothing here is
    // icon-ONLY: the name stays, because an icon alone is a thing you have to
    // already know.
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *bar = window.findChild<QToolBar *>();
    QVERIFY(bar);

    int actions = 0;
    for (QAction *action : bar->actions()) {
        if (action->isSeparator() || action->text().isEmpty())
            continue;
        actions++;
        QVERIFY2(!action->icon().isNull(),
                 qPrintable(QStringLiteral("'%1' has no icon").arg(action->text())));
    }
    QVERIFY2(actions >= 6, "the toolbar lost actions");
}

QTEST_MAIN(TestWorkspaceWalkthrough)
#include "test_workspace_walkthrough.moc"
