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
//
// ⚑ AND FOUR MORE, FROM THE SWEEP OF 2026-09-09, DELIBERATELY NOT CHASED.
// Narrowing the scan's `y <= last` to `y < last` drops the final row of every
// region, `x <= last` the final column, and pulling either clamp back from
// `size - 1` to `size - 2` does the same thing one region-edge further out. No
// property here can see any of them: every comparison between two regions
// shifts by the same one row, so they go on agreeing and disagreeing exactly
// as before. The only thing that would catch it is an absolute expectation for
// the mean -- a number taken from the implementation, which is worth less than
// no test at all, since it would agree with whatever the code does. What it
// costs if it is ever wrong is one row of pixels in a mean over tens of
// thousands, well below the two figures the estimate is quoted to.
//
// ⚑ AND ONE IN THE NO-SPECKLE GUARD, which is exact rather than a judgement.
// The guard reads `!(meanSssig > 0) || !(noiseStdDev > 0)`, and widening the
// FIRST half to `>= 0` cannot be caught by any image: an image flat enough to
// have no gradient energy also has no noise, so the second half fires first and
// refuses it either way. Measured on flat_frame.tif, which is where this was
// established rather than argued: its noise estimate is exactly zero while its
// gradient energy is not quite, so the noise half is the one doing the work.
// The other two mutants in that line are killed by the case below.
//
// Reading one row PAST the image is a different matter and is caught, by an
// out-of-bounds access into the gradient map that takes the process down. The
// case for it is below: a region drawn over the picture's edge, which is the
// only thing that asks the clamp to do its job.

#include "core/Roi.h"
#include "core/SpeckleQuality.h"

#include <QElapsedTimer>
#include <QTest>

#include <cmath>

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
    void a_subset_radius_of_one_pixel_is_the_smallest_there_is();
    void a_region_reaching_past_the_picture_measures_the_picture();
    void a_frame_with_no_speckle_says_so_rather_than_reporting_a_resolution();
    void a_prepared_image_answers_for_any_region_without_reading_it_again();
    void preparing_once_is_what_makes_asking_repeatedly_affordable();
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


void TestSpeckleQuality::a_subset_radius_of_one_pixel_is_the_smallest_there_is()
{
    // The boundary of the refusal, which the cases above step over: they ask
    // for 8 and 24, so a guard at "less than 1" and one at "less than 2" are
    // indistinguishable to them. One pixel of radius is a 3 by 3 subset, which
    // is small and real; zero is not a subset at all.
    const QString image = fixture(QStringLiteral("shift_reference.tif"));

    const SpeckleQuality none = speckleQualityIn(image, boxAt(20, 20, 60, 60), 0);
    QVERIFY2(!none.measured, "a radius of zero is refused");
    QVERIFY2(none.note.contains(QStringLiteral("at least 1")), qPrintable(none.note));

    const SpeckleQuality one = speckleQualityIn(image, boxAt(20, 20, 60, 60), 1);
    QVERIFY2(one.measured, "a radius of one pixel is measured, not refused");

    const SpeckleQuality negative = speckleQualityIn(image, boxAt(20, 20, 60, 60), -4);
    QVERIFY2(!negative.measured, "and a negative radius is refused too");
}

