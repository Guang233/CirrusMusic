# 04 · 领域与数据模型

> 状态：提案。这里定义客户端规范化模型，不是网易云官方 JSON 字段说明。

## 1. 建模原则

- 歌曲身份、账号身份、队列实例、媒体来源和本地文件身份分开。维护者新增播客／声音消费范围，PodcastId与PlaylistId、VoiceId与SongId各自独立，不通过mainSong／mainTrackId假装同一资源。
- 官方大整数 ID 在边界转为**十进制字符串**，不得经 JavaScript `Number` 或 C++ `double` 往返。JSON 数字字段使用可保持整数精度的解析路径并校验范围，禁止先 `toDouble()` 再转字符串；超出所选解析器精确范围时保留数字字面量或报契约错误，不接受已舍入 ID。
- 时长和位置统一使用带语义的 `qint64` 毫秒；墙上时间使用 UTC，调度／超时使用单调时钟。MPRIS 微秒只在适配器边界换算。
- `null/unknown` 与 `0/false/empty` 不等价：未知时长不是零秒，未知权限不是允许，空歌词不一定是纯音乐。
- 规范化模型可增加可选字段；缺少关键身份字段是契约错误，不用随机 ID 假装有效歌曲。
- 来自官方 API 的枚举保留未知情况，以 `Unknown` 降级，而不是因新增音质标识崩溃。

## 2. 身份模型

| 类型 | 结构／约束 |
| --- | --- |
| `TrackKey` | `{source, idText}`；source 首版为 netease、local、neteaseCloud；不表示已实现多平台聚合 |
| `AlbumId / ArtistId / PlaylistId` | 独立强类型字符串，禁止拿歌单 ID 当歌曲 ID |
| `PodcastId / VoiceId` | 独立强类型字符串；[实体搜索](api/entity-search-reads.md)及[消费／recent读取](api/podcast-and-recent-reads.md)已接入，不转为PlaylistId／SongId或从内嵌音轨制造身份；后续播放另包 |
| `ProgramId` | 传统消费端节目列表／详情的独立类型，不从VoiceId直接转换；两者实际对应尚未验证，不能用mainSong.id制造节目身份 |
| `AccountId` | 官方账号标识，仅用于账号逻辑，不用作可预测下载路径 |
| `ProfileId` | 本地随机 UUID；guest 独立 profile；账号到 profile 的映射不含凭据 |
| `QueueEntryId` | 随机 UUID；同一 TrackKey 可对应多个 entry |
| `LocalAssetId` | 本地 UUID；路径改变不会自动改变歌曲身份 |
| `RequestId / MutationId` | 操作追踪 ID；本地 MutationId 不代表官方支持幂等键 |

`neteaseCloud` 资源只能在所属 profile 解析；本地库和云盘匹配出的网易云歌曲关联是显式 `linkedTrackKey`，不可直接覆盖原始身份。缓存 key 还须包含 profile、权限上下文或公共范围，不能只用歌曲 ID。

## 3. 核心实体

| 实体 | 必需字段 | 可选／派生字段与规则 |
| --- | --- | --- |
| Track | key、title、artists[] | albumRef、durationMs?、coverRef?、discNo?、trackNo?、availability；title 缺失时可显示“未知曲目”，key 不能缺 |
| Artist | id、name | avatarRef、alias[]、description；列表与详情分级加载 |
| Album | id、title、artists[] | releaseDate?、coverRef、trackCount?；日期可能只有年份，不能补造月日 |
| Playlist | id、name、owner?、visibility | description、trackCount?、revision?、permissions、snapshotCompleteness |
| PlaylistEntry | localEntryId、trackKey、position | 官方 entryId?、addedAt?；未知重复策略由 adapter 决定 |
| Podcast / Voice | 独立PodcastId／VoiceId | 已实现消费搜索摘要／可选所属播客；Voice.providerDuration只保留原值，不声明单位、音源／时钟／报告或播放权益 |
| AccountProfile | profileId、accountId?、displayName | avatar、权益快照、lastValidatedAt；不得含 Cookie |
| Availability | state、reasonCode? | Unknown/Playable/PreviewOnly/Blocked；该值是快照，真正播放必须重新解析权限 |
| ResourcePermissions | read、write、delete、download 等三态能力 | Unknown/Allowed/Denied；服务端最终裁决，不因 ownerId 一致就盲目允许 |
| `Page<T>` | items、queryKey、nextCursor?、hasMore | total?、snapshotVersion?；total 是可选值，不与已加载数混为一谈 |

