# B-031 · 播放状态／歌词本机接口与系统媒体适配设计

> 状态：**O1与O2（codec/framer＋fake逻辑连接）限定 I/S；O3–O6真实IPC／native仍G**。维护者连续要求继续，本轮已接O2b便携逻辑连接、单latest mailbox／私有fence、分步编码与fake sink；不是可连接服务，不开放真实IPC或引入平台依赖。HEAD仍`1a36f78`、全部未提交；107 operations／26 families／44 FR不变，CTest登记53（非全53验收）。当前证据§9.3，O1/O2a各自历史§9.1/9.2，不累加或宣布三平台可用。

关联：FR-017/020/021/022/033、NFR-008/011/012，B-012/013/017，TC-PLAYBACK-OBSERVATION／TC-SYSTEM-MEDIA。`user_level`／`user_subcount` 两 API 先不做；v2、cloud 后续、spoken next、完整 Together 仍暂停；APK 禁读、固定 stash、UI 最后保持。此设计优先于此前建议的队列持久化小包，不撤销后续持久化／恢复需求。

## 1. 决策与非目标

1. **一份 Core typed 观测投影，多个消费者**：应用内订阅、系统媒体 adapter、本机 IPC 均读同一份已核对的快照。不把 `BackendFacade`、私有账号快照或 provider DTO 直接序列化。
2. **系统媒体与自定义 IPC 分开**：MPRIS／SMTC／macOS Now Playing 提供各自标准状态和控制；歌词由独立本机协议提供，不往 MPRIS 标准对象塞私有歌词属性。
3. **IPC v1 只读**：查询当前状态、当前六轨歌词及订阅。不能 play/seek/setVolume、登录、打开文件／URI、加载指定 SongId 或请求历史曲目的歌词。
4. **当前内容，不是内容服务器**：复用 ListeningSession 已持有的歌词；订阅、分页、重连不能引起额外官方请求、图片下载、播放、报告、SQL 写或全库缓存。
5. **默认关闭对外 IPC**；由显式 backend composition 配置启用，未来设置页再接线。不自动监听 TCP／HTTP／WebSocket，不做远程访问、浏览器接口、插件执行或通用 RPC。
6. 所有播放命令仍由 `ListeningSession → BackendFacade → PlaybackCoordinator → IPlaybackBackend` 执行。adapter、IPC 和观测投影不拥有播放器、队列调度器或第二歌词时钟。

系统媒体默认策略也应在 composition 明确配置，隐私策略优先。托盘、通知、portal 自定义快捷键、桌面歌词窗口、Windows/macOS 完整客户端发行不属于本包；三平台**媒体服务**需求不等于三平台所有产品能力已获验收。

## 2. O1之前的基线审计与拟补接缝（历史）

直接读取当前源体，不能用旧 `8485f760` review graph 判断最新能力：

| 当前事实 | 设计影响 |
| --- | --- |
| [ListeningSession](../../src/application/playback/listening_session.h) 是 caller-thread Core-only 唯一队列意图入口，已有 queue/playback/lyrics/cursors getter 和通知 | observer 与它同线程取值；不跨线程同步读 worker／媒体对象 |
| [begin/receive/reset/refreshCursors](../../src/application/playback/listening_session.cpp) 核对 LoadToken／requestId／SongId，并处理 reentrant revision | 不能绕开其原来源屏障；停止／换曲后的旧通知不能重新绑定新曲 |
| playbackChanged 在 refreshCursors **之前**发出，begin/reset 也分多次通知 | 不将每条原信号直接当原子 JSON 快照：事件循环尾合并，并增加可靠的最终观测提交接缝／重入测试 |
| [PlaybackSnapshot](../../src/domain/playback/playback_types.h) 有真实八态／intent／clip-local position／可选 duration／preview，但没有完整 Song 元数据或 volume | 标题、艺人、专辑缺失时未知；音量必须增加真实观测／设置反馈接缝，不能编默认 1.0 |
| QueueEntry 仅 UUID＋SongId；FM 的公开 queue getter 为空 | metadata 不自动通过 detail 补取；不能只靠 QueueEntryId 作为所有模式的活动身份 |
| [六轨 SongLyrics](../../src/domain/music/music_types.h) 保留 availability/timing/offset/lines/words；[LyricCursors](../../src/domain/playback/listening_types.h) 支持同时多行多字 | 不压成单个 currentLine，不按数组下标对齐翻译，不用 enhanced 字段名推断逐字能力 |
| [LyricTimeline](../../src/application/playback/lyric_timeline.cpp) 使用 source 时间＋各轨 offset＋user offset；ListeningSession 添加 preview.startMs | 暴露 clip 与原曲两套坐标；不在 IPC／adapter 重算另一份权威游标 |
| user offset 目前私有，offset 变化未必产生 cursorsChanged；seek／volume 命令接受也不是媒体效果事件 | 补 offset 观测、音量反馈、实际位置不连续证据，以及实际position事件到达时间传播（当前Snapshot也无此timestamp）；不能由 acknowledgement 编造 Seeked／已调音量或由任意changed重盖位置时间 |
| 当前 current() 主要依据 runtime Ready／token／load；不是完整 same-token account/phase 隐私门禁 | observer 额外捕获私有 session fingerprint；账号或认证态变化，即使 token 不变，也使外部投影失效 |
| [媒体 port](../../src/application/ports/playback.h) 只见 identity-free MediaSource；Qt backend／Ninja deps 已有历史范围内验证 | 投影不携带 source、URL、Cookie、账号ID、SessionToken、LoadToken、provider 原文错误或源路径 |

当前歌词 parser 本地界限：[512 Ki UTF-16 source／每轨 10,000 行／30,000 words／7日时间](../../src/infrastructure/netease/music/lyric_parser.cpp)；Timeline 同样有界。parser 会排序／归一化，IPC 导出的是**已解析领域值**，不是 provider raw LRC/YRC 或原服务全文。重复时间、重叠、未定尾行保留；JSON 重复展开可能比服务响应大，IPC 另有编码预算。

本轮 code intelligence 查询遇到 C++ read_symbol 三次未定位／module_report 符号0及旧 graph 警告，已改为直接读取当前方法体；不是缺实现的证据，也不是 fresh LSP／编译验收。现有 #92 cpp-check 输出解析问题独立保留。

## 3. 分层与线程

```text
caller/application thread
  ListeningSession + BackendFacade current getters
          │ final coherent sample / private provenance checks
          ▼
  PlaybackObservationPublisher (Qt Core only)
          ├── in-process immutable typed snapshot/document
          ├── bounded system-command bridge ──► ListeningSession
          └── latest-value mailbox, identity-tagged values only
                    ├── IPC I/O thread: codec + local server + peer guards
                    ├── Linux session-bus adapter
                    ├── Windows native SMTC adapter
                    └── macOS main-thread native adapter
```

分层位置（前三项在O1加入，第四项含codec/request/framer及O2b便携逻辑连接/mailbox，真实server/transport与第五项不存在；无产品入口接线）：

- `src/domain/playback/playback_observation_types.h`：公开值、binding、六轨只读 document view、capabilities，无 JSON／网络／native handles。
- `src/application/playback/playback_observation_publisher.*`：当前内容投影、原子采样、隐私屏障；不引入通用 Manager。
- `src/application/playback/system_media_commands.*`：窄命令、原 binding、动态能力与 bounded admission，委托唯一 ListeningSession。
- `src/infrastructure/ipc/playback_observation_*`：严格 JSON codec／framer／只读 server、平台 peer credential guard。
- `src/infrastructure/desktop/media/`：MPRIS／SMTC／macOS adapters，native headers 只在各自实现／target 中。

`cirrus_domain`／`cirrus_application` 必须仍仅 Qt Core，包括实际编译依赖与 MOC。IPC target 私有使用既有 Qt Network；Linux MPRIS 另需私有 Qt DBus、Windows native Windows SDK/COM/WinRT、macOS Objective-C++/Foundation/MediaPlayer framework：**原设计轮未改CMake；O1登记Core源／MOC及Core/Test套件；O2a独立cirrus_observation_codec静态target只依赖domain/Qt Core，JSON留基础设施，不反向依赖application；不安装或默认批准新平台依赖**，各 adapter 实现小包先确认可用工具链与依赖边界。它们不是 NetEase/NIM/Android SDK，也不许可引入第二媒体引擎。

跨线程不逐个排入所有 position 或整份歌词 queued signal。每个 sink 最多一个 latest-value mailbox＋一个待调度 wake；文档按不可变共享值跨线程，codec 只在 I/O thread 做有界编码。Core投影提供同源事实；document/cursor/metadata的export状态属于有界观测契约，I/O编码准备/失败按原binding报告，不能改内部LyricsState/availability/游标或播放事实，也不能把尚未完成编码缓存声明为ready。禁止 blocking queued connection。native handler 回调先捕获 binding，再入 caller 线程窄桥；限额8条待处理系统命令，满则拒绝，不补重放队列。桥被接受不表示媒体已播放／seek／调音量成功。

## 4. 公开快照与关联规则

### 4.1 Binding

- `instanceId`：本次服务实例随机 UUID；进程重启／服务重建不复用。
- `viewEpoch`：随机公开 UUID；启停暴露、账号/phase/token变化、隐私重置时更换。不暴露内部账号或 token。
- `trackId`：每个**有效 load**新 UUID，metadata 更新／position 变化不改；重复 SongId、FM、同一队列项重新 load 也有新值。无当前实例为 `null`。与 SongId／QueueEntryId／AudioTrackId 不互换。
- `lyricsRevision`：当前 binding 内十进制字符串计数；歌词文档或状态替换后递增，新load初始 `"0"`，不能凭0推断Missing。seek／普通进度不改变文档版本。
- `sequence`：投影实例内递增十进制字符串。合并更新可以跳号，不是丢失可重放事件、CAS 或源身份签名。计数耗尽必须退役实例，不能 wrap。

内部保存原 SessionToken／LoadToken／owner revision／phase/account fingerprint 用于验证，不输出。仅 SongId 相等不够；metadata 注入也必须带原当前 binding 和合法 Song 关联。没有原 metadata 就保持未知，禁止从任意 musicCompleted 或旧 cache 猜活动曲目标题。

### 4.2 Snapshot v1

