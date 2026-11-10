# 日志系统（设计，待评审）

- 日期：2026-11-09
- 状态：**设计已评审（2026-11-09，§10 全部按建议）**；实施中（§11）
- 起因：维护者 2026-11-08 决定排查问题要靠日志、不靠猜（听歌上报没出现在"最近播放"、偶发"无法播放"），并明确要求建**整个应用的完整日志系统**，不是为眼前几个问题临时加打印。

## 1. 目标与非目标

**目标**
1. 一套全应用统一的日志设施：后端（domain/application/infrastructure）、展示层、QML、平台集成、Qt 自身和第三方库的警告，都进同一个出口。
2. 写到文件，可轮转、有总预算、可清理；开发时同时输出到终端。
3. 脱敏在**写日志的地方**完成（[11 §5](../11-security-privacy-and-compliance.md)），凭据、签名地址、Cookie 不可能进文件。
4. 每条记录能定位：时间、级别、线程、模块、稳定的英文事件名、关联 ID（请求号、加载令牌、命令号）。
5. 不拖慢任何线程：写文件在独立线程；音频回调线程不写日志。
6. 用户可控：设置里能开详细日志、打开日志文件夹、导出诊断包（导出前能看到包含哪些文件）。
7. 有约定：以后每个新模块、每个错误分支怎么写日志，写进 HANDOFF 和 AGENTS，代码评审按它检查。

**非目标**
- 不上传，不做遥测、崩溃上报服务（NFR-012：默认无遥测）。
- 不收集 core dump（11 §5：可能含凭据）。
- 不引入第三方日志库（spdlog 等）：Qt 的 `QLoggingCategory` 已经覆盖分类和过滤，Qt 和 QML 的内部警告也只能从 Qt 的消息处理器拿到，用 Qt 的机制最完整。

## 2. 现状

- 只有 3 个分类：`cirrus.ui.backend`、`cirrus.ui.fonts`、`cirrus.ui.palette`。约 25 处 `qWarning`/`qCWarning`，大多在 UI 层；后端只有 `audio_store.cpp` 里 2 处。
- 没有消息处理器，没有文件输出。从桌面启动时，所有输出都丢了。
- 网络、鉴权、播放、上报这些出问题最多的地方没有任何记录。错误枚举（`TransportError`、`ApiError`、`PlaybackError`、`ReportError` 等）只沿调用链返回，没留痕。
- 文档里已有的约束（本设计照办）：
  - [09 §1](../09-storage-cache-and-sync.md)：独立目录，轮转，7 天或总共 20 MiB，先到先淘汰。
  - [11 §5](../11-security-privacy-and-compliance.md)：推荐字段与禁止字段；诊断包预览、不自动上传；用户开的诊断有时限。
  - [13 §4](../13-engineering-and-delivery.md)：按分类，稳定英文 eventName，在边界脱敏。
  - R-015：敏感 URL 和 Cookie 进日志是高风险项。

## 3. 架构

```
任意线程：qCInfo(lcNet) << ...  /  qWarning(...)  /  QML console.*  /  Qt 内部警告
        │
        ▼
消息处理器（qInstallMessageHandler，进程内唯一）
  · 级别过滤（分类本身已过滤 debug；这里只补"重复消息合并"）
  · 拼成一条记录：时间、级别、线程名、分类、正文
  · 放进有界队列（加锁、不做 I/O）；满了丢弃并计数
        │
        ├──► 终端 sink（始终输出到 stderr，与 Qt 默认一样；从桌面启动时没人看，也无妨）
        ├──► 内存环形缓冲（最近 2000 条，诊断包和以后的应用内查看用）
        └──► 写文件线程：批量写入、按大小切分、启动时清理旧文件
```

