qt_add_executable(appCirrusGallery
    main.cpp
)

set_source_files_properties(GalleryState.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(appCirrusGallery
    URI CirrusGallery
    QML_FILES
        Gallery.qml
        GalleryState.qml
        pages/ResourcesPage.qml
        pages/ThemePage.qml
        pages/ControlPage.qml
        pages/SectionTitle.qml
        pages/Caption.qml
        pages/ButtonsPage.qml
        pages/InputsPage.qml
        pages/ProgressPage.qml
        pages/ListsPage.qml
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
    m3shapesplugin
)
