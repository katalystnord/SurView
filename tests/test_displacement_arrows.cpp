// Displacement drawn as arrows over the field.
//
// WHY THIS EXISTS. Magnitude, u and v are three scalar maps, and direction is
// legible from none of them: a rotation or a shear reads at a glance as arrows
// and not at all as colour. Three rules carry over from every other view of
// the field, and each has a case:
//   - an arrow only where a point was measured: a rejected point has no
//     arrow, never a zero-length one standing in for it, which would read as
//     a point that did not move;
//   - thinned by whole lattice steps as the grid closes on screen, so arrows
//     never merge into a black mat, and every arrow still sits on a point;
//   - drawn to a stated scale, since an arrow true to length is a few pixels
//     long and invisible.
//
// Written red first (2026-10-08), six cases against a stub. NEGATIVE CHECK,
// four mutations, each caught: rejected points given arrows; u and v swapped;
// the stride rounded down (two cases); the scale ignoring the thinning, caught
// only by the note case, since the drawn lengths are not asserted at stride > 1.

#include "core/DisplacementArrows.h"

#include <QTest>

#include <cmath>
#include <limits>

namespace {

CorrelationPoint at(int gridIndex, float u, float v, bool converged = true)
{
    CorrelationPoint p;
    p.gridIndex = gridIndex;
    p.u = u;
    p.v = v;
    p.converged = converged;
    return p;
}

// A columns x rows grid at `step` px, every point measured with (u, v).
CorrelationResult uniform(int columns, int rows, int step, float u, float v)
{
    CorrelationResult result;
    result.gridColumns = columns;
    result.gridRows = rows;
    result.step = step;
    for (int row = 0; row < rows; row++) {
        for (int column = 0; column < columns; column++) {
            CorrelationPoint p = at(row * columns + column, u, v);
            p.x = float(20 + column * step);
            p.y = float(30 + row * step);
            result.points.append(p);
        }
    }
    return result;
}

double length(const DisplacementArrow &a) { return std::hypot(a.dx, a.dy); }

}  // namespace

class TestDisplacementArrows : public QObject
{
    Q_OBJECT

private slots:
    void a_rejected_point_gets_no_arrow_not_a_zero_length_one();
    void an_arrow_points_the_way_its_point_moved();
    void the_longest_arrow_fits_the_space_between_arrows();
    void arrows_thin_by_whole_grid_steps_as_the_grid_closes_on_screen();
    void a_specimen_that_did_not_move_draws_no_arrows_and_says_so();
    void the_note_states_the_drawing_scale_and_the_thinning();
    void arrows_drawn_shorter_than_true_say_so_in_words_that_read();
};

void TestDisplacementArrows::a_rejected_point_gets_no_arrow_not_a_zero_length_one()
{
    CorrelationResult result = uniform(3, 2, 10, 1.f, 0.5f);
    result.points[4].converged = false;   // the solver's leftover guess stays in u, v
    result.points[2].u = std::numeric_limits<float>::quiet_NaN();

    const ArrowLayout layout = layoutDisplacementArrows(result, 40.0, 18.0);
    QCOMPARE(layout.arrows.size(), 4);
    for (const DisplacementArrow &a : layout.arrows) {
        QVERIFY2(!(a.x == result.points[4].x && a.y == result.points[4].y),
                 "an arrow was drawn at a rejected point");
        QVERIFY(!(a.x == result.points[2].x && a.y == result.points[2].y));
        QVERIFY(length(a) > 0.0);
    }
}

void TestDisplacementArrows::an_arrow_points_the_way_its_point_moved()
{
    // Lopsided in both axes and in sign, so a swapped or negated component
    // cannot pass: +2 right, -1 up (y down, so up is negative).
    const ArrowLayout layout =
        layoutDisplacementArrows(uniform(2, 2, 10, 2.f, -1.f), 40.0, 18.0);
    QVERIFY(!layout.arrows.isEmpty());
    const DisplacementArrow &a = layout.arrows.first();
    QVERIFY(a.dx > 0.0);
    QVERIFY(a.dy < 0.0);
    QCOMPARE(a.dy / a.dx, -0.5);
}

