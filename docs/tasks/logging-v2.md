# 日志与诊断改造（第二版）

- 日期：2026-11-11
- 状态：**方向已定**（维护者 2026-11-11，§8 的 LQ7–LQ10）；V1–V5 已完成（§10）；维护者看过后要求错误能定位、环境更全、导出结构参考大型软件，见 §11、§12（2026-11-11 已实现）；V6 关联 ID 和 Windows 崩溃堆栈未做
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
- 遇到下面这些情况，把环里**还没写过的** debug 记录连同原时间戳一起写进文件，前后各加一行 `log.context_begin`／`log.context_end reason=… count=N`：
  - 一条 warning 或 critical（任何分类）：只写这条之前 30 秒内的，最多 500 条；每条记录只写一次，接连几条 warning 只带出它们之间的新记录。更早没写过的留在环里，等标记或导出时再写。
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

- 对话框和菜单打开时会拿到键盘焦点：`Diagnostics` 盯着窗口的焦点项，往上找到它所在的弹窗（弹窗内容项的 QObject 父对象就是弹窗本身），记 `ui.popup name=LoginDialog open=true|false`。名字取 `objectName`，没设时取 QML 类型名；不用改每个对话框。
- 弹出提示（snackbar）记**稳定编号**，不记文字（LQ8）：`ui.notice id=playlist.remove_failed source=PlaylistPage`。编号在发出提示的 C++ 里定义（`ui/presentation/notice.h`），一个编号对应一种文案；带歌单名之类参数的文案也只记编号。

### 4.3 界面上下文

- 日志库加一个全局上下文 `log::setContext(key, value)`（只接受短标识）。展示层放进 `page`（当前页面）、`popup`（当前弹窗）、`player`（播放页打开时为 `open`）。
- **每条** warning 都自动带上当前上下文：`… ctx.page=DiscoverPage ctx.popup=LoginDialog`。
- QML 引擎的警告不再直接打到终端：`Diagnostics::watch(QQmlEngine*)` 接过来记成 `cirrus.ui.qml qml.warning file=Cirrus/Components/CoverArt.qml line=121 msg=…`；后端已经记过的封面失败（`cover is unavailable`）只进飞行记录器。
- `LoadableModel` 的失败带上是哪一个：`model.failed model=ShelfModel ids=shelf:0 …`、`model=PlaylistPageModel ids=playlistKey:123`。`ids` 由反射自动取模型自己的属性，只取 `…Key`（`user…Key` 散列）和 `shelf`、`feed`、`date`、`tab`、`target`、`kind`、`category`，不用每个模型单独写。导航记录 `nav.page` 的 `ids` 用同样的规则，取自页面创建时的参数。

### 4.4 "记下刚才的问题"

- 入口：设置 → 诊断里一行按钮；快捷键 Ctrl+Alt+Shift+D（不会误按，也不和输入框冲突）。
- 做的事：
  1. 写一条 `user.mark n=<本次运行第几个>`（info，不触发 warning 的 30 秒截取，第 3 步写整个环）；
  2. 写一份**界面状态快照**（LQ7：不截图）：`ui.state` 一条，内容有页面栈（每层的 page 和 key）、当前页面、窗口尺寸、是否最大化或全屏、屏幕尺寸、缩放比例、刷新率、主题模式、配色来源、字体来源、侧边栏是否展开、播放页是否打开和视图、焦点所在控件的类型和 objectName、正在显示的对话框；
  3. 把飞行记录器整个写进文件（`log.context reason=mark`）；
  4. 弹出提示"已记下这一刻，导出诊断包时会一起带上"（Snackbar 还没有动作按钮，第一版不带"导出"）。
- 导出诊断包时也自动做第 2、3 步（`reason=export`），不加 `user.mark`。

## 5. 诊断包（P5、P7）

> 第一版的 `summary.txt` 已被 §12 的分文件结构取代，本节的内容分散到 §12 各文件。

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
- 系统：系统和内核、架构、桌面环境（`XDG_CURRENT_DESKTOP`、`XDG_SESSION_TYPE`）、QPA、渲染后端和 GPU（首帧时在渲染线程取，记一次 `app.gpu`；OpenGL 记 `GL_RENDERER`，其他图形接口只记接口名，`QRhi::driverInfo()` 要私有模块所以没用）、各屏幕的尺寸、缩放、刷新率、语言和 UTC 偏移（不记时区名）。
- 网络：系统代理类型（无、HTTP、SOCKS，以及来自环境变量还是系统设置），不记主机和端口；在线状态。
- 音频：第一版没做（展示层不链接 Multimedia）；设备变化在引擎的 `engine.device_changed` 里有。
- 账号：未登录、游客、已登录；不记任何 ID。
- 设置：`AppSettings` 里的每一项，排除搜索历史；自定义字体只记"有几个文件"、字体名照记。
- 存储：数据库、音频缓存、资源缓存的大小。
- 日志概况：包里每个会话的开始时间、时长、是否异常退出（信号）、warning 数（按事件名前十）、`user.mark` 的时间点列表。

