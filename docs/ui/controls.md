# MD3E 控件（阶段 2）

> 状态：第一批已实现并在 Linux 上验证（2026-10-25）。代码在 `ui/controls/`（QML 模块 `Cirrus.Controls`），Gallery 的“按钮”“选择与输入”“进度”“列表与菜单”页可交互检查。

## 1. 决定

| 事项 | 决定 |
| --- | --- |
| 命名 | 不加前缀：`Button`、`Switch`、`Slider`……业务 QML 只导入 `Cirrus.Controls`，**不得**同时导入 `QtQuick.Controls`（同名类型会冲突）。需要 Qt 的非可视工具（如互斥组）时用 `import QtQuick.Templates as T`，写 `T.ButtonGroup`。 |
| 实现基础 | 每个控件继承 `QtQuick.Templates` 的同名模板，只画外观；键盘、焦点、输入法、读屏角色、自动重复、拖动阈值等行为由 Qt 提供。不注册全局 QQuickStyle。 |
| 规格来源 | androidx compose material3 的生成令牌（`tokens/*Tokens.kt`）与 `*Defaults` 实现（2026-10 主分支）。令牌与实现不一致时以实现为准（例如文字按钮的文字色：令牌写 OnSurfaceVariant，实现带 TODO 使用 Primary）。 |
| 共享枚举 | C++ 命名空间 `Md`（`ui/controls/md.h`）：`Md.Filled/Tonal/Outlined/Text/Elevated/Standard`（样式）、`Md.XSmall…XLarge`（尺寸）、`Md.Round/Square`（形状）、`Md.Narrow/Default/Wide`（图标按钮宽度）、`Md.Alone/First/Middle/Last`（在连接按钮组中的位置）。 |
| 自动化测试（UQ-07） | 不写 Qt Quick Test；以 Gallery 人工检查（交互 + 离屏截图）为准。 |
| 形状变形（UQ-08） | 引入 m3shapes（`third_party/m3shapes`）。软件渲染后端下 m3shapes 不绘制，`MorphShape` 与 `LoadingIndicator` 自动退回圆角矩形／旋转圆弧，功能不受影响。 |
| 密度 | 采用 MD3 标准尺寸（小号按钮 40 dp）。Compose 的“精确指针”紧凑尺寸（36 dp）暂不做，阶段 3 外壳布局时再评估。 |

## 2. 共享基础

| 组件 | 作用 |
| --- | --- |
| `StateLayer` | 悬停 8%／焦点 10% 的前景色叠加（`fastEffects` 弹簧过渡），外加按下涟漪。涟漪是着色器：圆角矩形 SDF（四角半径独立）裁剪的扩散圆，225 ms 扩散、松开后 150 ms 淡出。软件渲染下没有涟漪，改为按下 10% 叠加。 |
| `FocusRing` | 键盘焦点指示：3 px、Secondary 色、外扩 2 px，四角半径随容器变化。只在 `visualFocus`（键盘导致的焦点）时显示。 |
| `Elevation` | 阴影，`RectangularShadow`（Qt 6.9+）。只用于 Elevated 按钮、菜单、浮层。软件渲染下不画阴影，靠色调层级区分。 |
| `MorphShape` | 包装 m3shapes 的 `MaterialShape`：35 种预设形状、`morphProgress` 由 `Spring` 驱动；软件渲染退回圆角矩形。 |
| `Theme.mix(a, b, t)` | 预乘 RGBA 线性混合。控件的状态颜色过渡用“弹簧驱动的 0–1 进度 + mix”，而不是 `Behavior on color`：后者在主题配色逐帧混合时会反复重启，颜色跟不上主题。 |
| `Theme.motion.custom(ζ, k, spatial)` | 自定义弹簧（如加载指示器的 0.6／200）。 |

**图标渲染（`Icon`）**：必须用 `Text.NativeRendering` 加 `Font.PreferNoHinting`，不能改回默认写法。原因（2026-10-27 排查，见 theme.md §8 的截图方法）：

