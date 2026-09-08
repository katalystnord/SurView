#pragma once

class vtkLookupTable;

// The colours a measured field is drawn in, in one place.
//
// ⚑ SHARED, not copied. Every panel that draws a field has to use the same ramp:
// the comparison screen puts a measured field beside the answer it should have
// been, and a second ramp built beside this one would eventually differ from it
// and show two correct fields as two different instruments.
//
// `diverging` gives blue for one sign and red for the other, pale in the middle,
// for a quantity whose zero is a physical state. `flag` overrides it with two
// flat colours, because a channel that takes two values and no others must not
// be drawn as a ramp through 254 it can never take.
void buildFieldColourTable(vtkLookupTable *table, bool diverging, bool flag,
                           double lowest, double highest);
