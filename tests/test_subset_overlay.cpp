// Drawing the settings at the size they will be measured at.
//
// WHAT MAKES THIS WORTH TESTING. Every failure mode here draws a box that looks
// exactly as convincing as a correct one: a square 2r across instead of 2r+1, a
// centre half a grid step off the lattice the run will use, a neighbourhood
// drawn out to the full lattice where the run could only place half of it. The
// picture is the whole point of the feature, and a wrong picture is more
// persuasive than no picture.
//
// ⚑ The case this file exists for most is
// the_points_drawn_are_the_ones_the_panel_counts. The panel states a number of
// neighbours in words and the image draws them as dots, so the same fact is now
// on screen twice; if the two ever disagree, one of them is lying and a reader
// has no way to tell which.
//
// NEGATIVE CHECK (2026-09-08): seven breaks, each reverted after and recorded
// at the assertion it exercises. Six landed on the case named for them. The
// seventh -- rounding the lattice reach up -- landed nowhere, which is correct
// and is written down at the case concerned rather than left looking like
// coverage.

#include "core/PoiGrid.h"
#include "core/StrainFit.h"
#include "core/SubsetOverlay.h"

#include <QTest>

#include <cmath>

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 480;

SubsetOverlay overlayAt(double x, double y, int subsetRadius = 16, int gridStep = 10,
                        bool strain = true, double strainRadius = 25.0,
                        const RegionOfInterest &roi = RegionOfInterest())
{
    return subsetOverlayAt(x, y, kWidth, kHeight, subsetRadius, gridStep, roi,
                           strain, strainRadius);
}

// Every position the run would place a point at, for the same settings. Asked
// of the grid builder itself rather than restated here: the whole claim of the
// snapping case is that the two agree.
QVector<QPoint> runPositions(int subsetRadius, int gridStep,
                             const RegionOfInterest &roi = RegionOfInterest())
{
    const PoiGrid grid = buildPoiGrid(kWidth, kHeight, subsetRadius, gridStep, roi,
                                      [](int, int) { return true; });
    QVector<QPoint> positions;
    for (const PoiGridCell &cell : grid.cells)
        positions.append(QPoint(cell.x, cell.y));
    return positions;
}

bool isARunPosition(const QVector<QPoint> &positions, double x, double y)
{
    return positions.contains(QPoint(int(std::lround(x)), int(std::lround(y))));
}

}  // namespace

class TestSubsetOverlay : public QObject
{
    Q_OBJECT

private slots:
    void the_square_drawn_is_the_subset_the_engine_will_correlate();
    void the_overlay_sits_on_the_grid_the_run_will_lay_out();
    void it_is_never_drawn_where_the_run_could_not_place_a_point();
    void a_region_keeps_the_overlay_inside_what_will_be_measured();

    void the_points_drawn_are_the_ones_the_panel_counts();
    void near_an_edge_only_the_neighbours_that_exist_are_drawn();
    void every_point_drawn_is_a_point_the_run_would_place();
    void the_centre_is_one_of_the_points_the_fit_would_use();

    void a_hole_takes_the_points_it_covers_out_of_the_neighbourhood();
    void nothing_is_drawn_for_a_subregion_no_strain_will_be_fitted_in();
    void settings_that_can_place_no_point_draw_nothing_and_say_why();
};

void TestSubsetOverlay::the_square_drawn_is_the_subset_the_engine_will_correlate()
{
    // ⚑ 2r + 1, not 2r. A subset of radius 16 spans 33 px, its own centre pixel
    // included, which is the same count the engine's own subset holds and the
    // same one the speckle estimate is quoted over. Drawn one pixel narrower it
    // is still a plausible square and still the wrong answer to "is the pattern
    // in here distinct".
    //
    // NEGATIVE CHECK: a side of 2r reads 32 px here and the case goes red.
    const SubsetOverlay overlay = overlayAt(320.0, 240.0);
    QVERIFY(overlay.valid);
    QCOMPARE(overlay.subsetRadius, 16);
    QCOMPARE(overlay.subsetSide(), 33.0);
}

void TestSubsetOverlay::the_overlay_sits_on_the_grid_the_run_will_lay_out()
{
    // ⚑ Snapped, not left under the pointer. The dots in the subregion are only
    // the points the fit would really use if the centre is one of them.
    //
    // NEGATIVE CHECK: taking the pointer position unchanged leaves the centre
    // at (323, 247), which is on no grid the run lays out, and this goes red.
    const QVector<QPoint> positions = runPositions(16, 10);
    QVERIFY(!positions.isEmpty());

    for (const QPointF &pointer : {QPointF(323.0, 247.0), QPointF(100.4, 99.6),
                                   QPointF(255.0, 255.0)}) {
        const SubsetOverlay overlay = overlayAt(pointer.x(), pointer.y());
        QVERIFY(overlay.valid);
        QVERIFY2(isARunPosition(positions, overlay.centreX, overlay.centreY),
                 qPrintable(QStringLiteral("centred at (%1, %2), which is not a "
                                           "position the run measures")
                                .arg(overlay.centreX).arg(overlay.centreY)));

        // And it is the NEAREST one, not merely one of them: a box that jumped
        // to a far corner would satisfy the check above.
        QVERIFY(std::abs(overlay.centreX - pointer.x()) <= 10.0);
        QVERIFY(std::abs(overlay.centreY - pointer.y()) <= 10.0);
    }
}