- Material Symbols 的 FILL 轴是这样实现的：每个镂空处有一条“填充轮廓”，FILL=0 时缩成一个点，FILL=1 时长成与镂空重合、方向相反的轮廓，靠非零环绕规则把镂空填上。字形里因此有**重叠、重合的轮廓**。
- `Text.QtRendering`（距离场，Qt Quick 默认）处理不了重叠轮廓：FILL=1 时出现不规则的空洞，细笔画在小字号下发虚、变细。
- `Text.CurveRendering` 和 `Shape` 的 CurveRenderer 在两条重合轮廓的边上会留下细缝。
- `NativeRendering`（Linux 上是 FreeType）本身没问题，但开着字体微调（hinting）时，镂空轮廓和填充轮廓会被分别吸附到像素网格上、不再重合，某些字号和字重下就出现浅色细线。关掉微调后完全正确。
- FILL 量化到 0.05 一档：每个不同的可变轴取值都会生成一份字形缓存，动画时不量化会无谓增加缓存。
- 代价：原生渲染的字形在 `scale` 变换下是位图缩放，缩放动画期间会略模糊；Windows（DirectWrite）、macOS（CoreText）上的效果未验证。

**状态到颜色**：禁用时容器为 OnSurface 12%、内容为 OnSurface 38%；选中（切换）状态用 `defaultEffects` 弹簧在两套颜色之间混合。

## 3. 按钮

**Button**（`variant`、`size`、`shape`、`glyph`、`checkable`）

| 尺寸 | 高 | 左右内距 | 图标 | 图标间距 | 文字样式 | 方形圆角 | 按下圆角 | 描边 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| XSmall | 32 | 12 | 20 | 4 | labelLarge | 12 | 8 | 1 |
| Small（默认） | 40 | 16 | 20 | 8 | labelLarge | 12 | 8 | 1 |
| Medium | 56 | 24 | 24 | 8 | titleMedium | 16 | 12 | 1 |
| Large | 96 | 48 | 32 | 12 | headlineSmall | 28 | 16 | 2 |
| XLarge | 136 | 64 | 40 | 16 | headlineLarge | 28 | 16 | 3 |

| 样式 | 容器／内容 | 选中（checkable）容器／内容 |
| --- | --- | --- |
| Filled | Primary／OnPrimary | 未选中 SurfaceContainer／OnSurfaceVariant，选中 Primary／OnPrimary |
| Tonal | SecondaryContainer／OnSecondaryContainer | 选中 Secondary／OnSecondary |
| Outlined | 透明＋OutlineVariant 描边／OnSurfaceVariant | 选中 InverseSurface／InverseOnSurface（无描边） |
| Text | 透明／Primary | 不支持切换 |
| Elevated | SurfaceContainerLow＋1 级阴影（悬停 2 级）／Primary | 选中 Primary／OnPrimary |

- **形状变形**：圆形按钮按下时圆角收到“按下圆角”，用 `defaultEffects`（不回弹）；可切换按钮按下用 `fastSpatial`（带回弹），选中后圆形变方形、方形变圆形。
- `checkable` 即 MD3E 的“切换按钮”，不另设类型。

**IconButton**（`variant`：Standard／Filled／Tonal／Outlined；`size`；`widthMode`：Narrow／Default／Wide；`shape`；`glyph`；`checkable`）

| 尺寸 | 高 | 图标 | 左右内距 窄／默认／宽 | 方形圆角 | 按下圆角 | 选中后圆角（圆→方） |
| --- | --- | --- | --- | --- | --- | --- |
| XSmall | 32 | 20 | 4／6／10 | 12 | 8 | 12 |
| Small（默认） | 40 | 24 | 4／8／14 | 12 | 8 | 12 |
| Medium | 56 | 24 | 12／16／24 | 16 | 12 | 16 |
| Large | 96 | 32 | 16／32／48 | 28 | 16 | 28 |
| XLarge | 136 | 40 | 32／48／72 | 28 | 16 | 28 |

- Standard 未选中 OnSurfaceVariant、选中 Primary；Filled 未选中 SurfaceContainer、选中 Primary；Tonal 选中 Secondary；Outlined 选中 InverseSurface。
- 选中时图标的 FILL 轴从 0 过渡到 1（例如收藏的空心心形变实心）。
- `text` 必填：作为读屏名称和悬停提示，不显示。

**按钮组**

