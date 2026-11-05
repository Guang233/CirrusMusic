pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme

// MD3E navigation rail (androidx WideNavigationRail): 96 wide collapsed, 220-360
// expanded (fits the widest label). Children are NavigationRailItems; `currentIndex`
// follows clicks and `activated(index)` reports them. `header` holds e.g. the menu
// button, left-aligned with the indicators (center a 40 px button with x: 8).
// `modal`: the rail expands over the content (the shell adds the scrim); its floating
// look (SurfaceContainer, rounded trailing corners) grows with the expansion. No
// shadow: the scrim already sets it apart.
// `footer` sits at the bottom (e.g. account and settings; it lays itself out for
// `progress`). `expandedContent` fills the space between the items and the footer
// while expanded (e.g. the user's playlists): it is laid out at the expanded width,
// clipped by the rail and fades in late in the expansion.
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
    property Component footer
    property Component expandedContent
    property real minimumExpandedWidth: 220
    // Space above the header (or the items without one).
    property real topInset: 44
    // Color while docked; floating it becomes SurfaceContainer.
    property color restColor: ColorScope.scheme.surface
    default property alias items: column.data

    // 0 collapsed, 1 expanded; drives the width and every item's layout.
    readonly property real progress: widthSpring.value
    readonly property real collapsedWidth: 96
    readonly property real expandedWidth: Math.max(minimumExpandedWidth, Math.min(360, widestItem))
    property real widestItem: 0

    signal activated(int index)

    // Sets `expanded` without animating, e.g. when the window crosses a breakpoint.
    function snap(value: bool) {
        expanded = value;
        widthSpring.snapTo(value ? 1 : 0);
    }

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

    // How far the floating look has grown: the expansion progress of a modal rail.
    readonly property real floating: modal ? progress : 0

    implicitWidth: Theme.motion.lerp(collapsedWidth, expandedWidth, progress)
    color: Theme.mix(restColor, ColorScope.scheme.surfaceContainer, floating)
    topRightRadius: Theme.shape.large * floating
    bottomRightRadius: Theme.shape.large * floating


    Loader {
        id: headerLoader
        x: 20
        y: root.topInset
        sourceComponent: root.header
    }

    Column {
        id: column
        y: headerLoader.item ? headerLoader.y + headerLoader.height + 40 : root.topInset
        width: root.width
        spacing: Theme.motion.lerp(4, 0, root.progress)
        onChildrenChanged: Qt.callLater(root.attach)
    }

    Item {
        id: expandedArea
        y: column.y + column.height + 8
        width: root.width
        height: Math.max(0, (footerLoader.item ? footerLoader.y : root.height) - y - 8)
        clip: true
        opacity: Theme.motion.segment(root.progress, 0.5, 1)
        visible: opacity > 0 && root.expandedContent !== null

        Loader {
            width: root.expandedWidth
            height: parent.height
            sourceComponent: root.expandedContent
        }
    }

    Loader {
        id: footerLoader
        y: root.height - height - 16
        width: root.width
        sourceComponent: root.footer
    }
}
