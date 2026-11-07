# The player page (docs/ui/pages/player.md): Apple Music-like, in its own language,
# over the whole window.
qt_add_library(cirrus_ui_player STATIC)

qt_add_qml_module(cirrus_ui_player
    URI Cirrus.Player
    VERSION 1.0
    DEPENDENCIES
        QtQuick
        Cirrus.Theme
        Cirrus.Controls
        Cirrus.Components
        Cirrus.Presentation
        Cirrus.Window
    QML_FILES
        PlayerPage.qml
        PlayerBackground.qml
        PlayerCover.qml
        PlayerButton.qml
        PlayerProgress.qml
        PlayerControls.qml
        SongHeader.qml
        VolumeButton.qml
        LyricsView.qml
        LyricLine.qml
        LyricSegment.qml
        InterludeDots.qml
        PlayerQueue.qml
        PlayerSheet.qml
        SongInfoDialog.qml
)

find_package(Qt6 REQUIRED COMPONENTS ShaderTools)
qt_add_shaders(cirrus_ui_player "cirrus_ui_player_shaders"
    PREFIX "/qt/qml/Cirrus/Player"
    FILES
        shaders/flow.frag
        shaders/present.frag
        shaders/fill.frag
        shaders/edgefade.frag
)

target_link_libraries(cirrus_ui_player
    PRIVATE Qt6::Quick cirrus_ui_theme cirrus_ui_controls cirrus_ui_components cirrus_presentation
            cirrus_ui_window)
