# 后端覆盖审计：107 operations 不等于107项产品能力

## 1. 初始审计、后续更新与判定标准

本页初始响应Together HTTP-only之后的覆盖盘点，代码基线 **`7b0d7620cd05a7f01245b19eefc3db9b9fceec03`**，docs-only交付53f8f3e时为97ops／48登记；没有该审计轮Qt/build/LSP diagnostics/actualdeps或新业务证据。后续维护者明确先补[常规发现9读取＋复杂百科审计](discovery-browse-reads.md)，本页随该代码包更新，不把新合成测试倒填旧审计。当时复杂百科缺schema不注册；后续[公开metadata窗口](discovery-public-response-verification.md)仅窄授权。最新[受限纯文本子集](song-wiki-text-subset.md)另获四次公开百科窗口和明确接入选择，仅两类文本/原请求关联/内容身份未独立验证；完整百科仍G，其他paused/账号/SDK/UI边界不恢复。

- 当前[ApiOperation](../../src/infrastructure/netease/api/api_operation.h)：**107唯一枚举**，首QrKey、末SongWikiText；header SHA-256 `54f2dc453c88690b839ac2901654752d18c40a1f1006648552dc4b897c2155c2`。旧106 header hash `fd6b3e4fa45da2d89da45087cb00cc34f933a88058e2d89ddb8f28baca131894`仅历史。初始97 header hash `42def4780a2f8d9cc916e1183b10cc9337a2bf62a9da6b3bc57b339858967ec7`仅历史。
- 当前root contract与collection/comment/comment-write/together/discovery-browse五个窄adapter、[Facade声明](../../src/application/backend/backend_facade.h)及实际接线；不是从旧review graph推当前支持。发现单Core discover(typed query)复用MusicQuery/read16/原worker/Gateway，无owner或下游新增。
- 当前`ctest --show-only=json-v1`实核**51注册套件／playback_observation1／song_wiki_text1／discovery_browse1／together_http1**；只是登记，不是全51测试或107用户能力验证。本轮affected/选定runtime单列§10，§9为上包历史。
- 下表人工归组26组（本轮受限百科单列一组）；E=Eapi、W=Weapi、X=Xeapi，合同 **39 E／63 W／5 X**。默认host/profile沿原route；Together12窄destination、报告2clientlog。path相同不意味着协议／输入／业务相同。
- 固定参考仍v4.40.1／`a8c781fd64faab17fedfd46e0615a2609307f163`：440模块＝164adopt／40defer／236exclude。**不能用164−97计算缺口**：module、逻辑path、operation、用户能力是四种单位；同path多动作／本地组合／新增doc接口／后续消费范围覆盖均存在。inventory的not-implemented/static-only是原审计时点标记，不重写成动态支持表。
- `main.cpp`／`Main.qml`仍模板。Core backend可调用不等于产品UI可用、服务兼容、会员／权限、远端效果或完整数据已验证。

判定分为：**I**已有代码／装配；**S**有所属工作包的合成证据（仅引用各契约历史，不自动给每个分支盖章）；**L**单列有限授权公开／媒体观察，不概括为全服务；**G**缺证／尚未实现；**P**维护者明确暂停；**X**排除或旧候选被后续范围决定覆盖。无“登记即FR完成”或百分比。

## 2. 已接入入口的26组索引

这些是内部Core／装配入口，不是QML注册。组内operation并不逐项对应独立Facade方法：`charts(kind)`、`playlistTags(kind)`、`comments(kind)`、各typed mutation及组合`playlistSongs`等有意复用。SongStream由原playSong/resolver消费，不新增暴露原始音源或物理→逻辑反推入口。

