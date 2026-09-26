# 07 · QML、设计系统与自定义控件

> 状态：工程规范提案，**不是最终 UI 设计稿**。配色、字体、图标、尺寸和动效曲线在设计阶段确认；本阶段确定可实施的结构与质量门槛。

## 1. 设计目标与原则

- 外观完全由 Cirrus 掌控，不沿用 Qt 默认样式作为成品。
- “重写控件”指重写视觉和组合体验，不重新发明焦点、键盘、点击、输入法及无障碍基础设施。
- 信息层级围绕歌曲、封面、歌词、播放控制和音乐库；不以装饰压过内容。
- 沉浸播放页参考 Apple Music 的大封面、歌词聚焦、空间和动效节奏，但保留 Cirrus 品牌，不复制界面素材或强行模拟 macOS 窗口按钮。
- Linux 原生行为、键盘可用性、性能和可读性优先于毛玻璃效果。

## 2. 实现路线

### 2.1 自有组件库，而非一开始接管全局样式

首版建立 QML 模块 `Cirrus.Controls`，导出 `CButton`、`CSlider` 等显式组件，以 `QtQuick.Templates as T` 的类型为基础，必要时基于 Basic 组合。业务页面只使用本模块的视觉契约。

这样可以逐个验收控件，不必在第一天实现所有 Qt 控件。未覆盖的内部基础控件必须明确限定用途；不能让同一产品页面混入两套视觉语言。

未来若需要完整 `QQuickStyle` 自定义 style，按 Qt 规则提供同名 Button.qml 等、Templates 根类型、qmldir 和 Basic fallback。不能从自己的 `QtQuick.Controls.Button` 递归继承。参见[官方依据](16-references.md)。

### 2.2 分层

```text
Tokens（语义变量）
    ↓
Primitives（文本、图标、表面、焦点环、分隔线）
    ↓
Controls（按钮、输入、滑块、菜单、对话框）
    ↓
Components（歌曲行、封面卡、歌词视图、播放控制组）
    ↓
Pages / AppShell（组合与导航）
```

Controls 不知道歌曲 ID、账号或 API；Components 接收数据和发出意图；Pages 绑定 ViewModel。Gallery 单独运行，可用 fake 数据，无需登录或网络。

## 3. Token 规范

单一来源建议为 `Cirrus.Controls` 中的 QML singleton；后续如采用设计工具导出，构建时生成而不是运行时散落解析 JSON。

| 分组 | 示例语义名 | 规则 |
| --- | --- | --- |
| 颜色 | surface/base、surface/elevated、text/primary、text/muted、accent、danger、focus | 业务 QML 不直接硬编码品牌色 |
| 字体 | body、label、title、display、lyric | 中文／拉丁回退明确；字体许可随包归档 |
| 间距 | space/xs/s/m/l/xl | 全局节奏一致，少量特殊值需注释用途 |
| 几何 | radius/control、radius/card、border/subtle | 不给所有元素无限圆角 |
| 动效 | duration/fast/normal/slow、easing/standard、motion/reduced | 页面不各自拼凑任意动画时长 |
| 尺寸 | control/compact/regular、hitTarget、icon/s/m | 视觉尺寸与命中区域可分开 |
| 层级 | overlay、dialog、toast、tooltip | 集中定义，避免随意 z=9999 |

主题模式为 Light/Dark/System；用户选择独立于系统当前状态，系统变化仅在 System 下跟随。封面提色只能影响有限装饰区域，不能破坏歌词／按钮对比度；提色异步并缓存，不每帧计算。

## 4. 控件交付清单

| 阶段 | 控件／基础设施 | 核心验收 |
| --- | --- | --- |
| M1 | CText、CIcon、CButton、CIconButton、CFocusRing、CSurface | 键盘触发、名称、禁用与焦点可见 |
| M1 | CTextField、CSearchField、CCheckBox、CSwitch、CRadioButton | IME、粘贴、选择、错误提示、读屏状态 |
| M1/M2 | CSlider、CProgressBar、CBusyIndicator、CScrollBar | 拖拽／键盘、缓冲状态、不定进度 |
| M2 | CMenu、CMenuItem、CToolTip、CDialog、CToast、CComboBox | 焦点捕获与归还、Esc、弹层定位、多屏 |
| M2 | CSongRow、CCoverImage、CEmptyState、CErrorState、CSkeleton | 占位、失败、选择、右键、触摸板 |
| M3 | CTabBar、CSidebarItem、CBreadcrumb、CPagination | 导航状态、历史恢复、可访问名称 |
| M4 | 下载行、批量选择条、设备选择器、快捷键编辑器 | 部分失败、长文本、受限平台降级 |

每个控件记录：用途、属性、信号、默认值、交互状态、键盘表、可访问性、尺寸约束、性能限制、Gallery 场景和 QML 测试。

## 5. 状态与交互契约

基础状态包括 default、hovered、pressed、focused、disabled；按控件再加 checked、loading、error、selected。状态不是互斥大枚举，组合时需明确优先级：禁用阻止操作，焦点环不因 selected 消失，错误不靠红色单独传达。

