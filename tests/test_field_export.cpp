// The measurement leaving the application.
//
// WHY THESE CASES. An export is the one operation whose output nobody in this
// codebase ever looks at again, so every defect in it is found by a stranger,
// in another program, weeks later, with no way to tell a bug from a result.
// Two failures matter more than the rest and both are asserted below by reading
// the file back rather than by trusting the writer:
//
//   - a value that was never measured arriving as 0. In ParaView a field of
//     zeros is a perfectly good blue region, indistinguishable from a real
//     measurement of no movement. Not-a-number is the only value that cannot
//     be mistaken for one.
//   - the file arriving with no account of how it was made. A displacement
//     field with no reference image, no solver and no settings is a picture,
//     not a measurement, and tenet 10 puts provenance with the record.
//
// The frame is asserted too. SurView works in image pixel coordinates with y
// DOWNWARD, which is not what a mesh viewer assumes, so the file has to say so
// in its own field data rather than leave the reader to discover it.
//
// NEGATIVE CHECK (2026-08-19): three, all watched red.
//   - provenance suppressed: the two cases that read it back both failed.
//   - the solver's leftover u and v exported in place of not-a-number: the two
//     "exports as nothing not as zero" cases failed.
//   - the last case, a_path_that_cannot_be_written..., failed against the FIRST
//     implementation and found a real trap. vtkXMLUnstructuredGridWriter::
//     Write() returns 1 -- success -- for a path in a directory that does not
//     exist, while GetErrorCode() reports CannotOpenFileError and no file
//     appears. Confirmed against VTK 9.5 directly, in a standalone program, not
//     inferred from this test. A writer trusted on its return value tells a
//     user their measurement is saved when it is not.

#include "core/Correlation.h"
#include "core/FieldExport.h"
#include "core/FieldMesh.h"
#include "core/ImageRecord.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkFieldData.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkStringArray.h>
#include <vtkUnstructuredGrid.h>
#include <vtkXMLUnstructuredGridReader.h>

#include <cmath>

namespace {

CorrelationResult twoByTwo()
{
    CorrelationResult result;
    result.gridColumns = 2;
    result.gridRows = 2;
    result.originX = 30.f;
    result.originY = 40.f;
    result.step = 6;

    for (int i = 0; i < 4; i++) {
        CorrelationPoint point;
        point.gridIndex = i;
        point.x = result.originX + float((i % 2) * result.step);
        point.y = result.originY + float((i / 2) * result.step);
        point.u = 1.5f + i;
        point.v = -0.5f;
        point.zncc = 0.98f;
        point.converged = true;
        point.exx = 0.001f * (i + 1);
        point.eyy = -0.002f;
        point.exy = 0.0005f;
        point.strainFitted = true;
        result.points.append(point);
    }
    for (int i = 0; i < 4; i++) {
        result.points[i].noiseFloor = 0.004f + 0.001f * i;
        result.points[i].noiseFloorMeasured = true;
        result.points[i].conditioning = 0.15f + 0.01f * i;
        result.points[i].conditioningMeasured = true;
    }
    result.noiseFloorMeasured = 4;
    result.referenceNoise = 1.75;
    result.converged = 4;
    result.strainRequested = true;
    result.strainFitted = 4;
    result.strainRadius = 25.0;
    result.strainMeasure = StrainMeasure::Cauchy;
    result.secondsElapsed = 1.25;
    return result;
}

FieldProvenance provenanceFor()
{
    FieldProvenance provenance;
    provenance.reference.fileName = QStringLiteral("ref.tif");
    provenance.reference.filePath = QStringLiteral("/somewhere/ref.tif");
    provenance.reference.sha256 = QStringLiteral("aaaabbbbccccdddd");
    provenance.target.fileName = QStringLiteral("tar.tif");
    provenance.target.filePath = QStringLiteral("/somewhere/tar.tif");
    provenance.target.sha256 = QStringLiteral("1111222233334444");
    provenance.settings.subsetRadius = 16;
    provenance.settings.gridStep = 6;
    provenance.applicationVersion = QStringLiteral("0.1.0");
    provenance.enginePin = QStringLiteral("abcdef1234567890");
    return provenance;
}

// Reads a written file back the way another program would.
vtkSmartPointer<vtkUnstructuredGrid> readBack(const QString &path)
{
    vtkNew<vtkXMLUnstructuredGridReader> reader;
    reader->SetFileName(path.toLocal8Bit().constData());
    reader->Update();
    return reader->GetOutput();
}

QString fieldText(vtkUnstructuredGrid *grid, const char *name)
{
    auto *array = vtkStringArray::SafeDownCast(
        grid->GetFieldData()->GetAbstractArray(name));
    if (!array || array->GetNumberOfValues() < 1)
        return QString();
    return QString::fromStdString(array->GetValue(0));
}

// Everything the file says, as one string. Used where the assertion is that a
// fact is stated SOMEWHERE in the provenance rather than under a particular key.
QString allFieldText(vtkUnstructuredGrid *grid)
{
    QString all;
    vtkFieldData *data = grid->GetFieldData();
    for (int i = 0; i < data->GetNumberOfArrays(); i++) {
        auto *array = vtkStringArray::SafeDownCast(data->GetAbstractArray(i));
        if (!array)
            continue;
        for (vtkIdType v = 0; v < array->GetNumberOfValues(); v++)
            all += QString::fromStdString(array->GetValue(v)) + QLatin1Char('\n');
    }
    return all;
}

}  // namespace

