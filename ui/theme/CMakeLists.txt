qt_add_library(cirrus_ui_theme STATIC)

set_source_files_properties(Symbols.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(cirrus_ui_theme
    URI Cirrus.Theme
    VERSION 1.0
    QML_FILES
        Symbols.qml
    SOURCES
        fonts.h
        fonts.cpp
    RESOURCES
        fonts/MaterialSymbolsRounded.ttf
)

target_compile_features(cirrus_ui_theme PUBLIC cxx_std_20)
target_link_libraries(cirrus_ui_theme PRIVATE Qt6::Gui Qt6::Qml)
