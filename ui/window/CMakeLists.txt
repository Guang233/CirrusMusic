qt_add_library(cirrus_ui_window STATIC)

qt_add_qml_module(cirrus_ui_window
    URI Cirrus.Window
    VERSION 1.0
    DEPENDENCIES
        QtQuick
        Cirrus.Theme
        Cirrus.Controls
    SOURCES
        title_bar_mode.h
        title_bar_mode.cpp
        window_frame.h
        window_frame.cpp
    QML_FILES
        AppWindow.qml
        TitleBar.qml
        WindowButtons.qml
        ResizeHandles.qml
)

target_compile_features(cirrus_ui_window PUBLIC cxx_std_20)
# main.cpp of the app and the gallery resolve the title bar mode (title_bar_mode.h).
target_include_directories(cirrus_ui_window PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cirrus_ui_window PUBLIC Qt6::Quick PRIVATE cirrus_ui_theme cirrus_ui_controls)

# Custom title bar on Wayland (window_frame.cpp): shadow margins outside the window
# geometry, tiling states and the window menu are only reachable through Qt's
# private Wayland client API, so it is optional and pinned to the Qt version built against.
# The packages are found in ui/CMakeLists.txt, so the executables see their targets.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(TARGET Qt6::WaylandClientPrivate)
        target_link_libraries(cirrus_ui_window PRIVATE Qt6::GuiPrivate Qt6::WaylandClientPrivate)
        target_compile_definitions(cirrus_ui_window PRIVATE CIRRUS_HAVE_WAYLAND)
    else()
        message(WARNING "Qt6::WaylandClientPrivate not found: the custom title bar has no "
                        "shadow or tiling support on Wayland")
    endif()
endif()
