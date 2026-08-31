# One executable per subject, each registered with CTest. Named for what it
# covers, so a failure names the thing that broke before anyone opens a file.
#
# Test names inside these binaries are named for the CASE, not the function --
# `a region touching the image edge keeps its subsets inside`, not
# `buildPoiGrid returns cells`. A design doc reads as satisfied; a named red
# test does not.

# Unit tests: no widgets, no display, no engine images. Fast enough that there
# is never a reason to skip them.
set(SURVIEW_UNIT_TESTS
    test_roi
    test_poi_grid
    test_image_record
    test_image_pairing
    test_image_decode
    test_field_layout
    test_field_mesh
    test_field_export
    test_field_csv
    test_roi_engine_boundary
    test_roi_detect
    test_solver_choices
    test_sequence
    test_reference_update
    test_sequence_runner
    test_strain_fit
    test_strain_measure
    test_uncertainty_measure
    test_point_readout
)

foreach(test_name ${SURVIEW_UNIT_TESTS})
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE surview_core Qt6::Test)
    add_test(NAME ${test_name} COMMAND ${test_name})
    # Fixtures are found by an absolute path compiled in, so a test binary runs
    # correctly from any working directory -- including ctest's.
    target_compile_definitions(${test_name} PRIVATE
        SURVIEW_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    # Unit tests must not need a display; offscreen makes that a fact rather
    # than a hope, and makes them run identically on a headless machine.
    set_tests_properties(${test_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

vtk_module_autoinit(
    TARGETS ${SURVIEW_UNIT_TESTS}
    MODULES ${VTK_LIBRARIES}
)

# Walkthrough tests: the real MainWindow, driven the way a user drives it.
# These are the analogue of an end-to-end suite -- they answer "can this be done
# from what is on screen", which no unit test can.
add_executable(test_workspace_walkthrough test_workspace_walkthrough.cpp)
target_link_libraries(test_workspace_walkthrough PRIVATE surview_gui Qt6::Test)
target_compile_definitions(test_workspace_walkthrough PRIVATE
    SURVIEW_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# ⚑ HEADLESS BY CONSTRUCTION, not by how the suite is invoked.
#
# This test calls show() on a real main window. Registered plainly, it inherits
# whatever DISPLAY the developer happens to have, and running `ctest` throws
# windows onto their desktop mid-session -- which is exactly what happened the
# first time this suite was run. Wrapping the command means the test carries its
# own private display with it, so `ctest`, a hook, and CI all behave the same.
#
# It cannot simply run offscreen: QT_QPA_PLATFORM=offscreen crashes the VTK
# OpenGL widget outright, and even if it did not, mapping a click to an image
# pixel goes through the renderer's real projection -- the thing these tests
# exist to check. A software-rendered X server is the lightest environment that
# still exercises it honestly.
option(SURVIEW_TESTS_HEADLESS
       "Run the walkthrough tests on a private X server instead of your desktop" ON)

find_program(SURVIEW_XVFB_RUN xvfb-run)

if(SURVIEW_TESTS_HEADLESS AND SURVIEW_XVFB_RUN)
    # -a picks a free display number, so parallel runs cannot collide. The
    # screen is given explicitly because xvfb-run's default is 8-bit, which is
    # not enough for the GL context.
    add_test(NAME test_workspace_walkthrough
             COMMAND ${SURVIEW_XVFB_RUN} -a -s "-screen 0 1600x1000x24"
                     $<TARGET_FILE:test_workspace_walkthrough>)
else()
    if(SURVIEW_TESTS_HEADLESS)
        message(WARNING
            "xvfb-run not found: the walkthrough tests will open windows on the "
            "current display. Install xvfb to keep `ctest` headless.")
    endif()
    add_test(NAME test_workspace_walkthrough COMMAND test_workspace_walkthrough)
endif()

# Software GL, so the result does not depend on the machine's graphics driver.
# WAYLAND_DISPLAY is cleared so Qt uses the X server provided above rather than
# reaching past it to a compositor.
set_tests_properties(test_workspace_walkthrough PROPERTIES
    ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1;QT_QPA_PLATFORM=xcb;WAYLAND_DISPLAY=")

vtk_module_autoinit(
    TARGETS test_workspace_walkthrough
    MODULES ${VTK_LIBRARIES}
)
