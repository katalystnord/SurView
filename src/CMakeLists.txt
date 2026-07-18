add_executable(SurView
    main.cpp
    gui/MainWindow.cpp
    gui/MainWindow.h
)

target_link_libraries(SurView PRIVATE
    Qt6::Widgets
    ${VTK_LIBRARIES}
)

vtk_module_autoinit(
    TARGETS SurView
    MODULES ${VTK_LIBRARIES}
)
