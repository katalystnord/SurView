#include "core/Clipping.h"

#include <algorithm>

double clippedShare(const std::function<double(int x, int y)> &pixel, int width,
                    int height, int centreX, int centreY, int radius,
                    double darkest, double brightest)
{
    if (radius < 0 || width <= 0 || height <= 0)
        return -1.0;

    const int left = std::max(0, centreX - radius);
    const int right = std::min(width - 1, centreX + radius);
    const int top = std::max(0, centreY - radius);
    const int bottom = std::min(height - 1, centreY + radius);

    int counted = 0;
    int clipped = 0;
    for (int y = top; y <= bottom; y++) {
        for (int x = left; x <= right; x++) {
            const double value = pixel(x, y);
            counted++;
            // Exact comparison on purpose: a clipped pixel holds the extreme
            // itself, and a pixel one grey level short of it still carries
            // whatever pattern it recorded.
            if (value == darkest || value == brightest)
                clipped++;
        }
    }
    return counted > 0 ? double(clipped) / counted : -1.0;
}
