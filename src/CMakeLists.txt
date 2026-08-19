# SurView is built as two libraries plus a thin executable, rather than as one
# monolithic target, so the tests link the SAME code the application runs. A
# test that links a re-compiled copy, or a re-implementation, is testing
# something the user never gets.

# --- core: the measurement half, no widgets -------------------------------
add_library(surview_core STATIC
    core/Correlation.cpp
    core/Correlation.h
    core/FieldExport.cpp
    core/FieldExport.h
    core/FieldLayout.cpp
    core/FieldLayout.h
    core/FieldMesh.cpp
    core/FieldMesh.h
    core/ImageDecode.cpp
    core/ImageDecode.h
    core/ImagePairing.cpp
    core/ImagePairing.h
    core/ImageRecord.cpp
    core/ImageRecord.h
    core/PoiGrid.cpp
    core/PoiGrid.h
    core/SequenceRunner.cpp
    core/SequenceRunner.h
    core/Sequence.cpp
    core/Sequence.h
    core/Roi.cpp
    core/Roi.h
    core/RoiDetect.cpp
    core/RoiDetect.h
    core/StrainFit.cpp
    core/StrainFit.h
)

# Headers are included by their path from src/ (e.g. "core/ImageRecord.h").
target_include_directories(surview_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(surview_core PUBLIC
    opencorr
    Qt6::Core
    ${VTK_LIBRARIES}
)

# --- gui: the widgets ------------------------------------------------------
add_library(surview_gui STATIC
    gui/MainWindow.cpp
    gui/MainWindow.h
    gui/ImageViewport.cpp
    gui/ImageViewport.h
    gui/RecordPanel.cpp
    gui/RecordPanel.h
)

target_link_libraries(surview_gui PUBLIC
    surview_core
    Qt6::Widgets
    Qt6::OpenGLWidgets
)

target_compile_definitions(surview_gui PUBLIC
    SURVIEW_VERSION="${PROJECT_VERSION}"
    # The engine commit this build is pinned to, compiled in so an exported
    # field can name what measured it. Read from cmake/opencorr.pin, the same
    # single source the configure-time check and tools/check-engine.sh use.
    SURVIEW_OPENCORR_PIN="${SURVIEW_OPENCORR_PIN}"
)

# --- the application ------------------------------------------------------
add_executable(SurView main.cpp)

target_link_libraries(SurView PRIVATE surview_gui)

vtk_module_autoinit(
    TARGETS SurView surview_core surview_gui
    MODULES ${VTK_LIBRARIES}
)