`Page` 的 cursor 是客户端不解释的 token。偏移式 API 由适配器映射；不能强制所有接口都支持 cursor、总数或稳定快照。

## 4. 音质与媒体来源

### 4.1 音质模型

`QualityPreference` 表示用户意图，例如自动、标准、高品质、无损、Hi-Res；真实官方音质枚举保存在适配层。`AudioFormat` 表达实际 codec、container、bitrate?、sampleRate?、bitDepth?、channels?。

- 标准显示名不得反过来成为协议参数。
- requestedQuality 与 actualQuality 分开；不能因为用户选择无损就显示“正在无损播放”。
- 只有源信息足够时显示精确采样率／位深。无法测得则隐藏，不猜测。
- 音质降级受设置控制，默认允许可见地降到合法可用质量；禁止改变账号权限。

### 4.2 PlayableSource（短生命周期）

字段：`trackKey`、`sourceKind`、`uri`、`actualFormat`、`expiresAt?`、`seekable?`、`previewRange?`、`requestPolicyRef?`、`sessionEpoch`、`authorizationScope`。

- `uri` 只留在受控内存或下载任务运行上下文中；不进入 QML、普通日志、队列快照或持久化表。
- `requestPolicyRef` 指向受控媒体传输策略，不是可向任意域发送的通用 Cookie 字典。
- 本地 source 的路径由文件服务验证；远程 source 的 scheme、域及重定向由网络策略验证。
- 缺少 expiresAt 时按短期临时资源处理；每次新的播放尝试重新解析，失败最多一次按策略换源。
- 试听区间是真实限制，进度和跳转不得显示可访问整首。

## 5. 队列模型

```text
QueueSnapshot
  queueId, revision
  entries: [QueueEntry(entryId, trackKey, origin, metadataSnapshot)]
  currentEntryId?
  orderMode: Sequential | Shuffle
  repeatMode: Off | All | One
  shuffleOrder: [entryId]
  playedHistory: [entryId]
  sourceContext?: playlist / album / fm / search
```

不变量：

1. entryId 唯一，当前项为空或属于 entries；移除当前项的行为由播放规则决定。
2. shuffleOrder 是当前有效 entryId 的排列；新增、移除、排序必须保持一致，不每次 next 临时随机。
3. revision 单调递增，拖拽操作带 expectedRevision，陈旧操作重算或拒绝。
4. 列表分页不等于播放队列分页。点击“播放全部”需得到完整 ID 序列或使用显式可继续加载的队列源。
5. 元数据快照允许离线显示；播放时仍检查 TrackKey、文件可用性和授权。
6. 自动跳过失败歌曲记录本轮已尝试 entry，有限终止，不在单曲循环中永久重试坏源。

## 6. 歌词模型

```text
SongLyrics
  song: SongId
  original / translation / romanization: LyricVersions
  providerNoLyric?, instrumental?

LyricVersions
  standard / enhanced: LyricTrack

LyricTrack
  availability: Missing | Empty | Present | Malformed
  timing: PlainText | Lines | Words
  providerOffsetMs, credits[], malformedLines
  lines: [LyricLine(startMs?, endMs?, text, words[])]

LyricWord
  startMs, durationMs, text
```

