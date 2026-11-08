# 阶段 5：业务接入计划

> 状态：**待维护者评审**（2026-11-07）。评审通过、§6 的决定落定后再动代码。§6 全部已决定（2026-11-07，D2、D3、D4、D6 按建议）；D5 的非歌单来源待登录接好后验证。实施进度见 §8。
> 相关：[presentation.md](presentation.md)（展示层分层、接口、§8 对照表）、[../HANDOFF.md](../HANDOFF.md)（后端现状）、[../api/backend-runtime.md](../api/backend-runtime.md)（装配与关闭）。

## 1. 目标与范围

把应用从假数据换成真实后端：`main.cpp` 组装 `BackendRuntime`，展示层的每个数据源接口（`ui/presentation/sources.h`）有一个包装后端的实现。页面、ViewModel、QML 不改或只做小改；Gallery 继续只用假数据。

不在本阶段：后端新功能（§6 列出的缺口除外，且要先经你决定）、听歌统计面板、Windows／macOS 验证、MPRIS（后端默认不编译）。

## 2. 结构

```text
ui/presentation/            Cirrus.Presentation：ViewModel、接口、假实现（不链接后端，不变）
ui/presentation/backend/    cirrus_presentation_backend：Backend*Source（链接 cirrus_backend）
ui/app/main.cpp             组装 BackendRuntime → makeBackendSources(runtime) → App::install
```

各个 Backend*Source 共用一个**请求分派器**（`BackendRequests`），解决展示层接口与 Facade 的差异：

| 展示层约定 | Facade 的做法 | 分派器负责 |
| --- | --- | --- |
| `Request` 句柄，销毁即取消；回调至多一次、不同步回调 | 命令返回 `quint64` ID，结果由 `musicCompleted(id, token, result)` 等信号送达；`cancelMusic(id)` | ID → 回调表；句柄销毁时 `cancelMusic`；按 ID 和会话 token 丢弃过期结果 |
| 页面随时发请求 | 音乐查询最多 16 条在途，返回 0 表示没接收（未就绪、关闭中、满了） | 本地排队：满了先排着，有空位再发；未就绪时等 `Ready`；关闭中直接失败 |
| `failure::Kind` | `MusicError` | 对照表：Network、Timeout、Busy、RateLimited 同名；AuthenticationRequired → SignedOut；ProviderRejected／InvalidResponse／ProtocolFailure／IncompleteCatalog → Rejected；InvalidRequest → Invalid；Canceled、StaleContext 不回调 |
| 展示值类型（`types.h`） | `domain::` 类型 | 每个 Source 里做转换，QML 拿不到 DTO（presentation.md §2 的规则） |

**标识**：展示层的 key 就是后端 ID 的字符串（歌曲、专辑、歌手、歌单、播客）。**封面 key** 是封面 URL；`image://cover/` 的路径里要做 URL 编码，提供器解码后交给 `ImageSource`。

**写操作**（喜欢、收藏、歌单、评论）：`mutateCollection`／`mutateComment` 的结果有 Acknowledged／Rejected／Unknown／NotSent 四种。Unknown（关闭、切号时发出去但没观察到结果）按失败处理，并提示“可能未保存”，不假装成功。

## 3. 装配与生命周期

1. **启动**：`main.cpp` 按 UQ-06 的决定（§6 D1）构造 `BackendConfig`（安装数据库路径、密钥环 service、封面缓存目录），创建 `BackendRuntime`，`facade().start()`。`App::install(makeBackendSources(runtime))` 在加载 QML 之前完成，和现在一样。
2. **就绪前**：Facade 进入 Ready 前，分派器把请求排着；页面照常显示骨架屏。启动失败（数据库、配置）→ 外壳显示一条带“重试”的错误（新：`App.backendState`）。匿名启动失败只是降级，仍可扫码登录。
3. **账号恢复**：Ready 后如果有记住的账号，调用 `restore()`（后端不自动恢复）。恢复期间外壳显示“正在登录”，不显示游客态再闪成已登录。
4. **切换账号／退出登录**：会话 token 变化时，分派器取消所有在途请求，ViewModel 收到 `accountChanged()`（假实现已有这个信号，页面已经会重新加载）。
5. **退出程序**：Ctrl+Q、托盘“退出”、关闭窗口选“退出”都改为：先 `close()`，事件循环继续运行，等 `stopped` 再 `quit()`。`closeDelayed`（5 秒后发一次，密钥环卡住时会出现）→ 需要你定策略（§6 D6）。
6. **播放**：`ListeningSession` 独占播放命令（后端规则）；`PlaybackSource` 只包装它，不直接调 Facade 的 `playSong`。