- **新库 `cirrus_logging`**（`src/logging/`，只依赖 QtCore），`cirrus_backend`、展示层、`appCirrusMusic`、`appCirrusGallery` 和测试都链接它。分类声明集中在 `src/logging/categories.h`，任何层都能直接用。
- **安装时机**：`main()` 里先设好应用名（存储路径要用），然后马上安装处理器（`log::Session`，比应用对象先建、后销毁），在 `QGuiApplication` 构造之前。单实例检查通过后才打开文件（`log::start`），之前的记录先存在内存里，文件就绪后补写；第二次启动只唤起已有窗口，不留日志文件。
- **退出**：写一条 `app.exit`，排空队列，关闭文件。`qFatal` 和 `critical` 会同步落盘再返回，防止进程随后崩溃时丢掉最后几条。
- **异常退出检测**：启动时如果上一份日志没有 `app.exit` 结尾，记一条 `app.previous_session_unclean`，带上那份文件名。另外安装一个只做异步信号安全操作的信号处理器（SIGSEGV/SIGABRT 等）：往已打开的文件描述符写一行 `fatal signal N`，然后恢复默认处理。Windows 第一版不写这一行，只靠下次启动的异常退出检测。堆栈见 §10 LQ6。
- **线程名**：后端工作线程、数据库线程、音频线程、写文件线程等在创建时设 `objectName`，日志里显示名字而不是地址。
- **性能**：被关掉的 debug 分类几乎没有开销（`QLoggingCategory` 先判断）。处理器只格式化字符串再入队；队列上限 8192 条，满了丢弃，恢复后补一条 `log.dropped count=N`。同一条正文在 1 秒内重复超过 20 次就合并，之后补一条 `repeated N times`，避免某个循环刷满日志。
- **音频实时线程禁止写日志**：处理器要加锁、要分配内存。欠载、解码错误等在实时线程里只累加原子计数，由所属线程用定时器汇总后再写（例如 `audio.underruns count=3 window_s=10`）。

## 4. 分类、级别与格式

### 4.1 分类

在 13 §4 的五大类基础上细分到模块，统一用 `cirrus.<层>.<模块>`：

| 分类 | 覆盖 |
| --- | --- |
| `cirrus.app` | 启动、退出、单实例、命令行、GPU 选择、环境摘要 |
| `cirrus.net` | HTTP 传输：每个请求的结果、耗时、大小、重定向、HTTP/2 和连接错误 |
| `cirrus.api` | 网易接口：操作名、业务码、错误映射、限流（retry-after） |
| `cirrus.auth` | 匿名注册、二维码登录、会话阶段变化、登出、换号 |
| `cirrus.secrets` | 密钥环读写的结果（永远不记内容） |
| `cirrus.storage` | SQLite 打开、迁移、事务失败 |
| `cirrus.cache` | 封面、歌词等资源缓存：命中、淘汰、清理 |
| `cirrus.playback` | 会话与队列：加载令牌、意图、状态变化、预载、接续、失败原因 |
| `cirrus.media` | 音频引擎与音频存储：打开、范围下载、缓存命中、地址过期刷新、设备变化、欠载汇总 |
| `cirrus.report` | 听歌上报：生成、发送、结果码、丢弃原因 |
| `cirrus.platform` | MPRIS、SMTC、Now Playing、系统托盘、窗口集成 |
| `cirrus.ui.*` | 展示层（`backend`、`fonts`、`palette`、`images`、`nav`…） |
| `qml` / `js` | QML 的 `console.*` 和绑定、加载警告（Qt 自带的分类，原样保留） |
| `qt.*` | Qt 内部警告（网络、多媒体、QPA 等），原样保留 |

### 4.2 级别怎么用

| 级别 | 用于 | 默认写入文件 |
| --- | --- | --- |
| `critical` | 无法继续：数据库打不开、后端启动失败 | 是 |
| `warning` | 意外但能继续：请求失败、播放失败、上报被拒、密钥环拒绝、契约不符 | 是 |
| `info` | 生命周期和状态变化：启动、登录、加载开始和结束、每个请求的结果摘要、上报结果 | 是 |
| `debug` | 细节：重试过程、缓冲进度、队列变化、QML 导航 | 只在详细模式（§7） |

- 同一个错误只在**作出决定的那一层**记一次（例如传输层记 `net.request.failed`，接口层只在把它映射成业务错误时补一条，不要每层都记一遍）。
- 用户预期的结果不算 warning：取消、用户停止、游客没有权限，记 info 或不记。

### 4.3 格式

一行一条，固定前缀，后面是事件名加 `key=value` 字段（见 §10 LQ1）：

