#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// The example data that ships with SurView, found on disk.
//
// The examples were committed to the repository and reachable from nothing: no
// install rule put them anywhere, and the application had no way to open one.
// A first-time reader met an empty window and had to go and find speckle images
// before the software could do anything, while a set with an exactly known
// answer sat in the source tree they never looked at.
//
// ⚑ Discovered rather than listed. A list compiled into the binary drifts from
// the folder in both directions: an example added is not offered until somebody
// remembers to register it, and one removed becomes a menu entry that fails
// when pressed.

struct ExampleSet
{
    // As a menu shows it, e.g. "Rotation".
    QString name;

    // Which family it belongs to, from the top folder under the examples root:
    // "Real" or "Synthetic". Two sets can carry the same name -- the real
    // rotation plate and the synthetic rotation both do -- and the family is
    // the difference that matters, since one has an exactly known answer.
    QString group;

    // One line about what it is, including the frame count, since that is what
    // says whether this is a pair or a sequence before anyone opens it.
    QString summary;

    // Every frame, in sequence order. The first is the reference.
    QStringList frames;

    QString reference() const { return frames.isEmpty() ? QString() : frames.first(); }
    QStringList targets() const { return frames.mid(1); }
};

// Every example under any of `roots`, in a stable order.
//
// A set is a run of images in one folder sharing a stem before their trailing
// digits: `rotation_00.tif` and `rotation_01.tif` are one set, `shear_00.tif`
// beside them is another. Grouping by folder alone would join them, and eight
// frames of two different experiments correlate perfectly well while meaning
// nothing at all.
//
// Frames come back in sequence order, not string order, for the reason
// core/Sequence.h exists. A run of fewer than two frames is not offered: there
// would be nothing to correlate against.
QVector<ExampleSet> findExamples(const QStringList &roots);

// Where to look, given the running executable's directory: beside the binary
// for an installed build, and up the tree for a build inside the source.
QStringList exampleSearchPaths(const QString &applicationDirectory);