| 组 | 字段与语义 |
| --- | --- |
| lifecycle | `availability`: ready/unavailable；backend 未 Ready、隐私屏障或未建立可信观察时 unavailable |
| playback | state: empty/resolving/loading/ready/playing/paused/ended/failed，intent: play/pause/stop；保事实与意图分离。当前没有权威 Buffering event，v1 不杜撰该态 |
| identity/metadata | trackId、可选 providerSongId、可选当前 entryId；title/artist names/album title 仅已有当前 Song 值。未知用 null，显式空保留；v1 不输出封面／分享／音源URL |
| time | positionMs 是**可播 clip-local**；durationMs 可未知且来自实际媒体，不用 Song metadata 猜；preview 原 start/end；songPositionMs = clip position＋preview start；checked arithmetic |
| clock | sampleMonoMs 为投影采样点，positionObservedMonoMs为最新位置到达owner时的记录，均服务进程相对单调十进制string；前者不能重置旧位置的新鲜度。不是媒体解码采样时刻／跨进程共用时钟／Unix时间；客户端插值只是视觉估计，Playing超过1 s未有位置证据必须停插值，不当已听量／报告 |
| settings | 已验证的 application-volume linear 0..1，否则 null；不是系统硬件音量。queue order/repeat、queue/FM模式及waiting/blocked仅真实当前值 |
| lyrics | session state none/loading/ready/failed；document export status；六轨摘要与每轨独立游标、userOffsetMs；providerNoLyric/instrumental 仅原可选显式值 |
| capabilities | 动态 canPlay/Pause/Stop/Next/Previous/Seek/setModes/setVolume；缺证 false。能力计算必须纯读，不调用 next、FM fetch、detail 或改 shuffle RNG/history |
| errors | 公开固定分类，不包含 provider error text／account／URL；诊断与完整失败值留内部 |

原 getters 必须同 caller 线程、无事件重入地采集；提交前后核对 private fence，不一致丢弃。原通知只置 dirty，事件循环尾提交，position 合并到至多4 Hz；切歌／暂停／stop／seek结果／文档更换／隐私屏障尽快提交，不等普通节流。offset 即使没有游标变化也须新快照。新增最终提交接缝需覆盖同步 observer 重入，不能只假设 signal 顺序永远安全。

下一曲数据绝不带上一曲游标或文本。stop/失联/关闭移除当前歌词 binding；ended 可以保留已结束曲目的 document，但游标遵循现有空结果，不自动延长高亮。缺状态或样本过期不持续推算 Playing。UI／外部动画不能回写 position、history 或报告。

## 5. 六轨歌词模型与同步

轨名固定：`original.standard`、`original.enhanced`、`translation.standard`、`translation.enhanced`、`romanization.standard`、`romanization.enhanced`。

每轨包含 availability（missing/empty/present/malformed）、timing（plainText/lines/words）、providerOffsetMs、malformedLines、lineCount。内容是归一化 `lines[{index,startMs?,endMs?,text,words[{index,startMs,durationMs,text}]}]`；未定 start/end 为 null，不把末行尾端造为无穷大；words 为空可是真实逐行／纯文本。v1 暂不导出 credits，明确不是完整原服务元数据。

- availability 和 exporter 状态分开：导出太大／当前不可用不能改称没有歌词或纯音乐。
- 每轨游标为 `lines:[index,…]` 和 `words:[{line,word},…]`；保留同时多行／重叠字，不压成一个 index。游标必须能定位本 binding、document revision 下的行字。
- clip-local 位置用于外部 seek／媒体条；原曲 songPositionMs 用于歌词。查询公式是 `songPositionMs − providerOffsetMs − userOffsetMs`。providerOffset 在领域中正值表示延后；不再使用原 raw LRC offset 符号。
- userOffset 限现有 ±120,000 ms；轨时间／provider offset 保现有7日界限。暂停／ready保留合法当前游标，失败／ended等使用现有行为；纯文本无伪时间。
- 原文、翻译、罗马音六轨分别定位，无猜测跨轨词级对齐。客户端可由 word 时间与采样位置绘制高亮比例，但只是呈现，不成为第二权威时钟。
- 读取／订阅没有再次请求歌词的入口。Loading/Failed/Missing 等原样可见；增强轨缺失不能复制标准轨假装逐字。

## 6. 本机 IPC v1 协议

### 6.1 传输与发现

优先 `QLocalServer/QLocalSocket`：Unix 使用 filesystem Unix-domain socket，Windows named pipe；不是 Qt Remote Objects、D-Bus 私有扩展、Node 服务或 TCP。

本轮冻结**线协议**；各平台 Qt/native transport 是否满足权限门禁另验。保留窄 `LocalPeerTransport` 基础设施接口：不满足门禁不能静默退化到开放 socket／TCP，可用 native local implementation 补足后再宣布支持。

显式启用后创建用户私有 runtime 子目录及 `observation-v1.json` discovery manifest（mode0600／等效DACL），包含 schema、instanceId、endpoint、pid提示，不含授权 token／歌词。目录mode0700／等效DACL，真实 app ID 命名空间仍待维护者冻结，开发占位不能发布。Linux 使用经核对的 XDG_RUNTIME_DIR；macOS 使用核对后的用户 runtime/temp root；Windows 使用核对后的用户 runtime root和随机 pipe 名。环境变量／pid／随机名字均非认证。路径须长度可用、owner正确、无不受信任 symlink/TOCTOU；OS正规root的canonicalization与私有leaf的no-follow检查分开。

不抢占另一个实例、不盲调用 removeServer、不删未知锁／socket；只删除本实例实际拥有的端点／manifest。多实例用独立 instanceId manifest，不共用单实例“激活／打开URI”通道。manifest失效仅报 unavailable，客户端不得因此启动播放或循环唤醒后端。

### 6.2 Framing 与 JSON

每帧 `4-byte unsigned big-endian byte length + exact UTF-8 JSON object`，length 不含prefix。适应分包／粘包；无BOM／压缩／二进制歌词／HTML。request body ≤4 KiB，response/event body ≤1 MiB；先验长度再分配。UTF-8必须严格有效，request nesting ≤8、重复key／多JSON值／未知method或字段／非法类型／越界计数拒绝。QJsonDocument 默认行为不足以证明重复key／深度门禁，codec须实现有界预检并测试，不能只用toString/toInt默认值。

信封：`v:1`，`kind:request/response/event`。request `id` 为1..64 ASCII字母数字/_/-，当前连接待处理期间不复用；response携同id及`ok`。event 无request id。counter/opaque IDs 用string，有限ms及index用安全范围整数；请求中的整数使用十进制整数词法，不接受小数或指数（包括1.0/1e0），防止Qt double舍入；不以double承载64位provider ID或sequence。请求严格；响应允许客户端忽略新增未知字段，但重大语义／frame改变升级major。v1不支持未知枚举的猜测或自动协议fallback。

### 6.3 Methods

| 方法 | 行为 |
| --- | --- |
| `hello` | 首帧必需，选择v1，返回支持的methods／tracks／limits及instanceId；不发送歌词正文 |
| `getSnapshot` | 当前**完整**小快照（含六轨当前游标），无官方请求 |
| `subscribe` | topics只允许 snapshot/lyrics/cursor；单连接一份订阅，可替换；先返回当前snapshot基线，再收更新，不回放过去 |
| `unsubscribe` | 清理待发送的订阅更新；不撤回已发送字节，不停播放／取消owner歌词 |
| `getLyricsSlice` | 必须传原instanceId/viewEpoch/trackId/lyricsRevision、轨名、fromLine、maxLines（1..128）；仅当前已解析document，按行有界本地分页，不接受任意SongId |

服务内部在一个 I/O 事件中安装订阅并取 mailbox 基线；保证 response baseline 排在该订阅后续 event 前。后续 event 带 sequence，**state/cursor更新发送可独立采用的完整小快照（极端超限用显式export状态，见下文）**，不是需要重放才能拼出的补丁；合并可以跳号。lyrics event 仅宣告文档版本／摘要变化，不广播整曲文本。订阅不需要客户端已有文档；游标可先收到，客户端在取到匹配版本前不渲染旧文本。

Slice返回原binding、track、fromLine、按原index排序的lines、nextLine（null=该轨结束）、lineCount；至少一行能装入，否则 `ExportLimitExceeded`，不切断一个行／词文本伪造内容。fromLine在0..lineCount内；等于lineCount返回[]/nextLine:null，越界InvalidRequest。maxLines是上限，还受编码字节预算，可返回更少并给真实nextLine。每次都核对binding；切歌／文档替换后的继续页返回 `StaleView`，绝不续页到新曲。正常position/seek不使同一document分页失效。

本地错误固定：UnsupportedVersion／InvalidRequest／NotReady／StaleView／LyricsNotReady／ExportLimitExceeded／Busy／RateLimited。Malformed协议／权限失败可直接关闭；不回显原输入和内部异常。ready中missing/empty轨可正常返回相应availability与[]，不是LyricsNotReady。

除slice外，hello/getSnapshot/unsubscribe只允许common信封及method；subscribe另需topics（1..3个唯一合法topic），slice仅允许表述的7个业务字段，不接受额外flags。hello响应另带instanceId/methods/tracks/limits；snapshot响应另带snapshot；subscribe响应带topics和snapshot。event带topic和完整snapshot；同时命中多个topic只发一次，按snapshot→lyrics→cursor选择首个已订阅topic。失败响应带`error:{code}`，无原输入回显；unsubscribe返回ok，不带snapshot。

示例均为**合成**，省略此前成功hello，只写JSON body，实际必须加长度前缀；O2a仅值codec存在，尚无可连接API：

```json
{"v":1,"kind":"request","id":"r1","method":"getSnapshot"}
```

```json
{"v":1,"kind":"request","id":"r2","method":"getLyricsSlice","instanceId":"00000000-0000-4000-8000-000000000001","viewEpoch":"00000000-0000-4000-8000-000000000002","trackId":"00000000-0000-4000-8000-000000000003","lyricsRevision":"1","track":"original.enhanced","fromLine":0,"maxLines":64}
```

```json
{"v":1,"kind":"response","id":"r2","ok":true,"instanceId":"00000000-0000-4000-8000-000000000001","viewEpoch":"00000000-0000-4000-8000-000000000002","trackId":"00000000-0000-4000-8000-000000000003","lyricsRevision":"1","track":"original.enhanced","availability":"present","fromLine":0,"lineCount":1,"nextLine":null,"lines":[{"index":0,"startMs":0,"endMs":2000,"text":"合成 雲","words":[{"index":0,"startMs":0,"durationMs":1000,"text":"合成 "},{"index":1,"startMs":1000,"durationMs":1000,"text":"雲"}]}]}
```

getSnapshot响应的snapshot required groups如下；没有值用null而不是删字段。计数/ID为string；timing限定0..604800000整数ms，负offset按既有界限；summary与cursor分开nullable。O2a已固定[完整ready golden](../../tests/fixtures/playback_observation_snapshot_v1.json)与[unavailable golden](../../tests/fixtures/playback_observation_unavailable_v1.json)，全部合成、按本字段表验证；这是值编码证据，非运行服务兼容证据。

