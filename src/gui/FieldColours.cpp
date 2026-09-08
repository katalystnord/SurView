#include "gui/FieldColours.h"

#include <vtkLookupTable.h>
#include <vtkMath.h>

#include <cmath>

void buildFieldColourTable(vtkLookupTable *table, bool diverging, bool flag,
                           double lowest, double highest)
{
    if (!table)
        return;

    // ⚑ A FLAG GETS TWO COLOURS, not a ramp through 254 it can never take. A
    // rainbow over a two-state channel says there is a continuum between the
    // states and invites a reader to look for the middle of it. Two flat
    // colours say what is true: a point is one thing or the other.
    if (flag) {
        table->SetNumberOfTableValues(2);
        table->SetRange(lowest, highest);
        table->Build();
        // The quiet colour for the ordinary case, so the eye goes to the
        // repairs rather than to the bulk of the field that needed none.
        table->SetTableValue(0, 0.29, 0.40, 0.52, 1.0);
        table->SetTableValue(1, 0.98, 0.58, 0.16, 1.0);
        table->SetNanColor(0.55, 0.55, 0.58, 0.45);
        table->Modified();
        return;
    }

    constexpr int kEntries = 256;

    table->SetNumberOfTableValues(kEntries);
    table->SetRange(lowest, highest);
    table->Build();   // sizes the table; the entries below replace it

    for (int i = 0; i < kEntries; i++) {
        const double t = double(i) / double(kEntries - 1);
        double r = 0.0;
        double g = 0.0;
        double b = 0.0;

        if (diverging) {
            // Blue for one sign, red for the other, pale in the middle. A
            // sequential ramp cannot show a sign change: it puts compression
            // and tension at two ends of one continuum and hides the zero
            // crossing somewhere in the middle of the colours.
            const double s = t * 2.0 - 1.0;          // -1 .. +1
            const double m = std::abs(s);
            const double lo = 1.0 - m;
            if (s < 0.0) {
                r = 0.19 + 0.72 * lo;
                g = 0.34 + 0.57 * lo;
                b = 0.75 + 0.16 * lo;
            } else {
                r = 0.79 + 0.12 * lo;
                g = 0.22 + 0.69 * lo;
                b = 0.18 + 0.73 * lo;
            }
        } else {
            // The hue ramp the displacement field has always used: blue at the
            // least, red at the most.
            double hsv[3] = {0.667 * (1.0 - t), 1.0, 1.0};
            double rgb[3];
            vtkMath::HSVToRGB(hsv, rgb);
            r = rgb[0];
            g = rgb[1];
            b = rgb[2];
        }
        table->SetTableValue(i, r, g, b, 1.0);
    }

    // Rejected, unreached, or unfitted: visibly not data, in every channel.
    table->SetNanColor(0.55, 0.55, 0.58, 0.45);
    table->Modified();
}
