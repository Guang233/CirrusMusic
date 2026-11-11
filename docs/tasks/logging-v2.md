# 日志与诊断改造（第二版）

- 日期：2026-11-11
- 状态：**方向已定**（维护者 2026-11-11，§8 的 LQ7–LQ10），实施中（§9）
- 基础：[logging.md](logging.md)（第一版：处理器、写文件线程、轮转、脱敏、`event`/`field`）。第一版的框架保留，本文只改**记什么**和**诊断包里有什么**；第一版里与本文冲突的地方以本文为准。
- 起因：维护者 2026-11-11 反馈，用户发来的诊断包没用，最常见的是**界面异常**。

## 1. 为什么没用（拿真实日志看）

样本：维护者本机 2026-11-11 的一次会话，1053 行，44 首歌，几次页面加载失败。

| # | 问题 | 证据 |
| --- | --- | --- |
| P1 | 出事时没有细节 | 详细日志默认关，用户出了问题才去开，那一次已经过去；而且全项目只有 11 处 `qCDebug`，开了也几乎不多出东西 |
| P2 | 信噪比低 | 511 行（49%）是 `api.done`；每首歌固定 13 行左右（`load.*`、`engine.*`、`audio.*`、`preload.*`）；每次上报 3 行；QML 的 `cover is unavailable` 在 `default` 分类连刷几十行 |
| P3 | 看不到用户做了什么、看到了什么 | 没有页面导航、对话框、弹出提示；用户说"刚才那个页面显示不对"，日志里找不到"刚才"和"那个页面" |
| P4 | 记录关联不上 | `model.failed model=cirrus::ui::ShelfModel kind=Network` 连续 4 条，分不出是哪个货架；`cover.failed id=884` 和 QML 的封面警告对不上；QML 警告不知道在哪个页面上发生 |
| P5 | 环境不够 | 网络失败全是 `ProxyConnectionRefusedError`，但会话头里没有代理设置；没有安装方式、桌面环境、屏幕和缩放、GPU、登录状态、相关设置 |
| P6 | 崩溃只有一行 `fatal signal 11` | 没有堆栈；Windows 连这一行也没有 |
| P7 | 诊断包只是把日志文件打成 zip | 没有摘要、没有用户的描述和发生时间，维护者要从几千行里自己找 |

**目标**：拿到一份诊断包，五分钟内能回答：用户在什么环境下、做了什么、看到了什么、当时程序内部发生了什么。

## 2. 飞行记录器（P1）

- `cirrus.*` 的 debug **一直开着**，但默认**不写文件**：处理器把 debug 记录放进内存环形缓冲（最近 2 MiB，约一万多行，正常使用约 10–20 分钟）。
- 遇到下面这些情况，把环里**还没写过的** debug 记录连同原时间戳一起写进文件，前后各加一行 `log.context begin|end reason=… count=N`：
  - 一条 warning 或 critical（任何分类）：只写这条之前 30 秒内的，最多 500 条；10 秒内多条 warning 合并成一次。
  - 用户点"记下刚才的问题"（§4.4）、导出诊断包：整个环。
- 文件里 info 及以上照常实时写；context 块里的 debug 行时间早于块前面的几行，查看工具（§7）按时间排回去。
- "详细日志"开关保留（24 小时），开着时 debug 也实时写文件，给需要长时间复现的情况用。
- 开销：被关掉的分类本来几乎零开销，现在 `cirrus.*` debug 要格式化一次再进环。约定 debug 不写在每帧、每个音频块这类高频路径上（§6），实测一首歌的播放过程。
- 补 debug 内容：网络（每个请求的完成、重发）、接口、播放各步骤、缓存、模型加载、导航细节——P2 降下来的那些 info 都变成 debug，进环。

## 3. 降噪（P2）

| 现在 | 改成 |
| --- | --- |
| 每个成功请求一条 info `api.done` | debug；每 5 分钟和退出时一条 info `api.stats`：请求数、失败数（按错误）、耗时 p50/p95/最大、最慢的接口 |
| 每首歌约 13 行 info | 每首一条 `load.start`、一条 `load.ready`（带解析、打开、首帧耗时，缓存命中），失败照旧 warning；其余降到 debug |
| 上报 3 行 | 一条 `report.result`（带结束方式、秒数、结果），`settled`/`send` 降到 debug |
| 空字段 `retry_after=""`、`status=0` | 没有值就不写（`log::field` 旁边加 `log::fieldIf`） |
| QML 的 `cover is unavailable` | 封面失败在后端已经记过一次；图片提供器对已知失败不再报错误字符串，改由 `CoverArt` 自己显示占位 |
| `qt.qpa.services`、`kf.config.core` 这类启动警告 | 保留（它们是环境信息），但只记一次 |