void TestSubsetOverlay::it_is_never_drawn_where_the_run_could_not_place_a_point()
{
    // A pointer in the corner still asks a real question about the settings, so
    // the box is CLAMPED rather than refused -- but it is clamped to somewhere
    // the run could measure. Drawn hanging off the edge it would show a subset
    // the engine will never correlate.
    //
    // NEGATIVE CHECK: without the clamp the corner case centres at (0, 0) and
    // the square reaches to -16 px, which is off the picture.
    for (const QPointF &pointer : {QPointF(0.0, 0.0), QPointF(-40.0, -40.0),
                                   QPointF(kWidth + 90.0, kHeight + 90.0)}) {
        const SubsetOverlay overlay = overlayAt(pointer.x(), pointer.y());
        QVERIFY(overlay.valid);
        QVERIFY2(overlay.centreX - overlay.subsetRadius >= 0.0
                     && overlay.centreY - overlay.subsetRadius >= 0.0
                     && overlay.centreX + overlay.subsetRadius <= kWidth - 1
                     && overlay.centreY + overlay.subsetRadius <= kHeight - 1,
                 qPrintable(QStringLiteral("a subset centred at (%1, %2) reaches "
                                           "outside the picture")
                                .arg(overlay.centreX).arg(overlay.centreY)));
    }
}

void TestSubsetOverlay::a_region_keeps_the_overlay_inside_what_will_be_measured()
{
    // With a region in force the run measures inside it, so a box shown outside
    // it would preview a measurement that is not going to happen.
    RegionOfInterest roi;
    roi.vertices = {QPoint(200, 150), QPoint(400, 150), QPoint(400, 310),
                    QPoint(200, 310)};
    QVERIFY(roi.isValid());

    const SubsetOverlay overlay = overlayAt(600.0, 440.0, 16, 10, true, 25.0, roi);
    QVERIFY(overlay.valid);
    QVERIFY2(roi.bounds().contains(QPoint(int(overlay.centreX), int(overlay.centreY))),
             qPrintable(QStringLiteral("centred at (%1, %2), outside the region "
                                       "being measured")
                            .arg(overlay.centreX).arg(overlay.centreY)));
}

void TestSubsetOverlay::the_points_drawn_are_the_ones_the_panel_counts()
{
    // ⚑ THE CASE THIS FILE EXISTS FOR. The same fact is now on screen twice --
    // the panel states the count in words, the image draws them as dots -- and
    // two statements of one number that can disagree are worse than one.
    //
    // NEGATIVE CHECK: taking the subregion as a SQUARE rather than a circle
    // reads 81 points against the panel's 49, and this goes red.
    //
    // ⚑ WHAT IT DOES NOT CATCH, checked and recorded rather than assumed:
    // rounding the lattice reach UP instead of down changes nothing at all,
    // because every candidate is still tested against the radius itself. The
    // reach only bounds the search; the radius test is what decides.
    for (int step : {5, 8, 10, 16}) {
        for (double radius : {20.0, 25.0, 40.0, 72.0}) {
            const SubsetOverlay overlay =
                overlayAt(320.0, 240.0, 16, step, true, radius);
            QVERIFY(overlay.valid);
            QVERIFY(overlay.hasSubregion);
            QCOMPARE(overlay.subregionRadius, radius);
            QVERIFY2(overlay.neighbours.size() == gridPointsInSubregion(radius, step),
                     qPrintable(QStringLiteral("radius %1 at step %2: drew %3 "
                                               "points, panel counts %4")
                                    .arg(radius).arg(step)
                                    .arg(overlay.neighbours.size())
                                    .arg(gridPointsInSubregion(radius, step))));
        }
    }
}

void TestSubsetOverlay::near_an_edge_only_the_neighbours_that_exist_are_drawn()
{
    // ⚑ Near an edge the fit genuinely has fewer points -- that is the lopsided
    // neighbourhood the accuracy suite excludes a margin for -- and drawing the
    // full lattice there would promise a fit the run cannot make.
    //
    // NEGATIVE CHECK: drawing the whole lattice regardless leaves the edge case
    // reading the same count as the middle, and the picture stops being true
    // exactly where the fit is weakest.
    const SubsetOverlay middle = overlayAt(320.0, 240.0, 16, 10, true, 40.0);
    const SubsetOverlay corner = overlayAt(0.0, 0.0, 16, 10, true, 40.0);
    QVERIFY(middle.valid && corner.valid);
    QVERIFY2(corner.neighbours.size() < middle.neighbours.size(),
             qPrintable(QStringLiteral("the corner drew %1 neighbours and the "
                                       "middle %2")
                            .arg(corner.neighbours.size())
                            .arg(middle.neighbours.size())));
    QVERIFY(!corner.neighbours.isEmpty());
}

