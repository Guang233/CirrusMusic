pragma ComponentBehavior: Bound
import QtQuick

// Resize bands for the custom title bar. Fill the visible window frame: each band
// reaches `outside` px into the transparent shadow margin and `inside` px over the
// window edge; corners are larger so diagonal resizing is easy to hit.
Item {
    id: root

    property real outside
    property real inside: 4
    property real corner: 16

    readonly property real band: outside + inside
    readonly property real cornerSize: outside + corner

    component Handle: Item {
        id: handle

        required property int edges
        required property int cursor

        HoverHandler {
            cursorShape: handle.cursor
        }
        DragHandler {
            target: null
            dragThreshold: 0
            onActiveChanged: if (active)
                                 handle.Window.window.startSystemResize(handle.edges)
        }
    }

    Handle {
        edges: Qt.LeftEdge
        cursor: Qt.SizeHorCursor
        x: -root.outside
        y: root.corner
        width: root.band
        height: root.height - 2 * root.corner
    }
    Handle {
        edges: Qt.RightEdge
        cursor: Qt.SizeHorCursor
        x: root.width - root.inside
        y: root.corner
        width: root.band
        height: root.height - 2 * root.corner
    }
    Handle {
        edges: Qt.TopEdge
        cursor: Qt.SizeVerCursor
        x: root.corner
        y: -root.outside
        width: root.width - 2 * root.corner
        height: root.band
    }
    Handle {
        edges: Qt.BottomEdge
        cursor: Qt.SizeVerCursor
        x: root.corner
        y: root.height - root.inside
        width: root.width - 2 * root.corner
        height: root.band
    }
    Handle {
        edges: Qt.TopEdge | Qt.LeftEdge
        cursor: Qt.SizeFDiagCursor
        x: -root.outside
        y: -root.outside
        width: root.cornerSize
        height: root.cornerSize
    }
    Handle {
        edges: Qt.TopEdge | Qt.RightEdge
        cursor: Qt.SizeBDiagCursor
        x: root.width - root.corner
        y: -root.outside
        width: root.cornerSize
        height: root.cornerSize
    }
    Handle {
        edges: Qt.BottomEdge | Qt.LeftEdge
        cursor: Qt.SizeBDiagCursor
        x: -root.outside
        y: root.height - root.corner
        width: root.cornerSize
        height: root.cornerSize
    }
    Handle {
        edges: Qt.BottomEdge | Qt.RightEdge
        cursor: Qt.SizeFDiagCursor
        x: root.width - root.corner
        y: root.height - root.corner
        width: root.cornerSize
        height: root.cornerSize
    }
}