## 4. 逐个数据源对照

✅ 后端已有，直接接；⚠️ 能接但有差异；❌ 后端没有。

**SessionSource**（扫码登录、账号）

| 展示层 | 后端 | |
| --- | --- | --- |
| `snapshot`、`changed` | `snapshot()`、`sessionChanged`、`statusChanged`、`signedIn`、`failed` | ✅ |
| `beginQrLogin(remember)` | `beginLogin(options)`，`challengeReady` 给出二维码内容 | ⚠️ 二维码要在本地画成图片：需要生成库（§6 D2） |
| `refreshQr`、`cancelLogin`、`signOut` | `cancel()` + `beginLogin()`、`cancel()`、`signOut()` | ✅ |

**CatalogSource**（只读目录）

| 展示层 | 后端 | |
| --- | --- | --- |
| `playlist`、`songDetails`、`album`、`artist` | `playlistDetails` + `playlistSongs`、`songDetails`（经 `resourceQueries()` 走缓存）、`albumDetails`、`artistDetails` | ✅ |
| `artistSongs`、`artistAlbums`、`artistIntroduction` | 同名 | ✅ |
| `similarArtists` | `discover(SimilarArtistsQuery)` | ⚠️ 公开窗口里服务端拒绝过（301），可能要求登录；失败时页面那一行隐藏 |
| `suggestions`、`hotKeywords` | `searchSuggestions`、`hotKeywords`／`hotKeywordDetails` | ✅ |
| `searchSongs`、`searchCollections`、`searchVoices` | `searchSongs`、`searchAlbums`／`Artists`／`Playlists`／`Podcasts`、`searchVoices` | ✅ |
| `podcast`、`podcastVoices` | `podcastDetails`、`podcastPrograms` | ✅ |
| `feed`（首页、发现的分区） | `recommendedPlaylists`、`dailyRecommendedPlaylists`、`charts`、`discover(...)` 组合 | ⚠️ 一个 feed 由多个后端读组成：并发发出，各分区独立加载／失败 |
| `dailySongs`、`dailyHistoryDates`、`dailyHistorySongs` | `dailyRecommendations`、`dailyRecommendationHistoryDates`、`…HistorySongs` | ✅ |
| `shelf`、`charts` | `recommendedPlaylists`／`dailyRecommendedPlaylists`、`charts` | ✅ |
| `playlistCategories`、`categoryPlaylists`、`premiumPlaylists` | `playlistTags`、`playlistList`、`premiumPlaylists` | ✅ |
| `browseArtists`、`newAlbums`、`albumChart` | `discover(ArtistBrowseQuery／NewAlbumsQuery／AlbumChartsQuery)` | ✅ |

**LibrarySource**（音乐库，需要登录）

| 展示层 | 后端 | |
| --- | --- | --- |
| `library`、`libraryCollections`、`likedPlaylist` | `myPlaylists`、`savedAlbums`、`savedArtists`、`subscribedPodcasts` | ✅ |
| `likedSongKeys`、`recentSongs`、`recentCollections`、`recentVoices`、`listeningRanking` | `likedSongIds`、`recentSongs`、`recentPlaylists`／`Albums`／`Podcasts`、`recentVoices`、`listeningRanking` | ✅ |
| `setSongLiked`、`setAlbumSubscribed`、`setArtistFollowed`、`setPodcastSubscribed` | `mutateCollection(SongHeart／AlbumCollection／ArtistCollection／PodcastSubscription)` | ✅ |
| `createPlaylist`、`updatePlaylist`、`deletePlaylist`、`reorderPlaylist` | `PlaylistCreate`、`PlaylistRename` + `PlaylistDescription`（两次写）、`PlaylistDelete`、`PlaylistSongOrder` | ✅ |
| `addToPlaylist`、`removeFromPlaylist` | `PlaylistCollection`（一次最多 100 首） | ⚠️ 超过 100 首分批 |
| `setPlaylistSubscribed`（收藏别人的歌单） | — | ❌ 后端因 checkToken v2 的门槛暂缓（§6 D4） |

