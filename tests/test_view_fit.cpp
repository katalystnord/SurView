// Fitting the whole image into the part of the viewport its bars leave free.
//
// The property that matters on screen -- the image's top edge below the field
// explanation -- is asserted by the walkthrough through the renderer's own
// projection, never here. These cases pin what the arithmetic must not do.
//
// NEGATIVE CHECK (2026-10-07), each caught here AND by the walkthrough case
// `the_field_explanation_does_not_cover_the_field_it_explains`:
//   - the centre offset dropped: `an_inset_moves_the_image_down_not_up` red.
//   - the free band taken as the whole widget: `a_tall_image_shrinks...` and
//     `a_band_with_no_room_left_fits_nothing` red.

#include "core/ViewFit.h"

#include <QTest>

class TestViewFit : public QObject
{
    Q_OBJECT

private slots:
    void without_bars_the_image_is_centred_and_fills_the_tighter_axis();
    void a_tall_image_shrinks_to_the_band_left_free();
    void an_inset_moves_the_image_down_not_up();
    void a_band_with_no_room_left_fits_nothing();
    void a_right_inset_moves_the_image_left_and_clear_of_it();
    void the_scale_sits_between_the_field_bar_and_the_legend();
    void a_column_too_short_for_the_scale_puts_it_beside_the_legend();
    void without_a_legend_the_scale_sits_on_the_bottom_bar();
    void the_scale_never_meets_the_bars_or_the_legend_at_any_size();
};

void TestViewFit::without_bars_the_image_is_centred_and_fills_the_tighter_axis()
{
    // 200 x 100 image in a 400 x 400 widget: width is tighter, 0.5 world per px.
    const ViewFit fit = fitImageInView(0, 200, 0, 100, 400, 400, 0, 0, 0.0);
    QVERIFY(fit.valid);
    QCOMPARE(fit.centreX, 100.0);
    QCOMPARE(fit.centreY, 50.0);
    QCOMPARE(fit.parallelScale, 100.0);
}

void TestViewFit::a_tall_image_shrinks_to_the_band_left_free()
{
    // 100 x 300 in 400 x 400 with 100 px covered: 300 free px, 1 world per px.
    const ViewFit covered = fitImageInView(0, 100, 0, 300, 400, 400, 100, 0, 0.0);
    const ViewFit bare = fitImageInView(0, 100, 0, 300, 400, 400, 0, 0, 0.0);
    QCOMPARE(covered.parallelScale, 200.0);
    QCOMPARE(bare.parallelScale, 150.0);
}

void TestViewFit::an_inset_moves_the_image_down_not_up()
{
    // A bar at the top pushes the picture down the screen, which in y-down
    // world means the camera looks at a SMALLER y than the image's centre.
    const ViewFit top = fitImageInView(0, 100, 0, 300, 400, 400, 100, 0, 0.0);
    QVERIFY2(top.centreY < 150.0, qPrintable(QString::number(top.centreY)));
    const ViewFit bottom = fitImageInView(0, 100, 0, 300, 400, 400, 0, 100, 0.0);
    QVERIFY2(bottom.centreY > 150.0, qPrintable(QString::number(bottom.centreY)));
}

void TestViewFit::a_band_with_no_room_left_fits_nothing()
{
    QVERIFY(!fitImageInView(0, 100, 0, 100, 400, 400, 250, 150, 0.0).valid);
    QVERIFY(!fitImageInView(0, 0, 0, 100, 400, 400, 0, 0, 0.0).valid);
    QVERIFY(!fitImageInView(0, 100, 0, 100, 0, 400, 0, 0, 0.0).valid);
}

void TestViewFit::a_right_inset_moves_the_image_left_and_clear_of_it()
{
    // 300 x 100 in 400 x 400 with the right 100 px covered: 300 px free.
    const ViewFit fit = fitImageInView(0, 300, 0, 100, 400, 400, 0, 0, 0.0, 100);
    QVERIFY(fit.valid);
    QVERIFY2(fit.centreX > 150.0, qPrintable(QString::number(fit.centreX)));
    const ScreenRect placed = imageOnScreen(fit, 0, 300, 0, 100, 400, 400);
    QVERIFY2(placed.right <= 300.0 + 1e-9, qPrintable(QString::number(placed.right)));
    QVERIFY2(placed.left >= -1e-9, qPrintable(QString::number(placed.left)));
}

