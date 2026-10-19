qt_add_library(cirrus_ui_theme STATIC)

set_source_files_properties(Symbols.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(cirrus_ui_theme
    URI Cirrus.Theme
    VERSION 1.0
    DEPENDENCIES
        QtQuick
    QML_FILES
        Symbols.qml
    SOURCES
        fonts.h
        fonts.cpp
        tokens.h
        tokens.cpp
        spring_math.h
        spring.h
        spring.cpp
        color_scheme.h
        color_scheme.cpp
        cover_palette.h
        cover_palette.cpp
        theme.h
        theme.cpp
        system_appearance.h
        system_appearance.cpp
    RESOURCES
        fonts/MaterialSymbolsRounded.ttf
)

target_compile_features(cirrus_ui_theme PUBLIC cxx_std_20)
target_link_libraries(cirrus_ui_theme PRIVATE Qt6::Gui Qt6::Qml cirrus_mcu)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # xdg-desktop-portal accent color and light/dark preference (system_appearance.cpp)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    target_link_libraries(cirrus_ui_theme PRIVATE Qt6::DBus)
endif()