void TestDisplacementArrows::the_longest_arrow_fits_the_space_between_arrows()
{
    CorrelationResult result = uniform(4, 4, 10, 1.f, 0.f);
    result.points[5].u = 3.f;   // the longest, three times the rest
    const ArrowLayout layout = layoutDisplacementArrows(result, 40.0, 18.0);
    QCOMPARE(layout.stride, 1);
    double longest = 0.0;
    for (const DisplacementArrow &a : layout.arrows)
        longest = std::max(longest, length(a));
    QCOMPARE(longest, kArrowFill * 10.0);
    QCOMPARE(layout.scale, kArrowFill * 10.0 / 3.0);
}

void TestDisplacementArrows::arrows_thin_by_whole_grid_steps_as_the_grid_closes_on_screen()
{
    const CorrelationResult result = uniform(10, 7, 5, 1.f, 1.f);
    // 4 screen px per grid step against an 18 px minimum: every 5th point.
    const ArrowLayout thinned = layoutDisplacementArrows(result, 4.0, 18.0);
    QCOMPARE(thinned.stride, 5);
    for (const DisplacementArrow &a : thinned.arrows) {
        const int column = int(std::lround((a.x - 20.0) / 5.0));
        const int row = int(std::lround((a.y - 30.0) / 5.0));
        QVERIFY2(column % 5 == 0 && row % 5 == 0,
                 qPrintable(QStringLiteral("arrow at column %1, row %2").arg(column).arg(row)));
    }
    QCOMPARE(thinned.arrows.size(), 2 * 2);   // columns 0, 5 by rows 0, 5
    // Wider apart on screen than the minimum: every point.
    QCOMPARE(layoutDisplacementArrows(result, 20.0, 18.0).arrows.size(), 70);
}

void TestDisplacementArrows::a_specimen_that_did_not_move_draws_no_arrows_and_says_so()
{
    const ArrowLayout layout =
        layoutDisplacementArrows(uniform(3, 3, 10, 0.f, 0.f), 40.0, 18.0);
    QVERIFY(layout.arrows.isEmpty());
    QVERIFY2(arrowNote(layout).contains(QStringLiteral("did not move")),
             qPrintable(arrowNote(layout)));
}

void TestDisplacementArrows::the_note_states_the_drawing_scale_and_the_thinning()
{
    CorrelationResult result = uniform(10, 7, 5, 0.25f, 0.f);
    const ArrowLayout layout = layoutDisplacementArrows(result, 4.0, 18.0);
    const QString note = arrowNote(layout);
    // 0.25 px drawn as kArrowFill * 25 px is 90 times its length.
    QVERIFY2(note.contains(QStringLiteral("90 times")), qPrintable(note));
    QVERIFY2(note.contains(QStringLiteral("every 5th point")), qPrintable(note));
    QVERIFY2(note.contains(QStringLiteral("not measured")), qPrintable(note));
}

void TestDisplacementArrows::arrows_drawn_shorter_than_true_say_so_in_words_that_read()
{
    // Found by screenshot on the rotation example, where points move 43 px:
    // "drawn 0.83 times their true length" is arithmetic, not a sentence.
    const ArrowLayout layout =
        layoutDisplacementArrows(uniform(3, 3, 10, 30.f, 0.f), 40.0, 18.0);
    QVERIFY(layout.scale < 1.0);
    const QString note = arrowNote(layout);
    QVERIFY2(note.contains(QStringLiteral("0.3 of their true length")), qPrintable(note));
    QVERIFY2(!note.contains(QStringLiteral("times")), qPrintable(note));
}

QTEST_GUILESS_MAIN(TestDisplacementArrows)
#include "test_displacement_arrows.moc"