| required field | v1 wire shape |
| --- | --- |
| instanceId/viewEpoch/trackId/sequence/lyricsRevision | 三UUID（trackId可null），两十进制unsigned64 string；entry另在track中 |
| availability/playback | ready/unavailable；`{state,intent}` 两个§4枚举，无真实当前时empty/stop |
| track | null或`{providerSongId,entryId}`，两字段各nullable string；不是metadata |
| metadataExportStatus/metadata | unknown/ready/limitExceeded/invalidData；null或`{title,artists,album}`（string/array/string各nullable，v1 names-only） |
| timing | `{positionMs,durationMs,songPositionMs,preview}`；无当前position与songPosition为null；duration nullable，preview null或`{startMs,endMs}` |
| clock | `{sampleMonoMs,positionObservedMonoMs}`，后者nullable，两者string且同实例相对原点 |
| settings | `{volumeLinear,mode,order,repeat,fmWaiting,fmBlocked}`；volume nullable 0..1，mode queue/personalFm，order sequential/shuffle、repeat off/all/one可null（FM不强造队列模式），两flags bool |
| capabilities | `{canPlay,canPause,canStop,canNext,canPrevious,canSeek,canSetModes,canSetVolume}`，全部bool、未知false |
| failure | null或`{code}`，固定为InvalidRequest/StaleContext/Canceled/Unavailable/ProtocolKeyMissing/Network/Timeout/RateLimited/InvalidResponse/UnsafeSource/Expired/TooLarge/Storage/UnsupportedFormat/Decode/NoOutput，无provider code/text |
| lyrics | `{state,documentExportStatus,cursorExportStatus,userOffsetMs,providerNoLyric,instrumental,tracks}`；none/loading/ready/failed；document notReady/preparing/ready/limitExceeded/invalidData，cursor notReady/ready/limitExceeded/invalidData；offset整数，两个provider flags nullable bool |
| lyrics.tracks | 恰好6个固定轨key；每值`{summary,cursor}`。summary null或`{availability,timing,providerOffsetMs,malformedLines,lineCount}`；cursor null或`{lines,words}`。非null摘要含全部字段，plainText/lines/words不是来自字段名猜测 |

snapshot/cursor事件是此同一shape。完整wire快照超限时须先移除超限的metadata或cursor部分并分别标limitExceeded/null，保状态／binding与全部六轨key；**null不冒充空游标或缺歌词**。摘要不足／字段非法时invalidData，不能截文本/行字。最小合法快照仍无法编码则关闭该client并记限额分类，不影响owner。所有小快照均不包含line/word正文，当前高亮文本通过对应slice定位；允许将来新增窄便利方法，但v1不默默增设RPC。

### 6.4 预算与背压（设计预算，未测性能）

- 客户端≤8；每连接待处理requests≤4，hello期限5 s，未完整frame期限2 s（按首字节计、不被慢滴流无限延后），入站限16 frames/s、burst16，超限有界拒绝／关闭。
- 入站应用buffer≤16 KiB/connection（framer／Qt read buffer／待解析小请求均计入）；setReadBufferSize与framer cap协同，不等于OS buffer也已受控。
- 小快照≤256 KiB（包含极端多重叠游标），slice≤1 MiB；单个无法编码行／坏typed值明确export不可用，不静默删字或改availability。
- 当前6轨document规范化wire content合计≤8 MiB（line/word JSON、摘要及结构计入；不含多次分页重复的信封），每轨仍≤10,000行／30,000words；UTF-16字段及UTF-8/escaping体积分别核对。超额标记exportLimitExceeded，不影响内部播放／歌词。
- 校验、预算累积须checked arithmetic，超额立即停止；在I/O thread分批编码并检查当前binding，不先序列化一个无界巨大document再算大小。
- 只保一个当前不可变document与≤8 MiB共享编码cache，不为每客户端复制／持久缓存旧歌词。待发位置／cursor只保latest；内容页按需，不push全文。
- bytesToWrite＋应用待发frame合计≤2 MiB/connection，8客户端合计≤16 MiB；编码cache单列计入总内存，typed共享值／Qt事件与kernel/native buffer须另做实测，不宣称整个进程≤该数字。
- 未提交的同binding可合并小快照；frame一旦交给transport不丢中间bytes、不交错另一frame。慢客户端5 s无发送进展或达到上限关闭；不能阻塞owner，不自动重连／outbox。无需心跳／自由运行的歌词timer。

## 7. 权限、隐私与清理

**Local不等于安全。** 本包只信任同OS用户，并说明不防同用户恶意进程、管理员／root或已授权消费者保存内容；没有产品级同用户应用沙箱认证。客户端也需验证server所属用户；随机endpoint只是防碰撞，不是认证。

| 平台 | 必须达到的门禁 |
| --- | --- |
| Linux | filesystem socket及用户0700目录／0600 socket，禁用abstract namespace；验证peer UID（native credential guard），不凭socket名字信任对方 |
| macOS | Qt文档明确Unix socket权限flag可能无效，不能照抄UserAccessOption；受保护目录＋native peer UID（getpeereid等实际SDK接口）校验及非owner负测，未验证不启用 |
| Windows | 同user SID DACL＋peer身份验证＋拒绝remote pipe clients＋端点实例独占；QLocalServer允许同名pipe多server，UserAccessOption也不证明remote拒绝／独占，须审Qt实现或native实现并真机验证，不够就保持不可用 |
| 沙箱 | Flatpak/session bus/native沙箱的endpoint发现和访问逐项验证；不为方便授予整个HOME、host filesystem或完整bus权限，失败不fallback TCP |

账号、phase、token或暴露策略变化先递增投影fence，移除metadata/document/mailbox和未提交页／事件；关闭旧IPC连接，旧native命令拒绝，系统元数据撤销／降级。认证中不沿用旧账号内容。旧binding不自动贴新epoch重新发布，须重新建立有效owner关联；这不是取消所有公开查询或停止现有媒体的理由。

已经写入内核／客户端、系统组件缓存或消费者保存的旧字节不能撤回；“关闭／清内存”不是安全擦除。所有frame按原binding标记，不能把旧内容声明为新账号／新曲；客户端必须discard stale版本。内部thread边界在发送前检查最新fence并退役旧mailbox，仍须明确在途字节边界，不宣称物理零泄露。

默认不记录歌词正文、标题、endpoint manifest全文或请求body；日志仅method／分类／计数／限额事件。不得将IPC分发当收听证据／上报。六轨文本只做plain text，不能由URL/HTML/credits执行动作；歌词导出不是音乐／歌词版权许可或批量分发授权。

## 8. 系统媒体 adapters（B-017）

### 8.1 Linux MPRIS

session bus／`/org/mpris/MediaPlayer2`；候选bus名`org.mpris.MediaPlayer2.Cirrus`，app ID与多实例规则在实现前冻结。只有root＋Player，HasTrackList=false，不冒充TrackList/Playlists实现。

- Metadata track object path由load的trackId编码为合法UUID hex字符，例如前缀`/org/mpris/MediaPlayer2/track/t_`，load内稳定；不直接塞带连字符UUID／SongId，空时按规范NoTrack处理。
- position／length用clip-local实际可播范围，ms→int64 µs checked；原曲总时长不能替代preview长度。title/artists/album仅已知值；length未知则省略，v1 artUrl不发布。
- Playing只映射实际Playing；Paused映射实际Paused，Ready且Pause意图可Paused；resolving/loading/empty/ended/failed保守Stopped，不以Play intent假装在播。尚未拥有真实duration/position等必需映射时注册门禁，不编造源值。
- Position自然推进不发PropertiesChanged；Seeked仅实际不连续位置证据，不能由command Ack直接发。新增观测接缝是adapter退出条件。
- SetPosition拒绝stale TrackId、负数／超长按规范无动作；Seek为相对µs，负越界→0、正越曲尾→Next，不误用内部clamp。未知duration/不可seek按能力禁用；向内部ms换算小于1 ms部分向零取整，先做checked signed加法。
- LoopStatus/Shuffle委托ListeningSession.setModes；FM不借此进入伪常规队列。Volume只表示已观测application线性值，负写clip0／超产品范围clip1，不暗中放大；需要volume真实读面后才能宣称完整Player接口。
- Rate/MinimumRate/MaximumRate=1.0；其他rate写入拒绝。CanControl为注册生命周期内固定真实桥能力，不逐曲乱发其PropertiesChanged；其余Can动态发布。桥失效退役服务，而不是保留假控制。
- headless阶段CanRaise/CanQuit=false，Raise/Quit不捏造窗口／同步退出；OpenUri不实现，SupportedUriSchemes/MimeTypes不声明未实现格式，不注册同媒体键的全局hook。

### 8.2 Windows SMTC

手动通过desktop native interop连接现有播放器，不构造Windows.Media.Playback.MediaPlayer作为第二引擎。宿主提供**本进程拥有的top-level HWND**；不能使用GetDesktopWindow、他进程窗口或假handle。UI尚未接入时用独立平台harness验证宿主接缝，无法提供handle则adapter unavailable，不伪造headless支持。

native controls的enabled/status/display metadata/timeline来自同一投影；button／position请求携捕获binding经过窄桥。COM/WinRT apartment、回调线程、Windows SDK／最低OS／包身份条件、事件撤销与window销毁先做真实工具链PoC；不由Linux条件编译或fake通过声称SMTC可用。暂不publish封面或拿SDK自动播放器的特性当Qt播放器证据。

### 8.3 macOS Now Playing

使用MediaPlayer的MPNowPlayingInfoCenter＋MPRemoteCommandCenter，Objective-C++实现隔离native框架，按实际要求在main run loop操作。提供title/artists/album/实际clip duration及elapsed时间；playbackRate只表示实际播放推进。macOS必须同步更新playbackState，不能只改nowPlayingInfo。

每个command保留handler registration token，关闭时remove handlers／清当前信息，迟到回调不再投递。native success只表示窄桥接受，不等于媒体已生效。SDK/API availability、Qt部署最低系统、包身份／签名／hardened runtime与系统控制实测另列门禁；Apple API availability不是本客户端最低系统支持承诺。

三平台均不添加第二媒体键hook、不把系统读取元数据当播放，不自动启动后端／播放器、不更新官方报告／Together room。

## 9. 分包实施与验收

| 顺序 | 小包 | 必须先通过的退出条件 |
| --- | --- | --- |
| O1 | Core投影／观测接缝及fake sink | coherent sample/reentrancy、volume/offset/discontinuity真实反馈、position到达时间与投影时间区分、来源metadata未知、原load与same-token phase/account屏障、Core实际依赖含MOC；无JSON/native/网络渗入 |
| O2 | v1 codec／framer／fake local transport | 固定完整schema和golden fixtures，分包粘包、非法UTF8/重复key/深度/长度/unknown/宽ID/越界；六轨重复/overlap/preview/offset/missing-empty-malformed与所有预算 |
| O3 | Linux安全本机IPC＋合成client示例 | UID／路径symlink／stale端点／限额／慢读／半帧／切号／关闭／重连、8客户端、官方新增请求0；示例只用合成内容，不装SDK或读取账号 |
| O4 | Linux MPRIS | 可选Qt DBus代码构建见§9.8；不新增测试代码。真实session bus／权限／单位／stale SetPosition／Seek越界／Seeked／Volume/Can/Rate/root与GNOME/KDE/Wayland/X11运行门禁另记，playerctl只是开发工具 |
| O5 | Windows SMTC＋安全IPC | Windows真实构建/运行；owned HWND/COM生命周期/SDK、remote-pipe拒绝／sameSID／独占、系统控件、媒体键无双触发 |
| O6 | macOS Now Playing＋安全IPC | macOS真实构建/运行；native注册撤销、playbackState、身份权限负测、系统控件／多应用争夺、最低OS／打包边界 |
| 后续 | 队列／历史持久化与启动恢复、流式起播／设备恢复 | 独立契约，不把这些功能塞进IPC server；仍需保持启动不自动出声，UI最后 |

