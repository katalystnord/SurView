#include "core/DicChallenge.h"

#include "core/Correlation.h"

#include <QFileInfo>

#include <algorithm>
#include <cmath>

namespace
{

// One axis token, as the challenge writes it: the letter, then a number that
// must consume the whole of the rest of the token. `Xenon` carries an X and
// states nothing; reading it as a shift of some default would be the absence
// dressed as an answer.
bool axisValue(const QString &token, QChar axis, double *out)
{
    if (token.size() < 2 || token.at(0).toUpper() != axis)
        return false;

    bool ok = false;
    const double value = token.mid(1).toDouble(&ok);
    if (!ok)
        return false;

    *out = value;
    return true;
}

}  // namespace

DicChallengeShift dicChallengeShiftFromFileName(const QString &fileName)
{
    // completeBaseName, not baseName: the latter stops at the FIRST dot, which
    // in `Sample3-003 X0.30 Y0.30 N2 C0 R0.tif` is the one inside the shift
    // itself, and the name would be cut to `Sample3-003 X0`.
    const QString name = QFileInfo(fileName).completeBaseName();

    DicChallengeShift shift;
    bool haveX = false;
    bool haveY = false;

    const QStringList tokens = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        double value = 0.0;
        if (!haveX && axisValue(token, QLatin1Char('X'), &value)) {
            shift.x = value;
            haveX = true;
        } else if (!haveY && axisValue(token, QLatin1Char('Y'), &value)) {
            shift.y = value;
            haveY = true;
        }
    }

    // Both axes or neither. Half an answer is not an answer: a name carrying
    // only X would otherwise state a y shift of zero that nobody wrote down.
    shift.stated = haveX && haveY;
    return shift;
}

ShiftAgreement agreementWithStatedShift(const QVector<CorrelationPoint> &points,
                                        const DicChallengeShift &stated)
{
    ShiftAgreement agreement;
    if (!stated.stated)
        return agreement;

    double sumU = 0.0;
    double sumV = 0.0;
    for (const CorrelationPoint &point : points) {
        if (!point.converged)
            continue;
        sumU += point.u;
        sumV += point.v;
        agreement.solved++;
    }

    if (agreement.solved == 0)
        return agreement;

    agreement.measured = true;
    agreement.meanU = sumU / agreement.solved;
    agreement.meanV = sumV / agreement.solved;
    agreement.errorX = agreement.meanU - stated.x;
    agreement.errorY = agreement.meanV - stated.y;
    agreement.worstError = std::max(std::abs(agreement.errorX),
                                    std::abs(agreement.errorY));
    return agreement;
}
