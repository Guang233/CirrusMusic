pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// The collapsed rail's shortcuts (docs/ui/redesign-md3e.md §8): the covers of the
// account's own playlists, as many as fit. The open playlist has a ring; the name is
// in the tooltip. The whole list is in the expanded rail.
Item {
    id: root

    required property LibraryPageModel library
    // The playlist open on the page, for the ring.
    property string selectedKey

    signal playlistRequested(string key)

    readonly property real pitch: 56
    readonly property int fit: Math.max(0, Math.floor((height - 17) / pitch))

    visible: App.session.signedIn

    Rectangle {
        x: (root.width - width) / 2
        width: 32
        height: 1
        visible: root.fit > 0 && covers.count > 0
        color: Theme.color.outlineVariant
    }

    Column {
        y: 17
        width: root.width
        spacing: root.pitch - 48

        Repeater {
            id: covers
            model: root.library.createdPlaylists
            delegate: T.AbstractButton {
                id: item
                required property int index
                required property string key
                required property string title
                required property string coverKey
                readonly property bool selected: root.selectedKey === key
                property bool hoveredOnce: false

                x: (root.width - width) / 2
                width: 48
                height: 48
                visible: index < root.fit
                focusPolicy: Qt.TabFocus
                text: title
                Accessible.name: title
                onClicked: root.playlistRequested(key)
                onHoveredChanged: if (hovered)
                                      hoveredOnce = true

                Spring {
                    id: corner
                    target: item.down ? Theme.shape.small
                          : item.selected || item.hovered ? 24 : Theme.shape.medium
                    spec: Theme.motion.fastSpatial
                }

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }
                contentItem: CoverArt {
                    coverKey: item.coverKey
                    glyph: Symbols.queue_music
                    radius: Math.max(0, corner.value)
                }
                background: Item {
                    // The ring of the open playlist, a little outside the cover.
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -4
                        visible: item.selected
                        radius: Math.max(0, corner.value) + 4
                        color: "transparent"
                        border.width: 2
                        border.color: Theme.color.primary
                    }
                    FocusRing {
                        shown: item.visualFocus
                        offset: item.selected ? 6 : 2
                        containerRadius: Math.max(0, corner.value)
                    }
                }
                Loader {
                    active: item.hoveredOnce
                    sourceComponent: ToolTip {
                        parent: item
                        text: item.title
                        visible: item.hovered && !item.pressed
                    }
                }
            }
        }
    }
}
