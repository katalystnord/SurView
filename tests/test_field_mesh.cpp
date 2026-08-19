// Turning a measured result into geometry a mesh file can hold.
//
// WHY THIS IS SEPARATE FROM THE WRITER. The arrangement of points into cells is
// where a field stops being a list of numbers and becomes a surface, and it is
// the part that can be wrong while the file still opens perfectly: a quad whose
// corners are listed diagonally renders as a bow tie, and cells built across a
// region's ragged edge span pixels nothing was measured over. Both look like
// results. Neither is one.
//
// The rule the cases below pin down: a cell exists where all four of its
// corners were ATTEMPTED, and the data arrays say what was found there. That
// keeps "where we looked" in the geometry and "what we found" in the values,
// rather than mixing the two into a mesh that quietly shrinks around failures.
//
// NEGATIVE CHECK (2026-08-19), two of them:
//   - against a stub returning an empty mesh, six of the seven cases went red.
//     `an_empty_result_makes_an_empty_mesh` did not: it passes against any
//     implementation that builds nothing, and is here to pin the degenerate
//     input rather than to show the builder works.
//   - with two corners swapped, so cells cross themselves,
//     `a_cell_walks_round_its_corners_rather_than_across_them` failed with
//     "consecutive corners are 7.07107 apart" -- the diagonal of a 5 px cell.

#include "core/Correlation.h"
#include "core/FieldMesh.h"

#include <QSet>
#include <QTest>

namespace {

// A result whose grid is `columns` by `rows`, with a point in every cell.
CorrelationResult fullGrid(int columns, int rows)
{
    CorrelationResult result;
    result.gridColumns = columns;
    result.gridRows = rows;
    result.originX = 10.f;
    result.originY = 20.f;
    result.step = 5;

    for (int row = 0; row < rows; row++) {
        for (int column = 0; column < columns; column++) {
            CorrelationPoint point;
            point.gridIndex = row * columns + column;
            point.x = result.originX + float(column * result.step);
            point.y = result.originY + float(row * result.step);
            point.converged = true;
            result.points.append(point);
        }
    }
    return result;
}

}  // namespace

class TestFieldMesh : public QObject
{
    Q_OBJECT

private slots:
    void every_measured_point_becomes_a_point_of_the_mesh();
    void a_full_grid_is_covered_by_cells();
    void a_cell_walks_round_its_corners_rather_than_across_them();
    void a_missing_corner_leaves_no_cell_over_ground_nothing_measured();
    void a_cell_survives_a_corner_the_solver_rejected();
    void a_grid_too_narrow_for_a_cell_still_keeps_its_points();
    void an_empty_result_makes_an_empty_mesh();
};

void TestFieldMesh::every_measured_point_becomes_a_point_of_the_mesh()
{
    // Nothing may be dropped on the way out. A point the solver rejected is
    // still a place the instrument looked, and the file has to say so.
    const CorrelationResult result = fullGrid(4, 3);
    const FieldMesh mesh = buildFieldMesh(result);

    QCOMPARE(mesh.pointSource.size(), 12);

    QSet<int> sources;
    for (int source : mesh.pointSource) {
        QVERIFY2(source >= 0 && source < result.points.size(),
                 "a mesh point points outside the result it came from");
        QVERIFY2(!sources.contains(source), "a result point was emitted twice");
        sources.insert(source);
    }
}

void TestFieldMesh::a_full_grid_is_covered_by_cells()
{
    // 4 by 3 points enclose 3 by 2 cells.
    const FieldMesh mesh = buildFieldMesh(fullGrid(4, 3));
    QCOMPARE(mesh.quads.size(), 6);
}

void TestFieldMesh::a_cell_walks_round_its_corners_rather_than_across_them()
{
    // Listed diagonally, the four corners still make a valid cell that any
    // reader will open -- as a bow tie. Checked by geometry rather than by
    // index order, so the property held is the one that matters: consecutive
    // corners are one grid step apart, and opposite corners are the diagonal.
    const CorrelationResult result = fullGrid(3, 3);
    const FieldMesh mesh = buildFieldMesh(result);
    QVERIFY(!mesh.quads.isEmpty());

    const double step = result.step;
    const double diagonal = step * std::sqrt(2.0);

    for (const FieldMeshQuad &quad : mesh.quads) {
        const int corner[4] = {quad.a, quad.b, quad.c, quad.d};

        QSet<int> distinct;
        for (int i = 0; i < 4; i++)
            distinct.insert(corner[i]);
        QCOMPARE(distinct.size(), 4);

        for (int i = 0; i < 4; i++) {
            const CorrelationPoint &from =
                result.points.at(mesh.pointSource.at(corner[i]));
            const CorrelationPoint &next =
                result.points.at(mesh.pointSource.at(corner[(i + 1) % 4]));
            const double side = std::hypot(next.x - from.x, next.y - from.y);
            QVERIFY2(qAbs(side - step) < 1e-6,
                     qPrintable(QStringLiteral("consecutive corners are %1 apart, "
                                               "not one %2 px grid step -- the "
                                               "cell is crossed")
                                    .arg(side)
                                    .arg(step)));
        }

        // And the corners that should be opposite really are.
        const CorrelationPoint &a = result.points.at(mesh.pointSource.at(quad.a));
        const CorrelationPoint &c = result.points.at(mesh.pointSource.at(quad.c));
        QVERIFY(qAbs(std::hypot(c.x - a.x, c.y - a.y) - diagonal) < 1e-6);
    }
}

void TestFieldMesh::a_missing_corner_leaves_no_cell_over_ground_nothing_measured()
{
    // The region-of-interest case. Drop one point from a 3 by 3 grid and the
    // cells that leaned on it must go with it -- otherwise the file claims a
    // surface over ground the instrument never looked at.
    CorrelationResult result = fullGrid(3, 3);
    QCOMPARE(buildFieldMesh(result).quads.size(), 4);

    // Remove the centre point, which is a corner of all four cells.
    for (int i = 0; i < result.points.size(); i++) {
        if (result.points.at(i).gridIndex == 4) {
            result.points.remove(i);
            break;
        }
    }

    const FieldMesh mesh = buildFieldMesh(result);
    QCOMPARE(mesh.pointSource.size(), 8);
    QCOMPARE(mesh.quads.size(), 0);
}

void TestFieldMesh::a_cell_survives_a_corner_the_solver_rejected()
{
    // A rejected point is a place that WAS looked at. The cell stays and the
    // value arrays carry the rejection, so the geometry keeps saying where the
    // instrument was pointed while the data says what came back. Collapsing
    // the mesh instead would make a run's coverage depend on its success.
    CorrelationResult result = fullGrid(2, 2);
    result.points[3].converged = false;

    const FieldMesh mesh = buildFieldMesh(result);

    QCOMPARE(mesh.pointSource.size(), 4);
    QCOMPARE(mesh.quads.size(), 1);
}

void TestFieldMesh::a_grid_too_narrow_for_a_cell_still_keeps_its_points()
{
    // A single row of points encloses no area at all. The points are still the
    // measurement and must survive; only the cells are absent.
    const FieldMesh mesh = buildFieldMesh(fullGrid(5, 1));
    QCOMPARE(mesh.pointSource.size(), 5);
    QCOMPARE(mesh.quads.size(), 0);
}

void TestFieldMesh::an_empty_result_makes_an_empty_mesh()
{
    const FieldMesh mesh = buildFieldMesh(CorrelationResult());
    QVERIFY(mesh.pointSource.isEmpty());
    QVERIFY(mesh.quads.isEmpty());
}

QTEST_MAIN(TestFieldMesh)
#include "test_field_mesh.moc"