namespace {
ScreenBox legendAt(int left, int top, int right, int bottom)
{
    ScreenBox box;
    box.valid = true;
    box.left = left;
    box.top = top;
    box.width = right - left;
    box.height = bottom - top;
    return box;
}
}  // namespace

void TestViewFit::the_scale_sits_between_the_field_bar_and_the_legend()
{
    // 800 x 600, field bar down to 100, legend's corner at (600, 480). Lopsided
    // on purpose: room above the legend is 374 px and the preferred height is
    // 252, so a scale stretched to the band and one bottom-aligned in it differ.
    const ScreenBox box = placeScaleBar(800, 600, 100, 600, legendAt(600, 480, 790, 590));
    QVERIFY(box.valid);
    QCOMPARE(box.right(), 800 - kOverlayMargin);
    QCOMPARE(box.width, kScaleBarWidth);
    QCOMPARE(box.bottom(), 480 - kOverlayGap);
    QCOMPARE(box.height, 252);
}

void TestViewFit::a_column_too_short_for_the_scale_puts_it_beside_the_legend()
{
    // A field bar down to 300 and a legend from 380: 68 px between them, too
    // short to read a scale in. Beside the legend, from below the field bar to
    // above the bottom edge, there is room.
    const ScreenBox box = placeScaleBar(400, 500, 300, 500, legendAt(200, 380, 390, 490));
    QVERIFY(box.valid);
    QCOMPARE(box.right(), 200 - kOverlayGap);
    QCOMPARE(box.bottom(), 500 - kOverlayMargin);
    QVERIFY2(box.top >= 300 + kOverlayGap, qPrintable(QString::number(box.top)));
    QVERIFY2(box.height >= kScaleBarShortest, qPrintable(QString::number(box.height)));
}

void TestViewFit::without_a_legend_the_scale_sits_on_the_bottom_bar()
{
    // No legend, and a region bar along the bottom from 450.
    const ScreenBox box = placeScaleBar(800, 600, 0, 450, ScreenBox());
    QVERIFY(box.valid);
    QCOMPARE(box.bottom(), 450 - kOverlayGap);
    QCOMPARE(box.right(), 800 - kOverlayMargin);
}

void TestViewFit::the_scale_never_meets_the_bars_or_the_legend_at_any_size()
{
    // The property the cases above are instances of, over a spread of
    // viewports, field bar depths and legend sizes. A legend is about 200 x
    // 130, so the narrowest viewports here are narrower than it -- its left
    // edge negative, as it really is in a 197 px viewport -- and there the
    // scale cannot sit beside it and must still not sit under it.
    for (int w = 150; w <= 1400; w += 50) {
        for (int h = 300; h <= 1100; h += 80) {
            for (int barDepth : {0, 60, 140, int(0.4 * h)}) {
                for (bool hasLegend : {false, true}) {
                    const ScreenBox legend =
                        hasLegend ? legendAt(w - kOverlayMargin - 200, h - kOverlayMargin - 130,
                                             w - kOverlayMargin, h - kOverlayMargin)
                                  : ScreenBox();
                    const ScreenBox box = placeScaleBar(w, h, barDepth, h, legend);
                    const QString where = QStringLiteral("%1 x %2, bar to %3, legend %4: "
                                                         "scale (%5, %6) to (%7, %8)")
                                              .arg(w).arg(h).arg(barDepth).arg(hasLegend)
                                              .arg(box.left).arg(box.top)
                                              .arg(box.right()).arg(box.bottom());
                    QVERIFY2(box.valid, qPrintable(where));
                    QVERIFY2(box.top >= barDepth, qPrintable(where));
                    QVERIFY2(box.left >= 0 && box.right() <= w && box.bottom() <= h,
                             qPrintable(where));
                    if (hasLegend)
                        QVERIFY2(!box.intersects(legend.left, legend.top, legend.right(),
                                                 legend.bottom()),
                                 qPrintable(where));
                }
            }
        }
    }
}

QTEST_GUILESS_MAIN(TestViewFit)
#include "test_view_fit.moc"
