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
#include "core/Roi.h"
#include "gui/ImageViewport.h"
#include "gui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QToolBar>
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

    QAction *exportAction = actionLabelled(&window, QStringLiteral("Export"));
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

QTEST_MAIN(TestWorkspaceWalkthrough)
#include "test_workspace_walkthrough.moc"