```
2026-11-09T14:03:12.345+08:00 W backend cirrus.net request.failed req=812 op=song/enhance/player/url/v1 proto=eapi err=network qt=protocol_failure ms=203 host=interface.music.163.com
2026-11-09T14:03:12.351+08:00 W main    cirrus.playback load.failed token=57 song=1357375695 reason=network attempt=1
2026-11-09T14:05:40.010+08:00 I backend cirrus.report report.result id=3f2c… song=1357375695 source=list sourceId=2829883282 code=200 outcome=acknowledged
```

- 时间带时区，到毫秒。级别一个字母（D I W C F）。
- 值里有空格或 `=` 时加引号并转义；换行转义成 `\n`，所以一条记录永远只占一行。
- 事件名是稳定的英文 `领域.动作`，以后改措辞不改事件名，方便检索。

### 4.4 写法

提供一个小工具，让事件名、字段和脱敏一次写清楚：

```cpp
#include "logging/log.h"
using namespace cirrus::log;

qCWarning(lcNet).noquote() << event("request.failed")
    << field("req", requestId) << field("op", logicalPath)
    << field("err", error) << field("ms", elapsed)
    << field("host", hostOf(url));      // 只有主机名，不含路径和查询串
```

- `field()` 只接受安全的类型：数字、枚举（用 `QMetaEnum` 或 `toString` 转名字）、`bool`、短字符串，以及 §5 里的脱敏包装。`QUrl`、`QNetworkRequest`、`QByteArray` 不能直接传，编译时就报错。
- 现有的 `qCWarning(x) << "..."` 写法仍然能用（Qt 的消息照样经过处理器），迁移时逐步改成事件格式。
- QML 里不需要新接口：`console.warn` 和 QML 警告自动进日志。展示层需要的 UI 事件（页面导航、播放器打开）在 C++ 展示层记。

## 5. 隐私与脱敏

规则在**写日志的地方**落实，靠类型而不是靠事后替换：

| 内容 | 处理 |
| --- | --- |
| Cookie、MUSIC_U、令牌、xeapi 会话、密码、验证码、二维码内容 | 永远不记。`TransportRequest`/`TransportResult` 里这些成员不提供可记录的形式 |
| 媒体地址、带签名的地址、任何完整 URL | 只记主机名（`hostOf`）或接口的逻辑路径（`logicalPath` 本来就不含参数） |
| 请求体和响应体 | 不记。只记大小、业务码、解析失败的位置（字段名） |
| 本地绝对路径 | 用 `pathOf()`：换成 `<cache>/…`、`<data>/…` 这类根目录占位，或只留文件名 |
| 账号 ID（uid） | 用 `account()`：本次安装内稳定的短散列（HMAC，密钥是安装级设备 ID），能看出"是不是同一个号"，但看不出是谁 |
| 手机号、邮箱、昵称、评论、歌单名、歌词正文 | 不记 |
| 搜索词 | 只记长度（`len=4`） |
| 歌曲、专辑、歌单、歌手 ID | 见 §10 LQ2 |

- **测试兜底**：加一个"金丝雀"测试：用含特殊标记的假 Cookie、假签名地址和假 uid 跑登录、播放、上报的假流程，然后检查整个日志输出里不出现这些标记。
- 诊断包导出前再列一遍文件清单和环境信息（§7），但脱敏不依赖这一步。

## 6. 文件、轮转与保留

- 位置：`QStandardPaths::StateLocation` 下的 `logs/`。Linux 是 `~/.local/state/Cirrus/logs`（遵循 `XDG_STATE_HOME`）；Windows 和 macOS 由 Qt 映射到各自的应用数据目录，需要在那两个平台上确认（§10 LQ5）。目录权限 0700，文件 0600。
- 每次启动一个文件：`cirrus-20261109-140312-<pid>.log`；单个文件超过 4 MiB 就续写 `…-2.log`。
- 保留：总量 20 MiB 或 7 天，先到先删，从最旧的删起（09 §1）。当前文件和上一次启动的文件始终保留，不管多旧，方便排查"上次为什么崩了"。启动时清理一次，写满切分时也检查一次。
- 每个文件开头写一段会话头：版本、git 提交、构建类型、Qt 版本、操作系统和内核、QPA 平台、渲染后端和 GPU 选择、界面语言、启动参数（不含路径）。不写用户名、主机名、路径。
- 开发和测试：位置跟着 `XDG_STATE_HOME` 走（测试运行设成 `build/linux-dev-local/xdg/state`）；单元测试默认不写文件，只在需要时把处理器装到临时目录。