关键回归：A→B→C乱序及旧歌词晚到、重复SongId/重复entry/FM、同曲新load旧SetPosition、seek暂停/预览起点、userOffset改但游标不变、ready→failed/ended、连接中途绑定更换、分页中换曲、认证中same-token账号/phase改变、shutdown待发送与旧native回调、observer重入、8慢读客户端不影响owner、limits±1。RPC-like未知method如play/openUri/queryLyrics必须拒绝且产生业务请求／播放器命令0。

大数据fixture检查每轨最大行字、编码escaping膨胀、多重叠游标、单行无法装入、aggregate wire cap；typed异常输入也须有界失败。只保合成文本；不能用真实歌词做repo golden fixtures／日志。

三平台分别报告 I/S/L/G/P/X与构建／runtime evidence，native fake只能S，缺工具链/设备维持G，不以Linux unit代替Windows/macOS验收。无必要实网，禁止自动恢复NetEase窗口／读取凭据。原设计轮仅文档一致、relative link路径、JSON示例及diff等有限验证，没有运行Qt；后续O1单独实测见§9.1，不借历史pass作为新功能证据。

### 9.1 O1实施与限定验收（独立历史子包，代码保留）

- [Core类型](../../src/domain/playback/playback_observation_types.h)、[投影](../../src/application/playback/playback_observation_publisher.cpp)、[唯一命令桥](../../src/application/playback/system_media_commands.cpp)已加入。只在owner/facade共同caller线程使用；`changed`是同线程历史完整值，不能直接连无界跨线程queued payload。fake sink采用单latest槽＋单wake并在消费前复验；生产IPC/native mailbox留O2–O6。
- 事件循环尾取最终同源快照，普通position≤4Hz；身份/状态/offset/文档/实际seek反馈优先。signal发出中同步读取只得到上一份完整样本或unavailable，不混新位置与旧游标。metadata显式带当前binding/Song关联；无自动详情/图片请求，names-only；超限或坏文字清metadata并标状态。
- 原token/load/phase/account只保私有。session通知立即退役旧load并改epoch，包括同token phase往返、第一次公开采样前改变；不能因回到原账号/phase重贴旧歌词，须显式新load。关闭退役binding，不向owner发送stop/signOut。已交给同进程消费者的值不能撤回。
- `PlaybackSnapshot`增加position/seek revision与可选真实输出volume；owner按position revision到达记录steady-clock时间，sample时间不刷新它。user offset有独立通知。Pause接受不再改成实际Paused。Qt backend从QAudioOutput读取volume，从原position信号捕获后延迟发布、有原load屏障。
- [保守seek判定](../../src/infrastructure/media/seek_observation.h)只检查request后的首个native位置：目标匹配且真变化，paused/倒退/超elapsed+250ms自然推进范围才确认；小或迟到forward seek可能不确认。自然播放稍后碰到目标不制造SeekConfirmed。此为Qt报告的位置不连续，不是声学/解码效果证明。
- 六轨availability/timing/offset/多行多字原样独立保留，credits从公共副本移除；内部owner不变。Core额外预算：合计1,048,576 UTF-16文字单元、每轨10,000行/30,000字、合计32,768游标项；拒绝坏UTF16/NUL/枚举/时间/关联并区分limitExceeded/invalidData。游标验证有界集合查找、只读Qt容器不额外detach；**不替代O2的UTF8/escaping/8MiB wire预算**。
- 系统命令只委托ListeningSession，≤8pending；原binding＋load/control revision、执行时能力复验。较新的用户pause/resume/seek/volume/modes取消旧ticket；桥自身scalar命令仍FIFO，不跨reentrant新意图改签。accepted只表示本地接受，绝非播放/seek/音量效果。IPC没有接到此命令桥。
- [观测测试](../../tests/unit/playback_observation_test.cpp)37pass；12个affected套件合计228pass/0fail/1skip（media默认实际音频smoke未授权、跳过），其中media的10个seek-proof合成行包含27pass总数。另3个选定runtime函数（playback两行、musicQueries、wiki）6pass/0fail/0skip，init/cleanup已计入；不累加历史中间轮。
- Debug实际build0/warnings0；30个changed/adjacent fresh all-server LSP0；16个application实际object含MOC，Ninja VALID summed deps8122/unique609，禁止模块/JSON/OpenSSL/infrastructure0。保留#92 cpp-check输出解析失败；LSP0不替代该检查。最低Qt6.10、Release、全51、实际设备/decoder/三平台权限及产品入口均未验。format与文档最终检查另记录ignored O1产物，不覆盖原设计/wiki产物。
- O1轮只推进Core，无额外官方请求/实网、SDK/DBus/COM/Apple框架安装、IPC端点、第二播放器/时钟/报告器、UI/entry接线、push或stash恢复。后续O2a只推进纯值codec，不改变这些禁限。

### 9.2 O2a实施与限定验收（历史子包，当时O2未完成）

- [codec](../../src/infrastructure/ipc/playback_observation_codec.h)、[有界编码](../../src/infrastructure/ipc/playback_observation_codec.cpp)、[严格请求／framer](../../src/infrastructure/ipc/playback_observation_request.cpp)已实现；独立静态target只用domain/Qt Core。无QLocalServer/QLocalSocket、端点发现、线程/定时器或产品入口接线。
- 五个只读method的值解析；4KiB请求、严格UTF8/no BOM、escaped重复key、容器depth8、合法surrogate、精确字段/类型、非空规范小写UUID、1..64 ASCII correlation id、完整uint64十进制string。整数词法拒绝小数/指数，含舍入成整数的恶意小数；没有合法id时error factory返回空object，调用方必须关闭，不能发id:null或回显输入。
- framer先验4-byte大端长度，仅保一个≤4KiBpartial请求；一次产一个body和consumed，不复制整批输入。首字节起2000ms固定deadline、不slow-drip延长；零长/超长/时钟回退/超时sticky close。**连接hello-first/5s期限不属于framer，尚未实现。**
- 完整snapshot及16个固定PlaybackError编码；unavailable清空track/metadata/lyrics/capabilities等内容而保实例/epoch/sequence/采样时刻。未知保持null，六轨摘要/游标分开状态；metadata无ID/URL、歌词无credits/provider raw错误。非法typed基础值拒绝；坏/超限文档、summary/cursor明示状态，不改变owner。
- immutable PreparedLyrics只由有界factory生成，保存原binding/revision/song、每轨摘要和完整行预编码；单个factory的规范化六轨含结构/摘要≤8MiB、UTF16≤1Mi、每轨10000行/30000words、cursor总32768，小snapshot≤256KiB，slice≤1MiB/128完整行。单行不能装入明确ExportLimitExceeded；不给截断文字或新曲续页。snapshot/slice均核对当前binding/revision/关联与摘要，非法summary不给正文。8MiB normalized cap精确±1与独立重构字节计数已合成测试。
- O2a时点prepare是**有界同步纯值factory**，不是可中断分批I/O任务/共享cache owner；不放在媒体owner上作为生产处理。函数的current值是调用方提供的历史值，不自行证明消费时私有fence。调用方能留旧shared_ptr或已经编码的bytes，codec不能撤回它们；O2b/O3必须统一退役/限额及消费/发送前复验。
- [测试](../../tests/unit/playback_observation_codec_test.cpp)125pass/0fail/0skip，包括golden、全部16错误、请求/帧/编码边界、Unicode原文保持、六轨完整行分页、stale/错摘要/limit、fake分块consumer官方/媒体请求0。**fake只验证framer→decode→纯值snapshot/slice，不是实现了连接状态机或server。**首次107、补边界123均为中间历史，不相加。
- 本轮5个selected suites合计216pass/0fail/0skip（125codec/37observer/20listening/8facade/26music），另3个选定runtime函数6pass；Debug build0/warnings0，11个changed/adjacent fresh all-server LSP0。不是全52/Release/minQt/真实peer/音频/平台或服务验收；#92cpp-check解析独立保留。原O1的228/1skip仅历史子包，不充当前总量。
- **O2a当时剩余O2b**（后续实现另见§9.3）：fake local connection hello-first、correlation pending≤4/duplicate/rate、subscribe基线先于event与topic优先级、unsubscribe/close、单latest/单wake、增量编码和binding取消、缓存/帧退役、慢消费者/partial write/backpressure与共享预算。fake chunk示例不满足这些退出条件；任务O2继续in_progress。再做O3 native peer/发现与真transport，不能因codec通过默认启用endpoint。

### 9.3 O2b逻辑连接／fake transport实施与限定验收

