# Presentation layer (docs/ui/presentation.md): ViewModels, list models, data source
# interfaces, fakes and the cover image provider. Never links the backend; the
# backend sources will live in a separate cirrus_presentation_backend target.
find_package(Qt6 REQUIRED COMPONENTS Network)

qt_add_library(cirrus_presentation STATIC)

qt_add_qml_module(cirrus_presentation
    URI Cirrus.Presentation
    VERSION 1.0
    DEPENDENCIES
        QtQuick
    SOURCES
        load_state.h
        request.h
        types.h
        playback.h
        song_item.h
        sources.h
        app.h
        app.cpp
        app_settings.h
        app_settings.cpp
        formatter.h
        formatter.cpp
        network_status.h
        network_status.cpp
        cover_provider.h
        cover_provider.cpp
        loadable_model.h
        loadable_model.cpp
        song_list_model.h
        song_list_model.cpp
        playlist_page_model.h
        playlist_page_model.cpp
        player.h
        player.cpp
        fake_controls.h
        fake_controls.cpp
        fake_sources.h
        fake_sources.cpp
)

target_compile_features(cirrus_presentation PUBLIC cxx_std_20)
# main.cpp installs the sources (app.h, fake_sources.h).
target_include_directories(cirrus_presentation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cirrus_presentation PUBLIC Qt6::Quick PRIVATE Qt6::Network)