| 组 | 操作数 | Core入口／装配入口 | 需求 | 实现契约／历史证据 |
| --- | ---: | --- | --- | --- |
| 认证／匿名启动 | 4 | `beginLogin/poll/restore；start的匿名bootstrap` | FR-001/002/003/004 | [authentication-transaction.md](authentication-transaction.md) |
| 歌曲查询／歌词／音源 | 4 | `searchSongs/songDetails/lyrics；playSong的resolver` | FR-006/008/017/018/021/022 | [music-queries-and-lyrics.md](music-queries-and-lyrics.md) |
| 五项目录 | 5 | `albumDetails/artistDetails/artistSongs/artistAlbums/playlistDetails` | FR-008/009/010 | [catalog-queries.md](catalog-queries.md) |
| 每日歌曲／FM条目 | 2 | `dailyRecommendations/personalFmBatch` | FR-015/016 | [discovery-and-fm.md](discovery-and-fm.md) |
| 当前账号四库 | 4 | `myPlaylists/likedSongIds/savedAlbums/savedArtists` | FR-010/013/014 | [personal-library.md](personal-library.md) |
| 显式听歌报告 | 2 | `reportListening(StartPlay/Play)` | FR-017/024 | [explicit-listening-reports.md](explicit-listening-reports.md) |
| 云端歌曲历史／排行 | 2 | `recentSongs/listeningRanking` | FR-024 | [cloud-history-reads.md](cloud-history-reads.md) |
| 榜单 | 2 | `charts(kind)` | FR-015 | [chart-reads.md](chart-reads.md) |
| 六项歌单发现 | 6 | `recommendedPlaylists/playlistList/premiumPlaylists/playlistTags(kind)` | FR-010/015 | [playlist-discovery-reads.md](playlist-discovery-reads.md) |
| 实体／播客／声音搜索 | 5 | `searchAlbums/searchArtists/searchPlaylists/searchPodcasts/searchVoices` | FR-006/044 | [entity-search-reads.md](entity-search-reads.md) |
| 播客消费／四类recent | 7 | `podcastDetails/podcastPrograms/programDetails/recentPlaylists/recentAlbums/recentPodcasts/recentVoices` | FR-024/044 | [podcast-and-recent-reads.md](podcast-and-recent-reads.md) |
| 评论读取 | 8 | `comments(kind)/hotComments/sortedComments/commentFloor/commentInfo` | FR-030 | [comment-reads.md](comment-reads.md) |
| 播客订阅／云盘只读 | 3 | `subscribedPodcasts/cloudSongs/cloudSongDetails` | FR-028/044 | [account-library-reads.md](account-library-reads.md) |
| 五类收藏订阅写 | 8 | `mutateCollection(typed action)` | FR-009/012/013/014/044 | [collection-writes.md](collection-writes.md) |
| 歌单基础写 | 3 | `mutateCollection(Create/Rename/Description)` | FR-011 | [playlist-management-writes.md](playlist-management-writes.md) |
| 删除／完整排序 | 3 | `mutateCollection(Delete/SongOrder/AccountOrder)` | FR-011/012 | [playlist-delete-and-order-writes.md](playlist-delete-and-order-writes.md) |
| 艺人介绍／歌曲百科基础 | 2 | `artistIntroduction/songWikiSummary` | FR-008/009 | [catalog-introduction-and-metadata-reads.md](catalog-introduction-and-metadata-reads.md) |
| 标签／单向公开 | 2 | `mutateCollection(Tags/MakePublic)` | FR-011 | [playlist-tags-and-publication-writes.md](playlist-tags-and-publication-writes.md) |
| 封面分配／提交 | 2 | `mutateCollection(Cover)，显式内存JPEG` | FR-011 | [playlist-cover-update.md](playlist-cover-update.md) |
| 评论五动作写 | 5 | `mutateComment(typed action)` | FR-030/031 | [comment-writes.md](comment-writes.md) |
| 搜索辅助 | 3 | `searchSuggestions/hotKeywords/hotKeywordDetails` | FR-007 | [search-assistance-reads.md](search-assistance-reads.md) |
| 每日推荐歌单 | 1 | `dailyRecommendedPlaylists` | FR-015 | [daily-recommended-playlists.md](daily-recommended-playlists.md) |
| 历史日推 | 2 | `dailyRecommendationHistoryDates/dailyRecommendationHistorySongs` | FR-015 | [daily-recommendation-history.md](daily-recommendation-history.md) |
| Together HTTP-only | 12 | `requestTogether(12 typed actions)` | FR-043 | [together-http-core.md](together-http-core.md) |
| 常规发现新碟／艺人／相似 | 9 | `discover(DiscoveryBrowseQuery)` | FR-008/009/010/015 | [discovery-browse-reads.md](discovery-browse-reads.md)／[公开响应与修正](discovery-public-response-verification.md) |
| 歌曲百科受限纯文本 | 1 | `songWikiText(SongWikiTextQuery)` | FR-008 | [song-wiki-text-subset.md](song-wiki-text-subset.md)，内容身份未独立验证 |

## 3. 已实现的非registry链与结构：不要误记为缺API

| 项目 | 当前实际边界 | 仍未完成／不据此推断 |
| --- | --- | --- |
| XEAPI公钥注册／会话密钥、QR URL本地构造、设备ID、Cookie epoch与两阶段认证 | 独立registrar／vault／auth链，非106枚举中的每一步；QR803仍需身份与原收据 | QR图形/UI、其他登录／真实账号和keyring／跨发行版验收 |
| Comment v3前置GET | 明确获准的窄原生issuer，不执行JS／SDK，发布／回复两阶段 | 不是v2替代，不是完整远端审核／所有权证明 |
| JPEG重编码／NOS explicit POST | Gui仅获准基础设施；分配/提交2operations外还有显式上传阶段 | 未真实上传，不推创建对账／无孤儿副作用 |
| 受控CDN缓冲／Qt Multimedia／单owner | Song上下文先验再下沉无身份MediaSource；限域、TTL、preview、完整临时缓冲 | 非下载文件功能、非流式起播、非Program/Voice/cloud可信绑定 |
| 队列／六轨歌词时钟／FM连续来源／观测历史 | Core逻辑与原Facade，非每个本地动作新增HTTP；observed time与provider history分开 | 持久队列／音乐库缓存／可靠对账／自动报告／声学验收未成 |
| PlaybackEntity／AudioTrackId／MediaSource共享接缝 | [结构准备](spoken-media-seam.md)保留唯一Song验证owner | 类型不是绑定／权益／实际spoken入口，不开第二播放器 |

## 4. 需求覆盖与剩余产品缺口