- `StandardButtonGroup`：按钮间距 12；按下的按钮宽度扩大 15%，相邻按钮让出空间（`fastSpatial`）。相邻按钮只让出两侧内距（每侧至少留 4，`shrinkLimit`），文字不会被省略；让不出的部分按下的按钮也不再扩大，组的总宽度不变。
- `ConnectedButtonGroup`：按钮间距 2，组两端为全圆角、内侧圆角 8（按下 4），选中的按钮变为全圆角。可设 `exclusive` 做单选（底层 `T.ButtonGroup`）。

## 4. 选择控件

- **Switch**：轨道 52×32、2 px 描边；未选中把手 16（有图标时 24）、选中 24、按下 28，把手位置用 `fastSpatial` 移动，尺寸用 `fastSpatial` 变化；可选把手图标（`showIcons`，选中 ✓、未选中 ✕）。悬停／按下时选中把手变为 PrimaryContainer。
- **CheckBox**：18×18、圆角 2、未选中 2 px OnSurfaceVariant 描边；勾选标记由路径绘制并在勾选时展开；支持三态和错误态（`error`）。状态层 40×40 圆形。
- **RadioButton**：外环 20、2 px；选中内点 10，弹簧缩放出现。状态层 40×40 圆形。

## 5. 输入

- **TextField**（`variant`：Filled／Outlined；`label`、`placeholderText`、`leadingGlyph`、`trailingGlyph`、`supportingText`、`error`）：高 56；Filled 容器 SurfaceContainerHighest、上圆角 4、底部指示线 1 px（聚焦 2 px Primary）；Outlined 描边 1 px Outline（聚焦 2 px Primary）、圆角 4。标签在有内容或聚焦时上浮缩小（bodyLarge → bodySmall，`fastSpatial`）。
- **SearchBar**：高 56、全圆角、SurfaceContainerHigh，左侧搜索图标、右侧清除按钮（有内容时出现，Esc 同样清空）。建议列表属于页面，由阶段 3 实现。

## 6. Slider

MD3E 样式：轨道高 16（`trackHeight` 可调为 24／40／56／96）、外圆角全圆、内侧圆角 2；把手为 4×44 的竖条，与两侧轨道各留 6 px 间隙，按下或聚焦时变为 2 px 宽；未激活轨道末端有 4 px 停止点。拖动时把手上方显示数值气泡（InverseSurface）。

## 7. 进度

- **ProgressBar**（线性）：4 px 粗、轨道与指示之间 4 px 间隙、末端 4 px 停止点（确定进度时）；不确定进度为两段交替滑动。
- **CircularProgress**：40×40、4 px、带间隙；不确定进度为旋转并伸缩的圆弧。
- **LoadingIndicator**：48 容器、38 指示形状，按 SoftBurst → Cookie9Sided → Pentagon → Pill → Sunny → Cookie4Sided → Oval 循环变形，每 650 ms 一次（弹簧 ζ 0.6、k 200），每次额外旋转 90°，并叠加 4666 ms 一圈的匀速旋转。`contained` 时加 PrimaryContainer 圆形容器。减少动态效果时只保留缓慢旋转。
- 波浪形（wavy）进度暂不做，播放器设计时再定。

## 8. 列表、菜单、提示、滚动条

- **ListItem**（继承 `T.ItemDelegate`）：一／二／三行高 56／72／88；`headline`、`supportingText`、`overline`、`trailingText`；`leading`／`trailing` 为任意项（图标、头像、封面、开关）。选中容器 SecondaryContainer。圆角随状态变化（静止 4、悬停 12、按下和选中 16），只在 `segmented`（分段列表）时启用；普通列表为直角。`first`／`last` 把列表首末项的外侧圆角至少设为 `outerRadius`（默认 16）：分段列表（项间距 2，容器色 SurfaceContainer）用它做出整组的外轮廓，放在圆角容器里的普通列表用它让状态层、涟漪和焦点环跟随容器圆角。歌曲列表用分段列表。
- **Menu / MenuItem / MenuSeparator**（MD3E 标准菜单）：容器 SurfaceContainerLow、圆角 16、2 级阴影、内边距 4；项高 44、圆角 4（首末项外侧 12）、选中 TertiaryContainer。打开时自锚点缩放展开（`fastSpatial`），关闭淡出（`fastEffects`）。
- **ToolTip**：InverseSurface、圆角 4、bodySmall、最小高 24、内距 8×4；悬停 500 ms 后出现；默认在控件上方，控件在窗口顶部 72 px 内时（顶栏）改在下方。
- **ScrollBar**：常态 4 px 细条，悬停或拖动时变为 8 px；颜色 OnSurfaceVariant，闲置 1 s 后淡出。