## 7. 用户可见的部分（设置 → 新增"诊断"组）

按"设计在先"，这里只定内容，页面细节等评审后写进 [settings.md](../ui/pages/settings.md)：

| 项 | 行为 |
| --- | --- |
| 详细日志（开关） | 打开 `cirrus.*` 的 debug 级别，24 小时后自动关闭（11 §5：诊断要有时限）。开启状态和到期时间会保存 |
| 打开日志文件夹 | 用系统文件管理器打开 `logs/` |
| 导出诊断包 | 先弹窗列出要包含的文件（日志文件、会话头，可勾选去掉），确认后选位置保存一个 zip。不包含数据库、缓存、配置 |
| 清除日志 | 删掉除当前文件以外的所有日志 |

- 开发者照旧可以用 `QT_LOGGING_RULES` 和 `--log-level`（新增命令行参数）临时改级别。环境变量优先级高于设置。
- 应用内日志查看页不在第一版（§10 LQ4）。

## 8. 要覆盖的事件（第一版基线）

"完整"指每个模块的生命周期、对外边界和失败分支都有记录。下面是各模块至少要有的事件；新模块按 §9 的约定补上自己的。

| 模块 | 事件 |
| --- | --- |
| 应用 | `app.start`（会话头）、`app.single_instance`、`app.gpu`、`app.exit`、`app.previous_session_unclean`、退出各阶段耗时、退出超时（D6） |
| 后端运行时 | 工作线程启动和停止、`backend.ready`/`backend.start_failed`（原因）、命令队列满、关闭耗时 |
| 网络 | 每个请求一条 `request.done` 或 `request.failed`：请求号、逻辑路径、协议、HTTP 状态、Qt 网络错误名、耗时、响应大小、retry-after；被阻止的重定向；超时；读操作的连接断开重发 `request.retry`（第几次、延迟） |
| 接口 | 业务码不为成功、错误映射结果、契约不符（字段名）、限流 |
| 鉴权 | 匿名注册的结果与节流、二维码状态迁移（不含内容）、会话阶段变化、换号（散列）、登出 |
| 密钥环 | 读、写、删的结果和错误类别 |
| 存储 | 打开、迁移版本、失败的事务（操作名）、关闭 |
| 资源缓存 | 根目录不安全、清理、容量淘汰汇总 |
| 播放会话 | 加载开始（令牌、歌曲、意图、来源）、解析音源（音质、试听、过期时间）、状态变化、失败（原因、第几次）、预载、无缝接续、地址过期刷新、队列替换（条数） |
| 音频引擎与存储 | 打开流（缓存命中、已缓存比例）、范围请求（区间、状态）、md5 校验失败、取消、设备变化与切换、解码错误、欠载汇总、缓存淘汰 |
| 听歌上报 | 结算（结束方式、秒数、来源）、不上报的原因（太短、游客、FM、换号）、发送、结果（业务码、结果）、退出时 flush 超时 |
| 音乐库写操作 | `ui.backend write.done`／`write.failed`（操作类别、结果、错误、业务码）、`write.dropped`（换了账号不发）、`write.refused`、`write.invalid`；不记歌单名、简介等内容 |
| 评论写操作 | `ui.backend comment_write.done`／`comment_write.failed`（点赞、取消点赞、发表、回复、删除；结果、错误、业务码）、`comment_write.dropped`（换了账号不发）、`comment_write.refused`、`comment_write.invalid`；不记评论内容 |
| 平台 | MPRIS、SMTC、Now Playing 的注册结果与失败，收到的外部命令（debug） |
| 展示层 | 页面加载失败（`LoadableModel` 的错误类别）、图片解码失败、无效请求（程序错误） |
| QML | 所有 QML 警告和 `console.*`（自动） |

