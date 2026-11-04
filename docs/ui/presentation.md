# 展示层：ViewModel 与数据源接口

> 状态：设计稿（2026-10-26）；骨架已实现（2026-10-30），实现情况见 §10。阶段 3、4 按此实现假数据版本，阶段 5 接入后端时只新增真实数据源，不改 QML。总体原则见 [README §3.2](README.md)；后端契约见 [HANDOFF](../HANDOFF.md)、[后台装配](../api/backend-runtime.md)、[队列与歌词时钟](../api/queue-and-lyric-clock.md)。

## 1. 目标

- QML 只看得到展示层：可绑定的属性、列表模型、意图方法。看不到后端的 DTO、请求 ID、会话 token、URL 和凭据。
- 页面先用假数据写完，接入后端时 QML 不用改。办法是 ViewModel 只依赖抽象的**数据源接口**，假实现和真实实现可以互换。
- 每个页面的加载、空、错误、离线、未登录、部分数据这几种状态，都能用假数据随时触发，方便在 Gallery 和调试面板里检查。

## 2. 分层与构建

```text
QML 页面 ──绑定/调用──▶ ViewModel（QObject，QML 可见）
                          │ 只依赖
                          ▼
                     数据源接口（纯 C++ 抽象类，QML 不可见）
                     ├─ Fake*Source     阶段 3、4：读 Cirrus.Samples，可模拟延迟和失败
                     └─ Backend*Source  阶段 5：包装 BackendFacade、ListeningSession 等
```

| 目标 | 目录 | 内容 | 链接 |
| --- | --- | --- | --- |
| `cirrus_presentation`（QML 模块 `Cirrus.Presentation`） | `ui/presentation/` | ViewModel、列表模型、展示值类型、数据源接口、假实现、封面图片提供器 | Qt Core/Gui/Qml/Quick/Network；**不链接后端** |
| `cirrus_presentation_backend` | `ui/presentation/backend/` | 真实数据源（领域类型 → 展示值类型的转换） | `cirrus_presentation`、`cirrus_backend` |

- Gallery 只链接 `cirrus_presentation`，永远用假数据。
- `appCirrusMusic` 阶段 3、4 也只用假数据；阶段 5 链接 `cirrus_presentation_backend`，在 `main.cpp` 里换成真实数据源。
- 展示值类型由 UI 自己定义（`ui/presentation/types.h`），不复用 `domain::` 类型。这样假实现不依赖后端，也从结构上保证 QML 拿不到 DTO。真实数据源负责转换。
- 这比 README §3.3 原来的写法（“只有 `Cirrus.Presentation` 链接后端”）更严格：链接后端的只有其中的 backend 子目标。

## 3. 装配

```cpp
// main.cpp（示意）
auto sources = cirrus::ui::makeFakeSources();          // 阶段 5：makeBackendSources(runtime)
cirrus::ui::App::install(std::move(sources));          // 必须在加载 QML 之前
engine.addImageProvider("cover", cirrus::ui::App::instance()->coverProvider());
engine.loadFromModule("CirrusMusic", "Main");
```

- `App` 是 C++ 单例（`QML_SINGLETON`，用 `static App* create(QQmlEngine*, QJSEngine*)` 返回预先装好的实例，所有权留在 C++），QML 里写 `App.player`、`App.session`。
- 全局状态挂在 `App` 下（§5.1），页面 ViewModel 由页面自己创建（§5.2），创建时从 `App` 取数据源。
- 所有 ViewModel 和数据源都在 GUI 线程。后端对象本来就要求在调用方线程使用（Facade 是调用方线程对象），不需要额外的线程切换。

## 4. 共同约定

### 4.1 请求状态

所有加载型 ViewModel 都有同一组属性：

| 属性 | 含义 |
| --- | --- |
| `state` | `LoadState.Idle`／`Loading`／`Ready`／`Empty`／`Error`／`Offline`／`SignedOut`／`Partial` |
| `refreshing` | 已有内容、正在刷新。刷新期间 `state` 保持 `Ready`，内容不清空 |
| `error` | `{ kind, message, retryable, retryAfterMs }`，`message` 是已翻译的文案 |
| `retry()` | 按最近一次请求重试 |

`state` 的语义：

