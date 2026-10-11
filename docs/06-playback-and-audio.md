# 06 · 播放与音频系统

> 状态：完整播放器设计仍为提案；[官方音源／Qt单曲播放](api/stream-resolution-and-playback.md)已落地并通过公开试听静音PoC。[队列／六轨歌词时钟](api/queue-and-lyric-clock.md)已完成最小后端闭环。当前仍为有界完整临时缓冲后起播，尚无流式起播；下文未逐项标注的完整产品能力不能视为已实现。

最新[播客／声音离线接缝阶段1](api/spoken-media-seam.md)已分离物理MediaSource：媒体port／buffer／Qt backend不含Song／Program／Voice身份，唯一歌曲coordinator仍先核对Song／token／TTL再下沉；独立PlaybackEntity／AudioTrackId仅结构准备，未接活动快照／历史或可信binding，不生成第二owner。373＋thread13／新28、0fail／原audio1默认skip，未证明实际节目／声音可播，消费binding与音源门禁仍保留；不恢复前三项或实网／UI，64MiB／45s等限制不扩。

最新系统媒体源码：[B-031 §9.9](tasks/B-031-playback-observation.md)补写SMTC／Now Playing原生代码及Core有界回调／main-loop生命周期，默认关闭、原binding/private intent复验，关闭不停止音乐。Linux便携接缝编译通过；Windows/macOS未原生编译／运行，不能称三平台支持已完成；无新增或运行测试、产品接线或依赖安装。

此前Linux系统媒体代码检查点：[B-031 §9.8](tasks/B-031-playback-observation.md)Linux MPRIS已写入可选默认OFF target；同一publisher/feed／原SystemMediaCommands桥、owner私有来源复验，独立D-Bus I/O线程与异步关闭，无第二player／clock／query。controls与metadata另行opt-in，Rate1／Position不PropertiesChanged／Seeked仅确认反馈；Debug/Release与Clang/链接通过，但未启动真实bus服务／新增或运行测试。SMTC／macOS Now Playing仍未实现，产品／桌面／设备／权限验收不变。

此前O3b2：[B-031 §9.6](tasks/B-031-playback-observation.md)live原来源→mailbox→Linux单I/O线程/真实socket、严格发现/双向UID/hello与显式XDG选择限定I/S；新50、Debug361＋runtime6/Release307，0fail/skip。真实八慢client的input/retained＋native pending、单票wake/隐私/期限/关闭已验；新实例不保留旧clock/document关联，也不撤销private retirement。无历史changed授权/第二播放链/自动业务查询/产品接线，default off、58登记非全58；发行身份/另一OS user/minQt/沙箱/部署性能及adapter仍G。

此前维护者选择：[B-031统一播放状态／六轨歌词本机只读IPC及三平台系统媒体设计](tasks/B-031-playback-observation.md)。Linux MPRIS、Windows SMTC、macOS Now Playing共用同一观测投影／命令桥；IPC含歌词文档与独立游标，不依赖UI、不加第二播放器/时钟。原设计后已按继续选择接O1 Core投影/coherent sample、实际volume反馈port、offset/位置到达及保守native位置不连续接缝，限定合成验收见§9.1；后续O2a codec/framer/完整golden/六轨分页限定I/S见§9.2（125新、5selected216＋runtime6），后续O2b逻辑连接/订阅/增量编码/退役/背压fake限定I/S见§9.3（43新/6selected259＋runtime6），后续O3a Linux私有资源/native UID隔离harness见§9.4（32新/3selected200），仍无Core/真实调度/发现消费者/另一OS用户负测或产品状态server；设备/decoder效果与三平台adapter仍G。不默认安装新平台依赖、开启HTTP或恢复paused。两个账号API先不做，队列/历史持久化后续另包。

## 1. 组件职责

| 组件 | 职责 |
| --- | --- |
| ListeningSession | 已实现：调用方Core-only唯一队列意图入口，通过异步Facade驱动单曲协调器与歌词查询 |
| PlaybackCoordinator | 已实现：后台单曲来源／目标意图／状态机／generation，恢复能力仍有限 |
| PlaybackQueue | 已实现：独立队列值模型、随机／循环与真实历史；不直接操作解码器 |
| IStreamResolver | 根据 TrackKey、账号和音质获得本次可播放来源 |
| IPlaybackBackend | 已实现load(MediaSource,LoadToken)、play/pause/stop/seek/volume／event，无identity、不调用官方API；设备与完整能力仍提案 |
| QtPlaybackBackend | QMediaPlayer/QAudioOutput 实现；把 Qt 状态映射为统一后端事件 |
| LyricTimeline | 已实现：独立六轨时间索引；ListeningSession用媒体位置及试听起点定位，不建立独立音频时钟 |
| PlayerViewModel | 对 QML 发布脱敏快照及窄命令接口 |
| PlaybackObservationPublisher（O1已实现） | Core-only coherent投影，原绑定／same-token隐私屏障；同线程历史值，未来跨线程consumer仍须单latest与消费时复验 |
| PlaybackObservationCodec／LinuxObservationService（限定已实现） | codec/framer/golden/六轨分页与便携逻辑连接已接；O3b2另有参数化default-off Linux owner→真实socket/严格发现组合，线程/限额/隐私见§9.6；非产品端点/发行验收，不拥有媒体或发官方请求 |
| SystemMediaAdapters（分阶段） | Linux MPRIS可选代码／编译已接，桌面运行仍G；[§9.9](tasks/B-031-playback-observation.md)新增Windows SMTC／macOS Now Playing原生源代码、owner/main-loop与有界callback/private intent gate。仅Linux便携接缝编译通过，原生SDK／运行未验，全default off／不接产品；命令仍通过ListeningSession唯一入口，不维护第二套权威状态 |

