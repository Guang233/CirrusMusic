# Business-facing building blocks of the shell and pages (docs/ui/README.md §3.1).
qt_add_library(cirrus_ui_components STATIC)

qt_add_qml_module(cirrus_ui_components
    URI Cirrus.Components
    VERSION 1.0
    DEPENDENCIES
        QtQuick
        Cirrus.Theme
        Cirrus.Controls
        Cirrus.Presentation
    QML_FILES
        PageStack.qml
        PageScaffold.qml
        CoverArt.qml
        PlaybackClock.qml
        MiniPlayer.qml
        Skeleton.qml
        ListSkeleton.qml
        StateMessage.qml
        NoticeBar.qml
        PageState.qml
        SongList.qml
        SongTable.qml
        SongTableSkeleton.qml
        CollectionCard.qml
        CollectionGrid.qml
        CollectionView.qml
        LoadMoreFooter.qml
)

target_link_libraries(cirrus_ui_components
    PRIVATE Qt6::Quick cirrus_ui_theme cirrus_ui_controls cirrus_presentation)
