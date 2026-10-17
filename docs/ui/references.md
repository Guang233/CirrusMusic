# UI 参考调研清单

> 状态：阶段 0 产出，2026-10-17。许可、语言、活跃度均于当日通过 GitHub API 核对；“借鉴内容”是阶段 3、4 设计时的出发点，届时按页面补充具体对照。
>
> 总原则：项目自身许可证尚未确定，**在此之前不复制任何 GPL／AGPL 代码**，只借鉴视觉与交互；MIT／Apache 代码如需借用，必须保留版权与许可声明并在本文登记。

## 1. 直接使用的依赖候选

| 项目 | 许可 | 用途 | 状态 |
| --- | --- | --- | --- |
| [material-color-utilities](https://github.com/material-foundation/material-color-utilities) | Apache-2.0 | 封面取色、HCT、配色方案（C++ 版本） | 阶段 1 引入固定提交；需去掉 `wsmeans.cc` 对 Abseil 的依赖，并对照 TS/Java 版补 2025 规范差异 |
| [m3shapes](https://github.com/soramanew/m3shapes) | Apache-2.0 | MD3E 形状与变形（androidx graphics-shapes 的 Qt QML 移植） | 阶段 1 评估。项目较新（16 star，最近提交 2026-09-17），Caelestia 已在使用 |
| [Material Symbols](https://github.com/google/material-design-icons) | Apache-2.0 | 图标 | 已引入子集，见 `ui/theme/fonts/README.md` |
| [Google Sans Flex](https://github.com/google/fonts/tree/main/ofl/googlesansflex) | OFL-1.1 | 拉丁字体 | 已引入 4.005 原文件 |

## 2. MD3／MD3E 视觉参考（Linux 社区）

| 项目 | 许可 | 技术 | 借鉴内容 | 代码 |
| --- | --- | --- | --- | --- |
| [illogical-impulse（end-4/dots-hyprland）](https://github.com/end-4/dots-hyprland) | GPL-3.0 | Quickshell（QML） | MD3 配色在深色桌面上的用法、表面层级与圆角、侧边栏和媒体控件的密度、动效手感 | 不复制 |
| [Caelestia shell](https://github.com/caelestia-dots/shell) | GPL-3.0 | Quickshell（QML）＋ m3shapes | MD3E 形状变形（加载指示器、媒体面板背景形状）、弹簧动效的 QML 写法、仪表盘中的媒体卡片 | 不复制；其依赖的 m3shapes 是 Apache-2.0，可单独评估 |
| [Noctalia](https://github.com/noctalia-dev/noctalia) | MIT | 早期为 Quickshell（QML），**现已改为 C++，不依赖 Qt/GTK** | 只按 MD3 命名组织的受限配色集合、从壁纸生成配色的流程 | 已不是 QML，代码参考价值低 |
| [Quickshell](https://github.com/quickshell-mirror/quickshell) | LGPL-3.0 | C++/QtQuick | 不作为依赖；阅读上述项目时用于理解其 QML 运行环境 | 不使用 |

这些都是桌面外壳而不是应用：借鉴配色、密度、组件观感和动效，不借鉴信息结构。

## 3. 应用信息结构参考（MD3 音乐客户端）

| 项目 | 许可 | 技术 | 借鉴内容 | 代码 |
| --- | --- | --- | --- | --- |
| [Metrolist](https://github.com/MetrolistGroup/Metrolist) | GPL-3.0 | Kotlin／Compose | 首页推荐分区、音乐库分类、迷你播放条与全屏播放器的切换 | 不复制 |
| [InnerTune](https://github.com/z-huang/InnerTune) | GPL-3.0 | Kotlin／Compose | 同上；Metrolist 的上游之一，最近提交 2025-11 | 不复制 |
| [Gramophone](https://github.com/FoedusProgramme/Gramophone) | GPL-3.0 | Kotlin／Material Components | 严格遵循 MD3 的列表、详情页与设置页 | 不复制 |

这些是手机应用。桌面端主要借鉴页面内容的组织方式，导航改为 NavigationRail，并利用更宽的窗口（多栏、辅助面板）。

## 4. 播放页与歌词参考（Apple Music 风格）

| 项目 | 许可 | 技术 | 借鉴内容 | 代码 |
| --- | --- | --- | --- | --- |
| Apple Music（macOS／iOS） | 专有 | — | 整体体验：大封面、流动模糊背景、逐字歌词、按距离淡化、控制区布局 | 只参考体验，不使用任何素材 |
| [AMLL](https://github.com/amll-dev/applemusic-like-lyrics) | AGPL-3.0 | TypeScript（React／Vue） | 逐字高亮、行间弹簧滚动、按距离模糊与缩放、背景渲染 | 不复制 |
| [YesPlayMusic](https://github.com/qier222/YesPlayMusic) | 主分支 MIT（v2 分支 AGPL-3.0） | Vue／Electron | 网易云客户端的信息结构（发现、音乐库、每日推荐、私人 FM）、Apple Music 风格歌词页 | 技术栈不同，主要借鉴设计；如借用主分支代码须保留 MIT 声明 |
| [SPlayer](https://github.com/SPlayer-Dev/SPlayer) | AGPL-3.0 | TypeScript | 网易云客户端的歌词与播放页细节 | 不复制 |

## 5. 规范与资料

- [Material Design 3](https://m3.material.io/)：配色角色、字体层级（15 个基础 ＋ 15 个强调样式）、形状与圆角刻度、动效、组件规格、窗口尺寸分级。
- androidx Compose material3 的 `MotionScheme` 实现：弹簧参数来源（见 `README.md` §4.4）。
- [Qt 6.9：扩展客户区与安全区](https://www.qt.io/blog/expanded-client-areas-and-safe-areas-in-qt-6.9)：标题栏方案 B 的依据。
- Qt 文档：`font.variableAxes`（6.7 起）、`MultiEffect`、`SpringAnimation`。

## 6. 阶段 0 发现的实现注意事项

- QML 会把 `onXxx` 形式的名字当作信号处理器，MD3 的 `onSurface`、`onPrimary` 等颜色名不能直接作为 QML 中声明并赋值的属性名。阶段 1 的主题 API 需要规避（例如在 C++ 中暴露、只读访问，或统一改名）。
- 图标名 `delete` 等是 ECMAScript 保留字，`Symbols` 中改为 `delete_`（生成脚本自动处理）。
