pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme

// MD3E navigation rail (androidx WideNavigationRail): 96 wide collapsed, 220-360
// expanded (fits the widest label). Children are NavigationRailItems; `currentIndex`
// follows clicks and `activated(index)` reports them. `header` holds e.g. the menu
// button, left-aligned with the indicators (center a 40 px button with x: 8).
// `modal` is the look of a rail floating over the content (the shell adds the scrim).
//   NavigationRail {
//       expanded: wide
//       NavigationRailItem { glyph: Symbols.home; text: qsTr("首页") }
//   }
Rectangle {
    id: root

    property bool expanded: false
    property bool modal: false
    property int currentIndex: 0
    property Component header
    default property alias items: column.data

    // 0 collapsed, 1 expanded; drives the width and every item's layout.
    readonly property real progress: widthSpring.value
    readonly property real collapsedWidth: 96
    readonly property real expandedWidth: Math.max(220, Math.min(360, widestItem))
    property real widestItem: 0

    signal activated(int index)

    function railItems() {
        const result = [];
        for (let i = 0; i < column.children.length; ++i) {
            const child = column.children[i];
            if (child instanceof NavigationRailItem)
                result.push(child);
        }
        return result;
    }

    function attach() {
        if (!root) // null while the rail is being torn down
            return;
        const list = railItems();
        let widest = 0;
        list.forEach((item, i) => {
            item.railIndex = i;
            item.progress = Qt.binding(() => root.progress);
            item.selected = Qt.binding(() => root.currentIndex === item.railIndex);
            item.width = Qt.binding(() => column.width);
            if (item.rail !== root) {
                item.clicked.connect(() => {
                    root.currentIndex = item.railIndex;
                    root.activated(item.railIndex);
                });
                item.expandedWidthChanged.connect(root.attach);
                item.rail = root;
            }
            widest = Math.max(widest, item.expandedWidth);
        });
        widestItem = widest;
    }

    // Pushing content: standard spring, so the reflowing layout does not bounce.
    // Floating: expressive (docs/ui/motion-choreography.md §5).
    Spring {
        id: widthSpring
        target: root.expanded ? 1 : 0
        spec: root.modal ? Theme.motion.defaultSpatial : Theme.motion.standardDefaultSpatial
        threshold: 0.001
    }

    implicitWidth: Theme.motion.lerp(collapsedWidth, expandedWidth, progress)
    color: modal ? Theme.color.surfaceContainer : Theme.color.surface
    topRightRadius: modal ? Theme.shape.large : 0
    bottomRightRadius: modal ? Theme.shape.large : 0

    Elevation {
        anchors.fill: parent
        z: -1
        level: root.modal ? Theme.elevation.level2 : 0
        topRightRadius: root.topRightRadius
        bottomRightRadius: root.bottomRightRadius
    }

    Loader {
        id: headerLoader
        x: 20
        y: 44
        sourceComponent: root.header
    }

    Column {
        id: column
        y: headerLoader.item ? headerLoader.y + headerLoader.height + 40 : 44
        width: root.width
        spacing: Theme.motion.lerp(4, 0, root.progress)
        onChildrenChanged: Qt.callLater(root.attach)
    }
}
