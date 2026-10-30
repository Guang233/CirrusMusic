# Business-facing building blocks of the shell and pages (docs/ui/README.md §3.1).
qt_add_library(cirrus_ui_components STATIC)

qt_add_qml_module(cirrus_ui_components
    URI Cirrus.Components
    VERSION 1.0
    DEPENDENCIES
        QtQuick
        Cirrus.Theme
        Cirrus.Controls
    QML_FILES
        PageStack.qml
        PageScaffold.qml
)

target_link_libraries(cirrus_ui_components PRIVATE Qt6::Quick cirrus_ui_theme cirrus_ui_controls)