- [逻辑连接与mailbox](../../src/infrastructure/ipc/playback_observation_connection.h)／[实现](../../src/infrastructure/ipc/playback_observation_connection.cpp)只用domain/Qt Core，与codec同一静态target；**没有QLocalServer/Socket、peer credentials、线程对象/计时器、端点manifest、入口/native接线**。I/O调用方提供单调毫秒、非阻塞write sink、定时期限和有界调度；不是第二歌词/播放时钟或自动业务调度器。
- 单producer/单I/O消费者mailbox，Qt mutex内仅typed共享值/标量、单latest/单wake；private fence不导出。owner必须同步调用retire，包括首样本前/phase往返；publish不能自行更换epoch，换view必须由**原owner私有来源重新核实**后显式beginView，旧epoch即使更高sequence也不能经publish回流；同实例sequence不得倒退/复用。此seam仅fake测试，真实Core→I/O composition和private通知接线在O3；不能将beginView当来自历史changed值的推测或授权RPC。
- ≤8client（第9拒绝），首hello/5s、partial frame首字节2s固定deadline；≤4持有correlation（包括未完整提交的response）、duplicate关闭；input满返回consumed并停止解析，不复制/保留调用方remainder，O3须限制其Qt/应用总read buffers≤16KiB。16frames/s＋burst16 token bucket，无输入echo；未知控制/登录/文件/额外查询拒绝，无application/Network/media写缝。
- 一次pump至多每client处理1请求、一个共享encoder最多128行/字结构记录；分步job未ready不导出部分正文。prepare同步便利factory复用同job仅供值测试，不在媒体owner上跑。单个text字段仍最多1Mi UTF16，**不是128字符或恒定时间/性能保证**；UTF8转换/容器capacity/allocator/typed共享值等临时内存另验，不宣称整个进程≤wire预算。
- 只持一个当前共享encoding或一个partial job，换binding/revision/export态/隐私时清旧cache与job；每次消费/执行及非阻塞write admission复验private fence。未开始的旧snapshot/page变StaleView，未开始event重取latest；旧partial frame不拼新内容，而关闭该client。已交给transport/native/kernel/消费者的字节不可撤回；write期间barrier只关闭/清余下，不能宣布物理零泄露。
- subscribe返回安装时完整snapshot基线，之后event；7种topic组合明确snapshot→lyrics→cursor优先级，一更新只发一次。事件只保dirty位和共享latest，position-only不触发lyrics/cursor订阅；无历史回放/按client复制歌词cache。replacement/unsubscribe移除未提交event，partial event先完整结束、不切中间bytes。pending id直到整个response提交才释放。
- 每次write≤64KiB、按剩余空间裁小；应用全active frame（**包括已提交但仍被QByteArray保留的prefix**）＋排队replies＋sink.pendingBytes≤2MiB/client，8个≤16MiB；生成/加入response先预检，不用未发送尾部数冒充实际保留字节。5s无真正drain关闭，新enqueue或仅送入不drain的sink不重置慢期限；不阻塞owner，不自动reconnect/outbox/poll。
- [新连接测试](../../tests/unit/playback_observation_connection_test.cpp)43pass/0fail/0skip：所有hello cuts/流水线、deadline、4/5pending/duplicate、16/17burst/refill、8/9client及8慢client、7topic基线/优先级/latest/替换/退订/partial、revision/新load旧页、首样本前/消费前/写入中隐私与旧epoch回流、retained-prefix精确2MiB与多页实际完整性、incremental/job取消/线程producer/关闭。target实际link只有codec/domain/Core/Test，无application/Network/SQL/media，因此不能执行播放器/业务请求。
- 当前6个selected suites合计259pass/0fail/0skip（43connection/125codec/37observer/20listening/8facade/26music）；另3选定runtime函数6pass。两个新协议CTest注册实际执行通过，53登记不是全53；历史125/216与O1 228/音频skip不相加。Debug build0/warnings0，Clang同真实flags syntax0；初次GCC通过但Clang nested aggregate traits/emplace失败，改显式Client构造并复验，**fresh LSP0不曾替代Clang失败**。首次连接37pass/1fail为fixture未更新doc revision，修fixture而非放宽版本屏障；失败日志保留。#92 cpp-check解析仍独立未闭、CLI不可用未安装；外部cache读取拒绝未绕过。
- 最终工作树39文件（原O2a 36保留＋connection头/实现/test 3新增），26 C++/2 CMake/9 Markdown/2合成JSON，21modified/18new、staged0。40个changed/adjacent fresh all-server LSP0；actual Ninja VALID：application16objects含MOC 8122 summed/609unique禁止0、codec4objects含MOC1521/516无application/Network/native禁入。format26/diff两种0，403relative paths missing0/有限literal flags0，非anchors/全面安全扫描；fixed440/Python15/API header hash/53 show-only核对。CMake工具一次unavailable未当clean，实际configure/build0；有意义的topic bit测试在7独立fixture/GCC/Clang/fresh诊断下验证，迟到Unnecessary bit operation标false-positive，无源码suppress。HEAD1a36f78/exact stash/entry/QML不变。独立ignored observation-o2b-*与playback-observation-o2b-check.json，不覆盖O1/O2a/wiki历史；productionCompositionVerified/realEndpointEnabled/platformTestsRun/liveRequestsRun均false。
- O2的便携codec/framer/fake逻辑传输退出条件限定I/S已覆盖；真实Qt/native传输、private owner组合调度/缓冲、peer验证/发现/权限与部署性能仍O3–O6 G。下一O3，不默认启用、不新增官方请求/外网/依赖/产品入口或第二owner，所有改动仍未提交。

### 9.4 O3a Linux私有资源／kernel UID接缝（历史子包，O3整体未完成）

- [Linux primitive](../../src/infrastructure/ipc/linux_observation_endpoint.h)／[实现](../../src/infrastructure/ipc/linux_observation_endpoint.cpp)独立Linux条件static target，仅domain/Core，不连application/codec/Network/media。enabled默认false，root/namespace/instance无发行默认值；仅隔离QTemporaryDir＋合成ob-test namespace harness显式创建真实filesystem listener，不接产品或Core、不提供观察响应。正式app ID未冻结、不发布开发占位；没有XDG/HOME/temp自动fallback或TCP/abstract namespace。
- 配置的OS runtime root先canonicalize后native open/pin，要求current euid owner与exact0700且不支持setuid；private namespace/instance只openat O_DIRECTORY/O_NOFOLLOW/CLOEXEC，identity dev/inode/type核对，实例mkdir exclusive、碰撞不抢占。Linux kernel-owned /proc/self/fd alias将bind锚定private leaf，仍核验正常可见socket pathname的sun_path上限；proc/fchmodat nofollow/renameat2不可用就失败，不替换权限/transport。
- Socket0600后才listen、backlog8/nonblocking/CLOEXEC；manifest≤4KiB，0600 exclusive tmp＋renameat2 NOREPLACE，仅schema/instanceId/endpoint/pid提示，不含授权、音乐或Cookie；write至多16次（含EINTR），filesystem setup/cleanup/guard仍可能阻塞，**只能I/O线程调用，非owner非恒定时间承诺**。manifest fd保留到清理以防inode复用误删；只unlink dev/inode/type匹配的本实例文件与empty owned目录，不递归/删未知文件、别实例、锁、替换node，也不盲removeServer。共享namespace最后可留空，保守不冒充最后实例所有权。
- 每call至多accept1；当前root/namespace/instance的0700 owner/identity、socket/manifest的0600 owner/identity再次核验；仅connected AF_UNIX/SOCK_STREAM、kernel SO_PEERCRED精确结构长度/pid>0/uid==euid的fd交给caller。client必须也核验server UID，随机name/PID不是认证。已accept fd属于caller，helper close不替caller关client或提供隐私/期限/调度；这些在O3b组合。已交内核/消费者字节仍不可撤回、不防同user malware/root/admin。
- [新Linux harness](../../tests/unit/linux_observation_endpoint_test.cpp)32pass/0fail/0skip（data/init/cleanup计入）：默认off无side effect/坏namespace/0755与symlink拒绝/restrictive umask rollback/已有leaf与live collision保留/长path回滚/atomic manifest四字段与模式/双向native UID与fork跨process真实peer PID关联/5-byte合成数据/nonblocking CLOEXEC/多实例/未知与替换manifest、socket、leaf不误删/5种当前权限坏值拒绝accept/32次失败无FD增长/无效与datagram拒绝。本机kernel same-user正测不是另一OS用户负测；未改uid、未安装依赖/DBus/native SDK/实网/产品入口。
- 本轮3selected200pass/0fail/0skip（32native/43connection/125codec），不与O2b259＋runtime6相加。Debug/GCC0 warnings0、真实flags Clang实现/test0、3fresh all-server LSP0；AUTOGEN .moc not-found先阻塞，新编译数据库复制后实际build＋fresh复验，不删除moc或压诊断。#92新路径仍出现cpp-check parse error，CLI不可用未装，LSP/Clang0不关闭此gap；CMake LSP unavailable也不当clean，实际configure/build与三个限定CTest另验。native2objects含MOC actualNinja VALID 551summed/550unique禁入0；54show-only非全54、107ops/26families/44FR不变。
- O3a最终工作树43文件＝O2b原39＋Linux header/实现/test 3＋根README前门1（此前9 Markdown包含docs/README而非根README）；29 C++/2 CMake/10 Markdown/2合成JSON，22modified/21new，staged0。format29/两种diff0、468relative paths missing0/有限literal flags0，非anchors/全面安全扫描；fixed440/Python15/configure/三个限定CTest均0，API header hash与HEAD/exact stash/entry/QML不变。独立check-playback-observation-o3a.py／playback-observation-o3a-check.json：nativeTemporaryHarnessVerified true，O3Complete/productionCompositionVerified/productEndpointEnabled/otherUserNegativeVerified/discoveryClientVerified/liveNeteaseRequestsRun/systemMediaAdaptersRun均false。检查器初次把根README误归旧39、期望42，按实际Git边界校正为43，不删文档来满足数字；batch wrapper zsh status只读错误改局部rc后复验，不算前次exit成功。
- **O3仍进行中**：尚无verified discovery reader/消费者hello instance关联、完整另一真实OS user负测、XDG生产选择/namespace冻结、private owner retire/beginView同步组合、bounded真实读写/wakes/期限/16KiB输入与2MiB pending证明、thread关闭生命周期、沙箱/发行/Qt6.10/Release性能矩阵。没有可消费播放观察的server，正式端点default off；O4–O6/MPRIS/SMTC/macOS门禁不关闭。工作树仍全未提交/未staged，HEAD1a36f78/exact stash/entry/UI不变，独立observation-o3a-*产物，不覆盖O2b历史。

### 9.5 O3b1 owner私有通知／live factory／单票wake组合（历史子包，彼时I/O未完成）