- `Loading`：第一次加载，还没有任何内容，显示骨架屏。
- `Empty`：加载成功但没有内容（空歌单、无搜索结果）。
- `Partial`：有内容，但后端明确表示不完整（专辑 `complete = false`、歌曲详情有 `missing`、`IncompleteCatalog`）。内容照常显示，另加提示条。
- `Offline`、`SignedOut`：见 §4.2 的映射；`SignedOut` 只出现在需要账号的页面。

后端错误到状态的映射（真实实现照此转换，假实现照此模拟）：

| `MusicError` | 展示 |
| --- | --- |
| `Canceled`、`StaleContext` | 不展示。旧请求的结果直接丢弃 |
| `Network`、`Timeout` | 系统网络不可达时为 `Offline`，否则 `Error`（可重试） |
| `Busy`、`RateLimited` | `Error`，可重试，带 `retryAfterMs` 时按它延后 |
| `AuthenticationRequired` | `SignedOut` |
| `IncompleteCatalog` | `Partial` |
| `ProviderRejected`、`InvalidResponse`、`ProtocolFailure`、`InvalidRequest` | `Error`，不提供重试按钮（`InvalidRequest` 说明是程序错误，同时记日志） |

- 网络可达性来自 `QNetworkInformation`。处于 `Offline` 的页面在网络恢复后自动重试一次。
- 分页列表另有 `canLoadMore`、`loadingMore`、`loadMoreError`。后端 `hasMore` 未知时按“可能还有”处理，直到返回空页；不根据 `total` 或页面是否装满去猜。

### 4.2 请求的生命周期

- 每个 ViewModel 持有自己发出的请求句柄（RAII：句柄析构时取消请求）。离开页面、ViewModel 销毁、发起新查询时，旧请求自动取消。
- 结果回调**永不同步调用**（即使假实现也延后一个事件循环），最多调用一次，取消后和数据源销毁后不再调用。
- 账号变化（登录、注销、换号、会话过期）时，`App.session` 发出 `accountChanged()`：
  - 需要账号的数据（音乐库、喜欢、每日推荐、私人 FM）立即清空，回到 `Idle`，页面可见时重新加载。
  - 公开数据（搜索、歌单详情）保留已显示的内容；在途请求作废，后端会以 `StaleContext` 拒绝，按上表忽略并重发。
  - 外壳清空页面历史里需要账号的页面（README §6.2）。

### 4.3 标识与图片

- 歌曲、专辑、歌手、歌单都用**不透明的字符串 key** 标识。QML 只拿来传回给意图方法或作为路由参数，不解析、不拼接、不做数值运算。假数据的 key 是 `so-001` 这样的示例 ID，真实数据是后端的数字字符串，QML 不区分。
- 封面用 `coverKey` 表示，QML 写成 `"image://cover/" + coverKey`，尺寸由 `Image.sourceSize` 传给图片提供器。图片提供器（`QQuickAsyncImageProvider`）在后台解码、按显示尺寸缩放、带内存上限缓存。假实现把 key 映射到示例封面；真实实现阶段 5 接后端的封面缓存（UQ-06 决定缓存目录）。
- `Theme.cover` 现在只接受 `qrc:`／`file:`，阶段 5 前要改为接受封面 key，并与图片提供器共用同一次解码结果（README §9）。
- 缺封面时 `coverKey` 为空，组件显示占位图。

### 4.4 展示值与格式化

- 列表模型用角色（role）提供数据；单个对象（如当前歌曲）用值类型（`Q_GADGET` + `QML_VALUE_TYPE`，QML 中为小写类型名如 `songItem`）。
- 时长、数量、日期的格式化在 C++ 的 `App.format` 里做（`duration(ms)`、`count(n)`，按系统语言），QML 不写格式化逻辑。时长未知时为 -1，显示为“--:--”。
- 文案用 `tr()`／`qsTr()`，错误文案由 ViewModel 提供，页面不自己拼。

## 5. ViewModel 清单

### 5.1 全局（挂在 `App` 下）

**`App.session`：账号**

