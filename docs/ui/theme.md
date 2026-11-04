# 主题系统（阶段 1）

> 状态：已实现并在 Linux 上验证（2026-10-19）。代码在 `ui/theme/`，Gallery“主题”页可交互检查。配色采用 MD3 2025 规范（§3）。

## 1. QML 用法

```qml
import Cirrus.Theme

Rectangle {
    color: Theme.color.surfaceContainer          // 49 个 MD3 颜色角色
    radius: Theme.shape.large                     // 圆角刻度
    Text {
        color: Theme.color.onSurface
        font: Theme.type.titleMedium.font         // 30 个字体样式
        lineHeight: Theme.type.titleMedium.lineHeight
        lineHeightMode: Text.FixedHeight
    }
}

Spring { id: slide; target: open ? 240 : 0; spec: Theme.motion.defaultSpatial }
Item { x: slide.value }

Behavior on opacity {
    NumberAnimation {
        duration: Theme.motion.defaultEffects.duration
        easing: Theme.motion.defaultEffects.easing
    }
}
```

| 入口 | 内容 |
| --- | --- |
| `Theme.seedSource` | `Theme.SeedSource.System`（默认，系统强调色）／`Cover`（可选）／`Fixed` |
| `Theme.systemAccent` | 系统强调色（平台不提供时无效，此时 System 退回固定种子） |
| `Theme.cover` | 当前封面 URL（`qrc:`／`file:`），设为空则不取色；只有种子来源为 Cover 时影响外壳配色 |
| `Theme.specVersion` | `Theme.SpecVersion.Spec2025`（默认）／`Spec2021` |
| `Theme.fixedSeed`、`Theme.seed` | 固定种子色（默认 MD3 基准色 `#6750a4`）；实际使用的种子色（只读） |
| `Theme.seedPresets` | 设置页色板提供的 8 个固定种子色（色环上分散的色相，第一个是基准紫） |
| `Theme.staticPalette` | Material 静态色板（`md.ref.palette`，不随种子变）：`blue`、`blueVariant`、`cyan`、`green`、`yellow`、`orange`、`red`、`pink`、`purple`、`grey`，每个是 `{container, onContainer}`（色调 90／暗色 80，与 30）。只用于按类别着色的图标，如设置索引，与 Android 16 设置首页一致 |
| `Theme.mode`、`Theme.dark` | `Theme.Mode.System`（默认）／`Light`／`Dark`；当前是否暗色（只读） |
| `Theme.variant`、`Theme.contrast` | 方案变体（默认 TonalSpot）；对比度 -1…1 |
| `Theme.coverPalette` | `valid`、`seed`、`accents`（评分后最多 4 个）、`dominant`（按占比最多 5 个，供播放页背景） |
| `Theme.extracting` | 是否正在取色 |
| `Theme.color` | 53 个颜色角色（含 2025 规范新增的 primaryDim、secondaryDim、tertiaryDim、errorDim）；`Theme.color.roles` 为角色名列表 |
| `Theme.type`、`Theme.shape`、`Theme.state`、`Theme.elevation` | 字体层级、圆角、状态层与禁用透明度、高度 |
| `Theme.motion` | 9 个弹簧令牌、`reduced`（减少动态效果）、`curve()`（采样曲线） |
| `Theme.contrastRatio(a, b)` | WCAG 对比度 |
| `Theme.mix(a, b, t)` | 预乘 RGBA 线性混合；控件状态过渡用（见 [controls.md](controls.md) §2） |
| `Theme.motion.custom(ζ, k, spatial)` | 令牌之外的自定义弹簧 |
| `Theme.motion.segment(p, a, b)`、`lerp(a, b, p)` | 共享进度的区间映射（截断，用于透明度、颜色）与线性插值（不截断，用于几何），见 [motion-choreography.md §1.2](motion-choreography.md) |
| `Theme.schemeFor(seed, dark)` | 不改变当前主题，计算任意种子的完整配色（例如播放页需要的深色方案） |

枚举是限定作用域的（`Theme.Mode.Dark`），因为 `Mode` 和 `SeedSource` 都有 `System`。

**命名**：MD3 角色名 `onPrimary` 等由 C++ 只读属性提供，在 QML 中以 `Theme.color.onPrimary` 读取没有问题，qmllint 也无警告。冲突只发生在 QML 里**声明并赋值**名为 `onXxx` 的属性时，因此控件和页面不要自己声明这类属性。

## 2. 颜色

**流程**

```text
Theme.cover 变化 → 递增编号 → 私有单线程线程池：
  QImageReader 按 128 px 缩小解码 → Celebi 量化 128 色 → MD3 评分
→ 回到 GUI 线程，编号不是最新则丢弃
→ 种子色 → 生成当前明暗的方案 → 49 个角色在 CAM16-UCS 中混合过渡
```