使用单路主音频实例；视频／MV 明确排除。一起听仍通过同一个协调器执行，不另建第二套播放器。两个 QML 页面不能各自实例化一个 `MediaPlayer`。

## 2. 后端契约与能力

逻辑命令：`load(source, loadToken)`、`play()`、`pause()`、`stop()`、`seek(positionMs)`、`setVolume(linear)`、`setDevice(deviceId)`。

事件携带固定 loadToken：`ready`、`stateChanged`、`positionChanged`、`durationChanged`、`bufferingChanged`、`seekabilityChanged`、`endOfMedia`、`error`。

`BackendCapabilities` 至少包含：seek、设备选择、倍速、变速保调、gapless、crossfade、DSP。部分能力依具体媒体动态变化；QMediaPlayer 可 seek 不意味着每个远程源都支持随机定位。

- 不承诺 Qt Multimedia 默认具备可产品化的无缝播放、交叉淡入淡出或均衡器。
- Qt 6.10 起有与 pitch compensation 相关的接口，但可用性依后端而定；运行时探测并测试才可显示。
- 输出走系统音频栈；系统重采样、混音和设备差异存在，首版不宣称“位完美输出”。
- Qt 内部下载／缓冲不等于应用持久化音频缓存。首版不宣称自动落盘缓存全部流；授权离线来自独立下载服务。

## 3. 播放状态与用户意图分离

`PlaybackSnapshot`：activeEntryId?、state、desiredTransport（Play/Pause/Stop）、positionMs、durationMs?、buffered?、actualQuality?、error?、generation。

| 状态 | 含义 | 关键迁移 |
| --- | --- | --- |
| Empty | 当前无可用媒体实例；队列可以非空 | 用户播放 → Resolving |
| Resolving | 解析权限和来源 | 有效来源 → Loading；拒绝 → Failed；取消 → Empty |
| Loading | 后端加载媒体 | ready → Ready；错误 → Failed |
| Ready | 来源已就绪、等待应用目标意图 | Play → Playing；Pause → Paused |
| Playing | 后端实际在播放 | pause → Paused；饥饿 → Buffering；结束 → Ended |
| Paused | 已加载但用户要求暂停 | play → Playing 或 Buffering；新目标 → Resolving |
| Buffering | 数据不足，不改变用户目标意图 | 恢复后按 desiredTransport；超时 → Failed |
| Ended | 当前媒体自然结束 | 根据队列规则进入下一项或保留结束态 |
| Failed | 当前尝试不可继续 | 用户重试创建新 generation，或自动队列策略有限跳过 |

`stop()` 使 generation 失效、释放当前来源、位置归零并进入 Empty；保留队列和选中项用于再次播放。应用关闭不等于远程“停止播放接口”。

Resolving/Loading 时用户按暂停必须立即把 desiredTransport 改为 Pause，加载完成也不能突然出声。缓冲中暂停同理。seek 在可 seek 前仅保存最新待应用位置，ready 后夹取到合法范围；不把每个拖拽事件都排进后端。

## 4. 反串歌协议（必须实现）

每次换曲、换音质、stop、登出或重置媒体实例：

1. 增加 playbackGeneration，记录目标 entryId、sessionEpoch 和用户播放意图。
2. 取消旧来源请求并退役旧后端 loadToken。
3. 发起解析；结果必须同时匹配 generation、epoch、entryId 才能使用。
4. 向后端传固定 token，不在事件到达时才读取“当前 generation”冒充事件所属代数。
5. ready、position、error、endOfMedia 都检查 token；重复 endOfMedia 只能消费一次。
6. 请求已经成功但用户已切走时，立即丢弃敏感 URL，不写入当前状态。

**QMediaPlayer 特别约束**：其信号通常不自带应用 loadToken。首个可靠实现可让每次 load 对应独立媒体对象上下文，旧对象停止、断开、按生命周期释放，连接 lambda 捕获固定 token。若为了优化复用实例，必须先证明旧源信号已隔离；仅在同一个 player 的回调里贴上最新 generation 是错误实现。

验收：快速点击 A→B→C，令网络按 C→A→B 返回，且 A 的 error/endOfMedia 延迟到达；只能听到 C，元数据／歌词／系统媒体信息也必须对应 C。

## 5. 队列与切歌语义

