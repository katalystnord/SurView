// What the speckle in a region is good for, before anything is measured.
//
// WHY THIS EXISTS. Subset radius and region were chosen blind. The reliability
// of a run is decided largely by both, and neither said anything about the
// result until after a correlation had been sat through.
//
// ⚑ It reports a DISPLACEMENT RESOLUTION, not a score. A bare quality number
// invites a threshold, and a threshold here would have to be invented: what
// counts as good speckle depends on the movement being measured. The estimate
// is the same expression the run reports per point afterwards, evaluated from
// the reference image alone, so the units are pixels and the number can be put
// against the movement expected.
//
// NEGATIVE CHECK (2026-09-01): four breaks. Two went red at once -- the
// assumption no longer stated, and the subset radius no longer changing the
// answer. The other two were gaps:
//
//   - ignoring the region entirely left everything green, because the fixture
//     is speckled corner to corner and a region over any part of it resolves
//     about the same. Covered now by a photograph of a real specimen, where a
//     region on the gauge and one on the bare background differ by more than
//     threefold.
//   - a region off the picture still reported nothing, but for the WRONG
//     reason: the division by no pixels makes a not-a-number that the
//     no-gradient guard catches. Two different situations with two different
//     answers for a reader, so the case now names which one it expects.
//
// And a third gap the breaks themselves exposed: for a RECTANGLE the polygon
// membership test is a no-op, since the bounding box is the region. Every case
// here used rectangles, so the one piece of arithmetic that makes a region a
// region was covered by nothing at all. A triangle covers it now, and dropping
// the membership test reddens that case.
//
// One further break stayed green and is NOT a gap, recorded so nobody chases
// it: widening the scan from the region's bounding box to the whole image
// changes no answer, because the membership test still excludes the same
// pixels. The box is an optimisation and the polygon is the restriction.

#include "core/Roi.h"
#include "core/SpeckleQuality.h"

#include <QTest>

namespace
{

QString fixture(const QString &name)
{
    return QStringLiteral(SURVIEW_TEST_FIXTURES "/") + name;
}

RegionOfInterest boxAt(int x, int y, int w, int h)
{
    RegionOfInterest roi;
    roi.vertices << QPoint(x, y) << QPoint(x + w, y)
                 << QPoint(x + w, y + h) << QPoint(x, y + h);
    return roi;
}

}  // namespace

class TestSpeckleQuality : public QObject
{
    Q_OBJECT

private slots:
    void a_speckled_region_reports_what_it_can_resolve();
    void the_estimate_is_in_pixels_and_says_what_it_assumes();
    void a_finer_subset_resolves_less_than_a_coarse_one();
    void an_image_that_cannot_be_read_reports_why_rather_than_a_number();
    void a_region_outside_the_picture_measures_nothing();
    void a_region_over_the_specimen_resolves_better_than_one_over_the_background();
    void a_region_that_is_not_a_rectangle_measures_only_what_is_inside_it();
};

void TestSpeckleQuality::a_speckled_region_reports_what_it_can_resolve()
{
    const SpeckleQuality quality = speckleQualityIn(
        fixture(QStringLiteral("shift_reference.tif")), boxAt(40, 40, 120, 80), 16);

    QVERIFY2(quality.measured, qPrintable(quality.note));

    // Strictly positive on all three. Zero gradient energy, zero noise or a
    // zero floor are each the flattering reading, and none is reachable from a
    // real photograph of a speckle pattern.
    QVERIFY2(quality.meanSssig > 0.0, "no gradient energy in a speckled region");
    QVERIFY2(quality.noiseStdDev > 0.0, "an image with no noise at all");
    QVERIFY2(quality.resolutionPx > 0.0, "a resolution of zero px");

    // Sane for a speckle pattern: better than a tenth of a pixel, worse than a
    // millionth. Bounds this wide cannot be tuned to pass; they catch a formula
    // that is out by orders of magnitude, which is what a wrong one usually is.
    QVERIFY2(quality.resolutionPx < 0.1 && quality.resolutionPx > 1e-6,
             qPrintable(QStringLiteral("resolution came out at %1 px")
                            .arg(quality.resolutionPx)));
}

void TestSpeckleQuality::the_estimate_is_in_pixels_and_says_what_it_assumes()
{
    const SpeckleQuality quality = speckleQualityIn(
        fixture(QStringLiteral("shift_reference.tif")), boxAt(40, 40, 120, 80), 16);
    QVERIFY(quality.measured);

    // ⚑ The assumption is not a footnote. SSSIG sums the squared gradient over
    // both axes while the reported noise floor divides by the WEAKER axis, so
    // this estimate takes the speckle to be equally strong in both directions.
    // On a pattern with a grain to it that is optimistic, and a reader has no
    // way to know unless it is said.
    QVERIFY2(quality.note.contains(QStringLiteral("direction"), Qt::CaseInsensitive),
             qPrintable(quality.note));
    QVERIFY2(!quality.note.isEmpty(), "an estimate offered with nothing said about it");
}

