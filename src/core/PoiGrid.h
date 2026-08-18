#pragma once

#include "core/Roi.h"

#include <QString>
#include <QVector>

#include <functional>

// Where a correlation run will place its measurement points.
//
// Separated from the run itself so it can be tested without an engine, an
// image, or a worker thread: given a picture's size, the subset radius, the
// grid step and a region, the set of points is fully determined, and it is the
// part with all the arithmetic that can be quietly wrong. A grid that is one
// step out, or that lets a subset hang off the image edge, produces a field
// that looks entirely plausible.
//
// This file deliberately does NOT know about OpenCorr. Membership in the region
// is asked of a predicate the caller supplies, so the production caller can
// hand it the engine's own Polygon2D::contains() while this code stays free of
// engine headers.

struct PoiGridCell
{
    int x = 0;          // reference-image pixel coordinates
    int y = 0;
    int gridIndex = 0;  // row * columns + column, in the geometry below
};

struct PoiGrid
{
    // The rectangle the grid spans, whether or not every cell in it holds a
    // point. A region excludes cells; the geometry still describes the whole
    // rectangle so a field can be laid out over it with the excluded cells
    // holding no value.
    int columns = 0;
    int rows    = 0;
    int originX = 0;
    int originY = 0;
    int step    = 1;

    QVector<PoiGridCell> cells;

    bool restricted = false;

    // Why no grid could be built, in words fit to show a user. Empty when one
    // was. A refusal is a result, not an error: "this region is too close to
    // the edge" is something the caller must be able to say out loud.
    QString refusal;

    bool isValid() const { return refusal.isEmpty(); }
    int cellCount() const { return columns * rows; }
};

// True when the pixel is inside the region. Only consulted when the region is
// valid.
using PoiInsideTest = std::function<bool(int x, int y)>;

// Build the grid for one run. An invalid region means the whole image.
//
// A subset must lie wholly inside the image, so no point is placed within
// `subsetRadius` of any edge — including when a region reaches further out
// than that.
PoiGrid buildPoiGrid(int imageWidth, int imageHeight, int subsetRadius,
                     int gridStep, const RegionOfInterest &roi,
                     const PoiInsideTest &inside);