| 成员 | 说明 |
| --- | --- |
| `status` | `Starting`／`Guest`／`SigningIn`／`SignedIn`／`Expired`／`Unavailable`（后端启动失败） |
| `nickname`、`avatarKey` | 已登录时有效 |
| `qrLogin` | 扫码登录子模型：`qrImage`（`image://qr/...`，二维码在 C++ 里生成，QML 拿不到登录 URL）、`phase`（`WaitingForScan`／`WaitingForConfirmation`／`Confirming`／`Expired`／`Failed`）、`remember`（是否记住登录） |
| `beginQrLogin()`、`refreshQr()`、`cancelLogin()`、`signOut()` | 意图。返回值不代表成功，结果看 `status` |
| `accountChanged()` | 信号，见 §4.2 |

**`App.player`：当前播放**

| 成员 | 说明 |
| --- | --- |
| `current` | `songItem`：key、标题、副标题、歌手、专辑、`coverKey`、时长 |
| `status` | `Empty`／`Loading`／`Playing`／`Paused`／`Ended`／`Failed` |
| `failureMessage` | 失败时的已翻译文案，否则为空；能否跳过看 `canNext` |
| `positionMs`、`positionAt`、`rate` | 播放位置的**基准值**：基准位置、对应的单调时钟时刻、速率（播放中为 1，否则为 0）。见下方“播放位置” |
| `durationMs`、`seekable`、`volume` | 时长未知时为 -1 |
| `mode` | `Queue`／`PersonalFm`；`fmWaiting` 表示 FM 正在取下一首 |
| `order`、`repeat`、`canNext`、`canPrevious` | 播放顺序（顺序／随机）、循环（关／列表／单曲） |
| `playPause()`、`next()`、`previous()`、`seek(ms)`、`setVolume(v)`、`setOrder(o)`、`setRepeat(r)` | 意图 |
| `playSongs(keys, index)`、`playNext(keys)`、`append(keys)`、`startFm()` | 替换队列并播放、插到下一首、追加、开始私人 FM |

播放位置：C++ 只在后端快照到达时更新基准值，不每帧发信号。需要平滑位置的地方（进度条、逐字歌词）用 QML 辅助元素 `PlaybackClock`（基于 `FrameAnimation`）按 `positionMs + (now - positionAt) × rate` 逐帧计算；只显示文字时间的地方可以每 250 ms 刷新一次。

**`App.queue`：队列**（列表模型）

- 角色：歌曲的全部角色（§5.3）加 `entryKey`（同一首歌可以出现多次，每次是不同条目）、`active`。
- 意图：`select(entryKey)`、`remove(entryKey)`、`clear()`。
- 后端目前**不支持调整队列顺序**，所以不做拖动排序；需要时向后端提需求。

**`App.lyrics`：当前歌曲的歌词**

- `status`：`None`／`Loading`／`Ready`／`Failed`／`NoLyrics`／`Instrumental`（纯音乐只在后端明确标记时出现）。
- `timing`：`PlainText`／`Lines`／`Words`。逐字时间不可用时自动退回逐行。
- `lines`（列表模型）：每行有 `startMs`、`endMs`、`text`、`translation`、`romanization`、`words`（`[{startMs, durationMs, text}]`）。原文为主轨，有增强版（逐字）时优先用增强版；翻译、音译按时间对齐到原文行，对齐规则在阶段 4 用示例歌词确定，写进 [pages/player.md](pages/player.md) §5.6。
- `activeLines`：当前行（可能有多行重叠），来自后端的歌词游标。行内的逐字进度由 QML 用 `PlaybackClock` 计算。
- `hasTranslation`、`hasRomanization`、`offsetMs`、`setOffset(ms)`。是否显示翻译、音译属于用户设置（`App.settings`）。

**`App.library`：喜欢与收藏状态**

- `likedPlaylistKey`（账号的"我喜欢的音乐"歌单）、`likedCount`。
- `isLiked(songKey)` 与 `likedRevision`（喜欢集合变化时递增，供绑定刷新）；`SongListModel` 的 `liked` 角色跟着它变。登录后读取喜欢列表（`LibrarySource::likedSongKeys`），退出时清空，`reload()` 重新读取。
- `setLiked(songKey, liked)`：先更新界面，再等后端结果。失败时恢复原状并发 `notice(message)`；未登录时只发提示（页面应先打开登录对话框）。以后后端报告结果"未知"（取消、关闭、换号）时重新拉取喜欢列表，而不是假设成功。
- `addToPlaylist(playlistKey, title, songKeys)`：收藏到歌单（不是某个页面自己的写入，所以放在这里）。成功后发 `playlistChanged(key)`（正在显示该歌单的页面刷新，外壳刷新侧边栏）和 `notice`。
- `notice` 由外壳接到提示条（`shell.notify`）。收藏专辑、关注歌手以后照同样的写法加。