下表逐FR列组件边界；涉及UI、实网、完整schema或远端效果的FR**不整体标完成**。依据[当前需求](../01-product-requirements.md)与各API契约，不把旧wrapper输入当官方完整schema。

| FR | I／S已落地组件 | G／P／X尚未完成或限制 |
| --- | --- | --- |
| 001 | 封闭native registry、协议／HTTP／decoder、无用户代理端点 | 广泛服务兼容／连接诊断产品体验、发行安装仍待验收 |
| 002 | anonymous bootstrap、Guest查询和原Song链 | 游客浏览UI／本地文件播放完整流程未接 |
| 003 | QR key/check／身份验证／两阶段凭据交接／deadline | QR图形与登录UI、其他登录方式未接；真实账号另授权 |
| 004 | Cookie epoch、记住／恢复／signOut、原账号切换屏障／close drain | 真实keyring／跨设备／crash与统一deadline未证 |
| 005 | 验证AccountIdentity与SessionSnapshot | 当前账号等级／收藏计数／更完整权益未接；身份／VIP不是所有歌曲权限 |
| 006 | Song/Album/Artist/Playlist/Podcast/Voice typed搜索与有界页 | IME／高亮／UI；scoped playlist search仍待补证，不拿全局搜索冒充 |
| 007 | 实体建议、简略/详细热搜3operations | 持久搜索历史／清除／UI未成；mobile/PC/default/multi-match不自动扩入 |
| 008 | Song详情／optional metadata／百科基础文本 | 跳转UI／复杂百科、完整制作署名、缺图体验未成 |
| 009 | Album/Artist详情、songs/albums/介绍、订阅写；新碟/艺人索引/热门/独立top50与SimilarArtists typed读 | 真实schema/期次/排名/全部艺人库与权益/UI未验收；top50非全部songs，完整署名/复杂百科仍G |
| 010 | Playlist详情/ID页组合歌曲、当前账号库与有界完整性区分 | 完整库缓存／服务全量／权限证明、只读编辑UI未成 |
| 011 | 创建／文本／删除／完整排序／标签／JPEG封面／单向公开 | 真实编辑权/效果、创建对账/UI未证；无反向私密转换／标签清空扩展 |
| 012 | 加/移歌曲与完整来源置换排序 | 可靠部分结果／并发CAS／自动对账、UI批量交互未成 |
| 013 | LikedSongIds与SongHeart独立动作 | 收藏到歌单≠红心，未做持久缓存／自动乐观更新或真实效果验收 |
| 014 | 专辑／艺人sub/unsub与列表 | **P：整歌单订阅v2**，原生issuer/许可/绑定未闭；不fake unavailable注册 |
| 015 | 日推Song／独立日推Playlist／history member日期／普通推荐／榜单；新碟显式年月分组/三类相似读 | 日期cache／服务window／完整性／会员与UI未证；dislike feedback未接 |
| 016 | FM批次／single consumer／有界连续播放 | FM mode／explicit trash反馈未接；next≠不喜欢，不能伪默认听歌时长 |
| 017 | 单Song play/pause/resume/stop/seek/volume，原unique owner | UI／设备／长期播放／声学验收、spoken/cloud模式未成 |
| 018 | quality与actual、URL safety／TTL／preview限制 | 历史仅有限试听证据，不能推full paid/FLAC/全部账户权益 |
| 019 | Core队列增删／顺序／有限导航 | save/restore持久化／拖拽UI未成 |
| 020 | 顺序／循环／随机历史及FM独占 | 重复/失败合成证据非长期声学验收，UI未成 |
| 021 | 逐行原文／翻译／罗马音，时钟定位 | 点击/手动滚动/恢复跟随属于UI，尚未接 |
| 022 | 三种逐字轨与独立游标，保真实六轨 | 对唱/背景样式与UI不从未知标记或合成轨伪造 |
| 023 | 单owner/快照可供UI消费 | **UI最后**；空模板，无沉浸页/迷你栏 |
| 024 | observed会话历史＋provider recent/ranking，显式报告 | 持久历史／清理产品范围／自动报告时机与新增来源未成 |
| 025 | 可复用原实际可听Song音源与安全限制 | 文件下载／Range持久任务／磁盘/恢复未成；**X：不加专用下载资格/音源API** |
| 026 | 原preview/TTL/access与临时文件政策 | 离线索引／迁移／删除／期限恢复未成，临时缓冲不是下载 |
| 027 | 仅基础媒体接缝 | 本地目录授权／导入／标签／扫描／去重／失效未接，不能将file URL塞入当前CDN policy |
| 028 | 原账号CloudSong列表/详情、独立CloudSongId | **P：cloud可信音源/播放后续**，simpleSong不自动绑定 |
| 029 | 仅静态来源审计 | **P：cloud上传/删除/匹配等后续** |
| 030 | 8评论读取operations与5typed资源的窄支持、reaction写 | 真实schema/UI/账号评论history未证；Voice/Artist不造绑定，通知不顺带纳入 |
| 031 | 显式add/reply/delete与原账号/UUID/Unknown | 审核/作者权限/真实删除效果/UI未证，无创建ID猜测/auto retry |
| 032 | 编号保留 | **X：视频/MV明确排除**，不因旧inventory模块或计数出现而恢复 |
| 033 | 分层／原队列及六轨lyrics可复用（非媒体适配已成） | I/S：[B-031 O1](../tasks/B-031-playback-observation.md)共同Core观测/接缝/窄桥；G：Linux MPRIS／Windows SMTC／macOS Now Playing及本机只读IPC，native/DBus依赖、peer权限及设备/三平台未验，不暗加依赖或新owner |
| 034 | Runtime异步生命周期/后台close | 托盘／通知／窗口关闭体验未成，桌面环境另验收 |
| 035 | 仅需求 | 应用/全局快捷键、桌面差异未成 |
| 036 | 原lyrics clock可复用 | P2桌面歌词窗未成，Wayland能力另验收 |
| 037 | 仅需求/ADR | UI/A11y/主题/缩放/键盘/Orca未成 |
| 038 | 原backend config/typed quality/volume | 设置后端持久化与页、设备/下载/隐私/语言未成 |
| 039 | generation、取消、TTL与关闭保护 | 队列/进度持久恢复、设备/休眠/失效恢复未成 |
| 040 | 有界typed errors、四写结果、cancel/timeout/close | UI通用错态、平台完整恢复不由Ack/空列表代替 |
| 041 | 仅需求 | 本地歌单导入导出／分享URI白名单解析未成 |
| 042 | 仅基础播放参数 | P2 timer/DSP/倍速/gapless/crossfade各自未验收 |
| 043 | **I/S：核心12 HTTP-only，部分metadata** | **P/G：完整sync/NIM/SELFHOST/G1–G6**；optional发送/playlist完整schema/profile兼容仍缺证；Ack非Active |
| 044 | typed消费搜索/读取/recent/订阅、media-seam stage1 | **P：下一阶段Program/Voice↔track/parent/context/v1/rights与logical播放**；-462窗口不自动重开 |

