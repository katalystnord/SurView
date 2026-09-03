// How the application says which way its axes point.
//
// WHY THIS EXISTS AS ITS OWN THING. The coordinate frame is the one convention
// every other number in the application is expressed in, and it is the one a
// reader is most likely to assume wrongly: images count y DOWNWARD from a
// top-left origin, and every graphing convention a person has ever met counts
// it upward. `CLAUDE.md` has a section about this and the manual has a chapter,
// and both are prose that has to be found and read.
//
// The fix is to state it on screen, permanently, next to the picture it
// describes. The wording lives here rather than in the widget for the reason
// every other sentence of that kind does: a widget is where such a sentence
// quietly rots, and here a test can hold it to saying all three things that
// make it useful.

#include "core/CoordinateFrame.h"

#include <QTest>

class TestCoordinateFrame : public QObject
{
    Q_OBJECT

private slots:
    void the_caption_states_both_directions_and_the_origin();
    void the_axis_labels_are_short_enough_to_draw_and_say_which_way();
    void nothing_here_claims_the_convention_a_reader_probably_expects();
};

void TestCoordinateFrame::the_caption_states_both_directions_and_the_origin()
{
    // All three, because any two of them still leave the frame ambiguous: a
    // reader who knows x goes right and the origin is at a corner still cannot
    // place a point without knowing which way y runs.
    const QString caption = coordinateFrameCaption();

    QVERIFY2(!caption.isEmpty(), "the frame has to say something");
    QVERIFY2(caption.contains(QStringLiteral("right"), Qt::CaseInsensitive),
             "the caption does not say which way x runs");
    QVERIFY2(caption.contains(QStringLiteral("down"), Qt::CaseInsensitive),
             "the caption does not say which way y runs, which is the whole point");
    QVERIFY2(caption.contains(QStringLiteral("top"), Qt::CaseInsensitive)
                 || caption.contains(QStringLiteral("origin"), Qt::CaseInsensitive),
             "the caption does not say where the origin sits");
}

void TestCoordinateFrame::the_axis_labels_are_short_enough_to_draw_and_say_which_way()
{
    // These are drawn beside arrows in a corner of the viewport, so they have
    // to be legible at a glance and short enough not to need the space the
    // picture is using.
    QCOMPARE(axisLabel(Axis::X), QStringLiteral("x"));
    QCOMPARE(axisLabel(Axis::Y), QStringLiteral("y"));
    QVERIFY(axisDirection(Axis::X).contains(QStringLiteral("right"), Qt::CaseInsensitive));
    QVERIFY(axisDirection(Axis::Y).contains(QStringLiteral("down"), Qt::CaseInsensitive));
}

void TestCoordinateFrame::nothing_here_claims_the_convention_a_reader_probably_expects()
{
    // ⚑ The failure this guards against is a caption that is quietly WRONG
    // rather than absent. y runs down; a caption saying "up" would be worse
    // than no caption at all, because a reader would believe it and every
    // vector they read off the screen would be mirrored.
    const QString caption = coordinateFrameCaption().toLower();
    QVERIFY2(!caption.contains(QStringLiteral("y up")),
             "the caption claims the graphing convention, which is the opposite "
             "of what the images and the engine actually use");
    QVERIFY2(!caption.contains(QStringLiteral("bottom-left")),
             "the origin is the top-left pixel, not the bottom-left one");
}

QTEST_MAIN(TestCoordinateFrame)
#include "test_coordinate_frame.moc"
