qt_add_executable(appCirrusMusic
    main.cpp
)

qt_add_qml_module(appCirrusMusic
    URI CirrusMusic
    QML_FILES
        Main.qml
        TitleBar.qml
        WindowButtons.qml
        ResizeHandles.qml
    SOURCES
        window_frame.h
        window_frame.cpp
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appCirrusMusic PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appCirrusMusic
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    # Keep the historical location build/<preset>/appCirrusMusic used by docs and launch configs.
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

target_link_libraries(appCirrusMusic PRIVATE
    Qt6::Quick
    cirrus_ui_themeplugin
    cirrus_ui_controlsplugin
    m3shapesplugin
)

# Custom title bar on Wayland (window_frame.cpp): shadow margins outside the window
# geometry, tiling states and the window menu are only reachable through Qt's
# private Wayland client API, so it is optional and pinned to the Qt version built against.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(QT_NO_PRIVATE_MODULE_WARNING ON) # deliberate, see above
    find_package(Qt6 QUIET COMPONENTS GuiPrivate WaylandClientPrivate)
    if(TARGET Qt6::WaylandClientPrivate)
        target_link_libraries(appCirrusMusic PRIVATE Qt6::GuiPrivate Qt6::WaylandClientPrivate)
        target_compile_definitions(appCirrusMusic PRIVATE CIRRUS_HAVE_WAYLAND)
    else()
        message(WARNING "Qt6::WaylandClientPrivate not found: the custom title bar has no "
                        "shadow or tiling support on Wayland")
    endif()
endif()

install(TARGETS appCirrusMusic
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