## 8a. 导航栏（第二批，阶段 3）

规格取自 androidx `WideNavigationRail` 与 `NavigationRail*Tokens`。

- **NavigationRail**：收起宽 96，展开宽 220–360（按最长的项）；内容距顶部 44，有 `header`（菜单按钮等）时 header 下方留 40；收起时项间距 4，展开时 0。容器 Surface（`restColor`）；`modal`（浮在内容上）时为 SurfaceContainer、右侧 large 圆角，不加阴影，遮罩由外壳画。`expanded` 由一个 0–1 进度弹簧驱动宽度和每一项的形态：推挤内容时用 `standardDefaultSpatial`，浮层用 `defaultSpatial`（见 [motion-choreography.md §5](motion-choreography.md)；androidx 两者分别用 DefaultSpatial、FastSpatial，这里按推挤布局不回弹的规则改用标准弹簧）。`currentIndex` 随点击更新，并发出 `activated(index)`。`footer` 放在底部（如账号、设置，按 `progress` 自行布局）；`expandedContent` 填在导航项和 footer 之间，只在展开时出现：按展开宽度布局、由导航栏裁剪、进度 0.5–1 淡入（外壳用它放歌单，见 [pages/shell.md](pages/shell.md)）。`minimumExpandedWidth` 默认 220；`topInset`（默认 44）是 header 或首项上方的距离；`restColor`（默认 Surface）是停靠时的底色。
- **NavigationRailItem**：收起时项高 64，指示条 56×32、距项顶 6，图标 24 在指示条中，标签 labelMedium 在指示条下方 4；展开时项高 56，指示条高 56、内边距 16／图标标签间距 8／16，标签 labelLarge 在图标右侧。左侧内距两种形态都是 20，所以图标的横向位置不变。过渡中顶部标签在进度 0–0.3 淡出、侧边标签在 0.6–1 淡入，指示条尺寸与图标纵向位置线性插值。
- 颜色：指示条 SecondaryContainer，选中图标 OnSecondaryContainer（并填充），选中标签收起时 Secondary、展开时 OnSecondaryContainer，未选中 OnSurfaceVariant；状态层与涟漪画在指示条形状上，颜色 OnSecondaryContainer。选中时指示条从中心展开（`fastSpatial`）。
- 读屏角色 PageTab；Tab 键在项之间移动，Space／Enter 选中。

## 8b. 标签页与纸片（第二批，阶段 3）

- **TabBar／TabButton**（MD3 主标签页）：项高 48，带图标 64（图标在标签上方）；标签 titleSmall，未选中 OnSurfaceVariant、选中 Primary（`defaultEffects` 过渡，图标同时填充）；状态层颜色选中为 Primary、否则 OnSurface。底部 1 px 分隔线 SurfaceContainerHighest；指示条 3 px、上方圆角 3、与标签同宽（至少 24），换标签时位置和宽度用 `fastSpatial` 滑动，首次布局不做动画。标签平分宽度，放不下时横向滚动。
- **Chip**：高 32、圆角 8（small），左右内距 16（有前置图标时左 8），图标 18、间距 8，标签 labelLarge。默认是建议／辅助纸片：OutlineVariant 1 px 描边，标签 OnSurface，图标 Primary。`checkable` 为筛选纸片：未选中标签 OnSurfaceVariant；选中填充 SecondaryContainer、去掉描边、前置对勾，内容 OnSecondaryContainer（`defaultEffects`）。`removable` 为输入纸片：标签 OnSurfaceVariant，左内距 12，标签后 18 px 删除图标（OnSurfaceVariant，到右边 8，自己的圆形状态层），点它发 `removeClicked`；`maximumLabelWidth` 让长标签省略。`highlighted` 显示焦点状态层，给键盘焦点留在别处、由列表代选的纸片用（搜索框的下拉列表）。`filter` 为选中状态由使用者设置的筛选纸片（点击不切换，单选行用）；`dropdown` 为带菜单的筛选纸片：尾部 18 px `arrow_drop_down`（颜色同标签），右内距 8，菜单由页面在点击时打开。
- **ChipGroup**：单选筛选纸片行（MD3 纸片组），间距 8，按顺序放下宽度装得下的纸片，其余隐藏（`shown`、`shownWidth`）。整行一个 Tab 停留点：进入时在选中的纸片上，←／→ 移动焦点，Space／Enter 选中；`activated(index)` 报告点选，`currentIndex` 由使用者设置（也可以从别处改）。