| 操作 | 规则 |
| --- | --- |
| 从列表播放单项 | 为当前上下文创建或定位 QueueEntry；重复歌曲按 entryId 定位 |
| 立即播放 | 切换目标并开始新 generation |
| 下一首播放 | 插入当前项之后；连续添加按用户操作顺序，而不是倒序堆栈 |
| 删除非当前项 | 不打断播放，更新随机序列／历史 |
| 删除当前项 | 停止当前来源并播放原后继；无后继时停止；保持用户暂停意图 |
| 清空队列 | 停止播放并清空，重要未保存队列可提供一次撤销快照 |
| 上一首 | 当前位置 > 3 s 且可 seek 时回到开头；否则回历史／前项；行为可后续设为偏好 |
| 顺序 + 不循环 | 最后一项自然结束即停止，不偷偷回第一首 |
| 列表循环 | 自然结束后按顺序／随机序列进入下一轮 |
| 单曲循环 | 仅自然结束重播当前项；手动 next 仍前进 |
| 随机 | 保存一轮固定排列与真实历史，上一首不是重新随机；多首时新轮尽量避免边界重复 |
| FM | 独立动态队列源；不强行套用常规列表循环，快耗尽时预取一次 |

大歌单：先获取完整曲目 ID 序列再按需取详情；若官方仅支持分页，使用显式分页队列源、加载状态和失败重试，不能只播首屏却标“全部”。

## 6. 来源解析与音质切换

来源选择优先考虑当前可用且符合授权的本地资源，再解析网络来源。用户自有文件、授权下载和流媒体是不同权限路径，不根据文件存在就忽略限制。

换音质流程：保存位置与暂停意图 → 新 generation → 请求目标质量 → 显示实际返回质量 → 加载新源 → 验证 seekability 后恢复位置 → 按原意图播放／暂停。失败默认保留可恢复状态，不能静默回到开头。

“自动音质”是选择策略，不是假定官方总有最优算法。接口只能返回一种质量时显示真实质量并禁用无效选项。仅部分试听权限时，限制进度范围并显示试听标识。

## 7. 错误恢复与终止

| 失败 | 策略 |
| --- | --- |
| 过期签名 URL／来源拒绝 | 对当前项重新解析一次；不复用已失败 URL 无限重试 |
| 临时网络中断 | 保留位置、暂停意图与有限恢复预算；恢复后重新检查 URL 有效性 |
| 下架／无版权／无权限 | 明确标记；用户直接点选时停留解释，自动连续播放时可跳过 |
| 解码器／格式不支持 | 显示实际格式；可请求较低合法音质一次，仍失败则终止 |
| 无输出设备 | 暂停并保留位置，设备恢复后默认保持暂停 |
| 资源不存在 | 本地文件标记 Missing；网络按官方原因处理，不自动删除用户记录 |

一次自动续播最多尝试 `min(队列可选项数, 5)` 个不同 entry；全部失败后停止并汇总原因。单曲循环不绕过此限制。用户点击重试可开启新一轮，但后台不能自发无穷重试。

错误弹窗不得每次切歌重复出现；在播放器内展示当前失败，加可展开诊断。所有重试与失败都计入脱敏事件，不记录媒体 URL。

## 8. 进度、歌词与历史

- 以后端媒体位置为事实，单调时钟只用于短时间 UI 插值；暂停、seek、换速、缓冲后重新对齐。
- 对 QML 的常规进度通知可节流到 100–250 ms；歌词高亮用时间索引和受控动画，不能每帧向数据库写进度。
- 本地测试音频下逐行切换初始误差预算 ±100 ms；逐字目标 ±50 ms，需设备回环测量，不以网络歌词自身偏差归罪时钟。
- 用户拖动进度时显示预览位置，不被后端旧位置拉回；释放后一次提交，支持键盘步进。
- 本地历史默认达到 `min(30 s, 曲长的 50%)` 的实际收听量后记录一次；未知曲长用 30 s。跳转不计为已听时间。
- 官方播放事件严格按真实契约发送，与本地历史阈值独立；不伪造完整听歌时长或后台刷记录。
- 每 5 s 最多合并持久化一次进度，暂停／切歌／正常退出时立即提交。崩溃后的恢复容许该检查点窗口，但不能损坏队列。

## 9. 音频 PoC 的退出条件

首轮已完成[公开试听MP3及合成WAV的静音加载／暂停／seek](api/stream-resolution-and-playback.md)，不代表下列矩阵全部完成。M0 选择有授权的短样本，覆盖 MP3、AAC、FLAC、WAV（实际格式是否支持以打包结果为准），并验证：

1. 官方真实源、HTTPS、CDN 重定向、过期 URL、需要的 headers、Range/seek。
2. 连续切换、暂停中换曲、加载中取消、自然结束、变更质量。
3. 默认输出、设备切换、蓝牙断开、无设备、休眠恢复。
4. GNOME/KDE 的 PipeWire 环境及实际使用的兼容层；记录 Qt 所选 backend。
5. 最终 Flatpak／AppImage 中的 codecs、插件和许可证，不能只测开发机。

若 Qt 后端无法满足 P0，先提交新的 ADR 评估 libmpv 等方案，再实现替代。不得把“以后换播放器”当作跳过接口设计和测试的借口。