**`App.settings`：用户设置**（`QSettings` 持久化）

- 外观：明暗模式、种子来源、配色变体、减少动态效果。由 `Main.qml` 绑定到 `Theme`。
- 窗口：`titleBar`：`Custom`／`System`／`Hidden`；用户没改过时不保存，按桌面取默认值（平铺窗口管理器为 `Hidden`，其余 `Custom`），见 README §6.1（UQ-02）。
- 播放：音质；歌词：显示翻译、显示音译。
- 每项在 `app_settings.h` 里用一行 `CIRRUS_SETTING(...)` 声明（[pages/settings.md §7](pages/settings.md)）。

**`App.cache`：资源缓存**：`bytes`、`limitBytes`（未知为 -1）、`clearing`；`refresh()`、`clear()`、`cleared(ok)`。数据源 `CacheSource`：`usage()`、`clear()`，只清封面、歌曲信息和歌词的缓存，不动登录、音乐库和听歌记录。

**`App.network`**：`online`（来自 `QNetworkInformation`）。

### 5.2 页面（页面自己创建）

```qml
import Cirrus.Presentation

PlaylistPageModel {
    id: model
    playlistKey: route.key          // 设置 key 即开始加载
}
PageState { model: model }          // Cirrus.Components：按 state 显示骨架屏、空、错误……
SongList { model: model.songs; onActivated: (index) => App.player.playSongs(model.songs.keys(), index) }
```

| ViewModel | 页面 | 内容 |
| --- | --- | --- |
| `HomePageModel` | （原首页／发现原型，现在没有页面用） | `sections`（列表模型：每节有 `kind`、`title`、`items`），每日推荐入口、私人 FM 入口 |
| `DailySongsPageModel` | 每日推荐 | `songs`；未登录为 `SignedOut` |
| `SearchModel` | 搜索 | `text`；`suggestions`（分组，输入停顿 200 ms 后请求，新输入取消旧请求）；`hotKeywords`（带说明）；`history`、`removeHistory()`、`clearHistory()`；`submit()`；七个标签 `overview`（综合，4 个并发小请求和最佳匹配）／`songs`／`artists`／`albums`／`playlists`／`podcasts`／`voices`，单类标签各自分页、带总数，切到哪个标签才加载哪个，各有自己的状态（[pages/search.md](pages/search.md)） |
| `PlaylistPageModel` | 歌单详情 | 头部（标题、简介、创建者、`coverKey`、`tags`、`updatedText`、`playCount`／`subscriberCount`／`commentCount`（未知为 -1）、`own`）、`songs`；`subscribed` 可写（先改界面，失败时撤回并发 `writeFailed(message)`，成功时发 `libraryChanged()`） |
| `AlbumPageModel` | 专辑详情 | 头部（标题、歌手、发行时间）、`songs`；不完整时为 `Partial` |
| `ArtistPageModel` | 歌手详情 | 头部（名字、简介、头像）、`topSongs`、`albums`（分页） |
| `LibraryPageModel` | 侧边栏的歌单（原音乐库原型） | `likedSongs`、`playlists`（创建的、收藏的）、`albums`、`artists`、`recentSongs`；未登录为 `SignedOut` |
| `LibraryOverviewModel` 等 | 音乐库、最近播放、听歌排行 | 见 [pages/library.md §8](pages/library.md)：集合标签分页（歌单可按创建／收藏筛选）、新建歌单；`RecentPageModel`、`RankingPageModel` |
| `PlaylistCategoriesModel`、`DiscoverPlaylistsModel` 等 | 发现 | 见 [pages/discover.md §8](pages/discover.md)：分类目录，歌单（分类、最热／最新／精品）、歌手（类型、地区、首字母）、新碟（地区、最新／周榜／月榜）三个标签模型，筛选变了原地重载；榜单用 `ChartsModel`（`charts`、`others`） |
| `CommentsPageModel` | 评论 | 见 [pages/comments.md §9](pages/comments.md)：排序、分页的评论和原地展开的楼层（`CommentListModel`，平铺），点赞、发表、回复、删除 |
| `SettingsPage` | 设置 | 直接绑定 `App.settings`，不需要单独的 ViewModel |

