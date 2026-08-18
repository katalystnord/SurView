// The seam between SurView's region and the engine's shape.
//
// The grid arithmetic is tested against a stand-in predicate (test_poi_grid),
// which leaves one thing unproven: that the boundary a user draws is the same
// boundary the ENGINE tests points against. This is the half that links the
// real engine and checks the conversion is faithful in both directions --
// SurView's open ring in, the engine's closed ring back out.

#include "core/Roi.h"

#include <QTest>

#include "opencorr.h"

using namespace opencorr;

namespace {

std::unique_ptr<Polygon2D> toEnginePolygon(const RegionOfInterest &roi)
{
    // The same conversion Correlation.cpp performs.
    std::vector<int> vertex_x;
    std::vector<int> vertex_y;
    for (const QPoint &vertex : roi.vertices) {
        vertex_x.push_back(vertex.x());
        vertex_y.push_back(vertex.y());
    }
    return std::make_unique<Polygon2D>(vertex_x, vertex_y);
}

}  // namespace

class TestRoiEngineBoundary : public QObject
{
    Q_OBJECT

private slots:
    void a_drawn_square_encloses_what_it_looks_like_it_encloses();
    void a_concave_region_is_not_treated_as_its_convex_hull();
    void the_engines_bounding_box_agrees_with_the_regions_own();
    void a_detected_boundary_survives_the_round_trip();
};

void TestRoiEngineBoundary::a_drawn_square_encloses_what_it_looks_like_it_encloses()
{
    RegionOfInterest roi;
    roi.vertices = {QPoint(10, 10), QPoint(20, 10), QPoint(20, 20), QPoint(10, 20)};

    const auto polygon = toEnginePolygon(roi);

    QVERIFY(polygon->contains(15, 15));
    QVERIFY(polygon->contains(10, 10));   // a corner counts as inside
    QVERIFY(!polygon->contains(5, 15));
    QVERIFY(!polygon->contains(25, 15));
    QVERIFY(!polygon->contains(15, 25));
}

void TestRoiEngineBoundary::a_concave_region_is_not_treated_as_its_convex_hull()
{
    // The detector routinely returns concave outlines -- the real one measured
    // on the sample pair had 28 corners and a deep notch. A membership test
    // that quietly used the hull would measure points the user excluded.
    RegionOfInterest roi;
    roi.vertices = {QPoint(0, 0), QPoint(10, 0), QPoint(10, 5),
                    QPoint(5, 5), QPoint(5, 10), QPoint(0, 10)};

    const auto polygon = toEnginePolygon(roi);

    QVERIFY(polygon->contains(2, 2));
    QVERIFY(polygon->contains(8, 2));
    QVERIFY(polygon->contains(2, 8));
    QVERIFY(!polygon->contains(8, 8));   // the notched-out quadrant
}

void TestRoiEngineBoundary::the_engines_bounding_box_agrees_with_the_regions_own()
{
    // The grid is spanned using RegionOfInterest::bounds() while membership is
    // asked of the engine. If the two disagreed the grid would be laid over the
    // wrong rectangle, and points would be silently dropped at one edge.
    RegionOfInterest roi;
    roi.vertices = {QPoint(13, 7), QPoint(64, 21), QPoint(40, 55), QPoint(9, 33)};

    const auto polygon = toEnginePolygon(roi);
    const QRect box = roi.bounds();

    QCOMPARE(polygon->getMinX(), box.left());
    QCOMPARE(polygon->getMaxX(), box.right());
    QCOMPARE(polygon->getMinY(), box.top());
    QCOMPARE(polygon->getMaxY(), box.bottom());
}

void TestRoiEngineBoundary::a_detected_boundary_survives_the_round_trip()
{
    // What auto-detection actually does: the engine builds a polygon, SurView
    // reads its vertices back to draw and re-use them. The engine stores the
    // ring CLOSED (first vertex repeated), SurView's own is open, and getting
    // that wrong adds a duplicate corner every time a region is re-read.
    RegionOfInterest original;
    original.vertices = {QPoint(4, 4), QPoint(30, 6), QPoint(28, 25), QPoint(6, 22)};

    const auto polygon = toEnginePolygon(original);

    const std::vector<int> &x = polygon->vertexX();
    const std::vector<int> &y = polygon->vertexY();
    QCOMPARE(polygon->numVertices(), int(original.vertices.size()));
    QCOMPARE(int(x.size()), polygon->numVertices() + 1);

    RegionOfInterest readBack;
    readBack.origin = RegionOfInterest::Detected;
    for (int i = 0; i < polygon->numVertices(); i++)
        readBack.vertices.append(QPoint(x[size_t(i)], y[size_t(i)]));

    QCOMPARE(readBack.vertices, original.vertices);
    QCOMPARE(readBack.bounds(), original.bounds());

    // And the rebuilt region must select the same pixels.
    const auto rebuilt = toEnginePolygon(readBack);
    for (int py = 0; py < 40; py++) {
        for (int px = 0; px < 40; px++)
            QCOMPARE(rebuilt->contains(px, py), polygon->contains(px, py));
    }
}

QTEST_MAIN(TestRoiEngineBoundary)
#include "test_roi_engine_boundary.moc"
