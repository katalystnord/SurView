#include "core/FieldExport.h"

#include "core/FieldMesh.h"
#include "core/StrainFit.h"

#include <QFileInfo>
#include <QObject>

#include <vtkCellArray.h>
#include <vtkErrorCode.h>
#include <vtkFieldData.h>
#include <vtkFloatArray.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkQuad.h>
#include <vtkStringArray.h>
#include <vtkUnsignedCharArray.h>
#include <vtkUnstructuredGrid.h>
#include <vtkXMLUnstructuredGridWriter.h>

#include <cmath>
#include <limits>

namespace {

// One line of stated fact, as its own named field-data array. Named arrays
// rather than one blob of text, so a reader can ask for the piece it wants.
void state(vtkUnstructuredGrid *grid, const char *name, const QString &value)
{
    vtkNew<vtkStringArray> array;
    array->SetName(name);
    array->InsertNextValue(value.toStdString());
    grid->GetFieldData()->AddArray(array);
}

vtkSmartPointer<vtkFloatArray> namedArray(const char *name, int components,
                                          vtkIdType tuples)
{
    vtkNew<vtkFloatArray> array;
    array->SetName(name);
    array->SetNumberOfComponents(components);
    array->SetNumberOfTuples(tuples);
    return array;
}

}  // namespace

