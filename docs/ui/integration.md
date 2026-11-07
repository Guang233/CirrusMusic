# 阶段 5：业务接入计划

> 状态：**待维护者评审**（2026-11-07）。评审通过、§6 的决定落定后再动代码。
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
| 试听片段 | `PlaybackSnapshot.preview` | ⚠️ 新：非会员歌曲只能试听时，迷你播放条和播放页标出“试听”，进度条只到片段长度 |

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
| D1 | **UQ-06 应用身份**：app ID、数据目录、密钥环 service、封面缓存目录 | app ID `io.github.guang233.CirrusMusic`；数据库 `QStandardPaths::AppDataLocation/state.sqlite`（Linux：`~/.local/share/CirrusMusic/`）；封面缓存 `CacheLocation/resources`（`~/.cache/CirrusMusic/resources`，目录权限 0700）；密钥环 service `CirrusMusic`。以后改 app ID 会让已记住的账号失效，所以要现在定 |
| D2 | **二维码生成库** | 引入 Nayuki QR Code generator（MIT，单个 .cpp/.hpp，放 `third_party/`，不改源码），二维码在本地生成，不把登录链接发给任何第三方服务 |
| D3 | **队列拖动排序**：后端没有移动条目 | 我按后端的约定（HANDOFF）给 `ListeningSession` 加 `move(entry, index)`，只改顺序不重新加载。不愿意动后端的话：接入后先隐藏拖动把手，Alt+↑/↓ 也关掉 |
| D4 | **收藏别人的歌单**：后端暂缓 | 接入后隐藏歌单页的“收藏”按钮，音乐库照常显示已收藏的歌单（读取是好的）。等后端解决 v2 门槛再打开 |
| D5 | **听歌上报**（`reportListening`，后端规定播放时不自动调用） | 先不上报。上报会影响网易云的听歌排行和推荐，属于产品决定；以后可以加成设置项（默认关） |
| D6 | **退出卡住**：密钥环不回调时 `stopped` 永远不来 | 等 `closeDelayed` 后再给 5 秒（共 10 秒），仍未停止就直接退出，并在日志里记一条。正常情况下不会遇到 |
| D7 | **启动时恢复上次的播放队列**（后端 `persistListeningState`，现在关着；设置页那一项写着“等后端”） | 打开后端持久化，设置项默认关；打开时只恢复队列，不自动播放（后端就是这样设计的） |
| D8 | **真实网络访问**：接入后运行应用就会访问网易云的官方接口，登录用你的账号 | 确认可以。开发期间我只在你要求时运行需要登录的检查，不自动登录 |

## 7. 风险

- **并发上限**：首页、发现页一次会发很多读，16 条在途的上限会让后面的分区排队变慢。分派器按页面先后排队，必要时页面分批加载（先首屏）。
- **服务端差异**：后端的读取大多只有离线测试和少量公开窗口验证（HANDOFF §1）。接入时遇到响应结构不符，按 AGENTS 的约定报给你，不在 UI 侧绕过后端校验。
- **全量构建失败**：`tst_entity_search` 现在会失败（AGENTS 里记的已有问题），不影响 UI 目标和 `cirrus_backend`，但接入后要跑后端测试时会碰到。
