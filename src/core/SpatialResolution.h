#pragma once

#include <QString>

// How far across the specimen a measurement is averaged.
//
// The other half of the trade the noise floor reports. A larger subset
// resolves movement more finely and detail on the specimen more coarsely, and
// stating only the first half reads as though the second cost nothing.
//
// Both lengths are the iDIC Good Practices Guide's: a displacement is averaged
// over its subset, a strain over the virtual strain gauge. ⚑ They are lengths
// AVERAGED OVER, never the smallest feature measured at full size. Measured on
// DIC Challenge 2.0 Star 1, a 33 px subset keeps 90 per cent of a displacement
// wave's amplitude only near a 130 px wavelength, so a reader told "33 px
// resolution" would be told something the instrument does not deliver.

// The subset a displacement is averaged over, 2r + 1 px across with its own
// centre pixel. Zero for a radius that describes no subset.
int displacementAveragingLength(int subsetRadius);

// The virtual strain gauge: the distance between the outermost grid points
// inside the subregion along an axis, plus one subset, since each of those
// points is itself a subset's average. Walked on the lattice rather than taken
// as 2R, because a radius between grid points reaches only the points inside
// it. Zero when the subregion holds only its centre, through which no plane
// can be fitted, or for settings that describe no grid.
int strainGaugeLength(double strainRadius, int gridStep, int subsetRadius);

// What the panel and the run report say. Empty for settings that describe
// nothing.
QString displacementResolutionNote(int subsetRadius);

// ⚑ States no length when the subregion holds fewer than `minNeighbours`
// points: the engine then fetches the nearest points from outside it, so the
// subregion is not what is averaged over and its length would be a fiction.
QString strainResolutionNote(double strainRadius, int gridStep, int subsetRadius,
                             int minNeighbours);