页面 ViewModel 的公开属性只有 key（输入）和上面的内容（输出），不暴露“开始加载”的方法；`retry()`、`refresh()`、`loadMore()` 例外。

### 5.3 通用列表模型

**`SongListModel`**：所有歌曲列表共用（歌单、专辑、搜索结果、每日推荐、队列）。

| 角色 | 说明 |
| --- | --- |
| `key`、`title`、`secondaryTitle` | `secondaryTitle` 是别名或译名，可能为空 |
| `artists`、`artistText` | 歌手列表（`[{key, name}]`，key 可能为空）与拼好的显示文字 |
| `albumKey`、`albumTitle`、`coverKey` | 均可能为空 |
| `durationMs`、`durationText` | 未知时 -1 和“--:--” |
| `loaded` | 元数据是否已到。大歌单只有歌曲 key 时，行先占位，滚动到附近再批量取详情（`CatalogSource::songDetails`，每批不超过 `kSongDetailsBatch` = 100；`PlaylistPageModel::ensureLoaded(first, last)`／`loadAll()`） |
| `missing` | 请求过详情但服务端没有给（已下架等）；`SongListModel::fill()` 标记 |
| `unavailable` | 已知不可播放（播放失败后才知道；元数据本身不代表能否播放） |
| `current` | 是否为正在播放的歌曲（跟随 `App.player.current`） |
| `liked` | 跟随 `App.library` |

方法：`keys()`（全部 key，供“播放全部”）、`loadMore()`。

**`SortedSongsModel`**：歌曲表的显示顺序（[pages/playlist.md](pages/playlist.md) §10）。`source` 是一个 `SongListModel`；`filterText` 在标题、别名、歌手、专辑里查找（不分大小写，未到详情的行不匹配）；`sortField`（`Original`／`Title`／`Album`／`Duration`）和 `descending`，`cycleSort(field)` 按"升序 → 降序 → 原顺序"切换；未到详情的行和时长未知的行总在最后，相同的保持原顺序。`keys()` 是显示顺序（"播放全部"用它），`keyAt(row)`、`rowOf(key)`。

**`CollectionListModel`**：歌单、专辑、歌手的卡片列表共用。角色：`key`、`kind`（`Playlist`／`Album`／`Artist`）、`title`、`subtitle`、`coverKey`、`count`。

## 6. 数据源接口

纯 C++ 抽象类，放在 `ui/presentation/sources.h`。示意：

```cpp
namespace cirrus::ui {

struct Failure {
    enum Kind { Network, Timeout, Busy, RateLimited, SignedOut, Rejected, Invalid, Unknown } kind;
    std::optional<qint64> retryAfterMs;
};
template <class T> struct Outcome {
    std::optional<T> value;        // 有值时可能同时 partial = true
    bool partial = false;
    std::optional<Failure> failure;
};
template <class T> using Reply = std::function<void(Outcome<T>)>;

class CatalogSource {
public:
    virtual ~CatalogSource() = default;
    virtual Request playlist(const QString& key, Reply<PlaylistPage> reply) = 0;
    virtual Request songs(const QStringList& keys, Reply<QList<SongInfo>> reply) = 0;
    virtual Request search(SearchKind kind, const QString& text, PageRequest page, Reply<SearchPage> reply) = 0;
    // album、artist、artistSongs、artistAlbums、suggestions、hotKeywords、searchVoices、podcast、podcastVoices、home、dailySongs、dailyHistoryDates、dailyHistorySongs……
};

} // namespace cirrus::ui
```

- `Request` 是只能移动的句柄，析构即取消（§4.2）。
- 取消、`StaleContext` 不会以失败形式回调，回调根本不会发生。

