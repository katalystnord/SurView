// The same measured field, written for a reader who will open it in a
// spreadsheet rather than in ParaView.
//
// WHY THIS EXISTS. The .vtu is the right format for the ecosystem SurView sits
// in, and it is unreadable without a VTK-aware tool: it is binary, base64 and
// compressed, so even its provenance cannot be grepped. A working scientist
// checking three numbers should not have to install ParaView to do it.
//
// ⚑ The trap this file is mostly about is that CSV has no not-a-number. Every
// rule the .vtu keeps has to be kept again here, in a format that makes it
// easier to get wrong: a rejected point written as 0 is a perfectly good row
// that plots as a real measurement of no movement, and nothing downstream can
// tell it from one. An empty cell is the only thing every spreadsheet reads as
// "no value", so that is what an unmeasured quantity gets, and the file says so
// in its own header rather than leaving it to be inferred.
//
// NEGATIVE CHECK (2026-08-31): nine breaks, each reverted after. All nine
// turned the case named for them red, and several turned more than one:
//   - an unmeasured value written as 0 instead of empty (reddened all three of
//     the displacement, strain and reliability cases at once, which is the
//     point: it is one rule, kept in three places)
//   - only solved points get a row
//   - the strain columns always present
//   - the header never saying what an empty cell means
//   - the file never stating its coordinate frame
//   - an empty result written rather than refused
//   - the rejection reason dropped
//   - a rejected point's negative status code written as its correlation
//   - the provenance block left out

#include "core/Correlation.h"
#include "core/FieldExport.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace
{

// Two points side by side, both solved, with strain fitted at both.
CorrelationResult plainResult()
{
    CorrelationResult result;
    result.gridColumns = 2;
    result.gridRows = 1;
    result.originX = 10.f;
    result.originY = 20.f;
    result.step = 5;
    result.strainRequested = true;
    result.strainFitted = 2;
    result.converged = 2;

    for (int i = 0; i < 2; i++) {
        CorrelationPoint point;
        point.gridIndex = i;
        point.x = 10.f + float(i * 5);
        point.y = 20.f;
        point.u = 2.5f;
        point.v = -0.5f;
        point.zncc = 0.98f;
        point.converged = true;
        point.exx = 1.9e-3f;
        point.eyy = -6.0e-4f;
        point.exy = 2.0e-5f;
        point.strainFitted = true;
        point.noiseFloor = 0.0034f;
        point.noiseFloorMeasured = true;
        point.conditioning = 0.27f;
        point.conditioningMeasured = true;
        result.points.append(point);
    }
    return result;
}

FieldProvenance plainProvenance()
{
    FieldProvenance provenance;
    provenance.reference.filePath = QStringLiteral("/tmp/reference.tif");
    provenance.reference.sha256 = QStringLiteral("abc123");
    provenance.target.filePath = QStringLiteral("/tmp/target.tif");
    provenance.target.sha256 = QStringLiteral("def456");
    provenance.applicationVersion = QStringLiteral("0.1.0");
    provenance.enginePin = QStringLiteral("c2d989e");
    return provenance;
}

QString contentsOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(file.readAll());
}

// The rows that carry data, with the commented header block and the column
// names dropped.
QStringList dataRows(const QString &text)
{
    QStringList rows;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (line.trimmed().isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        rows << line;
    }
    if (!rows.isEmpty())
        rows.removeFirst();   // the column names
    return rows;
}

QString columnNames(const QString &text)
{
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (line.trimmed().isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        return line;
    }
    return QString();
}

// The value of one named column in one row.
QString cell(const QString &text, int row, const QString &column)
{
    const QStringList names = columnNames(text).split(QLatin1Char(','));
    const int index = names.indexOf(column);
    if (index < 0)
        return QStringLiteral("<no such column: %1>").arg(column);
    const QStringList rows = dataRows(text);
    if (row >= rows.size())
        return QStringLiteral("<no such row>");
    const QStringList values = rows.at(row).split(QLatin1Char(','));
    return index < values.size() ? values.at(index) : QString();
}

}  // namespace

class TestFieldCsv : public QObject
{
    Q_OBJECT

private slots:
    void every_attempted_point_gets_a_row();
    void the_columns_name_themselves_and_their_units();
    void a_measured_point_carries_its_measurement();

    // The rule the whole file exists to keep.
    void a_rejected_point_leaves_its_displacement_empty_rather_than_zero();
    void a_rejected_point_states_its_rejection_rather_than_leaving_it_inferred();
    void an_unfitted_strain_is_empty_rather_than_three_zeros();
    void an_unestablished_reliability_is_empty_rather_than_a_flattering_zero();
    void strain_columns_are_absent_when_the_run_never_asked_for_strain();

