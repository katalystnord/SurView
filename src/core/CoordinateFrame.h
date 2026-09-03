#pragma once

#include <QString>

// The one coordinate convention everything in this application is expressed in,
// and the words it is described to a reader with.
//
// ⚑ x runs RIGHT, y runs DOWN, and the origin is the centre of the top-left
// pixel. That is the image's own row order, the viewport's world coordinates,
// the engine's `Point2D`, and what the exported file states about itself. There
// is no conversion anywhere between them, which is exactly why the convention
// has to be stated once and stated correctly.
//
// It is also the convention a reader is most likely to assume wrongly, because
// every graph they have ever seen counts y upward. `CLAUDE.md` explains why
// this had to be pinned down, and the manual has a chapter on it. Both are
// prose somebody has to go and find. This is the same fact, on screen, beside
// the picture it describes.
//
// The wording lives here rather than in the widget that draws it for the reason
// every sentence of this kind does in this code base: a widget is where such a
// sentence quietly rots, and here a test can hold it to saying all three of the
// things that make it useful.

enum class Axis { X, Y };

// "x" or "y", short enough to draw beside an arrow.
QString axisLabel(Axis axis);

// Which way that axis runs, in one word a reader can check against the arrow
// it is drawn next to.
QString axisDirection(Axis axis);

// The whole convention in one sentence: both directions and where the origin
// sits. Any two of the three still leave a point unplaceable.
QString coordinateFrameCaption();