## 8c. 对话框（第二批，阶段 3）

- **Dialog**：MD3 基础对话框的外观，桌面的交互（维护者要求：MD3E 风格，但符合电脑的操作逻辑，尽量支持纯键盘操作）。基于 `T.Popup`（不用 `T.Dialog`，它自己排 header／footer，与这里的单列布局冲突），挂在窗口的 Overlay 上居中；宽 320–560（不超过窗口宽减 48），高不超过窗口高减 48。容器 SurfaceContainerHigh、extraLarge 圆角、3 级阴影，内边距 24；遮罩 Scrim 32 %。
  - 布局（一列，间距 16）：标题行（可选图标 `glyph` 24 Secondary、标题 `title` titleLarge、右端关闭按钮"关闭（Esc）"）→ 说明 `supportingText`（bodyMedium，OnSurfaceVariant）→ 子项 → 按钮行（右对齐，间距 8，与上方相隔 24）：`buttons` 里的额外按钮、`rejectText` 文字按钮、`acceptText` 填充按钮（默认按钮，最右）。
  - 键盘：Esc 等于取消（`rejected`）；Enter 在按钮上时点击该按钮（与 GTK、KDE 一致），否则执行默认按钮（`accepted`，`acceptEnabled` 为假时不执行）；Tab／Shift+Tab 只在对话框内循环。打开时焦点给对话框本身而不是某个控件（不出现焦点环，Esc／Enter 照常可用，按 Tab 才进入控件），设了 `initialFocusItem`（如输入框）时给它；关闭后焦点回到打开前的控件。
  - 鼠标：点击外面不关闭（桌面上误点不该丢掉对话框里的状态）。
  - 动效：进入时缩放 0.95→1（`standardFastSpatial`）并淡入，退出淡出（`fastEffects`）。Gallery 导航页有示例。

## 8d. 提示条（阶段 3）

- **Snackbar**：MD3 提示条，用于操作结果的简短反馈（写入失败等）。InverseSurface 底、InverseOnSurface 文字（bodyMedium），extraSmall 圆角，最低 48 高、最窄 288，放在所在区域底部居中、离底 16。`show(text)` 显示，`timeout`（默认 4 秒）后淡出；指针停在上面时不消失。没有操作按钮（以后需要"撤销"时再加）。外壳放一个在内容面板底部，页面通过 `shell.notify(text)` 使用。

## 9. 每个控件的检查项

全部交互状态（静止、悬停、焦点、按下、拖动、选中、禁用）· 键盘操作（Tab、Space／Enter、方向键）· 可见焦点环 · `Accessible.name` 与角色 · 文案 `qsTr` · 亮暗两套主题 · 减少动态效果 · 软件渲染降级。

## 10. 已知限制与待办

- 只做了水平 Slider；离散滑块的刻度点、范围滑块（RangeSlider）未做。
- 波浪形进度、Slider 的 MD3E 其余尺寸档（把手高度随轨道变化）未做。
- 连接按钮组未处理从右到左布局（RTL）下的首末端圆角。
- `ToolTip` 只有纯文本样式；富提示（rich tooltip）未做。
- TextField 未做字符计数和前后缀；Outlined 标签缺口用 `gapColor` 填色，放在非 surface 背景上时要手动指定。
- 列表项与菜单项的读屏名称只取标题文字；需要额外描述时由页面设置 `Accessible.description`。
- 第二批已做 NavigationRail（§8a）、Tabs 与 Chips（§8b）、Dialog（§8c）；其余（Snackbar、Card、Divider、Badge、下拉选择）按页面需要再做。次级标签页、带阴影的纸片未做；输入纸片已随搜索历史加入 `Chip`（`removable`）；下拉筛选纸片和单选纸片行（`ChipGroup`）随发现页加入。
- NavigationRail 未做：方向键在项之间移动、徽标（Badge）、项过多时滚动。

