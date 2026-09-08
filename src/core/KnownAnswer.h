#pragma once

#include "core/FieldLayout.h"
#include "core/StrainFit.h"

#include <QString>
#include <QVector>

struct CorrelationResult;

// The answer an example states about itself, and what a measurement of it comes
// to when set against that answer.
//
// WHY THIS EXISTS. examples/synthetic states the exact deformation of every
// frame it ships, and until now only the test suite ever read it. A reader with
// no reason yet to believe a number this application produced could be told
// that 10,733 points converged, and nothing on screen ever put one of those
// numbers beside the answer it was supposed to be.
//
// ⚑ THE ANSWER IS EXACT BY CONSTRUCTION, NOT ACCURATE TO WITHIN A WARP. The
// generator renders each frame from an analytic pattern of Gaussian blobs under
// a uniform deformation gradient, which maps a Gaussian to a Gaussian exactly,
// so no pixel is ever resampled. That is what makes a difference map here an
// error map rather than a comparison of two estimates, and it is why this is
// worth putting on screen at all. The file says so in its own words, which is
// what `howTheAnswerIsExact` carries: the claim is the data's, not ours.
//
// ⚑ AND THE STATED ANSWER IS A DEFORMATION, NOT A FIELD OF NUMBERS. What is
// read from the file is one deformation gradient and one rigid shift per frame,
// from which the displacement at any pixel follows exactly. Nothing here
// interpolates a stated field, because there is no stated field to interpolate.

// One frame's stated answer, read from the ground_truth.json that ships beside
// the images.
struct KnownAnswer
{
    bool valid = false;

    QString set;            // "rotation"
    int frame = -1;         // position in the set, 0 being its own reference
    QString frameFile;      // the image this answer is about
    QString whatItShows;    // the set's own sentence about itself
    QString howTheAnswerIsExact;   // the file's own account of why it is exact
    QString source;         // the file it was read from

    // The deformation this frame was rendered under, in SurView's own frame:
    // a reference pixel at (x, y) is at f * (x, y) + shift in the target.
    double f00 = 1.0, f01 = 0.0;
    double f10 = 0.0, f11 = 1.0;
    double shiftX = 0.0, shiftY = 0.0;

    // How much of it the file says this frame carries: 3 for the rotation set's
    // three degrees, 0.02 for a strain of two per cent. Bare, because the file
    // states the number and the set's own sentence says what it counts.
    QString amount;
};

// Where a stated answer for `imagePath` would live: a ground_truth.json in the
// image's own directory. Named separately from the reading below so a caller
// can say WHERE it looked when it found nothing, rather than only that it
// failed.
QString statedAnswerPathFor(const QString &imagePath);

// The stated answer for one image, or an invalid one when there is no such file
// or the file does not name this image.
//
// Matched by FILE NAME, never by position. A set's frames and the application's
// target list are two orderings of the same images maintained in different
// places, and comparing a field against the wrong frame's answer would produce
// a plausible error map of an experiment nobody ran.
KnownAnswer knownAnswerFor(const QString &imagePath);
KnownAnswer knownAnswerFromFile(const QString &groundTruthPath,
                                const QString &imageFileName);

// True where the stated answer can speak to the channel at all. It states a
// deformation, so it says everything about displacement and strain and nothing
// whatever about how well a subset could be measured: setting a noise floor or
// a repair flag against it would be comparing two different kinds of thing.
bool knownAnswerCoversChannel(FieldChannel channel);

// What the stated deformation puts at one reference pixel, in the channel's own
// quantity.
//
// ⚑ Strain is evaluated in the SAME MEASURE the run used, because the two
// measures genuinely disagree about the same deformation: a rigid rotation of
// three degrees is exactly zero strain in the Green-Lagrange form and -0.0014
// in the linear one, which is the linear form's own well-known error and not a
// fault in the measurement. Evaluating the stated answer in one measure and the
// run in the other would show that disagreement as an error of the instrument.
double statedValue(const KnownAnswer &answer, FieldChannel channel,
                   double x, double y, StrainMeasure measure);

// The stated value in every cell the run put a point in, not-a-number
// elsewhere.
//
// ⚑ Every point the run ATTEMPTED, including the ones it failed to measure. The
// stated answer is known there whether or not the instrument managed to read
// it, and holding the two panels to the same shape would hide exactly what a
// reader is looking for -- where the measurement is missing.
QVector<float> layoutStatedField(const CorrelationResult &result,
                                 FieldChannel channel, const KnownAnswer &answer);

// Measured minus stated, in every cell where the run MEASURED a value.
//
// Not-a-number everywhere else, including at every point the solver rejected. A
// rejected point is not an error of zero any more than it is a displacement of
// zero, and drawn as one it would take the colour of a perfect measurement.
QVector<float> layoutErrorField(const CorrelationResult &result,
                                FieldChannel channel, const KnownAnswer &answer);

// How far a measured channel is from the answer the frame states.
//
// Three figures rather than one, for the same reason reliability is two: a mean
// says what the field does typically, the worst says what it does at its worst,
// and the two are answers to different questions. The worst carries where it
// happened, because on a fitted quantity the answer is usually "at the edge",
// which is a property of the fit rather than of the measurement.
struct AccuracyReport
{
    bool valid = false;
    int compared = 0;        // points that were measured AND have a stated value
    int attempted = 0;       // points the run placed at all
    double meanAbsolute = 0.0;
    double worstAbsolute = 0.0;
    double rms = 0.0;
    double worstAtX = 0.0;
    double worstAtY = 0.0;
};

AccuracyReport accuracyAgainstStated(const CorrelationResult &result,
                                     FieldChannel channel,
                                     const KnownAnswer &answer);

// The one colour range a measured field and the stated answer are BOTH drawn
// on. False when neither holds a value, which is no scale at all rather than a
// range of zero.
//
// ⚑ The rule the comparison rests on. Two fields drawn on their own scales look
// alike however far apart they are, because each stretches its own colours over
// its own range: a measurement wrong by half is painted in exactly the colours
// of the answer it missed. `centredOnZero` carries core/FieldLayout.h's own
// rule through unchanged -- a strain scale sits about zero because zero strain
// is a physical state.
bool sharedColourRange(const QVector<float> &measured, const QVector<float> &stated,
                       bool centredOnZero, double &lowest, double &highest);

// The range an error map is drawn on, always symmetric about zero.
//
// ⚑ In EVERY channel, displacement included, because zero error is the answer
// being right and that is a physical state whatever the quantity. Ranged over
// the errors themselves, a field wrong by between 0.4 and 0.5 px everywhere
// would spend the whole scale on that tenth of a pixel and read as a field with
// a hole in it rather than as one that is uniformly wrong.
bool errorColourRange(const QVector<float> &error, double &lowest, double &highest);
