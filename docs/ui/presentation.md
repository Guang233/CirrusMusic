# 展示层：ViewModel 与数据源接口

> 状态：设计稿（2026-10-26）。阶段 3、4 按此实现假数据版本，阶段 5 接入后端时只新增真实数据源，不改 QML。总体原则见 [README §3.2](README.md)；后端契约见 [HANDOFF](../HANDOFF.md)、[后台装配](../api/backend-runtime.md)、[队列与歌词时钟](../api/queue-and-lyric-clock.md)。

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
| `failure` | 失败时的已翻译文案和是否可跳过 |
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
- `lines`（列表模型）：每行有 `startMs`、`endMs`、`text`、`translation`、`romanization`、`words`（`[{startMs, durationMs, text}]`）。原文为主轨，有增强版（逐字）时优先用增强版；翻译、音译按时间对齐到原文行，对齐规则在阶段 4 用示例歌词确定，写进 `player.md`。
- `activeLines`：当前行（可能有多行重叠），来自后端的歌词游标。行内的逐字进度由 QML 用 `PlaybackClock` 计算。
- `hasTranslation`、`hasRomanization`、`offsetMs`、`setOffset(ms)`。是否显示翻译、音译属于用户设置（`App.settings`）。

**`App.library`：喜欢与收藏状态**

- `isLiked(songKey)` 与 `likedRevision`（喜欢集合变化时递增，供绑定刷新）。
- `setLiked(songKey, liked)`：先更新界面，再等后端结果。失败时恢复原状并提示；结果为“未知”（取消、关闭、换号时后端会这样报告）时重新拉取喜欢列表，而不是假设成功。
- 收藏专辑、关注歌手、收藏歌单到某个歌单：同样的写法，阶段 3 先做“喜欢”。

**`App.settings`：用户设置**（`QSettings` 持久化）

- 外观：明暗模式、种子来源、配色变体、减少动态效果。由 `Main.qml` 绑定到 `Theme`。
- 窗口：`titleBar`：`Custom`／`System`／`Hidden`；用户没改过时不保存，按桌面取默认值（平铺窗口管理器为 `Hidden`，其余 `Custom`），见 README §6.1（UQ-02）。
- 播放：音质；歌词：显示翻译、显示音译。

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
| `HomePageModel` | 首页／发现 | `sections`（列表模型：每节有 `kind`、`title`、`items`），每日推荐入口、私人 FM 入口 |
| `DailySongsPageModel` | 每日推荐 | `songs`；未登录为 `SignedOut` |
| `SearchModel` | 搜索 | `text`；`suggestions`（输入停顿 200 ms 后请求，新输入取消旧请求）；`hotKeywords`；`submit()`；四个结果标签页 `songs`／`albums`／`artists`／`playlists`，各自分页，切到哪个标签才加载哪个，各有自己的状态 |
| `PlaylistPageModel` | 歌单详情 | 头部（标题、简介、创建者、`coverKey`、歌曲数）、`songs` |
| `AlbumPageModel` | 专辑详情 | 头部（标题、歌手、发行时间）、`songs`；不完整时为 `Partial` |
| `ArtistPageModel` | 歌手详情 | 头部（名字、简介、头像）、`topSongs`、`albums`（分页） |
| `LibraryPageModel` | 音乐库 | `likedSongs`、`playlists`（创建的、收藏的）、`albums`、`artists`、`recentSongs`；未登录为 `SignedOut` |
| `SettingsPage` | 设置 | 直接绑定 `App.settings`，不需要单独的 ViewModel |
| `CommentsPageModel` | 评论 | 第一轮页面之后再做 |

页面 ViewModel 的公开属性只有 key（输入）和上面的内容（输出），不暴露“开始加载”的方法；`retry()`、`refresh()`、`loadMore()` 例外。

### 5.3 通用列表模型

**`SongListModel`**：所有歌曲列表共用（歌单、专辑、搜索结果、每日推荐、队列）。

| 角色 | 说明 |
| --- | --- |
| `key`、`title`、`secondaryTitle` | `secondaryTitle` 是别名或译名，可能为空 |
| `artists`、`artistText` | 歌手列表（`[{key, name}]`，key 可能为空）与拼好的显示文字 |
| `albumKey`、`albumTitle`、`coverKey` | 均可能为空 |
| `durationMs`、`durationText` | 未知时 -1 和“--:--” |
| `loaded` | 元数据是否已到。大歌单只有歌曲 key 时，行先占位，滚动到附近再批量取详情（每批不超过后端上限） |
| `unavailable` | 已知不可播放（播放失败后才知道；元数据本身不代表能否播放） |
| `current` | 是否为正在播放的歌曲（跟随 `App.player.current`） |
| `liked` | 跟随 `App.library` |

方法：`keys()`（全部 key，供“播放全部”）、`loadMore()`。

**`CollectionListModel`**：歌单、专辑、歌手的卡片列表共用。角色：`key`、`kind`（`Playlist`／`Album`／`Artist`）、`title`、`subtitle`、`coverKey`、`count`。

## 6. 数据源接口

纯 C++ 抽象类，放在 `ui/presentation/sources/`。示意：

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
    // album、artist、artistSongs、artistAlbums、suggestions、hotKeywords、home、dailySongs……
};

} // namespace cirrus::ui
```

- `Request` 是只能移动的句柄，析构即取消（§4.2）。
- 取消、`StaleContext` 不会以失败形式回调，回调根本不会发生。

| 接口 | 职责 |
| --- | --- |
| `SessionSource` | 账号状态、扫码登录、注销、账号变化通知 |
| `CatalogSource` | 只读目录：首页、搜索、建议、热搜、歌单／专辑／歌手详情、歌曲详情、每日推荐 |
| `LibrarySource` | 账号私有读取（喜欢、我的歌单、收藏、最近播放）和写入（喜欢、收藏） |
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
- **翻译与原文的对齐规则**：阶段 4 写入 `player.md`。
- **切换标题栏模式是否需要重启**：见 README §6.1，原型中确定。
