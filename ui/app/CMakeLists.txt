qt_add_executable(appCirrusMusic
    main.cpp
)

qt_add_qml_module(appCirrusMusic
    URI CirrusMusic
    QML_FILES
        Main.qml
        AppShell.qml
        PlaylistPage.qml
        AlbumPage.qml
        ArtistPage.qml
        SearchPage.qml
        FeedPage.qml
        DailySongsPage.qml
        LibraryPage.qml
        LoginDialog.qml
        ChartCard.qml
        SettingsPage.qml
        HomePage.qml
        TodayCard.qml
        FmCard.qml
        HomeSection.qml
        SidebarRow.qml
        RailPlaylists.qml
        RailAccount.qml
        SearchField.qml
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

target_link_libraries(appCirrusMusic PRIVATE
    Qt6::Quick
    cirrus_ui_themeplugin
    cirrus_ui_controlsplugin
    cirrus_ui_window
    cirrus_ui_windowplugin
    cirrus_presentation
    cirrus_presentationplugin
    cirrus_ui_componentsplugin
    # Fake data until the backend sources exist (UI phase 5).
    cirrus_ui_samplesplugin
    m3shapesplugin
)

install(TARGETS appCirrusMusic
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