**CommentSource**：`comments`、`replies` → `sortedComments`／`hotComments`、`commentFloor`；四种写 → `mutateComment`（点赞、发表、回复、删除）。✅

**PlaybackSource**（包装 `ListeningSession`）

| 展示层 | 后端 | |
| --- | --- | --- |
| `snapshot`、`changed` | `playbackChanged`、`queueChanged`、`fmChanged`、`cursorsChanged` 合成 | ✅ 后端的位置是“基准值 + 观测时刻”，和展示层的 `PlaybackClock` 一致 |
| `playSongs`、`playAt`、`removeAt` | `replaceQueue(ids, index, quality)`、`select(entry)`、`remove(entry)` | ✅ 展示层按位置，后端按条目 UUID，适配层换算 |
| `next`、`previous`、`playPause`、`seek`、`setVolume`、`setOrder`／`setRepeat`、`startFm` | 同名或 `pause`／`resume`、`setModes`、`startFm` | ✅ |
| `move`（队列拖动排序） | — | ❌ `ListeningSession` 没有移动条目（§6 D3） |
| `playVoices`、`canPlayVoices` | — | ❌ 后端只做了播客媒体接缝的第一阶段，没有声音播放；`canPlayVoices` 返回 false，页面已经按这个隐藏声音的播放按钮 |
| 试听片段 | `PlaybackSnapshot.preview` | ✅ 展示层快照加了 `previewStartMs`：进度条只到片段长度，歌词按整首歌的时间对齐。不加“试听”标记（维护者 2026-11-08）：开始播放试听片段时弹一次 snackbar“这首歌只能试听片段”，连续的试听歌曲只提示第一首 |

**LyricsSource**：`lyrics(songKey)` → `resourceQueries()` 的歌词读取（先缓存后网络）；播放中的歌曲用 `ListeningSession` 自带的歌词和游标。✅

**ImageSource**：`image(key)` → `resourceCache()->cover(url, size, PreferCache)`；尺寸按提供器请求的大小取 64／128／256／512 四档，避免同一张图存很多份。需要 `resourceCacheRoot`（D1），为空时后端关闭封面缓存，每次都下载。✅

**CacheSource**：`usage`、`clear` → `resourceCache()->usage()`、`clear()`。✅

## 5. 步骤与验收

每一步结束：构建、qmllint、在真实后端下手动走一遍（加载、错误、取消、切换账号），小提交，更新本文进度。

1. **骨架**：`cirrus_presentation_backend` 目标、`BackendRequests` 分派器、`main.cpp` 组装和退出流程。其余 Source 暂时仍用假实现（`Sources` 可以混装）。验收：应用启动、Ready、正常退出（等到 `stopped`），数据库在 D1 的位置。
2. **封面**：`ImageSource`、`CacheSource`。验收：封面从缓存目录读，清除缓存后重新下载。
3. **登录**：`SessionSource`、启动恢复、二维码。验收：扫码登录、记住登录后重启恢复、退出登录、游客态。
4. **搜索**：`CatalogSource` 的搜索部分。验收：建议、热搜、六种结果分页、空结果。
5. **详情**：歌单、专辑、歌手、播客、每日推荐。验收：大歌单分批取详情、缺封面、不可播放的歌。
6. **播放**：`PlaybackSource`、`LyricsSource`、私人 FM、音质设置。验收：队列、切歌、随机／循环、拖动跳转、歌词逐字、试听歌曲、FM 连续播放。
7. **音乐库与写操作**：`LibrarySource`。验收：喜欢、收藏专辑／歌手／播客、新建／改名／删除歌单、加歌和移出、歌单排序、Unknown 结果的提示。
8. **首页与发现**：`feed`、`shelf`、发现页各分区。
9. **评论**：读取、排序、楼层、四种写。

做完一步后，假实现里对应部分仍保留（Gallery 要用），`App.debug` 面板只在假数据下出现。

## 6. 需要你决定的事

