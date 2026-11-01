pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Controls
import Cirrus.Presentation

// Scrolling grid of collection cards, e.g. search results or the library. With a
// `pager` it loads the next page as the end comes near.
GridView {
    id: view

    property real minimumCardWidth: 152
    property real sideMargin: 24
    property PagedModel pager

    readonly property int columns: Math.max(1, Math.floor((width - 2 * sideMargin + 16)
                                                          / minimumCardWidth))

    signal activated(int kind, string key)

    function maybeLoadMore(): void {
        if (pager && pager.canLoadMore && pager.loadMoreError.kind < 0
                && contentY + height > contentHeight + originY - 600)
            pager.loadMore();
    }

    // Cards carry 8 px of padding, so the covers line up with the page margins.
    leftMargin: sideMargin - 8
    rightMargin: sideMargin - 8
    bottomMargin: 24
    cellWidth: (width - leftMargin - rightMargin) / columns
    cellHeight: cellWidth + 48
    clip: true
    // Overshoots and springs back at the ends.
    boundsBehavior: Flickable.DragAndOvershootBounds
    ScrollBar.vertical: ScrollBar {}

    onContentYChanged: maybeLoadMore()
    onCountChanged: Qt.callLater(maybeLoadMore)

    footer: LoadMoreFooter {
        width: view.width - view.leftMargin - view.rightMargin
        model: view.pager
    }

    delegate: CollectionCard {
        required property string key
        required property string title
        required kind
        required subtitle
        required coverKey
        width: view.cellWidth
        text: title
        onClicked: view.activated(kind, key)
    }
}