class TestFieldExport : public QObject
{
    Q_OBJECT

private slots:
    void a_written_field_reads_back_as_the_points_and_cells_it_was_made_of();
    void the_points_land_where_they_were_measured();
    void a_point_the_solver_rejected_exports_as_nothing_not_as_zero();
    void a_strain_the_fit_declined_exports_as_nothing_not_as_zero();
    void a_run_without_strain_writes_no_strain_arrays_at_all();
    void the_file_says_which_images_and_which_settings_produced_it();
    void the_file_states_the_frame_its_coordinates_are_in();
    void the_file_carries_how_far_each_point_can_be_trusted();
    void a_result_with_nothing_in_it_is_refused_with_a_reason();
    void a_path_that_cannot_be_written_is_reported_rather_than_swallowed();
};

void TestFieldExport::a_written_field_reads_back_as_the_points_and_cells_it_was_made_of()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("field.vtu"));

    const CorrelationResult result = twoByTwo();
    QCOMPARE(writeFieldVtu(path, result, provenanceFor()), QString());
    QVERIFY(QFile::exists(path));

    auto grid = readBack(path);
    QVERIFY(grid);

    const FieldMesh mesh = buildFieldMesh(result);
    QCOMPARE(int(grid->GetNumberOfPoints()), mesh.pointSource.size());
    QCOMPARE(int(grid->GetNumberOfCells()), mesh.quads.size());
}

void TestFieldExport::the_points_land_where_they_were_measured()
{
    // Reference-image pixel coordinates, unaltered. A field whose geometry has
    // been shifted or scaled on the way out cannot be laid back over the
    // photograph it came from, which is most of what the file is for.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("field.vtu"));
    const CorrelationResult result = twoByTwo();
    QCOMPARE(writeFieldVtu(path, result, provenanceFor()), QString());

    auto grid = readBack(path);
    double first[3] = {0, 0, 0};
    grid->GetPoint(0, first);

    QCOMPARE(first[0], double(result.points.at(0).x));
    QCOMPARE(first[1], double(result.points.at(0).y));
    QCOMPARE(first[2], 0.0);
}

