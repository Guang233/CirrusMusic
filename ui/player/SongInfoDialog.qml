pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// Copying from the playing song (docs/ui/pages/player.md §1.1): the title, artists
// and album, each selectable with a copy button, and the whole lyrics as selectable
// text, with the translation or romanization after each line if wanted. Text on the
// player page itself isn't selectable (it moves); this is where to copy it from.
Dialog {
    id: root

    readonly property var song: App.player.current
    property bool withTranslation: false
    property bool withRomanization: false

    width: Math.min(560, parent ? parent.width - 48 : 560)
    glyph: Symbols.content_copy
    title: qsTr("复制歌曲信息")
    rejectText: qsTr("关闭")

    component Field: Item {
        id: field
        required property string label
        required property string value
        width: parent.width
        height: 56
        visible: value !== ""

        Text {
            id: labelText
            y: 4
            text: field.label
            color: ColorScope.scheme.onSurfaceVariant
            font: Theme.type.labelMedium.font
        }
        TextEdit {
            anchors.left: parent.left
            anchors.right: copy.left
            anchors.rightMargin: 8
            anchors.top: labelText.bottom
            anchors.topMargin: 2
            text: field.value
            readOnly: true
            selectByMouse: true
            selectByKeyboard: true
            activeFocusOnTab: true
            wrapMode: TextEdit.NoWrap
            clip: true
            color: ColorScope.scheme.onSurface
            selectionColor: ColorScope.scheme.primaryContainer
            selectedTextColor: ColorScope.scheme.onPrimaryContainer
            font: Theme.type.bodyLarge.font
            Accessible.name: field.label
        }
        IconButton {
            id: copy
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            glyph: Symbols.content_copy
            text: qsTr("复制%1").arg(field.label)
            onClicked: App.copyText(field.value)
        }
    }

    Column {
        id: fields
        width: parent.width
        spacing: 8

        Field {
            label: qsTr("歌名")
            value: root.song.title
        }
        Field {
            label: qsTr("歌手")
            value: root.song.artistText
        }
        Field {
            label: qsTr("专辑")
            value: root.song.albumTitle
        }

        // The lyrics.
        Item {
            width: parent.width
            height: 40
            visible: App.lyrics.status === Lyrics.Ready

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("歌词")
                color: ColorScope.scheme.onSurfaceVariant
                font: Theme.type.labelMedium.font
            }
            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                Chip {
                    visible: App.lyrics.hasTranslation
                    checkable: true
                    checked: root.withTranslation
                    text: qsTr("翻译")
                    onClicked: root.withTranslation = checked
                }
                Chip {
                    visible: App.lyrics.hasRomanization
                    checkable: true
                    checked: root.withRomanization
                    text: qsTr("音译")
                    onClicked: root.withRomanization = checked
                }
                IconButton {
                    glyph: Symbols.content_copy
                    text: qsTr("复制歌词")
                    onClicked: App.copyText(lyrics.text)
                }
            }
        }
        Rectangle {
            width: parent.width
            height: 240
            visible: App.lyrics.status === Lyrics.Ready
            radius: Theme.shape.medium
            color: ColorScope.scheme.surfaceContainer

            Flickable {
                id: flick
                anchors.fill: parent
                anchors.margins: 12
                clip: true
                contentHeight: lyrics.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                TextEdit {
                    id: lyrics
                    width: flick.width - 12
                    text: root.opened ? App.lyrics.plainText(root.withRomanization, root.withTranslation) : ""
                    readOnly: true
                    selectByMouse: true
                    selectByKeyboard: true
                    activeFocusOnTab: true
                    wrapMode: TextEdit.Wrap
                    color: ColorScope.scheme.onSurface
                    selectionColor: ColorScope.scheme.primaryContainer
                    selectedTextColor: ColorScope.scheme.onPrimaryContainer
                    font: Theme.type.bodyMedium.font
                    Accessible.name: qsTr("歌词")
                    // Keeps the caret in view while selecting with the keyboard.
                    onCursorRectangleChanged: {
                        const r = cursorRectangle;
                        if (r.y < flick.contentY)
                            flick.contentY = r.y;
                        else if (r.y + r.height > flick.contentY + flick.height)
                            flick.contentY = r.y + r.height - flick.height;
                    }
                }
            }
        }
    }

    buttons: Button {
        variant: Md.Text
        text: qsTr("复制全部")
        onClicked: {
            const parts = [root.song.title, root.song.artistText];
            if (lyrics.text !== "")
                parts.push("", lyrics.text);
            App.copyText(parts.join("\n"));
        }
    }
}