### L：只能引用的历史有限观察

- [歌曲搜索/详情/歌词公开小样本](music-queries-and-lyrics.md)、[实体20公开搜索](entity-search-reads.md)与[消费3探测](podcast-and-recent-reads.md)分别有原窗口边界；program-list出现-462已停，拒绝不是支持证明。
- [官方SongStream与临时静音试听PoC](stream-resolution-and-playback.md)证明的是当时特定MP3 preview/TTL与操作观察，不是full paid/spoken/cloud/实时同步/全部音质或声学。
- 其他原账号读取、写入效果、一起听host/profile/双端等没有因此获得实网证明；本轮不重开窗口。

## 5. 剩余HTTP候选：不能按旧adopt列表机械补齐

本表是缺口族，不声称164模块逐个都是必须新增的独立operation。具体单包仍先写闭合合同／local synthetic schema与预算。

| 族 | 代表性固定候选／差异 | 当前处理 |
| --- | --- | --- |
| 账号只读 | user_level／user_subcount；另user_detail/vip_info_v2 | level/subcount最适合下一小包；更广user/VIP需独立schema/权益语义，不跨账号profile或infer playable |
| 发现后续／动态实体／样式／复杂metadata | album/artist/playlist动态、style_*、creators/wiki复杂部分及mixed homepage | 本轮9常规发现已接、不再作为未实现候选；旧partial及typed9不宣称全覆盖。complexWiki只request事实、root/blocks/绑定门禁Q-016；mixed block过滤视频/广告/AI/commerce，不做raw JSON Manager |
| FM mode／负反馈 | personal_fm_mode=Eapi同path但mode/subMode/limit；fm_trash=Weapi/time/alg | mode不是本地PlayMode；trash必须显式原FM来源/账号/真实观测time，无固定25、auto next-trigger或自动队列操作 |
| 日推dislike | recommend_songs_dislike=Weapi resId/resType4/sceneType1 | 原日期/批次成员来源、原token与四结果另设计，不默认自动刷新、伪历史来源 |
| 评论history/举报 | user_comment_history/comment_report | 未接；账号history消耗/隐私和举报明确写合同，不顺带通知、hug或社交 |
| 未接登录/权益分支 | refresh/server logout、VIP等 | QR/restore/local signOut已有；手机验证码／手机密码／邮箱密码登录与发送验证码已按维护者要求接入（[credential-login.md](credential-login.md)，2026-11-07）；不补refresh/remote logout或将Cookie=auth |
| 搜索替代分支 | cloudsearch/search_default/search_match/search_suggest_pc；scoped playlist未证 | 原封闭search/suggestion已有；mobile/PC/default/multi-match目前不自动扩入，scoped仍待补证 |
| lyrics/url/availability旧分支 | lyric旧版、song_url、check_music、download相关6模块 | 已有新lyrics/XEAPI实际source；不legacy downgrade/资格探测/forwarding/unlocking或假版权 |
| playlist包装/组合 | playlist_track_all、track_add/delete/tracks、合并update/playcount | playlistSongs与多型mutation已有组合/显式动作；不能把loaded page当全量或伪上报来源/atomic |
| whole playlist/cloud/spoken/Together full | v2 token；cloud写/track；消费binding；IM等 | **P/G原暂停继续**，不因此恢复SDK/host fallback/API或补假入口 |