- [owner feed](../../src/infrastructure/ipc/playback_observation_feed.h)／[实现](../../src/infrastructure/ipc/playback_observation_feed.cpp)独立Core infrastructure static target，允许向application/codec链接，不反向塞进application；default off、自建单producer mailbox、没有socket/JSON/encoder/文件/timer/命令/查询/Runtime或产品入口接线。当前是owner→mailbox组合的限定I/S，不是完整owner→真实socket service。
- Publisher新增同步contextRetired旧**公共**binding通知，session token/account/phase、backend离开Ready、explicit exposure policy与close/destroy在coherent flush前清旧document/metadata并退役source；首样本前也有效。Feed direct连接，不把历史changed参数作授权；每次从owner-thread validatedExport()重新核验原私有session fingerprint/原owner source/load与当前coherent sample，只有此活源factory结果可beginView/publish。factory拒绝foreign thread、无sample或private barrier emission重入中；nested capture需等屏障结束后下一coherent发布。
- 原retired load保留，新增private high-water load-intent boundary覆盖pending owner与先前backend token跨phase-back变化；control-only意图不重授权，local rejection rollback/equality保守拒绝，不改owner接受/回滚顺序、不导出此revision。关闭暴露后重新启用不把老内容贴新epoch，需新的原owner load intent；空状态只导出已核验空快照。所有owner playback/歌词命令仍唯一既有链，policy/retirement不停止媒体或自动重播/补查询。
- 只共享一个当前immutable document；相同binding/revision复用shared_ptr，owner即刻退役清producer引用，已交consumer/kernel的字节不承诺撤回。ObservationWakeTicket原子单票：wakeRequested无payload，最多一个queued wake（外加可能执行中的callback）。**I/O只能在该wake callback开始、pump/take前ack；periodic pump/take不得替尚未出队的通知ack**，避免lost wake或无界排队。实际Qt/native I/O receiver与调度仍下一O3b2验证，不拿纯票据测试证明真实thread/buffer预算。
- [新feed tests](../../tests/unit/playback_observation_feed_test.cpp)20pass/0fail/0skip：default off/首样本前、共享doc/latest和scalar coalescing、phase/原token/同token账号变化三data行的原私有来源与control-only/旧changed replay、policy off/back、新原owner load、旧changed槽重入与nested private barrier采样、伪造参数不能作授权、fake engine发送前fence关闭、backend shutdown/close/destroy/one-shot/foreign thread与I/O ack契约。首轮15pass/1fail为fixture误将take当pop；次轮16pass/1fail为fixture等待live getter而非post-barrier真实source发布，按真实契约加binding/sequence/无forged/wake与source输出等待；两个失败XML/log保留，不弱化epoch/负例/MOC/fatal。
- 本轮8selected311pass/0fail/0skip（20feed/32Linux/43connection/125codec/37observer/20listening/8facade/26music），另3选定runtime函数6pass，非历史累加。Debug/GCC0 warnings0，实际flags Clang publisher/feed/test0；52 changed/adjacent fresh all-server LSP0，非cpp-check成功。STOP optional被误读bitfield：真实Clang AST两个FieldDecl isBitfield false，显式OptionalDocument别名保留原类型、无suppression；m_retiredRevision真实声明遗漏立即补声明后build/fresh复验，.moc not-found先执行AUTOGEN并刷新真实编译数据库，不删除或压诊断。#92 parse/CLI unavailable和CMake LSP unavailable独立保留；实际application16objects含MOC8124summed/611unique无JSON/Network/infra禁入，feed2objects含MOC1113/567无Network/native/SQL/media禁入。
- 当前Linux55注册非全55，107ops/26families/44FR不增。**O3仍进行中**：下一O3b2 private producer＋真实资源/socket/IO receiver的bounded调度/read-write buffers/期限/16KiB与2MiB/lifecycle、verified discovery reader/hello instance关联、正式app ID/XDG选择/另一真实OS用户负测、沙箱/发行/Release/minQt/性能门禁；产品端点default off，O4–O6仍G，无新依赖安装/NetEase请求/UI/paused/stash/commit/push。独立observation-o3b-*产物，不覆盖O3a/O2b历史。

### 9.6 O3b2 Linux真实有界I/O／严格发现客户端（参数化限定I/S，发行门禁仍G）

- [LinuxObservationIo](../../src/infrastructure/ipc/linux_observation_io.h)／[实现](../../src/infrastructure/ipc/linux_observation_io.cpp)与[LinuxObservationService](../../src/infrastructure/ipc/linux_observation_service.h)／[实现](../../src/infrastructure/ipc/linux_observation_service.cpp)新增独立Linux service target。参数化显式start、enabled默认false、无发行namespace默认值；service只在owner线程重建public instance/建立private feed并move独立worker到一个QThread，所有filesystem/setup/guard/cleanup/accept/read/write/JSON/encoder在I/O线程，不在owner执行wait或blocking queued invocation。没有产品Runtime/main/QML接线或默认监听。
- start是one-shot；cancel谓词用shared atomic shutdown，close同步退役producer/source、异步关闭全部client与自身资源；thread完成通知与关闭ticket独立于service对象寿命，析构不等待。**应用必须在退出event-loop前排空shutdownTicket.complete**，不能析构后立刻退出进程并声称已清理。resource helper不持客户端，service/I/O负责fd/notifier/engine销毁；不删未知资源或别实例。
- `fromXdg(enabled, explicitNamespace)`仅显式选择XDG_RUNTIME_DIR；default off不读它，missing/unsafe root是失败，不选HOME/temp或猜发行ID。权限/canonical/pinned验证仍在I/O线程。测试仅使用短路径、自己拥有的隔离runtime root；正式namespace、真实发行XDG/沙箱仍待维护者确认与部署验证。
- nonblocking/CLOEXEC Unix fd＋read/write notifier、单票scalar wake接收者与合并单次turn timer；25ms timer只检查传输deadline，不是媒体/歌词权威时钟。wake仅在**真实queued wake callback开始**ack，periodic turn/take不能ack另一张尚未投递的票。每turn≤1accept（含过载）、≤8client、每client≤1recv及≤64KiB write admission、≤1请求执行；共享encoder≤128行/字结构records/step，单字段仍可≤1Mi UTF16，不是常数时间或128字符承诺。无自动重连/扫描/启动/业务poll。
- 每client固定1KiB raw receive storage，不保存第二份remainder；framer≤4KiB，最多4个已解码typed pending、保守按1KiB/slot计入input（不保留原JSON body），总计及部分缓冲期限逐turn复验≤16KiB。固定2s首字节/未消费input期限和5s hello/drain期限使用QElapsedTimer，不随drip/enqueue延长；第9client关闭，不放宽peer或路径检查。
- Linux sink采用MSG_DONTWAIT/MSG_NOSIGNAL与TIOCOUTQ；kernel记账可能含非payload开销，保守持有累计admitted debt，**只有kernel队列完全排空才释放此债务**。队列实际减少增加monotonic drainRevision（倒退拒绝），部分drain可作为慢期限进展；仅入队不算drain。真实上限包含whole retained active QByteArray的已提交prefix＋排队replies＋native debt，≤2MiB/client，八个≤16MiB；不把tail或内核误差当额外空间。源/临时/容器capacity与wire预算不同，不推全进程内存或部署性能保证。不能撤回已交kernel/consumer的bytes。
- publisher `restartExportInstance()`重建public UUID/sequence/clock origin，保留原private load/intent retirement及exposure policy，丢弃旧或barrier nested capture的load/document/metadata关联；旧position arrival不伪造成新时钟的fresh observation。新实例不能撤销退役或给旧load重授权。real engine消费/执行/发送与native sink发送前都复验private fence，phase-back/source关闭不能回放旧内容；instance变更停止旧listener。
- [严格发现client](../../src/infrastructure/ipc/linux_observation_client.h)／[实现](../../src/infrastructure/ipc/linux_observation_client.cpp)独立target仅native/codec/domain/Core，**不连application或Network/media**。显式root/namespace/instance，无扫描/fallback/自动start/reconnect；private目录/manifest/socket的owner/mode/no-follow/dev+inode+type及保留fd逐阶段复验。manifest≤4KiB strict UTF8/BOM/duplicate/depth/整数语法/schema/四字段/实例关联，endpoint仅选中的固定native路径，PID只是提示。两侧SO_PEERCRED同UID，不信UUID/PID/环境作认证。nonblocking advance每次≤1send/recv、hello固定5s，验证v/id/methods/six tracks/固定limits/instance后一次性transfer fd；transfer前再次验证路径/kernel peer，失效关闭且不能复活。caller此后拥有fd与消费策略；没有UI/render、控制RPC或历史缓存。
- 三个新[test I/O](../../tests/unit/linux_observation_io_test.cpp)13／[service](../../tests/unit/linux_observation_service_test.cpp)12／[client](../../tests/unit/linux_observation_client_test.cpp)25＝**50pass/0fail/0skip**（含data/init/cleanup）：真实hello/snapshot/slice/event、分片/合并请求、隐私关闭/权限坏值/期限、第9client、一个与八个真实不读socket的背压、callback-start ack/periodic pump单票、live owner→独立thread组合及strict client、重入instance/document关联、sticky retirement、default off/close-before-start/析构/8次shutdown FD、missing XDG/no fallback、manifest非法/替换/symlink/hello错实例/扩budget/移交前变更/32失败无FD增长。尚无另一真实OS用户负测；同UID与fork正测不替代。
- 最终11selected Debug **361pass/0fail/0skip**＝50新＋20feed/32endpoint/43connection/125codec/37observer/20listening/8facade/26music；另3runtime functions6pass。8selected Release **307pass/0fail/0skip**＝50新＋32endpoint/43connection/125codec/20feed/37observer，两组不相加，也不拿历史311/259/200充本轮。两配置7个限定CTest通过，实际58登记非全58；仅本Linux/Qt6.11.2，非minQt6.10/整个Release产品/发行包/设备或平台验收。
- Debug/Release configure/build0、GCC warnings0；真实flags Clang11 translation units0；52 unique changed/adjacent fresh all-server LSP0（请求53含一个重复path），不是#92 cpp-check成功。actual Ninja VALID含MOC：application16objects8124summed/611unique无JSON/Network/infra；codec4objects1521/516、feed2objects1113/567、native2objects551/550、client2objects564/563、service3objects1713/601禁入0；实际client link无application/Network/Gui/media/SQL/DBus/crypto。fixed440/Python15通过，无API注册变化或依赖安装。
- 首轮I/O45s超时/失败保留：fixture用destructive `next()`放QTRY导致复验再吞frame，改peek＋显式consume而非放宽协议。次轮client SIGSEGV是过长default QTemporaryDir导致endpoint创建失败后fixture解引用null；service同轮4fail为正确sun_path失败。改显式短隔离root/fixture guard，**没有放宽生产路径上限**；崩溃/失败XML/log/stack保留。AUTOGEN/.moc通过真实build/compileDB/fresh恢复，未删Q_OBJECT/.moc/负例/fatal或suppress。#92 parse/CLI unavailable仍独立未闭。
- 最终工作树55文件（O3b1的46＋6个IO/service/client源＋3tests），41 C++/2 CMake/10 Markdown/2合成JSON，22modified/33new、staged0。独立ignored `observation-o3b2-*`、`check-playback-observation-o3b2.py`／`playback-observation-o3b2-check.json`核对format/diff/local paths/有限literal/原HEAD/API registry/exact stash/entry/QML，非anchors/全面安全扫描，不覆盖旧包。
- **参数化Linux I/O与严格发现代码子包已限定完成；O3整体仍G**：正式namespace/真实另一OS用户负测/沙箱与发行XDG/最低Qt/部署负载性能。productionCompositionVerified/productEndpointEnabled/otherUserNegativeVerified/fullDeploymentSecurityVerified/systemMediaAdaptersRun/liveNeteaseRequestsRun均false；scoped ownerSocketCompositionVerified/realIoSchedulingVerified/discoveryClientVerified为true。没有产品入口或默认暴露，不新增官方query/网络窗口/第二owner/UI，所有改动仍未提交，HEAD/exact stash/paused/APK不动。O4–O6仍须QtDBus批准及各自native toolchain/权限/运行门禁，不能拿Linux小包替代。

### 9.7 跨平台资源／身份接口（代码阶段；Windows/macOS原生编译仍G）

维护者要求继续写代码，并明确后续不必新增测试。本阶段没有新增或运行测试，保留原测试与历史证据；不把以下源代码或Linux编译当成另一平台可用／安全验收。