### 5.2 导出框

- 新增一个多行输入："发生了什么、大概什么时候（可不填）"。原样放进 `description.txt`，不进日志（日志只记有没有写）。
- 默认勾选：本次运行、上一次运行、任何含 `user.mark` 或异常退出的会话；更早的不勾。
- 预览：`summary.txt` 可以展开看全文（11 §5 的"导出前能看到包含什么"）。

### 5.3 崩溃堆栈（LQ9）

- Linux 和 macOS：致命信号处理器里只用异步信号安全的调用（启动时先调一次 `backtrace`，让它在信号处理器之外完成加载）：`open` 出 `crash-<会话>.txt`，写信号号、各帧地址、`/proc/self/maps` 里的可执行段（macOS 没有，只有地址），最后才写 `backtrace_symbols_fd()` 能给出的导出符号名。不含内存内容。
- Windows：`SetUnhandledExceptionFilter`，`RtlCaptureStackBackTrace` 加模块基址算偏移。**还没做**：本机不能编译 Windows，先不写没验证过的代码；Windows 目前只有下次启动的"上次异常退出"。
- `std::set_terminate`：记未捕获异常的类型名。
- 信号处理器里不能脱敏，所以原始文件**不直接进诊断包**：下次启动时读出来、把路径换成占位，转成日志里的 `app.crash` 和 `app.crash_frame i=0 module=libCirrusUi.so offset=0x1a2b3c` 几行（warning），然后删掉原始文件。
- 会话头加 `build_id`（Linux 读自身 ELF 的 GNU build-id；Windows、macOS 还没做）。
- 还原：`tools/logs/cirrus_log.py symbolize <包> --search <同一构建的二进制目录>` 用 `addr2line` 还原函数名和行号。发布构建目前不留调试符号（`build-packages.sh` 安装时去掉）；CI 把带符号的二进制存成 artifact 还没做，要改 workflow，等维护者决定。

## 6. 关联 ID（P4，后做）

- 用户操作或模型加载开始时，展示层分配一个 `op` 号（进程内递增），随请求经 `MusicRequests` → Facade → 接口客户端 → 传输层，每层的日志都带 `op=`。
- 需要给后端的命令结构加一个可选字段，涉及面广，放在最后一步；在这之前先用 4.3 的 `logName`/`logKey` 和请求号把模型失败与请求对上。

## 7. 查看工具

`tools/logs/cirrus_log.py`（只用标准库）：

- `summary <zip|dir>`：会话列表、异常退出、标记、warning 排行。
- `timeline <zip|dir> [--around mark|HH:MM:SS] [--window 60s] [--cat ui]`：多个文件合并、按时间排序（含 context 块），在标记或指定时间前后截取。
- `symbolize <zip|dir> --search <dir>`：见 5.3。

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

**V1 飞行记录器与降噪（2026-11-11，`8df1dc9`）**
- `logger.cpp`：`cirrus.*` debug 常开；非详细模式下进 2 MiB 环，warning 时带出前 30 秒（最多 500 条），`log::writeContext(reason)` 写整个环；每条只写一次。`log::setContext`，每条 warning 带 `ctx.*`。
- `log::fieldIf`、`log::nameOf`、`log::Durations`（`src/logging/durations.*`）。
- 降噪：`api.done` → debug + 每 5 分钟和退出时 `api.stats`；`load.resolved`、`preload.*`、`load.ended`、`engine.load/ready/ended/next_opened/advanced`、`audio.open/prefetch/url_needed/cached`、`queue.begin/advance`、`report.settled/send` → debug；`load.ready` 带歌曲、音质、试听；`request.failed` 没有值的 `status`、`retry_after` 不写。
- QML 引擎警告改由 `Diagnostics::watch(QQmlEngine*)` 记（见 §4.3）。新分类 `cirrus.ui.qml`、`cirrus.ui.nav`。
- 验证：`tst_logging` 新增 `optionalFieldsAndDurations`、`flightRecorder`；`log_canary`、`session_api_client`、`http_transport`、`listening_reporter`、`playback` 通过（没跑全量，按改动范围选）。离屏启动一次：warning 前带出了 context 块。