void TestSpeckleQuality::a_finer_subset_resolves_less_than_a_coarse_one()
{
    // A larger subset holds more speckle, so it can resolve a smaller movement.
    // This is the relation the indicator exists to make visible while the
    // radius is being chosen, and it is a property of the physics rather than
    // of this implementation.
    const RegionOfInterest roi = boxAt(40, 40, 120, 80);
    const QString image = fixture(QStringLiteral("shift_reference.tif"));

    const SpeckleQuality small = speckleQualityIn(image, roi, 8);
    const SpeckleQuality large = speckleQualityIn(image, roi, 24);
    QVERIFY(small.measured && large.measured);

    QVERIFY2(large.resolutionPx < small.resolutionPx,
             qPrintable(QStringLiteral("a 24 px subset resolved %1 px against a "
                                       "8 px subset's %2 px")
                            .arg(large.resolutionPx).arg(small.resolutionPx)));
}

void TestSpeckleQuality::an_image_that_cannot_be_read_reports_why_rather_than_a_number()
{
    const SpeckleQuality quality =
        speckleQualityIn(QStringLiteral("/no/such/image.tif"), boxAt(0, 0, 20, 20), 16);
    QVERIFY2(!quality.measured, "a missing image produced a resolution");
    QVERIFY2(!quality.note.isEmpty(), "a failure with no reason");
}

void TestSpeckleQuality::a_region_outside_the_picture_measures_nothing()
{
    const SpeckleQuality quality = speckleQualityIn(
        fixture(QStringLiteral("shift_reference.tif")), boxAt(5000, 5000, 50, 50), 16);
    QVERIFY2(!quality.measured, "a region off the picture produced a resolution");

    // ⚑ The reason has to be the RIGHT one. Found by negative check: with the
    // "nothing to measure" guard removed this still passed, because the
    // division by no pixels produces a not-a-number that the no-gradient guard
    // then catches. Two different situations, two different things for a reader
    // to do about them, so the case names which it expects.
    QVERIFY2(quality.note.contains(QStringLiteral("lie over"), Qt::CaseInsensitive),
             qPrintable(quality.note));
}

void TestSpeckleQuality::a_region_over_the_specimen_resolves_better_than_one_over_the_background()
{
    // The whole purpose of the indicator: it answers for the REGION, so moving
    // the region changes the answer. Nothing proved that until this case -- the
    // synthetic fixture is speckled corner to corner, so ignoring the region
    // entirely gave almost the same number and every case stayed green.
    //
    // A photograph of a real specimen has both: speckle on the gauge and dark,
    // featureless background around it. That contrast is the measurement.
    const QString photograph =
        QStringLiteral(SURVIEW_EXAMPLES "/real/01_tension_without_holes/image_0000.png");

    const SpeckleQuality onSpecimen = speckleQualityIn(photograph, boxAt(90, 700, 100, 300), 16);
    const SpeckleQuality onBackground = speckleQualityIn(photograph, boxAt(5, 60, 40, 200), 16);

    QVERIFY2(onSpecimen.measured, qPrintable(onSpecimen.note));
    QVERIFY2(onBackground.measured, qPrintable(onBackground.note));

    QVERIFY2(onSpecimen.resolutionPx < onBackground.resolutionPx / 3.0,
             qPrintable(QStringLiteral("the speckled gauge resolved %1 px and the "
                                       "bare background %2 px, which is not the "
                                       "difference a region is supposed to make")
                            .arg(onSpecimen.resolutionPx)
                            .arg(onBackground.resolutionPx)));
}

void TestSpeckleQuality::a_region_that_is_not_a_rectangle_measures_only_what_is_inside_it()
{
    // ⚑ Every other case here uses a rectangle, and for a rectangle the polygon
    // test is a no-op: the bounding box IS the region. So removing the
    // membership test entirely left the whole file green, and the one piece of
    // arithmetic that makes a region a region was covered by nothing.
    //
    // A triangle whose bounding box takes in a great deal of bare background,
    // while the triangle itself sits on the speckled gauge, separates the two.
    const QString photograph =
        QStringLiteral(SURVIEW_EXAMPLES "/real/01_tension_without_holes/image_0000.png");

    RegionOfInterest triangle;
    triangle.vertices << QPoint(95, 700) << QPoint(185, 700) << QPoint(140, 1100);

    RegionOfInterest itsBoundingBox;
    const QRect box = triangle.bounds();
    itsBoundingBox.vertices << box.topLeft() << box.topRight()
                            << box.bottomRight() << box.bottomLeft();

    const SpeckleQuality inside = speckleQualityIn(photograph, triangle, 16);
    const SpeckleQuality around = speckleQualityIn(photograph, itsBoundingBox, 16);
    QVERIFY2(inside.measured, qPrintable(inside.note));
    QVERIFY2(around.measured, qPrintable(around.note));

    // The two must not be the same answer. If they are, the shape is being
    // thrown away and only its bounding box is ever measured.
    QVERIFY2(qAbs(inside.meanSssig - around.meanSssig) > 1e-3 * around.meanSssig,
             qPrintable(QStringLiteral("a triangle and its bounding box measured "
                                       "the same speckle: %1 against %2")
                            .arg(inside.meanSssig).arg(around.meanSssig)));
}

QTEST_MAIN(TestSpeckleQuality)
#include "test_speckle_quality.moc"
