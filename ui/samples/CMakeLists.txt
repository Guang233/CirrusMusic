# Fictional sample catalog for the gallery and prototype fakes; not linked by the product app.
qt_add_library(cirrus_ui_samples STATIC)

set_source_files_properties(Samples.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(cirrus_ui_samples
    URI Cirrus.Samples
    VERSION 1.0
    QML_FILES
        Samples.qml
    RESOURCES
        catalog.json
        lyrics/so-001.json
        lyrics/so-005.json
        covers/sunset.jpg
        covers/ocean.jpg
        covers/forest.jpg
        covers/neon.jpg
        covers/mono.jpg
        covers/pastel.jpg
        covers/crimson.jpg
        covers/hazard.jpg
        covers/earth.jpg
        covers/minimal.jpg
        covers/galaxy.jpg
        covers/stripes.jpg
)