- [LocalObservationEndpoint接口](../../src/infrastructure/ipc/local_observation_endpoint.h)／[平台选择与Linux适配](../../src/infrastructure/ipc/local_observation_endpoint.cpp)：显式root/namespace/instance、default off、Unsupported失败，无HOME/temp/TCP/Qt权限flag回退；区分borrowed socket/event与caller-owned pipe/socket。仅资源与credentials，不是另一个播放owner、完整服务、发现client或系统媒体adapter。操作和析构必须在I/O线程，不能在播放owner执行文件操作或等待。
- [Windows原生代码](../../src/infrastructure/ipc/windows_observation_endpoint.cpp)：仅本机固定磁盘root，reject UNC/remote drive/reparse；所有祖先保留无FILE_SHARE_DELETE的handle，私有目录/manifest检查owner SID与protected、仅该SID的allow DACL。命名管道采用FIRST_PIPE_INSTANCE、PIPE_REJECT_REMOTE_CLIENTS、overlapped byte stream与不可继承handle。替换listener在旧实例仍存活时创建，失败终止，不先关闭再盲目接管。端点资源最多9个pipe实例，为未来8客户端＋1listener留界限；资源接口自身不替代consumer的8客户端策略。
- Windows接纳先异步读**一个字节**、固定5s初始字节期限，再以kernel impersonation token检查SID/session并恢复身份；Peek不算read。返回的`LocalObservationAdmission`带这个字节，caller必须在后续socket/pipe数据前送入framer并计入input。client侧原语检查pipe owner/DACL和kernel关联server process的token SID/session，PID单独不是认证。readiness event可能随accept变化，caller须先停notifier再重绑；cancel完成前保留OVERLAPPED/event/buffer，资源析构可能在I/O线程等待OS取消，不能在owner或退出event-loop后声称已排空。
- [macOS原生代码](../../src/infrastructure/ipc/macos_observation_endpoint.cpp)：getpeereid双侧UID／拒绝setuid、nonblocking/CLOEXEC/SO_NOSIGPIPE、0700/0600/no-follow/pinned inode/type/owner。Darwin扩展ACL不能只靠mode验证，root/祖先/子节点保守仅接受空／不存在或DENY-only ACL，索引扫描≤128，未知／不支持失败；root要求本机filesystem。无Linux /proc bind别名，因而普通bind只经root/current-user拥有且不能由另一用户改名的可信祖先，root-owned sticky目录例外亦检查子owner与ACL。socket必须满足Darwin sun_path上限，不换临时目录救场；manifest≤4KiB、≤16次write、linkat原子no-replace发布，identity-matched仅自身清理，不递归删未知内容。
- 独立`cirrus_observation_local` target只接domain/Core与系统原语：Linux复用旧native资源；Darwin条件编译macOS文件；Windows条件编译并链接系统advapi32，不安装外部依赖。不接application、产品入口／Runtime／QML、媒体或官方API。
- 共享Linux适配在Qt6.11.2 Debug/Release编译通过，真实flags Clang通过；fresh LSP共享/相邻Linux路径0。Windows/macOS文件在此Linux主机的宏分支**不活动**，其LSP0不是原生代码通过。clang-cl存在但`windows.h` SDK探测失败（exit1）；xcrun不可用，亦无Windows Qt Kit/macOS native build/runtime证据。独立ignored `observation-cross-platform-*`日志保留失败和限定构建；旧§9.6的测试只属于那个Linux检查点，不挪作本包证据。
- **仍未实现Windows/macOS实际read/write/drain服务与严格发现组合**，更没有SMTC/Now Playing或产品激活；原生SDK/机器/权限/取消行为、跨用户负例、沙箱/部署负载、正式发行namespace全部未闭。默认关闭、唯一owner、只读v1与所有原隐私预算不变。无NetEase请求、暂停恢复、stash操作、提交或push。

### 9.8 Linux MPRIS适配（历史代码／限定编译检查点，桌面运行仍G）

本轮继续实现系统媒体，不新增或运行测试。已实现Linux代码，不把它等同于Windows SMTC、macOS Now Playing或已可用的产品集成。

- [MprisService](../../src/infrastructure/system_media/mpris_service.h)／[owner组合](../../src/infrastructure/system_media/mpris_service.cpp)及[独立worker](../../src/infrastructure/system_media/mpris_worker.h)／[D-Bus实现](../../src/infrastructure/system_media/mpris_worker.cpp)：复用原private feed、单latest mailbox／单票wake、唯一SystemMediaCommands桥。owner处理命令时重新调用validatedExport并核验原binding/private fence，不以历史D-Bus快照授权；≤8pending，执行仍由原ListeningSession复验。没有第二播放器、权威时钟或自动音乐／歌词／图片／报告请求；只读IPC v1未加控制method。
- 独立静态target `cirrus_system_media_mpris`，CMake `CIRRUS_BUILD_MPRIS=OFF`；仅显式ON时要求本机已有Qt DBus，不安装依赖、不连模板入口。服务enabled／allowControls／exportMetadata分别默认false；bus serviceName、显示identity与runtimeRoot必须显式提供，不猜正式namespace。正式identity／打包权限仍待确认。
- 不使用ambient DBUS_SESSION_BUS_ADDRESS／autolaunch：I/O线程选择显式canonical runtimeRoot下的固定bus Unix socket；root必须current UID/exact0700，root及socket保留no-follow fd并核验dev/inode/type/owner，拒绝setuid。先以同路径native connected probe核验SO_PEERCRED，再用单一percent-encoded unix:path地址创建Qt连接，期间／请求／发布／期限轮复验路径身份，失效终止，无TCP或其他transport fallback。**probe不是读取Qt D-Bus连接的原始fd**；固定私有路径关联不替代另一真实OS用户负测或沙箱验证，也不防同用户malware/root/admin。资源操作与最多5s探测等待只在I/O线程。
- 标准root＋Player对象；无TrackList／Playlists、Raise／Quit／OpenUri或额外歌词属性。元数据只含opaque per-load track path、checked ms→signed µs length，以及显式开启的title/artists/album；不导出歌词、artURL、Song/account/token/provider字段。未知volume／queue modes不造默认值，Get拒绝／GetAll省略并失效旧属性；不可用时Stopped／NoTrack及不可用能力。CanControl是不可变的控制实现opt-in策略，不表示当前播放能力，按规范不发它的PropertiesChanged。
- Play/Pause/PlayPause/Stop/Next/Previous、相对Seek、当前opaque track的SetPosition、volume及queue modes只提交窄意图。旧TrackId／非法绝对范围不重定向；相对越曲尾仅按当前next能力提交。Rate恒1，设置0按规范请求原Pause，不伪造倍速。Pause／seek命令入桥不制造Paused／Seeked；只有原实际状态／确认seek revision投影可发布。Position不发PropertiesChanged，无插值权威位置。
- 固定method/property/signature及参数数目检查，全局32calls/s限制；typed Core票据有界，不排队历史metadata。属性按最新值差分／coalesce，最多一组PropertiesChanged＋可选Seeked与一个异步daemon GetId屏障在途，5s超时终止。**daemon response不证明消费者drain／设备效果，也不把IPC 2MiB预算套到Qt/libdbus／daemon／消费者缓存**；部署负载／内存／慢客户端仍G。发送前private fence复验，已交native/kernel/daemon/consumer字节不可撤回。
- 所有D-Bus注册／操作／路径检查／清理在独立QThread；wake ticket只在真实wake callback起始ack，periodic deadline不ack别的票。close先同步退役owner feed，再异步cancel／撤销注册，不阻塞owner或停止音乐。shutdownTicket.complete独立于service寿命，应用仍须在退出event-loop前排空；未接产品生命周期。
- Qt6.11.2/Linux **Debug与Release目标编译exit0／compiler warnings0**；Clang按实际flags检查3个translation units含MOC，exit0／diagnostic bytes0。whole-archive shared link加no-undefined通过，不执行该产物。actual Ninja VALID应用16objects无infrastructure/QtDBus/QtNetwork反向依赖，MPRIS3objects；4个新源fresh all-server LSP无C++错误，两个Seeked英文拼写提示按官方固定协议名标false-positive，无源码suppress。#92 cpp-check解析问题保持未闭。
- 独立ignored `mpris-final-*`／`mpris-clang-*`／`mpris-build-check.json`／`mpris-actual-dependencies.json`／`mpris-link-check.*`保存本轮证据，不复用旧包pass。**testsAdded/testsRun/busServiceActivated/nativeRuntimeVerified均false**；无真实session bus服务启动、GNOME/KDE媒体键、实际设备、最低Qt6.10、Flatpak／发行权限或Windows/macOS验收。SMTC／Now Playing仍未实现，原生工具链门禁见§9.7。default off，无NetEase请求／新依赖安装／UI／paused恢复／stash操作／提交／push。

### 9.9 SMTC／Now Playing原生代码与有界回调接缝（原生编译／运行仍G）

维护者继续要求实现，保留“不新增／运行测试”的约束。本轮补写两端代码，不再把它们描述为完全没有源代码；**也不能因此称Windows/macOS适配已完成或可用**。