## 6. Together HTTP收尾缺口：不当作可直接连播放器的小包

| 现有事实 | 剩余G／P | 安全下一步 |
| --- | --- | --- |
| 全12标准Eapi-ECB/窄host，保PC metadata | 文档host+PC compatibility profile未实网证明；摘要CBC文字不一致 | 合法source片段/合成向量先核对；不改crypto/globalhost或试fallback |
| heartbeat仅roomId/songId/status/progress | playlistVersion发送扩展未提供typed入口 | 与account/version来源及精确JSON模型一起补证；无固定heartbeat timer |
| play仅六字段commandInfo，6种command | triggerType/userId/ignoreUserId/serverSeq发送扩展的取值、来源、权限未全闭 | 区分发送单数/接收复数，先合成场景；不从任意JSON解包成应用动作 |
| REPLACE双list+原账号单version元素 | 全成员version向量、CAS/并发/reset不明 | counter是caller值，别宣称server CAS或自动冲突覆盖 |
| Queue=data.playList、部分playCommand、playlistContentUnmapped | 完整playlist schema与空/版本/重复/异常/大小样本缺 | 请求**合成response**；无证不造songs、播放队列、root/大小写alias |
| Room/Status/Check/End/Reconnect/Device部分metadata | NIM/SELFHOST/auth/token/envelope/seq/time/permissions/Linux许可G1–G6 | 完整同步仍暂停；HTTP Ack/inRoom/canReconnect非Active/同步 |
| local roomGeneration及token/account/UUID相关 | 不是自动房间切换/全设备同步fence | caller显式cancel旧意图，原unique playback owner保持不动 |

APK许可撤回且从未读/hash/检查/解包/执行。本轮不访问它；资料补证只接受有权分享的必要片段/合成样本，不要求真实Cookie/token/room成员response/设备指纹/音频。固定stash **`9907ac4a9ad83b9cefb79fea9d3337b56b91ba6f`**仍保存、不pop/drop/apply。

## 7. 推荐下一组：当前账号等级／收藏计数两只读

**推荐进入独立契约/合成实现，不是本审计已实现或已授权实网。** 两接口已在原consumer/account采用候选中，空业务Weapi、无动态path/外部issuer、无需NIM/SDK/可信音轨绑定；可继续复用原verified account/MUSIC_U/read16/cancel-close/same-token屏障，不新增owner或依赖。

| 排序 | 候选 | 固定源码事实 | 提议的后续闭合范围 |
| --- | --- | --- | --- |
| 1a | user_level | Weapi `/api/user/level`，empty data；home.md541–547明确需登录，包含等级/登录天数/听歌次数/升级进度；TS1524只有Promise<Response> | 仅当前verified账号等级摘要/原计数与optional未知；不得把等级、nextLevel或听歌次数当会员/可播权/真实观测时长 |
| 1b | user_subcount | Weapi `/api/subcount`，empty data；home.md533–539说明当前登录账号数量；TS1539通用Response | 仅消费音乐收藏/歌单/艺人/专辑/播客相关候选计数；filter MV/creator/social，不用计数推完整页/编辑权/自动拉全库 |
| 2 | FM mode/negative feedback | mode/subMode/limit与time字段已有源码线索 | 原FM来源/time/permissions需独立契约；不比empty账号读更先，不自动接入FM链 |

固定module hash：user_level `9bf79e55bdb36a53f721fbc5b6aa022e9ed6298282571bbcd2709ec217c716b7`；user_subcount `641b2a812e03ffd249035f679117ddbe37a13b3364a804eb34cdd754ea45f888`；album_newest `3419a68a5f137a0f9142b3d6d9f8bf599d3bd7cf7d65124b2d7ab3ecd90761ce`。这是固定参考身份，不是service schema proof。user_level.js首行错误残留“类别热门电台”，**以实际path/body与home描述判定account read，不据注释把它作Podcast接口**。

尚未得到完整响应schema；TS通用Response与home概述不能补出真实root/data、计数精度、会员/权限或freshness。后续先拟严格synthetic mapper/预算/invalid-empty-missing边界及private context，合成成功单列、不开放UI或实网。需要进入实现时再明确单包选择，不默默将本轮盘点升级成API写操作。

## 8. 初始53f8f3e审计轮静态验证（历史）

以下只记录原97/48/24groups的docs-only时点，不是后续发现代码包；§9另列当前证据。

- 原ledger离线sanity-check：**97唯一名称/顺序＝7b0d762 enum；24family计数总和97；38E/54W/5X；每path literal/模板prefix可在当前root/窄adapter定位，每契约文件存在**。不是逐operation调用、参数可用性、完整schema、动态path实例或live证明。
- 当前LSP可定位root operationContract；review graph自报旧8485f760，不能用其refs当7b0d762覆盖。部分C++ member symbol查找未返回，已直接Read当前Facade体，不把“查不到symbol”当方法不存在。task92旧cpp-check output parse仍单独pending。
- 97operations/48CTest/together1元数据实核；源码、CMake/registry/fixtures、固定lock/inventory、入口/QML不改。**没有本轮Qt测试/build/新LSP diagnostics/actual app dependencies/SDK/服务/keyring/音频/UI验证**。
- [Together包](together-http-core.md)的109/911＋runtime17、16fresh/6992deps只属7b0d762历史；日推history839＋15等只属此前包，不累加成全48/Release/产品验收。
- 本页与HANDOFF/路线图/indexes的静态交付检查采用fixed440/public hash＋Python清单测试、diff、本地reference路径和有限Cookie/signed-query扫描；最终计数在HANDOFF记录。脚本/ledger中间产物与日志在ignored `build/workspace-checks/backend-coverage-*`，非原材料/私有response。