上述模型已在[首批音乐查询](api/music-queries-and-lyrics.md)实现；缓存fingerprint／版本、用户偏移设置及播放同步仍待后续实现。六个源字段独立保留，enhanced也可能是逐行；不同语言的行数／时间不一致时不能按下标绑定，不把原文逐字时长复制给译文／罗马音。

时间线使用内容时间，显示时应用偏移。约定**正 userOffsetMs 表示歌词延后显示**，有效行起点为 `startMs + providerOffsetMs + userOffsetMs`；实现和设置文案遵守同一符号约定。LRC的正offset表示提前，解析时转换为负providerOffsetMs，不直接改写内容时间。保留同时间多行，不能用时间戳作为唯一 key 丢掉对唱。空行、重叠、重复标签、畸形时间戳和无结束时间均需测试；逐字格式未经验证时降级，不臆造时间戳。

## 7. 下载、本地资源与操作结果

`DownloadTask`：taskId、profileId、trackKey、requestedQuality、actualFormat?、state、bytesReceived、totalBytes?、temporaryAssetId、retryCount、lastError?、authorizationSnapshot。

状态：`Queued → Resolving → Downloading → Verifying → Completed`；可进入 `Paused / Failed / Canceled / WaitingForAuth / Blocked`。重启后原 Downloading/Verifying 任务先进入 `Recovering`，验证文件和授权后转 Paused、Queued、Completed 或明确失败，不直接续写。未知 totalBytes 使用不定进度，不显示百分比除零。

`LocalAsset`：assetId、trackKey?、canonicalLocation、accessGrantRef?、size、modifiedAt、contentHash?、codec?、origin、entitlementRef?。区分用户自有文件、授权下载、应用临时缓存；导入文件默认只读，不重命名、不移动、不改标签。

`MutationReceipt`：mutationId、affectedEntity、state、confirmedItems[]、failedItems[]、unknownItems[]、serverRevision?。状态 `Confirmed / Rejected / PartiallyConfirmed / OutcomeUnknown`；请求超时不能简单等同“服务端没执行”。

## 8. 逻辑存储结构

采用公共元数据数据库与 profile 私有数据库分离，物理策略见[09](09-storage-cache-and-sync.md)。这不是复制两份全部服务端数据；每个库只保存需要的快照。

| 数据库／表 | 主键／索引 | 说明 |
| --- | --- | --- |
| catalog / public_tracks | source + idText；updatedAt | 只含公开歌曲元数据，无云盘或私人歌单内容 |
| catalog / public_albums、public_artists | source + idText | 按需缓存，不完整目录镜像 |
| profile / track_snapshots | trackKey | 队列、历史和私有内容离线展示用快照 |
| profile / playlists | playlistId；updatedAt | 名称、权限、revision、完整性、同步时间 |
| profile / playlist_entries | localEntryId；playlistId + position | FK 到本库歌单与快照；远程重复约束由契约决定 |
| profile / queues、queue_entries | queueId／entryId；queueId + position | 队列模式、当前项、顺序及快照 |
| profile / favorites | entityType + entityKey | 远程确认态与暂存本地意图分开 |
| profile / play_history | eventId；playedAt、trackKey | 本地播放事件，不冒充官方历史 |
| profile / local_assets | assetId；canonicalLocation、contentHash | 本地文件与下载索引 |
| profile / download_tasks | taskId；state、updatedAt | 不存长期可复用签名 URL或凭据 |
| profile / pending_mutations | mutationId；entityKey、state | 仅保存必要恢复上下文；不默认提供离线批量写队列 |
| profile / search_history、lyric_offsets | entryId／trackKey | 可单独清理；默认不跨账号共享 |
| 每个 DB / schema_migrations | version | checksum、appliedAt，迁移历史不可修改 |

### 8.1 约束示意（非已交付迁移）

