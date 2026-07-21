add_executable(SurView
    main.cpp
    gui/MainWindow.cpp
    gui/MainWindow.h
    gui/ImageViewport.cpp
    gui/ImageViewport.h
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
