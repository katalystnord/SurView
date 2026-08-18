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
#include "core/Roi.h"
#include "gui/ImageViewport.h"
#include "gui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QToolBar>
#include <QTreeWidget>

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

QTEST_MAIN(TestWorkspaceWalkthrough)
#include "test_workspace_walkthrough.moc"
