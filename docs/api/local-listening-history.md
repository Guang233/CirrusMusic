# 本地可观测收听累计与会话历史

**最新接续（2026-11-12）**：维护者要做本地播放统计看板，后端接口按 §6 补齐：表结构 v4 记播放来源和歌曲信息快照，统计查询在 SQLite 里聚合，计次规则见 §6.1。新增 `tst_listening_stats`（9 项）。

**最新接续**：新增[队列恢复与长期本地历史](../tasks/listening-state-persistence.md)显式default-off Runtime接缝，SQLite v3独立record ID／UTC起止／实际playedMs、长期保存与keyset分页；256仅近期内存窗口，不是保留上限。不序列化原token，不自动恢复活动账本／播放或报告。该新增包只做编译与静态检查，无新增或运行测试／SQL／真实播放；以下计量算法不改，§4为c838573历史证据。

## 1. 范围与入口

在[官方播放](stream-resolution-and-playback.md)、[普通队列](queue-and-lyric-clock.md)与[FM连续播放](fm-continuous-playback.md)之上增加：

- `PlaybackSnapshot::observed`：可选的 `ObservedPlayback { playedMs, capped }`，固定load内的累计观测值；缺失表示没有具备计量能力的后端证据，不是零秒。
- `ListeningSession::history()`／`historyChanged()`／`clearHistory()`：调用线程上、仅当前会话、最多256条的播放摘要。Runtime通过 `listening()` 暴露近期Core窗口；可选 `listeningState()` 服务提供长期磁盘分页，不新增数据库线程。
- Core-only `ObservedPlaybackMeter` 与 `ListeningHistory`；Qt播放器／单调时钟／采样定时器接线留在基础设施层。原计量包未新增依赖、数据库表、业务operation或UI接线；后续持久化新增SQL表，不增加计量器、业务operation或UI。

**这是保守的媒体播放进展观测，不是“用户确实听到了声音”的证明，本身不是持久历史或官方听歌报告；后续存储也不能把它提升为声学证明或官方计数。** 不从FM取出、喜欢ID、曲目时长、seek后的进度或自然End推导已听时长。不新增官方访问、真实账号／密钥环访问或写操作。

## 2. 计量契约

`QtPlaybackBackend`在原媒体线程每250ms采样，计时使用该load的 `QElapsedTimer`；Facade／GUI收消息的延迟不参与累计。

| 条件／事件 | 行为 |
| --- | --- |
| ready、实际Playing、BufferedMedia、1x、非静音且本播放器volume>0、无待确认seek | 才允许建立／延续采样区间 |
| 暂停、缓冲／状态变化、速率变化、静音边界、错误／结束／stop／新load | 断开区间；不补算未知尾部 |
| 第一份有效样本 | 只建锚点，不凭空计入之前的时间 |
| 媒体位置没有前进 | 不增加累计；短期保留锚点，兼容较粗的位置通知 |
| 时钟倒退／相等、位置倒退、非法位置 | 不计该间隔并重建／丢弃锚点 |
| 间隔>2000ms，或位置前进>单调时间差+250ms | 视为观测空档／跳跃，不计该间隔 |
| 正常连续样本 | 增加 `min(媒体位置前进, 单调时间差)`，不超过这段实际经过的时间 |

采样坐标是片段内位置，受已知Qt时长／试听片段长度上限约束；**不加试听起点**，也不拿整曲元数据时长充当累计。重新听过的片段可以再次贡献时间，不宣称统计了不重复音频覆盖率。每load上限7天，到顶 `capped=true`，不溢出或静默截断；新load从零开始。

seek在调用播放器之前先断区间，并等待 `positionChanged` 的目标位置值确认；确认后第一份样本仍只建锚点。当前坐标相同且无在途seek时不再发跳转。Qt没有统一的解码／出声seek完成收据：若后端不给出精确目标位置通知，计量保持关闭，直到后续可确认的seek或新load，**不按超时猜测恢复计量**。位置通知不是声学完成保证。

状态／状态码变化在媒体对象信号到达时立即断区间，避免漏掉两个采样tick之间的短暂缓冲；向上发进度仍由定时器完成，错误销毁仍延后到安全调用栈。累加值经 `PlaybackEvent::Observation` 传递，Coordinator只接受当前ready load的非负、有界、不回退值；旧load、重复通知和终态后的计量不能重复增加。

### 证据限制

- Qt的Playing／Buffered与位置是可用的客户端证据，但不是扬声器实际输出、耳机佩戴或人的注意力证据。系统级静音、外部混音器、硬件断连及音频管线延迟不一定能由这些属性证明。
- 有采样量化、seek确认及暂停／结束尾部丢弃；忙碌／休眠空档不回填。快速替换load还可能丢弃已在途的旧进度快照。因此不承诺精确实听秒数、完整覆盖率或精确自然结束时刻。
- 本包未做新的真实Qt音频计量端到端验收；以前静音试听PoC不是本包计量正确性的证明。本播放器静音时应为零，系统外部静音的可观测性另论。

