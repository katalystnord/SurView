cmake_minimum_required(VERSION 3.21)
project(SurView VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Some distro VTK builds (e.g. Ubuntu's VTK 9.5) are compiled with MPI and
# re-export MPI::MPI_C through their imported targets, but leave it to the
# consuming project to locate MPI. Find it up front so VTK's config succeeds.
find_package(MPI QUIET)

find_package(VTK REQUIRED COMPONENTS CommonCore CommonDataModel RenderingCore GUISupportQt)

add_subdirectory(src)
