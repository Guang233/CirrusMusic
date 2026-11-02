import QtQuick
import Cirrus.Theme
import Cirrus.Controls

// Creating a playlist (library.md §2): a title, then 新建. The shell opens the new
// playlist once it exists.
Dialog {
    id: dialog

    signal createRequested(string title)

    width: 440
    title: qsTr("新建歌单")
    acceptText: qsTr("新建")
    rejectText: qsTr("取消")
    acceptEnabled: titleField.text.trim() !== ""
    initialFocusItem: titleField
    onAboutToShow: titleField.text = ""
    onAccepted: createRequested(titleField.text)

    TextField {
        id: titleField
        width: parent.width
        variant: Md.Outlined
        gapColor: Theme.color.surfaceContainerHigh
        label: qsTr("歌单标题")
        maximumLength: 40
        supportingText: qsTr("最多 40 个字")
    }
}
