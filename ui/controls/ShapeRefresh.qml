import QtQuick

// Works around a Qt Quick Shapes bug (6.11) in the curve renderer: when the window gives
// back its scene graph (hidden in the tray, AppWindow sets setPersistentSceneGraph(false))
// and builds it again, a Shape gets a new, empty node, and QQuickShapeCurveRenderer::
// setRootNode() does not mark its paths dirty, so nothing is drawn until a path changes.
// `offset` flips between 0 and 0.001 each time the window is shown again; adding it to a
// ShapePath's startX (ignored by arcs, polylines and rectangles that move to their own
// start, an invisible shift otherwise; or to the stroke width of a stroke-only path)
// marks the path dirty and it is drawn again.
//   ShapeRefresh { id: refresh; target: shape }
//   ShapePath { startX: 4.5 + refresh.offset }
QtObject {
    id: root

    // Any item in the window.
    required property Item target
    property int shown: 0
    readonly property real offset: (shown % 2) * 0.001

    readonly property Connections watcher: Connections {
        target: root.target ? root.target.Window.window : null
        function onVisibleChanged() {
            if (root.target.Window.window?.visible)
                ++root.shown;
        }
    }
}
