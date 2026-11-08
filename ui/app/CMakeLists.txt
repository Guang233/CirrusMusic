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

target_link_libraries(appCirrusMusic PRIVATE
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

# MPRIS on the session bus (docs/ui/pages/shell.md §7): media keys and the desktop's
# media controls. Linux only; Windows SMTC / macOS Now Playing come later.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    target_sources(appCirrusMusic PRIVATE mpris.h mpris.cpp)
    target_compile_definitions(appCirrusMusic PRIVATE CIRRUS_UI_MPRIS)
    target_link_libraries(appCirrusMusic PRIVATE Qt6::DBus)
endif()

install(TARGETS appCirrusMusic
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
