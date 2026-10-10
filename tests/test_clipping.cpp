// The share of a subset at the image's own extremes.
//
// The fixture is lopsided in every axis the rule could confuse: clipped BRIGHT
// pixels sit in one place and clipped DARK pixels in another, with a different
// count of each, so a rule counting only one extreme gives a different answer;
// the image is wider than it is tall and the subsets are off the diagonal, so x
// and y swapped reads different pixels; and the extremes are not the pixel
// type's limits, so a rule judging against 0 and 255 finds nothing.

#include "core/Clipping.h"

#include <QTest>
#include <QVector>

class TestClipping : public QObject
{
    Q_OBJECT

private slots:
    void both_extremes_count_and_nothing_in_between_does();
    void the_extremes_are_the_images_own_not_the_types();
    void a_subset_past_the_edge_is_judged_on_what_it_holds();
    void no_pixels_is_no_share_not_a_share_of_zero();
};

namespace {

// 12 x 8, values 50..149 everywhere, then:
//   brightest (240) at x 0..2, y 0..1  -> 6 pixels
//   darkest   (12)  at x 9..11, y 6..7 -> 6 pixels, plus (10, 5) -> 7
// The image's own extremes are therefore 12 and 240, neither a type limit.
struct Fixture
{
    int width = 12;
    int height = 8;
    QVector<double> values;

    Fixture()
    {
        values.resize(width * height);
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
                values[y * width + x] = 50 + (x * 7 + y * 13) % 100;
        for (int y = 0; y <= 1; y++)
            for (int x = 0; x <= 2; x++)
                values[y * width + x] = 240;
        for (int y = 6; y <= 7; y++)
            for (int x = 9; x <= 11; x++)
                values[y * width + x] = 12;
        values[5 * width + 10] = 12;
    }

    std::function<double(int, int)> pixel() const
    {
        return [this](int x, int y) { return values[y * width + x]; };
    }
};

}  // namespace

void TestClipping::both_extremes_count_and_nothing_in_between_does()
{
    const Fixture image;
    // Radius 1 around (1, 1): x 0..2, y 0..2 -> 9 pixels, 6 of them bright.
    QCOMPARE(clippedShare(image.pixel(), image.width, image.height, 1, 1, 1, 12, 240),
             6.0 / 9.0);
    // Radius 1 around (10, 6): x 9..11, y 5..7 -> 9 pixels, 7 of them dark.
    QCOMPARE(clippedShare(image.pixel(), image.width, image.height, 10, 6, 1, 12, 240),
             7.0 / 9.0);
    // In the middle, nothing at either extreme.
    QCOMPARE(clippedShare(image.pixel(), image.width, image.height, 5, 3, 1, 12, 240),
             0.0);
    // Swapped coordinates read (6, 10), which is off the image: not the 7/9
    // that (10, 6) holds.
    QVERIFY(clippedShare(image.pixel(), image.width, image.height, 6, 10, 1, 12, 240)
            != 7.0 / 9.0);
}

void TestClipping::the_extremes_are_the_images_own_not_the_types()
{
    const Fixture image;
    // Judged against an 8-bit type's limits, the same subset shows nothing.
    QCOMPARE(clippedShare(image.pixel(), image.width, image.height, 1, 1, 1, 0, 255), 0.0);
}

void TestClipping::a_subset_past_the_edge_is_judged_on_what_it_holds()
{
    const Fixture image;
    // Radius 1 around (0, 0): only x 0..1, y 0..1 exist, all four bright.
    QCOMPARE(clippedShare(image.pixel(), image.width, image.height, 0, 0, 1, 12, 240), 1.0);
}

void TestClipping::no_pixels_is_no_share_not_a_share_of_zero()
{
    const Fixture image;
    QVERIFY(clippedShare(image.pixel(), image.width, image.height, 40, 40, 1, 12, 240) < 0.0);
    QVERIFY(clippedShare(image.pixel(), image.width, image.height, 5, 3, -1, 12, 240) < 0.0);
}

QTEST_GUILESS_MAIN(TestClipping)
#include "test_clipping.moc"