```sql
CREATE TABLE track_snapshots (
    track_key TEXT PRIMARY KEY,
    title TEXT NOT NULL,
    metadata_json TEXT NOT NULL,
    updated_at_ms INTEGER NOT NULL
);
CREATE TABLE queues (
    queue_id TEXT PRIMARY KEY,
    revision INTEGER NOT NULL CHECK (revision >= 0),
    current_entry_id TEXT,
    order_mode TEXT NOT NULL,
    repeat_mode TEXT NOT NULL
);
CREATE TABLE queue_entries (
    entry_id TEXT PRIMARY KEY,
    queue_id TEXT NOT NULL REFERENCES queues(queue_id) ON DELETE CASCADE,
    track_key TEXT NOT NULL REFERENCES track_snapshots(track_key),
    position INTEGER NOT NULL CHECK (position >= 0),
    UNIQUE (queue_id, position)
);
```

`current_entry_id` 必须属于同一 queue，此跨行约束在应用事务及完整性检查中校验。重排采用同事务内删除／重建位置或经过验证的两阶段重排，不能在唯一索引下逐行互换 position 导致中途冲突。`metadata_json` 用于非查询型快照扩展，不把所有业务查询退化为全表 JSON 扫描。

## 9. 已接入资源评论值

[comment_types.h与契约](api/comment-reads.md)定义CommentTarget（Song／Playlist／Album／Program／Podcast独立variant）和CommentId；不存在Voice／艺人／任意thread输入。普通评论拒绝Podcast；热门／新版／楼层／统计采用已确认通用命名空间。五种query／结果保留原目标、分页／排序／parent与provider次序，统计缺项为missing而非0；time、liked、计数未知保持optional，公开author ID不是认证AccountId。内容及引用纯文本、不可信、非递归预览有界；不抓头像／作者详情、无持久化、自动翻页、通知或写入。响应仅合成候选，JSON／provider type／thread／网络留在基础设施；调用方DTO副本不自动擦除。

## 10. 已接入账号补充读取值

[account_library_types.h与契约](api/account-library-reads.md)定义原accountId的SubscribedPodcasts／CloudSongs／CloudSongDetails查询与结果；独立CloudSongId，显式simpleSong.id仅作可选SongId catalogReference，不兜底或证明音轨绑定。列表保留顺序／重复／未知；详情按请求序并单列missing。fileName是私有展示文本而非路径，配额／文件大小／bitrate／time为原provider值，不推导权益、可播资产或剩余空间。无全库抓取／缓存、音源／播放／上传下载／写；JSON、Cookie留在基础设施，旧token／注销／关闭复用原屏障，已交付DTO副本不自动擦除。响应仅合成候选。

## 11. 已接入收藏订阅写值

[collection_types.h与契约](api/collection-writes.md)：原收藏包CollectionMutation以非空本地eventId关联五variant SongHeart／PlaylistCollection／AlbumCollection／ArtistCollection／PodcastSubscription；红心bool与收藏到选定歌单的Collect／Remove分开，1..100唯一SongId有界批次不拆分。CollectionResult仅NotSent／Acknowledged／Rejected／Unknown、脱敏error／providerCode／RetryAfter；原账号与token由facade捕获，无原JSON／Cookie／URL／权限或本地库状态。UUID不保证服务幂等，Ack不保证逐项原子提交，发送后cancel不能推断撤销；整歌单收藏无SDK入口，CloudSong／Program／Voice不强转；无自动读取／刷新／报告／播放／持久outbox。此为独立Core写port，非只读MusicQuery扩展。

## 12. 已接入歌单基础写值

[歌单基础写](api/playlist-management-writes.md)在同一闭合写variant增加PlaylistCreate／PlaylistRename／PlaylistDescription，该包时点合计8种。创建Privacy仅Public／Private且固定NORMAL；名称128、描述4096 UTF16预算，合法字符、保留原文本，显式空描述清空。CollectionResult新增可选createdPlaylist，仅合法创建Ack携带规范PlaylistId；candidate root.playlist.id及可选root.id一致性在基础设施校验，缺失／矛盾Unknown保留providerCode200。其他动作／失效不发布ID，身份不是所有权／隐私／可编辑权证明。原账号／token／generation和8槽共用；不按同名判唯一、不自动创建重试／读对账／更新库，多动作不是原子事务。无JSON／Cookie／SDK进入Core，该包不含删除／排序／封面／UI。