| | 问题 | 我的建议 |
| --- | --- | --- |
| D1 | **UQ-06 应用身份**：app ID、数据目录、密钥环 service、封面缓存目录 | **已决定（2026-11-07）**：应用名 **Cirrus**（仓库名 CirrusMusic 只为区分），app ID `com.guang233.Cirrus`（Wayland app_id／桌面文件名同此）。随之：数据库 `~/.local/share/Cirrus/state.sqlite`，封面缓存 `~/.cache/Cirrus/resources`（权限 0700），设置 `~/.config/Cirrus/`，密钥环 service `com.guang233.Cirrus`。现有的开发期设置（`~/.config/Cirrus/CirrusMusic.conf`）第一次启动时搬到新位置 |
| D2 | **二维码生成库** | **已决定（按建议）**：引入 Nayuki QR Code generator（MIT，单个 .cpp/.hpp，放 `third_party/`，不改源码），二维码在本地生成，不把登录链接发给任何第三方服务 |
| D3 | **队列拖动排序**：后端没有移动条目 | **已决定（按建议）**：我按后端的约定（HANDOFF）给 `ListeningSession` 加 `move(entry, index)`，只改顺序不重新加载。不愿意动后端的话：接入后先隐藏拖动把手，Alt+↑/↓ 也关掉 |
| D4 | **收藏别人的歌单**：后端暂缓 | **已决定（按建议）**：接入后隐藏歌单页的“收藏”按钮，音乐库照常显示已收藏的歌单（读取是好的）。等后端解决 v2 门槛再打开 |
| D5 | **听歌上报**（`reportListening`，后端规定播放时不自动调用） | **已决定（2026-11-07）**：必须上报、没有开关（官方客户端也这样做）；时机见下方 §6.1。非歌单来源待定 |
| D6 | **退出卡住**：密钥环不回调时 `stopped` 永远不来 | **已决定（按建议）**：等 `closeDelayed` 后再给 5 秒（共 10 秒），仍未停止就直接退出，并在日志里记一条。正常情况下不会遇到 |
| D7 | **启动时恢复上次的播放队列**（后端 `persistListeningState`，现在关着；设置页那一项写着“等后端”） | **已决定（2026-11-07）**：持久化必须做，总是恢复上次的队列和位置；**启动后是否自动播放**由用户在设置里选（默认不播放）。后端的恢复本身从不播放，自动播放是恢复完成后由 UI 发一次播放。队列绑定账号：记住的账号恢复后才恢复它的队列 |
| D8 | **真实网络访问**：接入后运行应用就会访问网易云的官方接口，登录用你的账号 | **已决定（2026-11-07）**：可以用维护者的账号测试。登录方式：现在后端只有扫码登录（加上恢复记住的账号、本地退出，没有服务端注销）；~~以后要加手机验证码、邮箱密码登录，是单独的后端任务~~ → **2026-11-07 改为本阶段第 3 步一起做**：手机号＋验证码、手机号＋密码、邮箱＋密码都加（后端见 [credential-login.md](../api/credential-login.md)）；网易要求安全验证（8821／-462）时提示改用扫码或验证码，不内嵌网页；区号用内置常用列表，默认 +86 |

### 6.1 听歌上报（D5）

后端现状（[explicit-listening-reports.md](../api/explicit-listening-reports.md)）：

- 两个动作，各一次请求：**StartPlay**（开始播放，时长 0）和 **Play**（带实际听了多少秒）。必须已登录（游客不能报）。
- 请求结构是 `ListeningReport{歌曲, 歌单, 动作, 秒数}`：**歌单 ID 必填**，日志里固定写 `source: "list"`、`sourceId`／`content` 为这个歌单。没有表示其他来源（专辑、搜索、每日推荐、私人 FM、排行榜、歌手页）的字段。参考的上游 `scrobble` 也只有这一种写法；另一种 `scrobble_v1`（另一套 clientlog 编码）没有实现，字段未验证。
- 没有自动重试、离线补报、退出时补发；结果可能是 Unknown（发出去了但不知道服务端收没收）。
- 后端本地有每次加载的实际收听时长（`ObservedPlayback`），可以用真实累计时长，而不是进度或歌曲长度。

**时机（已决定，2026-11-07）**：

