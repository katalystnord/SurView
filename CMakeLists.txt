cmake_minimum_required(VERSION 3.21)
project(SurView VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)

# Some distro VTK builds (e.g. Ubuntu's VTK 9.5) are compiled with MPI and
# re-export MPI::MPI_C through their imported targets, but leave it to the
# consuming project to locate MPI. Find it up front so VTK's config succeeds.
find_package(MPI QUIET)

find_package(VTK REQUIRED COMPONENTS
    CommonCore
    CommonDataModel
    RenderingCore
    RenderingOpenGL2      # OpenGL backend + object-factory overrides (rendering)
    InteractionStyle      # vtkInteractorStyleImage (2D pan/zoom in the viewport)
    IOImage               # vtkImageReader2Factory (TIFF/PNG/JPEG speckle images)
    GUISupportQt          # QVTKOpenGLNativeWidget (VTK inside a Qt widget)
)

# Which OpenCorr engine commit this build is verified against. Advisory: it
# reports drift and continues. Full three-way check: tools/check-engine.sh
include(cmake/OpenCorrPin.cmake)

add_subdirectory(src)
