#include "core/FieldMesh.h"

#include "core/Correlation.h"

FieldMesh buildFieldMesh(const CorrelationResult &result)
{
    FieldMesh mesh;

    const int columns = result.gridColumns;
    const int rows = result.gridRows;
    const int cells = columns * rows;
    if (cells <= 0)
        return mesh;

    // Which mesh point occupies each cell of the grid, or -1 where the grid
    // was never sampled -- inside a region's hollows, or outside it entirely.
    QVector<int> meshPointAt(cells, -1);

    mesh.pointSource.reserve(result.points.size());
    for (int i = 0; i < result.points.size(); i++) {
        const CorrelationPoint &point = result.points.at(i);
        if (point.gridIndex < 0 || point.gridIndex >= cells)
            continue;
        meshPointAt[point.gridIndex] = mesh.pointSource.size();
        mesh.pointSource.append(i);
    }

    for (int row = 0; row + 1 < rows; row++) {
        for (int column = 0; column + 1 < columns; column++) {
            // Round the perimeter: top-left, top-right, bottom-right,
            // bottom-left. Any other order is a valid cell that renders as a
            // bow tie, which is why the test checks the geometry of the sides
            // rather than this listing.
            const int corner[4] = {
                meshPointAt[row * columns + column],
                meshPointAt[row * columns + column + 1],
                meshPointAt[(row + 1) * columns + column + 1],
                meshPointAt[(row + 1) * columns + column],
            };
            if (corner[0] < 0 || corner[1] < 0 || corner[2] < 0 || corner[3] < 0)
                continue;
            mesh.quads.append(
                FieldMeshQuad{corner[0], corner[1], corner[2], corner[3]});
        }
    }

    return mesh;
}