- 每次加载（一首歌从开始播放到结束）至多报一次 Play，带实际累计秒数。
- 结算时机：播完、被切走、被替换、停止；或者**暂停后长时间没有继续**（定为 5 分钟，可调）。暂停后很快继续则接着累计，不结算。暂停超时已经报过的这一次加载，之后继续播放不再报（不让同一次收听算两次）；单曲循环重新开始算新的一次加载。
- 门槛很低，只过滤迅速切走的歌：累计 **≥ 3 秒**才报（维护者给的范围是 1–5 秒）。
- 退出程序时正在播的歌：先结算再关闭后端（最多等 2 秒），超时就放弃。
- 必须上报，没有设置开关；游客不报。
- 结果是 Rejected／Unknown 时不重试（后端也不重试）。
- StartPlay：官方客户端的做法未知，先不发；要发的话每次加载开始时一次，等来源字段核实时一起确认。

**来源**：展示层要记住每个队列条目从哪里来（`playSongs` 加来源参数；“下一首播放”“加入队列”的条目各自带来源），这部分无论怎样都要做。

**证据：网页版的做法**（2026-11-07 读取 music.163.com 公开下发的前端代码，只读，没有发送任何上报；`core_d6d4797f….js` SHA-256 前缀 `b79d32317e06a476`，`pt_frame_index_f8fa6836….js` 前缀 `4ccc6cb5d58085e4`）：

- 切歌时（换到下一首、用户点了别的歌、单曲循环重新开始），**旧歌播放超过 3 秒**才发 `play`，然后给新歌发 `startplay`。
- `play` 的内容：`{type:"song", wifi:0, download:0, id, time, end, source, sourceId, alg?}`。`time` 是秒数（网页版取的是当前播放位置，我们用真实累计时长）；`end` 是 `ui`（用户操作）／`playend`（播完）／`interrupt`（中断，默认）／`exception`（出错）；`alg` 是推荐算法标记，每日推荐等推荐歌曲带上。
- `startplay` 的内容：`{id, type:"song", content:<当前页面地址的查询串，如 "id=123">}`。
- `source` 与 `sourceId`（网页版按点击位置的来源编号换算）：

  | 来源 | `source` | `sourceId` |
  | --- | --- | --- |
  | 歌单 | `list` | 歌单 ID |
  | 专辑 | `album` | 专辑 ID |
  | 歌手页 | `artist` | 歌手 ID |
  | 排行榜 | `toplist` | 榜单 ID |
  | 每日推荐 | `dailySongRecommend` | （网页版生成链接时不用它，值待核实） |
  | 搜索（单曲／歌词） | `search` | **搜索关键词**（字符串，不是数字 ID） |
  | 单曲页 | `song` | 歌曲 ID |
  | 听歌排行 | `user` | `用户ID|类别` |
  | 播客节目 | `dj`（`type:"dj"`） | 节目 ID |

- 网页版没有私人 FM，FM 的来源名没有证据。

**和后端现在的差别**：后端固定 `source:"list"`，`sourceId` 只接受数字 ID（搜索的关键词放不进去）；`end` 固定 `playend`（网页版按切歌原因填）；没有 `alg`。后端发的是 PC 端的 eapi 格式（多了 `mainsite` 等字段），PC 端的来源取值是否和网页版完全一样还没有直接证据。

**下一步**：等第 3 步（登录）接好后，用维护者的账号做验证：按上表各来源上报几次，再用后端的“最近播放”读取确认记录出现；确认后给后端的上报请求加上来源类型和字符串形式的 `sourceId`、`end` 原因、`alg`（后端改动，按后端约定做并更新 `docs/api/explicit-listening-reports.md`）。FM 先不报，直到有证据。

## 7. 风险

- **并发上限**：首页、发现页一次会发很多读，16 条在途的上限会让后面的分区排队变慢。分派器按页面先后排队，必要时页面分批加载（先首屏）。
- **服务端差异**：后端的读取大多只有离线测试和少量公开窗口验证（HANDOFF §1）。接入时遇到响应结构不符，按 AGENTS 的约定报给你，不在 UI 侧绕过后端校验。
- **全量构建失败**：`tst_entity_search` 现在会失败（AGENTS 里记的已有问题），不影响 UI 目标和 `cirrus_backend`，但接入后要跑后端测试时会碰到。

