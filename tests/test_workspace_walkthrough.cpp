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
#include "core/StrainFit.h"
#include "core/SubsetOverlay.h"
#include "core/Sequence.h"
#include "core/Roi.h"
#include "gui/ImageViewport.h"
#include "gui/PlotPanel.h"
#include "gui/PointPanel.h"
#include "gui/CollapsibleSection.h"
#include "gui/ComparisonWindow.h"
#include "gui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QRegularExpression>
#include <QCheckBox>
#include <QWheelEvent>
#include <QComboBox>
#include <QIcon>
#include <QPixmap>
#include <QFrame>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
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

#include <optional>
#include <utility>

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
// ⚑ The heading's line AND the lines under it, because that is what the panel
// SHOWS. It used to return the top-level text alone, and a case asking whether
// the screen tells a reader the corners can be dragged was answered by a
// sentence the dock was eliding: the string was in the item, and the reader
// could not see it. Splitting the facts onto children is what fixed that, and
// this helper had to learn where the screen puts text or every case built on it
// would have gone red for the wrong reason.
QString projectLine(QWidget *root, const QString &heading)
{
    for (QTreeWidget *tree : root->findChildren<QTreeWidget *>()) {
        for (int i = 0; i < tree->topLevelItemCount(); i++) {
            QTreeWidgetItem *top = tree->topLevelItem(i);
            const QString text = top->text(0);
            if (!text.startsWith(heading))
                continue;
            QStringList shown{text};
            for (int c = 0; c < top->childCount(); c++)
                shown << top->child(c)->text(0);
            return shown.join(QStringLiteral(" \u00b7 "));
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

// Wait for an action to become enabled, rather than asserting the instant the
// field appears.
//
// ⚑ The field is rendered as soon as the measured frame lands; what a run
// ENABLES follows separately. Asserting immediately after hasField() therefore
// asserts an ordering nothing guarantees, and it holds on a fast machine and
// fails on a loaded CI runner -- which is exactly how it was found, with "a
// measured field still could not be exported as a table" against an
// application that exports it perfectly well a moment later.
//
// This waits for the state the case is actually about, so a slow machine
// reports the same result as a fast one. It is not a sleep: it returns as soon
// as the condition holds.
bool waitForEnabled(QAction *action, int timeoutMs = 5000)
{
    if (!action)
        return false;
    return QTest::qWaitFor([action] { return action->isEnabled(); }, timeoutMs);
}

// The two numbers on the panel's displacement line, as numbers.
//
// Returns nothing when the panel is not showing a measured displacement at all
// -- an unmeasured point, or no point under the pointer -- which is a
// different answer from "showing the wrong numbers" and has to stay
// distinguishable from it.
std::optional<std::pair<double, double>> displacementLine(const QString &panel)
{
    for (const QString &line : panel.split(QLatin1Char('\n'))) {
        if (!line.contains(QStringLiteral("px")))
            continue;
        // "<u>, <v> px" -- two numbers, in that order, on one line.
        static const QRegularExpression pattern(
            QStringLiteral("^\\s*(-?[0-9.eE+-]+)\\s*,\\s*(-?[0-9.eE+-]+)\\s*px\\s*$"));
        const QRegularExpressionMatch match = pattern.match(line);
        if (!match.hasMatch())
            continue;
        bool okU = false;
        bool okV = false;
        const double u = match.captured(1).toDouble(&okU);
        const double v = match.captured(2).toDouble(&okV);
        if (okU && okV)
            return std::make_pair(u, v);
    }
    return std::nullopt;
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
// The foldable section with a given title, found the way a reader finds it:
// by the words on its header.
CollapsibleSection *sectionTitled(QWidget *root, const QString &title)
{
    for (CollapsibleSection *section : root->findChildren<CollapsibleSection *>()) {
        auto *header = section->findChild<QToolButton *>();
        if (header && header->text().contains(title, Qt::CaseInsensitive))
            return section;
    }
    return nullptr;
}

// One entry of the Open Example submenu, found by the name it shows. A test
// that opened an example by path would be reaching past the interface into the
// file system; this opens the one a reader can see and press.
QAction *exampleNamed(QMainWindow *window, const QString &name)
{
    for (QAction *top : window->menuBar()->actions()) {
        QMenu *menu = top->menu();
        if (!menu)
            continue;
        for (QAction *action : menu->actions()) {
            QMenu *examples = action->menu();
            if (!examples || !action->text().contains(QStringLiteral("example"),
                                                      Qt::CaseInsensitive))
                continue;
            for (QAction *entry : examples->actions()) {
                if (entry->isEnabled() && !entry->isSeparator()
                    && entry->text().contains(name, Qt::CaseInsensitive))
                    return entry;
            }
        }
    }
    return nullptr;
}

// Everything a window's visible labels say, as one string. The captions of the
// comparison screen are read this way for the same reason the point panel is:
// what is asserted has to be what reaches a reader.
QString visibleText(QWidget *root)
{
    QStringList parts;
    for (QLabel *label : root->findChildren<QLabel *>()) {
        if (label->isVisible())
            parts << label->text();
    }
    return parts.join(QLatin1Char('\n'));
}

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
    void the_region_says_it_can_be_adjusted_where_the_panel_can_show_it();
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
    void the_shipped_examples_can_be_opened_from_the_menu();
    void a_session_saved_and_reopened_is_the_session_that_was_saved();
    void a_committed_region_can_be_adjusted_without_drawing_it_again();
    void a_corner_can_be_added_to_an_edge_and_taken_out_again_on_the_image();
    void a_refusal_to_remove_a_corner_does_not_take_the_picture_with_it();
    void a_region_moves_bodily_while_a_click_inside_it_still_pins_a_reading();
    void the_second_pass_is_on_screen_and_says_what_it_does_and_costs();
    void scrolling_the_analysis_panel_does_not_change_what_will_be_measured();
    void the_repaired_points_can_be_seen_on_the_map_and_counted_beside_it();
    void a_hole_can_be_cut_out_of_a_region_from_the_screen();
    void the_viewport_says_which_way_its_axes_run();
    void the_plot_panel_says_what_it_is_for_before_a_sequence_exists();
    void an_extensometer_is_placed_by_clicking_and_plotted_over_the_sequence();
    void exporting_a_sequence_as_tables_numbers_them_and_keeps_the_extension();

    void the_comparison_with_a_known_answer_says_why_it_is_unavailable_before_a_run();
    void an_example_that_states_its_own_answer_is_measured_against_it_on_screen();
    void a_dataset_that_states_no_answer_says_so_rather_than_offering_a_comparison();

    void the_subset_can_be_seen_at_its_real_size_before_any_run();
    void the_points_drawn_in_the_subregion_are_the_ones_the_panel_counts();
    void the_drawn_subset_follows_the_radius_that_will_be_measured();

    void what_the_camera_recorded_is_readable_before_any_correlation();
    void a_camera_reading_can_be_pinned_without_a_field_to_read();
    void the_pixel_read_out_is_the_one_the_file_holds_at_that_position();
    void clicking_another_pixel_moves_the_pin_rather_than_releasing_it();

    void the_window_carries_the_application_icon();
    void the_speckle_estimate_says_what_it_needs_before_any_image_exists();
    void the_empty_workspace_offers_the_examples_it_ships_with();
    void a_panel_section_folds_away_and_says_what_it_still_holds();
    void a_folded_section_still_governs_the_run();
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
    QVERIFY2(waitForEnabled(exportAction),
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

    // ⚑ Wait for the RUN to be finished, not merely for the last frame to have
    // arrived. The frames counter rises as each frame lands; the project line
    // below is written when the sequence ends, so between the two there is a
    // window where two frames are measured and the tree still says "none".
    // Seen as a one-off failure on a loaded machine, 2026-09-08 -- the same
    // lesson waitForEnabled() above was written for, in a case that had not
    // learned it.
    QVERIFY(waitForEnabled(actionLabelled(&window, QStringLiteral("Run Correlation")),
                           30000));

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

    // ⚑ The section is FOLDED to start with, and that is allowed here for one
    // reason: re-anchoring is off, so nothing can happen without unfolding it
    // first, and unfolding is what puts the caution on screen. What a fold may
    // never do is hide behaviour that is already running -- see the second
    // pass, which stays open for exactly that reason. Folded, the header still
    // states what the run will do.
    CollapsibleSection *reference = sectionTitled(&window, QStringLiteral("Reference"));
    QVERIFY2(reference, "the reference settings are in no findable section");
    QVERIFY2(visibleText(reference).contains(QStringLiteral("original reference")),
             qPrintable(QStringLiteral("a folded Reference section does not say "
                                       "what the run does:\n%1")
                            .arg(visibleText(reference))));

    // Opened from its own header, as a reader opens it.
    auto *header = reference->findChild<QToolButton *>();
    QVERIFY(header);
    QTest::mouseClick(header, Qt::LeftButton);
    QVERIFY2(!reference->isCollapsed(), "pressing the header did not open the section");

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

    // Let the window finish arriving before reading it, as the pinned-point
    // case does: the field appears as soon as the frame lands, the panel
    // follows.
    QTest::qWait(300);

    // Ask the result where a measured point IS, and point at that, rather than
    // at the middle of the picture and trusting one to be there. The middle
    // falls between grid nodes as easily as on one, and pointNearestTo answers
    // about the RESULT while the panel answers about the position actually
    // under the pointer -- so the two could disagree while both were right.
    const ImageRecord &record = viewport->record();
    const CorrelationResult &result = window.lastResult();
    const int index = pointNearestTo(result, float(record.width / 2.0),
                                     float(record.height / 2.0));
    QVERIFY2(index >= 0, "no measured point near the middle of the picture");
    const CorrelationPoint &point = result.points[index];
    QVERIFY(point.converged);

    // Click to pin, as the panel says, rather than reading a hover. ⚑ A
    // synthetic pointer move races with the X server's own and the last one
    // wins, which is not something this case is about: it is about whether the
    // panel carries this point's own measurement. Read from a hover, it failed
    // on CI with "no point measured here" while the pointer sat somewhere else
    // entirely. The pinned-point case beside this one already took a click for
    // exactly this reason; this one had not been changed to match.
    const QPoint at = widgetPointForPixel(viewport, double(point.x), double(point.y));
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, at);

    // Waited for rather than slept past, so a slow machine reports the same
    // result as a fast one.
    QVERIFY2(QTest::qWaitFor([&window] {
                 return displacementLine(pointPanelText(&window)).has_value();
             }, 5000),
             qPrintable(QStringLiteral("panel never showed a displacement:\n%1")
                            .arg(pointPanelText(&window))));

    const QString said = pointPanelText(&window);

    // The screen must carry this point's OWN measurement, not a summary and
    // not a placeholder.
    //
    // ⚑ Read as NUMBERS out of the displacement line, not as a substring of
    // the whole panel. The substring form looked strict and was nearly
    // vacuous: on this fixture u is exactly 3 px, so it asserted that the
    // panel contained the character "3" somewhere -- which it does in a
    // coordinate, a noise floor, or a correlation. Negative-checked by making
    // the panel report DOUBLE the displacement: the substring version passed,
    // this version fails and says by how much.
    //
    // Compared numerically rather than against a re-formatted string so the
    // test states the property (the panel carries this point's measurement)
    // without restating how the panel is allowed to format it. The tolerance
    // is the display's own: four significant digits.
    const auto shown = displacementLine(said);
    QVERIFY2(shown.has_value(), qPrintable(said));
    const double tolerance = 1e-3;
    QVERIFY2(qAbs(shown->first - double(point.u))
                 <= tolerance * qMax(1.0, qAbs(double(point.u))),
             qPrintable(QStringLiteral("panel said u = %1, measured u was %2\npanel:\n%3")
                            .arg(shown->first).arg(double(point.u)).arg(said)));
    QVERIFY2(qAbs(shown->second - double(point.v))
                 <= tolerance * qMax(1.0, qAbs(double(point.v))),
             qPrintable(QStringLiteral("panel said v = %1, measured v was %2\npanel:\n%3")
                            .arg(shown->second).arg(double(point.v)).arg(said)));
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
    QVERIFY2(waitForEnabled(csv),
             "a measured field still could not be exported as a table");
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

void TestWorkspaceWalkthrough::the_shipped_examples_can_be_opened_from_the_menu()
{
    // The examples were in the repository and reachable from nothing. A reader
    // who installs SurView should be able to measure something before going to
    // find speckle images of their own.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QMenu *examples = nullptr;
    for (QAction *top : window.menuBar()->actions()) {
        QMenu *menu = top->menu();
        if (!menu)
            continue;
        for (QAction *action : menu->actions()) {
            if (action->menu() && action->text().contains(QStringLiteral("example"),
                                                          Qt::CaseInsensitive))
                examples = action->menu();
        }
    }
    QVERIFY2(examples, "no menu offers the examples that ship with SurView");

    QList<QAction *> entries;
    for (QAction *action : examples->actions()) {
        if (!action->isSeparator() && action->isEnabled())
            entries << action;
    }
    // Headings are disabled, so this counts openable examples only.
    QVERIFY2(entries.size() >= 5,
             qPrintable(QStringLiteral("only %1 example(s) offered").arg(entries.size())));

    // Each says how many frames it has, so a reader knows whether they are
    // opening a pair or a sequence before they open it.
    for (QAction *action : entries) {
        QVERIFY2(!action->toolTip().isEmpty(), qPrintable(action->text()));
        QVERIFY2(action->toolTip().contains(QStringLiteral("frames")),
                 qPrintable(action->toolTip()));
    }

    // And opening one actually loads it: a reference and at least one target,
    // with the run now possible.
    entries.first()->trigger();
    QVERIFY2(QTest::qWaitFor([&window] { return window.measuredFrames() >= 0
                                                && !window.roi().isValid(); }, 5000),
             "opening an example did nothing");

    QVERIFY2(projectLine(&window, QStringLiteral("Reference image"))
                 .contains(QStringLiteral("none")) == false,
             qPrintable(projectLine(&window, QStringLiteral("Reference image"))));
    QVERIFY2(!projectLine(&window, QStringLiteral("Target images"))
                  .contains(QStringLiteral("none")),
             qPrintable(projectLine(&window, QStringLiteral("Target images"))));
    QVERIFY2(actionLabelled(&window, QStringLiteral("Run Correlation"))->isEnabled(),
             "an opened example still cannot be run");
}

void TestWorkspaceWalkthrough::a_session_saved_and_reopened_is_the_session_that_was_saved()
{
    // New, Open and Save Project sat on the File menu from the first window and
    // reported themselves unimplemented. A control that does nothing is worse
    // than one that is not there: it is a promise the application makes and
    // does not keep.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));
    window.addTargetImages({fixture(QStringLiteral("shift_target.tif"))});
    controlLabelled<QSpinBox>(&window, QStringLiteral("Subset radius"))->setValue(19);
    controlLabelled<QSpinBox>(&window, QStringLiteral("Grid step"))->setValue(13);

    auto *viewport = window.findChild<ImageViewport *>();
    actionLabelled(&window, QStringLiteral("Define ROI"))->trigger();
    for (const QPoint &pixel : {QPoint(60, 50), QPoint(170, 50),
                                QPoint(170, 110), QPoint(60, 110)}) {
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier,
                          widgetPointForPixel(viewport, pixel.x(), pixel.y()));
    }
    byVisibleText<QPushButton>(viewport, QStringLiteral("Close region"))->click();
    const RegionOfInterest saved = window.roi();
    QVERIFY(saved.isValid());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("session.svproj"));
    QVERIFY2(window.saveProjectTo(path), "saving the project reported failure");
    QVERIFY(QFile::exists(path));

    // A new project clears the session, so what comes back afterwards came from
    // the file rather than from what happened to still be on screen.
    QMenu *fileMenu = window.menuBar()->actions().first()->menu();
    QAction *fresh = nullptr;
    for (QAction *action : fileMenu->actions()) {
        if (action->text().contains(QStringLiteral("New Project")))
            fresh = action;
    }
    QVERIFY2(fresh, "there is no New Project");
    fresh->trigger();
    QVERIFY2(!window.roi().isValid(), "a new project kept the old region");
    QVERIFY2(projectLine(&window, QStringLiteral("Reference image"))
                 .contains(QStringLiteral("none")),
             "a new project kept the old reference image");

    QVERIFY2(window.openProjectFrom(path), "opening the project reported failure");

    QCOMPARE(window.roi().vertices, saved.vertices);
    QCOMPARE(controlLabelled<QSpinBox>(&window, QStringLiteral("Subset radius"))->value(), 19);
    QCOMPARE(controlLabelled<QSpinBox>(&window, QStringLiteral("Grid step"))->value(), 13);
    QVERIFY2(!projectLine(&window, QStringLiteral("Reference image"))
                  .contains(QStringLiteral("none")),
             "the reopened project has no reference image");
    QVERIFY2(actionLabelled(&window, QStringLiteral("Run Correlation"))->isEnabled(),
             "the reopened session cannot be run");
}

void TestWorkspaceWalkthrough::a_committed_region_can_be_adjusted_without_drawing_it_again()
{
    // A region could only be redrawn from scratch: one corner slightly wrong
    // meant placing every corner again.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));

    auto *viewport = window.findChild<ImageViewport *>();
    actionLabelled(&window, QStringLiteral("Define ROI"))->trigger();
    for (const QPoint &pixel : {QPoint(60, 50), QPoint(170, 50),
                                QPoint(170, 110), QPoint(60, 110)}) {
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier,
                          widgetPointForPixel(viewport, pixel.x(), pixel.y()));
    }
    byVisibleText<QPushButton>(viewport, QStringLiteral("Close region"))->click();
    QCOMPARE(window.roi().vertices.size(), 4);
    QCOMPARE(window.roi().vertices.at(1), QPoint(170, 50));

    // The screen has to say this is possible before a test may do it.
    QVERIFY2(projectLine(&window, QStringLiteral("Region of interest"))
                 .contains(QStringLiteral("drag"), Qt::CaseInsensitive),
             qPrintable(projectLine(&window, QStringLiteral("Region of interest"))));

    // Drag the second corner to a new place.
    const QPoint from = widgetPointForPixel(viewport, 170, 50);
    const QPoint to = widgetPointForPixel(viewport, 190, 40);
    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(viewport, to);
    QTest::qWait(30);
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, to);
    QTest::qWait(50);

    const RegionOfInterest adjusted = window.roi();
    QCOMPARE(adjusted.vertices.size(), 4);
    QVERIFY2(adjusted.vertices.at(1) != QPoint(170, 50),
             "the corner did not move");
    QVERIFY2(qAbs(adjusted.vertices.at(1).x() - 190) <= 2
                 && qAbs(adjusted.vertices.at(1).y() - 40) <= 2,
             qPrintable(QStringLiteral("corner landed at %1,%2")
                            .arg(adjusted.vertices.at(1).x())
                            .arg(adjusted.vertices.at(1).y())));

    // And the corners nobody touched stayed exactly where they were.
    QCOMPARE(adjusted.vertices.at(0), QPoint(60, 50));
    QCOMPARE(adjusted.vertices.at(2), QPoint(170, 110));
    QCOMPARE(adjusted.vertices.at(3), QPoint(60, 110));
}

