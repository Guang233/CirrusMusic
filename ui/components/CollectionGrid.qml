pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Presentation

// Cards of a CollectionListModel in rows that fill the width (not scrollable; place
// it in a page's list or flickable). Cards are at least `minimumCardWidth` wide.
Item {
    id: root

    property CollectionListModel model
    property real minimumCardWidth: 152
    property int maximumRows: 0 // 0 for all

    readonly property int columns: Math.max(1, Math.floor(width / minimumCardWidth))
    readonly property real cardWidth: width / columns

    signal activated(int kind, string key)

    implicitHeight: flow.implicitHeight

    Flow {
        id: flow
        width: root.width

        Repeater {
            model: root.model
            delegate: CollectionCard {
                required property int index
                required property string key
                required property string title
                // The card's own properties, filled from the roles of the same name.
                required kind
                required subtitle
                required coverKey
                width: root.cardWidth
                visible: root.maximumRows <= 0 || index < root.maximumRows * root.columns
                text: title
                onClicked: root.activated(kind, key)
            }
        }
    }
}