QString writeFieldVtu(const QString &path, const CorrelationResult &result,
                      const FieldProvenance &provenance)
{
    const QString name = QFileInfo(path).fileName();

    const FieldMesh mesh = buildFieldMesh(result);
    if (mesh.pointSource.isEmpty()) {
        return QObject::tr("There is no measured field to write to %1. Run a "
                           "correlation first.")
            .arg(name);
    }

    const auto count = vtkIdType(mesh.pointSource.size());
    const float nothing = std::numeric_limits<float>::quiet_NaN();

    vtkNew<vtkUnstructuredGrid> grid;

    // --- geometry -----------------------------------------------------------
    vtkNew<vtkPoints> points;
    points->SetNumberOfPoints(count);
    for (vtkIdType i = 0; i < count; i++) {
        const CorrelationPoint &point = result.points.at(mesh.pointSource.at(int(i)));
        // Reference-image pixel coordinates, unaltered, so the field can be
        // laid straight back over the photograph it was measured from. The
        // frame is stated below rather than converted to suit a viewer.
        points->SetPoint(i, point.x, point.y, 0.0);
    }
    grid->SetPoints(points);

    vtkNew<vtkCellArray> cells;
    for (const FieldMeshQuad &quad : mesh.quads) {
        vtkNew<vtkQuad> cell;
        cell->GetPointIds()->SetId(0, quad.a);
        cell->GetPointIds()->SetId(1, quad.b);
        cell->GetPointIds()->SetId(2, quad.c);
        cell->GetPointIds()->SetId(3, quad.d);
        cells->InsertNextCell(cell);
    }
    grid->SetCells(VTK_QUAD, cells);

    // --- what was measured --------------------------------------------------
    auto displacement = namedArray("displacement", 3, count);
    displacement->SetComponentName(0, "u");
    displacement->SetComponentName(1, "v");
    displacement->SetComponentName(2, "w");
    auto magnitude = namedArray("displacement_magnitude", 1, count);
    auto zncc = namedArray("zncc", 1, count);

    vtkNew<vtkUnsignedCharArray> solved;
    solved->SetName("solved");
    solved->SetNumberOfComponents(1);
    solved->SetNumberOfTuples(count);

    auto noiseFloor = namedArray("displacement_noise_floor", 1, count);
    auto conditioning = namedArray("match_conditioning", 1, count);

    const bool withStrain = result.strainRequested;
    auto exx = namedArray("strain_exx", 1, count);
    auto eyy = namedArray("strain_eyy", 1, count);
    auto exy = namedArray("strain_exy", 1, count);

    for (vtkIdType i = 0; i < count; i++) {
        const CorrelationPoint &point = result.points.at(mesh.pointSource.at(int(i)));

        // Not-a-number where nothing was measured, never zero. A field of
        // zeros is a perfectly good blue region in any viewer, and there is no
        // way for a reader to tell it from a real measurement of no movement.
        const bool has = point.converged;
        displacement->SetTuple3(i, has ? point.u : nothing,
                                has ? point.v : nothing, 0.f);
        magnitude->SetTuple1(i, has ? float(std::hypot(point.u, point.v)) : nothing);
        // The engine reports a failure as a negative code in this same field,
        // so a rejected point's zncc is a status rather than a correlation.
        zncc->SetTuple1(i, has ? point.zncc : nothing);
        solved->SetTuple1(i, has ? 1 : 0);

        // Zero is the flattering reading for both of these -- a perfect
        // measurement, a perfectly sharp cost -- so an unestablished one is
        // not-a-number like every other value nobody measured.
        noiseFloor->SetTuple1(i, point.noiseFloorMeasured ? point.noiseFloor : nothing);
        conditioning->SetTuple1(i,
                                point.conditioningMeasured ? point.conditioning : nothing);

        if (withStrain) {
            const bool fitted = point.strainFitted;
            exx->SetTuple1(i, fitted ? point.exx : nothing);
            eyy->SetTuple1(i, fitted ? point.eyy : nothing);
            exy->SetTuple1(i, fitted ? point.exy : nothing);
        }
    }

    grid->GetPointData()->AddArray(displacement);
    grid->GetPointData()->AddArray(magnitude);
    grid->GetPointData()->AddArray(zncc);
    grid->GetPointData()->AddArray(solved);
    grid->GetPointData()->AddArray(noiseFloor);
    grid->GetPointData()->AddArray(conditioning);
    grid->GetPointData()->SetVectors(displacement);
    grid->GetPointData()->SetScalars(magnitude);

    // Absent rather than present and empty when strain was never asked for. An
    // all-not-a-number strain array reads as a measurement that failed
    // everywhere, which is a far more alarming statement than "not attempted".
    if (withStrain) {
        grid->GetPointData()->AddArray(exx);
        grid->GetPointData()->AddArray(eyy);
        grid->GetPointData()->AddArray(exy);
    }

    // --- how it was made ----------------------------------------------------
    state(grid, "producer",
          QObject::tr("SurView DIC %1").arg(provenance.applicationVersion));
    state(grid, "engine",
          QObject::tr("OpenCorr, katalystnord fork, pinned at %1")
              .arg(provenance.enginePin));

    // ⚑ The frame, stated because no mesh viewer assumes it. SurView's y runs
    // DOWNWARD, matching the image's own row order; a reader that assumes y up
    // draws the field mirrored against the photograph and cannot tell, because
    // the picture is plausible either way.
    state(grid, "coordinate_frame",
          QObject::tr("Reference image pixels: x right, y down, origin at the "
                      "top-left pixel centre. Displacement u and v are in the "
                      "same frame, so a positive v is movement DOWN the image."));

    state(grid, "reference_image", provenance.reference.filePath);
    state(grid, "reference_image_sha256", provenance.reference.sha256);
    state(grid, "target_image", provenance.target.filePath);
    state(grid, "target_image_sha256", provenance.target.sha256);

    const CorrelationSettings &settings = provenance.settings;
    state(grid, "correlation",
          QObject::tr("%1, shape order %2, subset radius %3 px, grid step %4 px, "
                      "up to %5 iterations, convergence %6")
              .arg(solverDisplayName(settings.solver))
              .arg(settings.shapeOrder)
              .arg(settings.subsetRadius)
              .arg(settings.gridStep)
              .arg(settings.maxIterations)
              .arg(settings.convergence));

    state(grid, "region_of_interest",
          result.restrictedToRoi
              ? QObject::tr("A region of %1 corners; points outside it were "
                            "never measured.")
                    .arg(result.roi.vertices.size())
              : QObject::tr("None: the whole image was measured."));

    state(grid, "strain",
          withStrain
              ? QObject::tr("%1, fitted over a %2 px subregion from at least %3 "
                            "points, excluding any point correlating below %4. "
                            "Fitted at %5 of %6 points.")
                    .arg(strainMeasureName(result.strainMeasure))
                    .arg(result.strainRadius, 0, 'g', 4)
                    .arg(settings.strainMinPoints)
                    .arg(double(kStrainFitCorrelationFloor), 0, 'g', 2)
                    .arg(result.strainFitted)
                    .arg(result.total())
              : QObject::tr("Not fitted."));

    // Named in the terms the literature uses, so a stranger can look these up
    // rather than infer them from an array name, and qualified in the same
    // breath: an exported reliability figure read as a total error bar is worse
    // than none at all.
    state(grid, "reliability",
          QObject::tr(
              "displacement_noise_floor is DIC's sigma, in pixels: the finest "
              "displacement each subset's speckle can resolve against an "
              "estimated image noise of %1 grey levels. It is a lower bound on "
              "error, not a total error bar, and is computed from the reference "
              "image alone. match_conditioning is DIC's beta, dimensionless: "
              "how sharply the correlation cost rises around the solution "
              "found, relative within this run only. Larger is worse in both. "
              "Established at %2 of %3 solved points; %4 solved point(s) had a "
              "cost too flat to probe.")
              .arg(result.referenceNoise, 0, 'g', 4)
              .arg(result.noiseFloorMeasured)
              .arg(result.converged)
              .arg(result.conditioningUnusable));

    state(grid, "result",
          QObject::tr("%1 of %2 points solved in %3 s%4. Values that were not "
                      "measured are written as not-a-number, never as zero.")
              .arg(result.converged)
              .arg(result.total())
              .arg(result.secondsElapsed, 0, 'f', 2)
              .arg(result.cancelled ? QObject::tr(", run stopped early")
                                    : QString()));

    // --- write --------------------------------------------------------------
    vtkNew<vtkXMLUnstructuredGridWriter> writer;
    writer->SetFileName(path.toLocal8Bit().constData());
    writer->SetInputData(grid);
    writer->SetDataModeToBinary();

    // ⚑ Write()'s RETURN VALUE IS NOT THE ANSWER. Checked against VTK 9.5
    // directly rather than assumed: given a path in a directory that does not
    // exist, vtkXMLUnstructuredGridWriter::Write() returns 1 -- success -- while
    // GetErrorCode() reports 2 (CannotOpenFileError) and no file appears. A
    // writer trusted on its return value therefore tells a user their
    // measurement has been saved when it has not. The error code is the signal,
    // and the file's existence is checked behind it, because "no file" is the
    // outcome the user cares about however VTK chose to describe it.
    writer->Write();

    const unsigned long error = writer->GetErrorCode();
    if (error != vtkErrorCode::NoError) {
        return QObject::tr("Could not write %1: %2.")
            .arg(name)
            .arg(QString::fromLatin1(vtkErrorCode::GetStringFromErrorCode(error)));
    }
    if (!QFileInfo::exists(path)) {
        return QObject::tr("Could not write %1. The writer reported no error and "
                           "produced no file.")
            .arg(name);
    }

    return QString();
}