void TestSubsetOverlay::every_point_drawn_is_a_point_the_run_would_place()
{
    const QVector<QPoint> positions = runPositions(16, 10);
    const SubsetOverlay overlay = overlayAt(60.0, 60.0, 16, 10, true, 40.0);
    QVERIFY(overlay.valid);
    QVERIFY(!overlay.neighbours.isEmpty());

    for (const QPointF &point : overlay.neighbours) {
        QVERIFY2(isARunPosition(positions, point.x(), point.y()),
                 qPrintable(QStringLiteral("drew a neighbour at (%1, %2), where "
                                           "the run places nothing")
                                .arg(point.x()).arg(point.y())));
        // And inside the subregion it is drawn as being in.
        const double dx = point.x() - overlay.centreX;
        const double dy = point.y() - overlay.centreY;
        QVERIFY(dx * dx + dy * dy <= overlay.subregionRadius * overlay.subregionRadius);
    }
}

void TestSubsetOverlay::the_centre_is_one_of_the_points_the_fit_would_use()
{
    // The engine's own neighbour count includes the point being fitted, which is
    // why gridPointsInSubregion() counts it. The drawing has to agree.
    const SubsetOverlay overlay = overlayAt(320.0, 240.0, 16, 10, true, 25.0);
    QVERIFY(overlay.neighbours.contains(QPointF(overlay.centreX, overlay.centreY)));
}

void TestSubsetOverlay::a_hole_takes_the_points_it_covers_out_of_the_neighbourhood()
{
    // ⚑ A hole is where the specimen is not, so the run places no point there
    // and the fit averages over fewer. Drawing the dots the hole covers would
    // promise neighbours that do not exist -- the same lie as drawing the full
    // lattice at an edge, in the one place a reader is most likely to be
    // looking, since a hole is where the stress concentrates.
    //
    // NEGATIVE CHECK: not asking the region at all leaves both counts equal and
    // this red.
    RegionOfInterest whole;
    whole.vertices = {QPoint(100, 100), QPoint(540, 100), QPoint(540, 380),
                      QPoint(100, 380)};

    RegionOfInterest holed = whole;
    holed.holes = {{QPoint(300, 220), QPoint(360, 220), QPoint(360, 270),
                    QPoint(300, 270)}};
    QVERIFY(holed.hasHoles());

    const SubsetOverlay without =
        overlayAt(320.0, 240.0, 16, 10, true, 40.0, whole);
    const SubsetOverlay with =
        overlayAt(320.0, 240.0, 16, 10, true, 40.0, holed);
    QVERIFY(without.valid && with.valid);
    QCOMPARE(with.centreX, without.centreX);
    QVERIFY2(with.neighbours.size() < without.neighbours.size(),
             qPrintable(QStringLiteral("the hole took no points out: %1 against %2")
                            .arg(with.neighbours.size())
                            .arg(without.neighbours.size())));
}

void TestSubsetOverlay::nothing_is_drawn_for_a_subregion_no_strain_will_be_fitted_in()
{
    // A subregion drawn while strain is switched off would show a neighbourhood
    // nothing is going to average over.
    const SubsetOverlay overlay = overlayAt(320.0, 240.0, 16, 10, false, 25.0);
    QVERIFY(overlay.valid);          // the subset itself is still real
    QVERIFY(!overlay.hasSubregion);
    QVERIFY(overlay.neighbours.isEmpty());
}

void TestSubsetOverlay::settings_that_can_place_no_point_draw_nothing_and_say_why()
{
    // ⚑ The same refusal the run gives, in the same words. A box drawn for
    // settings that cannot measure anything would say the opposite of what the
    // Run button is about to say.
    const SubsetOverlay overlay = overlayAt(320.0, 240.0, 400, 10);
    QVERIFY(!overlay.valid);
    QVERIFY2(!overlay.refusal.isEmpty(), "nothing could be drawn and nothing said why");

    const PoiGrid grid = buildPoiGrid(kWidth, kHeight, 400, 10, RegionOfInterest(),
                                      [](int, int) { return true; });
    QCOMPARE(overlay.refusal, grid.refusal);

    // And settings that make no sense at all are refused rather than drawn at
    // some default size.
    QVERIFY(!overlayAt(320.0, 240.0, 0, 10).valid);
    QVERIFY(!overlayAt(320.0, 240.0, 16, 0).valid);
}

QTEST_MAIN(TestSubsetOverlay)
#include "test_subset_overlay.moc"