**V2 用户视角（2026-11-11，`5bd375f`）**
- `App.diagnostics`：`pageShown`（`AppShell` 的 `PageStack.onCurrentItemChanged`）、`playerShown`（`Main.qml`）、`notice(id)`、`mark()`、`watch(QQuickWindow*)`（弹窗、焦点、窗口）、`typeName`、`ids`。
- 提示编号：`noticed(id, text)`（`ui/presentation/notice.h`），C++ 里 32 处发出点、56 个分支都包上了；QML 里 3 处用 `App.diagnostics.notice`。
- "记下刚才的问题"：设置 → 诊断第一行，和全局快捷键 Ctrl+Alt+Shift+D。
- 验证：离屏启动看到 `nav.page page=HomePage`、`model.failed model=ShelfModel ids=shelf:0`、warning 带 `ctx.page`；临时钩子触发标记，日志里依次是 `user.mark`、`ui.state`（页面栈、窗口、屏幕、缩放、主题、设置）、46 条 context 记录；钩子已撤。qmllint：改动前后都是 23 条，全是原有的 `id-shadows-member` 之类（Qt 6.12 的 qmllint 新规则），没有新增。弹窗记录没在离屏下实测（不能点），弹窗与父对象的关系用一个独立小程序验证过。

**V3 诊断包（2026-11-11，`4529e0d`）**
- `log::writeArchive(target, files, extras)`：日志放在 `logs/` 下，另加生成的文件。`Diagnostics::summary(names)`、`exportTo(target, names, description)`；`files` 多了 `suggested`；`app.gpu`。
- `ExportLogsDialog`：默认只勾建议的，描述输入框，"查看摘要"展开预览。
- 验证：`tst_logging` 的打包测试检查 `logs/` 和额外文件，Python `zipfile.testzip()` 通过；临时钩子导出一个包，`summary.txt`、`description.txt`、建议的 6 个文件都在，摘要里没有路径、主机、ID；对话框离屏截图正常。

**V4 查看工具（2026-11-11，`bbcdc98`）**：`tools/logs/cirrus_log.py summary|timeline`，在测试日志上试过 `--around mark`、`--level`、`--cat`。

**V5 崩溃堆栈（2026-11-11）**：`src/logging/crash.*`（信号处理器、`std::terminate` 记异常类型、下次启动的 `app.crash`/`app.crash_frame`、`build_id`），`cirrus_log.py symbolize`。**这部分代码被另一个同时进行的会话的提交 `a463a24`（feat(cache) …）一起带进去了**，提交信息对不上，没有改写历史。验证：`tst_logging::crashStack` 在子进程里 `raise(SIGSEGV)`，下次 `report()` 记出 16 帧、无路径、原始文件已删；`symbolize` 把第 4 帧还原到 `logging_test.cpp:254`。

**§11 错误定位、§12 诊断包结构（2026-11-11，`b5b9c41`、`76d3da4`）**
- 验证：`tst_logging`（新增 `log::message` 清洗、`crashes/` 文件）、`log_canary`（签名地址不进 `detail`）、`http_transport`、`session_api_client`、`cover_checks` 通过。离屏导出两次（另设 `XDG_RUNTIME_DIR`，避开维护者正在运行的实例）：zip 校验通过，各文件内容如上；网络检查在本机显示经代理 `127.0.0.1:7890` 全部可达、直连全部超时，DNS 是代理 TUN 的 198.18.x 假 IP——这正是之前 `ProxyConnectionRefused` 那类问题需要的信息。离屏下没有 GPU 和音频设备（软件渲染、换了运行目录连不上 PulseAudio），这两份要在桌面上看。

**没做 / 留给以后**
- V6 关联 ID（§6）。
- Windows 崩溃堆栈、Windows/macOS 的 `build_id`；CI 保留调试符号。
- 摘要里的音频设备信息；Snackbar 的"导出"动作按钮。
- 详细日志开关保留，但文案改成"问题要很久才出现时打开"。

## 11. 错误要能定位（维护者 2026-11-11 反馈后）

维护者看了 V1–V5 后指出：错误不能只写一个 `NetworkError`，要能看出在哪里、为什么出错。