## 13. 已接入删除与完整范围排序值

[删除／排序](api/playlist-delete-and-order-writes.md)增加PlaylistDelete／PlaylistSongOrder／AccountPlaylistOrder，该包时点写variant合计11种。删除单个明确PlaylistId；排序由完整PlaylistDetails索引或明确total／hasMore=false且offset0的MyPlaylistsPage工厂生成，仅保留ID／原token／账号，不携带描述／封面等大DTO。唯一ID完整置换、1000／100预算与来源屏障拒绝截断／未知／跨号／旧token，不强转identity或承诺权限／并发原子性；工厂不是密码学origin证明，旧metadata不代表当前状态，无CAS／自动读／重试。

## 14. 已接入四类资料与独立介绍值

[契约](api/catalog-introduction-and-metadata-reads.md)：Song／Album／ArtistDetails／PlaylistDetails追加optional别名／译名／发行／公司／类型／标签／公开creator／原计数，缺失unknown、显式[]保留空与重复；不改变旧description或完整索引。PlaylistCreator.publicUserId为展示身份而非AccountIdentity／owner权限，playCount允许提供方小数，不换成观测时长。

ArtistIntroduction保留可选briefDescription与有序IntroductionSection(title可选／text必需)；SongWikiSummary保留SongId、四种SongWikiKind的card文本与unsupportedContent，不通用JSON或猜full prose。依赖Core，预算／纯文本／无URL／无生成，来源与schema仍候选。独立query/result通过原16槽／token／generation，不合并歌词／其他资源、不自动下游或播放。

## 15. 已接入标签编辑与单向公开值

[契约](api/playlist-tags-and-publication-writes.md)：PlaylistTagEdit私有构造、fromCatalogue(originalToken, PlaylistId, PlaylistTags, selected)验证Catalogue种类／成员关系／特殊all／唯一1..3与128 UTF16本地预算；不接受空清除／分号，保序不规范化，只保留选择／目标ID／原token。来源是结构而非密码学真实性／当前官方标签或权限保证，Hot／Premium和详情tags不能替代；不自动读，换号需显式重读。PlaylistMakePublic只有PlaylistId，无privacy输入，数字0仅在基础设施生成、无反向动作；写variant11→13种，原8槽与账号／token／generation及Unknown共用，不新增结果状态／createdPlaylist或库乐观状态。旧PlaylistDescription修改／显式清空仍独立；Ack不证明远端可见性，UI确认另验收。

## 16. 已接入有界封面更新值

[契约](api/playlist-cover-update.md)：第14写variant PlaylistCoverReplace仅PlaylistId／显式内存JPEG≤1MiB、Core只校验envelope；完整marker／1024方形解码、新像素JPEG85与去元数据在基础设施QtGui，不把路径／token／docId／URL带进Core。CollectionResult新增optional CoverStep Preparing／Allocating／Uploading／Committing及失效Unobserved，阶段不是已发送或效果证据，Ack仅合法原上下文最终commit；原8槽与账号／token／generation、Unknown共用，不发布createdPlaylist。不自动文件读取、裁剪、重试／刷新／库更新；上传成功而commit失败可留未绑定对象，无清理／撤销契约。

## 17. 已接入评论显式五动作值