## 8. 进度

- **第 1 步（骨架）完成（2026-11-07）**：
  - 应用身份：应用名 Cirrus、桌面文件名 `com.guang233.Cirrus`；旧设置 `~/.config/Cirrus/CirrusMusic.conf` 第一次启动时改名为 `Cirrus.conf`。
  - `cirrus_presentation_backend`（`ui/presentation/backend/`）：`BackendHost` 拥有 `BackendRuntime`，建好私有目录（0700）`~/.local/share/Cirrus/state.sqlite`、`~/.cache/Cirrus/resources`，启动后端，退出时关闭；`stopped` 不来时在 `closeDelayed` 后再等 5 秒就退出（D6）。队列持久化（D7）第 6 步再打开。
  - 退出流程：`Desktop` 拦下 Quit 事件（托盘退出、Ctrl+Q、关窗选退出、会话结束都经过它），先隐藏窗口、关闭后端，等停下再真正退出。离屏验证：就绪后触发退出，进程以 0 结束、没有等到超时。
  - 请求分派器随第一个用到它的数据源一起做（第 2–4 步），所有页面仍用假数据。
  - **观察**：用全新数据目录反复启动时，匿名注册被服务端拒绝（返回码非 200），只有一次成功，像是对同一 IP 频繁注册新设备的限流；正常使用时设备 ID 固定、匿名身份从密钥环恢复，不会反复注册。拒绝时后端以降级就绪启动（仍可扫码登录）。游客内容在降级状态下能否读取，第 3、4 步接内容时看。
- **第 2 步（封面和缓存）完成（2026-11-07）**：
  - `ImageSource::image` 多了尺寸参数（长边像素，0 为不知道），`image://cover` 传 `sourceSize` 的长边。
  - `CacheQueue`：资源缓存的任务排队，最多同时 6 个（留 1 个给后端自己）；后端就绪且会话为游客或已登录才提交，缓存满时 200 ms 后再试，会话变了（StaleContext／Canceled）重新排队，后端失败或关闭时全部以 Unknown 失败。
  - `BackendImages`：网址键走缓存（http 改 https、去掉查询，只收 p1–p4.music.126.net），尺寸归到 128/256/512/1024；同一封面同一尺寸同时只取一次；示例封面等非网址键仍走假数据。`BackendCache`：存储设置的占用（元数据＋封面）和清除（所有账号）。
  - 离屏验证：真实封面第一次走网络（7 天有效期落盘），第二次启动命中缓存；示例封面正常；存储设置显示真实占用，清除后为 0 B 并有提示。
  - **后端修正**（`docs/tasks/resource-cache-backend.md` 已记录）：图片主机的 Content-Type 是非标准的 `image/jpg`，原先所有封面都被当成网络错误；`cacheRetention` 解析不了以 GMT 结尾的 HTTP 日期，且先把有效期截到 7 天再减 Age，原先任何真实响应都不落盘。后两处与 API 响应共用。
  - **测试目录不能放在 `/tmp` 下**：缓存目录要求所有上级目录都不能被组或其他人写（`safePath`），`/tmp` 不满足，缓存会报 Storage 错误。用 `build/linux-dev-local/xdg/` 这类目录。
- **第 3 步（登录）完成（2026-11-07，维护者实测 2026-11-08）**：
  - 后端加了手机号＋验证码、手机号＋密码、邮箱＋密码登录和发送验证码（[credential-login.md](../api/credential-login.md)），发送验证码可以和扫码同时进行。
  - `BackendSession`：扫码（`autoPoll`，二维码用 Nayuki 在本地画，`third_party/qrcodegen` v1.8.0）、手机号／邮箱登录、发送验证码、退出登录；后端就绪后若之前记住过账号（`account/remembered` 设置，按密钥环保存结果更新）就调用 `restore()`，没记住过的不恢复，游客启动时不会闪"正在登录"。失败码转成简短提示（`failureText`）。
  - 登录对话框改成左扫码、右输入（shell.md §4）。离屏验证：真实二维码能生成并显示，标签、分段、区号菜单、本地格式校验正常。
  - **维护者实测（2026-11-08）**：扫码登录、验证码登录成功；邮箱＋密码被网易风控，显示"网易要求安全验证，请扫码登录"，符合 D8 的处理；手机号＋密码输错时显示"密码错误"（502 映射正确），正确密码的成功路径未测（同一接口与验证码登录只差参数）。
  - 记住登录后重启恢复、退出登录也已实测通过（2026-11-08）。
  - 头像：后端的身份只有 ID 和昵称，侧边栏暂时显示默认头像。
