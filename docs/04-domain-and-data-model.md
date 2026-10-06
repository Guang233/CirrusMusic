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