[契约](api/comment-writes.md)：CommentChange四variant（Reaction含like/unlike、Publish、Reply、Delete）与非空UUID CommentMutation，复用严格CommentTarget／CommentId；发布／回复仅Song／Playlist／Album／Program，点赞／删除另含Podcast，Voice／Artist不强转。文本合法非空≤4096 UTF16本地预算／不归一化，单个显式目标不依作者ID猜权限。CommentWriteResult仅eventId／四outcome／Token-Writing-Unobserved阶段／error及optional providerCode、retryAfterMs，无创建ID／正文／secret或状态推断。独立Core port／五动作共用8槽，原account/token/generation、UUID冲突、取消／晚到屏障；Token可能已GET但业务未发时NotSent，业务发送后Unknown，Facade未知阶段保守Unknown。不与collection／report合并原子事务，无自动读取／库更新／重试／outbox，token及JSONP parsing／XEAPI／HTTP只在基础设施，域与应用仍Core-only；结构值不是真实权限／发布效果证明。

## 18. 已接入搜索辅助预览值

[契约](api/search-assistance-reads.md)：SearchSuggestionsQuery{keywords}与HotKeywordsQuery／HotKeywordDetailsQuery为空值请求，3query/result扩展原MusicQuery／Result。四类`SearchSuggestion<ID>`仅独立Song／Artist／Album／Playlist ID及必需title，非完整歌曲／rights／音轨；optional数组区分未知／[]，typed SuggestionGroup保留支持order／重复，unsupportedGroups只flag不发布未知内容。HotKeyword只外部keyword，HotKeywordDetail保留optional raw providerScore／providerIconType／description及display icon（不授权fetch），不按列表位置生成排名／热度或动作URL。256/4096文字与20/16/100预算、纯文本／规范宽ID，原16槽／token与requestId-query乱序绑定，无自动搜索／详情／媒体／cache／持久历史；仅合成，不推定真实schema／完整性／权限。

## 19. 已接入独立每日推荐歌单值

[契约](api/daily-recommended-playlists.md)：DailyRecommendedPlaylistsQuery仅原verified accountId，DailyRecommendedPlaylists保留query及有序DiscoveryPlaylist摘要／重复宽PlaylistId，扩展原MusicQuery／Result；不是普通RecommendedPlaylists或DailyRecommendations，无虚构PlaylistId／日期／offset／total／hasMore或刷新参数。root.recommend100／1MiB、name必须string、copywriter optional／picUrl display-only／小写playcount原聚合显示数仅在基础设施映射，不把JSON、token或协议放进Core。query.accountId不进业务payload，read16／原token-generation复用；Service pending为私有读捕获optional authenticatedAccount，snapshot通知与finish及Facade同token变账号-认证态取消／deliver再次核对，不把late结果迁到新身份。不生成日期cache／权限／订阅状态、自动详情／图片／play／report／写或新owner。仅合成，真实schema／日期与完整性／频率／权益及UI另验收。

## 20. 已接入共享媒体接缝结构值（阶段1）

[契约](api/spoken-media-seam.md)：PlaybackEntity=SongId|ProgramId|VoiceId带kind，canonical校验与same(kind,value)比较独立；AudioTrackId独立正signed64结构类型、不隐式转SongId／ProgramId／VoiceId，不产生可信绑定或声明服务ID必不同。类型未接活动snapshot／history／report／新入口。MediaSource只有URL／actualQuality／format／size／deadline／preview，无身份／account；PreviewRange搬到media_source头不变语义。Song StreamSource以非多态值继承与兼容构造保留Song包络，唯一coordinator核验原Song／token／TTL后只下沉物理base，媒体port／buffer／Qt backend不再看identity；不反向从物理值造Song，不承诺aggregate／ABI。元数据值不是权益／URL安全证明，原媒体限制保留，无signed URL快照／持久化。新28与373＋thread13仅离线准备，消费binding／音源G1/G2及完整spoken单owner／计量仍待证，未开放真实节目／声音播放。后续维护者明确本项G1/G2阶段暂缓、先做历史日推。

## 21. 已接入每日推荐历史与原日期选择

