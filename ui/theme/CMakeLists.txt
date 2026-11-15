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
        color_scope.h
        color_scope.cpp
        cover_palette.h
        cover_palette.cpp
        player_palette.h
        player_palette.cpp
        theme.h
        theme.cpp
        system_appearance.h
        system_appearance.cpp
    RESOURCES
        fonts/MaterialSymbolsRounded.ttf
)

# The default text font, MiSans (fonts/MiSans-LICENSE.txt). 20 MB, so it is compiled as
# a big resource rather than a C array. Builds without the file fall back to the system
# font (for a smaller package without it, later).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/fonts/MiSansVF.ttf")
    # Not compressed: the font is used straight from the executable's pages (fonts.cpp).
    qt_add_resources(cirrus_ui_theme cirrus_ui_theme_misans
        BIG_RESOURCES
        PREFIX "/qt/qml/Cirrus/Theme"
        FILES fonts/MiSansVF.ttf
        OPTIONS -no-compress
    )
else()
    message(WARNING "ui/theme/fonts/MiSansVF.ttf is missing (tools/ui/prepare_fonts.py fetches "
                    "it). Building without the bundled text font: text uses the system font.")
endif()

target_compile_features(cirrus_ui_theme PUBLIC cxx_std_20)
find_package(Qt6 REQUIRED COMPONENTS QuickControls2)
# QuickControls2 for ColorScope's QQuickAttachedPropertyPropagator (public since Qt 6.5).
target_link_libraries(cirrus_ui_theme PRIVATE cirrus_logging Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 cirrus_mcu)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # xdg-desktop-portal accent color and light/dark preference (system_appearance.cpp)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    target_link_libraries(cirrus_ui_theme PRIVATE Qt6::DBus)
endif()