- [NativeMediaService](../../src/infrastructure/system_media/native_media_service.h)／[组合](../../src/infrastructure/system_media/native_media_service.cpp)、[回调inbox](../../src/infrastructure/system_media/native_media_inbox.h)／[实现](../../src/infrastructure/system_media/native_media_inbox.cpp)、[窄平台port](../../src/infrastructure/system_media/native_media_platform.h)：同一publisher与原SystemMediaCommands／ListeningSession链，无第二player／clock／scheduler／report／query，IPC v1仍只读。服务enabled、controls、metadata默认false，one-shot，必须位于应用main/GUI线程且与原owner相同。Linux/default-off构建的start返回Unsupported，不fallback MPRIS或假对象。
- 原[命令桥](../../src/application/playback/system_media_commands.h)新增owner-only `admissionRevision/submitGuarded`：live private factory确认原binding，再捕获原load/control intent pair；仅进程内私有，不入metadata、IPC或日志。Native callback在到达时捕获已核实binding／generation／原intent stamp，投递与执行再次复验；不得在晚到时采纳新的用户意图。原submit/FIFO/效果判断不变。queued但尚未桥接的陈旧原intent可以保守拒绝，不能拿新revision重签。
- Native inbox用受mutex保护的receiver lifetime，只传固定scalar意图；最多8个queued callbacks＋原桥pending tickets，32calls/s。OS closure仅持共享关闭gate，不持raw service指针；close先断receiver，迟到／撤销后callback不能访问已销毁QObject或发命令。历史snapshot不是private provenance；每次arm、native字段提交、owner消费都有live factory／original revision屏障。MAC handler返回Success仅表示队列接纳，仍可能在owner端被拒绝，不是播放或seek效果。
- [Windows SMTC代码](../../src/infrastructure/system_media/windows_smtc_platform.cpp)：仅真实本进程main-thread owned top-level HWND，拒绝child/foreign/fake/window helper；GetForWindow而非GetForCurrentView，不创建MediaPlayer。STA RoInitialize成功／S_FALSE有配对RoUninitialize，incompatible apartment失败。窗口附加自有随机property pin检测数字HWND复用，native destroy通知即刻关gate；移除filter／property仅本实例匹配。进程内claim只序列化我们的provider，不是身份认证或证明其他框架无媒体集成。
- SMTC订阅button与position request并保存确切event tokens；Play/Pause/Stop/Next/Previous／绝对定位只投原窄桥，unsupported按钮无命令、不另抢全局媒体键。actual状态／names-only opt-in metadata／timeline投影，checked ms→100ns，rate1；无音量／循环／随机／倍速／artwork接线。未知或不可表示timeline清为禁用seek范围的native中性值，不声称是真实位置；OS UI是否准确禁用仍须原生验。窗口／能力／原binding变化不能重定向旧request。
- [macOS Objective-C++代码](../../src/infrastructure/system_media/macos_now_playing_platform.mm)：Foundation/MediaPlayer ARC、main-runloop、availability检查；default NowPlayingInfoCenter＋RemoteCommandCenter。注册play/pause/toggle/stop/next/previous/position的exact returned token，handler按值持callback，撤销仅removeTarget自身token；finite seconds→ms检查，actual playbackState显式更新，rate0仅反映实际未Playing状态。metadata可选title/artists/album，无artwork／URL／歌词／ID／账号／查询；无额外音频会话或播放器。volume／queue modes／rate commands未接。
- macOS要求本进程原播放链独占集成：发现未知已有NowPlaying信息／状态时拒绝startup；我们的process claim不证明第三方handler不存在。当前dictionary pointer/equality只用于避免清理未知替换信息，**不是private来源证明**；源头仍只认原Core factory。外来替换时不改其metadata／enable flags，失败清理不授权新provider；精确撤销我们的tokens，恢复仍属于自身上下文的enable修改。多应用争夺、框架自动注册／媒体键双触发仍运行G。
- source retirement／close同步断gate；正在native调用时立即使permit失效，剩余字段不再提交，已经交OS／consumer的内容不可撤回。close同步退役publisher导出（旧load sticky retirement不撤销，也不停止音乐），native clear/token撤销/apartment释放排到main loop。清理若撞native重入则只保一个延迟cleanup turn；shared shutdown ticket独立于service，**退出loop前需complete且核对cleanupSucceeded**。Native UI API必须main/apartment执行，未声称SDK调用零阻塞或已证明物理零缓存；原socket/pipe资源仍只I/O线程。
- CMake增加仅Core的 `cirrus_system_media_native`；`CIRRUS_BUILD_SMTC`／`CIRRUS_BUILD_NOW_PLAYING`默认OFF且拒绝错误平台。Windows显式ON才编SDK自带C++/WinRT／system interop并链接windowsapp/runtimeobject/ole32/user32，不安装NuGet/库；Darwin显式ON才enable Objective-C++／ARC／blocks与系统Foundation/MediaPlayer。`.clang-format`去掉仅Cpp限制，共享原样式给Objective-C++，不放宽warning／负例。
- **Linux便携接缝Debug/Release与既有MPRIS目标编译0／compiler warnings0**，真实flags Clang4 translation units含native MOC／原commands0，whole-archive no-undefined Core链接0；不执行产物。8个接缝／Core／Runtime fresh LSP0，Runtime MOC按真实backend_autogen恢复，未删moc/Q_OBJECT；Windows inactive LSP不算原生通过。独立`native-media-*`保留首轮format拒绝Objective-C及误引用不存在Buffering的build失败：按真实enum修inbox，不改领域状态、旧测试或诊断阈值。
- Windows SDK再探测exit1 `windows.h file not found`，亦不能验证winrt headers；macOS无xcrun／SDK／native Qt Kit。官方Microsoft正文fetch仍certificate error，未绕过TLS／工具网络保护；仅官方source-linked搜索线索，不称SDK正文全审。**windowsSmtcNativeCompiled/macNowPlayingNativeCompiled/nativeRuntimeVerified/testsAdded/testsRun/productWired均false**。所有原生ABI／ARC／HWND/apartment／selector／callback撤销／实际设备／权限／包／最低OS与Qt仍G；原Windows/macOS IPC read-write-drain/严格发现亦未补完。未安装依赖／访问NetEase／接产品／恢复paused／动stash／提交／push，#92保持未闭。

### 9.10 本地提交边界

维护者明确允许commit、仅禁止push。本次提交自百科基线 `1a36f78` 起累积的B-031代码／文档／历史合成fixture与测试共71个文件，包含§9.1–9.9分阶段产物；没有新增测试或测试执行，不把旧suite/pass挪作最新原生阶段证据。此前“未提交／staged0／无commit”均保留为对应编码检查点历史状态，不是当前维护约束。

- 本地提交，不push／publish／bundle；ignored build日志／编译数据库／本机Qt配置不入commit，固定Together stash不apply/pop/drop。
- `main.cpp`／`Main.qml`／Runtime／官方API注册表不改，107 operations不增，所有产品暴露与可选native构建仍默认关闭；不接UI、不激活服务或访问NetEase。
- Linux便携native接缝／MPRIS已有§9.9限定Debug/Release、Clang与链接证据；Windows/macOS原生SDK／运行、正式发行身份／部署／最低Qt及#92门禁保持开放。没有两端设备，后续Linux优先，保留两端实验源代码而不声称验证成功。
- 本次仅提交检查格式／diff／文件范围及本地文档路径，既有源代码编译证据按原阶段保存；不是新全套测试、native平台、安全或产品验收。

## 10. 依据与尚未关闭的门禁

以下官方技术文档读取属于**原设计轮历史依据**，当时不访问NetEase服务或账号；O1/O2实施轮无新外网读取：

- [MPRIS Player规范](https://specifications.freedesktop.org/mpris-spec/latest/Player_Interface.html)：Position/Seeked、Seek/SetPosition、CanControl等已读取；[root规范](https://specifications.freedesktop.org/mpris-spec/latest/Media_Player.html)约束身份／Raise/Quit/声明能力。
- [Qt 6.11 QLocalServer](https://doc.qt.io/qt-6.11/qlocalserver.html)与[QLocalSocket](https://doc.qt.io/qt-6.11/qlocalsocket.html)：native local transport、macOS权限flag局限、Windows同名pipe非独占。最初泛Qt URL为6.12文档，已另读6.11.2，不能以新版本替代项目最低Qt6.10验证。
- [Microsoft GetForWindow](https://learn.microsoft.com/en-us/windows/win32/api/systemmediatransportcontrolsinterop/nf-systemmediatransportcontrolsinterop-isystemmediatransportcontrolsinterop-getforwindow)：官方定位及owned top-level HWND约束由搜索结果支持，**正文fetch未成功**。第一次证书 `UNKNOWN_CERTIFICATE_VERIFICATION_ERROR`，另一次工具实际拒绝 `Blocked internal address for learn.microsoft.com: 198.18.0.185`；未改TLS/allowRanges/DNS/proxy/IP、未用shell绕过。具体SDK/运行条件仍G，不能称全文已审／实测成功；这也不是NetEase默认网络不可用的证据。
- [Apple NowPlaying](https://developer.apple.com/documentation/mediaplayer/mpnowplayinginfocenter)、[remote command center](https://developer.apple.com/documentation/mediaplayer/mpremotecommandcenter)、[playbackState](https://developer.apple.com/documentation/mediaplayer/mpnowplayinginfocenter/playbackstate)：HTML仅标题，因此另读官方tutorials/data对应三个JSON文档；类metadata列macOS10.12.2 availability，playbackState Discussion明确macOS启停更新要求。未执行native代码，不宣称该OS版本可跑当前Qt客户端。

后续仍需：O3正式namespace/另一OS用户负测/发行XDG与沙箱/部署性能（§9.6仅参数化Linux真实I/O及发现限定I/S，非产品接线）；O1 native反馈的真实设备/decoder效果验证；实际最低Qt6.10与各部署权限／负载矩阵（本机Qt6.11.2 scoped Debug/Release不替代）；Windows/macOS工具链与真实机器；Linux MPRIS真实session bus／桌面运行、Qt DBus发行依赖确认、release app ID／沙箱权限与metadata隐私配置（§9.8仅可选target代码编译，未激活）。门禁不关闭就保守unavailable，不假对象、不默认fallback服务。

### 原设计轮检查（历史，非当前O1功能验收）

9个Markdown变更，387个本地reference路径missing0／有限Cookie-signed-query flags0；不含anchors验证或全面安全审计。3个合成JSON body解析／示例长度前缀round-trip及slice字段/文字关联检查通过，这不是尚未实现codec/framer的运行测试。107 ledger／39E63W5X／44FR唯一行与B-031连续表行核对、git diff --check通过；show-only实际登记50（没有执行Qt测试）。HEAD仍1a36f78，产品source/test/CMake/QML/entry delta0，固定stash仍原commit，无push／publication／bundle。新能力未实现／Qt或native runtime未验；文档变更未提交，独立ignored playback-observation-design-check.json与日志，不覆盖上包wiki验证产物。

### O2a最终工作树检查（历史限定，不适用于新增O2b文件）

36个未提交文件：原O1/设计29个保留，新增7个（3个codec/request源、support、test、2完整合成JSON），共23 C++／2 CMake／9 Markdown／2 JSON。独立`check-playback-observation-o2a.py`：399个本地reference路径missing0、有限Cookie/signed-query flags0；format23/两种diff0，非anchors或全面安全扫描。实际Ninja VALID：application16对象含MOC8122 summed/609 unique deps、禁入0；codec3对象含MOC1010/507，仅Core/基础设施JSON、无application反向/Network/native禁入。fixed440/Python15、原API hash与52 show-only登记核对；HEAD1a36f78/staged0/exact stash/entry/QML不变，不push/bundle。独立observation-o2-* XML/log/deps及playback-observation-o2a-check.json，不覆盖O1/wiki历史产物；O2Complete:false、realEndpointEnabled:false、platformTestsRun:false、liveRequestsRun:false。

### O1最终工作树检查（历史限定，不适用于新增O2a文件）

29文件：18 C++／2 CMake／9 Markdown，独立`check-playback-observation-o1.py`检查392个本地reference路径missing0、有限Cookie/signed-query flags0；未验anchors或全面安全。format0、两种diff --check0，原API header hash未变、fixed440与Python15通过；实际show-only登记51而非全51运行。HEAD仍1a36f78、staged0、所有本包未提交，无push或bundle；entry/QML和exact stash不变。独立ignored `playback-observation-o1-check.json`、selected/runtime XML、actual-deps与日志保存O1时点证据，不覆盖原设计/wiki的历史验证；后续O2a使用独立observation-o2-*产物。