| 接口 | 职责 |
| --- | --- |
| `SessionSource` | 账号状态、扫码登录、注销、账号变化通知 |
| `CatalogSource` | 只读目录：首页、发现（分类、分类歌单、精品歌单、榜单、歌手筛选、新碟与新碟榜）、搜索、建议、热搜、歌单／专辑／歌手详情、歌曲详情、每日推荐 |
| `CommentSource` | 歌曲／歌单／专辑的评论：排序分页读取、楼层；点赞、发表、回复、删除（要登录） |
| `LibrarySource` | 账号私有读取（喜欢、"我喜欢的音乐"歌单、我的歌单与收藏的专辑／歌手／播客、最近播放的各类、听歌排行）和写入（喜欢、收藏歌单、新建歌单、加入／移出歌单、调整顺序、改歌单信息、删除歌单） |
| `PlaybackSource` | 播放与队列命令、播放快照、队列快照、歌词与歌词游标、私人 FM |
| `ImageSource` | 按 key 和尺寸异步取图，供封面图片提供器使用 |

## 7. 假实现

- 数据来自 `Cirrus.Samples`（`catalog.json`、`covers/`、`lyrics/`，见 `ui/samples/README.md`），启动时读一次。
- 示例数据里的边界情况都会出现在页面上：缺封面（`al-13`、`pl-05`）、超长标题、未知时长（`so-008`）、不可播放（`so-012`）、空歌单（`pl-05`）。
- `FakePlayback` 用单调时钟模拟播放：推进位置、自然结束、按顺序／随机／循环切歌、按示例歌词给出游标，`so-012` 播放时报“不可用”。
- **调节项**（`App.debug`，只在 Debug 构建和 Gallery 中存在）：
  - 延迟：默认 300 ms，带随机抖动；可调为 0（截图用）或几秒（看骨架屏）。
  - 失败注入：下一次请求失败、某类页面始终失败、离线、返回空、返回部分数据。
  - 账号：已登录／游客／会话过期，切换时触发 `accountChanged()`。
- 应用里用 `Ctrl+Shift+D` 打开调试面板操作这些调节项；Gallery 新增“页面状态”页，把每种页面状态并排列出。阶段 3 的完成条件“所有页面状态都能触发”靠这两处检查。

## 8. 真实实现（阶段 5）

| 展示层 | 后端 |
| --- | --- |
| `SessionSource` | `BackendFacade`：`start`、`restore`、`beginLogin`、`poll`、`cancel`、`signOut`；`sessionChanged`、`challengeReady`、`statusChanged`、`signedIn`、`failed` |
| `CatalogSource` | `searchSongs`／`searchAlbums`／`searchArtists`／`searchPlaylists`、`searchSuggestions`、`hotKeywords`、`playlistDetails`、`playlistSongs`、`songDetails`、`albumDetails`、`artistDetails`、`artistSongs`、`artistAlbums`、`discover`、`recommendedPlaylists`、`dailyRecommendedPlaylists`、`charts`、`dailyRecommendations` |
| `LibrarySource` | `likedSongIds`、`myPlaylists`、`savedAlbums`、`savedArtists`、`recentSongs`；写入 `mutateCollection`（`SongHeart` 等） |
| `PlaybackSource` | `ListeningSession`（`BackendRuntime::listening()`）：队列、播放控制、歌词、游标、`startFm` |
| `ImageSource` | `BackendRuntime::resourceQueries()`／`resourceCache()` 的封面读取 |

必须遵守的后端规则：

- 每个结果按命令 ID 和会话 token 核对，过期结果丢弃。
- 使用队列时，`ListeningSession` 是**唯一**的播放命令来源，不能再直接调用 Facade 的 `playSong` 等方法。
- 听歌上报（`reportListening`）是显式写操作，播放时**不自动调用**；产品是否上报另行决定。
- 退出程序：在事件循环仍运行时先 `close()`，等 `stopped` 后再退出（[后台装配 §3](../api/backend-runtime.md)）。

## 9. 待定事项

- **二维码生成**：需要一个生成二维码图像的库（候选 Nayuki QR Code generator，MIT），属于新依赖，接入扫码登录前请维护者确认。假实现先用示例图片。
- **队列排序**：后端暂不支持移动队列条目。
- **翻译与原文的对齐规则**：阶段 4 写入 [pages/player.md](pages/player.md) §5.6。
- **切换标题栏模式是否需要重启**：见 README §6.1，原型中确定。

## 10. 实现情况（阶段 3 第 2 步，2026-10-30）

代码在 `ui/presentation/`（静态库 `cirrus_presentation`，QML 模块 `Cirrus.Presentation`），应用和 Gallery 都链接它并装入假数据源；Gallery 的“数据与状态”页可以操作 `App.debug`、检查各种状态。

