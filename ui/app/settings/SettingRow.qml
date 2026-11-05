import QtQuick
import Cirrus.Theme
import Cirrus.Controls

// A row of a SettingsGroup: the setting's name (`text`) and what it does
// (`supportingText`, wraps) on the left, its control on the right (`trailing`). The
// typed rows (SwitchSetting, ChoiceSetting, MenuSetting, ActionSetting) build on it;
// use it directly for rows of their own kind (the account, the shortcuts).
// `shown: false` folds the row away (and back) smoothly, for rows that depend on
// another setting; set `visible` instead for rows that never show on this machine.
ListItem {
    id: row

    property bool shown: true

    width: parent ? parent.width : 0
    height: implicitHeight * Math.min(1, reveal.value)
    visible: reveal.value > 0.01
    // Not reachable while folding away.
    enabled: shown
    opacity: Math.min(1, reveal.value)
    clip: reveal.value < 1
    // The row is the Tab stop for its switch or menu; rows whose control takes the
    // focus itself set Qt.NoFocus.
    focusPolicy: Qt.StrongFocus
    segmented: true
    containerColor: ColorScope.scheme.surfaceContainerLow
    supportingMaximumLines: 8
    // Two-line height at least; longer text grows the row (the control stays centered).
    lines: supportingText !== "" ? 2 : 1

    Spring {
        id: reveal
        target: row.shown ? 1 : 0
        spec: Theme.motion.standardFastSpatial
        threshold: 0.005
    }
}