void TestWorkspaceWalkthrough::the_second_pass_is_on_screen_and_says_what_it_does_and_costs()
{
    // ⚑ THE ONE FEATURE HERE THAT IS ON BY DEFAULT AND CHANGES THE RESULT.
    // Everything else the panel does happens because somebody asked for it. A
    // user who never opens this group still gets a fuller field than the solver
    // alone would produce, so the group has to be findable, has to say what it
    // does, and has to say what it costs -- otherwise it is exactly the hidden
    // behaviour the "can only use what he sees" rule forbids.
    MainWindow window;
    window.resize(1200, 900);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *retryBelow = controlLabelled<QDoubleSpinBox>(
        &window, QStringLiteral("Try again below this correlation"));
    auto *fitFrom = controlLabelled<QDoubleSpinBox>(
        &window, QStringLiteral("Fit from points at or above"));
    auto *rounds =
        controlLabelled<QSpinBox>(&window, QStringLiteral("Most rounds to try"));
    QVERIFY2(retryBelow && fitFrom && rounds,
             "the second pass has no controls on the Analysis panel, so a user "
             "cannot see that it runs or change how forgiving it is");

    // It says what it does, in the panel rather than in documentation nobody
    // has open.
    QVERIFY2(somethingOnScreenSays(&window, QStringLiteral("starting guess")),
             "the panel does not say why a point is worth trying again");
    QVERIFY2(somethingOnScreenSays(&window, QStringLiteral("rounds")),
             "the panel does not say the pass repeats");

    // And it says what it costs, which is the part nobody expects: that a
    // repaired point is a real measurement, marked, and that a worse answer is
    // never kept.
    QVERIFY2(somethingOnScreenSays(&window, QStringLiteral("marked as recovered")),
             "the panel does not say a repaired point is marked");
    QVERIFY2(somethingOnScreenSays(&window, QStringLiteral("better than the one it "
                                                          "replaces")),
             "the panel does not say the pass cannot spoil a point");

    // The neighbourhood is derived rather than typed, so it has to state where
    // its numbers came from -- and restate them when the grid step changes,
    // since that is what they are derived from.
    auto *gridStep = controlLabelled<QSpinBox>(&window, QStringLiteral("Grid step"));
    QVERIFY(gridStep);
    gridStep->setValue(3);
    QVERIFY2(somethingOnScreenSays(&window, QStringLiteral("within 12 px")),
             "the derived neighbourhood is not stated for a 3 px grid step");
    gridStep->setValue(5);
    QVERIFY2(somethingOnScreenSays(&window, QStringLiteral("within 20 px")),
             "the stated neighbourhood did not follow the grid step it is "
             "derived from, so the panel is describing a run that will not "
             "happen");
}