## 3. 会话历史与隐私

每个接受的队列／FM Play先绑定原 `SessionToken + load generation`、原账号（guest为空）、SongId与Queue／PersonalFm来源；普通队列保留entry UUID，FM不伪造普通队列entry或歌单ID。仅在收到正累计证据后入列表，最新load在前，超过256条逐出最旧条目。

- 保存实际音质、可选试听范围、累计毫秒与到顶标记；同曲重放是不同load，不按SongId错误去重。值是累计替换，不把重复快照再相加。
- `Active`表示load摘要尚未终结，**不等于正在出声**。End、Failed、Stopped、Replaced与外部生产者接管后的Detached只终结当前匹配记录；从未观测到正进展的失败／未播load不造记录。
- Play／Stop被本地邮箱拒绝时不改历史；重入的新意图优先于旧通知。普通队列与FM共用同一账本，但来源标签不跟随之后的模式切换修改。
- 单曲计量也适用于直接使用Facade的调用方；但本会话历史只记录ListeningSession拥有的load，不把外部Facade播放冒充队列／FM播放。
- 切号、退出、启动失败、关闭立即在getter隔离，并在失效处理时清空内存；相同账号重新登录也不继承旧epoch。仅停止／清空队列保留当前会话已有历史。
- `clearHistory()`清空列表并停止记录**当前load的剩余部分**；直到新load才重新入历史。这样晚到的、可能在清空前采样的累计值不能复活旧记录。它不停止音频，也不重置底层单曲计量。重复begin／旧generation不能重新开启已清空load。
- 信号只是通知，消费者应读当前getter；已经取走的值副本不会自动擦除，消费者仍需在账号变化时丢弃。原内存包没有写盘或时间；后续独立StoredListeningRecord存UUID、UTC日历、original account/song/source、正playedMs/终态/quality/preview，不存原token。开始／结束UTC仅日历，墙钟差不计时；长期分页与内存active分开，不能触发播放或报告，崩溃的Active磁盘摘要不是当前load。

## 4. 历史离线验证（原内存包c838573，非本轮持久化证据）

当时受影响Debug结果（含init/cleanup；前次运行不累计）。持久化阶段未新增或运行这些测试：

| 套件 | pass |
| --- | ---: |
| listening_history | 13 |
| playback | 9 |
| listening | 20 |
| fm_listening | 15 |
| media | 17 |
| backend_facade | 8 |
| 合计 | **82** |

`media`另有1项默认真实音频smoke skip。线程回归 `playbackThroughThread` 的Queue／FM两行与 `destructionStillDrains` 共**5 pass**（含init/cleanup）。线程替身显式注入计量证据，证明值／身份／来源跨线程接通，而不是在静音假播放器里声称真实出声。

```bash
cmake --build --preset linux-dev-local --target \
  tst_listening_history tst_playback tst_listening tst_fm_listening \
  tst_media tst_backend_facade tst_backend_runtime --parallel 4
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(listening_history|playback|listening|fm_listening|media|backend_facade)$' \
  -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  playbackThroughThread destructionStillDrains
```

覆盖虚拟单调时钟、不可计量间隔、粗粒度位置、时间／位置跳跃、计量到顶、重复／倒退／旧load、无证据不造历史、重复同曲、容量／账号隔离、清空后晚到、邮箱拒绝、重入切歌与外部接管。未重跑全量认证／Release矩阵、真实设备或账号验收。

15个变更C++文件的active LSP无诊断；format／diff检查通过，变更文档229个本地链接无断链；13个application编译单元的实际依赖无基础设施／Network／Multimedia／Sql／Quick／Qml／Gui／JSON穿透，history测试只链接Qt Core／Test。登记28个CTest套件，不是28套全量验收；main.cpp／Main.qml无改动。忽略日志在 `build/workspace-checks/history-*.log`，最终结果为final-build、final-focused与final-thread；不叠加开发期重复运行次数。

## 5. 下一步

维护者决定历史展示优先采用网易云记录，本地主要计量时长；后续[网易云最近听过／听歌排行读取](cloud-history-reads.md)已离线接入，但不提供实听时长，不与本地累计直接合并；维护者随后明确需要长期本地历史和实际时长供future dashboard，新增包已写独立schema与显式Runtime接缝：无默认条数删除、guest独立桶、clear／FIFO写与读取epoch、有限未送fact／分页。没有直接序列化本内存结构／token；统计聚合与dashboard仍另做，SQL语义、durability／磁盘满／崩溃恢复尚无本包运行证据。后续[显式startplay／play报告](explicit-listening-reports.md)已另包接入，不自动调用；播放来源扩展、资格／阈值、结算时机和load去重仍待播放接线，不喜欢反馈仍未实现，真实报告另行授权。UI、统计聚合／dashboard与先进音频仍另包处理；MPRIS已有default-off源码接缝，实际桌面仍未验；队列状态-only恢复见新包。