## 9. 约定（评审通过后写进 HANDOFF 和 AGENTS）

1. 新模块在 `categories.h` 声明自己的分类，不在 cpp 里各自 `Q_LOGGING_CATEGORY`。
2. 每个对外边界（网络、密钥环、数据库、系统接口）的每次失败都记一条 warning，在作出决定的那一层记一次。
3. 状态迁移和生命周期记 info；高频细节记 debug；循环里不记 info。
4. 只用 `event()` 和 `field()` 写新日志；字段名用小写下划线，同一个意思在全项目里用同一个名字（`req`、`token`、`song`、`ms`、`err`…，在 `log.h` 里列出）。
5. 不把 URL、请求体、响应体、凭据和用户输入的文本传进日志；需要时用 §5 的包装。
6. 音频实时线程和信号处理器里不写日志。
7. 被忽略的错误要么记 debug，要么在代码注释里写明为什么可以忽略（13 §4）。
8. 评审时检查：新的错误分支有没有日志，日志里有没有敏感字段。

## 10. 待决问题

维护者 2026-11-09 决定：全部按建议。

| # | 问题 | 选项 | 决定 |
| --- | --- | --- | --- |
| LQ1 | 文件格式 | A. 一行文本，固定前缀加 `key=value`（§4.3）；B. 每行一个 JSON | **A（已定）**：直接打开就能看，`grep` 也够用；字段格式固定，以后写查看器也能解析 |
| LQ2 | 歌曲、专辑、歌单、歌手 ID 明文记不记 | A. 明文记（日志只在本机，导出前能预览）；B. 散列 | **A（已定）**：排查"这首歌放不了""这次上报没生效"必须知道是哪首、哪个歌单，这些是公开的目录 ID。这会修改 11 §5（"歌曲历史默认不记"），需要你拍板。账号 uid 仍然散列，搜索词只记长度，歌名不记 |
| LQ3 | 详细日志开关的期限 | A. 24 小时后自动关；B. 到本次退出；C. 一直开到手动关 | **A（已定）**：复现问题常常要重启；24 小时满足 11 §5 的"有时限" |
| LQ4 | 设置里的入口 | A. "诊断"组：详细日志、打开文件夹、导出诊断包、清除日志；B. 再加一个应用内日志查看页 | **A（已定）**；查看页以后需要再做（环形缓冲已经预留） |
| LQ5 | Windows 和 macOS 的日志位置 | A. 用 Qt 的 `StateLocation` 默认映射；B. macOS 改用 `~/Library/Logs/Cirrus`（Console.app 能看到） | **B（已定）**：符合 macOS 习惯。Windows 用默认（`%LOCALAPPDATA%\Cirrus`）。两者都要等你在设备上确认 |
| LQ6 | 崩溃时记多少 | A. 只记"上次异常退出"和信号号；B. 再记调用堆栈（Linux 用 `backtrace`，Windows 用 `DbgHelp`） | **A（已定）** 先做；B 等真遇到难查的崩溃再加，而且堆栈要检查不含敏感内容 |

## 11. 实施步骤

| 步 | 内容 | 完成条件 |
| --- | --- | --- |
| L1 核心（**已完成**，§12） | `cirrus_logging`：处理器、队列、写文件线程、轮转与保留、会话头、异常退出检测、`event`/`field`/脱敏包装、分类表；在 `appCirrusMusic` 和 Gallery 里安装；现有约 25 处调用迁移到新分类 | `tst_logging`（格式、转义、轮转、保留、队列满、多线程、重复合并、异常退出标记）通过；从桌面启动后 `~/.local/state/Cirrus/logs` 里有完整的一次会话 |
| L2 后端覆盖（**已完成**，§12） | §8 中网络、接口、鉴权、密钥环、存储、缓存、播放、媒体、上报、平台的事件 | 金丝雀测试通过；用真实后端放一首歌、换一首、退出，日志能完整看出请求、加载、上报的经过 |
| L3 设置（**已完成**，§12） | "诊断"组（先写进 settings.md 评审），导出诊断包 | Gallery 截图检查；导出的 zip 内容与预览一致 |
| L4 文档（**已完成**） | HANDOFF、AGENTS、13 §4、11 §5（按 LQ2 的决定）更新约定 | — |

