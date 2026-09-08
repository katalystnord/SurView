#include "gui/ComparisonWindow.h"

#include "core/ImageDecode.h"
#include "core/ImageRecord.h"
#include "gui/FieldView.h"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <vtkImageData.h>

#include <cmath>

namespace {

QString count(int value)
{
    return QLocale().toString(value);
}

// A measured quantity as the captions print it: three significant figures, with
// the unit, since "0.004" and "0.004 px" are different measurements.
QString quantity(double value, FieldChannel channel)
{
    const QString figure = QString::number(value, 'g', 3);
    if (fieldChannelIsDimensionless(channel))
        return figure;
    return figure + QStringLiteral(" ") + fieldChannelUnit(channel);
}

QString rangeText(const QVector<float> &cells, FieldChannel channel)
{
    bool any = false;
    double lowest = 0.0;
    double highest = 0.0;
    for (float value : cells) {
        if (std::isnan(value))
            continue;
        if (!any) {
            lowest = highest = value;
            any = true;
            continue;
        }
        lowest = std::min(lowest, double(value));
        highest = std::max(highest, double(value));
    }
    if (!any)
        return {};
    return QObject::tr("%1 to %2")
        .arg(quantity(lowest, channel), quantity(highest, channel));
}

QLabel *note(QWidget *parent, const QString &style = QString())
{
    auto *label = new QLabel(parent);
    // ⚑ Word-wrapped, and given room. A wrapped QLabel reports ONE line as its
    // minimum height, so beside a stretching native GL widget the layout hands
    // the picture the space and the words are simply painted over -- the defect
    // found on the Analysis panel and then again on the plot panel. Here every
    // caption also sits ABOVE the view it describes, for the same reason.
    label->setWordWrap(true);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    label->setMinimumHeight(label->fontMetrics().height() * 3);
    if (!style.isEmpty())
        label->setStyleSheet(style);
    return label;
}

}  // namespace

ComparisonWindow::ComparisonWindow(QWidget *parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle(tr("Measured against the known answer"));
    resize(1280, 640);

    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(12, 12, 12, 12);
    column->setSpacing(8);

    m_heading = new QLabel(this);
    m_heading->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 600;"));
    column->addWidget(m_heading);

    m_whatItShows = note(this, QStringLiteral("color: #9aa0a6;"));
    column->addWidget(m_whatItShows);

    m_exactness = note(this, QStringLiteral("color: #9aa0a6; font-size: 11px;"));
    column->addWidget(m_exactness);

    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Quantity"), this));
    m_channel = new QComboBox(this);
    connect(m_channel, &QComboBox::currentIndexChanged, this,
            [this](int) { redraw(); });
    row->addWidget(m_channel, 1);
    column->addLayout(row);

    // The channel's own sentence about what it is NOT, on its own row rather
    // than beside the selector: given 130 px of a wide window it wraps to eight
    // lines and grows over the pictures it is explaining.
    m_channelNote = note(this, QStringLiteral("color: #9aa0a6;"));
    column->addWidget(m_channelNote);

    auto *panels = new QHBoxLayout;
    panels->setSpacing(10);

    const struct { Panel *panel; QString title; } built[] = {
        {&m_measured, tr("Measured")},
        {&m_stated, tr("Stated by the example")},
        {&m_error, tr("Measured minus stated")},
    };
    for (const auto &entry : built) {
        auto *panelColumn = new QVBoxLayout;
        entry.panel->title = new QLabel(entry.title, this);
        entry.panel->title->setStyleSheet(
            QStringLiteral("font-weight: 600; padding-top: 4px;"));
        panelColumn->addWidget(entry.panel->title);

        entry.panel->caption = note(this, QStringLiteral("color: #b8bdc4;"));
        entry.panel->caption->setMinimumHeight(
            entry.panel->caption->fontMetrics().height() * 4);
        panelColumn->addWidget(entry.panel->caption);

        entry.panel->view = new FieldView(this);
        entry.panel->view->setMinimumSize(240, 200);
        panelColumn->addWidget(entry.panel->view, 1);

        panels->addLayout(panelColumn, 1);
    }
    column->addLayout(panels, 1);

    // One camera across the three, so panning or zooming one moves all of them.
    // Set up once here rather than per comparison: a camera shared again on
    // every redraw would reset the view a reader had just chosen.
    m_measured.view->shareCameraWith(m_stated.view);
    m_measured.view->shareCameraWith(m_error.view);
    for (FieldView *view : {m_measured.view, m_stated.view, m_error.view}) {
        connect(view, &FieldView::cameraMoved, this, [this] {
            for (FieldView *other : {m_measured.view, m_stated.view, m_error.view})
                other->renderNow();
        });
    }
}

void ComparisonWindow::compare(const CorrelationResult &result,
                               const KnownAnswer &answer,
                               const QString &referenceImagePath,
                               const QString &targetName)
{
    m_result = result;
    m_answer = answer;
    m_targetName = targetName;

    ImageRecord record;
    m_image = decodeImage(referenceImagePath, record);
    m_displayLow = record.dataMin;
    m_displayHigh = record.dataMax;

    // ⚑ Named by its FILE, not by a frame number. The stated answer counts
    // frames from the set's own reference, and the project tree and run log
    // count them from the first target, so the same picture is frame 3 to one
    // and frame 2 to the other. Two numbers for one thing on one screen is
    // worse than none: the file name is what both agree on.
    m_heading->setText(tr("%1, measured against the answer the %2 example states "
                          "for it")
                           .arg(targetName, answer.set));
    m_whatItShows->setText(answer.whatItShows);
    // The file's own account of why its answer is exact, quoted rather than
    // paraphrased: the claim that makes a difference map an ERROR map is the
    // data's claim, not ours.
    m_exactness->setText(tr("Why this answer is exact, in the example's own words: "
                            "%1 Stated in %2.")
                             .arg(answer.howTheAnswerIsExact, answer.source));

    rebuildChannels();
    redraw();

    QWidget::show();
    raise();
    activateWindow();
}

