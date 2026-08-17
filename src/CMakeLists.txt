add_executable(SurView
    main.cpp
    core/ImageDecode.cpp
    core/ImageDecode.h
    core/ImagePairing.cpp
    core/ImagePairing.h
    core/ImageRecord.cpp
    core/ImageRecord.h
    gui/MainWindow.cpp
    gui/MainWindow.h
    gui/ImageViewport.cpp
    gui/ImageViewport.h
    gui/RecordPanel.cpp
    gui/RecordPanel.h
)

# Headers are included by their path from src/ (e.g. "core/ImageRecord.h").
target_include_directories(SurView PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(SurView PRIVATE
    SURVIEW_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(SurView PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    ${VTK_LIBRARIES}
)

vtk_module_autoinit(
    TARGETS SurView
    MODULES ${VTK_LIBRARIES}
)