## 9. 常规发现包更新与验证边界

后续维护者优先选择新碟/艺人/相似9Weapi与complexWiki审计；该包331b4fb／6f63c24时点106唯一enum/49show-only登记、25family合计106、38E63W5X。ledger名称顺序与当前enum、path literal/模板prefix与root/五窄adapter/契约路径静态对应，不证明每个参数/schema/live分支可用。

新165／22affected Debug1167＋15选runtime函数16data/18pass，0fail/skip；最终真实build0/warnings0、15fresh all-server C++LSP0/format15、14实际application对象（含MOC）6998Ninja deps禁入0/entry-QML0。fixed440/21referencehash0/Python15与最终worktree-staged diff/local refs/有限literal-query扫描另记HANDOFF，ignored discovery-browse-*；非anchors或全面安全。first fixture缺直接include/append overload、MOC新增/改名过期经真实include/AUTOGEN修，不删Q_OBJECT/.moc/坏schema/fatal，#92旧parse仍另记。没有完整49/Release/minQt/账号/服务/SDK/keyring/audio/UI或权益验收，不累加其他包或first123/newthread3。

以上为331b4fb初始合成包历史。后续公开窗口原native11次metadata：搜索2／complexWiki1／发现8；前10次HTTP/code200，其中原mapper2拒绝，SimilarArtists第11次code301即停，SimilarPlaylists未发。L分别为5个发现mapper接受、2个发现仅成功结构观察、1个业务301拒绝；未发仍G，不把L当完整兼容。月榜optional分组／相似歌曲100有界未知分页批修正后，离线188／22affected1190＋15选runtime18，0fail/skip，5fresh all-server0／14实对象6998逐对象-601唯一deps禁入0／UI0，未修后重发实网。只增加这个公开窗口L，不倒填旧audit或§9历史tests。

complexWiki仅data.blocks6/rnData前2块key/type观察，原Song绑定／块类及其余语义仍G/未注册，旧summary不套另path；常规9无自动下游/Owner/运行库。原v2/cloud/spoken-next/fullTogether暂停、播客−462停止、撤权APK禁读/stash原样保持。账号等级+计数仍推荐候选，未接入或获账号实网授权。

## 10. 受限歌曲百科增量（本轮）

[受限子集](song-wiki-text-subset.md)新增SongWikiText Eapi／原来源factory／read16／Facade关联，107 enum/50登记/26family/39E63W5X，44FR不增。四次另行确认metadata读只补结构与描述匹配；两文本子块 I/S、独立身份与完整百科 G。无后实现live、账号/SDK/媒体/暂停重开。

本轮百科88／23affected Debug1278＋16选runtime函数17data/init-cleanup19，0fail/skip；18 changed+邻接fresh all-server LSP0／13 format0，14实际application对象含MOC7004逐对象/602唯一VALID Ninja deps禁入0/UI0。旧§9测试不累加；非完整50/Release/minQt/服务/权益验收。失败日志和修因、固定440/Python15/文档有限检查见契约/HANDOFF，#92仍独立。

## 11. 播放观测／Linux参数化真实I/O已限定I/S，发行端点与系统媒体未完成

维护者明确两个账号等级/计数API先不做；[B-031](../tasks/B-031-playback-observation.md)设计后按继续选择接O1共同Core观测/接缝/窄桥，限定I/S证据见其§9.1（新suite37、12affected228＋runtime6、音频skip1）。44FR/107operations/26families/39E63W5X不变；O1历史51，后续O2a当前52登记非全52验收。§9.2纯Core基础设施codec/framer/golden/固定错误/六轨分页限定I/S：125新、5selected216＋runtime6、0fail/skip，11fresh0。后续O2b §9.3逻辑连接/mailbox/分步编码/订阅/退役/背压fake限定I/S：43新、6selected259＋runtime6，0fail/skip；O2b时53登记非全53。fake分块consumer不是server，真实IPC/peer及MPRIS/SMTC/macOS仍G，无新平台依赖、native运行、实际设备效果或服务窗口；原设计50登记/文档检查保历史，不倒填。O2只便携逻辑/fake退出条件限定I/S、非产品端点；真实owner组合与O3–O6分别验，UI最后/paused保持。