## 4. 用户视角（P3、P4，本轮重点）

### 4.1 导航轨迹

`AppShell.qml` 的导航入口（`go`、`visit`、`back`、`forward`、首页重置）统一记 info：

```
I main cirrus.ui.nav nav.push page=AlbumPage key=123456 depth=3 via=search
I main cirrus.ui.nav nav.back page=SearchPage depth=2
I main cirrus.ui.nav nav.section page=LibraryPage section=library
```

- `page` 是页面组件的文件名（不是标题）；`key` 是公开目录 ID（LQ2）；用户主页的 key 用 `log::account()` 散列。
- 播放页打开和关闭、迷你播放器、登录框、设置分组切换也记。

### 4.2 对话框和提示

- `Cirrus.Controls` 的 `Dialog` 打开和关闭时记 `ui.dialog opened|closed name=<objectName>`；产品里的对话框都设 `objectName`（没设的记文件名）。
- 弹出提示（snackbar）记**稳定编号**，不记文字（LQ8）：`ui.notice id=playlist.remove_failed source=PlaylistPage`。编号在发出提示的 C++ 里定义（`ui/presentation/notice.h`），一个编号对应一种文案；带歌单名之类参数的文案也只记编号。

### 4.3 界面上下文

- 日志库加一个全局上下文 `log::setContext(key, value)`（只接受短标识）。展示层把当前页面、播放页是否打开、当前对话框放进去。
- 来自 QML 和 Qt 的 warning（`default`、`qml`、`js`、`qt.*` 分类）自动带上当前上下文：`… TypeError: Cannot read property 'items' of null ctx.page=DiscoverPage ctx.dialog=-`。
- `LoadableModel` 的失败带上是哪一个：`model.failed model=ShelfModel name=newAlbums …`、`model=PlaylistPageModel key=…`。每个模型有一个 `logName`（类型名去掉命名空间）和可选的 `logKey`。

### 4.4 "记下刚才的问题"

- 入口：设置 → 诊断里一行按钮；快捷键 Ctrl+Alt+Shift+D（不会误按，也不和输入框冲突）。
- 做的事：
  1. 写一条 `user.mark n=<本次运行第几个>`；
  2. 写一份**界面状态快照**（LQ7：不截图）：`ui.state` 一条，内容有页面栈（每层的 page 和 key）、当前页面、窗口尺寸、是否最大化或全屏、屏幕尺寸、缩放比例、刷新率、主题模式、配色来源、字体来源、侧边栏是否展开、播放页是否打开和视图、焦点所在控件的类型和 objectName、正在显示的对话框；
  3. 把飞行记录器整个写进文件（`log.context reason=mark`）；
  4. 弹出提示"已记下这一刻，导出诊断包时会一起带上"，带"导出"按钮。
- 导出诊断包时也自动做第 2、3 步（`reason=export`），不加 `user.mark`。

## 5. 诊断包（P5、P7）

### 5.1 内容

```
cirrus-diagnostics-20261111-1403.zip
├── summary.txt        导出时生成，人读的摘要（下面）
├── description.txt    用户在导出框里写的描述和大概发生时间（可空，空就不放）
├── logs/cirrus-….log  勾选的日志文件
└── crash/…            堆栈（§5.3，若有）
```

`summary.txt`（所有值经过与日志相同的脱敏）：

- 应用：版本、提交、构建类型、安装方式（开发构建、AppImage、Flatpak、deb/rpm、Arch 包、Windows 安装版或便携版、macOS 包，按可执行文件位置和环境变量判断）。
- 系统：系统和内核、架构、桌面环境（`XDG_CURRENT_DESKTOP`、`XDG_SESSION_TYPE`）、QPA、渲染后端和 GPU 名称与驱动（`QRhi::driverInfo()`，首帧后记一次 `app.gpu`）、各屏幕的尺寸、缩放、刷新率、语言和时区。
- 网络：系统代理类型（无、HTTP、SOCKS，以及来自环境变量还是系统设置），不记主机和端口；在线状态。
- 音频：输出后端、设备是否默认设备、采样率和格式；不记设备名（可能带人名）。
- 账号：未登录、游客、已登录；不记任何 ID。
- 设置：`AppSettings` 里的每一项，排除搜索历史；自定义字体只记"有几个文件"、字体名照记。
- 存储：数据库、音频缓存、资源缓存的大小。
- 日志概况：包里每个会话的开始时间、时长、是否异常退出（信号）、warning 数（按事件名前十）、`user.mark` 的时间点列表。

