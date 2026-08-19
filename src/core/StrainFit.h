#pragma once

#include <QString>
#include <QVector>

// Fitting strain from a field of measured displacements.
//
// Strain is not measured. Correlation measures displacement at a point; strain
// is the GRADIENT of that displacement, and a gradient needs more than one
// point. The engine gets it by fitting a plane to the displacements of the
// points inside a subregion around each point, which means every strain value
// carries two things the displacement it came from does not: a neighbourhood
// it was fitted over, and the possibility that the neighbourhood was not the
// one the user chose.
//
// This header exists for that second part. The engine's own fit does not
// refuse a subregion that is too small -- it quietly searches for the nearest
// N points instead, however far outside the subregion they are, and returns a
// strain field that looks exactly as complete as a good one. The count below
// is computed before the run so that substitution can be said out loud.

// How the fitted displacement gradients are turned into strain.
enum class StrainMeasure
{
    Cauchy,          // engine approximation 1: the linear, small-strain form
    GreenLagrange,   // engine approximation 2: keeps the quadratic terms
};

// One strain measure the application offers, as the panel shows it.
//
// Single source of truth, in the same sense as offeredSolverChoices(): the
// Analysis panel builds its control from this list and the tests walk it, so a
// measure cannot be offered to a user without something checking it.
struct StrainMeasureChoice
{
    StrainMeasure measure;
    QString name;
    QString note;   // when to pick it, shown next to the choice
};

QVector<StrainMeasureChoice> offeredStrainMeasures();
QString strainMeasureName(StrainMeasure measure);

// The correlation a point must reach to be used in a neighbour's strain fit.
// This is the engine's own default, restated here rather than left implicit,
// because it silently decides which measurements the strain field is built
// from and the count it excludes is worth reporting.
constexpr float kStrainFitCorrelationFloor = 0.9f;

// How many grid points fall inside a subregion of `radius` px centred on a
// grid point, when the grid is spaced `gridStep` px apart -- the centre point
// included, because the engine's own neighbour count includes it.
//
// Counted exactly over the lattice rather than estimated as pi*r^2/step^2: the
// count decides which side of a threshold the warning below falls on, and the
// area estimate is wrong by whole rings of points at the small radii where
// that matters. Zero for a step or radius that cannot describe a grid.
int gridPointsInSubregion(double radius, int gridStep);

// What to tell the user, before the run, when the subregion they chose cannot
// hold the neighbours the fit needs. Empty when it can.
QString strainSubregionWarning(double radius, int gridStep, int minNeighbours);