void TestSpeckleQuality::a_frame_with_no_speckle_says_so_rather_than_reporting_a_resolution()
{
    // ⚑ THE ESTIMATE IS A DIVISION BY THE SPECKLE'S OWN GRADIENT ENERGY, and a
    // frame that carries none divides by zero. What comes out is a not-a-number
    // or an infinity, and either one on a panel that otherwise reads "0.004 px"
    // is a resolution claim about an image nothing can be correlated in.
    //
    // The guard refusing it had no case: the sweep of 2026-09-09 found both
    // halves could be widened to accept a value of exactly zero and nothing
    // went red, because every fixture here is speckled corner to corner.
    //
    // ⚑ AND blank_frame.tif WILL NOT DO IT, which is worth knowing rather than
    // discovering twice. That fixture is pure sensor NOISE, and noise has
    // gradient energy: it reports a finite 0.045 px, which is ten times worse
    // than the same estimate over real speckle and is the honest answer this
    // design gives (there is no invented threshold for "good enough"). The
    // guard is for a frame that is genuinely flat -- a lens cap, a blown-out
    // highlight, an unspeckled painted background -- so flat_frame.tif is
    // exactly that: 240x160 with every pixel at 128, one distinct value in the
    // whole image, which any image tool will confirm.
    const SpeckleQuality quality =
        speckleQualityIn(fixture(QStringLiteral("flat_frame.tif")),
                         boxAt(40, 40, 120, 80), 16);

    QVERIFY2(!quality.measured,
             qPrintable(QStringLiteral("a frame with no gradient at all "
                                       "reported a resolution of %1 px")
                            .arg(quality.resolutionPx)));
    QVERIFY2(quality.note.contains(QStringLiteral("speckle"), Qt::CaseInsensitive),
             qPrintable(quality.note));

    // And it is refused for the RIGHT reason. The region lies squarely over the
    // picture, so a refusal saying it does not is the other failure wearing
    // this one's clothes -- the confusion this file was already caught by once,
    // where a region off the picture was refused by the no-gradient guard
    // rather than by its own.
    QVERIFY2(!quality.note.contains(QStringLiteral("lie over"), Qt::CaseInsensitive),
             qPrintable(quality.note));
}

void TestSpeckleQuality::a_region_reaching_past_the_picture_measures_the_picture()
{
    // ⚑ The scan is clamped to the last row and column of the image, and a
    // region drawn over the edge is the only thing that asks it to be. Every
    // other case here sits comfortably inside the picture, where the clamp
    // never binds -- so it could read one row PAST the image, which is an
    // out-of-bounds access into the gradient map, and nothing would notice.
    //
    // The property is that clamping does not change the answer: a region
    // covering the whole picture and one covering the whole picture plus a
    // margin beyond it measure the same pixels, because there are no others to
    // measure.
    const QString image = fixture(QStringLiteral("shift_reference.tif"));
    const int width = 240;
    const int height = 160;

    const SpeckleQuality exact = speckleQualityIn(image, boxAt(0, 0, width, height), 16);
    QVERIFY2(exact.measured, "a region covering the whole picture is measured");

    const SpeckleQuality overhanging =
        speckleQualityIn(image, boxAt(-50, -50, width + 100, height + 100), 16);
    QVERIFY2(overhanging.measured, "and so is one drawn over its edges");

    QVERIFY2(std::abs(overhanging.meanSssig - exact.meanSssig) < 1e-9,
             qPrintable(QStringLiteral("the overhanging region measured %1 against %2: "
                                       "clamping the scan to the picture changed the answer")
                            .arg(overhanging.meanSssig)
                            .arg(exact.meanSssig)));
    QVERIFY2(std::abs(overhanging.resolutionPx - exact.resolutionPx) < 1e-12,
             "and reports the same resolution");
}

