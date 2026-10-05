# 六项歌单发现读取：推荐、列表、精品与分类

## 1. 成组范围与入口

按维护者要求一次完成多个相关只读接口，合并为歌单发现包：

```cpp
quint64 BackendFacade::recommendedPlaylists(int limit = 30);
quint64 BackendFacade::playlistList(PlaylistListQuery query = {});
quint64 BackendFacade::premiumPlaylists(PremiumPlaylistsQuery query = {});
quint64 BackendFacade::playlistTags(PlaylistTagsKind kind = PlaylistTagsKind::Catalogue);
// kind: Catalogue / Hot / Premium，三个独立远端读取
```

共6项远端operation，业务registry由25到31。推荐歌单与[每日歌曲推荐](discovery-and-fm.md)不是一个接口／DTO；普通分类、热门分类、精品标签也是不同来源，不相互替代。

复用MusicQuery／MusicResult、16槽邮箱、MusicQueryService、取消和Runtime后台装配；当前Ready的Guest／Authenticated可读。不传任意uid、不自动认证／恢复，不建立API转发器、全局歌单缓存、轮询器、新线程或运行依赖。非零ID是受理，不是成功；仍通过 `musicCompleted(id, originalContext, result)`返回值，`cancelMusic(id)`取消。domain／application保持Qt Core-only，模板入口／UI不变。

## 2. 固定参考与协议

固定参考 `a8c781fd64faab17fedfd46e0615a2609307f163`；以下6个模块的SHA-256均与inventory匹配，文档为 `public/docs/home.md`，没有运行上游Node或新增官方探测。

| operation／上游模块 | 逻辑路径 | 协议／JSON响应上限 | 固定业务payload |
| --- | --- | --- | --- |
| RecommendedPlaylists／personalized.js | `/api/personalized/playlist` | weapi／4MiB | limit默认30，total:true，n:1000；无offset |
| PlaylistList／top_playlist.js | `/api/playlist/list` | weapi／4MiB | cat默认全部，order默认hot／可选new，limit默认50，offset默认0，total:true |
| PremiumPlaylists／top_playlist_highquality.js | `/api/playlist/highquality/list` | weapi／4MiB | cat默认全部，limit默认50，lasttime来自before（默认0），total:true；无offset |
| PlaylistCatalogue／playlist_catlist.js | `/api/playlist/catalogue` | eapi／1MiB | 空对象 |
| HotPlaylistTags／playlist_hot.js | `/api/playlist/hottags` | weapi／512KiB | 空对象 |
| PremiumPlaylistTags／playlist_highquality_tags.js | `/api/playlist/highquality/tags` | weapi／1MiB | 空对象 |

只用原固定eapi／weapi官方目标与通用profile，禁止调用方传crypto、host、headers、total、n等内部字段。三种列表limit限制1～100；普通offset0～100000。before为安全非负整数（0～2^53−1），拒绝小数／bool／溢出，不按秒／本机日期改写。

cat是128 UTF-16以内、合法且无C0／DEL控制字符、非空／无外围空白的**不透明标签**；保留中文、R&B/Soul等标点作为JSON业务值，不拼到URL。不会把未知标签改成全部，也不照抄JS `limit || default`替无效0兜底。是否接受具体标签仍由服务端决定；不造本地固定分类名单或要求先自动请求分类。

普通分类不保证适用于精品；精品筛选的标签来源应为Premium。上游top_playlist将整个响应JSON里的avatarImgId_str改名，原生没有复制这段全局文本替换：这里只映射选定摘要字段，不发布头像／creator对象或原始JSON。

所有响应都不采纳Set-Cookie、不产生身份收据或改变记忆凭据。Guest可发起不等于已经证实所有地区匿名可用；业务失败不自动登录、重试或删除凭据。429保留合法Retry-After。取消／切号／退出／close保留原token／generation并隔离晚到结果；调用方持有的DTO副本仍须在上下文失效时丢弃。

## 3. DTO、未知字段与分页

`domain/music/playlist_discovery_types.h`定义4种query与4种result。JSON、路径、Cookie和response schema均在 `music/playlist_discovery_mapper.*`，不穿透Core层。

### 歌单摘要

`DiscoveryPlaylist`保存PlaylistId、标题、封面、可选summary／trackCount／playCount／providerUpdateTime，不带歌曲全集、所有者／收藏状态、头像或用户profile。

- 推荐读取root.result数组，picUrl／copywriter；普通和精品读取root.playlists数组，coverImgUrl／description。不会猜其他包装层或跨来源兜底。
- 使用原ID／文本／图片映射：规范正signed64字符串；JSON数值ID≤2^53−1，wide ID必须为字符串。文本≤4096、合法UTF-16、无NUL；标题缺失／null沿用共享映射为空展示标签，可选summary等字段缺失／null保持未知。图片仅作元数据，不抓取，非法／不支持URL降为不可用。
- trackCount与updateTime存在时须为安全非负整数；updateTime保留原字段，不推算墙钟／服务器日期。playCount接受有界有限非负JSON数（允许小数），是提供方聚合显示值，不是当前账号听过次数、分值或时长。
- 每页至多请求limit项，保留服务端顺序和重复歌单ID；不会本地排序／合并。坏行／超限整体失败，不跳过或静默截断。元数据不证明歌曲／下载权益。

### 三类分页不能混用

