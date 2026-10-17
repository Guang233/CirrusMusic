qt_add_library(cirrus_ui_controls STATIC)

qt_add_qml_module(cirrus_ui_controls
    URI Cirrus.Controls
    VERSION 1.0
    DEPENDENCIES
        QtQuick
        Cirrus.Theme
    QML_FILES
        Icon.qml
)

target_link_libraries(cirrus_ui_controls PRIVATE Qt6::Quick cirrus_ui_theme)