void TestWorkspaceWalkthrough::scrolling_the_analysis_panel_does_not_change_what_will_be_measured()
{
    // ⚑ FOUND BY SCREENSHOT, NOT BY TEST, 2026-09-02. The Analysis panel is
    // taller than its dock, so reaching the lower groups means scrolling -- and
    // a wheel event over a Qt spin box adjusts the spin box. Scrolling down to
    // READ the panel therefore silently changed the strain subregion radius
    // from 25.0 px to 11.0 px, with nothing on screen to say a setting had
    // moved and no reason for anyone to look.
    //
    // That is the worst shape of defect this application can have: not a wrong
    // answer, but a run quietly conducted under settings nobody chose. It gets
    // worse with every group added to the panel, because every group added
    // makes scrolling more necessary.
    MainWindow window;
    window.resize(1200, 700);   // deliberately short, so the panel must scroll
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *subsetRadius =
        controlLabelled<QSpinBox>(&window, QStringLiteral("Subset radius"));
    auto *strainRadius =
        controlLabelled<QDoubleSpinBox>(&window, QStringLiteral("Subregion radius"));
    auto *measure =
        controlLabelled<QComboBox>(&window, QStringLiteral("Strain measure"));
    QVERIFY(subsetRadius && strainRadius && measure);

    const int subsetBefore = subsetRadius->value();
    const double strainBefore = strainRadius->value();
    const int measureBefore = measure->currentIndex();

    // A wheel roll over each control, as it arrives while scrolling past.
    for (QWidget *control : {static_cast<QWidget *>(subsetRadius),
                             static_cast<QWidget *>(strainRadius),
                             static_cast<QWidget *>(measure)}) {
        for (int i = 0; i < 5; i++) {
            QWheelEvent wheel(QPointF(control->rect().center()),
                              control->mapToGlobal(control->rect().center()),
                              QPoint(0, -40), QPoint(0, -120),
                              Qt::NoButton, Qt::NoModifier,
                              Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(control, &wheel);
        }
    }

    QVERIFY2(subsetRadius->value() == subsetBefore,
             qPrintable(QStringLiteral("scrolling changed the subset radius "
                                       "from %1 to %2 px")
                            .arg(subsetBefore).arg(subsetRadius->value())));
    QVERIFY2(qFuzzyCompare(strainRadius->value(), strainBefore),
             qPrintable(QStringLiteral("scrolling changed the strain subregion "
                                       "from %1 to %2 px")
                            .arg(strainBefore).arg(strainRadius->value())));
    QVERIFY2(measure->currentIndex() == measureBefore,
             "scrolling changed the strain measure");

    // ...and the controls must still be usable deliberately, or the fix has
    // simply broken them. Clicking in gives focus, and a focused control keeps
    // its wheel.
    strainRadius->setFocus();
    QWheelEvent deliberate(QPointF(strainRadius->rect().center()),
                           strainRadius->mapToGlobal(strainRadius->rect().center()),
                           QPoint(0, -40), QPoint(0, -120),
                           Qt::NoButton, Qt::NoModifier,
                           Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(strainRadius, &deliberate);
    QVERIFY2(!qFuzzyCompare(strainRadius->value(), strainBefore),
             "a control the user has clicked into no longer takes the wheel, "
             "so the fix removed a way of setting it rather than protecting it");
}

void TestWorkspaceWalkthrough::the_repaired_points_can_be_seen_on_the_map_and_counted_beside_it()
{
    // The run report says HOW MANY points the second pass repaired. A reader's
    // next question is WHERE: repaired points clustered along one edge mean a
    // specimen problem, and repaired points scattered evenly mean a settings
    // one. Only a map answers that.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));
    window.addTargetImages({fixture(QStringLiteral("shift_target.tif"))});

    auto *viewport = window.findChild<ImageViewport *>();
    controlLabelled<QSpinBox>(&window, QStringLiteral("Grid step"))->setValue(12);

    actionLabelled(&window, QStringLiteral("Run Correlation"))->trigger();
    QVERIFY2(QTest::qWaitFor([viewport] { return viewport->hasField(); }, 120000),
             "the correlation produced no field within two minutes");

    auto *choice = viewport->findChild<QComboBox *>();
    QVERIFY(choice);

    const int index =
        choice->findText(fieldChannelName(FieldChannel::RecoveredOnSecondPass));
    QVERIFY2(index >= 0,
             "the selector does not offer the map of repaired points, so a "
             "reader can be told how many there were but never shown where");
    choice->setCurrentIndex(index);
    QCOMPARE(viewport->fieldChannel(), FieldChannel::RecoveredOnSecondPass);
    QVERIFY(viewport->hasField());

    // ⚑ And a count beside it, because the map alone cannot be read. On a pair
    // this easy almost nothing is repaired, so the map is very nearly uniform
    // -- and a uniform two-colour map is exactly as consistent with "nothing
    // needed repair" as with "the channel is broken". The number settles it,
    // which is the same reasoning as stating a field's range beside its colours.
    QVERIFY2(somethingOnScreenSays(&window, QStringLiteral("second pass")),
             "the repair map is on screen with no count beside it, so a nearly "
             "uniform map cannot be told from a broken one");
}

void TestWorkspaceWalkthrough::the_plot_panel_says_what_it_is_for_before_a_sequence_exists()
{
    // An empty chart is the least informative thing a panel can show. Before
    // anything has been measured this one has to say what would appear in it
    // and how to make that happen, or a reader meets a blank rectangle with no
    // way to tell whether it is broken, unimplemented, or waiting for them.
    MainWindow window;
    window.resize(1300, 900);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QVERIFY2(window.findChild<PlotPanel *>(),
             "there is no plot panel, so a sequence cannot be read as a curve");
    QVERIFY2(somethingOnScreenSays(&window, QStringLiteral("against frame")),
             "the empty plot does not say what it would plot");
    QVERIFY2(somethingOnScreenSays(&window, QStringLiteral("extensometer")),
             "the empty plot does not say an extensometer can be placed, so "
             "the feature exists only for somebody who already knows it does");

    // And the way to place one is on the toolbar, in words, not behind a
    // gesture nobody would guess.
    QVERIFY2(actionLabelled(&window, QStringLiteral("Extensometer")),
             "nothing on the toolbar places an extensometer");
}

void TestWorkspaceWalkthrough::an_extensometer_is_placed_by_clicking_and_plotted_over_the_sequence()
{
    // ⚑ Driven only by what the screen offers: press the toolbar action, read
    // the bar that appears, click the two points it asks for. If any step here
    // needed a coordinate or an order that nothing on screen describes, that
    // would be a UI defect found at the moment this was written.
    MainWindow window;
    window.resize(1300, 900);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));
    window.addTargetImages({fixture(QStringLiteral("shift_target.tif"))});

    auto *viewport = window.findChild<ImageViewport *>();
    controlLabelled<QSpinBox>(&window, QStringLiteral("Grid step"))->setValue(12);

    actionLabelled(&window, QStringLiteral("Run Correlation"))->trigger();
    QVERIFY2(QTest::qWaitFor([viewport] { return viewport->hasField(); }, 120000),
             "the correlation produced no field within two minutes");

    // Place the gauge the way the toolbar offers it.
    actionLabelled(&window, QStringLiteral("Extensometer"))->trigger();
    QVERIFY2(viewport->isPlacingExtensometer(),
             "pressing the toolbar action did not enter placement");
    QVERIFY2(somethingOnScreenSays(&window, QStringLiteral("FIRST point")),
             "placement mode does not say what to click, so the mode is a "
             "dead end for anyone who has not done it before");

    // Two clicks, well inside the picture so both anchors have measured points
    // all around them.
    const QVector<QPointF> anchors{QPointF(40, 60), QPointF(160, 60)};
    for (const QPointF &anchor : anchors) {
        QPointF at;
        QVERIFY2(viewport->widgetPositionForImagePixel(anchor, at),
                 "the anchor does not map onto the widget");
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, at.toPoint());
    }

    QVERIFY2(!viewport->isPlacingExtensometer(),
             "the second click did not finish the gauge, so placement never ends");

    auto *plot = window.findChild<PlotPanel *>();
    QVERIFY(plot);

    // The gauge is on the plot's selector by name, and picking it produces a
    // curve with a reading in it.
    auto *choice = plot->findChild<QComboBox *>();
    QVERIFY2(choice, "the plot panel offers no way to choose what it plots");
    const int index = choice->findText(QStringLiteral("E1"), Qt::MatchContains);
    QVERIFY2(index >= 0,
             qPrintable(QStringLiteral("the placed gauge is not on the plot's "
                                       "selector; it offers: %1")
                            .arg([choice] {
                                QStringList all;
                                for (int i = 0; i < choice->count(); i++)
                                    all << choice->itemText(i);
                                return all.join(QStringLiteral(", "));
                            }())));
    choice->setCurrentIndex(index);

    const Series series = plot->currentSeries();
    QVERIFY2(series.measuredCount() > 0,
             "the gauge read no frame at all, so placing it did nothing");
    QCOMPARE(series.points.size(), 1);   // one target was measured

    // ⚑ And the answer is RIGHT, not merely present. The fixture's target is
    // the reference shifted by a known +3 px in x, which is a rigid translation
    // - so a gauge across it must read a strain of ZERO. A gauge that reported
    // one anchor's displacement instead of the change in distance would read a
    // large strain here, and it would look entirely plausible on a curve.
    QVERIFY2(std::abs(series.points.first().value) < 1e-3,
             qPrintable(QStringLiteral("a rigid 3 px shift read as strain %1")
                            .arg(series.points.first().value)));

    // Whole-field curves are offered alongside, so a sequence can be read
    // without placing anything at all.
    QVERIFY2(choice->findText(QStringLiteral("Mean"), Qt::MatchContains) >= 0,
             "the plot offers no whole-field curve, so reading a sequence "
             "always requires placing a gauge first");
}