void TestSpeckleQuality::a_prepared_image_answers_for_any_region_without_reading_it_again()
{
    // ⚑ THE EXPENSIVE HALF DOES NOT DEPEND ON THE REGION. Reading the image,
    // running the windowed gradient pass over all of it and estimating the
    // noise are decided by the image and the subset radius alone; only the
    // averaging is about the region. Asked the one-shot way, every question
    // about a new boundary redoes all of it.
    //
    // So the prepared field must answer exactly what the one-shot call answers,
    // for any region - otherwise the saving is bought with a different number.
    const QString image = fixture(QStringLiteral("shift_reference.tif"));
    const SpeckleField field = prepareSpeckleField(image, 16);
    QVERIFY2(field.isValid(), qPrintable(field.note));

    for (const RegionOfInterest &roi : {boxAt(40, 40, 120, 80),
                                        boxAt(20, 20, 60, 60),
                                        RegionOfInterest()}) {
        const SpeckleQuality once = speckleQualityIn(image, roi, 16);
        const SpeckleQuality prepared = speckleQualityIn(field, roi);

        QCOMPARE(prepared.measured, once.measured);
        QVERIFY2(std::abs(prepared.resolutionPx - once.resolutionPx) < 1e-12,
                 qPrintable(QStringLiteral("prepared %1 against one-shot %2")
                                .arg(prepared.resolutionPx).arg(once.resolutionPx)));
        QVERIFY2(std::abs(prepared.meanSssig - once.meanSssig) < 1e-9,
                 qPrintable(QStringLiteral("prepared %1 against one-shot %2")
                                .arg(prepared.meanSssig).arg(once.meanSssig)));
        QCOMPARE(prepared.noiseStdDev, once.noiseStdDev);
        QCOMPARE(prepared.note, once.note);
    }

    // ⚑ A field carries the radius it was prepared at, because the windows it
    // averaged ARE that radius. Asked for a different one it would answer with
    // the wrong subset's figure, which is indistinguishable from the right one.
    const SpeckleField finer = prepareSpeckleField(image, 8);
    QVERIFY(finer.isValid());
    QCOMPARE(finer.subsetRadiusPx, 8);
    QCOMPARE(field.subsetRadiusPx, 16);
    QVERIFY2(speckleQualityIn(finer, boxAt(40, 40, 120, 80)).resolutionPx
                 != speckleQualityIn(field, boxAt(40, 40, 120, 80)).resolutionPx,
             "two radii gave the same answer, so the radius is not being carried");

    // A field that could not be prepared refuses in words rather than answering
    // from an empty map.
    const SpeckleField missing = prepareSpeckleField(
        fixture(QStringLiteral("no_such_image.tif")), 16);
    QVERIFY(!missing.isValid());
    QVERIFY(!missing.note.isEmpty());
    const SpeckleQuality nothing = speckleQualityIn(missing, boxAt(40, 40, 120, 80));
    QVERIFY(!nothing.measured);
    QVERIFY(!nothing.note.isEmpty());
}

void TestSpeckleQuality::preparing_once_is_what_makes_asking_repeatedly_affordable()
{
    // ⚑ THE SPEED IS THE FEATURE, so it is checked rather than assumed. This
    // estimate is shown while a boundary is being drawn and adjusted and is
    // recomputed on every change: measured 2026-09-11, the one-shot call takes
    // about a second on the larger images that ship, which is a second of
    // frozen window per corner dragged.
    //
    // ⚑ THE PROPERTY IS THE MARGINAL COST, not a ratio over some number of
    // asks. Asking N times from a prepared field can never beat N one-shot
    // calls by more than about N, so a bound stated that way is measuring the
    // count rather than the saving - the first version of this case demanded a
    // factor of five from five asks, which is the theoretical ceiling, and
    // failed against a working implementation. What matters is that asking
    // AGAIN is nearly free.
    const QString image = fixture(QStringLiteral("shift_reference.tif"));
    const RegionOfInterest roi = boxAt(40, 40, 120, 80);
    constexpr int kAsks = 20;

    QElapsedTimer timer;
    timer.start();
    speckleQualityIn(image, roi, 16);
    const qint64 oneShot = timer.elapsed();

    const SpeckleField field = prepareSpeckleField(image, 16);
    QVERIFY(field.isValid());
    timer.restart();
    for (int i = 0; i < kAsks; i++)
        speckleQualityIn(field, roi);
    const double perAsk = double(timer.elapsed()) / kAsks;

    // A tenth of a one-shot call, against a difference that is really two
    // orders of magnitude: loose enough that a busy machine does not fail it,
    // tight enough that folding the preparation back into every ask does.
    QVERIFY2(perAsk * 10.0 < double(oneShot),
             qPrintable(QStringLiteral("asking a prepared field costs %1 ms "
                                       "against %2 ms from scratch: the "
                                       "preparation is no longer being reused")
                            .arg(perAsk).arg(oneShot)));
}

QTEST_MAIN(TestSpeckleQuality)
#include "test_speckle_quality.moc"