- **第 4 步（搜索）代码完成（2026-11-08）**：
  - `MusicRequests`（请求分派器，§2）：Facade 音乐读取最多 16 条在途，这里同时最多 12 条，其余排队；就绪前等待，关闭／失败时以 Unknown 失败；StaleContext（会话变了）自动重发；Canceled 不回调；业务码 405（"操作频繁"）按 RateLimited 显示。
  - `BackendCatalog`：建议、热搜（用带说明的 `hotKeywordDetails`）、单曲／歌手／专辑／歌单／播客／声音六种结果分页接后端，其余目录方法仍转给假数据。旧搜索接口的单曲没有专辑封面：结果回来后再取这一页的歌曲详情补封面（失败就不带封面）。
  - 修了两个封面问题：`image://cover/` 的 key 没做 URL 编码（真实封面是网址，被弄坏）；资源缓存同时只下载 2 张封面，多出的返回 Busy，之前当失败处理，现在 200 ms 后重排。
  - 离屏验证：综合、单曲、歌手、专辑、歌单、播客、声音都出真实结果和封面。
  - **观察**：短时间内反复搜索（每次十来个请求）后，`/eapi/search/get` 先是偶发 HTTP/2 协议错误／连接被关，随后返回 405，判断是服务端限流，不改后端传输。页面显示可重试的错误。
  - **搜索接口**：维护者同意后，单曲／专辑／歌手／歌单搜索改用 PC 客户端的 `/api/cloudsearch/pc`（后端，2026-11-08）：单曲直接带专辑封面，不用再多取一次歌曲详情（补封面的逻辑留着，只在缺封面时生效），每页少一个请求。之前说"搜周杰伦排序差"是我判断错了：周杰伦的原版录音不在网易云，结果只能是翻唱、现场和用户上传；换接口后结果基本一样，搜陈奕迅时本人的歌排在最前。
  - 从搜索结果打开歌单／专辑／歌手、播放歌曲要等第 5、6 步（详情、播放）接好，现在打开会落到假数据，显示加载失败。
- **第 5 步（详情）代码完成（2026-11-08）**：
  - `BackendCatalog` 接上歌单、歌曲详情（分批，每次最多 `kSongDetailsBatch` 首）、专辑、歌手（头部＋热门 50 首同时读，只有头部失败页面才失败）、歌手的单曲／专辑分页、详情、相似歌手、播客与节目分页、每日推荐和历史日推。
  - 歌单：服务端给出完整曲目 ID（`trackIdsComplete`）时用它做索引，否则用随详情来的歌曲，`trackCount` 更多时标为 partial；创建者是当前账号时 `own`。专辑里没有封面的歌用专辑封面；歌手单曲等缺封面的列表仍走补封面。
  - 历史日推：后端要求日期必须来自服务端给的列表（`DailyHistorySongsQuery::fromDates`），所以保留最近一次的日期列表，换账号后不用旧列表。
  - `MusicRequests`：Facade 满了和"永远不会接"都返回 0，原先后者会每 200 ms 重试并堵住后面所有请求（游客打开首页时每日推荐把搜索也堵住了）。现在发送函数可以返回 `Refused`（按 Invalid 失败）或 `NeedsSignIn`（按 SignedOut 失败），只限登录的读取在游客态直接失败。
  - **后端修正**：封面请求带 `?param=<尺寸>y<尺寸>`，让图片主机先缩好（只在网络请求上，缓存键不变）。原图常超过 2048 像素／4 MiB，歌手头像和部分专辑封面被拒为 Unsupported／Capacity；顺带每张封面下载小得多。
  - 离屏验证：歌单页（标题、创建者、更新日期、简介、计数、封面）、歌手页（头像、别名、单曲／专辑数、简介、热门歌曲和封面）、歌手的专辑和相似歌手页签、专辑页（类型、发行日期、公司、首数与总时长、简介、歌曲）、播客页（节目、期号、最新／最早）都是真实数据。每日推荐和历史日推需要登录，离屏没测（游客态按 SignedOut 失败已确认不再堵队列）。
  - 少数播客封面上传的原图就是 WebP（图片主机仍标成 `image/jpg`）。维护者同意放开（2026-11-08）：缓存接受单帧 WebP，需要安装 Qt Image Formats（没装时 CMake 警告，这些封面显示占位图）。
  - **观察**：`cloudsearch` 也会限流，几分钟内搜五六次就开始 HTTP/2 协议错误，十分钟后恢复；正常使用的频率碰不到。
