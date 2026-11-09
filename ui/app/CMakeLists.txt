find_package(Qt6 REQUIRED COMPONENTS Network)

qt_add_executable(appCirrusMusic
    main.cpp
    app_icon.h
    gpu_preference.h
    gpu_preference.cpp
    single_instance.h
    single_instance.cpp
)

qt_add_qml_module(appCirrusMusic
    URI CirrusMusic
    QML_FILES
        Main.qml
        AppShell.qml
        SongCollectionPage.qml
        PlaylistPage.qml
        PlaylistOrderPage.qml
        AlbumPage.qml
        ArtistPage.qml
        PodcastPage.qml
        CommentsPage.qml
        TabState.qml
        SearchPage.qml
        DiscoverPage.qml
        DailySongsPage.qml
        LibraryPage.qml
        NewPlaylistDialog.qml
        GlyphTile.qml
        RecentPage.qml
        RankingPage.qml
        LoginDialog.qml
        AddToPlaylistDialog.qml
        ChartCard.qml
        SettingsPage.qml
        settings/SettingsGroup.qml
        settings/SettingRow.qml
        settings/SwitchSetting.qml
        settings/ChoiceSetting.qml
        settings/MenuSetting.qml
        settings/ActionSetting.qml
        settings/SliderSetting.qml
        settings/SeedSwatches.qml
        settings/CustomSeedDialog.qml
        settings/FontPickerDialog.qml
        settings/AccountSettings.qml
        settings/PlaybackSettings.qml
        settings/LyricsSettings.qml
        settings/PlayerSettings.qml
        settings/PlayerPanelSettings.qml
        settings/AppearanceSettings.qml
        settings/WindowSettings.qml
        settings/StorageSettings.qml
        settings/ShortcutSettings.qml
        settings/AboutSettings.qml
        HomePage.qml
        TodayCard.qml
        FmCard.qml
        HomeSection.qml
        SidebarRow.qml
        RailPlaylists.qml
        RailAccount.qml
        SearchField.qml
        TopBar.qml
        PanelCorner.qml
        CloseDialog.qml
        DateTile.qml
    SOURCES
        desktop.h
        desktop.cpp
    RESOURCES
        icons/cirrus-music.svg
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

# Shown in Settings › About.
target_compile_definitions(appCirrusMusic PRIVATE CIRRUS_VERSION="${PROJECT_VERSION}")
# The commit, for the log's session header (docs/tasks/logging.md §6): checked at every
# build; the header only changes (and main.cpp only rebuilds) when the commit does.
set(CIRRUS_COMMIT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/cirrus_commit.h")
add_custom_target(cirrus_commit
    COMMAND ${CMAKE_COMMAND} -DSRC=${PROJECT_SOURCE_DIR} -DOUT=${CIRRUS_COMMIT_HEADER}
            -P ${PROJECT_SOURCE_DIR}/cmake/cirrus_commit.cmake
    BYPRODUCTS ${CIRRUS_COMMIT_HEADER}
    COMMENT "Checking the commit for the log header" VERBATIM)
add_dependencies(appCirrusMusic cirrus_commit)
target_include_directories(appCirrusMusic PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_link_libraries(appCirrusMusic PRIVATE
    cirrus_logging
    Qt6::Quick
    Qt6::Network
    cirrus_ui_themeplugin
    cirrus_ui_controlsplugin
    cirrus_ui_window
    cirrus_ui_windowplugin
    cirrus_presentation
    cirrus_presentationplugin
    cirrus_presentation_backend
    cirrus_ui_componentsplugin
    cirrus_ui_playerplugin
    # Fake data until the backend sources exist (UI phase 5).
    cirrus_ui_samplesplugin
    m3shapesplugin
)

# The tray icon (docs/10-linux-integration.md §4). QSystemTrayIcon lives in Qt Widgets
# and needs QApplication; without it closing the window always quits.
option(CIRRUS_UI_TRAY "Show a system tray icon (links Qt Widgets)" ON)
if(CIRRUS_UI_TRAY)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    target_sources(appCirrusMusic PRIVATE tray.h tray.cpp)
    target_compile_definitions(appCirrusMusic PRIVATE CIRRUS_UI_TRAY)
    target_link_libraries(appCirrusMusic PRIVATE Qt6::Widgets)
endif()

# The system's media controls and media keys (docs/ui/pages/shell.md §7): MPRIS on
# Linux, SMTC on Windows, Now Playing on macOS. All read App.player (media_state.h).
# SMTC and Now Playing are not yet verified on their systems.
target_sources(appCirrusMusic PRIVATE media_state.h)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    target_sources(appCirrusMusic PRIVATE mpris.h mpris.cpp)
    target_compile_definitions(appCirrusMusic PRIVATE CIRRUS_UI_MPRIS)
    target_link_libraries(appCirrusMusic PRIVATE Qt6::DBus)
elseif(WIN32)
    # C++/WinRT from the Windows SDK (MSVC).
    option(CIRRUS_UI_SMTC "Windows media controls (SMTC); needs MSVC and the Windows SDK" ON)
    if(CIRRUS_UI_SMTC)
        target_sources(appCirrusMusic PRIVATE smtc.h smtc.cpp)
        set_source_files_properties(smtc.cpp PROPERTIES
            COMPILE_DEFINITIONS "NOMINMAX;WIN32_LEAN_AND_MEAN")
        target_compile_definitions(appCirrusMusic PRIVATE CIRRUS_UI_SMTC)
        target_link_libraries(appCirrusMusic PRIVATE windowsapp runtimeobject)
    endif()
elseif(APPLE)
    option(CIRRUS_UI_NOW_PLAYING "macOS Now Playing (MediaPlayer framework)" ON)
    if(CIRRUS_UI_NOW_PLAYING)
        enable_language(OBJCXX)
        target_sources(appCirrusMusic PRIVATE now_playing.h now_playing.mm)
        set_source_files_properties(now_playing.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        set_target_properties(appCirrusMusic PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON)
        target_compile_definitions(appCirrusMusic PRIVATE CIRRUS_UI_NOW_PLAYING)
        target_link_libraries(appCirrusMusic PRIVATE "-framework AppKit" "-framework MediaPlayer")
    endif()
endif()

install(TARGETS appCirrusMusic
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