L1、L2 完成后，就用日志排查两个遗留问题：听歌上报没出现、偶发的"无法播放"（[integration.md §8](../ui/integration.md)）。

## 12. 实施记录

**L1 核心（2026-11-09）**
- `src/logging/`（目标 `cirrus_logging`，只依赖 QtCore；`cirrus_domain`、协议、存储、密钥环、展示层、主题、应用和 Gallery 都链接它）：
  - `categories.h`：§4.1 的全部分类（`lcApp`、`lcNet`、`lcApi`、`lcAuth`、`lcSecrets`、`lcStorage`、`lcCache`、`lcPlayback`、`lcMedia`、`lcReport`、`lcPlatform`、`lcUi`、`lcUiBackend`、`lcUiTheme`、`lcLog`）。原来的 `cirrus.ui.fonts`、`cirrus.ui.palette` 并入 `cirrus.ui.theme`。
  - `log.h`：`event()`、`field()`、`host()`、`path()`、`account()`。枚举的名字依次取：枚举旁边的 `logName()`（ADL）、`Q_ENUM`、都没有就写数字。`QUrl`、`QByteArray` 等传进 `field()` 编译不过。
  - `logger.h`：`install`/`start`/`shutdown`/`flush`、`setVerbose`、`standardDirectory`、`recent`、`setPathRoots`、`setAccountKey`，以及 `Session`。
- 处理器之外还有一道兜底清洗：所有记录（包括 Qt、QML 自己的）都会去掉 URL 的查询串和片段，路径根目录换成 `<data>`、`<cache>`、`<config>`、`<logs>`、`<tmp>`，家目录换成 `~`。
- 账号散列的密钥存在设置里（`diagnostics/accountKey`，32 字节随机），诊断包不含设置。
- 线程名：`main`、`backend`、`db`、`decode`；线程池线程沿用 Qt 的名字。
- `appCirrusMusic` 新增 `--log-level debug`（只管本次运行）；会话头带 `commit=`（配置时的 git 提交）。
- 现有 25 处调用已改成事件格式：后端主机、登录恢复、模型、字体、取色、封面、MPRIS、SMTC、单实例、音频缓存。
- 验证：
  - `tst_logging` 9 项通过：字段格式与转义、安全包装、会话文件（文件头、暂存记录、单行、清洗、权限、详细模式）、上次异常退出（含信号号）、切分与按时间/总量清理、重复合并、4 线程 2000 条不丢。
  - `audio_store`、`storage`、`audio_engine` 测试通过；clang-format 通过。
  - 离屏启动真实应用后退出：`xdg/state/Cirrus/logs` 下目录 0700、文件 0600，内容是 `app.start` → `app.platform` → `backend.ready` → `app.quit` → `app.exit`。
  - Gallery 正常输出到终端。

