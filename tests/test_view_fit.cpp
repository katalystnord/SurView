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

QTEST_GUILESS_MAIN(TestViewFit)
#include "test_view_fit.moc"
