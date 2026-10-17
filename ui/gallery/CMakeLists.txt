qt_add_executable(appCirrusGallery
    main.cpp
)

qt_add_qml_module(appCirrusGallery
    URI CirrusGallery
    QML_FILES
        Gallery.qml
        pages/ResourcesPage.qml
)

set_target_properties(appCirrusGallery PROPERTIES
    WIN32_EXECUTABLE TRUE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

target_link_libraries(appCirrusGallery PRIVATE
    Qt6::Quick
    cirrus_ui_themeplugin
    cirrus_ui_controlsplugin
    cirrus_ui_samplesplugin
)