    void the_file_says_what_an_empty_cell_means();
    void the_file_states_its_own_coordinate_frame();
    void the_file_carries_the_provenance_the_vtu_carries();

    void a_result_with_no_points_is_refused_rather_than_written_empty();
    void a_path_that_cannot_be_written_is_reported_as_a_reason();
};

void TestFieldCsv::every_attempted_point_gets_a_row()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("field.csv"));

    CorrelationResult result = plainResult();
    result.points[1].converged = false;   // attempted, not solved

    QVERIFY(writeFieldCsv(path, result, plainProvenance()).isEmpty());

    // Geometry says where the instrument was pointed; the columns say what came
    // back. A difficult specimen must not export as a smaller one, which is the
    // same rule buildFieldMesh() keeps for the .vtu's cells.
    QCOMPARE(dataRows(contentsOf(path)).size(), 2);
}

void TestFieldCsv::the_columns_name_themselves_and_their_units()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("field.csv"));
    QVERIFY(writeFieldCsv(path, plainResult(), plainProvenance()).isEmpty());

    const QString names = columnNames(contentsOf(path));
    for (const QString &expected : {QStringLiteral("x_px"), QStringLiteral("y_px"),
                                    QStringLiteral("u_px"), QStringLiteral("v_px"),
                                    QStringLiteral("zncc"), QStringLiteral("solved"),
                                    QStringLiteral("noise_floor_px"),
                                    QStringLiteral("match_conditioning")}) {
        QVERIFY2(names.split(QLatin1Char(',')).contains(expected),
                 qPrintable(QStringLiteral("no column %1 in: %2").arg(expected, names)));
    }
}

void TestFieldCsv::a_measured_point_carries_its_measurement()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("field.csv"));
    QVERIFY(writeFieldCsv(path, plainResult(), plainProvenance()).isEmpty());

    const QString text = contentsOf(path);
    QCOMPARE(cell(text, 0, QStringLiteral("x_px")).toDouble(), 10.0);
    QCOMPARE(cell(text, 1, QStringLiteral("x_px")).toDouble(), 15.0);
    QVERIFY(qAbs(cell(text, 0, QStringLiteral("u_px")).toDouble() - 2.5) < 1e-6);
    QVERIFY(qAbs(cell(text, 0, QStringLiteral("v_px")).toDouble() + 0.5) < 1e-6);
}

void TestFieldCsv::a_rejected_point_leaves_its_displacement_empty_rather_than_zero()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("field.csv"));

    CorrelationResult result = plainResult();
    CorrelationPoint &point = result.points[1];
    point.converged = false;
    point.u = 0.f;
    point.v = 0.f;
    point.zncc = -3.f;

    QVERIFY(writeFieldCsv(path, result, plainProvenance()).isEmpty());
    const QString text = contentsOf(path);

    // ⚑ Empty, not 0. A zero here is a perfectly good row that plots as a real
    // measurement of no movement, and no reader downstream can tell them apart.
    QCOMPARE(cell(text, 1, QStringLiteral("u_px")), QString());
    QCOMPARE(cell(text, 1, QStringLiteral("v_px")), QString());

    // And the engine's negative status code must not be written in the column a
    // reader will average as a correlation.
    QCOMPARE(cell(text, 1, QStringLiteral("zncc")), QString());
}

void TestFieldCsv::a_rejected_point_states_its_rejection_rather_than_leaving_it_inferred()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("field.csv"));

    CorrelationResult result = plainResult();
    result.points[1].converged = false;
    result.points[1].failureReason = QStringLiteral("subset outside the image");

    QVERIFY(writeFieldCsv(path, result, plainProvenance()).isEmpty());
    const QString text = contentsOf(path);

    // Stated outright rather than inferred from the holes: a row of empty cells
    // could as easily be a bug in the writer as a point the solver refused.
    QCOMPARE(cell(text, 0, QStringLiteral("solved")), QStringLiteral("1"));
    QCOMPARE(cell(text, 1, QStringLiteral("solved")), QStringLiteral("0"));
    QVERIFY2(text.contains(QStringLiteral("subset outside the image")),
             qPrintable(text));
}

