#include "core/ViewFit.h"

#include <algorithm>
#include <cmath>

ViewFit fitImageInView(double x0, double x1, double y0, double y1,
                       int viewWidth, int viewHeight, int topInset,
                       int bottomInset, double padding, int rightInset)
{
    ViewFit fit;
    const double imageWidth = x1 - x0;
    const double imageHeight = y1 - y0;
    const int freeHeight = viewHeight - std::max(0, topInset) - std::max(0, bottomInset);
    const int freeWidth = viewWidth - std::max(0, rightInset);
    if (!(imageWidth > 0.0) || !(imageHeight > 0.0) || freeWidth <= 0 || freeHeight <= 0)
        return fit;

    // World units per screen pixel: whichever axis is tighter decides.
    const double perPixel = std::max(imageWidth / freeWidth, imageHeight / freeHeight)
                            * (1.0 + std::max(0.0, padding));

    // The free band's middle is (top - bottom) / 2 pixels below the widget's,
    // and the camera looks at the widget's middle, so it looks that far ABOVE
    // the image's centre for the image to sit in the band.
    const double offsetPixels = 0.5 * (std::max(0, topInset) - std::max(0, bottomInset));

    fit.valid = true;
    fit.parallelScale = 0.5 * viewHeight * perPixel;
    // Likewise sideways: a right inset moves the picture left, so the camera
    // looks to the right of the image's centre.
    fit.centreX = 0.5 * (x0 + x1) + 0.5 * std::max(0, rightInset) * perPixel;
    fit.centreY = 0.5 * (y0 + y1) - offsetPixels * perPixel;
    return fit;
}

ScreenRect imageOnScreen(const ViewFit &fit, double x0, double x1, double y0,
                         double y1, int viewWidth, int viewHeight)
{
    ScreenRect rect;
    if (!fit.valid || viewHeight <= 0)
        return rect;
    const double perPixel = 2.0 * fit.parallelScale / viewHeight;
    rect.left = 0.5 * viewWidth + (x0 - fit.centreX) / perPixel;
    rect.right = 0.5 * viewWidth + (x1 - fit.centreX) / perPixel;
    rect.top = 0.5 * viewHeight + (y0 - fit.centreY) / perPixel;
    rect.bottom = 0.5 * viewHeight + (y1 - fit.centreY) / perPixel;
    return rect;
}

ScreenBox placeScaleBar(int viewWidth, int viewHeight, int freeTop, int freeBottom,
                        const ScreenBox &legend)
{
    // Off a bar by the gap between overlays, off the viewport's own edge by the
    // margin every overlay keeps.
    const int top = freeTop > 0 ? freeTop + kOverlayGap : kOverlayMargin;
    const int floor = freeBottom < viewHeight ? freeBottom - kOverlayGap
                                              : viewHeight - kOverlayMargin;
    const int preferred = int(std::lround(0.42 * viewHeight));

    auto inColumn = [&](int right, int bottom) {
        ScreenBox box;
        box.width = kScaleBarWidth;
        box.left = right - kScaleBarWidth;
        box.height = std::min(bottom - top, preferred);
        box.top = bottom - box.height;
        box.valid = box.height > 0 && box.left >= 0;
        return box;
    };

    // The right-hand edge, above the legend when there is one.
    const ScreenBox edge = inColumn(viewWidth - kOverlayMargin,
                                    legend.valid ? legend.top - kOverlayGap : floor);
    if (edge.valid && edge.height >= kScaleBarShortest)
        return edge;

    // Beside the legend, down to the bottom, when the edge is too short.
    if (legend.valid) {
        const ScreenBox beside = inColumn(legend.left - kOverlayGap, floor);
        if (beside.valid && (!edge.valid || beside.height > edge.height))
            return beside;
    }
    return edge;
}