void TestFieldExport::a_point_the_solver_rejected_exports_as_nothing_not_as_zero()
{
    // NEGATIVE CHECK (2026-08-19): with the writer emitting the raw u and v of
    // every point, this failed on the displacement of the rejected point being
    // 99, and with them zeroed instead it failed on "0 is a measurement".
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("field.vtu"));

    CorrelationResult result = twoByTwo();
    result.points[2].converged = false;
    result.points[2].u = 99.f;   // the solver's leftover guess, not a measurement
    result.points[2].v = 99.f;
    result.converged = 3;

    QCOMPARE(writeFieldVtu(path, result, provenanceFor()), QString());
    auto grid = readBack(path);

    auto *displacement = grid->GetPointData()->GetArray("displacement");
    QVERIFY2(displacement, "the file carries no displacement array");
    QCOMPARE(displacement->GetNumberOfComponents(), 3);

    QVERIFY(std::isnan(displacement->GetComponent(2, 0)));
    QVERIFY(std::isnan(displacement->GetComponent(2, 1)));
    QVERIFY(displacement->GetComponent(2, 0) != 0.0);

    // The neighbours are untouched by it.
    QCOMPARE(float(displacement->GetComponent(1, 0)), result.points.at(1).u);

    // And the rejection itself is recorded, not merely implied by the holes.
    auto *solved = grid->GetPointData()->GetArray("solved");
    QVERIFY2(solved, "the file does not say which points were solved");
    QCOMPARE(solved->GetComponent(2, 0), 0.0);
    QCOMPARE(solved->GetComponent(1, 0), 1.0);
}

void TestFieldExport::a_strain_the_fit_declined_exports_as_nothing_not_as_zero()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("field.vtu"));

    CorrelationResult result = twoByTwo();
    result.points[1].strainFitted = false;
    result.strainFitted = 3;

    QCOMPARE(writeFieldVtu(path, result, provenanceFor()), QString());
    auto grid = readBack(path);

    auto *exx = grid->GetPointData()->GetArray("strain_exx");
    QVERIFY2(exx, "the file carries no strain array for a run that fitted strain");
    QVERIFY(std::isnan(exx->GetComponent(1, 0)));
    QCOMPARE(float(exx->GetComponent(0, 0)), result.points.at(0).exx);

    // Its displacement survives, because that part WAS measured.
    auto *displacement = grid->GetPointData()->GetArray("displacement");
    QCOMPARE(float(displacement->GetComponent(1, 0)), result.points.at(1).u);
}

void TestFieldExport::a_run_without_strain_writes_no_strain_arrays_at_all()
{
    // Absent, not present-and-empty. An all-not-a-number strain array reads as
    // a strain measurement that failed everywhere, which is a different and
    // much more alarming statement than "strain was never asked for".
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("field.vtu"));

    CorrelationResult result = twoByTwo();
    result.strainRequested = false;
    result.strainFitted = 0;
    for (CorrelationPoint &point : result.points)
        point.strainFitted = false;

    QCOMPARE(writeFieldVtu(path, result, provenanceFor()), QString());
    auto grid = readBack(path);

    QVERIFY(!grid->GetPointData()->GetArray("strain_exx"));
    QVERIFY(!grid->GetPointData()->GetArray("strain_eyy"));
    QVERIFY(!grid->GetPointData()->GetArray("strain_exy"));
    QVERIFY(grid->GetPointData()->GetArray("displacement"));
}

void TestFieldExport::the_file_says_which_images_and_which_settings_produced_it()
{
    // NEGATIVE CHECK (2026-08-19): watched red before the provenance block was
    // written, when the file carried geometry and arrays and nothing else.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("field.vtu"));
    QCOMPARE(writeFieldVtu(path, twoByTwo(), provenanceFor()), QString());

    auto grid = readBack(path);
    const QString stated = allFieldText(grid);

    QVERIFY2(stated.contains(QStringLiteral("ref.tif")), "no reference image named");
    QVERIFY2(stated.contains(QStringLiteral("tar.tif")), "no target image named");
    QVERIFY2(stated.contains(QStringLiteral("aaaabbbbccccdddd")),
             "the reference image is named but not identified");
    QVERIFY2(stated.contains(QStringLiteral("1111222233334444")),
             "the target image is named but not identified");
    QVERIFY2(stated.contains(QStringLiteral("ICGN")), "no solver stated");
    QVERIFY2(stated.contains(QStringLiteral("16")), "no subset radius stated");
    QVERIFY2(stated.contains(QStringLiteral("Cauchy")), "no strain measure stated");
    QVERIFY2(stated.contains(QStringLiteral("abcdef1234567890")),
             "the engine that measured this is not identified");
    QVERIFY2(stated.contains(QStringLiteral("SurView")), "no producer stated");
}

