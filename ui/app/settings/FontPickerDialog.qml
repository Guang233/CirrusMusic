pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs as D
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// Picks the custom text font: a search over the families (each previewed in itself,
// hundreds of them, so a list rather than a menu), or a font file that is not
// installed. "使用" applies the selection.
Dialog {
    id: dialog

    // The family selected in the list; starts at the one in use.
    property string selected
    property var families: []
    readonly property var shown: {
        const query = search.text.trim().toLowerCase();
        return query === "" ? families
                            : families.filter(family => family.toLowerCase().includes(query));
    }

    function choose(family: string): void {
        App.settings.customFontFamily = family;
        dialog.close();
    }

    title: qsTr("选择字体")
    // Wide enough for long family names; the content follows.
    width: Math.min(560, (parent ? parent.width : 560) - 48)
    acceptText: qsTr("使用")
    rejectText: qsTr("取消")
    acceptEnabled: selected !== ""
    initialFocusItem: search
    onAboutToShow: {
        families = Fonts.families();
        selected = App.settings.customFontFamily;
        search.text = "";
        Qt.callLater(() => list.positionViewAtIndex(Math.max(0, shown.indexOf(selected)),
                                                    ListView.Center));
    }
    onAccepted: App.settings.customFontFamily = selected

    buttons: Button {
        variant: Md.Text
        glyph: Symbols.font_download
        text: qsTr("从文件添加…")
        onClicked: fileDialog.open()
    }

    Column {
        width: dialog.availableWidth
        spacing: 12

        TextField {
            id: search
            width: parent.width
            leadingGlyph: Symbols.search
            placeholderText: qsTr("搜索字体")
            // ↓ goes on to the list.
            Keys.onDownPressed: {
                list.currentIndex = Math.max(0, list.currentIndex);
                list.forceActiveFocus();
            }
        }

        ListView {
            id: list
            width: parent.width
            height: 360
            clip: true
            model: dialog.shown
            currentIndex: dialog.shown.indexOf(dialog.selected)
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            onCurrentIndexChanged: {
                if (activeFocus && currentIndex >= 0)
                    dialog.selected = dialog.shown[currentIndex];
            }
            Keys.onReturnPressed: dialog.accept()
            Keys.onEnterPressed: dialog.accept()
            Accessible.role: Accessible.List
            Accessible.name: qsTr("字体")

            delegate: ListItem {
                required property int index
                required property string modelData
                width: ListView.view.width
                lines: 1
                segmented: true
                highlighted: modelData === dialog.selected
                text: modelData
                // Each family previews itself; the size follows bodyLarge.
                font: Qt.font({
                    family: modelData,
                    pixelSize: Theme.type.bodyLarge.font.pixelSize
                })
                focusPolicy: Qt.NoFocus
                onClicked: dialog.selected = modelData
                onDoubleClicked: dialog.choose(modelData)
            }

            Text {
                anchors.centerIn: parent
                visible: list.count === 0
                text: qsTr("没有找到这个字体")
                color: ColorScope.scheme.onSurfaceVariant
                font: Theme.type.bodyMedium.font
            }
        }
    }

    D.FileDialog {
        id: fileDialog
        title: qsTr("选择字体文件")
        nameFilters: [qsTr("字体文件 (*.ttf *.otf *.ttc *.otc)")]
        onAccepted: {
            const path = String(selectedFile);
            const family = Fonts.loadFontFile(path);
            if (family === "") {
                failed.open();
                return;
            }
            // Kept and loaded at every start.
            if (!App.settings.customFontFiles.includes(path))
                App.settings.customFontFiles = App.settings.customFontFiles.concat([path]);
            dialog.choose(family);
        }
    }

    Dialog {
        id: failed
        title: qsTr("不是可用的字体文件")
        supportingText: qsTr("支持 TTF、OTF、TTC 和 OTC 格式。")
        acceptText: qsTr("好")
    }
}