void TestWorkspaceWalkthrough::a_hole_can_be_cut_out_of_a_region_from_the_screen()
{
    // ⚑ The specimen this exists for ships with the application: the open-hole
    // tension example has two holes through it, and until now the region could
    // not say "not there" about anything. A point measured in a hole correlates
    // the background against itself and reports that nothing moved, which on a
    // strain map is a cold spot exactly where the stress concentrates.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));
    window.addTargetImages({fixture(QStringLiteral("shift_target.tif"))});

    auto *viewport = window.findChild<ImageViewport *>();
    auto *addHole = actionLabelled(&window, QStringLiteral("Add Hole"));
    QVERIFY2(addHole, "nothing on the toolbar cuts a hole out of a region");

    // ⚑ Dead until there is a region to cut it out of, and DISABLED rather than
    // failing when pressed: a control that does nothing when you press it is
    // indistinguishable from a broken one.
    QVERIFY2(!addHole->isEnabled(),
             "Add Hole is offered before any region exists");

    // Draw the outer boundary the way a user does.
    actionLabelled(&window, QStringLiteral("Define ROI"))->trigger();
    const QVector<QPointF> outer{QPointF(30, 30), QPointF(170, 30),
                                 QPointF(170, 120), QPointF(30, 120)};
    for (const QPointF &corner : outer) {
        QPointF at;
        QVERIFY(viewport->widgetPositionForImagePixel(corner, at));
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, at.toPoint());
    }
    // Finished from the mode bar's own button, as a user does. The bar carries
    // every way out of the mode, which is why the test never needs a shortcut.
    byVisibleText<QPushButton>(viewport, QStringLiteral("Close region"))->click();
    QVERIFY(window.roi().isValid());
    QVERIFY2(!window.roi().hasHoles(), "a fresh region has no holes");
    QVERIFY2(addHole->isEnabled(), "Add Hole stays dead once a region exists");

    // Now cut a hole out of it, with the same gesture.
    addHole->trigger();
    QVERIFY2(viewport->isDrawingHole(),
             "pressing Add Hole did not enter a drawing mode the user can see");
    const QVector<QPointF> hole{QPointF(80, 60), QPointF(120, 60),
                                QPointF(120, 90), QPointF(80, 90)};
    for (const QPointF &corner : hole) {
        QPointF at;
        QVERIFY(viewport->widgetPositionForImagePixel(corner, at));
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, at.toPoint());
    }
    byVisibleText<QPushButton>(viewport, QStringLiteral("Close region"))->click();

    QVERIFY2(window.roi().hasHoles(), "the ring did not become a hole");
    QCOMPARE(window.roi().vertices.size(), 4);   // the outer boundary is untouched

    // ⚑ A SECOND hole, because a specimen usually has more than one -- and
    // because the moment BEFORE its first corner is placed once corrupted VTK's
    // memory outright. With a hole already committed and none of the new ring
    // placed yet, moving the pointer drew a rubber band from corner "-1", and
    // the next render aborted the process with "double free or corruption".
    //
    // Found by driving the real application, not here: QTest::mouseClick jumps
    // straight to the press, so the suite never generated the pointer motion
    // that triggers it. The move below is what this case adds.
    //
    // NEGATIVE CHECK: with the `placed >= 1` guard removed this case does not
    // fail cleanly, it HANGS -- a corrupted heap does not abort on a schedule.
    // Worth knowing, because in CI that reads as a stuck runner rather than as
    // a defect.
    addHole->trigger();
    QVERIFY(viewport->isDrawingHole());
    {
        QPointF over;
        QVERIFY(viewport->widgetPositionForImagePixel(QPointF(70, 100), over));
        QTest::mouseMove(viewport, over.toPoint());
        QCoreApplication::processEvents();
    }
    const QVector<QPointF> second{QPointF(60, 95), QPointF(90, 95),
                                  QPointF(90, 110), QPointF(60, 110)};
    for (const QPointF &corner : second) {
        QPointF at;
        QVERIFY(viewport->widgetPositionForImagePixel(corner, at));
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, at.toPoint());
    }
    byVisibleText<QPushButton>(viewport, QStringLiteral("Close region"))->click();
    QCOMPARE(window.roi().holes.size(), 2);
    QVERIFY(!regionContains(window.roi(), 75, 100));

    // And the hole excludes: a point in it is outside the region, a point
    // beside it is not.
    QVERIFY(!regionContains(window.roi(), 100, 75));
    QVERIFY(regionContains(window.roi(), 50, 75));

    // Measure, and the run must actually place no point inside the hole.
    //
    // ⚑ A coarse grid on purpose. This case asks whether the ENGINE honours the
    // holes, not how dense a field can be, and the pre-commit hook builds
    // without optimisation -- at a fine step this one case took the hook from
    // two minutes to longer than anybody will wait, which is how a suite trains
    // its own authors to pass --no-verify. The hole is 40 by 30 px, so a 20 px
    // step still puts points in it if nothing excludes them.
    controlLabelled<QSpinBox>(&window, QStringLiteral("Grid step"))->setValue(20);
    actionLabelled(&window, QStringLiteral("Run Correlation"))->trigger();
    QVERIFY2(QTest::qWaitFor([viewport] { return viewport->hasField(); }, 120000),
             "the correlation produced no field within two minutes");

    for (const CorrelationPoint &point : window.lastResult().points) {
        QVERIFY2(!(point.x > 80 && point.x < 120 && point.y > 60 && point.y < 90),
                 qPrintable(QStringLiteral("a point was measured at (%1, %2), "
                                           "inside the hole")
                                .arg(double(point.x)).arg(double(point.y))));
    }
    QVERIFY2(window.lastResult().total() > 0, "and points were measured elsewhere");
}

void TestWorkspaceWalkthrough::the_viewport_says_which_way_its_axes_run()
{
    // ⚑ The convention every number in the application is expressed in, and
    // the one a reader is likeliest to assume wrongly, because every graph
    // they have ever met counts y upward and images count it down. CLAUDE.md
    // explains why it had to be pinned down and the manual has a chapter on
    // it; both are prose somebody has to go and find. This is the same fact
    // beside the picture it describes.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // Nothing to orient before an image exists, and a convention with nothing
    // to apply it to would be trivia sitting on an empty workspace.
    QVERIFY2(!somethingOnScreenSays(&window, QStringLiteral("y down")),
             "the frame legend is shown before there is any picture to orient");

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));

    QVERIFY2(somethingOnScreenSays(&window, QStringLiteral("y down")),
             "nothing on screen says which way y runs once an image is shown");
    QVERIFY2(somethingOnScreenSays(&window, QStringLiteral("x right")),
             "nothing on screen says which way x runs");
    QVERIFY2(somethingOnScreenSays(&window, QStringLiteral("top-left")),
             "nothing on screen says where the origin sits, and two directions "
             "without an origin still leave a point unplaceable");

    // ⚑ And it must not be drawn over the picture it describes. The legend is
    // an overlay, and every overlay in this viewport has at some point grown
    // over the specimen it was explaining - the field bar and the region bar
    // both did, and both were found by looking rather than by a test.
    auto *viewport = window.findChild<ImageViewport *>();
    QVERIFY(viewport);
    for (QFrame *frame : viewport->findChildren<QFrame *>()) {
        if (!frame->isVisible() || frame->height() == 0)
            continue;
        QVERIFY2(frame->height() < viewport->height() / 3,
                 qPrintable(QStringLiteral("an overlay is %1 px tall in a %2 px "
                                           "viewport, which is eating the picture")
                                .arg(frame->height()).arg(viewport->height())));
    }
}