最新O3b2 [§9.6](../tasks/B-031-playback-observation.md)参数化Linux owner→真实单I/O线程socket、严格私有发现/双向UID/hello关联与显式XDG选择限定I/S：新50/Debug11selected361＋runtime6/Release8selected307、0fail/skip，58登记非全58。八慢client/input/retained＋native pending/wake/隐私/期限/thread关闭已验；application无JSON/Network/infra反向边。default off/无产品接线，发行namespace/另一真实OS用户负测/沙箱/minQt/部署性能与adapter仍G，107operations/26families/44FR不增、#92独立未闭，无新服务L窗口/NetEase请求/UI/commit/push。此前O3b1 §9.5的20/311＋6为历史子包，不相加。

此前O3a §9.4已有Linux私有资源/kernel SO_PEERCRED primitive与隔离合成harness：32新/3selected200、0fail/skip，双向native UID＋fork跨process同user正测、0700/0600与nofollow/inode/exclusive/仅自身清理。当前Linux54登记非全54，业务ledger107/26/44不变；这是临时本机filesystem listener/resource测试，不是服务L窗口、音乐状态server或另一OS user负测/发行支持。正式app ID/XDG discovery消费者、Core private barrier组合、真实bounded调度/buffers/生命周期仍G，O3未完成；MPRIS/SMTC/macOS/沙箱/Release/minQt/device仍G，无平台依赖安装/NetEase请求/产品入口/UI/commit/push，#92未闭，旧包证据不累加。

## 附录A. 当前107-operation逐项ledger

协议仅来自当前合同，和wire/form/encrypted body/host/profile是不同层次。含`{…}`的path是**封闭模板注记，不是产品开放动态输入**；仍必须用adapter的规范ID/resource与payload验证。详见§2组内实际entry与契约；表中存在不代表每个参数/资源/response分支均已live或产品可用。

### 认证／匿名启动（4）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `QrKey` | E | `/api/login/qrcode/unikey` |
| `QrCheck` | E | `/api/login/qrcode/client/login` |
| `AccountIdentity` | W | `/api/w/nuser/account/get` |
| `AnonymousRegister` | X | `/api/register/anonimous` |

### 歌曲查询／歌词／音源（4）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `SearchSongs` | E | `/api/search/get` |
| `SongDetails` | W | `/api/v3/song/detail` |
| `SongLyrics` | E | `/api/song/lyric/v1` |
| `SongStream` | X | `/api/song/enhance/player/url/v1` |

### 五项目录（5）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `AlbumDetails` | W | `/api/v1/album/{id}` |
| `ArtistDetails` | E | `/api/artist/head/info/get` |
| `ArtistSongs` | E | `/api/v1/artist/songs` |
| `ArtistAlbums` | W | `/api/artist/albums/{id}` |
| `PlaylistDetails` | E | `/api/v6/playlist/detail` |

### 每日歌曲／FM条目（2）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `DailyRecommendations` | W | `/api/v3/discovery/recommend/songs` |
| `PersonalFm` | W | `/api/v1/radio/get` |

### 当前账号四库（4）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `MyPlaylists` | W | `/api/user/playlist` |
| `LikedSongIds` | E | `/api/song/like/get` |
| `SavedAlbums` | W | `/api/album/sublist` |
| `SavedArtists` | W | `/api/artist/sublist` |

### 显式听歌报告（2）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `ListeningStart` | E | `/api/feedback/weblog` |
| `ListeningPlay` | E | `/api/feedback/weblog` |

### 云端歌曲历史／排行（2）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `RecentSongs` | W | `/api/play-record/song/list` |
| `ListeningRanking` | W | `/api/v1/play/record` |

### 榜单（2）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `Charts` | E | `/api/toplist` |
| `ChartsWithPreviews` | W | `/api/toplist/detail` |

### 六项歌单发现（6）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `RecommendedPlaylists` | W | `/api/personalized/playlist` |
| `PlaylistList` | W | `/api/playlist/list` |
| `PremiumPlaylists` | W | `/api/playlist/highquality/list` |
| `PlaylistCatalogue` | E | `/api/playlist/catalogue` |
| `HotPlaylistTags` | W | `/api/playlist/hottags` |
| `PremiumPlaylistTags` | W | `/api/playlist/highquality/tags` |

### 实体／播客／声音搜索（5）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `SearchAlbums` | E | `/api/search/get` |
| `SearchArtists` | E | `/api/search/get` |
| `SearchPlaylists` | E | `/api/search/get` |
| `SearchPodcasts` | E | `/api/search/voicelist/get` |
| `SearchVoices` | E | `/api/search/voice/get` |

### 播客消费／四类recent（7）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `PodcastDetails` | W | `/api/djradio/v2/get` |
| `PodcastPrograms` | W | `/api/dj/program/byradio` |
| `ProgramDetails` | W | `/api/dj/program/detail` |
| `RecentPlaylists` | W | `/api/play-record/playlist/list` |
| `RecentAlbums` | W | `/api/play-record/album/list` |
| `RecentPodcasts` | W | `/api/play-record/djradio/list` |
| `RecentVoices` | W | `/api/play-record/voice/list` |