- **第 6 步（播放）第一部分完成（2026-11-08）**：播放先接了自己的音频引擎（[audio-engine.md](../tasks/audio-engine.md) 阶段 B、C）。
  - `BackendPlayback`（`backend_playback.{h,cpp}`）：包装 `ListeningSession`，由它的 `queueChanged`／`playbackChanged`／`fmChanged` 和 Facade 的会话、状态变化合成快照。队列按播放顺序（后端加了只读的 `playOrder()`；`queue()` 仍是插入顺序），展示层的位置换成条目 UUID。当前歌曲的详情向 `BackendCatalog` 的歌曲详情取（每首只取一次，失败不重试）。音质每次播放时读设置 `playback/audioQuality`。
  - 状态：Resolving／Loading 显示为 Loading；Ready 在意图为播放时仍是 Loading，否则是 Paused。播放失败的原因按 Network／Timeout／RateLimited／Rejected 等映射，无输出设备、存储错误显示通用的“播放失败”。
  - 位置：后端位置修订变了才换基准（基准时刻用会话记下的到达时间），否则在新快照里沿用当前时钟。只有状态变化或位置偏离外推超过 150 ms 才通知，播放中不会每 100 ms 刷一次界面。
  - 播放／暂停：已结束或失败时重新播放当前条目（`select(active)`），其余按会话的意图切换。音量先记在适配层，后端还没有播放时设的音量在下一次加载时补上。
  - `BackendLyrics`：正在播放的歌直接用会话已取的歌词，会话还在取时等它，不再发第二个请求（并发请求时偶发网络错误）；会话失败或换歌后、以及别的歌，走资源缓存再走网络。
  - 试听片段：快照加了 `previewStartMs`，`App.lyrics.shiftMs` 扣掉它，歌词按整首歌的时间对齐。
  - 还没做：拖动排序（D3）、启动恢复队列和自动播放（D7）、听歌上报（D5）。`playVoices` 仍不支持（`canPlayVoices` 为 false）。
  - 离屏验证（游客，音量 0）：富士山下（试听 30 秒）播放、拖到 20 秒、暂停与继续、下一首（晴天游客不可播，显示“这首歌暂时无法播放”）、`playAt`、单曲／列表循环、随机（当前歌曲排到第一位）、删掉正在播放的歌接着播下一首；迷你播放条显示真实封面和进度；播放页歌词（含粤拼）正常。私人 FM 需要登录，没测。
  - **维护者实测后的修正（2026-11-08）**：
    - 上一首改按播放顺序（队列里显示的顺序）：前 3 秒内回到前一首（列表循环时第一首回到最后一首），之后回到本曲开头；不再用后端按播放历史走的 `previous`（从列表中间开始播时它没有上一首）。私人 FM 仍用后端的。
    - 播放队列里正在播的那首没有信息：队列重置时把只有 key 的当前歌曲当成“已有详情”放进去了。现在只放有详情的，详情晚到时补进那一行；队列重排时保留已取到的详情。
    - 切歌后封面要等一会儿：新歌的详情要现取，封面再现下。现在 `Player` 预先取当前歌曲前后各一首的详情，切歌时先用队列里的详情显示；`upcomingCoverKey` 给出下一首的封面，播放页按页面和背景两种尺寸提前取好、解码好。离屏验证：下一首 0.7 秒内封面、标题、歌词都已换好。
  - FFmpeg 的警告不再输出（MP3 无缝裁剪每首都报一次 “Could not update timestamps for skipped samples”），只留错误。
