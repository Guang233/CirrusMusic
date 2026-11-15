import QtQuick
import QtQuick.Shapes
import Cirrus.Controls

// One rounded corner of the content panel, drawn as the frame color outside a
// quarter circle (top-left as is; rotate by 90° steps for the others). Covering the
// corners is cheaper than clipping the whole page stack to a rounded shape.
Shape {
    id: corner

    property real radius
    property color color

    width: radius
    height: radius
    preferredRendererType: Shape.CurveRenderer

    // Drawn again after the window comes back from the tray (a Qt bug).
    ShapeRefresh {
        id: refresh
        target: corner
    }

    ShapePath {
        fillColor: corner.color
        strokeWidth: -1
        startX: refresh.offset
        startY: 0
        PathLine {
            x: corner.radius
            y: 0
        }
        PathArc {
            x: 0
            y: corner.radius
            radiusX: corner.radius
            radiusY: corner.radius
            direction: PathArc.Counterclockwise
        }
        PathLine {
            x: 0
            y: 0
        }
    }
}