**L2 后端覆盖（2026-11-09）**
- 枚举起名：`CIRRUS_LOG_ENUM(Enum, A, B, …)`（`log.h`，借 C++20 `using enum` 按值对名字，不依赖顺序）。已给传输、接口、解码、播放、上报、会话、登录、密钥环、存储、缓存、匿名注册等错误与状态枚举起名。`log::join()` 用于运行时才决定级别的记录。
- 各模块的事件（§8）：
  - 网络：`net request.failed`（逻辑路径、协议、错误、Qt 错误名、HTTP 状态、耗时、是否 HTTP/2、retry-after）；成功和取消记 debug。
  - 接口：`api.done`（路径、业务码、耗时）、`api.failed`（解码、Cookie 等）；传输失败只由网络层记。
  - 读取：`api music.invalid`（warning，路径）是业务码 200 但映射器不接受的响应；映射器可在 debug 记 `music.invalid_why`（哪项检查、第几条，现有云端最近播放）。
  - 音频存储：`audio.open`（已缓存比例、是否校验过、临时文件）、`audio.range`（debug）、`audio.range_failed`、`audio.url_refused`/`url_needed`/`url_refreshed`、`audio.failed`、`audio.cached`、`audio.prefetch`、`audio_cache.evicted`/`clear`。
  - 音频引擎：`engine.load`/`ready`（打开耗时）/`ended`/`failed`/`next_opened`/`advanced`/`source_expired`/`device_changed`/`no_output`，`engine.underruns`（每 10 秒汇总）。
  - 播放：`load.start`/`refused`/`resolved`（实际音质、试听、耗时）/`ready`/`failed`（错误、业务码、当时状态）/`advanced`/`refresh`、`preload.*`；队列 `queue.replace`/`restored`/`select`/`next`/`previous`/`stop`/`advance`/`begin`/`begin_refused`。
  - 上报：`report.settled`（动作 StartPlay／Play、结束方式、秒数、来源是否找到）、`report.skip`（原因）、`report.send`、`report.response`（debug，只有字段名）、`report.result`（结果、业务码）、`report.flush`/`flush_timeout`。
  - 认证：`session.phase`（账号散列）、`guest.bootstrap`、`login.qr_ready`/`qr_status`/`failed`/`signed_in`；密钥环 `keyring.done`/`failed`（固定条目名）；`db.opened`/`open_failed`；`backend.start`/`started`/`close`/`drained`。
  - 封面：`cover.failed`（debug：`cover.fetched`、`cover.rejected` 哪项检查没过；展示层 `ui.backend cover.served`：来自磁盘还是网络、字节数、从请求到拿到的耗时）。展示层：`player.play`（条数、来源）、`player.play_pause`、`model.failed`（游客的"需要登录"记 info）。平台：`mpris.command`。
- 会话头的 `commit=` 每次构建检查（`cmake/cirrus_commit.cmake`），有未提交改动时带 `-dirty`。
- 验证：
  - `tst_log_canary`：假 Cookie、MUSIC_U、二维码 key、设备号、响应体、签名地址（路径和查询串）、账号 ID 走真实代码路径（详细模式），文件里都不出现，该有的记录都在。
  - 受影响的测试（网络、接口、播放、引擎、音频存储、上报、会话、密钥环、存储等）通过；`backend_runtime` 仍只有已知的 `closeWhileDatabaseOpening` 失败。
  - 离屏启动真实应用：一次会话 25 行，启动、恢复游客、各接口、退出都看得清。

**L3 设置（2026-11-09）**
- 日志库：`log::files()`（新到旧，标出本次运行的）、`log::clearOld()`（只留本次运行的）、`log::writeArchive()`（自己写的 zip：deflate、UTF-8 文件名、`QSaveFile` 写完才落盘；zlib 已是依赖）。
- 展示层 `App.diagnostics`（`ui/presentation/diagnostics.{h,cpp}`）：`verbose`（打开时把到期时间写进 `App.settings.verboseLogUntil`，启动时还没到期就重新打开，定时器到期自动关，记 `log.verbose`/`log.verbose_expired`）、`verboseEnds`、`available`、`bytes`、`oldBytes`、`files`、`refresh()`、`openFolder()`、`exportTo()`（只接受列表里的文件名，导出前先 `flush`）、`packageName()`、`clear()`。
- 设置页新增"诊断"组（`DiagnosticsSettings.qml`，图标 `bug_report` 已加进图标子集）和 `ExportLogsDialog.qml`（文件列表超过 360 px 滚动；回车、空格勾选当前项；系统保存对话框）。
- 验证：
  - `tst_logging` 10 项通过（新增列文件、打包、清除；打出的 zip 用 Python `zipfile -t` 校验通过）；qmllint 无警告。
  - 真实应用截图：诊断组四行、导出对话框（勾选、取消勾选）；打开详细日志后日志里有 `log.verbose on=true`。离屏软件渲染时列表裁剪外会漏出复选框的勾，OpenGL 后端没有，是软件渲染器的限制。
  - 撤掉测试钩子后，干净的二进制能正常启动和退出。
- 没有验证：系统文件管理器打开文件夹、系统保存对话框（离屏没有），等维护者在桌面上试。

**接下来**：用日志排查听歌上报没出现在"最近播放"、偶发"无法播放"（integration.md §8）。需要维护者在桌面上打开"详细日志"、复现一次，然后导出诊断包或直接看 `~/.local/state/Cirrus/logs`。