void TestWorkspaceWalkthrough::the_comparison_with_a_known_answer_says_why_it_is_unavailable_before_a_run()
{
    // ⚑ Visible before it is usable, and explaining itself while it is not. A
    // capability that appears only once its preconditions happen to be met is
    // one nobody can find on purpose -- the rule this suite is written under.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QAction *compare = menuActionLabelled(&window, QStringLiteral("known answer"));
    QVERIFY2(compare, "no menu offers a comparison with the known answer");
    QVERIFY(!compare->isEnabled());
    QVERIFY2(compare->toolTip().contains(QStringLiteral("correlation"),
                                         Qt::CaseInsensitive),
             qPrintable(compare->toolTip()));
}

void TestWorkspaceWalkthrough::an_example_that_states_its_own_answer_is_measured_against_it_on_screen()
{
    // The claim this screen exists to make: what SurView measured, the answer
    // the example states, and the difference between them, all visible at once.
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QAction *translation = exampleNamed(&window, QStringLiteral("Translation"));
    QVERIFY2(translation, "the synthetic translation example is not on the menu");
    translation->trigger();

    // Coarse, from the control on screen: this case is about the comparison,
    // not about how dense a field can be.
    auto *gridStep = controlLabelled<QSpinBox>(&window, QStringLiteral("Grid step"));
    QVERIFY(gridStep);
    gridStep->setValue(20);

    QAction *run = actionLabelled(&window, QStringLiteral("Run Correlation"));
    QVERIFY(waitForEnabled(run));
    run->trigger();

    QAction *compare = menuActionLabelled(&window, QStringLiteral("known answer"));
    QVERIFY(compare);
    QVERIFY2(waitForEnabled(compare, 120000),
             qPrintable(QStringLiteral("the comparison stayed unavailable: %1")
                            .arg(compare->toolTip())));
    compare->trigger();

    auto *comparison = window.findChild<ComparisonWindow *>();
    QVERIFY2(comparison, "nothing opened");
    QVERIFY(QTest::qWaitForWindowExposed(comparison));

    const QString said = visibleText(comparison);

    // It names the frame it is comparing, so a reader knows which answer this
    // is. Without that the screen is three pictures of an unnamed experiment.
    QVERIFY2(said.contains(QStringLiteral("translation_0")), qPrintable(said));

    // All three panels are labelled, including the one whose meaning is least
    // guessable.
    QVERIFY2(said.contains(QStringLiteral("Measured minus stated")), qPrintable(said));
    QVERIFY2(said.contains(QStringLiteral("Stated by the example")), qPrintable(said));

    // ⚑ And it says why the stated answer is exact, in the example's own words.
    // Without that sentence a reader has no reason to read the third panel as
    // an ERROR rather than as a disagreement between two estimates.
    QVERIFY2(said.contains(QStringLiteral("resampled")), qPrintable(said));

    // The comparison the screen made, asserted as a measurement rather than as
    // text: this example is a rigid sub-pixel translation, and SurView measures
    // it to well inside a tenth of a pixel.
    const AccuracyReport report = comparison->report();
    QVERIFY2(report.valid, "the screen compared nothing");
    QVERIFY2(report.compared > 100,
             qPrintable(QStringLiteral("only %1 points compared").arg(report.compared)));
    QVERIFY2(report.worstAbsolute < 0.1,
             qPrintable(QStringLiteral("worst point %1 px from the stated answer")
                            .arg(report.worstAbsolute)));

    // The counts reach the screen too, not only the pictures.
    QVERIFY2(said.contains(QString::number(report.compared)), qPrintable(said));
}

void TestWorkspaceWalkthrough::a_dataset_that_states_no_answer_says_so_rather_than_offering_a_comparison()
{
    // ⚑ The real examples are photographs of real specimens, and nothing states
    // what they did. Offered a comparison anyway, against a default answer of no
    // deformation, the screen would report the specimen's entire movement as the
    // instrument's error -- a confident, plausible, completely wrong verdict.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));
    window.addTargetImages({fixture(QStringLiteral("shift_target.tif"))});

    QAction *run = actionLabelled(&window, QStringLiteral("Run Correlation"));
    QVERIFY(waitForEnabled(run));
    run->trigger();

    QAction *exportAction = actionLabelled(&window, QStringLiteral("Export Results (.vtu)"));
    QVERIFY(waitForEnabled(exportAction, 120000));

    QAction *compare = menuActionLabelled(&window, QStringLiteral("known answer"));
    QVERIFY(compare);
    QVERIFY2(!compare->isEnabled(),
             "a comparison was offered against data that states no answer");
    QVERIFY2(compare->toolTip().contains(QStringLiteral("states no answer"),
                                         Qt::CaseInsensitive)
                 || compare->toolTip().contains(QStringLiteral("no stated answer"),
                                                Qt::CaseInsensitive),
             qPrintable(compare->toolTip()));
}


void TestWorkspaceWalkthrough::the_subset_can_be_seen_at_its_real_size_before_any_run()
{
    // ⚑ BEFORE a run, which is the whole point: the question it answers -- is
    // this square big enough to hold distinct pattern -- has to be answerable
    // while the settings are being chosen, not after a correlation has been sat
    // through.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openReferenceImage(fixture(QStringLiteral("speckle_patch.tif")));

    auto *viewport = window.findChild<ImageViewport *>();
    QVERIFY(viewport);
    QVERIFY2(!viewport->settingsPreview().valid,
             "the subset was drawn before anything asked for it");

    // Found the way a reader finds it: by the words on the switch.
    auto *show = byVisibleText<QCheckBox>(&window, QStringLiteral("Show the subset"));
    QVERIFY2(show, "nothing on the panel offers to show the subset");
    QVERIFY(!show->isChecked());
    QVERIFY(!show->toolTip().isEmpty());

    show->setChecked(true);

    const SubsetOverlay drawn = viewport->settingsPreview();
    QVERIFY2(drawn.valid, "the switch was turned on and nothing was drawn");

    // The square is the subset the engine will correlate: 2r + 1 px across.
    auto *radius = controlLabelled<QSpinBox>(&window, QStringLiteral("Subset radius"));
    QVERIFY(radius);
    QCOMPARE(drawn.subsetRadius, radius->value());
    QCOMPARE(drawn.subsetSide(), 2.0 * radius->value() + 1.0);
}

void TestWorkspaceWalkthrough::the_drawn_subset_follows_the_radius_that_will_be_measured()
{
    // A preview that kept the size it was switched on at would be the most
    // misleading thing on the screen: it would show one subset while the run
    // measured another, and look right doing it.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openReferenceImage(fixture(QStringLiteral("speckle_patch.tif")));

    auto *viewport = window.findChild<ImageViewport *>();
    auto *show = byVisibleText<QCheckBox>(&window, QStringLiteral("Show the subset"));
    auto *radius = controlLabelled<QSpinBox>(&window, QStringLiteral("Subset radius"));
    QVERIFY(viewport && show && radius);

    show->setChecked(true);
    radius->setValue(9);
    QCOMPARE(viewport->settingsPreview().subsetRadius, 9);
    QCOMPARE(viewport->settingsPreview().subsetSide(), 19.0);

    radius->setValue(24);
    QCOMPARE(viewport->settingsPreview().subsetRadius, 24);
}

void TestWorkspaceWalkthrough::the_points_drawn_in_the_subregion_are_the_ones_the_panel_counts()
{
    // ⚑ THE CASE THIS FEATURE MOST NEEDS. The panel states how many points the
    // strain fit has in words and the image draws them as dots, so the same
    // fact is now on screen twice. Two statements of one number that can
    // disagree are worse than one, because a reader has no way to tell which is
    // lying.
    MainWindow window;
    window.resize(1200, 900);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openReferenceImage(fixture(QStringLiteral("speckle_patch.tif")));

    auto *viewport = window.findChild<ImageViewport *>();
    auto *show = byVisibleText<QCheckBox>(&window, QStringLiteral("Show the subregion"));
    QVERIFY(viewport);
    QVERIFY2(show, "nothing on the panel offers to show the strain subregion");

    auto *radius = controlLabelled<QDoubleSpinBox>(&window,
                                                   QStringLiteral("Subregion radius"));
    auto *step = controlLabelled<QSpinBox>(&window, QStringLiteral("Grid step"));
    QVERIFY(radius && step);
    // Wide enough that the neighbourhood is several rings, and a step that
    // divides it unevenly, so a count that was estimated rather than walked
    // would differ.
    step->setValue(7);
    radius->setValue(30.0);
    show->setChecked(true);

    const SubsetOverlay drawn = viewport->settingsPreview();
    QVERIFY(drawn.valid);
    QVERIFY(drawn.hasSubregion);
    QVERIFY(!drawn.neighbours.isEmpty());

    // The number the panel states, read off the panel rather than recomputed.
    const int stated = gridPointsInSubregion(30.0, 7);
    QVERIFY2(somethingOnScreenSays(&window, QString::number(stated)),
             "the panel does not say how many points the subregion holds");
    QCOMPARE(int(drawn.neighbours.size()), stated);

    // ⚑ The marks appear only while the grid is open enough on screen to tell
    // them apart. At a fine step a subregion holds dozens of points inside a
    // circle a few dozen pixels wide, and drawing them all turned the
    // neighbourhood into one solid blob that hid the speckle it was drawn over.
    // Found by looking at the screen; pinned here through the two states one
    // screen can be in rather than by re-deriving the projection.
    step->setValue(40);
    QVERIFY2(viewport->settingsPreviewShowsNeighbours(),
             "a wide-open grid still refused to mark its points");
    step->setValue(2);
    QVERIFY2(!viewport->settingsPreviewShowsNeighbours(),
             "a grid too fine to tell apart was drawn point by point anyway");
    // The count on the panel is exact either way, which is what makes hiding
    // the marks honest rather than a loss.
    QVERIFY(somethingOnScreenSays(&window, QString::number(gridPointsInSubregion(30.0, 2))));

    step->setValue(7);

    // And switching strain off takes the subregion with it: a circle drawn
    // while nothing will be fitted would show a neighbourhood nobody averages
    // over.
    auto *strain = byVisibleText<QCheckBox>(&window, QStringLiteral("Fit strain"));
    QVERIFY(strain);
    strain->setChecked(false);
    QVERIFY(!viewport->settingsPreview().hasSubregion);
}