### 5.2 导出框

- 新增两个输入：问题描述（多行）、大概什么时候（"刚才""今天早些时候""更早"，或具体时间）。描述原样放进 `description.txt`，不进日志。
- 默认勾选：本次运行、上一次运行、任何含 `user.mark` 或异常退出的会话；更早的不勾。
- 预览：`summary.txt` 可以展开看全文（11 §5 的"导出前能看到包含什么"）。

### 5.3 崩溃堆栈（LQ9）

- Linux 和 macOS：致命信号处理器里用 `backtrace()` + `backtrace_symbols_fd()`（启动时先调一次 `backtrace`，让它在信号处理器之外完成加载），写到预先打开的 `crash-<会话>.txt`，只有模块和偏移，不含内存内容。
- Windows：`SetUnhandledExceptionFilter`，`RtlCaptureStackBackTrace` 加模块基址算偏移，写到预先打开的文件。
- `std::set_terminate`：记未捕获异常的类型名。
- 信号处理器里不能脱敏，所以原始文件**不直接进诊断包**：下次启动时读出来、把路径换成占位，转成日志里的 `app.crash` 和 `app.crash_frame i=0 module=libCirrusUi.so offset=0x1a2b3c` 几行（warning），然后删掉原始文件。
- 会话头加 `build_id`（Linux 读自身 ELF 的 GNU build-id，Windows 用 PE 时间戳加大小，macOS 用 LC_UUID）。
- 还原：发布构建保留调试符号（CI 的 artifact，不进安装包），`tools/logs/symbolize.py` 用 `llvm-symbolizer` 或 `addr2line` 按 build_id 还原函数名和行号。

## 6. 关联 ID（P4，后做）

- 用户操作或模型加载开始时，展示层分配一个 `op` 号（进程内递增），随请求经 `MusicRequests` → Facade → 接口客户端 → 传输层，每层的日志都带 `op=`。
- 需要给后端的命令结构加一个可选字段，涉及面广，放在最后一步；在这之前先用 4.3 的 `logName`/`logKey` 和请求号把模型失败与请求对上。

## 7. 查看工具

`tools/logs/cirrus_log.py`（只用标准库）：

- `summary <zip|dir>`：会话列表、异常退出、标记、warning 排行。
- `timeline <zip|dir> [--around mark|HH:MM:SS] [--window 60s] [--cat ui]`：多个文件合并、按时间排序（含 context 块），在标记或指定时间前后截取。
- `symbolize`：见 5.3。

## 8. 待决问题

| # | 问题 | 决定 |
| --- | --- | --- |
| LQ7 | 诊断包附窗口截图（会带昵称、歌单名） | **不做**（维护者 2026-11-11）：用 4.4 的界面状态快照代替 |
| LQ8 | 弹出提示怎么记 | **记稳定编号**（2026-11-11），不记文字 |
| LQ9 | 崩溃记调用堆栈（改第一版 LQ6） | **记**（2026-11-11）：只记模块加偏移，下次启动脱敏后进日志，CI 留符号离线还原 |
| LQ10 | debug 怎么采集 | **飞行记录器**（2026-11-11）：§2 |

## 9. 实施步骤

按"界面异常最多"排序。每步完成后在 §10 记录。

| 步 | 内容 | 完成条件 |
| --- | --- | --- |
| V1 | 飞行记录器（§2）、`fieldIf`、`setContext` 与 warning 自动带上下文（§4.3）、降噪（§3） | `tst_logging` 覆盖环、context 块、上下文；离线放几首歌，info 行数降到原来的三分之一以下 |
| V2 | 导航轨迹、对话框、提示编号、模型 `logName`/`logKey`、"记下刚才的问题"和界面状态快照（§4） | 真实应用里走一遍导航、打开对话框、触发提示和标记，日志能还原过程 |
| V3 | 诊断包 v2：`summary.txt`、描述、默认勾选、预览（§5.1、5.2），`app.gpu` | 导出的 zip 内容与预览一致；summary 没有路径、主机名、ID |
| V4 | 查看工具（§7 前两项） | 用 V3 的包跑通 |
| V5 | 崩溃堆栈与还原（§5.3） | Linux 上故意崩溃一次，下次启动日志里有堆栈，脚本能还原到函数名 |
| V6 | 关联 ID（§6） | 一次页面加载从模型到传输层能按 `op` 串起来 |

## 10. 实施记录

（暂无）