### 评论读取（8）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `SongComments` | W | `/api/v1/resource/comments/{Song资源ID}` |
| `AlbumComments` | W | `/api/v1/resource/comments/{Album资源ID}` |
| `PlaylistComments` | W | `/api/v1/resource/comments/{Playlist资源ID}` |
| `ProgramComments` | W | `/api/v1/resource/comments/{Program资源ID}` |
| `HotComments` | W | `/api/v1/resource/hotcomments/{封闭资源ID}` |
| `SortedComments` | E | `/api/v2/resource/comments` |
| `CommentFloor` | W | `/api/resource/comment/floor/get` |
| `CommentInfo` | W | `/api/resource/commentInfo/list` |

### 播客订阅／云盘只读（3）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `SubscribedPodcasts` | W | `/api/djradio/get/subed` |
| `CloudSongs` | W | `/api/v1/cloud/get` |
| `CloudSongDetails` | W | `/api/v1/cloud/get/byids` |

### 五类收藏订阅写（8）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `SongHeart` | W | `/api/radio/like` |
| `PlaylistCollectSongs` | E | `/api/playlist/manipulate/tracks` |
| `AlbumSubscribe` | W | `/api/album/sub` |
| `AlbumUnsubscribe` | W | `/api/album/unsub` |
| `ArtistSubscribe` | W | `/api/artist/sub` |
| `ArtistUnsubscribe` | W | `/api/artist/unsub` |
| `PodcastSubscribe` | W | `/api/djradio/sub` |
| `PodcastUnsubscribe` | W | `/api/djradio/unsub` |

### 歌单基础写（3）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `PlaylistCreate` | W | `/api/playlist/create` |
| `PlaylistRename` | E | `/api/playlist/update/name` |
| `PlaylistDescription` | E | `/api/playlist/desc/update` |

### 删除／完整排序（3）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `PlaylistDelete` | W | `/api/playlist/remove` |
| `PlaylistSongOrder` | E | `/api/playlist/manipulate/tracks` |
| `AccountPlaylistOrder` | W | `/api/playlist/order/update` |

### 艺人介绍／歌曲百科基础（2）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `ArtistIntroduction` | W | `/api/artist/introduction` |
| `SongWikiSummary` | E | `/api/song/play/about/block/page` |

### 标签／单向公开（2）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `PlaylistTagsUpdate` | E | `/api/playlist/tags/update` |
| `PlaylistMakePublic` | E | `/api/playlist/update/privacy` |

### 封面分配／提交（2）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `CoverAllocate` | W | `/api/nos/token/alloc` |
| `PlaylistCoverCommit` | W | `/api/playlist/cover/update` |

### 评论五动作写（5）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `CommentLike` | W | `/api/v1/comment/like` |
| `CommentUnlike` | W | `/api/v1/comment/unlike` |
| `CommentPublish` | X | `/api/resource/comments/add` |
| `CommentReply` | X | `/api/v1/resource/comments/reply` |
| `CommentDelete` | X | `/api/resource/comments/delete` |

### 搜索辅助（3）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `SearchSuggestions` | W | `/api/search/suggest/web` |
| `HotKeywords` | E | `/api/search/hot` |
| `HotKeywordDetails` | W | `/api/hotsearchlist/get` |

### 每日推荐歌单（1）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `DailyRecommendedPlaylists` | W | `/api/v1/discovery/recommend/resource` |

### 历史日推（2）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `DailyHistoryDates` | W | `/api/discovery/recommend/songs/history/recent` |
| `DailyHistorySongs` | W | `/api/discovery/recommend/songs/history/detail` |

### Together HTTP-only（12）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `TogetherCreate` | E | `/api/listen/together/room/create` |
| `TogetherCheck` | E | `/api/listen/together/room/check` |
| `TogetherJoin` | E | `/api/listen/together/play/invitation/accept` |
| `TogetherStatus` | E | `/api/listen/together/status/get` |
| `TogetherEndCheck` | E | `/api/listen/together/end/check` |
| `TogetherEnd` | E | `/api/listen/together/end/v2` |
| `TogetherHeartbeat` | E | `/api/listen/together/heartbeat` |
| `TogetherPlay` | E | `/api/listen/together/play/command/report` |
| `TogetherQueue` | E | `/api/listen/together/sync/playlist/get` |
| `TogetherReplaceQueue` | E | `/api/listen/together/sync/list/command/report` |
| `TogetherReconnectInfo` | E | `/api/listen/together/restore/reconnect/info` |
| `TogetherDeviceNotice` | E | `/api/listen/together/device/reconnect/notice` |

### 常规发现新碟／艺人／相似（9）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `LatestAlbums` | W | `/api/discovery/newAlbum` |
| `NewAlbums` | W | `/api/album/new` |
| `AlbumCharts` | W | `/api/discovery/new/albums/area` |
| `ArtistBrowse` | W | `/api/v1/artist/list` |
| `PopularArtists` | W | `/api/artist/top` |
| `ArtistTopSongs` | W | `/api/artist/top/song` |
| `SimilarSongs` | W | `/api/v1/discovery/simiSong` |
| `SimilarArtists` | W | `/api/discovery/simiArtist` |
| `SimilarPlaylists` | W | `/api/discovery/simiPlaylist` |

### 歌曲百科受限纯文本（1）

| Operation | 协议 | logical path模板 |
| --- | --- | --- |
| `SongWikiText` | E | `/api/link/page/parent/relation/construct/info` |