void TestFieldExport::the_file_states_the_frame_its_coordinates_are_in()
{
    // SurView's y runs DOWNWARD, which no mesh viewer assumes. Unstated, a
    // reader draws the field mirrored against the photograph and has no way to
    // know: the picture is plausible either way. This is the same trap that
    // produced a real bug in the viewport, one file format further out.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("field.vtu"));
    QCOMPARE(writeFieldVtu(path, twoByTwo(), provenanceFor()), QString());

    auto grid = readBack(path);
    const QString frame = fieldText(grid, "coordinate_frame");

    QVERIFY2(!frame.isEmpty(), "the file does not state its coordinate frame");
    QVERIFY(frame.contains(QStringLiteral("pixel"), Qt::CaseInsensitive));
    QVERIFY2(frame.contains(QStringLiteral("down"), Qt::CaseInsensitive),
             "the file does not say which way y runs");
}


void TestFieldExport::the_file_carries_how_far_each_point_can_be_trusted()
{
    // A field exported without its reliability is a field that will be trusted
    // absolutely by whoever opens it, since nothing in the file argues
    // otherwise. Tenet 9 does not stop at the application's own window.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("field.vtu"));

    CorrelationResult result = twoByTwo();
    result.points[2].conditioningMeasured = false;   // the probe found it unusable

    QCOMPARE(writeFieldVtu(path, result, provenanceFor()), QString());
    auto grid = readBack(path);

    auto *floor = grid->GetPointData()->GetArray("displacement_noise_floor");
    QVERIFY2(floor, "the file carries no noise floor");
    QCOMPARE(float(floor->GetComponent(0, 0)), result.points.at(0).noiseFloor);

    auto *conditioning = grid->GetPointData()->GetArray("match_conditioning");
    QVERIFY2(conditioning, "the file carries no conditioning");
    QCOMPARE(float(conditioning->GetComponent(1, 0)), result.points.at(1).conditioning);

    // Unestablished conditioning is not-a-number, for the same reason every
    // other unmeasured value is: zero would read as a perfectly sharp cost,
    // which is the best possible score rather than the warning it really is.
    QVERIFY(std::isnan(conditioning->GetComponent(2, 0)));
    QVERIFY(conditioning->GetComponent(2, 0) != 0.0);

    // And the file names what these are, in the terms the literature uses, so
    // a stranger can look them up rather than guess from an array name.
    const QString stated = allFieldText(grid);
    QVERIFY2(stated.contains(QStringLiteral("sigma")),
             "the file does not connect the noise floor to DIC's sigma");
    QVERIFY2(stated.contains(QStringLiteral("beta")),
             "the file does not connect the conditioning to DIC's beta");
    QVERIFY2(stated.contains(QStringLiteral("1.75")),
             "the file omits the image noise that scaled every noise floor");
}

void TestFieldExport::a_result_with_nothing_in_it_is_refused_with_a_reason()
{
    // Writing an empty file would be the worst of the options: it succeeds,
    // and the emptiness is discovered somewhere else entirely.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("empty.vtu"));

    const QString refusal = writeFieldVtu(path, CorrelationResult(), provenanceFor());

    QVERIFY2(!refusal.isEmpty(), "an empty result was exported without complaint");
    QVERIFY2(!QFile::exists(path), "an empty result left a file behind");
}

void TestFieldExport::a_path_that_cannot_be_written_is_reported_rather_than_swallowed()
{
    // VTK's writers report failure through a return code that is easy not to
    // look at, and a silent failure here means a user believes they have a
    // file they do not have.
    const QString refusal = writeFieldVtu(
        QStringLiteral("/definitely/not/a/directory/field.vtu"), twoByTwo(),
        provenanceFor());

    QVERIFY2(!refusal.isEmpty(), "an unwritable path reported success");
    QVERIFY2(refusal.contains(QStringLiteral("field.vtu")),
             "the refusal does not name the file it could not write");
}

QTEST_MAIN(TestFieldExport)
#include "test_field_export.moc"