void TestWorkspaceWalkthrough::what_the_camera_recorded_is_readable_before_any_correlation()
{
    // ⚑ BEFORE a run. The point panel could say nothing at all until a
    // correlation had been sat through, and the question that comes first is
    // about the photograph: is this exposed properly, does the speckle here
    // carry contrast? The Record panel answers that for the image as a whole
    // and cannot answer it anywhere in particular.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // The panel names the gesture before there is anything to use it on.
    QVERIFY2(pointPanelText(&window).contains(QStringLiteral("camera"),
                                              Qt::CaseInsensitive),
             qPrintable(pointPanelText(&window)));

    window.openReferenceImage(fixture(QStringLiteral("speckle_patch.tif")));

    auto *viewport = window.findChild<ImageViewport *>();
    QVERIFY(viewport);
    const ImageRecord &record = viewport->record();
    QVERIFY(record.isValid());

    // ⚑ READ FROM A CLICK, NEVER A HOVER. A synthetic mouse move races with the
    // X server's own pointer motion and the last one wins, so the panel answers
    // about wherever the pointer actually sat - which is how this case came to
    // fail on a loaded machine while passing on a quiet one. The rule was
    // established on 2026-09-02 and applied to every other reading in this
    // suite; this case was missed, and went on reading a hover until it was
    // caught here on 2026-09-11.
    //
    // A click is unambiguous, and it is what the panel itself instructs: "Click
    // to pin a reading so it stays while you look away". The walkthrough is
    // still only doing what the screen tells it to.
    const QPoint at = widgetPointForPixel(viewport, record.width / 2.0,
                                          record.height / 2.0);

    // ⚑ What the panel says BEFORE anything is pinned, which is where it tells a
    // reader what comes next. Once a reading is pinned the panel is showing that
    // reading and the invitation it replaced is no longer on screen, so the
    // sentence about a run adding a measurement is asked for here, in the state
    // that carries it, rather than demanded of a state that does not.
    const QString inviting = pointPanelText(&window);
    QVERIFY2(inviting.contains(QStringLiteral("measure"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("before a run, the panel does not say a "
                                       "correlation will add a measurement at "
                                       "the same point:\n%1").arg(inviting)));

    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, at);
    QTest::qWait(80);

    const QString said = pointPanelText(&window);

    // The value the FILE holds, and the type it is stored in -- not the
    // stretched brightness on screen. Checked against the pixel read straight
    // from the viewport, which is the same number correlation would use.
    //
    // ⚑ Sampled at the pixel the PANEL says it is reporting on, not at the one
    // this case aimed for: a synthetic pointer lands on whichever pixel the
    // projection puts under it, and comparing against a different pixel would
    // make this case fail for a reason that has nothing to do with the readout.
    static const QRegularExpression pixelLine(
        QStringLiteral("[Aa]t pixel (\\d+), (\\d+)"));
    int readX = -1;
    int readY = -1;
    const QRegularExpressionMatch match = pixelLine.match(said);
    if (match.hasMatch()) {
        readX = match.captured(1).toInt();
        readY = match.captured(2).toInt();
    }
    QVERIFY2(readX >= 0, qPrintable(QStringLiteral("the panel names no pixel:\n%1")
                                        .arg(said)));

    const QVector<double> sampled = viewport->sampleImageAt(readX, readY);
    QVERIFY2(!sampled.isEmpty(), "the viewport could not sample its own image");
    QVERIFY2(said.contains(QString::number(qint64(sampled.first()))),
             qPrintable(QStringLiteral("the panel does not report the file's own "
                                       "value (%1):\n%2")
                            .arg(sampled.first()).arg(said)));
    QVERIFY2(said.contains(record.pixelTypeName()), qPrintable(said));

    // And the pinned reading says it is pinned, so the reader knows why it is
    // staying put and how to let it go.
    QVERIFY2(said.contains(QStringLiteral("pinned"), Qt::CaseInsensitive),
             qPrintable(said));
}

void TestWorkspaceWalkthrough::a_camera_reading_can_be_pinned_without_a_field_to_read()
{
    // The panel invites a click from the moment an image is on screen. A click
    // that did nothing until a correlation existed would be a gesture the panel
    // offers and the window refuses.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openReferenceImage(fixture(QStringLiteral("speckle_patch.tif")));

    auto *viewport = window.findChild<ImageViewport *>();
    const ImageRecord &record = viewport->record();
    const QPoint at = widgetPointForPixel(viewport, record.width / 2.0,
                                          record.height / 2.0);

    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, at);
    QTest::qWait(80);
    const QString pinned = pointPanelText(&window);
    QVERIFY2(pinned.contains(QStringLiteral("pinned"), Qt::CaseInsensitive),
             qPrintable(pinned));

    // Leaving the picture does not disturb it -- which is the whole point.
    QTest::mouseMove(viewport, QPoint(3, 3));
    QTest::qWait(80);
    const QString afterLeaving = pointPanelText(&window);
    QVERIFY2(afterLeaving.contains(pinned.section(QLatin1Char('\n'), 1, 3)),
             qPrintable(QStringLiteral("the pinned pixel changed when the pointer "
                                       "left:\nwas:\n%1\nnow:\n%2")
                            .arg(pinned, afterLeaving)));
    QVERIFY2(afterLeaving.contains(QStringLiteral("release"), Qt::CaseInsensitive),
             qPrintable(afterLeaving));
}

void TestWorkspaceWalkthrough::the_pixel_read_out_is_the_one_the_file_holds_at_that_position()
{
    // ⚑ WRITTEN BECAUSE A NEGATIVE CHECK EXPOSED A GAP. The case above compares
    // what the panel says against what the viewport samples, so a sampler that
    // scaled every value would satisfy both halves and stay green. The
    // expectation here comes from the FIXTURE instead: row_order_marker.tif is
    // 8x8 with a bright 3x3 block at the picture's TOP-LEFT and one mid-grey
    // pixel at the TOP-RIGHT, asymmetric in both axes and independently checked
    // in tests/test_image_decode.cpp.
    //
    // That also makes this the one case that can catch a reading taken from a
    // flipped image: at (1, 1) the marker reads 255 and its mirror reads 0.
    constexpr int kMarker = 255;   // top-left 3x3
    constexpr int kTell = 128;     // single pixel, top-right

    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openReferenceImage(fixture(QStringLiteral("row_order_marker.tif")));

    auto *viewport = window.findChild<ImageViewport *>();
    QVERIFY(viewport);
    QCOMPARE(viewport->record().width, 8);

    QCOMPARE(viewport->sampleImageAt(1, 1), QVector<double>{double(kMarker)});
    QCOMPARE(viewport->sampleImageAt(1, 6), QVector<double>{0.0});
    QCOMPARE(viewport->sampleImageAt(7, 0), QVector<double>{double(kTell)});

    // And the number a reader actually sees is that one. Clicked rather than
    // hovered: a click names the pixel it landed on without racing the X
    // server's own pointer.
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier,
                      widgetPointForPixel(viewport, 1.0, 1.0));
    QTest::qWait(80);
    const QString said = pointPanelText(&window);
    QVERIFY2(said.contains(QString::number(kMarker)), qPrintable(said));

    // The brightest value the type allows is also the top of this image's own
    // range, so the readout warns rather than reporting it as an ordinary
    // brightness.
    QVERIFY2(said.contains(QStringLiteral("gradient"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("a saturated pixel was reported without "
                                       "saying what that costs:\n%1").arg(said)));
}

void TestWorkspaceWalkthrough::clicking_another_pixel_moves_the_pin_rather_than_releasing_it()
{
    // ⚑ FOUND BY DRIVING THE APPLICATION. With no field measured, every point
    // index is -1, so a release rule that compared indices treated any two
    // pixels as the same reading: clicking around the picture toggled one
    // reading on and off instead of moving it, and the panel kept showing the
    // first pixel clicked while appearing to respond.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openReferenceImage(fixture(QStringLiteral("row_order_marker.tif")));

    auto *viewport = window.findChild<ImageViewport *>();
    QVERIFY(viewport);

    // Two pixels the fixture states different values for: the bright block at
    // the top left, and a dark one lower down.
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier,
                      widgetPointForPixel(viewport, 1.0, 1.0));
    QTest::qWait(60);
    const QString first = pointPanelText(&window);
    QVERIFY2(first.contains(QStringLiteral("255")), qPrintable(first));

    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier,
                      widgetPointForPixel(viewport, 1.0, 6.0));
    QTest::qWait(60);
    const QString second = pointPanelText(&window);
    QVERIFY2(second.contains(QStringLiteral("pinned"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("clicking another pixel released the pin "
                                       "instead of moving it:\n%1").arg(second)));
    QVERIFY2(second.contains(QStringLiteral("At pixel 1, 6")), qPrintable(second));

    // And clicking the same pixel again does release it, which is the way out
    // the panel names.
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier,
                      widgetPointForPixel(viewport, 1.0, 6.0));
    QTest::qWait(60);
    QVERIFY2(!pointPanelText(&window).contains(QStringLiteral("Pinned"),
                                               Qt::CaseSensitive),
             qPrintable(pointPanelText(&window)));
}

