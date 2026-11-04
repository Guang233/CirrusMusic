pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Presentation

// Light or dark, the colors, the text font, motion.
SettingsGroup {
    title: qsTr("外观")

    ChoiceSetting {
        text: qsTr("明暗")
        options: [{
                value: 0,
                text: qsTr("跟随系统")
            }, {
                value: 1,
                text: qsTr("亮色")
            }, {
                value: 2,
                text: qsTr("暗色")
            }]
        value: App.settings.themeMode
        onChosen: value => App.settings.themeMode = value
    }
    // Theme.SeedSource: Cover (0), Fixed (1), System (2).
    ChoiceSetting {
        text: qsTr("主题色来源")
        supportingText: App.settings.seedSource === 0
            ? qsTr("界面配色随正在播放的歌曲封面变化。")
            : App.settings.seedSource === 1 ? qsTr("用下面选的颜色生成整套配色。")
                                            : qsTr("使用桌面的强调色。")
        options: [{
                value: 2,
                text: qsTr("系统强调色")
            }, {
                value: 0,
                text: qsTr("跟随封面")
            }, {
                value: 1,
                text: qsTr("固定")
            }]
        value: App.settings.seedSource
        onChosen: value => App.settings.seedSource = value
    }
    SettingRow {
        shown: App.settings.seedSource === 1
        text: qsTr("主题色")
        focusPolicy: Qt.NoFocus
        hoverEnabled: false
        trailing: Component {
            SeedSwatches {
                value: App.settings.fixedSeed
                onChosen: value => App.settings.fixedSeed = value
            }
        }
    }
    MenuSetting {
        text: qsTr("配色风格")
        supportingText: qsTr("同一个主题色生成配色的方式。")
        // Theme.Variant; the others (Content, Fidelity, Rainbow, FruitSalad) suit
        // colors taken from pictures, not a setting (settings.md §2).
        options: [{
                value: 0,
                text: qsTr("标准"),
                description: qsTr("柔和，以主题色为主")
            }, {
                value: 1,
                text: qsTr("表现力"),
                description: qsTr("色相更丰富，带一点意外")
            }, {
                value: 2,
                text: qsTr("鲜明"),
                description: qsTr("饱和度更高")
            }, {
                value: 5,
                text: qsTr("中性"),
                description: qsTr("接近灰色，只带一点主题色")
            }, {
                value: 6,
                text: qsTr("单色"),
                description: qsTr("只有灰阶")
            }]
        value: App.settings.variant
        onChosen: value => App.settings.variant = value
    }
    ChoiceSetting {
        text: qsTr("字体")
        supportingText: Fonts.bundledAvailable
            ? qsTr("内置字体为小米 MiSans。字体缺少的字符（如韩文、emoji）由系统字体补全。")
            : qsTr("这个版本没有内置字体，使用系统字体。")
        options: [{
                value: Fonts.Bundled,
                text: qsTr("内置（MiSans）")
            }, {
                value: Fonts.System,
                text: qsTr("系统字体")
            }, {
                value: Fonts.Custom,
                text: qsTr("自定义")
            }]
        value: App.settings.fontSource
        onChosen: value => App.settings.fontSource = value
    }
    MenuSetting {
        shown: App.settings.fontSource === Fonts.Custom
        text: qsTr("字体族")
        supportingText: qsTr("已安装的字体")
        placeholder: qsTr("选择字体")
        // Read once shown: there can be hundreds.
        options: shown ? Fonts.families().map(family => ({
                                                           value: family,
                                                           text: family
                                                       })) : []
        value: Fonts.customFamily !== "" && Fonts.sans === Fonts.customFamily ? Fonts.customFamily
                                                                              : ""
        onChosen: value => App.settings.customFontFamily = value
    }
    SwitchSetting {
        text: qsTr("减少动态效果")
        supportingText: qsTr("页面切换改为淡入淡出，不做位移和缩放")
        value: App.settings.reducedMotion
        onSwitched: value => App.settings.reducedMotion = value
    }
}
