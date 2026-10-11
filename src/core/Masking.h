#pragma once

#include "core/Correlation.h"
#include "core/Roi.h"

#include <QString>
#include <QStringList>

// What the screen says about masking subsets at cuts (CorrelationSettings::
// maskAtCuts), in one place, so the Analysis panel, the run report, the point
// readout and the tests share one wording. Engine-free; the masking itself is
// done in core/Correlation.cpp.
//
// ⚑ The two edges of a region now follow different rules, and the screen has to
// say so: a subset beside a CUT is measured on the ground outside it, while a
// subset reaching past the region's OUTER boundary keeps every pixel, as it
// always has. A rule that differs between two edges and is stated nowhere is a
// difference nobody can see or account for.

// The two controls' own labels, shared with the Settings window's lines.
QString maskAtCutsLabel();
QString minValidShareLabel();

// The note under the masking controls, for the settings and region as they
// stand: what the switch will do, or why it will do nothing.
QString maskingPanelNote(const CorrelationSettings &settings, const RegionOfInterest &roi);

// The run report's lines about masking; empty when there was nothing to say.
QStringList maskingReport(const CorrelationResult &result);

// What the exported files say about masking, in one sentence: whether it was
// applied, with what pull-in and floor, and that the outer edge was not masked.
QString maskingStatement(const CorrelationResult &result);