[契约](api/daily-recommendation-history.md)：DailyHistoryDatesQuery当前verified accountId仅本地屏障；DailyHistoryDates保原query及有序／重复YYYY-MM-DD calendar标签，366本地预算非服务窗口。DailyHistorySongsQuery私有构造，仅fromDates接受原完成token与源DTO的显式成员，保存原account/sourceContext/date、不保全列表，不允许任意／空／本机今天／trim日期。factory仅可信内部结构前置非服务签名、paired signal token不能重标旧DTO；Facade与Service／Gateway重核source token，复用同token原账号／认证态屏障。DailyHistorySongs保query与500有序／重复宽Song元数据，候选data.songs非dailySongs，JSON／Cookie只在infra；日期只是关联请求，不验证服务所属日／完整性／权益、不是observed/cloud history／playlist。原16槽／generic runtime，无cache／自动读／选日／详情／图／源／play／write／report／新owner。该历史包85ops／47登记，新86／839＋thread15仅合成，schema／会员／window与UI待证。

## 22. 已接入Together HTTP-only结构值

[契约](api/together-http-core.md)：TogetherAction为核心12 typed variant；TogetherRoomId和TogetherChatRoomId命名空间独立，SongId保留规范正数字符串，不借PlaylistId／AudioTrackId／NIM ID。TogetherRequest有UUID与调用方roomGeneration，port另传原SessionToken／verified accountId；完成保kind/eventId/generation及有限Room／inRoom／joinable／result／reconnect／reportedPlay事实。roomGeneration仅请求相关，不自动改变房间或作为服务器版本／CAS。Room/chatRoom metadata不选择transport；Playlist对象完整字段未知，仅playlistContentUnmapped，无假Song队列／members完备性／权益或Active。请求PLAY/PAUSE/NEXT/PREVIOUS/GOTO/PROGRESS与REPLACE只发送HTTP，不转换成本地播放或navigation；millisecond progress、精确counter／双list多重集是局部结构预算。shared8、same-token账号/phase通知及当前snapshot双屏障，4outcomes/cancel/client-loss/close保Unknown；无NIM／JSON／Network／Gui／Cookie进Core，无新owner／timer／自动报告／持久化。97ops／48登记、新109／911＋thread17是离线适配证据，optional发送扩展／完整playlist／profile实网／完整同步未完成。

## 23. 已接入常规发现结构值

[契约](api/discovery-browse-reads.md)：DiscoveryBrowseAction闭合LatestAlbums/NewAlbums/AlbumCharts/ArtistBrowse/PopularArtists/ArtistTopSongs/SimilarSongs/SimilarArtists/SimilarPlaylists九类。AlbumArea、AlbumChartOrder、ArtistCategory/ArtistArea白名单，CatalogPage有界，显式year/month及ASCII initial不读本机日期/正规化任意字符。seed SongId与ArtistId独立，相似歌单由原SongId而非PlaylistId派生。DiscoveryBrowsePage保原typed query、optional total/hasMore未知、content variant的Album/SearchArtist/Song/DiscoveryPlaylist摘要；AlbumCharts独立optional week/month，缺失未知、[]明确空，至少一组存在，不合并推完整排名；SimilarSongs的DiscoverySongs.requestedPaginationUnverified=true表明原limit/offset未获服务分页证明，100有界批保序／重复而不截成假页。matchingDiscoveryBrowsePage同时核query与content-kind，原id/token/generation/read16/cancel-close路线不新增owner。保重复顺序/宽ID/纯text，无JSON/Network/Gui/provider/token或任意path入Core，无自动下游/持久化/服务权益保证。106ops/49登记不变；[后续公开窗口](api/discovery-public-response-verification.md)11读／301即停，原月榜及相似歌曲2差异修正后188/affected1190＋thread18为离线证据（旧165/1167为331b4fb历史）。complexWiki仅data.blocks6/rnData部分结构观察、绑定／块类仍G，旧summary不套另path，未新增复杂百科DTO或入口。