void TestWorkspaceWalkthrough::the_window_carries_the_application_icon()
{
    // ⚑ A desktop entry names an icon and a task list shows one; an application
    // that ships an icon file and never sets it looks, on screen, exactly like
    // one that has none. Asked of the icon's own PIXELS rather than of the
    // QIcon being non-null: a QIcon built from a path nothing can decode is
    // perfectly non-null and draws nothing, which is how a missing image-format
    // plugin would slip through.
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const QIcon icon = window.windowIcon();
    QVERIFY2(!icon.isNull(), "the window has no icon at all");

    const QPixmap drawn = icon.pixmap(64, 64);
    QVERIFY2(!drawn.isNull() && drawn.width() > 0,
             "the window icon renders to nothing, so nothing will be drawn for it");
}

void TestWorkspaceWalkthrough::the_speckle_estimate_says_what_it_needs_before_any_image_exists()
{
    // ⚑ FOUND IN A SCREENSHOT OF THE ONE STATE NEVER SCREENSHOTTED: the empty
    // workspace. The estimate's standing invitation was written and never set
    // until something else refreshed the panel, so a first-run window showed a
    // BLANK GAP where a line of guidance belongs -- and a blank gap is read as a
    // layout fault rather than as a control waiting for input.
    //
    // Nothing else in this suite could see it: every other case loads an image
    // first, which is exactly what fills the label in.
    MainWindow window;
    window.resize(1200, 900);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QVERIFY2(somethingOnScreenSays(&window, QStringLiteral("speckle")),
             "the empty workspace says nothing about what the speckle estimate "
             "needs");

    // And an empty label may not sit there holding space either: a row with no
    // text is a hole in the form.
    for (QLabel *label : window.findChildren<QLabel *>()) {
        if (label->isVisible() && label->text().isEmpty()) {
            QVERIFY2(label->height() <= 1,
                     qPrintable(QStringLiteral("an empty label is holding %1 px "
                                               "of the panel open")
                                    .arg(label->height())));
        }
    }
}

void TestWorkspaceWalkthrough::the_empty_workspace_offers_the_examples_it_ships_with()
{
    // ⚑ Step 1 of the first-run card is "import the reference image", and a
    // reader who has no speckle images of their own cannot take it. The
    // examples ship with the application and were reachable only from a menu
    // nobody had a reason to open. The card says so now, where that reader is
    // standing when they find out they have nothing to import.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *offer = byVisibleText<QPushButton>(&window, QStringLiteral("example"));
    QVERIFY2(offer, "the empty workspace does not mention the examples it ships");
    QVERIFY(offer->isVisible());

    // And it raises the SAME list the File menu carries, not a second copy of
    // it: two lists of what is on disk would eventually disagree.
    QSignalSpy asked(window.findChild<ImageViewport *>(),
                     &ImageViewport::openExampleRequested);
    QVERIFY(asked.isValid());
    QTest::mouseClick(offer, Qt::LeftButton);
    QCOMPARE(asked.count(), 1);

    // Once an image is loaded there is nothing left to offer, and the card goes
    // with it rather than floating over the picture.
    window.openReferenceImage(fixture(QStringLiteral("speckle_patch.tif")));
    QVERIFY2(!offer->isVisible(),
             "the first-run card stayed on screen over the image it invited");
}

void TestWorkspaceWalkthrough::a_panel_section_folds_away_and_says_what_it_still_holds()
{
    // The Analysis panel has five groups, and a reader looking for one control
    // scrolls past four sets of settings they have already decided about.
    MainWindow window;
    window.resize(1200, 900);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const QList<CollapsibleSection *> sections =
        window.findChildren<CollapsibleSection *>();
    QVERIFY2(sections.size() >= 4,
             qPrintable(QStringLiteral("only %1 foldable sections").arg(sections.size())));

    // Every one of them is folded and unfolded from a control that SAYS it can
    // be: a header nobody can see is a mode with no way in.
    for (CollapsibleSection *section : sections) {
        auto *header = section->findChild<QToolButton *>();
        QVERIFY2(header, "a section has no header to press");
        QVERIFY2(!header->text().isEmpty(), "a section header has no name");
        QVERIFY2(!header->toolTip().isEmpty(), qPrintable(header->text()));
    }

    // ⚑ And a folded section says what it is holding. Folding away a 16 px
    // subset must not make the panel read as though nothing had been chosen:
    // the settings are still in force, out of sight.
    CollapsibleSection *correlation = sections.first();
    correlation->setCollapsed(true);
    QVERIFY(correlation->isCollapsed());

    auto *radius = controlLabelled<QSpinBox>(&window, QStringLiteral("Subset radius"));
    QVERIFY(radius);
    QVERIFY2(!radius->isVisible(), "folding the section left its controls on screen");

    const QString said = visibleText(correlation);
    QVERIFY2(said.contains(QString::number(radius->value())),
             qPrintable(QStringLiteral("a folded section does not say what it holds:\n%1")
                            .arg(said)));
}

void TestWorkspaceWalkthrough::a_folded_section_still_governs_the_run()
{
    // ⚑ THE CASE THAT MAKES FOLDING SAFE. Hiding a control does not change it,
    // and must not: a run measured under settings that are out of sight is
    // still measured under those settings. The opposite mistake -- a folded
    // section quietly ceasing to apply -- would be the worst defect this
    // application can have, a run conducted under settings nobody chose.
    MainWindow window;
    window.resize(1200, 900);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));
    window.addTargetImages({fixture(QStringLiteral("shift_target.tif"))});
    controlLabelled<QSpinBox>(&window, QStringLiteral("Grid step"))->setValue(20);

    auto *radius = controlLabelled<QSpinBox>(&window, QStringLiteral("Subset radius"));
    radius->setValue(12);
    for (CollapsibleSection *section : window.findChildren<CollapsibleSection *>())
        section->setCollapsed(true);

    QAction *run = actionLabelled(&window, QStringLiteral("Run Correlation"));
    QVERIFY(waitForEnabled(run));
    run->trigger();
    QVERIFY2(QTest::qWaitFor([&window] { return window.measuredFrames() >= 1; }, 120000),
             "the run measured nothing");

    const CorrelationResult &result = window.lastResult();
    QVERIFY2(result.converged > 0, "a run under folded settings measured nothing");
    QVERIFY2(result.strainRequested,
             "the folded Strain section stopped being asked for");
    QVERIFY2(result.strainFitted > 0, "no strain was fitted under folded settings");
}

void TestWorkspaceWalkthrough::the_region_says_it_can_be_adjusted_where_the_panel_can_show_it()
{
    // ⚑ A CAPABILITY NOBODY CAN SEE IS ONE THIS PROJECT TREATS AS ABSENT. The
    // corners of a committed region can be dragged, the handles look grabbable
    // and the cursor changes over one - but a reader has to be over a corner
    // already to learn any of that, so the project tree is what says so.
    //
    // ⚑ AND IT HAS TO FIT. Written as one line the sentence was the half the
    // panel threw away: at the dock's own width the item elided to "Region of
    // interest - drawn by hand, 4 corn...", and the comment beside the code
    // claimed the tree told the reader something the screen did not. Found by
    // driving the application and looking at it, 2026-09-11.
    //
    // This case cannot see an elision - the text is in the item either way -
    // so it pins the property that MAKES the sentence survivable: the
    // affordance is an item of its own, short, rather than a tail on a line
    // that also carries the facts. A line long enough to be elided is a line
    // this rule has stopped protecting.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));

    auto *viewport = window.findChild<ImageViewport *>();
    const QPoint centre(viewport->width() / 2, viewport->height() / 2);
    actionLabelled(&window, QStringLiteral("Define ROI"))->trigger();
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, centre);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, centre + QPoint(60, 0));
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, centre + QPoint(60, 40));
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, centre + QPoint(0, 40));
    byVisibleText<QPushButton>(viewport, QStringLiteral("Close region"))->click();

    QTreeWidgetItem *region = nullptr;
    for (QTreeWidget *tree : window.findChildren<QTreeWidget *>()) {
        for (int i = 0; i < tree->topLevelItemCount(); i++) {
            QTreeWidgetItem *top = tree->topLevelItem(i);
            if (top->text(0).startsWith(QStringLiteral("Region of interest")))
                region = top;
        }
    }
    QVERIFY2(region, "the project does not list the region of interest at all");

    QStringList lines;
    for (int c = 0; c < region->childCount(); c++)
        lines << region->child(c)->text(0);

    // ⚑ ALL THREE GESTURES, each in words. Moving a corner, adding one and
    // taking one out are three different things a region answers to, and not
    // one of them is visible on the image: a reader has no reason to try an
    // edge or a right-click unless the panel says so.
    for (const QString &gesture : {QStringLiteral("drag"),
                                   QStringLiteral("add a corner"),
                                   QStringLiteral("take it out")}) {
        QVERIFY2(!lines.filter(gesture, Qt::CaseInsensitive).isEmpty(),
                 qPrintable(QStringLiteral("nothing under the region mentions "
                                           "\"%1\"; it lists: %2")
                                .arg(gesture)
                                .arg(lines.join(QStringLiteral(" | ")))));
    }

    // ⚑ Expanded, not folded. A hint behind a disclosure arrow is a hint nobody
    // has, which is the same rule the Analysis panel follows for a section that
    // acts.
    QVERIFY2(region->isExpanded(),
             "the region's own lines are folded away, so the affordance is "
             "hidden behind a disclosure arrow");

    // And the parent stays short enough for a dock to show it. The number is
    // the dock's own order of magnitude rather than a measured pixel width:
    // what is being prevented is a line that grows until the panel eats its
    // tail, which is exactly what happened.
    QVERIFY2(region->text(0).size() < 60,
             qPrintable(QStringLiteral("the region's line is %1 characters and "
                                       "will be elided by the dock: %2")
                            .arg(region->text(0).size())
                            .arg(region->text(0))));
}