void TestFieldCsv::an_unfitted_strain_is_empty_rather_than_three_zeros()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("field.csv"));

    CorrelationResult result = plainResult();
    result.points[1].strainFitted = false;
    result.points[1].exx = 0.f;
    result.points[1].eyy = 0.f;
    result.points[1].exy = 0.f;

    QVERIFY(writeFieldCsv(path, result, plainProvenance()).isEmpty());
    const QString text = contentsOf(path);

    QCOMPARE(cell(text, 1, QStringLiteral("exx")), QString());
    QCOMPARE(cell(text, 1, QStringLiteral("eyy")), QString());
    QCOMPARE(cell(text, 1, QStringLiteral("exy")), QString());
    QVERIFY(!cell(text, 0, QStringLiteral("exx")).isEmpty());
}

void TestFieldCsv::an_unestablished_reliability_is_empty_rather_than_a_flattering_zero()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("field.csv"));

    CorrelationResult result = plainResult();
    result.points[1].noiseFloorMeasured = false;
    result.points[1].conditioningMeasured = false;

    QVERIFY(writeFieldCsv(path, result, plainProvenance()).isEmpty());
    const QString text = contentsOf(path);

    // Zero is the FLATTERING reading for both: a perfect measurement and a
    // perfectly sharp cost. Neither is reachable as a real result.
    QCOMPARE(cell(text, 1, QStringLiteral("noise_floor_px")), QString());
    QCOMPARE(cell(text, 1, QStringLiteral("match_conditioning")), QString());
}

void TestFieldCsv::strain_columns_are_absent_when_the_run_never_asked_for_strain()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("field.csv"));

    CorrelationResult result = plainResult();
    result.strainRequested = false;
    result.strainFitted = 0;
    for (CorrelationPoint &point : result.points)
        point.strainFitted = false;

    QVERIFY(writeFieldCsv(path, result, plainProvenance()).isEmpty());

    // Absent, not present and empty. A column of empty strain cells reads as a
    // fit that failed everywhere, which is a far more alarming statement than
    // "not attempted" -- the same distinction the .vtu makes by omitting the
    // arrays entirely.
    const QStringList names = columnNames(contentsOf(path)).split(QLatin1Char(','));
    QVERIFY2(!names.contains(QStringLiteral("exx")),
             qPrintable(names.join(QLatin1Char(','))));
}

void TestFieldCsv::the_file_says_what_an_empty_cell_means()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("field.csv"));
    QVERIFY(writeFieldCsv(path, plainResult(), plainProvenance()).isEmpty());

    // The convention has to travel with the file. A reader who opens this in a
    // spreadsheet a year from now has nothing else to go on.
    const QString text = contentsOf(path);
    QVERIFY2(text.contains(QStringLiteral("empty"), Qt::CaseInsensitive),
             qPrintable(text.left(2000)));
}

void TestFieldCsv::the_file_states_its_own_coordinate_frame()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("field.csv"));
    QVERIFY(writeFieldCsv(path, plainResult(), plainProvenance()).isEmpty());

    // The same trap as the .vtu's, one format further out: a reader who assumes
    // y runs up draws the field mirrored against the photograph and cannot
    // tell, because the picture is plausible either way.
    const QString text = contentsOf(path);
    QVERIFY2(text.contains(QStringLiteral("y down"), Qt::CaseInsensitive),
             qPrintable(text.left(2000)));
}

void TestFieldCsv::the_file_carries_the_provenance_the_vtu_carries()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("field.csv"));
    QVERIFY(writeFieldCsv(path, plainResult(), plainProvenance()).isEmpty());

    const QString text = contentsOf(path);
    // Unlike the .vtu's, this provenance IS greppable, which is half the reason
    // the format is worth having at all.
    for (const QString &expected : {QStringLiteral("/tmp/reference.tif"),
                                    QStringLiteral("abc123"),
                                    QStringLiteral("/tmp/target.tif"),
                                    QStringLiteral("def456"),
                                    QStringLiteral("c2d989e")}) {
        QVERIFY2(text.contains(expected), qPrintable(expected));
    }
}

void TestFieldCsv::a_result_with_no_points_is_refused_rather_than_written_empty()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("field.csv"));

    const QString reason = writeFieldCsv(path, CorrelationResult(),
                                         plainProvenance());
    QVERIFY2(!reason.isEmpty(), "an empty field was written without complaint");
    QVERIFY2(!QFile::exists(path), "an empty file was left behind");
}

void TestFieldCsv::a_path_that_cannot_be_written_is_reported_as_a_reason()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("no/such/place/field.csv"));

    const QString reason = writeFieldCsv(path, plainResult(), plainProvenance());
    QVERIFY2(!reason.isEmpty(), "a failed write reported success");

    // Named, because "could not save" leaves a user with nothing to act on.
    QVERIFY2(reason.contains(QStringLiteral("field.csv")), qPrintable(reason));
}

QTEST_MAIN(TestFieldCsv)
#include "test_field_csv.moc"