void ComparisonWindow::rebuildChannels()
{
    const QVariant kept = m_channel->currentData();

    QSignalBlocker blocked(m_channel);
    m_channel->clear();
    for (const FieldChannelInfo &info : offeredFieldChannels()) {
        // Only what the stated answer can speak to, and only what this run
        // actually produced: a strain channel offered over a run that fitted no
        // strain is an empty panel with no explanation of itself.
        if (!knownAnswerCoversChannel(info.channel))
            continue;
        if (info.fromStrainFit && !m_result.hasStrain())
            continue;
        m_channel->addItem(info.unit.isEmpty()
                               ? info.name
                               : tr("%1 (%2)").arg(info.name, info.unit),
                           int(info.channel));
    }
    const int previous = m_channel->findData(kept);
    m_channel->setCurrentIndex(previous >= 0 ? previous : 0);
}

FieldChannel ComparisonWindow::channel() const
{
    return FieldChannel(m_channel->currentData().toInt());
}

AccuracyReport ComparisonWindow::report() const
{
    return accuracyAgainstStated(m_result, channel(), m_answer);
}

void ComparisonWindow::redraw()
{
    if (m_channel->currentIndex() < 0 || !m_answer.valid)
        return;

    const FieldChannel channel = this->channel();
    const QVector<float> measured = layoutField(m_result, channel);
    const QVector<float> stated = layoutStatedField(m_result, channel, m_answer);
    const QVector<float> error = layoutErrorField(m_result, channel, m_answer);

    m_channelNote->setText(fieldChannelNote(channel));

    for (FieldView *view : {m_measured.view, m_stated.view, m_error.view})
        view->showImage(m_image, m_displayLow, m_displayHigh);

    // ⚑ ONE scale for the measured field and the stated one. See the header:
    // ranged separately, a measurement wrong by half is painted in exactly the
    // colours of the answer it missed.
    double lowest = 0.0;
    double highest = 0.0;
    const bool shared = sharedColourRange(measured, stated,
                                          fieldChannelIsCentredOnZero(channel),
                                          lowest, highest);
    // ⚑ SHORT titles on these bars. A scale bar in a third of a window is not
    // the field bar in a whole viewport: the full name and unit drew over the
    // topmost tick label and neither could be read. The quantity is named in
    // full by the selector directly above, which governs all three panels at
    // once, so the bar need only carry the unit.
    const QString scaleTitle = fieldChannelUnit(channel).isEmpty()
                                   ? fieldChannelName(channel)
                                   : fieldChannelUnit(channel);
    const QString errorTitle = fieldChannelUnit(channel).isEmpty()
                                   ? tr("error")
                                   : tr("error (%1)").arg(fieldChannelUnit(channel));
    if (shared) {
        const int digits = fieldScaleSignificantDigits(lowest, highest);
        m_measured.view->showField(measured, m_result.gridColumns, m_result.gridRows,
                                   m_result.originX, m_result.originY, m_result.step,
                                   scaleTitle, fieldChannelIsCentredOnZero(channel), false,
                                   lowest, highest, digits);
        m_stated.view->showField(stated, m_result.gridColumns, m_result.gridRows,
                                 m_result.originX, m_result.originY, m_result.step,
                                 scaleTitle, fieldChannelIsCentredOnZero(channel), false,
                                 lowest, highest, digits);
    }

    double errorLow = 0.0;
    double errorHigh = 0.0;
    if (errorColourRange(error, errorLow, errorHigh)) {
        m_error.view->showField(error, m_result.gridColumns, m_result.gridRows,
                                m_result.originX, m_result.originY, m_result.step,
                                errorTitle, true, false,
                                errorLow, errorHigh,
                                fieldScaleSignificantDigits(errorLow, errorHigh));
    }

    const AccuracyReport accuracy = report();
    const int attempted = int(m_result.points.size());

    m_measured.caption->setText(
        tr("What SurView measured: %1 of the %2 points it attempted. %3. "
           "Blank where the run measured nothing -- never a zero.")
            .arg(count(accuracy.compared), count(attempted),
                 rangeText(measured, channel)));

    m_stated.caption->setText(
        tr("The answer this frame states, at every one of the %1 points the run "
           "attempted, including the ones it could not measure. %2. On the same "
           "colour scale as the panel beside it, which is what makes the two "
           "comparable at a glance.")
            .arg(count(attempted), rangeText(stated, channel)));

    if (accuracy.valid) {
        m_error.caption->setText(
            tr("Measured minus stated, at the %1 points measured. Mean error %2, "
               "worst %3 at (%4, %5), RMS %6. Centred on zero, because zero here "
               "is the answer being right.")
                .arg(count(accuracy.compared),
                     quantity(accuracy.meanAbsolute, channel),
                     quantity(accuracy.worstAbsolute, channel),
                     QString::number(int(accuracy.worstAtX)),
                     QString::number(int(accuracy.worstAtY)),
                     quantity(accuracy.rms, channel)));
    } else {
        m_error.caption->setText(
            tr("Nothing was measured in this quantity, so there is nothing to "
               "compare. This is an absence, not an error of zero."));
    }
}