## 6. 本地统计（2026-11-12）

维护者要做本地播放统计看板，决定先补齐后端，再设计 UI，最后接真数据。

### 6.1 计次规则

一条记录（一次加载）算"听过一次"，要满足：不是试听片段，并且听满 30 秒；歌本身不足 30 秒的，听到结尾（`Ended`）或听了九成以上也算。所有记录都计入听歌时间，包括试听片段和不足 30 秒就切走的。规则只在查询时判断（`listening_stats_types.h` 的 `CountedPlayMinimumMs` 与存储里的 `counted()`），数据库只存原始数据，改规则不用改数据。没有歌曲时长（信息还没补上，或服务端没有这首歌）的，只能靠 30 秒计次。

### 6.2 表结构 v4

迁移只追加（`ListeningStateStore::migrations()`），升级前数据库照例留 `.backup-v3`：

- `playback_queue_entry`、`listening_history` 各加 `origin_kind`、`origin_id`、`origin_name`：队列项从哪里来（`domain::PlayOrigin`：歌单、专辑、歌手、每日推荐、搜索、最近播放、听歌排行、其他），歌单等的 ID 与当时的名字；不记搜索词。私人 FM 记录靠 `source` 区分，没有来源。旧数据为 `None`。来源随记录第一次写入固定，之后同一记录改来源按冲突处理（`Corrupt`）。
- `listening_song`、`listening_song_artist`：每个账号听过的歌的信息快照（歌名、专辑 ID／名／封面网址、时长、歌手 ID 和名字），按账号分开；清除听歌记录时一起删。服务端没有的歌记 `available=0`，不再反复查，也不会覆盖已有的信息。
- 新索引 `listening_history_song (account_id, song_id, started_at_ms)`，给"首次听"和缺信息的查询用。

### 6.3 接口

- 来源：`ListeningSession::replaceQueue(songs, start, quality, origin)`、`append(songs, playNext, origin)`，`PlaybackQueue` 同样。不传就是 `None`；来源不合法时队列操作失败，记录时则当作 `None`。
- 统计：`ListeningStateService::stats(ListeningStatsQuery)` → `statsReady(id, error, ListeningStats)`。查询带 UTC 起止（半开区间，0 表示不限）、分组粒度（日／周／月，本地日历，周从周一开始）、排行依据（次数／时长）和每个排行的条数（1–100）。查之前先把还没写入的记录写下去（同一数据库线程，按顺序执行）。和翻页读取一样只给当前账号，换账号或清除后的结果不送达。
- 结果 `ListeningStats`：
  - 汇总：总时长、次数、记录数、歌曲数、歌手数、专辑数、新听的歌（首次计次落在范围内）、有记录的天数；
  - 按日／周／月的时长和次数；24 小时和周一到周日分布；
  - 歌曲、歌手、专辑、来源排行：一首歌的时长全额算给它的每位歌手，没有 ID 的歌手按名字合并，来源取最近一次的名字；
  - 连续收听天数（全部时间，按有计次的天算：当前、最长及结束日、最近一天）；第一条记录的时间；范围内还没补上信息的歌曲数。
- 时间按本机时区分组（SQLite 的 `localtime`），"今天"可由查询的 `nowMs` 指定。

### 6.4 歌曲信息怎么补

`ListeningStateService` 一步一步来：查缺信息的歌（最多 100 首，最近听的在前）→ `songsWanted(ticket, songs)` → runtime 用 `CachedMusicQueryService::details(PreferCache)` 查（先缓存后网络，一次最多 100 首）→ `resolveSongs(ticket, …)` → 写入 → 接着查下一批。登录后 15 秒开始，之后每次写入记录 3 秒后再看一次；查询失败 10 分钟后再试；同一批查不到的歌不反复查。只有配置了资源缓存时才会补；没补上的歌在统计里只有 ID（`known=false`）。只写当前账号、仍在其听歌记录里的歌，清除或换账号后在途的结果丢弃。日志只记条数。

### 6.5 验证

`tst_listening_stats`（真实 SQLite 文件，时区固定为 UTC+8）：v3→v4 升级保留旧记录；队列与记录的来源往返、来源不可改写、FM 不带来源；计次规则各情况；歌曲／歌手（多歌手、无 ID）／专辑／来源排行与按时长排序；跨日边界的本地分组、周和月、小时与星期分布、连续天数；缺信息查询、保存条件、清除。播放、队列、FM、上报、存储等 10 个相关测试照常通过；`backend_runtime` 只有原有的 `closeWhileDatabaseOpening` 失败。在测试数据目录里实际运行应用：v3 库升级到 v4 并留下备份，登录 15 秒后补上了两首歌的信息（走缓存，约 0.1 秒）。

界面层（`App.stats`、看板页面）还没做；界面构造队列时还没有传来源，所以现在新记录的来源都是 `None`，接真数据时补上。
