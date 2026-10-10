#pragma once

// Where the camera has to stand for a whole image to be seen.
//
// The viewport carries bars over its own picture: the field's explanation
// across the top, the region and extensometer bars across the bottom. Fitted
// to the whole widget, the top of the image sat under the explanation of the
// field drawn on it -- on DIC Challenge Sample 3 that was the first grid row,
// and a tall specimen lost more. So the image is fitted into the band the bars
// leave free, and centred in that band rather than in the widget.
//
// World coordinates are image pixels with y DOWN, the project's one frame, so
// a band lower on screen is at larger world y.
struct ViewFit
{
    bool valid = false;
    double parallelScale = 0.0;   // half the world height the widget shows
    double centreX = 0.0;         // where the camera looks, in world units
    double centreY = 0.0;
};

// `x0..x1`, `y0..y1`: the image's world bounds. `viewWidth` x `viewHeight`:
// the widget in pixels. `topInset`, `bottomInset`, `rightInset`: pixels
// covered on those sides. `padding`: the share of the free area left empty
// around the image. Invalid when the bounds or the free area are empty.
ViewFit fitImageInView(double x0, double x1, double y0, double y1,
                       int viewWidth, int viewHeight, int topInset,
                       int bottomInset, double padding, int rightInset = 0);

// Where a fit puts the image on screen, in widget pixels, y down. For choosing
// between candidate fits; what is actually drawn is asked of the renderer.
struct ScreenRect
{
    double left = 0.0, top = 0.0, right = 0.0, bottom = 0.0;
    bool intersects(double l, double t, double r, double b) const
    {
        return left < r && l < right && top < b && t < bottom;
    }
};
ScreenRect imageOnScreen(const ViewFit &fit, double x0, double x1, double y0,
                         double y1, int viewWidth, int viewHeight);

// Where the colour scale goes, in widget pixels, y down.
//
// ⚑ It used to sit at fixed fractions of the viewport -- the right edge, from
// 6 to 48 per cent of the height up from the bottom -- which is the corner the
// coordinate-frame legend owns, so in any viewport short enough the legend
// covered the scale's lowest labels, and the image fit knew about neither. It
// is placed among the other overlays now: in the right-hand column between the
// field bar and the legend, and when that column is too short to read a scale
// in, beside the legend instead. The image is then fitted clear of it.
struct ScreenBox
{
    bool valid = false;
    int left = 0, top = 0, width = 0, height = 0;
    int right() const { return left + width; }
    int bottom() const { return top + height; }
    bool intersects(int l, int t, int r, int b) const
    {
        return left < r && l < right() && top < b && t < bottom();
    }
};

constexpr int kScaleBarWidth = 84;     // the bar and its labels
constexpr int kScaleBarShortest = 120; // below this its labels collide
constexpr int kOverlayMargin = 10;     // from the viewport's edge
constexpr int kOverlayGap = 6;         // between two overlays

// `freeTop`: the first row below the field bar (0 with none). `freeBottom`:
// the first row taken by a bar along the bottom (`viewHeight` with none).
// `legend`: where the legend is, invalid for none on screen. ⚑ A box with a
// flag, not a corner with -1 for absent: in a narrow viewport the legend is
// wider than the viewport and its left edge IS negative, which a sentinel
// read as "no legend" and put the scale straight under it. Invalid when no
// place is left for the scale at all.
ScreenBox placeScaleBar(int viewWidth, int viewHeight, int freeTop, int freeBottom,
                        const ScreenBox &legend);
