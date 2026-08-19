#pragma once

#include <QVector>

struct CorrelationResult;

// The arrangement of a measured result into geometry: points, and the cells
// that join them into a surface.
//
// Separate from the writer, and engine-free, because this is where a field
// stops being a list of numbers and becomes a shape. The failure modes are
// silent: a cell whose corners are listed diagonally opens perfectly and
// renders as a bow tie, and a cell built across the ragged edge of a region
// claims a surface over ground nothing was measured on.
//
// THE RULE. A cell exists where all four of its corners were ATTEMPTED. Whether
// the solver succeeded there belongs in the data arrays, not in the geometry:
// keeping the two apart means a run's coverage describes where the instrument
// was pointed, and the values describe what came back, so a difficult specimen
// does not quietly export as a smaller one.

// One cell, as four indices into the mesh's own point list, walking the
// perimeter rather than crossing it.
struct FieldMeshQuad
{
    int a = 0;
    int b = 0;
    int c = 0;
    int d = 0;
};

struct FieldMesh
{
    // For each mesh point, its index in the result's point list. The mesh holds
    // no coordinates of its own: they live in the result, and copying them here
    // would create a second place for them to be wrong.
    QVector<int> pointSource;

    QVector<FieldMeshQuad> quads;
};

FieldMesh buildFieldMesh(const CorrelationResult &result);
