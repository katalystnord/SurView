#pragma once

#include "core/Roi.h"

#include <QPointF>
#include <QString>
#include <QVector>

// The subset and the strain subregion, drawn over the reference image at the
// size they will actually be measured at.
//
// WHY THIS EXISTS. Subset radius, grid step and strain subregion are three
// numbers in a panel, and the question a person actually has about them is
// visual: is this square big enough to contain distinct pattern, and how much
// of the specimen does the strain fit average over? A number cannot answer
// that; a box over the speckle can. Upstream's own GUI draws both, and it is
// the fastest way to see what a setting means before spending a run on it.
//
// ⚑ IN ADDITION to counting the neighbours a subregion holds, which the
// Analysis panel already states in words -- not instead of it. The count is the
// rigorous answer to "will the fit have enough points"; the drawing is the fast
// answer to "is the pattern inside this box distinct". Neither replaces the
// other, and the two must never disagree: the points drawn here are counted by
// the same lattice walk gridPointsInSubregion() uses, and a test holds them to
// the same number.
//
// Separated from the widget for the usual reason: it is arithmetic that is
// quietly wrong in ways a picture still looks right for. A square drawn 2r
// across instead of 2r+1, or centred half a grid step off the lattice the run
// will use, looks exactly as convincing as a correct one.

struct SubsetOverlay
{
    bool valid = false;

    // Why nothing can be drawn, in words fit to show a user. Empty when
    // something can be. Taken from the grid's own refusal, so the overlay and
    // the run give the same reason for the same settings.
    QString refusal;

    // The grid point the overlay is centred on. ⚑ Snapped to the lattice the
    // run will lay out, not left under the pointer: the points drawn in the
    // subregion are only the points the fit would really use if the centre is
    // one of them, and a box floating between grid points would show a
    // neighbourhood that cannot occur.
    double centreX = 0.0;
    double centreY = 0.0;

    // The subset the engine will correlate, as pixels. A subset of radius r
    // spans 2r+1 px, its own centre pixel included.
    int subsetRadius = 0;
    double subsetSide() const { return 2.0 * subsetRadius + 1.0; }

    bool hasSubregion = false;
    double subregionRadius = 0.0;

    // The grid points the strain fit could take its neighbours from here,
    // centre included, and only those the run would actually place: near an
    // edge there are genuinely fewer, and drawing the full lattice there would
    // promise a fit the run cannot make.
    QVector<QPointF> neighbours;
};

// Where to draw, for a pointer at (x, y) in reference-image pixels.
//
// The position is CLAMPED to the grid, never refused: a pointer near the edge
// still asks a real question about the settings, and the box it gets is the
// nearest one the run could actually place. Nothing is drawn where the run
// could place nothing at all, which is what `refusal` says.
SubsetOverlay subsetOverlayAt(double x, double y, int imageWidth, int imageHeight,
                              int subsetRadius, int gridStep,
                              const RegionOfInterest &roi,
                              bool strainEnabled, double strainRadius);