- 过渡使用 `defaultEffects` 弹簧（临界阻尼，约 231 ms）；减少动态效果时立即切换。过渡中再次切换时从屏幕上当前的颜色继续。
- 取色耗时（Release，示例封面 640×640 JPEG）：每张 1–7 ms。
- **近灰种子**：MD3 评分在封面几乎无彩色时会退回 Google 蓝；Cirrus 改为不过滤重新评分，并在种子色 HCT 色度 < 5 时强制使用 Monochrome 变体。修正前灰色封面会得到青色主题。
- **种子来源 System（默认）**：Qt 6.11 在 Linux 上不读取 xdg-desktop-portal 的强调色（无平台主题时总是返回默认蓝 `#308cc6`），使用 portal 平台主题也只读明暗。因此 `SystemAppearance` 通过 D-Bus 读取 `org.freedesktop.appearance` 的 `accent-color` 与 `color-scheme`，并监听 `SettingChanged`；GNOME、KDE、niri 等实现了该接口。在 niri 会话中验证：强调色 `#d56199`、暗色偏好均正确读取。Windows/macOS 使用 `QPalette::Accent` 与 `QStyleHints::colorScheme`（未验证）。“跟随系统”的明暗同样优先使用 portal 的值。
- **种子来源 Cover（可选）**：外壳配色跟随当前封面。播放页的背景与文字颜色另行从封面取色，不使用 MD3 配色（阶段 4 设计），`Theme.coverPalette.dominant` 为其准备。
- 已知现象：`neon` 封面（大面积近黑紫色背景加细亮线）的种子是暗紫色 `#130c21`，这是 MD3 评分偏向占比大的颜色的结果，主题偏暗紫而不是亮粉／青。暂不干预。

**对比度**：12 张示例封面、亮暗两种模式下，Gallery 检查的 12 组文字／背景组合均 ≥ 6.1（正文要求 4.5）。

## 3. 配色规范：2025 版（C++ 移植）

material-color-utilities 的 C++ 实现只有 2021 规范，MD3 Expressive 使用的 2025 规范只在 TS/Java 中。按维护者决定，已把同一上游提交的 TS 实现移植到 C++（`third_party/material-color-utilities/cirrus/`，说明见该目录 README）：

- 范围：2021 与 2025 规范的调色板、全部颜色定义、色调计算（含 2025 的色度倍率、新的色调差约束、黄色 99 色调规则），以及 TemperatureCache、DislikeAnalyzer。不含 2026 规范与 CMF 变体。
- 与 TS 相同，2025 规范只作用于 Neutral、TonalSpot、Expressive、Vibrant，其余 5 个变体（Content、Fidelity、Monochrome、Rainbow、FruitSalad）回退 2021。
- **验证**：164 个种子 × 9 个变体 × 明暗 × 5 档对比度 × 2 个规范，共 1,649,840 个颜色与同一提交编译的 TS 逐一比较，84 个（0.005%）不同，均为单个通道相差 1 的浮点舍入。
- 性能：Release 下一套 55 个角色约 0.8 ms（加入每方案色调缓存前约 3.2 ms）。
- `Theme.specVersion` 可切回 2021，用于对比。

移植前的对比（C++ 2021 对 TS 2025，12 张示例封面）：TonalSpot 中位 ΔE 15–24，Expressive 74–83；2021 版 Expressive 会把主色色相转离种子色（橙色种子得到蓝色 primary），2025 版保持种子色相。默认变体仍为 TonalSpot（UQ-04 可在原型中再比较 Expressive）。

## 4. 字体层级

30 个样式的字号、行高、字重、字距取自 androidx compose material3 `TypeScaleTokens`（该文件最后提交 2026-08-05）。强调样式与基础样式同字号同行高，字重提高（Display/Headline/Title Large/Body 为 500，Title Medium/Small 与 Label 为 700），部分字距不同。字体由 `Fonts.font(weight, size)` 生成（默认 MiSans，可选系统或自定义字体，见 README §4.2）：MiSans 的字重经它映射到可变轴，所以 QML 里需要特定字重时用 `Fonts.font()`，不写 `font.weight`。字体切换时 `Theme.type` 重建并通知。MD3 的 sp 按 1:1 映射为逻辑像素。

## 5. 圆角、状态层、高度

- 圆角：none 0、extraSmall 4、small 8、medium 12、large 16、largeIncreased 20、extraLarge 28、extraLargeIncreased 32、extraExtraLarge 48、full（9999，由 Rectangle 自动限制为半高）。与 m3.material.io 一致。
- 状态层：hover 0.08、focus 0.10、pressed 0.10、dragged 0.16；禁用内容 0.38、禁用容器 0.12。
- 高度：level0–5 为 0、1、3、6、8、12 dp。阴影用 `RectangularShadow` 绘制（`Cirrus.Controls` 的 `Elevation`），软件渲染下不画，见 [controls.md](controls.md) §2。

## 6. 动效

**令牌**（阻尼比／刚度，来自 androidx material3 `MotionScheme`；时长为单位阶跃稳定到 0.1% 以内的时间）：