- 按钮使用 Templates 的 `clicked` 和键盘行为，不用 Rectangle+MouseArea 替代所有按钮。
- 图标按钮必须有可访问名称和必要 tooltip；tooltip 不代替名称。
- loading 按钮防止重复提交，但保持足够上下文让用户理解正在执行什么。
- 菜单打开后管理焦点，Esc 关闭，关闭后回原触发元素；危险操作不只依赖右键发现。
- 搜索框正确处理中文输入法组合文本，不能每输入一个拼音就立刻提交最终查询。
- 滑块对外分别发出 previewPosition 和 seekRequested；拖动时不被播放器 position 绑定拉回。
- 快捷键在文本编辑控件有焦点时尊重编辑语义，不把空格输入劫持为播放暂停。
- 列表支持单击选择、双击播放、Enter 播放、菜单操作；具体多选键行为与 Linux 习惯一致。

## 6. 信息架构与导航

以下是功能区域划分，不是像素布局：

| 区域 | 内容 |
| --- | --- |
| AppShell | 主导航、搜索入口、账号入口、页面容器、常驻迷你播放栏 |
| 发现 | 推荐、每日推荐、排行榜、私人 FM |
| 搜索 | 分类结果、建议和历史 |
| 音乐库 | 喜欢、歌单、收藏专辑／歌手、最近播放 |
| 资源管理 | 本地音乐、下载、云盘 |
| 详情 | 歌曲、歌单、专辑、歌手、相关评论 |
| 沉浸播放 | 封面、歌曲信息、主要控制、歌词、队列入口 |
| 设置 | 播放／音频、界面、文件、快捷键、隐私、关于；**无 API 地址设置** |

路由使用类型化参数，如 PlaylistRoute(playlistId)，不拼可执行 JS 字符串。返回时恢复滚动位置和筛选，网络请求不重复刷；页缓存设预算，不能让所有 Loader 永久 active。账号切换清理私有路由历史，防止返回键打开上一账号内容。

## 7. 沉浸播放页规格

- 迷你栏与沉浸页共享 PlayerViewModel；切换仅改变展示，不新建播放器，不重置队列。
- 宽屏可以封面／歌词双区，窄窗口转为单区切换；最终断点由原型验证，不把当前 640×480 模板当产品规范。
- 大封面用于信息和氛围；封面加载失败有稳定占位，页面不跳动。
- 当前歌词突出，前后行仍可读；翻译、音译、逐字高亮可独立开关。
- 手动滚动后进入 Browse 模式，暂停自动跟随；提供“返回当前歌词”，初始设计可在 5 s 无操作后恢复，需用户测试确认。
- 点击歌词只在源可 seek 时发出跳转意图；不可跳转时不伪装成功。
- 无歌词、纯音乐、解析失败、无网络、权限不足是不同状态，不都显示空白。
- 沉浸不意味着独占全屏；保持窗口模式、退出入口和键盘可用性。
- 动态背景使用限分辨率、有限模糊／缓存纹理；减少动态模式关闭大范围位移、模糊动画和逐字弹跳。

## 8. QML 性能规则

- 大列表使用 ListView/TableView 的虚拟化与可控 delegate，避免 Repeater 一次创建上万行。
- 模型增量通知；不在每次进度变化时 reset 整个列表。
- 图片指定 `sourceSize`，按显示尺寸解码；统一封面服务去重、限并发、回收缓存。
- 不在每一行开多层 ShaderEffect/阴影/blur；复杂视觉集中在当前播放页。
- 不在 GUI 执行同步文件访问、JSON 大解析、标签读取或昂贵 JS 循环。
- 不依赖 `QtGraphicalEffects` 的旧 Qt5 写法作为 Qt6 新工程方案；使用经过验证的 Qt6 能力并提供降级。
- QML 的热路径避免深层隐式依赖、绑定循环和频繁创建销毁；用 QML Profiler 验证，而不是凭动画观感判断。
- C++ 图像提供器返回经过限制的图片；私有图片引用使用不带 Cookie／签名 URL 的不透明 key。

## 9. 无障碍、缩放与本地化

- 使用 Qt Accessible 属性，测试角色、名称、状态、焦点顺序和 Orca 实际读出；不是加一个属性就算通过。
- 主要任务纯键盘完成；焦点可见，弹窗不泄露焦点到背景。
- 不以颜色作为唯一状态编码；歌曲不可播有文字原因，下载失败有动作。
- 测试长中文／英文标题、双向文字、缺字回退、emoji、200% 缩放以及屏幕间移动。
- 最小窗口尺寸在 M1 原型确定，建议以约 960×640 逻辑像素起测；更小窗口至少保持播放／暂停、导航和退出可达，不强裁内容。
- 翻译使用 qsTr/tr 与带参数占位；复数、日期和数值格式遵守 locale。
- 不依赖颜色渐变中的某一像素满足对比度；取最差区域验证。

## 10. 设计交付与评审门禁

正式视觉设计应交付：信息架构、关键用户流程、低保真原型、Tokens、控件所有状态、亮暗主题、核心页面和响应式规则、动效说明、可访问性注释及素材许可。

顺序：先 Gallery + 歌曲列表 + 播放栏的真实数据原型 → 再沉浸页视觉探索 → 再批量铺页面。视觉验收与业务验收分开，二者都通过才合并。截图基准仅在固定渲染环境比较，不能要求不同 GPU 的抗锯齿像素完全一致。
