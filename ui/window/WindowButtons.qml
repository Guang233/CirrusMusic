import QtQuick
import Cirrus.Theme
import Cirrus.Controls

// Minimize, maximize/restore and close for the custom title bar.
Row {
    id: root

    readonly property bool maximized: Window.visibility === Window.Maximized
                                      || Window.visibility === Window.FullScreen

    function toggleMaximized() {
        if (maximized)
            Window.window.showNormal();
        else
            Window.window.showMaximized();
    }

    spacing: 4

    IconButton {
        size: Md.XSmall
        glyph: Symbols.remove
        text: qsTr("最小化")
        onClicked: root.Window.window.showMinimized()
    }
    IconButton {
        size: Md.XSmall
        glyph: root.maximized ? Symbols.filter_none : Symbols.crop_square
        text: root.maximized ? qsTr("还原") : qsTr("最大化")
        onClicked: root.toggleMaximized()
    }
    IconButton {
        size: Md.XSmall
        glyph: Symbols.close
        text: qsTr("关闭")
        onClicked: root.Window.window.close()
    }
}