| 已实现 | 说明 |
| --- | --- |
| `LoadState`、`Failure` | QML 中为 `LoadState.Ready`、`Failure.Network` |
| `Request`、`Outcome<T>`、`Reply<T>` | `request.h`；句柄析构即取消 |
| `CatalogSource`、`ImageSource`、`Sources` | `sources.h`；接口方法随页面需要逐个添加，目前只有 `playlist()` 与 `image()` |
| `App` | `settings`、`format`、`network`、`debug`；`makeCoverProvider()` |
| `App.settings` | 外观（明暗、种子来源、配色变体、减少动态效果，存 Theme 枚举的整数值）、`titleBar`（未设置为 -1，按桌面取默认）、歌词翻译／音译；QSettings 位置用开发占位的组织名和应用名（UQ-06）。`Main.qml` 用 `Binding` 把外观绑到 Theme |
| `App.format` | `duration(ms)`、`count(n)`（中日文按万／亿，其他按 K／M／B） |
| `App.network` | `QNetworkInformation` 可达性，无后端时视为在线；假数据的“离线”开关也作用于它 |
| `App.debug`（`FakeControls`） | 延迟（默认 300 ms，±30% 抖动）、下一次失败、离线、空结果、部分数据；只在装入假数据源时存在 |
| 封面图片提供器 | `image://cover/<key>`：在 GUI 线程向 `ImageSource` 取编码数据，在线程池按 `sourceSize` 解码缩放（覆盖请求尺寸、不放大），解码结果进 64 MiB 内存缓存。被取消的请求仍会完成并写入缓存，只是不再交付 |
| `LoadableModel` | 加载型 ViewModel 的基类：单个在途请求、§4.1 的状态与错误映射、`retry()`／`refresh()`、刷新失败保留内容、`Offline` 下网络恢复后自动重试 |
| `SongListModel` | §5.3 的角色，`current`、`liked` 留到 `App.player`、`App.library` 实现时加 |
| `PlaylistPageModel` | 示范页面模型：设置 `playlistKey` 即加载 |
| 假数据源 | `fake_sources.cpp` 读 `Cirrus.Samples` 的 `catalog.json`；封面 key 为示例封面名（如 `sunset`）。封面请求不受延迟和失败注入影响 |

第 3 步（外壳）加入：

| 已实现 | 说明 |
| --- | --- |
| `PlaybackSource` | `sources.h`：`snapshot()` 返回完整的 `PlaybackSnapshot`（`playback.h`），变化后经 `setChangedHandler` 的回调通知（不在命令调用里同步回调）；命令 `playSongs`、`playPause`、`next`、`previous`（播放超过 3 秒时回到开头）、`seek`、`setVolume`、`setOrder`、`setRepeat`。`playNext`、`append`、`startFm` 随阶段 4 的队列面板加 |
| `App.player`（`Player`） | §5.1 的成员，枚举在 QML 中为 `Playback.Playing`、`Playback.Shuffle`、`Playback.RepeatOne`；`current` 是 `songItem` 值类型（与 `SongListModel` 角色同名）；`now()` 给出 `positionAt` 所用的单调时钟（毫秒） |
| `PlaybackClock`（`Cirrus.Components`） | `positionMs` 逐帧外推，只在播放中且 `active` 时运行 |
| `SongListModel.current` | 跟随 `App.player.current`，只更新受影响的行 |
| `FakePlayback` | 加载耗时取 `App.debug.delayMs`；歌曲自然结束后按顺序／随机、循环（关、列表、单曲）继续；列表放完进入 `Ended`；`so-012` 与离线时进入 `Failed`；时长未知的 `so-008` 按 200 秒模拟且不能拖动。示例目录的 `playable: false` 现在映射为 `unavailable` |

尚未实现：`App.session`、`App.queue`、`App.lyrics`、`App.library`，`CollectionListModel` 与其他页面模型（随第 4 步的页面加），大歌单按需批量取详情，账号变化处理（§4.2）。

验证：用临时检查程序（未提交）在离屏下跑通 13 项状态转换（加载、空、拒绝、网络失败与重试、刷新失败保留内容、离线与恢复后自动重试、未登录、部分数据、新请求取代旧请求、空 key 回到 Idle），以及时长、数量格式化。