- `RecommendedPlaylists`只有query和列表，没有虚构offset／total／hasMore。n:1000是固定兼容载荷，不授权自动抓取1000首歌或整个库。
- `PlaylistListPage`保留原category／order／limit／offset，可选root.total与root.more。缺失／null未知；已给计数必须非负且不小于当页条数，more必须bool。短页不能推断结束。
- `PremiumPlaylistsPage`保留before、可选total／more和原root.lasttime（providerLastTime）。**固定文档要求下一页before取上一页最后一个歌单updateTime**，调用方应明确采用该行的providerUpdateTime；不把offset、本机时间、页数或root.lasttime擅自改作该值。
- 若末项updateTime未知、页为空、cursor不前进、more未知，调用方不能猜测进展或自发循环。mapper只保留原字段，不承诺游标进展；本包没有自动翻页器、重试或fallback。不同页面没有原子快照保证，也不把数页拼成完整服务端库。

### 三类标签／分类

`PlaylistTags`保留query.kind、tags数组、可选all与可选categories字典。每个 `PlaylistTag`仅含不透明name、可选categoryCode／hot；category code允许0，不是音乐ID，hot缺失不强设false／true。

- Catalogue读取root.sub；all与categories若缺失／null保持未知，显式空字典与未知不同。
- Hot和Premium各自读取root.tags，不伪造Catalogue的all／分组，不把热门标记自动写为true，也不合并三份列表。
- Catalogue／Premium至多500标签，Hot至多100，分组至多50。这些是产品防护上限，不是已验证的服务端固定数量；超限失败，不截断。
- 标签／分组名遵循cat的128单元规则；分组键必须是规范安全非负十进制数。categories使用QMap表达字典，不赋予对象键“提供方顺序”；数组保留原次序／重复。未知categoryCode原值保留，不凭不完整分组表猜名称或将其改到“其他”。

空数组合法，缺失／null／错误数组、坏ID／文本／数值／bool、矛盾total或超限整体InvalidResponse；没有“HTTP200即页面成功”的宽松解析。

## 4. 证据限制与原播放链

固定模块／文档可确认路径、协议、请求字段、推荐无offset与精品before取末项updateTime等行为；**没有完整响应schema／本轮真实账号或公开只读样本**。result／playlists、total／more／lasttime、sub／tags／all／categories等是当前客户端的有界decoder契约，仅合成fixture验证，不宣称官方确认或广泛实网兼容。真实验证另授权，不保存真实库、原始provider响应或有版权曲目fixture。

选定摘要的PlaylistId后仍调用现有[歌单详情与两阶段歌曲页](catalog-queries.md)，确认完整trackIds再只补当前页详情，保留缺失与原顺序。调用方明确选SongId并通过既有ListeningSession.append／select播放；读取／翻页／摘要不是播放行为，不自动入队、播放或发[报告](explicit-listening-reports.md)。没有新歌单歌曲端点、专属播放器、来源归属模型或自动报告策略。

## 5. 定向离线验证

本机Qt6.11.2 Debug，`LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1`；各套件最新结果（含数据行、init/cleanup，不重复累计）：

| suite | pass |
| --- | ---: |
| playlist_discovery | 54 |
| catalog | 22 |
| music | 26 |
| charts | 28 |
| session_api_client | 24 |
| backend_facade | 8 |
| 合计 | **162** |

线程选择 `playlistDiscoveryThroughThread`、`chartsToPlaylistPlaybackThroughThread`、`cloudHistoryThroughThread`、`playbackThroughThread`的Queue／FM两行与 `destructionStillDrains`，共**8 pass**（含init/cleanup）。新用例逐项跨线程返回6种operation的值，显式用末项updateTime而非不同的root.lasttime翻精品下一页，再用推荐歌单ID贯通原详情／页补详情／明确选SongId2入队到假后端Playing／歌词Ready，最后在挂起分类请求中关闭排空。没有真实CDN、音频或设备声学证明。

单元覆盖6项精确payload／路径／协议／响应上限和默认值、推荐／offset／before区别、最大安全before、未知元数据／小数playCount／wide ID／重复顺序、类别0／未知分组与三类来源区别、27条坏schema数据行、500／100／50容量／文本边界、6项Guest与Authenticated共12条原生链、Cookie不回写、每operation取消／旧token／429及Facade16槽／晚到隔离。读取不自动补详情／翻页／播放／反馈。

```bash
cmake --build --preset linux-dev-local --target \
  tst_playlist_discovery tst_catalog tst_music tst_charts \
  tst_session_api_client tst_backend_facade tst_backend_runtime --parallel 4
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(playlist_discovery|catalog|music|charts|session_api_client|backend_facade)$' \
  -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  playlistDiscoveryThroughThread chartsToPlaylistPlaybackThroughThread cloudHistoryThroughThread \
  playbackThroughThread destructionStillDrains
```

当前登记32个CTest套件，不是32套全量验收；未跑全量认证／Release、真实keyring／账号／声卡矩阵。忽略日志 `build/workspace-checks/playlist-discovery-*.log`，最新final-build／final-focused／final-thread，不累计此前同选择的重跑。12个改变C++文件最终active LSP无诊断、format／diff检查通过；439个本地文档链接无断链（排除代码示例的LRC记号），13个手写application TU加AUTOMOC共14个已编译对象的实际依赖不含基础设施／Network／Multimedia／Sql／Quick／Qml／Gui／JSON；UI差异为0。

新test初始编译数据库／preamble带来的QHttpHeaders缺失、variant／QSignalSpy／Qt operator&连锁诊断在刷新数据库及文档版本后的active LSP消失；不移除必要include、写压制注释或放宽验证。

后续可成组补其他音乐搜索／评论等只读链；喜欢、收藏、创建／编辑、缓存／同步、文件下载与一起听仍单列。UI、真实写入、自动报告及详细本地历史持久化未因本包而交付。