| 令牌 | 参数 | 时长 |
| --- | --- | --- |
| `fastSpatial` | 0.6 / 800 | 360 ms |
| `defaultSpatial` | 0.8 / 380 | 435 ms |
| `slowSpatial` | 0.8 / 200 | 599 ms |
| `standardFastSpatial` | 0.9 / 1400 | 224 ms |
| `standardDefaultSpatial` | 0.9 / 700 | 317 ms |
| `standardSlowSpatial` | 0.9 / 300 | 484 ms |
| `fastEffects` | 1.0 / 3800 | 150 ms |
| `defaultEffects` | 1.0 / 1600 | 231 ms |
| `slowEffects` | 1.0 / 800 | 327 ms |

不带前缀的三个空间令牌是 Expressive 方案；`standard*` 是标准方案，供列表、导航等日常动作选择。

**Qt `SpringAnimation` 实验**：读 Qt 6.11.2 源码（`qquickspringanimation.cpp`），它每 16 ms 做一步显式欧拉积分（`v += spring·Δ − damping·v`，`x += v·0.016`），并把 `damping` 限制在 ≤ 1，帧率最高 62.5 Hz。按 `spring = k·0.016`、`damping = 2ζ√k·0.016` 换算后与解析解比较：

| 令牌 | damping 是否被截断 | 最大位置误差（占行程） | 过冲：解析 → Qt |
| --- | --- | --- | --- |
| fastSpatial | 否 | 18.9% | 9.5% → 4.2% |
| defaultSpatial | 否 | 11.4% | 1.5% → 0 |
| slowSpatial | 否 | 8.2% | 1.5% → 0.2% |
| standardFastSpatial | 是 | 23.4% | 0.2% → 0 |
| standardDefaultSpatial | 否 | 15.2% | 0.2% → 0 |
| standardSlowSpatial | 否 | 9.6% | 0.2% → 0 |
| fastEffects | 是 | 71.4% | 0 → 0 |
| defaultEffects | 是 | 28.5% | 0 → 0 |
| slowEffects | 否 | 15.8% | 0 → 0 |

结论：`SpringAnimation` 无法还原 MD3 弹簧，在高刷新率显示器上也只有 62.5 Hz 更新。因此：

- **`Spring` 元素**（C++，`ui/theme/spring.*`）：每帧按解析解计算（欠阻尼／临界／过阻尼三种闭式解），由 Qt 动画时钟驱动、跟随显示刷新；改目标时以当前位置和速度为初值继续。空间弹簧在减少动态效果时直接跳到目标，效果弹簧照常运行。
- **缓动预设**：每个令牌提供 `duration` 和对应的自定义 `QEasingCurve`，可以直接用于 `NumberAnimation`、`Transition`，适合不会被打断的动画。自定义参数的 `SpringSpec` 没有对应缓动（回退为 OutCubic），应使用 `Spring`。

**验证**（Release，离屏）：`defaultSpatial` 从 0 到 100 px，`Spring` 与解析解的最大偏差 0.5 px（包含计时偏差）；150 ms 时改目标，改目标前后速度均为 336 px/s，之后按目标稳定在 -50。Gallery 中三条对照轨道（Spring／弹簧缓动／换算后的 SpringAnimation）可人工观察差异。

## 7. 形状变形：m3shapes 评估（UQ-08）

- [m3shapes](https://github.com/soramanew/m3shapes) 提交 `8a6fe89`（2026-09-18），Apache-2.0，约 3800 行 C++，移植自 androidx graphics-shapes；提供 `MaterialShape` QML 类型（35 种预设形状、`fromShape`／`toShape`／`morphProgress`、自定义多边形），用自带的 QSGMaterial 和解析抗锯齿着色器绘制。
- 用 Qt 6.11.2 以其自带的严格警告选项构建，0 警告；离屏 RHI（OpenGL）渲染圆、Cookie、Burst、Pill、Sunny、Flower 及半程变形，边缘平滑。
- `morphProgress` 可写，可以由 `Spring` 驱动，与主题动效一致。
- 引入需要自写 CMake：上游是独立工程，构建为共享库，带安装规则和仅 GCC/Clang 可用的全局编译选项，不能直接 `add_subdirectory`。
- 结论：阶段 2 已以固定提交引入（`third_party/m3shapes`），用于 `MorphShape` 和 `LoadingIndicator`。按钮的圆角变形只需 `Rectangle` 的四角半径，不经过 m3shapes。

## 8. 截图与渲染后端

`QT_QPA_PLATFORM=offscreen` 默认使用 **software** 渲染后端：文字、Canvas、普通图形能正确截图，但自定义着色器（m3shapes、以后播放页的背景）不会绘制。需要检查着色器效果时加 `QT_QUICK_BACKEND=rhi QSG_RHI_BACKEND=opengl`。离屏下 Vulkan 无法初始化。

## 9. 待办

- 默认变体在原型中再比较（UQ-04）；2026 规范暂不移植。
- 系统“减少动态效果”设置的读取：目前只有应用内开关（`Theme.motion.reduced`）。
- `Theme.cover` 目前只接受 `qrc:` 与 `file:`；阶段 5 接入真实封面时改为经图片提供器的不透明 key。
- 系统强调色在 GNOME/KDE 会话及 Windows/macOS 上的表现（已在 niri 验证）。
