cmake_minimum_required(VERSION 3.21)
project(SurView VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Test)

# Some distro VTK builds (e.g. Ubuntu's VTK 9.5) are compiled against system
# libraries and re-export them through their imported targets, but leave it to
# the consuming project to locate them. Find them up front so VTK's config
# succeeds.
find_package(MPI QUIET)

# VTK::jsoncpp's link interface names JsonCpp::JsonCpp, which nothing defines
# unless the consumer asks for it. The gap only shows once another subdirectory
# is added to the build (the engine, below): CMake validates imported link
# interfaces at generate time, so the error surfaces after "Configuring done"
# and blames the find_package(VTK) line where the target was defined.
find_package(jsoncpp QUIET)

find_package(VTK REQUIRED COMPONENTS
    CommonCore
    CommonDataModel
    RenderingCore
    RenderingOpenGL2      # OpenGL backend + object-factory overrides (rendering)
    InteractionStyle      # vtkInteractorStyleImage (2D pan/zoom in the viewport)
    ImagingCore           # vtkImageMapToColors (field scalars -> displayed colour)
    RenderingAnnotation   # vtkScalarBarActor (the field's colour scale)
    IOImage               # vtkImageReader2Factory (TIFF/PNG/JPEG speckle images)
    GUISupportQt          # QVTKOpenGLNativeWidget (VTK inside a Qt widget)
)

# Which OpenCorr engine commit this build is verified against. Advisory: it
# reports drift and continues. Full three-way check: tools/check-engine.sh
include(cmake/OpenCorrPin.cmake)

# The engine is built from the pinned checkout rather than a system package:
# there is no released OpenCorr to depend on, and building it here is what makes
# the recorded SHA mean something about this binary.
if(NOT IS_DIRECTORY "${SURVIEW_OPENCORR_DIR}/src")
    message(FATAL_ERROR
        "OpenCorr sources not found at ${SURVIEW_OPENCORR_DIR}.\n"
        "SurView builds the engine from a checkout of katalystnord/OpenCorr; "
        "clone it beside this repo or pass -DSURVIEW_OPENCORR_DIR=<path>.")
endif()
add_subdirectory("${SURVIEW_OPENCORR_DIR}" opencorr EXCLUDE_FROM_ALL)

add_subdirectory(src)

# Tests are part of the build by default, not an opt-in extra. A suite that has
# to be asked for is a suite that stops being run. `tools/run-tests.sh` is the
# single entry point; it drives this and the engine's own suite together.
option(SURVIEW_BUILD_TESTS "Build SurView's test suite" ON)
if(SURVIEW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