- **代码位置**：顶层 CMake 打开 `QT_MESSAGELOGCONTEXT`（发布构建也带），处理器给每条 warning/critical 加 `src=<目录>/<文件>:<行> fn=<类>::<函数>`（文件只留最后两段，函数去掉返回类型、参数和外层命名空间）。QML 警告自带 `file=`/`line=`，不再加。
- **网络请求失败** `cirrus.net request.failed`：除错误类别外，另记 Qt 的原话 `detail`（经 `log::message()`：其中的 URL 只留主机，最长 300 字）、`method`、`host`、实际走的 `proxy`（与 QNetworkAccessManager 用同一个代理工厂，只记类型和地址，不记凭据）、`phase`（`connect` 没收到任何回应／`response` 有状态码／`body` 收到一部分）、`received` 字节、超时时的 `timeout_ms`、第几次 `attempt`、HTTP 原因短语 `reason`、TLS 错误 `tls`、系统报告的网络可达性 `reach`。重发记录 `request.retry` 也带 `detail`、`host`、`proxy`。
- **接口失败** `cirrus.api api.failed`／`api.done`：带传输层请求号 `req`（和 `request.*` 对得上）、业务码 `code`、服务端自己的说明 `msg`（如"操作频繁"，同样经 `log::message()`）。
- **封面下载失败** `cirrus.cache cover.failed`：带 Qt 错误、`detail`、HTTP 状态、类型不对时的 `type`、已收字节；超时带 `timeout_ms`。
- **崩溃**：除日志里的 `app.crash`（带崩溃那次的 `build_id`）外，另存 `crashes/<会话>.txt`（模块加偏移的表，最多 10 份），进诊断包。
- 原因从下层传到界面层（`model.failed` 直接带上 `request.failed` 的细节）要改多层值类型，仍是 V6；现在靠 `req` 和飞行记录器带出的前 30 秒记录串起来。

## 12. 诊断包结构（参考 IntelliJ IDEA 的 Collect Logs and Diagnostic Data、Firefox about:support）

```
cirrus-diagnostics-20261111-1403.zip
├── README.txt        包里有什么、怎么读（cirrus_log.py）、不包含什么；各会话的起止、是否正常结束、标记、warning 前十
├── about.txt         应用名、版本、提交、构建类型、build_id、安装方式、参数（路径换成占位）、PID、启动时间、运行时长；编译器、C++ 标准、编译与运行的 Qt、Qt 构建信息、TLS 后端与库版本
├── system.txt        系统、内核、架构、ABI、桌面、会话类型、QPA、系统深浅色；CPU 型号与线程数；内存总量/可用/交换、进程 RSS/峰值/线程数；数据、缓存、配置、日志所在分区的剩余与文件系统；语言、UTC 偏移；每块屏幕（名称、型号、几何、物理尺寸、缩放、DPI、刷新率、色深）；窗口
├── graphics.txt      图形接口、相关设置与环境变量（CIRRUS_GPU、QSG_RHI_BACKEND、DRI_PRIME、__NV_PRIME_RENDER_OFFLOAD…）；RHI 后端、设备名、设备/厂商 ID、类型（独显/核显）；OpenGL 时另有 GL_VENDOR/RENDERER/VERSION、GLSL、上下文版本、采样、交换方式
├── audio.txt         默认输出设备，每个输出设备的描述、首选格式、采样率与声道范围、支持的样本格式
├── network.txt       导出时现场检查（约 6 秒）：应用/系统代理、代理环境变量（去掉凭据）；网络后端、可达性、传输介质、是否计费、是否强制门户；TLS；对 interface/music/interface3.music.163.com、p1/m701.music.126.net 各做 DNS 解析（耗时、地址）和 HTTPS 请求（经代理、直连各一次：状态码、耗时、HTTP/2，失败时 Qt 的错误名和原话）
├── environment.txt   影响 Qt、显卡、桌面、音频、代理的环境变量（白名单；家目录换成 ~，代理去掉凭据）
├── modules.txt       已加载的库和插件（Linux）
├── state.txt         页面栈、弹窗、播放页；账号状态；播放状态、歌曲 ID、位置、队列位置、音量；网络状态；缓存与日志占用
├── settings.txt      全部设置（列表只记条数）
├── description.txt   用户写的描述（有才放）
├── crashes/          崩溃报告（§11）
└── logs/             选中的日志文件
```

- 导出改成异步：选好保存位置后先做网络检查，再写 zip，完成后 `App.diagnostics.exportFinished(ok)`；期间设置页显示"正在收集…"。
- 导出框"查看报告"展开的是除日志和 network.txt 以外的全部文件（`App.diagnostics.preview()`）。
- GPU 通过 RHI 取（`QQuickWindow::rhi()->driverInfo()`，需要 `Qt6::GuiPrivate`；找不到这个模块的构建里只写 OpenGL 自己报的信息，定义 `CIRRUS_HAVE_RHI` 区分）；首帧时记一条 `app.gpu backend=… device=…`。
- 隐私：不含账号或他人 ID、标题、搜索词、用户写的文字（描述除外）、本机路径（换成 `<data>`、`~`）、代理凭据。音频设备描述和屏幕型号照记（排查需要；是设备型号，不是个人内容）。