void TestWorkspaceWalkthrough::a_corner_can_be_added_to_an_edge_and_taken_out_again_on_the_image()
{
    // ⚑ ONE CORNER TOO FEW USED TO COST THE WHOLE BOUNDARY. Moving a corner was
    // the only edit a committed region allowed, so following a curve a little
    // better, or working round a fixture that turned out to be in shot, meant
    // placing every corner again from the first.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));

    auto *viewport = window.findChild<ImageViewport *>();
    actionLabelled(&window, QStringLiteral("Define ROI"))->trigger();
    for (const QPoint &pixel : {QPoint(60, 50), QPoint(170, 50),
                                QPoint(170, 110), QPoint(60, 110)}) {
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier,
                          widgetPointForPixel(viewport, pixel.x(), pixel.y()));
    }
    byVisibleText<QPushButton>(viewport, QStringLiteral("Close region"))->click();
    QCOMPARE(window.roi().vertices.size(), 4);

    // The screen has to say both gestures are possible before a test may use
    // them, which is the rule this whole suite is written under.
    const QString said = projectLine(&window, QStringLiteral("Region of interest"));
    QVERIFY2(said.contains(QStringLiteral("add a corner"), Qt::CaseInsensitive),
             qPrintable(said));
    QVERIFY2(said.contains(QStringLiteral("take it out"), Qt::CaseInsensitive),
             qPrintable(said));

    // Halfway down the right-hand edge, which is far from both of its ends -
    // the reason the viewport asks which EDGE is under the pointer rather than
    // which corner is nearest.
    const QPoint onTheEdge = widgetPointForPixel(viewport, 170, 80);
    QTest::mouseDClick(viewport, Qt::LeftButton, Qt::NoModifier, onTheEdge);
    QTest::qWait(50);

    const RegionOfInterest grown = window.roi();
    QCOMPARE(grown.vertices.size(), 5);
    // ⚑ In the ring's own order, between the corners whose edge it was placed
    // on. Appended at the end instead, the boundary crosses itself and what
    // counts as inside stops meaning what the reader drew.
    QCOMPARE(grown.vertices.at(1), QPoint(170, 50));
    QCOMPARE(grown.vertices.at(3), QPoint(170, 110));
    QVERIFY2(qAbs(grown.vertices.at(2).x() - 170) <= 2
                 && qAbs(grown.vertices.at(2).y() - 80) <= 2,
             qPrintable(QStringLiteral("the new corner landed at %1,%2")
                            .arg(grown.vertices.at(2).x())
                            .arg(grown.vertices.at(2).y())));

    // And out again, on the corner itself.
    QTest::mouseClick(viewport, Qt::RightButton, Qt::NoModifier, onTheEdge);
    QTest::qWait(50);
    QCOMPARE(window.roi().vertices.size(), 4);
}

void TestWorkspaceWalkthrough::a_refusal_to_remove_a_corner_does_not_take_the_picture_with_it()
{
    // ⚑ WRITTEN BECAUSE THE FIRST VERSION DID EXACTLY THAT. The refusal was
    // reported through ImageViewport::showMessage(), which is the viewport's
    // EMPTY-STATE text: it tears down the image and the boundary with it,
    // because a message there means there are no pixels to show. Right-clicking
    // the third corner of a triangle wiped the specimen off the screen.
    //
    // Found by driving the application and looking at it. No assertion about
    // the region would have noticed: the region was correctly left alone.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));

    auto *viewport = window.findChild<ImageViewport *>();
    actionLabelled(&window, QStringLiteral("Define ROI"))->trigger();
    for (const QPoint &pixel : {QPoint(60, 50), QPoint(170, 50), QPoint(60, 110)}) {
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier,
                          widgetPointForPixel(viewport, pixel.x(), pixel.y()));
    }
    byVisibleText<QPushButton>(viewport, QStringLiteral("Close region"))->click();
    QCOMPARE(window.roi().vertices.size(), 3);

    QTest::mouseClick(viewport, Qt::RightButton, Qt::NoModifier,
                      widgetPointForPixel(viewport, 170, 50));
    QTest::qWait(50);

    // The corner stays, because three is the fewest that enclose anything.
    QCOMPARE(window.roi().vertices.size(), 3);

    // ⚑ And so does everything else on screen. The image is still there, and
    // the boundary is still drawn over it.
    QVERIFY2(viewport->record().isValid(),
             "the refusal took the image off the screen with it");
    QVERIFY2(window.roi().isValid(),
             "the refusal cleared the region it had just declined to change");

    // The reader is told why, rather than left with a gesture that appears to
    // do nothing.
    auto *log = window.findChild<QPlainTextEdit *>();
    QVERIFY(log);
    QVERIFY2(log->toPlainText().contains(QStringLiteral("three corners"),
                                         Qt::CaseInsensitive),
             qPrintable(log->toPlainText()));
}

void TestWorkspaceWalkthrough::a_region_moves_bodily_while_a_click_inside_it_still_pins_a_reading()
{
    // The last edit a boundary could not take: a region the right SHAPE in the
    // wrong PLACE had to be dragged corner by corner, and distorted on the way.
    //
    // ⚑ THE GESTURE IS SHARED, and that is the whole difficulty. A press inside
    // the boundary already meant "pin the reading here", so the two are told
    // apart by whether the hand actually moved. Without that, every attempt to
    // pin a reading inside the region would nudge the boundary by a pixel or
    // two - the sort of damage nobody notices until a run reports a different
    // field from the one before it.
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openReferenceImage(fixture(QStringLiteral("shift_reference.tif")));

    auto *viewport = window.findChild<ImageViewport *>();
    actionLabelled(&window, QStringLiteral("Define ROI"))->trigger();
    for (const QPoint &pixel : {QPoint(60, 50), QPoint(170, 50),
                                QPoint(170, 110), QPoint(60, 110)}) {
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier,
                          widgetPointForPixel(viewport, pixel.x(), pixel.y()));
    }
    byVisibleText<QPushButton>(viewport, QStringLiteral("Close region"))->click();
    const RegionOfInterest before = window.roi();
    QCOMPARE(before.vertices.size(), 4);

    // The screen says so before the test does it.
    QVERIFY2(projectLine(&window, QStringLiteral("Region of interest"))
                 .contains(QStringLiteral("move the whole region"), Qt::CaseInsensitive),
             qPrintable(projectLine(&window, QStringLiteral("Region of interest"))));

    // ⚑ A CLICK INSIDE IT FIRST, which must leave the boundary exactly where it
    // is and pin a reading instead. This is the half that a move done without a
    // threshold would break, and it is checked BEFORE the move so a broken
    // threshold cannot be hidden by the drag that follows.
    const QPoint middle = widgetPointForPixel(viewport, 115, 80);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, middle);
    QTest::qWait(50);
    QCOMPARE(window.roi().vertices, before.vertices);
    QVERIFY2(pointPanelText(&window).contains(QStringLiteral("pinned"),
                                              Qt::CaseInsensitive),
             qPrintable(pointPanelText(&window)));

    // ⚑ AND A CLICK THAT WOBBLES, which is the only kind a hand makes. A
    // synthetic click sends press and release at one position with nothing in
    // between, so it never consults the threshold at all and cannot tell a
    // sound one from none: with the threshold removed, the click above still
    // passed. Two pixels of tremor is what a real click carries, and it has to
    // still be a click.
    //
    // It lands on the same pixel, so it RELEASES the pin it just set - which is
    // the panel's own rule, and is why the reading is checked before this.
    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, middle);
    QTest::mouseMove(viewport, middle + QPoint(2, 1));
    QTest::qWait(20);
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, middle + QPoint(2, 1));
    QTest::qWait(50);
    QVERIFY2(window.roi().vertices == before.vertices,
             "a click with a couple of pixels of tremor in it dragged the whole "
             "region, which is how a boundary moves without anyone meaning it to");

    // And now a drag, which moves every corner by the same amount.
    const QPoint to = widgetPointForPixel(viewport, 135, 90);
    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, middle);
    QTest::mouseMove(viewport, to);
    QTest::qWait(30);
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, to);
    QTest::qWait(50);

    const RegionOfInterest after = window.roi();
    QCOMPARE(after.vertices.size(), before.vertices.size());

    const QPoint shift = after.vertices.at(0) - before.vertices.at(0);
    QVERIFY2(!shift.isNull(), "the region did not move at all");
    for (int i = 1; i < after.vertices.size(); i++) {
        QVERIFY2(after.vertices.at(i) - before.vertices.at(i) == shift,
                 qPrintable(QStringLiteral("corner %1 moved by %2,%3 while the "
                                           "first moved by %4,%5: the region was "
                                           "distorted rather than moved")
                                .arg(i)
                                .arg((after.vertices.at(i) - before.vertices.at(i)).x())
                                .arg((after.vertices.at(i) - before.vertices.at(i)).y())
                                .arg(shift.x()).arg(shift.y())));
    }
}

QTEST_MAIN(TestWorkspaceWalkthrough)
#include "test_workspace_walkthrough.moc"
